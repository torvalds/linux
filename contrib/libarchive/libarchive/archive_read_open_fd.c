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

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "archive.h"

struct read_fd_data {
	int	 fd;
	size_t	 block_size;
	char	 use_lseek;
	void	*buffer;
};

static int	file_close(struct archive *, void *);
static ssize_t	file_read(struct archive *, void *, const void **buff);
static int64_t	file_seek(struct archive *, void *, int64_t request, int);
static int64_t	file_skip(struct archive *, void *, int64_t request);

int
archive_read_open_fd(struct archive *a, int fd, size_t block_size)
{
	struct stat st;
	struct read_fd_data *mine;
	void *b;

	archive_clear_error(a);
	if (fstat(fd, &st) != 0) {
		archive_set_error(a, errno, "Can't stat fd %d", fd);
		return (ARCHIVE_FATAL);
	}

	mine = (struct read_fd_data *)calloc(1, sizeof(*mine));
	b = malloc(block_size);
	if (mine == NULL || b == NULL) {
		archive_set_error(a, ENOMEM, "No memory");
		free(mine);
		free(b);
		return (ARCHIVE_FATAL);
	}
	mine->block_size = block_size;
	mine->buffer = b;
	mine->fd = fd;
	/*
	 * Skip support is a performance optimization for anything
	 * that supports lseek().  On FreeBSD, only regular files and
	 * raw disk devices support lseek() and there's no portable
	 * way to determine if a device is a raw disk device, so we
	 * only enable this optimization for regular files.
	 */
	if (S_ISREG(st.st_mode)) {
		archive_read_extract_set_skip_file(a, st.st_dev, st.st_ino);
		mine->use_lseek = 1;
	}
#if defined(__CYGWIN__) || defined(_WIN32)
	setmode(mine->fd, O_BINARY);
#endif

	archive_read_set_read_callback(a, file_read);
	archive_read_set_skip_callback(a, file_skip);
	archive_read_set_seek_callback(a, file_seek);
	archive_read_set_close_callback(a, file_close);
	archive_read_set_callback_data(a, mine);
	return (archive_read_open1(a));
}

static ssize_t
file_read(struct archive *a, void *client_data, const void **buff)
{
	struct read_fd_data *mine = (struct read_fd_data *)client_data;
	ssize_t bytes_read;

	*buff = mine->buffer;
	for (;;) {
		bytes_read = read(mine->fd, mine->buffer, mine->block_size);
		if (bytes_read < 0) {
			if (errno == EINTR)
				continue;
			archive_set_error(a, errno, "Error reading fd %d",
			    mine->fd);
		}
		return (bytes_read);
	}
}

static int64_t
file_skip(struct archive *a, void *client_data, int64_t request)
{
	struct read_fd_data *mine = (struct read_fd_data *)client_data;
	int64_t skip = request;
	int64_t old_offset, new_offset;
	int skip_bits = sizeof(skip) * 8 - 1;  /* off_t is a signed type. */

	if (!mine->use_lseek)
		return (0);

	/* Reduce a request that would overflow the 'skip' variable. */
	if (sizeof(request) > sizeof(skip)) {
		int64_t max_skip =
		    (((int64_t)1 << (skip_bits - 1)) - 1) * 2 + 1;
		if (request > max_skip)
			skip = max_skip;
	}

	/* Reduce request to the next smallest multiple of block_size */
	request = (request / mine->block_size) * mine->block_size;
	if (request == 0)
		return (0);

	if (((old_offset = lseek(mine->fd, 0, SEEK_CUR)) >= 0) &&
	    ((new_offset = lseek(mine->fd, skip, SEEK_CUR)) >= 0))
		return (new_offset - old_offset);

	/* If seek failed once, it will probably fail again. */
	mine->use_lseek = 0;

	/* Let libarchive recover with read+discard. */
	if (errno == ESPIPE)
		return (0);

	/*
	 * There's been an error other than ESPIPE. This is most
	 * likely caused by a programmer error (too large request)
	 * or a corrupted archive file.
	 */
	archive_set_error(a, errno, "Error seeking");
	return (-1);
}

/*
 * TODO: Store the offset and use it in the read callback.
 */
static int64_t
file_seek(struct archive *a, void *client_data, int64_t request, int whence)
{
	struct read_fd_data *mine = (struct read_fd_data *)client_data;
	int64_t r;

	/* We use off_t here because lseek() is declared that way. */
	/* See above for notes about when off_t is less than 64 bits. */
	r = lseek(mine->fd, request, whence);
	if (r >= 0)
		return r;

	if (errno == ESPIPE) {
		archive_set_error(a, errno,
		    "A file descriptor(%d) is not seekable(PIPE)", mine->fd);
		return (ARCHIVE_FAILED);
	} else {
		/* If the input is corrupted or truncated, fail. */
		archive_set_error(a, errno,
		    "Error seeking in a file descriptor(%d)", mine->fd);
		return (ARCHIVE_FATAL);
	}
}

static int
file_close(struct archive *a, void *client_data)
{
	struct read_fd_data *mine = (struct read_fd_data *)client_data;

	(void)a; /* UNUSED */
	free(mine->buffer);
	free(mine);
	return (ARCHIVE_OK);
}
