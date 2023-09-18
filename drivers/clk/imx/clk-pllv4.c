// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017~2018 NXP
 *
 * Author: Dong Aisheng <aisheng.dong@nxp.com>
 *
 */

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

#include "clk.h"

/* PLL Control Status Register (xPLLCSR) */
#define PLL_CSR_OFFSET		0x0
#define PLL_VLD			BIT(24)
#define PLL_EN			BIT(0)

/* PLL Configuration Register (xPLLCFG) */
#define PLL_CFG_OFFSET		0x08
#define IMX8ULP_PLL_CFG_OFFSET	0x10
#define BP_PLL_MULT		16
#define BM_PLL_MULT		(0x7f << 16)

/* PLL Numerator Register (xPLLNUM) */
#define PLL_NUM_OFFSET		0x10
#define IMX8ULP_PLL_NUM_OFFSET	0x1c

/* PLL Denominator Register (xPLLDENOM) */
#define PLL_DENOM_OFFSET	0x14
#define IMX8ULP_PLL_DENOM_OFFSET	0x18

#define MAX_MFD			0x3fffffff
#define DEFAULT_MFD		1000000

struct clk_pllv4 {
	struct clk_hw	hw;
	void __iomem	*base;
	u32		cfg_offset;
	u32		num_offset;
	u32		denom_offset;
	bool		use_mult_range;
};

/* Valid PLL MULT Table */
static const int pllv4_mult_table[] = {33, 27, 22, 20, 17, 16};

/* Valid PLL MULT range, (max, min) */
static const int pllv4_mult_range[] = {54, 27};

#define to_clk_pllv4(__hw) container_of(__hw, struct clk_pllv4, hw)

#define LOCK_TIMEOUT_US		USEC_PER_MSEC

static inline int clk_pllv4_wait_lock(struct clk_pllv4 *pll)
{
	u32 csr;

	return readl_poll_timeout(pll->base  + PLL_CSR_OFFSET,
				  csr, csr & PLL_VLD, 0, LOCK_TIMEOUT_US);
}

static int clk_pllv4_is_prepared(struct clk_hw *hw)
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

	mult = readl_relaxed(pll->base + pll->cfg_offset);
	mult &= BM_PLL_MULT;
	mult >>= BP_PLL_MULT;

	mfn = readl_relaxed(pll->base + pll->num_offset);
	mfd = readl_relaxed(pll->base + pll->denom_offset);
	temp64 = parent_rate;
	temp64 *= mfn;
	do_div(temp64, mfd);

	return (parent_rate * mult) + (u32)temp64;
}

static long clk_pllv4_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	struct clk_pllv4 *pll = to_clk_pllv4(hw);
	unsigned long parent_rate = *prate;
	unsigned long round_rate, i;
	u32 mfn, mfd = DEFAULT_MFD;
	bool found = false;
	u64 temp64;
	u32 mult;

	if (pll->use_mult_range) {
		temp64 = (u64)rate;
		do_div(temp64, parent_rate);
		mult = temp64;
		if (mult >= pllv4_mult_range[1] &&
		    mult <= pllv4_mult_range[0]) {
			round_rate = parent_rate * mult;
			found = true;
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(pllv4_mult_table); i++) {
			round_rate = parent_rate * pllv4_mult_table[i];
			if (rate >= round_rate) {
				found = true;
				break;
			}
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

static bool clk_pllv4_is_valid_mult(struct clk_pllv4 *pll, unsigned int mult)
{
	int i;

	/* check if mult is in valid MULT table */
	if (pll->use_mult_range) {
		if (mult >= pllv4_mult_range[1] &&
		    mult <= pllv4_mult_range[0])
			return true;
	} else {
		for (i = 0; i < ARRAY_SIZE(pllv4_mult_table); i++) {
			if (pllv4_mult_table[i] == mult)
				return true;
		}
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

	if (!clk_pllv4_is_valid_mult(pll, mult))
		return -EINVAL;

	if (parent_rate <= MAX_MFD)
		mfd = parent_rate;

	temp64 = (u64)(rate - mult * parent_rate);
	temp64 *= mfd;
	do_div(temp64, parent_rate);
	mfn = temp64;

	val = readl_relaxed(pll->base + pll->cfg_offset);
	val &= ~BM_PLL_MULT;
	val |= mult << BP_PLL_MULT;
	writel_relaxed(val, pll->base + pll->cfg_offset);

	writel_relaxed(mfn, pll->base + pll->num_offset);
	writel_relaxed(mfd, pll->base + pll->denom_offset);

	return 0;
}

static int clk_pllv4_prepare(struct clk_hw *hw)
{
	u32 val;
	struct clk_pllv4 *pll = to_clk_pllv4(hw);

	val = readl_relaxed(pll->base);
	val |= PLL_EN;
	writel_relaxed(val, pll->base);

	return clk_pllv4_wait_lock(pll);
}

static void clk_pllv4_unprepare(struct clk_hw *hw)
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
	.prepare	= clk_pllv4_prepare,
	.unprepare	= clk_pllv4_unprepare,
	.is_prepared	= clk_pllv4_is_prepared,
};

struct clk_hw *imx_clk_hw_pllv4(enum imx_pllv4_type type, const char *name,
		 const char *parent_name, void __iomem *base)
{
	struct clk_pllv4 *pll;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->base = base;

	if (type == IMX_PLLV4_IMX8ULP ||
	    type == IMX_PLLV4_IMX8ULP_1GHZ) {
		pll->cfg_offset = IMX8ULP_PLL_CFG_OFFSET;
		pll->num_offset = IMX8ULP_PLL_NUM_OFFSET;
		pll->denom_offset = IMX8ULP_PLL_DENOM_OFFSET;
		if (type == IMX_PLLV4_IMX8ULP_1GHZ)
			pll->use_mult_range = true;
	} else {
		pll->cfg_offset = PLL_CFG_OFFSET;
		pll->num_offset = PLL_NUM_OFFSET;
		pll->denom_offset = PLL_DENOM_OFFSET;
	}

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
EXPORT_SYMBOL_GPL(imx_clk_hw_pllv4);
