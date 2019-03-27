/* $Header: /p/tcsh/cvsroot/tcsh/sh.set.c,v 3.89 2015/09/08 15:49:53 christos Exp $ */
/*
 * sh.set.c: Setting and Clearing of variables
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
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
#include "sh.h"

RCSID("$tcsh: sh.set.c,v 3.89 2015/09/08 15:49:53 christos Exp $")

#include "ed.h"
#include "tw.h"

#ifdef HAVE_NL_LANGINFO
#include <langinfo.h>
#endif

extern int GotTermCaps;
int numeof = 0;

static	void		 update_vars	(Char *);
static	Char		*getinx		(Char *, int *);
static	void		 asx		(Char *, int, Char *);
static	struct varent 	*getvx		(Char *, int);
static	Char		*xset		(Char *, Char ***);
static	Char		*operate	(int, Char *, Char *);
static	void	 	 putn1		(tcsh_number_t);
static	struct varent	*madrof		(Char *, struct varent *);
static	void		 unsetv1	(struct varent *);
static	void		 exportpath	(Char **);
static	void		 balance	(struct varent *, int, int);
static	int		 set_noclobber  (Char **);

/*
 * C Shell
 */

static void
update_vars(Char *vp)
{
    if (eq(vp, STRpath)) {
	struct varent *p = adrof(STRpath); 
	if (p == NULL)
	    stderror(ERR_NAME | ERR_UNDVAR);
	else {
	    exportpath(p->vec);
	    dohash(NULL, NULL);
	}
    }
    else if (eq(vp, STRnoclobber)) {
	struct varent *p = adrof(STRnoclobber);
	if (p == NULL)
	    stderror(ERR_NAME | ERR_UNDVAR);
	else
	    no_clobber = set_noclobber(p->vec);
    }
    else if (eq(vp, STRhistchars)) {
	Char *pn = varval(vp);

	HIST = *pn++;
	if (HIST)
	    HISTSUB = *pn;
	else
	    HISTSUB = HIST;
    }
    else if (eq(vp, STRpromptchars)) {
	Char *pn = varval(vp);

	PRCH = *pn++;
	if (PRCH)
	    PRCHROOT = *pn;
	else
	    PRCHROOT = PRCH;
    }
    else if (eq(vp, STRhistlit)) {
	HistLit = 1;
    }
    else if (eq(vp, STRuser)) {
	tsetenv(STRKUSER, varval(vp));
	tsetenv(STRLOGNAME, varval(vp));
    }
    else if (eq(vp, STRgroup)) {
	tsetenv(STRKGROUP, varval(vp));
    }
    else if (eq(vp, STRwordchars)) {
	word_chars = varval(vp);
    }
    else if (eq(vp, STRloginsh)) {
	loginsh = 1;
    }
    else if (eq(vp, STRanyerror)) {
	anyerror = 1;
    }
    else if (eq(vp, STRsymlinks)) {
	Char *pn = varval(vp);

	if (eq(pn, STRignore))
	    symlinks = SYM_IGNORE;
	else if (eq(pn, STRexpand))
	    symlinks = SYM_EXPAND;
	else if (eq(pn, STRchase))
	    symlinks = SYM_CHASE;
	else
	    symlinks = 0;
    }
    else if (eq(vp, STRterm)) {
	Char *cp = varval(vp);
	tsetenv(STRKTERM, cp);
#ifdef DOESNT_WORK_RIGHT
	cp = getenv("TERMCAP");
	if (cp && (*cp != '/'))	/* if TERMCAP and not a path */
	    Unsetenv(STRTERMCAP);
#endif /* DOESNT_WORK_RIGHT */
	GotTermCaps = 0;
	if (noediting && Strcmp(cp, STRnetwork) != 0 &&
	    Strcmp(cp, STRunknown) != 0 && Strcmp(cp, STRdumb) != 0) {
	    editing = 1;
	    noediting = 0;
	    setNS(STRedit);
	}
	ed_Init();		/* reset the editor */
    }
    else if (eq(vp, STRhome)) {
	Char *cp, *canon;

	cp = Strsave(varval(vp));	/* get the old value back */
	cleanup_push(cp, xfree);

	/*
	 * convert to cononical pathname (possibly resolving symlinks)
	 */
	canon = dcanon(cp, cp);
	cleanup_ignore(cp);
	cleanup_until(cp);
	cleanup_push(canon, xfree);

	setcopy(vp, canon, VAR_READWRITE);	/* have to save the new val */

	/* and now mirror home with HOME */
	tsetenv(STRKHOME, canon);
	/* fix directory stack for new tilde home */
	dtilde();
	cleanup_until(canon);
    }
    else if (eq(vp, STRedit)) {
	editing = 1;
	noediting = 0;
	/* PWP: add more stuff in here later */
    }
    else if (eq(vp, STRvimode)) {
	VImode = 1;
	update_wordchars();
    }
    else if (eq(vp, STRshlvl)) {
	tsetenv(STRKSHLVL, varval(vp));
    }
    else if (eq(vp, STRignoreeof)) {
	Char *cp;
	numeof = 0;
    	for ((cp = varval(STRignoreeof)); cp && *cp; cp++) {
	    if (!Isdigit(*cp)) {
		numeof = 0;
		break;
	    }
	    numeof = numeof * 10 + *cp - '0';
	}
	if (numeof <= 0) numeof = 26;	/* Sanity check */
    } 
    else if (eq(vp, STRbackslash_quote)) {
	bslash_quote = 1;
    }
    else if (eq(vp, STRcompat_expr)) {
	compat_expr = 1;
    }
    else if (eq(vp, STRdirstack)) {
	dsetstack();
    }
    else if (eq(vp, STRrecognize_only_executables)) {
	tw_cmd_free();
    }
    else if (eq(vp, STRkillring)) {
	SetKillRing((int)getn(varval(vp)));
    }
    else if (eq(vp, STRhistory)) {
	sethistory((int)getn(varval(vp)));
    }
#ifndef HAVENOUTMP
    else if (eq(vp, STRwatch)) {
	resetwatch();
    }
#endif /* HAVENOUTMP */
    else if (eq(vp, STRimplicitcd)) {
	implicit_cd = ((eq(varval(vp), STRverbose)) ? 2 : 1);
    }
    else if (eq(vp, STRcdtohome)) {
	cdtohome = 1;
    }
#ifdef COLOR_LS_F
    else if (eq(vp, STRcolor)) {
	set_color_context();
    }
#endif /* COLOR_LS_F */
#if defined(KANJI) && defined(SHORT_STRINGS) && defined(DSPMBYTE)
    else if(eq(vp, CHECK_MBYTEVAR) || eq(vp, STRnokanji)) {
	update_dspmbyte_vars();
    }
#endif
#ifdef NLS_CATALOGS
    else if (eq(vp, STRcatalog)) {
	nlsclose();
	nlsinit();
    }
#if defined(FILEC) && defined(TIOCSTI)
    else if (eq(vp, STRfilec))
	filec = 1;
#endif
#endif /* NLS_CATALOGS */
}


/*ARGSUSED*/
void
doset(Char **v, struct command *c)
{
    Char *p;
    Char   *vp;
    Char  **vecp;
    int    hadsub;
    int     subscr;
    int	    flags = VAR_READWRITE;
    int    first_match = 0;
    int    last_match = 0;
    int    changed = 0;

    USE(c);
    v++;
    do {
	changed = 0;
	/*
	 * Readonly addition From: Tim P. Starrin <noid@cyborg.larc.nasa.gov>
	 */
	if (*v && eq(*v, STRmr)) {
	    flags = VAR_READONLY;
	    v++;
	    changed = 1;
	}
	if (*v && eq(*v, STRmf) && !last_match) {
	    first_match = 1;
	    v++;
	    changed = 1;
	}
	if (*v && eq(*v, STRml) && !first_match) {
	    last_match = 1;
	    v++;
	    changed = 1;
	}
    } while(changed);
    p = *v++;
    if (p == 0) {
	plist(&shvhed, flags);
	return;
    }
    do {
	hadsub = 0;
	vp = p;
	if (!letter(*p))
	    stderror(ERR_NAME | ERR_VARBEGIN);
	do {
	    p++;
	} while (alnum(*p));
	if (*p == '[') {
	    hadsub++;
	    p = getinx(p, &subscr);
	}
	if (*p != '\0' && *p != '=')
	    stderror(ERR_NAME | ERR_VARALNUM);
	if (*p == '=') {
	    *p++ = '\0';
	    if (*p == '\0' && *v != NULL && **v == '(')
		p = *v++;
	}
	else if (*v && eq(*v, STRequal)) {
	    if (*++v != NULL)
		p = *v++;
	}
	if (eq(p, STRLparen)) {
	    Char **e = v;

	    if (hadsub)
		stderror(ERR_NAME | ERR_SYNTAX);
	    for (;;) {
		if (!*e)
		    stderror(ERR_NAME | ERR_MISSING, ')');
		if (**e == ')')
		    break;
		e++;
	    }
	    p = *e;
	    *e = 0;
	    vecp = saveblk(v);
	    if (first_match)
	       flags |= VAR_FIRST;
	    else if (last_match)
	       flags |= VAR_LAST;

	    set1(vp, vecp, &shvhed, flags);
	    *e = p;
	    v = e + 1;
	}
	else if (hadsub) {
	    Char *copy;

	    copy = Strsave(p);
	    cleanup_push(copy, xfree);
	    asx(vp, subscr, copy);
	    cleanup_ignore(copy);
	    cleanup_until(copy);
	}
	else
	    setv(vp, Strsave(p), flags);
	update_vars(vp);
    } while ((p = *v++) != NULL);
}

static Char *
getinx(Char *cp, int *ip)
{
    *ip = 0;
    *cp++ = 0;
    while (*cp && Isdigit(*cp))
	*ip = *ip * 10 + *cp++ - '0';
    if (*cp++ != ']')
	stderror(ERR_NAME | ERR_SUBSCRIPT);
    return (cp);
}

static void
asx(Char *vp, int subscr, Char *p)
{
    struct varent *v = getvx(vp, subscr);
    Char *prev;

    if (v->v_flags & VAR_READONLY)
	stderror(ERR_READONLY|ERR_NAME, v->v_name);
    prev = v->vec[subscr - 1];
    cleanup_push(prev, xfree);
    v->vec[subscr - 1] = globone(p, G_APPEND);
    cleanup_until(prev);
}

static struct varent *
getvx(Char *vp, int subscr)
{
    struct varent *v = adrof(vp);

    if (v == 0)
	udvar(vp);
    if (subscr < 1 || subscr > blklen(v->vec))
	stderror(ERR_NAME | ERR_RANGE);
    return (v);
}

/*ARGSUSED*/
void
dolet(Char **v, struct command *dummy)
{
    Char *p;
    Char   *vp, c, op;
    int    hadsub;
    int     subscr;

    USE(dummy);
    v++;
    p = *v++;
    if (p == 0) {
	prvars();
	return;
    }
    do {
	hadsub = 0;
	vp = p;
	if (letter(*p))
	    for (; alnum(*p); p++)
		continue;
	if (vp == p || !letter(*vp))
	    stderror(ERR_NAME | ERR_VARBEGIN);
	if (*p == '[') {
	    hadsub++;
	    p = getinx(p, &subscr);
	}
	if (*p == 0 && *v)
	    p = *v++;
	if ((op = *p) != 0)
	    *p++ = 0;
	else
	    stderror(ERR_NAME | ERR_ASSIGN);

	/*
	 * if there is no expression after the '=' then print a "Syntax Error"
	 * message - strike
	 */
	if (*p == '\0' && *v == NULL)
	    stderror(ERR_NAME | ERR_ASSIGN);

	vp = Strsave(vp);
	cleanup_push(vp, xfree);
	if (op == '=') {
	    c = '=';
	    p = xset(p, &v);
	}
	else {
	    c = *p++;
	    if (any("+-", c)) {
		if (c != op || *p)
		    stderror(ERR_NAME | ERR_UNKNOWNOP);
		p = Strsave(STR1);
	    }
	    else {
		if (any("<>", op)) {
		    if (c != op)
			stderror(ERR_NAME | ERR_UNKNOWNOP);
		    stderror(ERR_NAME | ERR_SYNTAX);
		}
		if (c != '=')
		    stderror(ERR_NAME | ERR_UNKNOWNOP);
		p = xset(p, &v);
	    }
	}
	cleanup_push(p, xfree);
	if (op == '=') {
	    if (hadsub)
		asx(vp, subscr, p);
	    else
		setv(vp, p, VAR_READWRITE);
	    cleanup_ignore(p);
	}
	else if (hadsub) {
	    struct varent *gv = getvx(vp, subscr);
	    Char *val;

	    val = operate(op, gv->vec[subscr - 1], p);
	    cleanup_push(val, xfree);
	    asx(vp, subscr, val);
	    cleanup_ignore(val);
	    cleanup_until(val);
	}
	else {
	    Char *val;

	    val = operate(op, varval(vp), p);
	    cleanup_push(val, xfree);
	    setv(vp, val, VAR_READWRITE);
	    cleanup_ignore(val);
	    cleanup_until(val);
	}
	update_vars(vp);
	cleanup_until(vp);
    } while ((p = *v++) != NULL);
}

static Char *
xset(Char *cp, Char ***vp)
{
    Char *dp;

    if (*cp) {
	dp = Strsave(cp);
	--(*vp);
	xfree(** vp);
	**vp = dp;
    }
    return (putn(expr(vp)));
}

static Char *
operate(int op, Char *vp, Char *p)
{
    Char    opr[2];
    Char   *vec[5];
    Char **v = vec;
    Char  **vecp = v;
    tcsh_number_t i;

    if (op != '=') {
	if (*vp)
	    *v++ = vp;
	opr[0] = op;
	opr[1] = 0;
	*v++ = opr;
	if (op == '<' || op == '>')
	    *v++ = opr;
    }
    *v++ = p;
    *v++ = 0;
    i = expr(&vecp);
    if (*vecp)
	stderror(ERR_NAME | ERR_EXPRESSION);
    return (putn(i));
}

static Char *putp;

Char *
putn(tcsh_number_t n)
{
    Char nbuf[1024]; /* Enough even for octal */

    putp = nbuf;
    if (n < 0) {
	n = -n;
	*putp++ = '-';
    }
    putn1(n);
    *putp = 0;
    return (Strsave(nbuf));
}

static void
putn1(tcsh_number_t n)
{
    if (n > 9)
	putn1(n / 10);
    *putp++ = (Char)(n % 10 + '0');
}

tcsh_number_t
getn(const Char *cp)
{
    tcsh_number_t n;
    int     sign;
    int base;

    if (!cp)			/* PWP: extra error checking */
	stderror(ERR_NAME | ERR_BADNUM);

    sign = 0;
    if (cp[0] == '+' && cp[1])
	cp++;
    if (*cp == '-') {
	sign++;
	cp++;
	if (!Isdigit(*cp))
	    stderror(ERR_NAME | ERR_BADNUM);
    }

    if (cp[0] == '0' && cp[1] && is_set(STRparseoctal))
	base = 8;
    else
	base = 10;

    n = 0;
    while (Isdigit(*cp))
    {
	if (base == 8 && *cp >= '8')
	    stderror(ERR_NAME | ERR_BADNUM);
	n = n * base + *cp++ - '0';
    }
    if (*cp)
	stderror(ERR_NAME | ERR_BADNUM);
    return (sign ? -n : n);
}

Char   *
value1(Char *var, struct varent *head)
{
    struct varent *vp;

    if (!var || !head)		/* PWP: extra error checking */
	return (STRNULL);

    vp = adrof1(var, head);
    return ((vp == NULL || vp->vec == NULL || vp->vec[0] == NULL) ?
	STRNULL : vp->vec[0]);
}

static struct varent *
madrof(Char *pat, struct varent *vp)
{
    struct varent *vp1;

    for (vp = vp->v_left; vp; vp = vp->v_right) {
	if (vp->v_left && (vp1 = madrof(pat, vp)) != NULL)
	    return vp1;
	if (Gmatch(vp->v_name, pat))
	    return vp;
    }
    return vp;
}

struct varent *
adrof1(const Char *name, struct varent *v)
{
    int cmp;

    v = v->v_left;
    while (v && ((cmp = *name - *v->v_name) != 0 || 
		 (cmp = Strcmp(name, v->v_name)) != 0))
	if (cmp < 0)
	    v = v->v_left;
	else
	    v = v->v_right;
    return v;
}

void
setcopy(const Char *var, const Char *val, int flags)
{
    Char *copy;

    copy = Strsave(val);
    cleanup_push(copy, xfree);
    setv(var, copy, flags);
    cleanup_ignore(copy);
    cleanup_until(copy);
}

/*
 * The caller is responsible for putting value in a safe place
 */
void
setv(const Char *var, Char *val, int flags)
{
    Char **vec = xmalloc(2 * sizeof(Char **));

    vec[0] = val;
    vec[1] = 0;
    set1(var, vec, &shvhed, flags);
}

void
set1(const Char *var, Char **vec, struct varent *head, int flags)
{
    Char **oldv = vec;

    if ((flags & VAR_NOGLOB) == 0) {
	int gflag;

	gflag = tglob(oldv);
	if (gflag) {
	    vec = globall(oldv, gflag);
	    if (vec == 0) {
		blkfree(oldv);
		stderror(ERR_NAME | ERR_NOMATCH);
	    }
	    blkfree(oldv);
	}
    }
    /*
     * Uniqueness addition from: Michael Veksler <mveksler@vnet.ibm.com>
     */
    if ( flags & (VAR_FIRST | VAR_LAST) ) {
	/*
	 * Code for -f (VAR_FIRST) and -l (VAR_LAST) options.
	 * Method:
	 *  Delete all duplicate words leaving "holes" in the word array (vec).
	 *  Then remove the "holes", keeping the order of the words unchanged.
	 */
	if (vec && vec[0] && vec[1]) { /* more than one word ? */
	    int i, j;
	    int num_items;

	    for (num_items = 0; vec[num_items]; num_items++)
	        continue;
	    if (flags & VAR_FIRST) {
		/* delete duplications, keeping first occurance */
		for (i = 1; i < num_items; i++)
		    for (j = 0; j < i; j++)
			/* If have earlier identical item, remove i'th item */
			if (vec[i] && vec[j] && Strcmp(vec[j], vec[i]) == 0) {
			    xfree(vec[i]);
			    vec[i] = NULL;
			    break;
			}
	    } else if (flags & VAR_LAST) {
	      /* delete duplications, keeping last occurance */
		for (i = 0; i < num_items - 1; i++)
		    for (j = i + 1; j < num_items; j++)
			/* If have later identical item, remove i'th item */
			if (vec[i] && vec[j] && Strcmp(vec[j], vec[i]) == 0) {
			    /* remove identical item (the first) */
			    xfree(vec[i]);
			    vec[i] = NULL;
			}
	    }
	    /* Compress items - remove empty items */
	    for (j = i = 0; i < num_items; i++)
	       if (vec[i]) 
		  vec[j++] = vec[i];

	    /* NULL-fy remaining items */
	    for (; j < num_items; j++)
		 vec[j] = NULL;
	}
	/* don't let the attribute propagate */
	flags &= ~(VAR_FIRST|VAR_LAST);
    } 
    setq(var, vec, head, flags);
}


void
setq(const Char *name, Char **vec, struct varent *p, int flags)
{
    struct varent *c;
    int f;

    f = 0;			/* tree hangs off the header's left link */
    while ((c = p->v_link[f]) != 0) {
	if ((f = *name - *c->v_name) == 0 &&
	    (f = Strcmp(name, c->v_name)) == 0) {
	    if (c->v_flags & VAR_READONLY)
		stderror(ERR_READONLY|ERR_NAME, c->v_name);
	    blkfree(c->vec);
	    c->v_flags = flags;
	    trim(c->vec = vec);
	    return;
	}
	p = c;
	f = f > 0;
    }
    p->v_link[f] = c = xmalloc(sizeof(struct varent));
    c->v_name = Strsave(name);
    c->v_flags = flags;
    c->v_bal = 0;
    c->v_left = c->v_right = 0;
    c->v_parent = p;
    balance(p, f, 0);
    trim(c->vec = vec);
}

/*ARGSUSED*/
void
unset(Char **v, struct command *c)
{
    int did_roe, did_edit;

    USE(c);
    did_roe = adrof(STRrecognize_only_executables) != NULL;
    did_edit = adrof(STRedit) != NULL;
    unset1(v, &shvhed);

#if defined(FILEC) && defined(TIOCSTI)
    if (adrof(STRfilec) == 0)
	filec = 0;
#endif /* FILEC && TIOCSTI */

    if (adrof(STRhistchars) == 0) {
	HIST = '!';
	HISTSUB = '^';
    }
    if (adrof(STRignoreeof) == 0)
	numeof = 0;
    if (adrof(STRpromptchars) == 0) {
	PRCH = tcsh ? '>' : '%';
	PRCHROOT = '#';
    }
    if (adrof(STRnoclobber) == 0)
	no_clobber = 0;
    if (adrof(STRhistlit) == 0)
	HistLit = 0;
    if (adrof(STRloginsh) == 0)
	loginsh = 0;
    if (adrof(STRanyerror) == 0)
	anyerror = 0;
    if (adrof(STRwordchars) == 0)
	word_chars = STR_WORD_CHARS;
    if (adrof(STRedit) == 0)
	editing = 0;
    if (adrof(STRbackslash_quote) == 0)
	bslash_quote = 0;
    if (adrof(STRcompat_expr) == 0)
	compat_expr = 0;
    if (adrof(STRsymlinks) == 0)
	symlinks = 0;
    if (adrof(STRimplicitcd) == 0)
	implicit_cd = 0;
    if (adrof(STRcdtohome) == 0)
	cdtohome = 0;
    if (adrof(STRkillring) == 0)
	SetKillRing(0);
    if (did_edit && noediting && adrof(STRedit) == 0)
	noediting = 0;
    if (adrof(STRvimode) == 0)
	VImode = 0;
    if (did_roe && adrof(STRrecognize_only_executables) == 0)
	tw_cmd_free();
    if (adrof(STRhistory) == 0)
	sethistory(0);
#ifdef COLOR_LS_F
    if (adrof(STRcolor) == 0)
	set_color_context();
#endif /* COLOR_LS_F */
#if defined(KANJI) && defined(SHORT_STRINGS) && defined(DSPMBYTE)
    update_dspmbyte_vars();
#endif
    update_wordchars();
#ifdef NLS_CATALOGS
    nlsclose();
    nlsinit();
#endif /* NLS_CATALOGS */
}

void
unset1(Char *v[], struct varent *head)
{
    struct varent *vp;
    int cnt;

    while (*++v) {
	cnt = 0;
	while ((vp = madrof(*v, head)) != NULL)
	    if (vp->v_flags & VAR_READONLY)
		stderror(ERR_READONLY|ERR_NAME, vp->v_name);
	    else
		unsetv1(vp), cnt++;
	if (cnt == 0)
	    setname(short2str(*v));
    }
}

void
unsetv(Char *var)
{
    struct varent *vp;

    if ((vp = adrof1(var, &shvhed)) == 0)
	udvar(var);
    unsetv1(vp);
}

static void
unsetv1(struct varent *p)
{
    struct varent *c, *pp;
    int f;

    /*
     * Free associated memory first to avoid complications.
     */
    blkfree(p->vec);
    xfree(p->v_name);
    /*
     * If p is missing one child, then we can move the other into where p is.
     * Otherwise, we find the predecessor of p, which is guaranteed to have no
     * right child, copy it into p, and move it's left child into it.
     */
    if (p->v_right == 0)
	c = p->v_left;
    else if (p->v_left == 0)
	c = p->v_right;
    else {
	for (c = p->v_left; c->v_right; c = c->v_right)
	    continue;
	p->v_name = c->v_name;
	p->v_flags = c->v_flags;
	p->vec = c->vec;
	p = c;
	c = p->v_left;
    }

    /*
     * Move c into where p is.
     */
    pp = p->v_parent;
    f = pp->v_right == p;
    if ((pp->v_link[f] = c) != 0)
	c->v_parent = pp;
    /*
     * Free the deleted node, and rebalance.
     */
    xfree(p);
    balance(pp, f, 1);
}

/* Set variable name to NULL. */
void
setNS(const Char *varName)
{
    setcopy(varName, STRNULL, VAR_READWRITE);
}

/*ARGSUSED*/
void
shift(Char **v, struct command *c)
{
    struct varent *argv;
    Char *name;

    USE(c);
    v++;
    name = *v;
    if (name == 0)
	name = STRargv;
    else
	(void) strip(name);
    argv = adrof(name);
    if (argv == NULL || argv->vec == NULL)
	udvar(name);
    if (argv->vec[0] == 0)
	stderror(ERR_NAME | ERR_NOMORE);
    lshift(argv->vec, 1);
    update_vars(name);
}

static void
exportpath(Char **val)
{
    struct Strbuf buf = Strbuf_INIT;
    Char    	*exppath;

    if (val)
	while (*val) {
	    Strbuf_append(&buf, *val++);
	    if (*val == 0 || eq(*val, STRRparen))
		break;
	    Strbuf_append1(&buf, PATHSEP);
	}
    exppath = Strbuf_finish(&buf);
    cleanup_push(exppath, xfree);
    tsetenv(STRKPATH, exppath);
    cleanup_until(exppath);
}

static int
set_noclobber(Char **val)
{
    Char *option;
    int nc = NOCLOBBER_DEFAULT;

    if (val == NULL)
	return nc;
    while (*val) {
	if (*val == 0 || eq(*val, STRRparen))
	    return nc;

	option = *val++;

	if (eq(option, STRnotempty))
	    nc |= NOCLOBBER_NOTEMPTY;
	else if (eq(option, STRask))
	    nc |= NOCLOBBER_ASK;
    }
    return nc;
}

#ifndef lint
 /*
  * Lint thinks these have null effect
  */
 /* macros to do single rotations on node p */
# define rright(p) (\
	t = (p)->v_left,\
	(t)->v_parent = (p)->v_parent,\
	(((p)->v_left = t->v_right) != NULL) ?\
	    (t->v_right->v_parent = (p)) : 0,\
	(t->v_right = (p))->v_parent = t,\
	(p) = t)
# define rleft(p) (\
	t = (p)->v_right,\
	((t)->v_parent = (p)->v_parent,\
	((p)->v_right = t->v_left) != NULL) ? \
		(t->v_left->v_parent = (p)) : 0,\
	(t->v_left = (p))->v_parent = t,\
	(p) = t)
#else
static struct varent *
rleft(struct varent *p)
{
    return (p);
}
static struct varent *
rright(struct varent *p)
{
    return (p);
}

#endif /* ! lint */


/*
 * Rebalance a tree, starting at p and up.
 * F == 0 means we've come from p's left child.
 * D == 1 means we've just done a delete, otherwise an insert.
 */
static void
balance(struct varent *p, int f, int d)
{
    struct varent *pp;

#ifndef lint
    struct varent *t;	/* used by the rotate macros */
#endif /* !lint */
    int ff;
#ifdef lint
    ff = 0;	/* Sun's lint is dumb! */
#endif

    /*
     * Ok, from here on, p is the node we're operating on; pp is it's parent; f
     * is the branch of p from which we have come; ff is the branch of pp which
     * is p.
     */
    for (; (pp = p->v_parent) != 0; p = pp, f = ff) {
	ff = pp->v_right == p;
	if (f ^ d) {		/* right heavy */
	    switch (p->v_bal) {
	    case -1:		/* was left heavy */
		p->v_bal = 0;
		break;
	    case 0:		/* was balanced */
		p->v_bal = 1;
		break;
	    case 1:		/* was already right heavy */
		switch (p->v_right->v_bal) {
		case 1:	/* single rotate */
		    pp->v_link[ff] = rleft(p);
		    p->v_left->v_bal = 0;
		    p->v_bal = 0;
		    break;
		case 0:	/* single rotate */
		    pp->v_link[ff] = rleft(p);
		    p->v_left->v_bal = 1;
		    p->v_bal = -1;
		    break;
		case -1:	/* double rotate */
		    (void) rright(p->v_right);
		    pp->v_link[ff] = rleft(p);
		    p->v_left->v_bal =
			p->v_bal < 1 ? 0 : -1;
		    p->v_right->v_bal =
			p->v_bal > -1 ? 0 : 1;
		    p->v_bal = 0;
		    break;
		default:
		    break;
		}
		break;
	    default:
		break;
	    }
	}
	else {			/* left heavy */
	    switch (p->v_bal) {
	    case 1:		/* was right heavy */
		p->v_bal = 0;
		break;
	    case 0:		/* was balanced */
		p->v_bal = -1;
		break;
	    case -1:		/* was already left heavy */
		switch (p->v_left->v_bal) {
		case -1:	/* single rotate */
		    pp->v_link[ff] = rright(p);
		    p->v_right->v_bal = 0;
		    p->v_bal = 0;
		    break;
		case 0:	/* single rotate */
		    pp->v_link[ff] = rright(p);
		    p->v_right->v_bal = -1;
		    p->v_bal = 1;
		    break;
		case 1:	/* double rotate */
		    (void) rleft(p->v_left);
		    pp->v_link[ff] = rright(p);
		    p->v_left->v_bal =
			p->v_bal < 1 ? 0 : -1;
		    p->v_right->v_bal =
			p->v_bal > -1 ? 0 : 1;
		    p->v_bal = 0;
		    break;
		default:
		    break;
		}
		break;
	    default:
		break;
	    }
	}
	/*
	 * If from insert, then we terminate when p is balanced. If from
	 * delete, then we terminate when p is unbalanced.
	 */
	if ((p->v_bal == 0) ^ d)
	    break;
    }
}

void
plist(struct varent *p, int what)
{
    struct varent *c;
    int len;

    for (;;) {
	while (p->v_left)
	    p = p->v_left;
x:
	if (p->v_parent == 0)	/* is it the header? */
	    break;
	if ((p->v_flags & what) != 0) {
	    if (setintr) {
		int old_pintr_disabled;

		pintr_push_enable(&old_pintr_disabled);
		cleanup_until(&old_pintr_disabled);
	    }
	    len = blklen(p->vec);
	    xprintf("%S\t", p->v_name);
	    if (len != 1)
		xputchar('(');
	    blkpr(p->vec);
	    if (len != 1)
		xputchar(')');
	    xputchar('\n');
	}
	if (p->v_right) {
	    p = p->v_right;
	    continue;
	}
	do {
	    c = p;
	    p = p->v_parent;
	} while (p->v_right == c);
	goto x;
    }
}

#if defined(KANJI)
# if defined(SHORT_STRINGS) && defined(DSPMBYTE)
extern int dspmbyte_ls;

void
update_dspmbyte_vars(void)
{
    int lp, iskcode;
    Char *dstr1;
    struct varent *vp;
    
    /* if variable "nokanji" is set, multi-byte display is disabled */
    if ((vp = adrof(CHECK_MBYTEVAR)) && !adrof(STRnokanji)) {
	_enable_mbdisp = 1;
	dstr1 = vp->vec[0];
	if(eq (dstr1, STRsjis))
	    iskcode = 1;
	else if (eq(dstr1, STReuc))
	    iskcode = 2;
	else if (eq(dstr1, STRbig5))
	    iskcode = 3;
	else if (eq(dstr1, STRutf8))
	    iskcode = 4;
	else if ((dstr1[0] - '0') >= 0 && (dstr1[0] - '0') <= 3) {
	    iskcode = 0;
	}
	else {
	    xprintf(CGETS(18, 2,
	       "Warning: unknown multibyte display; using default(euc(JP))\n"));
	    iskcode = 2;
	}
	if (dstr1 && vp->vec[1] && eq(vp->vec[1], STRls))
	  dspmbyte_ls = 1;
	else
	  dspmbyte_ls = 0;
	for (lp = 0; lp < 256 && iskcode > 0; lp++) {
	    switch (iskcode) {
	    case 1:
		/* Shift-JIS */
		_cmap[lp] = _cmap_mbyte[lp];
		_mbmap[lp] = _mbmap_sjis[lp];
		break;
	    case 2:
		/* 2 ... euc */
		_cmap[lp] = _cmap_mbyte[lp];
		_mbmap[lp] = _mbmap_euc[lp];
		break;
	    case 3:
		/* 3 ... big5 */
		_cmap[lp] = _cmap_mbyte[lp];
		_mbmap[lp] = _mbmap_big5[lp];
		break;
	    case 4:
		/* 4 ... utf8 */
		_cmap[lp] = _cmap_mbyte[lp];
		_mbmap[lp] = _mbmap_utf8[lp];
		break;
	    default:
		xprintf(CGETS(18, 3,
		    "Warning: unknown multibyte code %d; multibyte disabled\n"),
		    iskcode);
		_cmap[lp] = _cmap_c[lp];
		_mbmap[lp] = 0;	/* Default map all 0 */
		_enable_mbdisp = 0;
		break;
	    }
	}
	if (iskcode == 0) {
	    /* check original table */
	    if (Strlen(dstr1) != 256) {
		xprintf(CGETS(18, 4,
       "Warning: Invalid multibyte table length (%d); multibyte disabled\n"),
		    Strlen(dstr1));
		_enable_mbdisp = 0;
	    }
	    for (lp = 0; lp < 256 && _enable_mbdisp == 1; lp++) {
		if (!((dstr1[lp] - '0') >= 0 && (dstr1[lp] - '0') <= 3)) {
		    xprintf(CGETS(18, 4,
	   "Warning: bad multibyte code at offset +%d; multibyte diabled\n"),
			lp);
		    _enable_mbdisp = 0;
		    break;
		}
	    }
	    /* set original table */
	    for (lp = 0; lp < 256; lp++) {
		if (_enable_mbdisp == 1) {
		    _cmap[lp] = _cmap_mbyte[lp];
		    _mbmap[lp] = (unsigned short) ((dstr1[lp] - '0') & 0x0f);
		}
		else {
		    _cmap[lp] = _cmap_c[lp];
		    _mbmap[lp] = 0;	/* Default map all 0 */
		}
	    }
	}
    }
    else {
	for (lp = 0; lp < 256; lp++) {
	    _cmap[lp] = _cmap_c[lp];
	    _mbmap[lp] = 0;	/* Default map all 0 */
	}
	_enable_mbdisp = 0;
	dspmbyte_ls = 0;
    }
#ifdef MBYTEDEBUG	/* Sorry, use for beta testing */
    {
	Char mbmapstr[300];
	for (lp = 0; lp < 256; lp++)
	    mbmapstr[lp] = _mbmap[lp] + '0';
	mbmapstr[lp] = 0;
	setcopy(STRmbytemap, mbmapstr, VAR_READWRITE);
    }
#endif /* MBYTEMAP */
}

/* dspkanji/dspmbyte autosetting */
/* PATCH IDEA FROM Issei.Suzuki VERY THANKS */
void
autoset_dspmbyte(const Char *pcp)
{
    int i;
    static const struct dspm_autoset_Table {
	Char *n;
	Char *v;
    } dspmt[] = {
	{ STRLANGEUCJP, STReuc },
	{ STRLANGEUCKR, STReuc },
	{ STRLANGEUCZH, STReuc },
	{ STRLANGEUCJPB, STReuc },
	{ STRLANGEUCKRB, STReuc },
	{ STRLANGEUCZHB, STReuc },
#ifdef __linux__
	{ STRLANGEUCJPC, STReuc },
#endif
	{ STRLANGSJIS, STRsjis },
	{ STRLANGSJISB, STRsjis },
	{ STRLANGBIG5, STRbig5 },
	{ STRstarutfstar8, STRutf8 },
	{ NULL, NULL }
    };
#if defined(HAVE_NL_LANGINFO) && defined(CODESET)
    static const struct dspm_autoset_Table dspmc[] = {
	{ STRstarutfstar8, STRutf8 },
	{ STReuc, STReuc },
	{ STRGB2312, STReuc },
	{ STRLANGBIG5, STRbig5 },
	{ NULL, NULL }
    };
    Char *codeset;

    codeset = str2short(nl_langinfo(CODESET));
    if (*codeset != '\0') {
	for (i = 0; dspmc[i].n; i++) {
	    const Char *estr;
	    if (dspmc[i].n[0] && t_pmatch(pcp, dspmc[i].n, &estr, 0) > 0) {
		setcopy(CHECK_MBYTEVAR, dspmc[i].v, VAR_READWRITE);
		update_dspmbyte_vars();
		return;
	    }
	}
    }
#endif
    
    if (*pcp == '\0')
	return;

    for (i = 0; dspmt[i].n; i++) {
	const Char *estr;
	if (dspmt[i].n[0] && t_pmatch(pcp, dspmt[i].n, &estr, 0) > 0) {
	    setcopy(CHECK_MBYTEVAR, dspmt[i].v, VAR_READWRITE);
	    update_dspmbyte_vars();
	    break;
	}
    }
}
# elif defined(AUTOSET_KANJI)
void
autoset_kanji(void)
{
    char *codeset = nl_langinfo(CODESET);
    
    if (*codeset == '\0') {
	if (adrof(STRnokanji) == NULL)
	    setNS(STRnokanji);
	return;
    }

    if (strcasestr(codeset, "SHIFT_JIS") == (char*)0) {
	if (adrof(STRnokanji) == NULL)
	    setNS(STRnokanji);
	return;
    }

    if (adrof(STRnokanji) != NULL)
	unsetv(STRnokanji);
}
#endif
#endif

void
update_wordchars(void)
{
    if ((word_chars == STR_WORD_CHARS) || (word_chars == STR_WORD_CHARS_VI)) {
	word_chars = (VImode ? STR_WORD_CHARS_VI : STR_WORD_CHARS);
    }
}
