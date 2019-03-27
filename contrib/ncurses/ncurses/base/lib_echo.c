/****************************************************************************
 * Copyright (c) 1998-2008,2009 Free Software Foundation, Inc.              *
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
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
 *	echo.c
 *
 *	Routines:
 *		echo()
 *		noecho()
 *
 */

#include <curses.priv.h>

MODULE_ID("$Id: lib_echo.c,v 1.8 2009/10/24 22:02:14 tom Exp $")

NCURSES_EXPORT(int)
NCURSES_SP_NAME(echo) (NCURSES_SP_DCL0)
{
    T((T_CALLED("echo(%p)"), (void *) SP_PARM));
    if (0 == SP_PARM)
	returnCode(ERR);
    SP_PARM->_echo = TRUE;
    returnCode(OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
echo(void)
{
    return NCURSES_SP_NAME(echo) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(noecho) (NCURSES_SP_DCL0)
{
    T((T_CALLED("noecho(%p)"), (void *) SP_PARM));
    if (0 == SP_PARM)
	returnCode(ERR);
    SP_PARM->_echo = FALSE;
    returnCode(OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
noecho(void)
{
    return NCURSES_SP_NAME(noecho) (CURRENT_SCREEN);
}
#endif
