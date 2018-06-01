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
 *      +------------------------------+
 *      |                              |
 * in -----[ /N ]---[ *M ]---[ >>OD ]----->> out
 *      |         ^        ^           |
 *      +------------------------------+
 *                |        |
 *               FREF     VCO
 *
 * out = in * (m + frac / frac_max) / (n << sum(ods))
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "clkc.h"

static inline struct meson_clk_pll_data *
meson_clk_pll_data(struct clk_regmap *clk)
{
	return (struct meson_clk_pll_data *)clk->data;
}

static unsigned long __pll_params_to_rate(unsigned long parent_rate,
					  const struct pll_rate_table *pllt,
					  u16 frac,
					  struct meson_clk_pll_data *pll)
{
	u64 rate = (u64)parent_rate * pllt->m;
	unsigned int od = pllt->od + pllt->od2 + pllt->od3;

	if (frac && MESON_PARM_APPLICABLE(&pll->frac)) {
		u64 frac_rate = (u64)parent_rate * frac;

		rate += DIV_ROUND_UP_ULL(frac_rate,
					 (1 << pll->frac.width));
	}

	return DIV_ROUND_UP_ULL(rate, pllt->n << od);
}

static unsigned long meson_clk_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	struct pll_rate_table pllt;
	u16 frac;

	pllt.n = meson_parm_read(clk->map, &pll->n);
	pllt.m = meson_parm_read(clk->map, &pll->m);
	pllt.od = meson_parm_read(clk->map, &pll->od);

	pllt.od2 = MESON_PARM_APPLICABLE(&pll->od2) ?
		meson_parm_read(clk->map, &pll->od2) :
		0;

	pllt.od3 = MESON_PARM_APPLICABLE(&pll->od3) ?
		meson_parm_read(clk->map, &pll->od3) :
		0;

	frac = MESON_PARM_APPLICABLE(&pll->frac) ?
		meson_parm_read(clk->map, &pll->frac) :
		0;

	return __pll_params_to_rate(parent_rate, &pllt, frac, pll);
}

static u16 __pll_params_with_frac(unsigned long rate,
				  unsigned long parent_rate,
				  const struct pll_rate_table *pllt,
				  struct meson_clk_pll_data *pll)
{
	u16 frac_max = (1 << pll->frac.width);
	u64 val = (u64)rate * pllt->n;

	val <<= pllt->od + pllt->od2 + pllt->od3;

	if (pll->flags & CLK_MESON_PLL_ROUND_CLOSEST)
		val = DIV_ROUND_CLOSEST_ULL(val * frac_max, parent_rate);
	else
		val = div_u64(val * frac_max, parent_rate);

	val -= pllt->m * frac_max;

	return min((u16)val, (u16)(frac_max - 1));
}

static const struct pll_rate_table *
meson_clk_get_pll_settings(unsigned long rate,
			   struct meson_clk_pll_data *pll)
{
	const struct pll_rate_table *table = pll->table;
	unsigned int i = 0;

	if (!table)
		return NULL;

	/* Find the first table element exceeding rate */
	while (table[i].rate && table[i].rate <= rate)
		i++;

	if (i != 0) {
		if (MESON_PARM_APPLICABLE(&pll->frac) ||
		    !(pll->flags & CLK_MESON_PLL_ROUND_CLOSEST) ||
		    (abs(rate - table[i - 1].rate) <
		     abs(rate - table[i].rate)))
			i--;
	}

	return (struct pll_rate_table *)&table[i];
}

static long meson_clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	const struct pll_rate_table *pllt =
		meson_clk_get_pll_settings(rate, pll);
	u16 frac;

	if (!pllt)
		return meson_clk_pll_recalc_rate(hw, *parent_rate);

	if (!MESON_PARM_APPLICABLE(&pll->frac)
	    || rate == pllt->rate)
		return pllt->rate;

	/*
	 * The rate provided by the setting is not an exact match, let's
	 * try to improve the result using the fractional parameter
	 */
	frac = __pll_params_with_frac(rate, *parent_rate, pllt, pll);

	return __pll_params_to_rate(*parent_rate, pllt, frac, pll);
}

static int meson_clk_pll_wait_lock(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	int delay = 24000000;

	do {
		/* Is the clock locked now ? */
		if (meson_parm_read(clk->map, &pll->l))
			return 0;

		delay--;
	} while (delay > 0);

	return -ETIMEDOUT;
}

static void meson_clk_pll_init(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);

	if (pll->init_count) {
		meson_parm_write(clk->map, &pll->rst, 1);
		regmap_multi_reg_write(clk->map, pll->init_regs,
				       pll->init_count);
		meson_parm_write(clk->map, &pll->rst, 0);
	}
}

static int meson_clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	const struct pll_rate_table *pllt;
	unsigned long old_rate;
	u16 frac = 0;

	if (parent_rate == 0 || rate == 0)
		return -EINVAL;

	old_rate = rate;

	pllt = meson_clk_get_pll_settings(rate, pll);
	if (!pllt)
		return -EINVAL;

	/* Put the pll in reset to write the params */
	meson_parm_write(clk->map, &pll->rst, 1);

	meson_parm_write(clk->map, &pll->n, pllt->n);
	meson_parm_write(clk->map, &pll->m, pllt->m);
	meson_parm_write(clk->map, &pll->od, pllt->od);

	if (MESON_PARM_APPLICABLE(&pll->od2))
		meson_parm_write(clk->map, &pll->od2, pllt->od2);

	if (MESON_PARM_APPLICABLE(&pll->od3))
		meson_parm_write(clk->map, &pll->od3, pllt->od3);

	if (MESON_PARM_APPLICABLE(&pll->frac)) {
		frac = __pll_params_with_frac(rate, parent_rate, pllt, pll);
		meson_parm_write(clk->map, &pll->frac, frac);
	}

	/* make sure the reset is cleared at this point */
	meson_parm_write(clk->map, &pll->rst, 0);

	if (meson_clk_pll_wait_lock(hw)) {
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

	return 0;
}

const struct clk_ops meson_clk_pll_ops = {
	.init		= meson_clk_pll_init,
	.recalc_rate	= meson_clk_pll_recalc_rate,
	.round_rate	= meson_clk_pll_round_rate,
	.set_rate	= meson_clk_pll_set_rate,
};

const struct clk_ops meson_clk_pll_ro_ops = {
	.recalc_rate	= meson_clk_pll_recalc_rate,
};
