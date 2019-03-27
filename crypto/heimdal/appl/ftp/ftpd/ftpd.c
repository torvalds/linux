/*
 * Copyright (c) 1985, 1988, 1990, 1992, 1993, 1994
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

#define	FTP_NAMES
#include "ftpd_locl.h"
#ifdef KRB5
#include <krb5.h>
#endif
#include "getarg.h"

RCSID("$Id$");

static char version[] = "Version 6.00";

extern	off_t restart_point;
extern	char cbuf[];

struct  sockaddr_storage ctrl_addr_ss;
struct  sockaddr *ctrl_addr = (struct sockaddr *)&ctrl_addr_ss;

struct  sockaddr_storage data_source_ss;
struct  sockaddr *data_source = (struct sockaddr *)&data_source_ss;

struct  sockaddr_storage data_dest_ss;
struct  sockaddr *data_dest = (struct sockaddr *)&data_dest_ss;

struct  sockaddr_storage his_addr_ss;
struct  sockaddr *his_addr = (struct sockaddr *)&his_addr_ss;

struct  sockaddr_storage pasv_addr_ss;
struct  sockaddr *pasv_addr = (struct sockaddr *)&pasv_addr_ss;

int	data;
int	logged_in;
struct	passwd *pw;
int	debug = 0;
int	ftpd_timeout = 900;    /* timeout after 15 minutes of inactivity */
int	maxtimeout = 7200;/* don't allow idle time to be set beyond 2 hours */
int	restricted_data_ports = 1;
int	logging;
int	guest;
int	dochroot;
int	type;
int	form;
int	stru;			/* avoid C keyword */
int	mode;
int	usedefault = 1;		/* for data transfers */
int	pdata = -1;		/* for passive mode */
int	allow_insecure_oob = 1;
static int transflag;
static int urgflag;
off_t	file_size;
off_t	byte_count;
#if !defined(CMASK) || CMASK == 0
#undef CMASK
#define CMASK 027
#endif
int	defumask = CMASK;		/* default umask value */
int	guest_umask = 0777;	/* Paranoia for anonymous users */
char	tmpline[10240];
char	hostname[MaxHostNameLen];
char	remotehost[MaxHostNameLen];
static char ttyline[20];
int     paranoid = 1;

#define AUTH_PLAIN	(1 << 0) /* allow sending passwords */
#define AUTH_OTP	(1 << 1) /* passwords are one-time */
#define AUTH_FTP	(1 << 2) /* allow anonymous login */

static int auth_level = 0; /* Only allow kerberos login by default */

/*
 * Timeout intervals for retrying connections
 * to hosts that don't accept PORT cmds.  This
 * is a kludge, but given the problems with TCP...
 */
#define	SWAITMAX	90	/* wait at most 90 seconds */
#define	SWAITINT	5	/* interval between retries */

int	swaitmax = SWAITMAX;
int	swaitint = SWAITINT;

#ifdef HAVE_SETPROCTITLE
char	proctitle[BUFSIZ];	/* initial part of title */
#endif /* HAVE_SETPROCTITLE */

#define LOGCMD(cmd, file) \
	if (logging > 1) \
	    syslog(LOG_INFO,"%s %s%s", cmd, \
		*(file) == '/' ? "" : curdir(), file);
#define LOGCMD2(cmd, file1, file2) \
	 if (logging > 1) \
	    syslog(LOG_INFO,"%s %s%s %s%s", cmd, \
		*(file1) == '/' ? "" : curdir(), file1, \
		*(file2) == '/' ? "" : curdir(), file2);
#define LOGBYTES(cmd, file, cnt) \
	if (logging > 1) { \
		if (cnt == (off_t)-1) \
		    syslog(LOG_INFO,"%s %s%s", cmd, \
			*(file) == '/' ? "" : curdir(), file); \
		else \
		    syslog(LOG_INFO, "%s %s%s = %ld bytes", \
			cmd, (*(file) == '/') ? "" : curdir(), file, (long)cnt); \
	}

static void	 ack (char *);
static void	 myoob (int);
static int	 handleoobcmd(void);
static int	 checkuser (char *, char *);
static int	 checkaccess (char *);
static FILE	*dataconn (const char *, off_t, const char *);
static void	 dolog (struct sockaddr *, int);
static void	 end_login (void);
static FILE	*getdatasock (const char *, int);
static char	*gunique (char *);
static RETSIGTYPE	 lostconn (int);
static int	 receive_data (FILE *, FILE *);
static void	 send_data (FILE *, FILE *);
static struct passwd * sgetpwnam (char *);

static char *
curdir(void)
{
	static char path[MaxPathLen+1];	/* path + '/' + '\0' */

	if (getcwd(path, sizeof(path)-1) == NULL)
		return ("");
	if (path[1] != '\0')		/* special case for root dir. */
		strlcat(path, "/", sizeof(path));
	/* For guest account, skip / since it's chrooted */
	return (guest ? path+1 : path);
}

#ifndef LINE_MAX
#define LINE_MAX 1024
#endif

static int
parse_auth_level(char *str)
{
    char *p;
    int ret = 0;
    char *foo = NULL;

    for(p = strtok_r(str, ",", &foo);
	p;
	p = strtok_r(NULL, ",", &foo)) {
	if(strcmp(p, "user") == 0)
	    ;
#ifdef OTP
	else if(strcmp(p, "otp") == 0)
	    ret |= AUTH_PLAIN|AUTH_OTP;
#endif
	else if(strcmp(p, "ftp") == 0 ||
		strcmp(p, "safe") == 0)
	    ret |= AUTH_FTP;
	else if(strcmp(p, "plain") == 0)
	    ret |= AUTH_PLAIN;
	else if(strcmp(p, "none") == 0)
	    ret |= AUTH_PLAIN|AUTH_FTP;
	else
	    warnx("bad value for -a: `%s'", p);
    }
    return ret;
}

/*
 * Print usage and die.
 */

static int interactive_flag;
static char *guest_umask_string;
static char *port_string;
static char *umask_string;
static char *auth_string;

int use_builtin_ls = -1;

static int help_flag;
static int version_flag;

static const char *good_chars = "+-=_,.";

struct getargs args[] = {
    { NULL, 'a', arg_string, &auth_string, "required authentication" },
    { NULL, 'i', arg_flag, &interactive_flag, "don't assume stdin is a socket" },
    { NULL, 'p', arg_string, &port_string, "what port to listen to" },
    { NULL, 'g', arg_string, &guest_umask_string, "umask for guest logins" },
    { NULL, 'l', arg_counter, &logging, "log more stuff", "" },
    { NULL, 't', arg_integer, &ftpd_timeout, "initial timeout" },
    { NULL, 'T', arg_integer, &maxtimeout, "max timeout" },
    { NULL, 'u', arg_string, &umask_string, "umask for user logins" },
    { NULL, 'U', arg_negative_flag, &restricted_data_ports, "don't use high data ports" },
    { NULL, 'd', arg_flag, &debug, "enable debugging" },
    { NULL, 'v', arg_flag, &debug, "enable debugging" },
    { "builtin-ls", 'B', arg_flag, &use_builtin_ls, "use built-in ls to list files" },
    { "good-chars", 0, arg_string, &good_chars, "allowed anonymous upload filename chars" },
    { "insecure-oob", 'I', arg_negative_flag, &allow_insecure_oob, "don't allow insecure OOB ABOR/STAT" },
#ifdef KRB5
    { "gss-bindings", 0,  arg_flag, &ftp_do_gss_bindings, "Require GSS-API bindings", NULL},
#endif
    { "version", 0, arg_flag, &version_flag },
    { "help", 'h', arg_flag, &help_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

static void
usage (int code)
{
    arg_printusage(args, num_args, NULL, "");
    exit (code);
}

/* output contents of a file */
static int
show_file(const char *file, int code)
{
    FILE *f;
    char buf[128];

    f = fopen(file, "r");
    if(f == NULL)
	return -1;
    while(fgets(buf, sizeof(buf), f)){
	buf[strcspn(buf, "\r\n")] = '\0';
	lreply(code, "%s", buf);
    }
    fclose(f);
    return 0;
}

int
main(int argc, char **argv)
{
    socklen_t his_addr_len, ctrl_addr_len;
    int on = 1;
    int port;
    struct servent *sp;

    int optind = 0;

    setprogname (argv[0]);

    if(getarg(args, num_args, argc, argv, &optind))
	usage(1);

    if(help_flag)
	usage(0);

    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    if(auth_string)
	auth_level = parse_auth_level(auth_string);
    {
	char *p;
	long val = 0;

	if(guest_umask_string) {
	    val = strtol(guest_umask_string, &p, 8);
	    if (*p != '\0' || val < 0)
		warnx("bad value for -g");
	    else
		guest_umask = val;
	}
	if(umask_string) {
	    val = strtol(umask_string, &p, 8);
	    if (*p != '\0' || val < 0)
		warnx("bad value for -u");
	    else
		defumask = val;
	}
    }
    sp = getservbyname("ftp", "tcp");
    if(sp)
	port = sp->s_port;
    else
	port = htons(21);
    if(port_string) {
	sp = getservbyname(port_string, "tcp");
	if(sp)
	    port = sp->s_port;
	else
	    if(isdigit((unsigned char)port_string[0]))
		port = htons(atoi(port_string));
	    else
		warnx("bad value for -p");
    }

    if (maxtimeout < ftpd_timeout)
	maxtimeout = ftpd_timeout;

#if 0
    if (ftpd_timeout > maxtimeout)
	ftpd_timeout = maxtimeout;
#endif

    if(interactive_flag)
	mini_inetd(port, NULL);

    /*
     * LOG_NDELAY sets up the logging connection immediately,
     * necessary for anonymous ftp's that chroot and can't do it later.
     */
    openlog("ftpd", LOG_PID | LOG_NDELAY, LOG_FTP);
    his_addr_len = sizeof(his_addr_ss);
    if (getpeername(STDIN_FILENO, his_addr, &his_addr_len) < 0) {
	syslog(LOG_ERR, "getpeername (%s): %m",argv[0]);
	exit(1);
    }
    ctrl_addr_len = sizeof(ctrl_addr_ss);
    if (getsockname(STDIN_FILENO, ctrl_addr, &ctrl_addr_len) < 0) {
	syslog(LOG_ERR, "getsockname (%s): %m",argv[0]);
	exit(1);
    }
#if defined(IP_TOS)
    if (ctrl_addr->sa_family == AF_INET)
	socket_set_tos(STDIN_FILENO, IP_TOS);
#endif
    data_source->sa_family = ctrl_addr->sa_family;
    socket_set_port (data_source,
		     htons(ntohs(socket_get_port(ctrl_addr)) - 1));

    /* set this here so it can be put in wtmp */
    snprintf(ttyline, sizeof(ttyline), "ftp%u", (unsigned)getpid());


    /*	freopen(_PATH_DEVNULL, "w", stderr); */
    signal(SIGPIPE, lostconn);
    signal(SIGCHLD, SIG_IGN);
#ifdef SIGURG
    if (signal(SIGURG, myoob) == SIG_ERR)
	syslog(LOG_ERR, "signal: %m");
#endif

    /* Try to handle urgent data inline */
#if defined(SO_OOBINLINE) && defined(HAVE_SETSOCKOPT)
    if (setsockopt(0, SOL_SOCKET, SO_OOBINLINE, (void *)&on,
		   sizeof(on)) < 0)
	syslog(LOG_ERR, "setsockopt: %m");
#endif

#ifdef	F_SETOWN
    if (fcntl(fileno(stdin), F_SETOWN, getpid()) == -1)
	syslog(LOG_ERR, "fcntl F_SETOWN: %m");
#endif
    dolog(his_addr, his_addr_len);
    /*
     * Set up default state
     */
    data = -1;
    type = TYPE_A;
    form = FORM_N;
    stru = STRU_F;
    mode = MODE_S;
    tmpline[0] = '\0';

    /* If logins are disabled, print out the message. */
    if(show_file(_PATH_NOLOGIN, 530) == 0) {
	reply(530, "System not available.");
	exit(0);
    }
    show_file(_PATH_FTPWELCOME, 220);
    /* reply(220,) must follow */
    gethostname(hostname, sizeof(hostname));

    reply(220, "%s FTP server (%s"
#ifdef KRB5
	  "+%s"
#endif
	  ") ready.", hostname, version
#ifdef KRB5
	  ,heimdal_version
#endif
	  );

    for (;;)
	yyparse();
    /* NOTREACHED */
}

static RETSIGTYPE
lostconn(int signo)
{

	if (debug)
		syslog(LOG_DEBUG, "lost connection");
	dologout(-1);
}

/*
 * Helper function for sgetpwnam().
 */
static char *
sgetsave(char *s)
{
	char *new = strdup(s);

	if (new == NULL) {
		perror_reply(421, "Local resource failure: malloc");
		dologout(1);
		/* NOTREACHED */
	}
	return new;
}

/*
 * Save the result of a getpwnam.  Used for USER command, since
 * the data returned must not be clobbered by any other command
 * (e.g., globbing).
 */
static struct passwd *
sgetpwnam(char *name)
{
	static struct passwd save;
	struct passwd *p;

	if ((p = k_getpwnam(name)) == NULL)
		return (p);
	if (save.pw_name) {
		free(save.pw_name);
		free(save.pw_passwd);
		free(save.pw_gecos);
		free(save.pw_dir);
		free(save.pw_shell);
	}
	save = *p;
	save.pw_name = sgetsave(p->pw_name);
	save.pw_passwd = sgetsave(p->pw_passwd);
	save.pw_gecos = sgetsave(p->pw_gecos);
	save.pw_dir = sgetsave(p->pw_dir);
	save.pw_shell = sgetsave(p->pw_shell);
	return (&save);
}

static int login_attempts;	/* number of failed login attempts */
static int askpasswd;		/* had user command, ask for passwd */
static char curname[10];	/* current USER name */
#ifdef OTP
OtpContext otp_ctx;
#endif

/*
 * USER command.
 * Sets global passwd pointer pw if named account exists and is acceptable;
 * sets askpasswd if a PASS command is expected.  If logged in previously,
 * need to reset state.  If name is "ftp" or "anonymous", the name is not in
 * _PATH_FTPUSERS, and ftp account exists, set guest and pw, then just return.
 * If account doesn't exist, ask for passwd anyway.  Otherwise, check user
 * requesting login privileges.  Disallow anyone who does not have a standard
 * shell as returned by getusershell().  Disallow anyone mentioned in the file
 * _PATH_FTPUSERS to allow people such as root and uucp to be avoided.
 */
void
user(char *name)
{
	char *cp, *shell;

	if(auth_level == 0 && !sec_complete){
	    reply(530, "No login allowed without authorization.");
	    return;
	}

	if (logged_in) {
		if (guest) {
			reply(530, "Can't change user from guest login.");
			return;
		} else if (dochroot) {
			reply(530, "Can't change user from chroot user.");
			return;
		}
		end_login();
	}

	guest = 0;
	if (strcmp(name, "ftp") == 0 || strcmp(name, "anonymous") == 0) {
	    if ((auth_level & AUTH_FTP) == 0 ||
		checkaccess("ftp") ||
		checkaccess("anonymous"))
		reply(530, "User %s access denied.", name);
	    else if ((pw = sgetpwnam("ftp")) != NULL) {
		guest = 1;
		defumask = guest_umask;	/* paranoia for incoming */
		askpasswd = 1;
		reply(331, "Guest login ok, type your name as password.");
	    } else
		reply(530, "User %s unknown.", name);
	    if (!askpasswd && logging) {
		char data_addr[256];

		if (inet_ntop (his_addr->sa_family,
			       socket_get_address(his_addr),
			       data_addr, sizeof(data_addr)) == NULL)
			strlcpy (data_addr, "unknown address",
					 sizeof(data_addr));

		syslog(LOG_NOTICE,
		       "ANONYMOUS FTP LOGIN REFUSED FROM %s(%s)",
		       remotehost, data_addr);
	    }
	    return;
	}
	if((auth_level & AUTH_PLAIN) == 0 && !sec_complete){
	    reply(530, "Only authorized and anonymous login allowed.");
	    return;
	}
	if ((pw = sgetpwnam(name))) {
		if ((shell = pw->pw_shell) == NULL || *shell == 0)
			shell = _PATH_BSHELL;
		while ((cp = getusershell()) != NULL)
			if (strcmp(cp, shell) == 0)
				break;
		endusershell();

		if (cp == NULL || checkaccess(name)) {
			reply(530, "User %s access denied.", name);
			if (logging) {
				char data_addr[256];

				if (inet_ntop (his_addr->sa_family,
					       socket_get_address(his_addr),
					       data_addr,
					       sizeof(data_addr)) == NULL)
					strlcpy (data_addr,
							 "unknown address",
							 sizeof(data_addr));

				syslog(LOG_NOTICE,
				       "FTP LOGIN REFUSED FROM %s(%s), %s",
				       remotehost,
				       data_addr,
				       name);
			}
			pw = (struct passwd *) NULL;
			return;
		}
	}
	if (logging)
	    strlcpy(curname, name, sizeof(curname));
	if(sec_complete) {
	    if(sec_userok(name) == 0) {
		do_login(232, name);
		sec_session(name);
	    } else
		reply(530, "User %s access denied.", name);
	} else {
#ifdef OTP
		char ss[256];

		if (otp_challenge(&otp_ctx, name, ss, sizeof(ss)) == 0) {
			reply(331, "Password %s for %s required.",
			      ss, name);
			askpasswd = 1;
		} else
#endif
		if ((auth_level & AUTH_OTP) == 0) {
		    reply(331, "Password required for %s.", name);
		    askpasswd = 1;
		} else {
#ifdef OTP
		    char *s;

		    if ((s = otp_error (&otp_ctx)) != NULL)
			lreply(530, "OTP: %s", s);
#endif
		    reply(530,
			  "Only authorized, anonymous"
#ifdef OTP
			  " and OTP "
#endif
			  "login allowed.");
		}

	}
	/*
	 * Delay before reading passwd after first failed
	 * attempt to slow down passwd-guessing programs.
	 */
	if (login_attempts)
		sleep(login_attempts);
}

/*
 * Check if a user is in the file "fname"
 */
static int
checkuser(char *fname, char *name)
{
	FILE *fd;
	int found = 0;
	char *p, line[BUFSIZ];

	if ((fd = fopen(fname, "r")) != NULL) {
		while (fgets(line, sizeof(line), fd) != NULL)
			if ((p = strchr(line, '\n')) != NULL) {
				*p = '\0';
				if (line[0] == '#')
					continue;
				if (strcmp(line, name) == 0) {
					found = 1;
					break;
				}
			}
		fclose(fd);
	}
	return (found);
}


/*
 * Determine whether a user has access, based on information in
 * _PATH_FTPUSERS. The users are listed one per line, with `allow'
 * or `deny' after the username. If anything other than `allow', or
 * just nothing, is given after the username, `deny' is assumed.
 *
 * If the user is not found in the file, but the pseudo-user `*' is,
 * the permission is taken from that line.
 *
 * This preserves the old semantics where if a user was listed in the
 * file he was denied, otherwise he was allowed.
 *
 * Return 1 if the user is denied, or 0 if he is allowed.  */

static int
match(const char *pattern, const char *string)
{
    return fnmatch(pattern, string, FNM_NOESCAPE);
}

static int
checkaccess(char *name)
{
#define ALLOWED		0
#define	NOT_ALLOWED	1
    FILE *fd;
    int allowed = ALLOWED;
    char *user, *perm, line[BUFSIZ];
    char *foo;

    fd = fopen(_PATH_FTPUSERS, "r");

    if(fd == NULL)
	return allowed;

    while (fgets(line, sizeof(line), fd) != NULL)  {
	foo = NULL;
	user = strtok_r(line, " \t\n", &foo);
	if (user == NULL || user[0] == '#')
	    continue;
	perm = strtok_r(NULL, " \t\n", &foo);
	if (match(user, name) == 0){
	    if(perm && strcmp(perm, "allow") == 0)
		allowed = ALLOWED;
	    else
		allowed = NOT_ALLOWED;
	    break;
	}
    }
    fclose(fd);
    return allowed;
}
#undef	ALLOWED
#undef	NOT_ALLOWED


int do_login(int code, char *passwd)
{
    login_attempts = 0;		/* this time successful */
    if (setegid((gid_t)pw->pw_gid) < 0) {
	reply(550, "Can't set gid.");
	return -1;
    }
    initgroups(pw->pw_name, pw->pw_gid);
#if defined(KRB5)
    if(k_hasafs())
	k_setpag();
#endif

    /* open wtmp before chroot */
    ftpd_logwtmp(ttyline, pw->pw_name, remotehost);
    logged_in = 1;

    dochroot = checkuser(_PATH_FTPCHROOT, pw->pw_name);
    if (guest) {
	/*
	 * We MUST do a chdir() after the chroot. Otherwise
	 * the old current directory will be accessible as "."
	 * outside the new root!
	 */
	if (chroot(pw->pw_dir) < 0 || chdir("/") < 0) {
	    reply(550, "Can't set guest privileges.");
	    return -1;
	}
    } else if (dochroot) {
	if (chroot(pw->pw_dir) < 0 || chdir("/") < 0) {
	    reply(550, "Can't change root.");
	    return -1;
	}
    } else if (chdir(pw->pw_dir) < 0) {
	if (chdir("/") < 0) {
	    reply(530, "User %s: can't change directory to %s.",
		  pw->pw_name, pw->pw_dir);
	    return -1;
	} else
	    lreply(code, "No directory! Logging in with home=/");
    }
    if (seteuid((uid_t)pw->pw_uid) < 0) {
	reply(550, "Can't set uid.");
	return -1;
    }

    if(use_builtin_ls == -1) {
	struct stat st;
	/* if /bin/ls exist and is a regular file, use it, otherwise
           use built-in ls */
	if(stat("/bin/ls", &st) == 0 &&
	   S_ISREG(st.st_mode))
	    use_builtin_ls = 0;
	else
	    use_builtin_ls = 1;
    }

    /*
     * Display a login message, if it exists.
     * N.B. reply(code,) must follow the message.
     */
    show_file(_PATH_FTPLOGINMESG, code);
    if(show_file(_PATH_ISSUE_NET, code) != 0)
	show_file(_PATH_ISSUE, code);
    if (guest) {
	reply(code, "Guest login ok, access restrictions apply.");
#ifdef HAVE_SETPROCTITLE
	snprintf (proctitle, sizeof(proctitle),
		  "%s: anonymous/%s",
		  remotehost,
		  passwd);
	setproctitle("%s", proctitle);
#endif /* HAVE_SETPROCTITLE */
	if (logging) {
	    char data_addr[256];

	    if (inet_ntop (his_addr->sa_family,
			   socket_get_address(his_addr),
			   data_addr, sizeof(data_addr)) == NULL)
		strlcpy (data_addr, "unknown address",
				 sizeof(data_addr));

	    syslog(LOG_INFO, "ANONYMOUS FTP LOGIN FROM %s(%s), %s",
		   remotehost,
		   data_addr,
		   passwd);
	}
    } else {
	reply(code, "User %s logged in.", pw->pw_name);
#ifdef HAVE_SETPROCTITLE
	snprintf(proctitle, sizeof(proctitle), "%s: %s", remotehost, pw->pw_name);
	setproctitle("%s", proctitle);
#endif /* HAVE_SETPROCTITLE */
	if (logging) {
	    char data_addr[256];

	    if (inet_ntop (his_addr->sa_family,
			   socket_get_address(his_addr),
			   data_addr, sizeof(data_addr)) == NULL)
		strlcpy (data_addr, "unknown address",
				 sizeof(data_addr));

	    syslog(LOG_INFO, "FTP LOGIN FROM %s(%s) as %s",
		   remotehost,
		   data_addr,
		   pw->pw_name);
	}
    }
    umask(defumask);
    return 0;
}

/*
 * Terminate login as previous user, if any, resetting state;
 * used when USER command is given or login fails.
 */
static void
end_login(void)
{

	if (seteuid((uid_t)0) < 0)
		fatal("Failed to seteuid");
	if (logged_in)
		ftpd_logwtmp(ttyline, "", "");
	pw = NULL;
	logged_in = 0;
	guest = 0;
	dochroot = 0;
}

#ifdef KRB5
static int
krb5_verify(struct passwd *pwd, char *passwd)
{
   krb5_context context;
   krb5_ccache  id;
   krb5_principal princ;
   krb5_error_code ret;

   ret = krb5_init_context(&context);
   if(ret)
        return ret;

  ret = krb5_parse_name(context, pwd->pw_name, &princ);
  if(ret){
        krb5_free_context(context);
        return ret;
  }
  ret = krb5_cc_new_unique(context, "MEMORY", NULL, &id);
  if(ret){
        krb5_free_principal(context, princ);
        krb5_free_context(context);
        return ret;
  }
  ret = krb5_verify_user(context,
                         princ,
                         id,
                         passwd,
                         1,
                         NULL);
  krb5_free_principal(context, princ);
  if (k_hasafs()) {
      krb5_afslog_uid_home(context, id,NULL, NULL,pwd->pw_uid, pwd->pw_dir);
  }
  krb5_cc_destroy(context, id);
  krb5_free_context (context);
  if(ret)
      return ret;
  return 0;
}
#endif /* KRB5 */

void
pass(char *passwd)
{
	int rval;

	/* some clients insists on sending a password */
	if (logged_in && askpasswd == 0){
	    reply(230, "Password not necessary");
	    return;
	}

	if (logged_in || askpasswd == 0) {
		reply(503, "Login with USER first.");
		return;
	}
	askpasswd = 0;
	rval = 1;
	if (!guest) {		/* "ftp" is only account allowed no password */
		if (pw == NULL)
			rval = 1;	/* failure below */
#ifdef OTP
		else if (otp_verify_user (&otp_ctx, passwd) == 0) {
		    rval = 0;
		}
#endif
		else if((auth_level & AUTH_OTP) == 0) {
#ifdef KRB5
		    rval = krb5_verify(pw, passwd);
#endif
		    if (rval)
			rval = unix_verify_user(pw->pw_name, passwd);
		} else {
#ifdef OTP
		    char *s;
		    if ((s = otp_error(&otp_ctx)) != NULL)
			lreply(530, "OTP: %s", s);
#endif
		}
		memset (passwd, 0, strlen(passwd));

		/*
		 * If rval == 1, the user failed the authentication
		 * check above.  If rval == 0, either Kerberos or
		 * local authentication succeeded.
		 */
		if (rval) {
			char data_addr[256];

			if (inet_ntop (his_addr->sa_family,
				       socket_get_address(his_addr),
				       data_addr, sizeof(data_addr)) == NULL)
				strlcpy (data_addr, "unknown address",
						 sizeof(data_addr));

			reply(530, "Login incorrect.");
			if (logging)
				syslog(LOG_NOTICE,
				    "FTP LOGIN FAILED FROM %s(%s), %s",
				       remotehost,
				       data_addr,
				       curname);
			pw = NULL;
			if (login_attempts++ >= 5) {
				syslog(LOG_NOTICE,
				       "repeated login failures from %s(%s)",
				       remotehost,
				       data_addr);
				exit(0);
			}
			return;
		}
	}
	if(!do_login(230, passwd))
	  return;

	/* Forget all about it... */
	end_login();
}

void
retrieve(const char *cmd, char *name)
{
	FILE *fin = NULL, *dout;
	struct stat st;
	int (*closefunc) (FILE *);
	char line[BUFSIZ];


	if (cmd == 0) {
		fin = fopen(name, "r");
		closefunc = fclose;
		st.st_size = 0;
		if(fin == NULL){
		    int save_errno = errno;
		    struct cmds {
			const char *ext;
			const char *cmd;
		        const char *rev_cmd;
		    } cmds[] = {
			{".tar", "/bin/gtar cPf - %s", NULL},
			{".tar.gz", "/bin/gtar zcPf - %s", NULL},
			{".tar.Z", "/bin/gtar ZcPf - %s", NULL},
			{".gz", "/bin/gzip -c -- %s", "/bin/gzip -c -d -- %s"},
			{".Z", "/bin/compress -c -- %s", "/bin/uncompress -c -- %s"},
			{NULL, NULL}
		    };
		    struct cmds *p;
		    for(p = cmds; p->ext; p++){
			char *tail = name + strlen(name) - strlen(p->ext);
			char c = *tail;

			if(strcmp(tail, p->ext) == 0 &&
			   (*tail  = 0) == 0 &&
			   access(name, R_OK) == 0){
			    snprintf (line, sizeof(line), p->cmd, name);
			    *tail  = c;
			    break;
			}
			*tail = c;
			if (p->rev_cmd != NULL) {
			    char *ext;
			    int ret;

			    ret = asprintf(&ext, "%s%s", name, p->ext);
			    if (ret != -1) {
  			        if (access(ext, R_OK) == 0) {
				    snprintf (line, sizeof(line),
					      p->rev_cmd, ext);
				    free(ext);
				    break;
				}
			        free(ext);
			    }
			}

		    }
		    if(p->ext){
			fin = ftpd_popen(line, "r", 0, 0);
			closefunc = ftpd_pclose;
			st.st_size = -1;
			cmd = line;
		    } else
			errno = save_errno;
		}
	} else {
		snprintf(line, sizeof(line), cmd, name);
		name = line;
		fin = ftpd_popen(line, "r", 1, 0);
		closefunc = ftpd_pclose;
		st.st_size = -1;
	}
	if (fin == NULL) {
		if (errno != 0) {
			perror_reply(550, name);
			if (cmd == 0) {
				LOGCMD("get", name);
			}
		}
		return;
	}
	byte_count = -1;
	if (cmd == 0){
	    if(fstat(fileno(fin), &st) < 0 || !S_ISREG(st.st_mode)) {
		reply(550, "%s: not a plain file.", name);
		goto done;
	    }
	}
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fin)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
		} else if (lseek(fileno(fin), restart_point, SEEK_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	dout = dataconn(name, st.st_size, "w");
	if (dout == NULL)
		goto done;
	set_buffer_size(fileno(dout), 0);
	send_data(fin, dout);
	fclose(dout);
	data = -1;
	pdata = -1;
done:
	if (cmd == 0)
		LOGBYTES("get", name, byte_count);
	(*closefunc)(fin);
}

/* filename sanity check */

int
filename_check(char *filename)
{
    char *p;

    p = strrchr(filename, '/');
    if(p)
	filename = p + 1;

    p = filename;

    if(isalnum((unsigned char)*p)){
	p++;
	while(*p && (isalnum((unsigned char)*p) || strchr(good_chars, (unsigned char)*p)))
	    p++;
	if(*p == '\0')
	    return 0;
    }
    lreply(553, "\"%s\" is not an acceptable filename.", filename);
    lreply(553, "The filename must start with an alphanumeric "
	   "character and must only");
    reply(553, "consist of alphanumeric characters or any of the following: %s",
	  good_chars);
    return 1;
}

void
do_store(char *name, char *mode, int unique)
{
	FILE *fout, *din;
	struct stat st;
	int (*closefunc) (FILE *);

	if(guest && filename_check(name))
	    return;
	if (unique) {
	    char *uname;
	    if (stat(name, &st) == 0) {
		if ((uname = gunique(name)) == NULL)
		    return;
		name = uname;
	    }
	    LOGCMD(*mode == 'w' ? "put" : "append", name);
	}

	if (restart_point)
		mode = "r+";
	fout = fopen(name, mode);
	closefunc = fclose;
	if (fout == NULL) {
		perror_reply(553, name);
		LOGCMD(*mode == 'w' ? "put" : "append", name);
		return;
	}
	byte_count = -1;
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fout)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
			/*
			 * We must do this seek to "current" position
			 * because we are changing from reading to
			 * writing.
			 */
			if (fseek(fout, 0L, SEEK_CUR) < 0) {
				perror_reply(550, name);
				goto done;
			}
		} else if (lseek(fileno(fout), restart_point, SEEK_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	din = dataconn(name, (off_t)-1, "r");
	if (din == NULL)
		goto done;
	set_buffer_size(fileno(din), 1);
	if (receive_data(din, fout) == 0) {
	    if((*closefunc)(fout) < 0)
		perror_reply(552, name);
	    else {
		if (unique)
			reply(226, "Transfer complete (unique file name:%s).",
			    name);
		else
			reply(226, "Transfer complete.");
	    }
	} else
	    (*closefunc)(fout);
	fclose(din);
	data = -1;
	pdata = -1;
done:
	LOGBYTES(*mode == 'w' ? "put" : "append", name, byte_count);
}

static FILE *
getdatasock(const char *mode, int domain)
{
	int s, t, tries;

	if (data >= 0)
		return (fdopen(data, mode));
	if (seteuid(0) < 0)
		fatal("Failed to seteuid");
	s = socket(domain, SOCK_STREAM, 0);
	if (s < 0)
		goto bad;
	socket_set_reuseaddr (s, 1);
	/* anchor socket to avoid multi-homing problems */
	socket_set_address_and_port (data_source,
				     socket_get_address (ctrl_addr),
				     socket_get_port (data_source));

	for (tries = 1; ; tries++) {
		if (bind(s, data_source,
			 socket_sockaddr_size (data_source)) >= 0)
			break;
		if (errno != EADDRINUSE || tries > 10)
			goto bad;
		sleep(tries);
	}
	if (seteuid(pw->pw_uid) < 0)
		fatal("Failed to seteuid");
#ifdef IPTOS_THROUGHPUT
	socket_set_tos (s, IPTOS_THROUGHPUT);
#endif
	return (fdopen(s, mode));
bad:
	/* Return the real value of errno (close may change it) */
	t = errno;
	if (seteuid((uid_t)pw->pw_uid) < 0)
		fatal("Failed to seteuid");
	close(s);
	errno = t;
	return (NULL);
}

static int
accept_with_timeout(int socket,
		    struct sockaddr *address,
		    socklen_t *address_len,
		    struct timeval *timeout)
{
    int ret;
    fd_set rfd;
    FD_ZERO(&rfd);
    FD_SET(socket, &rfd);
    ret = select(socket + 1, &rfd, NULL, NULL, timeout);
    if(ret < 0)
	return ret;
    if(ret == 0) {
	errno = ETIMEDOUT;
	return -1;
    }
    return accept(socket, address, address_len);
}

static FILE *
dataconn(const char *name, off_t size, const char *mode)
{
	char sizebuf[32];
	FILE *file;
	int domain, retry = 0;

	file_size = size;
	byte_count = 0;
	if (size >= 0)
	    snprintf(sizebuf, sizeof(sizebuf), " (%ld bytes)", (long)size);
	else
	    *sizebuf = '\0';
	if (pdata >= 0) {
		struct sockaddr_storage from_ss;
		struct sockaddr *from = (struct sockaddr *)&from_ss;
		struct timeval timeout;
		int s;
		socklen_t fromlen = sizeof(from_ss);

		timeout.tv_sec = 15;
		timeout.tv_usec = 0;
		s = accept_with_timeout(pdata, from, &fromlen, &timeout);
		if (s < 0) {
			reply(425, "Can't open data connection.");
			close(pdata);
			pdata = -1;
			return (NULL);
		}
		close(pdata);
		pdata = s;
#if defined(IPTOS_THROUGHPUT)
		if (from->sa_family == AF_INET)
		    socket_set_tos(s, IPTOS_THROUGHPUT);
#endif
		reply(150, "Opening %s mode data connection for '%s'%s.",
		     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
		return (fdopen(pdata, mode));
	}
	if (data >= 0) {
		reply(125, "Using existing data connection for '%s'%s.",
		    name, sizebuf);
		usedefault = 1;
		return (fdopen(data, mode));
	}
	if (usedefault)
		data_dest = his_addr;
	usedefault = 1;
	/*
	 * Default to using the same socket type as the ctrl address,
	 * unless we know the type of the data address.
	 */
	domain = data_dest->sa_family;
	if (domain == PF_UNSPEC)
	    domain = ctrl_addr->sa_family;

	file = getdatasock(mode, domain);
	if (file == NULL) {
		char data_addr[256];

		if (inet_ntop (data_source->sa_family,
			       socket_get_address(data_source),
			       data_addr, sizeof(data_addr)) == NULL)
			strlcpy (data_addr, "unknown address",
					 sizeof(data_addr));

		reply(425, "Can't create data socket (%s,%d): %s.",
		      data_addr,
		      socket_get_port (data_source),
		      strerror(errno));
		return (NULL);
	}
	data = fileno(file);
	while (connect(data, data_dest,
		       socket_sockaddr_size(data_dest)) < 0) {
		if (errno == EADDRINUSE && retry < swaitmax) {
			sleep(swaitint);
			retry += swaitint;
			continue;
		}
		perror_reply(425, "Can't build data connection");
		fclose(file);
		data = -1;
		return (NULL);
	}
	reply(150, "Opening %s mode data connection for '%s'%s.",
	     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
	return (file);
}

/*
 * Tranfer the contents of "instr" to "outstr" peer using the appropriate
 * encapsulation of the data subject * to Mode, Structure, and Type.
 *
 * NB: Form isn't handled.
 */
static void
send_data(FILE *instr, FILE *outstr)
{
	int c, cnt, filefd, netfd;
	static char *buf;
	static size_t bufsize;

	transflag = 1;
	switch (type) {

	case TYPE_A:
	    while ((c = getc(instr)) != EOF) {
		if (urgflag && handleoobcmd())
		    return;
		byte_count++;
		if(c == '\n')
		    sec_putc('\r', outstr);
		sec_putc(c, outstr);
	    }
	    sec_fflush(outstr);
	    transflag = 0;
	    urgflag = 0;
	    if (ferror(instr))
		goto file_err;
	    if (ferror(outstr))
		goto data_err;
	    reply(226, "Transfer complete.");
	    return;

	case TYPE_I:
	case TYPE_L:
#if 0 /* XXX handle urg flag */
#if defined(HAVE_MMAP) && !defined(NO_MMAP)
#ifndef MAP_FAILED
#define MAP_FAILED (-1)
#endif
	    {
		struct stat st;
		char *chunk;
		int in = fileno(instr);
		if(fstat(in, &st) == 0 && S_ISREG(st.st_mode)
		   && st.st_size > 0) {
		    /*
		     * mmap zero bytes has potential of loosing, don't do it.
		     */
		    chunk = mmap(0, st.st_size, PROT_READ,
				 MAP_SHARED, in, 0);
		    if((void *)chunk != (void *)MAP_FAILED) {
			cnt = st.st_size - restart_point;
			sec_write(fileno(outstr), chunk + restart_point, cnt);
			if (munmap(chunk, st.st_size) < 0)
			    warn ("munmap");
			sec_fflush(outstr);
			byte_count = cnt;
			transflag = 0;
			urgflag = 0;
		    }
		}
	    }
#endif
#endif
	if(transflag) {
	    struct stat st;

	    netfd = fileno(outstr);
	    filefd = fileno(instr);
	    buf = alloc_buffer (buf, &bufsize,
				fstat(filefd, &st) >= 0 ? &st : NULL);
	    if (buf == NULL) {
		transflag = 0;
		urgflag = 0;
		perror_reply(451, "Local resource failure: malloc");
		return;
	    }
	    while ((cnt = read(filefd, buf, bufsize)) > 0 &&
		   sec_write(netfd, buf, cnt) == cnt) {
		byte_count += cnt;
		if (urgflag && handleoobcmd())
		    return;
	    }
	    sec_fflush(outstr); /* to end an encrypted stream */
	    transflag = 0;
	    urgflag = 0;
	    if (cnt != 0) {
		if (cnt < 0)
		    goto file_err;
		goto data_err;
	    }
	}
	reply(226, "Transfer complete.");
	return;
	default:
	    transflag = 0;
	    urgflag = 0;
	    reply(550, "Unimplemented TYPE %d in send_data", type);
	    return;
	}

data_err:
	transflag = 0;
	urgflag = 0;
	perror_reply(426, "Data connection");
	return;

file_err:
	transflag = 0;
	urgflag = 0;
	perror_reply(551, "Error on input file");
}

/*
 * Transfer data from peer to "outstr" using the appropriate encapulation of
 * the data subject to Mode, Structure, and Type.
 *
 * N.B.: Form isn't handled.
 */
static int
receive_data(FILE *instr, FILE *outstr)
{
    int cnt, bare_lfs = 0;
    static char *buf;
    static size_t bufsize;
    struct stat st;

    transflag = 1;

    buf = alloc_buffer (buf, &bufsize,
			fstat(fileno(outstr), &st) >= 0 ? &st : NULL);
    if (buf == NULL) {
	transflag = 0;
	urgflag = 0;
	perror_reply(451, "Local resource failure: malloc");
	return -1;
    }

    switch (type) {

    case TYPE_I:
    case TYPE_L:
	while ((cnt = sec_read(fileno(instr), buf, bufsize)) > 0) {
	    if (write(fileno(outstr), buf, cnt) != cnt)
		goto file_err;
	    byte_count += cnt;
	    if (urgflag && handleoobcmd())
		return (-1);
	}
	if (cnt < 0)
	    goto data_err;
	transflag = 0;
	urgflag = 0;
	return (0);

    case TYPE_E:
	reply(553, "TYPE E not implemented.");
	transflag = 0;
	urgflag = 0;
	return (-1);

    case TYPE_A:
    {
	char *p, *q;
	int cr_flag = 0;
	while ((cnt = sec_read(fileno(instr),
				buf + cr_flag,
				bufsize - cr_flag)) > 0){
	    if (urgflag && handleoobcmd())
		return (-1);
	    byte_count += cnt;
	    cnt += cr_flag;
	    cr_flag = 0;
	    for(p = buf, q = buf; p < buf + cnt;) {
		if(*p == '\n')
		    bare_lfs++;
		if(*p == '\r') {
		    if(p == buf + cnt - 1){
			cr_flag = 1;
			p++;
			continue;
		    }else if(p[1] == '\n'){
			*q++ = '\n';
			p += 2;
			continue;
		    }
		}
		*q++ = *p++;
	    }
	    fwrite(buf, q - buf, 1, outstr);
	    if(cr_flag)
		buf[0] = '\r';
	}
	if(cr_flag)
	    putc('\r', outstr);
	fflush(outstr);
	if (ferror(instr))
	    goto data_err;
	if (ferror(outstr))
	    goto file_err;
	transflag = 0;
	urgflag = 0;
	if (bare_lfs) {
	    lreply(226, "WARNING! %d bare linefeeds received in ASCII mode\r\n"
		   "    File may not have transferred correctly.\r\n",
		   bare_lfs);
	}
	return (0);
    }
    default:
	reply(550, "Unimplemented TYPE %d in receive_data", type);
	transflag = 0;
	urgflag = 0;
	return (-1);
    }

data_err:
    transflag = 0;
    urgflag = 0;
    perror_reply(426, "Data Connection");
    return (-1);

file_err:
    transflag = 0;
    urgflag = 0;
    perror_reply(452, "Error writing file");
    return (-1);
}

void
statfilecmd(char *filename)
{
	FILE *fin;
	int c;
	char line[LINE_MAX];

	snprintf(line, sizeof(line), "/bin/ls -la -- %s", filename);
	fin = ftpd_popen(line, "r", 1, 0);
	lreply(211, "status of %s:", filename);
	while ((c = getc(fin)) != EOF) {
		if (c == '\n') {
			if (ferror(stdout)){
				perror_reply(421, "control connection");
				ftpd_pclose(fin);
				dologout(1);
				/* NOTREACHED */
			}
			if (ferror(fin)) {
				perror_reply(551, filename);
				ftpd_pclose(fin);
				return;
			}
			putc('\r', stdout);
		}
		putc(c, stdout);
	}
	ftpd_pclose(fin);
	reply(211, "End of Status");
}

void
statcmd(void)
{
#if 0
	struct sockaddr_in *sin;
	u_char *a, *p;

	lreply(211, "%s FTP server (%s) status:", hostname, version);
	printf("     %s\r\n", version);
	printf("     Connected to %s", remotehost);
	if (!isdigit((unsigned char)remotehost[0]))
		printf(" (%s)", inet_ntoa(his_addr.sin_addr));
	printf("\r\n");
	if (logged_in) {
		if (guest)
			printf("     Logged in anonymously\r\n");
		else
			printf("     Logged in as %s\r\n", pw->pw_name);
	} else if (askpasswd)
		printf("     Waiting for password\r\n");
	else
		printf("     Waiting for user name\r\n");
	printf("     TYPE: %s", typenames[type]);
	if (type == TYPE_A || type == TYPE_E)
		printf(", FORM: %s", formnames[form]);
	if (type == TYPE_L)
#if NBBY == 8
		printf(" %d", NBBY);
#else
		printf(" %d", bytesize);	/* need definition! */
#endif
	printf("; STRUcture: %s; transfer MODE: %s\r\n",
	    strunames[stru], modenames[mode]);
	if (data != -1)
		printf("     Data connection open\r\n");
	else if (pdata != -1) {
		printf("     in Passive mode");
		sin = &pasv_addr;
		goto printaddr;
	} else if (usedefault == 0) {
		printf("     PORT");
		sin = &data_dest;
printaddr:
		a = (u_char *) &sin->sin_addr;
		p = (u_char *) &sin->sin_port;
#define UC(b) (((int) b) & 0xff)
		printf(" (%d,%d,%d,%d,%d,%d)\r\n", UC(a[0]),
			UC(a[1]), UC(a[2]), UC(a[3]), UC(p[0]), UC(p[1]));
#undef UC
	} else
		printf("     No data connection\r\n");
#endif
	reply(211, "End of status");
}

void
fatal(char *s)
{

	reply(451, "Error in server: %s\n", s);
	reply(221, "Closing connection due to server error.");
	dologout(0);
	/* NOTREACHED */
}

static void
int_reply(int, char *, const char *, va_list)
#ifdef __GNUC__
__attribute__ ((format (printf, 3, 0)))
#endif
;

static void
int_reply(int n, char *c, const char *fmt, va_list ap)
{
    char buf[10240];
    char *p;
    p=buf;
    if(n){
	snprintf(p, sizeof(buf), "%d%s", n, c);
	p+=strlen(p);
    }
    vsnprintf(p, sizeof(buf) - strlen(p), fmt, ap);
    p+=strlen(p);
    snprintf(p, sizeof(buf) - strlen(p), "\r\n");
    p+=strlen(p);
    sec_fprintf(stdout, "%s", buf);
    fflush(stdout);
    if (debug)
	syslog(LOG_DEBUG, "<--- %s- ", buf);
}

void
reply(int n, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int_reply(n, " ", fmt, ap);
  delete_ftp_command();
  va_end(ap);
}

void
lreply(int n, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int_reply(n, "-", fmt, ap);
  va_end(ap);
}

void
nreply(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int_reply(0, NULL, fmt, ap);
  va_end(ap);
}

static void
ack(char *s)
{

	reply(250, "%s command successful.", s);
}

void
nack(char *s)
{

	reply(502, "%s command not implemented.", s);
}

void
do_delete(char *name)
{
	struct stat st;

	LOGCMD("delete", name);
	if (stat(name, &st) < 0) {
		perror_reply(550, name);
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		if (rmdir(name) < 0) {
			perror_reply(550, name);
			return;
		}
		goto done;
	}
	if (unlink(name) < 0) {
		perror_reply(550, name);
		return;
	}
done:
	ack("DELE");
}

void
cwd(const char *path)
{

	if (chdir(path) < 0)
		perror_reply(550, path);
	else
		ack("CWD");
}

void
makedir(char *name)
{

	LOGCMD("mkdir", name);
	if(guest && filename_check(name))
	    return;
	if (mkdir(name, 0777) < 0)
		perror_reply(550, name);
	else{
	    if(guest)
		chmod(name, 0700); /* guest has umask 777 */
	    reply(257, "MKD command successful.");
	}
}

void
removedir(char *name)
{

	LOGCMD("rmdir", name);
	if (rmdir(name) < 0)
		perror_reply(550, name);
	else
		ack("RMD");
}

void
pwd(void)
{
    char path[MaxPathLen];
    char *ret;

    /* SunOS has a broken getcwd that does popen(pwd) (!!!), this
     * failes miserably when running chroot
     */
    ret = getcwd(path, sizeof(path));
    if (ret == NULL)
	reply(550, "%s.", strerror(errno));
    else
	reply(257, "\"%s\" is current directory.", path);
}

char *
renamefrom(char *name)
{
	struct stat st;

	if (stat(name, &st) < 0) {
		perror_reply(550, name);
		return NULL;
	}
	reply(350, "File exists, ready for destination name");
	return (name);
}

void
renamecmd(char *from, char *to)
{

	LOGCMD2("rename", from, to);
	if(guest && filename_check(to))
	    return;
	if (rename(from, to) < 0)
		perror_reply(550, "rename");
	else
		ack("RNTO");
}

static void
dolog(struct sockaddr *sa, int len)
{
	getnameinfo_verified (sa, len, remotehost, sizeof(remotehost),
			      NULL, 0, 0);
#ifdef HAVE_SETPROCTITLE
	snprintf(proctitle, sizeof(proctitle), "%s: connected", remotehost);
	setproctitle("%s", proctitle);
#endif /* HAVE_SETPROCTITLE */

	if (logging) {
		char data_addr[256];

		if (inet_ntop (his_addr->sa_family,
			       socket_get_address(his_addr),
			       data_addr, sizeof(data_addr)) == NULL)
			strlcpy (data_addr, "unknown address",
					 sizeof(data_addr));


		syslog(LOG_INFO, "connection from %s(%s)",
		       remotehost,
		       data_addr);
	}
}

/*
 * Record logout in wtmp file
 * and exit with supplied status.
 */
void
dologout(int status)
{
    transflag = 0;
    urgflag = 0;
    if (logged_in) {
#if KRB5
	cond_kdestroy();
#endif
	seteuid((uid_t)0); /* No need to check, we call exit() below */
	ftpd_logwtmp(ttyline, "", "");
    }
    /* beware of flushing buffers after a SIGPIPE */
#ifdef XXX
    exit(status);
#else
    _exit(status);
#endif
}

void abor(void)
{
    if (!transflag)
	return;
    reply(426, "Transfer aborted. Data connection closed.");
    reply(226, "Abort successful");
    transflag = 0;
}

static void
myoob(int signo)
{
    urgflag = 1;
}

static char *
mec_space(char *p)
{
    while(isspace(*(unsigned char *)p))
	  p++;
    return p;
}

static int
handleoobcmd(void)
{
	char *cp;

	/* only process if transfer occurring */
	if (!transflag)
		return 0;

	urgflag = 0;

	cp = tmpline;
	if (ftpd_getline(cp, sizeof(tmpline)) == NULL) {
		reply(221, "You could at least say goodbye.");
		dologout(0);
	}

	if (strncasecmp("MIC", cp, 3) == 0) {
	    mec(mec_space(cp + 3), prot_safe);
	} else if (strncasecmp("CONF", cp, 4) == 0) {
	    mec(mec_space(cp + 4), prot_confidential);
	} else if (strncasecmp("ENC", cp, 3) == 0) {
	    mec(mec_space(cp + 3), prot_private);
	} else if (!allow_insecure_oob) {
	    reply(533, "Command protection level denied "
		  "for paranoid reasons.");
	    goto out;
	}

	if (secure_command())
	    cp = ftp_command;

	if (strcasecmp(cp, "ABOR\r\n") == 0) {
		abor();
	} else if (strcasecmp(cp, "STAT\r\n") == 0) {
		if (file_size != (off_t) -1)
			reply(213, "Status: %ld of %ld bytes transferred",
			      (long)byte_count,
			      (long)file_size);
		else
			reply(213, "Status: %ld bytes transferred",
			      (long)byte_count);
	}
out:
	return (transflag == 0);
}

/*
 * Note: a response of 425 is not mentioned as a possible response to
 *	the PASV command in RFC959. However, it has been blessed as
 *	a legitimate response by Jon Postel in a telephone conversation
 *	with Rick Adams on 25 Jan 89.
 */
void
pasv(void)
{
	socklen_t len;
	char *p, *a;
	struct sockaddr_in *sin;

	if (ctrl_addr->sa_family != AF_INET) {
		reply(425,
		      "You cannot do PASV with something that's not IPv4");
		return;
	}

	if(pdata != -1)
	    close(pdata);

	pdata = socket(ctrl_addr->sa_family, SOCK_STREAM, 0);
	if (pdata < 0) {
		perror_reply(425, "Can't open passive connection");
		return;
	}
	pasv_addr->sa_family = ctrl_addr->sa_family;
	socket_set_address_and_port (pasv_addr,
				     socket_get_address (ctrl_addr),
				     0);
	socket_set_portrange(pdata, restricted_data_ports,
	    pasv_addr->sa_family);
	if (seteuid(0) < 0)
		fatal("Failed to seteuid");
	if (bind(pdata, pasv_addr, socket_sockaddr_size (pasv_addr)) < 0) {
		if (seteuid(pw->pw_uid) < 0)
			fatal("Failed to seteuid");
		goto pasv_error;
	}
	if (seteuid(pw->pw_uid) < 0)
		fatal("Failed to seteuid");
	len = sizeof(pasv_addr_ss);
	if (getsockname(pdata, pasv_addr, &len) < 0)
		goto pasv_error;
	if (listen(pdata, 1) < 0)
		goto pasv_error;
	sin = (struct sockaddr_in *)pasv_addr;
	a = (char *) &sin->sin_addr;
	p = (char *) &sin->sin_port;

#define UC(b) (((int) b) & 0xff)

	reply(227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)", UC(a[0]),
		UC(a[1]), UC(a[2]), UC(a[3]), UC(p[0]), UC(p[1]));
	return;

pasv_error:
	close(pdata);
	pdata = -1;
	perror_reply(425, "Can't open passive connection");
	return;
}

void
epsv(char *proto)
{
	socklen_t len;

	pdata = socket(ctrl_addr->sa_family, SOCK_STREAM, 0);
	if (pdata < 0) {
		perror_reply(425, "Can't open passive connection");
		return;
	}
	pasv_addr->sa_family = ctrl_addr->sa_family;
	socket_set_address_and_port (pasv_addr,
				     socket_get_address (ctrl_addr),
				     0);
	socket_set_portrange(pdata, restricted_data_ports,
	    pasv_addr->sa_family);
	if (seteuid(0) < 0)
		fatal("Failed to seteuid");
	if (bind(pdata, pasv_addr, socket_sockaddr_size (pasv_addr)) < 0) {
		if (seteuid(pw->pw_uid))
			fatal("Failed to seteuid");
		goto pasv_error;
	}
	if (seteuid(pw->pw_uid) < 0)
		fatal("Failed to seteuid");
	len = sizeof(pasv_addr_ss);
	if (getsockname(pdata, pasv_addr, &len) < 0)
		goto pasv_error;
	if (listen(pdata, 1) < 0)
		goto pasv_error;

	reply(229, "Entering Extended Passive Mode (|||%d|)",
	      ntohs(socket_get_port (pasv_addr)));
	return;

pasv_error:
	close(pdata);
	pdata = -1;
	perror_reply(425, "Can't open passive connection");
	return;
}

void
eprt(char *str)
{
	char *end;
	char sep;
	int af;
	int ret;
	int port;

	usedefault = 0;
	if (pdata >= 0) {
	    close(pdata);
	    pdata = -1;
	}

	sep = *str++;
	if (sep == '\0') {
		reply(500, "Bad syntax in EPRT");
		return;
	}
	af = strtol (str, &end, 0);
	if (af == 0 || *end != sep) {
		reply(500, "Bad syntax in EPRT");
		return;
	}
	str = end + 1;
	switch (af) {
#ifdef HAVE_IPV6
	case 2 :
	    data_dest->sa_family = AF_INET6;
	    break;
#endif
	case 1 :
	    data_dest->sa_family = AF_INET;
		break;
	default :
		reply(522, "Network protocol %d not supported, use (1"
#ifdef HAVE_IPV6
		      ",2"
#endif
		      ")", af);
		return;
	}
	end = strchr (str, sep);
	if (end == NULL) {
		reply(500, "Bad syntax in EPRT");
		return;
	}
	*end = '\0';
	ret = inet_pton (data_dest->sa_family, str,
			 socket_get_address (data_dest));

	if (ret != 1) {
		reply(500, "Bad address syntax in EPRT");
		return;
	}
	str = end + 1;
	port = strtol (str, &end, 0);
	if (port == 0 || *end != sep) {
		reply(500, "Bad port syntax in EPRT");
		return;
	}
	if (port < IPPORT_RESERVED) {
		reply(500, "Bad port in invalid range in EPRT");
		return;
	}
	socket_set_port (data_dest, htons(port));

	if (paranoid &&
	    (data_dest->sa_family != his_addr->sa_family ||
	     memcmp(socket_get_address(data_dest), socket_get_address(his_addr), socket_sockaddr_size(data_dest)) != 0))
	{
		reply(500, "Bad address in EPRT");
	}
	reply(200, "EPRT command successful.");
}

/*
 * Generate unique name for file with basename "local".
 * The file named "local" is already known to exist.
 * Generates failure reply on error.
 */
static char *
gunique(char *local)
{
	static char new[MaxPathLen];
	struct stat st;
	int count;
	char *cp;

	cp = strrchr(local, '/');
	if (cp)
		*cp = '\0';
	if (stat(cp ? local : ".", &st) < 0) {
		perror_reply(553, cp ? local : ".");
		return NULL;
	}
	if (cp)
		*cp = '/';
	for (count = 1; count < 100; count++) {
		snprintf (new, sizeof(new), "%s.%d", local, count);
		if (stat(new, &st) < 0)
			return (new);
	}
	reply(452, "Unique file name cannot be created.");
	return (NULL);
}

/*
 * Format and send reply containing system error number.
 */
void
perror_reply(int code, const char *string)
{
	reply(code, "%s: %s.", string, strerror(errno));
}

static char *onefile[] = {
	"",
	0
};

void
list_file(char *file)
{
    if(use_builtin_ls) {
	FILE *dout;
	dout = dataconn(file, -1, "w");
	if (dout == NULL)
	    return;
	set_buffer_size(fileno(dout), 0);
	if(builtin_ls(dout, file) == 0)
	    reply(226, "Transfer complete.");
	else
	    reply(451, "Requested action aborted. Local error in processing.");
	fclose(dout);
	data = -1;
	pdata = -1;
    } else {
#ifdef HAVE_LS_A
	const char *cmd = "/bin/ls -lA %s";
#else
	const char *cmd = "/bin/ls -la %s";
#endif
	retrieve(cmd, file);
    }
}

void
send_file_list(char *whichf)
{
    struct stat st;
    DIR *dirp = NULL;
    struct dirent *dir;
    FILE *dout = NULL;
    char **dirlist, *dirname;
    int simple = 0;
    int freeglob = 0;
    glob_t gl;
    char buf[MaxPathLen];

    if (strpbrk(whichf, "~{[*?") != NULL) {
	int flags = GLOB_BRACE|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE|
#ifdef GLOB_MAXPATH
	    GLOB_MAXPATH
#else
	    GLOB_LIMIT
#endif
	    ;

	memset(&gl, 0, sizeof(gl));
	freeglob = 1;
	if (glob(whichf, flags, 0, &gl)) {
	    reply(550, "not found");
	    goto out;
	} else if (gl.gl_pathc == 0) {
	    errno = ENOENT;
	    perror_reply(550, whichf);
	    goto out;
	}
	dirlist = gl.gl_pathv;
    } else {
	onefile[0] = whichf;
	dirlist = onefile;
	simple = 1;
    }

    while ((dirname = *dirlist++)) {

	if (urgflag && handleoobcmd())
	    goto out;

	if (stat(dirname, &st) < 0) {
	    /*
	     * If user typed "ls -l", etc, and the client
	     * used NLST, do what the user meant.
	     */
	    if (dirname[0] == '-' && *dirlist == NULL &&
		transflag == 0) {
		list_file(dirname);
		goto out;
	    }
	    perror_reply(550, whichf);
	    goto out;
	}

	if (S_ISREG(st.st_mode)) {
	    if (dout == NULL) {
		dout = dataconn("file list", (off_t)-1, "w");
		if (dout == NULL)
		    goto out;
		transflag = 1;
	    }
	    snprintf(buf, sizeof(buf), "%s%s\n", dirname,
		     type == TYPE_A ? "\r" : "");
	    sec_write(fileno(dout), buf, strlen(buf));
	    byte_count += strlen(dirname) + 1;
	    continue;
	} else if (!S_ISDIR(st.st_mode))
	    continue;

	if ((dirp = opendir(dirname)) == NULL)
	    continue;

	while ((dir = readdir(dirp)) != NULL) {
	    char nbuf[MaxPathLen];

	    if (urgflag && handleoobcmd())
		goto out;

	    if (!strcmp(dir->d_name, "."))
		continue;
	    if (!strcmp(dir->d_name, ".."))
		continue;

	    snprintf(nbuf, sizeof(nbuf), "%s/%s", dirname, dir->d_name);

	    /*
	     * We have to do a stat to insure it's
	     * not a directory or special file.
	     */
	    if (simple || (stat(nbuf, &st) == 0 &&
			   S_ISREG(st.st_mode))) {
		if (dout == NULL) {
		    dout = dataconn("file list", (off_t)-1, "w");
		    if (dout == NULL)
			goto out;
		    transflag = 1;
		}
		if(strncmp(nbuf, "./", 2) == 0)
		    snprintf(buf, sizeof(buf), "%s%s\n", nbuf +2,
			     type == TYPE_A ? "\r" : "");
		else
		    snprintf(buf, sizeof(buf), "%s%s\n", nbuf,
			     type == TYPE_A ? "\r" : "");
		sec_write(fileno(dout), buf, strlen(buf));
		byte_count += strlen(nbuf) + 1;
	    }
	}
	closedir(dirp);
    }
    if (dout == NULL)
	reply(550, "No files found.");
    else if (ferror(dout) != 0)
	perror_reply(550, "Data connection");
    else
	reply(226, "Transfer complete.");

out:
    transflag = 0;
    if (dout != NULL){
	sec_write(fileno(dout), buf, 0); /* XXX flush */

	fclose(dout);
    }
    data = -1;
    pdata = -1;
    if (freeglob)
	globfree(&gl);
}


int
find(char *pattern)
{
    char line[1024];
    FILE *f;

    snprintf(line, sizeof(line),
	     "/bin/locate -d %s -- %s",
	     ftp_rooted("/etc/locatedb"),
	     pattern);
    f = ftpd_popen(line, "r", 1, 1);
    if(f == NULL){
	perror_reply(550, "/bin/locate");
	return 1;
    }
    lreply(200, "Output from find.");
    while(fgets(line, sizeof(line), f)){
	if(line[strlen(line)-1] == '\n')
	    line[strlen(line)-1] = 0;
	nreply("%s", line);
    }
    reply(200, "Done");
    ftpd_pclose(f);
    return 0;
}

