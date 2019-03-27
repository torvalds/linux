/* $Header: /p/tcsh/cvsroot/tcsh/sh.dol.c,v 3.87 2014/08/13 23:39:34 amold Exp $ */
/*
 * sh.dol.c: Variable substitutions
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

RCSID("$tcsh: sh.dol.c,v 3.87 2014/08/13 23:39:34 amold Exp $")

/*
 * C shell
 */

/*
 * These routines perform variable substitution and quoting via ' and ".
 * To this point these constructs have been preserved in the divided
 * input words.  Here we expand variables and turn quoting via ' and " into
 * QUOTE bits on characters (which prevent further interpretation).
 * If the `:q' modifier was applied during history expansion, then
 * some QUOTEing may have occurred already, so we dont "trim()" here.
 */

static eChar Dpeekc;		/* Peek for DgetC */
static eChar Dpeekrd;		/* Peek for Dreadc */
static Char *Dcp, *const *Dvp;	/* Input vector for Dreadc */

#define	DEOF	CHAR_ERR

#define	unDgetC(c)	Dpeekc = c

#define QUOTES		(_QF|_QB|_ESC)	/* \ ' " ` */

/*
 * The following variables give the information about the current
 * $ expansion, recording the current word position, the remaining
 * words within this expansion, the count of remaining words, and the
 * information about any : modifier which is being applied.
 */
static Char *dolp;		/* Remaining chars from this word */
static Char **dolnxt;		/* Further words */
static int dolcnt;		/* Count of further words */
static struct Strbuf dolmod; /* = Strbuf_INIT; : modifier characters */
static int dolmcnt;		/* :gx -> INT_MAX, else 1 */
static int dol_flag_a;		/* :ax -> 1, else 0 */

static	Char	 **Dfix2	(Char *const *);
static	int 	 Dpack		(struct Strbuf *);
static	int	 Dword		(struct blk_buf *);
static	void	 dolerror	(Char *);
static	eChar	 DgetC		(int);
static	void	 Dgetdol	(void);
static	void	 fixDolMod	(void);
static	void	 setDolp	(Char *);
static	void	 unDredc	(eChar);
static	eChar	 Dredc		(void);
static	void	 Dtestq		(Char);

/*
 * Fix up the $ expansions and quotations in the
 * argument list to command t.
 */
void
Dfix(struct command *t)
{
    Char **pp;
    Char *p;

    if (noexec)
	return;
    /* Note that t_dcom isn't trimmed thus !...:q's aren't lost */
    for (pp = t->t_dcom; (p = *pp++) != NULL;) {
	for (; *p; p++) {
	    if (cmap(*p, _DOL | QUOTES)) {	/* $, \, ', ", ` */
		Char **expanded;

		expanded = Dfix2(t->t_dcom);	/* found one */
		blkfree(t->t_dcom);
		t->t_dcom = expanded;
		return;
	    }
	}
    }
}

/*
 * $ substitute one word, for i/o redirection
 */
Char   *
Dfix1(Char *cp)
{
    Char *Dv[2], **expanded;

    if (noexec)
	return (0);
    Dv[0] = cp;
    Dv[1] = NULL;
    expanded = Dfix2(Dv);
    if (expanded[0] == NULL || expanded[1] != NULL) {
	blkfree(expanded);
	setname(short2str(cp));
	stderror(ERR_NAME | ERR_AMBIG);
    }
    cp = Strsave(expanded[0]);
    blkfree(expanded);
    return (cp);
}

/*
 * Subroutine to do actual fixing after state initialization.
 */
static Char **
Dfix2(Char *const *v)
{
    struct blk_buf *bb = bb_alloc();
    Char **vec;

    Dvp = v;
    Dcp = STRNULL;		/* Setup input vector for Dreadc */
    unDgetC(0);
    unDredc(0);			/* Clear out any old peeks (at error) */
    dolp = 0;
    dolcnt = 0;			/* Clear out residual $ expands (...) */
    cleanup_push(bb, bb_free);
    while (Dword(bb))
	continue;
    cleanup_ignore(bb);
    cleanup_until(bb);
    vec = bb_finish(bb);
    xfree(bb);
    return vec;
}

/*
 * Pack up more characters in this word
 */
static int
Dpack(struct Strbuf *wbuf)
{
    eChar c;

    for (;;) {
	c = DgetC(DODOL);
	if (c == '\\') {
	    c = DgetC(0);
	    if (c == DEOF) {
		unDredc(c);
		return 1;
	    }
	    if (c == '\n')
		c = ' ';
	    else
		c |= QUOTE;
	}
	if (c == DEOF) {
	    unDredc(c);
	    return 1;
	}
	if (cmap(c, _SP | _NL | _QF | _QB)) {	/* sp \t\n'"` */
	    unDgetC(c);
	    if (cmap(c, QUOTES))
		return 0;
	    return 1;
	}
	Strbuf_append1(wbuf, (Char) c);
    }
}

/*
 * Get a word.  This routine is analogous to the routine
 * word() in sh.lex.c for the main lexical input.  One difference
 * here is that we don't get a newline to terminate our expansion.
 * Rather, DgetC will return a DEOF when we hit the end-of-input.
 */
static int
Dword(struct blk_buf *bb)
{
    eChar c, c1;
    struct Strbuf *wbuf = Strbuf_alloc();
    int dolflg;
    int    sofar = 0;
    Char *str;

    cleanup_push(wbuf, Strbuf_free);
    for (;;) {
	c = DgetC(DODOL);
	switch (c) {

	case DEOF:
	    if (sofar == 0) {
		cleanup_until(wbuf);
		return (0);
	    }
	    /* finish this word and catch the code above the next time */
	    unDredc(c);
	    /*FALLTHROUGH*/

	case '\n':
	    goto end;

	case ' ':
	case '\t':
	    continue;

	case '`':
	    /* We preserve ` quotations which are done yet later */
	    Strbuf_append1(wbuf, (Char) c);
	    /*FALLTHROUGH*/
	case '\'':
	case '"':
	    /*
	     * Note that DgetC never returns a QUOTES character from an
	     * expansion, so only true input quotes will get us here or out.
	     */
	    c1 = c;
	    dolflg = c1 == '"' ? DODOL : 0;
	    for (;;) {
		c = DgetC(dolflg);
		if (c == c1)
		    break;
		if (c == '\n' || c == DEOF) {
		    cleanup_until(bb);
		    stderror(ERR_UNMATCHED, (int)c1);
		}
		if ((c & (QUOTE | TRIM)) == ('\n' | QUOTE)) {
		    if (wbuf->len != 0 && (wbuf->s[wbuf->len - 1] & TRIM) == '\\')
			wbuf->len--;
		}
		switch (c1) {

		case '"':
		    /*
		     * Leave any `s alone for later. Other chars are all
		     * quoted, thus `...` can tell it was within "...".
		     */
		    Strbuf_append1(wbuf, c == '`' ? '`' : c | QUOTE);
		    break;

		case '\'':
		    /* Prevent all further interpretation */
		    Strbuf_append1(wbuf, c | QUOTE);
		    break;

		case '`':
		    /* Leave all text alone for later */
		    Strbuf_append1(wbuf, (Char) c);
		    break;

		default:
		    break;
		}
	    }
	    if (c1 == '`')
		Strbuf_append1(wbuf, '`');
	    sofar = 1;
	    if (Dpack(wbuf) != 0)
		goto end;
	    continue;

	case '\\':
	    c = DgetC(0);	/* No $ subst! */
	    if (c == '\n' || c == DEOF)
		continue;
	    c |= QUOTE;
	    break;

	default:
	    break;
	}
	unDgetC(c);
	sofar = 1;
	if (Dpack(wbuf) != 0)
	    goto end;
    }

 end:
    cleanup_ignore(wbuf);
    cleanup_until(wbuf);
    str = Strbuf_finish(wbuf);
    bb_append(bb, str);
    xfree(wbuf);
    return 1;
}


/*
 * Get a character, performing $ substitution unless flag is 0.
 * Any QUOTES character which is returned from a $ expansion is
 * QUOTEd so that it will not be recognized above.
 */
static eChar
DgetC(int flag)
{
    eChar c;

top:
    if ((c = Dpeekc) != 0) {
	Dpeekc = 0;
	return (c);
    }
    if (lap < labuf.len) {
	c = labuf.s[lap++] & (QUOTE | TRIM);
quotspec:
	if (cmap(c, QUOTES))
	    return (c | QUOTE);
	return (c);
    }
    if (dolp) {
	if ((c = *dolp++ & (QUOTE | TRIM)) != 0)
	    goto quotspec;
	if (dolcnt > 0) {
	    setDolp(*dolnxt++);
	    --dolcnt;
	    return (' ');
	}
	dolp = 0;
    }
    if (dolcnt > 0) {
	setDolp(*dolnxt++);
	--dolcnt;
	goto top;
    }
    c = Dredc();
    if (c == '$' && flag) {
	Dgetdol();
	goto top;
    }
    return (c);
}

static Char *nulvec[] = { NULL };
static struct varent nulargv = {nulvec, STRargv, VAR_READWRITE, 
				{ NULL, NULL, NULL }, 0 };

static void
dolerror(Char *s)
{
    setname(short2str(s));
    stderror(ERR_NAME | ERR_RANGE);
}

/*
 * Handle the multitudinous $ expansion forms.
 * Ugh.
 */
static void
Dgetdol(void)
{
    Char *np;
    struct varent *vp = NULL;
    struct Strbuf *name = Strbuf_alloc();
    eChar   c, sc;
    int     subscr = 0, lwb = 1, upb = 0;
    int    dimen = 0, bitset = 0, length = 0;
    static Char *dolbang = NULL;

    cleanup_push(name, Strbuf_free);
    dolmod.len = dolmcnt = dol_flag_a = 0;
    c = sc = DgetC(0);
    if (c == DEOF) {
      stderror(ERR_SYNTAX);
      return;
    }
    if (c == '{')
	c = DgetC(0);		/* sc is { to take } later */
    if ((c & TRIM) == '#')
	dimen++, c = DgetC(0);	/* $# takes dimension */
    else if (c == '?')
	bitset++, c = DgetC(0);	/* $? tests existence */
    else if (c == '%')
	length++, c = DgetC(0); /* $% returns length in chars */
    switch (c) {

    case '!':
	if (dimen || bitset || length)
	    stderror(ERR_SYNTAX);
	if (backpid != 0) {
	    xfree(dolbang);
	    setDolp(dolbang = putn((tcsh_number_t)backpid));
	}
	cleanup_until(name);
	goto eatbrac;

    case '$':
	if (dimen || bitset || length)
	    stderror(ERR_SYNTAX);
	setDolp(doldol);
	cleanup_until(name);
	goto eatbrac;

    case '<'|QUOTE: {
	static struct Strbuf wbuf; /* = Strbuf_INIT; */

	if (bitset)
	    stderror(ERR_NOTALLOWED, "$?<");
	if (dimen)
	    stderror(ERR_NOTALLOWED, "$#<");
	if (length)
	    stderror(ERR_NOTALLOWED, "$%<");
	wbuf.len = 0;
	{
	    char cbuf[MB_LEN_MAX];
	    size_t cbp = 0;
	    int old_pintr_disabled;

	    for (;;) {
	        int len;
		ssize_t res;
		Char wc;

		pintr_push_enable(&old_pintr_disabled);
		res = force_read(OLDSTD, cbuf + cbp, 1);
		cleanup_until(&old_pintr_disabled);
		if (res != 1)
		    break;
		cbp++;
		len = normal_mbtowc(&wc, cbuf, cbp);
		if (len == -1) {
		    reset_mbtowc();
		    if (cbp < MB_LEN_MAX)
		        continue; /* Maybe a partial character */
		    wc = (unsigned char)*cbuf | INVALID_BYTE;
		}
		if (len <= 0)
		    len = 1;
		if (cbp != (size_t)len)
		    memmove(cbuf, cbuf + len, cbp - len);
		cbp -= len;
		if (wc == '\n')
		    break;
		Strbuf_append1(&wbuf, wc);
	    }
	    while (cbp != 0) {
		int len;
		Char wc;

		len = normal_mbtowc(&wc, cbuf, cbp);
		if (len == -1) {
		    reset_mbtowc();
		    wc = (unsigned char)*cbuf | INVALID_BYTE;
		}
		if (len <= 0)
		    len = 1;
		if (cbp != (size_t)len)
		    memmove(cbuf, cbuf + len, cbp - len);
		cbp -= len;
		if (wc == '\n')
		    break;
		Strbuf_append1(&wbuf, wc);
	    }
	    Strbuf_terminate(&wbuf);
	}

	fixDolMod();
	setDolp(wbuf.s); /* Kept allocated until next $< expansion */
	cleanup_until(name);
	goto eatbrac;
    }

    case '*':
	Strbuf_append(name, STRargv);
	Strbuf_terminate(name);
	vp = adrof(STRargv);
	subscr = -1;		/* Prevent eating [...] */
	break;

    case DEOF:
    case '\n':
	np = dimen ? STRargv : (bitset ? STRstatus : NULL);
	if (np) {
	    bitset = 0;
	    Strbuf_append(name, np);
	    Strbuf_terminate(name);
	    vp = adrof(np);
	    subscr = -1;		/* Prevent eating [...] */
	    unDredc(c);
	    break;
	}
	else
	    stderror(ERR_SYNTAX);
	/*NOTREACHED*/

    default:
	if (Isdigit(c)) {
	    if (dimen)
		stderror(ERR_NOTALLOWED, "$#<num>");
	    subscr = 0;
	    do {
		subscr = subscr * 10 + c - '0';
		c = DgetC(0);
	    } while (c != DEOF && Isdigit(c));
	    unDredc(c);
	    if (subscr < 0)
		stderror(ERR_RANGE);
	    if (subscr == 0) {
		if (bitset) {
		    dolp = dolzero ? STR1 : STR0;
		    cleanup_until(name);
		    goto eatbrac;
		}
		if (ffile == 0)
		    stderror(ERR_DOLZERO);
		if (length) {
		    length = Strlen(ffile);
		    addla(putn((tcsh_number_t)length));
		}
		else {
		    fixDolMod();
		    setDolp(ffile);
		}
		cleanup_until(name);
		goto eatbrac;
	    }
#if 0
	    if (bitset)
		stderror(ERR_NOTALLOWED, "$?<num>");
	    if (length)
		stderror(ERR_NOTALLOWED, "$%<num>");
#endif
	    vp = adrof(STRargv);
	    if (vp == 0) {
		vp = &nulargv;
		cleanup_until(name);
		goto eatmod;
	    }
	    break;
	}
	if (c == DEOF || !alnum(c)) {
	    np = dimen ? STRargv : (bitset ? STRstatus : NULL);
	    if (np) {
		bitset = 0;
		Strbuf_append(name, np);
		Strbuf_terminate(name);
		vp = adrof(np);
		subscr = -1;		/* Prevent eating [...] */
		unDredc(c);
		break;
	    }
	    else
		stderror(ERR_VARALNUM);
	}
	for (;;) {
	    Strbuf_append1(name, (Char) c);
	    c = DgetC(0);
	    if (c == DEOF || !alnum(c))
		break;
	}
	Strbuf_terminate(name);
	unDredc(c);
	vp = adrof(name->s);
    }
    if (bitset) {
	dolp = (vp || getenv(short2str(name->s))) ? STR1 : STR0;
	cleanup_until(name);
	goto eatbrac;
    }
    if (vp == NULL || vp->vec == NULL) {
	np = str2short(getenv(short2str(name->s)));
	if (np) {
	    static Char *env_val; /* = NULL; */

	    cleanup_until(name);
	    fixDolMod();
	    if (length) {
		    addla(putn((tcsh_number_t)Strlen(np)));
	    } else {
		    xfree(env_val);
		    env_val = Strsave(np);
		    setDolp(env_val);
	    }
	    goto eatbrac;
	}
	udvar(name->s);
	/* NOTREACHED */
    }
    cleanup_until(name);
    c = DgetC(0);
    upb = blklen(vp->vec);
    if (dimen == 0 && subscr == 0 && c == '[') {
	name = Strbuf_alloc();
	cleanup_push(name, Strbuf_free);
	np = name->s;
	for (;;) {
	    c = DgetC(DODOL);	/* Allow $ expand within [ ] */
	    if (c == ']')
		break;
	    if (c == '\n' || c == DEOF)
		stderror(ERR_INCBR);
	    Strbuf_append1(name, (Char) c);
	}
	Strbuf_terminate(name);
	np = name->s;
	if (dolp || dolcnt)	/* $ exp must end before ] */
	    stderror(ERR_EXPORD);
	if (!*np)
	    stderror(ERR_SYNTAX);
	if (Isdigit(*np)) {
	    int     i;

	    for (i = 0; Isdigit(*np); i = i * 10 + *np++ - '0')
		continue;
	    if (i < 0 || (i > upb && !any("-*", *np))) {
		cleanup_until(name);
		dolerror(vp->v_name);
		return;
	    }
	    lwb = i;
	    if (!*np)
		upb = lwb, np = STRstar;
	}
	if (*np == '*')
	    np++;
	else if (*np != '-')
	    stderror(ERR_MISSING, '-');
	else {
	    int i = upb;

	    np++;
	    if (Isdigit(*np)) {
		i = 0;
		while (Isdigit(*np))
		    i = i * 10 + *np++ - '0';
		if (i < 0 || i > upb) {
		    cleanup_until(name);
		    dolerror(vp->v_name);
		    return;
		}
	    }
	    if (i < lwb)
		upb = lwb - 1;
	    else
		upb = i;
	}
	if (lwb == 0) {
	    if (upb != 0) {
		cleanup_until(name);
		dolerror(vp->v_name);
		return;
	    }
	    upb = -1;
	}
	if (*np)
	    stderror(ERR_SYNTAX);
	cleanup_until(name);
    }
    else {
	if (subscr > 0) {
	    if (subscr > upb)
		lwb = 1, upb = 0;
	    else
		lwb = upb = subscr;
	}
	unDredc(c);
    }
    if (dimen) {
	/* this is a kludge. It prevents Dgetdol() from */
	/* pushing erroneous ${#<error> values into the labuf. */
	if (sc == '{') {
	    c = Dredc();
	    if (c != '}')
		stderror(ERR_MISSING, '}');
	    unDredc(c);
	}
	addla(putn((tcsh_number_t)(upb - lwb + 1)));
    }
    else if (length) {
	int i;

	for (i = lwb - 1, length = 0; i < upb; i++)
	    length += Strlen(vp->vec[i]);
#ifdef notdef
	/* We don't want that, since we can always compute it by adding $#xxx */
	length += i - 1;	/* Add the number of spaces in */
#endif
	addla(putn((tcsh_number_t)length));
    }
    else {
eatmod:
	fixDolMod();
	dolnxt = &vp->vec[lwb - 1];
	dolcnt = upb - lwb + 1;
    }
eatbrac:
    if (sc == '{') {
	c = Dredc();
	if (c != '}')
	    stderror(ERR_MISSING, '}');
    }
}

static void
fixDolMod(void)
{
    eChar c;

    c = DgetC(0);
    if (c == ':') {
	do {
	    c = DgetC(0), dolmcnt = 1, dol_flag_a = 0;
	    if (c == 'g' || c == 'a') {
		if (c == 'g')
		    dolmcnt = INT_MAX;
		else
		    dol_flag_a = 1;
		c = DgetC(0);
	    }
	    if ((c == 'g' && dolmcnt != INT_MAX) || 
		(c == 'a' && dol_flag_a == 0)) {
		if (c == 'g')
		    dolmcnt = INT_MAX;
		else
		    dol_flag_a = 1;
		c = DgetC(0);
	    }

	    if (c == 's') {	/* [eichin:19910926.0755EST] */
		int delimcnt = 2;
		eChar delim = DgetC(0);
		Strbuf_append1(&dolmod, (Char) c);
		Strbuf_append1(&dolmod, (Char) delim);

		if (delim == DEOF || !delim || letter(delim)
		    || Isdigit(delim) || any(" \t\n", delim)) {
		    seterror(ERR_BADSUBST);
		    break;
		}	
		while ((c = DgetC(0)) != DEOF) {
		    Strbuf_append1(&dolmod, (Char) c);
		    if(c == delim) delimcnt--;
		    if(!delimcnt) break;
		}
		if(delimcnt) {
		    seterror(ERR_BADSUBST);
		    break;
		}
		continue;
	    }
	    if (!any("luhtrqxes", c))
		stderror(ERR_BADMOD, (int)c);
	    Strbuf_append1(&dolmod, (Char) c);
	    if (c == 'q')
		dolmcnt = INT_MAX;
	}
	while ((c = DgetC(0)) == ':');
	unDredc(c);
    }
    else
	unDredc(c);
}

static void
setDolp(Char *cp)
{
    Char *dp;
    size_t i;

    if (dolmod.len == 0 || dolmcnt == 0) {
	dolp = cp;
	return;
    }
    cp = Strsave(cp);
    for (i = 0; i < dolmod.len; i++) {
	int didmod = 0;

	/* handle s// [eichin:19910926.0510EST] */
	if(dolmod.s[i] == 's') {
	    Char delim;
	    Char *lhsub, *rhsub, *np;
	    size_t lhlen = 0, rhlen = 0;

	    delim = dolmod.s[++i];
	    if (!delim || letter(delim)
		|| Isdigit(delim) || any(" \t\n", delim)) {
		seterror(ERR_BADSUBST);
		break;
	    }
	    lhsub = &dolmod.s[++i];
	    while(dolmod.s[i] != delim && dolmod.s[++i]) {
		lhlen++;
	    }
	    dolmod.s[i] = 0;
	    rhsub = &dolmod.s[++i];
	    while(dolmod.s[i] != delim && dolmod.s[++i]) {
		rhlen++;
	    }
	    dolmod.s[i] = 0;

	    strip(lhsub);
	    strip(rhsub);
	    strip(cp);
	    dp = cp;
	    do {
		dp = Strstr(dp, lhsub);
		if (dp) {
		    ptrdiff_t diff = dp - cp;
		    size_t len = (Strlen(cp) + 1 - lhlen + rhlen);
		    np = xmalloc(len * sizeof(Char));
		    (void) Strncpy(np, cp, diff);
		    (void) Strcpy(np + diff, rhsub);
		    (void) Strcpy(np + diff + rhlen, dp + lhlen);

		    xfree(cp);
		    dp = cp = np;
		    cp[--len] = '\0';
		    didmod = 1;
		    if (diff >= (ssize_t)len)
			break;
		} else {
		    /* should this do a seterror? */
		    break;
		}
	    }
	    while (dol_flag_a != 0);
	    /*
	     * restore dolmod for additional words
	     */
	    dolmod.s[i] = rhsub[-1] = (Char) delim;
        } else {

	    do {
		if ((dp = domod(cp, dolmod.s[i])) != NULL) {
		    didmod = 1;
		    if (Strcmp(cp, dp) == 0) {
			xfree(cp);
			cp = dp;
			break;
		    }
		    else {
			xfree(cp);
			cp = dp;
		    }
		}
		else
		    break;
	    }
	    while (dol_flag_a != 0);
	}
	if (didmod && dolmcnt != INT_MAX)
	    dolmcnt--;
#ifdef notdef
	else
	    break;
#endif
    }

    addla(cp);

    dolp = STRNULL;
    if (seterr)
	stderror(ERR_OLD);
}

static void
unDredc(eChar c)
{

    Dpeekrd = c;
}

static eChar
Dredc(void)
{
    eChar c;

    if ((c = Dpeekrd) != 0) {
	Dpeekrd = 0;
	return (c);
    }
    if (Dcp && (c = *Dcp++))
	return (c & (QUOTE | TRIM));
    if (*Dvp == 0) {
	Dcp = 0;
	return (DEOF);
    }
    Dcp = *Dvp++;
    return (' ');
}

static int gflag;

static void
Dtestq(Char c)
{

    if (cmap(c, QUOTES))
	gflag = 1;
}

static void
inheredoc_cleanup(void *dummy)
{
    USE(dummy);
    inheredoc = 0;
}

Char *
randsuf(void) {
#ifndef WINNT_NATIVE
	struct timeval tv;
	(void) gettimeofday(&tv, NULL);
	return putn((((tcsh_number_t)tv.tv_sec) ^ 
	    ((tcsh_number_t)tv.tv_usec) ^
	    ((tcsh_number_t)getpid())) & 0x00ffffff);
#else
    return putn(getpid());
#endif
}

/*
 * Form a shell temporary file (in unit 0) from the words
 * of the shell input up to EOF or a line the same as "term".
 * Unit 0 should have been closed before this call.
 */
void
heredoc(Char *term)
{
    eChar  c;
    Char   *Dv[2];
    struct Strbuf lbuf = Strbuf_INIT, mbuf = Strbuf_INIT;
    Char    obuf[BUFSIZE + 1];
#define OBUF_END (obuf + sizeof(obuf) / sizeof (*obuf) - 1)
    Char *lbp, *obp, *mbp;
    Char  **vp;
    int    quoted;
#ifdef HAVE_MKSTEMP
    char   *tmp = short2str(shtemp);
    char   *dot = strrchr(tmp, '.');

    if (!dot)
	stderror(ERR_NAME | ERR_NOMATCH);
    strcpy(dot, TMP_TEMPLATE);

    xclose(0);
    if (mkstemp(tmp) == -1)
	stderror(ERR_SYSTEM, tmp, strerror(errno));
#else /* !HAVE_MKSTEMP */
    char   *tmp;
# ifndef WINNT_NATIVE

again:
# endif /* WINNT_NATIVE */
    tmp = short2str(shtemp);
# if O_CREAT == 0
    if (xcreat(tmp, 0600) < 0)
	stderror(ERR_SYSTEM, tmp, strerror(errno));
# endif
    xclose(0);
    if (xopen(tmp, O_RDWR|O_CREAT|O_EXCL|O_TEMPORARY|O_LARGEFILE, 0600) ==
	-1) {
	int oerrno = errno;
# ifndef WINNT_NATIVE
	if (errno == EEXIST) {
	    if (unlink(tmp) == -1) {
		xfree(shtemp);
		mbp = randsuf();
		shtemp = Strspl(STRtmpsh, mbp);
		xfree(mbp);
	    }
	    goto again;
	}
# endif /* WINNT_NATIVE */
	(void) unlink(tmp);
	errno = oerrno;
 	stderror(ERR_SYSTEM, tmp, strerror(errno));
    }
#endif /* HAVE_MKSTEMP */
    (void) unlink(tmp);		/* 0 0 inode! */
    Dv[0] = term;
    Dv[1] = NULL;
    gflag = 0;
    trim(Dv);
    rscan(Dv, Dtestq);
    quoted = gflag;
    obp = obuf;
    obuf[BUFSIZE] = 0;
    inheredoc = 1;
    cleanup_push(&inheredoc, inheredoc_cleanup);
#ifdef WINNT_NATIVE
    __dup_stdin = 1;
#endif /* WINNT_NATIVE */
    cleanup_push(&lbuf, Strbuf_cleanup);
    cleanup_push(&mbuf, Strbuf_cleanup);
    for (;;) {
	Char **words;

	/*
	 * Read up a line
	 */
	lbuf.len = 0;
	for (;;) {
	    c = readc(1);	/* 1 -> Want EOF returns */
	    if (c == CHAR_ERR || c == '\n')
		break;
	    if ((c &= TRIM) != 0)
		Strbuf_append1(&lbuf, (Char) c);
	}
	Strbuf_terminate(&lbuf);

	/* Catch EOF in the middle of a line. */
	if (c == CHAR_ERR && lbuf.len != 0)
	    c = '\n';

	/*
	 * Check for EOF or compare to terminator -- before expansion
	 */
	if (c == CHAR_ERR || eq(lbuf.s, term))
	    break;

	/*
	 * If term was quoted or -n just pass it on
	 */
	if (quoted || noexec) {
	    Strbuf_append1(&lbuf, '\n');
	    Strbuf_terminate(&lbuf);
	    for (lbp = lbuf.s; (c = *lbp++) != 0;) {
		*obp++ = (Char) c;
		if (obp == OBUF_END) {
		    tmp = short2str(obuf);
		    (void) xwrite(0, tmp, strlen (tmp));
		    obp = obuf;
		}
	    }
	    continue;
	}

	/*
	 * Term wasn't quoted so variable and then command expand the input
	 * line
	 */
	Dcp = lbuf.s;
	Dvp = Dv + 1;
	mbuf.len = 0;
	for (;;) {
	    c = DgetC(DODOL);
	    if (c == DEOF)
		break;
	    if ((c &= TRIM) == 0)
		continue;
	    /* \ quotes \ $ ` here */
	    if (c == '\\') {
		c = DgetC(0);
		if (!any("$\\`", c))
		    unDgetC(c | QUOTE), c = '\\';
		else
		    c |= QUOTE;
	    }
	    Strbuf_append1(&mbuf, (Char) c);
	}
	Strbuf_terminate(&mbuf);

	/*
	 * If any ` in line do command substitution
	 */
	mbp = mbuf.s;
	if (Strchr(mbp, '`') != NULL) {
	    /*
	     * 1 arg to dobackp causes substitution to be literal. Words are
	     * broken only at newlines so that all blanks and tabs are
	     * preserved.  Blank lines (null words) are not discarded.
	     */
	    words = dobackp(mbp, 1);
	}
	else
	    /* Setup trivial vector similar to return of dobackp */
	    Dv[0] = mbp, Dv[1] = NULL, words = Dv;

	/*
	 * Resurrect the words from the command substitution each separated by
	 * a newline.  Note that the last newline of a command substitution
	 * will have been discarded, but we put a newline after the last word
	 * because this represents the newline after the last input line!
	 */
	for (vp= words; *vp; vp++) {
	    for (mbp = *vp; *mbp; mbp++) {
		*obp++ = *mbp & TRIM;
		if (obp == OBUF_END) {
		    tmp = short2str(obuf);
		    (void) xwrite(0, tmp, strlen (tmp));
		    obp = obuf;
		}
	    }
	    *obp++ = '\n';
	    if (obp == OBUF_END) {
	        tmp = short2str(obuf);
		(void) xwrite(0, tmp, strlen (tmp));
		obp = obuf;
	    }
	}
	if (words != Dv)
	    blkfree(words);
    }
    *obp = 0;
    tmp = short2str(obuf);
    (void) xwrite(0, tmp, strlen (tmp));
    (void) lseek(0, (off_t) 0, L_SET);
    cleanup_until(&inheredoc);
}
