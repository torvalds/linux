// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP4-specific DPLL control functions
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Rajendra Nayak
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/clk/ti.h>

#include "clock.h"

/*
 * Maximum DPLL input frequency (FINT) and output frequency (FOUT) that
 * can supported when using the DPLL low-power mode. Frequencies are
 * defined in OMAP4430/60 Public TRM section 3.6.3.3.2 "Enable Control,
 * Status, and Low-Power Operation Mode".
 */
#define OMAP4_DPLL_LP_FINT_MAX	1000000
#define OMAP4_DPLL_LP_FOUT_MAX	100000000

/*
 * Bitfield declarations
 */
#define OMAP4430_DPLL_CLKOUT_GATE_CTRL_MASK		BIT(8)
#define OMAP4430_DPLL_CLKOUTX2_GATE_CTRL_MASK		BIT(10)
#define OMAP4430_DPLL_REGM4XEN_MASK			BIT(11)

/* Static rate multiplier for OMAP4 REGM4XEN clocks */
#define OMAP4430_REGM4XEN_MULT				4

static void omap4_dpllmx_allow_gatectrl(struct clk_hw_omap *clk)
{
	u32 v;
	u32 mask;

	if (!clk)
		return;

	mask = clk->flags & CLOCK_CLKOUTX2 ?
			OMAP4430_DPLL_CLKOUTX2_GATE_CTRL_MASK :
			OMAP4430_DPLL_CLKOUT_GATE_CTRL_MASK;

	v = ti_clk_ll_ops->clk_readl(&clk->clksel_reg);
	/* Clear the bit to allow gatectrl */
	v &= ~mask;
	ti_clk_ll_ops->clk_writel(v, &clk->clksel_reg);
}

static void omap4_dpllmx_deny_gatectrl(struct clk_hw_omap *clk)
{
	u32 v;
	u32 mask;

	if (!clk)
		return;

	mask = clk->flags & CLOCK_CLKOUTX2 ?
			OMAP4430_DPLL_CLKOUTX2_GATE_CTRL_MASK :
			OMAP4430_DPLL_CLKOUT_GATE_CTRL_MASK;

	v = ti_clk_ll_ops->clk_readl(&clk->clksel_reg);
	/* Set the bit to deny gatectrl */
	v |= mask;
	ti_clk_ll_ops->clk_writel(v, &clk->clksel_reg);
}

const struct clk_hw_omap_ops clkhwops_omap4_dpllmx = {
	.allow_idle	= omap4_dpllmx_allow_gatectrl,
	.deny_idle      = omap4_dpllmx_deny_gatectrl,
};

/**
 * omap4_dpll_lpmode_recalc - compute DPLL low-power setting
 * @dd: pointer to the dpll data structure
 *
 * Calculates if low-power mode can be enabled based upon the last
 * multiplier and divider values calculated. If low-power mode can be
 * enabled, then the bit to enable low-power mode is stored in the
 * last_rounded_lpmode variable. This implementation is based upon the
 * criteria for enabling low-power mode as described in the OMAP4430/60
 * Public TRM section 3.6.3.3.2 "Enable Control, Status, and Low-Power
 * Operation Mode".
 */
static void omap4_dpll_lpmode_recalc(struct dpll_data *dd)
{
	long fint, fout;

	fint = clk_hw_get_rate(dd->clk_ref) / (dd->last_rounded_n + 1);
	fout = fint * dd->last_rounded_m;

	if ((fint < OMAP4_DPLL_LP_FINT_MAX) && (fout < OMAP4_DPLL_LP_FOUT_MAX))
		dd->last_rounded_lpmode = 1;
	else
		dd->last_rounded_lpmode = 0;
}

/**
 * omap4_dpll_regm4xen_recalc - compute DPLL rate, considering REGM4XEN bit
 * @hw: pointer to the clock to compute the rate for
 * @parent_rate: clock rate of the DPLL parent
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
	v = ti_clk_ll_ops->clk_readl(&dd->control_reg);
	if (v & OMAP4430_DPLL_REGM4XEN_MASK)
		rate *= OMAP4430_REGM4XEN_MULT;

	return rate;
}

/**
 * omap4_dpll_regm4xen_determine_rate - determine rate for a DPLL
 * @hw: pointer to the clock to determine rate for
 * @req: target rate request
 *
 * Determines which DPLL mode to use for reaching a desired rate.
 * Checks whether the DPLL shall be in bypass or locked mode, and if
 * locked, calculates the M,N values for the DPLL.
 * Returns 0 on success and a negative error value otherwise.
 */
int omap4_dpll_regm4xen_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct dpll_data *dd;

	if (!req->rate)
		return -EINVAL;

	dd = clk->dpll_data;
	if (!dd)
		return -EINVAL;

	if (clk_hw_get_rate(dd->clk_bypass) == req->rate &&
	    (dd->modes & (1 << DPLL_LOW_POWER_BYPASS))) {
		req->best_parent_hw = dd->clk_bypass;
	} else {
		struct clk_rate_request tmp_req;
		long r;

		clk_hw_init_rate_request(hw, &tmp_req, req->rate);
		dd->last_rounded_m4xen = 0;

		/*
		 * First try to compute the DPLL configuration for
		 * target rate without using the 4X multiplier.
		 */

		r = omap2_dpll_determine_rate(hw, &tmp_req);
		if (r < 0) {
			/*
			 * If we did not find a valid DPLL configuration, try again, but
			 * this time see if using the 4X multiplier can help. Enabling the
			 * 4X multiplier is equivalent to dividing the target rate by 4.
			 */
			tmp_req.rate /= OMAP4430_REGM4XEN_MULT;
			r = omap2_dpll_determine_rate(hw, &tmp_req);
			if (r < 0)
				return r;

			dd->last_rounded_rate *= OMAP4430_REGM4XEN_MULT;
			dd->last_rounded_m4xen = 1;
		}

		omap4_dpll_lpmode_recalc(dd);

		req->rate = dd->last_rounded_rate;
		req->best_parent_hw = dd->clk_ref;
	}

	req->best_parent_rate = req->rate;

	return 0;
}
