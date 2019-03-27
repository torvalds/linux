/*	$Id: eqn_term.c,v 1.17 2017/08/23 21:56:20 schwarze Exp $ */
/*
 * Copyright (c) 2011 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "out.h"
#include "term.h"

static	const enum termfont fontmap[EQNFONT__MAX] = {
	TERMFONT_NONE, /* EQNFONT_NONE */
	TERMFONT_NONE, /* EQNFONT_ROMAN */
	TERMFONT_BOLD, /* EQNFONT_BOLD */
	TERMFONT_BOLD, /* EQNFONT_FAT */
	TERMFONT_UNDER /* EQNFONT_ITALIC */
};

static void	eqn_box(struct termp *, const struct eqn_box *);


void
term_eqn(struct termp *p, const struct eqn_box *bp)
{

	eqn_box(p, bp);
	p->flags &= ~TERMP_NOSPACE;
}

static void
eqn_box(struct termp *p, const struct eqn_box *bp)
{
	const struct eqn_box *child;
	const char *cp;
	int delim;

	/* Delimiters around this box? */

	if ((bp->type == EQN_LIST && bp->expectargs > 1) ||
	    (bp->type == EQN_PILE && (bp->prev || bp->next)) ||
	    (bp->parent != NULL && (bp->parent->pos == EQNPOS_SQRT ||
	    /* Diacritic followed by ^ or _. */
	    ((bp->top != NULL || bp->bottom != NULL) &&
	     bp->parent->type == EQN_SUBEXPR &&
	     bp->parent->pos != EQNPOS_OVER && bp->next != NULL) ||
	    /* Nested over, sub, sup, from, to. */
	    (bp->type == EQN_SUBEXPR && bp->pos != EQNPOS_SQRT &&
	     ((bp->parent->type == EQN_LIST && bp->expectargs == 1) ||
	      (bp->parent->type == EQN_SUBEXPR &&
	       bp->pos != EQNPOS_SQRT)))))) {
		if ((bp->parent->type == EQN_SUBEXPR && bp->prev != NULL) ||
		    (bp->type == EQN_LIST &&
		     bp->first != NULL &&
		     bp->first->type != EQN_PILE &&
		     bp->first->type != EQN_MATRIX &&
		     bp->prev != NULL &&
		     (bp->prev->type == EQN_LIST ||
		      (bp->prev->type == EQN_TEXT &&
		       (*bp->prev->text == '\\' ||
		        isalpha((unsigned char)*bp->prev->text))))))
			p->flags |= TERMP_NOSPACE;
		term_word(p, bp->left != NULL ? bp->left : "(");
		p->flags |= TERMP_NOSPACE;
		delim = 1;
	} else
		delim = 0;

	/* Handle Fonts and text. */

	if (bp->font != EQNFONT_NONE)
		term_fontpush(p, fontmap[(int)bp->font]);

	if (bp->text != NULL) {
		if (strchr("!\"'),.:;?]}", *bp->text) != NULL)
			p->flags |= TERMP_NOSPACE;
		term_word(p, bp->text);
		if ((cp = strchr(bp->text, '\0')) > bp->text &&
		    (strchr("\"'([{", cp[-1]) != NULL ||
		     (bp->prev == NULL && (cp[-1] == '-' ||
		      (cp >= bp->text + 5 &&
		       strcmp(cp - 5, "\\[mi]") == 0)))))
			p->flags |= TERMP_NOSPACE;
	}

	/* Special box types. */

	if (bp->pos == EQNPOS_SQRT) {
		term_word(p, "sqrt");
		if (bp->first != NULL) {
			p->flags |= TERMP_NOSPACE;
			eqn_box(p, bp->first);
		}
	} else if (bp->type == EQN_SUBEXPR) {
		child = bp->first;
		eqn_box(p, child);
		p->flags |= TERMP_NOSPACE;
		term_word(p, bp->pos == EQNPOS_OVER ? "/" :
		    (bp->pos == EQNPOS_SUP ||
		     bp->pos == EQNPOS_TO) ? "^" : "_");
		child = child->next;
		if (child != NULL) {
			p->flags |= TERMP_NOSPACE;
			eqn_box(p, child);
			if (bp->pos == EQNPOS_FROMTO ||
			    bp->pos == EQNPOS_SUBSUP) {
				p->flags |= TERMP_NOSPACE;
				term_word(p, "^");
				p->flags |= TERMP_NOSPACE;
				child = child->next;
				if (child != NULL)
					eqn_box(p, child);
			}
		}
	} else {
		child = bp->first;
		if (bp->type == EQN_MATRIX &&
		    child != NULL &&
		    child->type == EQN_LIST &&
		    child->expectargs > 1)
			child = child->first;
		while (child != NULL) {
			eqn_box(p,
			    bp->type == EQN_PILE &&
			    child->type == EQN_LIST &&
			    child->expectargs > 1 &&
			    child->args == 1 ?
			    child->first : child);
			child = child->next;
		}
	}

	/* Handle Fonts and diacritics. */

	if (bp->font != EQNFONT_NONE)
		term_fontpop(p);
	if (bp->top != NULL) {
		p->flags |= TERMP_NOSPACE;
		term_word(p, bp->top);
	}
	if (bp->bottom != NULL) {
		p->flags |= TERMP_NOSPACE;
		term_word(p, "_");
	}

	/* Right delimiter after this box? */

	if (delim) {
		p->flags |= TERMP_NOSPACE;
		term_word(p, bp->right != NULL ? bp->right : ")");
		if (bp->parent->type == EQN_SUBEXPR && bp->next != NULL)
			p->flags |= TERMP_NOSPACE;
	}
}
