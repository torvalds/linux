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

#ifdef _SC_CRAY_SECURE_SYS
#include <sys/sysv.h>
#include <sys/secdev.h>
#include <sys/secparm.h>
#include <sys/usrv.h>
int	secflag;
char	tty_dev[16];
struct	secdev dv;
struct	sysv sysv;
struct	socksec ss;
#endif	/* _SC_CRAY_SECURE_SYS */

#ifdef AUTHENTICATION
int	auth_level = 0;
#endif

#ifdef KRB5
#define Authenticator k5_Authenticator
#include <krb5.h>
#undef Authenticator
#endif

extern	int utmp_len;
int	registerd_host_only = 0;
#ifdef ENCRYPTION
int	require_encryption = 0;
#endif

#ifdef STREAMSPTY

#ifdef _AIX
#include <sys/termio.h>
#endif
# ifdef HAVE_SYS_STRTTY_H
# include <sys/strtty.h>
# endif
# ifdef HAVE_SYS_STR_TTY_H
# include <sys/str_tty.h>
# endif
/* make sure we don't get the bsd version */
/* what is this here for? solaris? /joda */
# ifdef HAVE_SYS_TTY_H
# include "/usr/include/sys/tty.h"
# endif
# ifdef HAVE_SYS_PTYVAR_H
# include <sys/ptyvar.h>
# endif

/*
 * Because of the way ptyibuf is used with streams messages, we need
 * ptyibuf+1 to be on a full-word boundary.  The following wierdness
 * is simply to make that happen.
 */
long	ptyibufbuf[BUFSIZ/sizeof(long)+1];
char	*ptyibuf = ((char *)&ptyibufbuf[1])-1;
char	*ptyip = ((char *)&ptyibufbuf[1])-1;
char	ptyibuf2[BUFSIZ];
unsigned char ctlbuf[BUFSIZ];
struct	strbuf strbufc, strbufd;

int readstream(int, char*, int);

#else	/* ! STREAMPTY */

/*
 * I/O data buffers,
 * pointers, and counters.
 */
char	ptyibuf[BUFSIZ], *ptyip = ptyibuf;
char	ptyibuf2[BUFSIZ];

#endif /* ! STREAMPTY */

int	hostinfo = 1;			/* do we print login banner? */

#ifdef	_CRAY
extern int      newmap; /* nonzero if \n maps to ^M^J */
int	lowpty = 0, highpty;	/* low, high pty numbers */
#endif /* CRAY */

int debug = 0;
int keepalive = 1;
char *progname;

static void usage (int error_code);

/*
 * The string to pass to getopt().  We do it this way so
 * that only the actual options that we support will be
 * passed off to getopt().
 */
char valid_opts[] = "Bd:hklnS:u:UL:y"
#ifdef AUTHENTICATION
		    "a:X:z"
#endif
#ifdef ENCRYPTION
                     "e"
#endif
#ifdef DIAGNOSTICS
		    "D:"
#endif
#ifdef _CRAY
		    "r:"
#endif
		    ;

static void doit(struct sockaddr*, int);

int
main(int argc, char **argv)
{
    struct sockaddr_storage __ss;
    struct sockaddr *sa = (struct sockaddr *)&__ss;
    int on = 1;
    socklen_t sa_size;
    int ch;
#if	defined(IPPROTO_IP) && defined(IP_TOS)
    int tos = -1;
#endif
    pfrontp = pbackp = ptyobuf;
    netip = netibuf;
    nfrontp = nbackp = netobuf;

    setprogname(argv[0]);

    progname = *argv;
#ifdef ENCRYPTION
    nclearto = 0;
#endif

#ifdef _CRAY
    /*
     * Get number of pty's before trying to process options,
     * which may include changing pty range.
     */
    highpty = getnpty();
#endif /* CRAY */

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
	print_version(NULL);
	exit(0);
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0)
	usage(0);

    while ((ch = getopt(argc, argv, valid_opts)) != -1) {
	switch(ch) {

#ifdef	AUTHENTICATION
	case 'a':
	    /*
	     * Check for required authentication level
	     */
	    if (strcmp(optarg, "debug") == 0) {
		auth_debug_mode = 1;
	    } else if (strcasecmp(optarg, "none") == 0) {
		auth_level = 0;
	    } else if (strcasecmp(optarg, "otp") == 0) {
		auth_level = 0;
		require_otp = 1;
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
		fprintf(stderr,
			"telnetd: unknown authorization level for -a\n");
	    }
	    break;
#endif	/* AUTHENTICATION */

	case 'B': /* BFTP mode is not supported any more */
	    break;
	case 'd':
	    if (strcmp(optarg, "ebug") == 0) {
		debug++;
		break;
	    }
	    usage(1);
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
		usage(1);
		/* NOT REACHED */
	    }
	    break;
#endif /* DIAGNOSTICS */

#ifdef ENCRYPTION
	case 'e':
	    require_encryption = 1;
	    break;
#endif

	case 'h':
	    hostinfo = 0;
	    break;

	case 'k': /* Linemode is not supported any more */
	case 'l':
	    break;

	case 'n':
	    keepalive = 0;
	    break;

#ifdef _CRAY
	case 'r':
	    {
		char *strchr();
		char *c;

		/*
		 * Allow the specification of alterations
		 * to the pty search range.  It is legal to
		 * specify only one, and not change the
		 * other from its default.
		 */
		c = strchr(optarg, '-');
		if (c) {
		    *c++ = '\0';
		    highpty = atoi(c);
		}
		if (*optarg != '\0')
		    lowpty = atoi(optarg);
		if ((lowpty > highpty) || (lowpty < 0) ||
		    (highpty > 32767)) {
		    usage(1);
		    /* NOT REACHED */
		}
		break;
	    }
#endif	/* CRAY */

	case 'S':
#ifdef	HAVE_PARSETOS
	    if ((tos = parsetos(optarg, "tcp")) < 0)
		fprintf(stderr, "%s%s%s\n",
			"telnetd: Bad TOS argument '", optarg,
			"'; will try to use default TOS");
#else
	    fprintf(stderr, "%s%s\n", "TOS option unavailable; ",
		    "-S flag not supported\n");
#endif
	    break;

	case 'u': {
	    char *eptr;

	    utmp_len = strtol(optarg, &eptr, 0);
	    if (optarg == eptr)
		fprintf(stderr, "telnetd: unknown utmp len (%s)\n", optarg);
	    break;
	}

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
#endif
	case 'y':
	    no_warn = 1;
	    break;
#ifdef AUTHENTICATION
	case 'z':
	    log_unauth = 1;
	    break;

#endif	/* AUTHENTICATION */

	case 'L':
	    new_login = optarg;
	    break;

	default:
	    fprintf(stderr, "telnetd: %c: unknown option\n", ch);
	    /* FALLTHROUGH */
	case '?':
	    usage(0);
	    /* NOTREACHED */
	}
    }

    argc -= optind;
    argv += optind;

    if (debug) {
	int port = 0;
	struct servent *sp;

	if (argc > 1) {
	    usage (1);
	} else if (argc == 1) {
	    sp = roken_getservbyname (*argv, "tcp");
	    if (sp)
		port = sp->s_port;
	    else
		port = htons(atoi(*argv));
	} else {
#ifdef KRB5
	    port = krb5_getportbyname (NULL, "telnet", "tcp", 23);
#else
	    port = k_getportbyname("telnet", "tcp", htons(23));
#endif
	}
	mini_inetd (port, NULL);
    } else if (argc > 0) {
	usage(1);
	/* NOT REACHED */
    }

#ifdef _SC_CRAY_SECURE_SYS
    secflag = sysconf(_SC_CRAY_SECURE_SYS);

    /*
     *	Get socket's security label
     */
    if (secflag)  {
	socklen_t szss = sizeof(ss);
	int sock_multi;
	socklen_t szi = sizeof(int);

	memset(&dv, 0, sizeof(dv));

	if (getsysv(&sysv, sizeof(struct sysv)) != 0)
	    fatalperror(net, "getsysv");

	/*
	 *	Get socket security label and set device values
	 *	   {security label to be set on ttyp device}
	 */
#ifdef SO_SEC_MULTI			/* 8.0 code */
	if ((getsockopt(0, SOL_SOCKET, SO_SECURITY,
			(void *)&ss, &szss) < 0) ||
	    (getsockopt(0, SOL_SOCKET, SO_SEC_MULTI,
			(void *)&sock_multi, &szi) < 0))
	    fatalperror(net, "getsockopt");
	else {
	    dv.dv_actlvl = ss.ss_actlabel.lt_level;
	    dv.dv_actcmp = ss.ss_actlabel.lt_compart;
	    if (!sock_multi) {
		dv.dv_minlvl = dv.dv_maxlvl = dv.dv_actlvl;
		dv.dv_valcmp = dv.dv_actcmp;
	    } else {
		dv.dv_minlvl = ss.ss_minlabel.lt_level;
		dv.dv_maxlvl = ss.ss_maxlabel.lt_level;
		dv.dv_valcmp = ss.ss_maxlabel.lt_compart;
	    }
	    dv.dv_devflg = 0;
	}
#else /* SO_SEC_MULTI */		/* 7.0 code */
	if (getsockopt(0, SOL_SOCKET, SO_SECURITY,
		       (void *)&ss, &szss) >= 0) {
	    dv.dv_actlvl = ss.ss_slevel;
	    dv.dv_actcmp = ss.ss_compart;
	    dv.dv_minlvl = ss.ss_minlvl;
	    dv.dv_maxlvl = ss.ss_maxlvl;
	    dv.dv_valcmp = ss.ss_maxcmp;
	}
#endif /* SO_SEC_MULTI */
    }
#endif	/* _SC_CRAY_SECURE_SYS */

    roken_openlog("telnetd", LOG_PID | LOG_ODELAY, LOG_DAEMON);
    sa_size = sizeof (__ss);
    if (getpeername(STDIN_FILENO, sa, &sa_size) < 0) {
	fprintf(stderr, "%s: ", progname);
	perror("getpeername");
	_exit(1);
    }
    if (keepalive &&
	setsockopt(STDIN_FILENO, SOL_SOCKET, SO_KEEPALIVE,
		   (void *)&on, sizeof (on)) < 0) {
	syslog(LOG_WARNING, "setsockopt (SO_KEEPALIVE): %m");
    }

#if	defined(IPPROTO_IP) && defined(IP_TOS) && defined(HAVE_SETSOCKOPT)
    {
# ifdef HAVE_GETTOSBYNAME
	struct tosent *tp;
	if (tos < 0 && (tp = gettosbyname("telnet", "tcp")))
	    tos = tp->t_tos;
# endif
	if (tos < 0)
	    tos = 020;	/* Low Delay bit */
	if (tos
	    && sa->sa_family == AF_INET
	    && (setsockopt(STDIN_FILENO, IPPROTO_IP, IP_TOS,
			   (void *)&tos, sizeof(tos)) < 0)
	    && (errno != ENOPROTOOPT) )
	    syslog(LOG_WARNING, "setsockopt (IP_TOS): %m");
    }
#endif	/* defined(IPPROTO_IP) && defined(IP_TOS) */
    net = STDIN_FILENO;
    doit(sa, sa_size);
    /* NOTREACHED */
    return 0;
}  /* end of main */

static void
usage(int exit_code)
{
    fprintf(stderr, "Usage: telnetd");
    fprintf(stderr, " [--help]");
    fprintf(stderr, " [--version]");
#ifdef	AUTHENTICATION
    fprintf(stderr, " [-a (debug|other|otp|user|valid|off|none)]\n\t");
#endif
    fprintf(stderr, " [-debug]");
#ifdef DIAGNOSTICS
    fprintf(stderr, " [-D (options|report|exercise|netdata|ptydata)]\n\t");
#endif
#ifdef	AUTHENTICATION
    fprintf(stderr, " [-edebug]");
#endif
    fprintf(stderr, " [-h]");
    fprintf(stderr, " [-L login]");
    fprintf(stderr, " [-n]");
#ifdef	_CRAY
    fprintf(stderr, " [-r[lowpty]-[highpty]]");
#endif
    fprintf(stderr, "\n\t");
#ifdef	HAVE_GETTOSBYNAME
    fprintf(stderr, " [-S tos]");
#endif
#ifdef	AUTHENTICATION
    fprintf(stderr, " [-X auth-type] [-y] [-z]");
#endif
    fprintf(stderr, " [-u utmp_hostname_length] [-U]");
    fprintf(stderr, " [port]\n");
    exit(exit_code);
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

int
getterminaltype(char *name, size_t name_sz)
{
    int retval = -1;

    settimer(baseline);
#ifdef AUTHENTICATION
    /*
     * Handle the Authentication option before we do anything else.
     */
    send_do(TELOPT_AUTHENTICATION, 1);
    while (his_will_wont_is_changing(TELOPT_AUTHENTICATION))
	ttloop();
    if (his_state_is_will(TELOPT_AUTHENTICATION)) {
	retval = auth_wait(name, name_sz);
    }
#endif

#ifdef ENCRYPTION
    send_will(TELOPT_ENCRYPT, 1);
    send_do(TELOPT_ENCRYPT, 1);	/* esc@magic.fi */
#endif
    send_do(TELOPT_TTYPE, 1);
    send_do(TELOPT_TSPEED, 1);
    send_do(TELOPT_XDISPLOC, 1);
    send_do(TELOPT_NEW_ENVIRON, 1);
    send_do(TELOPT_OLD_ENVIRON, 1);
    while (
#ifdef ENCRYPTION
	   his_do_dont_is_changing(TELOPT_ENCRYPT) ||
#endif
	   his_will_wont_is_changing(TELOPT_TTYPE) ||
	   his_will_wont_is_changing(TELOPT_TSPEED) ||
	   his_will_wont_is_changing(TELOPT_XDISPLOC) ||
	   his_will_wont_is_changing(TELOPT_NEW_ENVIRON) ||
	   his_will_wont_is_changing(TELOPT_OLD_ENVIRON)) {
	ttloop();
    }
#ifdef ENCRYPTION
    /*
     * Wait for the negotiation of what type of encryption we can
     * send with.  If autoencrypt is not set, this will just return.
     */
    if (his_state_is_will(TELOPT_ENCRYPT)) {
	encrypt_wait();
    }
    if (require_encryption) {

	while (encrypt_delay())
	    if (telnet_spin())
		fatal(net, "Failed while waiting for encryption");

	if (!encrypt_is_encrypting())
	    fatal(net, "Encryption required but not turned on by client");
    }
#endif
    if (his_state_is_will(TELOPT_TSPEED)) {
	static unsigned char sb[] =
	{ IAC, SB, TELOPT_TSPEED, TELQUAL_SEND, IAC, SE };

	telnet_net_write (sb, sizeof sb);
	DIAG(TD_OPTIONS, printsub('>', sb + 2, sizeof sb - 2););
    }
    if (his_state_is_will(TELOPT_XDISPLOC)) {
	static unsigned char sb[] =
	{ IAC, SB, TELOPT_XDISPLOC, TELQUAL_SEND, IAC, SE };

	telnet_net_write (sb, sizeof sb);
	DIAG(TD_OPTIONS, printsub('>', sb + 2, sizeof sb - 2););
    }
    if (his_state_is_will(TELOPT_NEW_ENVIRON)) {
	static unsigned char sb[] =
	{ IAC, SB, TELOPT_NEW_ENVIRON, TELQUAL_SEND, IAC, SE };

	telnet_net_write (sb, sizeof sb);
	DIAG(TD_OPTIONS, printsub('>', sb + 2, sizeof sb - 2););
    }
    else if (his_state_is_will(TELOPT_OLD_ENVIRON)) {
	static unsigned char sb[] =
	{ IAC, SB, TELOPT_OLD_ENVIRON, TELQUAL_SEND, IAC, SE };

	telnet_net_write (sb, sizeof sb);
	DIAG(TD_OPTIONS, printsub('>', sb + 2, sizeof sb - 2););
    }
    if (his_state_is_will(TELOPT_TTYPE)) {

	telnet_net_write (ttytype_sbbuf, sizeof ttytype_sbbuf);
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
	    strlcpy(first, terminaltype, sizeof(first));
	    for(;;) {
		/*
		 * Save the unknown name, and request the next name.
		 */
		strlcpy(last, terminaltype, sizeof(last));
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
		    if (strncmp(first, terminaltype, sizeof(first)) != 0)
			strlcpy(terminaltype, first, sizeof(terminaltype));
		    break;
		}
	    }
	}
    }
    return(retval);
}  /* end of getterminaltype */

void
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
    telnet_net_write (ttytype_sbbuf, sizeof ttytype_sbbuf);
    DIAG(TD_OPTIONS, printsub('>', ttytype_sbbuf + 2,
			      sizeof ttytype_sbbuf - 2););
    while (sequenceIs(ttypesubopt, baseline))
	ttloop();
}

int
terminaltypeok(char *s)
{
    return 1;
}


char host_name[MaxHostNameLen];
char remote_host_name[MaxHostNameLen];
char remote_utmp_name[MaxHostNameLen];

/*
 * Get a pty, scan input lines.
 */
static void
doit(struct sockaddr *who, int who_len)
{
    int level;
    int ptynum;
    char user_name[256];
    int error;

    /*
     * Find an available pty to use.
     */
    ourpty = getpty(&ptynum);
    if (ourpty < 0)
	fatal(net, "All network ports in use");

#ifdef _SC_CRAY_SECURE_SYS
    /*
     *	set ttyp line security label
     */
    if (secflag) {
	char slave_dev[16];

	snprintf(tty_dev, sizeof(tty_dev), "/dev/pty/%03d", ptynum);
	if (setdevs(tty_dev, &dv) < 0)
	    fatal(net, "cannot set pty security");
	snprintf(slave_dev, sizeof(slave_dev), "/dev/ttyp%03d", ptynum);
	if (setdevs(slave_dev, &dv) < 0)
	    fatal(net, "cannot set tty security");
    }
#endif	/* _SC_CRAY_SECURE_SYS */

    error = getnameinfo_verified (who, who_len,
				  remote_host_name,
				  sizeof(remote_host_name),
				  NULL, 0,
				  registerd_host_only ? NI_NAMEREQD : 0);
    if (error)
	fatal(net, "Couldn't resolve your address into a host name.\r\n\
Please contact your net administrator");

    gethostname(host_name, sizeof (host_name));

    strlcpy (remote_utmp_name, remote_host_name, sizeof(remote_utmp_name));

    /* Only trim if too long (and possible) */
    if (strlen(remote_utmp_name) > utmp_len) {
	char *domain = strchr(host_name, '.');
	char *p = strchr(remote_utmp_name, '.');
	if (domain != NULL && p != NULL && (strcmp(p, domain) == 0))
	    *p = '\0'; /* remove domain part */
    }

    /*
     * If hostname still doesn't fit utmp, use ipaddr.
     */
    if (strlen(remote_utmp_name) > utmp_len) {
	error = getnameinfo (who, who_len,
			     remote_utmp_name,
			     sizeof(remote_utmp_name),
			     NULL, 0,
			     NI_NUMERICHOST);
	if (error)
	    fatal(net, "Couldn't get numeric address\r\n");
    }

#ifdef AUTHENTICATION
    auth_encrypt_init(host_name, remote_host_name, "TELNETD", 1);
#endif

    init_env();

    /* begin server processing */

    /*
     * Initialize the slc mapping table.
     */

    get_slc_defaults();

    /*
     * get terminal type.
     */
    *user_name = 0;
    level = getterminaltype(user_name, sizeof(user_name));
    esetenv("TERM", terminaltype[0] ? terminaltype : "network", 1);

#ifdef _SC_CRAY_SECURE_SYS
    if (secflag) {
	if (setulvl(dv.dv_actlvl) < 0)
	    fatal(net,"cannot setulvl()");
	if (setucmp(dv.dv_actcmp) < 0)
	    fatal(net, "cannot setucmp()");
    }
#endif	/* _SC_CRAY_SECURE_SYS */

    my_telnet(net, ourpty, remote_host_name, remote_utmp_name,
	      level, user_name);
    /*NOTREACHED*/
}  /* end of doit */

/* output contents of /etc/issue.net, or /etc/issue */
static void
show_issue(void)
{
    FILE *f;
    char buf[128];
    f = fopen(SYSCONFDIR "/issue.net", "r");
    if(f == NULL)
	f = fopen(SYSCONFDIR "/issue", "r");
    if(f){
	while(fgets(buf, sizeof(buf), f) != NULL) {
	    size_t len = strcspn(buf, "\r\n");
	    if(len == strlen(buf)) {
		/* there's no newline */
		writenet(buf, len);
	    } else {
		/* replace newline with \r\n */
		buf[len] = '\0';
		writenet(buf, len);
		writenet("\r\n", 2);
	    }
	}
	fclose(f);
    }
}

/*
 * Main loop.  Select from pty and network, and
 * hand data to telnet receiver finite state machine.
 */
void
my_telnet(int f, int p, const char *host, const char *utmp_host,
	  int level, char *autoname)
{
    int on = 1;
    char *he;
    char *IM;
    int nfd;
    int startslave_called = 0;
    time_t timeout;

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
	DIAG(TD_OPTIONS,
	     {output_data("td: simulating recv\r\n");
	     });
	willoption(TELOPT_ECHO);
    }

    /*
     * Finally, to clean things up, we turn on our echo.  This
     * will break stupid 4.2 telnets out of local terminal echo.
     */

    if (my_state_is_wont(TELOPT_ECHO))
	send_will(TELOPT_ECHO, 1);

#ifdef TIOCPKT
#ifdef	STREAMSPTY
    if (!really_stream)
#endif
	/*
	 * Turn on packet mode
	 */
	ioctl(p, TIOCPKT, (char *)&on);
#endif


    /*
     * Call telrcv() once to pick up anything received during
     * terminal type negotiation, 4.2/4.3 determination, and
     * linemode negotiation.
     */
    telrcv();

    ioctl(f, FIONBIO, (char *)&on);
    ioctl(p, FIONBIO, (char *)&on);

#if	defined(SO_OOBINLINE) && defined(HAVE_SETSOCKOPT)
    setsockopt(net, SOL_SOCKET, SO_OOBINLINE,
	       (void *)&on, sizeof on);
#endif	/* defined(SO_OOBINLINE) */

#ifdef	SIGTSTP
    signal(SIGTSTP, SIG_IGN);
#endif
#ifdef	SIGTTOU
    /*
     * Ignoring SIGTTOU keeps the kernel from blocking us
     * in ttioct() in /sys/tty.c.
     */
    signal(SIGTTOU, SIG_IGN);
#endif

    signal(SIGCHLD, cleanup);

#ifdef  TIOCNOTTY
    {
	int t;
	t = open(_PATH_TTY, O_RDWR);
	if (t >= 0) {
	    ioctl(t, TIOCNOTTY, (char *)0);
	    close(t);
	}
    }
#endif

    show_issue();
    /*
     * Show banner that getty never gave.
     *
     * We put the banner in the pty input buffer.  This way, it
     * gets carriage return null processing, etc., just like all
     * other pty --> client data.
     */

    if (getenv("USER"))
	hostinfo = 0;

    IM = DEFAULT_IM;
    he = 0;
    edithost(he, host_name);
    if (hostinfo && *IM)
	putf(IM, ptyibuf2);

    if (pcc)
	strncat(ptyibuf2, ptyip, pcc+1);
    ptyip = ptyibuf2;
    pcc = strlen(ptyip);

    DIAG(TD_REPORT, {
	output_data("td: Entering processing loop\r\n");
    });


    nfd = ((f > p) ? f : p) + 1;
    timeout = time(NULL) + 5;
    for (;;) {
	fd_set ibits, obits, xbits;
	int c;

	/* wait for encryption to be turned on, but don't wait
           indefinitely */
	if(!startslave_called && (!encrypt_delay() || timeout > time(NULL))){
	    startslave_called = 1;
	    startslave(host, utmp_host, level, autoname);
	}

	if (ncc < 0 && pcc < 0)
	    break;

	FD_ZERO(&ibits);
	FD_ZERO(&obits);
	FD_ZERO(&xbits);

	if (f >= FD_SETSIZE
	    || p >= FD_SETSIZE)
	    fatal(net, "fd too large");

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
#ifndef SO_OOBINLINE
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

		ioctl(net, SIOCATMARK, (char *)&atmark);
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
	    DIAG((TD_REPORT | TD_NETDATA), {
		output_data("td: netread %d chars\r\n", ncc);
		});
	    DIAG(TD_NETDATA, printdata("nd", netip, ncc));
	}

	/*
	 * Something to read from the pty...
	 */
	if (FD_ISSET(p, &ibits)) {
#ifdef STREAMSPTY
	    if (really_stream)
		pcc = readstream(p, ptyibuf, BUFSIZ);
	    else
#endif
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
		if (ptyibuf[0] & TIOCPKT_FLUSHWRITE) {
		    netclear();	/* clear buffer back */
#ifndef	NO_URGENT
		    /*
		     * There are client telnets on some
		     * operating systems get screwed up
		     * royally if we send them urgent
		     * mode data.
		     */
		    output_data ("%c%c", IAC, DM);

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
	    if ((&netobuf[BUFSIZ] - nfrontp) < 3)
		break;
	    c = *ptyip++ & 0377, pcc--;
	    if (c == IAC)
		*nfrontp++ = c;
	    *nfrontp++ = c;
	    if ((c == '\r') && (my_state_is_wont(TELOPT_BINARY))) {
		if (pcc > 0 && ((*ptyip & 0377) == '\n')) {
		    *nfrontp++ = *ptyip++ & 0377;
		    pcc--;
		} else
		    *nfrontp++ = '\0';
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
}

#ifndef	TCSIG
# ifdef	TIOCSIG
#  define TCSIG TIOCSIG
# endif
#endif

#ifdef	STREAMSPTY

    int flowison = -1;  /* current state of flow: -1 is unknown */

int
readstream(int p, char *ibuf, int bufsize)
{
    int flags = 0;
    int ret = 0;
    struct termios *tsp;
#if 0
    struct termio *tp;
#endif
    struct iocblk *ip;
    char vstop, vstart;
    int ixon;
    int newflow;

    strbufc.maxlen = BUFSIZ;
    strbufc.buf = (char *)ctlbuf;
    strbufd.maxlen = bufsize-1;
    strbufd.len = 0;
    strbufd.buf = ibuf+1;
    ibuf[0] = 0;

    ret = getmsg(p, &strbufc, &strbufd, &flags);
    if (ret < 0)  /* error of some sort -- probably EAGAIN */
	return(-1);

    if (strbufc.len <= 0 || ctlbuf[0] == M_DATA) {
	/* data message */
	if (strbufd.len > 0) {			/* real data */
	    return(strbufd.len + 1);	/* count header char */
	} else {
	    /* nothing there */
	    errno = EAGAIN;
	    return(-1);
	}
    }

    /*
     * It's a control message.  Return 1, to look at the flag we set
     */

    switch (ctlbuf[0]) {
    case M_FLUSH:
	if (ibuf[1] & FLUSHW)
	    ibuf[0] = TIOCPKT_FLUSHWRITE;
	return(1);

    case M_IOCTL:
	ip = (struct iocblk *) (ibuf+1);

	switch (ip->ioc_cmd) {
#ifdef TCSETS
	case TCSETS:
	case TCSETSW:
	case TCSETSF:
	    tsp = (struct termios *)
		(ibuf+1 + sizeof(struct iocblk));
	    vstop = tsp->c_cc[VSTOP];
	    vstart = tsp->c_cc[VSTART];
	    ixon = tsp->c_iflag & IXON;
	    break;
#endif
#if 0
	case TCSETA:
	case TCSETAW:
	case TCSETAF:
	    tp = (struct termio *) (ibuf+1 + sizeof(struct iocblk));
	    vstop = tp->c_cc[VSTOP];
	    vstart = tp->c_cc[VSTART];
	    ixon = tp->c_iflag & IXON;
	    break;
#endif
	default:
	    errno = EAGAIN;
	    return(-1);
	}

	newflow =  (ixon && (vstart == 021) && (vstop == 023)) ? 1 : 0;
	if (newflow != flowison) {  /* it's a change */
	    flowison = newflow;
	    ibuf[0] = newflow ? TIOCPKT_DOSTOP : TIOCPKT_NOSTOP;
	    return(1);
	}
    }

    /* nothing worth doing anything about */
    errno = EAGAIN;
    return(-1);
}
#endif /* STREAMSPTY */

/*
 * Send interrupt to process on other side of pty.
 * If it is in raw mode, just write NULL;
 * otherwise, write intr char.
 */
void
interrupt()
{
    ptyflush();	/* half-hearted */

#if defined(STREAMSPTY) && defined(TIOCSIGNAL)
    /* Streams PTY style ioctl to post a signal */
    if (really_stream)
	{
	    int sig = SIGINT;
	    ioctl(ourpty, TIOCSIGNAL, &sig);
	    ioctl(ourpty, I_FLUSH, FLUSHR);
	}
#else
#ifdef	TCSIG
    ioctl(ourpty, TCSIG, (char *)SIGINT);
#else	/* TCSIG */
    init_termbuf();
    *pfrontp++ = slctab[SLC_IP].sptr ?
	(unsigned char)*slctab[SLC_IP].sptr : '\177';
#endif	/* TCSIG */
#endif
}

/*
 * Send quit to process on other side of pty.
 * If it is in raw mode, just write NULL;
 * otherwise, write quit char.
 */
void
sendbrk()
{
    ptyflush();	/* half-hearted */
#ifdef	TCSIG
    ioctl(ourpty, TCSIG, (char *)SIGQUIT);
#else	/* TCSIG */
    init_termbuf();
    *pfrontp++ = slctab[SLC_ABORT].sptr ?
	(unsigned char)*slctab[SLC_ABORT].sptr : '\034';
#endif	/* TCSIG */
}

void
sendsusp()
{
#ifdef	SIGTSTP
    ptyflush();	/* half-hearted */
# ifdef	TCSIG
    ioctl(ourpty, TCSIG, (char *)SIGTSTP);
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
recv_ayt()
{
#if	defined(SIGINFO) && defined(TCSIG)
    if (slctab[SLC_AYT].sptr && *slctab[SLC_AYT].sptr != _POSIX_VDISABLE) {
	ioctl(ourpty, TCSIG, (char *)SIGINFO);
	return;
    }
#endif
    output_data("\r\n[Yes]\r\n");
}

void
doeof()
{
    init_termbuf();

    *pfrontp++ = slctab[SLC_EOF].sptr ?
	(unsigned char)*slctab[SLC_EOF].sptr : '\004';
}
