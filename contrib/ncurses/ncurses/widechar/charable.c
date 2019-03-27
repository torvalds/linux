/****************************************************************************
 * Copyright (c) 2003-2005,2008 Free Software Foundation, Inc.              *
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
**	Support functions for wide/narrow conversion.
*/

#include <curses.priv.h>

MODULE_ID("$Id: charable.c,v 1.5 2008/07/05 20:51:41 tom Exp $")

NCURSES_EXPORT(bool) _nc_is_charable(wchar_t ch)
{
    bool result;
#if HAVE_WCTOB
    result = (wctob((wint_t) ch) == (int) ch);
#else
    result = (_nc_to_char(ch) >= 0);
#endif
    return result;
}

NCURSES_EXPORT(int) _nc_to_char(wint_t ch)
{
    int result;
#if HAVE_WCTOB
    result = wctob(ch);
#elif HAVE_WCTOMB
    char temp[MB_LEN_MAX];
    result = wctomb(temp, ch);
    if (strlen(temp) == 1)
	result = UChar(temp[0]);
    else
	result = -1;
#endif
    return result;
}

NCURSES_EXPORT(wint_t) _nc_to_widechar(int ch)
{
    wint_t result;
#if HAVE_BTOWC
    result = btowc(ch);
#elif HAVE_MBTOWC
    wchar_t convert;
    char temp[2];
    temp[0] = ch;
    temp[1] = '\0';
    if (mbtowc(&convert, temp, 1) >= 0)
	result = convert;
    else
	result = WEOF;
#endif
    return result;
}
