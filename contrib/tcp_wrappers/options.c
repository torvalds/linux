 /*
  * General skeleton for adding options to the access control language. The
  * features offered by this module are documented in the hosts_options(5)
  * manual page (source file: hosts_options.5, "nroff -man" format).
  * 
  * Notes and warnings for those who want to add features:
  * 
  * In case of errors, abort options processing and deny access. There are too
  * many irreversible side effects to make error recovery feasible. For
  * example, it makes no sense to continue after we have already changed the
  * userid.
  * 
  * In case of errors, do not terminate the process: the routines might be
  * called from a long-running daemon that should run forever. Instead, call
  * tcpd_jump() which does a non-local goto back into the hosts_access()
  * routine.
  * 
  * In case of severe errors, use clean_exit() instead of directly calling
  * exit(), or the inetd may loop on an UDP request.
  * 
  * In verification mode (for example, with the "tcpdmatch" command) the
  * "dry_run" flag is set. In this mode, an option function should just "say"
  * what it is going to do instead of really doing it.
  * 
  * Some option functions do not return (for example, the twist option passes
  * control to another program). In verification mode (dry_run flag is set)
  * such options should clear the "dry_run" flag to inform the caller of this
  * course of action.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) options.c 1.17 96/02/11 17:01:31";
#endif

/* System libraries. */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#define SYSLOG_NAMES
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#ifndef MAXPATHNAMELEN
#define MAXPATHNAMELEN  BUFSIZ
#endif

/* Local stuff. */

#include "tcpd.h"

/* Options runtime support. */

int     dry_run = 0;			/* flag set in verification mode */
extern jmp_buf tcpd_buf;		/* tcpd_jump() support */

/* Options parser support. */

static char whitespace_eq[] = "= \t\r\n";
#define whitespace (whitespace_eq + 1)

static char *get_field();		/* chew :-delimited field off string */
static char *chop_string();		/* strip leading and trailing blanks */

/* List of functions that implement the options. Add yours here. */

static void user_option();		/* execute "user name.group" option */
static void group_option();		/* execute "group name" option */
static void umask_option();		/* execute "umask mask" option */
static void linger_option();		/* execute "linger time" option */
static void keepalive_option();		/* execute "keepalive" option */
static void spawn_option();		/* execute "spawn command" option */
static void twist_option();		/* execute "twist command" option */
static void rfc931_option();		/* execute "rfc931" option */
static void setenv_option();		/* execute "setenv name value" */
static void nice_option();		/* execute "nice" option */
static void severity_option();		/* execute "severity value" */
static void allow_option();		/* execute "allow" option */
static void deny_option();		/* execute "deny" option */
static void banners_option();		/* execute "banners path" option */

/* Structure of the options table. */

struct option {
    char   *name;			/* keyword name, case is ignored */
    void  (*func) ();			/* function that does the real work */
    int     flags;			/* see below... */
};

#define NEED_ARG	(1<<1)		/* option requires argument */
#define USE_LAST	(1<<2)		/* option must be last */
#define OPT_ARG		(1<<3)		/* option has optional argument */
#define EXPAND_ARG	(1<<4)		/* do %x expansion on argument */

#define need_arg(o)	((o)->flags & NEED_ARG)
#define opt_arg(o)	((o)->flags & OPT_ARG)
#define permit_arg(o)	((o)->flags & (NEED_ARG | OPT_ARG))
#define use_last(o)	((o)->flags & USE_LAST)
#define expand_arg(o)	((o)->flags & EXPAND_ARG)

/* List of known keywords. Add yours here. */

static struct option option_table[] = {
    "user", user_option, NEED_ARG,
    "group", group_option, NEED_ARG,
    "umask", umask_option, NEED_ARG,
    "linger", linger_option, NEED_ARG,
    "keepalive", keepalive_option, 0,
    "spawn", spawn_option, NEED_ARG | EXPAND_ARG,
    "twist", twist_option, NEED_ARG | EXPAND_ARG | USE_LAST,
    "rfc931", rfc931_option, OPT_ARG,
    "setenv", setenv_option, NEED_ARG | EXPAND_ARG,
    "nice", nice_option, OPT_ARG,
    "severity", severity_option, NEED_ARG,
    "allow", allow_option, USE_LAST,
    "deny", deny_option, USE_LAST,
    "banners", banners_option, NEED_ARG,
    0,
};

/* process_options - process access control options */

void    process_options(options, request)
char   *options;
struct request_info *request;
{
    char   *key;
    char   *value;
    char   *curr_opt;
    char   *next_opt;
    struct option *op;
    char    bf[BUFSIZ];

    for (curr_opt = get_field(options); curr_opt; curr_opt = next_opt) {
	next_opt = get_field((char *) 0);

	/*
	 * Separate the option into name and value parts. For backwards
	 * compatibility we ignore exactly one '=' between name and value.
	 */
	curr_opt = chop_string(curr_opt);
	if (*(value = curr_opt + strcspn(curr_opt, whitespace_eq))) {
	    if (*value != '=') {
		*value++ = 0;
		value += strspn(value, whitespace);
	    }
	    if (*value == '=') {
		*value++ = 0;
		value += strspn(value, whitespace);
	    }
	}
	if (*value == 0)
	    value = 0;
	key = curr_opt;

	/*
	 * Disallow missing option names (and empty option fields).
	 */
	if (*key == 0)
	    tcpd_jump("missing option name");

	/*
	 * Lookup the option-specific info and do some common error checks.
	 * Delegate option-specific processing to the specific functions.
	 */

	for (op = option_table; op->name && STR_NE(op->name, key); op++)
	     /* VOID */ ;
	if (op->name == 0)
	    tcpd_jump("bad option name: \"%s\"", key);
	if (!value && need_arg(op))
	    tcpd_jump("option \"%s\" requires value", key);
	if (value && !permit_arg(op))
	    tcpd_jump("option \"%s\" requires no value", key);
	if (next_opt && use_last(op))
	    tcpd_jump("option \"%s\" must be at end", key);
	if (value && expand_arg(op))
	    value = chop_string(percent_x(bf, sizeof(bf), value, request));
	if (hosts_access_verbose)
	    syslog(LOG_DEBUG, "option:   %s %s", key, value ? value : "");
	(*(op->func)) (value, request);
    }
}

/* allow_option - grant access */

/* ARGSUSED */

static void allow_option(value, request)
char   *value;
struct request_info *request;
{
    longjmp(tcpd_buf, AC_PERMIT);
}

/* deny_option - deny access */

/* ARGSUSED */

static void deny_option(value, request)
char   *value;
struct request_info *request;
{
    longjmp(tcpd_buf, AC_DENY);
}

/* banners_option - expand %<char>, terminate each line with CRLF */

static void banners_option(value, request)
char   *value;
struct request_info *request;
{
    char    path[MAXPATHNAMELEN];
    char    ibuf[BUFSIZ];
    char    obuf[2 * BUFSIZ];
    struct stat st;
    int     ch;
    FILE   *fp;

    sprintf(path, "%s/%s", value, eval_daemon(request));
    if ((fp = fopen(path, "r")) != 0) {
	while ((ch = fgetc(fp)) == 0)
	    write(request->fd, "", 1);
	ungetc(ch, fp);
	while (fgets(ibuf, sizeof(ibuf) - 1, fp)) {
	    if (split_at(ibuf, '\n'))
		strcat(ibuf, "\r\n");
	    percent_x(obuf, sizeof(obuf), ibuf, request);
	    write(request->fd, obuf, strlen(obuf));
	}
	fclose(fp);
    } else if (stat(value, &st) < 0) {
	tcpd_warn("%s: %m", value);
    }
}

/* group_option - switch group id */

/* ARGSUSED */

static void group_option(value, request)
char   *value;
struct request_info *request;
{
    struct group *grp;
    struct group *getgrnam();

    if ((grp = getgrnam(value)) == 0)
	tcpd_jump("unknown group: \"%s\"", value);
    endgrent();

    if (dry_run == 0 && setgid(grp->gr_gid))
	tcpd_jump("setgid(%s): %m", value);
}

/* user_option - switch user id */

/* ARGSUSED */

static void user_option(value, request)
char   *value;
struct request_info *request;
{
    struct passwd *pwd;
    struct passwd *getpwnam();
    char   *group;

    if ((group = split_at(value, '.')) != 0)
	group_option(group, request);
    if ((pwd = getpwnam(value)) == 0)
	tcpd_jump("unknown user: \"%s\"", value);
    endpwent();

    if (dry_run == 0 && setuid(pwd->pw_uid))
	tcpd_jump("setuid(%s): %m", value);
}

/* umask_option - set file creation mask */

/* ARGSUSED */

static void umask_option(value, request)
char   *value;
struct request_info *request;
{
    unsigned mask;
    char    junk;

    if (sscanf(value, "%o%c", &mask, &junk) != 1 || (mask & 0777) != mask)
	tcpd_jump("bad umask value: \"%s\"", value);
    (void) umask(mask);
}

/* spawn_option - spawn a shell command and wait */

/* ARGSUSED */

static void spawn_option(value, request)
char   *value;
struct request_info *request;
{
    if (dry_run == 0)
	shell_cmd(value);
}

/* linger_option - set the socket linger time (Marc Boucher <marc@cam.org>) */

/* ARGSUSED */

static void linger_option(value, request)
char   *value;
struct request_info *request;
{
    struct linger linger;
    char    junk;

    if (sscanf(value, "%d%c", &linger.l_linger, &junk) != 1
	|| linger.l_linger < 0)
	tcpd_jump("bad linger value: \"%s\"", value);
    if (dry_run == 0) {
	linger.l_onoff = (linger.l_linger != 0);
	if (setsockopt(request->fd, SOL_SOCKET, SO_LINGER, (char *) &linger,
		       sizeof(linger)) < 0)
	    tcpd_warn("setsockopt SO_LINGER %d: %m", linger.l_linger);
    }
}

/* keepalive_option - set the socket keepalive option */

/* ARGSUSED */

static void keepalive_option(value, request)
char   *value;
struct request_info *request;
{
    static int on = 1;

    if (dry_run == 0 && setsockopt(request->fd, SOL_SOCKET, SO_KEEPALIVE,
				   (char *) &on, sizeof(on)) < 0)
	tcpd_warn("setsockopt SO_KEEPALIVE: %m");
}

/* nice_option - set nice value */

/* ARGSUSED */

static void nice_option(value, request)
char   *value;
struct request_info *request;
{
    int     niceval = 10;
    char    junk;

    if (value != 0 && sscanf(value, "%d%c", &niceval, &junk) != 1)
	tcpd_jump("bad nice value: \"%s\"", value);
    if (dry_run == 0 && nice(niceval) < 0)
	tcpd_warn("nice(%d): %m", niceval);
}

/* twist_option - replace process by shell command */

static void twist_option(value, request)
char   *value;
struct request_info *request;
{
    char   *error;

    if (dry_run != 0) {
	dry_run = 0;
    } else {
	if (resident > 0)
	    tcpd_jump("twist option in resident process");

	syslog(deny_severity, "twist %s to %s", eval_client(request), value);

	/* Before switching to the shell, set up stdin, stdout and stderr. */

#define maybe_dup2(from, to) ((from == to) ? to : (close(to), dup(from)))

	if (maybe_dup2(request->fd, 0) != 0 ||
	    maybe_dup2(request->fd, 1) != 1 ||
	    maybe_dup2(request->fd, 2) != 2) {
	    error = "twist_option: dup: %m";
	} else {
	    if (request->fd > 2)
		close(request->fd);
	    (void) execl("/bin/sh", "sh", "-c", value, (char *) 0);
	    error = "twist_option: /bin/sh: %m";
	}

	/* Something went wrong: we MUST terminate the process. */

	tcpd_warn(error);
	clean_exit(request);
    }
}

/* rfc931_option - look up remote user name */

static void rfc931_option(value, request)
char   *value;
struct request_info *request;
{
    int     timeout;
    char    junk;

    if (value != 0) {
	if (sscanf(value, "%d%c", &timeout, &junk) != 1 || timeout <= 0)
	    tcpd_jump("bad rfc931 timeout: \"%s\"", value);
	rfc931_timeout = timeout;
    }
    (void) eval_user(request);
}

/* setenv_option - set environment variable */

/* ARGSUSED */

static void setenv_option(value, request)
char   *value;
struct request_info *request;
{
    char   *var_value;

    if (*(var_value = value + strcspn(value, whitespace)))
	*var_value++ = 0;
    if (setenv(chop_string(value), chop_string(var_value), 1))
	tcpd_jump("memory allocation failure");
}

/* severity_map - lookup facility or severity value */

static int severity_map(table, name)
const CODE   *table;
char   *name;
{
    const CODE *t;
    int ret = -1;

    for (t = table; t->c_name; t++)
	if (STR_EQ(t->c_name, name)) {
	    ret = t->c_val;
	    break;
	}
    if (ret == -1)
    	tcpd_jump("bad syslog facility or severity: \"%s\"", name);

    return (ret);
}

/* severity_option - change logging severity for this event (Dave Mitchell) */

/* ARGSUSED */

static void severity_option(value, request)
char   *value;
struct request_info *request;
{
    char   *level = split_at(value, '.');

    allow_severity = deny_severity = level ?
	severity_map(facilitynames, value) | severity_map(prioritynames, level)
	: severity_map(prioritynames, value);
}

/* get_field - return pointer to next field in string */

static char *get_field(string)
char   *string;
{
    static char *last = "";
    char   *src;
    char   *dst;
    char   *ret;
    int     ch;

    /*
     * This function returns pointers to successive fields within a given
     * string. ":" is the field separator; warn if the rule ends in one. It
     * replaces a "\:" sequence by ":", without treating the result of
     * substitution as field terminator. A null argument means resume search
     * where the previous call terminated. This function destroys its
     * argument.
     * 
     * Work from explicit source or from memory. While processing \: we
     * overwrite the input. This way we do not have to maintain buffers for
     * copies of input fields.
     */

    src = dst = ret = (string ? string : last);
    if (src[0] == 0)
	return (0);

    while (ch = *src) {
	if (ch == ':') {
	    if (*++src == 0)
		tcpd_warn("rule ends in \":\"");
	    break;
	}
	if (ch == '\\' && src[1] == ':')
	    src++;
	*dst++ = *src++;
    }
    last = src;
    *dst = 0;
    return (ret);
}

/* chop_string - strip leading and trailing blanks from string */

static char *chop_string(string)
register char *string;
{
    char   *start = 0;
    char   *end;
    char   *cp;

    for (cp = string; *cp; cp++) {
	if (!isspace(*cp)) {
	    if (start == 0)
		start = cp;
	    end = cp;
	}
    }
    return (start ? (end[1] = 0, start) : cp);
}
