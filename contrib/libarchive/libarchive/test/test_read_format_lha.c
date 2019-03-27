/*-
 * Copyright (c) 2008, 2010 Michihiro NAKAJIMA
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
__FBSDID("$FreeBSD");

/*
Execute the following command to rebuild the data for this program:
   tail -n +32 test_read_format_lha.c | /bin/sh

#/bin/sh
#
# How to make test data.
#
# Temporary directory.
base=/tmp/lha
# Owner id
owner=1001
# Group id
group=1001
#
# Make contents of a lha archive.
#
rm -rf ${base}
mkdir ${base}
mkdir ${base}/dir
cat > ${base}/file1 << END
                          file 1 contents
hello
hello
hello
END
cat > ${base}/file2 << END
                          file 2 contents
hello
hello
hello
hello
hello
hello
END
mkdir ${base}/dir2
#
# Set up a file mode, owner and group.
#
(cd ${base}/dir2; ln -s ../file1 symlink1)
(cd ${base}/dir2; ln -s ../file2 symlink2)
(cd ${base}; chown ${owner}:${group} dir file1 file2)
(cd ${base}; chown -h ${owner}:${group} dir2 dir2/symlink1 dir2/symlink2)
(cd ${base}; chmod 0750 dir)
(cd ${base}; chmod 0755 dir2)
(cd ${base}; chmod 0755 dir2/symlink1 dir2/symlink2)
(cd ${base}; chmod 0644 file1)
(cd ${base}; chmod 0666 file2)
TZ=utc touch -afhm -t 197001030000.02 ${base}/dir2/symlink1 ${base}/dir2/symlink2
TZ=utc touch -afhm -t 197001020000.01 ${base}/dir ${base}/dir2
TZ=utc touch -afhm -t 197001020000.01 ${base}/file1 ${base}/file2
#
# Make several lha archives.
#
# Make a lha archive with header level 0
lha0=test_read_format_lha_header0.lzh
(cd ${base}; lha c0q ${lha0} dir file1 file2 dir2) 
# Make a lha archive with header level 1
lha1=test_read_format_lha_header1.lzh
(cd ${base}; lha c1q ${lha1} dir file1 file2 dir2) 
# Make a lha archive with header level 2
lha2=test_read_format_lha_header2.lzh
(cd ${base}; lha c2q ${lha2} dir file1 file2 dir2) 
# Make a lha archive with -lh6- compression mode
lha3=test_read_format_lha_lh6.lzh
(cd ${base}; lha co6q ${lha3} dir file1 file2 dir2) 
# Make a lha archive with -lh7- compression mode
lha4=test_read_format_lha_lh7.lzh
(cd ${base}; lha co7q ${lha4} dir file1 file2 dir2) 
# Make a lha archive with -lh0- no compression
lha5=test_read_format_lha_lh0.lzh
(cd ${base}; lha czq ${lha5} dir file1 file2 dir2) 
# make a lha archive with junk data
lha6=test_read_format_lha_withjunk.lzh
(cd ${base}; cp ${lha2} ${lha6}; echo "junk data!!!!" >> ${lha6})
#
uuencode ${base}/${lha0} ${lha0} > ${lha0}.uu
uuencode ${base}/${lha1} ${lha1} > ${lha1}.uu
uuencode ${base}/${lha2} ${lha2} > ${lha2}.uu
uuencode ${base}/${lha3} ${lha3} > ${lha3}.uu
uuencode ${base}/${lha4} ${lha4} > ${lha4}.uu
uuencode ${base}/${lha5} ${lha5} > ${lha5}.uu
uuencode ${base}/${lha6} ${lha5} > ${lha5}.uu
uuencode ${base}/${lha6} ${lha6} > ${lha6}.uu
#
# Finish making test data.
exit 1
*/

static const char file1[] = {
"                          file 1 contents\n"
"hello\n"
"hello\n"
"hello\n"
};
#define file1_size (sizeof(file1)-1)
static const char file2[] = {
"                          file 2 contents\n"
"hello\n"
"hello\n"
"hello\n"
"hello\n"
"hello\n"
"hello\n"
};
#define file2_size (sizeof(file2)-1)

static void
verify(const char *refname, int posix)
{
	struct archive_entry *ae;
	struct archive *a;
	char buff[128];
	const void *pv;
	size_t s;
	int64_t o;
	int uid, gid;

	if (posix)
		uid = gid = 1001;
	else
		uid = gid = 0;

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_filename(a, refname, 10240));

	/* Verify directory1.  */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	if (posix)
		assertEqualInt((AE_IFDIR | 0750), archive_entry_mode(ae));
	else
		assertEqualInt((AE_IFDIR | 0755), archive_entry_mode(ae));
	assertEqualString("dir/", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(uid, archive_entry_uid(ae));
	assertEqualInt(gid, archive_entry_gid(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, ARCHIVE_EOF,
	    archive_read_data_block(a, &pv, &s, &o));
	assertEqualInt(s, 0);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* Verify directory2.  */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFDIR | 0755), archive_entry_mode(ae));
	assertEqualString("dir2/", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(uid, archive_entry_uid(ae));
	assertEqualInt(gid, archive_entry_gid(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, ARCHIVE_EOF,
	    archive_read_data_block(a, &pv, &s, &o));
	assertEqualInt(s, 0);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	if (posix) {
		/* Verify symbolic link file1. */
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		assertEqualInt((AE_IFLNK | 0755), archive_entry_mode(ae));
		assertEqualString("dir2/symlink1", archive_entry_pathname(ae));
		assertEqualString("../file1", archive_entry_symlink(ae));
		assertEqualInt(172802, archive_entry_mtime(ae));
		assertEqualInt(uid, archive_entry_uid(ae));
		assertEqualInt(gid, archive_entry_gid(ae));
		assertEqualInt(0, archive_entry_size(ae));
		assertEqualInt(archive_entry_is_encrypted(ae), 0);
		assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

		/* Verify symbolic link file2. */
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		assertEqualInt((AE_IFLNK | 0755), archive_entry_mode(ae));
		assertEqualString("dir2/symlink2", archive_entry_pathname(ae));
		assertEqualString("../file2", archive_entry_symlink(ae));
		assertEqualInt(172802, archive_entry_mtime(ae));
		assertEqualInt(uid, archive_entry_uid(ae));
		assertEqualInt(gid, archive_entry_gid(ae));
		assertEqualInt(0, archive_entry_size(ae));
		assertEqualInt(archive_entry_is_encrypted(ae), 0);
		assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
	}

	/* Verify regular file1. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file1", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(uid, archive_entry_uid(ae));
	assertEqualInt(gid, archive_entry_gid(ae));
	assertEqualInt(file1_size, archive_entry_size(ae));
	assertEqualInt(file1_size, archive_read_data(a, buff, file1_size));
	assertEqualMem(buff, file1, file1_size);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* Verify regular file2. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	if (posix)
		assertEqualInt((AE_IFREG | 0666), archive_entry_mode(ae));
	else
		assertEqualInt((AE_IFREG | 0644), archive_entry_mode(ae));
	assertEqualString("file2", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(uid, archive_entry_uid(ae));
	assertEqualInt(gid, archive_entry_gid(ae));
	assertEqualInt(file2_size, archive_entry_size(ae));
	assertEqualInt(file2_size, archive_read_data(a, buff, file2_size));
	assertEqualMem(buff, file2, file2_size);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* Verify the number of files read. */
	if (posix) {
		assertEqualInt(6, archive_file_count(a));
	} else {
		assertEqualInt(4, archive_file_count(a));
	}

	/* End of archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify the number of files read. */
	if (posix) {
		assertEqualInt(6, archive_file_count(a));
	} else {
		assertEqualInt(4, archive_file_count(a));
	}

	/* Verify encryption status */
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* Verify archive format. */
	assertEqualIntA(a, ARCHIVE_FILTER_NONE, archive_filter_code(a, 0));
	assertEqualIntA(a, ARCHIVE_FORMAT_LHA, archive_format(a));

	/* Close the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_read_format_lha)
{
	/* Verify Header level 0 */
	verify("test_read_format_lha_header0.lzh", 1);
	/* Verify Header level 1 */
	verify("test_read_format_lha_header1.lzh", 1);
	/* Verify Header level 2 */
	verify("test_read_format_lha_header2.lzh", 1);
	/* Verify Header level 3
	 * This test data can be made in Windows only. */
	verify("test_read_format_lha_header3.lzh", 0);
	/* Verify compression mode -lh6- */
	verify("test_read_format_lha_lh6.lzh", 1);
	/* Verify compression mode -lh7- */
	verify("test_read_format_lha_lh7.lzh", 1);
	/* Verify no compression -lh0- */
	verify("test_read_format_lha_lh0.lzh", 1);
	/* Verify an lha file with junk data. */
	verify("test_read_format_lha_withjunk.lzh", 1);
}

