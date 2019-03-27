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

#include "ftp_locl.h"
RCSID("$Id$");

static	int token (void);
static	FILE *cfile;

#define	DEFAULT	1
#define	LOGIN	2
#define	PASSWD	3
#define	ACCOUNT 4
#define MACDEF  5
#define PROT	6
#define	ID	10
#define	MACH	11

static char tokval[100];

static struct toktab {
	char *tokstr;
	int tval;
} toktab[]= {
	{ "default",	DEFAULT },
	{ "login",	LOGIN },
	{ "password",	PASSWD },
	{ "passwd",	PASSWD },
	{ "account",	ACCOUNT },
	{ "machine",	MACH },
	{ "macdef",	MACDEF },
	{ "prot", 	PROT },
	{ NULL,		0 }
};

/*
 * Write a copy of the hostname into `hostname, sz' and return a guess
 * as to the `domain' of that hostname.
 */

static char *
guess_domain (char *hostname_str, size_t sz)
{
    struct addrinfo *ai, *a;
    struct addrinfo hints;
    int error;
    char *dot;

    if (gethostname (hostname_str, sz) < 0) {
	strlcpy (hostname_str, "", sz);
	return "";
    }
    dot = strchr (hostname_str, '.');
    if (dot != NULL)
	return dot + 1;

    memset (&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;

    error = getaddrinfo (hostname_str, NULL, &hints, &ai);
    if (error)
	return hostname_str;

    for (a = ai; a != NULL; a = a->ai_next)
	if (a->ai_canonname != NULL) {
	    strlcpy (hostname_str, ai->ai_canonname, sz);
	    break;
	}
    freeaddrinfo (ai);
    dot = strchr (hostname_str, '.');
    if (dot != NULL)
	return dot + 1;
    else
	return hostname_str;
}

int
ruserpassword(char *host, char **aname, char **apass, char **aacct)
{
    char *hdir, buf[BUFSIZ], *tmp;
    int t, i, c, usedefault = 0;
    struct stat stb;

    mydomain = guess_domain (myhostname, MaxHostNameLen);

    hdir = getenv("HOME");
    if (hdir == NULL)
	hdir = ".";
    snprintf(buf, sizeof(buf), "%s/.netrc", hdir);
    cfile = fopen(buf, "r");
    if (cfile == NULL) {
	if (errno != ENOENT)
	    warn("%s", buf);
	return (0);
    }

next:
    while ((t = token())) switch(t) {

    case DEFAULT:
	usedefault = 1;
	/* FALL THROUGH */

    case MACH:
	if (!usedefault) {
	    if (token() != ID)
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
		tmp++ &&
		strcasecmp(tmp, mydomain) == 0 &&
		strncasecmp(hostname, tokval, tmp-hostname) == 0 &&
		tokval[tmp - hostname] == '\0')
		goto match;
	    if ((tmp = strchr(host, '.')) != NULL &&
		tmp++ &&
		strcasecmp(tmp, mydomain) == 0 &&
		strncasecmp(host, tokval, tmp - host) == 0 &&
		tokval[tmp - host] == '\0')
		goto match;
	    continue;
	}
    match:
	while ((t = token()) && t != MACH && t != DEFAULT) switch(t) {

	case LOGIN:
	    if (token()) {
		if (*aname == 0) {
		    *aname = strdup(tokval);
		} else {
		    if (strcmp(*aname, tokval))
			goto next;
		}
	    }
	    break;
	case PASSWD:
	    if ((*aname == NULL || strcmp(*aname, "anonymous")) &&
		fstat(fileno(cfile), &stb) >= 0 &&
		(stb.st_mode & 077) != 0) {
		warnx("Error: .netrc file is readable by others.");
		warnx("Remove password or make file unreadable by others.");
		goto bad;
	    }
	    if (token() && *apass == 0) {
		*apass = strdup(tokval);
	    }
	    break;
	case ACCOUNT:
	    if (fstat(fileno(cfile), &stb) >= 0
		&& (stb.st_mode & 077) != 0) {
		warnx("Error: .netrc file is readable by others.");
		warnx("Remove account or make file unreadable by others.");
		goto bad;
	    }
	    if (token() && *aacct == 0) {
		*aacct = strdup(tokval);
	    }
	    break;
	case MACDEF:
	    if (proxy) {
		fclose(cfile);
		return (0);
	    }
	    while ((c=getc(cfile)) != EOF &&
		   (c == ' ' || c == '\t'));
	    if (c == EOF || c == '\n') {
		printf("Missing macdef name argument.\n");
		goto bad;
	    }
	    if (macnum == 16) {
		printf("Limit of 16 macros have already been defined\n");
		goto bad;
	    }
	    tmp = macros[macnum].mac_name;
	    *tmp++ = c;
	    for (i=0; i < 8 && (c=getc(cfile)) != EOF &&
		     !isspace(c); ++i) {
		*tmp++ = c;
	    }
	    if (c == EOF) {
		printf("Macro definition missing null line terminator.\n");
		goto bad;
	    }
	    *tmp = '\0';
	    if (c != '\n') {
		while ((c=getc(cfile)) != EOF && c != '\n');
	    }
	    if (c == EOF) {
		printf("Macro definition missing null line terminator.\n");
		goto bad;
	    }
	    if (macnum == 0) {
		macros[macnum].mac_start = macbuf;
	    }
	    else {
		macros[macnum].mac_start = macros[macnum-1].mac_end + 1;
	    }
	    tmp = macros[macnum].mac_start;
	    while (tmp != macbuf + 4096) {
		if ((c=getc(cfile)) == EOF) {
		    printf("Macro definition missing null line terminator.\n");
		    goto bad;
		}
		*tmp = c;
		if (*tmp == '\n') {
		    if (*(tmp-1) == '\0') {
			macros[macnum++].mac_end = tmp - 1;
			break;
		    }
		    *tmp = '\0';
		}
		tmp++;
	    }
	    if (tmp == macbuf + 4096) {
		printf("4K macro buffer exceeded\n");
		goto bad;
	    }
	    break;
	case PROT:
	    token();
	    if(doencrypt == 0 && sec_request_prot(tokval) < 0)
		warnx("Unknown protection level \"%s\"", tokval);
	    break;
	default:
	    warnx("Unknown .netrc keyword %s", tokval);
	    break;
	}
	goto done;
    }
done:
    fclose(cfile);
    return (0);
bad:
    fclose(cfile);
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
				c = getc(cfile);
			*cp++ = c;
		}
	} else {
		*cp++ = c;
		while ((c = getc(cfile)) != EOF
		    && c != '\n' && c != '\t' && c != ' ' && c != ',') {
			if (c == '\\')
				c = getc(cfile);
			*cp++ = c;
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
