// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit tests for clk framework
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/clk-conf.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* Needed for clk_hw_get_clk() */
#include "clk.h"

#include <kunit/clk.h>
#include <kunit/of.h>
#include <kunit/platform_device.h>
#include <kunit/test.h>

#include "kunit_clk_assigned_rates.h"
#include "clk_parent_data_test.h"

static const struct clk_ops empty_clk_ops = { };

#define DUMMY_CLOCK_INIT_RATE	(42 * 1000 * 1000)
#define DUMMY_CLOCK_RATE_1	(142 * 1000 * 1000)
#define DUMMY_CLOCK_RATE_2	(242 * 1000 * 1000)

struct clk_dummy_context {
	struct clk_hw hw;
	unsigned long rate;
};

static unsigned long clk_dummy_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_dummy_context *ctx =
		container_of(hw, struct clk_dummy_context, hw);

	return ctx->rate;
}

static int clk_dummy_determine_rate(struct clk_hw *hw,
				    struct clk_rate_request *req)
{
	/* Just return the same rate without modifying it */
	return 0;
}

static int clk_dummy_maximize_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	/*
	 * If there's a maximum set, always run the clock at the maximum
	 * allowed.
	 */
	if (req->max_rate < ULONG_MAX)
		req->rate = req->max_rate;

	return 0;
}

static int clk_dummy_minimize_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	/*
	 * If there's a minimum set, always run the clock at the minimum
	 * allowed.
	 */
	if (req->min_rate > 0)
		req->rate = req->min_rate;

	return 0;
}

static int clk_dummy_set_rate(struct clk_hw *hw,
			      unsigned long rate,
			      unsigned long parent_rate)
{
	struct clk_dummy_context *ctx =
		container_of(hw, struct clk_dummy_context, hw);

	ctx->rate = rate;
	return 0;
}

static int clk_dummy_single_set_parent(struct clk_hw *hw, u8 index)
{
	if (index >= clk_hw_get_num_parents(hw))
		return -EINVAL;

	return 0;
}

static u8 clk_dummy_single_get_parent(struct clk_hw *hw)
{
	return 0;
}

static const struct clk_ops clk_dummy_rate_ops = {
	.recalc_rate = clk_dummy_recalc_rate,
	.determine_rate = clk_dummy_determine_rate,
	.set_rate = clk_dummy_set_rate,
};

static const struct clk_ops clk_dummy_maximize_rate_ops = {
	.recalc_rate = clk_dummy_recalc_rate,
	.determine_rate = clk_dummy_maximize_rate,
	.set_rate = clk_dummy_set_rate,
};

static const struct clk_ops clk_dummy_minimize_rate_ops = {
	.recalc_rate = clk_dummy_recalc_rate,
	.determine_rate = clk_dummy_minimize_rate,
	.set_rate = clk_dummy_set_rate,
};

static const struct clk_ops clk_dummy_single_parent_ops = {
	/*
	 * FIXME: Even though we should probably be able to use
	 * __clk_mux_determine_rate() here, if we use it and call
	 * clk_round_rate() or clk_set_rate() with a rate lower than
	 * what all the parents can provide, it will return -EINVAL.
	 *
	 * This is due to the fact that it has the undocumented
	 * behaviour to always pick up the closest rate higher than the
	 * requested rate. If we get something lower, it thus considers
	 * that it's not acceptable and will return an error.
	 *
	 * It's somewhat inconsistent and creates a weird threshold
	 * between rates above the parent rate which would be rounded to
	 * what the parent can provide, but rates below will simply
	 * return an error.
	 */
	.determine_rate = __clk_mux_determine_rate_closest,
	.set_parent = clk_dummy_single_set_parent,
	.get_parent = clk_dummy_single_get_parent,
};

struct clk_multiple_parent_ctx {
	struct clk_dummy_context parents_ctx[2];
	struct clk_hw hw;
	u8 current_parent;
};

static int clk_multiple_parents_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_multiple_parent_ctx *ctx =
		container_of(hw, struct clk_multiple_parent_ctx, hw);

	if (index >= clk_hw_get_num_parents(hw))
		return -EINVAL;

	ctx->current_parent = index;

	return 0;
}

static u8 clk_multiple_parents_mux_get_parent(struct clk_hw *hw)
{
	struct clk_multiple_parent_ctx *ctx =
		container_of(hw, struct clk_multiple_parent_ctx, hw);

	return ctx->current_parent;
}

static const struct clk_ops clk_multiple_parents_mux_ops = {
	.get_parent = clk_multiple_parents_mux_get_parent,
	.set_parent = clk_multiple_parents_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate_closest,
};

static const struct clk_ops clk_multiple_parents_no_reparent_mux_ops = {
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.get_parent = clk_multiple_parents_mux_get_parent,
	.set_parent = clk_multiple_parents_mux_set_parent,
};

static int clk_test_init_with_ops(struct kunit *test, const struct clk_ops *ops)
{
	struct clk_dummy_context *ctx;
	struct clk_init_data init = { };
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->rate = DUMMY_CLOCK_INIT_RATE;
	test->priv = ctx;

	init.name = "test_dummy_rate";
	init.ops = ops;
	ctx->hw.init = &init;

	ret = clk_hw_register(NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

static int clk_test_init(struct kunit *test)
{
	return clk_test_init_with_ops(test, &clk_dummy_rate_ops);
}

static int clk_maximize_test_init(struct kunit *test)
{
	return clk_test_init_with_ops(test, &clk_dummy_maximize_rate_ops);
}

static int clk_minimize_test_init(struct kunit *test)
{
	return clk_test_init_with_ops(test, &clk_dummy_minimize_rate_ops);
}

static void clk_test_exit(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;

	clk_hw_unregister(&ctx->hw);
}

/*
 * Test that the actual rate matches what is returned by clk_get_rate()
 */
static void clk_test_get_rate(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, ctx->rate);

	clk_put(clk);
}

/*
 * Test that, after a call to clk_set_rate(), the rate returned by
 * clk_get_rate() matches.
 *
 * This assumes that clk_ops.determine_rate or clk_ops.round_rate won't
 * modify the requested rate, which is our case in clk_dummy_rate_ops.
 */
static void clk_test_set_get_rate(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	clk_put(clk);
}

/*
 * Test that, after several calls to clk_set_rate(), the rate returned
 * by clk_get_rate() matches the last one.
 *
 * This assumes that clk_ops.determine_rate or clk_ops.round_rate won't
 * modify the requested rate, which is our case in clk_dummy_rate_ops.
 */
static void clk_test_set_set_get_rate(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that clk_round_rate and clk_set_rate are consistent and will
 * return the same frequency.
 */
static void clk_test_round_set_get_rate(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long set_rate;
	long rounded_rate;

	rounded_rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_1);
	KUNIT_ASSERT_GT(test, rounded_rate, 0);
	KUNIT_EXPECT_EQ(test, rounded_rate, DUMMY_CLOCK_RATE_1);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1),
			0);

	set_rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, set_rate, 0);
	KUNIT_EXPECT_EQ(test, rounded_rate, set_rate);

	clk_put(clk);
}

static struct kunit_case clk_test_cases[] = {
	KUNIT_CASE(clk_test_get_rate),
	KUNIT_CASE(clk_test_set_get_rate),
	KUNIT_CASE(clk_test_set_set_get_rate),
	KUNIT_CASE(clk_test_round_set_get_rate),
	{}
};

/*
 * Test suite for a basic rate clock, without any parent.
 *
 * These tests exercise the rate API with simple scenarios
 */
static struct kunit_suite clk_test_suite = {
	.name = "clk-test",
	.init = clk_test_init,
	.exit = clk_test_exit,
	.test_cases = clk_test_cases,
};

static int clk_uncached_test_init(struct kunit *test)
{
	struct clk_dummy_context *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->rate = DUMMY_CLOCK_INIT_RATE;
	ctx->hw.init = CLK_HW_INIT_NO_PARENT("test-clk",
					     &clk_dummy_rate_ops,
					     CLK_GET_RATE_NOCACHE);

	ret = clk_hw_register(NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

/*
 * Test that for an uncached clock, the clock framework doesn't cache
 * the rate and clk_get_rate() will return the underlying clock rate
 * even if it changed.
 */
static void clk_test_uncached_get_rate(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_INIT_RATE);

	/* We change the rate behind the clock framework's back */
	ctx->rate = DUMMY_CLOCK_RATE_1;
	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	clk_put(clk);
}

/*
 * Test that for an uncached clock, clk_set_rate_range() will work
 * properly if the rate hasn't changed.
 */
static void clk_test_uncached_set_range(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that for an uncached clock, clk_set_rate_range() will work
 * properly if the rate has changed in hardware.
 *
 * In this case, it means that if the rate wasn't initially in the range
 * we're trying to set, but got changed at some point into the range
 * without the kernel knowing about it, its rate shouldn't be affected.
 */
static void clk_test_uncached_updated_rate_set_range(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	/* We change the rate behind the clock framework's back */
	ctx->rate = DUMMY_CLOCK_RATE_1 + 1000;
	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1 + 1000);

	clk_put(clk);
}

static struct kunit_case clk_uncached_test_cases[] = {
	KUNIT_CASE(clk_test_uncached_get_rate),
	KUNIT_CASE(clk_test_uncached_set_range),
	KUNIT_CASE(clk_test_uncached_updated_rate_set_range),
	{}
};

/*
 * Test suite for a basic, uncached, rate clock, without any parent.
 *
 * These tests exercise the rate API with simple scenarios
 */
static struct kunit_suite clk_uncached_test_suite = {
	.name = "clk-uncached-test",
	.init = clk_uncached_test_init,
	.exit = clk_test_exit,
	.test_cases = clk_uncached_test_cases,
};

static int
clk_multiple_parents_mux_test_init(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx;
	const char *parents[2] = { "parent-0", "parent-1"};
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->parents_ctx[0].hw.init = CLK_HW_INIT_NO_PARENT("parent-0",
							    &clk_dummy_rate_ops,
							    0);
	ctx->parents_ctx[0].rate = DUMMY_CLOCK_RATE_1;
	ret = clk_hw_register_kunit(test, NULL, &ctx->parents_ctx[0].hw);
	if (ret)
		return ret;

	ctx->parents_ctx[1].hw.init = CLK_HW_INIT_NO_PARENT("parent-1",
							    &clk_dummy_rate_ops,
							    0);
	ctx->parents_ctx[1].rate = DUMMY_CLOCK_RATE_2;
	ret = clk_hw_register_kunit(test, NULL, &ctx->parents_ctx[1].hw);
	if (ret)
		return ret;

	ctx->current_parent = 0;
	ctx->hw.init = CLK_HW_INIT_PARENTS("test-mux", parents,
					   &clk_multiple_parents_mux_ops,
					   CLK_SET_RATE_PARENT);
	ret = clk_hw_register_kunit(test, NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

/*
 * Test that for a clock with multiple parents, clk_get_parent()
 * actually returns the current one.
 */
static void
clk_test_multiple_parents_mux_get_parent(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent = clk_hw_get_clk(&ctx->parents_ctx[0].hw, NULL);

	KUNIT_EXPECT_TRUE(test, clk_is_match(clk_get_parent(clk), parent));

	clk_put(parent);
	clk_put(clk);
}

/*
 * Test that for a clock with a multiple parents, clk_has_parent()
 * actually reports all of them as parents.
 */
static void
clk_test_multiple_parents_mux_has_parent(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;

	parent = clk_hw_get_clk(&ctx->parents_ctx[0].hw, NULL);
	KUNIT_EXPECT_TRUE(test, clk_has_parent(clk, parent));
	clk_put(parent);

	parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_EXPECT_TRUE(test, clk_has_parent(clk, parent));
	clk_put(parent);

	clk_put(clk);
}

/*
 * Test that for a clock with a multiple parents, if we set a range on
 * that clock and the parent is changed, its rate after the reparenting
 * is still within the range we asked for.
 *
 * FIXME: clk_set_parent() only does the reparenting but doesn't
 * reevaluate whether the new clock rate is within its boundaries or
 * not.
 */
static void
clk_test_multiple_parents_mux_set_range_set_parent_get_rate(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk_kunit(test, hw, NULL);
	struct clk *parent1, *parent2;
	unsigned long rate;
	int ret;

	kunit_skip(test, "This needs to be fixed in the core.");

	parent1 = clk_hw_get_clk_kunit(test, &ctx->parents_ctx[0].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent1);
	KUNIT_ASSERT_TRUE(test, clk_is_match(clk_get_parent(clk), parent1));

	parent2 = clk_hw_get_clk_kunit(test, &ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent2);

	ret = clk_set_rate(parent1, DUMMY_CLOCK_RATE_1);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate(parent2, DUMMY_CLOCK_RATE_2);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(clk,
				 DUMMY_CLOCK_RATE_1 - 1000,
				 DUMMY_CLOCK_RATE_1 + 1000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_parent(clk, parent2);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1 - 1000);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_1 + 1000);
}

static struct kunit_case clk_multiple_parents_mux_test_cases[] = {
	KUNIT_CASE(clk_test_multiple_parents_mux_get_parent),
	KUNIT_CASE(clk_test_multiple_parents_mux_has_parent),
	KUNIT_CASE(clk_test_multiple_parents_mux_set_range_set_parent_get_rate),
	{}
};

/*
 * Test suite for a basic mux clock with two parents, with
 * CLK_SET_RATE_PARENT on the child.
 *
 * These tests exercise the consumer API and check that the state of the
 * child and parents are sane and consistent.
 */
static struct kunit_suite
clk_multiple_parents_mux_test_suite = {
	.name = "clk-multiple-parents-mux-test",
	.init = clk_multiple_parents_mux_test_init,
	.test_cases = clk_multiple_parents_mux_test_cases,
};

static int
clk_orphan_transparent_multiple_parent_mux_test_init(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx;
	const char *parents[2] = { "missing-parent", "proper-parent"};
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->parents_ctx[1].hw.init = CLK_HW_INIT_NO_PARENT("proper-parent",
							    &clk_dummy_rate_ops,
							    0);
	ctx->parents_ctx[1].rate = DUMMY_CLOCK_INIT_RATE;
	ret = clk_hw_register_kunit(test, NULL, &ctx->parents_ctx[1].hw);
	if (ret)
		return ret;

	ctx->hw.init = CLK_HW_INIT_PARENTS("test-orphan-mux", parents,
					   &clk_multiple_parents_mux_ops,
					   CLK_SET_RATE_PARENT);
	ret = clk_hw_register_kunit(test, NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

/*
 * Test that, for a mux whose current parent hasn't been registered yet and is
 * thus orphan, clk_get_parent() will return NULL.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_get_parent(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);

	KUNIT_EXPECT_PTR_EQ(test, clk_get_parent(clk), NULL);

	clk_put(clk);
}

/*
 * Test that, for a mux whose current parent hasn't been registered yet,
 * calling clk_set_parent() to a valid parent will properly update the
 * mux parent and its orphan status.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_set_parent(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent, *new_parent;
	int ret;

	parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	ret = clk_set_parent(clk, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	new_parent = clk_get_parent(clk);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);
	KUNIT_EXPECT_TRUE(test, clk_is_match(parent, new_parent));

	clk_put(parent);
	clk_put(clk);
}

/*
 * Test that, for a mux that started orphan but got switched to a valid
 * parent, calling clk_drop_range() on the mux won't affect the parent
 * rate.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_set_parent_drop_range(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	unsigned long parent_rate, new_parent_rate;
	int ret;

	parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	parent_rate = clk_get_rate(parent);
	KUNIT_ASSERT_GT(test, parent_rate, 0);

	ret = clk_set_parent(clk, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_drop_range(clk);
	KUNIT_ASSERT_EQ(test, ret, 0);

	new_parent_rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, new_parent_rate, 0);
	KUNIT_EXPECT_EQ(test, parent_rate, new_parent_rate);

	clk_put(parent);
	clk_put(clk);
}

/*
 * Test that, for a mux that started orphan but got switched to a valid
 * parent, the rate of the mux and its new parent are consistent.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_set_parent_get_rate(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	unsigned long parent_rate, rate;
	int ret;

	parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	parent_rate = clk_get_rate(parent);
	KUNIT_ASSERT_GT(test, parent_rate, 0);

	ret = clk_set_parent(clk, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, parent_rate, rate);

	clk_put(parent);
	clk_put(clk);
}

/*
 * Test that, for a mux that started orphan but got switched to a valid
 * parent, calling clk_put() on the mux won't affect the parent rate.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_set_parent_put(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk *clk, *parent;
	unsigned long parent_rate, new_parent_rate;
	int ret;

	parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	clk = clk_hw_get_clk(&ctx->hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);

	parent_rate = clk_get_rate(parent);
	KUNIT_ASSERT_GT(test, parent_rate, 0);

	ret = clk_set_parent(clk, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	clk_put(clk);

	new_parent_rate = clk_get_rate(parent);
	KUNIT_ASSERT_GT(test, new_parent_rate, 0);
	KUNIT_EXPECT_EQ(test, parent_rate, new_parent_rate);

	clk_put(parent);
}

/*
 * Test that, for a mux that started orphan but got switched to a valid
 * parent, calling clk_set_rate_range() will affect the parent state if
 * its rate is out of range.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_set_parent_set_range_modified(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	unsigned long rate;
	int ret;

	parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	ret = clk_set_parent(clk, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(clk, DUMMY_CLOCK_RATE_1, DUMMY_CLOCK_RATE_2);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(parent);
	clk_put(clk);
}

/*
 * Test that, for a mux that started orphan but got switched to a valid
 * parent, calling clk_set_rate_range() won't affect the parent state if
 * its rate is within range.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_set_parent_set_range_untouched(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	unsigned long parent_rate, new_parent_rate;
	int ret;

	parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	parent_rate = clk_get_rate(parent);
	KUNIT_ASSERT_GT(test, parent_rate, 0);

	ret = clk_set_parent(clk, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(clk,
				 DUMMY_CLOCK_INIT_RATE - 1000,
				 DUMMY_CLOCK_INIT_RATE + 1000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	new_parent_rate = clk_get_rate(parent);
	KUNIT_ASSERT_GT(test, new_parent_rate, 0);
	KUNIT_EXPECT_EQ(test, parent_rate, new_parent_rate);

	clk_put(parent);
	clk_put(clk);
}

/*
 * Test that, for a mux whose current parent hasn't been registered yet,
 * calling clk_set_rate_range() will succeed, and will be taken into
 * account when rounding a rate.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_set_range_round_rate(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	long rate;
	int ret;

	ret = clk_set_rate_range(clk, DUMMY_CLOCK_RATE_1, DUMMY_CLOCK_RATE_2);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_1 - 1000);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that, for a mux that started orphan, was assigned and rate and
 * then got switched to a valid parent, its rate is eventually within
 * range.
 *
 * FIXME: Even though we update the rate as part of clk_set_parent(), we
 * don't evaluate whether that new rate is within range and needs to be
 * adjusted.
 */
static void
clk_test_orphan_transparent_multiple_parent_mux_set_range_set_parent_get_rate(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk_kunit(test, hw, NULL);
	struct clk *parent;
	unsigned long rate;
	int ret;

	kunit_skip(test, "This needs to be fixed in the core.");

	clk_hw_set_rate_range(hw, DUMMY_CLOCK_RATE_1, DUMMY_CLOCK_RATE_2);

	parent = clk_hw_get_clk_kunit(test, &ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	ret = clk_set_parent(clk, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);
}

static struct kunit_case clk_orphan_transparent_multiple_parent_mux_test_cases[] = {
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_get_parent),
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_set_parent),
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_set_parent_drop_range),
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_set_parent_get_rate),
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_set_parent_put),
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_set_parent_set_range_modified),
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_set_parent_set_range_untouched),
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_set_range_round_rate),
	KUNIT_CASE(clk_test_orphan_transparent_multiple_parent_mux_set_range_set_parent_get_rate),
	{}
};

/*
 * Test suite for a basic mux clock with two parents. The default parent
 * isn't registered, only the second parent is. By default, the clock
 * will thus be orphan.
 *
 * These tests exercise the behaviour of the consumer API when dealing
 * with an orphan clock, and how we deal with the transition to a valid
 * parent.
 */
static struct kunit_suite clk_orphan_transparent_multiple_parent_mux_test_suite = {
	.name = "clk-orphan-transparent-multiple-parent-mux-test",
	.init = clk_orphan_transparent_multiple_parent_mux_test_init,
	.test_cases = clk_orphan_transparent_multiple_parent_mux_test_cases,
};

struct clk_single_parent_ctx {
	struct clk_dummy_context parent_ctx;
	struct clk_hw hw;
};

static int clk_single_parent_mux_test_init(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->parent_ctx.rate = DUMMY_CLOCK_INIT_RATE;
	ctx->parent_ctx.hw.init =
		CLK_HW_INIT_NO_PARENT("parent-clk",
				      &clk_dummy_rate_ops,
				      0);

	ret = clk_hw_register_kunit(test, NULL, &ctx->parent_ctx.hw);
	if (ret)
		return ret;

	ctx->hw.init = CLK_HW_INIT("test-clk", "parent-clk",
				   &clk_dummy_single_parent_ops,
				   CLK_SET_RATE_PARENT);

	ret = clk_hw_register_kunit(test, NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

static void
clk_single_parent_mux_test_exit(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;

	clk_hw_unregister(&ctx->hw);
	clk_hw_unregister(&ctx->parent_ctx.hw);
}

/*
 * Test that for a clock with a single parent, clk_get_parent() actually
 * returns the parent.
 */
static void
clk_test_single_parent_mux_get_parent(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent = clk_hw_get_clk(&ctx->parent_ctx.hw, NULL);

	KUNIT_EXPECT_TRUE(test, clk_is_match(clk_get_parent(clk), parent));

	clk_put(parent);
	clk_put(clk);
}

/*
 * Test that for a clock with a single parent, clk_has_parent() actually
 * reports it as a parent.
 */
static void
clk_test_single_parent_mux_has_parent(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent = clk_hw_get_clk(&ctx->parent_ctx.hw, NULL);

	KUNIT_EXPECT_TRUE(test, clk_has_parent(clk, parent));

	clk_put(parent);
	clk_put(clk);
}

/*
 * Test that for a clock that can't modify its rate and with a single
 * parent, if we set disjoints range on the parent and then the child,
 * the second will return an error.
 *
 * FIXME: clk_set_rate_range() only considers the current clock when
 * evaluating whether ranges are disjoints and not the upstream clocks
 * ranges.
 */
static void
clk_test_single_parent_mux_set_range_disjoint_child_last(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk_kunit(test, hw, NULL);
	struct clk *parent;
	int ret;

	kunit_skip(test, "This needs to be fixed in the core.");

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	ret = clk_set_rate_range(parent, 1000, 2000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(clk, 3000, 4000);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that for a clock that can't modify its rate and with a single
 * parent, if we set disjoints range on the child and then the parent,
 * the second will return an error.
 *
 * FIXME: clk_set_rate_range() only considers the current clock when
 * evaluating whether ranges are disjoints and not the downstream clocks
 * ranges.
 */
static void
clk_test_single_parent_mux_set_range_disjoint_parent_last(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk_kunit(test, hw, NULL);
	struct clk *parent;
	int ret;

	kunit_skip(test, "This needs to be fixed in the core.");

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	ret = clk_set_rate_range(clk, 1000, 2000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(parent, 3000, 4000);
	KUNIT_EXPECT_LT(test, ret, 0);
}

/*
 * Test that for a clock that can't modify its rate and with a single
 * parent, if we set a range on the parent and then call
 * clk_round_rate(), the boundaries of the parent are taken into
 * account.
 */
static void
clk_test_single_parent_mux_set_range_round_rate_parent_only(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	long rate;
	int ret;

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	ret = clk_set_rate_range(parent, DUMMY_CLOCK_RATE_1, DUMMY_CLOCK_RATE_2);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_1 - 1000);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that for a clock that can't modify its rate and with a single
 * parent, if we set a range on the parent and a more restrictive one on
 * the child, and then call clk_round_rate(), the boundaries of the
 * two clocks are taken into account.
 */
static void
clk_test_single_parent_mux_set_range_round_rate_child_smaller(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	long rate;
	int ret;

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	ret = clk_set_rate_range(parent, DUMMY_CLOCK_RATE_1, DUMMY_CLOCK_RATE_2);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(clk, DUMMY_CLOCK_RATE_1 + 1000, DUMMY_CLOCK_RATE_2 - 1000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_1 - 1000);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1 + 1000);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2 - 1000);

	rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_2 + 1000);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1 + 1000);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2 - 1000);

	clk_put(clk);
}

/*
 * Test that for a clock that can't modify its rate and with a single
 * parent, if we set a range on the child and a more restrictive one on
 * the parent, and then call clk_round_rate(), the boundaries of the
 * two clocks are taken into account.
 */
static void
clk_test_single_parent_mux_set_range_round_rate_parent_smaller(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	long rate;
	int ret;

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	ret = clk_set_rate_range(parent, DUMMY_CLOCK_RATE_1 + 1000, DUMMY_CLOCK_RATE_2 - 1000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(clk, DUMMY_CLOCK_RATE_1, DUMMY_CLOCK_RATE_2);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_1 - 1000);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1 + 1000);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2 - 1000);

	rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_2 + 1000);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1 + 1000);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2 - 1000);

	clk_put(clk);
}

static struct kunit_case clk_single_parent_mux_test_cases[] = {
	KUNIT_CASE(clk_test_single_parent_mux_get_parent),
	KUNIT_CASE(clk_test_single_parent_mux_has_parent),
	KUNIT_CASE(clk_test_single_parent_mux_set_range_disjoint_child_last),
	KUNIT_CASE(clk_test_single_parent_mux_set_range_disjoint_parent_last),
	KUNIT_CASE(clk_test_single_parent_mux_set_range_round_rate_child_smaller),
	KUNIT_CASE(clk_test_single_parent_mux_set_range_round_rate_parent_only),
	KUNIT_CASE(clk_test_single_parent_mux_set_range_round_rate_parent_smaller),
	{}
};

/*
 * Test suite for a basic mux clock with one parent, with
 * CLK_SET_RATE_PARENT on the child.
 *
 * These tests exercise the consumer API and check that the state of the
 * child and parent are sane and consistent.
 */
static struct kunit_suite
clk_single_parent_mux_test_suite = {
	.name = "clk-single-parent-mux-test",
	.init = clk_single_parent_mux_test_init,
	.test_cases = clk_single_parent_mux_test_cases,
};

static int clk_orphan_transparent_single_parent_mux_test_init(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx;
	struct clk_init_data init = { };
	const char * const parents[] = { "orphan_parent" };
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	init.name = "test_orphan_dummy_parent";
	init.ops = &clk_dummy_single_parent_ops;
	init.parent_names = parents;
	init.num_parents = ARRAY_SIZE(parents);
	init.flags = CLK_SET_RATE_PARENT;
	ctx->hw.init = &init;

	ret = clk_hw_register(NULL, &ctx->hw);
	if (ret)
		return ret;

	memset(&init, 0, sizeof(init));
	init.name = "orphan_parent";
	init.ops = &clk_dummy_rate_ops;
	ctx->parent_ctx.hw.init = &init;
	ctx->parent_ctx.rate = DUMMY_CLOCK_INIT_RATE;

	ret = clk_hw_register(NULL, &ctx->parent_ctx.hw);
	if (ret)
		return ret;

	return 0;
}

/*
 * Test that a mux-only clock, with an initial rate within a range,
 * will still have the same rate after the range has been enforced.
 *
 * See:
 * https://lore.kernel.org/linux-clk/7720158d-10a7-a17b-73a4-a8615c9c6d5c@collabora.com/
 */
static void clk_test_orphan_transparent_parent_mux_set_range(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate, new_rate;

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   ctx->parent_ctx.rate - 1000,
					   ctx->parent_ctx.rate + 1000),
			0);

	new_rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, new_rate, 0);
	KUNIT_EXPECT_EQ(test, rate, new_rate);

	clk_put(clk);
}

static struct kunit_case clk_orphan_transparent_single_parent_mux_test_cases[] = {
	KUNIT_CASE(clk_test_orphan_transparent_parent_mux_set_range),
	{}
};

/*
 * Test suite for a basic mux clock with one parent. The parent is
 * registered after its child. The clock will thus be an orphan when
 * registered, but will no longer be when the tests run.
 *
 * These tests make sure a clock that used to be orphan has a sane,
 * consistent, behaviour.
 */
static struct kunit_suite clk_orphan_transparent_single_parent_test_suite = {
	.name = "clk-orphan-transparent-single-parent-test",
	.init = clk_orphan_transparent_single_parent_mux_test_init,
	.exit = clk_single_parent_mux_test_exit,
	.test_cases = clk_orphan_transparent_single_parent_mux_test_cases,
};

struct clk_single_parent_two_lvl_ctx {
	struct clk_dummy_context parent_parent_ctx;
	struct clk_dummy_context parent_ctx;
	struct clk_hw hw;
};

static int
clk_orphan_two_level_root_last_test_init(struct kunit *test)
{
	struct clk_single_parent_two_lvl_ctx *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->parent_ctx.hw.init =
		CLK_HW_INIT("intermediate-parent",
			    "root-parent",
			    &clk_dummy_single_parent_ops,
			    CLK_SET_RATE_PARENT);
	ret = clk_hw_register(NULL, &ctx->parent_ctx.hw);
	if (ret)
		return ret;

	ctx->hw.init =
		CLK_HW_INIT("test-clk", "intermediate-parent",
			    &clk_dummy_single_parent_ops,
			    CLK_SET_RATE_PARENT);
	ret = clk_hw_register(NULL, &ctx->hw);
	if (ret)
		return ret;

	ctx->parent_parent_ctx.rate = DUMMY_CLOCK_INIT_RATE;
	ctx->parent_parent_ctx.hw.init =
		CLK_HW_INIT_NO_PARENT("root-parent",
				      &clk_dummy_rate_ops,
				      0);
	ret = clk_hw_register(NULL, &ctx->parent_parent_ctx.hw);
	if (ret)
		return ret;

	return 0;
}

static void
clk_orphan_two_level_root_last_test_exit(struct kunit *test)
{
	struct clk_single_parent_two_lvl_ctx *ctx = test->priv;

	clk_hw_unregister(&ctx->hw);
	clk_hw_unregister(&ctx->parent_ctx.hw);
	clk_hw_unregister(&ctx->parent_parent_ctx.hw);
}

/*
 * Test that, for a clock whose parent used to be orphan, clk_get_rate()
 * will return the proper rate.
 */
static void
clk_orphan_two_level_root_last_test_get_rate(struct kunit *test)
{
	struct clk_single_parent_two_lvl_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	rate = clk_get_rate(clk);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_INIT_RATE);

	clk_put(clk);
}

/*
 * Test that, for a clock whose parent used to be orphan,
 * clk_set_rate_range() won't affect its rate if it is already within
 * range.
 *
 * See (for Exynos 4210):
 * https://lore.kernel.org/linux-clk/366a0232-bb4a-c357-6aa8-636e398e05eb@samsung.com/
 */
static void
clk_orphan_two_level_root_last_test_set_range(struct kunit *test)
{
	struct clk_single_parent_two_lvl_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;
	int ret;

	ret = clk_set_rate_range(clk,
				 DUMMY_CLOCK_INIT_RATE - 1000,
				 DUMMY_CLOCK_INIT_RATE + 1000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_INIT_RATE);

	clk_put(clk);
}

static struct kunit_case
clk_orphan_two_level_root_last_test_cases[] = {
	KUNIT_CASE(clk_orphan_two_level_root_last_test_get_rate),
	KUNIT_CASE(clk_orphan_two_level_root_last_test_set_range),
	{}
};

/*
 * Test suite for a basic, transparent, clock with a parent that is also
 * such a clock. The parent's parent is registered last, while the
 * parent and its child are registered in that order. The intermediate
 * and leaf clocks will thus be orphan when registered, but the leaf
 * clock itself will always have its parent and will never be
 * reparented. Indeed, it's only orphan because its parent is.
 *
 * These tests exercise the behaviour of the consumer API when dealing
 * with an orphan clock, and how we deal with the transition to a valid
 * parent.
 */
static struct kunit_suite
clk_orphan_two_level_root_last_test_suite = {
	.name = "clk-orphan-two-level-root-last-test",
	.init = clk_orphan_two_level_root_last_test_init,
	.exit = clk_orphan_two_level_root_last_test_exit,
	.test_cases = clk_orphan_two_level_root_last_test_cases,
};

/*
 * Test that clk_set_rate_range won't return an error for a valid range
 * and that it will make sure the rate of the clock is within the
 * boundaries.
 */
static void clk_range_test_set_range(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that calling clk_set_rate_range with a minimum rate higher than
 * the maximum rate returns an error.
 */
static void clk_range_test_set_range_invalid(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);

	KUNIT_EXPECT_LT(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1 + 1000,
					   DUMMY_CLOCK_RATE_1),
			0);

	clk_put(clk);
}

/*
 * Test that users can't set multiple, disjoints, range that would be
 * impossible to meet.
 */
static void clk_range_test_multiple_disjoints_range(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *user1, *user2;

	user1 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user1);

	user2 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user2);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user1, 1000, 2000),
			0);

	KUNIT_EXPECT_LT(test,
			clk_set_rate_range(user2, 3000, 4000),
			0);

	clk_put(user2);
	clk_put(user1);
}

/*
 * Test that if our clock has some boundaries and we try to round a rate
 * lower than the minimum, the returned rate will be within range.
 */
static void clk_range_test_set_range_round_rate_lower(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_1 - 1000);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that if our clock has some boundaries and we try to set a rate
 * higher than the maximum, the new rate will be within range.
 */
static void clk_range_test_set_range_set_rate_lower(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1 - 1000),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that if our clock has some boundaries and we try to round and
 * set a rate lower than the minimum, the rate returned by
 * clk_round_rate() will be consistent with the new rate set by
 * clk_set_rate().
 */
static void clk_range_test_set_range_set_round_rate_consistent_lower(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	long rounded;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rounded = clk_round_rate(clk, DUMMY_CLOCK_RATE_1 - 1000);
	KUNIT_ASSERT_GT(test, rounded, 0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1 - 1000),
			0);

	KUNIT_EXPECT_EQ(test, rounded, clk_get_rate(clk));

	clk_put(clk);
}

/*
 * Test that if our clock has some boundaries and we try to round a rate
 * higher than the maximum, the returned rate will be within range.
 */
static void clk_range_test_set_range_round_rate_higher(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_2 + 1000);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that if our clock has some boundaries and we try to set a rate
 * higher than the maximum, the new rate will be within range.
 */
static void clk_range_test_set_range_set_rate_higher(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_2 + 1000),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that if our clock has some boundaries and we try to round and
 * set a rate higher than the maximum, the rate returned by
 * clk_round_rate() will be consistent with the new rate set by
 * clk_set_rate().
 */
static void clk_range_test_set_range_set_round_rate_consistent_higher(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	long rounded;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rounded = clk_round_rate(clk, DUMMY_CLOCK_RATE_2 + 1000);
	KUNIT_ASSERT_GT(test, rounded, 0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_2 + 1000),
			0);

	KUNIT_EXPECT_EQ(test, rounded, clk_get_rate(clk));

	clk_put(clk);
}

/*
 * Test that if our clock has a rate lower than the minimum set by a
 * call to clk_set_rate_range(), the rate will be raised to match the
 * new minimum.
 *
 * This assumes that clk_ops.determine_rate or clk_ops.round_rate won't
 * modify the requested rate, which is our case in clk_dummy_rate_ops.
 */
static void clk_range_test_set_range_get_rate_raised(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1 - 1000),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	clk_put(clk);
}

/*
 * Test that if our clock has a rate higher than the maximum set by a
 * call to clk_set_rate_range(), the rate will be lowered to match the
 * new maximum.
 *
 * This assumes that clk_ops.determine_rate or clk_ops.round_rate won't
 * modify the requested rate, which is our case in clk_dummy_rate_ops.
 */
static void clk_range_test_set_range_get_rate_lowered(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_2 + 1000),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

static struct kunit_case clk_range_test_cases[] = {
	KUNIT_CASE(clk_range_test_set_range),
	KUNIT_CASE(clk_range_test_set_range_invalid),
	KUNIT_CASE(clk_range_test_multiple_disjoints_range),
	KUNIT_CASE(clk_range_test_set_range_round_rate_lower),
	KUNIT_CASE(clk_range_test_set_range_set_rate_lower),
	KUNIT_CASE(clk_range_test_set_range_set_round_rate_consistent_lower),
	KUNIT_CASE(clk_range_test_set_range_round_rate_higher),
	KUNIT_CASE(clk_range_test_set_range_set_rate_higher),
	KUNIT_CASE(clk_range_test_set_range_set_round_rate_consistent_higher),
	KUNIT_CASE(clk_range_test_set_range_get_rate_raised),
	KUNIT_CASE(clk_range_test_set_range_get_rate_lowered),
	{}
};

/*
 * Test suite for a basic rate clock, without any parent.
 *
 * These tests exercise the rate range API: clk_set_rate_range(),
 * clk_set_min_rate(), clk_set_max_rate(), clk_drop_range().
 */
static struct kunit_suite clk_range_test_suite = {
	.name = "clk-range-test",
	.init = clk_test_init,
	.exit = clk_test_exit,
	.test_cases = clk_range_test_cases,
};

/*
 * Test that if we have several subsequent calls to
 * clk_set_rate_range(), the core will reevaluate whether a new rate is
 * needed each and every time.
 *
 * With clk_dummy_maximize_rate_ops, this means that the rate will
 * trail along the maximum as it evolves.
 */
static void clk_range_test_set_range_rate_maximized(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_2 + 1000),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2 - 1000),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2 - 1000);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(clk);
}

/*
 * Test that if we have several subsequent calls to
 * clk_set_rate_range(), across multiple users, the core will reevaluate
 * whether a new rate is needed each and every time.
 *
 * With clk_dummy_maximize_rate_ops, this means that the rate will
 * trail along the maximum as it evolves.
 */
static void clk_range_test_multiple_set_range_rate_maximized(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *user1, *user2;
	unsigned long rate;

	user1 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user1);

	user2 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user2);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_2 + 1000),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user1,
					   0,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user2,
					   0,
					   DUMMY_CLOCK_RATE_1),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	KUNIT_ASSERT_EQ(test,
			clk_drop_range(user2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(user2);
	clk_put(user1);
	clk_put(clk);
}

/*
 * Test that if we have several subsequent calls to
 * clk_set_rate_range(), across multiple users, the core will reevaluate
 * whether a new rate is needed, including when a user drop its clock.
 *
 * With clk_dummy_maximize_rate_ops, this means that the rate will
 * trail along the maximum as it evolves.
 */
static void clk_range_test_multiple_set_range_rate_put_maximized(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *user1, *user2;
	unsigned long rate;

	user1 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user1);

	user2 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user2);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_2 + 1000),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user1,
					   0,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user2,
					   0,
					   DUMMY_CLOCK_RATE_1),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	clk_put(user2);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(user1);
	clk_put(clk);
}

static struct kunit_case clk_range_maximize_test_cases[] = {
	KUNIT_CASE(clk_range_test_set_range_rate_maximized),
	KUNIT_CASE(clk_range_test_multiple_set_range_rate_maximized),
	KUNIT_CASE(clk_range_test_multiple_set_range_rate_put_maximized),
	{}
};

/*
 * Test suite for a basic rate clock, without any parent.
 *
 * These tests exercise the rate range API: clk_set_rate_range(),
 * clk_set_min_rate(), clk_set_max_rate(), clk_drop_range(), with a
 * driver that will always try to run at the highest possible rate.
 */
static struct kunit_suite clk_range_maximize_test_suite = {
	.name = "clk-range-maximize-test",
	.init = clk_maximize_test_init,
	.exit = clk_test_exit,
	.test_cases = clk_range_maximize_test_cases,
};

/*
 * Test that if we have several subsequent calls to
 * clk_set_rate_range(), the core will reevaluate whether a new rate is
 * needed each and every time.
 *
 * With clk_dummy_minimize_rate_ops, this means that the rate will
 * trail along the minimum as it evolves.
 */
static void clk_range_test_set_range_rate_minimized(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1 - 1000),
			0);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1 + 1000,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1 + 1000);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1,
					   DUMMY_CLOCK_RATE_2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	clk_put(clk);
}

/*
 * Test that if we have several subsequent calls to
 * clk_set_rate_range(), across multiple users, the core will reevaluate
 * whether a new rate is needed each and every time.
 *
 * With clk_dummy_minimize_rate_ops, this means that the rate will
 * trail along the minimum as it evolves.
 */
static void clk_range_test_multiple_set_range_rate_minimized(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *user1, *user2;
	unsigned long rate;

	user1 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user1);

	user2 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user2);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user1,
					   DUMMY_CLOCK_RATE_1,
					   ULONG_MAX),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user2,
					   DUMMY_CLOCK_RATE_2,
					   ULONG_MAX),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	KUNIT_ASSERT_EQ(test,
			clk_drop_range(user2),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	clk_put(user2);
	clk_put(user1);
	clk_put(clk);
}

/*
 * Test that if we have several subsequent calls to
 * clk_set_rate_range(), across multiple users, the core will reevaluate
 * whether a new rate is needed, including when a user drop its clock.
 *
 * With clk_dummy_minimize_rate_ops, this means that the rate will
 * trail along the minimum as it evolves.
 */
static void clk_range_test_multiple_set_range_rate_put_minimized(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *user1, *user2;
	unsigned long rate;

	user1 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user1);

	user2 = clk_hw_get_clk(hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, user2);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user1,
					   DUMMY_CLOCK_RATE_1,
					   ULONG_MAX),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate_range(user2,
					   DUMMY_CLOCK_RATE_2,
					   ULONG_MAX),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(user2);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	clk_put(user1);
	clk_put(clk);
}

static struct kunit_case clk_range_minimize_test_cases[] = {
	KUNIT_CASE(clk_range_test_set_range_rate_minimized),
	KUNIT_CASE(clk_range_test_multiple_set_range_rate_minimized),
	KUNIT_CASE(clk_range_test_multiple_set_range_rate_put_minimized),
	{}
};

/*
 * Test suite for a basic rate clock, without any parent.
 *
 * These tests exercise the rate range API: clk_set_rate_range(),
 * clk_set_min_rate(), clk_set_max_rate(), clk_drop_range(), with a
 * driver that will always try to run at the lowest possible rate.
 */
static struct kunit_suite clk_range_minimize_test_suite = {
	.name = "clk-range-minimize-test",
	.init = clk_minimize_test_init,
	.exit = clk_test_exit,
	.test_cases = clk_range_minimize_test_cases,
};

struct clk_leaf_mux_ctx {
	struct clk_multiple_parent_ctx mux_ctx;
	struct clk_hw hw;
	struct clk_hw parent;
	struct clk_rate_request *req;
	int (*determine_rate_func)(struct clk_hw *hw, struct clk_rate_request *req);
};

static int clk_leaf_mux_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct clk_leaf_mux_ctx *ctx = container_of(hw, struct clk_leaf_mux_ctx, hw);
	int ret;
	struct clk_rate_request *parent_req = ctx->req;

	clk_hw_forward_rate_request(hw, req, req->best_parent_hw, parent_req, req->rate);
	ret = ctx->determine_rate_func(req->best_parent_hw, parent_req);
	if (ret)
		return ret;

	req->rate = parent_req->rate;

	return 0;
}

static const struct clk_ops clk_leaf_mux_set_rate_parent_ops = {
	.determine_rate = clk_leaf_mux_determine_rate,
	.set_parent = clk_dummy_single_set_parent,
	.get_parent = clk_dummy_single_get_parent,
};

static int
clk_leaf_mux_set_rate_parent_test_init(struct kunit *test)
{
	struct clk_leaf_mux_ctx *ctx;
	const char *top_parents[2] = { "parent-0", "parent-1" };
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->mux_ctx.parents_ctx[0].hw.init = CLK_HW_INIT_NO_PARENT("parent-0",
								    &clk_dummy_rate_ops,
								    0);
	ctx->mux_ctx.parents_ctx[0].rate = DUMMY_CLOCK_RATE_1;
	ret = clk_hw_register(NULL, &ctx->mux_ctx.parents_ctx[0].hw);
	if (ret)
		return ret;

	ctx->mux_ctx.parents_ctx[1].hw.init = CLK_HW_INIT_NO_PARENT("parent-1",
								    &clk_dummy_rate_ops,
								    0);
	ctx->mux_ctx.parents_ctx[1].rate = DUMMY_CLOCK_RATE_2;
	ret = clk_hw_register(NULL, &ctx->mux_ctx.parents_ctx[1].hw);
	if (ret)
		return ret;

	ctx->mux_ctx.current_parent = 0;
	ctx->mux_ctx.hw.init = CLK_HW_INIT_PARENTS("test-mux", top_parents,
						   &clk_multiple_parents_mux_ops,
						   0);
	ret = clk_hw_register(NULL, &ctx->mux_ctx.hw);
	if (ret)
		return ret;

	ctx->parent.init = CLK_HW_INIT_HW("test-parent", &ctx->mux_ctx.hw,
					  &empty_clk_ops, CLK_SET_RATE_PARENT);
	ret = clk_hw_register(NULL, &ctx->parent);
	if (ret)
		return ret;

	ctx->hw.init = CLK_HW_INIT_HW("test-clock", &ctx->parent,
				      &clk_leaf_mux_set_rate_parent_ops,
				      CLK_SET_RATE_PARENT);
	ret = clk_hw_register(NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

static void clk_leaf_mux_set_rate_parent_test_exit(struct kunit *test)
{
	struct clk_leaf_mux_ctx *ctx = test->priv;

	clk_hw_unregister(&ctx->hw);
	clk_hw_unregister(&ctx->parent);
	clk_hw_unregister(&ctx->mux_ctx.hw);
	clk_hw_unregister(&ctx->mux_ctx.parents_ctx[0].hw);
	clk_hw_unregister(&ctx->mux_ctx.parents_ctx[1].hw);
}

struct clk_leaf_mux_set_rate_parent_determine_rate_test_case {
	const char *desc;
	int (*determine_rate_func)(struct clk_hw *hw, struct clk_rate_request *req);
};

static void
clk_leaf_mux_set_rate_parent_determine_rate_test_case_to_desc(
		const struct clk_leaf_mux_set_rate_parent_determine_rate_test_case *t, char *desc)
{
	strcpy(desc, t->desc);
}

static const struct clk_leaf_mux_set_rate_parent_determine_rate_test_case
clk_leaf_mux_set_rate_parent_determine_rate_test_cases[] = {
	{
		/*
		 * Test that __clk_determine_rate() on the parent that can't
		 * change rate doesn't return a clk_rate_request structure with
		 * the best_parent_hw pointer pointing to the parent.
		 */
		.desc = "clk_leaf_mux_set_rate_parent__clk_determine_rate_proper_parent",
		.determine_rate_func = __clk_determine_rate,
	},
	{
		/*
		 * Test that __clk_mux_determine_rate() on the parent that
		 * can't change rate doesn't return a clk_rate_request
		 * structure with the best_parent_hw pointer pointing to
		 * the parent.
		 */
		.desc = "clk_leaf_mux_set_rate_parent__clk_mux_determine_rate_proper_parent",
		.determine_rate_func = __clk_mux_determine_rate,
	},
	{
		/*
		 * Test that __clk_mux_determine_rate_closest() on the parent
		 * that can't change rate doesn't return a clk_rate_request
		 * structure with the best_parent_hw pointer pointing to
		 * the parent.
		 */
		.desc = "clk_leaf_mux_set_rate_parent__clk_mux_determine_rate_closest_proper_parent",
		.determine_rate_func = __clk_mux_determine_rate_closest,
	},
	{
		/*
		 * Test that clk_hw_determine_rate_no_reparent() on the parent
		 * that can't change rate doesn't return a clk_rate_request
		 * structure with the best_parent_hw pointer pointing to
		 * the parent.
		 */
		.desc = "clk_leaf_mux_set_rate_parent_clk_hw_determine_rate_no_reparent_proper_parent",
		.determine_rate_func = clk_hw_determine_rate_no_reparent,
	},
};

KUNIT_ARRAY_PARAM(clk_leaf_mux_set_rate_parent_determine_rate_test,
		  clk_leaf_mux_set_rate_parent_determine_rate_test_cases,
		  clk_leaf_mux_set_rate_parent_determine_rate_test_case_to_desc)

/*
 * Test that when a clk that can't change rate itself calls a function like
 * __clk_determine_rate() on its parent it doesn't get back a clk_rate_request
 * structure that has the best_parent_hw pointer point to the clk_hw passed
 * into the determine rate function. See commit 262ca38f4b6e ("clk: Stop
 * forwarding clk_rate_requests to the parent") for more background.
 */
static void clk_leaf_mux_set_rate_parent_determine_rate_test(struct kunit *test)
{
	struct clk_leaf_mux_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk_rate_request req;
	unsigned long rate;
	const struct clk_leaf_mux_set_rate_parent_determine_rate_test_case *test_param;

	test_param = test->param_value;
	ctx->determine_rate_func = test_param->determine_rate_func;

	ctx->req = &req;
	rate = clk_get_rate(clk);
	KUNIT_ASSERT_EQ(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_ASSERT_EQ(test, DUMMY_CLOCK_RATE_2, clk_round_rate(clk, DUMMY_CLOCK_RATE_2));

	KUNIT_EXPECT_EQ(test, req.rate, DUMMY_CLOCK_RATE_2);
	KUNIT_EXPECT_EQ(test, req.best_parent_rate, DUMMY_CLOCK_RATE_2);
	KUNIT_EXPECT_PTR_EQ(test, req.best_parent_hw, &ctx->mux_ctx.hw);

	clk_put(clk);
}

static struct kunit_case clk_leaf_mux_set_rate_parent_test_cases[] = {
	KUNIT_CASE_PARAM(clk_leaf_mux_set_rate_parent_determine_rate_test,
			 clk_leaf_mux_set_rate_parent_determine_rate_test_gen_params),
	{}
};

/*
 * Test suite for a clock whose parent is a pass-through clk whose parent is a
 * mux with multiple parents. The leaf and pass-through clocks have the
 * CLK_SET_RATE_PARENT flag, and will forward rate requests to the mux, which
 * will then select which parent is the best fit for a given rate.
 *
 * These tests exercise the behaviour of muxes, and the proper selection
 * of parents.
 */
static struct kunit_suite clk_leaf_mux_set_rate_parent_test_suite = {
	.name = "clk-leaf-mux-set-rate-parent",
	.init = clk_leaf_mux_set_rate_parent_test_init,
	.exit = clk_leaf_mux_set_rate_parent_test_exit,
	.test_cases = clk_leaf_mux_set_rate_parent_test_cases,
};

struct clk_mux_notifier_rate_change {
	bool done;
	unsigned long old_rate;
	unsigned long new_rate;
	wait_queue_head_t wq;
};

struct clk_mux_notifier_ctx {
	struct clk_multiple_parent_ctx mux_ctx;
	struct clk *clk;
	struct notifier_block clk_nb;
	struct clk_mux_notifier_rate_change pre_rate_change;
	struct clk_mux_notifier_rate_change post_rate_change;
};

#define NOTIFIER_TIMEOUT_MS 100

static int clk_mux_notifier_callback(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct clk_notifier_data *clk_data = data;
	struct clk_mux_notifier_ctx *ctx = container_of(nb,
							struct clk_mux_notifier_ctx,
							clk_nb);

	if (action & PRE_RATE_CHANGE) {
		ctx->pre_rate_change.old_rate = clk_data->old_rate;
		ctx->pre_rate_change.new_rate = clk_data->new_rate;
		ctx->pre_rate_change.done = true;
		wake_up_interruptible(&ctx->pre_rate_change.wq);
	}

	if (action & POST_RATE_CHANGE) {
		ctx->post_rate_change.old_rate = clk_data->old_rate;
		ctx->post_rate_change.new_rate = clk_data->new_rate;
		ctx->post_rate_change.done = true;
		wake_up_interruptible(&ctx->post_rate_change.wq);
	}

	return 0;
}

static int clk_mux_notifier_test_init(struct kunit *test)
{
	struct clk_mux_notifier_ctx *ctx;
	const char *top_parents[2] = { "parent-0", "parent-1" };
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;
	ctx->clk_nb.notifier_call = clk_mux_notifier_callback;
	init_waitqueue_head(&ctx->pre_rate_change.wq);
	init_waitqueue_head(&ctx->post_rate_change.wq);

	ctx->mux_ctx.parents_ctx[0].hw.init = CLK_HW_INIT_NO_PARENT("parent-0",
								    &clk_dummy_rate_ops,
								    0);
	ctx->mux_ctx.parents_ctx[0].rate = DUMMY_CLOCK_RATE_1;
	ret = clk_hw_register(NULL, &ctx->mux_ctx.parents_ctx[0].hw);
	if (ret)
		return ret;

	ctx->mux_ctx.parents_ctx[1].hw.init = CLK_HW_INIT_NO_PARENT("parent-1",
								    &clk_dummy_rate_ops,
								    0);
	ctx->mux_ctx.parents_ctx[1].rate = DUMMY_CLOCK_RATE_2;
	ret = clk_hw_register(NULL, &ctx->mux_ctx.parents_ctx[1].hw);
	if (ret)
		return ret;

	ctx->mux_ctx.current_parent = 0;
	ctx->mux_ctx.hw.init = CLK_HW_INIT_PARENTS("test-mux", top_parents,
						   &clk_multiple_parents_mux_ops,
						   0);
	ret = clk_hw_register(NULL, &ctx->mux_ctx.hw);
	if (ret)
		return ret;

	ctx->clk = clk_hw_get_clk(&ctx->mux_ctx.hw, NULL);
	ret = clk_notifier_register(ctx->clk, &ctx->clk_nb);
	if (ret)
		return ret;

	return 0;
}

static void clk_mux_notifier_test_exit(struct kunit *test)
{
	struct clk_mux_notifier_ctx *ctx = test->priv;
	struct clk *clk = ctx->clk;

	clk_notifier_unregister(clk, &ctx->clk_nb);
	clk_put(clk);

	clk_hw_unregister(&ctx->mux_ctx.hw);
	clk_hw_unregister(&ctx->mux_ctx.parents_ctx[0].hw);
	clk_hw_unregister(&ctx->mux_ctx.parents_ctx[1].hw);
}

/*
 * Test that if the we have a notifier registered on a mux, the core
 * will notify us when we switch to another parent, and with the proper
 * old and new rates.
 */
static void clk_mux_notifier_set_parent_test(struct kunit *test)
{
	struct clk_mux_notifier_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->mux_ctx.hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *new_parent = clk_hw_get_clk(&ctx->mux_ctx.parents_ctx[1].hw, NULL);
	int ret;

	ret = clk_set_parent(clk, new_parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = wait_event_interruptible_timeout(ctx->pre_rate_change.wq,
					       ctx->pre_rate_change.done,
					       msecs_to_jiffies(NOTIFIER_TIMEOUT_MS));
	KUNIT_ASSERT_GT(test, ret, 0);

	KUNIT_EXPECT_EQ(test, ctx->pre_rate_change.old_rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_EQ(test, ctx->pre_rate_change.new_rate, DUMMY_CLOCK_RATE_2);

	ret = wait_event_interruptible_timeout(ctx->post_rate_change.wq,
					       ctx->post_rate_change.done,
					       msecs_to_jiffies(NOTIFIER_TIMEOUT_MS));
	KUNIT_ASSERT_GT(test, ret, 0);

	KUNIT_EXPECT_EQ(test, ctx->post_rate_change.old_rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_EQ(test, ctx->post_rate_change.new_rate, DUMMY_CLOCK_RATE_2);

	clk_put(new_parent);
	clk_put(clk);
}

static struct kunit_case clk_mux_notifier_test_cases[] = {
	KUNIT_CASE(clk_mux_notifier_set_parent_test),
	{}
};

/*
 * Test suite for a mux with multiple parents, and a notifier registered
 * on the mux.
 *
 * These tests exercise the behaviour of notifiers.
 */
static struct kunit_suite clk_mux_notifier_test_suite = {
	.name = "clk-mux-notifier",
	.init = clk_mux_notifier_test_init,
	.exit = clk_mux_notifier_test_exit,
	.test_cases = clk_mux_notifier_test_cases,
};

static int
clk_mux_no_reparent_test_init(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx;
	const char *parents[2] = { "parent-0", "parent-1"};
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->parents_ctx[0].hw.init = CLK_HW_INIT_NO_PARENT("parent-0",
							    &clk_dummy_rate_ops,
							    0);
	ctx->parents_ctx[0].rate = DUMMY_CLOCK_RATE_1;
	ret = clk_hw_register(NULL, &ctx->parents_ctx[0].hw);
	if (ret)
		return ret;

	ctx->parents_ctx[1].hw.init = CLK_HW_INIT_NO_PARENT("parent-1",
							    &clk_dummy_rate_ops,
							    0);
	ctx->parents_ctx[1].rate = DUMMY_CLOCK_RATE_2;
	ret = clk_hw_register(NULL, &ctx->parents_ctx[1].hw);
	if (ret)
		return ret;

	ctx->current_parent = 0;
	ctx->hw.init = CLK_HW_INIT_PARENTS("test-mux", parents,
					   &clk_multiple_parents_no_reparent_mux_ops,
					   0);
	ret = clk_hw_register(NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

static void
clk_mux_no_reparent_test_exit(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;

	clk_hw_unregister(&ctx->hw);
	clk_hw_unregister(&ctx->parents_ctx[0].hw);
	clk_hw_unregister(&ctx->parents_ctx[1].hw);
}

/*
 * Test that if the we have a mux that cannot change parent and we call
 * clk_round_rate() on it with a rate that should cause it to change
 * parent, it won't.
 */
static void clk_mux_no_reparent_round_rate(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *other_parent, *parent;
	unsigned long other_parent_rate;
	unsigned long parent_rate;
	long rounded_rate;

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	parent_rate = clk_get_rate(parent);
	KUNIT_ASSERT_GT(test, parent_rate, 0);

	other_parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, other_parent);
	KUNIT_ASSERT_FALSE(test, clk_is_match(parent, other_parent));

	other_parent_rate = clk_get_rate(other_parent);
	KUNIT_ASSERT_GT(test, other_parent_rate, 0);
	clk_put(other_parent);

	rounded_rate = clk_round_rate(clk, other_parent_rate);
	KUNIT_ASSERT_GT(test, rounded_rate, 0);
	KUNIT_EXPECT_EQ(test, rounded_rate, parent_rate);

	clk_put(clk);
}

/*
 * Test that if the we have a mux that cannot change parent and we call
 * clk_set_rate() on it with a rate that should cause it to change
 * parent, it won't.
 */
static void clk_mux_no_reparent_set_rate(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *other_parent, *parent;
	unsigned long other_parent_rate;
	unsigned long parent_rate;
	unsigned long rate;
	int ret;

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	parent_rate = clk_get_rate(parent);
	KUNIT_ASSERT_GT(test, parent_rate, 0);

	other_parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, other_parent);
	KUNIT_ASSERT_FALSE(test, clk_is_match(parent, other_parent));

	other_parent_rate = clk_get_rate(other_parent);
	KUNIT_ASSERT_GT(test, other_parent_rate, 0);
	clk_put(other_parent);

	ret = clk_set_rate(clk, other_parent_rate);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, parent_rate);

	clk_put(clk);
}

static struct kunit_case clk_mux_no_reparent_test_cases[] = {
	KUNIT_CASE(clk_mux_no_reparent_round_rate),
	KUNIT_CASE(clk_mux_no_reparent_set_rate),
	{}
};

/*
 * Test suite for a clock mux that isn't allowed to change parent, using
 * the clk_hw_determine_rate_no_reparent() helper.
 *
 * These tests exercise that helper, and the proper selection of
 * rates and parents.
 */
static struct kunit_suite clk_mux_no_reparent_test_suite = {
	.name = "clk-mux-no-reparent",
	.init = clk_mux_no_reparent_test_init,
	.exit = clk_mux_no_reparent_test_exit,
	.test_cases = clk_mux_no_reparent_test_cases,
};

struct clk_register_clk_parent_data_test_case {
	const char *desc;
	struct clk_parent_data pdata;
};

static void
clk_register_clk_parent_data_test_case_to_desc(
		const struct clk_register_clk_parent_data_test_case *t, char *desc)
{
	strcpy(desc, t->desc);
}

static const struct clk_register_clk_parent_data_test_case
clk_register_clk_parent_data_of_cases[] = {
	{
		/*
		 * Test that a clk registered with a struct device_node can
		 * find a parent based on struct clk_parent_data::index.
		 */
		.desc = "clk_parent_data_of_index_test",
		.pdata.index = 0,
	},
	{
		/*
		 * Test that a clk registered with a struct device_node can
		 * find a parent based on struct clk_parent_data::fwname.
		 */
		.desc = "clk_parent_data_of_fwname_test",
		.pdata.fw_name = CLK_PARENT_DATA_PARENT1,
	},
	{
		/*
		 * Test that a clk registered with a struct device_node can
		 * find a parent based on struct clk_parent_data::name.
		 */
		.desc = "clk_parent_data_of_name_test",
		/* The index must be negative to indicate firmware not used */
		.pdata.index = -1,
		.pdata.name = CLK_PARENT_DATA_1MHZ_NAME,
	},
	{
		/*
		 * Test that a clk registered with a struct device_node can
		 * find a parent based on struct
		 * clk_parent_data::{fw_name,name}.
		 */
		.desc = "clk_parent_data_of_fwname_name_test",
		.pdata.fw_name = CLK_PARENT_DATA_PARENT1,
		.pdata.name = "not_matching",
	},
	{
		/*
		 * Test that a clk registered with a struct device_node can
		 * find a parent based on struct clk_parent_data::{index,name}.
		 * Index takes priority.
		 */
		.desc = "clk_parent_data_of_index_name_priority_test",
		.pdata.index = 0,
		.pdata.name = "not_matching",
	},
	{
		/*
		 * Test that a clk registered with a struct device_node can
		 * find a parent based on struct
		 * clk_parent_data::{index,fwname,name}. The fw_name takes
		 * priority over index and name.
		 */
		.desc = "clk_parent_data_of_index_fwname_name_priority_test",
		.pdata.index = 1,
		.pdata.fw_name = CLK_PARENT_DATA_PARENT1,
		.pdata.name = "not_matching",
	},
};

KUNIT_ARRAY_PARAM(clk_register_clk_parent_data_of_test, clk_register_clk_parent_data_of_cases,
		  clk_register_clk_parent_data_test_case_to_desc)

/**
 * struct clk_register_clk_parent_data_of_ctx - Context for clk_parent_data OF tests
 * @np: device node of clk under test
 * @hw: clk_hw for clk under test
 */
struct clk_register_clk_parent_data_of_ctx {
	struct device_node *np;
	struct clk_hw hw;
};

static int clk_register_clk_parent_data_of_test_init(struct kunit *test)
{
	struct clk_register_clk_parent_data_of_ctx *ctx;

	KUNIT_ASSERT_EQ(test, 0,
			of_overlay_apply_kunit(test, kunit_clk_parent_data_test));

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->np = of_find_compatible_node(NULL, NULL, "test,clk-parent-data");
	if (!ctx->np)
		return -ENODEV;

	of_node_put_kunit(test, ctx->np);

	return 0;
}

/*
 * Test that a clk registered with a struct device_node can find a parent based on
 * struct clk_parent_data when the hw member isn't set.
 */
static void clk_register_clk_parent_data_of_test(struct kunit *test)
{
	struct clk_register_clk_parent_data_of_ctx *ctx = test->priv;
	struct clk_hw *parent_hw;
	const struct clk_register_clk_parent_data_test_case *test_param;
	struct clk_init_data init = { };
	struct clk *expected_parent, *actual_parent;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->np);

	expected_parent = of_clk_get_kunit(test, ctx->np, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, expected_parent);

	test_param = test->param_value;
	init.parent_data = &test_param->pdata;
	init.num_parents = 1;
	init.name = "parent_data_of_test_clk";
	init.ops = &clk_dummy_single_parent_ops;
	ctx->hw.init = &init;
	KUNIT_ASSERT_EQ(test, 0, of_clk_hw_register_kunit(test, ctx->np, &ctx->hw));

	parent_hw = clk_hw_get_parent(&ctx->hw);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent_hw);

	actual_parent = clk_hw_get_clk_kunit(test, parent_hw, __func__);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, actual_parent);

	KUNIT_EXPECT_TRUE(test, clk_is_match(expected_parent, actual_parent));
}

static struct kunit_case clk_register_clk_parent_data_of_test_cases[] = {
	KUNIT_CASE_PARAM(clk_register_clk_parent_data_of_test,
			 clk_register_clk_parent_data_of_test_gen_params),
	{}
};

/*
 * Test suite for registering clks with struct clk_parent_data and a struct
 * device_node.
 */
static struct kunit_suite clk_register_clk_parent_data_of_suite = {
	.name = "clk_register_clk_parent_data_of",
	.init = clk_register_clk_parent_data_of_test_init,
	.test_cases = clk_register_clk_parent_data_of_test_cases,
};

/**
 * struct platform_driver_dev_ctx - Context to stash platform device
 * @dev: device under test
 * @pdrv: driver to attach to find @dev
 */
struct platform_driver_dev_ctx {
	struct device *dev;
	struct platform_driver pdrv;
};

static inline struct platform_driver_dev_ctx *
pdev_to_platform_driver_dev_ctx(struct platform_device *pdev)
{
	return container_of(to_platform_driver(pdev->dev.driver),
			    struct platform_driver_dev_ctx, pdrv);
}

static int kunit_platform_driver_dev_probe(struct platform_device *pdev)
{
	struct platform_driver_dev_ctx *ctx;

	ctx = pdev_to_platform_driver_dev_ctx(pdev);
	ctx->dev = &pdev->dev;

	return 0;
}

static struct device *
kunit_of_platform_driver_dev(struct kunit *test, const struct of_device_id *match_table)
{
	struct platform_driver_dev_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	ctx->pdrv.probe = kunit_platform_driver_dev_probe;
	ctx->pdrv.driver.of_match_table = match_table;
	ctx->pdrv.driver.name = __func__;
	ctx->pdrv.driver.owner = THIS_MODULE;

	KUNIT_ASSERT_EQ(test, 0, kunit_platform_driver_register(test, &ctx->pdrv));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->dev);

	return ctx->dev;
}

static const struct clk_register_clk_parent_data_test_case
clk_register_clk_parent_data_device_cases[] = {
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::index.
		 */
		.desc = "clk_parent_data_device_index_test",
		.pdata.index = 1,
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::fwname.
		 */
		.desc = "clk_parent_data_device_fwname_test",
		.pdata.fw_name = CLK_PARENT_DATA_PARENT2,
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::name.
		 */
		.desc = "clk_parent_data_device_name_test",
		/* The index must be negative to indicate firmware not used */
		.pdata.index = -1,
		.pdata.name = CLK_PARENT_DATA_50MHZ_NAME,
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::{fw_name,name}.
		 */
		.desc = "clk_parent_data_device_fwname_name_test",
		.pdata.fw_name = CLK_PARENT_DATA_PARENT2,
		.pdata.name = "not_matching",
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::{index,name}. Index
		 * takes priority.
		 */
		.desc = "clk_parent_data_device_index_name_priority_test",
		.pdata.index = 1,
		.pdata.name = "not_matching",
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::{index,fwname,name}.
		 * The fw_name takes priority over index and name.
		 */
		.desc = "clk_parent_data_device_index_fwname_name_priority_test",
		.pdata.index = 0,
		.pdata.fw_name = CLK_PARENT_DATA_PARENT2,
		.pdata.name = "not_matching",
	},
};

KUNIT_ARRAY_PARAM(clk_register_clk_parent_data_device_test,
		  clk_register_clk_parent_data_device_cases,
		  clk_register_clk_parent_data_test_case_to_desc)

/*
 * Test that a clk registered with a struct device can find a parent based on
 * struct clk_parent_data when the hw member isn't set.
 */
static void clk_register_clk_parent_data_device_test(struct kunit *test)
{
	struct device *dev;
	struct clk_hw *hw;
	const struct clk_register_clk_parent_data_test_case *test_param;
	struct clk_hw *parent_hw;
	struct clk_init_data init = { };
	struct clk *expected_parent, *actual_parent;
	static const struct of_device_id match_table[] = {
		{ .compatible = "test,clk-parent-data" },
		{ }
	};

	dev = kunit_of_platform_driver_dev(test, match_table);

	expected_parent = clk_get_kunit(test, dev, "50");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, expected_parent);

	hw = kunit_kzalloc(test, sizeof(*hw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	test_param = test->param_value;
	init.parent_data = &test_param->pdata;
	init.num_parents = 1;
	init.name = "parent_data_device_test_clk";
	init.ops = &clk_dummy_single_parent_ops;
	hw->init = &init;
	KUNIT_ASSERT_EQ(test, 0, clk_hw_register_kunit(test, dev, hw));

	parent_hw = clk_hw_get_parent(hw);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent_hw);

	actual_parent = clk_hw_get_clk_kunit(test, parent_hw, __func__);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, actual_parent);

	KUNIT_EXPECT_TRUE(test, clk_is_match(expected_parent, actual_parent));
}

static const struct clk_register_clk_parent_data_test_case
clk_register_clk_parent_data_device_hw_cases[] = {
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::hw.
		 */
		.desc = "clk_parent_data_device_hw_index_test",
		/* The index must be negative to indicate firmware not used */
		.pdata.index = -1,
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::hw when
		 * struct clk_parent_data::fw_name is set.
		 */
		.desc = "clk_parent_data_device_hw_fwname_test",
		.pdata.fw_name = CLK_PARENT_DATA_PARENT2,
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::hw when struct
		 * clk_parent_data::name is set.
		 */
		.desc = "clk_parent_data_device_hw_name_test",
		/* The index must be negative to indicate firmware not used */
		.pdata.index = -1,
		.pdata.name = CLK_PARENT_DATA_50MHZ_NAME,
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::hw when struct
		 * clk_parent_data::{fw_name,name} are set.
		 */
		.desc = "clk_parent_data_device_hw_fwname_name_test",
		.pdata.fw_name = CLK_PARENT_DATA_PARENT2,
		.pdata.name = "not_matching",
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::hw when struct
		 * clk_parent_data::index is set. The hw pointer takes
		 * priority.
		 */
		.desc = "clk_parent_data_device_hw_index_priority_test",
		.pdata.index = 0,
	},
	{
		/*
		 * Test that a clk registered with a struct device can find a
		 * parent based on struct clk_parent_data::hw when
		 * struct clk_parent_data::{index,fwname,name} are set.
		 * The hw pointer takes priority over everything else.
		 */
		.desc = "clk_parent_data_device_hw_index_fwname_name_priority_test",
		.pdata.index = 0,
		.pdata.fw_name = CLK_PARENT_DATA_PARENT2,
		.pdata.name = "not_matching",
	},
};

KUNIT_ARRAY_PARAM(clk_register_clk_parent_data_device_hw_test,
		  clk_register_clk_parent_data_device_hw_cases,
		  clk_register_clk_parent_data_test_case_to_desc)

/*
 * Test that a clk registered with a struct device can find a
 * parent based on struct clk_parent_data::hw.
 */
static void clk_register_clk_parent_data_device_hw_test(struct kunit *test)
{
	struct device *dev;
	struct clk_hw *hw;
	const struct clk_register_clk_parent_data_test_case *test_param;
	struct clk_dummy_context *parent;
	struct clk_hw *parent_hw;
	struct clk_parent_data pdata = { };
	struct clk_init_data init = { };
	static const struct of_device_id match_table[] = {
		{ .compatible = "test,clk-parent-data" },
		{ }
	};

	dev = kunit_of_platform_driver_dev(test, match_table);

	parent = kunit_kzalloc(test, sizeof(*parent), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	parent_hw = &parent->hw;
	parent_hw->init = CLK_HW_INIT_NO_PARENT("parent-clk",
						&clk_dummy_rate_ops, 0);

	KUNIT_ASSERT_EQ(test, 0, clk_hw_register_kunit(test, dev, parent_hw));

	hw = kunit_kzalloc(test, sizeof(*hw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	test_param = test->param_value;
	memcpy(&pdata, &test_param->pdata, sizeof(pdata));
	pdata.hw = parent_hw;
	init.parent_data = &pdata;
	init.num_parents = 1;
	init.ops = &clk_dummy_single_parent_ops;
	init.name = "parent_data_device_hw_test_clk";
	hw->init = &init;
	KUNIT_ASSERT_EQ(test, 0, clk_hw_register_kunit(test, dev, hw));

	KUNIT_EXPECT_PTR_EQ(test, parent_hw, clk_hw_get_parent(hw));
}

static struct kunit_case clk_register_clk_parent_data_device_test_cases[] = {
	KUNIT_CASE_PARAM(clk_register_clk_parent_data_device_test,
			 clk_register_clk_parent_data_device_test_gen_params),
	KUNIT_CASE_PARAM(clk_register_clk_parent_data_device_hw_test,
			 clk_register_clk_parent_data_device_hw_test_gen_params),
	{}
};

static int clk_register_clk_parent_data_device_init(struct kunit *test)
{
	KUNIT_ASSERT_EQ(test, 0,
			of_overlay_apply_kunit(test, kunit_clk_parent_data_test));

	return 0;
}

/*
 * Test suite for registering clks with struct clk_parent_data and a struct
 * device.
 */
static struct kunit_suite clk_register_clk_parent_data_device_suite = {
	.name = "clk_register_clk_parent_data_device",
	.init = clk_register_clk_parent_data_device_init,
	.test_cases = clk_register_clk_parent_data_device_test_cases,
};

struct clk_assigned_rates_context {
	struct clk_dummy_context clk0;
	struct clk_dummy_context clk1;
};

/*
 * struct clk_assigned_rates_test_param - Test parameters for clk_assigned_rates test
 * @desc: Test description
 * @overlay_begin: Pointer to start of DT overlay to apply for test
 * @overlay_end: Pointer to end of DT overlay to apply for test
 * @rate0: Initial rate of first clk
 * @rate1: Initial rate of second clk
 * @consumer_test: true if a consumer is being tested
 */
struct clk_assigned_rates_test_param {
	const char *desc;
	u8 *overlay_begin;
	u8 *overlay_end;
	unsigned long rate0;
	unsigned long rate1;
	bool consumer_test;
};

#define TEST_PARAM_OVERLAY(overlay_name)				\
	.overlay_begin = of_overlay_begin(overlay_name),		\
	.overlay_end = of_overlay_end(overlay_name)

static void
clk_assigned_rates_register_clk(struct kunit *test,
				struct clk_dummy_context *ctx,
				struct device_node *np, const char *name,
				unsigned long rate)
{
	struct clk_init_data init = { };

	init.name = name;
	init.ops = &clk_dummy_rate_ops;
	ctx->hw.init = &init;
	ctx->rate = rate;

	KUNIT_ASSERT_EQ(test, 0, of_clk_hw_register_kunit(test, np, &ctx->hw));
	KUNIT_ASSERT_EQ(test, ctx->rate, rate);
}

/*
 * Does most of the work of the test:
 *
 * 1. Apply the overlay to test
 * 2. Register the clk or clks to test
 * 3. Register the clk provider
 * 4. Apply clk defaults to the consumer device if this is a consumer test
 *
 * The tests will set different test_param values to test different scenarios
 * and validate that in their test functions.
 */
static int clk_assigned_rates_test_init(struct kunit *test)
{
	struct device_node *np, *consumer;
	struct clk_hw_onecell_data *data;
	struct clk_assigned_rates_context *ctx;
	u32 clk_cells;
	const struct clk_assigned_rates_test_param *test_param;

	test_param = test->param_value;

	KUNIT_ASSERT_EQ(test, 0, __of_overlay_apply_kunit(test,
							  test_param->overlay_begin,
							  test_param->overlay_end));

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test,
		ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL));
	test->priv = ctx;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test,
		np = of_find_compatible_node(NULL, NULL, "test,clk-assigned-rates"));
	of_node_put_kunit(test, np);

	KUNIT_ASSERT_EQ(test, 0, of_property_read_u32(np, "#clock-cells", &clk_cells));
	/* Only support #clock-cells = <0> or <1> */
	KUNIT_ASSERT_LT(test, clk_cells, 2);

	clk_assigned_rates_register_clk(test, &ctx->clk0, np,
					"test_assigned_rate0", test_param->rate0);
	if (clk_cells == 0) {
		KUNIT_ASSERT_EQ(test, 0,
				of_clk_add_hw_provider_kunit(test, np, of_clk_hw_simple_get,
							     &ctx->clk0.hw));
	} else if (clk_cells == 1) {
		clk_assigned_rates_register_clk(test, &ctx->clk1, np,
						"test_assigned_rate1", test_param->rate1);

		KUNIT_ASSERT_NOT_ERR_OR_NULL(test,
			data = kunit_kzalloc(test, struct_size(data, hws, 2), GFP_KERNEL));
		data->num = 2;
		data->hws[0] = &ctx->clk0.hw;
		data->hws[1] = &ctx->clk1.hw;

		KUNIT_ASSERT_EQ(test, 0,
				of_clk_add_hw_provider_kunit(test, np, of_clk_hw_onecell_get, data));
	}

	/* Consumers are optional */
	if (test_param->consumer_test) {
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test,
			consumer = of_find_compatible_node(NULL, NULL, "test,clk-consumer"));
		of_node_put_kunit(test, consumer);

		KUNIT_ASSERT_EQ(test, 0, of_clk_set_defaults(consumer, false));
	}

	return 0;
}

static void clk_assigned_rates_assigns_one(struct kunit *test)
{
	struct clk_assigned_rates_context *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, ctx->clk0.rate, ASSIGNED_RATES_0_RATE);
}

static void clk_assigned_rates_assigns_multiple(struct kunit *test)
{
	struct clk_assigned_rates_context *ctx = test->priv;

	KUNIT_EXPECT_EQ(test, ctx->clk0.rate, ASSIGNED_RATES_0_RATE);
	KUNIT_EXPECT_EQ(test, ctx->clk1.rate, ASSIGNED_RATES_1_RATE);
}

static void clk_assigned_rates_skips(struct kunit *test)
{
	struct clk_assigned_rates_context *ctx = test->priv;
	const struct clk_assigned_rates_test_param *test_param = test->param_value;

	KUNIT_EXPECT_NE(test, ctx->clk0.rate, ASSIGNED_RATES_0_RATE);
	KUNIT_EXPECT_EQ(test, ctx->clk0.rate, test_param->rate0);
}

OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_one);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_one_consumer);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_u64_one);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_u64_one_consumer);

/* Test cases that assign one rate */
static const struct clk_assigned_rates_test_param clk_assigned_rates_assigns_one_test_params[] = {
	{
		/*
		 * Test that a single cell assigned-clock-rates property
		 * assigns the rate when the property is in the provider.
		 */
		.desc = "provider assigns",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_one),
	},
	{
		/*
		 * Test that a single cell assigned-clock-rates property
		 * assigns the rate when the property is in the consumer.
		 */
		.desc = "consumer assigns",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_one_consumer),
		.consumer_test = true,
	},
	{
		/*
		 * Test that a single cell assigned-clock-rates-u64 property
		 * assigns the rate when the property is in the provider.
		 */
		.desc = "provider assigns u64",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_u64_one),
	},
	{
		/*
		 * Test that a single cell assigned-clock-rates-u64 property
		 * assigns the rate when the property is in the consumer.
		 */
		.desc = "consumer assigns u64",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_u64_one_consumer),
		.consumer_test = true,
	},
};
KUNIT_ARRAY_PARAM_DESC(clk_assigned_rates_assigns_one,
		       clk_assigned_rates_assigns_one_test_params, desc)

OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_multiple);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_multiple_consumer);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_u64_multiple);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_u64_multiple_consumer);

/* Test cases that assign multiple rates */
static const struct clk_assigned_rates_test_param clk_assigned_rates_assigns_multiple_test_params[] = {
	{
		/*
		 * Test that a multiple cell assigned-clock-rates property
		 * assigns the rates when the property is in the provider.
		 */
		.desc = "provider assigns",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_multiple),
	},
	{
		/*
		 * Test that a multiple cell assigned-clock-rates property
		 * assigns the rates when the property is in the consumer.
		 */
		.desc = "consumer assigns",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_multiple_consumer),
		.consumer_test = true,
	},
	{
		/*
		 * Test that a single cell assigned-clock-rates-u64 property
		 * assigns the rate when the property is in the provider.
		 */
		.desc = "provider assigns u64",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_u64_multiple),
	},
	{
		/*
		 * Test that a multiple cell assigned-clock-rates-u64 property
		 * assigns the rates when the property is in the consumer.
		 */
		.desc = "consumer assigns u64",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_u64_multiple_consumer),
		.consumer_test = true,
	},
};
KUNIT_ARRAY_PARAM_DESC(clk_assigned_rates_assigns_multiple,
		       clk_assigned_rates_assigns_multiple_test_params,
		       desc)

OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_without);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_without_consumer);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_zero);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_zero_consumer);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_null);
OF_OVERLAY_DECLARE(kunit_clk_assigned_rates_null_consumer);

/* Test cases that skip changing the rate due to malformed DT */
static const struct clk_assigned_rates_test_param clk_assigned_rates_skips_test_params[] = {
	{
		/*
		 * Test that an assigned-clock-rates property without an assigned-clocks
		 * property fails when the property is in the provider.
		 */
		.desc = "provider missing assigned-clocks",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_without),
		.rate0 = 3000,
	},
	{
		/*
		 * Test that an assigned-clock-rates property without an assigned-clocks
		 * property fails when the property is in the consumer.
		 */
		.desc = "consumer missing assigned-clocks",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_without_consumer),
		.rate0 = 3000,
		.consumer_test = true,
	},
	{
		/*
		 * Test that an assigned-clock-rates property of zero doesn't
		 * set a rate when the property is in the provider.
		 */
		.desc = "provider assigned-clock-rates of zero",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_zero),
		.rate0 = 3000,
	},
	{
		/*
		 * Test that an assigned-clock-rates property of zero doesn't
		 * set a rate when the property is in the consumer.
		 */
		.desc = "consumer assigned-clock-rates of zero",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_zero_consumer),
		.rate0 = 3000,
		.consumer_test = true,
	},
	{
		/*
		 * Test that an assigned-clocks property with a null phandle
		 * doesn't set a rate when the property is in the provider.
		 */
		.desc = "provider assigned-clocks null phandle",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_null),
		.rate0 = 3000,
	},
	{
		/*
		 * Test that an assigned-clocks property with a null phandle
		 * doesn't set a rate when the property is in the consumer.
		 */
		.desc = "provider assigned-clocks null phandle",
		TEST_PARAM_OVERLAY(kunit_clk_assigned_rates_null_consumer),
		.rate0 = 3000,
		.consumer_test = true,
	},
};
KUNIT_ARRAY_PARAM_DESC(clk_assigned_rates_skips,
		       clk_assigned_rates_skips_test_params,
		       desc)

static struct kunit_case clk_assigned_rates_test_cases[] = {
	KUNIT_CASE_PARAM(clk_assigned_rates_assigns_one,
			 clk_assigned_rates_assigns_one_gen_params),
	KUNIT_CASE_PARAM(clk_assigned_rates_assigns_multiple,
			 clk_assigned_rates_assigns_multiple_gen_params),
	KUNIT_CASE_PARAM(clk_assigned_rates_skips,
			 clk_assigned_rates_skips_gen_params),
	{}
};

/*
 * Test suite for assigned-clock-rates{-u64} DT property.
 */
static struct kunit_suite clk_assigned_rates_suite = {
	.name = "clk_assigned_rates",
	.test_cases = clk_assigned_rates_test_cases,
	.init = clk_assigned_rates_test_init,
};

static const struct clk_init_data clk_hw_get_dev_of_node_init_data = {
	.name = "clk_hw_get_dev_of_node",
	.ops = &empty_clk_ops,
};

/*
 * Test that a clk registered with a struct device returns the device from
 * clk_hw_get_dev() and the node from clk_hw_get_of_node()
 */
static void clk_hw_register_dev_get_dev_returns_dev(struct kunit *test)
{
	struct device *dev;
	struct clk_hw *hw;
	static const struct of_device_id match_table[] = {
		{ .compatible = "test,clk-hw-get-dev-of-node" },
		{ }
	};

	KUNIT_ASSERT_EQ(test, 0, of_overlay_apply_kunit(test, kunit_clk_hw_get_dev_of_node));

	dev = kunit_of_platform_driver_dev(test, match_table);

	hw = kunit_kzalloc(test, sizeof(*hw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	hw->init = &clk_hw_get_dev_of_node_init_data;
	KUNIT_ASSERT_EQ(test, 0, clk_hw_register_kunit(test, dev, hw));

	KUNIT_EXPECT_PTR_EQ(test, dev, clk_hw_get_dev(hw));
	KUNIT_EXPECT_PTR_EQ(test, dev_of_node(dev), clk_hw_get_of_node(hw));
}

/*
 * Test that a clk registered with a struct device that's not associated with
 * an OF node returns the device from clk_hw_get_dev() and NULL from
 * clk_hw_get_of_node()
 */
static void clk_hw_register_dev_no_node_get_dev_returns_dev(struct kunit *test)
{
	struct platform_device *pdev;
	struct device *dev;
	struct clk_hw *hw;

	pdev = kunit_platform_device_alloc(test, "clk_hw_register_dev_no_node", -1);
	KUNIT_ASSERT_NOT_NULL(test, pdev);
	KUNIT_ASSERT_EQ(test, 0, kunit_platform_device_add(test, pdev));
	dev = &pdev->dev;

	hw = kunit_kzalloc(test, sizeof(*hw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	hw->init = &clk_hw_get_dev_of_node_init_data;
	KUNIT_ASSERT_EQ(test, 0, clk_hw_register_kunit(test, dev, hw));

	KUNIT_EXPECT_PTR_EQ(test, dev, clk_hw_get_dev(hw));
	KUNIT_EXPECT_PTR_EQ(test, NULL, clk_hw_get_of_node(hw));
}

/*
 * Test that a clk registered without a struct device returns NULL from
 * clk_hw_get_dev()
 */
static void clk_hw_register_NULL_get_dev_of_node_returns_NULL(struct kunit *test)
{
	struct clk_hw *hw;

	hw = kunit_kzalloc(test, sizeof(*hw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	hw->init = &clk_hw_get_dev_of_node_init_data;

	KUNIT_ASSERT_EQ(test, 0, clk_hw_register_kunit(test, NULL, hw));

	KUNIT_EXPECT_PTR_EQ(test, NULL, clk_hw_get_dev(hw));
	KUNIT_EXPECT_PTR_EQ(test, NULL, clk_hw_get_of_node(hw));
}

/*
 * Test that a clk registered with an of_node returns the node from
 * clk_hw_get_of_node() and NULL from clk_hw_get_dev()
 */
static void of_clk_hw_register_node_get_of_node_returns_node(struct kunit *test)
{
	struct device_node *np;
	struct clk_hw *hw;

	hw = kunit_kzalloc(test, sizeof(*hw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	KUNIT_ASSERT_EQ(test, 0, of_overlay_apply_kunit(test, kunit_clk_hw_get_dev_of_node));

	np = of_find_compatible_node(NULL, NULL, "test,clk-hw-get-dev-of-node");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, np);
	of_node_put_kunit(test, np);

	hw->init = &clk_hw_get_dev_of_node_init_data;
	KUNIT_ASSERT_EQ(test, 0, of_clk_hw_register_kunit(test, np, hw));

	KUNIT_EXPECT_PTR_EQ(test, NULL, clk_hw_get_dev(hw));
	KUNIT_EXPECT_PTR_EQ(test, np, clk_hw_get_of_node(hw));
}

/*
 * Test that a clk registered without an of_node returns the node from
 * clk_hw_get_of_node() and clk_hw_get_dev()
 */
static void of_clk_hw_register_NULL_get_of_node_returns_NULL(struct kunit *test)
{
	struct clk_hw *hw;

	hw = kunit_kzalloc(test, sizeof(*hw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hw);

	hw->init = &clk_hw_get_dev_of_node_init_data;
	KUNIT_ASSERT_EQ(test, 0, of_clk_hw_register_kunit(test, NULL, hw));

	KUNIT_EXPECT_PTR_EQ(test, NULL, clk_hw_get_dev(hw));
	KUNIT_EXPECT_PTR_EQ(test, NULL, clk_hw_get_of_node(hw));
}

static struct kunit_case clk_hw_get_dev_of_node_test_cases[] = {
	KUNIT_CASE(clk_hw_register_dev_get_dev_returns_dev),
	KUNIT_CASE(clk_hw_register_dev_no_node_get_dev_returns_dev),
	KUNIT_CASE(clk_hw_register_NULL_get_dev_of_node_returns_NULL),
	KUNIT_CASE(of_clk_hw_register_node_get_of_node_returns_node),
	KUNIT_CASE(of_clk_hw_register_NULL_get_of_node_returns_NULL),
	{}
};

/*
 * Test suite to verify clk_hw_get_dev() and clk_hw_get_of_node() when clk
 * registered with clk_hw_register() and of_clk_hw_register()
 */
static struct kunit_suite clk_hw_get_dev_of_node_test_suite = {
	.name = "clk_hw_get_dev_of_node_test_suite",
	.test_cases = clk_hw_get_dev_of_node_test_cases,
};


kunit_test_suites(
	&clk_assigned_rates_suite,
	&clk_hw_get_dev_of_node_test_suite,
	&clk_leaf_mux_set_rate_parent_test_suite,
	&clk_test_suite,
	&clk_multiple_parents_mux_test_suite,
	&clk_mux_no_reparent_test_suite,
	&clk_mux_notifier_test_suite,
	&clk_orphan_transparent_multiple_parent_mux_test_suite,
	&clk_orphan_transparent_single_parent_test_suite,
	&clk_orphan_two_level_root_last_test_suite,
	&clk_range_test_suite,
	&clk_range_maximize_test_suite,
	&clk_range_minimize_test_suite,
	&clk_register_clk_parent_data_of_suite,
	&clk_register_clk_parent_data_device_suite,
	&clk_single_parent_mux_test_suite,
	&clk_uncached_test_suite,
);
MODULE_DESCRIPTION("Kunit tests for clk framework");
MODULE_LICENSE("GPL v2");
