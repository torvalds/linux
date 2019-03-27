/* Get common system includes and various definitions and declarations based
   on autoconf macros.
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

This file is part of libcpp (aka cpplib).

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#ifndef LIBCPP_SYSTEM_H
#define LIBCPP_SYSTEM_H

/* We must include stdarg.h before stdio.h.  */
#include <stdarg.h>

#ifdef HAVE_STDDEF_H
# include <stddef.h>
#endif

#include <stdio.h>

/* Define a generic NULL if one hasn't already been defined.  */
#ifndef NULL
#define NULL 0
#endif

/* Use the unlocked open routines from libiberty.  */
#define fopen(PATH,MODE) fopen_unlocked(PATH,MODE)
#define fdopen(FILDES,MODE) fdopen_unlocked(FILDES,MODE)
#define freopen(PATH,MODE,STREAM) freopen_unlocked(PATH,MODE,STREAM)

/* The compiler is not a multi-threaded application and therefore we
   do not have to use the locking functions.  In fact, using the locking
   functions can cause the compiler to be significantly slower under
   I/O bound conditions (such as -g -O0 on very large source files).

   HAVE_DECL_PUTC_UNLOCKED actually indicates whether or not the stdio
   code is multi-thread safe by default.  If it is set to 0, then do
   not worry about using the _unlocked functions.

   fputs_unlocked, fwrite_unlocked, and fprintf_unlocked are
   extensions and need to be prototyped by hand (since we do not
   define _GNU_SOURCE).  */

#if defined HAVE_DECL_PUTC_UNLOCKED && HAVE_DECL_PUTC_UNLOCKED

# ifdef HAVE_PUTC_UNLOCKED
#  undef putc
#  define putc(C, Stream) putc_unlocked (C, Stream)
# endif
# ifdef HAVE_PUTCHAR_UNLOCKED
#  undef putchar
#  define putchar(C) putchar_unlocked (C)
# endif
# ifdef HAVE_GETC_UNLOCKED
#  undef getc
#  define getc(Stream) getc_unlocked (Stream)
# endif
# ifdef HAVE_GETCHAR_UNLOCKED
#  undef getchar
#  define getchar() getchar_unlocked ()
# endif
# ifdef HAVE_FPUTC_UNLOCKED
#  undef fputc
#  define fputc(C, Stream) fputc_unlocked (C, Stream)
# endif

# ifdef HAVE_CLEARERR_UNLOCKED
#  undef clearerr
#  define clearerr(Stream) clearerr_unlocked (Stream)
#  if defined (HAVE_DECL_CLEARERR_UNLOCKED) && !HAVE_DECL_CLEARERR_UNLOCKED
extern void clearerr_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FEOF_UNLOCKED
#  undef feof
#  define feof(Stream) feof_unlocked (Stream)
#  if defined (HAVE_DECL_FEOF_UNLOCKED) && !HAVE_DECL_FEOF_UNLOCKED
extern int feof_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FILENO_UNLOCKED
#  undef fileno
#  define fileno(Stream) fileno_unlocked (Stream)
#  if defined (HAVE_DECL_FILENO_UNLOCKED) && !HAVE_DECL_FILENO_UNLOCKED
extern int fileno_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FFLUSH_UNLOCKED
#  undef fflush
#  define fflush(Stream) fflush_unlocked (Stream)
#  if defined (HAVE_DECL_FFLUSH_UNLOCKED) && !HAVE_DECL_FFLUSH_UNLOCKED
extern int fflush_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FGETC_UNLOCKED
#  undef fgetc
#  define fgetc(Stream) fgetc_unlocked (Stream)
#  if defined (HAVE_DECL_FGETC_UNLOCKED) && !HAVE_DECL_FGETC_UNLOCKED
extern int fgetc_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FGETS_UNLOCKED
#  undef fgets
#  define fgets(S, n, Stream) fgets_unlocked (S, n, Stream)
#  if defined (HAVE_DECL_FGETS_UNLOCKED) && !HAVE_DECL_FGETS_UNLOCKED
extern char *fgets_unlocked (char *, int, FILE *);
#  endif
# endif
# ifdef HAVE_FPUTS_UNLOCKED
#  undef fputs
#  define fputs(String, Stream) fputs_unlocked (String, Stream)
#  if defined (HAVE_DECL_FPUTS_UNLOCKED) && !HAVE_DECL_FPUTS_UNLOCKED
extern int fputs_unlocked (const char *, FILE *);
#  endif
# endif
# ifdef HAVE_FERROR_UNLOCKED
#  undef ferror
#  define ferror(Stream) ferror_unlocked (Stream)
#  if defined (HAVE_DECL_FERROR_UNLOCKED) && !HAVE_DECL_FERROR_UNLOCKED
extern int ferror_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FREAD_UNLOCKED
#  undef fread
#  define fread(Ptr, Size, N, Stream) fread_unlocked (Ptr, Size, N, Stream)
#  if defined (HAVE_DECL_FREAD_UNLOCKED) && !HAVE_DECL_FREAD_UNLOCKED
extern size_t fread_unlocked (void *, size_t, size_t, FILE *);
#  endif
# endif
# ifdef HAVE_FWRITE_UNLOCKED
#  undef fwrite
#  define fwrite(Ptr, Size, N, Stream) fwrite_unlocked (Ptr, Size, N, Stream)
#  if defined (HAVE_DECL_FWRITE_UNLOCKED) && !HAVE_DECL_FWRITE_UNLOCKED
extern size_t fwrite_unlocked (const void *, size_t, size_t, FILE *);
#  endif
# endif
# ifdef HAVE_FPRINTF_UNLOCKED
#  undef fprintf
/* We can't use a function-like macro here because we don't know if
   we have varargs macros.  */
#  define fprintf fprintf_unlocked
#  if defined (HAVE_DECL_FPRINTF_UNLOCKED) && !HAVE_DECL_FPRINTF_UNLOCKED
extern int fprintf_unlocked (FILE *, const char *, ...);
#  endif
# endif

#endif

/* ??? Glibc's fwrite/fread_unlocked macros cause
   "warning: signed and unsigned type in conditional expression".  */
#undef fread_unlocked
#undef fwrite_unlocked

#include <sys/types.h>
#include <errno.h>

#if !defined (errno) && defined (HAVE_DECL_ERRNO) && !HAVE_DECL_ERRNO
extern int errno;
#endif

/* Some of glibc's string inlines cause warnings.  Plus we'd rather
   rely on (and therefore test) GCC's string builtins.  */
#define __NO_STRING_INLINES

#ifdef STRING_WITH_STRINGS
# include <string.h>
# include <strings.h>
#else
# ifdef HAVE_STRING_H
#  include <string.h>
# else
#  ifdef HAVE_STRINGS_H
#   include <strings.h>
#  endif
# endif
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#endif

/* Infrastructure for defining missing _MAX and _MIN macros.  Note that
   macros defined with these cannot be used in #if.  */

/* The extra casts work around common compiler bugs.  */
#define INTTYPE_SIGNED(t) (! ((t) 0 < (t) -1))
/* The outer cast is needed to work around a bug in Cray C 5.0.3.0.
   It is necessary at least when t == time_t.  */
#define INTTYPE_MINIMUM(t) ((t) (INTTYPE_SIGNED (t) \
                             ? ~ (t) 0 << (sizeof(t) * CHAR_BIT - 1) : (t) 0))
#define INTTYPE_MAXIMUM(t) ((t) (~ (t) 0 - INTTYPE_MINIMUM (t)))

/* Use that infrastructure to provide a few constants.  */
#ifndef UCHAR_MAX
# define UCHAR_MAX INTTYPE_MAXIMUM (unsigned char)
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  ifdef HAVE_TIME_H
#   include <time.h>
#  endif
# endif
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# ifdef HAVE_SYS_FILE_H
#  include <sys/file.h>
# endif
#endif

#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#ifdef HAVE_LANGINFO_CODESET
# include <langinfo.h>
#endif

#ifndef HAVE_SETLOCALE
# define setlocale(category, locale) (locale)
#endif

#ifdef ENABLE_NLS
#include <libintl.h>
#else
/* Stubs.  */
# undef dgettext
# define dgettext(package, msgid) (msgid)
#endif

#ifndef _
# define _(msgid) dgettext (PACKAGE, msgid)
#endif

#ifndef N_
# define N_(msgid) msgid
#endif

/* Some systems define these in, e.g., param.h.  We undefine these names
   here to avoid the warnings.  We prefer to use our definitions since we
   know they are correct.  */

#undef MIN
#undef MAX
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

/* The HAVE_DECL_* macros are three-state, undefined, 0 or 1.  If they
   are defined to 0 then we must provide the relevant declaration
   here.  These checks will be in the undefined state while configure
   is running so be careful to test "defined (HAVE_DECL_*)".  */

#if defined (HAVE_DECL_ABORT) && !HAVE_DECL_ABORT
extern void abort (void);
#endif

#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

/* Test if something is a normal file.  */
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

/* Test if something is a directory.  */
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

/* Test if something is a character special file.  */
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#endif

/* Test if something is a block special file.  */
#ifndef S_ISBLK
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif

/* Test if something is a socket.  */
#ifndef S_ISSOCK
# ifdef S_IFSOCK
#   define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
# else
#   define S_ISSOCK(m) 0
# endif
#endif

/* Test if something is a FIFO.  */
#ifndef S_ISFIFO
# ifdef S_IFIFO
#  define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
# else
#  define S_ISFIFO(m) 0
# endif
#endif

/* Approximate O_NOCTTY and O_BINARY.  */
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#ifndef O_BINARY
# define O_BINARY 0
#endif

/* Filename handling macros.  */
#include "filenames.h"

/* Get libiberty declarations.  */
#include "libiberty.h"
#include "safe-ctype.h"

/* 1 if we have C99 designated initializers.

   ??? C99 designated initializers are not supported by most C++
   compilers, including G++.  -- gdr, 2005-05-18  */
#if !defined(HAVE_DESIGNATED_INITIALIZERS)
# if (!defined(__cplusplus) && (GCC_VERSION >= 2007)) \
     ||(__STDC_VERSION__ >= 199901L)
#  define HAVE_DESIGNATED_INITIALIZERS 1
# else
#  define HAVE_DESIGNATED_INITIALIZERS 0
# endif
#endif

/* Be conservative and only use enum bitfields with GCC.
   FIXME: provide a complete autoconf test for buggy enum bitfields.  */

#if (GCC_VERSION > 2000)
#define ENUM_BITFIELD(TYPE) __extension__ enum TYPE
#else
#define ENUM_BITFIELD(TYPE) unsigned int
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER)	((size_t) &((TYPE *) 0)->MEMBER)
#endif

/* __builtin_expect(A, B) evaluates to A, but notifies the compiler that
   the most likely value of A is B.  This feature was added at some point
   between 2.95 and 3.0.  Let's use 3.0 as the lower bound for now.  */
#if (GCC_VERSION < 3000)
#define __builtin_expect(a, b) (a)
#endif

/* Provide a fake boolean type.  We make no attempt to use the
   C99 _Bool, as it may not be available in the bootstrap compiler,
   and even if it is, it is liable to be buggy.  
   This must be after all inclusion of system headers, as some of
   them will mess us up.  */
#undef bool
#undef true
#undef false
#undef TRUE
#undef FALSE

#ifndef __cplusplus
#define bool unsigned char
#endif
#define true 1
#define false 0

/* Some compilers do not allow the use of unsigned char in bitfields.  */
#define BOOL_BITFIELD unsigned int

/* Poison identifiers we do not want to use.  */
#if (GCC_VERSION >= 3000)
#undef calloc
#undef strdup
#undef malloc
#undef realloc
 #pragma GCC poison calloc strdup
 #pragma GCC poison malloc realloc

/* Libiberty macros that are no longer used in GCC.  */
#undef ANSI_PROTOTYPES
#undef PTR_CONST
#undef LONG_DOUBLE
#undef VPARAMS
#undef VA_OPEN
#undef VA_FIXEDARG
#undef VA_CLOSE
#undef VA_START
 #pragma GCC poison ANSI_PROTOTYPES PTR_CONST LONG_DOUBLE VPARAMS VA_OPEN \
  VA_FIXEDARG VA_CLOSE VA_START

/* Note: not all uses of the `index' token (e.g. variable names and
   structure members) have been eliminated.  */
#undef bcopy
#undef bzero
#undef bcmp
#undef rindex
 #pragma GCC poison bcopy bzero bcmp rindex

#endif /* GCC >= 3.0 */
#endif /* ! LIBCPP_SYSTEM_H */
