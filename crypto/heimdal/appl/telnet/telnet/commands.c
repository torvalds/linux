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

#include "telnet_locl.h"

RCSID("$Id$");

#if	defined(IPPROTO_IP) && defined(IP_TOS)
int tos = -1;
#endif	/* defined(IPPROTO_IP) && defined(IP_TOS) */

char	*hostname;
static char _hostname[MaxHostNameLen];

typedef int (*intrtn_t)(int, char**);
static int call(intrtn_t, ...);

typedef struct {
	char	*name;		/* command name */
	char	*help;		/* help string (NULL for no help) */
	int	(*handler)();	/* routine which executes command */
	int	needconnect;	/* Do we need to be connected to execute? */
} Command;

static char line[256];
static char saveline[256];
static int margc;
static char *margv[20];

static void
makeargv()
{
    char *cp, *cp2, c;
    char **argp = margv;

    margc = 0;
    cp = line;
    if (*cp == '!') {		/* Special case shell escape */
	/* save for shell command */
	strlcpy(saveline, line, sizeof(saveline));
	*argp++ = "!";		/* No room in string to get this */
	margc++;
	cp++;
    }
    while ((c = *cp)) {
	int inquote = 0;
	while (isspace((unsigned char)c))
	    c = *++cp;
	if (c == '\0')
	    break;
	*argp++ = cp;
	margc += 1;
	for (cp2 = cp; c != '\0'; c = *++cp) {
	    if (inquote) {
		if (c == inquote) {
		    inquote = 0;
		    continue;
		}
	    } else {
		if (c == '\\') {
		    if ((c = *++cp) == '\0')
			break;
		} else if (c == '"') {
		    inquote = '"';
		    continue;
		} else if (c == '\'') {
		    inquote = '\'';
		    continue;
		} else if (isspace((unsigned char)c))
		    break;
	    }
	    *cp2++ = c;
	}
	*cp2 = '\0';
	if (c == '\0')
	    break;
	cp++;
    }
    *argp++ = 0;
}

/*
 * Make a character string into a number.
 *
 * Todo:  1.  Could take random integers (12, 0x12, 012, 0b1).
 */

static char
special(char *s)
{
	char c;
	char b;

	switch (*s) {
	case '^':
		b = *++s;
		if (b == '?') {
		    c = b | 0x40;		/* DEL */
		} else {
		    c = b & 0x1f;
		}
		break;
	default:
		c = *s;
		break;
	}
	return c;
}

/*
 * Construct a control character sequence
 * for a special character.
 */
static char *
control(cc_t c)
{
	static char buf[5];
	/*
	 * The only way I could get the Sun 3.5 compiler
	 * to shut up about
	 *	if ((unsigned int)c >= 0x80)
	 * was to assign "c" to an unsigned int variable...
	 * Arggg....
	 */
	unsigned int uic = (unsigned int)c;

	if (uic == 0x7f)
		return ("^?");
	if (c == (cc_t)_POSIX_VDISABLE) {
		return "off";
	}
	if (uic >= 0x80) {
		buf[0] = '\\';
		buf[1] = ((c>>6)&07) + '0';
		buf[2] = ((c>>3)&07) + '0';
		buf[3] = (c&07) + '0';
		buf[4] = 0;
	} else if (uic >= 0x20) {
		buf[0] = c;
		buf[1] = 0;
	} else {
		buf[0] = '^';
		buf[1] = '@'+c;
		buf[2] = 0;
	}
	return (buf);
}



/*
 *	The following are data structures and routines for
 *	the "send" command.
 *
 */

struct sendlist {
    char	*name;		/* How user refers to it (case independent) */
    char	*help;		/* Help information (0 ==> no help) */
    int		needconnect;	/* Need to be connected */
    int		narg;		/* Number of arguments */
    int		(*handler)();	/* Routine to perform (for special ops) */
    int		nbyte;		/* Number of bytes to send this command */
    int		what;		/* Character to be sent (<0 ==> special) */
};


static int
	send_esc (void),
	send_help (void),
	send_docmd (char *),
	send_dontcmd (char *),
	send_willcmd (char *),
	send_wontcmd (char *);

static struct sendlist Sendlist[] = {
    { "ao",	"Send Telnet Abort output",		1, 0, 0, 2, AO },
    { "ayt",	"Send Telnet 'Are You There'",		1, 0, 0, 2, AYT },
    { "brk",	"Send Telnet Break",			1, 0, 0, 2, BREAK },
    { "break",	0,					1, 0, 0, 2, BREAK },
    { "ec",	"Send Telnet Erase Character",		1, 0, 0, 2, EC },
    { "el",	"Send Telnet Erase Line",		1, 0, 0, 2, EL },
    { "escape",	"Send current escape character",	1, 0, send_esc, 1, 0 },
    { "ga",	"Send Telnet 'Go Ahead' sequence",	1, 0, 0, 2, GA },
    { "ip",	"Send Telnet Interrupt Process",	1, 0, 0, 2, IP },
    { "intp",	0,					1, 0, 0, 2, IP },
    { "interrupt", 0,					1, 0, 0, 2, IP },
    { "intr",	0,					1, 0, 0, 2, IP },
    { "nop",	"Send Telnet 'No operation'",		1, 0, 0, 2, NOP },
    { "eor",	"Send Telnet 'End of Record'",		1, 0, 0, 2, EOR },
    { "abort",	"Send Telnet 'Abort Process'",		1, 0, 0, 2, ABORT },
    { "susp",	"Send Telnet 'Suspend Process'",	1, 0, 0, 2, SUSP },
    { "eof",	"Send Telnet End of File Character",	1, 0, 0, 2, xEOF },
    { "synch",	"Perform Telnet 'Synch operation'",	1, 0, dosynch, 2, 0 },
    { "getstatus", "Send request for STATUS",		1, 0, get_status, 6, 0 },
    { "?",	"Display send options",			0, 0, send_help, 0, 0 },
    { "help",	0,					0, 0, send_help, 0, 0 },
    { "do",	0,					0, 1, send_docmd, 3, 0 },
    { "dont",	0,					0, 1, send_dontcmd, 3, 0 },
    { "will",	0,					0, 1, send_willcmd, 3, 0 },
    { "wont",	0,					0, 1, send_wontcmd, 3, 0 },
    { 0 }
};

#define	GETSEND(name) ((struct sendlist *) genget(name, (char **) Sendlist, \
				sizeof(struct sendlist)))

static int
sendcmd(int argc, char **argv)
{
    int count;		/* how many bytes we are going to need to send */
    int i;
    struct sendlist *s;	/* pointer to current command */
    int success = 0;
    int needconnect = 0;

    if (argc < 2) {
	printf("need at least one argument for 'send' command\r\n");
	printf("'send ?' for help\r\n");
	return 0;
    }
    /*
     * First, validate all the send arguments.
     * In addition, we see how much space we are going to need, and
     * whether or not we will be doing a "SYNCH" operation (which
     * flushes the network queue).
     */
    count = 0;
    for (i = 1; i < argc; i++) {
	s = GETSEND(argv[i]);
	if (s == 0) {
	    printf("Unknown send argument '%s'\r\n'send ?' for help.\r\n",
			argv[i]);
	    return 0;
	} else if (Ambiguous(s)) {
	    printf("Ambiguous send argument '%s'\r\n'send ?' for help.\r\n",
			argv[i]);
	    return 0;
	}
	if (i + s->narg >= argc) {
	    fprintf(stderr,
	    "Need %d argument%s to 'send %s' command.  'send %s ?' for help.\r\n",
		s->narg, s->narg == 1 ? "" : "s", s->name, s->name);
	    return 0;
	}
	count += s->nbyte;
	if (s->handler == send_help) {
	    send_help();
	    return 0;
	}

	i += s->narg;
	needconnect += s->needconnect;
    }
    if (!connected && needconnect) {
	printf("?Need to be connected first.\r\n");
	printf("'send ?' for help\r\n");
	return 0;
    }
    /* Now, do we have enough room? */
    if (NETROOM() < count) {
	printf("There is not enough room in the buffer TO the network\r\n");
	printf("to process your request.  Nothing will be done.\r\n");
	printf("('send synch' will throw away most data in the network\r\n");
	printf("buffer, if this might help.)\r\n");
	return 0;
    }
    /* OK, they are all OK, now go through again and actually send */
    count = 0;
    for (i = 1; i < argc; i++) {
	if ((s = GETSEND(argv[i])) == 0) {
	    fprintf(stderr, "Telnet 'send' error - argument disappeared!\r\n");
	    quit();
	    /*NOTREACHED*/
	}
	if (s->handler) {
	    count++;
	    success += (*s->handler)((s->narg > 0) ? argv[i+1] : 0,
				  (s->narg > 1) ? argv[i+2] : 0);
	    i += s->narg;
	} else {
	    NET2ADD(IAC, s->what);
	    printoption("SENT", IAC, s->what);
	}
    }
    return (count == success);
}

static int
send_tncmd(void (*func)(), char *cmd, char *name);

static int
send_esc()
{
    NETADD(escape);
    return 1;
}

static int
send_docmd(char *name)
{
    return(send_tncmd(send_do, "do", name));
}

static int
send_dontcmd(char *name)
{
    return(send_tncmd(send_dont, "dont", name));
}

static int
send_willcmd(char *name)
{
    return(send_tncmd(send_will, "will", name));
}

static int
send_wontcmd(char *name)
{
    return(send_tncmd(send_wont, "wont", name));
}

extern char *telopts[];		/* XXX */

static int
send_tncmd(void (*func)(), char *cmd, char *name)
{
    char **cpp;
    int val = 0;

    if (isprefix(name, "help") || isprefix(name, "?")) {
	int col, len;

	printf("Usage: send %s <value|option>\r\n", cmd);
	printf("\"value\" must be from 0 to 255\r\n");
	printf("Valid options are:\r\n\t");

	col = 8;
	for (cpp = telopts; *cpp; cpp++) {
	    len = strlen(*cpp) + 3;
	    if (col + len > 65) {
		printf("\r\n\t");
		col = 8;
	    }
	    printf(" \"%s\"", *cpp);
	    col += len;
	}
	printf("\r\n");
	return 0;
    }
    cpp = genget(name, telopts, sizeof(char *));
    if (Ambiguous(cpp)) {
	fprintf(stderr,"'%s': ambiguous argument ('send %s ?' for help).\r\n",
					name, cmd);
	return 0;
    }
    if (cpp) {
	val = cpp - telopts;
    } else {
	char *cp = name;

	while (*cp >= '0' && *cp <= '9') {
	    val *= 10;
	    val += *cp - '0';
	    cp++;
	}
	if (*cp != 0) {
	    fprintf(stderr, "'%s': unknown argument ('send %s ?' for help).\r\n",
					name, cmd);
	    return 0;
	} else if (val < 0 || val > 255) {
	    fprintf(stderr, "'%s': bad value ('send %s ?' for help).\r\n",
					name, cmd);
	    return 0;
	}
    }
    if (!connected) {
	printf("?Need to be connected first.\r\n");
	return 0;
    }
    (*func)(val, 1);
    return 1;
}

static int
send_help()
{
    struct sendlist *s;	/* pointer to current command */
    for (s = Sendlist; s->name; s++) {
	if (s->help)
	    printf("%-15s %s\r\n", s->name, s->help);
    }
    return(0);
}

/*
 * The following are the routines and data structures referred
 * to by the arguments to the "toggle" command.
 */

static int
lclchars()
{
    donelclchars = 1;
    return 1;
}

static int
togdebug()
{
#ifndef	NOT43
    if (net > 0 &&
	(SetSockOpt(net, SOL_SOCKET, SO_DEBUG, debug)) < 0) {
	    perror("setsockopt (SO_DEBUG)");
    }
#else	/* NOT43 */
    if (debug) {
	if (net > 0 && SetSockOpt(net, SOL_SOCKET, SO_DEBUG, 0, 0) < 0)
	    perror("setsockopt (SO_DEBUG)");
    } else
	printf("Cannot turn off socket debugging\r\n");
#endif	/* NOT43 */
    return 1;
}

static int
togcrlf()
{
    if (crlf) {
	printf("Will send carriage returns as telnet <CR><LF>.\r\n");
    } else {
	printf("Will send carriage returns as telnet <CR><NUL>.\r\n");
    }
    return 1;
}

int binmode;

static int
togbinary(int val)
{
    donebinarytoggle = 1;

    if (val >= 0) {
	binmode = val;
    } else {
	if (my_want_state_is_will(TELOPT_BINARY) &&
				my_want_state_is_do(TELOPT_BINARY)) {
	    binmode = 1;
	} else if (my_want_state_is_wont(TELOPT_BINARY) &&
				my_want_state_is_dont(TELOPT_BINARY)) {
	    binmode = 0;
	}
	val = binmode ? 0 : 1;
    }

    if (val == 1) {
	if (my_want_state_is_will(TELOPT_BINARY) &&
					my_want_state_is_do(TELOPT_BINARY)) {
	    printf("Already operating in binary mode with remote host.\r\n");
	} else {
	    printf("Negotiating binary mode with remote host.\r\n");
	    tel_enter_binary(3);
	}
    } else {
	if (my_want_state_is_wont(TELOPT_BINARY) &&
					my_want_state_is_dont(TELOPT_BINARY)) {
	    printf("Already in network ascii mode with remote host.\r\n");
	} else {
	    printf("Negotiating network ascii mode with remote host.\r\n");
	    tel_leave_binary(3);
	}
    }
    return 1;
}

static int
togrbinary(int val)
{
    donebinarytoggle = 1;

    if (val == -1)
	val = my_want_state_is_do(TELOPT_BINARY) ? 0 : 1;

    if (val == 1) {
	if (my_want_state_is_do(TELOPT_BINARY)) {
	    printf("Already receiving in binary mode.\r\n");
	} else {
	    printf("Negotiating binary mode on input.\r\n");
	    tel_enter_binary(1);
	}
    } else {
	if (my_want_state_is_dont(TELOPT_BINARY)) {
	    printf("Already receiving in network ascii mode.\r\n");
	} else {
	    printf("Negotiating network ascii mode on input.\r\n");
	    tel_leave_binary(1);
	}
    }
    return 1;
}

static int
togxbinary(int val)
{
    donebinarytoggle = 1;

    if (val == -1)
	val = my_want_state_is_will(TELOPT_BINARY) ? 0 : 1;

    if (val == 1) {
	if (my_want_state_is_will(TELOPT_BINARY)) {
	    printf("Already transmitting in binary mode.\r\n");
	} else {
	    printf("Negotiating binary mode on output.\r\n");
	    tel_enter_binary(2);
	}
    } else {
	if (my_want_state_is_wont(TELOPT_BINARY)) {
	    printf("Already transmitting in network ascii mode.\r\n");
	} else {
	    printf("Negotiating network ascii mode on output.\r\n");
	    tel_leave_binary(2);
	}
    }
    return 1;
}


static int togglehelp (void);
#if	defined(AUTHENTICATION)
extern int auth_togdebug (int);
#endif
#if	defined(ENCRYPTION)
extern int EncryptAutoEnc (int);
extern int EncryptAutoDec (int);
extern int EncryptDebug (int);
extern int EncryptVerbose (int);
#endif

struct togglelist {
    char	*name;		/* name of toggle */
    char	*help;		/* help message */
    int		(*handler)();	/* routine to do actual setting */
    int		*variable;
    char	*actionexplanation;
};

static struct togglelist Togglelist[] = {
    { "autoflush",
	"flushing of output when sending interrupt characters",
	    0,
		&autoflush,
		    "flush output when sending interrupt characters" },
    { "autosynch",
	"automatic sending of interrupt characters in urgent mode",
	    0,
		&autosynch,
		    "send interrupt characters in urgent mode" },
#if	defined(AUTHENTICATION)
    { "autologin",
	"automatic sending of login and/or authentication info",
	    0,
		&autologin,
		    "send login name and/or authentication information" },
    { "authdebug",
	"authentication debugging",
	    auth_togdebug,
		0,
		     "print authentication debugging information" },
#endif
#if	defined(ENCRYPTION)
    { "autoencrypt",
	"automatic encryption of data stream",
	    EncryptAutoEnc,
		0,
		    "automatically encrypt output" },
    { "autodecrypt",
	"automatic decryption of data stream",
	    EncryptAutoDec,
		0,
		    "automatically decrypt input" },
    { "verbose_encrypt",
	"verbose encryption output",
	    EncryptVerbose,
		0,
		    "print verbose encryption output" },
    { "encdebug",
	"encryption debugging",
	    EncryptDebug,
		0,
		    "print encryption debugging information" },
#endif
#if defined(KRB5)
    { "forward",
	"credentials forwarding",
	    kerberos5_set_forward,
		0,
		    "forward credentials" },
    { "forwardable",
	"forwardable flag of forwarded credentials",
	    kerberos5_set_forwardable,
		0,
		    "forward forwardable credentials" },
#endif
   { "skiprc",
	"don't read ~/.telnetrc file",
	    0,
		&skiprc,
		    "skip reading of ~/.telnetrc file" },
    { "binary",
	"sending and receiving of binary data",
	    togbinary,
		0,
		    0 },
    { "inbinary",
	"receiving of binary data",
	    togrbinary,
		0,
		    0 },
    { "outbinary",
	"sending of binary data",
	    togxbinary,
		0,
		    0 },
    { "crlf",
	"sending carriage returns as telnet <CR><LF>",
	    togcrlf,
		&crlf,
		    0 },
    { "crmod",
	"mapping of received carriage returns",
	    0,
		&crmod,
		    "map carriage return on output" },
    { "localchars",
	"local recognition of certain control characters",
	    lclchars,
		&localchars,
		    "recognize certain control characters" },
    { " ", "", 0 },		/* empty line */
    { "debug",
	"debugging",
	    togdebug,
		&debug,
		    "turn on socket level debugging" },
    { "netdata",
	"printing of hexadecimal network data (debugging)",
	    0,
		&netdata,
		    "print hexadecimal representation of network traffic" },
    { "prettydump",
	"output of \"netdata\" to user readable format (debugging)",
	    0,
		&prettydump,
		    "print user readable output for \"netdata\"" },
    { "options",
	"viewing of options processing (debugging)",
	    0,
		&showoptions,
		    "show option processing" },
    { "termdata",
	"printing of hexadecimal terminal data (debugging)",
	    0,
		&termdata,
		    "print hexadecimal representation of terminal traffic" },
    { "?",
	0,
	    togglehelp },
    { "help",
	0,
	    togglehelp },
    { 0 }
};

static int
togglehelp()
{
    struct togglelist *c;

    for (c = Togglelist; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s toggle %s\r\n", c->name, c->help);
	    else
		printf("\r\n");
	}
    }
    printf("\r\n");
    printf("%-15s %s\r\n", "?", "display help information");
    return 0;
}

static void
settogglehelp(int set)
{
    struct togglelist *c;

    for (c = Togglelist; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s %s\r\n", c->name, set ? "enable" : "disable",
						c->help);
	    else
		printf("\r\n");
	}
    }
}

#define	GETTOGGLE(name) (struct togglelist *) \
		genget(name, (char **) Togglelist, sizeof(struct togglelist))

static int
toggle(int argc, char *argv[])
{
    int retval = 1;
    char *name;
    struct togglelist *c;

    if (argc < 2) {
	fprintf(stderr,
	    "Need an argument to 'toggle' command.  'toggle ?' for help.\r\n");
	return 0;
    }
    argc--;
    argv++;
    while (argc--) {
	name = *argv++;
	c = GETTOGGLE(name);
	if (Ambiguous(c)) {
	    fprintf(stderr, "'%s': ambiguous argument ('toggle ?' for help).\r\n",
					name);
	    return 0;
	} else if (c == 0) {
	    fprintf(stderr, "'%s': unknown argument ('toggle ?' for help).\r\n",
					name);
	    return 0;
	} else {
	    if (c->variable) {
		*c->variable = !*c->variable;		/* invert it */
		if (c->actionexplanation) {
		    printf("%s %s.\r\n", *c->variable? "Will" : "Won't",
							c->actionexplanation);
		}
	    }
	    if (c->handler) {
		retval &= (*c->handler)(-1);
	    }
	}
    }
    return retval;
}

/*
 * The following perform the "set" command.
 */

struct termios new_tc = { 0 };

struct setlist {
    char *name;				/* name */
    char *help;				/* help information */
    void (*handler)();
    cc_t *charp;			/* where it is located at */
};

static struct setlist Setlist[] = {
#ifdef	KLUDGELINEMODE
    { "echo", 	"character to toggle local echoing on/off", 0, &echoc },
#endif
    { "escape",	"character to escape back to telnet command mode", 0, &escape },
    { "rlogin", "rlogin escape character", 0, &rlogin },
    { "tracefile", "file to write trace information to", SetNetTrace, (cc_t *)NetTraceFile},
    { " ", "" },
    { " ", "The following need 'localchars' to be toggled true", 0, 0 },
    { "flushoutput", "character to cause an Abort Output", 0, &termFlushChar },
    { "interrupt", "character to cause an Interrupt Process", 0, &termIntChar },
    { "quit",	"character to cause an Abort process", 0, &termQuitChar },
    { "eof",	"character to cause an EOF ", 0, &termEofChar },
    { " ", "" },
    { " ", "The following are for local editing in linemode", 0, 0 },
    { "erase",	"character to use to erase a character", 0, &termEraseChar },
    { "kill",	"character to use to erase a line", 0, &termKillChar },
    { "lnext",	"character to use for literal next", 0, &termLiteralNextChar },
    { "susp",	"character to cause a Suspend Process", 0, &termSuspChar },
    { "reprint", "character to use for line reprint", 0, &termRprntChar },
    { "worderase", "character to use to erase a word", 0, &termWerasChar },
    { "start",	"character to use for XON", 0, &termStartChar },
    { "stop",	"character to use for XOFF", 0, &termStopChar },
    { "forw1",	"alternate end of line character", 0, &termForw1Char },
    { "forw2",	"alternate end of line character", 0, &termForw2Char },
    { "ayt",	"alternate AYT character", 0, &termAytChar },
    { 0 }
};

static struct setlist *
getset(char *name)
{
    return (struct setlist *)
		genget(name, (char **) Setlist, sizeof(struct setlist));
}

void
set_escape_char(char *s)
{
	if (rlogin != _POSIX_VDISABLE) {
		rlogin = (s && *s) ? special(s) : _POSIX_VDISABLE;
		printf("Telnet rlogin escape character is '%s'.\r\n",
					control(rlogin));
	} else {
		escape = (s && *s) ? special(s) : _POSIX_VDISABLE;
		printf("Telnet escape character is '%s'.\r\n", control(escape));
	}
}

static int
setcmd(int argc, char *argv[])
{
    int value;
    struct setlist *ct;
    struct togglelist *c;

    if (argc < 2 || argc > 3) {
	printf("Format is 'set Name Value'\r\n'set ?' for help.\r\n");
	return 0;
    }
    if ((argc == 2) && (isprefix(argv[1], "?") || isprefix(argv[1], "help"))) {
	for (ct = Setlist; ct->name; ct++)
	    printf("%-15s %s\r\n", ct->name, ct->help);
	printf("\r\n");
	settogglehelp(1);
	printf("%-15s %s\r\n", "?", "display help information");
	return 0;
    }

    ct = getset(argv[1]);
    if (ct == 0) {
	c = GETTOGGLE(argv[1]);
	if (c == 0) {
	    fprintf(stderr, "'%s': unknown argument ('set ?' for help).\r\n",
			argv[1]);
	    return 0;
	} else if (Ambiguous(c)) {
	    fprintf(stderr, "'%s': ambiguous argument ('set ?' for help).\r\n",
			argv[1]);
	    return 0;
	}
	if (c->variable) {
	    if ((argc == 2) || (strcmp("on", argv[2]) == 0))
		*c->variable = 1;
	    else if (strcmp("off", argv[2]) == 0)
		*c->variable = 0;
	    else {
		printf("Format is 'set togglename [on|off]'\r\n'set ?' for help.\r\n");
		return 0;
	    }
	    if (c->actionexplanation) {
		printf("%s %s.\r\n", *c->variable? "Will" : "Won't",
							c->actionexplanation);
	    }
	}
	if (c->handler)
	    (*c->handler)(1);
    } else if (argc != 3) {
	printf("Format is 'set Name Value'\r\n'set ?' for help.\r\n");
	return 0;
    } else if (Ambiguous(ct)) {
	fprintf(stderr, "'%s': ambiguous argument ('set ?' for help).\r\n",
			argv[1]);
	return 0;
    } else if (ct->handler) {
	(*ct->handler)(argv[2]);
	printf("%s set to \"%s\".\r\n", ct->name, (char *)ct->charp);
    } else {
	if (strcmp("off", argv[2])) {
	    value = special(argv[2]);
	} else {
	    value = _POSIX_VDISABLE;
	}
	*(ct->charp) = (cc_t)value;
	printf("%s character is '%s'.\r\n", ct->name, control(*(ct->charp)));
    }
    slc_check();
    return 1;
}

static int
unsetcmd(int argc, char *argv[])
{
    struct setlist *ct;
    struct togglelist *c;
    char *name;

    if (argc < 2) {
	fprintf(stderr,
	    "Need an argument to 'unset' command.  'unset ?' for help.\r\n");
	return 0;
    }
    if (isprefix(argv[1], "?") || isprefix(argv[1], "help")) {
	for (ct = Setlist; ct->name; ct++)
	    printf("%-15s %s\r\n", ct->name, ct->help);
	printf("\r\n");
	settogglehelp(0);
	printf("%-15s %s\r\n", "?", "display help information");
	return 0;
    }

    argc--;
    argv++;
    while (argc--) {
	name = *argv++;
	ct = getset(name);
	if (ct == 0) {
	    c = GETTOGGLE(name);
	    if (c == 0) {
		fprintf(stderr, "'%s': unknown argument ('unset ?' for help).\r\n",
			name);
		return 0;
	    } else if (Ambiguous(c)) {
		fprintf(stderr, "'%s': ambiguous argument ('unset ?' for help).\r\n",
			name);
		return 0;
	    }
	    if (c->variable) {
		*c->variable = 0;
		if (c->actionexplanation) {
		    printf("%s %s.\r\n", *c->variable? "Will" : "Won't",
							c->actionexplanation);
		}
	    }
	    if (c->handler)
		(*c->handler)(0);
	} else if (Ambiguous(ct)) {
	    fprintf(stderr, "'%s': ambiguous argument ('unset ?' for help).\r\n",
			name);
	    return 0;
	} else if (ct->handler) {
	    (*ct->handler)(0);
	    printf("%s reset to \"%s\".\r\n", ct->name, (char *)ct->charp);
	} else {
	    *(ct->charp) = _POSIX_VDISABLE;
	    printf("%s character is '%s'.\r\n", ct->name, control(*(ct->charp)));
	}
    }
    return 1;
}

/*
 * The following are the data structures and routines for the
 * 'mode' command.
 */
#ifdef	KLUDGELINEMODE

static int
dokludgemode(void)
{
    kludgelinemode = 1;
    send_wont(TELOPT_LINEMODE, 1);
    send_dont(TELOPT_SGA, 1);
    send_dont(TELOPT_ECHO, 1);
    return 1;
}
#endif

static int
dolinemode()
{
#ifdef	KLUDGELINEMODE
    if (kludgelinemode)
	send_dont(TELOPT_SGA, 1);
#endif
    send_will(TELOPT_LINEMODE, 1);
    send_dont(TELOPT_ECHO, 1);
    return 1;
}

static int
docharmode()
{
#ifdef	KLUDGELINEMODE
    if (kludgelinemode)
	send_do(TELOPT_SGA, 1);
    else
#endif
    send_wont(TELOPT_LINEMODE, 1);
    send_do(TELOPT_ECHO, 1);
    return 1;
}

static int
dolmmode(int bit, int on)
{
    unsigned char c;

    if (my_want_state_is_wont(TELOPT_LINEMODE)) {
	printf("?Need to have LINEMODE option enabled first.\r\n");
	printf("'mode ?' for help.\r\n");
 	return 0;
    }

    if (on)
	c = (linemode | bit);
    else
	c = (linemode & ~bit);
    lm_mode(&c, 1, 1);
    return 1;
}

static int
tn_setmode(int bit)
{
    return dolmmode(bit, 1);
}

static int
tn_clearmode(int bit)
{
    return dolmmode(bit, 0);
}

struct modelist {
	char	*name;		/* command name */
	char	*help;		/* help string */
	int	(*handler)();	/* routine which executes command */
	int	needconnect;	/* Do we need to be connected to execute? */
	int	arg1;
};

static int modehelp(void);

static struct modelist ModeList[] = {
    { "character", "Disable LINEMODE option",	docharmode, 1 },
#ifdef	KLUDGELINEMODE
    { "",	"(or disable obsolete line-by-line mode)", 0 },
#endif
    { "line",	"Enable LINEMODE option",	dolinemode, 1 },
#ifdef	KLUDGELINEMODE
    { "",	"(or enable obsolete line-by-line mode)", 0 },
#endif
    { "", "", 0 },
    { "",	"These require the LINEMODE option to be enabled", 0 },
    { "isig",	"Enable signal trapping",	tn_setmode, 1, MODE_TRAPSIG },
    { "+isig",	0,				tn_setmode, 1, MODE_TRAPSIG },
    { "-isig",	"Disable signal trapping",	tn_clearmode, 1, MODE_TRAPSIG },
    { "edit",	"Enable character editing",	tn_setmode, 1, MODE_EDIT },
    { "+edit",	0,				tn_setmode, 1, MODE_EDIT },
    { "-edit",	"Disable character editing",	tn_clearmode, 1, MODE_EDIT },
    { "softtabs", "Enable tab expansion",	tn_setmode, 1, MODE_SOFT_TAB },
    { "+softtabs", 0,				tn_setmode, 1, MODE_SOFT_TAB },
    { "-softtabs", "Disable tab expansion",	tn_clearmode, 1, MODE_SOFT_TAB },
    { "litecho", "Enable literal character echo", tn_setmode, 1, MODE_LIT_ECHO },
    { "+litecho", 0,				tn_setmode, 1, MODE_LIT_ECHO },
    { "-litecho", "Disable literal character echo", tn_clearmode, 1, MODE_LIT_ECHO },
    { "help",	0,				modehelp, 0 },
#ifdef	KLUDGELINEMODE
    { "kludgeline", 0,				dokludgemode, 1 },
#endif
    { "", "", 0 },
    { "?",	"Print help information",	modehelp, 0 },
    { 0 },
};


static int
modehelp(void)
{
    struct modelist *mt;

    printf("format is:  'mode Mode', where 'Mode' is one of:\r\n\r\n");
    for (mt = ModeList; mt->name; mt++) {
	if (mt->help) {
	    if (*mt->help)
		printf("%-15s %s\r\n", mt->name, mt->help);
	    else
		printf("\r\n");
	}
    }
    return 0;
}

#define	GETMODECMD(name) (struct modelist *) \
		genget(name, (char **) ModeList, sizeof(struct modelist))

static int
modecmd(int argc, char **argv)
{
    struct modelist *mt;

    if (argc != 2) {
	printf("'mode' command requires an argument\r\n");
	printf("'mode ?' for help.\r\n");
    } else if ((mt = GETMODECMD(argv[1])) == 0) {
	fprintf(stderr, "Unknown mode '%s' ('mode ?' for help).\r\n", argv[1]);
    } else if (Ambiguous(mt)) {
	fprintf(stderr, "Ambiguous mode '%s' ('mode ?' for help).\r\n", argv[1]);
    } else if (mt->needconnect && !connected) {
	printf("?Need to be connected first.\r\n");
	printf("'mode ?' for help.\r\n");
    } else if (mt->handler) {
	return (*mt->handler)(mt->arg1);
    }
    return 0;
}

/*
 * The following data structures and routines implement the
 * "display" command.
 */

static int
display(int argc, char *argv[])
{
    struct togglelist *tl;
    struct setlist *sl;

#define	dotog(tl)	if (tl->variable && tl->actionexplanation) { \
			    if (*tl->variable) { \
				printf("will"); \
			    } else { \
				printf("won't"); \
			    } \
			    printf(" %s.\r\n", tl->actionexplanation); \
			}

#define	doset(sl)   if (sl->name && *sl->name != ' ') { \
			if (sl->handler == 0) \
			    printf("%-15s [%s]\r\n", sl->name, control(*sl->charp)); \
			else \
			    printf("%-15s \"%s\"\r\n", sl->name, (char *)sl->charp); \
		    }

    if (argc == 1) {
	for (tl = Togglelist; tl->name; tl++) {
	    dotog(tl);
	}
	printf("\r\n");
	for (sl = Setlist; sl->name; sl++) {
	    doset(sl);
	}
    } else {
	int i;

	for (i = 1; i < argc; i++) {
	    sl = getset(argv[i]);
	    tl = GETTOGGLE(argv[i]);
	    if (Ambiguous(sl) || Ambiguous(tl)) {
		printf("?Ambiguous argument '%s'.\r\n", argv[i]);
		return 0;
	    } else if (!sl && !tl) {
		printf("?Unknown argument '%s'.\r\n", argv[i]);
		return 0;
	    } else {
		if (tl) {
		    dotog(tl);
		}
		if (sl) {
		    doset(sl);
		}
	    }
	}
    }
/*@*/optionstatus();
#if	defined(ENCRYPTION)
    EncryptStatus();
#endif
    return 1;
#undef	doset
#undef	dotog
}

/*
 * The following are the data structures, and many of the routines,
 * relating to command processing.
 */

/*
 * Set the escape character.
 */
static int
setescape(int argc, char *argv[])
{
	char *arg;
	char buf[50];

	printf(
	    "Deprecated usage - please use 'set escape%s%s' in the future.\r\n",
				(argc > 2)? " ":"", (argc > 2)? argv[1]: "");
	if (argc > 2)
		arg = argv[1];
	else {
		printf("new escape character: ");
		fgets(buf, sizeof(buf), stdin);
		arg = buf;
	}
	if (arg[0] != '\0')
		escape = arg[0];
	printf("Escape character is '%s'.\r\n", control(escape));

	fflush(stdout);
	return 1;
}

static int
togcrmod()
{
    crmod = !crmod;
    printf("Deprecated usage - please use 'toggle crmod' in the future.\r\n");
    printf("%s map carriage return on output.\r\n", crmod ? "Will" : "Won't");
    fflush(stdout);
    return 1;
}

static int
telnetsuspend()
{
#ifdef	SIGTSTP
    setcommandmode();
    {
	long oldrows, oldcols, newrows, newcols, err;

	err = (TerminalWindowSize(&oldrows, &oldcols) == 0) ? 1 : 0;
	kill(0, SIGTSTP);
	/*
	 * If we didn't get the window size before the SUSPEND, but we
	 * can get them now (?), then send the NAWS to make sure that
	 * we are set up for the right window size.
	 */
	if (TerminalWindowSize(&newrows, &newcols) && connected &&
	    (err || ((oldrows != newrows) || (oldcols != newcols)))) {
		sendnaws();
	}
    }
    /* reget parameters in case they were changed */
    TerminalSaveState();
    setconnmode(0);
#else
    printf("Suspend is not supported.  Try the '!' command instead\r\n");
#endif
    return 1;
}

static int
shell(int argc, char **argv)
{
    long oldrows, oldcols, newrows, newcols, err;

    setcommandmode();

    err = (TerminalWindowSize(&oldrows, &oldcols) == 0) ? 1 : 0;
    switch(fork()) {
    case -1:
	perror("Fork failed\r\n");
	break;

    case 0:
	{
	    /*
	     * Fire up the shell in the child.
	     */
	    char *shellp, *shellname;

	    shellp = getenv("SHELL");
	    if (shellp == NULL)
		shellp = "/bin/sh";
	    if ((shellname = strrchr(shellp, '/')) == 0)
		shellname = shellp;
	    else
		shellname++;
	    if (argc > 1)
		execl(shellp, shellname, "-c", &saveline[1], NULL);
	    else
		execl(shellp, shellname, NULL);
	    perror("Execl");
	    _exit(1);
	}
    default:
	    wait((int *)0);	/* Wait for the shell to complete */

	    if (TerminalWindowSize(&newrows, &newcols) && connected &&
		(err || ((oldrows != newrows) || (oldcols != newcols)))) {
		    sendnaws();
	    }
	    break;
    }
    return 1;
}

static int
bye(int argc, char **argv)
{
    if (connected) {
	shutdown(net, 2);
	printf("Connection closed.\r\n");
	NetClose(net);
	connected = 0;
	resettermname = 1;
#if	defined(AUTHENTICATION) || defined(ENCRYPTION)
	auth_encrypt_connect(connected);
#endif
	/* reset options */
	tninit();
    }
    if ((argc != 2) || (strcmp(argv[1], "fromquit") != 0))
	longjmp(toplevel, 1);
    return 0;	/* NOTREACHED */
}

int
quit(void)
{
	call(bye, "bye", "fromquit", 0);
	Exit(0);
	return 0; /*NOTREACHED*/
}

static int
logout()
{
	send_do(TELOPT_LOGOUT, 1);
	netflush();
	return 1;
}


/*
 * The SLC command.
 */

struct slclist {
	char	*name;
	char	*help;
	void	(*handler)();
	int	arg;
};

static void slc_help(void);

struct slclist SlcList[] = {
    { "export",	"Use local special character definitions",
						slc_mode_export,	0 },
    { "import",	"Use remote special character definitions",
						slc_mode_import,	1 },
    { "check",	"Verify remote special character definitions",
						slc_mode_import,	0 },
    { "help",	0,				slc_help,		0 },
    { "?",	"Print help information",	slc_help,		0 },
    { 0 },
};

static void
slc_help(void)
{
    struct slclist *c;

    for (c = SlcList; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s\r\n", c->name, c->help);
	    else
		printf("\r\n");
	}
    }
}

static struct slclist *
getslc(char *name)
{
    return (struct slclist *)
		genget(name, (char **) SlcList, sizeof(struct slclist));
}

static int
slccmd(int argc, char **argv)
{
    struct slclist *c;

    if (argc != 2) {
	fprintf(stderr,
	    "Need an argument to 'slc' command.  'slc ?' for help.\r\n");
	return 0;
    }
    c = getslc(argv[1]);
    if (c == 0) {
	fprintf(stderr, "'%s': unknown argument ('slc ?' for help).\r\n",
    				argv[1]);
	return 0;
    }
    if (Ambiguous(c)) {
	fprintf(stderr, "'%s': ambiguous argument ('slc ?' for help).\r\n",
    				argv[1]);
	return 0;
    }
    (*c->handler)(c->arg);
    slcstate();
    return 1;
}

/*
 * The ENVIRON command.
 */

struct envlist {
	char	*name;
	char	*help;
	void	(*handler)();
	int	narg;
};

static void env_help (void);

struct envlist EnvList[] = {
    { "define",	"Define an environment variable",
						(void (*)())env_define,	2 },
    { "undefine", "Undefine an environment variable",
						env_undefine,	1 },
    { "export",	"Mark an environment variable for automatic export",
						env_export,	1 },
    { "unexport", "Don't mark an environment variable for automatic export",
						env_unexport,	1 },
    { "send",	"Send an environment variable", env_send,	1 },
    { "list",	"List the current environment variables",
						env_list,	0 },
    { "help",	0,				env_help,		0 },
    { "?",	"Print help information",	env_help,		0 },
    { 0 },
};

static void
env_help()
{
    struct envlist *c;

    for (c = EnvList; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s\r\n", c->name, c->help);
	    else
		printf("\r\n");
	}
    }
}

static struct envlist *
getenvcmd(char *name)
{
    return (struct envlist *)
		genget(name, (char **) EnvList, sizeof(struct envlist));
}

static int
env_cmd(int argc, char **argv)
{
    struct envlist *c;

    if (argc < 2) {
	fprintf(stderr,
	    "Need an argument to 'environ' command.  'environ ?' for help.\r\n");
	return 0;
    }
    c = getenvcmd(argv[1]);
    if (c == 0) {
	fprintf(stderr, "'%s': unknown argument ('environ ?' for help).\r\n",
    				argv[1]);
	return 0;
    }
    if (Ambiguous(c)) {
	fprintf(stderr, "'%s': ambiguous argument ('environ ?' for help).\r\n",
    				argv[1]);
	return 0;
    }
    if (c->narg + 2 != argc) {
	fprintf(stderr,
	    "Need %s%d argument%s to 'environ %s' command.  'environ ?' for help.\r\n",
		c->narg < argc + 2 ? "only " : "",
		c->narg, c->narg == 1 ? "" : "s", c->name);
	return 0;
    }
    (*c->handler)(argv[2], argv[3]);
    return 1;
}

struct env_lst {
	struct env_lst *next;	/* pointer to next structure */
	struct env_lst *prev;	/* pointer to previous structure */
	unsigned char *var;	/* pointer to variable name */
	unsigned char *value;	/* pointer to variable value */
	int export;		/* 1 -> export with default list of variables */
	int welldefined;	/* A well defined variable */
};

struct env_lst envlisthead;

struct env_lst *
env_find(unsigned char *var)
{
	struct env_lst *ep;

	for (ep = envlisthead.next; ep; ep = ep->next) {
		if (strcmp((char *)ep->var, (char *)var) == 0)
			return(ep);
	}
	return(NULL);
}

#if !HAVE_DECL_ENVIRON
extern char **environ;
#endif

void
env_init(void)
{
	char **epp, *cp;
	struct env_lst *ep;

	for (epp = environ; *epp; epp++) {
		if ((cp = strchr(*epp, '='))) {
			*cp = '\0';
			ep = env_define((unsigned char *)*epp,
					(unsigned char *)cp+1);
			ep->export = 0;
			*cp = '=';
		}
	}
	/*
	 * Special case for DISPLAY variable.  If it is ":0.0" or
	 * "unix:0.0", we have to get rid of "unix" and insert our
	 * hostname.
	 */
	if ((ep = env_find((unsigned char*)"DISPLAY"))
	    && (*ep->value == ':'
	    || strncmp((char *)ep->value, "unix:", 5) == 0)) {
		char hbuf[256+1];
		char *cp2 = strchr((char *)ep->value, ':');
		int error;

		/* XXX - should be k_gethostname? */
		gethostname(hbuf, 256);
		hbuf[256] = '\0';

		/* If this is not the full name, try to get it via DNS */
		if (strchr(hbuf, '.') == 0) {
			struct addrinfo hints, *ai, *a;

			memset (&hints, 0, sizeof(hints));
			hints.ai_flags = AI_CANONNAME;

			error = getaddrinfo (hbuf, NULL, &hints, &ai);
			if (error == 0) {
				for (a = ai; a != NULL; a = a->ai_next)
					if (a->ai_canonname != NULL) {
						strlcpy (hbuf,
							 ai->ai_canonname,
							 256);
						break;
					}
				freeaddrinfo (ai);
			}
		}

		error = asprintf (&cp, "%s%s", hbuf, cp2);
		if (error != -1) {
		    free (ep->value);
		    ep->value = (unsigned char *)cp;
		}
	}
	/*
	 * If USER is not defined, but LOGNAME is, then add
	 * USER with the value from LOGNAME.  By default, we
	 * don't export the USER variable.
	 */
	if ((env_find((unsigned char*)"USER") == NULL) &&
	    (ep = env_find((unsigned char*)"LOGNAME"))) {
		env_define((unsigned char *)"USER", ep->value);
		env_unexport((unsigned char *)"USER");
	}
	env_export((unsigned char *)"DISPLAY");
	env_export((unsigned char *)"PRINTER");
	env_export((unsigned char *)"XAUTHORITY");
}

struct env_lst *
env_define(unsigned char *var, unsigned char *value)
{
	struct env_lst *ep;

	if ((ep = env_find(var))) {
		if (ep->var)
			free(ep->var);
		if (ep->value)
			free(ep->value);
	} else {
		ep = (struct env_lst *)malloc(sizeof(struct env_lst));
		ep->next = envlisthead.next;
		envlisthead.next = ep;
		ep->prev = &envlisthead;
		if (ep->next)
			ep->next->prev = ep;
	}
	ep->welldefined = opt_welldefined((char *)var);
	ep->export = 1;
	ep->var = (unsigned char *)strdup((char *)var);
	ep->value = (unsigned char *)strdup((char *)value);
	return(ep);
}

void
env_undefine(unsigned char *var)
{
	struct env_lst *ep;

	if ((ep = env_find(var))) {
		ep->prev->next = ep->next;
		if (ep->next)
			ep->next->prev = ep->prev;
		if (ep->var)
			free(ep->var);
		if (ep->value)
			free(ep->value);
		free(ep);
	}
}

void
env_export(unsigned char *var)
{
	struct env_lst *ep;

	if ((ep = env_find(var)))
		ep->export = 1;
}

void
env_unexport(unsigned char *var)
{
	struct env_lst *ep;

	if ((ep = env_find(var)))
		ep->export = 0;
}

void
env_send(unsigned char *var)
{
	struct env_lst *ep;

	if (my_state_is_wont(TELOPT_NEW_ENVIRON)
#ifdef	OLD_ENVIRON
	    && my_state_is_wont(TELOPT_OLD_ENVIRON)
#endif
		) {
		fprintf(stderr,
		    "Cannot send '%s': Telnet ENVIRON option not enabled\r\n",
									var);
		return;
	}
	ep = env_find(var);
	if (ep == 0) {
		fprintf(stderr, "Cannot send '%s': variable not defined\r\n",
									var);
		return;
	}
	env_opt_start_info();
	env_opt_add(ep->var);
	env_opt_end(0);
}

void
env_list(void)
{
	struct env_lst *ep;

	for (ep = envlisthead.next; ep; ep = ep->next) {
		printf("%c %-20s %s\r\n", ep->export ? '*' : ' ',
					ep->var, ep->value);
	}
}

unsigned char *
env_default(int init, int welldefined)
{
	static struct env_lst *nep = NULL;

	if (init) {
		nep = &envlisthead;
		return NULL;
	}
	if (nep) {
		while ((nep = nep->next)) {
			if (nep->export && (nep->welldefined == welldefined))
				return(nep->var);
		}
	}
	return(NULL);
}

unsigned char *
env_getvalue(unsigned char *var)
{
	struct env_lst *ep;

	if ((ep = env_find(var)))
		return(ep->value);
	return(NULL);
}


#if	defined(AUTHENTICATION)
/*
 * The AUTHENTICATE command.
 */

struct authlist {
	char	*name;
	char	*help;
	int	(*handler)();
	int	narg;
};

static int
	auth_help (void);

struct authlist AuthList[] = {
    { "status",	"Display current status of authentication information",
						auth_status,	0 },
    { "disable", "Disable an authentication type ('auth disable ?' for more)",
						auth_disable,	1 },
    { "enable", "Enable an authentication type ('auth enable ?' for more)",
						auth_enable,	1 },
    { "help",	0,				auth_help,		0 },
    { "?",	"Print help information",	auth_help,		0 },
    { 0 },
};

static int
auth_help()
{
    struct authlist *c;

    for (c = AuthList; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s\r\n", c->name, c->help);
	    else
		printf("\r\n");
	}
    }
    return 0;
}

static int
auth_cmd(int argc, char **argv)
{
    struct authlist *c;

    if (argc < 2) {
	fprintf(stderr,
	    "Need an argument to 'auth' command.  'auth ?' for help.\r\n");
	return 0;
    }

    c = (struct authlist *)
		genget(argv[1], (char **) AuthList, sizeof(struct authlist));
    if (c == 0) {
	fprintf(stderr, "'%s': unknown argument ('auth ?' for help).\r\n",
    				argv[1]);
	return 0;
    }
    if (Ambiguous(c)) {
	fprintf(stderr, "'%s': ambiguous argument ('auth ?' for help).\r\n",
    				argv[1]);
	return 0;
    }
    if (c->narg + 2 != argc) {
	fprintf(stderr,
	    "Need %s%d argument%s to 'auth %s' command.  'auth ?' for help.\r\n",
		c->narg < argc + 2 ? "only " : "",
		c->narg, c->narg == 1 ? "" : "s", c->name);
	return 0;
    }
    return((*c->handler)(argv[2], argv[3]));
}
#endif


#if	defined(ENCRYPTION)
/*
 * The ENCRYPT command.
 */

struct encryptlist {
	char	*name;
	char	*help;
	int	(*handler)();
	int	needconnect;
	int	minarg;
	int	maxarg;
};

static int
	EncryptHelp (void);

struct encryptlist EncryptList[] = {
    { "enable", "Enable encryption. ('encrypt enable ?' for more)",
						EncryptEnable, 1, 1, 2 },
    { "disable", "Disable encryption. ('encrypt enable ?' for more)",
						EncryptDisable, 0, 1, 2 },
    { "type", "Set encryptiong type. ('encrypt type ?' for more)",
						EncryptType, 0, 1, 1 },
    { "start", "Start encryption. ('encrypt start ?' for more)",
						EncryptStart, 1, 0, 1 },
    { "stop", "Stop encryption. ('encrypt stop ?' for more)",
						EncryptStop, 1, 0, 1 },
    { "input", "Start encrypting the input stream",
						EncryptStartInput, 1, 0, 0 },
    { "-input", "Stop encrypting the input stream",
						EncryptStopInput, 1, 0, 0 },
    { "output", "Start encrypting the output stream",
						EncryptStartOutput, 1, 0, 0 },
    { "-output", "Stop encrypting the output stream",
						EncryptStopOutput, 1, 0, 0 },

    { "status",	"Display current status of authentication information",
						EncryptStatus,	0, 0, 0 },
    { "help",	0,				EncryptHelp,	0, 0, 0 },
    { "?",	"Print help information",	EncryptHelp,	0, 0, 0 },
    { 0 },
};

static int
EncryptHelp()
{
    struct encryptlist *c;

    for (c = EncryptList; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s\r\n", c->name, c->help);
	    else
		printf("\r\n");
	}
    }
    return 0;
}

static int
encrypt_cmd(int argc, char **argv)
{
    struct encryptlist *c;

    c = (struct encryptlist *)
		genget(argv[1], (char **) EncryptList, sizeof(struct encryptlist));
    if (c == 0) {
        fprintf(stderr, "'%s': unknown argument ('encrypt ?' for help).\r\n",
    				argv[1]);
        return 0;
    }
    if (Ambiguous(c)) {
        fprintf(stderr, "'%s': ambiguous argument ('encrypt ?' for help).\r\n",
    				argv[1]);
        return 0;
    }
    argc -= 2;
    if (argc < c->minarg || argc > c->maxarg) {
	if (c->minarg == c->maxarg) {
	    fprintf(stderr, "Need %s%d argument%s ",
		c->minarg < argc ? "only " : "", c->minarg,
		c->minarg == 1 ? "" : "s");
	} else {
	    fprintf(stderr, "Need %s%d-%d arguments ",
		c->maxarg < argc ? "only " : "", c->minarg, c->maxarg);
	}
	fprintf(stderr, "to 'encrypt %s' command.  'encrypt ?' for help.\r\n",
		c->name);
	return 0;
    }
    if (c->needconnect && !connected) {
	if (!(argc && (isprefix(argv[2], "help") || isprefix(argv[2], "?")))) {
	    printf("?Need to be connected first.\r\n");
	    return 0;
	}
    }
    return ((*c->handler)(argc > 0 ? argv[2] : 0,
			argc > 1 ? argv[3] : 0,
			argc > 2 ? argv[4] : 0));
}
#endif


/*
 * Print status about the connection.
 */

static int
status(int argc, char **argv)
{
    if (connected) {
	printf("Connected to %s.\r\n", hostname);
	if ((argc < 2) || strcmp(argv[1], "notmuch")) {
	    int mode = getconnmode();

	    if (my_want_state_is_will(TELOPT_LINEMODE)) {
		printf("Operating with LINEMODE option\r\n");
		printf("%s line editing\r\n", (mode&MODE_EDIT) ? "Local" : "No");
		printf("%s catching of signals\r\n",
					(mode&MODE_TRAPSIG) ? "Local" : "No");
		slcstate();
#ifdef	KLUDGELINEMODE
	    } else if (kludgelinemode && my_want_state_is_dont(TELOPT_SGA)) {
		printf("Operating in obsolete linemode\r\n");
#endif
	    } else {
		printf("Operating in single character mode\r\n");
		if (localchars)
		    printf("Catching signals locally\r\n");
	    }
	    printf("%s character echo\r\n", (mode&MODE_ECHO) ? "Local" : "Remote");
	    if (my_want_state_is_will(TELOPT_LFLOW))
		printf("%s flow control\r\n", (mode&MODE_FLOW) ? "Local" : "No");
#if	defined(ENCRYPTION)
	    encrypt_display();
#endif
	}
    } else {
	printf("No connection.\r\n");
    }
    printf("Escape character is '%s'.\r\n", control(escape));
    fflush(stdout);
    return 1;
}

#ifdef	SIGINFO
/*
 * Function that gets called when SIGINFO is received.
 */
RETSIGTYPE
ayt_status(int ignore)
{
    call(status, "status", "notmuch", 0);
}
#endif

static Command *getcmd(char *name);

static void
cmdrc(char *m1, char *m2)
{
    static char rcname[128];
    Command *c;
    FILE *rcfile;
    int gotmachine = 0;
    int l1 = strlen(m1);
    int l2 = strlen(m2);
    char m1save[64];

    if (skiprc)
	return;

    strlcpy(m1save, m1, sizeof(m1save));
    m1 = m1save;

    if (rcname[0] == 0) {
	char *home = getenv("HOME");

	snprintf (rcname, sizeof(rcname), "%s/.telnetrc",
		  home ? home : "");
    }

    if ((rcfile = fopen(rcname, "r")) == 0) {
	return;
    }

    for (;;) {
	if (fgets(line, sizeof(line), rcfile) == NULL)
	    break;
	if (line[0] == 0)
	    break;
	if (line[0] == '#')
	    continue;
	if (gotmachine) {
	    if (!isspace((unsigned char)line[0]))
		gotmachine = 0;
	}
	if (gotmachine == 0) {
	    if (isspace((unsigned char)line[0]))
		continue;
	    if (strncasecmp(line, m1, l1) == 0)
		strncpy(line, &line[l1], sizeof(line) - l1);
	    else if (strncasecmp(line, m2, l2) == 0)
		strncpy(line, &line[l2], sizeof(line) - l2);
	    else if (strncasecmp(line, "DEFAULT", 7) == 0)
		strncpy(line, &line[7], sizeof(line) - 7);
	    else
		continue;
	    if (line[0] != ' ' && line[0] != '\t' && line[0] != '\n')
		continue;
	    gotmachine = 1;
	}
	makeargv();
	if (margv[0] == 0)
	    continue;
	c = getcmd(margv[0]);
	if (Ambiguous(c)) {
	    printf("?Ambiguous command: %s\r\n", margv[0]);
	    continue;
	}
	if (c == 0) {
	    printf("?Invalid command: %s\r\n", margv[0]);
	    continue;
	}
	/*
	 * This should never happen...
	 */
	if (c->needconnect && !connected) {
	    printf("?Need to be connected first for %s.\r\n", margv[0]);
	    continue;
	}
	(*c->handler)(margc, margv);
    }
    fclose(rcfile);
}

int
tn(int argc, char **argv)
{
    struct servent *sp = 0;
    char *cmd, *hostp = 0, *portp = 0;
    char *user = 0;
    int port = 0;

    /* clear the socket address prior to use */

    if (connected) {
	printf("?Already connected to %s\r\n", hostname);
	return 0;
    }
    if (argc < 2) {
	strlcpy(line, "open ", sizeof(line));
	printf("(to) ");
	fgets(&line[strlen(line)], sizeof(line) - strlen(line), stdin);
	makeargv();
	argc = margc;
	argv = margv;
    }
    cmd = *argv;
    --argc; ++argv;
    while (argc) {
	if (strcmp(*argv, "help") == 0 || isprefix(*argv, "?"))
	    goto usage;
	if (strcmp(*argv, "-l") == 0) {
	    --argc; ++argv;
	    if (argc == 0)
		goto usage;
	    user = strdup(*argv++);
	    --argc;
	    continue;
	}
	if (strcmp(*argv, "-a") == 0) {
	    --argc; ++argv;
	    autologin = 1;
	    continue;
	}
	if (hostp == 0) {
	    hostp = *argv++;
	    --argc;
	    continue;
	}
	if (portp == 0) {
	    portp = *argv++;
	    --argc;
	    continue;
	}
    usage:
	printf("usage: %s [-l user] [-a] host-name [port]\r\n", cmd);
	return 0;
    }
    if (hostp == 0)
	goto usage;

    strlcpy (_hostname, hostp, sizeof(_hostname));
    hostp = _hostname;
    if (hostp[0] == '@' || hostp[0] == '!') {
	char *p;
	hostname = NULL;
	for (p = hostp + 1; *p; p++) {
	    if (*p == ',' || *p == '@')
		hostname = p;
	}
	if (hostname == NULL) {
	    fprintf(stderr, "%s: bad source route specification\n", hostp);
	    return 0;
	}
	*hostname++ = '\0';
    } else
	hostname = hostp;

    if (portp) {
	if (*portp == '-') {
	    portp++;
	    telnetport = 1;
	} else
	    telnetport = 0;
	port = atoi(portp);
	if (port == 0) {
	    sp = roken_getservbyname(portp, "tcp");
	    if (sp)
		port = sp->s_port;
	    else {
		printf("%s: bad port number\r\n", portp);
		return 0;
	    }
	} else {
	    port = htons(port);
	}
    } else {
	if (sp == 0) {
	    sp = roken_getservbyname("telnet", "tcp");
	    if (sp == 0) {
		fprintf(stderr, "telnet: tcp/telnet: unknown service\r\n");
		return 0;
	    }
	    port = sp->s_port;
	}
	telnetport = 1;
    }

    {
	struct addrinfo *ai, *a, hints;
	int error;
	char portstr[NI_MAXSERV];

	memset (&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags    = AI_CANONNAME;

	snprintf (portstr, sizeof(portstr), "%u", ntohs(port));

	error = getaddrinfo (hostname, portstr, &hints, &ai);
	if (error) {
	    fprintf (stderr, "%s: %s\r\n", hostname, gai_strerror (error));
	    return 0;
	}

	for (a = ai; a != NULL && connected == 0; a = a->ai_next) {
	    char addrstr[256];

	    if (a->ai_canonname != NULL)
		strlcpy (_hostname, a->ai_canonname, sizeof(_hostname));

	    if (getnameinfo (a->ai_addr, a->ai_addrlen,
			     addrstr, sizeof(addrstr),
			     NULL, 0, NI_NUMERICHOST) != 0)
		strlcpy (addrstr, "unknown address", sizeof(addrstr));

	    printf("Trying %s...\r\n", addrstr);

	    net = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
	    if (net < 0) {
		warn ("socket");
		continue;
	    }

#if	defined(IP_OPTIONS) && defined(IPPROTO_IP) && defined(HAVE_SETSOCKOPT)
	if (hostp[0] == '@' || hostp[0] == '!') {
	    char *srp = 0;
	    int srlen;
	    int proto, opt;

	    if ((srlen = sourceroute(a, hostp, &srp, &proto, &opt)) < 0) {
		(void) NetClose(net);
		net = -1;
		continue;
	    }
	    if (srp && setsockopt(net, proto, opt, srp, srlen) < 0)
		perror("setsockopt (source route)");
	}
#endif

#if	defined(IPPROTO_IP) && defined(IP_TOS)
	    if (a->ai_family == AF_INET) {
# if	defined(HAVE_GETTOSBYNAME)
		struct tosent *tp;
		if (tos < 0 && (tp = gettosbyname("telnet", "tcp")))
		    tos = tp->t_tos;
# endif
		if (tos < 0)
		    tos = 020;	/* Low Delay bit */
		if (tos
		    && (setsockopt(net, IPPROTO_IP, IP_TOS,
				   (void *)&tos, sizeof(int)) < 0)
		    && (errno != ENOPROTOOPT))
		    perror("telnet: setsockopt (IP_TOS) (ignored)");
	    }
#endif	/* defined(IPPROTO_IP) && defined(IP_TOS) */
	    if (debug && SetSockOpt(net, SOL_SOCKET, SO_DEBUG, 1) < 0) {
		perror("setsockopt (SO_DEBUG)");
	    }

	    if (connect (net, a->ai_addr, a->ai_addrlen) < 0) {
		fprintf (stderr, "telnet: connect to address %s: %s\n",
			 addrstr, strerror(errno));
		NetClose(net);
		if (a->ai_next != NULL) {
		    continue;
		} else {
		    freeaddrinfo (ai);
		    return 0;
		}
	    }
	    ++connected;
#if	defined(AUTHENTICATION) || defined(ENCRYPTION)
	    auth_encrypt_connect(connected);
#endif
	}
	freeaddrinfo (ai);
	if (connected == 0)
	    return 0;
    }
    cmdrc(hostp, hostname);
    set_forward_options();
    if (autologin && user == NULL)
	user = (char *)get_default_username ();
    if (user) {
	env_define((unsigned char *)"USER", (unsigned char *)user);
	env_export((unsigned char *)"USER");
    }
    call(status, "status", "notmuch", 0);
    if (setjmp(peerdied) == 0)
	my_telnet((char *)user);
    NetClose(net);
    ExitString("Connection closed by foreign host.\r\n",1);
    /*NOTREACHED*/
    return 0;
}

#define HELPINDENT ((int)sizeof ("connect"))

static char
	openhelp[] =	"connect to a site",
	closehelp[] =	"close current connection",
	logouthelp[] =	"forcibly logout remote user and close the connection",
	quithelp[] =	"exit telnet",
	statushelp[] =	"print status information",
	helphelp[] =	"print help information",
	sendhelp[] =	"transmit special characters ('send ?' for more)",
	sethelp[] = 	"set operating parameters ('set ?' for more)",
	unsethelp[] = 	"unset operating parameters ('unset ?' for more)",
	togglestring[] ="toggle operating parameters ('toggle ?' for more)",
	slchelp[] =	"change state of special charaters ('slc ?' for more)",
	displayhelp[] =	"display operating parameters",
#if	defined(AUTHENTICATION)
	authhelp[] =	"turn on (off) authentication ('auth ?' for more)",
#endif
#if	defined(ENCRYPTION)
	encrypthelp[] =	"turn on (off) encryption ('encrypt ?' for more)",
#endif
	zhelp[] =	"suspend telnet",
	shellhelp[] =	"invoke a subshell",
	envhelp[] =	"change environment variables ('environ ?' for more)",
	modestring[] = "try to enter line or character mode ('mode ?' for more)";

static int help(int argc, char **argv);

static Command cmdtab[] = {
	{ "close",	closehelp,	bye,		1 },
	{ "logout",	logouthelp,	logout,		1 },
	{ "display",	displayhelp,	display,	0 },
	{ "mode",	modestring,	modecmd,	0 },
	{ "open",	openhelp,	tn,		0 },
	{ "quit",	quithelp,	quit,		0 },
	{ "send",	sendhelp,	sendcmd,	0 },
	{ "set",	sethelp,	setcmd,		0 },
	{ "unset",	unsethelp,	unsetcmd,	0 },
	{ "status",	statushelp,	status,		0 },
	{ "toggle",	togglestring,	toggle,		0 },
	{ "slc",	slchelp,	slccmd,		0 },
#if	defined(AUTHENTICATION)
	{ "auth",	authhelp,	auth_cmd,	0 },
#endif
#if	defined(ENCRYPTION)
	{ "encrypt",	encrypthelp,	encrypt_cmd,	0 },
#endif
	{ "z",		zhelp,		telnetsuspend,	0 },
	{ "!",		shellhelp,	shell,		0 },
	{ "environ",	envhelp,	env_cmd,	0 },
	{ "?",		helphelp,	help,		0 },
	{ 0,            0,              0,              0 }
};

static char	crmodhelp[] =	"deprecated command -- use 'toggle crmod' instead";
static char	escapehelp[] =	"deprecated command -- use 'set escape' instead";

static Command cmdtab2[] = {
	{ "help",	0,		help,		0 },
	{ "escape",	escapehelp,	setescape,	0 },
	{ "crmod",	crmodhelp,	togcrmod,	0 },
	{ 0,            0,		0, 		0 }
};


/*
 * Call routine with argc, argv set from args (terminated by 0).
 */

static int
call(intrtn_t routine, ...)
{
    va_list ap;
    char *args[100];
    int argno = 0;

    va_start(ap, routine);
    while ((args[argno++] = va_arg(ap, char *)) != 0);
    va_end(ap);
    return (*routine)(argno-1, args);
}


static Command
*getcmd(char *name)
{
    Command *cm;

    if ((cm = (Command *) genget(name, (char **) cmdtab, sizeof(Command))))
	return cm;
    return (Command *) genget(name, (char **) cmdtab2, sizeof(Command));
}

void
command(int top, char *tbuf, int cnt)
{
    Command *c;

    setcommandmode();
    if (!top) {
	putchar('\n');
    } else {
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
    }
    for (;;) {
	if (rlogin == _POSIX_VDISABLE)
		printf("%s> ", prompt);
	if (tbuf) {
	    char *cp;
	    cp = line;
	    while (cnt > 0 && (*cp++ = *tbuf++) != '\n')
		cnt--;
	    tbuf = 0;
	    if (cp == line || *--cp != '\n' || cp == line)
		goto getline;
	    *cp = '\0';
	    if (rlogin == _POSIX_VDISABLE)
		printf("%s\r\n", line);
	} else {
	getline:
	    if (rlogin != _POSIX_VDISABLE)
		printf("%s> ", prompt);
	    if (fgets(line, sizeof(line), stdin) == NULL) {
		if (feof(stdin) || ferror(stdin)) {
		    quit();
		    /*NOTREACHED*/
		}
		break;
	    }
	}
	if (line[0] == 0)
	    break;
	makeargv();
	if (margv[0] == 0) {
	    break;
	}
	c = getcmd(margv[0]);
	if (Ambiguous(c)) {
	    printf("?Ambiguous command\r\n");
	    continue;
	}
	if (c == 0) {
	    printf("?Invalid command\r\n");
	    continue;
	}
	if (c->needconnect && !connected) {
	    printf("?Need to be connected first.\r\n");
	    continue;
	}
	if ((*c->handler)(margc, margv)) {
	    break;
	}
    }
    if (!top) {
	if (!connected) {
	    longjmp(toplevel, 1);
	    /*NOTREACHED*/
	}
	setconnmode(0);
    }
}

/*
 * Help command.
 */
static int
help(int argc, char **argv)
{
	Command *c;

	if (argc == 1) {
		printf("Commands may be abbreviated.  Commands are:\r\n\r\n");
		for (c = cmdtab; c->name; c++)
			if (c->help) {
				printf("%-*s\t%s\r\n", HELPINDENT, c->name,
								    c->help);
			}
		return 0;
	}
	while (--argc > 0) {
		char *arg;
		arg = *++argv;
		c = getcmd(arg);
		if (Ambiguous(c))
			printf("?Ambiguous help command %s\r\n", arg);
		else if (c == (Command *)0)
			printf("?Invalid help command %s\r\n", arg);
		else
			printf("%s\r\n", c->help);
	}
	return 0;
}


#if	defined(IP_OPTIONS) && defined(IPPROTO_IP)

/*
 * Source route is handed in as
 *	[!]@hop1@hop2...@dst
 *
 * If the leading ! is present, it is a strict source route, otherwise it is
 * assmed to be a loose source route.  Note that leading ! is effective
 * only for IPv4 case.
 *
 * We fill in the source route option as
 *	hop1,hop2,hop3...dest
 * and return a pointer to hop1, which will
 * be the address to connect() to.
 *
 * Arguments:
 *	ai:	The address (by struct addrinfo) for the final destination.
 *
 *	arg:	Pointer to route list to decipher
 *
 *	cpp: 	Pointer to a pointer, so that sourceroute() can return
 *		the address of result buffer (statically alloc'ed).
 *
 *	protop/optp:
 *		Pointer to an integer.  The pointed variable
 *	lenp:	pointer to an integer that contains the
 *		length of *cpp if *cpp != NULL.
 *
 * Return values:
 *
 *	Returns the length of the option pointed to by *cpp.  If the
 *	return value is -1, there was a syntax error in the
 *	option, either arg contained unknown characters or too many hosts,
 *	or hostname cannot be resolved.
 *
 *	The caller needs to pass return value (len), *cpp, *protop and *optp
 *	to setsockopt(2).
 *
 *	*cpp:	Points to the result buffer.  The region is statically
 *		allocated by the function.
 *
 *	*protop:
 *		protocol # to be passed to setsockopt(2).
 *
 *	*optp:	option # to be passed to setsockopt(2).
 *
 */
int
sourceroute(struct addrinfo *ai,
	    char *arg,
	    char **cpp,
	    int *protop,
	    int *optp)
{
	char *cp, *cp2, *lsrp = NULL, *lsrep = NULL;
	struct addrinfo hints, *res;
	int len, error;
	struct sockaddr_in *sin;
	register char c;
	static char lsr[44];
#ifdef INET6
	struct cmsghdr *cmsg = NULL;
	struct sockaddr_in6 *sin6;
	static char rhbuf[1024];
#endif

	/*
	 * Verify the arguments.
	 */
	if (cpp == NULL)
		return -1;

	cp = arg;

	*cpp = NULL;
	switch (ai->ai_family) {
	case AF_INET:
		lsrp = lsr;
		lsrep = lsrp + sizeof(lsr);

		/*
		 * Next, decide whether we have a loose source
		 * route or a strict source route, and fill in
		 * the begining of the option.
		 */
		if (*cp == '!') {
			cp++;
			*lsrp++ = IPOPT_SSRR;
		} else
			*lsrp++ = IPOPT_LSRR;
		if (*cp != '@')
			return -1;
		lsrp++;		/* skip over length, we'll fill it in later */
		*lsrp++ = 4;
		cp++;
		*protop = IPPROTO_IP;
		*optp = IP_OPTIONS;
		break;
#ifdef INET6
	case AF_INET6:
/* this needs to be updated for rfc2292bis */
#ifdef IPV6_PKTOPTIONS
		cmsg = inet6_rthdr_init(rhbuf, IPV6_RTHDR_TYPE_0);
		if (*cp != '@')
			return -1;
		cp++;
		*protop = IPPROTO_IPV6;
		*optp = IPV6_PKTOPTIONS;
		break;
#else
		return -1;
#endif
#endif
	default:
		return -1;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = ai->ai_family;
	hints.ai_socktype = SOCK_STREAM;

	for (c = 0;;) {
		if (c == ':')
			cp2 = 0;
		else for (cp2 = cp; (c = *cp2) != '\0'; cp2++) {
			if (c == ',') {
				*cp2++ = '\0';
				if (*cp2 == '@')
					cp2++;
			} else if (c == '@') {
				*cp2++ = '\0';
			}
#if 0	/*colon conflicts with IPv6 address*/
			else if (c == ':') {
				*cp2++ = '\0';
			}
#endif
			else
				continue;
			break;
		}
		if (!c)
			cp2 = 0;

		error = getaddrinfo(cp, NULL, &hints, &res);
		if (error) {
			fprintf(stderr, "%s: %s\n", cp, gai_strerror(error));
			return -1;
		}
		if (ai->ai_family != res->ai_family) {
			freeaddrinfo(res);
			return -1;
		}
		if (ai->ai_family == AF_INET) {
			/*
			 * Check to make sure there is space for address
			 */
			if (lsrp + 4 > lsrep) {
				freeaddrinfo(res);
				return -1;
			}
			sin = (struct sockaddr_in *)res->ai_addr;
			memcpy(lsrp, &sin->sin_addr, sizeof(struct in_addr));
			lsrp += sizeof(struct in_addr);
		}
#ifdef INET6
		else if (ai->ai_family == AF_INET6) {
			sin6 = (struct sockaddr_in6 *)res->ai_addr;
			inet6_rthdr_add(cmsg, &sin6->sin6_addr,
				IPV6_RTHDR_LOOSE);
		}
#endif
		else {
			freeaddrinfo(res);
			return -1;
		}
		freeaddrinfo(res);
		if (cp2)
			cp = cp2;
		else
			break;
	}
	if (ai->ai_family == AF_INET) {
		/* record the last hop */
		if (lsrp + 4 > lsrep)
			return -1;
		sin = (struct sockaddr_in *)ai->ai_addr;
		memcpy(lsrp, &sin->sin_addr, sizeof(struct in_addr));
		lsrp += sizeof(struct in_addr);
#ifndef	sysV88
		lsr[IPOPT_OLEN] = lsrp - lsr;
		if (lsr[IPOPT_OLEN] <= 7 || lsr[IPOPT_OLEN] > 40)
			return -1;
		*lsrp++ = IPOPT_NOP;	/*32bit word align*/
		len = lsrp - lsr;
		*cpp = lsr;
#else
		ipopt.io_len = lsrp - lsr;
		if (ipopt.io_len <= 5)	/*is 3 better?*/
			return -1;
		*cpp = (char 8)&ipopt;
#endif
	}
#ifdef INET6
	else if (ai->ai_family == AF_INET6) {
		inet6_rthdr_lasthop(cmsg, IPV6_RTHDR_LOOSE);
		len = cmsg->cmsg_len;
		*cpp = rhbuf;
	}
#endif
	else
		return -1;
	return len;
}
#endif
