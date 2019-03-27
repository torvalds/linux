/* $NetBSD: t_mkdir.c,v 1.2 2011/10/15 07:38:31 jruoho Exp $ */

/*-
 * Copyright (c) 2008, 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe and Jukka Ruohonen.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_mkdir.c,v 1.2 2011/10/15 07:38:31 jruoho Exp $");

#include <sys/stat.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ATF_TC(mkdir_err);
ATF_TC_HEAD(mkdir_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks errors from mkdir(2)");
}

ATF_TC_BODY(mkdir_err, tc)
{
	char buf[PATH_MAX + 1];
	int fd;

	(void)memset(buf, 'x', sizeof(buf));

	fd = open("/etc", O_RDONLY);

	if (fd >= 0) {

		(void)close(fd);

		errno = 0;
		ATF_REQUIRE_ERRNO(EEXIST, mkdir("/etc", 0500) == -1);
	}

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, mkdir((void *)-1, 0500) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, mkdir(buf, 0500) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, mkdir("/a/b/c/d/e/f/g/h/i/j/k", 0500) == -1);
}

ATF_TC_WITH_CLEANUP(mkdir_perm);
ATF_TC_HEAD(mkdir_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks permissions with mkdir(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(mkdir_perm, tc)
{
	errno = 0;
	ATF_REQUIRE_ERRNO(EACCES, mkdir("/usr/__nonexistent__", 0500) == -1);
}

ATF_TC_CLEANUP(mkdir_perm, tc)
{
	(void)rmdir("/usr/__nonexistent__");
}

ATF_TC_WITH_CLEANUP(mkdir_mode);
ATF_TC_HEAD(mkdir_mode, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that UIDs and GIDs are right "
	    "for a directory created with mkdir(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(mkdir_mode, tc)
{
	static const char *path = "/tmp/mkdir";
	struct stat st_a, st_b;
	struct passwd *pw;
	pid_t pid;
	int sta;

	(void)memset(&st_a, 0, sizeof(struct stat));
	(void)memset(&st_b, 0, sizeof(struct stat));

	pw = getpwnam("nobody");

	ATF_REQUIRE(pw != NULL);
	ATF_REQUIRE(stat("/tmp", &st_a) == 0);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (setuid(pw->pw_uid) != 0)
			_exit(EXIT_FAILURE);

		if (mkdir(path, 0500) != 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)sleep(1);
	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("failed to create '%s'", path);

	ATF_REQUIRE(stat(path, &st_b) == 0);
	ATF_REQUIRE(rmdir(path) == 0);

	/*
	 * The directory's owner ID should be set to the
	 * effective UID, whereas the group ID should be
	 * set to that of the parent directory.
	 */
	if (st_b.st_uid != pw->pw_uid)
		atf_tc_fail("invalid UID for '%s'", path);

	if (st_b.st_gid != st_a.st_gid)
		atf_tc_fail("GID did not follow the parent directory");
}

ATF_TC_CLEANUP(mkdir_mode, tc)
{
	(void)rmdir("/tmp/mkdir");
}

ATF_TC(mkdir_trail);
ATF_TC_HEAD(mkdir_trail, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks mkdir(2) for trailing slashes");
}

ATF_TC_BODY(mkdir_trail, tc)
{
	const char *tests[] = {
		/*
		 * IEEE 1003.1 second ed. 2.2.2.78:
		 *
		 * If the pathname refers to a directory, it may also have
		 * one or more trailing slashes.  Multiple successive slashes
		 * are considered to be the same as one slash.
		 */
		"dir1/",
		"dir2//",

		NULL,
	};

	const char **test;

	for (test = &tests[0]; *test != NULL; ++test) {

		(void)printf("Checking \"%s\"\n", *test);
		(void)rmdir(*test);

		ATF_REQUIRE(mkdir(*test, 0777) == 0);
		ATF_REQUIRE(rename(*test, "foo") == 0);
		ATF_REQUIRE(rename("foo/", *test) == 0);
		ATF_REQUIRE(rmdir(*test) == 0);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mkdir_err);
	ATF_TP_ADD_TC(tp, mkdir_perm);
	ATF_TP_ADD_TC(tp, mkdir_mode);
	ATF_TP_ADD_TC(tp, mkdir_trail);

	return atf_no_error();
}
