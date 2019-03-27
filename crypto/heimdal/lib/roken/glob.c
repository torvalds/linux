/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guido van Rossum.
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

/*
 * glob(3) -- a superset of the one defined in POSIX 1003.2.
 *
 * The [!...] convention to negate a range is supported (SysV, Posix, ksh).
 *
 * Optional extra services, controlled by flags not defined by POSIX:
 *
 * GLOB_QUOTE:
 *	Escaping convention: \ inhibits any special meaning the following
 *	character might have (except \ at end of string is retained).
 * GLOB_MAGCHAR:
 *	Set in gl_flags if pattern contained a globbing character.
 * GLOB_NOMAGIC:
 *	Same as GLOB_NOCHECK, but it will only append pattern if it did
 *	not contain any magic characters.  [Used in csh style globbing]
 * GLOB_ALTDIRFUNC:
 *	Use alternately specified directory access functions.
 * GLOB_TILDE:
 *	expand ~user/foo to the /home/dir/of/user/foo
 * GLOB_BRACE:
 *	expand {1,2}{a,b} to 1a 1b 2a 2b
 * gl_matchc:
 *	Number of matches in the current invocation of glob.
 */

#include <config.h>

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include <ctype.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <errno.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include "glob.h"
#include "roken.h"

#ifndef ARG_MAX
#define ARG_MAX _POSIX_ARG_MAX
#endif

#define	CHAR_DOLLAR		'$'
#define	CHAR_DOT		'.'
#define	CHAR_EOS		'\0'
#define	CHAR_LBRACKET		'['
#define	CHAR_NOT		'!'
#define	CHAR_QUESTION		'?'
#define	CHAR_QUOTE		'\\'
#define	CHAR_RANGE		'-'
#define	CHAR_RBRACKET		']'
#define	CHAR_SEP		'/'
#define	CHAR_STAR		'*'
#define	CHAR_TILDE		'~'
#define	CHAR_UNDERSCORE		'_'
#define	CHAR_LBRACE		'{'
#define	CHAR_RBRACE		'}'
#define	CHAR_SLASH		'/'
#define	CHAR_COMMA		','

#ifndef DEBUG

#define	M_QUOTE		0x8000
#define	M_PROTECT	0x4000
#define	M_MASK		0xffff
#define	M_ASCII		0x00ff

typedef u_short Char;

#else

#define	M_QUOTE		0x80
#define	M_PROTECT	0x40
#define	M_MASK		0xff
#define	M_ASCII		0x7f

typedef char Char;

#endif


#define	CHAR(c)		((Char)((c)&M_ASCII))
#define	META(c)		((Char)((c)|M_QUOTE))
#define	M_ALL		META('*')
#define	M_END		META(']')
#define	M_NOT		META('!')
#define	M_ONE		META('?')
#define	M_RNG		META('-')
#define	M_SET		META('[')
#define	ismeta(c)	(((c)&M_QUOTE) != 0)


static int	 compare (const void *, const void *);
static void	 g_Ctoc (const Char *, char *);
static int	 g_lstat (Char *, struct stat *, glob_t *);
static DIR	*g_opendir (Char *, glob_t *);
static Char	*g_strchr (const Char *, int);
#ifdef notdef
static Char	*g_strcat (Char *, const Char *);
#endif
static int	 g_stat (Char *, struct stat *, glob_t *);
static int	 glob0 (const Char *, glob_t *);
static int	 glob1 (Char *, glob_t *, size_t *);
static int	 glob2 (Char *, Char *, Char *, glob_t *, size_t *);
static int	 glob3 (Char *, Char *, Char *, Char *, glob_t *, size_t *);
static int	 globextend (const Char *, glob_t *, size_t *);
static const Char *	 globtilde (const Char *, Char *, glob_t *);
static int	 globexp1 (const Char *, glob_t *);
static int	 globexp2 (const Char *, const Char *, glob_t *, int *);
static int	 match (Char *, Char *, Char *);
#ifdef DEBUG
static void	 qprintf (const char *, Char *);
#endif

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
glob(const char *pattern,
     int flags,
     int (*errfunc)(const char *, int),
     glob_t *pglob)
{
	const u_char *patnext;
	int c;
	Char *bufnext, *bufend, patbuf[MaxPathLen+1];

	patnext = (const u_char *) pattern;
	if (!(flags & GLOB_APPEND)) {
		pglob->gl_pathc = 0;
		pglob->gl_pathv = NULL;
		if (!(flags & GLOB_DOOFFS))
			pglob->gl_offs = 0;
	}
	pglob->gl_flags = flags & ~GLOB_MAGCHAR;
	pglob->gl_errfunc = errfunc;
	pglob->gl_matchc = 0;

	bufnext = patbuf;
	bufend = bufnext + MaxPathLen;
	if (flags & GLOB_QUOTE) {
		/* Protect the quoted characters. */
		while (bufnext < bufend && (c = *patnext++) != CHAR_EOS)
			if (c == CHAR_QUOTE) {
				if ((c = *patnext++) == CHAR_EOS) {
					c = CHAR_QUOTE;
					--patnext;
				}
				*bufnext++ = c | M_PROTECT;
			}
			else
				*bufnext++ = c;
	}
	else
	    while (bufnext < bufend && (c = *patnext++) != CHAR_EOS)
		    *bufnext++ = c;
	*bufnext = CHAR_EOS;

	if (flags & GLOB_BRACE)
	    return globexp1(patbuf, pglob);
	else
	    return glob0(patbuf, pglob);
}

/*
 * Expand recursively a glob {} pattern. When there is no more expansion
 * invoke the standard globbing routine to glob the rest of the magic
 * characters
 */
static int globexp1(const Char *pattern, glob_t *pglob)
{
	const Char* ptr = pattern;
	int rv;

	/* Protect a single {}, for find(1), like csh */
	if (pattern[0] == CHAR_LBRACE && pattern[1] == CHAR_RBRACE && pattern[2] == CHAR_EOS)
		return glob0(pattern, pglob);

	while ((ptr = (const Char *) g_strchr(ptr, CHAR_LBRACE)) != NULL)
		if (!globexp2(ptr, pattern, pglob, &rv))
			return rv;

	return glob0(pattern, pglob);
}


/*
 * Recursive brace globbing helper. Tries to expand a single brace.
 * If it succeeds then it invokes globexp1 with the new pattern.
 * If it fails then it tries to glob the rest of the pattern and returns.
 */
static int globexp2(const Char *ptr, const Char *pattern,
		    glob_t *pglob, int *rv)
{
	int     i;
	Char   *lm, *ls;
	const Char *pe, *pm, *pl;
	Char    patbuf[MaxPathLen + 1];

	/* copy part up to the brace */
	for (lm = patbuf, pm = pattern; pm != ptr; *lm++ = *pm++)
		continue;
	ls = lm;

	/* Find the balanced brace */
	for (i = 0, pe = ++ptr; *pe; pe++)
		if (*pe == CHAR_LBRACKET) {
			/* Ignore everything between [] */
			for (pm = pe++; *pe != CHAR_RBRACKET && *pe != CHAR_EOS; pe++)
				continue;
			if (*pe == CHAR_EOS) {
				/*
				 * We could not find a matching CHAR_RBRACKET.
				 * Ignore and just look for CHAR_RBRACE
				 */
				pe = pm;
			}
		}
		else if (*pe == CHAR_LBRACE)
			i++;
		else if (*pe == CHAR_RBRACE) {
			if (i == 0)
				break;
			i--;
		}

	/* Non matching braces; just glob the pattern */
	if (i != 0 || *pe == CHAR_EOS) {
		*rv = glob0(patbuf, pglob);
		return 0;
	}

	for (i = 0, pl = pm = ptr; pm <= pe; pm++)
		switch (*pm) {
		case CHAR_LBRACKET:
			/* Ignore everything between [] */
			for (pl = pm++; *pm != CHAR_RBRACKET && *pm != CHAR_EOS; pm++)
				continue;
			if (*pm == CHAR_EOS) {
				/*
				 * We could not find a matching CHAR_RBRACKET.
				 * Ignore and just look for CHAR_RBRACE
				 */
				pm = pl;
			}
			break;

		case CHAR_LBRACE:
			i++;
			break;

		case CHAR_RBRACE:
			if (i) {
			    i--;
			    break;
			}
			/* FALLTHROUGH */
		case CHAR_COMMA:
			if (i && *pm == CHAR_COMMA)
				break;
			else {
				/* Append the current string */
				for (lm = ls; (pl < pm); *lm++ = *pl++)
					continue;
				/*
				 * Append the rest of the pattern after the
				 * closing brace
				 */
				for (pl = pe + 1; (*lm++ = *pl++) != CHAR_EOS;)
					continue;

				/* Expand the current pattern */
#ifdef DEBUG
				qprintf("globexp2:", patbuf);
#endif
				*rv = globexp1(patbuf, pglob);

				/* move after the comma, to the next string */
				pl = pm + 1;
			}
			break;

		default:
			break;
		}
	*rv = 0;
	return 0;
}



/*
 * expand tilde from the passwd file.
 */
static const Char *
globtilde(const Char *pattern, Char *patbuf, glob_t *pglob)
{
	struct passwd *pwd;
	char *h;
	const Char *p;
	Char *b;

	if (*pattern != CHAR_TILDE || !(pglob->gl_flags & GLOB_TILDE))
		return pattern;

	/* Copy up to the end of the string or / */
	for (p = pattern + 1, h = (char *) patbuf; *p && *p != CHAR_SLASH;
	     *h++ = *p++)
		continue;

	*h = CHAR_EOS;

	if (((char *) patbuf)[0] == CHAR_EOS) {
		/*
		 * handle a plain ~ or ~/ by expanding $HOME
		 * first and then trying the password file
		 */
		if ((h = getenv("HOME")) == NULL) {
			if ((pwd = k_getpwuid(getuid())) == NULL)
				return pattern;
			else
				h = pwd->pw_dir;
		}
	}
	else {
		/*
		 * Expand a ~user
		 */
		if ((pwd = k_getpwnam((char*) patbuf)) == NULL)
			return pattern;
		else
			h = pwd->pw_dir;
	}

	/* Copy the home directory */
	for (b = patbuf; *h; *b++ = *h++)
		continue;

	/* Append the rest of the pattern */
	while ((*b++ = *p++) != CHAR_EOS)
		continue;

	return patbuf;
}


/*
 * The main glob() routine: compiles the pattern (optionally processing
 * quotes), calls glob1() to do the real pattern matching, and finally
 * sorts the list (unless unsorted operation is requested).  Returns 0
 * if things went well, nonzero if errors occurred.  It is not an error
 * to find no matches.
 */
static int
glob0(const Char *pattern, glob_t *pglob)
{
	const Char *qpatnext;
	int c, err, oldpathc;
	Char *bufnext, patbuf[MaxPathLen+1];
	size_t limit = 0;

	qpatnext = globtilde(pattern, patbuf, pglob);
	oldpathc = pglob->gl_pathc;
	bufnext = patbuf;

	/* We don't need to check for buffer overflow any more. */
	while ((c = *qpatnext++) != CHAR_EOS) {
		switch (c) {
		case CHAR_LBRACKET:
			c = *qpatnext;
			if (c == CHAR_NOT)
				++qpatnext;
			if (*qpatnext == CHAR_EOS ||
			    g_strchr(qpatnext+1, CHAR_RBRACKET) == NULL) {
				*bufnext++ = CHAR_LBRACKET;
				if (c == CHAR_NOT)
					--qpatnext;
				break;
			}
			*bufnext++ = M_SET;
			if (c == CHAR_NOT)
				*bufnext++ = M_NOT;
			c = *qpatnext++;
			do {
				*bufnext++ = CHAR(c);
				if (*qpatnext == CHAR_RANGE &&
				    (c = qpatnext[1]) != CHAR_RBRACKET) {
					*bufnext++ = M_RNG;
					*bufnext++ = CHAR(c);
					qpatnext += 2;
				}
			} while ((c = *qpatnext++) != CHAR_RBRACKET);
			pglob->gl_flags |= GLOB_MAGCHAR;
			*bufnext++ = M_END;
			break;
		case CHAR_QUESTION:
			pglob->gl_flags |= GLOB_MAGCHAR;
			*bufnext++ = M_ONE;
			break;
		case CHAR_STAR:
			pglob->gl_flags |= GLOB_MAGCHAR;
			/* collapse adjacent stars to one,
			 * to avoid exponential behavior
			 */
			if (bufnext == patbuf || bufnext[-1] != M_ALL)
			    *bufnext++ = M_ALL;
			break;
		default:
			*bufnext++ = CHAR(c);
			break;
		}
	}
	*bufnext = CHAR_EOS;
#ifdef DEBUG
	qprintf("glob0:", patbuf);
#endif

	if ((err = glob1(patbuf, pglob, &limit)) != 0)
		return(err);

	/*
	 * If there was no match we are going to append the pattern
	 * if GLOB_NOCHECK was specified or if GLOB_NOMAGIC was specified
	 * and the pattern did not contain any magic characters
	 * GLOB_NOMAGIC is there just for compatibility with csh.
	 */
	if (pglob->gl_pathc == oldpathc &&
	    ((pglob->gl_flags & GLOB_NOCHECK) ||
	      ((pglob->gl_flags & GLOB_NOMAGIC) &&
	       !(pglob->gl_flags & GLOB_MAGCHAR))))
		return(globextend(pattern, pglob, &limit));
	else if (!(pglob->gl_flags & GLOB_NOSORT))
		qsort(pglob->gl_pathv + pglob->gl_offs + oldpathc,
		    pglob->gl_pathc - oldpathc, sizeof(char *), compare);
	return(0);
}

static int
compare(const void *p, const void *q)
{
	return(strcmp(*(char **)p, *(char **)q));
}

static int
glob1(Char *pattern, glob_t *pglob, size_t *limit)
{
	Char pathbuf[MaxPathLen+1];

	/* A null pathname is invalid -- POSIX 1003.1 sect. 2.4. */
	if (*pattern == CHAR_EOS)
		return(0);
	return(glob2(pathbuf, pathbuf, pattern, pglob, limit));
}

/*
 * The functions glob2 and glob3 are mutually recursive; there is one level
 * of recursion for each segment in the pattern that contains one or more
 * meta characters.
 */

#ifndef S_ISLNK
#if defined(S_IFLNK) && defined(S_IFMT)
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#else
#define S_ISLNK(mode) 0
#endif
#endif

static int
glob2(Char *pathbuf, Char *pathend, Char *pattern, glob_t *pglob,
      size_t *limit)
{
	struct stat sb;
	Char *p, *q;
	int anymeta;

	/*
	 * Loop over pattern segments until end of pattern or until
	 * segment with meta character found.
	 */
	for (anymeta = 0;;) {
		if (*pattern == CHAR_EOS) {		/* End of pattern? */
			*pathend = CHAR_EOS;
			if (g_lstat(pathbuf, &sb, pglob))
				return(0);

			if (((pglob->gl_flags & GLOB_MARK) &&
			    pathend[-1] != CHAR_SEP) && (S_ISDIR(sb.st_mode)
			    || (S_ISLNK(sb.st_mode) &&
			    (g_stat(pathbuf, &sb, pglob) == 0) &&
			    S_ISDIR(sb.st_mode)))) {
				*pathend++ = CHAR_SEP;
				*pathend = CHAR_EOS;
			}
			++pglob->gl_matchc;
			return(globextend(pathbuf, pglob, limit));
		}

		/* Find end of next segment, copy tentatively to pathend. */
		q = pathend;
		p = pattern;
		while (*p != CHAR_EOS && *p != CHAR_SEP) {
			if (ismeta(*p))
				anymeta = 1;
			*q++ = *p++;
		}

		if (!anymeta) {		/* No expansion, do next segment. */
			pathend = q;
			pattern = p;
			while (*pattern == CHAR_SEP)
				*pathend++ = *pattern++;
		} else			/* Need expansion, recurse. */
			return(glob3(pathbuf, pathend, pattern, p, pglob,
			    limit));
	}
	/* NOTREACHED */
}

static int
glob3(Char *pathbuf, Char *pathend, Char *pattern, Char *restpattern,
      glob_t *pglob, size_t *limit)
{
	struct dirent *dp;
	DIR *dirp;
	int err;
	char buf[MaxPathLen];

	/*
	 * The readdirfunc declaration can't be prototyped, because it is
	 * assigned, below, to two functions which are prototyped in glob.h
	 * and dirent.h as taking pointers to differently typed opaque
	 * structures.
	 */
	struct dirent *(*readdirfunc)(void *);

	*pathend = CHAR_EOS;
	errno = 0;

	if ((dirp = g_opendir(pathbuf, pglob)) == NULL) {
		/* TODO: don't call for ENOENT or ENOTDIR? */
		if (pglob->gl_errfunc) {
			g_Ctoc(pathbuf, buf);
			if (pglob->gl_errfunc(buf, errno) ||
			    pglob->gl_flags & GLOB_ERR)
				return (GLOB_ABEND);
		}
		return(0);
	}

	err = 0;

	/* Search directory for matching names. */
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		readdirfunc = pglob->gl_readdir;
	else
		readdirfunc = (struct dirent *(*)(void *))readdir;
	while ((dp = (*readdirfunc)(dirp))) {
		u_char *sc;
		Char *dc;

		/* Initial CHAR_DOT must be matched literally. */
		if (dp->d_name[0] == CHAR_DOT && *pattern != CHAR_DOT)
			continue;
		for (sc = (u_char *) dp->d_name, dc = pathend;
		     (*dc++ = *sc++) != CHAR_EOS;)
			continue;
		if (!match(pathend, pattern, restpattern)) {
			*pathend = CHAR_EOS;
			continue;
		}
		err = glob2(pathbuf, --dc, restpattern, pglob, limit);
		if (err)
			break;
	}

	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		(*pglob->gl_closedir)(dirp);
	else
		closedir(dirp);
	return(err);
}


/*
 * Extend the gl_pathv member of a glob_t structure to accomodate a new item,
 * add the new item, and update gl_pathc.
 *
 * This assumes the BSD realloc, which only copies the block when its size
 * crosses a power-of-two boundary; for v7 realloc, this would cause quadratic
 * behavior.
 *
 * Return 0 if new item added, error code if memory couldn't be allocated.
 *
 * Invariant of the glob_t structure:
 *	Either gl_pathc is zero and gl_pathv is NULL; or gl_pathc > 0 and
 *	gl_pathv points to (gl_offs + gl_pathc + 1) items.
 */
static int
globextend(const Char *path, glob_t *pglob, size_t *limit)
{
	char **pathv;
	int i;
	size_t newsize, len;
	char *copy;
	const Char *p;

	newsize = sizeof(*pathv) * (2 + pglob->gl_pathc + pglob->gl_offs);
	pathv = pglob->gl_pathv ?
		    realloc(pglob->gl_pathv, newsize) :
		    malloc(newsize);
	if (pathv == NULL)
		return(GLOB_NOSPACE);

	if (pglob->gl_pathv == NULL && pglob->gl_offs > 0) {
		/* first time around -- clear initial gl_offs items */
		pathv += pglob->gl_offs;
		for (i = pglob->gl_offs; --i >= 0; )
			*--pathv = NULL;
	}
	pglob->gl_pathv = pathv;

	for (p = path; *p++;)
		continue;
	len = (size_t)(p - path);
	*limit += len;
	if ((copy = malloc(len)) != NULL) {
		g_Ctoc(path, copy);
		pathv[pglob->gl_offs + pglob->gl_pathc++] = copy;
	}
	pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;

	if ((pglob->gl_flags & GLOB_LIMIT) && (newsize + *limit) >= ARG_MAX) {
		errno = 0;
		return(GLOB_NOSPACE);
	}

	return(copy == NULL ? GLOB_NOSPACE : 0);
}


/*
 * pattern matching function for filenames.  Each occurrence of the *
 * pattern causes a recursion level.
 */
static int
match(Char *name, Char *pat, Char *patend)
{
	int ok, negate_range;
	Char c, k;

	while (pat < patend) {
		c = *pat++;
		switch (c & M_MASK) {
		case M_ALL:
			if (pat == patend)
				return(1);
			do
			    if (match(name, pat, patend))
				    return(1);
			while (*name++ != CHAR_EOS);
			return(0);
		case M_ONE:
			if (*name++ == CHAR_EOS)
				return(0);
			break;
		case M_SET:
			ok = 0;
			if ((k = *name++) == CHAR_EOS)
				return(0);
			if ((negate_range = ((*pat & M_MASK) == M_NOT)) != CHAR_EOS)
				++pat;
			while (((c = *pat++) & M_MASK) != M_END)
				if ((*pat & M_MASK) == M_RNG) {
					if (c <= k && k <= pat[1])
						ok = 1;
					pat += 2;
				} else if (c == k)
					ok = 1;
			if (ok == negate_range)
				return(0);
			break;
		default:
			if (*name++ != c)
				return(0);
			break;
		}
	}
	return(*name == CHAR_EOS);
}

/* Free allocated data belonging to a glob_t structure. */
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
globfree(glob_t *pglob)
{
	int i;
	char **pp;

	if (pglob->gl_pathv != NULL) {
		pp = pglob->gl_pathv + pglob->gl_offs;
		for (i = pglob->gl_pathc; i--; ++pp)
			if (*pp)
				free(*pp);
		free(pglob->gl_pathv);
		pglob->gl_pathv = NULL;
	}
}

static DIR *
g_opendir(Char *str, glob_t *pglob)
{
	char buf[MaxPathLen];

	if (!*str)
		strlcpy(buf, ".", sizeof(buf));
	else
		g_Ctoc(str, buf);

	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return((*pglob->gl_opendir)(buf));

	return(opendir(buf));
}

static int
g_lstat(Char *fn, struct stat *sb, glob_t *pglob)
{
	char buf[MaxPathLen];

	g_Ctoc(fn, buf);
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return((*pglob->gl_lstat)(buf, sb));
	return(lstat(buf, sb));
}

static int
g_stat(Char *fn, struct stat *sb, glob_t *pglob)
{
	char buf[MaxPathLen];

	g_Ctoc(fn, buf);
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return((*pglob->gl_stat)(buf, sb));
	return(stat(buf, sb));
}

static Char *
g_strchr(const Char *str, int ch)
{
	do {
		if (*str == ch)
			return (Char *)str;
	} while (*str++);
	return (NULL);
}

#ifdef notdef
static Char *
g_strcat(Char *dst, const Char *src)
{
	Char *sdst = dst;

	while (*dst++)
		continue;
	--dst;
	while((*dst++ = *src++) != CHAR_EOS)
	    continue;

	return (sdst);
}
#endif

static void
g_Ctoc(const Char *str, char *buf)
{
	char *dc;

	for (dc = buf; (*dc++ = *str++) != CHAR_EOS;)
		continue;
}

#ifdef DEBUG
static void
qprintf(const Char *str, Char *s)
{
	Char *p;

	printf("%s:\n", str);
	for (p = s; *p; p++)
		printf("%c", CHAR(*p));
	printf("\n");
	for (p = s; *p; p++)
		printf("%c", *p & M_PROTECT ? '"' : ' ');
	printf("\n");
	for (p = s; *p; p++)
		printf("%c", ismeta(*p) ? '_' : ' ');
	printf("\n");
}
#endif
