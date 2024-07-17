// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the FPGA Region
 *
 * Copyright (C) 2023 Red Hat, Inc.
 *
 * Author: Marco Pagani <marpagan@redhat.com>
 */

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/fpga/fpga-bridge.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-region.h>
#include <linux/module.h>
#include <linux/types.h>

struct mgr_stats {
	u32 write_count;
};

struct bridge_stats {
	bool enable;
	u32 cycles_count;
};

struct test_ctx {
	struct fpga_manager *mgr;
	struct device *mgr_dev;
	struct fpga_bridge *bridge;
	struct device *bridge_dev;
	struct fpga_region *region;
	struct device *region_dev;
	struct bridge_stats bridge_stats;
	struct mgr_stats mgr_stats;
};

static int op_write(struct fpga_manager *mgr, const char *buf, size_t count)
{
	struct mgr_stats *stats = mgr->priv;

	stats->write_count++;

	return 0;
}

/*
 * Fake FPGA manager that implements only the write op to count the number
 * of programming cycles. The internals of the programming sequence are
 * tested in the Manager suite since they are outside the responsibility
 * of the Region.
 */
static const struct fpga_manager_ops fake_mgr_ops = {
	.write = op_write,
};

static int op_enable_set(struct fpga_bridge *bridge, bool enable)
{
	struct bridge_stats *stats = bridge->priv;

	if (!stats->enable && enable)
		stats->cycles_count++;

	stats->enable = enable;

	return 0;
}

/*
 * Fake FPGA bridge that implements only enable_set op to count the number
 * of activation cycles.
 */
static const struct fpga_bridge_ops fake_bridge_ops = {
	.enable_set = op_enable_set,
};

static int fake_region_get_bridges(struct fpga_region *region)
{
	struct fpga_bridge *bridge = region->priv;

	return fpga_bridge_get_to_list(bridge->dev.parent, region->info, &region->bridge_list);
}

static int fake_region_match(struct device *dev, const void *data)
{
	return dev->parent == data;
}

static void fpga_region_test_class_find(struct kunit *test)
{
	struct test_ctx *ctx = test->priv;
	struct fpga_region *region;

	region = fpga_region_class_find(NULL, ctx->region_dev, fake_region_match);
	KUNIT_EXPECT_PTR_EQ(test, region, ctx->region);

	put_device(&region->dev);
}

/*
 * FPGA Region programming test. The Region must call get_bridges() to get
 * and control the bridges, and then the Manager for the actual programming.
 */
static void fpga_region_test_program_fpga(struct kunit *test)
{
	struct test_ctx *ctx = test->priv;
	struct fpga_image_info *img_info;
	char img_buf[4];
	int ret;

	img_info = fpga_image_info_alloc(ctx->mgr_dev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, img_info);

	img_info->buf = img_buf;
	img_info->count = sizeof(img_buf);

	ctx->region->info = img_info;
	ret = fpga_region_program_fpga(ctx->region);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, 1, ctx->mgr_stats.write_count);
	KUNIT_EXPECT_EQ(test, 1, ctx->bridge_stats.cycles_count);

	fpga_bridges_put(&ctx->region->bridge_list);

	ret = fpga_region_program_fpga(ctx->region);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, 2, ctx->mgr_stats.write_count);
	KUNIT_EXPECT_EQ(test, 2, ctx->bridge_stats.cycles_count);

	fpga_bridges_put(&ctx->region->bridge_list);

	fpga_image_info_free(img_info);
}

/*
 * The configuration used in this test suite uses a single bridge to
 * limit the code under test to a single unit. The functions used by the
 * Region for getting and controlling bridges are tested (with a list of
 * multiple bridges) in the Bridge suite.
 */
static int fpga_region_test_init(struct kunit *test)
{
	struct test_ctx *ctx;
	struct fpga_region_info region_info = { 0 };

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	ctx->mgr_dev = kunit_device_register(test, "fpga-manager-test-dev");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->mgr_dev);

	ctx->mgr = devm_fpga_mgr_register(ctx->mgr_dev, "Fake FPGA Manager",
					  &fake_mgr_ops, &ctx->mgr_stats);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(ctx->mgr));

	ctx->bridge_dev = kunit_device_register(test, "fpga-bridge-test-dev");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->bridge_dev);

	ctx->bridge = fpga_bridge_register(ctx->bridge_dev, "Fake FPGA Bridge",
					   &fake_bridge_ops, &ctx->bridge_stats);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(ctx->bridge));

	ctx->bridge_stats.enable = true;

	ctx->region_dev = kunit_device_register(test, "fpga-region-test-dev");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->region_dev);

	region_info.mgr = ctx->mgr;
	region_info.priv = ctx->bridge;
	region_info.get_bridges = fake_region_get_bridges;

	ctx->region = fpga_region_register_full(ctx->region_dev, &region_info);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(ctx->region));

	test->priv = ctx;

	return 0;
}

static void fpga_region_test_exit(struct kunit *test)
{
	struct test_ctx *ctx = test->priv;

	fpga_region_unregister(ctx->region);
	kunit_device_unregister(test, ctx->region_dev);

	fpga_bridge_unregister(ctx->bridge);
	kunit_device_unregister(test, ctx->bridge_dev);

	kunit_device_unregister(test, ctx->mgr_dev);
}

static struct kunit_case fpga_region_test_cases[] = {
	KUNIT_CASE(fpga_region_test_class_find),
	KUNIT_CASE(fpga_region_test_program_fpga),
	{}
};

static struct kunit_suite fpga_region_suite = {
	.name = "fpga_mgr",
	.init = fpga_region_test_init,
	.exit = fpga_region_test_exit,
	.test_cases = fpga_region_test_cases,
};

kunit_test_suite(fpga_region_suite);

MODULE_LICENSE("GPL");
