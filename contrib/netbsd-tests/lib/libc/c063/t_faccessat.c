/*	$NetBSD: t_faccessat.c,v 1.3 2017/01/10 15:13:56 christos Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus.
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
__RCSID("$NetBSD: t_faccessat.c,v 1.3 2017/01/10 15:13:56 christos Exp $");

#include <sys/param.h>
#include <sys/stat.h>
#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DIR "dir"
#define FILE "dir/faccessat"
#define BASEFILE "faccessat"
#define LINK "dir/symlink"
#define BASELINK "symlink"
#define FILEERR "dir/faccessaterr"

ATF_TC(faccessat_fd);
ATF_TC_HEAD(faccessat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that faccessat works with fd");
}
ATF_TC_BODY(faccessat_fd, tc)
{
	int dfd;
	int fd;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(faccessat(dfd, BASEFILE, F_OK, 0) == 0);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC(faccessat_fdcwd);
ATF_TC_HEAD(faccessat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that faccessat works with fd as AT_FDCWD");
}
ATF_TC_BODY(faccessat_fdcwd, tc)
{
	int fd;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(chdir(DIR) == 0);
	ATF_REQUIRE(faccessat(AT_FDCWD, BASEFILE, F_OK, 0) == 0);
}

ATF_TC(faccessat_fdcwderr);
ATF_TC_HEAD(faccessat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that faccessat fails with fd as AT_FDCWD and bad path");
}
ATF_TC_BODY(faccessat_fdcwderr, tc)
{
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(faccessat(AT_FDCWD, FILEERR, F_OK, 0) == -1);
}

ATF_TC(faccessat_fderr1);
ATF_TC_HEAD(faccessat_fderr1, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that faccessat fail with bad path");
}
ATF_TC_BODY(faccessat_fderr1, tc)
{
	int dfd;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(faccessat(dfd, FILEERR, F_OK, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC(faccessat_fderr2);
ATF_TC_HEAD(faccessat_fderr2, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that faccessat fails with bad fdat");
}
ATF_TC_BODY(faccessat_fderr2, tc)
{
	int dfd;
	int fd;
	char cwd[MAXPATHLEN];

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE((dfd = open(getcwd(cwd, MAXPATHLEN), O_RDONLY, 0)) != -1);
	ATF_REQUIRE(faccessat(dfd, BASEFILE, F_OK, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC(faccessat_fderr3);
ATF_TC_HEAD(faccessat_fderr3, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that faccessat fails with fd as -1");
}
ATF_TC_BODY(faccessat_fderr3, tc)
{
	int fd;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(faccessat(-1, FILE, F_OK, 0) == -1);
}

ATF_TC(faccessat_fdlink);
ATF_TC_HEAD(faccessat_fdlink, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that faccessat works on symlink");
}
ATF_TC_BODY(faccessat_fdlink, tc)
{
	int dfd;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(symlink(FILE, LINK) == 0); /* NB: FILE does not exists */

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);

	ATF_REQUIRE(faccessat(dfd, BASELINK, F_OK, 0) == -1);
	ATF_REQUIRE(errno == ENOENT);

#ifdef __FreeBSD__
	atf_tc_expect_fail("Depends on non-standard behavior not mentioned in POSIX.1-2008");
#endif
	ATF_REQUIRE(faccessat(dfd, BASELINK, F_OK, AT_SYMLINK_NOFOLLOW) == 0);

	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, faccessat_fd);
	ATF_TP_ADD_TC(tp, faccessat_fdcwd);
	ATF_TP_ADD_TC(tp, faccessat_fdcwderr);
	ATF_TP_ADD_TC(tp, faccessat_fderr1);
	ATF_TP_ADD_TC(tp, faccessat_fderr2);
	ATF_TP_ADD_TC(tp, faccessat_fderr3);
	ATF_TP_ADD_TC(tp, faccessat_fdlink);

	return atf_no_error();
}
