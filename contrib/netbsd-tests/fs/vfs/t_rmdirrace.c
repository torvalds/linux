/*	$NetBSD: t_rmdirrace.c,v 1.9 2012/02/16 02:47:56 perseant Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nicolas Joly.
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

#include <sys/stat.h>

#include <atf-c.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rump.h>

#include "../common/h_fsmacros.h"

#define DIRNAME "rmdir.test"

static void *func1(void *arg)
{

	while (*(int *)arg != 1)
		rump_sys_mkdir(DIRNAME, 0755);

	return NULL;
}

static void *func2(void *arg)
{

	while (*(int *)arg != 1)
		rump_sys_rmdir(DIRNAME);

	return NULL;
}

static void
race(const atf_tc_t *tc, const char *path)
{
	int res, fd, quit;
	pthread_t th1, th2;

	if (FSTYPE_SYSVBFS(tc))
		atf_tc_skip("directories not supported by file system");

	fd = rump_sys_open(".", O_RDONLY, 0666);
	if (fd == -1)
		atf_tc_fail("open failed");
	res = rump_sys_chdir(path);
	if (res == -1)
		atf_tc_fail("chdir failed");

	quit = 0;

	res = pthread_create(&th1, NULL, func1, &quit);
	if (res != 0)
		atf_tc_fail("pthread_create1 failed");
	res = pthread_create(&th2, NULL, func2, &quit);
	if (res != 0)
		atf_tc_fail("pthread_create2 failed");

	sleep(10);

	quit = 1;

	res = pthread_join(th2, NULL);
	if (res != 0)
		atf_tc_fail("pthread_join2 failed");
	res = pthread_join(th1, NULL);
	if (res != 0)
		atf_tc_fail("pthread_join1 failed");

	res = rump_sys_fchdir(fd);
	if (res == -1)
		atf_tc_fail("fchdir failed");
}

ATF_FSAPPLY(race, "rmdir(2) race");
