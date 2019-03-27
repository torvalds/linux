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

static char buff[1000000];
static char buff2[64];

DEFINE_TEST(test_write_format_tar)
{
	struct archive_entry *ae;
	struct archive *a;
	char *p;
	size_t used;
	size_t blocksize;

	/* Repeat the following for a variety of odd blocksizes. */
	for (blocksize = 1; blocksize < 100000; blocksize += blocksize + 3) {
		/* Create a new archive in memory. */
		assert((a = archive_write_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_format_ustar(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_add_filter_none(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_bytes_per_block(a, (int)blocksize));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_bytes_in_last_block(a, (int)blocksize));
		assertEqualInt(blocksize,
		    archive_write_get_bytes_in_last_block(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_open_memory(a, buff, sizeof(buff), &used));
		assertEqualInt(blocksize,
		    archive_write_get_bytes_in_last_block(a));

		/*
		 * Write a file to it.
		 */
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_set_mtime(ae, 1, 10);
		assertEqualInt(1, archive_entry_mtime(ae));
		assertEqualInt(10, archive_entry_mtime_nsec(ae));
		p = strdup("file");
		archive_entry_copy_pathname(ae, p);
		strcpy(p, "XXXX");
		free(p);
		assertEqualString("file", archive_entry_pathname(ae));
		archive_entry_set_mode(ae, S_IFREG | 0755);
		assertEqualInt(S_IFREG | 0755, archive_entry_mode(ae));
		archive_entry_set_size(ae, 8);

		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		archive_entry_free(ae);
		assertEqualInt(8, archive_write_data(a, "12345678", 9));

		/* Close out the archive. */
		assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));

		/* This calculation gives "the smallest multiple of
		 * the block size that is at least 2048 bytes". */
		failure("blocksize=%d", blocksize);
		assertEqualInt(((2048 - 1)/blocksize+1)*blocksize, used);

		/*
		 * Now, read the data back.
		 */
		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_support_format_all(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_support_filter_all(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_open_memory(a, buff, used));

		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae));

		assertEqualInt(1, archive_entry_mtime(ae));
		/* Not the same as above: ustar doesn't store hi-res times. */
		assertEqualInt(0, archive_entry_mtime_nsec(ae));
		assertEqualInt(0, archive_entry_atime(ae));
		assertEqualInt(0, archive_entry_ctime(ae));
		assertEqualString("file", archive_entry_pathname(ae));
		assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
		assertEqualInt(AE_IFREG | 0755, archive_entry_mode(ae));
		assertEqualInt(8, archive_entry_size(ae));
		assertEqualInt(8, archive_read_data(a, buff2, 10));
		assertEqualMem(buff2, "12345678", 8);

		/* Verify the end of the archive. */
		assertEqualIntA(a, ARCHIVE_EOF,
		    archive_read_next_header(a, &ae));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}
}
