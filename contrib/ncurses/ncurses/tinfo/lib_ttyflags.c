/****************************************************************************
 * Copyright (c) 1998-2010,2012 Free Software Foundation, Inc.              *
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

/*
 *		def_prog_mode()
 *		def_shell_mode()
 *		reset_prog_mode()
 *		reset_shell_mode()
 *		savetty()
 *		resetty()
 */

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_ttyflags.c,v 1.28 2012/01/21 19:21:29 KO.Myung-Hun Exp $")

NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_get_tty_mode) (NCURSES_SP_DCLx TTY * buf)
{
    int result = OK;

    if (buf == 0 || SP_PARM == 0) {
	result = ERR;
    } else {
	TERMINAL *termp = TerminalOf(SP_PARM);

	if (0 == termp) {
	    result = ERR;
	} else {
#ifdef USE_TERM_DRIVER
	    result = CallDriver_2(SP_PARM, sgmode, FALSE, buf);
#else
	    for (;;) {
		if (GET_TTY(termp->Filedes, buf) != 0) {
		    if (errno == EINTR)
			continue;
		    result = ERR;
		}
		break;
	    }
#endif
	}

	if (result == ERR)
	    memset(buf, 0, sizeof(*buf));

	TR(TRACE_BITS, ("_nc_get_tty_mode(%d): %s",
			termp ? termp->Filedes : -1,
			_nc_trace_ttymode(buf)));
    }
    return (result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_get_tty_mode(TTY * buf)
{
    return NCURSES_SP_NAME(_nc_get_tty_mode) (CURRENT_SCREEN, buf);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_DCLx TTY * buf)
{
    int result = OK;

    if (buf == 0 || SP_PARM == 0) {
	result = ERR;
    } else {
	TERMINAL *termp = TerminalOf(SP_PARM);

	if (0 == termp) {
	    result = ERR;
	} else {
#ifdef USE_TERM_DRIVER
	    result = CallDriver_2(SP_PARM, sgmode, TRUE, buf);
#else
	    for (;;) {
		if ((SET_TTY(termp->Filedes, buf) != 0)
#if USE_KLIBC_KBD
		    && !isatty(termp->Filedes)
#endif
		    ) {
		    if (errno == EINTR)
			continue;
		    if ((errno == ENOTTY) && (SP_PARM != 0))
			SP_PARM->_notty = TRUE;
		    result = ERR;
		}
		break;
	    }
#endif
	}
	TR(TRACE_BITS, ("_nc_set_tty_mode(%d): %s",
			termp ? termp->Filedes : -1,
			_nc_trace_ttymode(buf)));
    }
    return (result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_set_tty_mode(TTY * buf)
{
    return NCURSES_SP_NAME(_nc_set_tty_mode) (CURRENT_SCREEN, buf);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(def_shell_mode) (NCURSES_SP_DCL0)
{
    int rc = ERR;
    TERMINAL *termp = TerminalOf(SP_PARM);

    T((T_CALLED("def_shell_mode(%p)"), (void *) SP_PARM));

    if (termp != 0) {
#ifdef USE_TERM_DRIVER
	rc = CallDriver_2(SP_PARM, mode, FALSE, TRUE);
#else
	/*
	 * If XTABS was on, remove the tab and backtab capabilities.
	 */
	if (_nc_get_tty_mode(&termp->Ottyb) == OK) {
#ifdef TERMIOS
	    if (termp->Ottyb.c_oflag & OFLAGS_TABS)
		tab = back_tab = NULL;
#else
	    if (termp->Ottyb.sg_flags & XTABS)
		tab = back_tab = NULL;
#endif
	    rc = OK;
	}
#endif
    }
    returnCode(rc);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
def_shell_mode(void)
{
    return NCURSES_SP_NAME(def_shell_mode) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(def_prog_mode) (NCURSES_SP_DCL0)
{
    int rc = ERR;
    TERMINAL *termp = TerminalOf(SP_PARM);

    T((T_CALLED("def_prog_mode(%p)"), (void *) SP_PARM));

    if (termp != 0) {
#ifdef USE_TERM_DRIVER
	rc = CallDriver_2(SP_PARM, mode, TRUE, TRUE);
#else
	/*
	 * Turn off the XTABS bit in the tty structure if it was on.
	 */
	if (_nc_get_tty_mode(&termp->Nttyb) == OK) {
#ifdef TERMIOS
	    termp->Nttyb.c_oflag &= (unsigned) (~OFLAGS_TABS);
#else
	    termp->Nttyb.sg_flags &= (unsigned) (~XTABS);
#endif
	    rc = OK;
	}
#endif
    }
    returnCode(rc);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
def_prog_mode(void)
{
    return NCURSES_SP_NAME(def_prog_mode) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(reset_prog_mode) (NCURSES_SP_DCL0)
{
    int rc = ERR;
    TERMINAL *termp = TerminalOf(SP_PARM);

    T((T_CALLED("reset_prog_mode(%p)"), (void *) SP_PARM));

    if (termp != 0) {
#ifdef USE_TERM_DRIVER
	rc = CallDriver_2(SP_PARM, mode, TRUE, FALSE);
#else
	if (_nc_set_tty_mode(&termp->Nttyb) == OK) {
	    if (SP_PARM) {
		if (SP_PARM->_keypad_on)
		    _nc_keypad(SP_PARM, TRUE);
		NC_BUFFERED(SP_PARM, TRUE);
	    }
	    rc = OK;
	}
#endif
    }
    returnCode(rc);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
reset_prog_mode(void)
{
    return NCURSES_SP_NAME(reset_prog_mode) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(reset_shell_mode) (NCURSES_SP_DCL0)
{
    int rc = ERR;
    TERMINAL *termp = TerminalOf(SP_PARM);

    T((T_CALLED("reset_shell_mode(%p)"), (void *) SP_PARM));

    if (termp != 0) {
#ifdef USE_TERM_DRIVER
	rc = CallDriver_2(SP_PARM, mode, FALSE, FALSE);
#else
	if (SP_PARM) {
	    _nc_keypad(SP_PARM, FALSE);
	    _nc_flush();
	    NC_BUFFERED(SP_PARM, FALSE);
	}
	rc = _nc_set_tty_mode(&termp->Ottyb);
#endif
    }
    returnCode(rc);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
reset_shell_mode(void)
{
    return NCURSES_SP_NAME(reset_shell_mode) (CURRENT_SCREEN);
}
#endif

static TTY *
saved_tty(NCURSES_SP_DCL0)
{
    TTY *result = 0;

    if (SP_PARM != 0) {
	result = (TTY *) & (SP_PARM->_saved_tty);
    } else {
	if (_nc_prescreen.saved_tty == 0) {
	    _nc_prescreen.saved_tty = typeCalloc(TTY, 1);
	}
	result = _nc_prescreen.saved_tty;
    }
    return result;
}

/*
**	savetty()  and  resetty()
**
*/

NCURSES_EXPORT(int)
NCURSES_SP_NAME(savetty) (NCURSES_SP_DCL0)
{
    T((T_CALLED("savetty(%p)"), (void *) SP_PARM));
    returnCode(NCURSES_SP_NAME(_nc_get_tty_mode) (NCURSES_SP_ARGx saved_tty(NCURSES_SP_ARG)));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
savetty(void)
{
    return NCURSES_SP_NAME(savetty) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(resetty) (NCURSES_SP_DCL0)
{
    T((T_CALLED("resetty(%p)"), (void *) SP_PARM));
    returnCode(NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx saved_tty(NCURSES_SP_ARG)));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
resetty(void)
{
    return NCURSES_SP_NAME(resetty) (CURRENT_SCREEN);
}
#endif
