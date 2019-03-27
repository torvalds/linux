///////////////////////////////////////////////////////////////////////////////
//
/// \file       private.h
/// \brief      Common includes, definions, and prototypes
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "sysdefs.h"
#include "mythread.h"

#include "lzma.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <locale.h>
#include <stdio.h>
#include <unistd.h>

#include "tuklib_gettext.h"
#include "tuklib_progname.h"
#include "tuklib_exit.h"
#include "tuklib_mbstr.h"

#if defined(_WIN32) && !defined(__CYGWIN__)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

#ifndef STDIN_FILENO
#	define STDIN_FILENO (fileno(stdin))
#endif

#ifndef STDOUT_FILENO
#	define STDOUT_FILENO (fileno(stdout))
#endif

#ifndef STDERR_FILENO
#	define STDERR_FILENO (fileno(stderr))
#endif

#ifdef HAVE_CAPSICUM
#	define ENABLE_SANDBOX 1
#endif

#include "main.h"
#include "mytime.h"
#include "coder.h"
#include "message.h"
#include "args.h"
#include "hardware.h"
#include "file_io.h"
#include "options.h"
#include "signals.h"
#include "suffix.h"
#include "util.h"

#ifdef HAVE_DECODERS
#	include "list.h"
#endif
