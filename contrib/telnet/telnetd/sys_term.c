/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static const char sccsid[] = "@(#)sys_term.c	8.4+1 (Berkeley) 5/30/95";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/tty.h>
#include <libutil.h>
#include <stdlib.h>

#include "telnetd.h"
#include "pathnames.h"
#include "types.h"
#include "baud.h"

#ifdef	AUTHENTICATION
#include <libtelnet/auth.h>
#endif

int cleanopen(char *);
void scrub_env(void);

char	*envinit[3];
extern char **environ;

#define SCPYN(a, b)	(void) strncpy(a, b, sizeof(a))
#define SCMPN(a, b)	strncmp(a, b, sizeof(a))

#ifdef	t_erase
#undef	t_erase
#undef	t_kill
#undef	t_intrc
#undef	t_quitc
#undef	t_startc
#undef	t_stopc
#undef	t_eofc
#undef	t_brkc
#undef	t_suspc
#undef	t_dsuspc
#undef	t_rprntc
#undef	t_flushc
#undef	t_werasc
#undef	t_lnextc
#endif

#ifndef	USE_TERMIO
struct termbuf {
	struct sgttyb sg;
	struct tchars tc;
	struct ltchars ltc;
	int state;
	int lflags;
} termbuf, termbuf2;
# define	cfsetospeed(tp, val)	(tp)->sg.sg_ospeed = (val)
# define	cfsetispeed(tp, val)	(tp)->sg.sg_ispeed = (val)
# define	cfgetospeed(tp)		(tp)->sg.sg_ospeed
# define	cfgetispeed(tp)		(tp)->sg.sg_ispeed
#else	/* USE_TERMIO */
# ifndef	TCSANOW
#  ifdef TCSETS
#   define	TCSANOW		TCSETS
#   define	TCSADRAIN	TCSETSW
#   define	tcgetattr(f, t)	ioctl(f, TCGETS, (char *)t)
#  else
#   ifdef TCSETA
#    define	TCSANOW		TCSETA
#    define	TCSADRAIN	TCSETAW
#    define	tcgetattr(f, t)	ioctl(f, TCGETA, (char *)t)
#   else
#    define	TCSANOW		TIOCSETA
#    define	TCSADRAIN	TIOCSETAW
#    define	tcgetattr(f, t)	ioctl(f, TIOCGETA, (char *)t)
#   endif
#  endif
#  define	tcsetattr(f, a, t)	ioctl(f, a, t)
#  define	cfsetospeed(tp, val)	(tp)->c_cflag &= ~CBAUD; \
					(tp)->c_cflag |= (val)
#  define	cfgetospeed(tp)		((tp)->c_cflag & CBAUD)
#  ifdef CIBAUD
#   define	cfsetispeed(tp, val)	(tp)->c_cflag &= ~CIBAUD; \
					(tp)->c_cflag |= ((val)<<IBSHIFT)
#   define	cfgetispeed(tp)		(((tp)->c_cflag & CIBAUD)>>IBSHIFT)
#  else
#   define	cfsetispeed(tp, val)	(tp)->c_cflag &= ~CBAUD; \
					(tp)->c_cflag |= (val)
#   define	cfgetispeed(tp)		((tp)->c_cflag & CBAUD)
#  endif
# endif /* TCSANOW */
struct termios termbuf, termbuf2;	/* pty control structure */
#endif	/* USE_TERMIO */

#include <sys/types.h>
#include <libutil.h>

int cleanopen(char *);
void scrub_env(void);
static char **addarg(char **, const char *);

/*
 * init_termbuf()
 * copy_termbuf(cp)
 * set_termbuf()
 *
 * These three routines are used to get and set the "termbuf" structure
 * to and from the kernel.  init_termbuf() gets the current settings.
 * copy_termbuf() hands in a new "termbuf" to write to the kernel, and
 * set_termbuf() writes the structure into the kernel.
 */

void
init_termbuf(void)
{
#ifndef	USE_TERMIO
	(void) ioctl(pty, TIOCGETP, (char *)&termbuf.sg);
	(void) ioctl(pty, TIOCGETC, (char *)&termbuf.tc);
	(void) ioctl(pty, TIOCGLTC, (char *)&termbuf.ltc);
# ifdef	TIOCGSTATE
	(void) ioctl(pty, TIOCGSTATE, (char *)&termbuf.state);
# endif
#else
	(void) tcgetattr(pty, &termbuf);
#endif
	termbuf2 = termbuf;
}

#if	defined(LINEMODE) && defined(TIOCPKT_IOCTL)
void
copy_termbuf(char *cp, size_t len)
{
	if (len > sizeof(termbuf))
		len = sizeof(termbuf);
	memmove((char *)&termbuf, cp, len);
	termbuf2 = termbuf;
}
#endif	/* defined(LINEMODE) && defined(TIOCPKT_IOCTL) */

void
set_termbuf(void)
{
	/*
	 * Only make the necessary changes.
	 */
#ifndef	USE_TERMIO
	if (memcmp((char *)&termbuf.sg, (char *)&termbuf2.sg,
							sizeof(termbuf.sg)))
		(void) ioctl(pty, TIOCSETN, (char *)&termbuf.sg);
	if (memcmp((char *)&termbuf.tc, (char *)&termbuf2.tc,
							sizeof(termbuf.tc)))
		(void) ioctl(pty, TIOCSETC, (char *)&termbuf.tc);
	if (memcmp((char *)&termbuf.ltc, (char *)&termbuf2.ltc,
							sizeof(termbuf.ltc)))
		(void) ioctl(pty, TIOCSLTC, (char *)&termbuf.ltc);
	if (termbuf.lflags != termbuf2.lflags)
		(void) ioctl(pty, TIOCLSET, (char *)&termbuf.lflags);
#else	/* USE_TERMIO */
	if (memcmp((char *)&termbuf, (char *)&termbuf2, sizeof(termbuf)))
		(void) tcsetattr(pty, TCSANOW, &termbuf);
#endif	/* USE_TERMIO */
}


/*
 * spcset(func, valp, valpp)
 *
 * This function takes various special characters (func), and
 * sets *valp to the current value of that character, and
 * *valpp to point to where in the "termbuf" structure that
 * value is kept.
 *
 * It returns the SLC_ level of support for this function.
 */

#ifndef	USE_TERMIO
int
spcset(int func, cc_t *valp, cc_t **valpp)
{
	switch(func) {
	case SLC_EOF:
		*valp = termbuf.tc.t_eofc;
		*valpp = (cc_t *)&termbuf.tc.t_eofc;
		return(SLC_VARIABLE);
	case SLC_EC:
		*valp = termbuf.sg.sg_erase;
		*valpp = (cc_t *)&termbuf.sg.sg_erase;
		return(SLC_VARIABLE);
	case SLC_EL:
		*valp = termbuf.sg.sg_kill;
		*valpp = (cc_t *)&termbuf.sg.sg_kill;
		return(SLC_VARIABLE);
	case SLC_IP:
		*valp = termbuf.tc.t_intrc;
		*valpp = (cc_t *)&termbuf.tc.t_intrc;
		return(SLC_VARIABLE|SLC_FLUSHIN|SLC_FLUSHOUT);
	case SLC_ABORT:
		*valp = termbuf.tc.t_quitc;
		*valpp = (cc_t *)&termbuf.tc.t_quitc;
		return(SLC_VARIABLE|SLC_FLUSHIN|SLC_FLUSHOUT);
	case SLC_XON:
		*valp = termbuf.tc.t_startc;
		*valpp = (cc_t *)&termbuf.tc.t_startc;
		return(SLC_VARIABLE);
	case SLC_XOFF:
		*valp = termbuf.tc.t_stopc;
		*valpp = (cc_t *)&termbuf.tc.t_stopc;
		return(SLC_VARIABLE);
	case SLC_AO:
		*valp = termbuf.ltc.t_flushc;
		*valpp = (cc_t *)&termbuf.ltc.t_flushc;
		return(SLC_VARIABLE);
	case SLC_SUSP:
		*valp = termbuf.ltc.t_suspc;
		*valpp = (cc_t *)&termbuf.ltc.t_suspc;
		return(SLC_VARIABLE);
	case SLC_EW:
		*valp = termbuf.ltc.t_werasc;
		*valpp = (cc_t *)&termbuf.ltc.t_werasc;
		return(SLC_VARIABLE);
	case SLC_RP:
		*valp = termbuf.ltc.t_rprntc;
		*valpp = (cc_t *)&termbuf.ltc.t_rprntc;
		return(SLC_VARIABLE);
	case SLC_LNEXT:
		*valp = termbuf.ltc.t_lnextc;
		*valpp = (cc_t *)&termbuf.ltc.t_lnextc;
		return(SLC_VARIABLE);
	case SLC_FORW1:
		*valp = termbuf.tc.t_brkc;
		*valpp = (cc_t *)&termbuf.ltc.t_lnextc;
		return(SLC_VARIABLE);
	case SLC_BRK:
	case SLC_SYNCH:
	case SLC_AYT:
	case SLC_EOR:
		*valp = (cc_t)0;
		*valpp = (cc_t *)0;
		return(SLC_DEFAULT);
	default:
		*valp = (cc_t)0;
		*valpp = (cc_t *)0;
		return(SLC_NOSUPPORT);
	}
}

#else	/* USE_TERMIO */


#define	setval(a, b)	*valp = termbuf.c_cc[a]; \
			*valpp = &termbuf.c_cc[a]; \
			return(b);
#define	defval(a) *valp = ((cc_t)a); *valpp = (cc_t *)0; return(SLC_DEFAULT);

int
spcset(int func, cc_t *valp, cc_t **valpp)
{
	switch(func) {
	case SLC_EOF:
		setval(VEOF, SLC_VARIABLE);
	case SLC_EC:
		setval(VERASE, SLC_VARIABLE);
	case SLC_EL:
		setval(VKILL, SLC_VARIABLE);
	case SLC_IP:
		setval(VINTR, SLC_VARIABLE|SLC_FLUSHIN|SLC_FLUSHOUT);
	case SLC_ABORT:
		setval(VQUIT, SLC_VARIABLE|SLC_FLUSHIN|SLC_FLUSHOUT);
	case SLC_XON:
#ifdef	VSTART
		setval(VSTART, SLC_VARIABLE);
#else
		defval(0x13);
#endif
	case SLC_XOFF:
#ifdef	VSTOP
		setval(VSTOP, SLC_VARIABLE);
#else
		defval(0x11);
#endif
	case SLC_EW:
#ifdef	VWERASE
		setval(VWERASE, SLC_VARIABLE);
#else
		defval(0);
#endif
	case SLC_RP:
#ifdef	VREPRINT
		setval(VREPRINT, SLC_VARIABLE);
#else
		defval(0);
#endif
	case SLC_LNEXT:
#ifdef	VLNEXT
		setval(VLNEXT, SLC_VARIABLE);
#else
		defval(0);
#endif
	case SLC_AO:
#if	!defined(VDISCARD) && defined(VFLUSHO)
# define VDISCARD VFLUSHO
#endif
#ifdef	VDISCARD
		setval(VDISCARD, SLC_VARIABLE|SLC_FLUSHOUT);
#else
		defval(0);
#endif
	case SLC_SUSP:
#ifdef	VSUSP
		setval(VSUSP, SLC_VARIABLE|SLC_FLUSHIN);
#else
		defval(0);
#endif
#ifdef	VEOL
	case SLC_FORW1:
		setval(VEOL, SLC_VARIABLE);
#endif
#ifdef	VEOL2
	case SLC_FORW2:
		setval(VEOL2, SLC_VARIABLE);
#endif
	case SLC_AYT:
#ifdef	VSTATUS
		setval(VSTATUS, SLC_VARIABLE);
#else
		defval(0);
#endif

	case SLC_BRK:
	case SLC_SYNCH:
	case SLC_EOR:
		defval(0);

	default:
		*valp = 0;
		*valpp = 0;
		return(SLC_NOSUPPORT);
	}
}
#endif	/* USE_TERMIO */

/*
 * getpty()
 *
 * Allocate a pty.  As a side effect, the external character
 * array "line" contains the name of the slave side.
 *
 * Returns the file descriptor of the opened pty.
 */
char line[32];

int
getpty(int *ptynum __unused)
{
	int p;
	const char *pn;

	p = posix_openpt(O_RDWR|O_NOCTTY);
	if (p < 0)
		return (-1);
	
	if (grantpt(p) == -1)
		return (-1);

	if (unlockpt(p) == -1)
		return (-1);
	
	pn = ptsname(p);
	if (pn == NULL)
		return (-1);
	
	if (strlcpy(line, pn, sizeof line) >= sizeof line)
		return (-1);

	return (p);
}

#ifdef	LINEMODE
/*
 * tty_flowmode()	Find out if flow control is enabled or disabled.
 * tty_linemode()	Find out if linemode (external processing) is enabled.
 * tty_setlinemod(on)	Turn on/off linemode.
 * tty_isecho()		Find out if echoing is turned on.
 * tty_setecho(on)	Enable/disable character echoing.
 * tty_israw()		Find out if terminal is in RAW mode.
 * tty_binaryin(on)	Turn on/off BINARY on input.
 * tty_binaryout(on)	Turn on/off BINARY on output.
 * tty_isediting()	Find out if line editing is enabled.
 * tty_istrapsig()	Find out if signal trapping is enabled.
 * tty_setedit(on)	Turn on/off line editing.
 * tty_setsig(on)	Turn on/off signal trapping.
 * tty_issofttab()	Find out if tab expansion is enabled.
 * tty_setsofttab(on)	Turn on/off soft tab expansion.
 * tty_islitecho()	Find out if typed control chars are echoed literally
 * tty_setlitecho()	Turn on/off literal echo of control chars
 * tty_tspeed(val)	Set transmit speed to val.
 * tty_rspeed(val)	Set receive speed to val.
 */


int
tty_linemode(void)
{
#ifndef	USE_TERMIO
	return(termbuf.state & TS_EXTPROC);
#else
	return(termbuf.c_lflag & EXTPROC);
#endif
}

void
tty_setlinemode(int on)
{
#ifdef	TIOCEXT
	set_termbuf();
	(void) ioctl(pty, TIOCEXT, (char *)&on);
	init_termbuf();
#else	/* !TIOCEXT */
# ifdef	EXTPROC
	if (on)
		termbuf.c_lflag |= EXTPROC;
	else
		termbuf.c_lflag &= ~EXTPROC;
# endif
#endif	/* TIOCEXT */
}
#endif	/* LINEMODE */

int
tty_isecho(void)
{
#ifndef USE_TERMIO
	return (termbuf.sg.sg_flags & ECHO);
#else
	return (termbuf.c_lflag & ECHO);
#endif
}

int
tty_flowmode(void)
{
#ifndef USE_TERMIO
	return(((termbuf.tc.t_startc) > 0 && (termbuf.tc.t_stopc) > 0) ? 1 : 0);
#else
	return((termbuf.c_iflag & IXON) ? 1 : 0);
#endif
}

int
tty_restartany(void)
{
#ifndef USE_TERMIO
# ifdef	DECCTQ
	return((termbuf.lflags & DECCTQ) ? 0 : 1);
# else
	return(-1);
# endif
#else
	return((termbuf.c_iflag & IXANY) ? 1 : 0);
#endif
}

void
tty_setecho(int on)
{
#ifndef	USE_TERMIO
	if (on)
		termbuf.sg.sg_flags |= ECHO|CRMOD;
	else
		termbuf.sg.sg_flags &= ~(ECHO|CRMOD);
#else
	if (on)
		termbuf.c_lflag |= ECHO;
	else
		termbuf.c_lflag &= ~ECHO;
#endif
}

int
tty_israw(void)
{
#ifndef USE_TERMIO
	return(termbuf.sg.sg_flags & RAW);
#else
	return(!(termbuf.c_lflag & ICANON));
#endif
}

#ifdef	AUTHENTICATION
#if	defined(NO_LOGIN_F) && defined(LOGIN_R)
int
tty_setraw(int on)
{
#  ifndef USE_TERMIO
	if (on)
		termbuf.sg.sg_flags |= RAW;
	else
		termbuf.sg.sg_flags &= ~RAW;
#  else
	if (on)
		termbuf.c_lflag &= ~ICANON;
	else
		termbuf.c_lflag |= ICANON;
#  endif
}
#endif
#endif /* AUTHENTICATION */

void
tty_binaryin(int on)
{
#ifndef	USE_TERMIO
	if (on)
		termbuf.lflags |= LPASS8;
	else
		termbuf.lflags &= ~LPASS8;
#else
	if (on) {
		termbuf.c_iflag &= ~ISTRIP;
	} else {
		termbuf.c_iflag |= ISTRIP;
	}
#endif
}

void
tty_binaryout(int on)
{
#ifndef	USE_TERMIO
	if (on)
		termbuf.lflags |= LLITOUT;
	else
		termbuf.lflags &= ~LLITOUT;
#else
	if (on) {
		termbuf.c_cflag &= ~(CSIZE|PARENB);
		termbuf.c_cflag |= CS8;
		termbuf.c_oflag &= ~OPOST;
	} else {
		termbuf.c_cflag &= ~CSIZE;
		termbuf.c_cflag |= CS7|PARENB;
		termbuf.c_oflag |= OPOST;
	}
#endif
}

int
tty_isbinaryin(void)
{
#ifndef	USE_TERMIO
	return(termbuf.lflags & LPASS8);
#else
	return(!(termbuf.c_iflag & ISTRIP));
#endif
}

int
tty_isbinaryout(void)
{
#ifndef	USE_TERMIO
	return(termbuf.lflags & LLITOUT);
#else
	return(!(termbuf.c_oflag&OPOST));
#endif
}

#ifdef	LINEMODE
int
tty_isediting(void)
{
#ifndef USE_TERMIO
	return(!(termbuf.sg.sg_flags & (CBREAK|RAW)));
#else
	return(termbuf.c_lflag & ICANON);
#endif
}

int
tty_istrapsig(void)
{
#ifndef USE_TERMIO
	return(!(termbuf.sg.sg_flags&RAW));
#else
	return(termbuf.c_lflag & ISIG);
#endif
}

void
tty_setedit(int on)
{
#ifndef USE_TERMIO
	if (on)
		termbuf.sg.sg_flags &= ~CBREAK;
	else
		termbuf.sg.sg_flags |= CBREAK;
#else
	if (on)
		termbuf.c_lflag |= ICANON;
	else
		termbuf.c_lflag &= ~ICANON;
#endif
}

void
tty_setsig(int on)
{
#ifndef	USE_TERMIO
	if (on)
		;
#else
	if (on)
		termbuf.c_lflag |= ISIG;
	else
		termbuf.c_lflag &= ~ISIG;
#endif
}
#endif	/* LINEMODE */

int
tty_issofttab(void)
{
#ifndef	USE_TERMIO
	return (termbuf.sg.sg_flags & XTABS);
#else
# ifdef	OXTABS
	return (termbuf.c_oflag & OXTABS);
# endif
# ifdef	TABDLY
	return ((termbuf.c_oflag & TABDLY) == TAB3);
# endif
#endif
}

void
tty_setsofttab(int on)
{
#ifndef	USE_TERMIO
	if (on)
		termbuf.sg.sg_flags |= XTABS;
	else
		termbuf.sg.sg_flags &= ~XTABS;
#else
	if (on) {
# ifdef	OXTABS
		termbuf.c_oflag |= OXTABS;
# endif
# ifdef	TABDLY
		termbuf.c_oflag &= ~TABDLY;
		termbuf.c_oflag |= TAB3;
# endif
	} else {
# ifdef	OXTABS
		termbuf.c_oflag &= ~OXTABS;
# endif
# ifdef	TABDLY
		termbuf.c_oflag &= ~TABDLY;
		termbuf.c_oflag |= TAB0;
# endif
	}
#endif
}

int
tty_islitecho(void)
{
#ifndef	USE_TERMIO
	return (!(termbuf.lflags & LCTLECH));
#else
# ifdef	ECHOCTL
	return (!(termbuf.c_lflag & ECHOCTL));
# endif
# ifdef	TCTLECH
	return (!(termbuf.c_lflag & TCTLECH));
# endif
# if	!defined(ECHOCTL) && !defined(TCTLECH)
	return (0);	/* assumes ctl chars are echoed '^x' */
# endif
#endif
}

void
tty_setlitecho(int on)
{
#ifndef	USE_TERMIO
	if (on)
		termbuf.lflags &= ~LCTLECH;
	else
		termbuf.lflags |= LCTLECH;
#else
# ifdef	ECHOCTL
	if (on)
		termbuf.c_lflag &= ~ECHOCTL;
	else
		termbuf.c_lflag |= ECHOCTL;
# endif
# ifdef	TCTLECH
	if (on)
		termbuf.c_lflag &= ~TCTLECH;
	else
		termbuf.c_lflag |= TCTLECH;
# endif
#endif
}

int
tty_iscrnl(void)
{
#ifndef	USE_TERMIO
	return (termbuf.sg.sg_flags & CRMOD);
#else
	return (termbuf.c_iflag & ICRNL);
#endif
}

void
tty_tspeed(int val)
{
#ifdef	DECODE_BAUD
	struct termspeeds *tp;

	for (tp = termspeeds; (tp->speed != -1) && (val > tp->speed); tp++)
		;
	if (tp->speed == -1)	/* back up to last valid value */
		--tp;
	cfsetospeed(&termbuf, tp->value);
#else	/* DECODE_BAUD */
	cfsetospeed(&termbuf, val);
#endif	/* DECODE_BAUD */
}

void
tty_rspeed(int val)
{
#ifdef	DECODE_BAUD
	struct termspeeds *tp;

	for (tp = termspeeds; (tp->speed != -1) && (val > tp->speed); tp++)
		;
	if (tp->speed == -1)	/* back up to last valid value */
		--tp;
	cfsetispeed(&termbuf, tp->value);
#else	/* DECODE_BAUD */
	cfsetispeed(&termbuf, val);
#endif	/* DECODE_BAUD */
}

/*
 * getptyslave()
 *
 * Open the slave side of the pty, and do any initialization
 * that is necessary.
 */
static void
getptyslave(void)
{
	int t = -1;
	char erase;

# ifdef	LINEMODE
	int waslm;
# endif
# ifdef	TIOCGWINSZ
	struct winsize ws;
	extern int def_row, def_col;
# endif
	extern int def_tspeed, def_rspeed;
	/*
	 * Opening the slave side may cause initilization of the
	 * kernel tty structure.  We need remember the state of
	 * 	if linemode was turned on
	 *	terminal window size
	 *	terminal speed
	 *	erase character
	 * so that we can re-set them if we need to.
	 */
# ifdef	LINEMODE
	waslm = tty_linemode();
# endif
	erase = termbuf.c_cc[VERASE];

	/*
	 * Make sure that we don't have a controlling tty, and
	 * that we are the session (process group) leader.
	 */
# ifdef	TIOCNOTTY
	t = open(_PATH_TTY, O_RDWR);
	if (t >= 0) {
		(void) ioctl(t, TIOCNOTTY, (char *)0);
		(void) close(t);
	}
# endif

	t = cleanopen(line);
	if (t < 0)
		fatalperror(net, line);


	/*
	 * set up the tty modes as we like them to be.
	 */
	init_termbuf();
# ifdef	TIOCGWINSZ
	if (def_row || def_col) {
		memset((char *)&ws, 0, sizeof(ws));
		ws.ws_col = def_col;
		ws.ws_row = def_row;
		(void)ioctl(t, TIOCSWINSZ, (char *)&ws);
	}
# endif

	/*
	 * Settings for sgtty based systems
	 */
# ifndef	USE_TERMIO
	termbuf.sg.sg_flags |= CRMOD|ANYP|ECHO|XTABS;
# endif	/* USE_TERMIO */

	/*
	 * Settings for all other termios/termio based
	 * systems, other than 4.4BSD.  In 4.4BSD the
	 * kernel does the initial terminal setup.
	 */
	tty_rspeed((def_rspeed > 0) ? def_rspeed : 9600);
	tty_tspeed((def_tspeed > 0) ? def_tspeed : 9600);
	if (erase)
		termbuf.c_cc[VERASE] = erase;
# ifdef	LINEMODE
	if (waslm)
		tty_setlinemode(1);
# endif	/* LINEMODE */

	/*
	 * Set the tty modes, and make this our controlling tty.
	 */
	set_termbuf();
	if (login_tty(t) == -1)
		fatalperror(net, "login_tty");
	if (net > 2)
		(void) close(net);
#ifdef	AUTHENTICATION
#if	defined(NO_LOGIN_F) && defined(LOGIN_R)
	/*
	 * Leave the pty open so that we can write out the rlogin
	 * protocol for /bin/login, if the authentication works.
	 */
#else
	if (pty > 2) {
		(void) close(pty);
		pty = -1;
	}
#endif
#endif /* AUTHENTICATION */
}

#ifndef	O_NOCTTY
#define	O_NOCTTY	0
#endif
/*
 * Open the specified slave side of the pty,
 * making sure that we have a clean tty.
 */
int
cleanopen(char *li)
{
	int t;

	/*
	 * Make sure that other people can't open the
	 * slave side of the connection.
	 */
	(void) chown(li, 0, 0);
	(void) chmod(li, 0600);

	(void) revoke(li);

	t = open(line, O_RDWR|O_NOCTTY);

	if (t < 0)
		return(-1);

	return(t);
}

/*
 * startslave(host)
 *
 * Given a hostname, do whatever
 * is necessary to startup the login process on the slave side of the pty.
 */

/* ARGSUSED */
void
startslave(char *host, int autologin, char *autoname)
{
	int i;

#ifdef	AUTHENTICATION
	if (!autoname || !autoname[0])
		autologin = 0;

	if (autologin < auth_level) {
		fatal(net, "Authorization failed");
		exit(1);
	}
#endif


	if ((i = fork()) < 0)
		fatalperror(net, "fork");
	if (i) {
	} else {
		getptyslave();
		start_login(host, autologin, autoname);
		/*NOTREACHED*/
	}
}

void
init_env(void)
{
	char **envp;

	envp = envinit;
	if ((*envp = getenv("TZ")))
		*envp++ -= 3;
	*envp = 0;
	environ = envinit;
}


/*
 * start_login(host)
 *
 * Assuming that we are now running as a child processes, this
 * function will turn us into the login process.
 */

#ifndef AUTHENTICATION
#define undef1 __unused
#else
#define undef1
#endif

void
start_login(char *host undef1, int autologin undef1, char *name undef1)
{
	char **argv;
	char *user;

	user = getenv("USER");
	user = (user != NULL) ? strdup(user) : NULL;

	scrub_env();

	/*
	 * -h : pass on name of host.
	 *		WARNING:  -h is accepted by login if and only if
	 *			getuid() == 0.
	 * -p : don't clobber the environment (so terminal type stays set).
	 *
	 * -f : force this login, he has already been authenticated
	 */
	argv = addarg(0, "login");

#if	!defined(NO_LOGIN_H)
#ifdef	AUTHENTICATION
# if	defined(NO_LOGIN_F) && defined(LOGIN_R)
	/*
	 * Don't add the "-h host" option if we are going
	 * to be adding the "-r host" option down below...
	 */
	if ((auth_level < 0) || (autologin != AUTH_VALID))
# endif
#endif /* AUTHENTICATION */
	{
		argv = addarg(argv, "-h");
		argv = addarg(argv, host);
	}
#endif
#if	!defined(NO_LOGIN_P)
	argv = addarg(argv, "-p");
#endif
#ifdef	LINEMODE
	/*
	 * Set the environment variable "LINEMODE" to either
	 * "real" or "kludge" if we are operating in either
	 * real or kludge linemode.
	 */
	if (lmodetype == REAL_LINEMODE)
		setenv("LINEMODE", "real", 1);
# ifdef KLUDGELINEMODE
	else if (lmodetype == KLUDGE_LINEMODE || lmodetype == KLUDGE_OK)
		setenv("LINEMODE", "kludge", 1);
# endif
#endif
#ifdef	BFTPDAEMON
	/*
	 * Are we working as the bftp daemon?  If so, then ask login
	 * to start bftp instead of shell.
	 */
	if (bftpd) {
		argv = addarg(argv, "-e");
		argv = addarg(argv, BFTPPATH);
	} else
#endif
#ifdef	AUTHENTICATION
	if (auth_level >= 0 && autologin == AUTH_VALID) {
# if	!defined(NO_LOGIN_F)
		argv = addarg(argv, "-f");
		argv = addarg(argv, "--");
		argv = addarg(argv, name);
# else
#  if defined(LOGIN_R)
		/*
		 * We don't have support for "login -f", but we
		 * can fool /bin/login into thinking that we are
		 * rlogind, and allow us to log in without a
		 * password.  The rlogin protocol expects
		 *	local-user\0remote-user\0term/speed\0
		 */

		if (pty > 2) {
			char *cp;
			char speed[128];
			int isecho, israw, xpty, len;
			extern int def_rspeed;
#  ifndef LOGIN_HOST
			/*
			 * Tell login that we are coming from "localhost".
			 * If we passed in the real host name, then the
			 * user would have to allow .rhost access from
			 * every machine that they want authenticated
			 * access to work from, which sort of defeats
			 * the purpose of an authenticated login...
			 * So, we tell login that the session is coming
			 * from "localhost", and the user will only have
			 * to have "localhost" in their .rhost file.
			 */
#			define LOGIN_HOST "localhost"
#  endif
			argv = addarg(argv, "-r");
			argv = addarg(argv, LOGIN_HOST);

			xpty = pty;
			pty = 0;
			init_termbuf();
			isecho = tty_isecho();
			israw = tty_israw();
			if (isecho || !israw) {
				tty_setecho(0);		/* Turn off echo */
				tty_setraw(1);		/* Turn on raw */
				set_termbuf();
			}
			len = strlen(name)+1;
			write(xpty, name, len);
			write(xpty, name, len);
			snprintf(speed, sizeof(speed),
				"%s/%d", (cp = getenv("TERM")) ? cp : "",
				(def_rspeed > 0) ? def_rspeed : 9600);
			len = strlen(speed)+1;
			write(xpty, speed, len);

			if (isecho || !israw) {
				init_termbuf();
				tty_setecho(isecho);
				tty_setraw(israw);
				set_termbuf();
				if (!israw) {
					/*
					 * Write a newline to ensure
					 * that login will be able to
					 * read the line...
					 */
					write(xpty, "\n", 1);
				}
			}
			pty = xpty;
		}
#  else
		argv = addarg(argv, "--");
		argv = addarg(argv, name);
#  endif
# endif
	} else
#endif
	if (user != NULL) {
 		argv = addarg(argv, "--");
		argv = addarg(argv, user);
#if	defined(LOGIN_ARGS) && defined(NO_LOGIN_P)
		{
			char **cpp;
			for (cpp = environ; *cpp; cpp++)
				argv = addarg(argv, *cpp);
		}
#endif
	}
#ifdef	AUTHENTICATION
#if	defined(NO_LOGIN_F) && defined(LOGIN_R)
	if (pty > 2)
		close(pty);
#endif
#endif /* AUTHENTICATION */
	closelog();

	if (user != NULL)
		free(user);

	if (altlogin == NULL) {
		altlogin = _PATH_LOGIN;
	}
	execv(altlogin, argv);

	syslog(LOG_ERR, "%s: %m", altlogin);
	fatalperror(net, altlogin);
	/*NOTREACHED*/
}

static char **
addarg(char **argv, const char *val)
{
	char **cpp;

	if (argv == NULL) {
		/*
		 * 10 entries, a leading length, and a null
		 */
		argv = (char **)malloc(sizeof(*argv) * 12);
		if (argv == NULL)
			fatal(net, "failure allocating argument space");
		*argv++ = (char *)10;
		*argv = (char *)0;
	}
	for (cpp = argv; *cpp; cpp++)
		;
	if (cpp == &argv[(long)argv[-1]]) {
		--argv;
		*argv = (char *)((long)(*argv) + 10);
		argv = (char **)realloc(argv, sizeof(*argv)*((long)(*argv) + 2));
		if (argv == NULL)
			fatal(net, "failure allocating argument space");
		argv++;
		cpp = &argv[(long)argv[-1] - 10];
	}
	if ((*cpp++ = strdup(val)) == NULL)
		fatal(net, "failure allocating argument space");
	*cpp = 0;
	return(argv);
}

/*
 * scrub_env()
 *
 * We only accept the environment variables listed below.
 */
void
scrub_env(void)
{
	static const char *rej[] = {
		"TERMCAP=/",
		NULL
	};

	static const char *acc[] = {
		"XAUTH=", "XAUTHORITY=", "DISPLAY=",
		"TERM=",
		"EDITOR=",
		"PAGER=",
		"LOGNAME=",
		"POSIXLY_CORRECT=",
		"PRINTER=",
		NULL
	};

	char **cpp, **cpp2;
	const char **p;
	char ** new_environ;
	size_t count;

	/* Allocate space for scrubbed environment. */
	for (count = 1, cpp = environ; *cpp; count++, cpp++)
		continue;
	if ((new_environ = malloc(count * sizeof(char *))) == NULL) {
		environ = NULL;
		return;
	}

 	for (cpp2 = new_environ, cpp = environ; *cpp; cpp++) {
		int reject_it = 0;

		for(p = rej; *p; p++)
			if(strncmp(*cpp, *p, strlen(*p)) == 0) {
				reject_it = 1;
				break;
			}
		if (reject_it)
			continue;

		for(p = acc; *p; p++)
			if(strncmp(*cpp, *p, strlen(*p)) == 0)
				break;
		if(*p != NULL) {
			if ((*cpp2++ = strdup(*cpp)) == NULL) {
				environ = new_environ;
				return;
			}
		}
 	}
	*cpp2 = NULL;
	environ = new_environ;
}

/*
 * cleanup()
 *
 * This is the routine to call when we are all through, to
 * clean up anything that needs to be cleaned up.
 */
/* ARGSUSED */
void
cleanup(int sig __unused)
{

	(void) shutdown(net, SHUT_RDWR);
	_exit(1);
}
