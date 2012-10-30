/*
 * OMAP3/4 - specific DPLL control functions
 *
 * Copyright (C) 2009-2010 Texas Instruments, Inc.
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Written by Paul Walmsley
 * Testing and integration fixes by Jouni Högander
 *
 * 36xx support added by Vishwanath BS, Richard Woodruff, and Nishanth
 * Menon
 *
 * Parts of this code are based on code written by
 * Richard Woodruff, Tony Lindgren, Tuukka Tikkanen, Karthik Dasu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/clkdev.h>

#include <plat/clock.h>

#include "soc.h"
#include "clock.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-34xx.h"

/* CM_AUTOIDLE_PLL*.AUTO_* bit values */
#define DPLL_AUTOIDLE_DISABLE			0x0
#define DPLL_AUTOIDLE_LOW_POWER_STOP		0x1

#define MAX_DPLL_WAIT_TRIES		1000000

/* Private functions */

/* _omap3_dpll_write_clken - write clken_bits arg to a DPLL's enable bits */
static void _omap3_dpll_write_clken(struct clk *clk, u8 clken_bits)
{
	const struct dpll_data *dd;
	u32 v;

	dd = clk->dpll_data;

	v = __raw_readl(dd->control_reg);
	v &= ~dd->enable_mask;
	v |= clken_bits << __ffs(dd->enable_mask);
	__raw_writel(v, dd->control_reg);
}

/* _omap3_wait_dpll_status: wait for a DPLL to enter a specific state */
static int _omap3_wait_dpll_status(struct clk *clk, u8 state)
{
	const struct dpll_data *dd;
	int i = 0;
	int ret = -EINVAL;
	const char *clk_name;

	dd = clk->dpll_data;
	clk_name = __clk_get_name(clk);

	state <<= __ffs(dd->idlest_mask);

	while (((__raw_readl(dd->idlest_reg) & dd->idlest_mask) != state) &&
	       i < MAX_DPLL_WAIT_TRIES) {
		i++;
		udelay(1);
	}

	if (i == MAX_DPLL_WAIT_TRIES) {
		printk(KERN_ERR "clock: %s failed transition to '%s'\n",
		       clk_name, (state) ? "locked" : "bypassed");
	} else {
		pr_debug("clock: %s transition to '%s' in %d loops\n",
			 clk_name, (state) ? "locked" : "bypassed", i);

		ret = 0;
	}

	return ret;
}

/* From 3430 TRM ES2 4.7.6.2 */
static u16 _omap3_dpll_compute_freqsel(struct clk *clk, u8 n)
{
	unsigned long fint;
	u16 f = 0;

	fint = __clk_get_rate(clk->dpll_data->clk_ref) / n;

	pr_debug("clock: fint is %lu\n", fint);

	if (fint >= 750000 && fint <= 1000000)
		f = 0x3;
	else if (fint > 1000000 && fint <= 1250000)
		f = 0x4;
	else if (fint > 1250000 && fint <= 1500000)
		f = 0x5;
	else if (fint > 1500000 && fint <= 1750000)
		f = 0x6;
	else if (fint > 1750000 && fint <= 2100000)
		f = 0x7;
	else if (fint > 7500000 && fint <= 10000000)
		f = 0xB;
	else if (fint > 10000000 && fint <= 12500000)
		f = 0xC;
	else if (fint > 12500000 && fint <= 15000000)
		f = 0xD;
	else if (fint > 15000000 && fint <= 17500000)
		f = 0xE;
	else if (fint > 17500000 && fint <= 21000000)
		f = 0xF;
	else
		pr_debug("clock: unknown freqsel setting for %d\n", n);

	return f;
}

/*
 * _omap3_noncore_dpll_lock - instruct a DPLL to lock and wait for readiness
 * @clk: pointer to a DPLL struct clk
 *
 * Instructs a non-CORE DPLL to lock.  Waits for the DPLL to report
 * readiness before returning.  Will save and restore the DPLL's
 * autoidle state across the enable, per the CDP code.  If the DPLL
 * locked successfully, return 0; if the DPLL did not lock in the time
 * allotted, or DPLL3 was passed in, return -EINVAL.
 */
static int _omap3_noncore_dpll_lock(struct clk *clk)
{
	const struct dpll_data *dd;
	u8 ai;
	u8 state = 1;
	int r = 0;

	pr_debug("clock: locking DPLL %s\n", __clk_get_name(clk));

	dd = clk->dpll_data;
	state <<= __ffs(dd->idlest_mask);

	/* Check if already locked */
	if ((__raw_readl(dd->idlest_reg) & dd->idlest_mask) == state)
		goto done;

	ai = omap3_dpll_autoidle_read(clk);

	if (ai)
		omap3_dpll_deny_idle(clk);

	_omap3_dpll_write_clken(clk, DPLL_LOCKED);

	r = _omap3_wait_dpll_status(clk, 1);

	if (ai)
		omap3_dpll_allow_idle(clk);

done:
	return r;
}

/*
 * _omap3_noncore_dpll_bypass - instruct a DPLL to bypass and wait for readiness
 * @clk: pointer to a DPLL struct clk
 *
 * Instructs a non-CORE DPLL to enter low-power bypass mode.  In
 * bypass mode, the DPLL's rate is set equal to its parent clock's
 * rate.  Waits for the DPLL to report readiness before returning.
 * Will save and restore the DPLL's autoidle state across the enable,
 * per the CDP code.  If the DPLL entered bypass mode successfully,
 * return 0; if the DPLL did not enter bypass in the time allotted, or
 * DPLL3 was passed in, or the DPLL does not support low-power bypass,
 * return -EINVAL.
 */
static int _omap3_noncore_dpll_bypass(struct clk *clk)
{
	int r;
	u8 ai;

	if (!(clk->dpll_data->modes & (1 << DPLL_LOW_POWER_BYPASS)))
		return -EINVAL;

	pr_debug("clock: configuring DPLL %s for low-power bypass\n",
		 __clk_get_name(clk));

	ai = omap3_dpll_autoidle_read(clk);

	_omap3_dpll_write_clken(clk, DPLL_LOW_POWER_BYPASS);

	r = _omap3_wait_dpll_status(clk, 0);

	if (ai)
		omap3_dpll_allow_idle(clk);

	return r;
}

/*
 * _omap3_noncore_dpll_stop - instruct a DPLL to stop
 * @clk: pointer to a DPLL struct clk
 *
 * Instructs a non-CORE DPLL to enter low-power stop. Will save and
 * restore the DPLL's autoidle state across the stop, per the CDP
 * code.  If DPLL3 was passed in, or the DPLL does not support
 * low-power stop, return -EINVAL; otherwise, return 0.
 */
static int _omap3_noncore_dpll_stop(struct clk *clk)
{
	u8 ai;

	if (!(clk->dpll_data->modes & (1 << DPLL_LOW_POWER_STOP)))
		return -EINVAL;

	pr_debug("clock: stopping DPLL %s\n", __clk_get_name(clk));

	ai = omap3_dpll_autoidle_read(clk);

	_omap3_dpll_write_clken(clk, DPLL_LOW_POWER_STOP);

	if (ai)
		omap3_dpll_allow_idle(clk);

	return 0;
}

/**
 * _lookup_dco - Lookup DCO used by j-type DPLL
 * @clk: pointer to a DPLL struct clk
 * @dco: digital control oscillator selector
 * @m: DPLL multiplier to set
 * @n: DPLL divider to set
 *
 * See 36xx TRM section 3.5.3.3.3.2 "Type B DPLL (Low-Jitter)"
 *
 * XXX This code is not needed for 3430/AM35xx; can it be optimized
 * out in non-multi-OMAP builds for those chips?
 */
static void _lookup_dco(struct clk *clk, u8 *dco, u16 m, u8 n)
{
	unsigned long fint, clkinp; /* watch out for overflow */

	clkinp = __clk_get_rate(__clk_get_parent(clk));
	fint = (clkinp / n) * m;

	if (fint < 1000000000)
		*dco = 2;
	else
		*dco = 4;
}

/**
 * _lookup_sddiv - Calculate sigma delta divider for j-type DPLL
 * @clk: pointer to a DPLL struct clk
 * @sd_div: target sigma-delta divider
 * @m: DPLL multiplier to set
 * @n: DPLL divider to set
 *
 * See 36xx TRM section 3.5.3.3.3.2 "Type B DPLL (Low-Jitter)"
 *
 * XXX This code is not needed for 3430/AM35xx; can it be optimized
 * out in non-multi-OMAP builds for those chips?
 */
static void _lookup_sddiv(struct clk *clk, u8 *sd_div, u16 m, u8 n)
{
	unsigned long clkinp, sd; /* watch out for overflow */
	int mod1, mod2;

	clkinp = __clk_get_rate(__clk_get_parent(clk));

	/*
	 * target sigma-delta to near 250MHz
	 * sd = ceil[(m/(n+1)) * (clkinp_MHz / 250)]
	 */
	clkinp /= 100000; /* shift from MHz to 10*Hz for 38.4 and 19.2 */
	mod1 = (clkinp * m) % (250 * n);
	sd = (clkinp * m) / (250 * n);
	mod2 = sd % 10;
	sd /= 10;

	if (mod1 || mod2)
		sd++;
	*sd_div = sd;
}

/*
 * _omap3_noncore_dpll_program - set non-core DPLL M,N values directly
 * @clk: struct clk * of DPLL to set
 * @m: DPLL multiplier to set
 * @n: DPLL divider to set
 * @freqsel: FREQSEL value to set
 *
 * Program the DPLL with the supplied M, N values, and wait for the DPLL to
 * lock..  Returns -EINVAL upon error, or 0 upon success.
 */
static int omap3_noncore_dpll_program(struct clk *clk, u16 m, u8 n, u16 freqsel)
{
	struct dpll_data *dd = clk->dpll_data;
	u8 dco, sd_div;
	u32 v;

	/* 3430 ES2 TRM: 4.7.6.9 DPLL Programming Sequence */
	_omap3_noncore_dpll_bypass(clk);

	/*
	 * Set jitter correction. No jitter correction for OMAP4 and 3630
	 * since freqsel field is no longer present
	 */
	if (!soc_is_am33xx() && !cpu_is_omap44xx() && !cpu_is_omap3630()) {
		v = __raw_readl(dd->control_reg);
		v &= ~dd->freqsel_mask;
		v |= freqsel << __ffs(dd->freqsel_mask);
		__raw_writel(v, dd->control_reg);
	}

	/* Set DPLL multiplier, divider */
	v = __raw_readl(dd->mult_div1_reg);
	v &= ~(dd->mult_mask | dd->div1_mask);
	v |= m << __ffs(dd->mult_mask);
	v |= (n - 1) << __ffs(dd->div1_mask);

	/* Configure dco and sd_div for dplls that have these fields */
	if (dd->dco_mask) {
		_lookup_dco(clk, &dco, m, n);
		v &= ~(dd->dco_mask);
		v |= dco << __ffs(dd->dco_mask);
	}
	if (dd->sddiv_mask) {
		_lookup_sddiv(clk, &sd_div, m, n);
		v &= ~(dd->sddiv_mask);
		v |= sd_div << __ffs(dd->sddiv_mask);
	}

	__raw_writel(v, dd->mult_div1_reg);

	/* We let the clock framework set the other output dividers later */

	/* REVISIT: Set ramp-up delay? */

	_omap3_noncore_dpll_lock(clk);

	return 0;
}

/* Public functions */

/**
 * omap3_dpll_recalc - recalculate DPLL rate
 * @clk: DPLL struct clk
 *
 * Recalculate and propagate the DPLL rate.
 */
unsigned long omap3_dpll_recalc(struct clk *clk)
{
	return omap2_get_dpll_rate(clk);
}

/* Non-CORE DPLL (e.g., DPLLs that do not control SDRC) clock functions */

/**
 * omap3_noncore_dpll_enable - instruct a DPLL to enter bypass or lock mode
 * @clk: pointer to a DPLL struct clk
 *
 * Instructs a non-CORE DPLL to enable, e.g., to enter bypass or lock.
 * The choice of modes depends on the DPLL's programmed rate: if it is
 * the same as the DPLL's parent clock, it will enter bypass;
 * otherwise, it will enter lock.  This code will wait for the DPLL to
 * indicate readiness before returning, unless the DPLL takes too long
 * to enter the target state.  Intended to be used as the struct clk's
 * enable function.  If DPLL3 was passed in, or the DPLL does not
 * support low-power stop, or if the DPLL took too long to enter
 * bypass or lock, return -EINVAL; otherwise, return 0.
 */
int omap3_noncore_dpll_enable(struct clk *clk)
{
	int r;
	struct dpll_data *dd;
	struct clk *parent;

	dd = clk->dpll_data;
	if (!dd)
		return -EINVAL;

	parent = __clk_get_parent(clk);

	if (__clk_get_rate(clk) == __clk_get_rate(dd->clk_bypass)) {
		WARN_ON(parent != dd->clk_bypass);
		r = _omap3_noncore_dpll_bypass(clk);
	} else {
		WARN_ON(parent != dd->clk_ref);
		r = _omap3_noncore_dpll_lock(clk);
	}
	/*
	 *FIXME: this is dubious - if clk->rate has changed, what about
	 * propagating?
	 */
	if (!r)
		clk->rate = (clk->recalc) ? clk->recalc(clk) :
			omap2_get_dpll_rate(clk);

	return r;
}

/**
 * omap3_noncore_dpll_disable - instruct a DPLL to enter low-power stop
 * @clk: pointer to a DPLL struct clk
 *
 * Instructs a non-CORE DPLL to enter low-power stop.  This function is
 * intended for use in struct clkops.  No return value.
 */
void omap3_noncore_dpll_disable(struct clk *clk)
{
	_omap3_noncore_dpll_stop(clk);
}


/* Non-CORE DPLL rate set code */

/**
 * omap3_noncore_dpll_set_rate - set non-core DPLL rate
 * @clk: struct clk * of DPLL to set
 * @rate: rounded target rate
 *
 * Set the DPLL CLKOUT to the target rate.  If the DPLL can enter
 * low-power bypass, and the target rate is the bypass source clock
 * rate, then configure the DPLL for bypass.  Otherwise, round the
 * target rate if it hasn't been done already, then program and lock
 * the DPLL.  Returns -EINVAL upon error, or 0 upon success.
 */
int omap3_noncore_dpll_set_rate(struct clk *clk, unsigned long rate)
{
	struct clk *new_parent = NULL;
	unsigned long hw_rate, bypass_rate;
	u16 freqsel = 0;
	struct dpll_data *dd;
	int ret;

	if (!clk || !rate)
		return -EINVAL;

	dd = clk->dpll_data;
	if (!dd)
		return -EINVAL;

	hw_rate = (clk->recalc) ? clk->recalc(clk) : omap2_get_dpll_rate(clk);
	if (rate == hw_rate)
		return 0;

	/*
	 * Ensure both the bypass and ref clocks are enabled prior to
	 * doing anything; we need the bypass clock running to reprogram
	 * the DPLL.
	 */
	omap2_clk_enable(dd->clk_bypass);
	omap2_clk_enable(dd->clk_ref);

	bypass_rate = __clk_get_rate(dd->clk_bypass);
	if (bypass_rate == rate &&
	    (clk->dpll_data->modes & (1 << DPLL_LOW_POWER_BYPASS))) {
		pr_debug("clock: %s: set rate: entering bypass.\n", clk->name);

		ret = _omap3_noncore_dpll_bypass(clk);
		if (!ret)
			new_parent = dd->clk_bypass;
	} else {
		if (dd->last_rounded_rate != rate)
			rate = clk->round_rate(clk, rate);

		if (dd->last_rounded_rate == 0)
			return -EINVAL;

		/* No freqsel on OMAP4 and OMAP3630 */
		if (!soc_is_am33xx() && !cpu_is_omap44xx() && !cpu_is_omap3630()) {
			freqsel = _omap3_dpll_compute_freqsel(clk,
						dd->last_rounded_n);
			if (!freqsel)
				WARN_ON(1);
		}

		pr_debug("clock: %s: set rate: locking rate to %lu.\n",
			 __clk_get_name(clk), rate);

		ret = omap3_noncore_dpll_program(clk, dd->last_rounded_m,
						 dd->last_rounded_n, freqsel);
		if (!ret)
			new_parent = dd->clk_ref;
	}
	if (!ret) {
		/*
		 * Switch the parent clock in the hierarchy, and make sure
		 * that the new parent's usecount is correct.  Note: we
		 * enable the new parent before disabling the old to avoid
		 * any unnecessary hardware disable->enable transitions.
		 */
		if (clk->usecount) {
			omap2_clk_enable(new_parent);
			omap2_clk_disable(clk->parent);
		}
		clk_reparent(clk, new_parent);
		clk->rate = rate;
	}
	omap2_clk_disable(dd->clk_ref);
	omap2_clk_disable(dd->clk_bypass);

	return 0;
}

/* DPLL autoidle read/set code */

/**
 * omap3_dpll_autoidle_read - read a DPLL's autoidle bits
 * @clk: struct clk * of the DPLL to read
 *
 * Return the DPLL's autoidle bits, shifted down to bit 0.  Returns
 * -EINVAL if passed a null pointer or if the struct clk does not
 * appear to refer to a DPLL.
 */
u32 omap3_dpll_autoidle_read(struct clk *clk)
{
	const struct dpll_data *dd;
	u32 v;

	if (!clk || !clk->dpll_data)
		return -EINVAL;

	dd = clk->dpll_data;

	if (!dd->autoidle_reg)
		return -EINVAL;

	v = __raw_readl(dd->autoidle_reg);
	v &= dd->autoidle_mask;
	v >>= __ffs(dd->autoidle_mask);

	return v;
}

/**
 * omap3_dpll_allow_idle - enable DPLL autoidle bits
 * @clk: struct clk * of the DPLL to operate on
 *
 * Enable DPLL automatic idle control.  This automatic idle mode
 * switching takes effect only when the DPLL is locked, at least on
 * OMAP3430.  The DPLL will enter low-power stop when its downstream
 * clocks are gated.  No return value.
 */
void omap3_dpll_allow_idle(struct clk *clk)
{
	const struct dpll_data *dd;
	u32 v;

	if (!clk || !clk->dpll_data)
		return;

	dd = clk->dpll_data;

	if (!dd->autoidle_reg) {
		pr_debug("clock: DPLL %s: autoidle not supported\n",
			__clk_get_name(clk));
		return;
	}

	/*
	 * REVISIT: CORE DPLL can optionally enter low-power bypass
	 * by writing 0x5 instead of 0x1.  Add some mechanism to
	 * optionally enter this mode.
	 */
	v = __raw_readl(dd->autoidle_reg);
	v &= ~dd->autoidle_mask;
	v |= DPLL_AUTOIDLE_LOW_POWER_STOP << __ffs(dd->autoidle_mask);
	__raw_writel(v, dd->autoidle_reg);

}

/**
 * omap3_dpll_deny_idle - prevent DPLL from automatically idling
 * @clk: struct clk * of the DPLL to operate on
 *
 * Disable DPLL automatic idle control.  No return value.
 */
void omap3_dpll_deny_idle(struct clk *clk)
{
	const struct dpll_data *dd;
	u32 v;

	if (!clk || !clk->dpll_data)
		return;

	dd = clk->dpll_data;

	if (!dd->autoidle_reg) {
		pr_debug("clock: DPLL %s: autoidle not supported\n",
			__clk_get_name(clk));
		return;
	}

	v = __raw_readl(dd->autoidle_reg);
	v &= ~dd->autoidle_mask;
	v |= DPLL_AUTOIDLE_DISABLE << __ffs(dd->autoidle_mask);
	__raw_writel(v, dd->autoidle_reg);

}

/* Clock control for DPLL outputs */

/**
 * omap3_clkoutx2_recalc - recalculate DPLL X2 output virtual clock rate
 * @clk: DPLL output struct clk
 *
 * Using parent clock DPLL data, look up DPLL state.  If locked, set our
 * rate to the dpll_clk * 2; otherwise, just use dpll_clk.
 */
unsigned long omap3_clkoutx2_recalc(struct clk *clk)
{
	const struct dpll_data *dd;
	unsigned long rate;
	u32 v;
	struct clk *pclk;
	unsigned long parent_rate;

	/* Walk up the parents of clk, looking for a DPLL */
	pclk = __clk_get_parent(clk);
	while (pclk && !pclk->dpll_data)
		pclk = __clk_get_parent(pclk);

	/* clk does not have a DPLL as a parent?  error in the clock data */
	if (!pclk) {
		WARN_ON(1);
		return 0;
	}

	dd = pclk->dpll_data;

	WARN_ON(!dd->enable_mask);

	parent_rate = __clk_get_rate(__clk_get_parent(clk));
	v = __raw_readl(dd->control_reg) & dd->enable_mask;
	v >>= __ffs(dd->enable_mask);
	if ((v != OMAP3XXX_EN_DPLL_LOCKED) || (dd->flags & DPLL_J_TYPE))
		rate = parent_rate;
	else
		rate = parent_rate * 2;
	return rate;
}

/* OMAP3/4 non-CORE DPLL clkops */

const struct clkops clkops_omap3_noncore_dpll_ops = {
	.enable		= omap3_noncore_dpll_enable,
	.disable	= omap3_noncore_dpll_disable,
	.allow_idle	= omap3_dpll_allow_idle,
	.deny_idle	= omap3_dpll_deny_idle,
};

const struct clkops clkops_omap3_core_dpll_ops = {
	.allow_idle	= omap3_dpll_allow_idle,
	.deny_idle	= omap3_dpll_deny_idle,
};
