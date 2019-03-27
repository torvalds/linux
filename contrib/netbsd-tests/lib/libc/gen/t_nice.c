/*	$NetBSD: t_nice.c,v 1.8 2012/03/18 07:00:51 jruoho Exp $ */

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
__RCSID("$NetBSD: t_nice.c,v 1.8 2012/03/18 07:00:51 jruoho Exp $");

#include <sys/resource.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

static void	*threadfunc(void *);

static void *
threadfunc(void *arg)
{
	int pri, val;

	val = *(int *)arg;

	errno = 0;
	pri = getpriority(PRIO_PROCESS, 0);
	ATF_REQUIRE(errno == 0);

	if (pri != val)
		atf_tc_fail("nice(3) value was not propagated to threads");

	return NULL;
}

ATF_TC(nice_err);
ATF_TC_HEAD(nice_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test nice(3) for invalid parameters (PR lib/42587)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(nice_err, tc)
{
	int i;

	/*
	 * The call should fail with EPERM if the
	 * supplied parameter is negative and the
	 * caller does not have privileges.
	 */
	for (i = -20; i < 0; i++) {

		errno = 0;

		ATF_REQUIRE_ERRNO(EPERM, nice(i) == -1);
	}
}

ATF_TC(nice_priority);
ATF_TC_HEAD(nice_priority, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test nice(3) vs. getpriority(2)");
}

ATF_TC_BODY(nice_priority, tc)
{
#ifdef __FreeBSD__
	int i, pri, pri2, nic;
#else
	int i, pri, nic;
#endif
	pid_t pid;
	int sta;

	for (i = 0; i <= 20; i++) {

		nic = nice(i);
		ATF_REQUIRE(nic != -1);

		errno = 0;
		pri = getpriority(PRIO_PROCESS, 0);
		ATF_REQUIRE(errno == 0);

#ifdef __NetBSD__
		if (nic != pri)
			atf_tc_fail("nice(3) and getpriority(2) conflict");
#endif

		/*
		 * Also verify that the nice(3) values
		 * are inherited by child processes.
		 */
		pid = fork();
		ATF_REQUIRE(pid >= 0);

		if (pid == 0) {

			errno = 0;
#ifdef __NetBSD__
			pri = getpriority(PRIO_PROCESS, 0);
#else
			pri2 = getpriority(PRIO_PROCESS, 0);
#endif
			ATF_REQUIRE(errno == 0);

#ifdef __FreeBSD__
			if (pri != pri2)
#else
			if (nic != pri)
#endif
				_exit(EXIT_FAILURE);

			_exit(EXIT_SUCCESS);
		}

		(void)wait(&sta);

		if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
			atf_tc_fail("nice(3) value was not inherited");
	}
}

ATF_TC(nice_root);
ATF_TC_HEAD(nice_root, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that nice(3) works");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(nice_root, tc)
{
	int i;

	for (i = -20; i <= 20; i++) {

		ATF_REQUIRE(nice(i) != -1);
	}
}

ATF_TC(nice_thread);
ATF_TC_HEAD(nice_thread, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test nice(3) with threads");
}

ATF_TC_BODY(nice_thread, tc)
{
	pthread_t tid[5];
#ifdef __FreeBSD__
	int pri, rv, val;
#else
	int rv, val;
#endif
	size_t i;

	/*
	 * Test that the scheduling priority is
	 * propagated to all system scope threads.
	 */
	for (i = 0; i < __arraycount(tid); i++) {

		val = nice(i);
		ATF_REQUIRE(val != -1);

#ifdef __FreeBSD__
		pri = getpriority(PRIO_PROCESS, 0);
		rv = pthread_create(&tid[i], NULL, threadfunc, &pri);
#else
		rv = pthread_create(&tid[i], NULL, threadfunc, &val);
#endif
		ATF_REQUIRE(rv == 0);

		rv = pthread_join(tid[i], NULL);
		ATF_REQUIRE(rv == 0);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, nice_err);
	ATF_TP_ADD_TC(tp, nice_priority);
	ATF_TP_ADD_TC(tp, nice_root);
	ATF_TP_ADD_TC(tp, nice_thread);

	return atf_no_error();
}
