/****************************************************************************
 * Copyright (c) 2013 Free Software Foundation, Inc.                        *
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
 *  Author: Thomas E. Dickey                        2013                    *
 ****************************************************************************/

/*
**	Support for obsolete features.
*/

#include <curses.priv.h>

MODULE_ID("$Id: obsolete.c,v 1.1 2013/01/26 22:07:51 tom Exp $")

/*
 * Obsolete entrypoint retained for binary compatbility.
 */
NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_set_buffer) (NCURSES_SP_DCLx FILE *ofp, int buffered)
{
#if NCURSES_SP_FUNCS
    (void) SP_PARM;
#endif
    (void) ofp;
    (void) buffered;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_set_buffer(FILE *ofp, int buffered)
{
    NCURSES_SP_NAME(_nc_set_buffer) (CURRENT_SCREEN, ofp, buffered);
}
#endif

#if !HAVE_STRDUP
NCURSES_EXPORT(char *)
_nc_strdup(const char *s)
{
    char *result = 0;
    if (s != 0) {
	size_t need = strlen(s);
	result = malloc(need + 1);
	if (result != 0) {
	    strcpy(result, s);
	}
    }
    return result;
}
#endif

#if USE_MY_MEMMOVE
#define DST ((char *)s1)
#define SRC ((const char *)s2)
NCURSES_EXPORT(void *)
_nc_memmove(void *s1, const void *s2, size_t n)
{
    if (n != 0) {
	if ((DST + n > SRC) && (SRC + n > DST)) {
	    static char *bfr;
	    static size_t length;
	    register size_t j;
	    if (length < n) {
		length = (n * 3) / 2;
		bfr = typeRealloc(char, length, bfr);
	    }
	    for (j = 0; j < n; j++)
		bfr[j] = SRC[j];
	    s2 = bfr;
	}
	while (n-- != 0)
	    DST[n] = SRC[n];
    }
    return s1;
}
#endif /* USE_MY_MEMMOVE */
