/****************************************************************************
 * Copyright (c) 1998-2009,2013 Free Software Foundation, Inc.              *
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
 *     and: Thomas E. Dickey                        1996-2003               *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
**	lib_has_cap.c
**
**	The routines to query terminal capabilities
**
*/

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_has_cap.c,v 1.10 2013/11/16 19:57:22 tom Exp $")

NCURSES_EXPORT(bool)
NCURSES_SP_NAME(has_ic) (NCURSES_SP_DCL0)
{
    bool code = FALSE;

    T((T_CALLED("has_ic(%p)"), (void *) SP_PARM));

    if (HasTInfoTerminal(SP_PARM)) {
	code = ((insert_character || parm_ich
		 || (enter_insert_mode && exit_insert_mode))
		&& (delete_character || parm_dch)) ? TRUE : FALSE;
    }

    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
has_ic(void)
{
    return NCURSES_SP_NAME(has_ic) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(bool)
NCURSES_SP_NAME(has_il) (NCURSES_SP_DCL0)
{
    bool code = FALSE;
    T((T_CALLED("has_il(%p)"), (void *) SP_PARM));
    if (HasTInfoTerminal(SP_PARM)) {
	code = ((insert_line || parm_insert_line)
		&& (delete_line || parm_delete_line)) ? TRUE : FALSE;
    }

    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
has_il(void)
{
    return NCURSES_SP_NAME(has_il) (CURRENT_SCREEN);
}
#endif
