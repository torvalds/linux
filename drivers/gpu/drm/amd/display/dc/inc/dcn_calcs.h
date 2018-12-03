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

/**
 * Bandwidth and Watermark calculations interface.
 * (Refer to "DCEx_mode_support.xlsm" from Perforce.)
 */
#ifndef __DCN_CALCS_H__
#define __DCN_CALCS_H__

#include "bw_fixed.h"
#include "display_clock.h"
#include "../dml/display_mode_lib.h"

struct dc;
struct dc_state;

/*******************************************************************************
 * DCN data structures.
 ******************************************************************************/

#define number_of_planes   6
#define number_of_planes_minus_one   5
#define number_of_states   4
#define number_of_states_plus_one   5

#define ddr4_dram_width   64
#define ddr4_dram_factor_single_Channel   16
enum dcn_bw_defs {
	dcn_bw_v_min0p65,
	dcn_bw_v_mid0p72,
	dcn_bw_v_nom0p8,
	dcn_bw_v_max0p9,
	dcn_bw_v_max0p91,
	dcn_bw_no_support = 5,
	dcn_bw_yes,
	dcn_bw_hor,
	dcn_bw_vert,
	dcn_bw_override,
	dcn_bw_rgb_sub_64,
	dcn_bw_rgb_sub_32,
	dcn_bw_rgb_sub_16,
	dcn_bw_no,
	dcn_bw_sw_linear,
	dcn_bw_sw_4_kb_d,
	dcn_bw_sw_4_kb_d_x,
	dcn_bw_sw_64_kb_d,
	dcn_bw_sw_64_kb_d_t,
	dcn_bw_sw_64_kb_d_x,
	dcn_bw_sw_var_d,
	dcn_bw_sw_var_d_x,
	dcn_bw_yuv420_sub_8,
	dcn_bw_sw_4_kb_s,
	dcn_bw_sw_4_kb_s_x,
	dcn_bw_sw_64_kb_s,
	dcn_bw_sw_64_kb_s_t,
	dcn_bw_sw_64_kb_s_x,
	dcn_bw_writeback,
	dcn_bw_444,
	dcn_bw_dp,
	dcn_bw_420,
	dcn_bw_hdmi,
	dcn_bw_sw_var_s,
	dcn_bw_sw_var_s_x,
	dcn_bw_yuv420_sub_10,
	dcn_bw_supported_in_v_active,
	dcn_bw_supported_in_v_blank,
	dcn_bw_not_supported,
	dcn_bw_na,
	dcn_bw_encoder_8bpc,
	dcn_bw_encoder_10bpc,
	dcn_bw_encoder_12bpc,
	dcn_bw_encoder_16bpc,
};

/*bounding box parameters*/
/*mode parameters*/
/*system configuration*/
/* display configuration*/
struct dcn_bw_internal_vars {
	float voltage[number_of_states_plus_one + 1];
	float max_dispclk[number_of_states_plus_one + 1];
	float max_dppclk[number_of_states_plus_one + 1];
	float dcfclk_per_state[number_of_states_plus_one + 1];
	float phyclk_per_state[number_of_states_plus_one + 1];
	float fabric_and_dram_bandwidth_per_state[number_of_states_plus_one + 1];
	float sr_exit_time;
	float sr_enter_plus_exit_time;
	float dram_clock_change_latency;
	float urgent_latency;
	float write_back_latency;
	float percent_of_ideal_drambw_received_after_urg_latency;
	float dcfclkv_max0p9;
	float dcfclkv_nom0p8;
	float dcfclkv_mid0p72;
	float dcfclkv_min0p65;
	float max_dispclk_vmax0p9;
	float max_dppclk_vmax0p9;
	float max_dispclk_vnom0p8;
	float max_dppclk_vnom0p8;
	float max_dispclk_vmid0p72;
	float max_dppclk_vmid0p72;
	float max_dispclk_vmin0p65;
	float max_dppclk_vmin0p65;
	float socclk;
	float fabric_and_dram_bandwidth_vmax0p9;
	float fabric_and_dram_bandwidth_vnom0p8;
	float fabric_and_dram_bandwidth_vmid0p72;
	float fabric_and_dram_bandwidth_vmin0p65;
	float round_trip_ping_latency_cycles;
	float urgent_out_of_order_return_per_channel;
	float number_of_channels;
	float vmm_page_size;
	float return_bus_width;
	float rob_buffer_size_in_kbyte;
	float det_buffer_size_in_kbyte;
	float dpp_output_buffer_pixels;
	float opp_output_buffer_lines;
	float pixel_chunk_size_in_kbyte;
	float pte_chunk_size;
	float meta_chunk_size;
	float writeback_chunk_size;
	enum dcn_bw_defs odm_capability;
	enum dcn_bw_defs dsc_capability;
	float line_buffer_size;
	enum dcn_bw_defs is_line_buffer_bpp_fixed;
	float line_buffer_fixed_bpp;
	float max_line_buffer_lines;
	float writeback_luma_buffer_size;
	float writeback_chroma_buffer_size;
	float max_num_dpp;
	float max_num_writeback;
	float max_dchub_topscl_throughput;
	float max_pscl_tolb_throughput;
	float max_lb_tovscl_throughput;
	float max_vscl_tohscl_throughput;
	float max_hscl_ratio;
	float max_vscl_ratio;
	float max_hscl_taps;
	float max_vscl_taps;
	float under_scan_factor;
	float phyclkv_max0p9;
	float phyclkv_nom0p8;
	float phyclkv_mid0p72;
	float phyclkv_min0p65;
	float pte_buffer_size_in_requests;
	float dispclk_ramping_margin;
	float downspreading;
	float max_inter_dcn_tile_repeaters;
	enum dcn_bw_defs can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one;
	enum dcn_bw_defs bug_forcing_luma_and_chroma_request_to_same_size_fixed;
	int mode;
	float viewport_width[number_of_planes_minus_one + 1];
	float htotal[number_of_planes_minus_one + 1];
	float vtotal[number_of_planes_minus_one + 1];
	float v_sync_plus_back_porch[number_of_planes_minus_one + 1];
	float vactive[number_of_planes_minus_one + 1];
	float pixel_clock[number_of_planes_minus_one + 1]; /*MHz*/
	float viewport_height[number_of_planes_minus_one + 1];
	enum dcn_bw_defs dcc_enable[number_of_planes_minus_one + 1];
	float dcc_rate[number_of_planes_minus_one + 1];
	enum dcn_bw_defs source_scan[number_of_planes_minus_one + 1];
	float lb_bit_per_pixel[number_of_planes_minus_one + 1];
	enum dcn_bw_defs source_pixel_format[number_of_planes_minus_one + 1];
	enum dcn_bw_defs source_surface_mode[number_of_planes_minus_one + 1];
	enum dcn_bw_defs output_format[number_of_planes_minus_one + 1];
	enum dcn_bw_defs output_deep_color[number_of_planes_minus_one + 1];
	enum dcn_bw_defs output[number_of_planes_minus_one + 1];
	float scaler_rec_out_width[number_of_planes_minus_one + 1];
	float scaler_recout_height[number_of_planes_minus_one + 1];
	float underscan_output[number_of_planes_minus_one + 1];
	float interlace_output[number_of_planes_minus_one + 1];
	float override_hta_ps[number_of_planes_minus_one + 1];
	float override_vta_ps[number_of_planes_minus_one + 1];
	float override_hta_pschroma[number_of_planes_minus_one + 1];
	float override_vta_pschroma[number_of_planes_minus_one + 1];
	float urgent_latency_support_us[number_of_planes_minus_one + 1];
	float h_ratio[number_of_planes_minus_one + 1];
	float v_ratio[number_of_planes_minus_one + 1];
	float htaps[number_of_planes_minus_one + 1];
	float vtaps[number_of_planes_minus_one + 1];
	float hta_pschroma[number_of_planes_minus_one + 1];
	float vta_pschroma[number_of_planes_minus_one + 1];
	enum dcn_bw_defs pte_enable;
	enum dcn_bw_defs synchronized_vblank;
	enum dcn_bw_defs ta_pscalculation;
	int voltage_override_level;
	int number_of_active_planes;
	int voltage_level;
	enum dcn_bw_defs immediate_flip_supported;
	float dcfclk;
	float max_phyclk;
	float fabric_and_dram_bandwidth;
	float dpp_per_plane_per_ratio[1 + 1][number_of_planes_minus_one + 1];
	enum dcn_bw_defs dispclk_dppclk_support_per_ratio[1 + 1];
	float required_dispclk_per_ratio[1 + 1];
	enum dcn_bw_defs error_message[1 + 1];
	int dispclk_dppclk_ratio;
	float dpp_per_plane[number_of_planes_minus_one + 1];
	float det_buffer_size_y[number_of_planes_minus_one + 1];
	float det_buffer_size_c[number_of_planes_minus_one + 1];
	float swath_height_y[number_of_planes_minus_one + 1];
	float swath_height_c[number_of_planes_minus_one + 1];
	enum dcn_bw_defs final_error_message;
	float frequency;
	float header_line;
	float header;
	enum dcn_bw_defs voltage_override;
	enum dcn_bw_defs allow_different_hratio_vratio;
	float acceptable_quality_hta_ps;
	float acceptable_quality_vta_ps;
	float no_of_dpp[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float swath_width_yper_state[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float swath_height_yper_state[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float swath_height_cper_state[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float urgent_latency_support_us_per_state[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float v_ratio_pre_ywith_immediate_flip[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float v_ratio_pre_cwith_immediate_flip[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float required_prefetch_pixel_data_bw_with_immediate_flip[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float v_ratio_pre_ywithout_immediate_flip[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float v_ratio_pre_cwithout_immediate_flip[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	float required_prefetch_pixel_data_bw_without_immediate_flip[number_of_states_plus_one + 1][1 + 1][number_of_planes_minus_one + 1];
	enum dcn_bw_defs prefetch_supported_with_immediate_flip[number_of_states_plus_one + 1][1 + 1];
	enum dcn_bw_defs prefetch_supported_without_immediate_flip[number_of_states_plus_one + 1][1 + 1];
	enum dcn_bw_defs v_ratio_in_prefetch_supported_with_immediate_flip[number_of_states_plus_one + 1][1 + 1];
	enum dcn_bw_defs v_ratio_in_prefetch_supported_without_immediate_flip[number_of_states_plus_one + 1][1 + 1];
	float required_dispclk[number_of_states_plus_one + 1][1 + 1];
	enum dcn_bw_defs dispclk_dppclk_support[number_of_states_plus_one + 1][1 + 1];
	enum dcn_bw_defs total_available_pipes_support[number_of_states_plus_one + 1][1 + 1];
	float total_number_of_active_dpp[number_of_states_plus_one + 1][1 + 1];
	float total_number_of_dcc_active_dpp[number_of_states_plus_one + 1][1 + 1];
	enum dcn_bw_defs urgent_latency_support[number_of_states_plus_one + 1][1 + 1];
	enum dcn_bw_defs mode_support_with_immediate_flip[number_of_states_plus_one + 1][1 + 1];
	enum dcn_bw_defs mode_support_without_immediate_flip[number_of_states_plus_one + 1][1 + 1];
	float return_bw_per_state[number_of_states_plus_one + 1];
	enum dcn_bw_defs dio_support[number_of_states_plus_one + 1];
	float urgent_round_trip_and_out_of_order_latency_per_state[number_of_states_plus_one + 1];
	enum dcn_bw_defs rob_support[number_of_states_plus_one + 1];
	enum dcn_bw_defs bandwidth_support[number_of_states_plus_one + 1];
	float prefetch_bw[number_of_planes_minus_one + 1];
	float meta_pte_bytes_per_frame[number_of_planes_minus_one + 1];
	float meta_row_bytes[number_of_planes_minus_one + 1];
	float dpte_bytes_per_row[number_of_planes_minus_one + 1];
	float prefetch_lines_y[number_of_planes_minus_one + 1];
	float prefetch_lines_c[number_of_planes_minus_one + 1];
	float max_num_sw_y[number_of_planes_minus_one + 1];
	float max_num_sw_c[number_of_planes_minus_one + 1];
	float line_times_for_prefetch[number_of_planes_minus_one + 1];
	float lines_for_meta_pte_with_immediate_flip[number_of_planes_minus_one + 1];
	float lines_for_meta_pte_without_immediate_flip[number_of_planes_minus_one + 1];
	float lines_for_meta_and_dpte_row_with_immediate_flip[number_of_planes_minus_one + 1];
	float lines_for_meta_and_dpte_row_without_immediate_flip[number_of_planes_minus_one + 1];
	float min_dppclk_using_single_dpp[number_of_planes_minus_one + 1];
	float swath_width_ysingle_dpp[number_of_planes_minus_one + 1];
	float byte_per_pixel_in_dety[number_of_planes_minus_one + 1];
	float byte_per_pixel_in_detc[number_of_planes_minus_one + 1];
	float number_of_dpp_required_for_det_and_lb_size[number_of_planes_minus_one + 1];
	float required_phyclk[number_of_planes_minus_one + 1];
	float read256_block_height_y[number_of_planes_minus_one + 1];
	float read256_block_width_y[number_of_planes_minus_one + 1];
	float read256_block_height_c[number_of_planes_minus_one + 1];
	float read256_block_width_c[number_of_planes_minus_one + 1];
	float max_swath_height_y[number_of_planes_minus_one + 1];
	float max_swath_height_c[number_of_planes_minus_one + 1];
	float min_swath_height_y[number_of_planes_minus_one + 1];
	float min_swath_height_c[number_of_planes_minus_one + 1];
	float read_bandwidth[number_of_planes_minus_one + 1];
	float write_bandwidth[number_of_planes_minus_one + 1];
	float pscl_factor[number_of_planes_minus_one + 1];
	float pscl_factor_chroma[number_of_planes_minus_one + 1];
	enum dcn_bw_defs scale_ratio_support;
	enum dcn_bw_defs source_format_pixel_and_scan_support;
	float total_read_bandwidth_consumed_gbyte_per_second;
	float total_write_bandwidth_consumed_gbyte_per_second;
	float total_bandwidth_consumed_gbyte_per_second;
	enum dcn_bw_defs dcc_enabled_in_any_plane;
	float return_bw_todcn_per_state;
	float critical_point;
	enum dcn_bw_defs writeback_latency_support;
	float required_output_bw;
	float total_number_of_active_writeback;
	enum dcn_bw_defs total_available_writeback_support;
	float maximum_swath_width;
	float number_of_dpp_required_for_det_size;
	float number_of_dpp_required_for_lb_size;
	float min_dispclk_using_single_dpp;
	float min_dispclk_using_dual_dpp;
	enum dcn_bw_defs viewport_size_support;
	float swath_width_granularity_y;
	float rounded_up_max_swath_size_bytes_y;
	float swath_width_granularity_c;
	float rounded_up_max_swath_size_bytes_c;
	float lines_in_det_luma;
	float lines_in_det_chroma;
	float effective_lb_latency_hiding_source_lines_luma;
	float effective_lb_latency_hiding_source_lines_chroma;
	float effective_detlb_lines_luma;
	float effective_detlb_lines_chroma;
	float projected_dcfclk_deep_sleep;
	float meta_req_height_y;
	float meta_req_width_y;
	float meta_surface_width_y;
	float meta_surface_height_y;
	float meta_pte_bytes_per_frame_y;
	float meta_row_bytes_y;
	float macro_tile_block_size_bytes_y;
	float macro_tile_block_height_y;
	float data_pte_req_height_y;
	float data_pte_req_width_y;
	float dpte_bytes_per_row_y;
	float meta_req_height_c;
	float meta_req_width_c;
	float meta_surface_width_c;
	float meta_surface_height_c;
	float meta_pte_bytes_per_frame_c;
	float meta_row_bytes_c;
	float macro_tile_block_size_bytes_c;
	float macro_tile_block_height_c;
	float macro_tile_block_width_c;
	float data_pte_req_height_c;
	float data_pte_req_width_c;
	float dpte_bytes_per_row_c;
	float v_init_y;
	float max_partial_sw_y;
	float v_init_c;
	float max_partial_sw_c;
	float dst_x_after_scaler;
	float dst_y_after_scaler;
	float time_calc;
	float v_update_offset[number_of_planes_minus_one + 1][2];
	float total_repeater_delay;
	float v_update_width[number_of_planes_minus_one + 1][2];
	float v_ready_offset[number_of_planes_minus_one + 1][2];
	float time_setup;
	float extra_latency;
	float maximum_vstartup;
	float bw_available_for_immediate_flip;
	float total_immediate_flip_bytes[number_of_planes_minus_one + 1];
	float time_for_meta_pte_with_immediate_flip;
	float time_for_meta_pte_without_immediate_flip;
	float time_for_meta_and_dpte_row_with_immediate_flip;
	float time_for_meta_and_dpte_row_without_immediate_flip;
	float line_times_to_request_prefetch_pixel_data_with_immediate_flip;
	float line_times_to_request_prefetch_pixel_data_without_immediate_flip;
	float maximum_read_bandwidth_with_prefetch_with_immediate_flip;
	float maximum_read_bandwidth_with_prefetch_without_immediate_flip;
	float voltage_level_with_immediate_flip;
	float voltage_level_without_immediate_flip;
	float total_number_of_active_dpp_per_ratio[1 + 1];
	float byte_per_pix_dety;
	float byte_per_pix_detc;
	float read256_bytes_block_height_y;
	float read256_bytes_block_width_y;
	float read256_bytes_block_height_c;
	float read256_bytes_block_width_c;
	float maximum_swath_height_y;
	float maximum_swath_height_c;
	float minimum_swath_height_y;
	float minimum_swath_height_c;
	float swath_width;
	float prefetch_bandwidth[number_of_planes_minus_one + 1];
	float v_init_pre_fill_y[number_of_planes_minus_one + 1];
	float v_init_pre_fill_c[number_of_planes_minus_one + 1];
	float max_num_swath_y[number_of_planes_minus_one + 1];
	float max_num_swath_c[number_of_planes_minus_one + 1];
	float prefill_y[number_of_planes_minus_one + 1];
	float prefill_c[number_of_planes_minus_one + 1];
	float v_startup[number_of_planes_minus_one + 1];
	enum dcn_bw_defs allow_dram_clock_change_during_vblank[number_of_planes_minus_one + 1];
	float allow_dram_self_refresh_during_vblank[number_of_planes_minus_one + 1];
	float v_ratio_prefetch_y[number_of_planes_minus_one + 1];
	float v_ratio_prefetch_c[number_of_planes_minus_one + 1];
	float destination_lines_for_prefetch[number_of_planes_minus_one + 1];
	float destination_lines_to_request_vm_inv_blank[number_of_planes_minus_one + 1];
	float destination_lines_to_request_row_in_vblank[number_of_planes_minus_one + 1];
	float min_ttuv_blank[number_of_planes_minus_one + 1];
	float byte_per_pixel_dety[number_of_planes_minus_one + 1];
	float byte_per_pixel_detc[number_of_planes_minus_one + 1];
	float swath_width_y[number_of_planes_minus_one + 1];
	float lines_in_dety[number_of_planes_minus_one + 1];
	float lines_in_dety_rounded_down_to_swath[number_of_planes_minus_one + 1];
	float lines_in_detc[number_of_planes_minus_one + 1];
	float lines_in_detc_rounded_down_to_swath[number_of_planes_minus_one + 1];
	float full_det_buffering_time_y[number_of_planes_minus_one + 1];
	float full_det_buffering_time_c[number_of_planes_minus_one + 1];
	float active_dram_clock_change_latency_margin[number_of_planes_minus_one + 1];
	float v_blank_dram_clock_change_latency_margin[number_of_planes_minus_one + 1];
	float dcfclk_deep_sleep_per_plane[number_of_planes_minus_one + 1];
	float read_bandwidth_plane_luma[number_of_planes_minus_one + 1];
	float read_bandwidth_plane_chroma[number_of_planes_minus_one + 1];
	float display_pipe_line_delivery_time_luma[number_of_planes_minus_one + 1];
	float display_pipe_line_delivery_time_chroma[number_of_planes_minus_one + 1];
	float display_pipe_line_delivery_time_luma_prefetch[number_of_planes_minus_one + 1];
	float display_pipe_line_delivery_time_chroma_prefetch[number_of_planes_minus_one + 1];
	float pixel_pte_bytes_per_row[number_of_planes_minus_one + 1];
	float meta_pte_bytes_frame[number_of_planes_minus_one + 1];
	float meta_row_byte[number_of_planes_minus_one + 1];
	float prefetch_source_lines_y[number_of_planes_minus_one + 1];
	float prefetch_source_lines_c[number_of_planes_minus_one + 1];
	float pscl_throughput[number_of_planes_minus_one + 1];
	float pscl_throughput_chroma[number_of_planes_minus_one + 1];
	float output_bpphdmi[number_of_planes_minus_one + 1];
	float output_bppdp4_lane_hbr[number_of_planes_minus_one + 1];
	float output_bppdp4_lane_hbr2[number_of_planes_minus_one + 1];
	float output_bppdp4_lane_hbr3[number_of_planes_minus_one + 1];
	float max_vstartup_lines[number_of_planes_minus_one + 1];
	float dispclk_with_ramping;
	float dispclk_without_ramping;
	float dppclk_using_single_dpp_luma;
	float dppclk_using_single_dpp;
	float dppclk_using_single_dpp_chroma;
	enum dcn_bw_defs odm_capable;
	float dispclk;
	float dppclk;
	float return_bandwidth_to_dcn;
	enum dcn_bw_defs dcc_enabled_any_plane;
	float return_bw;
	float critical_compression;
	float total_data_read_bandwidth;
	float total_active_dpp;
	float total_dcc_active_dpp;
	float urgent_round_trip_and_out_of_order_latency;
	float last_pixel_of_line_extra_watermark;
	float data_fabric_line_delivery_time_luma;
	float data_fabric_line_delivery_time_chroma;
	float urgent_extra_latency;
	float urgent_watermark;
	float ptemeta_urgent_watermark;
	float dram_clock_change_watermark;
	float total_active_writeback;
	float writeback_dram_clock_change_watermark;
	float min_full_det_buffering_time;
	float frame_time_for_min_full_det_buffering_time;
	float average_read_bandwidth_gbyte_per_second;
	float part_of_burst_that_fits_in_rob;
	float stutter_burst_time;
	float stutter_efficiency_not_including_vblank;
	float smallest_vblank;
	float v_blank_time;
	float stutter_efficiency;
	float dcf_clk_deep_sleep;
	float stutter_exit_watermark;
	float stutter_enter_plus_exit_watermark;
	float effective_det_plus_lb_lines_luma;
	float urgent_latency_support_us_luma;
	float effective_det_plus_lb_lines_chroma;
	float urgent_latency_support_us_chroma;
	float min_urgent_latency_support_us;
	float non_urgent_latency_tolerance;
	float block_height256_bytes_y;
	float block_height256_bytes_c;
	float meta_request_width_y;
	float meta_surf_width_y;
	float meta_surf_height_y;
	float meta_pte_bytes_frame_y;
	float meta_row_byte_y;
	float macro_tile_size_byte_y;
	float macro_tile_height_y;
	float pixel_pte_req_height_y;
	float pixel_pte_req_width_y;
	float pixel_pte_bytes_per_row_y;
	float meta_request_width_c;
	float meta_surf_width_c;
	float meta_surf_height_c;
	float meta_pte_bytes_frame_c;
	float meta_row_byte_c;
	float macro_tile_size_bytes_c;
	float macro_tile_height_c;
	float pixel_pte_req_height_c;
	float pixel_pte_req_width_c;
	float pixel_pte_bytes_per_row_c;
	float max_partial_swath_y;
	float max_partial_swath_c;
	float t_calc;
	float next_prefetch_mode;
	float v_startup_lines;
	enum dcn_bw_defs planes_with_room_to_increase_vstartup_prefetch_bw_less_than_active_bw;
	enum dcn_bw_defs planes_with_room_to_increase_vstartup_vratio_prefetch_more_than4;
	enum dcn_bw_defs planes_with_room_to_increase_vstartup_destination_line_times_for_prefetch_less_than2;
	enum dcn_bw_defs v_ratio_prefetch_more_than4;
	enum dcn_bw_defs destination_line_times_for_prefetch_less_than2;
	float prefetch_mode;
	float dstx_after_scaler;
	float dsty_after_scaler;
	float v_update_offset_pix[number_of_planes_minus_one + 1];
	float total_repeater_delay_time;
	float v_update_width_pix[number_of_planes_minus_one + 1];
	float v_ready_offset_pix[number_of_planes_minus_one + 1];
	float t_setup;
	float t_wait;
	float bandwidth_available_for_immediate_flip;
	float tot_immediate_flip_bytes;
	float max_rd_bandwidth;
	float time_for_fetching_meta_pte;
	float time_for_fetching_row_in_vblank;
	float lines_to_request_prefetch_pixel_data;
	float required_prefetch_pix_data_bw;
	enum dcn_bw_defs prefetch_mode_supported;
	float active_dp_ps;
	float lb_latency_hiding_source_lines_y;
	float lb_latency_hiding_source_lines_c;
	float effective_lb_latency_hiding_y;
	float effective_lb_latency_hiding_c;
	float dpp_output_buffer_lines_y;
	float dpp_output_buffer_lines_c;
	float dppopp_buffering_y;
	float max_det_buffering_time_y;
	float active_dram_clock_change_latency_margin_y;
	float dppopp_buffering_c;
	float max_det_buffering_time_c;
	float active_dram_clock_change_latency_margin_c;
	float writeback_dram_clock_change_latency_margin;
	float min_active_dram_clock_change_margin;
	float v_blank_of_min_active_dram_clock_change_margin;
	float second_min_active_dram_clock_change_margin;
	float min_vblank_dram_clock_change_margin;
	float dram_clock_change_margin;
	float dram_clock_change_support;
	float wr_bandwidth;
	float max_used_bw;
};

struct dcn_soc_bounding_box {
	float sr_exit_time; /*us*/
	float sr_enter_plus_exit_time; /*us*/
	float urgent_latency; /*us*/
	float write_back_latency; /*us*/
	float percent_of_ideal_drambw_received_after_urg_latency; /*%*/
	int max_request_size; /*bytes*/
	float dcfclkv_max0p9; /*MHz*/
	float dcfclkv_nom0p8; /*MHz*/
	float dcfclkv_mid0p72; /*MHz*/
	float dcfclkv_min0p65; /*MHz*/
	float max_dispclk_vmax0p9; /*MHz*/
	float max_dispclk_vmid0p72; /*MHz*/
	float max_dispclk_vnom0p8; /*MHz*/
	float max_dispclk_vmin0p65; /*MHz*/
	float max_dppclk_vmax0p9; /*MHz*/
	float max_dppclk_vnom0p8; /*MHz*/
	float max_dppclk_vmid0p72; /*MHz*/
	float max_dppclk_vmin0p65; /*MHz*/
	float socclk; /*MHz*/
	float fabric_and_dram_bandwidth_vmax0p9; /*GB/s*/
	float fabric_and_dram_bandwidth_vnom0p8; /*GB/s*/
	float fabric_and_dram_bandwidth_vmid0p72; /*GB/s*/
	float fabric_and_dram_bandwidth_vmin0p65; /*GB/s*/
	float phyclkv_max0p9; /*MHz*/
	float phyclkv_nom0p8; /*MHz*/
	float phyclkv_mid0p72; /*MHz*/
	float phyclkv_min0p65; /*MHz*/
	float downspreading; /*%*/
	int round_trip_ping_latency_cycles; /*DCFCLK Cycles*/
	int urgent_out_of_order_return_per_channel; /*bytes*/
	int number_of_channels;
	int vmm_page_size; /*bytes*/
	float dram_clock_change_latency; /*us*/
	int return_bus_width; /*bytes*/
	float percent_disp_bw_limit; /*%*/
};
extern const struct dcn_soc_bounding_box dcn10_soc_defaults;

struct dcn_ip_params {
	float rob_buffer_size_in_kbyte;
	float det_buffer_size_in_kbyte;
	float dpp_output_buffer_pixels;
	float opp_output_buffer_lines;
	float pixel_chunk_size_in_kbyte;
	enum dcn_bw_defs pte_enable;
	int pte_chunk_size; /*kbytes*/
	int meta_chunk_size; /*kbytes*/
	int writeback_chunk_size; /*kbytes*/
	enum dcn_bw_defs odm_capability;
	enum dcn_bw_defs dsc_capability;
	int line_buffer_size; /*bit*/
	int max_line_buffer_lines;
	enum dcn_bw_defs is_line_buffer_bpp_fixed;
	int line_buffer_fixed_bpp;
	int writeback_luma_buffer_size; /*kbytes*/
	int writeback_chroma_buffer_size; /*kbytes*/
	int max_num_dpp;
	int max_num_writeback;
	int max_dchub_topscl_throughput; /*pixels/dppclk*/
	int max_pscl_tolb_throughput; /*pixels/dppclk*/
	int max_lb_tovscl_throughput; /*pixels/dppclk*/
	int max_vscl_tohscl_throughput; /*pixels/dppclk*/
	float max_hscl_ratio;
	float max_vscl_ratio;
	int max_hscl_taps;
	int max_vscl_taps;
	int pte_buffer_size_in_requests;
	float dispclk_ramping_margin; /*%*/
	float under_scan_factor;
	int max_inter_dcn_tile_repeaters;
	enum dcn_bw_defs can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one;
	enum dcn_bw_defs bug_forcing_luma_and_chroma_request_to_same_size_fixed;
	int dcfclk_cstate_latency;
};
extern const struct dcn_ip_params dcn10_ip_defaults;

bool dcn_validate_bandwidth(
		struct dc *dc,
		struct dc_state *context);

unsigned int dcn_find_dcfclk_suits_all(
	const struct dc *dc,
	struct dc_clocks *clocks);

void dcn_bw_update_from_pplib(struct dc *dc);
void dcn_bw_notify_pplib_of_wm_ranges(struct dc *dc);
void dcn_bw_sync_calcs_and_dml(struct dc *dc);

#endif /* __DCN_CALCS_H__ */

