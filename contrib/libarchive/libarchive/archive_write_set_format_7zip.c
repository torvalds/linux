/*-
 * Copyright (c) 2011-2012 Michihiro NAKAJIMA
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
#include <stdlib.h>
#ifdef HAVE_BZLIB_H
#include <bzlib.h>
#endif
#if HAVE_LZMA_H
#include <lzma.h>
#endif
#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#include "archive.h"
#ifndef HAVE_ZLIB_H
#include "archive_crc32.h"
#endif
#include "archive_endian.h"
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_ppmd7_private.h"
#include "archive_private.h"
#include "archive_rb.h"
#include "archive_string.h"
#include "archive_write_private.h"

/*
 * Codec ID
 */
#define _7Z_COPY	0
#define _7Z_LZMA1	0x030101
#define _7Z_LZMA2	0x21
#define _7Z_DEFLATE	0x040108
#define _7Z_BZIP2	0x040202
#define _7Z_PPMD	0x030401

/*
 * 7-Zip header property IDs.
 */
#define kEnd			0x00
#define kHeader			0x01
#define kArchiveProperties	0x02
#define kAdditionalStreamsInfo	0x03
#define kMainStreamsInfo	0x04
#define kFilesInfo		0x05
#define kPackInfo		0x06
#define kUnPackInfo		0x07
#define kSubStreamsInfo		0x08
#define kSize			0x09
#define kCRC			0x0A
#define kFolder			0x0B
#define kCodersUnPackSize	0x0C
#define kNumUnPackStream	0x0D
#define kEmptyStream		0x0E
#define kEmptyFile		0x0F
#define kAnti			0x10
#define kName			0x11
#define kCTime			0x12
#define kATime			0x13
#define kMTime			0x14
#define kAttributes		0x15
#define kEncodedHeader		0x17

enum la_zaction {
	ARCHIVE_Z_FINISH,
	ARCHIVE_Z_RUN
};

/*
 * A stream object of universal compressor.
 */
struct la_zstream {
	const uint8_t		*next_in;
	size_t			 avail_in;
	uint64_t		 total_in;

	uint8_t			*next_out;
	size_t			 avail_out;
	uint64_t		 total_out;

	uint32_t		 prop_size;
	uint8_t			*props;

	int			 valid;
	void			*real_stream;
	int			 (*code) (struct archive *a,
				    struct la_zstream *lastrm,
				    enum la_zaction action);
	int			 (*end)(struct archive *a,
				    struct la_zstream *lastrm);
};

#define PPMD7_DEFAULT_ORDER	6
#define PPMD7_DEFAULT_MEM_SIZE	(1 << 24)

struct ppmd_stream {
	int			 stat;
	CPpmd7			 ppmd7_context;
	CPpmd7z_RangeEnc	 range_enc;
	IByteOut		 byteout;
	uint8_t			*buff;
	uint8_t			*buff_ptr;
	uint8_t			*buff_end;
	size_t			 buff_bytes;
};

struct coder {
	unsigned		 codec;
	size_t			 prop_size;
	uint8_t			*props;
};

struct file {
	struct archive_rb_node	 rbnode;

	struct file		*next;
	unsigned		 name_len;
	uint8_t			*utf16name;/* UTF16-LE name. */
	uint64_t		 size;
	unsigned		 flg;
#define MTIME_IS_SET	(1<<0)
#define ATIME_IS_SET	(1<<1)
#define CTIME_IS_SET	(1<<2)
#define CRC32_IS_SET	(1<<3)
#define HAS_STREAM	(1<<4)

	struct {
		time_t		 time;
		long		 time_ns;
	}			 times[3];
#define MTIME 0
#define ATIME 1
#define CTIME 2

	mode_t			 mode;
	uint32_t		 crc32;

	int			 dir:1;
};

struct _7zip {
	int			 temp_fd;
	uint64_t		 temp_offset;

	struct file		*cur_file;
	size_t			 total_number_entry;
	size_t			 total_number_nonempty_entry;
	size_t			 total_number_empty_entry;
	size_t			 total_number_dir_entry;
	size_t			 total_bytes_entry_name;
	size_t			 total_number_time_defined[3];
	uint64_t		 total_bytes_compressed;
	uint64_t		 total_bytes_uncompressed;
	uint64_t		 entry_bytes_remaining;
	uint32_t		 entry_crc32;
	uint32_t		 precode_crc32;
	uint32_t		 encoded_crc32;
	int			 crc32flg;
#define	PRECODE_CRC32	1
#define	ENCODED_CRC32	2

	unsigned		 opt_compression;
	int			 opt_compression_level;

	struct la_zstream	 stream;
	struct coder		 coder;

	struct archive_string_conv *sconv;

	/*
	 * Compressed data buffer.
	 */
	unsigned char		 wbuff[512 * 20 * 6];
	size_t			 wbuff_remaining;

	/*
	 * The list of the file entries which has its contents is used to
	 * manage struct file objects.
	 * We use 'next' (a member of struct file) to chain.
	 */
	struct {
		struct file	*first;
		struct file	**last;
	}			 file_list, empty_list;
	struct archive_rb_tree	 rbtree;/* for empty files */
};

static int	_7z_options(struct archive_write *,
		    const char *, const char *);
static int	_7z_write_header(struct archive_write *,
		    struct archive_entry *);
static ssize_t	_7z_write_data(struct archive_write *,
		    const void *, size_t);
static int	_7z_finish_entry(struct archive_write *);
static int	_7z_close(struct archive_write *);
static int	_7z_free(struct archive_write *);
static int	file_cmp_node(const struct archive_rb_node *,
		    const struct archive_rb_node *);
static int	file_cmp_key(const struct archive_rb_node *, const void *);
static int	file_new(struct archive_write *a, struct archive_entry *,
		    struct file **);
static void	file_free(struct file *);
static void	file_register(struct _7zip *, struct file *);
static void	file_register_empty(struct _7zip *, struct file *);
static void	file_init_register(struct _7zip *);
static void	file_init_register_empty(struct _7zip *);
static void	file_free_register(struct _7zip *);
static ssize_t	compress_out(struct archive_write *, const void *, size_t ,
		    enum la_zaction);
static int	compression_init_encoder_copy(struct archive *,
		    struct la_zstream *);
static int	compression_code_copy(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end_copy(struct archive *, struct la_zstream *);
static int	compression_init_encoder_deflate(struct archive *,
		    struct la_zstream *, int, int);
#ifdef HAVE_ZLIB_H
static int	compression_code_deflate(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end_deflate(struct archive *, struct la_zstream *);
#endif
static int	compression_init_encoder_bzip2(struct archive *,
		    struct la_zstream *, int);
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
static int	compression_code_bzip2(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end_bzip2(struct archive *, struct la_zstream *);
#endif
static int	compression_init_encoder_lzma1(struct archive *,
		    struct la_zstream *, int);
static int	compression_init_encoder_lzma2(struct archive *,
		    struct la_zstream *, int);
#if defined(HAVE_LZMA_H)
static int	compression_code_lzma(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end_lzma(struct archive *, struct la_zstream *);
#endif
static int	compression_init_encoder_ppmd(struct archive *,
		    struct la_zstream *, unsigned, uint32_t);
static int	compression_code_ppmd(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end_ppmd(struct archive *, struct la_zstream *);
static int	_7z_compression_init_encoder(struct archive_write *, unsigned,
		    int);
static int	compression_code(struct archive *,
		    struct la_zstream *, enum la_zaction);
static int	compression_end(struct archive *,
		    struct la_zstream *);
static int	enc_uint64(struct archive_write *, uint64_t);
static int	make_header(struct archive_write *, uint64_t, uint64_t,
		    uint64_t, int, struct coder *);
static int	make_streamsInfo(struct archive_write *, uint64_t, uint64_t,
		    	uint64_t, int, struct coder *, int, uint32_t);

int
archive_write_set_format_7zip(struct archive *_a)
{
	static const struct archive_rb_tree_ops rb_ops = {
		file_cmp_node, file_cmp_key
	};
	struct archive_write *a = (struct archive_write *)_a;
	struct _7zip *zip;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_set_format_7zip");

	/* If another format was already registered, unregister it. */
	if (a->format_free != NULL)
		(a->format_free)(a);

	zip = calloc(1, sizeof(*zip));
	if (zip == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate 7-Zip data");
		return (ARCHIVE_FATAL);
	}
	zip->temp_fd = -1;
	__archive_rb_tree_init(&(zip->rbtree), &rb_ops);
	file_init_register(zip);
	file_init_register_empty(zip);

	/* Set default compression type and its level. */
#if HAVE_LZMA_H
	zip->opt_compression = _7Z_LZMA1;
#elif defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
	zip->opt_compression = _7Z_BZIP2;
#elif defined(HAVE_ZLIB_H)
	zip->opt_compression = _7Z_DEFLATE;
#else
	zip->opt_compression = _7Z_COPY;
#endif
	zip->opt_compression_level = 6;

	a->format_data = zip;

	a->format_name = "7zip";
	a->format_options = _7z_options;
	a->format_write_header = _7z_write_header;
	a->format_write_data = _7z_write_data;
	a->format_finish_entry = _7z_finish_entry;
	a->format_close = _7z_close;
	a->format_free = _7z_free;
	a->archive.archive_format = ARCHIVE_FORMAT_7ZIP;
	a->archive.archive_format_name = "7zip";

	return (ARCHIVE_OK);
}

static int
_7z_options(struct archive_write *a, const char *key, const char *value)
{
	struct _7zip *zip;

	zip = (struct _7zip *)a->format_data;

	if (strcmp(key, "compression") == 0) {
		const char *name = NULL;

		if (value == NULL || strcmp(value, "copy") == 0 ||
		    strcmp(value, "COPY") == 0 ||
		    strcmp(value, "store") == 0 ||
		    strcmp(value, "STORE") == 0)
			zip->opt_compression = _7Z_COPY;
		else if (strcmp(value, "deflate") == 0 ||
		    strcmp(value, "DEFLATE") == 0)
#if HAVE_ZLIB_H
			zip->opt_compression = _7Z_DEFLATE;
#else
			name = "deflate";
#endif
		else if (strcmp(value, "bzip2") == 0 ||
		    strcmp(value, "BZIP2") == 0)
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
			zip->opt_compression = _7Z_BZIP2;
#else
			name = "bzip2";
#endif
		else if (strcmp(value, "lzma1") == 0 ||
		    strcmp(value, "LZMA1") == 0)
#if HAVE_LZMA_H
			zip->opt_compression = _7Z_LZMA1;
#else
			name = "lzma1";
#endif
		else if (strcmp(value, "lzma2") == 0 ||
		    strcmp(value, "LZMA2") == 0)
#if HAVE_LZMA_H
			zip->opt_compression = _7Z_LZMA2;
#else
			name = "lzma2";
#endif
		else if (strcmp(value, "ppmd") == 0 ||
		    strcmp(value, "PPMD") == 0 ||
		    strcmp(value, "PPMd") == 0)
			zip->opt_compression = _7Z_PPMD;
		else {
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "Unknown compression name: `%s'",
			    value);
			return (ARCHIVE_FAILED);
		}
		if (name != NULL) {
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "`%s' compression not supported "
			    "on this platform",
			    name);
			return (ARCHIVE_FAILED);
		}
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "compression-level") == 0) {
		if (value == NULL ||
		    !(value[0] >= '0' && value[0] <= '9') ||
		    value[1] != '\0') {
			archive_set_error(&(a->archive),
			    ARCHIVE_ERRNO_MISC,
			    "Illegal value `%s'",
			    value);
			return (ARCHIVE_FAILED);
		}
		zip->opt_compression_level = value[0] - '0';
		return (ARCHIVE_OK);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

static int
_7z_write_header(struct archive_write *a, struct archive_entry *entry)
{
	struct _7zip *zip;
	struct file *file;
	int r;

	zip = (struct _7zip *)a->format_data;
	zip->cur_file = NULL;
	zip->entry_bytes_remaining = 0;

	if (zip->sconv == NULL) {
		zip->sconv = archive_string_conversion_to_charset(
		    &a->archive, "UTF-16LE", 1);
		if (zip->sconv == NULL)
			return (ARCHIVE_FATAL);
	}

	r = file_new(a, entry, &file);
	if (r < ARCHIVE_WARN) {
		if (file != NULL)
			file_free(file);
		return (r);
	}
	if (file->size == 0 && file->dir) {
		if (!__archive_rb_tree_insert_node(&(zip->rbtree),
		    (struct archive_rb_node *)file)) {
			/* We have already had the same file. */
			file_free(file);
			return (ARCHIVE_OK);
		}
	}

	if (file->flg & MTIME_IS_SET)
		zip->total_number_time_defined[MTIME]++;
	if (file->flg & CTIME_IS_SET)
		zip->total_number_time_defined[CTIME]++;
	if (file->flg & ATIME_IS_SET)
		zip->total_number_time_defined[ATIME]++;

	zip->total_number_entry++;
	zip->total_bytes_entry_name += file->name_len + 2;
	if (file->size == 0) {
		/* Count up the number of empty files. */
		zip->total_number_empty_entry++;
		if (file->dir)
			zip->total_number_dir_entry++;
		else
			file_register_empty(zip, file);
		return (r);
	}

	/*
	 * Init compression.
	 */
	if ((zip->total_number_entry - zip->total_number_empty_entry) == 1) {
		r = _7z_compression_init_encoder(a, zip->opt_compression,
			zip->opt_compression_level);
		if (r < 0) {
			file_free(file);
			return (ARCHIVE_FATAL);
		}
	}

	/* Register a non-empty file. */
	file_register(zip, file);

	/*
	 * Set the current file to cur_file to read its contents.
	 */
	zip->cur_file = file;


	/* Save a offset of current file in temporary file. */
	zip->entry_bytes_remaining = file->size;
	zip->entry_crc32 = 0;

	/*
	 * Store a symbolic link name as file contents.
	 */
	if (archive_entry_filetype(entry) == AE_IFLNK) {
		ssize_t bytes;
		const void *p = (const void *)archive_entry_symlink(entry);
		bytes = compress_out(a, p, (size_t)file->size, ARCHIVE_Z_RUN);
		if (bytes < 0)
			return ((int)bytes);
		zip->entry_crc32 = crc32(zip->entry_crc32, p, (unsigned)bytes);
		zip->entry_bytes_remaining -= bytes;
	}

	return (r);
}

/*
 * Write data to a temporary file.
 */
static int
write_to_temp(struct archive_write *a, const void *buff, size_t s)
{
	struct _7zip *zip;
	const unsigned char *p;
	ssize_t ws;

	zip = (struct _7zip *)a->format_data;

	/*
	 * Open a temporary file.
	 */
	if (zip->temp_fd == -1) {
		zip->temp_offset = 0;
		zip->temp_fd = __archive_mktemp(NULL);
		if (zip->temp_fd < 0) {
			archive_set_error(&a->archive, errno,
			    "Couldn't create temporary file");
			return (ARCHIVE_FATAL);
		}
	}

	p = (const unsigned char *)buff;
	while (s) {
		ws = write(zip->temp_fd, p, s);
		if (ws < 0) {
			archive_set_error(&(a->archive), errno,
			    "fwrite function failed");
			return (ARCHIVE_FATAL);
		}
		s -= ws;
		p += ws;
		zip->temp_offset += ws;
	}
	return (ARCHIVE_OK);
}

static ssize_t
compress_out(struct archive_write *a, const void *buff, size_t s,
    enum la_zaction run)
{
	struct _7zip *zip = (struct _7zip *)a->format_data;
	int r;

	if (run == ARCHIVE_Z_FINISH && zip->stream.total_in == 0 && s == 0)
		return (0);

	if ((zip->crc32flg & PRECODE_CRC32) && s)
		zip->precode_crc32 = crc32(zip->precode_crc32, buff,
		    (unsigned)s);
	zip->stream.next_in = (const unsigned char *)buff;
	zip->stream.avail_in = s;
	for (;;) {
		/* Compress file data. */
		r = compression_code(&(a->archive), &(zip->stream), run);
		if (r != ARCHIVE_OK && r != ARCHIVE_EOF)
			return (ARCHIVE_FATAL);
		if (zip->stream.avail_out == 0) {
			if (write_to_temp(a, zip->wbuff, sizeof(zip->wbuff))
			    != ARCHIVE_OK)
				return (ARCHIVE_FATAL);
			zip->stream.next_out = zip->wbuff;
			zip->stream.avail_out = sizeof(zip->wbuff);
			if (zip->crc32flg & ENCODED_CRC32)
				zip->encoded_crc32 = crc32(zip->encoded_crc32,
				    zip->wbuff, sizeof(zip->wbuff));
			if (run == ARCHIVE_Z_FINISH && r != ARCHIVE_EOF)
				continue;
		}
		if (zip->stream.avail_in == 0)
			break;
	}
	if (run == ARCHIVE_Z_FINISH) {
		uint64_t bytes = sizeof(zip->wbuff) - zip->stream.avail_out;
		if (write_to_temp(a, zip->wbuff, (size_t)bytes) != ARCHIVE_OK)
			return (ARCHIVE_FATAL);
		if ((zip->crc32flg & ENCODED_CRC32) && bytes)
			zip->encoded_crc32 = crc32(zip->encoded_crc32,
			    zip->wbuff, (unsigned)bytes);
	}

	return (s);
}

static ssize_t
_7z_write_data(struct archive_write *a, const void *buff, size_t s)
{
	struct _7zip *zip;
	ssize_t bytes;

	zip = (struct _7zip *)a->format_data;

	if (s > zip->entry_bytes_remaining)
		s = (size_t)zip->entry_bytes_remaining;
	if (s == 0 || zip->cur_file == NULL)
		return (0);
	bytes = compress_out(a, buff, s, ARCHIVE_Z_RUN);
	if (bytes < 0)
		return (bytes);
	zip->entry_crc32 = crc32(zip->entry_crc32, buff, (unsigned)bytes);
	zip->entry_bytes_remaining -= bytes;
	return (bytes);
}

static int
_7z_finish_entry(struct archive_write *a)
{
	struct _7zip *zip;
	size_t s;
	ssize_t r;

	zip = (struct _7zip *)a->format_data;
	if (zip->cur_file == NULL)
		return (ARCHIVE_OK);

	while (zip->entry_bytes_remaining > 0) {
		s = (size_t)zip->entry_bytes_remaining;
		if (s > a->null_length)
			s = a->null_length;
		r = _7z_write_data(a, a->nulls, s);
		if (r < 0)
			return ((int)r);
	}
	zip->total_bytes_compressed += zip->stream.total_in;
	zip->total_bytes_uncompressed += zip->stream.total_out;
	zip->cur_file->crc32 = zip->entry_crc32;
	zip->cur_file = NULL;

	return (ARCHIVE_OK);
}

static int
flush_wbuff(struct archive_write *a)
{
	struct _7zip *zip;
	int r;
	size_t s;

	zip = (struct _7zip *)a->format_data;
	s = sizeof(zip->wbuff) - zip->wbuff_remaining;
	r = __archive_write_output(a, zip->wbuff, s);
	if (r != ARCHIVE_OK)
		return (r);
	zip->wbuff_remaining = sizeof(zip->wbuff);
	return (r);
}

static int
copy_out(struct archive_write *a, uint64_t offset, uint64_t length)
{
	struct _7zip *zip;
	int r;

	zip = (struct _7zip *)a->format_data;
	if (zip->temp_offset > 0 &&
	    lseek(zip->temp_fd, offset, SEEK_SET) < 0) {
		archive_set_error(&(a->archive), errno, "lseek failed");
		return (ARCHIVE_FATAL);
	}
	while (length) {
		size_t rsize;
		ssize_t rs;
		unsigned char *wb;

		if (length > zip->wbuff_remaining)
			rsize = zip->wbuff_remaining;
		else
			rsize = (size_t)length;
		wb = zip->wbuff + (sizeof(zip->wbuff) - zip->wbuff_remaining);
		rs = read(zip->temp_fd, wb, rsize);
		if (rs < 0) {
			archive_set_error(&(a->archive), errno,
			    "Can't read temporary file(%jd)",
			    (intmax_t)rs);
			return (ARCHIVE_FATAL);
		}
		if (rs == 0) {
			archive_set_error(&(a->archive), 0,
			    "Truncated 7-Zip archive");
			return (ARCHIVE_FATAL);
		}
		zip->wbuff_remaining -= rs;
		length -= rs;
		if (zip->wbuff_remaining == 0) {
			r = flush_wbuff(a);
			if (r != ARCHIVE_OK)
				return (r);
		}
	}
	return (ARCHIVE_OK);
}

static int
_7z_close(struct archive_write *a)
{
	struct _7zip *zip;
	unsigned char *wb;
	uint64_t header_offset, header_size, header_unpacksize;
	uint64_t length;
	uint32_t header_crc32;
	int r;

	zip = (struct _7zip *)a->format_data;

	if (zip->total_number_entry > 0) {
		struct archive_rb_node *n;
		uint64_t data_offset, data_size, data_unpacksize;
		unsigned header_compression;

		r = (int)compress_out(a, NULL, 0, ARCHIVE_Z_FINISH);
		if (r < 0)
			return (r);
		data_offset = 0;
		data_size = zip->stream.total_out;
		data_unpacksize = zip->stream.total_in;
		zip->coder.codec = zip->opt_compression;
		zip->coder.prop_size = zip->stream.prop_size;
		zip->coder.props = zip->stream.props;
		zip->stream.prop_size = 0;
		zip->stream.props = NULL;
		zip->total_number_nonempty_entry =
		    zip->total_number_entry - zip->total_number_empty_entry;

		/* Connect an empty file list. */
		if (zip->empty_list.first != NULL) {
			*zip->file_list.last = zip->empty_list.first;
			zip->file_list.last = zip->empty_list.last;
		}
		/* Connect a directory file list. */
		ARCHIVE_RB_TREE_FOREACH(n, &(zip->rbtree)) {
			file_register(zip, (struct file *)n);
		}

		/*
		 * NOTE: 7z command supports just LZMA1, LZMA2 and COPY for
		 * the compression type for encoding the header.
		 */
#if HAVE_LZMA_H
		header_compression = _7Z_LZMA1;
		/* If the stored file is only one, do not encode the header.
		 * This is the same way 7z command does. */
		if (zip->total_number_entry == 1)
			header_compression = _7Z_COPY;
#else
		header_compression = _7Z_COPY;
#endif
		r = _7z_compression_init_encoder(a, header_compression, 6);
		if (r < 0)
			return (r);
		zip->crc32flg = PRECODE_CRC32;
		zip->precode_crc32 = 0;
		r = make_header(a, data_offset, data_size, data_unpacksize,
			1, &(zip->coder));
		if (r < 0)
			return (r);
		r = (int)compress_out(a, NULL, 0, ARCHIVE_Z_FINISH);
		if (r < 0)
			return (r);
		header_offset = data_offset + data_size;
		header_size = zip->stream.total_out;
		header_crc32 = zip->precode_crc32;
		header_unpacksize = zip->stream.total_in;

		if (header_compression != _7Z_COPY) {
			/*
			 * Encode the header in order to reduce the size
			 * of the archive.
			 */
			free(zip->coder.props);
			zip->coder.codec = header_compression;
			zip->coder.prop_size = zip->stream.prop_size;
			zip->coder.props = zip->stream.props;
			zip->stream.prop_size = 0;
			zip->stream.props = NULL;

			r = _7z_compression_init_encoder(a, _7Z_COPY, 0);
			if (r < 0)
				return (r);
			zip->crc32flg = ENCODED_CRC32;
			zip->encoded_crc32 = 0;

			/*
			 * Make EncodedHeader.
			 */
			r = enc_uint64(a, kEncodedHeader);
			if (r < 0)
				return (r);
			r = make_streamsInfo(a, header_offset, header_size,
			      header_unpacksize, 1, &(zip->coder), 0,
			      header_crc32);
			if (r < 0)
				return (r);
			r = (int)compress_out(a, NULL, 0, ARCHIVE_Z_FINISH);
			if (r < 0)
				return (r);
			header_offset = header_offset + header_size;
			header_size = zip->stream.total_out;
			header_crc32 = zip->encoded_crc32;
		}
		zip->crc32flg = 0;
	} else {
		header_offset = header_size = 0;
		header_crc32 = 0;
	}
	
	length = zip->temp_offset;

	/*
	 * Make the zip header on wbuff(write buffer).
	 */
	wb = zip->wbuff;
	zip->wbuff_remaining = sizeof(zip->wbuff);
	memcpy(&wb[0], "7z\xBC\xAF\x27\x1C", 6);
	wb[6] = 0;/* Major version. */
	wb[7] = 3;/* Minor version. */
	archive_le64enc(&wb[12], header_offset);/* Next Header Offset */
	archive_le64enc(&wb[20], header_size);/* Next Header Size */
	archive_le32enc(&wb[28], header_crc32);/* Next Header CRC */
	archive_le32enc(&wb[8], crc32(0, &wb[12], 20));/* Start Header CRC */
	zip->wbuff_remaining -= 32;

	/*
	 * Read all file contents and an encoded header from the temporary
	 * file and write out it.
	 */
	r = copy_out(a, 0, length);
	if (r != ARCHIVE_OK)
		return (r);
	r = flush_wbuff(a);
	return (r);
}

/*
 * Encode 64 bits value into 7-Zip's encoded UINT64 value.
 */
static int
enc_uint64(struct archive_write *a, uint64_t val)
{
	unsigned mask = 0x80;
	uint8_t numdata[9];
	int i;

	numdata[0] = 0;
	for (i = 1; i < (int)sizeof(numdata); i++) {
		if (val < mask) {
			numdata[0] |= (uint8_t)val;
			break;
		}
		numdata[i] = (uint8_t)val;
		val >>= 8;
		numdata[0] |= mask;
		mask >>= 1;
	}
	return ((int)compress_out(a, numdata, i, ARCHIVE_Z_RUN));
}

static int
make_substreamsInfo(struct archive_write *a, struct coder *coders)
{
	struct _7zip *zip = (struct _7zip *)a->format_data;
	struct file *file;
	int r;

	/*
	 * Make SubStreamsInfo.
	 */
	r = enc_uint64(a, kSubStreamsInfo);
	if (r < 0)
		return (r);

	if (zip->total_number_nonempty_entry > 1 && coders->codec != _7Z_COPY) {
		/*
		 * Make NumUnPackStream.
		 */
		r = enc_uint64(a, kNumUnPackStream);
		if (r < 0)
			return (r);

		/* Write numUnpackStreams */
		r = enc_uint64(a, zip->total_number_nonempty_entry);
		if (r < 0)
			return (r);

		/*
		 * Make kSize.
		 */
		r = enc_uint64(a, kSize);
		if (r < 0)
			return (r);
		file = zip->file_list.first;
		for (;file != NULL; file = file->next) {
			if (file->next == NULL ||
			    file->next->size == 0)
				break;
			r = enc_uint64(a, file->size);
			if (r < 0)
				return (r);
		}
	}

	/*
	 * Make CRC.
	 */
	r = enc_uint64(a, kCRC);
	if (r < 0)
		return (r);


	/* All are defined */
	r = enc_uint64(a, 1);
	if (r < 0)
		return (r);
	file = zip->file_list.first;
	for (;file != NULL; file = file->next) {
		uint8_t crc[4];
		if (file->size == 0)
			break;
		archive_le32enc(crc, file->crc32);
		r = (int)compress_out(a, crc, 4, ARCHIVE_Z_RUN);
		if (r < 0)
			return (r);
	}

	/* Write End. */
	r = enc_uint64(a, kEnd);
	if (r < 0)
		return (r);
	return (ARCHIVE_OK);
}

static int
make_streamsInfo(struct archive_write *a, uint64_t offset, uint64_t pack_size,
    uint64_t unpack_size, int num_coder, struct coder *coders, int substrm,
    uint32_t header_crc)
{
	struct _7zip *zip = (struct _7zip *)a->format_data;
	uint8_t codec_buff[8];
	int numFolders, fi;
	int codec_size;
	int i, r;

	if (coders->codec == _7Z_COPY)
		numFolders = (int)zip->total_number_nonempty_entry;
	else
		numFolders = 1;

	/*
	 * Make PackInfo.
	 */
	r = enc_uint64(a, kPackInfo);
	if (r < 0)
		return (r);

	/* Write PackPos. */
	r = enc_uint64(a, offset);
	if (r < 0)
		return (r);

	/* Write NumPackStreams. */
	r = enc_uint64(a, numFolders);
	if (r < 0)
		return (r);

	/* Make Size. */
	r = enc_uint64(a, kSize);
	if (r < 0)
		return (r);

	if (numFolders > 1) {
		struct file *file = zip->file_list.first;
		for (;file != NULL; file = file->next) {
			if (file->size == 0)
				break;
			r = enc_uint64(a, file->size);
			if (r < 0)
				return (r);
		}
	} else {
		/* Write size. */
		r = enc_uint64(a, pack_size);
		if (r < 0)
			return (r);
	}

	r = enc_uint64(a, kEnd);
	if (r < 0)
		return (r);

	/*
	 * Make UnPackInfo.
	 */
	r = enc_uint64(a, kUnPackInfo);
	if (r < 0)
		return (r);

	/*
	 * Make Folder.
	 */
	r = enc_uint64(a, kFolder);
	if (r < 0)
		return (r);

	/* Write NumFolders. */
	r = enc_uint64(a, numFolders);
	if (r < 0)
		return (r);

	/* Write External. */
	r = enc_uint64(a, 0);
	if (r < 0)
		return (r);

	for (fi = 0; fi < numFolders; fi++) {
		/* Write NumCoders. */
		r = enc_uint64(a, num_coder);
		if (r < 0)
			return (r);

		for (i = 0; i < num_coder; i++) {
			unsigned codec_id = coders[i].codec;

			/* Write Codec flag. */
			archive_be64enc(codec_buff, codec_id);
			for (codec_size = 8; codec_size > 0; codec_size--) {
				if (codec_buff[8 - codec_size])
					break;
			}
			if (codec_size == 0)
				codec_size = 1;
			if (coders[i].prop_size)
				r = enc_uint64(a, codec_size | 0x20);
			else
				r = enc_uint64(a, codec_size);
			if (r < 0)
				return (r);

			/* Write Codec ID. */
			codec_size &= 0x0f;
			r = (int)compress_out(a, &codec_buff[8-codec_size],
				codec_size, ARCHIVE_Z_RUN);
			if (r < 0)
				return (r);

			if (coders[i].prop_size) {
				/* Write Codec property size. */
				r = enc_uint64(a, coders[i].prop_size);
				if (r < 0)
					return (r);

				/* Write Codec properties. */
				r = (int)compress_out(a, coders[i].props,
					coders[i].prop_size, ARCHIVE_Z_RUN);
				if (r < 0)
					return (r);
			}
		}
	}

	/*
	 * Make CodersUnPackSize.
	 */
	r = enc_uint64(a, kCodersUnPackSize);
	if (r < 0)
		return (r);

	if (numFolders > 1) {
		struct file *file = zip->file_list.first;
		for (;file != NULL; file = file->next) {
			if (file->size == 0)
				break;
			r = enc_uint64(a, file->size);
			if (r < 0)
				return (r);
		}

	} else {
		/* Write UnPackSize. */
		r = enc_uint64(a, unpack_size);
		if (r < 0)
			return (r);
	}

	if (!substrm) {
		uint8_t crc[4];
		/*
		 * Make CRC.
		 */
		r = enc_uint64(a, kCRC);
		if (r < 0)
			return (r);

		/* All are defined */
		r = enc_uint64(a, 1);
		if (r < 0)
			return (r);
		archive_le32enc(crc, header_crc);
		r = (int)compress_out(a, crc, 4, ARCHIVE_Z_RUN);
		if (r < 0)
			return (r);
	}

	/* Write End. */
	r = enc_uint64(a, kEnd);
	if (r < 0)
		return (r);

	if (substrm) {
		/*
		 * Make SubStreamsInfo.
		 */
		r = make_substreamsInfo(a, coders);
		if (r < 0)
			return (r);
	}


	/* Write End. */
	r = enc_uint64(a, kEnd);
	if (r < 0)
		return (r);

	return (ARCHIVE_OK);
}


#define EPOC_TIME ARCHIVE_LITERAL_ULL(116444736000000000)
static uint64_t
utcToFiletime(time_t t, long ns)
{
	uint64_t fileTime;

	fileTime = t;
	fileTime *= 10000000;
	fileTime += ns / 100;
	fileTime += EPOC_TIME;
	return (fileTime);
}

static int
make_time(struct archive_write *a, uint8_t type, unsigned flg, int ti)
{
	uint8_t filetime[8];
	struct _7zip *zip = (struct _7zip *)a->format_data;
	struct file *file;
	int r;
	uint8_t b, mask;

	/*
	 * Make Time Bools.
	 */
	if (zip->total_number_time_defined[ti] == zip->total_number_entry) {
		/* Write Time Type. */
		r = enc_uint64(a, type);
		if (r < 0)
			return (r);
		/* Write EmptyStream Size. */
		r = enc_uint64(a, 2 + zip->total_number_entry * 8);
		if (r < 0)
			return (r);
		/* All are defined. */
		r = enc_uint64(a, 1);
		if (r < 0)
			return (r);
	} else {
		if (zip->total_number_time_defined[ti] == 0)
			return (ARCHIVE_OK);

		/* Write Time Type. */
		r = enc_uint64(a, type);
		if (r < 0)
			return (r);
		/* Write EmptyStream Size. */
		r = enc_uint64(a, 2 + ((zip->total_number_entry + 7) >> 3)
			+ zip->total_number_time_defined[ti] * 8);
		if (r < 0)
			return (r);

		/* All are not defined. */
		r = enc_uint64(a, 0);
		if (r < 0)
			return (r);

		b = 0;
		mask = 0x80;
		file = zip->file_list.first;
		for (;file != NULL; file = file->next) {
			if (file->flg & flg)
				b |= mask;
			mask >>= 1;
			if (mask == 0) {
				r = (int)compress_out(a, &b, 1, ARCHIVE_Z_RUN);
				if (r < 0)
					return (r);
				mask = 0x80;
				b = 0;
			}
		}
		if (mask != 0x80) {
			r = (int)compress_out(a, &b, 1, ARCHIVE_Z_RUN);
			if (r < 0)
				return (r);
		}
	}

	/* External. */
	r = enc_uint64(a, 0);
	if (r < 0)
		return (r);


	/*
	 * Make Times.
	 */
	file = zip->file_list.first;
	for (;file != NULL; file = file->next) {
		if ((file->flg & flg) == 0)
			continue;
		archive_le64enc(filetime, utcToFiletime(file->times[ti].time,
			file->times[ti].time_ns));
		r = (int)compress_out(a, filetime, 8, ARCHIVE_Z_RUN);
		if (r < 0)
			return (r);
	}

	return (ARCHIVE_OK);
}

static int
make_header(struct archive_write *a, uint64_t offset, uint64_t pack_size,
    uint64_t unpack_size, int codernum, struct coder *coders)
{
	struct _7zip *zip = (struct _7zip *)a->format_data;
	struct file *file;
	int r;
	uint8_t b, mask;

	/*
	 * Make FilesInfo.
	 */
	r = enc_uint64(a, kHeader);
	if (r < 0)
		return (r);

	/*
	 * If there are empty files only, do not write MainStreamInfo.
	 */
	if (zip->total_number_nonempty_entry) {
		/*
		 * Make MainStreamInfo.
		 */
		r = enc_uint64(a, kMainStreamsInfo);
		if (r < 0)
			return (r);
		r = make_streamsInfo(a, offset, pack_size, unpack_size,
		      codernum, coders, 1, 0);
		if (r < 0)
			return (r);
	}

	/*
	 * Make FilesInfo.
	 */
	r = enc_uint64(a, kFilesInfo);
	if (r < 0)
		return (r);

	/* Write numFiles. */
	r = enc_uint64(a, zip->total_number_entry);
	if (r < 0)
		return (r);

	if (zip->total_number_empty_entry > 0) {
		/* Make EmptyStream. */
		r = enc_uint64(a, kEmptyStream);
		if (r < 0)
			return (r);

		/* Write EmptyStream Size. */
		r = enc_uint64(a, (zip->total_number_entry+7)>>3);
		if (r < 0)
			return (r);

		b = 0;
		mask = 0x80;
		file = zip->file_list.first;
		for (;file != NULL; file = file->next) {
			if (file->size == 0)
				b |= mask;
			mask >>= 1;
			if (mask == 0) {
				r = (int)compress_out(a, &b, 1, ARCHIVE_Z_RUN);
				if (r < 0)
					return (r);
				mask = 0x80;
				b = 0;
			}
		}
		if (mask != 0x80) {
			r = (int)compress_out(a, &b, 1, ARCHIVE_Z_RUN);
			if (r < 0)
				return (r);
		}
	}

	if (zip->total_number_empty_entry > zip->total_number_dir_entry) {
		/* Make EmptyFile. */
		r = enc_uint64(a, kEmptyFile);
		if (r < 0)
			return (r);

		/* Write EmptyFile Size. */
		r = enc_uint64(a, (zip->total_number_empty_entry + 7) >> 3);
		if (r < 0)
			return (r);

		b = 0;
		mask = 0x80;
		file = zip->file_list.first;
		for (;file != NULL; file = file->next) {
			if (file->size)
				continue;
			if (!file->dir)
				b |= mask;
			mask >>= 1;
			if (mask == 0) {
				r = (int)compress_out(a, &b, 1, ARCHIVE_Z_RUN);
				if (r < 0)
					return (r);
				mask = 0x80;
				b = 0;
			}
		}
		if (mask != 0x80) {
			r = (int)compress_out(a, &b, 1, ARCHIVE_Z_RUN);
			if (r < 0)
				return (r);
		}
	}

	/* Make Name. */
	r = enc_uint64(a, kName);
	if (r < 0)
		return (r);

	/* Write Name size. */
	r = enc_uint64(a, zip->total_bytes_entry_name+1);
	if (r < 0)
		return (r);

	/* Write dmy byte. */
	r = enc_uint64(a, 0);
	if (r < 0)
		return (r);

	file = zip->file_list.first;
	for (;file != NULL; file = file->next) {
		r = (int)compress_out(a, file->utf16name, file->name_len+2,
			ARCHIVE_Z_RUN);
		if (r < 0)
			return (r);
	}

	/* Make MTime. */
	r = make_time(a, kMTime, MTIME_IS_SET, MTIME);
	if (r < 0)
		return (r);

	/* Make CTime. */
	r = make_time(a, kCTime, CTIME_IS_SET, CTIME);
	if (r < 0)
		return (r);

	/* Make ATime. */
	r = make_time(a, kATime, ATIME_IS_SET, ATIME);
	if (r < 0)
		return (r);

	/* Make Attributes. */
	r = enc_uint64(a, kAttributes);
	if (r < 0)
		return (r);

	/* Write Attributes size. */
	r = enc_uint64(a, 2 + zip->total_number_entry * 4);
	if (r < 0)
		return (r);

	/* Write "All Are Defined". */
	r = enc_uint64(a, 1);
	if (r < 0)
		return (r);

	/* Write dmy byte. */
	r = enc_uint64(a, 0);
	if (r < 0)
		return (r);

	file = zip->file_list.first;
	for (;file != NULL; file = file->next) {
		/*
		 * High 16bits is unix mode.
		 * Low 16bits is Windows attributes.
		 */
		uint32_t encattr, attr;
		if (file->dir)
			attr = 0x8010;
		else
			attr = 0x8020;
		if ((file->mode & 0222) == 0)
			attr |= 1;/* Read Only. */
		attr |= ((uint32_t)file->mode) << 16;
		archive_le32enc(&encattr, attr);
		r = (int)compress_out(a, &encattr, 4, ARCHIVE_Z_RUN);
		if (r < 0)
			return (r);
	}

	/* Write End. */
	r = enc_uint64(a, kEnd);
	if (r < 0)
		return (r);

	/* Write End. */
	r = enc_uint64(a, kEnd);
	if (r < 0)
		return (r);

	return (ARCHIVE_OK);
}


static int
_7z_free(struct archive_write *a)
{
	struct _7zip *zip = (struct _7zip *)a->format_data;

	/* Close the temporary file. */
	if (zip->temp_fd >= 0)
		close(zip->temp_fd);

	file_free_register(zip);
	compression_end(&(a->archive), &(zip->stream));
	free(zip->coder.props);
	free(zip);

	return (ARCHIVE_OK);
}

static int
file_cmp_node(const struct archive_rb_node *n1,
    const struct archive_rb_node *n2)
{
	const struct file *f1 = (const struct file *)n1;
	const struct file *f2 = (const struct file *)n2;

	if (f1->name_len == f2->name_len)
		return (memcmp(f1->utf16name, f2->utf16name, f1->name_len));
	return (f1->name_len > f2->name_len)?1:-1;
}
        
static int
file_cmp_key(const struct archive_rb_node *n, const void *key)
{
	const struct file *f = (const struct file *)n;

	return (f->name_len - *(const char *)key);
}

static int
file_new(struct archive_write *a, struct archive_entry *entry,
    struct file **newfile)
{
	struct _7zip *zip;
	struct file *file;
	const char *u16;
	size_t u16len;
	int ret = ARCHIVE_OK;

	zip = (struct _7zip *)a->format_data;
	*newfile = NULL;

	file = calloc(1, sizeof(*file));
	if (file == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate memory");
		return (ARCHIVE_FATAL);
	}

	if (0 > archive_entry_pathname_l(entry, &u16, &u16len, zip->sconv)) {
		if (errno == ENOMEM) {
			free(file);
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory for UTF-16LE");
			return (ARCHIVE_FATAL);
		}
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "A filename cannot be converted to UTF-16LE;"
		    "You should disable making Joliet extension");
		ret = ARCHIVE_WARN;
	}
	file->utf16name = malloc(u16len + 2);
	if (file->utf16name == NULL) {
		free(file);
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate memory for Name");
		return (ARCHIVE_FATAL);
	}
	memcpy(file->utf16name, u16, u16len);
	file->utf16name[u16len+0] = 0;
	file->utf16name[u16len+1] = 0;
	file->name_len = (unsigned)u16len;
	file->mode = archive_entry_mode(entry);
	if (archive_entry_filetype(entry) == AE_IFREG)
		file->size = archive_entry_size(entry);
	else
		archive_entry_set_size(entry, 0);
	if (archive_entry_filetype(entry) == AE_IFDIR)
		file->dir = 1;
	else if (archive_entry_filetype(entry) == AE_IFLNK)
		file->size = strlen(archive_entry_symlink(entry));
	if (archive_entry_mtime_is_set(entry)) {
		file->flg |= MTIME_IS_SET;
		file->times[MTIME].time = archive_entry_mtime(entry);
		file->times[MTIME].time_ns = archive_entry_mtime_nsec(entry);
	}
	if (archive_entry_atime_is_set(entry)) {
		file->flg |= ATIME_IS_SET;
		file->times[ATIME].time = archive_entry_atime(entry);
		file->times[ATIME].time_ns = archive_entry_atime_nsec(entry);
	}
	if (archive_entry_ctime_is_set(entry)) {
		file->flg |= CTIME_IS_SET;
		file->times[CTIME].time = archive_entry_ctime(entry);
		file->times[CTIME].time_ns = archive_entry_ctime_nsec(entry);
	}

	*newfile = file;
	return (ret);
}

static void
file_free(struct file *file)
{
	free(file->utf16name);
	free(file);
}

static void
file_register(struct _7zip *zip, struct file *file)
{
	file->next = NULL;
	*zip->file_list.last = file;
	zip->file_list.last = &(file->next);
}

static void
file_init_register(struct _7zip *zip)
{
	zip->file_list.first = NULL;
	zip->file_list.last = &(zip->file_list.first);
}

static void
file_free_register(struct _7zip *zip)
{
	struct file *file, *file_next;

	file = zip->file_list.first;
	while (file != NULL) {
		file_next = file->next;
		file_free(file);
		file = file_next;
	}
}

static void
file_register_empty(struct _7zip *zip, struct file *file)
{
	file->next = NULL;
	*zip->empty_list.last = file;
	zip->empty_list.last = &(file->next);
}

static void
file_init_register_empty(struct _7zip *zip)
{
	zip->empty_list.first = NULL;
	zip->empty_list.last = &(zip->empty_list.first);
}

#if !defined(HAVE_ZLIB_H) || !defined(HAVE_BZLIB_H) ||\
	 !defined(BZ_CONFIG_ERROR) || !defined(HAVE_LZMA_H)
static int
compression_unsupported_encoder(struct archive *a,
    struct la_zstream *lastrm, const char *name)
{

	archive_set_error(a, ARCHIVE_ERRNO_MISC,
	    "%s compression not supported on this platform", name);
	lastrm->valid = 0;
	lastrm->real_stream = NULL;
	return (ARCHIVE_FAILED);
}
#endif

/*
 * _7_COPY compressor.
 */
static int
compression_init_encoder_copy(struct archive *a, struct la_zstream *lastrm)
{

	if (lastrm->valid)
		compression_end(a, lastrm);
	lastrm->valid = 1;
	lastrm->code = compression_code_copy;
	lastrm->end = compression_end_copy;
	return (ARCHIVE_OK);
}

static int
compression_code_copy(struct archive *a,
    struct la_zstream *lastrm, enum la_zaction action)
{
	size_t bytes;

	(void)a; /* UNUSED */
	if (lastrm->avail_out > lastrm->avail_in)
		bytes = lastrm->avail_in;
	else
		bytes = lastrm->avail_out;
	if (bytes) {
		memcpy(lastrm->next_out, lastrm->next_in, bytes);
		lastrm->next_in += bytes;
		lastrm->avail_in -= bytes;
		lastrm->total_in += bytes;
		lastrm->next_out += bytes;
		lastrm->avail_out -= bytes;
		lastrm->total_out += bytes;
	}
	if (action == ARCHIVE_Z_FINISH && lastrm->avail_in == 0)
		return (ARCHIVE_EOF);
	return (ARCHIVE_OK);
}

static int
compression_end_copy(struct archive *a, struct la_zstream *lastrm)
{
	(void)a; /* UNUSED */
	lastrm->valid = 0;
	return (ARCHIVE_OK);
}

/*
 * _7_DEFLATE compressor.
 */
#ifdef HAVE_ZLIB_H
static int
compression_init_encoder_deflate(struct archive *a,
    struct la_zstream *lastrm, int level, int withheader)
{
	z_stream *strm;

	if (lastrm->valid)
		compression_end(a, lastrm);
	strm = calloc(1, sizeof(*strm));
	if (strm == NULL) {
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for gzip stream");
		return (ARCHIVE_FATAL);
	}
	/* zlib.h is not const-correct, so we need this one bit
	 * of ugly hackery to convert a const * pointer to
	 * a non-const pointer. */
	strm->next_in = (Bytef *)(uintptr_t)(const void *)lastrm->next_in;
	strm->avail_in = (uInt)lastrm->avail_in;
	strm->total_in = (uLong)lastrm->total_in;
	strm->next_out = lastrm->next_out;
	strm->avail_out = (uInt)lastrm->avail_out;
	strm->total_out = (uLong)lastrm->total_out;
	if (deflateInit2(strm, level, Z_DEFLATED,
	    (withheader)?15:-15,
	    8, Z_DEFAULT_STRATEGY) != Z_OK) {
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Internal error initializing compression library");
		return (ARCHIVE_FATAL);
	}
	lastrm->real_stream = strm;
	lastrm->valid = 1;
	lastrm->code = compression_code_deflate;
	lastrm->end = compression_end_deflate;
	return (ARCHIVE_OK);
}

static int
compression_code_deflate(struct archive *a,
    struct la_zstream *lastrm, enum la_zaction action)
{
	z_stream *strm;
	int r;

	strm = (z_stream *)lastrm->real_stream;
	/* zlib.h is not const-correct, so we need this one bit
	 * of ugly hackery to convert a const * pointer to
	 * a non-const pointer. */
	strm->next_in = (Bytef *)(uintptr_t)(const void *)lastrm->next_in;
	strm->avail_in = (uInt)lastrm->avail_in;
	strm->total_in = (uLong)lastrm->total_in;
	strm->next_out = lastrm->next_out;
	strm->avail_out = (uInt)lastrm->avail_out;
	strm->total_out = (uLong)lastrm->total_out;
	r = deflate(strm,
	    (action == ARCHIVE_Z_FINISH)? Z_FINISH: Z_NO_FLUSH);
	lastrm->next_in = strm->next_in;
	lastrm->avail_in = strm->avail_in;
	lastrm->total_in = strm->total_in;
	lastrm->next_out = strm->next_out;
	lastrm->avail_out = strm->avail_out;
	lastrm->total_out = strm->total_out;
	switch (r) {
	case Z_OK:
		return (ARCHIVE_OK);
	case Z_STREAM_END:
		return (ARCHIVE_EOF);
	default:
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "GZip compression failed:"
		    " deflate() call returned status %d", r);
		return (ARCHIVE_FATAL);
	}
}

static int
compression_end_deflate(struct archive *a, struct la_zstream *lastrm)
{
	z_stream *strm;
	int r;

	strm = (z_stream *)lastrm->real_stream;
	r = deflateEnd(strm);
	free(strm);
	lastrm->real_stream = NULL;
	lastrm->valid = 0;
	if (r != Z_OK) {
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Failed to clean up compressor");
		return (ARCHIVE_FATAL);
	}
	return (ARCHIVE_OK);
}
#else
static int
compression_init_encoder_deflate(struct archive *a,
    struct la_zstream *lastrm, int level, int withheader)
{

	(void) level; /* UNUSED */
	(void) withheader; /* UNUSED */
	if (lastrm->valid)
		compression_end(a, lastrm);
	return (compression_unsupported_encoder(a, lastrm, "deflate"));
}
#endif

/*
 * _7_BZIP2 compressor.
 */
#if defined(HAVE_BZLIB_H) && defined(BZ_CONFIG_ERROR)
static int
compression_init_encoder_bzip2(struct archive *a,
    struct la_zstream *lastrm, int level)
{
	bz_stream *strm;

	if (lastrm->valid)
		compression_end(a, lastrm);
	strm = calloc(1, sizeof(*strm));
	if (strm == NULL) {
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for bzip2 stream");
		return (ARCHIVE_FATAL);
	}
	/* bzlib.h is not const-correct, so we need this one bit
	 * of ugly hackery to convert a const * pointer to
	 * a non-const pointer. */
	strm->next_in = (char *)(uintptr_t)(const void *)lastrm->next_in;
	strm->avail_in = lastrm->avail_in;
	strm->total_in_lo32 = (uint32_t)(lastrm->total_in & 0xffffffff);
	strm->total_in_hi32 = (uint32_t)(lastrm->total_in >> 32);
	strm->next_out = (char *)lastrm->next_out;
	strm->avail_out = lastrm->avail_out;
	strm->total_out_lo32 = (uint32_t)(lastrm->total_out & 0xffffffff);
	strm->total_out_hi32 = (uint32_t)(lastrm->total_out >> 32);
	if (BZ2_bzCompressInit(strm, level, 0, 30) != BZ_OK) {
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Internal error initializing compression library");
		return (ARCHIVE_FATAL);
	}
	lastrm->real_stream = strm;
	lastrm->valid = 1;
	lastrm->code = compression_code_bzip2;
	lastrm->end = compression_end_bzip2;
	return (ARCHIVE_OK);
}

static int
compression_code_bzip2(struct archive *a,
    struct la_zstream *lastrm, enum la_zaction action)
{
	bz_stream *strm;
	int r;

	strm = (bz_stream *)lastrm->real_stream;
	/* bzlib.h is not const-correct, so we need this one bit
	 * of ugly hackery to convert a const * pointer to
	 * a non-const pointer. */
	strm->next_in = (char *)(uintptr_t)(const void *)lastrm->next_in;
	strm->avail_in = lastrm->avail_in;
	strm->total_in_lo32 = (uint32_t)(lastrm->total_in & 0xffffffff);
	strm->total_in_hi32 = (uint32_t)(lastrm->total_in >> 32);
	strm->next_out = (char *)lastrm->next_out;
	strm->avail_out = lastrm->avail_out;
	strm->total_out_lo32 = (uint32_t)(lastrm->total_out & 0xffffffff);
	strm->total_out_hi32 = (uint32_t)(lastrm->total_out >> 32);
	r = BZ2_bzCompress(strm,
	    (action == ARCHIVE_Z_FINISH)? BZ_FINISH: BZ_RUN);
	lastrm->next_in = (const unsigned char *)strm->next_in;
	lastrm->avail_in = strm->avail_in;
	lastrm->total_in =
	    (((uint64_t)(uint32_t)strm->total_in_hi32) << 32)
	    + (uint64_t)(uint32_t)strm->total_in_lo32;
	lastrm->next_out = (unsigned char *)strm->next_out;
	lastrm->avail_out = strm->avail_out;
	lastrm->total_out =
	    (((uint64_t)(uint32_t)strm->total_out_hi32) << 32)
	    + (uint64_t)(uint32_t)strm->total_out_lo32;
	switch (r) {
	case BZ_RUN_OK:     /* Non-finishing */
	case BZ_FINISH_OK:  /* Finishing: There's more work to do */
		return (ARCHIVE_OK);
	case BZ_STREAM_END: /* Finishing: all done */
		/* Only occurs in finishing case */
		return (ARCHIVE_EOF);
	default:
		/* Any other return value indicates an error */
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Bzip2 compression failed:"
		    " BZ2_bzCompress() call returned status %d", r);
		return (ARCHIVE_FATAL);
	}
}

static int
compression_end_bzip2(struct archive *a, struct la_zstream *lastrm)
{
	bz_stream *strm;
	int r;

	strm = (bz_stream *)lastrm->real_stream;
	r = BZ2_bzCompressEnd(strm);
	free(strm);
	lastrm->real_stream = NULL;
	lastrm->valid = 0;
	if (r != BZ_OK) {
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Failed to clean up compressor");
		return (ARCHIVE_FATAL);
	}
	return (ARCHIVE_OK);
}

#else
static int
compression_init_encoder_bzip2(struct archive *a,
    struct la_zstream *lastrm, int level)
{

	(void) level; /* UNUSED */
	if (lastrm->valid)
		compression_end(a, lastrm);
	return (compression_unsupported_encoder(a, lastrm, "bzip2"));
}
#endif

/*
 * _7_LZMA1, _7_LZMA2 compressor.
 */
#if defined(HAVE_LZMA_H)
static int
compression_init_encoder_lzma(struct archive *a,
    struct la_zstream *lastrm, int level, uint64_t filter_id)
{
	static const lzma_stream lzma_init_data = LZMA_STREAM_INIT;
	lzma_stream *strm;
	lzma_filter *lzmafilters;
	lzma_options_lzma lzma_opt;
	int r;

	if (lastrm->valid)
		compression_end(a, lastrm);
	strm = calloc(1, sizeof(*strm) + sizeof(*lzmafilters) * 2);
	if (strm == NULL) {
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for lzma stream");
		return (ARCHIVE_FATAL);
	}
	lzmafilters = (lzma_filter *)(strm+1);
	if (level > 6)
		level = 6;
	if (lzma_lzma_preset(&lzma_opt, level)) {
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ENOMEM,
		    "Internal error initializing compression library");
		return (ARCHIVE_FATAL);
	}
	lzmafilters[0].id = filter_id;
	lzmafilters[0].options = &lzma_opt;
	lzmafilters[1].id = LZMA_VLI_UNKNOWN;/* Terminate */

	r = lzma_properties_size(&(lastrm->prop_size), lzmafilters);
	if (r != LZMA_OK) {
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "lzma_properties_size failed");
		return (ARCHIVE_FATAL);
	}
	if (lastrm->prop_size) {
		lastrm->props = malloc(lastrm->prop_size);
		if (lastrm->props == NULL) {
			free(strm);
			lastrm->real_stream = NULL;
			archive_set_error(a, ENOMEM,
			    "Cannot allocate memory");
			return (ARCHIVE_FATAL);
		}
		r = lzma_properties_encode(lzmafilters,  lastrm->props);
		if (r != LZMA_OK) {
			free(strm);
			lastrm->real_stream = NULL;
			archive_set_error(a, ARCHIVE_ERRNO_MISC,
			    "lzma_properties_encode failed");
			return (ARCHIVE_FATAL);
		}
	}

	*strm = lzma_init_data;
	r = lzma_raw_encoder(strm, lzmafilters);
	switch (r) {
	case LZMA_OK:
		lastrm->real_stream = strm;
		lastrm->valid = 1;
		lastrm->code = compression_code_lzma;
		lastrm->end = compression_end_lzma;
		r = ARCHIVE_OK;
		break;
	case LZMA_MEM_ERROR:
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ENOMEM,
		    "Internal error initializing compression library: "
		    "Cannot allocate memory");
		r =  ARCHIVE_FATAL;
		break;
        default:
		free(strm);
		lastrm->real_stream = NULL;
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "Internal error initializing compression library: "
		    "It's a bug in liblzma");
		r =  ARCHIVE_FATAL;
		break;
	}
	return (r);
}

static int
compression_init_encoder_lzma1(struct archive *a,
    struct la_zstream *lastrm, int level)
{
	return compression_init_encoder_lzma(a, lastrm, level,
		    LZMA_FILTER_LZMA1);
}

static int
compression_init_encoder_lzma2(struct archive *a,
    struct la_zstream *lastrm, int level)
{
	return compression_init_encoder_lzma(a, lastrm, level,
		    LZMA_FILTER_LZMA2);
}

static int
compression_code_lzma(struct archive *a,
    struct la_zstream *lastrm, enum la_zaction action)
{
	lzma_stream *strm;
	int r;

	strm = (lzma_stream *)lastrm->real_stream;
	strm->next_in = lastrm->next_in;
	strm->avail_in = lastrm->avail_in;
	strm->total_in = lastrm->total_in;
	strm->next_out = lastrm->next_out;
	strm->avail_out = lastrm->avail_out;
	strm->total_out = lastrm->total_out;
	r = lzma_code(strm,
	    (action == ARCHIVE_Z_FINISH)? LZMA_FINISH: LZMA_RUN);
	lastrm->next_in = strm->next_in;
	lastrm->avail_in = strm->avail_in;
	lastrm->total_in = strm->total_in;
	lastrm->next_out = strm->next_out;
	lastrm->avail_out = strm->avail_out;
	lastrm->total_out = strm->total_out;
	switch (r) {
	case LZMA_OK:
		/* Non-finishing case */
		return (ARCHIVE_OK);
	case LZMA_STREAM_END:
		/* This return can only occur in finishing case. */
		return (ARCHIVE_EOF);
	case LZMA_MEMLIMIT_ERROR:
		archive_set_error(a, ENOMEM,
		    "lzma compression error:"
		    " %ju MiB would have been needed",
		    (uintmax_t)((lzma_memusage(strm) + 1024 * 1024 -1)
			/ (1024 * 1024)));
		return (ARCHIVE_FATAL);
	default:
		/* Any other return value indicates an error */
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "lzma compression failed:"
		    " lzma_code() call returned status %d", r);
		return (ARCHIVE_FATAL);
	}
}

static int
compression_end_lzma(struct archive *a, struct la_zstream *lastrm)
{
	lzma_stream *strm;

	(void)a; /* UNUSED */
	strm = (lzma_stream *)lastrm->real_stream;
	lzma_end(strm);
	free(strm);
	lastrm->valid = 0;
	lastrm->real_stream = NULL;
	return (ARCHIVE_OK);
}
#else
static int
compression_init_encoder_lzma1(struct archive *a,
    struct la_zstream *lastrm, int level)
{

	(void) level; /* UNUSED */
	if (lastrm->valid)
		compression_end(a, lastrm);
	return (compression_unsupported_encoder(a, lastrm, "lzma"));
}
static int
compression_init_encoder_lzma2(struct archive *a,
    struct la_zstream *lastrm, int level)
{

	(void) level; /* UNUSED */
	if (lastrm->valid)
		compression_end(a, lastrm);
	return (compression_unsupported_encoder(a, lastrm, "lzma"));
}
#endif

/*
 * _7_PPMD compressor.
 */
static void
ppmd_write(void *p, Byte b)
{
	struct archive_write *a = ((IByteOut *)p)->a;
	struct _7zip *zip = (struct _7zip *)(a->format_data);
	struct la_zstream *lastrm = &(zip->stream);
	struct ppmd_stream *strm;

	if (lastrm->avail_out) {
		*lastrm->next_out++ = b;
		lastrm->avail_out--;
		lastrm->total_out++;
		return;
	}
	strm = (struct ppmd_stream *)lastrm->real_stream;
	if (strm->buff_ptr < strm->buff_end) {
		*strm->buff_ptr++ = b;
		strm->buff_bytes++;
	}
}

static int
compression_init_encoder_ppmd(struct archive *a,
    struct la_zstream *lastrm, unsigned maxOrder, uint32_t msize)
{
	struct ppmd_stream *strm;
	uint8_t *props;
	int r;

	if (lastrm->valid)
		compression_end(a, lastrm);
	strm = calloc(1, sizeof(*strm));
	if (strm == NULL) {
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for PPMd");
		return (ARCHIVE_FATAL);
	}
	strm->buff = malloc(32);
	if (strm->buff == NULL) {
		free(strm);
		archive_set_error(a, ENOMEM,
		    "Can't allocate memory for PPMd");
		return (ARCHIVE_FATAL);
	}
	strm->buff_ptr = strm->buff;
	strm->buff_end = strm->buff + 32;

	props = malloc(1+4);
	if (props == NULL) {
		free(strm->buff);
		free(strm);
		archive_set_error(a, ENOMEM,
		    "Coludn't allocate memory for PPMd");
		return (ARCHIVE_FATAL);
	}
	props[0] = maxOrder;
	archive_le32enc(props+1, msize);
	__archive_ppmd7_functions.Ppmd7_Construct(&strm->ppmd7_context);
	r = __archive_ppmd7_functions.Ppmd7_Alloc(
		&strm->ppmd7_context, msize);
	if (r == 0) {
		free(strm->buff);
		free(strm);
		free(props);
		archive_set_error(a, ENOMEM,
		    "Coludn't allocate memory for PPMd");
		return (ARCHIVE_FATAL);
	}
	__archive_ppmd7_functions.Ppmd7_Init(&(strm->ppmd7_context), maxOrder);
	strm->byteout.a = (struct archive_write *)a;
	strm->byteout.Write = ppmd_write;
	strm->range_enc.Stream = &(strm->byteout);
	__archive_ppmd7_functions.Ppmd7z_RangeEnc_Init(&(strm->range_enc));
	strm->stat = 0;

	lastrm->real_stream = strm;
	lastrm->valid = 1;
	lastrm->code = compression_code_ppmd;
	lastrm->end = compression_end_ppmd;
	lastrm->prop_size = 5;
	lastrm->props = props;
	return (ARCHIVE_OK);
}

static int
compression_code_ppmd(struct archive *a,
    struct la_zstream *lastrm, enum la_zaction action)
{
	struct ppmd_stream *strm;

	(void)a; /* UNUSED */

	strm = (struct ppmd_stream *)lastrm->real_stream;

	/* Copy encoded data if there are remaining bytes from previous call. */
	if (strm->buff_bytes) {
		uint8_t *p = strm->buff_ptr - strm->buff_bytes;
		while (lastrm->avail_out && strm->buff_bytes) {
			*lastrm->next_out++ = *p++;
			lastrm->avail_out--;
			lastrm->total_out++;
			strm->buff_bytes--;
		}
		if (strm->buff_bytes)
			return (ARCHIVE_OK);
		if (strm->stat == 1)
			return (ARCHIVE_EOF);
		strm->buff_ptr = strm->buff;
	}
	while (lastrm->avail_in && lastrm->avail_out) {
		__archive_ppmd7_functions.Ppmd7_EncodeSymbol(
			&(strm->ppmd7_context), &(strm->range_enc),
			*lastrm->next_in++);
		lastrm->avail_in--;
		lastrm->total_in++;
	}
	if (lastrm->avail_in == 0 && action == ARCHIVE_Z_FINISH) {
		__archive_ppmd7_functions.Ppmd7z_RangeEnc_FlushData(
			&(strm->range_enc));
		strm->stat = 1;
		/* Return EOF if there are no remaining bytes. */
		if (strm->buff_bytes == 0)
			return (ARCHIVE_EOF);
	}
	return (ARCHIVE_OK);
}

static int
compression_end_ppmd(struct archive *a, struct la_zstream *lastrm)
{
	struct ppmd_stream *strm;

	(void)a; /* UNUSED */

	strm = (struct ppmd_stream *)lastrm->real_stream;
	__archive_ppmd7_functions.Ppmd7_Free(&strm->ppmd7_context);
	free(strm->buff);
	free(strm);
	lastrm->real_stream = NULL;
	lastrm->valid = 0;
	return (ARCHIVE_OK);
}

/*
 * Universal compressor initializer.
 */
static int
_7z_compression_init_encoder(struct archive_write *a, unsigned compression,
    int compression_level)
{
	struct _7zip *zip;
	int r;

	zip = (struct _7zip *)a->format_data;
	switch (compression) {
	case _7Z_DEFLATE:
		r = compression_init_encoder_deflate(
		    &(a->archive), &(zip->stream),
		    compression_level, 0);
		break;
	case _7Z_BZIP2:
		r = compression_init_encoder_bzip2(
		    &(a->archive), &(zip->stream),
		    compression_level);
		break;
	case _7Z_LZMA1:
		r = compression_init_encoder_lzma1(
		    &(a->archive), &(zip->stream),
		    compression_level);
		break;
	case _7Z_LZMA2:
		r = compression_init_encoder_lzma2(
		    &(a->archive), &(zip->stream),
		    compression_level);
		break;
	case _7Z_PPMD:
		r = compression_init_encoder_ppmd(
		    &(a->archive), &(zip->stream),
		    PPMD7_DEFAULT_ORDER, PPMD7_DEFAULT_MEM_SIZE);
		break;
	case _7Z_COPY:
	default:
		r = compression_init_encoder_copy(
		    &(a->archive), &(zip->stream));
		break;
	}
	if (r == ARCHIVE_OK) {
		zip->stream.total_in = 0;
		zip->stream.next_out = zip->wbuff;
		zip->stream.avail_out = sizeof(zip->wbuff);
		zip->stream.total_out = 0;
	}

	return (r);
}

static int
compression_code(struct archive *a, struct la_zstream *lastrm,
    enum la_zaction action)
{
	if (lastrm->valid)
		return (lastrm->code(a, lastrm, action));
	return (ARCHIVE_OK);
}

static int
compression_end(struct archive *a, struct la_zstream *lastrm)
{
	if (lastrm->valid) {
		lastrm->prop_size = 0;
		free(lastrm->props);
		lastrm->props = NULL;
		return (lastrm->end(a, lastrm));
	}
	return (ARCHIVE_OK);
}


