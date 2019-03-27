/* $Header: /p/tcsh/cvsroot/tcsh/tw.comp.c,v 1.45 2015/09/30 13:28:02 christos Exp $ */
/*
 * tw.comp.c: File completion builtin
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

RCSID("$tcsh: tw.comp.c,v 1.45 2015/09/30 13:28:02 christos Exp $")

#include "tw.h"
#include "ed.h"
#include "tc.h"

/* #define TDEBUG */
struct varent completions;

static int 	 	  tw_result	(const Char *, Char **);
static Char		**tw_find	(Char *, struct varent *, int);
static Char 		 *tw_tok	(Char *);
static int	 	  tw_pos	(Char *, int);
static void	  	  tw_pr		(Char **);
static int	  	  tw_match	(const Char *, const Char *, int);
static void	 	  tw_prlist	(struct varent *);
static const Char	 *tw_dollar	(const Char *,Char **, size_t, Char **,
					 Char, const char *);

/* docomplete():
 *	Add or list completions in the completion list
 */
/*ARGSUSED*/
void
docomplete(Char **v, struct command *t)
{
    struct varent *vp;
    Char *p;
    Char **pp;

    USE(t);
    v++;
    p = *v++;
    if (p == 0)
	tw_prlist(&completions);
    else if (*v == 0) {
	vp = adrof1(strip(p), &completions);
	if (vp && vp->vec)
	    tw_pr(vp->vec), xputchar('\n');
	else
	{
#ifdef TDEBUG
	    xprintf("tw_find(%s) \n", short2str(strip(p)));
#endif /* TDEBUG */
	    pp = tw_find(strip(p), &completions, FALSE);
	    if (pp)
		tw_pr(pp), xputchar('\n');
	}
    }
    else
	set1(strip(p), saveblk(v), &completions, VAR_READWRITE);
} /* end docomplete */


/* douncomplete():
 *	Remove completions from the completion list
 */
/*ARGSUSED*/
void
douncomplete(Char **v, struct command *t)
{
    USE(t);
    unset1(v, &completions);
} /* end douncomplete */


/* tw_prlist():
 *	Pretty print a list of variables
 */
static void
tw_prlist(struct varent *p)
{
    struct varent *c;

    for (;;) {
	while (p->v_left)
	    p = p->v_left;
x:
	if (p->v_parent == 0)	/* is it the header? */
	    break;
	if (setintr) {
	    int old_pintr_disabled;

	    pintr_push_enable(&old_pintr_disabled);
	    cleanup_until(&old_pintr_disabled);
	}
	xprintf("%s\t", short2str(p->v_name));
	if (p->vec)
	    tw_pr(p->vec);
	xputchar('\n');
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
} /* end tw_prlist */


/* tw_pr():
 *	Pretty print a completion, adding single quotes around 
 *	a completion argument and collapsing multiple spaces to one.
 */
static void
tw_pr(Char **cmp)
{
    int sp, osp;
    Char *ptr;

    for (; *cmp; cmp++) {
	xputchar('\'');
	for (osp = 0, ptr = *cmp; *ptr; ptr++) {
	    sp = Isspace(*ptr);
	    if (sp && osp)
		continue;
	    xputwchar(*ptr);
	    osp = sp;
	}
	xputchar('\'');
	if (cmp[1])
	    xputchar(' ');
    }
} /* end tw_pr */


/* tw_find():
 *	Find the first matching completion. 
 *	For commands we only look at names that start with -
 */
static Char **
tw_find(Char *nam, struct varent *vp, int cmd)
{
    Char **rv;

    for (vp = vp->v_left; vp; vp = vp->v_right) {
	if (vp->v_left && (rv = tw_find(nam, vp, cmd)) != NULL)
	    return rv;
	if (cmd) {
	    if (vp->v_name[0] != '-')
		continue;
	    if (Gmatch(nam, &vp->v_name[1]) && vp->vec != NULL)
		return vp->vec;
	}
	else
	    if (Gmatch(nam, vp->v_name) && vp->vec != NULL)
		return vp->vec;
    }
    return NULL;
} /* end tw_find */


/* tw_pos():
 *	Return true if the position is within the specified range
 */
static int
tw_pos(Char *ran, int wno)
{
    Char *p;

    if (ran[0] == '*' && ran[1] == '\0')
	return 1;

    for (p = ran; *p && *p != '-'; p++)
	continue;

    if (*p == '\0')			/* range == <number> */
	return wno == getn(ran);
    
    if (ran == p)			/* range = - <number> */
	return wno <= getn(&ran[1]);
    *p++ = '\0';

    if (*p == '\0')			/* range = <number> - */
	return getn(ran) <= wno;
    else				/* range = <number> - <number> */
	return (getn(ran) <= wno) && (wno <= getn(p));
} /* end tw_pos */


/* tw_tok():
 *	Return the next word from string, unquoteing it.
 */
static Char *
tw_tok(Char *str)
{
    static Char *bf = NULL;

    if (str != NULL)
	bf = str;
    
    /* skip leading spaces */
    for (; *bf && Isspace(*bf); bf++)
	continue;

    for (str = bf; *bf && !Isspace(*bf); bf++) {
	if (ismetahash(*bf))
	    return INVPTR;
	*bf = *bf & ~QUOTE;
    }
    if (*bf != '\0')
	*bf++ = '\0';

    return *str ? str : NULL;
} /* end tw_tok */


/* tw_match():
 *	Match a string against the pattern given.
 *	and return the number of matched characters
 *	in a prefix of the string.
 */
static int
tw_match(const Char *str, const Char *pat, int exact)
{
    const Char *estr;
    int rv = exact ? Gmatch(estr = str, pat) : Gnmatch(str, pat, &estr);
#ifdef TDEBUG
    xprintf("G%smatch(%s, ", exact ? "" : "n", short2str(str));
    xprintf("%s, ", short2str(pat));
    xprintf("%s) = %d [%" TCSH_PTRDIFF_T_FMT "d]\n", short2str(estr), rv,
	estr - str);
#endif /* TDEBUG */
    return (int) (rv ? estr - str : -1);
}


/* tw_result():
 *	Return what the completion action should be depending on the
 *	string
 */
static int
tw_result(const Char *act, Char **pat)
{
    int looking;
    static Char* res = NULL;
    Char *p;

    if (res != NULL)
	xfree(res), res = NULL;

    switch (act[0] & ~QUOTE) {
    case 'X':
	looking = TW_COMPLETION;
	break;
    case 'S':
	looking = TW_SIGNAL;
	break;
    case 'a':
	looking = TW_ALIAS;
	break;
    case 'b':
	looking = TW_BINDING;
	break;
    case 'c':
	looking = TW_COMMAND;
	break;
    case 'C':
	looking = TW_PATH | TW_COMMAND;
	break;
    case 'd':
	looking = TW_DIRECTORY;
	break;
    case 'D':
	looking = TW_PATH | TW_DIRECTORY;
	break;
    case 'e':
	looking = TW_ENVVAR;
	break;
    case 'f':
	looking = TW_FILE;
	break;
#ifdef COMPAT
    case 'p':
#endif /* COMPAT */
    case 'F':
	looking = TW_PATH | TW_FILE;
	break;
    case 'g':
	looking = TW_GRPNAME;
	break;
    case 'j':
	looking = TW_JOB;
	break;
    case 'l':
	looking = TW_LIMIT;
	break;
    case 'n':
	looking = TW_NONE;
	break;
    case 's':
	looking = TW_SHELLVAR;
	break;
    case 't':
	looking = TW_TEXT;
	break;
    case 'T':
	looking = TW_PATH | TW_TEXT;
	break;
    case 'v':
	looking = TW_VARIABLE;
	break;
    case 'u':
	looking = TW_USER;
	break;
    case 'x':
	looking = TW_EXPLAIN;
	break;

    case '$':
	*pat = res = Strsave(&act[1]);
	(void) strip(res);
	return(TW_VARLIST);

    case '(':
	*pat = res = Strsave(&act[1]);
	if ((p = Strchr(res, ')')) != NULL)
	    *p = '\0';
	(void) strip(res);
	return TW_WORDLIST;

    case '`':
	res = Strsave(act);
	if ((p = Strchr(&res[1], '`')) != NULL)
	    *++p = '\0';
	
	if (didfds == 0) {
	    /*
	     * Make sure that we have some file descriptors to
	     * play with, so that the processes have at least 0, 1, 2
	     * open
	     */
	    (void) dcopy(SHIN, 0);
	    (void) dcopy(SHOUT, 1);
	    (void) dcopy(SHDIAG, 2);
	}
	if ((p = globone(res, G_APPEND)) != NULL) {
	    xfree(res), res = NULL;
	    *pat = res = Strsave(p);
	    xfree(p);
	    return TW_WORDLIST;
	}
	return TW_ZERO;

    default:
	stderror(ERR_COMPCOM, short2str(act));
	return TW_ZERO;
    }

    switch (act[1] & ~QUOTE) {
    case '\0':
	return looking;

    case ':':
	*pat = res = Strsave(&act[2]);
	(void) strip(res);
	return looking;

    default:
	stderror(ERR_COMPCOM, short2str(act));
	return TW_ZERO;
    }
} /* end tw_result */


/* tw_dollar():
 *	Expand $<n> args in buffer
 */
static const Char *
tw_dollar(const Char *str, Char **wl, size_t nwl, Char **result, Char sep,
	  const char *msg)
{
    struct Strbuf buf = Strbuf_INIT;
    Char *res;
    const Char *sp;

    for (sp = str; *sp && *sp != sep;)
	if (sp[0] == '$' && sp[1] == ':' && Isdigit(sp[sp[2] == '-' ? 3 : 2])) {
	    int num, neg = 0;
	    sp += 2;
	    if (*sp == '-') {
		neg = 1;
		sp++;
	    }
	    for (num = *sp++ - '0'; Isdigit(*sp); num += 10 * num + *sp++ - '0')
		continue;
	    if (neg)
		num = nwl - num - 1;
	    if (num >= 0 && (size_t)num < nwl)
		Strbuf_append(&buf, wl[num]);
	}
	else
	    Strbuf_append1(&buf, *sp++);

    res = Strbuf_finish(&buf);

    if (*sp++ == sep) {
	*result = res;
	return sp;
    }

    xfree(res);
    /* Truncates data if WIDE_STRINGS */
    stderror(ERR_COMPMIS, (int)sep, msg, short2str(str));
    return --sp;
} /* end tw_dollar */


/* tw_complete():
 *	Return the appropriate completion for the command
 *
 *	valid completion strings are:
 *	p/<range>/<completion>/[<suffix>/]	positional
 *	c/<pattern>/<completion>/[<suffix>/]	current word ignore pattern
 *	C/<pattern>/<completion>/[<suffix>/]	current word with pattern
 *	n/<pattern>/<completion>/[<suffix>/]	next word
 *	N/<pattern>/<completion>/[<suffix>/]	next-next word
 */
int
tw_complete(const Char *line, Char **word, Char **pat, int looking, eChar *suf)
{
    Char *buf, **vec, **wl;
    static Char nomatch[2] = { (Char) ~0, 0x00 };
    const Char *ptr;
    size_t wordno;
    int n;

    buf = Strsave(line);
    cleanup_push(buf, xfree);
    /* Single-character words, empty current word, terminating NULL */
    wl = xmalloc(((Strlen(line) + 1) / 2 + 2) * sizeof (*wl));
    cleanup_push(wl, xfree);

    /* find the command */
    if ((wl[0] = tw_tok(buf)) == NULL || wl[0] == INVPTR) {
	cleanup_until(buf);
	return TW_ZERO;
    }

    /*
     * look for hardwired command completions using a globbing
     * search and for arguments using a normal search.
     */
    if ((vec = tw_find(wl[0], &completions, (looking == TW_COMMAND)))
	== NULL) {
	cleanup_until(buf);
	return looking;
    }

    /* tokenize the line one more time :-( */
    for (wordno = 1; (wl[wordno] = tw_tok(NULL)) != NULL &&
		      wl[wordno] != INVPTR; wordno++)
	continue;

    if (wl[wordno] == INVPTR) {		/* Found a meta character */
	cleanup_until(buf);
	return TW_ZERO;			/* de-activate completions */
    }
#ifdef TDEBUG
    {
	size_t i;
	for (i = 0; i < wordno; i++)
	    xprintf("'%s' ", short2str(wl[i]));
	xprintf("\n");
    }
#endif /* TDEBUG */

    /* if the current word is empty move the last word to the next */
    if (**word == '\0') {
	wl[wordno] = *word;
	wordno++;
    }
    wl[wordno] = NULL;
	

#ifdef TDEBUG
    xprintf("\r\n");
    xprintf("  w#: %lu\n", (unsigned long)wordno);
    xprintf("line: %s\n", short2str(line));
    xprintf(" cmd: %s\n", short2str(wl[0]));
    xprintf("word: %s\n", short2str(*word));
    xprintf("last: %s\n", wordno >= 2 ? short2str(wl[wordno-2]) : "n/a");
    xprintf("this: %s\n", wordno >= 1 ? short2str(wl[wordno-1]) : "n/a");
#endif /* TDEBUG */
    
    for (;vec != NULL && (ptr = vec[0]) != NULL; vec++) {
	Char  *ran,	        /* The pattern or range X/<range>/XXXX/ */
	      *com,	        /* The completion X/XXXXX/<completion>/ */
	     *pos = NULL;	/* scratch pointer 			*/
	int   cmd, res;
        Char  sep;		/* the command and separator characters */
	int   exact;

	if (ptr[0] == '\0')
	    continue;

#ifdef TDEBUG
	xprintf("match %s\n", short2str(ptr));
#endif /* TDEBUG */

	switch (cmd = ptr[0]) {
	case 'N':
	    pos = (wordno < 3) ? nomatch : wl[wordno - 3];
	    break;
	case 'n':
	    pos = (wordno < 2) ? nomatch : wl[wordno - 2];
	    break;
	case 'c':
	case 'C':
	    pos = (wordno < 1) ? nomatch : wl[wordno - 1];
	    break;
	case 'p':
	    break;
	default:
	    stderror(ERR_COMPINV, CGETS(27, 1, "command"), cmd);
	    return TW_ZERO;
	}

	sep = ptr[1];
	if (!Ispunct(sep)) {
	    /* Truncates data if WIDE_STRINGS */
	    stderror(ERR_COMPINV, CGETS(27, 2, "separator"), (int)sep);
	    return TW_ZERO;
	}

	ptr = tw_dollar(&ptr[2], wl, wordno, &ran, sep,
			CGETS(27, 3, "pattern"));
	cleanup_push(ran, xfree);
	if (ran[0] == '\0')	/* check for empty pattern (disallowed) */
	{
	    stderror(ERR_COMPINC, cmd == 'p' ?  CGETS(27, 4, "range") :
		     CGETS(27, 3, "pattern"), "");
	    return TW_ZERO;
	}

	ptr = tw_dollar(ptr, wl, wordno, &com, sep,
			CGETS(27, 5, "completion"));
	cleanup_push(com, xfree);

	if (*ptr != '\0') {
	    if (*ptr == sep)
		*suf = CHAR_ERR;
	    else
		*suf = *ptr;
	}
	else
	    *suf = '\0';

#ifdef TDEBUG
	xprintf("command:    %c\nseparator:  %c\n", cmd, (int)sep);
	xprintf("pattern:    %s\n", short2str(ran));
	xprintf("completion: %s\n", short2str(com));
	xprintf("suffix:     ");
        switch (*suf) {
	case 0:
	    xprintf("*auto suffix*\n");
	    break;
	case CHAR_ERR:
	    xprintf("*no suffix*\n");
	    break;
	default:
	    xprintf("%c\n", (int)*suf);
	    break;
	}
#endif /* TDEBUG */

	exact = 0;
	switch (cmd) {
	case 'p':			/* positional completion */
#ifdef TDEBUG
	    xprintf("p: tw_pos(%s, %lu) = ", short2str(ran),
		    (unsigned long)wordno - 1);
	    xprintf("%d\n", tw_pos(ran, wordno - 1));
#endif /* TDEBUG */
	    if (!tw_pos(ran, wordno - 1)) {
		cleanup_until(ran);
		continue;
	    }
	    break;

	case 'N':			/* match with the next-next word */
	case 'n':			/* match with the next word */
	    exact = 1;
	    /*FALLTHROUGH*/
	case 'c':			/* match with the current word */
	case 'C':
#ifdef TDEBUG
	    xprintf("%c: ", cmd);
#endif /* TDEBUG */
	    if ((n = tw_match(pos, ran, exact)) < 0) {
		cleanup_until(ran);
		continue;
	    }
	    if (cmd == 'c')
		*word += n;
	    break;

	default:
	    abort();		       /* Cannot happen */
	}
	tsetenv(STRCOMMAND_LINE, line);
	res = tw_result(com, pat);
	Unsetenv(STRCOMMAND_LINE);
	cleanup_until(buf);
	return res;
    }
    cleanup_until(buf);
    *suf = '\0';
    return TW_ZERO;
} /* end tw_complete */
