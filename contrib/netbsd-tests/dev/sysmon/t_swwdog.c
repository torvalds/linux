/*	$NetBSD: t_swwdog.c,v 1.7 2017/01/13 21:30:39 christos Exp $	*/

/*
 * Copyright (c) 2010 Antti Kantee.  All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/wdog.h>

#include <assert.h>
#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"

static volatile sig_atomic_t tcount;

static void
sigcount(int sig)
{

	assert(sig == SIGUSR1);
	tcount++;
}

/*
 * Since we are testing for swwdog's ability to reboot/panic, we need
 * to fork and monitor the exit status from the parent and report
 * something sensible back to atf.
 */
static int
testbody(int max)
{
	char wname[WDOG_NAMESIZE];
	struct wdog_conf wc;
	struct wdog_mode wm;
	pid_t p1, p2;
	int status;
	int fd;

	signal(SIGUSR1, sigcount);

	switch ((p1 = fork())) {
	case 0:
		break;
	case -1:
		atf_tc_fail_errno("fork");
		break;
	default:
		p2 = wait(&status);
		ATF_REQUIRE_EQ(p1, p2);
		ATF_REQUIRE_EQ(tcount, max);
		return status;
	}

	rump_init();

	fd = rump_sys_open("/dev/watchdog", O_RDWR);
	if (fd == -1)
		err(1, "open watchdog");

	wc.wc_count = 1;
	wc.wc_names = wname;

	if (rump_sys_ioctl(fd, WDOGIOC_GWDOGS, &wc) == -1)
		err(1, "can't fetch watchdog names");

	if (wc.wc_count) {
		assert(wc.wc_count == 1);

		strlcpy(wm.wm_name, wc.wc_names, sizeof(wm.wm_name));
		wm.wm_mode = WDOG_MODE_ETICKLE;
		wm.wm_period = 1;
		if (rump_sys_ioctl(fd, WDOGIOC_SMODE, &wm) == -1)
			atf_tc_fail_errno("failed to set tickle");

		usleep(400000);
		if (max == 1)
			rump_sys_ioctl(fd, WDOGIOC_TICKLE);
		else {
			wm.wm_mode = WDOG_MODE_DISARMED;
			rump_sys_ioctl(fd, WDOGIOC_SMODE, &wm);
		}
		kill(getppid(), SIGUSR1);

		sleep(2);
		printf("staying alive\n");
		kill(getppid(), SIGUSR1);
		_exit(2);
	}
	/* fail */
	printf("no watchdog registered!\n");
	_exit(1);
}

ATF_TC(reboot);
ATF_TC_HEAD(reboot, tc)
{

	atf_tc_set_md_var(tc, "descr", "check swwdog reboot capability");
}

ATF_TC_BODY(reboot, tc)
{
	extern bool rumpns_swwdog_reboot;
	int status;

	/* XXX: should use sysctl */
	rumpns_swwdog_reboot = true;
	status = testbody(1);

	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE_EQ(WEXITSTATUS(status), 0);
}

ATF_TC(panic);
ATF_TC_HEAD(panic, tc)
{

	atf_tc_set_md_var(tc, "descr", "check swwdog panic capability");
}

ATF_TC_BODY(panic, tc)
{
	extern bool rumpns_swwdog_reboot;
	int status;

	/* XXX: should use sysctl */
	rumpns_swwdog_reboot = false;
	status = testbody(1);

	ATF_REQUIRE(WIFSIGNALED(status));
	ATF_REQUIRE_EQ(WTERMSIG(status), SIGABRT);
}

ATF_TC(disarm);
ATF_TC_HEAD(disarm, tc)
{

	atf_tc_set_md_var(tc, "descr", "check swwdog disarm capability");
}

ATF_TC_BODY(disarm, tc)
{
	int status;

	status = testbody(2);

	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE_EQ(WEXITSTATUS(status), 2);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, panic);
	ATF_TP_ADD_TC(tp, reboot);
	ATF_TP_ADD_TC(tp, disarm);

	return atf_no_error();
}
