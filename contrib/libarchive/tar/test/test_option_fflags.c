/*-
 * Copyright (c) 2017 Martin Matuska
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

#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__BORLANDC__)
#define chmod _chmod
#endif

static void
clear_fflags(const char *pathname)
{
#if defined(HAVE_STRUCT_STAT_ST_FLAGS)
	chflags(pathname, 0);
#elif (defined(FS_IOC_GETFLAGS) && defined(HAVE_WORKING_FS_IOC_GETFLAGS)) || \
      (defined(EXT2_IOC_GETFLAGS) && defined(HAVE_WORKING_EXT2_IOC_GETFLAGS))
	int fd;

	fd = open(pathname, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return;
	ioctl(fd,
#ifdef FS_IOC_GETFLAGS
	    FS_IOC_GETFLAGS,
#else
	    EXT2_IOC_GETFLAGS,
#endif
	    0);
#else
	(void)pathname; /* UNUSED */
#endif
	return;
}

DEFINE_TEST(test_option_fflags)
{
	int r;

	if (!canNodump()) {
		skipping("Can't test nodump flag on this filesystem");
		return;
	}

	/* Create a file. */
	assertMakeFile("f", 0644, "a");

	/* Set nodump flag on the file */
	assertSetNodump("f");

	/* FreeBSD ZFS workaround: ZFS sets uarch on all touched files and dirs */
	chmod("f", 0644);

	/* Archive it with fflags */
	r = systemf("%s -c --fflags -f fflags.tar f >fflags.out 2>fflags.err", testprog);
	assertEqualInt(r, 0);

	/* Archive it without fflags */
	r = systemf("%s -c --no-fflags -f nofflags.tar f >nofflags.out 2>nofflags.err", testprog);
	assertEqualInt(r, 0);

	/* Extract fflags with fflags */
	assertMakeDir("fflags_fflags", 0755);
	clear_fflags("fflags_fflags");
	r = systemf("%s -x -C fflags_fflags --no-same-permissions --fflags -f fflags.tar >fflags_fflags.out 2>fflags_fflags.err", testprog);
	assertEqualInt(r, 0);
	assertEqualFflags("f", "fflags_fflags/f");

	/* Extract fflags without fflags */
	assertMakeDir("fflags_nofflags", 0755);
	clear_fflags("fflags_nofflags");
	r = systemf("%s -x -C fflags_nofflags -p --no-fflags -f fflags.tar >fflags_nofflags.out 2>fflags_nofflags.err", testprog);
	assertEqualInt(r, 0);
	assertUnequalFflags("f", "fflags_nofflags/f");

	/* Extract nofflags with fflags */
	assertMakeDir("nofflags_fflags", 0755);
	clear_fflags("nofflags_fflags");
	r = systemf("%s -x -C nofflags_fflags --no-same-permissions --fflags -f nofflags.tar >nofflags_fflags.out 2>nofflags_fflags.err", testprog);
	assertEqualInt(r, 0);	
	assertUnequalFflags("f", "nofflags_fflags/f");

	/* Extract nofflags with nofflags */
	assertMakeDir("nofflags_nofflags", 0755);
	clear_fflags("nofflags_nofflags");
	r = systemf("%s -x -C nofflags_nofflags -p --no-fflags -f nofflags.tar >nofflags_nofflags.out 2>nofflags_nofflags.err", testprog);
	assertEqualInt(r, 0);
	assertUnequalFflags("f", "nofflags_nofflags/f");
}
