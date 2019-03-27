/*	$NetBSD: t_mountd.c,v 1.6 2017/01/13 21:30:40 christos Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
#include <sys/mount.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"
#include "../common/h_fsmacros.h"

ATF_TC(mountdhup);
ATF_TC_HEAD(mountdhup, tc)
{

	atf_tc_set_md_var(tc, "descr", "test for service interrupt while "
	    "mountd handles SIGHUP");
}

static volatile int quit;

static void *
wrkwrkwrk(void *unused)
{
	int fd, fail;

	fail = 0;

	rump_sys_chdir(FSTEST_MNTNAME);
	while (!quit) {
		fd = rump_sys_open("file", O_RDWR | O_CREAT);
		if (fd == -1) {
			if (errno == EACCES) {
				fail++;
				break;
			}
		}
		rump_sys_close(fd);
		if (rump_sys_unlink("file") == -1) {
			if (errno == EACCES) {
				fail++;
				break;
			}
		}
	}
	rump_sys_chdir("/");
	quit = 1;

	return fail ? wrkwrkwrk : NULL;
}

ATF_TC_BODY(mountdhup, tc)
{
	pthread_t pt;
	struct nfstestargs *nfsargs;
	void *voidargs;
	int attempts;
	void *fail;

	FSTEST_CONSTRUCTOR(tc, nfs, voidargs);
	nfsargs = voidargs;

	pthread_create(&pt, NULL, wrkwrkwrk, NULL);
	for (attempts = 100; attempts && !quit; attempts--) {
		usleep(100000);
		kill(nfsargs->ta_childpid, SIGHUP);
	}
	quit = 1;
	pthread_join(pt, &fail);

	FSTEST_DESTRUCTOR(tc, nfs, voidargs);

	atf_tc_expect_fail("PR kern/5844");
	if (fail)
		atf_tc_fail("op failed with EACCES");
	else
		atf_tc_fail("race did not trigger this time");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mountdhup);

	return atf_no_error();
}
