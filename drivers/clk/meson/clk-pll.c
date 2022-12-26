// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * Copyright (c) 2018 Baylibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

/*
 * In the most basic form, a Meson PLL is composed as follows:
 *
 *                     PLL
 *        +--------------------------------+
 *        |                                |
 *        |             +--+               |
 *  in >>-----[ /N ]--->|  |      +-----+  |
 *        |             |  |------| DCO |---->> out
 *        |  +--------->|  |      +--v--+  |
 *        |  |          +--+         |     |
 *        |  |                       |     |
 *        |  +--[ *(M + (F/Fmax) ]<--+     |
 *        |                                |
 *        +--------------------------------+
 *
 * out = in * (m + frac / frac_max) / n
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>

#include "clk-regmap.h"
#include "clk-pll.h"

static inline struct meson_clk_pll_data *
meson_clk_pll_data(struct clk_regmap *clk)
{
	return (struct meson_clk_pll_data *)clk->data;
}

static int __pll_round_closest_mult(struct meson_clk_pll_data *pll)
{
	if ((pll->flags & CLK_MESON_PLL_ROUND_CLOSEST) &&
	    !MESON_PARM_APPLICABLE(&pll->frac))
		return 1;

	return 0;
}

static unsigned long __pll_params_to_rate(unsigned long parent_rate,
					  unsigned int m, unsigned int n,
					  unsigned int frac,
					  struct meson_clk_pll_data *pll)
{
	u64 rate = (u64)parent_rate * m;

	if (frac && MESON_PARM_APPLICABLE(&pll->frac)) {
		u64 frac_rate = (u64)parent_rate * frac;

		rate += DIV_ROUND_UP_ULL(frac_rate,
					 (1 << pll->frac.width));
	}

	return DIV_ROUND_UP_ULL(rate, n);
}

static unsigned long meson_clk_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	unsigned int m, n, frac;

	n = meson_parm_read(clk->map, &pll->n);

	/*
	 * On some HW, N is set to zero on init. This value is invalid as
	 * it would result in a division by zero. The rate can't be
	 * calculated in this case
	 */
	if (n == 0)
		return 0;

	m = meson_parm_read(clk->map, &pll->m);

	frac = MESON_PARM_APPLICABLE(&pll->frac) ?
		meson_parm_read(clk->map, &pll->frac) :
		0;

	return __pll_params_to_rate(parent_rate, m, n, frac, pll);
}

static unsigned int __pll_params_with_frac(unsigned long rate,
					   unsigned long parent_rate,
					   unsigned int m,
					   unsigned int n,
					   struct meson_clk_pll_data *pll)
{
	unsigned int frac_max = (1 << pll->frac.width);
	u64 val = (u64)rate * n;

	/* Bail out if we are already over the requested rate */
	if (rate < parent_rate * m / n)
		return 0;

	if (pll->flags & CLK_MESON_PLL_ROUND_CLOSEST)
		val = DIV_ROUND_CLOSEST_ULL(val * frac_max, parent_rate);
	else
		val = div_u64(val * frac_max, parent_rate);

	val -= m * frac_max;

	return min((unsigned int)val, (frac_max - 1));
}

static bool meson_clk_pll_is_better(unsigned long rate,
				    unsigned long best,
				    unsigned long now,
				    struct meson_clk_pll_data *pll)
{
	if (__pll_round_closest_mult(pll)) {
		/* Round Closest */
		if (abs(now - rate) < abs(best - rate))
			return true;
	} else {
		/* Round down */
		if (now <= rate && best < now)
			return true;
	}

	return false;
}

static int meson_clk_get_pll_table_index(unsigned int index,
					 unsigned int *m,
					 unsigned int *n,
					 struct meson_clk_pll_data *pll)
{
	if (!pll->table[index].n)
		return -EINVAL;

	*m = pll->table[index].m;
	*n = pll->table[index].n;

	return 0;
}

static unsigned int meson_clk_get_pll_range_m(unsigned long rate,
					      unsigned long parent_rate,
					      unsigned int n,
					      struct meson_clk_pll_data *pll)
{
	u64 val = (u64)rate * n;

	if (__pll_round_closest_mult(pll))
		return DIV_ROUND_CLOSEST_ULL(val, parent_rate);

	return div_u64(val,  parent_rate);
}

static int meson_clk_get_pll_range_index(unsigned long rate,
					 unsigned long parent_rate,
					 unsigned int index,
					 unsigned int *m,
					 unsigned int *n,
					 struct meson_clk_pll_data *pll)
{
	*n = index + 1;

	/* Check the predivider range */
	if (*n >= (1 << pll->n.width))
		return -EINVAL;

	if (*n == 1) {
		/* Get the boundaries out the way */
		if (rate <= pll->range->min * parent_rate) {
			*m = pll->range->min;
			return -ENODATA;
		} else if (rate >= pll->range->max * parent_rate) {
			*m = pll->range->max;
			return -ENODATA;
		}
	}

	*m = meson_clk_get_pll_range_m(rate, parent_rate, *n, pll);

	/* the pre-divider gives a multiplier too big - stop */
	if (*m >= (1 << pll->m.width))
		return -EINVAL;

	return 0;
}

static int meson_clk_get_pll_get_index(unsigned long rate,
				       unsigned long parent_rate,
				       unsigned int index,
				       unsigned int *m,
				       unsigned int *n,
				       struct meson_clk_pll_data *pll)
{
	if (pll->range)
		return meson_clk_get_pll_range_index(rate, parent_rate,
						     index, m, n, pll);
	else if (pll->table)
		return meson_clk_get_pll_table_index(index, m, n, pll);

	return -EINVAL;
}

static int meson_clk_get_pll_settings(unsigned long rate,
				      unsigned long parent_rate,
				      unsigned int *best_m,
				      unsigned int *best_n,
				      struct meson_clk_pll_data *pll)
{
	unsigned long best = 0, now = 0;
	unsigned int i, m, n;
	int ret;

	for (i = 0, ret = 0; !ret; i++) {
		ret = meson_clk_get_pll_get_index(rate, parent_rate,
						  i, &m, &n, pll);
		if (ret == -EINVAL)
			break;

		now = __pll_params_to_rate(parent_rate, m, n, 0, pll);
		if (meson_clk_pll_is_better(rate, best, now, pll)) {
			best = now;
			*best_m = m;
			*best_n = n;

			if (now == rate)
				break;
		}
	}

	return best ? 0 : -EINVAL;
}

static int meson_clk_pll_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	unsigned int m, n, frac;
	unsigned long round;
	int ret;

	ret = meson_clk_get_pll_settings(req->rate, req->best_parent_rate,
					 &m, &n, pll);
	if (ret)
		return ret;

	round = __pll_params_to_rate(req->best_parent_rate, m, n, 0, pll);

	if (!MESON_PARM_APPLICABLE(&pll->frac) || req->rate == round) {
		req->rate = round;
		return 0;
	}

	/*
	 * The rate provided by the setting is not an exact match, let's
	 * try to improve the result using the fractional parameter
	 */
	frac = __pll_params_with_frac(req->rate, req->best_parent_rate, m, n, pll);
	req->rate = __pll_params_to_rate(req->best_parent_rate, m, n, frac, pll);

	return 0;
}

static int meson_clk_pll_wait_lock(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	int delay = 5000;

	do {
		/* Is the clock locked now ? Time out after 100ms. */
		if (meson_parm_read(clk->map, &pll->l))
			return 0;

		udelay(20);
	} while (--delay);

	return -ETIMEDOUT;
}

static int meson_clk_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);

	if (pll->init_count) {
		meson_parm_write(clk->map, &pll->rst, 1);
		regmap_multi_reg_write(clk->map, pll->init_regs,
				       pll->init_count);
		meson_parm_write(clk->map, &pll->rst, 0);
	}

	return 0;
}

static int meson_clk_pll_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);

	if (meson_parm_read(clk->map, &pll->rst) ||
	    !meson_parm_read(clk->map, &pll->en) ||
	    !meson_parm_read(clk->map, &pll->l))
		return 0;

	return 1;
}

static int meson_clk_pcie_pll_enable(struct clk_hw *hw)
{
	int retries = 10;

	do {
		meson_clk_pll_init(hw);
		if (!meson_clk_pll_wait_lock(hw))
			return 0;
		pr_info("Retry enabling PCIe PLL clock\n");
	} while (--retries);

	return -EIO;
}

static int meson_clk_pll_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);

	/* do nothing if the PLL is already enabled */
	if (clk_hw_is_enabled(hw))
		return 0;

	/* Make sure the pll is in reset */
	meson_parm_write(clk->map, &pll->rst, 1);

	/* Enable the pll */
	meson_parm_write(clk->map, &pll->en, 1);

	/* Take the pll out reset */
	meson_parm_write(clk->map, &pll->rst, 0);

	if (meson_clk_pll_wait_lock(hw))
		return -EIO;

	return 0;
}

static void meson_clk_pll_disable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);

	/* Put the pll is in reset */
	meson_parm_write(clk->map, &pll->rst, 1);

	/* Disable the pll */
	meson_parm_write(clk->map, &pll->en, 0);
}

static int meson_clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	unsigned int enabled, m, n, frac = 0;
	unsigned long old_rate;
	int ret;

	if (parent_rate == 0 || rate == 0)
		return -EINVAL;

	old_rate = clk_hw_get_rate(hw);

	ret = meson_clk_get_pll_settings(rate, parent_rate, &m, &n, pll);
	if (ret)
		return ret;

	enabled = meson_parm_read(clk->map, &pll->en);
	if (enabled)
		meson_clk_pll_disable(hw);

	meson_parm_write(clk->map, &pll->n, n);
	meson_parm_write(clk->map, &pll->m, m);

	if (MESON_PARM_APPLICABLE(&pll->frac)) {
		frac = __pll_params_with_frac(rate, parent_rate, m, n, pll);
		meson_parm_write(clk->map, &pll->frac, frac);
	}

	/* If the pll is stopped, bail out now */
	if (!enabled)
		return 0;

	ret = meson_clk_pll_enable(hw);
	if (ret) {
		pr_warn("%s: pll did not lock, trying to restore old rate %lu\n",
			__func__, old_rate);
		/*
		 * FIXME: Do we really need/want this HACK ?
		 * It looks unsafe. what happens if the clock gets into a
		 * broken state and we can't lock back on the old_rate ? Looks
		 * like an infinite recursion is possible
		 */
		meson_clk_pll_set_rate(hw, old_rate, parent_rate);
	}

	return ret;
}

/*
 * The Meson G12A PCIE PLL is fined tuned to deliver a very precise
 * 100MHz reference clock for the PCIe Analog PHY, and thus requires
 * a strict register sequence to enable the PLL.
 * To simplify, re-use the _init() op to enable the PLL and keep
 * the other ops except set_rate since the rate is fixed.
 */
const struct clk_ops meson_clk_pcie_pll_ops = {
	.recalc_rate	= meson_clk_pll_recalc_rate,
	.determine_rate	= meson_clk_pll_determine_rate,
	.is_enabled	= meson_clk_pll_is_enabled,
	.enable		= meson_clk_pcie_pll_enable,
	.disable	= meson_clk_pll_disable
};
EXPORT_SYMBOL_GPL(meson_clk_pcie_pll_ops);

const struct clk_ops meson_clk_pll_ops = {
	.init		= meson_clk_pll_init,
	.recalc_rate	= meson_clk_pll_recalc_rate,
	.determine_rate	= meson_clk_pll_determine_rate,
	.set_rate	= meson_clk_pll_set_rate,
	.is_enabled	= meson_clk_pll_is_enabled,
	.enable		= meson_clk_pll_enable,
	.disable	= meson_clk_pll_disable
};
EXPORT_SYMBOL_GPL(meson_clk_pll_ops);

const struct clk_ops meson_clk_pll_ro_ops = {
	.recalc_rate	= meson_clk_pll_recalc_rate,
	.is_enabled	= meson_clk_pll_is_enabled,
};
EXPORT_SYMBOL_GPL(meson_clk_pll_ro_ops);

MODULE_DESCRIPTION("Amlogic PLL driver");
MODULE_AUTHOR("Carlo Caione <carlo@endlessm.com>");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
