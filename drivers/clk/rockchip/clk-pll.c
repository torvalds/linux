/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/div64.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include "clk.h"

#define PLL_MODE_MASK		0x3
#define PLL_MODE_SLOW		0x0
#define PLL_MODE_NORM		0x1
#define PLL_MODE_DEEP		0x2

struct rockchip_clk_pll {
	struct clk_hw		hw;

	struct clk_mux		pll_mux;
	const struct clk_ops	*pll_mux_ops;

	struct notifier_block	clk_nb;

	void __iomem		*reg_base;
	int			lock_offset;
	unsigned int		lock_shift;
	enum rockchip_pll_type	type;
	u8			flags;
	const struct rockchip_pll_rate_table *rate_table;
	unsigned int		rate_count;
	spinlock_t		*lock;
};

#define to_rockchip_clk_pll(_hw) container_of(_hw, struct rockchip_clk_pll, hw)
#define to_rockchip_clk_pll_nb(nb) \
			container_of(nb, struct rockchip_clk_pll, clk_nb)

static const struct rockchip_pll_rate_table *rockchip_get_pll_settings(
			    struct rockchip_clk_pll *pll, unsigned long rate)
{
	const struct rockchip_pll_rate_table  *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate)
			return &rate_table[i];
	}

	return NULL;
}

static long rockchip_pll_round_rate(struct clk_hw *hw,
			    unsigned long drate, unsigned long *prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate_table = pll->rate_table;
	int i;

	/* Assumming rate_table is in descending order */
	for (i = 0; i < pll->rate_count; i++) {
		if (drate >= rate_table[i].rate)
			return rate_table[i].rate;
	}

	/* return minimum supported value */
	return rate_table[i - 1].rate;
}

/*
 * Wait for the pll to reach the locked state.
 * The calling set_rate function is responsible for making sure the
 * grf regmap is available.
 */
static int rockchip_pll_wait_lock(struct rockchip_clk_pll *pll)
{
	struct regmap *grf = rockchip_clk_get_grf();
	unsigned int val;
	int delay = 24000000, ret;

	while (delay > 0) {
		ret = regmap_read(grf, pll->lock_offset, &val);
		if (ret) {
			pr_err("%s: failed to read pll lock status: %d\n",
			       __func__, ret);
			return ret;
		}

		if (val & BIT(pll->lock_shift))
			return 0;
		delay--;
	}

	pr_err("%s: timeout waiting for pll to lock\n", __func__);
	return -ETIMEDOUT;
}

/**
 * PLL used in RK3066, RK3188 and RK3288
 */

#define RK3066_PLL_RESET_DELAY(nr)	((nr * 500) / 24 + 1)

#define RK3066_PLLCON(i)		(i * 0x4)
#define RK3066_PLLCON0_OD_MASK		0xf
#define RK3066_PLLCON0_OD_SHIFT		0
#define RK3066_PLLCON0_NR_MASK		0x3f
#define RK3066_PLLCON0_NR_SHIFT		8
#define RK3066_PLLCON1_NF_MASK		0x1fff
#define RK3066_PLLCON1_NF_SHIFT		0
#define RK3066_PLLCON2_NB_MASK		0xfff
#define RK3066_PLLCON2_NB_SHIFT		0
#define RK3066_PLLCON3_RESET		(1 << 5)
#define RK3066_PLLCON3_PWRDOWN		(1 << 1)
#define RK3066_PLLCON3_BYPASS		(1 << 0)

static unsigned long rockchip_rk3066_pll_recalc_rate(struct clk_hw *hw,
						     unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	u64 nf, nr, no, rate64 = prate;
	u32 pllcon;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(3));
	if (pllcon & RK3066_PLLCON3_BYPASS) {
		pr_debug("%s: pll %s is bypassed\n", __func__,
			clk_hw_get_name(hw));
		return prate;
	}

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(1));
	nf = (pllcon >> RK3066_PLLCON1_NF_SHIFT) & RK3066_PLLCON1_NF_MASK;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(0));
	nr = (pllcon >> RK3066_PLLCON0_NR_SHIFT) & RK3066_PLLCON0_NR_MASK;
	no = (pllcon >> RK3066_PLLCON0_OD_SHIFT) & RK3066_PLLCON0_OD_MASK;

	rate64 *= (nf + 1);
	do_div(rate64, nr + 1);
	do_div(rate64, no + 1);

	return (unsigned long)rate64;
}

static int rockchip_rk3066_pll_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	unsigned long old_rate = rockchip_rk3066_pll_recalc_rate(hw, prate);
	struct regmap *grf = rockchip_clk_get_grf();
	struct clk_mux *pll_mux = &pll->pll_mux;
	const struct clk_ops *pll_mux_ops = pll->pll_mux_ops;
	int rate_change_remuxed = 0;
	int cur_parent;
	int ret;

	if (IS_ERR(grf)) {
		pr_debug("%s: grf regmap not available, aborting rate change\n",
			 __func__);
		return PTR_ERR(grf);
	}

	pr_debug("%s: changing %s from %lu to %lu with a parent rate of %lu\n",
		 __func__, clk_hw_get_name(hw), old_rate, drate, prate);

	/* Get required rate settings from table */
	rate = rockchip_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, clk_hw_get_name(hw));
		return -EINVAL;
	}

	pr_debug("%s: rate settings for %lu (nr, no, nf): (%d, %d, %d)\n",
		 __func__, rate->rate, rate->nr, rate->no, rate->nf);

	cur_parent = pll_mux_ops->get_parent(&pll_mux->hw);
	if (cur_parent == PLL_MODE_NORM) {
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_SLOW);
		rate_change_remuxed = 1;
	}

	/* enter reset mode */
	writel(HIWORD_UPDATE(RK3066_PLLCON3_RESET, RK3066_PLLCON3_RESET, 0),
	       pll->reg_base + RK3066_PLLCON(3));

	/* update pll values */
	writel(HIWORD_UPDATE(rate->nr - 1, RK3066_PLLCON0_NR_MASK,
					   RK3066_PLLCON0_NR_SHIFT) |
	       HIWORD_UPDATE(rate->no - 1, RK3066_PLLCON0_OD_MASK,
					   RK3066_PLLCON0_OD_SHIFT),
	       pll->reg_base + RK3066_PLLCON(0));

	writel_relaxed(HIWORD_UPDATE(rate->nf - 1, RK3066_PLLCON1_NF_MASK,
						   RK3066_PLLCON1_NF_SHIFT),
		       pll->reg_base + RK3066_PLLCON(1));
	writel_relaxed(HIWORD_UPDATE(rate->nb - 1, RK3066_PLLCON2_NB_MASK,
						   RK3066_PLLCON2_NB_SHIFT),
		       pll->reg_base + RK3066_PLLCON(2));

	/* leave reset and wait the reset_delay */
	writel(HIWORD_UPDATE(0, RK3066_PLLCON3_RESET, 0),
	       pll->reg_base + RK3066_PLLCON(3));
	udelay(RK3066_PLL_RESET_DELAY(rate->nr));

	/* wait for the pll to lock */
	ret = rockchip_pll_wait_lock(pll);
	if (ret) {
		pr_warn("%s: pll did not lock, trying to restore old rate %lu\n",
			__func__, old_rate);
		rockchip_rk3066_pll_set_rate(hw, old_rate, prate);
	}

	if (rate_change_remuxed)
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_NORM);

	return ret;
}

static int rockchip_rk3066_pll_enable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	writel(HIWORD_UPDATE(0, RK3066_PLLCON3_PWRDOWN, 0),
	       pll->reg_base + RK3066_PLLCON(3));

	return 0;
}

static void rockchip_rk3066_pll_disable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	writel(HIWORD_UPDATE(RK3066_PLLCON3_PWRDOWN,
			     RK3066_PLLCON3_PWRDOWN, 0),
	       pll->reg_base + RK3066_PLLCON(3));
}

static int rockchip_rk3066_pll_is_enabled(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	u32 pllcon = readl(pll->reg_base + RK3066_PLLCON(3));

	return !(pllcon & RK3066_PLLCON3_PWRDOWN);
}

static void rockchip_rk3066_pll_init(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	unsigned int nf, nr, no, nb;
	unsigned long drate;
	u32 pllcon;

	if (!(pll->flags & ROCKCHIP_PLL_SYNC_RATE))
		return;

	drate = clk_hw_get_rate(hw);
	rate = rockchip_get_pll_settings(pll, drate);

	/* when no rate setting for the current rate, rely on clk_set_rate */
	if (!rate)
		return;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(0));
	nr = ((pllcon >> RK3066_PLLCON0_NR_SHIFT) & RK3066_PLLCON0_NR_MASK) + 1;
	no = ((pllcon >> RK3066_PLLCON0_OD_SHIFT) & RK3066_PLLCON0_OD_MASK) + 1;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(1));
	nf = ((pllcon >> RK3066_PLLCON1_NF_SHIFT) & RK3066_PLLCON1_NF_MASK) + 1;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(2));
	nb = ((pllcon >> RK3066_PLLCON2_NB_SHIFT) & RK3066_PLLCON2_NB_MASK) + 1;

	pr_debug("%s: pll %s@%lu: nr (%d:%d); no (%d:%d); nf(%d:%d), nb(%d:%d)\n",
		 __func__, clk_hw_get_name(hw), drate, rate->nr, nr,
		rate->no, no, rate->nf, nf, rate->nb, nb);
	if (rate->nr != nr || rate->no != no || rate->nf != nf
					     || rate->nb != nb) {
		struct clk_hw *parent = clk_hw_get_parent(hw);
		unsigned long prate;

		if (!parent) {
			pr_warn("%s: parent of %s not available\n",
				__func__, clk_hw_get_name(hw));
			return;
		}

		pr_debug("%s: pll %s: rate params do not match rate table, adjusting\n",
			 __func__, clk_hw_get_name(hw));
		prate = clk_hw_get_rate(parent);
		rockchip_rk3066_pll_set_rate(hw, drate, prate);
	}
}

static const struct clk_ops rockchip_rk3066_pll_clk_norate_ops = {
	.recalc_rate = rockchip_rk3066_pll_recalc_rate,
	.enable = rockchip_rk3066_pll_enable,
	.disable = rockchip_rk3066_pll_disable,
	.is_enabled = rockchip_rk3066_pll_is_enabled,
};

static const struct clk_ops rockchip_rk3066_pll_clk_ops = {
	.recalc_rate = rockchip_rk3066_pll_recalc_rate,
	.round_rate = rockchip_pll_round_rate,
	.set_rate = rockchip_rk3066_pll_set_rate,
	.enable = rockchip_rk3066_pll_enable,
	.disable = rockchip_rk3066_pll_disable,
	.is_enabled = rockchip_rk3066_pll_is_enabled,
	.init = rockchip_rk3066_pll_init,
};

/*
 * Common registering of pll clocks
 */

struct clk *rockchip_clk_register_pll(enum rockchip_pll_type pll_type,
		const char *name, const char *const *parent_names,
		u8 num_parents, void __iomem *base, int con_offset,
		int grf_lock_offset, int lock_shift, int mode_offset,
		int mode_shift, struct rockchip_pll_rate_table *rate_table,
		u8 clk_pll_flags, spinlock_t *lock)
{
	const char *pll_parents[3];
	struct clk_init_data init;
	struct rockchip_clk_pll *pll;
	struct clk_mux *pll_mux;
	struct clk *pll_clk, *mux_clk;
	char pll_name[20];

	if (num_parents != 2) {
		pr_err("%s: needs two parent clocks\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	/* name the actual pll */
	snprintf(pll_name, sizeof(pll_name), "pll_%s", name);

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = pll_name;

	/* keep all plls untouched for now */
	init.flags = CLK_IGNORE_UNUSED;

	init.parent_names = &parent_names[0];
	init.num_parents = 1;

	if (rate_table) {
		int len;

		/* find count of rates in rate_table */
		for (len = 0; rate_table[len].rate != 0; )
			len++;

		pll->rate_count = len;
		pll->rate_table = kmemdup(rate_table,
					pll->rate_count *
					sizeof(struct rockchip_pll_rate_table),
					GFP_KERNEL);
		WARN(!pll->rate_table,
			"%s: could not allocate rate table for %s\n",
			__func__, name);
	}

	switch (pll_type) {
	case pll_rk3066:
		if (!pll->rate_table)
			init.ops = &rockchip_rk3066_pll_clk_norate_ops;
		else
			init.ops = &rockchip_rk3066_pll_clk_ops;
		break;
	default:
		pr_warn("%s: Unknown pll type for pll clk %s\n",
			__func__, name);
	}

	pll->hw.init = &init;
	pll->type = pll_type;
	pll->reg_base = base + con_offset;
	pll->lock_offset = grf_lock_offset;
	pll->lock_shift = lock_shift;
	pll->flags = clk_pll_flags;
	pll->lock = lock;

	/* create the mux on top of the real pll */
	pll->pll_mux_ops = &clk_mux_ops;
	pll_mux = &pll->pll_mux;
	pll_mux->reg = base + mode_offset;
	pll_mux->shift = mode_shift;
	pll_mux->mask = PLL_MODE_MASK;
	pll_mux->flags = 0;
	pll_mux->lock = lock;
	pll_mux->hw.init = &init;

	if (pll_type == pll_rk3066)
		pll_mux->flags |= CLK_MUX_HIWORD_MASK;

	pll_clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(pll_clk)) {
		pr_err("%s: failed to register pll clock %s : %ld\n",
			__func__, name, PTR_ERR(pll_clk));
		mux_clk = pll_clk;
		goto err_pll;
	}

	/* the actual muxing is xin24m, pll-output, xin32k */
	pll_parents[0] = parent_names[0];
	pll_parents[1] = pll_name;
	pll_parents[2] = parent_names[1];

	init.name = name;
	init.flags = CLK_SET_RATE_PARENT;
	init.ops = pll->pll_mux_ops;
	init.parent_names = pll_parents;
	init.num_parents = ARRAY_SIZE(pll_parents);

	mux_clk = clk_register(NULL, &pll_mux->hw);
	if (IS_ERR(mux_clk))
		goto err_mux;

	return mux_clk;

err_mux:
	clk_unregister(pll_clk);
err_pll:
	kfree(pll);
	return mux_clk;
}
