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

#include "../cpio.h"
#include "err.h"

#if !defined(_WIN32)
#define ROOT "root"
static const int root_uids[] = { 0 };
static const int root_gids[] = { 0, 1 };
#elif defined(__CYGWIN__)
/* On cygwin, the Administrator user most likely exists (unless
 * it has been renamed or is in a non-English localization), but
 * its primary group membership depends on how the user set up
 * their /etc/passwd. Likely values are 513 (None), 545 (Users),
 * or 544 (Administrators). Just check for one of those...
 * TODO: Handle non-English localizations... e.g. French 'Administrateur'
 *       Use CreateWellKnownSID() and LookupAccountName()?
 */
#define ROOT "Administrator"
static const int root_uids[] = { 500 };
static const int root_gids[] = { 513, 545, 544 };
#endif

#if defined(ROOT)
static int
int_in_list(int i, const int *l, size_t n)
{
	while (n-- > 0)
		if (*l++ == i)
			return (1);
	failure("%d", i);
	return (0);
}
#endif

DEFINE_TEST(test_owner_parse)
{
#if !defined(ROOT)
	skipping("No uid/gid configuration for this OS");
#else
	int uid, gid;

	assert(NULL == owner_parse(ROOT, &uid, &gid));
	assert(int_in_list(uid, root_uids,
		sizeof(root_uids)/sizeof(root_uids[0])));
	assertEqualInt(-1, gid);


	assert(NULL == owner_parse(ROOT ":", &uid, &gid));
	assert(int_in_list(uid, root_uids,
		sizeof(root_uids)/sizeof(root_uids[0])));
	assert(int_in_list(gid, root_gids,
		sizeof(root_gids)/sizeof(root_gids[0])));

	assert(NULL == owner_parse(ROOT ".", &uid, &gid));
	assert(int_in_list(uid, root_uids,
		sizeof(root_uids)/sizeof(root_uids[0])));
	assert(int_in_list(gid, root_gids,
		sizeof(root_gids)/sizeof(root_gids[0])));

	assert(NULL == owner_parse("111", &uid, &gid));
	assertEqualInt(111, uid);
	assertEqualInt(-1, gid);

	assert(NULL == owner_parse("112:", &uid, &gid));
	assertEqualInt(112, uid);
	/* Can't assert gid, since we don't know gid for user #112. */

	assert(NULL == owner_parse("113.", &uid, &gid));
	assertEqualInt(113, uid);
	/* Can't assert gid, since we don't know gid for user #113. */

	assert(NULL == owner_parse(":114", &uid, &gid));
	assertEqualInt(-1, uid);
	assertEqualInt(114, gid);

	assert(NULL == owner_parse(".115", &uid, &gid));
	assertEqualInt(-1, uid);
	assertEqualInt(115, gid);

	assert(NULL == owner_parse("116:117", &uid, &gid));
	assertEqualInt(116, uid);
	assertEqualInt(117, gid);

	/*
	 * TODO: Lookup current user/group name, build strings and
	 * use those to verify username/groupname lookups for ordinary
	 * users.
	 */

	assert(NULL != owner_parse(":nonexistentgroup", &uid, &gid));
	assert(NULL != owner_parse(ROOT ":nonexistentgroup", &uid, &gid));
	assert(NULL !=
	    owner_parse("nonexistentuser:nonexistentgroup", &uid, &gid));
#endif
}
