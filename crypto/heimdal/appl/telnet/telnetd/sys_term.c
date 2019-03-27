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

#include "telnetd.h"

RCSID("$Id$");

#if defined(_CRAY) || (defined(__hpux) && !defined(HAVE_UTMPX_H))
# define PARENT_DOES_UTMP
#endif

#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif

#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif

#ifdef HAVE_UTMPX_H
struct	utmpx wtmp;
#elif defined(HAVE_UTMP_H)
struct	utmp wtmp;
#endif /* HAVE_UTMPX_H */

#ifdef HAVE_STRUCT_UTMP_UT_HOST
int	utmp_len = sizeof(wtmp.ut_host);
#else
int	utmp_len = MaxHostNameLen;
#endif

#ifndef UTMP_FILE
#ifdef _PATH_UTMP
#define UTMP_FILE _PATH_UTMP
#else
#define UTMP_FILE "/etc/utmp"
#endif
#endif

/* really, mac os uses wtmpx (or asl) */
#ifdef __APPLE__
#undef _PATH_WTMP
#endif

#if !defined(WTMP_FILE) && defined(_PATH_WTMP)
#define WTMP_FILE _PATH_WTMP
#endif

#ifndef PARENT_DOES_UTMP
#ifdef WTMP_FILE
char	wtmpf[] = WTMP_FILE;
#else
char	wtmpf[]	= "/usr/adm/wtmp";
#endif
char	utmpf[] = UTMP_FILE;
#else /* PARENT_DOES_UTMP */
#ifdef WTMP_FILE
char	wtmpf[] = WTMP_FILE;
#else
char	wtmpf[]	= "/etc/wtmp";
#endif
#endif /* PARENT_DOES_UTMP */

#ifdef HAVE_TMPDIR_H
#include <tmpdir.h>
#endif	/* CRAY */

#if !(defined(__sgi) || defined(__linux) || defined(_AIX)) && defined(HAVE_SYS_TTY)
#include <sys/tty.h>
#endif
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

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#else
#ifdef HAVE_TERMIO_H
#include <termio.h>
#endif
#endif

#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

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
# ifdef  STREAMSPTY
     static int ttyfd = -1;
     int really_stream = 0;
# else
#define really_stream 0
# endif

     const char *new_login = _PATH_LOGIN;

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
# ifdef  STREAMSPTY
    if (really_stream)
	tcgetattr(ttyfd, &termbuf);
    else
# endif
	tcgetattr(ourpty, &termbuf);
    termbuf2 = termbuf;
}

void
set_termbuf(void)
{
    /*
     * Only make the necessary changes.
	 */
    if (memcmp(&termbuf, &termbuf2, sizeof(termbuf))) {
# ifdef  STREAMSPTY
	if (really_stream)
	    tcsetattr(ttyfd, TCSANOW, &termbuf);
	else
# endif
	    tcsetattr(ourpty, TCSANOW, &termbuf);
    }
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


int
spcset(int func, cc_t *valp, cc_t **valpp)
{

#define	setval(a, b)	*valp = termbuf.c_cc[a]; \
    *valpp = &termbuf.c_cc[a]; \
				   return(b);
#define	defval(a) *valp = ((cc_t)a); *valpp = (cc_t *)0; return(SLC_DEFAULT);

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

#ifdef _CRAY
/*
 * getnpty()
 *
 * Return the number of pty's configured into the system.
 */
int
getnpty()
{
#ifdef _SC_CRAY_NPTY
    int numptys;

    if ((numptys = sysconf(_SC_CRAY_NPTY)) != -1)
	return numptys;
    else
#endif /* _SC_CRAY_NPTY */
	return 128;
}
#endif /* CRAY */

/*
 * getpty()
 *
 * Allocate a pty.  As a side effect, the external character
 * array "line" contains the name of the slave side.
 *
 * Returns the file descriptor of the opened pty.
 */

static int ptyslavefd = -1;

static char Xline[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
char *line = Xline;

#ifdef	_CRAY
char myline[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
#endif	/* CRAY */

#if !defined(HAVE_PTSNAME) && defined(STREAMSPTY)
static char *ptsname(int fd)
{
#ifdef HAVE_TTYNAME
    return ttyname(fd);
#else
    return NULL;
#endif
}
#endif

int getpty(int *ptynum)
{
#if defined(HAVE_OPENPTY) || defined(__linux) || defined(__osf__) /* XXX */
    {
	int master;
	int slave;
	if(openpty(&master, &slave, line, 0, 0) == 0){
	    ptyslavefd = slave;
	    return master;
	}
    }
#endif /* HAVE_OPENPTY .... */
#ifdef HAVE__GETPTY
    {
	int master;
	char *p;
	p = _getpty(&master, O_RDWR, 0600, 1);
	if(p == NULL)
	    return -1;
	strlcpy(line, p, sizeof(Xline));
	return master;
    }
#endif

#ifdef	STREAMSPTY
    {
	char *clone[] = { "/dev/ptc", "/dev/ptmx", "/dev/ptm",
			  "/dev/ptym/clone", 0 };

	char **q;
	int p;
	for(q=clone; *q; q++){
	    p=open(*q, O_RDWR);
	    if(p >= 0){
#ifdef HAVE_GRANTPT
		grantpt(p);
#endif
#ifdef HAVE_UNLOCKPT
		unlockpt(p);
#endif
		strlcpy(line, ptsname(p), sizeof(Xline));
		really_stream = 1;
		return p;
	    }
	}
    }
#endif /* STREAMSPTY */
#ifndef _CRAY
    {
	int p;
	char *cp, *p1, *p2;
	int i;

#ifndef	__hpux
	snprintf(line, sizeof(Xline), "/dev/ptyXX");
	p1 = &line[8];
	p2 = &line[9];
#else
	snprintf(line, sizeof(Xline), "/dev/ptym/ptyXX");
	p1 = &line[13];
	p2 = &line[14];
#endif


	for (cp = "pqrstuvwxyzPQRST"; *cp; cp++) {
	    struct stat stb;

	    *p1 = *cp;
	    *p2 = '0';
	    /*
	     * This stat() check is just to keep us from
	     * looping through all 256 combinations if there
	     * aren't that many ptys available.
	     */
	    if (stat(line, &stb) < 0)
		break;
	    for (i = 0; i < 16; i++) {
		*p2 = "0123456789abcdef"[i];
		p = open(line, O_RDWR);
		if (p > 0) {
#if SunOS == 40
		    int dummy;
#endif

#ifndef	__hpux
		    line[5] = 't';
#else
		    for (p1 = &line[8]; *p1; p1++)
			*p1 = *(p1+1);
		    line[9] = 't';
#endif
		    chown(line, 0, 0);
		    chmod(line, 0600);
#if SunOS == 40
		    if (ioctl(p, TIOCGPGRP, &dummy) == 0
			|| errno != EIO) {
			chmod(line, 0666);
			close(p);
			line[5] = 'p';
		    } else
#endif /* SunOS == 40 */
			return(p);
		}
	    }
	}
    }
#else	/* CRAY */
    {
	extern lowpty, highpty;
	struct stat sb;
	int p;

	for (*ptynum = lowpty; *ptynum <= highpty; (*ptynum)++) {
	    snprintf(myline, sizeof(myline), "/dev/pty/%03d", *ptynum);
	    p = open(myline, 2);
	    if (p < 0)
		continue;
	    snprintf(line, sizeof(Xline), "/dev/ttyp%03d", *ptynum);
	    /*
	     * Here are some shenanigans to make sure that there
	     * are no listeners lurking on the line.
	     */
	    if(stat(line, &sb) < 0) {
		close(p);
		continue;
	    }
	    if(sb.st_uid || sb.st_gid || sb.st_mode != 0600) {
		chown(line, 0, 0);
		chmod(line, 0600);
		close(p);
		p = open(myline, 2);
		if (p < 0)
		    continue;
	    }
	    /*
	     * Now it should be safe...check for accessability.
	     */
	    if (access(line, 6) == 0)
		return(p);
	    else {
		/* no tty side to pty so skip it */
		close(p);
	    }
	}
    }
#endif	/* CRAY */
    return(-1);
}


int
tty_isecho(void)
{
    return (termbuf.c_lflag & ECHO);
}

int
tty_flowmode(void)
{
    return((termbuf.c_iflag & IXON) ? 1 : 0);
}

int
tty_restartany(void)
{
    return((termbuf.c_iflag & IXANY) ? 1 : 0);
}

void
tty_setecho(int on)
{
    if (on)
	termbuf.c_lflag |= ECHO;
    else
	termbuf.c_lflag &= ~ECHO;
}

int
tty_israw(void)
{
    return(!(termbuf.c_lflag & ICANON));
}

void
tty_binaryin(int on)
{
    if (on) {
	termbuf.c_iflag &= ~ISTRIP;
    } else {
	termbuf.c_iflag |= ISTRIP;
    }
}

void
tty_binaryout(int on)
{
    if (on) {
	termbuf.c_cflag &= ~(CSIZE|PARENB);
	termbuf.c_cflag |= CS8;
	termbuf.c_oflag &= ~OPOST;
    } else {
	termbuf.c_cflag &= ~CSIZE;
	termbuf.c_cflag |= CS7|PARENB;
	termbuf.c_oflag |= OPOST;
    }
}

int
tty_isbinaryin(void)
{
    return(!(termbuf.c_iflag & ISTRIP));
}

int
tty_isbinaryout(void)
{
    return(!(termbuf.c_oflag&OPOST));
}


int
tty_issofttab(void)
{
# ifdef	OXTABS
    return (termbuf.c_oflag & OXTABS);
# endif
# ifdef	TABDLY
    return ((termbuf.c_oflag & TABDLY) == TAB3);
# endif
}

void
tty_setsofttab(int on)
{
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
}

int
tty_islitecho(void)
{
# ifdef	ECHOCTL
    return (!(termbuf.c_lflag & ECHOCTL));
# endif
# ifdef	TCTLECH
    return (!(termbuf.c_lflag & TCTLECH));
# endif
# if	!defined(ECHOCTL) && !defined(TCTLECH)
    return (0);	/* assumes ctl chars are echoed '^x' */
# endif
}

void
tty_setlitecho(int on)
{
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
}

int
tty_iscrnl(void)
{
    return (termbuf.c_iflag & ICRNL);
}

/*
 * Try to guess whether speeds are "encoded" (4.2BSD) or just numeric (4.4BSD).
 */
#if B4800 != 4800
#define	DECODE_BAUD
#endif

#ifdef	DECODE_BAUD

/*
 * A table of available terminal speeds
 */
struct termspeeds {
    int	speed;
    int	value;
} termspeeds[] = {
    { 0,      B0 },      { 50,    B50 },    { 75,     B75 },
    { 110,    B110 },    { 134,   B134 },   { 150,    B150 },
    { 200,    B200 },    { 300,   B300 },   { 600,    B600 },
    { 1200,   B1200 },   { 1800,  B1800 },  { 2400,   B2400 },
    { 4800,   B4800 },
#ifdef	B7200
    { 7200,  B7200 },
#endif
    { 9600,   B9600 },
#ifdef	B14400
    { 14400,  B14400 },
#endif
#ifdef	B19200
    { 19200,  B19200 },
#endif
#ifdef	B28800
    { 28800,  B28800 },
#endif
#ifdef	B38400
    { 38400,  B38400 },
#endif
#ifdef	B57600
    { 57600,  B57600 },
#endif
#ifdef	B115200
    { 115200, B115200 },
#endif
#ifdef	B230400
    { 230400, B230400 },
#endif
    { -1,     0 }
};
#endif	/* DECODE_BUAD */

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
#else	/* DECODE_BUAD */
    cfsetospeed(&termbuf, val);
#endif	/* DECODE_BUAD */
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

#ifdef PARENT_DOES_UTMP
extern	struct utmp wtmp;
extern char wtmpf[];

extern void utmp_sig_init (void);
extern void utmp_sig_reset (void);
extern void utmp_sig_wait (void);
extern void utmp_sig_notify (int);
# endif /* PARENT_DOES_UTMP */

#ifdef STREAMSPTY

/* I_FIND seems to live a life of its own */
static int my_find(int fd, char *module)
{
#if defined(I_FIND) && defined(I_LIST)
    static int flag;
    static struct str_list sl;
    int n;
    int i;

    if(!flag){
	n = ioctl(fd, I_LIST, 0);
	if(n < 0){
	    perror("ioctl(fd, I_LIST, 0)");
	    return -1;
	}
	sl.sl_modlist=(struct str_mlist*)malloc(n * sizeof(struct str_mlist));
	sl.sl_nmods = n;
	n = ioctl(fd, I_LIST, &sl);
	if(n < 0){
	    perror("ioctl(fd, I_LIST, n)");
	    return -1;
	}
	flag = 1;
    }

    for(i=0; i<sl.sl_nmods; i++)
	if(!strcmp(sl.sl_modlist[i].l_name, module))
	    return 1;
#endif
    return 0;
}

static void maybe_push_modules(int fd, char **modules)
{
    char **p;
    int err;

    for(p=modules; *p; p++){
	err = my_find(fd, *p);
	if(err == 1)
	    break;
	if(err < 0 && errno != EINVAL)
	    fatalperror(net, "my_find()");
	/* module not pushed or does not exist */
    }
    /* p points to null or to an already pushed module, now push all
       modules before this one */

    for(p--; p >= modules; p--){
	err = ioctl(fd, I_PUSH, *p);
	if(err < 0 && errno != EINVAL)
	    fatalperror(net, "I_PUSH");
    }
}
#endif

/*
 * getptyslave()
 *
 * Open the slave side of the pty, and do any initialization
 * that is necessary.  The return value is a file descriptor
 * for the slave side.
 */
void getptyslave(void)
{
    int t = -1;

    struct winsize ws;
    /*
     * Opening the slave side may cause initilization of the
     * kernel tty structure.  We need remember the state of
     * 	if linemode was turned on
     *	terminal window size
     *	terminal speed
     * so that we can re-set them if we need to.
     */


    /*
     * Make sure that we don't have a controlling tty, and
     * that we are the session (process group) leader.
     */

#ifdef HAVE_SETSID
    if(setsid()<0)
	fatalperror(net, "setsid()");
#else
# ifdef	TIOCNOTTY
    t = open(_PATH_TTY, O_RDWR);
    if (t >= 0) {
	ioctl(t, TIOCNOTTY, (char *)0);
	close(t);
    }
# endif
#endif

# ifdef PARENT_DOES_UTMP
    /*
     * Wait for our parent to get the utmp stuff to get done.
     */
    utmp_sig_wait();
# endif

    t = cleanopen(line);
    if (t < 0)
	fatalperror(net, line);

#ifdef  STREAMSPTY
    ttyfd = t;


    /*
     * Not all systems have (or need) modules ttcompat and pckt so
     * don't flag it as a fatal error if they don't exist.
     */

    if (really_stream)
	{
	    /* these are the streams modules that we want pushed. note
	       that they are in reverse order, ptem will be pushed
	       first. maybe_push_modules() will try to push all modules
	       before the first one that isn't already pushed. i.e if
	       ldterm is pushed, only ttcompat will be attempted.

	       all this is because we don't know which modules are
	       available, and we don't know which modules are already
	       pushed (via autopush, for instance).

	       */

	    char *ttymodules[] = { "ttcompat", "ldterm", "ptem", NULL };
	    char *ptymodules[] = { "pckt", NULL };

	    maybe_push_modules(t, ttymodules);
	    maybe_push_modules(ourpty, ptymodules);
	}
#endif
    /*
     * set up the tty modes as we like them to be.
     */
    init_termbuf();
# ifdef	TIOCSWINSZ
    if (def_row || def_col) {
	memset(&ws, 0, sizeof(ws));
	ws.ws_col = def_col;
	ws.ws_row = def_row;
	ioctl(t, TIOCSWINSZ, (char *)&ws);
    }
# endif

    /*
     * Settings for sgtty based systems
     */

    /*
     * Settings for UNICOS (and HPUX)
     */
# if defined(_CRAY) || defined(__hpux)
    termbuf.c_oflag = OPOST|ONLCR|TAB3;
    termbuf.c_iflag = IGNPAR|ISTRIP|ICRNL|IXON;
    termbuf.c_lflag = ISIG|ICANON|ECHO|ECHOE|ECHOK;
    termbuf.c_cflag = EXTB|HUPCL|CS8;
# endif

    /*
     * Settings for all other termios/termio based
     * systems, other than 4.4BSD.  In 4.4BSD the
     * kernel does the initial terminal setup.
     */
# if !(defined(_CRAY) || defined(__hpux)) && (BSD <= 43)
#  ifndef	OXTABS
#   define OXTABS	0
#  endif
    termbuf.c_lflag |= ECHO;
    termbuf.c_oflag |= ONLCR|OXTABS;
    termbuf.c_iflag |= ICRNL;
    termbuf.c_iflag &= ~IXOFF;
# endif
    tty_rspeed((def_rspeed > 0) ? def_rspeed : 9600);
    tty_tspeed((def_tspeed > 0) ? def_tspeed : 9600);

    /*
     * Set the tty modes, and make this our controlling tty.
     */
    set_termbuf();
    if (login_tty(t) == -1)
	fatalperror(net, "login_tty");
    if (net > 2)
	close(net);
    if (ourpty > 2) {
	close(ourpty);
	ourpty = -1;
    }
}

#ifndef	O_NOCTTY
#define	O_NOCTTY	0
#endif
/*
 * Open the specified slave side of the pty,
 * making sure that we have a clean tty.
 */

int cleanopen(char *line)
{
    int t;

    if (ptyslavefd != -1)
	return ptyslavefd;

#ifdef STREAMSPTY
    if (!really_stream)
#endif
	{
	    /*
	     * Make sure that other people can't open the
	     * slave side of the connection.
	     */
	    chown(line, 0, 0);
	    chmod(line, 0600);
	}

#ifdef HAVE_REVOKE
    revoke(line);
#endif

    t = open(line, O_RDWR|O_NOCTTY);

    if (t < 0)
	return(-1);

    /*
     * Hangup anybody else using this ttyp, then reopen it for
     * ourselves.
     */
# if !(defined(_CRAY) || defined(__hpux)) && (BSD <= 43) && !defined(STREAMSPTY)
    signal(SIGHUP, SIG_IGN);
#ifdef HAVE_VHANGUP
    vhangup();
#else
#endif
    signal(SIGHUP, SIG_DFL);
    t = open(line, O_RDWR|O_NOCTTY);
    if (t < 0)
	return(-1);
# endif
# if	defined(_CRAY) && defined(TCVHUP)
    {
	int i;
	signal(SIGHUP, SIG_IGN);
	ioctl(t, TCVHUP, (char *)0);
	signal(SIGHUP, SIG_DFL);

	i = open(line, O_RDWR);

	if (i < 0)
	    return(-1);
	close(t);
	t = i;
    }
# endif	/* defined(CRAY) && defined(TCVHUP) */
    return(t);
}

#if !defined(BSD4_4)

int login_tty(int t)
{
    /* Dont need to set this as the controlling PTY on steams sockets,
     * don't abort on failure. */
# if defined(TIOCSCTTY) && !defined(__hpux)
    if (ioctl(t, TIOCSCTTY, (char *)0) < 0 && !really_stream)
	fatalperror(net, "ioctl(sctty)");
#  ifdef _CRAY
    /*
     * Close the hard fd to /dev/ttypXXX, and re-open through
     * the indirect /dev/tty interface.
     */
    close(t);
    if ((t = open("/dev/tty", O_RDWR)) < 0)
	fatalperror(net, "open(/dev/tty)");
#  endif
# else
    /*
     * We get our controlling tty assigned as a side-effect
     * of opening up a tty device.  But on BSD based systems,
     * this only happens if our process group is zero.  The
     * setsid() call above may have set our pgrp, so clear
     * it out before opening the tty...
     */
#ifdef HAVE_SETPGID
    setpgid(0, 0);
#else
    setpgrp(0, 0); /* if setpgid isn't available, setpgrp
		      probably takes arguments */
#endif
    close(open(line, O_RDWR));
# endif
    if (t != 0)
	dup2(t, 0);
    if (t != 1)
	dup2(t, 1);
    if (t != 2)
	dup2(t, 2);
    if (t > 2)
	close(t);
    return(0);
}
#endif	/* BSD <= 43 */

/*
 * This comes from ../../bsd/tty.c and should not really be here.
 */

/*
 * Clean the tty name.  Return a pointer to the cleaned version.
 */

static char * clean_ttyname (char *) __attribute__((unused));

static char *
clean_ttyname (char *tty)
{
  char *res = tty;

  if (strncmp (res, _PATH_DEV, strlen(_PATH_DEV)) == 0)
    res += strlen(_PATH_DEV);
  if (strncmp (res, "pty/", 4) == 0)
    res += 4;
  if (strncmp (res, "ptym/", 5) == 0)
    res += 5;
  return res;
}

/*
 * Generate a name usable as an `ut_id', typically without `tty'.
 */

#ifdef HAVE_STRUCT_UTMP_UT_ID
static char *
make_id (char *tty)
{
  char *res = tty;

  if (strncmp (res, "pts/", 4) == 0)
    res += 4;
  if (strncmp (res, "tty", 3) == 0)
    res += 3;
  return res;
}
#endif

/*
 * startslave(host)
 *
 * Given a hostname, do whatever
 * is necessary to startup the login process on the slave side of the pty.
 */

/* ARGSUSED */
void
startslave(const char *host, const char *utmp_host,
	   int autologin, char *autoname)
{
    int i;

#ifdef AUTHENTICATION
    if (!autoname || !autoname[0])
	autologin = 0;

    if (autologin < auth_level) {
	fatal(net, "Authorization failed");
	exit(1);
    }
#endif

    {
	char *tbuf =
	    "\r\n*** Connection not encrypted! "
	    "Communication may be eavesdropped. ***\r\n";
#ifdef ENCRYPTION
	if (!no_warn && (encrypt_output == 0 || decrypt_input == 0))
#endif
	    writenet(tbuf, strlen(tbuf));
    }
# ifdef	PARENT_DOES_UTMP
    utmp_sig_init();
# endif	/* PARENT_DOES_UTMP */

    if ((i = fork()) < 0)
	fatalperror(net, "fork");
    if (i) {
# ifdef PARENT_DOES_UTMP
	/*
	 * Cray parent will create utmp entry for child and send
	 * signal to child to tell when done.  Child waits for signal
	 * before doing anything important.
	 */
	int pid = i;
	void sigjob (int);

	setpgrp();
	utmp_sig_reset();		/* reset handler to default */
	/*
	 * Create utmp entry for child
	 */
	wtmp.ut_time = time(NULL);
	wtmp.ut_type = LOGIN_PROCESS;
	wtmp.ut_pid = pid;
	strncpy(wtmp.ut_user,  "LOGIN", sizeof(wtmp.ut_user));
	strncpy(wtmp.ut_host,  utmp_host, sizeof(wtmp.ut_host));
	strncpy(wtmp.ut_line,  clean_ttyname(line), sizeof(wtmp.ut_line));
#ifdef HAVE_STRUCT_UTMP_UT_ID
	strncpy(wtmp.ut_id, wtmp.ut_line + 3, sizeof(wtmp.ut_id));
#endif

	pututline(&wtmp);
	endutent();
	if ((i = open(wtmpf, O_WRONLY|O_APPEND)) >= 0) {
	    write(i, &wtmp, sizeof(struct utmp));
	    close(i);
	}
#ifdef	_CRAY
	signal(WJSIGNAL, sigjob);
#endif
	utmp_sig_notify(pid);
# endif	/* PARENT_DOES_UTMP */
    } else {
	getptyslave();
#if defined(DCE)
	/* if we authenticated via K5, try and join the PAG */
	kerberos5_dfspag();
#endif
	start_login(host, autologin, autoname);
	/*NOTREACHED*/
    }
}

char	*envinit[3];
#if !HAVE_DECL_ENVIRON
extern char **environ;
#endif

void
init_env(void)
{
    char **envp;

    envp = envinit;
    if ((*envp = getenv("TZ")))
	*envp++ -= 3;
#if defined(_CRAY) || defined(__hpux)
    else
	*envp++ = "TZ=GMT0";
#endif
    *envp = 0;
    environ = envinit;
}

/*
 * scrub_env()
 *
 * We only accept the environment variables listed below.
 */

static void
scrub_env(void)
{
    static const char *reject[] = {
	"TERMCAP=/",
	NULL
    };

    static const char *accept[] = {
	"XAUTH=", "XAUTHORITY=", "DISPLAY=",
	"TERM=",
	"EDITOR=",
	"PAGER=",
	"PRINTER=",
	"LOGNAME=",
	"POSIXLY_CORRECT=",
	"TERMCAP=",
	NULL
    };

    char **cpp, **cpp2;
    const char **p;

    for (cpp2 = cpp = environ; *cpp; cpp++) {
	int reject_it = 0;

	for(p = reject; *p; p++)
	    if(strncmp(*cpp, *p, strlen(*p)) == 0) {
		reject_it = 1;
		break;
	    }
	if (reject_it)
	    continue;

	for(p = accept; *p; p++)
	    if(strncmp(*cpp, *p, strlen(*p)) == 0)
		break;
	if(*p != NULL)
	    *cpp2++ = *cpp;
    }
    *cpp2 = NULL;
}


struct arg_val {
    int size;
    int argc;
    char **argv;
};

static void addarg(struct arg_val*, const char*);

/*
 * start_login(host)
 *
 * Assuming that we are now running as a child processes, this
 * function will turn us into the login process.
 */

void
start_login(const char *host, int autologin, char *name)
{
    struct arg_val argv;
    char *user;
    int save_errno;

#ifdef ENCRYPTION
    encrypt_output = NULL;
    decrypt_input = NULL;
#endif

#ifdef HAVE_UTMPX_H
    {
	int pid = getpid();
	struct utmpx utmpx;
	struct timeval tv;
	char *clean_tty;

	/*
	 * Create utmp entry for child
	 */

	clean_tty = clean_ttyname(line);
	memset(&utmpx, 0, sizeof(utmpx));
	strncpy(utmpx.ut_user,  ".telnet", sizeof(utmpx.ut_user));
	strncpy(utmpx.ut_line,  clean_tty, sizeof(utmpx.ut_line));
#ifdef HAVE_STRUCT_UTMP_UT_ID
	strncpy(utmpx.ut_id, make_id(clean_tty), sizeof(utmpx.ut_id));
#endif
	utmpx.ut_pid = pid;

	utmpx.ut_type = LOGIN_PROCESS;

	gettimeofday (&tv, NULL);
	utmpx.ut_tv.tv_sec = tv.tv_sec;
	utmpx.ut_tv.tv_usec = tv.tv_usec;

	if (pututxline(&utmpx) == NULL)
	    fatal(net, "pututxline failed");
    }
#endif

    scrub_env();

    /*
     * -h : pass on name of host.
     *		WARNING:  -h is accepted by login if and only if
     *			getuid() == 0.
     * -p : don't clobber the environment (so terminal type stays set).
     *
     * -f : force this login, he has already been authenticated
     */

    /* init argv structure */
    argv.size=0;
    argv.argc=0;
    argv.argv=malloc(0); /*so we can call realloc later */
    addarg(&argv, "login");
    addarg(&argv, "-h");
    addarg(&argv, host);
    addarg(&argv, "-p");
    if(name && name[0])
	user = name;
    else
	user = getenv("USER");
#ifdef AUTHENTICATION
    if (auth_level < 0 || autologin != AUTH_VALID) {
	if(!no_warn) {
	    printf("User not authenticated. ");
	    if (require_otp)
		printf("Using one-time password\r\n");
	    else
		printf("Using plaintext username and password\r\n");
	}
	if (require_otp) {
	    addarg(&argv, "-a");
	    addarg(&argv, "otp");
	}
	if(log_unauth)
	    syslog(LOG_INFO, "unauthenticated access from %s (%s)",
		   host, user ? user : "unknown user");
    }
    if (auth_level >= 0 && autologin == AUTH_VALID)
	addarg(&argv, "-f");
#endif
    if(user){
	addarg(&argv, "--");
	addarg(&argv, strdup(user));
    }
    if (getenv("USER")) {
	/*
	 * Assume that login will set the USER variable
	 * correctly.  For SysV systems, this means that
	 * USER will no longer be set, just LOGNAME by
	 * login.  (The problem is that if the auto-login
	 * fails, and the user then specifies a different
	 * account name, he can get logged in with both
	 * LOGNAME and USER in his environment, but the
	 * USER value will be wrong.
	 */
	unsetenv("USER");
    }
    closelog();
    /*
     * This sleep(1) is in here so that telnetd can
     * finish up with the tty.  There's a race condition
     * the login banner message gets lost...
     */
    sleep(1);

    execv(new_login, argv.argv);
    save_errno = errno;
    syslog(LOG_ERR, "%s: %m", new_login);
    fatalperror_errno(net, new_login, save_errno);
    /*NOTREACHED*/
}

static void
addarg(struct arg_val *argv, const char *val)
{
    if(argv->size <= argv->argc+1) {
	argv->argv = realloc(argv->argv, sizeof(char*) * (argv->size + 10));
	if (argv->argv == NULL)
	    fatal (net, "realloc: out of memory");
	argv->size+=10;
    }
    if((argv->argv[argv->argc++] = strdup(val)) == NULL)
	fatal (net, "strdup: out of memory");
    argv->argv[argv->argc]   = NULL;
}


/*
 * rmut()
 *
 * This is the function called by cleanup() to
 * remove the utmp entry for this person.
 */

#ifdef HAVE_UTMPX_H
static void
rmut(void)
{
    struct utmpx utmpx, *non_save_utxp;
    char *clean_tty = clean_ttyname(line);

    /*
     * This updates the utmpx and utmp entries and make a wtmp/x entry
     */

    setutxent();
    memset(&utmpx, 0, sizeof(utmpx));
    strncpy(utmpx.ut_line, clean_tty, sizeof(utmpx.ut_line));
    utmpx.ut_type = LOGIN_PROCESS;
    non_save_utxp = getutxline(&utmpx);
    if (non_save_utxp) {
	struct utmpx *utxp;
	struct timeval tv;
	char user0;

	utxp = malloc(sizeof(struct utmpx));
	*utxp = *non_save_utxp;
	user0 = utxp->ut_user[0];
	utxp->ut_user[0] = '\0';
	utxp->ut_type = DEAD_PROCESS;
#ifdef HAVE_STRUCT_UTMPX_UT_EXIT
#ifdef _STRUCT___EXIT_STATUS
	utxp->ut_exit.__e_termination = 0;
	utxp->ut_exit.__e_exit = 0;
#elif defined(__osf__) /* XXX */
	utxp->ut_exit.ut_termination = 0;
	utxp->ut_exit.ut_exit = 0;
#else
	utxp->ut_exit.e_termination = 0;
	utxp->ut_exit.e_exit = 0;
#endif
#endif
	gettimeofday (&tv, NULL);
	utxp->ut_tv.tv_sec = tv.tv_sec;
	utxp->ut_tv.tv_usec = tv.tv_usec;

	pututxline(utxp);
#ifdef WTMPX_FILE
	utxp->ut_user[0] = user0;
	updwtmpx(WTMPX_FILE, utxp);
#elif defined(WTMP_FILE)
	/* This is a strange system with a utmpx and a wtmp! */
	{
	  int f = open(wtmpf, O_WRONLY|O_APPEND);
	  struct utmp wtmp;
	  if (f >= 0) {
	    strncpy(wtmp.ut_line,  clean_tty, sizeof(wtmp.ut_line));
	    strncpy(wtmp.ut_name,  "", sizeof(wtmp.ut_name));
#ifdef HAVE_STRUCT_UTMP_UT_HOST
	    strncpy(wtmp.ut_host,  "", sizeof(wtmp.ut_host));
#endif
	    wtmp.ut_time = time(NULL);
	    write(f, &wtmp, sizeof(wtmp));
	    close(f);
	  }
	}
#endif
	free (utxp);
    }
    endutxent();
}  /* end of rmut */
#endif

#if !defined(HAVE_UTMPX_H) && !(defined(_CRAY) || defined(__hpux)) && BSD <= 43
static void
rmut(void)
{
    int f;
    int found = 0;
    struct utmp *u, *utmp;
    int nutmp;
    struct stat statbf;
    char *clean_tty = clean_ttyname(line);

    f = open(utmpf, O_RDWR);
    if (f >= 0) {
	fstat(f, &statbf);
	utmp = (struct utmp *)malloc((unsigned)statbf.st_size);
	if (!utmp)
	    syslog(LOG_ERR, "utmp malloc failed");
	if (statbf.st_size && utmp) {
	    nutmp = read(f, utmp, (int)statbf.st_size);
	    nutmp /= sizeof(struct utmp);

	    for (u = utmp ; u < &utmp[nutmp] ; u++) {
		if (strncmp(u->ut_line,
			    clean_tty,
			    sizeof(u->ut_line)) ||
		    u->ut_name[0]==0)
		    continue;
		lseek(f, ((long)u)-((long)utmp), L_SET);
		strncpy(u->ut_name,  "", sizeof(u->ut_name));
#ifdef HAVE_STRUCT_UTMP_UT_HOST
		strncpy(u->ut_host,  "", sizeof(u->ut_host));
#endif
		u->ut_time = time(NULL);
		write(f, u, sizeof(wtmp));
		found++;
	    }
	}
	close(f);
    }
    if (found) {
	f = open(wtmpf, O_WRONLY|O_APPEND);
	if (f >= 0) {
	    strncpy(wtmp.ut_line,  clean_tty, sizeof(wtmp.ut_line));
	    strncpy(wtmp.ut_name,  "", sizeof(wtmp.ut_name));
#ifdef HAVE_STRUCT_UTMP_UT_HOST
	    strncpy(wtmp.ut_host,  "", sizeof(wtmp.ut_host));
#endif
	    wtmp.ut_time = time(NULL);
	    write(f, &wtmp, sizeof(wtmp));
	    close(f);
	}
    }
    chmod(line, 0666);
    chown(line, 0, 0);
    line[strlen("/dev/")] = 'p';
    chmod(line, 0666);
    chown(line, 0, 0);
}  /* end of rmut */
#endif	/* CRAY */

#if defined(__hpux) && !defined(HAVE_UTMPX_H)
static void
rmut (char *line)
{
    struct utmp utmp;
    struct utmp *utptr;
    int fd;			/* for /etc/wtmp */

    utmp.ut_type = USER_PROCESS;
    strncpy(utmp.ut_line, clean_ttyname(line), sizeof(utmp.ut_line));
    setutent();
    utptr = getutline(&utmp);
    /* write it out only if it exists */
    if (utptr) {
	utptr->ut_type = DEAD_PROCESS;
	utptr->ut_time = time(NULL);
	pututline(utptr);
	/* set wtmp entry if wtmp file exists */
	if ((fd = open(wtmpf, O_WRONLY | O_APPEND)) >= 0) {
	    write(fd, utptr, sizeof(utmp));
	    close(fd);
	}
    }
    endutent();

    chmod(line, 0666);
    chown(line, 0, 0);
    line[14] = line[13];
    line[13] = line[12];
    line[8] = 'm';
    line[9] = '/';
    line[10] = 'p';
    line[11] = 't';
    line[12] = 'y';
    chmod(line, 0666);
    chown(line, 0, 0);
}
#endif

/*
 * cleanup()
 *
 * This is the routine to call when we are all through, to
 * clean up anything that needs to be cleaned up.
 */

#ifdef PARENT_DOES_UTMP

void
cleanup(int sig)
{
#ifdef _CRAY
    static int incleanup = 0;
    int t;
    int child_status; /* status of child process as returned by waitpid */
    int flags = WNOHANG|WUNTRACED;

    /*
     * 1: Pick up the zombie, if we are being called
     *    as the signal handler.
     * 2: If we are a nested cleanup(), return.
     * 3: Try to clean up TMPDIR.
     * 4: Fill in utmp with shutdown of process.
     * 5: Close down the network and pty connections.
     * 6: Finish up the TMPDIR cleanup, if needed.
     */
    if (sig == SIGCHLD) {
	while (waitpid(-1, &child_status, flags) > 0)
	    ;	/* VOID */
	/* Check if the child process was stopped
	 * rather than exited.  We want cleanup only if
	 * the child has died.
	 */
	if (WIFSTOPPED(child_status)) {
	    return;
	}
    }
    t = sigblock(sigmask(SIGCHLD));
    if (incleanup) {
	sigsetmask(t);
	return;
    }
    incleanup = 1;
    sigsetmask(t);

    t = cleantmp(&wtmp);
    setutent();	/* just to make sure */
#endif /* CRAY */
    rmut(line);
    close(ourpty);
    shutdown(net, 2);
#ifdef _CRAY
    if (t == 0)
	cleantmp(&wtmp);
#endif /* CRAY */
    exit(1);
}

#else /* PARENT_DOES_UTMP */

void
cleanup(int sig)
{
#if defined(HAVE_UTMPX_H) || !defined(HAVE_LOGWTMP)
    rmut();
#ifdef HAVE_VHANGUP
#ifndef __sgi
    vhangup(); /* XXX */
#endif
#endif
#else
    char *p;

    p = line + sizeof("/dev/") - 1;
    if (logout(p))
	logwtmp(p, "", "");
    chmod(line, 0666);
    chown(line, 0, 0);
    *p = 'p';
    chmod(line, 0666);
    chown(line, 0, 0);
#endif
    shutdown(net, 2);
    exit(1);
}

#endif /* PARENT_DOES_UTMP */

#ifdef PARENT_DOES_UTMP
/*
 * _utmp_sig_rcv
 * utmp_sig_init
 * utmp_sig_wait
 *	These three functions are used to coordinate the handling of
 *	the utmp file between the server and the soon-to-be-login shell.
 *	The server actually creates the utmp structure, the child calls
 *	utmp_sig_wait(), until the server calls utmp_sig_notify() and
 *	signals the future-login shell to proceed.
 */
static int caught=0;		/* NZ when signal intercepted */
static void (*func)();		/* address of previous handler */

void
_utmp_sig_rcv(sig)
     int sig;
{
    caught = 1;
    signal(SIGUSR1, func);
}

void
utmp_sig_init()
{
    /*
     * register signal handler for UTMP creation
     */
    if ((int)(func = signal(SIGUSR1, _utmp_sig_rcv)) == -1)
	fatalperror(net, "telnetd/signal");
}

void
utmp_sig_reset()
{
    signal(SIGUSR1, func);	/* reset handler to default */
}

# ifdef __hpux
# define sigoff() /* do nothing */
# define sigon() /* do nothing */
# endif

void
utmp_sig_wait()
{
    /*
     * Wait for parent to write our utmp entry.
	 */
    sigoff();
    while (caught == 0) {
	pause();	/* wait until we get a signal (sigon) */
	sigoff();	/* turn off signals while we check caught */
    }
    sigon();		/* turn on signals again */
}

void
utmp_sig_notify(pid)
{
    kill(pid, SIGUSR1);
}

#ifdef _CRAY
static int gotsigjob = 0;

	/*ARGSUSED*/
void
sigjob(sig)
     int sig;
{
    int jid;
    struct jobtemp *jp;

    while ((jid = waitjob(NULL)) != -1) {
	if (jid == 0) {
	    return;
	}
	gotsigjob++;
	jobend(jid, NULL, NULL);
    }
}

/*
 *	jid_getutid:
 *		called by jobend() before calling cleantmp()
 *		to find the correct $TMPDIR to cleanup.
 */

struct utmp *
jid_getutid(jid)
     int jid;
{
    struct utmp *cur = NULL;

    setutent();	/* just to make sure */
    while (cur = getutent()) {
	if ( (cur->ut_type != NULL) && (jid == cur->ut_jid) ) {
	    return(cur);
	}
    }

    return(0);
}

/*
 * Clean up the TMPDIR that login created.
 * The first time this is called we pick up the info
 * from the utmp.  If the job has already gone away,
 * then we'll clean up and be done.  If not, then
 * when this is called the second time it will wait
 * for the signal that the job is done.
 */
int
cleantmp(wtp)
     struct utmp *wtp;
{
    struct utmp *utp;
    static int first = 1;
    int mask, omask, ret;
    extern struct utmp *getutid (const struct utmp *_Id);


    mask = sigmask(WJSIGNAL);

    if (first == 0) {
	omask = sigblock(mask);
	while (gotsigjob == 0)
	    sigpause(omask);
	return(1);
    }
    first = 0;
    setutent();	/* just to make sure */

    utp = getutid(wtp);
    if (utp == 0) {
	syslog(LOG_ERR, "Can't get /etc/utmp entry to clean TMPDIR");
	return(-1);
    }
    /*
     * Nothing to clean up if the user shell was never started.
     */
    if (utp->ut_type != USER_PROCESS || utp->ut_jid == 0)
	return(1);

    /*
     * Block the WJSIGNAL while we are in jobend().
     */
    omask = sigblock(mask);
    ret = jobend(utp->ut_jid, utp->ut_tpath, utp->ut_user);
    sigsetmask(omask);
    return(ret);
}

int
jobend(jid, path, user)
     int jid;
     char *path;
     char *user;
{
    static int saved_jid = 0;
    static int pty_saved_jid = 0;
    static char saved_path[sizeof(wtmp.ut_tpath)+1];
    static char saved_user[sizeof(wtmp.ut_user)+1];

    /*
     * this little piece of code comes into play
     * only when ptyreconnect is used to reconnect
     * to an previous session.
     *
     * this is the only time when the
     * "saved_jid != jid" code is executed.
     */

    if ( saved_jid && saved_jid != jid ) {
	if (!path) {	/* called from signal handler */
	    pty_saved_jid = jid;
	} else {
	    pty_saved_jid = saved_jid;
	}
    }

    if (path) {
	strlcpy(saved_path, path, sizeof(saved_path));
	strlcpy(saved_user, user, sizeof(saved_user));
    }
    if (saved_jid == 0) {
	saved_jid = jid;
	return(0);
    }

    /* if the jid has changed, get the correct entry from the utmp file */

    if ( saved_jid != jid ) {
	struct utmp *utp = NULL;
	struct utmp *jid_getutid();

	utp = jid_getutid(pty_saved_jid);

	if (utp == 0) {
	    syslog(LOG_ERR, "Can't get /etc/utmp entry to clean TMPDIR");
	    return(-1);
	}

	cleantmpdir(jid, utp->ut_tpath, utp->ut_user);
	return(1);
    }

    cleantmpdir(jid, saved_path, saved_user);
    return(1);
}

/*
 * Fork a child process to clean up the TMPDIR
 */
cleantmpdir(jid, tpath, user)
     int jid;
     char *tpath;
     char *user;
{
    switch(fork()) {
    case -1:
	syslog(LOG_ERR, "TMPDIR cleanup(%s): fork() failed: %m\n",
	       tpath);
	break;
    case 0:
	execl(CLEANTMPCMD, CLEANTMPCMD, user, tpath, NULL);
	syslog(LOG_ERR, "TMPDIR cleanup(%s): execl(%s) failed: %m\n",
	       tpath, CLEANTMPCMD);
	exit(1);
    default:
	/*
	 * Forget about child.  We will exit, and
	 * /etc/init will pick it up.
	 */
	break;
    }
}
#endif /* CRAY */
#endif	/* defined(PARENT_DOES_UTMP) */
