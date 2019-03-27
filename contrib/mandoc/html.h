/*	$Id: html.h,v 1.92 2018/06/25 16:54:59 schwarze Exp $ */
/*
 * Copyright (c) 2008-2011, 2014 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2017, 2018 Ingo Schwarze <schwarze@openbsd.org>
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

enum	htmltag {
	TAG_HTML,
	TAG_HEAD,
	TAG_BODY,
	TAG_META,
	TAG_TITLE,
	TAG_DIV,
	TAG_IDIV,
	TAG_H1,
	TAG_H2,
	TAG_SPAN,
	TAG_LINK,
	TAG_BR,
	TAG_A,
	TAG_TABLE,
	TAG_TR,
	TAG_TD,
	TAG_LI,
	TAG_UL,
	TAG_OL,
	TAG_DL,
	TAG_DT,
	TAG_DD,
	TAG_PRE,
	TAG_VAR,
	TAG_CITE,
	TAG_B,
	TAG_I,
	TAG_CODE,
	TAG_SMALL,
	TAG_STYLE,
	TAG_MATH,
	TAG_MROW,
	TAG_MI,
	TAG_MN,
	TAG_MO,
	TAG_MSUP,
	TAG_MSUB,
	TAG_MSUBSUP,
	TAG_MFRAC,
	TAG_MSQRT,
	TAG_MFENCED,
	TAG_MTABLE,
	TAG_MTR,
	TAG_MTD,
	TAG_MUNDEROVER,
	TAG_MUNDER,
	TAG_MOVER,
	TAG_MAX
};

enum	htmlfont {
	HTMLFONT_NONE = 0,
	HTMLFONT_BOLD,
	HTMLFONT_ITALIC,
	HTMLFONT_BI,
	HTMLFONT_MAX
};

struct	tag {
	struct tag	 *next;
	enum htmltag	  tag;
};

struct	html {
	int		  flags;
#define	HTML_NOSPACE	 (1 << 0) /* suppress next space */
#define	HTML_IGNDELIM	 (1 << 1)
#define	HTML_KEEP	 (1 << 2)
#define	HTML_PREKEEP	 (1 << 3)
#define	HTML_NONOSPACE	 (1 << 4) /* never add spaces */
#define	HTML_LITERAL	 (1 << 5) /* literal (e.g., <PRE>) context */
#define	HTML_SKIPCHAR	 (1 << 6) /* skip the next character */
#define	HTML_NOSPLIT	 (1 << 7) /* do not break line before .An */
#define	HTML_SPLIT	 (1 << 8) /* break line before .An */
#define	HTML_NONEWLINE	 (1 << 9) /* No line break in nofill mode. */
#define	HTML_BUFFER	 (1 << 10) /* Collect a word to see if it fits. */
	size_t		  indent; /* current output indentation level */
	int		  noindent; /* indent disabled by <pre> */
	size_t		  col; /* current output byte position */
	size_t		  bufcol; /* current buf byte position */
	char		  buf[80]; /* output buffer */
	struct tag	 *tag; /* last open tag */
	struct rofftbl	  tbl; /* current table */
	struct tag	 *tblt; /* current open table scope */
	char		 *base_man; /* base for manpage href */
	char		 *base_includes; /* base for include href */
	char		 *style; /* style-sheet URI */
	struct tag	 *metaf; /* current open font scope */
	enum htmlfont	  metal; /* last used font */
	enum htmlfont	  metac; /* current font mode */
	int		  oflags; /* output options */
#define	HTML_FRAGMENT	 (1 << 0) /* don't emit HTML/HEAD/BODY */
};


struct	roff_node;
struct	tbl_span;
struct	eqn_box;

void		  roff_html_pre(struct html *, const struct roff_node *);

void		  print_gen_comment(struct html *, struct roff_node *);
void		  print_gen_decls(struct html *);
void		  print_gen_head(struct html *);
struct tag	 *print_otag(struct html *, enum htmltag, const char *, ...);
void		  print_tagq(struct html *, const struct tag *);
void		  print_stagq(struct html *, const struct tag *);
void		  print_text(struct html *, const char *);
void		  print_tblclose(struct html *);
void		  print_tbl(struct html *, const struct tbl_span *);
void		  print_eqn(struct html *, const struct eqn_box *);
void		  print_paragraph(struct html *);
void		  print_endline(struct html *);

char		 *html_make_id(const struct roff_node *, int);
