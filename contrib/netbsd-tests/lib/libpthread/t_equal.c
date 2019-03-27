/* $NetBSD: t_equal.c,v 1.1 2011/03/24 12:40:59 jruoho Exp $ */

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
__RCSID("$NetBSD: t_equal.c,v 1.1 2011/03/24 12:40:59 jruoho Exp $");

#include <pthread.h>

#include <atf-c.h>

#include "h_common.h"

static void	*func(void *);

static void *
func(void *arg)
{
	return NULL;
}

ATF_TC(pthread_equal);
ATF_TC_HEAD(pthread_equal, tc)
{
	atf_tc_set_md_var(tc, "descr", "A test of pthread_equal(3)");
}

ATF_TC_BODY(pthread_equal, tc)
{
	pthread_t t1, t2;

	ATF_REQUIRE(pthread_create(&t1, NULL, func, NULL) == 0);
	ATF_REQUIRE(pthread_create(&t2, NULL, func, NULL) == 0);

	ATF_REQUIRE(pthread_equal(t1, t1) != 0);
	ATF_REQUIRE(pthread_equal(t2, t2) != 0);
	ATF_REQUIRE(pthread_equal(t1, t2) == 0);

	ATF_REQUIRE(pthread_join(t1, NULL) == 0);
	ATF_REQUIRE(pthread_join(t2, NULL) == 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, pthread_equal);

	return atf_no_error();
}
