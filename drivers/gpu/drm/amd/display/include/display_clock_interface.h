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

#ifndef __DISPLAY_CLOCK_INTERFACE_H__
#define __DISPLAY_CLOCK_INTERFACE_H__

#include "hw_sequencer_types.h"
#include "grph_object_defs.h"
#include "signal_types.h"

/* Timing related information*/
struct dc_timing_params {
	uint32_t INTERLACED:1;
	uint32_t HCOUNT_BY_TWO:1;
	uint32_t PIXEL_REPETITION:4; /*< values 1 to 10 supported*/
	uint32_t PREFETCH:1;

	uint32_t h_total;
	uint32_t h_addressable;
	uint32_t h_sync_width;
};

/* Scaling related information*/
struct dc_scaling_params {
	uint32_t h_overscan_right;
	uint32_t h_overscan_left;
	uint32_t h_taps;
	uint32_t v_taps;
};

/* VScalerEfficiency */
enum v_scaler_efficiency {
	V_SCALER_EFFICIENCY_LB36BPP = 0,
	V_SCALER_EFFICIENCY_LB30BPP = 1,
	V_SCALER_EFFICIENCY_LB24BPP = 2,
	V_SCALER_EFFICIENCY_LB18BPP = 3
};

/* Parameters required for minimum Engine
 * and minimum Display clock calculations*/
struct min_clock_params {
	uint32_t id;
	uint32_t requested_pixel_clock; /* in KHz */
	uint32_t actual_pixel_clock; /* in KHz */
	struct view source_view;
	struct view dest_view;
	struct dc_timing_params timing_info;
	struct dc_scaling_params scaling_info;
	enum signal_type signal_type;
	enum dc_color_depth deep_color_depth;
	enum v_scaler_efficiency scaler_efficiency;
	bool line_buffer_prefetch_enabled;
};

/* Result of Minimum System and Display clock calculations.
 * Minimum System clock and Display clock, source and path to be used
 * for Display clock*/
struct minimum_clocks_calculation_result {
	uint32_t min_sclk_khz;
	uint32_t min_dclk_khz;
	uint32_t min_mclk_khz;
	uint32_t min_deep_sleep_sclk;
};

/* Enumeration of all clocks states */
enum clocks_state {
	CLOCKS_STATE_INVALID = 0,
	CLOCKS_STATE_ULTRA_LOW,
	CLOCKS_STATE_LOW,
	CLOCKS_STATE_NOMINAL,
	CLOCKS_STATE_PERFORMANCE,
	/* Starting from DCE11, Max 8 level DPM state supported */
	CLOCKS_DPM_STATE_LEVEL_INVALID = CLOCKS_STATE_INVALID,
	CLOCKS_DPM_STATE_LEVEL_0 = CLOCKS_STATE_ULTRA_LOW,
	CLOCKS_DPM_STATE_LEVEL_1 = CLOCKS_STATE_LOW,
	CLOCKS_DPM_STATE_LEVEL_2 = CLOCKS_STATE_NOMINAL,
	CLOCKS_DPM_STATE_LEVEL_3 = CLOCKS_STATE_PERFORMANCE,
	CLOCKS_DPM_STATE_LEVEL_4 = CLOCKS_DPM_STATE_LEVEL_3 + 1,
	CLOCKS_DPM_STATE_LEVEL_5 = CLOCKS_DPM_STATE_LEVEL_4 + 1,
	CLOCKS_DPM_STATE_LEVEL_6 = CLOCKS_DPM_STATE_LEVEL_5 + 1,
	CLOCKS_DPM_STATE_LEVEL_7 = CLOCKS_DPM_STATE_LEVEL_6 + 1,
};

/* Structure containing all state-dependent clocks
 * (dependent on "enum clocks_state") */
struct state_dependent_clocks {
	uint32_t display_clk_khz;
	uint32_t pixel_clk_khz;
};

struct display_clock_state {
	uint32_t DFS_BYPASS_ACTIVE:1;
};

struct display_clock;

struct display_clock *dal_display_clock_dce112_create(
	struct dc_context *ctx);

struct display_clock *dal_display_clock_dce110_create(
	struct dc_context *ctx);

struct display_clock *dal_display_clock_dce80_create(
	struct dc_context *ctx);

void dal_display_clock_destroy(struct display_clock **to_destroy);
bool dal_display_clock_validate(
	struct display_clock *disp_clk,
	struct min_clock_params *params);
uint32_t dal_display_clock_calculate_min_clock(
	struct display_clock *disp_clk,
	uint32_t path_num,
	struct min_clock_params *params);
uint32_t dal_display_clock_get_validation_clock(struct display_clock *disp_clk);
void dal_display_clock_set_clock(
	struct display_clock *disp_clk,
	uint32_t requested_clock_khz);
uint32_t dal_display_clock_get_clock(struct display_clock *disp_clk);
bool dal_display_clock_get_min_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state *clocks_state);
bool dal_display_clock_get_required_clocks_state(
	struct display_clock *disp_clk,
	struct state_dependent_clocks *req_clocks,
	enum clocks_state *clocks_state);
bool dal_display_clock_set_min_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state clocks_state);
uint32_t dal_display_clock_get_dp_ref_clk_frequency(
	struct display_clock *disp_clk);
/*the second parameter of "switchreferenceclock" is
 * a dummy argument for all pre dce 6.0 versions*/
void dal_display_clock_switch_reference_clock(
	struct display_clock *disp_clk,
	bool use_external_ref_clk,
	uint32_t requested_clock_khz);
void dal_display_clock_set_dp_ref_clock_source(
	struct display_clock *disp_clk,
	enum clock_source_id clk_src);
void dal_display_clock_store_max_clocks_state(
	struct display_clock *disp_clk,
	enum clocks_state max_clocks_state);
void dal_display_clock_set_clock_state(
	struct display_clock *disp_clk,
	struct display_clock_state clk_state);
struct display_clock_state dal_display_clock_get_clock_state(
	struct display_clock *disp_clk);
uint32_t dal_display_clock_get_dfs_bypass_threshold(
	struct display_clock *disp_clk);
void dal_display_clock_invalid_clock_state(
	struct display_clock *disp_clk);

#endif /* __DISPLAY_CLOCK_INTERFACE_H__ */
