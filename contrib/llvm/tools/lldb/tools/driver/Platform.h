//===-- Platform.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Platform_h_
#define lldb_Platform_h_

#if defined(_WIN32)

#include <io.h>
#if defined(_MSC_VER)
#include <signal.h>
#endif
#include "lldb/Host/windows/windows.h"
#include <inttypes.h>

struct winsize {
  long ws_col;
};

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

// fcntl.h
#define O_NOCTTY 0400

// ioctls.h
#define TIOCGWINSZ 0x5413

// signal.h
#define SIGPIPE 13
#define SIGCONT 18
#define SIGTSTP 20
#define SIGWINCH 28

// tcsetattr arguments
#define TCSANOW 0

#define NCCS 32
struct termios {
  tcflag_t c_iflag; // input mode flags
  tcflag_t c_oflag; // output mode flags
  tcflag_t c_cflag; // control mode flags
  tcflag_t c_lflag; // local mode flags
  cc_t c_line;      // line discipline
  cc_t c_cc[NCCS];  // control characters
  speed_t c_ispeed; // input speed
  speed_t c_ospeed; // output speed
};

#ifdef _MSC_VER
struct timeval {
  long tv_sec;
  long tv_usec;
};
typedef long pid_t;
#define PATH_MAX MAX_PATH
#endif

#define STDIN_FILENO 0

extern int ioctl(int d, int request, ...);
extern int kill(pid_t pid, int sig);
extern int tcsetattr(int fd, int optional_actions,
                     const struct termios *termios_p);
extern int tcgetattr(int fildes, struct termios *termios_p);

#else
#include <inttypes.h>

#include <libgen.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/time.h>
#endif

#endif // lldb_Platform_h_
