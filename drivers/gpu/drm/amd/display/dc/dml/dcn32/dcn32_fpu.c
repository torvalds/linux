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
#include "dcn32_fpu.h"
#include "display_mode_vba_util_32.h"
// We need this includes for WATERMARKS_* defines
#include "clk_mgr/dcn32/dcn32_smu13_driver_if.h"

struct _vcs_dpi_ip_params_st dcn3_2_ip = {
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

struct _vcs_dpi_soc_bounding_box_st dcn3_2_soc = {
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
			.dram_speed_mts = 16000.0,
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

void dcn32_build_wm_range_table_fpu(struct clk_mgr_internal *clk_mgr)
{
	/* defaults */
	double pstate_latency_us = clk_mgr->base.ctx->dc->dml.soc.dram_clock_change_latency_us;
	double fclk_change_latency_us = clk_mgr->base.ctx->dc->dml.soc.fclk_change_latency_us;
	double sr_exit_time_us = clk_mgr->base.ctx->dc->dml.soc.sr_exit_time_us;
	double sr_enter_plus_exit_time_us = clk_mgr->base.ctx->dc->dml.soc.sr_enter_plus_exit_time_us;
	/* For min clocks use as reported by PM FW and report those as min */
	uint16_t min_uclk_mhz			= clk_mgr->base.bw_params->clk_table.entries[0].memclk_mhz;
	uint16_t min_dcfclk_mhz			= clk_mgr->base.bw_params->clk_table.entries[0].dcfclk_mhz;
	uint16_t setb_min_uclk_mhz		= min_uclk_mhz;
	uint16_t dcfclk_mhz_for_the_second_state = clk_mgr->base.ctx->dc->dml.soc.clock_limits[2].dcfclk_mhz;

	dc_assert_fp_enabled();

	/* For Set B ranges use min clocks state 2 when available, and report those to PM FW */
	if (dcfclk_mhz_for_the_second_state)
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_dcfclk = dcfclk_mhz_for_the_second_state;
	else
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_dcfclk = clk_mgr->base.bw_params->clk_table.entries[0].dcfclk_mhz;

	if (clk_mgr->base.bw_params->clk_table.entries[2].memclk_mhz)
		setb_min_uclk_mhz = clk_mgr->base.bw_params->clk_table.entries[2].memclk_mhz;

	/* Set A - Normal - default values */
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us = pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.sr_exit_time_us = sr_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set B - Performance - higher clocks, using DPM[2] DCFCLK and UCLK */
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us = pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us = sr_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_uclk = setb_min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set C - Dummy P-State - P-State latency set to "dummy p-state" value */
	/* 'DalDummyClockChangeLatencyNs' registry key option set to 0x7FFFFFFF can be used to disable Set C for dummy p-state */
	if (clk_mgr->base.ctx->dc->bb_overrides.dummy_clock_change_latency_ns != 0x7FFFFFFF) {
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].valid = true;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.pstate_latency_us = 38;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.fclk_change_latency_us = fclk_change_latency_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us = sr_exit_time_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.wm_type = WATERMARKS_DUMMY_PSTATE;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_dcfclk = 0xFFFF;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_uclk = min_uclk_mhz;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_uclk = 0xFFFF;
		clk_mgr->base.bw_params->dummy_pstate_table[0].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[0].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[0].dummy_pstate_latency_us = 38;
		clk_mgr->base.bw_params->dummy_pstate_table[1].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[1].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[1].dummy_pstate_latency_us = 9;
		clk_mgr->base.bw_params->dummy_pstate_table[2].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[2].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[2].dummy_pstate_latency_us = 8;
		clk_mgr->base.bw_params->dummy_pstate_table[3].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[3].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[3].dummy_pstate_latency_us = 5;
	}
	/* Set D - MALL - SR enter and exit time specific to MALL, TBD after bringup or later phase for now use DRAM values / 2 */
	/* For MALL DRAM clock change latency is N/A, for watermak calculations use lowest value dummy P state latency */
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us = clk_mgr->base.bw_params->dummy_pstate_table[3].dummy_pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us = sr_exit_time_us / 2; // TBD
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us / 2; // TBD
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.wm_type = WATERMARKS_MALL;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_uclk = 0xFFFF;
}

/**
 * dcn32_helper_populate_phantom_dlg_params - Get DLG params for phantom pipes
 * and populate pipe_ctx with those params.
 *
 * This function must be called AFTER the phantom pipes are added to context
 * and run through DML (so that the DLG params for the phantom pipes can be
 * populated), and BEFORE we program the timing for the phantom pipes.
 *
 * @dc: [in] current dc state
 * @context: [in] new dc state
 * @pipes: [in] DML pipe params array
 * @pipe_cnt: [in] DML pipe count
 */
void dcn32_helper_populate_phantom_dlg_params(struct dc *dc,
					      struct dc_state *context,
					      display_e2e_pipe_params_st *pipes,
					      int pipe_cnt)
{
	uint32_t i, pipe_idx;

	dc_assert_fp_enabled();

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (pipe->plane_state && pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			pipes[pipe_idx].pipe.dest.vstartup_start =
				get_vstartup(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
			pipes[pipe_idx].pipe.dest.vupdate_offset =
				get_vupdate_offset(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
			pipes[pipe_idx].pipe.dest.vupdate_width =
				get_vupdate_width(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
			pipes[pipe_idx].pipe.dest.vready_offset =
				get_vready_offset(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
			pipe->pipe_dlg_param = pipes[pipe_idx].pipe.dest;
		}
		pipe_idx++;
	}
}

bool dcn32_predict_pipe_split(struct dc_state *context, display_pipe_params_st pipe, int index)
{
	double pscl_throughput;
	double pscl_throughput_chroma;
	double dpp_clk_single_dpp, clock;
	double clk_frequency = 0.0;
	double vco_speed = context->bw_ctx.dml.soc.dispclk_dppclk_vco_speed_mhz;

	dc_assert_fp_enabled();

	dml32_CalculateSinglePipeDPPCLKAndSCLThroughput(pipe.scale_ratio_depth.hscl_ratio,
							pipe.scale_ratio_depth.hscl_ratio_c,
							pipe.scale_ratio_depth.vscl_ratio,
							pipe.scale_ratio_depth.vscl_ratio_c,
							context->bw_ctx.dml.ip.max_dchub_pscl_bw_pix_per_clk,
							context->bw_ctx.dml.ip.max_pscl_lb_bw_pix_per_clk,
							pipe.dest.pixel_rate_mhz,
							pipe.src.source_format,
							pipe.scale_taps.htaps,
							pipe.scale_taps.htaps_c,
							pipe.scale_taps.vtaps,
							pipe.scale_taps.vtaps_c,
							/* Output */
							&pscl_throughput, &pscl_throughput_chroma,
							&dpp_clk_single_dpp);

	clock = dpp_clk_single_dpp * (1 + context->bw_ctx.dml.soc.dcn_downspread_percent / 100);

	if (clock > 0)
		clk_frequency = vco_speed * 4.0 / ((int)(vco_speed * 4.0));

	if (clk_frequency > context->bw_ctx.dml.soc.clock_limits[index].dppclk_mhz)
		return true;
	else
		return false;
}

static float calculate_net_bw_in_kbytes_sec(struct _vcs_dpi_voltage_scaling_st *entry)
{
	float memory_bw_kbytes_sec;
	float fabric_bw_kbytes_sec;
	float sdp_bw_kbytes_sec;
	float limiting_bw_kbytes_sec;

	memory_bw_kbytes_sec = entry->dram_speed_mts *
				dcn3_2_soc.num_chans *
				dcn3_2_soc.dram_channel_width_bytes *
				((float)dcn3_2_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only / 100);

	fabric_bw_kbytes_sec = entry->fabricclk_mhz *
				dcn3_2_soc.return_bus_width_bytes *
				((float)dcn3_2_soc.pct_ideal_fabric_bw_after_urgent / 100);

	sdp_bw_kbytes_sec = entry->dcfclk_mhz *
				dcn3_2_soc.return_bus_width_bytes *
				((float)dcn3_2_soc.pct_ideal_sdp_bw_after_urgent / 100);

	limiting_bw_kbytes_sec = memory_bw_kbytes_sec;

	if (fabric_bw_kbytes_sec < limiting_bw_kbytes_sec)
		limiting_bw_kbytes_sec = fabric_bw_kbytes_sec;

	if (sdp_bw_kbytes_sec < limiting_bw_kbytes_sec)
		limiting_bw_kbytes_sec = sdp_bw_kbytes_sec;

	return limiting_bw_kbytes_sec;
}

void insert_entry_into_table_sorted(struct _vcs_dpi_voltage_scaling_st *table,
				    unsigned int *num_entries,
				    struct _vcs_dpi_voltage_scaling_st *entry)
{
	int i = 0;
	int index = 0;
	float net_bw_of_new_state = 0;

	dc_assert_fp_enabled();

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

/**
 * dcn32_set_phantom_stream_timing: Set timing params for the phantom stream
 *
 * Set timing params of the phantom stream based on calculated output from DML.
 * This function first gets the DML pipe index using the DC pipe index, then
 * calls into DML (get_subviewport_lines_needed_in_mall) to get the number of
 * lines required for SubVP MCLK switching and assigns to the phantom stream
 * accordingly.
 *
 * - The number of SubVP lines calculated in DML does not take into account
 * FW processing delays and required pstate allow width, so we must include
 * that separately.
 *
 * - Set phantom backporch = vstartup of main pipe
 *
 * @dc: current dc state
 * @context: new dc state
 * @ref_pipe: Main pipe for the phantom stream
 * @pipes: DML pipe params
 * @pipe_cnt: number of DML pipes
 * @dc_pipe_idx: DC pipe index for the main pipe (i.e. ref_pipe)
 */
void dcn32_set_phantom_stream_timing(struct dc *dc,
				     struct dc_state *context,
				     struct pipe_ctx *ref_pipe,
				     struct dc_stream_state *phantom_stream,
				     display_e2e_pipe_params_st *pipes,
				     unsigned int pipe_cnt,
				     unsigned int dc_pipe_idx)
{
	unsigned int i, pipe_idx;
	struct pipe_ctx *pipe;
	uint32_t phantom_vactive, phantom_bp, pstate_width_fw_delay_lines;
	unsigned int vlevel = context->bw_ctx.dml.vba.VoltageLevel;
	unsigned int dcfclk = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
	unsigned int socclk = context->bw_ctx.dml.vba.SOCCLKPerState[vlevel];

	dc_assert_fp_enabled();

	// Find DML pipe index (pipe_idx) using dc_pipe_idx
	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (i == dc_pipe_idx)
			break;

		pipe_idx++;
	}

	// Calculate lines required for pstate allow width and FW processing delays
	pstate_width_fw_delay_lines = ((double)(dc->caps.subvp_fw_processing_delay_us +
			dc->caps.subvp_pstate_allow_width_us) / 1000000) *
			(ref_pipe->stream->timing.pix_clk_100hz * 100) /
			(double)ref_pipe->stream->timing.h_total;

	// Update clks_cfg for calling into recalculate
	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = socclk;

	// DML calculation for MALL region doesn't take into account FW delay
	// and required pstate allow width for multi-display cases
	phantom_vactive = get_subviewport_lines_needed_in_mall(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx) +
				pstate_width_fw_delay_lines;

	// For backporch of phantom pipe, use vstartup of the main pipe
	phantom_bp = get_vstartup(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);

	phantom_stream->dst.y = 0;
	phantom_stream->dst.height = phantom_vactive;
	phantom_stream->src.y = 0;
	phantom_stream->src.height = phantom_vactive;

	phantom_stream->timing.v_addressable = phantom_vactive;
	phantom_stream->timing.v_front_porch = 1;
	phantom_stream->timing.v_total = phantom_stream->timing.v_addressable +
						phantom_stream->timing.v_front_porch +
						phantom_stream->timing.v_sync_width +
						phantom_bp;
}

