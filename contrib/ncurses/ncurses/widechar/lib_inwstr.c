/****************************************************************************
 * Copyright (c) 2002-2009,2011 Free Software Foundation, Inc.              *
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
**	lib_inwstr.c
**
**	The routines winnwstr() and winwstr().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_inwstr.c,v 1.6 2011/05/28 22:49:49 tom Exp $")

NCURSES_EXPORT(int)
winnwstr(WINDOW *win, wchar_t *wstr, int n)
{
    int row, col, inx;
    int count = 0;
    int last = 0;
    cchar_t *text;
    wchar_t wch;

    T((T_CALLED("winnwstr(%p,%p,%d)"), (void *) win, (void *) wstr, n));
    if (wstr != 0) {
	if (win) {
	    getyx(win, row, col);

	    text = win->_line[row].text;
	    while (count < n && count != ERR) {
		if (!isWidecExt(text[col])) {
		    for (inx = 0; (inx < CCHARW_MAX)
			 && ((wch = text[col].chars[inx]) != 0);
			 ++inx) {
			if (count + 1 > n) {
			    if ((count = last) == 0) {
				count = ERR;	/* error if we store nothing */
			    }
			    break;
			}
			wstr[count++] = wch;
		    }
		}
		last = count;
		if (++col > win->_maxx) {
		    break;
		}
	    }
	}
	if (count > 0) {
	    wstr[count] = '\0';
	    T(("winnwstr returns %s", _nc_viswbuf(wstr)));
	}
    }
    returnCode(count);
}

/*
 * X/Open says winwstr() returns OK if not ERR.  If that is not a blunder, it
 * must have a null termination on the string (see above).  Unlike winnstr(),
 * it does not define what happens for a negative count with winnwstr().
 */
NCURSES_EXPORT(int)
winwstr(WINDOW *win, wchar_t *wstr)
{
    int result = OK;

    T((T_CALLED("winwstr(%p,%p)"), (void *) win, (void *) wstr));
    if (win == 0) {
	result = ERR;
    } else if (winnwstr(win, wstr,
			CCHARW_MAX * (win->_maxx - win->_curx + 1)) == ERR) {
	result = ERR;
    }
    returnCode(result);
}
