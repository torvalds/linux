/*-
 * Copyright (c) 2003-2008 Tim Kientzle
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

static struct {
	const char *name;
	time_t atime_sec;
} files[] = {
	{ "f0", 0 },
	{ "f1", 0 },
	{ "f2", 0 },
	{ "f3", 0 },
	{ "f4", 0 },
	{ "f5", 0 }
};

/*
 * Create a bunch of test files and record their atimes.
 * For the atime preserve/change tests, the files must have
 * atimes in the past.  We can accomplish this by explicitly invoking
 * utime() on platforms that support it or by simply sleeping
 * for a second after creating the files.  (Creating all of the files
 * at once means we only need to sleep once.)
 */
static void
test_create(void)
{
	struct stat st;
	struct utimbuf times;
	static const int numfiles = sizeof(files) / sizeof(files[0]);
	int i;

	for (i = 0; i < numfiles; ++i) {
		/*
		 * Note: Have to write at least one byte to the file.
		 * cpio doesn't bother reading the file if it's zero length,
		 * so the atime never gets changed in that case, which
		 * makes the tests below rather pointless.
		 */
		assertMakeFile(files[i].name, 0644, "a");

		/* If utime() isn't supported on your platform, just
		 * #ifdef this section out.  Most of the test below is
		 * still valid. */
		memset(&times, 0, sizeof(times));
		times.actime = 1;
		times.modtime = 3;
		assertEqualInt(0, utime(files[i].name, &times));

		/* Record whatever atime the file ended up with. */
		/* If utime() is available, this should be 1, but there's
		 * no harm in being careful. */
		assertEqualInt(0, stat(files[i].name, &st));
		files[i].atime_sec = st.st_atime;
	}

	/* Wait until the atime on the last file is actually in the past. */
	sleepUntilAfter(files[numfiles - 1].atime_sec);
}

DEFINE_TEST(test_option_a)
{
	struct stat st;
	int r;
	char *p;

	/* Create all of the test files. */
	test_create();

	/* Sanity check; verify that atimes really do get modified. */
	p = slurpfile(NULL, "f0");
	assert(p != NULL);
	free(p);
	assertEqualInt(0, stat("f0", &st));
	if (st.st_atime == files[0].atime_sec) {
		skipping("Cannot verify -a option\n"
		    "      Your system appears to not support atime.");
	}
	else
	{
		/*
		 * If this disk is mounted noatime, then we can't
		 * verify correct operation without -a.
		 */

		/* Copy the file without -a; should change the atime. */
		r = systemf("echo %s | %s -pd copy-no-a > copy-no-a.out 2>copy-no-a.err", files[1].name, testprog);
		assertEqualInt(r, 0);
		assertTextFileContents("1 block\n", "copy-no-a.err");
		assertEmptyFile("copy-no-a.out");
		assertEqualInt(0, stat(files[1].name, &st));
		failure("Copying file without -a should have changed atime.");
		assert(st.st_atime != files[1].atime_sec);

		/* Archive the file without -a; should change the atime. */
		r = systemf("echo %s | %s -o > archive-no-a.out 2>archive-no-a.err", files[2].name, testprog);
		assertEqualInt(r, 0);
		assertTextFileContents("1 block\n", "copy-no-a.err");
		assertEqualInt(0, stat(files[2].name, &st));
		failure("Archiving file without -a should have changed atime.");
		assert(st.st_atime != files[2].atime_sec);
	}

	/*
	 * We can, of course, still verify that the atime is unchanged
	 * when using the -a option.
	 */

	/* Copy the file with -a; should not change the atime. */
	r = systemf("echo %s | %s -pad copy-a > copy-a.out 2>copy-a.err",
	    files[3].name, testprog);
	assertEqualInt(r, 0);
	assertTextFileContents("1 block\n", "copy-a.err");
	assertEmptyFile("copy-a.out");
	assertEqualInt(0, stat(files[3].name, &st));
	failure("Copying file with -a should not have changed atime.");
	assertEqualInt(st.st_atime, files[3].atime_sec);

	/* Archive the file with -a; should not change the atime. */
	r = systemf("echo %s | %s -oa > archive-a.out 2>archive-a.err",
	    files[4].name, testprog);
	assertEqualInt(r, 0);
	assertTextFileContents("1 block\n", "copy-a.err");
	assertEqualInt(0, stat(files[4].name, &st));
	failure("Archiving file with -a should not have changed atime.");
	assertEqualInt(st.st_atime, files[4].atime_sec);
}
