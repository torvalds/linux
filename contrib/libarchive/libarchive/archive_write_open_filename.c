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
#include "archive_private.h"
#include "archive_string.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC	0
#endif

struct write_file_data {
	int		fd;
	struct archive_mstring filename;
};

static int	file_close(struct archive *, void *);
static int	file_open(struct archive *, void *);
static ssize_t	file_write(struct archive *, void *, const void *buff, size_t);
static int	open_filename(struct archive *, int, const void *);

int
archive_write_open_file(struct archive *a, const char *filename)
{
	return (archive_write_open_filename(a, filename));
}

int
archive_write_open_filename(struct archive *a, const char *filename)
{

	if (filename == NULL || filename[0] == '\0')
		return (archive_write_open_fd(a, 1));

	return (open_filename(a, 1, filename));
}

int
archive_write_open_filename_w(struct archive *a, const wchar_t *filename)
{

	if (filename == NULL || filename[0] == L'\0')
		return (archive_write_open_fd(a, 1));

	return (open_filename(a, 0, filename));
}

static int
open_filename(struct archive *a, int mbs_fn, const void *filename)
{
	struct write_file_data *mine;
	int r;

	mine = (struct write_file_data *)calloc(1, sizeof(*mine));
	if (mine == NULL) {
		archive_set_error(a, ENOMEM, "No memory");
		return (ARCHIVE_FATAL);
	}
	if (mbs_fn)
		r = archive_mstring_copy_mbs(&mine->filename, filename);
	else
		r = archive_mstring_copy_wcs(&mine->filename, filename);
	if (r < 0) {
		if (errno == ENOMEM) {
			archive_set_error(a, ENOMEM, "No memory");
			return (ARCHIVE_FATAL);
		}
		if (mbs_fn)
			archive_set_error(a, ARCHIVE_ERRNO_MISC,
			    "Can't convert '%s' to WCS",
			    (const char *)filename);
		else
			archive_set_error(a, ARCHIVE_ERRNO_MISC,
			    "Can't convert '%S' to MBS",
			    (const wchar_t *)filename);
		return (ARCHIVE_FAILED);
	}
	mine->fd = -1;
	return (archive_write_open(a, mine,
		file_open, file_write, file_close));
}

static int
file_open(struct archive *a, void *client_data)
{
	int flags;
	struct write_file_data *mine;
	struct stat st;
#if defined(_WIN32) && !defined(__CYGWIN__)
	wchar_t *fullpath;
#endif
	const wchar_t *wcs;
	const char *mbs;

	mine = (struct write_file_data *)client_data;
	flags = O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_CLOEXEC;

	/*
	 * Open the file.
	 */
	mbs = NULL; wcs = NULL;
#if defined(_WIN32) && !defined(__CYGWIN__)
	if (archive_mstring_get_wcs(a, &mine->filename, &wcs) != 0) {
		if (errno == ENOMEM)
			archive_set_error(a, errno, "No memory");
		else {
			archive_mstring_get_mbs(a, &mine->filename, &mbs);
			archive_set_error(a, errno,
			    "Can't convert '%s' to WCS", mbs);
		}
		return (ARCHIVE_FATAL);
	}
	fullpath = __la_win_permissive_name_w(wcs);
	if (fullpath != NULL) {
		mine->fd = _wopen(fullpath, flags, 0666);
		free(fullpath);
	} else
		mine->fd = _wopen(wcs, flags, 0666);
#else
	if (archive_mstring_get_mbs(a, &mine->filename, &mbs) != 0) {
		if (errno == ENOMEM)
			archive_set_error(a, errno, "No memory");
		else {
			archive_mstring_get_wcs(a, &mine->filename, &wcs);
			archive_set_error(a, errno,
			    "Can't convert '%S' to MBS", wcs);
		}
		return (ARCHIVE_FATAL);
	}
	mine->fd = open(mbs, flags, 0666);
	__archive_ensure_cloexec_flag(mine->fd);
#endif
	if (mine->fd < 0) {
		if (mbs != NULL)
			archive_set_error(a, errno, "Failed to open '%s'", mbs);
		else
			archive_set_error(a, errno, "Failed to open '%S'", wcs);
		return (ARCHIVE_FATAL);
	}

	if (fstat(mine->fd, &st) != 0) {
		if (mbs != NULL)
			archive_set_error(a, errno, "Couldn't stat '%s'", mbs);
		else
			archive_set_error(a, errno, "Couldn't stat '%S'", wcs);
		return (ARCHIVE_FATAL);
	}

	/*
	 * Set up default last block handling.
	 */
	if (archive_write_get_bytes_in_last_block(a) < 0) {
		if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode) ||
		    S_ISFIFO(st.st_mode))
			/* Pad last block when writing to device or FIFO. */
			archive_write_set_bytes_in_last_block(a, 0);
		else
			/* Don't pad last block otherwise. */
			archive_write_set_bytes_in_last_block(a, 1);
	}

	/*
	 * If the output file is a regular file, don't add it to
	 * itself.  If it's a device file, it's okay to add the device
	 * entry to the output archive.
	 */
	if (S_ISREG(st.st_mode))
		archive_write_set_skip_file(a, st.st_dev, st.st_ino);

	return (ARCHIVE_OK);
}

static ssize_t
file_write(struct archive *a, void *client_data, const void *buff,
    size_t length)
{
	struct write_file_data	*mine;
	ssize_t	bytesWritten;

	mine = (struct write_file_data *)client_data;
	for (;;) {
		bytesWritten = write(mine->fd, buff, length);
		if (bytesWritten <= 0) {
			if (errno == EINTR)
				continue;
			archive_set_error(a, errno, "Write error");
			return (-1);
		}
		return (bytesWritten);
	}
}

static int
file_close(struct archive *a, void *client_data)
{
	struct write_file_data	*mine = (struct write_file_data *)client_data;

	(void)a; /* UNUSED */

	if (mine->fd >= 0)
		close(mine->fd);

	archive_mstring_clean(&mine->filename);
	free(mine);
	return (ARCHIVE_OK);
}
