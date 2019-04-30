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

/* PLL Control Status Register (xPLLCSR) */
#define PLL_CSR_OFFSET		0x0
#define PLL_VLD			BIT(24)
#define PLL_EN			BIT(0)

/* PLL Configuration Register (xPLLCFG) */
#define PLL_CFG_OFFSET		0x08
#define BP_PLL_MULT		16
#define BM_PLL_MULT		(0x7f << 16)

/* PLL Numerator Register (xPLLNUM) */
#define PLL_NUM_OFFSET		0x10

/* PLL Denominator Register (xPLLDENOM) */
#define PLL_DENOM_OFFSET	0x14

#define MAX_MFD			0x3fffffff
#define DEFAULT_MFD		1000000

struct clk_pllv4 {
	struct clk_hw	hw;
	void __iomem	*base;
};

/* Valid PLL MULT Table */
static const int pllv4_mult_table[] = {33, 27, 22, 20, 17, 16};

#define to_clk_pllv4(__hw) container_of(__hw, struct clk_pllv4, hw)

#define LOCK_TIMEOUT_US		USEC_PER_MSEC

static inline int clk_pllv4_wait_lock(struct clk_pllv4 *pll)
{
	u32 csr;

	return readl_poll_timeout(pll->base  + PLL_CSR_OFFSET,
				  csr, csr & PLL_VLD, 0, LOCK_TIMEOUT_US);
}

static int clk_pllv4_is_enabled(struct clk_hw *hw)
{
	struct clk_pllv4 *pll = to_clk_pllv4(hw);

	if (readl_relaxed(pll->base) & PLL_EN)
		return 1;

	return 0;
}

static unsigned long clk_pllv4_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_pllv4 *pll = to_clk_pllv4(hw);
	u32 mult, mfn, mfd;
	u64 temp64;

	mult = readl_relaxed(pll->base + PLL_CFG_OFFSET);
	mult &= BM_PLL_MULT;
	mult >>= BP_PLL_MULT;

	mfn = readl_relaxed(pll->base + PLL_NUM_OFFSET);
	mfd = readl_relaxed(pll->base + PLL_DENOM_OFFSET);
	temp64 = parent_rate;
	temp64 *= mfn;
	do_div(temp64, mfd);

	return (parent_rate * mult) + (u32)temp64;
}

static long clk_pllv4_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	unsigned long parent_rate = *prate;
	unsigned long round_rate, i;
	u32 mfn, mfd = DEFAULT_MFD;
	bool found = false;
	u64 temp64;

	for (i = 0; i < ARRAY_SIZE(pllv4_mult_table); i++) {
		round_rate = parent_rate * pllv4_mult_table[i];
		if (rate >= round_rate) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_warn("%s: unable to round rate %lu, parent rate %lu\n",
			clk_hw_get_name(hw), rate, parent_rate);
		return 0;
	}

	if (parent_rate <= MAX_MFD)
		mfd = parent_rate;

	temp64 = (u64)(rate - round_rate);
	temp64 *= mfd;
	do_div(temp64, parent_rate);
	mfn = temp64;

	/*
	 * NOTE: The value of numerator must always be configured to be
	 * less than the value of the denominator. If we can't get a proper
	 * pair of mfn/mfd, we simply return the round_rate without using
	 * the frac part.
	 */
	if (mfn >= mfd)
		return round_rate;

	temp64 = (u64)parent_rate;
	temp64 *= mfn;
	do_div(temp64, mfd);

	return round_rate + (u32)temp64;
}

static bool clk_pllv4_is_valid_mult(unsigned int mult)
{
	int i;

	/* check if mult is in valid MULT table */
	for (i = 0; i < ARRAY_SIZE(pllv4_mult_table); i++) {
		if (pllv4_mult_table[i] == mult)
			return true;
	}

	return false;
}

static int clk_pllv4_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct clk_pllv4 *pll = to_clk_pllv4(hw);
	u32 val, mult, mfn, mfd = DEFAULT_MFD;
	u64 temp64;

	mult = rate / parent_rate;

	if (!clk_pllv4_is_valid_mult(mult))
		return -EINVAL;

	if (parent_rate <= MAX_MFD)
		mfd = parent_rate;

	temp64 = (u64)(rate - mult * parent_rate);
	temp64 *= mfd;
	do_div(temp64, parent_rate);
	mfn = temp64;

	val = readl_relaxed(pll->base + PLL_CFG_OFFSET);
	val &= ~BM_PLL_MULT;
	val |= mult << BP_PLL_MULT;
	writel_relaxed(val, pll->base + PLL_CFG_OFFSET);

	writel_relaxed(mfn, pll->base + PLL_NUM_OFFSET);
	writel_relaxed(mfd, pll->base + PLL_DENOM_OFFSET);

	return 0;
}

static int clk_pllv4_enable(struct clk_hw *hw)
{
	u32 val;
	struct clk_pllv4 *pll = to_clk_pllv4(hw);

	val = readl_relaxed(pll->base);
	val |= PLL_EN;
	writel_relaxed(val, pll->base);

	return clk_pllv4_wait_lock(pll);
}

static void clk_pllv4_disable(struct clk_hw *hw)
{
	u32 val;
	struct clk_pllv4 *pll = to_clk_pllv4(hw);

	val = readl_relaxed(pll->base);
	val &= ~PLL_EN;
	writel_relaxed(val, pll->base);
}

static const struct clk_ops clk_pllv4_ops = {
	.recalc_rate	= clk_pllv4_recalc_rate,
	.round_rate	= clk_pllv4_round_rate,
	.set_rate	= clk_pllv4_set_rate,
	.enable		= clk_pllv4_enable,
	.disable	= clk_pllv4_disable,
	.is_enabled	= clk_pllv4_is_enabled,
};

struct clk_hw *imx_clk_pllv4(const char *name, const char *parent_name,
			  void __iomem *base)
{
	struct clk_pllv4 *pll;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->base = base;

	init.name = name;
	init.ops = &clk_pllv4_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_GATE;

	pll->hw.init = &init;

	hw = &pll->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pll);
		hw = ERR_PTR(ret);
	}

	return hw;
}
