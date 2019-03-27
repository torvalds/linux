/*	$Id: term.c,v 1.274 2017/07/28 14:25:48 schwarze Exp $ */
/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2010-2017 Ingo Schwarze <schwarze@openbsd.org>
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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "mandoc_aux.h"
#include "out.h"
#include "term.h"
#include "main.h"

static	size_t		 cond_width(const struct termp *, int, int *);
static	void		 adjbuf(struct termp_col *, size_t);
static	void		 bufferc(struct termp *, char);
static	void		 encode(struct termp *, const char *, size_t);
static	void		 encode1(struct termp *, int);
static	void		 endline(struct termp *);


void
term_setcol(struct termp *p, size_t maxtcol)
{
	if (maxtcol > p->maxtcol) {
		p->tcols = mandoc_recallocarray(p->tcols,
		    p->maxtcol, maxtcol, sizeof(*p->tcols));
		p->maxtcol = maxtcol;
	}
	p->lasttcol = maxtcol - 1;
	p->tcol = p->tcols;
}

void
term_free(struct termp *p)
{
	for (p->tcol = p->tcols; p->tcol < p->tcols + p->maxtcol; p->tcol++)
		free(p->tcol->buf);
	free(p->tcols);
	free(p->fontq);
	free(p);
}

void
term_begin(struct termp *p, term_margin head,
		term_margin foot, const struct roff_meta *arg)
{

	p->headf = head;
	p->footf = foot;
	p->argf = arg;
	(*p->begin)(p);
}

void
term_end(struct termp *p)
{

	(*p->end)(p);
}

/*
 * Flush a chunk of text.  By default, break the output line each time
 * the right margin is reached, and continue output on the next line
 * at the same offset as the chunk itself.  By default, also break the
 * output line at the end of the chunk.
 * The following flags may be specified:
 *
 *  - TERMP_NOBREAK: Do not break the output line at the right margin,
 *    but only at the max right margin.  Also, do not break the output
 *    line at the end of the chunk, such that the next call can pad to
 *    the next column.  However, if less than p->trailspace blanks,
 *    which can be 0, 1, or 2, remain to the right margin, the line
 *    will be broken.
 *  - TERMP_BRTRSP: Consider trailing whitespace significant
 *    when deciding whether the chunk fits or not.
 *  - TERMP_BRIND: If the chunk does not fit and the output line has
 *    to be broken, start the next line at the right margin instead
 *    of at the offset.  Used together with TERMP_NOBREAK for the tags
 *    in various kinds of tagged lists.
 *  - TERMP_HANG: Do not break the output line at the right margin,
 *    append the next chunk after it even if this one is too long.
 *    To be used together with TERMP_NOBREAK.
 *  - TERMP_NOPAD: Start writing at the current position,
 *    do not pad with blank characters up to the offset.
 */
void
term_flushln(struct termp *p)
{
	size_t		 vis;   /* current visual position on output */
	size_t		 vbl;   /* number of blanks to prepend to output */
	size_t		 vend;	/* end of word visual position on output */
	size_t		 bp;    /* visual right border position */
	size_t		 dv;    /* temporary for visual pos calculations */
	size_t		 j;     /* temporary loop index for p->tcol->buf */
	size_t		 jhy;	/* last hyph before overflow w/r/t j */
	size_t		 maxvis; /* output position of visible boundary */
	int		 ntab;	/* number of tabs to prepend */
	int		 breakline; /* after this word */

	vbl = (p->flags & TERMP_NOPAD) || p->tcol->offset < p->viscol ?
	    0 : p->tcol->offset - p->viscol;
	if (p->minbl && vbl < p->minbl)
		vbl = p->minbl;
	maxvis = p->tcol->rmargin > p->viscol + vbl ?
	    p->tcol->rmargin - p->viscol - vbl : 0;
	bp = !(p->flags & TERMP_NOBREAK) ? maxvis :
	    p->maxrmargin > p->viscol + vbl ?
	    p->maxrmargin - p->viscol - vbl : 0;
	vis = vend = 0;

	if ((p->flags & TERMP_MULTICOL) == 0)
		p->tcol->col = 0;
	while (p->tcol->col < p->tcol->lastcol) {

		/*
		 * Handle literal tab characters: collapse all
		 * subsequent tabs into a single huge set of spaces.
		 */

		ntab = 0;
		while (p->tcol->col < p->tcol->lastcol &&
		    p->tcol->buf[p->tcol->col] == '\t') {
			vend = term_tab_next(vis);
			vbl += vend - vis;
			vis = vend;
			ntab++;
			p->tcol->col++;
		}

		/*
		 * Count up visible word characters.  Control sequences
		 * (starting with the CSI) aren't counted.  A space
		 * generates a non-printing word, which is valid (the
		 * space is printed according to regular spacing rules).
		 */

		jhy = 0;
		breakline = 0;
		for (j = p->tcol->col; j < p->tcol->lastcol; j++) {
			if (p->tcol->buf[j] == '\n') {
				if ((p->flags & TERMP_BRIND) == 0)
					breakline = 1;
				continue;
			}
			if (p->tcol->buf[j] == ' ' || p->tcol->buf[j] == '\t')
				break;

			/* Back over the last printed character. */
			if (p->tcol->buf[j] == '\b') {
				assert(j);
				vend -= (*p->width)(p, p->tcol->buf[j - 1]);
				continue;
			}

			/* Regular word. */
			/* Break at the hyphen point if we overrun. */
			if (vend > vis && vend < bp &&
			    (p->tcol->buf[j] == ASCII_HYPH||
			     p->tcol->buf[j] == ASCII_BREAK))
				jhy = j;

			/*
			 * Hyphenation now decided, put back a real
			 * hyphen such that we get the correct width.
			 */
			if (p->tcol->buf[j] == ASCII_HYPH)
				p->tcol->buf[j] = '-';

			vend += (*p->width)(p, p->tcol->buf[j]);
		}

		/*
		 * Find out whether we would exceed the right margin.
		 * If so, break to the next line.
		 */

		if (vend > bp && jhy == 0 && vis > 0 &&
		    (p->flags & TERMP_BRNEVER) == 0) {
			if (p->flags & TERMP_MULTICOL)
				return;

			endline(p);
			vend -= vis;

			/* Use pending tabs on the new line. */

			vbl = 0;
			while (ntab--)
				vbl = term_tab_next(vbl);

			/* Re-establish indentation. */

			if (p->flags & TERMP_BRIND)
				vbl += p->tcol->rmargin;
			else
				vbl += p->tcol->offset;
			maxvis = p->tcol->rmargin > vbl ?
			    p->tcol->rmargin - vbl : 0;
			bp = !(p->flags & TERMP_NOBREAK) ? maxvis :
			    p->maxrmargin > vbl ?  p->maxrmargin - vbl : 0;
		}

		/*
		 * Write out the rest of the word.
		 */

		for ( ; p->tcol->col < p->tcol->lastcol; p->tcol->col++) {
			if (vend > bp && jhy > 0 && p->tcol->col > jhy)
				break;
			if (p->tcol->buf[p->tcol->col] == '\n')
				continue;
			if (p->tcol->buf[p->tcol->col] == '\t')
				break;
			if (p->tcol->buf[p->tcol->col] == ' ') {
				j = p->tcol->col;
				while (p->tcol->col < p->tcol->lastcol &&
				    p->tcol->buf[p->tcol->col] == ' ')
					p->tcol->col++;
				dv = (p->tcol->col - j) * (*p->width)(p, ' ');
				vbl += dv;
				vend += dv;
				break;
			}
			if (p->tcol->buf[p->tcol->col] == ASCII_NBRSP) {
				vbl += (*p->width)(p, ' ');
				continue;
			}
			if (p->tcol->buf[p->tcol->col] == ASCII_BREAK)
				continue;

			/*
			 * Now we definitely know there will be
			 * printable characters to output,
			 * so write preceding white space now.
			 */
			if (vbl) {
				(*p->advance)(p, vbl);
				p->viscol += vbl;
				vbl = 0;
			}

			(*p->letter)(p, p->tcol->buf[p->tcol->col]);
			if (p->tcol->buf[p->tcol->col] == '\b')
				p->viscol -= (*p->width)(p,
				    p->tcol->buf[p->tcol->col - 1]);
			else
				p->viscol += (*p->width)(p,
				    p->tcol->buf[p->tcol->col]);
		}
		vis = vend;

		if (breakline == 0)
			continue;

		/* Explicitly requested output line break. */

		if (p->flags & TERMP_MULTICOL)
			return;

		endline(p);
		breakline = 0;
		vis = vend = 0;

		/* Re-establish indentation. */

		vbl = p->tcol->offset;
		maxvis = p->tcol->rmargin > vbl ?
		    p->tcol->rmargin - vbl : 0;
		bp = !(p->flags & TERMP_NOBREAK) ? maxvis :
		    p->maxrmargin > vbl ?  p->maxrmargin - vbl : 0;
	}

	/*
	 * If there was trailing white space, it was not printed;
	 * so reset the cursor position accordingly.
	 */

	if (vis > vbl)
		vis -= vbl;
	else
		vis = 0;

	p->col = p->tcol->col = p->tcol->lastcol = 0;
	p->minbl = p->trailspace;
	p->flags &= ~(TERMP_BACKAFTER | TERMP_BACKBEFORE | TERMP_NOPAD);

	if (p->flags & TERMP_MULTICOL)
		return;

	/* Trailing whitespace is significant in some columns. */

	if (vis && vbl && (TERMP_BRTRSP & p->flags))
		vis += vbl;

	/* If the column was overrun, break the line. */
	if ((p->flags & TERMP_NOBREAK) == 0 ||
	    ((p->flags & TERMP_HANG) == 0 &&
	     vis + p->trailspace * (*p->width)(p, ' ') > maxvis))
		endline(p);
}

static void
endline(struct termp *p)
{
	if ((p->flags & (TERMP_NEWMC | TERMP_ENDMC)) == TERMP_ENDMC) {
		p->mc = NULL;
		p->flags &= ~TERMP_ENDMC;
	}
	if (p->mc != NULL) {
		if (p->viscol && p->maxrmargin >= p->viscol)
			(*p->advance)(p, p->maxrmargin - p->viscol + 1);
		p->flags |= TERMP_NOBUF | TERMP_NOSPACE;
		term_word(p, p->mc);
		p->flags &= ~(TERMP_NOBUF | TERMP_NEWMC);
	}
	p->viscol = 0;
	p->minbl = 0;
	(*p->endline)(p);
}

/*
 * A newline only breaks an existing line; it won't assert vertical
 * space.  All data in the output buffer is flushed prior to the newline
 * assertion.
 */
void
term_newln(struct termp *p)
{

	p->flags |= TERMP_NOSPACE;
	if (p->tcol->lastcol || p->viscol)
		term_flushln(p);
}

/*
 * Asserts a vertical space (a full, empty line-break between lines).
 * Note that if used twice, this will cause two blank spaces and so on.
 * All data in the output buffer is flushed prior to the newline
 * assertion.
 */
void
term_vspace(struct termp *p)
{

	term_newln(p);
	p->viscol = 0;
	p->minbl = 0;
	if (0 < p->skipvsp)
		p->skipvsp--;
	else
		(*p->endline)(p);
}

/* Swap current and previous font; for \fP and .ft P */
void
term_fontlast(struct termp *p)
{
	enum termfont	 f;

	f = p->fontl;
	p->fontl = p->fontq[p->fonti];
	p->fontq[p->fonti] = f;
}

/* Set font, save current, discard previous; for \f, .ft, .B etc. */
void
term_fontrepl(struct termp *p, enum termfont f)
{

	p->fontl = p->fontq[p->fonti];
	p->fontq[p->fonti] = f;
}

/* Set font, save previous. */
void
term_fontpush(struct termp *p, enum termfont f)
{

	p->fontl = p->fontq[p->fonti];
	if (++p->fonti == p->fontsz) {
		p->fontsz += 8;
		p->fontq = mandoc_reallocarray(p->fontq,
		    p->fontsz, sizeof(*p->fontq));
	}
	p->fontq[p->fonti] = f;
}

/* Flush to make the saved pointer current again. */
void
term_fontpopq(struct termp *p, int i)
{

	assert(i >= 0);
	if (p->fonti > i)
		p->fonti = i;
}

/* Pop one font off the stack. */
void
term_fontpop(struct termp *p)
{

	assert(p->fonti);
	p->fonti--;
}

/*
 * Handle pwords, partial words, which may be either a single word or a
 * phrase that cannot be broken down (such as a literal string).  This
 * handles word styling.
 */
void
term_word(struct termp *p, const char *word)
{
	struct roffsu	 su;
	const char	 nbrsp[2] = { ASCII_NBRSP, 0 };
	const char	*seq, *cp;
	int		 sz, uc;
	size_t		 csz, lsz, ssz;
	enum mandoc_esc	 esc;

	if ((p->flags & TERMP_NOBUF) == 0) {
		if ((p->flags & TERMP_NOSPACE) == 0) {
			if ((p->flags & TERMP_KEEP) == 0) {
				bufferc(p, ' ');
				if (p->flags & TERMP_SENTENCE)
					bufferc(p, ' ');
			} else
				bufferc(p, ASCII_NBRSP);
		}
		if (p->flags & TERMP_PREKEEP)
			p->flags |= TERMP_KEEP;
		if (p->flags & TERMP_NONOSPACE)
			p->flags |= TERMP_NOSPACE;
		else
			p->flags &= ~TERMP_NOSPACE;
		p->flags &= ~(TERMP_SENTENCE | TERMP_NONEWLINE);
		p->skipvsp = 0;
	}

	while ('\0' != *word) {
		if ('\\' != *word) {
			if (TERMP_NBRWORD & p->flags) {
				if (' ' == *word) {
					encode(p, nbrsp, 1);
					word++;
					continue;
				}
				ssz = strcspn(word, "\\ ");
			} else
				ssz = strcspn(word, "\\");
			encode(p, word, ssz);
			word += (int)ssz;
			continue;
		}

		word++;
		esc = mandoc_escape(&word, &seq, &sz);
		if (ESCAPE_ERROR == esc)
			continue;

		switch (esc) {
		case ESCAPE_UNICODE:
			uc = mchars_num2uc(seq + 1, sz - 1);
			break;
		case ESCAPE_NUMBERED:
			uc = mchars_num2char(seq, sz);
			if (uc < 0)
				continue;
			break;
		case ESCAPE_SPECIAL:
			if (p->enc == TERMENC_ASCII) {
				cp = mchars_spec2str(seq, sz, &ssz);
				if (cp != NULL)
					encode(p, cp, ssz);
			} else {
				uc = mchars_spec2cp(seq, sz);
				if (uc > 0)
					encode1(p, uc);
			}
			continue;
		case ESCAPE_FONTBOLD:
			term_fontrepl(p, TERMFONT_BOLD);
			continue;
		case ESCAPE_FONTITALIC:
			term_fontrepl(p, TERMFONT_UNDER);
			continue;
		case ESCAPE_FONTBI:
			term_fontrepl(p, TERMFONT_BI);
			continue;
		case ESCAPE_FONT:
		case ESCAPE_FONTROMAN:
			term_fontrepl(p, TERMFONT_NONE);
			continue;
		case ESCAPE_FONTPREV:
			term_fontlast(p);
			continue;
		case ESCAPE_BREAK:
			bufferc(p, '\n');
			continue;
		case ESCAPE_NOSPACE:
			if (p->flags & TERMP_BACKAFTER)
				p->flags &= ~TERMP_BACKAFTER;
			else if (*word == '\0')
				p->flags |= (TERMP_NOSPACE | TERMP_NONEWLINE);
			continue;
		case ESCAPE_HORIZ:
			if (*seq == '|') {
				seq++;
				uc = -p->col;
			} else
				uc = 0;
			if (a2roffsu(seq, &su, SCALE_EM) == NULL)
				continue;
			uc += term_hen(p, &su);
			if (uc > 0)
				while (uc-- > 0)
					bufferc(p, ASCII_NBRSP);
			else if (p->col > (size_t)(-uc))
				p->col += uc;
			else {
				uc += p->col;
				p->col = 0;
				if (p->tcol->offset > (size_t)(-uc)) {
					p->ti += uc;
					p->tcol->offset += uc;
				} else {
					p->ti -= p->tcol->offset;
					p->tcol->offset = 0;
				}
			}
			continue;
		case ESCAPE_HLINE:
			if ((cp = a2roffsu(seq, &su, SCALE_EM)) == NULL)
				continue;
			uc = term_hen(p, &su);
			if (uc <= 0) {
				if (p->tcol->rmargin <= p->tcol->offset)
					continue;
				lsz = p->tcol->rmargin - p->tcol->offset;
			} else
				lsz = uc;
			if (*cp == seq[-1])
				uc = -1;
			else if (*cp == '\\') {
				seq = cp + 1;
				esc = mandoc_escape(&seq, &cp, &sz);
				switch (esc) {
				case ESCAPE_UNICODE:
					uc = mchars_num2uc(cp + 1, sz - 1);
					break;
				case ESCAPE_NUMBERED:
					uc = mchars_num2char(cp, sz);
					break;
				case ESCAPE_SPECIAL:
					uc = mchars_spec2cp(cp, sz);
					break;
				default:
					uc = -1;
					break;
				}
			} else
				uc = *cp;
			if (uc < 0x20 || (uc > 0x7E && uc < 0xA0))
				uc = '_';
			if (p->enc == TERMENC_ASCII) {
				cp = ascii_uc2str(uc);
				csz = term_strlen(p, cp);
				ssz = strlen(cp);
			} else
				csz = (*p->width)(p, uc);
			while (lsz >= csz) {
				if (p->enc == TERMENC_ASCII)
					encode(p, cp, ssz);
				else
					encode1(p, uc);
				lsz -= csz;
			}
			continue;
		case ESCAPE_SKIPCHAR:
			p->flags |= TERMP_BACKAFTER;
			continue;
		case ESCAPE_OVERSTRIKE:
			cp = seq + sz;
			while (seq < cp) {
				if (*seq == '\\') {
					mandoc_escape(&seq, NULL, NULL);
					continue;
				}
				encode1(p, *seq++);
				if (seq < cp) {
					if (p->flags & TERMP_BACKBEFORE)
						p->flags |= TERMP_BACKAFTER;
					else
						p->flags |= TERMP_BACKBEFORE;
				}
			}
			/* Trim trailing backspace/blank pair. */
			if (p->tcol->lastcol > 2 &&
			    (p->tcol->buf[p->tcol->lastcol - 1] == ' ' ||
			     p->tcol->buf[p->tcol->lastcol - 1] == '\t'))
				p->tcol->lastcol -= 2;
			if (p->col > p->tcol->lastcol)
				p->col = p->tcol->lastcol;
			continue;
		default:
			continue;
		}

		/*
		 * Common handling for Unicode and numbered
		 * character escape sequences.
		 */

		if (p->enc == TERMENC_ASCII) {
			cp = ascii_uc2str(uc);
			encode(p, cp, strlen(cp));
		} else {
			if ((uc < 0x20 && uc != 0x09) ||
			    (uc > 0x7E && uc < 0xA0))
				uc = 0xFFFD;
			encode1(p, uc);
		}
	}
	p->flags &= ~TERMP_NBRWORD;
}

static void
adjbuf(struct termp_col *c, size_t sz)
{
	if (c->maxcols == 0)
		c->maxcols = 1024;
	while (c->maxcols <= sz)
		c->maxcols <<= 2;
	c->buf = mandoc_reallocarray(c->buf, c->maxcols, sizeof(*c->buf));
}

static void
bufferc(struct termp *p, char c)
{
	if (p->flags & TERMP_NOBUF) {
		(*p->letter)(p, c);
		return;
	}
	if (p->col + 1 >= p->tcol->maxcols)
		adjbuf(p->tcol, p->col + 1);
	if (p->tcol->lastcol <= p->col || (c != ' ' && c != ASCII_NBRSP))
		p->tcol->buf[p->col] = c;
	if (p->tcol->lastcol < ++p->col)
		p->tcol->lastcol = p->col;
}

/*
 * See encode().
 * Do this for a single (probably unicode) value.
 * Does not check for non-decorated glyphs.
 */
static void
encode1(struct termp *p, int c)
{
	enum termfont	  f;

	if (p->flags & TERMP_NOBUF) {
		(*p->letter)(p, c);
		return;
	}

	if (p->col + 7 >= p->tcol->maxcols)
		adjbuf(p->tcol, p->col + 7);

	f = (c == ASCII_HYPH || c > 127 || isgraph(c)) ?
	    p->fontq[p->fonti] : TERMFONT_NONE;

	if (p->flags & TERMP_BACKBEFORE) {
		if (p->tcol->buf[p->col - 1] == ' ' ||
		    p->tcol->buf[p->col - 1] == '\t')
			p->col--;
		else
			p->tcol->buf[p->col++] = '\b';
		p->flags &= ~TERMP_BACKBEFORE;
	}
	if (f == TERMFONT_UNDER || f == TERMFONT_BI) {
		p->tcol->buf[p->col++] = '_';
		p->tcol->buf[p->col++] = '\b';
	}
	if (f == TERMFONT_BOLD || f == TERMFONT_BI) {
		if (c == ASCII_HYPH)
			p->tcol->buf[p->col++] = '-';
		else
			p->tcol->buf[p->col++] = c;
		p->tcol->buf[p->col++] = '\b';
	}
	if (p->tcol->lastcol <= p->col || (c != ' ' && c != ASCII_NBRSP))
		p->tcol->buf[p->col] = c;
	if (p->tcol->lastcol < ++p->col)
		p->tcol->lastcol = p->col;
	if (p->flags & TERMP_BACKAFTER) {
		p->flags |= TERMP_BACKBEFORE;
		p->flags &= ~TERMP_BACKAFTER;
	}
}

static void
encode(struct termp *p, const char *word, size_t sz)
{
	size_t		  i;

	if (p->flags & TERMP_NOBUF) {
		for (i = 0; i < sz; i++)
			(*p->letter)(p, word[i]);
		return;
	}

	if (p->col + 2 + (sz * 5) >= p->tcol->maxcols)
		adjbuf(p->tcol, p->col + 2 + (sz * 5));

	for (i = 0; i < sz; i++) {
		if (ASCII_HYPH == word[i] ||
		    isgraph((unsigned char)word[i]))
			encode1(p, word[i]);
		else {
			if (p->tcol->lastcol <= p->col ||
			    (word[i] != ' ' && word[i] != ASCII_NBRSP))
				p->tcol->buf[p->col] = word[i];
			p->col++;

			/*
			 * Postpone the effect of \z while handling
			 * an overstrike sequence from ascii_uc2str().
			 */

			if (word[i] == '\b' &&
			    (p->flags & TERMP_BACKBEFORE)) {
				p->flags &= ~TERMP_BACKBEFORE;
				p->flags |= TERMP_BACKAFTER;
			}
		}
	}
	if (p->tcol->lastcol < p->col)
		p->tcol->lastcol = p->col;
}

void
term_setwidth(struct termp *p, const char *wstr)
{
	struct roffsu	 su;
	int		 iop, width;

	iop = 0;
	width = 0;
	if (NULL != wstr) {
		switch (*wstr) {
		case '+':
			iop = 1;
			wstr++;
			break;
		case '-':
			iop = -1;
			wstr++;
			break;
		default:
			break;
		}
		if (a2roffsu(wstr, &su, SCALE_MAX) != NULL)
			width = term_hspan(p, &su);
		else
			iop = 0;
	}
	(*p->setwidth)(p, iop, width);
}

size_t
term_len(const struct termp *p, size_t sz)
{

	return (*p->width)(p, ' ') * sz;
}

static size_t
cond_width(const struct termp *p, int c, int *skip)
{

	if (*skip) {
		(*skip) = 0;
		return 0;
	} else
		return (*p->width)(p, c);
}

size_t
term_strlen(const struct termp *p, const char *cp)
{
	size_t		 sz, rsz, i;
	int		 ssz, skip, uc;
	const char	*seq, *rhs;
	enum mandoc_esc	 esc;
	static const char rej[] = { '\\', ASCII_NBRSP, ASCII_HYPH,
			ASCII_BREAK, '\0' };

	/*
	 * Account for escaped sequences within string length
	 * calculations.  This follows the logic in term_word() as we
	 * must calculate the width of produced strings.
	 */

	sz = 0;
	skip = 0;
	while ('\0' != *cp) {
		rsz = strcspn(cp, rej);
		for (i = 0; i < rsz; i++)
			sz += cond_width(p, *cp++, &skip);

		switch (*cp) {
		case '\\':
			cp++;
			esc = mandoc_escape(&cp, &seq, &ssz);
			if (ESCAPE_ERROR == esc)
				continue;

			rhs = NULL;

			switch (esc) {
			case ESCAPE_UNICODE:
				uc = mchars_num2uc(seq + 1, ssz - 1);
				break;
			case ESCAPE_NUMBERED:
				uc = mchars_num2char(seq, ssz);
				if (uc < 0)
					continue;
				break;
			case ESCAPE_SPECIAL:
				if (p->enc == TERMENC_ASCII) {
					rhs = mchars_spec2str(seq, ssz, &rsz);
					if (rhs != NULL)
						break;
				} else {
					uc = mchars_spec2cp(seq, ssz);
					if (uc > 0)
						sz += cond_width(p, uc, &skip);
				}
				continue;
			case ESCAPE_SKIPCHAR:
				skip = 1;
				continue;
			case ESCAPE_OVERSTRIKE:
				rsz = 0;
				rhs = seq + ssz;
				while (seq < rhs) {
					if (*seq == '\\') {
						mandoc_escape(&seq, NULL, NULL);
						continue;
					}
					i = (*p->width)(p, *seq++);
					if (rsz < i)
						rsz = i;
				}
				sz += rsz;
				continue;
			default:
				continue;
			}

			/*
			 * Common handling for Unicode and numbered
			 * character escape sequences.
			 */

			if (rhs == NULL) {
				if (p->enc == TERMENC_ASCII) {
					rhs = ascii_uc2str(uc);
					rsz = strlen(rhs);
				} else {
					if ((uc < 0x20 && uc != 0x09) ||
					    (uc > 0x7E && uc < 0xA0))
						uc = 0xFFFD;
					sz += cond_width(p, uc, &skip);
					continue;
				}
			}

			if (skip) {
				skip = 0;
				break;
			}

			/*
			 * Common handling for all escape sequences
			 * printing more than one character.
			 */

			for (i = 0; i < rsz; i++)
				sz += (*p->width)(p, *rhs++);
			break;
		case ASCII_NBRSP:
			sz += cond_width(p, ' ', &skip);
			cp++;
			break;
		case ASCII_HYPH:
			sz += cond_width(p, '-', &skip);
			cp++;
			break;
		default:
			break;
		}
	}

	return sz;
}

int
term_vspan(const struct termp *p, const struct roffsu *su)
{
	double		 r;
	int		 ri;

	switch (su->unit) {
	case SCALE_BU:
		r = su->scale / 40.0;
		break;
	case SCALE_CM:
		r = su->scale * 6.0 / 2.54;
		break;
	case SCALE_FS:
		r = su->scale * 65536.0 / 40.0;
		break;
	case SCALE_IN:
		r = su->scale * 6.0;
		break;
	case SCALE_MM:
		r = su->scale * 0.006;
		break;
	case SCALE_PC:
		r = su->scale;
		break;
	case SCALE_PT:
		r = su->scale / 12.0;
		break;
	case SCALE_EN:
	case SCALE_EM:
		r = su->scale * 0.6;
		break;
	case SCALE_VS:
		r = su->scale;
		break;
	default:
		abort();
	}
	ri = r > 0.0 ? r + 0.4995 : r - 0.4995;
	return ri < 66 ? ri : 1;
}

/*
 * Convert a scaling width to basic units, rounding towards 0.
 */
int
term_hspan(const struct termp *p, const struct roffsu *su)
{

	return (*p->hspan)(p, su);
}

/*
 * Convert a scaling width to basic units, rounding to closest.
 */
int
term_hen(const struct termp *p, const struct roffsu *su)
{
	int bu;

	if ((bu = (*p->hspan)(p, su)) >= 0)
		return (bu + 11) / 24;
	else
		return -((-bu + 11) / 24);
}
