/**
 * File: emul.c
 * Author: Diego Parrilla Santamaría
 * Date: February 2025, February 2026
 * Copyright: 2025-2026 - GOODDATA LABS
 * Description: Hello World app core logic
 */

#include "emul.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Include in the C file to avoid multiple definitions
#include "target_firmware.h"  // Include the target firmware binary

#include "aconfig.h"
#include "display.h"
#include "memfunc.h"
#include "pico/stdlib.h"
#include "reset.h"
#include "romemul.h"
#include "select.h"
#include "term.h"

#define SLEEP_LOOP_MS 50
#define HELLO_TOP_ROW 0
#define HELLO_PROMPT_ROW (TERM_SCREEN_SIZE_Y - 2)
#define HELLO_INPUT_ROW (TERM_SCREEN_SIZE_Y - 1)
#define HELLO_MAX_NAME_LENGTH (TERM_SCREEN_SIZE_X - 1)
#define HELLO_TEXT_BUFFER_SIZE 96

enum {
  APP_MODE_SETUP = 255  // Setup
};

// Keep active loop or exit
static bool keepActive = true;

// Should we reset the device, or jump to the booster app?
// By default, we reset the device.
static bool resetDeviceAtBoot = true;

static char enteredName[HELLO_MAX_NAME_LENGTH + 1] = {0};
static size_t enteredNameLength = 0;
static char submittedName[HELLO_MAX_NAME_LENGTH + 1] = "World";

static bool getKeepActive(void) { return keepActive; }

static bool getResetDevice(void) { return resetDeviceAtBoot; }

static void appMoveCursor(uint8_t row, uint8_t col) {
  char cursorSequence[] = {
      TERM_ESC_CHAR,
      'Y',
      (char)(TERM_POS_Y + row),
      (char)(TERM_POS_X + col),
      '\0',
  };
  term_printString(cursorSequence);
}

static void appPrintLine(uint8_t row, const char *text) {
  if (text == NULL) {
    text = "";
  }

  char output[HELLO_TEXT_BUFFER_SIZE] = {0};
  snprintf(output, sizeof(output),
           "\x1B"
           "Y%c%c\x1B"
           "K%.*s",
           (char)(TERM_POS_Y + row), (char)TERM_POS_X, TERM_SCREEN_SIZE_X,
           text);
  term_printString(output);
}

static void appPlaceInputCursor(void) {
  appMoveCursor(HELLO_INPUT_ROW, (uint8_t)enteredNameLength);
}

static void appRenderGreeting(void) {
  char greeting[HELLO_TEXT_BUFFER_SIZE] = {0};
  snprintf(greeting, sizeof(greeting), "Hello, %s!", submittedName);
  appPrintLine(HELLO_TOP_ROW, greeting);
}

static void appRenderInputLine(void) {
  appPrintLine(HELLO_INPUT_ROW, enteredName);
  appPlaceInputCursor();
}

static void appRenderUi(void) {
  appRenderGreeting();
  appPrintLine(HELLO_PROMPT_ROW, "What's your name?");
  appRenderInputLine();
}

static size_t appTrimName(const char *source, size_t sourceLength, char *dest,
                          size_t destSize) {
  if ((source == NULL) || (dest == NULL) || (destSize == 0)) {
    return 0;
  }

  size_t start = 0;
  while ((start < sourceLength) &&
         isspace((unsigned char)source[start])) {
    start++;
  }

  size_t end = sourceLength;
  while ((end > start) && isspace((unsigned char)source[end - 1])) {
    end--;
  }

  size_t trimmedLength = end - start;
  if (trimmedLength >= destSize) {
    trimmedLength = destSize - 1;
  }

  if (trimmedLength > 0) {
    memcpy(dest, source + start, trimmedLength);
  }
  dest[trimmedLength] = '\0';

  return trimmedLength;
}

static void appSubmitName(void) {
  char trimmedName[HELLO_MAX_NAME_LENGTH + 1] = {0};
  size_t trimmedLength =
      appTrimName(enteredName, enteredNameLength, trimmedName,
                  sizeof(trimmedName));

  if (trimmedLength == 0) {
    snprintf(submittedName, sizeof(submittedName), "World");
    enteredName[0] = '\0';
    enteredNameLength = 0;
  } else {
    snprintf(submittedName, sizeof(submittedName), "%s", trimmedName);
    snprintf(enteredName, sizeof(enteredName), "%s", trimmedName);
    enteredNameLength = trimmedLength;
  }

  appRenderGreeting();
  appRenderInputLine();
}

static void appExitToBooster(void) {
  resetDeviceAtBoot = false;  // Jump to the booster app
  keepActive = false;         // Exit the active loop
}

static void appHandleKeystroke(char keystroke) {
  if ((keystroke == '\n') || (keystroke == '\r')) {
    appSubmitName();
    return;
  }

  if ((keystroke == '\b') || (keystroke == 127)) {
    if (enteredNameLength > 0) {
      enteredNameLength--;
      enteredName[enteredNameLength] = '\0';
      appRenderInputLine();
    }
    return;
  }

  if ((keystroke < TERM_KEYBOARD_KEY_START) ||
      (keystroke > TERM_KEYBOARD_KEY_END)) {
    return;
  }

  if (enteredNameLength < HELLO_MAX_NAME_LENGTH) {
    enteredName[enteredNameLength++] = keystroke;
    enteredName[enteredNameLength] = '\0';
    appRenderInputLine();
  }
}

static void init(void) {
  term_setKeystrokeHandler(appHandleKeystroke);
  term_setStartHandler(appExitToBooster);

  // Enter terminal mode directly when the app starts.
  term_enterMode();

  // Reset app state and draw the UI.
  memset(enteredName, 0, sizeof(enteredName));
  enteredNameLength = 0;
  snprintf(submittedName, sizeof(submittedName), "World");

  term_clearInputBuffer();
  term_clearScreen();
  appRenderUi();
}

void emul_start() {
  // The Atari-side code is embedded as an array in target_firmware.h.
  COPY_FIRMWARE_TO_RAM((uint16_t *)target_firmware, target_firmware_length);

  // Start command protocol handling between Atari ST and RP.
  init_romemul(NULL, term_dma_irq_handler_lookup, false);

  // Initialize display and terminal internals.
  display_setupU8g2();
  term_init();
  select_configure();
  init();

  // Main loop.
  while (getKeepActive()) {
    sleep_ms(SLEEP_LOOP_MS);
    term_loop();
  }

  // Exit flow: reset the target computer and either reboot app or jump booster.
  sleep_ms(SLEEP_LOOP_MS);
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
  sleep_ms(SLEEP_LOOP_MS);
  if (getResetDevice()) {
    reset_device();
  } else {
    settings_put_integer(aconfig_getContext(), ACONFIG_PARAM_MODE,
                         APP_MODE_SETUP);
    settings_save(aconfig_getContext(), true);
    reset_jump_to_booster();
  }
}
