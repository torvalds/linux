/*	$Id: tbl_layout.c,v 1.44 2017/06/27 18:25:02 schwarze Exp $ */
/*
 * Copyright (c) 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2012, 2014, 2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
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

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mandoc.h"
#include "mandoc_aux.h"
#include "libmandoc.h"
#include "libroff.h"

struct	tbl_phrase {
	char		 name;
	enum tbl_cellt	 key;
};

static	const struct tbl_phrase keys[] = {
	{ 'c',		 TBL_CELL_CENTRE },
	{ 'r',		 TBL_CELL_RIGHT },
	{ 'l',		 TBL_CELL_LEFT },
	{ 'n',		 TBL_CELL_NUMBER },
	{ 's',		 TBL_CELL_SPAN },
	{ 'a',		 TBL_CELL_LONG },
	{ '^',		 TBL_CELL_DOWN },
	{ '-',		 TBL_CELL_HORIZ },
	{ '_',		 TBL_CELL_HORIZ },
	{ '=',		 TBL_CELL_DHORIZ }
};

#define KEYS_MAX ((int)(sizeof(keys)/sizeof(keys[0])))

static	void		 mods(struct tbl_node *, struct tbl_cell *,
				int, const char *, int *);
static	void		 cell(struct tbl_node *, struct tbl_row *,
				int, const char *, int *);
static	struct tbl_cell *cell_alloc(struct tbl_node *, struct tbl_row *,
				enum tbl_cellt);


static void
mods(struct tbl_node *tbl, struct tbl_cell *cp,
		int ln, const char *p, int *pos)
{
	char		*endptr;
	size_t		 sz;

mod:
	while (p[*pos] == ' ' || p[*pos] == '\t')
		(*pos)++;

	/* Row delimiters and cell specifiers end modifier lists. */

	if (strchr(".,-=^_ACLNRSaclnrs", p[*pos]) != NULL)
		return;

	/* Throw away parenthesised expression. */

	if ('(' == p[*pos]) {
		(*pos)++;
		while (p[*pos] && ')' != p[*pos])
			(*pos)++;
		if (')' == p[*pos]) {
			(*pos)++;
			goto mod;
		}
		mandoc_msg(MANDOCERR_TBLLAYOUT_PAR, tbl->parse,
		    ln, *pos, NULL);
		return;
	}

	/* Parse numerical spacing from modifier string. */

	if (isdigit((unsigned char)p[*pos])) {
		cp->spacing = strtoull(p + *pos, &endptr, 10);
		*pos = endptr - p;
		goto mod;
	}

	switch (tolower((unsigned char)p[(*pos)++])) {
	case 'b':
		cp->flags |= TBL_CELL_BOLD;
		goto mod;
	case 'd':
		cp->flags |= TBL_CELL_BALIGN;
		goto mod;
	case 'e':
		cp->flags |= TBL_CELL_EQUAL;
		goto mod;
	case 'f':
		break;
	case 'i':
		cp->flags |= TBL_CELL_ITALIC;
		goto mod;
	case 'm':
		mandoc_msg(MANDOCERR_TBLLAYOUT_MOD, tbl->parse,
		    ln, *pos, "m");
		goto mod;
	case 'p':
	case 'v':
		if (p[*pos] == '-' || p[*pos] == '+')
			(*pos)++;
		while (isdigit((unsigned char)p[*pos]))
			(*pos)++;
		goto mod;
	case 't':
		cp->flags |= TBL_CELL_TALIGN;
		goto mod;
	case 'u':
		cp->flags |= TBL_CELL_UP;
		goto mod;
	case 'w':
		sz = 0;
		if (p[*pos] == '(') {
			(*pos)++;
			while (p[*pos + sz] != '\0' && p[*pos + sz] != ')')
				sz++;
		} else
			while (isdigit((unsigned char)p[*pos + sz]))
				sz++;
		if (sz) {
			free(cp->wstr);
			cp->wstr = mandoc_strndup(p + *pos, sz);
			*pos += sz;
			if (p[*pos] == ')')
				(*pos)++;
		}
		goto mod;
	case 'x':
		cp->flags |= TBL_CELL_WMAX;
		goto mod;
	case 'z':
		cp->flags |= TBL_CELL_WIGN;
		goto mod;
	case '|':
		if (cp->vert < 2)
			cp->vert++;
		else
			mandoc_msg(MANDOCERR_TBLLAYOUT_VERT,
			    tbl->parse, ln, *pos - 1, NULL);
		goto mod;
	default:
		mandoc_vmsg(MANDOCERR_TBLLAYOUT_CHAR, tbl->parse,
		    ln, *pos - 1, "%c", p[*pos - 1]);
		goto mod;
	}

	/* Ignore parenthised font names for now. */

	if (p[*pos] == '(')
		goto mod;

	/* Support only one-character font-names for now. */

	if (p[*pos] == '\0' || (p[*pos + 1] != ' ' && p[*pos + 1] != '.')) {
		mandoc_vmsg(MANDOCERR_FT_BAD, tbl->parse,
		    ln, *pos, "TS %s", p + *pos - 1);
		if (p[*pos] != '\0')
			(*pos)++;
		if (p[*pos] != '\0')
			(*pos)++;
		goto mod;
	}

	switch (p[(*pos)++]) {
	case '3':
	case 'B':
		cp->flags |= TBL_CELL_BOLD;
		goto mod;
	case '2':
	case 'I':
		cp->flags |= TBL_CELL_ITALIC;
		goto mod;
	case '1':
	case 'R':
		goto mod;
	default:
		mandoc_vmsg(MANDOCERR_FT_BAD, tbl->parse,
		    ln, *pos - 1, "TS f%c", p[*pos - 1]);
		goto mod;
	}
}

static void
cell(struct tbl_node *tbl, struct tbl_row *rp,
		int ln, const char *p, int *pos)
{
	int		 i;
	enum tbl_cellt	 c;

	/* Handle leading vertical lines */

	while (p[*pos] == ' ' || p[*pos] == '\t' || p[*pos] == '|') {
		if (p[*pos] == '|') {
			if (rp->vert < 2)
				rp->vert++;
			else
				mandoc_msg(MANDOCERR_TBLLAYOUT_VERT,
				    tbl->parse, ln, *pos, NULL);
		}
		(*pos)++;
	}

again:
	while (p[*pos] == ' ' || p[*pos] == '\t')
		(*pos)++;

	if (p[*pos] == '.' || p[*pos] == '\0')
		return;

	/* Parse the column position (`c', `l', `r', ...). */

	for (i = 0; i < KEYS_MAX; i++)
		if (tolower((unsigned char)p[*pos]) == keys[i].name)
			break;

	if (i == KEYS_MAX) {
		mandoc_vmsg(MANDOCERR_TBLLAYOUT_CHAR, tbl->parse,
		    ln, *pos, "%c", p[*pos]);
		(*pos)++;
		goto again;
	}
	c = keys[i].key;

	/* Special cases of spanners. */

	if (c == TBL_CELL_SPAN) {
		if (rp->last == NULL)
			mandoc_msg(MANDOCERR_TBLLAYOUT_SPAN,
			    tbl->parse, ln, *pos, NULL);
		else if (rp->last->pos == TBL_CELL_HORIZ ||
		    rp->last->pos == TBL_CELL_DHORIZ)
			c = rp->last->pos;
	} else if (c == TBL_CELL_DOWN && rp == tbl->first_row)
		mandoc_msg(MANDOCERR_TBLLAYOUT_DOWN,
		    tbl->parse, ln, *pos, NULL);

	(*pos)++;

	/* Allocate cell then parse its modifiers. */

	mods(tbl, cell_alloc(tbl, rp, c), ln, p, pos);
}

void
tbl_layout(struct tbl_node *tbl, int ln, const char *p, int pos)
{
	struct tbl_row	*rp;

	rp = NULL;
	for (;;) {
		/* Skip whitespace before and after each cell. */

		while (p[pos] == ' ' || p[pos] == '\t')
			pos++;

		switch (p[pos]) {
		case ',':  /* Next row on this input line. */
			pos++;
			rp = NULL;
			continue;
		case '\0':  /* Next row on next input line. */
			return;
		case '.':  /* End of layout. */
			pos++;
			tbl->part = TBL_PART_DATA;

			/*
			 * When the layout is completely empty,
			 * default to one left-justified column.
			 */

			if (tbl->first_row == NULL) {
				tbl->first_row = tbl->last_row =
				    mandoc_calloc(1, sizeof(*rp));
			}
			if (tbl->first_row->first == NULL) {
				mandoc_msg(MANDOCERR_TBLLAYOUT_NONE,
				    tbl->parse, ln, pos, NULL);
				cell_alloc(tbl, tbl->first_row,
				    TBL_CELL_LEFT);
				if (tbl->opts.lvert < tbl->first_row->vert)
					tbl->opts.lvert = tbl->first_row->vert;
				return;
			}

			/*
			 * Search for the widest line
			 * along the left and right margins.
			 */

			for (rp = tbl->first_row; rp; rp = rp->next) {
				if (tbl->opts.lvert < rp->vert)
					tbl->opts.lvert = rp->vert;
				if (rp->last != NULL &&
				    rp->last->col + 1 == tbl->opts.cols &&
				    tbl->opts.rvert < rp->last->vert)
					tbl->opts.rvert = rp->last->vert;

				/* If the last line is empty, drop it. */

				if (rp->next != NULL &&
				    rp->next->first == NULL) {
					free(rp->next);
					rp->next = NULL;
					tbl->last_row = rp;
				}
			}
			return;
		default:  /* Cell. */
			break;
		}

		/*
		 * If the last line had at least one cell,
		 * start a new one; otherwise, continue it.
		 */

		if (rp == NULL) {
			if (tbl->last_row == NULL ||
			    tbl->last_row->first != NULL) {
				rp = mandoc_calloc(1, sizeof(*rp));
				if (tbl->last_row)
					tbl->last_row->next = rp;
				else
					tbl->first_row = rp;
				tbl->last_row = rp;
			} else
				rp = tbl->last_row;
		}
		cell(tbl, rp, ln, p, &pos);
	}
}

static struct tbl_cell *
cell_alloc(struct tbl_node *tbl, struct tbl_row *rp, enum tbl_cellt pos)
{
	struct tbl_cell	*p, *pp;

	p = mandoc_calloc(1, sizeof(*p));
	p->spacing = SIZE_MAX;
	p->pos = pos;

	if ((pp = rp->last) != NULL) {
		pp->next = p;
		p->col = pp->col + 1;
	} else
		rp->first = p;
	rp->last = p;

	if (tbl->opts.cols <= p->col)
		tbl->opts.cols = p->col + 1;

	return p;
}
