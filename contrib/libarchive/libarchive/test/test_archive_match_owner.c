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

static void
test_uid(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	assertEqualIntA(m, 0, archive_match_include_uid(m, 1000));
	assertEqualIntA(m, 0, archive_match_include_uid(m, 1002));

	archive_entry_set_uid(ae, 0);
	failure("uid 0 should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_uid(ae, 1000);
	failure("uid 1000 should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_set_uid(ae, 1001);
	failure("uid 1001 should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_uid(ae, 1002);
	failure("uid 1002 should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_set_uid(ae, 1003);
	failure("uid 1003 should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_gid(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	assertEqualIntA(m, 0, archive_match_include_gid(m, 1000));
	assertEqualIntA(m, 0, archive_match_include_gid(m, 1002));

	archive_entry_set_gid(ae, 0);
	failure("uid 0 should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_gid(ae, 1000);
	failure("uid 1000 should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_set_gid(ae, 1001);
	failure("uid 1001 should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_set_gid(ae, 1002);
	failure("uid 1002 should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_set_gid(ae, 1003);
	failure("uid 1003 should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_uname_mbs(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	assertEqualIntA(m, 0, archive_match_include_uname(m, "foo"));
	assertEqualIntA(m, 0, archive_match_include_uname(m, "bar"));

	archive_entry_copy_uname(ae, "unknown");
	failure("User 'unknown' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_copy_uname(ae, "foo");
	failure("User 'foo' should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_copy_uname(ae, "foo1");
	failure("User 'foo1' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_copy_uname(ae, "bar");
	failure("User 'bar' should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_copy_uname(ae, "bar1");
	failure("User 'bar1' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_uname_wcs(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	assertEqualIntA(m, 0, archive_match_include_uname_w(m, L"foo"));
	assertEqualIntA(m, 0, archive_match_include_uname_w(m, L"bar"));

	archive_entry_copy_uname_w(ae, L"unknown");
	failure("User 'unknown' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_copy_uname_w(ae, L"foo");
	failure("User 'foo' should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_copy_uname_w(ae, L"foo1");
	failure("User 'foo1' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_copy_uname_w(ae, L"bar");
	failure("User 'bar' should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_copy_uname_w(ae, L"bar1");
	failure("User 'bar1' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_gname_mbs(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	assertEqualIntA(m, 0, archive_match_include_gname(m, "foo"));
	assertEqualIntA(m, 0, archive_match_include_gname(m, "bar"));

	archive_entry_copy_gname(ae, "unknown");
	failure("Group 'unknown' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_copy_gname(ae, "foo");
	failure("Group 'foo' should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_copy_gname(ae, "foo1");
	failure("Group 'foo1' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_copy_gname(ae, "bar");
	failure("Group 'bar' should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_copy_gname(ae, "bar1");
	failure("Group 'bar1' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

static void
test_gname_wcs(void)
{
	struct archive_entry *ae;
	struct archive *m;

	if (!assert((m = archive_match_new()) != NULL))
		return;
	if (!assert((ae = archive_entry_new()) != NULL)) {
		archive_match_free(m);
		return;
	}

	assertEqualIntA(m, 0, archive_match_include_gname_w(m, L"foo"));
	assertEqualIntA(m, 0, archive_match_include_gname_w(m, L"bar"));

	archive_entry_copy_gname_w(ae, L"unknown");
	failure("Group 'unknown' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_copy_gname_w(ae, L"foo");
	failure("Group 'foo' should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_copy_gname_w(ae, L"foo1");
	failure("Group 'foo1' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));
	archive_entry_copy_gname_w(ae, L"bar");
	failure("Group 'bar' should not be excluded");
	assertEqualInt(0, archive_match_owner_excluded(m, ae));
	assertEqualInt(0, archive_match_excluded(m, ae));
	archive_entry_copy_gname_w(ae, L"bar1");
	failure("Group 'bar1' should be excluded");
	assertEqualInt(1, archive_match_owner_excluded(m, ae));
	assertEqualInt(1, archive_match_excluded(m, ae));

	/* Clean up. */
	archive_entry_free(ae);
	archive_match_free(m);
}

DEFINE_TEST(test_archive_match_owner)
{
	test_uid();
	test_gid();
	test_uname_mbs();
	test_uname_wcs();
	test_gname_mbs();
	test_gname_wcs();
}
