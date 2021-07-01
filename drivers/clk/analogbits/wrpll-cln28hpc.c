// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019 SiFive, Inc.
 * Wesley Terpstra
 * Paul Walmsley
 *
 * This library supports configuration parsing and reprogramming of
 * the CLN28HPC variant of the Analog Bits Wide Range PLL.  The
 * intention is for this library to be reusable for any device that
 * integrates this PLL; thus the register structure and programming
 * details are expected to be provided by a separate IP block driver.
 *
 * The bulk of this code is primarily useful for clock configurations
 * that must operate at arbitrary rates, as opposed to clock configurations
 * that are restricted by software or manufacturer guidance to a small,
 * pre-determined set of performance points.
 *
 * References:
 * - Analog Bits "Wide Range PLL Datasheet", version 2015.10.01
 * - SiFive FU540-C000 Manual v1p0, Chapter 7 "Clocking and Reset"
 *   https://static.dev.sifive.com/FU540-C000-v1.0.pdf
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/limits.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/math.h>
#include <linux/minmax.h>

#include <linux/clk/analogbits-wrpll-cln28hpc.h>

/* MIN_INPUT_FREQ: minimum input clock frequency, in Hz (Fref_min) */
#define MIN_INPUT_FREQ			7000000

/* MAX_INPUT_FREQ: maximum input clock frequency, in Hz (Fref_max) */
#define MAX_INPUT_FREQ			600000000

/* MIN_POST_DIVIDE_REF_FREQ: minimum post-divider reference frequency, in Hz */
#define MIN_POST_DIVR_FREQ		7000000

/* MAX_POST_DIVIDE_REF_FREQ: maximum post-divider reference frequency, in Hz */
#define MAX_POST_DIVR_FREQ		200000000

/* MIN_VCO_FREQ: minimum VCO frequency, in Hz (Fvco_min) */
#define MIN_VCO_FREQ			2400000000UL

/* MAX_VCO_FREQ: maximum VCO frequency, in Hz (Fvco_max) */
#define MAX_VCO_FREQ			4800000000ULL

/* MAX_DIVQ_DIVISOR: maximum output divisor.  Selected by DIVQ = 6 */
#define MAX_DIVQ_DIVISOR		64

/* MAX_DIVR_DIVISOR: maximum reference divisor.  Selected by DIVR = 63 */
#define MAX_DIVR_DIVISOR		64

/* MAX_LOCK_US: maximum PLL lock time, in microseconds (tLOCK_max) */
#define MAX_LOCK_US			70

/*
 * ROUND_SHIFT: number of bits to shift to avoid precision loss in the rounding
 *              algorithm
 */
#define ROUND_SHIFT			20

/*
 * Private functions
 */

/**
 * __wrpll_calc_filter_range() - determine PLL loop filter bandwidth
 * @post_divr_freq: input clock rate after the R divider
 *
 * Select the value to be presented to the PLL RANGE input signals, based
 * on the input clock frequency after the post-R-divider @post_divr_freq.
 * This code follows the recommendations in the PLL datasheet for filter
 * range selection.
 *
 * Return: The RANGE value to be presented to the PLL configuration inputs,
 *         or a negative return code upon error.
 */
static int __wrpll_calc_filter_range(unsigned long post_divr_freq)
{
	if (post_divr_freq < MIN_POST_DIVR_FREQ ||
	    post_divr_freq > MAX_POST_DIVR_FREQ) {
		WARN(1, "%s: post-divider reference freq out of range: %lu",
		     __func__, post_divr_freq);
		return -ERANGE;
	}

	switch (post_divr_freq) {
	case 0 ... 10999999:
		return 1;
	case 11000000 ... 17999999:
		return 2;
	case 18000000 ... 29999999:
		return 3;
	case 30000000 ... 49999999:
		return 4;
	case 50000000 ... 79999999:
		return 5;
	case 80000000 ... 129999999:
		return 6;
	}

	return 7;
}

/**
 * __wrpll_calc_fbdiv() - return feedback fixed divide value
 * @c: ptr to a struct wrpll_cfg record to read from
 *
 * The internal feedback path includes a fixed by-two divider; the
 * external feedback path does not.  Return the appropriate divider
 * value (2 or 1) depending on whether internal or external feedback
 * is enabled.  This code doesn't test for invalid configurations
 * (e.g. both or neither of WRPLL_FLAGS_*_FEEDBACK are set); it relies
 * on the caller to do so.
 *
 * Context: Any context.  Caller must protect the memory pointed to by
 *          @c from simultaneous modification.
 *
 * Return: 2 if internal feedback is enabled or 1 if external feedback
 *         is enabled.
 */
static u8 __wrpll_calc_fbdiv(const struct wrpll_cfg *c)
{
	return (c->flags & WRPLL_FLAGS_INT_FEEDBACK_MASK) ? 2 : 1;
}

/**
 * __wrpll_calc_divq() - determine DIVQ based on target PLL output clock rate
 * @target_rate: target PLL output clock rate
 * @vco_rate: pointer to a u64 to store the computed VCO rate into
 *
 * Determine a reasonable value for the PLL Q post-divider, based on the
 * target output rate @target_rate for the PLL.  Along with returning the
 * computed Q divider value as the return value, this function stores the
 * desired target VCO rate into the variable pointed to by @vco_rate.
 *
 * Context: Any context.  Caller must protect the memory pointed to by
 *          @vco_rate from simultaneous access or modification.
 *
 * Return: a positive integer DIVQ value to be programmed into the hardware
 *         upon success, or 0 upon error (since 0 is an invalid DIVQ value)
 */
static u8 __wrpll_calc_divq(u32 target_rate, u64 *vco_rate)
{
	u64 s;
	u8 divq = 0;

	if (!vco_rate) {
		WARN_ON(1);
		goto wcd_out;
	}

	s = div_u64(MAX_VCO_FREQ, target_rate);
	if (s <= 1) {
		divq = 1;
		*vco_rate = MAX_VCO_FREQ;
	} else if (s > MAX_DIVQ_DIVISOR) {
		divq = ilog2(MAX_DIVQ_DIVISOR);
		*vco_rate = MIN_VCO_FREQ;
	} else {
		divq = ilog2(s);
		*vco_rate = (u64)target_rate << divq;
	}

wcd_out:
	return divq;
}

/**
 * __wrpll_update_parent_rate() - update PLL data when parent rate changes
 * @c: ptr to a struct wrpll_cfg record to write PLL data to
 * @parent_rate: PLL input refclk rate (pre-R-divider)
 *
 * Pre-compute some data used by the PLL configuration algorithm when
 * the PLL's reference clock rate changes.  The intention is to avoid
 * computation when the parent rate remains constant - expected to be
 * the common case.
 *
 * Returns: 0 upon success or -ERANGE if the reference clock rate is
 * out of range.
 */
static int __wrpll_update_parent_rate(struct wrpll_cfg *c,
				      unsigned long parent_rate)
{
	u8 max_r_for_parent;

	if (parent_rate > MAX_INPUT_FREQ || parent_rate < MIN_POST_DIVR_FREQ)
		return -ERANGE;

	c->parent_rate = parent_rate;
	max_r_for_parent = div_u64(parent_rate, MIN_POST_DIVR_FREQ);
	c->max_r = min_t(u8, MAX_DIVR_DIVISOR, max_r_for_parent);

	c->init_r = DIV_ROUND_UP_ULL(parent_rate, MAX_POST_DIVR_FREQ);

	return 0;
}

/**
 * wrpll_configure() - compute PLL configuration for a target rate
 * @c: ptr to a struct wrpll_cfg record to write into
 * @target_rate: target PLL output clock rate (post-Q-divider)
 * @parent_rate: PLL input refclk rate (pre-R-divider)
 *
 * Compute the appropriate PLL signal configuration values and store
 * in PLL context @c.  PLL reprogramming is not glitchless, so the
 * caller should switch any downstream logic to a different clock
 * source or clock-gate it before presenting these values to the PLL
 * configuration signals.
 *
 * The caller must pass this function a pre-initialized struct
 * wrpll_cfg record: either initialized to zero (with the
 * exception of the .name and .flags fields) or read from the PLL.
 *
 * Context: Any context.  Caller must protect the memory pointed to by @c
 *          from simultaneous access or modification.
 *
 * Return: 0 upon success; anything else upon failure.
 */
int wrpll_configure_for_rate(struct wrpll_cfg *c, u32 target_rate,
			     unsigned long parent_rate)
{
	unsigned long ratio;
	u64 target_vco_rate, delta, best_delta, f_pre_div, vco, vco_pre;
	u32 best_f, f, post_divr_freq;
	u8 fbdiv, divq, best_r, r;
	int range;

	if (c->flags == 0) {
		WARN(1, "%s called with uninitialized PLL config", __func__);
		return -EINVAL;
	}

	/* Initialize rounding data if it hasn't been initialized already */
	if (parent_rate != c->parent_rate) {
		if (__wrpll_update_parent_rate(c, parent_rate)) {
			pr_err("%s: PLL input rate is out of range\n",
			       __func__);
			return -ERANGE;
		}
	}

	c->flags &= ~WRPLL_FLAGS_RESET_MASK;

	/* Put the PLL into bypass if the user requests the parent clock rate */
	if (target_rate == parent_rate) {
		c->flags |= WRPLL_FLAGS_BYPASS_MASK;
		return 0;
	}

	c->flags &= ~WRPLL_FLAGS_BYPASS_MASK;

	/* Calculate the Q shift and target VCO rate */
	divq = __wrpll_calc_divq(target_rate, &target_vco_rate);
	if (!divq)
		return -1;
	c->divq = divq;

	/* Precalculate the pre-Q divider target ratio */
	ratio = div64_u64((target_vco_rate << ROUND_SHIFT), parent_rate);

	fbdiv = __wrpll_calc_fbdiv(c);
	best_r = 0;
	best_f = 0;
	best_delta = MAX_VCO_FREQ;

	/*
	 * Consider all values for R which land within
	 * [MIN_POST_DIVR_FREQ, MAX_POST_DIVR_FREQ]; prefer smaller R
	 */
	for (r = c->init_r; r <= c->max_r; ++r) {
		f_pre_div = ratio * r;
		f = (f_pre_div + (1 << ROUND_SHIFT)) >> ROUND_SHIFT;
		f >>= (fbdiv - 1);

		post_divr_freq = div_u64(parent_rate, r);
		vco_pre = fbdiv * post_divr_freq;
		vco = vco_pre * f;

		/* Ensure rounding didn't take us out of range */
		if (vco > target_vco_rate) {
			--f;
			vco = vco_pre * f;
		} else if (vco < MIN_VCO_FREQ) {
			++f;
			vco = vco_pre * f;
		}

		delta = abs(target_rate - vco);
		if (delta < best_delta) {
			best_delta = delta;
			best_r = r;
			best_f = f;
		}
	}

	c->divr = best_r - 1;
	c->divf = best_f - 1;

	post_divr_freq = div_u64(parent_rate, best_r);

	/* Pick the best PLL jitter filter */
	range = __wrpll_calc_filter_range(post_divr_freq);
	if (range < 0)
		return range;
	c->range = range;

	return 0;
}

/**
 * wrpll_calc_output_rate() - calculate the PLL's target output rate
 * @c: ptr to a struct wrpll_cfg record to read from
 * @parent_rate: PLL refclk rate
 *
 * Given a pointer to the PLL's current input configuration @c and the
 * PLL's input reference clock rate @parent_rate (before the R
 * pre-divider), calculate the PLL's output clock rate (after the Q
 * post-divider).
 *
 * Context: Any context.  Caller must protect the memory pointed to by @c
 *          from simultaneous modification.
 *
 * Return: the PLL's output clock rate, in Hz.  The return value from
 *         this function is intended to be convenient to pass directly
 *         to the Linux clock framework; thus there is no explicit
 *         error return value.
 */
unsigned long wrpll_calc_output_rate(const struct wrpll_cfg *c,
				     unsigned long parent_rate)
{
	u8 fbdiv;
	u64 n;

	if (c->flags & WRPLL_FLAGS_EXT_FEEDBACK_MASK) {
		WARN(1, "external feedback mode not yet supported");
		return ULONG_MAX;
	}

	fbdiv = __wrpll_calc_fbdiv(c);
	n = parent_rate * fbdiv * (c->divf + 1);
	n = div_u64(n, c->divr + 1);
	n >>= c->divq;

	return n;
}

/**
 * wrpll_calc_max_lock_us() - return the time for the PLL to lock
 * @c: ptr to a struct wrpll_cfg record to read from
 *
 * Return the minimum amount of time (in microseconds) that the caller
 * must wait after reprogramming the PLL to ensure that it is locked
 * to the input frequency and stable.  This is likely to depend on the DIVR
 * value; this is under discussion with the manufacturer.
 *
 * Return: the minimum amount of time the caller must wait for the PLL
 *         to lock (in microseconds)
 */
unsigned int wrpll_calc_max_lock_us(const struct wrpll_cfg *c)
{
	return MAX_LOCK_US;
}
