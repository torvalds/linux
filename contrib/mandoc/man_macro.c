/*	$Id: man_macro.c,v 1.123 2017/06/25 11:45:37 schwarze Exp $ */
/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2012-2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
 * Copyright (c) 2013 Franco Fichtner <franco@lastsummer.de>
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
#include <string.h>

#include "mandoc.h"
#include "roff.h"
#include "man.h"
#include "libmandoc.h"
#include "roff_int.h"
#include "libman.h"

static	void		 blk_close(MACRO_PROT_ARGS);
static	void		 blk_exp(MACRO_PROT_ARGS);
static	void		 blk_imp(MACRO_PROT_ARGS);
static	void		 in_line_eoln(MACRO_PROT_ARGS);
static	int		 man_args(struct roff_man *, int,
				int *, char *, char **);
static	void		 rew_scope(struct roff_man *, enum roff_tok);

const	struct man_macro __man_macros[MAN_MAX - MAN_TH] = {
	{ in_line_eoln, MAN_BSCOPE }, /* TH */
	{ blk_imp, MAN_BSCOPE | MAN_SCOPED }, /* SH */
	{ blk_imp, MAN_BSCOPE | MAN_SCOPED }, /* SS */
	{ blk_imp, MAN_BSCOPE | MAN_SCOPED }, /* TP */
	{ blk_imp, MAN_BSCOPE }, /* LP */
	{ blk_imp, MAN_BSCOPE }, /* PP */
	{ blk_imp, MAN_BSCOPE }, /* P */
	{ blk_imp, MAN_BSCOPE }, /* IP */
	{ blk_imp, MAN_BSCOPE }, /* HP */
	{ in_line_eoln, MAN_SCOPED | MAN_JOIN }, /* SM */
	{ in_line_eoln, MAN_SCOPED | MAN_JOIN }, /* SB */
	{ in_line_eoln, 0 }, /* BI */
	{ in_line_eoln, 0 }, /* IB */
	{ in_line_eoln, 0 }, /* BR */
	{ in_line_eoln, 0 }, /* RB */
	{ in_line_eoln, MAN_SCOPED | MAN_JOIN }, /* R */
	{ in_line_eoln, MAN_SCOPED | MAN_JOIN }, /* B */
	{ in_line_eoln, MAN_SCOPED | MAN_JOIN }, /* I */
	{ in_line_eoln, 0 }, /* IR */
	{ in_line_eoln, 0 }, /* RI */
	{ in_line_eoln, MAN_NSCOPED }, /* nf */
	{ in_line_eoln, MAN_NSCOPED }, /* fi */
	{ blk_close, MAN_BSCOPE }, /* RE */
	{ blk_exp, MAN_BSCOPE }, /* RS */
	{ in_line_eoln, 0 }, /* DT */
	{ in_line_eoln, 0 }, /* UC */
	{ in_line_eoln, MAN_NSCOPED }, /* PD */
	{ in_line_eoln, 0 }, /* AT */
	{ in_line_eoln, MAN_NSCOPED }, /* in */
	{ in_line_eoln, 0 }, /* OP */
	{ in_line_eoln, MAN_BSCOPE }, /* EX */
	{ in_line_eoln, MAN_BSCOPE }, /* EE */
	{ blk_exp, MAN_BSCOPE }, /* UR */
	{ blk_close, MAN_BSCOPE }, /* UE */
	{ blk_exp, MAN_BSCOPE }, /* MT */
	{ blk_close, MAN_BSCOPE }, /* ME */
};
const	struct man_macro *const man_macros = __man_macros - MAN_TH;


void
man_unscope(struct roff_man *man, const struct roff_node *to)
{
	struct roff_node *n;

	to = to->parent;
	n = man->last;
	while (n != to) {

		/* Reached the end of the document? */

		if (to == NULL && ! (n->flags & NODE_VALID)) {
			if (man->flags & (MAN_BLINE | MAN_ELINE) &&
			    man_macros[n->tok].flags & MAN_SCOPED) {
				mandoc_vmsg(MANDOCERR_BLK_LINE,
				    man->parse, n->line, n->pos,
				    "EOF breaks %s", roff_name[n->tok]);
				if (man->flags & MAN_ELINE)
					man->flags &= ~MAN_ELINE;
				else {
					assert(n->type == ROFFT_HEAD);
					n = n->parent;
					man->flags &= ~MAN_BLINE;
				}
				man->last = n;
				n = n->parent;
				roff_node_delete(man, man->last);
				continue;
			}
			if (n->type == ROFFT_BLOCK &&
			    man_macros[n->tok].fp == blk_exp)
				mandoc_msg(MANDOCERR_BLK_NOEND,
				    man->parse, n->line, n->pos,
				    roff_name[n->tok]);
		}

		/*
		 * We might delete the man->last node
		 * in the post-validation phase.
		 * Save a pointer to the parent such that
		 * we know where to continue the iteration.
		 */

		man->last = n;
		n = n->parent;
		man->last->flags |= NODE_VALID;
	}

	/*
	 * If we ended up at the parent of the node we were
	 * supposed to rewind to, that means the target node
	 * got deleted, so add the next node we parse as a child
	 * of the parent instead of as a sibling of the target.
	 */

	man->next = (man->last == to) ?
	    ROFF_NEXT_CHILD : ROFF_NEXT_SIBLING;
}

/*
 * Rewinding entails ascending the parse tree until a coherent point,
 * for example, the `SH' macro will close out any intervening `SS'
 * scopes.  When a scope is closed, it must be validated and actioned.
 */
static void
rew_scope(struct roff_man *man, enum roff_tok tok)
{
	struct roff_node *n;

	/* Preserve empty paragraphs before RS. */

	n = man->last;
	if (tok == MAN_RS && n->child == NULL &&
	    (n->tok == MAN_P || n->tok == MAN_PP || n->tok == MAN_LP))
		return;

	for (;;) {
		if (n->type == ROFFT_ROOT)
			return;
		if (n->flags & NODE_VALID) {
			n = n->parent;
			continue;
		}
		if (n->type != ROFFT_BLOCK) {
			if (n->parent->type == ROFFT_ROOT) {
				man_unscope(man, n);
				return;
			} else {
				n = n->parent;
				continue;
			}
		}
		if (tok != MAN_SH && (n->tok == MAN_SH ||
		    (tok != MAN_SS && (n->tok == MAN_SS ||
		     man_macros[n->tok].fp == blk_exp))))
			return;
		man_unscope(man, n);
		n = man->last;
	}
}


/*
 * Close out a generic explicit macro.
 */
void
blk_close(MACRO_PROT_ARGS)
{
	enum roff_tok		 ntok;
	const struct roff_node	*nn;
	char			*p;
	int			 nrew, target;

	nrew = 1;
	switch (tok) {
	case MAN_RE:
		ntok = MAN_RS;
		if ( ! man_args(man, line, pos, buf, &p))
			break;
		for (nn = man->last->parent; nn; nn = nn->parent)
			if (nn->tok == ntok && nn->type == ROFFT_BLOCK)
				nrew++;
		target = strtol(p, &p, 10);
		if (*p != '\0')
			mandoc_vmsg(MANDOCERR_ARG_EXCESS, man->parse,
			    line, p - buf, "RE ... %s", p);
		if (target == 0)
			target = 1;
		nrew -= target;
		if (nrew < 1) {
			mandoc_vmsg(MANDOCERR_RE_NOTOPEN, man->parse,
			    line, ppos, "RE %d", target);
			return;
		}
		break;
	case MAN_UE:
		ntok = MAN_UR;
		break;
	case MAN_ME:
		ntok = MAN_MT;
		break;
	default:
		abort();
	}

	for (nn = man->last->parent; nn; nn = nn->parent)
		if (nn->tok == ntok && nn->type == ROFFT_BLOCK && ! --nrew)
			break;

	if (nn == NULL) {
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, man->parse,
		    line, ppos, roff_name[tok]);
		rew_scope(man, MAN_PP);
	} else {
		line = man->last->line;
		ppos = man->last->pos;
		ntok = man->last->tok;
		man_unscope(man, nn);

		if (tok == MAN_RE && nn->head->aux > 0)
			roff_setreg(man->roff, "an-margin",
			    nn->head->aux, '-');

		/* Move a trailing paragraph behind the block. */

		if (ntok == MAN_LP || ntok == MAN_PP || ntok == MAN_P) {
			*pos = strlen(buf);
			blk_imp(man, ntok, line, ppos, pos, buf);
		}
	}
}

void
blk_exp(MACRO_PROT_ARGS)
{
	struct roff_node *head;
	char		*p;
	int		 la;

	rew_scope(man, tok);
	roff_block_alloc(man, line, ppos, tok);
	head = roff_head_alloc(man, line, ppos, tok);

	la = *pos;
	if (man_args(man, line, pos, buf, &p)) {
		roff_word_alloc(man, line, la, p);
		if (tok == MAN_RS) {
			if (roff_getreg(man->roff, "an-margin") == 0)
				roff_setreg(man->roff, "an-margin",
				    7 * 24, '=');
			if ((head->aux = strtod(p, NULL) * 24.0) > 0)
				roff_setreg(man->roff, "an-margin",
				    head->aux, '+');
		}
	}

	if (buf[*pos] != '\0')
		mandoc_vmsg(MANDOCERR_ARG_EXCESS, man->parse, line,
		    *pos, "%s ... %s", roff_name[tok], buf + *pos);

	man_unscope(man, head);
	roff_body_alloc(man, line, ppos, tok);
}

/*
 * Parse an implicit-block macro.  These contain a ROFFT_HEAD and a
 * ROFFT_BODY contained within a ROFFT_BLOCK.  Rules for closing out other
 * scopes, such as `SH' closing out an `SS', are defined in the rew
 * routines.
 */
void
blk_imp(MACRO_PROT_ARGS)
{
	int		 la;
	char		*p;
	struct roff_node *n;

	rew_scope(man, tok);
	n = roff_block_alloc(man, line, ppos, tok);
	if (n->tok == MAN_SH || n->tok == MAN_SS)
		man->flags &= ~MAN_LITERAL;
	n = roff_head_alloc(man, line, ppos, tok);

	/* Add line arguments. */

	for (;;) {
		la = *pos;
		if ( ! man_args(man, line, pos, buf, &p))
			break;
		roff_word_alloc(man, line, la, p);
	}

	/*
	 * For macros having optional next-line scope,
	 * keep the head open if there were no arguments.
	 * For `TP', always keep the head open.
	 */

	if (man_macros[tok].flags & MAN_SCOPED &&
	    (tok == MAN_TP || n == man->last)) {
		man->flags |= MAN_BLINE;
		return;
	}

	/* Close out the head and open the body. */

	man_unscope(man, n);
	roff_body_alloc(man, line, ppos, tok);
}

void
in_line_eoln(MACRO_PROT_ARGS)
{
	int		 la;
	char		*p;
	struct roff_node *n;

	roff_elem_alloc(man, line, ppos, tok);
	n = man->last;

	for (;;) {
		if (buf[*pos] != '\0' && (tok == MAN_fi || tok == MAN_nf)) {
			mandoc_vmsg(MANDOCERR_ARG_SKIP,
			    man->parse, line, *pos, "%s %s",
			    roff_name[tok], buf + *pos);
			break;
		}
		if (buf[*pos] != '\0' && man->last != n && tok == MAN_PD) {
			mandoc_vmsg(MANDOCERR_ARG_EXCESS,
			    man->parse, line, *pos, "%s ... %s",
			    roff_name[tok], buf + *pos);
			break;
		}
		la = *pos;
		if ( ! man_args(man, line, pos, buf, &p))
			break;
		if (man_macros[tok].flags & MAN_JOIN &&
		    man->last->type == ROFFT_TEXT)
			roff_word_append(man, p);
		else
			roff_word_alloc(man, line, la, p);
	}

	/*
	 * Append NODE_EOS in case the last snipped argument
	 * ends with a dot, e.g. `.IR syslog (3).'
	 */

	if (n != man->last &&
	    mandoc_eos(man->last->string, strlen(man->last->string)))
		man->last->flags |= NODE_EOS;

	/*
	 * If no arguments are specified and this is MAN_SCOPED (i.e.,
	 * next-line scoped), then set our mode to indicate that we're
	 * waiting for terms to load into our context.
	 */

	if (n == man->last && man_macros[tok].flags & MAN_SCOPED) {
		assert( ! (man_macros[tok].flags & MAN_NSCOPED));
		man->flags |= MAN_ELINE;
		return;
	}

	assert(man->last->type != ROFFT_ROOT);
	man->next = ROFF_NEXT_SIBLING;

	/* Rewind our element scope. */

	for ( ; man->last; man->last = man->last->parent) {
		man_state(man, man->last);
		if (man->last == n)
			break;
	}
}

void
man_endparse(struct roff_man *man)
{

	man_unscope(man, man->first);
	man->flags &= ~MAN_LITERAL;
}

static int
man_args(struct roff_man *man, int line, int *pos, char *buf, char **v)
{
	char	 *start;

	assert(*pos);
	*v = start = buf + *pos;
	assert(' ' != *start);

	if ('\0' == *start)
		return 0;

	*v = mandoc_getarg(man->parse, v, line, pos);
	return 1;
}
