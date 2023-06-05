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
#include "dcn321_fpu.h"
#include "dcn32/dcn32_resource.h"
#include "dcn321/dcn321_resource.h"
#include "dml/dcn32/display_mode_vba_util_32.h"

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
			.dcfclk_mhz = 1434.0,
			.fabricclk_mhz = 2250.0,
			.dispclk_mhz = 1720.0,
			.dppclk_mhz = 1720.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.phyclk_d32_mhz = 313.0,
			.socclk_mhz = 1200.0,
			.dscclk_mhz = 573.333,
			.dram_speed_mts = 16000.0,
			.dtbclk_mhz = 1564.0,
		},
	},
	.num_states = 1,
	.sr_exit_time_us = 19.95,
	.sr_enter_plus_exit_time_us = 24.36,
	.sr_exit_z8_time_us = 285.0,
	.sr_enter_plus_exit_z8_time_us = 320,
	.writeback_latency_us = 12.0,
	.round_trip_ping_latency_dcfclk_cycles = 207,
	.urgent_latency_pixel_data_only_us = 4,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4,
	.urgent_latency_vm_data_only_us = 4,
	.fclk_change_latency_us = 7,
	.usr_retraining_latency_us = 0,
	.smn_latency_us = 0,
	.mall_allocated_for_dcn_mbytes = 32,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_sdp_bw_after_urgent = 90.0,
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
	.urgent_latency_adjustment_fabric_clock_reference_mhz = 3000,
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

static void dcn321_insert_entry_into_table_sorted(struct _vcs_dpi_voltage_scaling_st *table,
					   unsigned int *num_entries,
					   struct _vcs_dpi_voltage_scaling_st *entry)
{
	int i = 0;
	int index = 0;

	dc_assert_fp_enabled();

	if (*num_entries == 0) {
		table[0] = *entry;
		(*num_entries)++;
	} else {
		while (entry->net_bw_in_kbytes_sec > table[index].net_bw_in_kbytes_sec) {
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

static void remove_entry_from_table_at_index(struct _vcs_dpi_voltage_scaling_st *table, unsigned int *num_entries,
		unsigned int index)
{
	int i;

	if (*num_entries == 0)
		return;

	for (i = index; i < *num_entries - 1; i++) {
		table[i] = table[i + 1];
	}
	memset(&table[--(*num_entries)], 0, sizeof(struct _vcs_dpi_voltage_scaling_st));
}

static void swap_table_entries(struct _vcs_dpi_voltage_scaling_st *first_entry,
		struct _vcs_dpi_voltage_scaling_st *second_entry)
{
	struct _vcs_dpi_voltage_scaling_st temp_entry = *first_entry;
	*first_entry = *second_entry;
	*second_entry = temp_entry;
}

/*
 * sort_entries_with_same_bw - Sort entries sharing the same bandwidth by DCFCLK
 */
static void sort_entries_with_same_bw(struct _vcs_dpi_voltage_scaling_st *table, unsigned int *num_entries)
{
	unsigned int start_index = 0;
	unsigned int end_index = 0;
	unsigned int current_bw = 0;

	for (int i = 0; i < (*num_entries - 1); i++) {
		if (table[i].net_bw_in_kbytes_sec == table[i+1].net_bw_in_kbytes_sec) {
			current_bw = table[i].net_bw_in_kbytes_sec;
			start_index = i;
			end_index = ++i;

			while ((i < (*num_entries - 1)) && (table[i+1].net_bw_in_kbytes_sec == current_bw))
				end_index = ++i;
		}

		if (start_index != end_index) {
			for (int j = start_index; j < end_index; j++) {
				for (int k = start_index; k < end_index; k++) {
					if (table[k].dcfclk_mhz > table[k+1].dcfclk_mhz)
						swap_table_entries(&table[k], &table[k+1]);
				}
			}
		}

		start_index = 0;
		end_index = 0;

	}
}

/*
 * remove_inconsistent_entries - Ensure entries with the same bandwidth have MEMCLK and FCLK monotonically increasing
 *                               and remove entries that do not follow this order
 */
static void remove_inconsistent_entries(struct _vcs_dpi_voltage_scaling_st *table, unsigned int *num_entries)
{
	for (int i = 0; i < (*num_entries - 1); i++) {
		if (table[i].net_bw_in_kbytes_sec == table[i+1].net_bw_in_kbytes_sec) {
			if ((table[i].dram_speed_mts > table[i+1].dram_speed_mts) ||
				(table[i].fabricclk_mhz > table[i+1].fabricclk_mhz))
				remove_entry_from_table_at_index(table, num_entries, i);
		}
	}
}

/*
 * override_max_clk_values - Overwrite the max clock frequencies with the max DC mode timings
 * Input:
 *	max_clk_limit - struct containing the desired clock timings
 * Output:
 *	curr_clk_limit  - struct containing the timings that need to be overwritten
 * Return: 0 upon success, non-zero for failure
 */
static int override_max_clk_values(struct clk_limit_table_entry *max_clk_limit,
		struct clk_limit_table_entry *curr_clk_limit)
{
	if (NULL == max_clk_limit || NULL == curr_clk_limit)
		return -1; //invalid parameters

	//only overwrite if desired max clock frequency is initialized
	if (max_clk_limit->dcfclk_mhz != 0)
		curr_clk_limit->dcfclk_mhz = max_clk_limit->dcfclk_mhz;

	if (max_clk_limit->fclk_mhz != 0)
		curr_clk_limit->fclk_mhz = max_clk_limit->fclk_mhz;

	if (max_clk_limit->memclk_mhz != 0)
		curr_clk_limit->memclk_mhz = max_clk_limit->memclk_mhz;

	if (max_clk_limit->socclk_mhz != 0)
		curr_clk_limit->socclk_mhz = max_clk_limit->socclk_mhz;

	if (max_clk_limit->dtbclk_mhz != 0)
		curr_clk_limit->dtbclk_mhz = max_clk_limit->dtbclk_mhz;

	if (max_clk_limit->dispclk_mhz != 0)
		curr_clk_limit->dispclk_mhz = max_clk_limit->dispclk_mhz;

	return 0;
}

static int build_synthetic_soc_states(bool disable_dc_mode_overwrite, struct clk_bw_params *bw_params,
		struct _vcs_dpi_voltage_scaling_st *table, unsigned int *num_entries)
{
	int i, j;
	struct _vcs_dpi_voltage_scaling_st entry = {0};
	struct clk_limit_table_entry max_clk_data = {0};

	unsigned int min_dcfclk_mhz = 199, min_fclk_mhz = 299;

	static const unsigned int num_dcfclk_stas = 5;
	unsigned int dcfclk_sta_targets[DC__VOLTAGE_STATES] = {199, 615, 906, 1324, 1564};

	unsigned int num_uclk_dpms = 0;
	unsigned int num_fclk_dpms = 0;
	unsigned int num_dcfclk_dpms = 0;

	unsigned int num_dc_uclk_dpms = 0;
	unsigned int num_dc_fclk_dpms = 0;
	unsigned int num_dc_dcfclk_dpms = 0;

	for (i = 0; i < MAX_NUM_DPM_LVL; i++) {
		if (bw_params->clk_table.entries[i].dcfclk_mhz > max_clk_data.dcfclk_mhz)
			max_clk_data.dcfclk_mhz = bw_params->clk_table.entries[i].dcfclk_mhz;
		if (bw_params->clk_table.entries[i].fclk_mhz > max_clk_data.fclk_mhz)
			max_clk_data.fclk_mhz = bw_params->clk_table.entries[i].fclk_mhz;
		if (bw_params->clk_table.entries[i].memclk_mhz > max_clk_data.memclk_mhz)
			max_clk_data.memclk_mhz = bw_params->clk_table.entries[i].memclk_mhz;
		if (bw_params->clk_table.entries[i].dispclk_mhz > max_clk_data.dispclk_mhz)
			max_clk_data.dispclk_mhz = bw_params->clk_table.entries[i].dispclk_mhz;
		if (bw_params->clk_table.entries[i].dppclk_mhz > max_clk_data.dppclk_mhz)
			max_clk_data.dppclk_mhz = bw_params->clk_table.entries[i].dppclk_mhz;
		if (bw_params->clk_table.entries[i].phyclk_mhz > max_clk_data.phyclk_mhz)
			max_clk_data.phyclk_mhz = bw_params->clk_table.entries[i].phyclk_mhz;
		if (bw_params->clk_table.entries[i].dtbclk_mhz > max_clk_data.dtbclk_mhz)
			max_clk_data.dtbclk_mhz = bw_params->clk_table.entries[i].dtbclk_mhz;

		if (bw_params->clk_table.entries[i].memclk_mhz > 0) {
			num_uclk_dpms++;
			if (bw_params->clk_table.entries[i].memclk_mhz <= bw_params->dc_mode_limit.memclk_mhz)
				num_dc_uclk_dpms++;
		}
		if (bw_params->clk_table.entries[i].fclk_mhz > 0) {
			num_fclk_dpms++;
			if (bw_params->clk_table.entries[i].fclk_mhz <= bw_params->dc_mode_limit.fclk_mhz)
				num_dc_fclk_dpms++;
		}
		if (bw_params->clk_table.entries[i].dcfclk_mhz > 0) {
			num_dcfclk_dpms++;
			if (bw_params->clk_table.entries[i].dcfclk_mhz <= bw_params->dc_mode_limit.dcfclk_mhz)
				num_dc_dcfclk_dpms++;
		}
	}

	if (!disable_dc_mode_overwrite) {
		//Overwrite max frequencies with max DC mode frequencies for DC mode systems
		override_max_clk_values(&bw_params->dc_mode_limit, &max_clk_data);
		num_uclk_dpms = num_dc_uclk_dpms;
		num_fclk_dpms = num_dc_fclk_dpms;
		num_dcfclk_dpms = num_dc_dcfclk_dpms;
		bw_params->clk_table.num_entries_per_clk.num_memclk_levels = num_uclk_dpms;
		bw_params->clk_table.num_entries_per_clk.num_fclk_levels = num_fclk_dpms;
	}

	if (num_dcfclk_dpms > 0 && bw_params->clk_table.entries[0].fclk_mhz > min_fclk_mhz)
		min_fclk_mhz = bw_params->clk_table.entries[0].fclk_mhz;

	if (!max_clk_data.dcfclk_mhz || !max_clk_data.dispclk_mhz || !max_clk_data.dtbclk_mhz)
		return -1;

	if (max_clk_data.dppclk_mhz == 0)
		max_clk_data.dppclk_mhz = max_clk_data.dispclk_mhz;

	if (max_clk_data.fclk_mhz == 0)
		max_clk_data.fclk_mhz = max_clk_data.dcfclk_mhz *
				dcn3_2_soc.pct_ideal_sdp_bw_after_urgent /
				dcn3_2_soc.pct_ideal_fabric_bw_after_urgent;

	if (max_clk_data.phyclk_mhz == 0)
		max_clk_data.phyclk_mhz = dcn3_2_soc.clock_limits[0].phyclk_mhz;

	*num_entries = 0;
	entry.dispclk_mhz = max_clk_data.dispclk_mhz;
	entry.dscclk_mhz = max_clk_data.dispclk_mhz / 3;
	entry.dppclk_mhz = max_clk_data.dppclk_mhz;
	entry.dtbclk_mhz = max_clk_data.dtbclk_mhz;
	entry.phyclk_mhz = max_clk_data.phyclk_mhz;
	entry.phyclk_d18_mhz = dcn3_2_soc.clock_limits[0].phyclk_d18_mhz;
	entry.phyclk_d32_mhz = dcn3_2_soc.clock_limits[0].phyclk_d32_mhz;

	// Insert all the DCFCLK STAs
	for (i = 0; i < num_dcfclk_stas; i++) {
		entry.dcfclk_mhz = dcfclk_sta_targets[i];
		entry.fabricclk_mhz = 0;
		entry.dram_speed_mts = 0;

		get_optimal_ntuple(&entry);
		entry.net_bw_in_kbytes_sec = calculate_net_bw_in_kbytes_sec(&entry);
		dcn321_insert_entry_into_table_sorted(table, num_entries, &entry);
	}

	// Insert the max DCFCLK
	entry.dcfclk_mhz = max_clk_data.dcfclk_mhz;
	entry.fabricclk_mhz = 0;
	entry.dram_speed_mts = 0;

	get_optimal_ntuple(&entry);
	entry.net_bw_in_kbytes_sec = calculate_net_bw_in_kbytes_sec(&entry);
	dcn321_insert_entry_into_table_sorted(table, num_entries, &entry);

	// Insert the UCLK DPMS
	for (i = 0; i < num_uclk_dpms; i++) {
		entry.dcfclk_mhz = 0;
		entry.fabricclk_mhz = 0;
		entry.dram_speed_mts = bw_params->clk_table.entries[i].memclk_mhz * 16;

		get_optimal_ntuple(&entry);
		entry.net_bw_in_kbytes_sec = calculate_net_bw_in_kbytes_sec(&entry);
		dcn321_insert_entry_into_table_sorted(table, num_entries, &entry);
	}

	// If FCLK is coarse grained, insert individual DPMs.
	if (num_fclk_dpms > 2) {
		for (i = 0; i < num_fclk_dpms; i++) {
			entry.dcfclk_mhz = 0;
			entry.fabricclk_mhz = bw_params->clk_table.entries[i].fclk_mhz;
			entry.dram_speed_mts = 0;

			get_optimal_ntuple(&entry);
			entry.net_bw_in_kbytes_sec = calculate_net_bw_in_kbytes_sec(&entry);
			dcn321_insert_entry_into_table_sorted(table, num_entries, &entry);
		}
	}
	// If FCLK fine grained, only insert max
	else {
		entry.dcfclk_mhz = 0;
		entry.fabricclk_mhz = max_clk_data.fclk_mhz;
		entry.dram_speed_mts = 0;

		get_optimal_ntuple(&entry);
		entry.net_bw_in_kbytes_sec = calculate_net_bw_in_kbytes_sec(&entry);
		dcn321_insert_entry_into_table_sorted(table, num_entries, &entry);
	}

	// At this point, the table contains all "points of interest" based on
	// DPMs from PMFW, and STAs.  Table is sorted by BW, and all clock
	// ratios (by derate, are exact).

	// Remove states that require higher clocks than are supported
	for (i = *num_entries - 1; i >= 0 ; i--) {
		if (table[i].dcfclk_mhz > max_clk_data.dcfclk_mhz ||
				table[i].fabricclk_mhz > max_clk_data.fclk_mhz ||
				table[i].dram_speed_mts > max_clk_data.memclk_mhz * 16)
			remove_entry_from_table_at_index(table, num_entries, i);
	}

	// Insert entry with all max dc limits without bandwitch matching
	if (!disable_dc_mode_overwrite) {
		struct _vcs_dpi_voltage_scaling_st max_dc_limits_entry = entry;

		max_dc_limits_entry.dcfclk_mhz = max_clk_data.dcfclk_mhz;
		max_dc_limits_entry.fabricclk_mhz = max_clk_data.fclk_mhz;
		max_dc_limits_entry.dram_speed_mts = max_clk_data.memclk_mhz * 16;

		max_dc_limits_entry.net_bw_in_kbytes_sec = calculate_net_bw_in_kbytes_sec(&max_dc_limits_entry);
		dcn321_insert_entry_into_table_sorted(table, num_entries, &max_dc_limits_entry);

		sort_entries_with_same_bw(table, num_entries);
		remove_inconsistent_entries(table, num_entries);
	}



	// At this point, the table only contains supported points of interest
	// it could be used as is, but some states may be redundant due to
	// coarse grained nature of some clocks, so we want to round up to
	// coarse grained DPMs and remove duplicates.

	// Round up UCLKs
	for (i = *num_entries - 1; i >= 0 ; i--) {
		for (j = 0; j < num_uclk_dpms; j++) {
			if (bw_params->clk_table.entries[j].memclk_mhz * 16 >= table[i].dram_speed_mts) {
				table[i].dram_speed_mts = bw_params->clk_table.entries[j].memclk_mhz * 16;
				break;
			}
		}
	}

	// If FCLK is coarse grained, round up to next DPMs
	if (num_fclk_dpms > 2) {
		for (i = *num_entries - 1; i >= 0 ; i--) {
			for (j = 0; j < num_fclk_dpms; j++) {
				if (bw_params->clk_table.entries[j].fclk_mhz >= table[i].fabricclk_mhz) {
					table[i].fabricclk_mhz = bw_params->clk_table.entries[j].fclk_mhz;
					break;
				}
			}
		}
	}
	// Otherwise, round up to minimum.
	else {
		for (i = *num_entries - 1; i >= 0 ; i--) {
			if (table[i].fabricclk_mhz < min_fclk_mhz) {
				table[i].fabricclk_mhz = min_fclk_mhz;
			}
		}
	}

	// Round DCFCLKs up to minimum
	for (i = *num_entries - 1; i >= 0 ; i--) {
		if (table[i].dcfclk_mhz < min_dcfclk_mhz) {
			table[i].dcfclk_mhz = min_dcfclk_mhz;
		}
	}

	// Remove duplicate states, note duplicate states are always neighbouring since table is sorted.
	i = 0;
	while (i < *num_entries - 1) {
		if (table[i].dcfclk_mhz == table[i + 1].dcfclk_mhz &&
				table[i].fabricclk_mhz == table[i + 1].fabricclk_mhz &&
				table[i].dram_speed_mts == table[i + 1].dram_speed_mts)
			remove_entry_from_table_at_index(table, num_entries, i + 1);
		else
			i++;
	}

	// Fix up the state indicies
	for (i = *num_entries - 1; i >= 0 ; i--) {
		table[i].state = i;
	}

	return 0;
}

static void dcn321_get_optimal_dcfclk_fclk_for_uclk(unsigned int uclk_mts,
		unsigned int *optimal_dcfclk,
		unsigned int *optimal_fclk)
{
	double bw_from_dram, bw_from_dram1, bw_from_dram2;

	bw_from_dram1 = uclk_mts * dcn3_21_soc.num_chans *
		dcn3_21_soc.dram_channel_width_bytes * (dcn3_21_soc.max_avg_dram_bw_use_normal_percent / 100);
	bw_from_dram2 = uclk_mts * dcn3_21_soc.num_chans *
		dcn3_21_soc.dram_channel_width_bytes * (dcn3_21_soc.max_avg_sdp_bw_use_normal_percent / 100);

	bw_from_dram = (bw_from_dram1 < bw_from_dram2) ? bw_from_dram1 : bw_from_dram2;

	if (optimal_fclk)
		*optimal_fclk = bw_from_dram /
		(dcn3_21_soc.fabric_datapath_to_dcn_data_return_bytes * (dcn3_21_soc.max_avg_sdp_bw_use_normal_percent / 100));

	if (optimal_dcfclk)
		*optimal_dcfclk =  bw_from_dram /
		(dcn3_21_soc.return_bus_width_bytes * (dcn3_21_soc.max_avg_sdp_bw_use_normal_percent / 100));
}

/** dcn321_update_bw_bounding_box
 * This would override some dcn3_2 ip_or_soc initial parameters hardcoded from spreadsheet
 * with actual values as per dGPU SKU:
 * -with passed few options from dc->config
 * -with dentist_vco_frequency from Clk Mgr (currently hardcoded, but might need to get it from PM FW)
 * -with passed latency values (passed in ns units) in dc-> bb override for debugging purposes
 * -with passed latencies from VBIOS (in 100_ns units) if available for certain dGPU SKU
 * -with number of DRAM channels from VBIOS (which differ for certain dGPU SKU of the same ASIC)
 * -clocks levels with passed clk_table entries from Clk Mgr as reported by PM FW for different
 *  clocks (which might differ for certain dGPU SKU of the same ASIC)
 */
void dcn321_update_bw_bounding_box_fpu(struct dc *dc, struct clk_bw_params *bw_params)
{
	dc_assert_fp_enabled();
	/* Overrides from dc->config options */
	dcn3_21_ip.clamp_min_dcfclk = dc->config.clamp_min_dcfclk;

	/* Override from passed dc->bb_overrides if available*/
	if ((int)(dcn3_21_soc.sr_exit_time_us * 1000) != dc->bb_overrides.sr_exit_time_ns
			&& dc->bb_overrides.sr_exit_time_ns) {
		dcn3_21_soc.sr_exit_time_us = dc->bb_overrides.sr_exit_time_ns / 1000.0;
	}

	if ((int)(dcn3_21_soc.sr_enter_plus_exit_time_us * 1000)
			!= dc->bb_overrides.sr_enter_plus_exit_time_ns
			&& dc->bb_overrides.sr_enter_plus_exit_time_ns) {
		dcn3_21_soc.sr_enter_plus_exit_time_us =
			dc->bb_overrides.sr_enter_plus_exit_time_ns / 1000.0;
	}

	if ((int)(dcn3_21_soc.urgent_latency_us * 1000) != dc->bb_overrides.urgent_latency_ns
		&& dc->bb_overrides.urgent_latency_ns) {
		dcn3_21_soc.urgent_latency_us = dc->bb_overrides.urgent_latency_ns / 1000.0;
		dcn3_21_soc.urgent_latency_pixel_data_only_us = dc->bb_overrides.urgent_latency_ns / 1000.0;
	}

	if ((int)(dcn3_21_soc.dram_clock_change_latency_us * 1000)
			!= dc->bb_overrides.dram_clock_change_latency_ns
			&& dc->bb_overrides.dram_clock_change_latency_ns) {
		dcn3_21_soc.dram_clock_change_latency_us =
			dc->bb_overrides.dram_clock_change_latency_ns / 1000.0;
	}

	if ((int)(dcn3_21_soc.fclk_change_latency_us * 1000)
			!= dc->bb_overrides.fclk_clock_change_latency_ns
			&& dc->bb_overrides.fclk_clock_change_latency_ns) {
		dcn3_21_soc.fclk_change_latency_us =
			dc->bb_overrides.fclk_clock_change_latency_ns / 1000;
	}

	if ((int)(dcn3_21_soc.dummy_pstate_latency_us * 1000)
			!= dc->bb_overrides.dummy_clock_change_latency_ns
			&& dc->bb_overrides.dummy_clock_change_latency_ns) {
		dcn3_21_soc.dummy_pstate_latency_us =
			dc->bb_overrides.dummy_clock_change_latency_ns / 1000.0;
	}

	/* Override from VBIOS if VBIOS bb_info available */
	if (dc->ctx->dc_bios->funcs->get_soc_bb_info) {
		struct bp_soc_bb_info bb_info = {0};

		if (dc->ctx->dc_bios->funcs->get_soc_bb_info(dc->ctx->dc_bios, &bb_info) == BP_RESULT_OK) {
			if (bb_info.dram_clock_change_latency_100ns > 0)
				dcn3_21_soc.dram_clock_change_latency_us =
					bb_info.dram_clock_change_latency_100ns * 10;

			if (bb_info.dram_sr_enter_exit_latency_100ns > 0)
				dcn3_21_soc.sr_enter_plus_exit_time_us =
					bb_info.dram_sr_enter_exit_latency_100ns * 10;

			if (bb_info.dram_sr_exit_latency_100ns > 0)
				dcn3_21_soc.sr_exit_time_us =
					bb_info.dram_sr_exit_latency_100ns * 10;
		}
	}

	/* Override from VBIOS for num_chan */
	if (dc->ctx->dc_bios->vram_info.num_chans) {
		dcn3_21_soc.num_chans = dc->ctx->dc_bios->vram_info.num_chans;
		dcn3_21_soc.mall_allocated_for_dcn_mbytes = (double)(dcn32_calc_num_avail_chans_for_mall(dc,
			dc->ctx->dc_bios->vram_info.num_chans) * dc->caps.mall_size_per_mem_channel);
	}

	if (dc->ctx->dc_bios->vram_info.dram_channel_width_bytes)
		dcn3_21_soc.dram_channel_width_bytes = dc->ctx->dc_bios->vram_info.dram_channel_width_bytes;

	/* DML DSC delay factor workaround */
	dcn3_21_ip.dsc_delay_factor_wa = dc->debug.dsc_delay_factor_wa_x1000 / 1000.0;

	dcn3_21_ip.min_prefetch_in_strobe_us = dc->debug.min_prefetch_in_strobe_ns / 1000.0;

	/* Override dispclk_dppclk_vco_speed_mhz from Clk Mgr */
	dcn3_21_soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	dc->dml.soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;

	/* Overrides Clock levelsfrom CLK Mgr table entries as reported by PM FW */
	if (dc->debug.use_legacy_soc_bb_mechanism) {
		unsigned int i = 0, j = 0, num_states = 0;

		unsigned int dcfclk_mhz[DC__VOLTAGE_STATES] = {0};
		unsigned int dram_speed_mts[DC__VOLTAGE_STATES] = {0};
		unsigned int optimal_uclk_for_dcfclk_sta_targets[DC__VOLTAGE_STATES] = {0};
		unsigned int optimal_dcfclk_for_uclk[DC__VOLTAGE_STATES] = {0};

		unsigned int dcfclk_sta_targets[DC__VOLTAGE_STATES] = {615, 906, 1324, 1564};
		unsigned int num_dcfclk_sta_targets = 4, num_uclk_states = 0;
		unsigned int max_dcfclk_mhz = 0, max_dispclk_mhz = 0, max_dppclk_mhz = 0, max_phyclk_mhz = 0;

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
			max_dcfclk_mhz = dcn3_21_soc.clock_limits[0].dcfclk_mhz;
		if (!max_dispclk_mhz)
			max_dispclk_mhz = dcn3_21_soc.clock_limits[0].dispclk_mhz;
		if (!max_dppclk_mhz)
			max_dppclk_mhz = dcn3_21_soc.clock_limits[0].dppclk_mhz;
		if (!max_phyclk_mhz)
			max_phyclk_mhz = dcn3_21_soc.clock_limits[0].phyclk_mhz;

		if (max_dcfclk_mhz > dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			// If max DCFCLK is greater than the max DCFCLK STA target, insert into the DCFCLK STA target array
			dcfclk_sta_targets[num_dcfclk_sta_targets] = max_dcfclk_mhz;
			num_dcfclk_sta_targets++;
		} else if (max_dcfclk_mhz < dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			// If max DCFCLK is less than the max DCFCLK STA target, cap values and remove duplicates
			for (i = 0; i < num_dcfclk_sta_targets; i++) {
				if (dcfclk_sta_targets[i] > max_dcfclk_mhz) {
					dcfclk_sta_targets[i] = max_dcfclk_mhz;
					break;
				}
			}
			// Update size of array since we "removed" duplicates
			num_dcfclk_sta_targets = i + 1;
		}

		num_uclk_states = bw_params->clk_table.num_entries;

		// Calculate optimal dcfclk for each uclk
		for (i = 0; i < num_uclk_states; i++) {
			dcn321_get_optimal_dcfclk_fclk_for_uclk(bw_params->clk_table.entries[i].memclk_mhz * 16,
					&optimal_dcfclk_for_uclk[i], NULL);
			if (optimal_dcfclk_for_uclk[i] < bw_params->clk_table.entries[0].dcfclk_mhz) {
				optimal_dcfclk_for_uclk[i] = bw_params->clk_table.entries[0].dcfclk_mhz;
			}
		}

		// Calculate optimal uclk for each dcfclk sta target
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
		// create the final dcfclk and uclk table
		while (i < num_dcfclk_sta_targets && j < num_uclk_states && num_states < DC__VOLTAGE_STATES) {
			if (dcfclk_sta_targets[i] < optimal_dcfclk_for_uclk[j] && i < num_dcfclk_sta_targets) {
				dcfclk_mhz[num_states] = dcfclk_sta_targets[i];
				dram_speed_mts[num_states++] = optimal_uclk_for_dcfclk_sta_targets[i++];
			} else {
				if (j < num_uclk_states && optimal_dcfclk_for_uclk[j] <= max_dcfclk_mhz) {
					dcfclk_mhz[num_states] = optimal_dcfclk_for_uclk[j];
					dram_speed_mts[num_states++] = bw_params->clk_table.entries[j++].memclk_mhz * 16;
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

		dcn3_21_soc.num_states = num_states;
		for (i = 0; i < dcn3_21_soc.num_states; i++) {
			dcn3_21_soc.clock_limits[i].state = i;
			dcn3_21_soc.clock_limits[i].dcfclk_mhz = dcfclk_mhz[i];
			dcn3_21_soc.clock_limits[i].fabricclk_mhz = dcfclk_mhz[i];

			/* Fill all states with max values of all these clocks */
			dcn3_21_soc.clock_limits[i].dispclk_mhz = max_dispclk_mhz;
			dcn3_21_soc.clock_limits[i].dppclk_mhz  = max_dppclk_mhz;
			dcn3_21_soc.clock_limits[i].phyclk_mhz  = max_phyclk_mhz;
			dcn3_21_soc.clock_limits[i].dscclk_mhz  = max_dispclk_mhz / 3;

			/* Populate from bw_params for DTBCLK, SOCCLK */
			if (i > 0) {
				if (!bw_params->clk_table.entries[i].dtbclk_mhz) {
					dcn3_21_soc.clock_limits[i].dtbclk_mhz  = dcn3_21_soc.clock_limits[i-1].dtbclk_mhz;
				} else {
					dcn3_21_soc.clock_limits[i].dtbclk_mhz  = bw_params->clk_table.entries[i].dtbclk_mhz;
				}
			} else if (bw_params->clk_table.entries[i].dtbclk_mhz) {
				dcn3_21_soc.clock_limits[i].dtbclk_mhz  = bw_params->clk_table.entries[i].dtbclk_mhz;
			}

			if (!bw_params->clk_table.entries[i].socclk_mhz && i > 0)
				dcn3_21_soc.clock_limits[i].socclk_mhz = dcn3_21_soc.clock_limits[i-1].socclk_mhz;
			else
				dcn3_21_soc.clock_limits[i].socclk_mhz = bw_params->clk_table.entries[i].socclk_mhz;

			if (!dram_speed_mts[i] && i > 0)
				dcn3_21_soc.clock_limits[i].dram_speed_mts = dcn3_21_soc.clock_limits[i-1].dram_speed_mts;
			else
				dcn3_21_soc.clock_limits[i].dram_speed_mts = dram_speed_mts[i];

			/* These clocks cannot come from bw_params, always fill from dcn3_21_soc[0] */
			/* PHYCLK_D18, PHYCLK_D32 */
			dcn3_21_soc.clock_limits[i].phyclk_d18_mhz = dcn3_21_soc.clock_limits[0].phyclk_d18_mhz;
			dcn3_21_soc.clock_limits[i].phyclk_d32_mhz = dcn3_21_soc.clock_limits[0].phyclk_d32_mhz;
		}
	} else {
		build_synthetic_soc_states(dc->debug.disable_dc_mode_overwrite, bw_params,
			dcn3_21_soc.clock_limits, &dcn3_21_soc.num_states);
	}

	/* Re-init DML with updated bb */
	dml_init_instance(&dc->dml, &dcn3_21_soc, &dcn3_21_ip, DML_PROJECT_DCN32);
	if (dc->current_state)
		dml_init_instance(&dc->current_state->bw_ctx.dml, &dcn3_21_soc, &dcn3_21_ip, DML_PROJECT_DCN32);
}

