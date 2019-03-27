/*	$Id: term_ascii.c,v 1.61 2018/05/20 21:37:34 schwarze Exp $ */
/*
 * Copyright (c) 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014, 2015, 2017, 2018 Ingo Schwarze <schwarze@openbsd.org>
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
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#if HAVE_WCHAR
#include <langinfo.h>
#include <locale.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if HAVE_WCHAR
#include <wchar.h>
#endif

#include "mandoc.h"
#include "mandoc_aux.h"
#include "out.h"
#include "term.h"
#include "manconf.h"
#include "main.h"

static	struct termp	 *ascii_init(enum termenc, const struct manoutput *);
static	int		  ascii_hspan(const struct termp *,
				const struct roffsu *);
static	size_t		  ascii_width(const struct termp *, int);
static	void		  ascii_advance(struct termp *, size_t);
static	void		  ascii_begin(struct termp *);
static	void		  ascii_end(struct termp *);
static	void		  ascii_endline(struct termp *);
static	void		  ascii_letter(struct termp *, int);
static	void		  ascii_setwidth(struct termp *, int, int);

#if HAVE_WCHAR
static	void		  locale_advance(struct termp *, size_t);
static	void		  locale_endline(struct termp *);
static	void		  locale_letter(struct termp *, int);
static	size_t		  locale_width(const struct termp *, int);
#endif


static struct termp *
ascii_init(enum termenc enc, const struct manoutput *outopts)
{
#if HAVE_WCHAR
	char		*v;
#endif
	struct termp	*p;

	p = mandoc_calloc(1, sizeof(*p));
	p->tcol = p->tcols = mandoc_calloc(1, sizeof(*p->tcol));
	p->maxtcol = 1;

	p->line = 1;
	p->defrmargin = p->lastrmargin = 78;
	p->fontq = mandoc_reallocarray(NULL,
	     (p->fontsz = 8), sizeof(*p->fontq));
	p->fontq[0] = p->fontl = TERMFONT_NONE;

	p->begin = ascii_begin;
	p->end = ascii_end;
	p->hspan = ascii_hspan;
	p->type = TERMTYPE_CHAR;

	p->enc = TERMENC_ASCII;
	p->advance = ascii_advance;
	p->endline = ascii_endline;
	p->letter = ascii_letter;
	p->setwidth = ascii_setwidth;
	p->width = ascii_width;

#if HAVE_WCHAR
	if (TERMENC_ASCII != enc) {

		/*
		 * Do not change any of this to LC_ALL.  It might break
		 * the formatting by subtly changing the behaviour of
		 * various functions, for example strftime(3).  As a
		 * worst case, it might even cause buffer overflows.
		 */

		v = TERMENC_LOCALE == enc ?
		    setlocale(LC_CTYPE, "") :
		    setlocale(LC_CTYPE, UTF8_LOCALE);

		/*
		 * We only support UTF-8,
		 * so revert to ASCII for anything else.
		 */

		if (v != NULL &&
		    strcmp(nl_langinfo(CODESET), "UTF-8") != 0)
			v = setlocale(LC_CTYPE, "C");

		if (v != NULL && MB_CUR_MAX > 1) {
			p->enc = enc;
			p->advance = locale_advance;
			p->endline = locale_endline;
			p->letter = locale_letter;
			p->width = locale_width;
		}
	}
#endif

	if (outopts->mdoc) {
		p->mdocstyle = 1;
		p->defindent = 5;
	}
	if (outopts->indent)
		p->defindent = outopts->indent;
	if (outopts->width)
		p->defrmargin = outopts->width;
	if (outopts->synopsisonly)
		p->synopsisonly = 1;

	assert(p->defindent < UINT16_MAX);
	assert(p->defrmargin < UINT16_MAX);
	return p;
}

void *
ascii_alloc(const struct manoutput *outopts)
{

	return ascii_init(TERMENC_ASCII, outopts);
}

void *
utf8_alloc(const struct manoutput *outopts)
{

	return ascii_init(TERMENC_UTF8, outopts);
}

void *
locale_alloc(const struct manoutput *outopts)
{

	return ascii_init(TERMENC_LOCALE, outopts);
}

static void
ascii_setwidth(struct termp *p, int iop, int width)
{

	width /= 24;
	p->tcol->rmargin = p->defrmargin;
	if (iop > 0)
		p->defrmargin += width;
	else if (iop == 0)
		p->defrmargin = width ? (size_t)width : p->lastrmargin;
	else if (p->defrmargin > (size_t)width)
		p->defrmargin -= width;
	else
		p->defrmargin = 0;
	if (p->defrmargin > 1000)
		p->defrmargin = 1000;
	p->lastrmargin = p->tcol->rmargin;
	p->tcol->rmargin = p->maxrmargin = p->defrmargin;
}

void
terminal_sepline(void *arg)
{
	struct termp	*p;
	size_t		 i;

	p = (struct termp *)arg;
	(*p->endline)(p);
	for (i = 0; i < p->defrmargin; i++)
		(*p->letter)(p, '-');
	(*p->endline)(p);
	(*p->endline)(p);
}

static size_t
ascii_width(const struct termp *p, int c)
{

	return 1;
}

void
ascii_free(void *arg)
{

	term_free((struct termp *)arg);
}

static void
ascii_letter(struct termp *p, int c)
{

	putchar(c);
}

static void
ascii_begin(struct termp *p)
{

	(*p->headf)(p, p->argf);
}

static void
ascii_end(struct termp *p)
{

	(*p->footf)(p, p->argf);
}

static void
ascii_endline(struct termp *p)
{

	p->line++;
	p->tcol->offset -= p->ti;
	p->ti = 0;
	putchar('\n');
}

static void
ascii_advance(struct termp *p, size_t len)
{
	size_t		i;

	assert(len < UINT16_MAX);
	for (i = 0; i < len; i++)
		putchar(' ');
}

static int
ascii_hspan(const struct termp *p, const struct roffsu *su)
{
	double		 r;

	switch (su->unit) {
	case SCALE_BU:
		r = su->scale;
		break;
	case SCALE_CM:
		r = su->scale * 240.0 / 2.54;
		break;
	case SCALE_FS:
		r = su->scale * 65536.0;
		break;
	case SCALE_IN:
		r = su->scale * 240.0;
		break;
	case SCALE_MM:
		r = su->scale * 0.24;
		break;
	case SCALE_VS:
	case SCALE_PC:
		r = su->scale * 40.0;
		break;
	case SCALE_PT:
		r = su->scale * 10.0 / 3.0;
		break;
	case SCALE_EN:
	case SCALE_EM:
		r = su->scale * 24.0;
		break;
	default:
		abort();
	}
	return r > 0.0 ? r + 0.01 : r - 0.01;
}

const char *
ascii_uc2str(int uc)
{
	static const char nbrsp[2] = { ASCII_NBRSP, '\0' };
	static const char *tab[] = {
	"<NUL>","<SOH>","<STX>","<ETX>","<EOT>","<ENQ>","<ACK>","<BEL>",
	"<BS>",	"\t",	"<LF>",	"<VT>",	"<FF>",	"<CR>",	"<SO>",	"<SI>",
	"<DLE>","<DC1>","<DC2>","<DC3>","<DC4>","<NAK>","<SYN>","<ETB>",
	"<CAN>","<EM>",	"<SUB>","<ESC>","<FS>",	"<GS>",	"<RS>",	"<US>",
	" ",	"!",	"\"",	"#",	"$",	"%",	"&",	"'",
	"(",	")",	"*",	"+",	",",	"-",	".",	"/",
	"0",	"1",	"2",	"3",	"4",	"5",	"6",	"7",
	"8",	"9",	":",	";",	"<",	"=",	">",	"?",
	"@",	"A",	"B",	"C",	"D",	"E",	"F",	"G",
	"H",	"I",	"J",	"K",	"L",	"M",	"N",	"O",
	"P",	"Q",	"R",	"S",	"T",	"U",	"V",	"W",
	"X",	"Y",	"Z",	"[",	"\\",	"]",	"^",	"_",
	"`",	"a",	"b",	"c",	"d",	"e",	"f",	"g",
	"h",	"i",	"j",	"k",	"l",	"m",	"n",	"o",
	"p",	"q",	"r",	"s",	"t",	"u",	"v",	"w",
	"x",	"y",	"z",	"{",	"|",	"}",	"~",	"<DEL>",
	"<80>",	"<81>",	"<82>",	"<83>",	"<84>",	"<85>",	"<86>",	"<87>",
	"<88>",	"<89>",	"<8A>",	"<8B>",	"<8C>",	"<8D>",	"<8E>",	"<8F>",
	"<90>",	"<91>",	"<92>",	"<93>",	"<94>",	"<95>",	"<96>",	"<97>",
	"<98>",	"<99>",	"<9A>",	"<9B>",	"<9C>",	"<9D>",	"<9E>",	"<9F>",
	nbrsp,	"!",	"/\bc",	"GBP",	"o\bx",	"=\bY",	"|",	"<section>",
	"\"",	"(C)",	"_\ba",	"<<",	"~",	"",	"(R)",	"-",
	"<degree>","+-","^2",	"^3",	"'","<micro>","<paragraph>",".",
	",",	"^1",	"_\bo",	">>",	"1/4",	"1/2",	"3/4",	"?",
	"`\bA",	"'\bA",	"^\bA",	"~\bA",	"\"\bA","o\bA",	"AE",	",\bC",
	"`\bE",	"'\bE",	"^\bE",	"\"\bE","`\bI",	"'\bI",	"^\bI",	"\"\bI",
	"Dh",	"~\bN",	"`\bO",	"'\bO",	"^\bO",	"~\bO",	"\"\bO","x",
	"/\bO",	"`\bU",	"'\bU",	"^\bU",	"\"\bU","'\bY",	"Th",	"ss",
	"`\ba",	"'\ba",	"^\ba",	"~\ba",	"\"\ba","o\ba",	"ae",	",\bc",
	"`\be",	"'\be",	"^\be",	"\"\be","`\bi",	"'\bi",	"^\bi",	"\"\bi",
	"dh",	"~\bn",	"`\bo",	"'\bo",	"^\bo",	"~\bo",	"\"\bo","/",
	"/\bo",	"`\bu",	"'\bu",	"^\bu",	"\"\bu","'\by",	"th",	"\"\by",
	"A",	"a",	"A",	"a",	"A",	"a",	"'\bC",	"'\bc",
	"^\bC",	"^\bc",	"C",	"c",	"C",	"c",	"D",	"d",
	"/\bD",	"/\bd",	"E",	"e",	"E",	"e",	"E",	"e",
	"E",	"e",	"E",	"e",	"^\bG",	"^\bg",	"G",	"g",
	"G",	"g",	",\bG",	",\bg",	"^\bH",	"^\bh",	"/\bH",	"/\bh",
	"~\bI",	"~\bi",	"I",	"i",	"I",	"i",	"I",	"i",
	"I",	"i",	"IJ",	"ij",	"^\bJ",	"^\bj",	",\bK",	",\bk",
	"q",	"'\bL",	"'\bl",	",\bL",	",\bl",	"L",	"l",	"L",
	"l",	"/\bL",	"/\bl",	"'\bN",	"'\bn",	",\bN",	",\bn",	"N",
	"n",	"'n",	"Ng",	"ng",	"O",	"o",	"O",	"o",
	"O",	"o",	"OE",	"oe",	"'\bR",	"'\br",	",\bR",	",\br",
	"R",	"r",	"'\bS",	"'\bs",	"^\bS",	"^\bs",	",\bS",	",\bs",
	"S",	"s",	",\bT",	",\bt",	"T",	"t",	"/\bT",	"/\bt",
	"~\bU",	"~\bu",	"U",	"u",	"U",	"u",	"U",	"u",
	"U",	"u",	"U",	"u",	"^\bW",	"^\bw",	"^\bY",	"^\by",
	"\"\bY","'\bZ",	"'\bz",	"Z",	"z",	"Z",	"z",	"s",
	"b",	"B",	"B",	"b",	"6",	"6",	"O",	"C",
	"c",	"D",	"D",	"D",	"d",	"d",	"3",	"@",
	"E",	"F",	",\bf",	"G",	"G",	"hv",	"I",	"/\bI",
	"K",	"k",	"/\bl",	"l",	"W",	"N",	"n",	"~\bO",
	"O",	"o",	"OI",	"oi",	"P",	"p",	"YR",	"2",
	"2",	"SH",	"sh",	"t",	"T",	"t",	"T",	"U",
	"u",	"Y",	"V",	"Y",	"y",	"/\bZ",	"/\bz",	"ZH",
	"ZH",	"zh",	"zh",	"/\b2",	"5",	"5",	"ts",	"w",
	"|",	"||",	"|=",	"!",	"DZ",	"Dz",	"dz",	"LJ",
	"Lj",	"lj",	"NJ",	"Nj",	"nj",	"A",	"a",	"I",
	"i",	"O",	"o",	"U",	"u",	"U",	"u",	"U",
	"u",	"U",	"u",	"U",	"u",	"@",	"A",	"a",
	"A",	"a",	"AE",	"ae",	"/\bG",	"/\bg",	"G",	"g",
	"K",	"k",	"O",	"o",	"O",	"o",	"ZH",	"zh",
	"j",	"DZ",	"Dz",	"dz",	"'\bG",	"'\bg",	"HV",	"W",
	"`\bN",	"`\bn",	"A",	"a",	"'\bAE","'\bae","O",	"o"};

	assert(uc >= 0);
	if ((size_t)uc < sizeof(tab)/sizeof(tab[0]))
		return tab[uc];
	return mchars_uc2str(uc);
}

#if HAVE_WCHAR
static size_t
locale_width(const struct termp *p, int c)
{
	int		rc;

	if (c == ASCII_NBRSP)
		c = ' ';
	rc = wcwidth(c);
	if (rc < 0)
		rc = 0;
	return rc;
}

static void
locale_advance(struct termp *p, size_t len)
{
	size_t		i;

	assert(len < UINT16_MAX);
	for (i = 0; i < len; i++)
		putwchar(L' ');
}

static void
locale_endline(struct termp *p)
{

	p->line++;
	p->tcol->offset -= p->ti;
	p->ti = 0;
	putwchar(L'\n');
}

static void
locale_letter(struct termp *p, int c)
{

	putwchar(c);
}
#endif
