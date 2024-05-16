// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit test for clock fractional divider
 */
#include <linux/clk-provider.h>
#include <kunit/test.h>

#include "clk-fractional-divider.h"

/*
 * Test the maximum denominator case for fd clock without flags.
 *
 * Expect the highest possible denominator to be used in order to get as close as possible to the
 * requested rate.
 */
static void clk_fd_test_approximation_max_denominator(struct kunit *test)
{
	struct clk_fractional_divider *fd;
	unsigned long rate, parent_rate, parent_rate_before, m, n, max_n;

	fd = kunit_kzalloc(test, sizeof(*fd), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fd);

	fd->mwidth = 3;
	fd->nwidth = 3;
	max_n = 7;

	rate = 240000000;
	parent_rate = (max_n + 1) * rate; /* so that it exceeds the maximum divisor */
	parent_rate_before = parent_rate;

	clk_fractional_divider_general_approximation(&fd->hw, rate, &parent_rate, &m, &n);
	KUNIT_ASSERT_EQ(test, parent_rate, parent_rate_before);

	KUNIT_EXPECT_EQ(test, m, 1);
	KUNIT_EXPECT_EQ(test, n, max_n);
}

/*
 * Test the maximum numerator case for fd clock without flags.
 *
 * Expect the highest possible numerator to be used in order to get as close as possible to the
 * requested rate.
 */
static void clk_fd_test_approximation_max_numerator(struct kunit *test)
{
	struct clk_fractional_divider *fd;
	unsigned long rate, parent_rate, parent_rate_before, m, n, max_m;

	fd = kunit_kzalloc(test, sizeof(*fd), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fd);

	fd->mwidth = 3;
	max_m = 7;
	fd->nwidth = 3;

	rate = 240000000;
	parent_rate = rate / (max_m + 1); /* so that it exceeds the maximum numerator */
	parent_rate_before = parent_rate;

	clk_fractional_divider_general_approximation(&fd->hw, rate, &parent_rate, &m, &n);
	KUNIT_ASSERT_EQ(test, parent_rate, parent_rate_before);

	KUNIT_EXPECT_EQ(test, m, max_m);
	KUNIT_EXPECT_EQ(test, n, 1);
}

/*
 * Test the maximum denominator case for zero based fd clock.
 *
 * Expect the highest possible denominator to be used in order to get as close as possible to the
 * requested rate.
 */
static void clk_fd_test_approximation_max_denominator_zero_based(struct kunit *test)
{
	struct clk_fractional_divider *fd;
	unsigned long rate, parent_rate, parent_rate_before, m, n, max_n;

	fd = kunit_kzalloc(test, sizeof(*fd), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fd);

	fd->flags = CLK_FRAC_DIVIDER_ZERO_BASED;
	fd->mwidth = 3;
	fd->nwidth = 3;
	max_n = 8;

	rate = 240000000;
	parent_rate = (max_n + 1) * rate; /* so that it exceeds the maximum divisor */
	parent_rate_before = parent_rate;

	clk_fractional_divider_general_approximation(&fd->hw, rate, &parent_rate, &m, &n);
	KUNIT_ASSERT_EQ(test, parent_rate, parent_rate_before);

	KUNIT_EXPECT_EQ(test, m, 1);
	KUNIT_EXPECT_EQ(test, n, max_n);
}

/*
 * Test the maximum numerator case for zero based fd clock.
 *
 * Expect the highest possible numerator to be used in order to get as close as possible to the
 * requested rate.
 */
static void clk_fd_test_approximation_max_numerator_zero_based(struct kunit *test)
{
	struct clk_fractional_divider *fd;
	unsigned long rate, parent_rate, parent_rate_before, m, n, max_m;

	fd = kunit_kzalloc(test, sizeof(*fd), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fd);

	fd->flags = CLK_FRAC_DIVIDER_ZERO_BASED;
	fd->mwidth = 3;
	max_m = 8;
	fd->nwidth = 3;

	rate = 240000000;
	parent_rate = rate / (max_m + 1); /* so that it exceeds the maximum numerator */
	parent_rate_before = parent_rate;

	clk_fractional_divider_general_approximation(&fd->hw, rate, &parent_rate, &m, &n);
	KUNIT_ASSERT_EQ(test, parent_rate, parent_rate_before);

	KUNIT_EXPECT_EQ(test, m, max_m);
	KUNIT_EXPECT_EQ(test, n, 1);
}

static struct kunit_case clk_fd_approximation_test_cases[] = {
	KUNIT_CASE(clk_fd_test_approximation_max_denominator),
	KUNIT_CASE(clk_fd_test_approximation_max_numerator),
	KUNIT_CASE(clk_fd_test_approximation_max_denominator_zero_based),
	KUNIT_CASE(clk_fd_test_approximation_max_numerator_zero_based),
	{}
};

/*
 * Test suite for clk_fractional_divider_general_approximation().
 */
static struct kunit_suite clk_fd_approximation_suite = {
	.name = "clk-fd-approximation",
	.test_cases = clk_fd_approximation_test_cases,
};

kunit_test_suites(
	&clk_fd_approximation_suite
);
MODULE_LICENSE("GPL");
