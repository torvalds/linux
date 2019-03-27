/*-
 * Copyright (c) 2003-2011 Tim Kientzle
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

static void
test_empty_file1(void)
{
	struct archive* a = archive_read_new();

	/* Try opening an empty file with the raw handler. */
	assertEqualInt(ARCHIVE_OK, archive_read_support_format_raw(a));
	assertEqualInt(0, archive_errno(a));
	assertEqualString(NULL, archive_error_string(a));

	/* Raw handler doesn't support empty files. */
	assertEqualInt(ARCHIVE_FATAL, archive_read_open_filename(a, "emptyfile", 0));
	assert(NULL != archive_error_string(a));

	archive_read_free(a);
}

static void
test_empty_file2(void)
{
	struct archive* a = archive_read_new();
	struct archive_entry* e;

	/* Try opening an empty file with raw and empty handlers. */
	assertEqualInt(ARCHIVE_OK, archive_read_support_format_raw(a));
	assertEqualInt(ARCHIVE_OK, archive_read_support_format_empty(a));
	assertEqualInt(0, archive_errno(a));
	assertEqualString(NULL, archive_error_string(a));

	assertEqualInt(ARCHIVE_OK, archive_read_open_filename(a, "emptyfile", 0));
	assertEqualInt(0, archive_errno(a));
	assertEqualString(NULL, archive_error_string(a));

	assertEqualInt(ARCHIVE_EOF, archive_read_next_header(a, &e));
	assertEqualInt(0, archive_errno(a));
	assertEqualString(NULL, archive_error_string(a));

	archive_read_free(a);
}

static void
test_empty_tarfile(void)
{
	struct archive* a = archive_read_new();
	struct archive_entry* e;

	/* Try opening an empty file with raw and empty handlers. */
	assertEqualInt(ARCHIVE_OK, archive_read_support_format_tar(a));
	assertEqualInt(0, archive_errno(a));
	assertEqualString(NULL, archive_error_string(a));

	assertEqualInt(ARCHIVE_OK, archive_read_open_filename(a, "empty.tar", 0));
	assertEqualInt(0, archive_errno(a));
	assertEqualString(NULL, archive_error_string(a));

	assertEqualInt(ARCHIVE_EOF, archive_read_next_header(a, &e));
	assertEqualInt(0, archive_errno(a));
	assertEqualString(NULL, archive_error_string(a));

	archive_read_free(a);
}

/* 512 zero bytes. */
static char nulls[512];

DEFINE_TEST(test_archive_read_next_header_empty)
{
	FILE *f;

	/* Create an empty file. */
	f = fopen("emptyfile", "wb");
	fclose(f);

	/* Create a file with 512 zero bytes. */
	f = fopen("empty.tar", "wb");
	assertEqualInt(512, fwrite(nulls, 1, 512, f));
	fclose(f);

	test_empty_file1();
	test_empty_file2();
	test_empty_tarfile();

}
