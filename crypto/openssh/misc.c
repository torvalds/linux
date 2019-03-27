/* $OpenBSD: misc.c,v 1.131 2018/07/27 05:13:02 dtucker Exp $ */
/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 * Copyright (c) 2005,2006 Damien Miller.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/un.h>

#include <limits.h>
#ifdef HAVE_LIBGEN_H
# include <libgen.h>
#endif
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#ifdef HAVE_PATHS_H
# include <paths.h>
#include <pwd.h>
#endif
#ifdef SSH_TUN_OPENBSD
#include <net/if.h>
#endif

#include "xmalloc.h"
#include "misc.h"
#include "log.h"
#include "ssh.h"
#include "sshbuf.h"
#include "ssherr.h"
#include "platform.h"

/* remove newline at end of string */
char *
chop(char *s)
{
	char *t = s;
	while (*t) {
		if (*t == '\n' || *t == '\r') {
			*t = '\0';
			return s;
		}
		t++;
	}
	return s;

}

/* set/unset filedescriptor to non-blocking */
int
set_nonblock(int fd)
{
	int val;

	val = fcntl(fd, F_GETFL);
	if (val < 0) {
		error("fcntl(%d, F_GETFL): %s", fd, strerror(errno));
		return (-1);
	}
	if (val & O_NONBLOCK) {
		debug3("fd %d is O_NONBLOCK", fd);
		return (0);
	}
	debug2("fd %d setting O_NONBLOCK", fd);
	val |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, val) == -1) {
		debug("fcntl(%d, F_SETFL, O_NONBLOCK): %s", fd,
		    strerror(errno));
		return (-1);
	}
	return (0);
}

int
unset_nonblock(int fd)
{
	int val;

	val = fcntl(fd, F_GETFL);
	if (val < 0) {
		error("fcntl(%d, F_GETFL): %s", fd, strerror(errno));
		return (-1);
	}
	if (!(val & O_NONBLOCK)) {
		debug3("fd %d is not O_NONBLOCK", fd);
		return (0);
	}
	debug("fd %d clearing O_NONBLOCK", fd);
	val &= ~O_NONBLOCK;
	if (fcntl(fd, F_SETFL, val) == -1) {
		debug("fcntl(%d, F_SETFL, ~O_NONBLOCK): %s",
		    fd, strerror(errno));
		return (-1);
	}
	return (0);
}

const char *
ssh_gai_strerror(int gaierr)
{
	if (gaierr == EAI_SYSTEM && errno != 0)
		return strerror(errno);
	return gai_strerror(gaierr);
}

/* disable nagle on socket */
void
set_nodelay(int fd)
{
	int opt;
	socklen_t optlen;

	optlen = sizeof opt;
	if (getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, &optlen) == -1) {
		debug("getsockopt TCP_NODELAY: %.100s", strerror(errno));
		return;
	}
	if (opt == 1) {
		debug2("fd %d is TCP_NODELAY", fd);
		return;
	}
	opt = 1;
	debug2("fd %d setting TCP_NODELAY", fd);
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt) == -1)
		error("setsockopt TCP_NODELAY: %.100s", strerror(errno));
}

/* Allow local port reuse in TIME_WAIT */
int
set_reuseaddr(int fd)
{
	int on = 1;

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
		error("setsockopt SO_REUSEADDR fd %d: %s", fd, strerror(errno));
		return -1;
	}
	return 0;
}

/* Get/set routing domain */
char *
get_rdomain(int fd)
{
#if defined(HAVE_SYS_GET_RDOMAIN)
	return sys_get_rdomain(fd);
#elif defined(__OpenBSD__)
	int rtable;
	char *ret;
	socklen_t len = sizeof(rtable);

	if (getsockopt(fd, SOL_SOCKET, SO_RTABLE, &rtable, &len) == -1) {
		error("Failed to get routing domain for fd %d: %s",
		    fd, strerror(errno));
		return NULL;
	}
	xasprintf(&ret, "%d", rtable);
	return ret;
#else /* defined(__OpenBSD__) */
	return NULL;
#endif
}

int
set_rdomain(int fd, const char *name)
{
#if defined(HAVE_SYS_SET_RDOMAIN)
	return sys_set_rdomain(fd, name);
#elif defined(__OpenBSD__)
	int rtable;
	const char *errstr;

	if (name == NULL)
		return 0; /* default table */

	rtable = (int)strtonum(name, 0, 255, &errstr);
	if (errstr != NULL) {
		/* Shouldn't happen */
		error("Invalid routing domain \"%s\": %s", name, errstr);
		return -1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_RTABLE,
	    &rtable, sizeof(rtable)) == -1) {
		error("Failed to set routing domain %d on fd %d: %s",
		    rtable, fd, strerror(errno));
		return -1;
	}
	return 0;
#else /* defined(__OpenBSD__) */
	error("Setting routing domain is not supported on this platform");
	return -1;
#endif
}

/* Characters considered whitespace in strsep calls. */
#define WHITESPACE " \t\r\n"
#define QUOTE	"\""

/* return next token in configuration line */
static char *
strdelim_internal(char **s, int split_equals)
{
	char *old;
	int wspace = 0;

	if (*s == NULL)
		return NULL;

	old = *s;

	*s = strpbrk(*s,
	    split_equals ? WHITESPACE QUOTE "=" : WHITESPACE QUOTE);
	if (*s == NULL)
		return (old);

	if (*s[0] == '\"') {
		memmove(*s, *s + 1, strlen(*s)); /* move nul too */
		/* Find matching quote */
		if ((*s = strpbrk(*s, QUOTE)) == NULL) {
			return (NULL);		/* no matching quote */
		} else {
			*s[0] = '\0';
			*s += strspn(*s + 1, WHITESPACE) + 1;
			return (old);
		}
	}

	/* Allow only one '=' to be skipped */
	if (split_equals && *s[0] == '=')
		wspace = 1;
	*s[0] = '\0';

	/* Skip any extra whitespace after first token */
	*s += strspn(*s + 1, WHITESPACE) + 1;
	if (split_equals && *s[0] == '=' && !wspace)
		*s += strspn(*s + 1, WHITESPACE) + 1;

	return (old);
}

/*
 * Return next token in configuration line; splts on whitespace or a
 * single '=' character.
 */
char *
strdelim(char **s)
{
	return strdelim_internal(s, 1);
}

/*
 * Return next token in configuration line; splts on whitespace only.
 */
char *
strdelimw(char **s)
{
	return strdelim_internal(s, 0);
}

struct passwd *
pwcopy(struct passwd *pw)
{
	struct passwd *copy = xcalloc(1, sizeof(*copy));

	copy->pw_name = xstrdup(pw->pw_name);
	copy->pw_passwd = xstrdup(pw->pw_passwd);
#ifdef HAVE_STRUCT_PASSWD_PW_GECOS
	copy->pw_gecos = xstrdup(pw->pw_gecos);
#endif
	copy->pw_uid = pw->pw_uid;
	copy->pw_gid = pw->pw_gid;
#ifdef HAVE_STRUCT_PASSWD_PW_EXPIRE
	copy->pw_expire = pw->pw_expire;
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_CHANGE
	copy->pw_change = pw->pw_change;
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_CLASS
	copy->pw_class = xstrdup(pw->pw_class);
#endif
	copy->pw_dir = xstrdup(pw->pw_dir);
	copy->pw_shell = xstrdup(pw->pw_shell);
	return copy;
}

/*
 * Convert ASCII string to TCP/IP port number.
 * Port must be >=0 and <=65535.
 * Return -1 if invalid.
 */
int
a2port(const char *s)
{
	long long port;
	const char *errstr;

	port = strtonum(s, 0, 65535, &errstr);
	if (errstr != NULL)
		return -1;
	return (int)port;
}

int
a2tun(const char *s, int *remote)
{
	const char *errstr = NULL;
	char *sp, *ep;
	int tun;

	if (remote != NULL) {
		*remote = SSH_TUNID_ANY;
		sp = xstrdup(s);
		if ((ep = strchr(sp, ':')) == NULL) {
			free(sp);
			return (a2tun(s, NULL));
		}
		ep[0] = '\0'; ep++;
		*remote = a2tun(ep, NULL);
		tun = a2tun(sp, NULL);
		free(sp);
		return (*remote == SSH_TUNID_ERR ? *remote : tun);
	}

	if (strcasecmp(s, "any") == 0)
		return (SSH_TUNID_ANY);

	tun = strtonum(s, 0, SSH_TUNID_MAX, &errstr);
	if (errstr != NULL)
		return (SSH_TUNID_ERR);

	return (tun);
}

#define SECONDS		1
#define MINUTES		(SECONDS * 60)
#define HOURS		(MINUTES * 60)
#define DAYS		(HOURS * 24)
#define WEEKS		(DAYS * 7)

/*
 * Convert a time string into seconds; format is
 * a sequence of:
 *      time[qualifier]
 *
 * Valid time qualifiers are:
 *      <none>  seconds
 *      s|S     seconds
 *      m|M     minutes
 *      h|H     hours
 *      d|D     days
 *      w|W     weeks
 *
 * Examples:
 *      90m     90 minutes
 *      1h30m   90 minutes
 *      2d      2 days
 *      1w      1 week
 *
 * Return -1 if time string is invalid.
 */
long
convtime(const char *s)
{
	long total, secs, multiplier = 1;
	const char *p;
	char *endp;

	errno = 0;
	total = 0;
	p = s;

	if (p == NULL || *p == '\0')
		return -1;

	while (*p) {
		secs = strtol(p, &endp, 10);
		if (p == endp ||
		    (errno == ERANGE && (secs == LONG_MIN || secs == LONG_MAX)) ||
		    secs < 0)
			return -1;

		switch (*endp++) {
		case '\0':
			endp--;
			break;
		case 's':
		case 'S':
			break;
		case 'm':
		case 'M':
			multiplier = MINUTES;
			break;
		case 'h':
		case 'H':
			multiplier = HOURS;
			break;
		case 'd':
		case 'D':
			multiplier = DAYS;
			break;
		case 'w':
		case 'W':
			multiplier = WEEKS;
			break;
		default:
			return -1;
		}
		if (secs >= LONG_MAX / multiplier)
			return -1;
		secs *= multiplier;
		if  (total >= LONG_MAX - secs)
			return -1;
		total += secs;
		if (total < 0)
			return -1;
		p = endp;
	}

	return total;
}

/*
 * Returns a standardized host+port identifier string.
 * Caller must free returned string.
 */
char *
put_host_port(const char *host, u_short port)
{
	char *hoststr;

	if (port == 0 || port == SSH_DEFAULT_PORT)
		return(xstrdup(host));
	if (asprintf(&hoststr, "[%s]:%d", host, (int)port) < 0)
		fatal("put_host_port: asprintf: %s", strerror(errno));
	debug3("put_host_port: %s", hoststr);
	return hoststr;
}

/*
 * Search for next delimiter between hostnames/addresses and ports.
 * Argument may be modified (for termination).
 * Returns *cp if parsing succeeds.
 * *cp is set to the start of the next field, if one was found.
 * The delimiter char, if present, is stored in delim.
 * If this is the last field, *cp is set to NULL.
 */
static char *
hpdelim2(char **cp, char *delim)
{
	char *s, *old;

	if (cp == NULL || *cp == NULL)
		return NULL;

	old = s = *cp;
	if (*s == '[') {
		if ((s = strchr(s, ']')) == NULL)
			return NULL;
		else
			s++;
	} else if ((s = strpbrk(s, ":/")) == NULL)
		s = *cp + strlen(*cp); /* skip to end (see first case below) */

	switch (*s) {
	case '\0':
		*cp = NULL;	/* no more fields*/
		break;

	case ':':
	case '/':
		if (delim != NULL)
			*delim = *s;
		*s = '\0';	/* terminate */
		*cp = s + 1;
		break;

	default:
		return NULL;
	}

	return old;
}

char *
hpdelim(char **cp)
{
	return hpdelim2(cp, NULL);
}

char *
cleanhostname(char *host)
{
	if (*host == '[' && host[strlen(host) - 1] == ']') {
		host[strlen(host) - 1] = '\0';
		return (host + 1);
	} else
		return host;
}

char *
colon(char *cp)
{
	int flag = 0;

	if (*cp == ':')		/* Leading colon is part of file name. */
		return NULL;
	if (*cp == '[')
		flag = 1;

	for (; *cp; ++cp) {
		if (*cp == '@' && *(cp+1) == '[')
			flag = 1;
		if (*cp == ']' && *(cp+1) == ':' && flag)
			return (cp+1);
		if (*cp == ':' && !flag)
			return (cp);
		if (*cp == '/')
			return NULL;
	}
	return NULL;
}

/*
 * Parse a [user@]host:[path] string.
 * Caller must free returned user, host and path.
 * Any of the pointer return arguments may be NULL (useful for syntax checking).
 * If user was not specified then *userp will be set to NULL.
 * If host was not specified then *hostp will be set to NULL.
 * If path was not specified then *pathp will be set to ".".
 * Returns 0 on success, -1 on failure.
 */
int
parse_user_host_path(const char *s, char **userp, char **hostp, char **pathp)
{
	char *user = NULL, *host = NULL, *path = NULL;
	char *sdup, *tmp;
	int ret = -1;

	if (userp != NULL)
		*userp = NULL;
	if (hostp != NULL)
		*hostp = NULL;
	if (pathp != NULL)
		*pathp = NULL;

	sdup = xstrdup(s);

	/* Check for remote syntax: [user@]host:[path] */
	if ((tmp = colon(sdup)) == NULL)
		goto out;

	/* Extract optional path */
	*tmp++ = '\0';
	if (*tmp == '\0')
		tmp = ".";
	path = xstrdup(tmp);

	/* Extract optional user and mandatory host */
	tmp = strrchr(sdup, '@');
	if (tmp != NULL) {
		*tmp++ = '\0';
		host = xstrdup(cleanhostname(tmp));
		if (*sdup != '\0')
			user = xstrdup(sdup);
	} else {
		host = xstrdup(cleanhostname(sdup));
		user = NULL;
	}

	/* Success */
	if (userp != NULL) {
		*userp = user;
		user = NULL;
	}
	if (hostp != NULL) {
		*hostp = host;
		host = NULL;
	}
	if (pathp != NULL) {
		*pathp = path;
		path = NULL;
	}
	ret = 0;
out:
	free(sdup);
	free(user);
	free(host);
	free(path);
	return ret;
}

/*
 * Parse a [user@]host[:port] string.
 * Caller must free returned user and host.
 * Any of the pointer return arguments may be NULL (useful for syntax checking).
 * If user was not specified then *userp will be set to NULL.
 * If port was not specified then *portp will be -1.
 * Returns 0 on success, -1 on failure.
 */
int
parse_user_host_port(const char *s, char **userp, char **hostp, int *portp)
{
	char *sdup, *cp, *tmp;
	char *user = NULL, *host = NULL;
	int port = -1, ret = -1;

	if (userp != NULL)
		*userp = NULL;
	if (hostp != NULL)
		*hostp = NULL;
	if (portp != NULL)
		*portp = -1;

	if ((sdup = tmp = strdup(s)) == NULL)
		return -1;
	/* Extract optional username */
	if ((cp = strrchr(tmp, '@')) != NULL) {
		*cp = '\0';
		if (*tmp == '\0')
			goto out;
		if ((user = strdup(tmp)) == NULL)
			goto out;
		tmp = cp + 1;
	}
	/* Extract mandatory hostname */
	if ((cp = hpdelim(&tmp)) == NULL || *cp == '\0')
		goto out;
	host = xstrdup(cleanhostname(cp));
	/* Convert and verify optional port */
	if (tmp != NULL && *tmp != '\0') {
		if ((port = a2port(tmp)) <= 0)
			goto out;
	}
	/* Success */
	if (userp != NULL) {
		*userp = user;
		user = NULL;
	}
	if (hostp != NULL) {
		*hostp = host;
		host = NULL;
	}
	if (portp != NULL)
		*portp = port;
	ret = 0;
 out:
	free(sdup);
	free(user);
	free(host);
	return ret;
}

/*
 * Converts a two-byte hex string to decimal.
 * Returns the decimal value or -1 for invalid input.
 */
static int
hexchar(const char *s)
{
	unsigned char result[2];
	int i;

	for (i = 0; i < 2; i++) {
		if (s[i] >= '0' && s[i] <= '9')
			result[i] = (unsigned char)(s[i] - '0');
		else if (s[i] >= 'a' && s[i] <= 'f')
			result[i] = (unsigned char)(s[i] - 'a') + 10;
		else if (s[i] >= 'A' && s[i] <= 'F')
			result[i] = (unsigned char)(s[i] - 'A') + 10;
		else
			return -1;
	}
	return (result[0] << 4) | result[1];
}

/*
 * Decode an url-encoded string.
 * Returns a newly allocated string on success or NULL on failure.
 */
static char *
urldecode(const char *src)
{
	char *ret, *dst;
	int ch;

	ret = xmalloc(strlen(src) + 1);
	for (dst = ret; *src != '\0'; src++) {
		switch (*src) {
		case '+':
			*dst++ = ' ';
			break;
		case '%':
			if (!isxdigit((unsigned char)src[1]) ||
			    !isxdigit((unsigned char)src[2]) ||
			    (ch = hexchar(src + 1)) == -1) {
				free(ret);
				return NULL;
			}
			*dst++ = ch;
			src += 2;
			break;
		default:
			*dst++ = *src;
			break;
		}
	}
	*dst = '\0';

	return ret;
}

/*
 * Parse an (scp|ssh|sftp)://[user@]host[:port][/path] URI.
 * See https://tools.ietf.org/html/draft-ietf-secsh-scp-sftp-ssh-uri-04
 * Either user or path may be url-encoded (but not host or port).
 * Caller must free returned user, host and path.
 * Any of the pointer return arguments may be NULL (useful for syntax checking)
 * but the scheme must always be specified.
 * If user was not specified then *userp will be set to NULL.
 * If port was not specified then *portp will be -1.
 * If path was not specified then *pathp will be set to NULL.
 * Returns 0 on success, 1 if non-uri/wrong scheme, -1 on error/invalid uri.
 */
int
parse_uri(const char *scheme, const char *uri, char **userp, char **hostp,
    int *portp, char **pathp)
{
	char *uridup, *cp, *tmp, ch;
	char *user = NULL, *host = NULL, *path = NULL;
	int port = -1, ret = -1;
	size_t len;

	len = strlen(scheme);
	if (strncmp(uri, scheme, len) != 0 || strncmp(uri + len, "://", 3) != 0)
		return 1;
	uri += len + 3;

	if (userp != NULL)
		*userp = NULL;
	if (hostp != NULL)
		*hostp = NULL;
	if (portp != NULL)
		*portp = -1;
	if (pathp != NULL)
		*pathp = NULL;

	uridup = tmp = xstrdup(uri);

	/* Extract optional ssh-info (username + connection params) */
	if ((cp = strchr(tmp, '@')) != NULL) {
		char *delim;

		*cp = '\0';
		/* Extract username and connection params */
		if ((delim = strchr(tmp, ';')) != NULL) {
			/* Just ignore connection params for now */
			*delim = '\0';
		}
		if (*tmp == '\0') {
			/* Empty username */
			goto out;
		}
		if ((user = urldecode(tmp)) == NULL)
			goto out;
		tmp = cp + 1;
	}

	/* Extract mandatory hostname */
	if ((cp = hpdelim2(&tmp, &ch)) == NULL || *cp == '\0')
		goto out;
	host = xstrdup(cleanhostname(cp));
	if (!valid_domain(host, 0, NULL))
		goto out;

	if (tmp != NULL && *tmp != '\0') {
		if (ch == ':') {
			/* Convert and verify port. */
			if ((cp = strchr(tmp, '/')) != NULL)
				*cp = '\0';
			if ((port = a2port(tmp)) <= 0)
				goto out;
			tmp = cp ? cp + 1 : NULL;
		}
		if (tmp != NULL && *tmp != '\0') {
			/* Extract optional path */
			if ((path = urldecode(tmp)) == NULL)
				goto out;
		}
	}

	/* Success */
	if (userp != NULL) {
		*userp = user;
		user = NULL;
	}
	if (hostp != NULL) {
		*hostp = host;
		host = NULL;
	}
	if (portp != NULL)
		*portp = port;
	if (pathp != NULL) {
		*pathp = path;
		path = NULL;
	}
	ret = 0;
 out:
	free(uridup);
	free(user);
	free(host);
	free(path);
	return ret;
}

/* function to assist building execv() arguments */
void
addargs(arglist *args, char *fmt, ...)
{
	va_list ap;
	char *cp;
	u_int nalloc;
	int r;

	va_start(ap, fmt);
	r = vasprintf(&cp, fmt, ap);
	va_end(ap);
	if (r == -1)
		fatal("addargs: argument too long");

	nalloc = args->nalloc;
	if (args->list == NULL) {
		nalloc = 32;
		args->num = 0;
	} else if (args->num+2 >= nalloc)
		nalloc *= 2;

	args->list = xrecallocarray(args->list, args->nalloc, nalloc, sizeof(char *));
	args->nalloc = nalloc;
	args->list[args->num++] = cp;
	args->list[args->num] = NULL;
}

void
replacearg(arglist *args, u_int which, char *fmt, ...)
{
	va_list ap;
	char *cp;
	int r;

	va_start(ap, fmt);
	r = vasprintf(&cp, fmt, ap);
	va_end(ap);
	if (r == -1)
		fatal("replacearg: argument too long");

	if (which >= args->num)
		fatal("replacearg: tried to replace invalid arg %d >= %d",
		    which, args->num);
	free(args->list[which]);
	args->list[which] = cp;
}

void
freeargs(arglist *args)
{
	u_int i;

	if (args->list != NULL) {
		for (i = 0; i < args->num; i++)
			free(args->list[i]);
		free(args->list);
		args->nalloc = args->num = 0;
		args->list = NULL;
	}
}

/*
 * Expands tildes in the file name.  Returns data allocated by xmalloc.
 * Warning: this calls getpw*.
 */
char *
tilde_expand_filename(const char *filename, uid_t uid)
{
	const char *path, *sep;
	char user[128], *ret;
	struct passwd *pw;
	u_int len, slash;

	if (*filename != '~')
		return (xstrdup(filename));
	filename++;

	path = strchr(filename, '/');
	if (path != NULL && path > filename) {		/* ~user/path */
		slash = path - filename;
		if (slash > sizeof(user) - 1)
			fatal("tilde_expand_filename: ~username too long");
		memcpy(user, filename, slash);
		user[slash] = '\0';
		if ((pw = getpwnam(user)) == NULL)
			fatal("tilde_expand_filename: No such user %s", user);
	} else if ((pw = getpwuid(uid)) == NULL)	/* ~/path */
		fatal("tilde_expand_filename: No such uid %ld", (long)uid);

	/* Make sure directory has a trailing '/' */
	len = strlen(pw->pw_dir);
	if (len == 0 || pw->pw_dir[len - 1] != '/')
		sep = "/";
	else
		sep = "";

	/* Skip leading '/' from specified path */
	if (path != NULL)
		filename = path + 1;

	if (xasprintf(&ret, "%s%s%s", pw->pw_dir, sep, filename) >= PATH_MAX)
		fatal("tilde_expand_filename: Path too long");

	return (ret);
}

/*
 * Expand a string with a set of %[char] escapes. A number of escapes may be
 * specified as (char *escape_chars, char *replacement) pairs. The list must
 * be terminated by a NULL escape_char. Returns replaced string in memory
 * allocated by xmalloc.
 */
char *
percent_expand(const char *string, ...)
{
#define EXPAND_MAX_KEYS	16
	u_int num_keys, i, j;
	struct {
		const char *key;
		const char *repl;
	} keys[EXPAND_MAX_KEYS];
	char buf[4096];
	va_list ap;

	/* Gather keys */
	va_start(ap, string);
	for (num_keys = 0; num_keys < EXPAND_MAX_KEYS; num_keys++) {
		keys[num_keys].key = va_arg(ap, char *);
		if (keys[num_keys].key == NULL)
			break;
		keys[num_keys].repl = va_arg(ap, char *);
		if (keys[num_keys].repl == NULL)
			fatal("%s: NULL replacement", __func__);
	}
	if (num_keys == EXPAND_MAX_KEYS && va_arg(ap, char *) != NULL)
		fatal("%s: too many keys", __func__);
	va_end(ap);

	/* Expand string */
	*buf = '\0';
	for (i = 0; *string != '\0'; string++) {
		if (*string != '%') {
 append:
			buf[i++] = *string;
			if (i >= sizeof(buf))
				fatal("%s: string too long", __func__);
			buf[i] = '\0';
			continue;
		}
		string++;
		/* %% case */
		if (*string == '%')
			goto append;
		if (*string == '\0')
			fatal("%s: invalid format", __func__);
		for (j = 0; j < num_keys; j++) {
			if (strchr(keys[j].key, *string) != NULL) {
				i = strlcat(buf, keys[j].repl, sizeof(buf));
				if (i >= sizeof(buf))
					fatal("%s: string too long", __func__);
				break;
			}
		}
		if (j >= num_keys)
			fatal("%s: unknown key %%%c", __func__, *string);
	}
	return (xstrdup(buf));
#undef EXPAND_MAX_KEYS
}

int
tun_open(int tun, int mode, char **ifname)
{
#if defined(CUSTOM_SYS_TUN_OPEN)
	return (sys_tun_open(tun, mode, ifname));
#elif defined(SSH_TUN_OPENBSD)
	struct ifreq ifr;
	char name[100];
	int fd = -1, sock;
	const char *tunbase = "tun";

	if (ifname != NULL)
		*ifname = NULL;

	if (mode == SSH_TUNMODE_ETHERNET)
		tunbase = "tap";

	/* Open the tunnel device */
	if (tun <= SSH_TUNID_MAX) {
		snprintf(name, sizeof(name), "/dev/%s%d", tunbase, tun);
		fd = open(name, O_RDWR);
	} else if (tun == SSH_TUNID_ANY) {
		for (tun = 100; tun >= 0; tun--) {
			snprintf(name, sizeof(name), "/dev/%s%d",
			    tunbase, tun);
			if ((fd = open(name, O_RDWR)) >= 0)
				break;
		}
	} else {
		debug("%s: invalid tunnel %u", __func__, tun);
		return -1;
	}

	if (fd < 0) {
		debug("%s: %s open: %s", __func__, name, strerror(errno));
		return -1;
	}

	debug("%s: %s mode %d fd %d", __func__, name, mode, fd);

	/* Bring interface up if it is not already */
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s%d", tunbase, tun);
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1)
		goto failed;

	if (ioctl(sock, SIOCGIFFLAGS, &ifr) == -1) {
		debug("%s: get interface %s flags: %s", __func__,
		    ifr.ifr_name, strerror(errno));
		goto failed;
	}

	if (!(ifr.ifr_flags & IFF_UP)) {
		ifr.ifr_flags |= IFF_UP;
		if (ioctl(sock, SIOCSIFFLAGS, &ifr) == -1) {
			debug("%s: activate interface %s: %s", __func__,
			    ifr.ifr_name, strerror(errno));
			goto failed;
		}
	}

	if (ifname != NULL)
		*ifname = xstrdup(ifr.ifr_name);

	close(sock);
	return fd;

 failed:
	if (fd >= 0)
		close(fd);
	if (sock >= 0)
		close(sock);
	return -1;
#else
	error("Tunnel interfaces are not supported on this platform");
	return (-1);
#endif
}

void
sanitise_stdfd(void)
{
	int nullfd, dupfd;

	if ((nullfd = dupfd = open(_PATH_DEVNULL, O_RDWR)) == -1) {
		fprintf(stderr, "Couldn't open /dev/null: %s\n",
		    strerror(errno));
		exit(1);
	}
	while (++dupfd <= STDERR_FILENO) {
		/* Only populate closed fds. */
		if (fcntl(dupfd, F_GETFL) == -1 && errno == EBADF) {
			if (dup2(nullfd, dupfd) == -1) {
				fprintf(stderr, "dup2: %s\n", strerror(errno));
				exit(1);
			}
		}
	}
	if (nullfd > STDERR_FILENO)
		close(nullfd);
}

char *
tohex(const void *vp, size_t l)
{
	const u_char *p = (const u_char *)vp;
	char b[3], *r;
	size_t i, hl;

	if (l > 65536)
		return xstrdup("tohex: length > 65536");

	hl = l * 2 + 1;
	r = xcalloc(1, hl);
	for (i = 0; i < l; i++) {
		snprintf(b, sizeof(b), "%02x", p[i]);
		strlcat(r, b, hl);
	}
	return (r);
}

u_int64_t
get_u64(const void *vp)
{
	const u_char *p = (const u_char *)vp;
	u_int64_t v;

	v  = (u_int64_t)p[0] << 56;
	v |= (u_int64_t)p[1] << 48;
	v |= (u_int64_t)p[2] << 40;
	v |= (u_int64_t)p[3] << 32;
	v |= (u_int64_t)p[4] << 24;
	v |= (u_int64_t)p[5] << 16;
	v |= (u_int64_t)p[6] << 8;
	v |= (u_int64_t)p[7];

	return (v);
}

u_int32_t
get_u32(const void *vp)
{
	const u_char *p = (const u_char *)vp;
	u_int32_t v;

	v  = (u_int32_t)p[0] << 24;
	v |= (u_int32_t)p[1] << 16;
	v |= (u_int32_t)p[2] << 8;
	v |= (u_int32_t)p[3];

	return (v);
}

u_int32_t
get_u32_le(const void *vp)
{
	const u_char *p = (const u_char *)vp;
	u_int32_t v;

	v  = (u_int32_t)p[0];
	v |= (u_int32_t)p[1] << 8;
	v |= (u_int32_t)p[2] << 16;
	v |= (u_int32_t)p[3] << 24;

	return (v);
}

u_int16_t
get_u16(const void *vp)
{
	const u_char *p = (const u_char *)vp;
	u_int16_t v;

	v  = (u_int16_t)p[0] << 8;
	v |= (u_int16_t)p[1];

	return (v);
}

void
put_u64(void *vp, u_int64_t v)
{
	u_char *p = (u_char *)vp;

	p[0] = (u_char)(v >> 56) & 0xff;
	p[1] = (u_char)(v >> 48) & 0xff;
	p[2] = (u_char)(v >> 40) & 0xff;
	p[3] = (u_char)(v >> 32) & 0xff;
	p[4] = (u_char)(v >> 24) & 0xff;
	p[5] = (u_char)(v >> 16) & 0xff;
	p[6] = (u_char)(v >> 8) & 0xff;
	p[7] = (u_char)v & 0xff;
}

void
put_u32(void *vp, u_int32_t v)
{
	u_char *p = (u_char *)vp;

	p[0] = (u_char)(v >> 24) & 0xff;
	p[1] = (u_char)(v >> 16) & 0xff;
	p[2] = (u_char)(v >> 8) & 0xff;
	p[3] = (u_char)v & 0xff;
}

void
put_u32_le(void *vp, u_int32_t v)
{
	u_char *p = (u_char *)vp;

	p[0] = (u_char)v & 0xff;
	p[1] = (u_char)(v >> 8) & 0xff;
	p[2] = (u_char)(v >> 16) & 0xff;
	p[3] = (u_char)(v >> 24) & 0xff;
}

void
put_u16(void *vp, u_int16_t v)
{
	u_char *p = (u_char *)vp;

	p[0] = (u_char)(v >> 8) & 0xff;
	p[1] = (u_char)v & 0xff;
}

void
ms_subtract_diff(struct timeval *start, int *ms)
{
	struct timeval diff, finish;

	monotime_tv(&finish);
	timersub(&finish, start, &diff);
	*ms -= (diff.tv_sec * 1000) + (diff.tv_usec / 1000);
}

void
ms_to_timeval(struct timeval *tv, int ms)
{
	if (ms < 0)
		ms = 0;
	tv->tv_sec = ms / 1000;
	tv->tv_usec = (ms % 1000) * 1000;
}

void
monotime_ts(struct timespec *ts)
{
	struct timeval tv;
#if defined(HAVE_CLOCK_GETTIME) && (defined(CLOCK_BOOTTIME) || \
    defined(CLOCK_MONOTONIC) || defined(CLOCK_REALTIME))
	static int gettime_failed = 0;

	if (!gettime_failed) {
# ifdef CLOCK_BOOTTIME
		if (clock_gettime(CLOCK_BOOTTIME, ts) == 0)
			return;
# endif /* CLOCK_BOOTTIME */
# ifdef CLOCK_MONOTONIC
		if (clock_gettime(CLOCK_MONOTONIC, ts) == 0)
			return;
# endif /* CLOCK_MONOTONIC */
# ifdef CLOCK_REALTIME
		/* Not monotonic, but we're almost out of options here. */
		if (clock_gettime(CLOCK_REALTIME, ts) == 0)
			return;
# endif /* CLOCK_REALTIME */
		debug3("clock_gettime: %s", strerror(errno));
		gettime_failed = 1;
	}
#endif /* HAVE_CLOCK_GETTIME && (BOOTTIME || MONOTONIC || REALTIME) */
	gettimeofday(&tv, NULL);
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = (long)tv.tv_usec * 1000;
}

void
monotime_tv(struct timeval *tv)
{
	struct timespec ts;

	monotime_ts(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
}

time_t
monotime(void)
{
	struct timespec ts;

	monotime_ts(&ts);
	return ts.tv_sec;
}

double
monotime_double(void)
{
	struct timespec ts;

	monotime_ts(&ts);
	return ts.tv_sec + ((double)ts.tv_nsec / 1000000000);
}

void
bandwidth_limit_init(struct bwlimit *bw, u_int64_t kbps, size_t buflen)
{
	bw->buflen = buflen;
	bw->rate = kbps;
	bw->thresh = bw->rate;
	bw->lamt = 0;
	timerclear(&bw->bwstart);
	timerclear(&bw->bwend);
}	

/* Callback from read/write loop to insert bandwidth-limiting delays */
void
bandwidth_limit(struct bwlimit *bw, size_t read_len)
{
	u_int64_t waitlen;
	struct timespec ts, rm;

	if (!timerisset(&bw->bwstart)) {
		monotime_tv(&bw->bwstart);
		return;
	}

	bw->lamt += read_len;
	if (bw->lamt < bw->thresh)
		return;

	monotime_tv(&bw->bwend);
	timersub(&bw->bwend, &bw->bwstart, &bw->bwend);
	if (!timerisset(&bw->bwend))
		return;

	bw->lamt *= 8;
	waitlen = (double)1000000L * bw->lamt / bw->rate;

	bw->bwstart.tv_sec = waitlen / 1000000L;
	bw->bwstart.tv_usec = waitlen % 1000000L;

	if (timercmp(&bw->bwstart, &bw->bwend, >)) {
		timersub(&bw->bwstart, &bw->bwend, &bw->bwend);

		/* Adjust the wait time */
		if (bw->bwend.tv_sec) {
			bw->thresh /= 2;
			if (bw->thresh < bw->buflen / 4)
				bw->thresh = bw->buflen / 4;
		} else if (bw->bwend.tv_usec < 10000) {
			bw->thresh *= 2;
			if (bw->thresh > bw->buflen * 8)
				bw->thresh = bw->buflen * 8;
		}

		TIMEVAL_TO_TIMESPEC(&bw->bwend, &ts);
		while (nanosleep(&ts, &rm) == -1) {
			if (errno != EINTR)
				break;
			ts = rm;
		}
	}

	bw->lamt = 0;
	monotime_tv(&bw->bwstart);
}

/* Make a template filename for mk[sd]temp() */
void
mktemp_proto(char *s, size_t len)
{
	const char *tmpdir;
	int r;

	if ((tmpdir = getenv("TMPDIR")) != NULL) {
		r = snprintf(s, len, "%s/ssh-XXXXXXXXXXXX", tmpdir);
		if (r > 0 && (size_t)r < len)
			return;
	}
	r = snprintf(s, len, "/tmp/ssh-XXXXXXXXXXXX");
	if (r < 0 || (size_t)r >= len)
		fatal("%s: template string too short", __func__);
}

static const struct {
	const char *name;
	int value;
} ipqos[] = {
	{ "none", INT_MAX },		/* can't use 0 here; that's CS0 */
	{ "af11", IPTOS_DSCP_AF11 },
	{ "af12", IPTOS_DSCP_AF12 },
	{ "af13", IPTOS_DSCP_AF13 },
	{ "af21", IPTOS_DSCP_AF21 },
	{ "af22", IPTOS_DSCP_AF22 },
	{ "af23", IPTOS_DSCP_AF23 },
	{ "af31", IPTOS_DSCP_AF31 },
	{ "af32", IPTOS_DSCP_AF32 },
	{ "af33", IPTOS_DSCP_AF33 },
	{ "af41", IPTOS_DSCP_AF41 },
	{ "af42", IPTOS_DSCP_AF42 },
	{ "af43", IPTOS_DSCP_AF43 },
	{ "cs0", IPTOS_DSCP_CS0 },
	{ "cs1", IPTOS_DSCP_CS1 },
	{ "cs2", IPTOS_DSCP_CS2 },
	{ "cs3", IPTOS_DSCP_CS3 },
	{ "cs4", IPTOS_DSCP_CS4 },
	{ "cs5", IPTOS_DSCP_CS5 },
	{ "cs6", IPTOS_DSCP_CS6 },
	{ "cs7", IPTOS_DSCP_CS7 },
	{ "ef", IPTOS_DSCP_EF },
	{ "lowdelay", IPTOS_LOWDELAY },
	{ "throughput", IPTOS_THROUGHPUT },
	{ "reliability", IPTOS_RELIABILITY },
	{ NULL, -1 }
};

int
parse_ipqos(const char *cp)
{
	u_int i;
	char *ep;
	long val;

	if (cp == NULL)
		return -1;
	for (i = 0; ipqos[i].name != NULL; i++) {
		if (strcasecmp(cp, ipqos[i].name) == 0)
			return ipqos[i].value;
	}
	/* Try parsing as an integer */
	val = strtol(cp, &ep, 0);
	if (*cp == '\0' || *ep != '\0' || val < 0 || val > 255)
		return -1;
	return val;
}

const char *
iptos2str(int iptos)
{
	int i;
	static char iptos_str[sizeof "0xff"];

	for (i = 0; ipqos[i].name != NULL; i++) {
		if (ipqos[i].value == iptos)
			return ipqos[i].name;
	}
	snprintf(iptos_str, sizeof iptos_str, "0x%02x", iptos);
	return iptos_str;
}

void
lowercase(char *s)
{
	for (; *s; s++)
		*s = tolower((u_char)*s);
}

int
unix_listener(const char *path, int backlog, int unlink_first)
{
	struct sockaddr_un sunaddr;
	int saved_errno, sock;

	memset(&sunaddr, 0, sizeof(sunaddr));
	sunaddr.sun_family = AF_UNIX;
	if (strlcpy(sunaddr.sun_path, path,
	    sizeof(sunaddr.sun_path)) >= sizeof(sunaddr.sun_path)) {
		error("%s: path \"%s\" too long for Unix domain socket",
		    __func__, path);
		errno = ENAMETOOLONG;
		return -1;
	}

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		saved_errno = errno;
		error("%s: socket: %.100s", __func__, strerror(errno));
		errno = saved_errno;
		return -1;
	}
	if (unlink_first == 1) {
		if (unlink(path) != 0 && errno != ENOENT)
			error("unlink(%s): %.100s", path, strerror(errno));
	}
	if (bind(sock, (struct sockaddr *)&sunaddr, sizeof(sunaddr)) < 0) {
		saved_errno = errno;
		error("%s: cannot bind to path %s: %s",
		    __func__, path, strerror(errno));
		close(sock);
		errno = saved_errno;
		return -1;
	}
	if (listen(sock, backlog) < 0) {
		saved_errno = errno;
		error("%s: cannot listen on path %s: %s",
		    __func__, path, strerror(errno));
		close(sock);
		unlink(path);
		errno = saved_errno;
		return -1;
	}
	return sock;
}

void
sock_set_v6only(int s)
{
#if defined(IPV6_V6ONLY) && !defined(__OpenBSD__)
	int on = 1;

	debug3("%s: set socket %d IPV6_V6ONLY", __func__, s);
	if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) == -1)
		error("setsockopt IPV6_V6ONLY: %s", strerror(errno));
#endif
}

/*
 * Compares two strings that maybe be NULL. Returns non-zero if strings
 * are both NULL or are identical, returns zero otherwise.
 */
static int
strcmp_maybe_null(const char *a, const char *b)
{
	if ((a == NULL && b != NULL) || (a != NULL && b == NULL))
		return 0;
	if (a != NULL && strcmp(a, b) != 0)
		return 0;
	return 1;
}

/*
 * Compare two forwards, returning non-zero if they are identical or
 * zero otherwise.
 */
int
forward_equals(const struct Forward *a, const struct Forward *b)
{
	if (strcmp_maybe_null(a->listen_host, b->listen_host) == 0)
		return 0;
	if (a->listen_port != b->listen_port)
		return 0;
	if (strcmp_maybe_null(a->listen_path, b->listen_path) == 0)
		return 0;
	if (strcmp_maybe_null(a->connect_host, b->connect_host) == 0)
		return 0;
	if (a->connect_port != b->connect_port)
		return 0;
	if (strcmp_maybe_null(a->connect_path, b->connect_path) == 0)
		return 0;
	/* allocated_port and handle are not checked */
	return 1;
}

/* returns 1 if process is already daemonized, 0 otherwise */
int
daemonized(void)
{
	int fd;

	if ((fd = open(_PATH_TTY, O_RDONLY | O_NOCTTY)) >= 0) {
		close(fd);
		return 0;	/* have controlling terminal */
	}
	if (getppid() != 1)
		return 0;	/* parent is not init */
	if (getsid(0) != getpid())
		return 0;	/* not session leader */
	debug3("already daemonized");
	return 1;
}


/*
 * Splits 's' into an argument vector. Handles quoted string and basic
 * escape characters (\\, \", \'). Caller must free the argument vector
 * and its members.
 */
int
argv_split(const char *s, int *argcp, char ***argvp)
{
	int r = SSH_ERR_INTERNAL_ERROR;
	int argc = 0, quote, i, j;
	char *arg, **argv = xcalloc(1, sizeof(*argv));

	*argvp = NULL;
	*argcp = 0;

	for (i = 0; s[i] != '\0'; i++) {
		/* Skip leading whitespace */
		if (s[i] == ' ' || s[i] == '\t')
			continue;

		/* Start of a token */
		quote = 0;
		if (s[i] == '\\' &&
		    (s[i + 1] == '\'' || s[i + 1] == '\"' || s[i + 1] == '\\'))
			i++;
		else if (s[i] == '\'' || s[i] == '"')
			quote = s[i++];

		argv = xreallocarray(argv, (argc + 2), sizeof(*argv));
		arg = argv[argc++] = xcalloc(1, strlen(s + i) + 1);
		argv[argc] = NULL;

		/* Copy the token in, removing escapes */
		for (j = 0; s[i] != '\0'; i++) {
			if (s[i] == '\\') {
				if (s[i + 1] == '\'' ||
				    s[i + 1] == '\"' ||
				    s[i + 1] == '\\') {
					i++; /* Skip '\' */
					arg[j++] = s[i];
				} else {
					/* Unrecognised escape */
					arg[j++] = s[i];
				}
			} else if (quote == 0 && (s[i] == ' ' || s[i] == '\t'))
				break; /* done */
			else if (quote != 0 && s[i] == quote)
				break; /* done */
			else
				arg[j++] = s[i];
		}
		if (s[i] == '\0') {
			if (quote != 0) {
				/* Ran out of string looking for close quote */
				r = SSH_ERR_INVALID_FORMAT;
				goto out;
			}
			break;
		}
	}
	/* Success */
	*argcp = argc;
	*argvp = argv;
	argc = 0;
	argv = NULL;
	r = 0;
 out:
	if (argc != 0 && argv != NULL) {
		for (i = 0; i < argc; i++)
			free(argv[i]);
		free(argv);
	}
	return r;
}

/*
 * Reassemble an argument vector into a string, quoting and escaping as
 * necessary. Caller must free returned string.
 */
char *
argv_assemble(int argc, char **argv)
{
	int i, j, ws, r;
	char c, *ret;
	struct sshbuf *buf, *arg;

	if ((buf = sshbuf_new()) == NULL || (arg = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);

	for (i = 0; i < argc; i++) {
		ws = 0;
		sshbuf_reset(arg);
		for (j = 0; argv[i][j] != '\0'; j++) {
			r = 0;
			c = argv[i][j];
			switch (c) {
			case ' ':
			case '\t':
				ws = 1;
				r = sshbuf_put_u8(arg, c);
				break;
			case '\\':
			case '\'':
			case '"':
				if ((r = sshbuf_put_u8(arg, '\\')) != 0)
					break;
				/* FALLTHROUGH */
			default:
				r = sshbuf_put_u8(arg, c);
				break;
			}
			if (r != 0)
				fatal("%s: sshbuf_put_u8: %s",
				    __func__, ssh_err(r));
		}
		if ((i != 0 && (r = sshbuf_put_u8(buf, ' ')) != 0) ||
		    (ws != 0 && (r = sshbuf_put_u8(buf, '"')) != 0) ||
		    (r = sshbuf_putb(buf, arg)) != 0 ||
		    (ws != 0 && (r = sshbuf_put_u8(buf, '"')) != 0))
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}
	if ((ret = malloc(sshbuf_len(buf) + 1)) == NULL)
		fatal("%s: malloc failed", __func__);
	memcpy(ret, sshbuf_ptr(buf), sshbuf_len(buf));
	ret[sshbuf_len(buf)] = '\0';
	sshbuf_free(buf);
	sshbuf_free(arg);
	return ret;
}

/* Returns 0 if pid exited cleanly, non-zero otherwise */
int
exited_cleanly(pid_t pid, const char *tag, const char *cmd, int quiet)
{
	int status;

	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR) {
			error("%s: waitpid: %s", tag, strerror(errno));
			return -1;
		}
	}
	if (WIFSIGNALED(status)) {
		error("%s %s exited on signal %d", tag, cmd, WTERMSIG(status));
		return -1;
	} else if (WEXITSTATUS(status) != 0) {
		do_log2(quiet ? SYSLOG_LEVEL_DEBUG1 : SYSLOG_LEVEL_INFO,
		    "%s %s failed, status %d", tag, cmd, WEXITSTATUS(status));
		return -1;
	}
	return 0;
}

/*
 * Check a given path for security. This is defined as all components
 * of the path to the file must be owned by either the owner of
 * of the file or root and no directories must be group or world writable.
 *
 * XXX Should any specific check be done for sym links ?
 *
 * Takes a file name, its stat information (preferably from fstat() to
 * avoid races), the uid of the expected owner, their home directory and an
 * error buffer plus max size as arguments.
 *
 * Returns 0 on success and -1 on failure
 */
int
safe_path(const char *name, struct stat *stp, const char *pw_dir,
    uid_t uid, char *err, size_t errlen)
{
	char buf[PATH_MAX], homedir[PATH_MAX];
	char *cp;
	int comparehome = 0;
	struct stat st;

	if (realpath(name, buf) == NULL) {
		snprintf(err, errlen, "realpath %s failed: %s", name,
		    strerror(errno));
		return -1;
	}
	if (pw_dir != NULL && realpath(pw_dir, homedir) != NULL)
		comparehome = 1;

	if (!S_ISREG(stp->st_mode)) {
		snprintf(err, errlen, "%s is not a regular file", buf);
		return -1;
	}
	if ((!platform_sys_dir_uid(stp->st_uid) && stp->st_uid != uid) ||
	    (stp->st_mode & 022) != 0) {
		snprintf(err, errlen, "bad ownership or modes for file %s",
		    buf);
		return -1;
	}

	/* for each component of the canonical path, walking upwards */
	for (;;) {
		if ((cp = dirname(buf)) == NULL) {
			snprintf(err, errlen, "dirname() failed");
			return -1;
		}
		strlcpy(buf, cp, sizeof(buf));

		if (stat(buf, &st) < 0 ||
		    (!platform_sys_dir_uid(st.st_uid) && st.st_uid != uid) ||
		    (st.st_mode & 022) != 0) {
			snprintf(err, errlen,
			    "bad ownership or modes for directory %s", buf);
			return -1;
		}

		/* If are past the homedir then we can stop */
		if (comparehome && strcmp(homedir, buf) == 0)
			break;

		/*
		 * dirname should always complete with a "/" path,
		 * but we can be paranoid and check for "." too
		 */
		if ((strcmp("/", buf) == 0) || (strcmp(".", buf) == 0))
			break;
	}
	return 0;
}

/*
 * Version of safe_path() that accepts an open file descriptor to
 * avoid races.
 *
 * Returns 0 on success and -1 on failure
 */
int
safe_path_fd(int fd, const char *file, struct passwd *pw,
    char *err, size_t errlen)
{
	struct stat st;

	/* check the open file to avoid races */
	if (fstat(fd, &st) < 0) {
		snprintf(err, errlen, "cannot stat file %s: %s",
		    file, strerror(errno));
		return -1;
	}
	return safe_path(file, &st, pw->pw_dir, pw->pw_uid, err, errlen);
}

/*
 * Sets the value of the given variable in the environment.  If the variable
 * already exists, its value is overridden.
 */
void
child_set_env(char ***envp, u_int *envsizep, const char *name,
	const char *value)
{
	char **env;
	u_int envsize;
	u_int i, namelen;

	if (strchr(name, '=') != NULL) {
		error("Invalid environment variable \"%.100s\"", name);
		return;
	}

	/*
	 * If we're passed an uninitialized list, allocate a single null
	 * entry before continuing.
	 */
	if (*envp == NULL && *envsizep == 0) {
		*envp = xmalloc(sizeof(char *));
		*envp[0] = NULL;
		*envsizep = 1;
	}

	/*
	 * Find the slot where the value should be stored.  If the variable
	 * already exists, we reuse the slot; otherwise we append a new slot
	 * at the end of the array, expanding if necessary.
	 */
	env = *envp;
	namelen = strlen(name);
	for (i = 0; env[i]; i++)
		if (strncmp(env[i], name, namelen) == 0 && env[i][namelen] == '=')
			break;
	if (env[i]) {
		/* Reuse the slot. */
		free(env[i]);
	} else {
		/* New variable.  Expand if necessary. */
		envsize = *envsizep;
		if (i >= envsize - 1) {
			if (envsize >= 1000)
				fatal("child_set_env: too many env vars");
			envsize += 50;
			env = (*envp) = xreallocarray(env, envsize, sizeof(char *));
			*envsizep = envsize;
		}
		/* Need to set the NULL pointer at end of array beyond the new slot. */
		env[i + 1] = NULL;
	}

	/* Allocate space and format the variable in the appropriate slot. */
	/* XXX xasprintf */
	env[i] = xmalloc(strlen(name) + 1 + strlen(value) + 1);
	snprintf(env[i], strlen(name) + 1 + strlen(value) + 1, "%s=%s", name, value);
}

/*
 * Check and optionally lowercase a domain name, also removes trailing '.'
 * Returns 1 on success and 0 on failure, storing an error message in errstr.
 */
int
valid_domain(char *name, int makelower, const char **errstr)
{
	size_t i, l = strlen(name);
	u_char c, last = '\0';
	static char errbuf[256];

	if (l == 0) {
		strlcpy(errbuf, "empty domain name", sizeof(errbuf));
		goto bad;
	}
	if (!isalpha((u_char)name[0]) && !isdigit((u_char)name[0])) {
		snprintf(errbuf, sizeof(errbuf), "domain name \"%.100s\" "
		    "starts with invalid character", name);
		goto bad;
	}
	for (i = 0; i < l; i++) {
		c = tolower((u_char)name[i]);
		if (makelower)
			name[i] = (char)c;
		if (last == '.' && c == '.') {
			snprintf(errbuf, sizeof(errbuf), "domain name "
			    "\"%.100s\" contains consecutive separators", name);
			goto bad;
		}
		if (c != '.' && c != '-' && !isalnum(c) &&
		    c != '_') /* technically invalid, but common */ {
			snprintf(errbuf, sizeof(errbuf), "domain name "
			    "\"%.100s\" contains invalid characters", name);
			goto bad;
		}
		last = c;
	}
	if (name[l - 1] == '.')
		name[l - 1] = '\0';
	if (errstr != NULL)
		*errstr = NULL;
	return 1;
bad:
	if (errstr != NULL)
		*errstr = errbuf;
	return 0;
}

const char *
atoi_err(const char *nptr, int *val)
{
	const char *errstr = NULL;
	long long num;

	if (nptr == NULL || *nptr == '\0')
		return "missing";
	num = strtonum(nptr, 0, INT_MAX, &errstr);
	if (errstr == NULL)
		*val = (int)num;
	return errstr;
}

int
parse_absolute_time(const char *s, uint64_t *tp)
{
	struct tm tm;
	time_t tt;
	char buf[32], *fmt;

	*tp = 0;

	/*
	 * POSIX strptime says "The application shall ensure that there
	 * is white-space or other non-alphanumeric characters between
	 * any two conversion specifications" so arrange things this way.
	 */
	switch (strlen(s)) {
	case 8: /* YYYYMMDD */
		fmt = "%Y-%m-%d";
		snprintf(buf, sizeof(buf), "%.4s-%.2s-%.2s", s, s + 4, s + 6);
		break;
	case 12: /* YYYYMMDDHHMM */
		fmt = "%Y-%m-%dT%H:%M";
		snprintf(buf, sizeof(buf), "%.4s-%.2s-%.2sT%.2s:%.2s",
		    s, s + 4, s + 6, s + 8, s + 10);
		break;
	case 14: /* YYYYMMDDHHMMSS */
		fmt = "%Y-%m-%dT%H:%M:%S";
		snprintf(buf, sizeof(buf), "%.4s-%.2s-%.2sT%.2s:%.2s:%.2s",
		    s, s + 4, s + 6, s + 8, s + 10, s + 12);
		break;
	default:
		return SSH_ERR_INVALID_FORMAT;
	}

	memset(&tm, 0, sizeof(tm));
	if (strptime(buf, fmt, &tm) == NULL)
		return SSH_ERR_INVALID_FORMAT;
	if ((tt = mktime(&tm)) < 0)
		return SSH_ERR_INVALID_FORMAT;
	/* success */
	*tp = (uint64_t)tt;
	return 0;
}

void
format_absolute_time(uint64_t t, char *buf, size_t len)
{
	time_t tt = t > INT_MAX ? INT_MAX : t; /* XXX revisit in 2038 :P */
	struct tm tm;

	localtime_r(&tt, &tm);
	strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm);
}
