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

#define USTAR_OPT " --format=ustar"

DEFINE_TEST(test_option_b)
{
	char *testprog_ustar;

	assertMakeFile("file1", 0644, "file1");
	if (systemf("cat file1 > test_cat.out 2> test_cat.err") != 0) {
		skipping("This test requires a `cat` program");
		return;
	}
	testprog_ustar = malloc(strlen(testprog) + sizeof(USTAR_OPT) + 1);
	strcpy(testprog_ustar, testprog);
	strcat(testprog_ustar, USTAR_OPT);

	/*
	 * Bsdtar does not pad if the output is going directly to a disk file.
	 */
	assertEqualInt(0, systemf("%s -cf archive1.tar file1 >test1.out 2>test1.err", testprog_ustar));
	failure("bsdtar does not pad archives written directly to regular files");
	assertFileSize("archive1.tar", 2048);
	assertEmptyFile("test1.out");
	assertEmptyFile("test1.err");

	/*
	 * Bsdtar does pad to the block size if the output is going to a socket.
	 */
	/* Default is -b 20 */
	assertEqualInt(0, systemf("%s -cf - file1 2>test2.err | cat >archive2.tar ", testprog_ustar));
	failure("bsdtar does pad archives written to pipes");
	assertFileSize("archive2.tar", 10240);
	assertEmptyFile("test2.err");

	assertEqualInt(0, systemf("%s -cf - -b 20 file1 2>test3.err | cat >archive3.tar ", testprog_ustar));
	assertFileSize("archive3.tar", 10240);
	assertEmptyFile("test3.err");

	assertEqualInt(0, systemf("%s -cf - -b 10 file1 2>test4.err | cat >archive4.tar ", testprog_ustar));
	assertFileSize("archive4.tar", 5120);
	assertEmptyFile("test4.err");

	assertEqualInt(0, systemf("%s -cf - -b 1 file1 2>test5.err | cat >archive5.tar ", testprog_ustar));
	assertFileSize("archive5.tar", 2048);
	assertEmptyFile("test5.err");

	assertEqualInt(0, systemf("%s -cf - -b 8192 file1 2>test6.err | cat >archive6.tar ", testprog_ustar));
	assertFileSize("archive6.tar", 4194304);
	assertEmptyFile("test6.err");

	/*
	 * Note: It's not possible to verify at this level that blocks
	 * are getting written with the
	 */

	free(testprog_ustar);
}
