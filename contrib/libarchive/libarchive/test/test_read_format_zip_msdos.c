/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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

/*
 * Test archive contains the following entries with only MSDOS attributes:
 *   'abc' -- zero-length file
 *   'def' -- directory without trailing slash and without streaming extension
 *   'def/foo' -- file in def
 *   'ghi/' -- directory with trailing slash and without streaming extension
 *   'jkl'  -- directory without trailing slash and with streaming extension
 *   'mno/' -- directory with trailing slash and streaming extension
 *
 * Seeking reader should identify all of these correctly using the
 * central directory information.
 * Streaming reader should correctly identify everything except 'def';
 * since the standard Zip local file header does not include any file
 * type information, it will be mis-identified as a zero-length file.
 */

static void verify(struct archive *a, int streaming) {
	struct archive_entry *ae;

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("abc", archive_entry_pathname(ae));
	assertEqualInt(AE_IFREG | 0664, archive_entry_mode(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	if (streaming) {
		/* Streaming reader has no basis for making this a dir */
		assertEqualString("def", archive_entry_pathname(ae));
		assertEqualInt(AE_IFREG | 0664, archive_entry_mode(ae));
	} else {
		/* Since 'def' is a dir, '/' should be added */
		assertEqualString("def/", archive_entry_pathname(ae));
		assertEqualInt(AE_IFDIR | 0775, archive_entry_mode(ae));
	}

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("def/foo", archive_entry_pathname(ae));
	assertEqualInt(AE_IFREG | 0664, archive_entry_mode(ae));

	/* Streaming reader can tell this is a dir because it ends in '/' */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("ghi/", archive_entry_pathname(ae));
	assertEqualInt(AE_IFDIR | 0775, archive_entry_mode(ae));

	/* Streaming reader can tell this is a dir because it has xl
	 * extension */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	/* '/' gets added because this is a dir */
	assertEqualString("jkl/", archive_entry_pathname(ae));
	assertEqualInt(AE_IFDIR | 0775, archive_entry_mode(ae));

	/* Streaming reader can tell this is a dir because it ends in
	 * '/' and has xl extension */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("mno/", archive_entry_pathname(ae));
	assertEqualInt(AE_IFDIR | 0775, archive_entry_mode(ae));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
}

DEFINE_TEST(test_read_format_zip_msdos)
{
	const char *refname = "test_read_format_zip_msdos.zip";
	struct archive *a;
	char *p;
	size_t s;

	extract_reference_file(refname);

	/* Verify with seeking reader. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a, refname, 17));
	verify(a, 0);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Verify with streaming reader. */
	p = slurpfile(&s, refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, read_open_memory(a, p, s, 31));
	verify(a, 1);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	
	free(p);
}
