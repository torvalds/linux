/****************************************************************************
 * Copyright (c) 1998-2009,2012 Free Software Foundation, Inc.              *
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
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 *	lib_tracechr.c - Tracing/Debugging routines
 */
#include <curses.priv.h>

#include <ctype.h>

MODULE_ID("$Id: lib_tracechr.c,v 1.22 2012/02/22 22:40:24 tom Exp $")

#ifdef TRACE

#define MyBufSize sizeof(_nc_globals.tracechr_buf)

NCURSES_EXPORT(char *)
_nc_tracechar(SCREEN *sp, int ch)
{
    NCURSES_CONST char *name;
    char *MyBuffer = ((sp != 0)
		      ? sp->tracechr_buf
		      : _nc_globals.tracechr_buf);

    if (ch > KEY_MIN || ch < 0) {
	name = safe_keyname(SP_PARM, ch);
	if (name == 0 || *name == '\0')
	    name = "NULL";
	_nc_SPRINTF(MyBuffer, _nc_SLIMIT(MyBufSize)
		    "'%.30s' = %#03o", name, ch);
    } else if (!is8bits(ch) || !isprint(UChar(ch))) {
	/*
	 * workaround for glibc bug:
	 * sprintf changes the result from unctrl() to an empty string if it
	 * does not correspond to a valid multibyte sequence.
	 */
	_nc_SPRINTF(MyBuffer, _nc_SLIMIT(MyBufSize)
		    "%#03o", ch);
    } else {
	name = safe_unctrl(SP_PARM, (chtype) ch);
	if (name == 0 || *name == 0)
	    name = "null";	/* shouldn't happen */
	_nc_SPRINTF(MyBuffer, _nc_SLIMIT(MyBufSize)
		    "'%.30s' = %#03o", name, ch);
    }
    return (MyBuffer);
}

NCURSES_EXPORT(char *)
_tracechar(int ch)
{
    return _nc_tracechar(CURRENT_SCREEN, ch);
}
#else
EMPTY_MODULE(_nc_lib_tracechr)
#endif
