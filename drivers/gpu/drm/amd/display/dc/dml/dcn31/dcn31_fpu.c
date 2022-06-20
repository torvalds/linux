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

#include "dml/dcn20/dcn20_fpu.h"
#include "dcn31_fpu.h"

/**
 * DOC: DCN31x FPU manipulation Overview
 *
 * The DCN architecture relies on FPU operations, which require special
 * compilation flags and the use of kernel_fpu_begin/end functions; ideally, we
 * want to avoid spreading FPU access across multiple files. With this idea in
 * mind, this file aims to centralize all DCN3.1.x functions that require FPU
 * access in a single place. Code in this file follows the following code
 * pattern:
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
 */

struct _vcs_dpi_ip_params_st dcn3_1_ip = {
	.gpuvm_enable = 1,
	.gpuvm_max_page_table_levels = 1,
	.hostvm_enable = 1,
	.hostvm_max_page_table_levels = 2,
	.rob_buffer_size_kbytes = 64,
	.det_buffer_size_kbytes = DCN3_1_DEFAULT_DET_SIZE,
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
	.num_dsc = 3,
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

struct _vcs_dpi_soc_bounding_box_st dcn3_1_soc = {
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

struct _vcs_dpi_ip_params_st dcn3_15_ip = {
	.gpuvm_enable = 1,
	.gpuvm_max_page_table_levels = 1,
	.hostvm_enable = 1,
	.hostvm_max_page_table_levels = 2,
	.rob_buffer_size_kbytes = 64,
	.det_buffer_size_kbytes = DCN3_15_DEFAULT_DET_SIZE,
	.min_comp_buffer_size_kbytes = DCN3_15_MIN_COMPBUF_SIZE_KB,
	.config_return_buffer_size_in_kbytes = 1024,
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
	.num_dsc = 3,
	.maximum_dsc_bits_per_component = 10,
	.dsc422_native_support = false,
	.is_line_buffer_bpp_fixed = true,
	.line_buffer_fixed_bpp = 49,
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
	.max_inter_dcn_tile_repeaters = 9,
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

struct _vcs_dpi_soc_bounding_box_st dcn3_15_soc = {
	.sr_exit_time_us = 9.0,
	.sr_enter_plus_exit_time_us = 11.0,
	.sr_exit_z8_time_us = 50.0,
	.sr_enter_plus_exit_z8_time_us = 50.0,
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
	.dcn_downspread_percent = 0.38,
	.gpuvm_min_page_size_bytes = 4096,
	.hostvm_min_page_size_bytes = 4096,
	.do_urgent_latency_adjustment = false,
	.urgent_latency_adjustment_fabric_clock_component_us = 0,
	.urgent_latency_adjustment_fabric_clock_reference_mhz = 0,
};

struct _vcs_dpi_ip_params_st dcn3_16_ip = {
	.gpuvm_enable = 1,
	.gpuvm_max_page_table_levels = 1,
	.hostvm_enable = 1,
	.hostvm_max_page_table_levels = 2,
	.rob_buffer_size_kbytes = 64,
	.det_buffer_size_kbytes = DCN3_16_DEFAULT_DET_SIZE,
	.config_return_buffer_size_in_kbytes = 1024,
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
	.num_dsc = 3,
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

struct _vcs_dpi_soc_bounding_box_st dcn3_16_soc = {
		/*TODO: correct dispclk/dppclk voltage level determination*/
	.clock_limits = {
		{
			.state = 0,
			.dispclk_mhz = 556.0,
			.dppclk_mhz = 556.0,
			.phyclk_mhz = 600.0,
			.phyclk_d18_mhz = 445.0,
			.dscclk_mhz = 186.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 1,
			.dispclk_mhz = 625.0,
			.dppclk_mhz = 625.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 209.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 2,
			.dispclk_mhz = 625.0,
			.dppclk_mhz = 625.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 209.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 3,
			.dispclk_mhz = 1112.0,
			.dppclk_mhz = 1112.0,
			.phyclk_mhz = 810.0,
			.phyclk_d18_mhz = 667.0,
			.dscclk_mhz = 371.0,
			.dtbclk_mhz = 625.0,
		},
		{
			.state = 4,
			.dispclk_mhz = 1250.0,
			.dppclk_mhz = 1250.0,
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

void dcn31_calculate_wm_and_dlg_fp(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
	int i, pipe_idx;
	double dcfclk = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];

	dc_assert_fp_enabled();

	if (context->bw_ctx.dml.soc.min_dcfclk > dcfclk)
		dcfclk = context->bw_ctx.dml.soc.min_dcfclk;

	/* We don't recalculate clocks for 0 pipe configs, which can block
	 * S0i3 as high clocks will block low power states
	 * Override any clocks that can block S0i3 to min here
	 */
	if (pipe_cnt == 0) {
		context->bw_ctx.bw.dcn.clk.dcfclk_khz = dcfclk; // always should be vlevel 0
		return;
	}

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;

#if 0 // TODO
	/* Set B:
	 * TODO
	 */
	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].valid) {
		if (vlevel == 0) {
			pipes[0].clks_cfg.voltage = 1;
			pipes[0].clks_cfg.dcfclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dcfclk_mhz;
		}
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.b.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_z8_ns = get_wm_z8_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_exit_z8_ns = get_wm_z8_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;

	/* Set C:
	 * TODO
	 */
	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.c.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_z8_ns = get_wm_z8_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_exit_z8_ns = get_wm_z8_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.c.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	/* Set D:
	 * TODO
	 */
	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us;
	}
	context->bw_ctx.bw.dcn.watermarks.d.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_z8_ns = get_wm_z8_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_exit_z8_ns = get_wm_z8_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.d.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
#endif

	/* Set A:
	 * All clocks min required
	 *
	 * Set A calculated last so that following calculations are based on Set A
	 */
	dc->res_pool->funcs->update_soc_for_wm_a(dc, context);
	context->bw_ctx.bw.dcn.watermarks.a.urgent_ns = get_wm_urgent(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns = get_wm_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns = get_wm_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_z8_ns = get_wm_z8_stutter_enter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_z8_ns = get_wm_z8_stutter_exit(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	/* TODO: remove: */
	context->bw_ctx.bw.dcn.watermarks.b = context->bw_ctx.bw.dcn.watermarks.a;
	context->bw_ctx.bw.dcn.watermarks.c = context->bw_ctx.bw.dcn.watermarks.a;
	context->bw_ctx.bw.dcn.watermarks.d = context->bw_ctx.bw.dcn.watermarks.a;
	/* end remove*/

	for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		pipes[pipe_idx].clks_cfg.dispclk_mhz = get_dispclk_calculated(&context->bw_ctx.dml, pipes, pipe_cnt);
		pipes[pipe_idx].clks_cfg.dppclk_mhz = get_dppclk_calculated(&context->bw_ctx.dml, pipes, pipe_cnt, pipe_idx);

		if (dc->config.forced_clocks || dc->debug.max_disp_clk) {
			pipes[pipe_idx].clks_cfg.dispclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dispclk_mhz;
			pipes[pipe_idx].clks_cfg.dppclk_mhz = context->bw_ctx.dml.soc.clock_limits[0].dppclk_mhz;
		}
		if (dc->debug.min_disp_clk_khz > pipes[pipe_idx].clks_cfg.dispclk_mhz * 1000)
			pipes[pipe_idx].clks_cfg.dispclk_mhz = dc->debug.min_disp_clk_khz / 1000.0;
		if (dc->debug.min_dpp_clk_khz > pipes[pipe_idx].clks_cfg.dppclk_mhz * 1000)
			pipes[pipe_idx].clks_cfg.dppclk_mhz = dc->debug.min_dpp_clk_khz / 1000.0;

		pipe_idx++;
	}

	dcn20_calculate_dlg_params(dc, context, pipes, pipe_cnt, vlevel);
}

void dcn31_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	struct clk_limit_table *clk_table = &bw_params->clk_table;
	struct _vcs_dpi_voltage_scaling_st clock_limits[DC__VOLTAGE_STATES];
	unsigned int i, closest_clk_lvl;
	int j;

	dc_assert_fp_enabled();

	// Default clock levels are used for diags, which may lead to overclocking.
	if (!IS_DIAG_DC(dc->ctx->dce_environment)) {
		int max_dispclk_mhz = 0, max_dppclk_mhz = 0;

		dcn3_1_ip.max_num_otg = dc->res_pool->res_cap->num_timing_generator;
		dcn3_1_ip.max_num_dpp = dc->res_pool->pipe_count;
		dcn3_1_soc.num_chans = bw_params->num_channels;

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
			for (closest_clk_lvl = 0, j = dcn3_1_soc.num_states - 1; j >= 0; j--) {
				if ((unsigned int) dcn3_1_soc.clock_limits[j].dcfclk_mhz <= clk_table->entries[i].dcfclk_mhz) {
					closest_clk_lvl = j;
					break;
				}
			}

			clock_limits[i].state = i;

			/* Clocks dependent on voltage level. */
			clock_limits[i].dcfclk_mhz = clk_table->entries[i].dcfclk_mhz;
			clock_limits[i].fabricclk_mhz = clk_table->entries[i].fclk_mhz;
			clock_limits[i].socclk_mhz = clk_table->entries[i].socclk_mhz;
			clock_limits[i].dram_speed_mts = clk_table->entries[i].memclk_mhz * 2 * clk_table->entries[i].wck_ratio;

			/* Clocks independent of voltage level. */
			clock_limits[i].dispclk_mhz = max_dispclk_mhz ? max_dispclk_mhz :
				dcn3_1_soc.clock_limits[closest_clk_lvl].dispclk_mhz;

			clock_limits[i].dppclk_mhz = max_dppclk_mhz ? max_dppclk_mhz :
				dcn3_1_soc.clock_limits[closest_clk_lvl].dppclk_mhz;

			clock_limits[i].dram_bw_per_chan_gbps = dcn3_1_soc.clock_limits[closest_clk_lvl].dram_bw_per_chan_gbps;
			clock_limits[i].dscclk_mhz = dcn3_1_soc.clock_limits[closest_clk_lvl].dscclk_mhz;
			clock_limits[i].dtbclk_mhz = dcn3_1_soc.clock_limits[closest_clk_lvl].dtbclk_mhz;
			clock_limits[i].phyclk_d18_mhz = dcn3_1_soc.clock_limits[closest_clk_lvl].phyclk_d18_mhz;
			clock_limits[i].phyclk_mhz = dcn3_1_soc.clock_limits[closest_clk_lvl].phyclk_mhz;
		}
		for (i = 0; i < clk_table->num_entries; i++)
			dcn3_1_soc.clock_limits[i] = clock_limits[i];
		if (clk_table->num_entries) {
			dcn3_1_soc.num_states = clk_table->num_entries;
		}
	}

	dcn3_1_soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	dc->dml.soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		dml_init_instance(&dc->dml, &dcn3_1_soc, &dcn3_1_ip, DML_PROJECT_DCN31);
	else
		dml_init_instance(&dc->dml, &dcn3_1_soc, &dcn3_1_ip, DML_PROJECT_DCN31_FPGA);
}

void dcn315_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	struct clk_limit_table *clk_table = &bw_params->clk_table;
	int i, max_dispclk_mhz = 0, max_dppclk_mhz = 0;

	dc_assert_fp_enabled();

	dcn3_15_ip.max_num_otg = dc->res_pool->res_cap->num_timing_generator;
	dcn3_15_ip.max_num_dpp = dc->res_pool->pipe_count;
	dcn3_15_soc.num_chans = bw_params->num_channels;

	ASSERT(clk_table->num_entries);

	/* Setup soc to always use max dispclk/dppclk to avoid odm-to-lower-voltage */
	for (i = 0; i < clk_table->num_entries; ++i) {
		if (clk_table->entries[i].dispclk_mhz > max_dispclk_mhz)
			max_dispclk_mhz = clk_table->entries[i].dispclk_mhz;
		if (clk_table->entries[i].dppclk_mhz > max_dppclk_mhz)
			max_dppclk_mhz = clk_table->entries[i].dppclk_mhz;
	}

	for (i = 0; i < clk_table->num_entries; i++) {
		dcn3_15_soc.clock_limits[i].state = i;

		/* Clocks dependent on voltage level. */
		dcn3_15_soc.clock_limits[i].dcfclk_mhz = clk_table->entries[i].dcfclk_mhz;
		dcn3_15_soc.clock_limits[i].fabricclk_mhz = clk_table->entries[i].fclk_mhz;
		dcn3_15_soc.clock_limits[i].socclk_mhz = clk_table->entries[i].socclk_mhz;
		dcn3_15_soc.clock_limits[i].dram_speed_mts = clk_table->entries[i].memclk_mhz * 2 * clk_table->entries[i].wck_ratio;

		/* These aren't actually read from smu, but rather set in clk_mgr defaults */
		dcn3_15_soc.clock_limits[i].dtbclk_mhz = clk_table->entries[i].dtbclk_mhz;
		dcn3_15_soc.clock_limits[i].phyclk_d18_mhz = clk_table->entries[i].phyclk_d18_mhz;
		dcn3_15_soc.clock_limits[i].phyclk_mhz = clk_table->entries[i].phyclk_mhz;

		/* Clocks independent of voltage level. */
		dcn3_15_soc.clock_limits[i].dispclk_mhz = max_dispclk_mhz;
		dcn3_15_soc.clock_limits[i].dppclk_mhz = max_dppclk_mhz;
		dcn3_15_soc.clock_limits[i].dscclk_mhz = max_dispclk_mhz / 3.0;
	}
	dcn3_15_soc.num_states = clk_table->num_entries;


	/* Set vco to max_dispclk * 2 to make sure the highest dispclk is always available for dml calcs,
	 * no impact outside of dml validation
	 */
	dcn3_15_soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		dml_init_instance(&dc->dml, &dcn3_15_soc, &dcn3_15_ip, DML_PROJECT_DCN31);
	else
		dml_init_instance(&dc->dml, &dcn3_15_soc, &dcn3_15_ip, DML_PROJECT_DCN31_FPGA);
}

void dcn316_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	struct clk_limit_table *clk_table = &bw_params->clk_table;
	struct _vcs_dpi_voltage_scaling_st clock_limits[DC__VOLTAGE_STATES];
	unsigned int i, closest_clk_lvl;
	int max_dispclk_mhz = 0, max_dppclk_mhz = 0;
	int j;

	dc_assert_fp_enabled();

	// Default clock levels are used for diags, which may lead to overclocking.
	if (!IS_DIAG_DC(dc->ctx->dce_environment)) {

		dcn3_16_ip.max_num_otg = dc->res_pool->res_cap->num_timing_generator;
		dcn3_16_ip.max_num_dpp = dc->res_pool->pipe_count;
		dcn3_16_soc.num_chans = bw_params->num_channels;

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
			for (closest_clk_lvl = 0, j = dcn3_16_soc.num_states - 1; j >= 0; j--) {
				if ((unsigned int) dcn3_16_soc.clock_limits[j].dcfclk_mhz <= clk_table->entries[i].dcfclk_mhz) {
					closest_clk_lvl = j;
					break;
				}
			}
			// Ported from DCN315
			if (clk_table->num_entries == 1) {
				/*smu gives one DPM level, let's take the highest one*/
				closest_clk_lvl = dcn3_16_soc.num_states - 1;
			}

			clock_limits[i].state = i;

			/* Clocks dependent on voltage level. */
			clock_limits[i].dcfclk_mhz = clk_table->entries[i].dcfclk_mhz;
			if (clk_table->num_entries == 1 &&
				clock_limits[i].dcfclk_mhz < dcn3_16_soc.clock_limits[closest_clk_lvl].dcfclk_mhz) {
				/*SMU fix not released yet*/
				clock_limits[i].dcfclk_mhz = dcn3_16_soc.clock_limits[closest_clk_lvl].dcfclk_mhz;
			}
			clock_limits[i].fabricclk_mhz = clk_table->entries[i].fclk_mhz;
			clock_limits[i].socclk_mhz = clk_table->entries[i].socclk_mhz;
			clock_limits[i].dram_speed_mts = clk_table->entries[i].memclk_mhz * 2 * clk_table->entries[i].wck_ratio;

			/* Clocks independent of voltage level. */
			clock_limits[i].dispclk_mhz = max_dispclk_mhz ? max_dispclk_mhz :
				dcn3_16_soc.clock_limits[closest_clk_lvl].dispclk_mhz;

			clock_limits[i].dppclk_mhz = max_dppclk_mhz ? max_dppclk_mhz :
				dcn3_16_soc.clock_limits[closest_clk_lvl].dppclk_mhz;

			clock_limits[i].dram_bw_per_chan_gbps = dcn3_16_soc.clock_limits[closest_clk_lvl].dram_bw_per_chan_gbps;
			clock_limits[i].dscclk_mhz = dcn3_16_soc.clock_limits[closest_clk_lvl].dscclk_mhz;
			clock_limits[i].dtbclk_mhz = dcn3_16_soc.clock_limits[closest_clk_lvl].dtbclk_mhz;
			clock_limits[i].phyclk_d18_mhz = dcn3_16_soc.clock_limits[closest_clk_lvl].phyclk_d18_mhz;
			clock_limits[i].phyclk_mhz = dcn3_16_soc.clock_limits[closest_clk_lvl].phyclk_mhz;
		}
		for (i = 0; i < clk_table->num_entries; i++)
			dcn3_16_soc.clock_limits[i] = clock_limits[i];
		if (clk_table->num_entries) {
			dcn3_16_soc.num_states = clk_table->num_entries;
		}
	}

	if (max_dispclk_mhz) {
		dcn3_16_soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;
		dc->dml.soc.dispclk_dppclk_vco_speed_mhz = max_dispclk_mhz * 2;
	}

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		dml_init_instance(&dc->dml, &dcn3_16_soc, &dcn3_16_ip, DML_PROJECT_DCN31);
	else
		dml_init_instance(&dc->dml, &dcn3_16_soc, &dcn3_16_ip, DML_PROJECT_DCN31_FPGA);
}
