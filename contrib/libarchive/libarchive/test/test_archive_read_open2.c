/*-
 * Copyright (c) 2011 Tim Kientzle
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

static int
open_cb(struct archive *a, void *client)
{
	(void)a; /* UNUSED */
	(void)client; /* UNUSED */
	return 0;
}
static ssize_t
read_cb(struct archive *a, void *client, const void **buff)
{
	(void)a; /* UNUSED */
	(void)client; /* UNUSED */
	(void)buff; /* UNUSED */
	return (ssize_t)0;
}
static int64_t
skip_cb(struct archive *a, void *client, int64_t request)
{
	(void)a; /* UNUSED */
	(void)client; /* UNUSED */
	(void)request; /* UNUSED */
	return (int64_t)0;
}
static int
close_cb(struct archive *a, void *client)
{
	(void)a; /* UNUSED */
	(void)client; /* UNUSED */
	return 0;
}

static void
test(int formatted, archive_open_callback *o, archive_read_callback *r,
    archive_skip_callback *s, archive_close_callback *c,
    int rv, const char *msg)
{
	struct archive* a = archive_read_new();
	if (formatted)
	    assertEqualInt(ARCHIVE_OK,
		archive_read_support_format_empty(a));
	assertEqualInt(rv,
	    archive_read_open2(a, NULL, o, r, s, c));
	assertEqualString(msg, archive_error_string(a));
	archive_read_free(a);
}

DEFINE_TEST(test_archive_read_open2)
{
	const char *no_reader =
	    "No reader function provided to archive_read_open";
	const char *no_formats = "No formats registered";

	test(1, NULL, NULL, NULL, NULL,
	    ARCHIVE_FATAL, no_reader);
	test(1, open_cb, NULL, NULL, NULL,
	    ARCHIVE_FATAL, no_reader);
	test(1, open_cb, read_cb, NULL, NULL,
	    ARCHIVE_OK, NULL);
	test(1, open_cb, read_cb, skip_cb, NULL,
	    ARCHIVE_OK, NULL);
	test(1, open_cb, read_cb, skip_cb, close_cb,
	    ARCHIVE_OK, NULL);
	test(1, NULL, read_cb, skip_cb, close_cb,
	    ARCHIVE_OK, NULL);
	test(1, open_cb, read_cb, skip_cb, NULL,
	    ARCHIVE_OK, NULL);
	test(1, NULL, read_cb, skip_cb, NULL,
	    ARCHIVE_OK, NULL);
	test(1, NULL, read_cb, NULL, NULL,
	    ARCHIVE_OK, NULL);

	test(0, NULL, NULL, NULL, NULL,
	    ARCHIVE_FATAL, no_reader);
	test(0, open_cb, NULL, NULL, NULL,
	    ARCHIVE_FATAL, no_reader);
	test(0, open_cb, read_cb, NULL, NULL,
	    ARCHIVE_FATAL, no_formats);
	test(0, open_cb, read_cb, skip_cb, NULL,
	    ARCHIVE_FATAL, no_formats);
	test(0, open_cb, read_cb, skip_cb, close_cb,
	    ARCHIVE_FATAL, no_formats);
}
