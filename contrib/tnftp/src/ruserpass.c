/*	$NetBSD: ruserpass.c,v 1.8 2007/08/06 04:33:24 lukem Exp $	*/
/*	from	NetBSD: ruserpass.c,v 1.33 2007/04/17 05:52:04 lukem Exp	*/

/*
 * Copyright (c) 1985, 1993, 1994
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
 * 3. Neither the name of the University nor the names of its contributors
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

#include "tnftp.h"

#if 0	/* tnftp */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)ruserpass.c	8.4 (Berkeley) 4/27/95";
#else
__RCSID(" NetBSD: ruserpass.c,v 1.33 2007/04/17 05:52:04 lukem Exp  ");
#endif
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#endif	/* tnftp */

#include "ftp_var.h"

static	int token(void);
static	FILE *cfile;

#define	DEFAULT	1
#define	LOGIN	2
#define	PASSWD	3
#define	ACCOUNT	4
#define	MACDEF	5
#define	ID	10
#define	MACH	11

static char tokval[100];

static struct toktab {
	const char *tokstr;
	int tval;
} toktab[] = {
	{ "default",	DEFAULT },
	{ "login",	LOGIN },
	{ "password",	PASSWD },
	{ "passwd",	PASSWD },
	{ "account",	ACCOUNT },
	{ "machine",	MACH },
	{ "macdef",	MACDEF },
	{ NULL,		0 }
};

int
ruserpass(const char *host, char **aname, char **apass, char **aacct)
{
	char *tmp;
	const char *mydomain;
	char myname[MAXHOSTNAMELEN + 1];
	int t, i, c, usedefault = 0;
	struct stat stb;

	if (netrc[0] == '\0')
		return (0);
	cfile = fopen(netrc, "r");
	if (cfile == NULL) {
		if (errno != ENOENT)
			warn("Can't read `%s'", netrc);
		return (0);
	}
	if (gethostname(myname, sizeof(myname)) < 0)
		myname[0] = '\0';
	myname[sizeof(myname) - 1] = '\0';
	if ((mydomain = strchr(myname, '.')) == NULL)
		mydomain = "";
 next:
	while ((t = token()) > 0) switch(t) {

	case DEFAULT:
		usedefault = 1;
		/* FALL THROUGH */

	case MACH:
		if (!usedefault) {
			if ((t = token()) == -1)
				goto bad;
			if (t != ID)
				continue;
			/*
			 * Allow match either for user's input host name
			 * or official hostname.  Also allow match of
			 * incompletely-specified host in local domain.
			 */
			if (strcasecmp(host, tokval) == 0)
				goto match;
			if (strcasecmp(hostname, tokval) == 0)
				goto match;
			if ((tmp = strchr(hostname, '.')) != NULL &&
			    strcasecmp(tmp, mydomain) == 0 &&
			    strncasecmp(hostname, tokval, tmp-hostname) == 0 &&
			    tokval[tmp - hostname] == '\0')
				goto match;
			if ((tmp = strchr(host, '.')) != NULL &&
			    strcasecmp(tmp, mydomain) == 0 &&
			    strncasecmp(host, tokval, tmp - host) == 0 &&
			    tokval[tmp - host] == '\0')
				goto match;
			continue;
		}
	match:
		while ((t = token()) > 0 &&
		    t != MACH && t != DEFAULT) switch(t) {

		case LOGIN:
			if ((t = token()) == -1)
				goto bad;
			if (t) {
				if (*aname == NULL)
					*aname = ftp_strdup(tokval);
				else {
					if (strcmp(*aname, tokval))
						goto next;
				}
			}
			break;
		case PASSWD:
			if ((*aname == NULL || strcmp(*aname, "anonymous")) &&
			    fstat(fileno(cfile), &stb) >= 0 &&
			    (stb.st_mode & 077) != 0) {
	warnx("Error: .netrc file is readable by others");
	warnx("Remove password or make file unreadable by others");
				goto bad;
			}
			if ((t = token()) == -1)
				goto bad;
			if (t && *apass == NULL)
				*apass = ftp_strdup(tokval);
			break;
		case ACCOUNT:
			if (fstat(fileno(cfile), &stb) >= 0
			    && (stb.st_mode & 077) != 0) {
	warnx("Error: .netrc file is readable by others");
	warnx("Remove account or make file unreadable by others");
				goto bad;
			}
			if ((t = token()) == -1)
				goto bad;
			if (t && *aacct == NULL)
				*aacct = ftp_strdup(tokval);
			break;
		case MACDEF:
			if (proxy) {
				(void)fclose(cfile);
				return (0);
			}
			while ((c = getc(cfile)) != EOF)
				if (c != ' ' && c != '\t')
					break;
			if (c == EOF || c == '\n') {
				fputs("Missing macdef name argument.\n",
				    ttyout);
				goto bad;
			}
			if (macnum == 16) {
				fputs(
			    "Limit of 16 macros have already been defined.\n",
				    ttyout);
				goto bad;
			}
			tmp = macros[macnum].mac_name;
			*tmp++ = c;
			for (i = 0; i < 8 && (c = getc(cfile)) != EOF &&
			    !isspace(c); ++i) {
				*tmp++ = c;
			}
			if (c == EOF) {
				fputs(
			    "Macro definition missing null line terminator.\n",
				    ttyout);
				goto bad;
			}
			*tmp = '\0';
			if (c != '\n') {
				while ((c = getc(cfile)) != EOF && c != '\n');
			}
			if (c == EOF) {
				fputs(
			    "Macro definition missing null line terminator.\n",
				    ttyout);
				goto bad;
			}
			if (macnum == 0) {
				macros[macnum].mac_start = macbuf;
			}
			else {
				macros[macnum].mac_start =
				    macros[macnum-1].mac_end + 1;
			}
			tmp = macros[macnum].mac_start;
			while (tmp != macbuf + 4096) {
				if ((c = getc(cfile)) == EOF) {
					fputs(
			    "Macro definition missing null line terminator.\n",
					    ttyout);
					goto bad;
				}
				*tmp = c;
				if (*tmp == '\n') {
					if (tmp == macros[macnum].mac_start) {
						macros[macnum++].mac_end = tmp;
						break;
					} else if (*(tmp - 1) == '\0') {
						macros[macnum++].mac_end =
						    tmp - 1;
						break;
					}
					*tmp = '\0';
				}
				tmp++;
			}
			if (tmp == macbuf + 4096) {
				fputs("4K macro buffer exceeded.\n",
				    ttyout);
				goto bad;
			}
			break;
		default:
			warnx("Unknown .netrc keyword `%s'", tokval);
			break;
		}
		goto done;
	}
 done:
	if (t == -1)
		goto bad;
	(void)fclose(cfile);
	return (0);
 bad:
	(void)fclose(cfile);
	return (-1);
}

static int
token(void)
{
	char *cp;
	int c;
	struct toktab *t;

	if (feof(cfile) || ferror(cfile))
		return (0);
	while ((c = getc(cfile)) != EOF &&
	    (c == '\n' || c == '\t' || c == ' ' || c == ','))
		continue;
	if (c == EOF)
		return (0);
	cp = tokval;
	if (c == '"') {
		while ((c = getc(cfile)) != EOF && c != '"') {
			if (c == '\\')
				if ((c = getc(cfile)) == EOF)
					break;
			*cp++ = c;
			if (cp == tokval + sizeof(tokval)) {
				warnx("Token in .netrc too long");
				return (-1);
			}
		}
	} else {
		*cp++ = c;
		while ((c = getc(cfile)) != EOF
		    && c != '\n' && c != '\t' && c != ' ' && c != ',') {
			if (c == '\\')
				if ((c = getc(cfile)) == EOF)
					break;
			*cp++ = c;
			if (cp == tokval + sizeof(tokval)) {
				warnx("Token in .netrc too long");
				return (-1);
			}
		}
	}
	*cp = 0;
	if (tokval[0] == 0)
		return (0);
	for (t = toktab; t->tokstr; t++)
		if (!strcmp(t->tokstr, tokval))
			return (t->tval);
	return (ID);
}
