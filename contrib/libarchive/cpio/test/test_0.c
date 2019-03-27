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
 * This first test does basic sanity checks on the environment.  For
 * most of these, we just exit on failure.
 */
#if !defined(_WIN32) || defined(__CYGWIN__)
#define DEV_NULL "/dev/null"
#else
#define DEV_NULL "NUL"
#endif

DEFINE_TEST(test_0)
{
	struct stat st;

	failure("File %s does not exist?!", testprogfile);
	if (!assertEqualInt(0, stat(testprogfile, &st))) {
		fprintf(stderr,
		    "\nFile %s does not exist; aborting test.\n\n",
		    testprog);
		exit(1);
	}

	failure("%s is not executable?!", testprogfile);
	if (!assert((st.st_mode & 0111) != 0)) {
		fprintf(stderr,
		    "\nFile %s not executable; aborting test.\n\n",
		    testprog);
		exit(1);
	}

	/*
	 * Try to successfully run the program; this requires that
	 * we know some option that will succeed.
	 */
	if (0 == systemf("%s --version >" DEV_NULL, testprog)) {
		/* This worked. */
	} else if (0 == systemf("%s -W version >" DEV_NULL, testprog)) {
		/* This worked. */
	} else {
		failure("Unable to successfully run any of the following:\n"
		    "  * %s --version\n"
		    "  * %s -W version\n",
		    testprog, testprog);
		assert(0);
	}

	/* TODO: Ensure that our reference files are available. */
}
