// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "dc_link_dp.h"
#include "dcn20/dcn20_resource.h"

#include "dcn20_fpu.h"

/**
 * DOC: DCN2x FPU manipulation Overview
 *
 * The DCN architecture relies on FPU operations, which require special
 * compilation flags and the use of kernel_fpu_begin/end functions; ideally, we
 * want to avoid spreading FPU access across multiple files. With this idea in
 * mind, this file aims to centralize all DCN20 and DCN2.1 (DCN2x) functions
 * that require FPU access in a single place. Code in this file follows the
 * following code pattern:
 *
 * 1. Functions that use FPU operations should be isolated in static functions.
 * 2. The FPU functions should have the noinline attribute to ensure anything
 *    that deals with FP register is contained within this call.
 * 3. All function that needs to be accessed outside this file requires a
 *    public interface that not uses any FPU reference.
 * 4. Developers **must not** use DC_FP_START/END in this file, but they need
 *    to ensure that the caller invokes it before access any function available
 *    in this file. For this reason, public functions in this file must invoke
 *    dc_assert_fp_enabled();
 *
 * Let's expand a little bit more the idea in the code pattern. To fully
 * isolate FPU operations in a single place, we must avoid situations where
 * compilers spill FP values to registers due to FP enable in a specific C
 * file. Note that even if we isolate all FPU functions in a single file and
 * call its interface from other files, the compiler might enable the use of
 * FPU before we call DC_FP_START. Nevertheless, it is the programmer's
 * responsibility to invoke DC_FP_START/END in the correct place. To highlight
 * situations where developers forgot to use the FP protection before calling
 * the DC FPU interface functions, we introduce a helper that checks if the
 * function is invoked under FP protection. If not, it will trigger a kernel
 * warning.
 */

struct _vcs_dpi_ip_params_st dcn2_0_ip = {
	.odm_capable = 1,
	.gpuvm_enable = 0,
	.hostvm_enable = 0,
	.gpuvm_max_page_table_levels = 4,
	.hostvm_max_page_table_levels = 4,
	.hostvm_cached_page_table_levels = 0,
	.pte_group_size_bytes = 2048,
	.num_dsc = 6,
	.rob_buffer_size_kbytes = 168,
	.det_buffer_size_kbytes = 164,
	.dpte_buffer_size_in_pte_reqs_luma = 84,
	.pde_proc_buffer_size_64k_reqs = 48,
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.pte_chunk_size_kbytes = 2,
	.meta_chunk_size_kbytes = 2,
	.writeback_chunk_size_kbytes = 2,
	.line_buffer_size_bits = 789504,
	.is_line_buffer_bpp_fixed = 0,
	.line_buffer_fixed_bpp = 0,
	.dcc_supported = true,
	.max_line_buffer_lines = 12,
	.writeback_luma_buffer_size_kbytes = 12,
	.writeback_chroma_buffer_size_kbytes = 8,
	.writeback_chroma_line_buffer_width_pixels = 4,
	.writeback_max_hscl_ratio = 1,
	.writeback_max_vscl_ratio = 1,
	.writeback_min_hscl_ratio = 1,
	.writeback_min_vscl_ratio = 1,
	.writeback_max_hscl_taps = 12,
	.writeback_max_vscl_taps = 12,
	.writeback_line_buffer_luma_buffer_size = 0,
	.writeback_line_buffer_chroma_buffer_size = 14643,
	.cursor_buffer_size = 8,
	.cursor_chunk_size = 2,
	.max_num_otg = 6,
	.max_num_dpp = 6,
	.max_num_wb = 1,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 8,
	.max_vscl_ratio = 8,
	.hscl_mults = 4,
	.vscl_mults = 4,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dispclk_ramp_margin_percent = 1,
	.underscan_factor = 1.10,
	.min_vblank_lines = 32, //
	.dppclk_delay_subtotal = 77, //
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_scl = 50,
	.dppclk_delay_cnvc_formatter = 8,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 87, //
	.dcfclk_cstate_latency = 10, // SRExitTime
	.max_inter_dcn_tile_repeaters = 8,
	.xfc_supported = true,
	.xfc_fill_bw_overhead_percent = 10.0,
	.xfc_fill_constant_bytes = 0,
	.number_of_cursors = 1,
};

struct _vcs_dpi_ip_params_st dcn2_0_nv14_ip = {
	.odm_capable = 1,
	.gpuvm_enable = 0,
	.hostvm_enable = 0,
	.gpuvm_max_page_table_levels = 4,
	.hostvm_max_page_table_levels = 4,
	.hostvm_cached_page_table_levels = 0,
	.num_dsc = 5,
	.rob_buffer_size_kbytes = 168,
	.det_buffer_size_kbytes = 164,
	.dpte_buffer_size_in_pte_reqs_luma = 84,
	.dpte_buffer_size_in_pte_reqs_chroma = 42,//todo
	.dpp_output_buffer_pixels = 2560,
	.opp_output_buffer_lines = 1,
	.pixel_chunk_size_kbytes = 8,
	.pte_enable = 1,
	.max_page_table_levels = 4,
	.pte_chunk_size_kbytes = 2,
	.meta_chunk_size_kbytes = 2,
	.writeback_chunk_size_kbytes = 2,
	.line_buffer_size_bits = 789504,
	.is_line_buffer_bpp_fixed = 0,
	.line_buffer_fixed_bpp = 0,
	.dcc_supported = true,
	.max_line_buffer_lines = 12,
	.writeback_luma_buffer_size_kbytes = 12,
	.writeback_chroma_buffer_size_kbytes = 8,
	.writeback_chroma_line_buffer_width_pixels = 4,
	.writeback_max_hscl_ratio = 1,
	.writeback_max_vscl_ratio = 1,
	.writeback_min_hscl_ratio = 1,
	.writeback_min_vscl_ratio = 1,
	.writeback_max_hscl_taps = 12,
	.writeback_max_vscl_taps = 12,
	.writeback_line_buffer_luma_buffer_size = 0,
	.writeback_line_buffer_chroma_buffer_size = 14643,
	.cursor_buffer_size = 8,
	.cursor_chunk_size = 2,
	.max_num_otg = 5,
	.max_num_dpp = 5,
	.max_num_wb = 1,
	.max_dchub_pscl_bw_pix_per_clk = 4,
	.max_pscl_lb_bw_pix_per_clk = 2,
	.max_lb_vscl_bw_pix_per_clk = 4,
	.max_vscl_hscl_bw_pix_per_clk = 4,
	.max_hscl_ratio = 8,
	.max_vscl_ratio = 8,
	.hscl_mults = 4,
	.vscl_mults = 4,
	.max_hscl_taps = 8,
	.max_vscl_taps = 8,
	.dispclk_ramp_margin_percent = 1,
	.underscan_factor = 1.10,
	.min_vblank_lines = 32, //
	.dppclk_delay_subtotal = 77, //
	.dppclk_delay_scl_lb_only = 16,
	.dppclk_delay_scl = 50,
	.dppclk_delay_cnvc_formatter = 8,
	.dppclk_delay_cnvc_cursor = 6,
	.dispclk_delay_subtotal = 87, //
	.dcfclk_cstate_latency = 10, // SRExitTime
	.max_inter_dcn_tile_repeaters = 8,
	.xfc_supported = true,
	.xfc_fill_bw_overhead_percent = 10.0,
	.xfc_fill_constant_bytes = 0,
	.ptoi_supported = 0,
	.number_of_cursors = 1,
};

struct _vcs_dpi_soc_bounding_box_st dcn2_0_soc = {
	/* Defaults that get patched on driver load from firmware. */
	.clock_limits = {
			{
				.state = 0,
				.dcfclk_mhz = 560.0,
				.fabricclk_mhz = 560.0,
				.dispclk_mhz = 513.0,
				.dppclk_mhz = 513.0,
				.phyclk_mhz = 540.0,
				.socclk_mhz = 560.0,
				.dscclk_mhz = 171.0,
				.dram_speed_mts = 8960.0,
			},
			{
				.state = 1,
				.dcfclk_mhz = 694.0,
				.fabricclk_mhz = 694.0,
				.dispclk_mhz = 642.0,
				.dppclk_mhz = 642.0,
				.phyclk_mhz = 600.0,
				.socclk_mhz = 694.0,
				.dscclk_mhz = 214.0,
				.dram_speed_mts = 11104.0,
			},
			{
				.state = 2,
				.dcfclk_mhz = 875.0,
				.fabricclk_mhz = 875.0,
				.dispclk_mhz = 734.0,
				.dppclk_mhz = 734.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 875.0,
				.dscclk_mhz = 245.0,
				.dram_speed_mts = 14000.0,
			},
			{
				.state = 3,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 1000.0,
				.dispclk_mhz = 1100.0,
				.dppclk_mhz = 1100.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1000.0,
				.dscclk_mhz = 367.0,
				.dram_speed_mts = 16000.0,
			},
			{
				.state = 4,
				.dcfclk_mhz = 1200.0,
				.fabricclk_mhz = 1200.0,
				.dispclk_mhz = 1284.0,
				.dppclk_mhz = 1284.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1200.0,
				.dscclk_mhz = 428.0,
				.dram_speed_mts = 16000.0,
			},
			/*Extra state, no dispclk ramping*/
			{
				.state = 5,
				.dcfclk_mhz = 1200.0,
				.fabricclk_mhz = 1200.0,
				.dispclk_mhz = 1284.0,
				.dppclk_mhz = 1284.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1200.0,
				.dscclk_mhz = 428.0,
				.dram_speed_mts = 16000.0,
			},
		},
	.num_states = 5,
	.sr_exit_time_us = 8.6,
	.sr_enter_plus_exit_time_us = 10.9,
	.urgent_latency_us = 4.0,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 40.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 40.0,
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 40.0,
	.max_avg_sdp_bw_use_normal_percent = 40.0,
	.max_avg_dram_bw_use_normal_percent = 40.0,
	.writeback_latency_us = 12.0,
	.ideal_dram_bw_after_urgent_percent = 40.0,
	.max_request_size_bytes = 256,
	.dram_channel_width_bytes = 2,
	.fabric_datapath_to_dcn_data_return_bytes = 64,
	.dcn_downspread_percent = 0.5,
	.downspread_percent = 0.38,
	.dram_page_open_time_ns = 50.0,
	.dram_rw_turnaround_time_ns = 17.5,
	.dram_return_buffer_per_channel_bytes = 8192,
	.round_trip_ping_latency_dcfclk_cycles = 131,
	.urgent_out_of_order_return_per_channel_bytes = 256,
	.channel_interleave_bytes = 256,
	.num_banks = 8,
	.num_chans = 16,
	.vmm_page_size_bytes = 4096,
	.dram_clock_change_latency_us = 404.0,
	.dummy_pstate_latency_us = 5.0,
	.writeback_dram_clock_change_latency_us = 23.0,
	.return_bus_width_bytes = 64,
	.dispclk_dppclk_vco_speed_mhz = 3850,
	.xfc_bus_transport_time_us = 20,
	.xfc_xbuf_latency_tolerance_us = 4,
	.use_urgent_burst_bw = 0
};

struct _vcs_dpi_soc_bounding_box_st dcn2_0_nv14_soc = {
	.clock_limits = {
			{
				.state = 0,
				.dcfclk_mhz = 560.0,
				.fabricclk_mhz = 560.0,
				.dispclk_mhz = 513.0,
				.dppclk_mhz = 513.0,
				.phyclk_mhz = 540.0,
				.socclk_mhz = 560.0,
				.dscclk_mhz = 171.0,
				.dram_speed_mts = 8960.0,
			},
			{
				.state = 1,
				.dcfclk_mhz = 694.0,
				.fabricclk_mhz = 694.0,
				.dispclk_mhz = 642.0,
				.dppclk_mhz = 642.0,
				.phyclk_mhz = 600.0,
				.socclk_mhz = 694.0,
				.dscclk_mhz = 214.0,
				.dram_speed_mts = 11104.0,
			},
			{
				.state = 2,
				.dcfclk_mhz = 875.0,
				.fabricclk_mhz = 875.0,
				.dispclk_mhz = 734.0,
				.dppclk_mhz = 734.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 875.0,
				.dscclk_mhz = 245.0,
				.dram_speed_mts = 14000.0,
			},
			{
				.state = 3,
				.dcfclk_mhz = 1000.0,
				.fabricclk_mhz = 1000.0,
				.dispclk_mhz = 1100.0,
				.dppclk_mhz = 1100.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1000.0,
				.dscclk_mhz = 367.0,
				.dram_speed_mts = 16000.0,
			},
			{
				.state = 4,
				.dcfclk_mhz = 1200.0,
				.fabricclk_mhz = 1200.0,
				.dispclk_mhz = 1284.0,
				.dppclk_mhz = 1284.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1200.0,
				.dscclk_mhz = 428.0,
				.dram_speed_mts = 16000.0,
			},
			/*Extra state, no dispclk ramping*/
			{
				.state = 5,
				.dcfclk_mhz = 1200.0,
				.fabricclk_mhz = 1200.0,
				.dispclk_mhz = 1284.0,
				.dppclk_mhz = 1284.0,
				.phyclk_mhz = 810.0,
				.socclk_mhz = 1200.0,
				.dscclk_mhz = 428.0,
				.dram_speed_mts = 16000.0,
			},
		},
	.num_states = 5,
	.sr_exit_time_us = 11.6,
	.sr_enter_plus_exit_time_us = 13.9,
	.urgent_latency_us = 4.0,
	.urgent_latency_pixel_data_only_us = 4.0,
	.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
	.urgent_latency_vm_data_only_us = 4.0,
	.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
	.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
	.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 40.0,
	.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 40.0,
	.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 40.0,
	.max_avg_sdp_bw_use_normal_percent = 40.0,
	.max_avg_dram_bw_use_normal_percent = 40.0,
	.writeback_latency_us = 12.0,
	.ideal_dram_bw_after_urgent_percent = 40.0,
	.max_request_size_bytes = 256,
	.dram_channel_width_bytes = 2,
	.fabric_datapath_to_dcn_data_return_bytes = 64,
	.dcn_downspread_percent = 0.5,
	.downspread_percent = 0.38,
	.dram_page_open_time_ns = 50.0,
	.dram_rw_turnaround_time_ns = 17.5,
	.dram_return_buffer_per_channel_bytes = 8192,
	.round_trip_ping_latency_dcfclk_cycles = 131,
	.urgent_out_of_order_return_per_channel_bytes = 256,
	.channel_interleave_bytes = 256,
	.num_banks = 8,
	.num_chans = 8,
	.vmm_page_size_bytes = 4096,
	.dram_clock_change_latency_us = 404.0,
	.dummy_pstate_latency_us = 5.0,
	.writeback_dram_clock_change_latency_us = 23.0,
	.return_bus_width_bytes = 64,
	.dispclk_dppclk_vco_speed_mhz = 3850,
	.xfc_bus_transport_time_us = 20,
	.xfc_xbuf_latency_tolerance_us = 4,
	.use_urgent_burst_bw = 0
};

struct _vcs_dpi_soc_bounding_box_st dcn2_0_nv12_soc = { 0 };

#define DC_LOGGER_INIT(logger)

void dcn20_populate_dml_writeback_from_context(struct dc *dc,
					       struct resource_context *res_ctx,
					       display_e2e_pipe_params_st *pipes)
{
	int pipe_cnt, i;

	dc_assert_fp_enabled();

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_writeback_info *wb_info = &res_ctx->pipe_ctx[i].stream->writeback_info[0];

		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		/* Set writeback information */
		pipes[pipe_cnt].dout.wb_enable = (wb_info->wb_enabled == true) ? 1 : 0;
		pipes[pipe_cnt].dout.num_active_wb++;
		pipes[pipe_cnt].dout.wb.wb_src_height = wb_info->dwb_params.cnv_params.crop_height;
		pipes[pipe_cnt].dout.wb.wb_src_width = wb_info->dwb_params.cnv_params.crop_width;
		pipes[pipe_cnt].dout.wb.wb_dst_width = wb_info->dwb_params.dest_width;
		pipes[pipe_cnt].dout.wb.wb_dst_height = wb_info->dwb_params.dest_height;
		pipes[pipe_cnt].dout.wb.wb_htaps_luma = 1;
		pipes[pipe_cnt].dout.wb.wb_vtaps_luma = 1;
		pipes[pipe_cnt].dout.wb.wb_htaps_chroma = wb_info->dwb_params.scaler_taps.h_taps_c;
		pipes[pipe_cnt].dout.wb.wb_vtaps_chroma = wb_info->dwb_params.scaler_taps.v_taps_c;
		pipes[pipe_cnt].dout.wb.wb_hratio = 1.0;
		pipes[pipe_cnt].dout.wb.wb_vratio = 1.0;
		if (wb_info->dwb_params.out_format == dwb_scaler_mode_yuv420) {
			if (wb_info->dwb_params.output_depth == DWB_OUTPUT_PIXEL_DEPTH_8BPC)
				pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_420_8;
			else
				pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_420_10;
		} else {
			pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_444_32;
		}

		pipe_cnt++;
	}
}

void dcn20_fpu_set_wb_arb_params(struct mcif_arb_params *wb_arb_params,
                                struct dc_state *context,
                                display_e2e_pipe_params_st *pipes,
                                int pipe_cnt, int i)
{
       int k;

       dc_assert_fp_enabled();

       for (k = 0; k < sizeof(wb_arb_params->cli_watermark)/sizeof(wb_arb_params->cli_watermark[0]); k++) {
               wb_arb_params->cli_watermark[k] = get_wm_writeback_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
               wb_arb_params->pstate_watermark[k] = get_wm_writeback_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
       }
       wb_arb_params->time_per_pixel = 16.0 * 1000 / (context->res_ctx.pipe_ctx[i].stream->phy_pix_clk / 1000); /* 4 bit fraction, ms */
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

static enum dcn_zstate_support_state  decide_zstate_support(struct dc *dc, struct dc_state *context)
{
	int plane_count;
	int i;

	plane_count = 0;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (context->res_ctx.pipe_ctx[i].plane_state)
			plane_count++;
	}

	/*
	 * Zstate is allowed in following scenarios:
	 * 	1. Single eDP with PSR enabled
	 * 	2. 0 planes (No memory requests)
	 * 	3. Single eDP without PSR but > 5ms stutter period
	 */
	if (plane_count == 0)
		return DCN_ZSTATE_SUPPORT_ALLOW;
	else if (context->stream_count == 1 &&  context->streams[0]->signal == SIGNAL_TYPE_EDP) {
		struct dc_link *link = context->streams[0]->sink->link;

		/* zstate only supported on PWRSEQ0 */
		if (link->link_index != 0)
			return DCN_ZSTATE_SUPPORT_DISALLOW;

		if (context->bw_ctx.dml.vba.StutterPeriod > 5000.0)
			return DCN_ZSTATE_SUPPORT_ALLOW;
		else if (link->psr_settings.psr_version == DC_PSR_VERSION_1 && !dc->debug.disable_psr)
			return DCN_ZSTATE_SUPPORT_ALLOW_Z10_ONLY;
		else
			return DCN_ZSTATE_SUPPORT_DISALLOW;
	} else
		return DCN_ZSTATE_SUPPORT_DISALLOW;
}

void dcn20_calculate_dlg_params(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
	int i, pipe_idx;

	dc_assert_fp_enabled();

	/* Writeback MCIF_WB arbitration parameters */
	dc->res_pool->funcs->set_mcif_arb_params(dc, context, pipes, pipe_cnt);

	context->bw_ctx.bw.dcn.clk.dispclk_khz = context->bw_ctx.dml.vba.DISPCLK * 1000;
	context->bw_ctx.bw.dcn.clk.dcfclk_khz = context->bw_ctx.dml.vba.DCFCLK * 1000;
	context->bw_ctx.bw.dcn.clk.socclk_khz = context->bw_ctx.dml.vba.SOCCLK * 1000;
	context->bw_ctx.bw.dcn.clk.dramclk_khz = context->bw_ctx.dml.vba.DRAMSpeed * 1000 / 16;

	if (dc->debug.min_dram_clk_khz > context->bw_ctx.bw.dcn.clk.dramclk_khz)
		context->bw_ctx.bw.dcn.clk.dramclk_khz = dc->debug.min_dram_clk_khz;

	context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz = context->bw_ctx.dml.vba.DCFCLKDeepSleep * 1000;
	context->bw_ctx.bw.dcn.clk.fclk_khz = context->bw_ctx.dml.vba.FabricClock * 1000;
	context->bw_ctx.bw.dcn.clk.p_state_change_support =
		context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][context->bw_ctx.dml.vba.maxMpcComb]
							!= dm_dram_clock_change_unsupported;
	context->bw_ctx.bw.dcn.clk.dppclk_khz = 0;

	context->bw_ctx.bw.dcn.clk.zstate_support = decide_zstate_support(dc, context);

	context->bw_ctx.bw.dcn.clk.dtbclk_en = is_dtbclk_required(dc, context);

	if (context->bw_ctx.bw.dcn.clk.dispclk_khz < dc->debug.min_disp_clk_khz)
		context->bw_ctx.bw.dcn.clk.dispclk_khz = dc->debug.min_disp_clk_khz;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
		pipes[pipe_idx].pipe.dest.vstartup_start = get_vstartup(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		pipes[pipe_idx].pipe.dest.vupdate_offset = get_vupdate_offset(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		pipes[pipe_idx].pipe.dest.vupdate_width = get_vupdate_width(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		pipes[pipe_idx].pipe.dest.vready_offset = get_vready_offset(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);
		context->res_ctx.pipe_ctx[i].det_buffer_size_kb = context->bw_ctx.dml.ip.det_buffer_size_kbytes;
		context->res_ctx.pipe_ctx[i].unbounded_req = pipes[pipe_idx].pipe.src.unbounded_req_mode;

		if (context->bw_ctx.bw.dcn.clk.dppclk_khz < pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			context->bw_ctx.bw.dcn.clk.dppclk_khz = pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz =
						pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000;
		context->res_ctx.pipe_ctx[i].pipe_dlg_param = pipes[pipe_idx].pipe.dest;
		pipe_idx++;
	}
	/*save a original dppclock copy*/
	context->bw_ctx.bw.dcn.clk.bw_dppclk_khz = context->bw_ctx.bw.dcn.clk.dppclk_khz;
	context->bw_ctx.bw.dcn.clk.bw_dispclk_khz = context->bw_ctx.bw.dcn.clk.dispclk_khz;
	context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dppclk_mhz * 1000;
	context->bw_ctx.bw.dcn.clk.max_supported_dispclk_khz = context->bw_ctx.dml.soc.clock_limits[vlevel].dispclk_mhz * 1000;

	context->bw_ctx.bw.dcn.compbuf_size_kb = context->bw_ctx.dml.ip.config_return_buffer_size_in_kbytes
						- context->bw_ctx.dml.ip.det_buffer_size_kbytes * pipe_idx;

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		bool cstate_en = context->bw_ctx.dml.vba.PrefetchMode[vlevel][context->bw_ctx.dml.vba.maxMpcComb] != 2;

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		if (dc->ctx->dce_version == DCN_VERSION_2_01)
			cstate_en = false;

		context->bw_ctx.dml.funcs.rq_dlg_get_dlg_reg(&context->bw_ctx.dml,
				&context->res_ctx.pipe_ctx[i].dlg_regs,
				&context->res_ctx.pipe_ctx[i].ttu_regs,
				pipes,
				pipe_cnt,
				pipe_idx,
				cstate_en,
				context->bw_ctx.bw.dcn.clk.p_state_change_support,
				false, false, true);

		context->bw_ctx.dml.funcs.rq_dlg_get_rq_reg(&context->bw_ctx.dml,
				&context->res_ctx.pipe_ctx[i].rq_regs,
				&pipes[pipe_idx].pipe);
		pipe_idx++;
	}
}

static void swizzle_to_dml_params(
		enum swizzle_mode_values swizzle,
		unsigned int *sw_mode)
{
	switch (swizzle) {
	case DC_SW_LINEAR:
		*sw_mode = dm_sw_linear;
		break;
	case DC_SW_4KB_S:
		*sw_mode = dm_sw_4kb_s;
		break;
	case DC_SW_4KB_S_X:
		*sw_mode = dm_sw_4kb_s_x;
		break;
	case DC_SW_4KB_D:
		*sw_mode = dm_sw_4kb_d;
		break;
	case DC_SW_4KB_D_X:
		*sw_mode = dm_sw_4kb_d_x;
		break;
	case DC_SW_64KB_S:
		*sw_mode = dm_sw_64kb_s;
		break;
	case DC_SW_64KB_S_X:
		*sw_mode = dm_sw_64kb_s_x;
		break;
	case DC_SW_64KB_S_T:
		*sw_mode = dm_sw_64kb_s_t;
		break;
	case DC_SW_64KB_D:
		*sw_mode = dm_sw_64kb_d;
		break;
	case DC_SW_64KB_D_X:
		*sw_mode = dm_sw_64kb_d_x;
		break;
	case DC_SW_64KB_D_T:
		*sw_mode = dm_sw_64kb_d_t;
		break;
	case DC_SW_64KB_R_X:
		*sw_mode = dm_sw_64kb_r_x;
		break;
	case DC_SW_VAR_S:
		*sw_mode = dm_sw_var_s;
		break;
	case DC_SW_VAR_S_X:
		*sw_mode = dm_sw_var_s_x;
		break;
	case DC_SW_VAR_D:
		*sw_mode = dm_sw_var_d;
		break;
	case DC_SW_VAR_D_X:
		*sw_mode = dm_sw_var_d_x;
		break;
	case DC_SW_VAR_R_X:
		*sw_mode = dm_sw_var_r_x;
		break;
	default:
		ASSERT(0); /* Not supported */
		break;
	}
}

int dcn20_populate_dml_pipes_from_context(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		bool fast_validate)
{
	int pipe_cnt, i;
	bool synchronized_vblank = true;
	struct resource_context *res_ctx = &context->res_ctx;

	dc_assert_fp_enabled();

	for (i = 0, pipe_cnt = -1; i < dc->res_pool->pipe_count; i++) {
		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		if (pipe_cnt < 0) {
			pipe_cnt = i;
			continue;
		}

		if (res_ctx->pipe_ctx[pipe_cnt].stream == res_ctx->pipe_ctx[i].stream)
			continue;

		if (dc->debug.disable_timing_sync ||
			(!resource_are_streams_timing_synchronizable(
				res_ctx->pipe_ctx[pipe_cnt].stream,
				res_ctx->pipe_ctx[i].stream) &&
			!resource_are_vblanks_synchronizable(
				res_ctx->pipe_ctx[pipe_cnt].stream,
				res_ctx->pipe_ctx[i].stream))) {
			synchronized_vblank = false;
			break;
		}
	}

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_crtc_timing *timing = &res_ctx->pipe_ctx[i].stream->timing;
		unsigned int v_total;
		unsigned int front_porch;
		int output_bpc;
		struct audio_check aud_check = {0};

		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		v_total = timing->v_total;
		front_porch = timing->v_front_porch;

		/* todo:
		pipes[pipe_cnt].pipe.src.dynamic_metadata_enable = 0;
		pipes[pipe_cnt].pipe.src.dcc = 0;
		pipes[pipe_cnt].pipe.src.vm = 0;*/

		pipes[pipe_cnt].clks_cfg.refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000.0;

		pipes[pipe_cnt].dout.dsc_enable = res_ctx->pipe_ctx[i].stream->timing.flags.DSC;
		/* todo: rotation?*/
		pipes[pipe_cnt].dout.dsc_slices = res_ctx->pipe_ctx[i].stream->timing.dsc_cfg.num_slices_h;
		if (res_ctx->pipe_ctx[i].stream->use_dynamic_meta) {
			pipes[pipe_cnt].pipe.src.dynamic_metadata_enable = true;
			/* 1/2 vblank */
			pipes[pipe_cnt].pipe.src.dynamic_metadata_lines_before_active =
				(v_total - timing->v_addressable
					- timing->v_border_top - timing->v_border_bottom) / 2;
			/* 36 bytes dp, 32 hdmi */
			pipes[pipe_cnt].pipe.src.dynamic_metadata_xmit_bytes =
				dc_is_dp_signal(res_ctx->pipe_ctx[i].stream->signal) ? 36 : 32;
		}
		pipes[pipe_cnt].pipe.src.dcc = false;
		pipes[pipe_cnt].pipe.src.dcc_rate = 1;
		pipes[pipe_cnt].pipe.dest.synchronized_vblank_all_planes = synchronized_vblank;
		pipes[pipe_cnt].pipe.dest.hblank_start = timing->h_total - timing->h_front_porch;
		pipes[pipe_cnt].pipe.dest.hblank_end = pipes[pipe_cnt].pipe.dest.hblank_start
				- timing->h_addressable
				- timing->h_border_left
				- timing->h_border_right;
		pipes[pipe_cnt].pipe.dest.vblank_start = v_total - front_porch;
		pipes[pipe_cnt].pipe.dest.vblank_end = pipes[pipe_cnt].pipe.dest.vblank_start
				- timing->v_addressable
				- timing->v_border_top
				- timing->v_border_bottom;
		pipes[pipe_cnt].pipe.dest.htotal = timing->h_total;
		pipes[pipe_cnt].pipe.dest.vtotal = v_total;
		pipes[pipe_cnt].pipe.dest.hactive =
			timing->h_addressable + timing->h_border_left + timing->h_border_right;
		pipes[pipe_cnt].pipe.dest.vactive =
			timing->v_addressable + timing->v_border_top + timing->v_border_bottom;
		pipes[pipe_cnt].pipe.dest.interlaced = timing->flags.INTERLACE;
		pipes[pipe_cnt].pipe.dest.pixel_rate_mhz = timing->pix_clk_100hz/10000.0;
		if (timing->timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
			pipes[pipe_cnt].pipe.dest.pixel_rate_mhz *= 2;
		pipes[pipe_cnt].pipe.dest.otg_inst = res_ctx->pipe_ctx[i].stream_res.tg->inst;
		pipes[pipe_cnt].dout.dp_lanes = 4;
		pipes[pipe_cnt].dout.is_virtual = 0;
		pipes[pipe_cnt].pipe.dest.vtotal_min = res_ctx->pipe_ctx[i].stream->adjust.v_total_min;
		pipes[pipe_cnt].pipe.dest.vtotal_max = res_ctx->pipe_ctx[i].stream->adjust.v_total_max;
		switch (get_num_odm_splits(&res_ctx->pipe_ctx[i])) {
		case 1:
			pipes[pipe_cnt].pipe.dest.odm_combine = dm_odm_combine_mode_2to1;
			break;
		case 3:
			pipes[pipe_cnt].pipe.dest.odm_combine = dm_odm_combine_mode_4to1;
			break;
		default:
			pipes[pipe_cnt].pipe.dest.odm_combine = dm_odm_combine_mode_disabled;
		}
		pipes[pipe_cnt].pipe.src.hsplit_grp = res_ctx->pipe_ctx[i].pipe_idx;
		if (res_ctx->pipe_ctx[i].top_pipe && res_ctx->pipe_ctx[i].top_pipe->plane_state
				== res_ctx->pipe_ctx[i].plane_state) {
			struct pipe_ctx *first_pipe = res_ctx->pipe_ctx[i].top_pipe;
			int split_idx = 0;

			while (first_pipe->top_pipe && first_pipe->top_pipe->plane_state
					== res_ctx->pipe_ctx[i].plane_state) {
				first_pipe = first_pipe->top_pipe;
				split_idx++;
			}
			/* Treat 4to1 mpc combine as an mpo of 2 2-to-1 combines */
			if (split_idx == 0)
				pipes[pipe_cnt].pipe.src.hsplit_grp = first_pipe->pipe_idx;
			else if (split_idx == 1)
				pipes[pipe_cnt].pipe.src.hsplit_grp = res_ctx->pipe_ctx[i].pipe_idx;
			else if (split_idx == 2)
				pipes[pipe_cnt].pipe.src.hsplit_grp = res_ctx->pipe_ctx[i].top_pipe->pipe_idx;
		} else if (res_ctx->pipe_ctx[i].prev_odm_pipe) {
			struct pipe_ctx *first_pipe = res_ctx->pipe_ctx[i].prev_odm_pipe;

			while (first_pipe->prev_odm_pipe)
				first_pipe = first_pipe->prev_odm_pipe;
			pipes[pipe_cnt].pipe.src.hsplit_grp = first_pipe->pipe_idx;
		}

		switch (res_ctx->pipe_ctx[i].stream->signal) {
		case SIGNAL_TYPE_DISPLAY_PORT_MST:
		case SIGNAL_TYPE_DISPLAY_PORT:
			pipes[pipe_cnt].dout.output_type = dm_dp;
			break;
		case SIGNAL_TYPE_EDP:
			pipes[pipe_cnt].dout.output_type = dm_edp;
			break;
		case SIGNAL_TYPE_HDMI_TYPE_A:
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
			pipes[pipe_cnt].dout.output_type = dm_hdmi;
			break;
		default:
			/* In case there is no signal, set dp with 4 lanes to allow max config */
			pipes[pipe_cnt].dout.is_virtual = 1;
			pipes[pipe_cnt].dout.output_type = dm_dp;
			pipes[pipe_cnt].dout.dp_lanes = 4;
		}

		switch (res_ctx->pipe_ctx[i].stream->timing.display_color_depth) {
		case COLOR_DEPTH_666:
			output_bpc = 6;
			break;
		case COLOR_DEPTH_888:
			output_bpc = 8;
			break;
		case COLOR_DEPTH_101010:
			output_bpc = 10;
			break;
		case COLOR_DEPTH_121212:
			output_bpc = 12;
			break;
		case COLOR_DEPTH_141414:
			output_bpc = 14;
			break;
		case COLOR_DEPTH_161616:
			output_bpc = 16;
			break;
		case COLOR_DEPTH_999:
			output_bpc = 9;
			break;
		case COLOR_DEPTH_111111:
			output_bpc = 11;
			break;
		default:
			output_bpc = 8;
			break;
		}

		switch (res_ctx->pipe_ctx[i].stream->timing.pixel_encoding) {
		case PIXEL_ENCODING_RGB:
		case PIXEL_ENCODING_YCBCR444:
			pipes[pipe_cnt].dout.output_format = dm_444;
			pipes[pipe_cnt].dout.output_bpp = output_bpc * 3;
			break;
		case PIXEL_ENCODING_YCBCR420:
			pipes[pipe_cnt].dout.output_format = dm_420;
			pipes[pipe_cnt].dout.output_bpp = (output_bpc * 3.0) / 2;
			break;
		case PIXEL_ENCODING_YCBCR422:
			if (res_ctx->pipe_ctx[i].stream->timing.flags.DSC &&
			    !res_ctx->pipe_ctx[i].stream->timing.dsc_cfg.ycbcr422_simple)
				pipes[pipe_cnt].dout.output_format = dm_n422;
			else
				pipes[pipe_cnt].dout.output_format = dm_s422;
			pipes[pipe_cnt].dout.output_bpp = output_bpc * 2;
			break;
		default:
			pipes[pipe_cnt].dout.output_format = dm_444;
			pipes[pipe_cnt].dout.output_bpp = output_bpc * 3;
		}

		if (res_ctx->pipe_ctx[i].stream->timing.flags.DSC)
			pipes[pipe_cnt].dout.output_bpp = res_ctx->pipe_ctx[i].stream->timing.dsc_cfg.bits_per_pixel / 16.0;

		/* todo: default max for now, until there is logic reflecting this in dc*/
		pipes[pipe_cnt].dout.dsc_input_bpc = 12;
		/*fill up the audio sample rate (unit in kHz)*/
		get_audio_check(&res_ctx->pipe_ctx[i].stream->audio_info, &aud_check);
		pipes[pipe_cnt].dout.max_audio_sample_rate = aud_check.max_audiosample_rate / 1000;
		/*
		 * For graphic plane, cursor number is 1, nv12 is 0
		 * bw calculations due to cursor on/off
		 */
		if (res_ctx->pipe_ctx[i].plane_state &&
				res_ctx->pipe_ctx[i].plane_state->address.type == PLN_ADDR_TYPE_VIDEO_PROGRESSIVE)
			pipes[pipe_cnt].pipe.src.num_cursors = 0;
		else
			pipes[pipe_cnt].pipe.src.num_cursors = dc->dml.ip.number_of_cursors;

		pipes[pipe_cnt].pipe.src.cur0_src_width = 256;
		pipes[pipe_cnt].pipe.src.cur0_bpp = dm_cur_32bit;

		if (!res_ctx->pipe_ctx[i].plane_state) {
			pipes[pipe_cnt].pipe.src.is_hsplit = pipes[pipe_cnt].pipe.dest.odm_combine != dm_odm_combine_mode_disabled;
			pipes[pipe_cnt].pipe.src.source_scan = dm_horz;
			pipes[pipe_cnt].pipe.src.sw_mode = dm_sw_4kb_s;
			pipes[pipe_cnt].pipe.src.macro_tile_size = dm_64k_tile;
			pipes[pipe_cnt].pipe.src.viewport_width = timing->h_addressable;
			if (pipes[pipe_cnt].pipe.src.viewport_width > 1920)
				pipes[pipe_cnt].pipe.src.viewport_width = 1920;
			pipes[pipe_cnt].pipe.src.viewport_height = timing->v_addressable;
			if (pipes[pipe_cnt].pipe.src.viewport_height > 1080)
				pipes[pipe_cnt].pipe.src.viewport_height = 1080;
			pipes[pipe_cnt].pipe.src.surface_height_y = pipes[pipe_cnt].pipe.src.viewport_height;
			pipes[pipe_cnt].pipe.src.surface_width_y = pipes[pipe_cnt].pipe.src.viewport_width;
			pipes[pipe_cnt].pipe.src.surface_height_c = pipes[pipe_cnt].pipe.src.viewport_height;
			pipes[pipe_cnt].pipe.src.surface_width_c = pipes[pipe_cnt].pipe.src.viewport_width;
			pipes[pipe_cnt].pipe.src.data_pitch = ((pipes[pipe_cnt].pipe.src.viewport_width + 255) / 256) * 256;
			pipes[pipe_cnt].pipe.src.source_format = dm_444_32;
			pipes[pipe_cnt].pipe.dest.recout_width = pipes[pipe_cnt].pipe.src.viewport_width; /*vp_width/hratio*/
			pipes[pipe_cnt].pipe.dest.recout_height = pipes[pipe_cnt].pipe.src.viewport_height; /*vp_height/vratio*/
			pipes[pipe_cnt].pipe.dest.full_recout_width = pipes[pipe_cnt].pipe.dest.recout_width;  /*when is_hsplit != 1*/
			pipes[pipe_cnt].pipe.dest.full_recout_height = pipes[pipe_cnt].pipe.dest.recout_height; /*when is_hsplit != 1*/
			pipes[pipe_cnt].pipe.scale_ratio_depth.lb_depth = dm_lb_16;
			pipes[pipe_cnt].pipe.scale_ratio_depth.hscl_ratio = 1.0;
			pipes[pipe_cnt].pipe.scale_ratio_depth.vscl_ratio = 1.0;
			pipes[pipe_cnt].pipe.scale_ratio_depth.scl_enable = 0; /*Lb only or Full scl*/
			pipes[pipe_cnt].pipe.scale_taps.htaps = 1;
			pipes[pipe_cnt].pipe.scale_taps.vtaps = 1;
			pipes[pipe_cnt].pipe.dest.vtotal_min = v_total;
			pipes[pipe_cnt].pipe.dest.vtotal_max = v_total;

			if (pipes[pipe_cnt].pipe.dest.odm_combine == dm_odm_combine_mode_2to1) {
				pipes[pipe_cnt].pipe.src.viewport_width /= 2;
				pipes[pipe_cnt].pipe.dest.recout_width /= 2;
			} else if (pipes[pipe_cnt].pipe.dest.odm_combine == dm_odm_combine_mode_4to1) {
				pipes[pipe_cnt].pipe.src.viewport_width /= 4;
				pipes[pipe_cnt].pipe.dest.recout_width /= 4;
			}
		} else {
			struct dc_plane_state *pln = res_ctx->pipe_ctx[i].plane_state;
			struct scaler_data *scl = &res_ctx->pipe_ctx[i].plane_res.scl_data;

			pipes[pipe_cnt].pipe.src.immediate_flip = pln->flip_immediate;
			pipes[pipe_cnt].pipe.src.is_hsplit = (res_ctx->pipe_ctx[i].bottom_pipe && res_ctx->pipe_ctx[i].bottom_pipe->plane_state == pln)
					|| (res_ctx->pipe_ctx[i].top_pipe && res_ctx->pipe_ctx[i].top_pipe->plane_state == pln)
					|| pipes[pipe_cnt].pipe.dest.odm_combine != dm_odm_combine_mode_disabled;

			/* stereo is not split */
			if (pln->stereo_format == PLANE_STEREO_FORMAT_SIDE_BY_SIDE ||
			    pln->stereo_format == PLANE_STEREO_FORMAT_TOP_AND_BOTTOM) {
				pipes[pipe_cnt].pipe.src.is_hsplit = false;
				pipes[pipe_cnt].pipe.src.hsplit_grp = res_ctx->pipe_ctx[i].pipe_idx;
			}

			pipes[pipe_cnt].pipe.src.source_scan = pln->rotation == ROTATION_ANGLE_90
					|| pln->rotation == ROTATION_ANGLE_270 ? dm_vert : dm_horz;
			pipes[pipe_cnt].pipe.src.viewport_y_y = scl->viewport.y;
			pipes[pipe_cnt].pipe.src.viewport_y_c = scl->viewport_c.y;
			pipes[pipe_cnt].pipe.src.viewport_width = scl->viewport.width;
			pipes[pipe_cnt].pipe.src.viewport_width_c = scl->viewport_c.width;
			pipes[pipe_cnt].pipe.src.viewport_height = scl->viewport.height;
			pipes[pipe_cnt].pipe.src.viewport_height_c = scl->viewport_c.height;
			pipes[pipe_cnt].pipe.src.viewport_width_max = pln->src_rect.width;
			pipes[pipe_cnt].pipe.src.viewport_height_max = pln->src_rect.height;
			pipes[pipe_cnt].pipe.src.surface_width_y = pln->plane_size.surface_size.width;
			pipes[pipe_cnt].pipe.src.surface_height_y = pln->plane_size.surface_size.height;
			pipes[pipe_cnt].pipe.src.surface_width_c = pln->plane_size.chroma_size.width;
			pipes[pipe_cnt].pipe.src.surface_height_c = pln->plane_size.chroma_size.height;
			if (pln->format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA
					|| pln->format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
				pipes[pipe_cnt].pipe.src.data_pitch = pln->plane_size.surface_pitch;
				pipes[pipe_cnt].pipe.src.data_pitch_c = pln->plane_size.chroma_pitch;
				pipes[pipe_cnt].pipe.src.meta_pitch = pln->dcc.meta_pitch;
				pipes[pipe_cnt].pipe.src.meta_pitch_c = pln->dcc.meta_pitch_c;
			} else {
				pipes[pipe_cnt].pipe.src.data_pitch = pln->plane_size.surface_pitch;
				pipes[pipe_cnt].pipe.src.meta_pitch = pln->dcc.meta_pitch;
			}
			pipes[pipe_cnt].pipe.src.dcc = pln->dcc.enable;
			pipes[pipe_cnt].pipe.dest.recout_width = scl->recout.width;
			pipes[pipe_cnt].pipe.dest.recout_height = scl->recout.height;
			pipes[pipe_cnt].pipe.dest.full_recout_height = scl->recout.height;
			pipes[pipe_cnt].pipe.dest.full_recout_width = scl->recout.width;
			if (pipes[pipe_cnt].pipe.dest.odm_combine == dm_odm_combine_mode_2to1)
				pipes[pipe_cnt].pipe.dest.full_recout_width *= 2;
			else if (pipes[pipe_cnt].pipe.dest.odm_combine == dm_odm_combine_mode_4to1)
				pipes[pipe_cnt].pipe.dest.full_recout_width *= 4;
			else {
				struct pipe_ctx *split_pipe = res_ctx->pipe_ctx[i].bottom_pipe;

				while (split_pipe && split_pipe->plane_state == pln) {
					pipes[pipe_cnt].pipe.dest.full_recout_width += split_pipe->plane_res.scl_data.recout.width;
					split_pipe = split_pipe->bottom_pipe;
				}
				split_pipe = res_ctx->pipe_ctx[i].top_pipe;
				while (split_pipe && split_pipe->plane_state == pln) {
					pipes[pipe_cnt].pipe.dest.full_recout_width += split_pipe->plane_res.scl_data.recout.width;
					split_pipe = split_pipe->top_pipe;
				}
			}

			pipes[pipe_cnt].pipe.scale_ratio_depth.lb_depth = dm_lb_16;
			pipes[pipe_cnt].pipe.scale_ratio_depth.hscl_ratio = (double) scl->ratios.horz.value / (1ULL<<32);
			pipes[pipe_cnt].pipe.scale_ratio_depth.hscl_ratio_c = (double) scl->ratios.horz_c.value / (1ULL<<32);
			pipes[pipe_cnt].pipe.scale_ratio_depth.vscl_ratio = (double) scl->ratios.vert.value / (1ULL<<32);
			pipes[pipe_cnt].pipe.scale_ratio_depth.vscl_ratio_c = (double) scl->ratios.vert_c.value / (1ULL<<32);
			pipes[pipe_cnt].pipe.scale_ratio_depth.scl_enable =
					scl->ratios.vert.value != dc_fixpt_one.value
					|| scl->ratios.horz.value != dc_fixpt_one.value
					|| scl->ratios.vert_c.value != dc_fixpt_one.value
					|| scl->ratios.horz_c.value != dc_fixpt_one.value /*Lb only or Full scl*/
					|| dc->debug.always_scale; /*support always scale*/
			pipes[pipe_cnt].pipe.scale_taps.htaps = scl->taps.h_taps;
			pipes[pipe_cnt].pipe.scale_taps.htaps_c = scl->taps.h_taps_c;
			pipes[pipe_cnt].pipe.scale_taps.vtaps = scl->taps.v_taps;
			pipes[pipe_cnt].pipe.scale_taps.vtaps_c = scl->taps.v_taps_c;

			pipes[pipe_cnt].pipe.src.macro_tile_size =
					swizzle_mode_to_macro_tile_size(pln->tiling_info.gfx9.swizzle);
			swizzle_to_dml_params(pln->tiling_info.gfx9.swizzle,
					&pipes[pipe_cnt].pipe.src.sw_mode);

			switch (pln->format) {
			case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
			case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
				pipes[pipe_cnt].pipe.src.source_format = dm_420_8;
				break;
			case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
			case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
				pipes[pipe_cnt].pipe.src.source_format = dm_420_10;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
			case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
			case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
				pipes[pipe_cnt].pipe.src.source_format = dm_444_64;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
			case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
				pipes[pipe_cnt].pipe.src.source_format = dm_444_16;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
				pipes[pipe_cnt].pipe.src.source_format = dm_444_8;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
				pipes[pipe_cnt].pipe.src.source_format = dm_rgbe_alpha;
				break;
			default:
				pipes[pipe_cnt].pipe.src.source_format = dm_444_32;
				break;
			}
		}

		pipe_cnt++;
	}

	/* populate writeback information */
	DC_FP_START();
	dc->res_pool->funcs->populate_dml_writeback_from_context(dc, res_ctx, pipes);
	DC_FP_END();

	return pipe_cnt;
}

void dcn20_calculate_wm(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *out_pipe_cnt,
		int *pipe_split_from,
		int vlevel,
		bool fast_validate)
{
	int pipe_cnt, i, pipe_idx;

	dc_assert_fp_enabled();

	for (i = 0, pipe_idx = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		pipes[pipe_cnt].clks_cfg.refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000.0;
		pipes[pipe_cnt].clks_cfg.dispclk_mhz = context->bw_ctx.dml.vba.RequiredDISPCLK[vlevel][context->bw_ctx.dml.vba.maxMpcComb];

		if (pipe_split_from[i] < 0) {
			pipes[pipe_cnt].clks_cfg.dppclk_mhz =
					context->bw_ctx.dml.vba.RequiredDPPCLK[vlevel][context->bw_ctx.dml.vba.maxMpcComb][pipe_idx];
			if (context->bw_ctx.dml.vba.BlendingAndTiming[pipe_idx] == pipe_idx)
				pipes[pipe_cnt].pipe.dest.odm_combine =
						context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_idx];
			else
				pipes[pipe_cnt].pipe.dest.odm_combine = 0;
			pipe_idx++;
		} else {
			pipes[pipe_cnt].clks_cfg.dppclk_mhz =
					context->bw_ctx.dml.vba.RequiredDPPCLK[vlevel][context->bw_ctx.dml.vba.maxMpcComb][pipe_split_from[i]];
			if (context->bw_ctx.dml.vba.BlendingAndTiming[pipe_split_from[i]] == pipe_split_from[i])
				pipes[pipe_cnt].pipe.dest.odm_combine =
						context->bw_ctx.dml.vba.ODMCombineEnabled[pipe_split_from[i]];
			else
				pipes[pipe_cnt].pipe.dest.odm_combine = 0;
		}

		if (dc->config.forced_clocks) {
			pipes[pipe_cnt].clks_cfg.dispclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dispclk_mhz;
			pipes[pipe_cnt].clks_cfg.dppclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dppclk_mhz;
		}
		if (dc->debug.min_disp_clk_khz > pipes[pipe_cnt].clks_cfg.dispclk_mhz * 1000)
			pipes[pipe_cnt].clks_cfg.dispclk_mhz = dc->debug.min_disp_clk_khz / 1000.0;
		if (dc->debug.min_dpp_clk_khz > pipes[pipe_cnt].clks_cfg.dppclk_mhz * 1000)
			pipes[pipe_cnt].clks_cfg.dppclk_mhz = dc->debug.min_dpp_clk_khz / 1000.0;

		pipe_cnt++;
	}

	if (pipe_cnt != pipe_idx) {
		if (dc->res_pool->funcs->populate_dml_pipes)
			pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc,
				context, pipes, fast_validate);
		else
			pipe_cnt = dcn20_populate_dml_pipes_from_context(dc,
				context, pipes, fast_validate);
	}

	*out_pipe_cnt = pipe_cnt;

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].dcfclk_mhz;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;

	/* only pipe 0 is read for voltage and dcf/soc clocks */
	if (vlevel < 1) {
		pipes[0].clks_cfg.voltage = 1;
		pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[1].dcfclk_mhz;
		pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[1].socclk_mhz;
	}
	context->bw_ctx.bw.dcn.watermarks.b.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	if (vlevel < 2) {
		pipes[0].clks_cfg.voltage = 2;
		pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[2].dcfclk_mhz;
		pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[2].socclk_mhz;
	}
	context->bw_ctx.bw.dcn.watermarks.c.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	if (vlevel < 3) {
		pipes[0].clks_cfg.voltage = 3;
		pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[2].dcfclk_mhz;
		pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[2].socclk_mhz;
	}
	context->bw_ctx.bw.dcn.watermarks.d.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].dcfclk_mhz;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;
	context->bw_ctx.bw.dcn.watermarks.a.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
}

void dcn20_update_bounding_box(struct dc *dc, struct _vcs_dpi_soc_bounding_box_st *bb,
		struct pp_smu_nv_clock_table *max_clocks, unsigned int *uclk_states, unsigned int num_states)
{
	struct _vcs_dpi_voltage_scaling_st calculated_states[DC__VOLTAGE_STATES];
	int i;
	int num_calculated_states = 0;
	int min_dcfclk = 0;

	dc_assert_fp_enabled();

	if (num_states == 0)
		return;

	memset(calculated_states, 0, sizeof(calculated_states));

	if (dc->bb_overrides.min_dcfclk_mhz > 0)
		min_dcfclk = dc->bb_overrides.min_dcfclk_mhz;
	else {
		if (ASICREV_IS_NAVI12_P(dc->ctx->asic_id.hw_internal_rev))
			min_dcfclk = 310;
		else
			// Accounting for SOC/DCF relationship, we can go as high as
			// 506Mhz in Vmin.
			min_dcfclk = 506;
	}

	for (i = 0; i < num_states; i++) {
		int min_fclk_required_by_uclk;
		calculated_states[i].state = i;
		calculated_states[i].dram_speed_mts = uclk_states[i] * 16 / 1000;

		// FCLK:UCLK ratio is 1.08
		min_fclk_required_by_uclk = div_u64(((unsigned long long)uclk_states[i]) * 1080,
			1000000);

		calculated_states[i].fabricclk_mhz = (min_fclk_required_by_uclk < min_dcfclk) ?
				min_dcfclk : min_fclk_required_by_uclk;

		calculated_states[i].socclk_mhz = (calculated_states[i].fabricclk_mhz > max_clocks->socClockInKhz / 1000) ?
				max_clocks->socClockInKhz / 1000 : calculated_states[i].fabricclk_mhz;

		calculated_states[i].dcfclk_mhz = (calculated_states[i].fabricclk_mhz > max_clocks->dcfClockInKhz / 1000) ?
				max_clocks->dcfClockInKhz / 1000 : calculated_states[i].fabricclk_mhz;

		calculated_states[i].dispclk_mhz = max_clocks->displayClockInKhz / 1000;
		calculated_states[i].dppclk_mhz = max_clocks->displayClockInKhz / 1000;
		calculated_states[i].dscclk_mhz = max_clocks->displayClockInKhz / (1000 * 3);

		calculated_states[i].phyclk_mhz = max_clocks->phyClockInKhz / 1000;

		num_calculated_states++;
	}

	calculated_states[num_calculated_states - 1].socclk_mhz = max_clocks->socClockInKhz / 1000;
	calculated_states[num_calculated_states - 1].fabricclk_mhz = max_clocks->socClockInKhz / 1000;
	calculated_states[num_calculated_states - 1].dcfclk_mhz = max_clocks->dcfClockInKhz / 1000;

	memcpy(bb->clock_limits, calculated_states, sizeof(bb->clock_limits));
	bb->num_states = num_calculated_states;

	// Duplicate the last state, DML always an extra state identical to max state to work
	memcpy(&bb->clock_limits[num_calculated_states], &bb->clock_limits[num_calculated_states - 1], sizeof(struct _vcs_dpi_voltage_scaling_st));
	bb->clock_limits[num_calculated_states].state = bb->num_states;
}

void dcn20_cap_soc_clocks(
		struct _vcs_dpi_soc_bounding_box_st *bb,
		struct pp_smu_nv_clock_table max_clocks)
{
	int i;

	dc_assert_fp_enabled();

	// First pass - cap all clocks higher than the reported max
	for (i = 0; i < bb->num_states; i++) {
		if ((bb->clock_limits[i].dcfclk_mhz > (max_clocks.dcfClockInKhz / 1000))
				&& max_clocks.dcfClockInKhz != 0)
			bb->clock_limits[i].dcfclk_mhz = (max_clocks.dcfClockInKhz / 1000);

		if ((bb->clock_limits[i].dram_speed_mts > (max_clocks.uClockInKhz / 1000) * 16)
						&& max_clocks.uClockInKhz != 0)
			bb->clock_limits[i].dram_speed_mts = (max_clocks.uClockInKhz / 1000) * 16;

		if ((bb->clock_limits[i].fabricclk_mhz > (max_clocks.fabricClockInKhz / 1000))
						&& max_clocks.fabricClockInKhz != 0)
			bb->clock_limits[i].fabricclk_mhz = (max_clocks.fabricClockInKhz / 1000);

		if ((bb->clock_limits[i].dispclk_mhz > (max_clocks.displayClockInKhz / 1000))
						&& max_clocks.displayClockInKhz != 0)
			bb->clock_limits[i].dispclk_mhz = (max_clocks.displayClockInKhz / 1000);

		if ((bb->clock_limits[i].dppclk_mhz > (max_clocks.dppClockInKhz / 1000))
						&& max_clocks.dppClockInKhz != 0)
			bb->clock_limits[i].dppclk_mhz = (max_clocks.dppClockInKhz / 1000);

		if ((bb->clock_limits[i].phyclk_mhz > (max_clocks.phyClockInKhz / 1000))
						&& max_clocks.phyClockInKhz != 0)
			bb->clock_limits[i].phyclk_mhz = (max_clocks.phyClockInKhz / 1000);

		if ((bb->clock_limits[i].socclk_mhz > (max_clocks.socClockInKhz / 1000))
						&& max_clocks.socClockInKhz != 0)
			bb->clock_limits[i].socclk_mhz = (max_clocks.socClockInKhz / 1000);

		if ((bb->clock_limits[i].dscclk_mhz > (max_clocks.dscClockInKhz / 1000))
						&& max_clocks.dscClockInKhz != 0)
			bb->clock_limits[i].dscclk_mhz = (max_clocks.dscClockInKhz / 1000);
	}

	// Second pass - remove all duplicate clock states
	for (i = bb->num_states - 1; i > 1; i--) {
		bool duplicate = true;

		if (bb->clock_limits[i-1].dcfclk_mhz != bb->clock_limits[i].dcfclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].dispclk_mhz != bb->clock_limits[i].dispclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].dppclk_mhz != bb->clock_limits[i].dppclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].dram_speed_mts != bb->clock_limits[i].dram_speed_mts)
			duplicate = false;
		if (bb->clock_limits[i-1].dscclk_mhz != bb->clock_limits[i].dscclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].fabricclk_mhz != bb->clock_limits[i].fabricclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].phyclk_mhz != bb->clock_limits[i].phyclk_mhz)
			duplicate = false;
		if (bb->clock_limits[i-1].socclk_mhz != bb->clock_limits[i].socclk_mhz)
			duplicate = false;

		if (duplicate)
			bb->num_states--;
	}
}

void dcn20_patch_bounding_box(struct dc *dc, struct _vcs_dpi_soc_bounding_box_st *bb)
{
	dc_assert_fp_enabled();

	if ((int)(bb->sr_exit_time_us * 1000) != dc->bb_overrides.sr_exit_time_ns
			&& dc->bb_overrides.sr_exit_time_ns) {
		bb->sr_exit_time_us = dc->bb_overrides.sr_exit_time_ns / 1000.0;
	}

	if ((int)(bb->sr_enter_plus_exit_time_us * 1000)
				!= dc->bb_overrides.sr_enter_plus_exit_time_ns
			&& dc->bb_overrides.sr_enter_plus_exit_time_ns) {
		bb->sr_enter_plus_exit_time_us =
				dc->bb_overrides.sr_enter_plus_exit_time_ns / 1000.0;
	}

	if ((int)(bb->urgent_latency_us * 1000) != dc->bb_overrides.urgent_latency_ns
			&& dc->bb_overrides.urgent_latency_ns) {
		bb->urgent_latency_us = dc->bb_overrides.urgent_latency_ns / 1000.0;
	}

	if ((int)(bb->dram_clock_change_latency_us * 1000)
				!= dc->bb_overrides.dram_clock_change_latency_ns
			&& dc->bb_overrides.dram_clock_change_latency_ns) {
		bb->dram_clock_change_latency_us =
				dc->bb_overrides.dram_clock_change_latency_ns / 1000.0;
	}

	if ((int)(bb->dummy_pstate_latency_us * 1000)
				!= dc->bb_overrides.dummy_clock_change_latency_ns
			&& dc->bb_overrides.dummy_clock_change_latency_ns) {
		bb->dummy_pstate_latency_us =
				dc->bb_overrides.dummy_clock_change_latency_ns / 1000.0;
	}
}

static bool dcn20_validate_bandwidth_internal(struct dc *dc, struct dc_state *context,
		bool fast_validate)
{
	bool out = false;

	BW_VAL_TRACE_SETUP();

	int vlevel = 0;
	int pipe_split_from[MAX_PIPES];
	int pipe_cnt = 0;
	display_e2e_pipe_params_st *pipes = kzalloc(dc->res_pool->pipe_count * sizeof(display_e2e_pipe_params_st), GFP_ATOMIC);
	DC_LOGGER_INIT(dc->ctx->logger);

	BW_VAL_TRACE_COUNT();

	out = dcn20_fast_validate_bw(dc, context, pipes, &pipe_cnt, pipe_split_from, &vlevel, fast_validate);

	if (pipe_cnt == 0)
		goto validate_out;

	if (!out)
		goto validate_fail;

	BW_VAL_TRACE_END_VOLTAGE_LEVEL();

	if (fast_validate) {
		BW_VAL_TRACE_SKIP(fast);
		goto validate_out;
	}

	dcn20_calculate_wm(dc, context, pipes, &pipe_cnt, pipe_split_from, vlevel, fast_validate);
	dcn20_calculate_dlg_params(dc, context, pipes, pipe_cnt, vlevel);

	BW_VAL_TRACE_END_WATERMARKS();

	goto validate_out;

validate_fail:
	DC_LOG_WARNING("Mode Validation Warning: %s failed validation.\n",
		dml_get_status_message(context->bw_ctx.dml.vba.ValidationStatus[context->bw_ctx.dml.vba.soc.num_states]));

	BW_VAL_TRACE_SKIP(fail);
	out = false;

validate_out:
	kfree(pipes);

	BW_VAL_TRACE_FINISH();

	return out;
}

bool dcn20_validate_bandwidth_fp(struct dc *dc,
                                struct dc_state *context,
                                bool fast_validate)
{
       bool voltage_supported = false;
       bool full_pstate_supported = false;
       bool dummy_pstate_supported = false;
       double p_state_latency_us;

       dc_assert_fp_enabled();

       p_state_latency_us = context->bw_ctx.dml.soc.dram_clock_change_latency_us;
       context->bw_ctx.dml.soc.disable_dram_clock_change_vactive_support =
               dc->debug.disable_dram_clock_change_vactive_support;
       context->bw_ctx.dml.soc.allow_dram_clock_one_display_vactive =
               dc->debug.enable_dram_clock_change_one_display_vactive;

       /*Unsafe due to current pipe merge and split logic*/
       ASSERT(context != dc->current_state);

       if (fast_validate) {
               return dcn20_validate_bandwidth_internal(dc, context, true);
       }

       // Best case, we support full UCLK switch latency
       voltage_supported = dcn20_validate_bandwidth_internal(dc, context, false);
       full_pstate_supported = context->bw_ctx.bw.dcn.clk.p_state_change_support;

       if (context->bw_ctx.dml.soc.dummy_pstate_latency_us == 0 ||
               (voltage_supported && full_pstate_supported)) {
               context->bw_ctx.bw.dcn.clk.p_state_change_support = full_pstate_supported;
               goto restore_dml_state;
       }

       // Fallback: Try to only support G6 temperature read latency
       context->bw_ctx.dml.soc.dram_clock_change_latency_us = context->bw_ctx.dml.soc.dummy_pstate_latency_us;

       voltage_supported = dcn20_validate_bandwidth_internal(dc, context, false);
       dummy_pstate_supported = context->bw_ctx.bw.dcn.clk.p_state_change_support;

       if (voltage_supported && (dummy_pstate_supported || !(context->stream_count))) {
               context->bw_ctx.bw.dcn.clk.p_state_change_support = false;
               goto restore_dml_state;
       }

       // ERROR: fallback is supposed to always work.
       ASSERT(false);

restore_dml_state:
       context->bw_ctx.dml.soc.dram_clock_change_latency_us = p_state_latency_us;
       return voltage_supported;
}

void dcn20_fpu_set_wm_ranges(int i,
                            struct pp_smu_wm_range_sets *ranges,
                            struct _vcs_dpi_soc_bounding_box_st *loaded_bb)
{
       dc_assert_fp_enabled();

       ranges->reader_wm_sets[i].min_fill_clk_mhz = (i > 0) ? (loaded_bb->clock_limits[i - 1].dram_speed_mts / 16) + 1 : 0;
       ranges->reader_wm_sets[i].max_fill_clk_mhz = loaded_bb->clock_limits[i].dram_speed_mts / 16;
}

void dcn20_fpu_adjust_dppclk(struct vba_vars_st *v,
                            int vlevel,
                            int max_mpc_comb,
                            int pipe_idx,
                            bool is_validating_bw)
{
       dc_assert_fp_enabled();

       if (is_validating_bw)
               v->RequiredDPPCLK[vlevel][max_mpc_comb][pipe_idx] *= 2;
       else
               v->RequiredDPPCLK[vlevel][max_mpc_comb][pipe_idx] /= 2;
}
