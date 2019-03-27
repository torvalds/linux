/*	$Id: mdoc_validate.c,v 1.360 2018/08/01 16:00:58 schwarze Exp $ */
/*
 * Copyright (c) 2008-2012 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2010-2018 Ingo Schwarze <schwarze@openbsd.org>
 * Copyright (c) 2010 Joerg Sonnenberger <joerg@netbsd.org>
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
#ifndef OSNAME
#include <sys/utsname.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "mandoc_xr.h"
#include "roff.h"
#include "mdoc.h"
#include "libmandoc.h"
#include "roff_int.h"
#include "libmdoc.h"

/* FIXME: .Bl -diag can't have non-text children in HEAD. */

#define	POST_ARGS struct roff_man *mdoc

enum	check_ineq {
	CHECK_LT,
	CHECK_GT,
	CHECK_EQ
};

typedef	void	(*v_post)(POST_ARGS);

static	int	 build_list(struct roff_man *, int);
static	void	 check_argv(struct roff_man *,
			struct roff_node *, struct mdoc_argv *);
static	void	 check_args(struct roff_man *, struct roff_node *);
static	void	 check_text(struct roff_man *, int, int, char *);
static	void	 check_text_em(struct roff_man *, int, int, char *);
static	void	 check_toptext(struct roff_man *, int, int, const char *);
static	int	 child_an(const struct roff_node *);
static	size_t		macro2len(enum roff_tok);
static	void	 rewrite_macro2len(struct roff_man *, char **);
static	int	 similar(const char *, const char *);

static	void	 post_an(POST_ARGS);
static	void	 post_an_norm(POST_ARGS);
static	void	 post_at(POST_ARGS);
static	void	 post_bd(POST_ARGS);
static	void	 post_bf(POST_ARGS);
static	void	 post_bk(POST_ARGS);
static	void	 post_bl(POST_ARGS);
static	void	 post_bl_block(POST_ARGS);
static	void	 post_bl_head(POST_ARGS);
static	void	 post_bl_norm(POST_ARGS);
static	void	 post_bx(POST_ARGS);
static	void	 post_defaults(POST_ARGS);
static	void	 post_display(POST_ARGS);
static	void	 post_dd(POST_ARGS);
static	void	 post_delim(POST_ARGS);
static	void	 post_delim_nb(POST_ARGS);
static	void	 post_dt(POST_ARGS);
static	void	 post_en(POST_ARGS);
static	void	 post_es(POST_ARGS);
static	void	 post_eoln(POST_ARGS);
static	void	 post_ex(POST_ARGS);
static	void	 post_fa(POST_ARGS);
static	void	 post_fn(POST_ARGS);
static	void	 post_fname(POST_ARGS);
static	void	 post_fo(POST_ARGS);
static	void	 post_hyph(POST_ARGS);
static	void	 post_ignpar(POST_ARGS);
static	void	 post_it(POST_ARGS);
static	void	 post_lb(POST_ARGS);
static	void	 post_nd(POST_ARGS);
static	void	 post_nm(POST_ARGS);
static	void	 post_ns(POST_ARGS);
static	void	 post_obsolete(POST_ARGS);
static	void	 post_os(POST_ARGS);
static	void	 post_par(POST_ARGS);
static	void	 post_prevpar(POST_ARGS);
static	void	 post_root(POST_ARGS);
static	void	 post_rs(POST_ARGS);
static	void	 post_rv(POST_ARGS);
static	void	 post_sh(POST_ARGS);
static	void	 post_sh_head(POST_ARGS);
static	void	 post_sh_name(POST_ARGS);
static	void	 post_sh_see_also(POST_ARGS);
static	void	 post_sh_authors(POST_ARGS);
static	void	 post_sm(POST_ARGS);
static	void	 post_st(POST_ARGS);
static	void	 post_std(POST_ARGS);
static	void	 post_sx(POST_ARGS);
static	void	 post_useless(POST_ARGS);
static	void	 post_xr(POST_ARGS);
static	void	 post_xx(POST_ARGS);

static	const v_post __mdoc_valids[MDOC_MAX - MDOC_Dd] = {
	post_dd,	/* Dd */
	post_dt,	/* Dt */
	post_os,	/* Os */
	post_sh,	/* Sh */
	post_ignpar,	/* Ss */
	post_par,	/* Pp */
	post_display,	/* D1 */
	post_display,	/* Dl */
	post_display,	/* Bd */
	NULL,		/* Ed */
	post_bl,	/* Bl */
	NULL,		/* El */
	post_it,	/* It */
	post_delim_nb,	/* Ad */
	post_an,	/* An */
	NULL,		/* Ap */
	post_defaults,	/* Ar */
	NULL,		/* Cd */
	post_delim_nb,	/* Cm */
	post_delim_nb,	/* Dv */
	post_delim_nb,	/* Er */
	post_delim_nb,	/* Ev */
	post_ex,	/* Ex */
	post_fa,	/* Fa */
	NULL,		/* Fd */
	post_delim_nb,	/* Fl */
	post_fn,	/* Fn */
	post_delim_nb,	/* Ft */
	post_delim_nb,	/* Ic */
	post_delim_nb,	/* In */
	post_defaults,	/* Li */
	post_nd,	/* Nd */
	post_nm,	/* Nm */
	post_delim_nb,	/* Op */
	post_obsolete,	/* Ot */
	post_defaults,	/* Pa */
	post_rv,	/* Rv */
	post_st,	/* St */
	post_delim_nb,	/* Va */
	post_delim_nb,	/* Vt */
	post_xr,	/* Xr */
	NULL,		/* %A */
	post_hyph,	/* %B */ /* FIXME: can be used outside Rs/Re. */
	NULL,		/* %D */
	NULL,		/* %I */
	NULL,		/* %J */
	post_hyph,	/* %N */
	post_hyph,	/* %O */
	NULL,		/* %P */
	post_hyph,	/* %R */
	post_hyph,	/* %T */ /* FIXME: can be used outside Rs/Re. */
	NULL,		/* %V */
	NULL,		/* Ac */
	NULL,		/* Ao */
	post_delim_nb,	/* Aq */
	post_at,	/* At */
	NULL,		/* Bc */
	post_bf,	/* Bf */
	NULL,		/* Bo */
	NULL,		/* Bq */
	post_xx,	/* Bsx */
	post_bx,	/* Bx */
	post_obsolete,	/* Db */
	NULL,		/* Dc */
	NULL,		/* Do */
	NULL,		/* Dq */
	NULL,		/* Ec */
	NULL,		/* Ef */
	post_delim_nb,	/* Em */
	NULL,		/* Eo */
	post_xx,	/* Fx */
	post_delim_nb,	/* Ms */
	NULL,		/* No */
	post_ns,	/* Ns */
	post_xx,	/* Nx */
	post_xx,	/* Ox */
	NULL,		/* Pc */
	NULL,		/* Pf */
	NULL,		/* Po */
	post_delim_nb,	/* Pq */
	NULL,		/* Qc */
	post_delim_nb,	/* Ql */
	NULL,		/* Qo */
	post_delim_nb,	/* Qq */
	NULL,		/* Re */
	post_rs,	/* Rs */
	NULL,		/* Sc */
	NULL,		/* So */
	post_delim_nb,	/* Sq */
	post_sm,	/* Sm */
	post_sx,	/* Sx */
	post_delim_nb,	/* Sy */
	post_useless,	/* Tn */
	post_xx,	/* Ux */
	NULL,		/* Xc */
	NULL,		/* Xo */
	post_fo,	/* Fo */
	NULL,		/* Fc */
	NULL,		/* Oo */
	NULL,		/* Oc */
	post_bk,	/* Bk */
	NULL,		/* Ek */
	post_eoln,	/* Bt */
	post_obsolete,	/* Hf */
	post_obsolete,	/* Fr */
	post_eoln,	/* Ud */
	post_lb,	/* Lb */
	post_par,	/* Lp */
	post_delim_nb,	/* Lk */
	post_defaults,	/* Mt */
	post_delim_nb,	/* Brq */
	NULL,		/* Bro */
	NULL,		/* Brc */
	NULL,		/* %C */
	post_es,	/* Es */
	post_en,	/* En */
	post_xx,	/* Dx */
	NULL,		/* %Q */
	NULL,		/* %U */
	NULL,		/* Ta */
};
static	const v_post *const mdoc_valids = __mdoc_valids - MDOC_Dd;

#define	RSORD_MAX 14 /* Number of `Rs' blocks. */

static	const enum roff_tok rsord[RSORD_MAX] = {
	MDOC__A,
	MDOC__T,
	MDOC__B,
	MDOC__I,
	MDOC__J,
	MDOC__R,
	MDOC__N,
	MDOC__V,
	MDOC__U,
	MDOC__P,
	MDOC__Q,
	MDOC__C,
	MDOC__D,
	MDOC__O
};

static	const char * const secnames[SEC__MAX] = {
	NULL,
	"NAME",
	"LIBRARY",
	"SYNOPSIS",
	"DESCRIPTION",
	"CONTEXT",
	"IMPLEMENTATION NOTES",
	"RETURN VALUES",
	"ENVIRONMENT",
	"FILES",
	"EXIT STATUS",
	"EXAMPLES",
	"DIAGNOSTICS",
	"COMPATIBILITY",
	"ERRORS",
	"SEE ALSO",
	"STANDARDS",
	"HISTORY",
	"AUTHORS",
	"CAVEATS",
	"BUGS",
	"SECURITY CONSIDERATIONS",
	NULL
};


void
mdoc_node_validate(struct roff_man *mdoc)
{
	struct roff_node *n, *np;
	const v_post *p;

	n = mdoc->last;
	mdoc->last = mdoc->last->child;
	while (mdoc->last != NULL) {
		mdoc_node_validate(mdoc);
		if (mdoc->last == n)
			mdoc->last = mdoc->last->child;
		else
			mdoc->last = mdoc->last->next;
	}

	mdoc->last = n;
	mdoc->next = ROFF_NEXT_SIBLING;
	switch (n->type) {
	case ROFFT_TEXT:
		np = n->parent;
		if (n->sec != SEC_SYNOPSIS ||
		    (np->tok != MDOC_Cd && np->tok != MDOC_Fd))
			check_text(mdoc, n->line, n->pos, n->string);
		if (np->tok != MDOC_Ql && np->tok != MDOC_Dl &&
		    (np->tok != MDOC_Bd ||
		     (mdoc->flags & MDOC_LITERAL) == 0) &&
		    (np->tok != MDOC_It || np->type != ROFFT_HEAD ||
		     np->parent->parent->norm->Bl.type != LIST_diag))
			check_text_em(mdoc, n->line, n->pos, n->string);
		if (np->tok == MDOC_It || (np->type == ROFFT_BODY &&
		    (np->tok == MDOC_Sh || np->tok == MDOC_Ss)))
			check_toptext(mdoc, n->line, n->pos, n->string);
		break;
	case ROFFT_COMMENT:
	case ROFFT_EQN:
	case ROFFT_TBL:
		break;
	case ROFFT_ROOT:
		post_root(mdoc);
		break;
	default:
		check_args(mdoc, mdoc->last);

		/*
		 * Closing delimiters are not special at the
		 * beginning of a block, opening delimiters
		 * are not special at the end.
		 */

		if (n->child != NULL)
			n->child->flags &= ~NODE_DELIMC;
		if (n->last != NULL)
			n->last->flags &= ~NODE_DELIMO;

		/* Call the macro's postprocessor. */

		if (n->tok < ROFF_MAX) {
			switch(n->tok) {
			case ROFF_br:
			case ROFF_sp:
				post_par(mdoc);
				break;
			default:
				roff_validate(mdoc);
				break;
			}
			break;
		}

		assert(n->tok >= MDOC_Dd && n->tok < MDOC_MAX);
		p = mdoc_valids + n->tok;
		if (*p)
			(*p)(mdoc);
		if (mdoc->last == n)
			mdoc_state(mdoc, n);
		break;
	}
}

static void
check_args(struct roff_man *mdoc, struct roff_node *n)
{
	int		 i;

	if (NULL == n->args)
		return;

	assert(n->args->argc);
	for (i = 0; i < (int)n->args->argc; i++)
		check_argv(mdoc, n, &n->args->argv[i]);
}

static void
check_argv(struct roff_man *mdoc, struct roff_node *n, struct mdoc_argv *v)
{
	int		 i;

	for (i = 0; i < (int)v->sz; i++)
		check_text(mdoc, v->line, v->pos, v->value[i]);
}

static void
check_text(struct roff_man *mdoc, int ln, int pos, char *p)
{
	char		*cp;

	if (MDOC_LITERAL & mdoc->flags)
		return;

	for (cp = p; NULL != (p = strchr(p, '\t')); p++)
		mandoc_msg(MANDOCERR_FI_TAB, mdoc->parse,
		    ln, pos + (int)(p - cp), NULL);
}

static void
check_text_em(struct roff_man *mdoc, int ln, int pos, char *p)
{
	const struct roff_node	*np, *nn;
	char			*cp;

	np = mdoc->last->prev;
	nn = mdoc->last->next;

	/* Look for em-dashes wrongly encoded as "--". */

	for (cp = p; *cp != '\0'; cp++) {
		if (cp[0] != '-' || cp[1] != '-')
			continue;
		cp++;

		/* Skip input sequences of more than two '-'. */

		if (cp[1] == '-') {
			while (cp[1] == '-')
				cp++;
			continue;
		}

		/* Skip "--" directly attached to something else. */

		if ((cp - p > 1 && cp[-2] != ' ') ||
		    (cp[1] != '\0' && cp[1] != ' '))
			continue;

		/* Require a letter right before or right afterwards. */

		if ((cp - p > 2 ?
		     isalpha((unsigned char)cp[-3]) :
		     np != NULL &&
		     np->type == ROFFT_TEXT &&
		     *np->string != '\0' &&
		     isalpha((unsigned char)np->string[
		       strlen(np->string) - 1])) ||
		    (cp[1] != '\0' && cp[2] != '\0' ?
		     isalpha((unsigned char)cp[2]) :
		     nn != NULL &&
		     nn->type == ROFFT_TEXT &&
		     isalpha((unsigned char)*nn->string))) {
			mandoc_msg(MANDOCERR_DASHDASH, mdoc->parse,
			    ln, pos + (int)(cp - p) - 1, NULL);
			break;
		}
	}
}

static void
check_toptext(struct roff_man *mdoc, int ln, int pos, const char *p)
{
	const char	*cp, *cpr;

	if (*p == '\0')
		return;

	if ((cp = strstr(p, "OpenBSD")) != NULL)
		mandoc_msg(MANDOCERR_BX, mdoc->parse,
		    ln, pos + (cp - p), "Ox");
	if ((cp = strstr(p, "NetBSD")) != NULL)
		mandoc_msg(MANDOCERR_BX, mdoc->parse,
		    ln, pos + (cp - p), "Nx");
	if ((cp = strstr(p, "FreeBSD")) != NULL)
		mandoc_msg(MANDOCERR_BX, mdoc->parse,
		    ln, pos + (cp - p), "Fx");
	if ((cp = strstr(p, "DragonFly")) != NULL)
		mandoc_msg(MANDOCERR_BX, mdoc->parse,
		    ln, pos + (cp - p), "Dx");

	cp = p;
	while ((cp = strstr(cp + 1, "()")) != NULL) {
		for (cpr = cp - 1; cpr >= p; cpr--)
			if (*cpr != '_' && !isalnum((unsigned char)*cpr))
				break;
		if ((cpr < p || *cpr == ' ') && cpr + 1 < cp) {
			cpr++;
			mandoc_vmsg(MANDOCERR_FUNC, mdoc->parse,
			    ln, pos + (cpr - p),
			    "%.*s()", (int)(cp - cpr), cpr);
		}
	}
}

static void
post_delim(POST_ARGS)
{
	const struct roff_node	*nch;
	const char		*lc;
	enum mdelim		 delim;
	enum roff_tok		 tok;

	tok = mdoc->last->tok;
	nch = mdoc->last->last;
	if (nch == NULL || nch->type != ROFFT_TEXT)
		return;
	lc = strchr(nch->string, '\0') - 1;
	if (lc < nch->string)
		return;
	delim = mdoc_isdelim(lc);
	if (delim == DELIM_NONE || delim == DELIM_OPEN)
		return;
	if (*lc == ')' && (tok == MDOC_Nd || tok == MDOC_Sh ||
	    tok == MDOC_Ss || tok == MDOC_Fo))
		return;

	mandoc_vmsg(MANDOCERR_DELIM, mdoc->parse,
	    nch->line, nch->pos + (lc - nch->string),
	    "%s%s %s", roff_name[tok],
	    nch == mdoc->last->child ? "" : " ...", nch->string);
}

static void
post_delim_nb(POST_ARGS)
{
	const struct roff_node	*nch;
	const char		*lc, *cp;
	int			 nw;
	enum mdelim		 delim;
	enum roff_tok		 tok;

	/*
	 * Find candidates: at least two bytes,
	 * the last one a closing or middle delimiter.
	 */

	tok = mdoc->last->tok;
	nch = mdoc->last->last;
	if (nch == NULL || nch->type != ROFFT_TEXT)
		return;
	lc = strchr(nch->string, '\0') - 1;
	if (lc <= nch->string)
		return;
	delim = mdoc_isdelim(lc);
	if (delim == DELIM_NONE || delim == DELIM_OPEN)
		return;

	/*
	 * Reduce false positives by allowing various cases.
	 */

	/* Escaped delimiters. */
	if (lc > nch->string + 1 && lc[-2] == '\\' &&
	    (lc[-1] == '&' || lc[-1] == 'e'))
		return;

	/* Specific byte sequences. */
	switch (*lc) {
	case ')':
		for (cp = lc; cp >= nch->string; cp--)
			if (*cp == '(')
				return;
		break;
	case '.':
		if (lc > nch->string + 1 && lc[-2] == '.' && lc[-1] == '.')
			return;
		if (lc[-1] == '.')
			return;
		break;
	case ';':
		if (tok == MDOC_Vt)
			return;
		break;
	case '?':
		if (lc[-1] == '?')
			return;
		break;
	case ']':
		for (cp = lc; cp >= nch->string; cp--)
			if (*cp == '[')
				return;
		break;
	case '|':
		if (lc == nch->string + 1 && lc[-1] == '|')
			return;
	default:
		break;
	}

	/* Exactly two non-alphanumeric bytes. */
	if (lc == nch->string + 1 && !isalnum((unsigned char)lc[-1]))
		return;

	/* At least three alphabetic words with a sentence ending. */
	if (strchr("!.:?", *lc) != NULL && (tok == MDOC_Em ||
	    tok == MDOC_Li || tok == MDOC_Pq || tok == MDOC_Sy)) {
		nw = 0;
		for (cp = lc - 1; cp >= nch->string; cp--) {
			if (*cp == ' ') {
				nw++;
				if (cp > nch->string && cp[-1] == ',')
					cp--;
			} else if (isalpha((unsigned int)*cp)) {
				if (nw > 1)
					return;
			} else
				break;
		}
	}

	mandoc_vmsg(MANDOCERR_DELIM_NB, mdoc->parse,
	    nch->line, nch->pos + (lc - nch->string),
	    "%s%s %s", roff_name[tok],
	    nch == mdoc->last->child ? "" : " ...", nch->string);
}

static void
post_bl_norm(POST_ARGS)
{
	struct roff_node *n;
	struct mdoc_argv *argv, *wa;
	int		  i;
	enum mdocargt	  mdoclt;
	enum mdoc_list	  lt;

	n = mdoc->last->parent;
	n->norm->Bl.type = LIST__NONE;

	/*
	 * First figure out which kind of list to use: bind ourselves to
	 * the first mentioned list type and warn about any remaining
	 * ones.  If we find no list type, we default to LIST_item.
	 */

	wa = (n->args == NULL) ? NULL : n->args->argv;
	mdoclt = MDOC_ARG_MAX;
	for (i = 0; n->args && i < (int)n->args->argc; i++) {
		argv = n->args->argv + i;
		lt = LIST__NONE;
		switch (argv->arg) {
		/* Set list types. */
		case MDOC_Bullet:
			lt = LIST_bullet;
			break;
		case MDOC_Dash:
			lt = LIST_dash;
			break;
		case MDOC_Enum:
			lt = LIST_enum;
			break;
		case MDOC_Hyphen:
			lt = LIST_hyphen;
			break;
		case MDOC_Item:
			lt = LIST_item;
			break;
		case MDOC_Tag:
			lt = LIST_tag;
			break;
		case MDOC_Diag:
			lt = LIST_diag;
			break;
		case MDOC_Hang:
			lt = LIST_hang;
			break;
		case MDOC_Ohang:
			lt = LIST_ohang;
			break;
		case MDOC_Inset:
			lt = LIST_inset;
			break;
		case MDOC_Column:
			lt = LIST_column;
			break;
		/* Set list arguments. */
		case MDOC_Compact:
			if (n->norm->Bl.comp)
				mandoc_msg(MANDOCERR_ARG_REP,
				    mdoc->parse, argv->line,
				    argv->pos, "Bl -compact");
			n->norm->Bl.comp = 1;
			break;
		case MDOC_Width:
			wa = argv;
			if (0 == argv->sz) {
				mandoc_msg(MANDOCERR_ARG_EMPTY,
				    mdoc->parse, argv->line,
				    argv->pos, "Bl -width");
				n->norm->Bl.width = "0n";
				break;
			}
			if (NULL != n->norm->Bl.width)
				mandoc_vmsg(MANDOCERR_ARG_REP,
				    mdoc->parse, argv->line,
				    argv->pos, "Bl -width %s",
				    argv->value[0]);
			rewrite_macro2len(mdoc, argv->value);
			n->norm->Bl.width = argv->value[0];
			break;
		case MDOC_Offset:
			if (0 == argv->sz) {
				mandoc_msg(MANDOCERR_ARG_EMPTY,
				    mdoc->parse, argv->line,
				    argv->pos, "Bl -offset");
				break;
			}
			if (NULL != n->norm->Bl.offs)
				mandoc_vmsg(MANDOCERR_ARG_REP,
				    mdoc->parse, argv->line,
				    argv->pos, "Bl -offset %s",
				    argv->value[0]);
			rewrite_macro2len(mdoc, argv->value);
			n->norm->Bl.offs = argv->value[0];
			break;
		default:
			continue;
		}
		if (LIST__NONE == lt)
			continue;
		mdoclt = argv->arg;

		/* Check: multiple list types. */

		if (LIST__NONE != n->norm->Bl.type) {
			mandoc_vmsg(MANDOCERR_BL_REP,
			    mdoc->parse, n->line, n->pos,
			    "Bl -%s", mdoc_argnames[argv->arg]);
			continue;
		}

		/* The list type should come first. */

		if (n->norm->Bl.width ||
		    n->norm->Bl.offs ||
		    n->norm->Bl.comp)
			mandoc_vmsg(MANDOCERR_BL_LATETYPE,
			    mdoc->parse, n->line, n->pos, "Bl -%s",
			    mdoc_argnames[n->args->argv[0].arg]);

		n->norm->Bl.type = lt;
		if (LIST_column == lt) {
			n->norm->Bl.ncols = argv->sz;
			n->norm->Bl.cols = (void *)argv->value;
		}
	}

	/* Allow lists to default to LIST_item. */

	if (LIST__NONE == n->norm->Bl.type) {
		mandoc_msg(MANDOCERR_BL_NOTYPE, mdoc->parse,
		    n->line, n->pos, "Bl");
		n->norm->Bl.type = LIST_item;
		mdoclt = MDOC_Item;
	}

	/*
	 * Validate the width field.  Some list types don't need width
	 * types and should be warned about them.  Others should have it
	 * and must also be warned.  Yet others have a default and need
	 * no warning.
	 */

	switch (n->norm->Bl.type) {
	case LIST_tag:
		if (n->norm->Bl.width == NULL)
			mandoc_msg(MANDOCERR_BL_NOWIDTH, mdoc->parse,
			    n->line, n->pos, "Bl -tag");
		break;
	case LIST_column:
	case LIST_diag:
	case LIST_ohang:
	case LIST_inset:
	case LIST_item:
		if (n->norm->Bl.width != NULL)
			mandoc_vmsg(MANDOCERR_BL_SKIPW, mdoc->parse,
			    wa->line, wa->pos, "Bl -%s",
			    mdoc_argnames[mdoclt]);
		n->norm->Bl.width = NULL;
		break;
	case LIST_bullet:
	case LIST_dash:
	case LIST_hyphen:
		if (n->norm->Bl.width == NULL)
			n->norm->Bl.width = "2n";
		break;
	case LIST_enum:
		if (n->norm->Bl.width == NULL)
			n->norm->Bl.width = "3n";
		break;
	default:
		break;
	}
}

static void
post_bd(POST_ARGS)
{
	struct roff_node *n;
	struct mdoc_argv *argv;
	int		  i;
	enum mdoc_disp	  dt;

	n = mdoc->last;
	for (i = 0; n->args && i < (int)n->args->argc; i++) {
		argv = n->args->argv + i;
		dt = DISP__NONE;

		switch (argv->arg) {
		case MDOC_Centred:
			dt = DISP_centered;
			break;
		case MDOC_Ragged:
			dt = DISP_ragged;
			break;
		case MDOC_Unfilled:
			dt = DISP_unfilled;
			break;
		case MDOC_Filled:
			dt = DISP_filled;
			break;
		case MDOC_Literal:
			dt = DISP_literal;
			break;
		case MDOC_File:
			mandoc_msg(MANDOCERR_BD_FILE, mdoc->parse,
			    n->line, n->pos, NULL);
			break;
		case MDOC_Offset:
			if (0 == argv->sz) {
				mandoc_msg(MANDOCERR_ARG_EMPTY,
				    mdoc->parse, argv->line,
				    argv->pos, "Bd -offset");
				break;
			}
			if (NULL != n->norm->Bd.offs)
				mandoc_vmsg(MANDOCERR_ARG_REP,
				    mdoc->parse, argv->line,
				    argv->pos, "Bd -offset %s",
				    argv->value[0]);
			rewrite_macro2len(mdoc, argv->value);
			n->norm->Bd.offs = argv->value[0];
			break;
		case MDOC_Compact:
			if (n->norm->Bd.comp)
				mandoc_msg(MANDOCERR_ARG_REP,
				    mdoc->parse, argv->line,
				    argv->pos, "Bd -compact");
			n->norm->Bd.comp = 1;
			break;
		default:
			abort();
		}
		if (DISP__NONE == dt)
			continue;

		if (DISP__NONE == n->norm->Bd.type)
			n->norm->Bd.type = dt;
		else
			mandoc_vmsg(MANDOCERR_BD_REP,
			    mdoc->parse, n->line, n->pos,
			    "Bd -%s", mdoc_argnames[argv->arg]);
	}

	if (DISP__NONE == n->norm->Bd.type) {
		mandoc_msg(MANDOCERR_BD_NOTYPE, mdoc->parse,
		    n->line, n->pos, "Bd");
		n->norm->Bd.type = DISP_ragged;
	}
}

/*
 * Stand-alone line macros.
 */

static void
post_an_norm(POST_ARGS)
{
	struct roff_node *n;
	struct mdoc_argv *argv;
	size_t	 i;

	n = mdoc->last;
	if (n->args == NULL)
		return;

	for (i = 1; i < n->args->argc; i++) {
		argv = n->args->argv + i;
		mandoc_vmsg(MANDOCERR_AN_REP,
		    mdoc->parse, argv->line, argv->pos,
		    "An -%s", mdoc_argnames[argv->arg]);
	}

	argv = n->args->argv;
	if (argv->arg == MDOC_Split)
		n->norm->An.auth = AUTH_split;
	else if (argv->arg == MDOC_Nosplit)
		n->norm->An.auth = AUTH_nosplit;
	else
		abort();
}

static void
post_eoln(POST_ARGS)
{
	struct roff_node	*n;

	post_useless(mdoc);
	n = mdoc->last;
	if (n->child != NULL)
		mandoc_vmsg(MANDOCERR_ARG_SKIP, mdoc->parse, n->line,
		    n->pos, "%s %s", roff_name[n->tok], n->child->string);

	while (n->child != NULL)
		roff_node_delete(mdoc, n->child);

	roff_word_alloc(mdoc, n->line, n->pos, n->tok == MDOC_Bt ?
	    "is currently in beta test." : "currently under development.");
	mdoc->last->flags |= NODE_EOS | NODE_NOSRC;
	mdoc->last = n;
}

static int
build_list(struct roff_man *mdoc, int tok)
{
	struct roff_node	*n;
	int			 ic;

	n = mdoc->last->next;
	for (ic = 1;; ic++) {
		roff_elem_alloc(mdoc, n->line, n->pos, tok);
		mdoc->last->flags |= NODE_NOSRC;
		mdoc_node_relink(mdoc, n);
		n = mdoc->last = mdoc->last->parent;
		mdoc->next = ROFF_NEXT_SIBLING;
		if (n->next == NULL)
			return ic;
		if (ic > 1 || n->next->next != NULL) {
			roff_word_alloc(mdoc, n->line, n->pos, ",");
			mdoc->last->flags |= NODE_DELIMC | NODE_NOSRC;
		}
		n = mdoc->last->next;
		if (n->next == NULL) {
			roff_word_alloc(mdoc, n->line, n->pos, "and");
			mdoc->last->flags |= NODE_NOSRC;
		}
	}
}

static void
post_ex(POST_ARGS)
{
	struct roff_node	*n;
	int			 ic;

	post_std(mdoc);

	n = mdoc->last;
	mdoc->next = ROFF_NEXT_CHILD;
	roff_word_alloc(mdoc, n->line, n->pos, "The");
	mdoc->last->flags |= NODE_NOSRC;

	if (mdoc->last->next != NULL)
		ic = build_list(mdoc, MDOC_Nm);
	else if (mdoc->meta.name != NULL) {
		roff_elem_alloc(mdoc, n->line, n->pos, MDOC_Nm);
		mdoc->last->flags |= NODE_NOSRC;
		roff_word_alloc(mdoc, n->line, n->pos, mdoc->meta.name);
		mdoc->last->flags |= NODE_NOSRC;
		mdoc->last = mdoc->last->parent;
		mdoc->next = ROFF_NEXT_SIBLING;
		ic = 1;
	} else {
		mandoc_msg(MANDOCERR_EX_NONAME, mdoc->parse,
		    n->line, n->pos, "Ex");
		ic = 0;
	}

	roff_word_alloc(mdoc, n->line, n->pos,
	    ic > 1 ? "utilities exit\\~0" : "utility exits\\~0");
	mdoc->last->flags |= NODE_NOSRC;
	roff_word_alloc(mdoc, n->line, n->pos,
	    "on success, and\\~>0 if an error occurs.");
	mdoc->last->flags |= NODE_EOS | NODE_NOSRC;
	mdoc->last = n;
}

static void
post_lb(POST_ARGS)
{
	struct roff_node	*n;
	const char		*p;

	post_delim_nb(mdoc);

	n = mdoc->last;
	assert(n->child->type == ROFFT_TEXT);
	mdoc->next = ROFF_NEXT_CHILD;

	if ((p = mdoc_a2lib(n->child->string)) != NULL) {
		n->child->flags |= NODE_NOPRT;
		roff_word_alloc(mdoc, n->line, n->pos, p);
		mdoc->last->flags = NODE_NOSRC;
		mdoc->last = n;
		return;
	}

	mandoc_vmsg(MANDOCERR_LB_BAD, mdoc->parse, n->child->line,
	    n->child->pos, "Lb %s", n->child->string);

	roff_word_alloc(mdoc, n->line, n->pos, "library");
	mdoc->last->flags = NODE_NOSRC;
	roff_word_alloc(mdoc, n->line, n->pos, "\\(lq");
	mdoc->last->flags = NODE_DELIMO | NODE_NOSRC;
	mdoc->last = mdoc->last->next;
	roff_word_alloc(mdoc, n->line, n->pos, "\\(rq");
	mdoc->last->flags = NODE_DELIMC | NODE_NOSRC;
	mdoc->last = n;
}

static void
post_rv(POST_ARGS)
{
	struct roff_node	*n;
	int			 ic;

	post_std(mdoc);

	n = mdoc->last;
	mdoc->next = ROFF_NEXT_CHILD;
	if (n->child != NULL) {
		roff_word_alloc(mdoc, n->line, n->pos, "The");
		mdoc->last->flags |= NODE_NOSRC;
		ic = build_list(mdoc, MDOC_Fn);
		roff_word_alloc(mdoc, n->line, n->pos,
		    ic > 1 ? "functions return" : "function returns");
		mdoc->last->flags |= NODE_NOSRC;
		roff_word_alloc(mdoc, n->line, n->pos,
		    "the value\\~0 if successful;");
	} else
		roff_word_alloc(mdoc, n->line, n->pos, "Upon successful "
		    "completion, the value\\~0 is returned;");
	mdoc->last->flags |= NODE_NOSRC;

	roff_word_alloc(mdoc, n->line, n->pos, "otherwise "
	    "the value\\~\\-1 is returned and the global variable");
	mdoc->last->flags |= NODE_NOSRC;
	roff_elem_alloc(mdoc, n->line, n->pos, MDOC_Va);
	mdoc->last->flags |= NODE_NOSRC;
	roff_word_alloc(mdoc, n->line, n->pos, "errno");
	mdoc->last->flags |= NODE_NOSRC;
	mdoc->last = mdoc->last->parent;
	mdoc->next = ROFF_NEXT_SIBLING;
	roff_word_alloc(mdoc, n->line, n->pos,
	    "is set to indicate the error.");
	mdoc->last->flags |= NODE_EOS | NODE_NOSRC;
	mdoc->last = n;
}

static void
post_std(POST_ARGS)
{
	struct roff_node *n;

	post_delim(mdoc);

	n = mdoc->last;
	if (n->args && n->args->argc == 1)
		if (n->args->argv[0].arg == MDOC_Std)
			return;

	mandoc_msg(MANDOCERR_ARG_STD, mdoc->parse,
	    n->line, n->pos, roff_name[n->tok]);
}

static void
post_st(POST_ARGS)
{
	struct roff_node	 *n, *nch;
	const char		 *p;

	n = mdoc->last;
	nch = n->child;
	assert(nch->type == ROFFT_TEXT);

	if ((p = mdoc_a2st(nch->string)) == NULL) {
		mandoc_vmsg(MANDOCERR_ST_BAD, mdoc->parse,
		    nch->line, nch->pos, "St %s", nch->string);
		roff_node_delete(mdoc, n);
		return;
	}

	nch->flags |= NODE_NOPRT;
	mdoc->next = ROFF_NEXT_CHILD;
	roff_word_alloc(mdoc, nch->line, nch->pos, p);
	mdoc->last->flags |= NODE_NOSRC;
	mdoc->last= n;
}

static void
post_obsolete(POST_ARGS)
{
	struct roff_node *n;

	n = mdoc->last;
	if (n->type == ROFFT_ELEM || n->type == ROFFT_BLOCK)
		mandoc_msg(MANDOCERR_MACRO_OBS, mdoc->parse,
		    n->line, n->pos, roff_name[n->tok]);
}

static void
post_useless(POST_ARGS)
{
	struct roff_node *n;

	n = mdoc->last;
	mandoc_msg(MANDOCERR_MACRO_USELESS, mdoc->parse,
	    n->line, n->pos, roff_name[n->tok]);
}

/*
 * Block macros.
 */

static void
post_bf(POST_ARGS)
{
	struct roff_node *np, *nch;

	/*
	 * Unlike other data pointers, these are "housed" by the HEAD
	 * element, which contains the goods.
	 */

	np = mdoc->last;
	if (np->type != ROFFT_HEAD)
		return;

	assert(np->parent->type == ROFFT_BLOCK);
	assert(np->parent->tok == MDOC_Bf);

	/* Check the number of arguments. */

	nch = np->child;
	if (np->parent->args == NULL) {
		if (nch == NULL) {
			mandoc_msg(MANDOCERR_BF_NOFONT, mdoc->parse,
			    np->line, np->pos, "Bf");
			return;
		}
		nch = nch->next;
	}
	if (nch != NULL)
		mandoc_vmsg(MANDOCERR_ARG_EXCESS, mdoc->parse,
		    nch->line, nch->pos, "Bf ... %s", nch->string);

	/* Extract argument into data. */

	if (np->parent->args != NULL) {
		switch (np->parent->args->argv[0].arg) {
		case MDOC_Emphasis:
			np->norm->Bf.font = FONT_Em;
			break;
		case MDOC_Literal:
			np->norm->Bf.font = FONT_Li;
			break;
		case MDOC_Symbolic:
			np->norm->Bf.font = FONT_Sy;
			break;
		default:
			abort();
		}
		return;
	}

	/* Extract parameter into data. */

	if ( ! strcmp(np->child->string, "Em"))
		np->norm->Bf.font = FONT_Em;
	else if ( ! strcmp(np->child->string, "Li"))
		np->norm->Bf.font = FONT_Li;
	else if ( ! strcmp(np->child->string, "Sy"))
		np->norm->Bf.font = FONT_Sy;
	else
		mandoc_vmsg(MANDOCERR_BF_BADFONT, mdoc->parse,
		    np->child->line, np->child->pos,
		    "Bf %s", np->child->string);
}

static void
post_fname(POST_ARGS)
{
	const struct roff_node	*n;
	const char		*cp;
	size_t			 pos;

	n = mdoc->last->child;
	pos = strcspn(n->string, "()");
	cp = n->string + pos;
	if ( ! (cp[0] == '\0' || (cp[0] == '(' && cp[1] == '*')))
		mandoc_msg(MANDOCERR_FN_PAREN, mdoc->parse,
		    n->line, n->pos + pos, n->string);
}

static void
post_fn(POST_ARGS)
{

	post_fname(mdoc);
	post_fa(mdoc);
}

static void
post_fo(POST_ARGS)
{
	const struct roff_node	*n;

	n = mdoc->last;

	if (n->type != ROFFT_HEAD)
		return;

	if (n->child == NULL) {
		mandoc_msg(MANDOCERR_FO_NOHEAD, mdoc->parse,
		    n->line, n->pos, "Fo");
		return;
	}
	if (n->child != n->last) {
		mandoc_vmsg(MANDOCERR_ARG_EXCESS, mdoc->parse,
		    n->child->next->line, n->child->next->pos,
		    "Fo ... %s", n->child->next->string);
		while (n->child != n->last)
			roff_node_delete(mdoc, n->last);
	} else
		post_delim(mdoc);

	post_fname(mdoc);
}

static void
post_fa(POST_ARGS)
{
	const struct roff_node *n;
	const char *cp;

	for (n = mdoc->last->child; n != NULL; n = n->next) {
		for (cp = n->string; *cp != '\0'; cp++) {
			/* Ignore callbacks and alterations. */
			if (*cp == '(' || *cp == '{')
				break;
			if (*cp != ',')
				continue;
			mandoc_msg(MANDOCERR_FA_COMMA, mdoc->parse,
			    n->line, n->pos + (cp - n->string),
			    n->string);
			break;
		}
	}
	post_delim_nb(mdoc);
}

static void
post_nm(POST_ARGS)
{
	struct roff_node	*n;

	n = mdoc->last;

	if (n->sec == SEC_NAME && n->child != NULL &&
	    n->child->type == ROFFT_TEXT && mdoc->meta.msec != NULL)
		mandoc_xr_add(mdoc->meta.msec, n->child->string, -1, -1);

	if (n->last != NULL &&
	    (n->last->tok == MDOC_Pp ||
	     n->last->tok == MDOC_Lp))
		mdoc_node_relink(mdoc, n->last);

	if (mdoc->meta.name == NULL)
		deroff(&mdoc->meta.name, n);

	if (mdoc->meta.name == NULL ||
	    (mdoc->lastsec == SEC_NAME && n->child == NULL))
		mandoc_msg(MANDOCERR_NM_NONAME, mdoc->parse,
		    n->line, n->pos, "Nm");

	switch (n->type) {
	case ROFFT_ELEM:
		post_delim_nb(mdoc);
		break;
	case ROFFT_HEAD:
		post_delim(mdoc);
		break;
	default:
		return;
	}

	if ((n->child != NULL && n->child->type == ROFFT_TEXT) ||
	    mdoc->meta.name == NULL)
		return;

	mdoc->next = ROFF_NEXT_CHILD;
	roff_word_alloc(mdoc, n->line, n->pos, mdoc->meta.name);
	mdoc->last->flags |= NODE_NOSRC;
	mdoc->last = n;
}

static void
post_nd(POST_ARGS)
{
	struct roff_node	*n;

	n = mdoc->last;

	if (n->type != ROFFT_BODY)
		return;

	if (n->sec != SEC_NAME)
		mandoc_msg(MANDOCERR_ND_LATE, mdoc->parse,
		    n->line, n->pos, "Nd");

	if (n->child == NULL)
		mandoc_msg(MANDOCERR_ND_EMPTY, mdoc->parse,
		    n->line, n->pos, "Nd");
	else
		post_delim(mdoc);

	post_hyph(mdoc);
}

static void
post_display(POST_ARGS)
{
	struct roff_node *n, *np;

	n = mdoc->last;
	switch (n->type) {
	case ROFFT_BODY:
		if (n->end != ENDBODY_NOT) {
			if (n->tok == MDOC_Bd &&
			    n->body->parent->args == NULL)
				roff_node_delete(mdoc, n);
		} else if (n->child == NULL)
			mandoc_msg(MANDOCERR_BLK_EMPTY, mdoc->parse,
			    n->line, n->pos, roff_name[n->tok]);
		else if (n->tok == MDOC_D1)
			post_hyph(mdoc);
		break;
	case ROFFT_BLOCK:
		if (n->tok == MDOC_Bd) {
			if (n->args == NULL) {
				mandoc_msg(MANDOCERR_BD_NOARG,
				    mdoc->parse, n->line, n->pos, "Bd");
				mdoc->next = ROFF_NEXT_SIBLING;
				while (n->body->child != NULL)
					mdoc_node_relink(mdoc,
					    n->body->child);
				roff_node_delete(mdoc, n);
				break;
			}
			post_bd(mdoc);
			post_prevpar(mdoc);
		}
		for (np = n->parent; np != NULL; np = np->parent) {
			if (np->type == ROFFT_BLOCK && np->tok == MDOC_Bd) {
				mandoc_vmsg(MANDOCERR_BD_NEST,
				    mdoc->parse, n->line, n->pos,
				    "%s in Bd", roff_name[n->tok]);
				break;
			}
		}
		break;
	default:
		break;
	}
}

static void
post_defaults(POST_ARGS)
{
	struct roff_node *nn;

	if (mdoc->last->child != NULL) {
		post_delim_nb(mdoc);
		return;
	}

	/*
	 * The `Ar' defaults to "file ..." if no value is provided as an
	 * argument; the `Mt' and `Pa' macros use "~"; the `Li' just
	 * gets an empty string.
	 */

	nn = mdoc->last;
	switch (nn->tok) {
	case MDOC_Ar:
		mdoc->next = ROFF_NEXT_CHILD;
		roff_word_alloc(mdoc, nn->line, nn->pos, "file");
		mdoc->last->flags |= NODE_NOSRC;
		roff_word_alloc(mdoc, nn->line, nn->pos, "...");
		mdoc->last->flags |= NODE_NOSRC;
		break;
	case MDOC_Pa:
	case MDOC_Mt:
		mdoc->next = ROFF_NEXT_CHILD;
		roff_word_alloc(mdoc, nn->line, nn->pos, "~");
		mdoc->last->flags |= NODE_NOSRC;
		break;
	default:
		abort();
	}
	mdoc->last = nn;
}

static void
post_at(POST_ARGS)
{
	struct roff_node	*n, *nch;
	const char		*att;

	n = mdoc->last;
	nch = n->child;

	/*
	 * If we have a child, look it up in the standard keys.  If a
	 * key exist, use that instead of the child; if it doesn't,
	 * prefix "AT&T UNIX " to the existing data.
	 */

	att = NULL;
	if (nch != NULL && ((att = mdoc_a2att(nch->string)) == NULL))
		mandoc_vmsg(MANDOCERR_AT_BAD, mdoc->parse,
		    nch->line, nch->pos, "At %s", nch->string);

	mdoc->next = ROFF_NEXT_CHILD;
	if (att != NULL) {
		roff_word_alloc(mdoc, nch->line, nch->pos, att);
		nch->flags |= NODE_NOPRT;
	} else
		roff_word_alloc(mdoc, n->line, n->pos, "AT&T UNIX");
	mdoc->last->flags |= NODE_NOSRC;
	mdoc->last = n;
}

static void
post_an(POST_ARGS)
{
	struct roff_node *np, *nch;

	post_an_norm(mdoc);

	np = mdoc->last;
	nch = np->child;
	if (np->norm->An.auth == AUTH__NONE) {
		if (nch == NULL)
			mandoc_msg(MANDOCERR_MACRO_EMPTY, mdoc->parse,
			    np->line, np->pos, "An");
		else
			post_delim_nb(mdoc);
	} else if (nch != NULL)
		mandoc_vmsg(MANDOCERR_ARG_EXCESS, mdoc->parse,
		    nch->line, nch->pos, "An ... %s", nch->string);
}

static void
post_en(POST_ARGS)
{

	post_obsolete(mdoc);
	if (mdoc->last->type == ROFFT_BLOCK)
		mdoc->last->norm->Es = mdoc->last_es;
}

static void
post_es(POST_ARGS)
{

	post_obsolete(mdoc);
	mdoc->last_es = mdoc->last;
}

static void
post_xx(POST_ARGS)
{
	struct roff_node	*n;
	const char		*os;
	char			*v;

	post_delim_nb(mdoc);

	n = mdoc->last;
	switch (n->tok) {
	case MDOC_Bsx:
		os = "BSD/OS";
		break;
	case MDOC_Dx:
		os = "DragonFly";
		break;
	case MDOC_Fx:
		os = "FreeBSD";
		break;
	case MDOC_Nx:
		os = "NetBSD";
		if (n->child == NULL)
			break;
		v = n->child->string;
		if ((v[0] != '0' && v[0] != '1') || v[1] != '.' ||
		    v[2] < '0' || v[2] > '9' ||
		    v[3] < 'a' || v[3] > 'z' || v[4] != '\0')
			break;
		n->child->flags |= NODE_NOPRT;
		mdoc->next = ROFF_NEXT_CHILD;
		roff_word_alloc(mdoc, n->child->line, n->child->pos, v);
		v = mdoc->last->string;
		v[3] = toupper((unsigned char)v[3]);
		mdoc->last->flags |= NODE_NOSRC;
		mdoc->last = n;
		break;
	case MDOC_Ox:
		os = "OpenBSD";
		break;
	case MDOC_Ux:
		os = "UNIX";
		break;
	default:
		abort();
	}
	mdoc->next = ROFF_NEXT_CHILD;
	roff_word_alloc(mdoc, n->line, n->pos, os);
	mdoc->last->flags |= NODE_NOSRC;
	mdoc->last = n;
}

static void
post_it(POST_ARGS)
{
	struct roff_node *nbl, *nit, *nch;
	int		  i, cols;
	enum mdoc_list	  lt;

	post_prevpar(mdoc);

	nit = mdoc->last;
	if (nit->type != ROFFT_BLOCK)
		return;

	nbl = nit->parent->parent;
	lt = nbl->norm->Bl.type;

	switch (lt) {
	case LIST_tag:
	case LIST_hang:
	case LIST_ohang:
	case LIST_inset:
	case LIST_diag:
		if (nit->head->child == NULL)
			mandoc_vmsg(MANDOCERR_IT_NOHEAD,
			    mdoc->parse, nit->line, nit->pos,
			    "Bl -%s It",
			    mdoc_argnames[nbl->args->argv[0].arg]);
		break;
	case LIST_bullet:
	case LIST_dash:
	case LIST_enum:
	case LIST_hyphen:
		if (nit->body == NULL || nit->body->child == NULL)
			mandoc_vmsg(MANDOCERR_IT_NOBODY,
			    mdoc->parse, nit->line, nit->pos,
			    "Bl -%s It",
			    mdoc_argnames[nbl->args->argv[0].arg]);
		/* FALLTHROUGH */
	case LIST_item:
		if ((nch = nit->head->child) != NULL)
			mandoc_vmsg(MANDOCERR_ARG_SKIP, mdoc->parse,
			    nit->line, nit->pos, "It %s",
			    nch->string == NULL ? roff_name[nch->tok] :
			    nch->string);
		break;
	case LIST_column:
		cols = (int)nbl->norm->Bl.ncols;

		assert(nit->head->child == NULL);

		if (nit->head->next->child == NULL &&
		    nit->head->next->next == NULL) {
			mandoc_msg(MANDOCERR_MACRO_EMPTY, mdoc->parse,
			    nit->line, nit->pos, "It");
			roff_node_delete(mdoc, nit);
			break;
		}

		i = 0;
		for (nch = nit->child; nch != NULL; nch = nch->next) {
			if (nch->type != ROFFT_BODY)
				continue;
			if (i++ && nch->flags & NODE_LINE)
				mandoc_msg(MANDOCERR_TA_LINE, mdoc->parse,
				    nch->line, nch->pos, "Ta");
		}
		if (i < cols || i > cols + 1)
			mandoc_vmsg(MANDOCERR_BL_COL,
			    mdoc->parse, nit->line, nit->pos,
			    "%d columns, %d cells", cols, i);
		else if (nit->head->next->child != NULL &&
		    nit->head->next->child->line > nit->line)
			mandoc_msg(MANDOCERR_IT_NOARG, mdoc->parse,
			    nit->line, nit->pos, "Bl -column It");
		break;
	default:
		abort();
	}
}

static void
post_bl_block(POST_ARGS)
{
	struct roff_node *n, *ni, *nc;

	post_prevpar(mdoc);

	n = mdoc->last;
	for (ni = n->body->child; ni != NULL; ni = ni->next) {
		if (ni->body == NULL)
			continue;
		nc = ni->body->last;
		while (nc != NULL) {
			switch (nc->tok) {
			case MDOC_Pp:
			case MDOC_Lp:
			case ROFF_br:
				break;
			default:
				nc = NULL;
				continue;
			}
			if (ni->next == NULL) {
				mandoc_msg(MANDOCERR_PAR_MOVE,
				    mdoc->parse, nc->line, nc->pos,
				    roff_name[nc->tok]);
				mdoc_node_relink(mdoc, nc);
			} else if (n->norm->Bl.comp == 0 &&
			    n->norm->Bl.type != LIST_column) {
				mandoc_vmsg(MANDOCERR_PAR_SKIP,
				    mdoc->parse, nc->line, nc->pos,
				    "%s before It", roff_name[nc->tok]);
				roff_node_delete(mdoc, nc);
			} else
				break;
			nc = ni->body->last;
		}
	}
}

/*
 * If the argument of -offset or -width is a macro,
 * replace it with the associated default width.
 */
static void
rewrite_macro2len(struct roff_man *mdoc, char **arg)
{
	size_t		  width;
	enum roff_tok	  tok;

	if (*arg == NULL)
		return;
	else if ( ! strcmp(*arg, "Ds"))
		width = 6;
	else if ((tok = roffhash_find(mdoc->mdocmac, *arg, 0)) == TOKEN_NONE)
		return;
	else
		width = macro2len(tok);

	free(*arg);
	mandoc_asprintf(arg, "%zun", width);
}

static void
post_bl_head(POST_ARGS)
{
	struct roff_node *nbl, *nh, *nch, *nnext;
	struct mdoc_argv *argv;
	int		  i, j;

	post_bl_norm(mdoc);

	nh = mdoc->last;
	if (nh->norm->Bl.type != LIST_column) {
		if ((nch = nh->child) == NULL)
			return;
		mandoc_vmsg(MANDOCERR_ARG_EXCESS, mdoc->parse,
		    nch->line, nch->pos, "Bl ... %s", nch->string);
		while (nch != NULL) {
			roff_node_delete(mdoc, nch);
			nch = nh->child;
		}
		return;
	}

	/*
	 * Append old-style lists, where the column width specifiers
	 * trail as macro parameters, to the new-style ("normal-form")
	 * lists where they're argument values following -column.
	 */

	if (nh->child == NULL)
		return;

	nbl = nh->parent;
	for (j = 0; j < (int)nbl->args->argc; j++)
		if (nbl->args->argv[j].arg == MDOC_Column)
			break;

	assert(j < (int)nbl->args->argc);

	/*
	 * Accommodate for new-style groff column syntax.  Shuffle the
	 * child nodes, all of which must be TEXT, as arguments for the
	 * column field.  Then, delete the head children.
	 */

	argv = nbl->args->argv + j;
	i = argv->sz;
	for (nch = nh->child; nch != NULL; nch = nch->next)
		argv->sz++;
	argv->value = mandoc_reallocarray(argv->value,
	    argv->sz, sizeof(char *));

	nh->norm->Bl.ncols = argv->sz;
	nh->norm->Bl.cols = (void *)argv->value;

	for (nch = nh->child; nch != NULL; nch = nnext) {
		argv->value[i++] = nch->string;
		nch->string = NULL;
		nnext = nch->next;
		roff_node_delete(NULL, nch);
	}
	nh->child = NULL;
}

static void
post_bl(POST_ARGS)
{
	struct roff_node	*nparent, *nprev; /* of the Bl block */
	struct roff_node	*nblock, *nbody;  /* of the Bl */
	struct roff_node	*nchild, *nnext;  /* of the Bl body */
	const char		*prev_Er;
	int			 order;

	nbody = mdoc->last;
	switch (nbody->type) {
	case ROFFT_BLOCK:
		post_bl_block(mdoc);
		return;
	case ROFFT_HEAD:
		post_bl_head(mdoc);
		return;
	case ROFFT_BODY:
		break;
	default:
		return;
	}
	if (nbody->end != ENDBODY_NOT)
		return;

	nchild = nbody->child;
	if (nchild == NULL) {
		mandoc_msg(MANDOCERR_BLK_EMPTY, mdoc->parse,
		    nbody->line, nbody->pos, "Bl");
		return;
	}
	while (nchild != NULL) {
		nnext = nchild->next;
		if (nchild->tok == MDOC_It ||
		    (nchild->tok == MDOC_Sm &&
		     nnext != NULL && nnext->tok == MDOC_It)) {
			nchild = nnext;
			continue;
		}

		/*
		 * In .Bl -column, the first rows may be implicit,
		 * that is, they may not start with .It macros.
		 * Such rows may be followed by nodes generated on the
		 * roff level, for example .TS, which cannot be moved
		 * out of the list.  In that case, wrap such roff nodes
		 * into an implicit row.
		 */

		if (nchild->prev != NULL) {
			mdoc->last = nchild;
			mdoc->next = ROFF_NEXT_SIBLING;
			roff_block_alloc(mdoc, nchild->line,
			    nchild->pos, MDOC_It);
			roff_head_alloc(mdoc, nchild->line,
			    nchild->pos, MDOC_It);
			mdoc->next = ROFF_NEXT_SIBLING;
			roff_body_alloc(mdoc, nchild->line,
			    nchild->pos, MDOC_It);
			while (nchild->tok != MDOC_It) {
				mdoc_node_relink(mdoc, nchild);
				if ((nchild = nnext) == NULL)
					break;
				nnext = nchild->next;
				mdoc->next = ROFF_NEXT_SIBLING;
			}
			mdoc->last = nbody;
			continue;
		}

		mandoc_msg(MANDOCERR_BL_MOVE, mdoc->parse,
		    nchild->line, nchild->pos, roff_name[nchild->tok]);

		/*
		 * Move the node out of the Bl block.
		 * First, collect all required node pointers.
		 */

		nblock  = nbody->parent;
		nprev   = nblock->prev;
		nparent = nblock->parent;

		/*
		 * Unlink this child.
		 */

		nbody->child = nnext;
		if (nnext == NULL)
			nbody->last  = NULL;
		else
			nnext->prev = NULL;

		/*
		 * Relink this child.
		 */

		nchild->parent = nparent;
		nchild->prev   = nprev;
		nchild->next   = nblock;

		nblock->prev = nchild;
		if (nprev == NULL)
			nparent->child = nchild;
		else
			nprev->next = nchild;

		nchild = nnext;
	}

	if (mdoc->meta.os_e != MANDOC_OS_NETBSD)
		return;

	prev_Er = NULL;
	for (nchild = nbody->child; nchild != NULL; nchild = nchild->next) {
		if (nchild->tok != MDOC_It)
			continue;
		if ((nnext = nchild->head->child) == NULL)
			continue;
		if (nnext->type == ROFFT_BLOCK)
			nnext = nnext->body->child;
		if (nnext == NULL || nnext->tok != MDOC_Er)
			continue;
		nnext = nnext->child;
		if (prev_Er != NULL) {
			order = strcmp(prev_Er, nnext->string);
			if (order > 0)
				mandoc_vmsg(MANDOCERR_ER_ORDER,
				    mdoc->parse, nnext->line, nnext->pos,
				    "Er %s %s (NetBSD)",
				    prev_Er, nnext->string);
			else if (order == 0)
				mandoc_vmsg(MANDOCERR_ER_REP,
				    mdoc->parse, nnext->line, nnext->pos,
				    "Er %s (NetBSD)", prev_Er);
		}
		prev_Er = nnext->string;
	}
}

static void
post_bk(POST_ARGS)
{
	struct roff_node	*n;

	n = mdoc->last;

	if (n->type == ROFFT_BLOCK && n->body->child == NULL) {
		mandoc_msg(MANDOCERR_BLK_EMPTY,
		    mdoc->parse, n->line, n->pos, "Bk");
		roff_node_delete(mdoc, n);
	}
}

static void
post_sm(POST_ARGS)
{
	struct roff_node	*nch;

	nch = mdoc->last->child;

	if (nch == NULL) {
		mdoc->flags ^= MDOC_SMOFF;
		return;
	}

	assert(nch->type == ROFFT_TEXT);

	if ( ! strcmp(nch->string, "on")) {
		mdoc->flags &= ~MDOC_SMOFF;
		return;
	}
	if ( ! strcmp(nch->string, "off")) {
		mdoc->flags |= MDOC_SMOFF;
		return;
	}

	mandoc_vmsg(MANDOCERR_SM_BAD,
	    mdoc->parse, nch->line, nch->pos,
	    "%s %s", roff_name[mdoc->last->tok], nch->string);
	mdoc_node_relink(mdoc, nch);
	return;
}

static void
post_root(POST_ARGS)
{
	const char *openbsd_arch[] = {
		"alpha", "amd64", "arm64", "armv7", "hppa", "i386",
		"landisk", "loongson", "luna88k", "macppc", "mips64",
		"octeon", "sgi", "socppc", "sparc64", NULL
	};
	const char *netbsd_arch[] = {
		"acorn26", "acorn32", "algor", "alpha", "amiga",
		"arc", "atari",
		"bebox", "cats", "cesfic", "cobalt", "dreamcast",
		"emips", "evbarm", "evbmips", "evbppc", "evbsh3", "evbsh5",
		"hp300", "hpcarm", "hpcmips", "hpcsh", "hppa",
		"i386", "ibmnws", "luna68k",
		"mac68k", "macppc", "mipsco", "mmeye", "mvme68k", "mvmeppc",
		"netwinder", "news68k", "newsmips", "next68k",
		"pc532", "playstation2", "pmax", "pmppc", "prep",
		"sandpoint", "sbmips", "sgimips", "shark",
		"sparc", "sparc64", "sun2", "sun3",
		"vax", "walnut", "x68k", "x86", "x86_64", "xen", NULL
        };
	const char **arches[] = { NULL, netbsd_arch, openbsd_arch };

	struct roff_node *n;
	const char **arch;

	/* Add missing prologue data. */

	if (mdoc->meta.date == NULL)
		mdoc->meta.date = mdoc->quick ? mandoc_strdup("") :
		    mandoc_normdate(mdoc, NULL, 0, 0);

	if (mdoc->meta.title == NULL) {
		mandoc_msg(MANDOCERR_DT_NOTITLE,
		    mdoc->parse, 0, 0, "EOF");
		mdoc->meta.title = mandoc_strdup("UNTITLED");
	}

	if (mdoc->meta.vol == NULL)
		mdoc->meta.vol = mandoc_strdup("LOCAL");

	if (mdoc->meta.os == NULL) {
		mandoc_msg(MANDOCERR_OS_MISSING,
		    mdoc->parse, 0, 0, NULL);
		mdoc->meta.os = mandoc_strdup("");
	} else if (mdoc->meta.os_e &&
	    (mdoc->meta.rcsids & (1 << mdoc->meta.os_e)) == 0)
		mandoc_msg(MANDOCERR_RCS_MISSING, mdoc->parse, 0, 0,
		    mdoc->meta.os_e == MANDOC_OS_OPENBSD ?
		    "(OpenBSD)" : "(NetBSD)");

	if (mdoc->meta.arch != NULL &&
	    (arch = arches[mdoc->meta.os_e]) != NULL) {
		while (*arch != NULL && strcmp(*arch, mdoc->meta.arch))
			arch++;
		if (*arch == NULL) {
			n = mdoc->first->child;
			while (n->tok != MDOC_Dt ||
			    n->child == NULL ||
			    n->child->next == NULL ||
			    n->child->next->next == NULL)
				n = n->next;
			n = n->child->next->next;
			mandoc_vmsg(MANDOCERR_ARCH_BAD,
			    mdoc->parse, n->line, n->pos,
			    "Dt ... %s %s", mdoc->meta.arch,
			    mdoc->meta.os_e == MANDOC_OS_OPENBSD ?
			    "(OpenBSD)" : "(NetBSD)");
		}
	}

	/* Check that we begin with a proper `Sh'. */

	n = mdoc->first->child;
	while (n != NULL &&
	    (n->type == ROFFT_COMMENT ||
	     (n->tok >= MDOC_Dd &&
	      mdoc_macros[n->tok].flags & MDOC_PROLOGUE)))
		n = n->next;

	if (n == NULL)
		mandoc_msg(MANDOCERR_DOC_EMPTY, mdoc->parse, 0, 0, NULL);
	else if (n->tok != MDOC_Sh)
		mandoc_msg(MANDOCERR_SEC_BEFORE, mdoc->parse,
		    n->line, n->pos, roff_name[n->tok]);
}

static void
post_rs(POST_ARGS)
{
	struct roff_node *np, *nch, *next, *prev;
	int		  i, j;

	np = mdoc->last;

	if (np->type != ROFFT_BODY)
		return;

	if (np->child == NULL) {
		mandoc_msg(MANDOCERR_RS_EMPTY, mdoc->parse,
		    np->line, np->pos, "Rs");
		return;
	}

	/*
	 * The full `Rs' block needs special handling to order the
	 * sub-elements according to `rsord'.  Pick through each element
	 * and correctly order it.  This is an insertion sort.
	 */

	next = NULL;
	for (nch = np->child->next; nch != NULL; nch = next) {
		/* Determine order number of this child. */
		for (i = 0; i < RSORD_MAX; i++)
			if (rsord[i] == nch->tok)
				break;

		if (i == RSORD_MAX) {
			mandoc_msg(MANDOCERR_RS_BAD, mdoc->parse,
			    nch->line, nch->pos, roff_name[nch->tok]);
			i = -1;
		} else if (nch->tok == MDOC__J || nch->tok == MDOC__B)
			np->norm->Rs.quote_T++;

		/*
		 * Remove this child from the chain.  This somewhat
		 * repeats roff_node_unlink(), but since we're
		 * just re-ordering, there's no need for the
		 * full unlink process.
		 */

		if ((next = nch->next) != NULL)
			next->prev = nch->prev;

		if ((prev = nch->prev) != NULL)
			prev->next = nch->next;

		nch->prev = nch->next = NULL;

		/*
		 * Scan back until we reach a node that's
		 * to be ordered before this child.
		 */

		for ( ; prev ; prev = prev->prev) {
			/* Determine order of `prev'. */
			for (j = 0; j < RSORD_MAX; j++)
				if (rsord[j] == prev->tok)
					break;
			if (j == RSORD_MAX)
				j = -1;

			if (j <= i)
				break;
		}

		/*
		 * Set this child back into its correct place
		 * in front of the `prev' node.
		 */

		nch->prev = prev;

		if (prev == NULL) {
			np->child->prev = nch;
			nch->next = np->child;
			np->child = nch;
		} else {
			if (prev->next)
				prev->next->prev = nch;
			nch->next = prev->next;
			prev->next = nch;
		}
	}
}

/*
 * For some arguments of some macros,
 * convert all breakable hyphens into ASCII_HYPH.
 */
static void
post_hyph(POST_ARGS)
{
	struct roff_node	*nch;
	char			*cp;

	for (nch = mdoc->last->child; nch != NULL; nch = nch->next) {
		if (nch->type != ROFFT_TEXT)
			continue;
		cp = nch->string;
		if (*cp == '\0')
			continue;
		while (*(++cp) != '\0')
			if (*cp == '-' &&
			    isalpha((unsigned char)cp[-1]) &&
			    isalpha((unsigned char)cp[1]))
				*cp = ASCII_HYPH;
	}
}

static void
post_ns(POST_ARGS)
{
	struct roff_node	*n;

	n = mdoc->last;
	if (n->flags & NODE_LINE ||
	    (n->next != NULL && n->next->flags & NODE_DELIMC))
		mandoc_msg(MANDOCERR_NS_SKIP, mdoc->parse,
		    n->line, n->pos, NULL);
}

static void
post_sx(POST_ARGS)
{
	post_delim(mdoc);
	post_hyph(mdoc);
}

static void
post_sh(POST_ARGS)
{

	post_ignpar(mdoc);

	switch (mdoc->last->type) {
	case ROFFT_HEAD:
		post_sh_head(mdoc);
		break;
	case ROFFT_BODY:
		switch (mdoc->lastsec)  {
		case SEC_NAME:
			post_sh_name(mdoc);
			break;
		case SEC_SEE_ALSO:
			post_sh_see_also(mdoc);
			break;
		case SEC_AUTHORS:
			post_sh_authors(mdoc);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void
post_sh_name(POST_ARGS)
{
	struct roff_node *n;
	int hasnm, hasnd;

	hasnm = hasnd = 0;

	for (n = mdoc->last->child; n != NULL; n = n->next) {
		switch (n->tok) {
		case MDOC_Nm:
			if (hasnm && n->child != NULL)
				mandoc_vmsg(MANDOCERR_NAMESEC_PUNCT,
				    mdoc->parse, n->line, n->pos,
				    "Nm %s", n->child->string);
			hasnm = 1;
			continue;
		case MDOC_Nd:
			hasnd = 1;
			if (n->next != NULL)
				mandoc_msg(MANDOCERR_NAMESEC_ND,
				    mdoc->parse, n->line, n->pos, NULL);
			break;
		case TOKEN_NONE:
			if (n->type == ROFFT_TEXT &&
			    n->string[0] == ',' && n->string[1] == '\0' &&
			    n->next != NULL && n->next->tok == MDOC_Nm) {
				n = n->next;
				continue;
			}
			/* FALLTHROUGH */
		default:
			mandoc_msg(MANDOCERR_NAMESEC_BAD, mdoc->parse,
			    n->line, n->pos, roff_name[n->tok]);
			continue;
		}
		break;
	}

	if ( ! hasnm)
		mandoc_msg(MANDOCERR_NAMESEC_NONM, mdoc->parse,
		    mdoc->last->line, mdoc->last->pos, NULL);
	if ( ! hasnd)
		mandoc_msg(MANDOCERR_NAMESEC_NOND, mdoc->parse,
		    mdoc->last->line, mdoc->last->pos, NULL);
}

static void
post_sh_see_also(POST_ARGS)
{
	const struct roff_node	*n;
	const char		*name, *sec;
	const char		*lastname, *lastsec, *lastpunct;
	int			 cmp;

	n = mdoc->last->child;
	lastname = lastsec = lastpunct = NULL;
	while (n != NULL) {
		if (n->tok != MDOC_Xr ||
		    n->child == NULL ||
		    n->child->next == NULL)
			break;

		/* Process one .Xr node. */

		name = n->child->string;
		sec = n->child->next->string;
		if (lastsec != NULL) {
			if (lastpunct[0] != ',' || lastpunct[1] != '\0')
				mandoc_vmsg(MANDOCERR_XR_PUNCT,
				    mdoc->parse, n->line, n->pos,
				    "%s before %s(%s)", lastpunct,
				    name, sec);
			cmp = strcmp(lastsec, sec);
			if (cmp > 0)
				mandoc_vmsg(MANDOCERR_XR_ORDER,
				    mdoc->parse, n->line, n->pos,
				    "%s(%s) after %s(%s)", name,
				    sec, lastname, lastsec);
			else if (cmp == 0 &&
			    strcasecmp(lastname, name) > 0)
				mandoc_vmsg(MANDOCERR_XR_ORDER,
				    mdoc->parse, n->line, n->pos,
				    "%s after %s", name, lastname);
		}
		lastname = name;
		lastsec = sec;

		/* Process the following node. */

		n = n->next;
		if (n == NULL)
			break;
		if (n->tok == MDOC_Xr) {
			lastpunct = "none";
			continue;
		}
		if (n->type != ROFFT_TEXT)
			break;
		for (name = n->string; *name != '\0'; name++)
			if (isalpha((const unsigned char)*name))
				return;
		lastpunct = n->string;
		if (n->next == NULL || n->next->tok == MDOC_Rs)
			mandoc_vmsg(MANDOCERR_XR_PUNCT, mdoc->parse,
			    n->line, n->pos, "%s after %s(%s)",
			    lastpunct, lastname, lastsec);
		n = n->next;
	}
}

static int
child_an(const struct roff_node *n)
{

	for (n = n->child; n != NULL; n = n->next)
		if ((n->tok == MDOC_An && n->child != NULL) || child_an(n))
			return 1;
	return 0;
}

static void
post_sh_authors(POST_ARGS)
{

	if ( ! child_an(mdoc->last))
		mandoc_msg(MANDOCERR_AN_MISSING, mdoc->parse,
		    mdoc->last->line, mdoc->last->pos, NULL);
}

/*
 * Return an upper bound for the string distance (allowing
 * transpositions).  Not a full Levenshtein implementation
 * because Levenshtein is quadratic in the string length
 * and this function is called for every standard name,
 * so the check for each custom name would be cubic.
 * The following crude heuristics is linear, resulting
 * in quadratic behaviour for checking one custom name,
 * which does not cause measurable slowdown.
 */
static int
similar(const char *s1, const char *s2)
{
	const int	maxdist = 3;
	int		dist = 0;

	while (s1[0] != '\0' && s2[0] != '\0') {
		if (s1[0] == s2[0]) {
			s1++;
			s2++;
			continue;
		}
		if (++dist > maxdist)
			return INT_MAX;
		if (s1[1] == s2[1]) {  /* replacement */
			s1++;
			s2++;
		} else if (s1[0] == s2[1] && s1[1] == s2[0]) {
			s1 += 2;	/* transposition */
			s2 += 2;
		} else if (s1[0] == s2[1])  /* insertion */
			s2++;
		else if (s1[1] == s2[0])  /* deletion */
			s1++;
		else
			return INT_MAX;
	}
	dist += strlen(s1) + strlen(s2);
	return dist > maxdist ? INT_MAX : dist;
}

static void
post_sh_head(POST_ARGS)
{
	struct roff_node	*nch;
	const char		*goodsec;
	const char *const	*testsec;
	int			 dist, mindist;
	enum roff_sec		 sec;

	/*
	 * Process a new section.  Sections are either "named" or
	 * "custom".  Custom sections are user-defined, while named ones
	 * follow a conventional order and may only appear in certain
	 * manual sections.
	 */

	sec = mdoc->last->sec;

	/* The NAME should be first. */

	if (sec != SEC_NAME && mdoc->lastnamed == SEC_NONE)
		mandoc_vmsg(MANDOCERR_NAMESEC_FIRST, mdoc->parse,
		    mdoc->last->line, mdoc->last->pos, "Sh %s",
		    sec != SEC_CUSTOM ? secnames[sec] :
		    (nch = mdoc->last->child) == NULL ? "" :
		    nch->type == ROFFT_TEXT ? nch->string :
		    roff_name[nch->tok]);

	/* The SYNOPSIS gets special attention in other areas. */

	if (sec == SEC_SYNOPSIS) {
		roff_setreg(mdoc->roff, "nS", 1, '=');
		mdoc->flags |= MDOC_SYNOPSIS;
	} else {
		roff_setreg(mdoc->roff, "nS", 0, '=');
		mdoc->flags &= ~MDOC_SYNOPSIS;
	}

	/* Mark our last section. */

	mdoc->lastsec = sec;

	/* We don't care about custom sections after this. */

	if (sec == SEC_CUSTOM) {
		if ((nch = mdoc->last->child) == NULL ||
		    nch->type != ROFFT_TEXT || nch->next != NULL)
			return;
		goodsec = NULL;
		mindist = INT_MAX;
		for (testsec = secnames + 1; *testsec != NULL; testsec++) {
			dist = similar(nch->string, *testsec);
			if (dist < mindist) {
				goodsec = *testsec;
				mindist = dist;
			}
		}
		if (goodsec != NULL)
			mandoc_vmsg(MANDOCERR_SEC_TYPO, mdoc->parse,
			    nch->line, nch->pos, "Sh %s instead of %s",
			    nch->string, goodsec);
		return;
	}

	/*
	 * Check whether our non-custom section is being repeated or is
	 * out of order.
	 */

	if (sec == mdoc->lastnamed)
		mandoc_vmsg(MANDOCERR_SEC_REP, mdoc->parse,
		    mdoc->last->line, mdoc->last->pos,
		    "Sh %s", secnames[sec]);

	if (sec < mdoc->lastnamed)
		mandoc_vmsg(MANDOCERR_SEC_ORDER, mdoc->parse,
		    mdoc->last->line, mdoc->last->pos,
		    "Sh %s", secnames[sec]);

	/* Mark the last named section. */

	mdoc->lastnamed = sec;

	/* Check particular section/manual conventions. */

	if (mdoc->meta.msec == NULL)
		return;

	goodsec = NULL;
	switch (sec) {
	case SEC_ERRORS:
		if (*mdoc->meta.msec == '4')
			break;
		goodsec = "2, 3, 4, 9";
		/* FALLTHROUGH */
	case SEC_RETURN_VALUES:
	case SEC_LIBRARY:
		if (*mdoc->meta.msec == '2')
			break;
		if (*mdoc->meta.msec == '3')
			break;
		if (NULL == goodsec)
			goodsec = "2, 3, 9";
		/* FALLTHROUGH */
	case SEC_CONTEXT:
		if (*mdoc->meta.msec == '9')
			break;
		if (NULL == goodsec)
			goodsec = "9";
		mandoc_vmsg(MANDOCERR_SEC_MSEC, mdoc->parse,
		    mdoc->last->line, mdoc->last->pos,
		    "Sh %s for %s only", secnames[sec], goodsec);
		break;
	default:
		break;
	}
}

static void
post_xr(POST_ARGS)
{
	struct roff_node *n, *nch;

	n = mdoc->last;
	nch = n->child;
	if (nch->next == NULL) {
		mandoc_vmsg(MANDOCERR_XR_NOSEC, mdoc->parse,
		    n->line, n->pos, "Xr %s", nch->string);
	} else {
		assert(nch->next == n->last);
		if(mandoc_xr_add(nch->next->string, nch->string,
		    nch->line, nch->pos))
			mandoc_vmsg(MANDOCERR_XR_SELF, mdoc->parse,
			    nch->line, nch->pos, "Xr %s %s",
			    nch->string, nch->next->string);
	}
	post_delim_nb(mdoc);
}

static void
post_ignpar(POST_ARGS)
{
	struct roff_node *np;

	switch (mdoc->last->type) {
	case ROFFT_BLOCK:
		post_prevpar(mdoc);
		return;
	case ROFFT_HEAD:
		post_delim(mdoc);
		post_hyph(mdoc);
		return;
	case ROFFT_BODY:
		break;
	default:
		return;
	}

	if ((np = mdoc->last->child) != NULL)
		if (np->tok == MDOC_Pp || np->tok == MDOC_Lp) {
			mandoc_vmsg(MANDOCERR_PAR_SKIP,
			    mdoc->parse, np->line, np->pos,
			    "%s after %s", roff_name[np->tok],
			    roff_name[mdoc->last->tok]);
			roff_node_delete(mdoc, np);
		}

	if ((np = mdoc->last->last) != NULL)
		if (np->tok == MDOC_Pp || np->tok == MDOC_Lp) {
			mandoc_vmsg(MANDOCERR_PAR_SKIP, mdoc->parse,
			    np->line, np->pos, "%s at the end of %s",
			    roff_name[np->tok],
			    roff_name[mdoc->last->tok]);
			roff_node_delete(mdoc, np);
		}
}

static void
post_prevpar(POST_ARGS)
{
	struct roff_node *n;

	n = mdoc->last;
	if (NULL == n->prev)
		return;
	if (n->type != ROFFT_ELEM && n->type != ROFFT_BLOCK)
		return;

	/*
	 * Don't allow prior `Lp' or `Pp' prior to a paragraph-type
	 * block:  `Lp', `Pp', or non-compact `Bd' or `Bl'.
	 */

	if (n->prev->tok != MDOC_Pp &&
	    n->prev->tok != MDOC_Lp &&
	    n->prev->tok != ROFF_br)
		return;
	if (n->tok == MDOC_Bl && n->norm->Bl.comp)
		return;
	if (n->tok == MDOC_Bd && n->norm->Bd.comp)
		return;
	if (n->tok == MDOC_It && n->parent->norm->Bl.comp)
		return;

	mandoc_vmsg(MANDOCERR_PAR_SKIP, mdoc->parse,
	    n->prev->line, n->prev->pos, "%s before %s",
	    roff_name[n->prev->tok], roff_name[n->tok]);
	roff_node_delete(mdoc, n->prev);
}

static void
post_par(POST_ARGS)
{
	struct roff_node *np;

	np = mdoc->last;
	if (np->tok != ROFF_br && np->tok != ROFF_sp)
		post_prevpar(mdoc);

	if (np->tok == ROFF_sp) {
		if (np->child != NULL && np->child->next != NULL)
			mandoc_vmsg(MANDOCERR_ARG_EXCESS, mdoc->parse,
			    np->child->next->line, np->child->next->pos,
			    "sp ... %s", np->child->next->string);
	} else if (np->child != NULL)
		mandoc_vmsg(MANDOCERR_ARG_SKIP,
		    mdoc->parse, np->line, np->pos, "%s %s",
		    roff_name[np->tok], np->child->string);

	if ((np = mdoc->last->prev) == NULL) {
		np = mdoc->last->parent;
		if (np->tok != MDOC_Sh && np->tok != MDOC_Ss)
			return;
	} else if (np->tok != MDOC_Pp && np->tok != MDOC_Lp &&
	    (mdoc->last->tok != ROFF_br ||
	     (np->tok != ROFF_sp && np->tok != ROFF_br)))
		return;

	mandoc_vmsg(MANDOCERR_PAR_SKIP, mdoc->parse,
	    mdoc->last->line, mdoc->last->pos, "%s after %s",
	    roff_name[mdoc->last->tok], roff_name[np->tok]);
	roff_node_delete(mdoc, mdoc->last);
}

static void
post_dd(POST_ARGS)
{
	struct roff_node *n;
	char		 *datestr;

	n = mdoc->last;
	n->flags |= NODE_NOPRT;

	if (mdoc->meta.date != NULL) {
		mandoc_msg(MANDOCERR_PROLOG_REP, mdoc->parse,
		    n->line, n->pos, "Dd");
		free(mdoc->meta.date);
	} else if (mdoc->flags & MDOC_PBODY)
		mandoc_msg(MANDOCERR_PROLOG_LATE, mdoc->parse,
		    n->line, n->pos, "Dd");
	else if (mdoc->meta.title != NULL)
		mandoc_msg(MANDOCERR_PROLOG_ORDER, mdoc->parse,
		    n->line, n->pos, "Dd after Dt");
	else if (mdoc->meta.os != NULL)
		mandoc_msg(MANDOCERR_PROLOG_ORDER, mdoc->parse,
		    n->line, n->pos, "Dd after Os");

	if (n->child == NULL || n->child->string[0] == '\0') {
		mdoc->meta.date = mdoc->quick ? mandoc_strdup("") :
		    mandoc_normdate(mdoc, NULL, n->line, n->pos);
		return;
	}

	datestr = NULL;
	deroff(&datestr, n);
	if (mdoc->quick)
		mdoc->meta.date = datestr;
	else {
		mdoc->meta.date = mandoc_normdate(mdoc,
		    datestr, n->line, n->pos);
		free(datestr);
	}
}

static void
post_dt(POST_ARGS)
{
	struct roff_node *nn, *n;
	const char	 *cp;
	char		 *p;

	n = mdoc->last;
	n->flags |= NODE_NOPRT;

	if (mdoc->flags & MDOC_PBODY) {
		mandoc_msg(MANDOCERR_DT_LATE, mdoc->parse,
		    n->line, n->pos, "Dt");
		return;
	}

	if (mdoc->meta.title != NULL)
		mandoc_msg(MANDOCERR_PROLOG_REP, mdoc->parse,
		    n->line, n->pos, "Dt");
	else if (mdoc->meta.os != NULL)
		mandoc_msg(MANDOCERR_PROLOG_ORDER, mdoc->parse,
		    n->line, n->pos, "Dt after Os");

	free(mdoc->meta.title);
	free(mdoc->meta.msec);
	free(mdoc->meta.vol);
	free(mdoc->meta.arch);

	mdoc->meta.title = NULL;
	mdoc->meta.msec = NULL;
	mdoc->meta.vol = NULL;
	mdoc->meta.arch = NULL;

	/* Mandatory first argument: title. */

	nn = n->child;
	if (nn == NULL || *nn->string == '\0') {
		mandoc_msg(MANDOCERR_DT_NOTITLE,
		    mdoc->parse, n->line, n->pos, "Dt");
		mdoc->meta.title = mandoc_strdup("UNTITLED");
	} else {
		mdoc->meta.title = mandoc_strdup(nn->string);

		/* Check that all characters are uppercase. */

		for (p = nn->string; *p != '\0'; p++)
			if (islower((unsigned char)*p)) {
				mandoc_vmsg(MANDOCERR_TITLE_CASE,
				    mdoc->parse, nn->line,
				    nn->pos + (p - nn->string),
				    "Dt %s", nn->string);
				break;
			}
	}

	/* Mandatory second argument: section. */

	if (nn != NULL)
		nn = nn->next;

	if (nn == NULL) {
		mandoc_vmsg(MANDOCERR_MSEC_MISSING,
		    mdoc->parse, n->line, n->pos,
		    "Dt %s", mdoc->meta.title);
		mdoc->meta.vol = mandoc_strdup("LOCAL");
		return;  /* msec and arch remain NULL. */
	}

	mdoc->meta.msec = mandoc_strdup(nn->string);

	/* Infer volume title from section number. */

	cp = mandoc_a2msec(nn->string);
	if (cp == NULL) {
		mandoc_vmsg(MANDOCERR_MSEC_BAD, mdoc->parse,
		    nn->line, nn->pos, "Dt ... %s", nn->string);
		mdoc->meta.vol = mandoc_strdup(nn->string);
	} else
		mdoc->meta.vol = mandoc_strdup(cp);

	/* Optional third argument: architecture. */

	if ((nn = nn->next) == NULL)
		return;

	for (p = nn->string; *p != '\0'; p++)
		*p = tolower((unsigned char)*p);
	mdoc->meta.arch = mandoc_strdup(nn->string);

	/* Ignore fourth and later arguments. */

	if ((nn = nn->next) != NULL)
		mandoc_vmsg(MANDOCERR_ARG_EXCESS, mdoc->parse,
		    nn->line, nn->pos, "Dt ... %s", nn->string);
}

static void
post_bx(POST_ARGS)
{
	struct roff_node	*n, *nch;
	const char		*macro;

	post_delim_nb(mdoc);

	n = mdoc->last;
	nch = n->child;

	if (nch != NULL) {
		macro = !strcmp(nch->string, "Open") ? "Ox" :
		    !strcmp(nch->string, "Net") ? "Nx" :
		    !strcmp(nch->string, "Free") ? "Fx" :
		    !strcmp(nch->string, "DragonFly") ? "Dx" : NULL;
		if (macro != NULL)
			mandoc_msg(MANDOCERR_BX, mdoc->parse,
			    n->line, n->pos, macro);
		mdoc->last = nch;
		nch = nch->next;
		mdoc->next = ROFF_NEXT_SIBLING;
		roff_elem_alloc(mdoc, n->line, n->pos, MDOC_Ns);
		mdoc->last->flags |= NODE_NOSRC;
		mdoc->next = ROFF_NEXT_SIBLING;
	} else
		mdoc->next = ROFF_NEXT_CHILD;
	roff_word_alloc(mdoc, n->line, n->pos, "BSD");
	mdoc->last->flags |= NODE_NOSRC;

	if (nch == NULL) {
		mdoc->last = n;
		return;
	}

	roff_elem_alloc(mdoc, n->line, n->pos, MDOC_Ns);
	mdoc->last->flags |= NODE_NOSRC;
	mdoc->next = ROFF_NEXT_SIBLING;
	roff_word_alloc(mdoc, n->line, n->pos, "-");
	mdoc->last->flags |= NODE_NOSRC;
	roff_elem_alloc(mdoc, n->line, n->pos, MDOC_Ns);
	mdoc->last->flags |= NODE_NOSRC;
	mdoc->last = n;

	/*
	 * Make `Bx's second argument always start with an uppercase
	 * letter.  Groff checks if it's an "accepted" term, but we just
	 * uppercase blindly.
	 */

	*nch->string = (char)toupper((unsigned char)*nch->string);
}

static void
post_os(POST_ARGS)
{
#ifndef OSNAME
	struct utsname	  utsname;
	static char	 *defbuf;
#endif
	struct roff_node *n;

	n = mdoc->last;
	n->flags |= NODE_NOPRT;

	if (mdoc->meta.os != NULL)
		mandoc_msg(MANDOCERR_PROLOG_REP, mdoc->parse,
		    n->line, n->pos, "Os");
	else if (mdoc->flags & MDOC_PBODY)
		mandoc_msg(MANDOCERR_PROLOG_LATE, mdoc->parse,
		    n->line, n->pos, "Os");

	post_delim(mdoc);

	/*
	 * Set the operating system by way of the `Os' macro.
	 * The order of precedence is:
	 * 1. the argument of the `Os' macro, unless empty
	 * 2. the -Ios=foo command line argument, if provided
	 * 3. -DOSNAME="\"foo\"", if provided during compilation
	 * 4. "sysname release" from uname(3)
	 */

	free(mdoc->meta.os);
	mdoc->meta.os = NULL;
	deroff(&mdoc->meta.os, n);
	if (mdoc->meta.os)
		goto out;

	if (mdoc->os_s != NULL) {
		mdoc->meta.os = mandoc_strdup(mdoc->os_s);
		goto out;
	}

#ifdef OSNAME
	mdoc->meta.os = mandoc_strdup(OSNAME);
#else /*!OSNAME */
	if (defbuf == NULL) {
		if (uname(&utsname) == -1) {
			mandoc_msg(MANDOCERR_OS_UNAME, mdoc->parse,
			    n->line, n->pos, "Os");
			defbuf = mandoc_strdup("UNKNOWN");
		} else
			mandoc_asprintf(&defbuf, "%s %s",
			    utsname.sysname, utsname.release);
	}
	mdoc->meta.os = mandoc_strdup(defbuf);
#endif /*!OSNAME*/

out:
	if (mdoc->meta.os_e == MANDOC_OS_OTHER) {
		if (strstr(mdoc->meta.os, "OpenBSD") != NULL)
			mdoc->meta.os_e = MANDOC_OS_OPENBSD;
		else if (strstr(mdoc->meta.os, "NetBSD") != NULL)
			mdoc->meta.os_e = MANDOC_OS_NETBSD;
	}

	/*
	 * This is the earliest point where we can check
	 * Mdocdate conventions because we don't know
	 * the operating system earlier.
	 */

	if (n->child != NULL)
		mandoc_vmsg(MANDOCERR_OS_ARG, mdoc->parse,
		    n->child->line, n->child->pos,
		    "Os %s (%s)", n->child->string,
		    mdoc->meta.os_e == MANDOC_OS_OPENBSD ?
		    "OpenBSD" : "NetBSD");

	while (n->tok != MDOC_Dd)
		if ((n = n->prev) == NULL)
			return;
	if ((n = n->child) == NULL)
		return;
	if (strncmp(n->string, "$" "Mdocdate", 9)) {
		if (mdoc->meta.os_e == MANDOC_OS_OPENBSD)
			mandoc_vmsg(MANDOCERR_MDOCDATE_MISSING,
			    mdoc->parse, n->line, n->pos,
			    "Dd %s (OpenBSD)", n->string);
	} else {
		if (mdoc->meta.os_e == MANDOC_OS_NETBSD)
			mandoc_vmsg(MANDOCERR_MDOCDATE,
			    mdoc->parse, n->line, n->pos,
			    "Dd %s (NetBSD)", n->string);
	}
}

enum roff_sec
mdoc_a2sec(const char *p)
{
	int		 i;

	for (i = 0; i < (int)SEC__MAX; i++)
		if (secnames[i] && 0 == strcmp(p, secnames[i]))
			return (enum roff_sec)i;

	return SEC_CUSTOM;
}

static size_t
macro2len(enum roff_tok macro)
{

	switch (macro) {
	case MDOC_Ad:
		return 12;
	case MDOC_Ao:
		return 12;
	case MDOC_An:
		return 12;
	case MDOC_Aq:
		return 12;
	case MDOC_Ar:
		return 12;
	case MDOC_Bo:
		return 12;
	case MDOC_Bq:
		return 12;
	case MDOC_Cd:
		return 12;
	case MDOC_Cm:
		return 10;
	case MDOC_Do:
		return 10;
	case MDOC_Dq:
		return 12;
	case MDOC_Dv:
		return 12;
	case MDOC_Eo:
		return 12;
	case MDOC_Em:
		return 10;
	case MDOC_Er:
		return 17;
	case MDOC_Ev:
		return 15;
	case MDOC_Fa:
		return 12;
	case MDOC_Fl:
		return 10;
	case MDOC_Fo:
		return 16;
	case MDOC_Fn:
		return 16;
	case MDOC_Ic:
		return 10;
	case MDOC_Li:
		return 16;
	case MDOC_Ms:
		return 6;
	case MDOC_Nm:
		return 10;
	case MDOC_No:
		return 12;
	case MDOC_Oo:
		return 10;
	case MDOC_Op:
		return 14;
	case MDOC_Pa:
		return 32;
	case MDOC_Pf:
		return 12;
	case MDOC_Po:
		return 12;
	case MDOC_Pq:
		return 12;
	case MDOC_Ql:
		return 16;
	case MDOC_Qo:
		return 12;
	case MDOC_So:
		return 12;
	case MDOC_Sq:
		return 12;
	case MDOC_Sy:
		return 6;
	case MDOC_Sx:
		return 16;
	case MDOC_Tn:
		return 10;
	case MDOC_Va:
		return 12;
	case MDOC_Vt:
		return 12;
	case MDOC_Xr:
		return 10;
	default:
		break;
	};
	return 0;
}
