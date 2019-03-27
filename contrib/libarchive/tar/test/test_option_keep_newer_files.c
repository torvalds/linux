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

DEFINE_TEST(test_option_keep_newer_files)
{
	const char *reffile = "test_option_keep_newer_files.tar.Z";

	/* Reference file has one entry "file" with a very old timestamp. */
	extract_reference_file(reffile);

	/* Test 1: Without --keep-newer-files */
	assertMakeDir("test1", 0755);
	assertChdir("test1");
	assertMakeFile("file", 0644, "new");
	assertEqualInt(0,
	    systemf("%s -xf ../%s >test.out 2>test.err", testprog, reffile));
	assertFileContents("old\n", 4, "file");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");

	/* Test 2: With --keep-newer-files */
	assertMakeDir("test2", 0755);
	assertChdir("test2");
	assertMakeFile("file", 0644, "new");
	assertEqualInt(0,
	    systemf("%s -xf ../%s --keep-newer-files >test.out 2>test.err", testprog, reffile));
	assertFileContents("new", 3, "file");
	assertEmptyFile("test.out");
	assertEmptyFile("test.err");
	assertChdir("..");
}
