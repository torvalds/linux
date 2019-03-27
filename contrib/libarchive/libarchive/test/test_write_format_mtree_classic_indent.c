/*-
 * Copyright (c) 2012 Michihiro NAKAJIMA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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

static char buff[4096];
static struct {
	const char	*path;
	mode_t		 mode;
	int		 nlink;
	time_t		 mtime;
	uid_t	 	 uid;
	gid_t		 gid;
} entries[] = {
	{ ".",			S_IFDIR | 0755, 3, 1231975636, 1001, 1001 },
	{ "./COPYING",		S_IFREG | 0644, 1, 1231975636, 1001, 1001 },
	{ "./Makefile", 	S_IFREG | 0644, 1, 1233041050, 1001, 1001 },
	{ "./NEWS", 		S_IFREG | 0644, 1, 1231975636, 1001, 1001 },
	{ "./PROJECTS", 	S_IFREG | 0644, 1, 1231975636, 1001, 1001 },
	{ "./README",		S_IFREG | 0644, 1, 1231975636, 1001, 1001 },
	{ "./subdir",		S_IFDIR | 0755, 3, 1233504586, 1001, 1001 },
	{ "./subdir/README",	S_IFREG | 0664, 1, 1231975636, 1002, 1001 },
	{ "./subdir/config",	S_IFREG | 0664, 1, 1232266273, 1003, 1003 },
	{ "./subdir2",		S_IFDIR | 0755, 3, 1233504586, 1001, 1001 },
	{ "./subdir3",		S_IFDIR | 0755, 3, 1233504586, 1001, 1001 },
	{ "./subdir3/mtree",	S_IFREG | 0664, 2, 1232266273, 1003, 1003 },
	{ NULL, 0, 0, 0, 0, 0 }
};

static const char image [] = {
"#mtree\n"
"\n"
"# .\n"
"/set type=file uid=1001 gid=1001 mode=644\n"
".               time=1231975636.0 mode=755 type=dir\n"
"    COPYING         time=1231975636.0 size=8\n"
"    Makefile        time=1233041050.0 size=8\n"
"    NEWS            time=1231975636.0 size=8\n"
"    PROJECTS        time=1231975636.0 size=8\n"
"    README          time=1231975636.0 size=8\n"
"\n"
"# ./subdir\n"
"/set mode=664\n"
"    subdir          time=1233504586.0 mode=755 type=dir\n"
"        README          time=1231975636.0 uid=1002 size=8\n"
"        config          time=1232266273.0 gid=1003 uid=1003 size=8\n"
"    # ./subdir\n"
"    ..\n"
"\n"
"\n"
"# ./subdir2\n"
"    subdir2         time=1233504586.0 mode=755 type=dir\n"
"    # ./subdir2\n"
"    ..\n"
"\n"
"\n"
"# ./subdir3\n"
"    subdir3         time=1233504586.0 mode=755 type=dir\n"
"        mtree           nlink=2 time=1232266273.0 gid=1003 uid=1003 size=8\n"
"    # ./subdir3\n"
"    ..\n"
"\n"
"..\n\n"
};

static const char image_dironly [] = {
"#mtree\n"
"# .\n"
"/set type=dir uid=1001 gid=1001 mode=755\n"
".               time=1231975636.0\n"
"# ./subdir\n"
"    subdir          time=1233504586.0\n"
"    # ./subdir\n"
"    ..\n"
"# ./subdir2\n"
"    subdir2         time=1233504586.0\n"
"    # ./subdir2\n"
"    ..\n"
"# ./subdir3\n"
"    subdir3         time=1233504586.0\n"
"    # ./subdir3\n"
"    ..\n"
"..\n"
};

static void
test_write_format_mtree_sub(int dironly)
{
	struct archive_entry *ae;
	struct archive* a;
	size_t used;
	int i;

	/* Create a mtree format archive. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_mtree_classic(a));
	assertEqualIntA(a, ARCHIVE_OK,
		archive_write_set_format_option(a, NULL, "indent", "1"));
	if (dironly)
		assertEqualIntA(a, ARCHIVE_OK,
			archive_write_set_format_option(a, NULL, "dironly", "1"));
	assertEqualIntA(a, ARCHIVE_OK,
		archive_write_open_memory(a, buff, sizeof(buff)-1, &used));

	/* Write entries */
	for (i = 0; entries[i].path != NULL; i++) {
		assert((ae = archive_entry_new()) != NULL);
		archive_entry_set_nlink(ae, entries[i].nlink);
		assertEqualInt(entries[i].nlink, archive_entry_nlink(ae));
		archive_entry_set_mtime(ae, entries[i].mtime, 0);
		assertEqualInt(entries[i].mtime, archive_entry_mtime(ae));
		archive_entry_set_mode(ae, entries[i].mode);
		assertEqualInt(entries[i].mode, archive_entry_mode(ae));
		archive_entry_set_uid(ae, entries[i].uid);
		assertEqualInt(entries[i].uid, archive_entry_uid(ae));
		archive_entry_set_gid(ae, entries[i].gid);
		assertEqualInt(entries[i].gid, archive_entry_gid(ae));
		archive_entry_copy_pathname(ae, entries[i].path);
		if ((entries[i].mode & AE_IFMT) != S_IFDIR)
			archive_entry_set_size(ae, 8);
		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		if ((entries[i].mode & AE_IFMT) != S_IFDIR)
			assertEqualIntA(a, 8,
			    archive_write_data(a, "Hello012", 15));
		archive_entry_free(ae);
	}
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
        assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	buff[used] = '\0';
	if (dironly)
		assertEqualString(buff, image_dironly);
	else
		assertEqualString(buff, image);

	/*
	 * Read the data and check it.
	 */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff, used));

	/* Read entries */
	for (i = 0; entries[i].path != NULL; i++) {
		if (dironly && (entries[i].mode & AE_IFMT) != S_IFDIR)
			continue;
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		assertEqualInt(entries[i].mtime, archive_entry_mtime(ae));
		assertEqualInt(entries[i].mode, archive_entry_mode(ae));
		assertEqualInt(entries[i].uid, archive_entry_uid(ae));
		assertEqualInt(entries[i].gid, archive_entry_gid(ae));
		if (i > 0)
			assertEqualString(entries[i].path + 2,
				archive_entry_pathname(ae));
		else
			assertEqualString(entries[i].path,
				archive_entry_pathname(ae));
		if ((entries[i].mode & AE_IFMT) != S_IFDIR)
			assertEqualInt(8, archive_entry_size(ae));
	}
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_write_format_mtree_classic_indent)
{
	/* Generate classic format. */
	test_write_format_mtree_sub(0);
	/* Generate classic format and Write directory only. */
	test_write_format_mtree_sub(1);
}
