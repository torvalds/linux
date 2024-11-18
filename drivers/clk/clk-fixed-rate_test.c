// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for clk fixed rate basic type
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <kunit/clk.h>
#include <kunit/of.h>
#include <kunit/platform_device.h>
#include <kunit/resource.h>
#include <kunit/test.h>

#include "clk-fixed-rate_test.h"

/**
 * struct clk_hw_fixed_rate_kunit_params - Parameters to pass to __clk_hw_register_fixed_rate()
 * @dev: device registering clk
 * @np: device_node of device registering clk
 * @name: name of clk
 * @parent_name: parent name of clk
 * @parent_hw: clk_hw pointer to parent of clk
 * @parent_data: parent_data describing parent of clk
 * @flags: clk framework flags
 * @fixed_rate: frequency of clk
 * @fixed_accuracy: accuracy of clk
 * @clk_fixed_flags: fixed rate specific clk flags
 */
struct clk_hw_fixed_rate_kunit_params {
	struct device *dev;
	struct device_node *np;
	const char *name;
	const char *parent_name;
	const struct clk_hw *parent_hw;
	const struct clk_parent_data *parent_data;
	unsigned long flags;
	unsigned long fixed_rate;
	unsigned long fixed_accuracy;
	unsigned long clk_fixed_flags;
};

static int
clk_hw_register_fixed_rate_kunit_init(struct kunit_resource *res, void *context)
{
	struct clk_hw_fixed_rate_kunit_params *params = context;
	struct clk_hw *hw;

	hw = __clk_hw_register_fixed_rate(params->dev, params->np,
					  params->name,
					  params->parent_name,
					  params->parent_hw,
					  params->parent_data,
					  params->flags,
					  params->fixed_rate,
					  params->fixed_accuracy,
					  params->clk_fixed_flags,
					  false);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	res->data = hw;

	return 0;
}

static void clk_hw_register_fixed_rate_kunit_exit(struct kunit_resource *res)
{
	struct clk_hw *hw = res->data;

	clk_hw_unregister_fixed_rate(hw);
}

/**
 * clk_hw_register_fixed_rate_kunit() - Test managed __clk_hw_register_fixed_rate()
 * @test: The test context
 * @params: Arguments to __clk_hw_register_fixed_rate()
 *
 * Return: Registered fixed rate clk_hw or ERR_PTR on failure
 */
static struct clk_hw *
clk_hw_register_fixed_rate_kunit(struct kunit *test,
				 struct clk_hw_fixed_rate_kunit_params *params)
{
	struct clk_hw *hw;

	hw = kunit_alloc_resource(test,
				  clk_hw_register_fixed_rate_kunit_init,
				  clk_hw_register_fixed_rate_kunit_exit,
				  GFP_KERNEL, params);
	if (!hw)
		return ERR_PTR(-EINVAL);

	return hw;
}

/**
 * clk_hw_unregister_fixed_rate_kunit() - Test managed clk_hw_unregister_fixed_rate()
 * @test: The test context
 * @hw: fixed rate clk to unregister upon test completion
 *
 * Automatically unregister @hw when @test is complete via
 * clk_hw_unregister_fixed_rate().
 *
 * Return: 0 on success or negative errno on failure
 */
static int clk_hw_unregister_fixed_rate_kunit(struct kunit *test, struct clk_hw *hw)
{
	if (!kunit_alloc_resource(test, NULL,
				  clk_hw_register_fixed_rate_kunit_exit,
				  GFP_KERNEL, hw))
		return -ENOMEM;

	return 0;
}

/*
 * Test that clk_get_rate() on a fixed rate clk registered with
 * clk_hw_register_fixed_rate() gets the proper frequency.
 */
static void clk_fixed_rate_rate_test(struct kunit *test)
{
	struct clk_hw *hw;
	struct clk *clk;
	const unsigned long fixed_rate = 230000;

	hw = clk_hw_register_fixed_rate(NULL, "test-fixed-rate", NULL, 0, fixed_rate);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_EQ(test, 0, clk_hw_unregister_fixed_rate_kunit(test, hw));

	clk = clk_hw_get_clk_prepared_enabled_kunit(test, hw, __func__);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);

	KUNIT_EXPECT_EQ(test, fixed_rate, clk_get_rate(clk));
}

/*
 * Test that clk_get_accuracy() on a fixed rate clk registered via
 * clk_hw_register_fixed_rate_with_accuracy() gets the proper accuracy.
 */
static void clk_fixed_rate_accuracy_test(struct kunit *test)
{
	struct clk_hw *hw;
	struct clk *clk;
	const unsigned long fixed_accuracy = 5000;

	hw = clk_hw_register_fixed_rate_with_accuracy(NULL, "test-fixed-rate",
						      NULL, 0, 0,
						      fixed_accuracy);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_EQ(test, 0, clk_hw_unregister_fixed_rate_kunit(test, hw));

	clk = clk_hw_get_clk_kunit(test, hw, __func__);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);

	KUNIT_EXPECT_EQ(test, fixed_accuracy, clk_get_accuracy(clk));
}

/* Test suite for a fixed rate clk without any parent */
static struct kunit_case clk_fixed_rate_test_cases[] = {
	KUNIT_CASE(clk_fixed_rate_rate_test),
	KUNIT_CASE(clk_fixed_rate_accuracy_test),
	{}
};

static struct kunit_suite clk_fixed_rate_suite = {
	.name = "clk_fixed_rate",
	.test_cases = clk_fixed_rate_test_cases,
};

/*
 * Test that clk_get_parent() on a fixed rate clk gets the proper parent.
 */
static void clk_fixed_rate_parent_test(struct kunit *test)
{
	struct clk_hw *hw, *parent_hw;
	struct clk *expected_parent, *actual_parent;
	struct clk *clk;
	const char *parent_name = "test-fixed-rate-parent";
	struct clk_hw_fixed_rate_kunit_params parent_params = {
		.name = parent_name,
	};

	parent_hw = clk_hw_register_fixed_rate_kunit(test, &parent_params);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent_hw);
	KUNIT_ASSERT_STREQ(test, parent_name, clk_hw_get_name(parent_hw));

	expected_parent = clk_hw_get_clk_kunit(test, parent_hw, __func__);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, expected_parent);

	hw = clk_hw_register_fixed_rate(NULL, "test-fixed-rate", parent_name, 0, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_EQ(test, 0, clk_hw_unregister_fixed_rate_kunit(test, hw));

	clk = clk_hw_get_clk_kunit(test, hw, __func__);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);

	actual_parent = clk_get_parent(clk);
	KUNIT_EXPECT_TRUE(test, clk_is_match(expected_parent, actual_parent));
}

/*
 * Test that clk_get_rate() on a fixed rate clk ignores the parent rate.
 */
static void clk_fixed_rate_parent_rate_test(struct kunit *test)
{
	struct clk_hw *hw, *parent_hw;
	struct clk *clk;
	const unsigned long expected_rate = 1405;
	const unsigned long parent_rate = 90402;
	const char *parent_name = "test-fixed-rate-parent";
	struct clk_hw_fixed_rate_kunit_params parent_params = {
		.name = parent_name,
		.fixed_rate = parent_rate,
	};

	parent_hw = clk_hw_register_fixed_rate_kunit(test, &parent_params);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent_hw);
	KUNIT_ASSERT_STREQ(test, parent_name, clk_hw_get_name(parent_hw));

	hw = clk_hw_register_fixed_rate(NULL, "test-fixed-rate", parent_name, 0,
					expected_rate);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_EQ(test, 0, clk_hw_unregister_fixed_rate_kunit(test, hw));

	clk = clk_hw_get_clk_prepared_enabled_kunit(test, hw, __func__);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);

	KUNIT_EXPECT_EQ(test, expected_rate, clk_get_rate(clk));
}

/*
 * Test that clk_get_accuracy() on a fixed rate clk ignores the parent accuracy.
 */
static void clk_fixed_rate_parent_accuracy_test(struct kunit *test)
{
	struct clk_hw *hw, *parent_hw;
	struct clk *clk;
	const unsigned long expected_accuracy = 900;
	const unsigned long parent_accuracy = 24000;
	const char *parent_name = "test-fixed-rate-parent";
	struct clk_hw_fixed_rate_kunit_params parent_params = {
		.name = parent_name,
		.fixed_accuracy = parent_accuracy,
	};

	parent_hw = clk_hw_register_fixed_rate_kunit(test, &parent_params);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent_hw);
	KUNIT_ASSERT_STREQ(test, parent_name, clk_hw_get_name(parent_hw));

	hw = clk_hw_register_fixed_rate_with_accuracy(NULL, "test-fixed-rate",
						      parent_name, 0, 0,
						      expected_accuracy);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_EQ(test, 0, clk_hw_unregister_fixed_rate_kunit(test, hw));

	clk = clk_hw_get_clk_kunit(test, hw, __func__);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);

	KUNIT_EXPECT_EQ(test, expected_accuracy, clk_get_accuracy(clk));
}

/* Test suite for a fixed rate clk with a parent */
static struct kunit_case clk_fixed_rate_parent_test_cases[] = {
	KUNIT_CASE(clk_fixed_rate_parent_test),
	KUNIT_CASE(clk_fixed_rate_parent_rate_test),
	KUNIT_CASE(clk_fixed_rate_parent_accuracy_test),
	{}
};

static struct kunit_suite clk_fixed_rate_parent_suite = {
	.name = "clk_fixed_rate_parent",
	.test_cases = clk_fixed_rate_parent_test_cases,
};

struct clk_fixed_rate_of_test_context {
	struct device *dev;
	struct platform_driver pdrv;
	struct completion probed;
};

static inline struct clk_fixed_rate_of_test_context *
pdev_to_clk_fixed_rate_of_test_context(struct platform_device *pdev)
{
	return container_of(to_platform_driver(pdev->dev.driver),
			    struct clk_fixed_rate_of_test_context,
			    pdrv);
}

/*
 * Test that of_fixed_clk_setup() registers a fixed rate clk with the proper
 * rate.
 */
static void clk_fixed_rate_of_probe_test(struct kunit *test)
{
	struct clk_fixed_rate_of_test_context *ctx = test->priv;
	struct device *dev = ctx->dev;
	struct clk *clk;

	clk = clk_get_kunit(test, dev, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);

	KUNIT_ASSERT_EQ(test, 0, clk_prepare_enable_kunit(test, clk));
	KUNIT_EXPECT_EQ(test, TEST_FIXED_FREQUENCY, clk_get_rate(clk));
}

/*
 * Test that of_fixed_clk_setup() registers a fixed rate clk with the proper
 * accuracy.
 */
static void clk_fixed_rate_of_accuracy_test(struct kunit *test)
{
	struct clk_fixed_rate_of_test_context *ctx = test->priv;
	struct device *dev = ctx->dev;
	struct clk *clk;

	clk = clk_get_kunit(test, dev, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);

	KUNIT_EXPECT_EQ(test, TEST_FIXED_ACCURACY, clk_get_accuracy(clk));
}

static struct kunit_case clk_fixed_rate_of_cases[] = {
	KUNIT_CASE(clk_fixed_rate_of_probe_test),
	KUNIT_CASE(clk_fixed_rate_of_accuracy_test),
	{}
};

static int clk_fixed_rate_of_test_probe(struct platform_device *pdev)
{
	struct clk_fixed_rate_of_test_context *ctx;

	ctx = pdev_to_clk_fixed_rate_of_test_context(pdev);
	ctx->dev = &pdev->dev;
	complete(&ctx->probed);

	return 0;
}

static int clk_fixed_rate_of_init(struct kunit *test)
{
	struct clk_fixed_rate_of_test_context *ctx;
	static const struct of_device_id match_table[] = {
		{ .compatible = "test,single-clk-consumer" },
		{ }
	};

	KUNIT_ASSERT_EQ(test, 0, of_overlay_apply_kunit(test, kunit_clk_fixed_rate_test));

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	test->priv = ctx;

	ctx->pdrv.probe = clk_fixed_rate_of_test_probe;
	ctx->pdrv.driver.of_match_table = match_table;
	ctx->pdrv.driver.name = __func__;
	ctx->pdrv.driver.owner = THIS_MODULE;
	init_completion(&ctx->probed);

	KUNIT_ASSERT_EQ(test, 0, kunit_platform_driver_register(test, &ctx->pdrv));
	KUNIT_ASSERT_NE(test, 0, wait_for_completion_timeout(&ctx->probed, HZ));

	return 0;
}

static struct kunit_suite clk_fixed_rate_of_suite = {
	.name = "clk_fixed_rate_of",
	.init = clk_fixed_rate_of_init,
	.test_cases = clk_fixed_rate_of_cases,
};

kunit_test_suites(
	&clk_fixed_rate_suite,
	&clk_fixed_rate_of_suite,
	&clk_fixed_rate_parent_suite,
);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit test for clk fixed rate basic type");
