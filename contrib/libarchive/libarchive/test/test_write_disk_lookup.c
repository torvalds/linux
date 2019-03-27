/*-
 * Copyright (c) 2003-2010 Tim Kientzle
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
group_cleanup(void *d)
{
	int *mp = d;
	assertEqualInt(*mp, 0x13579);
	*mp = 0x2468;
}

static int64_t
group_lookup(void *d, const char *name, int64_t g)
{
	int *mp = d;

	(void)g; /* UNUSED */

	assertEqualInt(*mp, 0x13579);
	if (strcmp(name, "FOOGROUP"))
		return (1);
	return (73);
}

static void
user_cleanup(void *d)
{
	int *mp = d;
	assertEqualInt(*mp, 0x1234);
	*mp = 0x2345;
}

static int64_t
user_lookup(void *d, const char *name, int64_t u)
{
	int *mp = d;

	(void)u; /* UNUSED */

	assertEqualInt(*mp, 0x1234);
	if (strcmp("FOO", name) == 0)
		return (2);
	return (74);
}

DEFINE_TEST(test_write_disk_lookup)
{
	struct archive *a;
	int gmagic = 0x13579, umagic = 0x1234;
	int64_t id;

	assert((a = archive_write_disk_new()) != NULL);

	/* Default uname/gname lookups always return ID. */
	assertEqualInt(0, archive_write_disk_gid(a, "", 0));
	assertEqualInt(12, archive_write_disk_gid(a, "root", 12));
	assertEqualInt(12, archive_write_disk_gid(a, "wheel", 12));
	assertEqualInt(0, archive_write_disk_uid(a, "", 0));
	assertEqualInt(18, archive_write_disk_uid(a, "root", 18));

	/* Register some weird lookup functions. */
	assertEqualInt(ARCHIVE_OK, archive_write_disk_set_group_lookup(a,
		       &gmagic, &group_lookup, &group_cleanup));
	/* Verify that our new function got called. */
	assertEqualInt(73, archive_write_disk_gid(a, "FOOGROUP", 8));
	assertEqualInt(1, archive_write_disk_gid(a, "NOTFOOGROUP", 8));

	/* De-register. */
	assertEqualInt(ARCHIVE_OK,
		       archive_write_disk_set_group_lookup(a, NULL, NULL, NULL));
	/* Ensure our cleanup function got called. */
	assertEqualInt(gmagic, 0x2468);

	/* Same thing with uname lookup.... */
	assertEqualInt(ARCHIVE_OK, archive_write_disk_set_user_lookup(a,
			   &umagic, &user_lookup, &user_cleanup));
	assertEqualInt(2, archive_write_disk_uid(a, "FOO", 0));
	assertEqualInt(74, archive_write_disk_uid(a, "NOTFOO", 1));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_disk_set_user_lookup(a, NULL, NULL, NULL));
	assertEqualInt(umagic, 0x2345);

	/* Try the standard lookup functions. */
	if (archive_write_disk_set_standard_lookup(a) != ARCHIVE_OK) {
		skipping("standard uname/gname lookup");
	} else {
		/* Try a few common names for group #0. */
		id = archive_write_disk_gid(a, "wheel", 8);
		if (id != 0)
			id = archive_write_disk_gid(a, "root", 8);
		failure("Unable to verify lookup of group #0");
#if defined(_WIN32) && !defined(__CYGWIN__)
		/* Not yet implemented on Windows. */
		assertEqualInt(8, id);
#else
		assertEqualInt(0, id);
#endif

		/* Try a few common names for user #0. */
		id = archive_write_disk_uid(a, "root", 8);
		failure("Unable to verify lookup of user #0");
#if defined(_WIN32) && !defined(__CYGWIN__)
		/* Not yet implemented on Windows. */
		assertEqualInt(8, id);
#else
		assertEqualInt(0, id);
#endif
	}

	/* Deregister again and verify the default lookups again. */
	assertEqualInt(ARCHIVE_OK,
	    archive_write_disk_set_group_lookup(a, NULL, NULL, NULL));
	assertEqualInt(ARCHIVE_OK,
	    archive_write_disk_set_user_lookup(a, NULL, NULL, NULL));
	assertEqualInt(0, archive_write_disk_gid(a, "", 0));
	assertEqualInt(0, archive_write_disk_uid(a, "", 0));

	/* Re-register our custom handlers. */
	gmagic = 0x13579;
	umagic = 0x1234;
	assertEqualInt(ARCHIVE_OK, archive_write_disk_set_group_lookup(a,
			   &gmagic, &group_lookup, &group_cleanup));
	assertEqualInt(ARCHIVE_OK, archive_write_disk_set_user_lookup(a,
		      &umagic, &user_lookup, &user_cleanup));

	/* Destroy the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Verify our cleanup functions got called. */
	assertEqualInt(gmagic, 0x2468);
	assertEqualInt(umagic, 0x2345);
}
