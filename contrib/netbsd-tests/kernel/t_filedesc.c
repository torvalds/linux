/*	$NetBSD: t_filedesc.c,v 1.6 2017/01/13 21:30:41 christos Exp $	*/

/*-
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
__RCSID("$NetBSD: t_filedesc.c,v 1.6 2017/01/13 21:30:41 christos Exp $");

#include <sys/types.h>

#include <assert.h>
#include <atf-c.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"

ATF_TC(getfilerace);
ATF_TC_HEAD(getfilerace, tc)
{

	atf_tc_set_md_var(tc, "descr", "race between multithreaded proc. "
	    "fd_getfile() and fd_close() (PR kern/43694)");
}

static int fd;
static volatile bool quit;

static void *
wrkwrk(void *arg)
{

	/* just something to cause fd_getfile() to be called */
	while (!quit)
		rump_sys_write(fd, &fd, sizeof(fd));

	return NULL;
}

/* for me, 1000 triggers extremely seldom, 10k sometimes, 100k almost always */
#define DEFAULT_ITERATIONS 10000

ATF_TC_BODY(getfilerace, tc)
{
	pthread_t pt;
	int fd_wrk;
	int i, iters;

	/*
	 * Want a multiprocessor virtual kernel.  A multiprocessor host
	 * probably helps too, but that's harder to do in software...
	 */
	setenv("RUMP_NCPU", "2", 1);
	rump_init();

	fd = fd_wrk = rump_sys_open("/dev/null", O_RDWR, 0);
	if (fd == -1)
		atf_tc_fail_errno("cannot open /dev/null");

	if (atf_tc_has_config_var(tc, "iters"))
		iters = atoi(atf_tc_get_config_var(tc, "iters"));
	else
		iters = DEFAULT_ITERATIONS;

	pthread_create(&pt, NULL, wrkwrk, NULL);
	for (i = 0; i < iters; i++) {
		rump_sys_close(fd_wrk);
		fd_wrk = rump_sys_open("/dev/null", O_RDWR, 0);
		assert(fd == fd_wrk);
	}

	quit = true;
	pthread_join(pt, NULL);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, getfilerace);

	return atf_no_error();
}
