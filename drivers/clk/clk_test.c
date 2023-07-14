// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit test for clk rate management
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>

/* Needed for clk_hw_get_clk() */
#include "clk.h"

#include <kunit/test.h>

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
 * Test that clk_round_rate and clk_set_rate are consitent and will
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
					   &clk_multiple_parents_mux_ops,
					   CLK_SET_RATE_PARENT);
	ret = clk_hw_register(NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

static void
clk_multiple_parents_mux_test_exit(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;

	clk_hw_unregister(&ctx->hw);
	clk_hw_unregister(&ctx->parents_ctx[0].hw);
	clk_hw_unregister(&ctx->parents_ctx[1].hw);
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
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent1, *parent2;
	unsigned long rate;
	int ret;

	kunit_skip(test, "This needs to be fixed in the core.");

	parent1 = clk_hw_get_clk(&ctx->parents_ctx[0].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent1);
	KUNIT_ASSERT_TRUE(test, clk_is_match(clk_get_parent(clk), parent1));

	parent2 = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
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

	clk_put(parent2);
	clk_put(parent1);
	clk_put(clk);
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
	.exit = clk_multiple_parents_mux_test_exit,
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
	ret = clk_hw_register(NULL, &ctx->parents_ctx[1].hw);
	if (ret)
		return ret;

	ctx->hw.init = CLK_HW_INIT_PARENTS("test-orphan-mux", parents,
					   &clk_multiple_parents_mux_ops,
					   CLK_SET_RATE_PARENT);
	ret = clk_hw_register(NULL, &ctx->hw);
	if (ret)
		return ret;

	return 0;
}

static void
clk_orphan_transparent_multiple_parent_mux_test_exit(struct kunit *test)
{
	struct clk_multiple_parent_ctx *ctx = test->priv;

	clk_hw_unregister(&ctx->hw);
	clk_hw_unregister(&ctx->parents_ctx[1].hw);
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
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	unsigned long rate;
	int ret;

	kunit_skip(test, "This needs to be fixed in the core.");

	clk_hw_set_rate_range(hw, DUMMY_CLOCK_RATE_1, DUMMY_CLOCK_RATE_2);

	parent = clk_hw_get_clk(&ctx->parents_ctx[1].hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent);

	ret = clk_set_parent(clk, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_GE(test, rate, DUMMY_CLOCK_RATE_1);
	KUNIT_EXPECT_LE(test, rate, DUMMY_CLOCK_RATE_2);

	clk_put(parent);
	clk_put(clk);
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
	.exit = clk_orphan_transparent_multiple_parent_mux_test_exit,
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

	ret = clk_hw_register(NULL, &ctx->parent_ctx.hw);
	if (ret)
		return ret;

	ctx->hw.init = CLK_HW_INIT("test-clk", "parent-clk",
				   &clk_dummy_single_parent_ops,
				   CLK_SET_RATE_PARENT);

	ret = clk_hw_register(NULL, &ctx->hw);
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
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	int ret;

	kunit_skip(test, "This needs to be fixed in the core.");

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	ret = clk_set_rate_range(parent, 1000, 2000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(clk, 3000, 4000);
	KUNIT_EXPECT_LT(test, ret, 0);

	clk_put(clk);
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
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk *parent;
	int ret;

	kunit_skip(test, "This needs to be fixed in the core.");

	parent = clk_get_parent(clk);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	ret = clk_set_rate_range(clk, 1000, 2000);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = clk_set_rate_range(parent, 3000, 4000);
	KUNIT_EXPECT_LT(test, ret, 0);

	clk_put(clk);
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
	.exit = clk_single_parent_mux_test_exit,
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

	ctx->hw.init = CLK_HW_INIT_HW("test-clock", &ctx->mux_ctx.hw,
				      &clk_dummy_single_parent_ops,
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
	clk_hw_unregister(&ctx->mux_ctx.hw);
	clk_hw_unregister(&ctx->mux_ctx.parents_ctx[0].hw);
	clk_hw_unregister(&ctx->mux_ctx.parents_ctx[1].hw);
}

/*
 * Test that, for a clock that will forward any rate request to its
 * parent, the rate request structure returned by __clk_determine_rate
 * is sane and will be what we expect.
 */
static void clk_leaf_mux_set_rate_parent_determine_rate(struct kunit *test)
{
	struct clk_leaf_mux_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = clk_hw_get_clk(hw, NULL);
	struct clk_rate_request req;
	unsigned long rate;
	int ret;

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_EQ(test, rate, DUMMY_CLOCK_RATE_1);

	clk_hw_init_rate_request(hw, &req, DUMMY_CLOCK_RATE_2);

	ret = __clk_determine_rate(hw, &req);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, req.rate, DUMMY_CLOCK_RATE_2);
	KUNIT_EXPECT_EQ(test, req.best_parent_rate, DUMMY_CLOCK_RATE_2);
	KUNIT_EXPECT_PTR_EQ(test, req.best_parent_hw, &ctx->mux_ctx.hw);

	clk_put(clk);
}

static struct kunit_case clk_leaf_mux_set_rate_parent_test_cases[] = {
	KUNIT_CASE(clk_leaf_mux_set_rate_parent_determine_rate),
	{}
};

/*
 * Test suite for a clock whose parent is a mux with multiple parents.
 * The leaf clock has CLK_SET_RATE_PARENT, and will forward rate
 * requests to the mux, which will then select which parent is the best
 * fit for a given rate.
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

kunit_test_suites(
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
	&clk_single_parent_mux_test_suite,
	&clk_uncached_test_suite
);
MODULE_LICENSE("GPL v2");
