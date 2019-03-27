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

/* This is a little pointless, as Windows doesn't support symlinks
 * (except for the seriously crippled CreateSymbolicLink API) so these
 * tests won't run on Windows. */
#if defined(_WIN32) && !defined(__CYGWIN__)
#define CAT "type"
#else
#define CAT "cat"
#endif

DEFINE_TEST(test_option_L_upper)
{
	FILE *filelist;
	int r;

	if (!canSymlink()) {
		skipping("Symlink tests");
		return;
	}

	filelist = fopen("filelist", "w");

	/* Create a file and a symlink to the file. */
	assertMakeFile("file", 0644, "1234567890");
	fprintf(filelist, "file\n");

	/* Symlink to above file. */
	assertMakeSymlink("symlink", "file");
	fprintf(filelist, "symlink\n");

	fclose(filelist);

	r = systemf(CAT " filelist | %s -pd copy >copy.out 2>copy.err", testprog);
	assertEqualInt(r, 0);
	assertTextFileContents("1 block\n", "copy.err");

	failure("Regular -p without -L should preserve symlinks.");
	assertIsSymlink("copy/symlink", NULL);

	r = systemf(CAT " filelist | %s -pd -L copy-L >copy-L.out 2>copy-L.err", testprog);
	assertEqualInt(r, 0);
	assertEmptyFile("copy-L.out");
	assertTextFileContents("1 block\n", "copy-L.err");
	failure("-pdL should dereference symlinks and turn them into files.");
	assertIsReg("copy-L/symlink", -1);

	r = systemf(CAT " filelist | %s -o >archive.out 2>archive.err", testprog);
	failure("Error invoking %s -o ", testprog);
	assertEqualInt(r, 0);
	assertTextFileContents("1 block\n", "archive.err");

	assertMakeDir("unpack", 0755);
	assertChdir("unpack");
	r = systemf(CAT " ../archive.out | %s -i >unpack.out 2>unpack.err", testprog);
	failure("Error invoking %s -i", testprog);
	assertEqualInt(r, 0);
	assertTextFileContents("1 block\n", "unpack.err");
	assertChdir("..");

	assertIsSymlink("unpack/symlink", NULL);

	r = systemf(CAT " filelist | %s -oL >archive-L.out 2>archive-L.err", testprog);
	failure("Error invoking %s -oL", testprog);
	assertEqualInt(r, 0);
	assertTextFileContents("1 block\n", "archive-L.err");

	assertMakeDir("unpack-L", 0755);
	assertChdir("unpack-L");
	r = systemf(CAT " ../archive-L.out | %s -i >unpack-L.out 2>unpack-L.err", testprog);
	failure("Error invoking %s -i < archive-L.out", testprog);
	assertEqualInt(r, 0);
	assertTextFileContents("1 block\n", "unpack-L.err");
	assertChdir("..");
	assertIsReg("unpack-L/symlink", -1);
}
