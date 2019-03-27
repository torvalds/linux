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

#define BUFF_SIZE 1000000
#define FILE_BUFF_SIZE 100000

DEFINE_TEST(test_read_extract)
{
	struct archive_entry *ae;
	struct archive *a;
	size_t used;
	int i, numEntries = 0;
	char *buff, *file_buff;

	buff = malloc(BUFF_SIZE);
	file_buff = malloc(FILE_BUFF_SIZE);

	/* Force the umask to something predictable. */
	assertUmask(022);

	/* Create a new archive in memory containing various types of entries. */
	assert((a = archive_write_new()) != NULL);
	assertA(0 == archive_write_set_format_ustar(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_open_memory(a, buff, BUFF_SIZE, &used));
	/* A directory to be restored with EXTRACT_PERM. */
	++numEntries;
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir_0775");
	archive_entry_set_mode(ae, S_IFDIR | 0775);
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);
	/* A regular file. */
	++numEntries;
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "file");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	for (i = 0; i < FILE_BUFF_SIZE; i++)
		file_buff[i] = (unsigned char)rand();
	archive_entry_set_size(ae, FILE_BUFF_SIZE);
	assertA(0 == archive_write_header(a, ae));
	assertA(FILE_BUFF_SIZE == archive_write_data(a, file_buff, FILE_BUFF_SIZE));
	archive_entry_free(ae);
	/* A directory that should obey umask when restored. */
	++numEntries;
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir");
	archive_entry_set_mode(ae, S_IFDIR | 0777);
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);
	/* A file in the directory. */
	++numEntries;
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir/file");
	archive_entry_set_mode(ae, S_IFREG | 0700);
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);
	/* A file in a dir that is not already in the archive. */
	++numEntries;
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir2/file");
	archive_entry_set_mode(ae, S_IFREG | 0000);
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);
	/* A dir with a trailing /. */
	++numEntries;
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir3/.");
	archive_entry_set_mode(ae, S_IFDIR | 0710);
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);
	/* Multiple dirs with a single entry. */
	++numEntries;
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir4/a/../b/../c/");
	archive_entry_set_mode(ae, S_IFDIR | 0711);
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);
	/* A symlink. */
	if (canSymlink()) {
		++numEntries;
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_copy_pathname(ae, "symlink");
		archive_entry_set_mode(ae, AE_IFLNK | 0755);
		archive_entry_set_symlink(ae, "file");
		assertA(0 == archive_write_header(a, ae));
		archive_entry_free(ae);
	}
	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Extract the entries to disk. */
	assert((a = archive_read_new()) != NULL);
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_open_memory(a, buff, BUFF_SIZE));
	/* Restore first entry with _EXTRACT_PERM. */
	failure("Error reading first entry", i);
	assertA(0 == archive_read_next_header(a, &ae));
	assertA(0 == archive_read_extract(a, ae, ARCHIVE_EXTRACT_PERM));
	/* Rest of entries get restored with no flags. */
	for (i = 1; i < numEntries; i++) {
		failure("Error reading entry %d", i);
		assertA(0 == archive_read_next_header(a, &ae));
		failure("Failed to extract entry %d: %s", i,
			archive_entry_pathname(ae));
		assertA(0 == archive_read_extract(a, ae, 0));
	}
	assertA(ARCHIVE_EOF == archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Test the entries on disk. */
	/* This first entry was extracted with ARCHIVE_EXTRACT_PERM,
	 * so the permissions should have been restored exactly,
	 * including resetting the gid bit on those platforms
	 * where gid is inherited by subdirs. */
	failure("This was 0775 in archive, and should be 0775 on disk");
	assertIsDir("dir_0775", 0775);
	/* Everything else was extracted without ARCHIVE_EXTRACT_PERM,
	 * so there may be some sloppiness about gid bits on directories. */
	assertIsReg("file", 0755);
	assertFileSize("file", FILE_BUFF_SIZE);
	assertFileContents(file_buff, FILE_BUFF_SIZE, "file");
	/* If EXTRACT_PERM wasn't used, be careful to ignore sgid bit
	 * when checking dir modes, as some systems inherit sgid bit
	 * from the parent dir. */
	failure("This was 0777 in archive, but umask should make it 0755");
	assertIsDir("dir", 0755);
	assertIsReg("dir/file", 0700);
	assertIsDir("dir2", 0755);
	assertIsReg("dir2/file", 0000);
	assertIsDir("dir3", 0710);
	assertIsDir("dir4", 0755);
	assertIsDir("dir4/a", 0755);
	assertIsDir("dir4/b", 0755);
	assertIsDir("dir4/c", 0711);
	if (canSymlink())
		assertIsSymlink("symlink", "file");

	free(buff);
	free(file_buff);
}
