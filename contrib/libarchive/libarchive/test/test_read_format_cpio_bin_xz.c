/*-
 * Copyright (c) 2009 Michihiro NAKAJIMA
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

static unsigned char archive[] = {
 0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04,
 0xe6, 0xd6, 0xb4, 0x46, 0x02, 0x00, 0x21, 0x01,
 0x16, 0x00, 0x00, 0x00, 0x74, 0x2f, 0xe5, 0xa3,
 0xe0, 0x01, 0xff, 0x00, 0x33, 0x5d, 0x00, 0x63,
 0x9c, 0x3e, 0xa0, 0x43, 0x7c, 0xe6, 0x5d, 0xdc,
 0xeb, 0x76, 0x1d, 0x4b, 0x1b, 0xe2, 0x9e, 0x43,
 0x95, 0x97, 0x60, 0x16, 0x36, 0xc6, 0xd1, 0x3f,
 0x68, 0xd1, 0x94, 0xf9, 0xee, 0x47, 0xbb, 0xc9,
 0xf3, 0xa2, 0x01, 0x2a, 0x2f, 0x2b, 0xb2, 0x23,
 0x5a, 0x06, 0x9c, 0xd0, 0x4a, 0x6b, 0x5b, 0x14,
 0xb4, 0x00, 0x00, 0x00, 0x91, 0x62, 0x1e, 0x15,
 0x04, 0x46, 0x6b, 0x4d, 0x00, 0x01, 0x4f, 0x80,
 0x04, 0x00, 0x00, 0x00, 0xa1, 0x4b, 0xdf, 0x03,
 0xb1, 0xc4, 0x67, 0xfb, 0x02, 0x00, 0x00, 0x00,
 0x00, 0x04, 0x59, 0x5a      
};

DEFINE_TEST(test_read_format_cpio_bin_xz)
{
	struct archive_entry *ae;
	struct archive *a;
	int r;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	r = archive_read_support_filter_xz(a);
	if (r == ARCHIVE_WARN) {
		skipping("xz reading not fully supported on this platform");
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
		return;
	}
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_memory(a, archive, sizeof(archive)));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_XZ);
	assertEqualInt(archive_format(a), ARCHIVE_FORMAT_CPIO_BIN_LE);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

