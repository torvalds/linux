/*
 * Copyright 2019-2021 Advanced Micro Devices, Inc.
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
#include "clk_mgr.h"
#include "dcn20/dcn20_resource.h"
#include "dcn303/dcn303_resource.h"

#include "dml/dcn20/dcn20_fpu.h"
#include "dcn303_fpu.h"

struct _vcs_dpi_ip_params_st dcn3_03_ip = {
		.use_min_dcfclk = 0,
		.clamp_min_dcfclk = 0,
		.odm_capable = 1,
		.gpuvm_enable = 1,
		.hostvm_enable = 0,
		.gpuvm_max_page_table_levels = 4,
		.hostvm_max_page_table_levels = 4,
		.hostvm_cached_page_table_levels = 0,
		.pte_group_size_bytes = 2048,
		.num_dsc = 2,
		.rob_buffer_size_kbytes = 184,
		.det_buffer_size_kbytes = 184,
		.dpte_buffer_size_in_pte_reqs_luma = 64,
		.dpte_buffer_size_in_pte_reqs_chroma = 34,
		.pde_proc_buffer_size_64k_reqs = 48,
		.dpp_output_buffer_pixels = 2560,
		.opp_output_buffer_lines = 1,
		.pixel_chunk_size_kbytes = 8,
		.pte_enable = 1,
		.max_page_table_levels = 2,
		.pte_chunk_size_kbytes = 2,  // ?
		.meta_chunk_size_kbytes = 2,
		.writeback_chunk_size_kbytes = 8,
		.line_buffer_size_bits = 789504,
		.is_line_buffer_bpp_fixed = 0,  // ?
		.line_buffer_fixed_bpp = 0,     // ?
		.dcc_supported = true,
		.writeback_interface_buffer_size_kbytes = 90,
		.writeback_line_buffer_buffer_size = 0,
		.max_line_buffer_lines = 12,
		.writeback_luma_buffer_size_kbytes = 12,  // writeback_line_buffer_buffer_size = 656640
		.writeback_chroma_buffer_size_kbytes = 8,
		.writeback_chroma_line_buffer_width_pixels = 4,
		.writeback_max_hscl_ratio = 1,
		.writeback_max_vscl_ratio = 1,
		.writeback_min_hscl_ratio = 1,
		.writeback_min_vscl_ratio = 1,
		.writeback_max_hscl_taps = 1,
		.writeback_max_vscl_taps = 1,
		.writeback_line_buffer_luma_buffer_size = 0,
		.writeback_line_buffer_chroma_buffer_size = 14643,
		.cursor_buffer_size = 8,
		.cursor_chunk_size = 2,
		.max_num_otg = 2,
		.max_num_dpp = 2,
		.max_num_wb = 1,
		.max_dchub_pscl_bw_pix_per_clk = 4,
		.max_pscl_lb_bw_pix_per_clk = 2,
		.max_lb_vscl_bw_pix_per_clk = 4,
		.max_vscl_hscl_bw_pix_per_clk = 4,
		.max_hscl_ratio = 6,
		.max_vscl_ratio = 6,
		.hscl_mults = 4,
		.vscl_mults = 4,
		.max_hscl_taps = 8,
		.max_vscl_taps = 8,
		.dispclk_ramp_margin_percent = 1,
		.underscan_factor = 1.11,
		.min_vblank_lines = 32,
		.dppclk_delay_subtotal = 46,
		.dynamic_metadata_vm_enabled = true,
		.dppclk_delay_scl_lb_only = 16,
		.dppclk_delay_scl = 50,
		.dppclk_delay_cnvc_formatter = 27,
		.dppclk_delay_cnvc_cursor = 6,
		.dispclk_delay_subtotal = 119,
		.dcfclk_cstate_latency = 5.2, // SRExitTime
		.max_inter_dcn_tile_repeaters = 8,
		.max_num_hdmi_frl_outputs = 1,
		.odm_combine_4to1_supported = false,

		.xfc_supported = false,
		.xfc_fill_bw_overhead_percent = 10.0,
		.xfc_fill_constant_bytes = 0,
		.gfx7_compat_tiling_supported = 0,
		.number_of_cursors = 1,
};

struct _vcs_dpi_soc_bounding_box_st dcn3_03_soc = {
		.clock_limits = {
				{
						.state = 0,
						.dispclk_mhz = 562.0,
						.dppclk_mhz = 300.0,
						.phyclk_mhz = 300.0,
						.phyclk_d18_mhz = 667.0,
						.dscclk_mhz = 405.6,
				},
		},

		.min_dcfclk = 500.0, /* TODO: set this to actual min DCFCLK */
		.num_states = 1,
		.sr_exit_time_us = 35.5,
		.sr_enter_plus_exit_time_us = 40,
		.urgent_latency_us = 4.0,
		.urgent_latency_pixel_data_only_us = 4.0,
		.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
		.urgent_latency_vm_data_only_us = 4.0,
		.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
		.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
		.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
		.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 80.0,
		.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 60.0,
		.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 40.0,
		.max_avg_sdp_bw_use_normal_percent = 60.0,
		.max_avg_dram_bw_use_normal_percent = 40.0,
		.writeback_latency_us = 12.0,
		.max_request_size_bytes = 256,
		.fabric_datapath_to_dcn_data_return_bytes = 64,
		.dcn_downspread_percent = 0.5,
		.downspread_percent = 0.38,
		.dram_page_open_time_ns = 50.0,
		.dram_rw_turnaround_time_ns = 17.5,
		.dram_return_buffer_per_channel_bytes = 8192,
		.round_trip_ping_latency_dcfclk_cycles = 156,
		.urgent_out_of_order_return_per_channel_bytes = 4096,
		.channel_interleave_bytes = 256,
		.num_banks = 8,
		.gpuvm_min_page_size_bytes = 4096,
		.hostvm_min_page_size_bytes = 4096,
		.dram_clock_change_latency_us = 404,
		.dummy_pstate_latency_us = 5,
		.writeback_dram_clock_change_latency_us = 23.0,
		.return_bus_width_bytes = 64,
		.dispclk_dppclk_vco_speed_mhz = 3650,
		.xfc_bus_transport_time_us = 20,      // ?
		.xfc_xbuf_latency_tolerance_us = 4,  // ?
		.use_urgent_burst_bw = 1,            // ?
		.do_urgent_latency_adjustment = true,
		.urgent_latency_adjustment_fabric_clock_component_us = 1.0,
		.urgent_latency_adjustment_fabric_clock_reference_mhz = 1000,
};

static void dcn303_get_optimal_dcfclk_fclk_for_uclk(unsigned int uclk_mts,
		unsigned int *optimal_dcfclk,
		unsigned int *optimal_fclk)
{
	double bw_from_dram, bw_from_dram1, bw_from_dram2;

	bw_from_dram1 = uclk_mts * dcn3_03_soc.num_chans *
		dcn3_03_soc.dram_channel_width_bytes * (dcn3_03_soc.max_avg_dram_bw_use_normal_percent / 100);
	bw_from_dram2 = uclk_mts * dcn3_03_soc.num_chans *
		dcn3_03_soc.dram_channel_width_bytes * (dcn3_03_soc.max_avg_sdp_bw_use_normal_percent / 100);

	bw_from_dram = (bw_from_dram1 < bw_from_dram2) ? bw_from_dram1 : bw_from_dram2;

	if (optimal_fclk)
		*optimal_fclk = bw_from_dram /
		(dcn3_03_soc.fabric_datapath_to_dcn_data_return_bytes *
				(dcn3_03_soc.max_avg_sdp_bw_use_normal_percent / 100));

	if (optimal_dcfclk)
		*optimal_dcfclk =  bw_from_dram /
		(dcn3_03_soc.return_bus_width_bytes * (dcn3_03_soc.max_avg_sdp_bw_use_normal_percent / 100));
}


void dcn303_fpu_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	unsigned int i, j;
	unsigned int num_states = 0;

	unsigned int dcfclk_mhz[DC__VOLTAGE_STATES] = {0};
	unsigned int dram_speed_mts[DC__VOLTAGE_STATES] = {0};
	unsigned int optimal_uclk_for_dcfclk_sta_targets[DC__VOLTAGE_STATES] = {0};
	unsigned int optimal_dcfclk_for_uclk[DC__VOLTAGE_STATES] = {0};

	unsigned int dcfclk_sta_targets[DC__VOLTAGE_STATES] = {694, 875, 1000, 1200};
	unsigned int num_dcfclk_sta_targets = 4;
	unsigned int num_uclk_states;

	dc_assert_fp_enabled();

	if (dc->ctx->dc_bios->vram_info.num_chans)
		dcn3_03_soc.num_chans = dc->ctx->dc_bios->vram_info.num_chans;

	if (dc->ctx->dc_bios->vram_info.dram_channel_width_bytes)
		dcn3_03_soc.dram_channel_width_bytes = dc->ctx->dc_bios->vram_info.dram_channel_width_bytes;

	dcn3_03_soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	dc->dml.soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;

	if (bw_params->clk_table.entries[0].memclk_mhz) {
		int max_dcfclk_mhz = 0, max_dispclk_mhz = 0, max_dppclk_mhz = 0, max_phyclk_mhz = 0;

		for (i = 0; i < MAX_NUM_DPM_LVL; i++) {
			if (bw_params->clk_table.entries[i].dcfclk_mhz > max_dcfclk_mhz)
				max_dcfclk_mhz = bw_params->clk_table.entries[i].dcfclk_mhz;
			if (bw_params->clk_table.entries[i].dispclk_mhz > max_dispclk_mhz)
				max_dispclk_mhz = bw_params->clk_table.entries[i].dispclk_mhz;
			if (bw_params->clk_table.entries[i].dppclk_mhz > max_dppclk_mhz)
				max_dppclk_mhz = bw_params->clk_table.entries[i].dppclk_mhz;
			if (bw_params->clk_table.entries[i].phyclk_mhz > max_phyclk_mhz)
				max_phyclk_mhz = bw_params->clk_table.entries[i].phyclk_mhz;
		}
		if (!max_dcfclk_mhz)
			max_dcfclk_mhz = dcn3_03_soc.clock_limits[0].dcfclk_mhz;
		if (!max_dispclk_mhz)
			max_dispclk_mhz = dcn3_03_soc.clock_limits[0].dispclk_mhz;
		if (!max_dppclk_mhz)
			max_dppclk_mhz = dcn3_03_soc.clock_limits[0].dppclk_mhz;
		if (!max_phyclk_mhz)
			max_phyclk_mhz = dcn3_03_soc.clock_limits[0].phyclk_mhz;

		if (max_dcfclk_mhz > dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			dcfclk_sta_targets[num_dcfclk_sta_targets] = max_dcfclk_mhz;
			num_dcfclk_sta_targets++;
		} else if (max_dcfclk_mhz < dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			for (i = 0; i < num_dcfclk_sta_targets; i++) {
				if (dcfclk_sta_targets[i] > max_dcfclk_mhz) {
					dcfclk_sta_targets[i] = max_dcfclk_mhz;
					break;
				}
			}
			/* Update size of array since we "removed" duplicates */
			num_dcfclk_sta_targets = i + 1;
		}

		num_uclk_states = bw_params->clk_table.num_entries;

		/* Calculate optimal dcfclk for each uclk */
		for (i = 0; i < num_uclk_states; i++) {
			dcn303_get_optimal_dcfclk_fclk_for_uclk(bw_params->clk_table.entries[i].memclk_mhz * 16,
					&optimal_dcfclk_for_uclk[i], NULL);
			if (optimal_dcfclk_for_uclk[i] < bw_params->clk_table.entries[0].dcfclk_mhz)
				optimal_dcfclk_for_uclk[i] = bw_params->clk_table.entries[0].dcfclk_mhz;
		}

		/* Calculate optimal uclk for each dcfclk sta target */
		for (i = 0; i < num_dcfclk_sta_targets; i++) {
			for (j = 0; j < num_uclk_states; j++) {
				if (dcfclk_sta_targets[i] < optimal_dcfclk_for_uclk[j]) {
					optimal_uclk_for_dcfclk_sta_targets[i] =
							bw_params->clk_table.entries[j].memclk_mhz * 16;
					break;
				}
			}
		}

		i = 0;
		j = 0;
		/* create the final dcfclk and uclk table */
		while (i < num_dcfclk_sta_targets && j < num_uclk_states && num_states < DC__VOLTAGE_STATES) {
			if (dcfclk_sta_targets[i] < optimal_dcfclk_for_uclk[j] && i < num_dcfclk_sta_targets) {
				dcfclk_mhz[num_states] = dcfclk_sta_targets[i];
				dram_speed_mts[num_states++] = optimal_uclk_for_dcfclk_sta_targets[i++];
			} else {
				if (j < num_uclk_states && optimal_dcfclk_for_uclk[j] <= max_dcfclk_mhz) {
					dcfclk_mhz[num_states] = optimal_dcfclk_for_uclk[j];
					dram_speed_mts[num_states++] =
							bw_params->clk_table.entries[j++].memclk_mhz * 16;
				} else {
					j = num_uclk_states;
				}
			}
		}

		while (i < num_dcfclk_sta_targets && num_states < DC__VOLTAGE_STATES) {
			dcfclk_mhz[num_states] = dcfclk_sta_targets[i];
			dram_speed_mts[num_states++] = optimal_uclk_for_dcfclk_sta_targets[i++];
		}

		while (j < num_uclk_states && num_states < DC__VOLTAGE_STATES &&
				optimal_dcfclk_for_uclk[j] <= max_dcfclk_mhz) {
			dcfclk_mhz[num_states] = optimal_dcfclk_for_uclk[j];
			dram_speed_mts[num_states++] = bw_params->clk_table.entries[j++].memclk_mhz * 16;
		}

		dcn3_03_soc.num_states = num_states;
		for (i = 0; i < dcn3_03_soc.num_states; i++) {
			dcn3_03_soc.clock_limits[i].state = i;
			dcn3_03_soc.clock_limits[i].dcfclk_mhz = dcfclk_mhz[i];
			dcn3_03_soc.clock_limits[i].fabricclk_mhz = dcfclk_mhz[i];
			dcn3_03_soc.clock_limits[i].dram_speed_mts = dram_speed_mts[i];

			/* Fill all states with max values of all other clocks */
			dcn3_03_soc.clock_limits[i].dispclk_mhz = max_dispclk_mhz;
			dcn3_03_soc.clock_limits[i].dppclk_mhz  = max_dppclk_mhz;
			dcn3_03_soc.clock_limits[i].phyclk_mhz  = max_phyclk_mhz;
			/* Populate from bw_params for DTBCLK, SOCCLK */
			if (!bw_params->clk_table.entries[i].dtbclk_mhz && i > 0)
				dcn3_03_soc.clock_limits[i].dtbclk_mhz = dcn3_03_soc.clock_limits[i-1].dtbclk_mhz;
			else
				dcn3_03_soc.clock_limits[i].dtbclk_mhz = bw_params->clk_table.entries[i].dtbclk_mhz;
			if (!bw_params->clk_table.entries[i].socclk_mhz && i > 0)
				dcn3_03_soc.clock_limits[i].socclk_mhz = dcn3_03_soc.clock_limits[i-1].socclk_mhz;
			else
				dcn3_03_soc.clock_limits[i].socclk_mhz = bw_params->clk_table.entries[i].socclk_mhz;
			/* These clocks cannot come from bw_params, always fill from dcn3_03_soc[1] */
			/* FCLK, PHYCLK_D18, DSCCLK */
			dcn3_03_soc.clock_limits[i].phyclk_d18_mhz = dcn3_03_soc.clock_limits[0].phyclk_d18_mhz;
			dcn3_03_soc.clock_limits[i].dscclk_mhz = dcn3_03_soc.clock_limits[0].dscclk_mhz;
		}

		if (dcn3_03_soc.num_chans <= 4) {
			for (i = 0; i < dcn3_03_soc.num_states; i++) {
				if (dcn3_03_soc.clock_limits[i].dram_speed_mts > 1700)
					break;

				if (dcn3_03_soc.clock_limits[i].dram_speed_mts >= 1500) {
					dcn3_03_soc.clock_limits[i].dcfclk_mhz = 100;
					dcn3_03_soc.clock_limits[i].fabricclk_mhz = 100;
				}
			}
		}

		/* re-init DML with updated bb */
		dml_init_instance(&dc->dml, &dcn3_03_soc, &dcn3_03_ip, DML_PROJECT_DCN30);
		if (dc->current_state)
			dml_init_instance(&dc->current_state->bw_ctx.dml, &dcn3_03_soc, &dcn3_03_ip, DML_PROJECT_DCN30);
	}
}

void dcn303_fpu_init_soc_bounding_box(struct bp_soc_bb_info bb_info)
{
	dc_assert_fp_enabled();

	if (bb_info.dram_clock_change_latency_100ns > 0)
		dcn3_03_soc.dram_clock_change_latency_us = bb_info.dram_clock_change_latency_100ns * 10;

	if (bb_info.dram_sr_enter_exit_latency_100ns > 0)
		dcn3_03_soc.sr_enter_plus_exit_time_us = bb_info.dram_sr_enter_exit_latency_100ns * 10;

	if (bb_info.dram_sr_exit_latency_100ns > 0)
		dcn3_03_soc.sr_exit_time_us = bb_info.dram_sr_exit_latency_100ns * 10;
}
