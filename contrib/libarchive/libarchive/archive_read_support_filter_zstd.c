/*-
 * Copyright (c) 2009-2011 Sean Purcell
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

#include "archive_platform.h"

__FBSDID("$FreeBSD$");

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ZSTD_H
#include <zstd.h>
#endif

#include "archive.h"
#include "archive_endian.h"
#include "archive_private.h"
#include "archive_read_private.h"

#if HAVE_ZSTD_H && HAVE_LIBZSTD

struct private_data {
	ZSTD_DStream	*dstream;
	unsigned char	*out_block;
	size_t		 out_block_size;
	int64_t		 total_out;
	char		 in_frame; /* True = in the middle of a zstd frame. */
	char		 eof; /* True = found end of compressed data. */
};

/* Zstd Filter. */
static ssize_t	zstd_filter_read(struct archive_read_filter *, const void**);
static int	zstd_filter_close(struct archive_read_filter *);
#endif

/*
 * Note that we can detect zstd compressed files even if we can't decompress
 * them.  (In fact, we like detecting them because we can give better error
 * messages.)  So the bid framework here gets compiled even if no zstd library
 * is available.
 */
static int	zstd_bidder_bid(struct archive_read_filter_bidder *,
		    struct archive_read_filter *);
static int	zstd_bidder_init(struct archive_read_filter *);

int
archive_read_support_filter_zstd(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter_bidder *bidder;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_filter_zstd");

	if (__archive_read_get_bidder(a, &bidder) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	bidder->data = NULL;
	bidder->name = "zstd";
	bidder->bid = zstd_bidder_bid;
	bidder->init = zstd_bidder_init;
	bidder->options = NULL;
	bidder->free = NULL;
#if HAVE_ZSTD_H && HAVE_LIBZSTD
	return (ARCHIVE_OK);
#else
	archive_set_error(_a, ARCHIVE_ERRNO_MISC,
	    "Using external zstd program for zstd decompression");
	return (ARCHIVE_WARN);
#endif
}

/*
 * Test whether we can handle this data.
 */
static int
zstd_bidder_bid(struct archive_read_filter_bidder *self,
    struct archive_read_filter *filter)
{
	const unsigned char *buffer;
	ssize_t avail;
	unsigned prefix;

	/* Zstd frame magic values */
	const unsigned zstd_magic = 0xFD2FB528U;

	(void) self; /* UNUSED */

	buffer = __archive_read_filter_ahead(filter, 4, &avail);
	if (buffer == NULL)
		return (0);

	prefix = archive_le32dec(buffer);
	if (prefix == zstd_magic)
		return (32);

	return (0);
}

#if !(HAVE_ZSTD_H && HAVE_LIBZSTD)

/*
 * If we don't have the library on this system, we can't do the
 * decompression directly.  We can, however, try to run "zstd -d"
 * in case that's available.
 */
static int
zstd_bidder_init(struct archive_read_filter *self)
{
	int r;

	r = __archive_read_program(self, "zstd -d -qq");
	/* Note: We set the format here even if __archive_read_program()
	 * above fails.  We do, after all, know what the format is
	 * even if we weren't able to read it. */
	self->code = ARCHIVE_FILTER_ZSTD;
	self->name = "zstd";
	return (r);
}

#else

/*
 * Initialize the filter object
 */
static int
zstd_bidder_init(struct archive_read_filter *self)
{
	struct private_data *state;
	const size_t out_block_size = ZSTD_DStreamOutSize();
	void *out_block;
	ZSTD_DStream *dstream;

	self->code = ARCHIVE_FILTER_ZSTD;
	self->name = "zstd";

	state = (struct private_data *)calloc(sizeof(*state), 1);
	out_block = (unsigned char *)malloc(out_block_size);
	dstream = ZSTD_createDStream();

	if (state == NULL || out_block == NULL || dstream == NULL) {
		free(out_block);
		free(state);
		ZSTD_freeDStream(dstream); /* supports free on NULL */
		archive_set_error(&self->archive->archive, ENOMEM,
		    "Can't allocate data for zstd decompression");
		return (ARCHIVE_FATAL);
	}

	self->data = state;

	state->out_block_size = out_block_size;
	state->out_block = out_block;
	state->dstream = dstream;
	self->read = zstd_filter_read;
	self->skip = NULL; /* not supported */
	self->close = zstd_filter_close;

	state->eof = 0;
	state->in_frame = 0;

	return (ARCHIVE_OK);
}

static ssize_t
zstd_filter_read(struct archive_read_filter *self, const void **p)
{
	struct private_data *state;
	size_t decompressed;
	ssize_t avail_in;
	ZSTD_outBuffer out;
	ZSTD_inBuffer in;

	state = (struct private_data *)self->data;

	out = (ZSTD_outBuffer) { state->out_block, state->out_block_size, 0 };

	/* Try to fill the output buffer. */
	while (out.pos < out.size && !state->eof) {
		if (!state->in_frame) {
			const size_t ret = ZSTD_initDStream(state->dstream);
			if (ZSTD_isError(ret)) {
				archive_set_error(&self->archive->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Error initializing zstd decompressor: %s",
				    ZSTD_getErrorName(ret));
				return (ARCHIVE_FATAL);
			}
		}
		in.src = __archive_read_filter_ahead(self->upstream, 1,
		    &avail_in);
		if (avail_in < 0) {
			return avail_in;
		}
		if (in.src == NULL && avail_in == 0) {
			if (!state->in_frame) {
				/* end of stream */
				state->eof = 1;
				break;
			} else {
				archive_set_error(&self->archive->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Truncated zstd input");
				return (ARCHIVE_FATAL);
			}
		}
		in.size = avail_in;
		in.pos = 0;

		{
			const size_t ret =
			    ZSTD_decompressStream(state->dstream, &out, &in);

			if (ZSTD_isError(ret)) {
				archive_set_error(&self->archive->archive,
				    ARCHIVE_ERRNO_MISC,
				    "Zstd decompression failed: %s",
				    ZSTD_getErrorName(ret));
				return (ARCHIVE_FATAL);
			}

			/* Decompressor made some progress */
			__archive_read_filter_consume(self->upstream, in.pos);

			/* ret guaranteed to be > 0 if frame isn't done yet */
			state->in_frame = (ret != 0);
		}
	}

	decompressed = out.pos;
	state->total_out += decompressed;
	if (decompressed == 0)
		*p = NULL;
	else
		*p = state->out_block;
	return (decompressed);
}

/*
 * Clean up the decompressor.
 */
static int
zstd_filter_close(struct archive_read_filter *self)
{
	struct private_data *state;

	state = (struct private_data *)self->data;

	ZSTD_freeDStream(state->dstream);
	free(state->out_block);
	free(state);

	return (ARCHIVE_OK);
}

#endif /* HAVE_ZLIB_H && HAVE_LIBZSTD */
