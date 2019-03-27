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

DEFINE_TEST(test_option_nodump)
{

	if (!canNodump()) {
		skipping("Can't test nodump on this filesystem");
		return;
	}

	assertMakeFile("file1", 0644, "file1");
	assertMakeFile("file2", 0644, "file2");
	assertMakeFile("file3", 0644, "file3");
	assertSetNodump("file2");

	/* Test 1: Without --nodump */
	assertEqualInt(0, systemf("%s -cf test1.tar file1 file2 file3",
	    testprog));
	assertMakeDir("test1", 0755);
	assertChdir("test1");
	assertEqualInt(0,
	    systemf("%s -xf ../test1.tar >test.out 2>test.err", testprog));
	assertFileContents("file1", 5, "file1");
	assertFileContents("file2", 5, "file2");
	assertFileContents("file3", 5, "file3");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 2: With --nodump */
	assertEqualInt(0, systemf("%s -cf test2.tar --nodump file1 file2 file3",
	    testprog));
	assertMakeDir("test2", 0755);
	assertChdir("test2");
	assertEqualInt(0,
	    systemf("%s -xf ../test2.tar >test.out 2>test.err", testprog));
	assertFileContents("file1", 5, "file1");
	assertFileNotExists("file2");
	assertFileContents("file3", 5, "file3");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");
}
