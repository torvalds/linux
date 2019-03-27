/*-
 * Copyright (c) 2003-2008 Tim Kientzle
 * Copyright (c) 2009,2010 Michihiro NAKAJIMA
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

static char buff2[64];
DEFINE_TEST(test_write_format_iso9660)
{
	size_t buffsize = 1000000;
	char *buff;
	struct archive_entry *ae;
	struct archive *a;
	char dirname[1024];
	char dir[6];
	char longname[] =
	    "longname00aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
	    "cccccccccccccccccccccccccccccccccccccccccccccccccc"
	    "dddddddddddddddddddddddddddddddddddddddddddddddddd";

	size_t used;
	int i;

	buff = malloc(buffsize); /* million bytes of work area */
	assert(buff != NULL);

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_iso9660(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_open_memory(a, buff, buffsize, &used));

	/*
	 * "file" has a bunch of attributes and 8 bytes of data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "file");
	archive_entry_set_mode(ae, AE_IFREG | 0755);
	archive_entry_set_size(ae, 8);
	archive_entry_set_nlink(ae, 2);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 8, archive_write_data(a, "12345678", 9));

	/*
	 * "hardlnk" has linked to "file".
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "hardlnk");
	archive_entry_set_mode(ae, AE_IFREG | 0755);
	archive_entry_set_hardlink(ae, "file");
	archive_entry_set_nlink(ae, 2);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/*
	 * longname is similar but has birthtime later than mtime.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 8, 80);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, longname);
	archive_entry_set_mode(ae, AE_IFREG | 0666);
	archive_entry_set_size(ae, 8);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 8, archive_write_data(a, "12345678", 9));

	/*
	 * "symlnk has symbolic linked to longname.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "symlnk");
	archive_entry_set_mode(ae, AE_IFLNK | 0555);
	archive_entry_set_symlink(ae, longname);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/*
	 * "dir*" has a bunch of attributes.
	 */
	dirname[0] = '\0';
	strcpy(dir, "/dir0");
	for (i = 0; i < 13; i++) {
		dir[4] = "0123456789ABCDEF"[i];
		if (i == 0)
			strcat(dirname, dir+1);
		else
			strcat(dirname, dir);
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_set_atime(ae, 2, 20);
		archive_entry_set_birthtime(ae, 3, 30);
		archive_entry_set_ctime(ae, 4, 40);
		archive_entry_set_mtime(ae, 5, 50);
		archive_entry_copy_pathname(ae, dirname);
		archive_entry_set_mode(ae, S_IFDIR | 0755);
		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		archive_entry_free(ae);
	}

	strcat(dirname, "/file");
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, dirname);
	archive_entry_set_mode(ae, S_IFREG | 0755);
	archive_entry_set_size(ae, 8);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 8, archive_write_data(a, "12345678", 9));

	/*
	 * "dir0/dir1/file1" has 8 bytes of data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "dir0/dir1/file1");
	archive_entry_set_mode(ae, AE_IFREG | 0755);
	archive_entry_set_size(ae, 8);
	archive_entry_set_nlink(ae, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 8, archive_write_data(a, "12345678", 9));

	/*
	 * "dir0/dir1/file2" has 8 bytes of data.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "dir0/dir1/file2");
	archive_entry_set_mode(ae, AE_IFREG | 0755);
	archive_entry_set_size(ae, 8);
	archive_entry_set_nlink(ae, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 8, archive_write_data(a, "12345678", 9));

	/*
	 * Add a wrong path "dir0/dir1/file2/wrong"
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 20);
	archive_entry_set_birthtime(ae, 3, 30);
	archive_entry_set_ctime(ae, 4, 40);
	archive_entry_set_mtime(ae, 5, 50);
	archive_entry_copy_pathname(ae, "dir0/dir1/file2/wrong");
	archive_entry_set_mode(ae, AE_IFREG | 0755);
	archive_entry_set_size(ae, 8);
	archive_entry_set_nlink(ae, 1);
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_free(a));

	/*
	 * -----------------------------------------------------------
	 * Now, read the data back(read Rockridge extensions).
	 * -----------------------------------------------------------
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, 0, archive_read_support_format_all(a));
	assertEqualIntA(a, 0, archive_read_support_filter_all(a));
	assertEqualIntA(a, 0, archive_read_open_memory(a, buff, used));

	/*
	 * Read Root Directory
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_ctime(ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_mtime(ae));
	assertEqualString(".", archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0", archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1", archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2", archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB/dirC"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB/dirC",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0555) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "hardlnk"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("hardlnk", archive_entry_pathname(ae));
	assert((AE_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(2, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "file"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file", archive_entry_pathname(ae));
	assertEqualString("hardlnk", archive_entry_hardlink(ae));
	assert((AE_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(2, archive_entry_nlink(ae));
	assertEqualInt(0, archive_entry_size(ae));

	/*
	 * Read longname
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assert(archive_entry_atime_is_set(ae));
	assertEqualInt(2, archive_entry_atime(ae));
	/* Birthtime > mtime above, so it doesn't get stored at all. */
	assert(!archive_entry_birthtime_is_set(ae));
	assertEqualInt(0, archive_entry_birthtime(ae));
	assert(archive_entry_ctime_is_set(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assert(archive_entry_mtime_is_set(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString(longname, archive_entry_pathname(ae));
#if !defined(_WIN32) && !defined(__CYGWIN__)
	assert((AE_IFREG | 0444) == archive_entry_mode(ae));
#else
	/* On Windows and CYGWIN, always set all exec bit ON by default. */ 
	assert((AE_IFREG | 0555) == archive_entry_mode(ae));
#endif
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB/dirC/file"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB/dirC/file", archive_entry_pathname(ae));
	assert((AE_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/file1"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/file1", archive_entry_pathname(ae));
	assert((AE_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/file2"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assertEqualInt(3, archive_entry_birthtime(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/file2", archive_entry_pathname(ae));
	assert((AE_IFREG | 0555) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "symlnk"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assert(archive_entry_atime_is_set(ae));
	assertEqualInt(2, archive_entry_atime(ae));
	assert(!archive_entry_birthtime_is_set(ae));
	assertEqualInt(0, archive_entry_birthtime(ae));
	assert(archive_entry_ctime_is_set(ae));
	assertEqualInt(4, archive_entry_ctime(ae));
	assert(archive_entry_mtime_is_set(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("symlnk", archive_entry_pathname(ae));
	assert((AE_IFLNK | 0555) == archive_entry_mode(ae));
	assertEqualString(longname, archive_entry_symlink(ae));

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	/*
	 * -----------------------------------------------------------
	 * Now, read the data back without Rockridge option
	 * (read Joliet extensions).
	 * -----------------------------------------------------------
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, 0, archive_read_support_format_all(a));
	assertEqualIntA(a, 0, archive_read_support_filter_all(a));
	/* Disable Rockridge extensions support. */
        assertEqualInt(ARCHIVE_OK,
            archive_read_set_option(a, NULL, "rockridge", NULL));
	assertEqualIntA(a, 0, archive_read_open_memory(a, buff, used));

	/*
	 * Read Root Directory
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_ctime(ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_mtime(ae));
	assertEqualString(".", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB/dirC"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB/dirC",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "file"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("file", archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "hardlnk"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("hardlnk", archive_entry_pathname(ae));
	assertEqualString("file", archive_entry_hardlink(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(2, archive_entry_nlink(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, 0, archive_read_data(a, buff2, 10));

	/*
	 * Read longname
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assert(archive_entry_atime_is_set(ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assert(archive_entry_ctime_is_set(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assert(archive_entry_mtime_is_set(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	/* Trim longname to 64 characters. */
	longname[64] = '\0';
	assertEqualString(longname, archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB/dirC/file"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString(
		"dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7/dir8/dir9/dirA/dirB/dirC/file",
		archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/file1"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/file1", archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/file2"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("dir0/dir1/file2", archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "symlnk"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assert(archive_entry_atime_is_set(ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assert(!archive_entry_birthtime_is_set(ae));
	assertEqualInt(0, archive_entry_birthtime(ae));
	assert(archive_entry_ctime_is_set(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assert(archive_entry_mtime_is_set(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(0, archive_entry_size(ae));

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	/*
	 * -----------------------------------------------------------
	 * Now, read the data back without Rockridge and Joliet option
	 * (read ISO9660).
	 * This mode appears rr_moved directory.
	 * -----------------------------------------------------------
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, 0, archive_read_support_format_all(a));
	assertEqualIntA(a, 0, archive_read_support_filter_all(a));
	/* Disable Rockridge and Joliet extensions support. */
        assertEqualInt(ARCHIVE_OK,
            archive_read_set_option(a, NULL, "rockridge", NULL));
        assertEqualInt(ARCHIVE_OK,
            archive_read_set_option(a, NULL, "joliet", NULL));
	assertEqualIntA(a, 0, archive_read_open_memory(a, buff, used));

	/*
	 * Read Root Directory
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_ctime(ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_mtime(ae));
	assertEqualString(".", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "rr_moved"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_ctime(ae));
	assertEqualInt(archive_entry_atime(ae), archive_entry_mtime(ae));
	assertEqualString("RR_MOVED", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "rr_moved/dir7"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("RR_MOVED/DIR7", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "rr_moved/dir7/dir8"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("RR_MOVED/DIR7/DIR8", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "rr_moved/dir7/dir8/dir9"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("RR_MOVED/DIR7/DIR8/DIR9",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "rr_moved/dir7/dir8/dir9/dira"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("RR_MOVED/DIR7/DIR8/DIR9/DIRA",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "rr_moved/dir7/dir8/dir9/dira/dirB"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("RR_MOVED/DIR7/DIR8/DIR9/DIRA/DIRB",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "rr_moved/dir7/dir8/dir9/dirA/dirB/dirC"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("RR_MOVED/DIR7/DIR8/DIR9/DIRA/DIRB/DIRC",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1/DIR2", archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1/DIR2/DIR3",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1/DIR2/DIR3/DIR4",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1/DIR2/DIR3/DIR4/DIR5",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1/DIR2/DIR3/DIR4/DIR5/DIR6",
	    archive_entry_pathname(ae));
	assert((S_IFDIR | 0700) == archive_entry_mode(ae));
	assertEqualInt(2048, archive_entry_size(ae));

	/*
	 * Read "hardlink"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("HARDLNK", archive_entry_pathname(ae));
	assertEqualString(NULL, archive_entry_hardlink(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "file"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(0, archive_entry_birthtime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("FILE", archive_entry_pathname(ae));
	assertEqualString("HARDLNK", archive_entry_hardlink(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(2, archive_entry_nlink(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, 0, archive_read_data(a, buff2, 10));


	/*
	 * Read longname
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assert(archive_entry_atime_is_set(ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assert(archive_entry_ctime_is_set(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assert(archive_entry_mtime_is_set(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("LONGNAME", archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "rr_moved/dir7/dir8/dir9/dirA/dirB/dirC/file"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString(
		"RR_MOVED/DIR7/DIR8/DIR9/DIRA/DIRB/DIRC/FILE",
		archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/file1"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1/FILE1", archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/file2"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1/FILE2", archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(1, archive_entry_nlink(ae));
	assertEqualInt(8, archive_entry_size(ae));
	assertEqualIntA(a, 8, archive_read_data(a, buff2, 10));
	assertEqualMem(buff2, "12345678", 8);

	/*
	 * Read "dir0/dir1/dir2/dir3/dir4/dir5/dir6/dir7" as file
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("DIR0/DIR1/DIR2/DIR3/DIR4/DIR5/DIR6/DIR7",
	    archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(0, archive_entry_size(ae));

	/*
	 * Read "symlnk"
	 */
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assert(archive_entry_atime_is_set(ae));
	assertEqualInt(5, archive_entry_atime(ae));
	assert(!archive_entry_birthtime_is_set(ae));
	assertEqualInt(0, archive_entry_birthtime(ae));
	assert(archive_entry_ctime_is_set(ae));
	assertEqualInt(5, archive_entry_ctime(ae));
	assert(archive_entry_mtime_is_set(ae));
	assertEqualInt(5, archive_entry_mtime(ae));
	assertEqualString("SYMLNK", archive_entry_pathname(ae));
	assert((AE_IFREG | 0400) == archive_entry_mode(ae));
	assertEqualInt(0, archive_entry_size(ae));

	/*
	 * Verify the end of the archive.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	free(buff);
}
