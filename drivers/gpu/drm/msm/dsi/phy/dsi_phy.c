// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/phy/phy.h>

#include "dsi_phy.h"

#define S_DIV_ROUND_UP(n, d)	\
	(((n) >= 0) ? (((n) + (d) - 1) / (d)) : (((n) - (d) + 1) / (d)))

static inline s32 linear_inter(s32 tmax, s32 tmin, s32 percent,
				s32 min_result, bool even)
{
	s32 v;

	v = (tmax - tmin) * percent;
	v = S_DIV_ROUND_UP(v, 100) + tmin;
	if (even && (v & 0x1))
		return max_t(s32, min_result, v - 1);
	else
		return max_t(s32, min_result, v);
}

static void dsi_dphy_timing_calc_clk_zero(struct msm_dsi_dphy_timing *timing,
					s32 ui, s32 coeff, s32 pcnt)
{
	s32 tmax, tmin, clk_z;
	s32 temp;

	/* reset */
	temp = 300 * coeff - ((timing->clk_prepare >> 1) + 1) * 2 * ui;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	if (tmin > 255) {
		tmax = 511;
		clk_z = linear_inter(2 * tmin, tmin, pcnt, 0, true);
	} else {
		tmax = 255;
		clk_z = linear_inter(tmax, tmin, pcnt, 0, true);
	}

	/* adjust */
	temp = (timing->hs_rqst + timing->clk_prepare + clk_z) & 0x7;
	timing->clk_zero = clk_z + 8 - temp;
}

int msm_dsi_dphy_timing_calc(struct msm_dsi_dphy_timing *timing,
			     struct msm_dsi_phy_clk_request *clk_req)
{
	const unsigned long bit_rate = clk_req->bitclk_rate;
	const unsigned long esc_rate = clk_req->escclk_rate;
	s32 ui, lpx;
	s32 tmax, tmin;
	s32 pcnt0 = 10;
	s32 pcnt1 = (bit_rate > 1200000000) ? 15 : 10;
	s32 pcnt2 = 10;
	s32 pcnt3 = (bit_rate > 180000000) ? 10 : 40;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	lpx = mult_frac(NSEC_PER_MSEC, coeff, esc_rate / 1000);

	tmax = S_DIV_ROUND_UP(95 * coeff, ui) - 2;
	tmin = S_DIV_ROUND_UP(38 * coeff, ui) - 2;
	timing->clk_prepare = linear_inter(tmax, tmin, pcnt0, 0, true);

	temp = lpx / ui;
	if (temp & 0x1)
		timing->hs_rqst = temp;
	else
		timing->hs_rqst = max_t(s32, 0, temp - 2);

	/* Calculate clk_zero after clk_prepare and hs_rqst */
	dsi_dphy_timing_calc_clk_zero(timing, ui, coeff, pcnt2);

	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	tmin = S_DIV_ROUND_UP(60 * coeff, ui) - 2;
	timing->clk_trail = linear_inter(tmax, tmin, pcnt3, 0, true);

	temp = 85 * coeff + 6 * ui;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	temp = 40 * coeff + 4 * ui;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_prepare = linear_inter(tmax, tmin, pcnt1, 0, true);

	tmax = 255;
	temp = ((timing->hs_prepare >> 1) + 1) * 2 * ui + 2 * ui;
	temp = 145 * coeff + 10 * ui - temp;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_zero = linear_inter(tmax, tmin, pcnt2, 24, true);

	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	temp = 60 * coeff + 4 * ui;
	tmin = DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_trail = linear_inter(tmax, tmin, pcnt3, 0, true);

	tmax = 255;
	tmin = S_DIV_ROUND_UP(100 * coeff, ui) - 2;
	timing->hs_exit = linear_inter(tmax, tmin, pcnt2, 0, true);

	tmax = 63;
	temp = ((timing->hs_exit >> 1) + 1) * 2 * ui;
	temp = 60 * coeff + 52 * ui - 24 * ui - temp;
	tmin = S_DIV_ROUND_UP(temp, 8 * ui) - 1;
	timing->shared_timings.clk_post = linear_inter(tmax, tmin, pcnt2, 0,
						       false);
	tmax = 63;
	temp = ((timing->clk_prepare >> 1) + 1) * 2 * ui;
	temp += ((timing->clk_zero >> 1) + 1) * 2 * ui;
	temp += 8 * ui + lpx;
	tmin = S_DIV_ROUND_UP(temp, 8 * ui) - 1;
	if (tmin > tmax) {
		temp = linear_inter(2 * tmax, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre = temp >> 1;
		timing->shared_timings.clk_pre_inc_by_2 = true;
	} else {
		timing->shared_timings.clk_pre =
				linear_inter(tmax, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre_inc_by_2 = false;
	}

	timing->ta_go = 3;
	timing->ta_sure = 0;
	timing->ta_get = 4;

	DBG("PHY timings: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
		timing->shared_timings.clk_pre, timing->shared_timings.clk_post,
		timing->shared_timings.clk_pre_inc_by_2, timing->clk_zero,
		timing->clk_trail, timing->clk_prepare, timing->hs_exit,
		timing->hs_zero, timing->hs_prepare, timing->hs_trail,
		timing->hs_rqst);

	return 0;
}

int msm_dsi_dphy_timing_calc_v2(struct msm_dsi_dphy_timing *timing,
				struct msm_dsi_phy_clk_request *clk_req)
{
	const unsigned long bit_rate = clk_req->bitclk_rate;
	const unsigned long esc_rate = clk_req->escclk_rate;
	s32 ui, ui_x8;
	s32 tmax, tmin;
	s32 pcnt0 = 50;
	s32 pcnt1 = 50;
	s32 pcnt2 = 10;
	s32 pcnt3 = 30;
	s32 pcnt4 = 10;
	s32 pcnt5 = 2;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 hb_en, hb_en_ckln, pd_ckln, pd;
	s32 val, val_ckln;
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	timing->hs_halfbyte_en = 0;
	hb_en = 0;
	timing->hs_halfbyte_en_ckln = 0;
	hb_en_ckln = 0;
	timing->hs_prep_dly_ckln = (bit_rate > 100000000) ? 0 : 3;
	pd_ckln = timing->hs_prep_dly_ckln;
	timing->hs_prep_dly = (bit_rate > 120000000) ? 0 : 1;
	pd = timing->hs_prep_dly;

	val = (hb_en << 2) + (pd << 1);
	val_ckln = (hb_en_ckln << 2) + (pd_ckln << 1);

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	ui_x8 = ui << 3;

	temp = S_DIV_ROUND_UP(38 * coeff - val_ckln * ui, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (95 * coeff - val_ckln * ui) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->clk_prepare = linear_inter(tmax, tmin, pcnt0, 0, false);

	temp = 300 * coeff - ((timing->clk_prepare << 3) + val_ckln) * ui;
	tmin = S_DIV_ROUND_UP(temp - 11 * ui, ui_x8) - 3;
	tmax = (tmin > 255) ? 511 : 255;
	timing->clk_zero = linear_inter(tmax, tmin, pcnt5, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 3 * ui, ui_x8);
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp + 3 * ui) / ui_x8;
	timing->clk_trail = linear_inter(tmax, tmin, pcnt3, 0, false);

	temp = S_DIV_ROUND_UP(40 * coeff + 4 * ui - val * ui, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (85 * coeff + 6 * ui - val * ui) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->hs_prepare = linear_inter(tmax, tmin, pcnt1, 0, false);

	temp = 145 * coeff + 10 * ui - ((timing->hs_prepare << 3) + val) * ui;
	tmin = S_DIV_ROUND_UP(temp - 11 * ui, ui_x8) - 3;
	tmax = 255;
	timing->hs_zero = linear_inter(tmax, tmin, pcnt4, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 4 * ui + 3 * ui, ui_x8);
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp + 3 * ui) / ui_x8;
	timing->hs_trail = linear_inter(tmax, tmin, pcnt3, 0, false);

	temp = 50 * coeff + ((hb_en << 2) - 8) * ui;
	timing->hs_rqst = S_DIV_ROUND_UP(temp, ui_x8);

	tmin = DIV_ROUND_UP(100 * coeff, ui_x8) - 1;
	tmax = 255;
	timing->hs_exit = linear_inter(tmax, tmin, pcnt2, 0, false);

	temp = 50 * coeff + ((hb_en_ckln << 2) - 8) * ui;
	timing->hs_rqst_ckln = S_DIV_ROUND_UP(temp, ui_x8);

	temp = 60 * coeff + 52 * ui - 43 * ui;
	tmin = DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 63;
	timing->shared_timings.clk_post =
				linear_inter(tmax, tmin, pcnt2, 0, false);

	temp = 8 * ui + ((timing->clk_prepare << 3) + val_ckln) * ui;
	temp += (((timing->clk_zero + 3) << 3) + 11 - (pd_ckln << 1)) * ui;
	temp += hb_en_ckln ? (((timing->hs_rqst_ckln << 3) + 4) * ui) :
				(((timing->hs_rqst_ckln << 3) + 8) * ui);
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 63;
	if (tmin > tmax) {
		temp = linear_inter(tmax << 1, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre = temp >> 1;
		timing->shared_timings.clk_pre_inc_by_2 = 1;
	} else {
		timing->shared_timings.clk_pre =
				linear_inter(tmax, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre_inc_by_2 = 0;
	}

	timing->ta_go = 3;
	timing->ta_sure = 0;
	timing->ta_get = 4;

	DBG("%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
	    timing->shared_timings.clk_pre, timing->shared_timings.clk_post,
	    timing->shared_timings.clk_pre_inc_by_2, timing->clk_zero,
	    timing->clk_trail, timing->clk_prepare, timing->hs_exit,
	    timing->hs_zero, timing->hs_prepare, timing->hs_trail,
	    timing->hs_rqst, timing->hs_rqst_ckln, timing->hs_halfbyte_en,
	    timing->hs_halfbyte_en_ckln, timing->hs_prep_dly,
	    timing->hs_prep_dly_ckln);

	return 0;
}

int msm_dsi_dphy_timing_calc_v3(struct msm_dsi_dphy_timing *timing,
	struct msm_dsi_phy_clk_request *clk_req)
{
	const unsigned long bit_rate = clk_req->bitclk_rate;
	const unsigned long esc_rate = clk_req->escclk_rate;
	s32 ui, ui_x8;
	s32 tmax, tmin;
	s32 pcnt0 = 50;
	s32 pcnt1 = 50;
	s32 pcnt2 = 10;
	s32 pcnt3 = 30;
	s32 pcnt4 = 10;
	s32 pcnt5 = 2;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 hb_en, hb_en_ckln;
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	timing->hs_halfbyte_en = 0;
	hb_en = 0;
	timing->hs_halfbyte_en_ckln = 0;
	hb_en_ckln = 0;

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	ui_x8 = ui << 3;

	temp = S_DIV_ROUND_UP(38 * coeff, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (95 * coeff) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->clk_prepare = linear_inter(tmax, tmin, pcnt0, 0, false);

	temp = 300 * coeff - (timing->clk_prepare << 3) * ui;
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = (tmin > 255) ? 511 : 255;
	timing->clk_zero = linear_inter(tmax, tmin, pcnt5, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 3 * ui, ui_x8);
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp + 3 * ui) / ui_x8;
	timing->clk_trail = linear_inter(tmax, tmin, pcnt3, 0, false);

	temp = S_DIV_ROUND_UP(40 * coeff + 4 * ui, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (85 * coeff + 6 * ui) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->hs_prepare = linear_inter(tmax, tmin, pcnt1, 0, false);

	temp = 145 * coeff + 10 * ui - (timing->hs_prepare << 3) * ui;
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 255;
	timing->hs_zero = linear_inter(tmax, tmin, pcnt4, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 4 * ui, ui_x8) - 1;
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp / ui_x8) - 1;
	timing->hs_trail = linear_inter(tmax, tmin, pcnt3, 0, false);

	temp = 50 * coeff + ((hb_en << 2) - 8) * ui;
	timing->hs_rqst = S_DIV_ROUND_UP(temp, ui_x8);

	tmin = DIV_ROUND_UP(100 * coeff, ui_x8) - 1;
	tmax = 255;
	timing->hs_exit = linear_inter(tmax, tmin, pcnt2, 0, false);

	temp = 50 * coeff + ((hb_en_ckln << 2) - 8) * ui;
	timing->hs_rqst_ckln = S_DIV_ROUND_UP(temp, ui_x8);

	temp = 60 * coeff + 52 * ui - 43 * ui;
	tmin = DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 63;
	timing->shared_timings.clk_post =
		linear_inter(tmax, tmin, pcnt2, 0, false);

	temp = 8 * ui + (timing->clk_prepare << 3) * ui;
	temp += (((timing->clk_zero + 3) << 3) + 11) * ui;
	temp += hb_en_ckln ? (((timing->hs_rqst_ckln << 3) + 4) * ui) :
		(((timing->hs_rqst_ckln << 3) + 8) * ui);
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 63;
	if (tmin > tmax) {
		temp = linear_inter(tmax << 1, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre = temp >> 1;
		timing->shared_timings.clk_pre_inc_by_2 = 1;
	} else {
		timing->shared_timings.clk_pre =
			linear_inter(tmax, tmin, pcnt2, 0, false);
		timing->shared_timings.clk_pre_inc_by_2 = 0;
	}

	timing->ta_go = 3;
	timing->ta_sure = 0;
	timing->ta_get = 4;

	DBG("%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
		timing->shared_timings.clk_pre, timing->shared_timings.clk_post,
		timing->shared_timings.clk_pre_inc_by_2, timing->clk_zero,
		timing->clk_trail, timing->clk_prepare, timing->hs_exit,
		timing->hs_zero, timing->hs_prepare, timing->hs_trail,
		timing->hs_rqst, timing->hs_rqst_ckln, timing->hs_halfbyte_en,
		timing->hs_halfbyte_en_ckln, timing->hs_prep_dly,
		timing->hs_prep_dly_ckln);

	return 0;
}

int msm_dsi_dphy_timing_calc_v4(struct msm_dsi_dphy_timing *timing,
	struct msm_dsi_phy_clk_request *clk_req)
{
	const unsigned long bit_rate = clk_req->bitclk_rate;
	const unsigned long esc_rate = clk_req->escclk_rate;
	s32 ui, ui_x8;
	s32 tmax, tmin;
	s32 pcnt_clk_prep = 50;
	s32 pcnt_clk_zero = 2;
	s32 pcnt_clk_trail = 30;
	s32 pcnt_hs_prep = 50;
	s32 pcnt_hs_zero = 10;
	s32 pcnt_hs_trail = 30;
	s32 pcnt_hs_exit = 10;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 hb_en;
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	hb_en = 0;

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	ui_x8 = ui << 3;

	/* TODO: verify these calculations against latest downstream driver
	 * everything except clk_post/clk_pre uses calculations from v3 based
	 * on the downstream driver having the same calculations for v3 and v4
	 */

	temp = S_DIV_ROUND_UP(38 * coeff, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (95 * coeff) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->clk_prepare = linear_inter(tmax, tmin, pcnt_clk_prep, 0, false);

	temp = 300 * coeff - (timing->clk_prepare << 3) * ui;
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = (tmin > 255) ? 511 : 255;
	timing->clk_zero = linear_inter(tmax, tmin, pcnt_clk_zero, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 3 * ui, ui_x8);
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp + 3 * ui) / ui_x8;
	timing->clk_trail = linear_inter(tmax, tmin, pcnt_clk_trail, 0, false);

	temp = S_DIV_ROUND_UP(40 * coeff + 4 * ui, ui_x8);
	tmin = max_t(s32, temp, 0);
	temp = (85 * coeff + 6 * ui) / ui_x8;
	tmax = max_t(s32, temp, 0);
	timing->hs_prepare = linear_inter(tmax, tmin, pcnt_hs_prep, 0, false);

	temp = 145 * coeff + 10 * ui - (timing->hs_prepare << 3) * ui;
	tmin = S_DIV_ROUND_UP(temp, ui_x8) - 1;
	tmax = 255;
	timing->hs_zero = linear_inter(tmax, tmin, pcnt_hs_zero, 0, false);

	tmin = DIV_ROUND_UP(60 * coeff + 4 * ui, ui_x8) - 1;
	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = (temp / ui_x8) - 1;
	timing->hs_trail = linear_inter(tmax, tmin, pcnt_hs_trail, 0, false);

	temp = 50 * coeff + ((hb_en << 2) - 8) * ui;
	timing->hs_rqst = S_DIV_ROUND_UP(temp, ui_x8);

	tmin = DIV_ROUND_UP(100 * coeff, ui_x8) - 1;
	tmax = 255;
	timing->hs_exit = linear_inter(tmax, tmin, pcnt_hs_exit, 0, false);

	/* recommended min
	 * = roundup((mipi_min_ns + t_hs_trail_ns)/(16*bit_clk_ns), 0) - 1
	 */
	temp = 60 * coeff + 52 * ui + + (timing->hs_trail + 1) * ui_x8;
	tmin = DIV_ROUND_UP(temp, 16 * ui) - 1;
	tmax = 255;
	timing->shared_timings.clk_post = linear_inter(tmax, tmin, 5, 0, false);

	/* recommended min
	 * val1 = (tlpx_ns + clk_prepare_ns + clk_zero_ns + hs_rqst_ns)
	 * val2 = (16 * bit_clk_ns)
	 * final = roundup(val1/val2, 0) - 1
	 */
	temp = 52 * coeff + (timing->clk_prepare + timing->clk_zero + 1) * ui_x8 + 54 * coeff;
	tmin = DIV_ROUND_UP(temp, 16 * ui) - 1;
	tmax = 255;
	timing->shared_timings.clk_pre = DIV_ROUND_UP((tmax - tmin) * 125, 10000) + tmin;

	DBG("%d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
		timing->shared_timings.clk_pre, timing->shared_timings.clk_post,
		timing->clk_zero, timing->clk_trail, timing->clk_prepare, timing->hs_exit,
		timing->hs_zero, timing->hs_prepare, timing->hs_trail, timing->hs_rqst);

	return 0;
}

int msm_dsi_cphy_timing_calc_v4(struct msm_dsi_dphy_timing *timing,
	struct msm_dsi_phy_clk_request *clk_req)
{
	const unsigned long bit_rate = clk_req->bitclk_rate;
	const unsigned long esc_rate = clk_req->escclk_rate;
	s32 ui, ui_x7;
	s32 tmax, tmin;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	ui_x7 = ui * 7;

	temp = S_DIV_ROUND_UP(38 * coeff, ui_x7);
	tmin = max_t(s32, temp, 0);
	temp = (95 * coeff) / ui_x7;
	tmax = max_t(s32, temp, 0);
	timing->clk_prepare = linear_inter(tmax, tmin, 50, 0, false);

	tmin = DIV_ROUND_UP(50 * coeff, ui_x7);
	tmax = 255;
	timing->hs_rqst = linear_inter(tmax, tmin, 1, 0, false);

	tmin = DIV_ROUND_UP(100 * coeff, ui_x7) - 1;
	tmax = 255;
	timing->hs_exit = linear_inter(tmax, tmin, 10, 0, false);

	tmin = 1;
	tmax = 32;
	timing->shared_timings.clk_post = linear_inter(tmax, tmin, 80, 0, false);

	tmin = min_t(s32, 64, S_DIV_ROUND_UP(262 * coeff, ui_x7) - 1);
	tmax = 64;
	timing->shared_timings.clk_pre = linear_inter(tmax, tmin, 20, 0, false);

	DBG("%d, %d, %d, %d, %d",
		timing->shared_timings.clk_pre, timing->shared_timings.clk_post,
		timing->clk_prepare, timing->hs_exit, timing->hs_rqst);

	return 0;
}

static int dsi_phy_enable_resource(struct msm_dsi_phy *phy)
{
	struct device *dev = &phy->pdev->dev;
	int ret;

	pm_runtime_get_sync(dev);

	ret = clk_prepare_enable(phy->ahb_clk);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: can't enable ahb clk, %d\n", __func__, ret);
		pm_runtime_put_sync(dev);
	}

	return ret;
}

static void dsi_phy_disable_resource(struct msm_dsi_phy *phy)
{
	clk_disable_unprepare(phy->ahb_clk);
	pm_runtime_put(&phy->pdev->dev);
}

static const struct of_device_id dsi_phy_dt_match[] = {
#ifdef CONFIG_DRM_MSM_DSI_28NM_PHY
	{ .compatible = "qcom,dsi-phy-28nm-hpm",
	  .data = &dsi_phy_28nm_hpm_cfgs },
	{ .compatible = "qcom,dsi-phy-28nm-hpm-fam-b",
	  .data = &dsi_phy_28nm_hpm_famb_cfgs },
	{ .compatible = "qcom,dsi-phy-28nm-lp",
	  .data = &dsi_phy_28nm_lp_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_20NM_PHY
	{ .compatible = "qcom,dsi-phy-20nm",
	  .data = &dsi_phy_20nm_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_28NM_8960_PHY
	{ .compatible = "qcom,dsi-phy-28nm-8960",
	  .data = &dsi_phy_28nm_8960_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_14NM_PHY
	{ .compatible = "qcom,dsi-phy-14nm",
	  .data = &dsi_phy_14nm_cfgs },
	{ .compatible = "qcom,dsi-phy-14nm-2290",
	  .data = &dsi_phy_14nm_2290_cfgs },
	{ .compatible = "qcom,dsi-phy-14nm-660",
	  .data = &dsi_phy_14nm_660_cfgs },
	{ .compatible = "qcom,dsi-phy-14nm-8953",
	  .data = &dsi_phy_14nm_8953_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_10NM_PHY
	{ .compatible = "qcom,dsi-phy-10nm",
	  .data = &dsi_phy_10nm_cfgs },
	{ .compatible = "qcom,dsi-phy-10nm-8998",
	  .data = &dsi_phy_10nm_8998_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_7NM_PHY
	{ .compatible = "qcom,dsi-phy-7nm",
	  .data = &dsi_phy_7nm_cfgs },
	{ .compatible = "qcom,dsi-phy-7nm-8150",
	  .data = &dsi_phy_7nm_8150_cfgs },
	{ .compatible = "qcom,sc7280-dsi-phy-7nm",
	  .data = &dsi_phy_7nm_7280_cfgs },
#endif
	{}
};

/*
 * Currently, we only support one SoC for each PHY type. When we have multiple
 * SoCs for the same PHY, we can try to make the index searching a bit more
 * clever.
 */
static int dsi_phy_get_id(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;
	const struct msm_dsi_phy_cfg *cfg = phy->cfg;
	struct resource *res;
	int i;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dsi_phy");
	if (!res)
		return -EINVAL;

	for (i = 0; i < cfg->num_dsi_phy; i++) {
		if (cfg->io_start[i] == res->start)
			return i;
	}

	return -EINVAL;
}

static int dsi_phy_driver_probe(struct platform_device *pdev)
{
	struct msm_dsi_phy *phy;
	struct device *dev = &pdev->dev;
	u32 phy_type;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->provided_clocks = devm_kzalloc(dev,
			struct_size(phy->provided_clocks, hws, NUM_PROVIDED_CLKS),
			GFP_KERNEL);
	if (!phy->provided_clocks)
		return -ENOMEM;

	phy->provided_clocks->num = NUM_PROVIDED_CLKS;

	phy->cfg = of_device_get_match_data(&pdev->dev);
	if (!phy->cfg)
		return -ENODEV;

	phy->pdev = pdev;

	phy->id = dsi_phy_get_id(phy);
	if (phy->id < 0)
		return dev_err_probe(dev, phy->id,
				     "Couldn't identify PHY index\n");

	phy->regulator_ldo_mode = of_property_read_bool(dev->of_node,
				"qcom,dsi-phy-regulator-ldo-mode");
	if (!of_property_read_u32(dev->of_node, "phy-type", &phy_type))
		phy->cphy_mode = (phy_type == PHY_TYPE_CPHY);

	phy->base = msm_ioremap_size(pdev, "dsi_phy", &phy->base_size);
	if (IS_ERR(phy->base))
		return dev_err_probe(dev, PTR_ERR(phy->base),
				     "Failed to map phy base\n");

	phy->pll_base = msm_ioremap_size(pdev, "dsi_pll", &phy->pll_size);
	if (IS_ERR(phy->pll_base))
		return dev_err_probe(dev, PTR_ERR(phy->pll_base),
				     "Failed to map pll base\n");

	if (phy->cfg->has_phy_lane) {
		phy->lane_base = msm_ioremap_size(pdev, "dsi_phy_lane", &phy->lane_size);
		if (IS_ERR(phy->lane_base))
			return dev_err_probe(dev, PTR_ERR(phy->lane_base),
					     "Failed to map phy lane base\n");
	}

	if (phy->cfg->has_phy_regulator) {
		phy->reg_base = msm_ioremap_size(pdev, "dsi_phy_regulator", &phy->reg_size);
		if (IS_ERR(phy->reg_base))
			return dev_err_probe(dev, PTR_ERR(phy->reg_base),
					     "Failed to map phy regulator base\n");
	}

	if (phy->cfg->ops.parse_dt_properties) {
		ret = phy->cfg->ops.parse_dt_properties(phy);
		if (ret)
			return ret;
	}

	ret = devm_regulator_bulk_get_const(dev, phy->cfg->num_regulators,
					    phy->cfg->regulator_data,
					    &phy->supplies);
	if (ret)
		return ret;

	phy->ahb_clk = msm_clk_get(pdev, "iface");
	if (IS_ERR(phy->ahb_clk))
		return dev_err_probe(dev, PTR_ERR(phy->ahb_clk),
				     "Unable to get ahb clk\n");

	/* PLL init will call into clk_register which requires
	 * register access, so we need to enable power and ahb clock.
	 */
	ret = dsi_phy_enable_resource(phy);
	if (ret)
		return ret;

	if (phy->cfg->ops.pll_init) {
		ret = phy->cfg->ops.pll_init(phy);
		if (ret)
			return dev_err_probe(dev, ret,
					     "PLL init failed; need separate clk driver\n");
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
				     phy->provided_clocks);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register clk provider\n");

	dsi_phy_disable_resource(phy);

	platform_set_drvdata(pdev, phy);

	return 0;
}

static struct platform_driver dsi_phy_platform_driver = {
	.probe      = dsi_phy_driver_probe,
	.driver     = {
		.name   = "msm_dsi_phy",
		.of_match_table = dsi_phy_dt_match,
	},
};

void __init msm_dsi_phy_driver_register(void)
{
	platform_driver_register(&dsi_phy_platform_driver);
}

void __exit msm_dsi_phy_driver_unregister(void)
{
	platform_driver_unregister(&dsi_phy_platform_driver);
}

int msm_dsi_phy_enable(struct msm_dsi_phy *phy,
			struct msm_dsi_phy_clk_request *clk_req,
			struct msm_dsi_phy_shared_timings *shared_timings)
{
	struct device *dev;
	int ret;

	if (!phy || !phy->cfg->ops.enable)
		return -EINVAL;

	dev = &phy->pdev->dev;

	ret = dsi_phy_enable_resource(phy);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: resource enable failed, %d\n",
			__func__, ret);
		goto res_en_fail;
	}

	ret = regulator_bulk_enable(phy->cfg->num_regulators, phy->supplies);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: regulator enable failed, %d\n",
			__func__, ret);
		goto reg_en_fail;
	}

	ret = phy->cfg->ops.enable(phy, clk_req);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: phy enable failed, %d\n", __func__, ret);
		goto phy_en_fail;
	}

	memcpy(shared_timings, &phy->timing.shared_timings,
	       sizeof(*shared_timings));

	/*
	 * Resetting DSI PHY silently changes its PLL registers to reset status,
	 * which will confuse clock driver and result in wrong output rate of
	 * link clocks. Restore PLL status if its PLL is being used as clock
	 * source.
	 */
	if (phy->usecase != MSM_DSI_PHY_SLAVE) {
		ret = msm_dsi_phy_pll_restore_state(phy);
		if (ret) {
			DRM_DEV_ERROR(dev, "%s: failed to restore phy state, %d\n",
				__func__, ret);
			goto pll_restor_fail;
		}
	}

	return 0;

pll_restor_fail:
	if (phy->cfg->ops.disable)
		phy->cfg->ops.disable(phy);
phy_en_fail:
	regulator_bulk_disable(phy->cfg->num_regulators, phy->supplies);
reg_en_fail:
	dsi_phy_disable_resource(phy);
res_en_fail:
	return ret;
}

void msm_dsi_phy_disable(struct msm_dsi_phy *phy)
{
	if (!phy || !phy->cfg->ops.disable)
		return;

	phy->cfg->ops.disable(phy);

	regulator_bulk_disable(phy->cfg->num_regulators, phy->supplies);
	dsi_phy_disable_resource(phy);
}

void msm_dsi_phy_set_usecase(struct msm_dsi_phy *phy,
			     enum msm_dsi_phy_usecase uc)
{
	if (phy)
		phy->usecase = uc;
}

/* Returns true if we have to clear DSI_LANE_CTRL.HS_REQ_SEL_PHY */
bool msm_dsi_phy_set_continuous_clock(struct msm_dsi_phy *phy, bool enable)
{
	if (!phy || !phy->cfg->ops.set_continuous_clock)
		return false;

	return phy->cfg->ops.set_continuous_clock(phy, enable);
}

void msm_dsi_phy_pll_save_state(struct msm_dsi_phy *phy)
{
	if (phy->cfg->ops.save_pll_state) {
		phy->cfg->ops.save_pll_state(phy);
		phy->state_saved = true;
	}
}

int msm_dsi_phy_pll_restore_state(struct msm_dsi_phy *phy)
{
	int ret;

	if (phy->cfg->ops.restore_pll_state && phy->state_saved) {
		ret = phy->cfg->ops.restore_pll_state(phy);
		if (ret)
			return ret;

		phy->state_saved = false;
	}

	return 0;
}

void msm_dsi_phy_snapshot(struct msm_disp_state *disp_state, struct msm_dsi_phy *phy)
{
	msm_disp_snapshot_add_block(disp_state,
			phy->base_size, phy->base,
			"dsi%d_phy", phy->id);

	/* Do not try accessing PLL registers if it is switched off */
	if (phy->pll_on)
		msm_disp_snapshot_add_block(disp_state,
			phy->pll_size, phy->pll_base,
			"dsi%d_pll", phy->id);

	if (phy->lane_base)
		msm_disp_snapshot_add_block(disp_state,
			phy->lane_size, phy->lane_base,
			"dsi%d_lane", phy->id);

	if (phy->reg_base)
		msm_disp_snapshot_add_block(disp_state,
			phy->reg_size, phy->reg_base,
			"dsi%d_reg", phy->id);
}
