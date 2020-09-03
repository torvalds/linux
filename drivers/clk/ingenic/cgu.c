// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ingenic SoC CGU driver
 *
 * Copyright (c) 2013-2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>

#include "cgu.h"

#define MHZ (1000 * 1000)

static inline const struct ingenic_cgu_clk_info *
to_clk_info(struct ingenic_clk *clk)
{
	return &clk->cgu->clock_info[clk->idx];
}

/**
 * ingenic_cgu_gate_get() - get the value of clock gate register bit
 * @cgu: reference to the CGU whose registers should be read
 * @info: info struct describing the gate bit
 *
 * Retrieves the state of the clock gate bit described by info. The
 * caller must hold cgu->lock.
 *
 * Return: true if the gate bit is set, else false.
 */
static inline bool
ingenic_cgu_gate_get(struct ingenic_cgu *cgu,
		     const struct ingenic_cgu_gate_info *info)
{
	return !!(readl(cgu->base + info->reg) & BIT(info->bit))
		^ info->clear_to_gate;
}

/**
 * ingenic_cgu_gate_set() - set the value of clock gate register bit
 * @cgu: reference to the CGU whose registers should be modified
 * @info: info struct describing the gate bit
 * @val: non-zero to gate a clock, otherwise zero
 *
 * Sets the given gate bit in order to gate or ungate a clock.
 *
 * The caller must hold cgu->lock.
 */
static inline void
ingenic_cgu_gate_set(struct ingenic_cgu *cgu,
		     const struct ingenic_cgu_gate_info *info, bool val)
{
	u32 clkgr = readl(cgu->base + info->reg);

	if (val ^ info->clear_to_gate)
		clkgr |= BIT(info->bit);
	else
		clkgr &= ~BIT(info->bit);

	writel(clkgr, cgu->base + info->reg);
}

/*
 * PLL operations
 */

static unsigned long
ingenic_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	const struct ingenic_cgu_pll_info *pll_info;
	unsigned m, n, od_enc, od;
	bool bypass;
	u32 ctl;

	BUG_ON(clk_info->type != CGU_CLK_PLL);
	pll_info = &clk_info->pll;

	ctl = readl(cgu->base + pll_info->reg);

	m = (ctl >> pll_info->m_shift) & GENMASK(pll_info->m_bits - 1, 0);
	m += pll_info->m_offset;
	n = (ctl >> pll_info->n_shift) & GENMASK(pll_info->n_bits - 1, 0);
	n += pll_info->n_offset;
	od_enc = ctl >> pll_info->od_shift;
	od_enc &= GENMASK(pll_info->od_bits - 1, 0);

	ctl = readl(cgu->base + pll_info->bypass_reg);

	bypass = !pll_info->no_bypass_bit &&
		 !!(ctl & BIT(pll_info->bypass_bit));

	if (bypass)
		return parent_rate;

	for (od = 0; od < pll_info->od_max; od++) {
		if (pll_info->od_encoding[od] == od_enc)
			break;
	}
	BUG_ON(od == pll_info->od_max);
	od++;

	return div_u64((u64)parent_rate * m * pll_info->rate_multiplier,
		n * od);
}

static unsigned long
ingenic_pll_calc(const struct ingenic_cgu_clk_info *clk_info,
		 unsigned long rate, unsigned long parent_rate,
		 unsigned *pm, unsigned *pn, unsigned *pod)
{
	const struct ingenic_cgu_pll_info *pll_info;
	unsigned m, n, od;

	pll_info = &clk_info->pll;
	od = 1;

	/*
	 * The frequency after the input divider must be between 10 and 50 MHz.
	 * The highest divider yields the best resolution.
	 */
	n = parent_rate / (10 * MHZ);
	n = min_t(unsigned, n, 1 << clk_info->pll.n_bits);
	n = max_t(unsigned, n, pll_info->n_offset);

	m = (rate / MHZ) * od * n / (parent_rate / MHZ);
	m = min_t(unsigned, m, 1 << clk_info->pll.m_bits);
	m = max_t(unsigned, m, pll_info->m_offset);

	if (pm)
		*pm = m;
	if (pn)
		*pn = n;
	if (pod)
		*pod = od;

	return div_u64((u64)parent_rate * m * pll_info->rate_multiplier,
		n * od);
}

static long
ingenic_pll_round_rate(struct clk_hw *hw, unsigned long req_rate,
		       unsigned long *prate)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);

	return ingenic_pll_calc(clk_info, req_rate, *prate, NULL, NULL, NULL);
}

static inline int ingenic_pll_check_stable(struct ingenic_cgu *cgu,
					   const struct ingenic_cgu_pll_info *pll_info)
{
	u32 ctl;

	return readl_poll_timeout(cgu->base + pll_info->reg, ctl,
				  ctl & BIT(pll_info->stable_bit),
				  0, 100 * USEC_PER_MSEC);
}

static int
ingenic_pll_set_rate(struct clk_hw *hw, unsigned long req_rate,
		     unsigned long parent_rate)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	const struct ingenic_cgu_pll_info *pll_info = &clk_info->pll;
	unsigned long rate, flags;
	unsigned int m, n, od;
	int ret = 0;
	u32 ctl;

	rate = ingenic_pll_calc(clk_info, req_rate, parent_rate,
			       &m, &n, &od);
	if (rate != req_rate)
		pr_info("ingenic-cgu: request '%s' rate %luHz, actual %luHz\n",
			clk_info->name, req_rate, rate);

	spin_lock_irqsave(&cgu->lock, flags);
	ctl = readl(cgu->base + pll_info->reg);

	ctl &= ~(GENMASK(pll_info->m_bits - 1, 0) << pll_info->m_shift);
	ctl |= (m - pll_info->m_offset) << pll_info->m_shift;

	ctl &= ~(GENMASK(pll_info->n_bits - 1, 0) << pll_info->n_shift);
	ctl |= (n - pll_info->n_offset) << pll_info->n_shift;

	ctl &= ~(GENMASK(pll_info->od_bits - 1, 0) << pll_info->od_shift);
	ctl |= pll_info->od_encoding[od - 1] << pll_info->od_shift;

	writel(ctl, cgu->base + pll_info->reg);

	/* If the PLL is enabled, verify that it's stable */
	if (ctl & BIT(pll_info->enable_bit))
		ret = ingenic_pll_check_stable(cgu, pll_info);

	spin_unlock_irqrestore(&cgu->lock, flags);

	return ret;
}

static int ingenic_pll_enable(struct clk_hw *hw)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	const struct ingenic_cgu_pll_info *pll_info = &clk_info->pll;
	unsigned long flags;
	int ret;
	u32 ctl;

	spin_lock_irqsave(&cgu->lock, flags);
	ctl = readl(cgu->base + pll_info->bypass_reg);

	ctl &= ~BIT(pll_info->bypass_bit);

	writel(ctl, cgu->base + pll_info->bypass_reg);

	ctl = readl(cgu->base + pll_info->reg);

	ctl |= BIT(pll_info->enable_bit);

	writel(ctl, cgu->base + pll_info->reg);

	ret = ingenic_pll_check_stable(cgu, pll_info);
	spin_unlock_irqrestore(&cgu->lock, flags);

	return ret;
}

static void ingenic_pll_disable(struct clk_hw *hw)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	const struct ingenic_cgu_pll_info *pll_info = &clk_info->pll;
	unsigned long flags;
	u32 ctl;

	spin_lock_irqsave(&cgu->lock, flags);
	ctl = readl(cgu->base + pll_info->reg);

	ctl &= ~BIT(pll_info->enable_bit);

	writel(ctl, cgu->base + pll_info->reg);
	spin_unlock_irqrestore(&cgu->lock, flags);
}

static int ingenic_pll_is_enabled(struct clk_hw *hw)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	const struct ingenic_cgu_pll_info *pll_info = &clk_info->pll;
	u32 ctl;

	ctl = readl(cgu->base + pll_info->reg);

	return !!(ctl & BIT(pll_info->enable_bit));
}

static const struct clk_ops ingenic_pll_ops = {
	.recalc_rate = ingenic_pll_recalc_rate,
	.round_rate = ingenic_pll_round_rate,
	.set_rate = ingenic_pll_set_rate,

	.enable = ingenic_pll_enable,
	.disable = ingenic_pll_disable,
	.is_enabled = ingenic_pll_is_enabled,
};

/*
 * Operations for all non-PLL clocks
 */

static u8 ingenic_clk_get_parent(struct clk_hw *hw)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	u32 reg;
	u8 i, hw_idx, idx = 0;

	if (clk_info->type & CGU_CLK_MUX) {
		reg = readl(cgu->base + clk_info->mux.reg);
		hw_idx = (reg >> clk_info->mux.shift) &
			 GENMASK(clk_info->mux.bits - 1, 0);

		/*
		 * Convert the hardware index to the parent index by skipping
		 * over any -1's in the parents array.
		 */
		for (i = 0; i < hw_idx; i++) {
			if (clk_info->parents[i] != -1)
				idx++;
		}
	}

	return idx;
}

static int ingenic_clk_set_parent(struct clk_hw *hw, u8 idx)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	unsigned long flags;
	u8 curr_idx, hw_idx, num_poss;
	u32 reg, mask;

	if (clk_info->type & CGU_CLK_MUX) {
		/*
		 * Convert the parent index to the hardware index by adding
		 * 1 for any -1 in the parents array preceding the given
		 * index. That is, we want the index of idx'th entry in
		 * clk_info->parents which does not equal -1.
		 */
		hw_idx = curr_idx = 0;
		num_poss = 1 << clk_info->mux.bits;
		for (; hw_idx < num_poss; hw_idx++) {
			if (clk_info->parents[hw_idx] == -1)
				continue;
			if (curr_idx == idx)
				break;
			curr_idx++;
		}

		/* idx should always be a valid parent */
		BUG_ON(curr_idx != idx);

		mask = GENMASK(clk_info->mux.bits - 1, 0);
		mask <<= clk_info->mux.shift;

		spin_lock_irqsave(&cgu->lock, flags);

		/* write the register */
		reg = readl(cgu->base + clk_info->mux.reg);
		reg &= ~mask;
		reg |= hw_idx << clk_info->mux.shift;
		writel(reg, cgu->base + clk_info->mux.reg);

		spin_unlock_irqrestore(&cgu->lock, flags);
		return 0;
	}

	return idx ? -EINVAL : 0;
}

static unsigned long
ingenic_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	unsigned long rate = parent_rate;
	u32 div_reg, div;

	if (clk_info->type & CGU_CLK_DIV) {
		div_reg = readl(cgu->base + clk_info->div.reg);
		div = (div_reg >> clk_info->div.shift) &
		      GENMASK(clk_info->div.bits - 1, 0);

		if (clk_info->div.div_table)
			div = clk_info->div.div_table[div];
		else
			div = (div + 1) * clk_info->div.div;

		rate /= div;
	} else if (clk_info->type & CGU_CLK_FIXDIV) {
		rate /= clk_info->fixdiv.div;
	}

	return rate;
}

static unsigned int
ingenic_clk_calc_hw_div(const struct ingenic_cgu_clk_info *clk_info,
			unsigned int div)
{
	unsigned int i;

	for (i = 0; i < (1 << clk_info->div.bits)
				&& clk_info->div.div_table[i]; i++) {
		if (clk_info->div.div_table[i] >= div)
			return i;
	}

	return i - 1;
}

static unsigned
ingenic_clk_calc_div(const struct ingenic_cgu_clk_info *clk_info,
		     unsigned long parent_rate, unsigned long req_rate)
{
	unsigned int div, hw_div;

	/* calculate the divide */
	div = DIV_ROUND_UP(parent_rate, req_rate);

	if (clk_info->div.div_table) {
		hw_div = ingenic_clk_calc_hw_div(clk_info, div);

		return clk_info->div.div_table[hw_div];
	}

	/* Impose hardware constraints */
	div = min_t(unsigned, div, 1 << clk_info->div.bits);
	div = max_t(unsigned, div, 1);

	/*
	 * If the divider value itself must be divided before being written to
	 * the divider register, we must ensure we don't have any bits set that
	 * would be lost as a result of doing so.
	 */
	div /= clk_info->div.div;
	div *= clk_info->div.div;

	return div;
}

static long
ingenic_clk_round_rate(struct clk_hw *hw, unsigned long req_rate,
		       unsigned long *parent_rate)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	unsigned int div = 1;

	if (clk_info->type & CGU_CLK_DIV)
		div = ingenic_clk_calc_div(clk_info, *parent_rate, req_rate);
	else if (clk_info->type & CGU_CLK_FIXDIV)
		div = clk_info->fixdiv.div;

	return DIV_ROUND_UP(*parent_rate, div);
}

static inline int ingenic_clk_check_stable(struct ingenic_cgu *cgu,
					   const struct ingenic_cgu_clk_info *clk_info)
{
	u32 reg;

	return readl_poll_timeout(cgu->base + clk_info->div.reg, reg,
				  !(reg & BIT(clk_info->div.busy_bit)),
				  0, 100 * USEC_PER_MSEC);
}

static int
ingenic_clk_set_rate(struct clk_hw *hw, unsigned long req_rate,
		     unsigned long parent_rate)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	unsigned long rate, flags;
	unsigned int hw_div, div;
	u32 reg, mask;
	int ret = 0;

	if (clk_info->type & CGU_CLK_DIV) {
		div = ingenic_clk_calc_div(clk_info, parent_rate, req_rate);
		rate = DIV_ROUND_UP(parent_rate, div);

		if (rate != req_rate)
			return -EINVAL;

		if (clk_info->div.div_table)
			hw_div = ingenic_clk_calc_hw_div(clk_info, div);
		else
			hw_div = ((div / clk_info->div.div) - 1);

		spin_lock_irqsave(&cgu->lock, flags);
		reg = readl(cgu->base + clk_info->div.reg);

		/* update the divide */
		mask = GENMASK(clk_info->div.bits - 1, 0);
		reg &= ~(mask << clk_info->div.shift);
		reg |= hw_div << clk_info->div.shift;

		/* clear the stop bit */
		if (clk_info->div.stop_bit != -1)
			reg &= ~BIT(clk_info->div.stop_bit);

		/* set the change enable bit */
		if (clk_info->div.ce_bit != -1)
			reg |= BIT(clk_info->div.ce_bit);

		/* update the hardware */
		writel(reg, cgu->base + clk_info->div.reg);

		/* wait for the change to take effect */
		if (clk_info->div.busy_bit != -1)
			ret = ingenic_clk_check_stable(cgu, clk_info);

		spin_unlock_irqrestore(&cgu->lock, flags);
		return ret;
	}

	return -EINVAL;
}

static int ingenic_clk_enable(struct clk_hw *hw)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	unsigned long flags;

	if (clk_info->type & CGU_CLK_GATE) {
		/* ungate the clock */
		spin_lock_irqsave(&cgu->lock, flags);
		ingenic_cgu_gate_set(cgu, &clk_info->gate, false);
		spin_unlock_irqrestore(&cgu->lock, flags);

		if (clk_info->gate.delay_us)
			udelay(clk_info->gate.delay_us);
	}

	return 0;
}

static void ingenic_clk_disable(struct clk_hw *hw)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	unsigned long flags;

	if (clk_info->type & CGU_CLK_GATE) {
		/* gate the clock */
		spin_lock_irqsave(&cgu->lock, flags);
		ingenic_cgu_gate_set(cgu, &clk_info->gate, true);
		spin_unlock_irqrestore(&cgu->lock, flags);
	}
}

static int ingenic_clk_is_enabled(struct clk_hw *hw)
{
	struct ingenic_clk *ingenic_clk = to_ingenic_clk(hw);
	const struct ingenic_cgu_clk_info *clk_info = to_clk_info(ingenic_clk);
	struct ingenic_cgu *cgu = ingenic_clk->cgu;
	int enabled = 1;

	if (clk_info->type & CGU_CLK_GATE)
		enabled = !ingenic_cgu_gate_get(cgu, &clk_info->gate);

	return enabled;
}

static const struct clk_ops ingenic_clk_ops = {
	.get_parent = ingenic_clk_get_parent,
	.set_parent = ingenic_clk_set_parent,

	.recalc_rate = ingenic_clk_recalc_rate,
	.round_rate = ingenic_clk_round_rate,
	.set_rate = ingenic_clk_set_rate,

	.enable = ingenic_clk_enable,
	.disable = ingenic_clk_disable,
	.is_enabled = ingenic_clk_is_enabled,
};

/*
 * Setup functions.
 */

static int ingenic_register_clock(struct ingenic_cgu *cgu, unsigned idx)
{
	const struct ingenic_cgu_clk_info *clk_info = &cgu->clock_info[idx];
	struct clk_init_data clk_init;
	struct ingenic_clk *ingenic_clk = NULL;
	struct clk *clk, *parent;
	const char *parent_names[4];
	unsigned caps, i, num_possible;
	int err = -EINVAL;

	BUILD_BUG_ON(ARRAY_SIZE(clk_info->parents) > ARRAY_SIZE(parent_names));

	if (clk_info->type == CGU_CLK_EXT) {
		clk = of_clk_get_by_name(cgu->np, clk_info->name);
		if (IS_ERR(clk)) {
			pr_err("%s: no external clock '%s' provided\n",
			       __func__, clk_info->name);
			err = -ENODEV;
			goto out;
		}
		err = clk_register_clkdev(clk, clk_info->name, NULL);
		if (err) {
			clk_put(clk);
			goto out;
		}
		cgu->clocks.clks[idx] = clk;
		return 0;
	}

	if (!clk_info->type) {
		pr_err("%s: no clock type specified for '%s'\n", __func__,
		       clk_info->name);
		goto out;
	}

	ingenic_clk = kzalloc(sizeof(*ingenic_clk), GFP_KERNEL);
	if (!ingenic_clk) {
		err = -ENOMEM;
		goto out;
	}

	ingenic_clk->hw.init = &clk_init;
	ingenic_clk->cgu = cgu;
	ingenic_clk->idx = idx;

	clk_init.name = clk_info->name;
	clk_init.flags = 0;
	clk_init.parent_names = parent_names;

	caps = clk_info->type;

	if (caps & (CGU_CLK_MUX | CGU_CLK_CUSTOM)) {
		clk_init.num_parents = 0;

		if (caps & CGU_CLK_MUX)
			num_possible = 1 << clk_info->mux.bits;
		else
			num_possible = ARRAY_SIZE(clk_info->parents);

		for (i = 0; i < num_possible; i++) {
			if (clk_info->parents[i] == -1)
				continue;

			parent = cgu->clocks.clks[clk_info->parents[i]];
			parent_names[clk_init.num_parents] =
				__clk_get_name(parent);
			clk_init.num_parents++;
		}

		BUG_ON(!clk_init.num_parents);
		BUG_ON(clk_init.num_parents > ARRAY_SIZE(parent_names));
	} else {
		BUG_ON(clk_info->parents[0] == -1);
		clk_init.num_parents = 1;
		parent = cgu->clocks.clks[clk_info->parents[0]];
		parent_names[0] = __clk_get_name(parent);
	}

	if (caps & CGU_CLK_CUSTOM) {
		clk_init.ops = clk_info->custom.clk_ops;

		caps &= ~CGU_CLK_CUSTOM;

		if (caps) {
			pr_err("%s: custom clock may not be combined with type 0x%x\n",
			       __func__, caps);
			goto out;
		}
	} else if (caps & CGU_CLK_PLL) {
		clk_init.ops = &ingenic_pll_ops;

		caps &= ~CGU_CLK_PLL;

		if (caps) {
			pr_err("%s: PLL may not be combined with type 0x%x\n",
			       __func__, caps);
			goto out;
		}
	} else {
		clk_init.ops = &ingenic_clk_ops;
	}

	/* nothing to do for gates or fixed dividers */
	caps &= ~(CGU_CLK_GATE | CGU_CLK_FIXDIV);

	if (caps & CGU_CLK_MUX) {
		if (!(caps & CGU_CLK_MUX_GLITCHFREE))
			clk_init.flags |= CLK_SET_PARENT_GATE;

		caps &= ~(CGU_CLK_MUX | CGU_CLK_MUX_GLITCHFREE);
	}

	if (caps & CGU_CLK_DIV) {
		caps &= ~CGU_CLK_DIV;
	} else {
		/* pass rate changes to the parent clock */
		clk_init.flags |= CLK_SET_RATE_PARENT;
	}

	if (caps) {
		pr_err("%s: unknown clock type 0x%x\n", __func__, caps);
		goto out;
	}

	clk = clk_register(NULL, &ingenic_clk->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register clock '%s'\n", __func__,
		       clk_info->name);
		err = PTR_ERR(clk);
		goto out;
	}

	err = clk_register_clkdev(clk, clk_info->name, NULL);
	if (err)
		goto out;

	cgu->clocks.clks[idx] = clk;
out:
	if (err)
		kfree(ingenic_clk);
	return err;
}

struct ingenic_cgu *
ingenic_cgu_new(const struct ingenic_cgu_clk_info *clock_info,
		unsigned num_clocks, struct device_node *np)
{
	struct ingenic_cgu *cgu;

	cgu = kzalloc(sizeof(*cgu), GFP_KERNEL);
	if (!cgu)
		goto err_out;

	cgu->base = of_iomap(np, 0);
	if (!cgu->base) {
		pr_err("%s: failed to map CGU registers\n", __func__);
		goto err_out_free;
	}

	cgu->np = np;
	cgu->clock_info = clock_info;
	cgu->clocks.clk_num = num_clocks;

	spin_lock_init(&cgu->lock);

	return cgu;

err_out_free:
	kfree(cgu);
err_out:
	return NULL;
}

int ingenic_cgu_register_clocks(struct ingenic_cgu *cgu)
{
	unsigned i;
	int err;

	cgu->clocks.clks = kcalloc(cgu->clocks.clk_num, sizeof(struct clk *),
				   GFP_KERNEL);
	if (!cgu->clocks.clks) {
		err = -ENOMEM;
		goto err_out;
	}

	for (i = 0; i < cgu->clocks.clk_num; i++) {
		err = ingenic_register_clock(cgu, i);
		if (err)
			goto err_out_unregister;
	}

	err = of_clk_add_provider(cgu->np, of_clk_src_onecell_get,
				  &cgu->clocks);
	if (err)
		goto err_out_unregister;

	return 0;

err_out_unregister:
	for (i = 0; i < cgu->clocks.clk_num; i++) {
		if (!cgu->clocks.clks[i])
			continue;
		if (cgu->clock_info[i].type & CGU_CLK_EXT)
			clk_put(cgu->clocks.clks[i]);
		else
			clk_unregister(cgu->clocks.clks[i]);
	}
	kfree(cgu->clocks.clks);
err_out:
	return err;
}
