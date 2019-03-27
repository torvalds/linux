/****************************************************************************
 * Copyright (c) 1998-2011,2012 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Thomas E. Dickey                    1997-on                     *
 ****************************************************************************/
/*
 * $Id: progs.priv.h,v 1.39 2012/02/22 22:11:27 tom Exp $
 *
 *	progs.priv.h
 *
 *	Header file for curses utility programs
 */

#include <ncurses_cfg.h>

#if USE_RCS_IDS
#define MODULE_ID(id) static const char Ident[] = id;
#else
#define MODULE_ID(id)		/*nothing */
#endif

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_SYS_BSDTYPES_H
#include <sys/bsdtypes.h>	/* needed for ISC */
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#elif HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
# if defined(_FILE_OFFSET_BITS) && defined(HAVE_STRUCT_DIRENT64)
#  if !defined(_LP64) && (_FILE_OFFSET_BITS == 64)
#   define	DIRENT	struct dirent64
#  else
#   define	DIRENT	struct dirent
#  endif
# else
#  define	DIRENT	struct dirent
# endif
#else
# define DIRENT struct direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include <assert.h>
#include <errno.h>

#if DECL_ERRNO
extern int errno;
#endif

#if HAVE_GETOPT_H
#include <getopt.h>
#elif !defined(HAVE_GETOPT_HEADER)
/* 'getopt()' may be prototyped in <stdlib.h>, but declaring its
 * variables doesn't hurt.
 */
extern char *optarg;
extern int optind;
#endif /* HAVE_GETOPT_H */

#include <curses.h>
#include <term_entry.h>
#include <nc_termios.h>
#include <tic.h>
#include <nc_tparm.h>

#include <nc_string.h>
#include <nc_alloc.h>
#if HAVE_NC_FREEALL
#undef ExitProgram
#ifdef USE_LIBTINFO
#define ExitProgram(code) _nc_free_tinfo(code)
#else
#define ExitProgram(code) _nc_free_tic(code)
#endif
#endif

/* usually in <unistd.h> */
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#ifndef R_OK
#define	R_OK	4		/* Test for readable.  */
#endif

#ifndef W_OK
#define	W_OK	2		/* Test for writable.  */
#endif

#ifndef X_OK
#define	X_OK	1		/* Test for executable.  */
#endif

#ifndef F_OK
#define	F_OK	0		/* Test for existence.  */
#endif

/* usually in <unistd.h> */
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

/* may be in limits.h, included from various places */
#ifndef PATH_MAX
# if defined(_POSIX_PATH_MAX)
#  define PATH_MAX _POSIX_PATH_MAX
# elif defined(MAXPATHLEN)
#  define PATH_MAX MAXPATHLEN
# else
#  define PATH_MAX 255		/* the Posix minimum pathsize */
# endif
#endif

/* We use isascii only to guard against use of 7-bit ctype tables in the
 * isprint test in infocmp.
 */
#if !HAVE_ISASCII
# undef isascii
# if ('z'-'a' == 25) && ('z' < 127) && ('Z'-'A' == 25) && ('Z' < 127) && ('9' < 127)
#  define isascii(c) (UChar(c) <= 127)
# else
#  define isascii(c) 1		/* not really ascii anyway */
# endif
#endif

#define UChar(c)    ((unsigned char)(c))

#define SIZEOF(v) (sizeof(v)/sizeof(v[0]))
