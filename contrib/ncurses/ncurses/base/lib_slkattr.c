/****************************************************************************
 * Copyright (c) 1998-2009,2010 Free Software Foundation, Inc.              *
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
 *	lib_slkattr.c
 *	Soft key routines.
 *      Fetch the labels attributes
 */
#include <curses.priv.h>

MODULE_ID("$Id: lib_slkattr.c,v 1.11 2010/12/20 01:41:25 tom Exp $")

NCURSES_EXPORT(attr_t)
NCURSES_SP_NAME(slk_attr) (NCURSES_SP_DCL0)
{
    T((T_CALLED("slk_attr(%p)"), (void *) SP_PARM));

    if (SP_PARM != 0 && SP_PARM->_slk != 0) {
	attr_t result = AttrOf(SP_PARM->_slk->attr) & ALL_BUT_COLOR;
	int pair = GetPair(SP_PARM->_slk->attr);

	result |= (attr_t) ColorPair(pair);
	returnAttr(result);
    } else
	returnAttr(0);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(attr_t)
slk_attr(void)
{
    return NCURSES_SP_NAME(slk_attr) (CURRENT_SCREEN);
}
#endif
