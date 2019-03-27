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

DEFINE_TEST(test_option_X_upper)
{
	int r;

	/*
	 * Create a sample archive.
	 */
	assertMakeFile("file1", 0644, "file1");
	assertMakeFile("file2", 0644, "file2");
	assertMakeFile("file3a", 0644, "file3a");
	assertMakeFile("file4a", 0644, "file4a");
	assertEqualInt(0,
	    systemf("%s -cf archive.tar file1 file2 file3a file4a", testprog));

	/*
	 * Now, try extracting from the test archive with various -X usage.
	 */

	/* Test 1: Without -X */
	assertMakeDir("test1", 0755);
	assertChdir("test1");
	r = systemf("%s -xf ../archive.tar >test.out 2>test.err",
	    testprog);
	if (!assertEqualInt(0, r))
		return;

	assertFileContents("file1", 5, "file1");
	assertFileContents("file2", 5, "file2");
	assertFileContents("file3a", 6, "file3a");
	assertFileContents("file4a", 6, "file4a");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 2: Use -X to skip one file */
	assertMakeDir("test2", 0755);
	assertChdir("test2");
	assertMakeFile("exclusions", 0644, "file1\n");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar -X exclusions >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileContents("file2", 5, "file2");
	assertFileContents("file3a", 6, "file3a");
	assertFileContents("file4a", 6, "file4a");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 3: Use -X to skip multiple files */
	assertMakeDir("test3", 0755);
	assertChdir("test3");
	assertMakeFile("exclusions", 0644, "file1\nfile2\n");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar -X exclusions >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileNotExists("file2");
	assertFileContents("file3a", 6, "file3a");
	assertFileContents("file4a", 6, "file4a");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 4: Omit trailing \n */
	assertMakeDir("test4", 0755);
	assertChdir("test4");
	assertMakeFile("exclusions", 0644, "file1\nfile2");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar -X exclusions >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileNotExists("file2");
	assertFileContents("file3a", 6, "file3a");
	assertFileContents("file4a", 6, "file4a");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 5: include/exclude without overlap */
	assertMakeDir("test5", 0755);
	assertChdir("test5");
	assertMakeFile("exclusions", 0644, "file1\nfile2");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar -X exclusions file3a >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileNotExists("file2");
	assertFileContents("file3a", 6, "file3a");
	assertFileNotExists("file4a");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 6: Overlapping include/exclude */
	assertMakeDir("test6", 0755);
	assertChdir("test6");
	assertMakeFile("exclusions", 0644, "file1\nfile2");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar -X exclusions file1 file3a >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileNotExists("file2");
	assertFileContents("file3a", 6, "file3a");
	assertFileNotExists("file4a");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 7: with pattern */
	assertMakeDir("test7", 0755);
	assertChdir("test7");
	assertMakeFile("exclusions", 0644, "file*a\nfile1");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar -X exclusions >test.out 2>test.err", testprog));
	assertFileNotExists("file1");
	assertFileContents("file2", 5, "file2");
	assertFileNotExists("file3a");
	assertFileNotExists("file4a");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 8: with empty exclusions file */
	assertMakeDir("test8", 0755);
	assertChdir("test8");
	assertMakeFile("exclusions", 0644, "");
	assertEqualInt(0,
	    systemf("%s -xf ../archive.tar -X exclusions >test.out 2>test.err", testprog));
	assertFileContents("file1", 5, "file1");
	assertFileContents("file2", 5, "file2");
	assertFileContents("file3a", 6, "file3a");
	assertFileContents("file4a", 6, "file4a");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");
}
