// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP2/3/4 DPLL clock functions
 *
 * Copyright (C) 2005-2008 Texas Instruments, Inc.
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Contacts:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/clk/ti.h>

#include <asm/div64.h>

#include "clock.h"

/* DPLL rate rounding: minimum DPLL multiplier, divider values */
#define DPLL_MIN_MULTIPLIER		2
#define DPLL_MIN_DIVIDER		1

/* Possible error results from _dpll_test_mult */
#define DPLL_MULT_UNDERFLOW		-1

/*
 * Scale factor to mitigate roundoff errors in DPLL rate rounding.
 * The higher the scale factor, the greater the risk of arithmetic overflow,
 * but the closer the rounded rate to the target rate.  DPLL_SCALE_FACTOR
 * must be a power of DPLL_SCALE_BASE.
 */
#define DPLL_SCALE_FACTOR		64
#define DPLL_SCALE_BASE			2
#define DPLL_ROUNDING_VAL		((DPLL_SCALE_BASE / 2) * \
					 (DPLL_SCALE_FACTOR / DPLL_SCALE_BASE))

/*
 * DPLL valid Fint frequency range for OMAP36xx and OMAP4xxx.
 * From device data manual section 4.3 "DPLL and DLL Specifications".
 */
#define OMAP3PLUS_DPLL_FINT_JTYPE_MIN	500000
#define OMAP3PLUS_DPLL_FINT_JTYPE_MAX	2500000

/* _dpll_test_fint() return codes */
#define DPLL_FINT_UNDERFLOW		-1
#define DPLL_FINT_INVALID		-2

/* Private functions */

/*
 * _dpll_test_fint - test whether an Fint value is valid for the DPLL
 * @clk: DPLL struct clk to test
 * @n: divider value (N) to test
 *
 * Tests whether a particular divider @n will result in a valid DPLL
 * internal clock frequency Fint. See the 34xx TRM 4.7.6.2 "DPLL Jitter
 * Correction".  Returns 0 if OK, -1 if the enclosing loop can terminate
 * (assuming that it is counting N upwards), or -2 if the enclosing loop
 * should skip to the next iteration (again assuming N is increasing).
 */
static int _dpll_test_fint(struct clk_hw_omap *clk, unsigned int n)
{
	struct dpll_data *dd;
	long fint, fint_min, fint_max;
	int ret = 0;

	dd = clk->dpll_data;

	/* DPLL divider must result in a valid jitter correction val */
	fint = clk_hw_get_rate(clk_hw_get_parent(&clk->hw)) / n;

	if (dd->flags & DPLL_J_TYPE) {
		fint_min = OMAP3PLUS_DPLL_FINT_JTYPE_MIN;
		fint_max = OMAP3PLUS_DPLL_FINT_JTYPE_MAX;
	} else {
		fint_min = ti_clk_get_features()->fint_min;
		fint_max = ti_clk_get_features()->fint_max;
	}

	if (!fint_min || !fint_max) {
		WARN(1, "No fint limits available!\n");
		return DPLL_FINT_INVALID;
	}

	if (fint < ti_clk_get_features()->fint_min) {
		pr_debug("rejecting n=%d due to Fint failure, lowering max_divider\n",
			 n);
		dd->max_divider = n;
		ret = DPLL_FINT_UNDERFLOW;
	} else if (fint > ti_clk_get_features()->fint_max) {
		pr_debug("rejecting n=%d due to Fint failure, boosting min_divider\n",
			 n);
		dd->min_divider = n;
		ret = DPLL_FINT_INVALID;
	} else if (fint > ti_clk_get_features()->fint_band1_max &&
		   fint < ti_clk_get_features()->fint_band2_min) {
		pr_debug("rejecting n=%d due to Fint failure\n", n);
		ret = DPLL_FINT_INVALID;
	}

	return ret;
}

static unsigned long _dpll_compute_new_rate(unsigned long parent_rate,
					    unsigned int m, unsigned int n)
{
	unsigned long long num;

	num = (unsigned long long)parent_rate * m;
	do_div(num, n);
	return num;
}

/*
 * _dpll_test_mult - test a DPLL multiplier value
 * @m: pointer to the DPLL m (multiplier) value under test
 * @n: current DPLL n (divider) value under test
 * @new_rate: pointer to storage for the resulting rounded rate
 * @target_rate: the desired DPLL rate
 * @parent_rate: the DPLL's parent clock rate
 *
 * This code tests a DPLL multiplier value, ensuring that the
 * resulting rate will not be higher than the target_rate, and that
 * the multiplier value itself is valid for the DPLL.  Initially, the
 * integer pointed to by the m argument should be prescaled by
 * multiplying by DPLL_SCALE_FACTOR.  The code will replace this with
 * a non-scaled m upon return.  This non-scaled m will result in a
 * new_rate as close as possible to target_rate (but not greater than
 * target_rate) given the current (parent_rate, n, prescaled m)
 * triple. Returns DPLL_MULT_UNDERFLOW in the event that the
 * non-scaled m attempted to underflow, which can allow the calling
 * function to bail out early; or 0 upon success.
 */
static int _dpll_test_mult(int *m, int n, unsigned long *new_rate,
			   unsigned long target_rate,
			   unsigned long parent_rate)
{
	int r = 0, carry = 0;

	/* Unscale m and round if necessary */
	if (*m % DPLL_SCALE_FACTOR >= DPLL_ROUNDING_VAL)
		carry = 1;
	*m = (*m / DPLL_SCALE_FACTOR) + carry;

	/*
	 * The new rate must be <= the target rate to avoid programming
	 * a rate that is impossible for the hardware to handle
	 */
	*new_rate = _dpll_compute_new_rate(parent_rate, *m, n);
	if (*new_rate > target_rate) {
		(*m)--;
		*new_rate = 0;
	}

	/* Guard against m underflow */
	if (*m < DPLL_MIN_MULTIPLIER) {
		*m = DPLL_MIN_MULTIPLIER;
		*new_rate = 0;
		r = DPLL_MULT_UNDERFLOW;
	}

	if (*new_rate == 0)
		*new_rate = _dpll_compute_new_rate(parent_rate, *m, n);

	return r;
}

/**
 * _omap2_dpll_is_in_bypass - check if DPLL is in bypass mode or not
 * @v: bitfield value of the DPLL enable
 *
 * Checks given DPLL enable bitfield to see whether the DPLL is in bypass
 * mode or not. Returns 1 if the DPLL is in bypass, 0 otherwise.
 */
static int _omap2_dpll_is_in_bypass(u32 v)
{
	u8 mask, val;

	mask = ti_clk_get_features()->dpll_bypass_vals;

	/*
	 * Each set bit in the mask corresponds to a bypass value equal
	 * to the bitshift. Go through each set-bit in the mask and
	 * compare against the given register value.
	 */
	while (mask) {
		val = __ffs(mask);
		mask ^= (1 << val);
		if (v == val)
			return 1;
	}

	return 0;
}

/* Public functions */
u8 omap2_init_dpll_parent(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 v;
	struct dpll_data *dd;

	dd = clk->dpll_data;
	if (!dd)
		return -EINVAL;

	v = ti_clk_ll_ops->clk_readl(&dd->control_reg);
	v &= dd->enable_mask;
	v >>= __ffs(dd->enable_mask);

	/* Reparent the struct clk in case the dpll is in bypass */
	if (_omap2_dpll_is_in_bypass(v))
		return 1;

	return 0;
}

/**
 * omap2_get_dpll_rate - returns the current DPLL CLKOUT rate
 * @clk: struct clk * of a DPLL
 *
 * DPLLs can be locked or bypassed - basically, enabled or disabled.
 * When locked, the DPLL output depends on the M and N values.  When
 * bypassed, on OMAP2xxx, the output rate is either the 32KiHz clock
 * or sys_clk.  Bypass rates on OMAP3 depend on the DPLL: DPLLs 1 and
 * 2 are bypassed with dpll1_fclk and dpll2_fclk respectively
 * (generated by DPLL3), while DPLL 3, 4, and 5 bypass rates are sys_clk.
 * Returns the current DPLL CLKOUT rate (*not* CLKOUTX2) if the DPLL is
 * locked, or the appropriate bypass rate if the DPLL is bypassed, or 0
 * if the clock @clk is not a DPLL.
 */
unsigned long omap2_get_dpll_rate(struct clk_hw_omap *clk)
{
	u64 dpll_clk;
	u32 dpll_mult, dpll_div, v;
	struct dpll_data *dd;

	dd = clk->dpll_data;
	if (!dd)
		return 0;

	/* Return bypass rate if DPLL is bypassed */
	v = ti_clk_ll_ops->clk_readl(&dd->control_reg);
	v &= dd->enable_mask;
	v >>= __ffs(dd->enable_mask);

	if (_omap2_dpll_is_in_bypass(v))
		return clk_hw_get_rate(dd->clk_bypass);

	v = ti_clk_ll_ops->clk_readl(&dd->mult_div1_reg);
	dpll_mult = v & dd->mult_mask;
	dpll_mult >>= __ffs(dd->mult_mask);
	dpll_div = v & dd->div1_mask;
	dpll_div >>= __ffs(dd->div1_mask);

	dpll_clk = (u64)clk_hw_get_rate(dd->clk_ref) * dpll_mult;
	do_div(dpll_clk, dpll_div + 1);

	return dpll_clk;
}

/* DPLL rate rounding code */

/**
 * omap2_dpll_round_rate - round a target rate for an OMAP DPLL
 * @hw: struct clk_hw containing the struct clk * for a DPLL
 * @target_rate: desired DPLL clock rate
 * @parent_rate: parent's DPLL clock rate
 *
 * Given a DPLL and a desired target rate, round the target rate to a
 * possible, programmable rate for this DPLL.  Attempts to select the
 * minimum possible n.  Stores the computed (m, n) in the DPLL's
 * dpll_data structure so set_rate() will not need to call this
 * (expensive) function again.  Returns ~0 if the target rate cannot
 * be rounded, or the rounded rate upon success.
 */
long omap2_dpll_round_rate(struct clk_hw *hw, unsigned long target_rate,
			   unsigned long *parent_rate)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	int m, n, r, scaled_max_m;
	int min_delta_m = INT_MAX, min_delta_n = INT_MAX;
	unsigned long scaled_rt_rp;
	unsigned long new_rate = 0;
	struct dpll_data *dd;
	unsigned long ref_rate;
	long delta;
	long prev_min_delta = LONG_MAX;
	const char *clk_name;

	if (!clk || !clk->dpll_data)
		return ~0;

	dd = clk->dpll_data;

	if (dd->max_rate && target_rate > dd->max_rate)
		target_rate = dd->max_rate;

	ref_rate = clk_hw_get_rate(dd->clk_ref);
	clk_name = clk_hw_get_name(hw);
	pr_debug("clock: %s: starting DPLL round_rate, target rate %lu\n",
		 clk_name, target_rate);

	scaled_rt_rp = target_rate / (ref_rate / DPLL_SCALE_FACTOR);
	scaled_max_m = dd->max_multiplier * DPLL_SCALE_FACTOR;

	dd->last_rounded_rate = 0;

	for (n = dd->min_divider; n <= dd->max_divider; n++) {
		/* Is the (input clk, divider) pair valid for the DPLL? */
		r = _dpll_test_fint(clk, n);
		if (r == DPLL_FINT_UNDERFLOW)
			break;
		else if (r == DPLL_FINT_INVALID)
			continue;

		/* Compute the scaled DPLL multiplier, based on the divider */
		m = scaled_rt_rp * n;

		/*
		 * Since we're counting n up, a m overflow means we
		 * can bail out completely (since as n increases in
		 * the next iteration, there's no way that m can
		 * increase beyond the current m)
		 */
		if (m > scaled_max_m)
			break;

		r = _dpll_test_mult(&m, n, &new_rate, target_rate,
				    ref_rate);

		/* m can't be set low enough for this n - try with a larger n */
		if (r == DPLL_MULT_UNDERFLOW)
			continue;

		/* skip rates above our target rate */
		delta = target_rate - new_rate;
		if (delta < 0)
			continue;

		if (delta < prev_min_delta) {
			prev_min_delta = delta;
			min_delta_m = m;
			min_delta_n = n;
		}

		pr_debug("clock: %s: m = %d: n = %d: new_rate = %lu\n",
			 clk_name, m, n, new_rate);

		if (delta == 0)
			break;
	}

	if (prev_min_delta == LONG_MAX) {
		pr_debug("clock: %s: cannot round to rate %lu\n",
			 clk_name, target_rate);
		return ~0;
	}

	dd->last_rounded_m = min_delta_m;
	dd->last_rounded_n = min_delta_n;
	dd->last_rounded_rate = target_rate - prev_min_delta;

	return dd->last_rounded_rate;
}
