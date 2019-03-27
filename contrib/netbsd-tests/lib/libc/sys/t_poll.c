/*	$NetBSD: t_poll.c,v 1.3 2012/03/18 07:00:52 jruoho Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matthias Scheler.
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

#include <sys/time.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <poll.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

static int desc;

static void
child1(void)
{
	struct pollfd pfd;

	pfd.fd = desc;
	pfd.events = POLLIN | POLLHUP | POLLOUT;

	(void)poll(&pfd, 1, 2000);
	(void)printf("child1 exit\n");
}

static void
child2(void)
{
	struct pollfd pfd;

	pfd.fd = desc;
	pfd.events = POLLIN | POLLHUP | POLLOUT;

	(void)sleep(1);
	(void)poll(&pfd, 1, INFTIM);
	(void)printf("child2 exit\n");
}

static void
child3(void)
{
	struct pollfd pfd;

	(void)sleep(5);

	pfd.fd = desc;
	pfd.events = POLLIN | POLLHUP | POLLOUT;

	(void)poll(&pfd, 1, INFTIM);
	(void)printf("child3 exit\n");
}

ATF_TC(poll_3way);
ATF_TC_HEAD(poll_3way, tc)
{
	atf_tc_set_md_var(tc, "timeout", "15");
	atf_tc_set_md_var(tc, "descr",
	    "Check for 3-way collision for descriptor. First child comes "
	    "and polls on descriptor, second child comes and polls, first "
	    "child times out and exits, third child comes and polls. When "
	    "the wakeup event happens, the two remaining children should "
	    "both be awaken. (kern/17517)");
}

ATF_TC_BODY(poll_3way, tc)
{
	int pf[2];
	int status, i;
	pid_t pid;

	pipe(pf);
	desc = pf[0];

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {
		(void)close(pf[1]);
		child1();
		_exit(0);
		/* NOTREACHED */
	}

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {
		(void)close(pf[1]);
		child2();
		_exit(0);
		/* NOTREACHED */
	}

	pid = fork();
	ATF_REQUIRE( pid >= 0);

	if (pid == 0) {
		(void)close(pf[1]);
		child3();
		_exit(0);
		/* NOTREACHED */
	}

	(void)sleep(10);

	(void)printf("parent write\n");

	ATF_REQUIRE(write(pf[1], "konec\n", 6) == 6);

	for(i = 0; i < 3; ++i)
		(void)wait(&status);

	(void)printf("parent terminated\n");
}

ATF_TC(poll_basic);
ATF_TC_HEAD(poll_basic, tc)
{
	atf_tc_set_md_var(tc, "timeout", "10");
	atf_tc_set_md_var(tc, "descr",
	    "Basis functionality test for poll(2)");
}

ATF_TC_BODY(poll_basic, tc)
{
	int fds[2];
	struct pollfd pfds[2];
	int ret;

	ATF_REQUIRE_EQ(pipe(fds), 0);

	pfds[0].fd = fds[0];
	pfds[0].events = POLLIN;
	pfds[1].fd = fds[1];
	pfds[1].events = POLLOUT;

	/*
	 * Check that we get a timeout waiting for data on the read end
	 * of our pipe.
	 */
	pfds[0].revents = -1;
	pfds[1].revents = -1;
	ATF_REQUIRE_EQ_MSG(ret = poll(&pfds[0], 1, 1), 0,
	    "got: %d", ret);
	ATF_REQUIRE_EQ_MSG(pfds[0].revents, 0, "got: %d", pfds[0].revents);
	ATF_REQUIRE_EQ_MSG(pfds[1].revents, -1, "got: %d", pfds[1].revents);

	/* Check that the write end of the pipe as reported as ready. */
	pfds[0].revents = -1;
	pfds[1].revents = -1;
	ATF_REQUIRE_EQ_MSG(ret = poll(&pfds[1], 1, 1), 1,
	    "got: %d", ret);
	ATF_REQUIRE_EQ_MSG(pfds[0].revents, -1, "got: %d", pfds[0].revents);
	ATF_REQUIRE_EQ_MSG(pfds[1].revents, POLLOUT, "got: %d",\
	    pfds[1].revents);

	/* Check that only the write end of the pipe as reported as ready. */
	pfds[0].revents = -1;
	pfds[1].revents = -1;
	ATF_REQUIRE_EQ_MSG(ret = poll(pfds, 2, 1), 1,
	    "got: %d", ret);
	ATF_REQUIRE_EQ_MSG(pfds[0].revents, 0, "got: %d", pfds[0].revents);
	ATF_REQUIRE_EQ_MSG(pfds[1].revents, POLLOUT, "got: %d",
	    pfds[1].revents);

	/* Write data to our pipe. */
	ATF_REQUIRE_EQ(write(fds[1], "", 1), 1);

	/* Check that both ends of our pipe are reported as ready. */
	pfds[0].revents = -1;
	pfds[1].revents = -1;
	ATF_REQUIRE_EQ_MSG(ret = poll(pfds, 2, 1), 2,
	    "got: %d", ret);
	ATF_REQUIRE_EQ_MSG(pfds[0].revents, POLLIN, "got: %d",
	    pfds[0].revents);
	ATF_REQUIRE_EQ_MSG(pfds[1].revents, POLLOUT, "got: %d",
	    pfds[1].revents);

	ATF_REQUIRE_EQ(close(fds[0]), 0);
	ATF_REQUIRE_EQ(close(fds[1]), 0);
}

ATF_TC(poll_err);
ATF_TC_HEAD(poll_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check errors from poll(2)");
}

ATF_TC_BODY(poll_err, tc)
{
	struct pollfd pfd;
	int fd = 0;

	pfd.fd = fd;
	pfd.events = POLLIN;

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, poll((struct pollfd *)-1, 1, -1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, poll(&pfd, 1, -2) == -1);
}

#ifndef __FreeBSD__
ATF_TC(pollts_basic);
ATF_TC_HEAD(pollts_basic, tc)
{
	atf_tc_set_md_var(tc, "timeout", "10");
	atf_tc_set_md_var(tc, "descr",
	    "Basis functionality test for pollts(2)");
}

ATF_TC_BODY(pollts_basic, tc)
{
	int fds[2];
	struct pollfd pfds[2];
	struct timespec timeout;
	int ret;

	ATF_REQUIRE_EQ(pipe(fds), 0);

	pfds[0].fd = fds[0];
	pfds[0].events = POLLIN;
	pfds[1].fd = fds[1];
	pfds[1].events = POLLOUT;

	/* Use a timeout of 1 second. */
	timeout.tv_sec = 1;
	timeout.tv_nsec = 0;

	/*
	 * Check that we get a timeout waiting for data on the read end
	 * of our pipe.
	 */
	pfds[0].revents = -1;
	pfds[1].revents = -1;
	ATF_REQUIRE_EQ_MSG(ret = pollts(&pfds[0], 1, &timeout, NULL), 0,
	    "got: %d", ret);
	ATF_REQUIRE_EQ_MSG(pfds[0].revents, 0, "got: %d", pfds[0].revents);
	ATF_REQUIRE_EQ_MSG(pfds[1].revents, -1, "got: %d", pfds[1].revents);

	/* Check that the write end of the pipe as reported as ready. */
	pfds[0].revents = -1;
	pfds[1].revents = -1;
	ATF_REQUIRE_EQ_MSG(ret = pollts(&pfds[1], 1, &timeout, NULL), 1,
	    "got: %d", ret);
	ATF_REQUIRE_EQ_MSG(pfds[0].revents, -1, "got: %d", pfds[0].revents);
	ATF_REQUIRE_EQ_MSG(pfds[1].revents, POLLOUT, "got: %d",\
	    pfds[1].revents);

	/* Check that only the write end of the pipe as reported as ready. */
	pfds[0].revents = -1;
	pfds[1].revents = -1;
	ATF_REQUIRE_EQ_MSG(ret = pollts(pfds, 2, &timeout, NULL), 1,
	    "got: %d", ret);
	ATF_REQUIRE_EQ_MSG(pfds[0].revents, 0, "got: %d", pfds[0].revents);
	ATF_REQUIRE_EQ_MSG(pfds[1].revents, POLLOUT, "got: %d",
	    pfds[1].revents);

	/* Write data to our pipe. */
	ATF_REQUIRE_EQ(write(fds[1], "", 1), 1);

	/* Check that both ends of our pipe are reported as ready. */
	pfds[0].revents = -1;
	pfds[1].revents = -1;
	ATF_REQUIRE_EQ_MSG(ret = pollts(pfds, 2, &timeout, NULL), 2,
	    "got: %d", ret);
	ATF_REQUIRE_EQ_MSG(pfds[0].revents, POLLIN, "got: %d",
	    pfds[0].revents);
	ATF_REQUIRE_EQ_MSG(pfds[1].revents, POLLOUT, "got: %d",
	    pfds[1].revents);

	ATF_REQUIRE_EQ(close(fds[0]), 0);
	ATF_REQUIRE_EQ(close(fds[1]), 0);
}

ATF_TC(pollts_err);
ATF_TC_HEAD(pollts_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check errors from pollts(2)");
}

ATF_TC_BODY(pollts_err, tc)
{
	struct timespec timeout;
	struct pollfd pfd;
	int fd = 0;

	pfd.fd = fd;
	pfd.events = POLLIN;

	timeout.tv_sec = 1;
	timeout.tv_nsec = 0;

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, pollts((void *)-1, 1, &timeout, NULL) == -1);

	timeout.tv_sec = -1;
	timeout.tv_nsec = -1;

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, pollts(&pfd, 1, &timeout, NULL) == -1);
}

ATF_TC(pollts_sigmask);
ATF_TC_HEAD(pollts_sigmask, tc)
{
	atf_tc_set_md_var(tc, "timeout", "10");
	atf_tc_set_md_var(tc, "descr",
	    "Check that pollts(2) restores the signal mask (PR kern/44986)");
}

ATF_TC_BODY(pollts_sigmask, tc)
{
	int fd;
	struct pollfd pfd;
	struct timespec timeout;
	sigset_t mask;
	int ret;

	fd = open(_PATH_DEVNULL, O_RDONLY);
	ATF_REQUIRE(fd >= 0);

	pfd.fd = fd;
	pfd.events = POLLIN;

	/* Use a timeout of 1 second. */
	timeout.tv_sec = 1;
	timeout.tv_nsec = 0;

	/* Unblock all signals. */
	ATF_REQUIRE_EQ(sigfillset(&mask), 0);
	ATF_REQUIRE_EQ(sigprocmask(SIG_UNBLOCK, &mask, NULL), 0);

	/*
	 * Check that pollts(2) immediately returns. We block *all*
	 * signals during pollts(2).
	 */
	ATF_REQUIRE_EQ_MSG(ret = pollts(&pfd, 1, &timeout, &mask), 1,
	    "got: %d", ret);

	/* Check that signals are now longer blocked. */
	ATF_REQUIRE_EQ(sigprocmask(SIG_SETMASK, NULL, &mask), 0);
	ATF_REQUIRE_EQ_MSG(sigismember(&mask, SIGUSR1), 0,
	    "signal mask was changed.");

	ATF_REQUIRE_EQ(close(fd), 0);
}
#endif

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, poll_3way);
	ATF_TP_ADD_TC(tp, poll_basic);
	ATF_TP_ADD_TC(tp, poll_err);
#ifndef __FreeBSD__
	ATF_TP_ADD_TC(tp, pollts_basic);
	ATF_TP_ADD_TC(tp, pollts_err);
	ATF_TP_ADD_TC(tp, pollts_sigmask);
#endif

	return atf_no_error();
}
