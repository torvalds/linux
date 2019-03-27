/*-
 * Copyright (c) 2008 Joerg Sonnenberger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1985, 1986, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Diomidis Spinellis and James A. Woods, derived from original
 * work by Spencer Thomas and Joseph Orost.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "archive_platform.h"

__FBSDID("$FreeBSD: head/lib/libarchive/archive_write_set_compression_compress.c 201111 2009-12-28 03:33:05Z kientzle $");

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "archive.h"
#include "archive_private.h"
#include "archive_write_private.h"

#define	HSIZE		69001	/* 95% occupancy */
#define	HSHIFT		8	/* 8 - trunc(log2(HSIZE / 65536)) */
#define	CHECK_GAP 10000		/* Ratio check interval. */

#define	MAXCODE(bits)	((1 << (bits)) - 1)

/*
 * the next two codes should not be changed lightly, as they must not
 * lie within the contiguous general code space.
 */
#define	FIRST	257		/* First free entry. */
#define	CLEAR	256		/* Table clear output code. */

struct private_data {
	int64_t in_count, out_count, checkpoint;

	int code_len;			/* Number of bits/code. */
	int cur_maxcode;		/* Maximum code, given n_bits. */
	int max_maxcode;		/* Should NEVER generate this code. */
	int hashtab [HSIZE];
	unsigned short codetab [HSIZE];
	int first_free;		/* First unused entry. */
	int compress_ratio;

	int cur_code, cur_fcode;

	int bit_offset;
	unsigned char bit_buf;

	unsigned char	*compressed;
	size_t		 compressed_buffer_size;
	size_t		 compressed_offset;
};

static int archive_compressor_compress_open(struct archive_write_filter *);
static int archive_compressor_compress_write(struct archive_write_filter *,
		    const void *, size_t);
static int archive_compressor_compress_close(struct archive_write_filter *);
static int archive_compressor_compress_free(struct archive_write_filter *);

#if ARCHIVE_VERSION_NUMBER < 4000000
int
archive_write_set_compression_compress(struct archive *a)
{
	__archive_write_filters_free(a);
	return (archive_write_add_filter_compress(a));
}
#endif

/*
 * Add a compress filter to this write handle.
 */
int
archive_write_add_filter_compress(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;
	struct archive_write_filter *f = __archive_write_allocate_filter(_a);

	archive_check_magic(&a->archive, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_add_filter_compress");
	f->open = &archive_compressor_compress_open;
	f->code = ARCHIVE_FILTER_COMPRESS;
	f->name = "compress";
	return (ARCHIVE_OK);
}

/*
 * Setup callback.
 */
static int
archive_compressor_compress_open(struct archive_write_filter *f)
{
	int ret;
	struct private_data *state;
	size_t bs = 65536, bpb;

	f->code = ARCHIVE_FILTER_COMPRESS;
	f->name = "compress";

	ret = __archive_write_open_filter(f->next_filter);
	if (ret != ARCHIVE_OK)
		return (ret);

	state = (struct private_data *)calloc(1, sizeof(*state));
	if (state == NULL) {
		archive_set_error(f->archive, ENOMEM,
		    "Can't allocate data for compression");
		return (ARCHIVE_FATAL);
	}

	if (f->archive->magic == ARCHIVE_WRITE_MAGIC) {
		/* Buffer size should be a multiple number of the of bytes
		 * per block for performance. */
		bpb = archive_write_get_bytes_per_block(f->archive);
		if (bpb > bs)
			bs = bpb;
		else if (bpb != 0)
			bs -= bs % bpb;
	}
	state->compressed_buffer_size = bs;
	state->compressed = malloc(state->compressed_buffer_size);

	if (state->compressed == NULL) {
		archive_set_error(f->archive, ENOMEM,
		    "Can't allocate data for compression buffer");
		free(state);
		return (ARCHIVE_FATAL);
	}

	f->write = archive_compressor_compress_write;
	f->close = archive_compressor_compress_close;
	f->free = archive_compressor_compress_free;

	state->max_maxcode = 0x10000;	/* Should NEVER generate this code. */
	state->in_count = 0;		/* Length of input. */
	state->bit_buf = 0;
	state->bit_offset = 0;
	state->out_count = 3;		/* Includes 3-byte header mojo. */
	state->compress_ratio = 0;
	state->checkpoint = CHECK_GAP;
	state->code_len = 9;
	state->cur_maxcode = MAXCODE(state->code_len);
	state->first_free = FIRST;

	memset(state->hashtab, 0xff, sizeof(state->hashtab));

	/* Prime output buffer with a gzip header. */
	state->compressed[0] = 0x1f; /* Compress */
	state->compressed[1] = 0x9d;
	state->compressed[2] = 0x90; /* Block mode, 16bit max */
	state->compressed_offset = 3;

	f->data = state;
	return (0);
}

/*-
 * Output the given code.
 * Inputs:
 * 	code:	A n_bits-bit integer.  If == -1, then EOF.  This assumes
 *		that n_bits <= (long)wordsize - 1.
 * Outputs:
 * 	Outputs code to the file.
 * Assumptions:
 *	Chars are 8 bits long.
 * Algorithm:
 * 	Maintain a BITS character long buffer (so that 8 codes will
 * fit in it exactly).  Use the VAX insv instruction to insert each
 * code in turn.  When the buffer fills up empty it and start over.
 */

static const unsigned char rmask[9] =
	{0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};

static int
output_byte(struct archive_write_filter *f, unsigned char c)
{
	struct private_data *state = f->data;

	state->compressed[state->compressed_offset++] = c;
	++state->out_count;

	if (state->compressed_buffer_size == state->compressed_offset) {
		int ret = __archive_write_filter(f->next_filter,
		    state->compressed, state->compressed_buffer_size);
		if (ret != ARCHIVE_OK)
			return ARCHIVE_FATAL;
		state->compressed_offset = 0;
	}

	return ARCHIVE_OK;
}

static int
output_code(struct archive_write_filter *f, int ocode)
{
	struct private_data *state = f->data;
	int bits, ret, clear_flg, bit_offset;

	clear_flg = ocode == CLEAR;

	/*
	 * Since ocode is always >= 8 bits, only need to mask the first
	 * hunk on the left.
	 */
	bit_offset = state->bit_offset % 8;
	state->bit_buf |= (ocode << bit_offset) & 0xff;
	output_byte(f, state->bit_buf);

	bits = state->code_len - (8 - bit_offset);
	ocode >>= 8 - bit_offset;
	/* Get any 8 bit parts in the middle (<=1 for up to 16 bits). */
	if (bits >= 8) {
		output_byte(f, ocode & 0xff);
		ocode >>= 8;
		bits -= 8;
	}
	/* Last bits. */
	state->bit_offset += state->code_len;
	state->bit_buf = ocode & rmask[bits];
	if (state->bit_offset == state->code_len * 8)
		state->bit_offset = 0;

	/*
	 * If the next entry is going to be too big for the ocode size,
	 * then increase it, if possible.
	 */
	if (clear_flg || state->first_free > state->cur_maxcode) {
	       /*
		* Write the whole buffer, because the input side won't
		* discover the size increase until after it has read it.
		*/
		if (state->bit_offset > 0) {
			while (state->bit_offset < state->code_len * 8) {
				ret = output_byte(f, state->bit_buf);
				if (ret != ARCHIVE_OK)
					return ret;
				state->bit_offset += 8;
				state->bit_buf = 0;
			}
		}
		state->bit_buf = 0;
		state->bit_offset = 0;

		if (clear_flg) {
			state->code_len = 9;
			state->cur_maxcode = MAXCODE(state->code_len);
		} else {
			state->code_len++;
			if (state->code_len == 16)
				state->cur_maxcode = state->max_maxcode;
			else
				state->cur_maxcode = MAXCODE(state->code_len);
		}
	}

	return (ARCHIVE_OK);
}

static int
output_flush(struct archive_write_filter *f)
{
	struct private_data *state = f->data;
	int ret;

	/* At EOF, write the rest of the buffer. */
	if (state->bit_offset % 8) {
		state->code_len = (state->bit_offset % 8 + 7) / 8;
		ret = output_byte(f, state->bit_buf);
		if (ret != ARCHIVE_OK)
			return ret;
	}

	return (ARCHIVE_OK);
}

/*
 * Write data to the compressed stream.
 */
static int
archive_compressor_compress_write(struct archive_write_filter *f,
    const void *buff, size_t length)
{
	struct private_data *state = (struct private_data *)f->data;
	int i;
	int ratio;
	int c, disp, ret;
	const unsigned char *bp;

	if (length == 0)
		return ARCHIVE_OK;

	bp = buff;

	if (state->in_count == 0) {
		state->cur_code = *bp++;
		++state->in_count;
		--length;
	}

	while (length--) {
		c = *bp++;
		state->in_count++;
		state->cur_fcode = (c << 16) + state->cur_code;
		i = ((c << HSHIFT) ^ state->cur_code);	/* Xor hashing. */

		if (state->hashtab[i] == state->cur_fcode) {
			state->cur_code = state->codetab[i];
			continue;
		}
		if (state->hashtab[i] < 0)	/* Empty slot. */
			goto nomatch;
		/* Secondary hash (after G. Knott). */
		if (i == 0)
			disp = 1;
		else
			disp = HSIZE - i;
 probe:
		if ((i -= disp) < 0)
			i += HSIZE;

		if (state->hashtab[i] == state->cur_fcode) {
			state->cur_code = state->codetab[i];
			continue;
		}
		if (state->hashtab[i] >= 0)
			goto probe;
 nomatch:
		ret = output_code(f, state->cur_code);
		if (ret != ARCHIVE_OK)
			return ret;
		state->cur_code = c;
		if (state->first_free < state->max_maxcode) {
			state->codetab[i] = state->first_free++;	/* code -> hashtable */
			state->hashtab[i] = state->cur_fcode;
			continue;
		}
		if (state->in_count < state->checkpoint)
			continue;

		state->checkpoint = state->in_count + CHECK_GAP;

		if (state->in_count <= 0x007fffff && state->out_count != 0)
			ratio = (int)(state->in_count * 256 / state->out_count);
		else if ((ratio = (int)(state->out_count / 256)) == 0)
			ratio = 0x7fffffff;
		else
			ratio = (int)(state->in_count / ratio);

		if (ratio > state->compress_ratio)
			state->compress_ratio = ratio;
		else {
			state->compress_ratio = 0;
			memset(state->hashtab, 0xff, sizeof(state->hashtab));
			state->first_free = FIRST;
			ret = output_code(f, CLEAR);
			if (ret != ARCHIVE_OK)
				return ret;
		}
	}

	return (ARCHIVE_OK);
}


/*
 * Finish the compression...
 */
static int
archive_compressor_compress_close(struct archive_write_filter *f)
{
	struct private_data *state = (struct private_data *)f->data;
	int ret, ret2;

	ret = output_code(f, state->cur_code);
	if (ret != ARCHIVE_OK)
		goto cleanup;
	ret = output_flush(f);
	if (ret != ARCHIVE_OK)
		goto cleanup;

	/* Write the last block */
	ret = __archive_write_filter(f->next_filter,
	    state->compressed, state->compressed_offset);
cleanup:
	ret2 = __archive_write_close_filter(f->next_filter);
	if (ret > ret2)
		ret = ret2;
	free(state->compressed);
	free(state);
	return (ret);
}

static int
archive_compressor_compress_free(struct archive_write_filter *f)
{
	(void)f; /* UNUSED */
	return (ARCHIVE_OK);
}
