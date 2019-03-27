/****************************************************************************
 * Copyright (c) 2002-2009,2010 Free Software Foundation, Inc.              *
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
 *  Author: Thomas Dickey 2002                                              *
 ****************************************************************************/

/*
**	lib_hline_set.c
**
**	The routine whline_set().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_hline_set.c,v 1.4 2010/12/19 01:45:03 tom Exp $")

NCURSES_EXPORT(int)
whline_set(WINDOW *win, const cchar_t *ch, int n)
{
    int code = ERR;
    int start;
    int end;

    T((T_CALLED("whline_set(%p,%s,%d)"), (void *) win, _tracecchar_t(ch), n));

    if (win) {
	struct ldat *line = &(win->_line[win->_cury]);
	NCURSES_CH_T wch;

	start = win->_curx;
	end = start + n - 1;
	if (end > win->_maxx)
	    end = win->_maxx;

	CHANGED_RANGE(line, start, end);

	if (ch == 0)
	    wch = *WACS_HLINE;
	else
	    wch = *ch;
	wch = _nc_render(win, wch);

	while (end >= start) {
	    line->text[end] = wch;
	    end--;
	}

	_nc_synchook(win);
	code = OK;
    }
    returnCode(code);
}
