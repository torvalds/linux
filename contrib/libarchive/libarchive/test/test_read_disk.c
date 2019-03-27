/*-
 * Copyright (c) 2003-2009 Tim Kientzle
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
gname_cleanup(void *d)
{
	int *mp = d;
	assertEqualInt(*mp, 0x13579);
	*mp = 0x2468;
}

static const char *
gname_lookup(void *d, int64_t g)
{
	int *mp = d;
	assertEqualInt(*mp, 0x13579);
	if (g == 1)
		return ("FOOGROUP");
	return ("NOTFOOGROUP");
}

static void
uname_cleanup(void *d)
{
	int *mp = d;
	assertEqualInt(*mp, 0x1234);
	*mp = 0x2345;
}

static const char *
uname_lookup(void *d, int64_t u)
{
	int *mp = d;
	assertEqualInt(*mp, 0x1234);
	if (u == 1)
		return ("FOO");
	return ("NOTFOO");
}

#if !defined(__CYGWIN__) && !defined(__HAIKU__)
/* We test GID lookup by looking up the name of group number zero and
 * checking it against the following list.  If your system uses a
 * different conventional name for group number zero, please extend
 * this array and send us a patch.  As always, please keep this list
 * sorted alphabetically.
 */
static const char *zero_groups[] = {
	"root",   /* Linux */
	"wheel"  /* BSD */
};
#endif

DEFINE_TEST(test_read_disk)
{
	struct archive *a;
	int gmagic = 0x13579, umagic = 0x1234;
#if !defined(__CYGWIN__) && !defined(__HAIKU__)
	const char *p;
	size_t i;
#endif

	assert((a = archive_read_disk_new()) != NULL);

	/* Default uname/gname lookups always return NULL. */
	assert(archive_read_disk_gname(a, 0) == NULL);
	assert(archive_read_disk_uname(a, 0) == NULL);

	/* Register some weird lookup functions. */
	assertEqualInt(ARCHIVE_OK, archive_read_disk_set_gname_lookup(a,
			   &gmagic, &gname_lookup, &gname_cleanup));
	/* Verify that our new function got called. */
	assertEqualString(archive_read_disk_gname(a, 0), "NOTFOOGROUP");
	assertEqualString(archive_read_disk_gname(a, 1), "FOOGROUP");

	/* De-register. */
	assertEqualInt(ARCHIVE_OK,
	    archive_read_disk_set_gname_lookup(a, NULL, NULL, NULL));
	/* Ensure our cleanup function got called. */
	assertEqualInt(gmagic, 0x2468);

	/* Same thing with uname lookup.... */
	assertEqualInt(ARCHIVE_OK, archive_read_disk_set_uname_lookup(a,
			   &umagic, &uname_lookup, &uname_cleanup));
	assertEqualString(archive_read_disk_uname(a, 0), "NOTFOO");
	assertEqualString(archive_read_disk_uname(a, 1), "FOO");
	assertEqualInt(ARCHIVE_OK,
	    archive_read_disk_set_uname_lookup(a, NULL, NULL, NULL));
	assertEqualInt(umagic, 0x2345);

	/* Try the standard lookup functions. */
	if (archive_read_disk_set_standard_lookup(a) != ARCHIVE_OK) {
		skipping("standard uname/gname lookup");
	} else {
#if defined(__CYGWIN__) || defined(__HAIKU__)
		/* Some platforms don't have predictable names for
		 * uid=0, so we skip this part of the test. */
		skipping("standard uname/gname lookup");
#else
		/* XXX Someday, we may need to generalize this the
		 * same way we generalized the group name check below.
		 * That's needed only if we encounter a system where
		 * uid 0 is not "root". XXX */
		assertEqualString(archive_read_disk_uname(a, 0), "root");

		/* Get the group name for group 0 and see if it makes sense. */
		p = archive_read_disk_gname(a, 0);
		assert(p != NULL);
		if (p != NULL) {
			i = 0;
			while (i < sizeof(zero_groups)/sizeof(zero_groups[0])) {
				if (strcmp(zero_groups[i], p) == 0)
					break;
				++i;
			}
			if (i == sizeof(zero_groups)/sizeof(zero_groups[0])) {
				/* If you get a failure here, either
				 * archive_read_disk_gname() isn't working or
				 * your system uses a different name for group
				 * number zero.  If the latter, please add a
				 * new entry to the zero_groups[] array above.
				 */
				failure("group 0 didn't have any of the expected names");
				assertEqualString(p, zero_groups[0]);
			}
		}
#endif
	}

	/* Deregister again and verify the default lookups again. */
	assertEqualInt(ARCHIVE_OK,
	    archive_read_disk_set_gname_lookup(a, NULL, NULL, NULL));
	assertEqualInt(ARCHIVE_OK,
	    archive_read_disk_set_uname_lookup(a, NULL, NULL, NULL));
	assert(archive_read_disk_gname(a, 0) == NULL);
	assert(archive_read_disk_uname(a, 0) == NULL);

	/* Re-register our custom handlers. */
	gmagic = 0x13579;
	umagic = 0x1234;
	assertEqualInt(ARCHIVE_OK, archive_read_disk_set_gname_lookup(a,
			   &gmagic, &gname_lookup, &gname_cleanup));
	assertEqualInt(ARCHIVE_OK, archive_read_disk_set_uname_lookup(a,
			   &umagic, &uname_lookup, &uname_cleanup));

	/* Destroy the archive. */
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/* Verify our cleanup functions got called. */
	assertEqualInt(gmagic, 0x2468);
	assertEqualInt(umagic, 0x2345);
}
