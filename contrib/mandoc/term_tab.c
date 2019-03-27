/*	$OpenBSD: term.c,v 1.119 2017/01/08 18:08:44 schwarze Exp $ */
/*
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
#include <sys/types.h>

#include <stddef.h>

#include "mandoc_aux.h"
#include "out.h"
#include "term.h"

struct tablist {
	size_t	*t;	/* Allocated array of tab positions. */
	size_t	 s;	/* Allocated number of positions. */
	size_t	 n;	/* Currently used number of positions. */
};

static struct {
	struct tablist	 a;	/* All tab positions for lookup. */
	struct tablist	 p;	/* Periodic tab positions to add. */
	size_t		 d;	/* Default tab width in units of n. */
} tabs;


void
term_tab_set(const struct termp *p, const char *arg)
{
	static int	 recording_period;

	struct roffsu	 su;
	struct tablist	*tl;
	size_t		 pos;
	int		 add;

	/* Special arguments: clear all tabs or switch lists. */

	if (arg == NULL) {
		tabs.a.n = tabs.p.n = 0;
		recording_period = 0;
		if (tabs.d == 0) {
			a2roffsu(".8i", &su, SCALE_IN);
			tabs.d = term_hen(p, &su);
		}
		return;
	}
	if (arg[0] == 'T' && arg[1] == '\0') {
		recording_period = 1;
		return;
	}

	/* Parse the sign, the number, and the unit. */

	if (*arg == '+') {
		add = 1;
		arg++;
	} else
		add = 0;
	if (a2roffsu(arg, &su, SCALE_EM) == NULL)
		return;

	/* Select the list, and extend it if it is full. */

	tl = recording_period ? &tabs.p : &tabs.a;
	if (tl->n >= tl->s) {
		tl->s += 8;
		tl->t = mandoc_reallocarray(tl->t, tl->s, sizeof(*tl->t));
	}

	/* Append the new position. */

	pos = term_hen(p, &su);
	tl->t[tl->n] = pos;
	if (add && tl->n)
		tl->t[tl->n] += tl->t[tl->n - 1];
	tl->n++;
}

/*
 * Simplified version without a parser,
 * never incremental, never periodic, for use by tbl(7).
 */
void
term_tab_iset(size_t inc)
{
	if (tabs.a.n >= tabs.a.s) {
		tabs.a.s += 8;
		tabs.a.t = mandoc_reallocarray(tabs.a.t, tabs.a.s,
		    sizeof(*tabs.a.t));
	}
	tabs.a.t[tabs.a.n++] = inc;
}

size_t
term_tab_next(size_t prev)
{
	size_t	 i, j;

	for (i = 0;; i++) {
		if (i == tabs.a.n) {
			if (tabs.p.n == 0)
				return prev;
			tabs.a.n += tabs.p.n;
			if (tabs.a.s < tabs.a.n) {
				tabs.a.s = tabs.a.n;
				tabs.a.t = mandoc_reallocarray(tabs.a.t,
				    tabs.a.s, sizeof(*tabs.a.t));
			}
			for (j = 0; j < tabs.p.n; j++)
				tabs.a.t[i + j] = tabs.p.t[j] +
				    (i ? tabs.a.t[i - 1] : 0);
		}
		if (prev < tabs.a.t[i])
			return tabs.a.t[i];
	}
}
