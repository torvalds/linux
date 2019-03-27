/****************************************************************************
 * Copyright (c) 2002-2007,2009 Free Software Foundation, Inc.              *
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
 * Author: Thomas Dickey                                                    *
 ****************************************************************************/

/*
**	lib_in_wchnstr.c
**
**	The routine win_wchnstr().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_in_wchnstr.c,v 1.8 2009/10/24 22:37:48 tom Exp $")

NCURSES_EXPORT(int)
win_wchnstr(WINDOW *win, cchar_t *wchstr, int n)
{
    int code = OK;

    T((T_CALLED("win_wchnstr(%p,%p,%d)"), (void *) win, (void *) wchstr, n));
    if (win != 0
	&& wchstr != 0) {
	NCURSES_CH_T *src;
	int row, col;
	int j, k, limit;

	getyx(win, row, col);
	limit = getmaxx(win) - col;
	src = &(win->_line[row].text[col]);

	if (n < 0) {
	    n = limit;
	} else if (n > limit) {
	    n = limit;
	}
	for (j = k = 0; j < n; ++j) {
	    if (j == 0 || !WidecExt(src[j]) || isWidecBase(src[j])) {
		wchstr[k++] = src[j];
	    }
	}
	memset(&(wchstr[k]), 0, sizeof(*wchstr));
	T(("result = %s", _nc_viscbuf(wchstr, n)));
    } else {
	code = ERR;
    }
    returnCode(code);
}
