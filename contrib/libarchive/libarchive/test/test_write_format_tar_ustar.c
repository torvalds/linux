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
is_null(const char *p, size_t l)
{
	while (l > 0) {
		if (*p != '\0')
			return (0);
		--l;
		++p;
	}
	return (1);
}

/* Verify the contents, then erase them to NUL bytes. */
/* Tar requires all "unused" bytes be set to NUL; this allows us
 * to easily verify that by invoking is_null() over the entire header
 * after verifying each field. */
#define myAssertEqualMem(a,b,s) assertEqualMem(a, b, s); memset(a, 0, s)

/*
 * Detailed verification that 'ustar' archives are written with
 * the correct format.
 */
DEFINE_TEST(test_write_format_tar_ustar)
{
	struct archive *a;
	struct archive_entry *entry;
	char *buff, *e;
	size_t buffsize = 100000;
	size_t used;
	int i;
	char f99[100];
	char f100[101];
	char f256[257];

	for (i = 0; i < 99; ++i)
		f99[i] = 'a' + i % 26;
	f99[99] = '\0';

	for (i = 0; i < 100; ++i)
		f100[i] = 'A' + i % 26;
	f100[100] = '\0';

	for (i = 0; i < 256; ++i)
		f256[i] = 'A' + i % 26;
	f256[155] = '/';
	f256[256] = '\0';

	buff = malloc(buffsize);

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_format_ustar(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_add_filter_none(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open_memory(a, buff, buffsize, &used));

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
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_header(a, entry));
	archive_entry_free(entry);
	assertEqualIntA(a, 10, archive_write_data(a, "1234567890", 10));

	/* Hardlink to "file" with 10 bytes of content */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 1, 10);
	archive_entry_set_pathname(entry, "linkfile");
	archive_entry_set_mode(entry, S_IFREG | 0664);
	/* TODO: Put this back and fix the bug. */
	/* archive_entry_set_size(entry, 10); */
	archive_entry_set_uid(entry, 80);
	archive_entry_set_gid(entry, 90);
	archive_entry_set_dev(entry, 12);
	archive_entry_set_ino(entry, 89);
	archive_entry_set_nlink(entry, 2);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_header(a, entry));
	archive_entry_free(entry);
	/* Write of data to dir should fail == zero bytes get written. */
	assertEqualIntA(a, 0, archive_write_data(a, "1234567890", 10));

	/* "dir" */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 2, 20);
	archive_entry_set_pathname(entry, "dir");
	archive_entry_set_mode(entry, S_IFDIR | 0775);
	archive_entry_set_size(entry, 10);
	archive_entry_set_nlink(entry, 2);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_header(a, entry));
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
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_header(a, entry));
	archive_entry_free(entry);
	/* Write of data to symlink should fail == zero bytes get written. */
	assertEqualIntA(a, 0, archive_write_data(a, "1234567890", 10));

	/* file with 99-char filename. */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 1, 10);
	archive_entry_set_pathname(entry, f99);
	archive_entry_set_mode(entry, S_IFREG | 0664);
	archive_entry_set_size(entry, 0);
	archive_entry_set_uid(entry, 82);
	archive_entry_set_gid(entry, 93);
	archive_entry_set_dev(entry, 102);
	archive_entry_set_ino(entry, 7);
	archive_entry_set_nlink(entry, 1);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_header(a, entry));
	archive_entry_free(entry);

	/* file with 100-char filename. */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 1, 10);
	archive_entry_set_pathname(entry, f100);
	archive_entry_set_mode(entry, S_IFREG | 0664);
	archive_entry_set_size(entry, 0);
	archive_entry_set_uid(entry, 82);
	archive_entry_set_gid(entry, 93);
	archive_entry_set_dev(entry, 102);
	archive_entry_set_ino(entry, 7);
	archive_entry_set_nlink(entry, 1);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_header(a, entry));
	archive_entry_free(entry);

	/* file with 256-char filename. */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_mtime(entry, 1, 10);
	archive_entry_set_pathname(entry, f256);
	archive_entry_set_mode(entry, S_IFREG | 0664);
	archive_entry_set_size(entry, 0);
	archive_entry_set_uid(entry, 82);
	archive_entry_set_gid(entry, 93);
	archive_entry_set_dev(entry, 102);
	archive_entry_set_ino(entry, 7);
	archive_entry_set_nlink(entry, 1);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_header(a, entry));
	archive_entry_free(entry);

	/* Close out the archive. */
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/*
	 * Verify the archive format.
	 */
	e = buff;

	/* "file" */
	myAssertEqualMem(e + 0, "file", 5); /* Filename */
	myAssertEqualMem(e + 100, "000664 ", 8); /* mode */
	myAssertEqualMem(e + 108, "000120 ", 8); /* uid */
	myAssertEqualMem(e + 116, "000132 ", 8); /* gid */
	myAssertEqualMem(e + 124, "00000000012 ", 12); /* size */
	myAssertEqualMem(e + 136, "00000000001 ", 12); /* mtime */
	myAssertEqualMem(e + 148, "010034\0 ", 8); /* checksum */
	myAssertEqualMem(e + 156, "0", 1); /* linkflag */
	myAssertEqualMem(e + 157, "", 1); /* linkname */
	myAssertEqualMem(e + 257, "ustar\000000", 8); /* signature/version */
	myAssertEqualMem(e + 265, "", 1); /* uname */
	myAssertEqualMem(e + 297, "", 1); /* gname */
	myAssertEqualMem(e + 329, "000000 ", 8); /* devmajor */
	myAssertEqualMem(e + 337, "000000 ", 8); /* devminor */
	myAssertEqualMem(e + 345, "", 1); /* prefix */
	assert(is_null(e + 0, 512));
	myAssertEqualMem(e + 512, "1234567890", 10);
	assert(is_null(e + 512, 512));
	e += 1024;

	/* hardlink to "file" */
	myAssertEqualMem(e + 0, "linkfile", 9); /* Filename */
	myAssertEqualMem(e + 100, "000664 ", 8); /* mode */
	myAssertEqualMem(e + 108, "000120 ", 8); /* uid */
	myAssertEqualMem(e + 116, "000132 ", 8); /* gid */
	myAssertEqualMem(e + 124, "00000000000 ", 12); /* size */
	myAssertEqualMem(e + 136, "00000000001 ", 12); /* mtime */
	myAssertEqualMem(e + 148, "010707\0 ", 8); /* checksum */
	myAssertEqualMem(e + 156, "0", 1); /* linkflag */
	myAssertEqualMem(e + 157, "", 1); /* linkname */
	myAssertEqualMem(e + 257, "ustar\000000", 8); /* signature/version */
	myAssertEqualMem(e + 265, "", 1); /* uname */
	myAssertEqualMem(e + 297, "", 1); /* gname */
	myAssertEqualMem(e + 329, "000000 ", 8); /* devmajor */
	myAssertEqualMem(e + 337, "000000 ", 8); /* devminor */
	myAssertEqualMem(e + 345, "", 1); /* prefix */
	assert(is_null(e + 0, 512));
	e += 512;

	/* "dir" */
	myAssertEqualMem(e + 0, "dir/", 4); /* Filename */
	myAssertEqualMem(e + 100, "000775 ", 8); /* mode */
	myAssertEqualMem(e + 108, "000000 ", 8); /* uid */
	myAssertEqualMem(e + 116, "000000 ", 8); /* gid */
	myAssertEqualMem(e + 124, "00000000000 ", 12); /* size */
	myAssertEqualMem(e + 136, "00000000002 ", 12); /* mtime */
	myAssertEqualMem(e + 148, "007747\0 ", 8); /* checksum */
	myAssertEqualMem(e + 156, "5", 1); /* typeflag */
	myAssertEqualMem(e + 157, "", 1); /* linkname */
	myAssertEqualMem(e + 257, "ustar\000000", 8); /* signature/version */
	myAssertEqualMem(e + 265, "", 1); /* uname */
	myAssertEqualMem(e + 297, "", 1); /* gname */
	myAssertEqualMem(e + 329, "000000 ", 8); /* devmajor */
	myAssertEqualMem(e + 337, "000000 ", 8); /* devminor */
	myAssertEqualMem(e + 345, "", 1); /* prefix */
	assert(is_null(e + 0, 512));
	e += 512;

	/* "symlink" pointing to "file" */
	myAssertEqualMem(e + 0, "symlink", 8); /* Filename */
	myAssertEqualMem(e + 100, "000664 ", 8); /* mode */
	myAssertEqualMem(e + 108, "000130 ", 8); /* uid */
	myAssertEqualMem(e + 116, "000142 ", 8); /* gid */
	myAssertEqualMem(e + 124, "00000000000 ", 12); /* size */
	myAssertEqualMem(e + 136, "00000000003 ", 12); /* mtime */
	myAssertEqualMem(e + 148, "011446\0 ", 8); /* checksum */
	myAssertEqualMem(e + 156, "2", 1); /* linkflag */
	myAssertEqualMem(e + 157, "file", 5); /* linkname */
	myAssertEqualMem(e + 257, "ustar\000000", 8); /* signature/version */
	myAssertEqualMem(e + 265, "", 1); /* uname */
	myAssertEqualMem(e + 297, "", 1); /* gname */
	myAssertEqualMem(e + 329, "000000 ", 8); /* devmajor */
	myAssertEqualMem(e + 337, "000000 ", 8); /* devminor */
	myAssertEqualMem(e + 345, "", 1); /* prefix */
	assert(is_null(e + 0, 512));
	e += 512;

	/* File with 99-char filename */
	myAssertEqualMem(e + 0, f99, 100); /* Filename */
	myAssertEqualMem(e + 100, "000664 ", 8); /* mode */
	myAssertEqualMem(e + 108, "000122 ", 8); /* uid */
	myAssertEqualMem(e + 116, "000135 ", 8); /* gid */
	myAssertEqualMem(e + 124, "00000000000 ", 12); /* size */
	myAssertEqualMem(e + 136, "00000000001 ", 12); /* mtime */
	myAssertEqualMem(e + 148, "034242\0 ", 8); /* checksum */
	myAssertEqualMem(e + 156, "0", 1); /* linkflag */
	myAssertEqualMem(e + 157, "", 1); /* linkname */
	myAssertEqualMem(e + 257, "ustar\000000", 8); /* signature/version */
	myAssertEqualMem(e + 265, "", 1); /* uname */
	myAssertEqualMem(e + 297, "", 1); /* gname */
	myAssertEqualMem(e + 329, "000000 ", 8); /* devmajor */
	myAssertEqualMem(e + 337, "000000 ", 8); /* devminor */
	myAssertEqualMem(e + 345, "", 1); /* prefix */
	assert(is_null(e + 0, 512));
	e += 512;

	/* File with 100-char filename */
	myAssertEqualMem(e + 0, f100, 100); /* Filename */
	myAssertEqualMem(e + 100, "000664 ", 8); /* mode */
	myAssertEqualMem(e + 108, "000122 ", 8); /* uid */
	myAssertEqualMem(e + 116, "000135 ", 8); /* gid */
	myAssertEqualMem(e + 124, "00000000000 ", 12); /* size */
	myAssertEqualMem(e + 136, "00000000001 ", 12); /* mtime */
	myAssertEqualMem(e + 148, "026230\0 ", 8); /* checksum */
	myAssertEqualMem(e + 156, "0", 1); /* linkflag */
	myAssertEqualMem(e + 157, "", 1); /* linkname */
	myAssertEqualMem(e + 257, "ustar\000000", 8); /* signature/version */
	myAssertEqualMem(e + 265, "", 1); /* uname */
	myAssertEqualMem(e + 297, "", 1); /* gname */
	myAssertEqualMem(e + 329, "000000 ", 8); /* devmajor */
	myAssertEqualMem(e + 337, "000000 ", 8); /* devminor */
	myAssertEqualMem(e + 345, "", 1); /* prefix */
	assert(is_null(e + 0, 512));
	e += 512;

	/* File with 256-char filename */
	myAssertEqualMem(e + 0, f256 + 156, 100); /* Filename */
	myAssertEqualMem(e + 100, "000664 ", 8); /* mode */
	myAssertEqualMem(e + 108, "000122 ", 8); /* uid */
	myAssertEqualMem(e + 116, "000135 ", 8); /* gid */
	myAssertEqualMem(e + 124, "00000000000 ", 12); /* size */
	myAssertEqualMem(e + 136, "00000000001 ", 12); /* mtime */
	myAssertEqualMem(e + 148, "055570\0 ", 8); /* checksum */
	myAssertEqualMem(e + 156, "0", 1); /* linkflag */
	myAssertEqualMem(e + 157, "", 1); /* linkname */
	myAssertEqualMem(e + 257, "ustar\000000", 8); /* signature/version */
	myAssertEqualMem(e + 265, "", 1); /* uname */
	myAssertEqualMem(e + 297, "", 1); /* gname */
	myAssertEqualMem(e + 329, "000000 ", 8); /* devmajor */
	myAssertEqualMem(e + 337, "000000 ", 8); /* devminor */
	myAssertEqualMem(e + 345, f256, 155); /* prefix */
	assert(is_null(e + 0, 512));
	e += 512;

	/* TODO: Verify other types of entries. */

	/* Last entry is end-of-archive marker. */
	assert(is_null(e, 1024));
	e += 1024;

	assertEqualInt((int)used, e - buff);

	free(buff);
}
