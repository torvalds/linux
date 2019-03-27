/*	$Id: man_term.c,v 1.211 2018/06/10 15:12:35 schwarze Exp $ */
/*
 * Copyright (c) 2008-2012 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2010-2015, 2017, 2018 Ingo Schwarze <schwarze@openbsd.org>
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
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "man.h"
#include "out.h"
#include "term.h"
#include "main.h"

#define	MAXMARGINS	  64 /* maximum number of indented scopes */

struct	mtermp {
	int		  fl;
#define	MANT_LITERAL	 (1 << 0)
	int		  lmargin[MAXMARGINS]; /* margins (incl. vis. page) */
	int		  lmargincur; /* index of current margin */
	int		  lmarginsz; /* actual number of nested margins */
	size_t		  offset; /* default offset to visible page */
	int		  pardist; /* vert. space before par., unit: [v] */
};

#define	DECL_ARGS	  struct termp *p, \
			  struct mtermp *mt, \
			  struct roff_node *n, \
			  const struct roff_meta *meta

struct	termact {
	int		(*pre)(DECL_ARGS);
	void		(*post)(DECL_ARGS);
	int		  flags;
#define	MAN_NOTEXT	 (1 << 0) /* Never has text children. */
};

static	void		  print_man_nodelist(DECL_ARGS);
static	void		  print_man_node(DECL_ARGS);
static	void		  print_man_head(struct termp *,
				const struct roff_meta *);
static	void		  print_man_foot(struct termp *,
				const struct roff_meta *);
static	void		  print_bvspace(struct termp *,
				const struct roff_node *, int);

static	int		  pre_B(DECL_ARGS);
static	int		  pre_DT(DECL_ARGS);
static	int		  pre_HP(DECL_ARGS);
static	int		  pre_I(DECL_ARGS);
static	int		  pre_IP(DECL_ARGS);
static	int		  pre_OP(DECL_ARGS);
static	int		  pre_PD(DECL_ARGS);
static	int		  pre_PP(DECL_ARGS);
static	int		  pre_RS(DECL_ARGS);
static	int		  pre_SH(DECL_ARGS);
static	int		  pre_SS(DECL_ARGS);
static	int		  pre_TP(DECL_ARGS);
static	int		  pre_UR(DECL_ARGS);
static	int		  pre_alternate(DECL_ARGS);
static	int		  pre_ign(DECL_ARGS);
static	int		  pre_in(DECL_ARGS);
static	int		  pre_literal(DECL_ARGS);

static	void		  post_IP(DECL_ARGS);
static	void		  post_HP(DECL_ARGS);
static	void		  post_RS(DECL_ARGS);
static	void		  post_SH(DECL_ARGS);
static	void		  post_SS(DECL_ARGS);
static	void		  post_TP(DECL_ARGS);
static	void		  post_UR(DECL_ARGS);

static	const struct termact __termacts[MAN_MAX - MAN_TH] = {
	{ NULL, NULL, 0 }, /* TH */
	{ pre_SH, post_SH, 0 }, /* SH */
	{ pre_SS, post_SS, 0 }, /* SS */
	{ pre_TP, post_TP, 0 }, /* TP */
	{ pre_PP, NULL, 0 }, /* LP */
	{ pre_PP, NULL, 0 }, /* PP */
	{ pre_PP, NULL, 0 }, /* P */
	{ pre_IP, post_IP, 0 }, /* IP */
	{ pre_HP, post_HP, 0 }, /* HP */
	{ NULL, NULL, 0 }, /* SM */
	{ pre_B, NULL, 0 }, /* SB */
	{ pre_alternate, NULL, 0 }, /* BI */
	{ pre_alternate, NULL, 0 }, /* IB */
	{ pre_alternate, NULL, 0 }, /* BR */
	{ pre_alternate, NULL, 0 }, /* RB */
	{ NULL, NULL, 0 }, /* R */
	{ pre_B, NULL, 0 }, /* B */
	{ pre_I, NULL, 0 }, /* I */
	{ pre_alternate, NULL, 0 }, /* IR */
	{ pre_alternate, NULL, 0 }, /* RI */
	{ pre_literal, NULL, 0 }, /* nf */
	{ pre_literal, NULL, 0 }, /* fi */
	{ NULL, NULL, 0 }, /* RE */
	{ pre_RS, post_RS, 0 }, /* RS */
	{ pre_DT, NULL, 0 }, /* DT */
	{ pre_ign, NULL, MAN_NOTEXT }, /* UC */
	{ pre_PD, NULL, MAN_NOTEXT }, /* PD */
	{ pre_ign, NULL, 0 }, /* AT */
	{ pre_in, NULL, MAN_NOTEXT }, /* in */
	{ pre_OP, NULL, 0 }, /* OP */
	{ pre_literal, NULL, 0 }, /* EX */
	{ pre_literal, NULL, 0 }, /* EE */
	{ pre_UR, post_UR, 0 }, /* UR */
	{ NULL, NULL, 0 }, /* UE */
	{ pre_UR, post_UR, 0 }, /* MT */
	{ NULL, NULL, 0 }, /* ME */
};
static	const struct termact *termacts = __termacts - MAN_TH;


void
terminal_man(void *arg, const struct roff_man *man)
{
	struct termp		*p;
	struct roff_node	*n;
	struct mtermp		 mt;
	size_t			 save_defindent;

	p = (struct termp *)arg;
	save_defindent = p->defindent;
	if (p->synopsisonly == 0 && p->defindent == 0)
		p->defindent = 7;
	p->tcol->rmargin = p->maxrmargin = p->defrmargin;
	term_tab_set(p, NULL);
	term_tab_set(p, "T");
	term_tab_set(p, ".5i");

	memset(&mt, 0, sizeof(struct mtermp));
	mt.lmargin[mt.lmargincur] = term_len(p, p->defindent);
	mt.offset = term_len(p, p->defindent);
	mt.pardist = 1;

	n = man->first->child;
	if (p->synopsisonly) {
		while (n != NULL) {
			if (n->tok == MAN_SH &&
			    n->child->child->type == ROFFT_TEXT &&
			    !strcmp(n->child->child->string, "SYNOPSIS")) {
				if (n->child->next->child != NULL)
					print_man_nodelist(p, &mt,
					    n->child->next->child,
					    &man->meta);
				term_newln(p);
				break;
			}
			n = n->next;
		}
	} else {
		term_begin(p, print_man_head, print_man_foot, &man->meta);
		p->flags |= TERMP_NOSPACE;
		if (n != NULL)
			print_man_nodelist(p, &mt, n, &man->meta);
		term_end(p);
	}
	p->defindent = save_defindent;
}

/*
 * Printing leading vertical space before a block.
 * This is used for the paragraph macros.
 * The rules are pretty simple, since there's very little nesting going
 * on here.  Basically, if we're the first within another block (SS/SH),
 * then don't emit vertical space.  If we are (RS), then do.  If not the
 * first, print it.
 */
static void
print_bvspace(struct termp *p, const struct roff_node *n, int pardist)
{
	int	 i;

	term_newln(p);

	if (n->body && n->body->child)
		if (n->body->child->type == ROFFT_TBL)
			return;

	if (n->parent->type == ROFFT_ROOT || n->parent->tok != MAN_RS)
		if (NULL == n->prev)
			return;

	for (i = 0; i < pardist; i++)
		term_vspace(p);
}


static int
pre_ign(DECL_ARGS)
{

	return 0;
}

static int
pre_I(DECL_ARGS)
{

	term_fontrepl(p, TERMFONT_UNDER);
	return 1;
}

static int
pre_literal(DECL_ARGS)
{

	term_newln(p);

	if (n->tok == MAN_nf || n->tok == MAN_EX)
		mt->fl |= MANT_LITERAL;
	else
		mt->fl &= ~MANT_LITERAL;

	/*
	 * Unlike .IP and .TP, .HP does not have a HEAD.
	 * So in case a second call to term_flushln() is needed,
	 * indentation has to be set up explicitly.
	 */
	if (n->parent->tok == MAN_HP && p->tcol->rmargin < p->maxrmargin) {
		p->tcol->offset = p->tcol->rmargin;
		p->tcol->rmargin = p->maxrmargin;
		p->trailspace = 0;
		p->flags &= ~(TERMP_NOBREAK | TERMP_BRIND);
		p->flags |= TERMP_NOSPACE;
	}

	return 0;
}

static int
pre_PD(DECL_ARGS)
{
	struct roffsu	 su;

	n = n->child;
	if (n == NULL) {
		mt->pardist = 1;
		return 0;
	}
	assert(n->type == ROFFT_TEXT);
	if (a2roffsu(n->string, &su, SCALE_VS) != NULL)
		mt->pardist = term_vspan(p, &su);
	return 0;
}

static int
pre_alternate(DECL_ARGS)
{
	enum termfont		 font[2];
	struct roff_node	*nn;
	int			 savelit, i;

	switch (n->tok) {
	case MAN_RB:
		font[0] = TERMFONT_NONE;
		font[1] = TERMFONT_BOLD;
		break;
	case MAN_RI:
		font[0] = TERMFONT_NONE;
		font[1] = TERMFONT_UNDER;
		break;
	case MAN_BR:
		font[0] = TERMFONT_BOLD;
		font[1] = TERMFONT_NONE;
		break;
	case MAN_BI:
		font[0] = TERMFONT_BOLD;
		font[1] = TERMFONT_UNDER;
		break;
	case MAN_IR:
		font[0] = TERMFONT_UNDER;
		font[1] = TERMFONT_NONE;
		break;
	case MAN_IB:
		font[0] = TERMFONT_UNDER;
		font[1] = TERMFONT_BOLD;
		break;
	default:
		abort();
	}

	savelit = MANT_LITERAL & mt->fl;
	mt->fl &= ~MANT_LITERAL;

	for (i = 0, nn = n->child; nn; nn = nn->next, i = 1 - i) {
		term_fontrepl(p, font[i]);
		if (savelit && NULL == nn->next)
			mt->fl |= MANT_LITERAL;
		assert(nn->type == ROFFT_TEXT);
		term_word(p, nn->string);
		if (nn->flags & NODE_EOS)
                	p->flags |= TERMP_SENTENCE;
		if (nn->next)
			p->flags |= TERMP_NOSPACE;
	}

	return 0;
}

static int
pre_B(DECL_ARGS)
{

	term_fontrepl(p, TERMFONT_BOLD);
	return 1;
}

static int
pre_OP(DECL_ARGS)
{

	term_word(p, "[");
	p->flags |= TERMP_NOSPACE;

	if (NULL != (n = n->child)) {
		term_fontrepl(p, TERMFONT_BOLD);
		term_word(p, n->string);
	}
	if (NULL != n && NULL != n->next) {
		term_fontrepl(p, TERMFONT_UNDER);
		term_word(p, n->next->string);
	}

	term_fontrepl(p, TERMFONT_NONE);
	p->flags |= TERMP_NOSPACE;
	term_word(p, "]");
	return 0;
}

static int
pre_in(DECL_ARGS)
{
	struct roffsu	 su;
	const char	*cp;
	size_t		 v;
	int		 less;

	term_newln(p);

	if (n->child == NULL) {
		p->tcol->offset = mt->offset;
		return 0;
	}

	cp = n->child->string;
	less = 0;

	if ('-' == *cp)
		less = -1;
	else if ('+' == *cp)
		less = 1;
	else
		cp--;

	if (a2roffsu(++cp, &su, SCALE_EN) == NULL)
		return 0;

	v = term_hen(p, &su);

	if (less < 0)
		p->tcol->offset -= p->tcol->offset > v ? v : p->tcol->offset;
	else if (less > 0)
		p->tcol->offset += v;
	else
		p->tcol->offset = v;
	if (p->tcol->offset > SHRT_MAX)
		p->tcol->offset = term_len(p, p->defindent);

	return 0;
}

static int
pre_DT(DECL_ARGS)
{
	term_tab_set(p, NULL);
	term_tab_set(p, "T");
	term_tab_set(p, ".5i");
	return 0;
}

static int
pre_HP(DECL_ARGS)
{
	struct roffsu		 su;
	const struct roff_node	*nn;
	int			 len;

	switch (n->type) {
	case ROFFT_BLOCK:
		print_bvspace(p, n, mt->pardist);
		return 1;
	case ROFFT_BODY:
		break;
	default:
		return 0;
	}

	if ( ! (MANT_LITERAL & mt->fl)) {
		p->flags |= TERMP_NOBREAK | TERMP_BRIND;
		p->trailspace = 2;
	}

	/* Calculate offset. */

	if ((nn = n->parent->head->child) != NULL &&
	    a2roffsu(nn->string, &su, SCALE_EN) != NULL) {
		len = term_hen(p, &su);
		if (len < 0 && (size_t)(-len) > mt->offset)
			len = -mt->offset;
		else if (len > SHRT_MAX)
			len = term_len(p, p->defindent);
		mt->lmargin[mt->lmargincur] = len;
	} else
		len = mt->lmargin[mt->lmargincur];

	p->tcol->offset = mt->offset;
	p->tcol->rmargin = mt->offset + len;
	return 1;
}

static void
post_HP(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_BODY:
		term_newln(p);

		/*
		 * Compatibility with a groff bug.
		 * The .HP macro uses the undocumented .tag request
		 * which causes a line break and cancels no-space
		 * mode even if there isn't any output.
		 */

		if (n->child == NULL)
			term_vspace(p);

		p->flags &= ~(TERMP_NOBREAK | TERMP_BRIND);
		p->trailspace = 0;
		p->tcol->offset = mt->offset;
		p->tcol->rmargin = p->maxrmargin;
		break;
	default:
		break;
	}
}

static int
pre_PP(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_BLOCK:
		mt->lmargin[mt->lmargincur] = term_len(p, p->defindent);
		print_bvspace(p, n, mt->pardist);
		break;
	default:
		p->tcol->offset = mt->offset;
		break;
	}

	return n->type != ROFFT_HEAD;
}

static int
pre_IP(DECL_ARGS)
{
	struct roffsu		 su;
	const struct roff_node	*nn;
	int			 len, savelit;

	switch (n->type) {
	case ROFFT_BODY:
		p->flags |= TERMP_NOSPACE;
		break;
	case ROFFT_HEAD:
		p->flags |= TERMP_NOBREAK;
		p->trailspace = 1;
		break;
	case ROFFT_BLOCK:
		print_bvspace(p, n, mt->pardist);
		/* FALLTHROUGH */
	default:
		return 1;
	}

	/* Calculate the offset from the optional second argument. */
	if ((nn = n->parent->head->child) != NULL &&
	    (nn = nn->next) != NULL &&
	    a2roffsu(nn->string, &su, SCALE_EN) != NULL) {
		len = term_hen(p, &su);
		if (len < 0 && (size_t)(-len) > mt->offset)
			len = -mt->offset;
		else if (len > SHRT_MAX)
			len = term_len(p, p->defindent);
		mt->lmargin[mt->lmargincur] = len;
	} else
		len = mt->lmargin[mt->lmargincur];

	switch (n->type) {
	case ROFFT_HEAD:
		p->tcol->offset = mt->offset;
		p->tcol->rmargin = mt->offset + len;

		savelit = MANT_LITERAL & mt->fl;
		mt->fl &= ~MANT_LITERAL;

		if (n->child)
			print_man_node(p, mt, n->child, meta);

		if (savelit)
			mt->fl |= MANT_LITERAL;

		return 0;
	case ROFFT_BODY:
		p->tcol->offset = mt->offset + len;
		p->tcol->rmargin = p->maxrmargin;
		break;
	default:
		break;
	}

	return 1;
}

static void
post_IP(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_HEAD:
		term_flushln(p);
		p->flags &= ~TERMP_NOBREAK;
		p->trailspace = 0;
		p->tcol->rmargin = p->maxrmargin;
		break;
	case ROFFT_BODY:
		term_newln(p);
		p->tcol->offset = mt->offset;
		break;
	default:
		break;
	}
}

static int
pre_TP(DECL_ARGS)
{
	struct roffsu		 su;
	struct roff_node	*nn;
	int			 len, savelit;

	switch (n->type) {
	case ROFFT_HEAD:
		p->flags |= TERMP_NOBREAK | TERMP_BRTRSP;
		p->trailspace = 1;
		break;
	case ROFFT_BODY:
		p->flags |= TERMP_NOSPACE;
		break;
	case ROFFT_BLOCK:
		print_bvspace(p, n, mt->pardist);
		/* FALLTHROUGH */
	default:
		return 1;
	}

	/* Calculate offset. */

	if ((nn = n->parent->head->child) != NULL &&
	    nn->string != NULL && ! (NODE_LINE & nn->flags) &&
	    a2roffsu(nn->string, &su, SCALE_EN) != NULL) {
		len = term_hen(p, &su);
		if (len < 0 && (size_t)(-len) > mt->offset)
			len = -mt->offset;
		else if (len > SHRT_MAX)
			len = term_len(p, p->defindent);
		mt->lmargin[mt->lmargincur] = len;
	} else
		len = mt->lmargin[mt->lmargincur];

	switch (n->type) {
	case ROFFT_HEAD:
		p->tcol->offset = mt->offset;
		p->tcol->rmargin = mt->offset + len;

		savelit = MANT_LITERAL & mt->fl;
		mt->fl &= ~MANT_LITERAL;

		/* Don't print same-line elements. */
		nn = n->child;
		while (NULL != nn && 0 == (NODE_LINE & nn->flags))
			nn = nn->next;

		while (NULL != nn) {
			print_man_node(p, mt, nn, meta);
			nn = nn->next;
		}

		if (savelit)
			mt->fl |= MANT_LITERAL;
		return 0;
	case ROFFT_BODY:
		p->tcol->offset = mt->offset + len;
		p->tcol->rmargin = p->maxrmargin;
		p->trailspace = 0;
		p->flags &= ~(TERMP_NOBREAK | TERMP_BRTRSP);
		break;
	default:
		break;
	}

	return 1;
}

static void
post_TP(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_HEAD:
		term_flushln(p);
		break;
	case ROFFT_BODY:
		term_newln(p);
		p->tcol->offset = mt->offset;
		break;
	default:
		break;
	}
}

static int
pre_SS(DECL_ARGS)
{
	int	 i;

	switch (n->type) {
	case ROFFT_BLOCK:
		mt->fl &= ~MANT_LITERAL;
		mt->lmargin[mt->lmargincur] = term_len(p, p->defindent);
		mt->offset = term_len(p, p->defindent);

		/*
		 * No vertical space before the first subsection
		 * and after an empty subsection.
		 */

		do {
			n = n->prev;
		} while (n != NULL && n->tok >= MAN_TH &&
		    termacts[n->tok].flags & MAN_NOTEXT);
		if (n == NULL || n->type == ROFFT_COMMENT ||
		    (n->tok == MAN_SS && n->body->child == NULL))
			break;

		for (i = 0; i < mt->pardist; i++)
			term_vspace(p);
		break;
	case ROFFT_HEAD:
		term_fontrepl(p, TERMFONT_BOLD);
		p->tcol->offset = term_len(p, 3);
		p->tcol->rmargin = mt->offset;
		p->trailspace = mt->offset;
		p->flags |= TERMP_NOBREAK | TERMP_BRIND;
		break;
	case ROFFT_BODY:
		p->tcol->offset = mt->offset;
		p->tcol->rmargin = p->maxrmargin;
		p->trailspace = 0;
		p->flags &= ~(TERMP_NOBREAK | TERMP_BRIND);
		break;
	default:
		break;
	}

	return 1;
}

static void
post_SS(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_HEAD:
		term_newln(p);
		break;
	case ROFFT_BODY:
		term_newln(p);
		break;
	default:
		break;
	}
}

static int
pre_SH(DECL_ARGS)
{
	int	 i;

	switch (n->type) {
	case ROFFT_BLOCK:
		mt->fl &= ~MANT_LITERAL;
		mt->lmargin[mt->lmargincur] = term_len(p, p->defindent);
		mt->offset = term_len(p, p->defindent);

		/*
		 * No vertical space before the first section
		 * and after an empty section.
		 */

		do {
			n = n->prev;
		} while (n != NULL && n->tok >= MAN_TH &&
		    termacts[n->tok].flags & MAN_NOTEXT);
		if (n == NULL || n->type == ROFFT_COMMENT ||
		    (n->tok == MAN_SH && n->body->child == NULL))
			break;

		for (i = 0; i < mt->pardist; i++)
			term_vspace(p);
		break;
	case ROFFT_HEAD:
		term_fontrepl(p, TERMFONT_BOLD);
		p->tcol->offset = 0;
		p->tcol->rmargin = mt->offset;
		p->trailspace = mt->offset;
		p->flags |= TERMP_NOBREAK | TERMP_BRIND;
		break;
	case ROFFT_BODY:
		p->tcol->offset = mt->offset;
		p->tcol->rmargin = p->maxrmargin;
		p->trailspace = 0;
		p->flags &= ~(TERMP_NOBREAK | TERMP_BRIND);
		break;
	default:
		break;
	}

	return 1;
}

static void
post_SH(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_HEAD:
		term_newln(p);
		break;
	case ROFFT_BODY:
		term_newln(p);
		break;
	default:
		break;
	}
}

static int
pre_RS(DECL_ARGS)
{
	struct roffsu	 su;

	switch (n->type) {
	case ROFFT_BLOCK:
		term_newln(p);
		return 1;
	case ROFFT_HEAD:
		return 0;
	default:
		break;
	}

	n = n->parent->head;
	n->aux = SHRT_MAX + 1;
	if (n->child == NULL)
		n->aux = mt->lmargin[mt->lmargincur];
	else if (a2roffsu(n->child->string, &su, SCALE_EN) != NULL)
		n->aux = term_hen(p, &su);
	if (n->aux < 0 && (size_t)(-n->aux) > mt->offset)
		n->aux = -mt->offset;
	else if (n->aux > SHRT_MAX)
		n->aux = term_len(p, p->defindent);

	mt->offset += n->aux;
	p->tcol->offset = mt->offset;
	p->tcol->rmargin = p->maxrmargin;

	if (++mt->lmarginsz < MAXMARGINS)
		mt->lmargincur = mt->lmarginsz;

	mt->lmargin[mt->lmargincur] = term_len(p, p->defindent);
	return 1;
}

static void
post_RS(DECL_ARGS)
{

	switch (n->type) {
	case ROFFT_BLOCK:
		return;
	case ROFFT_HEAD:
		return;
	default:
		term_newln(p);
		break;
	}

	mt->offset -= n->parent->head->aux;
	p->tcol->offset = mt->offset;

	if (--mt->lmarginsz < MAXMARGINS)
		mt->lmargincur = mt->lmarginsz;
}

static int
pre_UR(DECL_ARGS)
{

	return n->type != ROFFT_HEAD;
}

static void
post_UR(DECL_ARGS)
{

	if (n->type != ROFFT_BLOCK)
		return;

	term_word(p, "<");
	p->flags |= TERMP_NOSPACE;

	if (NULL != n->child->child)
		print_man_node(p, mt, n->child->child, meta);

	p->flags |= TERMP_NOSPACE;
	term_word(p, ">");
}

static void
print_man_node(DECL_ARGS)
{
	int		 c;

	switch (n->type) {
	case ROFFT_TEXT:
		/*
		 * If we have a blank line, output a vertical space.
		 * If we have a space as the first character, break
		 * before printing the line's data.
		 */
		if (*n->string == '\0') {
			if (p->flags & TERMP_NONEWLINE)
				term_newln(p);
			else
				term_vspace(p);
			return;
		} else if (*n->string == ' ' && n->flags & NODE_LINE &&
		    (p->flags & TERMP_NONEWLINE) == 0)
			term_newln(p);

		term_word(p, n->string);
		goto out;
	case ROFFT_COMMENT:
		return;
	case ROFFT_EQN:
		if ( ! (n->flags & NODE_LINE))
			p->flags |= TERMP_NOSPACE;
		term_eqn(p, n->eqn);
		if (n->next != NULL && ! (n->next->flags & NODE_LINE))
			p->flags |= TERMP_NOSPACE;
		return;
	case ROFFT_TBL:
		if (p->tbl.cols == NULL)
			term_vspace(p);
		term_tbl(p, n->span);
		return;
	default:
		break;
	}

	if (n->tok < ROFF_MAX) {
		roff_term_pre(p, n);
		return;
	}

	assert(n->tok >= MAN_TH && n->tok <= MAN_MAX);
	if ( ! (MAN_NOTEXT & termacts[n->tok].flags))
		term_fontrepl(p, TERMFONT_NONE);

	c = 1;
	if (termacts[n->tok].pre)
		c = (*termacts[n->tok].pre)(p, mt, n, meta);

	if (c && n->child)
		print_man_nodelist(p, mt, n->child, meta);

	if (termacts[n->tok].post)
		(*termacts[n->tok].post)(p, mt, n, meta);
	if ( ! (MAN_NOTEXT & termacts[n->tok].flags))
		term_fontrepl(p, TERMFONT_NONE);

out:
	/*
	 * If we're in a literal context, make sure that words
	 * together on the same line stay together.  This is a
	 * POST-printing call, so we check the NEXT word.  Since
	 * -man doesn't have nested macros, we don't need to be
	 * more specific than this.
	 */
	if (mt->fl & MANT_LITERAL &&
	    ! (p->flags & (TERMP_NOBREAK | TERMP_NONEWLINE)) &&
	    (n->next == NULL || n->next->flags & NODE_LINE)) {
		p->flags |= TERMP_BRNEVER | TERMP_NOSPACE;
		if (n->string != NULL && *n->string != '\0')
			term_flushln(p);
		else
			term_newln(p);
		p->flags &= ~TERMP_BRNEVER;
		if (p->tcol->rmargin < p->maxrmargin &&
		    n->parent->tok == MAN_HP) {
			p->tcol->offset = p->tcol->rmargin;
			p->tcol->rmargin = p->maxrmargin;
		}
	}
	if (NODE_EOS & n->flags)
		p->flags |= TERMP_SENTENCE;
}


static void
print_man_nodelist(DECL_ARGS)
{

	while (n != NULL) {
		print_man_node(p, mt, n, meta);
		n = n->next;
	}
}

static void
print_man_foot(struct termp *p, const struct roff_meta *meta)
{
	char			*title;
	size_t			 datelen, titlen;

	assert(meta->title);
	assert(meta->msec);
	assert(meta->date);

	term_fontrepl(p, TERMFONT_NONE);

	if (meta->hasbody)
		term_vspace(p);

	/*
	 * Temporary, undocumented option to imitate mdoc(7) output.
	 * In the bottom right corner, use the operating system
	 * instead of the title.
	 */

	if ( ! p->mdocstyle) {
		if (meta->hasbody) {
			term_vspace(p);
			term_vspace(p);
		}
		mandoc_asprintf(&title, "%s(%s)",
		    meta->title, meta->msec);
	} else if (meta->os) {
		title = mandoc_strdup(meta->os);
	} else {
		title = mandoc_strdup("");
	}
	datelen = term_strlen(p, meta->date);

	/* Bottom left corner: operating system. */

	p->flags |= TERMP_NOSPACE | TERMP_NOBREAK;
	p->trailspace = 1;
	p->tcol->offset = 0;
	p->tcol->rmargin = p->maxrmargin > datelen ?
	    (p->maxrmargin + term_len(p, 1) - datelen) / 2 : 0;

	if (meta->os)
		term_word(p, meta->os);
	term_flushln(p);

	/* At the bottom in the middle: manual date. */

	p->tcol->offset = p->tcol->rmargin;
	titlen = term_strlen(p, title);
	p->tcol->rmargin = p->maxrmargin > titlen ?
	    p->maxrmargin - titlen : 0;
	p->flags |= TERMP_NOSPACE;

	term_word(p, meta->date);
	term_flushln(p);

	/* Bottom right corner: manual title and section. */

	p->flags &= ~TERMP_NOBREAK;
	p->flags |= TERMP_NOSPACE;
	p->trailspace = 0;
	p->tcol->offset = p->tcol->rmargin;
	p->tcol->rmargin = p->maxrmargin;

	term_word(p, title);
	term_flushln(p);

	/*
	 * Reset the terminal state for more output after the footer:
	 * Some output modes, in particular PostScript and PDF, print
	 * the header and the footer into a buffer such that it can be
	 * reused for multiple output pages, then go on to format the
	 * main text.
	 */

        p->tcol->offset = 0;
        p->flags = 0;

	free(title);
}

static void
print_man_head(struct termp *p, const struct roff_meta *meta)
{
	const char		*volume;
	char			*title;
	size_t			 vollen, titlen;

	assert(meta->title);
	assert(meta->msec);

	volume = NULL == meta->vol ? "" : meta->vol;
	vollen = term_strlen(p, volume);

	/* Top left corner: manual title and section. */

	mandoc_asprintf(&title, "%s(%s)", meta->title, meta->msec);
	titlen = term_strlen(p, title);

	p->flags |= TERMP_NOBREAK | TERMP_NOSPACE;
	p->trailspace = 1;
	p->tcol->offset = 0;
	p->tcol->rmargin = 2 * (titlen+1) + vollen < p->maxrmargin ?
	    (p->maxrmargin - vollen + term_len(p, 1)) / 2 :
	    vollen < p->maxrmargin ? p->maxrmargin - vollen : 0;

	term_word(p, title);
	term_flushln(p);

	/* At the top in the middle: manual volume. */

	p->flags |= TERMP_NOSPACE;
	p->tcol->offset = p->tcol->rmargin;
	p->tcol->rmargin = p->tcol->offset + vollen + titlen <
	    p->maxrmargin ?  p->maxrmargin - titlen : p->maxrmargin;

	term_word(p, volume);
	term_flushln(p);

	/* Top right corner: title and section, again. */

	p->flags &= ~TERMP_NOBREAK;
	p->trailspace = 0;
	if (p->tcol->rmargin + titlen <= p->maxrmargin) {
		p->flags |= TERMP_NOSPACE;
		p->tcol->offset = p->tcol->rmargin;
		p->tcol->rmargin = p->maxrmargin;
		term_word(p, title);
		term_flushln(p);
	}

	p->flags &= ~TERMP_NOSPACE;
	p->tcol->offset = 0;
	p->tcol->rmargin = p->maxrmargin;

	/*
	 * Groff prints three blank lines before the content.
	 * Do the same, except in the temporary, undocumented
	 * mode imitating mdoc(7) output.
	 */

	term_vspace(p);
	if ( ! p->mdocstyle) {
		term_vspace(p);
		term_vspace(p);
	}
	free(title);
}
