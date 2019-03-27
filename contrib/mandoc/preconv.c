/*	$Id: preconv.c,v 1.16 2017/02/18 13:43:52 schwarze Exp $ */
/*
 * Copyright (c) 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2014 Ingo Schwarze <schwarze@openbsd.org>
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
#include <stdio.h>
#include <string.h>
#include "mandoc.h"
#include "libmandoc.h"

int
preconv_encode(const struct buf *ib, size_t *ii, struct buf *ob, size_t *oi,
    int *filenc)
{
	const unsigned char	*cu;
	int			 nby;
	unsigned int		 accum;

	cu = (const unsigned char *)ib->buf + *ii;
	assert(*cu & 0x80);

	if ( ! (*filenc & MPARSE_UTF8))
		goto latin;

	nby = 1;
	while (nby < 5 && *cu & (1 << (7 - nby)))
		nby++;

	switch (nby) {
	case 2:
		accum = *cu & 0x1f;
		if (accum < 0x02)  /* Obfuscated ASCII. */
			goto latin;
		break;
	case 3:
		accum = *cu & 0x0f;
		break;
	case 4:
		accum = *cu & 0x07;
		if (accum > 0x04) /* Beyond Unicode. */
			goto latin;
		break;
	default:  /* Bad sequence header. */
		goto latin;
	}

	cu++;
	switch (nby) {
	case 3:
		if ((accum == 0x00 && ! (*cu & 0x20)) ||  /* Use 2-byte. */
		    (accum == 0x0d && *cu & 0x20))  /* Surrogates. */
			goto latin;
		break;
	case 4:
		if ((accum == 0x00 && ! (*cu & 0x30)) ||  /* Use 3-byte. */
		    (accum == 0x04 && *cu & 0x30))  /* Beyond Unicode. */
			goto latin;
		break;
	default:
		break;
	}

	while (--nby) {
		if ((*cu & 0xc0) != 0x80)  /* Invalid continuation. */
			goto latin;
		accum <<= 6;
		accum += *cu & 0x3f;
		cu++;
	}

	assert(accum > 0x7f);
	assert(accum < 0x110000);
	assert(accum < 0xd800 || accum > 0xdfff);

	*oi += snprintf(ob->buf + *oi, 11, "\\[u%.4X]", accum);
	*ii = (const char *)cu - ib->buf;
	*filenc &= ~MPARSE_LATIN1;
	return 1;

latin:
	if ( ! (*filenc & MPARSE_LATIN1))
		return 0;

	*oi += snprintf(ob->buf + *oi, 11,
	    "\\[u%.4X]", (unsigned char)ib->buf[(*ii)++]);

	*filenc &= ~MPARSE_UTF8;
	return 1;
}

int
preconv_cue(const struct buf *b, size_t offset)
{
	const char	*ln, *eoln, *eoph;
	size_t		 sz, phsz;

	ln = b->buf + offset;
	sz = b->sz - offset;

	/* Look for the end-of-line. */

	if (NULL == (eoln = memchr(ln, '\n', sz)))
		eoln = ln + sz;

	/* Check if we have the correct header/trailer. */

	if ((sz = (size_t)(eoln - ln)) < 10 ||
	    memcmp(ln, ".\\\" -*-", 7) || memcmp(eoln - 3, "-*-", 3))
		return MPARSE_UTF8 | MPARSE_LATIN1;

	/* Move after the header and adjust for the trailer. */

	ln += 7;
	sz -= 10;

	while (sz > 0) {
		while (sz > 0 && ' ' == *ln) {
			ln++;
			sz--;
		}
		if (0 == sz)
			break;

		/* Find the end-of-phrase marker (or eoln). */

		if (NULL == (eoph = memchr(ln, ';', sz)))
			eoph = eoln - 3;
		else
			eoph++;

		/* Only account for the "coding" phrase. */

		if ((phsz = eoph - ln) < 7 ||
		    strncasecmp(ln, "coding:", 7)) {
			sz -= phsz;
			ln += phsz;
			continue;
		}

		sz -= 7;
		ln += 7;

		while (sz > 0 && ' ' == *ln) {
			ln++;
			sz--;
		}
		if (0 == sz)
			return 0;

		/* Check us against known encodings. */

		if (phsz > 4 && !strncasecmp(ln, "utf-8", 5))
			return MPARSE_UTF8;
		if (phsz > 10 && !strncasecmp(ln, "iso-latin-1", 11))
			return MPARSE_LATIN1;
		return 0;
	}
	return MPARSE_UTF8 | MPARSE_LATIN1;
}
