/****************************************************************************
 * Copyright (c) 1998-2010,2013 Free Software Foundation, Inc.              *
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
 ****************************************************************************/

/*
 *	lib_baudrate.c
 *
 */

#include <curses.priv.h>
#include <termcap.h>		/* ospeed */
#if defined(__FreeBSD__)
#include <sys/param.h>
#endif

/*
 * These systems use similar header files, which define B1200 as 1200, etc.,
 * but can be overridden by defining USE_OLD_TTY so B1200 is 9, which makes all
 * of the indices up to B115200 fit nicely in a 'short', allowing us to retain
 * ospeed's type for compatibility.
 */
#if NCURSES_OSPEED_COMPAT && ((defined(__FreeBSD__) && (__FreeBSD_version < 700000)) || defined(__NetBSD__) || defined(__OpenBSD__))
#undef B0
#undef B50
#undef B75
#undef B110
#undef B134
#undef B150
#undef B200
#undef B300
#undef B600
#undef B1200
#undef B1800
#undef B2400
#undef B4800
#undef B9600
#undef B19200
#undef EXTA
#undef B38400
#undef EXTB
#undef B57600
#undef B115200
#undef B230400
#undef B460800
#undef B921600
#define USE_OLD_TTY
#include <sys/ttydev.h>
#else
#undef USE_OLD_TTY
#endif /* USE_OLD_TTY */

MODULE_ID("$Id: lib_baudrate.c,v 1.34 2013/12/15 01:29:02 tom Exp $")

/*
 *	int
 *	baudrate()
 *
 *	Returns the current terminal's baud rate.
 *
 */

struct speed {
    NCURSES_OSPEED s;		/* values for 'ospeed' */
    int sp;			/* the actual speed */
};

#define DATA(number) { (NCURSES_OSPEED)B##number, number }

static struct speed const speeds[] =
{
    DATA(0),
    DATA(50),
    DATA(75),
    DATA(110),
    DATA(134),
    DATA(150),
    DATA(200),
    DATA(300),
    DATA(600),
    DATA(1200),
    DATA(1800),
    DATA(2400),
    DATA(4800),
    DATA(9600),
#ifdef B19200
    DATA(19200),
#elif defined(EXTA)
    {EXTA, 19200},
#endif
#ifdef B38400
    DATA(38400),
#elif defined(EXTB)
    {EXTB, 38400},
#endif
#ifdef B57600
    DATA(57600),
#endif
    /* ifdef to prevent overflow when OLD_TTY is not available */
#if !(NCURSES_OSPEED_COMPAT && defined(__FreeBSD__) && (__FreeBSD_version > 700000))
#ifdef B115200
    DATA(115200),
#endif
#ifdef B230400
    DATA(230400),
#endif
#ifdef B460800
    DATA(460800),
#endif
#ifdef B921600
    DATA(921600),
#endif
#endif
};

NCURSES_EXPORT(int)
_nc_baudrate(int OSpeed)
{
#if !USE_REENTRANT
    static int last_OSpeed;
    static int last_baudrate;
#endif

    int result = ERR;
    unsigned i;

#if !USE_REENTRANT
    if (OSpeed == last_OSpeed) {
	result = last_baudrate;
    }
#endif
    if (result == ERR) {
	if (OSpeed >= 0) {
	    for (i = 0; i < SIZEOF(speeds); i++) {
		if (speeds[i].s == OSpeed) {
		    result = speeds[i].sp;
		    break;
		}
	    }
	}
#if !USE_REENTRANT
	if (OSpeed != last_OSpeed) {
	    last_OSpeed = OSpeed;
	    last_baudrate = result;
	}
#endif
    }
    return (result);
}

NCURSES_EXPORT(int)
_nc_ospeed(int BaudRate)
{
    int result = 1;
    unsigned i;

    if (BaudRate >= 0) {
	for (i = 0; i < SIZEOF(speeds); i++) {
	    if (speeds[i].sp == BaudRate) {
		result = speeds[i].s;
		break;
	    }
	}
    }
    return (result);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(baudrate) (NCURSES_SP_DCL0)
{
    int result;

    T((T_CALLED("baudrate(%p)"), (void *) SP_PARM));

    /*
     * In debugging, allow the environment symbol to override when we're
     * redirecting to a file, so we can construct repeatable test-cases
     * that take into account costs that depend on baudrate.
     */
#ifdef TRACE
    if (IsValidTIScreen(SP_PARM)
	&& !isatty(fileno(SP_PARM ? SP_PARM->_ofp : stdout))
	&& getenv("BAUDRATE") != 0) {
	int ret;
	if ((ret = _nc_getenv_num("BAUDRATE")) <= 0)
	    ret = 9600;
	ospeed = (NCURSES_OSPEED) _nc_ospeed(ret);
	returnCode(ret);
    }
#endif

    if (IsValidTIScreen(SP_PARM)) {
#ifdef USE_OLD_TTY
	result = (int) cfgetospeed(&(TerminalOf(SP_PARM)->Nttyb));
	ospeed = (NCURSES_OSPEED) _nc_ospeed(result);
#else /* !USE_OLD_TTY */
#ifdef TERMIOS
	ospeed = (NCURSES_OSPEED) cfgetospeed(&(TerminalOf(SP_PARM)->Nttyb));
#else
	ospeed = (NCURSES_OSPEED) TerminalOf(SP_PARM)->Nttyb.sg_ospeed;
#endif
	result = _nc_baudrate(ospeed);
#endif
	TerminalOf(SP_PARM)->_baudrate = result;
    } else {
	result = ERR;
    }

    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
baudrate(void)
{
    return NCURSES_SP_NAME(baudrate) (CURRENT_SCREEN);
}
#endif
