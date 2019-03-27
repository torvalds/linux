/* $NetBSD: t_detach.c,v 1.2 2017/01/16 16:29:54 christos Exp $ */

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
__RCSID("$NetBSD: t_detach.c,v 1.2 2017/01/16 16:29:54 christos Exp $");

#include <errno.h>
#include <pthread.h>
#include <time.h>

#include <atf-c.h>

#include "h_common.h"

static void	*func(void *);

static void *
func(void *arg)
{
	sleep(2);
	return NULL;
}

ATF_TC(pthread_detach);
ATF_TC_HEAD(pthread_detach, tc)
{
	atf_tc_set_md_var(tc, "descr", "A test of pthread_detach(3)");
}

ATF_TC_BODY(pthread_detach, tc)
{
	const int state = PTHREAD_CREATE_JOINABLE;
	pthread_attr_t attr;
	pthread_t t;
	int rv;

	/*
	 * Create a joinable thread.
	 */
	PTHREAD_REQUIRE(pthread_attr_init(&attr));
	PTHREAD_REQUIRE(pthread_attr_setdetachstate(&attr, state));
	PTHREAD_REQUIRE(pthread_create(&t, &attr, func, NULL));

	/*
	 * Detach the thread and try to
	 * join it; EINVAL should follow.
	 */
	PTHREAD_REQUIRE(pthread_detach(t));

	sleep(1);
	rv = pthread_join(t, NULL);
	ATF_REQUIRE(rv == EINVAL);

	sleep(3);

	/*
	 * As usual, ESRCH should follow if
	 * we try to detach an invalid thread.
	 */
	rv = pthread_cancel(t);
	ATF_REQUIRE(rv == ESRCH);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, pthread_detach);

	return atf_no_error();
}
