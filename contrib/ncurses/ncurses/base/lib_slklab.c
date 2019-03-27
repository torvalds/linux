/****************************************************************************
 * Copyright (c) 1998-2003,2009 Free Software Foundation, Inc.              *
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
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and:  Juergen Pfeifer,                       1998,2009               *
 *     and:  Thomas E. Dickey                       1998-on                 *
 ****************************************************************************/

/*
 *	lib_slklab.c
 *	Soft key routines.
 *      Fetch the label text.
 */
#include <curses.priv.h>

MODULE_ID("$Id: lib_slklab.c,v 1.10 2009/10/24 22:12:21 tom Exp $")

NCURSES_EXPORT(char *)
NCURSES_SP_NAME(slk_label) (NCURSES_SP_DCLx int n)
{
    T((T_CALLED("slk_label(%p,%d)"), (void *) SP_PARM, n));

    if (SP_PARM == 0 || SP_PARM->_slk == 0 || n < 1 || n > SP_PARM->_slk->labcnt)
	returnPtr(0);
    returnPtr(SP_PARM->_slk->ent[n - 1].ent_text);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(char *)
slk_label(int n)
{
    return NCURSES_SP_NAME(slk_label) (CURRENT_SCREEN, n);
}
#endif
