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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "archive.h"

/*
 * Glue to read an archive from a block of memory.
 *
 * This is mostly a huge help in building test harnesses;
 * test programs can build archives in memory and read them
 * back again without having to mess with files on disk.
 */

struct read_memory_data {
	const unsigned char	*start;
	const unsigned char	*p;
	const unsigned char	*end;
	ssize_t	 read_size;
};

static int	memory_read_close(struct archive *, void *);
static int	memory_read_open(struct archive *, void *);
static int64_t	memory_read_seek(struct archive *, void *, int64_t offset, int whence);
static int64_t	memory_read_skip(struct archive *, void *, int64_t request);
static ssize_t	memory_read(struct archive *, void *, const void **buff);

int
archive_read_open_memory(struct archive *a, const void *buff, size_t size)
{
	return archive_read_open_memory2(a, buff, size, size);
}

/*
 * Don't use _open_memory2() in production code; the archive_read_open_memory()
 * version is the one you really want.  This is just here so that
 * test harnesses can exercise block operations inside the library.
 */
int
archive_read_open_memory2(struct archive *a, const void *buff,
    size_t size, size_t read_size)
{
	struct read_memory_data *mine;

	mine = (struct read_memory_data *)calloc(1, sizeof(*mine));
	if (mine == NULL) {
		archive_set_error(a, ENOMEM, "No memory");
		return (ARCHIVE_FATAL);
	}
	mine->start = mine->p = (const unsigned char *)buff;
	mine->end = mine->start + size;
	mine->read_size = read_size;
	archive_read_set_open_callback(a, memory_read_open);
	archive_read_set_read_callback(a, memory_read);
	archive_read_set_seek_callback(a, memory_read_seek);
	archive_read_set_skip_callback(a, memory_read_skip);
	archive_read_set_close_callback(a, memory_read_close);
	archive_read_set_callback_data(a, mine);
	return (archive_read_open1(a));
}

/*
 * There's nothing to open.
 */
static int
memory_read_open(struct archive *a, void *client_data)
{
	(void)a; /* UNUSED */
	(void)client_data; /* UNUSED */
	return (ARCHIVE_OK);
}

/*
 * This is scary simple:  Just advance a pointer.  Limiting
 * to read_size is not technically necessary, but it exercises
 * more of the internal logic when used with a small block size
 * in a test harness.  Production use should not specify a block
 * size; then this is much faster.
 */
static ssize_t
memory_read(struct archive *a, void *client_data, const void **buff)
{
	struct read_memory_data *mine = (struct read_memory_data *)client_data;
	ssize_t size;

	(void)a; /* UNUSED */
	*buff = mine->p;
	size = mine->end - mine->p;
	if (size > mine->read_size)
		size = mine->read_size;
        mine->p += size;
	return (size);
}

/*
 * Advancing is just as simple.  Again, this is doing more than
 * necessary in order to better exercise internal code when used
 * as a test harness.
 */
static int64_t
memory_read_skip(struct archive *a, void *client_data, int64_t skip)
{
	struct read_memory_data *mine = (struct read_memory_data *)client_data;

	(void)a; /* UNUSED */
	if ((int64_t)skip > (int64_t)(mine->end - mine->p))
		skip = mine->end - mine->p;
	/* Round down to block size. */
	skip /= mine->read_size;
	skip *= mine->read_size;
	mine->p += skip;
	return (skip);
}

/*
 * Seeking.
 */
static int64_t
memory_read_seek(struct archive *a, void *client_data, int64_t offset, int whence)
{
	struct read_memory_data *mine = (struct read_memory_data *)client_data;

	(void)a; /* UNUSED */
	switch (whence) {
	case SEEK_SET:
		mine->p = mine->start + offset;
		break;
	case SEEK_CUR:
		mine->p += offset;
		break;
	case SEEK_END:
		mine->p = mine->end + offset;
		break;
	default:
		return ARCHIVE_FATAL;
	}
	if (mine->p < mine->start) {
		mine->p = mine->start;
		return ARCHIVE_FAILED;
	}
	if (mine->p > mine->end) {
		mine->p = mine->end;
		return ARCHIVE_FAILED;
	}
	return (mine->p - mine->start);
}

/*
 * Close is just cleaning up our one small bit of data.
 */
static int
memory_read_close(struct archive *a, void *client_data)
{
	struct read_memory_data *mine = (struct read_memory_data *)client_data;
	(void)a; /* UNUSED */
	free(mine);
	return (ARCHIVE_OK);
}
