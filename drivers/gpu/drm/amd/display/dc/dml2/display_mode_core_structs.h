/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DISPLAY_MODE_CORE_STRUCT_H__
#define __DISPLAY_MODE_CORE_STRUCT_H__

#include "display_mode_lib_defines.h"

enum dml_project_id {
	dml_project_invalid = 0,
	dml_project_default = 1,
	dml_project_dcn32 = dml_project_default,
	dml_project_dcn321 = 2,
	dml_project_dcn35 = 3,
	dml_project_dcn351 = 4,
};
enum dml_prefetch_modes {
	dml_prefetch_support_uclk_fclk_and_stutter_if_possible = 0,
	dml_prefetch_support_uclk_fclk_and_stutter = 1,
	dml_prefetch_support_fclk_and_stutter = 2,
	dml_prefetch_support_stutter = 3,
	dml_prefetch_support_none = 4
};
enum dml_use_mall_for_pstate_change_mode {
	dml_use_mall_pstate_change_disable = 0,
	dml_use_mall_pstate_change_full_frame = 1,
	dml_use_mall_pstate_change_sub_viewport = 2,
	dml_use_mall_pstate_change_phantom_pipe = 3
};
enum dml_use_mall_for_static_screen_mode {
	dml_use_mall_static_screen_disable = 0,
	dml_use_mall_static_screen_enable = 1,
	dml_use_mall_static_screen_optimize = 2
};
enum dml_output_encoder_class {
	dml_dp = 0,
	dml_edp = 1,
	dml_dp2p0 = 2,
	dml_hdmi = 3,
	dml_hdmifrl = 4,
	dml_none = 5
};
enum dml_output_link_dp_rate{
	dml_dp_rate_na = 0,
	dml_dp_rate_hbr = 1,
	dml_dp_rate_hbr2 = 2,
	dml_dp_rate_hbr3 = 3,
	dml_dp_rate_uhbr10 = 4,
	dml_dp_rate_uhbr13p5 = 5,
	dml_dp_rate_uhbr20 = 6
};
enum dml_output_type_and_rate__type{
	dml_output_type_unknown = 0,
	dml_output_type_dp = 1,
	dml_output_type_edp = 2,
	dml_output_type_dp2p0 = 3,
	dml_output_type_hdmi = 4,
	dml_output_type_hdmifrl = 5
};
enum dml_output_type_and_rate__rate {
	dml_output_rate_unknown = 0,
	dml_output_rate_dp_rate_hbr = 1,
	dml_output_rate_dp_rate_hbr2 = 2,
	dml_output_rate_dp_rate_hbr3 = 3,
	dml_output_rate_dp_rate_uhbr10 = 4,
	dml_output_rate_dp_rate_uhbr13p5 = 5,
	dml_output_rate_dp_rate_uhbr20 = 6,
	dml_output_rate_hdmi_rate_3x3 = 7,
	dml_output_rate_hdmi_rate_6x3 = 8,
	dml_output_rate_hdmi_rate_6x4 = 9,
	dml_output_rate_hdmi_rate_8x4 = 10,
	dml_output_rate_hdmi_rate_10x4 = 11,
	dml_output_rate_hdmi_rate_12x4 = 12
};
enum dml_output_format_class {
	dml_444 = 0,
	dml_s422 = 1,
	dml_n422 = 2,
	dml_420 = 3
};
enum dml_source_format_class {
	dml_444_8 = 0,
	dml_444_16 = 1,
	dml_444_32 = 2,
	dml_444_64 = 3,
	dml_420_8 = 4,
	dml_420_10 = 5,
	dml_420_12 = 6,
	dml_422_8 = 7,
	dml_422_10 = 8,
	dml_rgbe_alpha = 9,
	dml_rgbe = 10,
	dml_mono_8 = 11,
	dml_mono_16 = 12
};
enum dml_output_bpc_class {
	dml_out_6 = 0,
	dml_out_8 = 1,
	dml_out_10 = 2,
	dml_out_12 = 3,
	dml_out_16 = 4
};
enum dml_output_standard_class {
	dml_std_cvt = 0,
	dml_std_cea = 1,
	dml_std_cvtr2 = 2
};
enum dml_rotation_angle {
	dml_rotation_0 = 0,
	dml_rotation_90 = 1,
	dml_rotation_180 = 2,
	dml_rotation_270 = 3,
	dml_rotation_0m = 4,
	dml_rotation_90m = 5,
	dml_rotation_180m = 6,
	dml_rotation_270m = 7
};
enum dml_swizzle_mode {
	dml_sw_linear = 0,
	dml_sw_256b_s = 1,
	dml_sw_256b_d = 2,
	dml_sw_256b_r = 3,
	dml_sw_4kb_z = 4,
	dml_sw_4kb_s = 5,
	dml_sw_4kb_d = 6,
	dml_sw_4kb_r = 7,
	dml_sw_64kb_z = 8,
	dml_sw_64kb_s = 9,
	dml_sw_64kb_d = 10,
	dml_sw_64kb_r = 11,
	dml_sw_256kb_z = 12,
	dml_sw_256kb_s = 13,
	dml_sw_256kb_d = 14,
	dml_sw_256kb_r = 15,
	dml_sw_64kb_z_t = 16,
	dml_sw_64kb_s_t = 17,
	dml_sw_64kb_d_t = 18,
	dml_sw_64kb_r_t = 19,
	dml_sw_4kb_z_x = 20,
	dml_sw_4kb_s_x = 21,
	dml_sw_4kb_d_x = 22,
	dml_sw_4kb_r_x = 23,
	dml_sw_64kb_z_x = 24,
	dml_sw_64kb_s_x = 25,
	dml_sw_64kb_d_x = 26,
	dml_sw_64kb_r_x = 27,
	dml_sw_256kb_z_x = 28,
	dml_sw_256kb_s_x = 29,
	dml_sw_256kb_d_x = 30,
	dml_sw_256kb_r_x = 31
};
enum dml_lb_depth {
	dml_lb_6 = 0,
	dml_lb_8 = 1,
	dml_lb_10 = 2,
	dml_lb_12 = 3,
	dml_lb_16 = 4
};
enum dml_voltage_state {
	dml_vmin_lv = 0,
	dml_vmin = 1,
	dml_vmid = 2,
	dml_vnom = 3,
	dml_vmax = 4
};
enum dml_source_macro_tile_size {
	dml_4k_tile = 0,
	dml_64k_tile = 1,
	dml_256k_tile = 2
};
enum dml_cursor_bpp {
	dml_cur_2bit = 0,
	dml_cur_32bit = 1,
	dml_cur_64bit = 2
};
enum dml_dram_clock_change_support {
	dml_dram_clock_change_vactive = 0,
	dml_dram_clock_change_vblank = 1,
	dml_dram_clock_change_vblank_drr = 2,
	dml_dram_clock_change_vactive_w_mall_full_frame = 3,
	dml_dram_clock_change_vactive_w_mall_sub_vp = 4,
	dml_dram_clock_change_vblank_w_mall_full_frame = 5,
	dml_dram_clock_change_vblank_drr_w_mall_full_frame = 6,
	dml_dram_clock_change_vblank_w_mall_sub_vp = 7,
	dml_dram_clock_change_vblank_drr_w_mall_sub_vp = 8,
	dml_dram_clock_change_unsupported = 9
};
enum dml_fclock_change_support {
	dml_fclock_change_vactive = 0,
	dml_fclock_change_vblank = 1,
	dml_fclock_change_unsupported = 2
};
enum dml_dsc_enable {
	dml_dsc_disable = 0,
	dml_dsc_enable = 1,
	dml_dsc_enable_if_necessary = 2
};
enum dml_mpc_use_policy {
	dml_mpc_disabled = 0,
	dml_mpc_as_possible = 1,
	dml_mpc_as_needed_for_voltage = 2,
	dml_mpc_as_needed_for_pstate_and_voltage = 3
};
enum dml_odm_use_policy {
	dml_odm_use_policy_bypass = 0,
	dml_odm_use_policy_combine_as_needed = 1,
	dml_odm_use_policy_combine_2to1 = 2,
	dml_odm_use_policy_combine_4to1 = 3,
	dml_odm_use_policy_split_1to2 = 4,
	dml_odm_use_policy_mso_1to2 = 5,
	dml_odm_use_policy_mso_1to4 = 6
};
enum dml_odm_mode {
	dml_odm_mode_bypass = 0,
	dml_odm_mode_combine_2to1 = 1,
	dml_odm_mode_combine_4to1 = 2,
	dml_odm_mode_split_1to2 = 3,
	dml_odm_mode_mso_1to2 = 4,
	dml_odm_mode_mso_1to4 = 5
};
enum dml_writeback_configuration {
	dml_whole_buffer_for_single_stream_no_interleave = 0,
	dml_whole_buffer_for_single_stream_interleave = 1
};
enum dml_immediate_flip_requirement {
	dml_immediate_flip_not_required = 0,
	dml_immediate_flip_required = 1,
	dml_immediate_flip_if_possible = 2
};
enum dml_unbounded_requesting_policy {
	dml_unbounded_requesting_enable = 0,
	dml_unbounded_requesting_edp_only = 1,
	dml_unbounded_requesting_disable = 2
};
enum dml_clk_cfg_policy {
	dml_use_required_freq = 0,
	dml_use_override_freq = 1,
	dml_use_state_freq = 2
};


struct soc_state_bounding_box_st {
	dml_float_t socclk_mhz;
	dml_float_t dscclk_mhz;
	dml_float_t phyclk_mhz;
	dml_float_t phyclk_d18_mhz;
	dml_float_t phyclk_d32_mhz;
	dml_float_t dtbclk_mhz;
	dml_float_t fabricclk_mhz;
	dml_float_t dcfclk_mhz;
	dml_float_t dispclk_mhz;
	dml_float_t dppclk_mhz;
	dml_float_t dram_speed_mts;
	dml_float_t urgent_latency_pixel_data_only_us;
	dml_float_t urgent_latency_pixel_mixed_with_vm_data_us;
	dml_float_t urgent_latency_vm_data_only_us;
	dml_float_t writeback_latency_us;
	dml_float_t urgent_latency_adjustment_fabric_clock_component_us;
	dml_float_t urgent_latency_adjustment_fabric_clock_reference_mhz;
	dml_float_t sr_exit_time_us;
	dml_float_t sr_enter_plus_exit_time_us;
	dml_float_t sr_exit_z8_time_us;
	dml_float_t sr_enter_plus_exit_z8_time_us;
	dml_float_t dram_clock_change_latency_us;
	dml_float_t fclk_change_latency_us;
	dml_float_t usr_retraining_latency_us;
	dml_bool_t use_ideal_dram_bw_strobe;
};

struct soc_bounding_box_st {
	dml_float_t dprefclk_mhz;
	dml_float_t xtalclk_mhz;
	dml_float_t pcierefclk_mhz;
	dml_float_t refclk_mhz;
	dml_float_t amclk_mhz;
	dml_float_t max_outstanding_reqs;
	dml_float_t pct_ideal_sdp_bw_after_urgent;
	dml_float_t pct_ideal_fabric_bw_after_urgent;
	dml_float_t pct_ideal_dram_bw_after_urgent_pixel_only;
	dml_float_t pct_ideal_dram_bw_after_urgent_pixel_and_vm;
	dml_float_t pct_ideal_dram_bw_after_urgent_vm_only;
	dml_float_t pct_ideal_dram_bw_after_urgent_strobe;
	dml_float_t max_avg_sdp_bw_use_normal_percent;
	dml_float_t max_avg_fabric_bw_use_normal_percent;
	dml_float_t max_avg_dram_bw_use_normal_percent;
	dml_float_t max_avg_dram_bw_use_normal_strobe_percent;
	dml_uint_t round_trip_ping_latency_dcfclk_cycles;
	dml_uint_t urgent_out_of_order_return_per_channel_pixel_only_bytes;
	dml_uint_t urgent_out_of_order_return_per_channel_pixel_and_vm_bytes;
	dml_uint_t urgent_out_of_order_return_per_channel_vm_only_bytes;
	dml_uint_t num_chans;
	dml_uint_t return_bus_width_bytes;
	dml_uint_t dram_channel_width_bytes;
	dml_uint_t fabric_datapath_to_dcn_data_return_bytes;
	dml_uint_t hostvm_min_page_size_kbytes;
	dml_uint_t gpuvm_min_page_size_kbytes;
	dml_float_t phy_downspread_percent;
	dml_float_t dcn_downspread_percent;
	dml_float_t smn_latency_us;
	dml_uint_t mall_allocated_for_dcn_mbytes;
	dml_float_t dispclk_dppclk_vco_speed_mhz;
	dml_bool_t do_urgent_latency_adjustment;
};

struct ip_params_st {
	dml_uint_t vblank_nom_default_us;
	dml_uint_t rob_buffer_size_kbytes;
	dml_uint_t config_return_buffer_size_in_kbytes;
	dml_uint_t config_return_buffer_segment_size_in_kbytes;
	dml_uint_t compressed_buffer_segment_size_in_kbytes;
	dml_uint_t meta_fifo_size_in_kentries;
	dml_uint_t zero_size_buffer_entries;
	dml_uint_t dpte_buffer_size_in_pte_reqs_luma;
	dml_uint_t dpte_buffer_size_in_pte_reqs_chroma;
	dml_uint_t dcc_meta_buffer_size_bytes;
	dml_bool_t gpuvm_enable;
	dml_bool_t hostvm_enable;
	dml_uint_t gpuvm_max_page_table_levels;
	dml_uint_t hostvm_max_page_table_levels;
	dml_uint_t pixel_chunk_size_kbytes;
	dml_uint_t alpha_pixel_chunk_size_kbytes;
	dml_uint_t min_pixel_chunk_size_bytes;
	dml_uint_t meta_chunk_size_kbytes;
	dml_uint_t min_meta_chunk_size_bytes;
	dml_uint_t writeback_chunk_size_kbytes;
	dml_uint_t line_buffer_size_bits;
	dml_uint_t max_line_buffer_lines;
	dml_uint_t writeback_interface_buffer_size_kbytes;
	dml_uint_t max_num_dpp;
	dml_uint_t max_num_otg;
	dml_uint_t max_num_wb;
	dml_uint_t max_dchub_pscl_bw_pix_per_clk;
	dml_uint_t max_pscl_lb_bw_pix_per_clk;
	dml_uint_t max_lb_vscl_bw_pix_per_clk;
	dml_uint_t max_vscl_hscl_bw_pix_per_clk;
	dml_float_t max_hscl_ratio;
	dml_float_t max_vscl_ratio;
	dml_uint_t max_hscl_taps;
	dml_uint_t max_vscl_taps;
	dml_uint_t num_dsc;
	dml_uint_t maximum_dsc_bits_per_component;
	dml_uint_t maximum_pixels_per_line_per_dsc_unit;
	dml_bool_t dsc422_native_support;
	dml_bool_t cursor_64bpp_support;
	dml_float_t dispclk_ramp_margin_percent;
	dml_uint_t dppclk_delay_subtotal;
	dml_uint_t dppclk_delay_scl;
	dml_uint_t dppclk_delay_scl_lb_only;
	dml_uint_t dppclk_delay_cnvc_formatter;
	dml_uint_t dppclk_delay_cnvc_cursor;
	dml_uint_t cursor_buffer_size;
	dml_uint_t cursor_chunk_size;
	dml_uint_t dispclk_delay_subtotal;
	dml_bool_t dynamic_metadata_vm_enabled;
	dml_uint_t max_inter_dcn_tile_repeaters;
	dml_uint_t max_num_hdmi_frl_outputs;
	dml_uint_t max_num_dp2p0_outputs;
	dml_uint_t max_num_dp2p0_streams;
	dml_bool_t dcc_supported;
	dml_bool_t ptoi_supported;
	dml_float_t writeback_max_hscl_ratio;
	dml_float_t writeback_max_vscl_ratio;
	dml_float_t writeback_min_hscl_ratio;
	dml_float_t writeback_min_vscl_ratio;
	dml_uint_t writeback_max_hscl_taps;
	dml_uint_t writeback_max_vscl_taps;
	dml_uint_t writeback_line_buffer_buffer_size;
};

struct DmlPipe {
	dml_float_t Dppclk;
	dml_float_t Dispclk;
	dml_float_t PixelClock;
	dml_float_t DCFClkDeepSleep;
	dml_uint_t DPPPerSurface;
	dml_bool_t ScalerEnabled;
	enum dml_rotation_angle SourceScan;
	dml_uint_t ViewportHeight;
	dml_uint_t ViewportHeightChroma;
	dml_uint_t BlockWidth256BytesY;
	dml_uint_t BlockHeight256BytesY;
	dml_uint_t BlockWidth256BytesC;
	dml_uint_t BlockHeight256BytesC;
	dml_uint_t BlockWidthY;
	dml_uint_t BlockHeightY;
	dml_uint_t BlockWidthC;
	dml_uint_t BlockHeightC;
	dml_uint_t InterlaceEnable;
	dml_uint_t NumberOfCursors;
	dml_uint_t VBlank;
	dml_uint_t HTotal;
	dml_uint_t HActive;
	dml_bool_t DCCEnable;
	enum dml_odm_mode ODMMode;
	enum dml_source_format_class SourcePixelFormat;
	enum dml_swizzle_mode SurfaceTiling;
	dml_uint_t BytePerPixelY;
	dml_uint_t BytePerPixelC;
	dml_bool_t ProgressiveToInterlaceUnitInOPP;
	dml_float_t VRatio;
	dml_float_t VRatioChroma;
	dml_uint_t VTaps;
	dml_uint_t VTapsChroma;
	dml_uint_t PitchY;
	dml_uint_t DCCMetaPitchY;
	dml_uint_t PitchC;
	dml_uint_t DCCMetaPitchC;
	dml_bool_t ViewportStationary;
	dml_uint_t ViewportXStart;
	dml_uint_t ViewportYStart;
	dml_uint_t ViewportXStartC;
	dml_uint_t ViewportYStartC;
	dml_bool_t FORCE_ONE_ROW_FOR_FRAME;
	dml_uint_t SwathHeightY;
	dml_uint_t SwathHeightC;
};

struct Watermarks {
	dml_float_t UrgentWatermark;
	dml_float_t WritebackUrgentWatermark;
	dml_float_t DRAMClockChangeWatermark;
	dml_float_t FCLKChangeWatermark;
	dml_float_t WritebackDRAMClockChangeWatermark;
	dml_float_t WritebackFCLKChangeWatermark;
	dml_float_t StutterExitWatermark;
	dml_float_t StutterEnterPlusExitWatermark;
	dml_float_t Z8StutterExitWatermark;
	dml_float_t Z8StutterEnterPlusExitWatermark;
	dml_float_t USRRetrainingWatermark;
};

struct SOCParametersList {
	dml_float_t UrgentLatency;
	dml_float_t ExtraLatency;
	dml_float_t WritebackLatency;
	dml_float_t DRAMClockChangeLatency;
	dml_float_t FCLKChangeLatency;
	dml_float_t SRExitTime;
	dml_float_t SREnterPlusExitTime;
	dml_float_t SRExitZ8Time;
	dml_float_t SREnterPlusExitZ8Time;
	dml_float_t USRRetrainingLatency;
	dml_float_t SMNLatency;
};

/// @brief Struct that represent Plane configration of a display cfg
struct dml_plane_cfg_st {
	//
	// Pipe/Surface Parameters
	//
	dml_bool_t GPUVMEnable; /// <brief Set if any pipe has GPUVM enable
	dml_bool_t HostVMEnable; /// <brief Set if any pipe has HostVM enable

	dml_uint_t GPUVMMaxPageTableLevels; /// <brief GPUVM level; max of all pipes'
	dml_uint_t HostVMMaxPageTableLevels; /// <brief HostVM level; max of all pipes'; that is the number of non-cache HVM level

	dml_uint_t GPUVMMinPageSizeKBytes[__DML_NUM_PLANES__];
	dml_bool_t ForceOneRowForFrame[__DML_NUM_PLANES__];
	dml_bool_t PTEBufferModeOverrideEn[__DML_NUM_PLANES__]; //< brief when override enable; the DML will only check the given pte buffer and will use the pte buffer mode as is
	dml_bool_t PTEBufferMode[__DML_NUM_PLANES__];
	dml_uint_t ViewportWidth[__DML_NUM_PLANES__];
	dml_uint_t ViewportHeight[__DML_NUM_PLANES__];
	dml_uint_t ViewportWidthChroma[__DML_NUM_PLANES__];
	dml_uint_t ViewportHeightChroma[__DML_NUM_PLANES__];
	dml_uint_t ViewportXStart[__DML_NUM_PLANES__];
	dml_uint_t ViewportXStartC[__DML_NUM_PLANES__];
	dml_uint_t ViewportYStart[__DML_NUM_PLANES__];
	dml_uint_t ViewportYStartC[__DML_NUM_PLANES__];
	dml_bool_t ViewportStationary[__DML_NUM_PLANES__];

	dml_bool_t ScalerEnabled[__DML_NUM_PLANES__];
	dml_float_t HRatio[__DML_NUM_PLANES__];
	dml_float_t VRatio[__DML_NUM_PLANES__];
	dml_float_t HRatioChroma[__DML_NUM_PLANES__];
	dml_float_t VRatioChroma[__DML_NUM_PLANES__];
	dml_uint_t HTaps[__DML_NUM_PLANES__];
	dml_uint_t VTaps[__DML_NUM_PLANES__];
	dml_uint_t HTapsChroma[__DML_NUM_PLANES__];
	dml_uint_t VTapsChroma[__DML_NUM_PLANES__];
	dml_uint_t LBBitPerPixel[__DML_NUM_PLANES__];

	enum dml_rotation_angle SourceScan[__DML_NUM_PLANES__];
	dml_uint_t ScalerRecoutWidth[__DML_NUM_PLANES__];

	dml_bool_t DynamicMetadataEnable[__DML_NUM_PLANES__];
	dml_uint_t DynamicMetadataLinesBeforeActiveRequired[__DML_NUM_PLANES__];
	dml_uint_t DynamicMetadataTransmittedBytes[__DML_NUM_PLANES__];
	dml_uint_t DETSizeOverride[__DML_NUM_PLANES__]; /// <brief user can specify the desire DET buffer usage per-plane

	dml_uint_t NumberOfCursors[__DML_NUM_PLANES__];
	dml_uint_t CursorWidth[__DML_NUM_PLANES__];
	dml_uint_t CursorBPP[__DML_NUM_PLANES__];

	enum dml_use_mall_for_static_screen_mode UseMALLForStaticScreen[__DML_NUM_PLANES__];
	enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange[__DML_NUM_PLANES__];

	dml_uint_t BlendingAndTiming[__DML_NUM_PLANES__]; /// <brief From which timing group (like OTG) that this plane is getting its timing from. Mode check also need this info for example to check num OTG; encoder; dsc etc.
}; // dml_plane_cfg_st;

/// @brief Surface Parameters
struct dml_surface_cfg_st {
	enum dml_swizzle_mode SurfaceTiling[__DML_NUM_PLANES__];
	enum dml_source_format_class SourcePixelFormat[__DML_NUM_PLANES__];
	dml_uint_t PitchY[__DML_NUM_PLANES__];
	dml_uint_t SurfaceWidthY[__DML_NUM_PLANES__];
	dml_uint_t SurfaceHeightY[__DML_NUM_PLANES__];
	dml_uint_t PitchC[__DML_NUM_PLANES__];
	dml_uint_t SurfaceWidthC[__DML_NUM_PLANES__];
	dml_uint_t SurfaceHeightC[__DML_NUM_PLANES__];

	dml_bool_t DCCEnable[__DML_NUM_PLANES__];
	dml_uint_t DCCMetaPitchY[__DML_NUM_PLANES__];
	dml_uint_t DCCMetaPitchC[__DML_NUM_PLANES__];

	dml_float_t DCCRateLuma[__DML_NUM_PLANES__];
	dml_float_t DCCRateChroma[__DML_NUM_PLANES__];
	dml_float_t DCCFractionOfZeroSizeRequestsLuma[__DML_NUM_PLANES__];
	dml_float_t DCCFractionOfZeroSizeRequestsChroma[__DML_NUM_PLANES__];
}; // dml_surface_cfg_st

/// @brief structure that represents the timing configuration
struct dml_timing_cfg_st {
	dml_uint_t HTotal[__DML_NUM_PLANES__];
	dml_uint_t VTotal[__DML_NUM_PLANES__];
	dml_uint_t HBlankEnd[__DML_NUM_PLANES__];
	dml_uint_t VBlankEnd[__DML_NUM_PLANES__];
	dml_uint_t RefreshRate[__DML_NUM_PLANES__];
	dml_uint_t VFrontPorch[__DML_NUM_PLANES__];
	dml_float_t PixelClock[__DML_NUM_PLANES__];
	dml_uint_t HActive[__DML_NUM_PLANES__];
	dml_uint_t VActive[__DML_NUM_PLANES__];
	dml_bool_t Interlace[__DML_NUM_PLANES__];
	dml_bool_t DRRDisplay[__DML_NUM_PLANES__];
	dml_uint_t VBlankNom[__DML_NUM_PLANES__];
}; // dml_timing_cfg_st;

/// @brief structure that represents the output stream
struct dml_output_cfg_st {
	// Output Setting
	dml_uint_t DSCInputBitPerComponent[__DML_NUM_PLANES__];
	enum dml_output_format_class OutputFormat[__DML_NUM_PLANES__];
	enum dml_output_encoder_class OutputEncoder[__DML_NUM_PLANES__];
	dml_uint_t OutputMultistreamId[__DML_NUM_PLANES__];
	dml_bool_t OutputMultistreamEn[__DML_NUM_PLANES__];
	dml_float_t OutputBpp[__DML_NUM_PLANES__]; //< brief Use by mode_programming to specify a output bpp; user can use the output from mode_support (support.OutputBpp)
	dml_float_t PixelClockBackEnd[__DML_NUM_PLANES__];
	enum dml_dsc_enable DSCEnable[__DML_NUM_PLANES__]; //< brief for mode support check; use to determine if dsc is required
	dml_uint_t OutputLinkDPLanes[__DML_NUM_PLANES__];
	enum dml_output_link_dp_rate OutputLinkDPRate[__DML_NUM_PLANES__];
	dml_float_t ForcedOutputLinkBPP[__DML_NUM_PLANES__];
	dml_uint_t AudioSampleRate[__DML_NUM_PLANES__];
	dml_uint_t AudioSampleLayout[__DML_NUM_PLANES__];
	dml_bool_t OutputDisabled[__DML_NUM_PLANES__];
}; // dml_timing_cfg_st;

/// @brief Writeback Setting
struct dml_writeback_cfg_st {
	enum dml_source_format_class WritebackPixelFormat[__DML_NUM_PLANES__];
	dml_bool_t WritebackEnable[__DML_NUM_PLANES__];
	dml_uint_t ActiveWritebacksPerSurface[__DML_NUM_PLANES__];
	dml_uint_t WritebackDestinationWidth[__DML_NUM_PLANES__];
	dml_uint_t WritebackDestinationHeight[__DML_NUM_PLANES__];
	dml_uint_t WritebackSourceWidth[__DML_NUM_PLANES__];
	dml_uint_t WritebackSourceHeight[__DML_NUM_PLANES__];
	dml_uint_t WritebackHTaps[__DML_NUM_PLANES__];
	dml_uint_t WritebackVTaps[__DML_NUM_PLANES__];
	dml_float_t WritebackHRatio[__DML_NUM_PLANES__];
	dml_float_t WritebackVRatio[__DML_NUM_PLANES__];
}; // dml_writeback_cfg_st;

/// @brief Hardware resource specific; mainly used by mode_programming when test/sw wants to do some specific setting
///        which are not the same as what the mode support stage derive.  When call mode_support with mode_programm; the hw-specific
//         resource will be set to what the mode_support layer recommends
struct dml_hw_resource_st {
	enum dml_odm_mode ODMMode[__DML_NUM_PLANES__]; /// <brief ODM mode that is chosen in the mode check stage and will be used in mode programming stage
	dml_uint_t DPPPerSurface[__DML_NUM_PLANES__]; /// <brief How many DPPs are needed drive the surface to output. If MPCC or ODMC could be 2 or 4.
	dml_bool_t DSCEnabled[__DML_NUM_PLANES__]; /// <brief Indicate if the DSC is enabled; used in mode_programming
	dml_uint_t NumberOfDSCSlices[__DML_NUM_PLANES__]; /// <brief Indicate how many slices needed to support the given mode
	dml_float_t DLGRefClkFreqMHz; /// <brief DLG Global Reference timer
};

/// @brief DML display configuration.
///        Describe how to display a surface in multi-plane setup and output to different output and writeback using the specified timgin
struct dml_display_cfg_st {
	struct dml_surface_cfg_st surface;
	struct dml_plane_cfg_st plane;
	struct dml_timing_cfg_st timing;
	struct dml_output_cfg_st output;
	struct dml_writeback_cfg_st writeback;
	unsigned int num_surfaces;
	unsigned int num_timings;

	struct dml_hw_resource_st hw; //< brief for mode programming
}; // dml_display_cfg_st

/// @brief To control the clk usage for model programming
struct dml_clk_cfg_st {
	enum dml_clk_cfg_policy dcfclk_option; ///< brief Use for mode_program; user can select between use the min require clk req as calculated by DML or use the test-specific freq
	enum dml_clk_cfg_policy dispclk_option; ///< brief Use for mode_program; user can select between use the min require clk req as calculated by DML or use the test-specific freq
	enum dml_clk_cfg_policy dppclk_option[__DML_NUM_PLANES__];

	dml_float_t dcfclk_freq_mhz;
	dml_float_t dispclk_freq_mhz;
	dml_float_t dppclk_freq_mhz[__DML_NUM_PLANES__];
}; // dml_clk_cfg_st

/// @brief DML mode evaluation and programming policy
/// Those knobs that affect mode support and mode programming
struct dml_mode_eval_policy_st {
	// -------------------
	// Policy
	// -------------------
	enum dml_mpc_use_policy MPCCombineUse[__DML_NUM_PLANES__]; /// <brief MPC Combine mode as selected by the user; used in mode check stage
	enum dml_odm_use_policy ODMUse[__DML_NUM_PLANES__]; /// <brief ODM mode as selected by the user; used in mode check stage
	enum dml_unbounded_requesting_policy UseUnboundedRequesting; ///< brief Unbounded request mode preference
	enum dml_immediate_flip_requirement ImmediateFlipRequirement[__DML_NUM_PLANES__]; /// <brief Is immediate flip a requirement for this plane. When host vm is present iflip is needed regardless
	enum dml_prefetch_modes AllowForPStateChangeOrStutterInVBlank[__DML_NUM_PLANES__]; /// <brief To specify if the DML should calculate the values for support different pwr saving features (cstate; pstate; etc.) during vblank

	enum dml_prefetch_modes AllowForPStateChangeOrStutterInVBlankFinal;
	bool UseOnlyMaxPrefetchModes;
	dml_bool_t UseMinimumRequiredDCFCLK; //<brief When set the mode_check stage will figure the min DCFCLK freq to support the given display configuration. User can tell use the output DCFCLK for mode programming.
	dml_bool_t DRAMClockChangeRequirementFinal;
	dml_bool_t FCLKChangeRequirementFinal;
	dml_bool_t USRRetrainingRequiredFinal;
	dml_bool_t EnhancedPrefetchScheduleAccelerationFinal;

	dml_bool_t NomDETInKByteOverrideEnable; //<brief Nomimal DET buffer size for a pipe. If this size fit the required 2 swathes; DML will use this DET size
	dml_uint_t NomDETInKByteOverrideValue;

	dml_bool_t DCCProgrammingAssumesScanDirectionUnknownFinal;
	dml_bool_t SynchronizeTimingsFinal;
	dml_bool_t SynchronizeDRRDisplaysForUCLKPStateChangeFinal;
	dml_bool_t AssumeModeSupportAtMaxPwrStateEvenDRAMClockChangeNotSupported; //<brief if set; the mode support will say mode is supported even though the DRAM clock change is not support (assuming the soc will be stay in max power state)
	dml_bool_t AssumeModeSupportAtMaxPwrStateEvenFClockChangeNotSupported; //<brief if set; the mode support will say mode is supported even though the Fabric clock change is not support (assuming the soc will be stay in max power state
};

/// @brief Contains important information after the mode support steps. Also why a mode is not supported.
struct dml_mode_support_info_st {
	//-----------------
	// Mode Support Information
	//-----------------
	dml_bool_t ModeIsSupported; //<brief Is the mode support any voltage and combine setting
	dml_bool_t ImmediateFlipSupport; //<brief Means mode support immediate flip at the max combine setting; determine in mode support and used in mode programming
	dml_uint_t MaximumMPCCombine; //<brief If using MPC combine helps the power saving support; then this will be set to 1
	dml_bool_t UnboundedRequestEnabled;
	dml_uint_t CompressedBufferSizeInkByte;

	/* Mode Support Reason */
	dml_bool_t WritebackLatencySupport;
	dml_bool_t ScaleRatioAndTapsSupport;
	dml_bool_t SourceFormatPixelAndScanSupport;
	dml_bool_t MPCCombineMethodIncompatible;
	dml_bool_t P2IWith420;
	dml_bool_t DSCOnlyIfNecessaryWithBPP;
	dml_bool_t DSC422NativeNotSupported;
	dml_bool_t LinkRateDoesNotMatchDPVersion;
	dml_bool_t LinkRateForMultistreamNotIndicated;
	dml_bool_t BPPForMultistreamNotIndicated;
	dml_bool_t MultistreamWithHDMIOreDP;
	dml_bool_t MSOOrODMSplitWithNonDPLink;
	dml_bool_t NotEnoughLanesForMSO;
	dml_bool_t NumberOfOTGSupport;
	dml_bool_t NumberOfHDMIFRLSupport;
	dml_bool_t NumberOfDP2p0Support;
	dml_bool_t NonsupportedDSCInputBPC;
	dml_bool_t WritebackScaleRatioAndTapsSupport;
	dml_bool_t CursorSupport;
	dml_bool_t PitchSupport;
	dml_bool_t ViewportExceedsSurface;
	dml_bool_t ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified;
	dml_bool_t ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe;
	dml_bool_t InvalidCombinationOfMALLUseForPStateAndStaticScreen;
	dml_bool_t InvalidCombinationOfMALLUseForPState;
	dml_bool_t ExceededMALLSize;
	dml_bool_t EnoughWritebackUnits;

	dml_bool_t ExceededMultistreamSlots;
	dml_bool_t ODMCombineTwoToOneSupportCheckOK;
	dml_bool_t ODMCombineFourToOneSupportCheckOK;
	dml_bool_t NotEnoughDSCUnits;
	dml_bool_t NotEnoughDSCSlices;
	dml_bool_t PixelsPerLinePerDSCUnitSupport;
	dml_bool_t DSCCLKRequiredMoreThanSupported;
	dml_bool_t DTBCLKRequiredMoreThanSupported;
	dml_bool_t LinkCapacitySupport;

	dml_bool_t ROBSupport[2];
	dml_bool_t PTEBufferSizeNotExceeded[2];
	dml_bool_t DCCMetaBufferSizeNotExceeded[2];
	dml_bool_t TotalVerticalActiveBandwidthSupport[2];
	enum dml_dram_clock_change_support DRAMClockChangeSupport[2];
	dml_float_t ActiveDRAMClockChangeLatencyMargin[__DML_NUM_PLANES__];
	dml_uint_t SubViewportLinesNeededInMALL[__DML_NUM_PLANES__];
	enum dml_fclock_change_support FCLKChangeSupport[2];
	dml_bool_t USRRetrainingSupport[2];
	dml_bool_t VActiveBandwithSupport[2];
	dml_bool_t PrefetchSupported[2];
	dml_bool_t DynamicMetadataSupported[2];
	dml_bool_t VRatioInPrefetchSupported[2];
	dml_bool_t DISPCLK_DPPCLK_Support[2];
	dml_bool_t TotalAvailablePipesSupport[2];
	dml_bool_t ModeSupport[2];
	dml_bool_t ViewportSizeSupport[2];
	dml_bool_t ImmediateFlipSupportedForState[2];

	dml_bool_t NoTimeForPrefetch[2][__DML_NUM_PLANES__];
	dml_bool_t NoTimeForDynamicMetadata[2][__DML_NUM_PLANES__];

	dml_bool_t MPCCombineEnable[__DML_NUM_PLANES__]; /// <brief Indicate if the MPC Combine enable in the given state and optimize mpc combine setting
	enum dml_odm_mode ODMMode[__DML_NUM_PLANES__]; /// <brief ODM mode that is chosen in the mode check stage and will be used in mode programming stage
	dml_uint_t DPPPerSurface[__DML_NUM_PLANES__]; /// <brief How many DPPs are needed drive the surface to output. If MPCC or ODMC could be 2 or 4.
	dml_bool_t DSCEnabled[__DML_NUM_PLANES__]; /// <brief Indicate if the DSC is actually required; used in mode_programming
	dml_bool_t FECEnabled[__DML_NUM_PLANES__]; /// <brief Indicate if the FEC is actually required
	dml_uint_t NumberOfDSCSlices[__DML_NUM_PLANES__]; /// <brief Indicate how many slices needed to support the given mode

	dml_float_t OutputBpp[__DML_NUM_PLANES__];
	enum dml_output_type_and_rate__type OutputType[__DML_NUM_PLANES__];
	enum dml_output_type_and_rate__rate OutputRate[__DML_NUM_PLANES__];

	dml_float_t AlignedDCCMetaPitchY[__DML_NUM_PLANES__]; /// <brief Pitch value that is aligned to tiling setting
	dml_float_t AlignedDCCMetaPitchC[__DML_NUM_PLANES__];
	dml_float_t AlignedYPitch[__DML_NUM_PLANES__];
	dml_float_t AlignedCPitch[__DML_NUM_PLANES__];
	dml_float_t MaxTotalVerticalActiveAvailableBandwidth[2]; /// <brief nominal bw available for display
}; // dml_mode_support_info_st

/// @brief Treat this as the intermediate values and outputs of mode check function. User can query the content of the struct to know more about the result of mode evaluation.
struct mode_support_st {
	struct ip_params_st ip;
	struct soc_bounding_box_st soc;
	struct soc_state_bounding_box_st state; //<brief Per-state bbox values; only 1 state per compute
	struct dml_mode_eval_policy_st policy;

	dml_uint_t state_idx; //<brief The power state idx for the power state under this computation
	dml_uint_t max_state_idx; //<brief The MAX power state idx
	struct soc_state_bounding_box_st max_state; //<brief The MAX power state; some algo needs to know the max state info to determine if
	struct dml_display_cfg_st cache_display_cfg; // <brief A copy of the current display cfg in consideration

	// Physical info; only using for programming
	dml_uint_t num_active_planes; // <brief As determined by either e2e_pipe_param or display_cfg

	// Calculated Clocks
	dml_float_t RequiredDISPCLK[2]; /// <brief Required DISPCLK; depends on pixel rate; odm mode etc.
	dml_float_t RequiredDPPCLKThisState[__DML_NUM_PLANES__];
	dml_float_t DCFCLKState[2]; /// <brief recommended DCFCLK freq; calculated by DML. If UseMinimumRequiredDCFCLK is not set; then it will be just the state DCFCLK; else it will min DCFCLK for support
	dml_float_t RequiredDISPCLKPerSurface[2][__DML_NUM_PLANES__];
	dml_float_t RequiredDPPCLKPerSurface[2][__DML_NUM_PLANES__];

	dml_float_t FabricClock; /// <brief Basically just the clock freq at the min (or given) state
	dml_float_t DRAMSpeed; /// <brief Basically just the clock freq at the min (or given) state
	dml_float_t SOCCLK; /// <brief Basically just the clock freq at the min (or given) state
	dml_float_t DCFCLK; /// <brief Basically just the clock freq at the min (or given) state and max combine setting
	dml_float_t GlobalDPPCLK; /// <brief the Max DPPCLK freq out of all pipes

	// ----------------------------------
	// Mode Support Info and fail reason
	// ----------------------------------
	struct dml_mode_support_info_st support;

	// These are calculated before the ModeSupport and ModeProgram step
	// They represent the bound for the return buffer sizing
	dml_uint_t MaxTotalDETInKByte;
	dml_uint_t NomDETInKByte;
	dml_uint_t MinCompressedBufferSizeInKByte;

	// Info obtained at the end of mode support calculations
	// The reported info is at the "optimal" state and combine setting
	dml_float_t ReturnBW;
	dml_float_t ReturnDRAMBW;
	dml_uint_t DETBufferSizeInKByte[__DML_NUM_PLANES__]; // <brief Recommended DET size configuration for this plane. All pipes under this plane should program the DET buffer size to the calculated value.
	dml_uint_t DETBufferSizeY[__DML_NUM_PLANES__];
	dml_uint_t DETBufferSizeC[__DML_NUM_PLANES__];
	dml_uint_t SwathHeightY[__DML_NUM_PLANES__];
	dml_uint_t SwathHeightC[__DML_NUM_PLANES__];

	// ----------------------------------
	// Intermediates/Informational
	// ----------------------------------
	dml_uint_t TotImmediateFlipBytes;
	dml_bool_t DCCEnabledInAnySurface;
	dml_float_t WritebackRequiredDISPCLK;
	dml_float_t TimeCalc;
	dml_float_t TWait;

	dml_uint_t SwathWidthYAllStates[2][__DML_NUM_PLANES__];
	dml_uint_t SwathWidthCAllStates[2][__DML_NUM_PLANES__];
	dml_uint_t SwathHeightYAllStates[2][__DML_NUM_PLANES__];
	dml_uint_t SwathHeightCAllStates[2][__DML_NUM_PLANES__];
	dml_uint_t SwathWidthYThisState[__DML_NUM_PLANES__];
	dml_uint_t SwathWidthCThisState[__DML_NUM_PLANES__];
	dml_uint_t SwathHeightYThisState[__DML_NUM_PLANES__];
	dml_uint_t SwathHeightCThisState[__DML_NUM_PLANES__];
	dml_uint_t DETBufferSizeInKByteAllStates[2][__DML_NUM_PLANES__];
	dml_uint_t DETBufferSizeYAllStates[2][__DML_NUM_PLANES__];
	dml_uint_t DETBufferSizeCAllStates[2][__DML_NUM_PLANES__];
	dml_bool_t UnboundedRequestEnabledAllStates[2];
	dml_uint_t CompressedBufferSizeInkByteAllStates[2];
	dml_bool_t UnboundedRequestEnabledThisState;
	dml_uint_t CompressedBufferSizeInkByteThisState;
	dml_uint_t DETBufferSizeInKByteThisState[__DML_NUM_PLANES__];
	dml_uint_t DETBufferSizeYThisState[__DML_NUM_PLANES__];
	dml_uint_t DETBufferSizeCThisState[__DML_NUM_PLANES__];
	dml_float_t VRatioPreY[2][__DML_NUM_PLANES__];
	dml_float_t VRatioPreC[2][__DML_NUM_PLANES__];
	dml_uint_t swath_width_luma_ub_all_states[2][__DML_NUM_PLANES__];
	dml_uint_t swath_width_chroma_ub_all_states[2][__DML_NUM_PLANES__];
	dml_uint_t swath_width_luma_ub_this_state[__DML_NUM_PLANES__];
	dml_uint_t swath_width_chroma_ub_this_state[__DML_NUM_PLANES__];
	dml_uint_t RequiredSlots[__DML_NUM_PLANES__];
	dml_uint_t PDEAndMetaPTEBytesPerFrame[2][__DML_NUM_PLANES__];
	dml_uint_t MetaRowBytes[2][__DML_NUM_PLANES__];
	dml_uint_t DPTEBytesPerRow[2][__DML_NUM_PLANES__];
	dml_uint_t PrefetchLinesY[2][__DML_NUM_PLANES__];
	dml_uint_t PrefetchLinesC[2][__DML_NUM_PLANES__];
	dml_uint_t MaxNumSwY[__DML_NUM_PLANES__]; /// <brief Max number of swath for prefetch
	dml_uint_t MaxNumSwC[__DML_NUM_PLANES__]; /// <brief Max number of swath for prefetch
	dml_uint_t PrefillY[__DML_NUM_PLANES__];
	dml_uint_t PrefillC[__DML_NUM_PLANES__];

	dml_uint_t PrefetchLinesYThisState[__DML_NUM_PLANES__];
	dml_uint_t PrefetchLinesCThisState[__DML_NUM_PLANES__];
	dml_uint_t DPTEBytesPerRowThisState[__DML_NUM_PLANES__];
	dml_uint_t PDEAndMetaPTEBytesPerFrameThisState[__DML_NUM_PLANES__];
	dml_uint_t MetaRowBytesThisState[__DML_NUM_PLANES__];
	dml_bool_t use_one_row_for_frame[2][__DML_NUM_PLANES__];
	dml_bool_t use_one_row_for_frame_flip[2][__DML_NUM_PLANES__];
	dml_bool_t use_one_row_for_frame_this_state[__DML_NUM_PLANES__];
	dml_bool_t use_one_row_for_frame_flip_this_state[__DML_NUM_PLANES__];

	dml_float_t LineTimesForPrefetch[__DML_NUM_PLANES__];
	dml_float_t LinesForMetaPTE[__DML_NUM_PLANES__];
	dml_float_t LinesForMetaAndDPTERow[__DML_NUM_PLANES__];
	dml_float_t SwathWidthYSingleDPP[__DML_NUM_PLANES__];
	dml_float_t SwathWidthCSingleDPP[__DML_NUM_PLANES__];
	dml_uint_t BytePerPixelY[__DML_NUM_PLANES__];
	dml_uint_t BytePerPixelC[__DML_NUM_PLANES__];
	dml_float_t BytePerPixelInDETY[__DML_NUM_PLANES__];
	dml_float_t BytePerPixelInDETC[__DML_NUM_PLANES__];

	dml_uint_t Read256BlockHeightY[__DML_NUM_PLANES__];
	dml_uint_t Read256BlockWidthY[__DML_NUM_PLANES__];
	dml_uint_t Read256BlockHeightC[__DML_NUM_PLANES__];
	dml_uint_t Read256BlockWidthC[__DML_NUM_PLANES__];
	dml_uint_t MacroTileHeightY[__DML_NUM_PLANES__];
	dml_uint_t MacroTileHeightC[__DML_NUM_PLANES__];
	dml_uint_t MacroTileWidthY[__DML_NUM_PLANES__];
	dml_uint_t MacroTileWidthC[__DML_NUM_PLANES__];
	dml_float_t PSCL_FACTOR[__DML_NUM_PLANES__];
	dml_float_t PSCL_FACTOR_CHROMA[__DML_NUM_PLANES__];
	dml_float_t MaximumSwathWidthLuma[__DML_NUM_PLANES__];
	dml_float_t MaximumSwathWidthChroma[__DML_NUM_PLANES__];
	dml_float_t Tno_bw[__DML_NUM_PLANES__];
	dml_float_t DestinationLinesToRequestVMInImmediateFlip[__DML_NUM_PLANES__];
	dml_float_t DestinationLinesToRequestRowInImmediateFlip[__DML_NUM_PLANES__];
	dml_float_t WritebackDelayTime[__DML_NUM_PLANES__];
	dml_uint_t dpte_group_bytes[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_height[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_height_chroma[__DML_NUM_PLANES__];
	dml_uint_t meta_row_height[__DML_NUM_PLANES__];
	dml_uint_t meta_row_height_chroma[__DML_NUM_PLANES__];
	dml_float_t UrgLatency;
	dml_float_t UrgentBurstFactorCursor[__DML_NUM_PLANES__];
	dml_float_t UrgentBurstFactorCursorPre[__DML_NUM_PLANES__];
	dml_float_t UrgentBurstFactorLuma[__DML_NUM_PLANES__];
	dml_float_t UrgentBurstFactorLumaPre[__DML_NUM_PLANES__];
	dml_float_t UrgentBurstFactorChroma[__DML_NUM_PLANES__];
	dml_float_t UrgentBurstFactorChromaPre[__DML_NUM_PLANES__];
	dml_float_t MaximumSwathWidthInLineBufferLuma;
	dml_float_t MaximumSwathWidthInLineBufferChroma;
	dml_float_t ExtraLatency;

	// Backend
	dml_bool_t RequiresDSC[__DML_NUM_PLANES__];
	dml_bool_t RequiresFEC[__DML_NUM_PLANES__];
	dml_float_t OutputBppPerState[__DML_NUM_PLANES__];
	dml_uint_t DSCDelayPerState[__DML_NUM_PLANES__];
	enum dml_output_type_and_rate__type OutputTypePerState[__DML_NUM_PLANES__];
	enum dml_output_type_and_rate__rate OutputRatePerState[__DML_NUM_PLANES__];

	// Bandwidth Related Info
	dml_float_t BandwidthAvailableForImmediateFlip;
	dml_float_t ReadBandwidthLuma[__DML_NUM_PLANES__];
	dml_float_t ReadBandwidthChroma[__DML_NUM_PLANES__];
	dml_float_t WriteBandwidth[__DML_NUM_PLANES__];
	dml_float_t RequiredPrefetchPixelDataBWLuma[__DML_NUM_PLANES__];
	dml_float_t RequiredPrefetchPixelDataBWChroma[__DML_NUM_PLANES__];
	dml_float_t cursor_bw[__DML_NUM_PLANES__];
	dml_float_t cursor_bw_pre[__DML_NUM_PLANES__];
	dml_float_t prefetch_vmrow_bw[__DML_NUM_PLANES__];
	dml_float_t final_flip_bw[__DML_NUM_PLANES__];
	dml_float_t meta_row_bandwidth_this_state[__DML_NUM_PLANES__];
	dml_float_t dpte_row_bandwidth_this_state[__DML_NUM_PLANES__];
	dml_float_t ReturnBWPerState[2];
	dml_float_t ReturnDRAMBWPerState[2];
	dml_float_t meta_row_bandwidth[2][__DML_NUM_PLANES__];
	dml_float_t dpte_row_bandwidth[2][__DML_NUM_PLANES__];

	// Something that should be feedback to caller
	enum dml_odm_mode ODMModePerState[__DML_NUM_PLANES__];
	enum dml_odm_mode ODMModeThisState[__DML_NUM_PLANES__];
	dml_uint_t SurfaceSizeInMALL[__DML_NUM_PLANES__];
	dml_uint_t NoOfDPP[2][__DML_NUM_PLANES__];
	dml_uint_t NoOfDPPThisState[__DML_NUM_PLANES__];
	dml_bool_t MPCCombine[2][__DML_NUM_PLANES__];
	dml_bool_t MPCCombineThisState[__DML_NUM_PLANES__];
	dml_float_t ProjectedDCFCLKDeepSleep[2];
	dml_float_t MinDPPCLKUsingSingleDPP[__DML_NUM_PLANES__];
	dml_bool_t SingleDPPViewportSizeSupportPerSurface[__DML_NUM_PLANES__];
	dml_bool_t ImmediateFlipSupportedForPipe[__DML_NUM_PLANES__];
	dml_bool_t NotUrgentLatencyHiding[__DML_NUM_PLANES__];
	dml_bool_t NotUrgentLatencyHidingPre[__DML_NUM_PLANES__];
	dml_bool_t PTEBufferSizeNotExceededPerState[__DML_NUM_PLANES__];
	dml_bool_t DCCMetaBufferSizeNotExceededPerState[__DML_NUM_PLANES__];
	dml_uint_t PrefetchMode[__DML_NUM_PLANES__];
	dml_uint_t TotalNumberOfActiveDPP[2];
	dml_uint_t TotalNumberOfSingleDPPSurfaces[2];
	dml_uint_t TotalNumberOfDCCActiveDPP[2];

	dml_uint_t SubViewportLinesNeededInMALL[__DML_NUM_PLANES__];

}; // mode_support_st

/// @brief A mega structure that houses various info for model programming step.
struct mode_program_st {

	//-------------
	// Intermediate/Informational
	//-------------
	dml_float_t UrgentLatency;
	dml_float_t UrgentLatencyWithUSRRetraining;
	dml_uint_t VInitPreFillY[__DML_NUM_PLANES__];
	dml_uint_t VInitPreFillC[__DML_NUM_PLANES__];
	dml_uint_t MaxNumSwathY[__DML_NUM_PLANES__];
	dml_uint_t MaxNumSwathC[__DML_NUM_PLANES__];

	dml_float_t BytePerPixelDETY[__DML_NUM_PLANES__];
	dml_float_t BytePerPixelDETC[__DML_NUM_PLANES__];
	dml_uint_t BytePerPixelY[__DML_NUM_PLANES__];
	dml_uint_t BytePerPixelC[__DML_NUM_PLANES__];
	dml_uint_t SwathWidthY[__DML_NUM_PLANES__];
	dml_uint_t SwathWidthC[__DML_NUM_PLANES__];
	dml_uint_t SwathWidthSingleDPPY[__DML_NUM_PLANES__];
	dml_uint_t SwathWidthSingleDPPC[__DML_NUM_PLANES__];
	dml_float_t ReadBandwidthSurfaceLuma[__DML_NUM_PLANES__];
	dml_float_t ReadBandwidthSurfaceChroma[__DML_NUM_PLANES__];

	dml_uint_t PixelPTEBytesPerRow[__DML_NUM_PLANES__];
	dml_uint_t PDEAndMetaPTEBytesFrame[__DML_NUM_PLANES__];
	dml_uint_t MetaRowByte[__DML_NUM_PLANES__];
	dml_uint_t PrefetchSourceLinesY[__DML_NUM_PLANES__];
	dml_float_t RequiredPrefetchPixDataBWLuma[__DML_NUM_PLANES__];
	dml_float_t RequiredPrefetchPixDataBWChroma[__DML_NUM_PLANES__];
	dml_uint_t PrefetchSourceLinesC[__DML_NUM_PLANES__];
	dml_float_t PSCL_THROUGHPUT[__DML_NUM_PLANES__];
	dml_float_t PSCL_THROUGHPUT_CHROMA[__DML_NUM_PLANES__];
	dml_uint_t DSCDelay[__DML_NUM_PLANES__];
	dml_float_t DPPCLKUsingSingleDPP[__DML_NUM_PLANES__];

	dml_uint_t MacroTileWidthY[__DML_NUM_PLANES__];
	dml_uint_t MacroTileWidthC[__DML_NUM_PLANES__];
	dml_uint_t BlockHeight256BytesY[__DML_NUM_PLANES__];
	dml_uint_t BlockHeight256BytesC[__DML_NUM_PLANES__];
	dml_uint_t BlockWidth256BytesY[__DML_NUM_PLANES__];
	dml_uint_t BlockWidth256BytesC[__DML_NUM_PLANES__];

	dml_uint_t BlockHeightY[__DML_NUM_PLANES__];
	dml_uint_t BlockHeightC[__DML_NUM_PLANES__];
	dml_uint_t BlockWidthY[__DML_NUM_PLANES__];
	dml_uint_t BlockWidthC[__DML_NUM_PLANES__];

	dml_uint_t SurfaceSizeInTheMALL[__DML_NUM_PLANES__];
	dml_float_t VRatioPrefetchY[__DML_NUM_PLANES__];
	dml_float_t VRatioPrefetchC[__DML_NUM_PLANES__];
	dml_float_t Tno_bw[__DML_NUM_PLANES__];
	dml_float_t final_flip_bw[__DML_NUM_PLANES__];
	dml_float_t prefetch_vmrow_bw[__DML_NUM_PLANES__];
	dml_float_t cursor_bw[__DML_NUM_PLANES__];
	dml_float_t cursor_bw_pre[__DML_NUM_PLANES__];
	dml_float_t WritebackDelay[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_height[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_height_linear[__DML_NUM_PLANES__];
	dml_uint_t meta_req_width[__DML_NUM_PLANES__];
	dml_uint_t meta_req_height[__DML_NUM_PLANES__];
	dml_uint_t meta_row_width[__DML_NUM_PLANES__];
	dml_uint_t meta_row_height[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_width_luma_ub[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_width_chroma_ub[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_height_chroma[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_height_linear_chroma[__DML_NUM_PLANES__];
	dml_uint_t meta_req_width_chroma[__DML_NUM_PLANES__];
	dml_uint_t meta_req_height_chroma[__DML_NUM_PLANES__];
	dml_uint_t meta_row_width_chroma[__DML_NUM_PLANES__];
	dml_uint_t meta_row_height_chroma[__DML_NUM_PLANES__];
	dml_uint_t vm_group_bytes[__DML_NUM_PLANES__];
	dml_uint_t dpte_group_bytes[__DML_NUM_PLANES__];
	dml_float_t meta_row_bw[__DML_NUM_PLANES__];
	dml_float_t dpte_row_bw[__DML_NUM_PLANES__];
	dml_float_t UrgBurstFactorCursor[__DML_NUM_PLANES__];
	dml_float_t UrgBurstFactorCursorPre[__DML_NUM_PLANES__];
	dml_float_t UrgBurstFactorLuma[__DML_NUM_PLANES__];
	dml_float_t UrgBurstFactorLumaPre[__DML_NUM_PLANES__];
	dml_float_t UrgBurstFactorChroma[__DML_NUM_PLANES__];
	dml_float_t UrgBurstFactorChromaPre[__DML_NUM_PLANES__];

	dml_uint_t swath_width_luma_ub[__DML_NUM_PLANES__];
	dml_uint_t swath_width_chroma_ub[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEReqWidthY[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEReqHeightY[__DML_NUM_PLANES__];
	dml_uint_t PTERequestSizeY[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEReqWidthC[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEReqHeightC[__DML_NUM_PLANES__];
	dml_uint_t PTERequestSizeC[__DML_NUM_PLANES__];

	dml_float_t Tdmdl_vm[__DML_NUM_PLANES__];
	dml_float_t Tdmdl[__DML_NUM_PLANES__];
	dml_float_t TSetup[__DML_NUM_PLANES__];
	dml_uint_t dpde0_bytes_per_frame_ub_l[__DML_NUM_PLANES__];
	dml_uint_t meta_pte_bytes_per_frame_ub_l[__DML_NUM_PLANES__];
	dml_uint_t dpde0_bytes_per_frame_ub_c[__DML_NUM_PLANES__];
	dml_uint_t meta_pte_bytes_per_frame_ub_c[__DML_NUM_PLANES__];

	dml_bool_t UnboundedRequestEnabled;
	dml_uint_t compbuf_reserved_space_64b;
	dml_uint_t compbuf_reserved_space_zs;
	dml_uint_t CompressedBufferSizeInkByte;

	dml_bool_t NoUrgentLatencyHiding[__DML_NUM_PLANES__];
	dml_bool_t NoUrgentLatencyHidingPre[__DML_NUM_PLANES__];
	dml_float_t UrgentExtraLatency;
	dml_bool_t PrefetchAndImmediateFlipSupported;
	dml_float_t TotalDataReadBandwidth;
	dml_float_t BandwidthAvailableForImmediateFlip;
	dml_bool_t NotEnoughTimeForDynamicMetadata[__DML_NUM_PLANES__];

	dml_float_t ReadBandwidthLuma[__DML_NUM_PLANES__];
	dml_float_t ReadBandwidthChroma[__DML_NUM_PLANES__];

	dml_float_t total_dcn_read_bw_with_flip;
	dml_float_t total_dcn_read_bw_with_flip_no_urgent_burst;
	dml_float_t TotalDataReadBandwidthNotIncludingMALLPrefetch;
	dml_float_t total_dcn_read_bw_with_flip_not_including_MALL_prefetch;
	dml_float_t non_urgent_total_dcn_read_bw_with_flip;
	dml_float_t non_urgent_total_dcn_read_bw_with_flip_not_including_MALL_prefetch;

	dml_bool_t use_one_row_for_frame[__DML_NUM_PLANES__];
	dml_bool_t use_one_row_for_frame_flip[__DML_NUM_PLANES__];

	dml_float_t TCalc;
	dml_uint_t TotImmediateFlipBytes;

	// -------------------
	// Output
	// -------------------
	dml_uint_t pipe_plane[__DML_NUM_PLANES__]; // <brief used mainly by dv to map the pipe inst to plane index within DML core; the plane idx of a pipe
	dml_uint_t num_active_pipes;

	dml_bool_t NoTimeToPrefetch[__DML_NUM_PLANES__]; /// <brief Prefetch schedule calculation result

	// Support
	dml_uint_t PrefetchMode[__DML_NUM_PLANES__]; /// <brief prefetch mode used for prefetch support check in mode programming step
	dml_bool_t PrefetchModeSupported; /// <brief Is the prefetch mode (bandwidth and latency) supported
	dml_bool_t ImmediateFlipSupported;
	dml_bool_t ImmediateFlipSupportedForPipe[__DML_NUM_PLANES__];

	// Clock
	dml_float_t Dcfclk;
	dml_float_t Dispclk; /// <brief dispclk being used in mode programming
	dml_float_t Dppclk[__DML_NUM_PLANES__]; /// <brief dppclk being used in mode programming
	dml_float_t WritebackDISPCLK;
	dml_float_t GlobalDPPCLK;

	//@ brief These "calculated" dispclk and dppclk clocks are calculated in the mode programming step.
	// Depends on the dml_clk_cfg_st option; these calculated values may not used in subsequent calculation.
	// Possible DV usage: Calculated values fetched by test once after mode_programming step and then possibly
	// use the values as min and adjust the actual freq used for the 2nd pass
	dml_float_t Dispclk_calculated;
	dml_float_t Dppclk_calculated[__DML_NUM_PLANES__];

	dml_float_t DSCCLK_calculated[__DML_NUM_PLANES__]; //< brief Required DSCCLK freq. Backend; not used in any subsequent calculations for now
	dml_float_t DCFCLKDeepSleep;

	// ARB reg
	dml_bool_t DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE;
	struct Watermarks Watermark;

	// DCC compression control
	dml_uint_t DCCYMaxUncompressedBlock[__DML_NUM_PLANES__];
	dml_uint_t DCCYMaxCompressedBlock[__DML_NUM_PLANES__];
	dml_uint_t DCCYIndependentBlock[__DML_NUM_PLANES__];
	dml_uint_t DCCCMaxUncompressedBlock[__DML_NUM_PLANES__];
	dml_uint_t DCCCMaxCompressedBlock[__DML_NUM_PLANES__];
	dml_uint_t DCCCIndependentBlock[__DML_NUM_PLANES__];

	// Stutter Efficiency
	dml_float_t StutterEfficiency;
	dml_float_t StutterEfficiencyNotIncludingVBlank;
	dml_uint_t NumberOfStutterBurstsPerFrame;
	dml_float_t Z8StutterEfficiency;
	dml_uint_t Z8NumberOfStutterBurstsPerFrame;
	dml_float_t Z8StutterEfficiencyNotIncludingVBlank;
	dml_float_t StutterPeriod;
	dml_float_t Z8StutterEfficiencyBestCase;
	dml_uint_t Z8NumberOfStutterBurstsPerFrameBestCase;
	dml_float_t Z8StutterEfficiencyNotIncludingVBlankBestCase;
	dml_float_t StutterPeriodBestCase;

	// DLG TTU reg
	dml_float_t MIN_DST_Y_NEXT_START[__DML_NUM_PLANES__];
	dml_bool_t VREADY_AT_OR_AFTER_VSYNC[__DML_NUM_PLANES__];
	dml_uint_t DSTYAfterScaler[__DML_NUM_PLANES__];
	dml_uint_t DSTXAfterScaler[__DML_NUM_PLANES__];
	dml_float_t DestinationLinesForPrefetch[__DML_NUM_PLANES__];
	dml_float_t DestinationLinesToRequestVMInVBlank[__DML_NUM_PLANES__];
	dml_float_t DestinationLinesToRequestRowInVBlank[__DML_NUM_PLANES__];
	dml_float_t DestinationLinesToRequestVMInImmediateFlip[__DML_NUM_PLANES__];
	dml_float_t DestinationLinesToRequestRowInImmediateFlip[__DML_NUM_PLANES__];
	dml_float_t MinTTUVBlank[__DML_NUM_PLANES__];
	dml_float_t DisplayPipeLineDeliveryTimeLuma[__DML_NUM_PLANES__];
	dml_float_t DisplayPipeLineDeliveryTimeChroma[__DML_NUM_PLANES__];
	dml_float_t DisplayPipeLineDeliveryTimeLumaPrefetch[__DML_NUM_PLANES__];
	dml_float_t DisplayPipeLineDeliveryTimeChromaPrefetch[__DML_NUM_PLANES__];
	dml_float_t DisplayPipeRequestDeliveryTimeLuma[__DML_NUM_PLANES__];
	dml_float_t DisplayPipeRequestDeliveryTimeChroma[__DML_NUM_PLANES__];
	dml_float_t DisplayPipeRequestDeliveryTimeLumaPrefetch[__DML_NUM_PLANES__];
	dml_float_t DisplayPipeRequestDeliveryTimeChromaPrefetch[__DML_NUM_PLANES__];
	dml_float_t CursorRequestDeliveryTime[__DML_NUM_PLANES__];
	dml_float_t CursorRequestDeliveryTimePrefetch[__DML_NUM_PLANES__];

	dml_float_t DST_Y_PER_PTE_ROW_NOM_L[__DML_NUM_PLANES__];
	dml_float_t DST_Y_PER_PTE_ROW_NOM_C[__DML_NUM_PLANES__];
	dml_float_t DST_Y_PER_META_ROW_NOM_L[__DML_NUM_PLANES__];
	dml_float_t DST_Y_PER_META_ROW_NOM_C[__DML_NUM_PLANES__];
	dml_float_t TimePerMetaChunkNominal[__DML_NUM_PLANES__];
	dml_float_t TimePerChromaMetaChunkNominal[__DML_NUM_PLANES__];
	dml_float_t TimePerMetaChunkVBlank[__DML_NUM_PLANES__];
	dml_float_t TimePerChromaMetaChunkVBlank[__DML_NUM_PLANES__];
	dml_float_t TimePerMetaChunkFlip[__DML_NUM_PLANES__];
	dml_float_t TimePerChromaMetaChunkFlip[__DML_NUM_PLANES__];
	dml_float_t time_per_pte_group_nom_luma[__DML_NUM_PLANES__];
	dml_float_t time_per_pte_group_nom_chroma[__DML_NUM_PLANES__];
	dml_float_t time_per_pte_group_vblank_luma[__DML_NUM_PLANES__];
	dml_float_t time_per_pte_group_vblank_chroma[__DML_NUM_PLANES__];
	dml_float_t time_per_pte_group_flip_luma[__DML_NUM_PLANES__];
	dml_float_t time_per_pte_group_flip_chroma[__DML_NUM_PLANES__];
	dml_float_t TimePerVMGroupVBlank[__DML_NUM_PLANES__];
	dml_float_t TimePerVMGroupFlip[__DML_NUM_PLANES__];
	dml_float_t TimePerVMRequestVBlank[__DML_NUM_PLANES__];
	dml_float_t TimePerVMRequestFlip[__DML_NUM_PLANES__];

	dml_float_t FractionOfUrgentBandwidth;
	dml_float_t FractionOfUrgentBandwidthImmediateFlip;

	// RQ registers
	dml_bool_t PTE_BUFFER_MODE[__DML_NUM_PLANES__];
	dml_uint_t BIGK_FRAGMENT_SIZE[__DML_NUM_PLANES__];

	dml_uint_t SubViewportLinesNeededInMALL[__DML_NUM_PLANES__];
	dml_bool_t UsesMALLForStaticScreen[__DML_NUM_PLANES__];

	// OTG
	dml_uint_t VStartupMin[__DML_NUM_PLANES__]; /// <brief Minimum vstartup to meet the prefetch schedule (i.e. the prefetch solution can be found at this vstartup time); not the actual global sync vstartup pos.
	dml_uint_t VStartup[__DML_NUM_PLANES__]; /// <brief The vstartup value for OTG programming (will set to max vstartup; but now bounded by min(vblank_nom. actual vblank))
	dml_uint_t VUpdateOffsetPix[__DML_NUM_PLANES__];
	dml_uint_t VUpdateWidthPix[__DML_NUM_PLANES__];
	dml_uint_t VReadyOffsetPix[__DML_NUM_PLANES__];

	// Latency and Support
	dml_float_t MaxActiveFCLKChangeLatencySupported;
	dml_bool_t USRRetrainingSupport;
	enum dml_fclock_change_support FCLKChangeSupport;
	enum dml_dram_clock_change_support DRAMClockChangeSupport;
	dml_float_t MaxActiveDRAMClockChangeLatencySupported[__DML_NUM_PLANES__];
	dml_float_t WritebackAllowFCLKChangeEndPosition[__DML_NUM_PLANES__];
	dml_float_t WritebackAllowDRAMClockChangeEndPosition[__DML_NUM_PLANES__];

	// buffer sizing
	dml_uint_t DETBufferSizeInKByte[__DML_NUM_PLANES__];  // <brief Recommended DET size configuration for this plane.  All pipes under this plane should program the DET buffer size to the calculated value.
	dml_uint_t DETBufferSizeY[__DML_NUM_PLANES__];
	dml_uint_t DETBufferSizeC[__DML_NUM_PLANES__];
	dml_uint_t SwathHeightY[__DML_NUM_PLANES__];
	dml_uint_t SwathHeightC[__DML_NUM_PLANES__];
}; // mode_program_st

struct soc_states_st {
	dml_uint_t num_states; /// <brief num of soc pwr states
	struct soc_state_bounding_box_st state_array[__DML_MAX_STATE_ARRAY_SIZE__]; /// <brief fixed size array that holds states struct
};

struct UseMinimumDCFCLK_params_st {
	enum dml_use_mall_for_pstate_change_mode *UseMALLForPStateChange;
	dml_bool_t *DRRDisplay;
	dml_bool_t SynchronizeDRRDisplaysForUCLKPStateChangeFinal;
	dml_uint_t MaxInterDCNTileRepeaters;
	dml_uint_t MaxPrefetchMode;
	dml_float_t DRAMClockChangeLatencyFinal;
	dml_float_t FCLKChangeLatency;
	dml_float_t SREnterPlusExitTime;
	dml_uint_t ReturnBusWidth;
	dml_uint_t RoundTripPingLatencyCycles;
	dml_uint_t ReorderingBytes;
	dml_uint_t PixelChunkSizeInKByte;
	dml_uint_t MetaChunkSize;
	dml_bool_t GPUVMEnable;
	dml_uint_t GPUVMMaxPageTableLevels;
	dml_bool_t HostVMEnable;
	dml_uint_t NumberOfActiveSurfaces;
	dml_uint_t HostVMMinPageSize;
	dml_uint_t HostVMMaxNonCachedPageTableLevels;
	dml_bool_t DynamicMetadataVMEnabled;
	dml_bool_t ImmediateFlipRequirement;
	dml_bool_t ProgressiveToInterlaceUnitInOPP;
	dml_float_t MaxAveragePercentOfIdealSDPPortBWDisplayCanUseInNormalSystemOperation;
	dml_float_t PercentOfIdealSDPPortBWReceivedAfterUrgLatency;
	dml_uint_t *VTotal;
	dml_uint_t *VActive;
	dml_uint_t *DynamicMetadataTransmittedBytes;
	dml_uint_t *DynamicMetadataLinesBeforeActiveRequired;
	dml_bool_t *Interlace;
	dml_float_t (*RequiredDPPCLKPerSurface)[__DML_NUM_PLANES__];
	dml_float_t *RequiredDISPCLK;
	dml_float_t UrgLatency;
	dml_uint_t (*NoOfDPP)[__DML_NUM_PLANES__];
	dml_float_t *ProjectedDCFCLKDeepSleep;
	dml_uint_t (*MaximumVStartup)[__DML_NUM_PLANES__];
	dml_uint_t *TotalNumberOfActiveDPP;
	dml_uint_t *TotalNumberOfDCCActiveDPP;
	dml_uint_t *dpte_group_bytes;
	dml_uint_t (*PrefetchLinesY)[__DML_NUM_PLANES__];
	dml_uint_t (*PrefetchLinesC)[__DML_NUM_PLANES__];
	dml_uint_t (*swath_width_luma_ub_all_states)[__DML_NUM_PLANES__];
	dml_uint_t (*swath_width_chroma_ub_all_states)[__DML_NUM_PLANES__];
	dml_uint_t *BytePerPixelY;
	dml_uint_t *BytePerPixelC;
	dml_uint_t *HTotal;
	dml_float_t *PixelClock;
	dml_uint_t (*PDEAndMetaPTEBytesPerFrame)[__DML_NUM_PLANES__];
	dml_uint_t (*DPTEBytesPerRow)[__DML_NUM_PLANES__];
	dml_uint_t (*MetaRowBytes)[__DML_NUM_PLANES__];
	dml_bool_t *DynamicMetadataEnable;
	dml_float_t *ReadBandwidthLuma;
	dml_float_t *ReadBandwidthChroma;
	dml_float_t DCFCLKPerState;
	dml_float_t *DCFCLKState;
};

struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params_st {
	dml_bool_t USRRetrainingRequiredFinal;
	enum dml_use_mall_for_pstate_change_mode *UseMALLForPStateChange;
	dml_uint_t *PrefetchMode;
	dml_uint_t NumberOfActiveSurfaces;
	dml_uint_t MaxLineBufferLines;
	dml_uint_t LineBufferSize;
	dml_uint_t WritebackInterfaceBufferSize;
	dml_float_t DCFCLK;
	dml_float_t ReturnBW;
	dml_bool_t SynchronizeTimingsFinal;
	dml_bool_t SynchronizeDRRDisplaysForUCLKPStateChangeFinal;
	dml_bool_t *DRRDisplay;
	dml_uint_t *dpte_group_bytes;
	dml_uint_t *meta_row_height;
	dml_uint_t *meta_row_height_chroma;
	struct SOCParametersList mmSOCParameters;
	dml_uint_t WritebackChunkSize;
	dml_float_t SOCCLK;
	dml_float_t DCFClkDeepSleep;
	dml_uint_t *DETBufferSizeY;
	dml_uint_t *DETBufferSizeC;
	dml_uint_t *SwathHeightY;
	dml_uint_t *SwathHeightC;
	dml_uint_t *LBBitPerPixel;
	dml_uint_t *SwathWidthY;
	dml_uint_t *SwathWidthC;
	dml_float_t *HRatio;
	dml_float_t *HRatioChroma;
	dml_uint_t *VTaps;
	dml_uint_t *VTapsChroma;
	dml_float_t *VRatio;
	dml_float_t *VRatioChroma;
	dml_uint_t *HTotal;
	dml_uint_t *VTotal;
	dml_uint_t *VActive;
	dml_float_t *PixelClock;
	dml_uint_t *BlendingAndTiming;
	dml_uint_t *DPPPerSurface;
	dml_float_t *BytePerPixelDETY;
	dml_float_t *BytePerPixelDETC;
	dml_uint_t *DSTXAfterScaler;
	dml_uint_t *DSTYAfterScaler;
	dml_bool_t *WritebackEnable;
	enum dml_source_format_class *WritebackPixelFormat;
	dml_uint_t *WritebackDestinationWidth;
	dml_uint_t *WritebackDestinationHeight;
	dml_uint_t *WritebackSourceHeight;
	dml_bool_t UnboundedRequestEnabled;
	dml_uint_t CompressedBufferSizeInkByte;

	// Output
	struct Watermarks *Watermark;
	enum dml_dram_clock_change_support *DRAMClockChangeSupport;
	dml_float_t *MaxActiveDRAMClockChangeLatencySupported;
	dml_uint_t *SubViewportLinesNeededInMALL;
	enum dml_fclock_change_support *FCLKChangeSupport;
	dml_float_t *MaxActiveFCLKChangeLatencySupported;
	dml_bool_t *USRRetrainingSupport;
	dml_float_t *ActiveDRAMClockChangeLatencyMargin;
};

struct CalculateVMRowAndSwath_params_st {
	dml_uint_t NumberOfActiveSurfaces;
	struct DmlPipe *myPipe;
	dml_uint_t *SurfaceSizeInMALL;
	dml_uint_t PTEBufferSizeInRequestsLuma;
	dml_uint_t PTEBufferSizeInRequestsChroma;
	dml_uint_t DCCMetaBufferSizeBytes;
	enum dml_use_mall_for_static_screen_mode *UseMALLForStaticScreen;
	enum dml_use_mall_for_pstate_change_mode *UseMALLForPStateChange;
	dml_uint_t MALLAllocatedForDCN;
	dml_uint_t *SwathWidthY;
	dml_uint_t *SwathWidthC;
	dml_bool_t GPUVMEnable;
	dml_bool_t HostVMEnable;
	dml_uint_t HostVMMaxNonCachedPageTableLevels;
	dml_uint_t GPUVMMaxPageTableLevels;
	dml_uint_t *GPUVMMinPageSizeKBytes;
	dml_uint_t HostVMMinPageSize;
	dml_bool_t *PTEBufferModeOverrideEn;
	dml_bool_t *PTEBufferModeOverrideVal;

	// Output
	dml_bool_t *PTEBufferSizeNotExceeded;
	dml_bool_t *DCCMetaBufferSizeNotExceeded;
	dml_uint_t *dpte_row_width_luma_ub;
	dml_uint_t *dpte_row_width_chroma_ub;
	dml_uint_t *dpte_row_height_luma;
	dml_uint_t *dpte_row_height_chroma;
	dml_uint_t *dpte_row_height_linear_luma; // VBA_DELTA
	dml_uint_t *dpte_row_height_linear_chroma; // VBA_DELTA
	dml_uint_t *meta_req_width;
	dml_uint_t *meta_req_width_chroma;
	dml_uint_t *meta_req_height;
	dml_uint_t *meta_req_height_chroma;
	dml_uint_t *meta_row_width;
	dml_uint_t *meta_row_width_chroma;
	dml_uint_t *meta_row_height;
	dml_uint_t *meta_row_height_chroma;
	dml_uint_t *vm_group_bytes;
	dml_uint_t *dpte_group_bytes;
	dml_uint_t *PixelPTEReqWidthY;
	dml_uint_t *PixelPTEReqHeightY;
	dml_uint_t *PTERequestSizeY;
	dml_uint_t *PixelPTEReqWidthC;
	dml_uint_t *PixelPTEReqHeightC;
	dml_uint_t *PTERequestSizeC;
	dml_uint_t *dpde0_bytes_per_frame_ub_l;
	dml_uint_t *meta_pte_bytes_per_frame_ub_l;
	dml_uint_t *dpde0_bytes_per_frame_ub_c;
	dml_uint_t *meta_pte_bytes_per_frame_ub_c;
	dml_uint_t *PrefetchSourceLinesY;
	dml_uint_t *PrefetchSourceLinesC;
	dml_uint_t *VInitPreFillY;
	dml_uint_t *VInitPreFillC;
	dml_uint_t *MaxNumSwathY;
	dml_uint_t *MaxNumSwathC;
	dml_float_t *meta_row_bw;
	dml_float_t *dpte_row_bw;
	dml_uint_t *PixelPTEBytesPerRow;
	dml_uint_t *PDEAndMetaPTEBytesFrame;
	dml_uint_t *MetaRowByte;
	dml_bool_t *use_one_row_for_frame;
	dml_bool_t *use_one_row_for_frame_flip;
	dml_bool_t *UsesMALLForStaticScreen;
	dml_bool_t *PTE_BUFFER_MODE;
	dml_uint_t *BIGK_FRAGMENT_SIZE;
};

struct CalculateSwathAndDETConfiguration_params_st {
	dml_uint_t *DETSizeOverride;
	enum dml_use_mall_for_pstate_change_mode *UseMALLForPStateChange;
	dml_uint_t ConfigReturnBufferSizeInKByte;
	dml_uint_t ROBBufferSizeInKByte;
	dml_uint_t MaxTotalDETInKByte;
	dml_uint_t MinCompressedBufferSizeInKByte;
	dml_uint_t PixelChunkSizeInKByte;
	dml_bool_t ForceSingleDPP;
	dml_uint_t NumberOfActiveSurfaces;
	dml_uint_t nomDETInKByte;
	enum dml_unbounded_requesting_policy UseUnboundedRequestingFinal;
	dml_uint_t ConfigReturnBufferSegmentSizeInkByte;
	dml_uint_t CompressedBufferSegmentSizeInkByteFinal;
	enum dml_output_encoder_class *Output;
	dml_float_t *ReadBandwidthLuma;
	dml_float_t *ReadBandwidthChroma;
	dml_float_t *MaximumSwathWidthLuma;
	dml_float_t *MaximumSwathWidthChroma;
	enum dml_rotation_angle *SourceScan;
	dml_bool_t *ViewportStationary;
	enum dml_source_format_class *SourcePixelFormat;
	enum dml_swizzle_mode *SurfaceTiling;
	dml_uint_t *ViewportWidth;
	dml_uint_t *ViewportHeight;
	dml_uint_t *ViewportXStart;
	dml_uint_t *ViewportYStart;
	dml_uint_t *ViewportXStartC;
	dml_uint_t *ViewportYStartC;
	dml_uint_t *SurfaceWidthY;
	dml_uint_t *SurfaceWidthC;
	dml_uint_t *SurfaceHeightY;
	dml_uint_t *SurfaceHeightC;
	dml_uint_t *Read256BytesBlockHeightY;
	dml_uint_t *Read256BytesBlockHeightC;
	dml_uint_t *Read256BytesBlockWidthY;
	dml_uint_t *Read256BytesBlockWidthC;
	enum dml_odm_mode *ODMMode;
	dml_uint_t *BlendingAndTiming;
	dml_uint_t *BytePerPixY;
	dml_uint_t *BytePerPixC;
	dml_float_t *BytePerPixDETY;
	dml_float_t *BytePerPixDETC;
	dml_uint_t *HActive;
	dml_float_t *HRatio;
	dml_float_t *HRatioChroma;
	dml_uint_t *DPPPerSurface;
	dml_uint_t *swath_width_luma_ub;
	dml_uint_t *swath_width_chroma_ub;
	dml_uint_t *SwathWidth;
	dml_uint_t *SwathWidthChroma;
	dml_uint_t *SwathHeightY;
	dml_uint_t *SwathHeightC;
	dml_uint_t *DETBufferSizeInKByte;
	dml_uint_t *DETBufferSizeY;
	dml_uint_t *DETBufferSizeC;
	dml_bool_t *UnboundedRequestEnabled;
	dml_uint_t *compbuf_reserved_space_64b;
	dml_uint_t *compbuf_reserved_space_zs;
	dml_uint_t *CompressedBufferSizeInkByte;
	dml_bool_t *ViewportSizeSupportPerSurface;
	dml_bool_t *ViewportSizeSupport;
};

struct CalculateStutterEfficiency_params_st {
	dml_uint_t CompressedBufferSizeInkByte;
	enum dml_use_mall_for_pstate_change_mode *UseMALLForPStateChange;
	dml_bool_t UnboundedRequestEnabled;
	dml_uint_t MetaFIFOSizeInKEntries;
	dml_uint_t ZeroSizeBufferEntries;
	dml_uint_t PixelChunkSizeInKByte;
	dml_uint_t NumberOfActiveSurfaces;
	dml_uint_t ROBBufferSizeInKByte;
	dml_float_t TotalDataReadBandwidth;
	dml_float_t DCFCLK;
	dml_float_t ReturnBW;
	dml_uint_t CompbufReservedSpace64B;
	dml_uint_t CompbufReservedSpaceZs;
	dml_float_t SRExitTime;
	dml_float_t SRExitZ8Time;
	dml_bool_t SynchronizeTimingsFinal;
	dml_uint_t *BlendingAndTiming;
	dml_float_t StutterEnterPlusExitWatermark;
	dml_float_t Z8StutterEnterPlusExitWatermark;
	dml_bool_t ProgressiveToInterlaceUnitInOPP;
	dml_bool_t *Interlace;
	dml_float_t *MinTTUVBlank;
	dml_uint_t *DPPPerSurface;
	dml_uint_t *DETBufferSizeY;
	dml_uint_t *BytePerPixelY;
	dml_float_t *BytePerPixelDETY;
	dml_uint_t *SwathWidthY;
	dml_uint_t *SwathHeightY;
	dml_uint_t *SwathHeightC;
	dml_float_t *NetDCCRateLuma;
	dml_float_t *NetDCCRateChroma;
	dml_float_t *DCCFractionOfZeroSizeRequestsLuma;
	dml_float_t *DCCFractionOfZeroSizeRequestsChroma;
	dml_uint_t *HTotal;
	dml_uint_t *VTotal;
	dml_float_t *PixelClock;
	dml_float_t *VRatio;
	enum dml_rotation_angle *SourceScan;
	dml_uint_t *BlockHeight256BytesY;
	dml_uint_t *BlockWidth256BytesY;
	dml_uint_t *BlockHeight256BytesC;
	dml_uint_t *BlockWidth256BytesC;
	dml_uint_t *DCCYMaxUncompressedBlock;
	dml_uint_t *DCCCMaxUncompressedBlock;
	dml_uint_t *VActive;
	dml_bool_t *DCCEnable;
	dml_bool_t *WritebackEnable;
	dml_float_t *ReadBandwidthSurfaceLuma;
	dml_float_t *ReadBandwidthSurfaceChroma;
	dml_float_t *meta_row_bw;
	dml_float_t *dpte_row_bw;
	dml_float_t *StutterEfficiencyNotIncludingVBlank;
	dml_float_t *StutterEfficiency;
	dml_uint_t *NumberOfStutterBurstsPerFrame;
	dml_float_t *Z8StutterEfficiencyNotIncludingVBlank;
	dml_float_t *Z8StutterEfficiency;
	dml_uint_t *Z8NumberOfStutterBurstsPerFrame;
	dml_float_t *StutterPeriod;
	dml_bool_t *DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE;
};

struct CalculatePrefetchSchedule_params_st {
	dml_bool_t EnhancedPrefetchScheduleAccelerationFinal;
	dml_float_t HostVMInefficiencyFactor;
	struct DmlPipe *myPipe;
	dml_uint_t DSCDelay;
	dml_float_t DPPCLKDelaySubtotalPlusCNVCFormater;
	dml_float_t DPPCLKDelaySCL;
	dml_float_t DPPCLKDelaySCLLBOnly;
	dml_float_t DPPCLKDelayCNVCCursor;
	dml_float_t DISPCLKDelaySubtotal;
	dml_uint_t DPP_RECOUT_WIDTH;
	enum dml_output_format_class OutputFormat;
	dml_uint_t MaxInterDCNTileRepeaters;
	dml_uint_t VStartup;
	dml_uint_t MaxVStartup;
	dml_uint_t GPUVMPageTableLevels;
	dml_bool_t GPUVMEnable;
	dml_bool_t HostVMEnable;
	dml_uint_t HostVMMaxNonCachedPageTableLevels;
	dml_uint_t HostVMMinPageSize;
	dml_bool_t DynamicMetadataEnable;
	dml_bool_t DynamicMetadataVMEnabled;
	int DynamicMetadataLinesBeforeActiveRequired;
	dml_uint_t DynamicMetadataTransmittedBytes;
	dml_float_t UrgentLatency;
	dml_float_t UrgentExtraLatency;
	dml_float_t TCalc;
	dml_uint_t PDEAndMetaPTEBytesFrame;
	dml_uint_t MetaRowByte;
	dml_uint_t PixelPTEBytesPerRow;
	dml_float_t PrefetchSourceLinesY;
	dml_uint_t VInitPreFillY;
	dml_uint_t MaxNumSwathY;
	dml_float_t PrefetchSourceLinesC;
	dml_uint_t VInitPreFillC;
	dml_uint_t MaxNumSwathC;
	dml_uint_t swath_width_luma_ub;
	dml_uint_t swath_width_chroma_ub;
	dml_uint_t SwathHeightY;
	dml_uint_t SwathHeightC;
	dml_float_t TWait;
	dml_uint_t *DSTXAfterScaler;
	dml_uint_t *DSTYAfterScaler;
	dml_float_t *DestinationLinesForPrefetch;
	dml_float_t *DestinationLinesToRequestVMInVBlank;
	dml_float_t *DestinationLinesToRequestRowInVBlank;
	dml_float_t *VRatioPrefetchY;
	dml_float_t *VRatioPrefetchC;
	dml_float_t *RequiredPrefetchPixDataBWLuma;
	dml_float_t *RequiredPrefetchPixDataBWChroma;
	dml_bool_t *NotEnoughTimeForDynamicMetadata;
	dml_float_t *Tno_bw;
	dml_float_t *prefetch_vmrow_bw;
	dml_float_t *Tdmdl_vm;
	dml_float_t *Tdmdl;
	dml_float_t *TSetup;
	dml_uint_t *VUpdateOffsetPix;
	dml_uint_t *VUpdateWidthPix;
	dml_uint_t *VReadyOffsetPix;
};

struct dml_core_mode_support_locals_st {
	dml_bool_t dummy_boolean[2];
	dml_uint_t dummy_integer[3];
	dml_uint_t dummy_integer_array[22][__DML_NUM_PLANES__];
	enum dml_odm_mode dummy_odm_mode[__DML_NUM_PLANES__];
	dml_bool_t dummy_boolean_array[2][__DML_NUM_PLANES__];
	dml_uint_t MaxVStartupAllPlanes[2];
	dml_uint_t MaximumVStartup[2][__DML_NUM_PLANES__];
	dml_uint_t DSTYAfterScaler[__DML_NUM_PLANES__];
	dml_uint_t DSTXAfterScaler[__DML_NUM_PLANES__];
	dml_uint_t NextPrefetchMode[__DML_NUM_PLANES__];
	dml_uint_t MinPrefetchMode[__DML_NUM_PLANES__];
	dml_uint_t MaxPrefetchMode[__DML_NUM_PLANES__];
	dml_float_t dummy_single[3];
	dml_float_t dummy_single_array[__DML_NUM_PLANES__];
	struct Watermarks dummy_watermark;
	struct SOCParametersList mSOCParameters;
	struct DmlPipe myPipe;
	struct DmlPipe SurfParameters[__DML_NUM_PLANES__];
	dml_uint_t TotalNumberOfActiveWriteback;
	dml_uint_t MaximumSwathWidthSupportLuma;
	dml_uint_t MaximumSwathWidthSupportChroma;
	dml_bool_t MPCCombineMethodAsNeededForPStateChangeAndVoltage;
	dml_bool_t MPCCombineMethodAsPossible;
	dml_bool_t TotalAvailablePipesSupportNoDSC;
	dml_uint_t NumberOfDPPNoDSC;
	enum dml_odm_mode ODMModeNoDSC;
	dml_float_t RequiredDISPCLKPerSurfaceNoDSC;
	dml_bool_t TotalAvailablePipesSupportDSC;
	dml_uint_t NumberOfDPPDSC;
	enum dml_odm_mode ODMModeDSC;
	dml_float_t RequiredDISPCLKPerSurfaceDSC;
	dml_bool_t NoChromaOrLinear;
	dml_float_t BWOfNonCombinedSurfaceOfMaximumBandwidth;
	dml_uint_t NumberOfNonCombinedSurfaceOfMaximumBandwidth;
	dml_uint_t TotalNumberOfActiveOTG;
	dml_uint_t TotalNumberOfActiveHDMIFRL;
	dml_uint_t TotalNumberOfActiveDP2p0;
	dml_uint_t TotalNumberOfActiveDP2p0Outputs;
	dml_uint_t TotalSlots;
	dml_uint_t DSCFormatFactor;
	dml_uint_t TotalDSCUnitsRequired;
	dml_uint_t ReorderingBytes;
	dml_bool_t ImmediateFlipRequiredFinal;
	dml_bool_t FullFrameMALLPStateMethod;
	dml_bool_t SubViewportMALLPStateMethod;
	dml_bool_t PhantomPipeMALLPStateMethod;
	dml_bool_t SubViewportMALLRefreshGreaterThan120Hz;
	dml_float_t MaxTotalVActiveRDBandwidth;
	dml_float_t VMDataOnlyReturnBWPerState;
	dml_float_t HostVMInefficiencyFactor;
	dml_uint_t NextMaxVStartup;
	dml_uint_t MaxVStartup;
	dml_bool_t AllPrefetchModeTested;
	dml_bool_t AnyLinesForVMOrRowTooLarge;
	dml_bool_t is_max_pwr_state;
	dml_bool_t is_max_dram_pwr_state;
	dml_bool_t dram_clock_change_support;
	dml_bool_t f_clock_change_support;
};

struct dml_core_mode_programming_locals_st {
	dml_uint_t DSCFormatFactor;
	dml_uint_t dummy_integer_array[2][__DML_NUM_PLANES__];
	enum dml_output_encoder_class dummy_output_encoder_array[__DML_NUM_PLANES__];
	dml_float_t dummy_single_array[2][__DML_NUM_PLANES__];
	dml_uint_t dummy_long_array[4][__DML_NUM_PLANES__];
	dml_bool_t dummy_boolean_array[2][__DML_NUM_PLANES__];
	dml_bool_t dummy_boolean[1];
	struct DmlPipe SurfaceParameters[__DML_NUM_PLANES__];
	dml_uint_t ReorderBytes;
	dml_float_t VMDataOnlyReturnBW;
	dml_float_t HostVMInefficiencyFactor;
	dml_uint_t TotalDCCActiveDPP;
	dml_uint_t TotalActiveDPP;
	dml_uint_t VStartupLines;
	dml_uint_t MaxVStartupLines[__DML_NUM_PLANES__]; /// <brief more like vblank for the plane's OTG
	dml_uint_t MaxVStartupAllPlanes;
	dml_bool_t ImmediateFlipRequirementFinal;
	int iteration;
	dml_float_t MaxTotalRDBandwidth;
	dml_float_t MaxTotalRDBandwidthNoUrgentBurst;
	dml_bool_t DestinationLineTimesForPrefetchLessThan2;
	dml_bool_t VRatioPrefetchMoreThanMax;
	dml_float_t MaxTotalRDBandwidthNotIncludingMALLPrefetch;
	dml_uint_t NextPrefetchMode[__DML_NUM_PLANES__];
	dml_uint_t MinPrefetchMode[__DML_NUM_PLANES__];
	dml_uint_t MaxPrefetchMode[__DML_NUM_PLANES__];
	dml_bool_t AllPrefetchModeTested;
	dml_float_t dummy_unit_vector[__DML_NUM_PLANES__];
	dml_float_t NonUrgentMaxTotalRDBandwidth;
	dml_float_t NonUrgentMaxTotalRDBandwidthNotIncludingMALLPrefetch;
	dml_float_t dummy_single[2];
	struct SOCParametersList mmSOCParameters;
	dml_float_t Tvstartup_margin;
	dml_float_t dlg_vblank_start;
	dml_float_t LSetup;
	dml_float_t blank_lines_remaining;
	dml_float_t old_MIN_DST_Y_NEXT_START;
	dml_float_t TotalWRBandwidth;
	dml_float_t WRBandwidth;
	struct Watermarks dummy_watermark;
	struct DmlPipe myPipe;
};

struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals_st {
	dml_float_t ActiveDRAMClockChangeLatencyMargin[__DML_NUM_PLANES__];
	dml_float_t ActiveFCLKChangeLatencyMargin[__DML_NUM_PLANES__];
	dml_float_t USRRetrainingLatencyMargin[__DML_NUM_PLANES__];

	dml_bool_t SynchronizedSurfaces[__DML_NUM_PLANES__][__DML_NUM_PLANES__];
	dml_float_t EffectiveLBLatencyHidingY;
	dml_float_t EffectiveLBLatencyHidingC;
	dml_float_t LinesInDETY[__DML_NUM_PLANES__];
	dml_float_t LinesInDETC[__DML_NUM_PLANES__];
	dml_uint_t LinesInDETYRoundedDownToSwath[__DML_NUM_PLANES__];
	dml_uint_t LinesInDETCRoundedDownToSwath[__DML_NUM_PLANES__];
	dml_float_t FullDETBufferingTimeY;
	dml_float_t FullDETBufferingTimeC;
	dml_float_t WritebackDRAMClockChangeLatencyMargin;
	dml_float_t WritebackFCLKChangeLatencyMargin;
	dml_float_t WritebackLatencyHiding;

	dml_uint_t TotalActiveWriteback;
	dml_uint_t LBLatencyHidingSourceLinesY[__DML_NUM_PLANES__];
	dml_uint_t LBLatencyHidingSourceLinesC[__DML_NUM_PLANES__];
	dml_float_t TotalPixelBW;
	dml_float_t EffectiveDETBufferSizeY;
	dml_float_t ActiveClockChangeLatencyHidingY;
	dml_float_t ActiveClockChangeLatencyHidingC;
	dml_float_t ActiveClockChangeLatencyHiding;
	dml_bool_t FoundCriticalSurface;
	dml_uint_t LastSurfaceWithoutMargin;
	dml_uint_t FCLKChangeSupportNumber;
	dml_uint_t DRAMClockChangeMethod;
	dml_uint_t DRAMClockChangeSupportNumber;
	dml_uint_t dst_y_pstate;
	dml_uint_t src_y_pstate_l;
	dml_uint_t src_y_pstate_c;
	dml_uint_t src_y_ahead_l;
	dml_uint_t src_y_ahead_c;
	dml_uint_t sub_vp_lines_l;
	dml_uint_t sub_vp_lines_c;
};

struct CalculateVMRowAndSwath_locals_st {
	dml_uint_t PTEBufferSizeInRequestsForLuma[__DML_NUM_PLANES__];
	dml_uint_t PTEBufferSizeInRequestsForChroma[__DML_NUM_PLANES__];
	dml_uint_t PDEAndMetaPTEBytesFrameY;
	dml_uint_t PDEAndMetaPTEBytesFrameC;
	dml_uint_t MetaRowByteY[__DML_NUM_PLANES__];
	dml_uint_t MetaRowByteC[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEBytesPerRowY[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEBytesPerRowC[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEBytesPerRowStorageY[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEBytesPerRowStorageC[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEBytesPerRowY_one_row_per_frame[__DML_NUM_PLANES__];
	dml_uint_t PixelPTEBytesPerRowC_one_row_per_frame[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_width_luma_ub_one_row_per_frame[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_height_luma_one_row_per_frame[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_width_chroma_ub_one_row_per_frame[__DML_NUM_PLANES__];
	dml_uint_t dpte_row_height_chroma_one_row_per_frame[__DML_NUM_PLANES__];
	dml_bool_t one_row_per_frame_fits_in_buffer[__DML_NUM_PLANES__];

	dml_uint_t HostVMDynamicLevels;
};

struct UseMinimumDCFCLK_locals_st {
	dml_uint_t dummy1;
	dml_uint_t dummy2;
	dml_uint_t dummy3;
	dml_float_t NormalEfficiency;
	dml_float_t TotalMaxPrefetchFlipDPTERowBandwidth[2];

	dml_float_t PixelDCFCLKCyclesRequiredInPrefetch[__DML_NUM_PLANES__];
	dml_float_t PrefetchPixelLinesTime[__DML_NUM_PLANES__];
	dml_float_t DCFCLKRequiredForPeakBandwidthPerSurface[__DML_NUM_PLANES__];
	dml_float_t DynamicMetadataVMExtraLatency[__DML_NUM_PLANES__];
	dml_float_t MinimumTWait;
	dml_float_t DPTEBandwidth;
	dml_float_t DCFCLKRequiredForAverageBandwidth;
	dml_uint_t ExtraLatencyBytes;
	dml_float_t ExtraLatencyCycles;
	dml_float_t DCFCLKRequiredForPeakBandwidth;
	dml_uint_t NoOfDPPState[__DML_NUM_PLANES__];
	dml_float_t MinimumTvmPlus2Tr0;
};

struct CalculatePrefetchSchedule_locals_st {
	dml_bool_t MyError;
	dml_uint_t DPPCycles;
	dml_uint_t DISPCLKCycles;
	dml_float_t DSTTotalPixelsAfterScaler;
	dml_float_t LineTime;
	dml_float_t dst_y_prefetch_equ;
	dml_float_t prefetch_bw_oto;
	dml_float_t Tvm_oto;
	dml_float_t Tr0_oto;
	dml_float_t Tvm_oto_lines;
	dml_float_t Tr0_oto_lines;
	dml_float_t dst_y_prefetch_oto;
	dml_float_t TimeForFetchingMetaPTE;
	dml_float_t TimeForFetchingRowInVBlank;
	dml_float_t LinesToRequestPrefetchPixelData;
	dml_uint_t HostVMDynamicLevelsTrips;
	dml_float_t trip_to_mem;
	dml_float_t Tvm_trips;
	dml_float_t Tr0_trips;
	dml_float_t Tvm_trips_rounded;
	dml_float_t Tr0_trips_rounded;
	dml_float_t max_Tsw;
	dml_float_t Lsw_oto;
	dml_float_t Tpre_rounded;
	dml_float_t prefetch_bw_equ;
	dml_float_t Tvm_equ;
	dml_float_t Tr0_equ;
	dml_float_t Tdmbf;
	dml_float_t Tdmec;
	dml_float_t Tdmsks;
	dml_float_t prefetch_sw_bytes;
	dml_float_t prefetch_bw_pr;
	dml_float_t bytes_pp;
	dml_float_t dep_bytes;
	dml_float_t min_Lsw_oto;
	dml_float_t Tsw_est1;
	dml_float_t Tsw_est3;
	dml_float_t PrefetchBandwidth1;
	dml_float_t PrefetchBandwidth2;
	dml_float_t PrefetchBandwidth3;
	dml_float_t PrefetchBandwidth4;
};

/// @brief To minimize stack usage; function locals are instead placed into this scratch structure which is allocated per context
struct display_mode_lib_scratch_st {
	// Scratch space for function locals
	struct dml_core_mode_support_locals_st dml_core_mode_support_locals;
	struct dml_core_mode_programming_locals_st dml_core_mode_programming_locals;
	struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals_st CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals;
	struct CalculateVMRowAndSwath_locals_st CalculateVMRowAndSwath_locals;
	struct UseMinimumDCFCLK_locals_st UseMinimumDCFCLK_locals;
	struct CalculatePrefetchSchedule_locals_st CalculatePrefetchSchedule_locals;

	// Scratch space for function params
	struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params_st CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;
	struct CalculateVMRowAndSwath_params_st CalculateVMRowAndSwath_params;
	struct UseMinimumDCFCLK_params_st UseMinimumDCFCLK_params;
	struct CalculateSwathAndDETConfiguration_params_st CalculateSwathAndDETConfiguration_params;
	struct CalculateStutterEfficiency_params_st CalculateStutterEfficiency_params;
	struct CalculatePrefetchSchedule_params_st CalculatePrefetchSchedule_params;
};

/// @brief Represent the overall soc/ip enviroment. It contains data structure represent the soc/ip characteristic and also structures that hold calculation output
struct display_mode_lib_st {
	dml_uint_t project;

	//@brief Mode evaluation and programming policy
	struct dml_mode_eval_policy_st policy;

	//@brief IP/SOC characteristic
	struct ip_params_st ip;
	struct soc_bounding_box_st soc;
	struct soc_states_st states;

	//@brief Mode Support and Mode programming struct
	// Used to hold input; intermediate and output of the calculations
	struct mode_support_st ms; // struct for mode support
	struct mode_program_st mp; // struct for mode programming

	struct display_mode_lib_scratch_st scratch;
};

struct dml_mode_support_ex_params_st {
	struct display_mode_lib_st *mode_lib;
	const struct dml_display_cfg_st *in_display_cfg;
	dml_uint_t out_lowest_state_idx;
	struct dml_mode_support_info_st *out_evaluation_info;
};

typedef struct _vcs_dpi_dml_display_rq_regs_st  dml_display_rq_regs_st;
typedef struct _vcs_dpi_dml_display_dlg_regs_st dml_display_dlg_regs_st;
typedef struct _vcs_dpi_dml_display_ttu_regs_st dml_display_ttu_regs_st;
typedef struct _vcs_dpi_dml_display_arb_params_st   dml_display_arb_params_st;
typedef struct _vcs_dpi_dml_display_plane_rq_regs_st    dml_display_plane_rq_regs_st;

struct  _vcs_dpi_dml_display_dlg_regs_st {
	dml_uint_t  refcyc_h_blank_end;
	dml_uint_t  dlg_vblank_end;
	dml_uint_t  min_dst_y_next_start;
	dml_uint_t  refcyc_per_htotal;
	dml_uint_t  refcyc_x_after_scaler;
	dml_uint_t  dst_y_after_scaler;
	dml_uint_t  dst_y_prefetch;
	dml_uint_t  dst_y_per_vm_vblank;
	dml_uint_t  dst_y_per_row_vblank;
	dml_uint_t  dst_y_per_vm_flip;
	dml_uint_t  dst_y_per_row_flip;
	dml_uint_t  ref_freq_to_pix_freq;
	dml_uint_t  vratio_prefetch;
	dml_uint_t  vratio_prefetch_c;
	dml_uint_t  refcyc_per_pte_group_vblank_l;
	dml_uint_t  refcyc_per_pte_group_vblank_c;
	dml_uint_t  refcyc_per_meta_chunk_vblank_l;
	dml_uint_t  refcyc_per_meta_chunk_vblank_c;
	dml_uint_t  refcyc_per_pte_group_flip_l;
	dml_uint_t  refcyc_per_pte_group_flip_c;
	dml_uint_t  refcyc_per_meta_chunk_flip_l;
	dml_uint_t  refcyc_per_meta_chunk_flip_c;
	dml_uint_t  dst_y_per_pte_row_nom_l;
	dml_uint_t  dst_y_per_pte_row_nom_c;
	dml_uint_t  refcyc_per_pte_group_nom_l;
	dml_uint_t  refcyc_per_pte_group_nom_c;
	dml_uint_t  dst_y_per_meta_row_nom_l;
	dml_uint_t  dst_y_per_meta_row_nom_c;
	dml_uint_t  refcyc_per_meta_chunk_nom_l;
	dml_uint_t  refcyc_per_meta_chunk_nom_c;
	dml_uint_t  refcyc_per_line_delivery_pre_l;
	dml_uint_t  refcyc_per_line_delivery_pre_c;
	dml_uint_t  refcyc_per_line_delivery_l;
	dml_uint_t  refcyc_per_line_delivery_c;
	dml_uint_t  refcyc_per_vm_group_vblank;
	dml_uint_t  refcyc_per_vm_group_flip;
	dml_uint_t  refcyc_per_vm_req_vblank;
	dml_uint_t  refcyc_per_vm_req_flip;
	dml_uint_t  dst_y_offset_cur0;
	dml_uint_t  chunk_hdl_adjust_cur0;
	dml_uint_t  dst_y_offset_cur1;
	dml_uint_t  chunk_hdl_adjust_cur1;
	dml_uint_t  vready_after_vcount0;
	dml_uint_t  dst_y_delta_drq_limit;
	dml_uint_t  refcyc_per_vm_dmdata;
	dml_uint_t  dmdata_dl_delta;
};

struct  _vcs_dpi_dml_display_ttu_regs_st {
	dml_uint_t  qos_level_low_wm;
	dml_uint_t  qos_level_high_wm;
	dml_uint_t  min_ttu_vblank;
	dml_uint_t  qos_level_flip;
	dml_uint_t  refcyc_per_req_delivery_l;
	dml_uint_t  refcyc_per_req_delivery_c;
	dml_uint_t  refcyc_per_req_delivery_cur0;
	dml_uint_t  refcyc_per_req_delivery_cur1;
	dml_uint_t  refcyc_per_req_delivery_pre_l;
	dml_uint_t  refcyc_per_req_delivery_pre_c;
	dml_uint_t  refcyc_per_req_delivery_pre_cur0;
	dml_uint_t  refcyc_per_req_delivery_pre_cur1;
	dml_uint_t  qos_level_fixed_l;
	dml_uint_t  qos_level_fixed_c;
	dml_uint_t  qos_level_fixed_cur0;
	dml_uint_t  qos_level_fixed_cur1;
	dml_uint_t  qos_ramp_disable_l;
	dml_uint_t  qos_ramp_disable_c;
	dml_uint_t  qos_ramp_disable_cur0;
	dml_uint_t  qos_ramp_disable_cur1;
};

struct  _vcs_dpi_dml_display_arb_params_st {
	dml_uint_t  max_req_outstanding;
	dml_uint_t  min_req_outstanding;
	dml_uint_t  sat_level_us;
	dml_uint_t  hvm_max_qos_commit_threshold;
	dml_uint_t  hvm_min_req_outstand_commit_threshold;
	dml_uint_t  compbuf_reserved_space_kbytes;
};

struct  _vcs_dpi_dml_display_plane_rq_regs_st {
	dml_uint_t  chunk_size;
	dml_uint_t  min_chunk_size;
	dml_uint_t  meta_chunk_size;
	dml_uint_t  min_meta_chunk_size;
	dml_uint_t  dpte_group_size;
	dml_uint_t  mpte_group_size;
	dml_uint_t  swath_height;
	dml_uint_t  pte_row_height_linear;
};

struct  _vcs_dpi_dml_display_rq_regs_st {
	dml_display_plane_rq_regs_st    rq_regs_l;
	dml_display_plane_rq_regs_st    rq_regs_c;
	dml_uint_t  drq_expansion_mode;
	dml_uint_t  prq_expansion_mode;
	dml_uint_t  mrq_expansion_mode;
	dml_uint_t  crq_expansion_mode;
	dml_uint_t  plane1_base_address;
};

#endif
