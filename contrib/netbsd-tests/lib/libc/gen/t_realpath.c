/* $NetBSD: t_realpath.c,v 1.2 2012/03/27 07:54:58 njoly Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_realpath.c,v 1.2 2012/03/27 07:54:58 njoly Exp $");

#include <sys/param.h>

#include <atf-c.h>
#ifdef	__FreeBSD__
#include <errno.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const struct {
	const char *path;
	const char *result;
} paths[] = {

	{ "/",			"/"		},
	{ "///////",		"/"		},
	{ "",			NULL		},
	{ "       ",		NULL		},
	{ "/      ",		NULL		},
	{ "      /",		NULL		},
	{ "/etc///",		"/etc"		},
	{ "///////etc",		"/etc"		},
	{ "/a/b/c/d/e",		NULL		},
	{ "    /usr/bin	   ",	NULL		},
	{ "\\//////usr//bin",	NULL		},
	{ "//usr//bin//",	"/usr/bin"	},
	{ "//////usr//bin//",	"/usr/bin"	},
	{ "/usr/bin//////////", "/usr/bin"	},
};

ATF_TC(realpath_basic);
ATF_TC_HEAD(realpath_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of realpath(3)");
}

ATF_TC_BODY(realpath_basic, tc)
{
	char buf[MAXPATHLEN];
	char *ptr;
	size_t i;

	for (i = 0; i < __arraycount(paths); i++) {

		(void)memset(buf, '\0', sizeof(buf));

		ptr = realpath(paths[i].path, buf);

		if (ptr == NULL && paths[i].result == NULL)
			continue;

		if (ptr == NULL && paths[i].result != NULL)
			atf_tc_fail("realpath failed for '%s'", paths[i].path);

		if (strcmp(paths[i].result, buf) != 0)
			atf_tc_fail("expected '%s', got '%s'",
			    paths[i].result, buf);
	}
}

ATF_TC(realpath_huge);
ATF_TC_HEAD(realpath_huge, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test huge path with realpath(3)");
}

ATF_TC_BODY(realpath_huge, tc)
{
	char result[MAXPATHLEN] = { 0 };
	char buffer[MAXPATHLEN] = { 0 };

	(void)memset(buffer, '/', sizeof(buffer) - 1);

	ATF_CHECK(realpath(buffer, result) != NULL);
	ATF_CHECK(strlen(result) == 1);
	ATF_CHECK(result[0] == '/');
}

ATF_TC(realpath_symlink);
ATF_TC_HEAD(realpath_symlink, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test symbolic link with realpath(3)");
}

ATF_TC_BODY(realpath_symlink, tc)
{
	char path[MAXPATHLEN] = { 0 };
	char slnk[MAXPATHLEN] = { 0 };
	char resb[MAXPATHLEN] = { 0 };
	int fd;

#ifdef	__FreeBSD__
	ATF_REQUIRE_MSG(getcwd(path, sizeof(path)) != NULL,
	    "getcwd(path) failed: %s", strerror(errno));
	ATF_REQUIRE_MSG(getcwd(slnk, sizeof(slnk)) != NULL,
	    "getcwd(slnk) failed: %s", strerror(errno));
#else
	(void)getcwd(path, sizeof(path));
	(void)getcwd(slnk, sizeof(slnk));
#endif

	(void)strlcat(path, "/realpath", sizeof(path));
	(void)strlcat(slnk, "/symbolic", sizeof(slnk));

	fd = open(path, O_RDONLY | O_CREAT, 0600);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(symlink(path, slnk) == 0);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(realpath(slnk, resb) != NULL);
	ATF_REQUIRE(strcmp(resb, path) == 0);

	ATF_REQUIRE(unlink(path) == 0);
	ATF_REQUIRE(unlink(slnk) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, realpath_basic);
	ATF_TP_ADD_TC(tp, realpath_huge);
	ATF_TP_ADD_TC(tp, realpath_symlink);

	return atf_no_error();
}
