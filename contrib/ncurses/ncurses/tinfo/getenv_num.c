/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey <dickey@clark.net> 1998                        *
 ****************************************************************************/

/*
 *	getenv_num.c -- obtain a number from the environment
 */

#include <curses.priv.h>

MODULE_ID("$Id: getenv_num.c,v 1.6 2013/09/28 20:25:08 tom Exp $")

NCURSES_EXPORT(int)
_nc_getenv_num(const char *name)
{
    char *dst = 0;
    char *src = getenv(name);
    long value;

    if ((src == 0)
	|| (value = strtol(src, &dst, 0)) < 0
	|| (dst == src)
	|| (*dst != '\0')
	|| (int) value < value)
	value = -1;

    return (int) value;
}

NCURSES_EXPORT(void)
_nc_setenv_num(const char *name, int value)
{
    if (name != 0 && value >= 0) {
	char buffer[128];
#if HAVE_SETENV
	_nc_SPRINTF(buffer, _nc_SLIMIT(sizeof(buffer)) "%d", value);
	setenv(name, buffer, 1);
#elif HAVE_PUTENV
	char *s;
	_nc_SPRINTF(buffer, _nc_SLIMIT(sizeof(buffer)) "%s=%d", name, value);
	if ((s = strdup(buffer)) != 0)
	    putenv(s);
#endif
    }
}
