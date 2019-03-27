/* $NetBSD: t_mktime.c,v 1.5 2012/03/18 07:33:58 jruoho Exp $ */

/*-
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

#include <atf-c.h>

#include <err.h>
#include <errno.h>
#include <string.h>
#include <time.h>

ATF_TC(localtime_r_gmt);
ATF_TC_HEAD(localtime_r_gmt, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that localtime_r(3) "
	    "returns localtime, not GMT (PR lib/28324)");
}

ATF_TC_BODY(localtime_r_gmt, tc)
{
	struct tm *t;
	struct tm tt;
	time_t x;

	x = time(NULL);
	localtime_r(&x, &tt);
	t = localtime(&x);

	if (t->tm_sec != tt.tm_sec || t->tm_min != tt.tm_min ||
	    t->tm_hour != tt.tm_hour || t->tm_mday != tt.tm_mday)
		atf_tc_fail("inconsistencies between "
		    "localtime(3) and localtime_r(3)");
}

ATF_TC(mktime_negyear);
ATF_TC_HEAD(mktime_negyear, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mktime(3) with negative year");
}

ATF_TC_BODY(mktime_negyear, tc)
{
#ifdef __FreeBSD__
	atf_tc_expect_fail("needs work");
#endif
	struct tm tms;
	time_t t;

	(void)memset(&tms, 0, sizeof(tms));
	tms.tm_year = ~0;

	errno = 0;
	t = mktime(&tms);
	ATF_REQUIRE_ERRNO(0, t != (time_t)-1);
}

ATF_TC(timegm_epoch);
ATF_TC_HEAD(timegm_epoch, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test timegm(3) close to the epoch");
}

ATF_TC_BODY(timegm_epoch, tc)
{
	struct tm tms;
	time_t t;

	/* midnight on 1 Jan 1970 */
	(void)memset(&tms, 0, sizeof(tms));
	errno = 0;
	tms.tm_year = 1970 - 1900;
	tms.tm_mday = 1;
	t = timegm(&tms);
	ATF_REQUIRE_ERRNO(0, t == (time_t)0);

	/* one second after midnight on 1 Jan 1970 */
	(void)memset(&tms, 0, sizeof(tms));
	errno = 0;
	tms.tm_year = 1970 - 1900;
	tms.tm_mday = 1;
	tms.tm_sec = 1;
	t = timegm(&tms);
	ATF_REQUIRE_ERRNO(0, t == (time_t)1);

	/*
	 * 1969-12-31 23:59:59 = one second before the epoch.
	 * Result should be -1 with errno = 0.
	 */
	(void)memset(&tms, 0, sizeof(tms));
	errno = 0;
	tms.tm_year = 1969 - 1900;
	tms.tm_mon = 12 - 1;
	tms.tm_mday = 31;
	tms.tm_hour = 23;
	tms.tm_min = 59;
	tms.tm_sec = 59;
	t = timegm(&tms);
	ATF_REQUIRE_ERRNO(0, t == (time_t)-1);

	/*
	 * Another way of getting one second before the epoch:
	 * Set date to 1 Jan 1970, and time to -1 second.
	 */
	(void)memset(&tms, 0, sizeof(tms));
	errno = 0;
	tms.tm_year = 1970 - 1900;
	tms.tm_mday = 1;
	tms.tm_sec = -1;
	t = timegm(&tms);
	ATF_REQUIRE_ERRNO(0, t == (time_t)-1);

	/*
	 * Two seconds before the epoch.
	 */
	(void)memset(&tms, 0, sizeof(tms));
	errno = 0;
	tms.tm_year = 1970 - 1900;
	tms.tm_mday = 1;
	tms.tm_sec = -2;
	t = timegm(&tms);
	ATF_REQUIRE_ERRNO(0, t == (time_t)-2);

}


ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, localtime_r_gmt);
	ATF_TP_ADD_TC(tp, mktime_negyear);
	ATF_TP_ADD_TC(tp, timegm_epoch);

	return atf_no_error();
}
