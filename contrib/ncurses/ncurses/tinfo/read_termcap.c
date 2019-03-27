/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 * Termcap compatibility support
 *
 * If your OS integrator didn't install a terminfo database, you can call
 * _nc_read_termcap_entry() to support reading and translating capabilities
 * from the system termcap file.  This is a kludge; it will bulk up and slow
 * down every program that uses ncurses, and translated termcap entries cannot
 * use full terminfo capabilities.  Don't use it unless you absolutely have to;
 * instead, get your system people to run tic(1) from root on the terminfo
 * master included with ncurses to translate it into a terminfo database.
 *
 * If USE_GETCAP is enabled, we use what is effectively a copy of the 4.4BSD
 * getcap code to fetch entries.  There are disadvantages to this; mainly that
 * getcap(3) does its own resolution, meaning that entries read in in this way
 * can't reference the terminfo tree.  The only thing it buys is faster startup
 * time, getcap(3) is much faster than our tic parser.
 */

#include <curses.priv.h>

#include <ctype.h>
#include <sys/types.h>
#include <tic.h>

MODULE_ID("$Id: read_termcap.c,v 1.89 2013/12/15 00:32:43 tom Exp $")

#if !PURE_TERMINFO

#define TC_SUCCESS     0
#define TC_NOT_FOUND  -1
#define TC_SYS_ERR    -2
#define TC_REF_LOOP   -3
#define TC_UNRESOLVED -4	/* this is not returned by BSD cgetent */

static NCURSES_CONST char *
get_termpath(void)
{
    NCURSES_CONST char *result;

    if (!use_terminfo_vars() || (result = getenv("TERMPATH")) == 0)
	result = TERMPATH;
    TR(TRACE_DATABASE, ("TERMPATH is %s", result));
    return result;
}

/*
 * Note:
 * getcap(), cgetent(), etc., are BSD functions.  A copy of those was added to
 * this file in November 1995, derived from the BSD4.4 Lite sources.
 *
 * The initial adaptation uses 518 lines from that source.
 * The current source (in 2009) uses 183 lines of BSD4.4 Lite (441 ignoring
 * whitespace).
 */
#if USE_GETCAP

#if HAVE_BSD_CGETENT
#define _nc_cgetcap   cgetcap
#define _nc_cgetent(buf, oline, db_array, name) cgetent(buf, db_array, name)
#define _nc_cgetmatch cgetmatch
#define _nc_cgetset   cgetset
#else
static int _nc_cgetmatch(char *, const char *);
static int _nc_getent(char **, unsigned *, int *, int, char **, int, const char
		      *, int, char *);
static int _nc_nfcmp(const char *, char *);

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Casey Leedom of Lawrence Livermore National Laboratory.
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

/* static char sccsid[] = "@(#)getcap.c	8.3 (Berkeley) 3/25/94"; */

#define	BFRAG		1024
#define	BSIZE		1024
#define	MAX_RECURSION	32	/* maximum getent recursion */

static size_t topreclen;	/* toprec length */
static char *toprec;		/* Additional record specified by cgetset() */
static int gottoprec;		/* Flag indicating retrieval of toprecord */

/*
 * Cgetset() allows the addition of a user specified buffer to be added to the
 * database array, in effect "pushing" the buffer on top of the virtual
 * database.  0 is returned on success, -1 on failure.
 */
static int
_nc_cgetset(const char *ent)
{
    if (ent == 0) {
	FreeIfNeeded(toprec);
	toprec = 0;
	topreclen = 0;
	return (0);
    }
    topreclen = strlen(ent);
    if ((toprec = typeMalloc(char, topreclen + 1)) == 0) {
	errno = ENOMEM;
	return (-1);
    }
    gottoprec = 0;
    _nc_STRCPY(toprec, ent, topreclen);
    return (0);
}

/*
 * Cgetcap searches the capability record buf for the capability cap with type
 * `type'.  A pointer to the value of cap is returned on success, 0 if the
 * requested capability couldn't be found.
 *
 * Specifying a type of ':' means that nothing should follow cap (:cap:).  In
 * this case a pointer to the terminating ':' or NUL will be returned if cap is
 * found.
 *
 * If (cap, '@') or (cap, terminator, '@') is found before (cap, terminator)
 * return 0.
 */
static char *
_nc_cgetcap(char *buf, const char *cap, int type)
{
    register const char *cp;
    register char *bp;

    bp = buf;
    for (;;) {
	/*
	 * Skip past the current capability field - it's either the
	 * name field if this is the first time through the loop, or
	 * the remainder of a field whose name failed to match cap.
	 */
	for (;;) {
	    if (*bp == '\0')
		return (0);
	    else if (*bp++ == ':')
		break;
	}

	/*
	 * Try to match (cap, type) in buf.
	 */
	for (cp = cap; *cp == *bp && *bp != '\0'; cp++, bp++)
	    continue;
	if (*cp != '\0')
	    continue;
	if (*bp == '@')
	    return (0);
	if (type == ':') {
	    if (*bp != '\0' && *bp != ':')
		continue;
	    return (bp);
	}
	if (*bp != type)
	    continue;
	bp++;
	return (*bp == '@' ? 0 : bp);
    }
    /* NOTREACHED */
}

/*
 * Cgetent extracts the capability record name from the NULL terminated file
 * array db_array and returns a pointer to a malloc'd copy of it in buf.  Buf
 * must be retained through all subsequent calls to cgetcap, cgetnum, cgetflag,
 * and cgetstr, but may then be freed.
 *
 * Returns:
 *
 * positive #    on success (i.e., the index in db_array)
 * TC_NOT_FOUND  if the requested record couldn't be found
 * TC_SYS_ERR    if a system error was encountered (e.g.,couldn't open a file)
 * TC_REF_LOOP   if a potential reference loop is detected
 * TC_UNRESOLVED if we had too many recurrences to resolve
 */
static int
_nc_cgetent(char **buf, int *oline, char **db_array, const char *name)
{
    unsigned dummy;

    return (_nc_getent(buf, &dummy, oline, 0, db_array, -1, name, 0, 0));
}

/*
 * Getent implements the functions of cgetent.  If fd is non-negative,
 * *db_array has already been opened and fd is the open file descriptor.  We
 * do this to save time and avoid using up file descriptors for tc=
 * recursions.
 *
 * Getent returns the same success/failure codes as cgetent.  On success, a
 * pointer to a malloc'd capability record with all tc= capabilities fully
 * expanded and its length (not including trailing ASCII NUL) are left in
 * *cap and *len.
 *
 * Basic algorithm:
 *	+ Allocate memory incrementally as needed in chunks of size BFRAG
 *	  for capability buffer.
 *	+ Recurse for each tc=name and interpolate result.  Stop when all
 *	  names interpolated, a name can't be found, or depth exceeds
 *	  MAX_RECURSION.
 */
#define DOALLOC(size) typeRealloc(char, size, record)
static int
_nc_getent(
	      char **cap,	/* termcap-content */
	      unsigned *len,	/* length, needed for recursion */
	      int *beginning,	/* line-number at match */
	      int in_array,	/* index in 'db_array[] */
	      char **db_array,	/* list of files to search */
	      int fd,
	      const char *name,
	      int depth,
	      char *nfield)
{
    register char *r_end, *rp;
    int myfd = FALSE;
    char *record = 0;
    int tc_not_resolved;
    int current;
    int lineno;

    /*
     * Return with ``loop detected'' error if we've recurred more than
     * MAX_RECURSION times.
     */
    if (depth > MAX_RECURSION)
	return (TC_REF_LOOP);

    /*
     * Check if we have a top record from cgetset().
     */
    if (depth == 0 && toprec != 0 && _nc_cgetmatch(toprec, name) == 0) {
	if ((record = DOALLOC(topreclen + BFRAG)) == 0) {
	    errno = ENOMEM;
	    return (TC_SYS_ERR);
	}
	_nc_STRCPY(record, toprec, topreclen + BFRAG);
	rp = record + topreclen + 1;
	r_end = rp + BFRAG;
	current = in_array;
    } else {
	int foundit;

	/*
	 * Allocate first chunk of memory.
	 */
	if ((record = DOALLOC(BFRAG)) == 0) {
	    errno = ENOMEM;
	    return (TC_SYS_ERR);
	}
	rp = r_end = record + BFRAG;
	foundit = FALSE;

	/*
	 * Loop through database array until finding the record.
	 */
	for (current = in_array; db_array[current] != 0; current++) {
	    int eof = FALSE;

	    /*
	     * Open database if not already open.
	     */
	    if (fd >= 0) {
		(void) lseek(fd, (off_t) 0, SEEK_SET);
	    } else if ((_nc_access(db_array[current], R_OK) < 0)
		       || (fd = open(db_array[current], O_RDONLY, 0)) < 0) {
		/* No error on unfound file. */
		if (errno == ENOENT)
		    continue;
		free(record);
		return (TC_SYS_ERR);
	    } else {
		myfd = TRUE;
	    }
	    lineno = 0;

	    /*
	     * Find the requested capability record ...
	     */
	    {
		char buf[2048];
		register char *b_end = buf;
		register char *bp = buf;
		register int c;

		/*
		 * Loop invariants:
		 *      There is always room for one more character in record.
		 *      R_end always points just past end of record.
		 *      Rp always points just past last character in record.
		 *      B_end always points just past last character in buf.
		 *      Bp always points at next character in buf.
		 */

		for (;;) {
		    int first = lineno + 1;

		    /*
		     * Read in a line implementing (\, newline)
		     * line continuation.
		     */
		    rp = record;
		    for (;;) {
			if (bp >= b_end) {
			    int n;

			    n = read(fd, buf, sizeof(buf));
			    if (n <= 0) {
				if (myfd)
				    (void) close(fd);
				if (n < 0) {
				    free(record);
				    return (TC_SYS_ERR);
				}
				fd = -1;
				eof = TRUE;
				break;
			    }
			    b_end = buf + n;
			    bp = buf;
			}

			c = *bp++;
			if (c == '\n') {
			    lineno++;
			    /*
			     * Unlike BSD 4.3, this ignores a backslash at the
			     * end of a comment-line.  That makes it consistent
			     * with the rest of ncurses -TD
			     */
			    if (rp == record
				|| *record == '#'
				|| *(rp - 1) != '\\')
				break;
			}
			*rp++ = c;

			/*
			 * Enforce loop invariant: if no room
			 * left in record buffer, try to get
			 * some more.
			 */
			if (rp >= r_end) {
			    unsigned pos;
			    size_t newsize;

			    pos = rp - record;
			    newsize = r_end - record + BFRAG;
			    record = DOALLOC(newsize);
			    if (record == 0) {
				if (myfd)
				    (void) close(fd);
				errno = ENOMEM;
				return (TC_SYS_ERR);
			    }
			    r_end = record + newsize;
			    rp = record + pos;
			}
		    }
		    /* loop invariant lets us do this */
		    *rp++ = '\0';

		    /*
		     * If encountered eof check next file.
		     */
		    if (eof)
			break;

		    /*
		     * Toss blank lines and comments.
		     */
		    if (*record == '\0' || *record == '#')
			continue;

		    /*
		     * See if this is the record we want ...
		     */
		    if (_nc_cgetmatch(record, name) == 0
			&& (nfield == 0
			    || !_nc_nfcmp(nfield, record))) {
			foundit = TRUE;
			*beginning = first;
			break;	/* found it! */
		    }
		}
	    }
	    if (foundit)
		break;
	}

	if (!foundit) {
	    free(record);
	    return (TC_NOT_FOUND);
	}
    }

    /*
     * Got the capability record, but now we have to expand all tc=name
     * references in it ...
     */
    {
	register char *newicap, *s;
	register int newilen;
	unsigned ilen;
	int diff, iret, tclen, oline;
	char *icap = 0, *scan, *tc, *tcstart, *tcend;

	/*
	 * Loop invariants:
	 *      There is room for one more character in record.
	 *      R_end points just past end of record.
	 *      Rp points just past last character in record.
	 *      Scan points at remainder of record that needs to be
	 *      scanned for tc=name constructs.
	 */
	scan = record;
	tc_not_resolved = FALSE;
	for (;;) {
	    if ((tc = _nc_cgetcap(scan, "tc", '=')) == 0) {
		break;
	    }

	    /*
	     * Find end of tc=name and stomp on the trailing `:'
	     * (if present) so we can use it to call ourselves.
	     */
	    s = tc;
	    while (*s != '\0') {
		if (*s++ == ':') {
		    *(s - 1) = '\0';
		    break;
		}
	    }
	    tcstart = tc - 3;
	    tclen = s - tcstart;
	    tcend = s;

	    icap = 0;
	    iret = _nc_getent(&icap, &ilen, &oline, current, db_array, fd,
			      tc, depth + 1, 0);
	    newicap = icap;	/* Put into a register. */
	    newilen = ilen;
	    if (iret != TC_SUCCESS) {
		/* an error */
		if (iret < TC_NOT_FOUND) {
		    if (myfd)
			(void) close(fd);
		    free(record);
		    FreeIfNeeded(icap);
		    return (iret);
		}
		if (iret == TC_UNRESOLVED) {
		    tc_not_resolved = TRUE;
		    /* couldn't resolve tc */
		} else if (iret == TC_NOT_FOUND) {
		    *(s - 1) = ':';
		    scan = s - 1;
		    tc_not_resolved = TRUE;
		    continue;
		}
	    }

	    /* not interested in name field of tc'ed record */
	    s = newicap;
	    while (*s != '\0' && *s++ != ':') ;
	    newilen -= s - newicap;
	    newicap = s;

	    /* make sure interpolated record is `:'-terminated */
	    s += newilen;
	    if (*(s - 1) != ':') {
		*s = ':';	/* overwrite NUL with : */
		newilen++;
	    }

	    /*
	     * Make sure there's enough room to insert the
	     * new record.
	     */
	    diff = newilen - tclen;
	    if (diff >= r_end - rp) {
		unsigned pos, tcpos, tcposend;
		size_t newsize;

		pos = rp - record;
		newsize = r_end - record + diff + BFRAG;
		tcpos = tcstart - record;
		tcposend = tcend - record;
		record = DOALLOC(newsize);
		if (record == 0) {
		    if (myfd)
			(void) close(fd);
		    free(icap);
		    errno = ENOMEM;
		    return (TC_SYS_ERR);
		}
		r_end = record + newsize;
		rp = record + pos;
		tcstart = record + tcpos;
		tcend = record + tcposend;
	    }

	    /*
	     * Insert tc'ed record into our record.
	     */
	    s = tcstart + newilen;
	    memmove(s, tcend, (size_t) (rp - tcend));
	    memmove(tcstart, newicap, (size_t) newilen);
	    rp += diff;
	    free(icap);

	    /*
	     * Start scan on `:' so next cgetcap works properly
	     * (cgetcap always skips first field).
	     */
	    scan = s - 1;
	}
    }

    /*
     * Close file (if we opened it), give back any extra memory, and
     * return capability, length and success.
     */
    if (myfd)
	(void) close(fd);
    *len = rp - record - 1;	/* don't count NUL */
    if (r_end > rp) {
	if ((record = DOALLOC((size_t) (rp - record))) == 0) {
	    errno = ENOMEM;
	    return (TC_SYS_ERR);
	}
    }

    *cap = record;
    if (tc_not_resolved) {
	return (TC_UNRESOLVED);
    }
    return (current);
}

/*
 * Cgetmatch will return 0 if name is one of the names of the capability
 * record buf, -1 if not.
 */
static int
_nc_cgetmatch(char *buf, const char *name)
{
    register const char *np;
    register char *bp;

    /*
     * Start search at beginning of record.
     */
    bp = buf;
    for (;;) {
	/*
	 * Try to match a record name.
	 */
	np = name;
	for (;;) {
	    if (*np == '\0') {
		if (*bp == '|' || *bp == ':' || *bp == '\0')
		    return (0);
		else
		    break;
	    } else if (*bp++ != *np++) {
		break;
	    }
	}

	/*
	 * Match failed, skip to next name in record.
	 */
	bp--;			/* a '|' or ':' may have stopped the match */
	for (;;) {
	    if (*bp == '\0' || *bp == ':')
		return (-1);	/* match failed totally */
	    else if (*bp++ == '|')
		break;		/* found next name */
	}
    }
}

/*
 * Compare name field of record.
 */
static int
_nc_nfcmp(const char *nf, char *rec)
{
    char *cp, tmp;
    int ret;

    for (cp = rec; *cp != ':'; cp++) ;

    tmp = *(cp + 1);
    *(cp + 1) = '\0';
    ret = strcmp(nf, rec);
    *(cp + 1) = tmp;

    return (ret);
}
#endif /* HAVE_BSD_CGETENT */

/*
 * Since ncurses provides its own 'tgetent()', we cannot use the native one.
 * So we reproduce the logic to get down to cgetent() -- or our cut-down
 * version of that -- to circumvent the problem of configuring against the
 * termcap library.
 */
#define USE_BSD_TGETENT 1

#if USE_BSD_TGETENT
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgment:
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

/* static char sccsid[] = "@(#)termcap.c	8.1 (Berkeley) 6/4/93" */

#define	PBUFSIZ		512	/* max length of filename path */
#define	PVECSIZ		32	/* max number of names in path */
#define TBUFSIZ (2048*2)

/*
 * On entry, srcp points to a non ':' character which is the beginning of the
 * token, if any.  We'll try to return a string that doesn't end with a ':'.
 */
static char *
get_tc_token(char **srcp, int *endp)
{
    int ch;
    bool found = FALSE;
    char *s, *base;
    char *tok = 0;

    *endp = TRUE;
    for (s = base = *srcp; *s != '\0';) {
	ch = *s++;
	if (ch == '\\') {
	    if (*s == '\0') {
		break;
	    } else if (*s++ == '\n') {
		while (isspace(UChar(*s)))
		    s++;
	    } else {
		found = TRUE;
	    }
	} else if (ch == ':') {
	    if (found) {
		tok = base;
		s[-1] = '\0';
		*srcp = s;
		*endp = FALSE;
		break;
	    }
	    base = s;
	} else if (isgraph(UChar(ch))) {
	    found = TRUE;
	}
    }

    /* malformed entry may end without a ':' */
    if (tok == 0 && found) {
	tok = base;
    }

    return tok;
}

static char *
copy_tc_token(char *dst, const char *src, size_t len)
{
    int ch;

    while ((ch = *src++) != '\0') {
	if (ch == '\\' && *src == '\n') {
	    while (isspace(UChar(*src)))
		src++;
	    continue;
	}
	if (--len == 0) {
	    dst = 0;
	    break;
	}
	*dst++ = (char) ch;
    }
    return dst;
}

/*
 * Get an entry for terminal name in buffer bp from the termcap file.
 */
static int
_nc_tgetent(char *bp, char **sourcename, int *lineno, const char *name)
{
    static char *the_source;

    register char *p;
    register char *cp;
    char *dummy = NULL;
    CGETENT_CONST char **fname;
    char *home;
    int i;
    char pathbuf[PBUFSIZ];	/* holds raw path of filenames */
    CGETENT_CONST char *pathvec[PVECSIZ];	/* point to names in pathbuf */
    NCURSES_CONST char *termpath;
    string_desc desc;

    *lineno = 1;
    fname = pathvec;
    p = pathbuf;
    cp = use_terminfo_vars()? getenv("TERMCAP") : NULL;

    /*
     * TERMCAP can have one of two things in it.  It can be the name of a file
     * to use instead of /etc/termcap.  In this case it better start with a
     * "/".  Or it can be an entry to use so we don't have to read the file. 
     * In this case it has to already have the newlines crunched out.  If
     * TERMCAP does not hold a file name then a path of names is searched
     * instead.  The path is found in the TERMPATH variable, or becomes
     * "$HOME/.termcap /etc/termcap" if no TERMPATH exists.
     */
    _nc_str_init(&desc, pathbuf, sizeof(pathbuf));
    if (cp == NULL) {
	_nc_safe_strcpy(&desc, get_termpath());
    } else if (!_nc_is_abs_path(cp)) {	/* TERMCAP holds an entry */
	if ((termpath = get_termpath()) != 0) {
	    _nc_safe_strcat(&desc, termpath);
	} else {
	    char temp[PBUFSIZ];
	    temp[0] = 0;
	    if ((home = getenv("HOME")) != 0 && *home != '\0'
		&& strchr(home, ' ') == 0
		&& strlen(home) < sizeof(temp) - 10) {	/* setup path */
		_nc_SPRINTF(temp, _nc_SLIMIT(sizeof(temp))
			    "%s/", home);	/* $HOME first */
	    }
	    /* if no $HOME look in current directory */
	    _nc_STRCAT(temp, ".termcap", sizeof(temp));
	    _nc_safe_strcat(&desc, temp);
	    _nc_safe_strcat(&desc, " ");
	    _nc_safe_strcat(&desc, get_termpath());
	}
    } else {			/* user-defined name in TERMCAP */
	_nc_safe_strcat(&desc, cp);	/* still can be tokenized */
    }

    *fname++ = pathbuf;		/* tokenize path into vector of names */
    while (*++p) {
	if (*p == ' ' || *p == NCURSES_PATHSEP) {
	    *p = '\0';
	    while (*++p)
		if (*p != ' ' && *p != NCURSES_PATHSEP)
		    break;
	    if (*p == '\0')
		break;
	    *fname++ = p;
	    if (fname >= pathvec + PVECSIZ) {
		fname--;
		break;
	    }
	}
    }
    *fname = 0;			/* mark end of vector */
#if !HAVE_BSD_CGETENT
    (void) _nc_cgetset(0);
#endif
    if (_nc_is_abs_path(cp)) {
	if (_nc_cgetset(cp) < 0) {
	    return (TC_SYS_ERR);
	}
    }

    i = _nc_cgetent(&dummy, lineno, pathvec, name);

    /* ncurses' termcap-parsing routines cannot handle multiple adjacent
     * empty fields, and mistakenly use the last valid cap entry instead of
     * the first (breaks tc= includes)
     */
    *bp = '\0';
    if (i >= 0) {
	char *pd, *ps, *tok;
	int endflag = FALSE;
	char *list[1023];
	size_t n, count = 0;

	pd = bp;
	ps = dummy;
	while (!endflag && (tok = get_tc_token(&ps, &endflag)) != 0) {
	    bool ignore = FALSE;

	    for (n = 1; n < count; n++) {
		char *s = list[n];
		if (s[0] == tok[0]
		    && s[1] == tok[1]) {
		    ignore = TRUE;
		    break;
		}
	    }
	    if (ignore != TRUE) {
		list[count++] = tok;
		pd = copy_tc_token(pd, tok, (size_t) (TBUFSIZ - (2 + pd - bp)));
		if (pd == 0) {
		    i = -1;
		    break;
		}
		*pd++ = ':';
		*pd = '\0';
	    }
	}
    }

    FreeIfNeeded(dummy);
    FreeIfNeeded(the_source);
    the_source = 0;

    /* This is not related to the BSD cgetent(), but to fake up a suitable
     * filename for ncurses' error reporting.  (If we are not using BSD
     * cgetent, then it is the actual filename).
     */
    if (i >= 0) {
#if HAVE_BSD_CGETENT
	char temp[PATH_MAX];

	_nc_str_init(&desc, temp, sizeof(temp));
	_nc_safe_strcpy(&desc, pathvec[i]);
	_nc_safe_strcat(&desc, ".db");
	if (_nc_access(temp, R_OK) == 0) {
	    _nc_safe_strcpy(&desc, pathvec[i]);
	}
	if ((the_source = strdup(temp)) != 0)
	    *sourcename = the_source;
#else
	if ((the_source = strdup(pathvec[i])) != 0)
	    *sourcename = the_source;
#endif
    }

    return (i);
}
#endif /* USE_BSD_TGETENT */
#endif /* USE_GETCAP */

#define MAXPATHS	32

/*
 * Add a filename to the list in 'termpaths[]', checking that we really have
 * a right to open the file.
 */
#if !USE_GETCAP
static int
add_tc(char *termpaths[], char *path, int count)
{
    char *save = strchr(path, NCURSES_PATHSEP);
    if (save != 0)
	*save = '\0';
    if (count < MAXPATHS
	&& _nc_access(path, R_OK) == 0) {
	termpaths[count++] = path;
	TR(TRACE_DATABASE, ("Adding termpath %s", path));
    }
    termpaths[count] = 0;
    if (save != 0)
	*save = NCURSES_PATHSEP;
    return count;
}
#define ADD_TC(path, count) filecount = add_tc(termpaths, path, count)
#endif /* !USE_GETCAP */

NCURSES_EXPORT(int)
_nc_read_termcap_entry(const char *const tn, TERMTYPE *const tp)
{
    int found = TGETENT_NO;
    ENTRY *ep;
#if USE_GETCAP_CACHE
    char cwd_buf[PATH_MAX];
#endif
#if USE_GETCAP
    char *p, tc[TBUFSIZ];
    int status;
    static char *source;
    static int lineno;

    TR(TRACE_DATABASE, ("read termcap entry for %s", tn));

    if (strlen(tn) == 0
	|| strcmp(tn, ".") == 0
	|| strcmp(tn, "..") == 0
	|| _nc_pathlast(tn) != 0) {
	TR(TRACE_DATABASE, ("illegal or missing entry name '%s'", tn));
	return TGETENT_NO;
    }

    if (use_terminfo_vars() && (p = getenv("TERMCAP")) != 0
	&& !_nc_is_abs_path(p) && _nc_name_match(p, tn, "|:")) {
	/* TERMCAP holds a termcap entry */
	strncpy(tc, p, sizeof(tc) - 1);
	tc[sizeof(tc) - 1] = '\0';
	_nc_set_source("TERMCAP");
    } else {
	/* we're using getcap(3) */
	if ((status = _nc_tgetent(tc, &source, &lineno, tn)) < 0)
	    return (status == TC_NOT_FOUND ? TGETENT_NO : TGETENT_ERR);

	_nc_curr_line = lineno;
	_nc_set_source(source);
    }
    _nc_read_entry_source((FILE *) 0, tc, FALSE, TRUE, NULLHOOK);
#else
    /*
     * Here is what the 4.4BSD termcap(3) page prescribes:
     *
     * It will look in the environment for a TERMCAP variable.  If found, and
     * the value does not begin with a slash, and the terminal type name is the
     * same as the environment string TERM, the TERMCAP string is used instead
     * of reading a termcap file.  If it does begin with a slash, the string is
     * used as a path name of the termcap file to search.  If TERMCAP does not
     * begin with a slash and name is different from TERM, tgetent() searches
     * the files $HOME/.termcap and /usr/share/misc/termcap, in that order,
     * unless the environment variable TERMPATH exists, in which case it
     * specifies a list of file pathnames (separated by spaces or colons) to be
     * searched instead.
     *
     * It goes on to state:
     *
     * Whenever multiple files are searched and a tc field occurs in the
     * requested entry, the entry it names must be found in the same file or
     * one of the succeeding files.
     *
     * However, this restriction is relaxed in ncurses; tc references to
     * previous files are permitted.
     *
     * This routine returns 1 if an entry is found, 0 if not found, and -1 if
     * the database is not accessible.
     */
    FILE *fp;
    char *tc, *termpaths[MAXPATHS];
    int filecount = 0;
    int j, k;
    bool use_buffer = FALSE;
    bool normal = TRUE;
    char tc_buf[1024];
    char pathbuf[PATH_MAX];
    char *copied = 0;
    char *cp;
    struct stat test_stat[MAXPATHS];

    termpaths[filecount] = 0;
    if (use_terminfo_vars() && (tc = getenv("TERMCAP")) != 0) {
	if (_nc_is_abs_path(tc)) {	/* interpret as a filename */
	    ADD_TC(tc, 0);
	    normal = FALSE;
	} else if (_nc_name_match(tc, tn, "|:")) {	/* treat as a capability file */
	    use_buffer = TRUE;
	    _nc_SPRINTF(tc_buf,
			_nc_SLIMIT(sizeof(tc_buf))
			"%.*s\n", (int) sizeof(tc_buf) - 2, tc);
	    normal = FALSE;
	}
    }

    if (normal) {		/* normal case */
	char envhome[PATH_MAX], *h;

	copied = strdup(get_termpath());
	for (cp = copied; *cp; cp++) {
	    if (*cp == NCURSES_PATHSEP)
		*cp = '\0';
	    else if (cp == copied || cp[-1] == '\0') {
		ADD_TC(cp, filecount);
	    }
	}

#define PRIVATE_CAP "%s/.termcap"

	if (use_terminfo_vars() && (h = getenv("HOME")) != NULL && *h != '\0'
	    && (strlen(h) + sizeof(PRIVATE_CAP)) < PATH_MAX) {
	    /* user's .termcap, if any, should override it */
	    _nc_STRCPY(envhome, h, sizeof(envhome));
	    _nc_SPRINTF(pathbuf, _nc_SLIMIT(sizeof(pathbuf))
			PRIVATE_CAP, envhome);
	    ADD_TC(pathbuf, filecount);
	}
    }

    /*
     * Probably /etc/termcap is a symlink to /usr/share/misc/termcap.
     * Avoid reading the same file twice.
     */
#if HAVE_LINK
    for (j = 0; j < filecount; j++) {
	bool omit = FALSE;
	if (stat(termpaths[j], &test_stat[j]) != 0
	    || !S_ISREG(test_stat[j].st_mode)) {
	    omit = TRUE;
	} else {
	    for (k = 0; k < j; k++) {
		if (test_stat[k].st_dev == test_stat[j].st_dev
		    && test_stat[k].st_ino == test_stat[j].st_ino) {
		    omit = TRUE;
		    break;
		}
	    }
	}
	if (omit) {
	    TR(TRACE_DATABASE, ("Path %s is a duplicate", termpaths[j]));
	    for (k = j + 1; k < filecount; k++) {
		termpaths[k - 1] = termpaths[k];
		test_stat[k - 1] = test_stat[k];
	    }
	    --filecount;
	    --j;
	}
    }
#endif

    /* parse the sources */
    if (use_buffer) {
	_nc_set_source("TERMCAP");

	/*
	 * We don't suppress warning messages here.  The presumption is
	 * that since it's just a single entry, they won't be a pain.
	 */
	_nc_read_entry_source((FILE *) 0, tc_buf, FALSE, FALSE, NULLHOOK);
    } else {
	int i;

	for (i = 0; i < filecount; i++) {

	    TR(TRACE_DATABASE, ("Looking for %s in %s", tn, termpaths[i]));
	    if (_nc_access(termpaths[i], R_OK) == 0
		&& (fp = fopen(termpaths[i], "r")) != (FILE *) 0) {
		_nc_set_source(termpaths[i]);

		/*
		 * Suppress warning messages.  Otherwise you get 400 lines of
		 * crap from archaic termcap files as ncurses complains about
		 * all the obsolete capabilities.
		 */
		_nc_read_entry_source(fp, (char *) 0, FALSE, TRUE, NULLHOOK);

		(void) fclose(fp);
	    }
	}
    }
    if (copied != 0)
	free(copied);
#endif /* USE_GETCAP */

    if (_nc_head == 0)
	return (TGETENT_ERR);

    /* resolve all use references */
    _nc_resolve_uses2(TRUE, FALSE);

    /* find a terminal matching tn, if we can */
#if USE_GETCAP_CACHE
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != 0) {
	_nc_set_writedir((char *) 0);	/* note: this does a chdir */
#endif
	for_entry_list(ep) {
	    if (_nc_name_match(ep->tterm.term_names, tn, "|:")) {
		/*
		 * Make a local copy of the terminal capabilities, delinked
		 * from the list.
		 */
		*tp = ep->tterm;
		_nc_free_entry(_nc_head, &(ep->tterm));

		/*
		 * OK, now try to write the type to user's terminfo directory. 
		 * Next time he loads this, it will come through terminfo.
		 *
		 * Advantage:  Second and subsequent fetches of this entry will
		 * be very fast.
		 *
		 * Disadvantage:  After the first time a termcap type is loaded
		 * by its user, editing it in the /etc/termcap file, or in
		 * TERMCAP, or in a local ~/.termcap, will be ineffective
		 * unless the terminfo entry is explicitly removed.
		 */
#if USE_GETCAP_CACHE
		(void) _nc_write_entry(tp);
#endif
		found = TGETENT_YES;
		break;
	    }
	}
#if USE_GETCAP_CACHE
	chdir(cwd_buf);
    }
#endif

    return (found);
}
#else
extern
NCURSES_EXPORT(void)
_nc_read_termcap(void);
NCURSES_EXPORT(void)
_nc_read_termcap(void)
{
}
#endif /* PURE_TERMINFO */
