// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "lib_float_math.h"
#include "lib_frl_cap_check.h"

#define frl_dump_var(fmt, var) {}
#define frl_print(fmt, ...) {}

static const double   EPSILON = 0.01;
static const double   DBL_EPSILON = 2.2204460492503131e-16;
static const int      C_FRL_CB = 510;
static const double   OVERHEAD_M = 0.003;  /* %   */
static const double   TOLERANCE_PIXEL_CLOCK = 0.005;  /* %   */
static const double   TOLERANCE_AUDIO_CLOCK = 1000;   /* ppm */
static const int      TOLERANCE_FRL_BIT = 300;    /* ppm */
static const int      ACR_RATE_MAX = 1500;
const int             DML2_FRL_CHK_TB_BORROWED_MAX = 400;

static enum lib_frl_cap_check_status frl_cap_check_common(struct lib_frl_cap_check_intermediates *inter, struct lib_frl_cap_check_params *params)
{
	double   audio_bw_reserve = (params->compressed ? 192000.0 : 0.0);
	/*
		if (getenv("DEBUG_FRL_CAP_CHK"))
		{
			printf("frl_cap_chk inputs:\n");
			printf("-------------------\n");
			frl_dump_var("%i", params->lanes);
			frl_dump_var("%le", params->f_pixel_clock_nominal);
			frl_dump_var("%le", params->r_bit_nominal);
			frl_dump_var("%i", params->audio_packet_type);
			frl_dump_var("%le", params->f_audio);
			frl_dump_var("%i", params->h_active);
			frl_dump_var("%i", params->h_blank);
			frl_dump_var("%i", params->bpc);
			frl_dump_var("%i", params->pixel_encoding);
			frl_dump_var("%i", params->compressed);
			frl_dump_var("%i", params->slices);
			frl_dump_var("%i", params->slice_width);
			frl_dump_var("%le", params->bpp_target);
			frl_dump_var("%i", params->layout);
			frl_dump_var("%i", params->acat);
			printf("frl_cap_chk outputs:\n");
			printf("---------------------\n");
		}
	*/
	inter->c_frl_sb = 4 * C_FRL_CB + params->lanes;
	inter->overhead_sb = (double)params->lanes / inter->c_frl_sb;
	inter->overhead_rs = 8.0 * 4.0 / inter->c_frl_sb;
	inter->overhead_map = 2.5 / inter->c_frl_sb;
	inter->overhead_min = inter->overhead_sb + inter->overhead_rs + inter->overhead_map;
	inter->overhead_max = inter->overhead_min + OVERHEAD_M;
	inter->f_pixel_clock_max = params->f_pixel_clock_nominal * (1.0 + TOLERANCE_PIXEL_CLOCK);
	inter->t_line = (params->h_active + params->h_blank) / inter->f_pixel_clock_max;
	inter->r_bit_min = params->r_bit_nominal * (1.0 - TOLERANCE_FRL_BIT / 1000000.0);
	inter->r_frl_char_min = inter->r_bit_min / 18.0;
	inter->c_frl_line = math_floor(inter->t_line * inter->r_frl_char_min * params->lanes);
	/*
		if (getenv("DEBUG_FRL_CAP_CHK"))
		{
			frl_dump_var("%i", inter->c_frl_sb);
			frl_dump_var("%le", inter->overhead_sb);
			frl_dump_var("%le", inter->overhead_rs);
			frl_dump_var("%le", inter->overhead_map);
			frl_dump_var("%le", inter->overhead_min);
			frl_dump_var("%le", inter->overhead_max);
			frl_dump_var("%le", inter->f_pixel_clock_max);
			frl_dump_var("%le", inter->t_line);
			frl_dump_var("%le", inter->r_bit_min);
			frl_dump_var("%le", inter->r_frl_char_min);
			frl_dump_var("%le", inter->c_frl_line);
		}
	*/
	switch (params->audio_packet_type) {
	case 0x02:
		/* unsupported
	case 0x07:
		*/
		if (params->layout == 0)
			inter->ap = 0.25;
		else if (params->layout == 1)
			inter->ap = 1.0;
		break;
	case 0x08:
		inter->ap = 0.25;
		break;
	case 0x09:
		/* unsupported
	case 0x0e:
	case 0x0f:
		*/
		inter->ap = 1.0;
		break;
		/* unsupported
	case 0x0b:
	case 0x0c:
		if (acat == 0x01)
			ap = 2.0;
		else if (acat == 0x02)
			ap = 3.0;
		else if (acat == 0x03)
			ap = 4.0;
		break;
		*/
	case 0x07:
	case 0x0e:
	case 0x0f:
	case 0x0b:
	case 0x0c:
		// Unsupported audio format
		return LIB_FRL_CAP_CHECK_ERROR_UNSUPPORTED_AUDIO;
	default:
		inter->ap = 0.0;
	}

	inter->r_ap = (math_max2(audio_bw_reserve, params->f_audio * inter->ap) + 2 * ACR_RATE_MAX) * (1 + TOLERANCE_AUDIO_CLOCK / 1000000.0);
	inter->avg_audio_packets_line = inter->r_ap * inter->t_line;
	inter->audio_packets_line = (int)math_ceil(inter->avg_audio_packets_line);
	inter->blank_audio_min = 32 + 32 * inter->audio_packets_line; // h_blank_audio_min or hc_blank_audio_min

	params->audio_packets_line = inter->audio_packets_line;
	/*
		if (getenv("DEBUG_FRL_CAP_CHK"))
		{
			frl_dump_var("%le", inter->ap);
			frl_dump_var("%le", inter->r_ap);
			frl_dump_var("%le", inter->avg_audio_packets_line);
			frl_dump_var("%i", inter->audio_packets_line);
			frl_dump_var("%i", inter->blank_audio_min);
		}
	*/
	return LIB_FRL_CAP_CHECK_OK;
}


static enum lib_frl_cap_check_status frl_cap_check_uncompressed(struct lib_frl_cap_check_params *params, struct lib_frl_cap_check_intermediates *inter)
{
	enum lib_frl_cap_check_status res;

	int k_420;
	double k_cd;
	int c_frl_free;
	int c_frl_rc_margin;
	int c_frl_rc_savings;
	int bpp;
	double bytes_line;
	int tb_active;
	int tb_blank;
	double f_tb_average;
	double t_active_ref;
	double t_blank_ref;
	double t_active_min;
	double t_blank_min;
	double t_borrowed;
	double tb_borrowed;
	int c_frl_actual_payload;
	double utilization;
	double margin;

	res = frl_cap_check_common(inter, params);
	if (res != LIB_FRL_CAP_CHECK_OK) {
		return res;
	}

	k_420 = params->pixel_encoding == LIB_FRL_CAP_CHECK_PIXEL_ENCODING_420 ? 2 : 1;
	k_cd = params->pixel_encoding == LIB_FRL_CAP_CHECK_PIXEL_ENCODING_422 ? 1.0 : params->bpc / 8.0;
	c_frl_free = (int)math_max2(params->h_blank * k_cd / k_420 - 32 * (1 + inter->audio_packets_line) - 7, 0);
	c_frl_rc_margin = 4;
	c_frl_rc_savings = (int)math_floor(math_max2(((7.0 / 8.0) * c_frl_free) - c_frl_rc_margin, 0.0));
	bpp = (int)(24 * k_cd / k_420);
	bytes_line = bpp * params->h_active / 8.0;
	tb_active = (int)math_ceil(bytes_line / 3);
	tb_blank = (int)math_ceil(params->h_blank * k_cd / k_420);
	/*
		if (getenv("DEBUG_FRL_CAP_CHK"))
		{
			frl_dump_var("%i", k_420);
			frl_dump_var("%le", k_cd);
			frl_dump_var("%i", c_frl_free);
			frl_dump_var("%i", c_frl_rc_margin);
			frl_dump_var("%i", c_frl_rc_savings);
			frl_dump_var("%i", bpp);
			frl_dump_var("%le", bytes_line);
			frl_dump_var("%i", tb_active);
			frl_dump_var("%i", tb_blank);
		}
	*/
	if (!(inter->blank_audio_min <= tb_blank)) {
		frl_dump_var("%i", inter->blank_audio_min);
		frl_dump_var("%i", tb_blank);
		return LIB_FRL_CAP_CHECK_ERROR_AUDIO_BW;
	}

	f_tb_average = (inter->f_pixel_clock_max / (params->h_active + params->h_blank)) * (tb_active + tb_blank);
	t_active_ref = inter->t_line * ((double)params->h_active / (params->h_active + params->h_blank));
	t_blank_ref = inter->t_line * ((double)params->h_blank / (params->h_active + params->h_blank));
	t_active_min = (3.0 / 2.0) * tb_active / (params->lanes * inter->r_frl_char_min * (1.0 - inter->overhead_max));
	t_blank_min = tb_blank / (params->lanes * inter->r_frl_char_min * (1.0 - inter->overhead_max));
	/*
		if (getenv("DEBUG_FRL_CAP_CHK"))
		{
			frl_dump_var("%le", f_tb_average);
			frl_dump_var("%le", t_active_ref);
			frl_dump_var("%le", t_blank_ref);
			frl_dump_var("%le", t_active_min);
			frl_dump_var("%le", t_blank_min);
		}
	*/
	if ((t_active_ref >= t_active_min) && (t_blank_ref >= t_blank_min)) {
		t_borrowed = 0;
		params->borrow_mode = LIB_FRL_CAP_CHECK_BORROW_MODE_NONE;
	} else if ((t_active_ref < t_active_min) && (t_blank_ref >= t_blank_min)) {
		t_borrowed = t_active_min - t_active_ref;
		params->borrow_mode = LIB_FRL_CAP_CHECK_BORROW_MODE_FROM_BLANK;
	} else
		return LIB_FRL_CAP_CHECK_ERROR_BORROW;

	tb_borrowed = math_ceil(t_borrowed * f_tb_average);
	/*
		if (getenv("DEBUG_FRL_CAP_CHK"))
		{
			frl_dump_var("%le", tb_borrowed);
			frl_dump_var("%i", params->borrow_mode);
	}
	*/
	if (!(tb_borrowed <= DML2_FRL_CHK_TB_BORROWED_MAX))
		return LIB_FRL_CAP_CHECK_ERROR_MAX_BORROW;

	c_frl_actual_payload = (int)math_ceil((3.0 / 2.0) * tb_active) + tb_blank - c_frl_rc_savings;
	utilization = c_frl_actual_payload / inter->c_frl_line;
	margin = 1.0 - (utilization + inter->overhead_max);
	/*
		if (getenv("DEBUG_FRL_CAP_CHK"))
		{
			frl_dump_var("%i",  c_frl_actual_payload);
			frl_dump_var("%le", utilization);
			frl_dump_var("%le", margin);
		}
	*/
	if (margin < 0 && math_fabs(margin) > EPSILON)
		return LIB_FRL_CAP_CHECK_ERROR_MARGIN;

	return LIB_FRL_CAP_CHECK_OK;
}

static enum lib_frl_cap_check_status frl_cap_check_compressed(struct lib_frl_cap_check_params *params, struct lib_frl_cap_check_intermediates *inter)
{
	enum lib_frl_cap_check_status res;

	int      c_frl_available;
	int      c_frl_active_available;
	int      c_frl_blank_available;
	int      bytes_target;
	int      hc_active_target;
	int      hc_blank_target_est1;
	int      hc_blank_target_est2;
	int      hc_blank_target;
	double   f_tb_average;
	double   t_active_ref;
	double   t_blank_ref;
	double   t_active_target;
	double   t_blank_target;
	double   tb_borrowed;
	int      c_frl_actual_target_payload;
	double   utilization_targeted;
	double   margin_target;
#if defined(DEBUG_FRL_CAP_CHK)
	double   tb_delta;
	double   tb_delta_limit;
	int      tb_worst;
#endif

	res = frl_cap_check_common(inter, params);
	if (res != LIB_FRL_CAP_CHECK_OK)
		return res;

	c_frl_available = (int)math_floor((1 - inter->overhead_max) * inter->c_frl_line);
	c_frl_active_available = (int)math_floor(c_frl_available * ((double)params->h_active / (params->h_active + params->h_blank)));
	(void)c_frl_active_available;
	c_frl_blank_available = (int)math_floor(c_frl_available * ((double)params->h_blank / (params->h_active + params->h_blank)));
	(void)c_frl_blank_available;
	bytes_target = params->slices * (int)math_ceil(params->bpp_target * params->slice_width / 8.0);

	if (!params->bypass_hc_target_calc)
		hc_active_target = (int)math_ceil(bytes_target / 3.0);
	else
		hc_active_target = params->hc_active_target;

	hc_blank_target_est1 = (int)math_ceil(hc_active_target * ((double)params->h_blank / params->h_active));
	hc_blank_target_est2 = (int)math_max2(hc_blank_target_est1, inter->blank_audio_min);

	if (!params->bypass_hc_target_calc) {
		hc_blank_target = 4 * (int)math_floor(math_min2(hc_blank_target_est2, c_frl_available - 3.0 / 2.0 * hc_active_target) / 4.0);

		params->hc_active_target = hc_active_target;
		params->hc_blank_target = hc_blank_target;
	} else {
		hc_blank_target = params->hc_blank_target;
	}
	/*
		if (getenv("DEBUG_FRL_CAP_CHK"))
		{
			frl_dump_var("%i", c_frl_available);
			frl_dump_var("%i", c_frl_active_available);
			frl_dump_var("%i", c_frl_blank_available);
			frl_dump_var("%i", bytes_target);
			frl_dump_var("%i", hc_active_target);
			frl_dump_var("%i", hc_blank_target_est1);
			frl_dump_var("%i", hc_blank_target_est2);
			frl_dump_var("%i", hc_blank_target);
		}
	*/
	if (!(inter->blank_audio_min <= hc_blank_target)) {
		frl_dump_var("%i", inter->blank_audio_min);
		frl_dump_var("%i", hc_blank_target);
		return LIB_FRL_CAP_CHECK_ERROR_AUDIO_BW;
	}

	f_tb_average = inter->f_pixel_clock_max / (params->h_active + params->h_blank) * (hc_active_target + hc_blank_target);
	t_active_ref = inter->t_line * ((double)params->h_active / (params->h_active + params->h_blank));
	t_blank_ref = inter->t_line - t_active_ref; // * ((double) params->h_blank / (params->h_active + params->h_blank));
	t_active_target = math_max2((hc_active_target / f_tb_average), (3.0 / 2.0 * hc_active_target) / (params->lanes * inter->r_frl_char_min * (1.0 - inter->overhead_max)));
	t_blank_target = inter->t_line - t_active_target;

	tb_borrowed = t_active_target * f_tb_average - hc_active_target;
#if defined(DEBUG_FRL_CAP_CHK)
	tb_delta = math_fabs(t_active_target - t_active_ref) * (hc_active_target + hc_blank_target_est1) / inter->t_line;
		{
			frl_dump_var("%le", f_tb_average);
			frl_dump_var("%le", t_active_ref);
			frl_dump_var("%le", t_blank_ref);
			frl_dump_var("%le", t_active_target);
			frl_dump_var("%le", t_blank_target);
			frl_dump_var("%le", tb_delta);
		}
#endif
	if (t_blank_target - t_blank_ref > DBL_EPSILON) {
#if defined(DEBUG_FRL_CAP_CHK)
		tb_delta_limit = (t_active_ref - hc_active_target / f_tb_average) * (hc_active_target + hc_blank_target_est1) / inter->t_line;
#endif
		params->borrow_mode = LIB_FRL_CAP_CHECK_BORROW_MODE_FROM_ACTIVE;
	} else if (t_active_target - t_active_ref > DBL_EPSILON) {
#if defined(DEBUG_FRL_CAP_CHK)
		tb_delta_limit = tb_delta;
#endif
		params->borrow_mode = LIB_FRL_CAP_CHECK_BORROW_MODE_FROM_BLANK;
	} else {
#if defined(DEBUG_FRL_CAP_CHK)
		tb_delta_limit = 0;
#endif
		params->borrow_mode = LIB_FRL_CAP_CHECK_BORROW_MODE_NONE;
	}

#if defined(DEBUG_FRL_CAP_CHK)
	tb_worst = (int)math_ceil(math_max2(tb_borrowed, tb_delta_limit));

		{
			frl_dump_var("%le", tb_delta_limit);
			frl_dump_var("%le", tb_borrowed);
			frl_dump_var("%i", params->borrow_mode);
			frl_dump_var("%i", tb_worst);
		}
#endif
	if (!(tb_borrowed <= DML2_FRL_CHK_TB_BORROWED_MAX))
		return LIB_FRL_CAP_CHECK_ERROR_MAX_BORROW;

	c_frl_actual_target_payload = (int)math_ceil(3.0 / 2.0 * hc_active_target) + hc_blank_target;
	utilization_targeted = c_frl_actual_target_payload / inter->c_frl_line;
	margin_target = 1.0 - (utilization_targeted + inter->overhead_max);
#if defined(DEBUG_FRL_CAP_CHK)
		{
			frl_dump_var("%i", c_frl_actual_target_payload);
			frl_dump_var("%le", utilization_targeted);
			frl_dump_var("%le", margin_target);
		}
#endif
	// oversubscribed bandwidth relative to margin
	if (margin_target < 0 && math_fabs(margin_target) > EPSILON)
		return LIB_FRL_CAP_CHECK_ERROR_MARGIN;

	return LIB_FRL_CAP_CHECK_OK;
}

enum lib_frl_cap_check_status frl_cap_check(struct lib_frl_cap_check_params *params)
{
	struct lib_frl_cap_check_intermediates inter;
	return frl_cap_check_intermediates(params, &inter);
}

enum lib_frl_cap_check_status frl_cap_check_intermediates(struct lib_frl_cap_check_params *params, struct lib_frl_cap_check_intermediates *inter)
{
	if (params->compressed)
		return frl_cap_check_compressed(params, inter);
	return frl_cap_check_uncompressed(params, inter);
}
