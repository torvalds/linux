/*	$Id: mdoc_macro.c,v 1.224 2017/05/30 16:22:03 schwarze Exp $ */
/*
 * Copyright (c) 2008-2012 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2010, 2012-2017 Ingo Schwarze <schwarze@openbsd.org>
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mandoc.h"
#include "roff.h"
#include "mdoc.h"
#include "libmandoc.h"
#include "roff_int.h"
#include "libmdoc.h"

static	void		blk_full(MACRO_PROT_ARGS);
static	void		blk_exp_close(MACRO_PROT_ARGS);
static	void		blk_part_exp(MACRO_PROT_ARGS);
static	void		blk_part_imp(MACRO_PROT_ARGS);
static	void		ctx_synopsis(MACRO_PROT_ARGS);
static	void		in_line_eoln(MACRO_PROT_ARGS);
static	void		in_line_argn(MACRO_PROT_ARGS);
static	void		in_line(MACRO_PROT_ARGS);
static	void		phrase_ta(MACRO_PROT_ARGS);

static	void		append_delims(struct roff_man *, int, int *, char *);
static	void		dword(struct roff_man *, int, int, const char *,
				enum mdelim, int);
static	int		find_pending(struct roff_man *, enum roff_tok,
				int, int, struct roff_node *);
static	int		lookup(struct roff_man *, int, int, int, const char *);
static	int		macro_or_word(MACRO_PROT_ARGS, int);
static	void		break_intermediate(struct roff_node *,
				struct roff_node *);
static	int		parse_rest(struct roff_man *, enum roff_tok,
				int, int *, char *);
static	enum roff_tok	rew_alt(enum roff_tok);
static	void		rew_elem(struct roff_man *, enum roff_tok);
static	void		rew_last(struct roff_man *, const struct roff_node *);
static	void		rew_pending(struct roff_man *,
				const struct roff_node *);

const	struct mdoc_macro __mdoc_macros[MDOC_MAX - MDOC_Dd] = {
	{ in_line_eoln, MDOC_PROLOGUE }, /* Dd */
	{ in_line_eoln, MDOC_PROLOGUE }, /* Dt */
	{ in_line_eoln, MDOC_PROLOGUE }, /* Os */
	{ blk_full, MDOC_PARSED | MDOC_JOIN }, /* Sh */
	{ blk_full, MDOC_PARSED | MDOC_JOIN }, /* Ss */
	{ in_line_eoln, 0 }, /* Pp */
	{ blk_part_imp, MDOC_PARSED | MDOC_JOIN }, /* D1 */
	{ blk_part_imp, MDOC_PARSED | MDOC_JOIN }, /* Dl */
	{ blk_full, MDOC_EXPLICIT }, /* Bd */
	{ blk_exp_close, MDOC_EXPLICIT | MDOC_JOIN }, /* Ed */
	{ blk_full, MDOC_EXPLICIT }, /* Bl */
	{ blk_exp_close, MDOC_EXPLICIT | MDOC_JOIN }, /* El */
	{ blk_full, MDOC_PARSED | MDOC_JOIN }, /* It */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Ad */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* An */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_IGNDELIM | MDOC_JOIN }, /* Ap */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Ar */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Cd */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Cm */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Dv */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Er */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Ev */
	{ in_line_eoln, 0 }, /* Ex */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Fa */
	{ in_line_eoln, 0 }, /* Fd */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Fl */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Fn */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Ft */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Ic */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* In */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Li */
	{ blk_full, MDOC_JOIN }, /* Nd */
	{ ctx_synopsis, MDOC_CALLABLE | MDOC_PARSED }, /* Nm */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED }, /* Op */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Ot */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Pa */
	{ in_line_eoln, 0 }, /* Rv */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* St */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Va */
	{ ctx_synopsis, MDOC_CALLABLE | MDOC_PARSED }, /* Vt */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* Xr */
	{ in_line_eoln, MDOC_JOIN }, /* %A */
	{ in_line_eoln, MDOC_JOIN }, /* %B */
	{ in_line_eoln, MDOC_JOIN }, /* %D */
	{ in_line_eoln, MDOC_JOIN }, /* %I */
	{ in_line_eoln, MDOC_JOIN }, /* %J */
	{ in_line_eoln, 0 }, /* %N */
	{ in_line_eoln, MDOC_JOIN }, /* %O */
	{ in_line_eoln, 0 }, /* %P */
	{ in_line_eoln, MDOC_JOIN }, /* %R */
	{ in_line_eoln, MDOC_JOIN }, /* %T */
	{ in_line_eoln, 0 }, /* %V */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Ac */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_EXPLICIT | MDOC_JOIN }, /* Ao */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Aq */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* At */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Bc */
	{ blk_full, MDOC_EXPLICIT }, /* Bf */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_EXPLICIT | MDOC_JOIN }, /* Bo */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Bq */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* Bsx */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* Bx */
	{ in_line_eoln, 0 }, /* Db */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Dc */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_EXPLICIT | MDOC_JOIN }, /* Do */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Dq */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED | MDOC_EXPLICIT }, /* Ec */
	{ blk_exp_close, MDOC_EXPLICIT | MDOC_JOIN }, /* Ef */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Em */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED | MDOC_EXPLICIT }, /* Eo */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* Fx */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Ms */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* No */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_IGNDELIM | MDOC_JOIN }, /* Ns */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* Nx */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* Ox */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Pc */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED | MDOC_IGNDELIM }, /* Pf */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_EXPLICIT | MDOC_JOIN }, /* Po */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Pq */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Qc */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Ql */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_EXPLICIT | MDOC_JOIN }, /* Qo */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Qq */
	{ blk_exp_close, MDOC_EXPLICIT | MDOC_JOIN }, /* Re */
	{ blk_full, MDOC_EXPLICIT }, /* Rs */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Sc */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_EXPLICIT | MDOC_JOIN }, /* So */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Sq */
	{ in_line_argn, 0 }, /* Sm */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Sx */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Sy */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Tn */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Ux */
	{ blk_exp_close, MDOC_EXPLICIT | MDOC_CALLABLE | MDOC_PARSED }, /* Xc */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED | MDOC_EXPLICIT }, /* Xo */
	{ blk_full, MDOC_EXPLICIT | MDOC_CALLABLE }, /* Fo */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Fc */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_EXPLICIT | MDOC_JOIN }, /* Oo */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Oc */
	{ blk_full, MDOC_EXPLICIT }, /* Bk */
	{ blk_exp_close, MDOC_EXPLICIT | MDOC_JOIN }, /* Ek */
	{ in_line_eoln, 0 }, /* Bt */
	{ in_line_eoln, 0 }, /* Hf */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Fr */
	{ in_line_eoln, 0 }, /* Ud */
	{ in_line, 0 }, /* Lb */
	{ in_line_eoln, 0 }, /* Lp */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Lk */
	{ in_line, MDOC_CALLABLE | MDOC_PARSED }, /* Mt */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Brq */
	{ blk_part_exp, MDOC_CALLABLE | MDOC_PARSED |
			MDOC_EXPLICIT | MDOC_JOIN }, /* Bro */
	{ blk_exp_close, MDOC_CALLABLE | MDOC_PARSED |
			 MDOC_EXPLICIT | MDOC_JOIN }, /* Brc */
	{ in_line_eoln, MDOC_JOIN }, /* %C */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* Es */
	{ blk_part_imp, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* En */
	{ in_line_argn, MDOC_CALLABLE | MDOC_PARSED }, /* Dx */
	{ in_line_eoln, MDOC_JOIN }, /* %Q */
	{ in_line_eoln, 0 }, /* %U */
	{ phrase_ta, MDOC_CALLABLE | MDOC_PARSED | MDOC_JOIN }, /* Ta */
};
const	struct mdoc_macro *const mdoc_macros = __mdoc_macros - MDOC_Dd;


/*
 * This is called at the end of parsing.  It must traverse up the tree,
 * closing out open [implicit] scopes.  Obviously, open explicit scopes
 * are errors.
 */
void
mdoc_endparse(struct roff_man *mdoc)
{
	struct roff_node *n;

	/* Scan for open explicit scopes. */

	n = mdoc->last->flags & NODE_VALID ?
	    mdoc->last->parent : mdoc->last;

	for ( ; n; n = n->parent)
		if (n->type == ROFFT_BLOCK &&
		    mdoc_macros[n->tok].flags & MDOC_EXPLICIT)
			mandoc_msg(MANDOCERR_BLK_NOEND, mdoc->parse,
			    n->line, n->pos, roff_name[n->tok]);

	/* Rewind to the first. */

	rew_last(mdoc, mdoc->first);
	mdoc_state_reset(mdoc);
}

/*
 * Look up the macro at *p called by "from",
 * or as a line macro if from == TOKEN_NONE.
 */
static int
lookup(struct roff_man *mdoc, int from, int line, int ppos, const char *p)
{
	enum roff_tok	 res;

	if (mdoc->flags & MDOC_PHRASEQF) {
		mdoc->flags &= ~MDOC_PHRASEQF;
		return TOKEN_NONE;
	}
	if (from == TOKEN_NONE || mdoc_macros[from].flags & MDOC_PARSED) {
		res = roffhash_find(mdoc->mdocmac, p, 0);
		if (res != TOKEN_NONE) {
			if (mdoc_macros[res].flags & MDOC_CALLABLE)
				return res;
			mandoc_msg(MANDOCERR_MACRO_CALL,
			    mdoc->parse, line, ppos, p);
		}
	}
	return TOKEN_NONE;
}

/*
 * Rewind up to and including a specific node.
 */
static void
rew_last(struct roff_man *mdoc, const struct roff_node *to)
{

	if (to->flags & NODE_VALID)
		return;

	while (mdoc->last != to) {
		mdoc_state(mdoc, mdoc->last);
		mdoc->last->flags |= NODE_VALID | NODE_ENDED;
		mdoc->last = mdoc->last->parent;
	}
	mdoc_state(mdoc, mdoc->last);
	mdoc->last->flags |= NODE_VALID | NODE_ENDED;
	mdoc->next = ROFF_NEXT_SIBLING;
}

/*
 * Rewind up to a specific block, including all blocks that broke it.
 */
static void
rew_pending(struct roff_man *mdoc, const struct roff_node *n)
{

	for (;;) {
		rew_last(mdoc, n);

		if (mdoc->last == n) {
			switch (n->type) {
			case ROFFT_HEAD:
				roff_body_alloc(mdoc, n->line, n->pos,
				    n->tok);
				break;
			case ROFFT_BLOCK:
				break;
			default:
				return;
			}
			if ( ! (n->flags & NODE_BROKEN))
				return;
		} else
			n = mdoc->last;

		for (;;) {
			if ((n = n->parent) == NULL)
				return;

			if (n->type == ROFFT_BLOCK ||
			    n->type == ROFFT_HEAD) {
				if (n->flags & NODE_ENDED)
					break;
				else
					return;
			}
		}
	}
}

/*
 * For a block closing macro, return the corresponding opening one.
 * Otherwise, return the macro itself.
 */
static enum roff_tok
rew_alt(enum roff_tok tok)
{
	switch (tok) {
	case MDOC_Ac:
		return MDOC_Ao;
	case MDOC_Bc:
		return MDOC_Bo;
	case MDOC_Brc:
		return MDOC_Bro;
	case MDOC_Dc:
		return MDOC_Do;
	case MDOC_Ec:
		return MDOC_Eo;
	case MDOC_Ed:
		return MDOC_Bd;
	case MDOC_Ef:
		return MDOC_Bf;
	case MDOC_Ek:
		return MDOC_Bk;
	case MDOC_El:
		return MDOC_Bl;
	case MDOC_Fc:
		return MDOC_Fo;
	case MDOC_Oc:
		return MDOC_Oo;
	case MDOC_Pc:
		return MDOC_Po;
	case MDOC_Qc:
		return MDOC_Qo;
	case MDOC_Re:
		return MDOC_Rs;
	case MDOC_Sc:
		return MDOC_So;
	case MDOC_Xc:
		return MDOC_Xo;
	default:
		return tok;
	}
}

static void
rew_elem(struct roff_man *mdoc, enum roff_tok tok)
{
	struct roff_node *n;

	n = mdoc->last;
	if (n->type != ROFFT_ELEM)
		n = n->parent;
	assert(n->type == ROFFT_ELEM);
	assert(tok == n->tok);
	rew_last(mdoc, n);
}

static void
break_intermediate(struct roff_node *n, struct roff_node *breaker)
{
	if (n != breaker &&
	    n->type != ROFFT_BLOCK && n->type != ROFFT_HEAD &&
	    (n->type != ROFFT_BODY || n->end != ENDBODY_NOT))
		n = n->parent;
	while (n != breaker) {
		if ( ! (n->flags & NODE_VALID))
			n->flags |= NODE_BROKEN;
		n = n->parent;
	}
}

/*
 * If there is an open sub-block of the target requiring
 * explicit close-out, postpone closing out the target until
 * the rew_pending() call closing out the sub-block.
 */
static int
find_pending(struct roff_man *mdoc, enum roff_tok tok, int line, int ppos,
	struct roff_node *target)
{
	struct roff_node	*n;
	int			 irc;

	if (target->flags & NODE_VALID)
		return 0;

	irc = 0;
	for (n = mdoc->last; n != NULL && n != target; n = n->parent) {
		if (n->flags & NODE_ENDED)
			continue;
		if (n->type == ROFFT_BLOCK &&
		    mdoc_macros[n->tok].flags & MDOC_EXPLICIT) {
			irc = 1;
			break_intermediate(mdoc->last, target);
			if (target->type == ROFFT_HEAD)
				target->flags |= NODE_ENDED;
			else if ( ! (target->flags & NODE_ENDED)) {
				mandoc_vmsg(MANDOCERR_BLK_NEST,
				    mdoc->parse, line, ppos,
				    "%s breaks %s", roff_name[tok],
				    roff_name[n->tok]);
				mdoc_endbody_alloc(mdoc, line, ppos,
				    tok, target);
			}
		}
	}
	return irc;
}

/*
 * Allocate a word and check whether it's punctuation or not.
 * Punctuation consists of those tokens found in mdoc_isdelim().
 */
static void
dword(struct roff_man *mdoc, int line, int col, const char *p,
		enum mdelim d, int may_append)
{

	if (d == DELIM_MAX)
		d = mdoc_isdelim(p);

	if (may_append &&
	    ! (mdoc->flags & (MDOC_SYNOPSIS | MDOC_KEEP | MDOC_SMOFF)) &&
	    d == DELIM_NONE && mdoc->last->type == ROFFT_TEXT &&
	    mdoc_isdelim(mdoc->last->string) == DELIM_NONE) {
		roff_word_append(mdoc, p);
		return;
	}

	roff_word_alloc(mdoc, line, col, p);

	/*
	 * If the word consists of a bare delimiter,
	 * flag the new node accordingly,
	 * unless doing so was vetoed by the invoking macro.
	 * Always clear the veto, it is only valid for one word.
	 */

	if (d == DELIM_OPEN)
		mdoc->last->flags |= NODE_DELIMO;
	else if (d == DELIM_CLOSE &&
	    ! (mdoc->flags & MDOC_NODELIMC) &&
	    mdoc->last->parent->tok != MDOC_Fd)
		mdoc->last->flags |= NODE_DELIMC;
	mdoc->flags &= ~MDOC_NODELIMC;
}

static void
append_delims(struct roff_man *mdoc, int line, int *pos, char *buf)
{
	char		*p;
	int		 la;

	if (buf[*pos] == '\0')
		return;

	for (;;) {
		la = *pos;
		if (mdoc_args(mdoc, line, pos, buf, TOKEN_NONE, &p) ==
		    ARGS_EOLN)
			break;
		dword(mdoc, line, la, p, DELIM_MAX, 1);

		/*
		 * If we encounter end-of-sentence symbols, then trigger
		 * the double-space.
		 *
		 * XXX: it's easy to allow this to propagate outward to
		 * the last symbol, such that `. )' will cause the
		 * correct double-spacing.  However, (1) groff isn't
		 * smart enough to do this and (2) it would require
		 * knowing which symbols break this behaviour, for
		 * example, `.  ;' shouldn't propagate the double-space.
		 */

		if (mandoc_eos(p, strlen(p)))
			mdoc->last->flags |= NODE_EOS;
	}
}

/*
 * Parse one word.
 * If it is a macro, call it and return 1.
 * Otherwise, allocate it and return 0.
 */
static int
macro_or_word(MACRO_PROT_ARGS, int parsed)
{
	char		*p;
	int		 ntok;

	p = buf + ppos;
	ntok = TOKEN_NONE;
	if (*p == '"')
		p++;
	else if (parsed && ! (mdoc->flags & MDOC_PHRASELIT))
		ntok = lookup(mdoc, tok, line, ppos, p);

	if (ntok == TOKEN_NONE) {
		dword(mdoc, line, ppos, p, DELIM_MAX, tok == TOKEN_NONE ||
		    mdoc_macros[tok].flags & MDOC_JOIN);
		return 0;
	} else {
		if (tok != TOKEN_NONE &&
		    mdoc_macros[tok].fp == in_line_eoln)
			rew_elem(mdoc, tok);
		mdoc_macro(mdoc, ntok, line, ppos, pos, buf);
		if (tok == TOKEN_NONE)
			append_delims(mdoc, line, pos, buf);
		return 1;
	}
}

/*
 * Close out block partial/full explicit.
 */
static void
blk_exp_close(MACRO_PROT_ARGS)
{
	struct roff_node *body;		/* Our own body. */
	struct roff_node *endbody;	/* Our own end marker. */
	struct roff_node *itblk;	/* An It block starting later. */
	struct roff_node *later;	/* A sub-block starting later. */
	struct roff_node *n;		/* Search back to our block. */
	struct roff_node *target;	/* For find_pending(). */

	int		 j, lastarg, maxargs, nl, pending;
	enum margserr	 ac;
	enum roff_tok	 atok, ntok;
	char		*p;

	nl = MDOC_NEWLINE & mdoc->flags;

	switch (tok) {
	case MDOC_Ec:
		maxargs = 1;
		break;
	case MDOC_Ek:
		mdoc->flags &= ~MDOC_KEEP;
		/* FALLTHROUGH */
	default:
		maxargs = 0;
		break;
	}

	/* Search backwards for the beginning of our own body. */

	atok = rew_alt(tok);
	body = NULL;
	for (n = mdoc->last; n; n = n->parent) {
		if (n->flags & NODE_ENDED || n->tok != atok ||
		    n->type != ROFFT_BODY || n->end != ENDBODY_NOT)
			continue;
		body = n;
		break;
	}

	/*
	 * Search backwards for beginnings of blocks,
	 * both of our own and of pending sub-blocks.
	 */

	endbody = itblk = later = NULL;
	for (n = mdoc->last; n; n = n->parent) {
		if (n->flags & NODE_ENDED)
			continue;

		/*
		 * Mismatching end macros can never break anything
		 * and we only care about the breaking of BLOCKs.
		 */

		if (body == NULL || n->type != ROFFT_BLOCK)
			continue;

		/*
		 * SYNOPSIS name blocks can not be broken themselves,
		 * but they do get broken together with a broken child.
		 */

		if (n->tok == MDOC_Nm) {
			if (later != NULL)
				n->flags |= NODE_BROKEN | NODE_ENDED;
			continue;
		}

		if (n->tok == MDOC_It) {
			itblk = n;
			continue;
		}

		if (atok == n->tok) {

			/*
			 * Found the start of our own block.
			 * When there is no pending sub block,
			 * just proceed to closing out.
			 */

			if (later == NULL ||
			    (tok == MDOC_El && itblk == NULL))
				break;

			/*
			 * When there is a pending sub block, postpone
			 * closing out the current block until the
			 * rew_pending() closing out the sub-block.
			 * Mark the place where the formatting - but not
			 * the scope - of the current block ends.
			 */

			mandoc_vmsg(MANDOCERR_BLK_NEST, mdoc->parse,
			    line, ppos, "%s breaks %s",
			    roff_name[atok], roff_name[later->tok]);

			endbody = mdoc_endbody_alloc(mdoc, line, ppos,
			    atok, body);

			if (tok == MDOC_El)
				itblk->flags |= NODE_ENDED | NODE_BROKEN;

			/*
			 * If a block closing macro taking arguments
			 * breaks another block, put the arguments
			 * into the end marker.
			 */

			if (maxargs)
				mdoc->next = ROFF_NEXT_CHILD;
			break;
		}

		/*
		 * Explicit blocks close out description lines, but
		 * even those can get broken together with a child.
		 */

		if (n->tok == MDOC_Nd) {
			if (later != NULL)
				n->flags |= NODE_BROKEN | NODE_ENDED;
			else
				rew_last(mdoc, n);
			continue;
		}

		/* Breaking an open sub block. */

		break_intermediate(mdoc->last, body);
		n->flags |= NODE_BROKEN;
		if (later == NULL)
			later = n;
	}

	if (body == NULL) {
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, mdoc->parse,
		    line, ppos, roff_name[tok]);
		if (maxargs && endbody == NULL) {
			/*
			 * Stray .Ec without previous .Eo:
			 * Break the output line, keep the arguments.
			 */
			roff_elem_alloc(mdoc, line, ppos, ROFF_br);
			rew_elem(mdoc, ROFF_br);
		}
	} else if (endbody == NULL) {
		rew_last(mdoc, body);
		if (maxargs)
			mdoc_tail_alloc(mdoc, line, ppos, atok);
	}

	if ( ! (mdoc_macros[tok].flags & MDOC_PARSED)) {
		if (buf[*pos] != '\0')
			mandoc_vmsg(MANDOCERR_ARG_SKIP,
			    mdoc->parse, line, ppos,
			    "%s %s", roff_name[tok],
			    buf + *pos);
		if (endbody == NULL && n != NULL)
			rew_pending(mdoc, n);
		return;
	}

	if (endbody != NULL)
		n = endbody;

	ntok = TOKEN_NONE;
	for (j = 0; ; j++) {
		lastarg = *pos;

		if (j == maxargs && n != NULL)
			rew_last(mdoc, n);

		ac = mdoc_args(mdoc, line, pos, buf, tok, &p);
		if (ac == ARGS_PUNCT || ac == ARGS_EOLN)
			break;

		ntok = lookup(mdoc, tok, line, lastarg, p);

		if (ntok == TOKEN_NONE) {
			dword(mdoc, line, lastarg, p, DELIM_MAX,
			    MDOC_JOIN & mdoc_macros[tok].flags);
			continue;
		}

		if (n != NULL)
			rew_last(mdoc, n);
		mdoc->flags &= ~MDOC_NEWLINE;
		mdoc_macro(mdoc, ntok, line, lastarg, pos, buf);
		break;
	}

	if (n != NULL) {
		pending = 0;
		if (ntok != TOKEN_NONE && n->flags & NODE_BROKEN) {
			target = n;
			do
				target = target->parent;
			while ( ! (target->flags & NODE_ENDED));
			pending = find_pending(mdoc, ntok, line, ppos, target);
		}
		if ( ! pending)
			rew_pending(mdoc, n);
	}
	if (nl)
		append_delims(mdoc, line, pos, buf);
}

static void
in_line(MACRO_PROT_ARGS)
{
	int		 la, scope, cnt, firstarg, mayopen, nc, nl;
	enum roff_tok	 ntok;
	enum margserr	 ac;
	enum mdelim	 d;
	struct mdoc_arg	*arg;
	char		*p;

	nl = MDOC_NEWLINE & mdoc->flags;

	/*
	 * Whether we allow ignored elements (those without content,
	 * usually because of reserved words) to squeak by.
	 */

	switch (tok) {
	case MDOC_An:
	case MDOC_Ar:
	case MDOC_Fl:
	case MDOC_Mt:
	case MDOC_Nm:
	case MDOC_Pa:
		nc = 1;
		break;
	default:
		nc = 0;
		break;
	}

	mdoc_argv(mdoc, line, tok, &arg, pos, buf);

	d = DELIM_NONE;
	firstarg = 1;
	mayopen = 1;
	for (cnt = scope = 0;; ) {
		la = *pos;
		ac = mdoc_args(mdoc, line, pos, buf, tok, &p);

		/*
		 * At the end of a macro line,
		 * opening delimiters do not suppress spacing.
		 */

		if (ac == ARGS_EOLN) {
			if (d == DELIM_OPEN)
				mdoc->last->flags &= ~NODE_DELIMO;
			break;
		}

		/*
		 * The rest of the macro line is only punctuation,
		 * to be handled by append_delims().
		 * If there were no other arguments,
		 * do not allow the first one to suppress spacing,
		 * even if it turns out to be a closing one.
		 */

		if (ac == ARGS_PUNCT) {
			if (cnt == 0 && (nc == 0 || tok == MDOC_An))
				mdoc->flags |= MDOC_NODELIMC;
			break;
		}

		ntok = (tok == MDOC_Fn && !cnt) ?
		    TOKEN_NONE : lookup(mdoc, tok, line, la, p);

		/*
		 * In this case, we've located a submacro and must
		 * execute it.  Close out scope, if open.  If no
		 * elements have been generated, either create one (nc)
		 * or raise a warning.
		 */

		if (ntok != TOKEN_NONE) {
			if (scope)
				rew_elem(mdoc, tok);
			if (nc && ! cnt) {
				mdoc_elem_alloc(mdoc, line, ppos, tok, arg);
				rew_last(mdoc, mdoc->last);
			} else if ( ! nc && ! cnt) {
				mdoc_argv_free(arg);
				mandoc_msg(MANDOCERR_MACRO_EMPTY,
				    mdoc->parse, line, ppos,
				    roff_name[tok]);
			}
			mdoc_macro(mdoc, ntok, line, la, pos, buf);
			if (nl)
				append_delims(mdoc, line, pos, buf);
			return;
		}

		/*
		 * Handle punctuation.  Set up our scope, if a word;
		 * rewind the scope, if a delimiter; then append the word.
		 */

		if ((d = mdoc_isdelim(p)) != DELIM_NONE) {
			/*
			 * If we encounter closing punctuation, no word
			 * has been emitted, no scope is open, and we're
			 * allowed to have an empty element, then start
			 * a new scope.
			 */
			if ((d == DELIM_CLOSE ||
			     (d == DELIM_MIDDLE && tok == MDOC_Fl)) &&
			    !cnt && !scope && nc && mayopen) {
				mdoc_elem_alloc(mdoc, line, ppos, tok, arg);
				scope = 1;
				cnt++;
				if (tok == MDOC_Nm)
					mayopen = 0;
			}
			/*
			 * Close out our scope, if one is open, before
			 * any punctuation.
			 */
			if (scope && tok != MDOC_Lk) {
				rew_elem(mdoc, tok);
				scope = 0;
				if (tok == MDOC_Fn)
					mayopen = 0;
			}
		} else if (mayopen && !scope) {
			mdoc_elem_alloc(mdoc, line, ppos, tok, arg);
			scope = 1;
			cnt++;
		}

		dword(mdoc, line, la, p, d,
		    mdoc_macros[tok].flags & MDOC_JOIN);

		/*
		 * If the first argument is a closing delimiter,
		 * do not suppress spacing before it.
		 */

		if (firstarg && d == DELIM_CLOSE && !nc)
			mdoc->last->flags &= ~NODE_DELIMC;
		firstarg = 0;

		/*
		 * `Fl' macros have their scope re-opened with each new
		 * word so that the `-' can be added to each one without
		 * having to parse out spaces.
		 */
		if (scope && tok == MDOC_Fl) {
			rew_elem(mdoc, tok);
			scope = 0;
		}
	}

	if (scope && tok != MDOC_Lk) {
		rew_elem(mdoc, tok);
		scope = 0;
	}

	/*
	 * If no elements have been collected and we're allowed to have
	 * empties (nc), open a scope and close it out.  Otherwise,
	 * raise a warning.
	 */

	if ( ! cnt) {
		if (nc) {
			mdoc_elem_alloc(mdoc, line, ppos, tok, arg);
			rew_last(mdoc, mdoc->last);
		} else {
			mdoc_argv_free(arg);
			mandoc_msg(MANDOCERR_MACRO_EMPTY, mdoc->parse,
			    line, ppos, roff_name[tok]);
		}
	}
	if (nl)
		append_delims(mdoc, line, pos, buf);
	if (scope)
		rew_elem(mdoc, tok);
}

static void
blk_full(MACRO_PROT_ARGS)
{
	int		  la, nl, parsed;
	struct mdoc_arg	 *arg;
	struct roff_node *blk; /* Our own or a broken block. */
	struct roff_node *head; /* Our own head. */
	struct roff_node *body; /* Our own body. */
	struct roff_node *n;
	enum margserr	  ac, lac;
	char		 *p;

	nl = MDOC_NEWLINE & mdoc->flags;

	if (buf[*pos] == '\0' && (tok == MDOC_Sh || tok == MDOC_Ss)) {
		mandoc_msg(MANDOCERR_MACRO_EMPTY, mdoc->parse,
		    line, ppos, roff_name[tok]);
		return;
	}

	if ( ! (mdoc_macros[tok].flags & MDOC_EXPLICIT)) {

		/* Here, tok is one of Sh Ss Nm Nd It. */

		blk = NULL;
		for (n = mdoc->last; n != NULL; n = n->parent) {
			if (n->flags & NODE_ENDED) {
				if ( ! (n->flags & NODE_VALID))
					n->flags |= NODE_BROKEN;
				continue;
			}
			if (n->type != ROFFT_BLOCK)
				continue;

			if (tok == MDOC_It && n->tok == MDOC_Bl) {
				if (blk != NULL) {
					mandoc_vmsg(MANDOCERR_BLK_BROKEN,
					    mdoc->parse, line, ppos,
					    "It breaks %s",
					    roff_name[blk->tok]);
					rew_pending(mdoc, blk);
				}
				break;
			}

			if (mdoc_macros[n->tok].flags & MDOC_EXPLICIT) {
				switch (tok) {
				case MDOC_Sh:
				case MDOC_Ss:
					mandoc_vmsg(MANDOCERR_BLK_BROKEN,
					    mdoc->parse, line, ppos,
					    "%s breaks %s", roff_name[tok],
					    roff_name[n->tok]);
					rew_pending(mdoc, n);
					n = mdoc->last;
					continue;
				case MDOC_It:
					/* Delay in case it's astray. */
					blk = n;
					continue;
				default:
					break;
				}
				break;
			}

			/* Here, n is one of Sh Ss Nm Nd It. */

			if (tok != MDOC_Sh && (n->tok == MDOC_Sh ||
			    (tok != MDOC_Ss && (n->tok == MDOC_Ss ||
			     (tok != MDOC_It && n->tok == MDOC_It)))))
				break;

			/* Item breaking an explicit block. */

			if (blk != NULL) {
				mandoc_vmsg(MANDOCERR_BLK_BROKEN,
				    mdoc->parse, line, ppos,
				    "It breaks %s", roff_name[blk->tok]);
				rew_pending(mdoc, blk);
				blk = NULL;
			}

			/* Close out prior implicit scopes. */

			rew_pending(mdoc, n);
		}

		/* Skip items outside lists. */

		if (tok == MDOC_It && (n == NULL || n->tok != MDOC_Bl)) {
			mandoc_vmsg(MANDOCERR_IT_STRAY, mdoc->parse,
			    line, ppos, "It %s", buf + *pos);
			roff_elem_alloc(mdoc, line, ppos, ROFF_br);
			rew_elem(mdoc, ROFF_br);
			return;
		}
	}

	/*
	 * This routine accommodates implicitly- and explicitly-scoped
	 * macro openings.  Implicit ones first close out prior scope
	 * (seen above).  Delay opening the head until necessary to
	 * allow leading punctuation to print.  Special consideration
	 * for `It -column', which has phrase-part syntax instead of
	 * regular child nodes.
	 */

	mdoc_argv(mdoc, line, tok, &arg, pos, buf);
	blk = mdoc_block_alloc(mdoc, line, ppos, tok, arg);
	head = body = NULL;

	/*
	 * Exception: Heads of `It' macros in `-diag' lists are not
	 * parsed, even though `It' macros in general are parsed.
	 */

	parsed = tok != MDOC_It ||
	    mdoc->last->parent->tok != MDOC_Bl ||
	    mdoc->last->parent->norm->Bl.type != LIST_diag;

	/*
	 * The `Nd' macro has all arguments in its body: it's a hybrid
	 * of block partial-explicit and full-implicit.  Stupid.
	 */

	if (tok == MDOC_Nd) {
		head = roff_head_alloc(mdoc, line, ppos, tok);
		rew_last(mdoc, head);
		body = roff_body_alloc(mdoc, line, ppos, tok);
	}

	if (tok == MDOC_Bk)
		mdoc->flags |= MDOC_KEEP;

	ac = ARGS_EOLN;
	for (;;) {

		/*
		 * If we are right after a tab character,
		 * do not parse the first word for macros.
		 */

		if (mdoc->flags & MDOC_PHRASEQN) {
			mdoc->flags &= ~MDOC_PHRASEQN;
			mdoc->flags |= MDOC_PHRASEQF;
		}

		la = *pos;
		lac = ac;
		ac = mdoc_args(mdoc, line, pos, buf, tok, &p);
		if (ac == ARGS_EOLN) {
			if (lac != ARGS_PHRASE ||
			    ! (mdoc->flags & MDOC_PHRASEQF))
				break;

			/*
			 * This line ends in a tab; start the next
			 * column now, with a leading blank.
			 */

			if (body != NULL)
				rew_last(mdoc, body);
			body = roff_body_alloc(mdoc, line, ppos, tok);
			roff_word_alloc(mdoc, line, ppos, "\\&");
			break;
		}

		if (tok == MDOC_Bd || tok == MDOC_Bk) {
			mandoc_vmsg(MANDOCERR_ARG_EXCESS,
			    mdoc->parse, line, la, "%s ... %s",
			    roff_name[tok], buf + la);
			break;
		}
		if (tok == MDOC_Rs) {
			mandoc_vmsg(MANDOCERR_ARG_SKIP, mdoc->parse,
			    line, la, "Rs %s", buf + la);
			break;
		}
		if (ac == ARGS_PUNCT)
			break;

		/*
		 * Emit leading punctuation (i.e., punctuation before
		 * the ROFFT_HEAD) for non-phrase types.
		 */

		if (head == NULL &&
		    ac != ARGS_PHRASE &&
		    mdoc_isdelim(p) == DELIM_OPEN) {
			dword(mdoc, line, la, p, DELIM_OPEN, 0);
			continue;
		}

		/* Open a head if one hasn't been opened. */

		if (head == NULL)
			head = roff_head_alloc(mdoc, line, ppos, tok);

		if (ac == ARGS_PHRASE) {

			/*
			 * If we haven't opened a body yet, rewind the
			 * head; if we have, rewind that instead.
			 */

			rew_last(mdoc, body == NULL ? head : body);
			body = roff_body_alloc(mdoc, line, ppos, tok);

			/* Process to the tab or to the end of the line. */

			mdoc->flags |= MDOC_PHRASE;
			parse_rest(mdoc, TOKEN_NONE, line, &la, buf);
			mdoc->flags &= ~MDOC_PHRASE;

			/* There may have been `Ta' macros. */

			while (body->next != NULL)
				body = body->next;
			continue;
		}

		if (macro_or_word(mdoc, tok, line, la, pos, buf, parsed))
			break;
	}

	if (blk->flags & NODE_VALID)
		return;
	if (head == NULL)
		head = roff_head_alloc(mdoc, line, ppos, tok);
	if (nl && tok != MDOC_Bd && tok != MDOC_Bl && tok != MDOC_Rs)
		append_delims(mdoc, line, pos, buf);
	if (body != NULL)
		goto out;
	if (find_pending(mdoc, tok, line, ppos, head))
		return;

	/* Close out scopes to remain in a consistent state. */

	rew_last(mdoc, head);
	body = roff_body_alloc(mdoc, line, ppos, tok);
out:
	if (mdoc->flags & MDOC_FREECOL) {
		rew_last(mdoc, body);
		rew_last(mdoc, blk);
		mdoc->flags &= ~MDOC_FREECOL;
	}
}

static void
blk_part_imp(MACRO_PROT_ARGS)
{
	int		  la, nl;
	enum margserr	  ac;
	char		 *p;
	struct roff_node *blk; /* saved block context */
	struct roff_node *body; /* saved body context */
	struct roff_node *n;

	nl = MDOC_NEWLINE & mdoc->flags;

	/*
	 * A macro that spans to the end of the line.  This is generally
	 * (but not necessarily) called as the first macro.  The block
	 * has a head as the immediate child, which is always empty,
	 * followed by zero or more opening punctuation nodes, then the
	 * body (which may be empty, depending on the macro), then zero
	 * or more closing punctuation nodes.
	 */

	blk = mdoc_block_alloc(mdoc, line, ppos, tok, NULL);
	rew_last(mdoc, roff_head_alloc(mdoc, line, ppos, tok));

	/*
	 * Open the body scope "on-demand", that is, after we've
	 * processed all our the leading delimiters (open parenthesis,
	 * etc.).
	 */

	for (body = NULL; ; ) {
		la = *pos;
		ac = mdoc_args(mdoc, line, pos, buf, tok, &p);
		if (ac == ARGS_EOLN || ac == ARGS_PUNCT)
			break;

		if (body == NULL && mdoc_isdelim(p) == DELIM_OPEN) {
			dword(mdoc, line, la, p, DELIM_OPEN, 0);
			continue;
		}

		if (body == NULL)
			body = roff_body_alloc(mdoc, line, ppos, tok);

		if (macro_or_word(mdoc, tok, line, la, pos, buf, 1))
			break;
	}
	if (body == NULL)
		body = roff_body_alloc(mdoc, line, ppos, tok);

	if (find_pending(mdoc, tok, line, ppos, body))
		return;

	rew_last(mdoc, body);
	if (nl)
		append_delims(mdoc, line, pos, buf);
	rew_pending(mdoc, blk);

	/* Move trailing .Ns out of scope. */

	for (n = body->child; n && n->next; n = n->next)
		/* Do nothing. */ ;
	if (n && n->tok == MDOC_Ns)
		mdoc_node_relink(mdoc, n);
}

static void
blk_part_exp(MACRO_PROT_ARGS)
{
	int		  la, nl;
	enum margserr	  ac;
	struct roff_node *head; /* keep track of head */
	char		 *p;

	nl = MDOC_NEWLINE & mdoc->flags;

	/*
	 * The opening of an explicit macro having zero or more leading
	 * punctuation nodes; a head with optional single element (the
	 * case of `Eo'); and a body that may be empty.
	 */

	roff_block_alloc(mdoc, line, ppos, tok);
	head = NULL;
	for (;;) {
		la = *pos;
		ac = mdoc_args(mdoc, line, pos, buf, tok, &p);
		if (ac == ARGS_PUNCT || ac == ARGS_EOLN)
			break;

		/* Flush out leading punctuation. */

		if (head == NULL && mdoc_isdelim(p) == DELIM_OPEN) {
			dword(mdoc, line, la, p, DELIM_OPEN, 0);
			continue;
		}

		if (head == NULL) {
			head = roff_head_alloc(mdoc, line, ppos, tok);
			if (tok == MDOC_Eo)  /* Not parsed. */
				dword(mdoc, line, la, p, DELIM_MAX, 0);
			rew_last(mdoc, head);
			roff_body_alloc(mdoc, line, ppos, tok);
			if (tok == MDOC_Eo)
				continue;
		}

		if (macro_or_word(mdoc, tok, line, la, pos, buf, 1))
			break;
	}

	/* Clean-up to leave in a consistent state. */

	if (head == NULL) {
		rew_last(mdoc, roff_head_alloc(mdoc, line, ppos, tok));
		roff_body_alloc(mdoc, line, ppos, tok);
	}
	if (nl)
		append_delims(mdoc, line, pos, buf);
}

static void
in_line_argn(MACRO_PROT_ARGS)
{
	struct mdoc_arg	*arg;
	char		*p;
	enum margserr	 ac;
	enum roff_tok	 ntok;
	int		 state; /* arg#; -1: not yet open; -2: closed */
	int		 la, maxargs, nl;

	nl = mdoc->flags & MDOC_NEWLINE;

	/*
	 * A line macro that has a fixed number of arguments (maxargs).
	 * Only open the scope once the first non-leading-punctuation is
	 * found (unless MDOC_IGNDELIM is noted, like in `Pf'), then
	 * keep it open until the maximum number of arguments are
	 * exhausted.
	 */

	switch (tok) {
	case MDOC_Ap:
	case MDOC_Ns:
	case MDOC_Ux:
		maxargs = 0;
		break;
	case MDOC_Bx:
	case MDOC_Es:
	case MDOC_Xr:
		maxargs = 2;
		break;
	default:
		maxargs = 1;
		break;
	}

	mdoc_argv(mdoc, line, tok, &arg, pos, buf);

	state = -1;
	p = NULL;
	for (;;) {
		la = *pos;
		ac = mdoc_args(mdoc, line, pos, buf, tok, &p);

		if (ac == ARGS_WORD && state == -1 &&
		    ! (mdoc_macros[tok].flags & MDOC_IGNDELIM) &&
		    mdoc_isdelim(p) == DELIM_OPEN) {
			dword(mdoc, line, la, p, DELIM_OPEN, 0);
			continue;
		}

		if (state == -1 && tok != MDOC_In &&
		    tok != MDOC_St && tok != MDOC_Xr) {
			mdoc_elem_alloc(mdoc, line, ppos, tok, arg);
			state = 0;
		}

		if (ac == ARGS_PUNCT || ac == ARGS_EOLN) {
			if (abs(state) < 2 && tok == MDOC_Pf)
				mandoc_vmsg(MANDOCERR_PF_SKIP,
				    mdoc->parse, line, ppos, "Pf %s",
				    p == NULL ? "at eol" : p);
			break;
		}

		if (state == maxargs) {
			rew_elem(mdoc, tok);
			state = -2;
		}

		ntok = (tok == MDOC_Pf && state == 0) ?
		    TOKEN_NONE : lookup(mdoc, tok, line, la, p);

		if (ntok != TOKEN_NONE) {
			if (state >= 0) {
				rew_elem(mdoc, tok);
				state = -2;
			}
			mdoc_macro(mdoc, ntok, line, la, pos, buf);
			break;
		}

		if (mdoc_macros[tok].flags & MDOC_IGNDELIM ||
		    mdoc_isdelim(p) == DELIM_NONE) {
			if (state == -1) {
				mdoc_elem_alloc(mdoc, line, ppos, tok, arg);
				state = 1;
			} else if (state >= 0)
				state++;
		} else if (state >= 0) {
			rew_elem(mdoc, tok);
			state = -2;
		}

		dword(mdoc, line, la, p, DELIM_MAX,
		    mdoc_macros[tok].flags & MDOC_JOIN);
	}

	if (state == -1) {
		mandoc_msg(MANDOCERR_MACRO_EMPTY, mdoc->parse,
		    line, ppos, roff_name[tok]);
		return;
	}

	if (state == 0 && tok == MDOC_Pf)
		append_delims(mdoc, line, pos, buf);
	if (state >= 0)
		rew_elem(mdoc, tok);
	if (nl)
		append_delims(mdoc, line, pos, buf);
}

static void
in_line_eoln(MACRO_PROT_ARGS)
{
	struct roff_node	*n;
	struct mdoc_arg		*arg;

	if ((tok == MDOC_Pp || tok == MDOC_Lp) &&
	    ! (mdoc->flags & MDOC_SYNOPSIS)) {
		n = mdoc->last;
		if (mdoc->next == ROFF_NEXT_SIBLING)
			n = n->parent;
		if (n->tok == MDOC_Nm)
			rew_last(mdoc, n->parent);
	}

	if (buf[*pos] == '\0' &&
	    (tok == MDOC_Fd || *roff_name[tok] == '%')) {
		mandoc_msg(MANDOCERR_MACRO_EMPTY, mdoc->parse,
		    line, ppos, roff_name[tok]);
		return;
	}

	mdoc_argv(mdoc, line, tok, &arg, pos, buf);
	mdoc_elem_alloc(mdoc, line, ppos, tok, arg);
	if (parse_rest(mdoc, tok, line, pos, buf))
		return;
	rew_elem(mdoc, tok);
}

/*
 * The simplest argument parser available: Parse the remaining
 * words until the end of the phrase or line and return 0
 * or until the next macro, call that macro, and return 1.
 */
static int
parse_rest(struct roff_man *mdoc, enum roff_tok tok,
    int line, int *pos, char *buf)
{
	int		 la;

	for (;;) {
		la = *pos;
		if (mdoc_args(mdoc, line, pos, buf, tok, NULL) == ARGS_EOLN)
			return 0;
		if (macro_or_word(mdoc, tok, line, la, pos, buf, 1))
			return 1;
	}
}

static void
ctx_synopsis(MACRO_PROT_ARGS)
{

	if (~mdoc->flags & (MDOC_SYNOPSIS | MDOC_NEWLINE))
		in_line(mdoc, tok, line, ppos, pos, buf);
	else if (tok == MDOC_Nm)
		blk_full(mdoc, tok, line, ppos, pos, buf);
	else {
		assert(tok == MDOC_Vt);
		blk_part_imp(mdoc, tok, line, ppos, pos, buf);
	}
}

/*
 * Phrases occur within `Bl -column' entries, separated by `Ta' or tabs.
 * They're unusual because they're basically free-form text until a
 * macro is encountered.
 */
static void
phrase_ta(MACRO_PROT_ARGS)
{
	struct roff_node *body, *n;

	/* Make sure we are in a column list or ignore this macro. */

	body = NULL;
	for (n = mdoc->last; n != NULL; n = n->parent) {
		if (n->flags & NODE_ENDED)
			continue;
		if (n->tok == MDOC_It && n->type == ROFFT_BODY)
			body = n;
		if (n->tok == MDOC_Bl && n->end == ENDBODY_NOT)
			break;
	}

	if (n == NULL || n->norm->Bl.type != LIST_column) {
		mandoc_msg(MANDOCERR_TA_STRAY, mdoc->parse,
		    line, ppos, "Ta");
		return;
	}

	/* Advance to the next column. */

	rew_last(mdoc, body);
	roff_body_alloc(mdoc, line, ppos, MDOC_It);
	parse_rest(mdoc, TOKEN_NONE, line, pos, buf);
}
