/* $NetBSD: t_dup.c,v 1.9 2017/01/13 20:31:53 christos Exp $ */

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
__RCSID("$NetBSD: t_dup.c,v 1.9 2017/01/13 20:31:53 christos Exp $");

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

static char	path[] = "dup";
#ifdef __NetBSD__
static void	check_mode(bool, bool, bool);
#endif

static void
check_mode(bool _dup, bool _dup2, bool _dup3)
{
	int mode[3] = { O_RDONLY, O_WRONLY, O_RDWR   };
	int perm[5] = { 0700, 0400, 0600, 0444, 0666 };
	struct stat st, st1;
	int fd, fd1, fd2;
	size_t i, j;

	/*
	 * Check that a duplicated descriptor
	 * retains the mode of the original file.
	 */
	for (i = 0; i < __arraycount(mode); i++) {

		for (j = 0; j < __arraycount(perm); j++) {

			fd1 = open(path, mode[i] | O_CREAT, perm[j]);
			fd2 = open("/etc/passwd", O_RDONLY);

			ATF_REQUIRE(fd1 >= 0);
			ATF_REQUIRE(fd2 >= 0);

			if (_dup != false)
				fd = dup(fd1);
			else if (_dup2 != false)
				fd = dup2(fd1, fd2);
			else if (_dup3 != false)
				fd = dup3(fd1, fd2, O_CLOEXEC);
			else {
				fd = -1;
			}

			ATF_REQUIRE(fd >= 0);

			(void)memset(&st, 0, sizeof(struct stat));
			(void)memset(&st1, 0, sizeof(struct stat));

			ATF_REQUIRE(fstat(fd, &st) == 0);
			ATF_REQUIRE(fstat(fd1, &st1) == 0);

			if (st.st_mode != st1.st_mode)
				atf_tc_fail("invalid mode");

			(void)close(fd);
			(void)close(fd1);
			(void)close(fd2);
			(void)unlink(path);
		}
	}
}

ATF_TC(dup2_basic);
ATF_TC_HEAD(dup2_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of dup2(2)");
}

ATF_TC_BODY(dup2_basic, tc)
{
	int fd, fd1, fd2;

	fd1 = open("/etc/passwd", O_RDONLY);
	fd2 = open("/etc/passwd", O_RDONLY);

	ATF_REQUIRE(fd1 >= 0);
	ATF_REQUIRE(fd2 >= 0);

	fd = dup2(fd1, fd2);
	ATF_REQUIRE(fd >= 0);

	if (fd != fd2)
		atf_tc_fail("invalid descriptor");

	(void)close(fd);
	(void)close(fd1);

	ATF_REQUIRE(close(fd2) != 0);
}

ATF_TC(dup2_err);
ATF_TC_HEAD(dup2_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of dup2(2)");
}

ATF_TC_BODY(dup2_err, tc)
{
	int fd;

	fd = open("/etc/passwd", O_RDONLY);
	ATF_REQUIRE(fd >= 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup2(-1, -1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup2(fd, -1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup2(-1, fd) == -1);

	/*
	 * Note that this should not fail with EINVAL.
	 */
	ATF_REQUIRE(dup2(fd, fd) != -1);

	(void)close(fd);
}

ATF_TC(dup2_max);
ATF_TC_HEAD(dup2_max, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test dup2(2) against limits");
}

ATF_TC_BODY(dup2_max, tc)
{
	struct rlimit res;

	(void)memset(&res, 0, sizeof(struct rlimit));
	(void)getrlimit(RLIMIT_NOFILE, &res);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup2(STDERR_FILENO, res.rlim_cur + 1) == -1);
}

ATF_TC_WITH_CLEANUP(dup2_mode);
ATF_TC_HEAD(dup2_mode, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of dup2(2)");
}

ATF_TC_BODY(dup2_mode, tc)
{
	check_mode(false, true, false);
}

ATF_TC_CLEANUP(dup2_mode, tc)
{
	(void)unlink(path);
}


ATF_TC(dup3_err);
ATF_TC_HEAD(dup3_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test error conditions of dup3(2) (PR lib/45148)");
}

ATF_TC_BODY(dup3_err, tc)
{
	int fd;

	fd = open("/etc/passwd", O_RDONLY);
	ATF_REQUIRE(fd >= 0);

	errno = 0;
#if defined(__FreeBSD__) || defined(__linux__)
	/*
	 * FreeBSD and linux return EINVAL, because...
	 *
	 * [EINVAL] The oldd argument is equal to the newd argument.
	 */
	ATF_REQUIRE(dup3(fd, fd, O_CLOEXEC) == -1);
#else
	ATF_REQUIRE(dup3(fd, fd, O_CLOEXEC) != -1);
#endif

	errno = 0;
#if defined(__FreeBSD__) || defined(__linux__)
	ATF_REQUIRE_ERRNO(EINVAL, dup3(-1, -1, O_CLOEXEC) == -1);
	ATF_REQUIRE_ERRNO(EBADF, dup3(fd, -1, O_CLOEXEC) == -1);
#else
	ATF_REQUIRE_ERRNO(EBADF, dup3(-1, -1, O_CLOEXEC) == -1);
#endif

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup3(fd, -1, O_CLOEXEC) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup3(-1, fd, O_CLOEXEC) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, dup3(fd, 1, O_NOFOLLOW) == -1);

	(void)close(fd);
}

ATF_TC(dup3_max);
ATF_TC_HEAD(dup3_max, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test dup3(2) against limits");
}

ATF_TC_BODY(dup3_max, tc)
{
	struct rlimit res;

	(void)memset(&res, 0, sizeof(struct rlimit));
	(void)getrlimit(RLIMIT_NOFILE, &res);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup3(STDERR_FILENO,
		res.rlim_cur + 1, O_CLOEXEC) == -1);
}

ATF_TC_WITH_CLEANUP(dup3_mode);
ATF_TC_HEAD(dup3_mode, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of dup3(2)");
}

ATF_TC_BODY(dup3_mode, tc)
{
	check_mode(false, false, true);
}

ATF_TC_CLEANUP(dup3_mode, tc)
{
	(void)unlink(path);
}

ATF_TC(dup_err);
ATF_TC_HEAD(dup_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of dup(2)");
}

ATF_TC_BODY(dup_err, tc)
{

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup(-1) == -1);
}

ATF_TC_WITH_CLEANUP(dup_max);
ATF_TC_HEAD(dup_max, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test dup(2) against limits");
}

ATF_TC_BODY(dup_max, tc)
{
	struct rlimit res;
	int *buf, fd, sta;
	size_t i, n;
	pid_t pid;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		/*
		 * Open a temporary file until the
		 * maximum number of open files is
		 * reached. Ater that dup(2) family
		 * should fail with EMFILE.
		 */
		(void)closefrom(0);
		(void)memset(&res, 0, sizeof(struct rlimit));

		n = 10;
		res.rlim_cur = res.rlim_max = n;
		if (setrlimit(RLIMIT_NOFILE, &res) != 0)
			_exit(EX_OSERR);

		buf = calloc(n, sizeof(int));

		if (buf == NULL)
			_exit(EX_OSERR);

		buf[0] = mkstemp(path);

		if (buf[0] < 0)
			_exit(EX_OSERR);

		for (i = 1; i < n; i++) {

			buf[i] = open(path, O_RDONLY);

			if (buf[i] < 0)
				_exit(EX_OSERR);
		}

		errno = 0;
		fd = dup(buf[0]);

		if (fd != -1 || errno != EMFILE)
			_exit(EX_DATAERR);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS) {

		if (WEXITSTATUS(sta) == EX_OSERR)
			atf_tc_fail("system call error");

		if (WEXITSTATUS(sta) == EX_DATAERR)
			atf_tc_fail("dup(2) dupped more than RLIMIT_NOFILE");

		atf_tc_fail("unknown error");
	}

	(void)unlink(path);
}

ATF_TC_CLEANUP(dup_max, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(dup_mode);
ATF_TC_HEAD(dup_mode, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of dup(2)");
}

ATF_TC_BODY(dup_mode, tc)
{
	check_mode(true, false, false);
}

ATF_TC_CLEANUP(dup_mode, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, dup2_basic);
	ATF_TP_ADD_TC(tp, dup2_err);
	ATF_TP_ADD_TC(tp, dup2_max);
	ATF_TP_ADD_TC(tp, dup2_mode);
	ATF_TP_ADD_TC(tp, dup3_err);
	ATF_TP_ADD_TC(tp, dup3_max);
	ATF_TP_ADD_TC(tp, dup3_mode);
	ATF_TP_ADD_TC(tp, dup_err);
	ATF_TP_ADD_TC(tp, dup_max);
	ATF_TP_ADD_TC(tp, dup_mode);

	return atf_no_error();
}
