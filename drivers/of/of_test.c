// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for OF APIs
 */
#include <linux/ioport.h>
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

struct of_address_resource_bounds_case {
	u64 start;
	u64 size;
	int ret;

	u64 res_start;
	u64 res_end;
};

static void of_address_resource_bounds_case_desc(const struct of_address_resource_bounds_case *p,
						 char *name)
{
	snprintf(name, KUNIT_PARAM_DESC_SIZE, "start=0x%016llx,size=0x%016llx", p->start, p->size);
}

static const struct of_address_resource_bounds_case of_address_resource_bounds_cases[] = {
	{
		.start = 0,
		.size = 0,
		.ret = 0,
		.res_start = 0,
		.res_end = -1,
	},
	{
		.start = 0,
		.size = 0x1000,
		.ret = 0,
		.res_start = 0,
		.res_end = 0xfff,
	},
	{
		.start = 0x1000,
		.size = 0,
		.ret = 0,
		.res_start = 0x1000,
		.res_end = 0xfff,
	},
	{
		.start = 0x1000,
		.size = 0x1000,
		.ret = 0,
		.res_start = 0x1000,
		.res_end = 0x1fff,
	},
	{
		.start = 1,
		.size = RESOURCE_SIZE_MAX,
		.ret = 0,
		.res_start = 1,
		.res_end = RESOURCE_SIZE_MAX,
	},
	{
		.start = RESOURCE_SIZE_MAX,
		.size = 1,
		.ret = 0,
		.res_start = RESOURCE_SIZE_MAX,
		.res_end = RESOURCE_SIZE_MAX,
	},
	{
		.start = 2,
		.size = RESOURCE_SIZE_MAX,
		.ret = -EOVERFLOW,
	},
	{
		.start = RESOURCE_SIZE_MAX,
		.size = 2,
		.ret = -EOVERFLOW,
	},
	{
		.start = ULL(0x100000000),
		.size = 1,
		.ret = sizeof(resource_size_t) > sizeof(u32) ? 0 : -EOVERFLOW,
		.res_start = ULL(0x100000000),
		.res_end = ULL(0x100000000),
	},
	{
		.start = 0x1000,
		.size = 0xffffffff,
		.ret = sizeof(resource_size_t) > sizeof(u32) ? 0 : -EOVERFLOW,
		.res_start = 0x1000,
		.res_end = ULL(0x100000ffe),
	},
};

KUNIT_ARRAY_PARAM(of_address_resource_bounds,
		  of_address_resource_bounds_cases, of_address_resource_bounds_case_desc);

static void of_address_resource_bounds(struct kunit *test)
{
	const struct of_address_resource_bounds_case *param = test->param_value;
	struct resource r; /* Intentionally uninitialized */
	int ret;

	if (!IS_ENABLED(CONFIG_OF_ADDRESS))
		kunit_skip(test, "CONFIG_OF_ADDRESS not enabled\n");

	ret = __of_address_resource_bounds(&r, param->start, param->size);
	KUNIT_EXPECT_EQ(test, param->ret, ret);
	if (ret == 0) {
		KUNIT_EXPECT_EQ(test, (resource_size_t)param->res_start, r.start);
		KUNIT_EXPECT_EQ(test, (resource_size_t)param->res_end, r.end);
		KUNIT_EXPECT_EQ(test, param->size, resource_size(&r));
	}
}

static struct kunit_case of_address_test_cases[] = {
	KUNIT_CASE_PARAM(of_address_resource_bounds, of_address_resource_bounds_gen_params),
	{}
};

static struct kunit_suite of_address_suite = {
	.name = "of_address",
	.test_cases = of_address_test_cases,
};

kunit_test_suites(
	&of_dtb_suite, &of_address_suite,
);
MODULE_DESCRIPTION("KUnit tests for OF APIs");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_LICENSE("GPL");
