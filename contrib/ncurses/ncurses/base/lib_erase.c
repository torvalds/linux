/****************************************************************************
 * Copyright (c) 1998-2005,2009 Free Software Foundation, Inc.              *
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
**	lib_erase.c
**
**	The routine werase().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_erase.c,v 1.17 2009/10/24 22:32:29 tom Exp $")

NCURSES_EXPORT(int)
werase(WINDOW *win)
{
    int code = ERR;
    int y;
    NCURSES_CH_T blank;
    NCURSES_CH_T *sp, *end, *start;

    T((T_CALLED("werase(%p)"), (void *) win));

    if (win) {
	blank = win->_nc_bkgd;
	for (y = 0; y <= win->_maxy; y++) {
	    start = win->_line[y].text;
	    end = &start[win->_maxx];

	    /*
	     * If this is a derived window, we have to handle the case where
	     * a multicolumn character extends into the window that we are
	     * erasing.
	     */
	    if_WIDEC({
		if (isWidecExt(start[0])) {
		    int x = (win->_parent != 0) ? (win->_begx) : 0;
		    while (x-- > 0) {
			if (isWidecBase(start[-1])) {
			    --start;
			    break;
			}
			--start;
		    }
		}
	    });

	    for (sp = start; sp <= end; sp++)
		*sp = blank;

	    win->_line[y].firstchar = 0;
	    win->_line[y].lastchar = win->_maxx;
	}
	win->_curx = win->_cury = 0;
	win->_flags &= ~_WRAPPED;
	_nc_synchook(win);
	code = OK;
    }
    returnCode(code);
}
