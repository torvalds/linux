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

DEFINE_TEST(test_option_n)
{
	assertMakeDir("d1", 0755);
	assertMakeFile("d1/file1", 0644, "d1/file1");

	/* Test 1: -c without -n */
	assertMakeDir("test1", 0755);
	assertChdir("test1");
	assertEqualInt(0,
	    systemf("%s -cf archive.tar -C .. d1 >c.out 2>c.err", testprog));
	assertEmptyFile("c.out");
	assertEmptyFile("c.err");
	assertEqualInt(0,
	    systemf("%s -xf archive.tar >x.out 2>x.err", testprog));
	assertEmptyFile("x.out");
	assertEmptyFile("x.err");
	assertFileContents("d1/file1", 8, "d1/file1");
	assertChdir("..");

	/* Test 2: -c with -n */
	assertMakeDir("test2", 0755);
	assertChdir("test2");
	assertEqualInt(0,
	    systemf("%s -cnf archive.tar -C .. d1 >c.out 2>c.err", testprog));
	assertEmptyFile("c.out");
	assertEmptyFile("c.err");
	assertEqualInt(0,
	    systemf("%s -xf archive.tar >x.out 2>x.err", testprog));
	assertEmptyFile("x.out");
	assertEmptyFile("x.err");
	assertIsDir("d1", umasked(0755));
	assertFileNotExists("d1/file1");
	assertChdir("..");
}
