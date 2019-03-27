/* $NetBSD: t_clock_nanosleep.c,v 1.1 2016/11/11 15:30:44 njoly Exp $ */

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_clock_nanosleep.c,v 1.1 2016/11/11 15:30:44 njoly Exp $");

#include <atf-c.h>
#include <time.h>

ATF_TC(clock_nanosleep_remain);
ATF_TC_HEAD(clock_nanosleep_remain, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Check clock_nanosleep(2) remaining time");
}

ATF_TC_BODY(clock_nanosleep_remain, tc)
{
	struct timespec rqtp, rmtp;

	rqtp.tv_sec = 0; rqtp.tv_nsec = 0;
	rmtp.tv_sec = -1; rmtp.tv_nsec = -1;
	ATF_REQUIRE(clock_nanosleep(CLOCK_REALTIME, 0, &rqtp, &rmtp) == 0);
#ifdef __FreeBSD__
	ATF_CHECK(rmtp.tv_sec == -1 && rmtp.tv_nsec == -1);
#else
	ATF_CHECK(rmtp.tv_sec == 0 && rmtp.tv_nsec == 0);
#endif

	ATF_REQUIRE(clock_gettime(CLOCK_REALTIME, &rqtp) == 0);
	rmtp.tv_sec = -1; rmtp.tv_nsec = -1;
	ATF_REQUIRE(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &rqtp, &rmtp) == 0);
	ATF_CHECK(rmtp.tv_sec == -1 && rmtp.tv_nsec == -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, clock_nanosleep_remain);

	return atf_no_error();
}
