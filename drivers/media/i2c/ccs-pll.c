// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/media/i2c/ccs-pll.c
 *
 * Generic MIPI CCS/SMIA/SMIA++ PLL calculator
 *
 * Copyright (C) 2020 Intel Corporation
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/gcd.h>
#include <linux/lcm.h>
#include <linux/module.h>

#include "ccs-pll.h"

/* Return an even number or one. */
static inline uint32_t clk_div_even(uint32_t a)
{
	return max_t(uint32_t, 1, a & ~1);
}

/* Return an even number or one. */
static inline uint32_t clk_div_even_up(uint32_t a)
{
	if (a == 1)
		return 1;
	return (a + 1) & ~1;
}

static inline uint32_t is_one_or_even(uint32_t a)
{
	if (a == 1)
		return 1;
	if (a & 1)
		return 0;

	return 1;
}

static inline uint32_t one_or_more(uint32_t a)
{
	return a ?: 1;
}

static int bounds_check(struct device *dev, uint32_t val,
			uint32_t min, uint32_t max, char *str)
{
	if (val >= min && val <= max)
		return 0;

	dev_dbg(dev, "%s out of bounds: %d (%d--%d)\n", str, val, min, max);

	return -EINVAL;
}

static void print_pll(struct device *dev, struct ccs_pll *pll)
{
	dev_dbg(dev, "pre_pll_clk_div\t%u\n",  pll->vt_fr.pre_pll_clk_div);
	dev_dbg(dev, "pll_multiplier \t%u\n",  pll->vt_fr.pll_multiplier);
	if (!(pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS)) {
		dev_dbg(dev, "op_sys_clk_div \t%u\n", pll->op_bk.sys_clk_div);
		dev_dbg(dev, "op_pix_clk_div \t%u\n", pll->op_bk.pix_clk_div);
	}
	dev_dbg(dev, "vt_sys_clk_div \t%u\n",  pll->vt_bk.sys_clk_div);
	dev_dbg(dev, "vt_pix_clk_div \t%u\n",  pll->vt_bk.pix_clk_div);

	dev_dbg(dev, "ext_clk_freq_hz \t%u\n", pll->ext_clk_freq_hz);
	dev_dbg(dev, "pll_ip_clk_freq_hz \t%u\n", pll->vt_fr.pll_ip_clk_freq_hz);
	dev_dbg(dev, "pll_op_clk_freq_hz \t%u\n", pll->vt_fr.pll_op_clk_freq_hz);
	if (!(pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS)) {
		dev_dbg(dev, "op_sys_clk_freq_hz \t%u\n",
			pll->op_bk.sys_clk_freq_hz);
		dev_dbg(dev, "op_pix_clk_freq_hz \t%u\n",
			pll->op_bk.pix_clk_freq_hz);
	}
	dev_dbg(dev, "vt_sys_clk_freq_hz \t%u\n", pll->vt_bk.sys_clk_freq_hz);
	dev_dbg(dev, "vt_pix_clk_freq_hz \t%u\n", pll->vt_bk.pix_clk_freq_hz);
}

static int check_all_bounds(struct device *dev,
			    const struct ccs_pll_limits *lim,
			    const struct ccs_pll_branch_limits_fr *op_lim_fr,
			    const struct ccs_pll_branch_limits_bk *op_lim_bk,
			    struct ccs_pll *pll,
			    struct ccs_pll_branch_fr *op_pll_fr,
			    struct ccs_pll_branch_bk *op_pll_bk)
{
	int rval;

	rval = bounds_check(dev, op_pll_fr->pll_ip_clk_freq_hz,
			    op_lim_fr->min_pll_ip_clk_freq_hz,
			    op_lim_fr->max_pll_ip_clk_freq_hz,
			    "pll_ip_clk_freq_hz");
	if (!rval)
		rval = bounds_check(
			dev, op_pll_fr->pll_multiplier,
			op_lim_fr->min_pll_multiplier,
			op_lim_fr->max_pll_multiplier, "pll_multiplier");
	if (!rval)
		rval = bounds_check(
			dev, op_pll_fr->pll_op_clk_freq_hz,
			op_lim_fr->min_pll_op_clk_freq_hz,
			op_lim_fr->max_pll_op_clk_freq_hz, "pll_op_clk_freq_hz");
	if (!rval)
		rval = bounds_check(
			dev, op_pll_bk->sys_clk_div,
			op_lim_bk->min_sys_clk_div, op_lim_bk->max_sys_clk_div,
			"op_sys_clk_div");
	if (!rval)
		rval = bounds_check(
			dev, op_pll_bk->sys_clk_freq_hz,
			op_lim_bk->min_sys_clk_freq_hz,
			op_lim_bk->max_sys_clk_freq_hz,
			"op_sys_clk_freq_hz");
	if (!rval)
		rval = bounds_check(
			dev, op_pll_bk->pix_clk_freq_hz,
			op_lim_bk->min_pix_clk_freq_hz,
			op_lim_bk->max_pix_clk_freq_hz,
			"op_pix_clk_freq_hz");

	/*
	 * If there are no OP clocks, the VT clocks are contained in
	 * the OP clock struct.
	 */
	if (pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS)
		return rval;

	if (!rval)
		rval = bounds_check(
			dev, pll->vt_bk.sys_clk_freq_hz,
			lim->vt_bk.min_sys_clk_freq_hz,
			lim->vt_bk.max_sys_clk_freq_hz,
			"vt_sys_clk_freq_hz");
	if (!rval)
		rval = bounds_check(
			dev, pll->vt_bk.pix_clk_freq_hz,
			lim->vt_bk.min_pix_clk_freq_hz,
			lim->vt_bk.max_pix_clk_freq_hz,
			"vt_pix_clk_freq_hz");

	return rval;
}

/*
 * Heuristically guess the PLL tree for a given common multiplier and
 * divisor. Begin with the operational timing and continue to video
 * timing once operational timing has been verified.
 *
 * @mul is the PLL multiplier and @div is the common divisor
 * (pre_pll_clk_div and op_sys_clk_div combined). The final PLL
 * multiplier will be a multiple of @mul.
 *
 * @return Zero on success, error code on error.
 */
static int
__ccs_pll_calculate(struct device *dev, const struct ccs_pll_limits *lim,
		    const struct ccs_pll_branch_limits_fr *op_lim_fr,
		    const struct ccs_pll_branch_limits_bk *op_lim_bk,
		    struct ccs_pll *pll, struct ccs_pll_branch_fr *op_pll_fr,
		    struct ccs_pll_branch_bk *op_pll_bk, uint32_t mul,
		    uint32_t div)
{
	uint32_t sys_div;
	uint32_t best_pix_div = INT_MAX >> 1;
	uint32_t vt_op_binning_div;
	/*
	 * Higher multipliers (and divisors) are often required than
	 * necessitated by the external clock and the output clocks.
	 * There are limits for all values in the clock tree. These
	 * are the minimum and maximum multiplier for mul.
	 */
	uint32_t more_mul_min, more_mul_max;
	uint32_t more_mul_factor;
	uint32_t min_vt_div, max_vt_div, vt_div;
	uint32_t min_sys_div, max_sys_div;
	uint32_t i;

	/*
	 * Get pre_pll_clk_div so that our pll_op_clk_freq_hz won't be
	 * too high.
	 */
	dev_dbg(dev, "op_pre_pll_clk_div %u\n", op_pll_fr->pre_pll_clk_div);

	/* Don't go above max pll multiplier. */
	more_mul_max = op_lim_fr->max_pll_multiplier / mul;
	dev_dbg(dev, "more_mul_max: max_op_pll_multiplier check: %u\n",
		more_mul_max);
	/* Don't go above max pll op frequency. */
	more_mul_max =
		min_t(uint32_t,
		      more_mul_max,
		      op_lim_fr->max_pll_op_clk_freq_hz
		      / (pll->ext_clk_freq_hz /
			 op_pll_fr->pre_pll_clk_div * mul));
	dev_dbg(dev, "more_mul_max: max_pll_op_clk_freq_hz check: %u\n",
		more_mul_max);
	/* Don't go above the division capability of op sys clock divider. */
	more_mul_max = min(more_mul_max,
			   op_lim_bk->max_sys_clk_div * op_pll_fr->pre_pll_clk_div
			   / div);
	dev_dbg(dev, "more_mul_max: max_op_sys_clk_div check: %u\n",
		more_mul_max);
	/* Ensure we won't go above max_pll_multiplier. */
	more_mul_max = min(more_mul_max, op_lim_fr->max_pll_multiplier / mul);
	dev_dbg(dev, "more_mul_max: min_pll_multiplier check: %u\n",
		more_mul_max);

	/* Ensure we won't go below min_pll_op_clk_freq_hz. */
	more_mul_min = DIV_ROUND_UP(op_lim_fr->min_pll_op_clk_freq_hz,
				    pll->ext_clk_freq_hz /
				    op_pll_fr->pre_pll_clk_div * mul);
	dev_dbg(dev, "more_mul_min: min_op_pll_op_clk_freq_hz check: %u\n",
		more_mul_min);
	/* Ensure we won't go below min_pll_multiplier. */
	more_mul_min = max(more_mul_min,
			   DIV_ROUND_UP(op_lim_fr->min_pll_multiplier, mul));
	dev_dbg(dev, "more_mul_min: min_op_pll_multiplier check: %u\n",
		more_mul_min);

	if (more_mul_min > more_mul_max) {
		dev_dbg(dev,
			"unable to compute more_mul_min and more_mul_max\n");
		return -EINVAL;
	}

	more_mul_factor = lcm(div, op_pll_fr->pre_pll_clk_div) / div;
	dev_dbg(dev, "more_mul_factor: %u\n", more_mul_factor);
	more_mul_factor = lcm(more_mul_factor, op_lim_bk->min_sys_clk_div);
	dev_dbg(dev, "more_mul_factor: min_op_sys_clk_div: %d\n",
		more_mul_factor);
	i = roundup(more_mul_min, more_mul_factor);
	if (!is_one_or_even(i))
		i <<= 1;

	dev_dbg(dev, "final more_mul: %u\n", i);
	if (i > more_mul_max) {
		dev_dbg(dev, "final more_mul is bad, max %u\n", more_mul_max);
		return -EINVAL;
	}

	op_pll_fr->pll_multiplier = mul * i;
	op_pll_bk->sys_clk_div = div * i / op_pll_fr->pre_pll_clk_div;
	dev_dbg(dev, "op_sys_clk_div: %u\n", op_pll_bk->sys_clk_div);

	op_pll_fr->pll_ip_clk_freq_hz = pll->ext_clk_freq_hz
		/ op_pll_fr->pre_pll_clk_div;

	op_pll_fr->pll_op_clk_freq_hz = op_pll_fr->pll_ip_clk_freq_hz
		* op_pll_fr->pll_multiplier;

	op_pll_bk->pix_clk_div = pll->bits_per_pixel
		* pll->op_lanes / pll->csi2.lanes;
	op_pll_bk->pix_clk_freq_hz =
		op_pll_bk->sys_clk_freq_hz / op_pll_bk->pix_clk_div;
	dev_dbg(dev, "op_pix_clk_div: %u\n", op_pll_bk->pix_clk_div);

	if (pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS) {
		/* No OP clocks --- VT clocks are used instead. */
		goto out_skip_vt_calc;
	}

	/*
	 * Some sensors perform analogue binning and some do this
	 * digitally. The ones doing this digitally can be roughly be
	 * found out using this formula. The ones doing this digitally
	 * should run at higher clock rate, so smaller divisor is used
	 * on video timing side.
	 */
	if (lim->min_line_length_pck_bin > lim->min_line_length_pck
	    / pll->binning_horizontal)
		vt_op_binning_div = pll->binning_horizontal;
	else
		vt_op_binning_div = 1;
	dev_dbg(dev, "vt_op_binning_div: %u\n", vt_op_binning_div);

	/*
	 * Profile 2 supports vt_pix_clk_div E [4, 10]
	 *
	 * Horizontal binning can be used as a base for difference in
	 * divisors. One must make sure that horizontal blanking is
	 * enough to accommodate the CSI-2 sync codes.
	 *
	 * Take scaling factor and number of VT lanes into account as well.
	 *
	 * Find absolute limits for the factor of vt divider.
	 */
	dev_dbg(dev, "scale_m: %u\n", pll->scale_m);
	min_vt_div = DIV_ROUND_UP(op_pll_bk->pix_clk_div
				  * op_pll_bk->sys_clk_div * pll->scale_n
				  * pll->vt_lanes,
				  pll->op_lanes * vt_op_binning_div
				  * pll->scale_m);

	/* Find smallest and biggest allowed vt divisor. */
	dev_dbg(dev, "min_vt_div: %u\n", min_vt_div);
	min_vt_div = max(min_vt_div,
			 DIV_ROUND_UP(op_pll_fr->pll_op_clk_freq_hz,
				      lim->vt_bk.max_pix_clk_freq_hz));
	dev_dbg(dev, "min_vt_div: max_vt_pix_clk_freq_hz: %u\n",
		min_vt_div);
	min_vt_div = max_t(uint32_t, min_vt_div,
			   lim->vt_bk.min_pix_clk_div
			   * lim->vt_bk.min_sys_clk_div);
	dev_dbg(dev, "min_vt_div: min_vt_clk_div: %u\n", min_vt_div);

	max_vt_div = lim->vt_bk.max_sys_clk_div * lim->vt_bk.max_pix_clk_div;
	dev_dbg(dev, "max_vt_div: %u\n", max_vt_div);
	max_vt_div = min(max_vt_div,
			 DIV_ROUND_UP(op_pll_fr->pll_op_clk_freq_hz,
				      lim->vt_bk.min_pix_clk_freq_hz));
	dev_dbg(dev, "max_vt_div: min_vt_pix_clk_freq_hz: %u\n",
		max_vt_div);

	/*
	 * Find limitsits for sys_clk_div. Not all values are possible
	 * with all values of pix_clk_div.
	 */
	min_sys_div = lim->vt_bk.min_sys_clk_div;
	dev_dbg(dev, "min_sys_div: %u\n", min_sys_div);
	min_sys_div = max(min_sys_div,
			  DIV_ROUND_UP(min_vt_div,
				       lim->vt_bk.max_pix_clk_div));
	dev_dbg(dev, "min_sys_div: max_vt_pix_clk_div: %u\n", min_sys_div);
	min_sys_div = max(min_sys_div,
			  op_pll_fr->pll_op_clk_freq_hz
			  / lim->vt_bk.max_sys_clk_freq_hz);
	dev_dbg(dev, "min_sys_div: max_pll_op_clk_freq_hz: %u\n", min_sys_div);
	min_sys_div = clk_div_even_up(min_sys_div);
	dev_dbg(dev, "min_sys_div: one or even: %u\n", min_sys_div);

	max_sys_div = lim->vt_bk.max_sys_clk_div;
	dev_dbg(dev, "max_sys_div: %u\n", max_sys_div);
	max_sys_div = min(max_sys_div,
			  DIV_ROUND_UP(max_vt_div,
				       lim->vt_bk.min_pix_clk_div));
	dev_dbg(dev, "max_sys_div: min_vt_pix_clk_div: %u\n", max_sys_div);
	max_sys_div = min(max_sys_div,
			  DIV_ROUND_UP(op_pll_fr->pll_op_clk_freq_hz,
				       lim->vt_bk.min_pix_clk_freq_hz));
	dev_dbg(dev, "max_sys_div: min_vt_pix_clk_freq_hz: %u\n", max_sys_div);

	/*
	 * Find pix_div such that a legal pix_div * sys_div results
	 * into a value which is not smaller than div, the desired
	 * divisor.
	 */
	for (vt_div = min_vt_div; vt_div <= max_vt_div;
	     vt_div += 2 - (vt_div & 1)) {
		for (sys_div = min_sys_div;
		     sys_div <= max_sys_div;
		     sys_div += 2 - (sys_div & 1)) {
			uint16_t pix_div = DIV_ROUND_UP(vt_div, sys_div);
			uint16_t rounded_div;

			if (pix_div < lim->vt_bk.min_pix_clk_div
			    || pix_div > lim->vt_bk.max_pix_clk_div) {
				dev_dbg(dev,
					"pix_div %u too small or too big (%u--%u)\n",
					pix_div,
					lim->vt_bk.min_pix_clk_div,
					lim->vt_bk.max_pix_clk_div);
				continue;
			}

			rounded_div = roundup(vt_div, best_pix_div);

			/* Check if this one is better. */
			if (pix_div * sys_div <= rounded_div)
				best_pix_div = pix_div;

			/* Bail out if we've already found the best value. */
			if (vt_div == rounded_div)
				break;
		}
		if (best_pix_div < INT_MAX >> 1)
			break;
	}

	pll->vt_bk.sys_clk_div = DIV_ROUND_UP(vt_div, best_pix_div);
	pll->vt_bk.pix_clk_div = best_pix_div;

	pll->vt_bk.sys_clk_freq_hz =
		op_pll_fr->pll_op_clk_freq_hz / pll->vt_bk.sys_clk_div;
	pll->vt_bk.pix_clk_freq_hz =
		pll->vt_bk.sys_clk_freq_hz / pll->vt_bk.pix_clk_div;

out_skip_vt_calc:
	pll->pixel_rate_pixel_array =
		pll->vt_bk.pix_clk_freq_hz * pll->vt_lanes;

	return check_all_bounds(dev, lim, op_lim_fr, op_lim_bk, pll, op_pll_fr,
				op_pll_bk);
}

int ccs_pll_calculate(struct device *dev, const struct ccs_pll_limits *lim,
		      struct ccs_pll *pll)
{
	const struct ccs_pll_branch_limits_fr *op_lim_fr = &lim->vt_fr;
	const struct ccs_pll_branch_limits_bk *op_lim_bk = &lim->op_bk;
	struct ccs_pll_branch_fr *op_pll_fr = &pll->vt_fr;
	struct ccs_pll_branch_bk *op_pll_bk = &pll->op_bk;
	uint16_t min_op_pre_pll_clk_div;
	uint16_t max_op_pre_pll_clk_div;
	uint32_t mul, div;
	uint32_t i;
	int rval = -EINVAL;

	if (!(pll->flags & CCS_PLL_FLAG_LANE_SPEED_MODEL)) {
		pll->op_lanes = 1;
		pll->vt_lanes = 1;
	}
	dev_dbg(dev, "vt_lanes: %u\n", pll->vt_lanes);
	dev_dbg(dev, "op_lanes: %u\n", pll->op_lanes);

	if (pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS) {
		/*
		 * If there's no OP PLL at all, use the VT values
		 * instead. The OP values are ignored for the rest of
		 * the PLL calculation.
		 */
		op_lim_fr = &lim->vt_fr;
		op_lim_bk = &lim->vt_bk;
		op_pll_bk = &pll->vt_bk;
	}

	dev_dbg(dev, "binning: %ux%u\n", pll->binning_horizontal,
		pll->binning_vertical);

	switch (pll->bus_type) {
	case CCS_PLL_BUS_TYPE_CSI2_DPHY:
		/* CSI transfers 2 bits per clock per lane; thus times 2 */
		op_pll_bk->sys_clk_freq_hz = pll->link_freq * 2
			* (pll->flags & CCS_PLL_FLAG_LANE_SPEED_MODEL ?
			   1 : pll->csi2.lanes);
		break;
	default:
		return -EINVAL;
	}

	pll->pixel_rate_csi =
		op_pll_bk->pix_clk_freq_hz
		* (pll->flags & CCS_PLL_FLAG_LANE_SPEED_MODEL ?
		   pll->csi2.lanes : 1);

	/* Figure out limits for OP pre-pll divider based on extclk */
	dev_dbg(dev, "min / max op_pre_pll_clk_div: %u / %u\n",
		op_lim_fr->min_pre_pll_clk_div, op_lim_fr->max_pre_pll_clk_div);
	max_op_pre_pll_clk_div =
		min_t(uint16_t, op_lim_fr->max_pre_pll_clk_div,
		      clk_div_even(pll->ext_clk_freq_hz /
				   op_lim_fr->min_pll_ip_clk_freq_hz));
	min_op_pre_pll_clk_div =
		max_t(uint16_t, op_lim_fr->min_pre_pll_clk_div,
		      clk_div_even_up(
			      DIV_ROUND_UP(pll->ext_clk_freq_hz,
					   op_lim_fr->max_pll_ip_clk_freq_hz)));
	dev_dbg(dev, "pre-pll check: min / max op_pre_pll_clk_div: %u / %u\n",
		min_op_pre_pll_clk_div, max_op_pre_pll_clk_div);

	i = gcd(op_pll_bk->sys_clk_freq_hz, pll->ext_clk_freq_hz);
	mul = op_pll_bk->sys_clk_freq_hz / i;
	div = pll->ext_clk_freq_hz / i;
	dev_dbg(dev, "mul %u / div %u\n", mul, div);

	min_op_pre_pll_clk_div =
		max_t(uint16_t, min_op_pre_pll_clk_div,
		      clk_div_even_up(
			      mul /
			      one_or_more(
				      DIV_ROUND_UP(op_lim_fr->max_pll_op_clk_freq_hz,
						   pll->ext_clk_freq_hz))));
	dev_dbg(dev, "pll_op check: min / max op_pre_pll_clk_div: %u / %u\n",
		min_op_pre_pll_clk_div, max_op_pre_pll_clk_div);

	for (op_pll_fr->pre_pll_clk_div = min_op_pre_pll_clk_div;
	     op_pll_fr->pre_pll_clk_div <= max_op_pre_pll_clk_div;
	     op_pll_fr->pre_pll_clk_div +=
		     (pll->flags & CCS_PLL_FLAG_EXT_IP_PLL_DIVIDER) ? 1 :
		     2 - (op_pll_fr->pre_pll_clk_div & 1)) {
		rval = __ccs_pll_calculate(dev, lim, op_lim_fr, op_lim_bk, pll,
					   op_pll_fr, op_pll_bk, mul, div);
		if (rval)
			continue;

		print_pll(dev, pll);
		return 0;
	}

	dev_dbg(dev, "unable to compute pre_pll divisor\n");

	return rval;
}
EXPORT_SYMBOL_GPL(ccs_pll_calculate);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_DESCRIPTION("Generic MIPI CCS/SMIA/SMIA++ PLL calculator");
MODULE_LICENSE("GPL v2");
