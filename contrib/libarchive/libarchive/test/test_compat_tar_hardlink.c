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

/*
 * Background:  There are two written standards for the tar file format.
 * The first is the POSIX 1988 "ustar" format, the second is the 2001
 * "pax extended" format that builds on the "ustar" format by adding
 * support for generic additional attributes.  Buried in the details
 * is one frustrating incompatibility:  The 1988 standard says that
 * tar readers MUST ignore the size field on hardlink entries; the
 * 2001 standard says that tar readers MUST obey the size field on
 * hardlink entries.  libarchive tries to navigate this particular
 * minefield by using auto-detect logic to guess whether it should
 * or should not obey the size field.
 *
 * This test tries to probe the boundaries of such handling; the test
 * archives here were adapted from real archives created by real
 * tar implementations that are (as of early 2008) apparently still
 * in use.
 */

static void
test_compat_tar_hardlink_1(void)
{
	char name[] = "test_compat_tar_hardlink_1.tar";
	struct archive_entry *ae;
	struct archive *a;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	extract_reference_file(name);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a, name, 10240));

	/* Read first entry, which is a regular file. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("xmcd-3.3.2/docs_d/READMf",
		archive_entry_pathname(ae));
	assertEqualString(NULL, archive_entry_hardlink(ae));
	assertEqualInt(321, archive_entry_size(ae));
	assertEqualInt(1082575645, archive_entry_mtime(ae));
	assertEqualInt(1851, archive_entry_uid(ae));
	assertEqualInt(3, archive_entry_gid(ae));
	assertEqualInt(0100444, archive_entry_mode(ae));

	/* Read second entry, which is a hard link at the end of archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("xmcd-3.3.2/README",
		archive_entry_pathname(ae));
	assertEqualString(
		"xmcd-3.3.2/docs_d/READMf",
		archive_entry_hardlink(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualInt(1082575645, archive_entry_mtime(ae));
	assertEqualInt(1851, archive_entry_uid(ae));
	assertEqualInt(3, archive_entry_gid(ae));
	assertEqualInt(0100444, archive_entry_mode(ae));

	/* Verify the end-of-archive. */
	/*
	 * This failed in libarchive 2.4.12 because the tar reader
	 * tried to obey the size field for the hard link and ended
	 * up running past the end of the file.
	 */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Verify that the format detection worked. */
	assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_NONE);
	assertEqualInt(archive_format(a), ARCHIVE_FORMAT_TAR);

	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_compat_tar_hardlink)
{
	test_compat_tar_hardlink_1();
}


