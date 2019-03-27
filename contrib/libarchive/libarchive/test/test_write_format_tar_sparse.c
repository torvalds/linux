/*-
 * Copyright (c) 2003-2010 Tim Kientzle
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
#include "test.h"
__FBSDID("$FreeBSD$");

static char buff[1000000];

static void
test_1(void)
{
	struct archive_entry *ae;
	struct archive *a;
	size_t used;
	size_t blocksize;
	int64_t offset, length;
	char *buff2;
	size_t buff2_size = 0x13000;
	char buff3[1024];
	long i;

	assert((buff2 = malloc(buff2_size)) != NULL);
	/* Repeat the following for a variety of odd blocksizes. */
	for (blocksize = 1; blocksize < 100000; blocksize += blocksize + 3) {
		/* Create a new archive in memory. */
		assert((a = archive_write_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_format_pax(a));
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
		archive_entry_copy_pathname(ae, "file");
		assertEqualString("file", archive_entry_pathname(ae));
		archive_entry_set_mode(ae, S_IFREG | 0755);
		assertEqualInt(S_IFREG | 0755, archive_entry_mode(ae));
		archive_entry_set_size(ae, 0x81000);
		archive_entry_sparse_add_entry(ae, 0x10000, 0x1000);
		archive_entry_sparse_add_entry(ae, 0x80000, 0x1000);

		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		archive_entry_free(ae);
		memset(buff2, 'a', buff2_size);
		for (i = 0; i < 0x81000;) {
			size_t ws = buff2_size;
			if (i + ws > 0x81000)
				ws = 0x81000 - i;
			assertEqualInt(ws,
				archive_write_data(a, buff2, ws));
			i += (long)ws;
		}

		/* Close out the archive. */
		assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));

		/* This calculation gives "the smallest multiple of
		 * the block size that is at least 11264 bytes". */
		failure("blocksize=%d", blocksize);
		assertEqualInt(((11264 - 1)/blocksize+1)*blocksize, used);

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
		assertEqualInt(10, archive_entry_mtime_nsec(ae));
		assertEqualInt(0, archive_entry_atime(ae));
		assertEqualInt(0, archive_entry_ctime(ae));
		assertEqualString("file", archive_entry_pathname(ae));
		assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
		assertEqualInt(AE_IFREG | 0755, archive_entry_mode(ae));
		assertEqualInt(0x81000, archive_entry_size(ae));
		/* Verify sparse information. */
		assertEqualInt(2, archive_entry_sparse_reset(ae));
		assertEqualInt(0,
			archive_entry_sparse_next(ae, &offset, &length));
		assertEqualInt(0x10000, offset);
		assertEqualInt(0x1000, length);
		assertEqualInt(0,
			archive_entry_sparse_next(ae, &offset, &length));
		assertEqualInt(0x80000, offset);
		assertEqualInt(0x1000, length);
		/* Verify file contents. */
		memset(buff3, 0, sizeof(buff3));
		for (i = 0; i < 0x10000; i += 1024) {
			assertEqualInt(1024, archive_read_data(a, buff2, 1024));
			failure("Read data(0x%lx - 0x%lx) should be all zero",
			    i, i + 1024);
			assertEqualMem(buff2, buff3, 1024);
		}
		memset(buff3, 'a', sizeof(buff3));
		for (i = 0x10000; i < 0x11000; i += 1024) {
			assertEqualInt(1024, archive_read_data(a, buff2, 1024));
			failure("Read data(0x%lx - 0x%lx) should be all 'a'",
			    i, i + 1024);
			assertEqualMem(buff2, buff3, 1024);
		}
		memset(buff3, 0, sizeof(buff3));
		for (i = 0x11000; i < 0x80000; i += 1024) {
			assertEqualInt(1024, archive_read_data(a, buff2, 1024));
			failure("Read data(0x%lx - 0x%lx) should be all zero",
			    i, i + 1024);
			assertEqualMem(buff2, buff3, 1024);
		}
		memset(buff3, 'a', sizeof(buff3));
		for (i = 0x80000; i < 0x81000; i += 1024) {
			assertEqualInt(1024, archive_read_data(a, buff2, 1024));
			failure("Read data(0x%lx - 0x%lx) should be all 'a'",
			    i, i + 1024);
			assertEqualMem(buff2, buff3, 1024);
		}

		/* Verify the end of the archive. */
		assertEqualIntA(a, ARCHIVE_EOF,
		    archive_read_next_header(a, &ae));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}
	free(buff2);
}

/*
 * Test for the case the full bytes of sparse file data is not written.
 */
static void
test_2(void)
{
	struct archive_entry *ae;
	struct archive *a;
	size_t used;
	size_t blocksize = 20 * 512;
	int64_t offset, length;
	char *buff2;
	size_t buff2_size = 0x11000;
	char buff3[1024];
	long i;

	assert((buff2 = malloc(buff2_size)) != NULL);
	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_format_pax(a));
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
	archive_entry_copy_pathname(ae, "file");
	assertEqualString("file", archive_entry_pathname(ae));
	archive_entry_set_mode(ae, S_IFREG | 0755);
	assertEqualInt(S_IFREG | 0755, archive_entry_mode(ae));
	archive_entry_set_size(ae, 0x81000);
	archive_entry_sparse_add_entry(ae, 0x10000, 0x1000);
	archive_entry_sparse_add_entry(ae, 0x80000, 0x1000);

	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	memset(buff2, 'a', buff2_size);
	/* Write bytes less than it should be. */
	assertEqualInt(buff2_size, archive_write_data(a, buff2, buff2_size));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* This calculation gives "the smallest multiple of
	 * the block size that is at least 11264 bytes". */
	failure("blocksize=%d", blocksize);
	assertEqualInt(((11264 - 1)/blocksize+1)*blocksize, used);

	/*
	 * Now, read the data back.
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff, used));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));

	assertEqualInt(1, archive_entry_mtime(ae));
	assertEqualInt(10, archive_entry_mtime_nsec(ae));
	assertEqualInt(0, archive_entry_atime(ae));
	assertEqualInt(0, archive_entry_ctime(ae));
	assertEqualString("file", archive_entry_pathname(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(AE_IFREG | 0755, archive_entry_mode(ae));
	assertEqualInt(0x81000, archive_entry_size(ae));
	/* Verify sparse information. */
	assertEqualInt(2, archive_entry_sparse_reset(ae));
	assertEqualInt(0, archive_entry_sparse_next(ae, &offset, &length));
	assertEqualInt(0x10000, offset);
	assertEqualInt(0x1000, length);
	assertEqualInt(0, archive_entry_sparse_next(ae, &offset, &length));
	assertEqualInt(0x80000, offset);
	assertEqualInt(0x1000, length);
	/* Verify file contents. */
	memset(buff3, 0, sizeof(buff3));
	for (i = 0; i < 0x10000; i += 1024) {
		assertEqualInt(1024, archive_read_data(a, buff2, 1024));
		failure("Read data(0x%lx - 0x%lx) should be all zero",
		    i, i + 1024);
		assertEqualMem(buff2, buff3, 1024);
	}
	memset(buff3, 'a', sizeof(buff3));
	for (i = 0x10000; i < 0x11000; i += 1024) {
		assertEqualInt(1024, archive_read_data(a, buff2, 1024));
		failure("Read data(0x%lx - 0x%lx) should be all 'a'",
		    i, i + 1024);
		assertEqualMem(buff2, buff3, 1024);
	}
	memset(buff3, 0, sizeof(buff3));
	for (i = 0x11000; i < 0x80000; i += 1024) {
		assertEqualInt(1024, archive_read_data(a, buff2, 1024));
		failure("Read data(0x%lx - 0x%lx) should be all zero",
		    i, i + 1024);
		assertEqualMem(buff2, buff3, 1024);
	}
	memset(buff3, 0, sizeof(buff3));
	for (i = 0x80000; i < 0x81000; i += 1024) {
		assertEqualInt(1024, archive_read_data(a, buff2, 1024));
		failure("Read data(0x%lx - 0x%lx) should be all 'a'",
		    i, i + 1024);
		assertEqualMem(buff2, buff3, 1024);
	}

	/* Verify the end of the archive. */
	assertEqualIntA(a, ARCHIVE_EOF,
	    archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	free(buff2);
}

DEFINE_TEST(test_write_format_tar_sparse)
{
	/* Test1: archiving sparse files. */
	test_1();
	/* Test2: incompletely archiving sparse files. */
	test_2();
}
