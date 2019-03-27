/*-
 * Copyright (c) 2014 Michihiro NAKAJIMA
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
#ifdef HAVE_LZ4_H
#include <lz4.h>
#endif

#include "archive.h"
#include "archive_endian.h"
#include "archive_private.h"
#include "archive_read_private.h"
#include "archive_xxhash.h"

#define LZ4_MAGICNUMBER		0x184d2204
#define LZ4_SKIPPABLED		0x184d2a50
#define LZ4_LEGACY		0x184c2102

#if defined(HAVE_LIBLZ4)
struct private_data {
	enum {  SELECT_STREAM,
		READ_DEFAULT_STREAM,
		READ_DEFAULT_BLOCK,
		READ_LEGACY_STREAM,
		READ_LEGACY_BLOCK,
	}		stage;
	struct {
		unsigned block_independence:1;
		unsigned block_checksum:3;
		unsigned stream_size:1;
		unsigned stream_checksum:1;
		unsigned preset_dictionary:1;
		int	 block_maximum_size;
	} flags;
	int64_t		 stream_size;
	uint32_t	 dict_id;
	char		*out_block;
	size_t		 out_block_size;

	/* Bytes read but not yet consumed via __archive_read_consume() */
	size_t		 unconsumed;
	size_t		 decoded_size;
	void		*xxh32_state;

	char		 valid; /* True = decompressor is initialized */
	char		 eof; /* True = found end of compressed data. */
};

#define LEGACY_BLOCK_SIZE	(8 * 1024 * 1024)

/* Lz4 filter */
static ssize_t	lz4_filter_read(struct archive_read_filter *, const void **);
static int	lz4_filter_close(struct archive_read_filter *);
#endif

/*
 * Note that we can detect lz4 archives even if we can't decompress
 * them.  (In fact, we like detecting them because we can give better
 * error messages.)  So the bid framework here gets compiled even
 * if liblz4 is unavailable.
 */
static int	lz4_reader_bid(struct archive_read_filter_bidder *, struct archive_read_filter *);
static int	lz4_reader_init(struct archive_read_filter *);
static int	lz4_reader_free(struct archive_read_filter_bidder *);
#if defined(HAVE_LIBLZ4)
static ssize_t  lz4_filter_read_default_stream(struct archive_read_filter *,
		    const void **);
static ssize_t  lz4_filter_read_legacy_stream(struct archive_read_filter *,
		    const void **);
#endif

int
archive_read_support_filter_lz4(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter_bidder *reader;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_filter_lz4");

	if (__archive_read_get_bidder(a, &reader) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	reader->data = NULL;
	reader->name = "lz4";
	reader->bid = lz4_reader_bid;
	reader->init = lz4_reader_init;
	reader->options = NULL;
	reader->free = lz4_reader_free;
#if defined(HAVE_LIBLZ4)
	return (ARCHIVE_OK);
#else
	archive_set_error(_a, ARCHIVE_ERRNO_MISC,
	    "Using external lz4 program");
	return (ARCHIVE_WARN);
#endif
}

static int
lz4_reader_free(struct archive_read_filter_bidder *self){
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
lz4_reader_bid(struct archive_read_filter_bidder *self,
    struct archive_read_filter *filter)
{
	const unsigned char *buffer;
	ssize_t avail;
	int bits_checked;
	uint32_t number;

	(void)self; /* UNUSED */

	/* Minimal lz4 archive is 11 bytes. */
	buffer = __archive_read_filter_ahead(filter, 11, &avail);
	if (buffer == NULL)
		return (0);

	/* First four bytes must be LZ4 magic numbers. */
	bits_checked = 0;
	if ((number = archive_le32dec(buffer)) == LZ4_MAGICNUMBER) {
		unsigned char flag, BD;

		bits_checked += 32;
		/* Next follows a stream descriptor. */
		/* Descriptor Flags. */
		flag = buffer[4];
		/* A version number must be "01". */
		if (((flag & 0xc0) >> 6) != 1)
			return (0);
		/* A reserved bit must be "0". */
		if (flag & 2)
			return (0);
		bits_checked += 8;
		BD = buffer[5];
		/* A block maximum size should be more than 3. */
		if (((BD & 0x70) >> 4) < 4)
			return (0);
		/* Reserved bits must be "0". */
		if (BD & ~0x70)
			return (0);
		bits_checked += 8;
	} else if (number == LZ4_LEGACY) {
		bits_checked += 32;
	}
	
	return (bits_checked);
}

#if !defined(HAVE_LIBLZ4)

/*
 * If we don't have the library on this system, we can't actually do the
 * decompression.  We can, however, still detect compressed archives
 * and emit a useful message.
 */
static int
lz4_reader_init(struct archive_read_filter *self)
{
	int r;

	r = __archive_read_program(self, "lz4 -d -q");
	/* Note: We set the format here even if __archive_read_program()
	 * above fails.  We do, after all, know what the format is
	 * even if we weren't able to read it. */
	self->code = ARCHIVE_FILTER_LZ4;
	self->name = "lz4";
	return (r);
}


#else

/*
 * Setup the callbacks.
 */
static int
lz4_reader_init(struct archive_read_filter *self)
{
	struct private_data *state;

	self->code = ARCHIVE_FILTER_LZ4;
	self->name = "lz4";

	state = (struct private_data *)calloc(sizeof(*state), 1);
	if (state == NULL) {
		archive_set_error(&self->archive->archive, ENOMEM,
		    "Can't allocate data for lz4 decompression");
		return (ARCHIVE_FATAL);
	}

	self->data = state;
	state->stage = SELECT_STREAM;
	self->read = lz4_filter_read;
	self->skip = NULL; /* not supported */
	self->close = lz4_filter_close;

	return (ARCHIVE_OK);
}

static int
lz4_allocate_out_block(struct archive_read_filter *self)
{
	struct private_data *state = (struct private_data *)self->data;
	size_t out_block_size = state->flags.block_maximum_size;
	void *out_block;

	if (!state->flags.block_independence)
		out_block_size += 64 * 1024;
	if (state->out_block_size < out_block_size) {
		free(state->out_block);
		out_block = (unsigned char *)malloc(out_block_size);
		state->out_block_size = out_block_size;
		if (out_block == NULL) {
			archive_set_error(&self->archive->archive, ENOMEM,
			    "Can't allocate data for lz4 decompression");
			return (ARCHIVE_FATAL);
		}
		state->out_block = out_block;
	}
	if (!state->flags.block_independence)
		memset(state->out_block, 0, 64 * 1024);
	return (ARCHIVE_OK);
}

static int
lz4_allocate_out_block_for_legacy(struct archive_read_filter *self)
{
	struct private_data *state = (struct private_data *)self->data;
	size_t out_block_size = LEGACY_BLOCK_SIZE;
	void *out_block;

	if (state->out_block_size < out_block_size) {
		free(state->out_block);
		out_block = (unsigned char *)malloc(out_block_size);
		state->out_block_size = out_block_size;
		if (out_block == NULL) {
			archive_set_error(&self->archive->archive, ENOMEM,
			    "Can't allocate data for lz4 decompression");
			return (ARCHIVE_FATAL);
		}
		state->out_block = out_block;
	}
	return (ARCHIVE_OK);
}

/*
 * Return the next block of decompressed data.
 */
static ssize_t
lz4_filter_read(struct archive_read_filter *self, const void **p)
{
	struct private_data *state = (struct private_data *)self->data;
	ssize_t ret;

	if (state->eof) {
		*p = NULL;
		return (0);
	}

	__archive_read_filter_consume(self->upstream, state->unconsumed);
	state->unconsumed = 0;

	switch (state->stage) {
	case SELECT_STREAM:
		break;
	case READ_DEFAULT_STREAM:
	case READ_LEGACY_STREAM:
		/* Reading a lz4 stream already failed. */
		archive_set_error(&self->archive->archive,
		    ARCHIVE_ERRNO_MISC, "Invalid sequence.");
		return (ARCHIVE_FATAL);
	case READ_DEFAULT_BLOCK:
		ret = lz4_filter_read_default_stream(self, p);
		if (ret != 0 || state->stage != SELECT_STREAM)
			return ret;
		break;
	case READ_LEGACY_BLOCK:
		ret = lz4_filter_read_legacy_stream(self, p);
		if (ret != 0 || state->stage != SELECT_STREAM)
			return ret;
		break;
	default:
		archive_set_error(&self->archive->archive,
		    ARCHIVE_ERRNO_MISC, "Program error.");
		return (ARCHIVE_FATAL);
		break;
	}

	while (state->stage == SELECT_STREAM) {
		const char *read_buf;

		/* Read a magic number. */
		read_buf = __archive_read_filter_ahead(self->upstream, 4,
				NULL);
		if (read_buf == NULL) {
			state->eof = 1;
			*p = NULL;
			return (0);
		}
		uint32_t number = archive_le32dec(read_buf);
		__archive_read_filter_consume(self->upstream, 4);
		if (number == LZ4_MAGICNUMBER)
			return lz4_filter_read_default_stream(self, p);
		else if (number == LZ4_LEGACY)
			return lz4_filter_read_legacy_stream(self, p);
		else if ((number & ~0xF) == LZ4_SKIPPABLED) {
			read_buf = __archive_read_filter_ahead(
				self->upstream, 4, NULL);
			if (read_buf == NULL) {
				archive_set_error(
				    &self->archive->archive,
		    		    ARCHIVE_ERRNO_MISC,
				    "Malformed lz4 data");
				return (ARCHIVE_FATAL);
			}
			uint32_t skip_bytes = archive_le32dec(read_buf);
			__archive_read_filter_consume(self->upstream,
				4 + skip_bytes);
		} else {
			/* Ignore following unrecognized data. */
			state->eof = 1;
			*p = NULL;
			return (0);
		}
	}
	state->eof = 1;
	*p = NULL;
	return (0);
}

static int
lz4_filter_read_descriptor(struct archive_read_filter *self)
{
	struct private_data *state = (struct private_data *)self->data;
	const char *read_buf;
	ssize_t bytes_remaining;
	ssize_t descriptor_bytes;
	unsigned char flag, bd;
	unsigned int chsum, chsum_verifier;

	/* Make sure we have 2 bytes for flags. */
	read_buf = __archive_read_filter_ahead(self->upstream, 2,
	    &bytes_remaining);
	if (read_buf == NULL) {
		archive_set_error(&self->archive->archive,
		    ARCHIVE_ERRNO_MISC,
		    "truncated lz4 input");
		return (ARCHIVE_FATAL);
	}

	/*
	   Parse flags.
	 */
	flag = (unsigned char)read_buf[0];
	/* Verify version number. */
	if ((flag & 0xc0) != 1<<6)
		goto malformed_error;
	/* A reserved bit must be zero. */
	if (flag & 0x02)
		goto malformed_error;
	state->flags.block_independence = (flag & 0x20) != 0;
	state->flags.block_checksum = (flag & 0x10)?4:0;
	state->flags.stream_size = (flag & 0x08) != 0;
	state->flags.stream_checksum = (flag & 0x04) != 0;
	state->flags.preset_dictionary = (flag & 0x01) != 0;

	/* BD */
	bd = (unsigned char)read_buf[1];
	/* Reserved bits must be zero. */
	if (bd & 0x8f)
		goto malformed_error;
	/* Get a maximum block size. */
	switch (read_buf[1] >> 4) {
	case 4: /* 64 KB */
		state->flags.block_maximum_size = 64 * 1024;
		break;
	case 5: /* 256 KB */
		state->flags.block_maximum_size = 256 * 1024;
		break;
	case 6: /* 1 MB */
		state->flags.block_maximum_size = 1024 * 1024;
		break;
	case 7: /* 4 MB */
		state->flags.block_maximum_size = 4 * 1024 * 1024;
		break;
	default:
		goto malformed_error;
	}

	/* Read the whole descriptor in a stream block. */
	descriptor_bytes = 3;
	if (state->flags.stream_size)
		descriptor_bytes += 8;
	if (state->flags.preset_dictionary)
		descriptor_bytes += 4;
	if (bytes_remaining < descriptor_bytes) {
		read_buf = __archive_read_filter_ahead(self->upstream,
		    descriptor_bytes, &bytes_remaining);
		if (read_buf == NULL) {
			archive_set_error(&self->archive->archive,
			    ARCHIVE_ERRNO_MISC,
			    "truncated lz4 input");
			return (ARCHIVE_FATAL);
		}
	}
	/* Check if a descriptor is corrupted */
	chsum = __archive_xxhash.XXH32(read_buf, (int)descriptor_bytes -1, 0);
	chsum = (chsum >> 8) & 0xff;
	chsum_verifier = read_buf[descriptor_bytes-1] & 0xff;
	if (chsum != chsum_verifier)
		goto malformed_error;

	__archive_read_filter_consume(self->upstream, descriptor_bytes);

	/* Make sure we have an enough buffer for uncompressed data. */
	if (lz4_allocate_out_block(self) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);
	if (state->flags.stream_checksum)
		state->xxh32_state = __archive_xxhash.XXH32_init(0);

	state->decoded_size = 0;
	/* Success */
	return (ARCHIVE_OK);
malformed_error:
	archive_set_error(&self->archive->archive, ARCHIVE_ERRNO_MISC,
	    "malformed lz4 data");
	return (ARCHIVE_FATAL);
}

static ssize_t
lz4_filter_read_data_block(struct archive_read_filter *self, const void **p)
{
	struct private_data *state = (struct private_data *)self->data;
	ssize_t compressed_size;
	const char *read_buf;
	ssize_t bytes_remaining;
	int checksum_size;
	ssize_t uncompressed_size;
	size_t prefix64k;

	*p = NULL;

	/* Make sure we have 4 bytes for a block size. */
	read_buf = __archive_read_filter_ahead(self->upstream, 4,
	    &bytes_remaining);
	if (read_buf == NULL)
		goto truncated_error;
	compressed_size = archive_le32dec(read_buf);
	if ((compressed_size & 0x7fffffff) > state->flags.block_maximum_size)
		goto malformed_error;
	/* A compressed size == 0 means the end of stream blocks. */
	if (compressed_size == 0) {
		__archive_read_filter_consume(self->upstream, 4);
		return 0;
	}

	checksum_size = state->flags.block_checksum;
	/* Check if the block is uncompressed. */
	if (compressed_size & 0x80000000U) {
		compressed_size &= 0x7fffffff;
		uncompressed_size = compressed_size;
	} else
		uncompressed_size = 0;/* Unknown yet. */

	/*
	  Unfortunately, lz4 decompression API requires a whole block
	  for its decompression speed, so we read a whole block and allocate
	  a huge buffer used for decoded data.
	*/
	read_buf = __archive_read_filter_ahead(self->upstream,
	    4 + compressed_size + checksum_size, &bytes_remaining);
	if (read_buf == NULL)
		goto truncated_error;

	/* Optional process, checking a block sum. */
	if (checksum_size) {
		unsigned int chsum = __archive_xxhash.XXH32(
			read_buf + 4, (int)compressed_size, 0);
		unsigned int chsum_block =
		    archive_le32dec(read_buf + 4 + compressed_size);
		if (chsum != chsum_block)
			goto malformed_error;
	}


	/* If the block is uncompressed, there is nothing to do. */
	if (uncompressed_size) {
		/* Prepare a prefix 64k block for next block. */
		if (!state->flags.block_independence) {
			prefix64k = 64 * 1024;
			if (uncompressed_size < (ssize_t)prefix64k) {
				memcpy(state->out_block
					+ prefix64k - uncompressed_size,
				    read_buf + 4,
				    uncompressed_size);
				memset(state->out_block, 0,
				    prefix64k - uncompressed_size);
			} else {
				memcpy(state->out_block,
				    read_buf + 4
					+ uncompressed_size - prefix64k,
				    prefix64k);
			}
			state->decoded_size = 0;
		}
		state->unconsumed = 4 + uncompressed_size + checksum_size;
		*p = read_buf + 4;
		return uncompressed_size;
	}

	/*
	   Decompress a block data.
	 */
	if (state->flags.block_independence) {
		prefix64k = 0;
		uncompressed_size = LZ4_decompress_safe(read_buf + 4,
		    state->out_block, (int)compressed_size,
		    state->flags.block_maximum_size);
	} else {
		prefix64k = 64 * 1024;
		if (state->decoded_size) {
			if (state->decoded_size < prefix64k) {
				memmove(state->out_block
					+ prefix64k - state->decoded_size,
				    state->out_block + prefix64k,
				    state->decoded_size);
				memset(state->out_block, 0,
				    prefix64k - state->decoded_size);
			} else {
				memmove(state->out_block,
				    state->out_block + state->decoded_size,
				    prefix64k);
			}
		}
#if LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 7
		uncompressed_size = LZ4_decompress_safe_usingDict(
		    read_buf + 4,
		    state->out_block + prefix64k, (int)compressed_size,
		    state->flags.block_maximum_size,
		    state->out_block,
		    prefix64k);
#else
		uncompressed_size = LZ4_decompress_safe_withPrefix64k(
		    read_buf + 4,
		    state->out_block + prefix64k, (int)compressed_size,
		    state->flags.block_maximum_size);
#endif
	}

	/* Check if an error occurred in the decompression process. */
	if (uncompressed_size < 0) {
		archive_set_error(&(self->archive->archive),
		    ARCHIVE_ERRNO_MISC, "lz4 decompression failed");
		return (ARCHIVE_FATAL);
	}

	state->unconsumed = 4 + compressed_size + checksum_size;
	*p = state->out_block + prefix64k;
	state->decoded_size = uncompressed_size;
	return uncompressed_size;

malformed_error:
	archive_set_error(&self->archive->archive, ARCHIVE_ERRNO_MISC,
	    "malformed lz4 data");
	return (ARCHIVE_FATAL);
truncated_error:
	archive_set_error(&self->archive->archive, ARCHIVE_ERRNO_MISC,
	    "truncated lz4 input");
	return (ARCHIVE_FATAL);
}

static ssize_t
lz4_filter_read_default_stream(struct archive_read_filter *self, const void **p)
{
	struct private_data *state = (struct private_data *)self->data;
	const char *read_buf;
	ssize_t bytes_remaining;
	ssize_t ret;

	if (state->stage == SELECT_STREAM) {
		state->stage = READ_DEFAULT_STREAM;
		/* First, read a descriptor. */
		if((ret = lz4_filter_read_descriptor(self)) != ARCHIVE_OK)
			return (ret);
		state->stage = READ_DEFAULT_BLOCK;
	}
	/* Decompress a block. */
	ret = lz4_filter_read_data_block(self, p);

	/* If the end of block is detected, change the filter status
	   to read next stream. */
	if (ret == 0 && *p == NULL)
		state->stage = SELECT_STREAM;

	/* Optional process, checking a stream sum. */
	if (state->flags.stream_checksum) {
		if (state->stage == SELECT_STREAM) {
			unsigned int checksum;
			unsigned int checksum_stream;
			read_buf = __archive_read_filter_ahead(self->upstream,
			    4, &bytes_remaining);
			if (read_buf == NULL) {
				archive_set_error(&self->archive->archive,
				    ARCHIVE_ERRNO_MISC, "truncated lz4 input");
				return (ARCHIVE_FATAL);
			}
			checksum = archive_le32dec(read_buf);
			__archive_read_filter_consume(self->upstream, 4);
			checksum_stream = __archive_xxhash.XXH32_digest(
			    state->xxh32_state);
			state->xxh32_state = NULL;
			if (checksum != checksum_stream) {
				archive_set_error(&self->archive->archive,
				    ARCHIVE_ERRNO_MISC,
				    "lz4 stream cheksum error");
				return (ARCHIVE_FATAL);
			}
		} else if (ret > 0)
			__archive_xxhash.XXH32_update(state->xxh32_state,
			    *p, (int)ret);
	}
	return (ret);
}

static ssize_t
lz4_filter_read_legacy_stream(struct archive_read_filter *self, const void **p)
{
	struct private_data *state = (struct private_data *)self->data;
	int compressed;
	const char *read_buf;
	ssize_t ret;

	*p = NULL;
	ret = lz4_allocate_out_block_for_legacy(self);
	if (ret != ARCHIVE_OK)
		return ret;

	/* Make sure we have 4 bytes for a block size. */
	read_buf = __archive_read_filter_ahead(self->upstream, 4, NULL);
	if (read_buf == NULL) {
		if (state->stage == SELECT_STREAM) {
			state->stage = READ_LEGACY_STREAM;
			archive_set_error(&self->archive->archive,
			    ARCHIVE_ERRNO_MISC,
			    "truncated lz4 input");
			return (ARCHIVE_FATAL);
		}
		state->stage = SELECT_STREAM;
		return 0;
	}
	state->stage = READ_LEGACY_BLOCK;
	compressed = archive_le32dec(read_buf);
	if (compressed > LZ4_COMPRESSBOUND(LEGACY_BLOCK_SIZE)) {
		state->stage = SELECT_STREAM;
		return 0;
	}

	/* Make sure we have a whole block. */
	read_buf = __archive_read_filter_ahead(self->upstream,
	    4 + compressed, NULL);
	if (read_buf == NULL) {
		archive_set_error(&(self->archive->archive),
		    ARCHIVE_ERRNO_MISC, "truncated lz4 input");
		return (ARCHIVE_FATAL);
	}
	ret = LZ4_decompress_safe(read_buf + 4, state->out_block,
	    compressed, (int)state->out_block_size);
	if (ret < 0) {
		archive_set_error(&(self->archive->archive),
		    ARCHIVE_ERRNO_MISC, "lz4 decompression failed");
		return (ARCHIVE_FATAL);
	}
	*p = state->out_block;
	state->unconsumed = 4 + compressed;
	return ret;
}

/*
 * Clean up the decompressor.
 */
static int
lz4_filter_close(struct archive_read_filter *self)
{
	struct private_data *state;
	int ret = ARCHIVE_OK;

	state = (struct private_data *)self->data;
	free(state->xxh32_state);
	free(state->out_block);
	free(state);
	return (ret);
}

#endif /* HAVE_LIBLZ4 */
