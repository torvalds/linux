//===-- Platform.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#pragma once

#if defined(_MSC_VER)

#include <inttypes.h>
#include <io.h>
#include <signal.h>

#include "lldb/Host/HostGetOpt.h"
#include "lldb/Host/windows/windows.h"

struct winsize {
  long ws_col;
};

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

// fcntl.h // This is not used by MI
#define O_NOCTTY 0400

// ioctls.h
#define TIOCGWINSZ 0x5413

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

typedef long pid_t;

#define STDIN_FILENO 0
#define PATH_MAX 32768
#define snprintf _snprintf

extern int ioctl(int d, int request, ...);
extern int kill(pid_t pid, int sig);
extern int tcsetattr(int fd, int optional_actions,
                     const struct termios *termios_p);
extern int tcgetattr(int fildes, struct termios *termios_p);

// signal handler function pointer type
typedef void (*sighandler_t)(int);

// CODETAG_IOR_SIGNALS
// signal.h
#define SIGQUIT 3   // Terminal quit signal
#define SIGKILL 9   // Kill (cannot be caught or ignored)
#define SIGPIPE 13  // Write on a pipe with no one to read it
#define SIGCONT 18  // Continue executing, if stopped.
#define SIGTSTP 20  // Terminal stop signal
#define SIGSTOP 23  // Stop executing (cannot be caught or ignored)
#define SIGWINCH 28 // (== SIGVTALRM)

#else

#include <inttypes.h>
#include <limits.h>

#include <getopt.h>
#include <libgen.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/time.h>

#endif
