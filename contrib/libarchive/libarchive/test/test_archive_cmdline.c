/*-
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

#define __LIBARCHIVE_TEST
#include "archive_cmdline_private.h"

DEFINE_TEST(test_archive_cmdline)
{
	struct archive_cmdline *cl;

	/* Command name only. */
	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl, "gzip"));
	assertEqualInt(1, cl->argc);
	assertEqualString("gzip", cl->path);
	assertEqualString("gzip", cl->argv[0]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));

	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl, "gzip "));
	assertEqualInt(1, cl->argc);
	failure("path should not include a space character");
	assertEqualString("gzip", cl->path);
	failure("arg0 should not include a space character");
	assertEqualString("gzip", cl->argv[0]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));

	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl,
	    "/usr/bin/gzip "));
	assertEqualInt(1, cl->argc);
	failure("path should be a full path");
	assertEqualString("/usr/bin/gzip", cl->path);
	failure("arg0 should not be a full path");
	assertEqualString("gzip", cl->argv[0]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));

	/* A command line includes space character. */
	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl, "\"gzip \""));
	assertEqualInt(1, cl->argc);
	failure("path should include a space character");
	assertEqualString("gzip ", cl->path);
	failure("arg0 should include a space character");
	assertEqualString("gzip ", cl->argv[0]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));

	/* A command line includes space character: pattern 2.*/
	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl, "\"gzip \"x"));
	assertEqualInt(1, cl->argc);
	failure("path should include a space character");
	assertEqualString("gzip x", cl->path);
	failure("arg0 should include a space character");
	assertEqualString("gzip x", cl->argv[0]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));

	/* A command line includes space character: pattern 3.*/
	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl,
	    "\"gzip \"x\" s \""));
	assertEqualInt(1, cl->argc);
	failure("path should include a space character");
	assertEqualString("gzip x s ", cl->path);
	failure("arg0 should include a space character");
	assertEqualString("gzip x s ", cl->argv[0]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));

	/* A command line includes space character: pattern 4.*/
	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl,
	    "\"gzip\\\" \""));
	assertEqualInt(1, cl->argc);
	failure("path should include a space character");
	assertEqualString("gzip\" ", cl->path);
	failure("arg0 should include a space character");
	assertEqualString("gzip\" ", cl->argv[0]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));

	/* A command name with a argument. */
	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl, "gzip -d"));
	assertEqualInt(2, cl->argc);
	assertEqualString("gzip", cl->path);
	assertEqualString("gzip", cl->argv[0]);
	assertEqualString("-d", cl->argv[1]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));

	/* A command name with two arguments. */
	assert((cl = __archive_cmdline_allocate()) != NULL);
	if (cl == NULL)
		return;
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_parse(cl, "gzip -d -q"));
	assertEqualInt(3, cl->argc);
	assertEqualString("gzip", cl->path);
	assertEqualString("gzip", cl->argv[0]);
	assertEqualString("-d", cl->argv[1]);
	assertEqualString("-q", cl->argv[2]);
	assertEqualInt(ARCHIVE_OK, __archive_cmdline_free(cl));
}
