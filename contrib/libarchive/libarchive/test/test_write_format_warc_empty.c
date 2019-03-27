/*-
 * Copyright (C) 2014 Sebastian Freundt
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

DEFINE_TEST(test_write_format_warc_empty)
{
	struct archive *a;
	struct archive_entry *ae;
	char buff[512U];
	size_t used;

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_warc(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open_memory(a, buff, sizeof(buff), &used));

	/* Add "." entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, ".");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add ".." entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "..");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add "/" entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "/");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add "../" entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "../");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add "../../." entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "../../.");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Add "..//.././" entry which must be ignored. */ 
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_atime(ae, 2, 0);
	archive_entry_set_ctime(ae, 4, 0);
	archive_entry_set_mtime(ae, 5, 0);
	archive_entry_copy_pathname(ae, "..//.././");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	assertEqualIntA(a, ARCHIVE_FAILED, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Close out the archive without writing anything. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Test whether last archive is empty indeed. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_memory(a, buff, used));

	/* Test EOF */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}
