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


static int
is_octal(const char *p, size_t l)
{
	while (l > 0) {
		if (*p < '0' || *p > '7')
			return (0);
		--l;
		++p;
	}
	return (1);
}

/*
 * Detailed verification that cpio 'odc' archives are written with
 * the correct format.
 */
DEFINE_TEST(test_write_format_cpio_odc)
{
	struct archive *a;
	struct archive_entry *entry;
	char *buff, *e, *file;
	size_t buffsize = 100000;
	size_t used;

	buff = malloc(buffsize);

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, 0, archive_write_set_format_cpio(a));
	assertEqualIntA(a, 0, archive_write_add_filter_none(a));
	assertEqualIntA(a, 0, archive_write_open_memory(a, buff, buffsize, &used));

	/*
	 * Add various files to it.
	 * TODO: Extend this to cover more filetypes.
	 */

	/* "file" with 10 bytes of content */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 1, 10);
	archive_entry_set_pathname(entry, "file");
	archive_entry_set_mode(entry, S_IFREG | 0664);
	archive_entry_set_size(entry, 10);
	archive_entry_set_uid(entry, 80);
	archive_entry_set_gid(entry, 90);
	archive_entry_set_dev(entry, 12);
	archive_entry_set_ino(entry, 89);
	archive_entry_set_nlink(entry, 2);
	assertEqualIntA(a, 0, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualIntA(a, 10, archive_write_data(a, "1234567890", 10));

	/* Hardlink to "file" with 10 bytes of content */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 1, 10);
	archive_entry_set_pathname(entry, "linkfile");
	archive_entry_set_mode(entry, S_IFREG | 0664);
	archive_entry_set_size(entry, 10);
	archive_entry_set_uid(entry, 80);
	archive_entry_set_gid(entry, 90);
	archive_entry_set_dev(entry, 12);
	archive_entry_set_ino(entry, 89);
	archive_entry_set_nlink(entry, 2);
	assertEqualIntA(a, 0, archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualIntA(a, 10, archive_write_data(a, "1234567890", 10));

	/* "dir" */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 2, 20);
	archive_entry_set_pathname(entry, "dir");
	archive_entry_set_mode(entry, S_IFDIR | 0775);
	archive_entry_set_size(entry, 10);
	archive_entry_set_nlink(entry, 2);
	assertEqualIntA(a, 0, archive_write_header(a, entry));
	archive_entry_free(entry);
	/* Write of data to dir should fail == zero bytes get written. */
	assertEqualIntA(a, 0, archive_write_data(a, "1234567890", 10));

	/* "symlink" pointing to "file" */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 3, 30);
	archive_entry_set_pathname(entry, "symlink");
	archive_entry_set_mode(entry, 0664);
	archive_entry_set_filetype(entry, AE_IFLNK);
	archive_entry_set_symlink(entry,"file");
	archive_entry_set_size(entry, 0);
	archive_entry_set_uid(entry, 88);
	archive_entry_set_gid(entry, 98);
	archive_entry_set_dev(entry, 12);
	archive_entry_set_ino(entry, 90);
	archive_entry_set_nlink(entry, 1);
	assertEqualIntA(a, 0, archive_write_header(a, entry));
	archive_entry_free(entry);
	/* Write of data to symlink should fail == zero bytes get written. */
	assertEqualIntA(a, 0, archive_write_data(a, "1234567890", 10));

	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/*
	 * Verify the archive format.
	 *
	 * Notes on the ino validation: cpio does not actually require
	 * that the ino values written to the archive match those read
	 * from disk.  It really requires that:
	 *   * matching non-zero ino values be written as matching
	 *     non-zero values
	 *   * non-matching non-zero ino values be written as non-matching
	 *     non-zero values
	 * Libarchive further ensures that zero ino values get written
	 * as zeroes.  This allows the cpio writer to generate
	 * synthetic ino values for the archive that may be different
	 * than those on disk in order to avoid problems due to truncation.
	 * This is especially needed for odc (POSIX format) that
	 * only supports 18-bit ino values.
	 */
	e = buff;

	/* "file" */
	file = e; /* Remember where this starts... */
	assert(is_octal(e, 76)); /* Entire header is octal digits. */
	assertEqualMem(e + 0, "070707", 6); /* Magic */
	assertEqualMem(e + 6, "000014", 6); /* dev */
	assert(memcmp(e + 12, "000000", 6) != 0); /* ino must be != 0 */
	assertEqualMem(e + 18, "100664", 6); /* Mode */
	assertEqualMem(e + 24, "000120", 6); /* uid */
	assertEqualMem(e + 30, "000132", 6); /* gid */
	assertEqualMem(e + 36, "000002", 6); /* nlink */
	assertEqualMem(e + 42, "000000", 6); /* rdev */
	assertEqualMem(e + 48, "00000000001", 11); /* mtime */
	assertEqualMem(e + 59, "000005", 6); /* Name size */
	assertEqualMem(e + 65, "00000000012", 11); /* File size */
	assertEqualMem(e + 76, "file\0", 5); /* Name contents */
	assertEqualMem(e + 81, "1234567890", 10); /* File contents */
	e += 91;

	/* hardlink to "file" */
	assert(is_octal(e, 76)); /* Entire header is octal digits. */
	assertEqualMem(e + 0, "070707", 6); /* Magic */
	assertEqualMem(e + 6, "000014", 6); /* dev */
	assertEqualMem(e + 12, file + 12, 6); /* ino must match above */
	assertEqualMem(e + 18, "100664", 6); /* Mode */
	assertEqualMem(e + 24, "000120", 6); /* uid */
	assertEqualMem(e + 30, "000132", 6); /* gid */
	assertEqualMem(e + 36, "000002", 6); /* nlink */
	assertEqualMem(e + 42, "000000", 6); /* rdev */
	assertEqualMem(e + 48, "00000000001", 11); /* mtime */
	assertEqualMem(e + 59, "000011", 6); /* Name size */
	assertEqualMem(e + 65, "00000000012", 11); /* File size */
	assertEqualMem(e + 76, "linkfile\0", 9); /* Name contents */
	assertEqualMem(e + 85, "1234567890", 10); /* File contents */
	e += 95;

	/* "dir" */
	assert(is_octal(e, 76));
	assertEqualMem(e + 0, "070707", 6); /* Magic */
	assertEqualMem(e + 6, "000000", 6); /* dev */
	assertEqualMem(e + 12, "000000", 6); /* ino */
	assertEqualMem(e + 18, "040775", 6); /* Mode */
	assertEqualMem(e + 24, "000000", 6); /* uid */
	assertEqualMem(e + 30, "000000", 6); /* gid */
	assertEqualMem(e + 36, "000002", 6); /* Nlink */
	assertEqualMem(e + 42, "000000", 6); /* rdev */
	assertEqualMem(e + 48, "00000000002", 11); /* mtime */
	assertEqualMem(e + 59, "000004", 6); /* Name size */
	assertEqualMem(e + 65, "00000000000", 11); /* File size */
	assertEqualMem(e + 76, "dir\0", 4); /* name */
	e += 80;

	/* "symlink" pointing to "file" */
	assert(is_octal(e, 76)); /* Entire header is octal digits. */
	assertEqualMem(e + 0, "070707", 6); /* Magic */
	assertEqualMem(e + 6, "000014", 6); /* dev */
	assert(memcmp(e + 12, file + 12, 6) != 0); /* ino must != file ino */
	assert(memcmp(e + 12, "000000", 6) != 0); /* ino must != 0 */
	assertEqualMem(e + 18, "120664", 6); /* Mode */
	assertEqualMem(e + 24, "000130", 6); /* uid */
	assertEqualMem(e + 30, "000142", 6); /* gid */
	assertEqualMem(e + 36, "000001", 6); /* nlink */
	assertEqualMem(e + 42, "000000", 6); /* rdev */
	assertEqualMem(e + 48, "00000000003", 11); /* mtime */
	assertEqualMem(e + 59, "000010", 6); /* Name size */
	assertEqualMem(e + 65, "00000000004", 11); /* File size */
	assertEqualMem(e + 76, "symlink\0", 8); /* Name contents */
	assertEqualMem(e + 84, "file", 4); /* File contents == link target */
	e += 88;

	/* TODO: Verify other types of entries. */

	/* Last entry is end-of-archive marker. */
	assert(is_octal(e, 76));
	assertEqualMem(e + 0, "070707", 6); /* Magic */
	assertEqualMem(e + 6, "000000", 6); /* dev */
	assertEqualMem(e + 12, "000000", 6); /* ino */
	assertEqualMem(e + 18, "000000", 6); /* Mode */
	assertEqualMem(e + 24, "000000", 6); /* uid */
	assertEqualMem(e + 30, "000000", 6); /* gid */
	assertEqualMem(e + 36, "000001", 6); /* Nlink */
	assertEqualMem(e + 42, "000000", 6); /* rdev */
	assertEqualMem(e + 48, "00000000000", 11); /* mtime */
	assertEqualMem(e + 59, "000013", 6); /* Name size */
	assertEqualMem(e + 65, "00000000000", 11); /* File size */
	assertEqualMem(e + 76, "TRAILER!!!\0", 11); /* Name */
	e += 87;

	assertEqualInt((int)used, e - buff);

	free(buff);
}
