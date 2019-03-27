/*	$NetBSD: t_fuzz.c,v 1.6 2017/01/13 21:30:40 christos Exp $	*/

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

/*
 * Fuzztest puffs mount.  There are n different levels of testing:
 * each one pours more and more sane garbage into the args to that
 * the mount progresses further and further.  Level 8 (at least when
 * writing this comment) should be the one where mounting actually
 * succeeds.
 *
 * Our metric of success is crash / no crash.
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/poll.h>

#include <assert.h>
#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <fs/puffs/puffs_msgif.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"

#define ITERATIONS 100

static void
fixversion(struct puffs_kargs *kargs)
{

	kargs->pa_vers = PUFFSVERSION;
}

static void
fixkflag(struct puffs_kargs *kargs)
{

	kargs->pa_flags &= PUFFS_KFLAG_MASK;

	/*
	 * PUFFS_KFLAG_CACHE_FS_TTL require extended behavior
	 * from the filesystem for which we have no test right now.
	 */
	kargs->pa_flags &= ~PUFFS_KFLAG_CACHE_FS_TTL;
}

static void
fixfhflag(struct puffs_kargs *kargs)
{

	kargs->pa_fhflags &= PUFFS_FHFLAG_MASK;
}

static void
fixspare(struct puffs_kargs *kargs)
{

	memset(&kargs->pa_spare, 0, sizeof(kargs->pa_spare));
}

static void
fixhandsize(struct puffs_kargs *kargs)
{

	kargs->pa_fhsize %= PUFFS_FHSIZE_MAX+4;
}

static void
fixhandsize2(struct puffs_kargs *kargs)
{

	/* XXX: values */
	if (kargs->pa_fhflags & PUFFS_FHFLAG_NFSV3)
		kargs->pa_fhsize %= 60;
	if (kargs->pa_fhflags & PUFFS_FHFLAG_NFSV2)
		kargs->pa_fhsize %= 28;
}

static void
fixputter(struct puffs_kargs *kargs)
{

	kargs->pa_fd = rump_sys_open("/dev/putter", O_RDWR);
	if (kargs->pa_fd == -1)
		atf_tc_fail_errno("open putter");
}

static void
fixroot(struct puffs_kargs *kargs)
{

	kargs->pa_root_vtype %= VBAD;
}

static void
unfixputter(struct puffs_kargs *kargs)
{

	rump_sys_close(kargs->pa_fd);
}

typedef void (*fixfn)(struct puffs_kargs *);
static fixfn fixstack[] = {
	fixversion,
	fixkflag,
	fixfhflag,
	fixspare,
	fixhandsize,
	fixhandsize2,
	fixputter,
	fixroot,
};

static void
fixup(int nfix, struct puffs_kargs *kargs)
{
	int i;

	assert(nfix <= __arraycount(fixstack));
	for (i = 0; i < nfix; i++)
		fixstack[i](kargs);
}

static void
unfixup(int nfix, struct puffs_kargs *kargs)
{

	if (nfix >= 7)
		unfixputter(kargs);
}

static pthread_mutex_t damtx;
static pthread_cond_t dacv;
static int dafd = -1;

static void *
respondthread(void *arg)
{
	char buf[PUFFS_MSG_MAXSIZE];
	struct puffs_req *preq = (void *)buf;
	struct pollfd pfd;
	ssize_t n;

	pthread_mutex_lock(&damtx);
	for (;;) {
		while (dafd == -1)
			pthread_cond_wait(&dacv, &damtx);

		while (dafd != -1) {
			pthread_mutex_unlock(&damtx);
			pfd.fd = dafd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			if (rump_sys_poll(&pfd, 1, 10) == 0) {
				pthread_mutex_lock(&damtx);
				continue;
			}
			n = rump_sys_read(dafd, buf, sizeof(buf));
			if (n <= 0) {
				pthread_mutex_lock(&damtx);
				break;
			}

			/* just say it was succesful */
			preq->preq_rv = 0;
			rump_sys_write(dafd, buf, n);
			pthread_mutex_lock(&damtx);
		}
	}

	return NULL;
}

static void
testbody(int nfix)
{
	pthread_t pt;
	struct puffs_kargs kargs;
	unsigned long seed;
	int i;

	seed = time(NULL);
	srandom(seed);
	printf("test seeded RNG with %lu\n", seed);

	rump_init();

	pthread_mutex_init(&damtx, NULL);
	pthread_cond_init(&dacv, NULL);
	pthread_create(&pt, NULL, respondthread, NULL);

	ATF_REQUIRE(rump_sys_mkdir("/mnt", 0777) == 0);

	for (i = 0; i < ITERATIONS; i++) {
		tests_makegarbage(&kargs, sizeof(kargs));
		fixup(nfix, &kargs);
		if (rump_sys_mount(MOUNT_PUFFS, "/mnt", 0,
		    &kargs, sizeof(kargs)) == 0) {
			struct stat sb;

			pthread_mutex_lock(&damtx);
			dafd = kargs.pa_fd;
			pthread_cond_signal(&dacv);
			pthread_mutex_unlock(&damtx);

			rump_sys_stat("/mnt", &sb);
			rump_sys_unmount("/mnt", MNT_FORCE);
		}
		unfixup(nfix, &kargs);

		pthread_mutex_lock(&damtx);
		dafd = -1;
		pthread_mutex_unlock(&damtx);
	}
}

#define MAKETEST(_n_)							\
ATF_TC(mountfuzz##_n_);							\
ATF_TC_HEAD(mountfuzz##_n_, tc)						\
{atf_tc_set_md_var(tc, "descr", "garbage kargs, " # _n_ " fix(es)");}	\
ATF_TC_BODY(mountfuzz##_n_, tc) {testbody(_n_);}

MAKETEST(0);
MAKETEST(1);
MAKETEST(2);
MAKETEST(3);
MAKETEST(4);
MAKETEST(5);
MAKETEST(6);
MAKETEST(7);
MAKETEST(8);

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mountfuzz0);
	ATF_TP_ADD_TC(tp, mountfuzz1);
	ATF_TP_ADD_TC(tp, mountfuzz2);
	ATF_TP_ADD_TC(tp, mountfuzz3);
	ATF_TP_ADD_TC(tp, mountfuzz4);
	ATF_TP_ADD_TC(tp, mountfuzz5);
	ATF_TP_ADD_TC(tp, mountfuzz6);
	ATF_TP_ADD_TC(tp, mountfuzz7);
	ATF_TP_ADD_TC(tp, mountfuzz8);

	return atf_no_error();
}
