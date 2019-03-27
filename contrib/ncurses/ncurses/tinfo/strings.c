/****************************************************************************
 * Copyright (c) 2000-2007,2012 Free Software Foundation, Inc.              *
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
**	lib_mvcur.c
**/

#include <curses.priv.h>

MODULE_ID("$Id: strings.c,v 1.8 2012/02/22 22:34:31 tom Exp $")

/****************************************************************************
 * Useful string functions (especially for mvcur)
 ****************************************************************************/

#if !HAVE_STRSTR
NCURSES_EXPORT(char *)
_nc_strstr(const char *haystack, const char *needle)
{
    size_t len1 = strlen(haystack);
    size_t len2 = strlen(needle);
    char *result = 0;

    while ((len1 != 0) && (len1-- >= len2)) {
	if (!strncmp(haystack, needle, len2)) {
	    result = (char *) haystack;
	    break;
	}
	haystack++;
    }
    return result;
}
#endif

/*
 * Initialize the descriptor so we can append to it.  Note that 'src' may
 * be a null pointer (see _nc_str_null), so the corresponding strcat and
 * strcpy calls have to allow for this.
 */
NCURSES_EXPORT(string_desc *)
_nc_str_init(string_desc * dst, char *src, size_t len)
{
    if (dst != 0) {
	dst->s_head = src;
	dst->s_tail = src;
	dst->s_size = len - 1;
	dst->s_init = dst->s_size;
	if (src != 0)
	    *src = 0;
    }
    return dst;
}

/*
 * Initialize the descriptor for only tracking the amount of memory used.
 */
NCURSES_EXPORT(string_desc *)
_nc_str_null(string_desc * dst, size_t len)
{
    return _nc_str_init(dst, 0, len);
}

/*
 * Copy a descriptor
 */
NCURSES_EXPORT(string_desc *)
_nc_str_copy(string_desc * dst, string_desc * src)
{
    *dst = *src;
    return dst;
}

/*
 * Replaces strcat into a fixed buffer, returning false on failure.
 */
NCURSES_EXPORT(bool)
_nc_safe_strcat(string_desc * dst, const char *src)
{
    if (src != 0) {
	size_t len = strlen(src);

	if (len < dst->s_size) {
	    if (dst->s_tail != 0) {
		_nc_STRCPY(dst->s_tail, src, dst->s_size);
		dst->s_tail += len;
	    }
	    dst->s_size -= len;
	    return TRUE;
	}
    }
    return FALSE;
}

/*
 * Replaces strcpy into a fixed buffer, returning false on failure.
 */
NCURSES_EXPORT(bool)
_nc_safe_strcpy(string_desc * dst, const char *src)
{
    if (src != 0) {
	size_t len = strlen(src);

	if (len < dst->s_size) {
	    if (dst->s_head != 0) {
		_nc_STRCPY(dst->s_head, src, dst->s_size);
		dst->s_tail = dst->s_head + len;
	    }
	    dst->s_size = dst->s_init - len;
	    return TRUE;
	}
    }
    return FALSE;
}
