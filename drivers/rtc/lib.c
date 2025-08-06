// SPDX-License-Identifier: GPL-2.0
/*
 * rtc and date/time utility functions
 *
 * Copyright (C) 2005-06 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on arch/arm/common/rtctime.c and other bits
 *
 * Author: Cassio Neri <cassio.neri@gmail.com> (rtc_time64_to_tm)
 */

#include <linux/export.h>
#include <linux/rtc.h>

static const unsigned char rtc_days_in_month[] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const unsigned short rtc_ydays[2][13] = {
	/* Normal years */
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	/* Leap years */
	{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

/*
 * The number of days in the month.
 */
int rtc_month_days(unsigned int month, unsigned int year)
{
	return rtc_days_in_month[month] + (is_leap_year(year) && month == 1);
}
EXPORT_SYMBOL(rtc_month_days);

/*
 * The number of days since January 1. (0 to 365)
 */
int rtc_year_days(unsigned int day, unsigned int month, unsigned int year)
{
	return rtc_ydays[is_leap_year(year)][month] + day - 1;
}
EXPORT_SYMBOL(rtc_year_days);

/**
 * rtc_time64_to_tm - converts time64_t to rtc_time.
 *
 * @time:	The number of seconds since 01-01-1970 00:00:00.
 *		Works for values since at least 1900
 * @tm:		Pointer to the struct rtc_time.
 */
void rtc_time64_to_tm(time64_t time, struct rtc_time *tm)
{
	int days, secs;

	u64 u64tmp;
	u32 u32tmp, udays, century, day_of_century, year_of_century, year,
		day_of_year, month, day;
	bool is_Jan_or_Feb, is_leap_year;

	/*
	 * Get days and seconds while preserving the sign to
	 * handle negative time values (dates before 1970-01-01)
	 */
	days = div_s64_rem(time, 86400, &secs);

	/*
	 * We need 0 <= secs < 86400 which isn't given for negative
	 * values of time. Fixup accordingly.
	 */
	if (secs < 0) {
		days -= 1;
		secs += 86400;
	}

	/* day of the week, 1970-01-01 was a Thursday */
	tm->tm_wday = (days + 4) % 7;
	/* Ensure tm_wday is always positive */
	if (tm->tm_wday < 0)
		tm->tm_wday += 7;

	/*
	 * The following algorithm is, basically, Proposition 6.3 of Neri
	 * and Schneider [1]. In a few words: it works on the computational
	 * (fictitious) calendar where the year starts in March, month = 2
	 * (*), and finishes in February, month = 13. This calendar is
	 * mathematically convenient because the day of the year does not
	 * depend on whether the year is leap or not. For instance:
	 *
	 * March 1st		0-th day of the year;
	 * ...
	 * April 1st		31-st day of the year;
	 * ...
	 * January 1st		306-th day of the year; (Important!)
	 * ...
	 * February 28th	364-th day of the year;
	 * February 29th	365-th day of the year (if it exists).
	 *
	 * After having worked out the date in the computational calendar
	 * (using just arithmetics) it's easy to convert it to the
	 * corresponding date in the Gregorian calendar.
	 *
	 * [1] "Euclidean Affine Functions and Applications to Calendar
	 * Algorithms". https://arxiv.org/abs/2102.06959
	 *
	 * (*) The numbering of months follows rtc_time more closely and
	 * thus, is slightly different from [1].
	 */

	udays		= days + 719468;

	u32tmp		= 4 * udays + 3;
	century		= u32tmp / 146097;
	day_of_century	= u32tmp % 146097 / 4;

	u32tmp		= 4 * day_of_century + 3;
	u64tmp		= 2939745ULL * u32tmp;
	year_of_century	= upper_32_bits(u64tmp);
	day_of_year	= lower_32_bits(u64tmp) / 2939745 / 4;

	year		= 100 * century + year_of_century;
	is_leap_year	= year_of_century != 0 ?
		year_of_century % 4 == 0 : century % 4 == 0;

	u32tmp		= 2141 * day_of_year + 132377;
	month		= u32tmp >> 16;
	day		= ((u16) u32tmp) / 2141;

	/*
	 * Recall that January 01 is the 306-th day of the year in the
	 * computational (not Gregorian) calendar.
	 */
	is_Jan_or_Feb	= day_of_year >= 306;

	/* Converts to the Gregorian calendar. */
	year		= year + is_Jan_or_Feb;
	month		= is_Jan_or_Feb ? month - 12 : month;
	day		= day + 1;

	day_of_year	= is_Jan_or_Feb ?
		day_of_year - 306 : day_of_year + 31 + 28 + is_leap_year;

	/* Converts to rtc_time's format. */
	tm->tm_year	= (int) (year - 1900);
	tm->tm_mon	= (int) month;
	tm->tm_mday	= (int) day;
	tm->tm_yday	= (int) day_of_year + 1;

	tm->tm_hour = secs / 3600;
	secs -= tm->tm_hour * 3600;
	tm->tm_min = secs / 60;
	tm->tm_sec = secs - tm->tm_min * 60;

	tm->tm_isdst = 0;
}
EXPORT_SYMBOL(rtc_time64_to_tm);

/*
 * Does the rtc_time represent a valid date/time?
 */
int rtc_valid_tm(struct rtc_time *tm)
{
	if (tm->tm_year < 70 ||
	    tm->tm_year > (INT_MAX - 1900) ||
	    ((unsigned int)tm->tm_mon) >= 12 ||
	    tm->tm_mday < 1 ||
	    tm->tm_mday > rtc_month_days(tm->tm_mon,
					 ((unsigned int)tm->tm_year + 1900)) ||
	    ((unsigned int)tm->tm_hour) >= 24 ||
	    ((unsigned int)tm->tm_min) >= 60 ||
	    ((unsigned int)tm->tm_sec) >= 60)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(rtc_valid_tm);

/*
 * rtc_tm_to_time64 - Converts rtc_time to time64_t.
 * Convert Gregorian date to seconds since 01-01-1970 00:00:00.
 */
time64_t rtc_tm_to_time64(struct rtc_time *tm)
{
	return mktime64(((unsigned int)tm->tm_year + 1900), tm->tm_mon + 1,
			tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}
EXPORT_SYMBOL(rtc_tm_to_time64);

/*
 * Convert rtc_time to ktime
 */
ktime_t rtc_tm_to_ktime(struct rtc_time tm)
{
	return ktime_set(rtc_tm_to_time64(&tm), 0);
}
EXPORT_SYMBOL_GPL(rtc_tm_to_ktime);

/*
 * Convert ktime to rtc_time
 */
struct rtc_time rtc_ktime_to_tm(ktime_t kt)
{
	struct timespec64 ts;
	struct rtc_time ret;

	ts = ktime_to_timespec64(kt);
	/* Round up any ns */
	if (ts.tv_nsec)
		ts.tv_sec++;
	rtc_time64_to_tm(ts.tv_sec, &ret);
	return ret;
}
EXPORT_SYMBOL_GPL(rtc_ktime_to_tm);
