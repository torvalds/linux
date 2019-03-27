/*	$NetBSD: fetch.c,v 1.18 2009/11/15 10:12:37 lukem Exp $	*/
/*	from	NetBSD: fetch.c,v 1.191 2009/08/17 09:08:16 christos Exp	*/

/*-
 * Copyright (c) 1997-2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Scott Aaron Bamford.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "tnftp.h"

#if 0	/* tnftp */

#include <sys/cdefs.h>
#ifndef lint
__RCSID(" NetBSD: fetch.c,v 1.191 2009/08/17 09:08:16 christos Exp  ");
#endif /* not lint */

/*
 * FTP User Program -- Command line file retrieval
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <arpa/ftp.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#endif	/* tnftp */

#include "ftp_var.h"
#include "version.h"

typedef enum {
	UNKNOWN_URL_T=-1,
	HTTP_URL_T,
	FTP_URL_T,
	FILE_URL_T,
	CLASSIC_URL_T
} url_t;

void		aborthttp(int);
#ifndef NO_AUTH
static int	auth_url(const char *, char **, const char *, const char *);
static void	base64_encode(const unsigned char *, size_t, unsigned char *);
#endif
static int	go_fetch(const char *);
static int	fetch_ftp(const char *);
static int	fetch_url(const char *, const char *, char *, char *);
static const char *match_token(const char **, const char *);
static int	parse_url(const char *, const char *, url_t *, char **,
			    char **, char **, char **, in_port_t *, char **);
static void	url_decode(char *);

static int	redirect_loop;


#define	STRNEQUAL(a,b)	(strncasecmp((a), (b), sizeof((b))-1) == 0)
#define	ISLWS(x)	((x)=='\r' || (x)=='\n' || (x)==' ' || (x)=='\t')
#define	SKIPLWS(x)	do { while (ISLWS((*x))) x++; } while (0)


#define	ABOUT_URL	"about:"	/* propaganda */
#define	FILE_URL	"file://"	/* file URL prefix */
#define	FTP_URL		"ftp://"	/* ftp URL prefix */
#define	HTTP_URL	"http://"	/* http URL prefix */


/*
 * Determine if token is the next word in buf (case insensitive).
 * If so, advance buf past the token and any trailing LWS, and
 * return a pointer to the token (in buf).  Otherwise, return NULL.
 * token may be preceded by LWS.
 * token must be followed by LWS or NUL.  (I.e, don't partial match).
 */
static const char *
match_token(const char **buf, const char *token)
{
	const char	*p, *orig;
	size_t		tlen;

	tlen = strlen(token);
	p = *buf;
	SKIPLWS(p);
	orig = p;
	if (strncasecmp(p, token, tlen) != 0)
		return NULL;
	p += tlen;
	if (*p != '\0' && !ISLWS(*p))
		return NULL;
	SKIPLWS(p);
	orig = *buf;
	*buf = p;
	return orig;
}

#ifndef NO_AUTH
/*
 * Generate authorization response based on given authentication challenge.
 * Returns -1 if an error occurred, otherwise 0.
 * Sets response to a malloc(3)ed string; caller should free.
 */
static int
auth_url(const char *challenge, char **response, const char *guser,
	const char *gpass)
{
	const char	*cp, *scheme, *errormsg;
	char		*ep, *clear, *realm;
	char		 uuser[BUFSIZ], *gotpass;
	const char	*upass;
	int		 rval;
	size_t		 len, clen, rlen;

	*response = NULL;
	clear = realm = NULL;
	rval = -1;
	cp = challenge;
	scheme = "Basic";	/* only support Basic authentication */
	gotpass = NULL;

	DPRINTF("auth_url: challenge `%s'\n", challenge);

	if (! match_token(&cp, scheme)) {
		warnx("Unsupported authentication challenge `%s'",
		    challenge);
		goto cleanup_auth_url;
	}

#define	REALM "realm=\""
	if (STRNEQUAL(cp, REALM))
		cp += sizeof(REALM) - 1;
	else {
		warnx("Unsupported authentication challenge `%s'",
		    challenge);
		goto cleanup_auth_url;
	}
/* XXX: need to improve quoted-string parsing to support \ quoting, etc. */
	if ((ep = strchr(cp, '\"')) != NULL) {
		len = ep - cp;
		realm = (char *)ftp_malloc(len + 1);
		(void)strlcpy(realm, cp, len + 1);
	} else {
		warnx("Unsupported authentication challenge `%s'",
		    challenge);
		goto cleanup_auth_url;
	}

	fprintf(ttyout, "Username for `%s': ", realm);
	if (guser != NULL) {
		(void)strlcpy(uuser, guser, sizeof(uuser));
		fprintf(ttyout, "%s\n", uuser);
	} else {
		(void)fflush(ttyout);
		if (get_line(stdin, uuser, sizeof(uuser), &errormsg) < 0) {
			warnx("%s; can't authenticate", errormsg);
			goto cleanup_auth_url;
		}
	}
	if (gpass != NULL)
		upass = gpass;
	else {
		gotpass = getpass("Password: ");
		if (gotpass == NULL) {
			warnx("Can't read password");
			goto cleanup_auth_url;
		}
		upass = gotpass;
	}

	clen = strlen(uuser) + strlen(upass) + 2;	/* user + ":" + pass + "\0" */
	clear = (char *)ftp_malloc(clen);
	(void)strlcpy(clear, uuser, clen);
	(void)strlcat(clear, ":", clen);
	(void)strlcat(clear, upass, clen);
	if (gotpass)
		memset(gotpass, 0, strlen(gotpass));

						/* scheme + " " + enc + "\0" */
	rlen = strlen(scheme) + 1 + (clen + 2) * 4 / 3 + 1;
	*response = (char *)ftp_malloc(rlen);
	(void)strlcpy(*response, scheme, rlen);
	len = strlcat(*response, " ", rlen);
			/* use  `clen - 1'  to not encode the trailing NUL */
	base64_encode((unsigned char *)clear, clen - 1,
	    (unsigned char *)*response + len);
	memset(clear, 0, clen);
	rval = 0;

 cleanup_auth_url:
	FREEPTR(clear);
	FREEPTR(realm);
	return (rval);
}

/*
 * Encode len bytes starting at clear using base64 encoding into encoded,
 * which should be at least ((len + 2) * 4 / 3 + 1) in size.
 */
static void
base64_encode(const unsigned char *clear, size_t len, unsigned char *encoded)
{
	static const unsigned char enc[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned char	*cp;
	size_t	 i;

	cp = encoded;
	for (i = 0; i < len; i += 3) {
		*(cp++) = enc[((clear[i + 0] >> 2))];
		*(cp++) = enc[((clear[i + 0] << 4) & 0x30)
			    | ((clear[i + 1] >> 4) & 0x0f)];
		*(cp++) = enc[((clear[i + 1] << 2) & 0x3c)
			    | ((clear[i + 2] >> 6) & 0x03)];
		*(cp++) = enc[((clear[i + 2]     ) & 0x3f)];
	}
	*cp = '\0';
	while (i-- > len)
		*(--cp) = '=';
}
#endif

/*
 * Decode %xx escapes in given string, `in-place'.
 */
static void
url_decode(char *url)
{
	unsigned char *p, *q;

	if (EMPTYSTRING(url))
		return;
	p = q = (unsigned char *)url;

#define	HEXTOINT(x) (x - (isdigit(x) ? '0' : (islower(x) ? 'a' : 'A') - 10))
	while (*p) {
		if (p[0] == '%'
		    && p[1] && isxdigit((unsigned char)p[1])
		    && p[2] && isxdigit((unsigned char)p[2])) {
			*q++ = HEXTOINT(p[1]) * 16 + HEXTOINT(p[2]);
			p+=3;
		} else
			*q++ = *p++;
	}
	*q = '\0';
}


/*
 * Parse URL of form (per RFC3986):
 *	<type>://[<user>[:<password>]@]<host>[:<port>][/<path>]
 * Returns -1 if a parse error occurred, otherwise 0.
 * It's the caller's responsibility to url_decode() the returned
 * user, pass and path.
 *
 * Sets type to url_t, each of the given char ** pointers to a
 * malloc(3)ed strings of the relevant section, and port to
 * the number given, or ftpport if ftp://, or httpport if http://.
 *
 * XXX: this is not totally RFC3986 compliant; <path> will have the
 * leading `/' unless it's an ftp:// URL, as this makes things easier
 * for file:// and http:// URLs.  ftp:// URLs have the `/' between the
 * host and the URL-path removed, but any additional leading slashes
 * in the URL-path are retained (because they imply that we should
 * later do "CWD" with a null argument).
 *
 * Examples:
 *	 input URL			 output path
 *	 ---------			 -----------
 *	"http://host"			"/"
 *	"http://host/"			"/"
 *	"http://host/path"		"/path"
 *	"file://host/dir/file"		"dir/file"
 *	"ftp://host"			""
 *	"ftp://host/"			""
 *	"ftp://host//"			"/"
 *	"ftp://host/dir/file"		"dir/file"
 *	"ftp://host//dir/file"		"/dir/file"
 */
static int
parse_url(const char *url, const char *desc, url_t *utype,
		char **uuser, char **pass, char **host, char **port,
		in_port_t *portnum, char **path)
{
	const char	*origurl, *tport;
	char		*cp, *ep, *thost;
	size_t		 len;

	if (url == NULL || desc == NULL || utype == NULL || uuser == NULL
	    || pass == NULL || host == NULL || port == NULL || portnum == NULL
	    || path == NULL)
		errx(1, "parse_url: invoked with NULL argument!");
	DPRINTF("parse_url: %s `%s'\n", desc, url);

	origurl = url;
	*utype = UNKNOWN_URL_T;
	*uuser = *pass = *host = *port = *path = NULL;
	*portnum = 0;
	tport = NULL;

	if (STRNEQUAL(url, HTTP_URL)) {
		url += sizeof(HTTP_URL) - 1;
		*utype = HTTP_URL_T;
		*portnum = HTTP_PORT;
		tport = httpport;
	} else if (STRNEQUAL(url, FTP_URL)) {
		url += sizeof(FTP_URL) - 1;
		*utype = FTP_URL_T;
		*portnum = FTP_PORT;
		tport = ftpport;
	} else if (STRNEQUAL(url, FILE_URL)) {
		url += sizeof(FILE_URL) - 1;
		*utype = FILE_URL_T;
	} else {
		warnx("Invalid %s `%s'", desc, url);
 cleanup_parse_url:
		FREEPTR(*uuser);
		if (*pass != NULL)
			memset(*pass, 0, strlen(*pass));
		FREEPTR(*pass);
		FREEPTR(*host);
		FREEPTR(*port);
		FREEPTR(*path);
		return (-1);
	}

	if (*url == '\0')
		return (0);

			/* find [user[:pass]@]host[:port] */
	ep = strchr(url, '/');
	if (ep == NULL)
		thost = ftp_strdup(url);
	else {
		len = ep - url;
		thost = (char *)ftp_malloc(len + 1);
		(void)strlcpy(thost, url, len + 1);
		if (*utype == FTP_URL_T)	/* skip first / for ftp URLs */
			ep++;
		*path = ftp_strdup(ep);
	}

	cp = strchr(thost, '@');	/* look for user[:pass]@ in URLs */
	if (cp != NULL) {
		if (*utype == FTP_URL_T)
			anonftp = 0;	/* disable anonftp */
		*uuser = thost;
		*cp = '\0';
		thost = ftp_strdup(cp + 1);
		cp = strchr(*uuser, ':');
		if (cp != NULL) {
			*cp = '\0';
			*pass = ftp_strdup(cp + 1);
		}
		url_decode(*uuser);
		if (*pass)
			url_decode(*pass);
	}

#ifdef INET6
			/*
			 * Check if thost is an encoded IPv6 address, as per
			 * RFC3986:
			 *	`[' ipv6-address ']'
			 */
	if (*thost == '[') {
		cp = thost + 1;
		if ((ep = strchr(cp, ']')) == NULL ||
		    (ep[1] != '\0' && ep[1] != ':')) {
			warnx("Invalid address `%s' in %s `%s'",
			    thost, desc, origurl);
			goto cleanup_parse_url;
		}
		len = ep - cp;		/* change `[xyz]' -> `xyz' */
		memmove(thost, thost + 1, len);
		thost[len] = '\0';
		if (! isipv6addr(thost)) {
			warnx("Invalid IPv6 address `%s' in %s `%s'",
			    thost, desc, origurl);
			goto cleanup_parse_url;
		}
		cp = ep + 1;
		if (*cp == ':')
			cp++;
		else
			cp = NULL;
	} else
#endif /* INET6 */
		if ((cp = strchr(thost, ':')) != NULL)
			*cp++ = '\0';
	*host = thost;

			/* look for [:port] */
	if (cp != NULL) {
		unsigned long	nport;

		nport = strtoul(cp, &ep, 10);
		if (*cp == '\0' || *ep != '\0' ||
		    nport < 1 || nport > MAX_IN_PORT_T) {
			warnx("Unknown port `%s' in %s `%s'",
			    cp, desc, origurl);
			goto cleanup_parse_url;
		}
		*portnum = nport;
		tport = cp;
	}

	if (tport != NULL)
		*port = ftp_strdup(tport);
	if (*path == NULL) {
		const char *emptypath = "/";
		if (*utype == FTP_URL_T)	/* skip first / for ftp URLs */
			emptypath++;
		*path = ftp_strdup(emptypath);
	}

	DPRINTF("parse_url: user `%s' pass `%s' host %s port %s(%d) "
	    "path `%s'\n",
	    STRorNULL(*uuser), STRorNULL(*pass),
	    STRorNULL(*host), STRorNULL(*port),
	    *portnum ? *portnum : -1, STRorNULL(*path));

	return (0);
}

sigjmp_buf	httpabort;

/*
 * Retrieve URL, via a proxy if necessary, using HTTP.
 * If proxyenv is set, use that for the proxy, otherwise try ftp_proxy or
 * http_proxy as appropriate.
 * Supports HTTP redirects.
 * Returns 1 on failure, 0 on completed xfer, -1 if ftp connection
 * is still open (e.g, ftp xfer with trailing /)
 */
static int
fetch_url(const char *url, const char *proxyenv, char *proxyauth, char *wwwauth)
{
	struct addrinfo		hints, *res, *res0 = NULL;
	int			error;
	sigfunc volatile	oldintr;
	sigfunc volatile	oldintp;
	int volatile		s;
	struct stat		sb;
	int volatile		ischunked;
	int volatile		isproxy;
	int volatile		rval;
	int volatile		hcode;
	int			len;
	size_t			flen;
	static size_t		bufsize;
	static char		*xferbuf;
	const char		*cp, *token;
	char			*ep;
	char			buf[FTPBUFLEN];
	const char		*errormsg;
	char			*volatile savefile;
	char			*volatile auth;
	char			*volatile location;
	char			*volatile message;
	char			*uuser, *pass, *host, *port, *path;
	char			*volatile decodedpath;
	char			*puser, *ppass, *useragent;
	off_t			hashbytes, rangestart, rangeend, entitylen;
	int			(*volatile closefunc)(FILE *);
	FILE			*volatile fin;
	FILE			*volatile fout;
	time_t			mtime;
	url_t			urltype;
	in_port_t		portnum;

	DPRINTF("fetch_url: `%s' proxyenv `%s'\n", url, STRorNULL(proxyenv));

	oldintr = oldintp = NULL;
	closefunc = NULL;
	fin = fout = NULL;
	s = -1;
	savefile = NULL;
	auth = location = message = NULL;
	ischunked = isproxy = hcode = 0;
	rval = 1;
	uuser = pass = host = path = decodedpath = puser = ppass = NULL;

	if (parse_url(url, "URL", &urltype, &uuser, &pass, &host, &port,
	    &portnum, &path) == -1)
		goto cleanup_fetch_url;

	if (urltype == FILE_URL_T && ! EMPTYSTRING(host)
	    && strcasecmp(host, "localhost") != 0) {
		warnx("No support for non local file URL `%s'", url);
		goto cleanup_fetch_url;
	}

	if (EMPTYSTRING(path)) {
		if (urltype == FTP_URL_T) {
			rval = fetch_ftp(url);
			goto cleanup_fetch_url;
		}
		if (urltype != HTTP_URL_T || outfile == NULL)  {
			warnx("Invalid URL (no file after host) `%s'", url);
			goto cleanup_fetch_url;
		}
	}

	decodedpath = ftp_strdup(path);
	url_decode(decodedpath);

	if (outfile)
		savefile = outfile;
	else {
		cp = strrchr(decodedpath, '/');		/* find savefile */
		if (cp != NULL)
			savefile = ftp_strdup(cp + 1);
		else
			savefile = ftp_strdup(decodedpath);
	}
	DPRINTF("fetch_url: savefile `%s'\n", savefile);
	if (EMPTYSTRING(savefile)) {
		if (urltype == FTP_URL_T) {
			rval = fetch_ftp(url);
			goto cleanup_fetch_url;
		}
		warnx("No file after directory (you must specify an "
		    "output file) `%s'", url);
		goto cleanup_fetch_url;
	}

	restart_point = 0;
	filesize = -1;
	rangestart = rangeend = entitylen = -1;
	mtime = -1;
	if (restartautofetch) {
		if (stat(savefile, &sb) == 0)
			restart_point = sb.st_size;
	}
	if (urltype == FILE_URL_T) {		/* file:// URLs */
		direction = "copied";
		fin = fopen(decodedpath, "r");
		if (fin == NULL) {
			warn("Can't open `%s'", decodedpath);
			goto cleanup_fetch_url;
		}
		if (fstat(fileno(fin), &sb) == 0) {
			mtime = sb.st_mtime;
			filesize = sb.st_size;
		}
		if (restart_point) {
			if (lseek(fileno(fin), restart_point, SEEK_SET) < 0) {
				warn("Can't seek to restart `%s'",
				    decodedpath);
				goto cleanup_fetch_url;
			}
		}
		if (verbose) {
			fprintf(ttyout, "Copying %s", decodedpath);
			if (restart_point)
				fprintf(ttyout, " (restarting at " LLF ")",
				    (LLT)restart_point);
			fputs("\n", ttyout);
		}
	} else {				/* ftp:// or http:// URLs */
		const char *leading;
		int hasleading;

		if (proxyenv == NULL) {
			if (urltype == HTTP_URL_T)
				proxyenv = getoptionvalue("http_proxy");
			else if (urltype == FTP_URL_T)
				proxyenv = getoptionvalue("ftp_proxy");
		}
		direction = "retrieved";
		if (! EMPTYSTRING(proxyenv)) {			/* use proxy */
			url_t purltype;
			char *phost, *ppath;
			char *pport, *no_proxy;
			in_port_t pportnum;

			isproxy = 1;

				/* check URL against list of no_proxied sites */
			no_proxy = getoptionvalue("no_proxy");
			if (! EMPTYSTRING(no_proxy)) {
				char *np, *np_copy, *np_iter;
				unsigned long np_port;
				size_t hlen, plen;

				np_iter = np_copy = ftp_strdup(no_proxy);
				hlen = strlen(host);
				while ((cp = strsep(&np_iter, " ,")) != NULL) {
					if (*cp == '\0')
						continue;
					if ((np = strrchr(cp, ':')) != NULL) {
						*np++ =  '\0';
						np_port = strtoul(np, &ep, 10);
						if (*np == '\0' || *ep != '\0')
							continue;
						if (np_port != portnum)
							continue;
					}
					plen = strlen(cp);
					if (hlen < plen)
						continue;
					if (strncasecmp(host + hlen - plen,
					    cp, plen) == 0) {
						isproxy = 0;
						break;
					}
				}
				FREEPTR(np_copy);
				if (isproxy == 0 && urltype == FTP_URL_T) {
					rval = fetch_ftp(url);
					goto cleanup_fetch_url;
				}
			}

			if (isproxy) {
				if (restart_point) {
					warnx("Can't restart via proxy URL `%s'",
					    proxyenv);
					goto cleanup_fetch_url;
				}
				if (parse_url(proxyenv, "proxy URL", &purltype,
				    &puser, &ppass, &phost, &pport, &pportnum,
				    &ppath) == -1)
					goto cleanup_fetch_url;

				if ((purltype != HTTP_URL_T
				     && purltype != FTP_URL_T) ||
				    EMPTYSTRING(phost) ||
				    (! EMPTYSTRING(ppath)
				     && strcmp(ppath, "/") != 0)) {
					warnx("Malformed proxy URL `%s'",
					    proxyenv);
					FREEPTR(phost);
					FREEPTR(pport);
					FREEPTR(ppath);
					goto cleanup_fetch_url;
				}
				if (isipv6addr(host) &&
				    strchr(host, '%') != NULL) {
					warnx(
"Scoped address notation `%s' disallowed via web proxy",
					    host);
					FREEPTR(phost);
					FREEPTR(pport);
					FREEPTR(ppath);
					goto cleanup_fetch_url;
				}

				FREEPTR(host);
				host = phost;
				FREEPTR(port);
				port = pport;
				FREEPTR(path);
				path = ftp_strdup(url);
				FREEPTR(ppath);
			}
		} /* ! EMPTYSTRING(proxyenv) */

		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = 0;
		hints.ai_family = family;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = 0;
		error = getaddrinfo(host, port, &hints, &res0);
		if (error) {
			warnx("Can't lookup `%s:%s': %s", host, port,
			    (error == EAI_SYSTEM) ? strerror(errno)
						  : gai_strerror(error));
			goto cleanup_fetch_url;
		}
		if (res0->ai_canonname)
			host = res0->ai_canonname;

		s = -1;
		for (res = res0; res; res = res->ai_next) {
			char	hname[NI_MAXHOST], sname[NI_MAXSERV];

			ai_unmapped(res);
			if (getnameinfo(res->ai_addr, res->ai_addrlen,
			    hname, sizeof(hname), sname, sizeof(sname),
			    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
				strlcpy(hname, "?", sizeof(hname));
				strlcpy(sname, "?", sizeof(sname));
			}

			if (verbose && res0->ai_next) {
				fprintf(ttyout, "Trying %s:%s ...\n",
				    hname, sname);
			}

			s = socket(res->ai_family, SOCK_STREAM,
			    res->ai_protocol);
			if (s < 0) {
				warn(
				    "Can't create socket for connection to "
				    "`%s:%s'", hname, sname);
				continue;
			}

			if (ftp_connect(s, res->ai_addr, res->ai_addrlen) < 0) {
				close(s);
				s = -1;
				continue;
			}

			/* success */
			break;
		}

		if (s < 0) {
			warnx("Can't connect to `%s:%s'", host, port);
			goto cleanup_fetch_url;
		}

		fin = fdopen(s, "r+");
		/*
		 * Construct and send the request.
		 */
		if (verbose)
			fprintf(ttyout, "Requesting %s\n", url);
		leading = "  (";
		hasleading = 0;
		if (isproxy) {
			if (verbose) {
				fprintf(ttyout, "%svia %s:%s", leading,
				    host, port);
				leading = ", ";
				hasleading++;
			}
			fprintf(fin, "GET %s HTTP/1.0\r\n", path);
			if (flushcache)
				fprintf(fin, "Pragma: no-cache\r\n");
		} else {
			fprintf(fin, "GET %s HTTP/1.1\r\n", path);
			if (strchr(host, ':')) {
				char *h, *p;

				/*
				 * strip off IPv6 scope identifier, since it is
				 * local to the node
				 */
				h = ftp_strdup(host);
				if (isipv6addr(h) &&
				    (p = strchr(h, '%')) != NULL) {
					*p = '\0';
				}
				fprintf(fin, "Host: [%s]", h);
				free(h);
			} else
				fprintf(fin, "Host: %s", host);
			if (portnum != HTTP_PORT)
				fprintf(fin, ":%u", portnum);
			fprintf(fin, "\r\n");
			fprintf(fin, "Accept: */*\r\n");
			fprintf(fin, "Connection: close\r\n");
			if (restart_point) {
				fputs(leading, ttyout);
				fprintf(fin, "Range: bytes=" LLF "-\r\n",
				    (LLT)restart_point);
				fprintf(ttyout, "restarting at " LLF,
				    (LLT)restart_point);
				leading = ", ";
				hasleading++;
			}
			if (flushcache)
				fprintf(fin, "Cache-Control: no-cache\r\n");
		}
		if ((useragent=getenv("FTPUSERAGENT")) != NULL) {
			fprintf(fin, "User-Agent: %s\r\n", useragent);
		} else {
			fprintf(fin, "User-Agent: %s/%s\r\n",
			    FTP_PRODUCT, FTP_VERSION);
		}
		if (wwwauth) {
			if (verbose) {
				fprintf(ttyout, "%swith authorization",
				    leading);
				leading = ", ";
				hasleading++;
			}
			fprintf(fin, "Authorization: %s\r\n", wwwauth);
		}
		if (proxyauth) {
			if (verbose) {
				fprintf(ttyout,
				    "%swith proxy authorization", leading);
				leading = ", ";
				hasleading++;
			}
			fprintf(fin, "Proxy-Authorization: %s\r\n", proxyauth);
		}
		if (verbose && hasleading)
			fputs(")\n", ttyout);
		fprintf(fin, "\r\n");
		if (fflush(fin) == EOF) {
			warn("Writing HTTP request");
			goto cleanup_fetch_url;
		}

				/* Read the response */
		len = get_line(fin, buf, sizeof(buf), &errormsg);
		if (len < 0) {
			if (*errormsg == '\n')
				errormsg++;
			warnx("Receiving HTTP reply: %s", errormsg);
			goto cleanup_fetch_url;
		}
		while (len > 0 && (ISLWS(buf[len-1])))
			buf[--len] = '\0';
		DPRINTF("fetch_url: received `%s'\n", buf);

				/* Determine HTTP response code */
		cp = strchr(buf, ' ');
		if (cp == NULL)
			goto improper;
		else
			cp++;
		hcode = strtol(cp, &ep, 10);
		if (*ep != '\0' && !isspace((unsigned char)*ep))
			goto improper;
		message = ftp_strdup(cp);

				/* Read the rest of the header. */
		while (1) {
			len = get_line(fin, buf, sizeof(buf), &errormsg);
			if (len < 0) {
				if (*errormsg == '\n')
					errormsg++;
				warnx("Receiving HTTP reply: %s", errormsg);
				goto cleanup_fetch_url;
			}
			while (len > 0 && (ISLWS(buf[len-1])))
				buf[--len] = '\0';
			if (len == 0)
				break;
			DPRINTF("fetch_url: received `%s'\n", buf);

		/*
		 * Look for some headers
		 */

			cp = buf;

			if (match_token(&cp, "Content-Length:")) {
				filesize = STRTOLL(cp, &ep, 10);
				if (filesize < 0 || *ep != '\0')
					goto improper;
				DPRINTF("fetch_url: parsed len as: " LLF "\n",
				    (LLT)filesize);

			} else if (match_token(&cp, "Content-Range:")) {
				if (! match_token(&cp, "bytes"))
					goto improper;

				if (*cp == '*')
					cp++;
				else {
					rangestart = STRTOLL(cp, &ep, 10);
					if (rangestart < 0 || *ep != '-')
						goto improper;
					cp = ep + 1;
					rangeend = STRTOLL(cp, &ep, 10);
					if (rangeend < 0 || rangeend < rangestart)
						goto improper;
					cp = ep;
				}
				if (*cp != '/')
					goto improper;
				cp++;
				if (*cp == '*')
					cp++;
				else {
					entitylen = STRTOLL(cp, &ep, 10);
					if (entitylen < 0)
						goto improper;
					cp = ep;
				}
				if (*cp != '\0')
					goto improper;

#ifndef NO_DEBUG
				if (ftp_debug) {
					fprintf(ttyout, "parsed range as: ");
					if (rangestart == -1)
						fprintf(ttyout, "*");
					else
						fprintf(ttyout, LLF "-" LLF,
						    (LLT)rangestart,
						    (LLT)rangeend);
					fprintf(ttyout, "/" LLF "\n", (LLT)entitylen);
				}
#endif
				if (! restart_point) {
					warnx(
				    "Received unexpected Content-Range header");
					goto cleanup_fetch_url;
				}

			} else if (match_token(&cp, "Last-Modified:")) {
				struct tm parsed;
				char *t;

				memset(&parsed, 0, sizeof(parsed));
							/* RFC1123 */
				if ((t = strptime(cp,
						"%a, %d %b %Y %H:%M:%S GMT",
						&parsed))
							/* RFC0850 */
				    || (t = strptime(cp,
						"%a, %d-%b-%y %H:%M:%S GMT",
						&parsed))
							/* asctime */
				    || (t = strptime(cp,
						"%a, %b %d %H:%M:%S %Y",
						&parsed))) {
					parsed.tm_isdst = -1;
					if (*t == '\0')
						mtime = timegm(&parsed);
#ifndef NO_DEBUG
					if (ftp_debug && mtime != -1) {
						fprintf(ttyout,
						    "parsed date as: %s",
						rfc2822time(localtime(&mtime)));
					}
#endif
				}

			} else if (match_token(&cp, "Location:")) {
				location = ftp_strdup(cp);
				DPRINTF("fetch_url: parsed location as `%s'\n",
				    cp);

			} else if (match_token(&cp, "Transfer-Encoding:")) {
				if (match_token(&cp, "binary")) {
					warnx(
			"Bogus transfer encoding `binary' (fetching anyway)");
					continue;
				}
				if (! (token = match_token(&cp, "chunked"))) {
					warnx(
				    "Unsupported transfer encoding `%s'",
					    token);
					goto cleanup_fetch_url;
				}
				ischunked++;
				DPRINTF("fetch_url: using chunked encoding\n");

			} else if (match_token(&cp, "Proxy-Authenticate:")
				|| match_token(&cp, "WWW-Authenticate:")) {
				if (! (token = match_token(&cp, "Basic"))) {
					DPRINTF(
			"fetch_url: skipping unknown auth scheme `%s'\n",
						    token);
					continue;
				}
				FREEPTR(auth);
				auth = ftp_strdup(token);
				DPRINTF("fetch_url: parsed auth as `%s'\n", cp);
			}

		}
				/* finished parsing header */

		switch (hcode) {
		case 200:
			break;
		case 206:
			if (! restart_point) {
				warnx("Not expecting partial content header");
				goto cleanup_fetch_url;
			}
			break;
		case 300:
		case 301:
		case 302:
		case 303:
		case 305:
		case 307:
			if (EMPTYSTRING(location)) {
				warnx(
				"No redirection Location provided by server");
				goto cleanup_fetch_url;
			}
			if (redirect_loop++ > 5) {
				warnx("Too many redirections requested");
				goto cleanup_fetch_url;
			}
			if (hcode == 305) {
				if (verbose)
					fprintf(ttyout, "Redirected via %s\n",
					    location);
				rval = fetch_url(url, location,
				    proxyauth, wwwauth);
			} else {
				if (verbose)
					fprintf(ttyout, "Redirected to %s\n",
					    location);
				rval = go_fetch(location);
			}
			goto cleanup_fetch_url;
#ifndef NO_AUTH
		case 401:
		case 407:
		    {
			char **authp;
			char *auser, *apass;

			if (hcode == 401) {
				authp = &wwwauth;
				auser = uuser;
				apass = pass;
			} else {
				authp = &proxyauth;
				auser = puser;
				apass = ppass;
			}
			if (verbose || *authp == NULL ||
			    auser == NULL || apass == NULL)
				fprintf(ttyout, "%s\n", message);
			if (EMPTYSTRING(auth)) {
				warnx(
			    "No authentication challenge provided by server");
				goto cleanup_fetch_url;
			}
			if (*authp != NULL) {
				char reply[10];

				fprintf(ttyout,
				    "Authorization failed. Retry (y/n)? ");
				if (get_line(stdin, reply, sizeof(reply), NULL)
				    < 0) {
					goto cleanup_fetch_url;
				}
				if (tolower((unsigned char)reply[0]) != 'y')
					goto cleanup_fetch_url;
				auser = NULL;
				apass = NULL;
			}
			if (auth_url(auth, authp, auser, apass) == 0) {
				rval = fetch_url(url, proxyenv,
				    proxyauth, wwwauth);
				memset(*authp, 0, strlen(*authp));
				FREEPTR(*authp);
			}
			goto cleanup_fetch_url;
		    }
#endif
		default:
			if (message)
				warnx("Error retrieving file `%s'", message);
			else
				warnx("Unknown error retrieving file");
			goto cleanup_fetch_url;
		}
	}		/* end of ftp:// or http:// specific setup */

			/* Open the output file. */

	/*
	 * Only trust filenames with special meaning if they came from
	 * the command line
	 */
	if (outfile == savefile) {
		if (strcmp(savefile, "-") == 0) {
			fout = stdout;
		} else if (*savefile == '|') {
			oldintp = xsignal(SIGPIPE, SIG_IGN);
			fout = popen(savefile + 1, "w");
			if (fout == NULL) {
				warn("Can't execute `%s'", savefile + 1);
				goto cleanup_fetch_url;
			}
			closefunc = pclose;
		}
	}
	if (fout == NULL) {
		if ((rangeend != -1 && rangeend <= restart_point) ||
		    (rangestart == -1 && filesize != -1 && filesize <= restart_point)) {
			/* already done */
			if (verbose)
				fprintf(ttyout, "already done\n");
			rval = 0;
			goto cleanup_fetch_url;
		}
		if (restart_point && rangestart != -1) {
			if (entitylen != -1)
				filesize = entitylen;
			if (rangestart != restart_point) {
				warnx(
				    "Size of `%s' differs from save file `%s'",
				    url, savefile);
				goto cleanup_fetch_url;
			}
			fout = fopen(savefile, "a");
		} else
			fout = fopen(savefile, "w");
		if (fout == NULL) {
			warn("Can't open `%s'", savefile);
			goto cleanup_fetch_url;
		}
		closefunc = fclose;
	}

			/* Trap signals */
	if (sigsetjmp(httpabort, 1))
		goto cleanup_fetch_url;
	(void)xsignal(SIGQUIT, psummary);
	oldintr = xsignal(SIGINT, aborthttp);

	if ((size_t)rcvbuf_size > bufsize) {
		if (xferbuf)
			(void)free(xferbuf);
		bufsize = rcvbuf_size;
		xferbuf = ftp_malloc(bufsize);
	}

	bytes = 0;
	hashbytes = mark;
	progressmeter(-1);

			/* Finally, suck down the file. */
	do {
		long chunksize;
		short lastchunk;

		chunksize = 0;
		lastchunk = 0;
					/* read chunk-size */
		if (ischunked) {
			if (fgets(xferbuf, bufsize, fin) == NULL) {
				warnx("Unexpected EOF reading chunk-size");
				goto cleanup_fetch_url;
			}
			errno = 0;
			chunksize = strtol(xferbuf, &ep, 16);
			if (ep == xferbuf) {
				warnx("Invalid chunk-size");
				goto cleanup_fetch_url;
			}
			if (errno == ERANGE || chunksize < 0) {
				errno = ERANGE;
				warn("Chunk-size `%.*s'",
				    (int)(ep-xferbuf), xferbuf);
				goto cleanup_fetch_url;
			}

				/*
				 * XXX:	Work around bug in Apache 1.3.9 and
				 *	1.3.11, which incorrectly put trailing
				 *	space after the chunk-size.
				 */
			while (*ep == ' ')
				ep++;

					/* skip [ chunk-ext ] */
			if (*ep == ';') {
				while (*ep && *ep != '\r')
					ep++;
			}

			if (strcmp(ep, "\r\n") != 0) {
				warnx("Unexpected data following chunk-size");
				goto cleanup_fetch_url;
			}
			DPRINTF("fetch_url: got chunk-size of " LLF "\n",
			    (LLT)chunksize);
			if (chunksize == 0) {
				lastchunk = 1;
				goto chunkdone;
			}
		}
					/* transfer file or chunk */
		while (1) {
			struct timeval then, now, td;
			off_t bufrem;

			if (rate_get)
				(void)gettimeofday(&then, NULL);
			bufrem = rate_get ? rate_get : (off_t)bufsize;
			if (ischunked)
				bufrem = MIN(chunksize, bufrem);
			while (bufrem > 0) {
				flen = fread(xferbuf, sizeof(char),
				    MIN((off_t)bufsize, bufrem), fin);
				if (flen <= 0)
					goto chunkdone;
				bytes += flen;
				bufrem -= flen;
				if (fwrite(xferbuf, sizeof(char), flen, fout)
				    != flen) {
					warn("Writing `%s'", savefile);
					goto cleanup_fetch_url;
				}
				if (hash && !progress) {
					while (bytes >= hashbytes) {
						(void)putc('#', ttyout);
						hashbytes += mark;
					}
					(void)fflush(ttyout);
				}
				if (ischunked) {
					chunksize -= flen;
					if (chunksize <= 0)
						break;
				}
			}
			if (rate_get) {
				while (1) {
					(void)gettimeofday(&now, NULL);
					timersub(&now, &then, &td);
					if (td.tv_sec > 0)
						break;
					usleep(1000000 - td.tv_usec);
				}
			}
			if (ischunked && chunksize <= 0)
				break;
		}
					/* read CRLF after chunk*/
 chunkdone:
		if (ischunked) {
			if (fgets(xferbuf, bufsize, fin) == NULL) {
				warnx("Unexpected EOF reading chunk CRLF");
				goto cleanup_fetch_url;
			}
			if (strcmp(xferbuf, "\r\n") != 0) {
				warnx("Unexpected data following chunk");
				goto cleanup_fetch_url;
			}
			if (lastchunk)
				break;
		}
	} while (ischunked);

/* XXX: deal with optional trailer & CRLF here? */

	if (hash && !progress && bytes > 0) {
		if (bytes < mark)
			(void)putc('#', ttyout);
		(void)putc('\n', ttyout);
	}
	if (ferror(fin)) {
		warn("Reading file");
		goto cleanup_fetch_url;
	}
	progressmeter(1);
	(void)fflush(fout);
	if (closefunc == fclose && mtime != -1) {
		struct timeval tval[2];

		(void)gettimeofday(&tval[0], NULL);
		tval[1].tv_sec = mtime;
		tval[1].tv_usec = 0;
		(*closefunc)(fout);
		fout = NULL;

		if (utimes(savefile, tval) == -1) {
			fprintf(ttyout,
			    "Can't change modification time to %s",
			    rfc2822time(localtime(&mtime)));
		}
	}
	if (bytes > 0)
		ptransfer(0);
	bytes = 0;

	rval = 0;
	goto cleanup_fetch_url;

 improper:
	warnx("Improper response from `%s:%s'", host, port);

 cleanup_fetch_url:
	if (oldintr)
		(void)xsignal(SIGINT, oldintr);
	if (oldintp)
		(void)xsignal(SIGPIPE, oldintp);
	if (fin != NULL)
		fclose(fin);
	else if (s != -1)
		close(s);
	if (closefunc != NULL && fout != NULL)
		(*closefunc)(fout);
	if (res0)
		freeaddrinfo(res0);
	if (savefile != outfile)
		FREEPTR(savefile);
	FREEPTR(uuser);
	if (pass != NULL)
		memset(pass, 0, strlen(pass));
	FREEPTR(pass);
	FREEPTR(host);
	FREEPTR(port);
	FREEPTR(path);
	FREEPTR(decodedpath);
	FREEPTR(puser);
	if (ppass != NULL)
		memset(ppass, 0, strlen(ppass));
	FREEPTR(ppass);
	FREEPTR(auth);
	FREEPTR(location);
	FREEPTR(message);
	return (rval);
}

/*
 * Abort a HTTP retrieval
 */
void
aborthttp(int notused)
{
	char msgbuf[100];
	size_t len;

	sigint_raised = 1;
	alarmtimer(0);
	len = strlcpy(msgbuf, "\nHTTP fetch aborted.\n", sizeof(msgbuf));
	write(fileno(ttyout), msgbuf, len);
	siglongjmp(httpabort, 1);
}

/*
 * Retrieve ftp URL or classic ftp argument using FTP.
 * Returns 1 on failure, 0 on completed xfer, -1 if ftp connection
 * is still open (e.g, ftp xfer with trailing /)
 */
static int
fetch_ftp(const char *url)
{
	char		*cp, *xargv[5], rempath[MAXPATHLEN];
	char		*host, *path, *dir, *file, *uuser, *pass;
	char		*port;
	char		 cmdbuf[MAXPATHLEN];
	char		 dirbuf[4];
	int		 dirhasglob, filehasglob, rval, transtype, xargc;
	int		 oanonftp, oautologin;
	in_port_t	 portnum;
	url_t		 urltype;

	DPRINTF("fetch_ftp: `%s'\n", url);
	host = path = dir = file = uuser = pass = NULL;
	port = NULL;
	rval = 1;
	transtype = TYPE_I;

	if (STRNEQUAL(url, FTP_URL)) {
		if ((parse_url(url, "URL", &urltype, &uuser, &pass,
		    &host, &port, &portnum, &path) == -1) ||
		    (uuser != NULL && *uuser == '\0') ||
		    EMPTYSTRING(host)) {
			warnx("Invalid URL `%s'", url);
			goto cleanup_fetch_ftp;
		}
		/*
		 * Note: Don't url_decode(path) here.  We need to keep the
		 * distinction between "/" and "%2F" until later.
		 */

					/* check for trailing ';type=[aid]' */
		if (! EMPTYSTRING(path) && (cp = strrchr(path, ';')) != NULL) {
			if (strcasecmp(cp, ";type=a") == 0)
				transtype = TYPE_A;
			else if (strcasecmp(cp, ";type=i") == 0)
				transtype = TYPE_I;
			else if (strcasecmp(cp, ";type=d") == 0) {
				warnx(
			    "Directory listing via a URL is not supported");
				goto cleanup_fetch_ftp;
			} else {
				warnx("Invalid suffix `%s' in URL `%s'", cp,
				    url);
				goto cleanup_fetch_ftp;
			}
			*cp = 0;
		}
	} else {			/* classic style `[user@]host:[file]' */
		urltype = CLASSIC_URL_T;
		host = ftp_strdup(url);
		cp = strchr(host, '@');
		if (cp != NULL) {
			*cp = '\0';
			uuser = host;
			anonftp = 0;	/* disable anonftp */
			host = ftp_strdup(cp + 1);
		}
		cp = strchr(host, ':');
		if (cp != NULL) {
			*cp = '\0';
			path = ftp_strdup(cp + 1);
		}
	}
	if (EMPTYSTRING(host))
		goto cleanup_fetch_ftp;

			/* Extract the file and (if present) directory name. */
	dir = path;
	if (! EMPTYSTRING(dir)) {
		/*
		 * If we are dealing with classic `[user@]host:[path]' syntax,
		 * then a path of the form `/file' (resulting from input of the
		 * form `host:/file') means that we should do "CWD /" before
		 * retrieving the file.  So we set dir="/" and file="file".
		 *
		 * But if we are dealing with URLs like `ftp://host/path' then
		 * a path of the form `/file' (resulting from a URL of the form
		 * `ftp://host//file') means that we should do `CWD ' (with an
		 * empty argument) before retrieving the file.  So we set
		 * dir="" and file="file".
		 *
		 * If the path does not contain / at all, we set dir=NULL.
		 * (We get a path without any slashes if we are dealing with
		 * classic `[user@]host:[file]' or URL `ftp://host/file'.)
		 *
		 * In all other cases, we set dir to a string that does not
		 * include the final '/' that separates the dir part from the
		 * file part of the path.  (This will be the empty string if
		 * and only if we are dealing with a path of the form `/file'
		 * resulting from an URL of the form `ftp://host//file'.)
		 */
		cp = strrchr(dir, '/');
		if (cp == dir && urltype == CLASSIC_URL_T) {
			file = cp + 1;
			(void)strlcpy(dirbuf, "/", sizeof(dirbuf));
			dir = dirbuf;
		} else if (cp != NULL) {
			*cp++ = '\0';
			file = cp;
		} else {
			file = dir;
			dir = NULL;
		}
	} else
		dir = NULL;
	if (urltype == FTP_URL_T && file != NULL) {
		url_decode(file);
		/* but still don't url_decode(dir) */
	}
	DPRINTF("fetch_ftp: user `%s' pass `%s' host %s port %s "
	    "path `%s' dir `%s' file `%s'\n",
	    STRorNULL(uuser), STRorNULL(pass),
	    STRorNULL(host), STRorNULL(port),
	    STRorNULL(path), STRorNULL(dir), STRorNULL(file));

	dirhasglob = filehasglob = 0;
	if (doglob && urltype == CLASSIC_URL_T) {
		if (! EMPTYSTRING(dir) && strpbrk(dir, "*?[]{}") != NULL)
			dirhasglob = 1;
		if (! EMPTYSTRING(file) && strpbrk(file, "*?[]{}") != NULL)
			filehasglob = 1;
	}

			/* Set up the connection */
	oanonftp = anonftp;
	if (connected)
		disconnect(0, NULL);
	anonftp = oanonftp;
	(void)strlcpy(cmdbuf, getprogname(), sizeof(cmdbuf));
	xargv[0] = cmdbuf;
	xargv[1] = host;
	xargv[2] = NULL;
	xargc = 2;
	if (port) {
		xargv[2] = port;
		xargv[3] = NULL;
		xargc = 3;
	}
	oautologin = autologin;
		/* don't autologin in setpeer(), use ftp_login() below */
	autologin = 0;
	setpeer(xargc, xargv);
	autologin = oautologin;
	if ((connected == 0) ||
	    (connected == 1 && !ftp_login(host, uuser, pass))) {
		warnx("Can't connect or login to host `%s:%s'",
			host, port ? port : "?");
		goto cleanup_fetch_ftp;
	}

	switch (transtype) {
	case TYPE_A:
		setascii(1, xargv);
		break;
	case TYPE_I:
		setbinary(1, xargv);
		break;
	default:
		errx(1, "fetch_ftp: unknown transfer type %d", transtype);
	}

		/*
		 * Change directories, if necessary.
		 *
		 * Note: don't use EMPTYSTRING(dir) below, because
		 * dir=="" means something different from dir==NULL.
		 */
	if (dir != NULL && !dirhasglob) {
		char *nextpart;

		/*
		 * If we are dealing with a classic `[user@]host:[path]'
		 * (urltype is CLASSIC_URL_T) then we have a raw directory
		 * name (not encoded in any way) and we can change
		 * directories in one step.
		 *
		 * If we are dealing with an `ftp://host/path' URL
		 * (urltype is FTP_URL_T), then RFC3986 says we need to
		 * send a separate CWD command for each unescaped "/"
		 * in the path, and we have to interpret %hex escaping
		 * *after* we find the slashes.  It's possible to get
		 * empty components here, (from multiple adjacent
		 * slashes in the path) and RFC3986 says that we should
		 * still do `CWD ' (with a null argument) in such cases.
		 *
		 * Many ftp servers don't support `CWD ', so if there's an
		 * error performing that command, bail out with a descriptive
		 * message.
		 *
		 * Examples:
		 *
		 * host:			dir="", urltype=CLASSIC_URL_T
		 *		logged in (to default directory)
		 * host:file			dir=NULL, urltype=CLASSIC_URL_T
		 *		"RETR file"
		 * host:dir/			dir="dir", urltype=CLASSIC_URL_T
		 *		"CWD dir", logged in
		 * ftp://host/			dir="", urltype=FTP_URL_T
		 *		logged in (to default directory)
		 * ftp://host/dir/		dir="dir", urltype=FTP_URL_T
		 *		"CWD dir", logged in
		 * ftp://host/file		dir=NULL, urltype=FTP_URL_T
		 *		"RETR file"
		 * ftp://host//file		dir="", urltype=FTP_URL_T
		 *		"CWD ", "RETR file"
		 * host:/file			dir="/", urltype=CLASSIC_URL_T
		 *		"CWD /", "RETR file"
		 * ftp://host///file		dir="/", urltype=FTP_URL_T
		 *		"CWD ", "CWD ", "RETR file"
		 * ftp://host/%2F/file		dir="%2F", urltype=FTP_URL_T
		 *		"CWD /", "RETR file"
		 * ftp://host/foo/file		dir="foo", urltype=FTP_URL_T
		 *		"CWD foo", "RETR file"
		 * ftp://host/foo/bar/file	dir="foo/bar"
		 *		"CWD foo", "CWD bar", "RETR file"
		 * ftp://host//foo/bar/file	dir="/foo/bar"
		 *		"CWD ", "CWD foo", "CWD bar", "RETR file"
		 * ftp://host/foo//bar/file	dir="foo//bar"
		 *		"CWD foo", "CWD ", "CWD bar", "RETR file"
		 * ftp://host/%2F/foo/bar/file	dir="%2F/foo/bar"
		 *		"CWD /", "CWD foo", "CWD bar", "RETR file"
		 * ftp://host/%2Ffoo/bar/file	dir="%2Ffoo/bar"
		 *		"CWD /foo", "CWD bar", "RETR file"
		 * ftp://host/%2Ffoo%2Fbar/file	dir="%2Ffoo%2Fbar"
		 *		"CWD /foo/bar", "RETR file"
		 * ftp://host/%2Ffoo%2Fbar%2Ffile	dir=NULL
		 *		"RETR /foo/bar/file"
		 *
		 * Note that we don't need `dir' after this point.
		 */
		do {
			if (urltype == FTP_URL_T) {
				nextpart = strchr(dir, '/');
				if (nextpart) {
					*nextpart = '\0';
					nextpart++;
				}
				url_decode(dir);
			} else
				nextpart = NULL;
			DPRINTF("fetch_ftp: dir `%s', nextpart `%s'\n",
			    STRorNULL(dir), STRorNULL(nextpart));
			if (urltype == FTP_URL_T || *dir != '\0') {
				(void)strlcpy(cmdbuf, "cd", sizeof(cmdbuf));
				xargv[0] = cmdbuf;
				xargv[1] = dir;
				xargv[2] = NULL;
				dirchange = 0;
				cd(2, xargv);
				if (! dirchange) {
					if (*dir == '\0' && code == 500)
						fprintf(stderr,
"\n"
"ftp: The `CWD ' command (without a directory), which is required by\n"
"     RFC3986 to support the empty directory in the URL pathname (`//'),\n"
"     conflicts with the server's conformance to RFC0959.\n"
"     Try the same URL without the `//' in the URL pathname.\n"
"\n");
					goto cleanup_fetch_ftp;
				}
			}
			dir = nextpart;
		} while (dir != NULL);
	}

	if (EMPTYSTRING(file)) {
		rval = -1;
		goto cleanup_fetch_ftp;
	}

	if (dirhasglob) {
		(void)strlcpy(rempath, dir,	sizeof(rempath));
		(void)strlcat(rempath, "/",	sizeof(rempath));
		(void)strlcat(rempath, file,	sizeof(rempath));
		file = rempath;
	}

			/* Fetch the file(s). */
	xargc = 2;
	(void)strlcpy(cmdbuf, "get", sizeof(cmdbuf));
	xargv[0] = cmdbuf;
	xargv[1] = file;
	xargv[2] = NULL;
	if (dirhasglob || filehasglob) {
		int ointeractive;

		ointeractive = interactive;
		interactive = 0;
		if (restartautofetch)
			(void)strlcpy(cmdbuf, "mreget", sizeof(cmdbuf));
		else
			(void)strlcpy(cmdbuf, "mget", sizeof(cmdbuf));
		xargv[0] = cmdbuf;
		mget(xargc, xargv);
		interactive = ointeractive;
	} else {
		if (outfile == NULL) {
			cp = strrchr(file, '/');	/* find savefile */
			if (cp != NULL)
				outfile = cp + 1;
			else
				outfile = file;
		}
		xargv[2] = (char *)outfile;
		xargv[3] = NULL;
		xargc++;
		if (restartautofetch)
			reget(xargc, xargv);
		else
			get(xargc, xargv);
	}

	if ((code / 100) == COMPLETE)
		rval = 0;

 cleanup_fetch_ftp:
	FREEPTR(port);
	FREEPTR(host);
	FREEPTR(path);
	FREEPTR(uuser);
	if (pass)
		memset(pass, 0, strlen(pass));
	FREEPTR(pass);
	return (rval);
}

/*
 * Retrieve the given file to outfile.
 * Supports arguments of the form:
 *	"host:path", "ftp://host/path"	if $ftpproxy, call fetch_url() else
 *					call fetch_ftp()
 *	"http://host/path"		call fetch_url() to use HTTP
 *	"file:///path"			call fetch_url() to copy
 *	"about:..."			print a message
 *
 * Returns 1 on failure, 0 on completed xfer, -1 if ftp connection
 * is still open (e.g, ftp xfer with trailing /)
 */
static int
go_fetch(const char *url)
{
	char *proxyenv;

#ifndef NO_ABOUT
	/*
	 * Check for about:*
	 */
	if (STRNEQUAL(url, ABOUT_URL)) {
		url += sizeof(ABOUT_URL) -1;
		if (strcasecmp(url, "ftp") == 0 ||
		    strcasecmp(url, "tnftp") == 0) {
			fputs(
"This version of ftp has been enhanced by Luke Mewburn <lukem@NetBSD.org>\n"
"for the NetBSD project.  Execute `man ftp' for more details.\n", ttyout);
		} else if (strcasecmp(url, "lukem") == 0) {
			fputs(
"Luke Mewburn is the author of most of the enhancements in this ftp client.\n"
"Please email feedback to <lukem@NetBSD.org>.\n", ttyout);
		} else if (strcasecmp(url, "netbsd") == 0) {
			fputs(
"NetBSD is a freely available and redistributable UNIX-like operating system.\n"
"For more information, see http://www.NetBSD.org/\n", ttyout);
		} else if (strcasecmp(url, "version") == 0) {
			fprintf(ttyout, "Version: %s %s%s\n",
			    FTP_PRODUCT, FTP_VERSION,
#ifdef INET6
			    ""
#else
			    " (-IPv6)"
#endif
			);
		} else {
			fprintf(ttyout, "`%s' is an interesting topic.\n", url);
		}
		fputs("\n", ttyout);
		return (0);
	}
#endif

	/*
	 * Check for file:// and http:// URLs.
	 */
	if (STRNEQUAL(url, HTTP_URL) || STRNEQUAL(url, FILE_URL))
		return (fetch_url(url, NULL, NULL, NULL));

	/*
	 * Try FTP URL-style and host:file arguments next.
	 * If ftpproxy is set with an FTP URL, use fetch_url()
	 * Othewise, use fetch_ftp().
	 */
	proxyenv = getoptionvalue("ftp_proxy");
	if (!EMPTYSTRING(proxyenv) && STRNEQUAL(url, FTP_URL))
		return (fetch_url(url, NULL, NULL, NULL));

	return (fetch_ftp(url));
}

/*
 * Retrieve multiple files from the command line,
 * calling go_fetch() for each file.
 *
 * If an ftp path has a trailing "/", the path will be cd-ed into and
 * the connection remains open, and the function will return -1
 * (to indicate the connection is alive).
 * If an error occurs the return value will be the offset+1 in
 * argv[] of the file that caused a problem (i.e, argv[x]
 * returns x+1)
 * Otherwise, 0 is returned if all files retrieved successfully.
 */
int
auto_fetch(int argc, char *argv[])
{
	volatile int	argpos, rval;

	argpos = rval = 0;

	if (sigsetjmp(toplevel, 1)) {
		if (connected)
			disconnect(0, NULL);
		if (rval > 0)
			rval = argpos + 1;
		return (rval);
	}
	(void)xsignal(SIGINT, intr);
	(void)xsignal(SIGPIPE, lostpeer);

	/*
	 * Loop through as long as there's files to fetch.
	 */
	for (; (rval == 0) && (argpos < argc); argpos++) {
		if (strchr(argv[argpos], ':') == NULL)
			break;
		redirect_loop = 0;
		if (!anonftp)
			anonftp = 2;	/* Handle "automatic" transfers. */
		rval = go_fetch(argv[argpos]);
		if (outfile != NULL && strcmp(outfile, "-") != 0
		    && outfile[0] != '|')
			outfile = NULL;
		if (rval > 0)
			rval = argpos + 1;
	}

	if (connected && rval != -1)
		disconnect(0, NULL);
	return (rval);
}


/*
 * Upload multiple files from the command line.
 *
 * If an error occurs the return value will be the offset+1 in
 * argv[] of the file that caused a problem (i.e, argv[x]
 * returns x+1)
 * Otherwise, 0 is returned if all files uploaded successfully.
 */
int
auto_put(int argc, char **argv, const char *uploadserver)
{
	char	*uargv[4], *path, *pathsep;
	int	 uargc, rval, argpos;
	size_t	 len;
	char	 cmdbuf[MAX_C_NAME];

	(void)strlcpy(cmdbuf, "mput", sizeof(cmdbuf));
	uargv[0] = cmdbuf;
	uargv[1] = argv[0];
	uargc = 2;
	uargv[2] = uargv[3] = NULL;
	pathsep = NULL;
	rval = 1;

	DPRINTF("auto_put: target `%s'\n", uploadserver);

	path = ftp_strdup(uploadserver);
	len = strlen(path);
	if (path[len - 1] != '/' && path[len - 1] != ':') {
			/*
			 * make sure we always pass a directory to auto_fetch
			 */
		if (argc > 1) {		/* more than one file to upload */
			len = strlen(uploadserver) + 2;	/* path + "/" + "\0" */
			free(path);
			path = (char *)ftp_malloc(len);
			(void)strlcpy(path, uploadserver, len);
			(void)strlcat(path, "/", len);
		} else {		/* single file to upload */
			(void)strlcpy(cmdbuf, "put", sizeof(cmdbuf));
			uargv[0] = cmdbuf;
			pathsep = strrchr(path, '/');
			if (pathsep == NULL) {
				pathsep = strrchr(path, ':');
				if (pathsep == NULL) {
					warnx("Invalid URL `%s'", path);
					goto cleanup_auto_put;
				}
				pathsep++;
				uargv[2] = ftp_strdup(pathsep);
				pathsep[0] = '/';
			} else
				uargv[2] = ftp_strdup(pathsep + 1);
			pathsep[1] = '\0';
			uargc++;
		}
	}
	DPRINTF("auto_put: URL `%s' argv[2] `%s'\n",
	    path, STRorNULL(uargv[2]));

			/* connect and cwd */
	rval = auto_fetch(1, &path);
	if(rval >= 0)
		goto cleanup_auto_put;

	rval = 0;

			/* target filename provided; upload 1 file */
			/* XXX : is this the best way? */
	if (uargc == 3) {
		uargv[1] = argv[0];
		put(uargc, uargv);
		if ((code / 100) != COMPLETE)
			rval = 1;
	} else {	/* otherwise a target dir: upload all files to it */
		for(argpos = 0; argv[argpos] != NULL; argpos++) {
			uargv[1] = argv[argpos];
			mput(uargc, uargv);
			if ((code / 100) != COMPLETE) {
				rval = argpos + 1;
				break;
			}
		}
	}

 cleanup_auto_put:
	free(path);
	FREEPTR(uargv[2]);
	return (rval);
}
