// SPDX-License-Identifier: GPL-2.0
/*
 * PLL clock driver for TI Davinci SoCs
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 *
 * Based on arch/arm/mach-davinci/clock.c
 * Copyright (C) 2006-2007 Texas Instruments.
 * Copyright (C) 2008-2009 Deep Root Systems, LLC
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clk/davinci.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_data/clk-davinci-pll.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "pll.h"

#define MAX_NAME_SIZE	20
#define OSCIN_CLK_NAME	"oscin"

#define REVID		0x000
#define PLLCTL		0x100
#define OCSEL		0x104
#define PLLSECCTL	0x108
#define PLLM		0x110
#define PREDIV		0x114
#define PLLDIV1		0x118
#define PLLDIV2		0x11c
#define PLLDIV3		0x120
#define OSCDIV		0x124
#define POSTDIV		0x128
#define BPDIV		0x12c
#define PLLCMD		0x138
#define PLLSTAT		0x13c
#define ALNCTL		0x140
#define DCHANGE		0x144
#define CKEN		0x148
#define CKSTAT		0x14c
#define SYSTAT		0x150
#define PLLDIV4		0x160
#define PLLDIV5		0x164
#define PLLDIV6		0x168
#define PLLDIV7		0x16c
#define PLLDIV8		0x170
#define PLLDIV9		0x174

#define PLLCTL_PLLEN		BIT(0)
#define PLLCTL_PLLPWRDN		BIT(1)
#define PLLCTL_PLLRST		BIT(3)
#define PLLCTL_PLLDIS		BIT(4)
#define PLLCTL_PLLENSRC		BIT(5)
#define PLLCTL_CLKMODE		BIT(8)

/* shared by most *DIV registers */
#define DIV_RATIO_SHIFT		0
#define DIV_RATIO_WIDTH		5
#define DIV_ENABLE_SHIFT	15

#define PLLCMD_GOSET		BIT(0)
#define PLLSTAT_GOSTAT		BIT(0)

#define CKEN_OBSCLK_SHIFT	1
#define CKEN_AUXEN_SHIFT	0

/*
 * OMAP-L138 system reference guide recommends a wait for 4 OSCIN/CLKIN
 * cycles to ensure that the PLLC has switched to bypass mode. Delay of 1us
 * ensures we are good for all > 4MHz OSCIN/CLKIN inputs. Typically the input
 * is ~25MHz. Units are micro seconds.
 */
#define PLL_BYPASS_TIME		1

/* From OMAP-L138 datasheet table 6-4. Units are micro seconds */
#define PLL_RESET_TIME		1

/*
 * From OMAP-L138 datasheet table 6-4; assuming prediv = 1, sqrt(pllm) = 4
 * Units are micro seconds.
 */
#define PLL_LOCK_TIME		20

/**
 * struct davinci_pll_clk - Main PLL clock (aka PLLOUT)
 * @hw: clk_hw for the pll
 * @base: Base memory address
 * @pllm_min: The minimum allowable PLLM[PLLM] value
 * @pllm_max: The maxiumum allowable PLLM[PLLM] value
 * @pllm_mask: Bitmask for PLLM[PLLM] value
 */
struct davinci_pll_clk {
	struct clk_hw hw;
	void __iomem *base;
	u32 pllm_min;
	u32 pllm_max;
	u32 pllm_mask;
};

#define to_davinci_pll_clk(_hw) \
	container_of((_hw), struct davinci_pll_clk, hw)

static unsigned long davinci_pll_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct davinci_pll_clk *pll = to_davinci_pll_clk(hw);
	unsigned long rate = parent_rate;
	u32 mult;

	mult = readl(pll->base + PLLM) & pll->pllm_mask;
	rate *= mult + 1;

	return rate;
}

static int davinci_pll_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	struct davinci_pll_clk *pll = to_davinci_pll_clk(hw);
	struct clk_hw *parent = req->best_parent_hw;
	unsigned long parent_rate = req->best_parent_rate;
	unsigned long rate = req->rate;
	unsigned long best_rate, r;
	u32 mult;

	/* there is a limited range of valid outputs (see datasheet) */
	if (rate < req->min_rate)
		return -EINVAL;

	rate = min(rate, req->max_rate);
	mult = rate / parent_rate;
	best_rate = parent_rate * mult;

	/* easy case when there is no PREDIV */
	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		if (best_rate < req->min_rate)
			return -EINVAL;

		if (mult < pll->pllm_min || mult > pll->pllm_max)
			return -EINVAL;

		req->rate = best_rate;

		return 0;
	}

	/* see if the PREDIV clock can help us */
	best_rate = 0;

	for (mult = pll->pllm_min; mult <= pll->pllm_max; mult++) {
		parent_rate = clk_hw_round_rate(parent, rate / mult);
		r = parent_rate * mult;
		if (r < req->min_rate)
			continue;
		if (r > rate || r > req->max_rate)
			break;
		if (r > best_rate) {
			best_rate = r;
			req->rate = best_rate;
			req->best_parent_rate = parent_rate;
			if (best_rate == rate)
				break;
		}
	}

	return 0;
}

static int davinci_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct davinci_pll_clk *pll = to_davinci_pll_clk(hw);
	u32 mult;

	mult = rate / parent_rate;
	writel(mult - 1, pll->base + PLLM);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void davinci_pll_debug_init(struct clk_hw *hw, struct dentry *dentry);
#else
#define davinci_pll_debug_init NULL
#endif

static const struct clk_ops davinci_pll_ops = {
	.recalc_rate	= davinci_pll_recalc_rate,
	.determine_rate	= davinci_pll_determine_rate,
	.set_rate	= davinci_pll_set_rate,
	.debug_init	= davinci_pll_debug_init,
};

/* PLLM works differently on DM365 */
static unsigned long dm365_pll_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct davinci_pll_clk *pll = to_davinci_pll_clk(hw);
	unsigned long rate = parent_rate;
	u32 mult;

	mult = readl(pll->base + PLLM) & pll->pllm_mask;
	rate *= mult * 2;

	return rate;
}

static const struct clk_ops dm365_pll_ops = {
	.recalc_rate	= dm365_pll_recalc_rate,
	.debug_init	= davinci_pll_debug_init,
};

/**
 * davinci_pll_div_register - common *DIV clock implementation
 * @dev: The PLL platform device or NULL
 * @name: the clock name
 * @parent_name: the parent clock name
 * @reg: the *DIV register
 * @fixed: if true, the divider is a fixed value
 * @flags: bitmap of CLK_* flags from clock-provider.h
 */
static struct clk *davinci_pll_div_register(struct device *dev,
					    const char *name,
					    const char *parent_name,
					    void __iomem *reg,
					    bool fixed, u32 flags)
{
	const char * const *parent_names = parent_name ? &parent_name : NULL;
	int num_parents = parent_name ? 1 : 0;
	const struct clk_ops *divider_ops = &clk_divider_ops;
	struct clk_gate *gate;
	struct clk_divider *divider;
	struct clk *clk;
	int ret;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->reg = reg;
	gate->bit_idx = DIV_ENABLE_SHIFT;

	divider = kzalloc(sizeof(*divider), GFP_KERNEL);
	if (!divider) {
		ret = -ENOMEM;
		goto err_free_gate;
	}

	divider->reg = reg;
	divider->shift = DIV_RATIO_SHIFT;
	divider->width = DIV_RATIO_WIDTH;

	if (fixed) {
		divider->flags |= CLK_DIVIDER_READ_ONLY;
		divider_ops = &clk_divider_ro_ops;
	}

	clk = clk_register_composite(dev, name, parent_names, num_parents,
				     NULL, NULL, &divider->hw, divider_ops,
				     &gate->hw, &clk_gate_ops, flags);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err_free_divider;
	}

	return clk;

err_free_divider:
	kfree(divider);
err_free_gate:
	kfree(gate);

	return ERR_PTR(ret);
}

struct davinci_pllen_clk {
	struct clk_hw hw;
	void __iomem *base;
};

#define to_davinci_pllen_clk(_hw) \
	container_of((_hw), struct davinci_pllen_clk, hw)

static const struct clk_ops davinci_pllen_ops = {
	/* this clocks just uses the clock notification feature */
};

/*
 * The PLL has to be switched into bypass mode while we are chaning the rate,
 * so we do that on the PLLEN clock since it is the end of the line. This will
 * switch to bypass before any of the parent clocks (PREDIV, PLL, POSTDIV) are
 * changed and will switch back to the PLL after the changes have been made.
 */
static int davinci_pllen_rate_change(struct notifier_block *nb,
				     unsigned long flags, void *data)
{
	struct clk_notifier_data *cnd = data;
	struct clk_hw *hw = __clk_get_hw(cnd->clk);
	struct davinci_pllen_clk *pll = to_davinci_pllen_clk(hw);
	u32 ctrl;

	ctrl = readl(pll->base + PLLCTL);

	if (flags == PRE_RATE_CHANGE) {
		/* Switch the PLL to bypass mode */
		ctrl &= ~(PLLCTL_PLLENSRC | PLLCTL_PLLEN);
		writel(ctrl, pll->base + PLLCTL);

		udelay(PLL_BYPASS_TIME);

		/* Reset and enable PLL */
		ctrl &= ~(PLLCTL_PLLRST | PLLCTL_PLLDIS);
		writel(ctrl, pll->base + PLLCTL);
	} else {
		udelay(PLL_RESET_TIME);

		/* Bring PLL out of reset */
		ctrl |= PLLCTL_PLLRST;
		writel(ctrl, pll->base + PLLCTL);

		udelay(PLL_LOCK_TIME);

		/* Remove PLL from bypass mode */
		ctrl |= PLLCTL_PLLEN;
		writel(ctrl, pll->base + PLLCTL);
	}

	return NOTIFY_OK;
}

static struct notifier_block davinci_pllen_notifier = {
	.notifier_call = davinci_pllen_rate_change,
};

/**
 * davinci_pll_clk_register - Register a PLL clock
 * @dev: The PLL platform device or NULL
 * @info: The device-specific clock info
 * @parent_name: The parent clock name
 * @base: The PLL's memory region
 * @cfgchip: CFGCHIP syscon regmap for info->unlock_reg or NULL
 *
 * This creates a series of clocks that represent the PLL.
 *
 *     OSCIN > [PREDIV >] PLL > [POSTDIV >] PLLEN
 *
 * - OSCIN is the parent clock (on secondary PLL, may come from primary PLL)
 * - PREDIV and POSTDIV are optional (depends on the PLL controller)
 * - PLL is the PLL output (aka PLLOUT)
 * - PLLEN is the bypass multiplexer
 *
 * Returns: The PLLOUT clock or a negative error code.
 */
struct clk *davinci_pll_clk_register(struct device *dev,
				     const struct davinci_pll_clk_info *info,
				     const char *parent_name,
				     void __iomem *base,
				     struct regmap *cfgchip)
{
	char prediv_name[MAX_NAME_SIZE];
	char pllout_name[MAX_NAME_SIZE];
	char postdiv_name[MAX_NAME_SIZE];
	char pllen_name[MAX_NAME_SIZE];
	struct clk_init_data init = {};
	struct davinci_pll_clk *pllout;
	struct davinci_pllen_clk *pllen;
	struct clk *oscin_clk = NULL;
	struct clk *prediv_clk = NULL;
	struct clk *pllout_clk;
	struct clk *postdiv_clk = NULL;
	struct clk *pllen_clk;
	int ret;

	if (info->flags & PLL_HAS_CLKMODE) {
		/*
		 * If a PLL has PLLCTL[CLKMODE], then it is the primary PLL.
		 * We register a clock named "oscin" that serves as the internal
		 * "input clock" domain shared by both PLLs (if there are 2)
		 * and will be the parent clock to the AUXCLK, SYSCLKBP and
		 * OBSCLK domains. NB: The various TRMs use "OSCIN" to mean
		 * a number of different things. In this driver we use it to
		 * mean the signal after the PLLCTL[CLKMODE] switch.
		 */
		oscin_clk = clk_register_fixed_factor(dev, OSCIN_CLK_NAME,
						      parent_name, 0, 1, 1);
		if (IS_ERR(oscin_clk))
			return oscin_clk;

		parent_name = OSCIN_CLK_NAME;
	}

	if (info->flags & PLL_HAS_PREDIV) {
		bool fixed = info->flags & PLL_PREDIV_FIXED_DIV;
		u32 flags = 0;

		snprintf(prediv_name, MAX_NAME_SIZE, "%s_prediv", info->name);

		if (info->flags & PLL_PREDIV_ALWAYS_ENABLED)
			flags |= CLK_IS_CRITICAL;

		/* Some? DM355 chips don't correctly report the PREDIV value */
		if (info->flags & PLL_PREDIV_FIXED8)
			prediv_clk = clk_register_fixed_factor(dev, prediv_name,
							parent_name, flags, 1, 8);
		else
			prediv_clk = davinci_pll_div_register(dev, prediv_name,
				parent_name, base + PREDIV, fixed, flags);
		if (IS_ERR(prediv_clk)) {
			ret = PTR_ERR(prediv_clk);
			goto err_unregister_oscin;
		}

		parent_name = prediv_name;
	}

	/* Unlock writing to PLL registers */
	if (info->unlock_reg) {
		if (IS_ERR_OR_NULL(cfgchip))
			dev_warn(dev, "Failed to get CFGCHIP (%ld)\n",
				 PTR_ERR(cfgchip));
		else
			regmap_write_bits(cfgchip, info->unlock_reg,
					  info->unlock_mask, 0);
	}

	pllout = kzalloc(sizeof(*pllout), GFP_KERNEL);
	if (!pllout) {
		ret = -ENOMEM;
		goto err_unregister_prediv;
	}

	snprintf(pllout_name, MAX_NAME_SIZE, "%s_pllout", info->name);

	init.name = pllout_name;
	if (info->flags & PLL_PLLM_2X)
		init.ops = &dm365_pll_ops;
	else
		init.ops = &davinci_pll_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;

	if (info->flags & PLL_HAS_PREDIV)
		init.flags |= CLK_SET_RATE_PARENT;

	pllout->hw.init = &init;
	pllout->base = base;
	pllout->pllm_mask = info->pllm_mask;
	pllout->pllm_min = info->pllm_min;
	pllout->pllm_max = info->pllm_max;

	pllout_clk = clk_register(dev, &pllout->hw);
	if (IS_ERR(pllout_clk)) {
		ret = PTR_ERR(pllout_clk);
		goto err_free_pllout;
	}

	clk_hw_set_rate_range(&pllout->hw, info->pllout_min_rate,
			      info->pllout_max_rate);

	parent_name = pllout_name;

	if (info->flags & PLL_HAS_POSTDIV) {
		bool fixed = info->flags & PLL_POSTDIV_FIXED_DIV;
		u32 flags = CLK_SET_RATE_PARENT;

		snprintf(postdiv_name, MAX_NAME_SIZE, "%s_postdiv", info->name);

		if (info->flags & PLL_POSTDIV_ALWAYS_ENABLED)
			flags |= CLK_IS_CRITICAL;

		postdiv_clk = davinci_pll_div_register(dev, postdiv_name,
				parent_name, base + POSTDIV, fixed, flags);
		if (IS_ERR(postdiv_clk)) {
			ret = PTR_ERR(postdiv_clk);
			goto err_unregister_pllout;
		}

		parent_name = postdiv_name;
	}

	pllen = kzalloc(sizeof(*pllout), GFP_KERNEL);
	if (!pllen) {
		ret = -ENOMEM;
		goto err_unregister_postdiv;
	}

	snprintf(pllen_name, MAX_NAME_SIZE, "%s_pllen", info->name);

	init.name = pllen_name;
	init.ops = &davinci_pllen_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_PARENT;

	pllen->hw.init = &init;
	pllen->base = base;

	pllen_clk = clk_register(dev, &pllen->hw);
	if (IS_ERR(pllen_clk)) {
		ret = PTR_ERR(pllen_clk);
		goto err_free_pllen;
	}

	clk_notifier_register(pllen_clk, &davinci_pllen_notifier);

	return pllout_clk;

err_free_pllen:
	kfree(pllen);
err_unregister_postdiv:
	clk_unregister(postdiv_clk);
err_unregister_pllout:
	clk_unregister(pllout_clk);
err_free_pllout:
	kfree(pllout);
err_unregister_prediv:
	clk_unregister(prediv_clk);
err_unregister_oscin:
	clk_unregister(oscin_clk);

	return ERR_PTR(ret);
}

/**
 * davinci_pll_auxclk_register - Register bypass clock (AUXCLK)
 * @dev: The PLL platform device or NULL
 * @name: The clock name
 * @base: The PLL memory region
 */
struct clk *davinci_pll_auxclk_register(struct device *dev,
					const char *name,
					void __iomem *base)
{
	return clk_register_gate(dev, name, OSCIN_CLK_NAME, 0, base + CKEN,
				 CKEN_AUXEN_SHIFT, 0, NULL);
}

/**
 * davinci_pll_sysclkbp_clk_register - Register bypass divider clock (SYSCLKBP)
 * @dev: The PLL platform device or NULL
 * @name: The clock name
 * @base: The PLL memory region
 */
struct clk *davinci_pll_sysclkbp_clk_register(struct device *dev,
					      const char *name,
					      void __iomem *base)
{
	return clk_register_divider(dev, name, OSCIN_CLK_NAME, 0, base + BPDIV,
				    DIV_RATIO_SHIFT, DIV_RATIO_WIDTH,
				    CLK_DIVIDER_READ_ONLY, NULL);
}

/**
 * davinci_pll_obsclk_register - Register oscillator divider clock (OBSCLK)
 * @dev: The PLL platform device or NULL
 * @info: The clock info
 * @base: The PLL memory region
 */
struct clk *
davinci_pll_obsclk_register(struct device *dev,
			    const struct davinci_pll_obsclk_info *info,
			    void __iomem *base)
{
	struct clk_mux *mux;
	struct clk_gate *gate;
	struct clk_divider *divider;
	struct clk *clk;
	u32 oscdiv;
	int ret;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	mux->reg = base + OCSEL;
	mux->table = info->table;
	mux->mask = info->ocsrc_mask;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate) {
		ret = -ENOMEM;
		goto err_free_mux;
	}

	gate->reg = base + CKEN;
	gate->bit_idx = CKEN_OBSCLK_SHIFT;

	divider = kzalloc(sizeof(*divider), GFP_KERNEL);
	if (!divider) {
		ret = -ENOMEM;
		goto err_free_gate;
	}

	divider->reg = base + OSCDIV;
	divider->shift = DIV_RATIO_SHIFT;
	divider->width = DIV_RATIO_WIDTH;

	/* make sure divider is enabled just in case bootloader disabled it */
	oscdiv = readl(base + OSCDIV);
	oscdiv |= BIT(DIV_ENABLE_SHIFT);
	writel(oscdiv, base + OSCDIV);

	clk = clk_register_composite(dev, info->name, info->parent_names,
				     info->num_parents,
				     &mux->hw, &clk_mux_ops,
				     &divider->hw, &clk_divider_ops,
				     &gate->hw, &clk_gate_ops, 0);

	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err_free_divider;
	}

	return clk;

err_free_divider:
	kfree(divider);
err_free_gate:
	kfree(gate);
err_free_mux:
	kfree(mux);

	return ERR_PTR(ret);
}

/* The PLL SYSCLKn clocks have a mechanism for synchronizing rate changes. */
static int davinci_pll_sysclk_rate_change(struct notifier_block *nb,
					  unsigned long flags, void *data)
{
	struct clk_notifier_data *cnd = data;
	struct clk_hw *hw = __clk_get_hw(clk_get_parent(cnd->clk));
	struct davinci_pllen_clk *pll = to_davinci_pllen_clk(hw);
	u32 pllcmd, pllstat;

	switch (flags) {
	case POST_RATE_CHANGE:
		/* apply the changes */
		pllcmd = readl(pll->base + PLLCMD);
		pllcmd |= PLLCMD_GOSET;
		writel(pllcmd, pll->base + PLLCMD);
		/* fallthrough */
	case PRE_RATE_CHANGE:
		/* Wait until for outstanding changes to take effect */
		do {
			pllstat = readl(pll->base + PLLSTAT);
		} while (pllstat & PLLSTAT_GOSTAT);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block davinci_pll_sysclk_notifier = {
	.notifier_call = davinci_pll_sysclk_rate_change,
};

/**
 * davinci_pll_sysclk_register - Register divider clocks (SYSCLKn)
 * @dev: The PLL platform device or NULL
 * @info: The clock info
 * @base: The PLL memory region
 */
struct clk *
davinci_pll_sysclk_register(struct device *dev,
			    const struct davinci_pll_sysclk_info *info,
			    void __iomem *base)
{
	const struct clk_ops *divider_ops = &clk_divider_ops;
	struct clk_gate *gate;
	struct clk_divider *divider;
	struct clk *clk;
	u32 reg;
	u32 flags = 0;
	int ret;

	/* PLLDIVn registers are not entirely consecutive */
	if (info->id < 4)
		reg = PLLDIV1 + 4 * (info->id - 1);
	else
		reg = PLLDIV4 + 4 * (info->id - 4);

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->reg = base + reg;
	gate->bit_idx = DIV_ENABLE_SHIFT;

	divider = kzalloc(sizeof(*divider), GFP_KERNEL);
	if (!divider) {
		ret = -ENOMEM;
		goto err_free_gate;
	}

	divider->reg = base + reg;
	divider->shift = DIV_RATIO_SHIFT;
	divider->width = info->ratio_width;
	divider->flags = 0;

	if (info->flags & SYSCLK_FIXED_DIV) {
		divider->flags |= CLK_DIVIDER_READ_ONLY;
		divider_ops = &clk_divider_ro_ops;
	}

	/* Only the ARM clock can change the parent PLL rate */
	if (info->flags & SYSCLK_ARM_RATE)
		flags |= CLK_SET_RATE_PARENT;

	if (info->flags & SYSCLK_ALWAYS_ENABLED)
		flags |= CLK_IS_CRITICAL;

	clk = clk_register_composite(dev, info->name, &info->parent_name, 1,
				     NULL, NULL, &divider->hw, divider_ops,
				     &gate->hw, &clk_gate_ops, flags);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err_free_divider;
	}

	clk_notifier_register(clk, &davinci_pll_sysclk_notifier);

	return clk;

err_free_divider:
	kfree(divider);
err_free_gate:
	kfree(gate);

	return ERR_PTR(ret);
}

int of_davinci_pll_init(struct device *dev, struct device_node *node,
			const struct davinci_pll_clk_info *info,
			const struct davinci_pll_obsclk_info *obsclk_info,
			const struct davinci_pll_sysclk_info **div_info,
			u8 max_sysclk_id,
			void __iomem *base,
			struct regmap *cfgchip)
{
	struct device_node *child;
	const char *parent_name;
	struct clk *clk;

	if (info->flags & PLL_HAS_CLKMODE)
		parent_name = of_clk_get_parent_name(node, 0);
	else
		parent_name = OSCIN_CLK_NAME;

	clk = davinci_pll_clk_register(dev, info, parent_name, base, cfgchip);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to register %s\n", info->name);
		return PTR_ERR(clk);
	}

	child = of_get_child_by_name(node, "pllout");
	if (of_device_is_available(child))
		of_clk_add_provider(child, of_clk_src_simple_get, clk);
	of_node_put(child);

	child = of_get_child_by_name(node, "sysclk");
	if (of_device_is_available(child)) {
		struct clk_onecell_data *clk_data;
		struct clk **clks;
		int n_clks =  max_sysclk_id + 1;
		int i;

		clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
		if (!clk_data)
			return -ENOMEM;

		clks = kmalloc_array(n_clks, sizeof(*clks), GFP_KERNEL);
		if (!clks) {
			kfree(clk_data);
			return -ENOMEM;
		}

		clk_data->clks = clks;
		clk_data->clk_num = n_clks;

		for (i = 0; i < n_clks; i++)
			clks[i] = ERR_PTR(-ENOENT);

		for (; *div_info; div_info++) {
			clk = davinci_pll_sysclk_register(dev, *div_info, base);
			if (IS_ERR(clk))
				dev_warn(dev, "failed to register %s (%ld)\n",
					 (*div_info)->name, PTR_ERR(clk));
			else
				clks[(*div_info)->id] = clk;
		}
		of_clk_add_provider(child, of_clk_src_onecell_get, clk_data);
	}
	of_node_put(child);

	child = of_get_child_by_name(node, "auxclk");
	if (of_device_is_available(child)) {
		char child_name[MAX_NAME_SIZE];

		snprintf(child_name, MAX_NAME_SIZE, "%s_auxclk", info->name);

		clk = davinci_pll_auxclk_register(dev, child_name, base);
		if (IS_ERR(clk))
			dev_warn(dev, "failed to register %s (%ld)\n",
				 child_name, PTR_ERR(clk));
		else
			of_clk_add_provider(child, of_clk_src_simple_get, clk);
	}
	of_node_put(child);

	child = of_get_child_by_name(node, "obsclk");
	if (of_device_is_available(child)) {
		if (obsclk_info)
			clk = davinci_pll_obsclk_register(dev, obsclk_info, base);
		else
			clk = ERR_PTR(-EINVAL);

		if (IS_ERR(clk))
			dev_warn(dev, "failed to register obsclk (%ld)\n",
				 PTR_ERR(clk));
		else
			of_clk_add_provider(child, of_clk_src_simple_get, clk);
	}
	of_node_put(child);

	return 0;
}

static struct davinci_pll_platform_data *davinci_pll_get_pdata(struct device *dev)
{
	struct davinci_pll_platform_data *pdata = dev_get_platdata(dev);

	/*
	 * Platform data is optional, so allocate a new struct if one was not
	 * provided. For device tree, this will always be the case.
	 */
	if (!pdata)
		pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	/* for device tree, we need to fill in the struct */
	if (dev->of_node)
		pdata->cfgchip =
			syscon_regmap_lookup_by_compatible("ti,da830-cfgchip");

	return pdata;
}

/* needed in early boot for clocksource/clockevent */
#ifdef CONFIG_ARCH_DAVINCI_DA850
CLK_OF_DECLARE(da850_pll0, "ti,da850-pll0", of_da850_pll0_init);
#endif

static const struct of_device_id davinci_pll_of_match[] = {
#ifdef CONFIG_ARCH_DAVINCI_DA850
	{ .compatible = "ti,da850-pll1", .data = of_da850_pll1_init },
#endif
	{ }
};

static const struct platform_device_id davinci_pll_id_table[] = {
#ifdef CONFIG_ARCH_DAVINCI_DA830
	{ .name = "da830-pll",   .driver_data = (kernel_ulong_t)da830_pll_init   },
#endif
#ifdef CONFIG_ARCH_DAVINCI_DA850
	{ .name = "da850-pll0",  .driver_data = (kernel_ulong_t)da850_pll0_init  },
	{ .name = "da850-pll1",  .driver_data = (kernel_ulong_t)da850_pll1_init  },
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM355
	{ .name = "dm355-pll1",  .driver_data = (kernel_ulong_t)dm355_pll1_init  },
	{ .name = "dm355-pll2",  .driver_data = (kernel_ulong_t)dm355_pll2_init  },
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM365
	{ .name = "dm365-pll1",  .driver_data = (kernel_ulong_t)dm365_pll1_init  },
	{ .name = "dm365-pll2",  .driver_data = (kernel_ulong_t)dm365_pll2_init  },
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM644x
	{ .name = "dm644x-pll1", .driver_data = (kernel_ulong_t)dm644x_pll1_init },
	{ .name = "dm644x-pll2", .driver_data = (kernel_ulong_t)dm644x_pll2_init },
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM646x
	{ .name = "dm646x-pll1", .driver_data = (kernel_ulong_t)dm646x_pll1_init },
	{ .name = "dm646x-pll2", .driver_data = (kernel_ulong_t)dm646x_pll2_init },
#endif
	{ }
};

typedef int (*davinci_pll_init)(struct device *dev, void __iomem *base,
				struct regmap *cfgchip);

static int davinci_pll_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct davinci_pll_platform_data *pdata;
	const struct of_device_id *of_id;
	davinci_pll_init pll_init = NULL;
	struct resource *res;
	void __iomem *base;

	of_id = of_match_device(davinci_pll_of_match, dev);
	if (of_id)
		pll_init = of_id->data;
	else if (pdev->id_entry)
		pll_init = (void *)pdev->id_entry->driver_data;

	if (!pll_init) {
		dev_err(dev, "unable to find driver data\n");
		return -EINVAL;
	}

	pdata = davinci_pll_get_pdata(dev);
	if (!pdata) {
		dev_err(dev, "missing platform data\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	return pll_init(dev, base, pdata->cfgchip);
}

static struct platform_driver davinci_pll_driver = {
	.probe		= davinci_pll_probe,
	.driver		= {
		.name		= "davinci-pll-clk",
		.of_match_table	= davinci_pll_of_match,
	},
	.id_table	= davinci_pll_id_table,
};

static int __init davinci_pll_driver_init(void)
{
	return platform_driver_register(&davinci_pll_driver);
}

/* has to be postcore_initcall because PSC devices depend on PLL parent clocks */
postcore_initcall(davinci_pll_driver_init);

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

#define DEBUG_REG(n)	\
{			\
	.name	= #n,	\
	.offset	= n,	\
}

static const struct debugfs_reg32 davinci_pll_regs[] = {
	DEBUG_REG(REVID),
	DEBUG_REG(PLLCTL),
	DEBUG_REG(OCSEL),
	DEBUG_REG(PLLSECCTL),
	DEBUG_REG(PLLM),
	DEBUG_REG(PREDIV),
	DEBUG_REG(PLLDIV1),
	DEBUG_REG(PLLDIV2),
	DEBUG_REG(PLLDIV3),
	DEBUG_REG(OSCDIV),
	DEBUG_REG(POSTDIV),
	DEBUG_REG(BPDIV),
	DEBUG_REG(PLLCMD),
	DEBUG_REG(PLLSTAT),
	DEBUG_REG(ALNCTL),
	DEBUG_REG(DCHANGE),
	DEBUG_REG(CKEN),
	DEBUG_REG(CKSTAT),
	DEBUG_REG(SYSTAT),
	DEBUG_REG(PLLDIV4),
	DEBUG_REG(PLLDIV5),
	DEBUG_REG(PLLDIV6),
	DEBUG_REG(PLLDIV7),
	DEBUG_REG(PLLDIV8),
	DEBUG_REG(PLLDIV9),
};

static void davinci_pll_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	struct davinci_pll_clk *pll = to_davinci_pll_clk(hw);
	struct debugfs_regset32 *regset;

	regset = kzalloc(sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	regset->regs = davinci_pll_regs;
	regset->nregs = ARRAY_SIZE(davinci_pll_regs);
	regset->base = pll->base;

	debugfs_create_regset32("registers", 0400, dentry, regset);
}
#endif
