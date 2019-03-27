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

/* Try to force archive_write_open_memory.c to write past the end of an array. */
static unsigned char buff[16384];

DEFINE_TEST(test_write_open_memory)
{
	unsigned int i;
	struct archive *a;
	struct archive_entry *ae;
	const char *name="/tmp/test";

	/* Create a simple archive_entry. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_pathname(ae, name);
	archive_entry_set_mode(ae, S_IFREG);
	assertEqualString(archive_entry_pathname(ae), name);

	/* Try writing with different buffer sizes. */
	/* Make sure that we get failure on too-small buffers, success on
	 * large enough ones. */
	for (i = 100; i < 1600; i++) {
		size_t used;
		size_t blocksize = 94;
		assert((a = archive_write_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_format_ustar(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_bytes_in_last_block(a, 1));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_set_bytes_per_block(a, (int)blocksize));
		buff[i] = 0xAE;
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_open_memory(a, buff, i, &used));
		/* If buffer is smaller than a tar header, this should fail. */
		if (i < (511/blocksize)*blocksize)
			assertEqualIntA(a, ARCHIVE_FATAL,
			    archive_write_header(a,ae));
		else
			assertEqualIntA(a, ARCHIVE_OK,
			    archive_write_header(a, ae));
		/* If buffer is smaller than a tar header plus 1024 byte
		 * end-of-archive marker, then this should fail. */
		failure("buffer size=%d\n", (int)i);
		if (i < 1536)
			assertEqualIntA(a, ARCHIVE_FATAL,
			    archive_write_close(a));
		else {
			assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
			assertEqualInt(used, archive_filter_bytes(a, -1));
			assertEqualInt(archive_filter_bytes(a, -1),
			    archive_filter_bytes(a, 0));
		}
		assertEqualInt(ARCHIVE_OK, archive_write_free(a));
		assertEqualInt(buff[i], 0xAE);
		assert(used <= i);
	}
	archive_entry_free(ae);
}
