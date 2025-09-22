/*	$OpenBSD: termcap.c,v 1.2 2015/11/15 07:12:50 deraadt Exp $	*/
/*	$NetBSD: termcap.c,v 1.7 1995/06/05 19:45:52 pk Exp $	*/

/*
 * Copyright (c) 1980, 1993
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

#define	PBUFSIZ		512	/* max length of filename path */
#define	PVECSIZ		32	/* max number of names in path */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <curses.h>
#include "pathnames.h"

/*
 * termcap - routines for dealing with the terminal capability data base
 *
 * BUG:		Should use a "last" pointer in tbuf, so that searching
 *		for capabilities alphabetically would not be a n**2/2
 *		process when large numbers of capabilities are given.
 * Note:	If we add a last pointer now we will screw up the
 *		tc capability. We really should compile termcap.
 *
 * Essentially all the work here is scanning and decoding escapes
 * in string capabilities.  We don't use stdio because the editor
 * doesn't, and because living w/o it is not hard.
 */

static	char *tbuf;	/* termcap buffer */

/*
 * Get an entry for terminal name in buffer bp from the termcap file.
 */
int
tgetent(char *bp, char *name)
{
	char *p, *cp, *dummy, **fname, *home;
	int    i;
	char   pathbuf[PATH_MAX];	/* holds raw path of filenames */
	char  *pathvec[PVECSIZ];	/* to point to names in pathbuf */
	char **pvec;			/* holds usable tail of path vector */
	char  *termpath;

	fname = pathvec;
	pvec = pathvec;
	tbuf = bp;

	cp = issetugid() ? NULL : getenv("TERMCAP");
	/*
	 * TERMCAP can have one of two things in it. It can be the name
	 * of a file to use instead of /usr/share/misc/termcap. In this
	 * case it better start with a "/". Or it can be an entry to use
	 * so we don't have to read the file. In this case it has to
	 * already have the newlines crunched out.  If TERMCAP does not
	 * hold a file name then a path of names is searched instead.
	 * The path is found in the TERMPATH variable, or becomes
	 * "$HOME/.termcap /usr/share/misc/termcap" if no TERMPATH exists.
	 */
	if (cp == NULL) {
		strlcpy(pathbuf, _PATH_TERMCAP, sizeof(pathbuf));
	} else if (!cp || *cp != '/') { /* TERMCAP holds an entry */
		if ((termpath = getenv("TERMPATH")) != NULL)
			strlcpy(pathbuf, termpath, sizeof(pathbuf));
		else if ((home = getenv("HOME")) == NULL || *home == '\0' ||
		    snprintf(pathbuf, sizeof(pathbuf), "%s/%s", home,
		    _PATH_DEF) >= sizeof(pathbuf))
			strlcpy(pathbuf, _PATH_DEF, sizeof(pathbuf));
	} else {		/* user-defined path in TERMCAP */
		/* still can be tokenized */
		strlcpy(pathbuf, cp, sizeof(pathbuf));
	}
	*fname++ = pathbuf;	/* tokenize path into vector of names */

	/* split pathbuf into a vector of paths */
	p = pathbuf;
	while (*++p)
		if (*p == ' ' || *p == ':') {
			*p = '\0';
			while (*++p)
				if (*p != ' ' && *p != ':')
					break;
			if (*p == '\0')
				break;
			*fname++ = p;
			if (fname >= pathvec + PVECSIZ) {
				fname--;
				break;
			}
		}
	*fname = (char *) 0;			/* mark end of vector */
	if (cp && *cp && *cp != '/')
		if (cgetset(cp) < 0)
			return (-2);

	dummy = NULL;
	i = cgetent(&dummy, pathvec, name);

	if (i == 0 && bp != NULL) {
		strlcpy(bp, dummy, 1024);
		if ((cp = strrchr(bp, ':')) != NULL)
			if (cp[1] != '\0')
				cp[1] = '\0';
	}
	else if (i == 0 && bp == NULL)
		tbuf = dummy;
	else if (dummy != NULL)
		free(dummy);

	/* no tc reference loop return code in libterm XXX */
	if (i == -3)
		return (-1);
	return (i + 1);
}

/*
 * Return the (numeric) option id.
 * Numeric options look like
 *	li#80
 * i.e. the option string is separated from the numeric value by
 * a # character.  If the option is not found we return -1.
 * Note that we handle octal numbers beginning with 0.
 */
int
tgetnum(char *id)
{
	long num;

	if (cgetnum(tbuf, id, &num) == 0)
		return (num);
	else
		return (-1);
}

/*
 * Handle a flag option.
 * Flag options are given "naked", i.e. followed by a : or the end
 * of the buffer.  Return 1 if we find the option, or 0 if it is
 * not given.
 */
int
tgetflag(char *id)
{
	return (cgetcap(tbuf, id, ':') != NULL);
}

/*
 * Get a string valued option.
 * These are given as
 *	cl=^Z
 * Much decoding is done on the strings, and the strings are
 * placed in area, which is a ref parameter which is updated.
 * No checking on area overflow.
 */
char *
tgetstr(char *id, char **area)
{
	char ids[3];
	char *s;
	int i;

	/*
	 * XXX
	 * This is for all the boneheaded programs that relied on tgetstr
	 * to look only at the first 2 characters of the string passed...
	 */
	*ids = *id;
	ids[1] = id[1];
	ids[2] = '\0';

	if ((i = cgetstr(tbuf, ids, &s)) < 0)
		return NULL;

	strlcpy(*area, s, 1024);
	*area += i + 1;
	return (s);
}
