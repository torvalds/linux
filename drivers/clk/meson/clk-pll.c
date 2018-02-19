/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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
 * out = (in * M / N) >> OD
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

static unsigned long meson_clk_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	u64 rate;
	u16 n, m, frac = 0, od, od2 = 0, od3 = 0;

	n = meson_parm_read(clk->map, &pll->n);
	m = meson_parm_read(clk->map, &pll->m);
	od = meson_parm_read(clk->map, &pll->od);

	if (MESON_PARM_APPLICABLE(&pll->od2))
		od2 = meson_parm_read(clk->map, &pll->od2);

	if (MESON_PARM_APPLICABLE(&pll->od3))
		od3 = meson_parm_read(clk->map, &pll->od3);

	rate = (u64)m * parent_rate;

	if (MESON_PARM_APPLICABLE(&pll->frac)) {
		frac = meson_parm_read(clk->map, &pll->frac);

		rate += mul_u64_u32_shr(parent_rate, frac, pll->frac.width);
	}

	return div_u64(rate, n) >> od >> od2 >> od3;
}

static long meson_clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_pll_data *pll = meson_clk_pll_data(clk);
	const struct pll_rate_table *pllt;

	/*
	 * if the table is missing, just return the current rate
	 * since we don't have the other available frequencies
	 */
	if (!pll->table)
		return meson_clk_pll_recalc_rate(hw, *parent_rate);

	for (pllt = pll->table; pllt->rate; pllt++) {
		if (rate <= pllt->rate)
			return pllt->rate;
	}

	/* else return the smallest value */
	return pll->table[0].rate;
}

static const struct pll_rate_table *
meson_clk_get_pll_settings(const struct pll_rate_table *table,
			   unsigned long rate)
{
	const struct pll_rate_table *pllt;

	if (!table)
		return NULL;

	for (pllt = table; pllt->rate; pllt++) {
		if (rate == pllt->rate)
			return pllt;
	}

	return NULL;
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

	if (parent_rate == 0 || rate == 0)
		return -EINVAL;

	old_rate = rate;

	pllt = meson_clk_get_pll_settings(pll->table, rate);
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

	if (MESON_PARM_APPLICABLE(&pll->frac))
		meson_parm_write(clk->map, &pll->frac, pllt->frac);

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
