/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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

DEFINE_TEST(test_option_a)
{
	size_t s;
	char *p;

	/* Create a file. */
	assertMakeFile("f", 0644, "a");

	/* Test1: archive it with .tar.Z suffix. */
	assertEqualInt(0,
	    systemf("%s -acf test1.tar.Z f 2>test1.err", testprog));
	assertEmptyFile("test1.err");
	/* Check that the archive file has a compress signature. */
	p = slurpfile(&s, "test1.tar.Z");
	assert(s > 2);
	failure("The archive should be compressed");
	assertEqualMem(p, "\x1f\x9d", 2);
	free(p);

	/* Test2: archive it with .taZ suffix. */
	assertEqualInt(0,
	    systemf("%s -acf test2.taZ f 2>test2.err", testprog));
	assertEmptyFile("test2.err");
	/* Check that the archive file has a compress signature. */
	p = slurpfile(&s, "test2.taZ");
	assert(s > 2);
	failure("The archive should be compressed");
	assertEqualMem(p, "\x1f\x9d", 2);
	free(p);

	/* Test3: archive it with .tar.Z.uu suffix. */
	assertEqualInt(0,
	    systemf("%s -acf test3.tar.Z.uu f 2>test3.err", testprog));
	assertEmptyFile("test3.err");
	/* Check that the archive file has a compress signature. */
	p = slurpfile(&s, "test3.tar.Z.uu");
	assert(s > 12);
	failure("The archive should be uuencoded");
	assertEqualMem(p, "begin 644 -\n", 12);
	free(p);

	/* Test4: archive it with .zip suffix. */
	assertEqualInt(0,
	    systemf("%s -acf test4.zip f 2>test4.err", testprog));
	assertEmptyFile("test4.err");
	/* Check that the archive file has a compress signature. */
	p = slurpfile(&s, "test4.zip");
	assert(s > 4);
	failure("The archive should be zipped");
	assertEqualMem(p, "\x50\x4b\x03\x04", 4);
	free(p);

	/* Test5: archive it with .tar.Z suffix and --uuencode option. */
	assertEqualInt(0,
	    systemf("%s -acf test5.tar.Z --uuencode f 2>test5.err",
		testprog));
	assertEmptyFile("test5.err");
	/* Check that the archive file has a compress signature. */
	p = slurpfile(&s, "test5.tar.Z");
	assert(s > 2);
	failure("The archive should be compressed, ignoring --uuencode option");
	assertEqualMem(p, "\x1f\x9d", 2);
	free(p);

	/* Test6: archive it with .xxx suffix(unknown suffix) and
	 * --uuencode option. */
	assertEqualInt(0,
	    systemf("%s -acf test6.xxx --uuencode f 2>test6.err",
		testprog));
	assertEmptyFile("test6.err");
	/* Check that the archive file has a compress signature. */
	p = slurpfile(&s, "test6.xxx");
	assert(s > 12);
	failure("The archive should be uuencoded");
	assertEqualMem(p, "begin 644 -\n", 12);
	free(p);

	/* Test7: archive it with .tar.Z suffix using a long-name option. */
	assertEqualInt(0,
	    systemf("%s --auto-compress -cf test7.tar.Z f 2>test7.err",
		testprog));
	assertEmptyFile("test7.err");
	/* Check that the archive file has a compress signature. */
	p = slurpfile(&s, "test7.tar.Z");
	assert(s > 2);
	failure("The archive should be compressed");
	assertEqualMem(p, "\x1f\x9d", 2);
	free(p);
}
