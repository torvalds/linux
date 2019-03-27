/*	$NetBSD: t_mkdirat.c,v 1.2 2013/03/17 04:46:06 jmmv Exp $ */

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
__RCSID("$NetBSD: t_mkdirat.c,v 1.2 2013/03/17 04:46:06 jmmv Exp $");

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>

#define DIR "dir"
#define SDIR "dir/openat"
#define BASESDIR "openat"
#define SDIRERR "dir/openaterr" 

ATF_TC(mkdirat_fd);
ATF_TC_HEAD(mkdirat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that mkdirat works with fd");
}
ATF_TC_BODY(mkdirat_fd, tc)
{
	int dfd;
	mode_t mode = 0755;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(mkdirat(dfd, BASESDIR, mode) != -1);
	ATF_REQUIRE(close(dfd) == 0);
	ATF_REQUIRE(access(SDIR, F_OK) == 0);
}

ATF_TC(mkdirat_fdcwd);
ATF_TC_HEAD(mkdirat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that mkdirat works with fd as AT_FDCWD");
}
ATF_TC_BODY(mkdirat_fdcwd, tc)
{
	mode_t mode = 0755;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(mkdirat(AT_FDCWD, SDIR, mode) != -1);
	ATF_REQUIRE(access(SDIR, F_OK) == 0);
}

ATF_TC(mkdirat_fdcwderr);
ATF_TC_HEAD(mkdirat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that mkdirat fails with fd as AT_FDCWD and bad path");
}
ATF_TC_BODY(mkdirat_fdcwderr, tc)
{
	mode_t mode = 0755;

	ATF_REQUIRE(mkdirat(AT_FDCWD, SDIRERR, mode) == -1);
}

ATF_TC(mkdirat_fderr);
ATF_TC_HEAD(mkdirat_fderr, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that mkdirat fails with fd as -1");
}
ATF_TC_BODY(mkdirat_fderr, tc)
{
	int fd;
	mode_t mode = 0755;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(SDIR, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(mkdirat(-1, SDIR, mode) == -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mkdirat_fd);
	ATF_TP_ADD_TC(tp, mkdirat_fdcwd);
	ATF_TP_ADD_TC(tp, mkdirat_fdcwderr);
	ATF_TP_ADD_TC(tp, mkdirat_fderr);

	return atf_no_error();
}
