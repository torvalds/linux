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

DEFINE_TEST(test_option_exclude)
{
	int r;

	assertMakeFile("file1", 0644, "file1");
	assertMakeFile("file2", 0644, "file2");
	assertEqualInt(0, systemf("%s -cf archive.tar file1 file2", testprog));

	/*
	 * Now, try extracting from the test archive with various --exclude options.
	 */

	/* Test 1: Without --exclude */
	assertMakeDir("test1", 0755);
	assertChdir("test1");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar >test.out 2>test.err", testprog));
	assertFileContents("file1", 5, "file1");
	assertFileContents("file2", 5, "file2");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 2: Selecting just one file */
	assertMakeDir("test2", 0755);
	assertChdir("test2");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar file1 >test.out 2>test.err", testprog));
	assertFileContents("file1", 5, "file1");
	assertFileNotExists("file2");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 3: Use --exclude to skip one file */
	assertMakeDir("test3", 0755);
	assertChdir("test3");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar --exclude file1 >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileContents("file2", 5, "file2");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 4: Selecting one valid and one invalid file */
	assertMakeDir("test4", 0755);
	assertChdir("test4");
	r = systemf("%s -xf ../archive.tar file1 file3 >test.out 2>test.err", testprog);
	assert(r != 0);
	assertFileContents("file1", 5, "file1");
	assertFileNotExists("file2");
	assertFileNotExists("file3");
	assertEmptyFile("test.out");
	assertNonEmptyFile("test.err");
	assertChdir("..");

	/* Test 5: Selecting one valid file twice */
	assertMakeDir("test5", 0755);
	assertChdir("test5");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar file1 file1 >test.out 2>test.err", testprog));
	assertFileContents("file1", 5, "file1");
	assertFileNotExists("file2");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 6: Include and exclude the same file */
	assertMakeDir("test6", 0755);
	assertChdir("test6");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar --exclude file1 file1 >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileNotExists("file2");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 7: Exclude a non-existent file */
	assertMakeDir("test7", 0755);
	assertChdir("test7");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar --exclude file3 file1 >test.out 2>test.err", testprog));
	assertFileContents("file1", 5, "file1");
	assertFileNotExists("file2");
	assertFileNotExists("file3");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 8: Include a non-existent file */
	assertMakeDir("test8", 0755);
	assertChdir("test8");
	r = systemf("%s -xf ../archive.tar file3 >test.out 2>test.err", testprog);
	assert(r != 0);
	assertFileNotExists("file1");
	assertFileNotExists("file2");
	assertFileNotExists("file3");
	assertEmptyFile("test.out");
	assertNonEmptyFile("test.err");
	assertChdir("..");

	/* Test 9: Include a non-existent file plus an exclusion */
	assertMakeDir("test9", 0755);
	assertChdir("test9");
	r = systemf("%s -xf ../archive.tar --exclude file1 file3 >test.out 2>test.err", testprog);
	assert(r != 0);
	assertFileNotExists("file1");
	assertFileNotExists("file2");
	assertFileNotExists("file3");
	assertEmptyFile("test.out");
	assertNonEmptyFile("test.err");
	assertChdir("..");
}
