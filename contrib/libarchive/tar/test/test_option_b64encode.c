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

DEFINE_TEST(test_option_b64encode)
{
	char *p;
	size_t s;

	/* Create a file. */
	assertMakeFile("f", 0644, "a");

	/* Archive it with compress compression and uuencode. */
	assertEqualInt(0,
	    systemf("%s -cf - -Z --b64encode f >archive.out 2>archive.err",
	    testprog));
	/* Check that the archive file has an uuencode signature. */
	p = slurpfile(&s, "archive.out");
	assert(s > 2);
	assertEqualMem(p, "begin-base64 644", 16);
	free(p);

	/* Archive it with uuencode only. */
	assertEqualInt(0,
	    systemf("%s -cf - --b64encode f >archive.out 2>archive.err",
	    testprog));
	/* Check that the archive file has an uuencode signature. */
	p = slurpfile(&s, "archive.out");
	assert(s > 2);
	assertEqualMem(p, "begin-base64 644", 16);
	free(p);
}
