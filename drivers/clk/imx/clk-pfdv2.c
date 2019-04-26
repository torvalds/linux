// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017~2018 NXP
 *
 * Author: Dong Aisheng <aisheng.dong@nxp.com>
 *
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

#include "clk.h"

/**
 * struct clk_pfdv2 - IMX PFD clock
 * @clk_hw:	clock source
 * @reg:	PFD register address
 * @gate_bit:	Gate bit offset
 * @vld_bit:	Valid bit offset
 * @frac_off:	PLL Fractional Divider offset
 */

struct clk_pfdv2 {
	struct clk_hw	hw;
	void __iomem	*reg;
	u8		gate_bit;
	u8		vld_bit;
	u8		frac_off;
};

#define to_clk_pfdv2(_hw) container_of(_hw, struct clk_pfdv2, hw)

#define CLK_PFDV2_FRAC_MASK 0x3f

#define LOCK_TIMEOUT_US		USEC_PER_MSEC

static DEFINE_SPINLOCK(pfd_lock);

static int clk_pfdv2_wait(struct clk_pfdv2 *pfd)
{
	u32 val;

	return readl_poll_timeout(pfd->reg, val, val & (1 << pfd->vld_bit),
				  0, LOCK_TIMEOUT_US);
}

static int clk_pfdv2_enable(struct clk_hw *hw)
{
	struct clk_pfdv2 *pfd = to_clk_pfdv2(hw);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&pfd_lock, flags);
	val = readl_relaxed(pfd->reg);
	val &= ~(1 << pfd->gate_bit);
	writel_relaxed(val, pfd->reg);
	spin_unlock_irqrestore(&pfd_lock, flags);

	return clk_pfdv2_wait(pfd);
}

static void clk_pfdv2_disable(struct clk_hw *hw)
{
	struct clk_pfdv2 *pfd = to_clk_pfdv2(hw);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&pfd_lock, flags);
	val = readl_relaxed(pfd->reg);
	val |= (1 << pfd->gate_bit);
	writel_relaxed(val, pfd->reg);
	spin_unlock_irqrestore(&pfd_lock, flags);
}

static unsigned long clk_pfdv2_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_pfdv2 *pfd = to_clk_pfdv2(hw);
	u64 tmp = parent_rate;
	u8 frac;

	frac = (readl_relaxed(pfd->reg) >> pfd->frac_off)
		& CLK_PFDV2_FRAC_MASK;

	if (!frac) {
		pr_debug("clk_pfdv2: %s invalid pfd frac value 0\n",
			 clk_hw_get_name(hw));
		return 0;
	}

	tmp *= 18;
	do_div(tmp, frac);

	return tmp;
}

static long clk_pfdv2_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	u64 tmp = *prate;
	u8 frac;

	tmp = tmp * 18 + rate / 2;
	do_div(tmp, rate);
	frac = tmp;

	if (frac < 12)
		frac = 12;
	else if (frac > 35)
		frac = 35;

	tmp = *prate;
	tmp *= 18;
	do_div(tmp, frac);

	return tmp;
}

static int clk_pfdv2_is_enabled(struct clk_hw *hw)
{
	struct clk_pfdv2 *pfd = to_clk_pfdv2(hw);

	if (readl_relaxed(pfd->reg) & (1 << pfd->gate_bit))
		return 0;

	return 1;
}

static int clk_pfdv2_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct clk_pfdv2 *pfd = to_clk_pfdv2(hw);
	unsigned long flags;
	u64 tmp = parent_rate;
	u32 val;
	u8 frac;

	tmp = tmp * 18 + rate / 2;
	do_div(tmp, rate);
	frac = tmp;
	if (frac < 12)
		frac = 12;
	else if (frac > 35)
		frac = 35;

	spin_lock_irqsave(&pfd_lock, flags);
	val = readl_relaxed(pfd->reg);
	val &= ~(CLK_PFDV2_FRAC_MASK << pfd->frac_off);
	val |= frac << pfd->frac_off;
	writel_relaxed(val, pfd->reg);
	spin_unlock_irqrestore(&pfd_lock, flags);

	return 0;
}

static const struct clk_ops clk_pfdv2_ops = {
	.enable		= clk_pfdv2_enable,
	.disable	= clk_pfdv2_disable,
	.recalc_rate	= clk_pfdv2_recalc_rate,
	.round_rate	= clk_pfdv2_round_rate,
	.set_rate	= clk_pfdv2_set_rate,
	.is_enabled     = clk_pfdv2_is_enabled,
};

struct clk_hw *imx_clk_pfdv2(const char *name, const char *parent_name,
			     void __iomem *reg, u8 idx)
{
	struct clk_init_data init;
	struct clk_pfdv2 *pfd;
	struct clk_hw *hw;
	int ret;

	WARN_ON(idx > 3);

	pfd = kzalloc(sizeof(*pfd), GFP_KERNEL);
	if (!pfd)
		return ERR_PTR(-ENOMEM);

	pfd->reg = reg;
	pfd->gate_bit = (idx + 1) * 8 - 1;
	pfd->vld_bit = pfd->gate_bit - 1;
	pfd->frac_off = idx * 8;

	init.name = name;
	init.ops = &clk_pfdv2_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_GATE;

	pfd->hw.init = &init;

	hw = &pfd->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pfd);
		hw = ERR_PTR(ret);
	}

	return hw;
}
