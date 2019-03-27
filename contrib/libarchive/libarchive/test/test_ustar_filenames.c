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

/*
 * Exercise various lengths of filenames in ustar archives.
 */

static void
test_filename(const char *prefix, int dlen, int flen)
{
	char buff[8192];
	char filename[400];
	char dirname[400];
	struct archive_entry *ae;
	struct archive *a;
	size_t used;
	int separator = 0;
	int i = 0;

	if (prefix != NULL) {
		strcpy(filename, prefix);
		i = (int)strlen(prefix);
	}
	if (dlen > 0) {
		for (; i < dlen; i++)
			filename[i] = 'a';
		filename[i++] = '/';
		separator = 1;
	}
	for (; i < dlen + flen + separator; i++)
		filename[i] = 'b';
	filename[i] = '\0';

	strcpy(dirname, filename);

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_ustar(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_set_bytes_per_block(a,0));
	assertA(0 == archive_write_open_memory(a, buff, sizeof(buff), &used));

	/*
	 * Write a file to it.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, filename);
	archive_entry_set_mode(ae, S_IFREG | 0755);
	failure("dlen=%d, flen=%d", dlen, flen);
	if (flen > 100) {
		assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	} else {
		assertEqualIntA(a, 0, archive_write_header(a, ae));
	}
	archive_entry_free(ae);

	/*
	 * Write a dir to it (without trailing '/').
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, dirname);
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	failure("dlen=%d, flen=%d", dlen, flen);
	if (flen >= 100) {
		assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	} else {
		assertEqualIntA(a, 0, archive_write_header(a, ae));
	}
	archive_entry_free(ae);

	/* Tar adds a '/' to directory names. */
	strcat(dirname, "/");

	/*
	 * Write a dir to it (with trailing '/').
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, dirname);
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	failure("dlen=%d, flen=%d", dlen, flen);
	if (flen >= 100) {
		assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	} else {
		assertEqualIntA(a, 0, archive_write_header(a, ae));
	}
	archive_entry_free(ae);

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/*
	 * Now, read the data back.
	 */
	assert((a = archive_read_new()) != NULL);
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_open_memory(a, buff, used));

	if (flen <= 100) {
		/* Read the file and check the filename. */
		assertA(0 == archive_read_next_header(a, &ae));
		failure("dlen=%d, flen=%d", dlen, flen);
		assertEqualString(filename, archive_entry_pathname(ae));
		assertEqualInt((S_IFREG | 0755), archive_entry_mode(ae));
	}

	/*
	 * Read the two dirs and check the names.
	 *
	 * Both dirs should read back with the same name, since
	 * tar should add a trailing '/' to any dir that doesn't
	 * already have one.
	 */
	if (flen <= 99) {
		assertA(0 == archive_read_next_header(a, &ae));
		assert((S_IFDIR | 0755) == archive_entry_mode(ae));
		failure("dlen=%d, flen=%d", dlen, flen);
		assertEqualString(dirname, archive_entry_pathname(ae));
	}

	if (flen <= 99) {
		assertA(0 == archive_read_next_header(a, &ae));
		assert((S_IFDIR | 0755) == archive_entry_mode(ae));
		assertEqualString(dirname, archive_entry_pathname(ae));
	}

	/* Verify the end of the archive. */
	failure("This fails if entries were written that should not have been written.  dlen=%d, flen=%d", dlen, flen);
	assertEqualInt(1, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_ustar_filenames)
{
	int dlen, flen;

	/* Try a bunch of different file/dir lengths that add up
	 * to just a little less or a little more than 100 bytes.
	 * This exercises the code that splits paths between ustar
	 * filename and prefix fields.
	 */
	for (dlen = 5; dlen < 70; dlen += 5) {
		for (flen = 100 - dlen - 5; flen < 100 - dlen + 5; flen++) {
			test_filename(NULL, dlen, flen);
			test_filename("/", dlen, flen);
		}
	}

	/* Probe the 100-char limit for paths with no '/'. */
	for (flen = 90; flen < 110; flen++) {
		test_filename(NULL, 0, flen);
		test_filename("/", dlen, flen);
	}

	/* XXXX TODO Probe the 100-char limit with a dir prefix. */
	/* XXXX TODO Probe the 255-char total limit. */
}
