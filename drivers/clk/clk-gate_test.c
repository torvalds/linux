// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit tests for clk gate
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include <kunit/test.h>

static void clk_gate_register_test_dev(struct kunit *test)
{
	struct clk_hw *ret;
	struct platform_device *pdev;

	pdev = platform_device_register_simple("test_gate_device", -1, NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	ret = clk_hw_register_gate(&pdev->dev, "test_gate", NULL, 0, NULL,
				   0, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ret);
	KUNIT_EXPECT_STREQ(test, "test_gate", clk_hw_get_name(ret));
	KUNIT_EXPECT_EQ(test, 0UL, clk_hw_get_flags(ret));

	clk_hw_unregister_gate(ret);
	platform_device_put(pdev);
}

static void clk_gate_register_test_parent_names(struct kunit *test)
{
	struct clk_hw *parent;
	struct clk_hw *ret;

	parent = clk_hw_register_fixed_rate(NULL, "test_parent", NULL, 0,
					    1000000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	ret = clk_hw_register_gate(NULL, "test_gate", "test_parent", 0, NULL,
				   0, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ret);
	KUNIT_EXPECT_PTR_EQ(test, parent, clk_hw_get_parent(ret));

	clk_hw_unregister_gate(ret);
	clk_hw_unregister_fixed_rate(parent);
}

static void clk_gate_register_test_parent_data(struct kunit *test)
{
	struct clk_hw *parent;
	struct clk_hw *ret;
	struct clk_parent_data pdata = { };

	parent = clk_hw_register_fixed_rate(NULL, "test_parent", NULL, 0,
					    1000000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);
	pdata.hw = parent;

	ret = clk_hw_register_gate_parent_data(NULL, "test_gate", &pdata, 0,
					       NULL, 0, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ret);
	KUNIT_EXPECT_PTR_EQ(test, parent, clk_hw_get_parent(ret));

	clk_hw_unregister_gate(ret);
	clk_hw_unregister_fixed_rate(parent);
}

static void clk_gate_register_test_parent_data_legacy(struct kunit *test)
{
	struct clk_hw *parent;
	struct clk_hw *ret;
	struct clk_parent_data pdata = { };

	parent = clk_hw_register_fixed_rate(NULL, "test_parent", NULL, 0,
					    1000000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);
	pdata.name = "test_parent";

	ret = clk_hw_register_gate_parent_data(NULL, "test_gate", &pdata, 0,
					       NULL, 0, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ret);
	KUNIT_EXPECT_PTR_EQ(test, parent, clk_hw_get_parent(ret));

	clk_hw_unregister_gate(ret);
	clk_hw_unregister_fixed_rate(parent);
}

static void clk_gate_register_test_parent_hw(struct kunit *test)
{
	struct clk_hw *parent;
	struct clk_hw *ret;

	parent = clk_hw_register_fixed_rate(NULL, "test_parent", NULL, 0,
					    1000000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	ret = clk_hw_register_gate_parent_hw(NULL, "test_gate", parent, 0, NULL,
					     0, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ret);
	KUNIT_EXPECT_PTR_EQ(test, parent, clk_hw_get_parent(ret));

	clk_hw_unregister_gate(ret);
	clk_hw_unregister_fixed_rate(parent);
}

static void clk_gate_register_test_hiword_invalid(struct kunit *test)
{
	struct clk_hw *ret;

	ret = clk_hw_register_gate(NULL, "test_gate", NULL, 0, NULL,
				   20, CLK_GATE_HIWORD_MASK, NULL);

	KUNIT_EXPECT_TRUE(test, IS_ERR(ret));
}

static struct kunit_case clk_gate_register_test_cases[] = {
	KUNIT_CASE(clk_gate_register_test_dev),
	KUNIT_CASE(clk_gate_register_test_parent_names),
	KUNIT_CASE(clk_gate_register_test_parent_data),
	KUNIT_CASE(clk_gate_register_test_parent_data_legacy),
	KUNIT_CASE(clk_gate_register_test_parent_hw),
	KUNIT_CASE(clk_gate_register_test_hiword_invalid),
	{}
};

static struct kunit_suite clk_gate_register_test_suite = {
	.name = "clk-gate-register-test",
	.test_cases = clk_gate_register_test_cases,
};

struct clk_gate_test_context {
	void __iomem *fake_mem;
	struct clk_hw *hw;
	struct clk_hw *parent;
	__le32 fake_reg; /* Keep at end, KASAN can detect out of bounds */
};

static struct clk_gate_test_context *clk_gate_test_alloc_ctx(struct kunit *test)
{
	struct clk_gate_test_context *ctx;

	test->priv = ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	ctx->fake_mem = (void __force __iomem *)&ctx->fake_reg;

	return ctx;
}

static void clk_gate_test_parent_rate(struct kunit *test)
{
	struct clk_gate_test_context *ctx = test->priv;
	struct clk_hw *parent = ctx->parent;
	struct clk_hw *hw = ctx->hw;
	unsigned long prate = clk_hw_get_rate(parent);
	unsigned long rate = clk_hw_get_rate(hw);

	KUNIT_EXPECT_EQ(test, prate, rate);
}

static void clk_gate_test_enable(struct kunit *test)
{
	struct clk_gate_test_context *ctx = test->priv;
	struct clk_hw *parent = ctx->parent;
	struct clk_hw *hw = ctx->hw;
	struct clk *clk = hw->clk;
	u32 enable_val = BIT(5);

	KUNIT_ASSERT_EQ(test, clk_prepare_enable(clk), 0);

	KUNIT_EXPECT_EQ(test, enable_val, le32_to_cpu(ctx->fake_reg));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_enabled(hw));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_prepared(hw));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_enabled(parent));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_prepared(parent));
}

static void clk_gate_test_disable(struct kunit *test)
{
	struct clk_gate_test_context *ctx = test->priv;
	struct clk_hw *parent = ctx->parent;
	struct clk_hw *hw = ctx->hw;
	struct clk *clk = hw->clk;
	u32 enable_val = BIT(5);
	u32 disable_val = 0;

	KUNIT_ASSERT_EQ(test, clk_prepare_enable(clk), 0);
	KUNIT_ASSERT_EQ(test, enable_val, le32_to_cpu(ctx->fake_reg));

	clk_disable_unprepare(clk);
	KUNIT_EXPECT_EQ(test, disable_val, le32_to_cpu(ctx->fake_reg));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_enabled(hw));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_prepared(hw));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_enabled(parent));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_prepared(parent));
}

static struct kunit_case clk_gate_test_cases[] = {
	KUNIT_CASE(clk_gate_test_parent_rate),
	KUNIT_CASE(clk_gate_test_enable),
	KUNIT_CASE(clk_gate_test_disable),
	{}
};

static int clk_gate_test_init(struct kunit *test)
{
	struct clk_hw *parent;
	struct clk_hw *hw;
	struct clk_gate_test_context *ctx;

	ctx = clk_gate_test_alloc_ctx(test);
	parent = clk_hw_register_fixed_rate(NULL, "test_parent", NULL, 0,
					    2000000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	hw = clk_hw_register_gate_parent_hw(NULL, "test_gate", parent, 0,
					    ctx->fake_mem, 5, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	ctx->hw = hw;
	ctx->parent = parent;

	return 0;
}

static void clk_gate_test_exit(struct kunit *test)
{
	struct clk_gate_test_context *ctx = test->priv;

	clk_hw_unregister_gate(ctx->hw);
	clk_hw_unregister_fixed_rate(ctx->parent);
}

static struct kunit_suite clk_gate_test_suite = {
	.name = "clk-gate-test",
	.init = clk_gate_test_init,
	.exit = clk_gate_test_exit,
	.test_cases = clk_gate_test_cases,
};

static void clk_gate_test_invert_enable(struct kunit *test)
{
	struct clk_gate_test_context *ctx = test->priv;
	struct clk_hw *parent = ctx->parent;
	struct clk_hw *hw = ctx->hw;
	struct clk *clk = hw->clk;
	u32 enable_val = 0;

	KUNIT_ASSERT_EQ(test, clk_prepare_enable(clk), 0);

	KUNIT_EXPECT_EQ(test, enable_val, le32_to_cpu(ctx->fake_reg));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_enabled(hw));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_prepared(hw));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_enabled(parent));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_prepared(parent));
}

static void clk_gate_test_invert_disable(struct kunit *test)
{
	struct clk_gate_test_context *ctx = test->priv;
	struct clk_hw *parent = ctx->parent;
	struct clk_hw *hw = ctx->hw;
	struct clk *clk = hw->clk;
	u32 enable_val = 0;
	u32 disable_val = BIT(15);

	KUNIT_ASSERT_EQ(test, clk_prepare_enable(clk), 0);
	KUNIT_ASSERT_EQ(test, enable_val, le32_to_cpu(ctx->fake_reg));

	clk_disable_unprepare(clk);
	KUNIT_EXPECT_EQ(test, disable_val, le32_to_cpu(ctx->fake_reg));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_enabled(hw));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_prepared(hw));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_enabled(parent));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_prepared(parent));
}

static struct kunit_case clk_gate_test_invert_cases[] = {
	KUNIT_CASE(clk_gate_test_invert_enable),
	KUNIT_CASE(clk_gate_test_invert_disable),
	{}
};

static int clk_gate_test_invert_init(struct kunit *test)
{
	struct clk_hw *parent;
	struct clk_hw *hw;
	struct clk_gate_test_context *ctx;

	ctx = clk_gate_test_alloc_ctx(test);
	parent = clk_hw_register_fixed_rate(NULL, "test_parent", NULL, 0,
					    2000000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	ctx->fake_reg = cpu_to_le32(BIT(15)); /* Default to off */
	hw = clk_hw_register_gate_parent_hw(NULL, "test_gate", parent, 0,
					    ctx->fake_mem, 15,
					    CLK_GATE_SET_TO_DISABLE, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	ctx->hw = hw;
	ctx->parent = parent;

	return 0;
}

static struct kunit_suite clk_gate_test_invert_suite = {
	.name = "clk-gate-invert-test",
	.init = clk_gate_test_invert_init,
	.exit = clk_gate_test_exit,
	.test_cases = clk_gate_test_invert_cases,
};

static void clk_gate_test_hiword_enable(struct kunit *test)
{
	struct clk_gate_test_context *ctx = test->priv;
	struct clk_hw *parent = ctx->parent;
	struct clk_hw *hw = ctx->hw;
	struct clk *clk = hw->clk;
	u32 enable_val = BIT(9) | BIT(9 + 16);

	KUNIT_ASSERT_EQ(test, clk_prepare_enable(clk), 0);

	KUNIT_EXPECT_EQ(test, enable_val, le32_to_cpu(ctx->fake_reg));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_enabled(hw));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_prepared(hw));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_enabled(parent));
	KUNIT_EXPECT_TRUE(test, clk_hw_is_prepared(parent));
}

static void clk_gate_test_hiword_disable(struct kunit *test)
{
	struct clk_gate_test_context *ctx = test->priv;
	struct clk_hw *parent = ctx->parent;
	struct clk_hw *hw = ctx->hw;
	struct clk *clk = hw->clk;
	u32 enable_val = BIT(9) | BIT(9 + 16);
	u32 disable_val = BIT(9 + 16);

	KUNIT_ASSERT_EQ(test, clk_prepare_enable(clk), 0);
	KUNIT_ASSERT_EQ(test, enable_val, le32_to_cpu(ctx->fake_reg));

	clk_disable_unprepare(clk);
	KUNIT_EXPECT_EQ(test, disable_val, le32_to_cpu(ctx->fake_reg));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_enabled(hw));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_prepared(hw));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_enabled(parent));
	KUNIT_EXPECT_FALSE(test, clk_hw_is_prepared(parent));
}

static struct kunit_case clk_gate_test_hiword_cases[] = {
	KUNIT_CASE(clk_gate_test_hiword_enable),
	KUNIT_CASE(clk_gate_test_hiword_disable),
	{}
};

static int clk_gate_test_hiword_init(struct kunit *test)
{
	struct clk_hw *parent;
	struct clk_hw *hw;
	struct clk_gate_test_context *ctx;

	ctx = clk_gate_test_alloc_ctx(test);
	parent = clk_hw_register_fixed_rate(NULL, "test_parent", NULL, 0,
					    2000000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	hw = clk_hw_register_gate_parent_hw(NULL, "test_gate", parent, 0,
					    ctx->fake_mem, 9,
					    CLK_GATE_HIWORD_MASK, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	ctx->hw = hw;
	ctx->parent = parent;

	return 0;
}

static struct kunit_suite clk_gate_test_hiword_suite = {
	.name = "clk-gate-hiword-test",
	.init = clk_gate_test_hiword_init,
	.exit = clk_gate_test_exit,
	.test_cases = clk_gate_test_hiword_cases,
};

static void clk_gate_test_is_enabled(struct kunit *test)
{
	struct clk_hw *hw;
	struct clk_gate_test_context *ctx;

	ctx = clk_gate_test_alloc_ctx(test);
	ctx->fake_reg = cpu_to_le32(BIT(7));
	hw = clk_hw_register_gate(NULL, "test_gate", NULL, 0, ctx->fake_mem, 7,
				  0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_TRUE(test, clk_hw_is_enabled(hw));

	clk_hw_unregister_gate(hw);
}

static void clk_gate_test_is_disabled(struct kunit *test)
{
	struct clk_hw *hw;
	struct clk_gate_test_context *ctx;

	ctx = clk_gate_test_alloc_ctx(test);
	ctx->fake_reg = cpu_to_le32(BIT(4));
	hw = clk_hw_register_gate(NULL, "test_gate", NULL, 0, ctx->fake_mem, 7,
				  0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_FALSE(test, clk_hw_is_enabled(hw));

	clk_hw_unregister_gate(hw);
}

static void clk_gate_test_is_enabled_inverted(struct kunit *test)
{
	struct clk_hw *hw;
	struct clk_gate_test_context *ctx;

	ctx = clk_gate_test_alloc_ctx(test);
	ctx->fake_reg = cpu_to_le32(BIT(31));
	hw = clk_hw_register_gate(NULL, "test_gate", NULL, 0, ctx->fake_mem, 2,
				  CLK_GATE_SET_TO_DISABLE, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_TRUE(test, clk_hw_is_enabled(hw));

	clk_hw_unregister_gate(hw);
}

static void clk_gate_test_is_disabled_inverted(struct kunit *test)
{
	struct clk_hw *hw;
	struct clk_gate_test_context *ctx;

	ctx = clk_gate_test_alloc_ctx(test);
	ctx->fake_reg = cpu_to_le32(BIT(29));
	hw = clk_hw_register_gate(NULL, "test_gate", NULL, 0, ctx->fake_mem, 29,
				  CLK_GATE_SET_TO_DISABLE, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);
	KUNIT_ASSERT_FALSE(test, clk_hw_is_enabled(hw));

	clk_hw_unregister_gate(hw);
}

static struct kunit_case clk_gate_test_enabled_cases[] = {
	KUNIT_CASE(clk_gate_test_is_enabled),
	KUNIT_CASE(clk_gate_test_is_disabled),
	KUNIT_CASE(clk_gate_test_is_enabled_inverted),
	KUNIT_CASE(clk_gate_test_is_disabled_inverted),
	{}
};

static struct kunit_suite clk_gate_test_enabled_suite = {
	.name = "clk-gate-is_enabled-test",
	.test_cases = clk_gate_test_enabled_cases,
};

kunit_test_suites(
	&clk_gate_register_test_suite,
	&clk_gate_test_suite,
	&clk_gate_test_invert_suite,
	&clk_gate_test_hiword_suite,
	&clk_gate_test_enabled_suite
);
MODULE_DESCRIPTION("Kunit tests for clk gate");
MODULE_LICENSE("GPL v2");
