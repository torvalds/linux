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

DEFINE_TEST(test_archive_api_feature)
{
	char buff[128];
	const char *p;

	/* This is the (hopefully) final versioning API. */
	assertEqualInt(ARCHIVE_VERSION_NUMBER, archive_version_number());
	sprintf(buff, "libarchive %d.%d.%d",
	    archive_version_number() / 1000000,
	    (archive_version_number() / 1000) % 1000,
	    archive_version_number() % 1000);
	failure("Version string is: %s, computed is: %s",
	    archive_version_string(), buff);
	assertEqualMem(buff, archive_version_string(), strlen(buff));
	if (strlen(buff) < strlen(archive_version_string())) {
		p = archive_version_string() + strlen(buff);
		failure("Version string is: %s", archive_version_string());
		if (p[0] == 'd'&& p[1] == 'e' && p[2] == 'v')
			p += 3;
		else {
			assert(*p == 'a' || *p == 'b' || *p == 'c' || *p == 'd');
			++p;
		}
		failure("Version string is: %s", archive_version_string());
		assert(*p == '\0');
	}
}
