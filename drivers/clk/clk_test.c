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
	.set_parent = clk_dummy_single_set_parent,
	.get_parent = clk_dummy_single_get_parent,
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
	struct clk *clk = hw->clk;
	unsigned long rate;

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, ctx->rate);
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
	struct clk *clk = hw->clk;
	unsigned long rate;

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1),
			0);

	rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, rate, 0);
	KUNIT_EXPECT_EQ(test, rate, DUMMY_CLOCK_RATE_1);
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
	struct clk *clk = hw->clk;
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
}

/*
 * Test that clk_round_rate and clk_set_rate are consitent and will
 * return the same frequency.
 */
static void clk_test_round_set_get_rate(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = hw->clk;
	unsigned long rounded_rate, set_rate;

	rounded_rate = clk_round_rate(clk, DUMMY_CLOCK_RATE_1);
	KUNIT_ASSERT_GT(test, rounded_rate, 0);
	KUNIT_EXPECT_EQ(test, rounded_rate, DUMMY_CLOCK_RATE_1);

	KUNIT_ASSERT_EQ(test,
			clk_set_rate(clk, DUMMY_CLOCK_RATE_1),
			0);

	set_rate = clk_get_rate(clk);
	KUNIT_ASSERT_GT(test, set_rate, 0);
	KUNIT_EXPECT_EQ(test, rounded_rate, set_rate);
}

static struct kunit_case clk_test_cases[] = {
	KUNIT_CASE(clk_test_get_rate),
	KUNIT_CASE(clk_test_set_get_rate),
	KUNIT_CASE(clk_test_set_set_get_rate),
	KUNIT_CASE(clk_test_round_set_get_rate),
	{}
};

static struct kunit_suite clk_test_suite = {
	.name = "clk-test",
	.init = clk_test_init,
	.exit = clk_test_exit,
	.test_cases = clk_test_cases,
};

struct clk_single_parent_ctx {
	struct clk_dummy_context parent_ctx;
	struct clk_hw hw;
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

static void clk_orphan_transparent_single_parent_mux_test_exit(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;

	clk_hw_unregister(&ctx->hw);
	clk_hw_unregister(&ctx->parent_ctx.hw);
}

/*
 * Test that a mux-only clock, with an initial rate within a range,
 * will still have the same rate after the range has been enforced.
 */
static void clk_test_orphan_transparent_parent_mux_set_range(struct kunit *test)
{
	struct clk_single_parent_ctx *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = hw->clk;
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
}

static struct kunit_case clk_orphan_transparent_single_parent_mux_test_cases[] = {
	KUNIT_CASE(clk_test_orphan_transparent_parent_mux_set_range),
	{}
};

static struct kunit_suite clk_orphan_transparent_single_parent_test_suite = {
	.name = "clk-orphan-transparent-single-parent-test",
	.init = clk_orphan_transparent_single_parent_mux_test_init,
	.exit = clk_orphan_transparent_single_parent_mux_test_exit,
	.test_cases = clk_orphan_transparent_single_parent_mux_test_cases,
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
	struct clk *clk = hw->clk;
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
}

/*
 * Test that calling clk_set_rate_range with a minimum rate higher than
 * the maximum rate returns an error.
 */
static void clk_range_test_set_range_invalid(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = hw->clk;

	KUNIT_EXPECT_LT(test,
			clk_set_rate_range(clk,
					   DUMMY_CLOCK_RATE_1 + 1000,
					   DUMMY_CLOCK_RATE_1),
			0);
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
	struct clk *clk = hw->clk;
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
}

/*
 * Test that if our clock has some boundaries and we try to set a rate
 * higher than the maximum, the new rate will be within range.
 */
static void clk_range_test_set_range_set_rate_lower(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = hw->clk;
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
	struct clk *clk = hw->clk;
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
}

/*
 * Test that if our clock has some boundaries and we try to round a rate
 * higher than the maximum, the returned rate will be within range.
 */
static void clk_range_test_set_range_round_rate_higher(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = hw->clk;
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
}

/*
 * Test that if our clock has some boundaries and we try to set a rate
 * higher than the maximum, the new rate will be within range.
 */
static void clk_range_test_set_range_set_rate_higher(struct kunit *test)
{
	struct clk_dummy_context *ctx = test->priv;
	struct clk_hw *hw = &ctx->hw;
	struct clk *clk = hw->clk;
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
	struct clk *clk = hw->clk;
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
	struct clk *clk = hw->clk;
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
	struct clk *clk = hw->clk;
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
	struct clk *clk = hw->clk;
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
	struct clk *clk = hw->clk;
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
}

static struct kunit_case clk_range_maximize_test_cases[] = {
	KUNIT_CASE(clk_range_test_set_range_rate_maximized),
	KUNIT_CASE(clk_range_test_multiple_set_range_rate_maximized),
	{}
};

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
	struct clk *clk = hw->clk;
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
	struct clk *clk = hw->clk;
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
}

static struct kunit_case clk_range_minimize_test_cases[] = {
	KUNIT_CASE(clk_range_test_set_range_rate_minimized),
	KUNIT_CASE(clk_range_test_multiple_set_range_rate_minimized),
	{}
};

static struct kunit_suite clk_range_minimize_test_suite = {
	.name = "clk-range-minimize-test",
	.init = clk_minimize_test_init,
	.exit = clk_test_exit,
	.test_cases = clk_range_minimize_test_cases,
};

kunit_test_suites(
	&clk_test_suite,
	&clk_orphan_transparent_single_parent_test_suite,
	&clk_range_test_suite,
	&clk_range_maximize_test_suite,
	&clk_range_minimize_test_suite
);
MODULE_LICENSE("GPL v2");
