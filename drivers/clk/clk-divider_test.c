// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for clk_divider_bestdiv()
 */
#include <kunit/test.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/limits.h>
#include <linux/units.h>

#define PARENT_RATE_1GHZ	GIGA
#define PARENT_RATE_2GHZ	(2 * GIGA)
#define PARENT_RATE_4GHZ	(4 * GIGA)

KUNIT_DEFINE_ACTION_WRAPPER(clk_hw_unregister_fixed_rate_wrapper,
			    clk_hw_unregister_fixed_rate, struct clk_hw *);
KUNIT_DEFINE_ACTION_WRAPPER(clk_hw_unregister_divider_wrapper,
			    clk_hw_unregister_divider, struct clk_hw *);
KUNIT_DEFINE_ACTION_WRAPPER(clk_hw_unregister_mux_wrapper,
			    clk_hw_unregister_mux, struct clk_hw *);

static const struct clk_div_table bestdiv_table[] = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 4 },
	{ .val = 2, .div = 8 },
	{ /* sentinel */ }
};

/*
 * Test that clk_round_rate(clk, ULONG_MAX) returns the maximum achievable
 * rate for a divider clock.
 */
static void clk_divider_bestdiv_ulong_max_returns_max_rate(struct kunit *test)
{
	struct clk_hw *parent_hw, *div_hw;
	unsigned long rate;
	u32 *fake_reg;

	fake_reg = kunit_kzalloc(test, sizeof(*fake_reg), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fake_reg);

	parent_hw = clk_hw_register_fixed_rate(NULL, "bestdiv-parent",
					       NULL, 0, PARENT_RATE_1GHZ);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent_hw);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, clk_hw_unregister_fixed_rate_wrapper,
						  parent_hw));

	div_hw = clk_hw_register_divider_table(NULL, "bestdiv-div",
					       "bestdiv-parent",
					       CLK_SET_RATE_PARENT,
					       (void __iomem __force *)fake_reg,
					       0, 2, 0, bestdiv_table, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, div_hw);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, clk_hw_unregister_divider_wrapper,
						  div_hw));

	/*
	 * ULONG_MAX is the canonical way to probe the maximum rate a clock
	 * can produce.
	 */
	rate = clk_hw_round_rate(div_hw, ULONG_MAX);
	KUNIT_EXPECT_EQ(test, rate, PARENT_RATE_1GHZ / 2);
}

/*
 * Test that clk_round_rate(clk, ULONG_MAX) returns the correct maximum rate
 * when a mux clock sits between a divider and its parent candidates.
 *
 * Topology:
 *
 *   [fixed 4 GHz] --\
 *                    +--> [mux CLK_SET_RATE_PARENT] --> [div {2,4,8} CLK_SET_RATE_PARENT]
 *   [fixed 2 GHz] --/
 *
 */
static void clk_divider_bestdiv_mux_ulong_max_returns_max_rate(struct kunit *test)
{
	static const char * const mux_parents[] = {
		"bestdiv-mux-parent-a",
		"bestdiv-mux-parent-b",
	};
	struct clk_hw *parent_a_hw, *parent_b_hw, *mux_hw, *div_hw;
	u32 *fake_reg_mux, *fake_reg_div;
	unsigned long rate;

	fake_reg_mux = kunit_kzalloc(test, sizeof(*fake_reg_mux), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fake_reg_mux);

	fake_reg_div = kunit_kzalloc(test, sizeof(*fake_reg_div), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fake_reg_div);

	/* Higher-rate parent: the mux should select this for ULONG_MAX. */
	parent_a_hw = clk_hw_register_fixed_rate(NULL, "bestdiv-mux-parent-a",
						 NULL, 0, PARENT_RATE_4GHZ);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent_a_hw);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, clk_hw_unregister_fixed_rate_wrapper,
						  parent_a_hw));

	/* Lower-rate parent: should not be selected. */
	parent_b_hw = clk_hw_register_fixed_rate(NULL, "bestdiv-mux-parent-b",
						 NULL, 0, PARENT_RATE_2GHZ);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, parent_b_hw);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, clk_hw_unregister_fixed_rate_wrapper,
						  parent_b_hw));

	/*
	 * 1-bit mux register selects between the two parents.
	 * CLK_SET_RATE_PARENT allows the divider's rate request to
	 * propagate into clk_mux_determine_rate().
	 */
	mux_hw = clk_hw_register_mux(NULL, "bestdiv-mux",
				     mux_parents, ARRAY_SIZE(mux_parents),
				     CLK_SET_RATE_PARENT,
				     (void __iomem __force *)fake_reg_mux,
				     0, 1, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mux_hw);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, clk_hw_unregister_mux_wrapper,
						  mux_hw));

	div_hw = clk_hw_register_divider_table(NULL, "bestdiv-mux-div",
					       "bestdiv-mux",
					       CLK_SET_RATE_PARENT,
					       (void __iomem __force *)fake_reg_div,
					       0, 2, 0, bestdiv_table, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, div_hw);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, clk_hw_unregister_divider_wrapper,
						  div_hw));

	rate = clk_hw_round_rate(div_hw, ULONG_MAX);
	KUNIT_EXPECT_EQ(test, rate, PARENT_RATE_4GHZ / 2);
}

static struct kunit_case clk_divider_bestdiv_test_cases[] = {
	KUNIT_CASE(clk_divider_bestdiv_ulong_max_returns_max_rate),
	KUNIT_CASE(clk_divider_bestdiv_mux_ulong_max_returns_max_rate),
	{}
};

static struct kunit_suite clk_divider_bestdiv_test_suite = {
	.name = "clk_divider_bestdiv",
	.test_cases = clk_divider_bestdiv_test_cases,
};

kunit_test_suite(clk_divider_bestdiv_test_suite);

MODULE_DESCRIPTION("KUnit tests for clk divider");
MODULE_LICENSE("GPL");
