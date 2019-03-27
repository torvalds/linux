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
static const char sccsid[] = "@(#)telnet.c	8.4 (Berkeley) 5/30/95";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

/* By the way, we need to include curses.h before telnet.h since,
 * among other things, telnet.h #defines 'DO', which is a variable
 * declared in curses.h.
 */

#include <ctype.h>
#include <curses.h>
#include <signal.h>
#include <stdlib.h>
#include <term.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>

#include "ring.h"

#include "defines.h"
#include "externs.h"
#include "types.h"
#include "general.h"

#ifdef	AUTHENTICATION
#include <libtelnet/auth.h>
#endif
#ifdef	ENCRYPTION
#include <libtelnet/encrypt.h>
#endif
#include <libtelnet/misc.h>

#define	strip(x) ((my_want_state_is_wont(TELOPT_BINARY)) ? ((x)&0x7f) : (x))

static unsigned char	subbuffer[SUBBUFSIZE],
			*subpointer, *subend;	 /* buffer for sub-options */
#define	SB_CLEAR()	subpointer = subbuffer;
#define	SB_TERM()	{ subend = subpointer; SB_CLEAR(); }
#define	SB_ACCUM(c)	if (subpointer < (subbuffer+sizeof subbuffer)) { \
				*subpointer++ = (c); \
			}

#define	SB_GET()	((*subpointer++)&0xff)
#define	SB_PEEK()	((*subpointer)&0xff)
#define	SB_EOF()	(subpointer >= subend)
#define	SB_LEN()	(subend - subpointer)

char	options[256];		/* The combined options */
char	do_dont_resp[256];
char	will_wont_resp[256];

int
	eight = 0,
	autologin = 0,	/* Autologin anyone? */
	skiprc = 0,
	connected,
	showoptions,
	ISend,		/* trying to send network data in */
	telnet_debug = 0,
	crmod,
	netdata,	/* Print out network data flow */
	crlf,		/* Should '\r' be mapped to <CR><LF> (or <CR><NUL>)? */
	telnetport,
	SYNCHing,	/* we are in TELNET SYNCH mode */
	flushout,	/* flush output */
	autoflush = 0,	/* flush output when interrupting? */
	autosynch,	/* send interrupt characters with SYNCH? */
	localflow,	/* we handle flow control locally */
	restartany,	/* if flow control enabled, restart on any character */
	localchars,	/* we recognize interrupt/quit */
	donelclchars,	/* the user has set "localchars" */
	donebinarytoggle,	/* the user has put us in binary */
	dontlecho,	/* do we suppress local echoing right now? */
	globalmode,
	doaddrlookup = 1, /* do a reverse address lookup? */
	clienteof = 0;

char *prompt = 0;
#ifdef ENCRYPTION
char *line;		/* hack around breakage in sra.c :-( !! */
#endif

cc_t escape;
cc_t rlogin;
#ifdef	KLUDGELINEMODE
cc_t echoc;
#endif

/*
 * Telnet receiver states for fsm
 */
#define	TS_DATA		0
#define	TS_IAC		1
#define	TS_WILL		2
#define	TS_WONT		3
#define	TS_DO		4
#define	TS_DONT		5
#define	TS_CR		6
#define	TS_SB		7		/* sub-option collection */
#define	TS_SE		8		/* looking for sub-option end */

static int	telrcv_state;
#ifdef	OLD_ENVIRON
unsigned char telopt_environ = TELOPT_NEW_ENVIRON;
#else
# define telopt_environ TELOPT_NEW_ENVIRON
#endif

jmp_buf	toplevel;

int	flushline;
int	linemode;

#ifdef	KLUDGELINEMODE
int	kludgelinemode = 1;
#endif

static int is_unique(char *, char **, char **);

/*
 * The following are some clocks used to decide how to interpret
 * the relationship between various variables.
 */

Clocks clocks;

/*
 * Initialize telnet environment.
 */

void
init_telnet(void)
{
    env_init();

    SB_CLEAR();
    ClearArray(options);

    connected = ISend = localflow = donebinarytoggle = 0;
#ifdef	AUTHENTICATION
#ifdef	ENCRYPTION
    auth_encrypt_connect(connected);
#endif
#endif
    restartany = -1;

    SYNCHing = 0;

    /* Don't change NetTrace */

    escape = CONTROL(']');
    rlogin = _POSIX_VDISABLE;
#ifdef	KLUDGELINEMODE
    echoc = CONTROL('E');
#endif

    flushline = 1;
    telrcv_state = TS_DATA;
}


/*
 * These routines are in charge of sending option negotiations
 * to the other side.
 *
 * The basic idea is that we send the negotiation if either side
 * is in disagreement as to what the current state should be.
 */

unsigned char ComPortBaudRate[256];

void
DoBaudRate(char *arg)
{
    char *temp, temp2[10];
    int i;
    uint32_t baudrate;

    errno = 0;
    baudrate = (uint32_t)strtol(arg, &temp, 10);
    if (temp[0] != '\0' || (baudrate == 0 && errno != 0))
	ExitString("Invalid baud rate provided.\n", 1);

    for (i = 1; termspeeds[i].speed != -1; i++)
	if (baudrate == termspeeds[i].speed)
	    break;
    if (termspeeds[i].speed == -1)
	ExitString("Invalid baud rate provided.\n", 1);

    strlcpy(ComPortBaudRate, arg, sizeof(ComPortBaudRate));

    if (NETROOM() < sizeof(temp2)) {
	ExitString("No room in buffer for baud rate.\n", 1);
	/* NOTREACHED */
    }

    snprintf(temp2, sizeof(temp2), "%c%c%c%c....%c%c", IAC, SB, TELOPT_COMPORT,
	COMPORT_SET_BAUDRATE, IAC, SE);

    baudrate = htonl(baudrate);
    memcpy(&temp2[4], &baudrate, sizeof(baudrate));
    ring_supply_data(&netoring, temp2, sizeof(temp2));
    printsub('>', &temp[2], sizeof(temp2) - 2);
}

void
send_do(int c, int init)
{
    if (init) {
	if (((do_dont_resp[c] == 0) && my_state_is_do(c)) ||
				my_want_state_is_do(c))
	    return;
	set_my_want_state_do(c);
	do_dont_resp[c]++;
    }
    if (telnetport < 0)
	return;
    NET2ADD(IAC, DO);
    NETADD(c);
    printoption("SENT", DO, c);
}

void
send_dont(int c, int init)
{
    if (init) {
	if (((do_dont_resp[c] == 0) && my_state_is_dont(c)) ||
				my_want_state_is_dont(c))
	    return;
	set_my_want_state_dont(c);
	do_dont_resp[c]++;
    }
    if (telnetport < 0)
	return;
    NET2ADD(IAC, DONT);
    NETADD(c);
    printoption("SENT", DONT, c);
}

void
send_will(int c, int init)
{
    if (init) {
	if (((will_wont_resp[c] == 0) && my_state_is_will(c)) ||
				my_want_state_is_will(c))
	    return;
	set_my_want_state_will(c);
	will_wont_resp[c]++;
    }
    if (telnetport < 0)
	return;
    NET2ADD(IAC, WILL);
    NETADD(c);
    printoption("SENT", WILL, c);
}

void
send_wont(int c, int init)
{
    if (init) {
	if (((will_wont_resp[c] == 0) && my_state_is_wont(c)) ||
				my_want_state_is_wont(c))
	    return;
	set_my_want_state_wont(c);
	will_wont_resp[c]++;
    }
    if (telnetport < 0)
	return;
    NET2ADD(IAC, WONT);
    NETADD(c);
    printoption("SENT", WONT, c);
}

void
willoption(int option)
{
	int new_state_ok = 0;

	if (do_dont_resp[option]) {
	    --do_dont_resp[option];
	    if (do_dont_resp[option] && my_state_is_do(option))
		--do_dont_resp[option];
	}

	if ((do_dont_resp[option] == 0) && my_want_state_is_dont(option)) {

	    switch (option) {

	    case TELOPT_ECHO:
	    case TELOPT_BINARY:
	    case TELOPT_SGA:
		settimer(modenegotiated);
		/* FALLTHROUGH */
	    case TELOPT_STATUS:
#ifdef	AUTHENTICATION
	    case TELOPT_AUTHENTICATION:
#endif
#ifdef	ENCRYPTION
	    case TELOPT_ENCRYPT:
#endif /* ENCRYPTION */
		new_state_ok = 1;
		break;

	    case TELOPT_TM:
		if (flushout)
		    flushout = 0;
		/*
		 * Special case for TM.  If we get back a WILL,
		 * pretend we got back a WONT.
		 */
		set_my_want_state_dont(option);
		set_my_state_dont(option);
		return;			/* Never reply to TM will's/wont's */

	    case TELOPT_LINEMODE:
	    default:
		break;
	    }

	    if (new_state_ok) {
		set_my_want_state_do(option);
		send_do(option, 0);
		setconnmode(0);		/* possibly set new tty mode */
	    } else {
		do_dont_resp[option]++;
		send_dont(option, 0);
	    }
	}
	set_my_state_do(option);
#ifdef	ENCRYPTION
	if (option == TELOPT_ENCRYPT)
		encrypt_send_support();
#endif	/* ENCRYPTION */
}

void
wontoption(int option)
{
	if (do_dont_resp[option]) {
	    --do_dont_resp[option];
	    if (do_dont_resp[option] && my_state_is_dont(option))
		--do_dont_resp[option];
	}

	if ((do_dont_resp[option] == 0) && my_want_state_is_do(option)) {

	    switch (option) {

#ifdef	KLUDGELINEMODE
	    case TELOPT_SGA:
		if (!kludgelinemode)
		    break;
		/* FALLTHROUGH */
#endif
	    case TELOPT_ECHO:
		settimer(modenegotiated);
		break;

	    case TELOPT_TM:
		if (flushout)
		    flushout = 0;
		set_my_want_state_dont(option);
		set_my_state_dont(option);
		return;		/* Never reply to TM will's/wont's */

	    default:
		break;
	    }
	    set_my_want_state_dont(option);
	    if (my_state_is_do(option))
		send_dont(option, 0);
	    setconnmode(0);			/* Set new tty mode */
	} else if (option == TELOPT_TM) {
	    /*
	     * Special case for TM.
	     */
	    if (flushout)
		flushout = 0;
	    set_my_want_state_dont(option);
	}
	set_my_state_dont(option);
}

static void
dooption(int option)
{
	int new_state_ok = 0;

	if (will_wont_resp[option]) {
	    --will_wont_resp[option];
	    if (will_wont_resp[option] && my_state_is_will(option))
		--will_wont_resp[option];
	}

	if (will_wont_resp[option] == 0) {
	  if (my_want_state_is_wont(option)) {

	    switch (option) {

	    case TELOPT_TM:
		/*
		 * Special case for TM.  We send a WILL, but pretend
		 * we sent WONT.
		 */
		send_will(option, 0);
		set_my_want_state_wont(TELOPT_TM);
		set_my_state_wont(TELOPT_TM);
		return;

	    case TELOPT_BINARY:		/* binary mode */
	    case TELOPT_NAWS:		/* window size */
	    case TELOPT_TSPEED:		/* terminal speed */
	    case TELOPT_LFLOW:		/* local flow control */
	    case TELOPT_TTYPE:		/* terminal type option */
	    case TELOPT_SGA:		/* no big deal */
#ifdef	ENCRYPTION
	    case TELOPT_ENCRYPT:	/* encryption variable option */
#endif	/* ENCRYPTION */
		new_state_ok = 1;
		break;

	    case TELOPT_NEW_ENVIRON:	/* New environment variable option */
#ifdef	OLD_ENVIRON
		if (my_state_is_will(TELOPT_OLD_ENVIRON))
			send_wont(TELOPT_OLD_ENVIRON, 1); /* turn off the old */
		goto env_common;
	    case TELOPT_OLD_ENVIRON:	/* Old environment variable option */
		if (my_state_is_will(TELOPT_NEW_ENVIRON))
			break;		/* Don't enable if new one is in use! */
	    env_common:
		telopt_environ = option;
#endif
		new_state_ok = 1;
		break;

#ifdef	AUTHENTICATION
	    case TELOPT_AUTHENTICATION:
		if (autologin)
			new_state_ok = 1;
		break;
#endif

	    case TELOPT_XDISPLOC:	/* X Display location */
		if (env_getvalue("DISPLAY"))
		    new_state_ok = 1;
		break;

	    case TELOPT_LINEMODE:
#ifdef	KLUDGELINEMODE
		kludgelinemode = 0;
		send_do(TELOPT_SGA, 1);
#endif
		set_my_want_state_will(TELOPT_LINEMODE);
		send_will(option, 0);
		set_my_state_will(TELOPT_LINEMODE);
		slc_init();
		return;

	    case TELOPT_ECHO:		/* We're never going to echo... */
	    default:
		break;
	    }

	    if (new_state_ok) {
		set_my_want_state_will(option);
		send_will(option, 0);
		setconnmode(0);			/* Set new tty mode */
	    } else {
		will_wont_resp[option]++;
		send_wont(option, 0);
	    }
	  } else {
	    /*
	     * Handle options that need more things done after the
	     * other side has acknowledged the option.
	     */
	    switch (option) {
	    case TELOPT_LINEMODE:
#ifdef	KLUDGELINEMODE
		kludgelinemode = 0;
		send_do(TELOPT_SGA, 1);
#endif
		set_my_state_will(option);
		slc_init();
		send_do(TELOPT_SGA, 0);
		return;
	    }
	  }
	}
	set_my_state_will(option);
}

static void
dontoption(int option)
{

	if (will_wont_resp[option]) {
	    --will_wont_resp[option];
	    if (will_wont_resp[option] && my_state_is_wont(option))
		--will_wont_resp[option];
	}

	if ((will_wont_resp[option] == 0) && my_want_state_is_will(option)) {
	    switch (option) {
	    case TELOPT_LINEMODE:
		linemode = 0;	/* put us back to the default state */
		break;
#ifdef	OLD_ENVIRON
	    case TELOPT_NEW_ENVIRON:
		/*
		 * The new environ option wasn't recognized, try
		 * the old one.
		 */
		send_will(TELOPT_OLD_ENVIRON, 1);
		telopt_environ = TELOPT_OLD_ENVIRON;
		break;
#endif
	    }
	    /* we always accept a DONT */
	    set_my_want_state_wont(option);
	    if (my_state_is_will(option))
		send_wont(option, 0);
	    setconnmode(0);			/* Set new tty mode */
	}
	set_my_state_wont(option);
}

/*
 * Given a buffer returned by tgetent(), this routine will turn
 * the pipe separated list of names in the buffer into an array
 * of pointers to null terminated names.  We toss out any bad,
 * duplicate, or verbose names (names with spaces).
 */

static const char *name_unknown = "UNKNOWN";
static const char *unknown[] = { NULL, NULL };

static const char **
mklist(char *buf, char *name)
{
	int n;
	char c, *cp, **argvp, *cp2, **argv, **avt;

	if (name) {
		if (strlen(name) > 40) {
			name = 0;
			unknown[0] = name_unknown;
		} else {
			unknown[0] = name;
			upcase(name);
		}
	} else
		unknown[0] = name_unknown;
	/*
	 * Count up the number of names.
	 */
	for (n = 1, cp = buf; *cp && *cp != ':'; cp++) {
		if (*cp == '|')
			n++;
	}
	/*
	 * Allocate an array to put the name pointers into
	 */
	argv = (char **)malloc((n+3)*sizeof(char *));
	if (argv == 0)
		return(unknown);

	/*
	 * Fill up the array of pointers to names.
	 */
	*argv = 0;
	argvp = argv+1;
	n = 0;
	for (cp = cp2 = buf; (c = *cp);  cp++) {
		if (c == '|' || c == ':') {
			*cp++ = '\0';
			/*
			 * Skip entries that have spaces or are over 40
			 * characters long.  If this is our environment
			 * name, then put it up front.  Otherwise, as
			 * long as this is not a duplicate name (case
			 * insensitive) add it to the list.
			 */
			if (n || (cp - cp2 > 41))
				;
			else if (name && (strncasecmp(name, cp2, cp-cp2) == 0))
				*argv = cp2;
			else if (is_unique(cp2, argv+1, argvp))
				*argvp++ = cp2;
			if (c == ':')
				break;
			/*
			 * Skip multiple delimiters. Reset cp2 to
			 * the beginning of the next name. Reset n,
			 * the flag for names with spaces.
			 */
			while ((c = *cp) == '|')
				cp++;
			cp2 = cp;
			n = 0;
		}
		/*
		 * Skip entries with spaces or non-ascii values.
		 * Convert lower case letters to upper case.
		 */
		if ((c == ' ') || !isascii(c))
			n = 1;
		else if (islower(c))
			*cp = toupper(c);
	}

	/*
	 * Check for an old V6 2 character name.  If the second
	 * name points to the beginning of the buffer, and is
	 * only 2 characters long, move it to the end of the array.
	 */
	if ((argv[1] == buf) && (strlen(argv[1]) == 2)) {
		--argvp;
		for (avt = &argv[1]; avt < argvp; avt++)
			*avt = *(avt+1);
		*argvp++ = buf;
	}

	/*
	 * Duplicate last name, for TTYPE option, and null
	 * terminate the array.  If we didn't find a match on
	 * our terminal name, put that name at the beginning.
	 */
	cp = *(argvp-1);
	*argvp++ = cp;
	*argvp = 0;

	if (*argv == 0) {
		if (name)
			*argv = name;
		else {
			--argvp;
			for (avt = argv; avt < argvp; avt++)
				*avt = *(avt+1);
		}
	}
	if (*argv)
		return((const char **)argv);
	else
		return(unknown);
}

static int
is_unique(char *name, char **as, char **ae)
{
	char **ap;
	int n;

	n = strlen(name) + 1;
	for (ap = as; ap < ae; ap++)
		if (strncasecmp(*ap, name, n) == 0)
			return(0);
	return (1);
}

#ifdef	TERMCAP
char termbuf[1024];

/*ARGSUSED*/
static int
setupterm(char *tname, int fd, int *errp)
{
	if (tgetent(termbuf, tname) == 1) {
		termbuf[1023] = '\0';
		if (errp)
			*errp = 1;
		return(0);
	}
	if (errp)
		*errp = 0;
	return(-1);
}
#else
#define	termbuf	ttytype
extern char ttytype[];
#endif

int resettermname = 1;

static const char *
gettermname(void)
{
	char *tname;
	static const char **tnamep = 0;
	static const char **next;
	int err;

	if (resettermname) {
		resettermname = 0;
		if (tnamep && tnamep != unknown)
			free(tnamep);
		if ((tname = env_getvalue("TERM")) &&
				(setupterm(tname, 1, &err) == 0)) {
			tnamep = mklist(termbuf, tname);
		} else {
			if (tname && (strlen(tname) <= 40)) {
				unknown[0] = tname;
				upcase(tname);
			} else
				unknown[0] = name_unknown;
			tnamep = unknown;
		}
		next = tnamep;
	}
	if (*next == 0)
		next = tnamep;
	return(*next++);
}
/*
 * suboption()
 *
 *	Look at the sub-option buffer, and try to be helpful to the other
 * side.
 *
 *	Currently we recognize:
 *
 *		Terminal type, send request.
 *		Terminal speed (send request).
 *		Local flow control (is request).
 *		Linemode
 */

static void
suboption(void)
{
    unsigned char subchar;

    printsub('<', subbuffer, SB_LEN()+2);
    switch (subchar = SB_GET()) {
    case TELOPT_TTYPE:
	if (my_want_state_is_wont(TELOPT_TTYPE))
	    return;
	if (SB_EOF() || SB_GET() != TELQUAL_SEND) {
	    return;
	} else {
	    const char *name;
	    unsigned char temp[50];
	    int len;

	    name = gettermname();
	    len = strlen(name) + 4 + 2;
	    if (len < NETROOM()) {
		sprintf(temp, "%c%c%c%c%s%c%c", IAC, SB, TELOPT_TTYPE,
				TELQUAL_IS, name, IAC, SE);
		ring_supply_data(&netoring, temp, len);
		printsub('>', &temp[2], len-2);
	    } else {
		ExitString("No room in buffer for terminal type.\n", 1);
		/*NOTREACHED*/
	    }
	}
	break;
    case TELOPT_TSPEED:
	if (my_want_state_is_wont(TELOPT_TSPEED))
	    return;
	if (SB_EOF())
	    return;
	if (SB_GET() == TELQUAL_SEND) {
	    long ospeed, ispeed;
	    unsigned char temp[50];
	    int len;

	    TerminalSpeeds(&ispeed, &ospeed);

	    sprintf((char *)temp, "%c%c%c%c%ld,%ld%c%c", IAC, SB, TELOPT_TSPEED,
		    TELQUAL_IS, ospeed, ispeed, IAC, SE);
	    len = strlen((char *)temp+4) + 4;	/* temp[3] is 0 ... */

	    if (len < NETROOM()) {
		ring_supply_data(&netoring, temp, len);
		printsub('>', temp+2, len - 2);
	    }
/*@*/	    else printf("lm_will: not enough room in buffer\n");
	}
	break;
    case TELOPT_LFLOW:
	if (my_want_state_is_wont(TELOPT_LFLOW))
	    return;
	if (SB_EOF())
	    return;
	switch(SB_GET()) {
	case LFLOW_RESTART_ANY:
	    restartany = 1;
	    break;
	case LFLOW_RESTART_XON:
	    restartany = 0;
	    break;
	case LFLOW_ON:
	    localflow = 1;
	    break;
	case LFLOW_OFF:
	    localflow = 0;
	    break;
	default:
	    return;
	}
	setcommandmode();
	setconnmode(0);
	break;

    case TELOPT_LINEMODE:
	if (my_want_state_is_wont(TELOPT_LINEMODE))
	    return;
	if (SB_EOF())
	    return;
	switch (SB_GET()) {
	case WILL:
	    lm_will(subpointer, SB_LEN());
	    break;
	case WONT:
	    lm_wont(subpointer, SB_LEN());
	    break;
	case DO:
	    lm_do(subpointer, SB_LEN());
	    break;
	case DONT:
	    lm_dont(subpointer, SB_LEN());
	    break;
	case LM_SLC:
	    slc(subpointer, SB_LEN());
	    break;
	case LM_MODE:
	    lm_mode(subpointer, SB_LEN(), 0);
	    break;
	default:
	    break;
	}
	break;

#ifdef	OLD_ENVIRON
    case TELOPT_OLD_ENVIRON:
#endif
    case TELOPT_NEW_ENVIRON:
	if (SB_EOF())
	    return;
	switch(SB_PEEK()) {
	case TELQUAL_IS:
	case TELQUAL_INFO:
	    if (my_want_state_is_dont(subchar))
		return;
	    break;
	case TELQUAL_SEND:
	    if (my_want_state_is_wont(subchar)) {
		return;
	    }
	    break;
	default:
	    return;
	}
	env_opt(subpointer, SB_LEN());
	break;

    case TELOPT_XDISPLOC:
	if (my_want_state_is_wont(TELOPT_XDISPLOC))
	    return;
	if (SB_EOF())
	    return;
	if (SB_GET() == TELQUAL_SEND) {
	    unsigned char temp[50], *dp;
	    int len;

	    if ((dp = env_getvalue("DISPLAY")) == NULL ||
		strlen(dp) > sizeof(temp) - 7) {
		/*
		 * Something happened, we no longer have a DISPLAY
		 * variable.  Or it is too long.  So, turn off the option.
		 */
		send_wont(TELOPT_XDISPLOC, 1);
		break;
	    }
	    snprintf(temp, sizeof(temp), "%c%c%c%c%s%c%c", IAC, SB,
		    TELOPT_XDISPLOC, TELQUAL_IS, dp, IAC, SE);
	    len = strlen((char *)temp+4) + 4;	/* temp[3] is 0 ... */

	    if (len < NETROOM()) {
		ring_supply_data(&netoring, temp, len);
		printsub('>', temp+2, len - 2);
	    }
/*@*/	    else printf("lm_will: not enough room in buffer\n");
	}
	break;

#ifdef	AUTHENTICATION
	case TELOPT_AUTHENTICATION: {
		if (!autologin)
			break;
		if (SB_EOF())
			return;
		switch(SB_GET()) {
		case TELQUAL_IS:
			if (my_want_state_is_dont(TELOPT_AUTHENTICATION))
				return;
			auth_is(subpointer, SB_LEN());
			break;
		case TELQUAL_SEND:
			if (my_want_state_is_wont(TELOPT_AUTHENTICATION))
				return;
			auth_send(subpointer, SB_LEN());
			break;
		case TELQUAL_REPLY:
			if (my_want_state_is_wont(TELOPT_AUTHENTICATION))
				return;
			auth_reply(subpointer, SB_LEN());
			break;
		case TELQUAL_NAME:
			if (my_want_state_is_dont(TELOPT_AUTHENTICATION))
				return;
			auth_name(subpointer, SB_LEN());
			break;
		}
	}
	break;
#endif
#ifdef	ENCRYPTION
	case TELOPT_ENCRYPT:
		if (SB_EOF())
			return;
		switch(SB_GET()) {
		case ENCRYPT_START:
			if (my_want_state_is_dont(TELOPT_ENCRYPT))
				return;
			encrypt_start(subpointer, SB_LEN());
			break;
		case ENCRYPT_END:
			if (my_want_state_is_dont(TELOPT_ENCRYPT))
				return;
			encrypt_end();
			break;
		case ENCRYPT_SUPPORT:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			encrypt_support(subpointer, SB_LEN());
			break;
		case ENCRYPT_REQSTART:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			encrypt_request_start(subpointer, SB_LEN());
			break;
		case ENCRYPT_REQEND:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			/*
			 * We can always send an REQEND so that we cannot
			 * get stuck encrypting.  We should only get this
			 * if we have been able to get in the correct mode
			 * anyhow.
			 */
			encrypt_request_end();
			break;
		case ENCRYPT_IS:
			if (my_want_state_is_dont(TELOPT_ENCRYPT))
				return;
			encrypt_is(subpointer, SB_LEN());
			break;
		case ENCRYPT_REPLY:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			encrypt_reply(subpointer, SB_LEN());
			break;
		case ENCRYPT_ENC_KEYID:
			if (my_want_state_is_dont(TELOPT_ENCRYPT))
				return;
			encrypt_enc_keyid(subpointer, SB_LEN());
			break;
		case ENCRYPT_DEC_KEYID:
			if (my_want_state_is_wont(TELOPT_ENCRYPT))
				return;
			encrypt_dec_keyid(subpointer, SB_LEN());
			break;
		default:
			break;
		}
		break;
#endif	/* ENCRYPTION */
    default:
	break;
    }
}

static unsigned char str_lm[] = { IAC, SB, TELOPT_LINEMODE, 0, 0, IAC, SE };

void
lm_will(unsigned char *cmd, int len)
{
    if (len < 1) {
/*@*/	printf("lm_will: no command!!!\n");	/* Should not happen... */
	return;
    }
    switch(cmd[0]) {
    case LM_FORWARDMASK:	/* We shouldn't ever get this... */
    default:
	str_lm[3] = DONT;
	str_lm[4] = cmd[0];
	if (NETROOM() > (int)sizeof(str_lm)) {
	    ring_supply_data(&netoring, str_lm, sizeof(str_lm));
	    printsub('>', &str_lm[2], sizeof(str_lm)-2);
	}
/*@*/	else printf("lm_will: not enough room in buffer\n");
	break;
    }
}

void
lm_wont(unsigned char *cmd, int len)
{
    if (len < 1) {
/*@*/	printf("lm_wont: no command!!!\n");	/* Should not happen... */
	return;
    }
    switch(cmd[0]) {
    case LM_FORWARDMASK:	/* We shouldn't ever get this... */
    default:
	/* We are always DONT, so don't respond */
	return;
    }
}

void
lm_do(unsigned char *cmd, int len)
{
    if (len < 1) {
/*@*/	printf("lm_do: no command!!!\n");	/* Should not happen... */
	return;
    }
    switch(cmd[0]) {
    case LM_FORWARDMASK:
    default:
	str_lm[3] = WONT;
	str_lm[4] = cmd[0];
	if (NETROOM() > (int)sizeof(str_lm)) {
	    ring_supply_data(&netoring, str_lm, sizeof(str_lm));
	    printsub('>', &str_lm[2], sizeof(str_lm)-2);
	}
/*@*/	else printf("lm_do: not enough room in buffer\n");
	break;
    }
}

void
lm_dont(unsigned char *cmd, int len)
{
    if (len < 1) {
/*@*/	printf("lm_dont: no command!!!\n");	/* Should not happen... */
	return;
    }
    switch(cmd[0]) {
    case LM_FORWARDMASK:
    default:
	/* we are always WONT, so don't respond */
	break;
    }
}

static unsigned char str_lm_mode[] = {
	IAC, SB, TELOPT_LINEMODE, LM_MODE, 0, IAC, SE
};

void
lm_mode(unsigned char *cmd, int len, int init)
{
	if (len != 1)
		return;
	if ((linemode&MODE_MASK&~MODE_ACK) == *cmd)
		return;
	if (*cmd&MODE_ACK)
		return;
	linemode = *cmd&(MODE_MASK&~MODE_ACK);
	str_lm_mode[4] = linemode;
	if (!init)
	    str_lm_mode[4] |= MODE_ACK;
	if (NETROOM() > (int)sizeof(str_lm_mode)) {
	    ring_supply_data(&netoring, str_lm_mode, sizeof(str_lm_mode));
	    printsub('>', &str_lm_mode[2], sizeof(str_lm_mode)-2);
	}
/*@*/	else printf("lm_mode: not enough room in buffer\n");
	setconnmode(0);	/* set changed mode */
}



/*
 * slc()
 * Handle special character suboption of LINEMODE.
 */

struct spc {
	cc_t val;
	cc_t *valp;
	char flags;	/* Current flags & level */
	char mylevel;	/* Maximum level & flags */
} spc_data[NSLC+1];

#define SLC_IMPORT	0
#define	SLC_EXPORT	1
#define SLC_RVALUE	2
static int slc_mode = SLC_EXPORT;

void
slc_init(void)
{
	struct spc *spcp;

	localchars = 1;
	for (spcp = spc_data; spcp < &spc_data[NSLC+1]; spcp++) {
		spcp->val = 0;
		spcp->valp = 0;
		spcp->flags = spcp->mylevel = SLC_NOSUPPORT;
	}

#define	initfunc(func, flags) { \
					spcp = &spc_data[func]; \
					if ((spcp->valp = tcval(func))) { \
					    spcp->val = *spcp->valp; \
					    spcp->mylevel = SLC_VARIABLE|flags; \
					} else { \
					    spcp->val = 0; \
					    spcp->mylevel = SLC_DEFAULT; \
					} \
				    }

	initfunc(SLC_SYNCH, 0);
	/* No BRK */
	initfunc(SLC_AO, 0);
	initfunc(SLC_AYT, 0);
	/* No EOR */
	initfunc(SLC_ABORT, SLC_FLUSHIN|SLC_FLUSHOUT);
	initfunc(SLC_EOF, 0);
#ifndef	SYSV_TERMIO
	initfunc(SLC_SUSP, SLC_FLUSHIN);
#endif
	initfunc(SLC_EC, 0);
	initfunc(SLC_EL, 0);
#ifndef	SYSV_TERMIO
	initfunc(SLC_EW, 0);
	initfunc(SLC_RP, 0);
	initfunc(SLC_LNEXT, 0);
#endif
	initfunc(SLC_XON, 0);
	initfunc(SLC_XOFF, 0);
#ifdef	SYSV_TERMIO
	spc_data[SLC_XON].mylevel = SLC_CANTCHANGE;
	spc_data[SLC_XOFF].mylevel = SLC_CANTCHANGE;
#endif
	initfunc(SLC_FORW1, 0);
#ifdef	USE_TERMIO
	initfunc(SLC_FORW2, 0);
	/* No FORW2 */
#endif

	initfunc(SLC_IP, SLC_FLUSHIN|SLC_FLUSHOUT);
#undef	initfunc

	if (slc_mode == SLC_EXPORT)
		slc_export();
	else
		slc_import(1);

}

void
slcstate(void)
{
    printf("Special characters are %s values\n",
		slc_mode == SLC_IMPORT ? "remote default" :
		slc_mode == SLC_EXPORT ? "local" :
					 "remote");
}

void
slc_mode_export(void)
{
    slc_mode = SLC_EXPORT;
    if (my_state_is_will(TELOPT_LINEMODE))
	slc_export();
}

void
slc_mode_import(int def)
{
    slc_mode = def ? SLC_IMPORT : SLC_RVALUE;
    if (my_state_is_will(TELOPT_LINEMODE))
	slc_import(def);
}

unsigned char slc_import_val[] = {
	IAC, SB, TELOPT_LINEMODE, LM_SLC, 0, SLC_VARIABLE, 0, IAC, SE
};
unsigned char slc_import_def[] = {
	IAC, SB, TELOPT_LINEMODE, LM_SLC, 0, SLC_DEFAULT, 0, IAC, SE
};

void
slc_import(int def)
{
    if (NETROOM() > (int)sizeof(slc_import_val)) {
	if (def) {
	    ring_supply_data(&netoring, slc_import_def, sizeof(slc_import_def));
	    printsub('>', &slc_import_def[2], sizeof(slc_import_def)-2);
	} else {
	    ring_supply_data(&netoring, slc_import_val, sizeof(slc_import_val));
	    printsub('>', &slc_import_val[2], sizeof(slc_import_val)-2);
	}
    }
/*@*/ else printf("slc_import: not enough room\n");
}

void
slc_export(void)
{
    struct spc *spcp;

    TerminalDefaultChars();

    slc_start_reply();
    for (spcp = &spc_data[1]; spcp < &spc_data[NSLC+1]; spcp++) {
	if (spcp->mylevel != SLC_NOSUPPORT) {
	    if (spcp->val == (cc_t)(_POSIX_VDISABLE))
		spcp->flags = SLC_NOSUPPORT;
	    else
		spcp->flags = spcp->mylevel;
	    if (spcp->valp)
		spcp->val = *spcp->valp;
	    slc_add_reply(spcp - spc_data, spcp->flags, spcp->val);
	}
    }
    slc_end_reply();
    (void)slc_update();
    setconnmode(1);	/* Make sure the character values are set */
}

void
slc(unsigned char *cp, int len)
{
	struct spc *spcp;
	int func,level;

	slc_start_reply();

	for (; len >= 3; len -=3, cp +=3) {

		func = cp[SLC_FUNC];

		if (func == 0) {
			/*
			 * Client side: always ignore 0 function.
			 */
			continue;
		}
		if (func > NSLC) {
			if ((cp[SLC_FLAGS] & SLC_LEVELBITS) != SLC_NOSUPPORT)
				slc_add_reply(func, SLC_NOSUPPORT, 0);
			continue;
		}

		spcp = &spc_data[func];

		level = cp[SLC_FLAGS]&(SLC_LEVELBITS|SLC_ACK);

		if ((cp[SLC_VALUE] == (unsigned char)spcp->val) &&
		    ((level&SLC_LEVELBITS) == (spcp->flags&SLC_LEVELBITS))) {
			continue;
		}

		if (level == (SLC_DEFAULT|SLC_ACK)) {
			/*
			 * This is an error condition, the SLC_ACK
			 * bit should never be set for the SLC_DEFAULT
			 * level.  Our best guess to recover is to
			 * ignore the SLC_ACK bit.
			 */
			cp[SLC_FLAGS] &= ~SLC_ACK;
		}

		if (level == ((spcp->flags&SLC_LEVELBITS)|SLC_ACK)) {
			spcp->val = (cc_t)cp[SLC_VALUE];
			spcp->flags = cp[SLC_FLAGS];	/* include SLC_ACK */
			continue;
		}

		level &= ~SLC_ACK;

		if (level <= (spcp->mylevel&SLC_LEVELBITS)) {
			spcp->flags = cp[SLC_FLAGS]|SLC_ACK;
			spcp->val = (cc_t)cp[SLC_VALUE];
		}
		if (level == SLC_DEFAULT) {
			if ((spcp->mylevel&SLC_LEVELBITS) != SLC_DEFAULT)
				spcp->flags = spcp->mylevel;
			else
				spcp->flags = SLC_NOSUPPORT;
		}
		slc_add_reply(func, spcp->flags, spcp->val);
	}
	slc_end_reply();
	if (slc_update())
		setconnmode(1);	/* set the  new character values */
}

void
slc_check(void)
{
    struct spc *spcp;

    slc_start_reply();
    for (spcp = &spc_data[1]; spcp < &spc_data[NSLC+1]; spcp++) {
	if (spcp->valp && spcp->val != *spcp->valp) {
	    spcp->val = *spcp->valp;
	    if (spcp->val == (cc_t)(_POSIX_VDISABLE))
		spcp->flags = SLC_NOSUPPORT;
	    else
		spcp->flags = spcp->mylevel;
	    slc_add_reply(spcp - spc_data, spcp->flags, spcp->val);
	}
    }
    slc_end_reply();
    setconnmode(1);
}

unsigned char slc_reply[128];
unsigned char const * const slc_reply_eom = &slc_reply[sizeof(slc_reply)];
unsigned char *slc_replyp;

void
slc_start_reply(void)
{
	slc_replyp = slc_reply;
	*slc_replyp++ = IAC;
	*slc_replyp++ = SB;
	*slc_replyp++ = TELOPT_LINEMODE;
	*slc_replyp++ = LM_SLC;
}

void
slc_add_reply(unsigned char func, unsigned char flags, cc_t value)
{
	/* A sequence of up to 6 bytes my be written for this member of the SLC
	 * suboption list by this function.  The end of negotiation command,
	 * which is written by slc_end_reply(), will require 2 additional
	 * bytes.  Do not proceed unless there is sufficient space for these
	 * items.
	 */
	if (&slc_replyp[6+2] > slc_reply_eom)
		return;
	if ((*slc_replyp++ = func) == IAC)
		*slc_replyp++ = IAC;
	if ((*slc_replyp++ = flags) == IAC)
		*slc_replyp++ = IAC;
	if ((*slc_replyp++ = (unsigned char)value) == IAC)
		*slc_replyp++ = IAC;
}

void
slc_end_reply(void)
{
    int len;

    /* The end of negotiation command requires 2 bytes. */
    if (&slc_replyp[2] > slc_reply_eom)
            return;
    *slc_replyp++ = IAC;
    *slc_replyp++ = SE;
    len = slc_replyp - slc_reply;
    if (len <= 6)
	return;
    if (NETROOM() > len) {
	ring_supply_data(&netoring, slc_reply, slc_replyp - slc_reply);
	printsub('>', &slc_reply[2], slc_replyp - slc_reply - 2);
    }
/*@*/else printf("slc_end_reply: not enough room\n");
}

int
slc_update(void)
{
	struct spc *spcp;
	int need_update = 0;

	for (spcp = &spc_data[1]; spcp < &spc_data[NSLC+1]; spcp++) {
		if (!(spcp->flags&SLC_ACK))
			continue;
		spcp->flags &= ~SLC_ACK;
		if (spcp->valp && (*spcp->valp != spcp->val)) {
			*spcp->valp = spcp->val;
			need_update = 1;
		}
	}
	return(need_update);
}

#ifdef	OLD_ENVIRON
# ifdef	ENV_HACK
/*
 * Earlier version of telnet/telnetd from the BSD code had
 * the definitions of VALUE and VAR reversed.  To ensure
 * maximum interoperability, we assume that the server is
 * an older BSD server, until proven otherwise.  The newer
 * BSD servers should be able to handle either definition,
 * so it is better to use the wrong values if we don't
 * know what type of server it is.
 */
int env_auto = 1;
int old_env_var = OLD_ENV_VAR;
int old_env_value = OLD_ENV_VALUE;
# else
#  define old_env_var OLD_ENV_VAR
#  define old_env_value OLD_ENV_VALUE
# endif
#endif

void
env_opt(unsigned char *buf, int len)
{
	unsigned char *ep = 0, *epc = 0;
	int i;

	switch(buf[0]&0xff) {
	case TELQUAL_SEND:
		env_opt_start();
		if (len == 1) {
			env_opt_add(NULL);
		} else for (i = 1; i < len; i++) {
			switch (buf[i]&0xff) {
#ifdef	OLD_ENVIRON
			case OLD_ENV_VAR:
# ifdef	ENV_HACK
				if (telopt_environ == TELOPT_OLD_ENVIRON
				    && env_auto) {
					/* Server has the same definitions */
					old_env_var = OLD_ENV_VAR;
					old_env_value = OLD_ENV_VALUE;
				}
				/* FALLTHROUGH */
# endif
			case OLD_ENV_VALUE:
				/*
				 * Although OLD_ENV_VALUE is not legal, we will
				 * still recognize it, just in case it is an
				 * old server that has VAR & VALUE mixed up...
				 */
				/* FALLTHROUGH */
#else
			case NEW_ENV_VAR:
#endif
			case ENV_USERVAR:
				if (ep) {
					*epc = 0;
					env_opt_add(ep);
				}
				ep = epc = &buf[i+1];
				break;
			case ENV_ESC:
				i++;
				/*FALLTHROUGH*/
			default:
				if (epc)
					*epc++ = buf[i];
				break;
			}
		}
		if (ep) {
			*epc = 0;
			env_opt_add(ep);
		}
		env_opt_end(1);
		break;

	case TELQUAL_IS:
	case TELQUAL_INFO:
		/* Ignore for now.  We shouldn't get it anyway. */
		break;

	default:
		break;
	}
}

#define	OPT_REPLY_SIZE	(2 * SUBBUFSIZE)
unsigned char *opt_reply = NULL;
unsigned char *opt_replyp;
unsigned char *opt_replyend;

void
env_opt_start(void)
{
	if (opt_reply)
		opt_reply = (unsigned char *)realloc(opt_reply, OPT_REPLY_SIZE);
	else
		opt_reply = (unsigned char *)malloc(OPT_REPLY_SIZE);
	if (opt_reply == NULL) {
/*@*/		printf("env_opt_start: malloc()/realloc() failed!!!\n");
		opt_reply = opt_replyp = opt_replyend = NULL;
		return;
	}
	opt_replyp = opt_reply;
	opt_replyend = opt_reply + OPT_REPLY_SIZE;
	*opt_replyp++ = IAC;
	*opt_replyp++ = SB;
	*opt_replyp++ = telopt_environ;
	*opt_replyp++ = TELQUAL_IS;
}

void
env_opt_start_info(void)
{
	env_opt_start();
	if (opt_replyp)
	    opt_replyp[-1] = TELQUAL_INFO;
}

void
env_opt_add(unsigned char *ep)
{
	unsigned char *vp, c;

	if (opt_reply == NULL)		/*XXX*/
		return;			/*XXX*/

	if (ep == NULL || *ep == '\0') {
		/* Send user defined variables first. */
		env_default(1, 0);
		while ((ep = env_default(0, 0)))
			env_opt_add(ep);

		/* Now add the list of well know variables.  */
		env_default(1, 1);
		while ((ep = env_default(0, 1)))
			env_opt_add(ep);
		return;
	}
	vp = env_getvalue(ep);
        if (opt_replyp + (vp ? 2 * strlen((char *)vp) : 0) +
                                2 * strlen((char *)ep) + 6 > opt_replyend)
        {
		int len;
		opt_replyend += OPT_REPLY_SIZE;
		len = opt_replyend - opt_reply;
		opt_reply = (unsigned char *)realloc(opt_reply, len);
		if (opt_reply == NULL) {
/*@*/			printf("env_opt_add: realloc() failed!!!\n");
			opt_reply = opt_replyp = opt_replyend = NULL;
			return;
		}
		opt_replyp = opt_reply + len - (opt_replyend - opt_replyp);
		opt_replyend = opt_reply + len;
	}
	if (opt_welldefined(ep))
#ifdef	OLD_ENVIRON
		if (telopt_environ == TELOPT_OLD_ENVIRON)
			*opt_replyp++ = old_env_var;
		else
#endif
			*opt_replyp++ = NEW_ENV_VAR;
	else
		*opt_replyp++ = ENV_USERVAR;
	for (;;) {
		while ((c = *ep++)) {
			if (opt_replyp + (2 + 2) > opt_replyend)
				return;
			switch(c&0xff) {
			case IAC:
				*opt_replyp++ = IAC;
				break;
			case NEW_ENV_VAR:
			case NEW_ENV_VALUE:
			case ENV_ESC:
			case ENV_USERVAR:
				*opt_replyp++ = ENV_ESC;
				break;
			}
			*opt_replyp++ = c;
		}
		if ((ep = vp)) {
			if (opt_replyp + (1 + 2 + 2) > opt_replyend)
				return;
#ifdef	OLD_ENVIRON
			if (telopt_environ == TELOPT_OLD_ENVIRON)
				*opt_replyp++ = old_env_value;
			else
#endif
				*opt_replyp++ = NEW_ENV_VALUE;
			vp = NULL;
		} else
			break;
	}
}

int
opt_welldefined(const char *ep)
{
	if ((strcmp(ep, "USER") == 0) ||
	    (strcmp(ep, "DISPLAY") == 0) ||
	    (strcmp(ep, "PRINTER") == 0) ||
	    (strcmp(ep, "SYSTEMTYPE") == 0) ||
	    (strcmp(ep, "JOB") == 0) ||
	    (strcmp(ep, "ACCT") == 0))
		return(1);
	return(0);
}

void
env_opt_end(int emptyok)
{
	int len;

	if (opt_replyp + 2 > opt_replyend)
		return;
	len = opt_replyp + 2 - opt_reply;
	if (emptyok || len > 6) {
		*opt_replyp++ = IAC;
		*opt_replyp++ = SE;
		if (NETROOM() > len) {
			ring_supply_data(&netoring, opt_reply, len);
			printsub('>', &opt_reply[2], len - 2);
		}
/*@*/		else printf("slc_end_reply: not enough room\n");
	}
	if (opt_reply) {
		free(opt_reply);
		opt_reply = opt_replyp = opt_replyend = NULL;
	}
}



int
telrcv(void)
{
    int c;
    int scc;
    unsigned char *sbp;
    int count;
    int returnValue = 0;

    scc = 0;
    count = 0;
    while (TTYROOM() > 2) {
	if (scc == 0) {
	    if (count) {
		ring_consumed(&netiring, count);
		returnValue = 1;
		count = 0;
	    }
	    sbp = netiring.consume;
	    scc = ring_full_consecutive(&netiring);
	    if (scc == 0) {
		/* No more data coming in */
		break;
	    }
	}

	c = *sbp++ & 0xff, scc--; count++;
#ifdef	ENCRYPTION
	if (decrypt_input)
		c = (*decrypt_input)(c);
#endif	/* ENCRYPTION */

	switch (telrcv_state) {

	case TS_CR:
	    telrcv_state = TS_DATA;
	    if (c == '\0') {
		break;	/* Ignore \0 after CR */
	    }
	    else if ((c == '\n') && my_want_state_is_dont(TELOPT_ECHO) && !crmod) {
		TTYADD(c);
		break;
	    }
	    /* FALLTHROUGH */

	case TS_DATA:
	    if (c == IAC && telnetport >= 0) {
		telrcv_state = TS_IAC;
		break;
	    }
		    /*
		     * The 'crmod' hack (see following) is needed
		     * since we can't * set CRMOD on output only.
		     * Machines like MULTICS like to send \r without
		     * \n; since we must turn off CRMOD to get proper
		     * input, the mapping is done here (sigh).
		     */
	    if ((c == '\r') && my_want_state_is_dont(TELOPT_BINARY)) {
		if (scc > 0) {
		    c = *sbp&0xff;
#ifdef	ENCRYPTION
		    if (decrypt_input)
			c = (*decrypt_input)(c);
#endif	/* ENCRYPTION */
		    if (c == 0) {
			sbp++, scc--; count++;
			/* a "true" CR */
			TTYADD('\r');
		    } else if (my_want_state_is_dont(TELOPT_ECHO) &&
					(c == '\n')) {
			sbp++, scc--; count++;
			TTYADD('\n');
		    } else {
#ifdef	ENCRYPTION
			if (decrypt_input)
			    (*decrypt_input)(-1);
#endif	/* ENCRYPTION */

			TTYADD('\r');
			if (crmod) {
				TTYADD('\n');
			}
		    }
		} else {
		    telrcv_state = TS_CR;
		    TTYADD('\r');
		    if (crmod) {
			    TTYADD('\n');
		    }
		}
	    } else {
		TTYADD(c);
	    }
	    continue;

	case TS_IAC:
process_iac:
	    switch (c) {

	    case WILL:
		telrcv_state = TS_WILL;
		continue;

	    case WONT:
		telrcv_state = TS_WONT;
		continue;

	    case DO:
		telrcv_state = TS_DO;
		continue;

	    case DONT:
		telrcv_state = TS_DONT;
		continue;

	    case DM:
		    /*
		     * We may have missed an urgent notification,
		     * so make sure we flush whatever is in the
		     * buffer currently.
		     */
		printoption("RCVD", IAC, DM);
		SYNCHing = 1;
		(void) ttyflush(1);
		SYNCHing = stilloob();
		settimer(gotDM);
		break;

	    case SB:
		SB_CLEAR();
		telrcv_state = TS_SB;
		continue;

	    case IAC:
		TTYADD(IAC);
		break;

	    case NOP:
	    case GA:
	    default:
		printoption("RCVD", IAC, c);
		break;
	    }
	    telrcv_state = TS_DATA;
	    continue;

	case TS_WILL:
	    printoption("RCVD", WILL, c);
	    willoption(c);
	    telrcv_state = TS_DATA;
	    continue;

	case TS_WONT:
	    printoption("RCVD", WONT, c);
	    wontoption(c);
	    telrcv_state = TS_DATA;
	    continue;

	case TS_DO:
	    printoption("RCVD", DO, c);
	    dooption(c);
	    if (c == TELOPT_NAWS) {
		sendnaws();
	    } else if (c == TELOPT_LFLOW) {
		localflow = 1;
		setcommandmode();
		setconnmode(0);
	    }
	    telrcv_state = TS_DATA;
	    continue;

	case TS_DONT:
	    printoption("RCVD", DONT, c);
	    dontoption(c);
	    flushline = 1;
	    setconnmode(0);	/* set new tty mode (maybe) */
	    telrcv_state = TS_DATA;
	    continue;

	case TS_SB:
	    if (c == IAC) {
		telrcv_state = TS_SE;
	    } else {
		SB_ACCUM(c);
	    }
	    continue;

	case TS_SE:
	    if (c != SE) {
		if (c != IAC) {
		    /*
		     * This is an error.  We only expect to get
		     * "IAC IAC" or "IAC SE".  Several things may
		     * have happend.  An IAC was not doubled, the
		     * IAC SE was left off, or another option got
		     * inserted into the suboption are all possibilities.
		     * If we assume that the IAC was not doubled,
		     * and really the IAC SE was left off, we could
		     * get into an infinate loop here.  So, instead,
		     * we terminate the suboption, and process the
		     * partial suboption if we can.
		     */
		    SB_ACCUM(IAC);
		    SB_ACCUM(c);
		    subpointer -= 2;
		    SB_TERM();

		    printoption("In SUBOPTION processing, RCVD", IAC, c);
		    suboption();	/* handle sub-option */
		    telrcv_state = TS_IAC;
		    goto process_iac;
		}
		SB_ACCUM(c);
		telrcv_state = TS_SB;
	    } else {
		SB_ACCUM(IAC);
		SB_ACCUM(SE);
		subpointer -= 2;
		SB_TERM();
		suboption();	/* handle sub-option */
		telrcv_state = TS_DATA;
	    }
	}
    }
    if (count)
	ring_consumed(&netiring, count);
    return returnValue||count;
}

static int bol = 1, local = 0;

int
rlogin_susp(void)
{
    if (local) {
	local = 0;
	bol = 1;
	command(0, "z\n", 2);
	return(1);
    }
    return(0);
}

static int
telsnd(void)
{
    int tcc;
    int count;
    int returnValue = 0;
    unsigned char *tbp;

    tcc = 0;
    count = 0;
    while (NETROOM() > 2) {
	int sc;
	int c;

	if (tcc == 0) {
	    if (count) {
		ring_consumed(&ttyiring, count);
		returnValue = 1;
		count = 0;
	    }
	    tbp = ttyiring.consume;
	    tcc = ring_full_consecutive(&ttyiring);
	    if (tcc == 0) {
		break;
	    }
	}
	c = *tbp++ & 0xff, sc = strip(c), tcc--; count++;
	if (rlogin != _POSIX_VDISABLE) {
		if (bol) {
			bol = 0;
			if (sc == rlogin) {
				local = 1;
				continue;
			}
		} else if (local) {
			local = 0;
			if (sc == '.' || c == termEofChar) {
				bol = 1;
				command(0, "close\n", 6);
				continue;
			}
			if (sc == termSuspChar) {
				bol = 1;
				command(0, "z\n", 2);
				continue;
			}
			if (sc == escape) {
				command(0, tbp, tcc);
				bol = 1;
				count += tcc;
				tcc = 0;
				flushline = 1;
				break;
			}
			if (sc != rlogin) {
				++tcc;
				--tbp;
				--count;
				c = sc = rlogin;
			}
		}
		if ((sc == '\n') || (sc == '\r'))
			bol = 1;
	} else if (escape != _POSIX_VDISABLE && sc == escape) {
	    /*
	     * Double escape is a pass through of a single escape character.
	     */
	    if (tcc && strip(*tbp) == escape) {
		tbp++;
		tcc--;
		count++;
		bol = 0;
	    } else {
		command(0, (char *)tbp, tcc);
		bol = 1;
		count += tcc;
		tcc = 0;
		flushline = 1;
		break;
	    }
	} else
	    bol = 0;
#ifdef	KLUDGELINEMODE
	if (kludgelinemode && (globalmode&MODE_EDIT) && (sc == echoc)) {
	    if (tcc > 0 && strip(*tbp) == echoc) {
		tcc--; tbp++; count++;
	    } else {
		dontlecho = !dontlecho;
		settimer(echotoggle);
		setconnmode(0);
		flushline = 1;
		break;
	    }
	}
#endif
	if (MODE_LOCAL_CHARS(globalmode)) {
	    if (TerminalSpecialChars(sc) == 0) {
		bol = 1;
		break;
	    }
	}
	if (my_want_state_is_wont(TELOPT_BINARY)) {
	    switch (c) {
	    case '\n':
		    /*
		     * If we are in CRMOD mode (\r ==> \n)
		     * on our local machine, then probably
		     * a newline (unix) is CRLF (TELNET).
		     */
		if (MODE_LOCAL_CHARS(globalmode)) {
		    NETADD('\r');
		}
		NETADD('\n');
		bol = flushline = 1;
		break;
	    case '\r':
		if (!crlf) {
		    NET2ADD('\r', '\0');
		} else {
		    NET2ADD('\r', '\n');
		}
		bol = flushline = 1;
		break;
	    case IAC:
		NET2ADD(IAC, IAC);
		break;
	    default:
		NETADD(c);
		break;
	    }
	} else if (c == IAC) {
	    NET2ADD(IAC, IAC);
	} else {
	    NETADD(c);
	}
    }
    if (count)
	ring_consumed(&ttyiring, count);
    return returnValue||count;		/* Non-zero if we did anything */
}

/*
 * Scheduler()
 *
 * Try to do something.
 *
 * If we do something useful, return 1; else return 0.
 *
 */

static int
Scheduler(int block)
{
		/* One wants to be a bit careful about setting returnValue
		 * to one, since a one implies we did some useful work,
		 * and therefore probably won't be called to block next
		 */
    int returnValue;
    int netin, netout, netex, ttyin, ttyout;

    /* Decide which rings should be processed */

    netout = ring_full_count(&netoring) &&
	    (flushline ||
		(my_want_state_is_wont(TELOPT_LINEMODE)
#ifdef	KLUDGELINEMODE
			&& (!kludgelinemode || my_want_state_is_do(TELOPT_SGA))
#endif
		) ||
			my_want_state_is_will(TELOPT_BINARY));
    ttyout = ring_full_count(&ttyoring);

    ttyin = ring_empty_count(&ttyiring) && (clienteof == 0);

    netin = !ISend && ring_empty_count(&netiring);

    netex = !SYNCHing;

    /* Call to system code to process rings */

    returnValue = process_rings(netin, netout, netex, ttyin, ttyout, !block);

    /* Now, look at the input rings, looking for work to do. */

    if (ring_full_count(&ttyiring)) {
	    returnValue |= telsnd();
    }

    if (ring_full_count(&netiring)) {
	returnValue |= telrcv();
    }
    return returnValue;
}

#ifdef	AUTHENTICATION
#define __unusedhere
#else
#define __unusedhere __unused
#endif
/*
 * Select from tty and network...
 */
void
telnet(char *user __unusedhere)
{
    sys_telnet_init();

#ifdef	AUTHENTICATION
#ifdef	ENCRYPTION
    {
	static char local_host[256] = { 0 };

	if (!local_host[0]) {
		gethostname(local_host, sizeof(local_host));
		local_host[sizeof(local_host)-1] = 0;
	}
	auth_encrypt_init(local_host, hostname, "TELNET", 0);
	auth_encrypt_user(user);
    }
#endif
#endif
    if (telnetport > 0) {
#ifdef	AUTHENTICATION
	if (autologin)
		send_will(TELOPT_AUTHENTICATION, 1);
#endif
#ifdef	ENCRYPTION
	send_do(TELOPT_ENCRYPT, 1);
	send_will(TELOPT_ENCRYPT, 1);
#endif	/* ENCRYPTION */
	send_do(TELOPT_SGA, 1);
	send_will(TELOPT_TTYPE, 1);
	send_will(TELOPT_NAWS, 1);
	send_will(TELOPT_TSPEED, 1);
	send_will(TELOPT_LFLOW, 1);
	send_will(TELOPT_LINEMODE, 1);
	send_will(TELOPT_NEW_ENVIRON, 1);
	send_do(TELOPT_STATUS, 1);
	if (env_getvalue("DISPLAY"))
	    send_will(TELOPT_XDISPLOC, 1);
	if (eight)
	    tel_enter_binary(eight);
    }

    for (;;) {
	int schedValue;

	while ((schedValue = Scheduler(0)) != 0) {
	    if (schedValue == -1) {
		setcommandmode();
		return;
	    }
	}

	if (Scheduler(1) == -1) {
	    setcommandmode();
	    return;
	}
    }
}

#if	0	/* XXX - this not being in is a bug */
/*
 * nextitem()
 *
 *	Return the address of the next "item" in the TELNET data
 * stream.  This will be the address of the next character if
 * the current address is a user data character, or it will
 * be the address of the character following the TELNET command
 * if the current address is a TELNET IAC ("I Am a Command")
 * character.
 */

static char *
nextitem(char *current)
{
    if ((*current&0xff) != IAC) {
	return current+1;
    }
    switch (*(current+1)&0xff) {
    case DO:
    case DONT:
    case WILL:
    case WONT:
	return current+3;
    case SB:		/* loop forever looking for the SE */
	{
	    char *look = current+2;

	    for (;;) {
		if ((*look++&0xff) == IAC) {
		    if ((*look++&0xff) == SE) {
			return look;
		    }
		}
	    }
	}
    default:
	return current+2;
    }
}
#endif	/* 0 */

/*
 * netclear()
 *
 *	We are about to do a TELNET SYNCH operation.  Clear
 * the path to the network.
 *
 *	Things are a bit tricky since we may have sent the first
 * byte or so of a previous TELNET command into the network.
 * So, we have to scan the network buffer from the beginning
 * until we are up to where we want to be.
 *
 *	A side effect of what we do, just to keep things
 * simple, is to clear the urgent data pointer.  The principal
 * caller should be setting the urgent data pointer AFTER calling
 * us in any case.
 */

static void
netclear(void)
{
	/* Deleted */
}

/*
 * These routines add various telnet commands to the data stream.
 */

static void
doflush(void)
{
    NET2ADD(IAC, DO);
    NETADD(TELOPT_TM);
    flushline = 1;
    flushout = 1;
    (void) ttyflush(1);			/* Flush/drop output */
    /* do printoption AFTER flush, otherwise the output gets tossed... */
    printoption("SENT", DO, TELOPT_TM);
}

void
xmitAO(void)
{
    NET2ADD(IAC, AO);
    printoption("SENT", IAC, AO);
    if (autoflush) {
	doflush();
    }
}

void
xmitEL(void)
{
    NET2ADD(IAC, EL);
    printoption("SENT", IAC, EL);
}

void
xmitEC(void)
{
    NET2ADD(IAC, EC);
    printoption("SENT", IAC, EC);
}

int
dosynch(char *ch __unused)
{
    netclear();			/* clear the path to the network */
    NETADD(IAC);
    setneturg();
    NETADD(DM);
    printoption("SENT", IAC, DM);
    return 1;
}

int want_status_response = 0;

int
get_status(char *ch __unused)
{
    unsigned char tmp[16];
    unsigned char *cp;

    if (my_want_state_is_dont(TELOPT_STATUS)) {
	printf("Remote side does not support STATUS option\n");
	return 0;
    }
    cp = tmp;

    *cp++ = IAC;
    *cp++ = SB;
    *cp++ = TELOPT_STATUS;
    *cp++ = TELQUAL_SEND;
    *cp++ = IAC;
    *cp++ = SE;
    if (NETROOM() >= cp - tmp) {
	ring_supply_data(&netoring, tmp, cp-tmp);
	printsub('>', tmp+2, cp - tmp - 2);
    }
    ++want_status_response;
    return 1;
}

void
intp(void)
{
    NET2ADD(IAC, IP);
    printoption("SENT", IAC, IP);
    flushline = 1;
    if (autoflush) {
	doflush();
    }
    if (autosynch) {
	dosynch(NULL);
    }
}

void
sendbrk(void)
{
    NET2ADD(IAC, BREAK);
    printoption("SENT", IAC, BREAK);
    flushline = 1;
    if (autoflush) {
	doflush();
    }
    if (autosynch) {
	dosynch(NULL);
    }
}

void
sendabort(void)
{
    NET2ADD(IAC, ABORT);
    printoption("SENT", IAC, ABORT);
    flushline = 1;
    if (autoflush) {
	doflush();
    }
    if (autosynch) {
	dosynch(NULL);
    }
}

void
sendsusp(void)
{
    NET2ADD(IAC, SUSP);
    printoption("SENT", IAC, SUSP);
    flushline = 1;
    if (autoflush) {
	doflush();
    }
    if (autosynch) {
	dosynch(NULL);
    }
}

void
sendeof(void)
{
    NET2ADD(IAC, xEOF);
    printoption("SENT", IAC, xEOF);
}

void
sendayt(void)
{
    NET2ADD(IAC, AYT);
    printoption("SENT", IAC, AYT);
}

/*
 * Send a window size update to the remote system.
 */

void
sendnaws(void)
{
    long rows, cols;
    unsigned char tmp[16];
    unsigned char *cp;

    if (my_state_is_wont(TELOPT_NAWS))
	return;

#define	PUTSHORT(cp, x) { if ((*cp++ = ((x)>>8)&0xff) == IAC) *cp++ = IAC; \
			    if ((*cp++ = ((x))&0xff) == IAC) *cp++ = IAC; }

    if (TerminalWindowSize(&rows, &cols) == 0) {	/* Failed */
	return;
    }

    cp = tmp;

    *cp++ = IAC;
    *cp++ = SB;
    *cp++ = TELOPT_NAWS;
    PUTSHORT(cp, cols);
    PUTSHORT(cp, rows);
    *cp++ = IAC;
    *cp++ = SE;
    if (NETROOM() >= cp - tmp) {
	ring_supply_data(&netoring, tmp, cp-tmp);
	printsub('>', tmp+2, cp - tmp - 2);
    }
}

void
tel_enter_binary(int rw)
{
    if (rw&1)
	send_do(TELOPT_BINARY, 1);
    if (rw&2)
	send_will(TELOPT_BINARY, 1);
}

void
tel_leave_binary(int rw)
{
    if (rw&1)
	send_dont(TELOPT_BINARY, 1);
    if (rw&2)
	send_wont(TELOPT_BINARY, 1);
}
