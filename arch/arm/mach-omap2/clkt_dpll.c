/*
 * OMAP2/3/4 DPLL clock functions
 *
 * Copyright (C) 2005-2008 Texas Instruments, Inc.
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Contacts:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
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

#include <asm/div64.h>

#include "soc.h"
#include "clock.h"
#include "cm-regbits-24xx.h"
#include "cm-regbits-34xx.h"

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

/* DPLL valid Fint frequency band limits - from 34xx TRM Section 4.7.6.2 */
#define OMAP3430_DPLL_FINT_BAND1_MIN	750000
#define OMAP3430_DPLL_FINT_BAND1_MAX	2100000
#define OMAP3430_DPLL_FINT_BAND2_MIN	7500000
#define OMAP3430_DPLL_FINT_BAND2_MAX	21000000

/*
 * DPLL valid Fint frequency range for OMAP36xx and OMAP4xxx.
 * From device data manual section 4.3 "DPLL and DLL Specifications".
 */
#define OMAP3PLUS_DPLL_FINT_JTYPE_MIN	500000
#define OMAP3PLUS_DPLL_FINT_JTYPE_MAX	2500000
#define OMAP3PLUS_DPLL_FINT_MIN		32000
#define OMAP3PLUS_DPLL_FINT_MAX		52000000

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
static int _dpll_test_fint(struct clk *clk, u8 n)
{
	struct dpll_data *dd;
	long fint, fint_min, fint_max;
	int ret = 0;

	dd = clk->dpll_data;

	/* DPLL divider must result in a valid jitter correction val */
	fint = __clk_get_rate(__clk_get_parent(clk)) / n;

	if (cpu_is_omap24xx()) {
		/* Should not be called for OMAP2, so warn if it is called */
		WARN(1, "No fint limits available for OMAP2!\n");
		return DPLL_FINT_INVALID;
	} else if (cpu_is_omap3430()) {
		fint_min = OMAP3430_DPLL_FINT_BAND1_MIN;
		fint_max = OMAP3430_DPLL_FINT_BAND2_MAX;
	} else if (dd->flags & DPLL_J_TYPE) {
		fint_min = OMAP3PLUS_DPLL_FINT_JTYPE_MIN;
		fint_max = OMAP3PLUS_DPLL_FINT_JTYPE_MAX;
	} else {
		fint_min = OMAP3PLUS_DPLL_FINT_MIN;
		fint_max = OMAP3PLUS_DPLL_FINT_MAX;
	}

	if (fint < fint_min) {
		pr_debug("rejecting n=%d due to Fint failure, lowering max_divider\n",
			 n);
		dd->max_divider = n;
		ret = DPLL_FINT_UNDERFLOW;
	} else if (fint > fint_max) {
		pr_debug("rejecting n=%d due to Fint failure, boosting min_divider\n",
			 n);
		dd->min_divider = n;
		ret = DPLL_FINT_INVALID;
	} else if (cpu_is_omap3430() && fint > OMAP3430_DPLL_FINT_BAND1_MAX &&
		   fint < OMAP3430_DPLL_FINT_BAND2_MIN) {
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

/* Public functions */

void omap2_init_dpll_parent(struct clk *clk)
{
	u32 v;
	struct dpll_data *dd;

	dd = clk->dpll_data;
	if (!dd)
		return;

	v = __raw_readl(dd->control_reg);
	v &= dd->enable_mask;
	v >>= __ffs(dd->enable_mask);

	/* Reparent the struct clk in case the dpll is in bypass */
	if (cpu_is_omap24xx()) {
		if (v == OMAP2XXX_EN_DPLL_LPBYPASS ||
		    v == OMAP2XXX_EN_DPLL_FRBYPASS)
			clk_reparent(clk, dd->clk_bypass);
	} else if (cpu_is_omap34xx()) {
		if (v == OMAP3XXX_EN_DPLL_LPBYPASS ||
		    v == OMAP3XXX_EN_DPLL_FRBYPASS)
			clk_reparent(clk, dd->clk_bypass);
	} else if (soc_is_am33xx() || cpu_is_omap44xx()) {
		if (v == OMAP4XXX_EN_DPLL_LPBYPASS ||
		    v == OMAP4XXX_EN_DPLL_FRBYPASS ||
		    v == OMAP4XXX_EN_DPLL_MNBYPASS)
			clk_reparent(clk, dd->clk_bypass);
	}
	return;
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
u32 omap2_get_dpll_rate(struct clk *clk)
{
	long long dpll_clk;
	u32 dpll_mult, dpll_div, v;
	struct dpll_data *dd;

	dd = clk->dpll_data;
	if (!dd)
		return 0;

	/* Return bypass rate if DPLL is bypassed */
	v = __raw_readl(dd->control_reg);
	v &= dd->enable_mask;
	v >>= __ffs(dd->enable_mask);

	if (cpu_is_omap24xx()) {
		if (v == OMAP2XXX_EN_DPLL_LPBYPASS ||
		    v == OMAP2XXX_EN_DPLL_FRBYPASS)
			return __clk_get_rate(dd->clk_bypass);
	} else if (cpu_is_omap34xx()) {
		if (v == OMAP3XXX_EN_DPLL_LPBYPASS ||
		    v == OMAP3XXX_EN_DPLL_FRBYPASS)
			return __clk_get_rate(dd->clk_bypass);
	} else if (soc_is_am33xx() || cpu_is_omap44xx()) {
		if (v == OMAP4XXX_EN_DPLL_LPBYPASS ||
		    v == OMAP4XXX_EN_DPLL_FRBYPASS ||
		    v == OMAP4XXX_EN_DPLL_MNBYPASS)
			return __clk_get_rate(dd->clk_bypass);
	}

	v = __raw_readl(dd->mult_div1_reg);
	dpll_mult = v & dd->mult_mask;
	dpll_mult >>= __ffs(dd->mult_mask);
	dpll_div = v & dd->div1_mask;
	dpll_div >>= __ffs(dd->div1_mask);

	dpll_clk = (long long) __clk_get_rate(dd->clk_ref) * dpll_mult;
	do_div(dpll_clk, dpll_div + 1);

	return dpll_clk;
}

/* DPLL rate rounding code */

/**
 * omap2_dpll_round_rate - round a target rate for an OMAP DPLL
 * @clk: struct clk * for a DPLL
 * @target_rate: desired DPLL clock rate
 *
 * Given a DPLL and a desired target rate, round the target rate to a
 * possible, programmable rate for this DPLL.  Attempts to select the
 * minimum possible n.  Stores the computed (m, n) in the DPLL's
 * dpll_data structure so set_rate() will not need to call this
 * (expensive) function again.  Returns ~0 if the target rate cannot
 * be rounded, or the rounded rate upon success.
 */
long omap2_dpll_round_rate(struct clk *clk, unsigned long target_rate)
{
	int m, n, r, scaled_max_m;
	unsigned long scaled_rt_rp;
	unsigned long new_rate = 0;
	struct dpll_data *dd;
	unsigned long ref_rate;
	const char *clk_name;

	if (!clk || !clk->dpll_data)
		return ~0;

	dd = clk->dpll_data;

	ref_rate = __clk_get_rate(dd->clk_ref);
	clk_name = __clk_get_name(clk);
	pr_debug("clock: %s: starting DPLL round_rate, target rate %ld\n",
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

		pr_debug("clock: %s: m = %d: n = %d: new_rate = %ld\n",
			 clk_name, m, n, new_rate);

		if (target_rate == new_rate) {
			dd->last_rounded_m = m;
			dd->last_rounded_n = n;
			dd->last_rounded_rate = target_rate;
			break;
		}
	}

	if (target_rate != new_rate) {
		pr_debug("clock: %s: cannot round to rate %ld\n",
			 clk_name, target_rate);
		return ~0;
	}

	return target_rate;
}

