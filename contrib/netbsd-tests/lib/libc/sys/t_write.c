/* $NetBSD: t_write.c,v 1.3 2017/01/13 19:27:23 christos Exp $ */

/*-
 * Copyright (c) 2001, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
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
__RCSID("$NetBSD: t_write.c,v 1.3 2017/01/13 19:27:23 christos Exp $");

#include <sys/uio.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void		 sighandler(int);

static bool		 fail = false;
static const char	*path = "write";

static void
sighandler(int signo __unused)
{
	fail = false;
}

ATF_TC_WITH_CLEANUP(write_err);
ATF_TC_HEAD(write_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks errors from write(2)");
}

ATF_TC_BODY(write_err, tc)
{
	char rbuf[3] = { 'a', 'b', 'c' };
	char wbuf[3] = { 'x', 'y', 'z' };
	int fd;

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, write(-1, wbuf, sizeof(wbuf)) == -1);

	fd = open(path, O_RDWR | O_CREAT);

	if (fd >= 0) {

		errno = 0;
		ATF_REQUIRE_ERRNO(0, write(fd, wbuf, 3) == 3);

		errno = 0;
		ATF_REQUIRE_ERRNO(EINVAL, write(fd, wbuf, SIZE_MAX) == -1);

		errno = 0;
		ATF_REQUIRE_ERRNO(EFAULT, write(fd, (void *)-1, 1) == -1);

		/*
		 * Check that the above bogus write(2)
		 * calls did not corrupt the file.
		 */
		ATF_REQUIRE(lseek(fd, 0, SEEK_SET) == 0);
		ATF_REQUIRE(read(fd, rbuf, 3) == 3);
		ATF_REQUIRE(memcmp(rbuf, wbuf, 3) == 0);

		(void)close(fd);
		(void)unlink(path);
	}
}

ATF_TC_CLEANUP(write_err, tc)
{
	(void)unlink(path);
}

ATF_TC(write_pipe);
ATF_TC_HEAD(write_pipe, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks for EPIPE from write(2)");
}

ATF_TC_BODY(write_pipe, tc)
{
	int fds[2];

	ATF_REQUIRE(pipe(fds) == 0);
	ATF_REQUIRE(signal(SIGPIPE, sighandler) == 0);

	ATF_REQUIRE(write(fds[1], "x", 1) != -1);
	ATF_REQUIRE(close(fds[0]) == 0);

	errno = 0;
	fail = true;

	if (write(fds[1], "x", 1) != -1 || errno != EPIPE)
		atf_tc_fail_nonfatal("expected EPIPE but write(2) succeeded");

	ATF_REQUIRE(close(fds[1]) == 0);

	if (fail != false)
		atf_tc_fail_nonfatal("SIGPIPE was not raised");
}

ATF_TC_WITH_CLEANUP(write_pos);
ATF_TC_HEAD(write_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that write(2) "
	    "updates the file position");
}

ATF_TC_BODY(write_pos, tc)
{
	const size_t n = 123;
	size_t i;
	int fd;

	fd = open(path, O_RDWR | O_CREAT);
	ATF_REQUIRE(fd >= 0);

	for (i = 0; i < n; i++) {
		ATF_REQUIRE(write(fd, "x", 1) == 1);
		ATF_REQUIRE(lseek(fd, 0, SEEK_CUR) == (off_t)(i + 1));
	}

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(write_pos, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(write_ret);
ATF_TC_HEAD(write_ret, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks return values from write(2)");
}

ATF_TC_BODY(write_ret, tc)
{
	const size_t n = 99;
	char buf[123];
	size_t i, j;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT);
	ATF_REQUIRE(fd >= 0);

	(void)memset(buf, 'x', sizeof(buf));

	for (i = j = 0; i < n; i++)
		j += write(fd, buf, sizeof(buf));

	if (j != n * 123)
		atf_tc_fail("inconsistent return values from write(2)");

	(void)close(fd);
	(void)unlink(path);
}

ATF_TC_CLEANUP(write_ret, tc)
{
	(void)unlink(path);
}

ATF_TC(writev_iovmax);
ATF_TC_HEAD(writev_iovmax, tc)
{
	atf_tc_set_md_var(tc, "timeout", "10");
	atf_tc_set_md_var(tc, "descr",
	    "Checks that file descriptor is properly FILE_UNUSE()d "
	    "when iovcnt is greater than IOV_MAX");
}

ATF_TC_BODY(writev_iovmax, tc)
{
	ssize_t retval;

	(void)printf("Calling writev(2, NULL, IOV_MAX + 1)...\n");

	errno = 0;
	retval = writev(2, NULL, IOV_MAX + 1);

	ATF_REQUIRE_EQ_MSG(retval, -1, "got: %zd", retval);
	ATF_REQUIRE_EQ_MSG(errno, EINVAL, "got: %s", strerror(errno));
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, write_err);
	ATF_TP_ADD_TC(tp, write_pipe);
	ATF_TP_ADD_TC(tp, write_pos);
	ATF_TP_ADD_TC(tp, write_ret);
	ATF_TP_ADD_TC(tp, writev_iovmax);

	return atf_no_error();
}
