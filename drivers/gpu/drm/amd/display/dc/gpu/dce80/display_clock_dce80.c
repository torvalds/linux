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

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "include/bios_parser_interface.h"
#include "include/fixed32_32.h"
#include "include/logger_interface.h"

#include "../divider_range.h"
#include "display_clock_dce80.h"
#include "dc.h"

#define DCE80_DFS_BYPASS_THRESHOLD_KHZ 100000

/* Max clock values for each state indexed by "enum clocks_state": */
static struct state_dependent_clocks max_clks_by_state[] = {
/* ClocksStateInvalid - should not be used */
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/* ClocksStateUltraLow - not expected to be used for DCE 8.0 */
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/* ClocksStateLow */
{ .display_clk_khz = 352000, .pixel_clk_khz = 330000},
/* ClocksStateNominal */
{ .display_clk_khz = 600000, .pixel_clk_khz = 400000 },
/* ClocksStatePerformance */
{ .display_clk_khz = 600000, .pixel_clk_khz = 400000 } };

/* Starting point for each divider range.*/
enum divider_range_start {
	DIVIDER_RANGE_01_START = 200, /* 2.00*/
	DIVIDER_RANGE_02_START = 1600, /* 16.00*/
	DIVIDER_RANGE_03_START = 3200, /* 32.00*/
	DIVIDER_RANGE_SCALE_FACTOR = 100 /* Results are scaled up by 100.*/
};

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

/* Array identifiers and count for the divider ranges.*/
enum divider_range_count {
	DIVIDER_RANGE_01 = 0,
	DIVIDER_RANGE_02,
	DIVIDER_RANGE_03,
	DIVIDER_RANGE_MAX /* == 3*/
};

static struct divider_range divider_ranges[DIVIDER_RANGE_MAX];

#define FROM_DISPLAY_CLOCK(base) \
	container_of(base, struct display_clock_dce80, disp_clk)

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

static uint32_t get_scaler_efficiency(struct min_clock_params *params)
{
	uint32_t scaler_efficiency = 3;

	switch (params->scaler_efficiency) {
	case V_SCALER_EFFICIENCY_LB18BPP:
	case V_SCALER_EFFICIENCY_LB24BPP:
		scaler_efficiency = 4;
		break;

	case V_SCALER_EFFICIENCY_LB30BPP:
	case V_SCALER_EFFICIENCY_LB36BPP:
		scaler_efficiency = 3;
		break;

	default:
		break;
	}

	return scaler_efficiency;
}

static uint32_t get_actual_required_display_clk(
	struct display_clock_dce80 *disp_clk,
	uint32_t  target_clk_khz)
{
	uint32_t disp_clk_khz = target_clk_khz;
	uint32_t div = INVALID_DIVIDER;
	uint32_t did = INVALID_DID;
	uint32_t scaled_vco =
		disp_clk->dentist_vco_freq_khz * DIVIDER_RANGE_SCALE_FACTOR;

	ASSERT(disp_clk_khz);

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

static uint32_t get_validation_clock(struct display_clock *dc)
{
	uint32_t clk = 0;
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	switch (disp_clk->max_clks_state) {
	case CLOCKS_STATE_ULTRA_LOW:
		/*Currently not supported, it has 0 in table entry*/
	case CLOCKS_STATE_LOW:
		clk = max_clks_by_state[CLOCKS_STATE_LOW].
						display_clk_khz;
		break;

	case CLOCKS_STATE_NOMINAL:
		clk = max_clks_by_state[CLOCKS_STATE_NOMINAL].
						display_clk_khz;
		break;

	case CLOCKS_STATE_PERFORMANCE:
		clk = max_clks_by_state[CLOCKS_STATE_PERFORMANCE].
						display_clk_khz;
		break;

	case CLOCKS_STATE_INVALID:
	default:
		/*Invalid Clocks State*/
		BREAK_TO_DEBUGGER();
		/* just return the display engine clock for
		 * lowest supported state*/
		clk = max_clks_by_state[CLOCKS_STATE_LOW].
						display_clk_khz;
		break;
	}
	return clk;
}

static uint32_t calc_single_display_min_clks(
	struct display_clock *base,
	struct min_clock_params *params,
	bool set_clk)
{
	struct fixed32_32 h_scale = dal_fixed32_32_from_int(1);
	struct fixed32_32 v_scale = dal_fixed32_32_from_int(1);
	uint32_t pix_clk_khz = params->requested_pixel_clock;
	uint32_t line_total = params->timing_info.h_total;
	uint32_t max_clk_khz = get_validation_clock(base);
	struct fixed32_32 deep_color_factor = get_deep_color_factor(params);
	uint32_t scaler_efficiency = get_scaler_efficiency(params);
	struct fixed32_32 v_filter_init;
	uint32_t v_filter_init_trunc;
	struct fixed32_32 v_filter_init_ceil;
	struct fixed32_32 src_lines_per_dst_line;
	uint32_t src_wdth_rnd_to_chunks;
	struct fixed32_32 scaling_coeff;
	struct fixed32_32 fx_disp_clk_khz;
	struct fixed32_32 fx_alt_disp_clk_khz;
	uint32_t disp_clk_khz;
	uint32_t alt_disp_clk_khz;
	struct display_clock_dce80 *dc = FROM_DISPLAY_CLOCK(base);

	if (0 != params->dest_view.height && 0 != params->dest_view.width) {

		h_scale = dal_fixed32_32_from_fraction(
			params->source_view.width,
			params->dest_view.width);
		v_scale = dal_fixed32_32_from_fraction(
			params->source_view.height,
			params->dest_view.height);
	}

	v_filter_init = dal_fixed32_32_from_fraction(
		params->scaling_info.v_taps, 2u);
	v_filter_init = dal_fixed32_32_add(v_filter_init,
		dal_fixed32_32_div_int(v_scale, 2));
	v_filter_init = dal_fixed32_32_add(v_filter_init,
		dal_fixed32_32_from_fraction(15, 10));

	v_filter_init_trunc = dal_fixed32_32_floor(v_filter_init);

	v_filter_init_ceil = dal_fixed32_32_from_fraction(
						v_filter_init_trunc, 2);
	v_filter_init_ceil = dal_fixed32_32_from_int(
		dal_fixed32_32_ceil(v_filter_init_ceil));
	v_filter_init_ceil = dal_fixed32_32_mul_int(v_filter_init_ceil, 2);
	v_filter_init_ceil = dal_fixed32_32_div_int(v_filter_init_ceil, 3);
	v_filter_init_ceil = dal_fixed32_32_from_int(
		dal_fixed32_32_ceil(v_filter_init_ceil));

	src_lines_per_dst_line = dal_fixed32_32_max(
		dal_fixed32_32_from_int(dal_fixed32_32_ceil(v_scale)),
		v_filter_init_ceil);

	src_wdth_rnd_to_chunks =
		((params->source_view.width - 1) / 128) * 128 + 256;

	scaling_coeff = dal_fixed32_32_max(
		dal_fixed32_32_from_fraction(params->scaling_info.h_taps, 4),
		dal_fixed32_32_mul(
			dal_fixed32_32_from_fraction(
				params->scaling_info.v_taps,
				scaler_efficiency),
					h_scale));

	scaling_coeff = dal_fixed32_32_max(scaling_coeff, h_scale);

	fx_disp_clk_khz = dal_fixed32_32_mul(
		scaling_coeff, dal_fixed32_32_from_fraction(11, 10));
	if (0 != line_total) {
		struct fixed32_32 d_clk = dal_fixed32_32_mul_int(
			src_lines_per_dst_line, src_wdth_rnd_to_chunks);
		d_clk = dal_fixed32_32_div_int(d_clk, line_total);
		d_clk = dal_fixed32_32_mul(d_clk,
				dal_fixed32_32_from_fraction(11, 10));
		fx_disp_clk_khz = dal_fixed32_32_max(fx_disp_clk_khz, d_clk);
	}

	fx_disp_clk_khz = dal_fixed32_32_max(fx_disp_clk_khz,
		dal_fixed32_32_mul(deep_color_factor,
		dal_fixed32_32_from_fraction(11, 10)));

	fx_disp_clk_khz = dal_fixed32_32_mul_int(fx_disp_clk_khz, pix_clk_khz);
	fx_disp_clk_khz = dal_fixed32_32_mul(fx_disp_clk_khz,
		dal_fixed32_32_from_fraction(1005, 1000));

	fx_alt_disp_clk_khz = scaling_coeff;

	if (0 != line_total) {
		struct fixed32_32 d_clk = dal_fixed32_32_mul_int(
			src_lines_per_dst_line, src_wdth_rnd_to_chunks);
		d_clk = dal_fixed32_32_div_int(d_clk, line_total);
		d_clk = dal_fixed32_32_mul(d_clk,
			dal_fixed32_32_from_fraction(105, 100));
		fx_alt_disp_clk_khz = dal_fixed32_32_max(
			fx_alt_disp_clk_khz, d_clk);
	}
	fx_alt_disp_clk_khz = dal_fixed32_32_max(
		fx_alt_disp_clk_khz, fx_alt_disp_clk_khz);

	fx_alt_disp_clk_khz = dal_fixed32_32_mul_int(
		fx_alt_disp_clk_khz, pix_clk_khz);

	/* convert to integer*/
	disp_clk_khz = dal_fixed32_32_floor(fx_disp_clk_khz);
	alt_disp_clk_khz = dal_fixed32_32_floor(fx_alt_disp_clk_khz);

	if (set_clk) { /* only compensate clock if we are going to set it.*/
		disp_clk_khz = get_actual_required_display_clk(
			dc, disp_clk_khz);
		alt_disp_clk_khz = get_actual_required_display_clk(
			dc, alt_disp_clk_khz);
	}

	if ((disp_clk_khz > max_clk_khz) && (alt_disp_clk_khz <= max_clk_khz))
		disp_clk_khz = alt_disp_clk_khz;

	return disp_clk_khz;

}

static uint32_t calc_cursor_bw_for_min_clks(struct min_clock_params *params)
{

	struct fixed32_32 v_scale = dal_fixed32_32_from_int(1);
	struct fixed32_32 v_filter_ceiling;
	struct fixed32_32 src_lines_per_dst_line;
	struct fixed32_32 cursor_bw;

	/*  DCE8 Mode Support and Mode Set Architecture Specification Rev 1.3
	 6.3.3	Cursor data Throughput requirement on DISPCLK
	 The MCIF to DCP cursor data return throughput is one pixel per DISPCLK
	  shared among the display heads.
	 If (Total Cursor Bandwidth in pixels for All heads> DISPCLK)
	 The mode is not supported
	 Cursor Bandwidth in Pixels = Cursor Width *
	 (SourceLinesPerDestinationLine / Line Time)
	 Assuming that Cursor Width = 128
	 */
	/*In the hardware doc they mention an Interlace Factor
	  It is not used here because we have already used it when
	  calculating destination view*/
	if (0 != params->dest_view.height)
		v_scale = dal_fixed32_32_from_fraction(
			params->source_view.height,
			params->dest_view.height);

	{
	/*Do: Vertical Filter Init = 0.5 + VTAPS/2 + VSR/2 * Interlace Factor*/
	/*Interlace Factor is included in verticalScaleRatio*/
	struct fixed32_32 v_filter = dal_fixed32_32_add(
		dal_fixed32_32_from_fraction(params->scaling_info.v_taps, 2),
		dal_fixed32_32_div_int(v_scale, 2));
	/*Do : Ceiling (Vertical Filter Init, 2)/3 )*/
	v_filter_ceiling = dal_fixed32_32_div_int(v_filter, 2);
	v_filter_ceiling = dal_fixed32_32_mul_int(
		dal_fixed32_32_from_int(dal_fixed32_32_ceil(v_filter_ceiling)),
							2);
	v_filter_ceiling = dal_fixed32_32_div_int(v_filter_ceiling, 3);
	}
	/*Do : MAX( CeilCeiling (VSR), Ceiling (Vertical Filter Init, 2)/3 )*/
	/*Do : SourceLinesPerDestinationLine =
	 * MAX( Ceiling (VSR), Ceiling (Vertical Filter Init, 2)/3 )*/
	src_lines_per_dst_line = dal_fixed32_32_max(v_scale, v_filter_ceiling);

	if ((params->requested_pixel_clock != 0) &&
		(params->timing_info.h_total != 0)) {
		/* pixelClock is in units of KHz.  Calc lineTime in us*/
		struct fixed32_32 inv_line_time = dal_fixed32_32_from_fraction(
			params->requested_pixel_clock,
			params->timing_info.h_total);
		cursor_bw = dal_fixed32_32_mul(
			dal_fixed32_32_mul_int(inv_line_time, 128),
						src_lines_per_dst_line);
	}

	/* convert to integer*/
	return dal_fixed32_32_floor(cursor_bw);
}

static bool validate(
	struct display_clock *dc,
	struct min_clock_params *params)
{
	uint32_t max_clk_khz = get_validation_clock(dc);
	uint32_t req_clk_khz;

	if (params == NULL)
		return false;

	req_clk_khz = calc_single_display_min_clks(dc, params, false);

	return (req_clk_khz <= max_clk_khz);
}

static uint32_t calculate_min_clock(
	struct display_clock *dc,
	uint32_t path_num,
	struct min_clock_params *params)
{
	uint32_t i;
	uint32_t validation_clk_khz = get_validation_clock(dc);
	uint32_t min_clk_khz = validation_clk_khz;
	uint32_t max_clk_khz = 0;
	uint32_t total_cursor_bw = 0;
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	if (disp_clk->use_max_disp_clk)
		return min_clk_khz;

	if (params != NULL) {
		uint32_t disp_clk_khz = 0;

		for (i = 0; i < path_num; ++i) {
			disp_clk_khz = calc_single_display_min_clks(
				dc, params, true);

			/* update the max required clock found*/
			if (disp_clk_khz > max_clk_khz)
				max_clk_khz = disp_clk_khz;

			disp_clk_khz = calc_cursor_bw_for_min_clks(params);

			total_cursor_bw += disp_clk_khz;

			params++;

		}
	}

	max_clk_khz = (total_cursor_bw > max_clk_khz) ? total_cursor_bw :
								max_clk_khz;

	min_clk_khz = max_clk_khz;

	/*"Cursor data Throughput requirement on DISPCLK is now a factor,
	 *  need to change the code */
	ASSERT(total_cursor_bw < validation_clk_khz);

	if (min_clk_khz > validation_clk_khz)
		min_clk_khz = validation_clk_khz;
	else if (min_clk_khz < dc->min_display_clk_threshold_khz)
		min_clk_khz = dc->min_display_clk_threshold_khz;

	return min_clk_khz;
}

static void set_clock(
	struct display_clock *dc,
	uint32_t requested_clk_khz)
{
	struct bp_pixel_clock_parameters pxl_clk_params;
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);
	struct dc_bios *bp = dc->ctx->dc_bios;

	/* Prepare to program display clock*/
	memset(&pxl_clk_params, 0, sizeof(pxl_clk_params));

	pxl_clk_params.target_pixel_clock = requested_clk_khz;
	pxl_clk_params.pll_id = dc->id;

	bp->funcs->program_display_engine_pll(bp, &pxl_clk_params);

	if (disp_clk->dfs_bypass_enabled) {

		/* Cache the fixed display clock*/
		disp_clk->dfs_bypass_disp_clk =
			pxl_clk_params.dfs_bypass_display_clock;
	}

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		disp_clk->cur_min_clks_state = CLOCKS_STATE_NOMINAL;
}

static uint32_t get_clock(struct display_clock *dc)
{
	uint32_t disp_clock = get_validation_clock(dc);
	uint32_t target_div = INVALID_DIVIDER;
	uint32_t addr = mmDENTIST_DISPCLK_CNTL;
	uint32_t value = 0;
	uint32_t field = 0;
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	if (disp_clk->dfs_bypass_enabled && disp_clk->dfs_bypass_disp_clk)
		return disp_clk->dfs_bypass_disp_clk;

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

static void set_clock_state(
	struct display_clock *dc,
	struct display_clock_state clk_state)
{
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	disp_clk->clock_state = clk_state;
}
static struct display_clock_state get_clock_state(
	struct display_clock *dc)
{
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	return disp_clk->clock_state;
}

static enum clocks_state get_min_clocks_state(struct display_clock *dc)
{
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	return disp_clk->cur_min_clks_state;
}

static enum clocks_state get_required_clocks_state
	(struct display_clock *dc,
	struct state_dependent_clocks *req_clocks)
{
	int32_t i;
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);
	enum clocks_state low_req_clk = disp_clk->max_clks_state;

	if (!req_clocks) {
		/* NULL pointer*/
		BREAK_TO_DEBUGGER();
		return CLOCKS_STATE_INVALID;
	}

	/* Iterate from highest supported to lowest valid state, and update
	 * lowest RequiredState with the lowest state that satisfies
	 * all required clocks
	 */
	for (i = disp_clk->max_clks_state; i >= CLOCKS_STATE_ULTRA_LOW; --i) {
		if ((req_clocks->display_clk_khz <=
			max_clks_by_state[i].display_clk_khz) &&
			(req_clocks->pixel_clk_khz <=
				max_clks_by_state[i].pixel_clk_khz))
			low_req_clk = i;
	}
	return low_req_clk;
}

static bool set_min_clocks_state(
	struct display_clock *dc,
	enum clocks_state clocks_state)
{
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	struct dm_pp_power_level_change_request level_change_req = {
			DM_PP_POWER_LEVEL_INVALID};

	if (clocks_state > disp_clk->max_clks_state) {
		/*Requested state exceeds max supported state.*/
		dm_logger_write(dc->ctx->logger, LOG_WARNING,
				"Requested state exceeds max supported state");
		return false;
	} else if (clocks_state == dc->cur_min_clks_state) {
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
		dm_logger_write(dc->ctx->logger, LOG_WARNING,
				"Requested state invalid state");
		return false;
	}

	/* get max clock state from PPLIB */
	if (dm_pp_apply_power_level_change_request(dc->ctx, &level_change_req))
		dc->cur_min_clks_state = clocks_state;

	return true;
}

static uint32_t get_dp_ref_clk_frequency(struct display_clock *dc)
{
	uint32_t dispclk_cntl_value;
	uint32_t dp_ref_clk_cntl_value;
	uint32_t dp_ref_clk_cntl_src_sel_value;
	uint32_t dp_ref_clk_khz = 600000;
	uint32_t target_div = INVALID_DIVIDER;
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

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

static void store_max_clocks_state(
	struct display_clock *dc,
	enum clocks_state max_clocks_state)
{
	struct display_clock_dce80 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	switch (max_clocks_state) {
	case CLOCKS_STATE_LOW:
	case CLOCKS_STATE_NOMINAL:
	case CLOCKS_STATE_PERFORMANCE:
	case CLOCKS_STATE_ULTRA_LOW:
		disp_clk->max_clks_state = max_clocks_state;
		break;

	case CLOCKS_STATE_INVALID:
	default:
		/*Invalid Clocks State!*/
		BREAK_TO_DEBUGGER();
		break;
	}
}

static void display_clock_ss_construct(
	struct display_clock_dce80 *disp_clk)
{
	struct dc_bios *bp = disp_clk->disp_clk.ctx->dc_bios;
	uint32_t ss_entry_num = bp->funcs->get_ss_entry_number(bp,
		AS_SIGNAL_TYPE_GPU_PLL);

	/*Read SS Info from VBIOS SS Info table for DP Reference Clock spread.*/
	if (ss_entry_num > 0) {/* Should be only one entry */
		struct spread_spectrum_info ss_info;
		enum bp_result res;

		memset(&ss_info, 0, sizeof(struct spread_spectrum_info));

		res = bp->funcs->get_spread_spectrum_info(bp,
			AS_SIGNAL_TYPE_GPU_PLL, 0, &ss_info);

		/* Based on VBIOS, VBIOS will keep entry for GPU PLL SS even if
		 * SS not enabled and in that case
		 * SSInfo.spreadSpectrumPercentage !=0 would be
		 * sign that SS is enabled*/
		if (res == BP_RESULT_OK && ss_info.spread_spectrum_percentage != 0) {
			disp_clk->ss_on_gpu_pll = true;
			disp_clk->gpu_pll_ss_divider =
				ss_info.spread_percentage_divider;
			if (ss_info.type.CENTER_MODE == 0)
				/* Currently we need only SS
				 * percentage for down-spread*/
				disp_clk->gpu_pll_ss_percentage =
					ss_info.spread_spectrum_percentage;
		}
	}
}

static bool display_clock_integrated_info_construct(
	struct display_clock_dce80 *disp_clk)
{
	struct dc_debug *debug = &disp_clk->disp_clk.ctx->dc->debug;
	struct dc_bios *bp = disp_clk->disp_clk.ctx->dc_bios;
	struct integrated_info info = { { { 0 } } };
	struct firmware_info fw_info = { { 0 } };
	uint32_t i;

	if (bp->integrated_info)
		info = *bp->integrated_info;

	disp_clk->dentist_vco_freq_khz = info.dentist_vco_freq;
	if (disp_clk->dentist_vco_freq_khz == 0) {
		bp->funcs->get_firmware_info(bp, &fw_info);
		disp_clk->dentist_vco_freq_khz =
			fw_info.smu_gpu_pll_output_freq;
		if (disp_clk->dentist_vco_freq_khz == 0)
			disp_clk->dentist_vco_freq_khz = 3600000;
		}
	disp_clk->disp_clk.min_display_clk_threshold_khz =
		disp_clk->dentist_vco_freq_khz / 64;

	/* TODO: initialise disp_clk->dfs_bypass_disp_clk */

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
			max_clks_by_state[clk_state].display_clk_khz =
				info.disp_clk_voltage[i].max_supported_clk;
		}
	}

	disp_clk->dfs_bypass_enabled = false;
	if (!debug->disable_dfs_bypass)
		if (bp->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE)
			disp_clk->dfs_bypass_enabled = true;

	disp_clk->use_max_disp_clk = debug->max_disp_clk;

	return true;
}

static uint32_t get_dfs_bypass_threshold(struct display_clock *dc)
{
	return DCE80_DFS_BYPASS_THRESHOLD_KHZ;
}

static void destroy(struct display_clock **dc)
{
	struct display_clock_dce80 *disp_clk;

	disp_clk = FROM_DISPLAY_CLOCK(*dc);
	dm_free(disp_clk);
	*dc = NULL;
}

static const struct display_clock_funcs funcs = {
	.calculate_min_clock = calculate_min_clock,
	.destroy = destroy,
	.get_clock = get_clock,
	.get_clock_state = get_clock_state,
	.get_dfs_bypass_threshold = get_dfs_bypass_threshold,
	.get_dp_ref_clk_frequency = get_dp_ref_clk_frequency,
	.get_min_clocks_state = get_min_clocks_state,
	.get_required_clocks_state = get_required_clocks_state,
	.get_validation_clock = get_validation_clock,
	.set_clock = set_clock,
	.set_clock_state = set_clock_state,
	.set_dp_ref_clock_source =
		dal_display_clock_base_set_dp_ref_clock_source,
	.set_min_clocks_state = set_min_clocks_state,
	.store_max_clocks_state = store_max_clocks_state,
	.validate = validate,
};

static bool display_clock_construct(
	struct dc_context *ctx,
	struct display_clock_dce80 *disp_clk)
{
	struct display_clock *dc_base = &disp_clk->disp_clk;

	if (!dal_display_clock_construct_base(dc_base, ctx))
		return false;

	dc_base->funcs = &funcs;
	/*
	 * set_dp_ref_clock_source
	 * set_clock_state
	 * get_clock_state
	 * get_dfs_bypass_threshold
	 */

	disp_clk->gpu_pll_ss_percentage = 0;
	disp_clk->gpu_pll_ss_divider = 1000;
	disp_clk->ss_on_gpu_pll = false;
	disp_clk->dfs_bypass_enabled = false;
	disp_clk->dfs_bypass_disp_clk = 0;
	disp_clk->use_max_disp_clk = true;/* false will hang the system! */

	disp_clk->disp_clk.id = CLOCK_SOURCE_ID_DFS;
/* Initially set max clocks state to nominal.  This should be updated by
 * via a pplib call to DAL IRI eventually calling a
 * DisplayEngineClock_Dce50::StoreMaxClocksState().  This call will come in
 * on PPLIB init. This is from DCE5x. in case HW wants to use mixed method.*/
	disp_clk->max_clks_state = CLOCKS_STATE_NOMINAL;
/* Initially set current min clocks state to invalid since we
 * cannot make any assumption about PPLIB's initial state. This will be updated
 * by HWSS via SetMinClocksState() on first mode set prior to programming
 * state dependent clocks.*/
	disp_clk->cur_min_clks_state = CLOCKS_STATE_INVALID;

	display_clock_ss_construct(disp_clk);

	if (!display_clock_integrated_info_construct(disp_clk)) {
		dm_logger_write(dc_base->ctx->logger, LOG_WARNING,
			"Cannot obtain VBIOS integrated info");
	}

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
	return true;
}

struct display_clock *dal_display_clock_dce80_create(
	struct dc_context *ctx)
{
	struct display_clock_dce80 *disp_clk;

	disp_clk = dm_alloc(sizeof(struct display_clock_dce80));

	if (disp_clk == NULL)
		return NULL;

	if (display_clock_construct(ctx, disp_clk))
		return &disp_clk->disp_clk;

	dm_free(disp_clk);
	return NULL;
}

