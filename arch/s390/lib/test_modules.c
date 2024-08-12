// SPDX-License-Identifier: GPL-2.0+

#include <kunit/test.h>
#include <linux/module.h>

#include "test_modules.h"

/*
 * Test that modules with many relocations are loaded properly.
 */
static void test_modules_many_vmlinux_relocs(struct kunit *test)
{
	int result = 0;

#define CALL_RETURN(i) result += test_modules_return_ ## i()
	REPEAT_10000(CALL_RETURN);
	KUNIT_ASSERT_EQ(test, result, 49995000);
}

static struct kunit_case modules_testcases[] = {
	KUNIT_CASE(test_modules_many_vmlinux_relocs),
	{}
};

static struct kunit_suite modules_test_suite = {
	.name = "modules_test_s390",
	.test_cases = modules_testcases,
};

kunit_test_suites(&modules_test_suite);

MODULE_DESCRIPTION("KUnit test that modules with many relocations are loaded properly");
MODULE_LICENSE("GPL");
