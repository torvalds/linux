/****************************************************************************
 * Copyright (c) 1998-2004,2012 Free Software Foundation, Inc.              *
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
 *   Author:  Juergen Pfeifer, 1995,1997                                    *
 ****************************************************************************/

/* $Id: mf_common.h,v 0.24 2012/06/10 00:06:54 tom Exp $ */

/* Common internal header for menu and form library */

#ifndef MF_COMMON_H_incl
#define MF_COMMON_H_incl 1

#include <ncurses_cfg.h>
#include <curses.h>

#include <stdlib.h>
#include <sys/types.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#if DECL_ERRNO
extern int errno;
#endif

/* in case of debug version we ignore the suppression of assertions */
#ifdef TRACE
#  ifdef NDEBUG
#    undef NDEBUG
#  endif
#endif

#include <nc_alloc.h>

#if USE_RCS_IDS
#define MODULE_ID(id) static const char Ident[] = id;
#else
#define MODULE_ID(id)		/*nothing */
#endif

/* Maximum regular 8-bit character code */
#define MAX_REGULAR_CHARACTER (0xff)

#define SET_ERROR(code) (errno=(code))
#define GET_ERROR()     (errno)

#ifdef TRACE
#define RETURN(code)    returnCode( SET_ERROR(code) )
#else
#define RETURN(code)    return( SET_ERROR(code) )
#endif

/* The few common values in the status fields for menus and forms */
#define _POSTED         (0x01U)	/* menu or form is posted                  */
#define _IN_DRIVER      (0x02U)	/* menu or form is processing hook routine */

#define SetStatus(target,mask) (target)->status |= (unsigned short) (mask)
#define ClrStatus(target,mask) (target)->status = (unsigned short) (target->status & (~mask))

/* Call object hook */
#define Call_Hook( object, handler ) \
   if ( (object) != 0 && ((object)->handler) != (void *) 0 )\
   {\
	SetStatus(object, _IN_DRIVER);\
	(object)->handler(object);\
	ClrStatus(object, _IN_DRIVER);\
   }

#endif /* MF_COMMON_H_incl */
