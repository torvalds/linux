/*
 * OMAP36xx-specific clkops
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 *
 * Mike Turquette
 * Vijaykumar GN
 * Paul Walmsley
 *
 * Parts of this code are based on code written by
 * Richard Woodruff, Tony Lindgren, Tuukka Tikkanen, Karthik Dasu,
 * Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>

#include "clock.h"
#include "clock36xx.h"


/**
 * omap36xx_pwrdn_clk_enable_with_hsdiv_restore - enable clocks suffering
 *         from HSDivider PWRDN problem Implements Errata ID: i556.
 * @clk: DPLL output struct clk
 *
 * 3630 only: dpll3_m3_ck, dpll4_m2_ck, dpll4_m3_ck, dpll4_m4_ck,
 * dpll4_m5_ck & dpll4_m6_ck dividers gets loaded with reset
 * valueafter their respective PWRDN bits are set.  Any dummy write
 * (Any other value different from the Read value) to the
 * corresponding CM_CLKSEL register will refresh the dividers.
 */
#ifdef CONFIG_COMMON_CLK
int omap36xx_pwrdn_clk_enable_with_hsdiv_restore(struct clk_hw *clk)
{
	struct clk_hw_omap *parent;
	struct clk_hw *parent_hw;
#else
static int omap36xx_pwrdn_clk_enable_with_hsdiv_restore(struct clk *clk)
{
	struct clk *parent;
#endif
	u32 dummy_v, orig_v, clksel_shift;
	int ret;

	/* Clear PWRDN bit of HSDIVIDER */
	ret = omap2_dflt_clk_enable(clk);

#ifdef CONFIG_COMMON_CLK
	parent_hw = __clk_get_hw(__clk_get_parent(clk->clk));
	parent = to_clk_hw_omap(parent_hw);
#else
	parent = clk->parent;
#endif

	/* Restore the dividers */
	if (!ret) {
		clksel_shift = __ffs(parent->clksel_mask);
		orig_v = __raw_readl(parent->clksel_reg);
		dummy_v = orig_v;

		/* Write any other value different from the Read value */
		dummy_v ^= (1 << clksel_shift);
		__raw_writel(dummy_v, parent->clksel_reg);

		/* Write the original divider */
		__raw_writel(orig_v, parent->clksel_reg);
	}

	return ret;
}

#ifndef CONFIG_COMMON_CLK
const struct clkops clkops_omap36xx_pwrdn_with_hsdiv_wait_restore = {
	.enable		= omap36xx_pwrdn_clk_enable_with_hsdiv_restore,
	.disable	= omap2_dflt_clk_disable,
	.find_companion	= omap2_clk_dflt_find_companion,
	.find_idlest	= omap2_clk_dflt_find_idlest,
};
#endif
