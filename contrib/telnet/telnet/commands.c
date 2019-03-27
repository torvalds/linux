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
static const char sccsid[] = "@(#)commands.c	8.4 (Berkeley) 5/30/95";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/un.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/telnet.h>
#include <arpa/inet.h>

#include "general.h"

#include "ring.h"

#include "externs.h"
#include "defines.h"
#include "types.h"
#include "misc.h"

#ifdef	AUTHENTICATION
#include <libtelnet/auth.h>
#endif
#ifdef	ENCRYPTION
#include <libtelnet/encrypt.h>
#endif

#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#ifndef       MAXHOSTNAMELEN
#define       MAXHOSTNAMELEN 256
#endif

typedef int (*intrtn_t)(int, char **);

#ifdef	AUTHENTICATION
extern int auth_togdebug(int);
#endif
#ifdef	ENCRYPTION
extern int EncryptAutoEnc(int);
extern int EncryptAutoDec(int);
extern int EncryptDebug(int);
extern int EncryptVerbose(int);
#endif	/* ENCRYPTION */
#if	defined(IPPROTO_IP) && defined(IP_TOS)
int tos = -1;
#endif	/* defined(IPPROTO_IP) && defined(IP_TOS) */

char	*hostname;
static char _hostname[MAXHOSTNAMELEN];

static int help(int, char **);
static int call(intrtn_t, ...);
static void cmdrc(char *, char *);
#ifdef INET6
static int switch_af(struct addrinfo **);
#endif
static int togglehelp(void);
static int send_tncmd(void (*)(int, int), const char *, char *);
static int setmod(int);
static int clearmode(int);
static int modehelp(void);
static int sourceroute(struct addrinfo *, char *, unsigned char **, int *, int *, int *);

typedef struct {
	const char *name;	/* command name */
	const char *help;	/* help string (NULL for no help) */
	int	(*handler)(int, char **); /* routine which executes command */
	int	needconnect;	/* Do we need to be connected to execute? */
} Command;

static char line[256];
static char saveline[256];
static int margc;
static char *margv[20];

#ifdef OPIE
#include <sys/wait.h>
#define PATH_OPIEKEY	"/usr/bin/opiekey"
static int
opie_calc(int argc, char *argv[])
{
	int status;

	if(argc != 3) {
		printf("%s sequence challenge\n", argv[0]);
		return (0);
	}

	switch(fork()) {
	case 0:
		execv(PATH_OPIEKEY, argv);
		exit (1);
	case -1:
		perror("fork");
		break;
	default:
		(void) wait(&status);
		if (WIFEXITED(status))
			return (WEXITSTATUS(status));
	}
	return (0);
}
#endif

static void
makeargv(void)
{
    char *cp, *cp2, c;
    char **argp = margv;

    margc = 0;
    cp = line;
    if (*cp == '!') {		/* Special case shell escape */
	strcpy(saveline, line);	/* save for shell command */
	*argp++ = strdup("!");		/* No room in string to get this */
	margc++;
	cp++;
    }
    while ((c = *cp)) {
	int inquote = 0;
	while (isspace(c))
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
		} else if (isspace(c))
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

static int
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
static const char *
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
    const char	*name;		/* How user refers to it (case independent) */
    const char	*help;		/* Help information (0 ==> no help) */
    int		needconnect;	/* Need to be connected */
    int		narg;		/* Number of arguments */
    int		(*handler)(char *, ...); /* Routine to perform (for special ops) */
    int		nbyte;		/* Number of bytes to send this command */
    int		what;		/* Character to be sent (<0 ==> special) */
};


static int
	send_esc(void),
	send_help(void),
	send_docmd(char *),
	send_dontcmd(char *),
	send_willcmd(char *),
	send_wontcmd(char *);

static struct sendlist Sendlist[] = {
    { "ao",	"Send Telnet Abort output",	1, 0, NULL, 2, AO },
    { "ayt",	"Send Telnet 'Are You There'",	1, 0, NULL, 2, AYT },
    { "brk",	"Send Telnet Break",		1, 0, NULL, 2, BREAK },
    { "break",	NULL,				1, 0, NULL, 2, BREAK },
    { "ec",	"Send Telnet Erase Character",	1, 0, NULL, 2, EC },
    { "el",	"Send Telnet Erase Line",	1, 0, NULL, 2, EL },
    { "escape",	"Send current escape character",1, 0, (int (*)(char *, ...))send_esc, 1, 0 },
    { "ga",	"Send Telnet 'Go Ahead' sequence", 1, 0, NULL, 2, GA },
    { "ip",	"Send Telnet Interrupt Process",1, 0, NULL, 2, IP },
    { "intp",	NULL,				1, 0, NULL, 2, IP },
    { "interrupt", NULL,			1, 0, NULL, 2, IP },
    { "intr",	NULL,				1, 0, NULL, 2, IP },
    { "nop",	"Send Telnet 'No operation'",	1, 0, NULL, 2, NOP },
    { "eor",	"Send Telnet 'End of Record'",	1, 0, NULL, 2, EOR },
    { "abort",	"Send Telnet 'Abort Process'",	1, 0, NULL, 2, ABORT },
    { "susp",	"Send Telnet 'Suspend Process'",1, 0, NULL, 2, SUSP },
    { "eof",	"Send Telnet End of File Character", 1, 0, NULL, 2, xEOF },
    { "synch",	"Perform Telnet 'Synch operation'", 1, 0, (int (*)(char *, ...))dosynch, 2, 0 },
    { "getstatus", "Send request for STATUS",	1, 0, (int (*)(char *, ...))get_status, 6, 0 },
    { "?",	"Display send options",		0, 0, (int (*)(char *, ...))send_help, 0, 0 },
    { "help",	NULL,				0, 0, (int (*)(char *, ...))send_help, 0, 0 },
    { "do",	NULL,				0, 1, (int (*)(char *, ...))send_docmd, 3, 0 },
    { "dont",	NULL,				0, 1, (int (*)(char *, ...))send_dontcmd, 3, 0 },
    { "will",	NULL,				0, 1, (int (*)(char *, ...))send_willcmd, 3, 0 },
    { "wont",	NULL,				0, 1, (int (*)(char *, ...))send_wontcmd, 3, 0 },
    { NULL,	NULL,				0, 0, NULL, 0, 0 }
};

#define	GETSEND(name) ((struct sendlist *) genget(name, (char **) Sendlist, \
				sizeof(struct sendlist)))

static int
sendcmd(int argc, char *argv[])
{
    int count;		/* how many bytes we are going to need to send */
    int i;
    struct sendlist *s;	/* pointer to current command */
    int success = 0;
    int needconnect = 0;

    if (argc < 2) {
	printf("need at least one argument for 'send' command\n");
	printf("'send ?' for help\n");
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
	    printf("Unknown send argument '%s'\n'send ?' for help.\n",
			argv[i]);
	    return 0;
	} else if (Ambiguous((void *)s)) {
	    printf("Ambiguous send argument '%s'\n'send ?' for help.\n",
			argv[i]);
	    return 0;
	}
	if (i + s->narg >= argc) {
	    fprintf(stderr,
	    "Need %d argument%s to 'send %s' command.  'send %s ?' for help.\n",
		s->narg, s->narg == 1 ? "" : "s", s->name, s->name);
	    return 0;
	}
	count += s->nbyte;
	if ((void *)s->handler == (void *)send_help) {
	    send_help();
	    return 0;
	}

	i += s->narg;
	needconnect += s->needconnect;
    }
    if (!connected && needconnect) {
	printf("?Need to be connected first.\n");
	printf("'send ?' for help\n");
	return 0;
    }
    /* Now, do we have enough room? */
    if (NETROOM() < count) {
	printf("There is not enough room in the buffer TO the network\n");
	printf("to process your request.  Nothing will be done.\n");
	printf("('send synch' will throw away most data in the network\n");
	printf("buffer, if this might help.)\n");
	return 0;
    }
    /* OK, they are all OK, now go through again and actually send */
    count = 0;
    for (i = 1; i < argc; i++) {
	if ((s = GETSEND(argv[i])) == 0) {
	    fprintf(stderr, "Telnet 'send' error - argument disappeared!\n");
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
send_esc(void)
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
send_dontcmd(name)
    char *name;
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

static int
send_tncmd(void (*func)(int, int), const char *cmd, char *name)
{
    char **cpp;
    extern char *telopts[];
    int val = 0;

    if (isprefix(name, "help") || isprefix(name, "?")) {
	int col, len;

	printf("usage: send %s <value|option>\n", cmd);
	printf("\"value\" must be from 0 to 255\n");
	printf("Valid options are:\n\t");

	col = 8;
	for (cpp = telopts; *cpp; cpp++) {
	    len = strlen(*cpp) + 3;
	    if (col + len > 65) {
		printf("\n\t");
		col = 8;
	    }
	    printf(" \"%s\"", *cpp);
	    col += len;
	}
	printf("\n");
	return 0;
    }
    cpp = (char **)genget(name, telopts, sizeof(char *));
    if (Ambiguous(cpp)) {
	fprintf(stderr,"'%s': ambiguous argument ('send %s ?' for help).\n",
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
	    fprintf(stderr, "'%s': unknown argument ('send %s ?' for help).\n",
					name, cmd);
	    return 0;
	} else if (val < 0 || val > 255) {
	    fprintf(stderr, "'%s': bad value ('send %s ?' for help).\n",
					name, cmd);
	    return 0;
	}
    }
    if (!connected) {
	printf("?Need to be connected first.\n");
	return 0;
    }
    (*func)(val, 1);
    return 1;
}

static int
send_help(void)
{
    struct sendlist *s;	/* pointer to current command */
    for (s = Sendlist; s->name; s++) {
	if (s->help)
	    printf("%-15s %s\n", s->name, s->help);
    }
    return(0);
}

/*
 * The following are the routines and data structures referred
 * to by the arguments to the "toggle" command.
 */

static int
lclchars(void)
{
    donelclchars = 1;
    return 1;
}

static int
togdebug(void)
{
#ifndef	NOT43
    if (net > 0 &&
	(SetSockOpt(net, SOL_SOCKET, SO_DEBUG, telnet_debug)) < 0) {
	    perror("setsockopt (SO_DEBUG)");
    }
#else	/* NOT43 */
    if (telnet_debug) {
	if (net > 0 && SetSockOpt(net, SOL_SOCKET, SO_DEBUG, 1) < 0)
	    perror("setsockopt (SO_DEBUG)");
    } else
	printf("Cannot turn off socket debugging\n");
#endif	/* NOT43 */
    return 1;
}


static int
togcrlf(void)
{
    if (crlf) {
	printf("Will send carriage returns as telnet <CR><LF>.\n");
    } else {
	printf("Will send carriage returns as telnet <CR><NUL>.\n");
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
	    printf("Already operating in binary mode with remote host.\n");
	} else {
	    printf("Negotiating binary mode with remote host.\n");
	    tel_enter_binary(3);
	}
    } else {
	if (my_want_state_is_wont(TELOPT_BINARY) &&
					my_want_state_is_dont(TELOPT_BINARY)) {
	    printf("Already in network ascii mode with remote host.\n");
	} else {
	    printf("Negotiating network ascii mode with remote host.\n");
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
	    printf("Already receiving in binary mode.\n");
	} else {
	    printf("Negotiating binary mode on input.\n");
	    tel_enter_binary(1);
	}
    } else {
	if (my_want_state_is_dont(TELOPT_BINARY)) {
	    printf("Already receiving in network ascii mode.\n");
	} else {
	    printf("Negotiating network ascii mode on input.\n");
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
	    printf("Already transmitting in binary mode.\n");
	} else {
	    printf("Negotiating binary mode on output.\n");
	    tel_enter_binary(2);
	}
    } else {
	if (my_want_state_is_wont(TELOPT_BINARY)) {
	    printf("Already transmitting in network ascii mode.\n");
	} else {
	    printf("Negotiating network ascii mode on output.\n");
	    tel_leave_binary(2);
	}
    }
    return 1;
}

struct togglelist {
    const char	*name;		/* name of toggle */
    const char	*help;		/* help message */
    int		(*handler)(int); /* routine to do actual setting */
    int		*variable;
    const char	*actionexplanation;
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
#ifdef	AUTHENTICATION
    { "autologin",
	"automatic sending of login and/or authentication info",
	    0,
		&autologin,
		    "send login name and/or authentication information" },
    { "authdebug",
	"Toggle authentication debugging",
	    auth_togdebug,
		0,
		     "print authentication debugging information" },
#endif
#ifdef	ENCRYPTION
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
	"Toggle verbose encryption output",
	    EncryptVerbose,
		0,
		    "print verbose encryption output" },
    { "encdebug",
	"Toggle encryption debugging",
	    EncryptDebug,
		0,
		    "print encryption debugging information" },
#endif	/* ENCRYPTION */
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
	    (int (*)(int))togcrlf,
		&crlf,
		    0 },
    { "crmod",
	"mapping of received carriage returns",
	    0,
		&crmod,
		    "map carriage return on output" },
    { "localchars",
	"local recognition of certain control characters",
	    (int (*)(int))lclchars,
		&localchars,
		    "recognize certain control characters" },
    { " ", "", NULL, NULL, NULL },		/* empty line */
    { "debug",
	"debugging",
	    (int (*)(int))togdebug,
		&telnet_debug,
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
	"(debugging) toggle printing of hexadecimal terminal data",
	    0,
		&termdata,
		    "print hexadecimal representation of terminal traffic" },
    { "?",
	NULL,
	    (int (*)(int))togglehelp,
		NULL,
		    NULL },
    { NULL, NULL, NULL, NULL, NULL },
    { "help",
	NULL,
	    (int (*)(int))togglehelp,
		NULL,
		    NULL },
    { NULL, NULL, NULL, NULL, NULL }
};

static int
togglehelp(void)
{
    struct togglelist *c;

    for (c = Togglelist; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s toggle %s\n", c->name, c->help);
	    else
		printf("\n");
	}
    }
    printf("\n");
    printf("%-15s %s\n", "?", "display help information");
    return 0;
}

static void
settogglehelp(int set)
{
    struct togglelist *c;

    for (c = Togglelist; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s %s\n", c->name, set ? "enable" : "disable",
						c->help);
	    else
		printf("\n");
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
	    "Need an argument to 'toggle' command.  'toggle ?' for help.\n");
	return 0;
    }
    argc--;
    argv++;
    while (argc--) {
	name = *argv++;
	c = GETTOGGLE(name);
	if (Ambiguous((void *)c)) {
	    fprintf(stderr, "'%s': ambiguous argument ('toggle ?' for help).\n",
					name);
	    return 0;
	} else if (c == 0) {
	    fprintf(stderr, "'%s': unknown argument ('toggle ?' for help).\n",
					name);
	    return 0;
	} else {
	    if (c->variable) {
		*c->variable = !*c->variable;		/* invert it */
		if (c->actionexplanation) {
		    printf("%s %s.\n", *c->variable? "Will" : "Won't",
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

#ifdef	USE_TERMIO
struct termio new_tc = { 0, 0, 0, 0, {}, 0, 0 };
#endif

struct setlist {
    const char *name;			/* name */
    const char *help;			/* help information */
    void (*handler)(char *);
    cc_t *charp;			/* where it is located at */
};

static struct setlist Setlist[] = {
#ifdef	KLUDGELINEMODE
    { "echo", 	"character to toggle local echoing on/off", NULL, &echoc },
#endif
    { "escape",	"character to escape back to telnet command mode", NULL, &escape },
    { "rlogin", "rlogin escape character", 0, &rlogin },
    { "tracefile", "file to write trace information to", SetNetTrace, (cc_t *)NetTraceFile},
    { " ", "", NULL, NULL },
    { " ", "The following need 'localchars' to be toggled true", NULL, NULL },
    { "flushoutput", "character to cause an Abort Output", NULL, termFlushCharp },
    { "interrupt", "character to cause an Interrupt Process", NULL, termIntCharp },
    { "quit",	"character to cause an Abort process", NULL, termQuitCharp },
    { "eof",	"character to cause an EOF ", NULL, termEofCharp },
    { " ", "", NULL, NULL },
    { " ", "The following are for local editing in linemode", NULL, NULL },
    { "erase",	"character to use to erase a character", NULL, termEraseCharp },
    { "kill",	"character to use to erase a line", NULL, termKillCharp },
    { "lnext",	"character to use for literal next", NULL, termLiteralNextCharp },
    { "susp",	"character to cause a Suspend Process", NULL, termSuspCharp },
    { "reprint", "character to use for line reprint", NULL, termRprntCharp },
    { "worderase", "character to use to erase a word", NULL, termWerasCharp },
    { "start",	"character to use for XON", NULL, termStartCharp },
    { "stop",	"character to use for XOFF", NULL, termStopCharp },
    { "forw1",	"alternate end of line character", NULL, termForw1Charp },
    { "forw2",	"alternate end of line character", NULL, termForw2Charp },
    { "ayt",	"alternate AYT character", NULL, termAytCharp },
    { "baudrate", "set remote baud rate", DoBaudRate, ComPortBaudRate },
    { NULL, NULL, NULL, NULL }
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
		printf("Telnet rlogin escape character is '%s'.\n",
					control(rlogin));
	} else {
		escape = (s && *s) ? special(s) : _POSIX_VDISABLE;
		printf("Telnet escape character is '%s'.\n", control(escape));
	}
}

static int
setcmd(int argc, char *argv[])
{
    int value;
    struct setlist *ct;
    struct togglelist *c;

    if (argc < 2 || argc > 3) {
	printf("Format is 'set Name Value'\n'set ?' for help.\n");
	return 0;
    }
    if ((argc == 2) && (isprefix(argv[1], "?") || isprefix(argv[1], "help"))) {
	for (ct = Setlist; ct->name; ct++)
	    printf("%-15s %s\n", ct->name, ct->help);
	printf("\n");
	settogglehelp(1);
	printf("%-15s %s\n", "?", "display help information");
	return 0;
    }

    ct = getset(argv[1]);
    if (ct == 0) {
	c = GETTOGGLE(argv[1]);
	if (c == 0) {
	    fprintf(stderr, "'%s': unknown argument ('set ?' for help).\n",
			argv[1]);
	    return 0;
	} else if (Ambiguous((void *)c)) {
	    fprintf(stderr, "'%s': ambiguous argument ('set ?' for help).\n",
			argv[1]);
	    return 0;
	}
	if (c->variable) {
	    if ((argc == 2) || (strcmp("on", argv[2]) == 0))
		*c->variable = 1;
	    else if (strcmp("off", argv[2]) == 0)
		*c->variable = 0;
	    else {
		printf("Format is 'set togglename [on|off]'\n'set ?' for help.\n");
		return 0;
	    }
	    if (c->actionexplanation) {
		printf("%s %s.\n", *c->variable? "Will" : "Won't",
							c->actionexplanation);
	    }
	}
	if (c->handler)
	    (*c->handler)(1);
    } else if (argc != 3) {
	printf("Format is 'set Name Value'\n'set ?' for help.\n");
	return 0;
    } else if (Ambiguous((void *)ct)) {
	fprintf(stderr, "'%s': ambiguous argument ('set ?' for help).\n",
			argv[1]);
	return 0;
    } else if (ct->handler) {
	(*ct->handler)(argv[2]);
	printf("%s set to \"%s\".\n", ct->name, (char *)ct->charp);
    } else {
	if (strcmp("off", argv[2])) {
	    value = special(argv[2]);
	} else {
	    value = _POSIX_VDISABLE;
	}
	*(ct->charp) = (cc_t)value;
	printf("%s character is '%s'.\n", ct->name, control(*(ct->charp)));
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
	    "Need an argument to 'unset' command.  'unset ?' for help.\n");
	return 0;
    }
    if (isprefix(argv[1], "?") || isprefix(argv[1], "help")) {
	for (ct = Setlist; ct->name; ct++)
	    printf("%-15s %s\n", ct->name, ct->help);
	printf("\n");
	settogglehelp(0);
	printf("%-15s %s\n", "?", "display help information");
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
		fprintf(stderr, "'%s': unknown argument ('unset ?' for help).\n",
			name);
		return 0;
	    } else if (Ambiguous((void *)c)) {
		fprintf(stderr, "'%s': ambiguous argument ('unset ?' for help).\n",
			name);
		return 0;
	    }
	    if (c->variable) {
		*c->variable = 0;
		if (c->actionexplanation) {
		    printf("%s %s.\n", *c->variable? "Will" : "Won't",
							c->actionexplanation);
		}
	    }
	    if (c->handler)
		(*c->handler)(0);
	} else if (Ambiguous((void *)ct)) {
	    fprintf(stderr, "'%s': ambiguous argument ('unset ?' for help).\n",
			name);
	    return 0;
	} else if (ct->handler) {
	    (*ct->handler)(0);
	    printf("%s reset to \"%s\".\n", ct->name, (char *)ct->charp);
	} else {
	    *(ct->charp) = _POSIX_VDISABLE;
	    printf("%s character is '%s'.\n", ct->name, control(*(ct->charp)));
	}
    }
    return 1;
}

/*
 * The following are the data structures and routines for the
 * 'mode' command.
 */
#ifdef	KLUDGELINEMODE
extern int kludgelinemode;

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
dolinemode(void)
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
docharmode(void)
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
    extern int linemode;

    if (my_want_state_is_wont(TELOPT_LINEMODE)) {
	printf("?Need to have LINEMODE option enabled first.\n");
	printf("'mode ?' for help.\n");
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
setmod(int bit)
{
    return dolmmode(bit, 1);
}

static int
clearmode(int bit)
{
    return dolmmode(bit, 0);
}

struct modelist {
	const char	*name;	/* command name */
	const char	*help;	/* help string */
	int	(*handler)(int);/* routine which executes command */
	int	needconnect;	/* Do we need to be connected to execute? */
	int	arg1;
};

static struct modelist ModeList[] = {
    { "character", "Disable LINEMODE option",	(int (*)(int))docharmode, 1, 0 },
#ifdef	KLUDGELINEMODE
    { "",	"(or disable obsolete line-by-line mode)", NULL, 0, 0 },
#endif
    { "line",	"Enable LINEMODE option",	(int (*)(int))dolinemode, 1, 0 },
#ifdef	KLUDGELINEMODE
    { "",	"(or enable obsolete line-by-line mode)", NULL, 0, 0 },
#endif
    { "", "", NULL, 0, 0 },
    { "",	"These require the LINEMODE option to be enabled", NULL, 0, 0 },
    { "isig",	"Enable signal trapping",	setmod, 1, MODE_TRAPSIG },
    { "+isig",	0,				setmod, 1, MODE_TRAPSIG },
    { "-isig",	"Disable signal trapping",	clearmode, 1, MODE_TRAPSIG },
    { "edit",	"Enable character editing",	setmod, 1, MODE_EDIT },
    { "+edit",	0,				setmod, 1, MODE_EDIT },
    { "-edit",	"Disable character editing",	clearmode, 1, MODE_EDIT },
    { "softtabs", "Enable tab expansion",	setmod, 1, MODE_SOFT_TAB },
    { "+softtabs", 0,				setmod, 1, MODE_SOFT_TAB },
    { "-softtabs", "Disable character editing",	clearmode, 1, MODE_SOFT_TAB },
    { "litecho", "Enable literal character echo", setmod, 1, MODE_LIT_ECHO },
    { "+litecho", 0,				setmod, 1, MODE_LIT_ECHO },
    { "-litecho", "Disable literal character echo", clearmode, 1, MODE_LIT_ECHO },
    { "help",	0,				(int (*)(int))modehelp, 0, 0 },
#ifdef	KLUDGELINEMODE
    { "kludgeline", 0,				(int (*)(int))dokludgemode, 1, 0 },
#endif
    { "", "", NULL, 0, 0 },
    { "?",	"Print help information",	(int (*)(int))modehelp, 0, 0 },
    { NULL, NULL, NULL, 0, 0 },
};


static int
modehelp(void)
{
    struct modelist *mt;

    printf("format is:  'mode Mode', where 'Mode' is one of:\n\n");
    for (mt = ModeList; mt->name; mt++) {
	if (mt->help) {
	    if (*mt->help)
		printf("%-15s %s\n", mt->name, mt->help);
	    else
		printf("\n");
	}
    }
    return 0;
}

#define	GETMODECMD(name) (struct modelist *) \
		genget(name, (char **) ModeList, sizeof(struct modelist))

static int
modecmd(int argc, char *argv[])
{
    struct modelist *mt;

    if (argc != 2) {
	printf("'mode' command requires an argument\n");
	printf("'mode ?' for help.\n");
    } else if ((mt = GETMODECMD(argv[1])) == 0) {
	fprintf(stderr, "Unknown mode '%s' ('mode ?' for help).\n", argv[1]);
    } else if (Ambiguous((void *)mt)) {
	fprintf(stderr, "Ambiguous mode '%s' ('mode ?' for help).\n", argv[1]);
    } else if (mt->needconnect && !connected) {
	printf("?Need to be connected first.\n");
	printf("'mode ?' for help.\n");
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
			    printf(" %s.\n", tl->actionexplanation); \
			}

#define	doset(sl)   if (sl->name && *sl->name != ' ') { \
			if (sl->handler == 0) \
			    printf("%-15s [%s]\n", sl->name, control(*sl->charp)); \
			else \
			    printf("%-15s \"%s\"\n", sl->name, (char *)sl->charp); \
		    }

    if (argc == 1) {
	for (tl = Togglelist; tl->name; tl++) {
	    dotog(tl);
	}
	printf("\n");
	for (sl = Setlist; sl->name; sl++) {
	    doset(sl);
	}
    } else {
	int i;

	for (i = 1; i < argc; i++) {
	    sl = getset(argv[i]);
	    tl = GETTOGGLE(argv[i]);
	    if (Ambiguous((void *)sl) || Ambiguous((void *)tl)) {
		printf("?Ambiguous argument '%s'.\n", argv[i]);
		return 0;
	    } else if (!sl && !tl) {
		printf("?Unknown argument '%s'.\n", argv[i]);
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
#ifdef	ENCRYPTION
    EncryptStatus();
#endif	/* ENCRYPTION */
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
	    "Deprecated usage - please use 'set escape%s%s' in the future.\n",
				(argc > 2)? " ":"", (argc > 2)? argv[1]: "");
	if (argc > 2)
		arg = argv[1];
	else {
		printf("new escape character: ");
		(void) fgets(buf, sizeof(buf), stdin);
		arg = buf;
	}
	if (arg[0] != '\0')
		escape = arg[0];
	(void) fflush(stdout);
	return 1;
}

static int
togcrmod(void)
{
    crmod = !crmod;
    printf("Deprecated usage - please use 'toggle crmod' in the future.\n");
    printf("%s map carriage return on output.\n", crmod ? "Will" : "Won't");
    (void) fflush(stdout);
    return 1;
}

static int
suspend(void)
{
#ifdef	SIGTSTP
    setcommandmode();
    {
	long oldrows, oldcols, newrows, newcols, err_;

	err_ = (TerminalWindowSize(&oldrows, &oldcols) == 0) ? 1 : 0;
	(void) kill(0, SIGTSTP);
	/*
	 * If we didn't get the window size before the SUSPEND, but we
	 * can get them now (?), then send the NAWS to make sure that
	 * we are set up for the right window size.
	 */
	if (TerminalWindowSize(&newrows, &newcols) && connected &&
	    (err_ || ((oldrows != newrows) || (oldcols != newcols)))) {
		sendnaws();
	}
    }
    /* reget parameters in case they were changed */
    TerminalSaveState();
    setconnmode(0);
#else
    printf("Suspend is not supported.  Try the '!' command instead\n");
#endif
    return 1;
}

static int
shell(int argc, char *argv[] __unused)
{
    long oldrows, oldcols, newrows, newcols, err_;

    setcommandmode();

    err_ = (TerminalWindowSize(&oldrows, &oldcols) == 0) ? 1 : 0;
    switch(vfork()) {
    case -1:
	perror("Fork failed\n");
	break;

    case 0:
	{
	    /*
	     * Fire up the shell in the child.
	     */
	    const char *shellp, *shellname;

	    shellp = getenv("SHELL");
	    if (shellp == NULL)
		shellp = "/bin/sh";
	    if ((shellname = strrchr(shellp, '/')) == 0)
		shellname = shellp;
	    else
		shellname++;
	    if (argc > 1)
		execl(shellp, shellname, "-c", &saveline[1], (char *)0);
	    else
		execl(shellp, shellname, (char *)0);
	    perror("Execl");
	    _exit(1);
	}
    default:
	    (void)wait((int *)0);	/* Wait for the shell to complete */

	    if (TerminalWindowSize(&newrows, &newcols) && connected &&
		(err_ || ((oldrows != newrows) || (oldcols != newcols)))) {
		    sendnaws();
	    }
	    break;
    }
    return 1;
}

static int
bye(int argc, char *argv[])
{
    extern int resettermname;

    if (connected) {
	(void) shutdown(net, 2);
	printf("Connection closed.\n");
	(void) NetClose(net);
	connected = 0;
	resettermname = 1;
#ifdef	AUTHENTICATION
#ifdef	ENCRYPTION
	auth_encrypt_connect(connected);
#endif
#endif
	/* reset options */
	tninit();
    }
    if ((argc != 2) || (strcmp(argv[1], "fromquit") != 0)) {
	longjmp(toplevel, 1);
	/* NOTREACHED */
    }
    return 1;			/* Keep lint, etc., happy */
}

void
quit(void)
{
	(void) call(bye, "bye", "fromquit", 0);
	Exit(0);
}

static int
logout(void)
{
	send_do(TELOPT_LOGOUT, 1);
	(void) netflush();
	return 1;
}


/*
 * The SLC command.
 */

struct slclist {
	const char	*name;
	const char	*help;
	void	(*handler)(int);
	int	arg;
};

static void slc_help(void);

struct slclist SlcList[] = {
    { "export",	"Use local special character definitions",
						(void (*)(int))slc_mode_export,	0 },
    { "import",	"Use remote special character definitions",
						slc_mode_import,	1 },
    { "check",	"Verify remote special character definitions",
						slc_mode_import,	0 },
    { "help",	NULL,				(void (*)(int))slc_help,		0 },
    { "?",	"Print help information",	(void (*)(int))slc_help,		0 },
    { NULL, NULL, NULL, 0 },
};

static void
slc_help(void)
{
    struct slclist *c;

    for (c = SlcList; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s\n", c->name, c->help);
	    else
		printf("\n");
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
slccmd(int argc, char *argv[])
{
    struct slclist *c;

    if (argc != 2) {
	fprintf(stderr,
	    "Need an argument to 'slc' command.  'slc ?' for help.\n");
	return 0;
    }
    c = getslc(argv[1]);
    if (c == 0) {
	fprintf(stderr, "'%s': unknown argument ('slc ?' for help).\n",
    				argv[1]);
	return 0;
    }
    if (Ambiguous((void *)c)) {
	fprintf(stderr, "'%s': ambiguous argument ('slc ?' for help).\n",
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
	const char	*name;
	const char	*help;
	void	(*handler)(unsigned char *, unsigned char *);
	int	narg;
};

extern struct env_lst *
	env_define(const unsigned char *, unsigned char *);
extern void
	env_undefine(unsigned char *),
	env_export(const unsigned char *),
	env_unexport(const unsigned char *),
	env_send(unsigned char *),
#if defined(OLD_ENVIRON) && defined(ENV_HACK)
	env_varval(unsigned char *),
#endif
	env_list(void);
static void
	env_help(void);

struct envlist EnvList[] = {
    { "define",	"Define an environment variable",
						(void (*)(unsigned char *, unsigned char *))env_define,	2 },
    { "undefine", "Undefine an environment variable",
						(void (*)(unsigned char *, unsigned char *))env_undefine,	1 },
    { "export",	"Mark an environment variable for automatic export",
						(void (*)(unsigned char *, unsigned char *))env_export,	1 },
    { "unexport", "Don't mark an environment variable for automatic export",
						(void (*)(unsigned char *, unsigned char *))env_unexport,	1 },
    { "send",	"Send an environment variable", (void (*)(unsigned char *, unsigned char *))env_send,	1 },
    { "list",	"List the current environment variables",
						(void (*)(unsigned char *, unsigned char *))env_list,	0 },
#if defined(OLD_ENVIRON) && defined(ENV_HACK)
    { "varval", "Reverse VAR and VALUE (auto, right, wrong, status)",
						(void (*)(unsigned char *, unsigned char *))env_varval,    1 },
#endif
    { "help",	NULL,				(void (*)(unsigned char *, unsigned char *))env_help,		0 },
    { "?",	"Print help information",	(void (*)(unsigned char *, unsigned char *))env_help,		0 },
    { NULL, NULL, NULL, 0 },
};

static void
env_help(void)
{
    struct envlist *c;

    for (c = EnvList; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s\n", c->name, c->help);
	    else
		printf("\n");
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
env_cmd(int argc, char *argv[])
{
    struct envlist *c;

    if (argc < 2) {
	fprintf(stderr,
	    "Need an argument to 'environ' command.  'environ ?' for help.\n");
	return 0;
    }
    c = getenvcmd(argv[1]);
    if (c == 0) {
	fprintf(stderr, "'%s': unknown argument ('environ ?' for help).\n",
    				argv[1]);
	return 0;
    }
    if (Ambiguous((void *)c)) {
	fprintf(stderr, "'%s': ambiguous argument ('environ ?' for help).\n",
    				argv[1]);
	return 0;
    }
    if (c->narg + 2 != argc) {
	fprintf(stderr,
	    "Need %s%d argument%s to 'environ %s' command.  'environ ?' for help.\n",
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

static struct env_lst *
env_find(const unsigned char *var)
{
	struct env_lst *ep;

	for (ep = envlisthead.next; ep; ep = ep->next) {
		if (strcmp(ep->var, var) == 0)
			return(ep);
	}
	return(NULL);
}

void
env_init(void)
{
	extern char **environ;
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
	if ((ep = env_find("DISPLAY"))
	    && ((*ep->value == ':')
		|| (strncmp((char *)ep->value, "unix:", 5) == 0))) {
		char hbuf[256+1];
		char *cp2 = strchr((char *)ep->value, ':');

		gethostname(hbuf, 256);
		hbuf[256] = '\0';
		cp = (char *)malloc(strlen(hbuf) + strlen(cp2) + 1);
		sprintf((char *)cp, "%s%s", hbuf, cp2);
		free(ep->value);
		ep->value = (unsigned char *)cp;
	}
	/*
	 * If USER is not defined, but LOGNAME is, then add
	 * USER with the value from LOGNAME.  By default, we
	 * don't export the USER variable.
	 */
	if ((env_find("USER") == NULL) && (ep = env_find("LOGNAME"))) {
		env_define("USER", ep->value);
		env_unexport("USER");
	}
	env_export("DISPLAY");
	env_export("PRINTER");
}

struct env_lst *
env_define(const unsigned char *var, unsigned char *value)
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
	ep->welldefined = opt_welldefined(var);
	ep->export = 1;
	ep->var = strdup(var);
	ep->value = strdup(value);
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
env_export(const unsigned char *var)
{
	struct env_lst *ep;

	if ((ep = env_find(var)))
		ep->export = 1;
}

void
env_unexport(const unsigned char *var)
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
		    "Cannot send '%s': Telnet ENVIRON option not enabled\n",
									var);
		return;
	}
	ep = env_find(var);
	if (ep == 0) {
		fprintf(stderr, "Cannot send '%s': variable not defined\n",
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
		printf("%c %-20s %s\n", ep->export ? '*' : ' ',
					ep->var, ep->value);
	}
}

unsigned char *
env_default(int init, int welldefined)
{
	static struct env_lst *nep = NULL;

	if (init) {
		nep = &envlisthead;
		return(NULL);
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
env_getvalue(const unsigned char *var)
{
	struct env_lst *ep;

	if ((ep = env_find(var)))
		return(ep->value);
	return(NULL);
}

#if defined(OLD_ENVIRON) && defined(ENV_HACK)
void
env_varval(unsigned char *what)
{
	extern int old_env_var, old_env_value, env_auto;
	int len = strlen((char *)what);

	if (len == 0)
		goto unknown;

	if (strncasecmp((char *)what, "status", len) == 0) {
		if (env_auto)
			printf("%s%s", "VAR and VALUE are/will be ",
					"determined automatically\n");
		if (old_env_var == OLD_ENV_VAR)
			printf("VAR and VALUE set to correct definitions\n");
		else
			printf("VAR and VALUE definitions are reversed\n");
	} else if (strncasecmp((char *)what, "auto", len) == 0) {
		env_auto = 1;
		old_env_var = OLD_ENV_VALUE;
		old_env_value = OLD_ENV_VAR;
	} else if (strncasecmp((char *)what, "right", len) == 0) {
		env_auto = 0;
		old_env_var = OLD_ENV_VAR;
		old_env_value = OLD_ENV_VALUE;
	} else if (strncasecmp((char *)what, "wrong", len) == 0) {
		env_auto = 0;
		old_env_var = OLD_ENV_VALUE;
		old_env_value = OLD_ENV_VAR;
	} else {
unknown:
		printf("Unknown \"varval\" command. (\"auto\", \"right\", \"wrong\", \"status\")\n");
	}
}
#endif

#ifdef	AUTHENTICATION
/*
 * The AUTHENTICATE command.
 */

struct authlist {
	const char	*name;
	const char	*help;
	int	(*handler)(char *);
	int	narg;
};

extern int
	auth_enable(char *),
	auth_disable(char *),
	auth_status(void);
static int
	auth_help(void);

struct authlist AuthList[] = {
    { "status",	"Display current status of authentication information",
						(int (*)(char *))auth_status,	0 },
    { "disable", "Disable an authentication type ('auth disable ?' for more)",
						auth_disable,	1 },
    { "enable", "Enable an authentication type ('auth enable ?' for more)",
						auth_enable,	1 },
    { "help",	NULL,				(int (*)(char *))auth_help,		0 },
    { "?",	"Print help information",	(int (*)(char *))auth_help,		0 },
    { NULL, NULL, NULL, 0 },
};

static int
auth_help(void)
{
    struct authlist *c;

    for (c = AuthList; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s\n", c->name, c->help);
	    else
		printf("\n");
	}
    }
    return 0;
}

int
auth_cmd(int argc, char *argv[])
{
    struct authlist *c;

    if (argc < 2) {
	fprintf(stderr,
	    "Need an argument to 'auth' command.  'auth ?' for help.\n");
	return 0;
    }

    c = (struct authlist *)
		genget(argv[1], (char **) AuthList, sizeof(struct authlist));
    if (c == 0) {
	fprintf(stderr, "'%s': unknown argument ('auth ?' for help).\n",
    				argv[1]);
	return 0;
    }
    if (Ambiguous((void *)c)) {
	fprintf(stderr, "'%s': ambiguous argument ('auth ?' for help).\n",
    				argv[1]);
	return 0;
    }
    if (c->narg + 2 != argc) {
	fprintf(stderr,
	    "Need %s%d argument%s to 'auth %s' command.  'auth ?' for help.\n",
		c->narg < argc + 2 ? "only " : "",
		c->narg, c->narg == 1 ? "" : "s", c->name);
	return 0;
    }
    return((*c->handler)(argv[2]));
}
#endif

#ifdef	ENCRYPTION
/*
 * The ENCRYPT command.
 */

struct encryptlist {
	const char	*name;
	const char	*help;
	int	(*handler)(char *, char *);
	int	needconnect;
	int	minarg;
	int	maxarg;
};

extern int
	EncryptEnable(char *, char *),
	EncryptDisable(char *, char *),
	EncryptType(char *, char *),
	EncryptStart(char *),
	EncryptStartInput(void),
	EncryptStartOutput(void),
	EncryptStop(char *),
	EncryptStopInput(void),
	EncryptStopOutput(void),
	EncryptStatus(void);
static int
	EncryptHelp(void);

struct encryptlist EncryptList[] = {
    { "enable", "Enable encryption. ('encrypt enable ?' for more)",
						EncryptEnable, 1, 1, 2 },
    { "disable", "Disable encryption. ('encrypt enable ?' for more)",
						EncryptDisable, 0, 1, 2 },
    { "type", "Set encryption type. ('encrypt type ?' for more)",
						EncryptType, 0, 1, 1 },
    { "start", "Start encryption. ('encrypt start ?' for more)",
						(int (*)(char *, char *))EncryptStart, 1, 0, 1 },
    { "stop", "Stop encryption. ('encrypt stop ?' for more)",
						(int (*)(char *, char *))EncryptStop, 1, 0, 1 },
    { "input", "Start encrypting the input stream",
						(int (*)(char *, char *))EncryptStartInput, 1, 0, 0 },
    { "-input", "Stop encrypting the input stream",
						(int (*)(char *, char *))EncryptStopInput, 1, 0, 0 },
    { "output", "Start encrypting the output stream",
						(int (*)(char *, char *))EncryptStartOutput, 1, 0, 0 },
    { "-output", "Stop encrypting the output stream",
						(int (*)(char *, char *))EncryptStopOutput, 1, 0, 0 },

    { "status",	"Display current status of authentication information",
						(int (*)(char *, char *))EncryptStatus,	0, 0, 0 },
    { "help",	NULL,				(int (*)(char *, char *))EncryptHelp,	0, 0, 0 },
    { "?",	"Print help information",	(int (*)(char *, char *))EncryptHelp,	0, 0, 0 },
    { NULL, NULL, NULL, 0, 0, 0 },
};

static int
EncryptHelp(void)
{
    struct encryptlist *c;

    for (c = EncryptList; c->name; c++) {
	if (c->help) {
	    if (*c->help)
		printf("%-15s %s\n", c->name, c->help);
	    else
		printf("\n");
	}
    }
    return 0;
}

static int
encrypt_cmd(int argc, char *argv[])
{
    struct encryptlist *c;

    if (argc < 2) {
	fprintf(stderr,
	    "Need an argument to 'encrypt' command.  'encrypt ?' for help.\n");
	return 0;
    }

    c = (struct encryptlist *)
		genget(argv[1], (char **) EncryptList, sizeof(struct encryptlist));
    if (c == 0) {
	fprintf(stderr, "'%s': unknown argument ('encrypt ?' for help).\n",
    				argv[1]);
	return 0;
    }
    if (Ambiguous((void *)c)) {
	fprintf(stderr, "'%s': ambiguous argument ('encrypt ?' for help).\n",
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
	fprintf(stderr, "to 'encrypt %s' command.  'encrypt ?' for help.\n",
		c->name);
	return 0;
    }
    if (c->needconnect && !connected) {
	if (!(argc && (isprefix(argv[2], "help") || isprefix(argv[2], "?")))) {
	    printf("?Need to be connected first.\n");
	    return 0;
	}
    }
    return ((*c->handler)(argc > 0 ? argv[2] : 0,
			argc > 1 ? argv[3] : 0));
}
#endif	/* ENCRYPTION */

/*
 * Print status about the connection.
 */
/*ARGSUSED*/
static int
status(int argc, char *argv[])
{
    if (connected) {
	printf("Connected to %s.\n", hostname);
	if ((argc < 2) || strcmp(argv[1], "notmuch")) {
	    int mode = getconnmode();

	    if (my_want_state_is_will(TELOPT_LINEMODE)) {
		printf("Operating with LINEMODE option\n");
		printf("%s line editing\n", (mode&MODE_EDIT) ? "Local" : "No");
		printf("%s catching of signals\n",
					(mode&MODE_TRAPSIG) ? "Local" : "No");
		slcstate();
#ifdef	KLUDGELINEMODE
	    } else if (kludgelinemode && my_want_state_is_dont(TELOPT_SGA)) {
		printf("Operating in obsolete linemode\n");
#endif
	    } else {
		printf("Operating in single character mode\n");
		if (localchars)
		    printf("Catching signals locally\n");
	    }
	    printf("%s character echo\n", (mode&MODE_ECHO) ? "Local" : "Remote");
	    if (my_want_state_is_will(TELOPT_LFLOW))
		printf("%s flow control\n", (mode&MODE_FLOW) ? "Local" : "No");
#ifdef	ENCRYPTION
	    encrypt_display();
#endif	/* ENCRYPTION */
	}
    } else {
	printf("No connection.\n");
    }
    printf("Escape character is '%s'.\n", control(escape));
    (void) fflush(stdout);
    return 1;
}

#ifdef	SIGINFO
/*
 * Function that gets called when SIGINFO is received.
 */
void
ayt_status(void)
{
    (void) call(status, "status", "notmuch", 0);
}
#endif

static const char *
sockaddr_ntop(struct sockaddr *sa)
{
    void *addr;
    static char addrbuf[INET6_ADDRSTRLEN];

    switch (sa->sa_family) {
    case AF_INET:
	addr = &((struct sockaddr_in *)sa)->sin_addr;
	break;
    case AF_UNIX:
	addr = &((struct sockaddr_un *)sa)->sun_path;
	break;
#ifdef INET6
    case AF_INET6:
	addr = &((struct sockaddr_in6 *)sa)->sin6_addr;
	break;
#endif
    default:
	return NULL;
    }
    inet_ntop(sa->sa_family, addr, addrbuf, sizeof(addrbuf));
    return addrbuf;
}

#if defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
static int
setpolicy(int lnet, struct addrinfo *res, char *policy)
{
	char *buf;
	int level;
	int optname;

	if (policy == NULL)
		return 0;

	buf = ipsec_set_policy(policy, strlen(policy));
	if (buf == NULL) {
		printf("%s\n", ipsec_strerror());
		return -1;
	}
	level = res->ai_family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6;
	optname = res->ai_family == AF_INET ? IP_IPSEC_POLICY : IPV6_IPSEC_POLICY;
	if (setsockopt(lnet, level, optname, buf, ipsec_get_policylen(buf)) < 0){
		perror("setsockopt");
		return -1;
	}

	free(buf);
	return 0;
}
#endif

#ifdef INET6
/*
 * When an Address Family related error happend, check if retry with
 * another AF is possible or not.
 * Return 1, if retry with another af is OK. Else, return 0.
 */
static int
switch_af(struct addrinfo **aip)
{
    int nextaf;
    struct addrinfo *ai;

    ai = *aip;
    nextaf = (ai->ai_family == AF_INET) ? AF_INET6 : AF_INET;
    do
        ai=ai->ai_next;
    while (ai != NULL && ai->ai_family != nextaf);
    *aip = ai;
    if (*aip != NULL) {
        return 1;
    }
    return 0;
}
#endif

int
tn(int argc, char *argv[])
{
    unsigned char *srp = 0;
    int proto, opt;
    int srlen;
    int srcroute = 0, result;
    char *cmd, *hostp = 0, *portp = 0, *user = 0;
    char *src_addr = NULL;
    struct addrinfo hints, *res, *res0 = NULL, *src_res, *src_res0 = NULL;
    int error = 0, af_error = 0;

    if (connected) {
	printf("?Already connected to %s\n", hostname);
	setuid(getuid());
	return 0;
    }
    if (argc < 2) {
	(void) strcpy(line, "open ");
	printf("(to) ");
	(void) fgets(&line[strlen(line)], sizeof(line) - strlen(line), stdin);
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
	    user = *argv++;
	    --argc;
	    continue;
	}
	if (strcmp(*argv, "-a") == 0) {
	    --argc; ++argv;
	    autologin = 1;
	    continue;
	}
	if (strcmp(*argv, "-s") == 0) {
	    --argc; ++argv;
	    if (argc == 0)
		goto usage;
	    src_addr = *argv++;
	    --argc;
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
	printf("usage: %s [-l user] [-a] [-s src_addr] host-name [port]\n", cmd);
	setuid(getuid());
	return 0;
    }
    if (hostp == 0)
	goto usage;

    if (src_addr != NULL) {
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(src_addr, 0, &hints, &src_res);
	if (error == EAI_NONAME) {
		hints.ai_flags = 0;
		error = getaddrinfo(src_addr, 0, &hints, &src_res);
	}
	if (error != 0) {
		fprintf(stderr, "%s: %s\n", src_addr, gai_strerror(error));
		if (error == EAI_SYSTEM)
			fprintf(stderr, "%s: %s\n", src_addr, strerror(errno));
		setuid(getuid());
		return 0;
	}
	src_res0 = src_res;
    }
    if (hostp[0] == '/') {
	struct sockaddr_un su;
	
	if (strlen(hostp) >= sizeof(su.sun_path)) {
	    fprintf(stderr, "hostname too long for unix domain socket: %s",
		    hostp);
		goto fail;
	}
	hostname = hostp;
	memset(&su, 0, sizeof su);
	su.sun_family = AF_UNIX;
	strncpy(su.sun_path, hostp, sizeof su.sun_path);
	printf("Trying %s...\n", hostp);
	net = socket(PF_UNIX, SOCK_STREAM, 0);
	if ( net < 0) {
	    perror("socket");
	    goto fail;
	}
	if (connect(net, (struct sockaddr *)&su, sizeof su) == -1) {
	    perror(su.sun_path);
	    (void) NetClose(net);
	    goto fail;
	}
	goto af_unix;
    } else if (hostp[0] == '@' || hostp[0] == '!') {
	if (
#ifdef INET6
	    family == AF_INET6 ||
#endif
	    (hostname = strrchr(hostp, ':')) == NULL)
	    hostname = strrchr(hostp, '@');
	if (hostname == NULL) {
	    hostname = hostp;
	} else {
	    hostname++;
	    srcroute = 1;
	}
    } else
        hostname = hostp;
    if (!portp) {
      telnetport = 1;
      portp = strdup("telnet");
    } else if (*portp == '-') {
      portp++;
      telnetport = 1;
    } else if (*portp == '+') {
      portp++;
      telnetport = -1;
    } else
      telnetport = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    error = getaddrinfo(hostname, portp, &hints, &res);
    if (error) {
        hints.ai_flags = AI_CANONNAME;
	error = getaddrinfo(hostname, portp, &hints, &res);
    }
    if (error != 0) {
	fprintf(stderr, "%s: %s\n", hostname, gai_strerror(error));
	if (error == EAI_SYSTEM)
	    fprintf(stderr, "%s: %s\n", hostname, strerror(errno));
	setuid(getuid());
	goto fail;
    }
    if (hints.ai_flags == AI_NUMERICHOST) {
	/* hostname has numeric */
        int gni_err = 1;

	if (doaddrlookup)
	    gni_err = getnameinfo(res->ai_addr, res->ai_addr->sa_len,
				  _hostname, sizeof(_hostname) - 1, NULL, 0,
				  NI_NAMEREQD);
	if (gni_err != 0)
	    (void) strncpy(_hostname, hostp, sizeof(_hostname) - 1);
	_hostname[sizeof(_hostname)-1] = '\0';
	hostname = _hostname;
    } else {
	/* hostname has FQDN */
	if (srcroute != 0)
	    (void) strncpy(_hostname, hostname, sizeof(_hostname) - 1);
	else if (res->ai_canonname != NULL)
	  strcpy(_hostname, res->ai_canonname);
	else
	  (void) strncpy(_hostname, hostp, sizeof(_hostname) - 1);
	_hostname[sizeof(_hostname)-1] = '\0';
	hostname = _hostname;
    }
    res0 = res;
 #ifdef INET6
 af_again:
 #endif
    if (srcroute != 0) {
        static char hostbuf[BUFSIZ];

	if (af_error == 0) { /* save intermediate hostnames for retry */
		strncpy(hostbuf, hostp, BUFSIZ - 1);
		hostbuf[BUFSIZ - 1] = '\0';
	} else
		hostp = hostbuf;
	srp = 0;
	result = sourceroute(res, hostp, &srp, &srlen, &proto, &opt);
	if (result == 0) {
#ifdef INET6
	    if (family == AF_UNSPEC && af_error == 0 &&
		switch_af(&res) == 1) {
	        af_error = 1;
		goto af_again;
	    }
#endif
	    setuid(getuid());
	    goto fail;
	} else if (result == -1) {
	    printf("Bad source route option: %s\n", hostp);
	    setuid(getuid());
	    goto fail;
	}
    }
    do {
        printf("Trying %s...\n", sockaddr_ntop(res->ai_addr));
	net = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	setuid(getuid());
	if (net < 0) {
#ifdef INET6
	    if (family == AF_UNSPEC && af_error == 0 &&
		switch_af(&res) == 1) {
	        af_error = 1;
		goto af_again;
	    }
#endif
	    perror("telnet: socket");
	    goto fail;
	}
	if (srp && setsockopt(net, proto, opt, (char *)srp, srlen) < 0)
		perror("setsockopt (source route)");
#if	defined(IPPROTO_IP) && defined(IP_TOS)
	if (res->ai_family == PF_INET) {
# if	defined(HAS_GETTOS)
	    struct tosent *tp;
	    if (tos < 0 && (tp = gettosbyname("telnet", "tcp")))
		tos = tp->t_tos;
# endif
	    if (tos < 0)
		tos = IPTOS_LOWDELAY;
	    if (tos
		&& (setsockopt(net, IPPROTO_IP, IP_TOS,
		    (char *)&tos, sizeof(int)) < 0)
		&& (errno != ENOPROTOOPT))
		    perror("telnet: setsockopt (IP_TOS) (ignored)");
	}
#endif	/* defined(IPPROTO_IP) && defined(IP_TOS) */

	if (telnet_debug && SetSockOpt(net, SOL_SOCKET, SO_DEBUG, 1) < 0) {
		perror("setsockopt (SO_DEBUG)");
	}

	if (src_addr != NULL) {
	    for (src_res = src_res0; src_res != 0; src_res = src_res->ai_next)
	        if (src_res->ai_family == res->ai_family)
		    break;
	    if (src_res == NULL)
		src_res = src_res0;
	    if (bind(net, src_res->ai_addr, src_res->ai_addrlen) == -1) {
#ifdef INET6
	        if (family == AF_UNSPEC && af_error == 0 &&
		    switch_af(&res) == 1) {
		    af_error = 1;
		    (void) NetClose(net);
		    goto af_again;
		}
#endif
		perror("bind");
		(void) NetClose(net);
		goto fail;
	    }
	}
#if defined(IPSEC) && defined(IPSEC_POLICY_IPSEC)
	if (setpolicy(net, res, ipsec_policy_in) < 0) {
		(void) NetClose(net);
		goto fail;
	}
	if (setpolicy(net, res, ipsec_policy_out) < 0) {
		(void) NetClose(net);
		goto fail;
	}
#endif

	if (connect(net, res->ai_addr, res->ai_addrlen) < 0) {
	    struct addrinfo *next;

	    next = res->ai_next;
	    /* If already an af failed, only try same af. */
	    if (af_error != 0)
		while (next != NULL && next->ai_family != res->ai_family)
		    next = next->ai_next;
	    warn("connect to address %s", sockaddr_ntop(res->ai_addr));
	    if (next != NULL) {
		res = next;
		(void) NetClose(net);
		continue;
	    }
	    warnx("Unable to connect to remote host");
	    (void) NetClose(net);
	    goto fail;
	}
	connected++;
#ifdef	AUTHENTICATION
#ifdef	ENCRYPTION
	auth_encrypt_connect(connected);
#endif
#endif
    } while (connected == 0);
    freeaddrinfo(res0);
    if (src_res0 != NULL)
        freeaddrinfo(src_res0);
    cmdrc(hostp, hostname);
 af_unix:    
    connected = 1;
    if (autologin && user == NULL) {
	struct passwd *pw;

	user = getenv("USER");
	if (user == NULL ||
	    ((pw = getpwnam(user)) && pw->pw_uid != getuid())) {
		if ((pw = getpwuid(getuid())))
			user = pw->pw_name;
		else
			user = NULL;
	}
    }
    if (user) {
	env_define("USER", user);
	env_export("USER");
    }
    (void) call(status, "status", "notmuch", 0);
    telnet(user); 
    (void) NetClose(net);
    ExitString("Connection closed by foreign host.\n",1);
    /*NOTREACHED*/
 fail:
    if (res0 != NULL)
        freeaddrinfo(res0);
    if (src_res0 != NULL)
        freeaddrinfo(src_res0);
    return 0;
}

#define HELPINDENT (sizeof ("connect"))

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
#ifdef	AUTHENTICATION
	authhelp[] =	"turn on (off) authentication ('auth ?' for more)",
#endif
#ifdef	ENCRYPTION
	encrypthelp[] =	"turn on (off) encryption ('encrypt ?' for more)",
#endif	/* ENCRYPTION */
	zhelp[] =	"suspend telnet",
#ifdef OPIE
	opiehelp[] =    "compute response to OPIE challenge",
#endif
	shellhelp[] =	"invoke a subshell",
	envhelp[] =	"change environment variables ('environ ?' for more)",
	modestring[] = "try to enter line or character mode ('mode ?' for more)";

static Command cmdtab[] = {
	{ "close",	closehelp,	bye,		1 },
	{ "logout",	logouthelp,	(int (*)(int, char **))logout,		1 },
	{ "display",	displayhelp,	display,	0 },
	{ "mode",	modestring,	modecmd,	0 },
	{ "telnet",	openhelp,	tn,		0 },
	{ "open",	openhelp,	tn,		0 },
	{ "quit",	quithelp,	(int (*)(int, char **))quit,		0 },
	{ "send",	sendhelp,	sendcmd,	0 },
	{ "set",	sethelp,	setcmd,		0 },
	{ "unset",	unsethelp,	unsetcmd,	0 },
	{ "status",	statushelp,	status,		0 },
	{ "toggle",	togglestring,	toggle,		0 },
	{ "slc",	slchelp,	slccmd,		0 },
#ifdef	AUTHENTICATION
	{ "auth",	authhelp,	auth_cmd,	0 },
#endif
#ifdef	ENCRYPTION
	{ "encrypt",	encrypthelp,	encrypt_cmd,	0 },
#endif	/* ENCRYPTION */
	{ "z",		zhelp,		(int (*)(int, char **))suspend,	0 },
	{ "!",		shellhelp,	shell,		1 },
	{ "environ",	envhelp,	env_cmd,	0 },
	{ "?",		helphelp,	help,		0 },
#ifdef OPIE
	{ "opie",       opiehelp,       opie_calc,      0 },
#endif		
	{ NULL, NULL, NULL, 0 }
};

static char	crmodhelp[] =	"deprecated command -- use 'toggle crmod' instead";
static char	escapehelp[] =	"deprecated command -- use 'set escape' instead";

static Command cmdtab2[] = {
	{ "help",	0,		help,		0 },
	{ "escape",	escapehelp,	setescape,	0 },
	{ "crmod",	crmodhelp,	(int (*)(int, char **))togcrmod,	0 },
	{ NULL, NULL, NULL, 0 }
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


static Command *
getcmd(char *name)
{
    Command *cm;

    if ((cm = (Command *) genget(name, (char **) cmdtab, sizeof(Command))))
	return cm;
    return (Command *) genget(name, (char **) cmdtab2, sizeof(Command));
}

void
command(int top, const char *tbuf, int cnt)
{
    Command *c;

    setcommandmode();
    if (!top) {
	putchar('\n');
    } else {
	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGQUIT, SIG_DFL);
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
		printf("%s\n", line);
	} else {
	getline:
	    if (rlogin != _POSIX_VDISABLE)
		printf("%s> ", prompt);
	    if (fgets(line, sizeof(line), stdin) == NULL) {
		if (feof(stdin) || ferror(stdin)) {
		    (void) quit();
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
	if (Ambiguous((void *)c)) {
	    printf("?Ambiguous command\n");
	    continue;
	}
	if (c == 0) {
	    printf("?Invalid command\n");
	    continue;
	}
	if (c->needconnect && !connected) {
	    printf("?Need to be connected first.\n");
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
help(int argc, char *argv[])
{
	Command *c;

	if (argc == 1) {
		printf("Commands may be abbreviated.  Commands are:\n\n");
		for (c = cmdtab; c->name; c++)
			if (c->help) {
				printf("%-*s\t%s\n", (int)HELPINDENT, c->name,
								    c->help);
			}
		return 0;
	}
	else while (--argc > 0) {
		char *arg;
		arg = *++argv;
		c = getcmd(arg);
		if (Ambiguous((void *)c))
			printf("?Ambiguous help command %s\n", arg);
		else if (c == (Command *)0)
			printf("?Invalid help command %s\n", arg);
		else
			printf("%s\n", c->help);
	}
	return 0;
}

static char *rcname = 0;
static char rcbuf[128];

void
cmdrc(char *m1, char *m2)
{
    Command *c;
    FILE *rcfile;
    int gotmachine = 0;
    int l1 = strlen(m1);
    int l2 = strlen(m2);
    char m1save[MAXHOSTNAMELEN];

    if (skiprc)
	return;

    strlcpy(m1save, m1, sizeof(m1save));
    m1 = m1save;

    if (rcname == 0) {
	rcname = getenv("HOME");
	if (rcname && (strlen(rcname) + 10) < sizeof(rcbuf))
	    strcpy(rcbuf, rcname);
	else
	    rcbuf[0] = '\0';
	strcat(rcbuf, "/.telnetrc");
	rcname = rcbuf;
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
	    if (!isspace(line[0]))
		gotmachine = 0;
	}
	if (gotmachine == 0) {
	    if (isspace(line[0]))
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
	if (Ambiguous((void *)c)) {
	    printf("?Ambiguous command: %s\n", margv[0]);
	    continue;
	}
	if (c == 0) {
	    printf("?Invalid command: %s\n", margv[0]);
	    continue;
	}
	/*
	 * This should never happen...
	 */
	if (c->needconnect && !connected) {
	    printf("?Need to be connected first for %s.\n", margv[0]);
	    continue;
	}
	(*c->handler)(margc, margv);
    }
    fclose(rcfile);
}

/*
 * Source route is handed in as
 *	[!]@hop1@hop2...[@|:]dst
 * If the leading ! is present, it is a
 * strict source route, otherwise it is
 * assmed to be a loose source route.
 *
 * We fill in the source route option as
 *	hop1,hop2,hop3...dest
 * and return a pointer to hop1, which will
 * be the address to connect() to.
 *
 * Arguments:
 *
 *	res:	ponter to addrinfo structure which contains sockaddr to
 *		the host to connect to.
 *
 *	arg:	pointer to route list to decipher
 *
 *	cpp: 	If *cpp is not equal to NULL, this is a
 *		pointer to a pointer to a character array
 *		that should be filled in with the option.
 *
 *	lenp:	pointer to an integer that contains the
 *		length of *cpp if *cpp != NULL.
 *
 *	protop:	pointer to an integer that should be filled in with
 *		appropriate protocol for setsockopt, as socket 
 *		protocol family.
 *
 *	optp:	pointer to an integer that should be filled in with
 *		appropriate option for setsockopt, as socket protocol
 *		family.
 *
 * Return values:
 *
 *	If the return value is 1, then all operations are
 *	successful. If the
 *	return value is -1, there was a syntax error in the
 *	option, either unknown characters, or too many hosts.
 *	If the return value is 0, one of the hostnames in the
 *	path is unknown, and *cpp is set to point to the bad
 *	hostname.
 *
 *	*cpp:	If *cpp was equal to NULL, it will be filled
 *		in with a pointer to our static area that has
 *		the option filled in.  This will be 32bit aligned.
 *
 *	*lenp:	This will be filled in with how long the option
 *		pointed to by *cpp is.
 *
 *	*protop: This will be filled in with appropriate protocol for
 *		 setsockopt, as socket protocol family.
 *
 *	*optp:	This will be filled in with appropriate option for
 *		setsockopt, as socket protocol family.
 */
static int
sourceroute(struct addrinfo *ai, char *arg, unsigned char **cpp, int *lenp, int *protop, int *optp)
{
	static char buf[1024 + ALIGNBYTES];	/*XXX*/
	unsigned char *cp, *cp2, *lsrp, *ep;
	struct sockaddr_in *_sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct ip6_rthdr *rth;
#endif
	struct addrinfo hints, *res;
	int error;
	char c;

	/*
	 * Verify the arguments, and make sure we have
	 * at least 7 bytes for the option.
	 */
	if (cpp == NULL || lenp == NULL)
		return -1;
	if (*cpp != NULL) {
		switch (res->ai_family) {
		case AF_INET:
			if (*lenp < 7)
				return -1;
			break;
#ifdef INET6
		case AF_INET6:
			if (*lenp < (int)CMSG_SPACE(sizeof(struct ip6_rthdr) +
				               sizeof(struct in6_addr)))
				return -1;
			break;
#endif
		}
	}
	/*
	 * Decide whether we have a buffer passed to us,
	 * or if we need to use our own static buffer.
	 */
	if (*cpp) {
		lsrp = *cpp;
		ep = lsrp + *lenp;
	} else {
		*cpp = lsrp = (char *)ALIGN(buf);
		ep = lsrp + 1024;
	}

	cp = arg;

#ifdef INET6
	if (ai->ai_family == AF_INET6) {
		if ((rth = inet6_rth_init((void *)*cpp, sizeof(buf),
					  IPV6_RTHDR_TYPE_0, 0)) == NULL)
			return -1;
		if (*cp != '@')
			return -1;
		*protop = IPPROTO_IPV6;
		*optp = IPV6_RTHDR;
	} else
#endif
      {
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
	*protop = IPPROTO_IP;
	*optp = IP_OPTIONS;
      }

	cp++;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = ai->ai_family;
	hints.ai_socktype = SOCK_STREAM;
	for (c = 0;;) {
		if (
#ifdef INET6
		    ai->ai_family != AF_INET6 &&
#endif
		    c == ':')
			cp2 = 0;
		else for (cp2 = cp; (c = *cp2); cp2++) {
			if (c == ',') {
				*cp2++ = '\0';
				if (*cp2 == '@')
					cp2++;
			} else if (c == '@') {
				*cp2++ = '\0';
			} else if (
#ifdef INET6
				   ai->ai_family != AF_INET6 &&
#endif
				   c == ':') {
				*cp2++ = '\0';
			} else
				continue;
			break;
		}
		if (!c)
			cp2 = 0;

		hints.ai_flags = AI_NUMERICHOST;
		error = getaddrinfo(cp, NULL, &hints, &res);
		if (error == EAI_NONAME) {
			hints.ai_flags = 0;
			error = getaddrinfo(cp, NULL, &hints, &res);
		}
		if (error != 0) {
			fprintf(stderr, "%s: %s\n", cp, gai_strerror(error));
			if (error == EAI_SYSTEM)
				fprintf(stderr, "%s: %s\n", cp,
					strerror(errno));
			*cpp = cp;
			return(0);
		}
#ifdef INET6
		if (res->ai_family == AF_INET6) {
			sin6 = (struct sockaddr_in6 *)res->ai_addr;
			if (inet6_rth_add((void *)rth, &sin6->sin6_addr) == -1)
				return(0);
		} else
#endif
	      {
		_sin = (struct sockaddr_in *)res->ai_addr;
		memcpy(lsrp, (char *)&_sin->sin_addr, 4);
		lsrp += 4;
	      }
		if (cp2)
			cp = cp2;
		else
			break;
		/*
		 * Check to make sure there is space for next address
		 */
		if (lsrp + 4 > ep)
			return -1;
		freeaddrinfo(res);
	}
#ifdef INET6
	if (res->ai_family == AF_INET6) {
		rth->ip6r_len = rth->ip6r_segleft * 2;
		*lenp = (rth->ip6r_len + 1) << 3;
	} else
#endif
      {
	if ((*(*cpp+IPOPT_OLEN) = lsrp - *cpp) <= 7) {
		*cpp = 0;
		*lenp = 0;
		return -1;
	}
	*lsrp++ = IPOPT_NOP; /* 32 bit word align it */
	*lenp = lsrp - *cpp;
      }
	freeaddrinfo(res);
	return 1;
}
