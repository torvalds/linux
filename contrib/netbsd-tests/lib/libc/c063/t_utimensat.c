/*	$NetBSD: t_utimensat.c,v 1.6 2017/01/10 15:13:56 christos Exp $ */

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
__RCSID("$NetBSD: t_utimensat.c,v 1.6 2017/01/10 15:13:56 christos Exp $");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DIR "dir"
#define FILE "dir/utimensat"
#define BASEFILE "utimensat"
#define LINK "dir/symlink"
#define BASELINK "symlink"
#define FILEERR "dir/symlink"

const struct timespec tptr[] = { 
	{ 0x12345678, 987654321 },
	{ 0x15263748, 123456789 },
};

ATF_TC(utimensat_fd);
ATF_TC_HEAD(utimensat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that utimensat works with fd");
}
ATF_TC_BODY(utimensat_fd, tc)
{
	int dfd;
	int fd;
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(utimensat(dfd, BASEFILE, tptr, 0) == 0);
	ATF_REQUIRE(close(dfd) == 0);

	ATF_REQUIRE(stat(FILE, &st) == 0);
	ATF_REQUIRE(st.st_atimespec.tv_sec == tptr[0].tv_sec);
	ATF_REQUIRE(st.st_atimespec.tv_nsec == tptr[0].tv_nsec);
	ATF_REQUIRE(st.st_mtimespec.tv_sec == tptr[1].tv_sec);
	ATF_REQUIRE(st.st_mtimespec.tv_nsec == tptr[1].tv_nsec);
}

ATF_TC(utimensat_fdcwd);
ATF_TC_HEAD(utimensat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that utimensat works with fd as AT_FDCWD");
}
ATF_TC_BODY(utimensat_fdcwd, tc)
{
	int fd;
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(chdir(DIR) == 0);
	ATF_REQUIRE(utimensat(AT_FDCWD, BASEFILE, tptr, 0) == 0);

	ATF_REQUIRE(stat(BASEFILE, &st) == 0);
	ATF_REQUIRE(st.st_atimespec.tv_sec == tptr[0].tv_sec);
	ATF_REQUIRE(st.st_atimespec.tv_nsec == tptr[0].tv_nsec);
	ATF_REQUIRE(st.st_mtimespec.tv_sec == tptr[1].tv_sec);
	ATF_REQUIRE(st.st_mtimespec.tv_nsec == tptr[1].tv_nsec);
}

ATF_TC(utimensat_fdcwderr);
ATF_TC_HEAD(utimensat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that utimensat fails with fd as AT_FDCWD and bad path");
}
ATF_TC_BODY(utimensat_fdcwderr, tc)
{
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(utimensat(AT_FDCWD, FILEERR, tptr, 0) == -1);
}

ATF_TC(utimensat_fderr1);
ATF_TC_HEAD(utimensat_fderr1, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that utimensat fail with bad path");
}
ATF_TC_BODY(utimensat_fderr1, tc)
{
	int dfd;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(utimensat(dfd, FILEERR, tptr, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC(utimensat_fderr2);
ATF_TC_HEAD(utimensat_fderr2, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that utimensat fails with bad fdat");
}
ATF_TC_BODY(utimensat_fderr2, tc)
{
	int dfd;
	int fd;
	char cwd[MAXPATHLEN];

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE((dfd = open(getcwd(cwd, MAXPATHLEN), O_RDONLY, 0)) != -1);
	ATF_REQUIRE(utimensat(dfd, BASEFILE, tptr, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC(utimensat_fderr3);
ATF_TC_HEAD(utimensat_fderr3, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that utimensat fails with fd as -1");
}
ATF_TC_BODY(utimensat_fderr3, tc)
{
	int fd;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(utimensat(-1, FILE, tptr, 0) == -1);
}

ATF_TC(utimensat_fdlink);
ATF_TC_HEAD(utimensat_fdlink, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that utimensat works on symlink");
}
ATF_TC_BODY(utimensat_fdlink, tc)
{
	int dfd;
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(symlink(FILE, LINK) == 0); /* NB: FILE does not exists */

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);

	ATF_REQUIRE(utimensat(dfd, BASELINK, tptr, 0) == -1);
	ATF_REQUIRE(errno = ENOENT);

	ATF_REQUIRE(utimensat(dfd, BASELINK, tptr, AT_SYMLINK_NOFOLLOW) == 0);

	ATF_REQUIRE(close(dfd) == 0);

	ATF_REQUIRE(lstat(LINK, &st) == 0);
	ATF_REQUIRE(st.st_atimespec.tv_sec == tptr[0].tv_sec);
	ATF_REQUIRE(st.st_atimespec.tv_nsec == tptr[0].tv_nsec);
	ATF_REQUIRE(st.st_mtimespec.tv_sec == tptr[1].tv_sec);
	ATF_REQUIRE(st.st_mtimespec.tv_nsec == tptr[1].tv_nsec);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, utimensat_fd);
	ATF_TP_ADD_TC(tp, utimensat_fdcwd);
	ATF_TP_ADD_TC(tp, utimensat_fdcwderr);
	ATF_TP_ADD_TC(tp, utimensat_fderr1);
	ATF_TP_ADD_TC(tp, utimensat_fderr2);
	ATF_TP_ADD_TC(tp, utimensat_fderr3);
	ATF_TP_ADD_TC(tp, utimensat_fdlink);

	return atf_no_error();
}
