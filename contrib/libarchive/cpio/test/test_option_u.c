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
#if defined(HAVE_UTIME_H)
#include <utime.h>
#elif defined(HAVE_SYS_UTIME_H)
#include <sys/utime.h>
#endif
__FBSDID("$FreeBSD$");

DEFINE_TEST(test_option_u)
{
	struct utimbuf times;
	char *p;
	size_t s;
	int r;

	/* Create a file. */
	assertMakeFile("f", 0644, "a");

	/* Copy the file to the "copy" dir. */
	r = systemf("echo f| %s -pd copy >copy.out 2>copy.err",
	    testprog);
	assertEqualInt(r, 0);

	/* Check that the file contains only a single "a" */
	p = slurpfile(&s, "copy/f");
	assertEqualInt(s, 1);
	assertEqualMem(p, "a", 1);
	free(p);

	/* Recreate the file with a single "b" */
	assertMakeFile("f", 0644, "b");

	/* Set the mtime to the distant past. */
	memset(&times, 0, sizeof(times));
	times.actime = 1;
	times.modtime = 3;
	assertEqualInt(0, utime("f", &times));

	/* Copy the file to the "copy" dir. */
	r = systemf("echo f| %s -pd copy >copy.out 2>copy.err",
	    testprog);
	assertEqualInt(r, 0);

	/* Verify that the file hasn't changed (it wasn't overwritten) */
	p = slurpfile(&s, "copy/f");
	assertEqualInt(s, 1);
	assertEqualMem(p, "a", 1);
	free(p);

	/* Copy the file to the "copy" dir with -u (force) */
	r = systemf("echo f| %s -pud copy >copy.out 2>copy.err",
	    testprog);
	assertEqualInt(r, 0);

	/* Verify that the file has changed (it was overwritten) */
	p = slurpfile(&s, "copy/f");
	assertEqualInt(s, 1);
	assertEqualMem(p, "b", 1);
	free(p);
}
