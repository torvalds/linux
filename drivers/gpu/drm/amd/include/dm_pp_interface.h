/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 */
#ifndef _DM_PP_INTERFACE_
#define _DM_PP_INTERFACE_

#define PP_MAX_CLOCK_LEVELS 16

enum amd_pp_display_config_type{
	AMD_PP_DisplayConfigType_None = 0,
	AMD_PP_DisplayConfigType_DP54 ,
	AMD_PP_DisplayConfigType_DP432 ,
	AMD_PP_DisplayConfigType_DP324 ,
	AMD_PP_DisplayConfigType_DP27,
	AMD_PP_DisplayConfigType_DP243,
	AMD_PP_DisplayConfigType_DP216,
	AMD_PP_DisplayConfigType_DP162,
	AMD_PP_DisplayConfigType_HDMI6G ,
	AMD_PP_DisplayConfigType_HDMI297 ,
	AMD_PP_DisplayConfigType_HDMI162,
	AMD_PP_DisplayConfigType_LVDS,
	AMD_PP_DisplayConfigType_DVI,
	AMD_PP_DisplayConfigType_WIRELESS,
	AMD_PP_DisplayConfigType_VGA
};

struct single_display_configuration
{
	uint32_t controller_index;
	uint32_t controller_id;
	uint32_t signal_type;
	uint32_t display_state;
	/* phy id for the primary internal transmitter */
	uint8_t primary_transmitter_phyi_d;
	/* bitmap with the active lanes */
	uint8_t primary_transmitter_active_lanemap;
	/* phy id for the secondary internal transmitter (for dual-link dvi) */
	uint8_t secondary_transmitter_phy_id;
	/* bitmap with the active lanes */
	uint8_t secondary_transmitter_active_lanemap;
	/* misc phy settings for SMU. */
	uint32_t config_flags;
	uint32_t display_type;
	uint32_t view_resolution_cx;
	uint32_t view_resolution_cy;
	enum amd_pp_display_config_type displayconfigtype;
	uint32_t vertical_refresh; /* for active display */
};

#define MAX_NUM_DISPLAY 32

struct amd_pp_display_configuration {
	bool nb_pstate_switch_disable;/* controls NB PState switch */
	bool cpu_cc6_disable; /* controls CPU CState switch ( on or off) */
	bool cpu_pstate_disable;
	uint32_t cpu_pstate_separation_time;

	uint32_t num_display;  /* total number of display*/
	uint32_t num_path_including_non_display;
	uint32_t crossfire_display_index;
	uint32_t min_mem_set_clock;
	uint32_t min_core_set_clock;
	/* unit 10KHz x bit*/
	uint32_t min_bus_bandwidth;
	/* minimum required stutter sclk, in 10khz uint32_t ulMinCoreSetClk;*/
	uint32_t min_core_set_clock_in_sr;

	struct single_display_configuration displays[MAX_NUM_DISPLAY];

	uint32_t vrefresh; /* for active display*/

	uint32_t min_vblank_time; /* for active display*/
	bool multi_monitor_in_sync;
	/* Controller Index of primary display - used in MCLK SMC switching hang
	 * SW Workaround*/
	uint32_t crtc_index;
	/* htotal*1000/pixelclk - used in MCLK SMC switching hang SW Workaround*/
	uint32_t line_time_in_us;
	bool invalid_vblank_time;

	uint32_t display_clk;
	/*
	 * for given display configuration if multimonitormnsync == false then
	 * Memory clock DPMS with this latency or below is allowed, DPMS with
	 * higher latency not allowed.
	 */
	uint32_t dce_tolerable_mclk_in_active_latency;
	uint32_t min_dcef_set_clk;
	uint32_t min_dcef_deep_sleep_set_clk;
};

struct amd_pp_simple_clock_info {
	uint32_t	engine_max_clock;
	uint32_t	memory_max_clock;
	uint32_t	level;
};

enum PP_DAL_POWERLEVEL {
	PP_DAL_POWERLEVEL_INVALID = 0,
	PP_DAL_POWERLEVEL_ULTRALOW,
	PP_DAL_POWERLEVEL_LOW,
	PP_DAL_POWERLEVEL_NOMINAL,
	PP_DAL_POWERLEVEL_PERFORMANCE,

	PP_DAL_POWERLEVEL_0 = PP_DAL_POWERLEVEL_ULTRALOW,
	PP_DAL_POWERLEVEL_1 = PP_DAL_POWERLEVEL_LOW,
	PP_DAL_POWERLEVEL_2 = PP_DAL_POWERLEVEL_NOMINAL,
	PP_DAL_POWERLEVEL_3 = PP_DAL_POWERLEVEL_PERFORMANCE,
	PP_DAL_POWERLEVEL_4 = PP_DAL_POWERLEVEL_3+1,
	PP_DAL_POWERLEVEL_5 = PP_DAL_POWERLEVEL_4+1,
	PP_DAL_POWERLEVEL_6 = PP_DAL_POWERLEVEL_5+1,
	PP_DAL_POWERLEVEL_7 = PP_DAL_POWERLEVEL_6+1,
};

struct amd_pp_clock_info {
	uint32_t min_engine_clock;
	uint32_t max_engine_clock;
	uint32_t min_memory_clock;
	uint32_t max_memory_clock;
	uint32_t min_bus_bandwidth;
	uint32_t max_bus_bandwidth;
	uint32_t max_engine_clock_in_sr;
	uint32_t min_engine_clock_in_sr;
	enum PP_DAL_POWERLEVEL max_clocks_state;
};

enum amd_pp_clock_type {
	amd_pp_disp_clock = 1,
	amd_pp_sys_clock,
	amd_pp_mem_clock,
	amd_pp_dcef_clock,
	amd_pp_soc_clock,
	amd_pp_pixel_clock,
	amd_pp_phy_clock,
	amd_pp_dcf_clock,
	amd_pp_dpp_clock,
	amd_pp_f_clock = amd_pp_dcef_clock,
};

#define MAX_NUM_CLOCKS 16

struct amd_pp_clocks {
	uint32_t count;
	uint32_t clock[MAX_NUM_CLOCKS];
	uint32_t latency[MAX_NUM_CLOCKS];
};

struct pp_clock_with_latency {
	uint32_t clocks_in_khz;
	uint32_t latency_in_us;
};

struct pp_clock_levels_with_latency {
	uint32_t num_levels;
	struct pp_clock_with_latency data[PP_MAX_CLOCK_LEVELS];
};

struct pp_clock_with_voltage {
	uint32_t clocks_in_khz;
	uint32_t voltage_in_mv;
};

struct pp_clock_levels_with_voltage {
	uint32_t num_levels;
	struct pp_clock_with_voltage data[PP_MAX_CLOCK_LEVELS];
};

struct pp_display_clock_request {
	enum amd_pp_clock_type clock_type;
	uint32_t clock_freq_in_khz;
};

#define PP_MAX_WM_SETS 4

enum pp_wm_set_id {
	DC_WM_SET_A = 0,
	DC_WM_SET_B,
	DC_WM_SET_C,
	DC_WM_SET_D,
	DC_WM_SET_INVALID = 0xffff,
};

struct pp_wm_set_with_dmif_clock_range_soc15 {
	enum pp_wm_set_id wm_set_id;
	uint32_t wm_min_dcefclk_in_khz;
	uint32_t wm_max_dcefclk_in_khz;
	uint32_t wm_min_memclk_in_khz;
	uint32_t wm_max_memclk_in_khz;
};

struct pp_wm_set_with_mcif_clock_range_soc15 {
	enum pp_wm_set_id wm_set_id;
	uint32_t wm_min_socclk_in_khz;
	uint32_t wm_max_socclk_in_khz;
	uint32_t wm_min_memclk_in_khz;
	uint32_t wm_max_memclk_in_khz;
};

struct pp_wm_sets_with_clock_ranges_soc15 {
	uint32_t num_wm_sets_dmif;
	uint32_t num_wm_sets_mcif;
	struct pp_wm_set_with_dmif_clock_range_soc15
		wm_sets_dmif[PP_MAX_WM_SETS];
	struct pp_wm_set_with_mcif_clock_range_soc15
		wm_sets_mcif[PP_MAX_WM_SETS];
};

#endif /* _DM_PP_INTERFACE_ */
