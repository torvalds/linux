/*	$NetBSD: t_time.c,v 1.4 2017/01/10 15:32:46 christos Exp $ */

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
__RCSID("$NetBSD: t_time.c,v 1.4 2017/01/10 15:32:46 christos Exp $");

#include <atf-c.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

ATF_TC(time_copy);
ATF_TC_HEAD(time_copy, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test the return values of time(3)");
}

ATF_TC_BODY(time_copy, tc)
{
	time_t t1, t2 = 0;

	t1 = time(&t2);

	if (t1 != t2)
		atf_tc_fail("incorrect return values from time(3)");
}

ATF_TC(time_mono);
ATF_TC_HEAD(time_mono, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test monotonicity of time(3)");
}

ATF_TC_BODY(time_mono, tc)
{
	const size_t maxiter = 10;
	time_t t1, t2;
	size_t i;

	for (i = 0; i < maxiter; i++) {

		t1 = time(NULL);
		(void)sleep(1);
		t2 = time(NULL);

		(void)fprintf(stderr, "%"PRId64" vs. %"PRId64"\n",
		    (int64_t)t1, (int64_t)t2);

		if (t1 >= t2)
			atf_tc_fail("time(3) is not monotonic");
	}
}

ATF_TC(time_timeofday);
ATF_TC_HEAD(time_timeofday, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test time(3) vs. gettimeofday(2)");
}

ATF_TC_BODY(time_timeofday, tc)
{
	struct timeval tv = { 0, 0 };
	time_t t1, t2;

	t1 = time(NULL);
	ATF_REQUIRE(gettimeofday(&tv, NULL) == 0);
	t2 = time(NULL);

	(void)fprintf(stderr, "%"PRId64" vs. %"PRId64"\n",
	    (int64_t)t1, (int64_t)tv.tv_sec);

	if (t1 > tv.tv_sec || t2 < tv.tv_sec)
		atf_tc_fail("time(3) and gettimeofday(2) differ");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, time_copy);
	ATF_TP_ADD_TC(tp, time_mono);
	ATF_TP_ADD_TC(tp, time_timeofday);

	return atf_no_error();
}
