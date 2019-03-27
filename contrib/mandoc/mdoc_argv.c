/*	$Id: mdoc_argv.c,v 1.115 2017/05/30 16:22:03 schwarze Exp $ */
/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2012, 2014-2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "mdoc.h"
#include "libmandoc.h"
#include "roff_int.h"
#include "libmdoc.h"

#define	MULTI_STEP	 5 /* pre-allocate argument values */
#define	DELIMSZ		 6 /* max possible size of a delimiter */

enum	argsflag {
	ARGSFL_NONE = 0,
	ARGSFL_DELIM, /* handle delimiters of [[::delim::][ ]+]+ */
	ARGSFL_TABSEP /* handle tab/`Ta' separated phrases */
};

enum	argvflag {
	ARGV_NONE, /* no args to flag (e.g., -split) */
	ARGV_SINGLE, /* one arg to flag (e.g., -file xxx)  */
	ARGV_MULTI /* multiple args (e.g., -column xxx yyy) */
};

struct	mdocarg {
	enum argsflag	 flags;
	const enum mdocargt *argvs;
};

static	void		 argn_free(struct mdoc_arg *, int);
static	enum margserr	 args(struct roff_man *, int, int *,
				char *, enum argsflag, char **);
static	int		 args_checkpunct(const char *, int);
static	void		 argv_multi(struct roff_man *, int,
				struct mdoc_argv *, int *, char *);
static	void		 argv_single(struct roff_man *, int,
				struct mdoc_argv *, int *, char *);

static	const enum argvflag argvflags[MDOC_ARG_MAX] = {
	ARGV_NONE,	/* MDOC_Split */
	ARGV_NONE,	/* MDOC_Nosplit */
	ARGV_NONE,	/* MDOC_Ragged */
	ARGV_NONE,	/* MDOC_Unfilled */
	ARGV_NONE,	/* MDOC_Literal */
	ARGV_SINGLE,	/* MDOC_File */
	ARGV_SINGLE,	/* MDOC_Offset */
	ARGV_NONE,	/* MDOC_Bullet */
	ARGV_NONE,	/* MDOC_Dash */
	ARGV_NONE,	/* MDOC_Hyphen */
	ARGV_NONE,	/* MDOC_Item */
	ARGV_NONE,	/* MDOC_Enum */
	ARGV_NONE,	/* MDOC_Tag */
	ARGV_NONE,	/* MDOC_Diag */
	ARGV_NONE,	/* MDOC_Hang */
	ARGV_NONE,	/* MDOC_Ohang */
	ARGV_NONE,	/* MDOC_Inset */
	ARGV_MULTI,	/* MDOC_Column */
	ARGV_SINGLE,	/* MDOC_Width */
	ARGV_NONE,	/* MDOC_Compact */
	ARGV_NONE,	/* MDOC_Std */
	ARGV_NONE,	/* MDOC_Filled */
	ARGV_NONE,	/* MDOC_Words */
	ARGV_NONE,	/* MDOC_Emphasis */
	ARGV_NONE,	/* MDOC_Symbolic */
	ARGV_NONE	/* MDOC_Symbolic */
};

static	const enum mdocargt args_Ex[] = {
	MDOC_Std,
	MDOC_ARG_MAX
};

static	const enum mdocargt args_An[] = {
	MDOC_Split,
	MDOC_Nosplit,
	MDOC_ARG_MAX
};

static	const enum mdocargt args_Bd[] = {
	MDOC_Ragged,
	MDOC_Unfilled,
	MDOC_Filled,
	MDOC_Literal,
	MDOC_File,
	MDOC_Offset,
	MDOC_Compact,
	MDOC_Centred,
	MDOC_ARG_MAX
};

static	const enum mdocargt args_Bf[] = {
	MDOC_Emphasis,
	MDOC_Literal,
	MDOC_Symbolic,
	MDOC_ARG_MAX
};

static	const enum mdocargt args_Bk[] = {
	MDOC_Words,
	MDOC_ARG_MAX
};

static	const enum mdocargt args_Bl[] = {
	MDOC_Bullet,
	MDOC_Dash,
	MDOC_Hyphen,
	MDOC_Item,
	MDOC_Enum,
	MDOC_Tag,
	MDOC_Diag,
	MDOC_Hang,
	MDOC_Ohang,
	MDOC_Inset,
	MDOC_Column,
	MDOC_Width,
	MDOC_Offset,
	MDOC_Compact,
	MDOC_Nested,
	MDOC_ARG_MAX
};

static	const struct mdocarg __mdocargs[MDOC_MAX - MDOC_Dd] = {
	{ ARGSFL_NONE, NULL }, /* Dd */
	{ ARGSFL_NONE, NULL }, /* Dt */
	{ ARGSFL_NONE, NULL }, /* Os */
	{ ARGSFL_NONE, NULL }, /* Sh */
	{ ARGSFL_NONE, NULL }, /* Ss */
	{ ARGSFL_NONE, NULL }, /* Pp */
	{ ARGSFL_DELIM, NULL }, /* D1 */
	{ ARGSFL_DELIM, NULL }, /* Dl */
	{ ARGSFL_NONE, args_Bd }, /* Bd */
	{ ARGSFL_NONE, NULL }, /* Ed */
	{ ARGSFL_NONE, args_Bl }, /* Bl */
	{ ARGSFL_NONE, NULL }, /* El */
	{ ARGSFL_NONE, NULL }, /* It */
	{ ARGSFL_DELIM, NULL }, /* Ad */
	{ ARGSFL_DELIM, args_An }, /* An */
	{ ARGSFL_DELIM, NULL }, /* Ap */
	{ ARGSFL_DELIM, NULL }, /* Ar */
	{ ARGSFL_DELIM, NULL }, /* Cd */
	{ ARGSFL_DELIM, NULL }, /* Cm */
	{ ARGSFL_DELIM, NULL }, /* Dv */
	{ ARGSFL_DELIM, NULL }, /* Er */
	{ ARGSFL_DELIM, NULL }, /* Ev */
	{ ARGSFL_NONE, args_Ex }, /* Ex */
	{ ARGSFL_DELIM, NULL }, /* Fa */
	{ ARGSFL_NONE, NULL }, /* Fd */
	{ ARGSFL_DELIM, NULL }, /* Fl */
	{ ARGSFL_DELIM, NULL }, /* Fn */
	{ ARGSFL_DELIM, NULL }, /* Ft */
	{ ARGSFL_DELIM, NULL }, /* Ic */
	{ ARGSFL_DELIM, NULL }, /* In */
	{ ARGSFL_DELIM, NULL }, /* Li */
	{ ARGSFL_NONE, NULL }, /* Nd */
	{ ARGSFL_DELIM, NULL }, /* Nm */
	{ ARGSFL_DELIM, NULL }, /* Op */
	{ ARGSFL_DELIM, NULL }, /* Ot */
	{ ARGSFL_DELIM, NULL }, /* Pa */
	{ ARGSFL_NONE, args_Ex }, /* Rv */
	{ ARGSFL_DELIM, NULL }, /* St */
	{ ARGSFL_DELIM, NULL }, /* Va */
	{ ARGSFL_DELIM, NULL }, /* Vt */
	{ ARGSFL_DELIM, NULL }, /* Xr */
	{ ARGSFL_NONE, NULL }, /* %A */
	{ ARGSFL_NONE, NULL }, /* %B */
	{ ARGSFL_NONE, NULL }, /* %D */
	{ ARGSFL_NONE, NULL }, /* %I */
	{ ARGSFL_NONE, NULL }, /* %J */
	{ ARGSFL_NONE, NULL }, /* %N */
	{ ARGSFL_NONE, NULL }, /* %O */
	{ ARGSFL_NONE, NULL }, /* %P */
	{ ARGSFL_NONE, NULL }, /* %R */
	{ ARGSFL_NONE, NULL }, /* %T */
	{ ARGSFL_NONE, NULL }, /* %V */
	{ ARGSFL_DELIM, NULL }, /* Ac */
	{ ARGSFL_NONE, NULL }, /* Ao */
	{ ARGSFL_DELIM, NULL }, /* Aq */
	{ ARGSFL_DELIM, NULL }, /* At */
	{ ARGSFL_DELIM, NULL }, /* Bc */
	{ ARGSFL_NONE, args_Bf }, /* Bf */
	{ ARGSFL_NONE, NULL }, /* Bo */
	{ ARGSFL_DELIM, NULL }, /* Bq */
	{ ARGSFL_DELIM, NULL }, /* Bsx */
	{ ARGSFL_DELIM, NULL }, /* Bx */
	{ ARGSFL_NONE, NULL }, /* Db */
	{ ARGSFL_DELIM, NULL }, /* Dc */
	{ ARGSFL_NONE, NULL }, /* Do */
	{ ARGSFL_DELIM, NULL }, /* Dq */
	{ ARGSFL_DELIM, NULL }, /* Ec */
	{ ARGSFL_NONE, NULL }, /* Ef */
	{ ARGSFL_DELIM, NULL }, /* Em */
	{ ARGSFL_NONE, NULL }, /* Eo */
	{ ARGSFL_DELIM, NULL }, /* Fx */
	{ ARGSFL_DELIM, NULL }, /* Ms */
	{ ARGSFL_DELIM, NULL }, /* No */
	{ ARGSFL_DELIM, NULL }, /* Ns */
	{ ARGSFL_DELIM, NULL }, /* Nx */
	{ ARGSFL_DELIM, NULL }, /* Ox */
	{ ARGSFL_DELIM, NULL }, /* Pc */
	{ ARGSFL_DELIM, NULL }, /* Pf */
	{ ARGSFL_NONE, NULL }, /* Po */
	{ ARGSFL_DELIM, NULL }, /* Pq */
	{ ARGSFL_DELIM, NULL }, /* Qc */
	{ ARGSFL_DELIM, NULL }, /* Ql */
	{ ARGSFL_NONE, NULL }, /* Qo */
	{ ARGSFL_DELIM, NULL }, /* Qq */
	{ ARGSFL_NONE, NULL }, /* Re */
	{ ARGSFL_NONE, NULL }, /* Rs */
	{ ARGSFL_DELIM, NULL }, /* Sc */
	{ ARGSFL_NONE, NULL }, /* So */
	{ ARGSFL_DELIM, NULL }, /* Sq */
	{ ARGSFL_NONE, NULL }, /* Sm */
	{ ARGSFL_DELIM, NULL }, /* Sx */
	{ ARGSFL_DELIM, NULL }, /* Sy */
	{ ARGSFL_DELIM, NULL }, /* Tn */
	{ ARGSFL_DELIM, NULL }, /* Ux */
	{ ARGSFL_DELIM, NULL }, /* Xc */
	{ ARGSFL_NONE, NULL }, /* Xo */
	{ ARGSFL_NONE, NULL }, /* Fo */
	{ ARGSFL_DELIM, NULL }, /* Fc */
	{ ARGSFL_NONE, NULL }, /* Oo */
	{ ARGSFL_DELIM, NULL }, /* Oc */
	{ ARGSFL_NONE, args_Bk }, /* Bk */
	{ ARGSFL_NONE, NULL }, /* Ek */
	{ ARGSFL_NONE, NULL }, /* Bt */
	{ ARGSFL_NONE, NULL }, /* Hf */
	{ ARGSFL_DELIM, NULL }, /* Fr */
	{ ARGSFL_NONE, NULL }, /* Ud */
	{ ARGSFL_DELIM, NULL }, /* Lb */
	{ ARGSFL_NONE, NULL }, /* Lp */
	{ ARGSFL_DELIM, NULL }, /* Lk */
	{ ARGSFL_DELIM, NULL }, /* Mt */
	{ ARGSFL_DELIM, NULL }, /* Brq */
	{ ARGSFL_NONE, NULL }, /* Bro */
	{ ARGSFL_DELIM, NULL }, /* Brc */
	{ ARGSFL_NONE, NULL }, /* %C */
	{ ARGSFL_NONE, NULL }, /* Es */
	{ ARGSFL_DELIM, NULL }, /* En */
	{ ARGSFL_DELIM, NULL }, /* Dx */
	{ ARGSFL_NONE, NULL }, /* %Q */
	{ ARGSFL_NONE, NULL }, /* %U */
	{ ARGSFL_NONE, NULL }, /* Ta */
};
static	const struct mdocarg *const mdocargs = __mdocargs - MDOC_Dd;


/*
 * Parse flags and their arguments from the input line.
 * These come in the form -flag [argument ...].
 * Some flags take no argument, some one, some multiple.
 */
void
mdoc_argv(struct roff_man *mdoc, int line, enum roff_tok tok,
	struct mdoc_arg **reta, int *pos, char *buf)
{
	struct mdoc_argv	  tmpv;
	struct mdoc_argv	**retv;
	const enum mdocargt	 *argtable;
	char			 *argname;
	int			  ipos, retc;
	char			  savechar;

	*reta = NULL;

	/* Which flags does this macro support? */

	assert(tok >= MDOC_Dd && tok < MDOC_MAX);
	argtable = mdocargs[tok].argvs;
	if (argtable == NULL)
		return;

	/* Loop over the flags on the input line. */

	ipos = *pos;
	while (buf[ipos] == '-') {

		/* Seek to the first unescaped space. */

		for (argname = buf + ++ipos; buf[ipos] != '\0'; ipos++)
			if (buf[ipos] == ' ' && buf[ipos - 1] != '\\')
				break;

		/*
		 * We want to nil-terminate the word to look it up.
		 * But we may not have a flag, in which case we need
		 * to restore the line as-is.  So keep around the
		 * stray byte, which we'll reset upon exiting.
		 */

		if ((savechar = buf[ipos]) != '\0')
			buf[ipos++] = '\0';

		/*
		 * Now look up the word as a flag.  Use temporary
		 * storage that we'll copy into the node's flags.
		 */

		while ((tmpv.arg = *argtable++) != MDOC_ARG_MAX)
			if ( ! strcmp(argname, mdoc_argnames[tmpv.arg]))
				break;

		/* If it isn't a flag, restore the saved byte. */

		if (tmpv.arg == MDOC_ARG_MAX) {
			if (savechar != '\0')
				buf[ipos - 1] = savechar;
			break;
		}

		/* Read to the next word (the first argument). */

		while (buf[ipos] == ' ')
			ipos++;

		/* Parse the arguments of the flag. */

		tmpv.line  = line;
		tmpv.pos   = *pos;
		tmpv.sz    = 0;
		tmpv.value = NULL;

		switch (argvflags[tmpv.arg]) {
		case ARGV_SINGLE:
			argv_single(mdoc, line, &tmpv, &ipos, buf);
			break;
		case ARGV_MULTI:
			argv_multi(mdoc, line, &tmpv, &ipos, buf);
			break;
		case ARGV_NONE:
			break;
		}

		/* Append to the return values. */

		if (*reta == NULL)
			*reta = mandoc_calloc(1, sizeof(**reta));

		retc = ++(*reta)->argc;
		retv = &(*reta)->argv;
		*retv = mandoc_reallocarray(*retv, retc, sizeof(**retv));
		memcpy(*retv + retc - 1, &tmpv, sizeof(**retv));

		/* Prepare for parsing the next flag. */

		*pos = ipos;
		argtable = mdocargs[tok].argvs;
	}
}

void
mdoc_argv_free(struct mdoc_arg *p)
{
	int		 i;

	if (NULL == p)
		return;

	if (p->refcnt) {
		--(p->refcnt);
		if (p->refcnt)
			return;
	}
	assert(p->argc);

	for (i = (int)p->argc - 1; i >= 0; i--)
		argn_free(p, i);

	free(p->argv);
	free(p);
}

static void
argn_free(struct mdoc_arg *p, int iarg)
{
	struct mdoc_argv *arg;
	int		  j;

	arg = &p->argv[iarg];

	if (arg->sz && arg->value) {
		for (j = (int)arg->sz - 1; j >= 0; j--)
			free(arg->value[j]);
		free(arg->value);
	}

	for (--p->argc; iarg < (int)p->argc; iarg++)
		p->argv[iarg] = p->argv[iarg+1];
}

enum margserr
mdoc_args(struct roff_man *mdoc, int line, int *pos,
	char *buf, enum roff_tok tok, char **v)
{
	struct roff_node *n;
	char		 *v_local;
	enum argsflag	  fl;

	if (v == NULL)
		v = &v_local;
	fl = tok == TOKEN_NONE ? ARGSFL_NONE : mdocargs[tok].flags;

	/*
	 * We know that we're in an `It', so it's reasonable to expect
	 * us to be sitting in a `Bl'.  Someday this may not be the case
	 * (if we allow random `It's sitting out there), so provide a
	 * safe fall-back into the default behaviour.
	 */

	if (tok == MDOC_It) {
		for (n = mdoc->last; n != NULL; n = n->parent) {
			if (n->tok != MDOC_Bl)
				continue;
			if (n->norm->Bl.type == LIST_column)
				fl = ARGSFL_TABSEP;
			break;
		}
	}

	return args(mdoc, line, pos, buf, fl, v);
}

static enum margserr
args(struct roff_man *mdoc, int line, int *pos,
		char *buf, enum argsflag fl, char **v)
{
	char		*p;
	int		 pairs;

	if (buf[*pos] == '\0') {
		if (mdoc->flags & MDOC_PHRASELIT &&
		    ! (mdoc->flags & MDOC_PHRASE)) {
			mandoc_msg(MANDOCERR_ARG_QUOTE,
			    mdoc->parse, line, *pos, NULL);
			mdoc->flags &= ~MDOC_PHRASELIT;
		}
		return ARGS_EOLN;
	}

	*v = buf + *pos;

	if (fl == ARGSFL_DELIM && args_checkpunct(buf, *pos))
		return ARGS_PUNCT;

	/*
	 * Tabs in `It' lines in `Bl -column' can't be escaped.
	 * Phrases are reparsed for `Ta' and other macros later.
	 */

	if (fl == ARGSFL_TABSEP) {
		if ((p = strchr(*v, '\t')) != NULL) {

			/*
			 * Words right before and right after
			 * tab characters are not parsed,
			 * unless there is a blank in between.
			 */

			if (p > buf && p[-1] != ' ')
				mdoc->flags |= MDOC_PHRASEQL;
			if (p[1] != ' ')
				mdoc->flags |= MDOC_PHRASEQN;

			/*
			 * One or more blanks after a tab cause
			 * one leading blank in the next column.
			 * So skip all but one of them.
			 */

			*pos += (int)(p - *v) + 1;
			while (buf[*pos] == ' ' && buf[*pos + 1] == ' ')
				(*pos)++;

			/*
			 * A tab at the end of an input line
			 * switches to the next column.
			 */

			if (buf[*pos] == '\0' || buf[*pos + 1] == '\0')
				mdoc->flags |= MDOC_PHRASEQN;
		} else {
			p = strchr(*v, '\0');
			if (p[-1] == ' ')
				mandoc_msg(MANDOCERR_SPACE_EOL,
				    mdoc->parse, line, *pos, NULL);
			*pos += (int)(p - *v);
		}

		/* Skip any trailing blank characters. */
		while (p > *v && p[-1] == ' ' &&
		    (p - 1 == *v || p[-2] != '\\'))
			p--;
		*p = '\0';

		return ARGS_PHRASE;
	}

	/*
	 * Process a quoted literal.  A quote begins with a double-quote
	 * and ends with a double-quote NOT preceded by a double-quote.
	 * NUL-terminate the literal in place.
	 * Collapse pairs of quotes inside quoted literals.
	 * Whitespace is NOT involved in literal termination.
	 */

	if (mdoc->flags & MDOC_PHRASELIT || buf[*pos] == '\"') {
		if ( ! (mdoc->flags & MDOC_PHRASELIT))
			*v = &buf[++(*pos)];

		if (mdoc->flags & MDOC_PHRASE)
			mdoc->flags |= MDOC_PHRASELIT;

		pairs = 0;
		for ( ; buf[*pos]; (*pos)++) {
			/* Move following text left after quoted quotes. */
			if (pairs)
				buf[*pos - pairs] = buf[*pos];
			if ('\"' != buf[*pos])
				continue;
			/* Unquoted quotes end quoted args. */
			if ('\"' != buf[*pos + 1])
				break;
			/* Quoted quotes collapse. */
			pairs++;
			(*pos)++;
		}
		if (pairs)
			buf[*pos - pairs] = '\0';

		if (buf[*pos] == '\0') {
			if ( ! (mdoc->flags & MDOC_PHRASE))
				mandoc_msg(MANDOCERR_ARG_QUOTE,
				    mdoc->parse, line, *pos, NULL);
			return ARGS_WORD;
		}

		mdoc->flags &= ~MDOC_PHRASELIT;
		buf[(*pos)++] = '\0';

		if ('\0' == buf[*pos])
			return ARGS_WORD;

		while (' ' == buf[*pos])
			(*pos)++;

		if ('\0' == buf[*pos])
			mandoc_msg(MANDOCERR_SPACE_EOL, mdoc->parse,
			    line, *pos, NULL);

		return ARGS_WORD;
	}

	p = &buf[*pos];
	*v = mandoc_getarg(mdoc->parse, &p, line, pos);

	/*
	 * After parsing the last word in this phrase,
	 * tell lookup() whether or not to interpret it.
	 */

	if (*p == '\0' && mdoc->flags & MDOC_PHRASEQL) {
		mdoc->flags &= ~MDOC_PHRASEQL;
		mdoc->flags |= MDOC_PHRASEQF;
	}
	return ARGS_WORD;
}

/*
 * Check if the string consists only of space-separated closing
 * delimiters.  This is a bit of a dance: the first must be a close
 * delimiter, but it may be followed by middle delimiters.  Arbitrary
 * whitespace may separate these tokens.
 */
static int
args_checkpunct(const char *buf, int i)
{
	int		 j;
	char		 dbuf[DELIMSZ];
	enum mdelim	 d;

	/* First token must be a close-delimiter. */

	for (j = 0; buf[i] && ' ' != buf[i] && j < DELIMSZ; j++, i++)
		dbuf[j] = buf[i];

	if (DELIMSZ == j)
		return 0;

	dbuf[j] = '\0';
	if (DELIM_CLOSE != mdoc_isdelim(dbuf))
		return 0;

	while (' ' == buf[i])
		i++;

	/* Remaining must NOT be open/none. */

	while (buf[i]) {
		j = 0;
		while (buf[i] && ' ' != buf[i] && j < DELIMSZ)
			dbuf[j++] = buf[i++];

		if (DELIMSZ == j)
			return 0;

		dbuf[j] = '\0';
		d = mdoc_isdelim(dbuf);
		if (DELIM_NONE == d || DELIM_OPEN == d)
			return 0;

		while (' ' == buf[i])
			i++;
	}

	return '\0' == buf[i];
}

static void
argv_multi(struct roff_man *mdoc, int line,
		struct mdoc_argv *v, int *pos, char *buf)
{
	enum margserr	 ac;
	char		*p;

	for (v->sz = 0; ; v->sz++) {
		if (buf[*pos] == '-')
			break;
		ac = args(mdoc, line, pos, buf, ARGSFL_NONE, &p);
		if (ac == ARGS_EOLN)
			break;

		if (v->sz % MULTI_STEP == 0)
			v->value = mandoc_reallocarray(v->value,
			    v->sz + MULTI_STEP, sizeof(char *));

		v->value[(int)v->sz] = mandoc_strdup(p);
	}
}

static void
argv_single(struct roff_man *mdoc, int line,
		struct mdoc_argv *v, int *pos, char *buf)
{
	enum margserr	 ac;
	char		*p;

	ac = args(mdoc, line, pos, buf, ARGSFL_NONE, &p);
	if (ac == ARGS_EOLN)
		return;

	v->sz = 1;
	v->value = mandoc_malloc(sizeof(char *));
	v->value[0] = mandoc_strdup(p);
}
