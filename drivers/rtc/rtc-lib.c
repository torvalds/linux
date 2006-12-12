/*
 * rtc and date/time utility functions
 *
 * Copyright (C) 2005-06 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on arch/arm/common/rtctime.c and other bits
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
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

#define LEAPS_THRU_END_OF(y) ((y)/4 - (y)/100 + (y)/400)
#define LEAP_YEAR(year) ((!(year % 4) && (year % 100)) || !(year % 400))

/*
 * The number of days in the month.
 */
int rtc_month_days(unsigned int month, unsigned int year)
{
	return rtc_days_in_month[month] + (LEAP_YEAR(year) && month == 1);
}
EXPORT_SYMBOL(rtc_month_days);

/*
 * The number of days since January 1. (0 to 365)
 */
int rtc_year_days(unsigned int day, unsigned int month, unsigned int year)
{
	return rtc_ydays[LEAP_YEAR(year)][month] + day-1;
}
EXPORT_SYMBOL(rtc_year_days);

/*
 * Convert seconds since 01-01-1970 00:00:00 to Gregorian date.
 */
void rtc_time_to_tm(unsigned long time, struct rtc_time *tm)
{
	register int days, month, year;

	days = time / 86400;
	time -= days * 86400;

	/* day of the week, 1970-01-01 was a Thursday */
	tm->tm_wday = (days + 4) % 7;

	year = 1970 + days / 365;
	days -= (year - 1970) * 365
		+ LEAPS_THRU_END_OF(year - 1)
		- LEAPS_THRU_END_OF(1970 - 1);
	if (days < 0) {
		year -= 1;
		days += 365 + LEAP_YEAR(year);
	}
	tm->tm_year = year - 1900;
	tm->tm_yday = days + 1;

	for (month = 0; month < 11; month++) {
		int newdays;

		newdays = days - rtc_month_days(month, year);
		if (newdays < 0)
			break;
		days = newdays;
	}
	tm->tm_mon = month;
	tm->tm_mday = days + 1;

	tm->tm_hour = time / 3600;
	time -= tm->tm_hour * 3600;
	tm->tm_min = time / 60;
	tm->tm_sec = time - tm->tm_min * 60;
}
EXPORT_SYMBOL(rtc_time_to_tm);

/*
 * Does the rtc_time represent a valid date/time?
 */
int rtc_valid_tm(struct rtc_time *tm)
{
	if (tm->tm_year < 70
		|| ((unsigned)tm->tm_mon) >= 12
		|| tm->tm_mday < 1
		|| tm->tm_mday > rtc_month_days(tm->tm_mon, tm->tm_year + 1900)
		|| ((unsigned)tm->tm_hour) >= 24
		|| ((unsigned)tm->tm_min) >= 60
		|| ((unsigned)tm->tm_sec) >= 60)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(rtc_valid_tm);

/*
 * Convert Gregorian date to seconds since 01-01-1970 00:00:00.
 */
int rtc_tm_to_time(struct rtc_time *tm, unsigned long *time)
{
	*time = mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	return 0;
}
EXPORT_SYMBOL(rtc_tm_to_time);


/* Merge the valid (i.e. non-negative) fields of alarm into the current
 * time.  If the valid alarm fields are earlier than the equivalent
 * fields in the time, carry one into the least significant invalid
 * field, so that the alarm expiry is in the future.  It assumes that the
 * least significant invalid field is more significant than the most
 * significant valid field, and that the seconds field is valid.
 *
 * This is used by alarms that take relative (rather than absolute)
 * times, and/or have a simple binary second counter instead of
 * day/hour/minute/sec registers.
 */
void rtc_merge_alarm(struct rtc_time *now, struct rtc_time *alarm)
{
	int *alarmp = &alarm->tm_sec;
	int *timep = &now->tm_sec;
	int carry_into, i;

	/* Ignore everything past the 6th element (tm_year). */
	for (i = 5; i > 0; i--) {
		if (alarmp[i] < 0)
			alarmp[i] = timep[i];
		else
			break;
	}

	/* No carry needed if all fields are valid. */
	if (i == 5)
		return;

	for (carry_into = i + 1; i >= 0; i--) {
		if (alarmp[i] < timep[i])
			break;

		if (alarmp[i] > timep[i])
			return;
	}

	switch (carry_into) {
		case 1:
			alarm->tm_min++;

			if (alarm->tm_min < 60)
				return;

			alarm->tm_min = 0;
			/* fall-through */

		case 2:
			alarm->tm_hour++;

			if (alarm->tm_hour < 60)
				return;

			alarm->tm_hour = 0;
			/* fall-through */

		case 3:
			alarm->tm_mday++;

			if (alarm->tm_mday <= rtc_days_in_month[alarm->tm_mon])
				return;

			alarm->tm_mday = 1;
			/* fall-through */

		case 4:
			alarm->tm_mon++;

			if (alarm->tm_mon <= 12)
				return;

			alarm->tm_mon = 1;
			/* fall-through */

		case 5:
			alarm->tm_year++;
	}
}
EXPORT_SYMBOL(rtc_merge_alarm);

MODULE_LICENSE("GPL");
