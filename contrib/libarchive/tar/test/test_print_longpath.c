/*-
 * Copyright (c) 2011 Michihiro NAKAJIMA
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

DEFINE_TEST(test_print_longpath)
{
	const char *reffile = "test_print_longpath.tar.Z";
	char buff[2048];
	int i, j, k;

	/* Reference file has one entry "file" with a very old timestamp. */
	extract_reference_file(reffile);

	/* Build long path pattern. */
	memset(buff, 0, sizeof(buff));
	for (k = 0; k < 4; k++) {
		for (j = 0; j < k+1; j++) {
			for (i = 0; i < 10; i++)
				strncat(buff, "0123456789",
				    sizeof(buff) - strlen(buff) -1);
			strncat(buff, "/", sizeof(buff) - strlen(buff) -1);
		}
		strncat(buff, "\n", sizeof(buff) - strlen(buff) -1);
	}
	buff[sizeof(buff)-1] = '\0';

	assertEqualInt(0,
	    systemf("%s -tf %s >test.out 2>test.err", testprog, reffile));
	assertTextFileContents(buff, "test.out");
	assertEmptyFile("test.err");
}
