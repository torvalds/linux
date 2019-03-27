/* $NetBSD: t_parsedate.c,v 1.25 2016/06/22 15:01:38 kre Exp $ */
/*-
 * Copyright (c) 2010, 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_parsedate.c,v 1.25 2016/06/22 15:01:38 kre Exp $");

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <util.h>

/*
 * ANY is used as a placeholder for values that do not need to be
 * checked.  The actual value is arbitrary.  We don't use -1
 * because some tests might want to use -1 as a literal value.
 */
#define ANY -30215

/* parsecheck --
 * call parsedate(), then call time_to_tm() on the result,
 * and check that year/month/day/hour/minute/second are as expected.
 *
 * time_to_tm should usually be localtime_r or gmtime_r.
 *
 * Don't check values specified as ANY.
 */
static void
parsecheck(const char *datestr, const time_t *reftime, const int *zoff,
	struct tm * time_to_tm(const time_t *, struct tm *),
	int year, int month, int day, int hour, int minute, int second)
{
	time_t t;
	struct tm tm;
	char argstr[128];

	/*
	 * printable version of the args.
	 *
	 * Note that printf("%.*d", 0, 0)) prints nothing at all,
	 * while printf("%.*d", 1, val) prints the value as usual.
	 */
	snprintf(argstr, sizeof(argstr), "%s%s%s, %s%.*jd, %s%.*d",
		/* NULL or \"<datestr>\" */
		(datestr ? "\"" : ""),
		(datestr ? datestr : "NULL"),
		(datestr ? "\"" : ""),
		/* NULL or *reftime */
		(reftime ? "" : "NULL"),
		(reftime ? 1 : 0), 
		(reftime ? (intmax_t)*reftime : (intmax_t)0), 
		/* NULL or *zoff */
		(zoff ? "" : "NULL"),
		(zoff ? 1 : 0), 
		(zoff ? *zoff : 0));

	ATF_CHECK_MSG((t = parsedate(datestr, reftime, zoff)) != -1,
	    "parsedate(%s) returned -1\n", argstr);
	ATF_CHECK(time_to_tm(&t, &tm) != NULL);
	if (year != ANY)
		ATF_CHECK_MSG(tm.tm_year + 1900 == year,
		    "parsedate(%s) expected year %d got %d (+1900)\n",
		    argstr, year, (int)tm.tm_year);
	if (month != ANY)
		ATF_CHECK_MSG(tm.tm_mon + 1 == month,
		    "parsedate(%s) expected month %d got %d (+1)\n",
		    argstr, month, (int)tm.tm_mon);
	if (day != ANY)
		ATF_CHECK_MSG(tm.tm_mday == day,
		    "parsedate(%s) expected day %d got %d\n",
		    argstr, day, (int)tm.tm_mday);
	if (hour != ANY)
		ATF_CHECK_MSG(tm.tm_hour == hour,
		    "parsedate(%s) expected hour %d got %d\n",
		    argstr, hour, (int)tm.tm_hour);
	if (minute != ANY)
		ATF_CHECK_MSG(tm.tm_min == minute,
		    "parsedate(%s) expected minute %d got %d\n",
		    argstr, minute, (int)tm.tm_min);
	if (second != ANY)
		ATF_CHECK_MSG(tm.tm_sec == second,
		    "parsedate(%s) expected second %d got %d\n",
		    argstr, second, (int)tm.tm_sec);
}

ATF_TC(dates);

ATF_TC_HEAD(dates, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test unambiguous dates"
	    " (PR lib/44255)");
}

ATF_TC_BODY(dates, tc)
{

	parsecheck("9/10/69", NULL, NULL, localtime_r,
		2069, 9, 10, 0, 0, 0); /* year < 70: add 2000 */
	parsecheck("9/10/70", NULL, NULL, localtime_r,
		1970, 9, 10, 0, 0, 0); /* 70 <= year < 100: add 1900 */
	parsecheck("69-09-10", NULL, NULL, localtime_r,
		69, 9, 10, 0, 0, 0); /* ISO8601 year remains unchanged */
	parsecheck("70-09-10", NULL, NULL, localtime_r,
		70, 9, 10, 0, 0, 0); /* ISO8601 year remains unchanged */
	parsecheck("2006-11-17", NULL, NULL, localtime_r,
		2006, 11, 17, 0, 0, 0);
	parsecheck("10/1/2000", NULL, NULL, localtime_r,
		2000, 10, 1, 0, 0, 0); /* month/day/year */
	parsecheck("20 Jun 1994", NULL, NULL, localtime_r,
		1994, 6, 20, 0, 0, 0);
	parsecheck("97 September 2", NULL, NULL, localtime_r,
		1997, 9, 2, 0, 0, 0);
	parsecheck("23jun2001", NULL, NULL, localtime_r,
		2001, 6, 23, 0, 0, 0);
	parsecheck("1-sep-06", NULL, NULL, localtime_r,
		2006, 9, 1, 0, 0, 0);
	parsecheck("1/11", NULL, NULL, localtime_r,
		ANY, 1, 11, 0, 0, 0); /* month/day */
	parsecheck("1500-01-02", NULL, NULL, localtime_r,
		1500, 1, 2, 0, 0, 0);
	parsecheck("9999-12-21", NULL, NULL, localtime_r,
		9999, 12, 21, 0, 0, 0);
	parsecheck("2015.12.07.08.07.35", NULL, NULL, localtime_r,
		2015, 12, 7, 8, 7, 35);
}

ATF_TC(times);

ATF_TC_HEAD(times, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test times"
	    " (PR lib/44255)");
}

ATF_TC_BODY(times, tc)
{

	parsecheck("10:01", NULL, NULL, localtime_r,
		ANY, ANY, ANY, 10, 1, 0);
	parsecheck("10:12pm", NULL, NULL, localtime_r,
		ANY, ANY, ANY, 22, 12, 0);
	parsecheck("12:11:01.000012", NULL, NULL, localtime_r,
		ANY, ANY, ANY, 12, 11, 1);
	parsecheck("12:21-0500", NULL, NULL, gmtime_r,
		ANY, ANY, ANY, 12+5, 21, 0);
	/* numeric zones not permitted with am/pm ... */
	parsecheck("7 a.m. ICT", NULL, NULL, gmtime_r,
		ANY, ANY, ANY, 7-7, 0, 0);
	parsecheck("midnight", NULL, NULL, localtime_r,
		ANY, ANY, ANY, 0, 0, 0);
	parsecheck("mn", NULL, NULL, localtime_r,
		ANY, ANY, ANY, 0, 0, 0);
	parsecheck("noon", NULL, NULL, localtime_r,
		ANY, ANY, ANY, 12, 0, 0);
}

ATF_TC(dsttimes);

ATF_TC_HEAD(dsttimes, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test DST transition times"
	    " (PR lib/47916)");
}

ATF_TC_BODY(dsttimes, tc)
{
	struct tm tm;
	time_t t;
	int tzoff;

	putenv(__UNCONST("TZ=EST"));
	tzset();
	parsecheck("12:0", NULL, NULL, localtime_r,
		ANY, ANY, ANY, 12, 0, 0);

	putenv(__UNCONST("TZ=Asia/Tokyo"));
	tzset();
	parsecheck("12:0", NULL, NULL, localtime_r,
		ANY, ANY, ANY, 12, 0, 0);

	/*
	 * When the effective local time is Tue Jul  9 13:21:53 BST 2013,
	 * check mktime("14:00")
	 */
	putenv(__UNCONST("TZ=Europe/London"));
	tzset();
	tm = (struct tm){
		.tm_year = 2013-1900, .tm_mon = 7-1, .tm_mday = 9,
		.tm_hour = 13, .tm_min = 21, .tm_sec = 53,
		.tm_isdst = 0 };
	t = mktime(&tm);
	ATF_CHECK(t != (time_t)-1);
	parsecheck("14:00", &t, NULL, localtime_r,
		2013, 7, 9, 14, 0, 0);
	tzoff = -60; /* British Summer Time */
	parsecheck("14:00", &t, &tzoff, localtime_r,
		2013, 7, 9, 14, 0, 0);
}

ATF_TC(relative);

ATF_TC_HEAD(relative, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test relative items"
	    " (PR lib/44255)");
}

ATF_TC_BODY(relative, tc)
{
	struct tm tm;
	time_t now;

#define REL_CHECK(s, now, tm) do {					\
	time_t p, q;							\
	char nb[30], pb[30], qb[30];					\
	p = parsedate(s, &now, NULL);					\
	q = mktime(&tm);						\
	ATF_CHECK_EQ_MSG(p, q,						\
	    "From %jd (%24.24s) using \"%s\", obtained %jd (%24.24s); expected %jd (%24.24s)", \
	    (uintmax_t)now, ctime_r(&now, nb),				\
	    s, (uintmax_t)p, ctime_r(&p, pb), (uintmax_t)q, 		\
	    ctime_r(&q, qb));						\
    } while (/*CONSTCOND*/0)

#define isleap(yr) (((yr) & 3) == 0 && (((yr) % 100) != 0 ||		\
			((1900+(yr)) % 400) == 0))

	ATF_CHECK(parsedate("-1 month", NULL, NULL) != -1);
	ATF_CHECK(parsedate("last friday", NULL, NULL) != -1);
	ATF_CHECK(parsedate("one week ago", NULL, NULL) != -1);
	ATF_CHECK(parsedate("this thursday", NULL, NULL) != -1);
	ATF_CHECK(parsedate("next sunday", NULL, NULL) != -1);
	ATF_CHECK(parsedate("+2 years", NULL, NULL) != -1);

	/*
	 * Test relative to a number of fixed dates.  Avoid the
	 * edges of the time_t range to avert under- or overflow
	 * of the relative date, and use a prime step for maximum
	 * coverage of different times of day/week/month/year.
	 */
	for (now = 0x00FFFFFF; now < 0xFF000000; now += 3777779) {
		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mday--;
		/* "yesterday" leaves time untouched */
		tm.tm_isdst = -1;
		REL_CHECK("yesterday", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mday++;
		/* as does "tomorrow" */
		tm.tm_isdst = -1;
		REL_CHECK("tomorrow", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		if (tm.tm_wday > 4)
			tm.tm_mday += 7;
		tm.tm_mday += 4 - tm.tm_wday;
		/* if a day name is mentioned, it means midnight (by default) */
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		tm.tm_isdst = -1;
		REL_CHECK("this thursday", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mday += 14 - (tm.tm_wday ? tm.tm_wday : 7);
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		tm.tm_isdst = -1;
		REL_CHECK("next sunday", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		if (tm.tm_wday <= 5)
			tm.tm_mday -= 7;
		tm.tm_mday += 5 - tm.tm_wday;
		tm.tm_sec = tm.tm_min = 0;
		tm.tm_hour = 16;
		tm.tm_isdst = -1;
		REL_CHECK("last friday 4 p.m.", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mday += 14;
		if (tm.tm_wday > 3)
			tm.tm_mday += 7;
		tm.tm_mday += 3 - tm.tm_wday;
		tm.tm_sec = tm.tm_min = 0;
		tm.tm_hour = 3;
		tm.tm_isdst = -1;
		REL_CHECK("we fortnight 3 a.m.", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_min -= 5;
		tm.tm_isdst = -1;
		REL_CHECK("5 minutes ago", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_hour++;
		tm.tm_min += 37;
		tm.tm_isdst = -1;
		REL_CHECK("97 minutes", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mon++;
		if (tm.tm_mon == 1 &&
		    tm.tm_mday > 28 + isleap(tm.tm_year))
			tm.tm_mday = 28 + isleap(tm.tm_year);
		else if ((tm.tm_mon == 3 || tm.tm_mon == 5 ||
		    tm.tm_mon == 8 || tm.tm_mon == 10) && tm.tm_mday == 31)
			tm.tm_mday = 30;
		tm.tm_isdst = -1;
		REL_CHECK("month", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mon += 2;		/* "next" means add 2 ... */
		if (tm.tm_mon == 13 &&
		    tm.tm_mday > 28 + isleap(tm.tm_year + 1))
			tm.tm_mday = 28 + isleap(tm.tm_year + 1);
		else if (tm.tm_mon == 8 && tm.tm_mday == 31)
			tm.tm_mday = 30;
		tm.tm_isdst = -1;
		REL_CHECK("next month", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mon--;
		if (tm.tm_mon == 1 &&
		    tm.tm_mday > 28 + isleap(tm.tm_year))
			tm.tm_mday = 28 + isleap(tm.tm_year);
		else if ((tm.tm_mon == 3 || tm.tm_mon == 5 ||
		    tm.tm_mon == 8 || tm.tm_mon == 10) && tm.tm_mday == 31)
			tm.tm_mday = 30;
		tm.tm_isdst = -1;
		REL_CHECK("last month", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mon += 6;
		if (tm.tm_mon == 13 &&
		    tm.tm_mday > 28 + isleap(tm.tm_year + 1))
			tm.tm_mday = 28 + isleap(tm.tm_year + 1);
		else if ((tm.tm_mon == 15 || tm.tm_mon == 17 ||
		    tm.tm_mon == 8 || tm.tm_mon == 10) && tm.tm_mday == 31)
			tm.tm_mday = 30;
		tm.tm_mday += 2;
		tm.tm_isdst = -1;
		REL_CHECK("+6 months 2 days", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_mon -= 9;
		if (tm.tm_mon == 1 && tm.tm_mday > 28 + isleap(tm.tm_year))
			tm.tm_mday = 28 + isleap(tm.tm_year);
		else if ((tm.tm_mon == -9 || tm.tm_mon == -7 ||
		    tm.tm_mon == -2) && tm.tm_mday == 31)
			tm.tm_mday = 30;
		tm.tm_isdst = -1;
		REL_CHECK("9 months ago", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		if (tm.tm_wday <= 2)
			tm.tm_mday -= 7;
		tm.tm_mday += 2 - tm.tm_wday;
		tm.tm_isdst = -1;
		tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
		REL_CHECK("1 week ago Tu", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_isdst = -1;
		tm.tm_mday++;
		tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
		REL_CHECK("midnight tomorrow", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_isdst = -1;
		tm.tm_mday++;
		tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
		REL_CHECK("tomorrow midnight", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		tm.tm_isdst = -1;
		tm.tm_mday++;
		tm.tm_hour = 12;
		tm.tm_min = tm.tm_sec = 0;
		REL_CHECK("noon tomorrow", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		if (tm.tm_wday > 2)
			tm.tm_mday += 7;
		tm.tm_mday += 2 - tm.tm_wday;
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		tm.tm_isdst = -1;
		REL_CHECK("midnight Tuesday", now, tm);

		ATF_CHECK(localtime_r(&now, &tm) != NULL);
		if (tm.tm_wday > 2 + 1)
			tm.tm_mday += 7;
		tm.tm_mday += 2 - tm.tm_wday;
		tm.tm_mday++;	/* xxx midnight --> the next day */
		tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
		tm.tm_isdst = -1;
		REL_CHECK("Tuesday midnight", now, tm);
	}
}

ATF_TC(atsecs);

ATF_TC_HEAD(atsecs, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test seconds past the epoch");
}

ATF_TC_BODY(atsecs, tc)
{
	int tzoff;

	/* "@0" -> (time_t)0, regardless of timezone */
	ATF_CHECK(parsedate("@0", NULL, NULL) == (time_t)0);
	putenv(__UNCONST("TZ=Europe/Berlin"));
	tzset();
	ATF_CHECK(parsedate("@0", NULL, NULL) == (time_t)0);
	putenv(__UNCONST("TZ=America/New_York"));
	tzset();
	ATF_CHECK(parsedate("@0", NULL, NULL) == (time_t)0);
	tzoff = 0;
	ATF_CHECK(parsedate("@0", NULL, &tzoff) == (time_t)0);
	tzoff = 3600;
	ATF_CHECK(parsedate("@0", NULL, &tzoff) == (time_t)0);
	tzoff = -3600;
	ATF_CHECK(parsedate("@0", NULL, &tzoff) == (time_t)0);

	/* -1 or other negative numbers are not errors */
	errno = 0;
	ATF_CHECK(parsedate("@-1", NULL, &tzoff) == (time_t)-1 && errno == 0);
	ATF_CHECK(parsedate("@-2", NULL, &tzoff) == (time_t)-2 && errno == 0);

	/* junk is an error */
	errno = 0;
	ATF_CHECK(parsedate("@junk", NULL, NULL) == (time_t)-1 && errno != 0);
}

ATF_TC(zones);

ATF_TC_HEAD(zones, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test parsing dates with zones");
}

ATF_TC_BODY(zones, tc)
{
	parsecheck("2015-12-06 16:11:48 UTC", NULL, NULL, gmtime_r,
		2015, 12, 6, 16, 11, 48);
	parsecheck("2015-12-06 16:11:48 UT", NULL, NULL, gmtime_r,
		2015, 12, 6, 16, 11, 48);
	parsecheck("2015-12-06 16:11:48 GMT", NULL, NULL, gmtime_r,
		2015, 12, 6, 16, 11, 48);
	parsecheck("2015-12-06 16:11:48 +0000", NULL, NULL, gmtime_r,
		2015, 12, 6, 16, 11, 48);

	parsecheck("2015-12-06 16:11:48 -0500", NULL, NULL, gmtime_r,
		2015, 12, 6, 21, 11, 48);
	parsecheck("2015-12-06 16:11:48 EST", NULL, NULL, gmtime_r,
		2015, 12, 6, 21, 11, 48);
	parsecheck("2015-12-06 16:11:48 EDT", NULL, NULL, gmtime_r,
		2015, 12, 6, 20, 11, 48);
	parsecheck("2015-12-06 16:11:48 +0500", NULL, NULL, gmtime_r,
		2015, 12, 6, 11, 11, 48);

	parsecheck("2015-12-06 16:11:48 +1000", NULL, NULL, gmtime_r,
		2015, 12, 6, 6, 11, 48);
	parsecheck("2015-12-06 16:11:48 AEST", NULL, NULL, gmtime_r,
		2015, 12, 6, 6, 11, 48);
	parsecheck("2015-12-06 16:11:48 -1000", NULL, NULL, gmtime_r,
		2015, 12, 7, 2, 11, 48);
	parsecheck("2015-12-06 16:11:48 HST", NULL, NULL, gmtime_r,
		2015, 12, 7, 2, 11, 48);

	parsecheck("2015-12-06 16:11:48 AWST", NULL, NULL, gmtime_r,
		2015, 12, 6, 8, 11, 48);
	parsecheck("2015-12-06 16:11:48 NZDT", NULL, NULL, gmtime_r,
		2015, 12, 6, 3, 11, 48);

        parsecheck("Sun, 6 Dec 2015 09:43:16 -0500", NULL, NULL, gmtime_r,
		2015, 12, 6, 14, 43, 16);
	parsecheck("Mon Dec  7 03:13:31 ICT 2015", NULL, NULL, gmtime_r,
		2015, 12, 6, 20, 13, 31);
	/* the day name is ignored when a day of month (etc) is given... */
	parsecheck("Sat Dec  7 03:13:31 ICT 2015", NULL, NULL, gmtime_r,
		2015, 12, 6, 20, 13, 31);


	parsecheck("2015-12-06 12:00:00 IDLW", NULL, NULL, gmtime_r,
		2015, 12, 7, 0, 0, 0);
	parsecheck("2015-12-06 12:00:00 IDLE", NULL, NULL, gmtime_r,
		2015, 12, 6, 0, 0, 0);

	parsecheck("2015-12-06 21:17:33 NFT", NULL, NULL, gmtime_r,
		2015, 12, 7, 0, 47, 33);
	parsecheck("2015-12-06 21:17:33 ACST", NULL, NULL, gmtime_r,
		2015, 12, 6, 11, 47, 33);
	parsecheck("2015-12-06 21:17:33 +0717", NULL, NULL, gmtime_r,
		2015, 12, 6, 14, 0, 33);

	parsecheck("2015-12-06 21:21:21 Z", NULL, NULL, gmtime_r,
		2015, 12, 6, 21, 21, 21);
	parsecheck("2015-12-06 21:21:21 A", NULL, NULL, gmtime_r,
		2015, 12, 6, 22, 21, 21);
	parsecheck("2015-12-06 21:21:21 G", NULL, NULL, gmtime_r,
		2015, 12, 7, 4, 21, 21);
	parsecheck("2015-12-06 21:21:21 M", NULL, NULL, gmtime_r,
		2015, 12, 7, 9, 21, 21);
	parsecheck("2015-12-06 21:21:21 N", NULL, NULL, gmtime_r,
		2015, 12, 6, 20, 21, 21);
	parsecheck("2015-12-06 21:21:21 T", NULL, NULL, gmtime_r,
		2015, 12, 6, 14, 21, 21);
	parsecheck("2015-12-06 21:21:21 Y", NULL, NULL, gmtime_r,
		2015, 12, 6, 9, 21, 21);

}

ATF_TC(gibberish);

ATF_TC_HEAD(gibberish, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test (not) parsing nonsense");
}

ATF_TC_BODY(gibberish, tc)
{
	errno = 0;
	ATF_CHECK(parsedate("invalid nonsense", NULL, NULL) == (time_t)-1
	    && errno != 0);
	errno = 0;
	ATF_CHECK(parsedate("12th day of Christmas", NULL, NULL) == (time_t)-1
	    && errno != 0);
	errno = 0;
	ATF_CHECK(parsedate("2015-31-07 15:00", NULL, NULL) == (time_t)-1
	    && errno != 0);
	errno = 0;
	ATF_CHECK(parsedate("2015-02-29 10:01", NULL, NULL) == (time_t)-1
	    && errno != 0);
	errno = 0;
	ATF_CHECK(parsedate("2015-12-06 24:01", NULL, NULL) == (time_t)-1
	    && errno != 0);
	errno = 0;
	ATF_CHECK(parsedate("2015-12-06 14:61", NULL, NULL) == (time_t)-1
	    && errno != 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, dates);
	ATF_TP_ADD_TC(tp, times);
	ATF_TP_ADD_TC(tp, dsttimes);
	ATF_TP_ADD_TC(tp, relative);
	ATF_TP_ADD_TC(tp, atsecs);
	ATF_TP_ADD_TC(tp, zones);
	ATF_TP_ADD_TC(tp, gibberish);

	return atf_no_error();
}

