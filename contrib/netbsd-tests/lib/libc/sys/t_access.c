/* $NetBSD: t_access.c,v 2.2 2017/01/10 22:36:29 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_access.c,v 1.2 2017/01/10 22:36:29 christos Exp $");

#ifdef __FreeBSD__
#include <sys/param.h> /* For __FreeBSD_version */
#endif

#include <atf-c.h>

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static const char path[] = "access";
static const int mode[4] = { R_OK, W_OK, X_OK, F_OK };

ATF_TC_WITH_CLEANUP(access_access);
ATF_TC_HEAD(access_access, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test access(2) for EACCES");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(access_access, tc)
{
	const int perm[3] = { 0200, 0400, 0000 };
	size_t i;
	int fd;

	fd = open(path, O_RDONLY | O_CREAT);

	if (fd < 0)
		return;

	for (i = 0; i < __arraycount(mode) - 1; i++) {

		ATF_REQUIRE(fchmod(fd, perm[i]) == 0);

		errno = 0;

		ATF_REQUIRE(access(path, mode[i]) != 0);
		ATF_REQUIRE(errno == EACCES);
	}

	ATF_REQUIRE(close(fd) == 0);
}

ATF_TC_CLEANUP(access_access, tc)
{
	(void)unlink(path);
}

ATF_TC(access_fault);
ATF_TC_HEAD(access_fault, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test access(2) for EFAULT");
}

ATF_TC_BODY(access_fault, tc)
{
	size_t i;

	for (i = 0; i < __arraycount(mode); i++) {

		errno = 0;

		ATF_REQUIRE(access(NULL, mode[i]) != 0);
		ATF_REQUIRE(errno == EFAULT);

		errno = 0;

		ATF_REQUIRE(access((char *)-1, mode[i]) != 0);
		ATF_REQUIRE(errno == EFAULT);
	}
}

ATF_TC(access_inval);
ATF_TC_HEAD(access_inval, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test access(2) for EINVAL");
}

ATF_TC_BODY(access_inval, tc)
{

#if defined(__FreeBSD__) && __FreeBSD_version < 1100033
	atf_tc_expect_fail("arguments to access aren't validated; see "
	    "bug # 181155 for more details");
#endif
	errno = 0;

	ATF_REQUIRE(access("/usr", -1) != 0);
	ATF_REQUIRE(errno == EINVAL);
}

ATF_TC(access_notdir);
ATF_TC_HEAD(access_notdir, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test access(2) for ENOTDIR");
}

ATF_TC_BODY(access_notdir, tc)
{
	size_t i;

	for (i = 0; i < __arraycount(mode); i++) {

		errno = 0;

		/*
		 *  IEEE Std 1003.1-2008 about ENOTDIR:
		 *
		 *  "A component of the path prefix is not a directory,
		 *   or the path argument contains at least one non-<slash>
		 *   character and ends with one or more trailing <slash>
		 *   characters and the last pathname component names an
		 *   existing file that is neither a directory nor a symbolic
		 *   link to a directory."
		 */
		ATF_REQUIRE(access("/etc/passwd//", mode[i]) != 0);
		ATF_REQUIRE(errno == ENOTDIR);
	}
}

ATF_TC(access_notexist);
ATF_TC_HEAD(access_notexist, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test access(2) for ENOENT");
}

ATF_TC_BODY(access_notexist, tc)
{
	size_t i;

	for (i = 0; i < __arraycount(mode); i++) {

		errno = 0;

		ATF_REQUIRE(access("", mode[i]) != 0);
		ATF_REQUIRE(errno == ENOENT);
	}
}

ATF_TC(access_toolong);
ATF_TC_HEAD(access_toolong, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test access(2) for ENAMETOOLONG");
}

ATF_TC_BODY(access_toolong, tc)
{
	char *buf;
	size_t i;

	buf = malloc(PATH_MAX);

	if (buf == NULL)
		return;

	for (i = 0; i < PATH_MAX; i++)
		buf[i] = 'x';

	for (i = 0; i < __arraycount(mode); i++) {

		errno = 0;

		ATF_REQUIRE(access(buf, mode[i]) != 0);
		ATF_REQUIRE(errno == ENAMETOOLONG);
	}

	free(buf);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, access_access);
	ATF_TP_ADD_TC(tp, access_fault);
	ATF_TP_ADD_TC(tp, access_inval);
	ATF_TP_ADD_TC(tp, access_notdir);
	ATF_TP_ADD_TC(tp, access_notexist);
	ATF_TP_ADD_TC(tp, access_toolong);

	return atf_no_error();
}
