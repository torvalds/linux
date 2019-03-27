/* $Header: /p/tcsh/cvsroot/tcsh/tc.func.c,v 3.158 2016/05/13 15:08:12 christos Exp $ */
/*
 * tc.func.c: New tcsh builtins.
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

RCSID("$tcsh: tc.func.c,v 3.158 2016/05/13 15:08:12 christos Exp $")

#include "ed.h"
#include "ed.defns.h"		/* for the function names */
#include "tw.h"
#include "tc.h"
#ifdef WINNT_NATIVE
#include "nt.const.h"
#else /* WINNT_NATIVE */
#include <sys/wait.h>
#endif /* WINNT_NATIVE */

#ifdef AFS
#include <afs/stds.h>
#include <afs/kautils.h>
long ka_UserAuthenticateGeneral();
#endif /* AFS */

#ifdef TESLA
extern int do_logout;
#endif /* TESLA */
extern time_t t_period;
extern int just_signaled;
static int precmd_active = 0;
static int jobcmd_active = 0; /* GrP */
static int postcmd_active = 0;
static int periodic_active = 0;
static int cwdcmd_active = 0;	/* PWP: for cwd_cmd */
static int beepcmd_active = 0;
static void (*alm_fun)(void) = NULL;

static	void	 auto_logout	(void);
static	char	*xgetpass	(const char *);
static	void	 auto_lock	(void);
#ifdef BSDJOBS
static	void	 insert		(struct wordent *, int);
static	void	 insert_we	(struct wordent *, struct wordent *);
static	int	 inlist		(Char *, Char *);
#endif /* BSDJOBS */
static	int	 tildecompare	(const void *, const void *);
static  Char    *gethomedir	(const Char *);
#ifdef REMOTEHOST
static	void	 palarm		(int);
static	void	 getremotehost	(int);
#endif /* REMOTEHOST */

/*
 * Tops-C shell
 */

/*
 * expand_lex: Take the given lex and return an expanded version of it.
 * First guy in lex list is ignored; last guy is ^J which we ignore.
 * Only take lex'es from position 'from' to position 'to' inclusive
 *
 * Note: csh sometimes sets bit 8 in characters which causes all kinds
 * of problems if we don't mask it here. Note: excl's in lexes have been
 * un-back-slashed and must be re-back-slashed
 *
 */
/* PWP: this is a combination of the old sprlex() and the expand_lex from
   the magic-space stuff */

Char   *
expand_lex(const struct wordent *sp0, int from, int to)
{
    struct Strbuf buf = Strbuf_INIT;
    const struct wordent *sp;
    Char *s;
    Char prev_c;
    int i;

    prev_c = '\0';

    if (!sp0 || (sp = sp0->next) == sp0 || sp == (sp0 = sp0->prev))
	return Strbuf_finish(&buf); /* null lex */

    for (i = 0; ; i++) {
	if ((i >= from) && (i <= to)) {	/* if in range */
	    for (s = sp->word; *s; s++) {
		/*
		 * bugfix by Michael Bloom: anything but the current history
		 * character {(PWP) and backslash} seem to be dealt with
		 * elsewhere.
		 */
		if ((*s & QUOTE)
		    && (((*s & TRIM) == HIST && HIST != '\0') ||
			(((*s & TRIM) == '\'') && (prev_c != '\\')) ||
			(((*s & TRIM) == '\"') && (prev_c != '\\')))) {
		    Strbuf_append1(&buf, '\\');
		}
#if INVALID_BYTE != 0
		if ((*s & INVALID_BYTE) != INVALID_BYTE) /* *s < INVALID_BYTE */
		    Strbuf_append1(&buf, *s & TRIM);
		else
		    Strbuf_append1(&buf, *s);
#else
		Strbuf_append1(&buf, *s & TRIM);
#endif
		prev_c = *s;
	    }
	    Strbuf_append1(&buf, ' ');
	}
	sp = sp->next;
	if (sp == sp0)
	    break;
    }
    if (buf.len != 0)
	buf.len--;		/* get rid of trailing space */

    return Strbuf_finish(&buf);
}

Char   *
sprlex(const struct wordent *sp0)
{
    return expand_lex(sp0, 0, INT_MAX);
}


Char *
Itoa(int n, size_t min_digits, Char attributes)
{
    /*
     * The array size here is derived from
     *	log8(UINT_MAX)
     * which is guaranteed to be enough for a decimal
     * representation.  We add 1 because integer divide
     * rounds down.
     */
#ifndef CHAR_BIT
# define CHAR_BIT 8
#endif
    Char buf[CHAR_BIT * sizeof(int) / 3 + 1], *res, *p, *s;
    unsigned int un;	/* handle most negative # too */
    int pad = (min_digits != 0);

    if (sizeof(buf) - 1 < min_digits)
	min_digits = sizeof(buf) - 1;

    un = n;
    if (n < 0)
	un = -n;

    p = buf;
    do {
	*p++ = un % 10 + '0';
	un /= 10;
    } while ((pad && (ssize_t)--min_digits > 0) || un != 0);

    res = xmalloc((p - buf + 2) * sizeof(*res));
    s = res;
    if (n < 0)
	*s++ = '-';
    while (p > buf)
	*s++ = *--p | attributes;

    *s = '\0';
    return res;
}


/*ARGSUSED*/
void
dolist(Char **v, struct command *c)
{
    Char **globbed;
    int     i, k, ret = 0;
    struct stat st;

    USE(c);
    if (*++v == NULL) {
	struct Strbuf word = Strbuf_INIT;

	Strbuf_terminate(&word);
	cleanup_push(&word, Strbuf_cleanup);
	(void) t_search(&word, LIST, TW_ZERO, 0, STRNULL, 0);
	cleanup_until(&word);
	return;
    }
    v = glob_all_or_error(v);
    globbed = v;
    cleanup_push(globbed, blk_cleanup);
    for (k = 0; v[k] != NULL && v[k][0] != '-'; k++)
	continue;
    if (v[k]) {
	/*
	 * We cannot process a flag therefore we let ls do it right.
	 */
	Char *lspath;
	struct command *t;
	struct wordent cmd, *nextword, *lastword;
	Char   *cp;
	struct varent *vp;

	if (setintr) {
	    pintr_disabled++;
	    cleanup_push(&pintr_disabled, disabled_cleanup);
	}
	if (seterr) {
	    xfree(seterr);
	    seterr = NULL;
	}

	lspath = STRls;
	STRmCF[1] = 'C';
	STRmCF[3] = '\0';
	/* Look at listflags, to add -A to the flags, to get a path
	   of ls if necessary */
	if ((vp = adrof(STRlistflags)) != NULL && vp->vec != NULL &&
	    vp->vec[0] != STRNULL) {
	    if (vp->vec[1] != NULL && vp->vec[1][0] != '\0')
		lspath = vp->vec[1];
	    for (cp = vp->vec[0]; *cp; cp++)
		switch (*cp) {
		case 'x':
		    STRmCF[1] = 'x';
		    break;
		case 'a':
		    STRmCF[3] = 'a';
		    break;
		case 'A':
		    STRmCF[3] = 'A';
		    break;
		default:
		    break;
		}
	}

	cmd.word = STRNULL;
	lastword = &cmd;
	nextword = xcalloc(1, sizeof cmd);
	nextword->word = Strsave(lspath);
	lastword->next = nextword;
	nextword->prev = lastword;
	lastword = nextword;
	nextword = xcalloc(1, sizeof cmd);
	nextword->word = Strsave(STRmCF);
	lastword->next = nextword;
	nextword->prev = lastword;
#if defined(KANJI) && defined(SHORT_STRINGS) && defined(DSPMBYTE)
	if (dspmbyte_ls) {
	    lastword = nextword;
	    nextword = xcalloc(1, sizeof cmd);
	    nextword->word = Strsave(STRmmliteral);
	    lastword->next = nextword;
	    nextword->prev = lastword;
	}
#endif
#ifdef COLOR_LS_F
	if (color_context_ls) {
	    lastword = nextword;
	    nextword = xcalloc(1, sizeof cmd);
	    nextword->word = Strsave(STRmmcolormauto);
	    lastword->next = nextword;
	    nextword->prev = lastword;
	}
#endif /* COLOR_LS_F */
	lastword = nextword;
	for (cp = *v; cp; cp = *++v) {
	    nextword = xcalloc(1, sizeof cmd);
	    nextword->word = quote(Strsave(cp));
	    lastword->next = nextword;
	    nextword->prev = lastword;
	    lastword = nextword;
	}
	lastword->next = &cmd;
	cmd.prev = lastword;
	cleanup_push(&cmd, lex_cleanup);

	/* build a syntax tree for the command. */
	t = syntax(cmd.next, &cmd, 0);
	cleanup_push(t, syntax_cleanup);
	if (seterr)
	    stderror(ERR_OLD);
	/* expand aliases like process() does */
	/* alias(&cmd); */
	/* execute the parse tree. */
	execute(t, tpgrp > 0 ? tpgrp : -1, NULL, NULL, FALSE);
	/* done. free the lex list and parse tree. */
	cleanup_until(&cmd);
	if (setintr)
	    cleanup_until(&pintr_disabled);
    }
    else {
	Char   *dp, *tmp;
	struct Strbuf buf = Strbuf_INIT;

	cleanup_push(&buf, Strbuf_cleanup);
	for (k = 0, i = 0; v[k] != NULL; k++) {
	    tmp = dnormalize(v[k], symlinks == SYM_IGNORE);
	    cleanup_push(tmp, xfree);
	    dp = Strend(tmp) - 1;
	    if (*dp == '/' && dp != tmp)
#ifdef apollo
		if (dp != &tmp[1])
#endif /* apollo */
		*dp = '\0';
	    if (stat(short2str(tmp), &st) == -1) {
		int err;

		err = errno;
		if (k != i) {
		    if (i != 0)
			xputchar('\n');
		    print_by_column(STRNULL, &v[i], k - i, FALSE);
		}
		haderr = 1;
		xprintf("%S: %s.\n", tmp, strerror(err));
		haderr = 0;
		i = k + 1;
		ret = 1;
	    }
	    else if (S_ISDIR(st.st_mode)) {
		Char   *cp;

		if (k != i) {
		    if (i != 0)
			xputchar('\n');
		    print_by_column(STRNULL, &v[i], k - i, FALSE);
		}
		if (k != 0 && v[1] != NULL)
		    xputchar('\n');
		xprintf("%S:\n", tmp);
		buf.len = 0;
		for (cp = tmp; *cp; cp++)
		    Strbuf_append1(&buf, (*cp | QUOTE));
		Strbuf_terminate(&buf);
		dp = &buf.s[buf.len - 1];
		if (
#ifdef WINNT_NATIVE
		    (*dp != (Char) (':' | QUOTE)) &&
#endif /* WINNT_NATIVE */
		    (*dp != (Char) ('/' | QUOTE))) {
		    Strbuf_append1(&buf, '/');
		    Strbuf_terminate(&buf);
		} else 
		    *dp &= TRIM;
		(void) t_search(&buf, LIST, TW_ZERO, 0, STRNULL, 0);
		i = k + 1;
	    }
	    cleanup_until(tmp);
	}
	cleanup_until(&buf);
	if (k != i) {
	    if (i != 0)
		xputchar('\n');
	    print_by_column(STRNULL, &v[i], k - i, FALSE);
	}
	if (ret)
	    stderror(ERR_SILENT);
    }

    cleanup_until(globbed);
}

extern int GotTermCaps;

/*ARGSUSED*/
void
dotelltc(Char **v, struct command *c)
{
    USE(v);
    USE(c);
    if (!GotTermCaps)
	GetTermCaps();
    TellTC();
}

/*ARGSUSED*/
void
doechotc(Char **v, struct command *c)
{
    USE(c);
    if (!GotTermCaps)
	GetTermCaps();
    EchoTC(++v);
}

/*ARGSUSED*/
void
dosettc(Char **v, struct command *c)
{
    char    *tv[2];

    USE(c);
    if (!GotTermCaps)
	GetTermCaps();

    tv[0] = strsave(short2str(v[1]));
    cleanup_push(tv[0], xfree);
    tv[1] = strsave(short2str(v[2]));
    cleanup_push(tv[1], xfree);
    SetTC(tv[0], tv[1]);
    cleanup_until(tv[0]);
}

/* The dowhich() is by:
 *  Andreas Luik <luik@isaak.isa.de>
 *  I S A  GmbH - Informationssysteme fuer computerintegrierte Automatisierung
 *  Azenberstr. 35
 *  D-7000 Stuttgart 1
 *  West-Germany
 * Thanks!!
 */
int
cmd_expand(Char *cmd, Char **str)
{
    struct wordent lexp[3];
    struct varent *vp;
    int rv = TRUE;

    lexp[0].next = &lexp[1];
    lexp[1].next = &lexp[2];
    lexp[2].next = &lexp[0];

    lexp[0].prev = &lexp[2];
    lexp[1].prev = &lexp[0];
    lexp[2].prev = &lexp[1];

    lexp[0].word = STRNULL;
    lexp[2].word = STRret;

    if ((vp = adrof1(cmd, &aliases)) != NULL && vp->vec != NULL) {
	if (str == NULL) {
	    xprintf(CGETS(22, 1, "%S: \t aliased to "), cmd);
	    blkpr(vp->vec);
	    xputchar('\n');
	}
	else
	    *str = blkexpand(vp->vec);
    }
    else {
	lexp[1].word = cmd;
	rv = tellmewhat(lexp, str);
    }
    return rv;
}


/*ARGSUSED*/
void
dowhich(Char **v, struct command *c)
{
    int rv = TRUE;
    USE(c);

    /*
     * We don't want to glob dowhich args because we lose quoteing
     * E.g. which \ls if ls is aliased will not work correctly if
     * we glob here.
     */

    while (*++v) 
	rv &= cmd_expand(*v, NULL);

    if (!rv)
	setcopy(STRstatus, STR1, VAR_READWRITE);
}

static int
findvv(Char **vv, const char *cp)
{
    for (; vv && *vv; vv++) {
	size_t i;
	for (i = 0; (*vv)[i] && (*vv)[i] == cp[i]; i++)
	    continue;
	if ((*vv)[i] == '\0' && cp[i] == '\0')
	    return 1;
    }
    return 0;
}

/* PWP: a hack to start up your stopped editor on a single keystroke */
/* jbs - fixed hack so it worked :-) 3/28/89 */

struct process *
find_stop_ed(void)
{
    struct process *pp, *retp;
    const char *ep = NULL, *vp = NULL;
    char *cp, *p;
    size_t epl = 0, vpl = 0;
    int pstatus;
    struct varent *varp;
    Char **vv;

    if (pcurrent == NULL)	/* see if we have any jobs */
	return NULL;		/* nope */

    if ((varp = adrof(STReditors)) != NULL)
	vv = varp->vec;
    else
	vv = NULL;

    if (! vv) {
	if ((ep = getenv("EDITOR")) != NULL) {	/* if we have a value */
	    if ((p = strrchr(ep, '/')) != NULL) 	/* if it has a path */
		ep = p + 1;		/* then we want only the last part */
	}
	else
	    ep = "ed";

	if ((vp = getenv("VISUAL")) != NULL) {	/* if we have a value */
	    if ((p = strrchr(vp, '/')) != NULL) 	/* and it has a path */
		vp = p + 1;		/* then we want only the last part */
	}
	else
	    vp = "vi";

	for (vpl = 0; vp[vpl] && !isspace((unsigned char)vp[vpl]); vpl++)
	    continue;
	for (epl = 0; ep[epl] && !isspace((unsigned char)ep[epl]); epl++)
	    continue;
    }

    retp = NULL;
    for (pp = proclist.p_next; pp; pp = pp->p_next)
	if (pp->p_procid == pp->p_jobid) {

	    /*
	     * Only foreground an edit session if it is suspended.  Some GUI
	     * editors have may be happily running in a separate window, no
	     * point in foregrounding these if they're already running - webb
	     */
	    pstatus = (int) (pp->p_flags & PALLSTATES);
	    if (pstatus != PINTERRUPTED && pstatus != PSTOPPED &&
		pstatus != PSIGNALED)
		continue;

	    p = short2str(pp->p_command);
	    /* get the first word */
	    for (cp = p; *cp && !isspace((unsigned char) *cp); cp++)
		continue;
	    *cp = '\0';
		
	    if ((cp = strrchr(p, '/')) != NULL)	/* and it has a path */
		cp = cp + 1;		/* then we want only the last part */
	    else
		cp = p;			/* else we get all of it */

	    /*
	     * If we find the current name in the $editors array (if set)
	     * or as $EDITOR or $VISUAL (if $editors not set), fg it.
	     */
	    if ((vv && findvv(vv, cp)) ||
	        (epl && strncmp(ep, cp, epl) == 0 && cp[epl] == '\0') ||
		(vpl && strncmp(vp, cp, vpl) == 0 && cp[vpl] == '\0')) {
		/*
		 * If there is a choice, then choose the current process if
		 * available, or the previous process otherwise, or else
		 * anything will do - Robert Webb (robertw@mulga.cs.mu.oz.au).
		 */
		if (pp == pcurrent)
		    return pp;
		else if (retp == NULL || pp == pprevious)
		    retp = pp;
	    }
	}

    return retp;		/* Will be NULL if we didn't find a job */
}

void
fg_proc_entry(struct process *pp)
{
    jmp_buf_t osetexit;
    int    ohaderr;
    Char    oGettingInput;
    size_t omark;

    getexit(osetexit);

    pintr_disabled++;
    oGettingInput = GettingInput;
    GettingInput = 0;

    ohaderr = haderr;		/* we need to ignore setting of haderr due to
				 * process getting stopped by a signal */
    omark = cleanup_push_mark();
    if (setexit() == 0) {	/* come back here after pjwait */
	pendjob();
	(void) alarm(0);	/* No autologout */
	alrmcatch_disabled = 1;
	if (!pstart(pp, 1)) {
	    pp->p_procid = 0;
	    stderror(ERR_BADJOB, pp->p_command, strerror(errno));
	}
	pjwait(pp);
    }
    setalarm(1);		/* Autologout back on */
    cleanup_pop_mark(omark);
    resexit(osetexit);
    haderr = ohaderr;
    GettingInput = oGettingInput;

    disabled_cleanup(&pintr_disabled);
}

static char *
xgetpass(const char *prm)
{
    static struct strbuf pass; /* = strbuf_INIT; */
    int fd;
    sigset_t oset, set;
    struct sigaction sa, osa;

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGINT, &sa, &osa);

    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    (void)sigprocmask(SIG_UNBLOCK, &set, &oset);

    cleanup_push(&osa, sigint_cleanup);
    cleanup_push(&oset, sigprocmask_cleanup);
    (void) Rawmode();	/* Make sure, cause we want echo off */
    fd = xopen("/dev/tty", O_RDWR|O_LARGEFILE);
    if (fd == -1)
	fd = SHIN;
    else
	cleanup_push(&fd, open_cleanup);

    xprintf("%s", prm); flush();
    pass.len = 0;
    for (;;)  {
	char c;

	if (xread(fd, &c, 1) < 1 || c == '\n') 
	    break;
	strbuf_append1(&pass, c);
    }
    strbuf_terminate(&pass);

    cleanup_until(&osa);

    return pass.s;
}

#ifndef NO_CRYPT
#if !HAVE_DECL_CRYPT
    extern char *crypt ();
#endif
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif
#endif

/*
 * Ask the user for his login password to continue working
 * On systems that have a shadow password, this will only 
 * work for root, but what can we do?
 *
 * If we fail to get the password, then we log the user out
 * immediately
 */
/*ARGSUSED*/
static void
auto_lock(void)
{
#ifndef NO_CRYPT

    int i;
    char *srpp = NULL;
    struct passwd *pw;

#undef XCRYPT

#if defined(HAVE_AUTH_H) && defined(HAVE_GETAUTHUID)

    struct authorization *apw;
    extern char *crypt16 (const char *, const char *);

# define XCRYPT(pw, a, b) crypt16(a, b)

    if ((pw = xgetpwuid(euid)) != NULL &&	/* effective user passwd  */
        (apw = getauthuid(euid)) != NULL) 	/* enhanced ultrix passwd */
	srpp = apw->a_password;

#elif defined(HAVE_SHADOW_H)

    struct spwd *spw;

# define XCRYPT(pw, a, b) crypt(a, b)

    if ((pw = xgetpwuid(euid)) != NULL)	{	/* effective user passwd  */
	errno = 0;
	while ((spw = getspnam(pw->pw_name)) == NULL && errno == EINTR) {
	    handle_pending_signals();
	    errno = 0;
	}
	if (spw != NULL)			 /* shadowed passwd	  */
	    srpp = spw->sp_pwdp;
    }

#else


#ifdef __CYGWIN__
# define XCRYPT(pw, a, b) cygwin_xcrypt(pw, a, b)
#else
# define XCRYPT(pw, a, b) crypt(a, b)
#endif

#if !defined(__MVS__)
    if ((pw = xgetpwuid(euid)) != NULL)	/* effective user passwd  */
	srpp = pw->pw_passwd;
#endif /* !MVS */

#endif

    if (srpp == NULL) {
	auto_logout();
	/*NOTREACHED*/
	return;
    }

    setalarm(0);		/* Not for locking any more */
    xputchar('\n');
    for (i = 0; i < 5; i++) {
	const char *crpp;
	char *pp;
#ifdef AFS
	char *afsname;
	Char *safs;

	if ((safs = varval(STRafsuser)) != STRNULL)
	    afsname = short2str(safs);
	else
	    if ((afsname = getenv("AFSUSER")) == NULL)
	        afsname = pw->pw_name;
#endif
	pp = xgetpass("Password:");

	crpp = XCRYPT(pw, pp, srpp);
	if ((crpp && strcmp(crpp, srpp) == 0)
#ifdef AFS
	    || (ka_UserAuthenticateGeneral(KA_USERAUTH_VERSION,
					   afsname,     /* name */
					   NULL,        /* instance */
					   NULL,        /* realm */
					   pp,          /* password */
					   0,           /* lifetime */
					   0, 0,         /* spare */
					   NULL)        /* reason */
	    == 0)
#endif /* AFS */
	    ) {
	    (void) memset(pp, 0, strlen(pp));
	    if (GettingInput && !just_signaled) {
		(void) Rawmode();
		ClearLines();
		ClearDisp();
		Refresh();
	    }
	    just_signaled = 0;
	    return;
	}
	xprintf(CGETS(22, 2, "\nIncorrect passwd for %s\n"), pw->pw_name);
    }
#endif /* NO_CRYPT */
    auto_logout();
}


static void
auto_logout(void)
{
    xprintf("auto-logout\n");
    /* Don't leave the tty in raw mode */
    if (editing)
	(void) Cookedmode();
    xclose(SHIN);
    setcopy(STRlogout, STRautomatic, VAR_READWRITE);
    child = 1;
#ifdef TESLA
    do_logout = 1;
#endif /* TESLA */
    GettingInput = FALSE; /* make flush() work to write hist files. Huber*/
    goodbye(NULL, NULL);
}

void
alrmcatch(void)
{
    (*alm_fun)();
    setalarm(1);
}

/*
 * Karl Kleinpaste, 21oct1983.
 * Added precmd(), which checks for the alias
 * precmd in aliases.  If it's there, the alias
 * is executed as a command.  This is done
 * after mailchk() and just before print-
 * ing the prompt.  Useful for things like printing
 * one's current directory just before each command.
 */
void
precmd(void)
{
    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);
    if (precmd_active) {	/* an error must have been caught */
	aliasrun(2, STRunalias, STRprecmd);
	xprintf("%s", CGETS(22, 3, "Faulty alias 'precmd' removed.\n"));
	goto leave;
    }
    precmd_active = 1;
    if (!whyles && adrof1(STRprecmd, &aliases))
	aliasrun(1, STRprecmd, NULL);
leave:
    precmd_active = 0;
    cleanup_until(&pintr_disabled);
}

void
postcmd(void)
{
    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);
    if (postcmd_active) {	/* an error must have been caught */
	aliasrun(2, STRunalias, STRpostcmd);
	xprintf("%s", CGETS(22, 3, "Faulty alias 'postcmd' removed.\n"));
	goto leave;
    }
    postcmd_active = 1;
    if (!whyles && adrof1(STRpostcmd, &aliases))
	aliasrun(1, STRpostcmd, NULL);
leave:
    postcmd_active = 0;
    cleanup_until(&pintr_disabled);
}

/*
 * Paul Placeway  11/24/87  Added cwd_cmd by hacking precmd() into
 * submission...  Run every time $cwd is set (after it is set).  Useful
 * for putting your machine and cwd (or anything else) in an xterm title
 * space.
 */
void
cwd_cmd(void)
{
    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);
    if (cwdcmd_active) {	/* an error must have been caught */
	aliasrun(2, STRunalias, STRcwdcmd);
	xprintf("%s", CGETS(22, 4, "Faulty alias 'cwdcmd' removed.\n"));
	goto leave;
    }
    cwdcmd_active = 1;
    if (!whyles && adrof1(STRcwdcmd, &aliases))
	aliasrun(1, STRcwdcmd, NULL);
leave:
    cwdcmd_active = 0;
    cleanup_until(&pintr_disabled);
}

/*
 * Joachim Hoenig  07/16/91  Added beep_cmd, run every time tcsh wishes 
 * to beep the terminal bell. Useful for playing nice sounds instead.
 */
void
beep_cmd(void)
{
    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);
    if (beepcmd_active) {	/* an error must have been caught */
	aliasrun(2, STRunalias, STRbeepcmd);
	xprintf("%s", CGETS(22, 5, "Faulty alias 'beepcmd' removed.\n"));
    }
    else {
	beepcmd_active = 1;
	if (!whyles && adrof1(STRbeepcmd, &aliases))
	    aliasrun(1, STRbeepcmd, NULL);
    }
    beepcmd_active = 0;
    cleanup_until(&pintr_disabled);
}


/*
 * Karl Kleinpaste, 18 Jan 1984.
 * Added period_cmd(), which executes the alias "periodic" every
 * $tperiod minutes.  Useful for occasional checking of msgs and such.
 */
void
period_cmd(void)
{
    Char *vp;
    time_t  t, interval;

    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);
    if (periodic_active) {	/* an error must have been caught */
	aliasrun(2, STRunalias, STRperiodic);
	xprintf("%s", CGETS(22, 6, "Faulty alias 'periodic' removed.\n"));
	goto leave;
    }
    periodic_active = 1;
    if (!whyles && adrof1(STRperiodic, &aliases)) {
	vp = varval(STRtperiod);
	if (vp == STRNULL) {
	    aliasrun(1, STRperiodic, NULL);
	    goto leave;
	}
	interval = getn(vp);
	(void) time(&t);
	if (t - t_period >= interval * 60) {
	    t_period = t;
	    aliasrun(1, STRperiodic, NULL);
	}
    }
leave:
    periodic_active = 0;
    cleanup_until(&pintr_disabled);
}


/* 
 * GrP Greg Parker May 2001
 * Added job_cmd(), which is run every time a job is started or 
 * foregrounded. The command is passed a single argument, the string 
 * used to start the job originally. With precmd, useful for setting 
 * xterm titles.
 * Cloned from cwd_cmd().
 */
void
job_cmd(Char *args)
{
    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);
    if (jobcmd_active) {	/* an error must have been caught */
	aliasrun(2, STRunalias, STRjobcmd);
	xprintf("%s", CGETS(22, 14, "Faulty alias 'jobcmd' removed.\n"));
	goto leave;
    }
    jobcmd_active = 1;
    if (!whyles && adrof1(STRjobcmd, &aliases)) {
	struct process *pp = pcurrjob; /* put things back after the hook */
	aliasrun(2, STRjobcmd, args);
	pcurrjob = pp;
    }
leave:
    jobcmd_active = 0;
    cleanup_until(&pintr_disabled);
}


/*
 * Karl Kleinpaste, 21oct1983.
 * Set up a one-word alias command, for use for special things.
 * This code is based on the mainline of process().
 */
void
aliasrun(int cnt, Char *s1, Char *s2)
{
    struct wordent w, *new1, *new2;	/* for holding alias name */
    struct command *t = NULL;
    jmp_buf_t osetexit;
    int status;
    size_t omark;

    getexit(osetexit);
    if (seterr) {
	xfree(seterr);
	seterr = NULL;	/* don't repeatedly print err msg. */
    }
    w.word = STRNULL;
    new1 = xcalloc(1, sizeof w);
    new1->word = Strsave(s1);
    if (cnt == 1) {
	/* build a lex list with one word. */
	w.next = w.prev = new1;
	new1->next = new1->prev = &w;
    }
    else {
	/* build a lex list with two words. */
	new2 = xcalloc(1, sizeof w);
	new2->word = Strsave(s2);
	w.next = new2->prev = new1;
	new1->next = w.prev = new2;
	new1->prev = new2->next = &w;
    }
    cleanup_push(&w, lex_cleanup);

    /* Save the old status */
    status = getn(varval(STRstatus));

    /* expand aliases like process() does. */
    alias(&w);
    /* build a syntax tree for the command. */
    t = syntax(w.next, &w, 0);
    cleanup_push(t, syntax_cleanup);
    if (seterr)
	stderror(ERR_OLD);

    psavejob();
    cleanup_push(&cnt, psavejob_cleanup); /* cnt is used only as a marker */

    /* catch any errors here */
    omark = cleanup_push_mark();
    if (setexit() == 0)
	/* execute the parse tree. */
	/*
	 * From: Michael Schroeder <mlschroe@immd4.informatik.uni-erlangen.de>
	 * was execute(t, tpgrp);
	 */
	execute(t, tpgrp > 0 ? tpgrp : -1, NULL, NULL, TRUE);
    /* reset the error catcher to the old place */
    cleanup_pop_mark(omark);
    resexit(osetexit);
    if (haderr) {
	haderr = 0;
	/*
	 * Either precmd, or cwdcmd, or periodic had an error. Call it again so
	 * that it is removed
	 */
	if (precmd_active)
	    precmd();
	if (postcmd_active)
	    postcmd();
#ifdef notdef
	/*
	 * XXX: On the other hand, just interrupting them causes an error too.
	 * So if we hit ^C in the middle of cwdcmd or periodic the alias gets
	 * removed. We don't want that. Note that we want to remove precmd
	 * though, cause that could lead into an infinite loop. This should be
	 * fixed correctly, but then haderr should give us the whole exit
	 * status not just true or false.
	 */
	else if (cwdcmd_active)
	    cwd_cmd();
	else if (beepcmd_active)
	    beep_cmd();
	else if (periodic_active)
	    period_cmd();
#endif /* notdef */
    }
    cleanup_until(&w);
    pendjob();
    /* Restore status */
    setv(STRstatus, putn((tcsh_number_t)status), VAR_READWRITE);
}

void
setalarm(int lck)
{
    struct varent *vp;
    Char   *cp;
    unsigned alrm_time = 0, logout_time, lock_time;
    time_t cl, nl, sched_dif;

    if ((vp = adrof(STRautologout)) != NULL && vp->vec != NULL) {
	if ((cp = vp->vec[0]) != 0) {
	    if ((logout_time = (unsigned) atoi(short2str(cp)) * 60) > 0) {
#ifdef SOLARIS2
		/*
		 * Solaris alarm(2) uses a timer based in clock ticks
		 * internally so it multiplies our value with CLK_TCK...
		 * Of course that can overflow leading to unexpected
		 * results, so we clip it here. Grr. Where is that
		 * documented folks?
		 */
		if (logout_time >= 0x7fffffff / CLK_TCK)
			logout_time = 0x7fffffff / CLK_TCK;
#endif /* SOLARIS2 */
		alrm_time = logout_time;
		alm_fun = auto_logout;
	    }
	}
	if ((cp = vp->vec[1]) != 0) {
	    if ((lock_time = (unsigned) atoi(short2str(cp)) * 60) > 0) {
		if (lck) {
		    if (alrm_time == 0 || lock_time < alrm_time) {
			alrm_time = lock_time;
			alm_fun = auto_lock;
		    }
		}
		else /* lock_time always < alrm_time */
		    if (alrm_time)
			alrm_time -= lock_time;
	    }
	}
    }
    if ((nl = sched_next()) != -1) {
	(void) time(&cl);
	sched_dif = nl > cl ? nl - cl : 0;
	if ((alrm_time == 0) || ((unsigned) sched_dif < alrm_time)) {
	    alrm_time = ((unsigned) sched_dif) + 1;
	    alm_fun = sched_run;
	}
    }
    alrmcatch_disabled = 0;
    (void) alarm(alrm_time);	/* Autologout ON */
}

#undef RMDEBUG			/* For now... */

void
rmstar(struct wordent *cp)
{
    struct wordent *we, *args;
    struct wordent *tmp, *del;

#ifdef RMDEBUG
    static Char STRrmdebug[] = {'r', 'm', 'd', 'e', 'b', 'u', 'g', '\0'};
    Char   *tag;
#endif /* RMDEBUG */
    Char   *charac;
    int     ask, doit, star = 0, silent = 0, opintr_disabled;

    if (!adrof(STRrmstar))
	return;
#ifdef RMDEBUG
    tag = varval(STRrmdebug);
#endif /* RMDEBUG */
    we = cp->next;
    while (*we->word == ';' && we != cp)
	we = we->next;
    opintr_disabled = pintr_disabled;
    pintr_disabled = 0;
    while (we != cp) {
#ifdef RMDEBUG
	if (*tag)
	    xprintf(CGETS(22, 7, "parsing command line\n"));
#endif /* RMDEBUG */
	if (!Strcmp(we->word, STRrm)) {
	    args = we->next;
	    ask = (*args->word != '-');
	    while (*args->word == '-' && !silent) {	/* check options */
		for (charac = (args->word + 1); *charac && !silent; charac++)
		    silent = (*charac == 'i' || *charac == 'f');
		args = args->next;
	    }
	    ask = (ask || (!ask && !silent));
	    if (ask) {
		for (; !star && *args->word != ';'
		     && args != cp; args = args->next)
		    if (!Strcmp(args->word, STRstar))
			star = 1;
		if (ask && star) {
		    doit = getYN(CGETS(22, 8,
			"Do you really want to delete all files? [N/y] "));
		    if (!doit) {
			/* remove the command instead */
#ifdef RMDEBUG
			if (*tag)
			    xprintf(CGETS(22, 9,
				    "skipping deletion of files!\n"));
#endif /* RMDEBUG */
			for (tmp = we;
			     *tmp->word != '\n' &&
			     *tmp->word != ';' && tmp != cp;) {
			    tmp->prev->next = tmp->next;
			    tmp->next->prev = tmp->prev;
			    xfree(tmp->word);
			    del = tmp;
			    tmp = tmp->next;
			    xfree(del);
			}
			if (*tmp->word == ';') {
			    tmp->prev->next = tmp->next;
			    tmp->next->prev = tmp->prev;
			    xfree(tmp->word);
			    del = tmp;
			    tmp = tmp->next;
			    xfree(del);
			}
			we = tmp;
			continue;
		    }
		}
	    }
	}
	for (we = we->next;
	     *we->word != ';' && we != cp;
	     we = we->next)
	    continue;
	if (*we->word == ';')
	    we = we->next;
    }
#ifdef RMDEBUG
    if (*tag) {
	xprintf(CGETS(22, 10, "command line now is:\n"));
	for (we = cp->next; we != cp; we = we->next)
	    xprintf("%S ", we->word);
    }
#endif /* RMDEBUG */
    pintr_disabled = opintr_disabled;
    return;
}

#ifdef BSDJOBS
/* Check if command is in continue list
   and do a "aliasing" if it exists as a job in background */

#undef CNDEBUG			/* For now */
void
continue_jobs(struct wordent *cp)
{
    struct wordent *we;
    struct process *pp, *np;
    Char   *cmd, *continue_list, *continue_args_list;

#ifdef CNDEBUG
    Char   *tag;
    static Char STRcndebug[] =
    {'c', 'n', 'd', 'e', 'b', 'u', 'g', '\0'};
#endif /* CNDEBUG */
    int    in_cont_list, in_cont_arg_list;


#ifdef CNDEBUG
    tag = varval(STRcndebug);
#endif /* CNDEBUG */
    continue_list = varval(STRcontinue);
    continue_args_list = varval(STRcontinue_args);
    if (*continue_list == '\0' && *continue_args_list == '\0')
	return;

    we = cp->next;
    while (*we->word == ';' && we != cp)
	we = we->next;
    while (we != cp) {
#ifdef CNDEBUG
	if (*tag)
	    xprintf(CGETS(22, 11, "parsing command line\n"));
#endif /* CNDEBUG */
	cmd = we->word;
	in_cont_list = inlist(continue_list, cmd);
	in_cont_arg_list = inlist(continue_args_list, cmd);
	if (in_cont_list || in_cont_arg_list) {
#ifdef CNDEBUG
	    if (*tag)
		xprintf(CGETS(22, 12, "in one of the lists\n"));
#endif /* CNDEBUG */
	    np = NULL;
	    for (pp = proclist.p_next; pp; pp = pp->p_next) {
		if (prefix(cmd, pp->p_command)) {
		    if (pp->p_index) {
			np = pp;
			break;
		    }
		}
	    }
	    if (np) {
		insert(we, in_cont_arg_list);
	    }
	}
	for (we = we->next;
	     *we->word != ';' && we != cp;
	     we = we->next)
	    continue;
	if (*we->word == ';')
	    we = we->next;
    }
#ifdef CNDEBUG
    if (*tag) {
	xprintf(CGETS(22, 13, "command line now is:\n"));
	for (we = cp->next; we != cp; we = we->next)
	    xprintf("%S ", we->word);
    }
#endif /* CNDEBUG */
    return;
}

/* The actual "aliasing" of for backgrounds() is done here
   with the aid of insert_we().   */
static void
insert(struct wordent *pl, int file_args)
{
    struct wordent *now, *last;
    Char   *cmd, *bcmd, *cp1, *cp2;
    size_t cmd_len;
    Char   *upause = STRunderpause;
    size_t p_len = Strlen(upause);

    cmd_len = Strlen(pl->word);
    cmd = xcalloc(1, (cmd_len + 1) * sizeof(Char));
    (void) Strcpy(cmd, pl->word);
/* Do insertions at beginning, first replace command word */

    if (file_args) {
	now = pl;
	xfree(now->word);
	now->word = xcalloc(1, 5 * sizeof(Char));
	(void) Strcpy(now->word, STRecho);

	now = xcalloc(1, sizeof(struct wordent));
	now->word = xcalloc(1, 6 * sizeof(Char));
	(void) Strcpy(now->word, STRbackqpwd);
	insert_we(now, pl);

	for (last = now; *last->word != '\n' && *last->word != ';';
	     last = last->next)
	    continue;

	now = xcalloc(1, sizeof(struct wordent));
	now->word = xcalloc(1, 2 * sizeof(Char));
	(void) Strcpy(now->word, STRgt);
	insert_we(now, last->prev);

	now = xcalloc(1, sizeof(struct wordent));
	now->word = xcalloc(1, 2 * sizeof(Char));
	(void) Strcpy(now->word, STRbang);
	insert_we(now, last->prev);

	now = xcalloc(1, sizeof(struct wordent));
	now->word = xcalloc(1, (cmd_len + p_len + 4) * sizeof(Char));
	cp1 = now->word;
	cp2 = cmd;
	*cp1++ = '~';
	*cp1++ = '/';
	*cp1++ = '.';
	while ((*cp1++ = *cp2++) != '\0')
	    continue;
	cp1--;
	cp2 = upause;
	while ((*cp1++ = *cp2++) != '\0')
	    continue;
	insert_we(now, last->prev);

	now = xcalloc(1, sizeof(struct wordent));
	now->word = xcalloc(1, 2 * sizeof(Char));
	(void) Strcpy(now->word, STRsemi);
	insert_we(now, last->prev);
	bcmd = xcalloc(1, (cmd_len + 2) * sizeof(Char));
	*bcmd = '%';
	Strcpy(bcmd + 1, cmd);
	now = xcalloc(1, sizeof(struct wordent));
	now->word = bcmd;
	insert_we(now, last->prev);
    }
    else {
	struct wordent *del;

	now = pl;
	xfree(now->word);
	now->word = xcalloc(1, (cmd_len + 2) * sizeof(Char));
	*now->word = '%';
	Strcpy(now->word + 1, cmd);
	for (now = now->next;
	     *now->word != '\n' && *now->word != ';' && now != pl;) {
	    now->prev->next = now->next;
	    now->next->prev = now->prev;
	    xfree(now->word);
	    del = now;
	    now = now->next;
	    xfree(del);
	}
    }
}

static void
insert_we(struct wordent *new, struct wordent *where)
{

    new->prev = where;
    new->next = where->next;
    where->next = new;
    new->next->prev = new;
}

static int
inlist(Char *list, Char *name)
{
    Char *l, *n;

    l = list;
    n = name;

    while (*l && *n) {
	if (*l == *n) {
	    l++;
	    n++;
	    if (*n == '\0' && (*l == ' ' || *l == '\0'))
		return (1);
	    else
		continue;
	}
	else {
	    while (*l && *l != ' ')
		l++;		/* skip to blank */
	    while (*l && *l == ' ')
		l++;		/* and find first nonblank character */
	    n = name;
	}
    }
    return (0);
}

#endif /* BSDJOBS */


/*
 * Implement a small cache for tilde names. This is used primarily
 * to expand tilde names to directories, but also
 * we can find users from their home directories for the tilde
 * prompt, on machines where yp lookup is slow this can be a big win...
 * As with any cache this can run out of sync, rehash can sync it again.
 */
static struct tildecache {
    Char   *user;
    Char   *home;
    size_t  hlen;
}      *tcache = NULL;

#define TILINCR 10
size_t tlength = 0;
static size_t tsize = TILINCR;

static int
tildecompare(const void *xp1, const void *xp2)
{
    const struct tildecache *p1, *p2;

    p1 = xp1;
    p2 = xp2;
    return Strcmp(p1->user, p2->user);
}

static Char *
gethomedir(const Char *us)
{
    struct passwd *pp;
#ifdef HESIOD
    char **res, **res1, *cp;
    Char *rp;
#endif /* HESIOD */
    
    pp = xgetpwnam(short2str(us));
#ifdef YPBUGS
    fix_yp_bugs();
#endif /* YPBUGS */
    if (pp != NULL) {
#if 0
	/* Don't return if root */
	if (pp->pw_dir[0] == '/' && pp->pw_dir[1] == '\0')
	    return NULL;
	else
#endif
	    return Strsave(str2short(pp->pw_dir));
    }
#ifdef HESIOD
    res = hes_resolve(short2str(us), "filsys");
    rp = NULL;
    if (res != NULL) {
	if ((*res) != NULL) {
	    /*
	     * Look at the first token to determine how to interpret
	     * the rest of it.
	     * Yes, strtok is evil (it's not thread-safe), but it's also
	     * easy to use.
	     */
	    cp = strtok(*res, " ");
	    if (strcmp(cp, "AFS") == 0) {
		/* next token is AFS pathname.. */
		cp = strtok(NULL, " ");
		if (cp != NULL)
		    rp = Strsave(str2short(cp));
	    } else if (strcmp(cp, "NFS") == 0) {
		cp = NULL;
		if ((strtok(NULL, " ")) && /* skip remote pathname */
		    (strtok(NULL, " ")) && /* skip host */
		    (strtok(NULL, " ")) && /* skip mode */
		    (cp = strtok(NULL, " "))) {
		    rp = Strsave(str2short(cp));
		}
	    }
	}
	for (res1 = res; *res1; res1++)
	    free(*res1);
#if 0
	/* Don't return if root */
	if (rp != NULL && rp[0] == '/' && rp[1] == '\0') {
	    xfree(rp);
	    rp = NULL;
	}
#endif
	return rp;
    }
#endif /* HESIOD */
    return NULL;
}

Char   *
gettilde(const Char *us)
{
    struct tildecache *bp1, *bp2, *bp;
    Char *hd;

    /* Ignore NIS special names */
    if (*us == '+' || *us == '-')
	return NULL;

    if (tcache == NULL)
	tcache = xmalloc(TILINCR * sizeof(struct tildecache));
    /*
     * Binary search
     */
    for (bp1 = tcache, bp2 = tcache + tlength; bp1 < bp2;) {
	int i;

	bp = bp1 + ((bp2 - bp1) >> 1);
	if ((i = *us - *bp->user) == 0 && (i = Strcmp(us, bp->user)) == 0)
	    return (bp->home);
	if (i < 0)
	    bp2 = bp;
	else
	    bp1 = bp + 1;
    }
    /*
     * Not in the cache, try to get it from the passwd file
     */
    hd = gethomedir(us);
    if (hd == NULL)
	return NULL;

    /*
     * Update the cache
     */
    tcache[tlength].user = Strsave(us);
    tcache[tlength].home = hd;
    tcache[tlength++].hlen = Strlen(hd);

    qsort(tcache, tlength, sizeof(struct tildecache), tildecompare);

    if (tlength == tsize) {
	tsize += TILINCR;
	tcache = xrealloc(tcache, tsize * sizeof(struct tildecache));
    }
    return (hd);
}

/*
 * Return the username if the directory path passed contains a
 * user's home directory in the tilde cache, otherwise return NULL
 * hm points to the place where the path became different.
 * Special case: Our own home directory.
 * If we are passed a null pointer, then we flush the cache.
 */
Char   *
getusername(Char **hm)
{
    Char   *h, *p;
    size_t i, j;

    if (hm == NULL) {
	for (i = 0; i < tlength; i++) {
	    xfree(tcache[i].home);
	    xfree(tcache[i].user);
	}
	xfree(tcache);
	tlength = 0;
	tsize = TILINCR;
	tcache = NULL;
	return NULL;
    }
    p = *hm;
    if (((h = varval(STRhome)) != STRNULL) &&
	(Strncmp(p, h, j = Strlen(h)) == 0) &&
	(p[j] == '/' || p[j] == '\0')) {
	*hm = &p[j];
	return STRNULL;
    }
    for (i = 0; i < tlength; i++)
	if ((Strncmp(p, tcache[i].home, (j = tcache[i].hlen)) == 0) &&
	    (p[j] == '/' || p[j] == '\0')) {
	    *hm = &p[j];
	    return tcache[i].user;
	}
    return NULL;
}


/*
 * set the shell-level var to 1 or apply change to it.
 */
void
shlvl(int val)
{
    char *cp;

    if ((cp = getenv("SHLVL")) != NULL) {

	if (loginsh)
	    val = 1;
	else
	    val += atoi(cp);

	if (val <= 0) {
	    if (adrof(STRshlvl) != NULL)
		unsetv(STRshlvl);
	    Unsetenv(STRKSHLVL);
	}
	else {
	    Char *p;

	    p = Itoa(val, 0, 0);
	    cleanup_push(p, xfree);
	    setv(STRshlvl, p, VAR_READWRITE);
	    cleanup_ignore(p);
	    cleanup_until(p);
	    tsetenv(STRKSHLVL, p);
	}
    }
    else {
	setcopy(STRshlvl, STR1, VAR_READWRITE);
	tsetenv(STRKSHLVL, STR1);
    }
}


/* fixio():
 *	Try to recover from a read error
 */
int
fixio(int fd, int e)
{
    switch (e) {
    case -1:	/* Make sure that the code is reachable */

#ifdef EWOULDBLOCK
    case EWOULDBLOCK:
# define FDRETRY
#endif /* EWOULDBLOCK */

#if defined(POSIX) && defined(EAGAIN)
# if !defined(EWOULDBLOCK) || EWOULDBLOCK != EAGAIN
    case EAGAIN:
#  define FDRETRY
# endif /* !EWOULDBLOCK || EWOULDBLOCK != EAGAIN */
#endif /* POSIX && EAGAIN */

	e = -1;
#ifdef FDRETRY
# ifdef F_SETFL
/*
 * Great! we have on suns 3 flavors and 5 names...
 * I hope that will cover everything.
 * I added some more defines... many systems have different defines.
 * Rather than dealing with getting the right includes, we'll just
 * cover all the known possibilities here.  -- sterling@netcom.com
 */
#  ifndef O_NONBLOCK
#   define O_NONBLOCK 0
#  endif /* O_NONBLOCK */
#  ifndef O_NDELAY
#   define O_NDELAY 0
#  endif /* O_NDELAY */
#  ifndef FNBIO
#   define FNBIO 0
#  endif /* FNBIO */
#  ifndef _FNBIO
#   define _FNBIO 0
#  endif /* _FNBIO */
#  ifndef FNONBIO
#   define FNONBIO 0
#  endif /* FNONBIO */
#  ifndef FNONBLOCK
#   define FNONBLOCK 0
#  endif /* FNONBLOCK */
#  ifndef _FNONBLOCK
#   define _FNONBLOCK 0
#  endif /* _FNONBLOCK */
#  ifndef FNDELAY
#   define FNDELAY 0
#  endif /* FNDELAY */
#  ifndef _FNDELAY
#   define _FNDELAY 0
#  endif /* _FNDELAY */
#  ifndef FNDLEAY	/* Some linux versions have this typo */
#   define FNDLEAY 0
#  endif /* FNDLEAY */
	if ((e = fcntl(fd, F_GETFL, 0)) == -1)
	    return -1;

	e &= ~(O_NDELAY|O_NONBLOCK|FNBIO|_FNBIO|FNONBIO|FNONBLOCK|_FNONBLOCK|
	       FNDELAY|_FNDELAY|FNDLEAY);	/* whew! */
	if (fcntl(fd, F_SETFL, e) == -1)
	    return -1;
	else 
	    e = 0;
# endif /* F_SETFL */

# ifdef FIONBIO
	e = 0;
	if (ioctl(fd, FIONBIO, (ioctl_t) &e) == -1)
	    return -1;
# endif	/* FIONBIO */

#endif /* FDRETRY */
	return e;

    case EINTR:
	return 0;

    default:
	return -1;
    }
}

/* collate():
 *	String collation
 */
int
collate(const Char *a, const Char *b)
{
    int rv;
#ifdef SHORT_STRINGS
    /* This strips the quote bit as a side effect */
    char *sa = strsave(short2str(a));
    char *sb = strsave(short2str(b));
#else
    char *sa = strip(strsave(a));
    char *sb = strip(strsave(b));
#endif /* SHORT_STRINGS */

#if defined(NLS) && defined(HAVE_STRCOLL)
    errno = 0;	/* strcoll sets errno, another brain-damage */

    rv = strcoll(sa, sb);

    /*
     * We should be checking for errno != 0, but some systems
     * forget to reset errno to 0. So we only check for the 
     * only documented valid errno value for strcoll [EINVAL]
     */
    if (errno == EINVAL) {
	xfree(sa);
	xfree(sb);
	stderror(ERR_SYSTEM, "strcoll", strerror(errno));
    }
#else
    rv = strcmp(sa, sb);
#endif /* NLS && HAVE_STRCOLL */

    xfree(sa);
    xfree(sb);

    return rv;
}

#ifdef HASHBANG
/*
 * From: peter@zeus.dialix.oz.au (Peter Wemm)
 * If exec() fails look first for a #! [word] [word] ....
 * If it is, splice the header into the argument list and retry.
 */
#define HACKBUFSZ 1024		/* Max chars in #! vector */
int
hashbang(int fd, Char ***vp)
{
    struct blk_buf sarg = BLK_BUF_INIT;
    char lbuf[HACKBUFSZ], *p, *ws;
#ifdef WINNT_NATIVE
    int fw = 0; 	/* found at least one word */
    int first_word = 1;
    char *real;
#endif /* WINNT_NATIVE */

    if (xread(fd, lbuf, HACKBUFSZ) <= 0)
	return -1;

    ws = 0;	/* word started = 0 */

    for (p = lbuf; p < &lbuf[HACKBUFSZ]; ) {
	switch (*p) {
	case ' ':
	case '\t':
#if defined(WINNT_NATIVE) || defined (__CYGWIN__)
	case '\r':
#endif /* WINNT_NATIVE || __CYGWIN__ */
	    if (ws) {	/* a blank after a word.. save it */
		*p = '\0';
#ifdef WINNT_NATIVE
		if (first_word) {
		    real = hb_subst(ws);
		    if (real != NULL)
			ws = real;
		}
	    	fw = 1;
		first_word = 0;
#endif /* WINNT_NATIVE */
		bb_append(&sarg, SAVE(ws));
		ws = NULL;
	    }
	    p++;
	    continue;

	case '\0':	/* Whoa!! what the hell happened */
	    goto err;

	case '\n':	/* The end of the line. */
	    if (
#ifdef WINNT_NATIVE
		fw ||
#endif /* WINNT_NATIVE */
		ws) {	/* terminate the last word */
		*p = '\0';
#ifdef WINNT_NATIVE
		/* deal with the 1-word case */
		if (first_word) {
		    real = hb_subst(ws);
		    if (real != NULL)
			ws = real;
		}
#endif /* !WINNT_NATIVE */
		if (ws)
		    bb_append(&sarg, SAVE(ws));
	    }
	    if (sarg.len > 0) {
		*vp = bb_finish(&sarg);
		return 0;
	    }
	    else
		goto err;

	default:
	    if (!ws)	/* Start a new word? */
		ws = p; 
	    p++;
	    break;
	}
    }
 err:
    bb_cleanup(&sarg);
    return -1;
}
#endif /* HASHBANG */

#ifdef REMOTEHOST

static void
palarm(int snum)
{
    USE(snum);
    _exit(1);
}

static void
getremotehost(int dest_fd)
{
    const char *host = NULL;
#ifdef INET6
    struct sockaddr_storage saddr;
    static char hbuf[NI_MAXHOST];
#else
    struct hostent* hp;
    struct sockaddr_in saddr;
#endif
    socklen_t len = sizeof(saddr);

#ifdef INET6
    if (getpeername(SHIN, (struct sockaddr *) &saddr, &len) != -1 &&
	(saddr.ss_family == AF_INET6 || saddr.ss_family == AF_INET)) {
	int flag = NI_NUMERICHOST;

#ifdef NI_WITHSCOPEID
	flag |= NI_WITHSCOPEID;
#endif
	getnameinfo((struct sockaddr *)&saddr, len, hbuf, sizeof(hbuf),
		    NULL, 0, flag);
	host = hbuf;
#else
    if (getpeername(SHIN, (struct sockaddr *) &saddr, &len) != -1 &&
	saddr.sin_family == AF_INET) {
#if 0
	if ((hp = gethostbyaddr((char *)&saddr.sin_addr, sizeof(struct in_addr),
				AF_INET)) != NULL)
	    host = hp->h_name;
	else
#endif
	    host = inet_ntoa(saddr.sin_addr);
#endif
    }
#ifdef HAVE_STRUCT_UTMP_UT_HOST
    else {
	char *ptr;
	char *name = utmphost();
	/* Avoid empty names and local X displays */
	if (name != NULL && *name != '\0' && *name != ':') {
	    struct in_addr addr;
	    char *sptr;

	    /* Look for host:display.screen */
	    /*
	     * There is conflict with IPv6 address and X DISPLAY.  So,
	     * we assume there is no IPv6 address in utmp and don't
	     * touch here.
	     */
	    if ((sptr = strchr(name, ':')) != NULL)
		*sptr = '\0';
	    /* Leave IPv4 address as is */
	    /*
	     * we use inet_addr here, not inet_aton because many systems
	     * have not caught up yet.
	     */
	    addr.s_addr = inet_addr(name);
	    if (addr.s_addr != (unsigned int)~0)
		host = name;
	    else {
		if (sptr != name) {
#ifdef INET6
		    char *s, *domain;
		    char dbuf[MAXHOSTNAMELEN];
		    struct addrinfo hints, *res = NULL;

		    memset(&hints, 0, sizeof(hints));
		    hints.ai_family = PF_UNSPEC;
		    hints.ai_socktype = SOCK_STREAM;
		    hints.ai_flags = AI_PASSIVE | AI_CANONNAME;
		    if (strlen(name) < utmphostsize())
		    {
			if (getaddrinfo(name, NULL, &hints, &res) != 0)
			    res = NULL;
		    } else if (gethostname(dbuf, sizeof(dbuf)) == 0 &&
			       (dbuf[sizeof(dbuf)-1] = '\0', /*FIXME: ugly*/
				(domain = strchr(dbuf, '.')) != NULL)) {
			for (s = strchr(name, '.');
			     s != NULL; s = strchr(s + 1, '.')) {
			    if (*(s + 1) != '\0' &&
				(ptr = strstr(domain, s)) != NULL) {
			        char *cbuf;

				cbuf = strspl(name, ptr + strlen(s));
				if (getaddrinfo(cbuf, NULL, &hints, &res) != 0)
				    res = NULL;
				xfree(cbuf);
				break;
			    }
			}
		    }
		    if (res != NULL) {
			if (res->ai_canonname != NULL) {
			    strncpy(hbuf, res->ai_canonname, sizeof(hbuf));
			    hbuf[sizeof(hbuf) - 1] = '\0';
			    host = hbuf;
			}
			freeaddrinfo(res);
		    }
#else
		    if ((hp = gethostbyname(name)) == NULL) {
			/* Try again eliminating the trailing domain */
			if ((ptr = strchr(name, '.')) != NULL) {
			    *ptr = '\0';
			    if ((hp = gethostbyname(name)) != NULL)
				host = hp->h_name;
			    *ptr = '.';
			}
		    }
		    else
			host = hp->h_name;
#endif
		}
	    }
	}
    }
#endif

    if (host) {
	size_t left;

	left = strlen(host);
	while (left != 0) {
	    ssize_t res;

	    res = xwrite(dest_fd, host, left);
	    if (res < 0)
		_exit(1);
	    host += res;
	    left -= res;
	}
    }
    _exit(0);
}

/*
 * From: <lesv@ppvku.ericsson.se> (Lennart Svensson)
 */
void
remotehost(void)
{
    struct sigaction sa;
    struct strbuf hostname = strbuf_INIT;
    int fds[2], wait_options, status;
    pid_t pid, wait_res;

    sa.sa_handler = SIG_DFL; /* Make sure a zombie is created */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGCHLD, &sa, NULL);
    mypipe(fds);
    pid = fork();
    if (pid == 0) {
	sigset_t set;
	xclose(fds[0]);
	/* Don't get stuck if the resolver does not work! */
	signal(SIGALRM, palarm);
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	(void)sigprocmask(SIG_UNBLOCK, &set, NULL);
	(void)alarm(2);
	getremotehost(fds[1]);
	/*NOTREACHED*/
    }
    xclose(fds[1]);
    for (;;) {
	char buf[BUFSIZE];
	ssize_t res;

	res = xread(fds[0], buf, sizeof(buf));
	if (res == -1) {
	    hostname.len = 0;
	    wait_options = WNOHANG;
	    goto done;
	}
	if (res == 0)
	    break;
	strbuf_appendn(&hostname, buf, res);
    }
    wait_options = 0;
 done:
    cleanup_push(&hostname, strbuf_cleanup);
    xclose(fds[0]);
    while ((wait_res = waitpid(pid, &status, wait_options)) == -1
	   && errno == EINTR)
	handle_pending_signals();
    if (hostname.len > 0 && wait_res == pid && WIFEXITED(status)
	   && WEXITSTATUS(status) == 0) {
	strbuf_terminate(&hostname);
	tsetenv(STRREMOTEHOST, str2short(hostname.s));
    }
    cleanup_until(&hostname);

#ifdef YPBUGS
    /* From: casper@fwi.uva.nl (Casper H.S. Dik), for Solaris 2.3 */
    fix_yp_bugs();
#endif /* YPBUGS */

}
#endif /* REMOTEHOST */

#ifndef WINNT_NATIVE
/*
 * indicate if a terminal type is defined in terminfo/termcap
 * (by default the current term type). This allows ppl to look
 * for a working term type automatically in their login scripts
 * when using a terminal known as different things on different
 * platforms
 */
void
dotermname(Char **v, struct command *c)
{
    char *termtype;
    /*
     * Maximum size of a termcap record. We make it twice as large.
     */
    char termcap_buffer[2048];

    USE(c);
    /* try to find which entry we should be looking for */
    termtype = (v[1] == NULL ? getenv("TERM") : short2str(v[1]));
    if (termtype == NULL) {
	/* no luck - the user didn't provide one and none is 
	 * specified in the environment
	 */
	setcopy(STRstatus, STR1, VAR_READWRITE);
	return;
    }

    /*
     * we use the termcap function - if we are using terminfo we 
     * will end up with it's compatibility function
     * terminfo/termcap will be initialized with the new
     * type but we don't care because tcsh has cached all the things
     * it needs.
     */
    if (tgetent(termcap_buffer, termtype) == 1) {
	xprintf("%s\n", termtype);
	setcopy(STRstatus, STR0, VAR_READWRITE);
    } else
	setcopy(STRstatus, STR1, VAR_READWRITE);
}
#endif /* WINNT_NATIVE */
