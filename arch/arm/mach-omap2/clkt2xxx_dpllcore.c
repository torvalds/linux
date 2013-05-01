/*
 * DPLL + CORE_CLK composite clock functions
 *
 * Copyright (C) 2005-2008 Texas Instruments, Inc.
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Contacts:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
 *
 * Based on earlier work by Tuukka Tikkanen, Tony Lindgren,
 * Gordon McNutt and RidgeRun, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX The DPLL and CORE clocks should be split into two separate clock
 * types.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/io.h>

#include "clock.h"
#include "clock2xxx.h"
#include "opp2xxx.h"
#include "cm2xxx.h"
#include "cm-regbits-24xx.h"
#include "sdrc.h"
#include "sram.h"

/* #define DOWN_VARIABLE_DPLL 1 */		/* Experimental */

/*
 * dpll_core_ck: pointer to the combined dpll_ck + core_ck on OMAP2xxx
 * (currently defined as "dpll_ck" in the OMAP2xxx clock tree).  Set
 * during dpll_ck init and used later by omap2xxx_clk_get_core_rate().
 */
static struct clk_hw_omap *dpll_core_ck;

/**
 * omap2xxx_clk_get_core_rate - return the CORE_CLK rate
 *
 * Returns the CORE_CLK rate.  CORE_CLK can have one of three rate
 * sources on OMAP2xxx: the DPLL CLKOUT rate, DPLL CLKOUTX2, or 32KHz
 * (the latter is unusual).  This currently should be called with
 * struct clk *dpll_ck, which is a composite clock of dpll_ck and
 * core_ck.
 */
unsigned long omap2xxx_clk_get_core_rate(void)
{
	long long core_clk;
	u32 v;

	WARN_ON(!dpll_core_ck);

	core_clk = omap2_get_dpll_rate(dpll_core_ck);

	v = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	v &= OMAP24XX_CORE_CLK_SRC_MASK;

	if (v == CORE_CLK_SRC_32K)
		core_clk = 32768;
	else
		core_clk *= v;

	return core_clk;
}

/*
 * Uses the current prcm set to tell if a rate is valid.
 * You can go slower, but not faster within a given rate set.
 */
static long omap2_dpllcore_round_rate(unsigned long target_rate)
{
	u32 high, low, core_clk_src;

	core_clk_src = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	core_clk_src &= OMAP24XX_CORE_CLK_SRC_MASK;

	if (core_clk_src == CORE_CLK_SRC_DPLL) {	/* DPLL clockout */
		high = curr_prcm_set->dpll_speed * 2;
		low = curr_prcm_set->dpll_speed;
	} else {				/* DPLL clockout x 2 */
		high = curr_prcm_set->dpll_speed;
		low = curr_prcm_set->dpll_speed / 2;
	}

#ifdef DOWN_VARIABLE_DPLL
	if (target_rate > high)
		return high;
	else
		return target_rate;
#else
	if (target_rate > low)
		return high;
	else
		return low;
#endif

}

unsigned long omap2_dpllcore_recalc(struct clk_hw *hw,
				    unsigned long parent_rate)
{
	return omap2xxx_clk_get_core_rate();
}

int omap2_reprogram_dpllcore(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 cur_rate, low, mult, div, valid_rate, done_rate;
	u32 bypass = 0;
	struct prcm_config tmpset;
	const struct dpll_data *dd;

	cur_rate = omap2xxx_clk_get_core_rate();
	mult = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	mult &= OMAP24XX_CORE_CLK_SRC_MASK;

	if ((rate == (cur_rate / 2)) && (mult == 2)) {
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL, 1);
	} else if ((rate == (cur_rate * 2)) && (mult == 1)) {
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL_X2, 1);
	} else if (rate != cur_rate) {
		valid_rate = omap2_dpllcore_round_rate(rate);
		if (valid_rate != rate)
			return -EINVAL;

		if (mult == 1)
			low = curr_prcm_set->dpll_speed;
		else
			low = curr_prcm_set->dpll_speed / 2;

		dd = clk->dpll_data;
		if (!dd)
			return -EINVAL;

		tmpset.cm_clksel1_pll = __raw_readl(dd->mult_div1_reg);
		tmpset.cm_clksel1_pll &= ~(dd->mult_mask |
					   dd->div1_mask);
		div = ((curr_prcm_set->xtal_speed / 1000000) - 1);
		tmpset.cm_clksel2_pll = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
		tmpset.cm_clksel2_pll &= ~OMAP24XX_CORE_CLK_SRC_MASK;
		if (rate > low) {
			tmpset.cm_clksel2_pll |= CORE_CLK_SRC_DPLL_X2;
			mult = ((rate / 2) / 1000000);
			done_rate = CORE_CLK_SRC_DPLL_X2;
		} else {
			tmpset.cm_clksel2_pll |= CORE_CLK_SRC_DPLL;
			mult = (rate / 1000000);
			done_rate = CORE_CLK_SRC_DPLL;
		}
		tmpset.cm_clksel1_pll |= (div << __ffs(dd->mult_mask));
		tmpset.cm_clksel1_pll |= (mult << __ffs(dd->div1_mask));

		/* Worst case */
		tmpset.base_sdrc_rfr = SDRC_RFR_CTRL_BYPASS;

		if (rate == curr_prcm_set->xtal_speed)	/* If asking for 1-1 */
			bypass = 1;

		/* For omap2xxx_sdrc_init_params() */
		omap2xxx_sdrc_reprogram(CORE_CLK_SRC_DPLL_X2, 1);

		/* Force dll lock mode */
		omap2_set_prcm(tmpset.cm_clksel1_pll, tmpset.base_sdrc_rfr,
			       bypass);

		/* Errata: ret dll entry state */
		omap2xxx_sdrc_init_params(omap2xxx_sdrc_dll_is_unlocked());
		omap2xxx_sdrc_reprogram(done_rate, 0);
	}

	return 0;
}

/**
 * omap2xxx_clkt_dpllcore_init - clk init function for dpll_ck
 * @clk: struct clk *dpll_ck
 *
 * Store a local copy of @clk in dpll_core_ck so other code can query
 * the core rate without having to clk_get(), which can sleep.  Must
 * only be called once.  No return value.  XXX If the clock
 * registration process is ever changed such that dpll_ck is no longer
 * statically defined, this code may need to change to increment some
 * kind of use count on dpll_ck.
 */
void omap2xxx_clkt_dpllcore_init(struct clk_hw *hw)
{
	WARN(dpll_core_ck, "dpll_core_ck already set - should never happen");
	dpll_core_ck = to_clk_hw_omap(hw);
}
