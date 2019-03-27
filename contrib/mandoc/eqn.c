/*	$Id: eqn.c,v 1.78 2017/07/15 16:26:17 schwarze Exp $ */
/*
 * Copyright (c) 2011, 2014 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014, 2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "libmandoc.h"
#include "libroff.h"

#define	EQN_NEST_MAX	 128 /* maximum nesting of defines */
#define	STRNEQ(p1, sz1, p2, sz2) \
	((sz1) == (sz2) && 0 == strncmp((p1), (p2), (sz1)))

enum	eqn_tok {
	EQN_TOK_DYAD = 0,
	EQN_TOK_VEC,
	EQN_TOK_UNDER,
	EQN_TOK_BAR,
	EQN_TOK_TILDE,
	EQN_TOK_HAT,
	EQN_TOK_DOT,
	EQN_TOK_DOTDOT,
	EQN_TOK_FWD,
	EQN_TOK_BACK,
	EQN_TOK_DOWN,
	EQN_TOK_UP,
	EQN_TOK_FAT,
	EQN_TOK_ROMAN,
	EQN_TOK_ITALIC,
	EQN_TOK_BOLD,
	EQN_TOK_SIZE,
	EQN_TOK_SUB,
	EQN_TOK_SUP,
	EQN_TOK_SQRT,
	EQN_TOK_OVER,
	EQN_TOK_FROM,
	EQN_TOK_TO,
	EQN_TOK_BRACE_OPEN,
	EQN_TOK_BRACE_CLOSE,
	EQN_TOK_GSIZE,
	EQN_TOK_GFONT,
	EQN_TOK_MARK,
	EQN_TOK_LINEUP,
	EQN_TOK_LEFT,
	EQN_TOK_RIGHT,
	EQN_TOK_PILE,
	EQN_TOK_LPILE,
	EQN_TOK_RPILE,
	EQN_TOK_CPILE,
	EQN_TOK_MATRIX,
	EQN_TOK_CCOL,
	EQN_TOK_LCOL,
	EQN_TOK_RCOL,
	EQN_TOK_DELIM,
	EQN_TOK_DEFINE,
	EQN_TOK_TDEFINE,
	EQN_TOK_NDEFINE,
	EQN_TOK_UNDEF,
	EQN_TOK_ABOVE,
	EQN_TOK__MAX,
	EQN_TOK_FUNC,
	EQN_TOK_QUOTED,
	EQN_TOK_SYM,
	EQN_TOK_EOF
};

static	const char *eqn_toks[EQN_TOK__MAX] = {
	"dyad", /* EQN_TOK_DYAD */
	"vec", /* EQN_TOK_VEC */
	"under", /* EQN_TOK_UNDER */
	"bar", /* EQN_TOK_BAR */
	"tilde", /* EQN_TOK_TILDE */
	"hat", /* EQN_TOK_HAT */
	"dot", /* EQN_TOK_DOT */
	"dotdot", /* EQN_TOK_DOTDOT */
	"fwd", /* EQN_TOK_FWD * */
	"back", /* EQN_TOK_BACK */
	"down", /* EQN_TOK_DOWN */
	"up", /* EQN_TOK_UP */
	"fat", /* EQN_TOK_FAT */
	"roman", /* EQN_TOK_ROMAN */
	"italic", /* EQN_TOK_ITALIC */
	"bold", /* EQN_TOK_BOLD */
	"size", /* EQN_TOK_SIZE */
	"sub", /* EQN_TOK_SUB */
	"sup", /* EQN_TOK_SUP */
	"sqrt", /* EQN_TOK_SQRT */
	"over", /* EQN_TOK_OVER */
	"from", /* EQN_TOK_FROM */
	"to", /* EQN_TOK_TO */
	"{", /* EQN_TOK_BRACE_OPEN */
	"}", /* EQN_TOK_BRACE_CLOSE */
	"gsize", /* EQN_TOK_GSIZE */
	"gfont", /* EQN_TOK_GFONT */
	"mark", /* EQN_TOK_MARK */
	"lineup", /* EQN_TOK_LINEUP */
	"left", /* EQN_TOK_LEFT */
	"right", /* EQN_TOK_RIGHT */
	"pile", /* EQN_TOK_PILE */
	"lpile", /* EQN_TOK_LPILE */
	"rpile", /* EQN_TOK_RPILE */
	"cpile", /* EQN_TOK_CPILE */
	"matrix", /* EQN_TOK_MATRIX */
	"ccol", /* EQN_TOK_CCOL */
	"lcol", /* EQN_TOK_LCOL */
	"rcol", /* EQN_TOK_RCOL */
	"delim", /* EQN_TOK_DELIM */
	"define", /* EQN_TOK_DEFINE */
	"tdefine", /* EQN_TOK_TDEFINE */
	"ndefine", /* EQN_TOK_NDEFINE */
	"undef", /* EQN_TOK_UNDEF */
	"above", /* EQN_TOK_ABOVE */
};

static	const char *const eqn_func[] = {
	"acos",	"acsc",	"and",	"arc",	"asec",	"asin", "atan",
	"cos",	"cosh", "coth",	"csc",	"det",	"exp",	"for",
	"if",	"lim",	"ln",	"log",	"max",	"min",
	"sec",	"sin",	"sinh",	"tan",	"tanh",	"Im",	"Re",
};

enum	eqn_symt {
	EQNSYM_alpha = 0,
	EQNSYM_beta,
	EQNSYM_chi,
	EQNSYM_delta,
	EQNSYM_epsilon,
	EQNSYM_eta,
	EQNSYM_gamma,
	EQNSYM_iota,
	EQNSYM_kappa,
	EQNSYM_lambda,
	EQNSYM_mu,
	EQNSYM_nu,
	EQNSYM_omega,
	EQNSYM_omicron,
	EQNSYM_phi,
	EQNSYM_pi,
	EQNSYM_ps,
	EQNSYM_rho,
	EQNSYM_sigma,
	EQNSYM_tau,
	EQNSYM_theta,
	EQNSYM_upsilon,
	EQNSYM_xi,
	EQNSYM_zeta,
	EQNSYM_DELTA,
	EQNSYM_GAMMA,
	EQNSYM_LAMBDA,
	EQNSYM_OMEGA,
	EQNSYM_PHI,
	EQNSYM_PI,
	EQNSYM_PSI,
	EQNSYM_SIGMA,
	EQNSYM_THETA,
	EQNSYM_UPSILON,
	EQNSYM_XI,
	EQNSYM_inter,
	EQNSYM_union,
	EQNSYM_prod,
	EQNSYM_int,
	EQNSYM_sum,
	EQNSYM_grad,
	EQNSYM_del,
	EQNSYM_times,
	EQNSYM_cdot,
	EQNSYM_nothing,
	EQNSYM_approx,
	EQNSYM_prime,
	EQNSYM_half,
	EQNSYM_partial,
	EQNSYM_inf,
	EQNSYM_muchgreat,
	EQNSYM_muchless,
	EQNSYM_larrow,
	EQNSYM_rarrow,
	EQNSYM_pm,
	EQNSYM_nequal,
	EQNSYM_equiv,
	EQNSYM_lessequal,
	EQNSYM_moreequal,
	EQNSYM_minus,
	EQNSYM__MAX
};

struct	eqnsym {
	const char	*str;
	const char	*sym;
};

static	const struct eqnsym eqnsyms[EQNSYM__MAX] = {
	{ "alpha", "*a" }, /* EQNSYM_alpha */
	{ "beta", "*b" }, /* EQNSYM_beta */
	{ "chi", "*x" }, /* EQNSYM_chi */
	{ "delta", "*d" }, /* EQNSYM_delta */
	{ "epsilon", "*e" }, /* EQNSYM_epsilon */
	{ "eta", "*y" }, /* EQNSYM_eta */
	{ "gamma", "*g" }, /* EQNSYM_gamma */
	{ "iota", "*i" }, /* EQNSYM_iota */
	{ "kappa", "*k" }, /* EQNSYM_kappa */
	{ "lambda", "*l" }, /* EQNSYM_lambda */
	{ "mu", "*m" }, /* EQNSYM_mu */
	{ "nu", "*n" }, /* EQNSYM_nu */
	{ "omega", "*w" }, /* EQNSYM_omega */
	{ "omicron", "*o" }, /* EQNSYM_omicron */
	{ "phi", "*f" }, /* EQNSYM_phi */
	{ "pi", "*p" }, /* EQNSYM_pi */
	{ "psi", "*q" }, /* EQNSYM_psi */
	{ "rho", "*r" }, /* EQNSYM_rho */
	{ "sigma", "*s" }, /* EQNSYM_sigma */
	{ "tau", "*t" }, /* EQNSYM_tau */
	{ "theta", "*h" }, /* EQNSYM_theta */
	{ "upsilon", "*u" }, /* EQNSYM_upsilon */
	{ "xi", "*c" }, /* EQNSYM_xi */
	{ "zeta", "*z" }, /* EQNSYM_zeta */
	{ "DELTA", "*D" }, /* EQNSYM_DELTA */
	{ "GAMMA", "*G" }, /* EQNSYM_GAMMA */
	{ "LAMBDA", "*L" }, /* EQNSYM_LAMBDA */
	{ "OMEGA", "*W" }, /* EQNSYM_OMEGA */
	{ "PHI", "*F" }, /* EQNSYM_PHI */
	{ "PI", "*P" }, /* EQNSYM_PI */
	{ "PSI", "*Q" }, /* EQNSYM_PSI */
	{ "SIGMA", "*S" }, /* EQNSYM_SIGMA */
	{ "THETA", "*H" }, /* EQNSYM_THETA */
	{ "UPSILON", "*U" }, /* EQNSYM_UPSILON */
	{ "XI", "*C" }, /* EQNSYM_XI */
	{ "inter", "ca" }, /* EQNSYM_inter */
	{ "union", "cu" }, /* EQNSYM_union */
	{ "prod", "product" }, /* EQNSYM_prod */
	{ "int", "integral" }, /* EQNSYM_int */
	{ "sum", "sum" }, /* EQNSYM_sum */
	{ "grad", "gr" }, /* EQNSYM_grad */
	{ "del", "gr" }, /* EQNSYM_del */
	{ "times", "mu" }, /* EQNSYM_times */
	{ "cdot", "pc" }, /* EQNSYM_cdot */
	{ "nothing", "&" }, /* EQNSYM_nothing */
	{ "approx", "~~" }, /* EQNSYM_approx */
	{ "prime", "fm" }, /* EQNSYM_prime */
	{ "half", "12" }, /* EQNSYM_half */
	{ "partial", "pd" }, /* EQNSYM_partial */
	{ "inf", "if" }, /* EQNSYM_inf */
	{ ">>", ">>" }, /* EQNSYM_muchgreat */
	{ "<<", "<<" }, /* EQNSYM_muchless */
	{ "<-", "<-" }, /* EQNSYM_larrow */
	{ "->", "->" }, /* EQNSYM_rarrow */
	{ "+-", "+-" }, /* EQNSYM_pm */
	{ "!=", "!=" }, /* EQNSYM_nequal */
	{ "==", "==" }, /* EQNSYM_equiv */
	{ "<=", "<=" }, /* EQNSYM_lessequal */
	{ ">=", ">=" }, /* EQNSYM_moreequal */
	{ "-", "mi" }, /* EQNSYM_minus */
};

enum	parse_mode {
	MODE_QUOTED,
	MODE_NOSUB,
	MODE_SUB,
	MODE_TOK
};

static	struct eqn_box	*eqn_box_alloc(struct eqn_node *, struct eqn_box *);
static	struct eqn_box	*eqn_box_makebinary(struct eqn_node *,
				struct eqn_box *);
static	void		 eqn_def(struct eqn_node *);
static	struct eqn_def	*eqn_def_find(struct eqn_node *);
static	void		 eqn_delim(struct eqn_node *);
static	enum eqn_tok	 eqn_next(struct eqn_node *, enum parse_mode);
static	void		 eqn_undef(struct eqn_node *);


struct eqn_node *
eqn_alloc(struct mparse *parse)
{
	struct eqn_node *ep;

	ep = mandoc_calloc(1, sizeof(*ep));
	ep->parse = parse;
	ep->gsize = EQN_DEFSIZE;
	return ep;
}

void
eqn_reset(struct eqn_node *ep)
{
	free(ep->data);
	ep->data = ep->start = ep->end = NULL;
	ep->sz = ep->toksz = 0;
}

void
eqn_read(struct eqn_node *ep, const char *p)
{
	char		*cp;

	if (ep->data == NULL) {
		ep->sz = strlen(p);
		ep->data = mandoc_strdup(p);
	} else {
		ep->sz = mandoc_asprintf(&cp, "%s %s", ep->data, p);
		free(ep->data);
		ep->data = cp;
	}
	ep->sz += 1;
}

/*
 * Find the key "key" of the give size within our eqn-defined values.
 */
static struct eqn_def *
eqn_def_find(struct eqn_node *ep)
{
	int		 i;

	for (i = 0; i < (int)ep->defsz; i++)
		if (ep->defs[i].keysz && STRNEQ(ep->defs[i].key,
		    ep->defs[i].keysz, ep->start, ep->toksz))
			return &ep->defs[i];

	return NULL;
}

/*
 * Parse a token from the input text.  The modes are:
 * MODE_QUOTED: Use *ep->start as the delimiter; the token ends
 *   before its next occurence.  Do not interpret the token in any
 *   way and return EQN_TOK_QUOTED.  All other modes behave like
 *   MODE_QUOTED when *ep->start is '"'.
 * MODE_NOSUB: If *ep->start is a curly brace, the token ends after it;
 *   otherwise, it ends before the next whitespace or brace.
 *   Do not interpret the token and return EQN_TOK__MAX.
 * MODE_SUB: Like MODE_NOSUB, but try to interpret the token as an
 *   alias created with define.  If it is an alias, replace it with
 *   its string value and reparse.
 * MODE_TOK: Like MODE_SUB, but also check the token against the list
 *   of tokens, and if there is a match, return that token.  Otherwise,
 *   if the token matches a symbol, return EQN_TOK_SYM; if it matches
 *   a function name, EQN_TOK_FUNC, or else EQN_TOK__MAX.  Except for
 *   a token match, *ep->start is set to an allocated string that the
 *   caller is expected to free.
 * All modes skip whitespace following the end of the token.
 */
static enum eqn_tok
eqn_next(struct eqn_node *ep, enum parse_mode mode)
{
	static int	 last_len, lim;

	struct eqn_def	*def;
	size_t		 start;
	int		 diff, i, quoted;
	enum eqn_tok	 tok;

	/*
	 * Reset the recursion counter after advancing
	 * beyond the end of the previous substitution.
	 */
	if (ep->end - ep->data >= last_len)
		lim = 0;

	ep->start = ep->end;
	quoted = mode == MODE_QUOTED;
	for (;;) {
		switch (*ep->start) {
		case '\0':
			ep->toksz = 0;
			return EQN_TOK_EOF;
		case '"':
			quoted = 1;
			break;
		default:
			break;
		}
		if (quoted) {
			ep->end = strchr(ep->start + 1, *ep->start);
			ep->start++;  /* Skip opening quote. */
			if (ep->end == NULL) {
				mandoc_msg(MANDOCERR_ARG_QUOTE, ep->parse,
				    ep->node->line, ep->node->pos, NULL);
				ep->end = strchr(ep->start, '\0');
			}
		} else {
			ep->end = ep->start + 1;
			if (*ep->start != '{' && *ep->start != '}')
				ep->end += strcspn(ep->end, " ^~\"{}\t");
		}
		ep->toksz = ep->end - ep->start;
		if (quoted && *ep->end != '\0')
			ep->end++;  /* Skip closing quote. */
		while (*ep->end != '\0' && strchr(" \t^~", *ep->end) != NULL)
			ep->end++;
		if (quoted)  /* Cannot return, may have to strndup. */
			break;
		if (mode == MODE_NOSUB)
			return EQN_TOK__MAX;
		if ((def = eqn_def_find(ep)) == NULL)
			break;
		if (++lim > EQN_NEST_MAX) {
			mandoc_msg(MANDOCERR_ROFFLOOP, ep->parse,
			    ep->node->line, ep->node->pos, NULL);
			return EQN_TOK_EOF;
		}

		/* Replace a defined name with its string value. */
		if ((diff = def->valsz - ep->toksz) > 0) {
			start = ep->start - ep->data;
			ep->sz += diff;
			ep->data = mandoc_realloc(ep->data, ep->sz + 1);
			ep->start = ep->data + start;
		}
		if (diff)
			memmove(ep->start + def->valsz, ep->start + ep->toksz,
			    strlen(ep->start + ep->toksz) + 1);
		memcpy(ep->start, def->val, def->valsz);
		last_len = ep->start - ep->data + def->valsz;
	}
	if (mode != MODE_TOK)
		return quoted ? EQN_TOK_QUOTED : EQN_TOK__MAX;
	if (quoted) {
		ep->start = mandoc_strndup(ep->start, ep->toksz);
		return EQN_TOK_QUOTED;
	}
	for (tok = 0; tok < EQN_TOK__MAX; tok++)
		if (STRNEQ(ep->start, ep->toksz,
		    eqn_toks[tok], strlen(eqn_toks[tok])))
			return tok;

	for (i = 0; i < EQNSYM__MAX; i++) {
		if (STRNEQ(ep->start, ep->toksz,
		    eqnsyms[i].str, strlen(eqnsyms[i].str))) {
			mandoc_asprintf(&ep->start,
			    "\\[%s]", eqnsyms[i].sym);
			return EQN_TOK_SYM;
		}
	}
	ep->start = mandoc_strndup(ep->start, ep->toksz);
	for (i = 0; i < (int)(sizeof(eqn_func)/sizeof(*eqn_func)); i++)
		if (STRNEQ(ep->start, ep->toksz,
		    eqn_func[i], strlen(eqn_func[i])))
			return EQN_TOK_FUNC;
	return EQN_TOK__MAX;
}

void
eqn_box_free(struct eqn_box *bp)
{

	if (bp->first)
		eqn_box_free(bp->first);
	if (bp->next)
		eqn_box_free(bp->next);

	free(bp->text);
	free(bp->left);
	free(bp->right);
	free(bp->top);
	free(bp->bottom);
	free(bp);
}

/*
 * Allocate a box as the last child of the parent node.
 */
static struct eqn_box *
eqn_box_alloc(struct eqn_node *ep, struct eqn_box *parent)
{
	struct eqn_box	*bp;

	bp = mandoc_calloc(1, sizeof(struct eqn_box));
	bp->parent = parent;
	bp->parent->args++;
	bp->expectargs = UINT_MAX;
	bp->font = bp->parent->font;
	bp->size = ep->gsize;

	if (NULL != parent->first) {
		parent->last->next = bp;
		bp->prev = parent->last;
	} else
		parent->first = bp;

	parent->last = bp;
	return bp;
}

/*
 * Reparent the current last node (of the current parent) under a new
 * EQN_SUBEXPR as the first element.
 * Then return the new parent.
 * The new EQN_SUBEXPR will have a two-child limit.
 */
static struct eqn_box *
eqn_box_makebinary(struct eqn_node *ep, struct eqn_box *parent)
{
	struct eqn_box	*b, *newb;

	assert(NULL != parent->last);
	b = parent->last;
	if (parent->last == parent->first)
		parent->first = NULL;
	parent->args--;
	parent->last = b->prev;
	b->prev = NULL;
	newb = eqn_box_alloc(ep, parent);
	newb->type = EQN_SUBEXPR;
	newb->expectargs = 2;
	newb->args = 1;
	newb->first = newb->last = b;
	newb->first->next = NULL;
	b->parent = newb;
	return newb;
}

/*
 * Parse the "delim" control statement.
 */
static void
eqn_delim(struct eqn_node *ep)
{
	if (ep->end[0] == '\0' || ep->end[1] == '\0') {
		mandoc_msg(MANDOCERR_REQ_EMPTY, ep->parse,
		    ep->node->line, ep->node->pos, "delim");
		if (ep->end[0] != '\0')
			ep->end++;
	} else if (strncmp(ep->end, "off", 3) == 0) {
		ep->delim = 0;
		ep->end += 3;
	} else if (strncmp(ep->end, "on", 2) == 0) {
		if (ep->odelim && ep->cdelim)
			ep->delim = 1;
		ep->end += 2;
	} else {
		ep->odelim = *ep->end++;
		ep->cdelim = *ep->end++;
		ep->delim = 1;
	}
}

/*
 * Undefine a previously-defined string.
 */
static void
eqn_undef(struct eqn_node *ep)
{
	struct eqn_def	*def;

	if (eqn_next(ep, MODE_NOSUB) == EQN_TOK_EOF) {
		mandoc_msg(MANDOCERR_REQ_EMPTY, ep->parse,
		    ep->node->line, ep->node->pos, "undef");
		return;
	}
	if ((def = eqn_def_find(ep)) == NULL)
		return;
	free(def->key);
	free(def->val);
	def->key = def->val = NULL;
	def->keysz = def->valsz = 0;
}

static void
eqn_def(struct eqn_node *ep)
{
	struct eqn_def	*def;
	int		 i;

	if (eqn_next(ep, MODE_NOSUB) == EQN_TOK_EOF) {
		mandoc_msg(MANDOCERR_REQ_EMPTY, ep->parse,
		    ep->node->line, ep->node->pos, "define");
		return;
	}

	/*
	 * Search for a key that already exists.
	 * Create a new key if none is found.
	 */
	if ((def = eqn_def_find(ep)) == NULL) {
		/* Find holes in string array. */
		for (i = 0; i < (int)ep->defsz; i++)
			if (0 == ep->defs[i].keysz)
				break;

		if (i == (int)ep->defsz) {
			ep->defsz++;
			ep->defs = mandoc_reallocarray(ep->defs,
			    ep->defsz, sizeof(struct eqn_def));
			ep->defs[i].key = ep->defs[i].val = NULL;
		}

		def = ep->defs + i;
		free(def->key);
		def->key = mandoc_strndup(ep->start, ep->toksz);
		def->keysz = ep->toksz;
	}

	if (eqn_next(ep, MODE_QUOTED) == EQN_TOK_EOF) {
		mandoc_vmsg(MANDOCERR_REQ_EMPTY, ep->parse,
		    ep->node->line, ep->node->pos, "define %s", def->key);
		free(def->key);
		free(def->val);
		def->key = def->val = NULL;
		def->keysz = def->valsz = 0;
		return;
	}
	free(def->val);
	def->val = mandoc_strndup(ep->start, ep->toksz);
	def->valsz = ep->toksz;
}

void
eqn_parse(struct eqn_node *ep)
{
	struct eqn_box	*cur, *nbox, *parent, *split;
	const char	*cp, *cpn;
	char		*p;
	enum eqn_tok	 tok;
	enum { CCL_LET, CCL_DIG, CCL_PUN } ccl, ccln;
	int		 size;

	parent = ep->node->eqn;
	assert(parent != NULL);

	/*
	 * Empty equation.
	 * Do not add it to the high-level syntax tree.
	 */

	if (ep->data == NULL)
		return;

	ep->start = ep->end = ep->data + strspn(ep->data, " ^~");

next_tok:
	tok = eqn_next(ep, MODE_TOK);
	switch (tok) {
	case EQN_TOK_UNDEF:
		eqn_undef(ep);
		break;
	case EQN_TOK_NDEFINE:
	case EQN_TOK_DEFINE:
		eqn_def(ep);
		break;
	case EQN_TOK_TDEFINE:
		if (eqn_next(ep, MODE_NOSUB) == EQN_TOK_EOF ||
		    eqn_next(ep, MODE_QUOTED) == EQN_TOK_EOF)
			mandoc_msg(MANDOCERR_REQ_EMPTY, ep->parse,
			    ep->node->line, ep->node->pos, "tdefine");
		break;
	case EQN_TOK_DELIM:
		eqn_delim(ep);
		break;
	case EQN_TOK_GFONT:
		if (eqn_next(ep, MODE_SUB) == EQN_TOK_EOF)
			mandoc_msg(MANDOCERR_REQ_EMPTY, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
		break;
	case EQN_TOK_MARK:
	case EQN_TOK_LINEUP:
		/* Ignore these. */
		break;
	case EQN_TOK_DYAD:
	case EQN_TOK_VEC:
	case EQN_TOK_UNDER:
	case EQN_TOK_BAR:
	case EQN_TOK_TILDE:
	case EQN_TOK_HAT:
	case EQN_TOK_DOT:
	case EQN_TOK_DOTDOT:
		if (parent->last == NULL) {
			mandoc_msg(MANDOCERR_EQN_NOBOX, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
			cur = eqn_box_alloc(ep, parent);
			cur->type = EQN_TEXT;
			cur->text = mandoc_strdup("");
		}
		parent = eqn_box_makebinary(ep, parent);
		parent->type = EQN_LIST;
		parent->expectargs = 1;
		parent->font = EQNFONT_ROMAN;
		switch (tok) {
		case EQN_TOK_DOTDOT:
			parent->top = mandoc_strdup("\\[ad]");
			break;
		case EQN_TOK_VEC:
			parent->top = mandoc_strdup("\\[->]");
			break;
		case EQN_TOK_DYAD:
			parent->top = mandoc_strdup("\\[<>]");
			break;
		case EQN_TOK_TILDE:
			parent->top = mandoc_strdup("\\[a~]");
			break;
		case EQN_TOK_UNDER:
			parent->bottom = mandoc_strdup("\\[ul]");
			break;
		case EQN_TOK_BAR:
			parent->top = mandoc_strdup("\\[rn]");
			break;
		case EQN_TOK_DOT:
			parent->top = mandoc_strdup("\\[a.]");
			break;
		case EQN_TOK_HAT:
			parent->top = mandoc_strdup("\\[ha]");
			break;
		default:
			abort();
		}
		parent = parent->parent;
		break;
	case EQN_TOK_FWD:
	case EQN_TOK_BACK:
	case EQN_TOK_DOWN:
	case EQN_TOK_UP:
		if (eqn_next(ep, MODE_SUB) == EQN_TOK_EOF)
			mandoc_msg(MANDOCERR_REQ_EMPTY, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
		break;
	case EQN_TOK_FAT:
	case EQN_TOK_ROMAN:
	case EQN_TOK_ITALIC:
	case EQN_TOK_BOLD:
		while (parent->args == parent->expectargs)
			parent = parent->parent;
		/*
		 * These values apply to the next word or sequence of
		 * words; thus, we mark that we'll have a child with
		 * exactly one of those.
		 */
		parent = eqn_box_alloc(ep, parent);
		parent->type = EQN_LIST;
		parent->expectargs = 1;
		switch (tok) {
		case EQN_TOK_FAT:
			parent->font = EQNFONT_FAT;
			break;
		case EQN_TOK_ROMAN:
			parent->font = EQNFONT_ROMAN;
			break;
		case EQN_TOK_ITALIC:
			parent->font = EQNFONT_ITALIC;
			break;
		case EQN_TOK_BOLD:
			parent->font = EQNFONT_BOLD;
			break;
		default:
			abort();
		}
		break;
	case EQN_TOK_SIZE:
	case EQN_TOK_GSIZE:
		/* Accept two values: integral size and a single. */
		if (eqn_next(ep, MODE_SUB) == EQN_TOK_EOF) {
			mandoc_msg(MANDOCERR_REQ_EMPTY, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
			break;
		}
		size = mandoc_strntoi(ep->start, ep->toksz, 10);
		if (-1 == size) {
			mandoc_msg(MANDOCERR_IT_NONUM, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
			break;
		}
		if (EQN_TOK_GSIZE == tok) {
			ep->gsize = size;
			break;
		}
		while (parent->args == parent->expectargs)
			parent = parent->parent;
		parent = eqn_box_alloc(ep, parent);
		parent->type = EQN_LIST;
		parent->expectargs = 1;
		parent->size = size;
		break;
	case EQN_TOK_FROM:
	case EQN_TOK_TO:
	case EQN_TOK_SUB:
	case EQN_TOK_SUP:
		/*
		 * We have a left-right-associative expression.
		 * Repivot under a positional node, open a child scope
		 * and keep on reading.
		 */
		if (parent->last == NULL) {
			mandoc_msg(MANDOCERR_EQN_NOBOX, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
			cur = eqn_box_alloc(ep, parent);
			cur->type = EQN_TEXT;
			cur->text = mandoc_strdup("");
		}
		while (parent->expectargs == 1 && parent->args == 1)
			parent = parent->parent;
		if (tok == EQN_TOK_FROM || tok == EQN_TOK_TO)  {
			for (cur = parent; cur != NULL; cur = cur->parent)
				if (cur->pos == EQNPOS_SUB ||
				    cur->pos == EQNPOS_SUP ||
				    cur->pos == EQNPOS_SUBSUP ||
				    cur->pos == EQNPOS_SQRT ||
				    cur->pos == EQNPOS_OVER)
					break;
			if (cur != NULL)
				parent = cur->parent;
		}
		if (tok == EQN_TOK_SUP && parent->pos == EQNPOS_SUB) {
			parent->expectargs = 3;
			parent->pos = EQNPOS_SUBSUP;
			break;
		}
		if (tok == EQN_TOK_TO && parent->pos == EQNPOS_FROM) {
			parent->expectargs = 3;
			parent->pos = EQNPOS_FROMTO;
			break;
		}
		parent = eqn_box_makebinary(ep, parent);
		switch (tok) {
		case EQN_TOK_FROM:
			parent->pos = EQNPOS_FROM;
			break;
		case EQN_TOK_TO:
			parent->pos = EQNPOS_TO;
			break;
		case EQN_TOK_SUP:
			parent->pos = EQNPOS_SUP;
			break;
		case EQN_TOK_SUB:
			parent->pos = EQNPOS_SUB;
			break;
		default:
			abort();
		}
		break;
	case EQN_TOK_SQRT:
		while (parent->args == parent->expectargs)
			parent = parent->parent;
		/*
		 * Accept a left-right-associative set of arguments just
		 * like sub and sup and friends but without rebalancing
		 * under a pivot.
		 */
		parent = eqn_box_alloc(ep, parent);
		parent->type = EQN_SUBEXPR;
		parent->pos = EQNPOS_SQRT;
		parent->expectargs = 1;
		break;
	case EQN_TOK_OVER:
		/*
		 * We have a right-left-associative fraction.
		 * Close out anything that's currently open, then
		 * rebalance and continue reading.
		 */
		if (parent->last == NULL) {
			mandoc_msg(MANDOCERR_EQN_NOBOX, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
			cur = eqn_box_alloc(ep, parent);
			cur->type = EQN_TEXT;
			cur->text = mandoc_strdup("");
		}
		while (parent->args == parent->expectargs)
			parent = parent->parent;
		while (EQN_SUBEXPR == parent->type)
			parent = parent->parent;
		parent = eqn_box_makebinary(ep, parent);
		parent->pos = EQNPOS_OVER;
		break;
	case EQN_TOK_RIGHT:
	case EQN_TOK_BRACE_CLOSE:
		/*
		 * Close out the existing brace.
		 * FIXME: this is a shitty sentinel: we should really
		 * have a native EQN_BRACE type or whatnot.
		 */
		for (cur = parent; cur != NULL; cur = cur->parent)
			if (cur->type == EQN_LIST &&
			    cur->expectargs > 1 &&
			    (tok == EQN_TOK_BRACE_CLOSE ||
			     cur->left != NULL))
				break;
		if (cur == NULL) {
			mandoc_msg(MANDOCERR_BLK_NOTOPEN, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
			break;
		}
		parent = cur;
		if (EQN_TOK_RIGHT == tok) {
			if (eqn_next(ep, MODE_SUB) == EQN_TOK_EOF) {
				mandoc_msg(MANDOCERR_REQ_EMPTY,
				    ep->parse, ep->node->line,
				    ep->node->pos, eqn_toks[tok]);
				break;
			}
			/* Handling depends on right/left. */
			if (STRNEQ(ep->start, ep->toksz, "ceiling", 7))
				parent->right = mandoc_strdup("\\[rc]");
			else if (STRNEQ(ep->start, ep->toksz, "floor", 5))
				parent->right = mandoc_strdup("\\[rf]");
			else
				parent->right =
				    mandoc_strndup(ep->start, ep->toksz);
		}
		parent = parent->parent;
		if (tok == EQN_TOK_BRACE_CLOSE &&
		    (parent->type == EQN_PILE ||
		     parent->type == EQN_MATRIX))
			parent = parent->parent;
		/* Close out any "singleton" lists. */
		while (parent->type == EQN_LIST &&
		    parent->expectargs == 1 &&
		    parent->args == 1)
			parent = parent->parent;
		break;
	case EQN_TOK_BRACE_OPEN:
	case EQN_TOK_LEFT:
		/*
		 * If we already have something in the stack and we're
		 * in an expression, then rewind til we're not any more
		 * (just like with the text node).
		 */
		while (parent->args == parent->expectargs)
			parent = parent->parent;
		if (EQN_TOK_LEFT == tok &&
		    eqn_next(ep, MODE_SUB) == EQN_TOK_EOF) {
			mandoc_msg(MANDOCERR_REQ_EMPTY, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
			break;
		}
		parent = eqn_box_alloc(ep, parent);
		parent->type = EQN_LIST;
		if (EQN_TOK_LEFT == tok) {
			if (STRNEQ(ep->start, ep->toksz, "ceiling", 7))
				parent->left = mandoc_strdup("\\[lc]");
			else if (STRNEQ(ep->start, ep->toksz, "floor", 5))
				parent->left = mandoc_strdup("\\[lf]");
			else
				parent->left =
				    mandoc_strndup(ep->start, ep->toksz);
		}
		break;
	case EQN_TOK_PILE:
	case EQN_TOK_LPILE:
	case EQN_TOK_RPILE:
	case EQN_TOK_CPILE:
	case EQN_TOK_CCOL:
	case EQN_TOK_LCOL:
	case EQN_TOK_RCOL:
		while (parent->args == parent->expectargs)
			parent = parent->parent;
		parent = eqn_box_alloc(ep, parent);
		parent->type = EQN_PILE;
		parent->expectargs = 1;
		break;
	case EQN_TOK_ABOVE:
		for (cur = parent; cur != NULL; cur = cur->parent)
			if (cur->type == EQN_PILE)
				break;
		if (cur == NULL) {
			mandoc_msg(MANDOCERR_IT_STRAY, ep->parse,
			    ep->node->line, ep->node->pos, eqn_toks[tok]);
			break;
		}
		parent = eqn_box_alloc(ep, cur);
		parent->type = EQN_LIST;
		break;
	case EQN_TOK_MATRIX:
		while (parent->args == parent->expectargs)
			parent = parent->parent;
		parent = eqn_box_alloc(ep, parent);
		parent->type = EQN_MATRIX;
		parent->expectargs = 1;
		break;
	case EQN_TOK_EOF:
		return;
	case EQN_TOK__MAX:
	case EQN_TOK_FUNC:
	case EQN_TOK_QUOTED:
	case EQN_TOK_SYM:
		p = ep->start;
		assert(p != NULL);
		/*
		 * If we already have something in the stack and we're
		 * in an expression, then rewind til we're not any more.
		 */
		while (parent->args == parent->expectargs)
			parent = parent->parent;
		cur = eqn_box_alloc(ep, parent);
		cur->type = EQN_TEXT;
		cur->text = p;
		switch (tok) {
		case EQN_TOK_FUNC:
			cur->font = EQNFONT_ROMAN;
			break;
		case EQN_TOK_QUOTED:
			if (cur->font == EQNFONT_NONE)
				cur->font = EQNFONT_ITALIC;
			break;
		case EQN_TOK_SYM:
			break;
		default:
			if (cur->font != EQNFONT_NONE || *p == '\0')
				break;
			cpn = p - 1;
			ccln = CCL_LET;
			split = NULL;
			for (;;) {
				/* Advance to next character. */
				cp = cpn++;
				ccl = ccln;
				ccln = isalpha((unsigned char)*cpn) ? CCL_LET :
				    isdigit((unsigned char)*cpn) ||
				    (*cpn == '.' && (ccl == CCL_DIG ||
				     isdigit((unsigned char)cpn[1]))) ?
				    CCL_DIG : CCL_PUN;
				/* No boundary before first character. */
				if (cp < p)
					continue;
				cur->font = ccl == CCL_LET ?
				    EQNFONT_ITALIC : EQNFONT_ROMAN;
				if (*cp == '\\')
					mandoc_escape(&cpn, NULL, NULL);
				/* No boundary after last character. */
				if (*cpn == '\0')
					break;
				if (ccln == ccl && *cp != ',' && *cpn != ',')
					continue;
				/* Boundary found, split the text. */
				if (parent->args == parent->expectargs) {
					/* Remove the text from the tree. */
					if (cur->prev == NULL)
						parent->first = cur->next;
					else
						cur->prev->next = NULL;
					parent->last = cur->prev;
					parent->args--;
					/* Set up a list instead. */
					split = eqn_box_alloc(ep, parent);
					split->type = EQN_LIST;
					/* Insert the word into the list. */
					split->first = split->last = cur;
					cur->parent = split;
					cur->prev = NULL;
					parent = split;
				}
				/* Append a new text box. */
				nbox = eqn_box_alloc(ep, parent);
				nbox->type = EQN_TEXT;
				nbox->text = mandoc_strdup(cpn);
				/* Truncate the old box. */
				p = mandoc_strndup(cur->text,
				    cpn - cur->text);
				free(cur->text);
				cur->text = p;
				/* Setup to process the new box. */
				cur = nbox;
				p = nbox->text;
				cpn = p - 1;
				ccln = CCL_LET;
			}
			if (split != NULL)
				parent = split->parent;
			break;
		}
		break;
	default:
		abort();
	}
	goto next_tok;
}

void
eqn_free(struct eqn_node *p)
{
	int		 i;

	for (i = 0; i < (int)p->defsz; i++) {
		free(p->defs[i].key);
		free(p->defs[i].val);
	}

	free(p->data);
	free(p->defs);
	free(p);
}
