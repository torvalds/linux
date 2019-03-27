/*-
 * Copyright (c) 2010 Tim Kientzle
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

DEFINE_TEST(test_option_C_upper)
{
	int r;

	assertMakeDir("d1", 0755);
	assertMakeDir("d2", 0755);
	assertMakeFile("d1/file1", 0644, "d1/file1");
	assertMakeFile("d1/file2", 0644, "d1/file2");
	assertMakeFile("d2/file1", 0644, "d2/file1");
	assertMakeFile("d2/file2", 0644, "d2/file2");

	/*
	 * Test 1: Basic use of -C
	 */
	assertMakeDir("test1", 0755);
	assertChdir("test1");
	assertEqualInt(0, systemf("%s -cf archive.tar -C ../d1 file1 -C ../d2 file2", testprog));
	assertEqualInt(0,
	    systemf("%s -xf archive.tar >test.out 2>test.err", testprog));
	assertFileContents("d1/file1", 8, "file1");
	assertFileContents("d2/file2", 8, "file2");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");


	/*
	 * Test 2: Multiple -C
	 */
	assertMakeDir("test2", 0755);
	assertChdir("test2");
	assertEqualInt(0, systemf("%s -cf archive.tar -C .. -C d1 file1 -C .. -C d2 file2", testprog));
	assertEqualInt(0,
	    systemf("%s -xf archive.tar >test.out 2>test.err", testprog));
	assertFileContents("d1/file1", 8, "file1");
	assertFileContents("d2/file2", 8, "file2");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/*
	 * Test 3: -C fail
	 */
	assertMakeDir("test3", 0755);
	assertChdir("test3");
	r = systemf("%s -cf archive.tar -C ../XXX file1 -C ../d2 file2 2>write.err", testprog);
	assert(r != 0);
	assertNonEmptyFile("write.err");
	assertEqualInt(0,
	    systemf("%s -xf archive.tar >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileNotExists("file2");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/*
	 * Test 4: Absolute -C
	 */
	assertMakeDir("test4", 0755);
	assertChdir("test4");
	assertEqualInt(0,
	    systemf("%s -cf archive.tar -C %s/d1 file1",
		testprog, testworkdir));
	assertEqualInt(0,
	    systemf("%s -xf archive.tar >test.out 2>test.err", testprog));
	assertFileContents("d1/file1", 8, "file1");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/*
	 * Test 5: Unnecessary -C ignored even if directory named doesn't exist
	 */
	assertMakeDir("test5", 0755);
	assertChdir("test5");
	assertEqualInt(0,
	    systemf("%s -cf archive.tar -C XXX -C %s/d1 file1",
		testprog, testworkdir));
	assertEqualInt(0,
	    systemf("%s -xf archive.tar >test.out 2>test.err", testprog));
	assertFileContents("d1/file1", 8, "file1");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/*
	 * Test 6: Necessary -C not ignored if directory doesn't exist
	 */
	assertMakeDir("test6", 0755);
	assertChdir("test6");
	r = systemf("%s -cf archive.tar -C XXX -C ../d1 file1 2>write.err",
	    testprog, testworkdir);
	assert(r != 0);
	assertNonEmptyFile("write.err");
	assertEqualInt(0,
	    systemf("%s -xf archive.tar >test.out 2>test.err", testprog));
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/*
	 * Test 7: -C used without specifying directory
	 */
	assertMakeDir("test7", 0755);
	assertChdir("test7");
	r = systemf("%s -cf archive.tar ../d1/file1 -C 2>write.err", testprog);
	assert(r != 0);
	assertNonEmptyFile("write.err");
	assertChdir("..");

	/*
	 * Test 8: -C used with meaningless option ''
	 */
	assertMakeDir("test8", 0755);
	assertChdir("test8");
	r = systemf("%s -cf archive.tar ../d1/file1 -C \"\" 2>write.err",
	    testprog);
	assert(r != 0);
	assertNonEmptyFile("write.err");
	assertChdir("..");
}
