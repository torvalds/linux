/*-
 * Copyright (c) 2017 Martin Matuska
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

DEFINE_TEST(test_option_xattrs)
{
#if !ARCHIVE_XATTR_SUPPORT
        skipping("Extended atributes are not supported on this platform");
#else	/* ARCHIVE_XATTR_SUPPORT */

	const char *testattr = "user.libarchive.test";
	const char *testval = "testval";
	void *readval;
	size_t size;
	int r;

	/* Create a file. */
	assertMakeFile("f", 0644, "a");

	if (!setXattr("f", "user.libarchive.test", testval,
	    strlen(testval) + 1)) {
		skipping("Can't set user extended attributes on this "
		    "filesystem");
		return;
	}

	/* Archive with xattrs */
	r = systemf("%s -c --no-mac-metadata --xattrs -f xattrs.tar f >xattrs.out 2>xattrs.err", testprog);
	assertEqualInt(r, 0);

	/* Archive without xattrs */
	r = systemf("%s -c --no-mac-metadata --no-xattrs -f noxattrs.tar f >noxattrs.out 2>noxattrs.err", testprog);
	assertEqualInt(r, 0);

	/* Extract xattrs with xattrs */
	assertMakeDir("xattrs_xattrs", 0755);
	r = systemf("%s -x -C xattrs_xattrs --no-same-permissions --xattrs -f xattrs.tar >xattrs_xattrs.out 2>xattrs_xattrs.err", testprog);
	assertEqualInt(r, 0);
	readval = getXattr("xattrs_xattrs/f", testattr, &size);
	if(assertEqualInt(size, strlen(testval) + 1) != 0)
		assertEqualMem(readval, testval, size);
	free(readval);

	/* Extract xattrs without xattrs */
	assertMakeDir("xattrs_noxattrs", 0755);
	r = systemf("%s -x -C xattrs_noxattrs -p --no-xattrs -f xattrs.tar >xattrs_noxattrs.out 2>xattrs_noxattrs.err", testprog);
	assertEqualInt(r, 0);
	readval = getXattr("xattrs_noxattrs/f", testattr, &size);
	assert(readval == NULL);

	/* Extract noxattrs with xattrs */
	assertMakeDir("noxattrs_xattrs", 0755);
	r = systemf("%s -x -C noxattrs_xattrs --no-same-permissions --xattrs -f noxattrs.tar >noxattrs_xattrs.out 2>noxattrs_xattrs.err", testprog);
	assertEqualInt(r, 0);	
	readval = getXattr("noxattrs_xattrs/f", testattr, &size);
	assert(readval == NULL);

	/* Extract noxattrs with noxattrs */
	assertMakeDir("noxattrs_noxattrs", 0755);
	r = systemf("%s -x -C noxattrs_noxattrs -p --no-xattrs -f noxattrs.tar >noxattrs_noxattrs.out 2>noxattrs_noxattrs.err", testprog);
	assertEqualInt(r, 0);
	readval = getXattr("noxattrs_noxattrs/f", testattr, &size);
	assert(readval == NULL);
#endif	/* ARCHIVE_XATTR_SUPPORT */
}
