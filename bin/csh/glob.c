/*	$OpenBSD: glob.c,v 1.23 2018/09/08 01:28:39 miko Exp $	*/
/*	$NetBSD: glob.c,v 1.10 1995/03/21 09:03:01 cgd Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
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

#include <sys/types.h>
#include <glob.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>

#include "csh.h"
#include "extern.h"

static int noglob;
static int pargsiz, gargsiz;

/*
 * Values for gflag
 */
#define	G_NONE	0		/* No globbing needed			*/
#define	G_GLOB	1		/* string contains *?[] characters	*/
#define	G_CSH	2		/* string contains ~`{ characters	*/

#define	GLOBSPACE	100	/* Alloc increment			*/

#define LBRC '{'
#define RBRC '}'
#define LBRK '['
#define RBRK ']'
#define EOS '\0'

Char  **gargv = NULL;
long    gargc = 0;
Char  **pargv = NULL;
long    pargc = 0;

/*
 * globbing is now done in two stages. In the first pass we expand
 * csh globbing idioms ~`{ and then we proceed doing the normal
 * globbing if needed ?*[
 *
 * Csh type globbing is handled in globexpand() and the rest is
 * handled in glob() which is part of the 4.4BSD libc.
 *
 */
static Char	*globtilde(Char **, Char *);
static Char	**libglob(Char **);
static Char	**globexpand(Char **);
static int	globbrace(Char *, Char *, Char ***);
static void	expbrace(Char ***, Char ***, int);
static int	pmatch(Char *, Char *);
static void	pword(void);
static void	psave(int);
static void	backeval(Char *, bool);


static Char *
globtilde(Char **nv, Char *s)
{
    Char    gbuf[PATH_MAX], *gstart, *b, *u, *e;

    gstart = gbuf;
    *gstart++ = *s++;
    u = s;
    for (b = gstart, e = &gbuf[PATH_MAX - 1];
	 *s && *s != '/' && *s != ':' && b < e;
	 *b++ = *s++)
	 continue;
    *b = EOS;
    if (gethdir(gstart, &gbuf[sizeof(gbuf)/sizeof(Char)] - gstart)) {
	blkfree(nv);
	if (*gstart)
	    stderror(ERR_UNKUSER, vis_str(gstart));
	else
	    stderror(ERR_NOHOME);
    }
    b = &gstart[Strlen(gstart)];
    while (*s)
	*b++ = *s++;
    *b = EOS;
    --u;
    free(u);
    return (Strsave(gstart));
}

static int
globbrace(Char *s, Char *p, Char ***bl)
{
    int     i, len;
    Char   *pm, *pe, *lm, *pl;
    Char  **nv, **vl;
    Char    gbuf[PATH_MAX];
    int     size = GLOBSPACE;

    nv = vl = xreallocarray(NULL, size, sizeof(Char *));
    *vl = NULL;

    len = 0;
    /* copy part up to the brace */
    for (lm = gbuf, p = s; *p != LBRC; *lm++ = *p++)
	continue;

    /* check for balanced braces */
    for (i = 0, pe = ++p; *pe; pe++)
	if (*pe == LBRK) {
	    /* Ignore everything between [] */
	    for (++pe; *pe != RBRK && *pe != EOS; pe++)
		continue;
	    if (*pe == EOS) {
		blkfree(nv);
		return (-RBRK);
	    }
	}
	else if (*pe == LBRC)
	    i++;
	else if (*pe == RBRC) {
	    if (i == 0)
		break;
	    i--;
	}

    if (i != 0 || *pe == '\0') {
	blkfree(nv);
	return (-RBRC);
    }

    for (i = 0, pl = pm = p; pm <= pe; pm++)
	switch (*pm) {
	case LBRK:
	    for (++pm; *pm != RBRK && *pm != EOS; pm++)
		continue;
	    if (*pm == EOS) {
		*vl = NULL;
		blkfree(nv);
		return (-RBRK);
	    }
	    break;
	case LBRC:
	    i++;
	    break;
	case RBRC:
	    if (i) {
		i--;
		break;
	    }
	    /* FALLTHROUGH */
	case ',':
	    if (i && *pm == ',')
		break;
	    else {
		Char    savec = *pm;

		*pm = EOS;
		(void) Strlcpy(lm, pl, &gbuf[sizeof(gbuf)/sizeof(Char)] - lm);
		(void) Strlcat(gbuf, pe + 1, PATH_MAX);
		*pm = savec;
		*vl++ = Strsave(gbuf);
		len++;
		pl = pm + 1;
		if (vl == &nv[size]) {
		    size += GLOBSPACE;
		    nv = xreallocarray(nv, size, sizeof(Char *));
		    vl = &nv[size - GLOBSPACE];
		}
	    }
	    break;
	default:
	    break;
	}
    *vl = NULL;
    *bl = nv;
    return (len);
}


static void
expbrace(Char ***nvp, Char ***elp, int size)
{
    Char **vl, **el, **nv, *s;

    vl = nv = *nvp;
    if (elp != NULL)
	el = *elp;
    else
	for (el = vl; *el; el++)
	    continue;

    for (s = *vl; s; s = *++vl) {
	Char   *b;
	Char  **vp, **bp;

	/* leave {} untouched for find */
	if (s[0] == '{' && (s[1] == '\0' || (s[1] == '}' && s[2] == '\0')))
	    continue;
	if ((b = Strchr(s, '{')) != NULL) {
	    Char  **bl;
	    int     len;

	    if ((len = globbrace(s, b, &bl)) < 0) {
		free(nv);
		stderror(ERR_MISSING, -len);
	    }
	    free(s);
	    if (len == 1) {
		*vl-- = *bl;
		free(bl);
		continue;
	    }
	    len = blklen(bl);
	    if (&el[len] >= &nv[size]) {
		int     l, e;

		l = &el[len] - &nv[size];
		size += GLOBSPACE > l ? GLOBSPACE : l;
		l = vl - nv;
		e = el - nv;
		nv = xreallocarray(nv, size, sizeof(Char *));
		vl = nv + l;
		el = nv + e;
	    }
	    vp = vl--;
	    *vp = *bl;
	    len--;
	    for (bp = el; bp != vp; bp--)
		bp[len] = *bp;
	    el += len;
	    vp++;
	    for (bp = bl + 1; *bp; *vp++ = *bp++)
		continue;
	    free(bl);
	}

    }
    if (elp != NULL)
	*elp = el;
    *nvp = nv;
}

static Char **
globexpand(Char **v)
{
    Char   *s;
    Char  **nv, **vl, **el;
    int     size = GLOBSPACE;


    nv = vl = xreallocarray(NULL, size, sizeof(Char *));
    *vl = NULL;

    /*
     * Step 1: expand backquotes.
     */
    while ((s = *v++) != NULL) {
	if (Strchr(s, '`')) {
	    int     i;

	    (void) dobackp(s, 0);
	    for (i = 0; i < pargc; i++) {
		*vl++ = pargv[i];
		if (vl == &nv[size]) {
		    size += GLOBSPACE;
		    nv = xreallocarray(nv, size, sizeof(Char *));
		    vl = &nv[size - GLOBSPACE];
		}
	    }
	    free(pargv);
	    pargv = NULL;
	}
	else {
	    *vl++ = Strsave(s);
	    if (vl == &nv[size]) {
		size += GLOBSPACE;
		nv = xreallocarray(nv, size, sizeof(Char *));
		vl = &nv[size - GLOBSPACE];
	    }
	}
    }
    *vl = NULL;

    if (noglob)
	return (nv);

    /*
     * Step 2: expand braces
     */
    el = vl;
    expbrace(&nv, &el, size);

    /*
     * Step 3: expand ~
     */
    vl = nv;
    for (s = *vl; s; s = *++vl)
	if (*s == '~')
	    *vl = globtilde(nv, s);
    vl = nv;
    return (vl);
}

static Char *
handleone(Char *str, Char **vl, int action)
{

    Char   *cp, **vlp = vl;

    switch (action) {
    case G_ERROR:
	setname(vis_str(str));
	blkfree(vl);
	stderror(ERR_NAME | ERR_AMBIG);
	break;
    case G_APPEND:
	trim(vlp);
	str = Strsave(*vlp++);
	do {
	    cp = Strspl(str, STRspace);
	    free(str);
	    str = Strspl(cp, *vlp);
	    free(cp);
	}
	while (*++vlp)
	    ;
	blkfree(vl);
	break;
    case G_IGNORE:
	str = Strsave(strip(*vlp));
	blkfree(vl);
	break;
    default:
	break;
    }
    return (str);
}

static Char **
libglob(Char **vl)
{
    int     gflgs = GLOB_QUOTE | GLOB_NOMAGIC;
    glob_t  globv;
    char   *ptr;
    int     nonomatch = adrof(STRnonomatch) != 0, magic = 0, match = 0;

    if (!vl || !vl[0])
	return (vl);

    globv.gl_offs = 0;
    globv.gl_pathv = 0;
    globv.gl_pathc = 0;

    if (nonomatch)
	gflgs |= GLOB_NOCHECK;

    do {
	ptr = short2qstr(*vl);
	switch (glob(ptr, gflgs, 0, &globv)) {
	case GLOB_ABORTED:
	    setname(vis_str(*vl));
	    stderror(ERR_NAME | ERR_GLOB);
	    /* NOTREACHED */
	case GLOB_NOSPACE:
	    stderror(ERR_NOMEM);
	    /* NOTREACHED */
	default:
	    break;
	}
	if (globv.gl_flags & GLOB_MAGCHAR) {
	    match |= (globv.gl_matchc != 0);
	    magic = 1;
	}
	gflgs |= GLOB_APPEND;
    }
    while (*++vl)
	;
    vl = (globv.gl_pathc == 0 || (magic && !match && !nonomatch)) ?
	NULL : blk2short(globv.gl_pathv);
    globfree(&globv);
    return (vl);
}

Char   *
globone(Char *str, int action)
{
    Char   *v[2], **vl, **vo;
    int    gflg;

    noglob = adrof(STRnoglob) != 0;
    gflag = 0;
    v[0] = str;
    v[1] = 0;
    tglob(v);
    gflg = gflag;
    if (gflg == G_NONE)
	return (strip(Strsave(str)));

    if (gflg & G_CSH) {
	/*
	 * Expand back-quote, tilde and brace
	 */
	vo = globexpand(v);
	if (noglob || (gflg & G_GLOB) == 0) {
	    if (vo[0] == NULL) {
		free(vo);
		return (Strsave(STRNULL));
	    }
	    if (vo[1] != NULL)
		return (handleone(str, vo, action));
	    else {
		str = strip(vo[0]);
		free(vo);
		return (str);
	    }
	}
    }
    else if (noglob || (gflg & G_GLOB) == 0)
	return (strip(Strsave(str)));
    else
	vo = v;

    vl = libglob(vo);
    if ((gflg & G_CSH) && vl != vo)
	blkfree(vo);
    if (vl == NULL) {
	setname(vis_str(str));
	stderror(ERR_NAME | ERR_NOMATCH);
    }
    if (vl[0] == NULL) {
	free(vl);
	return (Strsave(STRNULL));
    }
    if (vl[1] != NULL)
	return (handleone(str, vl, action));
    else {
	str = strip(*vl);
	free(vl);
	return (str);
    }
}

Char  **
globall(Char **v)
{
    Char  **vl, **vo;
    int   gflg = gflag;

    if (!v || !v[0]) {
	gargv = saveblk(v);
	gargc = blklen(gargv);
	return (gargv);
    }

    noglob = adrof(STRnoglob) != 0;

    if (gflg & G_CSH)
	/*
	 * Expand back-quote, tilde and brace
	 */
	vl = vo = globexpand(v);
    else
	vl = vo = saveblk(v);

    if (!noglob && (gflg & G_GLOB)) {
	vl = libglob(vo);
	if ((gflg & G_CSH) && vl != vo)
	    blkfree(vo);
    }
    else
	trim(vl);

    gargc = vl ? blklen(vl) : 0;
    return (gargv = vl);
}

void
ginit(void)
{
    gargsiz = GLOBSPACE;
    gargv = xreallocarray(NULL, gargsiz, sizeof(Char *));
    gargv[0] = 0;
    gargc = 0;
}

void
rscan(Char **t, void (*f)(int))
{
    Char *p;

    while ((p = *t++) != NULL)
	while (*p)
	    (*f) (*p++);
}

void
trim(Char **t)
{
    Char *p;

    while ((p = *t++) != NULL)
	while (*p)
	    *p++ &= TRIM;
}

void
tglob(Char **t)
{
    Char *p, c;

    while ((p = *t++) != NULL) {
	if (*p == '~' || *p == '=')
	    gflag |= G_CSH;
	else if (*p == '{' &&
		 (p[1] == '\0' || (p[1] == '}' && p[2] == '\0')))
	    continue;
	while ((c = *p++) != '\0') {
	    /*
	     * eat everything inside the matching backquotes
	     */
	    if (c == '`') {
		gflag |= G_CSH;
		while (*p && *p != '`')
		    if (*p++ == '\\') {
			if (*p)		/* Quoted chars */
			    p++;
			else
			    break;
		    }
		if (*p)			/* The matching ` */
		    p++;
		else
		    break;
	    }
	    else if (c == '{')
		gflag |= G_CSH;
	    else if (isglob(c))
		gflag |= G_GLOB;
	}
    }
}

/*
 * Command substitute cp.  If literal, then this is a substitution from a
 * << redirection, and so we should not crunch blanks and tabs, separating
 * words only at newlines.
 */
Char  **
dobackp(Char *cp, bool literal)
{
    Char *lp, *rp;
    Char   *ep, word[PATH_MAX];

    blkfree(pargv);
    pargsiz = GLOBSPACE;
    pargv = xreallocarray(NULL, pargsiz, sizeof(Char *));
    pargv[0] = NULL;
    pargcp = pargs = word;
    pargc = 0;
    pnleft = PATH_MAX - 4;
    for (;;) {
	for (lp = cp; *lp != '`'; lp++) {
	    if (*lp == 0) {
		if (pargcp != pargs)
		    pword();
		return (pargv);
	    }
	    psave(*lp);
	}
	lp++;
	for (rp = lp; *rp && *rp != '`'; rp++)
	    if (*rp == '\\') {
		rp++;
		if (!*rp)
		    goto oops;
	    }
	if (!*rp)
    oops:  stderror(ERR_UNMATCHED, '`');
	ep = Strsave(lp);
	ep[rp - lp] = 0;
	backeval(ep, literal);
	cp = rp + 1;
    }
}

static void
backeval(Char *cp, bool literal)
{
    int icnt, c;
    Char *ip;
    struct command faket;
    bool    hadnl;
    int     pvec[2], quoted;
    Char   *fakecom[2], ibuf[BUFSIZ];
    char    tibuf[BUFSIZ];

    hadnl = 0;
    icnt = 0;
    quoted = (literal || (cp[0] & QUOTE)) ? QUOTE : 0;
    faket.t_dtyp = NODE_COMMAND;
    faket.t_dflg = 0;
    faket.t_dlef = 0;
    faket.t_drit = 0;
    faket.t_dspr = 0;
    faket.t_dcom = fakecom;
    fakecom[0] = STRfakecom1;
    fakecom[1] = 0;

    /*
     * We do the psave job to temporarily change the current job so that the
     * following fork is considered a separate job.  This is so that when
     * backquotes are used in a builtin function that calls glob the "current
     * job" is not corrupted.  We only need one level of pushed jobs as long as
     * we are sure to fork here.
     */
    psavejob();

    /*
     * It would be nicer if we could integrate this redirection more with the
     * routines in sh.sem.c by doing a fake execute on a builtin function that
     * was piped out.
     */
    mypipe(pvec);
    if (pfork(&faket, -1) == 0) {
	struct wordent paraml;
	struct command *t;

	(void) close(pvec[0]);
	(void) dmove(pvec[1], 1);
	(void) dmove(SHERR, 2);
	initdesc();
	/*
	 * Bugfix for nested backquotes by Michael Greim <greim@sbsvax.UUCP>,
	 * posted to comp.bugs.4bsd 12 Sep. 1989.
	 */
	if (pargv)		/* mg, 21.dec.88 */
	    blkfree(pargv), pargv = 0, pargsiz = 0;
	/* mg, 21.dec.88 */
	arginp = cp;
	while (*cp)
	    *cp++ &= TRIM;

	/*
	 * In the child ``forget'' everything about current aliases or
	 * eval vectors.
	 */
	alvec = NULL;
	evalvec = NULL;
	alvecp = NULL;
	evalp = NULL;
	(void) lex(&paraml);
	if (seterr)
	    stderror(ERR_OLD);
	alias(&paraml);
	t = syntax(paraml.next, &paraml, 0);
	if (seterr)
	    stderror(ERR_OLD);
	if (t)
	    t->t_dflg |= F_NOFORK;
	(void) signal(SIGTSTP, SIG_IGN);
	(void) signal(SIGTTIN, SIG_IGN);
	(void) signal(SIGTTOU, SIG_IGN);
	execute(t, -1, NULL, NULL);
	exitstat();
    }
    free(cp);
    (void) close(pvec[1]);
    c = 0;
    ip = NULL;
    do {
	int     cnt = 0;

	for (;;) {
	    if (icnt == 0) {
		int     i;

		ip = ibuf;
		do
		    icnt = read(pvec[0], tibuf, BUFSIZ);
		while (icnt == -1 && errno == EINTR);
		if (icnt <= 0) {
		    c = -1;
		    break;
		}
		for (i = 0; i < icnt; i++)
		    ip[i] = (unsigned char) tibuf[i];
	    }
	    if (hadnl)
		break;
	    --icnt;
	    c = (*ip++ & TRIM);
	    if (c == 0)
		break;
	    if (c == '\n') {
		/*
		 * Continue around the loop one more time, so that we can eat
		 * the last newline without terminating this word.
		 */
		hadnl = 1;
		continue;
	    }
	    if (!quoted && (c == ' ' || c == '\t'))
		break;
	    cnt++;
	    psave(c | quoted);
	}
	/*
	 * Unless at end-of-file, we will form a new word here if there were
	 * characters in the word, or in any case when we take text literally.
	 * If we didn't make empty words here when literal was set then we
	 * would lose blank lines.
	 */
	if (c != -1 && (cnt || literal))
	    pword();
	hadnl = 0;
    } while (c >= 0);
    (void) close(pvec[0]);
    pwait();
    prestjob();
}

static void
psave(int c)
{
    if (--pnleft <= 0)
	stderror(ERR_WTOOLONG);
    *pargcp++ = c;
}

static void
pword(void)
{
    psave(0);
    if (pargc == pargsiz - 1) {
	pargsiz += GLOBSPACE;
	pargv = xreallocarray(pargv, pargsiz, sizeof(Char *));
    }
    pargv[pargc++] = Strsave(pargs);
    pargv[pargc] = NULL;
    pargcp = pargs;
    pnleft = PATH_MAX - 4;
}

int
Gmatch(Char *string, Char *pattern)
{
    Char **blk, **p;
    int	   gpol = 1, gres = 0;

    if (*pattern == '^') {
	gpol = 0;
	pattern++;
    }

    blk = xreallocarray(NULL, GLOBSPACE, sizeof(Char *));
    blk[0] = Strsave(pattern);
    blk[1] = NULL;

    expbrace(&blk, NULL, GLOBSPACE);

    for (p = blk; *p; p++)
	gres |= pmatch(string, *p);

    blkfree(blk);
    return(gres == gpol);
}

static int
pmatch(Char *string, Char *pattern)
{
    Char stringc, patternc;
    int     match, negate_range;
    Char    rangec;

    for (;; ++string) {
	stringc = *string & TRIM;
	patternc = *pattern++;
	switch (patternc) {
	case 0:
	    return (stringc == 0);
	case '?':
	    if (stringc == 0)
		return (0);
	    break;
	case '*':
	    if (!*pattern)
		return (1);
	    while (*string)
		if (Gmatch(string++, pattern))
		    return (1);
	    return (0);
	case '[':
	    match = 0;
	    if ((negate_range = (*pattern == '^')) != 0)
		pattern++;
	    while ((rangec = *pattern++) != '\0') {
		if (rangec == ']')
		    break;
		if (match)
		    continue;
		if (rangec == '-' && *(pattern-2) != '[' && *pattern  != ']') {
		    match = (stringc <= (*pattern & TRIM) &&
			      (*(pattern-2) & TRIM) <= stringc);
		    pattern++;
		}
		else
		    match = (stringc == (rangec & TRIM));
	    }
	    if (rangec == 0)
		stderror(ERR_NAME | ERR_MISSING, ']');
	    if (match == negate_range)
		return (0);
	    break;
	default:
	    if ((patternc & TRIM) != stringc)
		return (0);
	    break;

	}
    }
}

void
Gcat(Char *s1, Char *s2)
{
    Char *p, *q;
    int     n;

    for (p = s1; *p++;)
	continue;
    for (q = s2; *q++;)
	continue;
    n = (p - s1) + (q - s2) - 1;
    if (++gargc >= gargsiz) {
	gargsiz += GLOBSPACE;
	gargv = xreallocarray(gargv, gargsiz, sizeof(Char *));
    }
    gargv[gargc] = 0;
    p = gargv[gargc - 1] = xreallocarray(NULL, n, sizeof(Char));
    for (q = s1; (*p++ = *q++) != '\0';)
	continue;
    for (p--, q = s2; (*p++ = *q++) != '\0';)
	continue;
}

int
sortscmp(const void *a, const void *b)
{
    char    buf[2048];

    if (!a)			/* check for NULL */
	return (b ? 1 : 0);
    if (!b)
	return (-1);

    if (!*(Char **)a)			/* check for NULL */
	return (*(Char **)b ? 1 : 0);
    if (!*(Char **)b)
	return (-1);

    (void) strlcpy(buf, short2str(*(Char **)a), sizeof buf);
    return ((int) strcoll(buf, short2str(*(Char **)b)));
}
