// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include "clk.h"

#define PLL_NUM_OFFSET		0x10
#define PLL_DENOM_OFFSET	0x20
#define PLL_IMX7_NUM_OFFSET	0x20
#define PLL_IMX7_DENOM_OFFSET	0x30

#define PLL_VF610_NUM_OFFSET	0x20
#define PLL_VF610_DENOM_OFFSET	0x30

#define BM_PLL_POWER		(0x1 << 12)
#define BM_PLL_LOCK		(0x1 << 31)
#define IMX7_ENET_PLL_POWER	(0x1 << 5)
#define IMX7_DDR_PLL_POWER	(0x1 << 20)

#define PLL_LOCK_TIMEOUT	10000

/**
 * struct clk_pllv3 - IMX PLL clock version 3
 * @hw:		clock source
 * @base:	 base address of PLL registers
 * @power_bit:	 pll power bit mask
 * @powerup_set: set power_bit to power up the PLL
 * @div_mask:	 mask of divider bits
 * @div_shift:	 shift of divider bits
 * @ref_clock:	reference clock rate
 * @num_offset:	num register offset
 * @denom_offset: denom register offset
 *
 * IMX PLL clock version 3, found on i.MX6 series.  Divider for pllv3
 * is actually a multiplier, and always sits at bit 0.
 */
struct clk_pllv3 {
	struct clk_hw	hw;
	void __iomem	*base;
	u32		power_bit;
	bool		powerup_set;
	u32		div_mask;
	u32		div_shift;
	unsigned long	ref_clock;
	u32		num_offset;
	u32		denom_offset;
};

#define to_clk_pllv3(_hw) container_of(_hw, struct clk_pllv3, hw)

static int clk_pllv3_wait_lock(struct clk_pllv3 *pll)
{
	u32 val = readl_relaxed(pll->base) & pll->power_bit;

	/* No need to wait for lock when pll is not powered up */
	if ((pll->powerup_set && !val) || (!pll->powerup_set && val))
		return 0;

	return readl_relaxed_poll_timeout(pll->base, val, val & BM_PLL_LOCK,
					  500, PLL_LOCK_TIMEOUT);
}

static int clk_pllv3_prepare(struct clk_hw *hw)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	u32 val;

	val = readl_relaxed(pll->base);
	if (pll->powerup_set)
		val |= pll->power_bit;
	else
		val &= ~pll->power_bit;
	writel_relaxed(val, pll->base);

	return clk_pllv3_wait_lock(pll);
}

static void clk_pllv3_unprepare(struct clk_hw *hw)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	u32 val;

	val = readl_relaxed(pll->base);
	if (pll->powerup_set)
		val &= ~pll->power_bit;
	else
		val |= pll->power_bit;
	writel_relaxed(val, pll->base);
}

static int clk_pllv3_is_prepared(struct clk_hw *hw)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);

	if (readl_relaxed(pll->base) & BM_PLL_LOCK)
		return 1;

	return 0;
}

static unsigned long clk_pllv3_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	u32 div = (readl_relaxed(pll->base) >> pll->div_shift)  & pll->div_mask;

	return (div == 1) ? parent_rate * 22 : parent_rate * 20;
}

static long clk_pllv3_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	unsigned long parent_rate = *prate;

	return (rate >= parent_rate * 22) ? parent_rate * 22 :
					    parent_rate * 20;
}

static int clk_pllv3_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	u32 val, div;

	if (rate == parent_rate * 22)
		div = 1;
	else if (rate == parent_rate * 20)
		div = 0;
	else
		return -EINVAL;

	val = readl_relaxed(pll->base);
	val &= ~(pll->div_mask << pll->div_shift);
	val |= (div << pll->div_shift);
	writel_relaxed(val, pll->base);

	return clk_pllv3_wait_lock(pll);
}

static const struct clk_ops clk_pllv3_ops = {
	.prepare	= clk_pllv3_prepare,
	.unprepare	= clk_pllv3_unprepare,
	.is_prepared	= clk_pllv3_is_prepared,
	.recalc_rate	= clk_pllv3_recalc_rate,
	.round_rate	= clk_pllv3_round_rate,
	.set_rate	= clk_pllv3_set_rate,
};

static unsigned long clk_pllv3_sys_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	u32 div = readl_relaxed(pll->base) & pll->div_mask;

	return parent_rate * div / 2;
}

static long clk_pllv3_sys_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	unsigned long parent_rate = *prate;
	unsigned long min_rate = parent_rate * 54 / 2;
	unsigned long max_rate = parent_rate * 108 / 2;
	u32 div;

	if (rate > max_rate)
		rate = max_rate;
	else if (rate < min_rate)
		rate = min_rate;
	div = rate * 2 / parent_rate;

	return parent_rate * div / 2;
}

static int clk_pllv3_sys_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	unsigned long min_rate = parent_rate * 54 / 2;
	unsigned long max_rate = parent_rate * 108 / 2;
	u32 val, div;

	if (rate < min_rate || rate > max_rate)
		return -EINVAL;

	div = rate * 2 / parent_rate;
	val = readl_relaxed(pll->base);
	val &= ~pll->div_mask;
	val |= div;
	writel_relaxed(val, pll->base);

	return clk_pllv3_wait_lock(pll);
}

static const struct clk_ops clk_pllv3_sys_ops = {
	.prepare	= clk_pllv3_prepare,
	.unprepare	= clk_pllv3_unprepare,
	.is_prepared	= clk_pllv3_is_prepared,
	.recalc_rate	= clk_pllv3_sys_recalc_rate,
	.round_rate	= clk_pllv3_sys_round_rate,
	.set_rate	= clk_pllv3_sys_set_rate,
};

static unsigned long clk_pllv3_av_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	u32 mfn = readl_relaxed(pll->base + pll->num_offset);
	u32 mfd = readl_relaxed(pll->base + pll->denom_offset);
	u32 div = readl_relaxed(pll->base) & pll->div_mask;
	u64 temp64 = (u64)parent_rate;

	temp64 *= mfn;
	do_div(temp64, mfd);

	return parent_rate * div + (unsigned long)temp64;
}

static long clk_pllv3_av_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *prate)
{
	unsigned long parent_rate = *prate;
	unsigned long min_rate = parent_rate * 27;
	unsigned long max_rate = parent_rate * 54;
	u32 div;
	u32 mfn, mfd = 1000000;
	u32 max_mfd = 0x3FFFFFFF;
	u64 temp64;

	if (rate > max_rate)
		rate = max_rate;
	else if (rate < min_rate)
		rate = min_rate;

	if (parent_rate <= max_mfd)
		mfd = parent_rate;

	div = rate / parent_rate;
	temp64 = (u64) (rate - div * parent_rate);
	temp64 *= mfd;
	do_div(temp64, parent_rate);
	mfn = temp64;

	temp64 = (u64)parent_rate;
	temp64 *= mfn;
	do_div(temp64, mfd);

	return parent_rate * div + (unsigned long)temp64;
}

static int clk_pllv3_av_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	unsigned long min_rate = parent_rate * 27;
	unsigned long max_rate = parent_rate * 54;
	u32 val, div;
	u32 mfn, mfd = 1000000;
	u32 max_mfd = 0x3FFFFFFF;
	u64 temp64;

	if (rate < min_rate || rate > max_rate)
		return -EINVAL;

	if (parent_rate <= max_mfd)
		mfd = parent_rate;

	div = rate / parent_rate;
	temp64 = (u64) (rate - div * parent_rate);
	temp64 *= mfd;
	do_div(temp64, parent_rate);
	mfn = temp64;

	val = readl_relaxed(pll->base);
	val &= ~pll->div_mask;
	val |= div;
	writel_relaxed(val, pll->base);
	writel_relaxed(mfn, pll->base + pll->num_offset);
	writel_relaxed(mfd, pll->base + pll->denom_offset);

	return clk_pllv3_wait_lock(pll);
}

static const struct clk_ops clk_pllv3_av_ops = {
	.prepare	= clk_pllv3_prepare,
	.unprepare	= clk_pllv3_unprepare,
	.is_prepared	= clk_pllv3_is_prepared,
	.recalc_rate	= clk_pllv3_av_recalc_rate,
	.round_rate	= clk_pllv3_av_round_rate,
	.set_rate	= clk_pllv3_av_set_rate,
};

struct clk_pllv3_vf610_mf {
	u32 mfi;	/* integer part, can be 20 or 22 */
	u32 mfn;	/* numerator, 30-bit value */
	u32 mfd;	/* denominator, 30-bit value, must be less than mfn */
};

static unsigned long clk_pllv3_vf610_mf_to_rate(unsigned long parent_rate,
		struct clk_pllv3_vf610_mf mf)
{
	u64 temp64;

	temp64 = parent_rate;
	temp64 *= mf.mfn;
	do_div(temp64, mf.mfd);

	return (parent_rate * mf.mfi) + temp64;
}

static struct clk_pllv3_vf610_mf clk_pllv3_vf610_rate_to_mf(
		unsigned long parent_rate, unsigned long rate)
{
	struct clk_pllv3_vf610_mf mf;
	u64 temp64;

	mf.mfi = (rate >= 22 * parent_rate) ? 22 : 20;
	mf.mfd = 0x3fffffff;	/* use max supported value for best accuracy */

	if (rate <= parent_rate * mf.mfi)
		mf.mfn = 0;
	else if (rate >= parent_rate * (mf.mfi + 1))
		mf.mfn = mf.mfd - 1;
	else {
		/* rate = parent_rate * (mfi + mfn/mfd) */
		temp64 = rate - parent_rate * mf.mfi;
		temp64 *= mf.mfd;
		do_div(temp64, parent_rate);
		mf.mfn = temp64;
	}

	return mf;
}

static unsigned long clk_pllv3_vf610_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	struct clk_pllv3_vf610_mf mf;

	mf.mfn = readl_relaxed(pll->base + pll->num_offset);
	mf.mfd = readl_relaxed(pll->base + pll->denom_offset);
	mf.mfi = (readl_relaxed(pll->base) & pll->div_mask) ? 22 : 20;

	return clk_pllv3_vf610_mf_to_rate(parent_rate, mf);
}

static long clk_pllv3_vf610_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *prate)
{
	struct clk_pllv3_vf610_mf mf = clk_pllv3_vf610_rate_to_mf(*prate, rate);

	return clk_pllv3_vf610_mf_to_rate(*prate, mf);
}

static int clk_pllv3_vf610_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	struct clk_pllv3_vf610_mf mf =
			clk_pllv3_vf610_rate_to_mf(parent_rate, rate);
	u32 val;

	val = readl_relaxed(pll->base);
	if (mf.mfi == 20)
		val &= ~pll->div_mask;	/* clear bit for mfi=20 */
	else
		val |= pll->div_mask;	/* set bit for mfi=22 */
	writel_relaxed(val, pll->base);

	writel_relaxed(mf.mfn, pll->base + pll->num_offset);
	writel_relaxed(mf.mfd, pll->base + pll->denom_offset);

	return clk_pllv3_wait_lock(pll);
}

static const struct clk_ops clk_pllv3_vf610_ops = {
	.prepare	= clk_pllv3_prepare,
	.unprepare	= clk_pllv3_unprepare,
	.is_prepared	= clk_pllv3_is_prepared,
	.recalc_rate	= clk_pllv3_vf610_recalc_rate,
	.round_rate	= clk_pllv3_vf610_round_rate,
	.set_rate	= clk_pllv3_vf610_set_rate,
};

static unsigned long clk_pllv3_enet_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);

	return pll->ref_clock;
}

static const struct clk_ops clk_pllv3_enet_ops = {
	.prepare	= clk_pllv3_prepare,
	.unprepare	= clk_pllv3_unprepare,
	.is_prepared	= clk_pllv3_is_prepared,
	.recalc_rate	= clk_pllv3_enet_recalc_rate,
};

struct clk_hw *imx_clk_hw_pllv3(enum imx_pllv3_type type, const char *name,
			  const char *parent_name, void __iomem *base,
			  u32 div_mask)
{
	struct clk_pllv3 *pll;
	const struct clk_ops *ops;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->power_bit = BM_PLL_POWER;
	pll->num_offset = PLL_NUM_OFFSET;
	pll->denom_offset = PLL_DENOM_OFFSET;

	switch (type) {
	case IMX_PLLV3_SYS:
		ops = &clk_pllv3_sys_ops;
		break;
	case IMX_PLLV3_SYS_VF610:
		ops = &clk_pllv3_vf610_ops;
		pll->num_offset = PLL_VF610_NUM_OFFSET;
		pll->denom_offset = PLL_VF610_DENOM_OFFSET;
		break;
	case IMX_PLLV3_USB_VF610:
		pll->div_shift = 1;
		fallthrough;
	case IMX_PLLV3_USB:
		ops = &clk_pllv3_ops;
		pll->powerup_set = true;
		break;
	case IMX_PLLV3_AV_IMX7:
		pll->num_offset = PLL_IMX7_NUM_OFFSET;
		pll->denom_offset = PLL_IMX7_DENOM_OFFSET;
		fallthrough;
	case IMX_PLLV3_AV:
		ops = &clk_pllv3_av_ops;
		break;
	case IMX_PLLV3_ENET_IMX7:
		pll->power_bit = IMX7_ENET_PLL_POWER;
		pll->ref_clock = 1000000000;
		ops = &clk_pllv3_enet_ops;
		break;
	case IMX_PLLV3_ENET:
		pll->ref_clock = 500000000;
		ops = &clk_pllv3_enet_ops;
		break;
	case IMX_PLLV3_DDR_IMX7:
		pll->power_bit = IMX7_DDR_PLL_POWER;
		pll->num_offset = PLL_IMX7_NUM_OFFSET;
		pll->denom_offset = PLL_IMX7_DENOM_OFFSET;
		ops = &clk_pllv3_av_ops;
		break;
	default:
		ops = &clk_pllv3_ops;
	}
	pll->base = base;
	pll->div_mask = div_mask;

	init.name = name;
	init.ops = ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll->hw.init = &init;
	hw = &pll->hw;

	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pll);
		return ERR_PTR(ret);
	}

	return hw;
}
