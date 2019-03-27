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
#ifdef HAVE_LZ4_H
#include <lz4.h>
#endif
#ifdef HAVE_LZ4HC_H
#include <lz4hc.h>
#endif

#include "archive.h"
#include "archive_endian.h"
#include "archive_private.h"
#include "archive_write_private.h"
#include "archive_xxhash.h"

#define LZ4_MAGICNUMBER	0x184d2204

struct private_data {
	int		 compression_level;
	unsigned	 header_written:1;
	unsigned	 version_number:1;
	unsigned	 block_independence:1;
	unsigned	 block_checksum:1;
	unsigned	 stream_size:1;
	unsigned	 stream_checksum:1;
	unsigned	 preset_dictionary:1;
	unsigned	 block_maximum_size:3;
#if defined(HAVE_LIBLZ4) && LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 2
	int64_t		 total_in;
	char		*out;
	char		*out_buffer;
	size_t		 out_buffer_size;
	size_t		 out_block_size;
	char		*in;
	char		*in_buffer_allocated;
	char		*in_buffer;
	size_t		 in_buffer_size;
	size_t		 block_size;

	void		*xxh32_state;
	void		*lz4_stream;
#else
	struct archive_write_program_data *pdata;
#endif
};

static int archive_filter_lz4_close(struct archive_write_filter *);
static int archive_filter_lz4_free(struct archive_write_filter *);
static int archive_filter_lz4_open(struct archive_write_filter *);
static int archive_filter_lz4_options(struct archive_write_filter *,
		    const char *, const char *);
static int archive_filter_lz4_write(struct archive_write_filter *,
		    const void *, size_t);

/*
 * Add a lz4 compression filter to this write handle.
 */
int
archive_write_add_filter_lz4(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;
	struct archive_write_filter *f = __archive_write_allocate_filter(_a);
	struct private_data *data;

	archive_check_magic(&a->archive, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_add_filter_lz4");

	data = calloc(1, sizeof(*data));
	if (data == NULL) {
		archive_set_error(&a->archive, ENOMEM, "Out of memory");
		return (ARCHIVE_FATAL);
	}

	/*
	 * Setup default settings.
	 */
	data->compression_level = 1;
	data->version_number = 0x01;
	data->block_independence = 1;
	data->block_checksum = 0;
	data->stream_size = 0;
	data->stream_checksum = 1;
	data->preset_dictionary = 0;
	data->block_maximum_size = 7;

	/*
	 * Setup a filter setting.
	 */
	f->data = data;
	f->options = &archive_filter_lz4_options;
	f->close = &archive_filter_lz4_close;
	f->free = &archive_filter_lz4_free;
	f->open = &archive_filter_lz4_open;
	f->code = ARCHIVE_FILTER_LZ4;
	f->name = "lz4";
#if defined(HAVE_LIBLZ4) && LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 2
	return (ARCHIVE_OK);
#else
	/*
	 * We don't have lz4 library, and execute external lz4 program
	 * instead.
	 */
	data->pdata = __archive_write_program_allocate("lz4");
	if (data->pdata == NULL) {
		free(data);
		archive_set_error(&a->archive, ENOMEM, "Out of memory");
		return (ARCHIVE_FATAL);
	}
	data->compression_level = 0;
	archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
	    "Using external lz4 program");
	return (ARCHIVE_WARN);
#endif
}

/*
 * Set write options.
 */
static int
archive_filter_lz4_options(struct archive_write_filter *f,
    const char *key, const char *value)
{
	struct private_data *data = (struct private_data *)f->data;

	if (strcmp(key, "compression-level") == 0) {
		int val;
		if (value == NULL || !((val = value[0] - '0') >= 1 && val <= 9) ||
		    value[1] != '\0')
			return (ARCHIVE_WARN);

#ifndef HAVE_LZ4HC_H
		if(val >= 3)
		{
			archive_set_error(f->archive, ARCHIVE_ERRNO_PROGRAMMER,
				"High compression not included in this build");
			return (ARCHIVE_FATAL);
		}
#endif
		data->compression_level = val;
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "stream-checksum") == 0) {
		data->stream_checksum = value != NULL;
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "block-checksum") == 0) {
		data->block_checksum = value != NULL;
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "block-size") == 0) {
		if (value == NULL || !(value[0] >= '4' && value[0] <= '7') ||
		    value[1] != '\0')
			return (ARCHIVE_WARN);
		data->block_maximum_size = value[0] - '0';
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "block-dependence") == 0) {
		data->block_independence = value == NULL;
		return (ARCHIVE_OK);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

#if defined(HAVE_LIBLZ4) && LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 2
/* Don't compile this if we don't have liblz4. */

static int drive_compressor(struct archive_write_filter *, const char *,
    size_t);
static int drive_compressor_independence(struct archive_write_filter *,
    const char *, size_t);
static int drive_compressor_dependence(struct archive_write_filter *,
    const char *, size_t);
static int lz4_write_stream_descriptor(struct archive_write_filter *);
static ssize_t lz4_write_one_block(struct archive_write_filter *, const char *,
    size_t);


/*
 * Setup callback.
 */
static int
archive_filter_lz4_open(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;
	int ret;
	size_t required_size;
	static size_t const bkmap[] = { 64 * 1024, 256 * 1024, 1 * 1024 * 1024,
			   4 * 1024 * 1024 };
	size_t pre_block_size;

	ret = __archive_write_open_filter(f->next_filter);
	if (ret != 0)
		return (ret);

	if (data->block_maximum_size < 4)
		data->block_size = bkmap[0];
	else
		data->block_size = bkmap[data->block_maximum_size - 4];

	required_size = 4 + 15 + 4 + data->block_size + 4 + 4;
	if (data->out_buffer_size < required_size) {
		size_t bs = required_size, bpb;
		free(data->out_buffer);
		if (f->archive->magic == ARCHIVE_WRITE_MAGIC) {
			/* Buffer size should be a multiple number of
			 * the of bytes per block for performance. */
			bpb = archive_write_get_bytes_per_block(f->archive);
			if (bpb > bs)
				bs = bpb;
			else if (bpb != 0) {
				bs += bpb;
				bs -= bs % bpb;
			}
		}
		data->out_block_size = bs;
		bs += required_size;
		data->out_buffer = malloc(bs);
		data->out = data->out_buffer;
		data->out_buffer_size = bs;
	}

	pre_block_size = (data->block_independence)? 0: 64 * 1024;
	if (data->in_buffer_size < data->block_size + pre_block_size) {
		free(data->in_buffer_allocated);
		data->in_buffer_size = data->block_size;
		data->in_buffer_allocated =
		    malloc(data->in_buffer_size + pre_block_size);
		data->in_buffer = data->in_buffer_allocated + pre_block_size;
		if (!data->block_independence && data->compression_level >= 3)
		    data->in_buffer = data->in_buffer_allocated;
		data->in = data->in_buffer;
		data->in_buffer_size = data->block_size;
	}

	if (data->out_buffer == NULL || data->in_buffer_allocated == NULL) {
		archive_set_error(f->archive, ENOMEM,
		    "Can't allocate data for compression buffer");
		return (ARCHIVE_FATAL);
	}

	f->write = archive_filter_lz4_write;

	return (ARCHIVE_OK);
}

/*
 * Write data to the out stream.
 *
 * Returns ARCHIVE_OK if all data written, error otherwise.
 */
static int
archive_filter_lz4_write(struct archive_write_filter *f,
    const void *buff, size_t length)
{
	struct private_data *data = (struct private_data *)f->data;
	int ret = ARCHIVE_OK;
	const char *p;
	size_t remaining;
	ssize_t size;

	/* If we haven't written a stream descriptor, we have to do it first. */
	if (!data->header_written) {
		ret = lz4_write_stream_descriptor(f);
		if (ret != ARCHIVE_OK)
			return (ret);
		data->header_written = 1;
	}

	/* Update statistics */
	data->total_in += length;

	p = (const char *)buff;
	remaining = length;
	while (remaining) {
		size_t l;
		/* Compress input data to output buffer */
		size = lz4_write_one_block(f, p, remaining);
		if (size < ARCHIVE_OK)
			return (ARCHIVE_FATAL);
		l = data->out - data->out_buffer;
		if (l >= data->out_block_size) {
			ret = __archive_write_filter(f->next_filter,
			    data->out_buffer, data->out_block_size);
			l -= data->out_block_size;
			memcpy(data->out_buffer,
			    data->out_buffer + data->out_block_size, l);
			data->out = data->out_buffer + l;
			if (ret < ARCHIVE_WARN)
				break;
		}
		p += size;
		remaining -= size;
	}

	return (ret);
}

/*
 * Finish the compression.
 */
static int
archive_filter_lz4_close(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;
	int ret, r1;

	/* Finish compression cycle. */
	ret = (int)lz4_write_one_block(f, NULL, 0);
	if (ret >= 0) {
		/*
		 * Write the last block and the end of the stream data.
		 */

		/* Write End Of Stream. */
		memset(data->out, 0, 4); data->out += 4;
		/* Write Stream checksum if needed. */
		if (data->stream_checksum) {
			unsigned int checksum;
			checksum = __archive_xxhash.XXH32_digest(
					data->xxh32_state);
			data->xxh32_state = NULL;
			archive_le32enc(data->out, checksum);
			data->out += 4;
		}
		ret = __archive_write_filter(f->next_filter,
			    data->out_buffer, data->out - data->out_buffer);
	}

	r1 = __archive_write_close_filter(f->next_filter);
	return (r1 < ret ? r1 : ret);
}

static int
archive_filter_lz4_free(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;

	if (data->lz4_stream != NULL) {
#ifdef HAVE_LZ4HC_H
		if (data->compression_level >= 3)
#if LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 7
			LZ4_freeStreamHC(data->lz4_stream);
#else
			LZ4_freeHC(data->lz4_stream);
#endif
		else
#endif
#if LZ4_VERSION_MINOR >= 3
			LZ4_freeStream(data->lz4_stream);
#else
			LZ4_free(data->lz4_stream);
#endif
	}
	free(data->out_buffer);
	free(data->in_buffer_allocated);
	free(data->xxh32_state);
	free(data);
	f->data = NULL;
	return (ARCHIVE_OK);
}

static int
lz4_write_stream_descriptor(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;
	uint8_t *sd;

	sd = (uint8_t *)data->out;
	/* Write Magic Number. */
	archive_le32enc(&sd[0], LZ4_MAGICNUMBER);
	/* FLG */
	sd[4] = (data->version_number << 6)
	      | (data->block_independence << 5)
	      | (data->block_checksum << 4)
	      | (data->stream_size << 3)
	      | (data->stream_checksum << 2)
	      | (data->preset_dictionary << 0);
	/* BD */
	sd[5] = (data->block_maximum_size << 4);
	sd[6] = (__archive_xxhash.XXH32(&sd[4], 2, 0) >> 8) & 0xff;
	data->out += 7;
	if (data->stream_checksum)
		data->xxh32_state = __archive_xxhash.XXH32_init(0);
	else
		data->xxh32_state = NULL;
	return (ARCHIVE_OK);
}

static ssize_t
lz4_write_one_block(struct archive_write_filter *f, const char *p,
    size_t length)
{
	struct private_data *data = (struct private_data *)f->data;
	ssize_t r;

	if (p == NULL) {
		/* Compress remaining uncompressed data. */
		if (data->in_buffer == data->in)
			return 0;
		else {
			size_t l = data->in - data->in_buffer;
			r = drive_compressor(f, data->in_buffer, l);
			if (r == ARCHIVE_OK)
				r = (ssize_t)l;
		}
	} else if ((data->block_independence || data->compression_level < 3) &&
	    data->in_buffer == data->in && length >= data->block_size) {
		r = drive_compressor(f, p, data->block_size);
		if (r == ARCHIVE_OK)
			r = (ssize_t)data->block_size;
	} else {
		size_t remaining_size = data->in_buffer_size -
			(data->in - data->in_buffer);
		size_t l = (remaining_size > length)? length: remaining_size;
		memcpy(data->in, p, l);
		data->in += l;
		if (l == remaining_size) {
			r = drive_compressor(f, data->in_buffer,
			    data->block_size);
			if (r == ARCHIVE_OK)
				r = (ssize_t)l;
			data->in = data->in_buffer;
		} else
			r = (ssize_t)l;
	}

	return (r);
}


/*
 * Utility function to push input data through compressor, writing
 * full output blocks as necessary.
 *
 * Note that this handles both the regular write case (finishing ==
 * false) and the end-of-archive case (finishing == true).
 */
static int
drive_compressor(struct archive_write_filter *f, const char *p, size_t length)
{
	struct private_data *data = (struct private_data *)f->data;

	if (data->stream_checksum)
		__archive_xxhash.XXH32_update(data->xxh32_state,
			p, (int)length);
	if (data->block_independence)
		return drive_compressor_independence(f, p, length);
	else
		return drive_compressor_dependence(f, p, length);
}

static int
drive_compressor_independence(struct archive_write_filter *f, const char *p,
    size_t length)
{
	struct private_data *data = (struct private_data *)f->data;
	unsigned int outsize;

#ifdef HAVE_LZ4HC_H
	if (data->compression_level >= 3)
#if LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 7
		outsize = LZ4_compress_HC(p, data->out + 4,
		     (int)length, (int)data->block_size,
		    data->compression_level);
#else
		outsize = LZ4_compressHC2_limitedOutput(p, data->out + 4,
		    (int)length, (int)data->block_size,
		    data->compression_level);
#endif
	else
#endif
#if LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 7
		outsize = LZ4_compress_default(p, data->out + 4,
		    (int)length, (int)data->block_size);
#else
		outsize = LZ4_compress_limitedOutput(p, data->out + 4,
		    (int)length, (int)data->block_size);
#endif

	if (outsize) {
		/* The buffer is compressed. */
		archive_le32enc(data->out, outsize);
		data->out += 4;
	} else {
		/* The buffer is not compressed. The compressed size was
		 * bigger than its uncompressed size. */
		archive_le32enc(data->out, length | 0x80000000);
		data->out += 4;
		memcpy(data->out, p, length);
		outsize = length;
	}
	data->out += outsize;
	if (data->block_checksum) {
		unsigned int checksum =
		    __archive_xxhash.XXH32(data->out - outsize, outsize, 0);
		archive_le32enc(data->out, checksum);
		data->out += 4;
	}
	return (ARCHIVE_OK);
}

static int
drive_compressor_dependence(struct archive_write_filter *f, const char *p,
    size_t length)
{
	struct private_data *data = (struct private_data *)f->data;
	int outsize;

#define DICT_SIZE	(64 * 1024)
#ifdef HAVE_LZ4HC_H
	if (data->compression_level >= 3) {
		if (data->lz4_stream == NULL) {
#if LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 7
			data->lz4_stream = LZ4_createStreamHC();
			LZ4_resetStreamHC(data->lz4_stream, data->compression_level);
#else
			data->lz4_stream =
			    LZ4_createHC(data->in_buffer_allocated);
#endif
			if (data->lz4_stream == NULL) {
				archive_set_error(f->archive, ENOMEM,
				    "Can't allocate data for compression"
				    " buffer");
				return (ARCHIVE_FATAL);
			}
		}
		else
			LZ4_loadDictHC(data->lz4_stream, data->in_buffer_allocated, DICT_SIZE);

#if LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 7
		outsize = LZ4_compress_HC_continue(
		    data->lz4_stream, p, data->out + 4, (int)length,
		    (int)data->block_size);
#else
		outsize = LZ4_compressHC2_limitedOutput_continue(
		    data->lz4_stream, p, data->out + 4, (int)length,
		    (int)data->block_size, data->compression_level);
#endif
	} else
#endif
	{
		if (data->lz4_stream == NULL) {
			data->lz4_stream = LZ4_createStream();
			if (data->lz4_stream == NULL) {
				archive_set_error(f->archive, ENOMEM,
				    "Can't allocate data for compression"
				    " buffer");
				return (ARCHIVE_FATAL);
			}
		}
		else
			LZ4_loadDict(data->lz4_stream, data->in_buffer_allocated, DICT_SIZE);

#if LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 7
		outsize = LZ4_compress_fast_continue(
		    data->lz4_stream, p, data->out + 4, (int)length,
		    (int)data->block_size, 1);
#else
		outsize = LZ4_compress_limitedOutput_continue(
		    data->lz4_stream, p, data->out + 4, (int)length,
		    (int)data->block_size);
#endif
	}

	if (outsize) {
		/* The buffer is compressed. */
		archive_le32enc(data->out, outsize);
		data->out += 4;
	} else {
		/* The buffer is not compressed. The compressed size was
		 * bigger than its uncompressed size. */
		archive_le32enc(data->out, length | 0x80000000);
		data->out += 4;
		memcpy(data->out, p, length);
		outsize = length;
	}
	data->out += outsize;
	if (data->block_checksum) {
		unsigned int checksum =
		    __archive_xxhash.XXH32(data->out - outsize, outsize, 0);
		archive_le32enc(data->out, checksum);
		data->out += 4;
	}

	if (length == data->block_size) {
#ifdef HAVE_LZ4HC_H
		if (data->compression_level >= 3) {
#if LZ4_VERSION_MAJOR >= 1 && LZ4_VERSION_MINOR >= 7
			LZ4_saveDictHC(data->lz4_stream, data->in_buffer_allocated, DICT_SIZE);
#else
			LZ4_slideInputBufferHC(data->lz4_stream);
#endif
			data->in_buffer = data->in_buffer_allocated + DICT_SIZE;
		}
		else
#endif
			LZ4_saveDict(data->lz4_stream,
			    data->in_buffer_allocated, DICT_SIZE);
#undef DICT_SIZE
	}
	return (ARCHIVE_OK);
}

#else /* HAVE_LIBLZ4 */

static int
archive_filter_lz4_open(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;
	struct archive_string as;
	int r;

	archive_string_init(&as);
	archive_strcpy(&as, "lz4 -z -q -q");

	/* Specify a compression level. */
	if (data->compression_level > 0) {
		archive_strcat(&as, " -");
		archive_strappend_char(&as, '0' + data->compression_level);
	}
	/* Specify a block size. */
	archive_strcat(&as, " -B");
	archive_strappend_char(&as, '0' + data->block_maximum_size);

	if (data->block_checksum)
		archive_strcat(&as, " -BX");
	if (data->stream_checksum == 0)
		archive_strcat(&as, " --no-frame-crc");
	if (data->block_independence == 0)
		archive_strcat(&as, " -BD");

	f->write = archive_filter_lz4_write;

	r = __archive_write_program_open(f, data->pdata, as.s);
	archive_string_free(&as);
	return (r);
}

static int
archive_filter_lz4_write(struct archive_write_filter *f, const void *buff,
    size_t length)
{
	struct private_data *data = (struct private_data *)f->data;

	return __archive_write_program_write(f, data->pdata, buff, length);
}

static int
archive_filter_lz4_close(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;

	return __archive_write_program_close(f, data->pdata);
}

static int
archive_filter_lz4_free(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;

	__archive_write_program_free(data->pdata);
	free(data);
	return (ARCHIVE_OK);
}

#endif /* HAVE_LIBLZ4 */
