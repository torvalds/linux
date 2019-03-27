/****************************************************************************
 * Copyright (c) 2010,2012 Free Software Foundation, Inc.                   *
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
 *  Author: Thomas E. Dickey                        2010                    *
 ****************************************************************************/

/*
 * $Id: build.priv.h,v 1.9 2012/02/22 22:17:02 tom Exp $
 *
 *	build.priv.h
 *
 *	This is a reduced version of curses.priv.h, for build-time utilties.
 *	Because it has fewer dependencies, this simplifies cross-compiling.
 *
 */

#ifndef CURSES_PRIV_H
#define CURSES_PRIV_H 1

#include <ncurses_dll.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <ncurses_cfg.h>

#if USE_RCS_IDS
#define MODULE_ID(id) static const char Ident[] = id;
#else
#define MODULE_ID(id) /*nothing*/
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <assert.h>
#include <stdio.h>

#include <errno.h>

#include <curses.h>	/* we'll use -Ipath directive to get the right one! */

/* usually in <unistd.h> */
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#define FreeAndNull(p)   free(p); p = 0
#define UChar(c)         ((unsigned char)(c))
#define SIZEOF(v)        (sizeof(v) / sizeof(v[0]))

#include <nc_alloc.h>
#include <nc_string.h>

/* declare these, to avoid needing term.h */
#if BROKEN_LINKER || USE_REENTRANT
#define NCURSES_ARRAY(name) \
	NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, name)

NCURSES_ARRAY(boolnames);
NCURSES_ARRAY(boolfnames);
NCURSES_ARRAY(numnames);
NCURSES_ARRAY(numfnames);
NCURSES_ARRAY(strnames);
NCURSES_ARRAY(strfnames);
#endif

#if NO_LEAKS
NCURSES_EXPORT(void) _nc_names_leaks(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CURSES_PRIV_H */
