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

DEFINE_TEST(test_empty_write)
{
	char buff[32768];
	struct archive_entry *ae;
	struct archive *a;
	size_t used;
	int r;

	/*
	 * Exercise a zero-byte write to a gzip-compressed archive.
	 */

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_ustar(a));
	r = archive_write_add_filter_gzip(a);
	if (r != ARCHIVE_OK && !canGzip()) {
		skipping("Empty write to gzip-compressed archive");
	} else {
		if (r != ARCHIVE_OK && canGzip())
			assertEqualIntA(a, ARCHIVE_WARN, r);
		else
			assertEqualIntA(a, ARCHIVE_OK, r);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_open_memory(a, buff, sizeof(buff), &used));
		/* Write a file to it. */
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_copy_pathname(ae, "file");
		archive_entry_set_mode(ae, S_IFREG | 0755);
		archive_entry_set_size(ae, 0);
		assertA(0 == archive_write_header(a, ae));
		archive_entry_free(ae);

		/* THE TEST: write zero bytes to this entry. */
		/* This used to crash. */
		assertEqualIntA(a, 0, archive_write_data(a, "", 0));

		/* Close out the archive. */
		assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	}

	/*
	 * Again, with bzip2 compression.
	 */

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_ustar(a));
	r = archive_write_add_filter_bzip2(a);
	if (r != ARCHIVE_OK && !canBzip2()) {
		skipping("Empty write to bzip2-compressed archive");
	} else {
		if (r != ARCHIVE_OK && canBzip2())
			assertEqualIntA(a, ARCHIVE_WARN, r);
		else
			assertEqualIntA(a, ARCHIVE_OK, r);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_open_memory(a, buff, sizeof(buff), &used));
		/* Write a file to it. */
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_copy_pathname(ae, "file");
		archive_entry_set_mode(ae, S_IFREG | 0755);
		archive_entry_set_size(ae, 0);
		assertA(0 == archive_write_header(a, ae));
		archive_entry_free(ae);

		/* THE TEST: write zero bytes to this entry. */
		assertEqualIntA(a, 0, archive_write_data(a, "", 0));

		/* Close out the archive. */
		assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	}

	/*
	 * For good measure, one more time with no compression.
	 */

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_ustar(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_open_memory(a, buff, sizeof(buff), &used));
	/* Write a file to it. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "file");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 0);
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);

	/* THE TEST: write zero bytes to this entry. */
	assertEqualIntA(a, 0, archive_write_data(a, "", 0));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
}
