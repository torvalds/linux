/*	$NetBSD: t_fchownat.c,v 1.4 2017/01/10 15:13:56 christos Exp $ */

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
__RCSID("$NetBSD: t_fchownat.c,v 1.4 2017/01/10 15:13:56 christos Exp $");

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
#include <pwd.h>

#define DIR "dir"
#define FILE "dir/fchownat"
#define BASEFILE "fchownat"
#define LINK "dir/symlink"
#define BASELINK "symlink"
#define FILEERR "dir/fchownaterr"
#define USER "nobody"

static int getuser(uid_t *, gid_t *);

static int getuser(uid_t *uid, gid_t *gid)
{
	struct passwd *pw;

	if ((pw = getpwnam(USER)) == NULL)
		return -1;

	*uid = pw->pw_uid;
	*gid = pw->pw_gid;

	return 0;
}

ATF_TC(fchownat_fd);
ATF_TC_HEAD(fchownat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fchownat works with fd");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(fchownat_fd, tc)
{
	int dfd;
	int fd;
	uid_t uid;
	gid_t gid;
	struct stat st;

	ATF_REQUIRE(getuser(&uid, &gid) == 0);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(fchownat(dfd, BASEFILE, uid, gid, 0) == 0);
	ATF_REQUIRE(close(dfd) == 0);

	ATF_REQUIRE(stat(FILE, &st) == 0);
	ATF_REQUIRE(st.st_uid == uid);
	ATF_REQUIRE(st.st_gid == gid);
}

ATF_TC(fchownat_fdcwd);
ATF_TC_HEAD(fchownat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that fchownat works with fd as AT_FDCWD");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(fchownat_fdcwd, tc)
{
	int fd;
	uid_t uid;
	gid_t gid;
	struct stat st;

	ATF_REQUIRE(getuser(&uid, &gid) == 0);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(chdir(DIR) == 0);
	ATF_REQUIRE(fchownat(AT_FDCWD, BASEFILE, uid, gid, 0) == 0);

	ATF_REQUIRE(stat(BASEFILE, &st) == 0);
	ATF_REQUIRE(st.st_uid == uid);
	ATF_REQUIRE(st.st_gid == gid);
}

ATF_TC(fchownat_fdcwderr);
ATF_TC_HEAD(fchownat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that fchownat fails with fd as AT_FDCWD and bad path");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(fchownat_fdcwderr, tc)
{
	uid_t uid;
	gid_t gid;

	ATF_REQUIRE(getuser(&uid, &gid) == 0);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(fchownat(AT_FDCWD, FILEERR, uid, gid, 0) == -1);
}

ATF_TC(fchownat_fderr1);
ATF_TC_HEAD(fchownat_fderr1, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fchownat fail with bad path");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(fchownat_fderr1, tc)
{
	int dfd;
	uid_t uid;
	gid_t gid;

	ATF_REQUIRE(getuser(&uid, &gid) == 0);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(fchownat(dfd, FILEERR, uid, gid, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC(fchownat_fderr2);
ATF_TC_HEAD(fchownat_fderr2, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fchownat fails with bad fdat");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(fchownat_fderr2, tc)
{
	int dfd;
	int fd;
	char cwd[MAXPATHLEN];
	uid_t uid;
	gid_t gid;

	ATF_REQUIRE(getuser(&uid, &gid) == 0);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE((dfd = open(getcwd(cwd, MAXPATHLEN), O_RDONLY, 0)) != -1);
	ATF_REQUIRE(fchownat(dfd, BASEFILE, uid, gid, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC(fchownat_fderr3);
ATF_TC_HEAD(fchownat_fderr3, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fchownat fails with fd as -1");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(fchownat_fderr3, tc)
{
	int fd;
	uid_t uid;
	gid_t gid;

	ATF_REQUIRE(getuser(&uid, &gid) == 0);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(fchownat(-1, FILE, uid, gid, 0) == -1);
}

ATF_TC(fchownat_fdlink);
ATF_TC_HEAD(fchownat_fdlink, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fchownat works on symlink");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(fchownat_fdlink, tc)
{
	int dfd;
	uid_t uid;
	gid_t gid;
	struct stat st;

	ATF_REQUIRE(getuser(&uid, &gid) == 0);
	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(symlink(FILE, LINK) == 0); /* Target does not exists */

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);

	ATF_REQUIRE(fchownat(dfd, BASELINK, uid, gid, 0) == -1);
	ATF_REQUIRE(errno == ENOENT);

	ATF_REQUIRE(fchownat(dfd, BASELINK, uid, gid,
	    AT_SYMLINK_NOFOLLOW) == 0);

	ATF_REQUIRE(close(dfd) == 0);

	ATF_REQUIRE(lstat(LINK, &st) == 0);
	ATF_REQUIRE(st.st_uid == uid);
	ATF_REQUIRE(st.st_gid == gid);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fchownat_fd);
	ATF_TP_ADD_TC(tp, fchownat_fdcwd);
	ATF_TP_ADD_TC(tp, fchownat_fdcwderr);
	ATF_TP_ADD_TC(tp, fchownat_fderr1);
	ATF_TP_ADD_TC(tp, fchownat_fderr2);
	ATF_TP_ADD_TC(tp, fchownat_fderr3);
	ATF_TP_ADD_TC(tp, fchownat_fdlink);

	return atf_no_error();
}
