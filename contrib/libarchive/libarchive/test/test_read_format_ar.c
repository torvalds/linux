/*-
 * Copyright (c) 2007 Kai Wang
 * Copyright (c) 2007 Tim Kientzle
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


DEFINE_TEST(test_read_format_ar)
{
	char buff[64];
	const char reffile[] = "test_read_format_ar.ar";
	struct archive_entry *ae;
	struct archive *a;

	extract_reference_file(reffile);
	assert((a = archive_read_new()) != NULL);
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_open_filename(a, reffile, 7));

	/* Filename table.  */
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualString("//", archive_entry_pathname(ae));
	assertEqualInt(0, archive_entry_mtime(ae));
	assertEqualInt(0, archive_entry_uid(ae));
	assertEqualInt(0, archive_entry_gid(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* First Entry */
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualString("yyytttsssaaafff.o", archive_entry_pathname(ae));
	assertEqualInt(1175465652, archive_entry_mtime(ae));
	assertEqualInt(1001, archive_entry_uid(ae));
	assertEqualInt(0, archive_entry_gid(ae));
	assert(8 == archive_entry_size(ae));
	assertA(8 == archive_read_data(a, buff, 10));
	assertEqualMem(buff, "55667788", 8);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* Second Entry */
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualString("gghh.o", archive_entry_pathname(ae));
	assertEqualInt(1175465668, archive_entry_mtime(ae));
	assertEqualInt(1001, archive_entry_uid(ae));
	assertEqualInt(0, archive_entry_gid(ae));
	assert(4 == archive_entry_size(ae));
	assertA(4 == archive_read_data(a, buff, 10));
	assertEqualMem(buff, "3333", 4);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* Third Entry */
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualString("hhhhjjjjkkkkllll.o", archive_entry_pathname(ae));
	assertEqualInt(1175465713, archive_entry_mtime(ae));
	assertEqualInt(1001, archive_entry_uid(ae));
	assertEqualInt(0, archive_entry_gid(ae));
	assert(9 == archive_entry_size(ae));
	assertA(9 == archive_read_data(a, buff, 9));
	assertEqualMem(buff, "987654321", 9);

	/* Test EOF */
	assertA(1 == archive_read_next_header(a, &ae));
	assertEqualInt(4, archive_file_count(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}
