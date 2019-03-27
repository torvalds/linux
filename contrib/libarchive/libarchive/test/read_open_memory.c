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

#include "test.h"
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * Read an archive from a block of memory.
 *
 * This is identical to archive_read_open_memory(), except
 * that it goes out of its way to be a little bit unpleasant,
 * in order to better test the libarchive internals.
 */

struct read_memory_data {
	const unsigned char	*start;
	const unsigned char	*p;
	const unsigned char	*end;
	size_t	 read_size;
	size_t copy_buff_size;
	size_t copy_buff_offset;
	char *copy_buff;
};

static int	memory_read_close(struct archive *, void *);
static int	memory_read_open(struct archive *, void *);
static int64_t	memory_read_seek(struct archive *, void *, int64_t request, int whence);
static int64_t	memory_read_skip(struct archive *, void *, int64_t request);
static ssize_t	memory_read(struct archive *, void *, const void **buff);
static int	read_open_memory_internal(struct archive *a, const void *buff,
    size_t size, size_t read_size, int fullapi);


int
read_open_memory(struct archive *a, const void *buff, size_t size, size_t read_size)
{
	return read_open_memory_internal(a, buff, size, read_size, 2);
}

/*
 * As above, but don't register any optional part of the API, to verify
 * that internals work correctly with just the minimal entry points.
 */
int
read_open_memory_minimal(struct archive *a, const void *buff, size_t size, size_t read_size)
{
	return read_open_memory_internal(a, buff, size, read_size, 1);
}

/*
 * Include a seek callback as well.
 */
int
read_open_memory_seek(struct archive *a, const void *buff, size_t size, size_t read_size)
{
	return read_open_memory_internal(a, buff, size, read_size, 3);
}

static int
read_open_memory_internal(struct archive *a, const void *buff,
    size_t size, size_t read_size, int level)
{
	struct read_memory_data *mine = NULL;

	switch (level) {
	case 3:
		archive_read_set_seek_callback(a, memory_read_seek);
		__LA_FALLTHROUGH;
	case 2:
		archive_read_set_open_callback(a, memory_read_open);
		archive_read_set_skip_callback(a, memory_read_skip);
		__LA_FALLTHROUGH;
	case 1:
		mine = malloc(sizeof(*mine));
		if (mine == NULL) {
			archive_set_error(a, ENOMEM, "No memory");
			return (ARCHIVE_FATAL);
		}
		memset(mine, 0, sizeof(*mine));
		mine->start = mine->p = (const unsigned char *)buff;
		mine->end = mine->start + size;
		mine->read_size = read_size;
		mine->copy_buff_offset = 32;
		mine->copy_buff_size = read_size + mine->copy_buff_offset * 2;
		mine->copy_buff = malloc(mine->copy_buff_size);
		memset(mine->copy_buff, 0xA5, mine->copy_buff_size);

		archive_read_set_read_callback(a, memory_read);
		archive_read_set_close_callback(a, memory_read_close);
		archive_read_set_callback_data(a, mine);
	}
	return archive_read_open1(a);
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
 * In order to exercise libarchive's internal read-combining logic,
 * we deliberately copy data for each read to a separate buffer.
 * That way, code that runs off the end of the provided data
 * will screw up.
 */
static ssize_t
memory_read(struct archive *a, void *client_data, const void **buff)
{
	struct read_memory_data *mine = (struct read_memory_data *)client_data;
	ssize_t size;

	(void)a; /* UNUSED */
	size = mine->end - mine->p;
	if (size < 0) {
		buff = NULL;
		return 0;
	}
	if ((size_t)size > mine->read_size)
		size = mine->read_size;
	else
		memset(mine->copy_buff, 0xA5, mine->copy_buff_size);
	memcpy(mine->copy_buff + mine->copy_buff_offset, mine->p, size);
	*buff = mine->copy_buff + mine->copy_buff_offset;

        mine->p += size;
	return ((ssize_t)size);
}

/*
 * How mean can a skip() routine be?  Let's try to find out.
 */
static int64_t
memory_read_skip(struct archive *a, void *client_data, int64_t skip)
{
	struct read_memory_data *mine = (struct read_memory_data *)client_data;

	(void)a; /* UNUSED */
	/* We can't skip by more than is available. */
	if ((off_t)skip > (off_t)(mine->end - mine->p))
		skip = mine->end - mine->p;
	/* Always do small skips by prime amounts. */
	if (skip > 71)
		skip = 71;
	mine->p += skip;
	return (skip);
}

/*
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
	case SEEK_END:
		mine->p = mine->end + offset;
		break;
	case SEEK_CUR:
		mine->p += offset;
		break;
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
	if (mine != NULL)
		free(mine->copy_buff);
	free(mine);
	return (ARCHIVE_OK);
}
