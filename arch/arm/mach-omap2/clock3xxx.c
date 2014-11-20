/*
 * OMAP3-specific clock framework functions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2010 Nokia Corporation
 *
 * Paul Walmsley
 * Jouni Högander
 *
 * Parts of this code are based on code written by
 * Richard Woodruff, Tony Lindgren, Tuukka Tikkanen, Karthik Dasu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/io.h>

#include "soc.h"
#include "clock.h"
#include "clock3xxx.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-34xx.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-34xx.h"

/*
 * DPLL5_FREQ_FOR_USBHOST: USBHOST and USBTLL are the only clocks
 * that are sourced by DPLL5, and both of these require this clock
 * to be at 120 MHz for proper operation.
 */
#define DPLL5_FREQ_FOR_USBHOST		120000000

/* needed by omap3_core_dpll_m2_set_rate() */
struct clk *sdrc_ick_p, *arm_fck_p;

/**
 * omap3_dpll4_set_rate - set rate for omap3 per-dpll
 * @hw: clock to change
 * @rate: target rate for clock
 * @parent_rate: rate of the parent clock
 *
 * Check if the current SoC supports the per-dpll reprogram operation
 * or not, and then do the rate change if supported. Returns -EINVAL
 * if not supported, 0 for success, and potential error codes from the
 * clock rate change.
 */
int omap3_dpll4_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	/*
	 * According to the 12-5 CDP code from TI, "Limitation 2.5"
	 * on 3430ES1 prevents us from changing DPLL multipliers or dividers
	 * on DPLL4.
	 */
	if (ti_clk_features.flags & TI_CLK_DPLL4_DENY_REPROGRAM) {
		pr_err("clock: DPLL4 cannot change rate due to silicon 'Limitation 2.5' on 3430ES1.\n");
		return -EINVAL;
	}

	return omap3_noncore_dpll_set_rate(hw, rate, parent_rate);
}

/**
 * omap3_dpll4_set_rate_and_parent - set rate and parent for omap3 per-dpll
 * @hw: clock to change
 * @rate: target rate for clock
 * @parent_rate: rate of the parent clock
 * @index: parent index, 0 - reference clock, 1 - bypass clock
 *
 * Check if the current SoC support the per-dpll reprogram operation
 * or not, and then do the rate + parent change if supported. Returns
 * -EINVAL if not supported, 0 for success, and potential error codes
 * from the clock rate change.
 */
int omap3_dpll4_set_rate_and_parent(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate, u8 index)
{
	if (ti_clk_features.flags & TI_CLK_DPLL4_DENY_REPROGRAM) {
		pr_err("clock: DPLL4 cannot change rate due to silicon 'Limitation 2.5' on 3430ES1.\n");
		return -EINVAL;
	}

	return omap3_noncore_dpll_set_rate_and_parent(hw, rate, parent_rate,
						      index);
}

void __init omap3_clk_lock_dpll5(void)
{
	struct clk *dpll5_clk;
	struct clk *dpll5_m2_clk;

	dpll5_clk = clk_get(NULL, "dpll5_ck");
	clk_set_rate(dpll5_clk, DPLL5_FREQ_FOR_USBHOST);
	clk_prepare_enable(dpll5_clk);

	/* Program dpll5_m2_clk divider for no division */
	dpll5_m2_clk = clk_get(NULL, "dpll5_m2_ck");
	clk_prepare_enable(dpll5_m2_clk);
	clk_set_rate(dpll5_m2_clk, DPLL5_FREQ_FOR_USBHOST);

	clk_disable_unprepare(dpll5_m2_clk);
	clk_disable_unprepare(dpll5_clk);
	return;
}

/* Common clock code */

/*
 * Switch the MPU rate if specified on cmdline.  We cannot do this
 * early until cmdline is parsed.  XXX This should be removed from the
 * clock code and handled by the OPP layer code in the near future.
 */
static int __init omap3xxx_clk_arch_init(void)
{
	int ret;

	if (!cpu_is_omap34xx())
		return 0;

	ret = omap2_clk_switch_mpurate_at_boot("dpll1_ck");
	if (!ret)
		omap2_clk_print_new_rates("osc_sys_ck", "core_ck", "arm_fck");

	return ret;
}

omap_arch_initcall(omap3xxx_clk_arch_init);


