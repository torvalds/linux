/*-
 * Copyright (c) 2014 Tim Kientzle
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

static void
verify_zip_filesize(uint64_t size, int expected)
{
	struct archive *a;
	struct archive_entry *ae;
	char buff[256];
	size_t used;

	/* Zip format: Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_zip(a));
	/* Disable Zip64 extensions. */
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_format_option(a, "zip", "zip64", NULL));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_pathname(ae, "test");
	archive_entry_set_mode(ae, AE_IFREG | 0644);
	archive_entry_set_size(ae, size);
	assertEqualInt(expected, archive_write_header(a, ae));

	archive_entry_free(ae);

	/* Don't actually write 4GB! ;-) */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_free(a));
}

DEFINE_TEST(test_write_format_zip_zip64_oversize)
{
	/* With Zip64 extensions disabled, we should be
	 * able to write a file with at most 4G-1 bytes. */

	/* Note:  Tar writer pads file to declared size when the file
	 * is closed.  If Zip writer is changed to behave the same
	 * way, it will be much harder to test the first case here. */
	verify_zip_filesize(0xffffffffLL, ARCHIVE_OK);

	verify_zip_filesize(0x100000000LL, ARCHIVE_FAILED);
}
