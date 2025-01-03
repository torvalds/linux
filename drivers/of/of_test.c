// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for OF APIs
 */
#include <linux/module.h>
#include <linux/of.h>

#include <kunit/test.h>

#include "of_private.h"

/*
 * Test that the root node "/" can be found by path.
 */
static void of_dtb_root_node_found_by_path(struct kunit *test)
{
	struct device_node *np;

	np = of_find_node_by_path("/");
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, np);
	of_node_put(np);
}

/*
 * Test that the 'of_root' global variable is always populated when DT code is
 * enabled. Remove this test once of_root is removed from global access.
 */
static void of_dtb_root_node_populates_of_root(struct kunit *test)
{
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, of_root);
}

static struct kunit_case of_dtb_test_cases[] = {
	KUNIT_CASE(of_dtb_root_node_found_by_path),
	KUNIT_CASE(of_dtb_root_node_populates_of_root),
	{}
};

static int of_dtb_test_init(struct kunit *test)
{
	of_root_kunit_skip(test);
	if (!IS_ENABLED(CONFIG_OF_EARLY_FLATTREE))
		kunit_skip(test, "requires CONFIG_OF_EARLY_FLATTREE");

	return 0;
}

/*
 * Test suite to confirm a DTB is loaded.
 */
static struct kunit_suite of_dtb_suite = {
	.name = "of_dtb",
	.test_cases = of_dtb_test_cases,
	.init = of_dtb_test_init,
};

kunit_test_suites(
	&of_dtb_suite,
);
MODULE_DESCRIPTION("KUnit tests for OF APIs");
MODULE_LICENSE("GPL");
