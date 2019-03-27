/*	$Id: eqn_html.c,v 1.17 2017/07/14 13:32:35 schwarze Exp $ */
/*
 * Copyright (c) 2011, 2014 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
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
#include "html.h"

static void
eqn_box(struct html *p, const struct eqn_box *bp)
{
	struct tag	*post, *row, *cell, *t;
	const struct eqn_box *child, *parent;
	const char	*cp;
	size_t		 i, j, rows;
	enum htmltag	 tag;
	enum eqn_fontt	 font;

	if (NULL == bp)
		return;

	post = NULL;

	/*
	 * Special handling for a matrix, which is presented to us in
	 * column order, but must be printed in row-order.
	 */
	if (EQN_MATRIX == bp->type) {
		if (NULL == bp->first)
			goto out;
		if (bp->first->type != EQN_LIST ||
		    bp->first->expectargs == 1) {
			eqn_box(p, bp->first);
			goto out;
		}
		if (NULL == (parent = bp->first->first))
			goto out;
		/* Estimate the number of rows, first. */
		if (NULL == (child = parent->first))
			goto out;
		for (rows = 0; NULL != child; rows++)
			child = child->next;
		/* Print row-by-row. */
		post = print_otag(p, TAG_MTABLE, "");
		for (i = 0; i < rows; i++) {
			parent = bp->first->first;
			row = print_otag(p, TAG_MTR, "");
			while (NULL != parent) {
				child = parent->first;
				for (j = 0; j < i; j++) {
					if (NULL == child)
						break;
					child = child->next;
				}
				cell = print_otag(p, TAG_MTD, "");
				/*
				 * If we have no data for this
				 * particular cell, then print a
				 * placeholder and continue--don't puke.
				 */
				if (NULL != child)
					eqn_box(p, child->first);
				print_tagq(p, cell);
				parent = parent->next;
			}
			print_tagq(p, row);
		}
		goto out;
	}

	switch (bp->pos) {
	case EQNPOS_TO:
		post = print_otag(p, TAG_MOVER, "");
		break;
	case EQNPOS_SUP:
		post = print_otag(p, TAG_MSUP, "");
		break;
	case EQNPOS_FROM:
		post = print_otag(p, TAG_MUNDER, "");
		break;
	case EQNPOS_SUB:
		post = print_otag(p, TAG_MSUB, "");
		break;
	case EQNPOS_OVER:
		post = print_otag(p, TAG_MFRAC, "");
		break;
	case EQNPOS_FROMTO:
		post = print_otag(p, TAG_MUNDEROVER, "");
		break;
	case EQNPOS_SUBSUP:
		post = print_otag(p, TAG_MSUBSUP, "");
		break;
	case EQNPOS_SQRT:
		post = print_otag(p, TAG_MSQRT, "");
		break;
	default:
		break;
	}

	if (bp->top || bp->bottom) {
		assert(NULL == post);
		if (bp->top && NULL == bp->bottom)
			post = print_otag(p, TAG_MOVER, "");
		else if (bp->top && bp->bottom)
			post = print_otag(p, TAG_MUNDEROVER, "");
		else if (bp->bottom)
			post = print_otag(p, TAG_MUNDER, "");
	}

	if (EQN_PILE == bp->type) {
		assert(NULL == post);
		if (bp->first != NULL &&
		    bp->first->type == EQN_LIST &&
		    bp->first->expectargs > 1)
			post = print_otag(p, TAG_MTABLE, "");
	} else if (bp->type == EQN_LIST && bp->expectargs > 1 &&
	    bp->parent && bp->parent->type == EQN_PILE) {
		assert(NULL == post);
		post = print_otag(p, TAG_MTR, "");
		print_otag(p, TAG_MTD, "");
	}

	if (bp->text != NULL) {
		assert(post == NULL);
		tag = TAG_MI;
		cp = bp->text;
		if (isdigit((unsigned char)cp[0]) ||
		    (cp[0] == '.' && isdigit((unsigned char)cp[1]))) {
			tag = TAG_MN;
			while (*++cp != '\0') {
				if (*cp != '.' &&
				    isdigit((unsigned char)*cp) == 0) {
					tag = TAG_MI;
					break;
				}
			}
		} else if (*cp != '\0' && isalpha((unsigned char)*cp) == 0) {
			tag = TAG_MO;
			while (*cp != '\0') {
				if (cp[0] == '\\' && cp[1] != '\0') {
					cp++;
					mandoc_escape(&cp, NULL, NULL);
				} else if (isalnum((unsigned char)*cp)) {
					tag = TAG_MI;
					break;
				} else
					cp++;
			}
		}
		font = bp->font;
		if (bp->text[0] != '\0' &&
		    (((tag == TAG_MN || tag == TAG_MO) &&
		      font == EQNFONT_ROMAN) ||
		     (tag == TAG_MI && font == (bp->text[1] == '\0' ?
		      EQNFONT_ITALIC : EQNFONT_ROMAN))))
			font = EQNFONT_NONE;
		switch (font) {
		case EQNFONT_NONE:
			post = print_otag(p, tag, "");
			break;
		case EQNFONT_ROMAN:
			post = print_otag(p, tag, "?", "fontstyle", "normal");
			break;
		case EQNFONT_BOLD:
		case EQNFONT_FAT:
			post = print_otag(p, tag, "?", "fontweight", "bold");
			break;
		case EQNFONT_ITALIC:
			post = print_otag(p, tag, "?", "fontstyle", "italic");
			break;
		default:
			abort();
		}
		print_text(p, bp->text);
	} else if (NULL == post) {
		if (NULL != bp->left || NULL != bp->right)
			post = print_otag(p, TAG_MFENCED, "??",
			    "open", bp->left == NULL ? "" : bp->left,
			    "close", bp->right == NULL ? "" : bp->right);
		if (NULL == post)
			post = print_otag(p, TAG_MROW, "");
		else
			print_otag(p, TAG_MROW, "");
	}

	eqn_box(p, bp->first);

out:
	if (NULL != bp->bottom) {
		t = print_otag(p, TAG_MO, "");
		print_text(p, bp->bottom);
		print_tagq(p, t);
	}
	if (NULL != bp->top) {
		t = print_otag(p, TAG_MO, "");
		print_text(p, bp->top);
		print_tagq(p, t);
	}

	if (NULL != post)
		print_tagq(p, post);

	eqn_box(p, bp->next);
}

void
print_eqn(struct html *p, const struct eqn_box *bp)
{
	struct tag	*t;

	if (bp->first == NULL)
		return;

	t = print_otag(p, TAG_MATH, "c", "eqn");

	p->flags |= HTML_NONOSPACE;
	eqn_box(p, bp);
	p->flags &= ~HTML_NONOSPACE;

	print_tagq(p, t);
}
