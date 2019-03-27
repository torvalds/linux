/*	$NetBSD: t_mknodat.c,v 1.4 2017/01/10 15:15:09 christos Exp $ */

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
__RCSID("$NetBSD: t_mknodat.c,v 1.4 2017/01/10 15:15:09 christos Exp $");

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
#define FILE "dir/openat"
#define BASEFILE "openat"
#define FILEERR "dir/openaterr" 

static dev_t get_devnull(void);

static dev_t
get_devnull(void)
{
	struct stat st;
	
	if (stat(_PATH_DEVNULL, &st) != 0)
		return NODEV;

	return st.st_rdev;
}


ATF_TC(mknodat_fd);
ATF_TC_HEAD(mknodat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that mknodat works with fd");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(mknodat_fd, tc)
{
	int dfd;
	int fd;
	dev_t dev;
	mode_t mode = S_IFCHR|0600;

	ATF_REQUIRE((dev = get_devnull()) != NODEV);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE((fd = mknodat(dfd, BASEFILE, mode, dev)) != -1);
	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(access(FILE, F_OK) == 0);
	(void)close(dfd);
}

ATF_TC(mknodat_fdcwd);
ATF_TC_HEAD(mknodat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that mknodat works with fd as AT_FDCWD");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(mknodat_fdcwd, tc)
{
	int fd;
	dev_t dev;
	mode_t mode = S_IFCHR|0600;

	ATF_REQUIRE((dev = get_devnull()) != NODEV);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = mknodat(AT_FDCWD, FILE, mode, dev)) != -1);
	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(access(FILE, F_OK) == 0);
}

ATF_TC(mknodat_fdcwderr);
ATF_TC_HEAD(mknodat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that mknodat fails with fd as AT_FDCWD and bad path");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(mknodat_fdcwderr, tc)
{
	int fd;
	dev_t dev;
	mode_t mode = S_IFCHR|0600;

	ATF_REQUIRE((dev = get_devnull()) != NODEV);
	ATF_REQUIRE((fd = mknodat(AT_FDCWD, FILEERR, mode, dev)) == -1);
}

ATF_TC(mknodat_fderr);
ATF_TC_HEAD(mknodat_fderr, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that mknodat fails with fd as -1");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(mknodat_fderr, tc)
{
	int fd;
	dev_t dev;
	mode_t mode = S_IFCHR|0600;

	ATF_REQUIRE((dev = get_devnull()) != NODEV);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE((fd = mknodat(-1, FILE, mode, dev)) == -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mknodat_fd);
	ATF_TP_ADD_TC(tp, mknodat_fdcwd);
	ATF_TP_ADD_TC(tp, mknodat_fdcwderr);
	ATF_TP_ADD_TC(tp, mknodat_fderr);

	return atf_no_error();
}
