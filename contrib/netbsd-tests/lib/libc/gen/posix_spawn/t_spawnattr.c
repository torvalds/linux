/* $NetBSD: t_spawnattr.c,v 1.1 2012/02/13 21:03:08 martin Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles Zhang <charles@NetBSD.org> and
 * Martin Husemann <martin@NetBSD.org>.
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

#include <atf-c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>

static int get_different_scheduler(void);
static int get_different_priority(int scheduler);

static const int schedulers[] = {
	SCHED_OTHER,
	SCHED_FIFO,
	SCHED_RR
};

static int
get_different_scheduler(void)
{
	u_int i;
	int scheduler;

	/* get current schedule policy */
	scheduler = sched_getscheduler(0);
	for (i = 0; i < __arraycount(schedulers); i++) {
		if (schedulers[i] == scheduler)
			break;
	}
	ATF_REQUIRE_MSG(i < __arraycount(schedulers),
	    "Unknown current scheduler %d", scheduler);

	/* new scheduler */
	i++;
	if (i >= __arraycount(schedulers))
		i = 0;
	return schedulers[i];
}

static int
get_different_priority(int scheduler)
{
	int max, min, new, priority;
	struct sched_param param;

	max = sched_get_priority_max(scheduler);
	min = sched_get_priority_min(scheduler);

	sched_getparam(0, &param);
	priority = param.sched_priority;

	/*
	 * Change numerical value of the priority, to ensure that it
	 * was set for the spawned child.
	 */
	new = priority + 1;
	if (new > max)
		new = min;
	return new;
}

ATF_TC(t_spawnattr);

ATF_TC_HEAD(t_spawnattr, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
	atf_tc_set_md_var(tc, "descr",
	    "Tests posix_spawn with scheduler attributes");
}

ATF_TC_BODY(t_spawnattr, tc)
{
	int pid, scheduler, child_scheduler, priority, status, err, pfd[2];
	char helper_arg[128];
	char * const args[] = { __UNCONST("h_spawnattr"), helper_arg, NULL };
	struct sched_param sp, child_sp;
	sigset_t sig;
	posix_spawnattr_t attr;
	char helper[FILENAME_MAX];

	/*
	 * create a pipe to controll the child
	 */
	err = pipe(pfd);
	ATF_REQUIRE_MSG(err == 0, "could not create pipe, errno %d", errno);
	sprintf(helper_arg, "%d", pfd[0]);

	posix_spawnattr_init(&attr);

	scheduler = get_different_scheduler();
	priority = get_different_priority(scheduler);
	sp.sched_priority = priority;

	sigemptyset(&sig);
	sigaddset(&sig, SIGUSR1);

	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSCHEDULER |
	    POSIX_SPAWN_SETSCHEDPARAM | POSIX_SPAWN_SETPGROUP |
	    POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
	posix_spawnattr_setpgroup(&attr, 0);
	posix_spawnattr_setschedparam(&attr, &sp);
	posix_spawnattr_setschedpolicy(&attr, scheduler);
	posix_spawnattr_setsigmask(&attr, &sig);
	posix_spawnattr_setsigdefault(&attr, &sig);

	sprintf(helper, "%s/h_spawnattr",
	    atf_tc_get_config_var(tc, "srcdir"));
	err = posix_spawn(&pid, helper, NULL, &attr, args, NULL);
	ATF_REQUIRE_MSG(err == 0, "error %d", err);

	child_scheduler = sched_getscheduler(pid);
	ATF_REQUIRE_MSG(scheduler == child_scheduler,
	    "scheduler = %d, child_scheduler = %d, pid %d, errno %d",
	    scheduler, child_scheduler, pid, errno);

	sched_getparam(pid, &child_sp);
	ATF_REQUIRE_MSG(child_sp.sched_priority == sp.sched_priority,
	    "priority is: %d, but we requested: %d",
	    child_sp.sched_priority, sp.sched_priority);

	ATF_REQUIRE_MSG(pid == getpgid(pid), "child pid: %d, child pgid: %d",
	    pid, getpgid(pid));

	/* ready, let child go */
	write(pfd[1], "q", 1);
	close(pfd[0]);
	close(pfd[1]);

	/* wait and check result from child */
	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);

	posix_spawnattr_destroy(&attr);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_spawnattr);

	return atf_no_error();
}
