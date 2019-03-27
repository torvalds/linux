/****************************************************************************
 * Copyright (c) 1999-2009,2011 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey                        1999-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: keybound.c,v 1.11 2011/10/22 16:47:05 tom Exp $")

/*
 * Returns the count'th string definition which is associated with the
 * given keycode.  The result is malloc'd, must be freed by the caller.
 */
NCURSES_EXPORT(char *)
NCURSES_SP_NAME(keybound) (NCURSES_SP_DCLx int code, int count)
{
    char *result = 0;

    T((T_CALLED("keybound(%p, %d,%d)"), (void *) SP_PARM, code, count));
    if (SP_PARM != 0 && code >= 0) {
	result = _nc_expand_try(SP_PARM->_keytry,
				(unsigned) code,
				&count,
				(size_t) 0);
    }
    returnPtr(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(char *)
keybound(int code, int count)
{
    return NCURSES_SP_NAME(keybound) (CURRENT_SCREEN, code, count);
}
#endif
