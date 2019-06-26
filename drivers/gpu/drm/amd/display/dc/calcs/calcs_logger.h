/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef _CALCS_CALCS_LOGGER_H_
#define _CALCS_CALCS_LOGGER_H_
#define DC_LOGGER ctx->logger

static void print_bw_calcs_dceip(struct dc_context *ctx, const struct bw_calcs_dceip *dceip)
{

	DC_LOG_BANDWIDTH_CALCS("#####################################################################");
	DC_LOG_BANDWIDTH_CALCS("struct bw_calcs_dceip");
	DC_LOG_BANDWIDTH_CALCS("#####################################################################");
	DC_LOG_BANDWIDTH_CALCS("	[enum]   bw_calcs_version version %d", dceip->version);
	DC_LOG_BANDWIDTH_CALCS("	[bool] large_cursor: %d", dceip->large_cursor);
	DC_LOG_BANDWIDTH_CALCS("	[bool] dmif_pipe_en_fbc_chunk_tracker: %d", dceip->dmif_pipe_en_fbc_chunk_tracker);
	DC_LOG_BANDWIDTH_CALCS("	[bool] display_write_back_supported: %d", dceip->display_write_back_supported);
	DC_LOG_BANDWIDTH_CALCS("	[bool] argb_compression_support: %d", dceip->argb_compression_support);
	DC_LOG_BANDWIDTH_CALCS("	[bool] pre_downscaler_enabled: %d", dceip->pre_downscaler_enabled);
	DC_LOG_BANDWIDTH_CALCS("	[bool] underlay_downscale_prefetch_enabled: %d",
				dceip->underlay_downscale_prefetch_enabled);
	DC_LOG_BANDWIDTH_CALCS("	[bool] graphics_lb_nodownscaling_multi_line_prefetching: %d",
				dceip->graphics_lb_nodownscaling_multi_line_prefetching);
	DC_LOG_BANDWIDTH_CALCS("	[bool] limit_excessive_outstanding_dmif_requests: %d",
				dceip->limit_excessive_outstanding_dmif_requests);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] cursor_max_outstanding_group_num: %d",
				dceip->cursor_max_outstanding_group_num);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] lines_interleaved_into_lb: %d", dceip->lines_interleaved_into_lb);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] low_power_tiling_mode: %d", dceip->low_power_tiling_mode);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] chunk_width: %d", dceip->chunk_width);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_graphics_pipes: %d", dceip->number_of_graphics_pipes);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_underlay_pipes: %d", dceip->number_of_underlay_pipes);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] max_dmif_buffer_allocated: %d", dceip->max_dmif_buffer_allocated);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] graphics_dmif_size: %d", dceip->graphics_dmif_size);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] underlay_luma_dmif_size: %d", dceip->underlay_luma_dmif_size);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] underlay_chroma_dmif_size: %d", dceip->underlay_chroma_dmif_size);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] scatter_gather_lines_of_pte_prefetching_in_linear_mode: %d",
				dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] display_write_back420_luma_mcifwr_buffer_size: %d",
				dceip->display_write_back420_luma_mcifwr_buffer_size);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] display_write_back420_chroma_mcifwr_buffer_size: %d",
				dceip->display_write_back420_chroma_mcifwr_buffer_size);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] scatter_gather_pte_request_rows_in_tiling_mode: %d",
				dceip->scatter_gather_pte_request_rows_in_tiling_mode);
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] underlay_vscaler_efficiency10_bit_per_component: %d",
				bw_fixed_to_int(dceip->underlay_vscaler_efficiency10_bit_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] underlay_vscaler_efficiency12_bit_per_component: %d",
				bw_fixed_to_int(dceip->underlay_vscaler_efficiency12_bit_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] graphics_vscaler_efficiency6_bit_per_component: %d",
				bw_fixed_to_int(dceip->graphics_vscaler_efficiency6_bit_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] graphics_vscaler_efficiency8_bit_per_component: %d",
				bw_fixed_to_int(dceip->graphics_vscaler_efficiency8_bit_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] graphics_vscaler_efficiency10_bit_per_component: %d",
				bw_fixed_to_int(dceip->graphics_vscaler_efficiency10_bit_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] graphics_vscaler_efficiency12_bit_per_component: %d",
				bw_fixed_to_int(dceip->graphics_vscaler_efficiency12_bit_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] alpha_vscaler_efficiency: %d",
				bw_fixed_to_int(dceip->alpha_vscaler_efficiency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lb_write_pixels_per_dispclk: %d",
				bw_fixed_to_int(dceip->lb_write_pixels_per_dispclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lb_size_per_component444: %d",
				bw_fixed_to_int(dceip->lb_size_per_component444));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_and_dram_clock_state_change_gated_before_cursor: %d",
				bw_fixed_to_int(dceip->stutter_and_dram_clock_state_change_gated_before_cursor));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] underlay420_luma_lb_size_per_component: %d",
				bw_fixed_to_int(dceip->underlay420_luma_lb_size_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] underlay420_chroma_lb_size_per_component: %d",
				bw_fixed_to_int(dceip->underlay420_chroma_lb_size_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] underlay422_lb_size_per_component: %d",
				bw_fixed_to_int(dceip->underlay422_lb_size_per_component));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] cursor_chunk_width: %d", bw_fixed_to_int(dceip->cursor_chunk_width));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] cursor_dcp_buffer_lines: %d",
				bw_fixed_to_int(dceip->cursor_dcp_buffer_lines));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] underlay_maximum_width_efficient_for_tiling: %d",
				bw_fixed_to_int(dceip->underlay_maximum_width_efficient_for_tiling));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] underlay_maximum_height_efficient_for_tiling: %d",
				bw_fixed_to_int(dceip->underlay_maximum_height_efficient_for_tiling));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display: %d",
				bw_fixed_to_int(dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation: %d",
				bw_fixed_to_int(dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] minimum_outstanding_pte_request_limit: %d",
				bw_fixed_to_int(dceip->minimum_outstanding_pte_request_limit));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] maximum_total_outstanding_pte_requests_allowed_by_saw: %d",
				bw_fixed_to_int(dceip->maximum_total_outstanding_pte_requests_allowed_by_saw));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] linear_mode_line_request_alternation_slice: %d",
				bw_fixed_to_int(dceip->linear_mode_line_request_alternation_slice));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] request_efficiency: %d", bw_fixed_to_int(dceip->request_efficiency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk_per_request: %d", bw_fixed_to_int(dceip->dispclk_per_request));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk_ramping_factor: %d",
				bw_fixed_to_int(dceip->dispclk_ramping_factor));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] display_pipe_throughput_factor: %d",
				bw_fixed_to_int(dceip->display_pipe_throughput_factor));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mcifwr_all_surfaces_burst_time: %d",
				bw_fixed_to_int(dceip->mcifwr_all_surfaces_burst_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmif_request_buffer_size: %d",
				bw_fixed_to_int(dceip->dmif_request_buffer_size));


}

static void print_bw_calcs_vbios(struct dc_context *ctx, const struct bw_calcs_vbios *vbios)
{

	DC_LOG_BANDWIDTH_CALCS("#####################################################################");
	DC_LOG_BANDWIDTH_CALCS("struct bw_calcs_vbios vbios");
	DC_LOG_BANDWIDTH_CALCS("#####################################################################");
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines memory_type: %d", vbios->memory_type);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines memory_type: %d", vbios->memory_type);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] dram_channel_width_in_bits: %d", vbios->dram_channel_width_in_bits);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_dram_channels: %d", vbios->number_of_dram_channels);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_dram_banks: %d", vbios->number_of_dram_banks);
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] low_yclk: %d", bw_fixed_to_int(vbios->low_yclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid_yclk: %d", bw_fixed_to_int(vbios->mid_yclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] high_yclk: %d", bw_fixed_to_int(vbios->high_yclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] low_sclk: %d", bw_fixed_to_int(vbios->low_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid1_sclk: %d", bw_fixed_to_int(vbios->mid1_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid2_sclk: %d", bw_fixed_to_int(vbios->mid2_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid3_sclk: %d", bw_fixed_to_int(vbios->mid3_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid4_sclk: %d", bw_fixed_to_int(vbios->mid4_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid5_sclk: %d", bw_fixed_to_int(vbios->mid5_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid6_sclk: %d", bw_fixed_to_int(vbios->mid6_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] high_sclk: %d", bw_fixed_to_int(vbios->high_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] low_voltage_max_dispclk: %d",
				bw_fixed_to_int(vbios->low_voltage_max_dispclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid_voltage_max_dispclk;: %d",
				bw_fixed_to_int(vbios->mid_voltage_max_dispclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] high_voltage_max_dispclk;: %d",
				bw_fixed_to_int(vbios->high_voltage_max_dispclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] low_voltage_max_phyclk: %d",
				bw_fixed_to_int(vbios->low_voltage_max_phyclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mid_voltage_max_phyclk: %d",
				bw_fixed_to_int(vbios->mid_voltage_max_phyclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] high_voltage_max_phyclk: %d",
				bw_fixed_to_int(vbios->high_voltage_max_phyclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] data_return_bus_width: %d", bw_fixed_to_int(vbios->data_return_bus_width));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] trc: %d", bw_fixed_to_int(vbios->trc));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmifmc_urgent_latency: %d", bw_fixed_to_int(vbios->dmifmc_urgent_latency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_self_refresh_exit_latency: %d",
				bw_fixed_to_int(vbios->stutter_self_refresh_exit_latency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_self_refresh_entry_latency: %d",
				bw_fixed_to_int(vbios->stutter_self_refresh_entry_latency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] nbp_state_change_latency: %d",
				bw_fixed_to_int(vbios->nbp_state_change_latency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mcifwrmc_urgent_latency: %d",
				bw_fixed_to_int(vbios->mcifwrmc_urgent_latency));
	DC_LOG_BANDWIDTH_CALCS("	[bool] scatter_gather_enable: %d", vbios->scatter_gather_enable);
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] down_spread_percentage: %d",
				bw_fixed_to_int(vbios->down_spread_percentage));
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] cursor_width: %d", vbios->cursor_width);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] average_compression_rate: %d", vbios->average_compression_rate);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_request_slots_gmc_reserves_for_dmif_per_channel: %d",
				vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel);
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] blackout_duration: %d", bw_fixed_to_int(vbios->blackout_duration));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] maximum_blackout_recovery_time: %d",
				bw_fixed_to_int(vbios->maximum_blackout_recovery_time));


}

static void print_bw_calcs_data(struct dc_context *ctx, struct bw_calcs_data *data)
{

	int i, j, k;

	DC_LOG_BANDWIDTH_CALCS("#####################################################################");
	DC_LOG_BANDWIDTH_CALCS("struct bw_calcs_data data");
	DC_LOG_BANDWIDTH_CALCS("#####################################################################");
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_displays: %d", data->number_of_displays);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines underlay_surface_type: %d", data->underlay_surface_type);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines panning_and_bezel_adjustment: %d",
				data->panning_and_bezel_adjustment);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines graphics_tiling_mode: %d", data->graphics_tiling_mode);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] graphics_lb_bpc: %d", data->graphics_lb_bpc);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] underlay_lb_bpc: %d", data->underlay_lb_bpc);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines underlay_tiling_mode: %d", data->underlay_tiling_mode);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines d0_underlay_mode: %d", data->d0_underlay_mode);
	DC_LOG_BANDWIDTH_CALCS("	[bool] d1_display_write_back_dwb_enable: %d", data->d1_display_write_back_dwb_enable);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines d1_underlay_mode: %d", data->d1_underlay_mode);
	DC_LOG_BANDWIDTH_CALCS("	[bool] cpup_state_change_enable: %d", data->cpup_state_change_enable);
	DC_LOG_BANDWIDTH_CALCS("	[bool] cpuc_state_change_enable: %d", data->cpuc_state_change_enable);
	DC_LOG_BANDWIDTH_CALCS("	[bool] nbp_state_change_enable: %d", data->nbp_state_change_enable);
	DC_LOG_BANDWIDTH_CALCS("	[bool] stutter_mode_enable: %d", data->stutter_mode_enable);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] y_clk_level: %d", data->y_clk_level);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] sclk_level: %d", data->sclk_level);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_underlay_surfaces: %d", data->number_of_underlay_surfaces);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_dram_wrchannels: %d", data->number_of_dram_wrchannels);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] chunk_request_delay: %d", data->chunk_request_delay);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] number_of_dram_channels: %d", data->number_of_dram_channels);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines underlay_micro_tile_mode: %d", data->underlay_micro_tile_mode);
	DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines graphics_micro_tile_mode: %d", data->graphics_micro_tile_mode);
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] max_phyclk: %d", bw_fixed_to_int(data->max_phyclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dram_efficiency: %d", bw_fixed_to_int(data->dram_efficiency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_width_after_surface_type: %d",
				bw_fixed_to_int(data->src_width_after_surface_type));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_height_after_surface_type: %d",
				bw_fixed_to_int(data->src_height_after_surface_type));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] hsr_after_surface_type: %d",
				bw_fixed_to_int(data->hsr_after_surface_type));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] vsr_after_surface_type: %d", bw_fixed_to_int(data->vsr_after_surface_type));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_width_after_rotation: %d",
				bw_fixed_to_int(data->src_width_after_rotation));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_height_after_rotation: %d",
				bw_fixed_to_int(data->src_height_after_rotation));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] hsr_after_rotation: %d", bw_fixed_to_int(data->hsr_after_rotation));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] vsr_after_rotation: %d", bw_fixed_to_int(data->vsr_after_rotation));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] source_height_pixels: %d", bw_fixed_to_int(data->source_height_pixels));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] hsr_after_stereo: %d", bw_fixed_to_int(data->hsr_after_stereo));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] vsr_after_stereo: %d", bw_fixed_to_int(data->vsr_after_stereo));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] source_width_in_lb: %d", bw_fixed_to_int(data->source_width_in_lb));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lb_line_pitch: %d", bw_fixed_to_int(data->lb_line_pitch));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] underlay_maximum_source_efficient_for_tiling: %d",
				bw_fixed_to_int(data->underlay_maximum_source_efficient_for_tiling));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] num_lines_at_frame_start: %d",
				bw_fixed_to_int(data->num_lines_at_frame_start));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] min_dmif_size_in_time: %d", bw_fixed_to_int(data->min_dmif_size_in_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] min_mcifwr_size_in_time: %d",
				bw_fixed_to_int(data->min_mcifwr_size_in_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_requests_for_dmif_size: %d",
				bw_fixed_to_int(data->total_requests_for_dmif_size));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] peak_pte_request_to_eviction_ratio_limiting: %d",
				bw_fixed_to_int(data->peak_pte_request_to_eviction_ratio_limiting));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] useful_pte_per_pte_request: %d",
				bw_fixed_to_int(data->useful_pte_per_pte_request));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_pte_request_rows: %d",
				bw_fixed_to_int(data->scatter_gather_pte_request_rows));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_row_height: %d",
				bw_fixed_to_int(data->scatter_gather_row_height));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_pte_requests_in_vblank: %d",
				bw_fixed_to_int(data->scatter_gather_pte_requests_in_vblank));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] inefficient_linear_pitch_in_bytes: %d",
				bw_fixed_to_int(data->inefficient_linear_pitch_in_bytes));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] cursor_total_data: %d", bw_fixed_to_int(data->cursor_total_data));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] cursor_total_request_groups: %d",
				bw_fixed_to_int(data->cursor_total_request_groups));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_total_pte_requests: %d",
				bw_fixed_to_int(data->scatter_gather_total_pte_requests));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_total_pte_request_groups: %d",
				bw_fixed_to_int(data->scatter_gather_total_pte_request_groups));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] tile_width_in_pixels: %d", bw_fixed_to_int(data->tile_width_in_pixels));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmif_total_number_of_data_request_page_close_open: %d",
				bw_fixed_to_int(data->dmif_total_number_of_data_request_page_close_open));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mcifwr_total_number_of_data_request_page_close_open: %d",
				bw_fixed_to_int(data->mcifwr_total_number_of_data_request_page_close_open));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] bytes_per_page_close_open: %d",
				bw_fixed_to_int(data->bytes_per_page_close_open));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mcifwr_total_page_close_open_time: %d",
				bw_fixed_to_int(data->mcifwr_total_page_close_open_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_requests_for_adjusted_dmif_size: %d",
				bw_fixed_to_int(data->total_requests_for_adjusted_dmif_size));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_dmifmc_urgent_trips: %d",
				bw_fixed_to_int(data->total_dmifmc_urgent_trips));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_dmifmc_urgent_latency: %d",
				bw_fixed_to_int(data->total_dmifmc_urgent_latency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_display_reads_required_data: %d",
				bw_fixed_to_int(data->total_display_reads_required_data));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_display_reads_required_dram_access_data: %d",
				bw_fixed_to_int(data->total_display_reads_required_dram_access_data));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_display_writes_required_data: %d",
				bw_fixed_to_int(data->total_display_writes_required_data));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_display_writes_required_dram_access_data: %d",
				bw_fixed_to_int(data->total_display_writes_required_dram_access_data));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] display_reads_required_data: %d",
				bw_fixed_to_int(data->display_reads_required_data));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] display_reads_required_dram_access_data: %d",
				bw_fixed_to_int(data->display_reads_required_dram_access_data));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmif_total_page_close_open_time: %d",
				bw_fixed_to_int(data->dmif_total_page_close_open_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] min_cursor_memory_interface_buffer_size_in_time: %d",
				bw_fixed_to_int(data->min_cursor_memory_interface_buffer_size_in_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] min_read_buffer_size_in_time: %d",
				bw_fixed_to_int(data->min_read_buffer_size_in_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] display_reads_time_for_data_transfer: %d",
				bw_fixed_to_int(data->display_reads_time_for_data_transfer));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] display_writes_time_for_data_transfer: %d",
				bw_fixed_to_int(data->display_writes_time_for_data_transfer));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmif_required_dram_bandwidth: %d",
				bw_fixed_to_int(data->dmif_required_dram_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mcifwr_required_dram_bandwidth: %d",
				bw_fixed_to_int(data->mcifwr_required_dram_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] required_dmifmc_urgent_latency_for_page_close_open: %d",
				bw_fixed_to_int(data->required_dmifmc_urgent_latency_for_page_close_open));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] required_mcifmcwr_urgent_latency: %d",
				bw_fixed_to_int(data->required_mcifmcwr_urgent_latency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] required_dram_bandwidth_gbyte_per_second: %d",
				bw_fixed_to_int(data->required_dram_bandwidth_gbyte_per_second));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dram_bandwidth: %d", bw_fixed_to_int(data->dram_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmif_required_sclk: %d", bw_fixed_to_int(data->dmif_required_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mcifwr_required_sclk: %d", bw_fixed_to_int(data->mcifwr_required_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] required_sclk: %d", bw_fixed_to_int(data->required_sclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] downspread_factor: %d", bw_fixed_to_int(data->downspread_factor));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] v_scaler_efficiency: %d", bw_fixed_to_int(data->v_scaler_efficiency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scaler_limits_factor: %d", bw_fixed_to_int(data->scaler_limits_factor));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] display_pipe_pixel_throughput: %d",
				bw_fixed_to_int(data->display_pipe_pixel_throughput));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_dispclk_required_with_ramping: %d",
				bw_fixed_to_int(data->total_dispclk_required_with_ramping));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_dispclk_required_without_ramping: %d",
				bw_fixed_to_int(data->total_dispclk_required_without_ramping));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_read_request_bandwidth: %d",
				bw_fixed_to_int(data->total_read_request_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_write_request_bandwidth: %d",
				bw_fixed_to_int(data->total_write_request_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk_required_for_total_read_request_bandwidth: %d",
				bw_fixed_to_int(data->dispclk_required_for_total_read_request_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_dispclk_required_with_ramping_with_request_bandwidth: %d",
				bw_fixed_to_int(data->total_dispclk_required_with_ramping_with_request_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_dispclk_required_without_ramping_with_request_bandwidth: %d",
				bw_fixed_to_int(data->total_dispclk_required_without_ramping_with_request_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk: %d", bw_fixed_to_int(data->dispclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] blackout_recovery_time: %d", bw_fixed_to_int(data->blackout_recovery_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] min_pixels_per_data_fifo_entry: %d",
				bw_fixed_to_int(data->min_pixels_per_data_fifo_entry));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] sclk_deep_sleep: %d", bw_fixed_to_int(data->sclk_deep_sleep));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] chunk_request_time: %d", bw_fixed_to_int(data->chunk_request_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] cursor_request_time: %d", bw_fixed_to_int(data->cursor_request_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] line_source_pixels_transfer_time: %d",
				bw_fixed_to_int(data->line_source_pixels_transfer_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmifdram_access_efficiency: %d",
				bw_fixed_to_int(data->dmifdram_access_efficiency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mcifwrdram_access_efficiency: %d",
				bw_fixed_to_int(data->mcifwrdram_access_efficiency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_average_bandwidth_no_compression: %d",
				bw_fixed_to_int(data->total_average_bandwidth_no_compression));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_average_bandwidth: %d",
				bw_fixed_to_int(data->total_average_bandwidth));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] total_stutter_cycle_duration: %d",
				bw_fixed_to_int(data->total_stutter_cycle_duration));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_burst_time: %d", bw_fixed_to_int(data->stutter_burst_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] time_in_self_refresh: %d", bw_fixed_to_int(data->time_in_self_refresh));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_efficiency: %d", bw_fixed_to_int(data->stutter_efficiency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] worst_number_of_trips_to_memory: %d",
				bw_fixed_to_int(data->worst_number_of_trips_to_memory));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] immediate_flip_time: %d", bw_fixed_to_int(data->immediate_flip_time));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] latency_for_non_dmif_clients: %d",
				bw_fixed_to_int(data->latency_for_non_dmif_clients));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] latency_for_non_mcifwr_clients: %d",
				bw_fixed_to_int(data->latency_for_non_mcifwr_clients));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmifmc_urgent_latency_supported_in_high_sclk_and_yclk: %d",
				bw_fixed_to_int(data->dmifmc_urgent_latency_supported_in_high_sclk_and_yclk));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] nbp_state_dram_speed_change_margin: %d",
				bw_fixed_to_int(data->nbp_state_dram_speed_change_margin));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] display_reads_time_for_data_transfer_and_urgent_latency: %d",
				bw_fixed_to_int(data->display_reads_time_for_data_transfer_and_urgent_latency));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dram_speed_change_margin: %d",
				bw_fixed_to_int(data->dram_speed_change_margin));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] min_vblank_dram_speed_change_margin: %d",
				bw_fixed_to_int(data->min_vblank_dram_speed_change_margin));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] min_stutter_refresh_duration: %d",
				bw_fixed_to_int(data->min_stutter_refresh_duration));
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] total_stutter_dmif_buffer_size: %d", data->total_stutter_dmif_buffer_size);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] total_bytes_requested: %d", data->total_bytes_requested);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] min_stutter_dmif_buffer_size: %d", data->min_stutter_dmif_buffer_size);
	DC_LOG_BANDWIDTH_CALCS("	[uint32_t] num_stutter_bursts: %d", data->num_stutter_bursts);
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] v_blank_nbp_state_dram_speed_change_latency_supported: %d",
				bw_fixed_to_int(data->v_blank_nbp_state_dram_speed_change_latency_supported));
	DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] nbp_state_dram_speed_change_latency_supported: %d",
				bw_fixed_to_int(data->nbp_state_dram_speed_change_latency_supported));

	for (i = 0; i < maximum_number_of_surfaces; i++) {
		DC_LOG_BANDWIDTH_CALCS("	[bool] fbc_en[%d]:%d\n", i, data->fbc_en[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] lpt_en[%d]:%d", i, data->lpt_en[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] displays_match_flag[%d]:%d", i, data->displays_match_flag[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] use_alpha[%d]:%d", i, data->use_alpha[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] orthogonal_rotation[%d]:%d", i, data->orthogonal_rotation[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] enable[%d]:%d", i, data->enable[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] access_one_channel_only[%d]:%d", i, data->access_one_channel_only[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] scatter_gather_enable_for_pipe[%d]:%d",
					i, data->scatter_gather_enable_for_pipe[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] interlace_mode[%d]:%d",
					i, data->interlace_mode[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] display_pstate_change_enable[%d]:%d",
					i, data->display_pstate_change_enable[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bool] line_buffer_prefetch[%d]:%d", i, data->line_buffer_prefetch[i]);
		DC_LOG_BANDWIDTH_CALCS("	[uint32_t] bytes_per_pixel[%d]:%d", i, data->bytes_per_pixel[i]);
		DC_LOG_BANDWIDTH_CALCS("	[uint32_t] max_chunks_non_fbc_mode[%d]:%d",
					i, data->max_chunks_non_fbc_mode[i]);
		DC_LOG_BANDWIDTH_CALCS("	[uint32_t] lb_bpc[%d]:%d", i, data->lb_bpc[i]);
		DC_LOG_BANDWIDTH_CALCS("	[uint32_t] output_bpphdmi[%d]:%d", i, data->output_bpphdmi[i]);
		DC_LOG_BANDWIDTH_CALCS("	[uint32_t] output_bppdp4_lane_hbr[%d]:%d", i, data->output_bppdp4_lane_hbr[i]);
		DC_LOG_BANDWIDTH_CALCS("	[uint32_t] output_bppdp4_lane_hbr2[%d]:%d",
					i, data->output_bppdp4_lane_hbr2[i]);
		DC_LOG_BANDWIDTH_CALCS("	[uint32_t] output_bppdp4_lane_hbr3[%d]:%d",
					i, data->output_bppdp4_lane_hbr3[i]);
		DC_LOG_BANDWIDTH_CALCS("	[enum] bw_defines stereo_mode[%d]:%d", i, data->stereo_mode[i]);
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmif_buffer_transfer_time[%d]:%d",
					i, bw_fixed_to_int(data->dmif_buffer_transfer_time[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] displays_with_same_mode[%d]:%d",
					i, bw_fixed_to_int(data->displays_with_same_mode[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_dmif_buffer_size[%d]:%d",
					i, bw_fixed_to_int(data->stutter_dmif_buffer_size[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_refresh_duration[%d]:%d",
					i, bw_fixed_to_int(data->stutter_refresh_duration[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_exit_watermark[%d]:%d",
					i, bw_fixed_to_int(data->stutter_exit_watermark[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_entry_watermark[%d]:%d",
					i, bw_fixed_to_int(data->stutter_entry_watermark[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] h_total[%d]:%d", i, bw_fixed_to_int(data->h_total[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] v_total[%d]:%d", i, bw_fixed_to_int(data->v_total[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] pixel_rate[%d]:%d", i, bw_fixed_to_int(data->pixel_rate[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_width[%d]:%d", i, bw_fixed_to_int(data->src_width[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] pitch_in_pixels[%d]:%d",
					i, bw_fixed_to_int(data->pitch_in_pixels[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] pitch_in_pixels_after_surface_type[%d]:%d",
					i, bw_fixed_to_int(data->pitch_in_pixels_after_surface_type[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_height[%d]:%d", i, bw_fixed_to_int(data->src_height[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scale_ratio[%d]:%d", i, bw_fixed_to_int(data->scale_ratio[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] h_taps[%d]:%d", i, bw_fixed_to_int(data->h_taps[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] v_taps[%d]:%d", i, bw_fixed_to_int(data->v_taps[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] h_scale_ratio[%d]:%d", i, bw_fixed_to_int(data->h_scale_ratio[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] v_scale_ratio[%d]:%d", i, bw_fixed_to_int(data->v_scale_ratio[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] rotation_angle[%d]:%d",
					i, bw_fixed_to_int(data->rotation_angle[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] compression_rate[%d]:%d",
					i, bw_fixed_to_int(data->compression_rate[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] hsr[%d]:%d", i, bw_fixed_to_int(data->hsr[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] vsr[%d]:%d", i, bw_fixed_to_int(data->vsr[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] source_width_rounded_up_to_chunks[%d]:%d",
					i, bw_fixed_to_int(data->source_width_rounded_up_to_chunks[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] source_width_pixels[%d]:%d",
					i, bw_fixed_to_int(data->source_width_pixels[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] source_height_rounded_up_to_chunks[%d]:%d",
					i, bw_fixed_to_int(data->source_height_rounded_up_to_chunks[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] display_bandwidth[%d]:%d",
					i, bw_fixed_to_int(data->display_bandwidth[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] request_bandwidth[%d]:%d",
					i, bw_fixed_to_int(data->request_bandwidth[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] bytes_per_request[%d]:%d",
					i, bw_fixed_to_int(data->bytes_per_request[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] useful_bytes_per_request[%d]:%d",
					i, bw_fixed_to_int(data->useful_bytes_per_request[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lines_interleaved_in_mem_access[%d]:%d",
					i, bw_fixed_to_int(data->lines_interleaved_in_mem_access[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] latency_hiding_lines[%d]:%d",
					i, bw_fixed_to_int(data->latency_hiding_lines[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lb_partitions[%d]:%d",
					i, bw_fixed_to_int(data->lb_partitions[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lb_partitions_max[%d]:%d",
					i, bw_fixed_to_int(data->lb_partitions_max[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk_required_with_ramping[%d]:%d",
					i, bw_fixed_to_int(data->dispclk_required_with_ramping[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk_required_without_ramping[%d]:%d",
					i, bw_fixed_to_int(data->dispclk_required_without_ramping[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] data_buffer_size[%d]:%d",
					i, bw_fixed_to_int(data->data_buffer_size[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] outstanding_chunk_request_limit[%d]:%d",
					i, bw_fixed_to_int(data->outstanding_chunk_request_limit[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] urgent_watermark[%d]:%d",
					i, bw_fixed_to_int(data->urgent_watermark[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] nbp_state_change_watermark[%d]:%d",
					i, bw_fixed_to_int(data->nbp_state_change_watermark[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] v_filter_init[%d]:%d", i, bw_fixed_to_int(data->v_filter_init[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] stutter_cycle_duration[%d]:%d",
					i, bw_fixed_to_int(data->stutter_cycle_duration[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] average_bandwidth[%d]:%d",
					i, bw_fixed_to_int(data->average_bandwidth[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] average_bandwidth_no_compression[%d]:%d",
					i, bw_fixed_to_int(data->average_bandwidth_no_compression[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_pte_request_limit[%d]:%d",
					i, bw_fixed_to_int(data->scatter_gather_pte_request_limit[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lb_size_per_component[%d]:%d",
					i, bw_fixed_to_int(data->lb_size_per_component[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] memory_chunk_size_in_bytes[%d]:%d",
					i, bw_fixed_to_int(data->memory_chunk_size_in_bytes[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] pipe_chunk_size_in_bytes[%d]:%d",
					i, bw_fixed_to_int(data->pipe_chunk_size_in_bytes[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] number_of_trips_to_memory_for_getting_apte_row[%d]:%d",
					i, bw_fixed_to_int(data->number_of_trips_to_memory_for_getting_apte_row[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] adjusted_data_buffer_size[%d]:%d",
					i, bw_fixed_to_int(data->adjusted_data_buffer_size[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] adjusted_data_buffer_size_in_memory[%d]:%d",
					i, bw_fixed_to_int(data->adjusted_data_buffer_size_in_memory[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] pixels_per_data_fifo_entry[%d]:%d",
					i, bw_fixed_to_int(data->pixels_per_data_fifo_entry[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_pte_requests_in_row[%d]:%d",
					i, bw_fixed_to_int(data->scatter_gather_pte_requests_in_row[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] pte_request_per_chunk[%d]:%d",
					i, bw_fixed_to_int(data->pte_request_per_chunk[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_page_width[%d]:%d",
					i, bw_fixed_to_int(data->scatter_gather_page_width[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] scatter_gather_page_height[%d]:%d",
					i, bw_fixed_to_int(data->scatter_gather_page_height[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lb_lines_in_per_line_out_in_beginning_of_frame[%d]:%d",
					i, bw_fixed_to_int(data->lb_lines_in_per_line_out_in_beginning_of_frame[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] lb_lines_in_per_line_out_in_middle_of_frame[%d]:%d",
					i, bw_fixed_to_int(data->lb_lines_in_per_line_out_in_middle_of_frame[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] cursor_width_pixels[%d]:%d",
					i, bw_fixed_to_int(data->cursor_width_pixels[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] minimum_latency_hiding[%d]:%d",
					i, bw_fixed_to_int(data->minimum_latency_hiding[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] maximum_latency_hiding[%d]:%d",
					i, bw_fixed_to_int(data->maximum_latency_hiding[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] minimum_latency_hiding_with_cursor[%d]:%d",
					i, bw_fixed_to_int(data->minimum_latency_hiding_with_cursor[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] maximum_latency_hiding_with_cursor[%d]:%d",
					i, bw_fixed_to_int(data->maximum_latency_hiding_with_cursor[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_pixels_for_first_output_pixel[%d]:%d",
					i, bw_fixed_to_int(data->src_pixels_for_first_output_pixel[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_pixels_for_last_output_pixel[%d]:%d",
					i, bw_fixed_to_int(data->src_pixels_for_last_output_pixel[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_data_for_first_output_pixel[%d]:%d",
					i, bw_fixed_to_int(data->src_data_for_first_output_pixel[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] src_data_for_last_output_pixel[%d]:%d",
					i, bw_fixed_to_int(data->src_data_for_last_output_pixel[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] active_time[%d]:%d", i, bw_fixed_to_int(data->active_time[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] horizontal_blank_and_chunk_granularity_factor[%d]:%d",
					i, bw_fixed_to_int(data->horizontal_blank_and_chunk_granularity_factor[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] cursor_latency_hiding[%d]:%d",
					i, bw_fixed_to_int(data->cursor_latency_hiding[i]));
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] v_blank_dram_speed_change_margin[%d]:%d",
					i, bw_fixed_to_int(data->v_blank_dram_speed_change_margin[i]));
		}

	for (i = 0; i < maximum_number_of_surfaces; i++) {
		for (j = 0; j < 3; j++) {
			for (k = 0; k < 8; k++) {

				DC_LOG_BANDWIDTH_CALCS("\n	[bw_fixed] line_source_transfer_time[%d][%d][%d]:%d",
					i, j, k, bw_fixed_to_int(data->line_source_transfer_time[i][j][k]));
				DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dram_speed_change_line_source_transfer_time[%d][%d][%d]:%d",
					i, j, k,
					bw_fixed_to_int(data->dram_speed_change_line_source_transfer_time[i][j][k]));
			}
		}
	}

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 8; j++) {

			DC_LOG_BANDWIDTH_CALCS("\n	[uint32_t] num_displays_with_margin[%d][%d]:%d",
					i, j, data->num_displays_with_margin[i][j]);
			DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmif_burst_time[%d][%d]:%d",
					i, j, bw_fixed_to_int(data->dmif_burst_time[i][j]));
			DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] mcifwr_burst_time[%d][%d]:%d",
					i, j, bw_fixed_to_int(data->mcifwr_burst_time[i][j]));
			DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] min_dram_speed_change_margin[%d][%d]:%d",
					i, j, bw_fixed_to_int(data->min_dram_speed_change_margin[i][j]));
			DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk_required_for_dram_speed_change[%d][%d]:%d",
					i, j, bw_fixed_to_int(data->dispclk_required_for_dram_speed_change[i][j]));
			DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] blackout_duration_margin[%d][%d]:%d",
					i, j, bw_fixed_to_int(data->blackout_duration_margin[i][j]));
			DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk_required_for_blackout_duration[%d][%d]:%d",
					i, j, bw_fixed_to_int(data->dispclk_required_for_blackout_duration[i][j]));
			DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dispclk_required_for_blackout_recovery[%d][%d]:%d",
					i, j, bw_fixed_to_int(data->dispclk_required_for_blackout_recovery[i][j]));
		}
	}

	for (i = 0; i < 6; i++) {
		DC_LOG_BANDWIDTH_CALCS("	[bw_fixed] dmif_required_sclk_for_urgent_latency[%d]:%d",
					i, bw_fixed_to_int(data->dmif_required_sclk_for_urgent_latency[i]));
	}
}
;

#endif /* _CALCS_CALCS_LOGGER_H_ */
