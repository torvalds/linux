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

struct meson_clk_pll {
	struct clk_hw	hw;
	void __iomem	*base;
	struct pll_conf	*conf;
	unsigned int	rate_count;
	spinlock_t	*lock;
};
#define to_meson_clk_pll(_hw) container_of(_hw, struct meson_clk_pll, hw)

static unsigned long meson_clk_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct meson_clk_pll *pll = to_meson_clk_pll(hw);
	struct parm *p;
	unsigned long parent_rate_mhz = parent_rate / 1000000;
	unsigned long rate_mhz;
	u16 n, m, od;
	u32 reg;

	p = &pll->conf->n;
	reg = readl(pll->base + p->reg_off);
	n = PARM_GET(p->width, p->shift, reg);

	p = &pll->conf->m;
	reg = readl(pll->base + p->reg_off);
	m = PARM_GET(p->width, p->shift, reg);

	p = &pll->conf->od;
	reg = readl(pll->base + p->reg_off);
	od = PARM_GET(p->width, p->shift, reg);

	rate_mhz = (parent_rate_mhz * m / n) >> od;

	return rate_mhz * 1000000;
}

static long meson_clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	struct meson_clk_pll *pll = to_meson_clk_pll(hw);
	const struct pll_rate_table *rate_table = pll->conf->rate_table;
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
	const struct pll_rate_table *rate_table = pll->conf->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate)
			return &rate_table[i];
	}
	return NULL;
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

	/* PLL reset */
	p = &pll->conf->n;
	reg = readl(pll->base + p->reg_off);
	writel(reg | MESON_PLL_RESET, pll->base + p->reg_off);

	reg = PARM_SET(p->width, p->shift, reg, rate_set->n);
	writel(reg, pll->base + p->reg_off);

	p = &pll->conf->m;
	reg = readl(pll->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, rate_set->m);
	writel(reg, pll->base + p->reg_off);

	p = &pll->conf->od;
	reg = readl(pll->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, rate_set->od);
	writel(reg, pll->base + p->reg_off);

	p = &pll->conf->n;
	ret = meson_clk_pll_wait_lock(pll, p);
	if (ret) {
		pr_warn("%s: pll did not lock, trying to restore old rate %lu\n",
			__func__, old_rate);
		meson_clk_pll_set_rate(hw, old_rate, parent_rate);
	}

	return ret;
}

static const struct clk_ops meson_clk_pll_ops = {
	.recalc_rate	= meson_clk_pll_recalc_rate,
	.round_rate	= meson_clk_pll_round_rate,
	.set_rate	= meson_clk_pll_set_rate,
};

static const struct clk_ops meson_clk_pll_ro_ops = {
	.recalc_rate	= meson_clk_pll_recalc_rate,
};

struct clk *meson_clk_register_pll(const struct clk_conf *clk_conf,
				   void __iomem *reg_base,
				   spinlock_t *lock)
{
	struct clk *clk;
	struct meson_clk_pll *clk_pll;
	struct clk_init_data init;

	clk_pll = kzalloc(sizeof(*clk_pll), GFP_KERNEL);
	if (!clk_pll)
		return ERR_PTR(-ENOMEM);

	clk_pll->base = reg_base + clk_conf->reg_off;
	clk_pll->lock = lock;
	clk_pll->conf = clk_conf->conf.pll;

	init.name = clk_conf->clk_name;
	init.flags = clk_conf->flags | CLK_GET_RATE_NOCACHE;

	init.parent_names = &clk_conf->clks_parent[0];
	init.num_parents = 1;
	init.ops = &meson_clk_pll_ro_ops;

	/* If no rate_table is specified we assume the PLL is read-only */
	if (clk_pll->conf->rate_table) {
		int len;

		for (len = 0; clk_pll->conf->rate_table[len].rate != 0; )
			len++;

		 clk_pll->rate_count = len;
		 init.ops = &meson_clk_pll_ops;
	}

	clk_pll->hw.init = &init;

	clk = clk_register(NULL, &clk_pll->hw);
	if (IS_ERR(clk))
		kfree(clk_pll);

	return clk;
}
