/*	$Id: dba_write.c,v 1.3 2016/08/05 23:15:08 schwarze Exp $ */
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
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
 *
 * Low-level functions for serializing allocation-based data to disk.
 * The interface is defined in "dba_write.h".
 */
#include "config.h"

#include <assert.h>
#if HAVE_ENDIAN
#include <endian.h>
#elif HAVE_SYS_ENDIAN
#include <sys/endian.h>
#elif HAVE_NTOHL
#include <arpa/inet.h>
#endif
#if HAVE_ERR
#include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>

#include "dba_write.h"

static FILE	*ofp;


int
dba_open(const char *fname)
{
	ofp = fopen(fname, "w");
	return ofp == NULL ? -1 : 0;
}

int
dba_close(void)
{
	return fclose(ofp) == EOF ? -1 : 0;
}

int32_t
dba_tell(void)
{
	long		 pos;

	if ((pos = ftell(ofp)) == -1)
		err(1, "ftell");
	if (pos >= INT32_MAX) {
		errno = EOVERFLOW;
		err(1, "ftell = %ld", pos);
	}
	return pos;
}

void
dba_seek(int32_t pos)
{
	if (fseek(ofp, pos, SEEK_SET) == -1)
		err(1, "fseek(%d)", pos);
}

int32_t
dba_align(void)
{
	int32_t		 pos;

	pos = dba_tell();
	while (pos & 3) {
		dba_char_write('\0');
		pos++;
	}
	return pos;
}

int32_t
dba_skip(int32_t nmemb, int32_t sz)
{
	const int32_t	 out[5] = {0, 0, 0, 0, 0};
	int32_t		 i, pos;

	assert(sz >= 0);
	assert(nmemb > 0);
	assert(nmemb <= 5);
	pos = dba_tell();
	for (i = 0; i < sz; i++)
		if (nmemb - fwrite(&out, sizeof(out[0]), nmemb, ofp))
			err(1, "fwrite");
	return pos;
}

void
dba_char_write(int c)
{
	if (putc(c, ofp) == EOF)
		err(1, "fputc");
}

void
dba_str_write(const char *str)
{
	if (fputs(str, ofp) == EOF)
		err(1, "fputs");
	dba_char_write('\0');
}

void
dba_int_write(int32_t i)
{
	i = htobe32(i);
	if (fwrite(&i, sizeof(i), 1, ofp) != 1)
		err(1, "fwrite");
}
