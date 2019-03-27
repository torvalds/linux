/*	$Id: tbl_html.c,v 1.24 2018/06/25 13:45:57 schwarze Exp $ */
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "out.h"
#include "html.h"

static	void	 html_tblopen(struct html *, const struct tbl_span *);
static	size_t	 html_tbl_len(size_t, void *);
static	size_t	 html_tbl_strlen(const char *, void *);
static	size_t	 html_tbl_sulen(const struct roffsu *, void *);


static size_t
html_tbl_len(size_t sz, void *arg)
{
	return sz;
}

static size_t
html_tbl_strlen(const char *p, void *arg)
{
	return strlen(p);
}

static size_t
html_tbl_sulen(const struct roffsu *su, void *arg)
{
	if (su->scale < 0.0)
		return 0;

	switch (su->unit) {
	case SCALE_FS:  /* 2^16 basic units */
		return su->scale * 65536.0 / 24.0;
	case SCALE_IN:  /* 10 characters per inch */
		return su->scale * 10.0;
	case SCALE_CM:  /* 2.54 cm per inch */
		return su->scale * 10.0 / 2.54;
	case SCALE_PC:  /* 6 pica per inch */
	case SCALE_VS:
		return su->scale * 10.0 / 6.0;
	case SCALE_EN:
	case SCALE_EM:
		return su->scale;
	case SCALE_PT:  /* 12 points per pica */
		return su->scale * 10.0 / 6.0 / 12.0;
	case SCALE_BU:  /* 24 basic units per character */
		return su->scale / 24.0;
	case SCALE_MM:  /* 1/1000 inch */
		return su->scale / 100.0;
	default:
		abort();
	}
}

static void
html_tblopen(struct html *h, const struct tbl_span *sp)
{
	if (h->tbl.cols == NULL) {
		h->tbl.len = html_tbl_len;
		h->tbl.slen = html_tbl_strlen;
		h->tbl.sulen = html_tbl_sulen;
		tblcalc(&h->tbl, sp, 0, 0);
	}
	assert(NULL == h->tblt);
	h->tblt = print_otag(h, TAG_TABLE, "c", "tbl");
}

void
print_tblclose(struct html *h)
{

	assert(h->tblt);
	print_tagq(h, h->tblt);
	h->tblt = NULL;
}

void
print_tbl(struct html *h, const struct tbl_span *sp)
{
	const struct tbl_dat *dp;
	struct tag	*tt;
	int		 ic;

	/* Inhibit printing of spaces: we do padding ourselves. */

	if (h->tblt == NULL)
		html_tblopen(h, sp);

	assert(h->tblt);

	h->flags |= HTML_NONOSPACE;
	h->flags |= HTML_NOSPACE;

	tt = print_otag(h, TAG_TR, "");

	switch (sp->pos) {
	case TBL_SPAN_HORIZ:
	case TBL_SPAN_DHORIZ:
		print_otag(h, TAG_TD, "?", "colspan", "0");
		break;
	default:
		dp = sp->first;
		for (ic = 0; ic < sp->opts->cols; ic++) {
			print_stagq(h, tt);
			print_otag(h, TAG_TD, "");

			if (dp == NULL || dp->layout->col > ic)
				continue;
			if (dp->layout->pos != TBL_CELL_DOWN)
				if (dp->string != NULL)
					print_text(h, dp->string);
			dp = dp->next;
		}
		break;
	}

	print_tagq(h, tt);

	h->flags &= ~HTML_NONOSPACE;

	if (sp->next == NULL) {
		assert(h->tbl.cols);
		free(h->tbl.cols);
		h->tbl.cols = NULL;
		print_tblclose(h);
	}

}
