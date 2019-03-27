/****************************************************************************
 * Copyright (c) 2009-2010,2011 Free Software Foundation, Inc.              *
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
#include <progs.priv.h>
#include <string.h>

#include <transform.h>

MODULE_ID("$Id: transform.c,v 1.3 2011/05/14 22:41:17 tom Exp $")

#ifdef SUFFIX_IGNORED
static void
trim_suffix(const char *a, size_t *len)
{
    const char ignore[] = SUFFIX_IGNORED;

    if (sizeof(ignore) != 0) {
	bool trim = FALSE;
	size_t need = (sizeof(ignore) - 1);

	if (*len > need) {
	    size_t first = *len - need;
	    size_t n;
	    trim = TRUE;
	    for (n = first; n < *len; ++n) {
		if (tolower(UChar(a[n])) != tolower(UChar(ignore[n - first]))) {
		    trim = FALSE;
		    break;
		}
	    }
	    if (trim) {
		*len -= need;
	    }
	}
    }
}
#else
#define trim_suffix(a, len)	/* nothing */
#endif

bool
same_program(const char *a, const char *b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);

    trim_suffix(a, &len_a);
    trim_suffix(b, &len_b);

    return (len_a == len_b) && (strncmp(a, b, len_a) == 0);
}
