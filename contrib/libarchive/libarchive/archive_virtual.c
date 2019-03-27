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

#include "archive.h"
#include "archive_entry.h"
#include "archive_private.h"

int
archive_filter_code(struct archive *a, int n)
{
	return ((a->vtable->archive_filter_code)(a, n));
}

int
archive_filter_count(struct archive *a)
{
	return ((a->vtable->archive_filter_count)(a));
}

const char *
archive_filter_name(struct archive *a, int n)
{
	return ((a->vtable->archive_filter_name)(a, n));
}

la_int64_t
archive_filter_bytes(struct archive *a, int n)
{
	return ((a->vtable->archive_filter_bytes)(a, n));
}

int
archive_free(struct archive *a)
{
	if (a == NULL)
		return (ARCHIVE_OK);
	return ((a->vtable->archive_free)(a));
}

int
archive_write_close(struct archive *a)
{
	return ((a->vtable->archive_close)(a));
}

int
archive_read_close(struct archive *a)
{
	return ((a->vtable->archive_close)(a));
}

int
archive_write_fail(struct archive *a)
{
	a->state = ARCHIVE_STATE_FATAL;
	return a->state;
}

int
archive_write_free(struct archive *a)
{
	return archive_free(a);
}

#if ARCHIVE_VERSION_NUMBER < 4000000
/* For backwards compatibility; will be removed with libarchive 4.0. */
int
archive_write_finish(struct archive *a)
{
	return archive_write_free(a);
}
#endif

int
archive_read_free(struct archive *a)
{
	return archive_free(a);
}

#if ARCHIVE_VERSION_NUMBER < 4000000
/* For backwards compatibility; will be removed with libarchive 4.0. */
int
archive_read_finish(struct archive *a)
{
	return archive_read_free(a);
}
#endif

int
archive_write_header(struct archive *a, struct archive_entry *entry)
{
	++a->file_count;
	return ((a->vtable->archive_write_header)(a, entry));
}

int
archive_write_finish_entry(struct archive *a)
{
	return ((a->vtable->archive_write_finish_entry)(a));
}

la_ssize_t
archive_write_data(struct archive *a, const void *buff, size_t s)
{
	return ((a->vtable->archive_write_data)(a, buff, s));
}

la_ssize_t
archive_write_data_block(struct archive *a, const void *buff, size_t s,
    la_int64_t o)
{
	if (a->vtable->archive_write_data_block == NULL) {
		archive_set_error(a, ARCHIVE_ERRNO_MISC,
		    "archive_write_data_block not supported");
		a->state = ARCHIVE_STATE_FATAL;
		return (ARCHIVE_FATAL);
	}
	return ((a->vtable->archive_write_data_block)(a, buff, s, o));
}

int
archive_read_next_header(struct archive *a, struct archive_entry **entry)
{
	return ((a->vtable->archive_read_next_header)(a, entry));
}

int
archive_read_next_header2(struct archive *a, struct archive_entry *entry)
{
	return ((a->vtable->archive_read_next_header2)(a, entry));
}

int
archive_read_data_block(struct archive *a,
    const void **buff, size_t *s, la_int64_t *o)
{
	return ((a->vtable->archive_read_data_block)(a, buff, s, o));
}
