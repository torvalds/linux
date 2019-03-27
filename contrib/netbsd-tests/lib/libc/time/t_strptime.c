/* $NetBSD: t_strptime.c,v 1.12 2015/10/31 02:25:11 christos Exp $ */

/*-
 * Copyright (c) 1998, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David Laight.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_strptime.c,v 1.12 2015/10/31 02:25:11 christos Exp $");

#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include <atf-c.h>

static void
h_pass(const char *buf, const char *fmt, int len,
    int tm_sec, int tm_min, int tm_hour, int tm_mday,
    int tm_mon, int tm_year, int tm_wday, int tm_yday)
{
	struct tm tm = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, NULL };
	const char *ret, *exp;

	exp = buf + len;
	ret = strptime(buf, fmt, &tm);

#ifdef __FreeBSD__
	ATF_CHECK_MSG(ret == exp,
	    "strptime(\"%s\", \"%s\", tm): incorrect return code: "
	    "expected: %p, got: %p", buf, fmt, exp, ret);

#define H_REQUIRE_FIELD(field)						\
		ATF_CHECK_MSG(tm.field == field,			\
		    "strptime(\"%s\", \"%s\", tm): incorrect %s: "	\
		    "expected: %d, but got: %d", buf, fmt,		\
		    ___STRING(field), field, tm.field)
#else
	ATF_REQUIRE_MSG(ret == exp,
	    "strptime(\"%s\", \"%s\", tm): incorrect return code: "
	    "expected: %p, got: %p", buf, fmt, exp, ret);

#define H_REQUIRE_FIELD(field)						\
		ATF_REQUIRE_MSG(tm.field == field,			\
		    "strptime(\"%s\", \"%s\", tm): incorrect %s: "	\
		    "expected: %d, but got: %d", buf, fmt,		\
		    ___STRING(field), field, tm.field)
#endif

	H_REQUIRE_FIELD(tm_sec);
	H_REQUIRE_FIELD(tm_min);
	H_REQUIRE_FIELD(tm_hour);
	H_REQUIRE_FIELD(tm_mday);
	H_REQUIRE_FIELD(tm_mon);
	H_REQUIRE_FIELD(tm_year);
	H_REQUIRE_FIELD(tm_wday);
	H_REQUIRE_FIELD(tm_yday);

#undef H_REQUIRE_FIELD
}

static void
h_fail(const char *buf, const char *fmt)
{
	struct tm tm = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, NULL };

#ifdef __FreeBSD__
	ATF_CHECK_MSG(strptime(buf, fmt, &tm) == NULL, "strptime(\"%s\", "
	    "\"%s\", &tm) should fail, but it didn't", buf, fmt);
#else
	ATF_REQUIRE_MSG(strptime(buf, fmt, &tm) == NULL, "strptime(\"%s\", "
	    "\"%s\", &tm) should fail, but it didn't", buf, fmt);
#endif
}

static struct {
	const char *name;
	long offs;
} zt[] = {
#ifndef __FreeBSD__
	{ "Z",				0 },
	{ "UT",				0 },
	{ "UTC",			0 },
	{ "GMT",			0 },
	{ "EST",			-18000 },
	{ "EDT",			-14400 },
	{ "CST",			-21600 },
	{ "CDT",			-18000 },
	{ "MST",			-25200 },
	{ "MDT",			-21600 },
	{ "PST",			-28800 },
	{ "PDT",			-25200 },

	{ "VST",			-1 },
	{ "VDT",			-1 },

	{ "+03",			10800 },
	{ "-03",			-10800 },
	{ "+0403",			14580 },
	{ "-0403",			-14580 },
	{ "+04:03",			14580 },
	{ "-04:03",			-14580 },
	{ "+14:00",			50400 },
	{ "-14:00",			-50400 },
	{ "+23:59",			86340 },
	{ "-23:59",			-86340 },

	{ "1",				-1 },
	{ "03",				-1 },
	{ "0304",			-1 },
	{ "+1",				-1 },
	{ "-203",			-1 },
	{ "+12345",			-1 },
	{ "+12:345",			-1 },
	{ "+123:45",			-1 },
	{ "+2400",			-1 },
	{ "-2400",			-1 },
	{ "+1060",			-1 },
	{ "-1060",			-1 },

	{ "A",				-3600 },
	{ "B",				-7200 },
	{ "C",				-10800 },
	{ "D",				-14400 },
	{ "E",				-18000 },
	{ "F",				-21600 },
	{ "G",				-25200 },
	{ "H",				-28800 },
	{ "I",				-32400 },
	{ "L",				-39600 },
	{ "M",				-43200 },
	{ "N",				3600 },
	{ "O",				7200 },
	{ "P",				10800 },
	{ "Q",				14400 },
	{ "R",				18000 },
	{ "T",				25200 },
	{ "U",				28800 },
	{ "V",				32400 },
	{ "W",				36000 },
	{ "X",				39600 },
	{ "Y",				43200 },

	{ "J",				-2 },

	{ "America/Los_Angeles",	-28800 },
	{ "America/New_York",		-18000 },
	{ "EST4EDT",			-14400 },

	{ "Bogus",			-1 },
#endif
};

static void
ztest1(const char *name, const char *fmt, long value)
{
	struct tm tm;
	char *rv;

	memset(&tm, 0, sizeof(tm));
	if ((rv = strptime(name, fmt, &tm)) == NULL) 
		tm.tm_gmtoff = -1;
	else if (rv == name && fmt[1] == 'Z')
		value = 0;

	switch (value) {
#ifndef __FreeBSD__
	case -2:
		value = -timezone;
		break;
#endif
	case -1:
		if (fmt[1] == 'Z')
			value = 0;
		break;
	default:
		break;
	}

	ATF_REQUIRE_MSG(tm.tm_gmtoff == value,
	    "strptime(\"%s\", \"%s\", &tm): "
	    "expected: tm.tm_gmtoff=%ld, got: tm.tm_gmtoff=%ld",
	    name, fmt, value, tm.tm_gmtoff);
	printf("%s %s %ld\n", name, fmt, tm.tm_gmtoff);
}

static void
ztest(const char *fmt)
{
	setenv("TZ", "US/Eastern", 1);
#ifndef __FreeBSD__
	ztest1("GMT", fmt, 0);
	ztest1("UTC", fmt, 0);
	ztest1("US/Eastern", fmt, -18000);
#endif
	for (size_t i = 0; i < __arraycount(zt); i++)
		ztest1(zt[i].name, fmt, zt[i].offs);
}

ATF_TC(common);

ATF_TC_HEAD(common, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks strptime(3): various checks");
}

ATF_TC_BODY(common, tc)
{

	h_pass("Tue Jan 20 23:27:46 1998", "%a %b %d %T %Y",
		24, 46, 27, 23, 20, 0, 98, 2, 19);
	h_pass("Tue Jan 20 23:27:46 1998", "%a %b %d %H:%M:%S %Y",
		24, 46, 27, 23, 20, 0, 98, 2, 19);
	h_pass("Tue Jan 20 23:27:46 1998", "%c",
		24, 46, 27, 23, 20, 0, 98, 2, 19);
	h_pass("Fri Mar  4 20:05:34 2005", "%a %b %e %H:%M:%S %Y",
		24, 34, 5, 20, 4, 2, 105, 5, 62);
	h_pass("5\t3  4 8pm:05:34 2005", "%w%n%m%t%d%n%k%p:%M:%S %Y",
		21, 34, 5, 20, 4, 2, 105, 5, 62);
	h_pass("Fri Mar  4 20:05:34 2005", "%c",
		24, 34, 5, 20, 4, 2, 105, 5, 62);
}

ATF_TC(day);

ATF_TC_HEAD(day, tc)
{

	atf_tc_set_md_var(tc, "descr",
			  "Checks strptime(3) day name conversions [aA]");
}

ATF_TC_BODY(day, tc)
{

	h_pass("Sun", "%a", 3, -1, -1, -1, -1, -1, -1, 0, -1);
	h_pass("Sunday", "%a", 6, -1, -1, -1, -1, -1, -1, 0, -1);
	h_pass("Mon", "%a", 3, -1, -1, -1, -1, -1, -1, 1, -1);
	h_pass("Monday", "%a", 6, -1, -1, -1, -1, -1, -1, 1, -1);
	h_pass("Tue", "%a", 3, -1, -1, -1, -1, -1, -1, 2, -1);
	h_pass("Tuesday", "%a", 7, -1, -1, -1, -1, -1, -1, 2, -1);
	h_pass("Wed", "%a", 3, -1, -1, -1, -1, -1, -1, 3, -1);
	h_pass("Wednesday", "%a", 9, -1, -1, -1, -1, -1, -1, 3, -1);
	h_pass("Thu", "%a", 3, -1, -1, -1, -1, -1, -1, 4, -1);
	h_pass("Thursday", "%a", 8, -1, -1, -1, -1, -1, -1, 4, -1);
	h_pass("Fri", "%a", 3, -1, -1, -1, -1, -1, -1, 5, -1);
	h_pass("Friday", "%a", 6, -1, -1, -1, -1, -1, -1, 5, -1);
	h_pass("Sat", "%a", 3, -1, -1, -1, -1, -1, -1, 6, -1);
	h_pass("Saturday", "%a", 8, -1, -1, -1, -1, -1, -1, 6, -1);
	h_pass("Saturn", "%a", 3, -1, -1, -1, -1, -1, -1, 6, -1);
	h_fail("Moon", "%a");
	h_pass("Sun", "%A", 3, -1, -1, -1, -1, -1, -1, 0, -1);
	h_pass("Sunday", "%A", 6, -1, -1, -1, -1, -1, -1, 0, -1);
	h_pass("Mon", "%A", 3, -1, -1, -1, -1, -1, -1, 1, -1);
	h_pass("Monday", "%A", 6, -1, -1, -1, -1, -1, -1, 1, -1);
	h_pass("Tue", "%A", 3, -1, -1, -1, -1, -1, -1, 2, -1);
	h_pass("Tuesday", "%A", 7, -1, -1, -1, -1, -1, -1, 2, -1);
	h_pass("Wed", "%A", 3, -1, -1, -1, -1, -1, -1, 3, -1);
	h_pass("Wednesday", "%A", 9, -1, -1, -1, -1, -1, -1, 3, -1);
	h_pass("Thu", "%A", 3, -1, -1, -1, -1, -1, -1, 4, -1);
	h_pass("Thursday", "%A", 8, -1, -1, -1, -1, -1, -1, 4, -1);
	h_pass("Fri", "%A", 3, -1, -1, -1, -1, -1, -1, 5, -1);
	h_pass("Friday", "%A", 6, -1, -1, -1, -1, -1, -1, 5, -1);
	h_pass("Sat", "%A", 3, -1, -1, -1, -1, -1, -1, 6, -1);
	h_pass("Saturday", "%A", 8, -1, -1, -1, -1, -1, -1, 6, -1);
	h_pass("Saturn", "%A", 3, -1, -1, -1, -1, -1, -1, 6, -1);
	h_fail("Moon", "%A");

	h_pass("mon", "%a", 3, -1, -1, -1, -1, -1, -1, 1, -1);
	h_pass("tueSDay", "%A", 7, -1, -1, -1, -1, -1, -1, 2, -1);
	h_pass("sunday", "%A", 6, -1, -1, -1, -1, -1, -1, 0, -1);
#ifdef __NetBSD__
	h_fail("sunday", "%EA");
#else
	h_pass("Sunday", "%EA", 6, -1, -1, -1, -1, -1, -1, 0, -1);
#endif
	h_pass("SaturDay", "%A", 8, -1, -1, -1, -1, -1, -1, 6, -1);
#ifdef __NetBSD__
	h_fail("SaturDay", "%OA");
#else
	h_pass("SaturDay", "%OA", 8, -1, -1, -1, -1, -1, -1, 6, -1);
#endif

#ifdef __FreeBSD__
	h_fail("00", "%d");
#endif
}

ATF_TC(hour);

ATF_TC_HEAD(hour, tc)
{

	atf_tc_set_md_var(tc, "descr",
#ifdef __FreeBSD__
			  "Checks strptime(3) hour conversions [HIkl]");
#else
			  "Checks strptime(3) hour conversions [IH]");
#endif
}

ATF_TC_BODY(hour, tc)
{

	h_fail("00", "%I");
	h_fail("13", "%I");

#ifdef __FreeBSD__
	h_pass("0", "%k", 1, -1, -1, 0, -1, -1, -1, -1, -1);
	h_pass("04", "%k", 2, -1, -1, 4, -1, -1, -1, -1, -1);
	h_pass(" 8", "%k", 2, -1, -1, 8, -1, -1, -1, -1, -1);
	h_pass("23", "%k", 2, -1, -1, 23, -1, -1, -1, -1, -1);
	h_fail("24", "%k");

	h_fail("0", "%l");
	h_pass("1", "%l", 1, -1, -1, 1, -1, -1, -1, -1, -1);
	h_pass("05", "%l", 2, -1, -1, 5, -1, -1, -1, -1, -1);
	h_pass(" 9", "%l", 2, -1, -1, 9, -1, -1, -1, -1, -1);
	h_pass("12", "%l", 2, -1, -1, 12, -1, -1, -1, -1, -1);
	h_fail("13", "%l");
#endif

	h_pass("00", "%H", 2, -1, -1, 0, -1, -1, -1, -1, -1);
	h_pass("12", "%H", 2, -1, -1, 12, -1, -1, -1, -1, -1);
	h_pass("23", "%H", 2, -1, -1, 23, -1, -1, -1, -1, -1);
	h_fail("24", "%H");
}


ATF_TC(month);

ATF_TC_HEAD(month, tc)
{

	atf_tc_set_md_var(tc, "descr",
			  "Checks strptime(3) month name conversions [bB]");
}

ATF_TC_BODY(month, tc)
{

	h_pass("Jan", "%b", 3, -1, -1, -1, -1, 0, -1, -1, -1);
	h_pass("January", "%b", 7, -1, -1, -1, -1, 0, -1, -1, -1);
	h_pass("Feb", "%b", 3, -1, -1, -1, -1, 1, -1, -1, -1);
	h_pass("February", "%b", 8, -1, -1, -1, -1, 1, -1, -1, -1);
	h_pass("Mar", "%b", 3, -1, -1, -1, -1, 2, -1, -1, -1);
	h_pass("March", "%b", 5, -1, -1, -1, -1, 2, -1, -1, -1);
	h_pass("Apr", "%b", 3, -1, -1, -1, -1, 3, -1, -1, -1);
	h_pass("April", "%b", 5, -1, -1, -1, -1, 3, -1, -1, -1);
	h_pass("May", "%b", 3, -1, -1, -1, -1, 4, -1, -1, -1);
	h_pass("Jun", "%b", 3, -1, -1, -1, -1, 5, -1, -1, -1);
	h_pass("June", "%b", 4, -1, -1, -1, -1, 5, -1, -1, -1);
	h_pass("Jul", "%b", 3, -1, -1, -1, -1, 6, -1, -1, -1);
	h_pass("July", "%b", 4, -1, -1, -1, -1, 6, -1, -1, -1);
	h_pass("Aug", "%b", 3, -1, -1, -1, -1, 7, -1, -1, -1);
	h_pass("August", "%b", 6, -1, -1, -1, -1, 7, -1, -1, -1);
	h_pass("Sep", "%b", 3, -1, -1, -1, -1, 8, -1, -1, -1);
	h_pass("September", "%b", 9, -1, -1, -1, -1, 8, -1, -1, -1);
	h_pass("Oct", "%b", 3, -1, -1, -1, -1, 9, -1, -1, -1);
	h_pass("October", "%b", 7, -1, -1, -1, -1, 9, -1, -1, -1);
	h_pass("Nov", "%b", 3, -1, -1, -1, -1, 10, -1, -1, -1);
	h_pass("November", "%b", 8, -1, -1, -1, -1, 10, -1, -1, -1);
	h_pass("Dec", "%b", 3, -1, -1, -1, -1, 11, -1, -1, -1);
	h_pass("December", "%b", 8, -1, -1, -1, -1, 11, -1, -1, -1);
	h_pass("Mayor", "%b", 3, -1, -1, -1, -1, 4, -1, -1, -1);
	h_pass("Mars", "%b", 3, -1, -1, -1, -1, 2, -1, -1, -1);
	h_fail("Rover", "%b");
	h_pass("Jan", "%B", 3, -1, -1, -1, -1, 0, -1, -1, -1);
	h_pass("January", "%B", 7, -1, -1, -1, -1, 0, -1, -1, -1);
	h_pass("Feb", "%B", 3, -1, -1, -1, -1, 1, -1, -1, -1);
	h_pass("February", "%B", 8, -1, -1, -1, -1, 1, -1, -1, -1);
	h_pass("Mar", "%B", 3, -1, -1, -1, -1, 2, -1, -1, -1);
	h_pass("March", "%B", 5, -1, -1, -1, -1, 2, -1, -1, -1);
	h_pass("Apr", "%B", 3, -1, -1, -1, -1, 3, -1, -1, -1);
	h_pass("April", "%B", 5, -1, -1, -1, -1, 3, -1, -1, -1);
	h_pass("May", "%B", 3, -1, -1, -1, -1, 4, -1, -1, -1);
	h_pass("Jun", "%B", 3, -1, -1, -1, -1, 5, -1, -1, -1);
	h_pass("June", "%B", 4, -1, -1, -1, -1, 5, -1, -1, -1);
	h_pass("Jul", "%B", 3, -1, -1, -1, -1, 6, -1, -1, -1);
	h_pass("July", "%B", 4, -1, -1, -1, -1, 6, -1, -1, -1);
	h_pass("Aug", "%B", 3, -1, -1, -1, -1, 7, -1, -1, -1);
	h_pass("August", "%B", 6, -1, -1, -1, -1, 7, -1, -1, -1);
	h_pass("Sep", "%B", 3, -1, -1, -1, -1, 8, -1, -1, -1);
	h_pass("September", "%B", 9, -1, -1, -1, -1, 8, -1, -1, -1);
	h_pass("Oct", "%B", 3, -1, -1, -1, -1, 9, -1, -1, -1);
	h_pass("October", "%B", 7, -1, -1, -1, -1, 9, -1, -1, -1);
	h_pass("Nov", "%B", 3, -1, -1, -1, -1, 10, -1, -1, -1);
	h_pass("November", "%B", 8, -1, -1, -1, -1, 10, -1, -1, -1);
	h_pass("Dec", "%B", 3, -1, -1, -1, -1, 11, -1, -1, -1);
	h_pass("December", "%B", 8, -1, -1, -1, -1, 11, -1, -1, -1);
	h_pass("Mayor", "%B", 3, -1, -1, -1, -1, 4, -1, -1, -1);
	h_pass("Mars", "%B", 3, -1, -1, -1, -1, 2, -1, -1, -1);
	h_fail("Rover", "%B");

	h_pass("september", "%b", 9, -1, -1, -1, -1, 8, -1, -1, -1);
	h_pass("septembe", "%B", 3, -1, -1, -1, -1, 8, -1, -1, -1);
}

ATF_TC(seconds);

ATF_TC_HEAD(seconds, tc)
{

	atf_tc_set_md_var(tc, "descr",
			  "Checks strptime(3) seconds conversions [S]");
}

ATF_TC_BODY(seconds, tc)
{

	h_pass("0", "%S", 1, 0, -1, -1, -1, -1, -1, -1, -1);
	h_pass("59", "%S", 2, 59, -1, -1, -1, -1, -1, -1, -1);
	h_pass("60", "%S", 2, 60, -1, -1, -1, -1, -1, -1, -1);
#ifdef __FreeBSD__
	/*
	 * (Much) older versions of the standard (up to the Issue 6) allowed for
	 * [0;61] range in %S conversion for double-leap seconds, and it's
	 * apparently what NetBSD and glibc are expecting, however current
	 * version defines allowed values to be [0;60], and that is what our
	 * strptime() implementation expects.
	 */
	h_fail("61", "%S");
#else
	h_pass("61", "%S", 2, 61, -1, -1, -1, -1, -1, -1, -1);
#endif
	h_fail("62", "%S");
}

ATF_TC(year);

ATF_TC_HEAD(year, tc)
{

	atf_tc_set_md_var(tc, "descr",
			  "Checks strptime(3) century/year conversions [CyY]");
}

ATF_TC_BODY(year, tc)
{

	h_pass("x20y", "x%Cy", 4, -1, -1, -1, -1, -1, 100, -1, -1);
	h_pass("x84y", "x%yy", 4, -1, -1, -1, -1, -1, 84, -1, -1);
	h_pass("x2084y", "x%C%yy", 6, -1, -1, -1, -1, -1, 184, -1, -1);
	h_pass("x8420y", "x%y%Cy", 6, -1, -1, -1, -1, -1, 184, -1, -1);
	h_pass("%20845", "%%%C%y5", 6, -1, -1, -1, -1, -1, 184, -1, -1);
#ifndef __FreeBSD__
	h_fail("%", "%E%");
#endif

	h_pass("1980", "%Y", 4, -1, -1, -1, -1, -1, 80, -1, -1);
	h_pass("1980", "%EY", 4, -1, -1, -1, -1, -1, 80, -1, -1);
}

ATF_TC(zone);

ATF_TC_HEAD(zone, tc)
{

	atf_tc_set_md_var(tc, "descr",
			  "Checks strptime(3) timezone conversion [z]");
}


ATF_TC_BODY(zone, tc)
{
	ztest("%z");
}

ATF_TC(Zone);

ATF_TC_HEAD(Zone, tc)
{

	atf_tc_set_md_var(tc, "descr",
			  "Checks strptime(3) timezone conversion [Z]");
}


ATF_TC_BODY(Zone, tc)
{
	ztest("%Z");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, common);
	ATF_TP_ADD_TC(tp, day);
	ATF_TP_ADD_TC(tp, hour);
	ATF_TP_ADD_TC(tp, month);
	ATF_TP_ADD_TC(tp, seconds);
	ATF_TP_ADD_TC(tp, year);
	ATF_TP_ADD_TC(tp, zone);
	ATF_TP_ADD_TC(tp, Zone);

	return atf_no_error();
}
