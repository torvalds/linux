/*-
 * Copyright (c) 2011 Michihiro NAKAJIMA
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
Execute the following command to rebuild the data for this program:
   tail -n +32 test_read_format_iso_xorriso.c | /bin/sh
#
rm -rf /tmp/iso
mkdir /tmp/iso
mkdir /tmp/iso/dir
mkdir /tmp/iso/dir2
echo "hello" >/tmp/iso/file
ln /tmp/iso/file /tmp/iso/hardlink
(cd /tmp/iso; ln -s file symlink)
TZ=utc touch -afm -t 197001020000.01  /tmp/iso/empty
echo "hello2" >/tmp/iso/dir/file2
echo "hello3" >/tmp/iso/dir/file3
echo "hello4" >/tmp/iso/dir2/file4

TZ=utc touch -afhm -t 197001020000.01 /tmp/iso/dir/file2 /tmp/iso/dir/file3
TZ=utc touch -afhm -t 197001020000.01 /tmp/iso/dir2/file4
TZ=utc touch -afhm -t 197001020000.01 /tmp/iso /tmp/iso/file /tmp/iso/dir
TZ=utc touch -afhm -t 197001020000.01 /tmp/iso/dir2
TZ=utc touch -afhm -t 197001030000.02 /tmp/iso/symlink
F=test_read_format_iso_xorriso.iso
xorriso -outdev - -map /tmp/iso / > $F
compress $F
uuencode $F.Z $F.Z > $F.Z.uu
rm $F.Z
exit 1
 */

/*
 * A test for the iso images made by xorriso which versions are
 * from 0.6.5 to 1.0.1.
 * The xorriso set 0 to the location of empty files(include symlink
 * files) that caused our iso reader could not read following directory
 * entries at all.
 *
 */

DEFINE_TEST(test_read_format_iso_xorriso)
{
	const char *refname = "test_read_format_iso_xorriso.iso.Z";
	struct archive_entry *ae;
	struct archive *a;
	const void *p;
	size_t size;
	int64_t offset;
	int i;

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualInt(0, archive_read_support_filter_all(a));
	assertEqualInt(0, archive_read_support_format_all(a));
	assertEqualInt(ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Retrieve each of the 10 files on the ISO image and
	 * verify that each one is what we expect. */
	for (i = 0; i < 10; ++i) {
		assertEqualInt(0, archive_read_next_header(a, &ae));

		assertEqualInt(archive_entry_is_encrypted(ae), 0);
		assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

		if (strcmp(".", archive_entry_pathname(ae)) == 0) {
			/* '.' root directory. */
			assertEqualInt(AE_IFDIR, archive_entry_filetype(ae));
			assertEqualInt(2048, archive_entry_size(ae));
			/* Now, we read timestamp recorded by RRIP "TF". */
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(0, archive_entry_mtime_nsec(ae));
			/* Now, we read links recorded by RRIP "PX". */
			assertEqualInt(4, archive_entry_nlink(ae));
			assertEqualIntA(a, ARCHIVE_EOF,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt((int)size, 0);
		} else if (strcmp("./dir", archive_entry_pathname(ae)) == 0) {
			/* A directory. */
			assertEqualInt(AE_IFDIR, archive_entry_filetype(ae));
			assertEqualInt(2048, archive_entry_size(ae));
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(86401, archive_entry_atime(ae));
			assertEqualInt(2, archive_entry_nlink(ae));
		} else if (strcmp("./dir2", archive_entry_pathname(ae)) == 0) {
			/* A directory. */
			assertEqualInt(AE_IFDIR, archive_entry_filetype(ae));
			assertEqualInt(2048, archive_entry_size(ae));
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(86401, archive_entry_atime(ae));
			assertEqualInt(2, archive_entry_nlink(ae));
		} else if (strcmp("./file",
		    archive_entry_pathname(ae)) == 0) {
			/* A regular file. */
			assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
			assertEqualInt(6, archive_entry_size(ae));
			assertEqualInt(0,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt(0, offset);
			assertEqualMem(p, "hello\n", 6);
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(86401, archive_entry_atime(ae));
			assertEqualInt(2, archive_entry_nlink(ae));
		} else if (strcmp("./hardlink",
		    archive_entry_pathname(ae)) == 0) {
			/* A hardlink to the regular file. */
			/* Note: If "hardlink" gets returned before "file",
			 * then "hardlink" will get returned as a regular file
			 * and "file" will get returned as the hardlink.
			 * This test should tolerate that, since it's a
			 * perfectly permissible thing for libarchive to do. */
			assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
			assertEqualString("./file", archive_entry_hardlink(ae));
			assertEqualInt(0, archive_entry_size_is_set(ae));
			assertEqualInt(0, archive_entry_size(ae));
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(86401, archive_entry_atime(ae));
			assertEqualInt(2, archive_entry_stat(ae)->st_nlink);
		} else if (strcmp("./symlink",
		    archive_entry_pathname(ae)) == 0) {
			/* A symlink to the regular file. */
			assertEqualInt(AE_IFLNK, archive_entry_filetype(ae));
			assertEqualString("file", archive_entry_symlink(ae));
			assertEqualInt(0, archive_entry_size(ae));
			assertEqualInt(172802, archive_entry_mtime(ae));
			assertEqualInt(172802, archive_entry_atime(ae));
			assertEqualInt(1, archive_entry_stat(ae)->st_nlink);
		} else if (strcmp("./empty",
		    archive_entry_pathname(ae)) == 0) {
			/* A empty file. */
			assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
			assertEqualInt(0, archive_entry_size(ae));
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(86401, archive_entry_atime(ae));
			assertEqualInt(1, archive_entry_nlink(ae));
		} else if (strcmp("./dir/file2",
		    archive_entry_pathname(ae)) == 0) {
			/* A regular file. */
			assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
			assertEqualInt(7, archive_entry_size(ae));
			assertEqualInt(0,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt(0, offset);
			assertEqualMem(p, "hello2\n", 7);
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(86401, archive_entry_atime(ae));
			assertEqualInt(1, archive_entry_nlink(ae));
		} else if (strcmp("./dir/file3",
		    archive_entry_pathname(ae)) == 0) {
			/* A regular file. */
			assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
			assertEqualInt(7, archive_entry_size(ae));
			assertEqualInt(0,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt(0, offset);
			assertEqualMem(p, "hello3\n", 7);
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(86401, archive_entry_atime(ae));
			assertEqualInt(1, archive_entry_nlink(ae));
		} else if (strcmp("./dir2/file4",
		    archive_entry_pathname(ae)) == 0) {
			/* A regular file. */
			assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
			assertEqualInt(7, archive_entry_size(ae));
			assertEqualInt(0,
			    archive_read_data_block(a, &p, &size, &offset));
			assertEqualInt(0, offset);
			assertEqualMem(p, "hello4\n", 7);
			assertEqualInt(86401, archive_entry_mtime(ae));
			assertEqualInt(86401, archive_entry_atime(ae));
			assertEqualInt(1, archive_entry_nlink(ae));
		} else {
			failure("Saw a file that shouldn't have been there");
			assertEqualString(archive_entry_pathname(ae), "");
		}
	}

	/* End of archive. */
	assertEqualInt(ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify archive format. */
	assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_COMPRESS);
	assertEqualInt(archive_format(a), ARCHIVE_FORMAT_ISO9660_ROCKRIDGE);

	/* Close the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}


