/*
 *  linux/arch/arm/mach-omap2/clock.c
 *
 *  Copyright (C) 2005-2008 Texas Instruments, Inc.
 *  Copyright (C) 2004-2008 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/bitops.h>

#include <mach/clock.h>
#include <mach/clockdomain.h>
#include <mach/cpu.h>
#include <asm/div64.h>

#include <mach/sdrc.h>
#include "sdrc.h"
#include "clock.h"
#include "prm.h"
#include "prm-regbits-24xx.h"
#include "cm.h"
#include "cm-regbits-24xx.h"
#include "cm-regbits-34xx.h"

#define MAX_CLOCK_ENABLE_WAIT		100000

/* DPLL rate rounding: minimum DPLL multiplier, divider values */
#define DPLL_MIN_MULTIPLIER		1
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
#define DPLL_FINT_BAND1_MIN		750000
#define DPLL_FINT_BAND1_MAX		2100000
#define DPLL_FINT_BAND2_MIN		7500000
#define DPLL_FINT_BAND2_MAX		21000000

/* _dpll_test_fint() return codes */
#define DPLL_FINT_UNDERFLOW		-1
#define DPLL_FINT_INVALID		-2

u8 cpu_mask;

/*-------------------------------------------------------------------------
 * OMAP2/3 specific clock functions
 *-------------------------------------------------------------------------*/

/**
 * _omap2xxx_clk_commit - commit clock parent/rate changes in hardware
 * @clk: struct clk *
 *
 * If @clk has the DELAYED_APP flag set, meaning that parent/rate changes
 * don't take effect until the VALID_CONFIG bit is written, write the
 * VALID_CONFIG bit and wait for the write to complete.  No return value.
 */
static void _omap2xxx_clk_commit(struct clk *clk)
{
	if (!cpu_is_omap24xx())
		return;

	if (!(clk->flags & DELAYED_APP))
		return;

	prm_write_mod_reg(OMAP24XX_VALID_CONFIG, OMAP24XX_GR_MOD,
		OMAP24XX_PRCM_CLKCFG_CTRL_OFFSET);
	/* OCP barrier */
	prm_read_mod_reg(OMAP24XX_GR_MOD, OMAP24XX_PRCM_CLKCFG_CTRL_OFFSET);
}

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
	long fint;
	int ret = 0;

	dd = clk->dpll_data;

	/* DPLL divider must result in a valid jitter correction val */
	fint = clk->parent->rate / (n + 1);
	if (fint < DPLL_FINT_BAND1_MIN) {

		pr_debug("rejecting n=%d due to Fint failure, "
			 "lowering max_divider\n", n);
		dd->max_divider = n;
		ret = DPLL_FINT_UNDERFLOW;

	} else if (fint > DPLL_FINT_BAND1_MAX &&
		   fint < DPLL_FINT_BAND2_MIN) {

		pr_debug("rejecting n=%d due to Fint failure\n", n);
		ret = DPLL_FINT_INVALID;

	} else if (fint > DPLL_FINT_BAND2_MAX) {

		pr_debug("rejecting n=%d due to Fint failure, "
			 "boosting min_divider\n", n);
		dd->min_divider = n;
		ret = DPLL_FINT_INVALID;

	}

	return ret;
}

/**
 * omap2_init_clk_clkdm - look up a clockdomain name, store pointer in clk
 * @clk: OMAP clock struct ptr to use
 *
 * Convert a clockdomain name stored in a struct clk 'clk' into a
 * clockdomain pointer, and save it into the struct clk.  Intended to be
 * called during clk_register().  No return value.
 */
void omap2_init_clk_clkdm(struct clk *clk)
{
	struct clockdomain *clkdm;

	if (!clk->clkdm_name)
		return;

	clkdm = clkdm_lookup(clk->clkdm_name);
	if (clkdm) {
		pr_debug("clock: associated clk %s to clkdm %s\n",
			 clk->name, clk->clkdm_name);
		clk->clkdm = clkdm;
	} else {
		pr_debug("clock: could not associate clk %s to "
			 "clkdm %s\n", clk->name, clk->clkdm_name);
	}
}

/**
 * omap2_init_clksel_parent - set a clksel clk's parent field from the hardware
 * @clk: OMAP clock struct ptr to use
 *
 * Given a pointer to a source-selectable struct clk, read the hardware
 * register and determine what its parent is currently set to.  Update the
 * clk->parent field with the appropriate clk ptr.
 */
void omap2_init_clksel_parent(struct clk *clk)
{
	const struct clksel *clks;
	const struct clksel_rate *clkr;
	u32 r, found = 0;

	if (!clk->clksel)
		return;

	r = __raw_readl(clk->clksel_reg) & clk->clksel_mask;
	r >>= __ffs(clk->clksel_mask);

	for (clks = clk->clksel; clks->parent && !found; clks++) {
		for (clkr = clks->rates; clkr->div && !found; clkr++) {
			if ((clkr->flags & cpu_mask) && (clkr->val == r)) {
				if (clk->parent != clks->parent) {
					pr_debug("clock: inited %s parent "
						 "to %s (was %s)\n",
						 clk->name, clks->parent->name,
						 ((clk->parent) ?
						  clk->parent->name : "NULL"));
					clk_reparent(clk, clks->parent);
				};
				found = 1;
			}
		}
	}

	if (!found)
		printk(KERN_ERR "clock: init parent: could not find "
		       "regval %0x for clock %s\n", r,  clk->name);

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
			return dd->clk_bypass->rate;
	} else if (cpu_is_omap34xx()) {
		if (v == OMAP3XXX_EN_DPLL_LPBYPASS ||
		    v == OMAP3XXX_EN_DPLL_FRBYPASS)
			return dd->clk_bypass->rate;
	}

	v = __raw_readl(dd->mult_div1_reg);
	dpll_mult = v & dd->mult_mask;
	dpll_mult >>= __ffs(dd->mult_mask);
	dpll_div = v & dd->div1_mask;
	dpll_div >>= __ffs(dd->div1_mask);

	dpll_clk = (long long)dd->clk_ref->rate * dpll_mult;
	do_div(dpll_clk, dpll_div + 1);

	return dpll_clk;
}

/*
 * Used for clocks that have the same value as the parent clock,
 * divided by some factor
 */
unsigned long omap2_fixed_divisor_recalc(struct clk *clk)
{
	WARN_ON(!clk->fixed_div);

	return clk->parent->rate / clk->fixed_div;
}

/**
 * omap2_wait_clock_ready - wait for clock to enable
 * @reg: physical address of clock IDLEST register
 * @mask: value to mask against to determine if the clock is active
 * @name: name of the clock (for printk)
 *
 * Returns 1 if the clock enabled in time, or 0 if it failed to enable
 * in roughly MAX_CLOCK_ENABLE_WAIT microseconds.
 */
int omap2_wait_clock_ready(void __iomem *reg, u32 mask, const char *name)
{
	int i = 0;
	int ena = 0;

	/*
	 * 24xx uses 0 to indicate not ready, and 1 to indicate ready.
	 * 34xx reverses this, just to keep us on our toes
	 */
	if (cpu_mask & (RATE_IN_242X | RATE_IN_243X))
		ena = mask;
	else if (cpu_mask & RATE_IN_343X)
		ena = 0;

	/* Wait for lock */
	while (((__raw_readl(reg) & mask) != ena) &&
	       (i++ < MAX_CLOCK_ENABLE_WAIT)) {
		udelay(1);
	}

	if (i < MAX_CLOCK_ENABLE_WAIT)
		pr_debug("Clock %s stable after %d loops\n", name, i);
	else
		printk(KERN_ERR "Clock %s didn't enable in %d tries\n",
		       name, MAX_CLOCK_ENABLE_WAIT);


	return (i < MAX_CLOCK_ENABLE_WAIT) ? 1 : 0;
};


/*
 * Note: We don't need special code here for INVERT_ENABLE
 * for the time being since INVERT_ENABLE only applies to clocks enabled by
 * CM_CLKEN_PLL
 */
static void omap2_clk_wait_ready(struct clk *clk)
{
	void __iomem *reg, *other_reg, *st_reg;
	u32 bit;

	/*
	 * REVISIT: This code is pretty ugly.  It would be nice to generalize
	 * it and pull it into struct clk itself somehow.
	 */
	reg = clk->enable_reg;

	/*
	 * Convert CM_ICLKEN* <-> CM_FCLKEN*.  This conversion assumes
	 * it's just a matter of XORing the bits.
	 */
	other_reg = (void __iomem *)((u32)reg ^ (CM_FCLKEN ^ CM_ICLKEN));

	/* Check if both functional and interface clocks
	 * are running. */
	bit = 1 << clk->enable_bit;
	if (!(__raw_readl(other_reg) & bit))
		return;
	st_reg = (void __iomem *)(((u32)other_reg & ~0xf0) | 0x20); /* CM_IDLEST* */

	omap2_wait_clock_ready(st_reg, bit, clk->name);
}

static int omap2_dflt_clk_enable(struct clk *clk)
{
	u32 v;

	if (unlikely(clk->enable_reg == NULL)) {
		printk(KERN_ERR "clock.c: Enable for %s without enable code\n",
		       clk->name);
		return 0; /* REVISIT: -EINVAL */
	}

	v = __raw_readl(clk->enable_reg);
	if (clk->flags & INVERT_ENABLE)
		v &= ~(1 << clk->enable_bit);
	else
		v |= (1 << clk->enable_bit);
	__raw_writel(v, clk->enable_reg);
	v = __raw_readl(clk->enable_reg); /* OCP barrier */

	return 0;
}

static int omap2_dflt_clk_enable_wait(struct clk *clk)
{
	int ret;

	if (!clk->enable_reg) {
		printk(KERN_ERR "clock.c: Enable for %s without enable code\n",
		       clk->name);
		return 0; /* REVISIT: -EINVAL */
	}

	ret = omap2_dflt_clk_enable(clk);
	if (ret == 0)
		omap2_clk_wait_ready(clk);
	return ret;
}

static void omap2_dflt_clk_disable(struct clk *clk)
{
	u32 v;

	if (!clk->enable_reg) {
		/*
		 * 'Independent' here refers to a clock which is not
		 * controlled by its parent.
		 */
		printk(KERN_ERR "clock: clk_disable called on independent "
		       "clock %s which has no enable_reg\n", clk->name);
		return;
	}

	v = __raw_readl(clk->enable_reg);
	if (clk->flags & INVERT_ENABLE)
		v |= (1 << clk->enable_bit);
	else
		v &= ~(1 << clk->enable_bit);
	__raw_writel(v, clk->enable_reg);
	/* No OCP barrier needed here since it is a disable operation */
}

const struct clkops clkops_omap2_dflt_wait = {
	.enable		= omap2_dflt_clk_enable_wait,
	.disable	= omap2_dflt_clk_disable,
};

const struct clkops clkops_omap2_dflt = {
	.enable		= omap2_dflt_clk_enable,
	.disable	= omap2_dflt_clk_disable,
};

/* Enables clock without considering parent dependencies or use count
 * REVISIT: Maybe change this to use clk->enable like on omap1?
 */
static int _omap2_clk_enable(struct clk *clk)
{
	return clk->ops->enable(clk);
}

/* Disables clock without considering parent dependencies or use count */
static void _omap2_clk_disable(struct clk *clk)
{
	clk->ops->disable(clk);
}

void omap2_clk_disable(struct clk *clk)
{
	if (clk->usecount > 0 && !(--clk->usecount)) {
		_omap2_clk_disable(clk);
		if (clk->parent)
			omap2_clk_disable(clk->parent);
		if (clk->clkdm)
			omap2_clkdm_clk_disable(clk->clkdm, clk);

	}
}

int omap2_clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount++ == 0) {
		if (clk->clkdm)
			omap2_clkdm_clk_enable(clk->clkdm, clk);

		if (clk->parent) {
			ret = omap2_clk_enable(clk->parent);
			if (ret)
				goto err;
		}

		ret = _omap2_clk_enable(clk);
		if (ret) {
			if (clk->parent)
				omap2_clk_disable(clk->parent);

			goto err;
		}
	}
	return ret;

err:
	if (clk->clkdm)
		omap2_clkdm_clk_disable(clk->clkdm, clk);
	clk->usecount--;
	return ret;
}

/*
 * Used for clocks that are part of CLKSEL_xyz governed clocks.
 * REVISIT: Maybe change to use clk->enable() functions like on omap1?
 */
unsigned long omap2_clksel_recalc(struct clk *clk)
{
	unsigned long rate;
	u32 div = 0;

	pr_debug("clock: recalc'ing clksel clk %s\n", clk->name);

	div = omap2_clksel_get_divisor(clk);
	if (div == 0)
		return clk->rate;

	rate = clk->parent->rate / div;

	pr_debug("clock: new clock rate is %ld (div %d)\n", rate, div);

	return rate;
}

/**
 * omap2_get_clksel_by_parent - return clksel struct for a given clk & parent
 * @clk: OMAP struct clk ptr to inspect
 * @src_clk: OMAP struct clk ptr of the parent clk to search for
 *
 * Scan the struct clksel array associated with the clock to find
 * the element associated with the supplied parent clock address.
 * Returns a pointer to the struct clksel on success or NULL on error.
 */
static const struct clksel *omap2_get_clksel_by_parent(struct clk *clk,
						       struct clk *src_clk)
{
	const struct clksel *clks;

	if (!clk->clksel)
		return NULL;

	for (clks = clk->clksel; clks->parent; clks++) {
		if (clks->parent == src_clk)
			break; /* Found the requested parent */
	}

	if (!clks->parent) {
		printk(KERN_ERR "clock: Could not find parent clock %s in "
		       "clksel array of clock %s\n", src_clk->name,
		       clk->name);
		return NULL;
	}

	return clks;
}

/**
 * omap2_clksel_round_rate_div - find divisor for the given clock and rate
 * @clk: OMAP struct clk to use
 * @target_rate: desired clock rate
 * @new_div: ptr to where we should store the divisor
 *
 * Finds 'best' divider value in an array based on the source and target
 * rates.  The divider array must be sorted with smallest divider first.
 * Note that this will not work for clocks which are part of CONFIG_PARTICIPANT,
 * they are only settable as part of virtual_prcm set.
 *
 * Returns the rounded clock rate or returns 0xffffffff on error.
 */
u32 omap2_clksel_round_rate_div(struct clk *clk, unsigned long target_rate,
				u32 *new_div)
{
	unsigned long test_rate;
	const struct clksel *clks;
	const struct clksel_rate *clkr;
	u32 last_div = 0;

	printk(KERN_INFO "clock: clksel_round_rate_div: %s target_rate %ld\n",
	       clk->name, target_rate);

	*new_div = 1;

	clks = omap2_get_clksel_by_parent(clk, clk->parent);
	if (!clks)
		return ~0;

	for (clkr = clks->rates; clkr->div; clkr++) {
		if (!(clkr->flags & cpu_mask))
		    continue;

		/* Sanity check */
		if (clkr->div <= last_div)
			printk(KERN_ERR "clock: clksel_rate table not sorted "
			       "for clock %s", clk->name);

		last_div = clkr->div;

		test_rate = clk->parent->rate / clkr->div;

		if (test_rate <= target_rate)
			break; /* found it */
	}

	if (!clkr->div) {
		printk(KERN_ERR "clock: Could not find divisor for target "
		       "rate %ld for clock %s parent %s\n", target_rate,
		       clk->name, clk->parent->name);
		return ~0;
	}

	*new_div = clkr->div;

	printk(KERN_INFO "clock: new_div = %d, new_rate = %ld\n", *new_div,
	       (clk->parent->rate / clkr->div));

	return (clk->parent->rate / clkr->div);
}

/**
 * omap2_clksel_round_rate - find rounded rate for the given clock and rate
 * @clk: OMAP struct clk to use
 * @target_rate: desired clock rate
 *
 * Compatibility wrapper for OMAP clock framework
 * Finds best target rate based on the source clock and possible dividers.
 * rates. The divider array must be sorted with smallest divider first.
 * Note that this will not work for clocks which are part of CONFIG_PARTICIPANT,
 * they are only settable as part of virtual_prcm set.
 *
 * Returns the rounded clock rate or returns 0xffffffff on error.
 */
long omap2_clksel_round_rate(struct clk *clk, unsigned long target_rate)
{
	u32 new_div;

	return omap2_clksel_round_rate_div(clk, target_rate, &new_div);
}


/* Given a clock and a rate apply a clock specific rounding function */
long omap2_clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk->round_rate)
		return clk->round_rate(clk, rate);

	if (clk->flags & RATE_FIXED)
		printk(KERN_ERR "clock: generic omap2_clk_round_rate called "
		       "on fixed-rate clock %s\n", clk->name);

	return clk->rate;
}

/**
 * omap2_clksel_to_divisor() - turn clksel field value into integer divider
 * @clk: OMAP struct clk to use
 * @field_val: register field value to find
 *
 * Given a struct clk of a rate-selectable clksel clock, and a register field
 * value to search for, find the corresponding clock divisor.  The register
 * field value should be pre-masked and shifted down so the LSB is at bit 0
 * before calling.  Returns 0 on error
 */
u32 omap2_clksel_to_divisor(struct clk *clk, u32 field_val)
{
	const struct clksel *clks;
	const struct clksel_rate *clkr;

	clks = omap2_get_clksel_by_parent(clk, clk->parent);
	if (!clks)
		return 0;

	for (clkr = clks->rates; clkr->div; clkr++) {
		if ((clkr->flags & cpu_mask) && (clkr->val == field_val))
			break;
	}

	if (!clkr->div) {
		printk(KERN_ERR "clock: Could not find fieldval %d for "
		       "clock %s parent %s\n", field_val, clk->name,
		       clk->parent->name);
		return 0;
	}

	return clkr->div;
}

/**
 * omap2_divisor_to_clksel() - turn clksel integer divisor into a field value
 * @clk: OMAP struct clk to use
 * @div: integer divisor to search for
 *
 * Given a struct clk of a rate-selectable clksel clock, and a clock divisor,
 * find the corresponding register field value.  The return register value is
 * the value before left-shifting.  Returns ~0 on error
 */
u32 omap2_divisor_to_clksel(struct clk *clk, u32 div)
{
	const struct clksel *clks;
	const struct clksel_rate *clkr;

	/* should never happen */
	WARN_ON(div == 0);

	clks = omap2_get_clksel_by_parent(clk, clk->parent);
	if (!clks)
		return ~0;

	for (clkr = clks->rates; clkr->div; clkr++) {
		if ((clkr->flags & cpu_mask) && (clkr->div == div))
			break;
	}

	if (!clkr->div) {
		printk(KERN_ERR "clock: Could not find divisor %d for "
		       "clock %s parent %s\n", div, clk->name,
		       clk->parent->name);
		return ~0;
	}

	return clkr->val;
}

/**
 * omap2_clksel_get_divisor - get current divider applied to parent clock.
 * @clk: OMAP struct clk to use.
 *
 * Returns the integer divisor upon success or 0 on error.
 */
u32 omap2_clksel_get_divisor(struct clk *clk)
{
	u32 v;

	if (!clk->clksel_mask)
		return 0;

	v = __raw_readl(clk->clksel_reg) & clk->clksel_mask;
	v >>= __ffs(clk->clksel_mask);

	return omap2_clksel_to_divisor(clk, v);
}

int omap2_clksel_set_rate(struct clk *clk, unsigned long rate)
{
	u32 v, field_val, validrate, new_div = 0;

	if (!clk->clksel_mask)
		return -EINVAL;

	validrate = omap2_clksel_round_rate_div(clk, rate, &new_div);
	if (validrate != rate)
		return -EINVAL;

	field_val = omap2_divisor_to_clksel(clk, new_div);
	if (field_val == ~0)
		return -EINVAL;

	v = __raw_readl(clk->clksel_reg);
	v &= ~clk->clksel_mask;
	v |= field_val << __ffs(clk->clksel_mask);
	__raw_writel(v, clk->clksel_reg);
	v = __raw_readl(clk->clksel_reg); /* OCP barrier */

	clk->rate = clk->parent->rate / new_div;

	_omap2xxx_clk_commit(clk);

	return 0;
}


/* Set the clock rate for a clock source */
int omap2_clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	pr_debug("clock: set_rate for clock %s to rate %ld\n", clk->name, rate);

	/* CONFIG_PARTICIPANT clocks are changed only in sets via the
	   rate table mechanism, driven by mpu_speed  */
	if (clk->flags & CONFIG_PARTICIPANT)
		return -EINVAL;

	/* dpll_ck, core_ck, virt_prcm_set; plus all clksel clocks */
	if (clk->set_rate)
		ret = clk->set_rate(clk, rate);

	return ret;
}

/*
 * Converts encoded control register address into a full address
 * On error, the return value (parent_div) will be 0.
 */
static u32 _omap2_clksel_get_src_field(struct clk *src_clk, struct clk *clk,
				       u32 *field_val)
{
	const struct clksel *clks;
	const struct clksel_rate *clkr;

	clks = omap2_get_clksel_by_parent(clk, src_clk);
	if (!clks)
		return 0;

	for (clkr = clks->rates; clkr->div; clkr++) {
		if (clkr->flags & cpu_mask && clkr->flags & DEFAULT_RATE)
			break; /* Found the default rate for this platform */
	}

	if (!clkr->div) {
		printk(KERN_ERR "clock: Could not find default rate for "
		       "clock %s parent %s\n", clk->name,
		       src_clk->parent->name);
		return 0;
	}

	/* Should never happen.  Add a clksel mask to the struct clk. */
	WARN_ON(clk->clksel_mask == 0);

	*field_val = clkr->val;

	return clkr->div;
}

int omap2_clk_set_parent(struct clk *clk, struct clk *new_parent)
{
	u32 field_val, v, parent_div;

	if (clk->flags & CONFIG_PARTICIPANT)
		return -EINVAL;

	if (!clk->clksel)
		return -EINVAL;

	parent_div = _omap2_clksel_get_src_field(new_parent, clk, &field_val);
	if (!parent_div)
		return -EINVAL;

	/* Set new source value (previous dividers if any in effect) */
	v = __raw_readl(clk->clksel_reg);
	v &= ~clk->clksel_mask;
	v |= field_val << __ffs(clk->clksel_mask);
	__raw_writel(v, clk->clksel_reg);
	v = __raw_readl(clk->clksel_reg);    /* OCP barrier */

	_omap2xxx_clk_commit(clk);

	clk_reparent(clk, new_parent);

	/* CLKSEL clocks follow their parents' rates, divided by a divisor */
	clk->rate = new_parent->rate;

	if (parent_div > 0)
		clk->rate /= parent_div;

	pr_debug("clock: set parent of %s to %s (new rate %ld)\n",
		 clk->name, clk->parent->name, clk->rate);

	return 0;
}

/* DPLL rate rounding code */

/**
 * omap2_dpll_set_rate_tolerance: set the error tolerance during rate rounding
 * @clk: struct clk * of the DPLL
 * @tolerance: maximum rate error tolerance
 *
 * Set the maximum DPLL rate error tolerance for the rate rounding
 * algorithm.  The rate tolerance is an attempt to balance DPLL power
 * saving (the least divider value "n") vs. rate fidelity (the least
 * difference between the desired DPLL target rate and the rounded
 * rate out of the algorithm).  So, increasing the tolerance is likely
 * to decrease DPLL power consumption and increase DPLL rate error.
 * Returns -EINVAL if provided a null clock ptr or a clk that is not a
 * DPLL; or 0 upon success.
 */
int omap2_dpll_set_rate_tolerance(struct clk *clk, unsigned int tolerance)
{
	if (!clk || !clk->dpll_data)
		return -EINVAL;

	clk->dpll_data->rate_tolerance = tolerance;

	return 0;
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
 * omap2_dpll_round_rate - round a target rate for an OMAP DPLL
 * @clk: struct clk * for a DPLL
 * @target_rate: desired DPLL clock rate
 *
 * Given a DPLL, a desired target rate, and a rate tolerance, round
 * the target rate to a possible, programmable rate for this DPLL.
 * Rate tolerance is assumed to be set by the caller before this
 * function is called.  Attempts to select the minimum possible n
 * within the tolerance to reduce power consumption.  Stores the
 * computed (m, n) in the DPLL's dpll_data structure so set_rate()
 * will not need to call this (expensive) function again.  Returns ~0
 * if the target rate cannot be rounded, either because the rate is
 * too low or because the rate tolerance is set too tightly; or the
 * rounded rate upon success.
 */
long omap2_dpll_round_rate(struct clk *clk, unsigned long target_rate)
{
	int m, n, r, e, scaled_max_m;
	unsigned long scaled_rt_rp, new_rate;
	int min_e = -1, min_e_m = -1, min_e_n = -1;
	struct dpll_data *dd;

	if (!clk || !clk->dpll_data)
		return ~0;

	dd = clk->dpll_data;

	pr_debug("clock: starting DPLL round_rate for clock %s, target rate "
		 "%ld\n", clk->name, target_rate);

	scaled_rt_rp = target_rate / (dd->clk_ref->rate / DPLL_SCALE_FACTOR);
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
				    dd->clk_ref->rate);

		/* m can't be set low enough for this n - try with a larger n */
		if (r == DPLL_MULT_UNDERFLOW)
			continue;

		e = target_rate - new_rate;
		pr_debug("clock: n = %d: m = %d: rate error is %d "
			 "(new_rate = %ld)\n", n, m, e, new_rate);

		if (min_e == -1 ||
		    min_e >= (int)(abs(e) - dd->rate_tolerance)) {
			min_e = e;
			min_e_m = m;
			min_e_n = n;

			pr_debug("clock: found new least error %d\n", min_e);

			/* We found good settings -- bail out now */
			if (min_e <= dd->rate_tolerance)
				break;
		}
	}

	if (min_e < 0) {
		pr_debug("clock: error: target rate or tolerance too low\n");
		return ~0;
	}

	dd->last_rounded_m = min_e_m;
	dd->last_rounded_n = min_e_n;
	dd->last_rounded_rate = _dpll_compute_new_rate(dd->clk_ref->rate,
						       min_e_m,  min_e_n);

	pr_debug("clock: final least error: e = %d, m = %d, n = %d\n",
		 min_e, min_e_m, min_e_n);
	pr_debug("clock: final rate: %ld  (target rate: %ld)\n",
		 dd->last_rounded_rate, target_rate);

	return dd->last_rounded_rate;
}

/*-------------------------------------------------------------------------
 * Omap2 clock reset and init functions
 *-------------------------------------------------------------------------*/

#ifdef CONFIG_OMAP_RESET_CLOCKS
void omap2_clk_disable_unused(struct clk *clk)
{
	u32 regval32, v;

	v = (clk->flags & INVERT_ENABLE) ? (1 << clk->enable_bit) : 0;

	regval32 = __raw_readl(clk->enable_reg);
	if ((regval32 & (1 << clk->enable_bit)) == v)
		return;

	printk(KERN_INFO "Disabling unused clock \"%s\"\n", clk->name);
	if (cpu_is_omap34xx()) {
		omap2_clk_enable(clk);
		omap2_clk_disable(clk);
	} else
		_omap2_clk_disable(clk);
}
#endif
