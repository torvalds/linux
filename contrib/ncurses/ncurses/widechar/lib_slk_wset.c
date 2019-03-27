/****************************************************************************
 * Copyright (c) 2003-2002,2011 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey                                                *
 ****************************************************************************/

/*
 *	lib_slk_wset.c
 *      Set soft label text.
 */
#include <curses.priv.h>

#if HAVE_WCTYPE_H
#include <wctype.h>
#endif

MODULE_ID("$Id: lib_slk_wset.c,v 1.13 2011/10/22 15:52:20 tom Exp $")

NCURSES_EXPORT(int)
slk_wset(int i, const wchar_t *astr, int format)
{
    int result = ERR;
    size_t arglen;
    const wchar_t *str;
    char *mystr;
    mbstate_t state;

    T((T_CALLED("slk_wset(%d, %s, %d)"), i, _nc_viswbuf(astr), format));

    if (astr != 0) {
	init_mb(state);
	str = astr;
	if ((arglen = wcsrtombs(NULL, &str, (size_t) 0, &state)) != (size_t) -1) {
	    if ((mystr = (char *) _nc_doalloc(0, arglen + 1)) != 0) {
		str = astr;
		if (wcsrtombs(mystr, &str, arglen, &state) != (size_t) -1) {
		    /* glibc documentation claims that the terminating L'\0'
		     * is written, but it is not...
		     */
		    mystr[arglen] = 0;
		    result = slk_set(i, mystr, format);
		}
		free(mystr);
	    }
	}
    }
    returnCode(result);
}
