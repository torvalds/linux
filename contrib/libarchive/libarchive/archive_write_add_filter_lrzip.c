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
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "archive.h"
#include "archive_string.h"
#include "archive_write_private.h"

struct write_lrzip {
	struct archive_write_program_data *pdata;
	int	compression_level;
	enum { lzma = 0, bzip2, gzip, lzo, none, zpaq } compression;
};

static int archive_write_lrzip_open(struct archive_write_filter *);
static int archive_write_lrzip_options(struct archive_write_filter *,
		    const char *, const char *);
static int archive_write_lrzip_write(struct archive_write_filter *,
		    const void *, size_t);
static int archive_write_lrzip_close(struct archive_write_filter *);
static int archive_write_lrzip_free(struct archive_write_filter *);

int
archive_write_add_filter_lrzip(struct archive *_a)
{
	struct archive_write_filter *f = __archive_write_allocate_filter(_a);
	struct write_lrzip *data;

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_add_filter_lrzip");

	data = calloc(1, sizeof(*data));
	if (data == NULL) {
		archive_set_error(_a, ENOMEM, "Can't allocate memory");
		return (ARCHIVE_FATAL);
	}
	data->pdata = __archive_write_program_allocate("lrzip");
	if (data->pdata == NULL) {
		free(data);
		archive_set_error(_a, ENOMEM, "Can't allocate memory");
		return (ARCHIVE_FATAL);
	}

	f->name = "lrzip";
	f->code = ARCHIVE_FILTER_LRZIP;
	f->data = data;
	f->open = archive_write_lrzip_open;
	f->options = archive_write_lrzip_options;
	f->write = archive_write_lrzip_write;
	f->close = archive_write_lrzip_close;
	f->free = archive_write_lrzip_free;

	/* Note: This filter always uses an external program, so we
	 * return "warn" to inform of the fact. */
	archive_set_error(_a, ARCHIVE_ERRNO_MISC,
	    "Using external lrzip program for lrzip compression");
	return (ARCHIVE_WARN);
}

static int
archive_write_lrzip_options(struct archive_write_filter *f, const char *key,
    const char *value)
{
	struct write_lrzip *data = (struct write_lrzip *)f->data;

	if (strcmp(key, "compression") == 0) {
		if (value == NULL)
			return (ARCHIVE_WARN);
		else if (strcmp(value, "bzip2") == 0)
			data->compression = bzip2;
		else if (strcmp(value, "gzip") == 0)
			data->compression = gzip;
		else if (strcmp(value, "lzo") == 0)
			data->compression = lzo;
		else if (strcmp(value, "none") == 0)
			data->compression = none;
		else if (strcmp(value, "zpaq") == 0)
			data->compression = zpaq;
		else
			return (ARCHIVE_WARN);
		return (ARCHIVE_OK);
	} else if (strcmp(key, "compression-level") == 0) {
		if (value == NULL || !(value[0] >= '1' && value[0] <= '9') ||
		    value[1] != '\0')
			return (ARCHIVE_WARN);
		data->compression_level = value[0] - '0';
		return (ARCHIVE_OK);
	}
	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

static int
archive_write_lrzip_open(struct archive_write_filter *f)
{
	struct write_lrzip *data = (struct write_lrzip *)f->data;
	struct archive_string as;
	int r;

	archive_string_init(&as);
	archive_strcpy(&as, "lrzip -q");

	/* Specify compression type. */
	switch (data->compression) {
	case lzma:/* default compression */
		break;
	case bzip2:
		archive_strcat(&as, " -b");
		break;
	case gzip:
		archive_strcat(&as, " -g");
		break;
	case lzo:
		archive_strcat(&as, " -l");
		break;
	case none:
		archive_strcat(&as, " -n");
		break;
	case zpaq:
		archive_strcat(&as, " -z");
		break;
	}

	/* Specify compression level. */
	if (data->compression_level > 0) {
		archive_strcat(&as, " -L ");
		archive_strappend_char(&as, '0' + data->compression_level);
	}

	r = __archive_write_program_open(f, data->pdata, as.s);
	archive_string_free(&as);
	return (r);
}

static int
archive_write_lrzip_write(struct archive_write_filter *f,
    const void *buff, size_t length)
{
	struct write_lrzip *data = (struct write_lrzip *)f->data;

	return __archive_write_program_write(f, data->pdata, buff, length);
}

static int
archive_write_lrzip_close(struct archive_write_filter *f)
{
	struct write_lrzip *data = (struct write_lrzip *)f->data;

	return __archive_write_program_close(f, data->pdata);
}

static int
archive_write_lrzip_free(struct archive_write_filter *f)
{
	struct write_lrzip *data = (struct write_lrzip *)f->data;

	__archive_write_program_free(data->pdata);
	free(data);
	return (ARCHIVE_OK);
}
