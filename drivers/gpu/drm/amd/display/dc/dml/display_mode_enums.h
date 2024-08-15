/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#ifndef __DISPLAY_MODE_ENUMS_H__
#define __DISPLAY_MODE_ENUMS_H__

enum output_encoder_class {
	dm_dp = 0,
	dm_hdmi = 1,
	dm_wb = 2,
	dm_edp = 3,
	dm_dp2p0 = 5,
};
enum output_format_class {
	dm_444 = 0, dm_420 = 1, dm_n422, dm_s422
};
enum source_format_class {
	dm_444_16 = 0,
	dm_444_32 = 1,
	dm_444_64 = 2,
	dm_420_8 = 3,
	dm_420_10 = 4,
	dm_420_12 = 5,
	dm_422_8 = 6,
	dm_422_10 = 7,
	dm_444_8 = 8,
	dm_mono_8 = dm_444_8,
	dm_mono_16 = dm_444_16,
	dm_rgbe = 9,
	dm_rgbe_alpha = 10,
};
enum output_bpc_class {
	dm_out_6 = 0, dm_out_8 = 1, dm_out_10 = 2, dm_out_12 = 3, dm_out_16 = 4
};
enum scan_direction_class {
	dm_horz = 0, dm_vert = 1
};
enum dm_swizzle_mode {
	dm_sw_linear = 0,
	dm_sw_256b_s = 1,
	dm_sw_256b_d = 2,
	dm_sw_SPARE_0 = 3,
	dm_sw_SPARE_1 = 4,
	dm_sw_4kb_s = 5,
	dm_sw_4kb_d = 6,
	dm_sw_SPARE_2 = 7,
	dm_sw_SPARE_3 = 8,
	dm_sw_64kb_s = 9,
	dm_sw_64kb_d = 10,
	dm_sw_SPARE_4 = 11,
	dm_sw_SPARE_5 = 12,
	dm_sw_var_s = 13,
	dm_sw_var_d = 14,
	dm_sw_SPARE_6 = 15,
	dm_sw_SPARE_7 = 16,
	dm_sw_64kb_s_t = 17,
	dm_sw_64kb_d_t = 18,
	dm_sw_SPARE_10 = 19,
	dm_sw_SPARE_11 = 20,
	dm_sw_4kb_s_x = 21,
	dm_sw_4kb_d_x = 22,
	dm_sw_SPARE_12 = 23,
	dm_sw_SPARE_13 = 24,
	dm_sw_64kb_s_x = 25,
	dm_sw_64kb_d_x = 26,
	dm_sw_64kb_r_x = 27,
	dm_sw_SPARE_15 = 28,
	dm_sw_var_s_x = 29,
	dm_sw_var_d_x = 30,
	dm_sw_var_r_x = 31,
	dm_sw_gfx7_2d_thin_l_vp,
	dm_sw_gfx7_2d_thin_gl,
};
enum lb_depth {
	dm_lb_10 = 0, dm_lb_8 = 1, dm_lb_6 = 2, dm_lb_12 = 3, dm_lb_16 = 4,
	dm_lb_19 = 5
};
enum voltage_state {
	dm_vmin = 0, dm_vmid = 1, dm_vnom = 2, dm_vmax = 3
};
enum source_macro_tile_size {
	dm_4k_tile = 0, dm_64k_tile = 1, dm_256k_tile = 2
};
enum cursor_bpp {
	dm_cur_2bit = 0, dm_cur_32bit = 1, dm_cur_64bit = 2
};

/**
 * @enum clock_change_support - It represents possible reasons to change the DRAM clock.
 *
 * DC may change the DRAM clock during its execution, and this enum tracks all
 * the available methods. Note that every ASIC has their specific way to deal
 * with these clock switch.
 */
enum clock_change_support {
	/**
	 * @dm_dram_clock_change_uninitialized: If you see this, we might have
	 * a code initialization issue
	 */
	dm_dram_clock_change_uninitialized = 0,

	/**
	 * @dm_dram_clock_change_vactive: Support DRAM switch in VActive
	 */
	dm_dram_clock_change_vactive,

	/**
	 * @dm_dram_clock_change_vblank: Support DRAM switch in VBlank
	 */
	dm_dram_clock_change_vblank,

	dm_dram_clock_change_vactive_w_mall_full_frame,
	dm_dram_clock_change_vactive_w_mall_sub_vp,
	dm_dram_clock_change_vblank_w_mall_full_frame,
	dm_dram_clock_change_vblank_w_mall_sub_vp,

	/**
	 * @dm_dram_clock_change_unsupported: Do not support DRAM switch
	 */
	dm_dram_clock_change_unsupported
};

enum output_standard {
	dm_std_uninitialized = 0,
	dm_std_cvtr2,
	dm_std_cvt
};

enum mpc_combine_affinity {
	dm_mpc_always_when_possible,
	dm_mpc_reduce_voltage,
	dm_mpc_reduce_voltage_and_clocks,
	dm_mpc_never
};

enum RequestType {
	REQ_256Bytes, REQ_128BytesNonContiguous, REQ_128BytesContiguous, REQ_NA
};

enum self_refresh_affinity {
	dm_try_to_allow_self_refresh_and_mclk_switch,
	dm_allow_self_refresh_and_mclk_switch,
	dm_allow_self_refresh,
	dm_neither_self_refresh_nor_mclk_switch
};

enum dm_validation_status {
	DML_VALIDATION_OK,
	DML_FAIL_SCALE_RATIO_TAP,
	DML_FAIL_SOURCE_PIXEL_FORMAT,
	DML_FAIL_VIEWPORT_SIZE,
	DML_FAIL_TOTAL_V_ACTIVE_BW,
	DML_FAIL_DIO_SUPPORT,
	DML_FAIL_NOT_ENOUGH_DSC,
	DML_FAIL_DSC_CLK_REQUIRED,
	DML_FAIL_DSC_VALIDATION_FAILURE,
	DML_FAIL_URGENT_LATENCY,
	DML_FAIL_REORDERING_BUFFER,
	DML_FAIL_DISPCLK_DPPCLK,
	DML_FAIL_TOTAL_AVAILABLE_PIPES,
	DML_FAIL_NUM_OTG,
	DML_FAIL_WRITEBACK_MODE,
	DML_FAIL_WRITEBACK_LATENCY,
	DML_FAIL_WRITEBACK_SCALE_RATIO_TAP,
	DML_FAIL_CURSOR_SUPPORT,
	DML_FAIL_PITCH_SUPPORT,
	DML_FAIL_PTE_BUFFER_SIZE,
	DML_FAIL_HOST_VM_IMMEDIATE_FLIP,
	DML_FAIL_DSC_INPUT_BPC,
	DML_FAIL_PREFETCH_SUPPORT,
	DML_FAIL_V_RATIO_PREFETCH,
};

enum writeback_config {
	dm_normal,
	dm_whole_buffer_for_single_stream_no_interleave,
	dm_whole_buffer_for_single_stream_interleave,
};

enum odm_combine_mode {
	dm_odm_combine_mode_disabled,
	dm_odm_combine_mode_2to1,
	dm_odm_combine_mode_4to1,
	dm_odm_split_mode_1to2,
	dm_odm_mode_mso_1to2,
	dm_odm_mode_mso_1to4
};

enum odm_combine_policy {
	dm_odm_combine_policy_dal,
	dm_odm_combine_policy_none,
	dm_odm_combine_policy_2to1,
	dm_odm_combine_policy_4to1,
	dm_odm_split_policy_1to2,
	dm_odm_mso_policy_1to2,
	dm_odm_mso_policy_1to4,
};

enum immediate_flip_requirement {
	dm_immediate_flip_not_required,
	dm_immediate_flip_required,
	dm_immediate_flip_opportunistic,
};

enum unbounded_requesting_policy {
	dm_unbounded_requesting,
	dm_unbounded_requesting_edp_only,
	dm_unbounded_requesting_disable
};

enum dm_rotation_angle {
	dm_rotation_0,
	dm_rotation_90,
	dm_rotation_180,
	dm_rotation_270,
	dm_rotation_0m,
	dm_rotation_90m,
	dm_rotation_180m,
	dm_rotation_270m,
};

enum dm_use_mall_for_pstate_change_mode {
	dm_use_mall_pstate_change_disable,
	dm_use_mall_pstate_change_full_frame,
	dm_use_mall_pstate_change_sub_viewport,
	dm_use_mall_pstate_change_phantom_pipe
};

enum dm_use_mall_for_static_screen_mode {
	dm_use_mall_static_screen_disable,
	dm_use_mall_static_screen_optimize,
	dm_use_mall_static_screen_enable,
};

enum dm_output_link_dp_rate {
	dm_dp_rate_na,
	dm_dp_rate_hbr,
	dm_dp_rate_hbr2,
	dm_dp_rate_hbr3,
	dm_dp_rate_uhbr10,
	dm_dp_rate_uhbr13p5,
	dm_dp_rate_uhbr20,
};

enum dm_fclock_change_support {
	dm_fclock_change_vactive,
	dm_fclock_change_vblank,
	dm_fclock_change_unsupported,
};

enum dm_prefetch_modes {
	dm_prefetch_support_uclk_fclk_and_stutter_if_possible,
	dm_prefetch_support_uclk_fclk_and_stutter,
	dm_prefetch_support_fclk_and_stutter,
	dm_prefetch_support_stutter,
	dm_prefetch_support_none,
};
enum dm_output_type {
	dm_output_type_unknown,
	dm_output_type_dp,
	dm_output_type_edp,
	dm_output_type_dp2p0,
	dm_output_type_hdmi,
	dm_output_type_hdmifrl,
};

enum dm_output_rate {
	dm_output_rate_unknown,
	dm_output_rate_dp_rate_hbr,
	dm_output_rate_dp_rate_hbr2,
	dm_output_rate_dp_rate_hbr3,
	dm_output_rate_dp_rate_uhbr10,
	dm_output_rate_dp_rate_uhbr13p5,
	dm_output_rate_dp_rate_uhbr20,
	dm_output_rate_hdmi_rate_3x3,
	dm_output_rate_hdmi_rate_6x3,
	dm_output_rate_hdmi_rate_6x4,
	dm_output_rate_hdmi_rate_8x4,
	dm_output_rate_hdmi_rate_10x4,
	dm_output_rate_hdmi_rate_12x4,
};
#endif
