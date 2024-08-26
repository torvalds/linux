// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the FPGA Bridge
 *
 * Copyright (C) 2023 Red Hat, Inc.
 *
 * Author: Marco Pagani <marpagan@redhat.com>
 */

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/fpga/fpga-bridge.h>
#include <linux/module.h>
#include <linux/types.h>

struct bridge_stats {
	bool enable;
};

struct bridge_ctx {
	struct fpga_bridge *bridge;
	struct device *dev;
	struct bridge_stats stats;
};

static int op_enable_set(struct fpga_bridge *bridge, bool enable)
{
	struct bridge_stats *stats = bridge->priv;

	stats->enable = enable;

	return 0;
}

/*
 * Fake FPGA bridge that implements only the enable_set op to track
 * the state.
 */
static const struct fpga_bridge_ops fake_bridge_ops = {
	.enable_set = op_enable_set,
};

/**
 * register_test_bridge() - Register a fake FPGA bridge for testing.
 * @test: KUnit test context object.
 * @dev_name: name of the kunit device to be registered
 *
 * Return: Context of the newly registered FPGA bridge.
 */
static struct bridge_ctx *register_test_bridge(struct kunit *test, const char *dev_name)
{
	struct bridge_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	ctx->dev = kunit_device_register(test, dev_name);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->dev);

	ctx->bridge = fpga_bridge_register(ctx->dev, "Fake FPGA bridge", &fake_bridge_ops,
					   &ctx->stats);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(ctx->bridge));

	return ctx;
}

static void unregister_test_bridge(struct kunit *test, struct bridge_ctx *ctx)
{
	fpga_bridge_unregister(ctx->bridge);
	kunit_device_unregister(test, ctx->dev);
}

static void fpga_bridge_test_get(struct kunit *test)
{
	struct bridge_ctx *ctx = test->priv;
	struct fpga_bridge *bridge;

	bridge = fpga_bridge_get(ctx->dev, NULL);
	KUNIT_EXPECT_PTR_EQ(test, bridge, ctx->bridge);

	bridge = fpga_bridge_get(ctx->dev, NULL);
	KUNIT_EXPECT_EQ(test, PTR_ERR(bridge), -EBUSY);

	fpga_bridge_put(ctx->bridge);
}

static void fpga_bridge_test_toggle(struct kunit *test)
{
	struct bridge_ctx *ctx = test->priv;
	int ret;

	ret = fpga_bridge_disable(ctx->bridge);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, ctx->stats.enable);

	ret = fpga_bridge_enable(ctx->bridge);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, ctx->stats.enable);
}

/* Test the functions for getting and controlling a list of bridges */
static void fpga_bridge_test_get_put_list(struct kunit *test)
{
	struct list_head bridge_list;
	struct bridge_ctx *ctx_0, *ctx_1;
	int ret;

	ctx_0 = test->priv;
	ctx_1 = register_test_bridge(test, "fpga-bridge-test-dev-1");

	INIT_LIST_HEAD(&bridge_list);

	/* Get bridge 0 and add it to the list */
	ret = fpga_bridge_get_to_list(ctx_0->dev, NULL, &bridge_list);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_PTR_EQ(test, ctx_0->bridge,
			    list_first_entry_or_null(&bridge_list, struct fpga_bridge, node));

	/* Get bridge 1 and add it to the list */
	ret = fpga_bridge_get_to_list(ctx_1->dev, NULL, &bridge_list);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_PTR_EQ(test, ctx_1->bridge,
			    list_first_entry_or_null(&bridge_list, struct fpga_bridge, node));

	/* Disable an then enable both bridges from the list */
	ret = fpga_bridges_disable(&bridge_list);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_FALSE(test, ctx_0->stats.enable);
	KUNIT_EXPECT_FALSE(test, ctx_1->stats.enable);

	ret = fpga_bridges_enable(&bridge_list);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_TRUE(test, ctx_0->stats.enable);
	KUNIT_EXPECT_TRUE(test, ctx_1->stats.enable);

	/* Put and remove both bridges from the list */
	fpga_bridges_put(&bridge_list);

	KUNIT_EXPECT_TRUE(test, list_empty(&bridge_list));

	unregister_test_bridge(test, ctx_1);
}

static int fpga_bridge_test_init(struct kunit *test)
{
	test->priv = register_test_bridge(test, "fpga-bridge-test-dev-0");

	return 0;
}

static void fpga_bridge_test_exit(struct kunit *test)
{
	unregister_test_bridge(test, test->priv);
}

static struct kunit_case fpga_bridge_test_cases[] = {
	KUNIT_CASE(fpga_bridge_test_get),
	KUNIT_CASE(fpga_bridge_test_toggle),
	KUNIT_CASE(fpga_bridge_test_get_put_list),
	{}
};

static struct kunit_suite fpga_bridge_suite = {
	.name = "fpga_bridge",
	.init = fpga_bridge_test_init,
	.exit = fpga_bridge_test_exit,
	.test_cases = fpga_bridge_test_cases,
};

kunit_test_suite(fpga_bridge_suite);

MODULE_LICENSE("GPL");
