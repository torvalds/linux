/*-
 * Copyright (c) 2003-2010 Tim Kientzle
 * Copyright (c) 2009-2012 Michihiro NAKAJIMA
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

__FBSDID("$FreeBSD: head/lib/libarchive/archive_write_set_compression_xz.c 201108 2009-12-28 03:28:21Z kientzle $");

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <time.h>
#ifdef HAVE_LZMA_H
#include <lzma.h>
#endif

#include "archive.h"
#include "archive_endian.h"
#include "archive_private.h"
#include "archive_write_private.h"

#if ARCHIVE_VERSION_NUMBER < 4000000
int
archive_write_set_compression_lzip(struct archive *a)
{
	__archive_write_filters_free(a);
	return (archive_write_add_filter_lzip(a));
}

int
archive_write_set_compression_lzma(struct archive *a)
{
	__archive_write_filters_free(a);
	return (archive_write_add_filter_lzma(a));
}

int
archive_write_set_compression_xz(struct archive *a)
{
	__archive_write_filters_free(a);
	return (archive_write_add_filter_xz(a));
}

#endif

#ifndef HAVE_LZMA_H
int
archive_write_add_filter_xz(struct archive *a)
{
	archive_set_error(a, ARCHIVE_ERRNO_MISC,
	    "xz compression not supported on this platform");
	return (ARCHIVE_FATAL);
}

int
archive_write_add_filter_lzma(struct archive *a)
{
	archive_set_error(a, ARCHIVE_ERRNO_MISC,
	    "lzma compression not supported on this platform");
	return (ARCHIVE_FATAL);
}

int
archive_write_add_filter_lzip(struct archive *a)
{
	archive_set_error(a, ARCHIVE_ERRNO_MISC,
	    "lzma compression not supported on this platform");
	return (ARCHIVE_FATAL);
}
#else
/* Don't compile this if we don't have liblzma. */

struct private_data {
	int		 compression_level;
	uint32_t	 threads;
	lzma_stream	 stream;
	lzma_filter	 lzmafilters[2];
	lzma_options_lzma lzma_opt;
	int64_t		 total_in;
	unsigned char	*compressed;
	size_t		 compressed_buffer_size;
	int64_t		 total_out;
	/* the CRC32 value of uncompressed data for lzip */
	uint32_t	 crc32;
};

static int	archive_compressor_xz_options(struct archive_write_filter *,
		    const char *, const char *);
static int	archive_compressor_xz_open(struct archive_write_filter *);
static int	archive_compressor_xz_write(struct archive_write_filter *,
		    const void *, size_t);
static int	archive_compressor_xz_close(struct archive_write_filter *);
static int	archive_compressor_xz_free(struct archive_write_filter *);
static int	drive_compressor(struct archive_write_filter *,
		    struct private_data *, int finishing);

struct option_value {
	uint32_t dict_size;
	uint32_t nice_len;
	lzma_match_finder mf;
};
static const struct option_value option_values[] = {
	{ 1 << 16, 32, LZMA_MF_HC3},
	{ 1 << 20, 32, LZMA_MF_HC3},
	{ 3 << 19, 32, LZMA_MF_HC4},
	{ 1 << 21, 32, LZMA_MF_BT4},
	{ 3 << 20, 32, LZMA_MF_BT4},
	{ 1 << 22, 32, LZMA_MF_BT4},
	{ 1 << 23, 64, LZMA_MF_BT4},
	{ 1 << 24, 64, LZMA_MF_BT4},
	{ 3 << 23, 64, LZMA_MF_BT4},
	{ 1 << 25, 64, LZMA_MF_BT4}
};

static int
common_setup(struct archive_write_filter *f)
{
	struct private_data *data;
	struct archive_write *a = (struct archive_write *)f->archive;
	data = calloc(1, sizeof(*data));
	if (data == NULL) {
		archive_set_error(&a->archive, ENOMEM, "Out of memory");
		return (ARCHIVE_FATAL);
	}
	f->data = data;
	data->compression_level = LZMA_PRESET_DEFAULT;
	data->threads = 1;
	f->open = &archive_compressor_xz_open;
	f->close = archive_compressor_xz_close;
	f->free = archive_compressor_xz_free;
	f->options = &archive_compressor_xz_options;
	return (ARCHIVE_OK);
}

/*
 * Add an xz compression filter to this write handle.
 */
int
archive_write_add_filter_xz(struct archive *_a)
{
	struct archive_write_filter *f;
	int r;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_add_filter_xz");
	f = __archive_write_allocate_filter(_a);
	r = common_setup(f);
	if (r == ARCHIVE_OK) {
		f->code = ARCHIVE_FILTER_XZ;
		f->name = "xz";
	}
	return (r);
}

/* LZMA is handled identically, we just need a different compression
 * code set.  (The liblzma setup looks at the code to determine
 * the one place that XZ and LZMA require different handling.) */
int
archive_write_add_filter_lzma(struct archive *_a)
{
	struct archive_write_filter *f;
	int r;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_add_filter_lzma");
	f = __archive_write_allocate_filter(_a);
	r = common_setup(f);
	if (r == ARCHIVE_OK) {
		f->code = ARCHIVE_FILTER_LZMA;
		f->name = "lzma";
	}
	return (r);
}

int
archive_write_add_filter_lzip(struct archive *_a)
{
	struct archive_write_filter *f;
	int r;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_add_filter_lzip");
	f = __archive_write_allocate_filter(_a);
	r = common_setup(f);
	if (r == ARCHIVE_OK) {
		f->code = ARCHIVE_FILTER_LZIP;
		f->name = "lzip";
	}
	return (r);
}

static int
archive_compressor_xz_init_stream(struct archive_write_filter *f,
    struct private_data *data)
{
	static const lzma_stream lzma_stream_init_data = LZMA_STREAM_INIT;
	int ret;
#ifdef HAVE_LZMA_STREAM_ENCODER_MT
	lzma_mt mt_options;
#endif

	data->stream = lzma_stream_init_data;
	data->stream.next_out = data->compressed;
	data->stream.avail_out = data->compressed_buffer_size;
	if (f->code == ARCHIVE_FILTER_XZ) {
#ifdef HAVE_LZMA_STREAM_ENCODER_MT
		if (data->threads != 1) {
			memset(&mt_options, 0, sizeof(mt_options));
			mt_options.threads = data->threads;
			mt_options.timeout = 300;
			mt_options.filters = data->lzmafilters;
			mt_options.check = LZMA_CHECK_CRC64;
			ret = lzma_stream_encoder_mt(&(data->stream),
			    &mt_options);
		} else
#endif
			ret = lzma_stream_encoder(&(data->stream),
			    data->lzmafilters, LZMA_CHECK_CRC64);
	} else if (f->code == ARCHIVE_FILTER_LZMA) {
		ret = lzma_alone_encoder(&(data->stream), &data->lzma_opt);
	} else {	/* ARCHIVE_FILTER_LZIP */
		int dict_size = data->lzma_opt.dict_size;
		int ds, log2dic, wedges;

		/* Calculate a coded dictionary size */
		if (dict_size < (1 << 12) || dict_size > (1 << 27)) {
			archive_set_error(f->archive, ARCHIVE_ERRNO_MISC,
			    "Unacceptable dictionary size for lzip: %d",
			    dict_size);
			return (ARCHIVE_FATAL);
		}
		for (log2dic = 27; log2dic >= 12; log2dic--) {
			if (dict_size & (1 << log2dic))
				break;
		}
		if (dict_size > (1 << log2dic)) {
			log2dic++;
			wedges =
			    ((1 << log2dic) - dict_size) / (1 << (log2dic - 4));
		} else
			wedges = 0;
		ds = ((wedges << 5) & 0xe0) | (log2dic & 0x1f);

		data->crc32 = 0;
		/* Make a header */
		data->compressed[0] = 0x4C;
		data->compressed[1] = 0x5A;
		data->compressed[2] = 0x49;
		data->compressed[3] = 0x50;
		data->compressed[4] = 1;/* Version */
		data->compressed[5] = (unsigned char)ds;
		data->stream.next_out += 6;
		data->stream.avail_out -= 6;

		ret = lzma_raw_encoder(&(data->stream), data->lzmafilters);
	}
	if (ret == LZMA_OK)
		return (ARCHIVE_OK);

	switch (ret) {
	case LZMA_MEM_ERROR:
		archive_set_error(f->archive, ENOMEM,
		    "Internal error initializing compression library: "
		    "Cannot allocate memory");
		break;
	default:
		archive_set_error(f->archive, ARCHIVE_ERRNO_MISC,
		    "Internal error initializing compression library: "
		    "It's a bug in liblzma");
		break;
	}
	return (ARCHIVE_FATAL);
}

/*
 * Setup callback.
 */
static int
archive_compressor_xz_open(struct archive_write_filter *f)
{
	struct private_data *data = f->data;
	int ret;

	ret = __archive_write_open_filter(f->next_filter);
	if (ret != ARCHIVE_OK)
		return (ret);

	if (data->compressed == NULL) {
		size_t bs = 65536, bpb;
		if (f->archive->magic == ARCHIVE_WRITE_MAGIC) {
			/* Buffer size should be a multiple number of the of bytes
			 * per block for performance. */
			bpb = archive_write_get_bytes_per_block(f->archive);
			if (bpb > bs)
				bs = bpb;
			else if (bpb != 0)
				bs -= bs % bpb;
		}
		data->compressed_buffer_size = bs;
		data->compressed
		    = (unsigned char *)malloc(data->compressed_buffer_size);
		if (data->compressed == NULL) {
			archive_set_error(f->archive, ENOMEM,
			    "Can't allocate data for compression buffer");
			return (ARCHIVE_FATAL);
		}
	}

	f->write = archive_compressor_xz_write;

	/* Initialize compression library. */
	if (f->code == ARCHIVE_FILTER_LZIP) {
		const struct option_value *val =
		    &option_values[data->compression_level];

		data->lzma_opt.dict_size = val->dict_size;
		data->lzma_opt.preset_dict = NULL;
		data->lzma_opt.preset_dict_size = 0;
		data->lzma_opt.lc = LZMA_LC_DEFAULT;
		data->lzma_opt.lp = LZMA_LP_DEFAULT;
		data->lzma_opt.pb = LZMA_PB_DEFAULT;
		data->lzma_opt.mode =
		    data->compression_level<= 2? LZMA_MODE_FAST:LZMA_MODE_NORMAL;
		data->lzma_opt.nice_len = val->nice_len;
		data->lzma_opt.mf = val->mf;
		data->lzma_opt.depth = 0;
		data->lzmafilters[0].id = LZMA_FILTER_LZMA1;
		data->lzmafilters[0].options = &data->lzma_opt;
		data->lzmafilters[1].id = LZMA_VLI_UNKNOWN;/* Terminate */
	} else {
		if (lzma_lzma_preset(&data->lzma_opt, data->compression_level)) {
			archive_set_error(f->archive, ARCHIVE_ERRNO_MISC,
			    "Internal error initializing compression library");
		}
		data->lzmafilters[0].id = LZMA_FILTER_LZMA2;
		data->lzmafilters[0].options = &data->lzma_opt;
		data->lzmafilters[1].id = LZMA_VLI_UNKNOWN;/* Terminate */
	}
	ret = archive_compressor_xz_init_stream(f, data);
	if (ret == LZMA_OK) {
		f->data = data;
		return (0);
	}
	return (ARCHIVE_FATAL);
}

/*
 * Set write options.
 */
static int
archive_compressor_xz_options(struct archive_write_filter *f,
    const char *key, const char *value)
{
	struct private_data *data = (struct private_data *)f->data;

	if (strcmp(key, "compression-level") == 0) {
		if (value == NULL || !(value[0] >= '0' && value[0] <= '9') ||
		    value[1] != '\0')
			return (ARCHIVE_WARN);
		data->compression_level = value[0] - '0';
		if (data->compression_level > 6)
			data->compression_level = 6;
		return (ARCHIVE_OK);
	} else if (strcmp(key, "threads") == 0) {
		if (value == NULL)
			return (ARCHIVE_WARN);
		data->threads = (int)strtoul(value, NULL, 10);
		if (data->threads == 0 && errno != 0) {
			data->threads = 1;
			return (ARCHIVE_WARN);
		}
		if (data->threads == 0) {
#ifdef HAVE_LZMA_STREAM_ENCODER_MT
			data->threads = lzma_cputhreads();
#else
			data->threads = 1;
#endif
		}
		return (ARCHIVE_OK);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

/*
 * Write data to the compressed stream.
 */
static int
archive_compressor_xz_write(struct archive_write_filter *f,
    const void *buff, size_t length)
{
	struct private_data *data = (struct private_data *)f->data;
	int ret;

	/* Update statistics */
	data->total_in += length;
	if (f->code == ARCHIVE_FILTER_LZIP)
		data->crc32 = lzma_crc32(buff, length, data->crc32);

	/* Compress input data to output buffer */
	data->stream.next_in = buff;
	data->stream.avail_in = length;
	if ((ret = drive_compressor(f, data, 0)) != ARCHIVE_OK)
		return (ret);

	return (ARCHIVE_OK);
}


/*
 * Finish the compression...
 */
static int
archive_compressor_xz_close(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;
	int ret, r1;

	ret = drive_compressor(f, data, 1);
	if (ret == ARCHIVE_OK) {
		data->total_out +=
		    data->compressed_buffer_size - data->stream.avail_out;
		ret = __archive_write_filter(f->next_filter,
		    data->compressed,
		    data->compressed_buffer_size - data->stream.avail_out);
		if (f->code == ARCHIVE_FILTER_LZIP && ret == ARCHIVE_OK) {
			archive_le32enc(data->compressed, data->crc32);
			archive_le64enc(data->compressed+4, data->total_in);
			archive_le64enc(data->compressed+12, data->total_out + 20);
			ret = __archive_write_filter(f->next_filter,
			    data->compressed, 20);
		}
	}
	lzma_end(&(data->stream));
	r1 = __archive_write_close_filter(f->next_filter);
	return (r1 < ret ? r1 : ret);
}

static int
archive_compressor_xz_free(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;
	free(data->compressed);
	free(data);
	f->data = NULL;
	return (ARCHIVE_OK);
}

/*
 * Utility function to push input data through compressor,
 * writing full output blocks as necessary.
 *
 * Note that this handles both the regular write case (finishing ==
 * false) and the end-of-archive case (finishing == true).
 */
static int
drive_compressor(struct archive_write_filter *f,
    struct private_data *data, int finishing)
{
	int ret;

	for (;;) {
		if (data->stream.avail_out == 0) {
			data->total_out += data->compressed_buffer_size;
			ret = __archive_write_filter(f->next_filter,
			    data->compressed,
			    data->compressed_buffer_size);
			if (ret != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
			data->stream.next_out = data->compressed;
			data->stream.avail_out = data->compressed_buffer_size;
		}

		/* If there's nothing to do, we're done. */
		if (!finishing && data->stream.avail_in == 0)
			return (ARCHIVE_OK);

		ret = lzma_code(&(data->stream),
		    finishing ? LZMA_FINISH : LZMA_RUN );

		switch (ret) {
		case LZMA_OK:
			/* In non-finishing case, check if compressor
			 * consumed everything */
			if (!finishing && data->stream.avail_in == 0)
				return (ARCHIVE_OK);
			/* In finishing case, this return always means
			 * there's more work */
			break;
		case LZMA_STREAM_END:
			/* This return can only occur in finishing case. */
			if (finishing)
				return (ARCHIVE_OK);
			archive_set_error(f->archive, ARCHIVE_ERRNO_MISC,
			    "lzma compression data error");
			return (ARCHIVE_FATAL);
		case LZMA_MEMLIMIT_ERROR:
			archive_set_error(f->archive, ENOMEM,
			    "lzma compression error: "
			    "%ju MiB would have been needed",
			    (uintmax_t)((lzma_memusage(&(data->stream))
				    + 1024 * 1024 -1)
				/ (1024 * 1024)));
			return (ARCHIVE_FATAL);
		default:
			/* Any other return value indicates an error. */
			archive_set_error(f->archive, ARCHIVE_ERRNO_MISC,
			    "lzma compression failed:"
			    " lzma_code() call returned status %d",
			    ret);
			return (ARCHIVE_FATAL);
		}
	}
}

#endif /* HAVE_LZMA_H */
