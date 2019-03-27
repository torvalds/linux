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

/*
 * Test that "--help", "-h", and "-W help" options all work and
 * generate reasonable output.
 */

static int
in_first_line(const char *p, const char *substring)
{
	size_t l = strlen(substring);

	while (*p != '\0' && *p != '\n') {
		if (memcmp(p, substring, l) == 0)
			return (1);
		++p;
	}
	return (0);
}

DEFINE_TEST(test_help)
{
	int r;
	char *p;
	size_t plen;

	/* Exercise --help option. */
	r = systemf("%s --help >help.stdout 2>help.stderr", testprog);
	assertEqualInt(r, 0);
	failure("--help should generate nothing to stderr.");
	assertEmptyFile("help.stderr");
	/* Help message should start with name of program. */
	p = slurpfile(&plen, "help.stdout");
	failure("Help output should be long enough.");
	assert(plen >= 6);
	failure("First line of help output should contain 'bsdtar': %s", p);
	assert(in_first_line(p, "bsdtar"));
	/*
	 * TODO: Extend this check to further verify that --help output
	 * looks approximately right.
	 */
	free(p);

	/* -h option should generate the same output. */
	r = systemf("%s -h >h.stdout 2>h.stderr", testprog);
	assertEqualInt(r, 0);
	failure("-h should generate nothing to stderr.");
	assertEmptyFile("h.stderr");
	failure("stdout should be same for -h and --help");
	assertEqualFile("h.stdout", "help.stdout");

	/* -W help should be another synonym. */
	r = systemf("%s -W help >Whelp.stdout 2>Whelp.stderr", testprog);
	assertEqualInt(r, 0);
	failure("-W help should generate nothing to stderr.");
	assertEmptyFile("Whelp.stderr");
	failure("stdout should be same for -W help and --help");
	assertEqualFile("Whelp.stdout", "help.stdout");
}
