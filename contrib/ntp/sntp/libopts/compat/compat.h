/*  -*- Mode: C -*- 
 *
 *  compat.h is free software.
 *  This file is part of AutoGen and AutoOpts.
 *
 *  AutoGen Copyright (C) 1992-2015 by Bruce Korb - all rights reserved
 *
 *  AutoOpts is available under any one of two licenses.  The license
 *  in use must be one of these two and the choice is under the control
 *  of the user of the license.
 *
 *   The GNU Lesser General Public License, version 3 or later
 *      See the files "COPYING.lgplv3" and "COPYING.gplv3"
 *
 *   The Modified Berkeley Software Distribution License
 *      See the file "COPYING.mbsd"
 *
 *  These files have the following sha256 sums:
 *
 *  8584710e9b04216a394078dc156b781d0b47e1729104d666658aecef8ee32e95  COPYING.gplv3
 *  4379e7444a0e2ce2b12dd6f5a52a27a4d02d39d247901d3285c88cf0d37f477b  COPYING.lgplv3
 *  13aa749a5b0a454917a944ed8fffc530b784f5ead522b1aacaf4ec8aa55a6239  COPYING.mbsd
 */

/**
 * \file compat.h
 *  fake the preprocessor into handlng stuff portability
 */
#ifndef COMPAT_H_GUARD
#define COMPAT_H_GUARD 1

#if defined(HAVE_CONFIG_H)
#  include <config.h>

#elif defined(_WIN32) && !defined(__CYGWIN__)
#  include "windows-config.h"

#else
#  error "compat.h" requires "config.h"
   choke me.
#endif


#ifndef HAVE_STRSIGNAL
# ifndef HAVE_RAW_DECL_STRSIGNAL
   char * strsignal(int signo);
# endif
#endif

#define  _GNU_SOURCE    1 /* for strsignal in GNU's libc */
#define  __USE_GNU      1 /* exact same thing as above   */
#define  __EXTENSIONS__ 1 /* and another way to call for it */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  SYSTEM HEADERS:
 */
#include <sys/types.h>
#ifdef HAVE_SYS_MMAN_H
#  include <sys/mman.h>
#endif
#include <sys/param.h>
#if HAVE_SYS_PROCSET_H
#  include <sys/procset.h>
#endif
#include <sys/stat.h>
#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif

#if defined( HAVE_SOLARIS_SYSINFO )
#  include <sys/systeminfo.h>
#elif defined( HAVE_UNAME_SYSCALL )
#  include <sys/utsname.h>
#endif

#ifdef DAEMON_ENABLED
#  if HAVE_SYS_STROPTS_H
#  include <sys/stropts.h>
#  endif

#  if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#  endif

#  if ! defined(HAVE_SYS_POLL_H) && ! defined(HAVE_SYS_SELECT_H)
#    error This system cannot support daemon processing
     Choke Me.
#  endif

#  if HAVE_SYS_POLL_H
#  include <sys/poll.h>
#  endif

#  if HAVE_SYS_SELECT_H
#  include <sys/select.h>
#  endif

#  if HAVE_NETINET_IN_H
#  include <netinet/in.h>
#  endif

#  if HAVE_SYS_UN_H
#  include <sys/un.h>
#  endif
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  USER HEADERS:
 */
#include <stdio.h>
#include <assert.h>
#include <ctype.h>

/*
 *  Directory opening stuff:
 */
# if defined (_POSIX_SOURCE)
/* Posix does not require that the d_ino field be present, and some
   systems do not provide it. */
#    define REAL_DIR_ENTRY(dp) 1
# else /* !_POSIX_SOURCE */
#    define REAL_DIR_ENTRY(dp) (dp->d_ino != 0)
# endif /* !_POSIX_SOURCE */

# if defined (HAVE_DIRENT_H)
#   include <dirent.h>
#   define D_NAMLEN(dirent) strlen((dirent)->d_name)
# else /* !HAVE_DIRENT_H */
#   define dirent direct
#   define D_NAMLEN(dirent) (dirent)->d_namlen
#   if defined (HAVE_SYS_NDIR_H)
#     include <sys/ndir.h>
#   endif /* HAVE_SYS_NDIR_H */
#   if defined (HAVE_SYS_DIR_H)
#     include <sys/dir.h>
#   endif /* HAVE_SYS_DIR_H */
#   if defined (HAVE_NDIR_H)
#     include <ndir.h>
#   endif /* HAVE_NDIR_H */
# endif /* !HAVE_DIRENT_H */

#include <errno.h>
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifndef O_NONBLOCK
# define O_NONBLOCK FNDELAY
#endif

#if defined(HAVE_LIBGEN) && defined(HAVE_LIBGEN_H)
#  include <libgen.h>
#endif

#if defined(HAVE_LIMITS_H)  /* this is also in options.h */
#  include <limits.h>
#elif defined(HAVE_SYS_LIMITS_H)
#  include <sys/limits.h>
#endif /* HAVE_LIMITS/SYS_LIMITS_H */

#include <memory.h>
#include <setjmp.h>
#include <signal.h>

#if defined(HAVE_STDINT_H)
#  include <stdint.h>

#elif defined(HAVE_INTTYPES_H)
#  include <inttypes.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_UTIME_H
#  include <utime.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_STDBOOL_H
#  include <stdbool.h>
#else
   typedef enum { false = 0, true = 1 } _Bool;
#  define bool _Bool

   /* The other macros must be usable in preprocessor directives.  */
#  define false 0
#  define true 1
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  FIXUPS and CONVIENCE STUFF:
 */
#ifdef __cplusplus
#   define EXTERN extern "C"
#else
#   define EXTERN extern
#endif

/* some systems #def errno! and others do not declare it!! */
#ifndef errno
   extern int errno;
#endif

/* Some machines forget this! */

# ifndef EXIT_FAILURE
#   define EXIT_SUCCESS 0
#   define EXIT_FAILURE 1
# endif

#ifndef NUL
#  define NUL '\0'
#endif

#ifndef NULL
#  define NULL 0
#endif

#if !defined (MAXPATHLEN) && defined (HAVE_SYS_PARAM_H)
#  include <sys/param.h>
#endif /* !MAXPATHLEN && HAVE_SYS_PARAM_H */

#if !defined (MAXPATHLEN) && defined (PATH_MAX)
#  define MAXPATHLEN PATH_MAX
#endif /* !MAXPATHLEN && PATH_MAX */

#if !defined (MAXPATHLEN) && defined(_MAX_PATH)
#  define PATH_MAX _MAX_PATH
#  define MAXPATHLEN _MAX_PATH
#endif

#if !defined (MAXPATHLEN)
#  define MAXPATHLEN 4096
#endif /* MAXPATHLEN */

#define AG_PATH_MAX  ((size_t)MAXPATHLEN)

#ifndef LONG_MAX
#  define LONG_MAX      ~(1L << (8*sizeof(long) -1))
#  define INT_MAX       ~(1 << (8*sizeof(int) -1))
#endif

#ifndef ULONG_MAX
#  define ULONG_MAX     ~(OUL)
#  define UINT_MAX      ~(OU)
#endif

#ifndef SHORT_MAX
#  define SHORT_MAX     ~(1 << (8*sizeof(short) - 1))
#else
#  define USHORT_MAX    ~(OUS)
#endif

#ifndef HAVE_INT8_T
  typedef signed char           int8_t;
# define  HAVE_INT8_T           1
#endif
#ifndef HAVE_UINT8_T
  typedef unsigned char         uint8_t;
# define  HAVE_UINT8_T          1
#endif
#ifndef HAVE_INT16_T
  typedef signed short          int16_t;
# define  HAVE_INT16_T          1
#endif
#ifndef HAVE_UINT16_T
  typedef unsigned short        uint16_t;
# define  HAVE_UINT16_T         1
#endif

#ifndef HAVE_INT32_T
# if SIZEOF_INT ==              4
    typedef signed int          int32_t;
# elif SIZEOF_LONG ==           4
    typedef signed long         int32_t;
# endif
# define  HAVE_INT32_T          1
#endif

#ifndef HAVE_UINT32_T
# if SIZEOF_INT ==              4
    typedef unsigned int        uint32_t;
# elif SIZEOF_LONG ==           4
    typedef unsigned long       uint32_t;
# else
#   error Cannot create a uint32_t type.
    Choke Me.
# endif
# define  HAVE_UINT32_T         1
#endif

#ifndef HAVE_INTPTR_T
# if SIZEOF_CHARP == SIZEOF_LONG
    typedef signed long         intptr_t;
# else
    typedef signed int          intptr_t;
# endif
# define  HAVE_INTPTR_T         1
#endif

#ifndef HAVE_UINTPTR_T
# if SIZEOF_CHARP == SIZEOF_LONG
    typedef unsigned long       intptr_t;
# else
    typedef unsigned int        intptr_t;
# endif
# define  HAVE_INTPTR_T         1
#endif

#ifndef HAVE_UINT_T
  typedef unsigned int          uint_t;
# define  HAVE_UINT_T           1
#endif

#ifndef HAVE_SIZE_T
  typedef unsigned int          size_t;
# define  HAVE_SIZE_T           1
#endif
#ifndef HAVE_WINT_T
  typedef unsigned int          wint_t;
# define  HAVE_WINT_T           1
#endif
#ifndef HAVE_PID_T
  typedef signed int            pid_t;
# define  HAVE_PID_T            1
#endif

/* redefine these for BSD style string libraries */
#ifndef HAVE_STRCHR
#  define strchr            index
#  define strrchr           rindex
#endif

#ifdef USE_FOPEN_BINARY
#  ifndef FOPEN_BINARY_FLAG
#    define FOPEN_BINARY_FLAG   "b"
#  endif
#  ifndef FOPEN_TEXT_FLAG
#    define FOPEN_TEXT_FLAG     "t"
#  endif
#else
#  ifndef FOPEN_BINARY_FLAG
#    define FOPEN_BINARY_FLAG
#  endif
#  ifndef FOPEN_TEXT_FLAG
#    define FOPEN_TEXT_FLAG
#  endif
#endif

#ifndef STR
#  define _STR(s) #s
#  define STR(s)  _STR(s)
#endif

/* ##### Pointer sized word ##### */

/* FIXME:  the MAX stuff in here is broken! */
#if SIZEOF_CHARP > SIZEOF_INT
   typedef long t_word;
   #define WORD_MAX  LONG_MAX
   #define WORD_MIN  LONG_MIN
#else /* SIZEOF_CHARP <= SIZEOF_INT */
   typedef int t_word;
   #define WORD_MAX  INT_MAX
   #define WORD_MIN  INT_MIN
#endif

#endif /* COMPAT_H_GUARD */

/*
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of compat/compat.h */
