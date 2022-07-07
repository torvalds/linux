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

