/*
 * Copyright 2012-2015 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/imx_sema4.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include "clk.h"
#include "hardware.h"
#include "common.h"

#define PLL_NUM_OFFSET		0x10
#define PLL_DENOM_OFFSET	0x20
#define PLL_AV_NUM_OFFSET	0x20
#define PLL_AV_DENOM_OFFSET	0x30

#define BM_PLL_POWER		(0x1 << 12)
#define BM_PLL_LOCK		(0x1 << 31)
#define BM_PLL_ENABLE		(0x1 << 13)
#define BM_PLL_BYPASS		(0x1 << 16)
#define ENET_PLL_POWER		(0x1 << 5)

/**
 * struct clk_pllv3 - IMX PLL clock version 3
 * @clk_hw:	 clock source
 * @base:	 base address of PLL registers
 * @powerup_set: set POWER bit to power up the PLL
 * @powerdown:   pll powerdown offset bit
 * @div_mask:	 mask of divider bits
 * @div_shift:	 shift of divider bits
 *
 * IMX PLL clock version 3, found on i.MX6 series.  Divider for pllv3
 * is actually a multiplier, and always sits at bit 0.
 */
struct clk_pllv3 {
	struct clk_hw	hw;
	void __iomem	*base;
	bool		powerup_set;
	u32		powerdown;
	u32		div_mask;
	u32		div_shift;
	u32		num_offset;
	u32		denom_offset;
};

#define to_clk_pllv3(_hw) container_of(_hw, struct clk_pllv3, hw)

static int clk_pllv3_wait_lock(struct clk_pllv3 *pll)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(10);
	u32 val = readl_relaxed(pll->base) & pll->powerdown;

	/* No need to wait for lock when pll is not powered up */
	if ((pll->powerup_set && !val) || (!pll->powerup_set && val))
		return 0;

	/* Wait for PLL to lock */
	do {
		if (readl_relaxed(pll->base) & BM_PLL_LOCK)
			break;
		if (time_after(jiffies, timeout))
			break;
	} while (1);

	return readl_relaxed(pll->base) & BM_PLL_LOCK ? 0 : -ETIMEDOUT;
}

static int clk_pllv3_do_hardware(struct clk_hw *hw, bool enable)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	int ret;
	u32 val;

	val = readl_relaxed(pll->base);
	if (enable) {
		if (pll->powerup_set)
			val |= pll->powerdown;
		else
			val &= ~pll->powerdown;
		writel_relaxed(val, pll->base);

		ret = clk_pllv3_wait_lock(pll);
		if (ret)
			return ret;
	} else {
		if (pll->powerup_set)
			val &= ~pll->powerdown;
		else
			val |= pll->powerdown;
		writel_relaxed(val, pll->base);
	}

	return 0;
}

static void clk_pllv3_do_shared_clks(struct clk_hw *hw, bool enable)
{
	if (imx_src_is_m4_enabled() && cpu_is_imx6sx()) {
#ifdef CONFIG_SOC_IMX6SX
		if (!amp_power_mutex || !shared_mem) {
			if (enable)
				clk_pllv3_do_hardware(hw, enable);
			return;
		}

		imx_sema4_mutex_lock(amp_power_mutex);
		if (shared_mem->ca9_valid != SHARED_MEM_MAGIC_NUMBER ||
			shared_mem->cm4_valid != SHARED_MEM_MAGIC_NUMBER) {
			imx_sema4_mutex_unlock(amp_power_mutex);
			return;
		}

		if (!imx_update_shared_mem(hw, enable)) {
			imx_sema4_mutex_unlock(amp_power_mutex);
			return;
		}
		clk_pllv3_do_hardware(hw, enable);

		imx_sema4_mutex_unlock(amp_power_mutex);
#endif
	} else {
		clk_pllv3_do_hardware(hw, enable);
	}
}

static int clk_pllv3_prepare(struct clk_hw *hw)
{
	clk_pllv3_do_shared_clks(hw, true);

	return 0;
}

static void clk_pllv3_unprepare(struct clk_hw *hw)
{
	clk_pllv3_do_shared_clks(hw, false);
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

	return (parent_rate * div) + (u32)temp64;
}

static long clk_pllv3_av_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *prate)
{
	unsigned long parent_rate = *prate;
	unsigned long min_rate = parent_rate * 27;
	unsigned long max_rate = parent_rate * 54;
	u32 div;
	u32 mfn, mfd = 1000000;
	s64 temp64;

	if (rate > max_rate)
		rate = max_rate;
	else if (rate < min_rate)
		rate = min_rate;

	div = rate / parent_rate;
	temp64 = (u64) (rate - div * parent_rate);
	temp64 *= mfd;
	do_div(temp64, parent_rate);
	mfn = temp64;

	return parent_rate * div + parent_rate / mfd * mfn;
}

static int clk_pllv3_av_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	unsigned long min_rate = parent_rate * 27;
	unsigned long max_rate = parent_rate * 54;
	u32 val, div;
	u32 mfn, mfd = 1000000;
	s64 temp64;

	if (rate < min_rate || rate > max_rate)
		return -EINVAL;

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

static unsigned long clk_pllv3_enet_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	if (cpu_is_imx7d())
		return 1000000000;
	else
		return 500000000;
}

static const struct clk_ops clk_pllv3_enet_ops = {
	.prepare	= clk_pllv3_prepare,
	.unprepare	= clk_pllv3_unprepare,
	.is_prepared	= clk_pllv3_is_prepared,
	.recalc_rate	= clk_pllv3_enet_recalc_rate,
};

static unsigned long clk_pllv3_generic_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_pllv3 *pll = to_clk_pllv3(hw);
	u32 div = (readl_relaxed(pll->base) >> pll->div_shift)  & pll->div_mask;
	u32 mfn = readl_relaxed(pll->base + pll->num_offset);
	u32 mfd = readl_relaxed(pll->base + pll->denom_offset);
	u64 temp64 = (u64)parent_rate;

	temp64 *= mfn;
	do_div(temp64, mfd);

	return (parent_rate * ((div == 1) ? 22 : 20)) + (u32)temp64;
}

static const struct clk_ops clk_pllv3_generic_ops = {
	.prepare	= clk_pllv3_prepare,
	.unprepare	= clk_pllv3_unprepare,
	.is_prepared	= clk_pllv3_is_prepared,
	.recalc_rate	= clk_pllv3_generic_recalc_rate,
};

struct clk *imx_clk_pllv3(enum imx_pllv3_type type, const char *name,
			  const char *parent_name, void __iomem *base,
			  u32 div_mask)
{
	struct clk_pllv3 *pll;
	const struct clk_ops *ops;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	switch (type) {
	case IMX_PLLV3_SYS:
		ops = &clk_pllv3_sys_ops;
		break;
	case IMX_PLLV3_GENERIC:
		ops = &clk_pllv3_generic_ops;
		break;
	case IMX_PLLV3_SYSV2:
		ops = &clk_pllv3_ops;
		break;
	case IMX_PLLV3_USB_VF610:
		pll->div_shift = 1;
	case IMX_PLLV3_USB:
		ops = &clk_pllv3_ops;
		pll->powerup_set = true;
		break;
	case IMX_PLLV3_AV:
		ops = &clk_pllv3_av_ops;
		break;
	case IMX_PLLV3_ENET:
		ops = &clk_pllv3_enet_ops;
		break;
	default:
		ops = &clk_pllv3_ops;
	}

	pll->base = base;
	pll->div_mask = div_mask;
	pll->powerdown = BM_PLL_POWER;
	pll->num_offset = PLL_NUM_OFFSET;
	pll->denom_offset = PLL_DENOM_OFFSET;

	if (cpu_is_imx7d() && type == IMX_PLLV3_ENET)
		pll->powerdown = ENET_PLL_POWER;
	else if ((cpu_is_imx7d() && type == IMX_PLLV3_AV) ||
		(cpu_is_imx6() && type == IMX_PLLV3_GENERIC)) {
		pll->num_offset = PLL_AV_NUM_OFFSET;
		pll->denom_offset = PLL_AV_DENOM_OFFSET;
	}

	init.name = name;
	init.ops = ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}
