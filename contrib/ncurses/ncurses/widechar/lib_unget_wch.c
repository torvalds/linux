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
 *  Author: Thomas E. Dickey 2002                                           *
 ****************************************************************************/

/*
**	lib_unget_wch.c
**
**	The routine unget_wch().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_unget_wch.c,v 1.15 2011/10/22 16:34:50 tom Exp $")

/*
 * Wrapper for wcrtomb() which obtains the length needed for the given
 * wide-character 'source'.
 */
NCURSES_EXPORT(size_t)
_nc_wcrtomb(char *target, wchar_t source, mbstate_t * state)
{
    int result;

    if (target == 0) {
	wchar_t temp[2];
	const wchar_t *tempp = temp;
	temp[0] = source;
	temp[1] = 0;
	result = (int) wcsrtombs(NULL, &tempp, (size_t) 0, state);
    } else {
	result = (int) wcrtomb(target, source, state);
    }
    if (!isEILSEQ(result) && (result == 0))
	result = 1;
    return (size_t) result;
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(unget_wch) (NCURSES_SP_DCLx const wchar_t wch)
{
    int result = OK;
    mbstate_t state;
    size_t length;
    int n;

    T((T_CALLED("unget_wch(%p, %#lx)"), (void *) SP_PARM, (unsigned long) wch));

    init_mb(state);
    length = _nc_wcrtomb(0, wch, &state);

    if (length != (size_t) (-1)
	&& length != 0) {
	char *string;

	if ((string = (char *) malloc(length)) != 0) {
	    init_mb(state);
	    /* ignore the result, since we already validated the character */
	    IGNORE_RC((int) wcrtomb(string, wch, &state));

	    for (n = (int) (length - 1); n >= 0; --n) {
		if (NCURSES_SP_NAME(ungetch) (NCURSES_SP_ARGx
					      UChar(string[n])) !=OK) {
		    result = ERR;
		    break;
		}
	    }
	    free(string);
	} else {
	    result = ERR;
	}
    } else {
	result = ERR;
    }

    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
unget_wch(const wchar_t wch)
{
    return NCURSES_SP_NAME(unget_wch) (CURRENT_SCREEN, wch);
}
#endif
