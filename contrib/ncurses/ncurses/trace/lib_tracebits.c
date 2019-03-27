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
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: lib_tracebits.c,v 1.23 2012/06/09 19:55:46 tom Exp $")

#if HAVE_SYS_TERMIO_H
#include <sys/termio.h>		/* needed for ISC */
#endif

#ifdef __EMX__
#include <io.h>
#endif

/* may be undefined if we're using termio.h */
#ifndef TOSTOP
#define TOSTOP 0
#endif

#ifndef IEXTEN
#define IEXTEN 0
#endif

#ifndef ONLCR
#define ONLCR 0
#endif

#ifndef OCRNL
#define OCRNL 0
#endif

#ifndef ONOCR
#define ONOCR 0
#endif

#ifndef ONLRET
#define ONLRET 0
#endif

#ifdef TRACE

typedef struct {
    unsigned int val;
    const char *name;
} BITNAMES;

#define TRACE_BUF_SIZE(num) (_nc_globals.tracebuf_ptr[num].size)

static void
lookup_bits(char *buf, const BITNAMES * table, const char *label, unsigned int val)
{
    const BITNAMES *sp;

    _nc_STRCAT(buf, label, TRACE_BUF_SIZE(0));
    _nc_STRCAT(buf, ": {", TRACE_BUF_SIZE(0));
    for (sp = table; sp->name; sp++)
	if (sp->val != 0
	    && (val & sp->val) == sp->val) {
	    _nc_STRCAT(buf, sp->name, TRACE_BUF_SIZE(0));
	    _nc_STRCAT(buf, ", ", TRACE_BUF_SIZE(0));
	}
    if (buf[strlen(buf) - 2] == ',')
	buf[strlen(buf) - 2] = '\0';
    _nc_STRCAT(buf, "} ", TRACE_BUF_SIZE(0));
}

NCURSES_EXPORT(char *)
_nc_trace_ttymode(TTY * tty)
/* describe the state of the terminal control bits exactly */
{
    char *buf;

#ifdef TERMIOS
    static const BITNAMES iflags[] =
    {
	{BRKINT, "BRKINT"},
	{IGNBRK, "IGNBRK"},
	{IGNPAR, "IGNPAR"},
	{PARMRK, "PARMRK"},
	{INPCK, "INPCK"},
	{ISTRIP, "ISTRIP"},
	{INLCR, "INLCR"},
	{IGNCR, "IGNC"},
	{ICRNL, "ICRNL"},
	{IXON, "IXON"},
	{IXOFF, "IXOFF"},
	{0, NULL}
#define ALLIN	(BRKINT|IGNBRK|IGNPAR|PARMRK|INPCK|ISTRIP|INLCR|IGNCR|ICRNL|IXON|IXOFF)
    }, oflags[] =
    {
	{OPOST, "OPOST"},
	{OFLAGS_TABS, "XTABS"},
	{ONLCR, "ONLCR"},
	{OCRNL, "OCRNL"},
	{ONOCR, "ONOCR"},
	{ONLRET, "ONLRET"},
	{0, NULL}
#define ALLOUT	(OPOST|OFLAGS_TABS|ONLCR|OCRNL|ONOCR|ONLRET)
    }, cflags[] =
    {
	{CLOCAL, "CLOCAL"},
	{CREAD, "CREAD"},
	{CSTOPB, "CSTOPB"},
#if !defined(CS5) || !defined(CS8)
	{CSIZE, "CSIZE"},
#endif
	{HUPCL, "HUPCL"},
	{PARENB, "PARENB"},
	{PARODD | PARENB, "PARODD"},	/* concession to readability */
	{0, NULL}
#define ALLCTRL	(CLOCAL|CREAD|CSIZE|CSTOPB|HUPCL|PARENB|PARODD)
    }, lflags[] =
    {
	{ECHO, "ECHO"},
	{ECHOE | ECHO, "ECHOE"},	/* concession to readability */
	{ECHOK | ECHO, "ECHOK"},	/* concession to readability */
	{ECHONL, "ECHONL"},
	{ICANON, "ICANON"},
	{ISIG, "ISIG"},
	{NOFLSH, "NOFLSH"},
	{TOSTOP, "TOSTOP"},
	{IEXTEN, "IEXTEN"},
	{0, NULL}
#define ALLLOCAL	(ECHO|ECHONL|ICANON|ISIG|NOFLSH|TOSTOP|IEXTEN)
    };

    buf = _nc_trace_buf(0,
			8 + sizeof(iflags) +
			8 + sizeof(oflags) +
			8 + sizeof(cflags) +
			8 + sizeof(lflags) +
			8);
    if (buf != 0) {

	if (tty->c_iflag & ALLIN)
	    lookup_bits(buf, iflags, "iflags", tty->c_iflag);

	if (tty->c_oflag & ALLOUT)
	    lookup_bits(buf, oflags, "oflags", tty->c_oflag);

	if (tty->c_cflag & ALLCTRL)
	    lookup_bits(buf, cflags, "cflags", tty->c_cflag);

#if defined(CS5) && defined(CS8)
	{
	    static struct {
		int value;
		const char *name;
	    } csizes[] = {
#define CS_DATA(name) { name, #name " " }
		CS_DATA(CS5),
#ifdef CS6
		    CS_DATA(CS6),
#endif
#ifdef CS7
		    CS_DATA(CS7),
#endif
		    CS_DATA(CS8),
	    };
	    const char *result = "CSIZE? ";
	    int value = (int) (tty->c_cflag & CSIZE);
	    unsigned n;

	    if (value != 0) {
		for (n = 0; n < SIZEOF(csizes); n++) {
		    if (csizes[n].value == value) {
			result = csizes[n].name;
			break;
		    }
		}
	    }
	    _nc_STRCAT(buf, result, TRACE_BUF_SIZE(0));
	}
#endif

	if (tty->c_lflag & ALLLOCAL)
	    lookup_bits(buf, lflags, "lflags", tty->c_lflag);
    }
#else
    /* reference: ttcompat(4M) on SunOS 4.1 */
#ifndef EVENP
#define EVENP 0
#endif
#ifndef LCASE
#define LCASE 0
#endif
#ifndef LLITOUT
#define LLITOUT 0
#endif
#ifndef ODDP
#define ODDP 0
#endif
#ifndef TANDEM
#define TANDEM 0
#endif

    static const BITNAMES cflags[] =
    {
	{CBREAK, "CBREAK"},
	{CRMOD, "CRMOD"},
	{ECHO, "ECHO"},
	{EVENP, "EVENP"},
	{LCASE, "LCASE"},
	{LLITOUT, "LLITOUT"},
	{ODDP, "ODDP"},
	{RAW, "RAW"},
	{TANDEM, "TANDEM"},
	{XTABS, "XTABS"},
	{0, NULL}
#define ALLCTRL	(CBREAK|CRMOD|ECHO|EVENP|LCASE|LLITOUT|ODDP|RAW|TANDEM|XTABS)
    };

    buf = _nc_trace_buf(0,
			8 + sizeof(cflags));
    if (buf != 0) {
	if (tty->sg_flags & ALLCTRL) {
	    lookup_bits(buf, cflags, "cflags", tty->sg_flags);
	}
    }
#endif
    return (buf);
}

NCURSES_EXPORT(char *)
_nc_tracebits(void)
{
    return _nc_trace_ttymode(&(cur_term->Nttyb));
}
#else
EMPTY_MODULE(_nc_empty_lib_tracebits)
#endif /* TRACE */
