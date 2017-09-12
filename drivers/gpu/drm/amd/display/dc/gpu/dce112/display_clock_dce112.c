/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"

#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"

#include "include/bios_parser_interface.h"
#include "include/fixed32_32.h"
#include "include/logger_interface.h"

#include "../divider_range.h"

#include "display_clock_dce112.h"

#define FROM_DISPLAY_CLOCK(base) \
	container_of(base, struct display_clock_dce112, disp_clk_base)

static struct state_dependent_clocks max_clks_by_state[] = {
/*ClocksStateInvalid - should not be used*/
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/*ClocksStateUltraLow - currently by HW design team not supposed to be used*/
{ .display_clk_khz = 389189, .pixel_clk_khz = 346672 },
/*ClocksStateLow*/
{ .display_clk_khz = 459000, .pixel_clk_khz = 400000 },
/*ClocksStateNominal*/
{ .display_clk_khz = 667000, .pixel_clk_khz = 600000 },
/*ClocksStatePerformance*/
{ .display_clk_khz = 1132000, .pixel_clk_khz = 600000 } };

/* Ranges for divider identifiers (Divider ID or DID)
 mmDENTIST_DISPCLK_CNTL.DENTIST_DISPCLK_WDIVIDER*/
enum divider_id_register_setting {
	DIVIDER_RANGE_01_BASE_DIVIDER_ID = 0X08,
	DIVIDER_RANGE_02_BASE_DIVIDER_ID = 0X40,
	DIVIDER_RANGE_03_BASE_DIVIDER_ID = 0X60,
	DIVIDER_RANGE_MAX_DIVIDER_ID = 0X80
};

/* Step size between each divider within a range.
 Incrementing the DENTIST_DISPCLK_WDIVIDER by one
 will increment the divider by this much.*/
enum divider_range_step_size {
	DIVIDER_RANGE_01_STEP_SIZE = 25, /* 0.25*/
	DIVIDER_RANGE_02_STEP_SIZE = 50, /* 0.50*/
	DIVIDER_RANGE_03_STEP_SIZE = 100 /* 1.00 */
};

static struct divider_range divider_ranges[DIVIDER_RANGE_MAX];

#define dce112_DFS_BYPASS_THRESHOLD_KHZ 400000
/*****************************************************************************
 * static functions
 *****************************************************************************/

/*
 * store_max_clocks_state
 *
 * @brief
 * Cache the clock state
 *
 * @param
 * struct display_clock *base - [out] cach the state in this structure
 * enum clocks_state max_clocks_state - [in] state to be stored
 */
void dispclk_dce112_store_max_clocks_state(
	struct display_clock *base,
	enum clocks_state max_clocks_state)
{
	struct display_clock_dce112 *dc = DCLCK112_FROM_BASE(base);

	switch (max_clocks_state) {
	case CLOCKS_STATE_LOW:
	case CLOCKS_STATE_NOMINAL:
	case CLOCKS_STATE_PERFORMANCE:
	case CLOCKS_STATE_ULTRA_LOW:
		dc->max_clks_state = max_clocks_state;
		break;

	case CLOCKS_STATE_INVALID:
	default:
		/*Invalid Clocks State!*/
		ASSERT_CRITICAL(false);
		break;
	}
}

enum clocks_state dispclk_dce112_get_min_clocks_state(
	struct display_clock *base)
{
	return base->cur_min_clks_state;
}

bool dispclk_dce112_set_min_clocks_state(
	struct display_clock *base,
	enum clocks_state clocks_state)
{
	struct display_clock_dce112 *dc = DCLCK112_FROM_BASE(base);
	struct dm_pp_power_level_change_request level_change_req = {
			DM_PP_POWER_LEVEL_INVALID};

	if (clocks_state > dc->max_clks_state) {
		/*Requested state exceeds max supported state.*/
		dm_logger_write(base->ctx->logger, LOG_WARNING,
				"Requested state exceeds max supported state");
		return false;
	} else if (clocks_state == base->cur_min_clks_state) {
		/*if we're trying to set the same state, we can just return
		 * since nothing needs to be done*/
		return true;
	}

	switch (clocks_state) {
	case CLOCKS_STATE_ULTRA_LOW:
		level_change_req.power_level = DM_PP_POWER_LEVEL_ULTRA_LOW;
		break;
	case CLOCKS_STATE_LOW:
		level_change_req.power_level = DM_PP_POWER_LEVEL_LOW;
		break;
	case CLOCKS_STATE_NOMINAL:
		level_change_req.power_level = DM_PP_POWER_LEVEL_NOMINAL;
		break;
	case CLOCKS_STATE_PERFORMANCE:
		level_change_req.power_level = DM_PP_POWER_LEVEL_PERFORMANCE;
		break;
	case CLOCKS_STATE_INVALID:
	default:
		dm_logger_write(base->ctx->logger, LOG_WARNING,
				"Requested state invalid state");
		return false;
	}

	/* get max clock state from PPLIB */
	if (dm_pp_apply_power_level_change_request(
			base->ctx, &level_change_req))
		base->cur_min_clks_state = clocks_state;

	return true;
}

static uint32_t get_dp_ref_clk_frequency(struct display_clock *dc)
{
	uint32_t dispclk_cntl_value;
	uint32_t dp_ref_clk_cntl_value;
	uint32_t dp_ref_clk_cntl_src_sel_value;
	uint32_t dp_ref_clk_khz = 600000;
	uint32_t target_div = INVALID_DIVIDER;
	struct display_clock_dce112 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	/* ASSERT DP Reference Clock source is from DFS*/
	dp_ref_clk_cntl_value = dm_read_reg(dc->ctx,
			mmDPREFCLK_CNTL);

	dp_ref_clk_cntl_src_sel_value =
			get_reg_field_value(
				dp_ref_clk_cntl_value,
				DPREFCLK_CNTL, DPREFCLK_SRC_SEL);

	ASSERT(dp_ref_clk_cntl_src_sel_value == 0);

	/* Read the mmDENTIST_DISPCLK_CNTL to get the currently
	 * programmed DID DENTIST_DPREFCLK_WDIVIDER*/
	dispclk_cntl_value = dm_read_reg(dc->ctx,
			mmDENTIST_DISPCLK_CNTL);

	/* Convert DENTIST_DPREFCLK_WDIVIDERto actual divider*/
	target_div = dal_divider_range_get_divider(
		divider_ranges,
		DIVIDER_RANGE_MAX,
		get_reg_field_value(dispclk_cntl_value,
			DENTIST_DISPCLK_CNTL,
			DENTIST_DPREFCLK_WDIVIDER));

	if (target_div != INVALID_DIVIDER) {
		/* Calculate the current DFS clock, in kHz.*/
		dp_ref_clk_khz = (DIVIDER_RANGE_SCALE_FACTOR
			* disp_clk->dentist_vco_freq_khz) / target_div;
	}

	/* SW will adjust DP REF Clock average value for all purposes
	 * (DP DTO / DP Audio DTO and DP GTC)
	 if clock is spread for all cases:
	 -if SS enabled on DP Ref clock and HW de-spreading enabled with SW
	 calculations for DS_INCR/DS_MODULO (this is planned to be default case)
	 -if SS enabled on DP Ref clock and HW de-spreading enabled with HW
	 calculations (not planned to be used, but average clock should still
	 be valid)
	 -if SS enabled on DP Ref clock and HW de-spreading disabled
	 (should not be case with CIK) then SW should program all rates
	 generated according to average value (case as with previous ASICs)
	  */
	if ((disp_clk->ss_on_gpu_pll) && (disp_clk->gpu_pll_ss_divider != 0)) {
		struct fixed32_32 ss_percentage = dal_fixed32_32_div_int(
				dal_fixed32_32_from_fraction(
					disp_clk->gpu_pll_ss_percentage,
					disp_clk->gpu_pll_ss_divider), 200);
		struct fixed32_32 adj_dp_ref_clk_khz;

		ss_percentage = dal_fixed32_32_sub(dal_fixed32_32_one,
								ss_percentage);
		adj_dp_ref_clk_khz =
			dal_fixed32_32_mul_int(
				ss_percentage,
				dp_ref_clk_khz);
		dp_ref_clk_khz = dal_fixed32_32_floor(adj_dp_ref_clk_khz);
	}

	return dp_ref_clk_khz;
}

void dispclk_dce112_destroy(struct display_clock **base)
{
	struct display_clock_dce112 *dc112;

	dc112 = DCLCK112_FROM_BASE(*base);

	dm_free(dc112);

	*base = NULL;
}

uint32_t dispclk_dce112_get_validation_clock(struct display_clock *dc)
{
	uint32_t clk = 0;
	struct display_clock_dce112 *disp_clk = DCLCK112_FROM_BASE(dc);

	switch (disp_clk->max_clks_state) {
	case CLOCKS_STATE_ULTRA_LOW:
		clk = (disp_clk->max_clks_by_state + CLOCKS_STATE_ULTRA_LOW)->
			display_clk_khz;

	case CLOCKS_STATE_LOW:
		clk = (disp_clk->max_clks_by_state + CLOCKS_STATE_LOW)->
			display_clk_khz;
		break;

	case CLOCKS_STATE_NOMINAL:
		clk = (disp_clk->max_clks_by_state + CLOCKS_STATE_NOMINAL)->
			display_clk_khz;
		break;

	case CLOCKS_STATE_PERFORMANCE:
		clk = (disp_clk->max_clks_by_state + CLOCKS_STATE_PERFORMANCE)->
			display_clk_khz;
		break;

	case CLOCKS_STATE_INVALID:
	default:
		/*Invalid Clocks State*/
		dm_logger_write(dc->ctx->logger, LOG_WARNING,
				"Invalid clock state");
		/* just return the display engine clock for
		 * lowest supported state*/
		clk = (disp_clk->max_clks_by_state + CLOCKS_STATE_LOW)->
				display_clk_khz;
		break;
	}
	return clk;
}

static struct fixed32_32 get_deep_color_factor(struct min_clock_params *params)
{
	/* DeepColorFactor = IF (HDMI = True, bpp / 24, 1)*/
	struct fixed32_32 deep_color_factor = dal_fixed32_32_from_int(1);

	if (params->signal_type != SIGNAL_TYPE_HDMI_TYPE_A)
		return deep_color_factor;

	switch (params->deep_color_depth) {
	case COLOR_DEPTH_101010:
		/*deep color ratio for 30bpp is 30/24 = 1.25*/
		deep_color_factor = dal_fixed32_32_from_fraction(30, 24);
		break;

	case COLOR_DEPTH_121212:
		/* deep color ratio for 36bpp is 36/24 = 1.5*/
		deep_color_factor = dal_fixed32_32_from_fraction(36, 24);
		break;

	case COLOR_DEPTH_161616:
		/* deep color ratio for 48bpp is 48/24 = 2.0 */
		deep_color_factor = dal_fixed32_32_from_fraction(48, 24);
		break;
	default:
		break;
	}
	return deep_color_factor;
}

static struct fixed32_32 get_scaler_efficiency(
	struct dc_context *ctx,
	struct min_clock_params *params)
{
	struct fixed32_32 scaler_efficiency = dal_fixed32_32_from_int(3);

	if (params->scaler_efficiency == V_SCALER_EFFICIENCY_LB18BPP) {
		scaler_efficiency =
			dal_fixed32_32_add(
				dal_fixed32_32_from_fraction(35555, 10000),
				dal_fixed32_32_from_fraction(
					55556,
					100000 * 10000));
	} else if (params->scaler_efficiency == V_SCALER_EFFICIENCY_LB24BPP) {
		scaler_efficiency =
			dal_fixed32_32_add(
				dal_fixed32_32_from_fraction(34285, 10000),
				dal_fixed32_32_from_fraction(
					71429,
					100000 * 10000));
	} else if (params->scaler_efficiency == V_SCALER_EFFICIENCY_LB30BPP)
		scaler_efficiency = dal_fixed32_32_from_fraction(32, 10);

	return scaler_efficiency;
}

static struct fixed32_32 get_lb_lines_in_per_line_out(
		struct min_clock_params *params,
		struct fixed32_32 v_scale_ratio)
{
	struct fixed32_32 two = dal_fixed32_32_from_int(2);
	struct fixed32_32 four = dal_fixed32_32_from_int(4);
	struct fixed32_32 f4_to_3 = dal_fixed32_32_from_fraction(4, 3);
	struct fixed32_32 f6_to_4 = dal_fixed32_32_from_fraction(6, 4);

	if (params->line_buffer_prefetch_enabled)
		return dal_fixed32_32_max(v_scale_ratio, dal_fixed32_32_one);
	else if (dal_fixed32_32_le(v_scale_ratio, dal_fixed32_32_one))
		return dal_fixed32_32_one;
	else if (dal_fixed32_32_le(v_scale_ratio, f4_to_3))
		return f4_to_3;
	else if (dal_fixed32_32_le(v_scale_ratio, f6_to_4))
		return f6_to_4;
	else if (dal_fixed32_32_le(v_scale_ratio, two))
		return two;
	else if (dal_fixed32_32_le(v_scale_ratio, dal_fixed32_32_from_int(3)))
		return four;
	else
		return dal_fixed32_32_zero;
}

static uint32_t get_actual_required_display_clk(
	struct display_clock_dce112 *disp_clk,
	uint32_t target_clk_khz)
{
	uint32_t disp_clk_khz = target_clk_khz;
	uint32_t div = INVALID_DIVIDER;
	uint32_t did = INVALID_DID;
	uint32_t scaled_vco =
		disp_clk->dentist_vco_freq_khz * DIVIDER_RANGE_SCALE_FACTOR;

	ASSERT_CRITICAL(!!disp_clk_khz);

	if (disp_clk_khz)
		div = scaled_vco / disp_clk_khz;

	did = dal_divider_range_get_did(divider_ranges, DIVIDER_RANGE_MAX, div);

	if (did != INVALID_DID) {
		div = dal_divider_range_get_divider(
			divider_ranges, DIVIDER_RANGE_MAX, did);

		if ((div != INVALID_DIVIDER) &&
			(did > DIVIDER_RANGE_01_BASE_DIVIDER_ID))
			if (disp_clk_khz > (scaled_vco / div))
				div = dal_divider_range_get_divider(
					divider_ranges, DIVIDER_RANGE_MAX,
					did - 1);

		if (div != INVALID_DIVIDER)
			disp_clk_khz = scaled_vco / div;

	}
	/* We need to add 10KHz to this value because the accuracy in VBIOS is
	 in 10KHz units. So we need to always round the last digit up in order
	 to reach the next div level.*/
	return disp_clk_khz + 10;
}

static uint32_t calc_single_display_min_clks(
	struct display_clock *base,
	struct min_clock_params *params,
	bool set_clk)
{
	struct fixed32_32 h_scale_ratio = dal_fixed32_32_one;
	struct fixed32_32 v_scale_ratio = dal_fixed32_32_one;
	uint32_t pix_clk_khz = 0;
	uint32_t lb_source_width = 0;
	struct fixed32_32 deep_color_factor;
	struct fixed32_32 scaler_efficiency;
	struct fixed32_32 v_filter_init;
	uint32_t v_filter_init_trunc;
	uint32_t num_lines_at_frame_start = 3;
	struct fixed32_32 v_filter_init_ceil;
	struct fixed32_32 lines_per_lines_out_at_frame_start;
	struct fixed32_32 lb_lines_in_per_line_out; /* in middle of the frame*/
	uint32_t src_wdth_rnd_to_chunks;
	struct fixed32_32 scaling_coeff;
	struct fixed32_32 h_blank_granularity_factor =
			dal_fixed32_32_one;
	struct fixed32_32 fx_disp_clk_mhz;
	struct fixed32_32 line_time;
	struct fixed32_32 disp_pipe_pix_throughput;
	struct fixed32_32 fx_alt_disp_clk_mhz;
	uint32_t disp_clk_khz;
	uint32_t alt_disp_clk_khz;
	struct display_clock_dce112 *disp_clk_110 = DCLCK112_FROM_BASE(base);
	uint32_t max_clk_khz = dispclk_dce112_get_validation_clock(base);
	bool panning_allowed = false; /* TODO: receive this value from AS */

	if (params == NULL) {
		dm_logger_write(base->ctx->logger, LOG_WARNING,
				"Invalid input parameter in %s",
				__func__);
		return 0;
	}

	deep_color_factor = get_deep_color_factor(params);
	scaler_efficiency = get_scaler_efficiency(base->ctx, params);
	pix_clk_khz = params->requested_pixel_clock;
	lb_source_width = params->source_view.width;

	if (0 != params->dest_view.height && 0 != params->dest_view.width) {

		h_scale_ratio = dal_fixed32_32_from_fraction(
			params->source_view.width,
			params->dest_view.width);
		v_scale_ratio = dal_fixed32_32_from_fraction(
			params->source_view.height,
			params->dest_view.height);
	} else {
		dm_logger_write(base->ctx->logger, LOG_WARNING,
				"Destination height or width is 0!\n");
	}

	v_filter_init =
		dal_fixed32_32_add(
			v_scale_ratio,
			dal_fixed32_32_add_int(
				dal_fixed32_32_div_int(
					dal_fixed32_32_mul_int(
						v_scale_ratio,
						params->timing_info.INTERLACED),
					2),
				params->scaling_info.v_taps + 1));
	v_filter_init = dal_fixed32_32_div_int(v_filter_init, 2);

	v_filter_init_trunc = dal_fixed32_32_floor(v_filter_init);

	v_filter_init_ceil = dal_fixed32_32_from_fraction(
						v_filter_init_trunc, 2);
	v_filter_init_ceil = dal_fixed32_32_from_int(
		dal_fixed32_32_ceil(v_filter_init_ceil));
	v_filter_init_ceil = dal_fixed32_32_mul_int(v_filter_init_ceil, 2);

	lines_per_lines_out_at_frame_start =
			dal_fixed32_32_div_int(v_filter_init_ceil,
					num_lines_at_frame_start);
	lb_lines_in_per_line_out =
			get_lb_lines_in_per_line_out(params, v_scale_ratio);

	if (panning_allowed)
		src_wdth_rnd_to_chunks =
			((lb_source_width - 1) / 128) * 128 + 256;
	else
		src_wdth_rnd_to_chunks =
			((lb_source_width + 127) / 128) * 128;

	scaling_coeff =
		dal_fixed32_32_div(
			dal_fixed32_32_from_int(params->scaling_info.v_taps),
			scaler_efficiency);

	if (dal_fixed32_32_le(h_scale_ratio, dal_fixed32_32_one))
		scaling_coeff = dal_fixed32_32_max(
			dal_fixed32_32_from_int(
				dal_fixed32_32_ceil(
					dal_fixed32_32_from_fraction(
						params->scaling_info.h_taps,
						4))),
			dal_fixed32_32_max(
				dal_fixed32_32_mul(
					scaling_coeff,
					h_scale_ratio),
				dal_fixed32_32_one));

	if (!params->line_buffer_prefetch_enabled &&
		dal_fixed32_32_floor(lb_lines_in_per_line_out) != 2 &&
		dal_fixed32_32_floor(lb_lines_in_per_line_out) != 4) {
		uint32_t line_total_pixel =
			params->timing_info.h_total + lb_source_width - 256;
		h_blank_granularity_factor = dal_fixed32_32_div(
			dal_fixed32_32_from_int(params->timing_info.h_total),
			dal_fixed32_32_div(
			dal_fixed32_32_from_fraction(
				line_total_pixel, 2),
				h_scale_ratio));
	}

	/* Calculate display clock with ramping. Ramping factor is 1.1*/
	fx_disp_clk_mhz =
		dal_fixed32_32_div_int(
			dal_fixed32_32_mul_int(scaling_coeff, 11),
			10);
	line_time = dal_fixed32_32_from_fraction(
			params->timing_info.h_total * 1000, pix_clk_khz);

	disp_pipe_pix_throughput = dal_fixed32_32_mul(
			lb_lines_in_per_line_out, h_blank_granularity_factor);
	disp_pipe_pix_throughput = dal_fixed32_32_max(
			disp_pipe_pix_throughput,
			lines_per_lines_out_at_frame_start);
	disp_pipe_pix_throughput = dal_fixed32_32_div(dal_fixed32_32_mul_int(
			disp_pipe_pix_throughput, src_wdth_rnd_to_chunks),
			line_time);

	if (0 != params->timing_info.h_total) {
		fx_disp_clk_mhz =
			dal_fixed32_32_max(
				dal_fixed32_32_div_int(
					dal_fixed32_32_mul_int(
						scaling_coeff, pix_clk_khz),
						1000),
				disp_pipe_pix_throughput);
		fx_disp_clk_mhz =
			dal_fixed32_32_mul(
				fx_disp_clk_mhz,
				dal_fixed32_32_from_fraction(11, 10));
	}

	fx_disp_clk_mhz = dal_fixed32_32_max(fx_disp_clk_mhz,
		dal_fixed32_32_mul(deep_color_factor,
		dal_fixed32_32_from_fraction(11, 10)));

	/* Calculate display clock without ramping */
	fx_alt_disp_clk_mhz = scaling_coeff;

	if (0 != params->timing_info.h_total) {
		fx_alt_disp_clk_mhz = dal_fixed32_32_max(
				dal_fixed32_32_div_int(dal_fixed32_32_mul_int(
						scaling_coeff, pix_clk_khz),
						1000),
				dal_fixed32_32_div_int(dal_fixed32_32_mul_int(
						disp_pipe_pix_throughput, 105),
						100));
	}

	if (set_clk && disp_clk_110->ss_on_gpu_pll &&
			disp_clk_110->gpu_pll_ss_divider)
		fx_alt_disp_clk_mhz = dal_fixed32_32_mul(fx_alt_disp_clk_mhz,
				dal_fixed32_32_add_int(
				dal_fixed32_32_div_int(
				dal_fixed32_32_div_int(
				dal_fixed32_32_from_fraction(
				disp_clk_110->gpu_pll_ss_percentage,
				disp_clk_110->gpu_pll_ss_divider), 100),
				2),
				1));

	/* convert to integer */
	disp_clk_khz = dal_fixed32_32_round(
			dal_fixed32_32_mul_int(fx_disp_clk_mhz, 1000));
	alt_disp_clk_khz = dal_fixed32_32_round(
			dal_fixed32_32_mul_int(fx_alt_disp_clk_mhz, 1000));

	if ((disp_clk_khz > max_clk_khz) && (alt_disp_clk_khz <= max_clk_khz))
		disp_clk_khz = alt_disp_clk_khz;

	if (set_clk) { /* only compensate clock if we are going to set it.*/
		disp_clk_khz = get_actual_required_display_clk(
			disp_clk_110, disp_clk_khz);
	}

	disp_clk_khz = disp_clk_khz > max_clk_khz ? max_clk_khz : disp_clk_khz;

	return disp_clk_khz;
}

uint32_t dispclk_dce112_calculate_min_clock(
	struct display_clock *base,
	uint32_t path_num,
	struct min_clock_params *params)
{
	uint32_t i;
	uint32_t validation_clk_khz =
			dispclk_dce112_get_validation_clock(base);
	uint32_t min_clk_khz = validation_clk_khz;
	uint32_t max_clk_khz = 0;
	struct display_clock_dce112 *dc = DCLCK112_FROM_BASE(base);

	if (dc->use_max_disp_clk)
		return min_clk_khz;

	if (params != NULL) {
		uint32_t disp_clk_khz = 0;

		for (i = 0; i < path_num; ++i) {

			disp_clk_khz = calc_single_display_min_clks(
							base, params, true);

			/* update the max required clock found*/
			if (disp_clk_khz > max_clk_khz)
				max_clk_khz = disp_clk_khz;

			params++;
		}
	}

	min_clk_khz = max_clk_khz;

	if (min_clk_khz > validation_clk_khz)
		min_clk_khz = validation_clk_khz;
	else if (min_clk_khz < base->min_display_clk_threshold_khz)
		min_clk_khz = base->min_display_clk_threshold_khz;

	if (dc->use_max_disp_clk)
		min_clk_khz = dispclk_dce112_get_validation_clock(base);

	return min_clk_khz;
}

static bool display_clock_integrated_info_construct(
	struct display_clock_dce112 *disp_clk)
{
	struct integrated_info info;
	uint32_t i;
	struct display_clock *base = &disp_clk->disp_clk_base;

	memset(&info, 0, sizeof(struct integrated_info));

	disp_clk->dentist_vco_freq_khz = info.dentist_vco_freq;
	if (disp_clk->dentist_vco_freq_khz == 0)
		disp_clk->dentist_vco_freq_khz = 3600000;

	disp_clk->crystal_freq_khz = 100000;

	base->min_display_clk_threshold_khz =
		disp_clk->dentist_vco_freq_khz / 64;

	/*update the maximum display clock for each power state*/
	for (i = 0; i < NUMBER_OF_DISP_CLK_VOLTAGE; ++i) {
		enum clocks_state clk_state = CLOCKS_STATE_INVALID;

		switch (i) {
		case 0:
			clk_state = CLOCKS_STATE_ULTRA_LOW;
			break;

		case 1:
			clk_state = CLOCKS_STATE_LOW;
			break;

		case 2:
			clk_state = CLOCKS_STATE_NOMINAL;
			break;

		case 3:
			clk_state = CLOCKS_STATE_PERFORMANCE;
			break;

		default:
			clk_state = CLOCKS_STATE_INVALID;
			break;
		}

		/*Do not allow bad VBIOS/SBIOS to override with invalid values,
		 * check for > 100MHz*/
		if (info.disp_clk_voltage[i].max_supported_clk >= 100000) {
			(disp_clk->max_clks_by_state + clk_state)->
					display_clk_khz =
				info.disp_clk_voltage[i].max_supported_clk;
		}
	}

	return true;
}

static uint32_t get_clock(struct display_clock *dc)
{
	uint32_t disp_clock = dispclk_dce112_get_validation_clock(dc);
	uint32_t target_div = INVALID_DIVIDER;
	uint32_t addr = mmDENTIST_DISPCLK_CNTL;
	uint32_t value = 0;
	uint32_t field = 0;
	struct display_clock_dce112 *disp_clk = DCLCK112_FROM_BASE(dc);

	/* Read the mmDENTIST_DISPCLK_CNTL to get the currently programmed
	 DID DENTIST_DISPCLK_WDIVIDER.*/
	value = dm_read_reg(dc->ctx, addr);
	field = get_reg_field_value(
			value, DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER);

	/* Convert DENTIST_DISPCLK_WDIVIDER to actual divider*/
	target_div = dal_divider_range_get_divider(
		divider_ranges,
		DIVIDER_RANGE_MAX,
		field);

	if (target_div != INVALID_DIVIDER)
		/* Calculate the current DFS clock in KHz.
		 Should be okay up to 42.9 THz before overflowing.*/
		disp_clock = (DIVIDER_RANGE_SCALE_FACTOR
			* disp_clk->dentist_vco_freq_khz) / target_div;
	return disp_clock;
}

enum clocks_state dispclk_dce112_get_required_clocks_state(
	struct display_clock *dc,
	struct state_dependent_clocks *req_clocks)
{
	int32_t i;
	struct display_clock_dce112 *disp_clk = DCLCK112_FROM_BASE(dc);
	enum clocks_state low_req_clk = disp_clk->max_clks_state;

	if (!req_clocks) {
		/* NULL pointer*/
		dm_logger_write(dc->ctx->logger, LOG_WARNING,
				"%s: Invalid parameter",
				__func__);
		return CLOCKS_STATE_INVALID;
	}

	/* Iterate from highest supported to lowest valid state, and update
	 * lowest RequiredState with the lowest state that satisfies
	 * all required clocks
	 */
	for (i = disp_clk->max_clks_state; i >= CLOCKS_STATE_ULTRA_LOW; --i) {
		if ((req_clocks->display_clk_khz <=
				(disp_clk->max_clks_by_state + i)->
					display_clk_khz) &&
			(req_clocks->pixel_clk_khz <=
					(disp_clk->max_clks_by_state + i)->
					pixel_clk_khz))
			low_req_clk = i;
	}
	return low_req_clk;
}

void dispclk_dce112_set_clock(
	struct display_clock *base,
	uint32_t requested_clk_khz)
{
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct display_clock_dce112 *dc = DCLCK112_FROM_BASE(base);
	struct dc_bios *bp = base->ctx->dc_bios;

	/* Prepare to program display clock*/
	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = dm_max(requested_clk_khz,
				base->min_display_clk_threshold_khz);

	dce_clk_params.target_clock_frequency = requested_clk_khz;
	dce_clk_params.pll_id = dc->disp_clk_base.id;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DISPLAY_CLOCK;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		base->cur_min_clks_state = CLOCKS_STATE_NOMINAL;

	/*Program DP ref Clock*/
	/*VBIOS will determine DPREFCLK frequency, so we don't set it*/
	dce_clk_params.target_clock_frequency = 0;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DPREFCLK;
	dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK =
			(dce_clk_params.pll_id ==
					CLOCK_SOURCE_COMBO_DISPLAY_PLL0);

	bp->funcs->set_dce_clock(bp, &dce_clk_params);
}

void dispclk_dce112_set_clock_state(
	struct display_clock *dc,
	struct display_clock_state clk_state)
{
	struct display_clock_dce112 *disp_clk = DCLCK112_FROM_BASE(dc);

	disp_clk->clock_state = clk_state;
}

struct display_clock_state dispclk_dce112_get_clock_state(
	struct display_clock *dc)
{
	struct display_clock_dce112 *disp_clk = DCLCK112_FROM_BASE(dc);

	return disp_clk->clock_state;
}

uint32_t dispclk_dce112_get_dfs_bypass_threshold(
	struct display_clock *dc)
{
	return dce112_DFS_BYPASS_THRESHOLD_KHZ;
}

static const struct display_clock_funcs funcs = {
	.destroy = dispclk_dce112_destroy,
	.calculate_min_clock = dispclk_dce112_calculate_min_clock,
	.get_clock = get_clock,
	.get_clock_state = dispclk_dce112_get_clock_state,
	.get_dfs_bypass_threshold = dispclk_dce112_get_dfs_bypass_threshold,
	.get_dp_ref_clk_frequency = get_dp_ref_clk_frequency,
	.get_min_clocks_state = dispclk_dce112_get_min_clocks_state,
	.get_required_clocks_state = dispclk_dce112_get_required_clocks_state,
	.get_validation_clock = dispclk_dce112_get_validation_clock,
	.set_clock = dispclk_dce112_set_clock,
	.set_clock_state = dispclk_dce112_set_clock_state,
	.set_dp_ref_clock_source = NULL,
	.set_min_clocks_state = dispclk_dce112_set_min_clocks_state,
	.store_max_clocks_state = dispclk_dce112_store_max_clocks_state,
	.validate = NULL,
};

bool dal_display_clock_dce112_construct(
	struct display_clock_dce112 *dc112,
	struct dc_context *ctx)
{
	struct display_clock *dc_base = &dc112->disp_clk_base;

	/*if (NULL == as)
		return false;*/

	if (!dal_display_clock_construct_base(dc_base, ctx))
		return false;

	dc_base->funcs = &funcs;

	dc112->dfs_bypass_disp_clk = 0;

	if (!display_clock_integrated_info_construct(dc112))
		dm_logger_write(dc_base->ctx->logger, LOG_WARNING,
			"Cannot obtain VBIOS integrated info\n");

	dc112->gpu_pll_ss_percentage = 0;
	dc112->gpu_pll_ss_divider = 1000;
	dc112->ss_on_gpu_pll = false;

	dc_base->id = CLOCK_SOURCE_ID_DFS;
/* Initially set max clocks state to nominal.  This should be updated by
 * via a pplib call to DAL IRI eventually calling a
 * DisplayEngineClock_dce112::StoreMaxClocksState().  This call will come in
 * on PPLIB init. This is from DCE5x. in case HW wants to use mixed method.*/
	dc112->max_clks_state = CLOCKS_STATE_NOMINAL;

	dc112->disp_clk_base.min_display_clk_threshold_khz =
			dc112->crystal_freq_khz;

	if (dc112->disp_clk_base.min_display_clk_threshold_khz <
			(dc112->dentist_vco_freq_khz / 62))
		dc112->disp_clk_base.min_display_clk_threshold_khz =
				(dc112->dentist_vco_freq_khz / 62);

	dal_divider_range_construct(
		&divider_ranges[DIVIDER_RANGE_01],
		DIVIDER_RANGE_01_START,
		DIVIDER_RANGE_01_STEP_SIZE,
		DIVIDER_RANGE_01_BASE_DIVIDER_ID,
		DIVIDER_RANGE_02_BASE_DIVIDER_ID);
	dal_divider_range_construct(
		&divider_ranges[DIVIDER_RANGE_02],
		DIVIDER_RANGE_02_START,
		DIVIDER_RANGE_02_STEP_SIZE,
		DIVIDER_RANGE_02_BASE_DIVIDER_ID,
		DIVIDER_RANGE_03_BASE_DIVIDER_ID);
	dal_divider_range_construct(
		&divider_ranges[DIVIDER_RANGE_03],
		DIVIDER_RANGE_03_START,
		DIVIDER_RANGE_03_STEP_SIZE,
		DIVIDER_RANGE_03_BASE_DIVIDER_ID,
		DIVIDER_RANGE_MAX_DIVIDER_ID);

	{
		uint32_t ss_info_num =
			ctx->dc_bios->funcs->
			get_ss_entry_number(ctx->dc_bios, AS_SIGNAL_TYPE_GPU_PLL);

		if (ss_info_num) {
			struct spread_spectrum_info info;
			bool result;

			memset(&info, 0, sizeof(info));

			result =
					(BP_RESULT_OK == ctx->dc_bios->funcs->
					get_spread_spectrum_info(ctx->dc_bios,
					AS_SIGNAL_TYPE_GPU_PLL, 0, &info)) ? true : false;


			/* Based on VBIOS, VBIOS will keep entry for GPU PLL SS
			 * even if SS not enabled and in that case
			 * SSInfo.spreadSpectrumPercentage !=0 would be sign
			 * that SS is enabled
			 */
			if (result && info.spread_spectrum_percentage != 0) {
				dc112->ss_on_gpu_pll = true;
				dc112->gpu_pll_ss_divider =
					info.spread_percentage_divider;

				if (info.type.CENTER_MODE == 0) {
					/* Currently for DP Reference clock we
					 * need only SS percentage for
					 * downspread */
					dc112->gpu_pll_ss_percentage =
						info.spread_spectrum_percentage;
				}
			}

		}
	}

	dc112->use_max_disp_clk = true;
	dc112->max_clks_by_state = max_clks_by_state;

	return true;
}

/*****************************************************************************
 * public functions
 *****************************************************************************/

struct display_clock *dal_display_clock_dce112_create(
	struct dc_context *ctx)
{
	struct display_clock_dce112 *dc112;

	dc112 = dm_alloc(sizeof(struct display_clock_dce112));

	if (dc112 == NULL)
		return NULL;

	if (dal_display_clock_dce112_construct(dc112, ctx))
		return &dc112->disp_clk_base;

	dm_free(dc112);

	return NULL;
}
