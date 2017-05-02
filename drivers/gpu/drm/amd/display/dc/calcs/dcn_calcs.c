/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "dm_services.h"
#include "dcn_calcs.h"
#include "dcn_calc_auto.h"
#include "dc.h"
#include "core_dc.h"
#include "dal_asic_id.h"

#include "resource.h"
#include "dcn10/dcn10_resource.h"
#include "dcn_calc_math.h"

/* Defaults from spreadsheet rev#247 */
const struct dcn_soc_bounding_box dcn10_soc_defaults = {
		.sr_exit_time = 17, /*us*/ /*update based on HW Request for 118773*/
		.sr_enter_plus_exit_time = 19, /*us*/
		.urgent_latency = 4, /*us*/
		.write_back_latency = 12, /*us*/
		.percent_of_ideal_drambw_received_after_urg_latency = 80, /*%*/
		.max_request_size = 256, /*bytes*/
		.dcfclkv_max0p9 = 600, /*MHz*/
		.dcfclkv_nom0p8 = 600, /*MHz*/
		.dcfclkv_mid0p72 = 300, /*MHz*/
		.dcfclkv_min0p65 = 300, /*MHz*/
		.max_dispclk_vmax0p9 = 1086, /*MHz*/
		.max_dispclk_vnom0p8 = 661, /*MHz*/
		.max_dispclk_vmid0p72 = 608, /*MHz*/
		.max_dispclk_vmin0p65 = 608, /*MHz*/
		.max_dppclk_vmax0p9 = 661, /*MHz*/
		.max_dppclk_vnom0p8 = 661, /*MHz*/
		.max_dppclk_vmid0p72 = 435, /*MHz*/
		.max_dppclk_vmin0p65 = 435, /*MHz*/
		.socclk = 208, /*MHz*/
		.fabric_and_dram_bandwidth_vmax0p9 = 38.4f, /*GB/s*/
		.fabric_and_dram_bandwidth_vnom0p8 = 34.1f, /*GB/s*/
		.fabric_and_dram_bandwidth_vmid0p72 = 29.8f, /*GB/s*/
		.fabric_and_dram_bandwidth_vmin0p65 = 12.8f, /*GB/s*/
		.phyclkv_max0p9 = 810, /*MHz*/
		.phyclkv_nom0p8 = 810, /*MHz*/
		.phyclkv_mid0p72 = 540, /*MHz*/
		.phyclkv_min0p65 = 540, /*MHz*/
		.downspreading = 0.5f, /*%*/
		.round_trip_ping_latency_cycles = 128, /*DCFCLK Cycles*/
		.urgent_out_of_order_return_per_channel = 256, /*bytes*/
		.number_of_channels = 2,
		.vmm_page_size = 4096, /*bytes*/
		.dram_clock_change_latency = 17, /*us*/
		.return_bus_width = 64, /*bytes*/
};

const struct dcn_ip_params dcn10_ip_defaults = {
		.rob_buffer_size_in_kbyte = 64,
		.det_buffer_size_in_kbyte = 164,
		.dpp_output_buffer_pixels = 2560,
		.opp_output_buffer_lines = 1,
		.pixel_chunk_size_in_kbyte = 8,
		.pte_enable = dcn_bw_yes,
		.pte_chunk_size = 2, /*kbytes*/
		.meta_chunk_size = 2, /*kbytes*/
		.writeback_chunk_size = 2, /*kbytes*/
		.odm_capability = dcn_bw_no,
		.dsc_capability = dcn_bw_no,
		.line_buffer_size = 589824, /*bit*/
		.max_line_buffer_lines = 12,
		.is_line_buffer_bpp_fixed = dcn_bw_no,
		.line_buffer_fixed_bpp = dcn_bw_na,
		.writeback_luma_buffer_size = 12, /*kbytes*/
		.writeback_chroma_buffer_size = 8, /*kbytes*/
		.max_num_dpp = 4,
		.max_num_writeback = 2,
		.max_dchub_topscl_throughput = 4, /*pixels/dppclk*/
		.max_pscl_tolb_throughput = 2, /*pixels/dppclk*/
		.max_lb_tovscl_throughput = 4, /*pixels/dppclk*/
		.max_vscl_tohscl_throughput = 4, /*pixels/dppclk*/
		.max_hscl_ratio = 4,
		.max_vscl_ratio = 4,
		.max_hscl_taps = 8,
		.max_vscl_taps = 8,
		.pte_buffer_size_in_requests = 42,
		.dispclk_ramping_margin = 1, /*%*/
		.under_scan_factor = 1.11f,
		.max_inter_dcn_tile_repeaters = 8,
		.can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one = dcn_bw_no,
		.bug_forcing_luma_and_chroma_request_to_same_size_fixed = dcn_bw_no,
		.dcfclk_cstate_latency = 10 /*TODO clone of something else? sr_enter_plus_exit_time?*/
};

static enum dcn_bw_defs tl_sw_mode_to_bw_defs(enum swizzle_mode_values sw_mode)
{
	switch (sw_mode) {
	case DC_SW_LINEAR:
		return dcn_bw_sw_linear;
	case DC_SW_4KB_S:
		return dcn_bw_sw_4_kb_s;
	case DC_SW_4KB_D:
		return dcn_bw_sw_4_kb_d;
	case DC_SW_64KB_S:
		return dcn_bw_sw_64_kb_s;
	case DC_SW_64KB_D:
		return dcn_bw_sw_64_kb_d;
	case DC_SW_VAR_S:
		return dcn_bw_sw_var_s;
	case DC_SW_VAR_D:
		return dcn_bw_sw_var_d;
	case DC_SW_64KB_S_T:
		return dcn_bw_sw_64_kb_s_t;
	case DC_SW_64KB_D_T:
		return dcn_bw_sw_64_kb_d_t;
	case DC_SW_4KB_S_X:
		return dcn_bw_sw_4_kb_s_x;
	case DC_SW_4KB_D_X:
		return dcn_bw_sw_4_kb_d_x;
	case DC_SW_64KB_S_X:
		return dcn_bw_sw_64_kb_s_x;
	case DC_SW_64KB_D_X:
		return dcn_bw_sw_64_kb_d_x;
	case DC_SW_VAR_S_X:
		return dcn_bw_sw_var_s_x;
	case DC_SW_VAR_D_X:
		return dcn_bw_sw_var_d_x;
	case DC_SW_256B_S:
	case DC_SW_256_D:
	case DC_SW_256_R:
	case DC_SW_4KB_R:
	case DC_SW_64KB_R:
	case DC_SW_VAR_R:
	case DC_SW_4KB_R_X:
	case DC_SW_64KB_R_X:
	case DC_SW_VAR_R_X:
	default:
		BREAK_TO_DEBUGGER(); /*not in formula*/
		return dcn_bw_sw_4_kb_s;
	}
}

static int tl_lb_bpp_to_int(enum lb_pixel_depth depth)
{
	switch (depth) {
	case LB_PIXEL_DEPTH_18BPP:
		return 18;
	case LB_PIXEL_DEPTH_24BPP:
		return 24;
	case LB_PIXEL_DEPTH_30BPP:
		return 30;
	case LB_PIXEL_DEPTH_36BPP:
		return 36;
	default:
		return 30;
	}
}

static enum dcn_bw_defs tl_pixel_format_to_bw_defs(enum surface_pixel_format format)
{
	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		return dcn_bw_rgb_sub_16;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
		return dcn_bw_rgb_sub_32;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		return dcn_bw_rgb_sub_64;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		return dcn_bw_yuv420_sub_8;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		return dcn_bw_yuv420_sub_10;
	default:
		return dcn_bw_rgb_sub_32;
	}
}

static void pipe_ctx_to_e2e_pipe_params (
		const struct pipe_ctx *pipe,
		struct _vcs_dpi_display_pipe_params_st *input)
{
	input->src.is_hsplit = false;
	if (pipe->top_pipe != NULL && pipe->top_pipe->surface == pipe->surface)
		input->src.is_hsplit = true;
	else if (pipe->bottom_pipe != NULL && pipe->bottom_pipe->surface == pipe->surface)
		input->src.is_hsplit = true;

	input->src.dcc                 = pipe->surface->public.dcc.enable;
	input->src.dcc_rate            = 1;
	input->src.meta_pitch          = pipe->surface->public.dcc.grph.meta_pitch;
	input->src.source_scan         = dm_horz;
	input->src.sw_mode             = pipe->surface->public.tiling_info.gfx9.swizzle;

	input->src.viewport_width      = pipe->scl_data.viewport.width;
	input->src.viewport_height     = pipe->scl_data.viewport.height;
	input->src.data_pitch          = pipe->scl_data.viewport.width;
	input->src.data_pitch_c        = pipe->scl_data.viewport.width;
	input->src.cur0_src_width      = 128; /* TODO: Cursor calcs, not curently stored */
	input->src.cur0_bpp            = 32;

	switch (pipe->surface->public.tiling_info.gfx9.swizzle) {
	/* for 4/8/16 high tiles */
	case DC_SW_LINEAR:
		input->src.is_display_sw = 1;
		input->src.macro_tile_size = dm_4k_tile;
		break;
	case DC_SW_4KB_S:
	case DC_SW_4KB_S_X:
		input->src.is_display_sw = 0;
		input->src.macro_tile_size = dm_4k_tile;
		break;
	case DC_SW_64KB_S:
	case DC_SW_64KB_S_X:
		input->src.is_display_sw = 0;
		input->src.macro_tile_size = dm_64k_tile;
		break;
	case DC_SW_VAR_S:
	case DC_SW_VAR_S_X:
		input->src.is_display_sw = 0;
		input->src.macro_tile_size = dm_256k_tile;
		break;

	/* For 64bpp 2 high tiles */
	case DC_SW_4KB_D:
	case DC_SW_4KB_D_X:
		input->src.is_display_sw = 1;
		input->src.macro_tile_size = dm_4k_tile;
		break;
	case DC_SW_64KB_D:
	case DC_SW_64KB_D_X:
		input->src.is_display_sw = 1;
		input->src.macro_tile_size = dm_64k_tile;
		break;
	case DC_SW_VAR_D:
	case DC_SW_VAR_D_X:
		input->src.is_display_sw = 1;
		input->src.macro_tile_size = dm_256k_tile;
		break;

	/* Unsupported swizzle modes for dcn */
	case DC_SW_256B_S:
	default:
		ASSERT(0); /* Not supported */
		break;
	}

	switch (pipe->surface->public.rotation) {
	case ROTATION_ANGLE_0:
	case ROTATION_ANGLE_180:
		input->src.source_scan = dm_horz;
		break;
	case ROTATION_ANGLE_90:
	case ROTATION_ANGLE_270:
		input->src.source_scan = dm_vert;
		break;
	default:
		ASSERT(0); /* Not supported */
		break;
	}

	/* TODO: Fix pixel format mappings */
	switch (pipe->surface->public.format) {
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		input->src.source_format = dm_420_8;
		input->src.viewport_width_c    = input->src.viewport_width / 2;
		input->src.viewport_height_c   = input->src.viewport_height / 2;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		input->src.source_format = dm_420_10;
		input->src.viewport_width_c    = input->src.viewport_width / 2;
		input->src.viewport_height_c   = input->src.viewport_height / 2;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		input->src.source_format = dm_444_64;
		input->src.viewport_width_c    = input->src.viewport_width;
		input->src.viewport_height_c   = input->src.viewport_height;
		break;
	default:
		input->src.source_format = dm_444_32;
		input->src.viewport_width_c    = input->src.viewport_width;
		input->src.viewport_height_c   = input->src.viewport_height;
		break;
	}

	input->scale_taps.htaps                = pipe->scl_data.taps.h_taps;
	input->scale_ratio_depth.hscl_ratio    = pipe->scl_data.ratios.horz.value/4294967296.0;
	input->scale_ratio_depth.vscl_ratio    = pipe->scl_data.ratios.vert.value/4294967296.0;
	input->scale_ratio_depth.vinit =  pipe->scl_data.inits.v.value/4294967296.0;
	if (input->scale_ratio_depth.vinit < 1.0)
			input->scale_ratio_depth.vinit = 1;
	input->scale_taps.vtaps = pipe->scl_data.taps.v_taps;
	input->scale_taps.vtaps_c = pipe->scl_data.taps.v_taps_c;
	input->scale_taps.htaps_c              = pipe->scl_data.taps.h_taps_c;
	input->scale_ratio_depth.hscl_ratio_c  = pipe->scl_data.ratios.horz_c.value/4294967296.0;
	input->scale_ratio_depth.vscl_ratio_c  = pipe->scl_data.ratios.vert_c.value/4294967296.0;
	input->scale_ratio_depth.vinit_c       = pipe->scl_data.inits.v_c.value/4294967296.0;
	if (input->scale_ratio_depth.vinit_c < 1.0)
			input->scale_ratio_depth.vinit_c = 1;
	switch (pipe->scl_data.lb_params.depth) {
	case LB_PIXEL_DEPTH_30BPP:
		input->scale_ratio_depth.lb_depth = 30; break;
	case LB_PIXEL_DEPTH_36BPP:
		input->scale_ratio_depth.lb_depth = 36; break;
	default:
		input->scale_ratio_depth.lb_depth = 24; break;
	}


	input->dest.vactive        = pipe->stream->public.timing.v_addressable;

	input->dest.recout_width   = pipe->scl_data.recout.width;
	input->dest.recout_height  = pipe->scl_data.recout.height;

	input->dest.full_recout_width   = pipe->scl_data.recout.width;
	input->dest.full_recout_height  = pipe->scl_data.recout.height;

	input->dest.htotal         = pipe->stream->public.timing.h_total;
	input->dest.hblank_start   = input->dest.htotal - pipe->stream->public.timing.h_front_porch;
	input->dest.hblank_end     = input->dest.hblank_start
			- pipe->stream->public.timing.h_addressable
			- pipe->stream->public.timing.h_border_left
			- pipe->stream->public.timing.h_border_right;

	input->dest.vtotal         = pipe->stream->public.timing.v_total;
	input->dest.vblank_start   = input->dest.vtotal - pipe->stream->public.timing.v_front_porch;
	input->dest.vblank_end     = input->dest.vblank_start
			- pipe->stream->public.timing.v_addressable
			- pipe->stream->public.timing.v_border_bottom
			- pipe->stream->public.timing.v_border_top;

	input->dest.vsync_plus_back_porch = pipe->stream->public.timing.v_total
			- pipe->stream->public.timing.v_addressable
			- pipe->stream->public.timing.v_front_porch;
	input->dest.pixel_rate_mhz = pipe->stream->public.timing.pix_clk_khz/1000.0;
	input->dest.vstartup_start = pipe->pipe_dlg_param.vstartup_start;
	input->dest.vupdate_offset = pipe->pipe_dlg_param.vupdate_offset;
	input->dest.vupdate_offset = pipe->pipe_dlg_param.vupdate_offset;
	input->dest.vupdate_width = pipe->pipe_dlg_param.vupdate_width;

}

static void dcn_bw_calc_rq_dlg_ttu(
		const struct core_dc *dc,
		const struct dcn_bw_internal_vars *v,
		struct pipe_ctx *pipe)
{
	struct display_mode_lib *dml = (struct display_mode_lib *)(&dc->dml);
	struct _vcs_dpi_display_dlg_regs_st *dlg_regs = &pipe->dlg_regs;
	struct _vcs_dpi_display_ttu_regs_st *ttu_regs = &pipe->ttu_regs;
	struct _vcs_dpi_display_rq_regs_st *rq_regs = &pipe->rq_regs;
	struct _vcs_dpi_display_rq_params_st rq_param = {0};
	struct _vcs_dpi_display_dlg_sys_params_st dlg_sys_param = {0};
	struct _vcs_dpi_display_e2e_pipe_params_st input = { { { 0 } } };
	float total_active_bw = 0;
	float total_prefetch_bw = 0;
	int total_flip_bytes = 0;
	int i;

	for (i = 0; i < number_of_planes; i++) {
		total_active_bw += v->read_bandwidth[i];
		total_prefetch_bw += v->prefetch_bandwidth[i];
		total_flip_bytes += v->total_immediate_flip_bytes[i];
	}
	dlg_sys_param.total_flip_bw = v->return_bw - dcn_bw_max2(total_active_bw, total_prefetch_bw);
	if (dlg_sys_param.total_flip_bw < 0.0)
		dlg_sys_param.total_flip_bw = 0;

	dlg_sys_param.t_mclk_wm_us = v->dram_clock_change_watermark;
	dlg_sys_param.t_sr_wm_us = v->stutter_enter_plus_exit_watermark;
	dlg_sys_param.t_urg_wm_us = v->urgent_watermark;
	dlg_sys_param.t_extra_us = v->urgent_extra_latency;
	dlg_sys_param.deepsleep_dcfclk_mhz = v->dcf_clk_deep_sleep;
	dlg_sys_param.total_flip_bytes = total_flip_bytes;

	pipe_ctx_to_e2e_pipe_params(pipe, &input.pipe);
	input.clks_cfg.dcfclk_mhz = v->dcfclk;
	input.clks_cfg.dispclk_mhz = v->dispclk;
	input.clks_cfg.dppclk_mhz = v->dppclk;
	input.clks_cfg.refclk_mhz = dc->res_pool->ref_clock_inKhz/1000;
	input.clks_cfg.socclk_mhz = v->socclk;
	input.clks_cfg.voltage = v->voltage_level;
//	dc->dml.logger = pool->base.logger;

	/*todo: soc->sr_enter_plus_exit_time??*/
	dlg_sys_param.t_srx_delay_us = dc->dcn_ip.dcfclk_cstate_latency / v->dcf_clk_deep_sleep;

	dml_rq_dlg_get_rq_params(dml, &rq_param, input.pipe.src);
	extract_rq_regs(dml, rq_regs, rq_param);
	dml_rq_dlg_get_dlg_params(
			dml,
			dlg_regs,
			ttu_regs,
			rq_param.dlg,
			dlg_sys_param,
			input,
			true,
			true,
			v->pte_enable == dcn_bw_yes,
			pipe->surface->public.flip_immediate);
}

static void dcn_dml_wm_override(
		const struct dcn_bw_internal_vars *v,
		struct display_mode_lib *dml,
		struct validate_context *context,
		const struct resource_pool *pool)
{
	int i, in_idx, active_count;

	struct _vcs_dpi_display_e2e_pipe_params_st *input = dm_alloc(pool->pipe_count *
					sizeof(struct _vcs_dpi_display_e2e_pipe_params_st));
	struct wm {
		double urgent;
		struct _vcs_dpi_cstate_pstate_watermarks_st cpstate;
		double pte_meta_urgent;
	} a;


	for (i = 0, in_idx = 0; i < pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream || !pipe->surface)
			continue;

		input[in_idx].clks_cfg.dcfclk_mhz = v->dcfclk;
		input[in_idx].clks_cfg.dispclk_mhz = v->dispclk;
		input[in_idx].clks_cfg.dppclk_mhz = v->dppclk;
		input[in_idx].clks_cfg.refclk_mhz = pool->ref_clock_inKhz / 1000;
		input[in_idx].clks_cfg.socclk_mhz = v->socclk;
		input[in_idx].clks_cfg.voltage = v->voltage_level;
		pipe_ctx_to_e2e_pipe_params(pipe, &input[in_idx].pipe);
		dml_rq_dlg_get_rq_reg(
			dml,
			&pipe->rq_regs,
			input[in_idx].pipe.src);
		in_idx++;
	}
	active_count = in_idx;

	a.urgent = dml_wm_urgent_e2e(dml, input, active_count);
	a.cpstate = dml_wm_cstate_pstate_e2e(dml, input, active_count);
	a.pte_meta_urgent = dml_wm_pte_meta_urgent(dml, a.urgent);

	context->bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns =
			a.cpstate.cstate_exit_us * 1000;
	context->bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns =
			a.cpstate.cstate_enter_plus_exit_us * 1000;
	context->bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns =
			a.cpstate.pstate_change_us * 1000;
	context->bw.dcn.watermarks.a.pte_meta_urgent_ns = a.pte_meta_urgent * 1000;
	context->bw.dcn.watermarks.a.urgent_ns = a.urgent * 1000;
	context->bw.dcn.watermarks.b = context->bw.dcn.watermarks.a;
	context->bw.dcn.watermarks.c = context->bw.dcn.watermarks.a;
	context->bw.dcn.watermarks.d = context->bw.dcn.watermarks.a;


	for (i = 0, in_idx = 0; i < pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream || !pipe->surface)
			continue;

		dml_rq_dlg_get_dlg_reg(dml,
			&pipe->dlg_regs,
			&pipe->ttu_regs,
			input, active_count,
			in_idx,
			true,
			true,
			v->pte_enable == dcn_bw_yes,
			pipe->surface->public.flip_immediate);
		in_idx++;
	}
	dm_free(input);
}

static void split_stream_across_pipes(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct pipe_ctx *primary_pipe,
		struct pipe_ctx *secondary_pipe)
{
	if (!primary_pipe->surface)
		return;

	secondary_pipe->stream = primary_pipe->stream;
	secondary_pipe->tg = primary_pipe->tg;

	secondary_pipe->mi = pool->mis[secondary_pipe->pipe_idx];
	secondary_pipe->ipp = pool->ipps[secondary_pipe->pipe_idx];
	secondary_pipe->xfm = pool->transforms[secondary_pipe->pipe_idx];
	secondary_pipe->opp = pool->opps[secondary_pipe->pipe_idx];
	if (primary_pipe->bottom_pipe) {
		secondary_pipe->bottom_pipe = primary_pipe->bottom_pipe;
		secondary_pipe->bottom_pipe->top_pipe = secondary_pipe;
	}
	primary_pipe->bottom_pipe = secondary_pipe;
	secondary_pipe->top_pipe = primary_pipe;
	secondary_pipe->surface = primary_pipe->surface;
	secondary_pipe->pipe_dlg_param = primary_pipe->pipe_dlg_param;

	resource_build_scaling_params(primary_pipe);
	resource_build_scaling_params(secondary_pipe);
}

static void calc_wm_sets_and_perf_params(
		struct validate_context *context,
		struct dcn_bw_internal_vars *v)
{
	/* Calculate set A last to keep internal var state consistent for required config */
	if (v->voltage_level < 2) {
		v->fabric_and_dram_bandwidth_per_state[1] = v->fabric_and_dram_bandwidth_vnom0p8;
		v->fabric_and_dram_bandwidth_per_state[0] = v->fabric_and_dram_bandwidth_vnom0p8;
		v->fabric_and_dram_bandwidth = v->fabric_and_dram_bandwidth_vnom0p8;
		dispclkdppclkdcfclk_deep_sleep_prefetch_parameters_watermarks_and_performance_calculation(v);

		context->bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns =
			v->stutter_exit_watermark * 1000;
		context->bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns =
				v->stutter_enter_plus_exit_watermark * 1000;
		context->bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns =
				v->dram_clock_change_watermark * 1000;
		context->bw.dcn.watermarks.b.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
		context->bw.dcn.watermarks.b.urgent_ns = v->urgent_watermark * 1000;

		v->dcfclk_per_state[1] = v->dcfclkv_nom0p8;
		v->dcfclk_per_state[0] = v->dcfclkv_nom0p8;
		v->dcfclk = v->dcfclkv_nom0p8;
		dispclkdppclkdcfclk_deep_sleep_prefetch_parameters_watermarks_and_performance_calculation(v);

		context->bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns =
			v->stutter_exit_watermark * 1000;
		context->bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns =
				v->stutter_enter_plus_exit_watermark * 1000;
		context->bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns =
				v->dram_clock_change_watermark * 1000;
		context->bw.dcn.watermarks.c.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
		context->bw.dcn.watermarks.c.urgent_ns = v->urgent_watermark * 1000;
	}

	if (v->voltage_level < 3) {
		v->fabric_and_dram_bandwidth_per_state[2] = v->fabric_and_dram_bandwidth_vmax0p9;
		v->fabric_and_dram_bandwidth_per_state[1] = v->fabric_and_dram_bandwidth_vmax0p9;
		v->fabric_and_dram_bandwidth_per_state[0] = v->fabric_and_dram_bandwidth_vmax0p9;
		v->fabric_and_dram_bandwidth = v->fabric_and_dram_bandwidth_vmax0p9;
		v->dcfclk_per_state[2] = v->dcfclkv_max0p9;
		v->dcfclk_per_state[1] = v->dcfclkv_max0p9;
		v->dcfclk_per_state[0] = v->dcfclkv_max0p9;
		v->dcfclk = v->dcfclkv_max0p9;
		dispclkdppclkdcfclk_deep_sleep_prefetch_parameters_watermarks_and_performance_calculation(v);

		context->bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns =
			v->stutter_exit_watermark * 1000;
		context->bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns =
				v->stutter_enter_plus_exit_watermark * 1000;
		context->bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns =
				v->dram_clock_change_watermark * 1000;
		context->bw.dcn.watermarks.d.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
		context->bw.dcn.watermarks.d.urgent_ns = v->urgent_watermark * 1000;
	}

	v->fabric_and_dram_bandwidth_per_state[2] = v->fabric_and_dram_bandwidth_vnom0p8;
	v->fabric_and_dram_bandwidth_per_state[1] = v->fabric_and_dram_bandwidth_vmid0p72;
	v->fabric_and_dram_bandwidth_per_state[0] = v->fabric_and_dram_bandwidth_vmin0p65;
	v->fabric_and_dram_bandwidth = v->fabric_and_dram_bandwidth_per_state[v->voltage_level];
	v->dcfclk_per_state[2] = v->dcfclkv_nom0p8;
	v->dcfclk_per_state[1] = v->dcfclkv_mid0p72;
	v->dcfclk_per_state[0] = v->dcfclkv_min0p65;
	v->dcfclk = v->dcfclk_per_state[v->voltage_level];
	dispclkdppclkdcfclk_deep_sleep_prefetch_parameters_watermarks_and_performance_calculation(v);

	context->bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns =
		v->stutter_exit_watermark * 1000;
	context->bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns =
			v->stutter_enter_plus_exit_watermark * 1000;
	context->bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns =
			v->dram_clock_change_watermark * 1000;
	context->bw.dcn.watermarks.a.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
	context->bw.dcn.watermarks.a.urgent_ns = v->urgent_watermark * 1000;
	if (v->voltage_level >= 2) {
		context->bw.dcn.watermarks.b = context->bw.dcn.watermarks.a;
		context->bw.dcn.watermarks.c = context->bw.dcn.watermarks.a;
	}
	if (v->voltage_level >= 3)
		context->bw.dcn.watermarks.d = context->bw.dcn.watermarks.a;
}

static bool dcn_bw_apply_registry_override(struct core_dc *dc)
{
	bool updated = false;

	kernel_fpu_begin();
	if ((int)(dc->dcn_soc.sr_exit_time * 1000) != dc->public.debug.sr_exit_time_ns
			&& dc->public.debug.sr_exit_time_ns) {
		updated = true;
		dc->dcn_soc.sr_exit_time = dc->public.debug.sr_exit_time_ns / 1000.0;
	}

	if ((int)(dc->dcn_soc.sr_enter_plus_exit_time * 1000)
				!= dc->public.debug.sr_enter_plus_exit_time_ns
			&& dc->public.debug.sr_enter_plus_exit_time_ns) {
		updated = true;
		dc->dcn_soc.sr_enter_plus_exit_time =
				dc->public.debug.sr_enter_plus_exit_time_ns / 1000.0;
	}

	if ((int)(dc->dcn_soc.urgent_latency * 1000) != dc->public.debug.urgent_latency_ns
			&& dc->public.debug.urgent_latency_ns) {
		updated = true;
		dc->dcn_soc.urgent_latency = dc->public.debug.urgent_latency_ns / 1000.0;
	}

	if ((int)(dc->dcn_soc.percent_of_ideal_drambw_received_after_urg_latency * 1000)
				!= dc->public.debug.percent_of_ideal_drambw
			&& dc->public.debug.percent_of_ideal_drambw) {
		updated = true;
		dc->dcn_soc.percent_of_ideal_drambw_received_after_urg_latency =
				dc->public.debug.percent_of_ideal_drambw;
	}

	if ((int)(dc->dcn_soc.dram_clock_change_latency * 1000)
				!= dc->public.debug.dram_clock_change_latency_ns
			&& dc->public.debug.dram_clock_change_latency_ns) {
		updated = true;
		dc->dcn_soc.dram_clock_change_latency =
				dc->public.debug.dram_clock_change_latency_ns / 1000.0;
	}
	kernel_fpu_end();

	return updated;
}

bool dcn_validate_bandwidth(
		const struct core_dc *dc,
		struct validate_context *context)
{
	const struct resource_pool *pool = dc->res_pool;
	struct dcn_bw_internal_vars *v = &context->dcn_bw_vars;
	int i, input_idx;
	int vesa_sync_start, asic_blank_end, asic_blank_start;

	if (dcn_bw_apply_registry_override(DC_TO_CORE(&dc->public)))
		dcn_bw_sync_calcs_and_dml(DC_TO_CORE(&dc->public));

	memset(v, 0, sizeof(*v));
	kernel_fpu_begin();
	v->sr_exit_time = dc->dcn_soc.sr_exit_time;
	v->sr_enter_plus_exit_time = dc->dcn_soc.sr_enter_plus_exit_time;
	v->urgent_latency = dc->dcn_soc.urgent_latency;
	v->write_back_latency = dc->dcn_soc.write_back_latency;
	v->percent_of_ideal_drambw_received_after_urg_latency =
			dc->dcn_soc.percent_of_ideal_drambw_received_after_urg_latency;

	v->dcfclkv_min0p65 = dc->dcn_soc.dcfclkv_min0p65;
	v->dcfclkv_mid0p72 = dc->dcn_soc.dcfclkv_mid0p72;
	v->dcfclkv_nom0p8 = dc->dcn_soc.dcfclkv_nom0p8;
	v->dcfclkv_max0p9 = dc->dcn_soc.dcfclkv_max0p9;

	v->max_dispclk_vmin0p65 = dc->dcn_soc.max_dispclk_vmin0p65;
	v->max_dispclk_vmid0p72 = dc->dcn_soc.max_dispclk_vmid0p72;
	v->max_dispclk_vnom0p8 = dc->dcn_soc.max_dispclk_vnom0p8;
	v->max_dispclk_vmax0p9 = dc->dcn_soc.max_dispclk_vmax0p9;

	v->max_dppclk_vmin0p65 = dc->dcn_soc.max_dppclk_vmin0p65;
	v->max_dppclk_vmid0p72 = dc->dcn_soc.max_dppclk_vmid0p72;
	v->max_dppclk_vnom0p8 = dc->dcn_soc.max_dppclk_vnom0p8;
	v->max_dppclk_vmax0p9 = dc->dcn_soc.max_dppclk_vmax0p9;

	v->socclk = dc->dcn_soc.socclk;

	v->fabric_and_dram_bandwidth_vmin0p65 = dc->dcn_soc.fabric_and_dram_bandwidth_vmin0p65;
	v->fabric_and_dram_bandwidth_vmid0p72 = dc->dcn_soc.fabric_and_dram_bandwidth_vmid0p72;
	v->fabric_and_dram_bandwidth_vnom0p8 = dc->dcn_soc.fabric_and_dram_bandwidth_vnom0p8;
	v->fabric_and_dram_bandwidth_vmax0p9 = dc->dcn_soc.fabric_and_dram_bandwidth_vmax0p9;

	v->phyclkv_min0p65 = dc->dcn_soc.phyclkv_min0p65;
	v->phyclkv_mid0p72 = dc->dcn_soc.phyclkv_mid0p72;
	v->phyclkv_nom0p8 = dc->dcn_soc.phyclkv_nom0p8;
	v->phyclkv_max0p9 = dc->dcn_soc.phyclkv_max0p9;

	v->downspreading = dc->dcn_soc.downspreading;
	v->round_trip_ping_latency_cycles = dc->dcn_soc.round_trip_ping_latency_cycles;
	v->urgent_out_of_order_return_per_channel = dc->dcn_soc.urgent_out_of_order_return_per_channel;
	v->number_of_channels = dc->dcn_soc.number_of_channels;
	v->vmm_page_size = dc->dcn_soc.vmm_page_size;
	v->dram_clock_change_latency = dc->dcn_soc.dram_clock_change_latency;
	v->return_bus_width = dc->dcn_soc.return_bus_width;

	v->rob_buffer_size_in_kbyte = dc->dcn_ip.rob_buffer_size_in_kbyte;
	v->det_buffer_size_in_kbyte = dc->dcn_ip.det_buffer_size_in_kbyte;
	v->dpp_output_buffer_pixels = dc->dcn_ip.dpp_output_buffer_pixels;
	v->opp_output_buffer_lines = dc->dcn_ip.opp_output_buffer_lines;
	v->pixel_chunk_size_in_kbyte = dc->dcn_ip.pixel_chunk_size_in_kbyte;
	v->pte_enable = dc->dcn_ip.pte_enable;
	v->pte_chunk_size = dc->dcn_ip.pte_chunk_size;
	v->meta_chunk_size = dc->dcn_ip.meta_chunk_size;
	v->writeback_chunk_size = dc->dcn_ip.writeback_chunk_size;
	v->odm_capability = dc->dcn_ip.odm_capability;
	v->dsc_capability = dc->dcn_ip.dsc_capability;
	v->line_buffer_size = dc->dcn_ip.line_buffer_size;
	v->is_line_buffer_bpp_fixed = dc->dcn_ip.is_line_buffer_bpp_fixed;
	v->line_buffer_fixed_bpp = dc->dcn_ip.line_buffer_fixed_bpp;
	v->max_line_buffer_lines = dc->dcn_ip.max_line_buffer_lines;
	v->writeback_luma_buffer_size = dc->dcn_ip.writeback_luma_buffer_size;
	v->writeback_chroma_buffer_size = dc->dcn_ip.writeback_chroma_buffer_size;
	v->max_num_dpp = dc->dcn_ip.max_num_dpp;
	v->max_num_writeback = dc->dcn_ip.max_num_writeback;
	v->max_dchub_topscl_throughput = dc->dcn_ip.max_dchub_topscl_throughput;
	v->max_pscl_tolb_throughput = dc->dcn_ip.max_pscl_tolb_throughput;
	v->max_lb_tovscl_throughput = dc->dcn_ip.max_lb_tovscl_throughput;
	v->max_vscl_tohscl_throughput = dc->dcn_ip.max_vscl_tohscl_throughput;
	v->max_hscl_ratio = dc->dcn_ip.max_hscl_ratio;
	v->max_vscl_ratio = dc->dcn_ip.max_vscl_ratio;
	v->max_hscl_taps = dc->dcn_ip.max_hscl_taps;
	v->max_vscl_taps = dc->dcn_ip.max_vscl_taps;
	v->under_scan_factor = dc->dcn_ip.under_scan_factor;
	v->pte_buffer_size_in_requests = dc->dcn_ip.pte_buffer_size_in_requests;
	v->dispclk_ramping_margin = dc->dcn_ip.dispclk_ramping_margin;
	v->max_inter_dcn_tile_repeaters = dc->dcn_ip.max_inter_dcn_tile_repeaters;
	v->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one =
			dc->dcn_ip.can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one;
	v->bug_forcing_luma_and_chroma_request_to_same_size_fixed =
			dc->dcn_ip.bug_forcing_luma_and_chroma_request_to_same_size_fixed;

	v->voltage[5] = dcn_bw_no_support;
	v->voltage[4] = dcn_bw_v_max0p9;
	v->voltage[3] = dcn_bw_v_max0p9;
	v->voltage[2] = dcn_bw_v_nom0p8;
	v->voltage[1] = dcn_bw_v_mid0p72;
	v->voltage[0] = dcn_bw_v_min0p65;
	v->fabric_and_dram_bandwidth_per_state[5] = v->fabric_and_dram_bandwidth_vmax0p9;
	v->fabric_and_dram_bandwidth_per_state[4] = v->fabric_and_dram_bandwidth_vmax0p9;
	v->fabric_and_dram_bandwidth_per_state[3] = v->fabric_and_dram_bandwidth_vmax0p9;
	v->fabric_and_dram_bandwidth_per_state[2] = v->fabric_and_dram_bandwidth_vnom0p8;
	v->fabric_and_dram_bandwidth_per_state[1] = v->fabric_and_dram_bandwidth_vmid0p72;
	v->fabric_and_dram_bandwidth_per_state[0] = v->fabric_and_dram_bandwidth_vmin0p65;
	v->dcfclk_per_state[5] = v->dcfclkv_max0p9;
	v->dcfclk_per_state[4] = v->dcfclkv_max0p9;
	v->dcfclk_per_state[3] = v->dcfclkv_max0p9;
	v->dcfclk_per_state[2] = v->dcfclkv_nom0p8;
	v->dcfclk_per_state[1] = v->dcfclkv_mid0p72;
	v->dcfclk_per_state[0] = v->dcfclkv_min0p65;
	v->max_dispclk[5] = v->max_dispclk_vmax0p9;
	v->max_dispclk[4] = v->max_dispclk_vmax0p9;
	v->max_dispclk[3] = v->max_dispclk_vmax0p9;
	v->max_dispclk[2] = v->max_dispclk_vnom0p8;
	v->max_dispclk[1] = v->max_dispclk_vmid0p72;
	v->max_dispclk[0] = v->max_dispclk_vmin0p65;
	v->max_dppclk[5] = v->max_dppclk_vmax0p9;
	v->max_dppclk[4] = v->max_dppclk_vmax0p9;
	v->max_dppclk[3] = v->max_dppclk_vmax0p9;
	v->max_dppclk[2] = v->max_dppclk_vnom0p8;
	v->max_dppclk[1] = v->max_dppclk_vmid0p72;
	v->max_dppclk[0] = v->max_dppclk_vmin0p65;
	v->phyclk_per_state[5] = v->phyclkv_max0p9;
	v->phyclk_per_state[4] = v->phyclkv_max0p9;
	v->phyclk_per_state[3] = v->phyclkv_max0p9;
	v->phyclk_per_state[2] = v->phyclkv_nom0p8;
	v->phyclk_per_state[1] = v->phyclkv_mid0p72;
	v->phyclk_per_state[0] = v->phyclkv_min0p65;

	if (dc->public.debug.use_max_voltage) {
		v->max_dppclk[1] = v->max_dppclk_vnom0p8;
		v->max_dppclk[0] = v->max_dppclk_vnom0p8;
	}

	if (v->voltage_override == dcn_bw_v_max0p9) {
		v->voltage_override_level = number_of_states - 1;
	} else if (v->voltage_override == dcn_bw_v_nom0p8) {
		v->voltage_override_level = number_of_states - 2;
	} else if (v->voltage_override == dcn_bw_v_mid0p72) {
		v->voltage_override_level = number_of_states - 3;
	} else {
		v->voltage_override_level = 0;
	}
	v->synchronized_vblank = dcn_bw_no;
	v->ta_pscalculation = dcn_bw_override;
	v->allow_different_hratio_vratio = dcn_bw_yes;


	for (i = 0, input_idx = 0; i < pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;
		/* skip all but first of split pipes */
		if (pipe->top_pipe && pipe->top_pipe->surface == pipe->surface)
			continue;

		v->underscan_output[input_idx] = false; /* taken care of in recout already*/
		v->interlace_output[input_idx] = false;

		v->htotal[input_idx] = pipe->stream->public.timing.h_total;
		v->vtotal[input_idx] = pipe->stream->public.timing.v_total;
		v->v_sync_plus_back_porch[input_idx] = pipe->stream->public.timing.v_total
				- pipe->stream->public.timing.v_addressable
				- pipe->stream->public.timing.v_front_porch;
		v->vactive[input_idx] = pipe->stream->public.timing.v_addressable;
		v->pixel_clock[input_idx] = pipe->stream->public.timing.pix_clk_khz / 1000.0f;


		if (!pipe->surface){
			v->dcc_enable[input_idx] = dcn_bw_yes;
			v->source_pixel_format[input_idx] = dcn_bw_rgb_sub_32;
			v->source_surface_mode[input_idx] = dcn_bw_sw_4_kb_s;
			v->lb_bit_per_pixel[input_idx] = 30;
			v->viewport_width[input_idx] = pipe->stream->public.timing.h_addressable;
			v->viewport_height[input_idx] = pipe->stream->public.timing.v_addressable;
			v->scaler_rec_out_width[input_idx] = pipe->stream->public.timing.h_addressable;
			v->scaler_recout_height[input_idx] = pipe->stream->public.timing.v_addressable;
			v->override_hta_ps[input_idx] = 1;
			v->override_vta_ps[input_idx] = 1;
			v->override_hta_pschroma[input_idx] = 1;
			v->override_vta_pschroma[input_idx] = 1;
			v->source_scan[input_idx] = dcn_bw_hor;

		} else {
			v->viewport_height[input_idx] =  pipe->scl_data.viewport.height;
			v->viewport_width[input_idx] = pipe->scl_data.viewport.width;
			v->scaler_rec_out_width[input_idx] = pipe->scl_data.recout.width;
			v->scaler_recout_height[input_idx] = pipe->scl_data.recout.height;
			if (pipe->bottom_pipe && pipe->bottom_pipe->surface == pipe->surface) {
				if (pipe->surface->public.rotation % 2 == 0) {
					int viewport_end = pipe->scl_data.viewport.width
							+ pipe->scl_data.viewport.x;
					int viewport_b_end = pipe->bottom_pipe->scl_data.viewport.width
							+ pipe->bottom_pipe->scl_data.viewport.x;

					if (viewport_end > viewport_b_end)
						v->viewport_width[input_idx] = viewport_end
							- pipe->bottom_pipe->scl_data.viewport.x;
					else
						v->viewport_width[input_idx] = viewport_b_end
									- pipe->scl_data.viewport.x;
				} else  {
					int viewport_end = pipe->scl_data.viewport.height
						+ pipe->scl_data.viewport.y;
					int viewport_b_end = pipe->bottom_pipe->scl_data.viewport.height
						+ pipe->bottom_pipe->scl_data.viewport.y;

					if (viewport_end > viewport_b_end)
						v->viewport_height[input_idx] = viewport_end
							- pipe->bottom_pipe->scl_data.viewport.y;
					else
						v->viewport_height[input_idx] = viewport_b_end
									- pipe->scl_data.viewport.y;
				}
				v->scaler_rec_out_width[input_idx] = pipe->scl_data.recout.width
						+ pipe->bottom_pipe->scl_data.recout.width;
			}

			v->dcc_enable[input_idx] = pipe->surface->public.dcc.enable ? dcn_bw_yes : dcn_bw_no;
			v->source_pixel_format[input_idx] = tl_pixel_format_to_bw_defs(
					pipe->surface->public.format);
			v->source_surface_mode[input_idx] = tl_sw_mode_to_bw_defs(
					pipe->surface->public.tiling_info.gfx9.swizzle);
			v->lb_bit_per_pixel[input_idx] = tl_lb_bpp_to_int(pipe->scl_data.lb_params.depth);
			v->override_hta_ps[input_idx] = pipe->scl_data.taps.h_taps;
			v->override_vta_ps[input_idx] = pipe->scl_data.taps.v_taps;
			v->override_hta_pschroma[input_idx] = pipe->scl_data.taps.h_taps_c;
			v->override_vta_pschroma[input_idx] = pipe->scl_data.taps.v_taps_c;
			v->source_scan[input_idx] = (pipe->surface->public.rotation % 2) ? dcn_bw_vert : dcn_bw_hor;
		}
		if (v->is_line_buffer_bpp_fixed == dcn_bw_yes)
			v->lb_bit_per_pixel[input_idx] = v->line_buffer_fixed_bpp;
		v->dcc_rate[input_idx] = 1; /*TODO: Worst case? does this change?*/
		v->output_format[input_idx] = dcn_bw_444;
		v->output[input_idx] = dcn_bw_dp;

		input_idx++;
	}
	v->number_of_active_planes = input_idx;

	scaler_settings_calculation(v);
	mode_support_and_system_configuration(v);

	if (v->voltage_level != 5) {
		float bw_consumed = v->total_bandwidth_consumed_gbyte_per_second;
		if (bw_consumed < v->fabric_and_dram_bandwidth_vmin0p65)
			bw_consumed = v->fabric_and_dram_bandwidth_vmin0p65;
		else if (bw_consumed < v->fabric_and_dram_bandwidth_vmid0p72)
			bw_consumed = v->fabric_and_dram_bandwidth_vmid0p72;
		else if (bw_consumed < v->fabric_and_dram_bandwidth_vnom0p8)
			bw_consumed = v->fabric_and_dram_bandwidth_vnom0p8;
		else
			bw_consumed = v->fabric_and_dram_bandwidth_vmax0p9;

		display_pipe_configuration(v);
		calc_wm_sets_and_perf_params(context, v);
		context->bw.dcn.calc_clk.fclk_khz = (int)(bw_consumed * 1000000 /
				(ddr4_dram_factor_single_Channel * v->number_of_channels));
		context->bw.dcn.calc_clk.dram_ccm_us = (int)(v->dram_clock_change_margin);
		context->bw.dcn.calc_clk.min_active_dram_ccm_us = (int)(v->min_active_dram_clock_change_margin);
		context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz = (int)(v->dcf_clk_deep_sleep * 1000);
		context->bw.dcn.calc_clk.dcfclk_khz = (int)(v->dcfclk * 1000);
		context->bw.dcn.calc_clk.dispclk_khz = (int)(v->dispclk * 1000);
		if (dc->public.debug.max_disp_clk == true)
			context->bw.dcn.calc_clk.dispclk_khz = (int)(dc->dcn_soc.max_dispclk_vmax0p9 * 1000);
		context->bw.dcn.calc_clk.dppclk_div = (int)(v->dispclk_dppclk_ratio) == 2;

		for (i = 0, input_idx = 0; i < pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

			/* skip inactive pipe */
			if (!pipe->stream)
				continue;
			/* skip all but first of split pipes */
			if (pipe->top_pipe && pipe->top_pipe->surface == pipe->surface)
				continue;

			pipe->pipe_dlg_param.vupdate_width = v->v_update_width[input_idx];
			pipe->pipe_dlg_param.vupdate_offset = v->v_update_offset[input_idx];
			pipe->pipe_dlg_param.vready_offset = v->v_ready_offset[input_idx];
			pipe->pipe_dlg_param.vstartup_start = v->v_startup[input_idx];

			pipe->pipe_dlg_param.htotal = pipe->stream->public.timing.h_total;
			pipe->pipe_dlg_param.vtotal = pipe->stream->public.timing.v_total;
			vesa_sync_start = pipe->stream->public.timing.v_addressable +
						pipe->stream->public.timing.v_border_bottom +
						pipe->stream->public.timing.v_front_porch;

			asic_blank_end = (pipe->stream->public.timing.v_total -
						vesa_sync_start -
						pipe->stream->public.timing.v_border_top)
			* (pipe->stream->public.timing.flags.INTERLACE ? 1 : 0);

			asic_blank_start = asic_blank_end +
						(pipe->stream->public.timing.v_border_top +
						pipe->stream->public.timing.v_addressable +
						pipe->stream->public.timing.v_border_bottom)
			* (pipe->stream->public.timing.flags.INTERLACE ? 1 : 0);

			pipe->pipe_dlg_param.vblank_start = asic_blank_start;
			pipe->pipe_dlg_param.vblank_end = asic_blank_end;

			if (pipe->surface) {
				struct pipe_ctx *hsplit_pipe = pipe->bottom_pipe;

				if (v->dpp_per_plane[input_idx] == 2 ||
						(pipe->stream->public.timing.timing_3d_format == TIMING_3D_FORMAT_TOP_AND_BOTTOM ||
						 pipe->stream->public.timing.timing_3d_format == TIMING_3D_FORMAT_SIDE_BY_SIDE)) {
					if (hsplit_pipe && hsplit_pipe->surface == pipe->surface) {
						/* update previously split pipe */
						hsplit_pipe->pipe_dlg_param.vupdate_width = v->v_update_width[input_idx];
						hsplit_pipe->pipe_dlg_param.vupdate_offset = v->v_update_offset[input_idx];
						hsplit_pipe->pipe_dlg_param.vready_offset = v->v_ready_offset[input_idx];
						hsplit_pipe->pipe_dlg_param.vstartup_start = v->v_startup[input_idx];

						hsplit_pipe->pipe_dlg_param.htotal = pipe->stream->public.timing.h_total;
						hsplit_pipe->pipe_dlg_param.vtotal = pipe->stream->public.timing.v_total;
						hsplit_pipe->pipe_dlg_param.vblank_start = pipe->pipe_dlg_param.vblank_start;
						hsplit_pipe->pipe_dlg_param.vblank_end = pipe->pipe_dlg_param.vblank_end;
					} else {
						/* pipe not split previously needs split */
						hsplit_pipe = find_idle_secondary_pipe(&context->res_ctx, pool);
						ASSERT(hsplit_pipe);
						split_stream_across_pipes(
							&context->res_ctx, pool,
							pipe, hsplit_pipe);
					}

					dcn_bw_calc_rq_dlg_ttu(dc, v, hsplit_pipe);
				} else if (hsplit_pipe && hsplit_pipe->surface == pipe->surface) {
					/* merge previously split pipe */
					if (pipe->bottom_pipe->bottom_pipe)
						pipe->bottom_pipe->bottom_pipe->top_pipe = pipe;
					memset(pipe->bottom_pipe, 0, sizeof(*pipe->bottom_pipe));
					pipe->bottom_pipe = pipe->bottom_pipe->bottom_pipe;
					resource_build_scaling_params(pipe);
				}
				/* for now important to do this after pipe split for building e2e params */
				dcn_bw_calc_rq_dlg_ttu(dc, v, pipe);
			}

			input_idx++;
		}
		if (dc->public.debug.use_dml_wm)
			dcn_dml_wm_override(v, (struct display_mode_lib *)
					&dc->dml, context, pool);
	}

	kernel_fpu_end();
	return v->voltage_level != 5;
}

unsigned int dcn_find_normalized_clock_vdd_Level(
	const struct core_dc *dc,
	enum dm_pp_clock_type clocks_type,
	int clocks_in_khz)
{
	int vdd_level = dcn_bw_v_min0p65;

	if (clocks_in_khz == 0)/*todo some clock not in the considerations*/
		return vdd_level;

	switch (clocks_type) {
	case DM_PP_CLOCK_TYPE_DISPLAY_CLK:
		if (clocks_in_khz > dc->dcn_soc.max_dispclk_vmax0p9*1000) {
			vdd_level = dcn_bw_v_max0p91;
			BREAK_TO_DEBUGGER();
		} else if (clocks_in_khz > dc->dcn_soc.max_dispclk_vnom0p8*1000) {
			vdd_level = dcn_bw_v_max0p9;
		} else if (clocks_in_khz > dc->dcn_soc.max_dispclk_vmid0p72*1000) {
			vdd_level = dcn_bw_v_nom0p8;
		} else if (clocks_in_khz > dc->dcn_soc.max_dispclk_vmin0p65*1000) {
			vdd_level = dcn_bw_v_mid0p72;
		} else
			vdd_level = dcn_bw_v_min0p65;
		break;
	case DM_PP_CLOCK_TYPE_DISPLAYPHYCLK:
		if (clocks_in_khz > dc->dcn_soc.phyclkv_max0p9*1000) {
			vdd_level = dcn_bw_v_max0p91;
			BREAK_TO_DEBUGGER();
		} else if (clocks_in_khz > dc->dcn_soc.phyclkv_nom0p8*1000) {
			vdd_level = dcn_bw_v_max0p9;
		} else if (clocks_in_khz > dc->dcn_soc.phyclkv_mid0p72*1000) {
			vdd_level = dcn_bw_v_nom0p8;
		} else if (clocks_in_khz > dc->dcn_soc.phyclkv_min0p65*1000) {
			vdd_level = dcn_bw_v_mid0p72;
		} else
			vdd_level = dcn_bw_v_min0p65;
		break;

	case DM_PP_CLOCK_TYPE_DPPCLK:
		if (clocks_in_khz > dc->dcn_soc.max_dppclk_vmax0p9*1000) {
			vdd_level = dcn_bw_v_max0p91;
			BREAK_TO_DEBUGGER();
		} else if (clocks_in_khz > dc->dcn_soc.max_dppclk_vnom0p8*1000) {
			vdd_level = dcn_bw_v_max0p9;
		} else if (clocks_in_khz > dc->dcn_soc.max_dppclk_vmid0p72*1000) {
			vdd_level = dcn_bw_v_nom0p8;
		} else if (clocks_in_khz > dc->dcn_soc.max_dppclk_vmin0p65*1000) {
			vdd_level = dcn_bw_v_mid0p72;
		} else
			vdd_level = dcn_bw_v_min0p65;
		break;

	case DM_PP_CLOCK_TYPE_MEMORY_CLK:
		{
			unsigned factor = (ddr4_dram_factor_single_Channel * dc->dcn_soc.number_of_channels);
			if (clocks_in_khz > dc->dcn_soc.fabric_and_dram_bandwidth_vmax0p9*1000000/factor) {
			vdd_level = dcn_bw_v_max0p91;
				BREAK_TO_DEBUGGER();
			} else if (clocks_in_khz > dc->dcn_soc.fabric_and_dram_bandwidth_vnom0p8*1000000/factor) {
				vdd_level = dcn_bw_v_max0p9;
			} else if (clocks_in_khz > dc->dcn_soc.fabric_and_dram_bandwidth_vmid0p72*1000000/factor) {
				vdd_level = dcn_bw_v_nom0p8;
			} else if (clocks_in_khz > dc->dcn_soc.fabric_and_dram_bandwidth_vmin0p65*1000000/factor) {
				vdd_level = dcn_bw_v_mid0p72;
			} else
				vdd_level = dcn_bw_v_min0p65;
		}
		break;

	case DM_PP_CLOCK_TYPE_DCFCLK:
		if (clocks_in_khz > dc->dcn_soc.dcfclkv_max0p9*1000) {
			vdd_level = dcn_bw_v_max0p91;
			BREAK_TO_DEBUGGER();
		} else if (clocks_in_khz > dc->dcn_soc.dcfclkv_nom0p8*1000) {
			vdd_level = dcn_bw_v_max0p9;
		} else if (clocks_in_khz > dc->dcn_soc.dcfclkv_mid0p72*1000) {
			vdd_level = dcn_bw_v_nom0p8;
		} else if (clocks_in_khz > dc->dcn_soc.dcfclkv_min0p65*1000) {
			vdd_level = dcn_bw_v_mid0p72;
		} else
			vdd_level = dcn_bw_v_min0p65;
		break;

	default:
		 break;
	}
	return vdd_level;
}

unsigned int dcn_find_dcfclk_suits_all(
	const struct core_dc *dc,
	struct clocks_value *clocks)
{
	unsigned vdd_level, vdd_level_temp;
	unsigned dcf_clk;

	/*find a common supported voltage level*/
	vdd_level = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_DISPLAY_CLK, clocks->dispclk_in_khz);
	vdd_level_temp = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_DISPLAYPHYCLK, clocks->phyclk_in_khz);

	vdd_level = dcn_bw_max(vdd_level, vdd_level_temp);
	vdd_level_temp = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_DPPCLK, clocks->dppclk_in_khz);
	vdd_level = dcn_bw_max(vdd_level, vdd_level_temp);

	vdd_level_temp = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_MEMORY_CLK, clocks->dcfclock_in_khz);
	vdd_level = dcn_bw_max(vdd_level, vdd_level_temp);
	vdd_level_temp = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_DCFCLK, clocks->dcfclock_in_khz);

	/*find that level conresponding dcfclk*/
	vdd_level = dcn_bw_max(vdd_level, vdd_level_temp);
	if (vdd_level == dcn_bw_v_max0p91) {
		BREAK_TO_DEBUGGER();
		dcf_clk = dc->dcn_soc.dcfclkv_max0p9*1000;
	} else if (vdd_level == dcn_bw_v_max0p9)
		dcf_clk =  dc->dcn_soc.dcfclkv_max0p9*1000;
	else if (vdd_level == dcn_bw_v_nom0p8)
		dcf_clk =  dc->dcn_soc.dcfclkv_nom0p8*1000;
	else if (vdd_level == dcn_bw_v_mid0p72)
		dcf_clk =  dc->dcn_soc.dcfclkv_mid0p72*1000;
	else
		dcf_clk =  dc->dcn_soc.dcfclkv_min0p65*1000;

	dm_logger_write(dc->ctx->logger, LOG_HW_MARKS,
		"\tdcf_clk for voltage = %d\n", dcf_clk);
	return dcf_clk;
}

void dcn_bw_update_from_pplib(struct core_dc *dc)
{
	struct dc_context *ctx = dc->ctx;
	struct dm_pp_clock_levels_with_latency clks = {0};
	struct dm_pp_clock_levels_with_voltage clks2 = {0};

	kernel_fpu_begin();
	dc->dcn_soc.number_of_channels = dc->ctx->asic_id.vram_width / ddr4_dram_width;
	ASSERT(dc->dcn_soc.number_of_channels && dc->dcn_soc.number_of_channels < 3);
	if (dc->dcn_soc.number_of_channels == 0)/*old sbios bug*/
		dc->dcn_soc.number_of_channels = 2;

	if (dm_pp_get_clock_levels_by_type_with_voltage(
				ctx, DM_PP_CLOCK_TYPE_DISPLAY_CLK, &clks2) &&
				clks2.num_levels >= 3) {
		dc->dcn_soc.max_dispclk_vmin0p65 = clks2.data[0].clocks_in_khz / 1000.0;
		dc->dcn_soc.max_dispclk_vmid0p72 = clks2.data[clks2.num_levels - 3].clocks_in_khz / 1000.0;
		dc->dcn_soc.max_dispclk_vnom0p8 = clks2.data[clks2.num_levels - 2].clocks_in_khz / 1000.0;
		dc->dcn_soc.max_dispclk_vmax0p9 = clks2.data[clks2.num_levels - 1].clocks_in_khz / 1000.0;
	} else
		BREAK_TO_DEBUGGER();
/*
	if (dm_pp_get_clock_levels_by_type_with_latency(
			ctx, DM_PP_CLOCK_TYPE_MEMORY_CLK, &clks) &&
			clks.num_levels != 0) {
			//this  is to get DRAM data_rate
		//FabricAndDRAMBandwidth = min(64*FCLK , Data rate * single_Channel_Width * number of channels);
	}*/
	if (dm_pp_get_clock_levels_by_type_with_latency(
			ctx, DM_PP_CLOCK_TYPE_FCLK, &clks) &&
			clks.num_levels != 0) {
		ASSERT(clks.num_levels >= 3);
		dc->dcn_soc.fabric_and_dram_bandwidth_vmin0p65 = dc->dcn_soc.number_of_channels *
			(clks.data[0].clocks_in_khz / 1000.0) * ddr4_dram_factor_single_Channel / 1000.0;
		if (clks.num_levels > 2) {
			dc->dcn_soc.fabric_and_dram_bandwidth_vmid0p72 = dc->dcn_soc.number_of_channels *
					(clks.data[clks.num_levels - 3].clocks_in_khz / 1000.0) * ddr4_dram_factor_single_Channel / 1000.0;
		} else {
			dc->dcn_soc.fabric_and_dram_bandwidth_vmid0p72 = dc->dcn_soc.number_of_channels *
					(clks.data[clks.num_levels - 2].clocks_in_khz / 1000.0) * ddr4_dram_factor_single_Channel / 1000.0;
		}
		dc->dcn_soc.fabric_and_dram_bandwidth_vnom0p8 = dc->dcn_soc.number_of_channels *
				(clks.data[clks.num_levels - 2].clocks_in_khz / 1000.0) * ddr4_dram_factor_single_Channel / 1000.0;
		dc->dcn_soc.fabric_and_dram_bandwidth_vmax0p9 = dc->dcn_soc.number_of_channels *
				(clks.data[clks.num_levels - 1].clocks_in_khz / 1000.0) * ddr4_dram_factor_single_Channel / 1000.0;
	} else
		BREAK_TO_DEBUGGER();
	if (dm_pp_get_clock_levels_by_type_with_latency(
				ctx, DM_PP_CLOCK_TYPE_DCFCLK, &clks) &&
				clks.num_levels >= 3) {
		dc->dcn_soc.dcfclkv_min0p65 = clks.data[0].clocks_in_khz / 1000.0;
		dc->dcn_soc.dcfclkv_mid0p72 = clks.data[clks.num_levels - 3].clocks_in_khz / 1000.0;
		dc->dcn_soc.dcfclkv_nom0p8 = clks.data[clks.num_levels - 2].clocks_in_khz / 1000.0;
		dc->dcn_soc.dcfclkv_max0p9 = clks.data[clks.num_levels - 1].clocks_in_khz / 1000.0;
	} else
		BREAK_TO_DEBUGGER();
	if (dm_pp_get_clock_levels_by_type_with_voltage(
				ctx, DM_PP_CLOCK_TYPE_DISPLAYPHYCLK, &clks2) &&
				clks2.num_levels >= 3) {
		dc->dcn_soc.phyclkv_min0p65 = clks2.data[0].clocks_in_khz / 1000.0;
		dc->dcn_soc.phyclkv_mid0p72 = clks2.data[clks2.num_levels - 3].clocks_in_khz / 1000.0;
		dc->dcn_soc.phyclkv_nom0p8 = clks2.data[clks2.num_levels - 2].clocks_in_khz / 1000.0;
		dc->dcn_soc.phyclkv_max0p9 = clks2.data[clks2.num_levels - 1].clocks_in_khz / 1000.0;
	} else
		BREAK_TO_DEBUGGER();
	if (dm_pp_get_clock_levels_by_type_with_latency(
				ctx, DM_PP_CLOCK_TYPE_DPPCLK, &clks) &&
				clks.num_levels >= 3) {
		dc->dcn_soc.max_dppclk_vmin0p65 = clks.data[0].clocks_in_khz / 1000.0;
		dc->dcn_soc.max_dppclk_vmid0p72 = clks.data[clks.num_levels - 3].clocks_in_khz / 1000.0;
		dc->dcn_soc.max_dppclk_vnom0p8 = clks.data[clks.num_levels - 2].clocks_in_khz / 1000.0;
		dc->dcn_soc.max_dppclk_vmax0p9 = clks.data[clks.num_levels - 1].clocks_in_khz / 1000.0;
	}

	if (dm_pp_get_clock_levels_by_type_with_latency(
				ctx, DM_PP_CLOCK_TYPE_SOCCLK, &clks) &&
				clks.num_levels >= 3) {
		dc->dcn_soc.socclk = clks.data[0].clocks_in_khz / 1000.0;
	} else
			BREAK_TO_DEBUGGER();
	kernel_fpu_end();
}

void dcn_bw_notify_pplib_of_wm_ranges(struct core_dc *dc)
{
	struct dm_pp_wm_sets_with_clock_ranges_soc15 clk_ranges = {0};
	int max_fclk_khz, nom_fclk_khz, min_fclk_khz, max_dcfclk_khz,
		nom_dcfclk_khz, min_dcfclk_khz, socclk_khz;
	const int overdrive = 5000000; /* 5 GHz to cover Overdrive */
	unsigned factor = (ddr4_dram_factor_single_Channel * dc->dcn_soc.number_of_channels);

	kernel_fpu_begin();
	max_fclk_khz = dc->dcn_soc.fabric_and_dram_bandwidth_vmax0p9 * 1000000 / factor;
	nom_fclk_khz = dc->dcn_soc.fabric_and_dram_bandwidth_vnom0p8 * 1000000 / factor;
	min_fclk_khz = dc->dcn_soc.fabric_and_dram_bandwidth_vmin0p65 * 1000000 / factor;
	max_dcfclk_khz = dc->dcn_soc.dcfclkv_max0p9 * 1000;
	nom_dcfclk_khz = dc->dcn_soc.dcfclkv_nom0p8 * 1000;
	min_dcfclk_khz = dc->dcn_soc.dcfclkv_min0p65 * 1000;
	socclk_khz = dc->dcn_soc.socclk * 1000;
	kernel_fpu_end();

	/* Now notify PPLib/SMU about which Watermarks sets they should select
	 * depending on DPM state they are in. And update BW MGR GFX Engine and
	 * Memory clock member variables for Watermarks calculations for each
	 * Watermark Set
	 */
	/* SOCCLK does not affect anytihng but writeback for DCN so for now we dont
	 * care what the value is, hence min to overdrive level
	 */
	clk_ranges.num_wm_dmif_sets = 4;
	clk_ranges.num_wm_mcif_sets = 4;
	clk_ranges.wm_dmif_clocks_ranges[0].wm_set_id = WM_SET_A;
	clk_ranges.wm_dmif_clocks_ranges[0].wm_min_dcfclk_clk_in_khz = min_dcfclk_khz;
	clk_ranges.wm_dmif_clocks_ranges[0].wm_max_dcfclk_clk_in_khz = nom_dcfclk_khz - 1;
	clk_ranges.wm_dmif_clocks_ranges[0].wm_min_memg_clk_in_khz = min_fclk_khz;
	clk_ranges.wm_dmif_clocks_ranges[0].wm_max_mem_clk_in_khz = nom_fclk_khz - 1;
	clk_ranges.wm_mcif_clocks_ranges[0].wm_set_id = WM_SET_A;
	clk_ranges.wm_mcif_clocks_ranges[0].wm_min_socclk_clk_in_khz = socclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[0].wm_max_socclk_clk_in_khz = overdrive;
	clk_ranges.wm_mcif_clocks_ranges[0].wm_min_memg_clk_in_khz = min_fclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[0].wm_max_mem_clk_in_khz = nom_fclk_khz - 1;

	clk_ranges.wm_dmif_clocks_ranges[1].wm_set_id = WM_SET_B;
	clk_ranges.wm_dmif_clocks_ranges[1].wm_min_dcfclk_clk_in_khz = min_dcfclk_khz;
	clk_ranges.wm_dmif_clocks_ranges[1].wm_max_dcfclk_clk_in_khz = nom_dcfclk_khz - 1;
	clk_ranges.wm_dmif_clocks_ranges[1].wm_min_memg_clk_in_khz = nom_fclk_khz;
	clk_ranges.wm_dmif_clocks_ranges[1].wm_max_mem_clk_in_khz = max_fclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[1].wm_set_id = WM_SET_B;
	clk_ranges.wm_mcif_clocks_ranges[1].wm_min_socclk_clk_in_khz = socclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[1].wm_max_socclk_clk_in_khz = overdrive;
	clk_ranges.wm_mcif_clocks_ranges[1].wm_min_memg_clk_in_khz = nom_fclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[1].wm_max_mem_clk_in_khz = max_fclk_khz;


	clk_ranges.wm_dmif_clocks_ranges[2].wm_set_id = WM_SET_C;
	clk_ranges.wm_dmif_clocks_ranges[2].wm_min_dcfclk_clk_in_khz = nom_dcfclk_khz;
	clk_ranges.wm_dmif_clocks_ranges[2].wm_max_dcfclk_clk_in_khz = max_dcfclk_khz;
	clk_ranges.wm_dmif_clocks_ranges[2].wm_min_memg_clk_in_khz = nom_fclk_khz;
	clk_ranges.wm_dmif_clocks_ranges[2].wm_max_mem_clk_in_khz = max_fclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[2].wm_set_id = WM_SET_C;
	clk_ranges.wm_mcif_clocks_ranges[2].wm_min_socclk_clk_in_khz = socclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[2].wm_max_socclk_clk_in_khz = overdrive;
	clk_ranges.wm_mcif_clocks_ranges[2].wm_min_memg_clk_in_khz = nom_fclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[2].wm_max_mem_clk_in_khz = max_fclk_khz;

	clk_ranges.wm_dmif_clocks_ranges[3].wm_set_id = WM_SET_D;
	clk_ranges.wm_dmif_clocks_ranges[3].wm_min_dcfclk_clk_in_khz = max_dcfclk_khz + 1;
	clk_ranges.wm_dmif_clocks_ranges[3].wm_max_dcfclk_clk_in_khz = overdrive;
	clk_ranges.wm_dmif_clocks_ranges[3].wm_min_memg_clk_in_khz = max_fclk_khz + 1;
	clk_ranges.wm_dmif_clocks_ranges[3].wm_max_mem_clk_in_khz = overdrive;
	clk_ranges.wm_mcif_clocks_ranges[3].wm_set_id = WM_SET_D;
	clk_ranges.wm_mcif_clocks_ranges[3].wm_min_socclk_clk_in_khz = socclk_khz;
	clk_ranges.wm_mcif_clocks_ranges[3].wm_max_socclk_clk_in_khz = overdrive;
	clk_ranges.wm_mcif_clocks_ranges[3].wm_min_memg_clk_in_khz = max_fclk_khz + 1;
	clk_ranges.wm_mcif_clocks_ranges[3].wm_max_mem_clk_in_khz = overdrive;

	/* Notify PP Lib/SMU which Watermarks to use for which clock ranges */
	dm_pp_notify_wm_clock_changes_soc15(dc->ctx, &clk_ranges);
}

void dcn_bw_sync_calcs_and_dml(struct core_dc *dc)
{
	kernel_fpu_begin();
	dc->dml.soc.vmin.socclk_mhz = dc->dcn_soc.socclk;
	dc->dml.soc.vmid.socclk_mhz = dc->dcn_soc.socclk;
	dc->dml.soc.vnom.socclk_mhz = dc->dcn_soc.socclk;
	dc->dml.soc.vmax.socclk_mhz = dc->dcn_soc.socclk;

	dc->dml.soc.vmin.dcfclk_mhz = dc->dcn_soc.dcfclkv_min0p65;
	dc->dml.soc.vmid.dcfclk_mhz = dc->dcn_soc.dcfclkv_mid0p72;
	dc->dml.soc.vnom.dcfclk_mhz = dc->dcn_soc.dcfclkv_nom0p8;
	dc->dml.soc.vmax.dcfclk_mhz = dc->dcn_soc.dcfclkv_max0p9;

	dc->dml.soc.vmin.dispclk_mhz = dc->dcn_soc.max_dispclk_vmin0p65;
	dc->dml.soc.vmid.dispclk_mhz = dc->dcn_soc.max_dispclk_vmid0p72;
	dc->dml.soc.vnom.dispclk_mhz = dc->dcn_soc.max_dispclk_vnom0p8;
	dc->dml.soc.vmax.dispclk_mhz = dc->dcn_soc.max_dispclk_vmax0p9;

	dc->dml.soc.vmin.dppclk_mhz = dc->dcn_soc.max_dppclk_vmin0p65;
	dc->dml.soc.vmid.dppclk_mhz = dc->dcn_soc.max_dppclk_vmid0p72;
	dc->dml.soc.vnom.dppclk_mhz = dc->dcn_soc.max_dppclk_vnom0p8;
	dc->dml.soc.vmax.dppclk_mhz = dc->dcn_soc.max_dppclk_vmax0p9;

	dc->dml.soc.vmin.phyclk_mhz = dc->dcn_soc.phyclkv_min0p65;
	dc->dml.soc.vmid.phyclk_mhz = dc->dcn_soc.phyclkv_mid0p72;
	dc->dml.soc.vnom.phyclk_mhz = dc->dcn_soc.phyclkv_nom0p8;
	dc->dml.soc.vmax.phyclk_mhz = dc->dcn_soc.phyclkv_max0p9;

	dc->dml.soc.vmin.dram_bw_per_chan_gbps = dc->dcn_soc.fabric_and_dram_bandwidth_vmin0p65;
	dc->dml.soc.vmid.dram_bw_per_chan_gbps = dc->dcn_soc.fabric_and_dram_bandwidth_vmid0p72;
	dc->dml.soc.vnom.dram_bw_per_chan_gbps = dc->dcn_soc.fabric_and_dram_bandwidth_vnom0p8;
	dc->dml.soc.vmax.dram_bw_per_chan_gbps = dc->dcn_soc.fabric_and_dram_bandwidth_vmax0p9;

	dc->dml.soc.sr_exit_time_us = dc->dcn_soc.sr_exit_time;
	dc->dml.soc.sr_enter_plus_exit_time_us = dc->dcn_soc.sr_enter_plus_exit_time;
	dc->dml.soc.urgent_latency_us = dc->dcn_soc.urgent_latency;
	dc->dml.soc.writeback_latency_us = dc->dcn_soc.write_back_latency;
	dc->dml.soc.ideal_dram_bw_after_urgent_percent =
			dc->dcn_soc.percent_of_ideal_drambw_received_after_urg_latency;
	dc->dml.soc.max_request_size_bytes = dc->dcn_soc.max_request_size;
	dc->dml.soc.downspread_percent = dc->dcn_soc.downspreading;
	dc->dml.soc.round_trip_ping_latency_dcfclk_cycles =
			dc->dcn_soc.round_trip_ping_latency_cycles;
	dc->dml.soc.urgent_out_of_order_return_per_channel_bytes =
			dc->dcn_soc.urgent_out_of_order_return_per_channel;
	dc->dml.soc.num_chans = dc->dcn_soc.number_of_channels;
	dc->dml.soc.vmm_page_size_bytes = dc->dcn_soc.vmm_page_size;
	dc->dml.soc.dram_clock_change_latency_us = dc->dcn_soc.dram_clock_change_latency;
	dc->dml.soc.return_bus_width_bytes = dc->dcn_soc.return_bus_width;

	dc->dml.ip.rob_buffer_size_kbytes = dc->dcn_ip.rob_buffer_size_in_kbyte;
	dc->dml.ip.det_buffer_size_kbytes = dc->dcn_ip.det_buffer_size_in_kbyte;
	dc->dml.ip.dpp_output_buffer_pixels = dc->dcn_ip.dpp_output_buffer_pixels;
	dc->dml.ip.opp_output_buffer_lines = dc->dcn_ip.opp_output_buffer_lines;
	dc->dml.ip.pixel_chunk_size_kbytes = dc->dcn_ip.pixel_chunk_size_in_kbyte;
	dc->dml.ip.pte_enable = dc->dcn_ip.pte_enable == dcn_bw_yes;
	dc->dml.ip.pte_chunk_size_kbytes = dc->dcn_ip.pte_chunk_size;
	dc->dml.ip.meta_chunk_size_kbytes = dc->dcn_ip.meta_chunk_size;
	dc->dml.ip.writeback_chunk_size_kbytes = dc->dcn_ip.writeback_chunk_size;
	dc->dml.ip.line_buffer_size_bits = dc->dcn_ip.line_buffer_size;
	dc->dml.ip.max_line_buffer_lines = dc->dcn_ip.max_line_buffer_lines;
	dc->dml.ip.IsLineBufferBppFixed = dc->dcn_ip.is_line_buffer_bpp_fixed == dcn_bw_yes;
	dc->dml.ip.LineBufferFixedBpp = dc->dcn_ip.line_buffer_fixed_bpp;
	dc->dml.ip.writeback_luma_buffer_size_kbytes = dc->dcn_ip.writeback_luma_buffer_size;
	dc->dml.ip.writeback_chroma_buffer_size_kbytes = dc->dcn_ip.writeback_chroma_buffer_size;
	dc->dml.ip.max_num_dpp = dc->dcn_ip.max_num_dpp;
	dc->dml.ip.max_num_wb = dc->dcn_ip.max_num_writeback;
	dc->dml.ip.max_dchub_pscl_bw_pix_per_clk = dc->dcn_ip.max_dchub_topscl_throughput;
	dc->dml.ip.max_pscl_lb_bw_pix_per_clk = dc->dcn_ip.max_pscl_tolb_throughput;
	dc->dml.ip.max_lb_vscl_bw_pix_per_clk = dc->dcn_ip.max_lb_tovscl_throughput;
	dc->dml.ip.max_vscl_hscl_bw_pix_per_clk = dc->dcn_ip.max_vscl_tohscl_throughput;
	dc->dml.ip.max_hscl_ratio = dc->dcn_ip.max_hscl_ratio;
	dc->dml.ip.max_vscl_ratio = dc->dcn_ip.max_vscl_ratio;
	dc->dml.ip.max_hscl_taps = dc->dcn_ip.max_hscl_taps;
	dc->dml.ip.max_vscl_taps = dc->dcn_ip.max_vscl_taps;
	/*pte_buffer_size_in_requests missing in dml*/
	dc->dml.ip.dispclk_ramp_margin_percent = dc->dcn_ip.dispclk_ramping_margin;
	dc->dml.ip.underscan_factor = dc->dcn_ip.under_scan_factor;
	dc->dml.ip.max_inter_dcn_tile_repeaters = dc->dcn_ip.max_inter_dcn_tile_repeaters;
	dc->dml.ip.can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one =
		dc->dcn_ip.can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one == dcn_bw_yes;
	dc->dml.ip.bug_forcing_LC_req_same_size_fixed =
		dc->dcn_ip.bug_forcing_luma_and_chroma_request_to_same_size_fixed == dcn_bw_yes;
	dc->dml.ip.dcfclk_cstate_latency = dc->dcn_ip.dcfclk_cstate_latency;
	kernel_fpu_end();
}
