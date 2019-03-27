/*	$NetBSD: t_readlinkat.c,v 1.4 2017/01/10 15:13:56 christos Exp $ */

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
__RCSID("$NetBSD: t_readlinkat.c,v 1.4 2017/01/10 15:13:56 christos Exp $");

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
#define FILE "dir/readlinkat"
#define BASEFILE "readlinkat"
#define LINK "dir/symlink"
#define BASELINK "symlink"
#define FILEERR "dir/readlinkaterr"

ATF_TC(readlinkat_fd);
ATF_TC_HEAD(readlinkat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that readlinkat works with fd");
}
ATF_TC_BODY(readlinkat_fd, tc)
{
	int dfd;
	int fd;
	ssize_t len;
	char buf[MAXPATHLEN];

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(symlink(FILE, LINK) == 0);

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	len = readlinkat(dfd, BASELINK, buf, sizeof(buf)-1);
	ATF_REQUIRE(len != -1);
	buf[len] = 0;
	ATF_REQUIRE(close(dfd) == 0);

	ATF_REQUIRE(strcmp(buf, FILE) == 0);
}

ATF_TC(readlinkat_fdcwd);
ATF_TC_HEAD(readlinkat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that readlinkat works with fd as AT_FDCWD");
}
ATF_TC_BODY(readlinkat_fdcwd, tc)
{
	int fd;
	ssize_t len;
	char buf[MAXPATHLEN];

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(symlink(FILE, LINK) == 0);

	len = readlinkat(AT_FDCWD, LINK, buf, sizeof(buf)-1);
	ATF_REQUIRE(len != -1);
	buf[len] = 0;

	ATF_REQUIRE(strcmp(buf, FILE) == 0);
}

ATF_TC(readlinkat_fdcwderr);
ATF_TC_HEAD(readlinkat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that readlinkat fails with fd as AT_FDCWD and bad path");
}
ATF_TC_BODY(readlinkat_fdcwderr, tc)
{
	char buf[MAXPATHLEN];

	ATF_REQUIRE(readlinkat(AT_FDCWD, LINK, buf, sizeof(buf)) == -1);
}

ATF_TC(readlinkat_fderr1);
ATF_TC_HEAD(readlinkat_fderr1, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that readlinkat fail with bad path");
}
ATF_TC_BODY(readlinkat_fderr1, tc)
{
	int dfd;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(readlinkat(dfd, FILEERR, F_OK, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC(readlinkat_fderr2);
ATF_TC_HEAD(readlinkat_fderr2, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that readlinkat fails with fd as -1");
}
ATF_TC_BODY(readlinkat_fderr2, tc)
{
	int fd;
	char buf[MAXPATHLEN];

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(symlink(FILE, LINK) == 0);

	ATF_REQUIRE(readlinkat(-1, LINK, buf, sizeof(buf)) == -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, readlinkat_fd);
	ATF_TP_ADD_TC(tp, readlinkat_fdcwd);
	ATF_TP_ADD_TC(tp, readlinkat_fdcwderr);
	ATF_TP_ADD_TC(tp, readlinkat_fderr1);
	ATF_TP_ADD_TC(tp, readlinkat_fderr2);

	return atf_no_error();
}
