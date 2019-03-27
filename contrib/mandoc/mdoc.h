/*	$Id: mdoc.h,v 1.145 2017/04/24 23:06:18 schwarze Exp $ */
/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014, 2015 Ingo Schwarze <schwarze@openbsd.org>
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

enum	mdocargt {
	MDOC_Split, /* -split */
	MDOC_Nosplit, /* -nospli */
	MDOC_Ragged, /* -ragged */
	MDOC_Unfilled, /* -unfilled */
	MDOC_Literal, /* -literal */
	MDOC_File, /* -file */
	MDOC_Offset, /* -offset */
	MDOC_Bullet, /* -bullet */
	MDOC_Dash, /* -dash */
	MDOC_Hyphen, /* -hyphen */
	MDOC_Item, /* -item */
	MDOC_Enum, /* -enum */
	MDOC_Tag, /* -tag */
	MDOC_Diag, /* -diag */
	MDOC_Hang, /* -hang */
	MDOC_Ohang, /* -ohang */
	MDOC_Inset, /* -inset */
	MDOC_Column, /* -column */
	MDOC_Width, /* -width */
	MDOC_Compact, /* -compact */
	MDOC_Std, /* -std */
	MDOC_Filled, /* -filled */
	MDOC_Words, /* -words */
	MDOC_Emphasis, /* -emphasis */
	MDOC_Symbolic, /* -symbolic */
	MDOC_Nested, /* -nested */
	MDOC_Centred, /* -centered */
	MDOC_ARG_MAX
};

/*
 * An argument to a macro (multiple values = `-column xxx yyy').
 */
struct	mdoc_argv {
	enum mdocargt	  arg; /* type of argument */
	int		  line;
	int		  pos;
	size_t		  sz; /* elements in "value" */
	char		**value; /* argument strings */
};

/*
 * Reference-counted macro arguments.  These are refcounted because
 * blocks have multiple instances of the same arguments spread across
 * the HEAD, BODY, TAIL, and BLOCK node types.
 */
struct	mdoc_arg {
	size_t		  argc;
	struct mdoc_argv *argv;
	unsigned int	  refcnt;
};

enum	mdoc_list {
	LIST__NONE = 0,
	LIST_bullet, /* -bullet */
	LIST_column, /* -column */
	LIST_dash, /* -dash */
	LIST_diag, /* -diag */
	LIST_enum, /* -enum */
	LIST_hang, /* -hang */
	LIST_hyphen, /* -hyphen */
	LIST_inset, /* -inset */
	LIST_item, /* -item */
	LIST_ohang, /* -ohang */
	LIST_tag, /* -tag */
	LIST_MAX
};

enum	mdoc_disp {
	DISP__NONE = 0,
	DISP_centered, /* -centered */
	DISP_ragged, /* -ragged */
	DISP_unfilled, /* -unfilled */
	DISP_filled, /* -filled */
	DISP_literal /* -literal */
};

enum	mdoc_auth {
	AUTH__NONE = 0,
	AUTH_split, /* -split */
	AUTH_nosplit /* -nosplit */
};

enum	mdoc_font {
	FONT__NONE = 0,
	FONT_Em, /* Em, -emphasis */
	FONT_Li, /* Li, -literal */
	FONT_Sy /* Sy, -symbolic */
};

struct	mdoc_bd {
	const char	 *offs; /* -offset */
	enum mdoc_disp	  type; /* -ragged, etc. */
	int		  comp; /* -compact */
};

struct	mdoc_bl {
	const char	 *width; /* -width */
	const char	 *offs; /* -offset */
	enum mdoc_list	  type; /* -tag, -enum, etc. */
	int		  comp; /* -compact */
	size_t		  ncols; /* -column arg count */
	const char	**cols; /* -column val ptr */
	int		  count; /* -enum counter */
};

struct	mdoc_bf {
	enum mdoc_font	  font; /* font */
};

struct	mdoc_an {
	enum mdoc_auth	  auth; /* -split, etc. */
};

struct	mdoc_rs {
	int		  quote_T; /* whether to quote %T */
};

/*
 * Consists of normalised node arguments.  These should be used instead
 * of iterating through the mdoc_arg pointers of a node: defaults are
 * provided, etc.
 */
union	mdoc_data {
	struct mdoc_an	  An;
	struct mdoc_bd	  Bd;
	struct mdoc_bf	  Bf;
	struct mdoc_bl	  Bl;
	struct roff_node *Es;
	struct mdoc_rs	  Rs;
};

/* Names of macro args.  Index is enum mdocargt. */
extern	const char *const *mdoc_argnames;

void		 mdoc_validate(struct roff_man *);
