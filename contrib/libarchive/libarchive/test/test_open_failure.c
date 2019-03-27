/*-
 * Copyright (c) 2003-2010 Tim Kientzle
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

#define MAGIC 123456789
struct my_data {
	int magic;
	int read_return;
	int read_called;
	int write_return;
	int write_called;
	int open_return;
	int open_called;
	int close_return;
	int close_called;
};

static ssize_t
my_read(struct archive *a, void *_private, const void **buff)
{
	struct my_data *private = (struct my_data *)_private;
	(void)a; /* UNUSED */
	(void)buff; /* UNUSED */
	assertEqualInt(MAGIC, private->magic);
	++private->read_called;
	return (private->read_return);
}

static ssize_t
my_write(struct archive *a, void *_private, const void *buff, size_t s)
{
	struct my_data *private = (struct my_data *)_private;
	(void)a; /* UNUSED */
	(void)buff; /* UNUSED */
	(void)s; /* UNUSED */
	assertEqualInt(MAGIC, private->magic);
	++private->write_called;
	return (private->write_return);
}

static int
my_open(struct archive *a, void *_private)
{
	struct my_data *private = (struct my_data *)_private;
	(void)a; /* UNUSED */
	assertEqualInt(MAGIC, private->magic);
	++private->open_called;
	return (private->open_return);
}

static int
my_close(struct archive *a, void *_private)
{
	struct my_data *private = (struct my_data *)_private;
	(void)a; /* UNUSED */
	assertEqualInt(MAGIC, private->magic);
	++private->close_called;
	return (private->close_return);
}


DEFINE_TEST(test_open_failure)
{
	struct archive *a;
	struct my_data private;

	memset(&private, 0, sizeof(private));
	private.magic = MAGIC;
	private.open_return = ARCHIVE_FATAL;
	a = archive_read_new();
	assert(a != NULL);
	assertEqualInt(ARCHIVE_FATAL,
	    archive_read_open(a, &private, my_open, my_read, my_close));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.read_called);
	assertEqualInt(1, private.close_called);
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.read_called);
	assertEqualInt(1, private.close_called);

	memset(&private, 0, sizeof(private));
	private.magic = MAGIC;
	private.open_return = ARCHIVE_FAILED;
	a = archive_read_new();
	assert(a != NULL);
	assertEqualInt(ARCHIVE_FAILED,
	    archive_read_open(a, &private, my_open, my_read, my_close));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.read_called);
	assertEqualInt(1, private.close_called);
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.read_called);
	assertEqualInt(1, private.close_called);

	memset(&private, 0, sizeof(private));
	private.magic = MAGIC;
	private.open_return = ARCHIVE_WARN;
	a = archive_read_new();
	assert(a != NULL);
	assertEqualInt(ARCHIVE_WARN,
	    archive_read_open(a, &private, my_open, my_read, my_close));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.read_called);
	assertEqualInt(1, private.close_called);
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.read_called);
	assertEqualInt(1, private.close_called);

	memset(&private, 0, sizeof(private));
	private.magic = MAGIC;
	private.open_return = ARCHIVE_OK;
	private.read_return = ARCHIVE_FATAL;
	a = archive_read_new();
	assert(a != NULL);
	assertEqualInt(ARCHIVE_OK,
	    archive_read_support_filter_compress(a));
	assertEqualInt(ARCHIVE_OK, archive_read_support_format_tar(a));
	assertEqualInt(ARCHIVE_FATAL,
	    archive_read_open(a, &private, my_open, my_read, my_close));
	assertEqualInt(1, private.open_called);
	assertEqualInt(1, private.read_called);
	assertEqualInt(1, private.close_called);
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	assertEqualInt(1, private.open_called);
	assertEqualInt(1, private.read_called);
	assertEqualInt(1, private.close_called);

	memset(&private, 0, sizeof(private));
	private.magic = MAGIC;
	private.open_return = ARCHIVE_FATAL;
	a = archive_write_new();
	assert(a != NULL);
	assertEqualInt(ARCHIVE_FATAL,
	    archive_write_open(a, &private, my_open, my_write, my_close));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.write_called);
	assertEqualInt(1, private.close_called);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.write_called);
	assertEqualInt(1, private.close_called);

	memset(&private, 0, sizeof(private));
	private.magic = MAGIC;
	private.open_return = ARCHIVE_FATAL;
	a = archive_write_new();
	assert(a != NULL);
	archive_write_add_filter_compress(a);
	archive_write_set_format_ustar(a);
	assertEqualInt(ARCHIVE_FATAL,
	    archive_write_open(a, &private, my_open, my_write, my_close));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.write_called);
	assertEqualInt(1, private.close_called);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.write_called);
	assertEqualInt(1, private.close_called);

	memset(&private, 0, sizeof(private));
	private.magic = MAGIC;
	private.open_return = ARCHIVE_FATAL;
	a = archive_write_new();
	assert(a != NULL);
	archive_write_set_format_zip(a);
	assertEqualInt(ARCHIVE_FATAL,
	    archive_write_open(a, &private, my_open, my_write, my_close));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.write_called);
	assertEqualInt(1, private.close_called);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.write_called);
	assertEqualInt(1, private.close_called);

	memset(&private, 0, sizeof(private));
	private.magic = MAGIC;
	private.open_return = ARCHIVE_FATAL;
	a = archive_write_new();
	assert(a != NULL);
	archive_write_add_filter_gzip(a);
	assertEqualInt(ARCHIVE_FATAL,
	    archive_write_open(a, &private, my_open, my_write, my_close));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.write_called);
	assertEqualInt(1, private.close_called);
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	assertEqualInt(1, private.open_called);
	assertEqualInt(0, private.write_called);
	assertEqualInt(1, private.close_called);

}
