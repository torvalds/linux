/*-
 * Copyright (c) 2007 Joerg Sonnenberger
 * Copyright (c) 2012 Michihiro NAKAJIMA
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
__FBSDID("$FreeBSD: head/lib/libarchive/archive_write_set_compression_program.c 201104 2009-12-28 03:14:30Z kientzle $");

#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif
#ifdef HAVE_ERRNO_H
#  include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif
#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#  include <string.h>
#endif

#include "archive.h"
#include "archive_private.h"
#include "archive_string.h"
#include "archive_write_private.h"
#include "filter_fork.h"

#if ARCHIVE_VERSION_NUMBER < 4000000
int
archive_write_set_compression_program(struct archive *a, const char *cmd)
{
	__archive_write_filters_free(a);
	return (archive_write_add_filter_program(a, cmd));
}
#endif

struct archive_write_program_data {
#if defined(_WIN32) && !defined(__CYGWIN__)
	HANDLE		 child;
#else
	pid_t		 child;
#endif
	int		 child_stdin, child_stdout;

	char		*child_buf;
	size_t		 child_buf_len, child_buf_avail;
	char		*program_name;
};

struct private_data {
	struct archive_write_program_data *pdata;
	struct archive_string description;
	char		*cmd;
};

static int archive_compressor_program_open(struct archive_write_filter *);
static int archive_compressor_program_write(struct archive_write_filter *,
		    const void *, size_t);
static int archive_compressor_program_close(struct archive_write_filter *);
static int archive_compressor_program_free(struct archive_write_filter *);

/*
 * Add a filter to this write handle that passes all data through an
 * external program.
 */
int
archive_write_add_filter_program(struct archive *_a, const char *cmd)
{
	struct archive_write_filter *f = __archive_write_allocate_filter(_a);
	struct private_data *data;
	static const char prefix[] = "Program: ";

	archive_check_magic(_a, ARCHIVE_WRITE_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_write_add_filter_program");

	f->data = calloc(1, sizeof(*data));
	if (f->data == NULL)
		goto memerr;
	data = (struct private_data *)f->data;

	data->cmd = strdup(cmd);
	if (data->cmd == NULL)
		goto memerr;

	data->pdata = __archive_write_program_allocate(cmd);
	if (data->pdata == NULL)
		goto memerr;

	/* Make up a description string. */
	if (archive_string_ensure(&data->description,
	    strlen(prefix) + strlen(cmd) + 1) == NULL)
		goto memerr;
	archive_strcpy(&data->description, prefix);
	archive_strcat(&data->description, cmd);

	f->name = data->description.s;
	f->code = ARCHIVE_FILTER_PROGRAM;
	f->open = archive_compressor_program_open;
	f->write = archive_compressor_program_write;
	f->close = archive_compressor_program_close;
	f->free = archive_compressor_program_free;
	return (ARCHIVE_OK);
memerr:
	archive_compressor_program_free(f);
	archive_set_error(_a, ENOMEM,
	    "Can't allocate memory for filter program");
	return (ARCHIVE_FATAL);
}

static int
archive_compressor_program_open(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;

	return __archive_write_program_open(f, data->pdata, data->cmd);
}

static int
archive_compressor_program_write(struct archive_write_filter *f,
    const void *buff, size_t length)
{
	struct private_data *data = (struct private_data *)f->data;

	return __archive_write_program_write(f, data->pdata, buff, length);
}

static int
archive_compressor_program_close(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;

	return __archive_write_program_close(f, data->pdata);
}

static int
archive_compressor_program_free(struct archive_write_filter *f)
{
	struct private_data *data = (struct private_data *)f->data;

	if (data) {
		free(data->cmd);
		archive_string_free(&data->description);
		__archive_write_program_free(data->pdata);
		free(data);
		f->data = NULL;
	}
	return (ARCHIVE_OK);
}

/*
 * Allocate resources for executing an external program.
 */
struct archive_write_program_data *
__archive_write_program_allocate(const char *program)
{
	struct archive_write_program_data *data;

	data = calloc(1, sizeof(struct archive_write_program_data));
	if (data == NULL)
		return (data);
	data->child_stdin = -1;
	data->child_stdout = -1;
	data->program_name = strdup(program);
	return (data);
}

/*
 * Release the resources.
 */
int
__archive_write_program_free(struct archive_write_program_data *data)
{

	if (data) {
#if defined(_WIN32) && !defined(__CYGWIN__)
		if (data->child)
			CloseHandle(data->child);
#endif
		free(data->program_name);
		free(data->child_buf);
		free(data);
	}
	return (ARCHIVE_OK);
}

int
__archive_write_program_open(struct archive_write_filter *f,
    struct archive_write_program_data *data, const char *cmd)
{
	pid_t child;
	int ret;

	ret = __archive_write_open_filter(f->next_filter);
	if (ret != ARCHIVE_OK)
		return (ret);

	if (data->child_buf == NULL) {
		data->child_buf_len = 65536;
		data->child_buf_avail = 0;
		data->child_buf = malloc(data->child_buf_len);

		if (data->child_buf == NULL) {
			archive_set_error(f->archive, ENOMEM,
			    "Can't allocate compression buffer");
			return (ARCHIVE_FATAL);
		}
	}

	child = __archive_create_child(cmd, &data->child_stdin,
		    &data->child_stdout);
	if (child == -1) {
		archive_set_error(f->archive, EINVAL,
		    "Can't launch external program: %s", cmd);
		return (ARCHIVE_FATAL);
	}
#if defined(_WIN32) && !defined(__CYGWIN__)
	data->child = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, child);
	if (data->child == NULL) {
		close(data->child_stdin);
		data->child_stdin = -1;
		close(data->child_stdout);
		data->child_stdout = -1;
		archive_set_error(f->archive, EINVAL,
		    "Can't launch external program: %s", cmd);
		return (ARCHIVE_FATAL);
	}
#else
	data->child = child;
#endif
	return (ARCHIVE_OK);
}

static ssize_t
child_write(struct archive_write_filter *f,
    struct archive_write_program_data *data, const char *buf, size_t buf_len)
{
	ssize_t ret;

	if (data->child_stdin == -1)
		return (-1);

	if (buf_len == 0)
		return (-1);

	for (;;) {
		do {
			ret = write(data->child_stdin, buf, buf_len);
		} while (ret == -1 && errno == EINTR);

		if (ret > 0)
			return (ret);
		if (ret == 0) {
			close(data->child_stdin);
			data->child_stdin = -1;
			fcntl(data->child_stdout, F_SETFL, 0);
			return (0);
		}
		if (ret == -1 && errno != EAGAIN)
			return (-1);

		if (data->child_stdout == -1) {
			fcntl(data->child_stdin, F_SETFL, 0);
			__archive_check_child(data->child_stdin,
				data->child_stdout);
			continue;
		}

		do {
			ret = read(data->child_stdout,
			    data->child_buf + data->child_buf_avail,
			    data->child_buf_len - data->child_buf_avail);
		} while (ret == -1 && errno == EINTR);

		if (ret == 0 || (ret == -1 && errno == EPIPE)) {
			close(data->child_stdout);
			data->child_stdout = -1;
			fcntl(data->child_stdin, F_SETFL, 0);
			continue;
		}
		if (ret == -1 && errno == EAGAIN) {
			__archive_check_child(data->child_stdin,
				data->child_stdout);
			continue;
		}
		if (ret == -1)
			return (-1);

		data->child_buf_avail += ret;

		ret = __archive_write_filter(f->next_filter,
		    data->child_buf, data->child_buf_avail);
		if (ret != ARCHIVE_OK)
			return (-1);
		data->child_buf_avail = 0;
	}
}

/*
 * Write data to the filter stream.
 */
int
__archive_write_program_write(struct archive_write_filter *f,
    struct archive_write_program_data *data, const void *buff, size_t length)
{
	ssize_t ret;
	const char *buf;

	if (data->child == 0)
		return (ARCHIVE_OK);

	buf = buff;
	while (length > 0) {
		ret = child_write(f, data, buf, length);
		if (ret == -1 || ret == 0) {
			archive_set_error(f->archive, EIO,
			    "Can't write to program: %s", data->program_name);
			return (ARCHIVE_FATAL);
		}
		length -= ret;
		buf += ret;
	}
	return (ARCHIVE_OK);
}

/*
 * Finish the filtering...
 */
int
__archive_write_program_close(struct archive_write_filter *f,
    struct archive_write_program_data *data)
{
	int ret, r1, status;
	ssize_t bytes_read;

	if (data->child == 0)
		return __archive_write_close_filter(f->next_filter);

	ret = 0;
	close(data->child_stdin);
	data->child_stdin = -1;
	fcntl(data->child_stdout, F_SETFL, 0);

	for (;;) {
		do {
			bytes_read = read(data->child_stdout,
			    data->child_buf + data->child_buf_avail,
			    data->child_buf_len - data->child_buf_avail);
		} while (bytes_read == -1 && errno == EINTR);

		if (bytes_read == 0 || (bytes_read == -1 && errno == EPIPE))
			break;

		if (bytes_read == -1) {
			archive_set_error(f->archive, errno,
			    "Error reading from program: %s", data->program_name);
			ret = ARCHIVE_FATAL;
			goto cleanup;
		}
		data->child_buf_avail += bytes_read;

		ret = __archive_write_filter(f->next_filter,
		    data->child_buf, data->child_buf_avail);
		if (ret != ARCHIVE_OK) {
			ret = ARCHIVE_FATAL;
			goto cleanup;
		}
		data->child_buf_avail = 0;
	}

cleanup:
	/* Shut down the child. */
	if (data->child_stdin != -1)
		close(data->child_stdin);
	if (data->child_stdout != -1)
		close(data->child_stdout);
	while (waitpid(data->child, &status, 0) == -1 && errno == EINTR)
		continue;
#if defined(_WIN32) && !defined(__CYGWIN__)
	CloseHandle(data->child);
#endif
	data->child = 0;

	if (status != 0) {
		archive_set_error(f->archive, EIO,
		    "Error closing program: %s", data->program_name);
		ret = ARCHIVE_FATAL;
	}
	r1 = __archive_write_close_filter(f->next_filter);
	return (r1 < ret ? r1 : ret);
}

