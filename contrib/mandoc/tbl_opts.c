/*	$Id: tbl_opts.c,v 1.21 2015/09/26 00:54:04 schwarze Exp $ */
/*
 * Copyright (c) 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2015 Ingo Schwarze <schwarze@openbsd.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "libmandoc.h"
#include "libroff.h"

#define	KEY_DPOINT	0
#define	KEY_DELIM	1
#define	KEY_LINESIZE	2
#define	KEY_TAB		3

struct	tbl_phrase {
	const char	*name;
	int		 key;
};

static	const struct tbl_phrase keys[] = {
	{"decimalpoint", 0},
	{"delim",	 0},
	{"linesize",	 0},
	{"tab",		 0},
	{"allbox",	 TBL_OPT_ALLBOX | TBL_OPT_BOX},
	{"box",		 TBL_OPT_BOX},
	{"frame",	 TBL_OPT_BOX},
	{"center",	 TBL_OPT_CENTRE},
	{"centre",	 TBL_OPT_CENTRE},
	{"doublebox",	 TBL_OPT_DBOX},
	{"doubleframe",  TBL_OPT_DBOX},
	{"expand",	 TBL_OPT_EXPAND},
	{"nokeep",	 TBL_OPT_NOKEEP},
	{"nospaces",	 TBL_OPT_NOSPACE},
	{"nowarn",	 TBL_OPT_NOWARN},
};

#define KEY_MAXKEYS ((int)(sizeof(keys)/sizeof(keys[0])))

static	void	 arg(struct tbl_node *, int, const char *, int *, int);


static void
arg(struct tbl_node *tbl, int ln, const char *p, int *pos, int key)
{
	int		 len, want;

	while (p[*pos] == ' ' || p[*pos] == '\t')
		(*pos)++;

	/* Arguments are enclosed in parentheses. */

	len = 0;
	if (p[*pos] == '(') {
		(*pos)++;
		while (p[*pos + len] != ')')
			len++;
	}

	switch (key) {
	case KEY_DELIM:
		mandoc_vmsg(MANDOCERR_TBLOPT_EQN, tbl->parse,
		    ln, *pos, "%.*s", len, p + *pos);
		want = 2;
		break;
	case KEY_TAB:
		want = 1;
		if (len == want)
			tbl->opts.tab = p[*pos];
		break;
	case KEY_LINESIZE:
		want = 0;
		break;
	case KEY_DPOINT:
		want = 1;
		if (len == want)
			tbl->opts.decimal = p[*pos];
		break;
	default:
		abort();
	}

	if (len == 0)
		mandoc_msg(MANDOCERR_TBLOPT_NOARG,
		    tbl->parse, ln, *pos, keys[key].name);
	else if (want && len != want)
		mandoc_vmsg(MANDOCERR_TBLOPT_ARGSZ,
		    tbl->parse, ln, *pos, "%s want %d have %d",
		    keys[key].name, want, len);

	*pos += len;
	if (p[*pos] == ')')
		(*pos)++;
}

/*
 * Parse one line of options up to the semicolon.
 * Each option can be preceded by blanks and/or commas,
 * and some options are followed by arguments.
 */
void
tbl_option(struct tbl_node *tbl, int ln, const char *p, int *offs)
{
	int		 i, pos, len;

	pos = *offs;
	for (;;) {
		while (p[pos] == ' ' || p[pos] == '\t' || p[pos] == ',')
			pos++;

		if (p[pos] == ';') {
			*offs = pos + 1;
			return;
		}

		/* Parse one option name. */

		len = 0;
		while (isalpha((unsigned char)p[pos + len]))
			len++;

		if (len == 0) {
			mandoc_vmsg(MANDOCERR_TBLOPT_ALPHA,
			    tbl->parse, ln, pos, "%c", p[pos]);
			pos++;
			continue;
		}

		/* Look up the option name. */

		i = 0;
		while (i < KEY_MAXKEYS &&
		    (strncasecmp(p + pos, keys[i].name, len) ||
		     keys[i].name[len] != '\0'))
			i++;

		if (i == KEY_MAXKEYS) {
			mandoc_vmsg(MANDOCERR_TBLOPT_BAD, tbl->parse,
			    ln, pos, "%.*s", len, p + pos);
			pos += len;
			continue;
		}

		/* Handle the option. */

		pos += len;
		if (keys[i].key)
			tbl->opts.opts |= keys[i].key;
		else
			arg(tbl, ln, p, &pos, i);
	}
}
