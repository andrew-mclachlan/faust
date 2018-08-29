/************************************************************************
 FAUST Architecture File
 Copyright (C) 2017 GRAME, Centre National de Creation Musicale
 ---------------------------------------------------------------------
 This Architecture section is free software; you can redistribute it
 and/or modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 3 of
 the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; If not, see <http://www.gnu.org/licenses/>.

 EXCEPTION : As a special exception, you may create a larger work
 that contains this FAUST architecture section and distribute
 that work under terms of your choice, so long as this FAUST
 architecture section is not modified.
 ************************************************************************/

#ifndef __Soundfile__
#define __Soundfile__

#include <iostream>

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

#define BUFFER_SIZE 1024
#define SAMPLE_RATE 44100
#define MAX_CHAN 64
#define MAX_PART 256

#ifdef _MSC_VER
#define PRE_PACKED_STRUCTURE __pragma(pack(push, 1))
#define POST_PACKED_STRUCTURE \
    ;                         \
    __pragma(pack(pop))
#else
#define PRE_PACKED_STRUCTURE
#define POST_PACKED_STRUCTURE __attribute__((__packed__))
#endif

/*
 The soundfile structure to be used by the DSP code. Soundfile has a MAX_PART parts 
 (even a single soundfile or an empty soundfile). 
 fLength, fOffset and fSampleRate field are filled accordingly by repeating 
 the actual parts if needed.
 
 It has to be 'packed' to that the LLVM backend can correctly access it.

 New index computation:
    - p is the current part number [0..MAX_PART-1] (must be proved by the type system)
    - i is the current position in the part. It will be constrained between [0..length]
    - idx(p,i) = fOffset[p] + max(0, min(i, fLength[p]));
*/

PRE_PACKED_STRUCTURE
struct Soundfile {
    FAUSTFLOAT** fBuffers;
    int fLength[MAX_PART];      // length of each part
    int fOffset[MAX_PART];      // offset of each part in the global buffer
    int fSampleRate[MAX_PART];  // sample rate of each part
    int fChannels;              // max number of channels of all concatenated files

    Soundfile()
    {
        fBuffers  = NULL;
        fChannels = -1;
    }

    ~Soundfile()
    {
        // Free the real channels only
        for (int chan = 0; chan < fChannels; chan++) {
            delete fBuffers[chan];
        }
        delete[] fBuffers;
    }

} POST_PACKED_STRUCTURE;

/*
 The generic soundfile reader.
 */

class SoundfileReader {
   protected:
    void empty(Soundfile* soundfile, int part, int& offset, int max_chan)
    {
        std::cout << "empty_sound" << std::endl;
        soundfile->fLength[part] = BUFFER_SIZE;
        soundfile->fOffset[part] = offset;
        soundfile->fSampleRate[part] = SAMPLE_RATE;
     
        // Update offset
        offset += soundfile->fLength[part];
    }

    Soundfile* create(int cur_channels, int length, int max_chan)
    {
        Soundfile* soundfile = new Soundfile();
        if (!soundfile) {
            throw std::bad_alloc();
        }
        
        soundfile->fBuffers = new FAUSTFLOAT*[max_chan];
        if (!soundfile->fBuffers) {
            throw std::bad_alloc();
        }
        
        for (int chan = 0; chan < max_chan; chan++) {
            soundfile->fBuffers[chan] = new FAUSTFLOAT[length];
            if (!soundfile->fBuffers[chan]) {
                throw std::bad_alloc();
            }
            memset(soundfile->fBuffers[chan], 0, sizeof(FAUSTFLOAT) * length);
        }
        
        soundfile->fChannels = cur_channels;
        return soundfile;
    }
    
    void getBuffersOffset(Soundfile* soundfile, FAUSTFLOAT** buffers, int offset)
    {
        for (int chan = 0; chan < soundfile->fChannels; chan++) {
            buffers[chan] = &soundfile->fBuffers[chan][offset];
        }
    }
    
    virtual void readOne(Soundfile* soundfile, const std::string& path_name, int part, int& offset, int max_chan) = 0;
  
    virtual std::string checkAux(const std::string& path_name) = 0;
    
    virtual void open(const std::string& path_name, int& channels, int& length) = 0;
    
   public:
    virtual ~SoundfileReader() {}
    
    Soundfile* read(const std::vector<std::string>& path_name_list, int max_chan)
    {
        try {
            int cur_chan = 0;
            int total_length = 0;
            
            // Compute total length and chan max of all files
            for (int i = 0; i < path_name_list.size(); i++) {
                int chan, length;
                if (path_name_list[i] == "__empty_sound__") {
                    length = BUFFER_SIZE;
                    chan = 1;
                } else {
                    open(path_name_list[i], chan, length);
                }
                cur_chan = std::max(cur_chan, chan);
                total_length += length;
            }
            
            // Complete with empty parts
            total_length += (MAX_PART - path_name_list.size()) * BUFFER_SIZE;
            
            std::cout << "read total_length " << total_length << " " << "cur_chan " << cur_chan << std::endl;
            
            // Create the soundfile
            Soundfile* soundfile = create(cur_chan, total_length, max_chan);
            
            // Init offset
            int offset = 0;
            
            // Read all files
            for (int i = 0; i < path_name_list.size(); i++) {
                if (path_name_list[i] == "__empty_sound__") {
                    empty(soundfile, i, offset, max_chan);
                } else {
                    readOne(soundfile, path_name_list[i], i, offset, max_chan);
                }
            }
            
            // Complete with empty parts
            for (int i = path_name_list.size(); i < MAX_PART; i++) {
                empty(soundfile, i, offset, max_chan);
            }
            
            return soundfile;
            
        } catch (...) {
            return NULL;
        }
    }

    // Soundfile path checking code
    static std::string checkFile(const std::vector<std::string>& sound_directories, const std::string& file_name);
   
    // Check if all soundfile exist and return their real path_name
    static std::vector<std::string> checkFiles(const std::vector<std::string>& sound_directories,
                                               const std::vector<std::string>& file_name_list)
    {
        std::vector<std::string> path_name_list;
        for (int i = 0; i < file_name_list.size(); i++) {
            std::string path_name = checkFile(sound_directories, file_name_list[i]);
            path_name_list.push_back((path_name == "") ? "__empty_sound__" : path_name);
        }
        return path_name_list;
    }

};

#endif
