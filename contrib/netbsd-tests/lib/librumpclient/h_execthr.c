/*	$NetBSD: h_execthr.c,v 1.7 2016/11/24 00:37:29 dholland Exp $	*/

/*
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rump/rumpclient.h>
#include <rump/rump_syscalls.h>

//#define VERBOSE

#ifdef VERBOSE
#define SAY(...) printf(__VA_ARGS__)
#else
#define SAY(...)
#endif

static int canreturn = 0;

/*
 * Use a fairly large number of threads so that we have
 * a better chance catching races.  XXX: this is rumpuser's
 * MAXWORKER-1.
 */
#define NTHR 63

#define P1_0 3
#define P1_1 4
#define P2_0 5
#define P2_1 6

static void *
wrk(void *arg)
{
	int fd = (uintptr_t)arg;

	rump_sys_read(fd, &fd, sizeof(fd));
	if (!canreturn)
		errx(1, "should not have returned");
	if (fd != 37)
		errx(1, "got invalid magic");

	return NULL;
}

static int
getproc(pid_t mypid, struct kinfo_proc2 *p)
{
	int name[6];
	size_t len = sizeof(*p);

	name[0] = CTL_KERN;
	name[1] = KERN_PROC2;
	name[2] = KERN_PROC_PID;
	name[3] = mypid;
	name[4] = len;
	name[5] = 1;

	return rump_sys___sysctl(name, __arraycount(name), p, &len, NULL, 0);
} 

int
main(int argc, char *argv[], char *envp[])
{
	struct kinfo_proc2 p;
	char *execarg[3];
	int p1[2], p2[2];
	pid_t mypid;
	pthread_t pt;
	ssize_t n;
	int i, execd;
	char nexec[16];

	if (argc > 1)
		execd = atoi(argv[1]);
	else
		execd = 0;
	sprintf(nexec, "%d", execd+1);
	SAY("execd: %d\n", execd);

	if (rumpclient_init() == -1) {
		if (execd)
			err(1, "init execd");
		else
			err(1, "init");
	}
	mypid = rump_sys_getpid();
	SAY("rumpclient_init finished.\n");

	if (execd) {
		canreturn = 1;
		errno = pthread_create(&pt, NULL,
		    wrk, (void *)(uintptr_t)P2_0);
		if (errno != 0)
			err(1, "exec pthread_create");
		SAY("startup pthread_create finished.\n");

		i = 37;
		rump_sys_write(P2_1, &i, sizeof(i));
		pthread_join(pt, NULL);
		SAY("startup pthread_join finished.\n");

		n = rump_sys_read(P1_0, &i, sizeof(i));
		if (n != -1 || errno != EBADF)
			errx(1, "post-exec cloexec works");
		SAY("startup rump_sys_read finished.\n");

		getproc(mypid, &p);
		SAY("startup getproc finished.\n");
		if (p.p_nlwps != 2)
			errx(1, "invalid nlwps: %lld", (long long)p.p_nlwps);

		/* we passed? */
		if (execd > 10) {
			SAY("done.\n");
			exit(0);
		}

		rump_sys_close(P2_0);
		rump_sys_close(P2_1);
	}

	SAY("making pipes...\n");

	if (rump_sys_pipe(p1) == -1)
		err(1, "pipe1");
	if (p1[0] != P1_0 || p1[1] != P1_1)
		errx(1, "p1 assumptions failed %d %d", p1[0], p1[1]);
	if (rump_sys_pipe(p2) == -1)
		err(1, "pipe1");
	if (p2[0] != P2_0 || p2[1] != P2_1)
		errx(1, "p2 assumptions failed");
	if (rump_sys_fcntl(p1[0], F_SETFD, FD_CLOEXEC) == -1)
		err(1, "cloexec");
	if (rump_sys_fcntl(p1[1], F_SETFD, FD_CLOEXEC) == -1)
		err(1, "cloexec");

	SAY("making threads...\n");

	for (i = 0; i < NTHR; i++) {
		errno = pthread_create(&pt, NULL,
		    wrk, (void *)(uintptr_t)p1[0]);
		if (errno != 0)
			err(1, "pthread_create 1 %d", i);
	}

	for (i = 0; i < NTHR; i++) {
		errno = pthread_create(&pt, NULL,
		    wrk, (void *)(uintptr_t)p2[0]);
		if (errno != 0)
			err(1, "pthread_create 2 %d", i);
	}

	SAY("waiting for threads to start...\n");

	/* wait for all the threads to be enjoying themselves */
	for (;;) {
		getproc(mypid, &p);
		SAY("getproc finished.\n");
		if (p.p_nlwps == 2*NTHR + 2)
			break;
		usleep(10000);
	}

	SAY("making some more threads start...\n");

	/*
	 * load up one more (big) set.  these won't start executing, though,
	 * but we're interested in if they create blockage
	 */
	for (i = 0; i < 3*NTHR; i++) {
		errno = pthread_create(&pt, NULL,
		    wrk, (void *)(uintptr_t)p1[0]);
		if (errno != 0)
			err(1, "pthread_create 3 %d", i);
	}

	SAY("calling exec...\n");

	/* then, we exec! */
	execarg[0] = argv[0];
	execarg[1] = nexec;
	execarg[2] = NULL;
	if (rumpclient_exec(argv[0], execarg, envp) == -1)
		err(1, "exec");
}
