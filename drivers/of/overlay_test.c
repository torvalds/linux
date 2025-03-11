// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for device tree overlays
 */
#include <linux/device/bus.h>
#include <linux/kconfig.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <kunit/of.h>
#include <kunit/test.h>

#include "of_private.h"

static const char * const kunit_node_name = "kunit-test";
static const char * const kunit_compatible = "test,empty";

/* Test that of_overlay_apply_kunit() adds a node to the live tree */
static void of_overlay_apply_kunit_apply(struct kunit *test)
{
	struct device_node *np;

	KUNIT_ASSERT_EQ(test, 0,
			of_overlay_apply_kunit(test, kunit_overlay_test));

	np = of_find_node_by_name(NULL, kunit_node_name);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, np);
	of_node_put(np);
}

/*
 * Test that of_overlay_apply_kunit() creates platform devices with the
 * expected device_node
 */
static void of_overlay_apply_kunit_platform_device(struct kunit *test)
{
	struct platform_device *pdev;
	struct device_node *np;

	KUNIT_ASSERT_EQ(test, 0,
			of_overlay_apply_kunit(test, kunit_overlay_test));

	np = of_find_node_by_name(NULL, kunit_node_name);
	of_node_put_kunit(test, np);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, np);

	pdev = of_find_device_by_node(np);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, pdev);
	if (pdev)
		put_device(&pdev->dev);
}

static int of_overlay_bus_match_compatible(struct device *dev, const void *data)
{
	return of_device_is_compatible(dev->of_node, data);
}

/* Test that of_overlay_apply_kunit() cleans up after the test is finished */
static void of_overlay_apply_kunit_cleanup(struct kunit *test)
{
	struct kunit fake;
	struct platform_device *pdev;
	struct device *dev;
	struct device_node *np;

	of_root_kunit_skip(test);
	if (!IS_ENABLED(CONFIG_OF_OVERLAY))
		kunit_skip(test, "requires CONFIG_OF_OVERLAY to apply overlay");
	if (!IS_ENABLED(CONFIG_OF_EARLY_FLATTREE))
		kunit_skip(test, "requires CONFIG_OF_EARLY_FLATTREE for root node");

	kunit_init_test(&fake, "fake test", NULL);
	KUNIT_ASSERT_EQ(test, fake.status, KUNIT_SUCCESS);

	KUNIT_ASSERT_EQ(test, 0,
			of_overlay_apply_kunit(&fake, kunit_overlay_test));

	np = of_find_node_by_name(NULL, kunit_node_name);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, np);
	of_node_put_kunit(&fake, np);

	pdev = of_find_device_by_node(np);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);
	put_device(&pdev->dev); /* Not derefing 'pdev' after this */

	/* Remove overlay */
	kunit_cleanup(&fake);

	/* The node and device should be removed */
	np = of_find_node_by_name(NULL, kunit_node_name);
	KUNIT_EXPECT_PTR_EQ(test, NULL, np);
	of_node_put(np);

	dev = bus_find_device(&platform_bus_type, NULL, kunit_compatible,
			      of_overlay_bus_match_compatible);
	KUNIT_EXPECT_PTR_EQ(test, NULL, dev);
	put_device(dev);
}

static struct kunit_case of_overlay_apply_kunit_test_cases[] = {
	KUNIT_CASE(of_overlay_apply_kunit_apply),
	KUNIT_CASE(of_overlay_apply_kunit_platform_device),
	KUNIT_CASE(of_overlay_apply_kunit_cleanup),
	{}
};

/*
 * Test suite for test managed device tree overlays.
 */
static struct kunit_suite of_overlay_apply_kunit_suite = {
	.name = "of_overlay_apply_kunit",
	.test_cases = of_overlay_apply_kunit_test_cases,
};

kunit_test_suites(
	&of_overlay_apply_kunit_suite,
);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for device tree overlays");
