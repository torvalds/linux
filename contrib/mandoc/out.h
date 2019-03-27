/*	$Id: out.h,v 1.32 2018/06/25 16:54:59 schwarze Exp $ */
/*
 * Copyright (c) 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014, 2017 Ingo Schwarze <schwarze@openbsd.org>
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

enum	roffscale {
	SCALE_CM, /* centimeters (c) */
	SCALE_IN, /* inches (i) */
	SCALE_PC, /* pica (P) */
	SCALE_PT, /* points (p) */
	SCALE_EM, /* ems (m) */
	SCALE_MM, /* mini-ems (M) */
	SCALE_EN, /* ens (n) */
	SCALE_BU, /* default horizontal (u) */
	SCALE_VS, /* default vertical (v) */
	SCALE_FS, /* syn. for u (f) */
	SCALE_MAX
};

struct	roffcol {
	size_t		 width; /* width of cell */
	size_t		 decimal; /* decimal position in cell */
	size_t		 spacing; /* spacing after the column */
	int		 flags; /* layout flags, see tbl_cell */
};

struct	roffsu {
	enum roffscale	  unit;
	double		  scale;
};

typedef	size_t	(*tbl_sulen)(const struct roffsu *, void *);
typedef	size_t	(*tbl_strlen)(const char *, void *);
typedef	size_t	(*tbl_len)(size_t, void *);

struct	rofftbl {
	tbl_sulen	 sulen; /* calculate scaling unit length */
	tbl_strlen	 slen; /* calculate string length */
	tbl_len		 len; /* produce width of empty space */
	struct roffcol	*cols; /* master column specifiers */
	void		*arg; /* passed to sulen, slen, and len */
};

#define	SCALE_HS_INIT(p, v) \
	do { (p)->unit = SCALE_EN; \
	     (p)->scale = (v); } \
	while (/* CONSTCOND */ 0)


struct	tbl_span;

const char	 *a2roffsu(const char *, struct roffsu *, enum roffscale);
void		  tblcalc(struct rofftbl *tbl,
			const struct tbl_span *, size_t, size_t);
