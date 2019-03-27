/****************************************************************************
 * Copyright (c) 1998-2013,2014 Free Software Foundation, Inc.              *
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
**	lib_instr.c
**
**	The routine winnstr().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_instr.c,v 1.21 2014/02/01 22:09:27 tom Exp $")

NCURSES_EXPORT(int)
winnstr(WINDOW *win, char *str, int n)
{
    int i = 0, row, col;

    T((T_CALLED("winnstr(%p,%p,%d)"), (void *) win, str, n));

    if (!str)
	returnCode(0);

    if (win) {
	getyx(win, row, col);

	if (n < 0)
	    n = win->_maxx - win->_curx + 1;

	for (; i < n;) {
#if USE_WIDEC_SUPPORT
	    cchar_t *cell = &(win->_line[row].text[col]);
	    wchar_t *wch;
	    attr_t attrs;
	    NCURSES_PAIRS_T pair;
	    int n2;
	    bool done = FALSE;
	    mbstate_t state;
	    size_t i3, n3;
	    char *tmp;

	    if (!isWidecExt(*cell)) {
		n2 = getcchar(cell, 0, 0, 0, 0);
		if (n2 > 0
		    && (wch = typeCalloc(wchar_t, (unsigned) n2 + 1)) != 0) {
		    if (getcchar(cell, wch, &attrs, &pair, 0) == OK) {

			init_mb(state);
			n3 = wcstombs(0, wch, (size_t) 0);
			if (!isEILSEQ(n3) && (n3 != 0)) {
			    size_t need = n3 + 10 + (size_t) i;
			    int have = (int) n3 + i;

			    /* check for loop-done as well as overflow */
			    if (have > n || (int) need <= 0) {
				done = TRUE;
			    } else if ((tmp = typeCalloc(char, need)) == 0) {
				done = TRUE;
			    } else {
				init_mb(state);
				wcstombs(tmp, wch, n3);
				for (i3 = 0; i3 < n3; ++i3)
				    str[i++] = tmp[i3];
				free(tmp);
			    }
			}
		    }
		    free(wch);
		    if (done)
			break;
		}
	    }
#else
	    str[i++] = (char) CharOf(win->_line[row].text[col]);
#endif
	    if (++col > win->_maxx) {
		break;
	    }
	}
    }
    str[i] = '\0';		/* SVr4 does not seem to count the null */
    T(("winnstr returns %s", _nc_visbuf(str)));
    returnCode(i);
}
