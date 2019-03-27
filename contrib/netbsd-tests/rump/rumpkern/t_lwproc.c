/*	$NetBSD: t_lwproc.c,v 1.9 2017/01/13 21:30:43 christos Exp $	*/

/*
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "h_macros.h"

ATF_TC(makelwp);
ATF_TC_HEAD(makelwp, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests that lwps can be attached to "
	    "processes");
}

ATF_TC_BODY(makelwp, tc)
{
	struct lwp *l;
	pid_t pid;

	rump_init();
	RZ(rump_pub_lwproc_newlwp(0));
	ATF_REQUIRE_EQ(rump_pub_lwproc_newlwp(37), ESRCH);
	l = rump_pub_lwproc_curlwp();

	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	ATF_REQUIRE(rump_pub_lwproc_curlwp() != l);
	l = rump_pub_lwproc_curlwp();

	RZ(rump_pub_lwproc_newlwp(rump_sys_getpid()));
	ATF_REQUIRE(rump_pub_lwproc_curlwp() != l);

	pid = rump_sys_getpid();
	ATF_REQUIRE(pid != -1 && pid != 0);
}

ATF_TC(proccreds);
ATF_TC_HEAD(proccreds, tc)
{

	atf_tc_set_md_var(tc, "descr", "check that procs have different creds");
}

ATF_TC_BODY(proccreds, tc)
{
	struct lwp *l1, *l2;

	rump_init();
	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	l1 = rump_pub_lwproc_curlwp();
	RZ(rump_pub_lwproc_newlwp(rump_sys_getpid()));

	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	l2 = rump_pub_lwproc_curlwp();

	RL(rump_sys_setuid(22));
	ATF_REQUIRE_EQ(rump_sys_getuid(), 22);

	rump_pub_lwproc_switch(l1);
	ATF_REQUIRE_EQ(rump_sys_getuid(), 0); /* from parent, proc0 */
	RL(rump_sys_setuid(11));
	ATF_REQUIRE_EQ(rump_sys_getuid(), 11);

	rump_pub_lwproc_switch(l2);
	ATF_REQUIRE_EQ(rump_sys_getuid(), 22);
	rump_pub_lwproc_newlwp(rump_sys_getpid());
	ATF_REQUIRE_EQ(rump_sys_getuid(), 22);
}


ATF_TC(inherit);
ATF_TC_HEAD(inherit, tc)
{

	atf_tc_set_md_var(tc, "descr", "new processes inherit creds from "
	    "parents");
}

ATF_TC_BODY(inherit, tc)
{

	rump_init();

	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	RL(rump_sys_setuid(66));
	ATF_REQUIRE_EQ(rump_sys_getuid(), 66);

	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	ATF_REQUIRE_EQ(rump_sys_getuid(), 66);

	/* release lwp and proc */
	rump_pub_lwproc_releaselwp();
	ATF_REQUIRE_EQ(rump_sys_getuid(), 0);
}

ATF_TC(lwps);
ATF_TC_HEAD(lwps, tc)
{

	atf_tc_set_md_var(tc, "descr", "proc can hold many lwps and is "
	    "automatically g/c'd when the last one exits");
}

#define LOOPS 128
ATF_TC_BODY(lwps, tc)
{
	struct lwp *l[LOOPS];
	pid_t mypid;
	struct lwp *l_orig;
	int i;

	rump_init();

	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	mypid = rump_sys_getpid();
	RL(rump_sys_setuid(375));

	l_orig = rump_pub_lwproc_curlwp();
	for (i = 0; i < LOOPS; i++) {
		mypid = rump_sys_getpid();
		ATF_REQUIRE(mypid != -1 && mypid != 0);
		RZ(rump_pub_lwproc_newlwp(mypid));
		l[i] = rump_pub_lwproc_curlwp();
		ATF_REQUIRE_EQ(rump_sys_getuid(), 375);
	}

	rump_pub_lwproc_switch(l_orig);
	rump_pub_lwproc_releaselwp();
	for (i = 0; i < LOOPS; i++) {
		rump_pub_lwproc_switch(l[i]);
		ATF_REQUIRE_EQ(rump_sys_getpid(), mypid);
		ATF_REQUIRE_EQ(rump_sys_getuid(), 375);
		rump_pub_lwproc_releaselwp();
		ATF_REQUIRE_EQ(rump_sys_getpid(), 1);
		ATF_REQUIRE_EQ(rump_sys_getuid(), 0);
	}

	ATF_REQUIRE_EQ(rump_pub_lwproc_newlwp(mypid), ESRCH);
}

ATF_TC(nolwprelease);
ATF_TC_HEAD(nolwprelease, tc)
{

	atf_tc_set_md_var(tc, "descr", "check that lwp context is required "
	    "for lwproc_releaselwp()");
}

ATF_TC_BODY(nolwprelease, tc)
{
	int status;

	switch (fork()) {
	case 0:
		rump_init();
		rump_pub_lwproc_releaselwp();
		atf_tc_fail("survived");
		break;
	case -1:
		atf_tc_fail_errno("fork");
		break;
	default:
		wait(&status);
		ATF_REQUIRE(WIFSIGNALED(status));
		ATF_REQUIRE_EQ(WTERMSIG(status), SIGABRT);

	}
}

ATF_TC(nolwp);
ATF_TC_HEAD(nolwp, tc)
{

	atf_tc_set_md_var(tc, "descr", "check that curlwp for an implicit "
	    "context is NULL");
}

ATF_TC_BODY(nolwp, tc)
{

	rump_init();
	ATF_REQUIRE_EQ(rump_pub_lwproc_curlwp(), NULL);
}

ATF_TC(nullswitch);
ATF_TC_HEAD(nullswitch, tc)
{

	atf_tc_set_md_var(tc, "descr", "check that switching to NULL marks "
	    "current lwp as not running");
}

ATF_TC_BODY(nullswitch, tc)
{
	struct lwp *l;

	rump_init();
	RZ(rump_pub_lwproc_newlwp(0));
	l = rump_pub_lwproc_curlwp();
	rump_pub_lwproc_switch(NULL);
	/* if remains LP_RUNNING, next call will panic */
	rump_pub_lwproc_switch(l);
}

ATF_TC(rfork);
ATF_TC_HEAD(rfork, tc)
{

	atf_tc_set_md_var(tc, "descr", "check that fork shares fd's");
}

ATF_TC_BODY(rfork, tc)
{
	struct stat sb;
	struct lwp *l, *l2;
	int fd;

	RZ(rump_init());

	ATF_REQUIRE_EQ(rump_pub_lwproc_rfork(RUMP_RFFDG|RUMP_RFCFDG), EINVAL);

	RZ(rump_pub_lwproc_rfork(0));
	l = rump_pub_lwproc_curlwp();

	RL(fd = rump_sys_open("/file", O_RDWR | O_CREAT, 0777));

	/* ok, first check rfork(RUMP_RFCFDG) does *not* preserve fd's */
	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));
	ATF_REQUIRE_ERRNO(EBADF, rump_sys_write(fd, &fd, sizeof(fd)) == -1);

	/* then check that rfork(0) does */
	rump_pub_lwproc_switch(l);
	RZ(rump_pub_lwproc_rfork(0));
	ATF_REQUIRE_EQ(rump_sys_write(fd, &fd, sizeof(fd)), sizeof(fd));
	RL(rump_sys_fstat(fd, &sb));
	l2 = rump_pub_lwproc_curlwp();

	/*
	 * check that the shared fd table is really shared by
	 * closing fd in parent
	 */
	rump_pub_lwproc_switch(l);
	RL(rump_sys_close(fd));
	rump_pub_lwproc_switch(l2);
	ATF_REQUIRE_ERRNO(EBADF, rump_sys_fstat(fd, &sb) == -1);

	/* redo, this time copying the fd table instead of sharing it */
	rump_pub_lwproc_releaselwp();
	rump_pub_lwproc_switch(l);
	RL(fd = rump_sys_open("/file", O_RDWR, 0777));
	RZ(rump_pub_lwproc_rfork(RUMP_RFFDG));
	ATF_REQUIRE_EQ(rump_sys_write(fd, &fd, sizeof(fd)), sizeof(fd));
	RL(rump_sys_fstat(fd, &sb));
	l2 = rump_pub_lwproc_curlwp();

	/* check that the fd table is copied */
	rump_pub_lwproc_switch(l);
	RL(rump_sys_close(fd));
	rump_pub_lwproc_switch(l2);
	RL(rump_sys_fstat(fd, &sb));
	ATF_REQUIRE_EQ(sb.st_size, sizeof(fd));
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, makelwp);
	ATF_TP_ADD_TC(tp, proccreds);
	ATF_TP_ADD_TC(tp, inherit);
	ATF_TP_ADD_TC(tp, lwps);
	ATF_TP_ADD_TC(tp, nolwprelease);
	ATF_TP_ADD_TC(tp, nolwp);
	ATF_TP_ADD_TC(tp, nullswitch);
	ATF_TP_ADD_TC(tp, rfork);

	return atf_no_error();
}
