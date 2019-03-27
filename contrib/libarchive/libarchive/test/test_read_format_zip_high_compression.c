/*-
 * Copyright (c) 2016 Tim Kientzle
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

#include <locale.h>


/*
 * Github Issue 748 reported problems with end-of-entry handling
 * with highly-compressible data.  This resulted in the end of the
 * data being truncated (extracted as zero bytes).
 */

/*
 * Extract the specific test archive that was used to diagnose
 * Issue 748:
 */
DEFINE_TEST(test_read_format_zip_high_compression)
{
	const char *refname = "test_read_format_zip_high_compression.zip";
	char *p;
	size_t archive_size;
	struct archive *a;
	struct archive_entry *entry;

	const void *pv;
	size_t s;
	int64_t o;

	if (archive_zlib_version() == NULL) {
		skipping("Zip compression test requires zlib");
		return;
	}

	extract_reference_file(refname);
	p = slurpfile(&archive_size, refname);

	assert((a = archive_read_new()) != NULL);
        assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_zip(a));
        assertEqualIntA(a, ARCHIVE_OK, read_open_memory_seek(a, p, archive_size, 16 * 1024));
	assertEqualInt(ARCHIVE_OK, archive_read_next_header(a, &entry));

	assertEqualInt(ARCHIVE_OK, archive_read_data_block(a, &pv, &s, &o));
	assertEqualInt(262144, s);
	assertEqualInt(0, o);

	assertEqualInt(ARCHIVE_OK, archive_read_data_block(a, &pv, &s, &o));
	assertEqualInt(160, s);
	assertEqualInt(262144, o);

	assertEqualInt(ARCHIVE_EOF, archive_read_data_block(a, &pv, &s, &o));

	assertEqualInt(ARCHIVE_OK, archive_free(a));
	free(p);
}

/*
 * Synthesize a lot of varying inputs that are highly compressible.
 */
DEFINE_TEST(test_read_format_zip_high_compression2)
{
	const size_t body_size = 1024 * 1024;
	const size_t buff_size = 2 * 1024 * 1024;
	char *body, *body_read, *buff;
	int n;

	if (archive_zlib_version() == NULL) {
		skipping("Zip compression test requires zlib");
		return;
	}

	assert((body = malloc(body_size)) != NULL);
	assert((body_read = malloc(body_size)) != NULL);
	assert((buff = malloc(buff_size)) != NULL);

	/* Highly-compressible data: all bytes 255, except for a
	 * single 1 byte.
	 * The body is always 256k + 6 bytes long (the internal deflation
	 * buffer is exactly 256k).
	 */

	for(n = 1024; n < (int)body_size; n += 1024) {
		struct archive *a;
		struct archive_entry *entry;
		size_t used = 0;
		const void *pv;
		size_t s;
		int64_t o;

		memset(body, 255, body_size);
		body[n] = 1;

		/* Write an archive with a single entry of n bytes. */
		assert((a = archive_write_new()) != NULL);
		assertEqualInt(ARCHIVE_OK, archive_write_set_format_zip(a));
		assertEqualInt(ARCHIVE_OK, archive_write_open_memory(a, buff, buff_size, &used));

		entry = archive_entry_new2(a);
		archive_entry_set_pathname(entry, "test");
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_size(entry, 262150);
		assertEqualInt(ARCHIVE_OK, archive_write_header(a, entry));
		archive_entry_free(entry);
		assertEqualInt(262150, archive_write_data(a, body, 262150));
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));

		/* Read back the entry and verify the contents. */
		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
		assertEqualIntA(a, ARCHIVE_OK, read_open_memory(a, buff, used, 17));
		assertEqualInt(ARCHIVE_OK, archive_read_next_header(a, &entry));

		assertEqualInt(ARCHIVE_OK, archive_read_data_block(a, &pv, &s, &o));
		assertEqualInt(262144, s);
		assertEqualInt(0, o);

		assertEqualInt(ARCHIVE_OK, archive_read_data_block(a, &pv, &s, &o));
		assertEqualInt(6, s);
		assertEqualInt(262144, o);

		assertEqualInt(ARCHIVE_EOF, archive_read_data_block(a, &pv, &s, &o));

		assertEqualInt(ARCHIVE_OK, archive_free(a));
	}

	free(body);
	free(body_read);
	free(buff);
}
