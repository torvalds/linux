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

static char buff[1000000];
static char buff2[100000];

DEFINE_TEST(test_read_truncated)
{
	struct archive_entry *ae;
	struct archive *a;
	unsigned int i;
	size_t used;

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_ustar(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_open_memory(a, buff, sizeof(buff), &used));

	/*
	 * Write a file to it.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "file");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	for (i = 0; i < sizeof(buff2); i++)
		buff2[i] = (unsigned char)rand();
	archive_entry_set_size(ae, sizeof(buff2));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, sizeof(buff2), archive_write_data(a, buff2, sizeof(buff2)));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Now, read back a truncated version of the archive and
	 * verify that we get an appropriate error. */
	for (i = 1; i < used + 100; i += 100) {
		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
		if (i < 512) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_open_memory(a, buff, i));
			goto wrap_up;
		} else {
			assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff, i));
		}
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));

		if (i < 512 + sizeof(buff2)) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_data(a, buff2, sizeof(buff2)));
			goto wrap_up;
		} else {
			assertEqualIntA(a, sizeof(buff2), archive_read_data(a, buff2, sizeof(buff2)));
		}

		/* Verify the end of the archive. */
		/* Archive must be long enough to capture a 512-byte
		 * block of zeroes after the entry.  (POSIX requires a
		 * second block of zeros to be written but libarchive
		 * does not return an error if it can't consume
		 * it.) */
		if (i < 512 + 512*((sizeof(buff2) + 511)/512) + 512) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_next_header(a, &ae));
		} else {
			assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
		}
	wrap_up:
		assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}



	/* Same as above, except skip the body instead of reading it. */
	for (i = 1; i < used + 100; i += 100) {
		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
		if (i < 512) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_open_memory(a, buff, i));
			goto wrap_up2;
		} else {
			assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff, i));
		}
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));

		if (i < 512 + 512*((sizeof(buff2)+511)/512)) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_data_skip(a));
			goto wrap_up2;
		} else {
			assertEqualIntA(a, ARCHIVE_OK, archive_read_data_skip(a));
		}

		/* Verify the end of the archive. */
		/* Archive must be long enough to capture a 512-byte
		 * block of zeroes after the entry.  (POSIX requires a
		 * second block of zeros to be written but libarchive
		 * does not return an error if it can't consume
		 * it.) */
		if (i < 512 + 512*((sizeof(buff2) + 511)/512) + 512) {
			assertEqualIntA(a, ARCHIVE_FATAL, archive_read_next_header(a, &ae));
		} else {
			assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
		}
	wrap_up2:
		assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}
}
