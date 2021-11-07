// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

static struct kunit_case ne_misc_dev_test_cases[] = {
	{}
};

static struct kunit_suite ne_misc_dev_test_suite = {
	.name = "ne_misc_dev_test",
	.test_cases = ne_misc_dev_test_cases,
};

static struct kunit_suite *ne_misc_dev_test_suites[] = {
	&ne_misc_dev_test_suite,
	NULL
};
