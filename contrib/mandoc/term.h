/*	$Id: term.h,v 1.130 2017/07/08 14:51:05 schwarze Exp $ */
/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2011-2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
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

enum	termenc {
	TERMENC_ASCII,
	TERMENC_LOCALE,
	TERMENC_UTF8
};

enum	termtype {
	TERMTYPE_CHAR,
	TERMTYPE_PS,
	TERMTYPE_PDF
};

enum	termfont {
	TERMFONT_NONE = 0,
	TERMFONT_BOLD,
	TERMFONT_UNDER,
	TERMFONT_BI,
	TERMFONT__MAX
};

struct	eqn_box;
struct	roff_meta;
struct	roff_node;
struct	tbl_span;
struct	termp;

typedef void	(*term_margin)(struct termp *, const struct roff_meta *);

struct	termp_tbl {
	int		  width;	/* width in fixed chars */
	int		  decimal;	/* decimal point position */
};

struct	termp_col {
	int		 *buf;		/* Output buffer. */
	size_t		  maxcols;	/* Allocated bytes in buf. */
	size_t		  lastcol;	/* Last byte in buf. */
	size_t		  col;		/* Byte in buf to be written. */
	size_t		  rmargin;	/* Current right margin. */
	size_t		  offset;	/* Current left margin. */
};

struct	termp {
	struct rofftbl	  tbl;		/* Table configuration. */
	struct termp_col *tcols;	/* Array of table columns. */
	struct termp_col *tcol;		/* Current table column. */
	size_t		  maxtcol;	/* Allocated table columns. */
	size_t		  lasttcol;	/* Last column currently used. */
	size_t		  line;		/* Current output line number. */
	size_t		  defindent;	/* Default indent for text. */
	size_t		  defrmargin;	/* Right margin of the device. */
	size_t		  lastrmargin;	/* Right margin before the last ll. */
	size_t		  maxrmargin;	/* Max right margin. */
	size_t		  col;		/* Byte position in buf. */
	size_t		  viscol;	/* Chars on current line. */
	size_t		  trailspace;	/* See term_flushln(). */
	size_t		  minbl;	/* Minimum blanks before next field. */
	int		  synopsisonly; /* Print the synopsis only. */
	int		  mdocstyle;	/* Imitate mdoc(7) output. */
	int		  ti;		/* Temporary indent for one line. */
	int		  skipvsp;	/* Vertical space to skip. */
	int		  flags;
#define	TERMP_SENTENCE	 (1 << 0)	/* Space before a sentence. */
#define	TERMP_NOSPACE	 (1 << 1)	/* No space before words. */
#define	TERMP_NONOSPACE	 (1 << 2)	/* No space (no autounset). */
#define	TERMP_NBRWORD	 (1 << 3)	/* Make next word nonbreaking. */
#define	TERMP_KEEP	 (1 << 4)	/* Keep words together. */
#define	TERMP_PREKEEP	 (1 << 5)	/* ...starting with the next one. */
#define	TERMP_BACKAFTER	 (1 << 6)	/* Back up after next character. */
#define	TERMP_BACKBEFORE (1 << 7)	/* Back up before next character. */
#define	TERMP_NOBREAK	 (1 << 8)	/* See term_flushln(). */
#define	TERMP_BRTRSP	 (1 << 9)	/* See term_flushln(). */
#define	TERMP_BRIND	 (1 << 10)	/* See term_flushln(). */
#define	TERMP_HANG	 (1 << 11)	/* See term_flushln(). */
#define	TERMP_NOPAD	 (1 << 12)	/* See term_flushln(). */
#define	TERMP_NOSPLIT	 (1 << 13)	/* Do not break line before .An. */
#define	TERMP_SPLIT	 (1 << 14)	/* Break line before .An. */
#define	TERMP_NONEWLINE	 (1 << 15)	/* No line break in nofill mode. */
#define	TERMP_BRNEVER	 (1 << 16)	/* Don't even break at maxrmargin. */
#define	TERMP_NOBUF	 (1 << 17)	/* Bypass output buffer. */
#define	TERMP_NEWMC	 (1 << 18)	/* No .mc printed yet. */
#define	TERMP_ENDMC	 (1 << 19)	/* Next break ends .mc mode. */
#define	TERMP_MULTICOL	 (1 << 20)	/* Multiple column mode. */
	enum termtype	  type;		/* Terminal, PS, or PDF. */
	enum termenc	  enc;		/* Type of encoding. */
	enum termfont	  fontl;	/* Last font set. */
	enum termfont	 *fontq;	/* Symmetric fonts. */
	int		  fontsz;	/* Allocated size of font stack */
	int		  fonti;	/* Index of font stack. */
	term_margin	  headf;	/* invoked to print head */
	term_margin	  footf;	/* invoked to print foot */
	void		(*letter)(struct termp *, int);
	void		(*begin)(struct termp *);
	void		(*end)(struct termp *);
	void		(*endline)(struct termp *);
	void		(*advance)(struct termp *, size_t);
	void		(*setwidth)(struct termp *, int, int);
	size_t		(*width)(const struct termp *, int);
	int		(*hspan)(const struct termp *,
				const struct roffsu *);
	const void	 *argf;		/* arg for headf/footf */
	const char	 *mc;		/* Margin character. */
	struct termp_ps	 *ps;
};


const char	 *ascii_uc2str(int);

void		  roff_term_pre(struct termp *, const struct roff_node *);

void		  term_eqn(struct termp *, const struct eqn_box *);
void		  term_tbl(struct termp *, const struct tbl_span *);
void		  term_free(struct termp *);
void		  term_setcol(struct termp *, size_t);
void		  term_newln(struct termp *);
void		  term_vspace(struct termp *);
void		  term_word(struct termp *, const char *);
void		  term_flushln(struct termp *);
void		  term_begin(struct termp *, term_margin,
			term_margin, const struct roff_meta *);
void		  term_end(struct termp *);

void		  term_setwidth(struct termp *, const char *);
int		  term_hspan(const struct termp *, const struct roffsu *);
int		  term_hen(const struct termp *, const struct roffsu *);
int		  term_vspan(const struct termp *, const struct roffsu *);
size_t		  term_strlen(const struct termp *, const char *);
size_t		  term_len(const struct termp *, size_t);

void		  term_tab_set(const struct termp *, const char *);
void		  term_tab_iset(size_t);
size_t		  term_tab_next(size_t);

void		  term_fontpush(struct termp *, enum termfont);
void		  term_fontpop(struct termp *);
void		  term_fontpopq(struct termp *, int);
void		  term_fontrepl(struct termp *, enum termfont);
void		  term_fontlast(struct termp *);
