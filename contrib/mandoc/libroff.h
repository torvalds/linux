/*	$Id: libroff.h,v 1.42 2017/07/08 17:52:49 schwarze Exp $ */
/*
 * Copyright (c) 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
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

enum	tbl_part {
	TBL_PART_OPTS, /* in options (first line) */
	TBL_PART_LAYOUT, /* describing layout */
	TBL_PART_DATA, /* creating data rows */
	TBL_PART_CDATA /* continue previous row */
};

struct	tbl_node {
	struct mparse	 *parse; /* parse point */
	int		  pos; /* invocation column */
	int		  line; /* invocation line */
	enum tbl_part	  part;
	struct tbl_opts	  opts;
	struct tbl_row	 *first_row;
	struct tbl_row	 *last_row;
	struct tbl_span	 *first_span;
	struct tbl_span	 *current_span;
	struct tbl_span	 *last_span;
	struct tbl_node	 *next;
};

struct	eqn_node {
	struct mparse	 *parse;  /* main parser, for error reporting */
	struct roff_node *node;   /* syntax tree of this equation */
	struct eqn_def	 *defs;   /* array of definitions */
	char		 *data;   /* source code of this equation */
	char		 *start;  /* first byte of the current token */
	char		 *end;	  /* first byte of the next token */
	size_t		  defsz;  /* number of definitions */
	size_t		  sz;     /* length of the source code */
	size_t		  toksz;  /* length of the current token */
	int		  gsize;  /* default point size */
	int		  delim;  /* in-line delimiters enabled */
	char		  odelim; /* in-line opening delimiter */
	char		  cdelim; /* in-line closing delimiter */
};

struct	eqn_def {
	char		 *key;
	size_t		  keysz;
	char		 *val;
	size_t		  valsz;
};


struct tbl_node	*tbl_alloc(int, int, struct mparse *);
void		 tbl_restart(int, int, struct tbl_node *);
void		 tbl_free(struct tbl_node *);
void		 tbl_reset(struct tbl_node *);
void		 tbl_read(struct tbl_node *, int, const char *, int);
void		 tbl_option(struct tbl_node *, int, const char *, int *);
void		 tbl_layout(struct tbl_node *, int, const char *, int);
void		 tbl_data(struct tbl_node *, int, const char *, int);
void		 tbl_cdata(struct tbl_node *, int, const char *, int);
const struct tbl_span	*tbl_span(struct tbl_node *);
int		 tbl_end(struct tbl_node *);
struct eqn_node	*eqn_alloc(struct mparse *);
void		 eqn_box_free(struct eqn_box *);
void		 eqn_free(struct eqn_node *);
void		 eqn_parse(struct eqn_node *);
void		 eqn_read(struct eqn_node *, const char *);
void		 eqn_reset(struct eqn_node *);
