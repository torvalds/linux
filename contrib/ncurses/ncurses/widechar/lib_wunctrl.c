/****************************************************************************
 * Copyright (c) 2001-2011,2012 Free Software Foundation, Inc.              *
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

/*
**	lib_wunctrl.c
**
**	The routine wunctrl().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_wunctrl.c,v 1.16 2012/12/15 20:53:42 tom Exp $")

NCURSES_EXPORT(wchar_t *)
NCURSES_SP_NAME(wunctrl) (NCURSES_SP_DCLx cchar_t *wc)
{
    static wchar_t str[CCHARW_MAX + 1], *wsp;
    wchar_t *result;

    if (wc == 0) {
	result = 0;
    } else if (SP_PARM != 0 && Charable(*wc)) {
	const char *p =
	NCURSES_SP_NAME(unctrl) (NCURSES_SP_ARGx
				 (unsigned) _nc_to_char((wint_t)CharOf(*wc)));

	for (wsp = str; *p; ++p) {
	    *wsp++ = (wchar_t) _nc_to_widechar(*p);
	}
	*wsp = 0;
	result = str;
    } else {
	result = wc->chars;
    }
    return result;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(wchar_t *)
wunctrl(cchar_t *wc)
{
    return NCURSES_SP_NAME(wunctrl) (CURRENT_SCREEN, wc);
}
#endif
