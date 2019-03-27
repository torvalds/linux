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

#include "archive_platform.h"

__FBSDID("$FreeBSD$");

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
#ifdef HAVE_BZLIB_H
#include <bzlib.h>
#endif

#include "archive.h"
#include "archive_private.h"
#include "archive_read_private.h"

#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
struct private_data {
	bz_stream	 stream;
	char		*out_block;
	size_t		 out_block_size;
	char		 valid; /* True = decompressor is initialized */
	char		 eof; /* True = found end of compressed data. */
};

/* Bzip2 filter */
static ssize_t	bzip2_filter_read(struct archive_read_filter *, const void **);
static int	bzip2_filter_close(struct archive_read_filter *);
#endif

/*
 * Note that we can detect bzip2 archives even if we can't decompress
 * them.  (In fact, we like detecting them because we can give better
 * error messages.)  So the bid framework here gets compiled even
 * if bzlib is unavailable.
 */
static int	bzip2_reader_bid(struct archive_read_filter_bidder *, struct archive_read_filter *);
static int	bzip2_reader_init(struct archive_read_filter *);
static int	bzip2_reader_free(struct archive_read_filter_bidder *);

#if ARCHIVE_VERSION_NUMBER < 4000000
/* Deprecated; remove in libarchive 4.0 */
int
archive_read_support_compression_bzip2(struct archive *a)
{
	return archive_read_support_filter_bzip2(a);
}
#endif

int
archive_read_support_filter_bzip2(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter_bidder *reader;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_filter_bzip2");

	if (__archive_read_get_bidder(a, &reader) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	reader->data = NULL;
	reader->name = "bzip2";
	reader->bid = bzip2_reader_bid;
	reader->init = bzip2_reader_init;
	reader->options = NULL;
	reader->free = bzip2_reader_free;
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
	return (ARCHIVE_OK);
#else
	archive_set_error(_a, ARCHIVE_ERRNO_MISC,
	    "Using external bzip2 program");
	return (ARCHIVE_WARN);
#endif
}

static int
bzip2_reader_free(struct archive_read_filter_bidder *self){
	(void)self; /* UNUSED */
	return (ARCHIVE_OK);
}

/*
 * Test whether we can handle this data.
 *
 * This logic returns zero if any part of the signature fails.  It
 * also tries to Do The Right Thing if a very short buffer prevents us
 * from verifying as much as we would like.
 */
static int
bzip2_reader_bid(struct archive_read_filter_bidder *self, struct archive_read_filter *filter)
{
	const unsigned char *buffer;
	ssize_t avail;
	int bits_checked;

	(void)self; /* UNUSED */

	/* Minimal bzip2 archive is 14 bytes. */
	buffer = __archive_read_filter_ahead(filter, 14, &avail);
	if (buffer == NULL)
		return (0);

	/* First three bytes must be "BZh" */
	bits_checked = 0;
	if (memcmp(buffer, "BZh", 3) != 0)
		return (0);
	bits_checked += 24;

	/* Next follows a compression flag which must be an ASCII digit. */
	if (buffer[3] < '1' || buffer[3] > '9')
		return (0);
	bits_checked += 5;

	/* After BZh[1-9], there must be either a data block
	 * which begins with 0x314159265359 or an end-of-data
	 * marker of 0x177245385090. */
	if (memcmp(buffer + 4, "\x31\x41\x59\x26\x53\x59", 6) == 0)
		bits_checked += 48;
	else if (memcmp(buffer + 4, "\x17\x72\x45\x38\x50\x90", 6) == 0)
		bits_checked += 48;
	else
		return (0);

	return (bits_checked);
}

#if !defined(HAVE_BZLIB_H) || !defined(BZ_CONFIG_ERROR)

/*
 * If we don't have the library on this system, we can't actually do the
 * decompression.  We can, however, still detect compressed archives
 * and emit a useful message.
 */
static int
bzip2_reader_init(struct archive_read_filter *self)
{
	int r;

	r = __archive_read_program(self, "bzip2 -d");
	/* Note: We set the format here even if __archive_read_program()
	 * above fails.  We do, after all, know what the format is
	 * even if we weren't able to read it. */
	self->code = ARCHIVE_FILTER_BZIP2;
	self->name = "bzip2";
	return (r);
}


#else

/*
 * Setup the callbacks.
 */
static int
bzip2_reader_init(struct archive_read_filter *self)
{
	static const size_t out_block_size = 64 * 1024;
	void *out_block;
	struct private_data *state;

	self->code = ARCHIVE_FILTER_BZIP2;
	self->name = "bzip2";

	state = (struct private_data *)calloc(sizeof(*state), 1);
	out_block = (unsigned char *)malloc(out_block_size);
	if (state == NULL || out_block == NULL) {
		archive_set_error(&self->archive->archive, ENOMEM,
		    "Can't allocate data for bzip2 decompression");
		free(out_block);
		free(state);
		return (ARCHIVE_FATAL);
	}

	self->data = state;
	state->out_block_size = out_block_size;
	state->out_block = out_block;
	self->read = bzip2_filter_read;
	self->skip = NULL; /* not supported */
	self->close = bzip2_filter_close;

	return (ARCHIVE_OK);
}

/*
 * Return the next block of decompressed data.
 */
static ssize_t
bzip2_filter_read(struct archive_read_filter *self, const void **p)
{
	struct private_data *state;
	size_t decompressed;
	const char *read_buf;
	ssize_t ret;

	state = (struct private_data *)self->data;

	if (state->eof) {
		*p = NULL;
		return (0);
	}

	/* Empty our output buffer. */
	state->stream.next_out = state->out_block;
	state->stream.avail_out = state->out_block_size;

	/* Try to fill the output buffer. */
	for (;;) {
		if (!state->valid) {
			if (bzip2_reader_bid(self->bidder, self->upstream) == 0) {
				state->eof = 1;
				*p = state->out_block;
				decompressed = state->stream.next_out
				    - state->out_block;
				return (decompressed);
			}
			/* Initialize compression library. */
			ret = BZ2_bzDecompressInit(&(state->stream),
					   0 /* library verbosity */,
					   0 /* don't use low-mem algorithm */);

			/* If init fails, try low-memory algorithm instead. */
			if (ret == BZ_MEM_ERROR)
				ret = BZ2_bzDecompressInit(&(state->stream),
					   0 /* library verbosity */,
					   1 /* do use low-mem algo */);

			if (ret != BZ_OK) {
				const char *detail = NULL;
				int err = ARCHIVE_ERRNO_MISC;
				switch (ret) {
				case BZ_PARAM_ERROR:
					detail = "invalid setup parameter";
					break;
				case BZ_MEM_ERROR:
					err = ENOMEM;
					detail = "out of memory";
					break;
				case BZ_CONFIG_ERROR:
					detail = "mis-compiled library";
					break;
				}
				archive_set_error(&self->archive->archive, err,
				    "Internal error initializing decompressor%s%s",
				    detail == NULL ? "" : ": ",
				    detail);
				return (ARCHIVE_FATAL);
			}
			state->valid = 1;
		}

		/* stream.next_in is really const, but bzlib
		 * doesn't declare it so. <sigh> */
		read_buf =
		    __archive_read_filter_ahead(self->upstream, 1, &ret);
		if (read_buf == NULL) {
			archive_set_error(&self->archive->archive,
			    ARCHIVE_ERRNO_MISC,
			    "truncated bzip2 input");
			return (ARCHIVE_FATAL);
		}
		state->stream.next_in = (char *)(uintptr_t)read_buf;
		state->stream.avail_in = ret;
		/* There is no more data, return whatever we have. */
		if (ret == 0) {
			state->eof = 1;
			*p = state->out_block;
			decompressed = state->stream.next_out
			    - state->out_block;
			return (decompressed);
		}

		/* Decompress as much as we can in one pass. */
		ret = BZ2_bzDecompress(&(state->stream));
		__archive_read_filter_consume(self->upstream,
		    state->stream.next_in - read_buf);

		switch (ret) {
		case BZ_STREAM_END: /* Found end of stream. */
			switch (BZ2_bzDecompressEnd(&(state->stream))) {
			case BZ_OK:
				break;
			default:
				archive_set_error(&(self->archive->archive),
					  ARCHIVE_ERRNO_MISC,
					  "Failed to clean up decompressor");
				return (ARCHIVE_FATAL);
			}
			state->valid = 0;
			/* FALLTHROUGH */
		case BZ_OK: /* Decompressor made some progress. */
			/* If we filled our buffer, update stats and return. */
			if (state->stream.avail_out == 0) {
				*p = state->out_block;
				decompressed = state->stream.next_out
				    - state->out_block;
				return (decompressed);
			}
			break;
		default: /* Return an error. */
			archive_set_error(&self->archive->archive,
			    ARCHIVE_ERRNO_MISC, "bzip decompression failed");
			return (ARCHIVE_FATAL);
		}
	}
}

/*
 * Clean up the decompressor.
 */
static int
bzip2_filter_close(struct archive_read_filter *self)
{
	struct private_data *state;
	int ret = ARCHIVE_OK;

	state = (struct private_data *)self->data;

	if (state->valid) {
		switch (BZ2_bzDecompressEnd(&state->stream)) {
		case BZ_OK:
			break;
		default:
			archive_set_error(&self->archive->archive,
					  ARCHIVE_ERRNO_MISC,
					  "Failed to clean up decompressor");
			ret = ARCHIVE_FATAL;
		}
		state->valid = 0;
	}

	free(state->out_block);
	free(state);
	return (ret);
}

#endif /* HAVE_BZLIB_H && BZ_CONFIG_ERROR */
