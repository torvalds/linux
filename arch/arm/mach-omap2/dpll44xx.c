/*
 * OMAP4-specific DPLL control functions
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Rajendra Nayak
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/bitops.h>

#include "soc.h"
#include "clock.h"
#include "clock44xx.h"
#include "cm-regbits-44xx.h"

/* Supported only on OMAP4 */
int omap4_dpllmx_gatectrl_read(struct clk_hw_omap *clk)
{
	u32 v;
	u32 mask;

	if (!clk || !clk->clksel_reg || !cpu_is_omap44xx())
		return -EINVAL;

	mask = clk->flags & CLOCK_CLKOUTX2 ?
			OMAP4430_DPLL_CLKOUTX2_GATE_CTRL_MASK :
			OMAP4430_DPLL_CLKOUT_GATE_CTRL_MASK;

	v = __raw_readl(clk->clksel_reg);
	v &= mask;
	v >>= __ffs(mask);

	return v;
}

void omap4_dpllmx_allow_gatectrl(struct clk_hw_omap *clk)
{
	u32 v;
	u32 mask;

	if (!clk || !clk->clksel_reg || !cpu_is_omap44xx())
		return;

	mask = clk->flags & CLOCK_CLKOUTX2 ?
			OMAP4430_DPLL_CLKOUTX2_GATE_CTRL_MASK :
			OMAP4430_DPLL_CLKOUT_GATE_CTRL_MASK;

	v = __raw_readl(clk->clksel_reg);
	/* Clear the bit to allow gatectrl */
	v &= ~mask;
	__raw_writel(v, clk->clksel_reg);
}

void omap4_dpllmx_deny_gatectrl(struct clk_hw_omap *clk)
{
	u32 v;
	u32 mask;

	if (!clk || !clk->clksel_reg || !cpu_is_omap44xx())
		return;

	mask = clk->flags & CLOCK_CLKOUTX2 ?
			OMAP4430_DPLL_CLKOUTX2_GATE_CTRL_MASK :
			OMAP4430_DPLL_CLKOUT_GATE_CTRL_MASK;

	v = __raw_readl(clk->clksel_reg);
	/* Set the bit to deny gatectrl */
	v |= mask;
	__raw_writel(v, clk->clksel_reg);
}

const struct clk_hw_omap_ops clkhwops_omap4_dpllmx = {
	.allow_idle	= omap4_dpllmx_allow_gatectrl,
	.deny_idle      = omap4_dpllmx_deny_gatectrl,
};

/**
 * omap4_dpll_regm4xen_recalc - compute DPLL rate, considering REGM4XEN bit
 * @clk: struct clk * of the DPLL to compute the rate for
 *
 * Compute the output rate for the OMAP4 DPLL represented by @clk.
 * Takes the REGM4XEN bit into consideration, which is needed for the
 * OMAP4 ABE DPLL.  Returns the DPLL's output rate (before M-dividers)
 * upon success, or 0 upon error.
 */
unsigned long omap4_dpll_regm4xen_recalc(struct clk_hw *hw,
			unsigned long parent_rate)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 v;
	unsigned long rate;
	struct dpll_data *dd;

	if (!clk || !clk->dpll_data)
		return 0;

	dd = clk->dpll_data;

	rate = omap2_get_dpll_rate(clk);

	/* regm4xen adds a multiplier of 4 to DPLL calculations */
	v = __raw_readl(dd->control_reg);
	if (v & OMAP4430_DPLL_REGM4XEN_MASK)
		rate *= OMAP4430_REGM4XEN_MULT;

	return rate;
}

/**
 * omap4_dpll_regm4xen_round_rate - round DPLL rate, considering REGM4XEN bit
 * @clk: struct clk * of the DPLL to round a rate for
 * @target_rate: the desired rate of the DPLL
 *
 * Compute the rate that would be programmed into the DPLL hardware
 * for @clk if set_rate() were to be provided with the rate
 * @target_rate.  Takes the REGM4XEN bit into consideration, which is
 * needed for the OMAP4 ABE DPLL.  Returns the rounded rate (before
 * M-dividers) upon success, -EINVAL if @clk is null or not a DPLL, or
 * ~0 if an error occurred in omap2_dpll_round_rate().
 */
long omap4_dpll_regm4xen_round_rate(struct clk_hw *hw,
				    unsigned long target_rate,
				    unsigned long *parent_rate)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 v;
	struct dpll_data *dd;
	long r;

	if (!clk || !clk->dpll_data)
		return -EINVAL;

	dd = clk->dpll_data;

	/* regm4xen adds a multiplier of 4 to DPLL calculations */
	v = __raw_readl(dd->control_reg) & OMAP4430_DPLL_REGM4XEN_MASK;

	if (v)
		target_rate = target_rate / OMAP4430_REGM4XEN_MULT;

	r = omap2_dpll_round_rate(hw, target_rate, NULL);
	if (r == ~0)
		return r;

	if (v)
		clk->dpll_data->last_rounded_rate *= OMAP4430_REGM4XEN_MULT;

	return clk->dpll_data->last_rounded_rate;
}
