/*	$Id: manconf.h,v 1.5 2017/07/01 09:47:30 schwarze Exp $ */
/*
 * Copyright (c) 2011, 2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
 * Copyright (c) 2011 Kristaps Dzonsons <kristaps@bsd.lv>
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

/* List of unique, absolute paths to manual trees. */

struct	manpaths {
	char	**paths;
	size_t	  sz;
};

/* Data from -O options and man.conf(5) output directives. */

struct	manoutput {
	char	 *includes;
	char	 *man;
	char	 *paper;
	char	 *style;
	size_t	  indent;
	size_t	  width;
	int	  fragment;
	int	  mdoc;
	int	  synopsisonly;
	int	  noval;
};

struct	manconf {
	struct manoutput	  output;
	struct manpaths		  manpath;
};


void	 manconf_parse(struct manconf *, const char *, char *, char *);
int	 manconf_output(struct manoutput *, const char *, int);
void	 manconf_free(struct manconf *);
void	 manpath_base(struct manpaths *);
