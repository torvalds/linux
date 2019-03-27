/****************************************************************************
 * Copyright (c) 1998-2010,2014 Free Software Foundation, Inc.              *
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
 *     and: Sven Verdoolaege                        2001                    *
 *     and: Thomas E. Dickey                        2005                    *
 ****************************************************************************/

/*
**	lib_chgat.c
**
**	The routine wchgat().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_chgat.c,v 1.10 2014/02/01 22:13:31 tom Exp $")

NCURSES_EXPORT(int)
wchgat(WINDOW *win,
       int n,
       attr_t attr,
       NCURSES_PAIRS_T color,
       const void *opts GCC_UNUSED)
{
    int i;

    T((T_CALLED("wchgat(%p,%d,%s,%d)"),
       (void *) win,
       n,
       _traceattr(attr),
       (int) color));

    if (win) {
	struct ldat *line = &(win->_line[win->_cury]);

	toggle_attr_on(attr, ColorPair(color));

	for (i = win->_curx; i <= win->_maxx && (n == -1 || (n-- > 0)); i++) {
	    SetAttr(line->text[i], attr);
	    SetPair(line->text[i], color);
	    CHANGED_CELL(line, i);
	}

	returnCode(OK);
    } else
	returnCode(ERR);
}
