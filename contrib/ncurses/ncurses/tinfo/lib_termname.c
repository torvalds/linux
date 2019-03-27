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

#include <curses.priv.h>

MODULE_ID("$Id: lib_termname.c,v 1.12 2009/10/24 21:56:58 tom Exp $")

NCURSES_EXPORT(char *)
NCURSES_SP_NAME(termname) (NCURSES_SP_DCL0)
{
    char *name = 0;

    T((T_CALLED("termname(%p)"), (void *) SP_PARM));

#if NCURSES_SP_FUNCS
    if (TerminalOf(SP_PARM) != 0) {
	name = TerminalOf(SP_PARM)->_termname;
    }
#else
    if (cur_term != 0)
	name = cur_term->_termname;
#endif

    returnPtr(name);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(char *)
termname(void)
{
    return NCURSES_SP_NAME(termname) (CURRENT_SCREEN);
}
#endif
