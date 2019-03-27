/*	$Id: term_ps.c,v 1.91 2017/11/10 23:42:52 schwarze Exp $ */
/*
 * Copyright (c) 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014, 2015, 2016, 2017 Ingo Schwarze <schwarze@openbsd.org>
 * Copyright (c) 2017 Marc Espie <espie@openbsd.org>
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
#if HAVE_ERR
#include <err.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mandoc_aux.h"
#include "out.h"
#include "term.h"
#include "manconf.h"
#include "main.h"

/* These work the buffer used by the header and footer. */
#define	PS_BUFSLOP	  128

/* Convert PostScript point "x" to an AFM unit. */
#define	PNT2AFM(p, x) \
	(size_t)((double)(x) * (1000.0 / (double)(p)->ps->scale))

/* Convert an AFM unit "x" to a PostScript points */
#define	AFM2PNT(p, x) \
	((double)(x) / (1000.0 / (double)(p)->ps->scale))

struct	glyph {
	unsigned short	  wx; /* WX in AFM */
};

struct	font {
	const char	 *name; /* FontName in AFM */
#define	MAXCHAR		  95 /* total characters we can handle */
	struct glyph	  gly[MAXCHAR]; /* glyph metrics */
};

struct	termp_ps {
	int		  flags;
#define	PS_INLINE	 (1 << 0)	/* we're in a word */
#define	PS_MARGINS	 (1 << 1)	/* we're in the margins */
#define	PS_NEWPAGE	 (1 << 2)	/* new page, no words yet */
#define	PS_BACKSP	 (1 << 3)	/* last character was backspace */
	size_t		  pscol;	/* visible column (AFM units) */
	size_t		  pscolnext;	/* used for overstrike */
	size_t		  psrow;	/* visible row (AFM units) */
	size_t		  lastrow;	/* psrow of the previous word */
	char		 *psmarg;	/* margin buf */
	size_t		  psmargsz;	/* margin buf size */
	size_t		  psmargcur;	/* cur index in margin buf */
	char		  last;		/* last non-backspace seen */
	enum termfont	  lastf;	/* last set font */
	enum termfont	  nextf;	/* building next font here */
	size_t		  scale;	/* font scaling factor */
	size_t		  pages;	/* number of pages shown */
	size_t		  lineheight;	/* line height (AFM units) */
	size_t		  top;		/* body top (AFM units) */
	size_t		  bottom;	/* body bottom (AFM units) */
	const char	 *medianame;	/* for DocumentMedia and PageSize */
	size_t		  height;	/* page height (AFM units */
	size_t		  width;	/* page width (AFM units) */
	size_t		  lastwidth;	/* page width before last ll */
	size_t		  left;		/* body left (AFM units) */
	size_t		  header;	/* header pos (AFM units) */
	size_t		  footer;	/* footer pos (AFM units) */
	size_t		  pdfbytes;	/* current output byte */
	size_t		  pdflastpg;	/* byte of last page mark */
	size_t		  pdfbody;	/* start of body object */
	size_t		 *pdfobjs;	/* table of object offsets */
	size_t		  pdfobjsz;	/* size of pdfobjs */
};

static	int		  ps_hspan(const struct termp *,
				const struct roffsu *);
static	size_t		  ps_width(const struct termp *, int);
static	void		  ps_advance(struct termp *, size_t);
static	void		  ps_begin(struct termp *);
static	void		  ps_closepage(struct termp *);
static	void		  ps_end(struct termp *);
static	void		  ps_endline(struct termp *);
static	void		  ps_growbuf(struct termp *, size_t);
static	void		  ps_letter(struct termp *, int);
static	void		  ps_pclose(struct termp *);
static	void		  ps_plast(struct termp *);
static	void		  ps_pletter(struct termp *, int);
static	void		  ps_printf(struct termp *, const char *, ...)
				__attribute__((__format__ (__printf__, 2, 3)));
static	void		  ps_putchar(struct termp *, char);
static	void		  ps_setfont(struct termp *, enum termfont);
static	void		  ps_setwidth(struct termp *, int, int);
static	struct termp	 *pspdf_alloc(const struct manoutput *, enum termtype);
static	void		  pdf_obj(struct termp *, size_t);

/*
 * We define, for the time being, three fonts: bold, oblique/italic, and
 * normal (roman).  The following table hard-codes the font metrics for
 * ASCII, i.e., 32--127.
 */

static	const struct font fonts[TERMFONT__MAX] = {
	{ "Times-Roman", {
		{ 250 },
		{ 333 },
		{ 408 },
		{ 500 },
		{ 500 },
		{ 833 },
		{ 778 },
		{ 333 },
		{ 333 },
		{ 333 },
		{ 500 },
		{ 564 },
		{ 250 },
		{ 333 },
		{ 250 },
		{ 278 },
		{ 500 },
		{ 500 },
		{ 500 },
		{ 500 },
		{ 500 },
		{ 500 },
		{ 500 },
		{ 500 },
		{ 500 },
		{ 500 },
		{ 278 },
		{ 278 },
		{ 564 },
		{ 564 },
		{ 564 },
		{ 444 },
		{ 921 },
		{ 722 },
		{ 667 },
		{ 667 },
		{ 722 },
		{ 611 },
		{ 556 },
		{ 722 },
		{ 722 },
		{ 333 },
		{ 389 },
		{ 722 },
		{ 611 },
		{ 889 },
		{ 722 },
		{ 722 },
		{ 556 },
		{ 722 },
		{ 667 },
		{ 556 },
		{ 611 },
		{ 722 },
		{ 722 },
		{ 944 },
		{ 722 },
		{ 722 },
		{ 611 },
		{ 333 },
		{ 278 },
		{ 333 },
		{ 469 },
		{ 500 },
		{ 333 },
		{ 444 },
		{ 500 },
		{ 444 },
		{  500},
		{  444},
		{  333},
		{  500},
		{  500},
		{  278},
		{  278},
		{  500},
		{  278},
		{  778},
		{  500},
		{  500},
		{  500},
		{  500},
		{  333},
		{  389},
		{  278},
		{  500},
		{  500},
		{  722},
		{  500},
		{  500},
		{  444},
		{  480},
		{  200},
		{  480},
		{  541},
	} },
	{ "Times-Bold", {
		{ 250  },
		{ 333  },
		{ 555  },
		{ 500  },
		{ 500  },
		{ 1000 },
		{ 833  },
		{ 333  },
		{ 333  },
		{ 333  },
		{ 500  },
		{ 570  },
		{ 250  },
		{ 333  },
		{ 250  },
		{ 278  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 333  },
		{ 333  },
		{ 570  },
		{ 570  },
		{ 570  },
		{ 500  },
		{ 930  },
		{ 722  },
		{ 667  },
		{ 722  },
		{ 722  },
		{ 667  },
		{ 611  },
		{ 778  },
		{ 778  },
		{ 389  },
		{ 500  },
		{ 778  },
		{ 667  },
		{ 944  },
		{ 722  },
		{ 778  },
		{ 611  },
		{ 778  },
		{ 722  },
		{ 556  },
		{ 667  },
		{ 722  },
		{ 722  },
		{ 1000 },
		{ 722  },
		{ 722  },
		{ 667  },
		{ 333  },
		{ 278  },
		{ 333  },
		{ 581  },
		{ 500  },
		{ 333  },
		{ 500  },
		{ 556  },
		{ 444  },
		{  556 },
		{  444 },
		{  333 },
		{  500 },
		{  556 },
		{  278 },
		{  333 },
		{  556 },
		{  278 },
		{  833 },
		{  556 },
		{  500 },
		{  556 },
		{  556 },
		{  444 },
		{  389 },
		{  333 },
		{  556 },
		{  500 },
		{  722 },
		{  500 },
		{  500 },
		{  444 },
		{  394 },
		{  220 },
		{  394 },
		{  520 },
	} },
	{ "Times-Italic", {
		{ 250  },
		{ 333  },
		{ 420  },
		{ 500  },
		{ 500  },
		{ 833  },
		{ 778  },
		{ 333  },
		{ 333  },
		{ 333  },
		{ 500  },
		{ 675  },
		{ 250  },
		{ 333  },
		{ 250  },
		{ 278  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 500  },
		{ 333  },
		{ 333  },
		{ 675  },
		{ 675  },
		{ 675  },
		{ 500  },
		{ 920  },
		{ 611  },
		{ 611  },
		{ 667  },
		{ 722  },
		{ 611  },
		{ 611  },
		{ 722  },
		{ 722  },
		{ 333  },
		{ 444  },
		{ 667  },
		{ 556  },
		{ 833  },
		{ 667  },
		{ 722  },
		{ 611  },
		{ 722  },
		{ 611  },
		{ 500  },
		{ 556  },
		{ 722  },
		{ 611  },
		{ 833  },
		{ 611  },
		{ 556  },
		{ 556  },
		{ 389  },
		{ 278  },
		{ 389  },
		{ 422  },
		{ 500  },
		{ 333  },
		{ 500  },
		{ 500  },
		{ 444  },
		{  500 },
		{  444 },
		{  278 },
		{  500 },
		{  500 },
		{  278 },
		{  278 },
		{  444 },
		{  278 },
		{  722 },
		{  500 },
		{  500 },
		{  500 },
		{  500 },
		{  389 },
		{  389 },
		{  278 },
		{  500 },
		{  444 },
		{  667 },
		{  444 },
		{  444 },
		{  389 },
		{  400 },
		{  275 },
		{  400 },
		{  541 },
	} },
	{ "Times-BoldItalic", {
		{  250 },
		{  389 },
		{  555 },
		{  500 },
		{  500 },
		{  833 },
		{  778 },
		{  333 },
		{  333 },
		{  333 },
		{  500 },
		{  570 },
		{  250 },
		{  333 },
		{  250 },
		{  278 },
		{  500 },
		{  500 },
		{  500 },
		{  500 },
		{  500 },
		{  500 },
		{  500 },
		{  500 },
		{  500 },
		{  500 },
		{  333 },
		{  333 },
		{  570 },
		{  570 },
		{  570 },
		{  500 },
		{  832 },
		{  667 },
		{  667 },
		{  667 },
		{  722 },
		{  667 },
		{  667 },
		{  722 },
		{  778 },
		{  389 },
		{  500 },
		{  667 },
		{  611 },
		{  889 },
		{  722 },
		{  722 },
		{  611 },
		{  722 },
		{  667 },
		{  556 },
		{  611 },
		{  722 },
		{  667 },
		{  889 },
		{  667 },
		{  611 },
		{  611 },
		{  333 },
		{  278 },
		{  333 },
		{  570 },
		{  500 },
		{  333 },
		{  500 },
		{  500 },
		{  444 },
		{  500 },
		{  444 },
		{  333 },
		{  500 },
		{  556 },
		{  278 },
		{  278 },
		{  500 },
		{  278 },
		{  778 },
		{  556 },
		{  500 },
		{  500 },
		{  500 },
		{  389 },
		{  389 },
		{  278 },
		{  556 },
		{  444 },
		{  667 },
		{  500 },
		{  444 },
		{  389 },
		{  348 },
		{  220 },
		{  348 },
		{  570 },
	} },
};

void *
pdf_alloc(const struct manoutput *outopts)
{
	return pspdf_alloc(outopts, TERMTYPE_PDF);
}

void *
ps_alloc(const struct manoutput *outopts)
{
	return pspdf_alloc(outopts, TERMTYPE_PS);
}

static struct termp *
pspdf_alloc(const struct manoutput *outopts, enum termtype type)
{
	struct termp	*p;
	unsigned int	 pagex, pagey;
	size_t		 marginx, marginy, lineheight;
	const char	*pp;

	p = mandoc_calloc(1, sizeof(*p));
	p->tcol = p->tcols = mandoc_calloc(1, sizeof(*p->tcol));
	p->maxtcol = 1;
	p->type = type;

	p->enc = TERMENC_ASCII;
	p->fontq = mandoc_reallocarray(NULL,
	    (p->fontsz = 8), sizeof(*p->fontq));
	p->fontq[0] = p->fontl = TERMFONT_NONE;
	p->ps = mandoc_calloc(1, sizeof(*p->ps));

	p->advance = ps_advance;
	p->begin = ps_begin;
	p->end = ps_end;
	p->endline = ps_endline;
	p->hspan = ps_hspan;
	p->letter = ps_letter;
	p->setwidth = ps_setwidth;
	p->width = ps_width;

	/* Default to US letter (millimetres). */

	p->ps->medianame = "Letter";
	pagex = 216;
	pagey = 279;

	/*
	 * The ISO-269 paper sizes can be calculated automatically, but
	 * it would require bringing in -lm for pow() and I'd rather not
	 * do that.  So just do it the easy way for now.  Since this
	 * only happens once, I'm not terribly concerned.
	 */

	pp = outopts->paper;
	if (pp != NULL && strcasecmp(pp, "letter") != 0) {
		if (strcasecmp(pp, "a3") == 0) {
			p->ps->medianame = "A3";
			pagex = 297;
			pagey = 420;
		} else if (strcasecmp(pp, "a4") == 0) {
			p->ps->medianame = "A4";
			pagex = 210;
			pagey = 297;
		} else if (strcasecmp(pp, "a5") == 0) {
			p->ps->medianame = "A5";
			pagex = 148;
			pagey = 210;
		} else if (strcasecmp(pp, "legal") == 0) {
			p->ps->medianame = "Legal";
			pagex = 216;
			pagey = 356;
		} else if (sscanf(pp, "%ux%u", &pagex, &pagey) == 2)
			p->ps->medianame = "CustomSize";
		else
			warnx("%s: Unknown paper", pp);
	}

	/*
	 * This MUST be defined before any PNT2AFM or AFM2PNT
	 * calculations occur.
	 */

	p->ps->scale = 11;

	/* Remember millimetres -> AFM units. */

	pagex = PNT2AFM(p, ((double)pagex * 72.0 / 25.4));
	pagey = PNT2AFM(p, ((double)pagey * 72.0 / 25.4));

	/* Margins are 1/9 the page x and y. */

	marginx = (size_t)((double)pagex / 9.0);
	marginy = (size_t)((double)pagey / 9.0);

	/* Line-height is 1.4em. */

	lineheight = PNT2AFM(p, ((double)p->ps->scale * 1.4));

	p->ps->width = p->ps->lastwidth = (size_t)pagex;
	p->ps->height = (size_t)pagey;
	p->ps->header = pagey - (marginy / 2) - (lineheight / 2);
	p->ps->top = pagey - marginy;
	p->ps->footer = (marginy / 2) - (lineheight / 2);
	p->ps->bottom = marginy;
	p->ps->left = marginx;
	p->ps->lineheight = lineheight;

	p->defrmargin = pagex - (marginx * 2);
	return p;
}

static void
ps_setwidth(struct termp *p, int iop, int width)
{
	size_t	 lastwidth;

	lastwidth = p->ps->width;
	if (iop > 0)
		p->ps->width += width;
	else if (iop == 0)
		p->ps->width = width ? (size_t)width : p->ps->lastwidth;
	else if (p->ps->width > (size_t)width)
		p->ps->width -= width;
	else
		p->ps->width = 0;
	p->ps->lastwidth = lastwidth;
}

void
pspdf_free(void *arg)
{
	struct termp	*p;

	p = (struct termp *)arg;

	free(p->ps->psmarg);
	free(p->ps->pdfobjs);

	free(p->ps);
	term_free(p);
}

static void
ps_printf(struct termp *p, const char *fmt, ...)
{
	va_list		 ap;
	int		 pos, len;

	va_start(ap, fmt);

	/*
	 * If we're running in regular mode, then pipe directly into
	 * vprintf().  If we're processing margins, then push the data
	 * into our growable margin buffer.
	 */

	if ( ! (PS_MARGINS & p->ps->flags)) {
		len = vprintf(fmt, ap);
		va_end(ap);
		p->ps->pdfbytes += len < 0 ? 0 : (size_t)len;
		return;
	}

	/*
	 * XXX: I assume that the in-margin print won't exceed
	 * PS_BUFSLOP (128 bytes), which is reasonable but still an
	 * assumption that will cause pukeage if it's not the case.
	 */

	ps_growbuf(p, PS_BUFSLOP);

	pos = (int)p->ps->psmargcur;
	vsnprintf(&p->ps->psmarg[pos], PS_BUFSLOP, fmt, ap);

	va_end(ap);

	p->ps->psmargcur = strlen(p->ps->psmarg);
}

static void
ps_putchar(struct termp *p, char c)
{
	int		 pos;

	/* See ps_printf(). */

	if ( ! (PS_MARGINS & p->ps->flags)) {
		putchar(c);
		p->ps->pdfbytes++;
		return;
	}

	ps_growbuf(p, 2);

	pos = (int)p->ps->psmargcur++;
	p->ps->psmarg[pos++] = c;
	p->ps->psmarg[pos] = '\0';
}

static void
pdf_obj(struct termp *p, size_t obj)
{

	assert(obj > 0);

	if ((obj - 1) >= p->ps->pdfobjsz) {
		p->ps->pdfobjsz = obj + 128;
		p->ps->pdfobjs = mandoc_reallocarray(p->ps->pdfobjs,
		    p->ps->pdfobjsz, sizeof(size_t));
	}

	p->ps->pdfobjs[(int)obj - 1] = p->ps->pdfbytes;
	ps_printf(p, "%zu 0 obj\n", obj);
}

static void
ps_closepage(struct termp *p)
{
	int		 i;
	size_t		 len, base;

	/*
	 * Close out a page that we've already flushed to output.  In
	 * PostScript, we simply note that the page must be shown.  In
	 * PDF, we must now create the Length, Resource, and Page node
	 * for the page contents.
	 */

	assert(p->ps->psmarg && p->ps->psmarg[0]);
	ps_printf(p, "%s", p->ps->psmarg);

	if (TERMTYPE_PS != p->type) {
		len = p->ps->pdfbytes - p->ps->pdflastpg;
		base = p->ps->pages * 4 + p->ps->pdfbody;

		ps_printf(p, "endstream\nendobj\n");

		/* Length of content. */
		pdf_obj(p, base + 1);
		ps_printf(p, "%zu\nendobj\n", len);

		/* Resource for content. */
		pdf_obj(p, base + 2);
		ps_printf(p, "<<\n/ProcSet [/PDF /Text]\n");
		ps_printf(p, "/Font <<\n");
		for (i = 0; i < (int)TERMFONT__MAX; i++)
			ps_printf(p, "/F%d %d 0 R\n", i, 3 + i);
		ps_printf(p, ">>\n>>\nendobj\n");

		/* Page node. */
		pdf_obj(p, base + 3);
		ps_printf(p, "<<\n");
		ps_printf(p, "/Type /Page\n");
		ps_printf(p, "/Parent 2 0 R\n");
		ps_printf(p, "/Resources %zu 0 R\n", base + 2);
		ps_printf(p, "/Contents %zu 0 R\n", base);
		ps_printf(p, ">>\nendobj\n");
	} else
		ps_printf(p, "showpage\n");

	p->ps->pages++;
	p->ps->psrow = p->ps->top;
	assert( ! (PS_NEWPAGE & p->ps->flags));
	p->ps->flags |= PS_NEWPAGE;
}

static void
ps_end(struct termp *p)
{
	size_t		 i, xref, base;

	ps_plast(p);
	ps_pclose(p);

	/*
	 * At the end of the file, do one last showpage.  This is the
	 * same behaviour as groff(1) and works for multiple pages as
	 * well as just one.
	 */

	if ( ! (PS_NEWPAGE & p->ps->flags)) {
		assert(0 == p->ps->flags);
		assert('\0' == p->ps->last);
		ps_closepage(p);
	}

	if (TERMTYPE_PS == p->type) {
		ps_printf(p, "%%%%Trailer\n");
		ps_printf(p, "%%%%Pages: %zu\n", p->ps->pages);
		ps_printf(p, "%%%%EOF\n");
		return;
	}

	pdf_obj(p, 2);
	ps_printf(p, "<<\n/Type /Pages\n");
	ps_printf(p, "/MediaBox [0 0 %zu %zu]\n",
			(size_t)AFM2PNT(p, p->ps->width),
			(size_t)AFM2PNT(p, p->ps->height));

	ps_printf(p, "/Count %zu\n", p->ps->pages);
	ps_printf(p, "/Kids [");

	for (i = 0; i < p->ps->pages; i++)
		ps_printf(p, " %zu 0 R", i * 4 + p->ps->pdfbody + 3);

	base = (p->ps->pages - 1) * 4 + p->ps->pdfbody + 4;

	ps_printf(p, "]\n>>\nendobj\n");
	pdf_obj(p, base);
	ps_printf(p, "<<\n");
	ps_printf(p, "/Type /Catalog\n");
	ps_printf(p, "/Pages 2 0 R\n");
	ps_printf(p, ">>\nendobj\n");
	xref = p->ps->pdfbytes;
	ps_printf(p, "xref\n");
	ps_printf(p, "0 %zu\n", base + 1);
	ps_printf(p, "0000000000 65535 f \n");

	for (i = 0; i < base; i++)
		ps_printf(p, "%.10zu 00000 n \n",
		    p->ps->pdfobjs[(int)i]);

	ps_printf(p, "trailer\n");
	ps_printf(p, "<<\n");
	ps_printf(p, "/Size %zu\n", base + 1);
	ps_printf(p, "/Root %zu 0 R\n", base);
	ps_printf(p, "/Info 1 0 R\n");
	ps_printf(p, ">>\n");
	ps_printf(p, "startxref\n");
	ps_printf(p, "%zu\n", xref);
	ps_printf(p, "%%%%EOF\n");
}

static void
ps_begin(struct termp *p)
{
	size_t		 width, height;
	int		 i;

	/*
	 * Print margins into margin buffer.  Nothing gets output to the
	 * screen yet, so we don't need to initialise the primary state.
	 */

	if (p->ps->psmarg) {
		assert(p->ps->psmargsz);
		p->ps->psmarg[0] = '\0';
	}

	/*p->ps->pdfbytes = 0;*/
	p->ps->psmargcur = 0;
	p->ps->flags = PS_MARGINS;
	p->ps->pscol = p->ps->left;
	p->ps->psrow = p->ps->header;
	p->ps->lastrow = 0; /* impossible row */

	ps_setfont(p, TERMFONT_NONE);

	(*p->headf)(p, p->argf);
	(*p->endline)(p);

	p->ps->pscol = p->ps->left;
	p->ps->psrow = p->ps->footer;

	(*p->footf)(p, p->argf);
	(*p->endline)(p);

	p->ps->flags &= ~PS_MARGINS;

	assert(0 == p->ps->flags);
	assert(p->ps->psmarg);
	assert('\0' != p->ps->psmarg[0]);

	/*
	 * Print header and initialise page state.  Following this,
	 * stuff gets printed to the screen, so make sure we're sane.
	 */

	if (TERMTYPE_PS == p->type) {
		width = AFM2PNT(p, p->ps->width);
		height = AFM2PNT(p, p->ps->height);

		ps_printf(p, "%%!PS-Adobe-3.0\n");
		ps_printf(p, "%%%%DocumentData: Clean7Bit\n");
		ps_printf(p, "%%%%Orientation: Portrait\n");
		ps_printf(p, "%%%%Pages: (atend)\n");
		ps_printf(p, "%%%%PageOrder: Ascend\n");
		ps_printf(p, "%%%%DocumentMedia: man-%s %zu %zu 0 () ()\n",
		    p->ps->medianame, width, height);
		ps_printf(p, "%%%%DocumentNeededResources: font");

		for (i = 0; i < (int)TERMFONT__MAX; i++)
			ps_printf(p, " %s", fonts[i].name);

		ps_printf(p, "\n%%%%DocumentSuppliedResources: "
		    "procset MandocProcs 1.0 0\n");
		ps_printf(p, "%%%%EndComments\n");
		ps_printf(p, "%%%%BeginProlog\n");
		ps_printf(p, "%%%%BeginResource: procset MandocProcs "
		    "10170 10170\n");
		/* The font size is effectively hard-coded for now. */
		ps_printf(p, "/fs %zu def\n", p->ps->scale);
		for (i = 0; i < (int)TERMFONT__MAX; i++)
			ps_printf(p, "/f%d { /%s fs selectfont } def\n",
			    i, fonts[i].name);
		ps_printf(p, "/s { 3 1 roll moveto show } bind def\n");
		ps_printf(p, "/c { exch currentpoint exch pop "
		    "moveto show } bind def\n");
		ps_printf(p, "%%%%EndResource\n");
		ps_printf(p, "%%%%EndProlog\n");
		ps_printf(p, "%%%%BeginSetup\n");
		ps_printf(p, "%%%%BeginFeature: *PageSize %s\n",
		    p->ps->medianame);
		ps_printf(p, "<</PageSize [%zu %zu]>>setpagedevice\n",
		    width, height);
		ps_printf(p, "%%%%EndFeature\n");
		ps_printf(p, "%%%%EndSetup\n");
	} else {
		ps_printf(p, "%%PDF-1.1\n");
		pdf_obj(p, 1);
		ps_printf(p, "<<\n");
		ps_printf(p, ">>\n");
		ps_printf(p, "endobj\n");

		for (i = 0; i < (int)TERMFONT__MAX; i++) {
			pdf_obj(p, (size_t)i + 3);
			ps_printf(p, "<<\n");
			ps_printf(p, "/Type /Font\n");
			ps_printf(p, "/Subtype /Type1\n");
			ps_printf(p, "/Name /F%d\n", i);
			ps_printf(p, "/BaseFont /%s\n", fonts[i].name);
			ps_printf(p, ">>\nendobj\n");
		}
	}

	p->ps->pdfbody = (size_t)TERMFONT__MAX + 3;
	p->ps->pscol = p->ps->left;
	p->ps->psrow = p->ps->top;
	p->ps->flags |= PS_NEWPAGE;
	ps_setfont(p, TERMFONT_NONE);
}

static void
ps_pletter(struct termp *p, int c)
{
	int		 f;

	/*
	 * If we haven't opened a page context, then output that we're
	 * in a new page and make sure the font is correctly set.
	 */

	if (PS_NEWPAGE & p->ps->flags) {
		if (TERMTYPE_PS == p->type) {
			ps_printf(p, "%%%%Page: %zu %zu\n",
			    p->ps->pages + 1, p->ps->pages + 1);
			ps_printf(p, "f%d\n", (int)p->ps->lastf);
		} else {
			pdf_obj(p, p->ps->pdfbody +
			    p->ps->pages * 4);
			ps_printf(p, "<<\n");
			ps_printf(p, "/Length %zu 0 R\n",
			    p->ps->pdfbody + 1 + p->ps->pages * 4);
			ps_printf(p, ">>\nstream\n");
		}
		p->ps->pdflastpg = p->ps->pdfbytes;
		p->ps->flags &= ~PS_NEWPAGE;
	}

	/*
	 * If we're not in a PostScript "word" context, then open one
	 * now at the current cursor.
	 */

	if ( ! (PS_INLINE & p->ps->flags)) {
		if (TERMTYPE_PS != p->type) {
			ps_printf(p, "BT\n/F%d %zu Tf\n",
			    (int)p->ps->lastf, p->ps->scale);
			ps_printf(p, "%.3f %.3f Td\n(",
			    AFM2PNT(p, p->ps->pscol),
			    AFM2PNT(p, p->ps->psrow));
		} else {
			ps_printf(p, "%.3f", AFM2PNT(p, p->ps->pscol));
			if (p->ps->psrow != p->ps->lastrow)
				ps_printf(p, " %.3f",
				    AFM2PNT(p, p->ps->psrow));
			ps_printf(p, "(");
		}
		p->ps->flags |= PS_INLINE;
	}

	assert( ! (PS_NEWPAGE & p->ps->flags));

	/*
	 * We need to escape these characters as per the PostScript
	 * specification.  We would also escape non-graphable characters
	 * (like tabs), but none of them would get to this point and
	 * it's superfluous to abort() on them.
	 */

	switch (c) {
	case '(':
	case ')':
	case '\\':
		ps_putchar(p, '\\');
		break;
	default:
		break;
	}

	/* Write the character and adjust where we are on the page. */

	f = (int)p->ps->lastf;

	if (c <= 32 || c - 32 >= MAXCHAR)
		c = 32;

	ps_putchar(p, (char)c);
	c -= 32;
	p->ps->pscol += (size_t)fonts[f].gly[c].wx;
}

static void
ps_pclose(struct termp *p)
{

	/*
	 * Spit out that we're exiting a word context (this is a
	 * "partial close" because we don't check the last-char buffer
	 * or anything).
	 */

	if ( ! (PS_INLINE & p->ps->flags))
		return;

	if (TERMTYPE_PS != p->type)
		ps_printf(p, ") Tj\nET\n");
	else if (p->ps->psrow == p->ps->lastrow)
		ps_printf(p, ")c\n");
	else {
		ps_printf(p, ")s\n");
		p->ps->lastrow = p->ps->psrow;
	}

	p->ps->flags &= ~PS_INLINE;
}

/* If we have a `last' char that wasn't printed yet, print it now. */
static void
ps_plast(struct termp *p)
{
	size_t	 wx;

	if (p->ps->last == '\0')
		return;

	/* Check the font mode; open a new scope if it doesn't match. */

	if (p->ps->nextf != p->ps->lastf) {
		ps_pclose(p);
		ps_setfont(p, p->ps->nextf);
	}
	p->ps->nextf = TERMFONT_NONE;

	/*
	 * For an overstrike, if a previous character
	 * was wider, advance to center the new one.
	 */

	if (p->ps->pscolnext) {
		wx = fonts[p->ps->lastf].gly[(int)p->ps->last-32].wx;
		if (p->ps->pscol + wx < p->ps->pscolnext)
			p->ps->pscol = (p->ps->pscol +
			    p->ps->pscolnext - wx) / 2;
	}

	ps_pletter(p, p->ps->last);
	p->ps->last = '\0';

	/*
	 * For an overstrike, if a previous character
	 * was wider, advance to the end of the old one.
	 */

	if (p->ps->pscol < p->ps->pscolnext) {
		ps_pclose(p);
		p->ps->pscol = p->ps->pscolnext;
	}
}

static void
ps_letter(struct termp *p, int arg)
{
	size_t		savecol;
	char		c;

	c = arg >= 128 || arg <= 0 ? '?' : arg;

	/*
	 * When receiving a backspace, merely flag it.
	 * We don't know yet whether it is
	 * a font instruction or an overstrike.
	 */

	if (c == '\b') {
		assert(p->ps->last != '\0');
		assert( ! (p->ps->flags & PS_BACKSP));
		p->ps->flags |= PS_BACKSP;
		return;
	}

	/*
	 * Decode font instructions.
	 */

	if (p->ps->flags & PS_BACKSP) {
		if (p->ps->last == '_') {
			switch (p->ps->nextf) {
			case TERMFONT_BI:
				break;
			case TERMFONT_BOLD:
				p->ps->nextf = TERMFONT_BI;
				break;
			default:
				p->ps->nextf = TERMFONT_UNDER;
			}
			p->ps->last = c;
			p->ps->flags &= ~PS_BACKSP;
			return;
		}
		if (p->ps->last == c) {
			switch (p->ps->nextf) {
			case TERMFONT_BI:
				break;
			case TERMFONT_UNDER:
				p->ps->nextf = TERMFONT_BI;
				break;
			default:
				p->ps->nextf = TERMFONT_BOLD;
			}
			p->ps->flags &= ~PS_BACKSP;
			return;
		}

		/*
		 * This is not a font instruction, but rather
		 * the next character.  Prepare for overstrike.
		 */

		savecol = p->ps->pscol;
	} else
		savecol = SIZE_MAX;

	/*
	 * We found the next character, so the font instructions
	 * for the previous one are complete.
	 * Use them and print it.
	 */

	ps_plast(p);

	/*
	 * Do not print the current character yet because font
	 * instructions might follow; only remember the character.
	 * It will get printed later from ps_plast().
	 */

	p->ps->last = c;

	/*
	 * For an overstrike, back up to the previous position.
	 * If the previous character is wider than any it overstrikes,
	 * remember the current position, because it might also be
	 * wider than all that will overstrike it.
	 */

	if (savecol != SIZE_MAX) {
		if (p->ps->pscolnext < p->ps->pscol)
			p->ps->pscolnext = p->ps->pscol;
		ps_pclose(p);
		p->ps->pscol = savecol;
		p->ps->flags &= ~PS_BACKSP;
	} else
		p->ps->pscolnext = 0;
}

static void
ps_advance(struct termp *p, size_t len)
{

	/*
	 * Advance some spaces.  This can probably be made smarter,
	 * i.e., to have multiple space-separated words in the same
	 * scope, but this is easier:  just close out the current scope
	 * and readjust our column settings.
	 */

	ps_plast(p);
	ps_pclose(p);
	p->ps->pscol += len;
}

static void
ps_endline(struct termp *p)
{

	/* Close out any scopes we have open: we're at eoln. */

	ps_plast(p);
	ps_pclose(p);

	/*
	 * If we're in the margin, don't try to recalculate our current
	 * row.  XXX: if the column tries to be fancy with multiple
	 * lines, we'll do nasty stuff.
	 */

	if (PS_MARGINS & p->ps->flags)
		return;

	/* Left-justify. */

	p->ps->pscol = p->ps->left;

	/* If we haven't printed anything, return. */

	if (PS_NEWPAGE & p->ps->flags)
		return;

	/*
	 * Put us down a line.  If we're at the page bottom, spit out a
	 * showpage and restart our row.
	 */

	if (p->ps->psrow >= p->ps->lineheight + p->ps->bottom) {
		p->ps->psrow -= p->ps->lineheight;
		return;
	}

	ps_closepage(p);

	p->tcol->offset -= p->ti;
	p->ti = 0;
}

static void
ps_setfont(struct termp *p, enum termfont f)
{

	assert(f < TERMFONT__MAX);
	p->ps->lastf = f;

	/*
	 * If we're still at the top of the page, let the font-setting
	 * be delayed until we actually have stuff to print.
	 */

	if (PS_NEWPAGE & p->ps->flags)
		return;

	if (TERMTYPE_PS == p->type)
		ps_printf(p, "f%d\n", (int)f);
	else
		ps_printf(p, "/F%d %zu Tf\n",
		    (int)f, p->ps->scale);
}

static size_t
ps_width(const struct termp *p, int c)
{

	if (c <= 32 || c - 32 >= MAXCHAR)
		c = 0;
	else
		c -= 32;

	return (size_t)fonts[(int)TERMFONT_NONE].gly[c].wx;
}

static int
ps_hspan(const struct termp *p, const struct roffsu *su)
{
	double		 r;

	/*
	 * All of these measurements are derived by converting from the
	 * native measurement to AFM units.
	 */
	switch (su->unit) {
	case SCALE_BU:
		/*
		 * Traditionally, the default unit is fixed to the
		 * output media.  So this would refer to the point.  In
		 * mandoc(1), however, we stick to the default terminal
		 * scaling unit so that output is the same regardless
		 * the media.
		 */
		r = PNT2AFM(p, su->scale * 72.0 / 240.0);
		break;
	case SCALE_CM:
		r = PNT2AFM(p, su->scale * 72.0 / 2.54);
		break;
	case SCALE_EM:
		r = su->scale *
		    fonts[(int)TERMFONT_NONE].gly[109 - 32].wx;
		break;
	case SCALE_EN:
		r = su->scale *
		    fonts[(int)TERMFONT_NONE].gly[110 - 32].wx;
		break;
	case SCALE_IN:
		r = PNT2AFM(p, su->scale * 72.0);
		break;
	case SCALE_MM:
		r = su->scale *
		    fonts[(int)TERMFONT_NONE].gly[109 - 32].wx / 100.0;
		break;
	case SCALE_PC:
		r = PNT2AFM(p, su->scale * 12.0);
		break;
	case SCALE_PT:
		r = PNT2AFM(p, su->scale * 1.0);
		break;
	case SCALE_VS:
		r = su->scale * p->ps->lineheight;
		break;
	default:
		r = su->scale;
		break;
	}

	return r * 24.0;
}

static void
ps_growbuf(struct termp *p, size_t sz)
{
	if (p->ps->psmargcur + sz <= p->ps->psmargsz)
		return;

	if (sz < PS_BUFSLOP)
		sz = PS_BUFSLOP;

	p->ps->psmargsz += sz;
	p->ps->psmarg = mandoc_realloc(p->ps->psmarg, p->ps->psmargsz);
}
