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

#include "clk_mgr.h"
#include "resource.h"
#include "dcn314_fpu.h"
#include "dml/display_mode_vba.h"

struct _vcs_dpi_ip_params_st dcn3_14_ip = {
	.VBlankNomDefaultUS = 668,
	.gpuvm_enable = 1,
	.gpuvm_max_page_table_levels = 1,
	.hostvm_enable = 1,
	.hostvm_max_page_table_levels = 2,
	.rob_buffer_size_kbytes = 64,
	.det_buffer_size_kbytes = DCN3_14_DEFAULT_DET_SIZE,
	.config_return_buffer_size_in_kbytes = 1792,
	.compressed_buffer_segment_size_in_kbytes = 64,
	.meta_fifo_size_in_kentries = 32,
	.zero_size_buffer_entries = 512,
	.compbuf_reserved_space_64b = 256,
	.compbuf_reserved_space_zs = 64,
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.meta_chunk_size_kbytes = 2,
	.min_meta_chunk_size_bytes = 256,
	.writeback_chunk_size_kbytes = 8,
	.ptoi_supported = false,
	.num_dsc = 4,
	.maximum_dsc_bits_per_component = 10,
	.dsc422_native_support = false,
	.is_line_buffer_bpp_fixed = true,
	.line_buffer_fixed_bpp = 48,
	.line_buffer_size_bits = 789504,
	.max_line_buffer_lines = 12,
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
	.dppclk_delay_subtotal = 46,
	.dppclk_delay_scl = 50,
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_cnvc_formatter = 27,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 119,
	.dynamic_metadata_vm_enabled = false,
	.odm_combine_4to1_supported = false,
	.dcc_supported = true,
};

struct _vcs_dpi_soc_bounding_box_st dcn3_14_soc = {
		/*TODO: correct dispclk/dppclk voltage level determination*/
	.clock_limits = {
		{
			.state = 0,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 600.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 186.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 1,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 209.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 2,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 209.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 3,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 371.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 4,
			.dispclk_mhz = 1200.0,
			.dppclk_mhz = 1200.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 417.0,
			.dtbclk_mhz = 625.0,
		},
	},
	.num_states = 5,
	.sr_exit_time_us = 9.0,
	.sr_enter_plus_exit_time_us = 11.0,
	.sr_exit_z8_time_us = 442.0,
	.sr_enter_plus_exit_z8_time_us = 560.0,
	.writeback_latency_us = 12.0,
	.dram_channel_width_bytes = 4,
	.round_trip_ping_latency_dcfclk_cycles = 106,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_sdp_bw_after_urgent = 80.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 65.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 60.0,
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 30.0,
	.max_avg_sdp_bw_use_normal_percent = 60.0,
	.max_avg_dram_bw_use_normal_percent = 60.0,
	.fabric_datapath_to_dcn_data_return_bytes = 32,
	.return_bus_width_bytes = 64,
	.downspread_percent = 0.38,
	.dcn_downspread_percent = 0.5,
	.gpuvm_min_page_size_bytes = 4096,
	.hostvm_min_page_size_bytes = 4096,
	.do_urgent_latency_adjustment = false,
	.urgent_latency_adjustment_fabric_clock_component_us = 0,
	.urgent_latency_adjustment_fabric_clock_reference_mhz = 0,
};


void dcn314_update_bw_bounding_box_fpu(struct dc *dc, struct clk_bw_params *bw_params)
{
	struct clk_limit_table *clk_table = &bw_params->clk_table;
	struct _vcs_dpi_voltage_scaling_st *clock_limits =
		dcn3_14_soc.clock_limits;
	unsigned int i, closest_clk_lvl;
	int max_dispclk_mhz = 0, max_dppclk_mhz = 0;
	int j;

	dc_assert_fp_enabled();

	// Default clock levels are used for diags, which may lead to overclocking.
	if (!IS_DIAG_DC(dc->ctx->dce_environment)) {

		dcn3_14_ip.max_num_otg = dc->res_pool->res_cap->num_timing_generator;
		dcn3_14_ip.max_num_dpp = dc->res_pool->pipe_count;

		if (bw_params->num_channels > 0)
			dcn3_14_soc.num_chans = bw_params->num_channels;

		ASSERT(dcn3_14_soc.num_chans);
		ASSERT(clk_table->num_entries);

		/* Prepass to find max clocks independent of voltage level. */
		for (i = 0; i < clk_table->num_entries; ++i) {
			if (clk_table->entries[i].dispclk_mhz > max_dispclk_mhz)
				max_dispclk_mhz = clk_table->entries[i].dispclk_mhz;
			if (clk_table->entries[i].dppclk_mhz > max_dppclk_mhz)
				max_dppclk_mhz = clk_table->entries[i].dppclk_mhz;
		}

		for (i = 0; i < clk_table->num_entries; i++) {
			/* loop backwards*/
			for (closest_clk_lvl = 0, j = dcn3_14_soc.num_states - 1; j >= 0; j--) {
				if ((unsigned int) dcn3_14_soc.clock_limits[j].dcfclk_mhz <= clk_table->entries[i].dcfclk_mhz) {
					closest_clk_lvl = j;
					break;
				}
			}
			if (clk_table->num_entries == 1) {
				/*smu gives one DPM level, let's take the highest one*/
				closest_clk_lvl = dcn3_14_soc.num_states - 1;
			}

			clock_limits[i].state = i;

			/* Clocks dependent on voltage level. */
			clock_limits[i].dcfclk_mhz = clk_table->entries[i].dcfclk_mhz;
			if (clk_table->num_entries == 1 &&
				clock_limits[i].dcfclk_mhz < dcn3_14_soc.clock_limits[closest_clk_lvl].dcfclk_mhz) {
				/*SMU fix not released yet*/
				clock_limits[i].dcfclk_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].dcfclk_mhz;
			}
			clock_limits[i].fabricclk_mhz = clk_table->entries[i].fclk_mhz;
			clock_limits[i].socclk_mhz = clk_table->entries[i].socclk_mhz;

			if (clk_table->entries[i].memclk_mhz && clk_table->entries[i].wck_ratio)
				clock_limits[i].dram_speed_mts = clk_table->entries[i].memclk_mhz * 2 * clk_table->entries[i].wck_ratio;

			/* Clocks independent of voltage level. */
			clock_limits[i].dispclk_mhz = max_dispclk_mhz ? max_dispclk_mhz :
				dcn3_14_soc.clock_limits[closest_clk_lvl].dispclk_mhz;

			clock_limits[i].dppclk_mhz = max_dppclk_mhz ? max_dppclk_mhz :
				dcn3_14_soc.clock_limits[closest_clk_lvl].dppclk_mhz;

			clock_limits[i].dram_bw_per_chan_gbps = dcn3_14_soc.clock_limits[closest_clk_lvl].dram_bw_per_chan_gbps;
			clock_limits[i].dscclk_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].dscclk_mhz;
			clock_limits[i].dtbclk_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].dtbclk_mhz;
			clock_limits[i].phyclk_d18_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].phyclk_d18_mhz;
			clock_limits[i].phyclk_mhz = dcn3_14_soc.clock_limits[closest_clk_lvl].phyclk_mhz;
		}
		for (i = 0; i < clk_table->num_entries; i++)
			dcn3_14_soc.clock_limits[i] = clock_limits[i];
		if (clk_table->num_entries) {
			dcn3_14_soc.num_states = clk_table->num_entries;
		}
	}

	if (max_dispclk_mhz) {
		dcn3_14_soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;
		dc->dml.soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;
	}

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		dml_init_instance(&dc->dml, &dcn3_14_soc, &dcn3_14_ip, DML_PROJECT_DCN31);
	else
		dml_init_instance(&dc->dml, &dcn3_14_soc, &dcn3_14_ip, DML_PROJECT_DCN31_FPGA);
}
