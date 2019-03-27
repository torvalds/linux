//===-- Platform.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// this file is only relevant for Visual C++
#if defined(_WIN32)

#include <assert.h>
#include <process.h>
#include <stdlib.h>

#include "Platform.h"
#include "llvm/Support/ErrorHandling.h"

int ioctl(int d, int request, ...) {
  switch (request) {
  // request the console windows size
  case (TIOCGWINSZ): {
    va_list vl;
    va_start(vl, request);
    // locate the window size structure on stack
    winsize *ws = va_arg(vl, winsize *);
    // get screen buffer information
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) ==
        TRUE)
      // fill in the columns
      ws->ws_col = info.dwMaximumWindowSize.X;
    va_end(vl);
    return 0;
  } break;
  default:
    llvm_unreachable("Not implemented!");
  }
}

int kill(pid_t pid, int sig) {
  // is the app trying to kill itself
  if (pid == getpid())
    exit(sig);
  //
  llvm_unreachable("Not implemented!");
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
  llvm_unreachable("Not implemented!");
}

int tcgetattr(int fildes, struct termios *termios_p) {
  //  assert( !"Not implemented!" );
  // error return value (0=success)
  return -1;
}

#endif
