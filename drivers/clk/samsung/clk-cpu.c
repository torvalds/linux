// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>
 *
 * This file contains the utility function to register CPU clock for Samsung
 * Exynos platforms. A CPU clock is defined as a clock supplied to a CPU or a
 * group of CPUs. The CPU clock is typically derived from a hierarchy of clock
 * blocks which includes mux and divider blocks. There are a number of other
 * auxiliary clocks supplied to the CPU domain such as the debug blocks and AXI
 * clock for CPU domain. The rates of these auxiliary clocks are related to the
 * CPU clock rate and this relation is usually specified in the hardware manual
 * of the SoC or supplied after the SoC characterization.
 *
 * The below implementation of the CPU clock allows the rate changes of the CPU
 * clock and the corresponding rate changes of the auxiliary clocks of the CPU
 * domain. The platform clock driver provides a clock register configuration
 * for each configurable rate which is then used to program the clock hardware
 * registers to achieve a fast coordinated rate change for all the CPU domain
 * clocks.
 *
 * On a rate change request for the CPU clock, the rate change is propagated
 * up to the PLL supplying the clock to the CPU domain clock blocks. While the
 * CPU domain PLL is reconfigured, the CPU domain clocks are driven using an
 * alternate clock source. If required, the alternate clock source is divided
 * down in order to keep the output clock rate within the previous OPP limits.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#include "clk.h"
#include "clk-cpu.h"

struct exynos_cpuclk;

typedef int (*exynos_rate_change_fn_t)(struct clk_notifier_data *ndata,
				       struct exynos_cpuclk *cpuclk);

/**
 * struct exynos_cpuclk_regs - Register offsets for CPU related clocks
 * @mux_sel: offset of CPU MUX_SEL register (for selecting MUX clock parent)
 * @mux_stat: offset of CPU MUX_STAT register (for checking MUX clock status)
 * @div_cpu0: offset of CPU DIV0 register (for modifying divider values)
 * @div_cpu1: offset of CPU DIV1 register (for modifying divider values)
 * @div_stat_cpu0: offset of CPU DIV0_STAT register (for checking DIV status)
 * @div_stat_cpu1: offset of CPU DIV1_STAT register (for checking DIV status)
 * @mux: offset of MUX register for choosing CPU clock source
 * @divs: offsets of DIV registers (ACLK, ATCLK, PCLKDBG and PERIPHCLK)
 */
struct exynos_cpuclk_regs {
	u32 mux_sel;
	u32 mux_stat;
	u32 div_cpu0;
	u32 div_cpu1;
	u32 div_stat_cpu0;
	u32 div_stat_cpu1;

	u32 mux;
	u32 divs[4];
};

/**
 * struct exynos_cpuclk_chip - Chip specific data for CPU clock
 * @regs: register offsets for CPU related clocks
 * @pre_rate_cb: callback to run before CPU clock rate change
 * @post_rate_cb: callback to run after CPU clock rate change
 */
struct exynos_cpuclk_chip {
	const struct exynos_cpuclk_regs		*regs;
	exynos_rate_change_fn_t			pre_rate_cb;
	exynos_rate_change_fn_t			post_rate_cb;
};

/**
 * struct exynos_cpuclk - information about clock supplied to a CPU core
 * @hw:		handle between CCF and CPU clock
 * @alt_parent:	alternate parent clock to use when switching the speed
 *		of the primary parent clock
 * @base:	start address of the CPU clock registers block
 * @lock:	cpu clock domain register access lock
 * @cfg:	cpu clock rate configuration data
 * @num_cfgs:	number of array elements in @cfg array
 * @clk_nb:	clock notifier registered for changes in clock speed of the
 *		primary parent clock
 * @flags:	configuration flags for the CPU clock
 * @chip:	chip-specific data for the CPU clock
 *
 * This structure holds information required for programming the CPU clock for
 * various clock speeds.
 */
struct exynos_cpuclk {
	struct clk_hw				hw;
	const struct clk_hw			*alt_parent;
	void __iomem				*base;
	spinlock_t				*lock;
	const struct exynos_cpuclk_cfg_data	*cfg;
	const unsigned long			num_cfgs;
	struct notifier_block			clk_nb;
	unsigned long				flags;
	const struct exynos_cpuclk_chip		*chip;
};

/* ---- Common code --------------------------------------------------------- */

/* Divider stabilization time, msec */
#define MAX_STAB_TIME		10
#define MAX_DIV			8
#define DIV_MASK		GENMASK(2, 0)
#define DIV_MASK_ALL		GENMASK(31, 0)
#define MUX_MASK		GENMASK(2, 0)

/*
 * Helper function to wait until divider(s) have stabilized after the divider
 * value has changed.
 */
static void wait_until_divider_stable(void __iomem *div_reg, unsigned long mask)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(MAX_STAB_TIME);

	do {
		if (!(readl(div_reg) & mask))
			return;
	} while (time_before(jiffies, timeout));

	if (!(readl(div_reg) & mask))
		return;

	pr_err("%s: timeout in divider stablization\n", __func__);
}

/*
 * Helper function to wait until mux has stabilized after the mux selection
 * value was changed.
 */
static void wait_until_mux_stable(void __iomem *mux_reg, u32 mux_pos,
				  unsigned long mask, unsigned long mux_value)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(MAX_STAB_TIME);

	do {
		if (((readl(mux_reg) >> mux_pos) & mask) == mux_value)
			return;
	} while (time_before(jiffies, timeout));

	if (((readl(mux_reg) >> mux_pos) & mask) == mux_value)
		return;

	pr_err("%s: re-parenting mux timed-out\n", __func__);
}

/*
 * Helper function to set the 'safe' dividers for the CPU clock. The parameters
 * div and mask contain the divider value and the register bit mask of the
 * dividers to be programmed.
 */
static void exynos_set_safe_div(struct exynos_cpuclk *cpuclk, unsigned long div,
				unsigned long mask)
{
	const struct exynos_cpuclk_regs * const regs = cpuclk->chip->regs;
	void __iomem *base = cpuclk->base;
	unsigned long div0;

	div0 = readl(base + regs->div_cpu0);
	div0 = (div0 & ~mask) | (div & mask);
	writel(div0, base + regs->div_cpu0);
	wait_until_divider_stable(base + regs->div_stat_cpu0, mask);
}

/* ---- Exynos 3/4/5 -------------------------------------------------------- */

#define E4210_DIV0_RATIO0_MASK	GENMASK(2, 0)
#define E4210_DIV1_HPM_MASK	GENMASK(6, 4)
#define E4210_DIV1_COPY_MASK	GENMASK(2, 0)
#define E4210_MUX_HPM_MASK	BIT(20)
#define E4210_DIV0_ATB_SHIFT	16
#define E4210_DIV0_ATB_MASK	(DIV_MASK << E4210_DIV0_ATB_SHIFT)

static const struct exynos_cpuclk_regs e4210_cpuclk_regs = {
	.mux_sel	= 0x200,
	.mux_stat	= 0x400,
	.div_cpu0	= 0x500,
	.div_cpu1	= 0x504,
	.div_stat_cpu0	= 0x600,
	.div_stat_cpu1	= 0x604,
};

/* handler for pre-rate change notification from parent clock */
static int exynos_cpuclk_pre_rate_change(struct clk_notifier_data *ndata,
					 struct exynos_cpuclk *cpuclk)
{
	const struct exynos_cpuclk_cfg_data *cfg_data = cpuclk->cfg;
	const struct exynos_cpuclk_regs * const regs = cpuclk->chip->regs;
	void __iomem *base = cpuclk->base;
	unsigned long alt_prate = clk_hw_get_rate(cpuclk->alt_parent);
	unsigned long div0, div1 = 0, mux_reg;
	unsigned long flags;

	/* find out the divider values to use for clock data */
	while ((cfg_data->prate * 1000) != ndata->new_rate) {
		if (cfg_data->prate == 0)
			return -EINVAL;
		cfg_data++;
	}

	spin_lock_irqsave(cpuclk->lock, flags);

	/*
	 * For the selected PLL clock frequency, get the pre-defined divider
	 * values. If the clock for sclk_hpm is not sourced from apll, then
	 * the values for DIV_COPY and DIV_HPM dividers need not be set.
	 */
	div0 = cfg_data->div0;
	if (cpuclk->flags & CLK_CPU_HAS_DIV1) {
		div1 = cfg_data->div1;
		if (readl(base + regs->mux_sel) & E4210_MUX_HPM_MASK)
			div1 = readl(base + regs->div_cpu1) &
				(E4210_DIV1_HPM_MASK | E4210_DIV1_COPY_MASK);
	}

	/*
	 * If the old parent clock speed is less than the clock speed of
	 * the alternate parent, then it should be ensured that at no point
	 * the armclk speed is more than the old_prate until the dividers are
	 * set.  Also workaround the issue of the dividers being set to lower
	 * values before the parent clock speed is set to new lower speed
	 * (this can result in too high speed of armclk output clocks).
	 */
	if (alt_prate > ndata->old_rate || ndata->old_rate > ndata->new_rate) {
		unsigned long tmp_rate = min(ndata->old_rate, ndata->new_rate);
		unsigned long alt_div, alt_div_mask = DIV_MASK;

		alt_div = DIV_ROUND_UP(alt_prate, tmp_rate) - 1;
		WARN_ON(alt_div >= MAX_DIV);

		if (cpuclk->flags & CLK_CPU_NEEDS_DEBUG_ALT_DIV) {
			/*
			 * In Exynos4210, ATB clock parent is also mout_core. So
			 * ATB clock also needs to be mantained at safe speed.
			 */
			alt_div |= E4210_DIV0_ATB_MASK;
			alt_div_mask |= E4210_DIV0_ATB_MASK;
		}
		exynos_set_safe_div(cpuclk, alt_div, alt_div_mask);
		div0 |= alt_div;
	}

	/* select sclk_mpll as the alternate parent */
	mux_reg = readl(base + regs->mux_sel);
	writel(mux_reg | (1 << 16), base + regs->mux_sel);
	wait_until_mux_stable(base + regs->mux_stat, 16, MUX_MASK, 2);

	/* alternate parent is active now. set the dividers */
	writel(div0, base + regs->div_cpu0);
	wait_until_divider_stable(base + regs->div_stat_cpu0, DIV_MASK_ALL);

	if (cpuclk->flags & CLK_CPU_HAS_DIV1) {
		writel(div1, base + regs->div_cpu1);
		wait_until_divider_stable(base + regs->div_stat_cpu1,
					  DIV_MASK_ALL);
	}

	spin_unlock_irqrestore(cpuclk->lock, flags);
	return 0;
}

/* handler for post-rate change notification from parent clock */
static int exynos_cpuclk_post_rate_change(struct clk_notifier_data *ndata,
					  struct exynos_cpuclk *cpuclk)
{
	const struct exynos_cpuclk_cfg_data *cfg_data = cpuclk->cfg;
	const struct exynos_cpuclk_regs * const regs = cpuclk->chip->regs;
	void __iomem *base = cpuclk->base;
	unsigned long div = 0, div_mask = DIV_MASK;
	unsigned long mux_reg;
	unsigned long flags;

	/* find out the divider values to use for clock data */
	if (cpuclk->flags & CLK_CPU_NEEDS_DEBUG_ALT_DIV) {
		while ((cfg_data->prate * 1000) != ndata->new_rate) {
			if (cfg_data->prate == 0)
				return -EINVAL;
			cfg_data++;
		}
	}

	spin_lock_irqsave(cpuclk->lock, flags);

	/* select mout_apll as the alternate parent */
	mux_reg = readl(base + regs->mux_sel);
	writel(mux_reg & ~(1 << 16), base + regs->mux_sel);
	wait_until_mux_stable(base + regs->mux_stat, 16, MUX_MASK, 1);

	if (cpuclk->flags & CLK_CPU_NEEDS_DEBUG_ALT_DIV) {
		div |= (cfg_data->div0 & E4210_DIV0_ATB_MASK);
		div_mask |= E4210_DIV0_ATB_MASK;
	}

	exynos_set_safe_div(cpuclk, div, div_mask);
	spin_unlock_irqrestore(cpuclk->lock, flags);
	return 0;
}

/* ---- Exynos5433 ---------------------------------------------------------- */

static const struct exynos_cpuclk_regs e5433_cpuclk_regs = {
	.mux_sel	= 0x208,
	.mux_stat	= 0x408,
	.div_cpu0	= 0x600,
	.div_cpu1	= 0x604,
	.div_stat_cpu0	= 0x700,
	.div_stat_cpu1	= 0x704,
};

/* handler for pre-rate change notification from parent clock */
static int exynos5433_cpuclk_pre_rate_change(struct clk_notifier_data *ndata,
					     struct exynos_cpuclk *cpuclk)
{
	const struct exynos_cpuclk_cfg_data *cfg_data = cpuclk->cfg;
	const struct exynos_cpuclk_regs * const regs = cpuclk->chip->regs;
	void __iomem *base = cpuclk->base;
	unsigned long alt_prate = clk_hw_get_rate(cpuclk->alt_parent);
	unsigned long div0, div1 = 0, mux_reg;
	unsigned long flags;

	/* find out the divider values to use for clock data */
	while ((cfg_data->prate * 1000) != ndata->new_rate) {
		if (cfg_data->prate == 0)
			return -EINVAL;
		cfg_data++;
	}

	spin_lock_irqsave(cpuclk->lock, flags);

	/*
	 * For the selected PLL clock frequency, get the pre-defined divider
	 * values.
	 */
	div0 = cfg_data->div0;
	div1 = cfg_data->div1;

	/*
	 * If the old parent clock speed is less than the clock speed of
	 * the alternate parent, then it should be ensured that at no point
	 * the armclk speed is more than the old_prate until the dividers are
	 * set.  Also workaround the issue of the dividers being set to lower
	 * values before the parent clock speed is set to new lower speed
	 * (this can result in too high speed of armclk output clocks).
	 */
	if (alt_prate > ndata->old_rate || ndata->old_rate > ndata->new_rate) {
		unsigned long tmp_rate = min(ndata->old_rate, ndata->new_rate);
		unsigned long alt_div, alt_div_mask = DIV_MASK;

		alt_div = DIV_ROUND_UP(alt_prate, tmp_rate) - 1;
		WARN_ON(alt_div >= MAX_DIV);

		exynos_set_safe_div(cpuclk, alt_div, alt_div_mask);
		div0 |= alt_div;
	}

	/* select the alternate parent */
	mux_reg = readl(base + regs->mux_sel);
	writel(mux_reg | 1, base + regs->mux_sel);
	wait_until_mux_stable(base + regs->mux_stat, 0, MUX_MASK, 2);

	/* alternate parent is active now. set the dividers */
	writel(div0, base + regs->div_cpu0);
	wait_until_divider_stable(base + regs->div_stat_cpu0, DIV_MASK_ALL);

	writel(div1, base + regs->div_cpu1);
	wait_until_divider_stable(base + regs->div_stat_cpu1, DIV_MASK_ALL);

	spin_unlock_irqrestore(cpuclk->lock, flags);
	return 0;
}

/* handler for post-rate change notification from parent clock */
static int exynos5433_cpuclk_post_rate_change(struct clk_notifier_data *ndata,
					      struct exynos_cpuclk *cpuclk)
{
	const struct exynos_cpuclk_regs * const regs = cpuclk->chip->regs;
	void __iomem *base = cpuclk->base;
	unsigned long div = 0, div_mask = DIV_MASK;
	unsigned long mux_reg;
	unsigned long flags;

	spin_lock_irqsave(cpuclk->lock, flags);

	/* select apll as the alternate parent */
	mux_reg = readl(base + regs->mux_sel);
	writel(mux_reg & ~1, base + regs->mux_sel);
	wait_until_mux_stable(base + regs->mux_stat, 0, MUX_MASK, 1);

	exynos_set_safe_div(cpuclk, div, div_mask);
	spin_unlock_irqrestore(cpuclk->lock, flags);
	return 0;
}

/* ---- Exynos850 ----------------------------------------------------------- */

#define E850_DIV_RATIO_MASK	GENMASK(3, 0)
#define E850_BUSY_MASK		BIT(16)

/* Max time for divider or mux to stabilize, usec */
#define E850_DIV_MUX_STAB_TIME	100
/* OSCCLK clock rate, Hz */
#define E850_OSCCLK		(26 * MHZ)

static const struct exynos_cpuclk_regs e850cl0_cpuclk_regs = {
	.mux	= 0x100c,
	.divs	= { 0x1800, 0x1808, 0x180c, 0x1810 },
};

static const struct exynos_cpuclk_regs e850cl1_cpuclk_regs = {
	.mux	= 0x1000,
	.divs	= { 0x1800, 0x1808, 0x180c, 0x1810 },
};

/*
 * Set alternate parent rate to "rate" value or less.
 *
 * rate: Desired alt_parent rate, or 0 for max alt_parent rate
 *
 * Exynos850 doesn't have CPU clock divider in CMU_CPUCLx block (CMUREF divider
 * doesn't affect CPU speed). So CPUCLx_SWITCH divider from CMU_TOP is used
 * instead to adjust alternate parent speed.
 *
 * It's possible to use clk_set_max_rate() instead of this function, but it
 * would set overly pessimistic rate values to alternate parent.
 */
static int exynos850_alt_parent_set_max_rate(const struct clk_hw *alt_parent,
					     unsigned long rate)
{
	struct clk_hw *clk_div, *clk_divp;
	unsigned long divp_rate, div_rate, div;
	int ret;

	/* Divider from CMU_TOP */
	clk_div = clk_hw_get_parent(alt_parent);
	if (!clk_div)
		return -ENOENT;
	/* Divider's parent from CMU_TOP */
	clk_divp = clk_hw_get_parent(clk_div);
	if (!clk_divp)
		return -ENOENT;
	/* Divider input rate */
	divp_rate = clk_hw_get_rate(clk_divp);
	if (!divp_rate)
		return -EINVAL;

	/* Calculate new alt_parent rate for integer divider value */
	if (rate == 0)
		div = 1;
	else
		div = DIV_ROUND_UP(divp_rate, rate);
	div_rate = DIV_ROUND_UP(divp_rate, div);
	WARN_ON(div >= MAX_DIV);

	/* alt_parent will propagate this change up to the divider */
	ret = clk_set_rate(alt_parent->clk, div_rate);
	if (ret)
		return ret;
	udelay(E850_DIV_MUX_STAB_TIME);

	return 0;
}

/* Handler for pre-rate change notification from parent clock */
static int exynos850_cpuclk_pre_rate_change(struct clk_notifier_data *ndata,
					    struct exynos_cpuclk *cpuclk)
{
	const unsigned int shifts[4] = { 16, 12, 8, 4 }; /* E850_CPU_DIV0() */
	const struct exynos_cpuclk_regs * const regs = cpuclk->chip->regs;
	const struct exynos_cpuclk_cfg_data *cfg_data = cpuclk->cfg;
	const struct clk_hw *alt_parent = cpuclk->alt_parent;
	void __iomem *base = cpuclk->base;
	unsigned long alt_prate = clk_hw_get_rate(alt_parent);
	unsigned long flags;
	u32 mux_reg;
	size_t i;
	int ret;

	/* No actions are needed when switching to or from OSCCLK parent */
	if (ndata->new_rate == E850_OSCCLK || ndata->old_rate == E850_OSCCLK)
		return 0;

	/* Find out the divider values to use for clock data */
	while ((cfg_data->prate * 1000) != ndata->new_rate) {
		if (cfg_data->prate == 0)
			return -EINVAL;
		cfg_data++;
	}

	/*
	 * If the old parent clock speed is less than the clock speed of
	 * the alternate parent, then it should be ensured that at no point
	 * the armclk speed is more than the old_prate until the dividers are
	 * set.  Also workaround the issue of the dividers being set to lower
	 * values before the parent clock speed is set to new lower speed
	 * (this can result in too high speed of armclk output clocks).
	 */
	if (alt_prate > ndata->old_rate || ndata->old_rate > ndata->new_rate) {
		unsigned long tmp_rate = min(ndata->old_rate, ndata->new_rate);

		ret = exynos850_alt_parent_set_max_rate(alt_parent, tmp_rate);
		if (ret)
			return ret;
	}

	spin_lock_irqsave(cpuclk->lock, flags);

	/* Select the alternate parent */
	mux_reg = readl(base + regs->mux);
	writel(mux_reg | 1, base + regs->mux);
	wait_until_mux_stable(base + regs->mux, 16, 1, 0);

	/* Alternate parent is active now. Set the dividers */
	for (i = 0; i < ARRAY_SIZE(shifts); ++i) {
		unsigned long div = (cfg_data->div0 >> shifts[i]) & 0xf;
		u32 val;

		val = readl(base + regs->divs[i]);
		val = (val & ~E850_DIV_RATIO_MASK) | div;
		writel(val, base + regs->divs[i]);
		wait_until_divider_stable(base + regs->divs[i], E850_BUSY_MASK);
	}

	spin_unlock_irqrestore(cpuclk->lock, flags);

	return 0;
}

/* Handler for post-rate change notification from parent clock */
static int exynos850_cpuclk_post_rate_change(struct clk_notifier_data *ndata,
					     struct exynos_cpuclk *cpuclk)
{
	const struct exynos_cpuclk_regs * const regs = cpuclk->chip->regs;
	const struct clk_hw *alt_parent = cpuclk->alt_parent;
	void __iomem *base = cpuclk->base;
	unsigned long flags;
	u32 mux_reg;

	/* No actions are needed when switching to or from OSCCLK parent */
	if (ndata->new_rate == E850_OSCCLK || ndata->old_rate == E850_OSCCLK)
		return 0;

	spin_lock_irqsave(cpuclk->lock, flags);

	/* Select main parent (PLL) for mux */
	mux_reg = readl(base + regs->mux);
	writel(mux_reg & ~1, base + regs->mux);
	wait_until_mux_stable(base + regs->mux, 16, 1, 0);

	spin_unlock_irqrestore(cpuclk->lock, flags);

	/* Set alt_parent rate back to max */
	return exynos850_alt_parent_set_max_rate(alt_parent, 0);
}

/* -------------------------------------------------------------------------- */

/* Common round rate callback usable for all types of CPU clocks */
static long exynos_cpuclk_round_rate(struct clk_hw *hw, unsigned long drate,
				     unsigned long *prate)
{
	struct clk_hw *parent = clk_hw_get_parent(hw);
	*prate = clk_hw_round_rate(parent, drate);
	return *prate;
}

/* Common recalc rate callback usable for all types of CPU clocks */
static unsigned long exynos_cpuclk_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	/*
	 * The CPU clock output (armclk) rate is the same as its parent
	 * rate. Although there exist certain dividers inside the CPU
	 * clock block that could be used to divide the parent clock,
	 * the driver does not make use of them currently, except during
	 * frequency transitions.
	 */
	return parent_rate;
}

static const struct clk_ops exynos_cpuclk_clk_ops = {
	.recalc_rate = exynos_cpuclk_recalc_rate,
	.round_rate = exynos_cpuclk_round_rate,
};

/*
 * This notifier function is called for the pre-rate and post-rate change
 * notifications of the parent clock of cpuclk.
 */
static int exynos_cpuclk_notifier_cb(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct exynos_cpuclk *cpuclk;
	int err = 0;

	cpuclk = container_of(nb, struct exynos_cpuclk, clk_nb);

	if (event == PRE_RATE_CHANGE)
		err = cpuclk->chip->pre_rate_cb(ndata, cpuclk);
	else if (event == POST_RATE_CHANGE)
		err = cpuclk->chip->post_rate_cb(ndata, cpuclk);

	return notifier_from_errno(err);
}

static const struct exynos_cpuclk_chip exynos_clkcpu_chips[] = {
	[CPUCLK_LAYOUT_E4210] = {
		.regs		= &e4210_cpuclk_regs,
		.pre_rate_cb	= exynos_cpuclk_pre_rate_change,
		.post_rate_cb	= exynos_cpuclk_post_rate_change,
	},
	[CPUCLK_LAYOUT_E5433] = {
		.regs		= &e5433_cpuclk_regs,
		.pre_rate_cb	= exynos5433_cpuclk_pre_rate_change,
		.post_rate_cb	= exynos5433_cpuclk_post_rate_change,
	},
	[CPUCLK_LAYOUT_E850_CL0] = {
		.regs		= &e850cl0_cpuclk_regs,
		.pre_rate_cb	= exynos850_cpuclk_pre_rate_change,
		.post_rate_cb	= exynos850_cpuclk_post_rate_change,
	},
	[CPUCLK_LAYOUT_E850_CL1] = {
		.regs		= &e850cl1_cpuclk_regs,
		.pre_rate_cb	= exynos850_cpuclk_pre_rate_change,
		.post_rate_cb	= exynos850_cpuclk_post_rate_change,
	},
};

/* helper function to register a CPU clock */
static int __init exynos_register_cpu_clock(struct samsung_clk_provider *ctx,
				const struct samsung_cpu_clock *clk_data)
{
	const struct clk_hw *parent, *alt_parent;
	struct clk_hw **hws;
	struct exynos_cpuclk *cpuclk;
	struct clk_init_data init;
	const char *parent_name;
	unsigned int num_cfgs;
	int ret = 0;

	hws = ctx->clk_data.hws;
	parent = hws[clk_data->parent_id];
	alt_parent = hws[clk_data->alt_parent_id];
	if (IS_ERR(parent) || IS_ERR(alt_parent)) {
		pr_err("%s: invalid parent clock(s)\n", __func__);
		return -EINVAL;
	}

	cpuclk = kzalloc(sizeof(*cpuclk), GFP_KERNEL);
	if (!cpuclk)
		return -ENOMEM;

	parent_name = clk_hw_get_name(parent);

	init.name = clk_data->name;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.ops = &exynos_cpuclk_clk_ops;

	cpuclk->alt_parent = alt_parent;
	cpuclk->hw.init = &init;
	cpuclk->base = ctx->reg_base + clk_data->offset;
	cpuclk->lock = &ctx->lock;
	cpuclk->flags = clk_data->flags;
	cpuclk->clk_nb.notifier_call = exynos_cpuclk_notifier_cb;
	cpuclk->chip = &exynos_clkcpu_chips[clk_data->reg_layout];

	ret = clk_notifier_register(parent->clk, &cpuclk->clk_nb);
	if (ret) {
		pr_err("%s: failed to register clock notifier for %s\n",
		       __func__, clk_data->name);
		goto free_cpuclk;
	}

	/* Find count of configuration rates in cfg */
	for (num_cfgs = 0; clk_data->cfg[num_cfgs].prate != 0; )
		num_cfgs++;

	cpuclk->cfg = kmemdup_array(clk_data->cfg, num_cfgs, sizeof(*cpuclk->cfg),
				    GFP_KERNEL);
	if (!cpuclk->cfg) {
		ret = -ENOMEM;
		goto unregister_clk_nb;
	}

	ret = clk_hw_register(NULL, &cpuclk->hw);
	if (ret) {
		pr_err("%s: could not register cpuclk %s\n", __func__,
		       clk_data->name);
		goto free_cpuclk_data;
	}

	samsung_clk_add_lookup(ctx, &cpuclk->hw, clk_data->id);
	return 0;

free_cpuclk_data:
	kfree(cpuclk->cfg);
unregister_clk_nb:
	clk_notifier_unregister(parent->clk, &cpuclk->clk_nb);
free_cpuclk:
	kfree(cpuclk);
	return ret;
}

void __init samsung_clk_register_cpu(struct samsung_clk_provider *ctx,
		const struct samsung_cpu_clock *list, unsigned int nr_clk)
{
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++)
		exynos_register_cpu_clock(ctx, &list[idx]);
}
