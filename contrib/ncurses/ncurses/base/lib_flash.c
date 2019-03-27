/****************************************************************************
 * Copyright (c) 1998-2011,2013 Free Software Foundation, Inc.              *
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
 *	flash.c
 *
 *	The routine flash().
 *
 */

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_flash.c,v 1.13 2013/01/12 17:26:07 tom Exp $")

/*
 *	flash()
 *
 *	Flash the current terminal's screen if possible.   If not,
 *	sound the audible bell if one exists.
 *
 */

NCURSES_EXPORT(int)
NCURSES_SP_NAME(flash) (NCURSES_SP_DCL0)
{
    int res = ERR;

    T((T_CALLED("flash(%p)"), (void *) SP_PARM));
#ifdef USE_TERM_DRIVER
    if (SP_PARM != 0)
	res = CallDriver_1(SP_PARM, doBeepOrFlash, FALSE);
#else
    if (HasTerminal(SP_PARM)) {
	/* FIXME: should make sure that we are not in altchar mode */
	if (flash_screen) {
	    res = NCURSES_PUTP2_FLUSH("flash_screen", flash_screen);
	} else if (bell) {
	    res = NCURSES_PUTP2_FLUSH("bell", bell);
	}
    }
#endif
    returnCode(res);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
flash(void)
{
    return NCURSES_SP_NAME(flash) (CURRENT_SCREEN);
}
#endif
