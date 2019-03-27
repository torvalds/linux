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
static const char sccsid[] = "@(#)telnetd.c	8.4 (Berkeley) 5/30/95";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "telnetd.h"
#include "pathnames.h"

#include <sys/mman.h>
#include <err.h>
#include <libutil.h>
#include <paths.h>
#include <termcap.h>

#include <arpa/inet.h>

#ifdef	AUTHENTICATION
#include <libtelnet/auth.h>
int	auth_level = 0;
#endif
#ifdef	ENCRYPTION
#include <libtelnet/encrypt.h>
#endif
#include <libtelnet/misc.h>

char	remote_hostname[MAXHOSTNAMELEN];
size_t	utmp_len = sizeof(remote_hostname) - 1;
int	registerd_host_only = 0;


/*
 * I/O data buffers,
 * pointers, and counters.
 */
char	ptyibuf[BUFSIZ], *ptyip = ptyibuf;
char	ptyibuf2[BUFSIZ];

int readstream(int, char *, int);
void doit(struct sockaddr *);
int terminaltypeok(char *);

int	hostinfo = 1;			/* do we print login banner? */

static int debug = 0;
int keepalive = 1;
const char *altlogin;

void doit(struct sockaddr *);
int terminaltypeok(char *);
void startslave(char *, int, char *);
extern void usage(void);
static void _gettermname(void);

/*
 * The string to pass to getopt().  We do it this way so
 * that only the actual options that we support will be
 * passed off to getopt().
 */
char valid_opts[] = {
	'd', ':', 'h', 'k', 'n', 'p', ':', 'S', ':', 'u', ':', 'U',
	'4', '6',
#ifdef	AUTHENTICATION
	'a', ':', 'X', ':',
#endif
#ifdef BFTPDAEMON
	'B',
#endif
#ifdef DIAGNOSTICS
	'D', ':',
#endif
#ifdef	ENCRYPTION
	'e', ':',
#endif
#ifdef	LINEMODE
	'l',
#endif
	'\0'
};

int family = AF_INET;

#ifndef	MAXHOSTNAMELEN
#define	MAXHOSTNAMELEN 256
#endif	/* MAXHOSTNAMELEN */

char *hostname;
char host_name[MAXHOSTNAMELEN];

extern void telnet(int, int, char *);

int level;
char user_name[256];

int
main(int argc, char *argv[])
{
	u_long ultmp;
	struct sockaddr_storage from;
	int on = 1, fromlen;
	int ch;
#if	defined(IPPROTO_IP) && defined(IP_TOS)
	int tos = -1;
#endif
	char *ep;

	pfrontp = pbackp = ptyobuf;
	netip = netibuf;
	nfrontp = nbackp = netobuf;
#ifdef	ENCRYPTION
	nclearto = 0;
#endif	/* ENCRYPTION */

	/*
	 * This initialization causes linemode to default to a configuration
	 * that works on all telnet clients, including the FreeBSD client.
	 * This is not quite the same as the telnet client issuing a "mode
	 * character" command, but has most of the same benefits, and is
	 * preferable since some clients (like usofts) don't have the
	 * mode character command anyway and linemode breaks things.
	 * The most notable symptom of fix is that csh "set filec" operations
	 * like <ESC> (filename completion) and ^D (choices) keys now work
	 * in telnet sessions and can be used more than once on the same line.
	 * CR/LF handling is also corrected in some termio modes.  This 
	 * change resolves problem reports bin/771 and bin/1037.
	 */

	linemode=1;	/*Default to mode that works on bulk of clients*/

	while ((ch = getopt(argc, argv, valid_opts)) != -1) {
		switch(ch) {

#ifdef	AUTHENTICATION
		case 'a':
			/*
			 * Check for required authentication level
			 */
			if (strcmp(optarg, "debug") == 0) {
				extern int auth_debug_mode;
				auth_debug_mode = 1;
			} else if (strcasecmp(optarg, "none") == 0) {
				auth_level = 0;
			} else if (strcasecmp(optarg, "other") == 0) {
				auth_level = AUTH_OTHER;
			} else if (strcasecmp(optarg, "user") == 0) {
				auth_level = AUTH_USER;
			} else if (strcasecmp(optarg, "valid") == 0) {
				auth_level = AUTH_VALID;
			} else if (strcasecmp(optarg, "off") == 0) {
				/*
				 * This hack turns off authentication
				 */
				auth_level = -1;
			} else {
				warnx("unknown authorization level for -a");
			}
			break;
#endif	/* AUTHENTICATION */

#ifdef BFTPDAEMON
		case 'B':
			bftpd++;
			break;
#endif /* BFTPDAEMON */

		case 'd':
			if (strcmp(optarg, "ebug") == 0) {
				debug++;
				break;
			}
			usage();
			/* NOTREACHED */
			break;

#ifdef DIAGNOSTICS
		case 'D':
			/*
			 * Check for desired diagnostics capabilities.
			 */
			if (!strcmp(optarg, "report")) {
				diagnostic |= TD_REPORT|TD_OPTIONS;
			} else if (!strcmp(optarg, "exercise")) {
				diagnostic |= TD_EXERCISE;
			} else if (!strcmp(optarg, "netdata")) {
				diagnostic |= TD_NETDATA;
			} else if (!strcmp(optarg, "ptydata")) {
				diagnostic |= TD_PTYDATA;
			} else if (!strcmp(optarg, "options")) {
				diagnostic |= TD_OPTIONS;
			} else {
				usage();
				/* NOT REACHED */
			}
			break;
#endif /* DIAGNOSTICS */

#ifdef	ENCRYPTION
		case 'e':
			if (strcmp(optarg, "debug") == 0) {
				extern int encrypt_debug_mode;
				encrypt_debug_mode = 1;
				break;
			}
			usage();
			/* NOTREACHED */
			break;
#endif	/* ENCRYPTION */

		case 'h':
			hostinfo = 0;
			break;

#ifdef	LINEMODE
		case 'l':
			alwayslinemode = 1;
			break;
#endif	/* LINEMODE */

		case 'k':
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
			lmodetype = NO_AUTOKLUDGE;
#else
			/* ignore -k option if built without kludge linemode */
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */
			break;

		case 'n':
			keepalive = 0;
			break;

		case 'p':
			altlogin = optarg;
			break;

		case 'S':
#ifdef	HAS_GETTOS
			if ((tos = parsetos(optarg, "tcp")) < 0)
				warnx("%s%s%s",
					"bad TOS argument '", optarg,
					"'; will try to use default TOS");
#else
#define	MAXTOS	255
			ultmp = strtoul(optarg, &ep, 0);
			if (*ep || ep == optarg || ultmp > MAXTOS)
				warnx("%s%s%s",
					"bad TOS argument '", optarg,
					"'; will try to use default TOS");
			else
				tos = ultmp;
#endif
			break;

		case 'u':
			utmp_len = (size_t)atoi(optarg);
			if (utmp_len >= sizeof(remote_hostname))
				utmp_len = sizeof(remote_hostname) - 1;
			break;

		case 'U':
			registerd_host_only = 1;
			break;

#ifdef	AUTHENTICATION
		case 'X':
			/*
			 * Check for invalid authentication types
			 */
			auth_disable_name(optarg);
			break;
#endif	/* AUTHENTICATION */

		case '4':
			family = AF_INET;
			break;

#ifdef INET6
		case '6':
			family = AF_INET6;
			break;
#endif

		default:
			warnx("%c: unknown option", ch);
			/* FALLTHROUGH */
		case '?':
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (debug) {
	    int s, ns, foo, error;
	    const char *service = "telnet";
	    struct addrinfo hints, *res;

	    if (argc > 1) {
		usage();
		/* NOT REACHED */
	    } else if (argc == 1)
		service = *argv;

	    memset(&hints, 0, sizeof(hints));
	    hints.ai_flags = AI_PASSIVE;
	    hints.ai_family = family;
	    hints.ai_socktype = SOCK_STREAM;
	    hints.ai_protocol = 0;
	    error = getaddrinfo(NULL, service, &hints, &res);

	    if (error) {
		errx(1, "tcp/%s: %s\n", service, gai_strerror(error));
		if (error == EAI_SYSTEM)
		    errx(1, "tcp/%s: %s\n", service, strerror(errno));
		usage();
	    }

	    s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	    if (s < 0)
		    err(1, "socket");
	    (void) setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
				(char *)&on, sizeof(on));
	    if (debug > 1)
	        (void) setsockopt(s, SOL_SOCKET, SO_DEBUG,
				(char *)&on, sizeof(on));
	    if (bind(s, res->ai_addr, res->ai_addrlen) < 0)
		err(1, "bind");
	    if (listen(s, 1) < 0)
		err(1, "listen");
	    foo = res->ai_addrlen;
	    ns = accept(s, res->ai_addr, &foo);
	    if (ns < 0)
		err(1, "accept");
	    (void) setsockopt(ns, SOL_SOCKET, SO_DEBUG,
				(char *)&on, sizeof(on));
	    (void) dup2(ns, 0);
	    (void) close(ns);
	    (void) close(s);
#ifdef convex
	} else if (argc == 1) {
		; /* VOID*/		/* Just ignore the host/port name */
#endif
	} else if (argc > 0) {
		usage();
		/* NOT REACHED */
	}

	openlog("telnetd", LOG_PID | LOG_ODELAY, LOG_DAEMON);
	fromlen = sizeof (from);
	if (getpeername(0, (struct sockaddr *)&from, &fromlen) < 0) {
		warn("getpeername");
		_exit(1);
	}
	if (keepalive &&
	    setsockopt(0, SOL_SOCKET, SO_KEEPALIVE,
			(char *)&on, sizeof (on)) < 0) {
		syslog(LOG_WARNING, "setsockopt (SO_KEEPALIVE): %m");
	}

#if	defined(IPPROTO_IP) && defined(IP_TOS)
	if (from.ss_family == AF_INET) {
# if	defined(HAS_GETTOS)
		struct tosent *tp;
		if (tos < 0 && (tp = gettosbyname("telnet", "tcp")))
			tos = tp->t_tos;
# endif
		if (tos < 0)
			tos = 020;	/* Low Delay bit */
		if (tos
		   && (setsockopt(0, IPPROTO_IP, IP_TOS,
				  (char *)&tos, sizeof(tos)) < 0)
		   && (errno != ENOPROTOOPT) )
			syslog(LOG_WARNING, "setsockopt (IP_TOS): %m");
	}
#endif	/* defined(IPPROTO_IP) && defined(IP_TOS) */
	net = 0;
	doit((struct sockaddr *)&from);
	/* NOTREACHED */
	return(0);
}  /* end of main */

	void
usage()
{
	fprintf(stderr, "usage: telnetd");
#ifdef	AUTHENTICATION
	fprintf(stderr,
	    " [-4] [-6] [-a (debug|other|user|valid|off|none)]\n\t");
#endif
#ifdef BFTPDAEMON
	fprintf(stderr, " [-B]");
#endif
	fprintf(stderr, " [-debug]");
#ifdef DIAGNOSTICS
	fprintf(stderr, " [-D (options|report|exercise|netdata|ptydata)]\n\t");
#endif
#ifdef	AUTHENTICATION
	fprintf(stderr, " [-edebug]");
#endif
	fprintf(stderr, " [-h]");
#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
	fprintf(stderr, " [-k]");
#endif
#ifdef LINEMODE
	fprintf(stderr, " [-l]");
#endif
	fprintf(stderr, " [-n]");
	fprintf(stderr, "\n\t");
#ifdef	HAS_GETTOS
	fprintf(stderr, " [-S tos]");
#endif
#ifdef	AUTHENTICATION
	fprintf(stderr, " [-X auth-type]");
#endif
	fprintf(stderr, " [-u utmp_hostname_length] [-U]");
	fprintf(stderr, " [port]\n");
	exit(1);
}

/*
 * getterminaltype
 *
 *	Ask the other end to send along its terminal type and speed.
 * Output is the variable terminaltype filled in.
 */
static unsigned char ttytype_sbbuf[] = {
	IAC, SB, TELOPT_TTYPE, TELQUAL_SEND, IAC, SE
};


#ifndef	AUTHENTICATION
#define undef2 __unused
#else
#define undef2
#endif

static int
getterminaltype(char *name undef2)
{
    int retval = -1;

    settimer(baseline);
#ifdef	AUTHENTICATION
    /*
     * Handle the Authentication option before we do anything else.
     */
    if (auth_level >= 0) {
	send_do(TELOPT_AUTHENTICATION, 1);
	while (his_will_wont_is_changing(TELOPT_AUTHENTICATION))
	    ttloop();
	if (his_state_is_will(TELOPT_AUTHENTICATION)) {
	    retval = auth_wait(name);
	}
    }
#endif

#ifdef	ENCRYPTION
    send_will(TELOPT_ENCRYPT, 1);
#endif	/* ENCRYPTION */
    send_do(TELOPT_TTYPE, 1);
    send_do(TELOPT_TSPEED, 1);
    send_do(TELOPT_XDISPLOC, 1);
    send_do(TELOPT_NEW_ENVIRON, 1);
    send_do(TELOPT_OLD_ENVIRON, 1);
    while (
#ifdef	ENCRYPTION
	   his_do_dont_is_changing(TELOPT_ENCRYPT) ||
#endif	/* ENCRYPTION */
	   his_will_wont_is_changing(TELOPT_TTYPE) ||
	   his_will_wont_is_changing(TELOPT_TSPEED) ||
	   his_will_wont_is_changing(TELOPT_XDISPLOC) ||
	   his_will_wont_is_changing(TELOPT_NEW_ENVIRON) ||
	   his_will_wont_is_changing(TELOPT_OLD_ENVIRON)) {
	ttloop();
    }
#ifdef	ENCRYPTION
    /*
     * Wait for the negotiation of what type of encryption we can
     * send with.  If autoencrypt is not set, this will just return.
     */
    if (his_state_is_will(TELOPT_ENCRYPT)) {
	encrypt_wait();
    }
#endif	/* ENCRYPTION */
    if (his_state_is_will(TELOPT_TSPEED)) {
	static unsigned char sb[] =
			{ IAC, SB, TELOPT_TSPEED, TELQUAL_SEND, IAC, SE };

	output_datalen(sb, sizeof sb);
	DIAG(TD_OPTIONS, printsub('>', sb + 2, sizeof sb - 2););
    }
    if (his_state_is_will(TELOPT_XDISPLOC)) {
	static unsigned char sb[] =
			{ IAC, SB, TELOPT_XDISPLOC, TELQUAL_SEND, IAC, SE };

	output_datalen(sb, sizeof sb);
	DIAG(TD_OPTIONS, printsub('>', sb + 2, sizeof sb - 2););
    }
    if (his_state_is_will(TELOPT_NEW_ENVIRON)) {
	static unsigned char sb[] =
			{ IAC, SB, TELOPT_NEW_ENVIRON, TELQUAL_SEND, IAC, SE };

	output_datalen(sb, sizeof sb);
	DIAG(TD_OPTIONS, printsub('>', sb + 2, sizeof sb - 2););
    }
    else if (his_state_is_will(TELOPT_OLD_ENVIRON)) {
	static unsigned char sb[] =
			{ IAC, SB, TELOPT_OLD_ENVIRON, TELQUAL_SEND, IAC, SE };

	output_datalen(sb, sizeof sb);
	DIAG(TD_OPTIONS, printsub('>', sb + 2, sizeof sb - 2););
    }
    if (his_state_is_will(TELOPT_TTYPE)) {

	output_datalen(ttytype_sbbuf, sizeof ttytype_sbbuf);
	DIAG(TD_OPTIONS, printsub('>', ttytype_sbbuf + 2,
					sizeof ttytype_sbbuf - 2););
    }
    if (his_state_is_will(TELOPT_TSPEED)) {
	while (sequenceIs(tspeedsubopt, baseline))
	    ttloop();
    }
    if (his_state_is_will(TELOPT_XDISPLOC)) {
	while (sequenceIs(xdisplocsubopt, baseline))
	    ttloop();
    }
    if (his_state_is_will(TELOPT_NEW_ENVIRON)) {
	while (sequenceIs(environsubopt, baseline))
	    ttloop();
    }
    if (his_state_is_will(TELOPT_OLD_ENVIRON)) {
	while (sequenceIs(oenvironsubopt, baseline))
	    ttloop();
    }
    if (his_state_is_will(TELOPT_TTYPE)) {
	char first[256], last[256];

	while (sequenceIs(ttypesubopt, baseline))
	    ttloop();

	/*
	 * If the other side has already disabled the option, then
	 * we have to just go with what we (might) have already gotten.
	 */
	if (his_state_is_will(TELOPT_TTYPE) && !terminaltypeok(terminaltype)) {
	    (void) strncpy(first, terminaltype, sizeof(first)-1);
	    first[sizeof(first)-1] = '\0';
	    for(;;) {
		/*
		 * Save the unknown name, and request the next name.
		 */
		(void) strncpy(last, terminaltype, sizeof(last)-1);
		last[sizeof(last)-1] = '\0';
		_gettermname();
		if (terminaltypeok(terminaltype))
		    break;
		if ((strncmp(last, terminaltype, sizeof(last)) == 0) ||
		    his_state_is_wont(TELOPT_TTYPE)) {
		    /*
		     * We've hit the end.  If this is the same as
		     * the first name, just go with it.
		     */
		    if (strncmp(first, terminaltype, sizeof(first)) == 0)
			break;
		    /*
		     * Get the terminal name one more time, so that
		     * RFC1091 compliant telnets will cycle back to
		     * the start of the list.
		     */
		     _gettermname();
		    if (strncmp(first, terminaltype, sizeof(first)) != 0) {
			(void) strncpy(terminaltype, first, sizeof(terminaltype)-1);
			terminaltype[sizeof(terminaltype)-1] = '\0';
		    }
		    break;
		}
	    }
	}
    }
    return(retval);
}  /* end of getterminaltype */

static void
_gettermname(void)
{
    /*
     * If the client turned off the option,
     * we can't send another request, so we
     * just return.
     */
    if (his_state_is_wont(TELOPT_TTYPE))
	return;
    settimer(baseline);
    output_datalen(ttytype_sbbuf, sizeof ttytype_sbbuf);
    DIAG(TD_OPTIONS, printsub('>', ttytype_sbbuf + 2,
					sizeof ttytype_sbbuf - 2););
    while (sequenceIs(ttypesubopt, baseline))
	ttloop();
}

int
terminaltypeok(char *s)
{
    char buf[1024];

    if (terminaltype == NULL)
	return(1);

    /*
     * tgetent() will return 1 if the type is known, and
     * 0 if it is not known.  If it returns -1, it couldn't
     * open the database.  But if we can't open the database,
     * it won't help to say we failed, because we won't be
     * able to verify anything else.  So, we treat -1 like 1.
     */
    if (tgetent(buf, s) == 0)
	return(0);
    return(1);
}

/*
 * Get a pty, scan input lines.
 */
void
doit(struct sockaddr *who)
{
	int err_; /* XXX */
	int ptynum;

	/*
	 * Find an available pty to use.
	 */
#ifndef	convex
	pty = getpty(&ptynum);
	if (pty < 0)
		fatal(net, "All network ports in use");
#else
	for (;;) {
		char *lp;

		if ((lp = getpty()) == NULL)
			fatal(net, "Out of ptys");

		if ((pty = open(lp, 2)) >= 0) {
			strlcpy(line,lp,sizeof(line));
			line[5] = 't';
			break;
		}
	}
#endif

	/* get name of connected client */
	if (realhostname_sa(remote_hostname, sizeof(remote_hostname) - 1,
	    who, who->sa_len) == HOSTNAME_INVALIDADDR && registerd_host_only)
		fatal(net, "Couldn't resolve your address into a host name.\r\n\
	Please contact your net administrator");
	remote_hostname[sizeof(remote_hostname) - 1] = '\0';

	if (!isdigit(remote_hostname[0]) && strlen(remote_hostname) > utmp_len)
		err_ = getnameinfo(who, who->sa_len, remote_hostname,
				  sizeof(remote_hostname), NULL, 0,
				  NI_NUMERICHOST);
		/* XXX: do 'err_' check */

	(void) gethostname(host_name, sizeof(host_name) - 1);
	host_name[sizeof(host_name) - 1] = '\0';
	hostname = host_name;

#ifdef	AUTHENTICATION
#ifdef	ENCRYPTION
/* The above #ifdefs should actually be "or"'ed, not "and"'ed.
 * This is a byproduct of needing "#ifdef" and not "#if defined()"
 * for unifdef. XXX MarkM
 */
	auth_encrypt_init(hostname, remote_hostname, "TELNETD", 1);
#endif
#endif

	init_env();
	/*
	 * get terminal type.
	 */
	*user_name = 0;
	level = getterminaltype(user_name);
	setenv("TERM", terminaltype ? terminaltype : "network", 1);

	telnet(net, pty, remote_hostname);	/* begin server process */

	/*NOTREACHED*/
}  /* end of doit */

/*
 * Main loop.  Select from pty and network, and
 * hand data to telnet receiver finite state machine.
 */
void
telnet(int f, int p, char *host)
{
	int on = 1;
#define	TABBUFSIZ	512
	char	defent[TABBUFSIZ];
	char	defstrs[TABBUFSIZ];
#undef	TABBUFSIZ
	char *HE;
	char *HN;
	char *IM;
	char *IF;
	char *if_buf;
	int if_fd = -1;
	struct stat statbuf;
	int nfd;

	/*
	 * Initialize the slc mapping table.
	 */
	get_slc_defaults();

	/*
	 * Do some tests where it is desireable to wait for a response.
	 * Rather than doing them slowly, one at a time, do them all
	 * at once.
	 */
	if (my_state_is_wont(TELOPT_SGA))
		send_will(TELOPT_SGA, 1);
	/*
	 * Is the client side a 4.2 (NOT 4.3) system?  We need to know this
	 * because 4.2 clients are unable to deal with TCP urgent data.
	 *
	 * To find out, we send out a "DO ECHO".  If the remote system
	 * answers "WILL ECHO" it is probably a 4.2 client, and we note
	 * that fact ("WILL ECHO" ==> that the client will echo what
	 * WE, the server, sends it; it does NOT mean that the client will
	 * echo the terminal input).
	 */
	send_do(TELOPT_ECHO, 1);

#ifdef	LINEMODE
	if (his_state_is_wont(TELOPT_LINEMODE)) {
		/* Query the peer for linemode support by trying to negotiate
		 * the linemode option.
		 */
		linemode = 0;
		editmode = 0;
		send_do(TELOPT_LINEMODE, 1);  /* send do linemode */
	}
#endif	/* LINEMODE */

	/*
	 * Send along a couple of other options that we wish to negotiate.
	 */
	send_do(TELOPT_NAWS, 1);
	send_will(TELOPT_STATUS, 1);
	flowmode = 1;		/* default flow control state */
	restartany = -1;	/* uninitialized... */
	send_do(TELOPT_LFLOW, 1);

	/*
	 * Spin, waiting for a response from the DO ECHO.  However,
	 * some REALLY DUMB telnets out there might not respond
	 * to the DO ECHO.  So, we spin looking for NAWS, (most dumb
	 * telnets so far seem to respond with WONT for a DO that
	 * they don't understand...) because by the time we get the
	 * response, it will already have processed the DO ECHO.
	 * Kludge upon kludge.
	 */
	while (his_will_wont_is_changing(TELOPT_NAWS))
		ttloop();

	/*
	 * But...
	 * The client might have sent a WILL NAWS as part of its
	 * startup code; if so, we'll be here before we get the
	 * response to the DO ECHO.  We'll make the assumption
	 * that any implementation that understands about NAWS
	 * is a modern enough implementation that it will respond
	 * to our DO ECHO request; hence we'll do another spin
	 * waiting for the ECHO option to settle down, which is
	 * what we wanted to do in the first place...
	 */
	if (his_want_state_is_will(TELOPT_ECHO) &&
	    his_state_is_will(TELOPT_NAWS)) {
		while (his_will_wont_is_changing(TELOPT_ECHO))
			ttloop();
	}
	/*
	 * On the off chance that the telnet client is broken and does not
	 * respond to the DO ECHO we sent, (after all, we did send the
	 * DO NAWS negotiation after the DO ECHO, and we won't get here
	 * until a response to the DO NAWS comes back) simulate the
	 * receipt of a will echo.  This will also send a WONT ECHO
	 * to the client, since we assume that the client failed to
	 * respond because it believes that it is already in DO ECHO
	 * mode, which we do not want.
	 */
	if (his_want_state_is_will(TELOPT_ECHO)) {
		DIAG(TD_OPTIONS, output_data("td: simulating recv\r\n"));
		willoption(TELOPT_ECHO);
	}

	/*
	 * Finally, to clean things up, we turn on our echo.  This
	 * will break stupid 4.2 telnets out of local terminal echo.
	 */

	if (my_state_is_wont(TELOPT_ECHO))
		send_will(TELOPT_ECHO, 1);

	/*
	 * Turn on packet mode
	 */
	(void) ioctl(p, TIOCPKT, (char *)&on);

#if	defined(LINEMODE) && defined(KLUDGELINEMODE)
	/*
	 * Continuing line mode support.  If client does not support
	 * real linemode, attempt to negotiate kludge linemode by sending
	 * the do timing mark sequence.
	 */
	if (lmodetype < REAL_LINEMODE)
		send_do(TELOPT_TM, 1);
#endif	/* defined(LINEMODE) && defined(KLUDGELINEMODE) */

	/*
	 * Call telrcv() once to pick up anything received during
	 * terminal type negotiation, 4.2/4.3 determination, and
	 * linemode negotiation.
	 */
	telrcv();

	(void) ioctl(f, FIONBIO, (char *)&on);
	(void) ioctl(p, FIONBIO, (char *)&on);

#if	defined(SO_OOBINLINE)
	(void) setsockopt(net, SOL_SOCKET, SO_OOBINLINE,
				(char *)&on, sizeof on);
#endif	/* defined(SO_OOBINLINE) */

#ifdef	SIGTSTP
	(void) signal(SIGTSTP, SIG_IGN);
#endif
#ifdef	SIGTTOU
	/*
	 * Ignoring SIGTTOU keeps the kernel from blocking us
	 * in ttioct() in /sys/tty.c.
	 */
	(void) signal(SIGTTOU, SIG_IGN);
#endif

	(void) signal(SIGCHLD, cleanup);

#ifdef  TIOCNOTTY
	{
		int t;
		t = open(_PATH_TTY, O_RDWR);
		if (t >= 0) {
			(void) ioctl(t, TIOCNOTTY, (char *)0);
			(void) close(t);
		}
	}
#endif

	/*
	 * Show banner that getty never gave.
	 *
	 * We put the banner in the pty input buffer.  This way, it
	 * gets carriage return null processing, etc., just like all
	 * other pty --> client data.
	 */

	if (getent(defent, "default") == 1) {
		char *cp=defstrs;

		HE = Getstr("he", &cp);
		HN = Getstr("hn", &cp);
		IM = Getstr("im", &cp);
		IF = Getstr("if", &cp);
		if (HN && *HN)
			(void) strlcpy(host_name, HN, sizeof(host_name));
		if (IF) {
		    if_fd = open(IF, O_RDONLY, 000);
		    IM = 0;
		}
		if (IM == 0)
			IM = strdup("");
	} else {
		IM = strdup(DEFAULT_IM);
		HE = 0;
	}
	edithost(HE, host_name);
	if (hostinfo && *IM)
		putf(IM, ptyibuf2);
	if (if_fd != -1) {
		if (fstat(if_fd, &statbuf) != -1 && statbuf.st_size > 0) {
			if_buf = (char *) mmap (0, statbuf.st_size,
			    PROT_READ, 0, if_fd, 0);
			if (if_buf != MAP_FAILED) {
				putf(if_buf, ptyibuf2);
				munmap(if_buf, statbuf.st_size);
			}
		}
		close (if_fd);
	}

	if (pcc)
		(void) strncat(ptyibuf2, ptyip, pcc+1);
	ptyip = ptyibuf2;
	pcc = strlen(ptyip);
#ifdef	LINEMODE
	/*
	 * Last check to make sure all our states are correct.
	 */
	init_termbuf();
	localstat();
#endif	/* LINEMODE */

	DIAG(TD_REPORT, output_data("td: Entering processing loop\r\n"));

	/*
	 * Startup the login process on the slave side of the terminal
	 * now.  We delay this until here to insure option negotiation
	 * is complete.
	 */
	startslave(host, level, user_name);

	nfd = ((f > p) ? f : p) + 1;
	for (;;) {
		fd_set ibits, obits, xbits;
		int c;

		if (ncc < 0 && pcc < 0)
			break;

		FD_ZERO(&ibits);
		FD_ZERO(&obits);
		FD_ZERO(&xbits);
		/*
		 * Never look for input if there's still
		 * stuff in the corresponding output buffer
		 */
		if (nfrontp - nbackp || pcc > 0) {
			FD_SET(f, &obits);
		} else {
			FD_SET(p, &ibits);
		}
		if (pfrontp - pbackp || ncc > 0) {
			FD_SET(p, &obits);
		} else {
			FD_SET(f, &ibits);
		}
		if (!SYNCHing) {
			FD_SET(f, &xbits);
		}
		if ((c = select(nfd, &ibits, &obits, &xbits,
						(struct timeval *)0)) < 1) {
			if (c == -1) {
				if (errno == EINTR) {
					continue;
				}
			}
			sleep(5);
			continue;
		}

		/*
		 * Any urgent data?
		 */
		if (FD_ISSET(net, &xbits)) {
		    SYNCHing = 1;
		}

		/*
		 * Something to read from the network...
		 */
		if (FD_ISSET(net, &ibits)) {
#if	!defined(SO_OOBINLINE)
			/*
			 * In 4.2 (and 4.3 beta) systems, the
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

			(void) ioctl(net, SIOCATMARK, (char *)&atmark);
			if (atmark) {
			    ncc = recv(net, netibuf, sizeof (netibuf), MSG_OOB);
			    if ((ncc == -1) && (errno == EINVAL)) {
				ncc = read(net, netibuf, sizeof (netibuf));
				if (sequenceIs(didnetreceive, gotDM)) {
				    SYNCHing = stilloob(net);
				}
			    }
			} else {
			    ncc = read(net, netibuf, sizeof (netibuf));
			}
		    } else {
			ncc = read(net, netibuf, sizeof (netibuf));
		    }
		    settimer(didnetreceive);
#else	/* !defined(SO_OOBINLINE)) */
		    ncc = read(net, netibuf, sizeof (netibuf));
#endif	/* !defined(SO_OOBINLINE)) */
		    if (ncc < 0 && errno == EWOULDBLOCK)
			ncc = 0;
		    else {
			if (ncc <= 0) {
			    break;
			}
			netip = netibuf;
		    }
		    DIAG((TD_REPORT | TD_NETDATA),
			output_data("td: netread %d chars\r\n", ncc));
		    DIAG(TD_NETDATA, printdata("nd", netip, ncc));
		}

		/*
		 * Something to read from the pty...
		 */
		if (FD_ISSET(p, &ibits)) {
			pcc = read(p, ptyibuf, BUFSIZ);
			/*
			 * On some systems, if we try to read something
			 * off the master side before the slave side is
			 * opened, we get EIO.
			 */
			if (pcc < 0 && (errno == EWOULDBLOCK ||
#ifdef	EAGAIN
					errno == EAGAIN ||
#endif
					errno == EIO)) {
				pcc = 0;
			} else {
				if (pcc <= 0)
					break;
#ifdef	LINEMODE
				/*
				 * If ioctl from pty, pass it through net
				 */
				if (ptyibuf[0] & TIOCPKT_IOCTL) {
					copy_termbuf(ptyibuf+1, pcc-1);
					localstat();
					pcc = 1;
				}
#endif	/* LINEMODE */
				if (ptyibuf[0] & TIOCPKT_FLUSHWRITE) {
					netclear();	/* clear buffer back */
#ifndef	NO_URGENT
					/*
					 * There are client telnets on some
					 * operating systems get screwed up
					 * royally if we send them urgent
					 * mode data.
					 */
					output_data("%c%c", IAC, DM);
					neturg = nfrontp-1; /* off by one XXX */
					DIAG(TD_OPTIONS,
					    printoption("td: send IAC", DM));

#endif
				}
				if (his_state_is_will(TELOPT_LFLOW) &&
				    (ptyibuf[0] &
				     (TIOCPKT_NOSTOP|TIOCPKT_DOSTOP))) {
					int newflow =
					    ptyibuf[0] & TIOCPKT_DOSTOP ? 1 : 0;
					if (newflow != flowmode) {
						flowmode = newflow;
						output_data("%c%c%c%c%c%c",
							IAC, SB, TELOPT_LFLOW,
							flowmode ? LFLOW_ON
								 : LFLOW_OFF,
							IAC, SE);
						DIAG(TD_OPTIONS, printsub('>',
						    (unsigned char *)nfrontp-4,
						    4););
					}
				}
				pcc--;
				ptyip = ptyibuf+1;
			}
		}

		while (pcc > 0) {
			if ((&netobuf[BUFSIZ] - nfrontp) < 2)
				break;
			c = *ptyip++ & 0377, pcc--;
			if (c == IAC)
				output_data("%c", c);
			output_data("%c", c);
			if ((c == '\r') && (my_state_is_wont(TELOPT_BINARY))) {
				if (pcc > 0 && ((*ptyip & 0377) == '\n')) {
					output_data("%c", *ptyip++ & 0377);
					pcc--;
				} else
					output_data("%c", '\0');
			}
		}

		if (FD_ISSET(f, &obits) && (nfrontp - nbackp) > 0)
			netflush();
		if (ncc > 0)
			telrcv();
		if (FD_ISSET(p, &obits) && (pfrontp - pbackp) > 0)
			ptyflush();
	}
	cleanup(0);
}  /* end of telnet */

#ifndef	TCSIG
# ifdef	TIOCSIG
#  define TCSIG TIOCSIG
# endif
#endif

/*
 * Send interrupt to process on other side of pty.
 * If it is in raw mode, just write NULL;
 * otherwise, write intr char.
 */
void
interrupt(void)
{
	ptyflush();	/* half-hearted */

#ifdef	TCSIG
	(void) ioctl(pty, TCSIG, SIGINT);
#else	/* TCSIG */
	init_termbuf();
	*pfrontp++ = slctab[SLC_IP].sptr ?
			(unsigned char)*slctab[SLC_IP].sptr : '\177';
#endif	/* TCSIG */
}

/*
 * Send quit to process on other side of pty.
 * If it is in raw mode, just write NULL;
 * otherwise, write quit char.
 */
void
sendbrk(void)
{
	ptyflush();	/* half-hearted */
#ifdef	TCSIG
	(void) ioctl(pty, TCSIG, SIGQUIT);
#else	/* TCSIG */
	init_termbuf();
	*pfrontp++ = slctab[SLC_ABORT].sptr ?
			(unsigned char)*slctab[SLC_ABORT].sptr : '\034';
#endif	/* TCSIG */
}

void
sendsusp(void)
{
#ifdef	SIGTSTP
	ptyflush();	/* half-hearted */
# ifdef	TCSIG
	(void) ioctl(pty, TCSIG, SIGTSTP);
# else	/* TCSIG */
	*pfrontp++ = slctab[SLC_SUSP].sptr ?
			(unsigned char)*slctab[SLC_SUSP].sptr : '\032';
# endif	/* TCSIG */
#endif	/* SIGTSTP */
}

/*
 * When we get an AYT, if ^T is enabled, use that.  Otherwise,
 * just send back "[Yes]".
 */
void
recv_ayt(void)
{
#if	defined(SIGINFO) && defined(TCSIG)
	if (slctab[SLC_AYT].sptr && *slctab[SLC_AYT].sptr != _POSIX_VDISABLE) {
		(void) ioctl(pty, TCSIG, SIGINFO);
		return;
	}
#endif
	output_data("\r\n[Yes]\r\n");
}

void
doeof(void)
{
	init_termbuf();

#if	defined(LINEMODE) && defined(USE_TERMIO) && (VEOF == VMIN)
	if (!tty_isediting()) {
		extern char oldeofc;
		*pfrontp++ = oldeofc;
		return;
	}
#endif
	*pfrontp++ = slctab[SLC_EOF].sptr ?
			(unsigned char)*slctab[SLC_EOF].sptr : '\004';
}
