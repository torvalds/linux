// SPDX-License-Identifier: LGPL-2.1+

#include <kunit/test.h>
#include <linux/rtc.h>

/*
 * Advance a date by one day.
 */
static void advance_date(int *year, int *month, int *mday, int *yday)
{
	if (*mday != rtc_month_days(*month - 1, *year)) {
		++*mday;
		++*yday;
		return;
	}

	*mday = 1;
	if (*month != 12) {
		++*month;
		++*yday;
		return;
	}

	*month = 1;
	*yday  = 1;
	++*year;
}

/*
 * Checks every day in a 160000 years interval starting on 1970-01-01
 * against the expected result.
 */
static void rtc_time64_to_tm_test_date_range(struct kunit *test)
{
	/*
	 * 160000 years	= (160000 / 400) * 400 years
	 *		= (160000 / 400) * 146097 days
	 *		= (160000 / 400) * 146097 * 86400 seconds
	 */
	time64_t total_secs = ((time64_t) 160000) / 400 * 146097 * 86400;

	int year	= 1970;
	int month	= 1;
	int mday	= 1;
	int yday	= 1;

	struct rtc_time result;
	time64_t secs;
	s64 days;

	for (secs = 0; secs <= total_secs; secs += 86400) {

		rtc_time64_to_tm(secs, &result);

		days = div_s64(secs, 86400);

		#define FAIL_MSG "%d/%02d/%02d (%2d) : %ld", \
			year, month, mday, yday, days

		KUNIT_ASSERT_EQ_MSG(test, year - 1900, result.tm_year, FAIL_MSG);
		KUNIT_ASSERT_EQ_MSG(test, month - 1, result.tm_mon, FAIL_MSG);
		KUNIT_ASSERT_EQ_MSG(test, mday, result.tm_mday, FAIL_MSG);
		KUNIT_ASSERT_EQ_MSG(test, yday, result.tm_yday, FAIL_MSG);

		advance_date(&year, &month, &mday, &yday);
	}
}

static struct kunit_case rtc_lib_test_cases[] = {
	KUNIT_CASE(rtc_time64_to_tm_test_date_range),
	{}
};

static struct kunit_suite rtc_lib_test_suite = {
	.name = "rtc_lib_test_cases",
	.test_cases = rtc_lib_test_cases,
};

kunit_test_suite(rtc_lib_test_suite);

MODULE_LICENSE("GPL");
