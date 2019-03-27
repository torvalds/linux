/*
 * Copyright (c) 1988, 1990, 1993
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
static const char sccsid[] = "@(#)sys_bsd.c	8.4 (Berkeley) 5/30/95";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * The following routines try to encapsulate what is system dependent
 * (at least between 4.x and dos) which is used in telnet.c.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/telnet.h>

#include "ring.h"
#include "fdset.h"
#include "defines.h"
#include "externs.h"
#include "types.h"
#include "baud.h"

int
	tout,			/* Output file descriptor */
	tin,			/* Input file descriptor */
	net;

#ifndef	USE_TERMIO
struct	tchars otc = { 0 }, ntc = { 0 };
struct	ltchars oltc = { 0 }, nltc = { 0 };
struct	sgttyb ottyb = { 0 }, nttyb = { 0 };
int	olmode = 0;
# define cfgetispeed(ptr)	(ptr)->sg_ispeed
# define cfgetospeed(ptr)	(ptr)->sg_ospeed
# define old_tc ottyb

#else	/* USE_TERMIO */
struct	termio old_tc = { 0, 0, 0, 0, {}, 0, 0 };

# ifndef	TCSANOW
#  ifdef TCSETS
#   define	TCSANOW		TCSETS
#   define	TCSADRAIN	TCSETSW
#   define	tcgetattr(f, t) ioctl(f, TCGETS, (char *)t)
#  else
#   ifdef TCSETA
#    define	TCSANOW		TCSETA
#    define	TCSADRAIN	TCSETAW
#    define	tcgetattr(f, t) ioctl(f, TCGETA, (char *)t)
#   else
#    define	TCSANOW		TIOCSETA
#    define	TCSADRAIN	TIOCSETAW
#    define	tcgetattr(f, t) ioctl(f, TIOCGETA, (char *)t)
#   endif
#  endif
#  define	tcsetattr(f, a, t) ioctl(f, a, (char *)t)
#  define	cfgetospeed(ptr)	((ptr)->c_cflag&CBAUD)
#  ifdef CIBAUD
#   define	cfgetispeed(ptr)	(((ptr)->c_cflag&CIBAUD) >> IBSHIFT)
#  else
#   define	cfgetispeed(ptr)	cfgetospeed(ptr)
#  endif
# endif /* TCSANOW */
# ifdef	sysV88
# define TIOCFLUSH TC_PX_DRAIN
# endif
#endif	/* USE_TERMIO */

static fd_set *ibitsp, *obitsp, *xbitsp;
int fdsn;

#ifdef	SIGINT
static SIG_FUNC_RET intr(int);
#endif	/* SIGINT */
#ifdef	SIGQUIT
static SIG_FUNC_RET intr2(int);
#endif	/* SIGQUIT */
#ifdef	SIGTSTP
static SIG_FUNC_RET susp(int);
#endif	/* SIGTSTP */
#ifdef	SIGINFO
static SIG_FUNC_RET ayt(int);
#endif

void
init_sys(void)
{
    tout = fileno(stdout);
    tin = fileno(stdin);
    errno = 0;
}

int
TerminalWrite(char *buf, int n)
{
    return write(tout, buf, n);
}

int
TerminalRead(char *buf, int n)
{
    return read(tin, buf, n);
}

/*
 *
 */

int
TerminalAutoFlush(void)
{
#if	defined(LNOFLSH)
    int flush;

    ioctl(0, TIOCLGET, (char *)&flush);
    return !(flush&LNOFLSH);	/* if LNOFLSH, no autoflush */
#else	/* LNOFLSH */
    return 1;
#endif	/* LNOFLSH */
}

#ifdef	KLUDGELINEMODE
extern int kludgelinemode;
#endif
/*
 * TerminalSpecialChars()
 *
 * Look at an input character to see if it is a special character
 * and decide what to do.
 *
 * Output:
 *
 *	0	Don't add this character.
 *	1	Do add this character
 */

int
TerminalSpecialChars(int c)
{
    if (c == termIntChar) {
	intp();
	return 0;
    } else if (c == termQuitChar) {
#ifdef	KLUDGELINEMODE
	if (kludgelinemode)
	    sendbrk();
	else
#endif
	    sendabort();
	return 0;
    } else if (c == termEofChar) {
	if (my_want_state_is_will(TELOPT_LINEMODE)) {
	    sendeof();
	    return 0;
	}
	return 1;
    } else if (c == termSuspChar) {
	sendsusp();
	return(0);
    } else if (c == termFlushChar) {
	xmitAO();		/* Transmit Abort Output */
	return 0;
    } else if (!MODE_LOCAL_CHARS(globalmode)) {
	if (c == termKillChar) {
	    xmitEL();
	    return 0;
	} else if (c == termEraseChar) {
	    xmitEC();		/* Transmit Erase Character */
	    return 0;
	}
    }
    return 1;
}


/*
 * Flush output to the terminal
 */

void
TerminalFlushOutput(void)
{
#ifdef	TIOCFLUSH
    (void) ioctl(fileno(stdout), TIOCFLUSH, (char *) 0);
#else
    (void) ioctl(fileno(stdout), TCFLSH, (char *) 0);
#endif
}

void
TerminalSaveState(void)
{
#ifndef	USE_TERMIO
    ioctl(0, TIOCGETP, (char *)&ottyb);
    ioctl(0, TIOCGETC, (char *)&otc);
    ioctl(0, TIOCGLTC, (char *)&oltc);
    ioctl(0, TIOCLGET, (char *)&olmode);

    ntc = otc;
    nltc = oltc;
    nttyb = ottyb;

#else	/* USE_TERMIO */
    tcgetattr(0, &old_tc);

    new_tc = old_tc;

#ifndef	VDISCARD
    termFlushChar = CONTROL('O');
#endif
#ifndef	VWERASE
    termWerasChar = CONTROL('W');
#endif
#ifndef	VREPRINT
    termRprntChar = CONTROL('R');
#endif
#ifndef	VLNEXT
    termLiteralNextChar = CONTROL('V');
#endif
#ifndef	VSTART
    termStartChar = CONTROL('Q');
#endif
#ifndef	VSTOP
    termStopChar = CONTROL('S');
#endif
#ifndef	VSTATUS
    termAytChar = CONTROL('T');
#endif
#endif	/* USE_TERMIO */
}

cc_t *
tcval(int func)
{
    switch(func) {
    case SLC_IP:	return(&termIntChar);
    case SLC_ABORT:	return(&termQuitChar);
    case SLC_EOF:	return(&termEofChar);
    case SLC_EC:	return(&termEraseChar);
    case SLC_EL:	return(&termKillChar);
    case SLC_XON:	return(&termStartChar);
    case SLC_XOFF:	return(&termStopChar);
    case SLC_FORW1:	return(&termForw1Char);
#ifdef	USE_TERMIO
    case SLC_FORW2:	return(&termForw2Char);
# ifdef	VDISCARD
    case SLC_AO:	return(&termFlushChar);
# endif
# ifdef	VSUSP
    case SLC_SUSP:	return(&termSuspChar);
# endif
# ifdef	VWERASE
    case SLC_EW:	return(&termWerasChar);
# endif
# ifdef	VREPRINT
    case SLC_RP:	return(&termRprntChar);
# endif
# ifdef	VLNEXT
    case SLC_LNEXT:	return(&termLiteralNextChar);
# endif
# ifdef	VSTATUS
    case SLC_AYT:	return(&termAytChar);
# endif
#endif

    case SLC_SYNCH:
    case SLC_BRK:
    case SLC_EOR:
    default:
	return((cc_t *)0);
    }
}

void
TerminalDefaultChars(void)
{
#ifndef	USE_TERMIO
    ntc = otc;
    nltc = oltc;
    nttyb.sg_kill = ottyb.sg_kill;
    nttyb.sg_erase = ottyb.sg_erase;
#else	/* USE_TERMIO */
    memcpy(new_tc.c_cc, old_tc.c_cc, sizeof(old_tc.c_cc));
# ifndef	VDISCARD
    termFlushChar = CONTROL('O');
# endif
# ifndef	VWERASE
    termWerasChar = CONTROL('W');
# endif
# ifndef	VREPRINT
    termRprntChar = CONTROL('R');
# endif
# ifndef	VLNEXT
    termLiteralNextChar = CONTROL('V');
# endif
# ifndef	VSTART
    termStartChar = CONTROL('Q');
# endif
# ifndef	VSTOP
    termStopChar = CONTROL('S');
# endif
# ifndef	VSTATUS
    termAytChar = CONTROL('T');
# endif
#endif	/* USE_TERMIO */
}

/*
 * TerminalNewMode - set up terminal to a specific mode.
 *	MODE_ECHO: do local terminal echo
 *	MODE_FLOW: do local flow control
 *	MODE_TRAPSIG: do local mapping to TELNET IAC sequences
 *	MODE_EDIT: do local line editing
 *
 *	Command mode:
 *		MODE_ECHO|MODE_EDIT|MODE_FLOW|MODE_TRAPSIG
 *		local echo
 *		local editing
 *		local xon/xoff
 *		local signal mapping
 *
 *	Linemode:
 *		local/no editing
 *	Both Linemode and Single Character mode:
 *		local/remote echo
 *		local/no xon/xoff
 *		local/no signal mapping
 */

void
TerminalNewMode(int f)
{
    static int prevmode = 0;
#ifndef	USE_TERMIO
    struct tchars tc;
    struct ltchars ltc;
    struct sgttyb sb;
    int lmode;
#else	/* USE_TERMIO */
    struct termio tmp_tc;
#endif	/* USE_TERMIO */
    int onoff;
    int old;
    cc_t esc;

    globalmode = f&~MODE_FORCE;
    if (prevmode == f)
	return;

    /*
     * Write any outstanding data before switching modes
     * ttyflush() returns 0 only when there is no more data
     * left to write out, it returns -1 if it couldn't do
     * anything at all, otherwise it returns 1 + the number
     * of characters left to write.
#ifndef	USE_TERMIO
     * We would really like ask the kernel to wait for the output
     * to drain, like we can do with the TCSADRAIN, but we don't have
     * that option.  The only ioctl that waits for the output to
     * drain, TIOCSETP, also flushes the input queue, which is NOT
     * what we want (TIOCSETP is like TCSADFLUSH).
#endif
     */
    old = ttyflush(SYNCHing|flushout);
    if (old < 0 || old > 1) {
#ifdef	USE_TERMIO
	tcgetattr(tin, &tmp_tc);
#endif	/* USE_TERMIO */
	do {
	    /*
	     * Wait for data to drain, then flush again.
	     */
#ifdef	USE_TERMIO
	    tcsetattr(tin, TCSADRAIN, &tmp_tc);
#endif	/* USE_TERMIO */
	    old = ttyflush(SYNCHing|flushout);
	} while (old < 0 || old > 1);
    }

    old = prevmode;
    prevmode = f&~MODE_FORCE;
#ifndef	USE_TERMIO
    sb = nttyb;
    tc = ntc;
    ltc = nltc;
    lmode = olmode;
#else
    tmp_tc = new_tc;
#endif

    if (f&MODE_ECHO) {
#ifndef	USE_TERMIO
	sb.sg_flags |= ECHO;
#else
	tmp_tc.c_lflag |= ECHO;
	tmp_tc.c_oflag |= ONLCR;
	if (crlf)
		tmp_tc.c_iflag |= ICRNL;
#endif
    } else {
#ifndef	USE_TERMIO
	sb.sg_flags &= ~ECHO;
#else
	tmp_tc.c_lflag &= ~ECHO;
	tmp_tc.c_oflag &= ~ONLCR;
#endif
    }

    if ((f&MODE_FLOW) == 0) {
#ifndef	USE_TERMIO
	tc.t_startc = _POSIX_VDISABLE;
	tc.t_stopc = _POSIX_VDISABLE;
#else
	tmp_tc.c_iflag &= ~(IXOFF|IXON);	/* Leave the IXANY bit alone */
    } else {
	if (restartany < 0) {
		tmp_tc.c_iflag |= IXOFF|IXON;	/* Leave the IXANY bit alone */
	} else if (restartany > 0) {
		tmp_tc.c_iflag |= IXOFF|IXON|IXANY;
	} else {
		tmp_tc.c_iflag |= IXOFF|IXON;
		tmp_tc.c_iflag &= ~IXANY;
	}
#endif
    }

    if ((f&MODE_TRAPSIG) == 0) {
#ifndef	USE_TERMIO
	tc.t_intrc = _POSIX_VDISABLE;
	tc.t_quitc = _POSIX_VDISABLE;
	tc.t_eofc = _POSIX_VDISABLE;
	ltc.t_suspc = _POSIX_VDISABLE;
	ltc.t_dsuspc = _POSIX_VDISABLE;
#else
	tmp_tc.c_lflag &= ~ISIG;
#endif
	localchars = 0;
    } else {
#ifdef	USE_TERMIO
	tmp_tc.c_lflag |= ISIG;
#endif
	localchars = 1;
    }

    if (f&MODE_EDIT) {
#ifndef	USE_TERMIO
	sb.sg_flags &= ~CBREAK;
	sb.sg_flags |= CRMOD;
#else
	tmp_tc.c_lflag |= ICANON;
#endif
    } else {
#ifndef	USE_TERMIO
	sb.sg_flags |= CBREAK;
	if (f&MODE_ECHO)
	    sb.sg_flags |= CRMOD;
	else
	    sb.sg_flags &= ~CRMOD;
#else
	tmp_tc.c_lflag &= ~ICANON;
	tmp_tc.c_iflag &= ~ICRNL;
	tmp_tc.c_cc[VMIN] = 1;
	tmp_tc.c_cc[VTIME] = 0;
#endif
    }

    if ((f&(MODE_EDIT|MODE_TRAPSIG)) == 0) {
#ifndef	USE_TERMIO
	ltc.t_lnextc = _POSIX_VDISABLE;
#else
# ifdef VLNEXT
	tmp_tc.c_cc[VLNEXT] = (cc_t)(_POSIX_VDISABLE);
# endif
#endif
    }

    if (f&MODE_SOFT_TAB) {
#ifndef USE_TERMIO
	sb.sg_flags |= XTABS;
#else
# ifdef	OXTABS
	tmp_tc.c_oflag |= OXTABS;
# endif
# ifdef	TABDLY
	tmp_tc.c_oflag &= ~TABDLY;
	tmp_tc.c_oflag |= TAB3;
# endif
#endif
    } else {
#ifndef USE_TERMIO
	sb.sg_flags &= ~XTABS;
#else
# ifdef	OXTABS
	tmp_tc.c_oflag &= ~OXTABS;
# endif
# ifdef	TABDLY
	tmp_tc.c_oflag &= ~TABDLY;
# endif
#endif
    }

    if (f&MODE_LIT_ECHO) {
#ifndef USE_TERMIO
	lmode &= ~LCTLECH;
#else
# ifdef	ECHOCTL
	tmp_tc.c_lflag &= ~ECHOCTL;
# endif
#endif
    } else {
#ifndef USE_TERMIO
	lmode |= LCTLECH;
#else
# ifdef	ECHOCTL
	tmp_tc.c_lflag |= ECHOCTL;
# endif
#endif
    }

    if (f == -1) {
	onoff = 0;
    } else {
#ifndef	USE_TERMIO
	if (f & MODE_OUTBIN)
		lmode |= LLITOUT;
	else
		lmode &= ~LLITOUT;

	if (f & MODE_INBIN)
		lmode |= LPASS8;
	else
		lmode &= ~LPASS8;
#else
	if (f & MODE_INBIN)
		tmp_tc.c_iflag &= ~ISTRIP;
	else
		tmp_tc.c_iflag |= ISTRIP;
	if (f & MODE_OUTBIN) {
		tmp_tc.c_cflag &= ~(CSIZE|PARENB);
		tmp_tc.c_cflag |= CS8;
		tmp_tc.c_oflag &= ~OPOST;
	} else {
		tmp_tc.c_cflag &= ~(CSIZE|PARENB);
		tmp_tc.c_cflag |= old_tc.c_cflag & (CSIZE|PARENB);
		tmp_tc.c_oflag |= OPOST;
	}
#endif
	onoff = 1;
    }

    if (f != -1) {
#ifdef  SIGINT
	(void) signal(SIGINT, intr);
#endif
#ifdef  SIGQUIT
	(void) signal(SIGQUIT, intr2);
#endif
#ifdef	SIGTSTP
	(void) signal(SIGTSTP, susp);
#endif	/* SIGTSTP */
#ifdef	SIGINFO
	(void) signal(SIGINFO, ayt);
#endif
#if	defined(USE_TERMIO) && defined(NOKERNINFO)
	tmp_tc.c_lflag |= NOKERNINFO;
#endif
	/*
	 * We don't want to process ^Y here.  It's just another
	 * character that we'll pass on to the back end.  It has
	 * to process it because it will be processed when the
	 * user attempts to read it, not when we send it.
	 */
#ifndef	USE_TERMIO
	ltc.t_dsuspc = _POSIX_VDISABLE;
#else
# ifdef	VDSUSP
	tmp_tc.c_cc[VDSUSP] = (cc_t)(_POSIX_VDISABLE);
# endif
#endif
#ifdef	USE_TERMIO
	/*
	 * If the VEOL character is already set, then use VEOL2,
	 * otherwise use VEOL.
	 */
	esc = (rlogin != _POSIX_VDISABLE) ? rlogin : escape;
	if ((tmp_tc.c_cc[VEOL] != esc)
# ifdef	VEOL2
	    && (tmp_tc.c_cc[VEOL2] != esc)
# endif
	    ) {
		if (tmp_tc.c_cc[VEOL] == (cc_t)(_POSIX_VDISABLE))
		    tmp_tc.c_cc[VEOL] = esc;
# ifdef	VEOL2
		else if (tmp_tc.c_cc[VEOL2] == (cc_t)(_POSIX_VDISABLE))
		    tmp_tc.c_cc[VEOL2] = esc;
# endif
	}
#else
	if (tc.t_brkc == (cc_t)(_POSIX_VDISABLE))
		tc.t_brkc = esc;
#endif
    } else {
#ifdef	SIGINFO
	(void) signal(SIGINFO, (void (*)(int))ayt_status);
#endif
#ifdef  SIGINT
	(void) signal(SIGINT, SIG_DFL);
#endif
#ifdef  SIGQUIT
	(void) signal(SIGQUIT, SIG_DFL);
#endif
#ifdef	SIGTSTP
	(void) signal(SIGTSTP, SIG_DFL);
# ifndef SOLARIS
	(void) sigsetmask(sigblock(0) & ~(1<<(SIGTSTP-1)));
# else	/* SOLARIS */
	(void) sigrelse(SIGTSTP);
# endif	/* SOLARIS */
#endif	/* SIGTSTP */
#ifndef USE_TERMIO
	ltc = oltc;
	tc = otc;
	sb = ottyb;
	lmode = olmode;
#else
	tmp_tc = old_tc;
#endif
    }
#ifndef USE_TERMIO
    ioctl(tin, TIOCLSET, (char *)&lmode);
    ioctl(tin, TIOCSLTC, (char *)&ltc);
    ioctl(tin, TIOCSETC, (char *)&tc);
    ioctl(tin, TIOCSETN, (char *)&sb);
#else
    if (tcsetattr(tin, TCSADRAIN, &tmp_tc) < 0)
	tcsetattr(tin, TCSANOW, &tmp_tc);
#endif

    ioctl(tin, FIONBIO, (char *)&onoff);
    ioctl(tout, FIONBIO, (char *)&onoff);

}

void
TerminalSpeeds(long *ispeed, long *ospeed)
{
#ifdef	DECODE_BAUD
    struct termspeeds *tp;
#endif	/* DECODE_BAUD */
    long in, out;

    out = cfgetospeed(&old_tc);
    in = cfgetispeed(&old_tc);
    if (in == 0)
	in = out;

#ifdef	DECODE_BAUD
    tp = termspeeds;
    while ((tp->speed != -1) && (tp->value < in))
	tp++;
    *ispeed = tp->speed;

    tp = termspeeds;
    while ((tp->speed != -1) && (tp->value < out))
	tp++;
    *ospeed = tp->speed;
#else	/* DECODE_BAUD */
	*ispeed = in;
	*ospeed = out;
#endif	/* DECODE_BAUD */
}

int
TerminalWindowSize(long *rows, long *cols)
{
#ifdef	TIOCGWINSZ
    struct winsize ws;

    if (ioctl(fileno(stdin), TIOCGWINSZ, (char *)&ws) >= 0) {
	*rows = ws.ws_row;
	*cols = ws.ws_col;
	return 1;
    }
#endif	/* TIOCGWINSZ */
    return 0;
}

int
NetClose(int fd)
{
    return close(fd);
}

static void
NetNonblockingIO(int fd, int onoff)
{
    ioctl(fd, FIONBIO, (char *)&onoff);
}


/*
 * Various signal handling routines.
 */

/* ARGSUSED */
SIG_FUNC_RET
intr(int sig __unused)
{
    if (localchars) {
	intp();
	return;
    }
    setcommandmode();
    longjmp(toplevel, -1);
}

/* ARGSUSED */
SIG_FUNC_RET
intr2(int sig __unused)
{
    if (localchars) {
#ifdef	KLUDGELINEMODE
	if (kludgelinemode)
	    sendbrk();
	else
#endif
	    sendabort();
	return;
    }
}

#ifdef	SIGTSTP
/* ARGSUSED */
SIG_FUNC_RET
susp(int sig __unused)
{
    if ((rlogin != _POSIX_VDISABLE) && rlogin_susp())
	return;
    if (localchars)
	sendsusp();
}
#endif

#ifdef	SIGWINCH
/* ARGSUSED */
static SIG_FUNC_RET
sendwin(int sig __unused)
{
    if (connected) {
	sendnaws();
    }
}
#endif

#ifdef	SIGINFO
/* ARGSUSED */
SIG_FUNC_RET
ayt(int sig __unused)
{
    if (connected)
	sendayt();
    else
	ayt_status();
}
#endif


void
sys_telnet_init(void)
{
    (void) signal(SIGINT, intr);
    (void) signal(SIGQUIT, intr2);
    (void) signal(SIGPIPE, SIG_IGN);
#ifdef	SIGWINCH
    (void) signal(SIGWINCH, sendwin);
#endif
#ifdef	SIGTSTP
    (void) signal(SIGTSTP, susp);
#endif
#ifdef	SIGINFO
    (void) signal(SIGINFO, ayt);
#endif

    setconnmode(0);

    NetNonblockingIO(net, 1);

#if	defined(SO_OOBINLINE)
    if (SetSockOpt(net, SOL_SOCKET, SO_OOBINLINE, 1) == -1) {
	perror("SetSockOpt");
    }
#endif	/* defined(SO_OOBINLINE) */
}

/*
 * Process rings -
 *
 *	This routine tries to fill up/empty our various rings.
 *
 *	The parameter specifies whether this is a poll operation,
 *	or a block-until-something-happens operation.
 *
 *	The return value is 1 if something happened, 0 if not.
 */

int
process_rings(int netin, int netout, int netex, int ttyin, int ttyout, int poll)
{
    int c;
    int returnValue = 0;
    static struct timeval TimeValue = { 0, 0 };
    int maxfd = -1;
    int tmp;

    if ((netout || netin || netex) && net > maxfd)
	maxfd = net;
    
    if (ttyout && tout > maxfd)
	maxfd = tout;
    if (ttyin && tin > maxfd)
	maxfd = tin;
    tmp = howmany(maxfd+1, NFDBITS) * sizeof(fd_mask);
    if (tmp > fdsn) {
	if (ibitsp)
	    free(ibitsp);
	if (obitsp)
	    free(obitsp);
	if (xbitsp)
	    free(xbitsp);
	
	fdsn = tmp;
	if ((ibitsp = (fd_set *)malloc(fdsn)) == NULL)
	    err(1, "malloc");
	if ((obitsp = (fd_set *)malloc(fdsn)) == NULL)
	    err(1, "malloc");
	if ((xbitsp = (fd_set *)malloc(fdsn)) == NULL)
	    err(1, "malloc");
	memset(ibitsp, 0, fdsn);
	memset(obitsp, 0, fdsn);
	memset(xbitsp, 0, fdsn);
    }
    
    if (netout)
	FD_SET(net, obitsp);
    if (ttyout)
	FD_SET(tout, obitsp);
    if (ttyin)
	FD_SET(tin, ibitsp);
    if (netin)
	FD_SET(net, ibitsp);
    if (netex)
	FD_SET(net, xbitsp);
    if ((c = select(maxfd + 1, ibitsp, obitsp, xbitsp,
	     (poll == 0)? (struct timeval *)0 : &TimeValue)) < 0) {
	if (c == -1) {
		    /*
		     * we can get EINTR if we are in line mode,
		     * and the user does an escape (TSTP), or
		     * some other signal generator.
		     */
	    if (errno == EINTR) {
		return 0;
	    }
		    /* I don't like this, does it ever happen? */
	    printf("sleep(5) from telnet, after select: %s\r\n", strerror(errno));
	    sleep(5);
	}
	return 0;
    }

    /*
     * Any urgent data?
     */
    if (FD_ISSET(net, xbitsp)) {
	FD_CLR(net, xbitsp);
	SYNCHing = 1;
	(void) ttyflush(1);	/* flush already enqueued data */
    }

    /*
     * Something to read from the network...
     */
    if (FD_ISSET(net, ibitsp)) {
	int canread;

	FD_CLR(net, ibitsp);
	canread = ring_empty_consecutive(&netiring);
#if	!defined(SO_OOBINLINE)
	    /*
	     * In 4.2 (and some early 4.3) systems, the
	     * OOB indication and data handling in the kernel
	     * is such that if two separate TCP Urgent requests
	     * come in, one byte of TCP data will be overlaid.
	     * This is fatal for Telnet, but we try to live
	     * with it.
	     *
	     * In addition, in 4.2 (and...), a special protocol
	     * is needed to pick up the TCP Urgent data in
	     * the correct sequence.
	     *
	     * What we do is:  if we think we are in urgent
	     * mode, we look to see if we are "at the mark".
	     * If we are, we do an OOB receive.  If we run
	     * this twice, we will do the OOB receive twice,
	     * but the second will fail, since the second
	     * time we were "at the mark", but there wasn't
	     * any data there (the kernel doesn't reset
	     * "at the mark" until we do a normal read).
	     * Once we've read the OOB data, we go ahead
	     * and do normal reads.
	     *
	     * There is also another problem, which is that
	     * since the OOB byte we read doesn't put us
	     * out of OOB state, and since that byte is most
	     * likely the TELNET DM (data mark), we would
	     * stay in the TELNET SYNCH (SYNCHing) state.
	     * So, clocks to the rescue.  If we've "just"
	     * received a DM, then we test for the
	     * presence of OOB data when the receive OOB
	     * fails (and AFTER we did the normal mode read
	     * to clear "at the mark").
	     */
	if (SYNCHing) {
	    int atmark;
	    static int bogus_oob = 0, first = 1;

	    ioctl(net, SIOCATMARK, (char *)&atmark);
	    if (atmark) {
		c = recv(net, netiring.supply, canread, MSG_OOB);
		if ((c == -1) && (errno == EINVAL)) {
		    c = recv(net, netiring.supply, canread, 0);
		    if (clocks.didnetreceive < clocks.gotDM) {
			SYNCHing = stilloob(net);
		    }
		} else if (first && c > 0) {
		    /*
		     * Bogosity check.  Systems based on 4.2BSD
		     * do not return an error if you do a second
		     * recv(MSG_OOB).  So, we do one.  If it
		     * succeeds and returns exactly the same
		     * data, then assume that we are running
		     * on a broken system and set the bogus_oob
		     * flag.  (If the data was different, then
		     * we probably got some valid new data, so
		     * increment the count...)
		     */
		    int i;
		    i = recv(net, netiring.supply + c, canread - c, MSG_OOB);
		    if (i == c &&
			memcmp(netiring.supply, netiring.supply + c, i) == 0) {
			bogus_oob = 1;
			first = 0;
		    } else if (i < 0) {
			bogus_oob = 0;
			first = 0;
		    } else
			c += i;
		}
		if (bogus_oob && c > 0) {
		    int i;
		    /*
		     * Bogosity.  We have to do the read
		     * to clear the atmark to get out of
		     * an infinate loop.
		     */
		    i = read(net, netiring.supply + c, canread - c);
		    if (i > 0)
			c += i;
		}
	    } else {
		c = recv(net, netiring.supply, canread, 0);
	    }
	} else {
	    c = recv(net, netiring.supply, canread, 0);
	}
	settimer(didnetreceive);
#else	/* !defined(SO_OOBINLINE) */
	c = recv(net, (char *)netiring.supply, canread, 0);
#endif	/* !defined(SO_OOBINLINE) */
	if (c < 0 && errno == EWOULDBLOCK) {
	    c = 0;
	} else if (c <= 0) {
	    return -1;
	}
	if (netdata) {
	    Dump('<', netiring.supply, c);
	}
	if (c)
	    ring_supplied(&netiring, c);
	returnValue = 1;
    }

    /*
     * Something to read from the tty...
     */
    if (FD_ISSET(tin, ibitsp)) {
	FD_CLR(tin, ibitsp);
	c = TerminalRead(ttyiring.supply, ring_empty_consecutive(&ttyiring));
	if (c < 0 && errno == EIO)
	    c = 0;
	if (c < 0 && errno == EWOULDBLOCK) {
	    c = 0;
	} else {
	    /* EOF detection for line mode!!!! */
	    if ((c == 0) && MODE_LOCAL_CHARS(globalmode) && isatty(tin)) {
			/* must be an EOF... */
		*ttyiring.supply = termEofChar;
		c = 1;
	    }
	    if (c <= 0) {
		return -1;
	    }
	    if (termdata) {
		Dump('<', ttyiring.supply, c);
	    }
	    ring_supplied(&ttyiring, c);
	}
	returnValue = 1;		/* did something useful */
    }

    if (FD_ISSET(net, obitsp)) {
	FD_CLR(net, obitsp);
	returnValue |= netflush();
    }
    if (FD_ISSET(tout, obitsp)) {
	FD_CLR(tout, obitsp);
	returnValue |= (ttyflush(SYNCHing|flushout) > 0);
    }

    return returnValue;
}
