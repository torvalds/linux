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
static inline u32 clk_div_even(u32 a)
{
	return max_t(u32, 1, a & ~1);
}

/* Return an even number or one. */
static inline u32 clk_div_even_up(u32 a)
{
	if (a == 1)
		return 1;
	return (a + 1) & ~1;
}

static inline u32 is_one_or_even(u32 a)
{
	if (a == 1)
		return 1;
	if (a & 1)
		return 0;

	return 1;
}

static inline u32 one_or_more(u32 a)
{
	return a ?: 1;
}

static int bounds_check(struct device *dev, u32 val,
			u32 min, u32 max, const char *prefix,
			char *str)
{
	if (val >= min && val <= max)
		return 0;

	dev_dbg(dev, "%s_%s out of bounds: %d (%d--%d)\n", prefix,
		str, val, min, max);

	return -EINVAL;
}

#define PLL_OP 1
#define PLL_VT 2

static const char *pll_string(unsigned int which)
{
	switch (which) {
	case PLL_OP:
		return "op";
	case PLL_VT:
		return "vt";
	}

	return NULL;
}

#define PLL_FL(f) CCS_PLL_FLAG_##f

static void print_pll(struct device *dev, const struct ccs_pll *pll)
{
	const struct {
		const struct ccs_pll_branch_fr *fr;
		const struct ccs_pll_branch_bk *bk;
		unsigned int which;
	} branches[] = {
		{ &pll->vt_fr, &pll->vt_bk, PLL_VT },
		{ &pll->op_fr, &pll->op_bk, PLL_OP }
	}, *br;
	unsigned int i;

	dev_dbg(dev, "ext_clk_freq_hz\t\t%u\n", pll->ext_clk_freq_hz);

	for (i = 0, br = branches; i < ARRAY_SIZE(branches); i++, br++) {
		const char *s = pll_string(br->which);

		if (pll->flags & CCS_PLL_FLAG_DUAL_PLL ||
		    br->which == PLL_VT) {
			dev_dbg(dev, "%s_pre_pll_clk_div\t\t%u\n",  s,
				br->fr->pre_pll_clk_div);
			dev_dbg(dev, "%s_pll_multiplier\t\t%u\n",  s,
				br->fr->pll_multiplier);

			dev_dbg(dev, "%s_pll_ip_clk_freq_hz\t%u\n", s,
				br->fr->pll_ip_clk_freq_hz);
			dev_dbg(dev, "%s_pll_op_clk_freq_hz\t%u\n", s,
				br->fr->pll_op_clk_freq_hz);
		}

		if (!(pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS) ||
		    br->which == PLL_VT) {
			dev_dbg(dev, "%s_sys_clk_div\t\t%u\n",  s,
				br->bk->sys_clk_div);
			dev_dbg(dev, "%s_pix_clk_div\t\t%u\n", s,
				br->bk->pix_clk_div);

			dev_dbg(dev, "%s_sys_clk_freq_hz\t%u\n", s,
				br->bk->sys_clk_freq_hz);
			dev_dbg(dev, "%s_pix_clk_freq_hz\t%u\n", s,
				br->bk->pix_clk_freq_hz);
		}
	}

	dev_dbg(dev, "pixel rate in pixel array:\t%u\n",
		pll->pixel_rate_pixel_array);
	dev_dbg(dev, "pixel rate on CSI-2 bus:\t%u\n",
		pll->pixel_rate_csi);
}

static void print_pll_flags(struct device *dev, struct ccs_pll *pll)
{
	dev_dbg(dev, "PLL flags%s%s%s%s%s%s%s%s%s%s%s\n",
		pll->flags & PLL_FL(OP_PIX_CLOCK_PER_LANE) ? " op-pix-clock-per-lane" : "",
		pll->flags & PLL_FL(EVEN_PLL_MULTIPLIER) ? " even-pll-multiplier" : "",
		pll->flags & PLL_FL(NO_OP_CLOCKS) ? " no-op-clocks" : "",
		pll->flags & PLL_FL(LANE_SPEED_MODEL) ? " lane-speed" : "",
		pll->flags & PLL_FL(EXT_IP_PLL_DIVIDER) ?
		" ext-ip-pll-divider" : "",
		pll->flags & PLL_FL(FLEXIBLE_OP_PIX_CLK_DIV) ?
		" flexible-op-pix-div" : "",
		pll->flags & PLL_FL(FIFO_DERATING) ? " fifo-derating" : "",
		pll->flags & PLL_FL(FIFO_OVERRATING) ? " fifo-overrating" : "",
		pll->flags & PLL_FL(DUAL_PLL) ? " dual-pll" : "",
		pll->flags & PLL_FL(OP_SYS_DDR) ? " op-sys-ddr" : "",
		pll->flags & PLL_FL(OP_PIX_DDR) ? " op-pix-ddr" : "");
}

static u32 op_sys_ddr(u32 flags)
{
	return flags & CCS_PLL_FLAG_OP_SYS_DDR ? 1 : 0;
}

static u32 op_pix_ddr(u32 flags)
{
	return flags & CCS_PLL_FLAG_OP_PIX_DDR ? 1 : 0;
}

static int check_fr_bounds(struct device *dev,
			   const struct ccs_pll_limits *lim,
			   const struct ccs_pll *pll, unsigned int which)
{
	const struct ccs_pll_branch_limits_fr *lim_fr;
	const struct ccs_pll_branch_fr *pll_fr;
	const char *s = pll_string(which);
	int rval;

	if (which == PLL_OP) {
		lim_fr = &lim->op_fr;
		pll_fr = &pll->op_fr;
	} else {
		lim_fr = &lim->vt_fr;
		pll_fr = &pll->vt_fr;
	}

	rval = bounds_check(dev, pll_fr->pre_pll_clk_div,
			    lim_fr->min_pre_pll_clk_div,
			    lim_fr->max_pre_pll_clk_div, s, "pre_pll_clk_div");

	if (!rval)
		rval = bounds_check(dev, pll_fr->pll_ip_clk_freq_hz,
				    lim_fr->min_pll_ip_clk_freq_hz,
				    lim_fr->max_pll_ip_clk_freq_hz,
				    s, "pll_ip_clk_freq_hz");
	if (!rval)
		rval = bounds_check(dev, pll_fr->pll_multiplier,
				    lim_fr->min_pll_multiplier,
				    lim_fr->max_pll_multiplier,
				    s, "pll_multiplier");
	if (!rval)
		rval = bounds_check(dev, pll_fr->pll_op_clk_freq_hz,
				    lim_fr->min_pll_op_clk_freq_hz,
				    lim_fr->max_pll_op_clk_freq_hz,
				    s, "pll_op_clk_freq_hz");

	return rval;
}

static int check_bk_bounds(struct device *dev,
			   const struct ccs_pll_limits *lim,
			   const struct ccs_pll *pll, unsigned int which)
{
	const struct ccs_pll_branch_limits_bk *lim_bk;
	const struct ccs_pll_branch_bk *pll_bk;
	const char *s = pll_string(which);
	int rval;

	if (which == PLL_OP) {
		if (pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS)
			return 0;

		lim_bk = &lim->op_bk;
		pll_bk = &pll->op_bk;
	} else {
		lim_bk = &lim->vt_bk;
		pll_bk = &pll->vt_bk;
	}

	rval = bounds_check(dev, pll_bk->sys_clk_div,
			    lim_bk->min_sys_clk_div,
			    lim_bk->max_sys_clk_div, s, "op_sys_clk_div");
	if (!rval)
		rval = bounds_check(dev, pll_bk->sys_clk_freq_hz,
				    lim_bk->min_sys_clk_freq_hz,
				    lim_bk->max_sys_clk_freq_hz,
				    s, "sys_clk_freq_hz");
	if (!rval)
		rval = bounds_check(dev, pll_bk->sys_clk_div,
				    lim_bk->min_sys_clk_div,
				    lim_bk->max_sys_clk_div,
				    s, "sys_clk_div");
	if (!rval)
		rval = bounds_check(dev, pll_bk->pix_clk_freq_hz,
				    lim_bk->min_pix_clk_freq_hz,
				    lim_bk->max_pix_clk_freq_hz,
				    s, "pix_clk_freq_hz");

	return rval;
}

static int check_ext_bounds(struct device *dev, const struct ccs_pll *pll)
{
	if (!(pll->flags & CCS_PLL_FLAG_FIFO_DERATING) &&
	    pll->pixel_rate_pixel_array > pll->pixel_rate_csi) {
		dev_dbg(dev, "device does not support derating\n");
		return -EINVAL;
	}

	if (!(pll->flags & CCS_PLL_FLAG_FIFO_OVERRATING) &&
	    pll->pixel_rate_pixel_array < pll->pixel_rate_csi) {
		dev_dbg(dev, "device does not support overrating\n");
		return -EINVAL;
	}

	return 0;
}

static void
ccs_pll_find_vt_sys_div(struct device *dev, const struct ccs_pll_limits *lim,
			struct ccs_pll *pll, struct ccs_pll_branch_fr *pll_fr,
			u16 min_vt_div, u16 max_vt_div,
			u16 *min_sys_div, u16 *max_sys_div)
{
	/*
	 * Find limits for sys_clk_div. Not all values are possible with all
	 * values of pix_clk_div.
	 */
	*min_sys_div = lim->vt_bk.min_sys_clk_div;
	dev_dbg(dev, "min_sys_div: %u\n", *min_sys_div);
	*min_sys_div = max_t(u16, *min_sys_div,
			     DIV_ROUND_UP(min_vt_div,
					  lim->vt_bk.max_pix_clk_div));
	dev_dbg(dev, "min_sys_div: max_vt_pix_clk_div: %u\n", *min_sys_div);
	*min_sys_div = max_t(u16, *min_sys_div,
			     pll_fr->pll_op_clk_freq_hz
			     / lim->vt_bk.max_sys_clk_freq_hz);
	dev_dbg(dev, "min_sys_div: max_pll_op_clk_freq_hz: %u\n", *min_sys_div);
	*min_sys_div = clk_div_even_up(*min_sys_div);
	dev_dbg(dev, "min_sys_div: one or even: %u\n", *min_sys_div);

	*max_sys_div = lim->vt_bk.max_sys_clk_div;
	dev_dbg(dev, "max_sys_div: %u\n", *max_sys_div);
	*max_sys_div = min_t(u16, *max_sys_div,
			     DIV_ROUND_UP(max_vt_div,
					  lim->vt_bk.min_pix_clk_div));
	dev_dbg(dev, "max_sys_div: min_vt_pix_clk_div: %u\n", *max_sys_div);
	*max_sys_div = min_t(u16, *max_sys_div,
			     DIV_ROUND_UP(pll_fr->pll_op_clk_freq_hz,
					  lim->vt_bk.min_pix_clk_freq_hz));
	dev_dbg(dev, "max_sys_div: min_vt_pix_clk_freq_hz: %u\n", *max_sys_div);
}

#define CPHY_CONST		7
#define DPHY_CONST		16
#define PHY_CONST_DIV		16

static inline int
__ccs_pll_calculate_vt_tree(struct device *dev,
			    const struct ccs_pll_limits *lim,
			    struct ccs_pll *pll, u32 mul, u32 div)
{
	const struct ccs_pll_branch_limits_fr *lim_fr = &lim->vt_fr;
	const struct ccs_pll_branch_limits_bk *lim_bk = &lim->vt_bk;
	struct ccs_pll_branch_fr *pll_fr = &pll->vt_fr;
	struct ccs_pll_branch_bk *pll_bk = &pll->vt_bk;
	u32 more_mul;
	u16 best_pix_div = SHRT_MAX >> 1, best_div = lim_bk->max_sys_clk_div;
	u16 vt_div, min_sys_div, max_sys_div, sys_div;

	pll_fr->pll_ip_clk_freq_hz =
		pll->ext_clk_freq_hz / pll_fr->pre_pll_clk_div;

	dev_dbg(dev, "vt_pll_ip_clk_freq_hz %u\n", pll_fr->pll_ip_clk_freq_hz);

	more_mul = one_or_more(DIV_ROUND_UP(lim_fr->min_pll_op_clk_freq_hz,
					    pll_fr->pll_ip_clk_freq_hz * mul));

	dev_dbg(dev, "more_mul: %u\n", more_mul);
	more_mul *= DIV_ROUND_UP(lim_fr->min_pll_multiplier, mul * more_mul);
	dev_dbg(dev, "more_mul2: %u\n", more_mul);

	if (pll->flags & CCS_PLL_FLAG_EVEN_PLL_MULTIPLIER &&
	    (mul & 1) && (more_mul & 1))
		more_mul <<= 1;

	pll_fr->pll_multiplier = mul * more_mul;
	if (pll_fr->pll_multiplier > lim_fr->max_pll_multiplier) {
		dev_dbg(dev, "pll multiplier %u too high\n",
			pll_fr->pll_multiplier);
		return -EINVAL;
	}

	pll_fr->pll_op_clk_freq_hz =
		pll_fr->pll_ip_clk_freq_hz * pll_fr->pll_multiplier;
	if (pll_fr->pll_op_clk_freq_hz > lim_fr->max_pll_op_clk_freq_hz) {
		dev_dbg(dev, "too high OP clock %u\n",
			pll_fr->pll_op_clk_freq_hz);
		return -EINVAL;
	}

	vt_div = div * more_mul;

	ccs_pll_find_vt_sys_div(dev, lim, pll, pll_fr, vt_div, vt_div,
				&min_sys_div, &max_sys_div);

	max_sys_div = (vt_div & 1) ? 1 : max_sys_div;

	dev_dbg(dev, "vt min/max_sys_div: %u,%u\n", min_sys_div, max_sys_div);

	for (sys_div = min_sys_div; sys_div <= max_sys_div;
	     sys_div += 2 - (sys_div & 1)) {
		u16 pix_div;

		if (vt_div % sys_div)
			continue;

		pix_div = vt_div / sys_div;

		if (pix_div < lim_bk->min_pix_clk_div ||
		    pix_div > lim_bk->max_pix_clk_div) {
			dev_dbg(dev,
				"pix_div %u too small or too big (%u--%u)\n",
				pix_div,
				lim_bk->min_pix_clk_div,
				lim_bk->max_pix_clk_div);
			continue;
		}

		dev_dbg(dev, "sys/pix/best_pix: %u,%u,%u\n", sys_div, pix_div,
			best_pix_div);

		if (pix_div * sys_div <= best_pix_div) {
			best_pix_div = pix_div;
			best_div = pix_div * sys_div;
		}
	}
	if (best_pix_div == SHRT_MAX >> 1)
		return -EINVAL;

	pll_bk->sys_clk_div = best_div / best_pix_div;
	pll_bk->pix_clk_div = best_pix_div;

	pll_bk->sys_clk_freq_hz =
		pll_fr->pll_op_clk_freq_hz / pll_bk->sys_clk_div;
	pll_bk->pix_clk_freq_hz =
		pll_bk->sys_clk_freq_hz / pll_bk->pix_clk_div;

	pll->pixel_rate_pixel_array =
		pll_bk->pix_clk_freq_hz * pll->vt_lanes;

	return 0;
}

static int ccs_pll_calculate_vt_tree(struct device *dev,
				     const struct ccs_pll_limits *lim,
				     struct ccs_pll *pll)
{
	const struct ccs_pll_branch_limits_fr *lim_fr = &lim->vt_fr;
	struct ccs_pll_branch_fr *pll_fr = &pll->vt_fr;
	u16 min_pre_pll_clk_div = lim_fr->min_pre_pll_clk_div;
	u16 max_pre_pll_clk_div = lim_fr->max_pre_pll_clk_div;
	u32 pre_mul, pre_div;

	pre_div = gcd(pll->pixel_rate_csi,
		      pll->ext_clk_freq_hz * pll->vt_lanes);
	pre_mul = pll->pixel_rate_csi / pre_div;
	pre_div = pll->ext_clk_freq_hz * pll->vt_lanes / pre_div;

	/* Make sure PLL input frequency is within limits */
	max_pre_pll_clk_div =
		min_t(u16, max_pre_pll_clk_div,
		      DIV_ROUND_UP(pll->ext_clk_freq_hz,
				   lim_fr->min_pll_ip_clk_freq_hz));

	min_pre_pll_clk_div = max_t(u16, min_pre_pll_clk_div,
				    pll->ext_clk_freq_hz /
				    lim_fr->max_pll_ip_clk_freq_hz);
	if (!(pll->flags & CCS_PLL_FLAG_EXT_IP_PLL_DIVIDER))
		min_pre_pll_clk_div = clk_div_even(min_pre_pll_clk_div);

	dev_dbg(dev, "vt min/max_pre_pll_clk_div: %u,%u\n",
		min_pre_pll_clk_div, max_pre_pll_clk_div);

	for (pll_fr->pre_pll_clk_div = min_pre_pll_clk_div;
	     pll_fr->pre_pll_clk_div <= max_pre_pll_clk_div;
	     pll_fr->pre_pll_clk_div +=
		     (pll->flags & CCS_PLL_FLAG_EXT_IP_PLL_DIVIDER) ? 1 :
		     2 - (pll_fr->pre_pll_clk_div & 1)) {
		u32 mul, div;
		int rval;

		div = gcd(pre_mul * pll_fr->pre_pll_clk_div, pre_div);
		mul = pre_mul * pll_fr->pre_pll_clk_div / div;
		div = pre_div / div;

		dev_dbg(dev, "vt pre-div/mul/div: %u,%u,%u\n",
			pll_fr->pre_pll_clk_div, mul, div);

		rval = __ccs_pll_calculate_vt_tree(dev, lim, pll,
						   mul, div);
		if (rval)
			continue;

		rval = check_fr_bounds(dev, lim, pll, PLL_VT);
		if (rval)
			continue;

		rval = check_bk_bounds(dev, lim, pll, PLL_VT);
		if (rval)
			continue;

		return 0;
	}

	dev_dbg(dev, "unable to compute VT pre_pll divisor\n");
	return -EINVAL;
}

static int
ccs_pll_calculate_vt(struct device *dev, const struct ccs_pll_limits *lim,
		     const struct ccs_pll_branch_limits_bk *op_lim_bk,
		     struct ccs_pll *pll, struct ccs_pll_branch_fr *pll_fr,
		     struct ccs_pll_branch_bk *op_pll_bk, bool cphy,
		     u32 phy_const)
{
	u16 sys_div;
	u16 best_pix_div = SHRT_MAX >> 1;
	u16 vt_op_binning_div;
	u16 min_vt_div, max_vt_div, vt_div;
	u16 min_sys_div, max_sys_div;

	if (pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS)
		goto out_calc_pixel_rate;

	/*
	 * Find out whether a sensor supports derating. If it does not, VT and
	 * OP domains are required to run at the same pixel rate.
	 */
	if (!(pll->flags & CCS_PLL_FLAG_FIFO_DERATING)) {
		min_vt_div =
			op_pll_bk->sys_clk_div * op_pll_bk->pix_clk_div
			* pll->vt_lanes * phy_const / pll->op_lanes
			/ (PHY_CONST_DIV << op_pix_ddr(pll->flags));
	} else {
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
		min_vt_div =
			DIV_ROUND_UP(pll->bits_per_pixel
				     * op_pll_bk->sys_clk_div * pll->scale_n
				     * pll->vt_lanes * phy_const,
				     (pll->flags &
				      CCS_PLL_FLAG_LANE_SPEED_MODEL ?
				      pll->csi2.lanes : 1)
				     * vt_op_binning_div * pll->scale_m
				     * PHY_CONST_DIV << op_pix_ddr(pll->flags));
	}

	/* Find smallest and biggest allowed vt divisor. */
	dev_dbg(dev, "min_vt_div: %u\n", min_vt_div);
	min_vt_div = max_t(u16, min_vt_div,
			   DIV_ROUND_UP(pll_fr->pll_op_clk_freq_hz,
					lim->vt_bk.max_pix_clk_freq_hz));
	dev_dbg(dev, "min_vt_div: max_vt_pix_clk_freq_hz: %u\n",
		min_vt_div);
	min_vt_div = max_t(u16, min_vt_div, lim->vt_bk.min_pix_clk_div
					    * lim->vt_bk.min_sys_clk_div);
	dev_dbg(dev, "min_vt_div: min_vt_clk_div: %u\n", min_vt_div);

	max_vt_div = lim->vt_bk.max_sys_clk_div * lim->vt_bk.max_pix_clk_div;
	dev_dbg(dev, "max_vt_div: %u\n", max_vt_div);
	max_vt_div = min_t(u16, max_vt_div,
			   DIV_ROUND_UP(pll_fr->pll_op_clk_freq_hz,
				      lim->vt_bk.min_pix_clk_freq_hz));
	dev_dbg(dev, "max_vt_div: min_vt_pix_clk_freq_hz: %u\n",
		max_vt_div);

	ccs_pll_find_vt_sys_div(dev, lim, pll, pll_fr, min_vt_div,
				max_vt_div, &min_sys_div, &max_sys_div);

	/*
	 * Find pix_div such that a legal pix_div * sys_div results
	 * into a value which is not smaller than div, the desired
	 * divisor.
	 */
	for (vt_div = min_vt_div; vt_div <= max_vt_div; vt_div++) {
		u16 __max_sys_div = vt_div & 1 ? 1 : max_sys_div;

		for (sys_div = min_sys_div; sys_div <= __max_sys_div;
		     sys_div += 2 - (sys_div & 1)) {
			u16 pix_div;
			u16 rounded_div;

			pix_div = DIV_ROUND_UP(vt_div, sys_div);

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
		if (best_pix_div < SHRT_MAX >> 1)
			break;
	}
	if (best_pix_div == SHRT_MAX >> 1)
		return -EINVAL;

	pll->vt_bk.sys_clk_div = DIV_ROUND_UP(vt_div, best_pix_div);
	pll->vt_bk.pix_clk_div = best_pix_div;

	pll->vt_bk.sys_clk_freq_hz =
		pll_fr->pll_op_clk_freq_hz / pll->vt_bk.sys_clk_div;
	pll->vt_bk.pix_clk_freq_hz =
		pll->vt_bk.sys_clk_freq_hz / pll->vt_bk.pix_clk_div;

out_calc_pixel_rate:
	pll->pixel_rate_pixel_array =
		pll->vt_bk.pix_clk_freq_hz * pll->vt_lanes;

	return 0;
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
ccs_pll_calculate_op(struct device *dev, const struct ccs_pll_limits *lim,
		     const struct ccs_pll_branch_limits_fr *op_lim_fr,
		     const struct ccs_pll_branch_limits_bk *op_lim_bk,
		     struct ccs_pll *pll, struct ccs_pll_branch_fr *op_pll_fr,
		     struct ccs_pll_branch_bk *op_pll_bk, u32 mul,
		     u32 div, u32 op_sys_clk_freq_hz_sdr, u32 l,
		     bool cphy, u32 phy_const)
{
	/*
	 * Higher multipliers (and divisors) are often required than
	 * necessitated by the external clock and the output clocks.
	 * There are limits for all values in the clock tree. These
	 * are the minimum and maximum multiplier for mul.
	 */
	u32 more_mul_min, more_mul_max;
	u32 more_mul_factor;
	u32 i;

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
		min_t(u32,
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

	if (pll->flags & CCS_PLL_FLAG_EVEN_PLL_MULTIPLIER &&
	    mul & 1 && i & 1)
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

	if (pll->flags & CCS_PLL_FLAG_LANE_SPEED_MODEL)
		op_pll_bk->pix_clk_div =
			(pll->bits_per_pixel
			 * pll->op_lanes * (phy_const << op_sys_ddr(pll->flags))
			 / PHY_CONST_DIV / pll->csi2.lanes / l)
			>> op_pix_ddr(pll->flags);
	else
		op_pll_bk->pix_clk_div =
			(pll->bits_per_pixel
			 * (phy_const << op_sys_ddr(pll->flags))
			 / PHY_CONST_DIV / l) >> op_pix_ddr(pll->flags);

	op_pll_bk->pix_clk_freq_hz =
		(op_sys_clk_freq_hz_sdr >> op_pix_ddr(pll->flags))
		/ op_pll_bk->pix_clk_div;
	op_pll_bk->sys_clk_freq_hz =
		op_sys_clk_freq_hz_sdr >> op_sys_ddr(pll->flags);

	dev_dbg(dev, "op_pix_clk_div: %u\n", op_pll_bk->pix_clk_div);

	return 0;
}

int ccs_pll_calculate(struct device *dev, const struct ccs_pll_limits *lim,
		      struct ccs_pll *pll)
{
	const struct ccs_pll_branch_limits_fr *op_lim_fr;
	const struct ccs_pll_branch_limits_bk *op_lim_bk;
	struct ccs_pll_branch_fr *op_pll_fr;
	struct ccs_pll_branch_bk *op_pll_bk;
	bool cphy = pll->bus_type == CCS_PLL_BUS_TYPE_CSI2_CPHY;
	u32 phy_const = cphy ? CPHY_CONST : DPHY_CONST;
	u32 op_sys_clk_freq_hz_sdr;
	u16 min_op_pre_pll_clk_div;
	u16 max_op_pre_pll_clk_div;
	u32 mul, div;
	u32 l = (!pll->op_bits_per_lane ||
		 pll->op_bits_per_lane >= pll->bits_per_pixel) ? 1 : 2;
	u32 i;
	int rval = -EINVAL;

	print_pll_flags(dev, pll);

	if (!(pll->flags & CCS_PLL_FLAG_LANE_SPEED_MODEL)) {
		pll->op_lanes = 1;
		pll->vt_lanes = 1;
	}

	if (pll->flags & CCS_PLL_FLAG_DUAL_PLL) {
		op_lim_fr = &lim->op_fr;
		op_lim_bk = &lim->op_bk;
		op_pll_fr = &pll->op_fr;
		op_pll_bk = &pll->op_bk;
	} else if (pll->flags & CCS_PLL_FLAG_NO_OP_CLOCKS) {
		/*
		 * If there's no OP PLL at all, use the VT values
		 * instead. The OP values are ignored for the rest of
		 * the PLL calculation.
		 */
		op_lim_fr = &lim->vt_fr;
		op_lim_bk = &lim->vt_bk;
		op_pll_fr = &pll->vt_fr;
		op_pll_bk = &pll->vt_bk;
	} else {
		op_lim_fr = &lim->vt_fr;
		op_lim_bk = &lim->op_bk;
		op_pll_fr = &pll->vt_fr;
		op_pll_bk = &pll->op_bk;
	}

	if (!pll->op_lanes || !pll->vt_lanes || !pll->bits_per_pixel ||
	    !pll->ext_clk_freq_hz || !pll->link_freq || !pll->scale_m ||
	    !op_lim_fr->min_pll_ip_clk_freq_hz ||
	    !op_lim_fr->max_pll_ip_clk_freq_hz ||
	    !op_lim_fr->min_pll_op_clk_freq_hz ||
	    !op_lim_fr->max_pll_op_clk_freq_hz ||
	    !op_lim_bk->max_sys_clk_div || !op_lim_fr->max_pll_multiplier)
		return -EINVAL;

	/*
	 * Make sure op_pix_clk_div will be integer --- unless flexible
	 * op_pix_clk_div is supported
	 */
	if (!(pll->flags & CCS_PLL_FLAG_FLEXIBLE_OP_PIX_CLK_DIV) &&
	    (pll->bits_per_pixel * pll->op_lanes) %
	    (pll->csi2.lanes * l << op_pix_ddr(pll->flags))) {
		dev_dbg(dev, "op_pix_clk_div not an integer (bpp %u, op lanes %u, lanes %u, l %u)\n",
			pll->bits_per_pixel, pll->op_lanes, pll->csi2.lanes, l);
		return -EINVAL;
	}

	dev_dbg(dev, "vt_lanes: %u\n", pll->vt_lanes);
	dev_dbg(dev, "op_lanes: %u\n", pll->op_lanes);

	dev_dbg(dev, "binning: %ux%u\n", pll->binning_horizontal,
		pll->binning_vertical);

	switch (pll->bus_type) {
	case CCS_PLL_BUS_TYPE_CSI2_DPHY:
	case CCS_PLL_BUS_TYPE_CSI2_CPHY:
		op_sys_clk_freq_hz_sdr = pll->link_freq * 2
			* (pll->flags & CCS_PLL_FLAG_LANE_SPEED_MODEL ?
			   1 : pll->csi2.lanes);
		break;
	default:
		return -EINVAL;
	}

	pll->pixel_rate_csi =
		div_u64((uint64_t)op_sys_clk_freq_hz_sdr
			* (pll->flags & CCS_PLL_FLAG_LANE_SPEED_MODEL ?
			   pll->csi2.lanes : 1) * PHY_CONST_DIV,
			phy_const * pll->bits_per_pixel * l);

	/* Figure out limits for OP pre-pll divider based on extclk */
	dev_dbg(dev, "min / max op_pre_pll_clk_div: %u / %u\n",
		op_lim_fr->min_pre_pll_clk_div, op_lim_fr->max_pre_pll_clk_div);
	max_op_pre_pll_clk_div =
		min_t(u16, op_lim_fr->max_pre_pll_clk_div,
		      DIV_ROUND_UP(pll->ext_clk_freq_hz,
				   op_lim_fr->min_pll_ip_clk_freq_hz));
	min_op_pre_pll_clk_div =
		max_t(u16, op_lim_fr->min_pre_pll_clk_div,
		      clk_div_even_up(
			      DIV_ROUND_UP(pll->ext_clk_freq_hz,
					   op_lim_fr->max_pll_ip_clk_freq_hz)));
	dev_dbg(dev, "pre-pll check: min / max op_pre_pll_clk_div: %u / %u\n",
		min_op_pre_pll_clk_div, max_op_pre_pll_clk_div);

	i = gcd(op_sys_clk_freq_hz_sdr,
		pll->ext_clk_freq_hz << op_pix_ddr(pll->flags));
	mul = op_sys_clk_freq_hz_sdr / i;
	div = (pll->ext_clk_freq_hz << op_pix_ddr(pll->flags)) / i;
	dev_dbg(dev, "mul %u / div %u\n", mul, div);

	min_op_pre_pll_clk_div =
		max_t(u16, min_op_pre_pll_clk_div,
		      clk_div_even_up(
			      mul /
			      one_or_more(
				      DIV_ROUND_UP(op_lim_fr->max_pll_op_clk_freq_hz,
						   pll->ext_clk_freq_hz))));
	if (!(pll->flags & CCS_PLL_FLAG_EXT_IP_PLL_DIVIDER))
		min_op_pre_pll_clk_div = clk_div_even(min_op_pre_pll_clk_div);
	dev_dbg(dev, "pll_op check: min / max op_pre_pll_clk_div: %u / %u\n",
		min_op_pre_pll_clk_div, max_op_pre_pll_clk_div);

	for (op_pll_fr->pre_pll_clk_div = min_op_pre_pll_clk_div;
	     op_pll_fr->pre_pll_clk_div <= max_op_pre_pll_clk_div;
	     op_pll_fr->pre_pll_clk_div +=
		     (pll->flags & CCS_PLL_FLAG_EXT_IP_PLL_DIVIDER) ? 1 :
		     2 - (op_pll_fr->pre_pll_clk_div & 1)) {
		rval = ccs_pll_calculate_op(dev, lim, op_lim_fr, op_lim_bk, pll,
					    op_pll_fr, op_pll_bk, mul, div,
					    op_sys_clk_freq_hz_sdr, l, cphy,
					    phy_const);
		if (rval)
			continue;

		rval = check_fr_bounds(dev, lim, pll,
				       pll->flags & CCS_PLL_FLAG_DUAL_PLL ?
				       PLL_OP : PLL_VT);
		if (rval)
			continue;

		rval = check_bk_bounds(dev, lim, pll, PLL_OP);
		if (rval)
			continue;

		if (pll->flags & CCS_PLL_FLAG_DUAL_PLL)
			break;

		rval = ccs_pll_calculate_vt(dev, lim, op_lim_bk, pll, op_pll_fr,
					    op_pll_bk, cphy, phy_const);
		if (rval)
			continue;

		rval = check_bk_bounds(dev, lim, pll, PLL_VT);
		if (rval)
			continue;
		rval = check_ext_bounds(dev, pll);
		if (rval)
			continue;

		break;
	}

	if (rval) {
		dev_dbg(dev, "unable to compute OP pre_pll divisor\n");
		return rval;
	}

	if (pll->flags & CCS_PLL_FLAG_DUAL_PLL) {
		rval = ccs_pll_calculate_vt_tree(dev, lim, pll);

		if (rval)
			return rval;
	}

	print_pll(dev, pll);

	return 0;
}
EXPORT_SYMBOL_GPL(ccs_pll_calculate);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_DESCRIPTION("Generic MIPI CCS/SMIA/SMIA++ PLL calculator");
MODULE_LICENSE("GPL");
