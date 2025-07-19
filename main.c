
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <js/glue.h>
#include <js/dom_pk_codes.h>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include "strings.h"

void __unordtf2(){}
#ifdef SDL2SUBDIR
// #include "SDL2/SDL.h"
#else
// #include "SDL.h"
#endif

// #include "zip.h"

#define WIDTH 512
#define HEIGHT 480
uint32_t pixels[WIDTH * HEIGHT];
uint32_t canvas[WIDTH * HEIGHT];
#include "snes.h"
#include "tracing.h"

/* depends on behaviour:
casting uintX_t to/from intX_t does 'expceted' unsigned<->signed conversion
  ((int8_t) 255) == -1
same with assignment
  int8_t a; a = 0xff; a == -1
overflow is handled as expected
  (uint8_t a = 255; a++; a == 0; uint8_t b = 0; b--; b == 255)
clipping is handled as expected
  (uint16_t a = 0x123; uint8_t b = a; b == 0x23)
giving non 0/1 value to boolean makes it 0/1
  (bool a = 2; a == 1)
giving out-of-range vaue to function parameter clips it in range
  (void test(uint8_t a) {...}; test(255 + 1); a == 0 within test)
int is at least 32 bits
shifting into sign bit makes value negative
  int a = ((int16_t) (0x1fff << 3)) >> 3; a == -1
*/

static struct {
  // rendering
  // SDL_Window* window;
  // SDL_Renderer* renderer;
  // SDL_Texture* texture;
  // // audio
  // SDL_AudioDeviceID audioDevice;
  int audioFrequency;
  int16_t* audioBuffer;
  // paths
  char* prefPath;
  char* pathSeparator;
  // snes, timing
  Snes* snes;
  float wantedFrames;
  int wantedSamples;
  // loaded rom
  bool loaded;
  char* romName;
  char* savePath;
  char* statePath;
} glb = {};

static bool onkey(void *user_data, bool pressed, int key, int code, int modifiers);
static uint8_t* readFile(const char* name, int* length);
static void loadRom(const char* path);
static void closeRom(void);
static void setPaths(const char* path);
static void setTitle(const char* path);
static void playAudio(void);
static void renderScreen(void);
static void handleInput(int key, int code, bool pressed);

bool running = true;
bool paused = false;
bool runOne = false;
bool turbo = false;

int main(int argc, char** argv) {
  // TODO
//   // set up audio
//   glb.audioFrequency = 48000;
//   SDL_AudioSpec want, have;
//   SDL_memset(&want, 0, sizeof(want));
//   want.freq = glb.audioFrequency;
//   want.format = AUDIO_S16;
//   want.channels = 2;
//   want.samples = 2048;
//   want.callback = NULL; // use queue
//   glb.audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
//   if(glb.audioDevice == 0) {
//     printf("Failed to open audio device: %s\n", SDL_GetError());
//     return 1;
//   }
  // glb.audioBuffer = malloc(glb.audioFrequency / 50 * 4); // *2 for stereo, *2 for sizeof(int16)
//   SDL_PauseAudioDevice(glb.audioDevice, 0);
  JS_createCanvas(WIDTH, HEIGHT);
  JS_setTitle("LakeSnes");
  JS_addKeyEventListener(NULL, onkey);

  glb.pathSeparator = "/";

  printf("LakeSnes\n");
  // init snes, load rom
  glb.snes = snes_init();
  glb.wantedFrames = 1.0 / 60.0;
  glb.wantedSamples = glb.audioFrequency / 60;
  glb.loaded = false;
  glb.romName = NULL;
  glb.savePath = NULL;
  glb.statePath = NULL;
  if(argc >= 2) {
    loadRom(argv[1]);
  } else {
    puts("No rom loaded");
#ifdef __wasm
#include <js/glue.h>
  JS_setFont("bold 48px Roboto");
  JS_fillStyle("white");
  const char *text[] = {
      "No rom loaded\n",
      "(.sfc/.smc)\n",
      "Click to browse...",
  };

  int len = sizeof(text) / sizeof(text[0]);
  int y = HEIGHT / len;
  int y_step = 128;

  for (int i = 0; i < len; i++) {
      JS_fillText(text[i], (WIDTH - JS_measureTextWidth(text[i])) / 2, (y + i * y_step) / 2);
  }

  int length;
  char* name = NULL;
  uint8_t *file = JS_openFilePicker(&length, &name);
  // close currently loaded rom (saves battery)
  closeRom();
  // load new rom
  if(snes_loadRom(glb.snes, file, length)) {
    // get rom name and paths, set title
    setPaths(name);
    setTitle(glb.romName);
    // set wantedFrames and wantedSamples
    glb.wantedFrames = 1.0 / (glb.snes->palTiming ? 50.0 : 60.0);
    glb.wantedSamples = glb.audioFrequency / (glb.snes->palTiming ? 50 : 60);
    glb.loaded = true;
    // load battery for loaded rom
    int size = 0;
    uint8_t* saveData = readFile(glb.savePath, &size);
    if(saveData != NULL) {
      if(snes_loadBattery(glb.snes, saveData, size)) {
        puts("Loaded battery data");
      } else {
        puts("Failed to load battery data");
      }
      free(saveData);
    }
  } // else, rom load failed, old rom still loaded
  free(file);
  free(name);
#endif
  }
  // timing
  uint64_t countFreq = 1000;
  uint64_t lastCount = JS_performanceNow();
  float timeAdder = 0.0;

  while(running) {
    uint64_t curCount = JS_performanceNow();
    uint64_t delta = curCount - lastCount;
    if (delta > countFreq / 60) {
      delta = countFreq / 60;
    }
    lastCount = curCount;
    float seconds = delta / (float) countFreq;
    timeAdder += seconds;
    // allow 2 ms earlier, to prevent skipping due to being just below wanted
    while(timeAdder >= glb.wantedFrames - 0.002) {
      timeAdder -= glb.wantedFrames;
      // run frame
      if(glb.loaded && (!paused || runOne)) {
        runOne = false;
        if(turbo) {
          snes_runFrame(glb.snes);
        }
        snes_runFrame(glb.snes);
        playAudio();
        renderScreen();
      }
    }

    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        uint32_t pixel = pixels[i];
        canvas[i] = ((pixel & 0xff0000) >> 16) | (pixel & 0x00ff00) | ((pixel & 0x0000ff) << 16) | 0xff000000;
    }
    JS_setPixelsAlpha(canvas);
    JS_requestAnimationFrame();
  }
  // close rom (saves battery)
  closeRom();
  // free snes
  snes_free(glb.snes);
  // free global allocs
  free(glb.audioBuffer);
  if(glb.romName) free(glb.romName);
  return 0;
}

static bool onkey(void *user_data, bool pressed, int key, int code, int modifiers) {
    (void)user_data,(void)modifiers;

    if (pressed) {
      switch(key) {
        case 'r': snes_reset(glb.snes, false); break;
        case 'e': snes_reset(glb.snes, true); break;
        case 'o': runOne = true; break;
        case 'p': paused = !paused; break;
        case 't': turbo = true; break;
        /* case 'j': {
          char* filePath = malloc(strlen(glb.prefPath) + 9); // "dump.bin" (8) + '\0'
          strcpy(filePath, glb.prefPath);
          strcat(filePath, "dump.bin");
          printf("Dumping to %s...\n", filePath);
          FILE* f = fopen(filePath, "wb");
          if(f == NULL) {
            puts("Failed to open file for writing");
            free(filePath);
            break;
          }
          fwrite(glb.snes->ram, 0x20000, 1, f);
          fwrite(glb.snes->ppu->vram, 0x10000, 1, f);
          fwrite(glb.snes->ppu->cgram, 0x200, 1, f);
          fwrite(glb.snes->ppu->oam, 0x200, 1, f);
          fwrite(glb.snes->ppu->highOam, 0x20, 1, f);
          fwrite(glb.snes->apu->ram, 0x10000, 1, f);
          fclose(f);
          free(filePath);
          break;
        } */
        case 'l': {
          // run one cpu cycle
          snes_runCpuCycle(glb.snes);
          char line[80];
          getProcessorStateCpu(glb.snes, line);
          puts(line);
          break;
        }
        case 'k': {
          // run one spc cycle
          snes_runSpcCycle(glb.snes);
          char line[57];
          getProcessorStateSpc(glb.snes, line);
          puts(line);
          break;
        }
        /* case 'm': {
          // save state
          int size = snes_saveState(glb.snes, NULL);
          uint8_t* stateData = malloc(size);
          snes_saveState(glb.snes, stateData);
          FILE* f = fopen(glb.statePath, "wb");
          if(f != NULL) {
            fwrite(stateData, size, 1, f);
            fclose(f);
            puts("Saved state");
          } else {
            puts("Failed to save state");
          }
          free(stateData);
          break;
        }
        case 'n': {
          // load state
          int size = 0;
          uint8_t* stateData = readFile(glb.statePath, &size);
          if(stateData != NULL) {
            if(snes_loadState(glb.snes, stateData, size)) {
              puts("Loaded state");
            } else {
              puts("Failed to load state, file contents invalid");
            }
            free(stateData);
          } else {
            puts("Failed to load state, failed to read file");
          }
          break;
        } */
      }
        // TODO
        // if (code == DOM_PK_ENTER && modifiers & KMOD_ALT) {
        //   fullscreenFlags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
        //   SDL_SetWindowFullscreen(glb.window, fullscreenFlags);
        // }
        // break;
    if((modifiers & (KMOD_ALT | KMOD_CTRL | KMOD_META)) == 0) {
      // only send keypress if not holding ctrl/alt/meta
      handleInput(key, code, true);
    }

    } else {
      switch(key) {
        case 't': turbo = false; break;
      }
      handleInput(key, code, false);
    }

    if (code == DOM_PK_F5 || code == DOM_PK_F11 || code == DOM_PK_F12) {
        return 0;
    }
    return 1;
}

static void playAudio() {
  snes_setSamples(glb.snes, glb.audioBuffer, glb.wantedSamples);
  // TODO
  // if(SDL_GetQueuedAudioSize(glb.audioDevice) <= glb.wantedSamples * 4 * 6) {
  //   // don't queue audio if buffer is still filled
  //   SDL_QueueAudio(glb.audioDevice, glb.audioBuffer, glb.wantedSamples * 4);
  // }
}

static void renderScreen() {
  snes_setPixels(glb.snes, (uint8_t*) pixels);
}

static void handleInput(int key, int code, bool pressed) {
  switch(key) {
    case 'z': snes_setButtonState(glb.snes, 1, 0, pressed); break;
    case 'a': snes_setButtonState(glb.snes, 1, 1, pressed); break;
    case 'x': snes_setButtonState(glb.snes, 1, 8, pressed); break;
    case 's': snes_setButtonState(glb.snes, 1, 9, pressed); break;
    case 'd': snes_setButtonState(glb.snes, 1, 10, pressed); break;
    case 'c': snes_setButtonState(glb.snes, 1, 11, pressed); break;
  }
  switch(code) {
    case DOM_PK_SHIFT_RIGHT: snes_setButtonState(glb.snes, 1, 2, pressed); break;
    case DOM_PK_ENTER: snes_setButtonState(glb.snes, 1, 3, pressed); break;
    case DOM_PK_ARROW_UP: snes_setButtonState(glb.snes, 1, 4, pressed); break;
    case DOM_PK_ARROW_DOWN: snes_setButtonState(glb.snes, 1, 5, pressed); break;
    case DOM_PK_ARROW_LEFT: snes_setButtonState(glb.snes, 1, 6, pressed); break;
    case DOM_PK_ARROW_RIGHT: snes_setButtonState(glb.snes, 1, 7, pressed); break;
  }
}

static void loadRom(const char* path) {
  int length = 0;
  uint8_t* file = readFile(path, &length);
  if(file == NULL) {
    printf("Failed to read file '%s'\n", path);
    return;
  }
  // close currently loaded rom (saves battery)
  closeRom();
  // load new rom
  if(snes_loadRom(glb.snes, file, length)) {
    // get rom name and paths, set title
    setPaths(path);
    setTitle(glb.romName);
    // set wantedFrames and wantedSamples
    glb.wantedFrames = 1.0 / (glb.snes->palTiming ? 50.0 : 60.0);
    glb.wantedSamples = glb.audioFrequency / (glb.snes->palTiming ? 50 : 60);
    glb.loaded = true;
    // load battery for loaded rom
    int size = 0;
    uint8_t* saveData = readFile(glb.savePath, &size);
    if(saveData != NULL) {
      if(snes_loadBattery(glb.snes, saveData, size)) {
        puts("Loaded battery data");
      } else {
        puts("Failed to load battery data");
      }
      free(saveData);
    }
  } // else, rom load failed, old rom still loaded
  free(file);
}

static void closeRom() {
  if(!glb.loaded) return;
  int size = snes_saveBattery(glb.snes, NULL);
  if(size > 0) {
    uint8_t* saveData = malloc(size);
    snes_saveBattery(glb.snes, saveData);
    FILE* f = fopen(glb.savePath, "wb");
    if(f != NULL) {
      fwrite(saveData, size, 1, f);
      fclose(f);
      puts("Saved battery data");
    } else {
      puts("Failed to save battery data");
    }
    free(saveData);
  }
}

static void setPaths(const char* path) {
  // get rom name
  if(glb.romName) free(glb.romName);
  const char* filename = strrchr(path, glb.pathSeparator[0]); // get last occurence of '/' or '\'
  if(filename == NULL) {
    filename = path;
  } else {
    filename += 1; // skip past '/' or '\' itself
  }
  glb.romName = malloc(strlen(filename) + 1); // +1 for '\0'
  strcpy(glb.romName, filename);
}

static void setTitle(const char* romName) {
  if(romName == NULL) {
    JS_setTitle("LakeSnes");
    return;
  }
  char* title = malloc(strlen(romName) + 12); // "LakeSnes - " (11) + '\0'
  strcpy(title, "LakeSnes - ");
  strcat(title, romName);
  JS_setTitle(title);
  free(title);
}

static uint8_t* readFile(const char* name, int* length) {
  FILE* f = fopen(name, "rb");
  if(f == NULL) return NULL;
  fseek(f, 0, SEEK_END);
  int size = ftell(f);
  rewind(f);
  uint8_t* buffer = malloc(size);
  if(fread(buffer, size, 1, f) != 1) {
    fclose(f);
    return NULL;
  }
  fclose(f);
  *length = size;
  return buffer;
}
