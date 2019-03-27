/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey                    1996-on                     *
 ****************************************************************************/
/* $Id: nc_alloc.h,v 1.22 2013/01/26 21:56:51 tom Exp $ */

#ifndef NC_ALLOC_included
#define NC_ALLOC_included 1
/* *INDENT-OFF* */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HAVE_LIBDMALLOC) && HAVE_LIBDMALLOC
#include <string.h>
#undef strndup		/* workaround for #define in GLIBC 2.7 */
#include <dmalloc.h>    /* Gray Watson's library */
#else
#undef  HAVE_LIBDMALLOC
#define HAVE_LIBDMALLOC 0
#endif

#if defined(HAVE_LIBDBMALLOC) && HAVE_LIBDBMALLOC
#include <dbmalloc.h>   /* Conor Cahill's library */
#else
#undef  HAVE_LIBDBMALLOC
#define HAVE_LIBDBMALLOC 0
#endif

#if defined(HAVE_LIBMPATROL) && HAVE_LIBMPATROL
#include <mpatrol.h>    /* Memory-Patrol library */
#else
#undef  HAVE_LIBMPATROL
#define HAVE_LIBMPATROL 0
#endif

#ifndef NO_LEAKS
#define NO_LEAKS 0
#endif

#if HAVE_LIBDBMALLOC || HAVE_LIBDMALLOC || NO_LEAKS
#define HAVE_NC_FREEALL 1
struct termtype;
extern NCURSES_EXPORT(void) _nc_free_and_exit(int) GCC_NORETURN;
extern NCURSES_EXPORT(void) _nc_free_tinfo(int) GCC_NORETURN;
extern NCURSES_EXPORT(void) _nc_free_tic(int) GCC_NORETURN;
extern NCURSES_EXPORT(void) _nc_free_tparm(void);
extern NCURSES_EXPORT(void) _nc_leaks_dump_entry(void);
extern NCURSES_EXPORT(void) _nc_leaks_tic(void);

#if NCURSES_SP_FUNCS
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(_nc_free_and_exit)(SCREEN*, int) GCC_NORETURN;
#endif

#define ExitProgram(code) _nc_free_and_exit(code)

#endif /* NO_LEAKS, etc */

#ifndef HAVE_NC_FREEALL
#define HAVE_NC_FREEALL 0
#endif

#ifndef ExitProgram
#define ExitProgram(code) exit(code)
#endif

/* doalloc.c */
extern NCURSES_EXPORT(void *) _nc_doalloc(void *, size_t);
#if !HAVE_STRDUP
#undef strdup
#define strdup _nc_strdup
extern NCURSES_EXPORT(char *) _nc_strdup(const char *);
#endif

/* entries.c */
extern NCURSES_EXPORT(void) _nc_leaks_tinfo(void);

#define typeMalloc(type,elts) (type *)malloc((size_t)(elts)*sizeof(type))
#define typeCalloc(type,elts) (type *)calloc((size_t)(elts),sizeof(type))
#define typeRealloc(type,elts,ptr) (type *)_nc_doalloc(ptr, (size_t)(elts)*sizeof(type))

#ifdef __cplusplus
}
#endif

/* *INDENT-ON* */

#endif /* NC_ALLOC_included */
