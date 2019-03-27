/*	$Id: demandoc.c,v 1.29 2017/06/24 14:38:32 schwarze Exp $ */
/*
 * Copyright (c) 2011 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <unistd.h>

#include "mandoc.h"
#include "roff.h"
#include "man.h"
#include "mdoc.h"

static	void	 pline(int, int *, int *, int);
static	void	 pman(const struct roff_node *, int *, int *, int);
static	void	 pmandoc(struct mparse *, int, const char *, int);
static	void	 pmdoc(const struct roff_node *, int *, int *, int);
static	void	 pstring(const char *, int, int *, int);
static	void	 usage(void);

static	const char	 *progname;

int
main(int argc, char *argv[])
{
	struct mparse	*mp;
	int		 ch, fd, i, list;
	extern int	 optind;

	if (argc < 1)
		progname = "demandoc";
	else if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		++progname;

	mp = NULL;
	list = 0;

	while (-1 != (ch = getopt(argc, argv, "ikm:pw")))
		switch (ch) {
		case ('i'):
			/* FALLTHROUGH */
		case ('k'):
			/* FALLTHROUGH */
		case ('m'):
			/* FALLTHROUGH */
		case ('p'):
			break;
		case ('w'):
			list = 1;
			break;
		default:
			usage();
			return (int)MANDOCLEVEL_BADARG;
		}

	argc -= optind;
	argv += optind;

	mchars_alloc();
	mp = mparse_alloc(MPARSE_SO, MANDOCERR_MAX, NULL,
	    MANDOC_OS_OTHER, NULL);
	assert(mp);

	if (argc < 1)
		pmandoc(mp, STDIN_FILENO, "<stdin>", list);

	for (i = 0; i < argc; i++) {
		mparse_reset(mp);
		if ((fd = mparse_open(mp, argv[i])) == -1) {
			perror(argv[i]);
			continue;
		}
		pmandoc(mp, fd, argv[i], list);
	}

	mparse_free(mp);
	mchars_free();
	return (int)MANDOCLEVEL_OK;
}

static void
usage(void)
{

	fprintf(stderr, "usage: %s [-w] [files...]\n", progname);
}

static void
pmandoc(struct mparse *mp, int fd, const char *fn, int list)
{
	struct roff_man	*man;
	int		 line, col;

	mparse_readfd(mp, fd, fn);
	close(fd);
	mparse_result(mp, &man, NULL);
	line = 1;
	col = 0;

	if (man == NULL)
		return;
	if (man->macroset == MACROSET_MDOC) {
		mdoc_validate(man);
		pmdoc(man->first->child, &line, &col, list);
	} else {
		man_validate(man);
		pman(man->first->child, &line, &col, list);
	}

	if ( ! list)
		putchar('\n');
}

/*
 * Strip the escapes out of a string, emitting the results.
 */
static void
pstring(const char *p, int col, int *colp, int list)
{
	enum mandoc_esc	 esc;
	const char	*start, *end;
	int		 emit;

	/*
	 * Print as many column spaces til we achieve parity with the
	 * input document.
	 */

again:
	if (list && '\0' != *p) {
		while (isspace((unsigned char)*p))
			p++;

		while ('\'' == *p || '(' == *p || '"' == *p)
			p++;

		emit = isalpha((unsigned char)p[0]) &&
			isalpha((unsigned char)p[1]);

		for (start = p; '\0' != *p; p++)
			if ('\\' == *p) {
				p++;
				esc = mandoc_escape(&p, NULL, NULL);
				if (ESCAPE_ERROR == esc)
					return;
				emit = 0;
			} else if (isspace((unsigned char)*p))
				break;

		end = p - 1;

		while (end > start)
			if ('.' == *end || ',' == *end ||
					'\'' == *end || '"' == *end ||
					')' == *end || '!' == *end ||
					'?' == *end || ':' == *end ||
					';' == *end)
				end--;
			else
				break;

		if (emit && end - start >= 1) {
			for ( ; start <= end; start++)
				if (ASCII_HYPH == *start)
					putchar('-');
				else
					putchar((unsigned char)*start);
			putchar('\n');
		}

		if (isspace((unsigned char)*p))
			goto again;

		return;
	}

	while (*colp < col) {
		putchar(' ');
		(*colp)++;
	}

	/*
	 * Print the input word, skipping any special characters.
	 */
	while ('\0' != *p)
		if ('\\' == *p) {
			p++;
			esc = mandoc_escape(&p, NULL, NULL);
			if (ESCAPE_ERROR == esc)
				break;
		} else {
			putchar((unsigned char )*p++);
			(*colp)++;
		}
}

static void
pline(int line, int *linep, int *col, int list)
{

	if (list)
		return;

	/*
	 * Print out as many lines as needed to reach parity with the
	 * original input.
	 */

	while (*linep < line) {
		putchar('\n');
		(*linep)++;
	}

	*col = 0;
}

static void
pmdoc(const struct roff_node *p, int *line, int *col, int list)
{

	for ( ; p; p = p->next) {
		if (NODE_LINE & p->flags)
			pline(p->line, line, col, list);
		if (ROFFT_TEXT == p->type)
			pstring(p->string, p->pos, col, list);
		if (p->child)
			pmdoc(p->child, line, col, list);
	}
}

static void
pman(const struct roff_node *p, int *line, int *col, int list)
{

	for ( ; p; p = p->next) {
		if (NODE_LINE & p->flags)
			pline(p->line, line, col, list);
		if (ROFFT_TEXT == p->type)
			pstring(p->string, p->pos, col, list);
		if (p->child)
			pman(p->child, line, col, list);
	}
}
