/****************************************************************************
 * Copyright (c) 1998-2011,2012 Free Software Foundation, Inc.              *
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
 *     and: Thomas E. Dickey                        1998-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
 *	raw.c
 *
 *	Routines:
 *		raw()
 *		cbreak()
 *		noraw()
 *		nocbreak()
 *		qiflush()
 *		noqiflush()
 *		intrflush()
 *
 */

#include <curses.priv.h>

MODULE_ID("$Id: lib_raw.c,v 1.21 2012/01/21 19:21:29 KO.Myung-Hun Exp $")

#if HAVE_SYS_TERMIO_H
#include <sys/termio.h>		/* needed for ISC */
#endif

#ifdef __EMX__
#include <io.h>
#define _nc_setmode(mode) setmode(SP_PARM->_ifd, mode)
#else
#define _nc_setmode(mode)	/* nothing */
#endif

#if USE_KLIBC_KBD
#define INCL_KBD
#include <os2.h>
#endif

#define COOKED_INPUT	(IXON|BRKINT|PARMRK)

#ifdef TRACE
#define BEFORE(N)	if (USE_TRACEF(TRACE_BITS)) _nc_locked_tracef("%s before bits: %s", N, _nc_tracebits())
#define AFTER(N)	if (USE_TRACEF(TRACE_BITS)) _nc_locked_tracef("%s after bits: %s", N, _nc_tracebits())
#else
#define BEFORE(s)
#define AFTER(s)
#endif /* TRACE */

NCURSES_EXPORT(int)
NCURSES_SP_NAME(raw) (NCURSES_SP_DCL0)
{
    int result = ERR;
    TERMINAL *termp;

    T((T_CALLED("raw(%p)"), (void *) SP_PARM));
    if ((termp = TerminalOf(SP_PARM)) != 0) {
	TTY buf;

	BEFORE("raw");
	_nc_setmode(O_BINARY);

	buf = termp->Nttyb;
#ifdef TERMIOS
	buf.c_lflag &= (unsigned) ~(ICANON | ISIG | IEXTEN);
	buf.c_iflag &= (unsigned) ~(COOKED_INPUT);
	buf.c_cc[VMIN] = 1;
	buf.c_cc[VTIME] = 0;
#else
	buf.sg_flags |= RAW;
#endif
	result = NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx &buf);
	if (result == OK) {
#if USE_KLIBC_KBD
	    KBDINFO kbdinfo;

	    kbdinfo.cb = sizeof(kbdinfo);
	    KbdGetStatus(&kbdinfo, 0);

	    kbdinfo.cb = sizeof(kbdinfo);
	    kbdinfo.fsMask &= ~KEYBOARD_ASCII_MODE;
	    kbdinfo.fsMask |= KEYBOARD_BINARY_MODE;
	    KbdSetStatus(&kbdinfo, 0);
#endif
	    SP_PARM->_raw = TRUE;
	    SP_PARM->_cbreak = 1;
	    termp->Nttyb = buf;
	}
	AFTER("raw");
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
raw(void)
{
    return NCURSES_SP_NAME(raw) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(cbreak) (NCURSES_SP_DCL0)
{
    int result = ERR;
    TERMINAL *termp;

    T((T_CALLED("cbreak(%p)"), (void *) SP_PARM));
    if ((termp = TerminalOf(SP_PARM)) != 0) {
	TTY buf;

	BEFORE("cbreak");
	_nc_setmode(O_BINARY);

	buf = termp->Nttyb;
#ifdef TERMIOS
	buf.c_lflag &= (unsigned) ~ICANON;
	buf.c_iflag &= (unsigned) ~ICRNL;
	buf.c_lflag |= ISIG;
	buf.c_cc[VMIN] = 1;
	buf.c_cc[VTIME] = 0;
#else
	buf.sg_flags |= CBREAK;
#endif
	result = NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx &buf);
	if (result == OK) {
	    SP_PARM->_cbreak = 1;
	    termp->Nttyb = buf;
	}
	AFTER("cbreak");
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
cbreak(void)
{
    return NCURSES_SP_NAME(cbreak) (CURRENT_SCREEN);
}
#endif

/*
 * Note:
 * this implementation may be wrong.  See the comment under intrflush().
 */
NCURSES_EXPORT(void)
NCURSES_SP_NAME(qiflush) (NCURSES_SP_DCL0)
{
    int result = ERR;
    TERMINAL *termp;

    T((T_CALLED("qiflush(%p)"), (void *) SP_PARM));
    if ((termp = TerminalOf(SP_PARM)) != 0) {
	TTY buf;

	BEFORE("qiflush");
	buf = termp->Nttyb;
#ifdef TERMIOS
	buf.c_lflag &= (unsigned) ~(NOFLSH);
	result = NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx &buf);
#else
	/* FIXME */
#endif
	if (result == OK)
	    termp->Nttyb = buf;
	AFTER("qiflush");
    }
    returnVoid;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
qiflush(void)
{
    NCURSES_SP_NAME(qiflush) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(noraw) (NCURSES_SP_DCL0)
{
    int result = ERR;
    TERMINAL *termp;

    T((T_CALLED("noraw(%p)"), (void *) SP_PARM));
    if ((termp = TerminalOf(SP_PARM)) != 0) {
	TTY buf;

	BEFORE("noraw");
	_nc_setmode(O_TEXT);

	buf = termp->Nttyb;
#ifdef TERMIOS
	buf.c_lflag |= ISIG | ICANON |
	    (termp->Ottyb.c_lflag & IEXTEN);
	buf.c_iflag |= COOKED_INPUT;
#else
	buf.sg_flags &= ~(RAW | CBREAK);
#endif
	result = NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx &buf);
	if (result == OK) {
#if USE_KLIBC_KBD
	    KBDINFO kbdinfo;

	    kbdinfo.cb = sizeof(kbdinfo);
	    KbdGetStatus(&kbdinfo, 0);

	    kbdinfo.cb = sizeof(kbdinfo);
	    kbdinfo.fsMask &= ~KEYBOARD_BINARY_MODE;
	    kbdinfo.fsMask |= KEYBOARD_ASCII_MODE;
	    KbdSetStatus(&kbdinfo, 0);
#endif
	    SP_PARM->_raw = FALSE;
	    SP_PARM->_cbreak = 0;
	    termp->Nttyb = buf;
	}
	AFTER("noraw");
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
noraw(void)
{
    return NCURSES_SP_NAME(noraw) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(nocbreak) (NCURSES_SP_DCL0)
{
    int result = ERR;
    TERMINAL *termp;

    T((T_CALLED("nocbreak(%p)"), (void *) SP_PARM));
    if ((termp = TerminalOf(SP_PARM)) != 0) {
	TTY buf;

	BEFORE("nocbreak");
	_nc_setmode(O_TEXT);

	buf = termp->Nttyb;
#ifdef TERMIOS
	buf.c_lflag |= ICANON;
	buf.c_iflag |= ICRNL;
#else
	buf.sg_flags &= ~CBREAK;
#endif
	result = NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx &buf);
	if (result == OK) {
	    SP_PARM->_cbreak = 0;
	    termp->Nttyb = buf;
	}
	AFTER("nocbreak");
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
nocbreak(void)
{
    return NCURSES_SP_NAME(nocbreak) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(noqiflush) (NCURSES_SP_DCL0)
{
    int result = ERR;
    TERMINAL *termp;

    T((T_CALLED("noqiflush(%p)"), (void *) SP_PARM));
    if ((termp = TerminalOf(SP_PARM)) != 0) {
	TTY buf;

	BEFORE("noqiflush");
	buf = termp->Nttyb;
#ifdef TERMIOS
	buf.c_lflag |= NOFLSH;
	result = NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx &buf);
#else
	/* FIXME */
#endif
	if (result == OK)
	    termp->Nttyb = buf;
	AFTER("noqiflush");
    }
    returnVoid;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
noqiflush(void)
{
    NCURSES_SP_NAME(noqiflush) (CURRENT_SCREEN);
}
#endif

/*
 * This call does the same thing as the qiflush()/noqiflush() pair.  We know
 * for certain that SVr3 intrflush() tweaks the NOFLSH bit; on the other hand,
 * the match (in the SVr4 man pages) between the language describing NOFLSH in
 * termio(7) and the language describing qiflush()/noqiflush() in
 * curs_inopts(3x) is too exact to be coincidence.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(intrflush) (NCURSES_SP_DCLx WINDOW *win GCC_UNUSED, bool flag)
{
    int result = ERR;
    TERMINAL *termp;

    T((T_CALLED("intrflush(%p,%d)"), (void *) SP_PARM, flag));
    if (SP_PARM == 0)
	returnCode(ERR);

    if ((termp = TerminalOf(SP_PARM)) != 0) {
	TTY buf;

	BEFORE("intrflush");
	buf = termp->Nttyb;
#ifdef TERMIOS
	if (flag)
	    buf.c_lflag &= (unsigned) ~(NOFLSH);
	else
	    buf.c_lflag |= (NOFLSH);
	result = NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx &buf);
#else
	/* FIXME */
#endif
	if (result == OK) {
	    termp->Nttyb = buf;
	}
	AFTER("intrflush");
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
intrflush(WINDOW *win GCC_UNUSED, bool flag)
{
    return NCURSES_SP_NAME(intrflush) (CURRENT_SCREEN, win, flag);
}
#endif
