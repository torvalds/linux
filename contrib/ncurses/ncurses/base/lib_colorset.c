/****************************************************************************
 * Copyright (c) 1998-2009,2014 Free Software Foundation, Inc.              *
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
 *  Author: Juergen Pfeifer,  1998                                          *
 *     and: Thomas E. Dickey, 2005-on                                       *
 ****************************************************************************/

/*
**	lib_colorset.c
**
**	The routine wcolor_set().
**
*/

#include <curses.priv.h>
#include <ctype.h>

MODULE_ID("$Id: lib_colorset.c,v 1.14 2014/02/01 22:10:42 tom Exp $")

NCURSES_EXPORT(int)
wcolor_set(WINDOW *win, NCURSES_PAIRS_T color_pair_number, void *opts)
{
    int code = ERR;

    T((T_CALLED("wcolor_set(%p,%d)"), (void *) win, (int) color_pair_number));
    if (win
	&& !opts
	&& (SP != 0)
	&& (color_pair_number >= 0)
	&& (color_pair_number < SP->_pair_limit)) {
	TR(TRACE_ATTRS, ("... current %ld", (long) GET_WINDOW_PAIR(win)));
	SET_WINDOW_PAIR(win, color_pair_number);
	if_EXT_COLORS(win->_color = color_pair_number);
	code = OK;
    }
    returnCode(code);
}
