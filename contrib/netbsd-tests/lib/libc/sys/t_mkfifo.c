/* $NetBSD: t_mkfifo.c,v 1.2 2011/11/02 06:04:48 jruoho Exp $ */

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
__RCSID("$NetBSD: t_mkfifo.c,v 1.2 2011/11/02 06:04:48 jruoho Exp $");

#include <sys/stat.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static const char	path[] = "fifo";
static void		support(void);

static void
support(void)
{

	errno = 0;

	if (mkfifo(path, 0600) == 0) {
		ATF_REQUIRE(unlink(path) == 0);
		return;
	}

	if (errno == EOPNOTSUPP)
		atf_tc_skip("the kernel does not support FIFOs");
	else {
		atf_tc_fail("mkfifo(2) failed");
	}
}

ATF_TC_WITH_CLEANUP(mkfifo_block);
ATF_TC_HEAD(mkfifo_block, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that FIFOs block");
}

ATF_TC_BODY(mkfifo_block, tc)
{
	int sta, fd = -1;
	pid_t pid;

	support();

	ATF_REQUIRE(mkfifo(path, 0600) == 0);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		/*
		 * If we open the FIFO as read-only (write-only),
		 * the call should block until another process
		 * opens the FIFO for writing (reading).
		 */
		fd = open(path, O_RDONLY);

		_exit(EXIT_FAILURE); /* NOTREACHED */
	}

	(void)sleep(1);

	ATF_REQUIRE(kill(pid, SIGKILL) == 0);

	(void)wait(&sta);

	if (WIFSIGNALED(sta) == 0 || WTERMSIG(sta) != SIGKILL)
		atf_tc_fail("FIFO did not block");

	(void)close(fd);
	(void)unlink(path);
}

ATF_TC_CLEANUP(mkfifo_block, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mkfifo_err);
ATF_TC_HEAD(mkfifo_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test erros from mkfifo(2)");
}

ATF_TC_BODY(mkfifo_err, tc)
{
	char buf[PATH_MAX + 1];

	support();

	(void)memset(buf, 'x', sizeof(buf));
	ATF_REQUIRE(mkfifo(path, 0600) == 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, mkfifo((char *)-1, 0600) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EEXIST, mkfifo("/etc/passwd", 0600) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EEXIST, mkfifo(path, 0600) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, mkfifo(buf, 0600) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, mkfifo("/a/b/c/d/e/f/g", 0600) == -1);

	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(mkfifo_err, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mkfifo_nonblock);
ATF_TC_HEAD(mkfifo_nonblock, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test O_NONBLOCK with FIFOs");
}

ATF_TC_BODY(mkfifo_nonblock, tc)
{
	int fd, sta;
	pid_t pid;

	support();

	fd = -1;
	ATF_REQUIRE(mkfifo(path, 0600) == 0);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		/*
		 * If we open the FIFO as O_NONBLOCK, the O_RDONLY
		 * call should return immediately, whereas the call
		 * for write-only should fail with ENXIO.
		 */
		fd = open(path, O_RDONLY | O_NONBLOCK);

		if (fd >= 0)
			_exit(EXIT_SUCCESS);

		(void)pause();	/* NOTREACHED */
	}

	(void)sleep(1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENXIO, open(path, O_WRONLY | O_NONBLOCK) == -1);

	(void)kill(pid, SIGKILL);
	(void)wait(&sta);

	if (WIFSIGNALED(sta) != 0 || WTERMSIG(sta) == SIGKILL)
		atf_tc_fail("FIFO blocked for O_NONBLOCK open(2)");

	(void)close(fd);
	(void)unlink(path);
}

ATF_TC_CLEANUP(mkfifo_nonblock, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mkfifo_perm);
ATF_TC_HEAD(mkfifo_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with mkfifo(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(mkfifo_perm, tc)
{

	support();

	errno = 0;
	ATF_REQUIRE_ERRNO(EACCES, mkfifo("/root/fifo", 0600) == -1);

	ATF_REQUIRE(mkfifo(path, 0600) == 0);

	/*
	 * For some reason this fails with EFTYPE...
	 */
	errno = 0;
	ATF_REQUIRE_ERRNO(EFTYPE, chmod(path, 1777) == -1);

	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(mkfifo_perm, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mkfifo_stat);
ATF_TC_HEAD(mkfifo_stat, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mkfifo(2) with stat");
}

ATF_TC_BODY(mkfifo_stat, tc)
{
	struct stat st;

	support();

	(void)memset(&st, 0, sizeof(struct stat));

	ATF_REQUIRE(mkfifo(path, 0600) == 0);
	ATF_REQUIRE(stat(path, &st) == 0);

	if (S_ISFIFO(st.st_mode) == 0)
		atf_tc_fail("invalid mode from mkfifo(2)");

	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(mkfifo_stat, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mkfifo_block);
	ATF_TP_ADD_TC(tp, mkfifo_err);
	ATF_TP_ADD_TC(tp, mkfifo_nonblock);
	ATF_TP_ADD_TC(tp, mkfifo_perm);
	ATF_TP_ADD_TC(tp, mkfifo_stat);

	return atf_no_error();
}
