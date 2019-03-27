/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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

/*
 * This code borrows heavily from "compress" source code, which is
 * protected by the following copyright.  (Clause 3 dropped by request
 * of the Regents.)
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
__FBSDID("$FreeBSD$");

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "archive.h"
#include "archive_private.h"
#include "archive_read_private.h"

/*
 * Because LZW decompression is pretty simple, I've just implemented
 * the whole decompressor here (cribbing from "compress" source code,
 * of course), rather than relying on an external library.  I have
 * made an effort to clarify and simplify the algorithm, so the
 * names and structure here don't exactly match those used by compress.
 */

struct private_data {
	/* Input variables. */
	const unsigned char	*next_in;
	size_t			 avail_in;
	size_t			 consume_unnotified;
	int			 bit_buffer;
	int			 bits_avail;
	size_t			 bytes_in_section;

	/* Output variables. */
	size_t			 out_block_size;
	void			*out_block;

	/* Decompression status variables. */
	int			 use_reset_code;
	int			 end_of_stream;	/* EOF status. */
	int			 maxcode;	/* Largest code. */
	int			 maxcode_bits;	/* Length of largest code. */
	int			 section_end_code; /* When to increase bits. */
	int			 bits;		/* Current code length. */
	int			 oldcode;	/* Previous code. */
	int			 finbyte;	/* Last byte of prev code. */

	/* Dictionary. */
	int			 free_ent;       /* Next dictionary entry. */
	unsigned char		 suffix[65536];
	uint16_t		 prefix[65536];

	/*
	 * Scratch area for expanding dictionary entries.  Note:
	 * "worst" case here comes from compressing /dev/zero: the
	 * last code in the dictionary will code a sequence of
	 * 65536-256 zero bytes.  Thus, we need stack space to expand
	 * a 65280-byte dictionary entry.  (Of course, 32640:1
	 * compression could also be considered the "best" case. ;-)
	 */
	unsigned char		*stackp;
	unsigned char		 stack[65300];
};

static int	compress_bidder_bid(struct archive_read_filter_bidder *, struct archive_read_filter *);
static int	compress_bidder_init(struct archive_read_filter *);
static int	compress_bidder_free(struct archive_read_filter_bidder *);

static ssize_t	compress_filter_read(struct archive_read_filter *, const void **);
static int	compress_filter_close(struct archive_read_filter *);

static int	getbits(struct archive_read_filter *, int n);
static int	next_code(struct archive_read_filter *);

#if ARCHIVE_VERSION_NUMBER < 4000000
/* Deprecated; remove in libarchive 4.0 */
int
archive_read_support_compression_compress(struct archive *a)
{
	return archive_read_support_filter_compress(a);
}
#endif

int
archive_read_support_filter_compress(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter_bidder *bidder;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_filter_compress");

	if (__archive_read_get_bidder(a, &bidder) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	bidder->data = NULL;
	bidder->name = "compress (.Z)";
	bidder->bid = compress_bidder_bid;
	bidder->init = compress_bidder_init;
	bidder->options = NULL;
	bidder->free = compress_bidder_free;
	return (ARCHIVE_OK);
}

/*
 * Test whether we can handle this data.
 * This logic returns zero if any part of the signature fails.
 */
static int
compress_bidder_bid(struct archive_read_filter_bidder *self,
    struct archive_read_filter *filter)
{
	const unsigned char *buffer;
	ssize_t avail;
	int bits_checked;

	(void)self; /* UNUSED */

	/* Shortest valid compress file is 3 bytes. */
	buffer = __archive_read_filter_ahead(filter, 3, &avail);

	if (buffer == NULL)
		return (0);

	bits_checked = 0;
	/* First two bytes are the magic value */
	if (buffer[0] != 0x1F || buffer[1] != 0x9D)
		return (0);
	/* Third byte holds compression parameters. */
	if (buffer[2] & 0x20) /* Reserved bit, must be zero. */
		return (0);
	if (buffer[2] & 0x40) /* Reserved bit, must be zero. */
		return (0);
	bits_checked += 18;

	return (bits_checked);
}

/*
 * Setup the callbacks.
 */
static int
compress_bidder_init(struct archive_read_filter *self)
{
	struct private_data *state;
	static const size_t out_block_size = 64 * 1024;
	void *out_block;
	int code;

	self->code = ARCHIVE_FILTER_COMPRESS;
	self->name = "compress (.Z)";

	state = (struct private_data *)calloc(sizeof(*state), 1);
	out_block = malloc(out_block_size);
	if (state == NULL || out_block == NULL) {
		free(out_block);
		free(state);
		archive_set_error(&self->archive->archive, ENOMEM,
		    "Can't allocate data for %s decompression",
		    self->name);
		return (ARCHIVE_FATAL);
	}

	self->data = state;
	state->out_block_size = out_block_size;
	state->out_block = out_block;
	self->read = compress_filter_read;
	self->skip = NULL; /* not supported */
	self->close = compress_filter_close;

	/* XXX MOVE THE FOLLOWING OUT OF INIT() XXX */

	(void)getbits(self, 8); /* Skip first signature byte. */
	(void)getbits(self, 8); /* Skip second signature byte. */

	/* Get compression parameters. */
	code = getbits(self, 8);
	if ((code & 0x1f) > 16) {
		archive_set_error(&self->archive->archive, -1,
		    "Invalid compressed data");
		return (ARCHIVE_FATAL);
	}
	state->maxcode_bits = code & 0x1f;
	state->maxcode = (1 << state->maxcode_bits);
	state->use_reset_code = code & 0x80;

	/* Initialize decompressor. */
	state->free_ent = 256;
	state->stackp = state->stack;
	if (state->use_reset_code)
		state->free_ent++;
	state->bits = 9;
	state->section_end_code = (1<<state->bits) - 1;
	state->oldcode = -1;
	for (code = 255; code >= 0; code--) {
		state->prefix[code] = 0;
		state->suffix[code] = code;
	}
	next_code(self);

	return (ARCHIVE_OK);
}

/*
 * Return a block of data from the decompression buffer.  Decompress more
 * as necessary.
 */
static ssize_t
compress_filter_read(struct archive_read_filter *self, const void **pblock)
{
	struct private_data *state;
	unsigned char *p, *start, *end;
	int ret;

	state = (struct private_data *)self->data;
	if (state->end_of_stream) {
		*pblock = NULL;
		return (0);
	}
	p = start = (unsigned char *)state->out_block;
	end = start + state->out_block_size;

	while (p < end && !state->end_of_stream) {
		if (state->stackp > state->stack) {
			*p++ = *--state->stackp;
		} else {
			ret = next_code(self);
			if (ret == -1)
				state->end_of_stream = ret;
			else if (ret != ARCHIVE_OK)
				return (ret);
		}
	}

	*pblock = start;
	return (p - start);
}

/*
 * Clean up the reader.
 */
static int
compress_bidder_free(struct archive_read_filter_bidder *self)
{
	self->data = NULL;
	return (ARCHIVE_OK);
}

/*
 * Close and release the filter.
 */
static int
compress_filter_close(struct archive_read_filter *self)
{
	struct private_data *state = (struct private_data *)self->data;

	free(state->out_block);
	free(state);
	return (ARCHIVE_OK);
}

/*
 * Process the next code and fill the stack with the expansion
 * of the code.  Returns ARCHIVE_FATAL if there is a fatal I/O or
 * format error, ARCHIVE_EOF if we hit end of data, ARCHIVE_OK otherwise.
 */
static int
next_code(struct archive_read_filter *self)
{
	struct private_data *state = (struct private_data *)self->data;
	int code, newcode;

	static int debug_buff[1024];
	static unsigned debug_index;

	code = newcode = getbits(self, state->bits);
	if (code < 0)
		return (code);

	debug_buff[debug_index++] = code;
	if (debug_index >= sizeof(debug_buff)/sizeof(debug_buff[0]))
		debug_index = 0;

	/* If it's a reset code, reset the dictionary. */
	if ((code == 256) && state->use_reset_code) {
		/*
		 * The original 'compress' implementation blocked its
		 * I/O in a manner that resulted in junk bytes being
		 * inserted after every reset.  The next section skips
		 * this junk.  (Yes, the number of *bytes* to skip is
		 * a function of the current *bit* length.)
		 */
		int skip_bytes =  state->bits -
		    (state->bytes_in_section % state->bits);
		skip_bytes %= state->bits;
		state->bits_avail = 0; /* Discard rest of this byte. */
		while (skip_bytes-- > 0) {
			code = getbits(self, 8);
			if (code < 0)
				return (code);
		}
		/* Now, actually do the reset. */
		state->bytes_in_section = 0;
		state->bits = 9;
		state->section_end_code = (1 << state->bits) - 1;
		state->free_ent = 257;
		state->oldcode = -1;
		return (next_code(self));
	}

	if (code > state->free_ent
	    || (code == state->free_ent && state->oldcode < 0)) {
		/* An invalid code is a fatal error. */
		archive_set_error(&(self->archive->archive), -1,
		    "Invalid compressed data");
		return (ARCHIVE_FATAL);
	}

	/* Special case for KwKwK string. */
	if (code >= state->free_ent) {
		*state->stackp++ = state->finbyte;
		code = state->oldcode;
	}

	/* Generate output characters in reverse order. */
	while (code >= 256) {
		*state->stackp++ = state->suffix[code];
		code = state->prefix[code];
	}
	*state->stackp++ = state->finbyte = code;

	/* Generate the new entry. */
	code = state->free_ent;
	if (code < state->maxcode && state->oldcode >= 0) {
		state->prefix[code] = state->oldcode;
		state->suffix[code] = state->finbyte;
		++state->free_ent;
	}
	if (state->free_ent > state->section_end_code) {
		state->bits++;
		state->bytes_in_section = 0;
		if (state->bits == state->maxcode_bits)
			state->section_end_code = state->maxcode;
		else
			state->section_end_code = (1 << state->bits) - 1;
	}

	/* Remember previous code. */
	state->oldcode = newcode;
	return (ARCHIVE_OK);
}

/*
 * Return next 'n' bits from stream.
 *
 * -1 indicates end of available data.
 */
static int
getbits(struct archive_read_filter *self, int n)
{
	struct private_data *state = (struct private_data *)self->data;
	int code;
	ssize_t ret;
	static const int mask[] = {
		0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff,
		0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff
	};

	while (state->bits_avail < n) {
		if (state->avail_in <= 0) {
			if (state->consume_unnotified) {
				__archive_read_filter_consume(self->upstream,
					state->consume_unnotified);
				state->consume_unnotified = 0;
			}
			state->next_in
			    = __archive_read_filter_ahead(self->upstream,
				1, &ret);
			if (ret == 0)
				return (-1);
			if (ret < 0 || state->next_in == NULL)
				return (ARCHIVE_FATAL);
			state->consume_unnotified = state->avail_in = ret;
		}
		state->bit_buffer |= *state->next_in++ << state->bits_avail;
		state->avail_in--;
		state->bits_avail += 8;
		state->bytes_in_section++;
	}

	code = state->bit_buffer;
	state->bit_buffer >>= n;
	state->bits_avail -= n;

	return (code & mask[n]);
}
