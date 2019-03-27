/****************************************************************************
 * Copyright (c) 1998-2005,2009 Free Software Foundation, Inc.              *
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
 *  Author:  Juergen Pfeifer, 1997                                          *
 *     and:  Thomas E. Dickey 2005                                          *
 ****************************************************************************/

/*
 *	lib_slkatrset.c
 *	Soft key routines.
 *      Set the labels attributes
 */
#include <curses.priv.h>

MODULE_ID("$Id: lib_slkatrset.c,v 1.10 2009/10/24 22:12:21 tom Exp $")

NCURSES_EXPORT(int)
NCURSES_SP_NAME(slk_attrset) (NCURSES_SP_DCLx const chtype attr)
{
    T((T_CALLED("slk_attrset(%p,%s)"), (void *) SP_PARM, _traceattr(attr)));

    if (SP_PARM != 0 && SP_PARM->_slk != 0) {
	SetAttr(SP_PARM->_slk->attr, attr);
	returnCode(OK);
    } else
	returnCode(ERR);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
slk_attrset(const chtype attr)
{
    return NCURSES_SP_NAME(slk_attrset) (CURRENT_SCREEN, attr);
}
#endif
