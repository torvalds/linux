/****************************************************************************
 * Copyright (c) 2012,2013 Free Software Foundation, Inc.                   *
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

#include <curses.priv.h>

#if USE_WIDEC_SUPPORT

MODULE_ID("$Id: widechars.c,v 1.5 2013/03/02 18:55:51 tom Exp $")

#if defined(__MINGW32__)
/*
 * MinGW has wide-character functions, but they do not work correctly.
 */

int
_nc_mbtowc(wchar_t *pwc, const char *s, size_t n)
{
    int result;
    int count;
    int try;

    if (s != 0 && n != 0) {
	/*
	 * MultiByteToWideChar() can decide to return more than one
	 * wide-character.  We want only one.  Ignore any trailing null, both
	 * in the initial count and in the conversion.
	 */
	count = 0;
	for (try = 1; try <= (int) n; ++try) {
	    count = MultiByteToWideChar(CP_UTF8,
					MB_ERR_INVALID_CHARS,
					s,
					try,
					pwc,
					0);
	    TR(TRACE_BITS, ("...try %d:%d", try, count));
	    if (count > 0) {
		break;
	    }
	}
	if (count < 1 || count > 2) {
	    result = -1;
	} else {
	    wchar_t actual[2];
	    memset(&actual, 0, sizeof(actual));
	    count = MultiByteToWideChar(CP_UTF8,
					MB_ERR_INVALID_CHARS,
					s,
					try,
					actual,
					2);
	    TR(TRACE_BITS, ("\twin32 ->%#x, %#x", actual[0], actual[1]));
	    *pwc = actual[0];
	    if (actual[1] != 0)
		result = -1;
	    else
		result = try;
	}
    } else {
	result = 0;
    }

    return result;
}

int
_nc_mblen(const char *s, size_t n)
{
    int result = -1;
    int count;
    wchar_t temp;

    if (s != 0 && n != 0) {
	count = _nc_mbtowc(&temp, s, n);
	if (count == 1) {
	    int check = WideCharToMultiByte(CP_UTF8,
					    0,
					    &temp,
					    1,
					    NULL,
					    0,	/* compute length only */
					    NULL,
					    NULL);
	    TR(TRACE_BITS, ("\tcheck ->%d\n", check));
	    if (check > 0 && (size_t) check <= n) {
		result = check;
	    }
	}
    } else {
	result = 0;
    }

    return result;
}

int __MINGW_NOTHROW
_nc_wctomb(char *s, wchar_t wc)
{
    int result;
    int check;

    check = WideCharToMultiByte(CP_UTF8,
				0,
				&wc,
				1,
				NULL,
				0,	/* compute length only */
				NULL,
				NULL);
    if (check > 0) {
	result = WideCharToMultiByte(CP_UTF8,
				     0,
				     &wc,
				     1,
				     s,
				     check + 1,
				     NULL,
				     NULL);
    } else {
	result = -1;
    }
    return result;
}

#endif /* __MINGW32__ */

#endif /* USE_WIDEC_SUPPORT */
