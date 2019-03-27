/****************************************************************************
 * Copyright (c) 1998-2002,2003 Free Software Foundation, Inc.              *
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

/* $Id: eti.h,v 1.8 2003/10/25 15:24:29 tom Exp $ */

#ifndef NCURSES_ETI_H_incl
#define NCURSES_ETI_H_incl 1

#define	E_OK			(0)
#define	E_SYSTEM_ERROR	 	(-1)
#define	E_BAD_ARGUMENT	 	(-2)
#define	E_POSTED	 	(-3)
#define	E_CONNECTED	 	(-4)
#define	E_BAD_STATE	 	(-5)
#define	E_NO_ROOM	 	(-6)
#define	E_NOT_POSTED		(-7)
#define	E_UNKNOWN_COMMAND	(-8)
#define	E_NO_MATCH		(-9)
#define	E_NOT_SELECTABLE	(-10)
#define	E_NOT_CONNECTED	        (-11)
#define	E_REQUEST_DENIED	(-12)
#define	E_INVALID_FIELD	        (-13)
#define	E_CURRENT		(-14)

#endif
