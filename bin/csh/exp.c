/*	$OpenBSD: exp.c,v 1.18 2018/09/18 06:56:09 deraadt Exp $	*/
/*	$NetBSD: exp.c,v 1.6 1995/03/21 09:02:51 cgd Exp $	*/

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
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "csh.h"
#include "extern.h"

#define IGNORE	1	/* in ignore, it means to ignore value, just parse */
#define NOGLOB	2	/* in ignore, it means not to globone */

#define	ADDOP	1
#define	MULOP	2
#define	EQOP	4
#define	RELOP	8
#define	RESTOP	16
#define	ANYOP	31

#define	EQEQ	1
#define	GTR	2
#define	LSS	4
#define	NOTEQ	6
#define EQMATCH 7
#define NOTEQMATCH 8

static int	exp1(Char ***, bool);
static int	exp2_(Char ***, bool);
static int	exp2a(Char ***, bool);
static int	exp2b(Char ***, bool);
static int	exp2c(Char ***, bool);
static Char *	exp3(Char ***, bool);
static Char *	exp3a(Char ***, bool);
static Char *	exp4(Char ***, bool);
static Char *	exp5(Char ***, bool);
static Char *	exp6(Char ***, bool);
static void	evalav(Char **);
static int	isa(Char *, int);
static int	egetn(Char *);

int
expr(Char ***vp)
{
    return (exp0(vp, 0));
}

int
exp0(Char ***vp, bool ignore)
{
    int p1 = exp1(vp, ignore);

    if (**vp && eq(**vp, STRor2)) {
	int p2;

	(*vp)++;
	p2 = exp0(vp, (ignore & IGNORE) || p1);
	return (p1 || p2);
    }
    return (p1);
}

static int
exp1(Char ***vp, bool ignore)
{
    int p1 = exp2_(vp, ignore);

    if (**vp && eq(**vp, STRand2)) {
	int p2;

	(*vp)++;
	p2 = exp1(vp, (ignore & IGNORE) || !p1);
	return (p1 && p2);
    }
    return (p1);
}

static int
exp2_(Char ***vp, bool ignore)
{
    int p1 = exp2a(vp, ignore);

    if (**vp && eq(**vp, STRor)) {
	int p2;

	(*vp)++;
	p2 = exp2_(vp, ignore);
	return (p1 | p2);
    }
    return (p1);
}

static int
exp2a(Char ***vp, bool ignore)
{
    int p1 = exp2b(vp, ignore);

    if (**vp && eq(**vp, STRcaret)) {
	int p2;

	(*vp)++;
	p2 = exp2a(vp, ignore);
	return (p1 ^ p2);
    }
    return (p1);
}

static int
exp2b(Char ***vp, bool ignore)
{
    int p1 = exp2c(vp, ignore);

    if (**vp && eq(**vp, STRand)) {
	int p2;

	(*vp)++;
	p2 = exp2b(vp, ignore);
	return (p1 & p2);
    }
    return (p1);
}

static int
exp2c(Char ***vp, bool ignore)
{
    Char *p1 = exp3(vp, ignore);
    Char *p2;
    int i;

    if ((i = isa(**vp, EQOP)) != 0) {
	(*vp)++;
	if (i == EQMATCH || i == NOTEQMATCH)
	    ignore |= NOGLOB;
	p2 = exp3(vp, ignore);
	if (!(ignore & IGNORE))
	    switch (i) {

	    case EQEQ:
		i = eq(p1, p2);
		break;

	    case NOTEQ:
		i = !eq(p1, p2);
		break;

	    case EQMATCH:
		i = Gmatch(p1, p2);
		break;

	    case NOTEQMATCH:
		i = !Gmatch(p1, p2);
		break;
	    }
	free(p1);
	free(p2);
	return (i);
    }
    i = egetn(p1);
    free(p1);
    return (i);
}

static Char *
exp3(Char ***vp, bool ignore)
{
    Char *p1, *p2;
    int i;

    p1 = exp3a(vp, ignore);
    if ((i = isa(**vp, RELOP)) != 0) {
	(*vp)++;
	if (**vp && eq(**vp, STRequal))
	    i |= 1, (*vp)++;
	p2 = exp3(vp, ignore);
	if (!(ignore & IGNORE))
	    switch (i) {

	    case GTR:
		i = egetn(p1) > egetn(p2);
		break;

	    case GTR | 1:
		i = egetn(p1) >= egetn(p2);
		break;

	    case LSS:
		i = egetn(p1) < egetn(p2);
		break;

	    case LSS | 1:
		i = egetn(p1) <= egetn(p2);
		break;
	    }
	free(p1);
	free(p2);
	return (putn(i));
    }
    return (p1);
}

static Char *
exp3a(Char ***vp, bool ignore)
{
    Char *p1, *p2, *op;
    int i;

    p1 = exp4(vp, ignore);
    op = **vp;
    if (op && any("<>", op[0]) && op[0] == op[1]) {
	(*vp)++;
	p2 = exp3a(vp, ignore);
	if (op[0] == '<')
	    i = egetn(p1) << egetn(p2);
	else
	    i = egetn(p1) >> egetn(p2);
	free(p1);
	free(p2);
	return (putn(i));
    }
    return (p1);
}

static Char *
exp4(Char ***vp, bool ignore)
{
    Char *p1, *p2;
    int i = 0;

    p1 = exp5(vp, ignore);
    if (isa(**vp, ADDOP)) {
	Char *op = *(*vp)++;

	p2 = exp4(vp, ignore);
	if (!(ignore & IGNORE))
	    switch (op[0]) {

	    case '+':
		i = egetn(p1) + egetn(p2);
		break;

	    case '-':
		i = egetn(p1) - egetn(p2);
		break;
	    }
	free(p1);
	free(p2);
	return (putn(i));
    }
    return (p1);
}

static Char *
exp5(Char ***vp, bool ignore)
{
    Char *p1, *p2;
    int i = 0, l;

    p1 = exp6(vp, ignore);
    if (isa(**vp, MULOP)) {
	Char *op = *(*vp)++;

	p2 = exp5(vp, ignore);
	if (!(ignore & IGNORE))
	    switch (op[0]) {

	    case '*':
		i = egetn(p1) * egetn(p2);
		break;

	    case '/':
		i = egetn(p2);
		if (i == 0)
		    stderror(ERR_DIV0);
		l = egetn(p1);
		if (l == INT_MIN && i == -1)
			i = INT_MIN;
		else
			i = l / i;
		break;

	    case '%':
		i = egetn(p2);
		if (i == 0)
		    stderror(ERR_MOD0);
		l = egetn(p1);
		if (l == INT_MIN && i == -1)
			i = 0;
		else
			i = l % i;
		break;
	    }
	free(p1);
	free(p2);
	return (putn(i));
    }
    return (p1);
}

static Char *
exp6(Char ***vp, bool ignore)
{
    int     ccode, i = 0;
    Char *cp, *dp, *ep;

    if (**vp == 0)
	stderror(ERR_NAME | ERR_EXPRESSION);
    if (eq(**vp, STRbang)) {
	(*vp)++;
	cp = exp6(vp, ignore);
	i = egetn(cp);
	free(cp);
	return (putn(!i));
    }
    if (eq(**vp, STRtilde)) {
	(*vp)++;
	cp = exp6(vp, ignore);
	i = egetn(cp);
	free(cp);
	return (putn(~i));
    }
    if (eq(**vp, STRLparen)) {
	(*vp)++;
	ccode = exp0(vp, ignore);
	if (*vp == 0 || **vp == 0 || ***vp != ')')
	    stderror(ERR_NAME | ERR_EXPRESSION);
	(*vp)++;
	return (putn(ccode));
    }
    if (eq(**vp, STRLbrace)) {
	Char **v;
	struct command faket;
	Char   *fakecom[2];

	faket.t_dtyp = NODE_COMMAND;
	faket.t_dflg = 0;
	faket.t_dcar = faket.t_dcdr = faket.t_dspr = NULL;
	faket.t_dcom = fakecom;
	fakecom[0] = STRfakecom;
	fakecom[1] = NULL;
	(*vp)++;
	v = *vp;
	for (;;) {
	    if (!**vp)
		stderror(ERR_NAME | ERR_MISSING, '}');
	    if (eq(*(*vp)++, STRRbrace))
		break;
	}
	if (ignore & IGNORE)
	    return (Strsave(STRNULL));
	psavejob();
	if (pfork(&faket, -1) == 0) {
	    *--(*vp) = 0;
	    evalav(v);
	    exitstat();
	}
	pwait();
	prestjob();
	return (putn(egetn(value(STRstatus)) == 0));
    }
    if (isa(**vp, ANYOP))
	return (Strsave(STRNULL));
    cp = *(*vp)++;
    if (*cp == '-' && any("erwxfdzopls", cp[1])) {
	struct stat stb;

	if (cp[2] != '\0')
	    stderror(ERR_NAME | ERR_FILEINQ);
	/*
	 * Detect missing file names by checking for operator in the file name
	 * position.  However, if an operator name appears there, we must make
	 * sure that there's no file by that name (e.g., "/") before announcing
	 * an error.  Even this check isn't quite right, since it doesn't take
	 * globbing into account.
	 */
	if (isa(**vp, ANYOP) && stat(short2str(**vp), &stb))
	    stderror(ERR_NAME | ERR_FILENAME);

	dp = *(*vp)++;
	if (ignore & IGNORE)
	    return (Strsave(STRNULL));
	ep = globone(dp, G_ERROR);
	switch (cp[1]) {

	case 'r':
	    i = !access(short2str(ep), R_OK);
	    break;

	case 'w':
	    i = !access(short2str(ep), W_OK);
	    break;

	case 'x':
	    i = !access(short2str(ep), X_OK);
	    break;

	default:
	    if (cp[1] == 'l' ? lstat(short2str(ep), &stb) :
		stat(short2str(ep), &stb)) {
		free(ep);
		return (Strsave(STR0));
	    }
	    switch (cp[1]) {

	    case 'f':
		i = S_ISREG(stb.st_mode);
		break;

	    case 'd':
		i = S_ISDIR(stb.st_mode);
		break;

	    case 'p':
		i = S_ISFIFO(stb.st_mode);
		break;

	    case 'l':
		i = S_ISLNK(stb.st_mode);
		break;

	    case 's':
		i = S_ISSOCK(stb.st_mode);
		break;

	    case 'z':
		i = stb.st_size == 0;
		break;

	    case 'e':
		i = 1;
		break;

	    case 'o':
		i = stb.st_uid == uid;
		break;
	    }
	}
	free(ep);
	return (putn(i));
    }
    return (ignore & NOGLOB ? Strsave(cp) : globone(cp, G_ERROR));
}

static void
evalav(Char **v)
{
    struct wordent paraml1;
    struct wordent *hp = &paraml1;
    struct command *t;
    struct wordent *wdp = hp;

    set(STRstatus, Strsave(STR0));
    hp->prev = hp->next = hp;
    hp->word = STRNULL;
    while (*v) {
	struct wordent *new = xcalloc(1, sizeof *wdp);

	new->prev = wdp;
	new->next = hp;
	wdp->next = new;
	wdp = new;
	wdp->word = Strsave(*v++);
    }
    hp->prev = wdp;
    alias(&paraml1);
    t = syntax(paraml1.next, &paraml1, 0);
    if (seterr)
	stderror(ERR_OLD);
    execute(t, -1, NULL, NULL);
    freelex(&paraml1), freesyn(t);
}

static int
isa(Char *cp, int what)
{
    if (cp == 0)
	return ((what & RESTOP) != 0);
    if (cp[1] == 0) {
	if (what & ADDOP && (*cp == '+' || *cp == '-'))
	    return (1);
	if (what & MULOP && (*cp == '*' || *cp == '/' || *cp == '%'))
	    return (1);
	if (what & RESTOP && (*cp == '(' || *cp == ')' || *cp == '!' ||
			      *cp == '~' || *cp == '^' || *cp == '"'))
	    return (1);
    }
    else if (cp[2] == 0) {
	if (what & RESTOP) {
	    if (cp[0] == '|' && cp[1] == '&')
		return (1);
	    if (cp[0] == '<' && cp[1] == '<')
		return (1);
	    if (cp[0] == '>' && cp[1] == '>')
		return (1);
	}
	if (what & EQOP) {
	    if (cp[0] == '=') {
		if (cp[1] == '=')
		    return (EQEQ);
		if (cp[1] == '~')
		    return (EQMATCH);
	    }
	    else if (cp[0] == '!') {
		if (cp[1] == '=')
		    return (NOTEQ);
		if (cp[1] == '~')
		    return (NOTEQMATCH);
	    }
	}
    }
    if (what & RELOP) {
	if (*cp == '<')
	    return (LSS);
	if (*cp == '>')
	    return (GTR);
    }
    return (0);
}

static int
egetn(Char *cp)
{
    if (*cp && *cp != '-' && !Isdigit(*cp))
	stderror(ERR_NAME | ERR_EXPRESSION);
    return (getn(cp));
}

/* Phew! */
