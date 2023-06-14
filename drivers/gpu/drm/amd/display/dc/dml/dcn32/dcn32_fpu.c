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
#include "dc_link_dp.h"
#include "dcn32/dcn32_resource.h"
#include "dcn20/dcn20_resource.h"
#include "display_mode_vba_util_32.h"
// We need this includes for WATERMARKS_* defines
#include "clk_mgr/dcn32/dcn32_smu13_driver_if.h"
#include "dcn30/dcn30_resource.h"

#define DC_LOGGER_INIT(logger)

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
	.sr_exit_time_us = 42.97,
	.sr_enter_plus_exit_time_us = 49.94,
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
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.pstate_latency_us = 50;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.fclk_change_latency_us = fclk_change_latency_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us = sr_exit_time_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.wm_type = WATERMARKS_DUMMY_PSTATE;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_dcfclk = 0xFFFF;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_uclk = min_uclk_mhz;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_uclk = 0xFFFF;
		clk_mgr->base.bw_params->dummy_pstate_table[0].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[0].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[0].dummy_pstate_latency_us = 50;
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

/*
 * Finds dummy_latency_index when MCLK switching using firmware based
 * vblank stretch is enabled. This function will iterate through the
 * table of dummy pstate latencies until the lowest value that allows
 * dm_allow_self_refresh_and_mclk_switch to happen is found
 */
int dcn32_find_dummy_latency_index_for_fw_based_mclk_switch(struct dc *dc,
							    struct dc_state *context,
							    display_e2e_pipe_params_st *pipes,
							    int pipe_cnt,
							    int vlevel)
{
	const int max_latency_table_entries = 4;
	const struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	int dummy_latency_index = 0;

	dc_assert_fp_enabled();

	while (dummy_latency_index < max_latency_table_entries) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
				dc->clk_mgr->bw_params->dummy_pstate_table[dummy_latency_index].dummy_pstate_latency_us;
		dcn32_internal_validate_bw(dc, context, pipes, &pipe_cnt, &vlevel, false);

		if (vlevel < context->bw_ctx.dml.vba.soc.num_states &&
				vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] != dm_dram_clock_change_unsupported)
			break;

		dummy_latency_index++;
	}

	if (dummy_latency_index == max_latency_table_entries) {
		ASSERT(dummy_latency_index != max_latency_table_entries);
		/* If the execution gets here, it means dummy p_states are
		 * not possible. This should never happen and would mean
		 * something is severely wrong.
		 * Here we reset dummy_latency_index to 3, because it is
		 * better to have underflows than system crashes.
		 */
		dummy_latency_index = max_latency_table_entries - 1;
	}

	return dummy_latency_index;
}

/**
 * dcn32_helper_populate_phantom_dlg_params - Get DLG params for phantom pipes
 * and populate pipe_ctx with those params.
 * @dc: [in] current dc state
 * @context: [in] new dc state
 * @pipes: [in] DML pipe params array
 * @pipe_cnt: [in] DML pipe count
 *
 * This function must be called AFTER the phantom pipes are added to context
 * and run through DML (so that the DLG params for the phantom pipes can be
 * populated), and BEFORE we program the timing for the phantom pipes.
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

/**
 * dcn32_predict_pipe_split - Predict if pipe split will occur for a given DML pipe
 * @context: [in] New DC state to be programmed
 * @pipe_e2e: [in] DML pipe end to end context
 *
 * This function takes in a DML pipe (pipe_e2e) and predicts if pipe split is required (both
 * ODM and MPC). For pipe split, ODM combine is determined by the ODM mode, and MPC combine is
 * determined by DPPClk requirements
 *
 * This function follows the same policy as DML:
 * - Check for ODM combine requirements / policy first
 * - MPC combine is only chosen if there is no ODM combine requirements / policy in place, and
 *   MPC is required
 *
 * Return: Number of splits expected (1 for 2:1 split, 3 for 4:1 split, 0 for no splits).
 */
uint8_t dcn32_predict_pipe_split(struct dc_state *context,
				  display_e2e_pipe_params_st *pipe_e2e)
{
	double pscl_throughput;
	double pscl_throughput_chroma;
	double dpp_clk_single_dpp, clock;
	double clk_frequency = 0.0;
	double vco_speed = context->bw_ctx.dml.soc.dispclk_dppclk_vco_speed_mhz;
	bool total_available_pipes_support = false;
	uint32_t number_of_dpp = 0;
	enum odm_combine_mode odm_mode = dm_odm_combine_mode_disabled;
	double req_dispclk_per_surface = 0;
	uint8_t num_splits = 0;

	dc_assert_fp_enabled();

	dml32_CalculateODMMode(context->bw_ctx.dml.ip.maximum_pixels_per_line_per_dsc_unit,
			pipe_e2e->pipe.dest.hactive,
			pipe_e2e->dout.output_format,
			pipe_e2e->dout.output_type,
			pipe_e2e->pipe.dest.odm_combine_policy,
			context->bw_ctx.dml.soc.clock_limits[context->bw_ctx.dml.soc.num_states - 1].dispclk_mhz,
			context->bw_ctx.dml.soc.clock_limits[context->bw_ctx.dml.soc.num_states - 1].dispclk_mhz,
			pipe_e2e->dout.dsc_enable != 0,
			0, /* TotalNumberOfActiveDPP can be 0 since we're predicting pipe split requirement */
			context->bw_ctx.dml.ip.max_num_dpp,
			pipe_e2e->pipe.dest.pixel_rate_mhz,
			context->bw_ctx.dml.soc.dcn_downspread_percent,
			context->bw_ctx.dml.ip.dispclk_ramp_margin_percent,
			context->bw_ctx.dml.soc.dispclk_dppclk_vco_speed_mhz,
			pipe_e2e->dout.dsc_slices,
			/* Output */
			&total_available_pipes_support,
			&number_of_dpp,
			&odm_mode,
			&req_dispclk_per_surface);

	dml32_CalculateSinglePipeDPPCLKAndSCLThroughput(pipe_e2e->pipe.scale_ratio_depth.hscl_ratio,
			pipe_e2e->pipe.scale_ratio_depth.hscl_ratio_c,
			pipe_e2e->pipe.scale_ratio_depth.vscl_ratio,
			pipe_e2e->pipe.scale_ratio_depth.vscl_ratio_c,
			context->bw_ctx.dml.ip.max_dchub_pscl_bw_pix_per_clk,
			context->bw_ctx.dml.ip.max_pscl_lb_bw_pix_per_clk,
			pipe_e2e->pipe.dest.pixel_rate_mhz,
			pipe_e2e->pipe.src.source_format,
			pipe_e2e->pipe.scale_taps.htaps,
			pipe_e2e->pipe.scale_taps.htaps_c,
			pipe_e2e->pipe.scale_taps.vtaps,
			pipe_e2e->pipe.scale_taps.vtaps_c,
			/* Output */
			&pscl_throughput, &pscl_throughput_chroma,
			&dpp_clk_single_dpp);

	clock = dpp_clk_single_dpp * (1 + context->bw_ctx.dml.soc.dcn_downspread_percent / 100);

	if (clock > 0)
		clk_frequency = vco_speed * 4.0 / ((int)(vco_speed * 4.0) / clock);

	if (odm_mode == dm_odm_combine_mode_2to1)
		num_splits = 1;
	else if (odm_mode == dm_odm_combine_mode_4to1)
		num_splits = 3;
	else if (clk_frequency > context->bw_ctx.dml.soc.clock_limits[context->bw_ctx.dml.soc.num_states - 1].dppclk_mhz)
		num_splits = 1;

	return num_splits;
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

static void get_optimal_ntuple(struct _vcs_dpi_voltage_scaling_st *entry)
{
	if (entry->dcfclk_mhz > 0) {
		float bw_on_sdp = entry->dcfclk_mhz * dcn3_2_soc.return_bus_width_bytes * ((float)dcn3_2_soc.pct_ideal_sdp_bw_after_urgent / 100);

		entry->fabricclk_mhz = bw_on_sdp / (dcn3_2_soc.return_bus_width_bytes * ((float)dcn3_2_soc.pct_ideal_fabric_bw_after_urgent / 100));
		entry->dram_speed_mts = bw_on_sdp / (dcn3_2_soc.num_chans *
				dcn3_2_soc.dram_channel_width_bytes * ((float)dcn3_2_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only / 100));
	} else if (entry->fabricclk_mhz > 0) {
		float bw_on_fabric = entry->fabricclk_mhz * dcn3_2_soc.return_bus_width_bytes * ((float)dcn3_2_soc.pct_ideal_fabric_bw_after_urgent / 100);

		entry->dcfclk_mhz = bw_on_fabric / (dcn3_2_soc.return_bus_width_bytes * ((float)dcn3_2_soc.pct_ideal_sdp_bw_after_urgent / 100));
		entry->dram_speed_mts = bw_on_fabric / (dcn3_2_soc.num_chans *
				dcn3_2_soc.dram_channel_width_bytes * ((float)dcn3_2_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only / 100));
	} else if (entry->dram_speed_mts > 0) {
		float bw_on_dram = entry->dram_speed_mts * dcn3_2_soc.num_chans *
				dcn3_2_soc.dram_channel_width_bytes * ((float)dcn3_2_soc.pct_ideal_dram_sdp_bw_after_urgent_pixel_only / 100);

		entry->fabricclk_mhz = bw_on_dram / (dcn3_2_soc.return_bus_width_bytes * ((float)dcn3_2_soc.pct_ideal_fabric_bw_after_urgent / 100));
		entry->dcfclk_mhz = bw_on_dram / (dcn3_2_soc.return_bus_width_bytes * ((float)dcn3_2_soc.pct_ideal_sdp_bw_after_urgent / 100));
	}
}

void insert_entry_into_table_sorted(struct _vcs_dpi_voltage_scaling_st *table,
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

/**
 * dcn32_set_phantom_stream_timing - Set timing params for the phantom stream
 * @dc: current dc state
 * @context: new dc state
 * @ref_pipe: Main pipe for the phantom stream
 * @phantom_stream: target phantom stream state
 * @pipes: DML pipe params
 * @pipe_cnt: number of DML pipes
 * @dc_pipe_idx: DC pipe index for the main pipe (i.e. ref_pipe)
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
	unsigned int num_dpp;
	unsigned int vlevel = context->bw_ctx.dml.vba.VoltageLevel;
	unsigned int dcfclk = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
	unsigned int socclk = context->bw_ctx.dml.vba.SOCCLKPerState[vlevel];
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

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
	/* Add 16 lines margin to the MALL REGION because SUB_VP_START_LINE must be aligned
	 * to 2 swaths (i.e. 16 lines)
	 */
	phantom_vactive = get_subviewport_lines_needed_in_mall(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx) +
				pstate_width_fw_delay_lines + dc->caps.subvp_swath_height_margin_lines;

	// W/A for DCC corruption with certain high resolution timings.
	// Determing if pipesplit is used. If so, add meta_row_height to the phantom vactive.
	num_dpp = vba->NoOfDPP[vba->VoltageLevel][vba->maxMpcComb][vba->pipe_plane[pipe_idx]];
	phantom_vactive += num_dpp > 1 ? vba->meta_row_height[vba->pipe_plane[pipe_idx]] : 0;

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
	phantom_stream->timing.flags.DSC = 0; // Don't need DSC for phantom timing
}

/**
 * dcn32_get_num_free_pipes - Calculate number of free pipes
 * @dc: current dc state
 * @context: new dc state
 *
 * This function assumes that a "used" pipe is a pipe that has
 * both a stream and a plane assigned to it.
 *
 * Return: Number of free pipes available in the context
 */
static unsigned int dcn32_get_num_free_pipes(struct dc *dc, struct dc_state *context)
{
	unsigned int i;
	unsigned int free_pipes = 0;
	unsigned int num_pipes = 0;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream && !pipe->top_pipe) {
			while (pipe) {
				num_pipes++;
				pipe = pipe->bottom_pipe;
			}
		}
	}

	free_pipes = dc->res_pool->pipe_count - num_pipes;
	return free_pipes;
}

/**
 * dcn32_assign_subvp_pipe - Function to decide which pipe will use Sub-VP.
 * @dc: current dc state
 * @context: new dc state
 * @index: [out] dc pipe index for the pipe chosen to have phantom pipes assigned
 *
 * We enter this function if we are Sub-VP capable (i.e. enough pipes available)
 * and regular P-State switching (i.e. VACTIVE/VBLANK) is not supported, or if
 * we are forcing SubVP P-State switching on the current config.
 *
 * The number of pipes used for the chosen surface must be less than or equal to the
 * number of free pipes available.
 *
 * In general we choose surfaces with the longest frame time first (better for SubVP + VBLANK).
 * For multi-display cases the ActiveDRAMClockChangeMargin doesn't provide enough info on its own
 * for determining which should be the SubVP pipe (need a way to determine if a pipe / plane doesn't
 * support MCLK switching naturally [i.e. ACTIVE or VBLANK]).
 *
 * Return: True if a valid pipe assignment was found for Sub-VP. Otherwise false.
 */
static bool dcn32_assign_subvp_pipe(struct dc *dc,
				    struct dc_state *context,
				    unsigned int *index)
{
	unsigned int i, pipe_idx;
	unsigned int max_frame_time = 0;
	bool valid_assignment_found = false;
	unsigned int free_pipes = dcn32_get_num_free_pipes(dc, context);
	bool current_assignment_freesync = false;
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		unsigned int num_pipes = 0;
		unsigned int refresh_rate = 0;

		if (!pipe->stream)
			continue;

		// Round up
		refresh_rate = (pipe->stream->timing.pix_clk_100hz * 100 +
				pipe->stream->timing.v_total * pipe->stream->timing.h_total - 1)
				/ (double)(pipe->stream->timing.v_total * pipe->stream->timing.h_total);
		/* SubVP pipe candidate requirements:
		 * - Refresh rate < 120hz
		 * - Not able to switch in vactive naturally (switching in active means the
		 *   DET provides enough buffer to hide the P-State switch latency -- trying
		 *   to combine this with SubVP can cause issues with the scheduling).
		 * - Not TMZ surface
		 */
		if (pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_NONE && refresh_rate < 120 && !pipe->plane_state->address.tmz_surface &&
				(vba->ActiveDRAMClockChangeLatencyMarginPerState[vba->VoltageLevel][vba->maxMpcComb][vba->pipe_plane[pipe_idx]] <= 0 ||
				(vba->ActiveDRAMClockChangeLatencyMarginPerState[vba->VoltageLevel][vba->maxMpcComb][vba->pipe_plane[pipe_idx]] > 0 &&
						dcn32_allow_subvp_with_active_margin(pipe)))) {
			while (pipe) {
				num_pipes++;
				pipe = pipe->bottom_pipe;
			}

			pipe = &context->res_ctx.pipe_ctx[i];
			if (num_pipes <= free_pipes) {
				struct dc_stream_state *stream = pipe->stream;
				unsigned int frame_us = (stream->timing.v_total * stream->timing.h_total /
						(double)(stream->timing.pix_clk_100hz * 100)) * 1000000;
				if (frame_us > max_frame_time && !stream->ignore_msa_timing_param) {
					*index = i;
					max_frame_time = frame_us;
					valid_assignment_found = true;
					current_assignment_freesync = false;
				/* For the 2-Freesync display case, still choose the one with the
			     * longest frame time
			     */
				} else if (stream->ignore_msa_timing_param && (!valid_assignment_found ||
						(current_assignment_freesync && frame_us > max_frame_time))) {
					*index = i;
					valid_assignment_found = true;
					current_assignment_freesync = true;
				}
			}
		}
		pipe_idx++;
	}
	return valid_assignment_found;
}

/**
 * dcn32_enough_pipes_for_subvp - Function to check if there are "enough" pipes for SubVP.
 * @dc: current dc state
 * @context: new dc state
 *
 * This function returns true if there are enough free pipes
 * to create the required phantom pipes for any given stream
 * (that does not already have phantom pipe assigned).
 *
 * e.g. For a 2 stream config where the first stream uses one
 * pipe and the second stream uses 2 pipes (i.e. pipe split),
 * this function will return true because there is 1 remaining
 * pipe which can be used as the phantom pipe for the non pipe
 * split pipe.
 *
 * Return:
 * True if there are enough free pipes to assign phantom pipes to at least one
 * stream that does not already have phantom pipes assigned. Otherwise false.
 */
static bool dcn32_enough_pipes_for_subvp(struct dc *dc, struct dc_state *context)
{
	unsigned int i, split_cnt, free_pipes;
	unsigned int min_pipe_split = dc->res_pool->pipe_count + 1; // init as max number of pipes + 1
	bool subvp_possible = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// Find the minimum pipe split count for non SubVP pipes
		if (pipe->stream && !pipe->top_pipe &&
		    pipe->stream->mall_stream_config.type == SUBVP_NONE) {
			split_cnt = 0;
			while (pipe) {
				split_cnt++;
				pipe = pipe->bottom_pipe;
			}

			if (split_cnt < min_pipe_split)
				min_pipe_split = split_cnt;
		}
	}

	free_pipes = dcn32_get_num_free_pipes(dc, context);

	// SubVP only possible if at least one pipe is being used (i.e. free_pipes
	// should not equal to the pipe_count)
	if (free_pipes >= min_pipe_split && free_pipes < dc->res_pool->pipe_count)
		subvp_possible = true;

	return subvp_possible;
}

/**
 * subvp_subvp_schedulable - Determine if SubVP + SubVP config is schedulable
 * @dc: current dc state
 * @context: new dc state
 *
 * High level algorithm:
 * 1. Find longest microschedule length (in us) between the two SubVP pipes
 * 2. Check if the worst case overlap (VBLANK in middle of ACTIVE) for both
 * pipes still allows for the maximum microschedule to fit in the active
 * region for both pipes.
 *
 * Return: True if the SubVP + SubVP config is schedulable, false otherwise
 */
static bool subvp_subvp_schedulable(struct dc *dc, struct dc_state *context)
{
	struct pipe_ctx *subvp_pipes[2];
	struct dc_stream_state *phantom = NULL;
	uint32_t microschedule_lines = 0;
	uint32_t index = 0;
	uint32_t i;
	uint32_t max_microschedule_us = 0;
	int32_t vactive1_us, vactive2_us, vblank1_us, vblank2_us;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		uint32_t time_us = 0;

		/* Loop to calculate the maximum microschedule time between the two SubVP pipes,
		 * and also to store the two main SubVP pipe pointers in subvp_pipes[2].
		 */
		if (pipe->stream && pipe->plane_state && !pipe->top_pipe &&
		    pipe->stream->mall_stream_config.type == SUBVP_MAIN) {
			phantom = pipe->stream->mall_stream_config.paired_stream;
			microschedule_lines = (phantom->timing.v_total - phantom->timing.v_front_porch) +
					phantom->timing.v_addressable;

			// Round up when calculating microschedule time (+ 1 at the end)
			time_us = (microschedule_lines * phantom->timing.h_total) /
					(double)(phantom->timing.pix_clk_100hz * 100) * 1000000 +
						dc->caps.subvp_prefetch_end_to_mall_start_us +
						dc->caps.subvp_fw_processing_delay_us + 1;
			if (time_us > max_microschedule_us)
				max_microschedule_us = time_us;

			subvp_pipes[index] = pipe;
			index++;

			// Maximum 2 SubVP pipes
			if (index == 2)
				break;
		}
	}
	vactive1_us = ((subvp_pipes[0]->stream->timing.v_addressable * subvp_pipes[0]->stream->timing.h_total) /
			(double)(subvp_pipes[0]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vactive2_us = ((subvp_pipes[1]->stream->timing.v_addressable * subvp_pipes[1]->stream->timing.h_total) /
				(double)(subvp_pipes[1]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vblank1_us = (((subvp_pipes[0]->stream->timing.v_total - subvp_pipes[0]->stream->timing.v_addressable) *
			subvp_pipes[0]->stream->timing.h_total) /
			(double)(subvp_pipes[0]->stream->timing.pix_clk_100hz * 100)) * 1000000;
	vblank2_us = (((subvp_pipes[1]->stream->timing.v_total - subvp_pipes[1]->stream->timing.v_addressable) *
			subvp_pipes[1]->stream->timing.h_total) /
			(double)(subvp_pipes[1]->stream->timing.pix_clk_100hz * 100)) * 1000000;

	if ((vactive1_us - vblank2_us) / 2 > max_microschedule_us &&
	    (vactive2_us - vblank1_us) / 2 > max_microschedule_us)
		return true;

	return false;
}

/**
 * subvp_drr_schedulable - Determine if SubVP + DRR config is schedulable
 * @dc: current dc state
 * @context: new dc state
 * @drr_pipe: DRR pipe_ctx for the SubVP + DRR config
 *
 * High level algorithm:
 * 1. Get timing for SubVP pipe, phantom pipe, and DRR pipe
 * 2. Determine the frame time for the DRR display when adding required margin for MCLK switching
 * (the margin is equal to the MALL region + DRR margin (500us))
 * 3.If (SubVP Active - Prefetch > Stretched DRR frame + max(MALL region, Stretched DRR frame))
 * then report the configuration as supported
 *
 * Return: True if the SubVP + DRR config is schedulable, false otherwise
 */
static bool subvp_drr_schedulable(struct dc *dc, struct dc_state *context, struct pipe_ctx *drr_pipe)
{
	bool schedulable = false;
	uint32_t i;
	struct pipe_ctx *pipe = NULL;
	struct dc_crtc_timing *main_timing = NULL;
	struct dc_crtc_timing *phantom_timing = NULL;
	struct dc_crtc_timing *drr_timing = NULL;
	int16_t prefetch_us = 0;
	int16_t mall_region_us = 0;
	int16_t drr_frame_us = 0;	// nominal frame time
	int16_t subvp_active_us = 0;
	int16_t stretched_drr_us = 0;
	int16_t drr_stretched_vblank_us = 0;
	int16_t max_vblank_mallregion = 0;

	// Find SubVP pipe
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		// We check for master pipe, but it shouldn't matter since we only need
		// the pipe for timing info (stream should be same for any pipe splits)
		if (!pipe->stream || !pipe->plane_state || pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		// Find the SubVP pipe
		if (pipe->stream->mall_stream_config.type == SUBVP_MAIN)
			break;
	}

	main_timing = &pipe->stream->timing;
	phantom_timing = &pipe->stream->mall_stream_config.paired_stream->timing;
	drr_timing = &drr_pipe->stream->timing;
	prefetch_us = (phantom_timing->v_total - phantom_timing->v_front_porch) * phantom_timing->h_total /
			(double)(phantom_timing->pix_clk_100hz * 100) * 1000000 +
			dc->caps.subvp_prefetch_end_to_mall_start_us;
	subvp_active_us = main_timing->v_addressable * main_timing->h_total /
			(double)(main_timing->pix_clk_100hz * 100) * 1000000;
	drr_frame_us = drr_timing->v_total * drr_timing->h_total /
			(double)(drr_timing->pix_clk_100hz * 100) * 1000000;
	// P-State allow width and FW delays already included phantom_timing->v_addressable
	mall_region_us = phantom_timing->v_addressable * phantom_timing->h_total /
			(double)(phantom_timing->pix_clk_100hz * 100) * 1000000;
	stretched_drr_us = drr_frame_us + mall_region_us + SUBVP_DRR_MARGIN_US;
	drr_stretched_vblank_us = (drr_timing->v_total - drr_timing->v_addressable) * drr_timing->h_total /
			(double)(drr_timing->pix_clk_100hz * 100) * 1000000 + (stretched_drr_us - drr_frame_us);
	max_vblank_mallregion = drr_stretched_vblank_us > mall_region_us ? drr_stretched_vblank_us : mall_region_us;

	/* We consider SubVP + DRR schedulable if the stretched frame duration of the DRR display (i.e. the
	 * highest refresh rate + margin that can support UCLK P-State switch) passes the static analysis
	 * for VBLANK: (VACTIVE region of the SubVP pipe can fit the MALL prefetch, VBLANK frame time,
	 * and the max of (VBLANK blanking time, MALL region)).
	 */
	if (stretched_drr_us < (1 / (double)drr_timing->min_refresh_in_uhz) * 1000000 * 1000000 &&
			subvp_active_us - prefetch_us - stretched_drr_us - max_vblank_mallregion > 0)
		schedulable = true;

	return schedulable;
}


/**
 * subvp_vblank_schedulable - Determine if SubVP + VBLANK config is schedulable
 * @dc: current dc state
 * @context: new dc state
 *
 * High level algorithm:
 * 1. Get timing for SubVP pipe, phantom pipe, and VBLANK pipe
 * 2. If (SubVP Active - Prefetch > Vblank Frame Time + max(MALL region, Vblank blanking time))
 * then report the configuration as supported
 * 3. If the VBLANK display is DRR, then take the DRR static schedulability path
 *
 * Return: True if the SubVP + VBLANK/DRR config is schedulable, false otherwise
 */
static bool subvp_vblank_schedulable(struct dc *dc, struct dc_state *context)
{
	struct pipe_ctx *pipe = NULL;
	struct pipe_ctx *subvp_pipe = NULL;
	bool found = false;
	bool schedulable = false;
	uint32_t i = 0;
	uint8_t vblank_index = 0;
	uint16_t prefetch_us = 0;
	uint16_t mall_region_us = 0;
	uint16_t vblank_frame_us = 0;
	uint16_t subvp_active_us = 0;
	uint16_t vblank_blank_us = 0;
	uint16_t max_vblank_mallregion = 0;
	struct dc_crtc_timing *main_timing = NULL;
	struct dc_crtc_timing *phantom_timing = NULL;
	struct dc_crtc_timing *vblank_timing = NULL;

	/* For SubVP + VBLANK/DRR cases, we assume there can only be
	 * a single VBLANK/DRR display. If DML outputs SubVP + VBLANK
	 * is supported, it is either a single VBLANK case or two VBLANK
	 * displays which are synchronized (in which case they have identical
	 * timings).
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		// We check for master pipe, but it shouldn't matter since we only need
		// the pipe for timing info (stream should be same for any pipe splits)
		if (!pipe->stream || !pipe->plane_state || pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		if (!found && pipe->stream->mall_stream_config.type == SUBVP_NONE) {
			// Found pipe which is not SubVP or Phantom (i.e. the VBLANK pipe).
			vblank_index = i;
			found = true;
		}

		if (!subvp_pipe && pipe->stream->mall_stream_config.type == SUBVP_MAIN)
			subvp_pipe = pipe;
	}
	// Use ignore_msa_timing_param flag to identify as DRR
	if (found && context->res_ctx.pipe_ctx[vblank_index].stream->ignore_msa_timing_param) {
		// SUBVP + DRR case
		schedulable = subvp_drr_schedulable(dc, context, &context->res_ctx.pipe_ctx[vblank_index]);
	} else if (found) {
		main_timing = &subvp_pipe->stream->timing;
		phantom_timing = &subvp_pipe->stream->mall_stream_config.paired_stream->timing;
		vblank_timing = &context->res_ctx.pipe_ctx[vblank_index].stream->timing;
		// Prefetch time is equal to VACTIVE + BP + VSYNC of the phantom pipe
		// Also include the prefetch end to mallstart delay time
		prefetch_us = (phantom_timing->v_total - phantom_timing->v_front_porch) * phantom_timing->h_total /
				(double)(phantom_timing->pix_clk_100hz * 100) * 1000000 +
				dc->caps.subvp_prefetch_end_to_mall_start_us;
		// P-State allow width and FW delays already included phantom_timing->v_addressable
		mall_region_us = phantom_timing->v_addressable * phantom_timing->h_total /
				(double)(phantom_timing->pix_clk_100hz * 100) * 1000000;
		vblank_frame_us = vblank_timing->v_total * vblank_timing->h_total /
				(double)(vblank_timing->pix_clk_100hz * 100) * 1000000;
		vblank_blank_us =  (vblank_timing->v_total - vblank_timing->v_addressable) * vblank_timing->h_total /
				(double)(vblank_timing->pix_clk_100hz * 100) * 1000000;
		subvp_active_us = main_timing->v_addressable * main_timing->h_total /
				(double)(main_timing->pix_clk_100hz * 100) * 1000000;
		max_vblank_mallregion = vblank_blank_us > mall_region_us ? vblank_blank_us : mall_region_us;

		// Schedulable if VACTIVE region of the SubVP pipe can fit the MALL prefetch, VBLANK frame time,
		// and the max of (VBLANK blanking time, MALL region)
		// TODO: Possibly add some margin (i.e. the below conditions should be [...] > X instead of [...] > 0)
		if (subvp_active_us - prefetch_us - vblank_frame_us - max_vblank_mallregion > 0)
			schedulable = true;
	}
	return schedulable;
}

/**
 * subvp_validate_static_schedulability - Check which SubVP case is calculated
 * and handle static analysis based on the case.
 * @dc: current dc state
 * @context: new dc state
 * @vlevel: Voltage level calculated by DML
 *
 * Three cases:
 * 1. SubVP + SubVP
 * 2. SubVP + VBLANK (DRR checked internally)
 * 3. SubVP + VACTIVE (currently unsupported)
 *
 * Return: True if statically schedulable, false otherwise
 */
static bool subvp_validate_static_schedulability(struct dc *dc,
				struct dc_state *context,
				int vlevel)
{
	bool schedulable = true;	// true by default for single display case
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	uint32_t i, pipe_idx;
	uint8_t subvp_count = 0;
	uint8_t vactive_count = 0;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (pipe->plane_state && !pipe->top_pipe &&
				pipe->stream->mall_stream_config.type == SUBVP_MAIN)
			subvp_count++;

		// Count how many planes that aren't SubVP/phantom are capable of VACTIVE
		// switching (SubVP + VACTIVE unsupported). In situations where we force
		// SubVP for a VACTIVE plane, we don't want to increment the vactive_count.
		if (vba->ActiveDRAMClockChangeLatencyMargin[vba->pipe_plane[pipe_idx]] > 0 &&
		    pipe->stream->mall_stream_config.type == SUBVP_NONE) {
			vactive_count++;
		}
		pipe_idx++;
	}

	if (subvp_count == 2) {
		// Static schedulability check for SubVP + SubVP case
		schedulable = subvp_subvp_schedulable(dc, context);
	} else if (vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_vblank_w_mall_sub_vp) {
		// Static schedulability check for SubVP + VBLANK case. Also handle the case where
		// DML outputs SubVP + VBLANK + VACTIVE (DML will report as SubVP + VBLANK)
		if (vactive_count > 0)
			schedulable = false;
		else
			schedulable = subvp_vblank_schedulable(dc, context);
	} else if (vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_vactive_w_mall_sub_vp &&
			vactive_count > 0) {
		// For single display SubVP cases, DML will output dm_dram_clock_change_vactive_w_mall_sub_vp by default.
		// We tell the difference between SubVP vs. SubVP + VACTIVE by checking the vactive_count.
		// SubVP + VACTIVE currently unsupported
		schedulable = false;
	}
	return schedulable;
}

static void dcn32_full_validate_bw_helper(struct dc *dc,
				   struct dc_state *context,
				   display_e2e_pipe_params_st *pipes,
				   int *vlevel,
				   int *split,
				   bool *merge,
				   int *pipe_cnt)
{
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;
	unsigned int dc_pipe_idx = 0;
	bool found_supported_config = false;
	struct pipe_ctx *pipe = NULL;
	uint32_t non_subvp_pipes = 0;
	bool drr_pipe_found = false;
	uint32_t drr_pipe_index = 0;
	uint32_t i = 0;

	dc_assert_fp_enabled();

	/*
	 * DML favors voltage over p-state, but we're more interested in
	 * supporting p-state over voltage. We can't support p-state in
	 * prefetch mode > 0 so try capping the prefetch mode to start.
	 * Override present for testing.
	 */
	if (dc->debug.dml_disallow_alternate_prefetch_modes)
		context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final =
			dm_prefetch_support_uclk_fclk_and_stutter;
	else
		context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final =
			dm_prefetch_support_uclk_fclk_and_stutter_if_possible;

	*vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, *pipe_cnt);
	/* This may adjust vlevel and maxMpcComb */
	if (*vlevel < context->bw_ctx.dml.soc.num_states) {
		*vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, *vlevel, split, merge);
		vba->VoltageLevel = *vlevel;
	}

	/* Conditions for setting up phantom pipes for SubVP:
	 * 1. Not force disable SubVP
	 * 2. Full update (i.e. !fast_validate)
	 * 3. Enough pipes are available to support SubVP (TODO: Which pipes will use VACTIVE / VBLANK / SUBVP?)
	 * 4. Display configuration passes validation
	 * 5. (Config doesn't support MCLK in VACTIVE/VBLANK || dc->debug.force_subvp_mclk_switch)
	 */
	if (!dc->debug.force_disable_subvp && dcn32_all_pipes_have_stream_and_plane(dc, context) &&
	    !dcn32_mpo_in_use(context) && !dcn32_any_surfaces_rotated(dc, context) &&
		(*vlevel == context->bw_ctx.dml.soc.num_states ||
	    vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported ||
	    dc->debug.force_subvp_mclk_switch)) {

		dcn32_merge_pipes_for_subvp(dc, context);
		memset(merge, 0, MAX_PIPES * sizeof(bool));

		/* to re-initialize viewport after the pipe merge */
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (!pipe_ctx->plane_state || !pipe_ctx->stream)
				continue;

			resource_build_scaling_params(pipe_ctx);
		}

		while (!found_supported_config && dcn32_enough_pipes_for_subvp(dc, context) &&
			dcn32_assign_subvp_pipe(dc, context, &dc_pipe_idx)) {
			/* For the case where *vlevel = num_states, bandwidth validation has failed for this config.
			 * Adding phantom pipes won't change the validation result, so change the DML input param
			 * for P-State support before adding phantom pipes and recalculating the DML result.
			 * However, this case is only applicable for SubVP + DRR cases because the prefetch mode
			 * will not allow for switch in VBLANK. The DRR display must have it's VBLANK stretched
			 * enough to support MCLK switching.
			 */
			if (*vlevel == context->bw_ctx.dml.soc.num_states &&
				context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final ==
					dm_prefetch_support_uclk_fclk_and_stutter) {
				context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final =
								dm_prefetch_support_stutter;
				/* There are params (such as FabricClock) that need to be recalculated
				 * after validation fails (otherwise it will be 0). Calculation for
				 * phantom vactive requires call into DML, so we must ensure all the
				 * vba params are valid otherwise we'll get incorrect phantom vactive.
				 */
				*vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, *pipe_cnt);
			}

			dc->res_pool->funcs->add_phantom_pipes(dc, context, pipes, *pipe_cnt, dc_pipe_idx);

			*pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, false);
			// Populate dppclk to trigger a recalculate in dml_get_voltage_level
			// so the phantom pipe DLG params can be assigned correctly.
			pipes[0].clks_cfg.dppclk_mhz = get_dppclk_calculated(&context->bw_ctx.dml, pipes, *pipe_cnt, 0);
			*vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, *pipe_cnt);

			if (*vlevel < context->bw_ctx.dml.soc.num_states &&
			    vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] != dm_dram_clock_change_unsupported
			    && subvp_validate_static_schedulability(dc, context, *vlevel)) {
				found_supported_config = true;
			} else if (*vlevel < context->bw_ctx.dml.soc.num_states &&
					vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported) {
				/* Case where 1 SubVP is added, and DML reports MCLK unsupported. This handles
				 * the case for SubVP + DRR, where the DRR display does not support MCLK switch
				 * at it's native refresh rate / timing.
				 */
				for (i = 0; i < dc->res_pool->pipe_count; i++) {
					pipe = &context->res_ctx.pipe_ctx[i];
					if (pipe->stream && pipe->plane_state && !pipe->top_pipe &&
					    pipe->stream->mall_stream_config.type == SUBVP_NONE) {
						non_subvp_pipes++;
						// Use ignore_msa_timing_param flag to identify as DRR
						if (pipe->stream->ignore_msa_timing_param) {
							drr_pipe_found = true;
							drr_pipe_index = i;
						}
					}
				}
				// If there is only 1 remaining non SubVP pipe that is DRR, check static
				// schedulability for SubVP + DRR.
				if (non_subvp_pipes == 1 && drr_pipe_found) {
					found_supported_config = subvp_drr_schedulable(dc, context,
										       &context->res_ctx.pipe_ctx[drr_pipe_index]);
				}
			}
		}

		// If SubVP pipe config is unsupported (or cannot be used for UCLK switching)
		// remove phantom pipes and repopulate dml pipes
		if (!found_supported_config) {
			dc->res_pool->funcs->remove_phantom_pipes(dc, context);
			vba->DRAMClockChangeSupport[*vlevel][vba->maxMpcComb] = dm_dram_clock_change_unsupported;
			*pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, false);

			*vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, *pipe_cnt);
			/* This may adjust vlevel and maxMpcComb */
			if (*vlevel < context->bw_ctx.dml.soc.num_states) {
				*vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, *vlevel, split, merge);
				vba->VoltageLevel = *vlevel;
			}
		} else {
			// Most populate phantom DLG params before programming hardware / timing for phantom pipe
			dcn32_helper_populate_phantom_dlg_params(dc, context, pipes, *pipe_cnt);

			/* Call validate_apply_pipe_split flags after calling DML getters for
			 * phantom dlg params, or some of the VBA params indicating pipe split
			 * can be overwritten by the getters.
			 *
			 * When setting up SubVP config, all pipes are merged before attempting to
			 * add phantom pipes. If pipe split (ODM / MPC) is required, both the main
			 * and phantom pipes will be split in the regular pipe splitting sequence.
			 */
			memset(split, 0, MAX_PIPES * sizeof(int));
			memset(merge, 0, MAX_PIPES * sizeof(bool));
			*vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, *vlevel, split, merge);
			vba->VoltageLevel = *vlevel;
			// Note: We can't apply the phantom pipes to hardware at this time. We have to wait
			// until driver has acquired the DMCUB lock to do it safely.
		}
	}
}

static bool is_dtbclk_required(struct dc *dc, struct dc_state *context)
{
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
		if (is_dp_128b_132b_signal(&context->res_ctx.pipe_ctx[i]))
			return true;
	}
	return false;
}

static void dcn32_calculate_dlg_params(struct dc *dc, struct dc_state *context,
				       display_e2e_pipe_params_st *pipes,
				       int pipe_cnt, int vlevel)
{
	int i, pipe_idx;
	bool usr_retraining_support = false;
	bool unbounded_req_enabled = false;

	dc_assert_fp_enabled();

	/* Writeback MCIF_WB arbitration parameters */
	dc->res_pool->funcs->set_mcif_arb_params(dc, context, pipes, pipe_cnt);

	context->bw_ctx.bw.dcn.clk.dispclk_khz = context->bw_ctx.dml.vba.DISPCLK * 1000;
	context->bw_ctx.bw.dcn.clk.dcfclk_khz = context->bw_ctx.dml.vba.DCFCLK * 1000;
	context->bw_ctx.bw.dcn.clk.socclk_khz = context->bw_ctx.dml.vba.SOCCLK * 1000;
	context->bw_ctx.bw.dcn.clk.dramclk_khz = context->bw_ctx.dml.vba.DRAMSpeed * 1000 / 16;
	context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz = context->bw_ctx.dml.vba.DCFCLKDeepSleep * 1000;
	context->bw_ctx.bw.dcn.clk.fclk_khz = context->bw_ctx.dml.vba.FabricClock * 1000;
	context->bw_ctx.bw.dcn.clk.p_state_change_support =
			context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb]
					!= dm_dram_clock_change_unsupported;
	context->bw_ctx.bw.dcn.clk.num_ways = dcn32_helper_calculate_num_ways_for_subvp(dc, context);

	context->bw_ctx.bw.dcn.clk.dppclk_khz = 0;
	context->bw_ctx.bw.dcn.clk.dtbclk_en = is_dtbclk_required(dc, context);
	context->bw_ctx.bw.dcn.clk.ref_dtbclk_khz = context->bw_ctx.dml.vba.DTBCLKPerState[vlevel] * 1000;
	if (context->bw_ctx.dml.vba.FCLKChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb] == dm_fclock_change_unsupported)
		context->bw_ctx.bw.dcn.clk.fclk_p_state_change_support = false;
	else
		context->bw_ctx.bw.dcn.clk.fclk_p_state_change_support = true;

	usr_retraining_support = context->bw_ctx.dml.vba.USRRetrainingSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
	ASSERT(usr_retraining_support);

	if (context->bw_ctx.bw.dcn.clk.dispclk_khz < dc->debug.min_disp_clk_khz)
		context->bw_ctx.bw.dcn.clk.dispclk_khz = dc->debug.min_disp_clk_khz;

	unbounded_req_enabled = get_unbounded_request_enabled(&context->bw_ctx.dml, pipes, pipe_cnt);

	if (unbounded_req_enabled && pipe_cnt > 1) {
		// Unbounded requesting should not ever be used when more than 1 pipe is enabled.
		ASSERT(false);
		unbounded_req_enabled = false;
	}

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
		pipes[pipe_idx].pipe.dest.vstartup_start = get_vstartup(&context->bw_ctx.dml, pipes, pipe_cnt,
				pipe_idx);
		pipes[pipe_idx].pipe.dest.vupdate_offset = get_vupdate_offset(&context->bw_ctx.dml, pipes, pipe_cnt,
				pipe_idx);
		pipes[pipe_idx].pipe.dest.vupdate_width = get_vupdate_width(&context->bw_ctx.dml, pipes, pipe_cnt,
				pipe_idx);
		pipes[pipe_idx].pipe.dest.vready_offset = get_vready_offset(&context->bw_ctx.dml, pipes, pipe_cnt,
				pipe_idx);

		if (context->res_ctx.pipe_ctx[i].stream->mall_stream_config.type == SUBVP_PHANTOM) {
			// Phantom pipe requires that DET_SIZE = 0 and no unbounded requests
			context->res_ctx.pipe_ctx[i].det_buffer_size_kb = 0;
			context->res_ctx.pipe_ctx[i].unbounded_req = false;
		} else {
			context->res_ctx.pipe_ctx[i].det_buffer_size_kb = get_det_buffer_size_kbytes(&context->bw_ctx.dml, pipes, pipe_cnt,
							pipe_idx);
			context->res_ctx.pipe_ctx[i].unbounded_req = unbounded_req_enabled;
		}

		if (context->bw_ctx.bw.dcn.clk.dppclk_khz < pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			context->bw_ctx.bw.dcn.clk.dppclk_khz = pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz = pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].pipe_dlg_param = pipes[pipe_idx].pipe.dest;
		pipe_idx++;
	}
	/*save a original dppclock copy*/
	context->bw_ctx.bw.dcn.clk.bw_dppclk_khz = context->bw_ctx.bw.dcn.clk.dppclk_khz;
	context->bw_ctx.bw.dcn.clk.bw_dispclk_khz = context->bw_ctx.bw.dcn.clk.dispclk_khz;
	context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dppclk_mhz
			* 1000;
	context->bw_ctx.bw.dcn.clk.max_supported_dispclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dispclk_mhz
			* 1000;

	context->bw_ctx.bw.dcn.compbuf_size_kb = context->bw_ctx.dml.ip.config_return_buffer_size_in_kbytes;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (context->res_ctx.pipe_ctx[i].stream)
			context->bw_ctx.bw.dcn.compbuf_size_kb -= context->res_ctx.pipe_ctx[i].det_buffer_size_kb;
	}

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		context->bw_ctx.dml.funcs.rq_dlg_get_dlg_reg_v2(&context->bw_ctx.dml,
				&context->res_ctx.pipe_ctx[i].dlg_regs, &context->res_ctx.pipe_ctx[i].ttu_regs, pipes,
				pipe_cnt, pipe_idx);

		context->bw_ctx.dml.funcs.rq_dlg_get_rq_reg_v2(&context->res_ctx.pipe_ctx[i].rq_regs,
				&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		pipe_idx++;
	}
}

static struct pipe_ctx *dcn32_find_split_pipe(
		struct dc *dc,
		struct dc_state *context,
		int old_index)
{
	struct pipe_ctx *pipe = NULL;
	int i;

	if (old_index >= 0 && context->res_ctx.pipe_ctx[old_index].stream == NULL) {
		pipe = &context->res_ctx.pipe_ctx[old_index];
		pipe->pipe_idx = old_index;
	}

	if (!pipe)
		for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
			if (dc->current_state->res_ctx.pipe_ctx[i].top_pipe == NULL
					&& dc->current_state->res_ctx.pipe_ctx[i].prev_odm_pipe == NULL) {
				if (context->res_ctx.pipe_ctx[i].stream == NULL) {
					pipe = &context->res_ctx.pipe_ctx[i];
					pipe->pipe_idx = i;
					break;
				}
			}
		}

	/*
	 * May need to fix pipes getting tossed from 1 opp to another on flip
	 * Add for debugging transient underflow during topology updates:
	 * ASSERT(pipe);
	 */
	if (!pipe)
		for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
			if (context->res_ctx.pipe_ctx[i].stream == NULL) {
				pipe = &context->res_ctx.pipe_ctx[i];
				pipe->pipe_idx = i;
				break;
			}
		}

	return pipe;
}

static bool dcn32_split_stream_for_mpc_or_odm(
		const struct dc *dc,
		struct resource_context *res_ctx,
		struct pipe_ctx *pri_pipe,
		struct pipe_ctx *sec_pipe,
		bool odm)
{
	int pipe_idx = sec_pipe->pipe_idx;
	const struct resource_pool *pool = dc->res_pool;

	DC_LOGGER_INIT(dc->ctx->logger);

	if (odm && pri_pipe->plane_state) {
		/* ODM + window MPO, where MPO window is on left half only */
		if (pri_pipe->plane_state->clip_rect.x + pri_pipe->plane_state->clip_rect.width <=
				pri_pipe->stream->src.x + pri_pipe->stream->src.width/2) {

			DC_LOG_SCALER("%s - ODM + window MPO(left). pri_pipe:%d\n",
					__func__,
					pri_pipe->pipe_idx);
			return true;
		}

		/* ODM + window MPO, where MPO window is on right half only */
		if (pri_pipe->plane_state->clip_rect.x >= pri_pipe->stream->src.x +  pri_pipe->stream->src.width/2) {

			DC_LOG_SCALER("%s - ODM + window MPO(right). pri_pipe:%d\n",
					__func__,
					pri_pipe->pipe_idx);
			return true;
		}
	}

	*sec_pipe = *pri_pipe;

	sec_pipe->pipe_idx = pipe_idx;
	sec_pipe->plane_res.mi = pool->mis[pipe_idx];
	sec_pipe->plane_res.hubp = pool->hubps[pipe_idx];
	sec_pipe->plane_res.ipp = pool->ipps[pipe_idx];
	sec_pipe->plane_res.xfm = pool->transforms[pipe_idx];
	sec_pipe->plane_res.dpp = pool->dpps[pipe_idx];
	sec_pipe->plane_res.mpcc_inst = pool->dpps[pipe_idx]->inst;
	sec_pipe->stream_res.dsc = NULL;
	if (odm) {
		if (pri_pipe->next_odm_pipe) {
			ASSERT(pri_pipe->next_odm_pipe != sec_pipe);
			sec_pipe->next_odm_pipe = pri_pipe->next_odm_pipe;
			sec_pipe->next_odm_pipe->prev_odm_pipe = sec_pipe;
		}
		if (pri_pipe->top_pipe && pri_pipe->top_pipe->next_odm_pipe) {
			pri_pipe->top_pipe->next_odm_pipe->bottom_pipe = sec_pipe;
			sec_pipe->top_pipe = pri_pipe->top_pipe->next_odm_pipe;
		}
		if (pri_pipe->bottom_pipe && pri_pipe->bottom_pipe->next_odm_pipe) {
			pri_pipe->bottom_pipe->next_odm_pipe->top_pipe = sec_pipe;
			sec_pipe->bottom_pipe = pri_pipe->bottom_pipe->next_odm_pipe;
		}
		pri_pipe->next_odm_pipe = sec_pipe;
		sec_pipe->prev_odm_pipe = pri_pipe;
		ASSERT(sec_pipe->top_pipe == NULL);

		if (!sec_pipe->top_pipe)
			sec_pipe->stream_res.opp = pool->opps[pipe_idx];
		else
			sec_pipe->stream_res.opp = sec_pipe->top_pipe->stream_res.opp;
		if (sec_pipe->stream->timing.flags.DSC == 1) {
			dcn20_acquire_dsc(dc, res_ctx, &sec_pipe->stream_res.dsc, pipe_idx);
			ASSERT(sec_pipe->stream_res.dsc);
			if (sec_pipe->stream_res.dsc == NULL)
				return false;
		}
	} else {
		if (pri_pipe->bottom_pipe) {
			ASSERT(pri_pipe->bottom_pipe != sec_pipe);
			sec_pipe->bottom_pipe = pri_pipe->bottom_pipe;
			sec_pipe->bottom_pipe->top_pipe = sec_pipe;
		}
		pri_pipe->bottom_pipe = sec_pipe;
		sec_pipe->top_pipe = pri_pipe;

		ASSERT(pri_pipe->plane_state);
	}

	return true;
}

bool dcn32_internal_validate_bw(struct dc *dc,
				struct dc_state *context,
				display_e2e_pipe_params_st *pipes,
				int *pipe_cnt_out,
				int *vlevel_out,
				bool fast_validate)
{
	bool out = false;
	bool repopulate_pipes = false;
	int split[MAX_PIPES] = { 0 };
	bool merge[MAX_PIPES] = { false };
	bool newly_split[MAX_PIPES] = { false };
	int pipe_cnt, i, pipe_idx;
	int vlevel = context->bw_ctx.dml.soc.num_states;
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

	dc_assert_fp_enabled();

	ASSERT(pipes);
	if (!pipes)
		return false;

	// For each full update, remove all existing phantom pipes first
	dc->res_pool->funcs->remove_phantom_pipes(dc, context);

	dc->res_pool->funcs->update_soc_for_wm_a(dc, context);

	pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, fast_validate);

	if (!pipe_cnt) {
		out = true;
		goto validate_out;
	}

	dml_log_pipe_params(&context->bw_ctx.dml, pipes, pipe_cnt);

	if (!fast_validate)
		dcn32_full_validate_bw_helper(dc, context, pipes, &vlevel, split, merge, &pipe_cnt);

	if (fast_validate ||
			(dc->debug.dml_disallow_alternate_prefetch_modes &&
			(vlevel == context->bw_ctx.dml.soc.num_states ||
				vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported))) {
		/*
		 * If dml_disallow_alternate_prefetch_modes is false, then we have already
		 * tried alternate prefetch modes during full validation.
		 *
		 * If mode is unsupported or there is no p-state support, then
		 * fall back to favouring voltage.
		 *
		 * If Prefetch mode 0 failed for this config, or passed with Max UCLK, then try
		 * to support with Prefetch mode 1 (dm_prefetch_support_fclk_and_stutter == 2)
		 */
		context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final =
			dm_prefetch_support_fclk_and_stutter;

		vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);

		/* Last attempt with Prefetch mode 2 (dm_prefetch_support_stutter == 3) */
		if (vlevel == context->bw_ctx.dml.soc.num_states) {
			context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final =
				dm_prefetch_support_stutter;
			vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);
		}

		if (vlevel < context->bw_ctx.dml.soc.num_states) {
			memset(split, 0, sizeof(split));
			memset(merge, 0, sizeof(merge));
			vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, vlevel, split, merge);
			// dcn20_validate_apply_pipe_split_flags can modify voltage level outside of DML
			vba->VoltageLevel = vlevel;
		}
	}

	dml_log_mode_support_params(&context->bw_ctx.dml);

	if (vlevel == context->bw_ctx.dml.soc.num_states)
		goto validate_fail;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *mpo_pipe = pipe->bottom_pipe;

		if (!pipe->stream)
			continue;

		if (vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled
				&& !dc->config.enable_windowed_mpo_odm
				&& pipe->plane_state && mpo_pipe
				&& memcmp(&mpo_pipe->plane_res.scl_data.recout,
						&pipe->plane_res.scl_data.recout,
						sizeof(struct rect)) != 0) {
			ASSERT(mpo_pipe->plane_state != pipe->plane_state);
			goto validate_fail;
		}
		pipe_idx++;
	}

	/* merge pipes if necessary */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		/*skip pipes that don't need merging*/
		if (!merge[i])
			continue;

		/* if ODM merge we ignore mpc tree, mpo pipes will have their own flags */
		if (pipe->prev_odm_pipe) {
			/*split off odm pipe*/
			pipe->prev_odm_pipe->next_odm_pipe = pipe->next_odm_pipe;
			if (pipe->next_odm_pipe)
				pipe->next_odm_pipe->prev_odm_pipe = pipe->prev_odm_pipe;

			/*2:1ODM+MPC Split MPO to Single Pipe + MPC Split MPO*/
			if (pipe->bottom_pipe) {
				if (pipe->bottom_pipe->prev_odm_pipe || pipe->bottom_pipe->next_odm_pipe) {
					/*MPC split rules will handle this case*/
					pipe->bottom_pipe->top_pipe = NULL;
				} else {
					/* when merging an ODM pipes, the bottom MPC pipe must now point to
					 * the previous ODM pipe and its associated stream assets
					 */
					if (pipe->prev_odm_pipe->bottom_pipe) {
						/* 3 plane MPO*/
						pipe->bottom_pipe->top_pipe = pipe->prev_odm_pipe->bottom_pipe;
						pipe->prev_odm_pipe->bottom_pipe->bottom_pipe = pipe->bottom_pipe;
					} else {
						/* 2 plane MPO*/
						pipe->bottom_pipe->top_pipe = pipe->prev_odm_pipe;
						pipe->prev_odm_pipe->bottom_pipe = pipe->bottom_pipe;
					}

					memcpy(&pipe->bottom_pipe->stream_res, &pipe->bottom_pipe->top_pipe->stream_res, sizeof(struct stream_resource));
				}
			}

			if (pipe->top_pipe) {
				pipe->top_pipe->bottom_pipe = NULL;
			}

			pipe->bottom_pipe = NULL;
			pipe->next_odm_pipe = NULL;
			pipe->plane_state = NULL;
			pipe->stream = NULL;
			pipe->top_pipe = NULL;
			pipe->prev_odm_pipe = NULL;
			if (pipe->stream_res.dsc)
				dcn20_release_dsc(&context->res_ctx, dc->res_pool, &pipe->stream_res.dsc);
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
			repopulate_pipes = true;
		} else if (pipe->top_pipe && pipe->top_pipe->plane_state == pipe->plane_state) {
			struct pipe_ctx *top_pipe = pipe->top_pipe;
			struct pipe_ctx *bottom_pipe = pipe->bottom_pipe;

			top_pipe->bottom_pipe = bottom_pipe;
			if (bottom_pipe)
				bottom_pipe->top_pipe = top_pipe;

			pipe->top_pipe = NULL;
			pipe->bottom_pipe = NULL;
			pipe->plane_state = NULL;
			pipe->stream = NULL;
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
			repopulate_pipes = true;
		} else
			ASSERT(0); /* Should never try to merge master pipe */

	}

	for (i = 0, pipe_idx = -1; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *hsplit_pipe = NULL;
		bool odm;
		int old_index = -1;

		if (!pipe->stream || newly_split[i])
			continue;

		pipe_idx++;
		odm = vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled;

		if (!pipe->plane_state && !odm)
			continue;

		if (split[i]) {
			if (odm) {
				if (split[i] == 4 && old_pipe->next_odm_pipe && old_pipe->next_odm_pipe->next_odm_pipe)
					old_index = old_pipe->next_odm_pipe->next_odm_pipe->pipe_idx;
				else if (old_pipe->next_odm_pipe)
					old_index = old_pipe->next_odm_pipe->pipe_idx;
			} else {
				if (split[i] == 4 && old_pipe->bottom_pipe && old_pipe->bottom_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
					old_index = old_pipe->bottom_pipe->bottom_pipe->pipe_idx;
				else if (old_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
					old_index = old_pipe->bottom_pipe->pipe_idx;
			}
			hsplit_pipe = dcn32_find_split_pipe(dc, context, old_index);
			ASSERT(hsplit_pipe);
			if (!hsplit_pipe)
				goto validate_fail;

			if (!dcn32_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					pipe, hsplit_pipe, odm))
				goto validate_fail;

			newly_split[hsplit_pipe->pipe_idx] = true;
			repopulate_pipes = true;
		}
		if (split[i] == 4) {
			struct pipe_ctx *pipe_4to1;

			if (odm && old_pipe->next_odm_pipe)
				old_index = old_pipe->next_odm_pipe->pipe_idx;
			else if (!odm && old_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
				old_index = old_pipe->bottom_pipe->pipe_idx;
			else
				old_index = -1;
			pipe_4to1 = dcn32_find_split_pipe(dc, context, old_index);
			ASSERT(pipe_4to1);
			if (!pipe_4to1)
				goto validate_fail;
			if (!dcn32_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					pipe, pipe_4to1, odm))
				goto validate_fail;
			newly_split[pipe_4to1->pipe_idx] = true;

			if (odm && old_pipe->next_odm_pipe && old_pipe->next_odm_pipe->next_odm_pipe
					&& old_pipe->next_odm_pipe->next_odm_pipe->next_odm_pipe)
				old_index = old_pipe->next_odm_pipe->next_odm_pipe->next_odm_pipe->pipe_idx;
			else if (!odm && old_pipe->bottom_pipe && old_pipe->bottom_pipe->bottom_pipe &&
					old_pipe->bottom_pipe->bottom_pipe->bottom_pipe &&
					old_pipe->bottom_pipe->bottom_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
				old_index = old_pipe->bottom_pipe->bottom_pipe->bottom_pipe->pipe_idx;
			else
				old_index = -1;
			pipe_4to1 = dcn32_find_split_pipe(dc, context, old_index);
			ASSERT(pipe_4to1);
			if (!pipe_4to1)
				goto validate_fail;
			if (!dcn32_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					hsplit_pipe, pipe_4to1, odm))
				goto validate_fail;
			newly_split[pipe_4to1->pipe_idx] = true;
		}
		if (odm)
			dcn20_build_mapped_resource(dc, context, pipe->stream);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state) {
			if (!resource_build_scaling_params(pipe))
				goto validate_fail;
		}
	}

	/* Actual dsc count per stream dsc validation*/
	if (!dcn20_validate_dsc(dc, context)) {
		vba->ValidationStatus[vba->soc.num_states] = DML_FAIL_DSC_VALIDATION_FAILURE;
		goto validate_fail;
	}

	if (repopulate_pipes) {
		pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, fast_validate);

		/* repopulate_pipes = 1 means the pipes were either split or merged. In this case
		 * we have to re-calculate the DET allocation and run through DML once more to
		 * ensure all the params are calculated correctly. We do not need to run the
		 * pipe split check again after this call (pipes are already split / merged).
		 * */
		if (!fast_validate) {
			context->bw_ctx.dml.soc.allow_for_pstate_or_stutter_in_vblank_final =
						dm_prefetch_support_uclk_fclk_and_stutter_if_possible;
			vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);
		}
	}
	*vlevel_out = vlevel;
	*pipe_cnt_out = pipe_cnt;

	out = true;
	goto validate_out;

validate_fail:
	out = false;

validate_out:
	return out;
}


void dcn32_calculate_wm_and_dlg_fpu(struct dc *dc, struct dc_state *context,
				display_e2e_pipe_params_st *pipes,
				int pipe_cnt,
				int vlevel)
{
	int i, pipe_idx, vlevel_temp = 0;
	double dcfclk = dcn3_2_soc.clock_limits[0].dcfclk_mhz;
	double dcfclk_from_validation = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
	double dcfclk_from_fw_based_mclk_switching = dcfclk_from_validation;
	bool pstate_en = context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb] !=
			dm_dram_clock_change_unsupported;
	unsigned int dummy_latency_index = 0;
	int maxMpcComb = context->bw_ctx.dml.vba.maxMpcComb;
	unsigned int min_dram_speed_mts = context->bw_ctx.dml.vba.DRAMSpeed;
	unsigned int min_dram_speed_mts_margin;

	dc_assert_fp_enabled();

	// Override DRAMClockChangeSupport for SubVP + DRR case where the DRR cannot switch without stretching it's VBLANK
	if (!pstate_en && dcn32_subvp_in_use(dc, context)) {
		context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb] = dm_dram_clock_change_vblank_w_mall_sub_vp;
		pstate_en = true;
	}

	context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching = false;

	if (!pstate_en) {
		/* only when the mclk switch can not be natural, is the fw based vblank stretch attempted */
		context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching =
			dcn30_can_support_mclk_switch_using_fw_based_vblank_stretch(dc, context);

		if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) {
			dummy_latency_index = dcn32_find_dummy_latency_index_for_fw_based_mclk_switch(dc,
				context, pipes, pipe_cnt, vlevel);

			/* After calling dcn30_find_dummy_latency_index_for_fw_based_mclk_switch
			 * we reinstate the original dram_clock_change_latency_us on the context
			 * and all variables that may have changed up to this point, except the
			 * newly found dummy_latency_index
			 */
			context->bw_ctx.dml.soc.dram_clock_change_latency_us =
					dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us;
			/* For DCN32/321 need to validate with fclk pstate change latency equal to dummy so
			 * prefetch is scheduled correctly to account for dummy pstate.
			 */
			if (dummy_latency_index == 0)
				context->bw_ctx.dml.soc.fclk_change_latency_us =
						dc->clk_mgr->bw_params->dummy_pstate_table[dummy_latency_index].dummy_pstate_latency_us;
			dcn32_internal_validate_bw(dc, context, pipes, &pipe_cnt, &vlevel, false);
			maxMpcComb = context->bw_ctx.dml.vba.maxMpcComb;
			dcfclk_from_fw_based_mclk_switching = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
			pstate_en = context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][maxMpcComb] !=
					dm_dram_clock_change_unsupported;
		}
	}

	/* Set B:
	 * For Set B calculations use clocks from clock_limits[2] when available i.e. when SMU is present,
	 * otherwise use arbitrary low value from spreadsheet for DCFCLK as lower is safer for watermark
	 * calculations to cover bootup clocks.
	 * DCFCLK: soc.clock_limits[2] when available
	 * UCLK: soc.clock_limits[2] when available
	 */
	if (dcn3_2_soc.num_states > 2) {
		vlevel_temp = 2;
		dcfclk = dcn3_2_soc.clock_limits[2].dcfclk_mhz;
	} else
		dcfclk = 615; //DCFCLK Vmin_lv

	pipes[0].clks_cfg.voltage = vlevel_temp;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel_temp].socclk_mhz;

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.fclk_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.fclk_change_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.b.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.fclk_pstate_change_ns = get_fclk_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.usr_retraining_ns = get_usr_retraining_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	/* Set D:
	 * All clocks min.
	 * DCFCLK: Min, as reported by PM FW when available
	 * UCLK  : Min, as reported by PM FW when available
	 * sr_enter_exit/sr_exit should be lower than used for DRAM (TBD after bringup or later, use as decided in Clk Mgr)
	 */

	if (dcn3_2_soc.num_states > 2) {
		vlevel_temp = 0;
		dcfclk = dc->clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz;
	} else
		dcfclk = 615; //DCFCLK Vmin_lv

	pipes[0].clks_cfg.voltage = vlevel_temp;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel_temp].socclk_mhz;

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.fclk_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.fclk_change_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.d.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.fclk_pstate_change_ns = get_fclk_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.usr_retraining_ns = get_usr_retraining_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	/* Set C, for Dummy P-State:
	 * All clocks min.
	 * DCFCLK: Min, as reported by PM FW, when available
	 * UCLK  : Min,  as reported by PM FW, when available
	 * pstate latency as per UCLK state dummy pstate latency
	 */

	// For Set A and Set C use values from validation
	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk_from_validation;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;

	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) {
		pipes[0].clks_cfg.dcfclk_mhz = dcfclk_from_fw_based_mclk_switching;
	}

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid) {
		min_dram_speed_mts = context->bw_ctx.dml.vba.DRAMSpeed;
		min_dram_speed_mts_margin = 160;

		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
			dc->clk_mgr->bw_params->dummy_pstate_table[0].dummy_pstate_latency_us;

		if (context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][maxMpcComb] ==
			dm_dram_clock_change_unsupported) {
			int min_dram_speed_mts_offset = dc->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_memclk_levels - 1;

			min_dram_speed_mts =
				dc->clk_mgr->bw_params->clk_table.entries[min_dram_speed_mts_offset].memclk_mhz * 16;
		}

		if (!context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) {
			/* find largest table entry that is lower than dram speed,
			 * but lower than DPM0 still uses DPM0
			 */
			for (dummy_latency_index = 3; dummy_latency_index > 0; dummy_latency_index--)
				if (min_dram_speed_mts + min_dram_speed_mts_margin >
					dc->clk_mgr->bw_params->dummy_pstate_table[dummy_latency_index].dram_speed_mts)
					break;
		}

		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
			dc->clk_mgr->bw_params->dummy_pstate_table[dummy_latency_index].dummy_pstate_latency_us;

		context->bw_ctx.dml.soc.fclk_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.fclk_change_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us;
	}

	context->bw_ctx.bw.dcn.watermarks.c.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	/* On DCN32/321, PMFW will set PSTATE_CHANGE_TYPE = 1 (FCLK) for UCLK dummy p-state.
	 * In this case we must program FCLK WM Set C to use the UCLK dummy p-state WM
	 * value.
	 */
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.fclk_pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.usr_retraining_ns = get_usr_retraining_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	if ((!pstate_en) && (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid)) {
		/* The only difference between A and C is p-state latency, if p-state is not supported
		 * with full p-state latency we want to calculate DLG based on dummy p-state latency,
		 * Set A p-state watermark set to 0 on DCN30, when p-state unsupported, for now keep as DCN30.
		 */
		context->bw_ctx.bw.dcn.watermarks.a = context->bw_ctx.bw.dcn.watermarks.c;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = 0;
		/* Calculate FCLK p-state change watermark based on FCLK pstate change latency in case
		 * UCLK p-state is not supported, to avoid underflow in case FCLK pstate is supported
		 */
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.fclk_pstate_change_ns = get_fclk_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	} else {
		/* Set A:
		 * All clocks min.
		 * DCFCLK: Min, as reported by PM FW, when available
		 * UCLK: Min, as reported by PM FW, when available
		 */
		dc->res_pool->funcs->update_soc_for_wm_a(dc, context);
		context->bw_ctx.bw.dcn.watermarks.a.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.fclk_pstate_change_ns = get_fclk_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.usr_retraining_ns = get_usr_retraining_watermark(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	}

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		pipes[pipe_idx].clks_cfg.dispclk_mhz = get_dispclk_calculated(&context->bw_ctx.dml, pipes, pipe_cnt);
		pipes[pipe_idx].clks_cfg.dppclk_mhz = get_dppclk_calculated(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);

		if (dc->config.forced_clocks) {
			pipes[pipe_idx].clks_cfg.dispclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dispclk_mhz;
			pipes[pipe_idx].clks_cfg.dppclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dppclk_mhz;
		}
		if (dc->debug.min_disp_clk_khz > pipes[pipe_idx].clks_cfg.dispclk_mhz * 1000)
			pipes[pipe_idx].clks_cfg.dispclk_mhz = dc->debug.min_disp_clk_khz / 1000.0;
		if (dc->debug.min_dpp_clk_khz > pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			pipes[pipe_idx].clks_cfg.dppclk_mhz = dc->debug.min_dpp_clk_khz / 1000.0;

		pipe_idx++;
	}

	context->perf_params.stutter_period_us = context->bw_ctx.dml.vba.StutterPeriod;

	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching && dummy_latency_index == 0)
		context->bw_ctx.dml.soc.fclk_change_latency_us =
				dc->clk_mgr->bw_params->dummy_pstate_table[dummy_latency_index].dummy_pstate_latency_us;

	dcn32_calculate_dlg_params(dc, context, pipes, pipe_cnt, vlevel);

	if (!pstate_en)
		/* Restore full p-state latency */
		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
				dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us;

	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) {
		dcn30_setup_mclk_switch_using_fw_based_vblank_stretch(dc, context);
		if (dummy_latency_index == 0)
			context->bw_ctx.dml.soc.fclk_change_latency_us =
					dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.fclk_change_latency_us;
	}
}

static void dcn32_get_optimal_dcfclk_fclk_for_uclk(unsigned int uclk_mts,
		unsigned int *optimal_dcfclk,
		unsigned int *optimal_fclk)
{
	double bw_from_dram, bw_from_dram1, bw_from_dram2;

	bw_from_dram1 = uclk_mts * dcn3_2_soc.num_chans *
		dcn3_2_soc.dram_channel_width_bytes * (dcn3_2_soc.max_avg_dram_bw_use_normal_percent / 100);
	bw_from_dram2 = uclk_mts * dcn3_2_soc.num_chans *
		dcn3_2_soc.dram_channel_width_bytes * (dcn3_2_soc.max_avg_sdp_bw_use_normal_percent / 100);

	bw_from_dram = (bw_from_dram1 < bw_from_dram2) ? bw_from_dram1 : bw_from_dram2;

	if (optimal_fclk)
		*optimal_fclk = bw_from_dram /
		(dcn3_2_soc.fabric_datapath_to_dcn_data_return_bytes * (dcn3_2_soc.max_avg_sdp_bw_use_normal_percent / 100));

	if (optimal_dcfclk)
		*optimal_dcfclk =  bw_from_dram /
		(dcn3_2_soc.return_bus_width_bytes * (dcn3_2_soc.max_avg_sdp_bw_use_normal_percent / 100));
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

void dcn32_patch_dpm_table(struct clk_bw_params *bw_params)
{
	int i;
	unsigned int max_dcfclk_mhz = 0, max_dispclk_mhz = 0, max_dppclk_mhz = 0,
			max_phyclk_mhz = 0, max_dtbclk_mhz = 0, max_fclk_mhz = 0, max_uclk_mhz = 0;

	for (i = 0; i < MAX_NUM_DPM_LVL; i++) {
		if (bw_params->clk_table.entries[i].dcfclk_mhz > max_dcfclk_mhz)
			max_dcfclk_mhz = bw_params->clk_table.entries[i].dcfclk_mhz;
		if (bw_params->clk_table.entries[i].fclk_mhz > max_fclk_mhz)
			max_fclk_mhz = bw_params->clk_table.entries[i].fclk_mhz;
		if (bw_params->clk_table.entries[i].memclk_mhz > max_uclk_mhz)
			max_uclk_mhz = bw_params->clk_table.entries[i].memclk_mhz;
		if (bw_params->clk_table.entries[i].dispclk_mhz > max_dispclk_mhz)
			max_dispclk_mhz = bw_params->clk_table.entries[i].dispclk_mhz;
		if (bw_params->clk_table.entries[i].dppclk_mhz > max_dppclk_mhz)
			max_dppclk_mhz = bw_params->clk_table.entries[i].dppclk_mhz;
		if (bw_params->clk_table.entries[i].phyclk_mhz > max_phyclk_mhz)
			max_phyclk_mhz = bw_params->clk_table.entries[i].phyclk_mhz;
		if (bw_params->clk_table.entries[i].dtbclk_mhz > max_dtbclk_mhz)
			max_dtbclk_mhz = bw_params->clk_table.entries[i].dtbclk_mhz;
	}

	/* Scan through clock values we currently have and if they are 0,
	 *  then populate it with dcn3_2_soc.clock_limits[] value.
	 *
	 * Do it for DCFCLK, DISPCLK, DTBCLK and UCLK as any of those being
	 *  0, will cause it to skip building the clock table.
	 */
	if (max_dcfclk_mhz == 0)
		bw_params->clk_table.entries[0].dcfclk_mhz = dcn3_2_soc.clock_limits[0].dcfclk_mhz;
	if (max_dispclk_mhz == 0)
		bw_params->clk_table.entries[0].dispclk_mhz = dcn3_2_soc.clock_limits[0].dispclk_mhz;
	if (max_dtbclk_mhz == 0)
		bw_params->clk_table.entries[0].dtbclk_mhz = dcn3_2_soc.clock_limits[0].dtbclk_mhz;
	if (max_uclk_mhz == 0)
		bw_params->clk_table.entries[0].memclk_mhz = dcn3_2_soc.clock_limits[0].dram_speed_mts / 16;
}

static int build_synthetic_soc_states(struct clk_bw_params *bw_params,
		struct _vcs_dpi_voltage_scaling_st *table, unsigned int *num_entries)
{
	int i, j;
	struct _vcs_dpi_voltage_scaling_st entry = {0};

	unsigned int max_dcfclk_mhz = 0, max_dispclk_mhz = 0, max_dppclk_mhz = 0,
			max_phyclk_mhz = 0, max_dtbclk_mhz = 0, max_fclk_mhz = 0, max_uclk_mhz = 0;

	unsigned int min_dcfclk_mhz = 199, min_fclk_mhz = 299;

	static const unsigned int num_dcfclk_stas = 5;
	unsigned int dcfclk_sta_targets[DC__VOLTAGE_STATES] = {199, 615, 906, 1324, 1564};

	unsigned int num_uclk_dpms = 0;
	unsigned int num_fclk_dpms = 0;
	unsigned int num_dcfclk_dpms = 0;

	for (i = 0; i < MAX_NUM_DPM_LVL; i++) {
		if (bw_params->clk_table.entries[i].dcfclk_mhz > max_dcfclk_mhz)
			max_dcfclk_mhz = bw_params->clk_table.entries[i].dcfclk_mhz;
		if (bw_params->clk_table.entries[i].fclk_mhz > max_fclk_mhz)
			max_fclk_mhz = bw_params->clk_table.entries[i].fclk_mhz;
		if (bw_params->clk_table.entries[i].memclk_mhz > max_uclk_mhz)
			max_uclk_mhz = bw_params->clk_table.entries[i].memclk_mhz;
		if (bw_params->clk_table.entries[i].dispclk_mhz > max_dispclk_mhz)
			max_dispclk_mhz = bw_params->clk_table.entries[i].dispclk_mhz;
		if (bw_params->clk_table.entries[i].dppclk_mhz > max_dppclk_mhz)
			max_dppclk_mhz = bw_params->clk_table.entries[i].dppclk_mhz;
		if (bw_params->clk_table.entries[i].phyclk_mhz > max_phyclk_mhz)
			max_phyclk_mhz = bw_params->clk_table.entries[i].phyclk_mhz;
		if (bw_params->clk_table.entries[i].dtbclk_mhz > max_dtbclk_mhz)
			max_dtbclk_mhz = bw_params->clk_table.entries[i].dtbclk_mhz;

		if (bw_params->clk_table.entries[i].memclk_mhz > 0)
			num_uclk_dpms++;
		if (bw_params->clk_table.entries[i].fclk_mhz > 0)
			num_fclk_dpms++;
		if (bw_params->clk_table.entries[i].dcfclk_mhz > 0)
			num_dcfclk_dpms++;
	}

	if (!max_dcfclk_mhz || !max_dispclk_mhz || !max_dtbclk_mhz)
		return -1;

	if (max_dppclk_mhz == 0)
		max_dppclk_mhz = max_dispclk_mhz;

	if (max_fclk_mhz == 0)
		max_fclk_mhz = max_dcfclk_mhz * dcn3_2_soc.pct_ideal_sdp_bw_after_urgent / dcn3_2_soc.pct_ideal_fabric_bw_after_urgent;

	if (max_phyclk_mhz == 0)
		max_phyclk_mhz = dcn3_2_soc.clock_limits[0].phyclk_mhz;

	*num_entries = 0;
	entry.dispclk_mhz = max_dispclk_mhz;
	entry.dscclk_mhz = max_dispclk_mhz / 3;
	entry.dppclk_mhz = max_dppclk_mhz;
	entry.dtbclk_mhz = max_dtbclk_mhz;
	entry.phyclk_mhz = max_phyclk_mhz;
	entry.phyclk_d18_mhz = dcn3_2_soc.clock_limits[0].phyclk_d18_mhz;
	entry.phyclk_d32_mhz = dcn3_2_soc.clock_limits[0].phyclk_d32_mhz;

	// Insert all the DCFCLK STAs
	for (i = 0; i < num_dcfclk_stas; i++) {
		entry.dcfclk_mhz = dcfclk_sta_targets[i];
		entry.fabricclk_mhz = 0;
		entry.dram_speed_mts = 0;

		insert_entry_into_table_sorted(table, num_entries, &entry);
	}

	// Insert the max DCFCLK
	entry.dcfclk_mhz = max_dcfclk_mhz;
	entry.fabricclk_mhz = 0;
	entry.dram_speed_mts = 0;

	insert_entry_into_table_sorted(table, num_entries, &entry);

	// Insert the UCLK DPMS
	for (i = 0; i < num_uclk_dpms; i++) {
		entry.dcfclk_mhz = 0;
		entry.fabricclk_mhz = 0;
		entry.dram_speed_mts = bw_params->clk_table.entries[i].memclk_mhz * 16;

		insert_entry_into_table_sorted(table, num_entries, &entry);
	}

	// If FCLK is coarse grained, insert individual DPMs.
	if (num_fclk_dpms > 2) {
		for (i = 0; i < num_fclk_dpms; i++) {
			entry.dcfclk_mhz = 0;
			entry.fabricclk_mhz = bw_params->clk_table.entries[i].fclk_mhz;
			entry.dram_speed_mts = 0;

			insert_entry_into_table_sorted(table, num_entries, &entry);
		}
	}
	// If FCLK fine grained, only insert max
	else {
		entry.dcfclk_mhz = 0;
		entry.fabricclk_mhz = max_fclk_mhz;
		entry.dram_speed_mts = 0;

		insert_entry_into_table_sorted(table, num_entries, &entry);
	}

	// At this point, the table contains all "points of interest" based on
	// DPMs from PMFW, and STAs.  Table is sorted by BW, and all clock
	// ratios (by derate, are exact).

	// Remove states that require higher clocks than are supported
	for (i = *num_entries - 1; i >= 0 ; i--) {
		if (table[i].dcfclk_mhz > max_dcfclk_mhz ||
				table[i].fabricclk_mhz > max_fclk_mhz ||
				table[i].dram_speed_mts > max_uclk_mhz * 16)
			remove_entry_from_table_at_index(table, num_entries, i);
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
				break;
			}
		}
	}

	// Round DCFCLKs up to minimum
	for (i = *num_entries - 1; i >= 0 ; i--) {
		if (table[i].dcfclk_mhz < min_dcfclk_mhz) {
			table[i].dcfclk_mhz = min_dcfclk_mhz;
			break;
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

/*
 * dcn32_update_bw_bounding_box
 *
 * This would override some dcn3_2 ip_or_soc initial parameters hardcoded from
 * spreadsheet with actual values as per dGPU SKU:
 * - with passed few options from dc->config
 * - with dentist_vco_frequency from Clk Mgr (currently hardcoded, but might
 *   need to get it from PM FW)
 * - with passed latency values (passed in ns units) in dc-> bb override for
 *   debugging purposes
 * - with passed latencies from VBIOS (in 100_ns units) if available for
 *   certain dGPU SKU
 * - with number of DRAM channels from VBIOS (which differ for certain dGPU SKU
 *   of the same ASIC)
 * - clocks levels with passed clk_table entries from Clk Mgr as reported by PM
 *   FW for different clocks (which might differ for certain dGPU SKU of the
 *   same ASIC)
 */
void dcn32_update_bw_bounding_box_fpu(struct dc *dc, struct clk_bw_params *bw_params)
{
	dc_assert_fp_enabled();

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		/* Overrides from dc->config options */
		dcn3_2_ip.clamp_min_dcfclk = dc->config.clamp_min_dcfclk;

		/* Override from passed dc->bb_overrides if available*/
		if ((int)(dcn3_2_soc.sr_exit_time_us * 1000) != dc->bb_overrides.sr_exit_time_ns
				&& dc->bb_overrides.sr_exit_time_ns) {
			dcn3_2_soc.sr_exit_time_us = dc->bb_overrides.sr_exit_time_ns / 1000.0;
		}

		if ((int)(dcn3_2_soc.sr_enter_plus_exit_time_us * 1000)
				!= dc->bb_overrides.sr_enter_plus_exit_time_ns
				&& dc->bb_overrides.sr_enter_plus_exit_time_ns) {
			dcn3_2_soc.sr_enter_plus_exit_time_us =
				dc->bb_overrides.sr_enter_plus_exit_time_ns / 1000.0;
		}

		if ((int)(dcn3_2_soc.urgent_latency_us * 1000) != dc->bb_overrides.urgent_latency_ns
			&& dc->bb_overrides.urgent_latency_ns) {
			dcn3_2_soc.urgent_latency_us = dc->bb_overrides.urgent_latency_ns / 1000.0;
			dcn3_2_soc.urgent_latency_pixel_data_only_us = dc->bb_overrides.urgent_latency_ns / 1000.0;
		}

		if ((int)(dcn3_2_soc.dram_clock_change_latency_us * 1000)
				!= dc->bb_overrides.dram_clock_change_latency_ns
				&& dc->bb_overrides.dram_clock_change_latency_ns) {
			dcn3_2_soc.dram_clock_change_latency_us =
				dc->bb_overrides.dram_clock_change_latency_ns / 1000.0;
		}

		if ((int)(dcn3_2_soc.fclk_change_latency_us * 1000)
				!= dc->bb_overrides.fclk_clock_change_latency_ns
				&& dc->bb_overrides.fclk_clock_change_latency_ns) {
			dcn3_2_soc.fclk_change_latency_us =
				dc->bb_overrides.fclk_clock_change_latency_ns / 1000;
		}

		if ((int)(dcn3_2_soc.dummy_pstate_latency_us * 1000)
				!= dc->bb_overrides.dummy_clock_change_latency_ns
				&& dc->bb_overrides.dummy_clock_change_latency_ns) {
			dcn3_2_soc.dummy_pstate_latency_us =
				dc->bb_overrides.dummy_clock_change_latency_ns / 1000.0;
		}

		/* Override from VBIOS if VBIOS bb_info available */
		if (dc->ctx->dc_bios->funcs->get_soc_bb_info) {
			struct bp_soc_bb_info bb_info = {0};

			if (dc->ctx->dc_bios->funcs->get_soc_bb_info(dc->ctx->dc_bios, &bb_info) == BP_RESULT_OK) {
				if (bb_info.dram_clock_change_latency_100ns > 0)
					dcn3_2_soc.dram_clock_change_latency_us =
						bb_info.dram_clock_change_latency_100ns * 10;

				if (bb_info.dram_sr_enter_exit_latency_100ns > 0)
					dcn3_2_soc.sr_enter_plus_exit_time_us =
						bb_info.dram_sr_enter_exit_latency_100ns * 10;

				if (bb_info.dram_sr_exit_latency_100ns > 0)
					dcn3_2_soc.sr_exit_time_us =
						bb_info.dram_sr_exit_latency_100ns * 10;
			}
		}

		/* Override from VBIOS for num_chan */
		if (dc->ctx->dc_bios->vram_info.num_chans) {
			dcn3_2_soc.num_chans = dc->ctx->dc_bios->vram_info.num_chans;
			dcn3_2_soc.mall_allocated_for_dcn_mbytes = (double)(dcn32_calc_num_avail_chans_for_mall(dc,
				dc->ctx->dc_bios->vram_info.num_chans) * dc->caps.mall_size_per_mem_channel);
		}

		if (dc->ctx->dc_bios->vram_info.dram_channel_width_bytes)
			dcn3_2_soc.dram_channel_width_bytes = dc->ctx->dc_bios->vram_info.dram_channel_width_bytes;
	}

	/* DML DSC delay factor workaround */
	dcn3_2_ip.dsc_delay_factor_wa = dc->debug.dsc_delay_factor_wa_x1000 / 1000.0;

	dcn3_2_ip.min_prefetch_in_strobe_us = dc->debug.min_prefetch_in_strobe_ns / 1000.0;

	/* Override dispclk_dppclk_vco_speed_mhz from Clk Mgr */
	dcn3_2_soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	dc->dml.soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;

	/* Overrides Clock levelsfrom CLK Mgr table entries as reported by PM FW */
	if ((!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) && (bw_params->clk_table.entries[0].memclk_mhz)) {
		if (dc->debug.use_legacy_soc_bb_mechanism) {
			unsigned int i = 0, j = 0, num_states = 0;

			unsigned int dcfclk_mhz[DC__VOLTAGE_STATES] = {0};
			unsigned int dram_speed_mts[DC__VOLTAGE_STATES] = {0};
			unsigned int optimal_uclk_for_dcfclk_sta_targets[DC__VOLTAGE_STATES] = {0};
			unsigned int optimal_dcfclk_for_uclk[DC__VOLTAGE_STATES] = {0};
			unsigned int min_dcfclk = UINT_MAX;
			/* Set 199 as first value in STA target array to have a minimum DCFCLK value.
			 * For DCN32 we set min to 199 so minimum FCLK DPM0 (300Mhz can be achieved) */
			unsigned int dcfclk_sta_targets[DC__VOLTAGE_STATES] = {199, 615, 906, 1324, 1564};
			unsigned int num_dcfclk_sta_targets = 4, num_uclk_states = 0;
			unsigned int max_dcfclk_mhz = 0, max_dispclk_mhz = 0, max_dppclk_mhz = 0, max_phyclk_mhz = 0;

			for (i = 0; i < MAX_NUM_DPM_LVL; i++) {
				if (bw_params->clk_table.entries[i].dcfclk_mhz > max_dcfclk_mhz)
					max_dcfclk_mhz = bw_params->clk_table.entries[i].dcfclk_mhz;
				if (bw_params->clk_table.entries[i].dcfclk_mhz != 0 &&
						bw_params->clk_table.entries[i].dcfclk_mhz < min_dcfclk)
					min_dcfclk = bw_params->clk_table.entries[i].dcfclk_mhz;
				if (bw_params->clk_table.entries[i].dispclk_mhz > max_dispclk_mhz)
					max_dispclk_mhz = bw_params->clk_table.entries[i].dispclk_mhz;
				if (bw_params->clk_table.entries[i].dppclk_mhz > max_dppclk_mhz)
					max_dppclk_mhz = bw_params->clk_table.entries[i].dppclk_mhz;
				if (bw_params->clk_table.entries[i].phyclk_mhz > max_phyclk_mhz)
					max_phyclk_mhz = bw_params->clk_table.entries[i].phyclk_mhz;
			}
			if (min_dcfclk > dcfclk_sta_targets[0])
				dcfclk_sta_targets[0] = min_dcfclk;
			if (!max_dcfclk_mhz)
				max_dcfclk_mhz = dcn3_2_soc.clock_limits[0].dcfclk_mhz;
			if (!max_dispclk_mhz)
				max_dispclk_mhz = dcn3_2_soc.clock_limits[0].dispclk_mhz;
			if (!max_dppclk_mhz)
				max_dppclk_mhz = dcn3_2_soc.clock_limits[0].dppclk_mhz;
			if (!max_phyclk_mhz)
				max_phyclk_mhz = dcn3_2_soc.clock_limits[0].phyclk_mhz;

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
				dcn32_get_optimal_dcfclk_fclk_for_uclk(bw_params->clk_table.entries[i].memclk_mhz * 16,
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

			dcn3_2_soc.num_states = num_states;
			for (i = 0; i < dcn3_2_soc.num_states; i++) {
				dcn3_2_soc.clock_limits[i].state = i;
				dcn3_2_soc.clock_limits[i].dcfclk_mhz = dcfclk_mhz[i];
				dcn3_2_soc.clock_limits[i].fabricclk_mhz = dcfclk_mhz[i];

				/* Fill all states with max values of all these clocks */
				dcn3_2_soc.clock_limits[i].dispclk_mhz = max_dispclk_mhz;
				dcn3_2_soc.clock_limits[i].dppclk_mhz  = max_dppclk_mhz;
				dcn3_2_soc.clock_limits[i].phyclk_mhz  = max_phyclk_mhz;
				dcn3_2_soc.clock_limits[i].dscclk_mhz  = max_dispclk_mhz / 3;

				/* Populate from bw_params for DTBCLK, SOCCLK */
				if (i > 0) {
					if (!bw_params->clk_table.entries[i].dtbclk_mhz) {
						dcn3_2_soc.clock_limits[i].dtbclk_mhz  = dcn3_2_soc.clock_limits[i-1].dtbclk_mhz;
					} else {
						dcn3_2_soc.clock_limits[i].dtbclk_mhz  = bw_params->clk_table.entries[i].dtbclk_mhz;
					}
				} else if (bw_params->clk_table.entries[i].dtbclk_mhz) {
					dcn3_2_soc.clock_limits[i].dtbclk_mhz  = bw_params->clk_table.entries[i].dtbclk_mhz;
				}

				if (!bw_params->clk_table.entries[i].socclk_mhz && i > 0)
					dcn3_2_soc.clock_limits[i].socclk_mhz = dcn3_2_soc.clock_limits[i-1].socclk_mhz;
				else
					dcn3_2_soc.clock_limits[i].socclk_mhz = bw_params->clk_table.entries[i].socclk_mhz;

				if (!dram_speed_mts[i] && i > 0)
					dcn3_2_soc.clock_limits[i].dram_speed_mts = dcn3_2_soc.clock_limits[i-1].dram_speed_mts;
				else
					dcn3_2_soc.clock_limits[i].dram_speed_mts = dram_speed_mts[i];

				/* These clocks cannot come from bw_params, always fill from dcn3_2_soc[0] */
				/* PHYCLK_D18, PHYCLK_D32 */
				dcn3_2_soc.clock_limits[i].phyclk_d18_mhz = dcn3_2_soc.clock_limits[0].phyclk_d18_mhz;
				dcn3_2_soc.clock_limits[i].phyclk_d32_mhz = dcn3_2_soc.clock_limits[0].phyclk_d32_mhz;
			}
		} else {
			build_synthetic_soc_states(bw_params, dcn3_2_soc.clock_limits, &dcn3_2_soc.num_states);
		}

		/* Re-init DML with updated bb */
		dml_init_instance(&dc->dml, &dcn3_2_soc, &dcn3_2_ip, DML_PROJECT_DCN32);
		if (dc->current_state)
			dml_init_instance(&dc->current_state->bw_ctx.dml, &dcn3_2_soc, &dcn3_2_ip, DML_PROJECT_DCN32);
	}
}

void dcn32_zero_pipe_dcc_fraction(display_e2e_pipe_params_st *pipes,
				  int pipe_cnt)
{
	dc_assert_fp_enabled();

	pipes[pipe_cnt].pipe.src.dcc_fraction_of_zs_req_luma = 0;
	pipes[pipe_cnt].pipe.src.dcc_fraction_of_zs_req_chroma = 0;
}

bool dcn32_allow_subvp_with_active_margin(struct pipe_ctx *pipe)
{
	bool allow = false;
	uint32_t refresh_rate = 0;

	/* Allow subvp on displays that have active margin for 2560x1440@60hz displays
	 * only for now. There must be no scaling as well.
	 *
	 * For now we only enable on 2560x1440@60hz displays to enable 4K60 + 1440p60 configs
	 * for p-state switching.
	 */
	if (pipe->stream && pipe->plane_state) {
		refresh_rate = (pipe->stream->timing.pix_clk_100hz * 100 +
						pipe->stream->timing.v_total * pipe->stream->timing.h_total - 1)
						/ (double)(pipe->stream->timing.v_total * pipe->stream->timing.h_total);
		if (pipe->stream->timing.v_addressable == 1440 &&
				pipe->stream->timing.h_addressable == 2560 &&
				refresh_rate >= 55 && refresh_rate <= 65 &&
				pipe->plane_state->src_rect.height == 1440 &&
				pipe->plane_state->src_rect.width == 2560 &&
				pipe->plane_state->dst_rect.height == 1440 &&
				pipe->plane_state->dst_rect.width == 2560)
			allow = true;
	}
	return allow;
}
