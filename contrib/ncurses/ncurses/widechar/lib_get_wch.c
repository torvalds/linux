/****************************************************************************
 * Copyright (c) 2002-2010,2011 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey 2002-on                                        *
 ****************************************************************************/

/*
**	lib_get_wch.c
**
**	The routine get_wch().
**
*/

#include <curses.priv.h>
#include <ctype.h>

MODULE_ID("$Id: lib_get_wch.c,v 1.23 2011/05/28 23:00:29 tom Exp $")

NCURSES_EXPORT(int)
wget_wch(WINDOW *win, wint_t *result)
{
    SCREEN *sp;
    int code;
    char buffer[(MB_LEN_MAX * 9) + 1];	/* allow some redundant shifts */
    int status;
    size_t count = 0;
    int value = 0;
    wchar_t wch;
#ifndef state_unused
    mbstate_t state;
#endif

    T((T_CALLED("wget_wch(%p)"), (void *) win));

    /*
     * We can get a stream of single-byte characters and KEY_xxx codes from
     * _nc_wgetch(), while we want to return a wide character or KEY_xxx code.
     */
    _nc_lock_global(curses);
    sp = _nc_screen_of(win);
    if (sp != 0) {
	for (;;) {
	    T(("reading %d of %d", (int) count + 1, (int) sizeof(buffer)));
	    code = _nc_wgetch(win, &value, TRUE EVENTLIST_2nd((_nc_eventlist
							       *) 0));
	    if (code == ERR) {
		break;
	    } else if (code == KEY_CODE_YES) {
		/*
		 * If we were processing an incomplete multibyte character,
		 * return an error since we have a KEY_xxx code which
		 * interrupts it.  For some cases, we could improve this by
		 * writing a new version of lib_getch.c(!), but it is not clear
		 * whether the improvement would be worth the effort.
		 */
		if (count != 0) {
		    safe_ungetch(SP_PARM, value);
		    code = ERR;
		}
		break;
	    } else if (count + 1 >= sizeof(buffer)) {
		safe_ungetch(SP_PARM, value);
		code = ERR;
		break;
	    } else {
		buffer[count++] = (char) UChar(value);
		reset_mbytes(state);
		status = count_mbytes(buffer, count, state);
		if (status >= 0) {
		    reset_mbytes(state);
		    if (check_mbytes(wch, buffer, count, state) != status) {
			code = ERR;	/* the two calls should match */
			safe_ungetch(SP_PARM, value);
		    }
		    value = wch;
		    break;
		}
	    }
	}
    } else {
	code = ERR;
    }

    if (result != 0)
	*result = (wint_t) value;

    _nc_unlock_global(curses);
    T(("result %#o", value));
    returnCode(code);
}
