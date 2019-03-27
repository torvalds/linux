/*	$NetBSD: t_cpuset.c,v 1.1 2011/11/08 05:47:00 jruoho Exp $ */

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
__RCSID("$NetBSD: t_cpuset.c,v 1.1 2011/11/08 05:47:00 jruoho Exp $");

#include <atf-c.h>
#include <limits.h>
#include <stdio.h>
#include <sched.h>

ATF_TC(cpuset_err);
ATF_TC_HEAD(cpuset_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from cpuset(3)");
}

ATF_TC_BODY(cpuset_err, tc)
{
	cpuset_t *set;

	set = cpuset_create();
	ATF_REQUIRE(set != NULL);

	ATF_CHECK(cpuset_set(-1, set) == -1);
	ATF_CHECK(cpuset_clr(-1, set) == -1);
	ATF_CHECK(cpuset_isset(-1, set) == -1);

	ATF_CHECK(cpuset_set(INT_MAX, set) == -1);
	ATF_CHECK(cpuset_clr(INT_MAX, set) == -1);
	ATF_CHECK(cpuset_isset(INT_MAX, set) == -1);

	cpuset_destroy(set);
}

ATF_TC(cpuset_set);
ATF_TC_HEAD(cpuset_set, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cpuset_set(3)");
}

ATF_TC_BODY(cpuset_set, tc)
{
	cpuset_t *set;

	set = cpuset_create();
	ATF_REQUIRE(set != NULL);

	ATF_REQUIRE(cpuset_set(0, set) == 0);
	ATF_REQUIRE(cpuset_isset(0, set) > 0);
	ATF_REQUIRE(cpuset_clr(0, set) == 0);
	ATF_REQUIRE(cpuset_isset(0, set) == 0);

	cpuset_destroy(set);
}

ATF_TC(cpuset_size);
ATF_TC_HEAD(cpuset_size, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test puset_size(3)");
}

ATF_TC_BODY(cpuset_size, tc)
{
	cpuset_t *set;
	size_t size;

	set = cpuset_create();
	ATF_REQUIRE(set != NULL);

	size = cpuset_size(set);

	ATF_CHECK(cpuset_set((size * 8) - 1, set) == 0);
	ATF_CHECK(cpuset_set((size * 8) + 1, set) == -1);

	cpuset_destroy(set);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, cpuset_err);
	ATF_TP_ADD_TC(tp, cpuset_set);
	ATF_TP_ADD_TC(tp, cpuset_size);

	return atf_no_error();
}
