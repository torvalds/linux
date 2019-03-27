/* $Header: /p/tcsh/cvsroot/tcsh/sh.lex.c,v 3.91 2016/08/01 16:21:09 christos Exp $ */
/*
 * sh.lex.c: Lexical analysis into tokens
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

RCSID("$tcsh: sh.lex.c,v 3.91 2016/08/01 16:21:09 christos Exp $")

#include "ed.h"

#include <assert.h>
/* #define DEBUG_INP */
/* #define DEBUG_SEEK */

/*
 * C shell
 */

#define FLAG_G	1
#define FLAG_A	2
/*
 * These lexical routines read input and form lists of words.
 * There is some involved processing here, because of the complications
 * of input buffering, and especially because of history substitution.
 */
static	Char		*word		(int);
static	eChar	 	 getC1		(int);
static	void	 	 getdol		(void);
static	void	 	 getexcl	(Char);
static	struct Hist 	*findev		(Char *, int);
static	void	 	 setexclp	(Char *);
static	eChar	 	 bgetc		(void);
static	void		 balloc		(int);
static	void	 	 bfree		(void);
static	struct wordent	*gethent	(Char);
static	int	 	 matchs		(const Char *, const Char *);
static	int	 	 getsel		(int *, int *, int);
static	struct wordent	*getsub		(struct wordent *);
static	Char 		*subword	(Char *, Char, int *, size_t *);
static	struct wordent	*dosub		(Char, struct wordent *, int);

/*
 * Peekc is a peek character for getC, peekread for readc.
 * There is a subtlety here in many places... history routines
 * will read ahead and then insert stuff into the input stream.
 * If they push back a character then they must push it behind
 * the text substituted by the history substitution.  On the other
 * hand in several places we need 2 peek characters.  To make this
 * all work, the history routines read with getC, and make use both
 * of ungetC and unreadc.  The key observation is that the state
 * of getC at the call of a history reference is such that calls
 * to getC from the history routines will always yield calls of
 * readc, unless this peeking is involved.  That is to say that during
 * getexcl the variables lap, exclp, and exclnxt are all zero.
 *
 * Getdol invokes history substitution, hence the extra peek, peekd,
 * which it can ungetD to be before history substitutions.
 */
static Char peekc = 0, peekd = 0;
static Char peekread = 0;

/* (Tail of) current word from ! subst */
static Char *exclp = NULL;

/* The rest of the ! subst words */
static struct wordent *exclnxt = NULL;

/* Count of remaining words in ! subst */
static int exclc = 0;

/* "Globp" for alias resubstitution */
int aret = TCSH_F_SEEK;

/*
 * Labuf implements a general buffer for lookahead during lexical operations.
 * Text which is to be placed in the input stream can be stuck here.
 * We stick parsed ahead $ constructs during initial input,
 * process id's from `$$', and modified variable values (from qualifiers
 * during expansion in sh.dol.c) here.
 */
struct Strbuf labuf; /* = Strbuf_INIT; */

/*
 * Lex returns to its caller not only a wordlist (as a "var" parameter)
 * but also whether a history substitution occurred.  This is used in
 * the main (process) routine to determine whether to echo, and also
 * when called by the alias routine to determine whether to keep the
 * argument list.
 */
static int hadhist = 0;

/*
 * Avoid alias expansion recursion via \!#
 */
int     hleft;

struct Strbuf histline; /* = Strbuf_INIT; last line input */

int    histvalid = 0;		/* is histline valid */

static Char getCtmp;

#define getC(f)		(((getCtmp = peekc) != '\0') ? (peekc = 0, (eChar)getCtmp) : getC1(f))
#define	ungetC(c)	peekc = (Char) c
#define	ungetD(c)	peekd = (Char) c

/* Use Htime to store timestamps picked up from history file for enthist()
 * if reading saved history (sg)
 */
time_t Htime = (time_t)0;
static time_t a2time_t (Char *);

/*
 * special parsing rules apply for source -h
 */
extern int enterhist;

int
lex(struct wordent *hp)
{
    struct wordent *wdp;
    eChar    c;
    int     parsehtime = enterhist;

    histvalid = 0;
    histline.len = 0;

    btell(&lineloc);
    hp->next = hp->prev = hp;
    hp->word = STRNULL;
    hadhist = 0;
    do
	c = readc(0);
    while (c == ' ' || c == '\t');
    if (c == (eChar)HISTSUB && intty)
	/* ^lef^rit	from tty is short !:s^lef^rit */
	getexcl(c);
    else
	unreadc(c);
    cleanup_push(hp, lex_cleanup);
    wdp = hp;
    /*
     * The following loop is written so that the links needed by freelex will
     * be ready and rarin to go even if it is interrupted.
     */
    do {
	struct wordent *new;

	new = xmalloc(sizeof(*new));
	new->word = NULL;
	new->prev = wdp;
	new->next = hp;
	wdp->next = new;
	hp->prev = new;
	wdp = new;
	wdp->word = word(parsehtime);
	parsehtime = 0;
    } while (wdp->word[0] != '\n');
    cleanup_ignore(hp);
    cleanup_until(hp);
    Strbuf_terminate(&histline);
    if (histline.len != 0 && histline.s[histline.len - 1] == '\n')
	histline.s[histline.len - 1] = '\0';
    histvalid = 1;

    return (hadhist);
}

static time_t
a2time_t(Char *wordx)
{
    /* Attempt to distinguish timestamps from other possible entries.
     * Format: "+NNNNNNNNNN" (10 digits, left padded with ascii '0') */

    time_t ret;
    Char *s;
    int ct;

    if (!wordx || *(s = wordx) != '+')
	return (time_t)0;

    for (++s, ret = 0, ct = 0; *s; ++s, ++ct) {
	if (!isdigit((unsigned char)*s))
	    return (time_t)0;
	ret = ret * 10 + (time_t)((unsigned char)*s - '0');
    }

    if (ct != 10)
	return (time_t)0;

    return ret;
}

void
prlex(struct wordent *sp0)
{
    struct wordent *sp = sp0->next;

    for (;;) {
	xprintf("%S", sp->word);
	sp = sp->next;
	if (sp == sp0)
	    break;
	if (sp->word[0] != '\n')
	    xputchar(' ');
    }
}

void
copylex(struct wordent *hp, struct wordent *fp)
{
    struct wordent *wdp;

    wdp = hp;
    fp = fp->next;
    do {
	struct wordent *new;

	new = xmalloc(sizeof(*new));
	new->word = NULL;
	new->prev = wdp;
	new->next = hp;
	wdp->next = new;
	hp->prev = new;
	wdp = new;
	wdp->word = Strsave(fp->word);
	fp = fp->next;
    } while (wdp->word[0] != '\n');
}

void
initlex(struct wordent *vp)
{
	vp->word = STRNULL;
	vp->prev = vp;
	vp->next = vp;
}

void
freelex(struct wordent *vp)
{
    struct wordent *fp;

    while (vp->next != vp) {
	fp = vp->next;
	vp->next = fp->next;
	xfree(fp->word);
	xfree(fp);
    }
    vp->prev = vp;
}

void
lex_cleanup(void *xvp)
{
    struct wordent *vp;

    vp = xvp;
    freelex(vp);
}

static Char *
word(int parsehtime)
{
    eChar c, c1;
    struct Strbuf wbuf = Strbuf_INIT;
    Char    hbuf[12];
    int	    h;
    int dolflg;

    cleanup_push(&wbuf, Strbuf_cleanup);
loop:
    while ((c = getC(DOALL)) == ' ' || c == '\t')
	continue;
    if (cmap(c, _META | _ESC))
	switch (c) {
	case '&':
	case '|':
	case '<':
	case '>':
	    Strbuf_append1(&wbuf, c);
	    c1 = getC(DOALL);
	    if (c1 == c)
		Strbuf_append1(&wbuf, c1);
	    else
		ungetC(c1);
	    goto ret;

	case '#':
	    if (intty || (enterhist && !parsehtime))
		break;
	    c = 0;
	    h = 0;
	    do {
		c1 = c;
		c = getC(0);
		if (h < 11 && parsehtime)
		    hbuf[h++] = c;
	    } while (c != '\n');
	    if (parsehtime) {
		hbuf[11] = '\0';
		Htime = a2time_t(hbuf); 
	    }
	    if (c1 == '\\')
		goto loop;
	    /*FALLTHROUGH*/

	case ';':
	case '(':
	case ')':
	case '\n':
	    Strbuf_append1(&wbuf, c);
	    goto ret;

	case '\\':
	    c = getC(0);
	    if (c == '\n') {
		if (onelflg == 1)
		    onelflg = 2;
		goto loop;
	    }
	    if (c != (eChar)HIST)
		Strbuf_append1(&wbuf, '\\');
	    c |= QUOTE;
	default:
	    break;
	}
    c1 = 0;
    dolflg = DOALL;
    for (;;) {
	if (c1) {
	    if (c == c1) {
		c1 = 0;
		dolflg = DOALL;
	    }
	    else if (c == '\\') {
		c = getC(0);
/*
 * PWP: this is dumb, but how all of the other shells work.  If \ quotes
 * a character OUTSIDE of a set of ''s, why shouldn't it quote EVERY
 * following character INSIDE a set of ''s.
 *
 * Actually, all I really want to be able to say is 'foo\'bar' --> foo'bar
 */
		if (c == (eChar)HIST)
		    c |= QUOTE;
		else {
		    if (bslash_quote &&
			((c == '\'') || (c == '"') ||
			 (c == '\\') || (c == '$'))) {
			c |= QUOTE;
		    }
		    else {
			if (c == '\n')
			    /*
			     * if (c1 == '`') c = ' '; else
			     */
			    c |= QUOTE;
			ungetC(c);
			c = '\\' | QUOTE;
		    }
		}
	    }
	    else if (c == '\n') {
		seterror(ERR_UNMATCHED, c1);
		ungetC(c);
		break;
	    }
	}
	else if (cmap(c, _META | _QF | _QB | _ESC)) {
	    if (c == '\\') {
		c = getC(0);
		if (c == '\n') {
		    if (onelflg == 1)
			onelflg = 2;
		    break;
		}
		if (c != (eChar)HIST)
		    Strbuf_append1(&wbuf, '\\');
		c |= QUOTE;
	    }
	    else if (cmap(c, _QF | _QB)) {	/* '"` */
		c1 = c;
		dolflg = c == '"' ? DOALL : DOEXCL;
	    }
	    else if (c != '#' || (!intty && !enterhist)) {
		ungetC(c);
		break;
	    }
	}
	Strbuf_append1(&wbuf, c);
	c = getC(dolflg);
    }
ret:
    cleanup_ignore(&wbuf);
    cleanup_until(&wbuf);
    return Strbuf_finish(&wbuf);
}

static eChar
getC1(int flag)
{
    eChar c;

    for (;;) {
	if ((c = peekc) != 0) {
	    peekc = 0;
	    return (c);
	}
	if (lap < labuf.len) {
	    c = labuf.s[lap++];
	    if (cmap(c, _META | _QF | _QB))
		c |= QUOTE;
	    return (c);
	}
	if ((c = peekd) != 0) {
	    peekd = 0;
	    return (c);
	}
	if (exclp) {
	    if ((c = *exclp++) != 0)
		return (c);
	    if (exclnxt && --exclc >= 0) {
		exclnxt = exclnxt->next;
		setexclp(exclnxt->word);
		return (' ');
	    }
	    exclp = 0;
	    exclnxt = 0;
	    /* this will throw away the dummy history entries */
	    savehist(NULL, 0);

	}
	if (exclnxt) {
	    exclnxt = exclnxt->next;
	    if (--exclc < 0)
		exclnxt = 0;
	    else
		setexclp(exclnxt->word);
	    continue;
	}
	c = readc(1);

	/* Catch EOF in the middle of a line.  (An EOF at the beginning of
	 * a line would have been processed by the readc(0) in lex().) */
	if (c == CHAR_ERR)
	    c = '\n';

	if (c == '$' && (flag & DODOL)) {
	    getdol();
	    continue;
	}
	if (c == (eChar)HIST && (flag & DOEXCL)) {
	    getexcl(0);
	    continue;
	}
	break;
    }
    return (c);
}

static void
getdol(void)
{
    struct Strbuf name = Strbuf_INIT;
    eChar c;
    eChar   sc;
    int    special = 0;

    c = sc = getC(DOEXCL);
    if (any("\t \n", c)) {
	ungetD(c);
	ungetC('$' | QUOTE);
	return;
    }
    cleanup_push(&name, Strbuf_cleanup);
    Strbuf_append1(&name, '$');
    if (c == '{')
	Strbuf_append1(&name, c), c = getC(DOEXCL);
    if (c == '#' || c == '?' || c == '%')
	special++, Strbuf_append1(&name, c), c = getC(DOEXCL);
    Strbuf_append1(&name, c);
    switch (c) {

    case '<':
    case '$':
    case '!':
	if (special)
	    seterror(ERR_SPDOLLT);
	goto end;

    case '\n':
	ungetD(c);
	name.len--;
	if (!special)
	    seterror(ERR_NEWLINE);
	goto end;

    case '*':
	if (special)
	    seterror(ERR_SPSTAR);
	goto end;

    default:
	if (Isdigit(c)) {
#ifdef notdef
	    /* let $?0 pass for now */
	    if (special) {
		seterror(ERR_DIGIT);
		goto end;
	    }
#endif
	    while ((c = getC(DOEXCL)) != 0) {
		if (!Isdigit(c))
		    break;
		Strbuf_append1(&name, c);
	    }
	}
	else if (letter(c)) {
	    while ((c = getC(DOEXCL)) != 0) {
		/* Bugfix for ${v123x} from Chris Torek, DAS DEC-90. */
		if (!letter(c) && !Isdigit(c))
		    break;
		Strbuf_append1(&name, c);
	    }
	}
	else {
	    if (!special)
		seterror(ERR_VARILL);
	    else {
		ungetD(c);
		name.len--;
	    }
	    goto end;
	}
	break;
    }
    if (c == '[') {
	Strbuf_append1(&name, c);
	do {
	    /*
	     * Michael Greim: Allow $ expansion to take place in selector
	     * expressions. (limits the number of characters returned)
	     */
	    c = getC(DOEXCL | DODOL);
	    if (c == '\n') {
		ungetD(c);
		name.len--;
		seterror(ERR_NLINDEX);
		goto end;
	    }
	    Strbuf_append1(&name, c);
	} while (c != ']');
	c = getC(DOEXCL);
    }
    if (c == ':') {
	/*
	 * if the :g modifier is followed by a newline, then error right away!
	 * -strike
	 */

	int     gmodflag = 0, amodflag = 0;

	do {
	    Strbuf_append1(&name, c), c = getC(DOEXCL);
	    if (c == 'g' || c == 'a') {
		if (c == 'g')
		    gmodflag++;
		else
		    amodflag++;
		Strbuf_append1(&name, c); c = getC(DOEXCL);
	    }
	    if ((c == 'g' && !gmodflag) || (c == 'a' && !amodflag)) {
		if (c == 'g')
		    gmodflag++;
		else
		    amodflag++;
		Strbuf_append1(&name, c); c = getC(DOEXCL);
	    }
	    Strbuf_append1(&name, c);
	    /* scan s// [eichin:19910926.0512EST] */
	    if (c == 's') {
		int delimcnt = 2;
		eChar delim = getC(0);

		Strbuf_append1(&name, delim);
		if (!delim || letter(delim)
		    || Isdigit(delim) || any(" \t\n", delim)) {
		    seterror(ERR_BADSUBST);
		    break;
		}
		while ((c = getC(0)) != CHAR_ERR) {
		    Strbuf_append1(&name, c);
		    if(c == delim) delimcnt--;
		    if(!delimcnt) break;
		}
		if(delimcnt) {
		    seterror(ERR_BADSUBST);
		    break;
		}
		c = 's';
	    }
	    if (!any("htrqxesul", c)) {
		if ((amodflag || gmodflag) && c == '\n')
		    stderror(ERR_VARSYN);	/* strike */
		seterror(ERR_BADMOD, c);
		goto end;
	    }
	}
	while ((c = getC(DOEXCL)) == ':');
	ungetD(c);
    }
    else
	ungetD(c);
    if (sc == '{') {
	c = getC(DOEXCL);
	if (c != '}') {
	    ungetD(c);
	    seterror(ERR_MISSING, '}');
	    goto end;
	}
	Strbuf_append1(&name, c);
    }
 end:
    cleanup_ignore(&name);
    cleanup_until(&name);
    addla(Strbuf_finish(&name));
}

/* xfree()'s its argument */
void
addla(Char *cp)
{
    static struct Strbuf buf; /* = Strbuf_INIT; */

    buf.len = 0;
    Strbuf_appendn(&buf, labuf.s + lap, labuf.len - lap);
    labuf.len = 0;
    Strbuf_append(&labuf, cp);
    Strbuf_terminate(&labuf);
    Strbuf_appendn(&labuf, buf.s, buf.len);
    xfree(cp);
    lap = 0;
}

/* left-hand side of last :s or search string of last ?event? */
static struct Strbuf lhsb; /* = Strbuf_INIT; */
static struct Strbuf slhs; /* = Strbuf_INIT; left-hand side of last :s */
static struct Strbuf rhsb; /* = Strbuf_INIT; right-hand side of last :s */
static int quesarg;

static void
getexcl(Char sc)
{
    struct wordent *hp, *ip;
    int     left, right, dol;
    eChar c;

    if (sc == 0) {
	c = getC(0);
	if (c == '{')
	    sc = (Char) c;
	else
	    ungetC(c);
    }
    quesarg = -1;

    lastev = eventno;
    hp = gethent(sc);
    if (hp == 0)
	return;
    hadhist = 1;
    dol = 0;
    if (hp == alhistp)
	for (ip = hp->next->next; ip != alhistt; ip = ip->next)
	    dol++;
    else
	for (ip = hp->next->next; ip != hp->prev; ip = ip->next)
	    dol++;
    left = 0, right = dol;
    if (sc == HISTSUB && HISTSUB != '\0') {
	ungetC('s'), unreadc(HISTSUB), c = ':';
	goto subst;
    }
    c = getC(0);
    if (!any(":^$*-%", c))
	goto subst;
    left = right = -1;
    if (c == ':') {
	c = getC(0);
	unreadc(c);
	if (letter(c) || c == '&') {
	    c = ':';
	    left = 0, right = dol;
	    goto subst;
	}
    }
    else
	ungetC(c);
    if (!getsel(&left, &right, dol))
	return;
    c = getC(0);
    if (c == '*')
	ungetC(c), c = '-';
    if (c == '-') {
	if (!getsel(&left, &right, dol))
	    return;
	c = getC(0);
    }
subst:
    exclc = right - left + 1;
    while (--left >= 0)
	hp = hp->next;
    if ((sc == HISTSUB && HISTSUB != '\0') || c == ':') {
	do {
	    hp = getsub(hp);
	    c = getC(0);
	} while (c == ':');
    }
    unreadc(c);
    if (sc == '{') {
	c = getC(0);
	if (c != '}')
	    seterror(ERR_BADBANG);
    }
    exclnxt = hp;
}

static struct wordent *
getsub(struct wordent *en)
{
    eChar   delim;
    eChar   c;
    eChar   sc;
    int global;

    do {
	exclnxt = 0;
	global = 0;
	sc = c = getC(0);
	while (c == 'g' || c == 'a') {
	    global |= (c == 'g') ? FLAG_G : FLAG_A;
	    sc = c = getC(0);
	}

	switch (c) {
	case 'p':
	    justpr++;
	    return (en);

	case 'x':
	case 'q':
	    global |= FLAG_G;
	    /*FALLTHROUGH*/

	case 'h':
	case 'r':
	case 't':
	case 'e':
	case 'u':
	case 'l':
	    break;

	case '&':
	    if (slhs.len == 0) {
		seterror(ERR_NOSUBST);
		return (en);
	    }
	    lhsb.len = 0;
	    Strbuf_append(&lhsb, slhs.s);
	    Strbuf_terminate(&lhsb);
	    break;

#ifdef notdef
	case '~':
	    if (lhsb.len == 0)
		goto badlhs;
	    break;
#endif

	case 's':
	    delim = getC(0);
	    if (letter(delim) || Isdigit(delim) || any(" \t\n", delim)) {
		unreadc(delim);
		lhsb.len = 0;
		seterror(ERR_BADSUBST);
		return (en);
	    }
	    Strbuf_terminate(&lhsb);
	    lhsb.len = 0;
	    for (;;) {
		c = getC(0);
		if (c == '\n') {
		    unreadc(c);
		    break;
		}
		if (c == delim)
		    break;
		if (c == '\\') {
		    c = getC(0);
		    if (c != delim && c != '\\')
			Strbuf_append1(&lhsb, '\\');
		}
		Strbuf_append1(&lhsb, c);
	    }
	    if (lhsb.len != 0)
		Strbuf_terminate(&lhsb);
	    else if (lhsb.s[0] == 0) {
		seterror(ERR_LHS);
		return (en);
	    } else
		lhsb.len = Strlen(lhsb.s); /* lhsb.s wasn't changed */
	    rhsb.len = 0;
	    for (;;) {
		c = getC(0);
		if (c == '\n') {
		    unreadc(c);
		    break;
		}
		if (c == delim)
		    break;
		if (c == '\\') {
		    c = getC(0);
		    if (c != delim /* && c != '~' */ )
			Strbuf_append1(&rhsb,  '\\');
		}
		Strbuf_append1(&rhsb, c);
	    }
	    Strbuf_terminate(&rhsb);
	    break;

	default:
	    if (c == '\n')
		unreadc(c);
	    seterror(ERR_BADBANGMOD, (int)c);
	    return (en);
	}
	slhs.len = 0;
	if (lhsb.s != NULL && lhsb.len != 0)
	    Strbuf_append(&slhs, lhsb.s);
	Strbuf_terminate(&slhs);
	if (exclc)
	    en = dosub(sc, en, global);
    }
    while ((c = getC(0)) == ':');
    unreadc(c);
    return (en);
}

/*
 * 
 * From Beto Appleton (beto@aixwiz.austin.ibm.com)
 *
 * when using history substitution, and the variable
 * 'history' is set to a value higher than 1000,
 * the shell might either freeze (hang) or core-dump.
 * We raise the limit to 50000000
 */

#define HIST_PURGE -50000000
static struct wordent *
dosub(Char sc, struct wordent *en, int global)
{
    struct wordent lexi;
    int    didsub = 0, didone = 0;
    struct wordent *hp = &lexi;
    struct wordent *wdp;
    int i = exclc;
    struct Hist *hst;

    wdp = hp;
    while (--i >= 0) {
	struct wordent *new = xcalloc(1, sizeof *wdp);

	new->word = 0;
	new->prev = wdp;
	new->next = hp;
	wdp->next = new;
	wdp = new;
	en = en->next;
	if (en->word) {
	    Char *tword, *otword;

	    if ((global & FLAG_G) || didsub == 0) {
		size_t pos;

		pos = 0;
		tword = subword(en->word, sc, &didone, &pos);
		if (didone)
		    didsub = 1;
		if (global & FLAG_A) {
		    while (didone && tword != STRNULL) {
			otword = tword;
			tword = subword(otword, sc, &didone, &pos);
			if (Strcmp(tword, otword) == 0) {
			    xfree(otword);
			    break;
			}
			else
			    xfree(otword);
		    }
		}
	    }
	    else
		tword = Strsave(en->word);
	    wdp->word = tword;
	}
    }
    if (didsub == 0)
	seterror(ERR_MODFAIL);
    hp->prev = wdp;
    /* 
     * ANSI mode HP/UX compiler chokes on
     * return &enthist(HIST_PURGE, &lexi, 0)->Hlex;
     */
    hst = enthist(HIST_PURGE, &lexi, 0, 0, -1);
    return &(hst->Hlex);
}

/* Return a newly allocated result of one modification of CP using the
   operation TYPE.  Set ADID to 1 if a modification was performed.
   If TYPE == 's', perform substitutions only from *START_POS on and set
   *START_POS to the position of next substitution attempt. */
static Char *
subword(Char *cp, Char type, int *adid, size_t *start_pos)
{
    Char *wp;
    const Char *mp, *np;

    switch (type) {

    case 'r':
    case 'e':
    case 'h':
    case 't':
    case 'q':
    case 'x':
    case 'u':
    case 'l':
	wp = domod(cp, type);
	if (wp == 0) {
	    *adid = 0;
	    return (Strsave(cp));
	}
	*adid = 1;
	return (wp);

    default:
	for (mp = cp + *start_pos; *mp; mp++) {
	    if (matchs(mp, lhsb.s)) {
		struct Strbuf wbuf = Strbuf_INIT;

		Strbuf_appendn(&wbuf, cp, mp - cp);
		for (np = rhsb.s; *np; np++)
		    switch (*np) {

		    case '\\':
			if (np[1] == '&')
			    np++;
			/* fall into ... */

		    default:
			Strbuf_append1(&wbuf, *np);
			continue;

		    case '&':
			Strbuf_append(&wbuf, lhsb.s);
			continue;
		    }
		*start_pos = wbuf.len;
		Strbuf_append(&wbuf, mp + lhsb.len);
		*adid = 1;
		return Strbuf_finish(&wbuf);
	    }
	}
	*adid = 0;
	return (Strsave(cp));
    }
}

Char   *
domod(Char *cp, Char type)
{
    Char *wp, *xp;
    int c;

    switch (type) {

    case 'x':
    case 'q':
	wp = Strsave(cp);
	for (xp = wp; (c = *xp) != 0; xp++)
	    if ((c != ' ' && c != '\t') || type == 'q')
		*xp |= QUOTE;
	return (wp);

    case 'l':
	wp = NLSChangeCase(cp, 1);
	return wp ? wp : Strsave(cp);

    case 'u':
	wp = NLSChangeCase(cp, 0);
	return wp ? wp : Strsave(cp);

    case 'h':
    case 't':
	if (!any(short2str(cp), '/'))
	    return (type == 't' ? Strsave(cp) : 0);
	wp = Strrchr(cp, '/');
	if (type == 'h')
	    xp = Strnsave(cp, wp - cp);
	else
	    xp = Strsave(wp + 1);
	return (xp);

    case 'e':
    case 'r':
	wp = Strend(cp);
	for (wp--; wp >= cp && *wp != '/'; wp--)
	    if (*wp == '.') {
		if (type == 'e')
		    xp = Strsave(wp + 1);
		else
		    xp = Strnsave(cp, wp - cp);
		return (xp);
	    }
	return (Strsave(type == 'e' ? STRNULL : cp));
    default:
	break;
    }
    return (0);
}

static int
matchs(const Char *str, const Char *pat)
{
    while (*str && *pat && *str == *pat)
	str++, pat++;
    return (*pat == 0);
}

static int
getsel(int *al, int *ar, int dol)
{
    eChar c = getC(0);
    int i;
    int    first = *al < 0;

    switch (c) {

    case '%':
	if (quesarg == -1) {
	    seterror(ERR_BADBANGARG);
	    return (0);
	}
	if (*al < 0)
	    *al = quesarg;
	*ar = quesarg;
	break;

    case '-':
	if (*al < 0) {
	    *al = 0;
	    *ar = dol - 1;
	    unreadc(c);
	}
	return (1);

    case '^':
	if (*al < 0)
	    *al = 1;
	*ar = 1;
	break;

    case '$':
	if (*al < 0)
	    *al = dol;
	*ar = dol;
	break;

    case '*':
	if (*al < 0)
	    *al = 1;
	*ar = dol;
	if (*ar < *al) {
	    *ar = 0;
	    *al = 1;
	    return (1);
	}
	break;

    default:
	if (Isdigit(c)) {
	    i = 0;
	    while (Isdigit(c)) {
		i = i * 10 + c - '0';
		c = getC(0);
	    }
	    if (i < 0)
		i = dol + 1;
	    if (*al < 0)
		*al = i;
	    *ar = i;
	}
	else if (*al < 0)
	    *al = 0, *ar = dol;
	else
	    *ar = dol - 1;
	unreadc(c);
	break;
    }
    if (first) {
	c = getC(0);
	unreadc(c);
	if (any("-$*", c))
	    return (1);
    }
    if (*al > *ar || *ar > dol) {
	seterror(ERR_BADBANGARG);
	return (0);
    }
    return (1);

}

static struct wordent *
gethent(Char sc)
{
    struct Hist *hp;
    Char *np;
    eChar c;
    int     event;
    int    back = 0;

    c = (sc == HISTSUB && HISTSUB != '\0') ? (eChar)HIST : getC(0);
    if (c == (eChar)HIST) {
	if (alhistp)
	    return (alhistp);
	event = eventno;
    }
    else
	switch (c) {

	case ':':
	case '^':
	case '$':
	case '*':
	case '%':
	    ungetC(c);
	    if (lastev == eventno && alhistp)
		return (alhistp);
	    event = lastev;
	    break;

	case '#':		/* !# is command being typed in (mrh) */
	    if (--hleft == 0) {
		seterror(ERR_HISTLOOP);
		return (0);
	    }
	    else
		return (&paraml);
	    /* NOTREACHED */

	case '-':
	    back = 1;
	    c = getC(0);
	    /* FALLSTHROUGH */

	default:
	    if (any("(=~", c)) {
		unreadc(c);
		ungetC(HIST);
		return (0);
	    }
	    Strbuf_terminate(&lhsb);
	    lhsb.len = 0;
	    event = 0;
	    while (!cmap(c, _ESC | _META | _QF | _QB) && !any("^*-%${}:#", c)) {
		if (event != -1 && Isdigit(c))
		    event = event * 10 + c - '0';
		else
		    event = -1;
		Strbuf_append1(&lhsb, c);
		c = getC(0);
	    }
	    unreadc(c);
	    if (lhsb.len == 0) {
		lhsb.len = Strlen(lhsb.s); /* lhsb.s wasn't changed */
		ungetC(HIST);
		return (0);
	    }
	    Strbuf_terminate(&lhsb);
	    if (event != -1) {
		/*
		 * History had only digits
		 */
		if (back)
		    event = eventno + (alhistp == 0) - event;
		break;
	    }
	    if (back) {
		Strbuf_append1(&lhsb, '\0'); /* Allocate space */
		Strbuf_terminate(&lhsb);
		memmove(lhsb.s + 1, lhsb.s, (lhsb.len - 1) * sizeof (*lhsb.s));
		lhsb.s[0] = '-';
	    }
	    hp = findev(lhsb.s, 0);
	    if (hp)
		lastev = hp->Hnum;
	    return (&hp->Hlex);

	case '?':
	    Strbuf_terminate(&lhsb);
	    lhsb.len = 0;
	    for (;;) {
		c = getC(0);
		if (c == '\n') {
		    unreadc(c);
		    break;
		}
		if (c == '?')
		    break;
		Strbuf_append1(&lhsb, c);
	    }
	    if (lhsb.len == 0) {
		lhsb.len = Strlen(lhsb.s); /* lhsb.s wasn't changed */
		if (lhsb.len == 0) {
		    seterror(ERR_NOSEARCH);
		    return (0);
		}
	    }
	    else
		Strbuf_terminate(&lhsb);
	    hp = findev(lhsb.s, 1);
	    if (hp)
		lastev = hp->Hnum;
	    return (&hp->Hlex);
	}

    for (hp = Histlist.Hnext; hp; hp = hp->Hnext)
	if (hp->Hnum == event) {
	    hp->Href = eventno;
	    lastev = hp->Hnum;
	    return (&hp->Hlex);
	}
    np = putn((tcsh_number_t)event);
    seterror(ERR_NOEVENT, short2str(np));
    xfree(np);
    return (0);
}

static struct Hist *
findev(Char *cp, int anyarg)
{
    struct Hist *hp;

    for (hp = Histlist.Hnext; hp; hp = hp->Hnext) {
	Char   *dp;
	Char *p, *q;
	struct wordent *lp = hp->Hlex.next;
	int     argno = 0;

	/*
	 * The entries added by alias substitution don't have a newline but do
	 * have a negative event number. Savehist() trims off these entries,
	 * but it happens before alias expansion, too early to delete those
	 * from the previous command.
	 */
	if (hp->Hnum < 0)
	    continue;
	if (lp->word[0] == '\n')
	    continue;
	if (!anyarg) {
	    p = cp;
	    q = lp->word;
	    do
		if (!*p)
		    return (hp);
	    while (*p++ == *q++);
	    continue;
	}
	do {
	    for (dp = lp->word; *dp; dp++) {
		p = cp;
		q = dp;
		do
		    if (!*p) {
			quesarg = argno;
			return (hp);
		    }
		while (*p++ == *q++);
	    }
	    lp = lp->next;
	    argno++;
	} while (lp->word[0] != '\n');
    }
    seterror(ERR_NOEVENT, short2str(cp));
    return (0);
}


static void
setexclp(Char *cp)
{
    if (cp && cp[0] == '\n')
	return;
    exclp = cp;
}

void
unreadc(Char c)
{
    peekread = (Char) c;
}

eChar
readc(int wanteof)
{
    eChar c;
    static  int sincereal;	/* Number of real EOFs we've seen */

#ifdef DEBUG_INP
    xprintf("readc\n");
#endif
    if ((c = peekread) != 0) {
	peekread = 0;
	return (c);
    }

top:
    aret = TCSH_F_SEEK;
    if (alvecp) {
	arun = 1;
#ifdef DEBUG_INP
	xprintf("alvecp %c\n", *alvecp & 0xff);
#endif
	aret = TCSH_A_SEEK;
	if ((c = *alvecp++) != 0)
	    return (c);
	if (alvec && *alvec) {
		alvecp = *alvec++;
		return (' ');
	}
	else {
	    alvecp = NULL;
	    aret = TCSH_F_SEEK;
	    return('\n');
	}
    }
    if (alvec) {
	arun = 1;
	if ((alvecp = *alvec) != 0) {
	    alvec++;
	    goto top;
	}
	/* Infinite source! */
	return ('\n');
    }
    arun = 0;
    if (evalp) {
	aret = TCSH_E_SEEK;
	if ((c = *evalp++) != 0)
	    return (c);
	if (evalvec && *evalvec) {
	    evalp = *evalvec++;
	    return (' ');
	}
	aret = TCSH_F_SEEK;
	evalp = 0;
    }
    if (evalvec) {
	if (evalvec == INVPPTR) {
	    doneinp = 1;
	    reset();
	}
	if ((evalp = *evalvec) != 0) {
	    evalvec++;
	    goto top;
	}
	evalvec = INVPPTR;
	return ('\n');
    }
    do {
	if (arginp == INVPTR || onelflg == 1) {
	    if (wanteof)
		return CHAR_ERR;
	    exitstat();
	}
	if (arginp) {
	    if ((c = *arginp++) == 0) {
		arginp = INVPTR;
		return ('\n');
	    }
	    return (c);
	}
#ifdef BSDJOBS
reread:
#endif /* BSDJOBS */
	c = bgetc();
	if (c == CHAR_ERR) {
#ifndef WINNT_NATIVE
# ifndef POSIX
#  ifdef TERMIO
	    struct termio tty;
#  else /* SGTTYB */
	    struct sgttyb tty;
#  endif /* TERMIO */
# else /* POSIX */
	    struct termios tty;
# endif /* POSIX */
#endif /* !WINNT_NATIVE */
	    if (wanteof)
		return CHAR_ERR;
	    /* was isatty but raw with ignoreeof yields problems */
#ifndef WINNT_NATIVE
# ifndef POSIX
#  ifdef TERMIO
	    if (ioctl(SHIN, TCGETA, (ioctl_t) & tty) == 0 &&
		(tty.c_lflag & ICANON))
#  else /* GSTTYB */
	    if (ioctl(SHIN, TIOCGETP, (ioctl_t) & tty) == 0 &&
		(tty.sg_flags & RAW) == 0)
#  endif /* TERMIO */
# else /* POSIX */
	    if (tcgetattr(SHIN, &tty) == 0 &&
		(tty.c_lflag & ICANON))
# endif /* POSIX */
#else /* WINNT_NATIVE */
	    if (isatty(SHIN))
#endif /* !WINNT_NATIVE */
	    {
#ifdef BSDJOBS
		pid_t ctpgrp;
#endif /* BSDJOBS */

		if (numeof != 0 && ++sincereal >= numeof)	/* Too many EOFs?  Bye! */
		    goto oops;
#ifdef BSDJOBS
		if (tpgrp != -1 &&
		    (ctpgrp = tcgetpgrp(FSHTTY)) != -1 &&
		    tpgrp != ctpgrp) {
		    (void) tcsetpgrp(FSHTTY, tpgrp);
# ifdef _SEQUENT_
		    if (ctpgrp)
# endif /* _SEQUENT */
		    (void) killpg(ctpgrp, SIGHUP);
# ifdef notdef
		    /*
		     * With the walking process group fix, this message
		     * is now obsolete. As the foreground process group
		     * changes, the shell needs to adjust. Well too bad.
		     */
		    xprintf(CGETS(16, 1, "Reset tty pgrp from %d to %d\n"),
			    (int)ctpgrp, (int)tpgrp);
# endif /* notdef */
		    goto reread;
		}
#endif /* BSDJOBS */
		/* What follows is complicated EOF handling -- sterling@netcom.com */
		/* First, we check to see if we have ignoreeof set */
		if (adrof(STRignoreeof)) {
			/* If so, we check for any stopped jobs only on the first EOF */
			if ((sincereal == 1) && (chkstop == 0)) {
				panystop(1);
			}
		} else {
			/* If we don't have ignoreeof set, always check for stopped jobs */
			if (chkstop == 0) {
				panystop(1);
			}
		}
		/* At this point, if there were stopped jobs, we would have already
		 * called reset().  If we got this far, assume we can print an
		 * exit/logout message if we ignoreeof, or just exit.
		 */
		if (adrof(STRignoreeof)) {
			/* If so, tell the user to use exit or logout */
		    if (loginsh) {
				xprintf("%s", CGETS(16, 2,
					"\nUse \"logout\" to logout.\n"));
		   	} else {
				xprintf(CGETS(16, 3,
					"\nUse \"exit\" to leave %s.\n"),
					progname);
			}
		    reset();
		} else {
			/* If we don't have ignoreeof set, just fall through */
			;	/* EMPTY */
		}
	    }
    oops:
	    doneinp = 1;
	    reset();
	}
	sincereal = 0;
	if (c == '\n' && onelflg)
	    onelflg--;
    } while (c == 0);
    Strbuf_append1(&histline, c);
    return (c);
}

static void
balloc(int buf)
{
    Char **nfbuf;

    while (buf >= fblocks) {
	nfbuf = xcalloc(fblocks + 2, sizeof(Char **));
	if (fbuf) {
	    (void) blkcpy(nfbuf, fbuf);
	    xfree(fbuf);
	}
	fbuf = nfbuf;
	fbuf[fblocks] = xcalloc(BUFSIZE, sizeof(Char));
	fblocks++;
    }
}

ssize_t
wide_read(int fildes, Char *buf, size_t nchars, int use_fclens)
{
    char cbuf[BUFSIZE + 1];
    ssize_t res, r = 0;
    size_t partial;
    int err;

    if (nchars == 0)
	return 0;
    assert (nchars <= sizeof(cbuf) / sizeof(*cbuf));
    USE(use_fclens);
    res = 0;
    partial = 0;
    do {
	size_t i;
	size_t len = nchars > partial ? nchars - partial : 1;

	if (partial + len >= sizeof(cbuf) / sizeof(*cbuf))
	    break;
	
	r = xread(fildes, cbuf + partial, len);
		  
	if (partial == 0 && r <= 0)
	    break;
	partial += r;
	i = 0;
	while (i < partial && nchars != 0) {
	    int tlen;

	    tlen = normal_mbtowc(buf + res, cbuf + i, partial - i);
	    if (tlen == -1) {
	        reset_mbtowc();
		if ((partial - i) < MB_LEN_MAX && r > 0)
		    /* Maybe a partial character and there is still a chance
		       to read more */
		    break;
		buf[res] = (unsigned char)cbuf[i] | INVALID_BYTE;
	    }
	    if (tlen <= 0)
		tlen = 1;
#ifdef WIDE_STRINGS
	    if (use_fclens)
		fclens[res] = tlen;
#endif
	    i += tlen;
	    res++;
	    nchars--;
	}
	if (i != partial)
	    memmove(cbuf, cbuf + i, partial - i);
	partial -= i;
    } while (partial != 0 && nchars > 0);
    /* Throwing away possible partial multibyte characters on error if the
       stream is not seekable */
    err = errno;
    lseek(fildes, -(off_t)partial, L_INCR);
    errno = err;
    return res != 0 ? res : r;
}

static eChar
bgetc(void)
{
    Char ch;
    int c, off, buf;
    int numleft = 0, roomleft;

    if (cantell) {
	if (fseekp < fbobp || fseekp > feobp) {
	    fbobp = feobp = fseekp;
	    (void) lseek(SHIN, fseekp, L_SET);
	}
	if (fseekp == feobp) {
#ifdef WIDE_STRINGS
	    off_t bytes;
	    size_t i;

	    bytes = fbobp;
	    for (i = 0; i < (size_t)(feobp - fbobp); i++)
		bytes += fclens[i];
	    fseekp = feobp = bytes;
#endif
	    fbobp = feobp;
	    c = wide_read(SHIN, fbuf[0], BUFSIZE, 1);
#ifdef convex
	    if (c < 0)
		stderror(ERR_SYSTEM, progname, strerror(errno));
#endif /* convex */
	    if (c <= 0)
		return CHAR_ERR;
	    feobp += c;
	}
#if !defined(WINNT_NATIVE) && !defined(__CYGWIN__)
	ch = fbuf[0][fseekp - fbobp];
	fseekp++;
#else
	do {
	    ch = fbuf[0][fseekp - fbobp];
	    fseekp++;
	} while(ch == '\r');
#endif /* !WINNT_NATIVE && !__CYGWIN__ */
	return (ch);
    }

    while (fseekp >= feobp) {
	if ((editing
#if defined(FILEC) && defined(TIOCSTI)
	    || filec
#endif /* FILEC && TIOCSTI */
	    ) && intty) {		/* then use twenex routine */
	    fseekp = feobp;		/* where else? */
#if defined(FILEC) && defined(TIOCSTI)
	    if (!editing)
		c = numleft = tenex(InputBuf, BUFSIZE);
	    else
#endif /* FILEC && TIOCSTI */
	    c = numleft = Inputl();	/* PWP: get a line */
	    while (numleft > 0) {
		off = (int) feobp % BUFSIZE;
		buf = (int) feobp / BUFSIZE;
		balloc(buf);
		roomleft = BUFSIZE - off;
		if (roomleft > numleft)
		    roomleft = numleft;
		(void) memcpy(fbuf[buf] + off, InputBuf + c - numleft,
			      roomleft * sizeof(Char));
		numleft -= roomleft;
		feobp += roomleft;
	    }
	} else {
	    off = (int) feobp % BUFSIZE;
	    buf = (int) feobp / BUFSIZE;
	    balloc(buf);
	    roomleft = BUFSIZE - off;
	    c = wide_read(SHIN, fbuf[buf] + off, roomleft, 0);
	    if (c > 0)
		feobp += c;
	}
	if (c == 0 || (c < 0 && fixio(SHIN, errno) == -1))
	    return CHAR_ERR;
    }
#ifdef SIG_WINDOW
    if (windowchg)
	(void) check_window_size(0);	/* for window systems */
#endif /* SIG_WINDOW */
#if !defined(WINNT_NATIVE) && !defined(__CYGWIN__)
    ch = fbuf[(int) fseekp / BUFSIZE][(int) fseekp % BUFSIZE];
    fseekp++;
#else
    do {
	ch = fbuf[(int) fseekp / BUFSIZE][(int) fseekp % BUFSIZE];
	fseekp++;
    } while(ch == '\r');
#endif /* !WINNT_NATIVE && !__CYGWIN__ */
    return (ch);
}

static void
bfree(void)
{
    int sb, i;

    if (cantell)
	return;
    if (whyles)
	return;
    sb = (int) (fseekp - 1) / BUFSIZE;
    if (sb > 0) {
	for (i = 0; i < sb; i++)
	    xfree(fbuf[i]);
	(void) blkcpy(fbuf, &fbuf[sb]);
	fseekp -= BUFSIZE * sb;
	feobp -= BUFSIZE * sb;
	fblocks -= sb;
    }
}

void
bseek(struct Ain *l)
{
    switch (aret = l->type) {
    case TCSH_E_SEEK:
	evalvec = l->a_seek;
	evalp = l->c_seek;
#ifdef DEBUG_SEEK
	xprintf(CGETS(16, 4, "seek to eval %x %x\n"), evalvec, evalp);
#endif
	return;
    case TCSH_A_SEEK:
	alvec = l->a_seek;
	alvecp = l->c_seek;
#ifdef DEBUG_SEEK
	xprintf(CGETS(16, 5, "seek to alias %x %x\n"), alvec, alvecp);
#endif
	return;
    case TCSH_F_SEEK:	
#ifdef DEBUG_SEEK
	xprintf(CGETS(16, 6, "seek to file %x\n"), fseekp);
#endif
	fseekp = l->f_seek;
#ifdef WIDE_STRINGS
	if (cantell) {
	    if (fseekp >= fbobp && feobp >= fbobp) {
		size_t i;
		off_t o;

		o = fbobp;
		for (i = 0; i < (size_t)(feobp - fbobp); i++) {
		    if (fseekp == o) {
			fseekp = fbobp + i;
			return;
		    }
		    o += fclens[i];
		}
		if (fseekp == o) {
		    fseekp = feobp;
		    return;
		}
	    }
	    fbobp = feobp = fseekp + 1; /* To force lseek() */
	}
#endif
	return;
    default:
	xprintf(CGETS(16, 7, "Bad seek type %d\n"), aret);
	abort();
    }
}

/* any similarity to bell telephone is purely accidental */
void
btell(struct Ain *l)
{
    switch (l->type = aret) {
    case TCSH_E_SEEK:
	l->a_seek = evalvec;
	l->c_seek = evalp;
#ifdef DEBUG_SEEK
	xprintf(CGETS(16, 8, "tell eval %x %x\n"), evalvec, evalp);
#endif
	return;
    case TCSH_A_SEEK:
	l->a_seek = alvec;
	l->c_seek = alvecp;
#ifdef DEBUG_SEEK
	xprintf(CGETS(16, 9, "tell alias %x %x\n"), alvec, alvecp);
#endif
	return;
    case TCSH_F_SEEK:
#ifdef WIDE_STRINGS
	if (cantell && fseekp >= fbobp && fseekp <= feobp) {
	    size_t i;

	    l->f_seek = fbobp;
	    for (i = 0; i < (size_t)(fseekp - fbobp); i++)
		l->f_seek += fclens[i];
	} else
#endif
	    /*SUPPRESS 112*/
	    l->f_seek = fseekp;
	l->a_seek = NULL;
#ifdef DEBUG_SEEK
	xprintf(CGETS(16, 10, "tell file %x\n"), fseekp);
#endif
	return;
    default:
	xprintf(CGETS(16, 7, "Bad seek type %d\n"), aret);
	abort();
    }
}

void
btoeof(void)
{
    (void) lseek(SHIN, (off_t) 0, L_XTND);
    aret = TCSH_F_SEEK;
    fseekp = feobp;
    alvec = NULL;
    alvecp = NULL;
    evalvec = NULL;
    evalp = NULL;
    wfree();
    bfree();
}

void
settell(void)
{
    off_t x;
    cantell = 0;
    if (arginp || onelflg || intty)
	return;
    if ((x = lseek(SHIN, (off_t) 0, L_INCR)) == -1)
	return;
    fbuf = xcalloc(2, sizeof(Char **));
    fblocks = 1;
    fbuf[0] = xcalloc(BUFSIZE, sizeof(Char));
    fseekp = fbobp = feobp = x;
    cantell = 1;
}
