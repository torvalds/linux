/* $NetBSD: t_link.c,v 1.3 2017/01/13 20:42:36 christos Exp $ */

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
__RCSID("$NetBSD: t_link.c,v 1.3 2017/01/13 20:42:36 christos Exp $");

#include <sys/param.h>
#include <sys/stat.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char	*getpath(void);
static char		 path[] = "link";
static const char	*pathl;

static const char *
getpath(void)
{
	static char buf[LINE_MAX];

	(void)memset(buf, '\0', sizeof(buf));

	if (getcwd(buf, sizeof(buf)) == NULL)
		return NULL;

	(void)strlcat(buf, path, sizeof(buf));
	(void)strlcat(buf, ".link", sizeof(buf));

	return buf;
}

ATF_TC_WITH_CLEANUP(link_count);
ATF_TC_HEAD(link_count, tc)
{
	atf_tc_set_md_var(tc, "descr", "link(2) counts are incremented?");
}

ATF_TC_BODY(link_count, tc)
{
	struct stat sa, sb;
	int fd;

	(void)memset(&sa, 0, sizeof(struct stat));
	(void)memset(&sb, 0, sizeof(struct stat));

	pathl = getpath();
	fd = open(path, O_RDWR | O_CREAT, 0600);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(pathl != NULL);

	ATF_REQUIRE(stat(path, &sa) == 0);
	ATF_REQUIRE(link(path, pathl) == 0);
	ATF_REQUIRE(stat(path, &sb) == 0);

	if (sa.st_nlink != sb.st_nlink - 1)
		atf_tc_fail("incorrect link(2) count");

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(path) == 0);
	ATF_REQUIRE(unlink(pathl) == 0);
}

ATF_TC_CLEANUP(link_count, tc)
{
	(void)unlink(path);
	(void)unlink(pathl);
}

ATF_TC_WITH_CLEANUP(link_err);
ATF_TC_HEAD(link_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of link(2)");
}

ATF_TC_BODY(link_err, tc)
{
	char buf[MAXPATHLEN + 1];
	int fd;

	(void)memset(buf, 'x', sizeof(buf));

	pathl = getpath();
	fd = open(path, O_RDWR | O_CREAT, 0600);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(pathl != NULL);

	errno = 0;
	ATF_REQUIRE(link(path, pathl) == 0);
	ATF_REQUIRE_ERRNO(EEXIST, link(path, pathl) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, link(buf, "xxx") == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, link(path, "/d/c/b/a") == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, link("/a/b/c/d", path) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, link("/a/b/c/d", "/d/c/b/a") == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, link(path, (const char *)-1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, link((const char *)-1, "xxx") == -1);

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(path) == 0);
	ATF_REQUIRE(unlink(pathl) == 0);
}

ATF_TC_CLEANUP(link_err, tc)
{
	(void)unlink(path);
	(void)unlink(pathl);
}

ATF_TC(link_perm);
ATF_TC_HEAD(link_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with link(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(link_perm, tc)
{
	int rv;

	errno = 0;
	rv = link("/root", "/root.link");
	ATF_REQUIRE_MSG(rv == -1 && (errno == EACCES || errno == EPERM),
	    "link to a directory did not fail with EPERM or EACCESS; link() "
	    "returned %d, errno %d", rv, errno);

	errno = 0;
	ATF_REQUIRE_ERRNO(EACCES,
	    link("/root/.profile", "/root/.profile.link") == -1);
}

ATF_TC_WITH_CLEANUP(link_stat);
ATF_TC_HEAD(link_stat, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check stat(2) of a linked file");
}

ATF_TC_BODY(link_stat, tc)
{
	struct stat sa, sb;
	int fd;

	(void)memset(&sa, 0, sizeof(struct stat));
	(void)memset(&sb, 0, sizeof(struct stat));

	pathl = getpath();
	fd = open(path, O_RDWR | O_CREAT, 0600);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(pathl != NULL);

	ATF_REQUIRE(link(path, pathl) == 0);
	ATF_REQUIRE(stat(path, &sa) == 0);
	ATF_REQUIRE(lstat(pathl, &sb) == 0);

	if (sa.st_uid != sb.st_uid)
		atf_tc_fail("unequal UIDs");

	if (sa.st_mode != sb.st_mode)
		atf_tc_fail("unequal modes");

	if (sa.st_ino != sb.st_ino)
		atf_tc_fail("unequal inodes");

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(path) == 0);
	ATF_REQUIRE(unlink(pathl) == 0);
}

ATF_TC_CLEANUP(link_stat, tc)
{
	(void)unlink(path);
	(void)unlink(pathl);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, link_count);
	ATF_TP_ADD_TC(tp, link_err);
	ATF_TP_ADD_TC(tp, link_perm);
	ATF_TP_ADD_TC(tp, link_stat);

	return atf_no_error();
}
