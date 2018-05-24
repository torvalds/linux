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

#ifndef __DM_SERVICES_TYPES_H__
#define __DM_SERVICES_TYPES_H__

#include "os_types.h"
#include "dc_types.h"

struct pp_smu_funcs_rv;

struct dm_pp_clock_range {
	int min_khz;
	int max_khz;
};

enum dm_pp_clocks_state {
	DM_PP_CLOCKS_STATE_INVALID,
	DM_PP_CLOCKS_STATE_ULTRA_LOW,
	DM_PP_CLOCKS_STATE_LOW,
	DM_PP_CLOCKS_STATE_NOMINAL,
	DM_PP_CLOCKS_STATE_PERFORMANCE,

	/* Starting from DCE11, Max 8 levels of DPM state supported. */
	DM_PP_CLOCKS_DPM_STATE_LEVEL_INVALID = DM_PP_CLOCKS_STATE_INVALID,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_0,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_1,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_2,
	/* to be backward compatible */
	DM_PP_CLOCKS_DPM_STATE_LEVEL_3,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_4,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_5,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_6,
	DM_PP_CLOCKS_DPM_STATE_LEVEL_7,

	DM_PP_CLOCKS_MAX_STATES
};

struct dm_pp_gpu_clock_range {
	enum dm_pp_clocks_state clock_state;
	struct dm_pp_clock_range sclk;
	struct dm_pp_clock_range mclk;
	struct dm_pp_clock_range eclk;
	struct dm_pp_clock_range dclk;
};

enum dm_pp_clock_type {
	DM_PP_CLOCK_TYPE_DISPLAY_CLK = 1,
	DM_PP_CLOCK_TYPE_ENGINE_CLK, /* System clock */
	DM_PP_CLOCK_TYPE_MEMORY_CLK,
	DM_PP_CLOCK_TYPE_DCFCLK,
	DM_PP_CLOCK_TYPE_DCEFCLK,
	DM_PP_CLOCK_TYPE_SOCCLK,
	DM_PP_CLOCK_TYPE_PIXELCLK,
	DM_PP_CLOCK_TYPE_DISPLAYPHYCLK,
	DM_PP_CLOCK_TYPE_DPPCLK,
	DM_PP_CLOCK_TYPE_FCLK,
};

#define DC_DECODE_PP_CLOCK_TYPE(clk_type) \
	(clk_type) == DM_PP_CLOCK_TYPE_DISPLAY_CLK ? "Display" : \
	(clk_type) == DM_PP_CLOCK_TYPE_ENGINE_CLK ? "Engine" : \
	(clk_type) == DM_PP_CLOCK_TYPE_MEMORY_CLK ? "Memory" : "Invalid"

#define DM_PP_MAX_CLOCK_LEVELS 8

struct dm_pp_clock_levels {
	uint32_t num_levels;
	uint32_t clocks_in_khz[DM_PP_MAX_CLOCK_LEVELS];
};

struct dm_pp_clock_with_latency {
	uint32_t clocks_in_khz;
	uint32_t latency_in_us;
};

struct dm_pp_clock_levels_with_latency {
	uint32_t num_levels;
	struct dm_pp_clock_with_latency data[DM_PP_MAX_CLOCK_LEVELS];
};

struct dm_pp_clock_with_voltage {
	uint32_t clocks_in_khz;
	uint32_t voltage_in_mv;
};

struct dm_pp_clock_levels_with_voltage {
	uint32_t num_levels;
	struct dm_pp_clock_with_voltage data[DM_PP_MAX_CLOCK_LEVELS];
};

struct dm_pp_single_disp_config {
	enum signal_type signal;
	uint8_t transmitter;
	uint8_t ddi_channel_mapping;
	uint8_t pipe_idx;
	uint32_t src_height;
	uint32_t src_width;
	uint32_t v_refresh;
	uint32_t sym_clock; /* HDMI only */
	struct dc_link_settings link_settings; /* DP only */
};

#define MAX_WM_SETS 4

enum dm_pp_wm_set_id {
	WM_SET_A = 0,
	WM_SET_B,
	WM_SET_C,
	WM_SET_D,
	WM_SET_INVALID = 0xffff,
};

struct dm_pp_clock_range_for_wm_set {
	enum dm_pp_wm_set_id wm_set_id;
	uint32_t wm_min_eng_clk_in_khz;
	uint32_t wm_max_eng_clk_in_khz;
	uint32_t wm_min_memg_clk_in_khz;
	uint32_t wm_max_mem_clk_in_khz;
};

struct dm_pp_wm_sets_with_clock_ranges {
	uint32_t num_wm_sets;
	struct dm_pp_clock_range_for_wm_set wm_clk_ranges[MAX_WM_SETS];
};

struct dm_pp_clock_range_for_dmif_wm_set_soc15 {
	enum dm_pp_wm_set_id wm_set_id;
	uint32_t wm_min_dcfclk_clk_in_khz;
	uint32_t wm_max_dcfclk_clk_in_khz;
	uint32_t wm_min_memg_clk_in_khz;
	uint32_t wm_max_mem_clk_in_khz;
};

struct dm_pp_clock_range_for_mcif_wm_set_soc15 {
	enum dm_pp_wm_set_id wm_set_id;
	uint32_t wm_min_socclk_clk_in_khz;
	uint32_t wm_max_socclk_clk_in_khz;
	uint32_t wm_min_memg_clk_in_khz;
	uint32_t wm_max_mem_clk_in_khz;
};

struct dm_pp_wm_sets_with_clock_ranges_soc15 {
	uint32_t num_wm_dmif_sets;
	uint32_t num_wm_mcif_sets;
	struct dm_pp_clock_range_for_dmif_wm_set_soc15
		wm_dmif_clocks_ranges[MAX_WM_SETS];
	struct dm_pp_clock_range_for_mcif_wm_set_soc15
		wm_mcif_clocks_ranges[MAX_WM_SETS];
};

#define MAX_DISPLAY_CONFIGS 6

struct dm_pp_display_configuration {
	bool nb_pstate_switch_disable;/* controls NB PState switch */
	bool cpu_cc6_disable; /* controls CPU CState switch ( on or off) */
	bool cpu_pstate_disable;
	uint32_t cpu_pstate_separation_time;

	uint32_t min_memory_clock_khz;
	uint32_t min_engine_clock_khz;
	uint32_t min_engine_clock_deep_sleep_khz;

	uint32_t avail_mclk_switch_time_us;
	uint32_t avail_mclk_switch_time_in_disp_active_us;
	uint32_t min_dcfclock_khz;
	uint32_t min_dcfc_deep_sleep_clock_khz;

	uint32_t disp_clk_khz;

	bool all_displays_in_sync;

	uint8_t display_count;
	struct dm_pp_single_disp_config disp_configs[MAX_DISPLAY_CONFIGS];

	/*Controller Index of primary display - used in MCLK SMC switching hang
	 * SW Workaround*/
	uint8_t crtc_index;
	/*htotal*1000/pixelclk - used in MCLK SMC switching hang SW Workaround*/
	uint32_t line_time_in_us;
};

struct dm_bl_data_point {
		/* Brightness level in percentage */
		uint8_t luminance;
		/* Brightness level as effective value in range 0-255,
		 * corresponding to above percentage
		 */
		uint8_t signalLevel;
};

/* Total size of the structure should not exceed 256 bytes */
struct dm_acpi_atif_backlight_caps {


	uint16_t size; /* Bytes 0-1 (2 bytes) */
	uint16_t flags; /* Byted 2-3 (2 bytes) */
	uint8_t  errorCode; /* Byte 4 */
	uint8_t  acLevelPercentage; /* Byte 5 */
	uint8_t  dcLevelPercentage; /* Byte 6 */
	uint8_t  minInputSignal; /* Byte 7 */
	uint8_t  maxInputSignal; /* Byte 8 */
	uint8_t  numOfDataPoints; /* Byte 9 */
	struct dm_bl_data_point dataPoints[99]; /* Bytes 10-207 (198 bytes)*/
};

enum dm_acpi_display_type {
	AcpiDisplayType_LCD1 = 0,
	AcpiDisplayType_CRT1 = 1,
	AcpiDisplayType_DFP1 = 3,
	AcpiDisplayType_CRT2 = 4,
	AcpiDisplayType_LCD2 = 5,
	AcpiDisplayType_DFP2 = 7,
	AcpiDisplayType_DFP3 = 9,
	AcpiDisplayType_DFP4 = 10,
	AcpiDisplayType_DFP5 = 11,
	AcpiDisplayType_DFP6 = 12
};

struct dm_pp_power_level_change_request {
	enum dm_pp_clocks_state power_level;
};

struct dm_pp_clock_for_voltage_req {
	enum dm_pp_clock_type clk_type;
	uint32_t clocks_in_khz;
};

struct dm_pp_static_clock_info {
	uint32_t max_sclk_khz;
	uint32_t max_mclk_khz;

	/* max possible display block clocks state */
	enum dm_pp_clocks_state max_clocks_state;
};

struct dtn_min_clk_info {
	uint32_t disp_clk_khz;
	uint32_t min_engine_clock_khz;
	uint32_t min_memory_clock_khz;
};

#endif
