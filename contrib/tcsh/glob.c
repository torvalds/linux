/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
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
#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)glob.c	5.12 (Berkeley) 6/24/91";
#endif /* LIBC_SCCS and not lint */
/*
 * Glob: the interface is a superset of the one defined in POSIX 1003.2,
 * draft 9.
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
 * GLOB_ALTNOT:
 *	Use ^ instead of ! for "not".
 * gl_matchc:
 *	Number of matches in the current invocation of glob.
 */

#ifdef WINNT_NATIVE
	#pragma warning(disable:4244)
#endif /* WINNT_NATIVE */

#define Char __Char
#include "sh.h"
#include "glob.h"

#ifndef HAVE_MBLEN
#undef mblen
#define mblen(_s,_n)	mbrlen((_s),(_n),NULL)
#endif

#undef Char
#undef QUOTE
#undef TILDE
#undef META
#undef ismeta
#undef Strchr

#ifndef S_ISDIR
#define S_ISDIR(a)	(((a) & S_IFMT) == S_IFDIR)
#endif

#if !defined(S_ISLNK) && defined(S_IFLNK)
#define S_ISLNK(a)	(((a) & S_IFMT) == S_IFLNK)
#endif

#if !defined(S_ISLNK) && !defined(lstat)
#define lstat stat
#endif

typedef unsigned short Char;

static	int	 glob1 		(Char *, glob_t *, int);
static	int	 glob2		(struct strbuf *, const Char *, glob_t *, int);
static	int	 glob3		(struct strbuf *, const Char *, const Char *,
				 const Char *, glob_t *, int);
static	void	 globextend	(const char *, glob_t *);
static	int	 match		(const char *, const Char *, const Char *,
				 int);
static	int	 compare	(const void *, const void *);
static 	DIR	*Opendir	(const char *);
#ifdef S_IFLNK
static	int	 Lstat		(const char *, struct stat *);
#endif
static	int	 Stat		(const char *, struct stat *sb);
static 	Char 	*Strchr		(Char *, int);
#ifdef DEBUG
static	void	 qprintf	(const Char *);
#endif

#define	DOLLAR		'$'
#define	DOT		'.'
#define	EOS		'\0'
#define	LBRACKET	'['
#define	NOT		'!'
#define ALTNOT		'^'
#define	QUESTION	'?'
#define	QUOTE		'\\'
#define	RANGE		'-'
#define	RBRACKET	']'
#define	SEP		'/'
#define	STAR		'*'
#define	TILDE		'~'
#define	UNDERSCORE	'_'

#define	M_META		0x8000
#define M_PROTECT	0x4000
#define	M_MASK		0xffff
#define	M_ASCII		0x00ff

#define	LCHAR(c)	((c)&M_ASCII)
#define	META(c)		((c)|M_META)
#define	M_ALL		META('*')
#define	M_END		META(']')
#define	M_NOT		META('!')
#define	M_ALTNOT	META('^')
#define	M_ONE		META('?')
#define	M_RNG		META('-')
#define	M_SET		META('[')
#define	ismeta(c)	(((c)&M_META) != 0)

int
globcharcoll(__Char c1, __Char c2, int cs)
{
#if defined(NLS) && defined(LC_COLLATE) && defined(HAVE_STRCOLL)
# if defined(WIDE_STRINGS)
    wchar_t s1[2], s2[2];

    if (c1 == c2)
	return (0);
    if (cs) {
	c1 = towlower(c1);
	c2 = towlower(c2);
    } else {
#ifndef __FreeBSD__
	/* This should not be here, but I'll rather leave it in than engage in
	   a LC_COLLATE flamewar about a shell I don't use... */
	if (iswlower(c1) && iswupper(c2))
	    return (1);
	if (iswupper(c1) && iswlower(c2))
	    return (-1);
#endif
    }
    s1[0] = c1;
    s2[0] = c2;
    s1[1] = s2[1] = '\0';
    return wcscoll(s1, s2);
# else /* not WIDE_STRINGS */
    char s1[2], s2[2];

    if (c1 == c2)
	return (0);
    /*
     * From kevin lyda <kevin@suberic.net>:
     * strcoll does not guarantee case sorting, so we pre-process now:
     */
    if (cs) {
	c1 = islower(c1) ? c1 : tolower(c1);
	c2 = islower(c2) ? c2 : tolower(c2);
    } else {
	if (islower(c1) && isupper(c2))
	    return (1);
	if (isupper(c1) && islower(c2))
	    return (-1);
    }
    s1[0] = c1;
    s2[0] = c2;
    s1[1] = s2[1] = '\0';
    return strcoll(s1, s2);
# endif
#else
    return (c1 - c2);
#endif
}

/*
 * Need to dodge two kernel bugs:
 * opendir("") != opendir(".")
 * NAMEI_BUG: on plain files trailing slashes are ignored in some kernels.
 *            POSIX specifies that they should be ignored in directories.
 */

static DIR *
Opendir(const char *str)
{
#if defined(hpux) || defined(__hpux)
    struct stat st;
#endif

    if (!*str)
	return (opendir("."));
#if defined(hpux) || defined(__hpux)
    /*
     * Opendir on some device files hangs, so avoid it
     */
    if (stat(str, &st) == -1 || !S_ISDIR(st.st_mode))
	return NULL;
#endif
    return opendir(str);
}

#ifdef S_IFLNK
static int
Lstat(const char *fn, struct stat *sb)
{
    int st;

    st = lstat(fn, sb);
# ifdef NAMEI_BUG
    if (*fn != 0 && strend(fn)[-1] == '/' && !S_ISDIR(sb->st_mode))
	st = -1;
# endif	/* NAMEI_BUG */
    return st;
}
#else
#define Lstat Stat
#endif /* S_IFLNK */

static int
Stat(const char *fn, struct stat *sb)
{
    int st;

    st = stat(fn, sb);
#ifdef NAMEI_BUG
    if (*fn != 0 && strend(fn)[-1] == '/' && !S_ISDIR(sb->st_mode))
	st = -1;
#endif /* NAMEI_BUG */
    return st;
}

static Char *
Strchr(Char *str, int ch)
{
    do
	if (*str == ch)
	    return (str);
    while (*str++);
    return (NULL);
}

#ifdef DEBUG
static void
qprintf(const Char *s)
{
    const Char *p;

    for (p = s; *p; p++)
	printf("%c", *p & 0xff);
    printf("\n");
    for (p = s; *p; p++)
	printf("%c", *p & M_PROTECT ? '"' : ' ');
    printf("\n");
    for (p = s; *p; p++)
	printf("%c", *p & M_META ? '_' : ' ');
    printf("\n");
}
#endif /* DEBUG */

static int
compare(const void *p, const void *q)
{
#if defined(NLS) && defined(HAVE_STRCOLL)
    return (strcoll(*(char *const *) p, *(char *const *) q));
#else
    return (strcmp(*(char *const *) p, *(char *const *) q));
#endif /* NLS && HAVE_STRCOLL */
}

/*
 * The main glob() routine: compiles the pattern (optionally processing
 * quotes), calls glob1() to do the real pattern matching, and finally
 * sorts the list (unless unsorted operation is requested).  Returns 0
 * if things went well, nonzero if errors occurred.  It is not an error
 * to find no matches.
 */
int
glob(const char *pattern, int flags, int (*errfunc) (const char *, int),
     glob_t *pglob)
{
    int     err, oldpathc;
    Char *bufnext, m_not;
    const unsigned char *patnext;
    int     c, not;
    Char *qpatnext, *patbuf;
    int     no_match;

    patnext = (const unsigned char *) pattern;
    if (!(flags & GLOB_APPEND)) {
	pglob->gl_pathc = 0;
	pglob->gl_pathv = NULL;
	if (!(flags & GLOB_DOOFFS))
	    pglob->gl_offs = 0;
    }
    pglob->gl_flags = flags & ~GLOB_MAGCHAR;
    pglob->gl_errfunc = errfunc;
    oldpathc = pglob->gl_pathc;
    pglob->gl_matchc = 0;

    if (pglob->gl_flags & GLOB_ALTNOT) {
	not = ALTNOT;
	m_not = M_ALTNOT;
    }
    else {
	not = NOT;
	m_not = M_NOT;
    }

    patbuf = xmalloc((strlen(pattern) + 1) * sizeof(*patbuf));
    bufnext = patbuf;

    no_match = *patnext == not;
    if (no_match)
	patnext++;

    if (flags & GLOB_QUOTE) {
	/* Protect the quoted characters */
	while ((c = *patnext++) != EOS) {
#ifdef WIDE_STRINGS
	    int len;
	    
	    len = mblen((const char *)(patnext - 1), MB_LEN_MAX);
	    if (len == -1)
		TCSH_IGNORE(mblen(NULL, 0));
	    else if (len > 1) {
		*bufnext++ = (Char) c;
		while (--len != 0)
		    *bufnext++ = (Char) (*patnext++ | M_PROTECT);
	    } else
#endif /* WIDE_STRINGS */
	    if (c == QUOTE) {
		if ((c = *patnext++) == EOS) {
		    c = QUOTE;
		    --patnext;
		}
		*bufnext++ = (Char) (c | M_PROTECT);
	    }
	    else
		*bufnext++ = (Char) c;
	}
    }
    else
	while ((c = *patnext++) != EOS)
	    *bufnext++ = (Char) c;
    *bufnext = EOS;

    bufnext = patbuf;
    qpatnext = patbuf;
    while ((c = *qpatnext++) != EOS) {
	switch (c) {
	case LBRACKET:
	    c = *qpatnext;
	    if (c == not)
		++qpatnext;
	    if (*qpatnext == EOS ||
		Strchr(qpatnext + 1, RBRACKET) == NULL) {
		*bufnext++ = LBRACKET;
		if (c == not)
		    --qpatnext;
		break;
	    }
	    pglob->gl_flags |= GLOB_MAGCHAR;
	    *bufnext++ = M_SET;
	    if (c == not)
		*bufnext++ = m_not;
	    c = *qpatnext++;
	    do {
		*bufnext++ = LCHAR(c);
		if (*qpatnext == RANGE &&
		    (c = qpatnext[1]) != RBRACKET) {
		    *bufnext++ = M_RNG;
		    *bufnext++ = LCHAR(c);
		    qpatnext += 2;
		}
	    } while ((c = *qpatnext++) != RBRACKET);
	    *bufnext++ = M_END;
	    break;
	case QUESTION:
	    pglob->gl_flags |= GLOB_MAGCHAR;
	    *bufnext++ = M_ONE;
	    break;
	case STAR:
	    pglob->gl_flags |= GLOB_MAGCHAR;
	    /* collapse adjacent stars to one [or three if globstar],
	     * to avoid exponential behavior
	     */
	    if (bufnext == patbuf || bufnext[-1] != M_ALL ||
	       ((flags & GLOB_STAR) != 0 && 
		 (bufnext - 1 == patbuf || bufnext[-2] != M_ALL ||
		 bufnext - 2 == patbuf || bufnext[-3] != M_ALL)))
		*bufnext++ = M_ALL;
	    break;
	default:
	    *bufnext++ = LCHAR(c);
	    break;
	}
    }
    *bufnext = EOS;
#ifdef DEBUG
    qprintf(patbuf);
#endif

    if ((err = glob1(patbuf, pglob, no_match)) != 0) {
	xfree(patbuf);
	return (err);
    }

    /*
     * If there was no match we are going to append the pattern 
     * if GLOB_NOCHECK was specified or if GLOB_NOMAGIC was specified
     * and the pattern did not contain any magic characters
     * GLOB_NOMAGIC is there just for compatibility with csh.
     */
    if (pglob->gl_pathc == oldpathc && 
	((flags & GLOB_NOCHECK) || 
	 ((flags & GLOB_NOMAGIC) && !(pglob->gl_flags & GLOB_MAGCHAR)))) {
	if (!(flags & GLOB_QUOTE))
	    globextend(pattern, pglob);
	else {
	    char *copy, *dest;
	    const char *src;

	    /* copy pattern, interpreting quotes */
	    copy = xmalloc(strlen(pattern) + 1);
	    dest = copy;
	    src = pattern;
	    while (*src != EOS) {
		/* Don't interpret quotes. The spec does not say we should do */
		if (*src == QUOTE) {
		    if (*++src == EOS)
			--src;
		}
		*dest++ = *src++;
	    }
	    *dest = EOS;
	    globextend(copy, pglob);
	    xfree(copy);
	}
	xfree(patbuf);
	return 0;
    }
    else if (!(flags & GLOB_NOSORT) && (pglob->gl_pathc != oldpathc))
	qsort(pglob->gl_pathv + pglob->gl_offs + oldpathc,
	      pglob->gl_pathc - oldpathc, sizeof(char *), compare);
    xfree(patbuf);
    return (0);
}

static int
glob1(Char *pattern, glob_t *pglob, int no_match)
{
    struct strbuf pathbuf = strbuf_INIT;
    int err;

    /*
     * a null pathname is invalid -- POSIX 1003.1 sect. 2.4.
     */
    if (*pattern == EOS)
	return (0);
    err = glob2(&pathbuf, pattern, pglob, no_match);
    xfree(pathbuf.s);
    return err;
}

/*
 * functions glob2 and glob3 are mutually recursive; there is one level
 * of recursion for each segment in the pattern that contains one or
 * more meta characters.
 */
static int
glob2(struct strbuf *pathbuf, const Char *pattern, glob_t *pglob, int no_match)
{
    struct stat sbuf;
    int anymeta;
    const Char *p;
    size_t orig_len;

    /*
     * loop over pattern segments until end of pattern or until segment with
     * meta character found.
     */
    anymeta = 0;
    for (;;) {
	if (*pattern == EOS) {	/* end of pattern? */
	    strbuf_terminate(pathbuf);

	    if (Lstat(pathbuf->s, &sbuf))
		return (0);

	    if (((pglob->gl_flags & GLOB_MARK) &&
		 pathbuf->s[pathbuf->len - 1] != SEP) &&
		(S_ISDIR(sbuf.st_mode)
#ifdef S_IFLNK
		 || (S_ISLNK(sbuf.st_mode) &&
		     (Stat(pathbuf->s, &sbuf) == 0) &&
		     S_ISDIR(sbuf.st_mode))
#endif
		 )) {
		strbuf_append1(pathbuf, SEP);
		strbuf_terminate(pathbuf);
	    }
	    ++pglob->gl_matchc;
	    globextend(pathbuf->s, pglob);
	    return 0;
	}

	/* find end of next segment, tentatively copy to pathbuf */
	p = pattern;
	orig_len = pathbuf->len;
	while (*p != EOS && *p != SEP) {
	    if (ismeta(*p))
		anymeta = 1;
	    strbuf_append1(pathbuf, *p++);
	}

	if (!anymeta) {		/* no expansion, do next segment */
	    pattern = p;
	    while (*pattern == SEP)
		strbuf_append1(pathbuf, *pattern++);
	}
	else {			/* need expansion, recurse */
	    pathbuf->len = orig_len;
	    return (glob3(pathbuf, pattern, p, pattern, pglob, no_match));
	}
    }
    /* NOTREACHED */
}

static size_t
One_Char_mbtowc(__Char *pwc, const Char *s, size_t n)
{
#ifdef WIDE_STRINGS
    char buf[MB_LEN_MAX], *p;

    if (n > MB_LEN_MAX)
	n = MB_LEN_MAX;
    p = buf;
    while (p < buf + n && (*p++ = LCHAR(*s++)) != 0)
	;
    return one_mbtowc(pwc, buf, n);
#else
    *pwc = *s & CHAR;
    return 1;
#endif
}
 
static int
glob3(struct strbuf *pathbuf, const Char *pattern, const Char *restpattern,
      const Char *pglobstar, glob_t *pglob, int no_match)
{
    DIR    *dirp;
    struct dirent *dp;
    struct stat sbuf;
    int     err;
    Char m_not = (pglob->gl_flags & GLOB_ALTNOT) ? M_ALTNOT : M_NOT;
    size_t orig_len;
    int globstar = 0;
    int chase_symlinks = 0;
    const Char *termstar = NULL;

    strbuf_terminate(pathbuf);
    orig_len = pathbuf->len;
    errno = err = 0;

    while (pglobstar < restpattern) {
	__Char wc;
	size_t width = One_Char_mbtowc(&wc, pglobstar, MB_LEN_MAX);
	if ((pglobstar[0] & M_MASK) == M_ALL &&
	    (pglobstar[width] & M_MASK) == M_ALL) {
	    globstar = 1;
	    chase_symlinks = (pglobstar[2 * width] & M_MASK) == M_ALL;
	    termstar = pglobstar + (2 + chase_symlinks) * width;
	    break;
	}
        pglobstar += width;
    } 

    if (globstar) {
	err = pglobstar==pattern && termstar==restpattern ?
		*restpattern == EOS ?
		glob2(pathbuf, restpattern - 1, pglob, no_match) :
		glob2(pathbuf, restpattern + 1, pglob, no_match) :
		glob3(pathbuf, pattern, restpattern, termstar, pglob, no_match);
	if (err)
	    return err;
	pathbuf->len = orig_len;
	strbuf_terminate(pathbuf);
    }

    if (*pathbuf->s && (Lstat(pathbuf->s, &sbuf) || !S_ISDIR(sbuf.st_mode)
#ifdef S_IFLINK
	     && ((globstar && !chase_symlinks) || !S_ISLNK(sbuf.st_mode))
#endif
	))
	return 0;

    if (!(dirp = Opendir(pathbuf->s))) {
	/* todo: don't call for ENOENT or ENOTDIR? */
	if ((pglob->gl_errfunc && (*pglob->gl_errfunc) (pathbuf->s, errno)) ||
	    (pglob->gl_flags & GLOB_ERR))
	    return (GLOB_ABEND);
	else
	    return (0);
    }

    /* search directory for matching names */
    while ((dp = readdir(dirp)) != NULL) {
	/* initial DOT must be matched literally */
	if (dp->d_name[0] == DOT && *pattern != DOT)
	    if (!(pglob->gl_flags & GLOB_DOT) || !dp->d_name[1] ||
		(dp->d_name[1] == DOT && !dp->d_name[2]))
		continue; /*unless globdot and not . or .. */
	pathbuf->len = orig_len;
	strbuf_append(pathbuf, dp->d_name);
	strbuf_terminate(pathbuf);

	if (globstar) {
#ifdef S_IFLNK
	    if (!chase_symlinks &&
		(Lstat(pathbuf->s, &sbuf) || S_ISLNK(sbuf.st_mode)))
		    continue;
#endif
	    if (match(pathbuf->s + orig_len, pattern, termstar,
		(int)m_not) == no_match) 
		    continue;
	    strbuf_append1(pathbuf, SEP);
	    strbuf_terminate(pathbuf);
	    if ((err = glob2(pathbuf, pglobstar, pglob, no_match)) != 0)
		break;
	} else {
	    if (match(pathbuf->s + orig_len, pattern, restpattern,
		(int) m_not) == no_match)
		continue;
	    if ((err = glob2(pathbuf, restpattern, pglob, no_match)) != 0)
		break;
	}
    }
    /* todo: check error from readdir? */
    closedir(dirp);
    return (err);
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
 *	 gl_pathv points to (gl_offs + gl_pathc + 1) items.
 */
static void
globextend(const char *path, glob_t *pglob)
{
    char **pathv;
    int i;
    size_t newsize;

    newsize = sizeof(*pathv) * (2 + pglob->gl_pathc + pglob->gl_offs);
    pathv = xrealloc(pglob->gl_pathv, newsize);

    if (pglob->gl_pathv == NULL && pglob->gl_offs > 0) {
	/* first time around -- clear initial gl_offs items */
	pathv += pglob->gl_offs;
	for (i = pglob->gl_offs; --i >= 0;)
	    *--pathv = NULL;
    }
    pglob->gl_pathv = pathv;

    pathv[pglob->gl_offs + pglob->gl_pathc++] = strsave(path);
    pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;
}

/*
 * pattern matching function for filenames.  Each occurrence of the *
 * pattern causes a recursion level.
 */
static  int
match(const char *name, const Char *pat, const Char *patend, int m_not)
{
    int ok, negate_range;
    Char c;

    while (pat < patend) {
	size_t lwk;
	__Char wc, wk;

	c = *pat; /* Only for M_MASK bits */
	pat += One_Char_mbtowc(&wc, pat, MB_LEN_MAX);
	lwk = one_mbtowc(&wk, name, MB_LEN_MAX);
	switch (c & M_MASK) {
	case M_ALL:
	    while (pat < patend && (*pat & M_MASK) == M_ALL)  /* eat consecutive '*' */
		pat += One_Char_mbtowc(&wc, pat, MB_LEN_MAX);
	    if (pat == patend)
	        return (1);
	    while (!match(name, pat, patend, m_not)) {
		if (*name == EOS)
		    return (0);
		name += lwk;
		lwk = one_mbtowc(&wk, name, MB_LEN_MAX);
	    }
	    return (1);
	case M_ONE:
	    if (*name == EOS)
		return (0);
	    name += lwk;
	    break;
	case M_SET:
	    ok = 0;
	    if (*name == EOS)
		return (0);
	    name += lwk;
	    if ((negate_range = ((*pat & M_MASK) == m_not)) != 0)
		++pat;
	    while ((*pat & M_MASK) != M_END) {
		pat += One_Char_mbtowc(&wc, pat, MB_LEN_MAX);
		if ((*pat & M_MASK) == M_RNG) {
		    __Char wc2;

		    pat++;
		    pat += One_Char_mbtowc(&wc2, pat, MB_LEN_MAX);
		    if (globcharcoll(wc, wk, 0) <= 0 &&
			globcharcoll(wk, wc2, 0) <= 0)
			ok = 1;
		} else if (wc == wk)
		    ok = 1;
	    }
	    pat += One_Char_mbtowc(&wc, pat, MB_LEN_MAX);
	    if (ok == negate_range)
		return (0);
	    break;
	default:
	    if (*name == EOS || samecase(wk) != samecase(wc))
		return (0);
	    name += lwk;
	    break;
	}
    }
    return (*name == EOS);
}

/* free allocated data belonging to a glob_t structure */
void
globfree(glob_t *pglob)
{
    int i;
    char **pp;

    if (pglob->gl_pathv != NULL) {
	pp = pglob->gl_pathv + pglob->gl_offs;
	for (i = pglob->gl_pathc; i--; ++pp)
	    if (*pp)
		xfree(*pp), *pp = NULL;
	xfree(pglob->gl_pathv), pglob->gl_pathv = NULL;
    }
}
