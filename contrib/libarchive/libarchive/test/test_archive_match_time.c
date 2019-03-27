/*-
 * Copyright (c) 2012 Michihiro NAKAJIMA
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

#define __LIBARCHIVE_BUILD 1
#include "archive_getdate.h"

static void
test_newer_time(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	assertEqualIntA(m, 0, archive_match_include_time(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_NEWER, 7880, 0));

	archive_entry_copy_pathname(ae, "file1");
	archive_entry_set_mtime(ae, 7880, 0);
	archive_entry_set_ctime(ae, 7880, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7879, 999);
	archive_entry_set_ctime(ae, 7879, 999);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	archive_entry_set_mtime(ae, 7881, 0);
	archive_entry_set_ctime(ae, 7881, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	archive_entry_set_mtime(ae, 7880, 1);
	archive_entry_set_ctime(ae, 7880, 0);
	failure("Its mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	archive_entry_set_mtime(ae, 7880, 0);
	archive_entry_set_ctime(ae, 7880, 1);
	failure("Its ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_newer_time_str(void)
{
	struct archive_entry *ae;
	struct archive *m;
	time_t now, t;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	time(&now);

	assertEqualIntA(m, 0, archive_match_include_date(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_NEWER, "1980/2/1 0:0:0 UTC"));

	/* Test1: Allow newer time. */
	archive_entry_copy_pathname(ae, "file1");
	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/2/1 0:0:1 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 1);
	archive_entry_set_ctime(ae, t, 0);
	failure("Its mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 1);
	failure("Its ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));


	/* Test2: Allow equal or newer time. */
	assertEqualIntA(m, 0, archive_match_include_date(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_NEWER | ARCHIVE_MATCH_EQUAL,
	    "1980/2/1 0:0:0 UTC"));

	archive_entry_copy_pathname(ae, "file1");
	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/2/1 0:0:1 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_newer_time_str_w(void)
{
	struct archive_entry *ae;
	struct archive *m;
	time_t now, t;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	time(&now);

	assertEqualIntA(m, 0, archive_match_include_date_w(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_NEWER, L"1980/2/1 0:0:0 UTC"));

	/* Test1: Allow newer time. */
	archive_entry_copy_pathname(ae, "file1");
	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/2/1 0:0:1 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 1);
	archive_entry_set_ctime(ae, t, 0);
	failure("Its mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 1);
	failure("Its ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));


	/* Test2: Allow equal or newer time. */
	assertEqualIntA(m, 0, archive_match_include_date_w(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_NEWER | ARCHIVE_MATCH_EQUAL,
	    L"1980/2/1 0:0:0 UTC"));

	archive_entry_copy_pathname(ae, "file1");
	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/2/1 0:0:1 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_newer_mtime_than_file_mbs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: newer mtime than a file specified in MBS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_NEWER, "mid_mtime"));

	/* Verify 'old_mtime' file. */
	archive_entry_copy_pathname(ae, "old_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'mid_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'new_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_mtime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_newer_ctime_than_file_mbs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: newer ctime than a file specified in MBS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time(m,
	    ARCHIVE_MATCH_CTIME | ARCHIVE_MATCH_NEWER, "mid_ctime"));

	/* Verify 'old_ctime' file. */
	archive_entry_copy_pathname(ae, "old_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'mid_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'new_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_newer_mtime_than_file_wcs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: newer mtime than a file specified in WCS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time_w(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_NEWER, L"mid_mtime"));

	/* Verify 'old_mtime' file. */
	archive_entry_copy_pathname(ae, "old_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'mid_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'new_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_mtime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_newer_ctime_than_file_wcs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: newer ctime than a file specified in WCS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time_w(m,
	    ARCHIVE_MATCH_CTIME | ARCHIVE_MATCH_NEWER, L"mid_ctime"));

	/* Verify 'old_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "old_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'mid_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'new_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_older_time(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	assertEqualIntA(m, 0, archive_match_include_time(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_OLDER, 7880, 0));

	archive_entry_copy_pathname(ae, "file1");
	archive_entry_set_mtime(ae, 7880, 0);
	archive_entry_set_ctime(ae, 7880, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7879, 999);
	archive_entry_set_ctime(ae, 7879, 999);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	archive_entry_set_mtime(ae, 7881, 0);
	archive_entry_set_ctime(ae, 7881, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	archive_entry_set_mtime(ae, 7880, 1);
	archive_entry_set_ctime(ae, 7879, 0);
	failure("Its mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	archive_entry_set_mtime(ae, 7879, 0);
	archive_entry_set_ctime(ae, 7880, 1);
	failure("Its ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_older_time_str(void)
{
	struct archive_entry *ae;
	struct archive *m;
	time_t now, t;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	time(&now);

	/* Test1: Allow newer time. */
	assertEqualIntA(m, 0, archive_match_include_date(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_OLDER, "1980/2/1 0:0:0 UTC"));

	archive_entry_copy_pathname(ae, "file1");
	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/3/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/3/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_ctime(ae, t, 0);
	failure("Its mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	t = __archive_get_date(now, "1980/3/1 0:0:0 UTC");
	archive_entry_set_ctime(ae, t, 0);
	failure("Its ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Test2: Allow equal or newer time. */
	assertEqualIntA(m, 0, archive_match_include_date(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_OLDER | ARCHIVE_MATCH_EQUAL,
	    "1980/2/1 0:0:0 UTC"));

	archive_entry_copy_pathname(ae, "file1");
	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/3/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_older_time_str_w(void)
{
	struct archive_entry *ae;
	struct archive *m;
	time_t now, t;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	time(&now);

	/* Test1: Allow newer time. */
	assertEqualIntA(m, 0, archive_match_include_date_w(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_OLDER, L"1980/2/1 0:0:0 UTC"));

	archive_entry_copy_pathname(ae, "file1");
	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/3/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/3/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_ctime(ae, t, 0);
	failure("Its mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	t = __archive_get_date(now, "1980/3/1 0:0:0 UTC");
	archive_entry_set_ctime(ae, t, 0);
	failure("Its ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Test2: Allow equal or newer time. */
	assertEqualIntA(m, 0, archive_match_include_date_w(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME |
	    ARCHIVE_MATCH_OLDER | ARCHIVE_MATCH_EQUAL,
	    L"1980/2/1 0:0:0 UTC"));

	archive_entry_copy_pathname(ae, "file1");
	t = __archive_get_date(now, "1980/2/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	t = __archive_get_date(now, "1980/1/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	t = __archive_get_date(now, "1980/3/1 0:0:0 UTC");
	archive_entry_set_mtime(ae, t, 0);
	archive_entry_set_ctime(ae, t, 0);
	failure("Both Its mtime and ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_older_mtime_than_file_mbs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: older mtime than a file specified in MBS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_OLDER, "mid_mtime"));

	/* Verify 'old_mtime' file. */
	archive_entry_copy_pathname(ae, "old_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_mtime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Verify 'mid_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'new_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_older_ctime_than_file_mbs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: older ctime than a file specified in MBS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time(m,
	    ARCHIVE_MATCH_CTIME | ARCHIVE_MATCH_OLDER, "mid_ctime"));

	/* Verify 'old_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "old_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Verify 'mid_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'new_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_older_mtime_than_file_wcs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: older mtime than a file specified in WCS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time_w(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_OLDER, L"mid_mtime"));

	/* Verify 'old_mtime' file. */
	archive_entry_copy_pathname(ae, "old_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_mtime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Verify 'mid_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'new_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_older_ctime_than_file_wcs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: older ctime than a file specified in WCS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time_w(m,
	    ARCHIVE_MATCH_CTIME | ARCHIVE_MATCH_OLDER, L"mid_ctime"));

	/* Verify 'old_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "old_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Verify 'mid_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'new_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_mtime_between_files_mbs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: mtime between  file specified in MBS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_NEWER, "old_mtime"));
	assertEqualIntA(m, 0, archive_match_include_file_time(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_OLDER, "new_mtime"));

	/* Verify 'old_mtime' file. */
	archive_entry_copy_pathname(ae, "old_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'mid_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_mtime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Verify 'new_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_mtime_between_files_wcs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: mtime between  file specified in WCS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time_w(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_NEWER, L"old_mtime"));
	assertEqualIntA(m, 0, archive_match_include_file_time_w(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_OLDER, L"new_mtime"));

	/* Verify 'old_mtime' file. */
	archive_entry_copy_pathname(ae, "old_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'mid_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_mtime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Verify 'new_mtime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_mtime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_mtime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_ctime_between_files_mbs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: ctime between files specified in MBS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time(m,
	    ARCHIVE_MATCH_CTIME | ARCHIVE_MATCH_NEWER, "old_ctime"));
	assertEqualIntA(m, 0, archive_match_include_file_time(m,
	    ARCHIVE_MATCH_CTIME | ARCHIVE_MATCH_OLDER, "new_ctime"));

	/* Verify 'old_ctime' file. */
	archive_entry_copy_pathname(ae, "old_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'mid_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Verify 'new_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_ctime_between_files_wcs(void)
{
	struct archive *a;
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}
	if (!assert((a = archive_read_disk_new()) != NULL)) {
		archive_match_free(m);
		archive_entry_free(ae);
		return;
	}

	/*
	 * Test: ctime between files specified in WCS file name.
	 */
	assertEqualIntA(m, 0, archive_match_include_file_time_w(m,
	    ARCHIVE_MATCH_CTIME | ARCHIVE_MATCH_NEWER, L"old_ctime"));
	assertEqualIntA(m, 0, archive_match_include_file_time_w(m,
	    ARCHIVE_MATCH_CTIME | ARCHIVE_MATCH_OLDER, L"new_ctime"));

	/* Verify 'old_ctime' file. */
	archive_entry_copy_pathname(ae, "old_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("old_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Verify 'mid_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "mid_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("mid_ctime should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	/* Verify 'new_ctime' file. */
	archive_entry_clear(ae);
	archive_entry_copy_pathname(ae, "new_ctime");
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_disk_entry_from_file(a, ae, -1, NULL));
	failure("new_ctime should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_read_free(a);
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
excluded(struct archive *m)
{
	struct archive_entry *ae;

	if (!assert((ae = archive_entry_new()) != NULL))
		return;

	archive_entry_copy_pathname(ae, "file1");
	archive_entry_set_mtime(ae, 7879, 999);
	failure("It should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7880, 0);
	failure("It should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7880, 1);
	failure("It should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	archive_entry_copy_pathname(ae, "file2");
	archive_entry_set_mtime(ae, 7879, 999);
	failure("It should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7880, 0);
	failure("It should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7880, 1);
	failure("It should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));

	archive_entry_copy_pathname(ae, "file3");
	archive_entry_set_mtime(ae, 7879, 999);
	failure("It should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7880, 0);
	failure("It should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7880, 1);
	failure("It should be excluded");
	assertEqualInt(1, archive_match_time_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/*
	 * "file4" is not registered, that sort of a file should not be
	 * excluded with any mtime.
	 */
	archive_entry_copy_pathname(ae, "file4");
	archive_entry_set_mtime(ae, 7879, 999);
	failure("It should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7880, 0);
	failure("It should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_set_mtime(ae, 7880, 1);
	failure("It should not be excluded");
	assertEqualInt(0, archive_match_time_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));


	/* Clean up. */
	archive_entry_free(ae);
}

static void
test_pathname_newer_mtime(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	archive_entry_copy_pathname(ae, "file1");
	archive_entry_set_mtime(ae, 7880, 0);
	assertEqualIntA(m, 0, archive_match_exclude_entry(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_OLDER |
	    ARCHIVE_MATCH_EQUAL, ae));
	archive_entry_copy_pathname(ae, "file2");
	archive_entry_set_mtime(ae, 1, 0);
	assertEqualIntA(m, 0, archive_match_exclude_entry(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_OLDER |
	    ARCHIVE_MATCH_EQUAL, ae));
	archive_entry_copy_pathname(ae, "file3");
	archive_entry_set_mtime(ae, 99999, 0);
	assertEqualIntA(m, 0, archive_match_exclude_entry(m,
	    ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_OLDER |
	    ARCHIVE_MATCH_EQUAL, ae));

	excluded(m);

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

DEFINE_TEST(test_archive_match_time)
{
	struct stat st;

	/* Test: matching newer times. */
	test_newer_time();
	test_newer_time_str();
	test_newer_time_str_w();
	/* Test: matching older times. */
	test_older_time();
	test_older_time_str();
	test_older_time_str_w();

	/*
	 * Create sample files for tests matching mtime.
	 * ctimes of those files may be all the same or the ctime of
	 * new_mtime may be older than old_mtime.
	 */
	assertMakeFile("new_mtime", 0666, "new");
	assertUtimes("new_mtime", 10002, 0, 10002, 0);
	assertMakeFile("mid_mtime", 0666, "mid");
	assertUtimes("mid_mtime", 10001, 0, 10001, 0);
	assertMakeFile("old_mtime", 0666, "old");
	assertUtimes("old_mtime", 10000, 0, 10000, 0);

	/*
	 * Create sample files for tests matching ctime.
	 * the mtime of mid_ctime is older than old_ctime and also the mtime
	 * of new_ctime is older than both mid_ctime and old_ctime.
	 */
	assertMakeFile("old_ctime", 0666, "old");
	assertUtimes("old_ctime", 10002, 0, 10002, 0);
	assertEqualInt(0, stat("old_ctime", &st));
	sleepUntilAfter(st.st_ctime);
	assertMakeFile("mid_ctime", 0666, "mid");
	assertUtimes("mid_ctime", 10001, 0, 10001, 0);
	assertEqualInt(0, stat("mid_ctime", &st));
	sleepUntilAfter(st.st_ctime);
	assertMakeFile("new_ctime", 0666, "new");
	assertUtimes("new_ctime", 10000, 0, 10000, 0);

	/*
	 * Test: matching mtime which indicated by files on the disk.
	 */
	test_newer_mtime_than_file_mbs();
	test_newer_mtime_than_file_wcs();
	test_older_mtime_than_file_mbs();
	test_older_mtime_than_file_wcs();
	test_mtime_between_files_mbs();
	test_mtime_between_files_wcs();

	/*
	 * Test: matching ctime which indicated by files on the disk.
	 */
	test_newer_ctime_than_file_mbs();
	test_newer_ctime_than_file_wcs();
	test_older_ctime_than_file_mbs();
	test_older_ctime_than_file_wcs();
	test_ctime_between_files_mbs();
	test_ctime_between_files_wcs();

	/* Test: matching both pathname and mtime. */
	test_pathname_newer_mtime();
}
