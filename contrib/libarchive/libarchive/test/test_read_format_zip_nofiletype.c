/*-
 * Copyright (c) 2013 Tim Kientzle
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
 * Issue 332:  Some epub files (which are really Zip archives) have
 * nonsense in the "external file attributes" field.
 */

DEFINE_TEST(test_read_format_zip_nofiletype)
{
	const char *refname = "test_read_format_zip_nofiletype.zip";
	char *p;
	size_t s;
	struct archive *a;
	struct archive_entry *ae;
	char data[16];

	extract_reference_file(refname);
	p = slurpfile(&s, refname);

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_zip_seekable(a));
	assertEqualIntA(a, ARCHIVE_OK, read_open_memory_seek(a, p, s, 1));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("file1", archive_entry_pathname(ae));
	assertEqualInt(AE_IFREG | 0644, archive_entry_mode(ae));
	assertEqualInt(6, archive_entry_size(ae));
	assertEqualIntA(a, 6, archive_read_data(a, data, 16));
	assertEqualMem(data, "file1\x0a", 6);

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("dir2/", archive_entry_pathname(ae));
	assertEqualInt(AE_IFDIR | 0755, archive_entry_mode(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, 0, archive_read_data(a, data, 16));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
	free(p);
}
