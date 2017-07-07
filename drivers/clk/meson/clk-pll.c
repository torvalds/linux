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
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "clkc.h"

#define MESON_PLL_RESET				BIT(29)
#define MESON_PLL_LOCK				BIT(31)

#define to_meson_clk_pll(_hw) container_of(_hw, struct meson_clk_pll, hw)

static unsigned long meson_clk_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct meson_clk_pll *pll = to_meson_clk_pll(hw);
	struct parm *p;
	unsigned long parent_rate_mhz = parent_rate / 1000000;
	unsigned long rate_mhz;
	u16 n, m, frac = 0, od, od2 = 0;
	u32 reg;

	p = &pll->n;
	reg = readl(pll->base + p->reg_off);
	n = PARM_GET(p->width, p->shift, reg);

	p = &pll->m;
	reg = readl(pll->base + p->reg_off);
	m = PARM_GET(p->width, p->shift, reg);

	p = &pll->od;
	reg = readl(pll->base + p->reg_off);
	od = PARM_GET(p->width, p->shift, reg);

	p = &pll->od2;
	if (p->width) {
		reg = readl(pll->base + p->reg_off);
		od2 = PARM_GET(p->width, p->shift, reg);
	}

	p = &pll->frac;
	if (p->width) {
		reg = readl(pll->base + p->reg_off);
		frac = PARM_GET(p->width, p->shift, reg);
		rate_mhz = (parent_rate_mhz * m + \
				(parent_rate_mhz * frac >> 12)) * 2 / n;
		rate_mhz = rate_mhz >> od >> od2;
	} else
		rate_mhz = (parent_rate_mhz * m / n) >> od >> od2;

	return rate_mhz * 1000000;
}

static long meson_clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	struct meson_clk_pll *pll = to_meson_clk_pll(hw);
	const struct pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate <= rate_table[i].rate)
			return rate_table[i].rate;
	}

	/* else return the smallest value */
	return rate_table[0].rate;
}

static const struct pll_rate_table *meson_clk_get_pll_settings(struct meson_clk_pll *pll,
							       unsigned long rate)
{
	const struct pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate)
			return &rate_table[i];
	}
	return NULL;
}

/* Specific wait loop for GXL/GXM GP0 PLL */
static int meson_clk_pll_wait_lock_reset(struct meson_clk_pll *pll,
					 struct parm *p_n)
{
	int delay = 100;
	u32 reg;

	while (delay > 0) {
		reg = readl(pll->base + p_n->reg_off);
		writel(reg | MESON_PLL_RESET, pll->base + p_n->reg_off);
		udelay(10);
		writel(reg & ~MESON_PLL_RESET, pll->base + p_n->reg_off);

		/* This delay comes from AMLogic tree clk-gp0-gxl driver */
		mdelay(1);

		reg = readl(pll->base + p_n->reg_off);
		if (reg & MESON_PLL_LOCK)
			return 0;
		delay--;
	}
	return -ETIMEDOUT;
}

static int meson_clk_pll_wait_lock(struct meson_clk_pll *pll,
				   struct parm *p_n)
{
	int delay = 24000000;
	u32 reg;

	while (delay > 0) {
		reg = readl(pll->base + p_n->reg_off);

		if (reg & MESON_PLL_LOCK)
			return 0;
		delay--;
	}
	return -ETIMEDOUT;
}

static void meson_clk_pll_init_params(struct meson_clk_pll *pll)
{
	int i;

	for (i = 0 ; i < pll->params.params_count ; ++i)
		writel(pll->params.params_table[i].value,
		       pll->base + pll->params.params_table[i].reg_off);
}

static int meson_clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct meson_clk_pll *pll = to_meson_clk_pll(hw);
	struct parm *p;
	const struct pll_rate_table *rate_set;
	unsigned long old_rate;
	int ret = 0;
	u32 reg;

	if (parent_rate == 0 || rate == 0)
		return -EINVAL;

	old_rate = rate;

	rate_set = meson_clk_get_pll_settings(pll, rate);
	if (!rate_set)
		return -EINVAL;

	/* Initialize the PLL in a clean state if specified */
	if (pll->params.params_count)
		meson_clk_pll_init_params(pll);

	/* PLL reset */
	p = &pll->n;
	reg = readl(pll->base + p->reg_off);
	/* If no_init_reset is provided, avoid resetting at this point */
	if (!pll->params.no_init_reset)
		writel(reg | MESON_PLL_RESET, pll->base + p->reg_off);

	reg = PARM_SET(p->width, p->shift, reg, rate_set->n);
	writel(reg, pll->base + p->reg_off);

	p = &pll->m;
	reg = readl(pll->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, rate_set->m);
	writel(reg, pll->base + p->reg_off);

	p = &pll->od;
	reg = readl(pll->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, rate_set->od);
	writel(reg, pll->base + p->reg_off);

	p = &pll->od2;
	if (p->width) {
		reg = readl(pll->base + p->reg_off);
		reg = PARM_SET(p->width, p->shift, reg, rate_set->od2);
		writel(reg, pll->base + p->reg_off);
	}

	p = &pll->frac;
	if (p->width) {
		reg = readl(pll->base + p->reg_off);
		reg = PARM_SET(p->width, p->shift, reg, rate_set->frac);
		writel(reg, pll->base + p->reg_off);
	}

	p = &pll->n;
	/* If clear_reset_for_lock is provided, remove the reset bit here */
	if (pll->params.clear_reset_for_lock) {
		reg = readl(pll->base + p->reg_off);
		writel(reg & ~MESON_PLL_RESET, pll->base + p->reg_off);
	}

	/* If reset_lock_loop, use a special loop including resetting */
	if (pll->params.reset_lock_loop)
		ret = meson_clk_pll_wait_lock_reset(pll, p);
	else
		ret = meson_clk_pll_wait_lock(pll, p);
	if (ret) {
		pr_warn("%s: pll did not lock, trying to restore old rate %lu\n",
			__func__, old_rate);
		meson_clk_pll_set_rate(hw, old_rate, parent_rate);
	}

	return ret;
}

const struct clk_ops meson_clk_pll_ops = {
	.recalc_rate	= meson_clk_pll_recalc_rate,
	.round_rate	= meson_clk_pll_round_rate,
	.set_rate	= meson_clk_pll_set_rate,
};

const struct clk_ops meson_clk_pll_ro_ops = {
	.recalc_rate	= meson_clk_pll_recalc_rate,
};
