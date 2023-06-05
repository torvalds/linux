/*
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
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
#include "reg_helper.h"
#include "dcn_calc_math.h"
#include "dcn20/dcn20_resource.h"
#include "dcn30/dcn30_resource.h"

#include "clk_mgr/dcn30/dcn30_smu11_driver_if.h"
#include "display_mode_vba_30.h"
#include "dcn30_fpu.h"

#define REG(reg)\
	optc1->tg_regs->reg

#define CTX \
	optc1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	optc1->tg_shift->field_name, optc1->tg_mask->field_name


struct _vcs_dpi_ip_params_st dcn3_0_ip = {
	.use_min_dcfclk = 0,
	.clamp_min_dcfclk = 0,
	.odm_capable = 1,
	.gpuvm_enable = 0,
	.hostvm_enable = 0,
	.gpuvm_max_page_table_levels = 4,
	.hostvm_max_page_table_levels = 4,
	.hostvm_cached_page_table_levels = 0,
	.pte_group_size_bytes = 2048,
	.num_dsc = 6,
	.rob_buffer_size_kbytes = 184,
	.det_buffer_size_kbytes = 184,
	.dpte_buffer_size_in_pte_reqs_luma = 84,
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
	.max_num_otg = 6,
	.max_num_dpp = 6,
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
	.odm_combine_4to1_supported = true,

	.xfc_supported = false,
	.xfc_fill_bw_overhead_percent = 10.0,
	.xfc_fill_constant_bytes = 0,
	.gfx7_compat_tiling_supported = 0,
	.number_of_cursors = 1,
};

struct _vcs_dpi_soc_bounding_box_st dcn3_0_soc = {
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
	.sr_exit_time_us = 15.5,
	.sr_enter_plus_exit_time_us = 20,
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
	.round_trip_ping_latency_dcfclk_cycles = 191,
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


void optc3_fpu_set_vrr_m_const(struct timing_generator *optc,
		double vtotal_avg)
{
	struct optc *optc1 = DCN10TG_FROM_TG(optc);
	double vtotal_min, vtotal_max;
	double ratio, modulo, phase;
	uint32_t vblank_start;
	uint32_t v_total_mask_value = 0;

	dc_assert_fp_enabled();

	/* Compute VTOTAL_MIN and VTOTAL_MAX, so that
	 * VOTAL_MAX - VTOTAL_MIN = 1
	 */
	v_total_mask_value = 16;
	vtotal_min = dcn_bw_floor(vtotal_avg);
	vtotal_max = dcn_bw_ceil(vtotal_avg);

	/* Check that bottom VBLANK is at least 2 lines tall when running with
	 * VTOTAL_MIN. Note that VTOTAL registers are defined as 'total number
	 * of lines in a frame - 1'.
	 */
	REG_GET(OTG_V_BLANK_START_END, OTG_V_BLANK_START,
		&vblank_start);
	ASSERT(vtotal_min >= vblank_start + 1);

	/* Special case where the average frame rate can be achieved
	 * without using the DTO
	 */
	if (vtotal_min == vtotal_max) {
		REG_SET(OTG_V_TOTAL, 0, OTG_V_TOTAL, (uint32_t)vtotal_min);

		optc->funcs->set_vtotal_min_max(optc, 0, 0);
		REG_SET(OTG_M_CONST_DTO0, 0, OTG_M_CONST_DTO_PHASE, 0);
		REG_SET(OTG_M_CONST_DTO1, 0, OTG_M_CONST_DTO_MODULO, 0);
		REG_UPDATE_3(OTG_V_TOTAL_CONTROL,
			OTG_V_TOTAL_MIN_SEL, 0,
			OTG_V_TOTAL_MAX_SEL, 0,
			OTG_SET_V_TOTAL_MIN_MASK_EN, 0);
		return;
	}

	ratio = vtotal_max - vtotal_avg;
	modulo = 65536.0 * 65536.0 - 1.0; /* 2^32 - 1 */
	phase = ratio * modulo;

	/* Special cases where the DTO phase gets rounded to 0 or
	 * to DTO modulo
	 */
	if (phase <= 0 || phase >= modulo) {
		REG_SET(OTG_V_TOTAL, 0, OTG_V_TOTAL,
			phase <= 0 ?
				(uint32_t)vtotal_max : (uint32_t)vtotal_min);
		REG_SET(OTG_V_TOTAL_MIN, 0, OTG_V_TOTAL_MIN, 0);
		REG_SET(OTG_V_TOTAL_MAX, 0, OTG_V_TOTAL_MAX, 0);
		REG_SET(OTG_M_CONST_DTO0, 0, OTG_M_CONST_DTO_PHASE, 0);
		REG_SET(OTG_M_CONST_DTO1, 0, OTG_M_CONST_DTO_MODULO, 0);
		REG_UPDATE_3(OTG_V_TOTAL_CONTROL,
			OTG_V_TOTAL_MIN_SEL, 0,
			OTG_V_TOTAL_MAX_SEL, 0,
			OTG_SET_V_TOTAL_MIN_MASK_EN, 0);
		return;
	}
	REG_UPDATE_6(OTG_V_TOTAL_CONTROL,
		OTG_V_TOTAL_MIN_SEL, 1,
		OTG_V_TOTAL_MAX_SEL, 1,
		OTG_SET_V_TOTAL_MIN_MASK_EN, 1,
		OTG_SET_V_TOTAL_MIN_MASK, v_total_mask_value,
		OTG_VTOTAL_MID_REPLACING_MIN_EN, 0,
		OTG_VTOTAL_MID_REPLACING_MAX_EN, 0);
	REG_SET(OTG_V_TOTAL, 0, OTG_V_TOTAL, (uint32_t)vtotal_min);
	optc->funcs->set_vtotal_min_max(optc, vtotal_min, vtotal_max);
	REG_SET(OTG_M_CONST_DTO0, 0, OTG_M_CONST_DTO_PHASE, (uint32_t)phase);
	REG_SET(OTG_M_CONST_DTO1, 0, OTG_M_CONST_DTO_MODULO, (uint32_t)modulo);
}

void dcn30_fpu_populate_dml_writeback_from_context(
		struct dc *dc, struct resource_context *res_ctx, display_e2e_pipe_params_st *pipes)
{
	int pipe_cnt, i, j;
	double max_calc_writeback_dispclk;
	double writeback_dispclk;
	struct writeback_st dout_wb;

	dc_assert_fp_enabled();

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_stream_state *stream = res_ctx->pipe_ctx[i].stream;

		if (!stream)
			continue;
		max_calc_writeback_dispclk = 0;

		/* Set writeback information */
		pipes[pipe_cnt].dout.wb_enable = 0;
		pipes[pipe_cnt].dout.num_active_wb = 0;
		for (j = 0; j < stream->num_wb_info; j++) {
			struct dc_writeback_info *wb_info = &stream->writeback_info[j];

			if (wb_info->wb_enabled && wb_info->writeback_source_plane &&
					(wb_info->writeback_source_plane == res_ctx->pipe_ctx[i].plane_state)) {
				pipes[pipe_cnt].dout.wb_enable = 1;
				pipes[pipe_cnt].dout.num_active_wb++;
				dout_wb.wb_src_height = wb_info->dwb_params.cnv_params.crop_en ?
					wb_info->dwb_params.cnv_params.crop_height :
					wb_info->dwb_params.cnv_params.src_height;
				dout_wb.wb_src_width = wb_info->dwb_params.cnv_params.crop_en ?
					wb_info->dwb_params.cnv_params.crop_width :
					wb_info->dwb_params.cnv_params.src_width;
				dout_wb.wb_dst_width = wb_info->dwb_params.dest_width;
				dout_wb.wb_dst_height = wb_info->dwb_params.dest_height;

				/* For IP that doesn't support WB scaling, set h/v taps to 1 to avoid DML validation failure */
				if (dc->dml.ip.writeback_max_hscl_taps > 1) {
					dout_wb.wb_htaps_luma = wb_info->dwb_params.scaler_taps.h_taps;
					dout_wb.wb_vtaps_luma = wb_info->dwb_params.scaler_taps.v_taps;
				} else {
					dout_wb.wb_htaps_luma = 1;
					dout_wb.wb_vtaps_luma = 1;
				}
				dout_wb.wb_htaps_chroma = 0;
				dout_wb.wb_vtaps_chroma = 0;
				dout_wb.wb_hratio = wb_info->dwb_params.cnv_params.crop_en ?
					(double)wb_info->dwb_params.cnv_params.crop_width /
						(double)wb_info->dwb_params.dest_width :
					(double)wb_info->dwb_params.cnv_params.src_width /
						(double)wb_info->dwb_params.dest_width;
				dout_wb.wb_vratio = wb_info->dwb_params.cnv_params.crop_en ?
					(double)wb_info->dwb_params.cnv_params.crop_height /
						(double)wb_info->dwb_params.dest_height :
					(double)wb_info->dwb_params.cnv_params.src_height /
						(double)wb_info->dwb_params.dest_height;
				if (wb_info->dwb_params.cnv_params.fc_out_format == DWB_OUT_FORMAT_64BPP_ARGB ||
					wb_info->dwb_params.cnv_params.fc_out_format == DWB_OUT_FORMAT_64BPP_RGBA)
					dout_wb.wb_pixel_format = dm_444_64;
				else
					dout_wb.wb_pixel_format = dm_444_32;

				/* Workaround for cases where multiple writebacks are connected to same plane
				 * In which case, need to compute worst case and set the associated writeback parameters
				 * This workaround is necessary due to DML computation assuming only 1 set of writeback
				 * parameters per pipe
				 */
				writeback_dispclk = dml30_CalculateWriteBackDISPCLK(
						dout_wb.wb_pixel_format,
						pipes[pipe_cnt].pipe.dest.pixel_rate_mhz,
						dout_wb.wb_hratio,
						dout_wb.wb_vratio,
						dout_wb.wb_htaps_luma,
						dout_wb.wb_vtaps_luma,
						dout_wb.wb_src_width,
						dout_wb.wb_dst_width,
						pipes[pipe_cnt].pipe.dest.htotal,
						dc->current_state->bw_ctx.dml.ip.writeback_line_buffer_buffer_size);

				if (writeback_dispclk > max_calc_writeback_dispclk) {
					max_calc_writeback_dispclk = writeback_dispclk;
					pipes[pipe_cnt].dout.wb = dout_wb;
				}
			}
		}

		pipe_cnt++;
	}
}

void dcn30_fpu_set_mcif_arb_params(struct mcif_arb_params *wb_arb_params,
	struct display_mode_lib *dml,
	display_e2e_pipe_params_st *pipes,
	int pipe_cnt,
	int cur_pipe)
{
	int i;

	dc_assert_fp_enabled();

	for (i = 0; i < ARRAY_SIZE(wb_arb_params->cli_watermark); i++) {
		wb_arb_params->cli_watermark[i] = get_wm_writeback_urgent(dml, pipes, pipe_cnt) * 1000;
		wb_arb_params->pstate_watermark[i] = get_wm_writeback_dram_clock_change(dml, pipes, pipe_cnt) * 1000;
	}

	wb_arb_params->dram_speed_change_duration = dml->vba.WritebackAllowDRAMClockChangeEndPosition[cur_pipe] * pipes[0].clks_cfg.refclk_mhz; /* num_clock_cycles = us * MHz */
}

void dcn30_fpu_update_soc_for_wm_a(struct dc *dc, struct dc_state *context)
{

	dc_assert_fp_enabled();

	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].valid) {
		if (!context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching ||
				context->bw_ctx.dml.soc.dram_clock_change_latency_us == 0)
			context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.sr_enter_plus_exit_time_us;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.sr_exit_time_us;
	}
}

void dcn30_fpu_calculate_wm_and_dlg(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
	int maxMpcComb = context->bw_ctx.dml.vba.maxMpcComb;
	int i, pipe_idx;
	double dcfclk = context->bw_ctx.dml.vba.DCFCLKState[vlevel][maxMpcComb];
	bool pstate_en = context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][maxMpcComb] != dm_dram_clock_change_unsupported;
	unsigned int dummy_latency_index = 0;

	dc_assert_fp_enabled();

	context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching = false;
    for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i])
			context->streams[i]->fpo_in_use = false;
	}

	if (!pstate_en) {
		/* only when the mclk switch can not be natural, is the fw based vblank stretch attempted */
		context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching =
			dcn30_can_support_mclk_switch_using_fw_based_vblank_stretch(dc, context);

		if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) {
			dummy_latency_index = dcn30_find_dummy_latency_index_for_fw_based_mclk_switch(dc,
				context, pipes, pipe_cnt, vlevel);

			/* After calling dcn30_find_dummy_latency_index_for_fw_based_mclk_switch
			 * we reinstate the original dram_clock_change_latency_us on the context
			 * and all variables that may have changed up to this point, except the
			 * newly found dummy_latency_index
			 */
			context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us;
			dcn30_internal_validate_bw(dc, context, pipes, &pipe_cnt, &vlevel, false, true);
			maxMpcComb = context->bw_ctx.dml.vba.maxMpcComb;
			dcfclk = context->bw_ctx.dml.vba.DCFCLKState[vlevel][context->bw_ctx.dml.vba.maxMpcComb];
			pstate_en = context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][maxMpcComb] != dm_dram_clock_change_unsupported;
		}
	}

	if (context->bw_ctx.dml.soc.min_dcfclk > dcfclk)
		dcfclk = context->bw_ctx.dml.soc.min_dcfclk;

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;
	pipes[0].clks_cfg.socclk_mhz = context->bw_ctx.dml.soc.clock_limits[vlevel].socclk_mhz;

	/* Set B:
	 * DCFCLK: 1GHz or min required above 1GHz
	 * FCLK/UCLK: Max
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
	context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns = get_wm_dram_clock_change(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.pte_meta_urgent_ns = get_wm_memory_trip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_nom = get_fraction_of_urgent_bandwidth(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.frac_urg_bw_flip = get_fraction_of_urgent_bandwidth_imm_flip(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;
	context->bw_ctx.bw.dcn.watermarks.b.urgent_latency_ns = get_urgent_latency(&context->bw_ctx.dml, pipes, pipe_cnt) * 1000;

	pipes[0].clks_cfg.voltage = vlevel;
	pipes[0].clks_cfg.dcfclk_mhz = dcfclk;

	/* Set D:
	 * DCFCLK: Min Required
	 * FCLK(proportional to UCLK): 1GHz or Max
	 * MALL stutter, sr_enter_exit = 4, sr_exit = 2us
	 */
	/*
	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].valid) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us = dc->clk_mgr->bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us;
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
	*/

	/* Set C:
	 * DCFCLK: Min Required
	 * FCLK(proportional to UCLK): 1GHz or Max
	 * pstate latency overridden to 5us
	 */
	if (dc->clk_mgr->bw_params->wm_table.nv_entries[WM_C].valid) {
		unsigned int min_dram_speed_mts = context->bw_ctx.dml.vba.DRAMSpeed;
		unsigned int min_dram_speed_mts_margin = 160;

		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
			dc->clk_mgr->bw_params->dummy_pstate_table[0].dummy_pstate_latency_us;

		if (context->bw_ctx.dml.vba.DRAMClockChangeSupport[vlevel][maxMpcComb] ==
			dm_dram_clock_change_unsupported) {
			int min_dram_speed_mts_offset = dc->clk_mgr->bw_params->clk_table.num_entries - 1;

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

	if (!pstate_en) {
		/* The only difference between A and C is p-state latency, if p-state is not supported we want to
		 * calculate DLG based on dummy p-state latency, and max out the set A p-state watermark
		 */
		context->bw_ctx.bw.dcn.watermarks.a = context->bw_ctx.bw.dcn.watermarks.c;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = 0;
	} else {
		/* Set A:
		 * DCFCLK: Min Required
		 * FCLK(proportional to UCLK): 1GHz or Max
		 *
		 * Set A calculated last so that following calculations are based on Set A
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
	}

	context->perf_params.stutter_period_us = context->bw_ctx.dml.vba.StutterPeriod;

	/* Make set D = set A until set D is enabled */
	context->bw_ctx.bw.dcn.watermarks.d = context->bw_ctx.bw.dcn.watermarks.a;

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

	// WA: restrict FPO to use first non-strobe mode (NV24 BW issue)
	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching &&
			dc->dml.soc.num_chans <= 4 &&
			context->bw_ctx.dml.vba.DRAMSpeed <= 1700 &&
			context->bw_ctx.dml.vba.DRAMSpeed >= 1500) {

		for (i = 0; i < dc->dml.soc.num_states; i++) {
			if (dc->dml.soc.clock_limits[i].dram_speed_mts > 1700) {
				context->bw_ctx.dml.vba.DRAMSpeed = dc->dml.soc.clock_limits[i].dram_speed_mts;
				break;
			}
		}
	}

	dcn20_calculate_dlg_params(dc, context, pipes, pipe_cnt, vlevel);

	if (!pstate_en)
		/* Restore full p-state latency */
		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
				dc->clk_mgr->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us;

	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching)
		dcn30_setup_mclk_switch_using_fw_based_vblank_stretch(dc, context);
}

void dcn30_fpu_update_dram_channel_width_bytes(struct dc *dc)
{
	dc_assert_fp_enabled();

	if (dc->ctx->dc_bios->vram_info.dram_channel_width_bytes)
		dcn3_0_soc.dram_channel_width_bytes = dc->ctx->dc_bios->vram_info.dram_channel_width_bytes;
}

void dcn30_fpu_update_max_clk(struct dc_bounding_box_max_clk *dcn30_bb_max_clk)
{
		dc_assert_fp_enabled();

		if (!dcn30_bb_max_clk->max_dcfclk_mhz)
			dcn30_bb_max_clk->max_dcfclk_mhz = dcn3_0_soc.clock_limits[0].dcfclk_mhz;
		if (!dcn30_bb_max_clk->max_dispclk_mhz)
			dcn30_bb_max_clk->max_dispclk_mhz = dcn3_0_soc.clock_limits[0].dispclk_mhz;
		if (!dcn30_bb_max_clk->max_dppclk_mhz)
			dcn30_bb_max_clk->max_dppclk_mhz = dcn3_0_soc.clock_limits[0].dppclk_mhz;
		if (!dcn30_bb_max_clk->max_phyclk_mhz)
			dcn30_bb_max_clk->max_phyclk_mhz = dcn3_0_soc.clock_limits[0].phyclk_mhz;
}

void dcn30_fpu_get_optimal_dcfclk_fclk_for_uclk(unsigned int uclk_mts,
		unsigned int *optimal_dcfclk,
		unsigned int *optimal_fclk)
{
	double bw_from_dram, bw_from_dram1, bw_from_dram2;

	dc_assert_fp_enabled();

	bw_from_dram1 = uclk_mts * dcn3_0_soc.num_chans *
		dcn3_0_soc.dram_channel_width_bytes * (dcn3_0_soc.max_avg_dram_bw_use_normal_percent / 100);
	bw_from_dram2 = uclk_mts * dcn3_0_soc.num_chans *
		dcn3_0_soc.dram_channel_width_bytes * (dcn3_0_soc.max_avg_sdp_bw_use_normal_percent / 100);

	bw_from_dram = (bw_from_dram1 < bw_from_dram2) ? bw_from_dram1 : bw_from_dram2;

	if (optimal_fclk)
		*optimal_fclk = bw_from_dram /
		(dcn3_0_soc.fabric_datapath_to_dcn_data_return_bytes * (dcn3_0_soc.max_avg_sdp_bw_use_normal_percent / 100));

	if (optimal_dcfclk)
		*optimal_dcfclk =  bw_from_dram /
		(dcn3_0_soc.return_bus_width_bytes * (dcn3_0_soc.max_avg_sdp_bw_use_normal_percent / 100));
}

void dcn30_fpu_update_bw_bounding_box(struct dc *dc,
	struct clk_bw_params *bw_params,
	struct dc_bounding_box_max_clk *dcn30_bb_max_clk,
	unsigned int *dcfclk_mhz,
	unsigned int *dram_speed_mts)
{
	unsigned int i;

	dc_assert_fp_enabled();

	dcn3_0_soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;
	dc->dml.soc.dispclk_dppclk_vco_speed_mhz = dc->clk_mgr->dentist_vco_freq_khz / 1000.0;

	for (i = 0; i < dcn3_0_soc.num_states; i++) {
		dcn3_0_soc.clock_limits[i].state = i;
		dcn3_0_soc.clock_limits[i].dcfclk_mhz = dcfclk_mhz[i];
		dcn3_0_soc.clock_limits[i].fabricclk_mhz = dcfclk_mhz[i];
		dcn3_0_soc.clock_limits[i].dram_speed_mts = dram_speed_mts[i];

		/* Fill all states with max values of all other clocks */
		dcn3_0_soc.clock_limits[i].dispclk_mhz = dcn30_bb_max_clk->max_dispclk_mhz;
		dcn3_0_soc.clock_limits[i].dppclk_mhz  = dcn30_bb_max_clk->max_dppclk_mhz;
		dcn3_0_soc.clock_limits[i].phyclk_mhz  = dcn30_bb_max_clk->max_phyclk_mhz;
		dcn3_0_soc.clock_limits[i].dtbclk_mhz = dcn3_0_soc.clock_limits[0].dtbclk_mhz;
		/* These clocks cannot come from bw_params, always fill from dcn3_0_soc[1] */
		/* FCLK, PHYCLK_D18, SOCCLK, DSCCLK */
		dcn3_0_soc.clock_limits[i].phyclk_d18_mhz = dcn3_0_soc.clock_limits[0].phyclk_d18_mhz;
		dcn3_0_soc.clock_limits[i].socclk_mhz = dcn3_0_soc.clock_limits[0].socclk_mhz;
		dcn3_0_soc.clock_limits[i].dscclk_mhz = dcn3_0_soc.clock_limits[0].dscclk_mhz;
	}
	/* re-init DML with updated bb */
	dml_init_instance(&dc->dml, &dcn3_0_soc, &dcn3_0_ip, DML_PROJECT_DCN30);
	if (dc->current_state)
		dml_init_instance(&dc->current_state->bw_ctx.dml, &dcn3_0_soc, &dcn3_0_ip, DML_PROJECT_DCN30);

}

/**
 * Finds dummy_latency_index when MCLK switching using firmware based
 * vblank stretch is enabled. This function will iterate through the
 * table of dummy pstate latencies until the lowest value that allows
 * dm_allow_self_refresh_and_mclk_switch to happen is found
 */
int dcn30_find_dummy_latency_index_for_fw_based_mclk_switch(struct dc *dc,
							    struct dc_state *context,
							    display_e2e_pipe_params_st *pipes,
							    int pipe_cnt,
							    int vlevel)
{
	const int max_latency_table_entries = 4;
	int dummy_latency_index = 0;

	dc_assert_fp_enabled();

	while (dummy_latency_index < max_latency_table_entries) {
		context->bw_ctx.dml.soc.dram_clock_change_latency_us =
				dc->clk_mgr->bw_params->dummy_pstate_table[dummy_latency_index].dummy_pstate_latency_us;
		dcn30_internal_validate_bw(dc, context, pipes, &pipe_cnt, &vlevel, false, true);

		if (context->bw_ctx.dml.soc.allow_dram_self_refresh_or_dram_clock_change_in_vblank ==
			dm_allow_self_refresh_and_mclk_switch)
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
		dummy_latency_index = 3;
	}

	return dummy_latency_index;
}

void dcn3_fpu_build_wm_range_table(struct clk_mgr *base)
{
	/* defaults */
	double pstate_latency_us = base->ctx->dc->dml.soc.dram_clock_change_latency_us;
	double sr_exit_time_us = base->ctx->dc->dml.soc.sr_exit_time_us;
	double sr_enter_plus_exit_time_us = base->ctx->dc->dml.soc.sr_enter_plus_exit_time_us;
	uint16_t min_uclk_mhz = base->bw_params->clk_table.entries[0].memclk_mhz;

	dc_assert_fp_enabled();

	/* Set A - Normal - default values*/
	base->bw_params->wm_table.nv_entries[WM_A].valid = true;
	base->bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us = pstate_latency_us;
	base->bw_params->wm_table.nv_entries[WM_A].dml_input.sr_exit_time_us = sr_exit_time_us;
	base->bw_params->wm_table.nv_entries[WM_A].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	base->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
	base->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_dcfclk = 0;
	base->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_dcfclk = 0xFFFF;
	base->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_uclk = min_uclk_mhz;
	base->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set B - Performance - higher minimum clocks */
//	base->bw_params->wm_table.nv_entries[WM_B].valid = true;
//	base->bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us = pstate_latency_us;
//	base->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us = sr_exit_time_us;
//	base->bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
//	base->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
//	base->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_dcfclk = TUNED VALUE;
//	base->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_dcfclk = 0xFFFF;
//	base->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_uclk = TUNED VALUE;
//	base->bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set C - Dummy P-State - P-State latency set to "dummy p-state" value */
	base->bw_params->wm_table.nv_entries[WM_C].valid = true;
	base->bw_params->wm_table.nv_entries[WM_C].dml_input.pstate_latency_us = 0;
	base->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us = sr_exit_time_us;
	base->bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	base->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.wm_type = WATERMARKS_DUMMY_PSTATE;
	base->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_dcfclk = 0;
	base->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_dcfclk = 0xFFFF;
	base->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_uclk = min_uclk_mhz;
	base->bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_uclk = 0xFFFF;
	base->bw_params->dummy_pstate_table[0].dram_speed_mts = 1600;
	base->bw_params->dummy_pstate_table[0].dummy_pstate_latency_us = 38;
	base->bw_params->dummy_pstate_table[1].dram_speed_mts = 8000;
	base->bw_params->dummy_pstate_table[1].dummy_pstate_latency_us = 9;
	base->bw_params->dummy_pstate_table[2].dram_speed_mts = 10000;
	base->bw_params->dummy_pstate_table[2].dummy_pstate_latency_us = 8;
	base->bw_params->dummy_pstate_table[3].dram_speed_mts = 16000;
	base->bw_params->dummy_pstate_table[3].dummy_pstate_latency_us = 5;

	/* Set D - MALL - SR enter and exit times adjusted for MALL */
	base->bw_params->wm_table.nv_entries[WM_D].valid = true;
	base->bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us = pstate_latency_us;
	base->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us = 2;
	base->bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us = 4;
	base->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.wm_type = WATERMARKS_MALL;
	base->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_dcfclk = 0;
	base->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_dcfclk = 0xFFFF;
	base->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_uclk = min_uclk_mhz;
	base->bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_uclk = 0xFFFF;
}

void patch_dcn30_soc_bounding_box(struct dc *dc, struct _vcs_dpi_soc_bounding_box_st *dcn3_0_ip)
{
	dc_assert_fp_enabled();

	if (dc->ctx->dc_bios->funcs->get_soc_bb_info) {
		struct bp_soc_bb_info bb_info = {0};

		if (dc->ctx->dc_bios->funcs->get_soc_bb_info(dc->ctx->dc_bios, &bb_info) == BP_RESULT_OK) {
			if (bb_info.dram_clock_change_latency_100ns > 0)
				dcn3_0_soc.dram_clock_change_latency_us = bb_info.dram_clock_change_latency_100ns * 10;

			if (bb_info.dram_sr_enter_exit_latency_100ns > 0)
				dcn3_0_soc.sr_enter_plus_exit_time_us = bb_info.dram_sr_enter_exit_latency_100ns * 10;

			if (bb_info.dram_sr_exit_latency_100ns > 0)
				dcn3_0_soc.sr_exit_time_us = bb_info.dram_sr_exit_latency_100ns * 10;
		}
	}
}
