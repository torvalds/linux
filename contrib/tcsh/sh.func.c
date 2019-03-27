/* $Header: /p/tcsh/cvsroot/tcsh/sh.func.c,v 3.176 2016/10/18 17:26:42 christos Exp $ */
/*
 * sh.func.c: csh builtin functions
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

RCSID("$tcsh: sh.func.c,v 3.176 2016/10/18 17:26:42 christos Exp $")

#include "ed.h"
#include "tw.h"
#include "tc.h"
#ifdef WINNT_NATIVE
#include "nt.const.h"
#endif /* WINNT_NATIVE */

#if defined (NLS_CATALOGS) && defined(HAVE_ICONV)
static iconv_t catgets_iconv; /* Or (iconv_t)-1 */
#endif

/*
 * C shell
 */

extern int MapsAreInited;
extern int NLSMapsAreInited;
extern int GotTermCaps;

static int zlast = -1;

static	void	islogin		(void);
static	void	preread		(void);
static	void	doagain		(void);
static  const char *isrchx	(int);
static	void	search		(int, int, Char *);
static	int	getword		(struct Strbuf *);
static	struct wordent	*histgetword	(struct wordent *);
static	void	toend		(void);
static	void	xecho		(int, Char **);
static	int	islocale_var	(Char *);
static	void	wpfree		(struct whyle *);

const struct biltins *
isbfunc(struct command *t)
{
    Char *cp = t->t_dcom[0];
    const struct biltins *bp, *bp1, *bp2;
    static struct biltins label = {"", dozip, 0, 0};
    static struct biltins foregnd = {"%job", dofg1, 0, 0};
    static struct biltins backgnd = {"%job &", dobg1, 0, 0};

    /*
     * We never match a builtin that has quoted the first
     * character; this has been the traditional way to escape 
     * builtin commands.
     */
    if (*cp & QUOTE)
	return NULL;

    if (*cp != ':' && lastchr(cp) == ':') {
	label.bname = short2str(cp);
	return (&label);
    }
    if (*cp == '%') {
	if (t->t_dflg & F_AMPERSAND) {
	    t->t_dflg &= ~F_AMPERSAND;
	    backgnd.bname = short2str(cp);
	    return (&backgnd);
	}
	foregnd.bname = short2str(cp);
	return (&foregnd);
    }
#ifdef WARP
    /*
     * This is a perhaps kludgy way to determine if the warp builtin is to be
     * acknowledged or not.  If checkwarp() fails, then we are to assume that
     * the warp command is invalid, and carry on as we would handle any other
     * non-builtin command.         -- JDK 2/4/88
     */
    if (eq(STRwarp, cp) && !checkwarp()) {
	return (0);		/* this builtin disabled */
    }
#endif /* WARP */
    /*
     * Binary search Bp1 is the beginning of the current search range. Bp2 is
     * one past the end.
     */
    for (bp1 = bfunc, bp2 = bfunc + nbfunc; bp1 < bp2;) {
	int i;

	bp = bp1 + ((bp2 - bp1) >> 1);
	if ((i = ((char) *cp) - *bp->bname) == 0 &&
	    (i = StrQcmp(cp, str2short(bp->bname))) == 0)
	    return bp;
	if (i < 0)
	    bp2 = bp;
	else
	    bp1 = bp + 1;
    }
#ifdef WINNT_NATIVE
    return nt_check_additional_builtins(cp);
#endif /*WINNT_NATIVE*/
    return (0);
}

void
func(struct command *t, const struct biltins *bp)
{
    int     i;

    xechoit(t->t_dcom);
    setname(bp->bname);
    i = blklen(t->t_dcom) - 1;
    if (i < bp->minargs)
	stderror(ERR_NAME | ERR_TOOFEW);
    if (i > bp->maxargs)
	stderror(ERR_NAME | ERR_TOOMANY);
    (*bp->bfunct) (t->t_dcom, t);
}

/*ARGSUSED*/
void
doonintr(Char **v, struct command *c)
{
    Char *cp;
    Char *vv = v[1];

    USE(c);
    if (parintr.sa_handler == SIG_IGN)
	return;
    if (setintr && intty)
	stderror(ERR_NAME | ERR_TERMINAL);
    cp = gointr;
    gointr = 0;
    xfree(cp);
    if (vv == 0) {
	if (setintr)
	    sigset_interrupting(SIGINT, queue_pintr);
	else
	    (void) signal(SIGINT, SIG_DFL);
	gointr = 0;
    }
    else if (eq((vv = strip(vv)), STRminus)) {
	(void) signal(SIGINT, SIG_IGN);
	gointr = Strsave(STRminus);
    }
    else {
	gointr = Strsave(vv);
	sigset_interrupting(SIGINT, queue_pintr);
    }
}

/*ARGSUSED*/
void
donohup(Char **v, struct command *c)
{
    USE(c);
    USE(v);
    if (intty)
	stderror(ERR_NAME | ERR_TERMINAL);
    if (setintr == 0) {
	(void) signal(SIGHUP, SIG_IGN);
	phup_disabled = 1;
#ifdef CC
	submit(getpid());
#endif /* CC */
    }
}

/*ARGSUSED*/
void
dohup(Char **v, struct command *c)
{
    USE(c);
    USE(v);
    if (intty)
	stderror(ERR_NAME | ERR_TERMINAL);
    if (setintr == 0)
	(void) signal(SIGHUP, SIG_DFL);
}


/*ARGSUSED*/
void
dozip(Char **v, struct command *c)
{
    USE(c);
    USE(v);
}

/*ARGSUSED*/
void
dofiletest(Char **v, struct command *c)
{
    Char **globbed, **fileptr, *ftest, *res;

    USE(c);
    if (*(ftest = *++v) != '-')
	stderror(ERR_NAME | ERR_FILEINQ);
    ++v;

    v = glob_all_or_error(v);
    globbed = v;
    cleanup_push(globbed, blk_cleanup);

    while (*(fileptr = v++) != '\0') {
	res = filetest(ftest, &fileptr, 0);
	cleanup_push(res, xfree);
	xprintf("%S", res);
	cleanup_until(res);
	if (*v)
	    xprintf(" ");
    }
    xprintf("\n");

    cleanup_until(globbed);
}

void
prvars(void)
{
    plist(&shvhed, VAR_ALL);
}

/*ARGSUSED*/
void
doalias(Char **v, struct command *c)
{
    struct varent *vp;
    Char *p;

    USE(c);
    v++;
    p = *v++;
    if (p == 0)
	plist(&aliases, VAR_ALL);
    else if (*v == 0) {
	vp = adrof1(strip(p), &aliases);
	if (vp && vp->vec)
	    blkpr(vp->vec), xputchar('\n');
    }
    else {
	if (eq(p, STRalias) || eq(p, STRunalias)) {
	    setname(short2str(p));
	    stderror(ERR_NAME | ERR_DANGER);
	}
	set1(strip(p), saveblk(v), &aliases, VAR_READWRITE);
	tw_cmd_free();
    }
}

/*ARGSUSED*/
void
unalias(Char **v, struct command *c)
{
    USE(c);
    unset1(v, &aliases);
    tw_cmd_free();
}

/*ARGSUSED*/
void
dologout(Char **v, struct command *c)
{
    USE(c);
    USE(v);
    islogin();
    goodbye(NULL, NULL);
}

/*ARGSUSED*/
void
dologin(Char **v, struct command *c)
{
#ifdef WINNT_NATIVE
    USE(c);
    USE(v);
#else /* !WINNT_NATIVE */
    char **p = short2blk(v);

    USE(c);
    cleanup_push((Char **)p, blk_cleanup);
    islogin();
    rechist(NULL, adrof(STRsavehist) != NULL);
    sigaction(SIGTERM, &parterm, NULL);
    (void) execv(_PATH_BIN_LOGIN, p);
    (void) execv(_PATH_USRBIN_LOGIN, p);
    cleanup_until((Char **)p);
    untty();
    xexit(1);
#endif /* !WINNT_NATIVE */
}


#ifdef NEWGRP
/*ARGSUSED*/
void
donewgrp(Char **v, struct command *c)
{
    char **p;
    if (chkstop == 0 && setintr)
	panystop(0);
    sigaction(SIGTERM, &parterm, NULL);
    p = short2blk(v);
    /*
     * From Beto Appleton (beto@aixwiz.austin.ibm.com)
     * Newgrp can take 2 arguments...
     */
    (void) execv(_PATH_BIN_NEWGRP, p);
    (void) execv(_PATH_USRBIN_NEWGRP, p);
    blkfree((Char **) p);
    untty();
    xexit(1);
}
#endif /* NEWGRP */

static void
islogin(void)
{
    if (chkstop == 0 && setintr)
	panystop(0);
    if (loginsh)
	return;
    stderror(ERR_NOTLOGIN);
}

void
doif(Char **v, struct command *kp)
{
    int i;
    Char **vv;

    v++;
    i = noexec ? 1 : expr(&v);
    vv = v;
    if (*vv == NULL)
	stderror(ERR_NAME | ERR_EMPTYIF);
    if (eq(*vv, STRthen)) {
	if (*++vv)
	    stderror(ERR_NAME | ERR_IMPRTHEN);
	setname(short2str(STRthen));
	/*
	 * If expression was zero, then scan to else , otherwise just fall into
	 * following code.
	 */
	if (!i)
	    search(TC_IF, 0, NULL);
	return;
    }
    /*
     * Simple command attached to this if. Left shift the node in this tree,
     * munging it so we can reexecute it.
     */
    if (i) {
	lshift(kp->t_dcom, vv - kp->t_dcom);
	reexecute(kp);
	donefds();
    }
}

/*
 * Reexecute a command, being careful not
 * to redo i/o redirection, which is already set up.
 */
void
reexecute(struct command *kp)
{
    kp->t_dflg &= F_SAVE;
    kp->t_dflg |= F_REPEAT;
    /*
     * If tty is still ours to arbitrate, arbitrate it; otherwise dont even set
     * pgrp's as the jobs would then have no way to get the tty (we can't give
     * it to them, and our parent wouldn't know their pgrp, etc.
     */
    execute(kp, (tpgrp > 0 ? tpgrp : -1), NULL, NULL, TRUE);
}

/*ARGSUSED*/
void
doelse (Char **v, struct command *c)
{
    USE(c);
    USE(v);
    if (!noexec)
	search(TC_ELSE, 0, NULL);
}

/*ARGSUSED*/
void
dogoto(Char **v, struct command *c)
{
    Char   *lp;

    USE(c);
    lp = globone(v[1], G_ERROR);
    cleanup_push(lp, xfree);
    if (!noexec)
	gotolab(lp);
    cleanup_until(lp);
}

void
gotolab(Char *lab)
{
    struct whyle *wp;
    /*
     * While we still can, locate any unknown ends of existing loops. This
     * obscure code is the WORST result of the fact that we don't really parse.
     */
    zlast = TC_GOTO;
    for (wp = whyles; wp; wp = wp->w_next)
	if (wp->w_end.type == TCSH_F_SEEK && wp->w_end.f_seek == 0) {
	    search(TC_BREAK, 0, NULL);
	    btell(&wp->w_end);
	}
	else {
	    bseek(&wp->w_end);
	}
    search(TC_GOTO, 0, lab);
    /*
     * Eliminate loops which were exited.
     */
    wfree();
}

/*ARGSUSED*/
void
doswitch(Char **v, struct command *c)
{
    Char *cp, *lp;

    USE(c);
    v++;
    if (!*v || *(*v++) != '(')
	stderror(ERR_SYNTAX);
    cp = **v == ')' ? STRNULL : *v++;
    if (*(*v++) != ')')
	v--;
    if (*v)
	stderror(ERR_SYNTAX);
    lp = globone(cp, G_ERROR);
    cleanup_push(lp, xfree);
    if (!noexec)
	search(TC_SWITCH, 0, lp);
    cleanup_until(lp);
}

/*ARGSUSED*/
void
dobreak(Char **v, struct command *c)
{
    USE(v);
    USE(c);
    if (whyles == NULL)
	stderror(ERR_NAME | ERR_NOTWHILE);
    if (!noexec)
	toend();
}

/*ARGSUSED*/
void
doexit(Char **v, struct command *c)
{
    USE(c);

    if (chkstop == 0 && (intty || intact) && evalvec == 0)
	panystop(0);
    /*
     * Don't DEMAND parentheses here either.
     */
    v++;
    if (*v) {
	setv(STRstatus, putn(expr(&v)), VAR_READWRITE);
	if (*v)
	    stderror(ERR_NAME | ERR_EXPRESSION);
    }
    btoeof();
#if 0
    if (intty)
#endif
    /* Always close, why only on ttys? */
	xclose(SHIN);
}

/*ARGSUSED*/
void
doforeach(Char **v, struct command *c)
{
    Char *cp, *sp;
    struct whyle *nwp;
    int gflag;

    USE(c);
    v++;
    cp = sp = strip(*v);
    if (!letter(*cp))
	stderror(ERR_NAME | ERR_VARBEGIN);
    do {
	cp++;
    } while (alnum(*cp));
    if (*cp != '\0')
	stderror(ERR_NAME | ERR_VARALNUM);
    cp = *v++;
    if (v[0][0] != '(' || v[blklen(v) - 1][0] != ')')
	stderror(ERR_NAME | ERR_NOPAREN);
    v++;
    gflag = tglob(v);
    if (gflag) {
	v = globall(v, gflag);
	if (v == 0 && !noexec)
	    stderror(ERR_NAME | ERR_NOMATCH);
    }
    else {
	v = saveblk(v);
	trim(v);
    }
    nwp = xcalloc(1, sizeof *nwp);
    nwp->w_fe = nwp->w_fe0 = v;
    btell(&nwp->w_start);
    nwp->w_fename = Strsave(cp);
    nwp->w_next = whyles;
    nwp->w_end.type = TCSH_F_SEEK;
    whyles = nwp;
    /*
     * Pre-read the loop so as to be more comprehensible to a terminal user.
     */
    zlast = TC_FOREACH;
    if (intty)
	preread();
    if (!noexec)
	doagain();
}

/*ARGSUSED*/
void
dowhile(Char **v, struct command *c)
{
    int status;
    int again = whyles != 0 && 
			  SEEKEQ(&whyles->w_start, &lineloc) &&
			  whyles->w_fename == 0;

    USE(c);
    v++;
    /*
     * Implement prereading here also, taking care not to evaluate the
     * expression before the loop has been read up from a terminal.
     */
    if (noexec)
	status = 0;
    else if (intty && !again)
	status = !exp0(&v, 1);
    else
	status = !expr(&v);
    if (*v && !noexec)
	stderror(ERR_NAME | ERR_EXPRESSION);
    if (!again) {
	struct whyle *nwp = xcalloc(1, sizeof(*nwp));

	nwp->w_start = lineloc;
	nwp->w_end.type = TCSH_F_SEEK;
	nwp->w_end.f_seek = 0;
	nwp->w_end.a_seek = 0;
	nwp->w_next = whyles;
	whyles = nwp;
	zlast = TC_WHILE;
	if (intty) {
	    /*
	     * The tty preread
	     */
	    preread();
	    doagain();
	    return;
	}
    }
    if (status)
	/* We ain't gonna loop no more, no more! */
	toend();
}

static void
preread(void)
{
    int old_pintr_disabled;

    whyles->w_end.type = TCSH_I_SEEK;
    if (setintr)
	pintr_push_enable(&old_pintr_disabled);
    search(TC_BREAK, 0, NULL);		/* read the expression in */
    if (setintr)
	cleanup_until(&old_pintr_disabled);
    btell(&whyles->w_end);
}

/*ARGSUSED*/
void
doend(Char **v, struct command *c)
{
    USE(v);
    USE(c);
    if (!whyles)
	stderror(ERR_NAME | ERR_NOTWHILE);
    btell(&whyles->w_end);
    if (!noexec)
	doagain();
}

/*ARGSUSED*/
void
docontin(Char **v, struct command *c)
{
    USE(v);
    USE(c);
    if (!whyles)
	stderror(ERR_NAME | ERR_NOTWHILE);
    if (!noexec)
	doagain();
}

static void
doagain(void)
{
    /* Repeating a while is simple */
    if (whyles->w_fename == 0) {
	bseek(&whyles->w_start);
	return;
    }
    /*
     * The foreach variable list actually has a spurious word ")" at the end of
     * the w_fe list.  Thus we are at the of the list if one word beyond this
     * is 0.
     */
    if (!whyles->w_fe[1]) {
	dobreak(NULL, NULL);
	return;
    }
    setv(whyles->w_fename, quote(Strsave(*whyles->w_fe++)), VAR_READWRITE);
    bseek(&whyles->w_start);
}

void
dorepeat(Char **v, struct command *kp)
{
    int i = 1;

    do {
	i *= getn(v[1]);
	lshift(v, 2);
    } while (v[0] != NULL && Strcmp(v[0], STRrepeat) == 0);
    if (noexec)
	i = 1;

    if (setintr) {
	pintr_disabled++;
	cleanup_push(&pintr_disabled, disabled_cleanup);
    }
    while (i > 0) {
	if (setintr && pintr_disabled == 1) {
	    cleanup_until(&pintr_disabled);
	    pintr_disabled++;
	    cleanup_push(&pintr_disabled, disabled_cleanup);
	}
	reexecute(kp);
	--i;
    }
    if (setintr && pintr_disabled == 1)
        cleanup_until(&pintr_disabled);
    donefds();
}

/*ARGSUSED*/
void
doswbrk(Char **v, struct command *c)
{
    USE(v);
    USE(c);
    if (!noexec)
	search(TC_BRKSW, 0, NULL);
}

int
srchx(Char *cp)
{
    struct srch *sp, *sp1, *sp2;
    int i;

    /*
     * Ignore keywords inside heredocs
     */
    if (inheredoc)
	return -1;

    /*
     * Binary search Sp1 is the beginning of the current search range. Sp2 is
     * one past the end.
     */
    for (sp1 = srchn, sp2 = srchn + nsrchn; sp1 < sp2;) {
	sp = sp1 + ((sp2 - sp1) >> 1);
	if ((i = *cp - *sp->s_name) == 0 &&
	    (i = Strcmp(cp, str2short(sp->s_name))) == 0)
	    return sp->s_value;
	if (i < 0)
	    sp2 = sp;
	else
	    sp1 = sp + 1;
    }
    return (-1);
}

static const char *
isrchx(int n)
{
    struct srch *sp, *sp2;

    for (sp = srchn, sp2 = srchn + nsrchn; sp < sp2; sp++)
	if (sp->s_value == n)
	    return (sp->s_name);
    return ("");
}


static int Stype;
static Char *Sgoal;

static void
search(int type, int level, Char *goal)
{
    struct Strbuf word = Strbuf_INIT;
    Char *cp;
    struct whyle *wp;
    int wlevel = 0;
    struct wordent *histent = NULL, *ohistent = NULL;

    Stype = type;
    Sgoal = goal;
    if (type == TC_GOTO) {
	struct Ain a;
	a.type = TCSH_F_SEEK;
	a.f_seek = 0;
	a.a_seek = 0;
	bseek(&a);
    }
    cleanup_push(&word, Strbuf_cleanup);
    do {
	    
	if (intty) {
	    histent = xmalloc(sizeof(*histent));
	    ohistent = xmalloc(sizeof(*histent));
	    ohistent->word = STRNULL;
	    ohistent->next = histent;
	    histent->prev = ohistent;
	}

	if (intty && fseekp == feobp && aret == TCSH_F_SEEK)
	    printprompt(1, isrchx(type == TC_BREAK ? zlast : type));
	/* xprintf("? "), flush(); */
	(void) getword(&word);
	Strbuf_terminate(&word);

	if (intty && Strlen(word.s) > 0) {
	    histent->word = Strsave(word.s);
	    histent->next = xmalloc(sizeof(*histent));
	    histent->next->prev = histent;
	    histent = histent->next;
	}

	switch (srchx(word.s)) {

	case TC_ELSE:
	    if (level == 0 && type == TC_IF)
		goto end;
	    break;

	case TC_IF:
	    while (getword(&word)) {
		if (intty) {
		    histent->word = Strsave(word.s);
		    histent->next = xmalloc(sizeof(*histent));
		    histent->next->prev = histent;
		    histent = histent->next;
		}
		continue;
	    }
	    
	    if ((type == TC_IF || type == TC_ELSE) &&
		eq(word.s, STRthen))
		level++;
	    break;

	case TC_ENDIF:
	    if (type == TC_IF || type == TC_ELSE)
		level--;
	    break;

	case TC_FOREACH:
	case TC_WHILE:
	    wlevel++;
	    if (type == TC_BREAK)
		level++;
	    break;

	case TC_END:
	    if (type == TC_BRKSW) {
		if (wlevel == 0) {
		    wp = whyles;
		    if (wp) {
			    whyles = wp->w_next;
			    wpfree(wp);
		    }
		}
	    }
	    if (type == TC_BREAK)
		level--;
	    wlevel--;
	    break;

	case TC_SWITCH:
	    if (type == TC_SWITCH || type == TC_BRKSW)
		level++;
	    break;

	case TC_ENDSW:
	    if (type == TC_SWITCH || type == TC_BRKSW)
		level--;
	    break;

	case TC_LABEL:
	    if (type == TC_GOTO && getword(&word) && eq(word.s, goal))
		level = -1;
	    break;

	default:
	    if (type != TC_GOTO && (type != TC_SWITCH || level != 0))
		break;
	    if (word.len == 0 || word.s[word.len - 1] != ':')
		break;
	    word.s[--word.len] = 0;
	    if ((type == TC_GOTO && eq(word.s, goal)) ||
		(type == TC_SWITCH && eq(word.s, STRdefault)))
		level = -1;
	    break;

	case TC_CASE:
	    if (type != TC_SWITCH || level != 0)
		break;
	    (void) getword(&word);
	    if (word.len != 0 && word.s[word.len - 1] == ':')
		word.s[--word.len] = 0;
	    cp = strip(Dfix1(word.s));
	    cleanup_push(cp, xfree);
	    if (Gmatch(goal, cp))
		level = -1;
	    cleanup_until(cp);
	    break;

	case TC_DEFAULT:
	    if (type == TC_SWITCH && level == 0)
		level = -1;
	    break;
	}
	if (intty) {
	    ohistent->prev = histgetword(histent);
	    ohistent->prev->next = ohistent;
	    savehist(ohistent, 0);
	    freelex(ohistent);
	    xfree(ohistent);
	} else 
	    (void) getword(NULL);
    } while (level >= 0);
 end:
    cleanup_until(&word);
}

static struct wordent *
histgetword(struct wordent *histent) 
{
    int first;
    eChar c, d;
    int e;
    struct Strbuf *tmp;
    tmp = xmalloc(sizeof(*tmp));
    tmp->size = 0;
    tmp->s = NULL;
    c = readc(1);
    d = 0;
    e = 0;
    for (;;) {
	tmp->len = 0;
	Strbuf_terminate (tmp);
	while (c == ' ' || c == '\t')
	    c = readc(1);
	if (c == '#')
	    do
		c = readc(1);
	    while (c != CHAR_ERR && c != '\n');
	if (c == CHAR_ERR)
	    goto past;
	if (c == '\n') 
	    goto nl;
	unreadc(c);
	first = 1;
	do {
	    e = (c == '\\');
	    c = readc(1);
	    if (c == '\\' && !e) {
		if ((c = readc(1)) == '\n') {
		    e = 1;
		    c = ' ';
		} else {
		    unreadc(c);
		    c = '\\';
		}
	    }
	    if ((c == '\'' || c == '"') && !e) {
		if (d == 0)
		    d = c;
		else if (d == c)
		    d = 0;
	    }
	    if (c == CHAR_ERR)
		goto past;
	    
	    Strbuf_append1(tmp, (Char) c);
	    
	    if (!first && !d && c == '(' && !e) {
		break;
	    }
	    first = 0;
	} while (d || e || (c != ' ' && c != '\t' && c != '\n'));
	tmp->len--;
	if (tmp->len) {
	    Strbuf_terminate(tmp);
	    histent->word = Strsave(tmp->s);
	    histent->next = xmalloc(sizeof (*histent));
	    histent->next->prev = histent;
	    histent = histent->next;
	}
	if (c == '\n') {
	nl:
	    tmp->len = 0;
	    Strbuf_append1(tmp, (Char) c);
	    Strbuf_terminate(tmp);
	    histent->word = Strsave(tmp->s);
	    return histent;
	}
    }
    
past:
    switch (Stype) {

    case TC_IF:
	stderror(ERR_NAME | ERR_NOTFOUND, "then/endif");
	break;

    case TC_ELSE:
	stderror(ERR_NAME | ERR_NOTFOUND, "endif");
	break;

    case TC_BRKSW:
    case TC_SWITCH:
	stderror(ERR_NAME | ERR_NOTFOUND, "endsw");
	break;

    case TC_BREAK:
	stderror(ERR_NAME | ERR_NOTFOUND, "end");
	break;

    case TC_GOTO:
	setname(short2str(Sgoal));
	stderror(ERR_NAME | ERR_NOTFOUND, "label");
	break;

    default:
	break;
    }
    /* NOTREACHED */
    return NULL;
}

static int
getword(struct Strbuf *wp)
{
    int found = 0, first;
    eChar c, d;

    if (wp)
	wp->len = 0;
    c = readc(1);
    d = 0;
    do {
	while (c == ' ' || c == '\t')
	    c = readc(1);
	if (c == '#')
	    do
		c = readc(1);
	    while (c != CHAR_ERR && c != '\n');
	if (c == CHAR_ERR)
	    goto past;
	if (c == '\n') {
	    if (wp)
		break;
	    return (0);
	}
	unreadc(c);
	found = 1;
	first = 1;
	do {
	    c = readc(1);
	    if (c == '\\' && (c = readc(1)) == '\n')
		c = ' ';
	    if (c == '\'' || c == '"') {
		if (d == 0)
		    d = c;
		else if (d == c)
		    d = 0;
	    }
	    if (c == CHAR_ERR)
		goto past;
	    if (wp)
		Strbuf_append1(wp, (Char) c);
	    if (!d && c == ')') {
		if (!first && wp) {
		    goto past_word_end;
		} else {
		    if (wp) {
			wp->len = 1;
			Strbuf_terminate(wp);
		    }
		    return found;
		}
	    }
	    if (!first && !d && c == '(') {
		if (wp)
		    goto past_word_end;
		else 
		    break;
	    }
	    first = 0;
	} while ((d || (c != ' ' && c != '\t')) && c != '\n');
    } while (wp == 0);

 past_word_end:
    unreadc(c);
    if (found) {
	wp->len--;
	Strbuf_terminate(wp);
    }

    return (found);

past:
    switch (Stype) {

    case TC_IF:
	stderror(ERR_NAME | ERR_NOTFOUND, "then/endif");
	break;

    case TC_ELSE:
	stderror(ERR_NAME | ERR_NOTFOUND, "endif");
	break;

    case TC_BRKSW:
    case TC_SWITCH:
	stderror(ERR_NAME | ERR_NOTFOUND, "endsw");
	break;

    case TC_BREAK:
	stderror(ERR_NAME | ERR_NOTFOUND, "end");
	break;

    case TC_GOTO:
	setname(short2str(Sgoal));
	stderror(ERR_NAME | ERR_NOTFOUND, "label");
	break;

    default:
	break;
    }
    /* NOTREACHED */
    return (0);
}

static void
toend(void)
{
    if (whyles->w_end.type == TCSH_F_SEEK && whyles->w_end.f_seek == 0) {
	search(TC_BREAK, 0, NULL);
	btell(&whyles->w_end);
	whyles->w_end.f_seek--;
    }
    else {
	bseek(&whyles->w_end);
    }
    wfree();
}

static void
wpfree(struct whyle *wp)
{
	if (wp->w_fe0)
	    blkfree(wp->w_fe0);
	xfree(wp->w_fename);
	xfree(wp);
}

void
wfree(void)
{
    struct Ain    o;
    struct whyle *nwp;
#ifdef lint
    nwp = NULL;	/* sun lint is dumb! */
#endif

#ifdef FDEBUG
    static const char foo[] = "IAFE";
#endif /* FDEBUG */

    btell(&o);

#ifdef FDEBUG
    xprintf("o->type %c o->a_seek %d o->f_seek %d\n",
	    foo[o.type + 1], o.a_seek, o.f_seek);
#endif /* FDEBUG */

    for (; whyles; whyles = nwp) {
	struct whyle *wp = whyles;
	nwp = wp->w_next;

#ifdef FDEBUG
	xprintf("start->type %c start->a_seek %d start->f_seek %d\n",
		foo[wp->w_start.type+1], 
		wp->w_start.a_seek, wp->w_start.f_seek);
	xprintf("end->type %c end->a_seek %d end->f_seek %d\n",
		foo[wp->w_end.type + 1], wp->w_end.a_seek, wp->w_end.f_seek);
#endif /* FDEBUG */

	/*
	 * XXX: We free loops that have different seek types.
	 */
	if (wp->w_end.type != TCSH_I_SEEK && wp->w_start.type == wp->w_end.type &&
	    wp->w_start.type == o.type) {
	    if (wp->w_end.type == TCSH_F_SEEK) {
		if (o.f_seek >= wp->w_start.f_seek && 
		    (wp->w_end.f_seek == 0 || o.f_seek < wp->w_end.f_seek))
		    break;
	    }
	    else {
		if (o.a_seek >= wp->w_start.a_seek && 
		    (wp->w_end.a_seek == 0 || o.a_seek < wp->w_end.a_seek))
		    break;
	    }
	}

	wpfree(wp);
    }
}

/*ARGSUSED*/
void
doecho(Char **v, struct command *c)
{
    USE(c);
    xecho(' ', v);
}

/*ARGSUSED*/
void
doglob(Char **v, struct command *c)
{
    USE(c);
    xecho(0, v);
    flush();
}

static void
xecho(int sep, Char **v)
{
    Char *cp, **globbed = NULL;
    int     nonl = 0;
    int	    echo_style = ECHO_STYLE;
    struct varent *vp;

    if ((vp = adrof(STRecho_style)) != NULL && vp->vec != NULL &&
	vp->vec[0] != NULL) {
	if (Strcmp(vp->vec[0], STRbsd) == 0)
	    echo_style = BSD_ECHO;
	else if (Strcmp(vp->vec[0], STRsysv) == 0)
	    echo_style = SYSV_ECHO;
	else if (Strcmp(vp->vec[0], STRboth) == 0)
	    echo_style = BOTH_ECHO;
	else if (Strcmp(vp->vec[0], STRnone) == 0)
	    echo_style = NONE_ECHO;
    }

    v++;
    if (*v == 0)
	goto done;
    if (setintr) {
	int old_pintr_disabled;
	pintr_push_enable(&old_pintr_disabled);
	v = glob_all_or_error(v);
	cleanup_until(&old_pintr_disabled);
    } else {
	v = glob_all_or_error(v);
    }
    globbed = v;
    if (globbed != NULL)
	cleanup_push(globbed, blk_cleanup);

    if ((echo_style & BSD_ECHO) != 0 && sep == ' ' && *v && eq(*v, STRmn))
	nonl++, v++;

    while ((cp = *v++) != 0) {
	Char c;

	if (setintr) {
	    int old_pintr_disabled;

	    pintr_push_enable(&old_pintr_disabled);
	    cleanup_until(&old_pintr_disabled);
	}
	while ((c = *cp++) != 0) {
	    if ((echo_style & SYSV_ECHO) != 0 && c == '\\') {
		switch (c = *cp++) {
		case 'a':
		    c = '\a';
		    break;
		case 'b':
		    c = '\b';
		    break;
		case 'c':
		    nonl = 1;
		    goto done;
		case 'e':
#if 0			/* Windows does not understand \e */
		    c = '\e';
#else
		    c = CTL_ESC('\033');
#endif
		    break;
		case 'f':
		    c = '\f';
		    break;
		case 'n':
		    c = '\n';
		    break;
		case 'r':
		    c = '\r';
		    break;
		case 't':
		    c = '\t';
		    break;
		case 'v':
		    c = '\v';
		    break;
		case '\\':
		    c = '\\';
		    break;
		case '0':
		    c = 0;
		    if (*cp >= '0' && *cp < '8')
			c = c * 8 + *cp++ - '0';
		    if (*cp >= '0' && *cp < '8')
			c = c * 8 + *cp++ - '0';
		    if (*cp >= '0' && *cp < '8')
			c = c * 8 + *cp++ - '0';
		    break;
		case '\0':
		    c = '\\';
		    cp--;
		    break;
		default:
		    xputchar('\\' | QUOTE);
		    break;
		}
	    }
	    xputwchar(c | QUOTE);

	}
	if (*v)
	    xputchar(sep | QUOTE);
    }
done:
    if (sep && nonl == 0)
	xputchar('\n');
    else
	flush();
    if (globbed != NULL)
	cleanup_until(globbed);
}

/* check whether an environment variable should invoke 'set_locale()' */
static int
islocale_var(Char *var)
{
    static Char *locale_vars[] = {
	STRLANG,	STRLC_ALL, 	STRLC_CTYPE,	STRLC_NUMERIC,
	STRLC_TIME,	STRLC_COLLATE,	STRLC_MESSAGES,	STRLC_MONETARY, 0
    };
    Char **v;

    for (v = locale_vars; *v; ++v)
	if (eq(var, *v))
	    return 1;
    return 0;
}

static void
xlate_cr_cleanup(void *dummy)
{
    USE(dummy);
    xlate_cr = 0;
}

/*ARGSUSED*/
void
doprintenv(Char **v, struct command *c) 
{
    Char   *e;

    USE(c);
    v++;
    if (*v == 0) {
	Char **ep;

	xlate_cr = 1;
	cleanup_push(&xlate_cr, xlate_cr_cleanup);
	for (ep = STR_environ; *ep; ep++) {
	    if (setintr) {
		int old_pintr_disabled;

		pintr_push_enable(&old_pintr_disabled);
		cleanup_until(&old_pintr_disabled);
	    }
	    xprintf("%S\n", *ep);
	}
	cleanup_until(&xlate_cr);
    }
    else if ((e = tgetenv(*v)) != NULL) {
	int old_output_raw;

	old_output_raw = output_raw;
	output_raw = 1;
	cleanup_push(&old_output_raw, output_raw_restore);
	xprintf("%S\n", e);
	cleanup_until(&old_output_raw);
    }
    else
	setcopy(STRstatus, STR1, VAR_READWRITE);
}

/* from "Karl Berry." <karl%mote.umb.edu@relay.cs.net> -- for NeXT things
   (and anything else with a modern compiler) */

/*ARGSUSED*/
void
dosetenv(Char **v, struct command *c)
{
    Char   *vp, *lp;

    USE(c);
    if (*++v == 0) {
	doprintenv(--v, 0);
	return;
    }

    vp = *v++;
    lp = vp;

    if (!letter(*lp))
	stderror(ERR_NAME | ERR_VARBEGIN);
    do {
	lp++;
    } while (alnum(*lp) || *lp == '.');
    if (*lp != '\0')
	stderror(ERR_NAME | ERR_VARALNUM);

    if ((lp = *v++) == 0)
	lp = STRNULL;

    lp = globone(lp, G_APPEND);
    cleanup_push(lp, xfree);
    tsetenv(vp, lp);
    if (eq(vp, STRKPATH)) {
        importpath(lp);
	dohash(NULL, NULL);
	cleanup_until(lp);
	return;
    }

#ifdef apollo
    if (eq(vp, STRSYSTYPE)) {
	dohash(NULL, NULL);
	cleanup_until(lp);
	return;
    }
#endif /* apollo */

    /* dspkanji/dspmbyte autosetting */
    /* PATCH IDEA FROM Issei.Suzuki VERY THANKS */
#if defined(DSPMBYTE)
    if(eq(vp, STRLANG) && !adrof(CHECK_MBYTEVAR)) {
	autoset_dspmbyte(lp);
    }
#endif

    if (islocale_var(vp)) {
#ifdef NLS
	int     k;

# ifdef SETLOCALEBUG
	dont_free = 1;
# endif /* SETLOCALEBUG */
	(void) setlocale(LC_ALL, "");
# ifdef LC_COLLATE
	(void) setlocale(LC_COLLATE, "");
# endif
# ifdef LC_CTYPE
	(void) setlocale(LC_CTYPE, ""); /* for iscntrl */
# endif /* LC_CTYPE */
# if defined(AUTOSET_KANJI)
        autoset_kanji();
# endif /* AUTOSET_KANJI */
# ifdef NLS_CATALOGS
#  ifdef LC_MESSAGES
	(void) setlocale(LC_MESSAGES, "");
#  endif /* LC_MESSAGES */
	nlsclose();
	nlsinit();
# endif /* NLS_CATALOGS */
# ifdef SETLOCALEBUG
	dont_free = 0;
# endif /* SETLOCALEBUG */
# ifdef STRCOLLBUG
	fix_strcoll_bug();
# endif /* STRCOLLBUG */
	tw_cmd_free();	/* since the collation sequence has changed */
	for (k = 0200; k <= 0377 && !Isprint(CTL_ESC(k)); k++)
	    continue;
	AsciiOnly = MB_CUR_MAX == 1 && k > 0377;
#else /* !NLS */
	AsciiOnly = 0;
#endif /* NLS */
	NLSMapsAreInited = 0;
	ed_Init();
	if (MapsAreInited && !NLSMapsAreInited)
	    ed_InitNLSMaps();
	cleanup_until(lp);
	return;
    }

#ifdef NLS_CATALOGS
    if (eq(vp, STRNLSPATH)) {
	nlsclose();
	nlsinit();
    }
#endif

    if (eq(vp, STRNOREBIND)) {
	NoNLSRebind = 1;
	MapsAreInited = 0;
	NLSMapsAreInited = 0;
	ed_InitMaps();
	cleanup_until(lp);
	return;
    }
#ifdef WINNT_NATIVE
    if (eq(vp, STRtcshlang)) {
	nlsinit();
	cleanup_until(lp);
	return;
    }
#endif /* WINNT_NATIVE */
    if (eq(vp, STRKTERM)) {
	char *t;

	setv(STRterm, quote(lp), VAR_READWRITE);	/* lp memory used here */
	cleanup_ignore(lp);
	cleanup_until(lp);
	t = short2str(lp);
	if (noediting && strcmp(t, "unknown") != 0 && strcmp(t,"dumb") != 0) {
	    editing = 1;
	    noediting = 0;
	    setNS(STRedit);
	}
	GotTermCaps = 0;
	ed_Init();
	return;
    }

    if (eq(vp, STRKHOME)) {
	Char *canon;
	/*
	 * convert to canonical pathname (possibly resolving symlinks)
	 */
	canon = dcanon(lp, lp);
	cleanup_ignore(lp);
	cleanup_until(lp);
	cleanup_push(canon, xfree);
	setv(STRhome, quote(canon), VAR_READWRITE); /* lp memory used here */
	cleanup_ignore(canon);
	cleanup_until(canon);

	/* fix directory stack for new tilde home */
	dtilde();
	return;
    }

    if (eq(vp, STRKSHLVL)) {
	setv(STRshlvl, quote(lp), VAR_READWRITE); /* lp memory used here */
	cleanup_ignore(lp);
	cleanup_until(lp);
	return;
    }

    if (eq(vp, STRKUSER)) {
	setv(STRuser, quote(lp), VAR_READWRITE);	/* lp memory used here */
	cleanup_ignore(lp);
	cleanup_until(lp);
	return;
    }

    if (eq(vp, STRKGROUP)) {
	setv(STRgroup, quote(lp), VAR_READWRITE); /* lp memory used here */
	cleanup_ignore(lp);
	cleanup_until(lp);
	return;
    }

#ifdef COLOR_LS_F
    if (eq(vp, STRLS_COLORS)) {
        parseLS_COLORS(lp);
	cleanup_until(lp);
	return;
    }
    if (eq(vp, STRLSCOLORS)) {
        parseLSCOLORS(lp);
	cleanup_until(lp);
	return;
    }
#endif /* COLOR_LS_F */

#ifdef SIG_WINDOW
    /*
     * Load/Update $LINES $COLUMNS
     */
    if ((eq(lp, STRNULL) && (eq(vp, STRLINES) || eq(vp, STRCOLUMNS))) ||
	eq(vp, STRTERMCAP)) {
	cleanup_until(lp);
	check_window_size(1);
	return;
    }

    /*
     * Change the size to the one directed by $LINES and $COLUMNS
     */
    if (eq(vp, STRLINES) || eq(vp, STRCOLUMNS)) {
#if 0
	GotTermCaps = 0;
#endif
	cleanup_until(lp);
	ed_Init();
	return;
    }
#endif /* SIG_WINDOW */
    cleanup_until(lp);
}

/*ARGSUSED*/
void
dounsetenv(Char **v, struct command *c)
{
    Char  **ep, *p, *n, *name;
    int     i, maxi;

    USE(c);
    /*
     * Find the longest environment variable
     */
    for (maxi = 0, ep = STR_environ; *ep; ep++) {
	for (i = 0, p = *ep; *p && *p != '='; p++, i++)
	    continue;
	if (i > maxi)
	    maxi = i;
    }

    name = xmalloc((maxi + 1) * sizeof(Char));
    cleanup_push(name, xfree);

    while (++v && *v) 
	for (maxi = 1; maxi;)
	    for (maxi = 0, ep = STR_environ; *ep; ep++) {
		for (n = name, p = *ep; *p && *p != '='; *n++ = *p++)
		    continue;
		*n = '\0';
		if (!Gmatch(name, *v))
		    continue;
		maxi = 1;

		/* Unset the name. This wasn't being done until
		 * later but most of the stuff following won't
		 * work (particularly the setlocale() and getenv()
		 * stuff) as intended until the name is actually
		 * removed. (sg)
		 */
		Unsetenv(name);

		if (eq(name, STRNOREBIND)) {
		    NoNLSRebind = 0;
		    MapsAreInited = 0;
		    NLSMapsAreInited = 0;
		    ed_InitMaps();
		}
#ifdef apollo
		else if (eq(name, STRSYSTYPE))
		    dohash(NULL, NULL);
#endif /* apollo */
		else if (islocale_var(name)) {
#ifdef NLS
		    int     k;

# ifdef SETLOCALEBUG
		    dont_free = 1;
# endif /* SETLOCALEBUG */
		    (void) setlocale(LC_ALL, "");
# ifdef LC_COLLATE
		    (void) setlocale(LC_COLLATE, "");
# endif
# ifdef LC_CTYPE
		    (void) setlocale(LC_CTYPE, ""); /* for iscntrl */
# endif /* LC_CTYPE */
# ifdef NLS_CATALOGS
#  ifdef LC_MESSAGES
		    (void) setlocale(LC_MESSAGES, "");
#  endif /* LC_MESSAGES */
		    nlsclose();
		    nlsinit();
# endif /* NLS_CATALOGS */
# ifdef SETLOCALEBUG
		    dont_free = 0;
# endif /* SETLOCALEBUG */
# ifdef STRCOLLBUG
		    fix_strcoll_bug();
# endif /* STRCOLLBUG */
		    tw_cmd_free();/* since the collation sequence has changed */
		    for (k = 0200; k <= 0377 && !Isprint(CTL_ESC(k)); k++)
			continue;
		    AsciiOnly = MB_CUR_MAX == 1 && k > 0377;
#else /* !NLS */
		    AsciiOnly = getenv("LANG") == NULL &&
			getenv("LC_CTYPE") == NULL;
#endif /* NLS */
		    NLSMapsAreInited = 0;
		    ed_Init();
		    if (MapsAreInited && !NLSMapsAreInited)
			ed_InitNLSMaps();

		}
#ifdef WINNT_NATIVE
		else if (eq(name,(STRtcshlang))) {
		    nls_dll_unload();
		    nlsinit();
		}
#endif /* WINNT_NATIVE */
#ifdef COLOR_LS_F
		else if (eq(name, STRLS_COLORS))
		    parseLS_COLORS(n);
		else if (eq(name, STRLSCOLORS))
		    parseLSCOLORS(n);
#endif /* COLOR_LS_F */
#ifdef NLS_CATALOGS
		else if (eq(name, STRNLSPATH)) {
		    nlsclose();
		    nlsinit();
		}
#endif
		/*
		 * start again cause the environment changes
		 */
		break;
	    }
    cleanup_until(name);
}

void
tsetenv(const Char *name, const Char *val)
{
#ifdef SETENV_IN_LIB
/*
 * XXX: This does not work right, since tcsh cannot track changes to
 * the environment this way. (the builtin setenv without arguments does
 * not print the right stuff neither does unsetenv). This was for Mach,
 * it is not needed anymore.
 */
#undef setenv
    char   *cname;

    if (name == NULL)
	return;
    cname = strsave(short2str(name));
    setenv(cname, short2str(val), 1);
    xfree(cname);
#else /* !SETENV_IN_LIB */
    Char **ep = STR_environ;
    const Char *ccp;
    Char *cp, *dp;
    Char   *blk[2];
    Char  **oep = ep;

#ifdef WINNT_NATIVE
    nt_set_env(name,val);
#endif /* WINNT_NATIVE */
    for (; *ep; ep++) {
#ifdef WINNT_NATIVE
	for (ccp = name, dp = *ep; *ccp && Tolower(*ccp & TRIM) == Tolower(*dp);
				ccp++, dp++)
#else
	for (ccp = name, dp = *ep; *ccp && (*ccp & TRIM) == *dp; ccp++, dp++)
#endif /* WINNT_NATIVE */
	    continue;
	if (*ccp != 0 || *dp != '=')
	    continue;
	cp = Strspl(STRequal, val);
	xfree(*ep);
	*ep = strip(Strspl(name, cp));
	xfree(cp);
	blkfree((Char **) environ);
	environ = short2blk(STR_environ);
	return;
    }
    cp = Strspl(name, STRequal);
    blk[0] = strip(Strspl(cp, val));
    xfree(cp);
    blk[1] = 0;
    STR_environ = blkspl(STR_environ, blk);
    blkfree((Char **) environ);
    environ = short2blk(STR_environ);
    xfree(oep);
#endif /* SETENV_IN_LIB */
}

void
Unsetenv(Char *name)
{
    Char **ep = STR_environ;
    Char *cp, *dp;
    Char **oep = ep;

#ifdef WINNT_NATIVE
	nt_set_env(name,NULL);
#endif /*WINNT_NATIVE */
    for (; *ep; ep++) {
	for (cp = name, dp = *ep; *cp && *cp == *dp; cp++, dp++)
	    continue;
	if (*cp != 0 || *dp != '=')
	    continue;
	cp = *ep;
	*ep = 0;
	STR_environ = blkspl(STR_environ, ep + 1);
	blkfree((Char **) environ);
	environ = short2blk(STR_environ);
	*ep = cp;
	xfree(cp);
	xfree(oep);
	return;
    }
}

/*ARGSUSED*/
void
doumask(Char **v, struct command *c)
{
    Char *cp = v[1];
    int i;

    USE(c);
    if (cp == 0) {
	i = (int)umask(0);
	(void) umask(i);
	xprintf("%o\n", i);
	return;
    }
    i = 0;
    while (Isdigit(*cp) && *cp != '8' && *cp != '9')
	i = i * 8 + *cp++ - '0';
    if (*cp || i < 0 || i > 0777)
	stderror(ERR_NAME | ERR_MASK);
    (void) umask(i);
}

#ifndef HAVENOLIMIT
# ifndef BSDLIMIT
   typedef long RLIM_TYPE;
#  ifdef _OSD_POSIX /* BS2000 */
#   include <ulimit.h>
#  endif
#  ifndef RLIM_INFINITY
#   if !defined(_MINIX) && !defined(__clipper__) && !defined(_CRAY)
    extern RLIM_TYPE ulimit();
#   endif /* ! _MINIX && !__clipper__ */
#   define RLIM_INFINITY 0x003fffff
#   define RLIMIT_FSIZE 1
#  endif /* RLIM_INFINITY */
#  ifdef aiws
#   define toset(a) (((a) == 3) ? 1004 : (a) + 1)
#   define RLIMIT_DATA	3
#   define RLIMIT_STACK 1005
#  else /* aiws */
#   define toset(a) ((a) + 1)
#  endif /* aiws */
# else /* BSDLIMIT */
#  if (defined(BSD4_4) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__) || (HPUXVERSION >= 1100)) && !defined(__386BSD__)
    typedef rlim_t RLIM_TYPE;
#  else
#   if defined(SOLARIS2) || (defined(sgi) && SYSVREL > 3)
     typedef rlim_t RLIM_TYPE;
#   else
#    if defined(_SX)
      typedef long long RLIM_TYPE;
#    else /* !_SX */
      typedef unsigned long RLIM_TYPE;
#    endif /* _SX */
#   endif /* SOLARIS2 || (sgi && SYSVREL > 3) */
#  endif /* BSD4_4 && !__386BSD__  */
# endif /* BSDLIMIT */

# if (HPUXVERSION > 700) && (HPUXVERSION < 1100) && defined(BSDLIMIT)
/* Yes hpux8.0 has limits but <sys/resource.h> does not make them public */
/* Yes, we could have defined _KERNEL, and -I/etc/conf/h, but is that better? */
#  ifndef RLIMIT_CPU
#   define RLIMIT_CPU		0
#   define RLIMIT_FSIZE		1
#   define RLIMIT_DATA		2
#   define RLIMIT_STACK		3
#   define RLIMIT_CORE		4
#   define RLIMIT_RSS		5
#   define RLIMIT_NOFILE	6
#  endif /* RLIMIT_CPU */
#  ifndef RLIM_INFINITY
#   define RLIM_INFINITY	0x7fffffff
#  endif /* RLIM_INFINITY */
   /*
    * old versions of HP/UX counted limits in 512 bytes
    */
#  ifndef SIGRTMIN
#   define FILESIZE512
#  endif /* SIGRTMIN */
# endif /* (HPUXVERSION > 700) && (HPUXVERSION < 1100) && BSDLIMIT */

# if SYSVREL > 3 && defined(BSDLIMIT) && !defined(_SX)
/* In order to use rusage, we included "/usr/ucbinclude/sys/resource.h" in */
/* sh.h.  However, some SVR4 limits are defined in <sys/resource.h>.  Rather */
/* than include both and get warnings, we define the extra SVR4 limits here. */
/* XXX: I don't understand if RLIMIT_AS is defined, why don't we define */
/* RLIMIT_VMEM based on it? */
#  ifndef RLIMIT_VMEM
#   define RLIMIT_VMEM	6
#  endif
#  ifndef RLIMIT_AS
#   define RLIMIT_AS	RLIMIT_VMEM
#  endif
# endif /* SYSVREL > 3 && BSDLIMIT */

# if (defined(__linux__) || defined(__GNU__) || defined(__GLIBC__))
#  if defined(RLIMIT_AS) && !defined(RLIMIT_VMEM)
#   define RLIMIT_VMEM	RLIMIT_AS
#  endif
/*
 * Oh well, <asm-generic/resource.h> has it, but <bits/resource.h> does not
 * Linux headers: When the left hand does not know what the right hand does.
 */
#  if defined(RLIMIT_RTPRIO) && !defined(RLIMIT_RTTIME)
#   define RLIMIT_RTTIME (RLIMIT_RTPRIO + 1)
#  endif
# endif

struct limits limits[] = 
{
# ifdef RLIMIT_CPU
    { RLIMIT_CPU, 	"cputime",	1,	"seconds"	},
# endif /* RLIMIT_CPU */

# ifdef RLIMIT_FSIZE
#  ifndef aiws
    { RLIMIT_FSIZE, 	"filesize",	1024,	"kbytes"	},
#  else
    { RLIMIT_FSIZE, 	"filesize",	512,	"blocks"	},
#  endif /* aiws */
# endif /* RLIMIT_FSIZE */

# ifdef RLIMIT_DATA
    { RLIMIT_DATA, 	"datasize",	1024,	"kbytes"	},
# endif /* RLIMIT_DATA */

# ifdef RLIMIT_STACK
#  ifndef aiws
    { RLIMIT_STACK, 	"stacksize",	1024,	"kbytes"	},
#  else
    { RLIMIT_STACK, 	"stacksize",	1024 * 1024,	"kbytes"},
#  endif /* aiws */
# endif /* RLIMIT_STACK */

# ifdef RLIMIT_CORE
    { RLIMIT_CORE, 	"coredumpsize",	1024,	"kbytes"	},
# endif /* RLIMIT_CORE */

# ifdef RLIMIT_RSS
    { RLIMIT_RSS, 	"memoryuse",	1024,	"kbytes"	},
# endif /* RLIMIT_RSS */

# ifdef RLIMIT_UMEM
    { RLIMIT_UMEM, 	"memoryuse",	1024,	"kbytes"	},
# endif /* RLIMIT_UMEM */

# ifdef RLIMIT_VMEM
    { RLIMIT_VMEM, 	"vmemoryuse",	1024,	"kbytes"	},
# endif /* RLIMIT_VMEM */

# if defined(RLIMIT_HEAP) /* found on BS2000/OSD systems */
    { RLIMIT_HEAP,	"heapsize",	1024,	"kbytes"	},
# endif /* RLIMIT_HEAP */

# ifdef RLIMIT_NOFILE
    { RLIMIT_NOFILE, 	"descriptors", 1,	""		},
# endif /* RLIMIT_NOFILE */

# ifdef RLIMIT_NPTS
    { RLIMIT_NPTS,	"pseudoterminals", 1,	""		},
# endif /* RLIMIT_NPTS */

# ifdef RLIMIT_KQUEUES
    { RLIMIT_KQUEUES,	"kqueues",	1,	""		},
# endif /* RLIMIT_KQUEUES */

# ifdef RLIMIT_CONCUR
    { RLIMIT_CONCUR, 	"concurrency", 1,	"thread(s)"	},
# endif /* RLIMIT_CONCUR */

# ifdef RLIMIT_MEMLOCK
    { RLIMIT_MEMLOCK,	"memorylocked",	1024,	"kbytes"	},
# endif /* RLIMIT_MEMLOCK */

# ifdef RLIMIT_NPROC
    { RLIMIT_NPROC,	"maxproc",	1,	""		},
# endif /* RLIMIT_NPROC */

# ifdef RLIMIT_NTHR
    { RLIMIT_NTHR,	"maxthread",	1,	""		},
# endif /* RLIMIT_NTHR */

# if defined(RLIMIT_OFILE) && !defined(RLIMIT_NOFILE)
    { RLIMIT_OFILE,	"openfiles",	1,	""		},
# endif /* RLIMIT_OFILE && !defined(RLIMIT_NOFILE) */

# ifdef RLIMIT_SBSIZE
    { RLIMIT_SBSIZE,	"sbsize",	1,	""		},
# endif /* RLIMIT_SBSIZE */

# ifdef RLIMIT_SWAP 
    { RLIMIT_SWAP,	"swapsize",	1024,	"kbytes"	}, 
# endif /* RLIMIT_SWAP */ 

# ifdef RLIMIT_LOCKS 
    { RLIMIT_LOCKS,	"maxlocks",	1,	""		}, 
# endif /* RLIMIT_LOCKS */ 

# ifdef RLIMIT_POSIXLOCKS
    { RLIMIT_POSIXLOCKS,"posixlocks",	1,	""		},
# endif /* RLIMIT_POSIXLOCKS */

# ifdef RLIMIT_SIGPENDING 
    { RLIMIT_SIGPENDING,"maxsignal",	1,	""		}, 
# endif /* RLIMIT_SIGPENDING */ 

# ifdef RLIMIT_MSGQUEUE 
    { RLIMIT_MSGQUEUE,	"maxmessage",	1,	""		}, 
# endif /* RLIMIT_MSGQUEUE */ 

# ifdef RLIMIT_NICE 
    { RLIMIT_NICE,	"maxnice",	1,	""		}, 
# endif /* RLIMIT_NICE */ 

# ifdef RLIMIT_RTPRIO 
    { RLIMIT_RTPRIO,	"maxrtprio",	1,	""		}, 
# endif /* RLIMIT_RTPRIO */ 

# ifdef RLIMIT_RTTIME 
    { RLIMIT_RTTIME,	"maxrttime",	1,	"usec"		}, 
# endif /* RLIMIT_RTTIME */ 

    { -1, 		NULL, 		0, 	NULL		}
};

static struct limits *findlim	(Char *);
static RLIM_TYPE getval		(struct limits *, Char **);
static int strtail		(Char *, const char *);
static void limtail		(Char *, const char *);
static void limtail2		(Char *, const char *, const char *);
static void plim		(struct limits *, int);
static int setlim		(struct limits *, int, RLIM_TYPE);

#ifdef convex
static  RLIM_TYPE
restrict_limit(double value)
{
    /*
     * is f too large to cope with? return the maximum or minimum int
     */
    if (value > (double) INT_MAX)
	return (RLIM_TYPE) INT_MAX;
    else if (value < (double) INT_MIN)
	return (RLIM_TYPE) INT_MIN;
    else
	return (RLIM_TYPE) value;
}
#else /* !convex */
# define restrict_limit(x)	((RLIM_TYPE) (x))
#endif /* convex */


static struct limits *
findlim(Char *cp)
{
    struct limits *lp, *res;

    res = NULL;
    for (lp = limits; lp->limconst >= 0; lp++)
	if (prefix(cp, str2short(lp->limname))) {
	    if (res)
		stderror(ERR_NAME | ERR_AMBIG);
	    res = lp;
	}
    if (res)
	return (res);
    stderror(ERR_NAME | ERR_LIMIT);
    /* NOTREACHED */
    return (0);
}

/*ARGSUSED*/
void
dolimit(Char **v, struct command *c)
{
    struct limits *lp;
    RLIM_TYPE limit;
    int    hard = 0;

    USE(c);
    v++;
    if (*v && eq(*v, STRmh)) {
	hard = 1;
	v++;
    }
    if (*v == 0) {
	for (lp = limits; lp->limconst >= 0; lp++)
	    plim(lp, hard);
	return;
    }
    lp = findlim(v[0]);
    if (v[1] == 0) {
	plim(lp, hard);
	return;
    }
    limit = getval(lp, v + 1);
    if (setlim(lp, hard, limit) < 0)
	stderror(ERR_SILENT);
}

static  RLIM_TYPE
getval(struct limits *lp, Char **v)
{
    float f;
    Char   *cp = *v++;

    f = atof(short2str(cp));

# ifdef convex
    /*
     * is f too large to cope with. limit f to minint, maxint  - X-6768 by
     * strike
     */
    if ((f < (double) INT_MIN) || (f > (double) INT_MAX)) {
	stderror(ERR_NAME | ERR_TOOLARGE);
    }
# endif /* convex */

    while (Isdigit(*cp) || *cp == '.' || *cp == 'e' || *cp == 'E')
	cp++;
    if (*cp == 0) {
	if (*v == 0)
	    return restrict_limit((f * lp->limdiv) + 0.5);
	cp = *v;
    }
    switch (*cp) {
# ifdef RLIMIT_CPU
    case ':':
	if (lp->limconst != RLIMIT_CPU)
	    goto badscal;
	return f == 0.0 ? (RLIM_TYPE) 0 : restrict_limit((f * 60.0 + atof(short2str(cp + 1))));
    case 'h':
	if (lp->limconst != RLIMIT_CPU)
	    goto badscal;
	limtail(cp, "hours");
	f *= 3600.0;
	break;
# endif /* RLIMIT_CPU */
    case 'm':
# ifdef RLIMIT_CPU
	if (lp->limconst == RLIMIT_CPU) {
	    limtail(cp, "minutes");
	    f *= 60.0;
	    break;
	}
# endif /* RLIMIT_CPU */
	limtail2(cp, "megabytes", "mbytes");
	f *= 1024.0 * 1024.0;
	break;
# ifdef RLIMIT_CPU
    case 's':
	if (lp->limconst != RLIMIT_CPU)
	    goto badscal;
	limtail(cp, "seconds");
	break;
# endif /* RLIMIT_CPU */
    case 'G':
	*cp = 'g';
	/*FALLTHROUGH*/
    case 'g':
# ifdef RLIMIT_CPU
	if (lp->limconst == RLIMIT_CPU)
	    goto badscal;
# endif /* RLIMIT_CPU */
	limtail2(cp, "gigabytes", "gbytes");
	f *= 1024.0 * 1024.0 * 1024.0;
	break;
    case 'M':
# ifdef RLIMIT_CPU
	if (lp->limconst == RLIMIT_CPU)
	    goto badscal;
# endif /* RLIMIT_CPU */
	*cp = 'm';
	limtail2(cp, "megabytes", "mbytes");
	f *= 1024.0 * 1024.0;
	break;
    case 'k':
# ifdef RLIMIT_CPU
	if (lp->limconst == RLIMIT_CPU)
	    goto badscal;
# endif /* RLIMIT_CPU */
	limtail2(cp, "kilobytes", "kbytes");
	f *= 1024.0;
	break;
    case 'b':
# ifdef RLIMIT_CPU
	if (lp->limconst == RLIMIT_CPU)
	    goto badscal;
# endif /* RLIMIT_CPU */
	limtail(cp, "blocks");
	f *= 512.0;
	break;
    case 'u':
	limtail(cp, "unlimited");
	return ((RLIM_TYPE) RLIM_INFINITY);
    default:
# ifdef RLIMIT_CPU
badscal:
# endif /* RLIMIT_CPU */
	stderror(ERR_NAME | ERR_SCALEF);
    }
# ifdef convex
    return f == 0.0 ? (RLIM_TYPE) 0 : restrict_limit((f + 0.5));
# else
    f += 0.5;
    if (f > (float) ((RLIM_TYPE) RLIM_INFINITY))
	return ((RLIM_TYPE) RLIM_INFINITY);
    else
	return ((RLIM_TYPE) f);
# endif /* convex */
}

static int
strtail(Char *cp, const char *str)
{
    while (*cp && *cp == (Char)*str)
	cp++, str++;
    return (*cp != '\0');
}

static void
limtail(Char *cp, const char *str)
{
    if (strtail(cp, str))
	stderror(ERR_BADSCALE, str);
}

static void
limtail2(Char *cp, const char *str1, const char *str2)
{
    if (strtail(cp, str1) && strtail(cp, str2))
	stderror(ERR_BADSCALE, str1);
}

/*ARGSUSED*/
static void
plim(struct limits *lp, int hard)
{
# ifdef BSDLIMIT
    struct rlimit rlim;
# endif /* BSDLIMIT */
    RLIM_TYPE limit;
    int     xdiv = lp->limdiv;

    xprintf("%-13.13s", lp->limname);

# ifndef BSDLIMIT
    limit = ulimit(lp->limconst, 0);
#  ifdef aiws
    if (lp->limconst == RLIMIT_DATA)
	limit -= 0x20000000;
#  endif /* aiws */
# else /* BSDLIMIT */
    (void) getrlimit(lp->limconst, &rlim);
    limit = hard ? rlim.rlim_max : rlim.rlim_cur;
# endif /* BSDLIMIT */

# if !defined(BSDLIMIT) || defined(FILESIZE512)
    /*
     * Christos: filesize comes in 512 blocks. we divide by 2 to get 1024
     * blocks. Note we cannot pre-multiply cause we might overflow (A/UX)
     */
    if (lp->limconst == RLIMIT_FSIZE) {
	if (limit >= (RLIM_INFINITY / 512))
	    limit = RLIM_INFINITY;
	else
	    xdiv = (xdiv == 1024 ? 2 : 1);
    }
# endif /* !BSDLIMIT || FILESIZE512 */

    if (limit == RLIM_INFINITY)
	xprintf("unlimited");
    else
# if defined(RLIMIT_CPU) && defined(_OSD_POSIX)
    if (lp->limconst == RLIMIT_CPU &&
        (unsigned long)limit >= 0x7ffffffdUL)
	xprintf("unlimited");
    else
# endif
# ifdef RLIMIT_CPU
    if (lp->limconst == RLIMIT_CPU)
	psecs(limit);
    else
# endif /* RLIMIT_CPU */
	xprintf("%ld %s", (long) (limit / xdiv), lp->limscale);
    xputchar('\n');
}

/*ARGSUSED*/
void
dounlimit(Char **v, struct command *c)
{
    struct limits *lp;
    int    lerr = 0;
    int    hard = 0;
    int	   force = 0;

    USE(c);
    while (*++v && **v == '-') {
	Char   *vp = *v;
	while (*++vp)
	    switch (*vp) {
	    case 'f':
		force = 1;
		break;
	    case 'h':
		hard = 1;
		break;
	    default:
		stderror(ERR_ULIMUS);
		break;
	    }
    }

    if (*v == 0) {
	for (lp = limits; lp->limconst >= 0; lp++)
	    if (setlim(lp, hard, (RLIM_TYPE) RLIM_INFINITY) < 0)
		lerr++;
	if (!force && lerr)
	    stderror(ERR_SILENT);
	return;
    }
    while (*v) {
	lp = findlim(*v++);
	if (setlim(lp, hard, (RLIM_TYPE) RLIM_INFINITY) < 0 && !force)
	    stderror(ERR_SILENT);
    }
}

static int
setlim(struct limits *lp, int hard, RLIM_TYPE limit)
{
# ifdef BSDLIMIT
    struct rlimit rlim;

    (void) getrlimit(lp->limconst, &rlim);

#  ifdef FILESIZE512
    /* Even though hpux has setrlimit(), it expects fsize in 512 byte blocks */
    if (limit != RLIM_INFINITY && lp->limconst == RLIMIT_FSIZE)
	limit /= 512;
#  endif /* FILESIZE512 */
    if (hard)
	rlim.rlim_max = limit;
    else if (limit == RLIM_INFINITY && euid != 0)
	rlim.rlim_cur = rlim.rlim_max;
    else
	rlim.rlim_cur = limit;

    if (rlim.rlim_cur > rlim.rlim_max)
	rlim.rlim_max = rlim.rlim_cur;

    if (setrlimit(lp->limconst, &rlim) < 0) {
# else /* BSDLIMIT */
    if (limit != RLIM_INFINITY && lp->limconst == RLIMIT_FSIZE)
	limit /= 512;
# ifdef aiws
    if (lp->limconst == RLIMIT_DATA)
	limit += 0x20000000;
# endif /* aiws */
    if (ulimit(toset(lp->limconst), limit) < 0) {
# endif /* BSDLIMIT */
        int err;
        char *op, *type;

	err = errno;
	op = strsave(limit == RLIM_INFINITY ? CGETS(15, 2, "remove") :
		     	CGETS(15, 3, "set"));
	cleanup_push(op, xfree);
	type = strsave(hard ? CGETS(15, 4, " hard") : "");
	cleanup_push(type, xfree);
	xprintf(CGETS(15, 1, "%s: %s: Can't %s%s limit (%s)\n"), bname,
	    lp->limname, op, type, strerror(err));
	cleanup_until(op);
	return (-1);
    }
    return (0);
}

#endif /* !HAVENOLIMIT */

/*ARGSUSED*/
void
dosuspend(Char **v, struct command *c)
{
#ifdef BSDJOBS
    struct sigaction old;
#endif /* BSDJOBS */

    USE(c);
    USE(v);

    if (loginsh)
	stderror(ERR_SUSPLOG);
    untty();

#ifdef BSDJOBS
    sigaction(SIGTSTP, NULL, &old);
    signal(SIGTSTP, SIG_DFL);
    (void) kill(0, SIGTSTP);
    /* the shell stops here */
    sigaction(SIGTSTP, &old, NULL);
#else /* !BSDJOBS */
    stderror(ERR_JOBCONTROL);
#endif /* BSDJOBS */

#ifdef BSDJOBS
    if (tpgrp != -1) {
	if (grabpgrp(FSHTTY, opgrp) == -1)
	    stderror(ERR_SYSTEM, "tcgetpgrp", strerror(errno));
	(void) setpgid(0, shpgrp);
	(void) tcsetpgrp(FSHTTY, shpgrp);
    }
#endif /* BSDJOBS */
    (void) setdisc(FSHTTY);
}

/* This is the dreaded EVAL built-in.
 *   If you don't fiddle with file descriptors, and reset didfds,
 *   this command will either ignore redirection inside or outside
 *   its arguments, e.g. eval "date >x"  vs.  eval "date" >x
 *   The stuff here seems to work, but I did it by trial and error rather
 *   than really knowing what was going on.  If tpgrp is zero, we are
 *   probably a background eval, e.g. "eval date &", and we want to
 *   make sure that any processes we start stay in our pgrp.
 *   This is also the case for "time eval date" -- stay in same pgrp.
 *   Otherwise, under stty tostop, processes will stop in the wrong
 *   pgrp, with no way for the shell to get them going again.  -IAN!
 */

struct doeval_state
{
    Char **evalvec, *evalp;
    int didfds;
#ifndef CLOSE_ON_EXEC
    int didcch;
#endif
    int saveIN, saveOUT, saveDIAG;
    int SHIN, SHOUT, SHDIAG;
};

static void
doeval_cleanup(void *xstate)
{
    struct doeval_state *state;

    state = xstate;
    evalvec = state->evalvec;
    evalp = state->evalp;
    doneinp = 0;
#ifndef CLOSE_ON_EXEC
    didcch = state->didcch;
#endif /* CLOSE_ON_EXEC */
    didfds = state->didfds;
    if (state->saveIN != SHIN)
	xclose(SHIN);
    if (state->saveOUT != SHOUT)
	xclose(SHOUT);
    if (state->saveDIAG != SHDIAG)
	xclose(SHDIAG);
    close_on_exec(SHIN = dmove(state->saveIN, state->SHIN), 1);
    close_on_exec(SHOUT = dmove(state->saveOUT, state->SHOUT), 1);
    close_on_exec(SHDIAG = dmove(state->saveDIAG, state->SHDIAG), 1);
    if (didfds) {
	close_on_exec(dcopy(SHIN, 0), 1);
	close_on_exec(dcopy(SHOUT, 1), 1);
	close_on_exec(dcopy(SHDIAG, 2), 1);
    }
}

static Char **Ggv;
/*ARGSUSED*/
void
doeval(Char **v, struct command *c)
{
    struct doeval_state state;
    int gflag, my_reenter;
    Char **gv;
    jmp_buf_t osetexit;

    USE(c);
    v++;
    if (*v == 0)
	return;
    gflag = tglob(v);
    if (gflag) {
	gv = v = globall(v, gflag);
	if (v == 0)
	    stderror(ERR_NOMATCH);
	cleanup_push(gv, blk_cleanup);
	v = copyblk(v);
    }
    else {
	gv = NULL;
	v = copyblk(v);
	trim(v);
    }

    Ggv = gv;
    state.evalvec = evalvec;
    state.evalp = evalp;
    state.didfds = didfds;
#ifndef CLOSE_ON_EXEC
    state.didcch = didcch;
#endif /* CLOSE_ON_EXEC */
    state.SHIN = SHIN;
    state.SHOUT = SHOUT;
    state.SHDIAG = SHDIAG;

    (void)close_on_exec(state.saveIN = dcopy(SHIN, -1), 1);
    (void)close_on_exec(state.saveOUT = dcopy(SHOUT, -1), 1);
    (void)close_on_exec(state.saveDIAG = dcopy(SHDIAG, -1), 1);

    cleanup_push(&state, doeval_cleanup);

    getexit(osetexit);

    /* PWP: setjmp/longjmp bugfix for optimizing compilers */
#ifdef cray
    my_reenter = 1;             /* assume non-zero return val */
    if (setexit() == 0) {
	my_reenter = 0;         /* Oh well, we were wrong */
#else /* !cray */
    if ((my_reenter = setexit()) == 0) {
#endif /* cray */
	evalvec = v;
	evalp = 0;
	(void)close_on_exec(SHIN = dcopy(0, -1), 1);
	(void)close_on_exec(SHOUT = dcopy(1, -1), 1);
	(void)close_on_exec(SHDIAG = dcopy(2, -1), 1);
#ifndef CLOSE_ON_EXEC
	didcch = 0;
#endif /* CLOSE_ON_EXEC */
	didfds = 0;
	gv = Ggv;
	process(0);
	Ggv = gv;
    }

    if (my_reenter == 0) {
	cleanup_until(&state);
	if (Ggv)
	    cleanup_until(Ggv);
    }

    resexit(osetexit);
    if (my_reenter)
	stderror(ERR_SILENT);
}

/*************************************************************************/
/* print list of builtin commands */

static void
lbuffed_cleanup (void *dummy)
{
    USE(dummy);
    lbuffed = 1;
}

/*ARGSUSED*/
void
dobuiltins(Char **v, struct command *c)
{
    /* would use print_by_column() in tw.parse.c but that assumes
     * we have an array of Char * to pass.. (sg)
     */
    const struct biltins *b;
    int row, col, columns, rows;
    unsigned int w, maxwidth;

    USE(c);
    USE(v);
    lbuffed = 0;		/* turn off line buffering */
    cleanup_push(&lbuffed, lbuffed_cleanup);

    /* find widest string */
    for (maxwidth = 0, b = bfunc; b < &bfunc[nbfunc]; ++b)
	maxwidth = max(maxwidth, strlen(b->bname));
    ++maxwidth;					/* for space */

    columns = (TermH + 1) / maxwidth;	/* PWP: terminal size change */
    if (!columns)
	columns = 1;
    rows = (nbfunc + (columns - 1)) / columns;

    for (b = bfunc, row = 0; row < rows; row++) {
	for (col = 0; col < columns; col++) {
	    if (b < &bfunc[nbfunc]) {
		w = strlen(b->bname);
		xprintf("%s", b->bname);
		if (col < (columns - 1))	/* Not last column? */
		    for (; w < maxwidth; w++)
			xputchar(' ');
		++b;
	    }
	}
	if (row < (rows - 1)) {
	    if (Tty_raw_mode)
		xputchar('\r');
	    xputchar('\n');
	}
    }
#ifdef WINNT_NATIVE
    nt_print_builtins(maxwidth);
#else
    if (Tty_raw_mode)
	xputchar('\r');
    xputchar('\n');
#endif /* WINNT_NATIVE */

    cleanup_until(&lbuffed);		/* turn back on line buffering */
    flush();
}

#ifdef NLS_CATALOGS
char *
xcatgets(nl_catd ctd, int set_id, int msg_id, const char *s)
{
    char *res;

    errno = 0;
    while ((res = catgets(ctd, set_id, msg_id, s)) == s && errno == EINTR) {
	handle_pending_signals();
	errno = 0;
    }
    return res;
}


# if defined(HAVE_ICONV) && defined(HAVE_NL_LANGINFO)
char *
iconv_catgets(nl_catd ctd, int set_id, int msg_id, const char *s)
{
    static char *buf = NULL;
    static size_t buf_size = 0;
  
    char *orig, *dest, *p;
    ICONV_CONST char *src;
    size_t src_size, dest_size;
  
    orig = xcatgets(ctd, set_id, msg_id, s);
    if (catgets_iconv == (iconv_t)-1 || orig == s)
        return orig;
    src = orig;
    src_size = strlen(src) + 1;
    if (buf == NULL && (buf = xmalloc(buf_size = src_size + 32)) == NULL)
	return orig;
    dest = buf;
    while (src_size != 0) {
        dest_size = buf + buf_size - dest;
	if (iconv(catgets_iconv, &src, &src_size, &dest, &dest_size)
	    == (size_t)-1) {
	    switch (errno) {
	    case E2BIG:
		if ((p = xrealloc(buf, buf_size * 2)) == NULL)
		    return orig;
		buf_size *= 2;
		dest = p + (dest - buf);
		buf = p;
		break;
		
	    case EILSEQ: case EINVAL: default:
		return orig;
	    }
	}
    }
    return buf;
}
# endif /* HAVE_ICONV && HAVE_NL_LANGINFO */
#endif /* NLS_CATALOGS */

void
nlsinit(void)
{
#ifdef NLS_CATALOGS
    static const char default_catalog[] = "tcsh";

    char *catalog = (char *)(intptr_t)default_catalog;

    if (adrof(STRcatalog) != NULL)
	catalog = xasprintf("tcsh.%s", short2str(varval(STRcatalog)));
#ifdef NL_CAT_LOCALE /* POSIX-compliant. */
    /*
     * Check if LC_MESSAGES is set in the environment and use it, if so.
     * If not, fall back to the setting of LANG.
     */
    catd = catopen(catalog, tgetenv(STRLC_MESSAGES) ? NL_CAT_LOCALE : 0);
#else /* pre-POSIX */
# ifndef MCLoadBySet
#  define MCLoadBySet 0
#  endif
    catd = catopen(catalog, MCLoadBySet);
#endif
    if (catalog != default_catalog)
	xfree(catalog);
#if defined(HAVE_ICONV) && defined(HAVE_NL_LANGINFO)
    /* xcatgets (), not CGETS, the charset name should be in ASCII anyway. */
    catgets_iconv = iconv_open (nl_langinfo (CODESET),
				xcatgets(catd, 255, 1, "UTF-8"));
#endif /* HAVE_ICONV && HAVE_NL_LANGINFO */
#endif /* NLS_CATALOGS */
#ifdef WINNT_NATIVE
    nls_dll_init();
#endif /* WINNT_NATIVE */
    errinit();		/* init the errorlist in correct locale */
    mesginit();		/* init the messages for signals */
    dateinit();		/* init the messages for dates */
    editinit();		/* init the editor messages */
    terminit();		/* init the termcap messages */
}

void
nlsclose(void)
{
#ifdef NLS_CATALOGS
#if defined(HAVE_ICONV) && defined(HAVE_NL_LANGINFO)
    if (catgets_iconv != (iconv_t)-1) {
	iconv_close(catgets_iconv);
	catgets_iconv = (iconv_t)-1;
    }
#endif /* HAVE_ICONV && HAVE_NL_LANGINFO */
    if (catd != (nl_catd)-1) {
	/*
	 * catclose can call other functions which can call longjmp
	 * making us re-enter this code. Prevent infinite recursion
	 * by resetting catd. Problem reported and solved by:
	 * Gerhard Niklasch
	 */
	nl_catd oldcatd = catd;
	catd = (nl_catd)-1;
	while (catclose(oldcatd) == -1 && errno == EINTR)
	    handle_pending_signals();
    }
#endif /* NLS_CATALOGS */
}

int
getYN(const char *prompt)
{
    int doit;
    char c;

    xprintf("%s", prompt);
    flush();
    (void) force_read(SHIN, &c, sizeof(c));
    /* 
     * Perhaps we should use the yesexpr from the
     * actual locale
     */
    doit = (strchr(CGETS(22, 14, "Yy"), c) != NULL);
    while (c != '\n' && force_read(SHIN, &c, sizeof(c)) == sizeof(c))
	continue;
    return doit;
}
