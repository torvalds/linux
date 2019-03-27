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

DEFINE_TEST(test_entry_strmode)
{
	struct archive_entry *entry;

	assert((entry = archive_entry_new()) != NULL);

	archive_entry_set_mode(entry, AE_IFREG | 0642);
	assertEqualString(archive_entry_strmode(entry), "-rw-r---w- ");

	/* Regular file + hardlink still shows as regular file. */
	archive_entry_set_mode(entry, AE_IFREG | 0644);
	archive_entry_set_hardlink(entry, "link");
	assertEqualString(archive_entry_strmode(entry), "-rw-r--r-- ");

	archive_entry_set_mode(entry, 0640);
	archive_entry_set_hardlink(entry, "link");
	assertEqualString(archive_entry_strmode(entry), "hrw-r----- ");
	archive_entry_set_hardlink(entry, NULL);

	archive_entry_set_mode(entry, AE_IFDIR | 0777);
	assertEqualString(archive_entry_strmode(entry), "drwxrwxrwx ");

	archive_entry_set_mode(entry, AE_IFBLK | 03642);
	assertEqualString(archive_entry_strmode(entry), "brw-r-S-wT ");

	archive_entry_set_mode(entry, AE_IFCHR | 05777);
	assertEqualString(archive_entry_strmode(entry), "crwsrwxrwt ");

	archive_entry_set_mode(entry, AE_IFSOCK | 0222);
	assertEqualString(archive_entry_strmode(entry), "s-w--w--w- ");

	archive_entry_set_mode(entry, AE_IFIFO | 0444);
	assertEqualString(archive_entry_strmode(entry), "pr--r--r-- ");

	archive_entry_set_mode(entry, AE_IFLNK | 04000);
	assertEqualString(archive_entry_strmode(entry), "l--S------ ");

	archive_entry_acl_add_entry(entry, ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
	    0007, ARCHIVE_ENTRY_ACL_GROUP, 78, "group78");
	assertEqualString(archive_entry_strmode(entry), "l--S------+");

	/* Release the experimental entry. */
	archive_entry_free(entry);
}
