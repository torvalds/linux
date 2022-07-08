// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#include "resource.h"
#include "dcn321_fpu.h"
#include "dcn32/dcn32_resource.h"
#include "dcn321/dcn321_resource.h"

#define DCN3_2_DEFAULT_DET_SIZE 256

struct _vcs_dpi_ip_params_st dcn3_21_ip = {
	.gpuvm_enable = 0,
	.gpuvm_max_page_table_levels = 4,
	.hostvm_enable = 0,
	.rob_buffer_size_kbytes = 128,
	.det_buffer_size_kbytes = DCN3_2_DEFAULT_DET_SIZE,
	.config_return_buffer_size_in_kbytes = 1280,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.meta_fifo_size_in_kentries = 22,
	.zero_size_buffer_entries = 512,
	.compbuf_reserved_space_64b = 256,
	.compbuf_reserved_space_zs = 64,
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.alpha_pixel_chunk_size_kbytes = 4,
	.min_pixel_chunk_size_bytes = 1024,
	.dcc_meta_buffer_size_bytes = 6272,
	.meta_chunk_size_kbytes = 2,
	.min_meta_chunk_size_bytes = 256,
	.writeback_chunk_size_kbytes = 8,
	.ptoi_supported = false,
	.num_dsc = 4,
	.maximum_dsc_bits_per_component = 12,
	.maximum_pixels_per_line_per_dsc_unit = 6016,
	.dsc422_native_support = true,
	.is_line_buffer_bpp_fixed = true,
	.line_buffer_fixed_bpp = 57,
	.line_buffer_size_bits = 1171920,
	.max_line_buffer_lines = 32,
	.writeback_interface_buffer_size_kbytes = 90,
	.max_num_dpp = 4,
	.max_num_otg = 4,
	.max_num_hdmi_frl_outputs = 1,
	.max_num_wb = 1,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 6,
	.max_vscl_ratio = 6,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dpte_buffer_size_in_pte_reqs_luma = 64,
	.dpte_buffer_size_in_pte_reqs_chroma = 34,
	.dispclk_ramp_margin_percent = 1,
	.max_inter_dcn_tile_repeaters = 8,
	.cursor_buffer_size = 16,
	.cursor_chunk_size = 2,
	.writeback_line_buffer_buffer_size = 0,
	.writeback_min_hscl_ratio = 1,
	.writeback_min_vscl_ratio = 1,
	.writeback_max_hscl_ratio = 1,
	.writeback_max_vscl_ratio = 1,
	.writeback_max_hscl_taps = 1,
	.writeback_max_vscl_taps = 1,
	.dppclk_delay_subtotal = 47,
	.dppclk_delay_scl = 50,
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_cnvc_formatter = 28,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 125,
	.dynamic_metadata_vm_enabled = false,
	.odm_combine_4to1_supported = false,
	.dcc_supported = true,
	.max_num_dp2p0_outputs = 2,
	.max_num_dp2p0_streams = 4,
};

struct _vcs_dpi_soc_bounding_box_st dcn3_21_soc = {
	.clock_limits = {
		{
			.state = 0,
			.dcfclk_mhz = 1564.0,
			.fabricclk_mhz = 400.0,
			.dispclk_mhz = 2150.0,
			.dppclk_mhz = 2150.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.phyclk_d32_mhz = 625.0,
			.socclk_mhz = 1200.0,
			.dscclk_mhz = 716.667,
			.dram_speed_mts = 1600.0,
			.dtbclk_mhz = 1564.0,
		},
	},
	.num_states = 1,
	.sr_exit_time_us = 5.20,
	.sr_enter_plus_exit_time_us = 9.60,
	.sr_exit_z8_time_us = 285.0,
	.sr_enter_plus_exit_z8_time_us = 320,
	.writeback_latency_us = 12.0,
	.round_trip_ping_latency_dcfclk_cycles = 263,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.fclk_change_latency_us = 20,
	.usr_retraining_latency_us = 2,
	.smn_latency_us = 2,
	.mall_allocated_for_dcn_mbytes = 64,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_sdp_bw_after_urgent = 100.0,
	.pct_ideal_fabric_bw_after_urgent = 67.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 20.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 60.0, // N/A, for now keep as is until DML implemented
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 30.0, // N/A, for now keep as is until DML implemented
	.pct_ideal_dram_bw_after_urgent_strobe = 67.0,
	.max_avg_sdp_bw_use_normal_percent = 80.0,
	.max_avg_fabric_bw_use_normal_percent = 60.0,
	.max_avg_dram_bw_use_normal_strobe_percent = 50.0,
	.max_avg_dram_bw_use_normal_percent = 15.0,
	.num_chans = 8,
	.dram_channel_width_bytes = 2,
	.fabric_datapath_to_dcn_data_return_bytes = 64,
	.return_bus_width_bytes = 64,
	.downspread_percent = 0.38,
	.dcn_downspread_percent = 0.5,
	.dram_clock_change_latency_us = 400,
	.dispclk_dppclk_vco_speed_mhz = 4300.0,
	.do_urgent_latency_adjustment = true,
	.urgent_latency_adjustment_fabric_clock_component_us = 1.0,
	.urgent_latency_adjustment_fabric_clock_reference_mhz = 1000,
};

static void get_optimal_ntuple(struct _vcs_dpi_voltage_scaling_st *entry)
{
	if (entry->dcfclk_mhz > 0) {
		float bw_on_sdp = entry->dcfclk_mhz * dcn3_21_soc.return_bus_width_bytes * ((float)dcn3_21_soc.pct_ideal_sdp_bw_after_urgent / 100);

		entry->fabricclk_mhz = bw_on_sdp / (dcn3_21_soc.return_bus_width_bytes * ((float)dcn3_21_soc.pct_ideal_fabric_bw_after_urgent / 100));
		entry->dram_speed_mts = bw_on_sdp / (dcn3_21_soc.num_chans *
				dcn3_21_soc.dram_channel_width_bytes * ((float)dcn3_21_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only / 100));
	} else if (entry->fabricclk_mhz > 0) {
		float bw_on_fabric = entry->fabricclk_mhz * dcn3_21_soc.return_bus_width_bytes * ((float)dcn3_21_soc.pct_ideal_fabric_bw_after_urgent / 100);

		entry->dcfclk_mhz = bw_on_fabric / (dcn3_21_soc.return_bus_width_bytes * ((float)dcn3_21_soc.pct_ideal_sdp_bw_after_urgent / 100));
		entry->dram_speed_mts = bw_on_fabric / (dcn3_21_soc.num_chans *
				dcn3_21_soc.dram_channel_width_bytes * ((float)dcn3_21_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only / 100));
	} else if (entry->dram_speed_mts > 0) {
		float bw_on_dram = entry->dram_speed_mts * dcn3_21_soc.num_chans *
				dcn3_21_soc.dram_channel_width_bytes * ((float)dcn3_21_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only / 100);

		entry->fabricclk_mhz = bw_on_dram / (dcn3_21_soc.return_bus_width_bytes * ((float)dcn3_21_soc.pct_ideal_fabric_bw_after_urgent / 100));
		entry->dcfclk_mhz = bw_on_dram / (dcn3_21_soc.return_bus_width_bytes * ((float)dcn3_21_soc.pct_ideal_sdp_bw_after_urgent / 100));
	}
}

static float calculate_net_bw_in_kbytes_sec(struct _vcs_dpi_voltage_scaling_st *entry)
{
	float memory_bw_kbytes_sec;
	float fabric_bw_kbytes_sec;
	float sdp_bw_kbytes_sec;
	float limiting_bw_kbytes_sec;

	memory_bw_kbytes_sec = entry->dram_speed_mts * dcn3_21_soc.num_chans *
			dcn3_21_soc.dram_channel_width_bytes * ((float)dcn3_21_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only / 100);

	fabric_bw_kbytes_sec = entry->fabricclk_mhz * dcn3_21_soc.return_bus_width_bytes * ((float)dcn3_21_soc.pct_ideal_fabric_bw_after_urgent / 100);

	sdp_bw_kbytes_sec = entry->dcfclk_mhz * dcn3_21_soc.return_bus_width_bytes * ((float)dcn3_21_soc.pct_ideal_sdp_bw_after_urgent / 100);

	limiting_bw_kbytes_sec = memory_bw_kbytes_sec;

	if (fabric_bw_kbytes_sec < limiting_bw_kbytes_sec)
		limiting_bw_kbytes_sec = fabric_bw_kbytes_sec;

	if (sdp_bw_kbytes_sec < limiting_bw_kbytes_sec)
		limiting_bw_kbytes_sec = sdp_bw_kbytes_sec;

	return limiting_bw_kbytes_sec;
}

void dcn321_insert_entry_into_table_sorted(struct _vcs_dpi_voltage_scaling_st *table,
					   unsigned int *num_entries,
					   struct _vcs_dpi_voltage_scaling_st *entry)
{
	int i = 0;
	int index = 0;
	float net_bw_of_new_state = 0;

	dc_assert_fp_enabled();

	get_optimal_ntuple(entry);

	if (*num_entries == 0) {
		table[0] = *entry;
		(*num_entries)++;
	} else {
		net_bw_of_new_state = calculate_net_bw_in_kbytes_sec(entry);
		while (net_bw_of_new_state > calculate_net_bw_in_kbytes_sec(&table[index])) {
			index++;
			if (index >= *num_entries)
				break;
		}

		for (i = *num_entries; i > index; i--)
			table[i] = table[i - 1];

		table[index] = *entry;
		(*num_entries)++;
	}
}

