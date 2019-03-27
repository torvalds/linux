/* $NetBSD: t_kill.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $ */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
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

/*-
 * Copyright (c)2004 YAMAMOTO Takashi,
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_kill.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $");

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <atf-c.h>

#include "h_common.h"

#define    NTHREAD    2

struct threadinfo {
	pthread_t id;
	sig_atomic_t gotsignal;
} th[NTHREAD];

pthread_t mainthread;

static void
sighandler(int signo)
{
	pthread_t self;
	int i;

	self = pthread_self();
	ATF_REQUIRE_MSG((self != mainthread) && (signo == SIGUSR1),
				  "unexpected signal");

	for (i = 0; i < NTHREAD; i++)
		if (self == th[i].id)
			break;

	ATF_REQUIRE_MSG(i != NTHREAD, "unknown thread");

	th[i].gotsignal++;
}

static void *
f(void *arg)
{
	struct threadinfo volatile *t = arg;

	while (t->gotsignal < 1) {
		/* busy loop */
	}

	return NULL;
}

ATF_TC(simple);
ATF_TC_HEAD(simple, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks pthread_kill()");
}
ATF_TC_BODY(simple, tc)
{
	int i;
	pthread_t self;

	mainthread = pthread_self();

	ATF_REQUIRE(signal(SIGUSR1, sighandler) != SIG_ERR);

	for (i = 0; i < NTHREAD; i++) {
		PTHREAD_REQUIRE(pthread_create(&th[i].id, NULL, f, &th[i]));
	}

	sched_yield();

	self = pthread_self();
	ATF_REQUIRE_EQ_MSG(self, mainthread, "thread id changed");

	for (i = 0; i < NTHREAD; i++) {
		PTHREAD_REQUIRE(pthread_kill(th[i].id, SIGUSR1));
	}

	for (i = 0; i < NTHREAD; i++) {
		PTHREAD_REQUIRE(pthread_join(th[i].id, NULL));
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, simple);

	return atf_no_error();
}
