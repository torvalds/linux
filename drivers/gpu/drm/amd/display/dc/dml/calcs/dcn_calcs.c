/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 * Copyright 2019 Raptor Engineering, LLC
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
#include "dc.h"
#include "dcn_calcs.h"
#include "dcn_calc_auto.h"
#include "dal_asic_id.h"
#include "resource.h"
#include "dcn10/dcn10_resource.h"
#include "dcn10/dcn10_hubbub.h"
#include "dml/dml1_display_rq_dlg_calc.h"

#include "dcn_calc_math.h"

#define DC_LOGGER \
	dc->ctx->logger

#define WM_SET_COUNT 4
#define WM_A 0
#define WM_B 1
#define WM_C 2
#define WM_D 3

/*
 * NOTE:
 *   This file is gcc-parseable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */

/* Defaults from spreadsheet rev#247.
 * RV2 delta: dram_clock_change_latency, max_num_dpp
 */
const struct dcn_soc_bounding_box dcn10_soc_defaults = {
		/* latencies */
		.sr_exit_time = 17, /*us*/
		.sr_enter_plus_exit_time = 19, /*us*/
		.urgent_latency = 4, /*us*/
		.dram_clock_change_latency = 17, /*us*/
		.write_back_latency = 12, /*us*/
		.percent_of_ideal_drambw_received_after_urg_latency = 80, /*%*/

		/* below default clocks derived from STA target base on
		 * slow-slow corner + 10% margin with voltages aligned to FCLK.
		 *
		 * Use these value if fused value doesn't make sense as earlier
		 * part don't have correct value fused */
		/* default DCF CLK DPM on RV*/
		.dcfclkv_max0p9 = 655,	/* MHz, = 3600/5.5 */
		.dcfclkv_nom0p8 = 626,	/* MHz, = 3600/5.75 */
		.dcfclkv_mid0p72 = 600,	/* MHz, = 3600/6, bypass */
		.dcfclkv_min0p65 = 300,	/* MHz, = 3600/12, bypass */

		/* default DISP CLK voltage state on RV */
		.max_dispclk_vmax0p9 = 1108,	/* MHz, = 3600/3.25 */
		.max_dispclk_vnom0p8 = 1029,	/* MHz, = 3600/3.5 */
		.max_dispclk_vmid0p72 = 960,	/* MHz, = 3600/3.75 */
		.max_dispclk_vmin0p65 = 626,	/* MHz, = 3600/5.75 */

		/* default DPP CLK voltage state on RV */
		.max_dppclk_vmax0p9 = 720,	/* MHz, = 3600/5 */
		.max_dppclk_vnom0p8 = 686,	/* MHz, = 3600/5.25 */
		.max_dppclk_vmid0p72 = 626,	/* MHz, = 3600/5.75 */
		.max_dppclk_vmin0p65 = 400,	/* MHz, = 3600/9 */

		/* default PHY CLK voltage state on RV */
		.phyclkv_max0p9 = 900, /*MHz*/
		.phyclkv_nom0p8 = 847, /*MHz*/
		.phyclkv_mid0p72 = 800, /*MHz*/
		.phyclkv_min0p65 = 600, /*MHz*/

		/* BW depend on FCLK, MCLK, # of channels */
		/* dual channel BW */
		.fabric_and_dram_bandwidth_vmax0p9 = 38.4f, /*GB/s*/
		.fabric_and_dram_bandwidth_vnom0p8 = 34.133f, /*GB/s*/
		.fabric_and_dram_bandwidth_vmid0p72 = 29.866f, /*GB/s*/
		.fabric_and_dram_bandwidth_vmin0p65 = 12.8f, /*GB/s*/
		/* single channel BW
		.fabric_and_dram_bandwidth_vmax0p9 = 19.2f,
		.fabric_and_dram_bandwidth_vnom0p8 = 17.066f,
		.fabric_and_dram_bandwidth_vmid0p72 = 14.933f,
		.fabric_and_dram_bandwidth_vmin0p65 = 12.8f,
		*/

		.number_of_channels = 2,

		.socclk = 208, /*MHz*/
		.downspreading = 0.5f, /*%*/
		.round_trip_ping_latency_cycles = 128, /*DCFCLK Cycles*/
		.urgent_out_of_order_return_per_channel = 256, /*bytes*/
		.vmm_page_size = 4096, /*bytes*/
		.return_bus_width = 64, /*bytes*/
		.max_request_size = 256, /*bytes*/

		/* Depends on user class (client vs embedded, workstation, etc) */
		.percent_disp_bw_limit = 0.3f /*%*/
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
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
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

enum source_macro_tile_size swizzle_mode_to_macro_tile_size(enum swizzle_mode_values sw_mode)
{
	switch (sw_mode) {
	/* for 4/8/16 high tiles */
	case DC_SW_LINEAR:
		return dm_4k_tile;
	case DC_SW_4KB_S:
	case DC_SW_4KB_S_X:
		return dm_4k_tile;
	case DC_SW_64KB_S:
	case DC_SW_64KB_S_X:
	case DC_SW_64KB_S_T:
		return dm_64k_tile;
	case DC_SW_VAR_S:
	case DC_SW_VAR_S_X:
		return dm_256k_tile;

	/* For 64bpp 2 high tiles */
	case DC_SW_4KB_D:
	case DC_SW_4KB_D_X:
		return dm_4k_tile;
	case DC_SW_64KB_D:
	case DC_SW_64KB_D_X:
	case DC_SW_64KB_D_T:
		return dm_64k_tile;
	case DC_SW_VAR_D:
	case DC_SW_VAR_D_X:
		return dm_256k_tile;

	case DC_SW_4KB_R:
	case DC_SW_4KB_R_X:
		return dm_4k_tile;
	case DC_SW_64KB_R:
	case DC_SW_64KB_R_X:
		return dm_64k_tile;
	case DC_SW_VAR_R:
	case DC_SW_VAR_R_X:
		return dm_256k_tile;

	/* Unsupported swizzle modes for dcn */
	case DC_SW_256B_S:
	default:
		ASSERT(0); /* Not supported */
		return 0;
	}
}

static void pipe_ctx_to_e2e_pipe_params (
		const struct pipe_ctx *pipe,
		struct _vcs_dpi_display_pipe_params_st *input)
{
	input->src.is_hsplit = false;

	/* stereo can never be split */
	if (pipe->plane_state->stereo_format == PLANE_STEREO_FORMAT_SIDE_BY_SIDE ||
	    pipe->plane_state->stereo_format == PLANE_STEREO_FORMAT_TOP_AND_BOTTOM) {
		/* reset the split group if it was already considered split. */
		input->src.hsplit_grp = pipe->pipe_idx;
	} else if (pipe->top_pipe != NULL && pipe->top_pipe->plane_state == pipe->plane_state) {
		input->src.is_hsplit = true;
	} else if (pipe->bottom_pipe != NULL && pipe->bottom_pipe->plane_state == pipe->plane_state) {
		input->src.is_hsplit = true;
	}

	if (pipe->plane_res.dpp->ctx->dc->debug.optimized_watermark) {
		/*
		 * this method requires us to always re-calculate watermark when dcc change
		 * between flip.
		 */
		input->src.dcc = pipe->plane_state->dcc.enable ? 1 : 0;
	} else {
		/*
		 * allow us to disable dcc on the fly without re-calculating WM
		 *
		 * extra overhead for DCC is quite small.  for 1080p WM without
		 * DCC is only 0.417us lower (urgent goes from 6.979us to 6.562us)
		 */
		unsigned int bpe;

		input->src.dcc = pipe->plane_res.dpp->ctx->dc->res_pool->hubbub->funcs->
			dcc_support_pixel_format(pipe->plane_state->format, &bpe) ? 1 : 0;
	}
	input->src.dcc_rate            = 1;
	input->src.meta_pitch          = pipe->plane_state->dcc.meta_pitch;
	input->src.source_scan         = dm_horz;
	input->src.sw_mode             = pipe->plane_state->tiling_info.gfx9.swizzle;

	input->src.viewport_width      = pipe->plane_res.scl_data.viewport.width;
	input->src.viewport_height     = pipe->plane_res.scl_data.viewport.height;
	input->src.data_pitch          = pipe->plane_res.scl_data.viewport.width;
	input->src.data_pitch_c        = pipe->plane_res.scl_data.viewport.width;
	input->src.cur0_src_width      = 128; /* TODO: Cursor calcs, not curently stored */
	input->src.cur0_bpp            = 32;

	input->src.macro_tile_size = swizzle_mode_to_macro_tile_size(pipe->plane_state->tiling_info.gfx9.swizzle);

	switch (pipe->plane_state->rotation) {
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
	switch (pipe->plane_state->format) {
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
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		input->src.source_format = dm_444_64;
		input->src.viewport_width_c    = input->src.viewport_width;
		input->src.viewport_height_c   = input->src.viewport_height;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		input->src.source_format = dm_rgbe_alpha;
		input->src.viewport_width_c    = input->src.viewport_width;
		input->src.viewport_height_c   = input->src.viewport_height;
		break;
	default:
		input->src.source_format = dm_444_32;
		input->src.viewport_width_c    = input->src.viewport_width;
		input->src.viewport_height_c   = input->src.viewport_height;
		break;
	}

	input->scale_taps.htaps                = pipe->plane_res.scl_data.taps.h_taps;
	input->scale_ratio_depth.hscl_ratio    = pipe->plane_res.scl_data.ratios.horz.value/4294967296.0;
	input->scale_ratio_depth.vscl_ratio    = pipe->plane_res.scl_data.ratios.vert.value/4294967296.0;
	input->scale_ratio_depth.vinit =  pipe->plane_res.scl_data.inits.v.value/4294967296.0;
	if (input->scale_ratio_depth.vinit < 1.0)
			input->scale_ratio_depth.vinit = 1;
	input->scale_taps.vtaps = pipe->plane_res.scl_data.taps.v_taps;
	input->scale_taps.vtaps_c = pipe->plane_res.scl_data.taps.v_taps_c;
	input->scale_taps.htaps_c              = pipe->plane_res.scl_data.taps.h_taps_c;
	input->scale_ratio_depth.hscl_ratio_c  = pipe->plane_res.scl_data.ratios.horz_c.value/4294967296.0;
	input->scale_ratio_depth.vscl_ratio_c  = pipe->plane_res.scl_data.ratios.vert_c.value/4294967296.0;
	input->scale_ratio_depth.vinit_c       = pipe->plane_res.scl_data.inits.v_c.value/4294967296.0;
	if (input->scale_ratio_depth.vinit_c < 1.0)
			input->scale_ratio_depth.vinit_c = 1;
	switch (pipe->plane_res.scl_data.lb_params.depth) {
	case LB_PIXEL_DEPTH_30BPP:
		input->scale_ratio_depth.lb_depth = 30; break;
	case LB_PIXEL_DEPTH_36BPP:
		input->scale_ratio_depth.lb_depth = 36; break;
	default:
		input->scale_ratio_depth.lb_depth = 24; break;
	}


	input->dest.vactive        = pipe->stream->timing.v_addressable + pipe->stream->timing.v_border_top
			+ pipe->stream->timing.v_border_bottom;

	input->dest.recout_width   = pipe->plane_res.scl_data.recout.width;
	input->dest.recout_height  = pipe->plane_res.scl_data.recout.height;

	input->dest.full_recout_width   = pipe->plane_res.scl_data.recout.width;
	input->dest.full_recout_height  = pipe->plane_res.scl_data.recout.height;

	input->dest.htotal         = pipe->stream->timing.h_total;
	input->dest.hblank_start   = input->dest.htotal - pipe->stream->timing.h_front_porch;
	input->dest.hblank_end     = input->dest.hblank_start
			- pipe->stream->timing.h_addressable
			- pipe->stream->timing.h_border_left
			- pipe->stream->timing.h_border_right;

	input->dest.vtotal         = pipe->stream->timing.v_total;
	input->dest.vblank_start   = input->dest.vtotal - pipe->stream->timing.v_front_porch;
	input->dest.vblank_end     = input->dest.vblank_start
			- pipe->stream->timing.v_addressable
			- pipe->stream->timing.v_border_bottom
			- pipe->stream->timing.v_border_top;
	input->dest.pixel_rate_mhz = pipe->stream->timing.pix_clk_100hz/10000.0;
	input->dest.vstartup_start = pipe->pipe_dlg_param.vstartup_start;
	input->dest.vupdate_offset = pipe->pipe_dlg_param.vupdate_offset;
	input->dest.vupdate_offset = pipe->pipe_dlg_param.vupdate_offset;
	input->dest.vupdate_width = pipe->pipe_dlg_param.vupdate_width;

}

static void dcn_bw_calc_rq_dlg_ttu(
		const struct dc *dc,
		const struct dcn_bw_internal_vars *v,
		struct pipe_ctx *pipe,
		int in_idx)
{
	struct display_mode_lib *dml = (struct display_mode_lib *)(&dc->dml);
	struct _vcs_dpi_display_dlg_regs_st *dlg_regs = &pipe->dlg_regs;
	struct _vcs_dpi_display_ttu_regs_st *ttu_regs = &pipe->ttu_regs;
	struct _vcs_dpi_display_rq_regs_st *rq_regs = &pipe->rq_regs;
	struct _vcs_dpi_display_rq_params_st *rq_param = &pipe->dml_rq_param;
	struct _vcs_dpi_display_dlg_sys_params_st *dlg_sys_param = &pipe->dml_dlg_sys_param;
	struct _vcs_dpi_display_e2e_pipe_params_st *input = &pipe->dml_input;
	float total_active_bw = 0;
	float total_prefetch_bw = 0;
	int total_flip_bytes = 0;
	int i;

	memset(dlg_regs, 0, sizeof(*dlg_regs));
	memset(ttu_regs, 0, sizeof(*ttu_regs));
	memset(rq_regs, 0, sizeof(*rq_regs));
	memset(rq_param, 0, sizeof(*rq_param));
	memset(dlg_sys_param, 0, sizeof(*dlg_sys_param));
	memset(input, 0, sizeof(*input));

	for (i = 0; i < number_of_planes; i++) {
		total_active_bw += v->read_bandwidth[i];
		total_prefetch_bw += v->prefetch_bandwidth[i];
		total_flip_bytes += v->total_immediate_flip_bytes[i];
	}
	dlg_sys_param->total_flip_bw = v->return_bw - dcn_bw_max2(total_active_bw, total_prefetch_bw);
	if (dlg_sys_param->total_flip_bw < 0.0)
		dlg_sys_param->total_flip_bw = 0;

	dlg_sys_param->t_mclk_wm_us = v->dram_clock_change_watermark;
	dlg_sys_param->t_sr_wm_us = v->stutter_enter_plus_exit_watermark;
	dlg_sys_param->t_urg_wm_us = v->urgent_watermark;
	dlg_sys_param->t_extra_us = v->urgent_extra_latency;
	dlg_sys_param->deepsleep_dcfclk_mhz = v->dcf_clk_deep_sleep;
	dlg_sys_param->total_flip_bytes = total_flip_bytes;

	pipe_ctx_to_e2e_pipe_params(pipe, &input->pipe);
	input->clks_cfg.dcfclk_mhz = v->dcfclk;
	input->clks_cfg.dispclk_mhz = v->dispclk;
	input->clks_cfg.dppclk_mhz = v->dppclk;
	input->clks_cfg.refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000.0;
	input->clks_cfg.socclk_mhz = v->socclk;
	input->clks_cfg.voltage = v->voltage_level;
//	dc->dml.logger = pool->base.logger;
	input->dout.output_format = (v->output_format[in_idx] == dcn_bw_420) ? dm_420 : dm_444;
	input->dout.output_type  = (v->output[in_idx] == dcn_bw_hdmi) ? dm_hdmi : dm_dp;
	//input[in_idx].dout.output_standard;

	/*todo: soc->sr_enter_plus_exit_time??*/

	dml1_rq_dlg_get_rq_params(dml, rq_param, &input->pipe.src);
	dml1_extract_rq_regs(dml, rq_regs, rq_param);
	dml1_rq_dlg_get_dlg_params(
			dml,
			dlg_regs,
			ttu_regs,
			&rq_param->dlg,
			dlg_sys_param,
			input,
			true,
			true,
			v->pte_enable == dcn_bw_yes,
			pipe->plane_state->flip_immediate);
}

static void split_stream_across_pipes(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct pipe_ctx *primary_pipe,
		struct pipe_ctx *secondary_pipe)
{
	int pipe_idx = secondary_pipe->pipe_idx;

	if (!primary_pipe->plane_state)
		return;

	*secondary_pipe = *primary_pipe;

	secondary_pipe->pipe_idx = pipe_idx;
	secondary_pipe->plane_res.mi = pool->mis[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.hubp = pool->hubps[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.ipp = pool->ipps[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.xfm = pool->transforms[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.dpp = pool->dpps[secondary_pipe->pipe_idx];
	secondary_pipe->plane_res.mpcc_inst = pool->dpps[secondary_pipe->pipe_idx]->inst;
	if (primary_pipe->bottom_pipe) {
		ASSERT(primary_pipe->bottom_pipe != secondary_pipe);
		secondary_pipe->bottom_pipe = primary_pipe->bottom_pipe;
		secondary_pipe->bottom_pipe->top_pipe = secondary_pipe;
	}
	primary_pipe->bottom_pipe = secondary_pipe;
	secondary_pipe->top_pipe = primary_pipe;

	resource_build_scaling_params(primary_pipe);
	resource_build_scaling_params(secondary_pipe);
}

#if 0
static void calc_wm_sets_and_perf_params(
		struct dc_state *context,
		struct dcn_bw_internal_vars *v)
{
	/* Calculate set A last to keep internal var state consistent for required config */
	if (v->voltage_level < 2) {
		v->fabric_and_dram_bandwidth_per_state[1] = v->fabric_and_dram_bandwidth_vnom0p8;
		v->fabric_and_dram_bandwidth_per_state[0] = v->fabric_and_dram_bandwidth_vnom0p8;
		v->fabric_and_dram_bandwidth = v->fabric_and_dram_bandwidth_vnom0p8;
		dispclkdppclkdcfclk_deep_sleep_prefetch_parameters_watermarks_and_performance_calculation(v);

		context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns =
			v->stutter_exit_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns =
				v->stutter_enter_plus_exit_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns =
				v->dram_clock_change_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.b.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.b.urgent_ns = v->urgent_watermark * 1000;

		v->dcfclk_per_state[1] = v->dcfclkv_nom0p8;
		v->dcfclk_per_state[0] = v->dcfclkv_nom0p8;
		v->dcfclk = v->dcfclkv_nom0p8;
		dispclkdppclkdcfclk_deep_sleep_prefetch_parameters_watermarks_and_performance_calculation(v);

		context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns =
			v->stutter_exit_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns =
				v->stutter_enter_plus_exit_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns =
				v->dram_clock_change_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.c.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.c.urgent_ns = v->urgent_watermark * 1000;
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

		context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns =
			v->stutter_exit_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns =
				v->stutter_enter_plus_exit_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns =
				v->dram_clock_change_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.d.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.d.urgent_ns = v->urgent_watermark * 1000;
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

	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns =
		v->stutter_exit_watermark * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns =
			v->stutter_enter_plus_exit_watermark * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns =
			v->dram_clock_change_watermark * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
	context->bw_ctx.bw.dcn.watermarks.a.urgent_ns = v->urgent_watermark * 1000;
	if (v->voltage_level >= 2) {
		context->bw_ctx.bw.dcn.watermarks.b = context->bw_ctx.bw.dcn.watermarks.a;
		context->bw_ctx.bw.dcn.watermarks.c = context->bw_ctx.bw.dcn.watermarks.a;
	}
	if (v->voltage_level >= 3)
		context->bw_ctx.bw.dcn.watermarks.d = context->bw_ctx.bw.dcn.watermarks.a;
}
#endif

static bool dcn_bw_apply_registry_override(struct dc *dc)
{
	bool updated = false;

	if ((int)(dc->dcn_soc->sr_exit_time * 1000) != dc->debug.sr_exit_time_ns
			&& dc->debug.sr_exit_time_ns) {
		updated = true;
		dc->dcn_soc->sr_exit_time = dc->debug.sr_exit_time_ns / 1000.0;
	}

	if ((int)(dc->dcn_soc->sr_enter_plus_exit_time * 1000)
				!= dc->debug.sr_enter_plus_exit_time_ns
			&& dc->debug.sr_enter_plus_exit_time_ns) {
		updated = true;
		dc->dcn_soc->sr_enter_plus_exit_time =
				dc->debug.sr_enter_plus_exit_time_ns / 1000.0;
	}

	if ((int)(dc->dcn_soc->urgent_latency * 1000) != dc->debug.urgent_latency_ns
			&& dc->debug.urgent_latency_ns) {
		updated = true;
		dc->dcn_soc->urgent_latency = dc->debug.urgent_latency_ns / 1000.0;
	}

	if ((int)(dc->dcn_soc->percent_of_ideal_drambw_received_after_urg_latency * 1000)
				!= dc->debug.percent_of_ideal_drambw
			&& dc->debug.percent_of_ideal_drambw) {
		updated = true;
		dc->dcn_soc->percent_of_ideal_drambw_received_after_urg_latency =
				dc->debug.percent_of_ideal_drambw;
	}

	if ((int)(dc->dcn_soc->dram_clock_change_latency * 1000)
				!= dc->debug.dram_clock_change_latency_ns
			&& dc->debug.dram_clock_change_latency_ns) {
		updated = true;
		dc->dcn_soc->dram_clock_change_latency =
				dc->debug.dram_clock_change_latency_ns / 1000.0;
	}

	return updated;
}

static void hack_disable_optional_pipe_split(struct dcn_bw_internal_vars *v)
{
	/*
	 * disable optional pipe split by lower dispclk bounding box
	 * at DPM0
	 */
	v->max_dispclk[0] = v->max_dppclk_vmin0p65;
}

static void hack_force_pipe_split(struct dcn_bw_internal_vars *v,
		unsigned int pixel_rate_100hz)
{
	float pixel_rate_mhz = pixel_rate_100hz / 10000;

	/*
	 * force enabling pipe split by lower dpp clock for DPM0 to just
	 * below the specify pixel_rate, so bw calc would split pipe.
	 */
	if (pixel_rate_mhz < v->max_dppclk[0])
		v->max_dppclk[0] = pixel_rate_mhz;
}

static void hack_bounding_box(struct dcn_bw_internal_vars *v,
		struct dc_debug_options *dbg,
		struct dc_state *context)
{
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		/**
		 * Workaround for avoiding pipe-split in cases where we'd split
		 * planes that are too small, resulting in splits that aren't
		 * valid for the scaler.
		 */
		if (pipe->plane_state &&
		    (pipe->plane_state->dst_rect.width <= 16 ||
		     pipe->plane_state->dst_rect.height <= 16 ||
		     pipe->plane_state->src_rect.width <= 16 ||
		     pipe->plane_state->src_rect.height <= 16)) {
			hack_disable_optional_pipe_split(v);
			return;
		}
	}

	if (dbg->pipe_split_policy == MPC_SPLIT_AVOID)
		hack_disable_optional_pipe_split(v);

	if (dbg->pipe_split_policy == MPC_SPLIT_AVOID_MULT_DISP &&
		context->stream_count >= 2)
		hack_disable_optional_pipe_split(v);

	if (context->stream_count == 1 &&
			dbg->force_single_disp_pipe_split)
		hack_force_pipe_split(v, context->streams[0]->timing.pix_clk_100hz);
}

static unsigned int get_highest_allowed_voltage_level(bool is_vmin_only_asic)
{
	/* for low power RV2 variants, the highest voltage level we want is 0 */
	if (is_vmin_only_asic)
		return 0;
	else	/* we are ok with all levels */
		return 4;
}

bool dcn_validate_bandwidth(
		struct dc *dc,
		struct dc_state *context,
		bool fast_validate)
{
	/*
	 * we want a breakdown of the various stages of validation, which the
	 * perf_trace macro doesn't support
	 */
	BW_VAL_TRACE_SETUP();

	const struct resource_pool *pool = dc->res_pool;
	struct dcn_bw_internal_vars *v = &context->dcn_bw_vars;
	int i, input_idx, k;
	int vesa_sync_start, asic_blank_end, asic_blank_start;
	bool bw_limit_pass;
	float bw_limit;

	PERFORMANCE_TRACE_START();

	BW_VAL_TRACE_COUNT();

	if (dcn_bw_apply_registry_override(dc))
		dcn_bw_sync_calcs_and_dml(dc);

	memset(v, 0, sizeof(*v));

	v->sr_exit_time = dc->dcn_soc->sr_exit_time;
	v->sr_enter_plus_exit_time = dc->dcn_soc->sr_enter_plus_exit_time;
	v->urgent_latency = dc->dcn_soc->urgent_latency;
	v->write_back_latency = dc->dcn_soc->write_back_latency;
	v->percent_of_ideal_drambw_received_after_urg_latency =
			dc->dcn_soc->percent_of_ideal_drambw_received_after_urg_latency;

	v->dcfclkv_min0p65 = dc->dcn_soc->dcfclkv_min0p65;
	v->dcfclkv_mid0p72 = dc->dcn_soc->dcfclkv_mid0p72;
	v->dcfclkv_nom0p8 = dc->dcn_soc->dcfclkv_nom0p8;
	v->dcfclkv_max0p9 = dc->dcn_soc->dcfclkv_max0p9;

	v->max_dispclk_vmin0p65 = dc->dcn_soc->max_dispclk_vmin0p65;
	v->max_dispclk_vmid0p72 = dc->dcn_soc->max_dispclk_vmid0p72;
	v->max_dispclk_vnom0p8 = dc->dcn_soc->max_dispclk_vnom0p8;
	v->max_dispclk_vmax0p9 = dc->dcn_soc->max_dispclk_vmax0p9;

	v->max_dppclk_vmin0p65 = dc->dcn_soc->max_dppclk_vmin0p65;
	v->max_dppclk_vmid0p72 = dc->dcn_soc->max_dppclk_vmid0p72;
	v->max_dppclk_vnom0p8 = dc->dcn_soc->max_dppclk_vnom0p8;
	v->max_dppclk_vmax0p9 = dc->dcn_soc->max_dppclk_vmax0p9;

	v->socclk = dc->dcn_soc->socclk;

	v->fabric_and_dram_bandwidth_vmin0p65 = dc->dcn_soc->fabric_and_dram_bandwidth_vmin0p65;
	v->fabric_and_dram_bandwidth_vmid0p72 = dc->dcn_soc->fabric_and_dram_bandwidth_vmid0p72;
	v->fabric_and_dram_bandwidth_vnom0p8 = dc->dcn_soc->fabric_and_dram_bandwidth_vnom0p8;
	v->fabric_and_dram_bandwidth_vmax0p9 = dc->dcn_soc->fabric_and_dram_bandwidth_vmax0p9;

	v->phyclkv_min0p65 = dc->dcn_soc->phyclkv_min0p65;
	v->phyclkv_mid0p72 = dc->dcn_soc->phyclkv_mid0p72;
	v->phyclkv_nom0p8 = dc->dcn_soc->phyclkv_nom0p8;
	v->phyclkv_max0p9 = dc->dcn_soc->phyclkv_max0p9;

	v->downspreading = dc->dcn_soc->downspreading;
	v->round_trip_ping_latency_cycles = dc->dcn_soc->round_trip_ping_latency_cycles;
	v->urgent_out_of_order_return_per_channel = dc->dcn_soc->urgent_out_of_order_return_per_channel;
	v->number_of_channels = dc->dcn_soc->number_of_channels;
	v->vmm_page_size = dc->dcn_soc->vmm_page_size;
	v->dram_clock_change_latency = dc->dcn_soc->dram_clock_change_latency;
	v->return_bus_width = dc->dcn_soc->return_bus_width;

	v->rob_buffer_size_in_kbyte = dc->dcn_ip->rob_buffer_size_in_kbyte;
	v->det_buffer_size_in_kbyte = dc->dcn_ip->det_buffer_size_in_kbyte;
	v->dpp_output_buffer_pixels = dc->dcn_ip->dpp_output_buffer_pixels;
	v->opp_output_buffer_lines = dc->dcn_ip->opp_output_buffer_lines;
	v->pixel_chunk_size_in_kbyte = dc->dcn_ip->pixel_chunk_size_in_kbyte;
	v->pte_enable = dc->dcn_ip->pte_enable;
	v->pte_chunk_size = dc->dcn_ip->pte_chunk_size;
	v->meta_chunk_size = dc->dcn_ip->meta_chunk_size;
	v->writeback_chunk_size = dc->dcn_ip->writeback_chunk_size;
	v->odm_capability = dc->dcn_ip->odm_capability;
	v->dsc_capability = dc->dcn_ip->dsc_capability;
	v->line_buffer_size = dc->dcn_ip->line_buffer_size;
	v->is_line_buffer_bpp_fixed = dc->dcn_ip->is_line_buffer_bpp_fixed;
	v->line_buffer_fixed_bpp = dc->dcn_ip->line_buffer_fixed_bpp;
	v->max_line_buffer_lines = dc->dcn_ip->max_line_buffer_lines;
	v->writeback_luma_buffer_size = dc->dcn_ip->writeback_luma_buffer_size;
	v->writeback_chroma_buffer_size = dc->dcn_ip->writeback_chroma_buffer_size;
	v->max_num_dpp = dc->dcn_ip->max_num_dpp;
	v->max_num_writeback = dc->dcn_ip->max_num_writeback;
	v->max_dchub_topscl_throughput = dc->dcn_ip->max_dchub_topscl_throughput;
	v->max_pscl_tolb_throughput = dc->dcn_ip->max_pscl_tolb_throughput;
	v->max_lb_tovscl_throughput = dc->dcn_ip->max_lb_tovscl_throughput;
	v->max_vscl_tohscl_throughput = dc->dcn_ip->max_vscl_tohscl_throughput;
	v->max_hscl_ratio = dc->dcn_ip->max_hscl_ratio;
	v->max_vscl_ratio = dc->dcn_ip->max_vscl_ratio;
	v->max_hscl_taps = dc->dcn_ip->max_hscl_taps;
	v->max_vscl_taps = dc->dcn_ip->max_vscl_taps;
	v->under_scan_factor = dc->dcn_ip->under_scan_factor;
	v->pte_buffer_size_in_requests = dc->dcn_ip->pte_buffer_size_in_requests;
	v->dispclk_ramping_margin = dc->dcn_ip->dispclk_ramping_margin;
	v->max_inter_dcn_tile_repeaters = dc->dcn_ip->max_inter_dcn_tile_repeaters;
	v->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one =
			dc->dcn_ip->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one;
	v->bug_forcing_luma_and_chroma_request_to_same_size_fixed =
			dc->dcn_ip->bug_forcing_luma_and_chroma_request_to_same_size_fixed;

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
	v->synchronized_vblank = dcn_bw_no;
	v->ta_pscalculation = dcn_bw_override;
	v->allow_different_hratio_vratio = dcn_bw_yes;

	for (i = 0, input_idx = 0; i < pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;
		/* skip all but first of split pipes */
		if (pipe->top_pipe && pipe->top_pipe->plane_state == pipe->plane_state)
			continue;

		v->underscan_output[input_idx] = false; /* taken care of in recout already*/
		v->interlace_output[input_idx] = false;

		v->htotal[input_idx] = pipe->stream->timing.h_total;
		v->vtotal[input_idx] = pipe->stream->timing.v_total;
		v->vactive[input_idx] = pipe->stream->timing.v_addressable +
				pipe->stream->timing.v_border_top + pipe->stream->timing.v_border_bottom;
		v->v_sync_plus_back_porch[input_idx] = pipe->stream->timing.v_total
				- v->vactive[input_idx]
				- pipe->stream->timing.v_front_porch;
		v->pixel_clock[input_idx] = pipe->stream->timing.pix_clk_100hz/10000.0;
		if (pipe->stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
			v->pixel_clock[input_idx] *= 2;
		if (!pipe->plane_state) {
			v->dcc_enable[input_idx] = dcn_bw_yes;
			v->source_pixel_format[input_idx] = dcn_bw_rgb_sub_32;
			v->source_surface_mode[input_idx] = dcn_bw_sw_4_kb_s;
			v->lb_bit_per_pixel[input_idx] = 30;
			v->viewport_width[input_idx] = pipe->stream->timing.h_addressable;
			v->viewport_height[input_idx] = pipe->stream->timing.v_addressable;
			/*
			 * for cases where we have no plane, we want to validate up to 1080p
			 * source size because here we are only interested in if the output
			 * timing is supported or not. if we cannot support native resolution
			 * of the high res display, we still want to support lower res up scale
			 * to native
			 */
			if (v->viewport_width[input_idx] > 1920)
				v->viewport_width[input_idx] = 1920;
			if (v->viewport_height[input_idx] > 1080)
				v->viewport_height[input_idx] = 1080;
			v->scaler_rec_out_width[input_idx] = v->viewport_width[input_idx];
			v->scaler_recout_height[input_idx] = v->viewport_height[input_idx];
			v->override_hta_ps[input_idx] = 1;
			v->override_vta_ps[input_idx] = 1;
			v->override_hta_pschroma[input_idx] = 1;
			v->override_vta_pschroma[input_idx] = 1;
			v->source_scan[input_idx] = dcn_bw_hor;

		} else {
			v->viewport_height[input_idx] =  pipe->plane_res.scl_data.viewport.height;
			v->viewport_width[input_idx] = pipe->plane_res.scl_data.viewport.width;
			v->scaler_rec_out_width[input_idx] = pipe->plane_res.scl_data.recout.width;
			v->scaler_recout_height[input_idx] = pipe->plane_res.scl_data.recout.height;
			if (pipe->bottom_pipe && pipe->bottom_pipe->plane_state == pipe->plane_state) {
				if (pipe->plane_state->rotation % 2 == 0) {
					int viewport_end = pipe->plane_res.scl_data.viewport.width
							+ pipe->plane_res.scl_data.viewport.x;
					int viewport_b_end = pipe->bottom_pipe->plane_res.scl_data.viewport.width
							+ pipe->bottom_pipe->plane_res.scl_data.viewport.x;

					if (viewport_end > viewport_b_end)
						v->viewport_width[input_idx] = viewport_end
							- pipe->bottom_pipe->plane_res.scl_data.viewport.x;
					else
						v->viewport_width[input_idx] = viewport_b_end
									- pipe->plane_res.scl_data.viewport.x;
				} else  {
					int viewport_end = pipe->plane_res.scl_data.viewport.height
						+ pipe->plane_res.scl_data.viewport.y;
					int viewport_b_end = pipe->bottom_pipe->plane_res.scl_data.viewport.height
						+ pipe->bottom_pipe->plane_res.scl_data.viewport.y;

					if (viewport_end > viewport_b_end)
						v->viewport_height[input_idx] = viewport_end
							- pipe->bottom_pipe->plane_res.scl_data.viewport.y;
					else
						v->viewport_height[input_idx] = viewport_b_end
									- pipe->plane_res.scl_data.viewport.y;
				}
				v->scaler_rec_out_width[input_idx] = pipe->plane_res.scl_data.recout.width
						+ pipe->bottom_pipe->plane_res.scl_data.recout.width;
			}

			if (pipe->plane_state->rotation % 2 == 0) {
				ASSERT(pipe->plane_res.scl_data.ratios.horz.value != dc_fixpt_one.value
					|| v->scaler_rec_out_width[input_idx] == v->viewport_width[input_idx]);
				ASSERT(pipe->plane_res.scl_data.ratios.vert.value != dc_fixpt_one.value
					|| v->scaler_recout_height[input_idx] == v->viewport_height[input_idx]);
			} else {
				ASSERT(pipe->plane_res.scl_data.ratios.horz.value != dc_fixpt_one.value
					|| v->scaler_recout_height[input_idx] == v->viewport_width[input_idx]);
				ASSERT(pipe->plane_res.scl_data.ratios.vert.value != dc_fixpt_one.value
					|| v->scaler_rec_out_width[input_idx] == v->viewport_height[input_idx]);
			}

			if (dc->debug.optimized_watermark) {
				/*
				 * this method requires us to always re-calculate watermark when dcc change
				 * between flip.
				 */
				v->dcc_enable[input_idx] = pipe->plane_state->dcc.enable ? dcn_bw_yes : dcn_bw_no;
			} else {
				/*
				 * allow us to disable dcc on the fly without re-calculating WM
				 *
				 * extra overhead for DCC is quite small.  for 1080p WM without
				 * DCC is only 0.417us lower (urgent goes from 6.979us to 6.562us)
				 */
				unsigned int bpe;

				v->dcc_enable[input_idx] = dc->res_pool->hubbub->funcs->dcc_support_pixel_format(
						pipe->plane_state->format, &bpe) ? dcn_bw_yes : dcn_bw_no;
			}

			v->source_pixel_format[input_idx] = tl_pixel_format_to_bw_defs(
					pipe->plane_state->format);
			v->source_surface_mode[input_idx] = tl_sw_mode_to_bw_defs(
					pipe->plane_state->tiling_info.gfx9.swizzle);
			v->lb_bit_per_pixel[input_idx] = tl_lb_bpp_to_int(pipe->plane_res.scl_data.lb_params.depth);
			v->override_hta_ps[input_idx] = pipe->plane_res.scl_data.taps.h_taps;
			v->override_vta_ps[input_idx] = pipe->plane_res.scl_data.taps.v_taps;
			v->override_hta_pschroma[input_idx] = pipe->plane_res.scl_data.taps.h_taps_c;
			v->override_vta_pschroma[input_idx] = pipe->plane_res.scl_data.taps.v_taps_c;
			/*
			 * Spreadsheet doesn't handle taps_c is one properly,
			 * need to force Chroma to always be scaled to pass
			 * bandwidth validation.
			 */
			if (v->override_hta_pschroma[input_idx] == 1)
				v->override_hta_pschroma[input_idx] = 2;
			if (v->override_vta_pschroma[input_idx] == 1)
				v->override_vta_pschroma[input_idx] = 2;
			v->source_scan[input_idx] = (pipe->plane_state->rotation % 2) ? dcn_bw_vert : dcn_bw_hor;
		}
		if (v->is_line_buffer_bpp_fixed == dcn_bw_yes)
			v->lb_bit_per_pixel[input_idx] = v->line_buffer_fixed_bpp;
		v->dcc_rate[input_idx] = 1; /*TODO: Worst case? does this change?*/
		v->output_format[input_idx] = pipe->stream->timing.pixel_encoding ==
				PIXEL_ENCODING_YCBCR420 ? dcn_bw_420 : dcn_bw_444;
		v->output[input_idx] = pipe->stream->signal ==
				SIGNAL_TYPE_HDMI_TYPE_A ? dcn_bw_hdmi : dcn_bw_dp;
		v->output_deep_color[input_idx] = dcn_bw_encoder_8bpc;
		if (v->output[input_idx] == dcn_bw_hdmi) {
			switch (pipe->stream->timing.display_color_depth) {
			case COLOR_DEPTH_101010:
				v->output_deep_color[input_idx] = dcn_bw_encoder_10bpc;
				break;
			case COLOR_DEPTH_121212:
				v->output_deep_color[input_idx]  = dcn_bw_encoder_12bpc;
				break;
			case COLOR_DEPTH_161616:
				v->output_deep_color[input_idx]  = dcn_bw_encoder_16bpc;
				break;
			default:
				break;
			}
		}

		input_idx++;
	}
	v->number_of_active_planes = input_idx;

	scaler_settings_calculation(v);

	hack_bounding_box(v, &dc->debug, context);

	mode_support_and_system_configuration(v);

	/* Unhack dppclk: dont bother with trying to pipe split if we cannot maintain dpm0 */
	if (v->voltage_level != 0
			&& context->stream_count == 1
			&& dc->debug.force_single_disp_pipe_split) {
		v->max_dppclk[0] = v->max_dppclk_vmin0p65;
		mode_support_and_system_configuration(v);
	}

	if (v->voltage_level == 0 &&
			(dc->debug.sr_exit_time_dpm0_ns
				|| dc->debug.sr_enter_plus_exit_time_dpm0_ns)) {

		if (dc->debug.sr_enter_plus_exit_time_dpm0_ns)
			v->sr_enter_plus_exit_time =
				dc->debug.sr_enter_plus_exit_time_dpm0_ns / 1000.0f;
		if (dc->debug.sr_exit_time_dpm0_ns)
			v->sr_exit_time =  dc->debug.sr_exit_time_dpm0_ns / 1000.0f;
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us = v->sr_enter_plus_exit_time;
		context->bw_ctx.dml.soc.sr_exit_time_us = v->sr_exit_time;
		mode_support_and_system_configuration(v);
	}

	display_pipe_configuration(v);

	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->source_scan[k] == dcn_bw_hor)
			v->swath_width_y[k] = v->viewport_width[k] / v->dpp_per_plane[k];
		else
			v->swath_width_y[k] = v->viewport_height[k] / v->dpp_per_plane[k];
	}
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		if (v->source_pixel_format[k] == dcn_bw_rgb_sub_64) {
			v->byte_per_pixel_dety[k] = 8.0;
			v->byte_per_pixel_detc[k] = 0.0;
		} else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_32) {
			v->byte_per_pixel_dety[k] = 4.0;
			v->byte_per_pixel_detc[k] = 0.0;
		} else if (v->source_pixel_format[k] == dcn_bw_rgb_sub_16) {
			v->byte_per_pixel_dety[k] = 2.0;
			v->byte_per_pixel_detc[k] = 0.0;
		} else if (v->source_pixel_format[k] == dcn_bw_yuv420_sub_8) {
			v->byte_per_pixel_dety[k] = 1.0;
			v->byte_per_pixel_detc[k] = 2.0;
		} else {
			v->byte_per_pixel_dety[k] = 4.0f / 3.0f;
			v->byte_per_pixel_detc[k] = 8.0f / 3.0f;
		}
	}

	v->total_data_read_bandwidth = 0.0;
	for (k = 0; k <= v->number_of_active_planes - 1; k++) {
		v->read_bandwidth_plane_luma[k] = v->swath_width_y[k] * v->dpp_per_plane[k] *
				dcn_bw_ceil2(v->byte_per_pixel_dety[k], 1.0) / (v->htotal[k] / v->pixel_clock[k]) * v->v_ratio[k];
		v->read_bandwidth_plane_chroma[k] = v->swath_width_y[k] / 2.0 * v->dpp_per_plane[k] *
				dcn_bw_ceil2(v->byte_per_pixel_detc[k], 2.0) / (v->htotal[k] / v->pixel_clock[k]) * v->v_ratio[k] / 2.0;
		v->total_data_read_bandwidth = v->total_data_read_bandwidth +
				v->read_bandwidth_plane_luma[k] + v->read_bandwidth_plane_chroma[k];
	}

	BW_VAL_TRACE_END_VOLTAGE_LEVEL();

	if (v->voltage_level != number_of_states_plus_one && !fast_validate) {
		float bw_consumed = v->total_bandwidth_consumed_gbyte_per_second;

		if (bw_consumed < v->fabric_and_dram_bandwidth_vmin0p65)
			bw_consumed = v->fabric_and_dram_bandwidth_vmin0p65;
		else if (bw_consumed < v->fabric_and_dram_bandwidth_vmid0p72)
			bw_consumed = v->fabric_and_dram_bandwidth_vmid0p72;
		else if (bw_consumed < v->fabric_and_dram_bandwidth_vnom0p8)
			bw_consumed = v->fabric_and_dram_bandwidth_vnom0p8;
		else
			bw_consumed = v->fabric_and_dram_bandwidth_vmax0p9;

		if (bw_consumed < v->fabric_and_dram_bandwidth)
			if (dc->debug.voltage_align_fclk)
				bw_consumed = v->fabric_and_dram_bandwidth;

		display_pipe_configuration(v);
		/*calc_wm_sets_and_perf_params(context, v);*/
		/* Only 1 set is used by dcn since no noticeable
		 * performance improvement was measured and due to hw bug DEGVIDCN10-254
		 */
		dispclkdppclkdcfclk_deep_sleep_prefetch_parameters_watermarks_and_performance_calculation(v);

		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns =
			v->stutter_exit_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns =
				v->stutter_enter_plus_exit_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns =
				v->dram_clock_change_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.pte_meta_urgent_ns = v->ptemeta_urgent_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.a.urgent_ns = v->urgent_watermark * 1000;
		context->bw_ctx.bw.dcn.watermarks.b = context->bw_ctx.bw.dcn.watermarks.a;
		context->bw_ctx.bw.dcn.watermarks.c = context->bw_ctx.bw.dcn.watermarks.a;
		context->bw_ctx.bw.dcn.watermarks.d = context->bw_ctx.bw.dcn.watermarks.a;

		context->bw_ctx.bw.dcn.clk.fclk_khz = (int)(bw_consumed * 1000000 /
				(ddr4_dram_factor_single_Channel * v->number_of_channels));
		if (bw_consumed == v->fabric_and_dram_bandwidth_vmin0p65)
			context->bw_ctx.bw.dcn.clk.fclk_khz = (int)(bw_consumed * 1000000 / 32);

		context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz = (int)(v->dcf_clk_deep_sleep * 1000);
		context->bw_ctx.bw.dcn.clk.dcfclk_khz = (int)(v->dcfclk * 1000);

		context->bw_ctx.bw.dcn.clk.dispclk_khz = (int)(v->dispclk * 1000);
		if (dc->debug.max_disp_clk == true)
			context->bw_ctx.bw.dcn.clk.dispclk_khz = (int)(dc->dcn_soc->max_dispclk_vmax0p9 * 1000);

		if (context->bw_ctx.bw.dcn.clk.dispclk_khz <
				dc->debug.min_disp_clk_khz) {
			context->bw_ctx.bw.dcn.clk.dispclk_khz =
					dc->debug.min_disp_clk_khz;
		}

		context->bw_ctx.bw.dcn.clk.dppclk_khz = context->bw_ctx.bw.dcn.clk.dispclk_khz /
				v->dispclk_dppclk_ratio;
		context->bw_ctx.bw.dcn.clk.phyclk_khz = v->phyclk_per_state[v->voltage_level];
		switch (v->voltage_level) {
		case 0:
			context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz =
					(int)(dc->dcn_soc->max_dppclk_vmin0p65 * 1000);
			break;
		case 1:
			context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz =
					(int)(dc->dcn_soc->max_dppclk_vmid0p72 * 1000);
			break;
		case 2:
			context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz =
					(int)(dc->dcn_soc->max_dppclk_vnom0p8 * 1000);
			break;
		default:
			context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz =
					(int)(dc->dcn_soc->max_dppclk_vmax0p9 * 1000);
			break;
		}

		BW_VAL_TRACE_END_WATERMARKS();

		for (i = 0, input_idx = 0; i < pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

			/* skip inactive pipe */
			if (!pipe->stream)
				continue;
			/* skip all but first of split pipes */
			if (pipe->top_pipe && pipe->top_pipe->plane_state == pipe->plane_state)
				continue;

			pipe->pipe_dlg_param.vupdate_width = v->v_update_width_pix[input_idx];
			pipe->pipe_dlg_param.vupdate_offset = v->v_update_offset_pix[input_idx];
			pipe->pipe_dlg_param.vready_offset = v->v_ready_offset_pix[input_idx];
			pipe->pipe_dlg_param.vstartup_start = v->v_startup[input_idx];

			pipe->pipe_dlg_param.htotal = pipe->stream->timing.h_total;
			pipe->pipe_dlg_param.vtotal = pipe->stream->timing.v_total;
			vesa_sync_start = pipe->stream->timing.v_addressable +
						pipe->stream->timing.v_border_bottom +
						pipe->stream->timing.v_front_porch;

			asic_blank_end = (pipe->stream->timing.v_total -
						vesa_sync_start -
						pipe->stream->timing.v_border_top)
			* (pipe->stream->timing.flags.INTERLACE ? 1 : 0);

			asic_blank_start = asic_blank_end +
						(pipe->stream->timing.v_border_top +
						pipe->stream->timing.v_addressable +
						pipe->stream->timing.v_border_bottom)
			* (pipe->stream->timing.flags.INTERLACE ? 1 : 0);

			pipe->pipe_dlg_param.vblank_start = asic_blank_start;
			pipe->pipe_dlg_param.vblank_end = asic_blank_end;

			if (pipe->plane_state) {
				struct pipe_ctx *hsplit_pipe = pipe->bottom_pipe;

				pipe->plane_state->update_flags.bits.full_update = 1;

				if (v->dpp_per_plane[input_idx] == 2 ||
					((pipe->stream->view_format ==
					  VIEW_3D_FORMAT_SIDE_BY_SIDE ||
					  pipe->stream->view_format ==
					  VIEW_3D_FORMAT_TOP_AND_BOTTOM) &&
					(pipe->stream->timing.timing_3d_format ==
					 TIMING_3D_FORMAT_TOP_AND_BOTTOM ||
					 pipe->stream->timing.timing_3d_format ==
					 TIMING_3D_FORMAT_SIDE_BY_SIDE))) {
					if (hsplit_pipe && hsplit_pipe->plane_state == pipe->plane_state) {
						/* update previously split pipe */
						hsplit_pipe->pipe_dlg_param.vupdate_width = v->v_update_width_pix[input_idx];
						hsplit_pipe->pipe_dlg_param.vupdate_offset = v->v_update_offset_pix[input_idx];
						hsplit_pipe->pipe_dlg_param.vready_offset = v->v_ready_offset_pix[input_idx];
						hsplit_pipe->pipe_dlg_param.vstartup_start = v->v_startup[input_idx];

						hsplit_pipe->pipe_dlg_param.htotal = pipe->stream->timing.h_total;
						hsplit_pipe->pipe_dlg_param.vtotal = pipe->stream->timing.v_total;
						hsplit_pipe->pipe_dlg_param.vblank_start = pipe->pipe_dlg_param.vblank_start;
						hsplit_pipe->pipe_dlg_param.vblank_end = pipe->pipe_dlg_param.vblank_end;
					} else {
						/* pipe not split previously needs split */
						hsplit_pipe = resource_find_free_secondary_pipe_legacy(&context->res_ctx, pool, pipe);
						ASSERT(hsplit_pipe);
						split_stream_across_pipes(&context->res_ctx, pool, pipe, hsplit_pipe);
					}

					dcn_bw_calc_rq_dlg_ttu(dc, v, hsplit_pipe, input_idx);
				} else if (hsplit_pipe && hsplit_pipe->plane_state == pipe->plane_state) {
					/* merge previously split pipe */
					pipe->bottom_pipe = hsplit_pipe->bottom_pipe;
					if (hsplit_pipe->bottom_pipe)
						hsplit_pipe->bottom_pipe->top_pipe = pipe;
					hsplit_pipe->plane_state = NULL;
					hsplit_pipe->stream = NULL;
					hsplit_pipe->top_pipe = NULL;
					hsplit_pipe->bottom_pipe = NULL;
					/* Clear plane_res and stream_res */
					memset(&hsplit_pipe->plane_res, 0, sizeof(hsplit_pipe->plane_res));
					memset(&hsplit_pipe->stream_res, 0, sizeof(hsplit_pipe->stream_res));
					resource_build_scaling_params(pipe);
				}
				/* for now important to do this after pipe split for building e2e params */
				dcn_bw_calc_rq_dlg_ttu(dc, v, pipe, input_idx);
			}

			input_idx++;
		}
	} else if (v->voltage_level == number_of_states_plus_one) {
		BW_VAL_TRACE_SKIP(fail);
	} else if (fast_validate) {
		BW_VAL_TRACE_SKIP(fast);
	}

	if (v->voltage_level == 0) {
		context->bw_ctx.dml.soc.sr_enter_plus_exit_time_us =
				dc->dcn_soc->sr_enter_plus_exit_time;
		context->bw_ctx.dml.soc.sr_exit_time_us = dc->dcn_soc->sr_exit_time;
	}

	/*
	 * BW limit is set to prevent display from impacting other system functions
	 */

	bw_limit = dc->dcn_soc->percent_disp_bw_limit * v->fabric_and_dram_bandwidth_vmax0p9;
	bw_limit_pass = (v->total_data_read_bandwidth / 1000.0) < bw_limit;

	PERFORMANCE_TRACE_END();
	BW_VAL_TRACE_FINISH();

	if (bw_limit_pass && v->voltage_level <= get_highest_allowed_voltage_level(dc->config.is_vmin_only_asic))
		return true;
	else
		return false;
}

static unsigned int dcn_find_normalized_clock_vdd_Level(
	const struct dc *dc,
	enum dm_pp_clock_type clocks_type,
	int clocks_in_khz)
{
	int vdd_level = dcn_bw_v_min0p65;

	if (clocks_in_khz == 0)/*todo some clock not in the considerations*/
		return vdd_level;

	switch (clocks_type) {
	case DM_PP_CLOCK_TYPE_DISPLAY_CLK:
		if (clocks_in_khz > dc->dcn_soc->max_dispclk_vmax0p9*1000) {
			vdd_level = dcn_bw_v_max0p91;
			BREAK_TO_DEBUGGER();
		} else if (clocks_in_khz > dc->dcn_soc->max_dispclk_vnom0p8*1000) {
			vdd_level = dcn_bw_v_max0p9;
		} else if (clocks_in_khz > dc->dcn_soc->max_dispclk_vmid0p72*1000) {
			vdd_level = dcn_bw_v_nom0p8;
		} else if (clocks_in_khz > dc->dcn_soc->max_dispclk_vmin0p65*1000) {
			vdd_level = dcn_bw_v_mid0p72;
		} else
			vdd_level = dcn_bw_v_min0p65;
		break;
	case DM_PP_CLOCK_TYPE_DISPLAYPHYCLK:
		if (clocks_in_khz > dc->dcn_soc->phyclkv_max0p9*1000) {
			vdd_level = dcn_bw_v_max0p91;
			BREAK_TO_DEBUGGER();
		} else if (clocks_in_khz > dc->dcn_soc->phyclkv_nom0p8*1000) {
			vdd_level = dcn_bw_v_max0p9;
		} else if (clocks_in_khz > dc->dcn_soc->phyclkv_mid0p72*1000) {
			vdd_level = dcn_bw_v_nom0p8;
		} else if (clocks_in_khz > dc->dcn_soc->phyclkv_min0p65*1000) {
			vdd_level = dcn_bw_v_mid0p72;
		} else
			vdd_level = dcn_bw_v_min0p65;
		break;

	case DM_PP_CLOCK_TYPE_DPPCLK:
		if (clocks_in_khz > dc->dcn_soc->max_dppclk_vmax0p9*1000) {
			vdd_level = dcn_bw_v_max0p91;
			BREAK_TO_DEBUGGER();
		} else if (clocks_in_khz > dc->dcn_soc->max_dppclk_vnom0p8*1000) {
			vdd_level = dcn_bw_v_max0p9;
		} else if (clocks_in_khz > dc->dcn_soc->max_dppclk_vmid0p72*1000) {
			vdd_level = dcn_bw_v_nom0p8;
		} else if (clocks_in_khz > dc->dcn_soc->max_dppclk_vmin0p65*1000) {
			vdd_level = dcn_bw_v_mid0p72;
		} else
			vdd_level = dcn_bw_v_min0p65;
		break;

	case DM_PP_CLOCK_TYPE_MEMORY_CLK:
		{
			unsigned factor = (ddr4_dram_factor_single_Channel * dc->dcn_soc->number_of_channels);

			if (clocks_in_khz > dc->dcn_soc->fabric_and_dram_bandwidth_vmax0p9*1000000/factor) {
				vdd_level = dcn_bw_v_max0p91;
				BREAK_TO_DEBUGGER();
			} else if (clocks_in_khz > dc->dcn_soc->fabric_and_dram_bandwidth_vnom0p8*1000000/factor) {
				vdd_level = dcn_bw_v_max0p9;
			} else if (clocks_in_khz > dc->dcn_soc->fabric_and_dram_bandwidth_vmid0p72*1000000/factor) {
				vdd_level = dcn_bw_v_nom0p8;
			} else if (clocks_in_khz > dc->dcn_soc->fabric_and_dram_bandwidth_vmin0p65*1000000/factor) {
				vdd_level = dcn_bw_v_mid0p72;
			} else
				vdd_level = dcn_bw_v_min0p65;
		}
		break;

	case DM_PP_CLOCK_TYPE_DCFCLK:
		if (clocks_in_khz > dc->dcn_soc->dcfclkv_max0p9*1000) {
			vdd_level = dcn_bw_v_max0p91;
			BREAK_TO_DEBUGGER();
		} else if (clocks_in_khz > dc->dcn_soc->dcfclkv_nom0p8*1000) {
			vdd_level = dcn_bw_v_max0p9;
		} else if (clocks_in_khz > dc->dcn_soc->dcfclkv_mid0p72*1000) {
			vdd_level = dcn_bw_v_nom0p8;
		} else if (clocks_in_khz > dc->dcn_soc->dcfclkv_min0p65*1000) {
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
	const struct dc *dc,
	struct dc_clocks *clocks)
{
	unsigned vdd_level, vdd_level_temp;
	unsigned dcf_clk;

	/*find a common supported voltage level*/
	vdd_level = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_DISPLAY_CLK, clocks->dispclk_khz);
	vdd_level_temp = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_DISPLAYPHYCLK, clocks->phyclk_khz);

	vdd_level = dcn_bw_max(vdd_level, vdd_level_temp);
	vdd_level_temp = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_DPPCLK, clocks->dppclk_khz);
	vdd_level = dcn_bw_max(vdd_level, vdd_level_temp);

	vdd_level_temp = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_MEMORY_CLK, clocks->fclk_khz);
	vdd_level = dcn_bw_max(vdd_level, vdd_level_temp);
	vdd_level_temp = dcn_find_normalized_clock_vdd_Level(
		dc, DM_PP_CLOCK_TYPE_DCFCLK, clocks->dcfclk_khz);

	/*find that level conresponding dcfclk*/
	vdd_level = dcn_bw_max(vdd_level, vdd_level_temp);
	if (vdd_level == dcn_bw_v_max0p91) {
		BREAK_TO_DEBUGGER();
		dcf_clk = dc->dcn_soc->dcfclkv_max0p9*1000;
	} else if (vdd_level == dcn_bw_v_max0p9)
		dcf_clk =  dc->dcn_soc->dcfclkv_max0p9*1000;
	else if (vdd_level == dcn_bw_v_nom0p8)
		dcf_clk =  dc->dcn_soc->dcfclkv_nom0p8*1000;
	else if (vdd_level == dcn_bw_v_mid0p72)
		dcf_clk =  dc->dcn_soc->dcfclkv_mid0p72*1000;
	else
		dcf_clk =  dc->dcn_soc->dcfclkv_min0p65*1000;

	DC_LOG_BANDWIDTH_CALCS("\tdcf_clk for voltage = %d\n", dcf_clk);
	return dcf_clk;
}

void dcn_bw_update_from_pplib_fclks(
	struct dc *dc,
	struct dm_pp_clock_levels_with_voltage *fclks)
{
	unsigned vmin0p65_idx, vmid0p72_idx, vnom0p8_idx, vmax0p9_idx;

	ASSERT(fclks->num_levels);

	vmin0p65_idx = 0;
	vmid0p72_idx = fclks->num_levels -
		(fclks->num_levels > 2 ? 3 : (fclks->num_levels > 1 ? 2 : 1));
	vnom0p8_idx = fclks->num_levels - (fclks->num_levels > 1 ? 2 : 1);
	vmax0p9_idx = fclks->num_levels - 1;

	dc->dcn_soc->fabric_and_dram_bandwidth_vmin0p65 =
		32 * (fclks->data[vmin0p65_idx].clocks_in_khz / 1000.0) / 1000.0;
	dc->dcn_soc->fabric_and_dram_bandwidth_vmid0p72 =
		dc->dcn_soc->number_of_channels *
		(fclks->data[vmid0p72_idx].clocks_in_khz / 1000.0)
		* ddr4_dram_factor_single_Channel / 1000.0;
	dc->dcn_soc->fabric_and_dram_bandwidth_vnom0p8 =
		dc->dcn_soc->number_of_channels *
		(fclks->data[vnom0p8_idx].clocks_in_khz / 1000.0)
		* ddr4_dram_factor_single_Channel / 1000.0;
	dc->dcn_soc->fabric_and_dram_bandwidth_vmax0p9 =
		dc->dcn_soc->number_of_channels *
		(fclks->data[vmax0p9_idx].clocks_in_khz / 1000.0)
		* ddr4_dram_factor_single_Channel / 1000.0;
}

void dcn_bw_update_from_pplib_dcfclks(
	struct dc *dc,
	struct dm_pp_clock_levels_with_voltage *dcfclks)
{
	if (dcfclks->num_levels >= 3) {
		dc->dcn_soc->dcfclkv_min0p65 = dcfclks->data[0].clocks_in_khz / 1000.0;
		dc->dcn_soc->dcfclkv_mid0p72 = dcfclks->data[dcfclks->num_levels - 3].clocks_in_khz / 1000.0;
		dc->dcn_soc->dcfclkv_nom0p8 = dcfclks->data[dcfclks->num_levels - 2].clocks_in_khz / 1000.0;
		dc->dcn_soc->dcfclkv_max0p9 = dcfclks->data[dcfclks->num_levels - 1].clocks_in_khz / 1000.0;
	}
}

void dcn_get_soc_clks(
	struct dc *dc,
	int *min_fclk_khz,
	int *min_dcfclk_khz,
	int *socclk_khz)
{
	*min_fclk_khz = dc->dcn_soc->fabric_and_dram_bandwidth_vmin0p65 * 1000000 / 32;
	*min_dcfclk_khz = dc->dcn_soc->dcfclkv_min0p65 * 1000;
	*socclk_khz = dc->dcn_soc->socclk * 1000;
}

void dcn_bw_notify_pplib_of_wm_ranges(
	struct dc *dc,
	int min_fclk_khz,
	int min_dcfclk_khz,
	int socclk_khz)
{
	struct pp_smu_funcs_rv *pp = NULL;
	struct pp_smu_wm_range_sets ranges = {0};
	const int overdrive = 5000000; /* 5 GHz to cover Overdrive */

	if (dc->res_pool->pp_smu)
		pp = &dc->res_pool->pp_smu->rv_funcs;
	if (!pp || !pp->set_wm_ranges)
		return;

	/* Now notify PPLib/SMU about which Watermarks sets they should select
	 * depending on DPM state they are in. And update BW MGR GFX Engine and
	 * Memory clock member variables for Watermarks calculations for each
	 * Watermark Set. Only one watermark set for dcn1 due to hw bug DEGVIDCN10-254.
	 */
	/* SOCCLK does not affect anytihng but writeback for DCN so for now we dont
	 * care what the value is, hence min to overdrive level
	 */
	ranges.num_reader_wm_sets = WM_SET_COUNT;
	ranges.num_writer_wm_sets = WM_SET_COUNT;
	ranges.reader_wm_sets[0].wm_inst = WM_A;
	ranges.reader_wm_sets[0].min_drain_clk_mhz = min_dcfclk_khz / 1000;
	ranges.reader_wm_sets[0].max_drain_clk_mhz = overdrive / 1000;
	ranges.reader_wm_sets[0].min_fill_clk_mhz = min_fclk_khz / 1000;
	ranges.reader_wm_sets[0].max_fill_clk_mhz = overdrive / 1000;
	ranges.writer_wm_sets[0].wm_inst = WM_A;
	ranges.writer_wm_sets[0].min_fill_clk_mhz = socclk_khz / 1000;
	ranges.writer_wm_sets[0].max_fill_clk_mhz = overdrive / 1000;
	ranges.writer_wm_sets[0].min_drain_clk_mhz = min_fclk_khz / 1000;
	ranges.writer_wm_sets[0].max_drain_clk_mhz = overdrive / 1000;

	if (dc->debug.pplib_wm_report_mode == WM_REPORT_OVERRIDE) {
		ranges.reader_wm_sets[0].wm_inst = WM_A;
		ranges.reader_wm_sets[0].min_drain_clk_mhz = 300;
		ranges.reader_wm_sets[0].max_drain_clk_mhz = 5000;
		ranges.reader_wm_sets[0].min_fill_clk_mhz = 800;
		ranges.reader_wm_sets[0].max_fill_clk_mhz = 5000;
		ranges.writer_wm_sets[0].wm_inst = WM_A;
		ranges.writer_wm_sets[0].min_fill_clk_mhz = 200;
		ranges.writer_wm_sets[0].max_fill_clk_mhz = 5000;
		ranges.writer_wm_sets[0].min_drain_clk_mhz = 800;
		ranges.writer_wm_sets[0].max_drain_clk_mhz = 5000;
	}

	ranges.reader_wm_sets[1] = ranges.writer_wm_sets[0];
	ranges.reader_wm_sets[1].wm_inst = WM_B;

	ranges.reader_wm_sets[2] = ranges.writer_wm_sets[0];
	ranges.reader_wm_sets[2].wm_inst = WM_C;

	ranges.reader_wm_sets[3] = ranges.writer_wm_sets[0];
	ranges.reader_wm_sets[3].wm_inst = WM_D;

	/* Notify PP Lib/SMU which Watermarks to use for which clock ranges */
	pp->set_wm_ranges(&pp->pp_smu, &ranges);
}

void dcn_bw_sync_calcs_and_dml(struct dc *dc)
{
	DC_LOG_BANDWIDTH_CALCS("sr_exit_time: %f ns\n"
			"sr_enter_plus_exit_time: %f ns\n"
			"urgent_latency: %f ns\n"
			"write_back_latency: %f ns\n"
			"percent_of_ideal_drambw_received_after_urg_latency: %f %%\n"
			"max_request_size: %d bytes\n"
			"dcfclkv_max0p9: %f kHz\n"
			"dcfclkv_nom0p8: %f kHz\n"
			"dcfclkv_mid0p72: %f kHz\n"
			"dcfclkv_min0p65: %f kHz\n"
			"max_dispclk_vmax0p9: %f kHz\n"
			"max_dispclk_vnom0p8: %f kHz\n"
			"max_dispclk_vmid0p72: %f kHz\n"
			"max_dispclk_vmin0p65: %f kHz\n"
			"max_dppclk_vmax0p9: %f kHz\n"
			"max_dppclk_vnom0p8: %f kHz\n"
			"max_dppclk_vmid0p72: %f kHz\n"
			"max_dppclk_vmin0p65: %f kHz\n"
			"socclk: %f kHz\n"
			"fabric_and_dram_bandwidth_vmax0p9: %f MB/s\n"
			"fabric_and_dram_bandwidth_vnom0p8: %f MB/s\n"
			"fabric_and_dram_bandwidth_vmid0p72: %f MB/s\n"
			"fabric_and_dram_bandwidth_vmin0p65: %f MB/s\n"
			"phyclkv_max0p9: %f kHz\n"
			"phyclkv_nom0p8: %f kHz\n"
			"phyclkv_mid0p72: %f kHz\n"
			"phyclkv_min0p65: %f kHz\n"
			"downspreading: %f %%\n"
			"round_trip_ping_latency_cycles: %d DCFCLK Cycles\n"
			"urgent_out_of_order_return_per_channel: %d Bytes\n"
			"number_of_channels: %d\n"
			"vmm_page_size: %d Bytes\n"
			"dram_clock_change_latency: %f ns\n"
			"return_bus_width: %d Bytes\n",
			dc->dcn_soc->sr_exit_time * 1000,
			dc->dcn_soc->sr_enter_plus_exit_time * 1000,
			dc->dcn_soc->urgent_latency * 1000,
			dc->dcn_soc->write_back_latency * 1000,
			dc->dcn_soc->percent_of_ideal_drambw_received_after_urg_latency,
			dc->dcn_soc->max_request_size,
			dc->dcn_soc->dcfclkv_max0p9 * 1000,
			dc->dcn_soc->dcfclkv_nom0p8 * 1000,
			dc->dcn_soc->dcfclkv_mid0p72 * 1000,
			dc->dcn_soc->dcfclkv_min0p65 * 1000,
			dc->dcn_soc->max_dispclk_vmax0p9 * 1000,
			dc->dcn_soc->max_dispclk_vnom0p8 * 1000,
			dc->dcn_soc->max_dispclk_vmid0p72 * 1000,
			dc->dcn_soc->max_dispclk_vmin0p65 * 1000,
			dc->dcn_soc->max_dppclk_vmax0p9 * 1000,
			dc->dcn_soc->max_dppclk_vnom0p8 * 1000,
			dc->dcn_soc->max_dppclk_vmid0p72 * 1000,
			dc->dcn_soc->max_dppclk_vmin0p65 * 1000,
			dc->dcn_soc->socclk * 1000,
			dc->dcn_soc->fabric_and_dram_bandwidth_vmax0p9 * 1000,
			dc->dcn_soc->fabric_and_dram_bandwidth_vnom0p8 * 1000,
			dc->dcn_soc->fabric_and_dram_bandwidth_vmid0p72 * 1000,
			dc->dcn_soc->fabric_and_dram_bandwidth_vmin0p65 * 1000,
			dc->dcn_soc->phyclkv_max0p9 * 1000,
			dc->dcn_soc->phyclkv_nom0p8 * 1000,
			dc->dcn_soc->phyclkv_mid0p72 * 1000,
			dc->dcn_soc->phyclkv_min0p65 * 1000,
			dc->dcn_soc->downspreading * 100,
			dc->dcn_soc->round_trip_ping_latency_cycles,
			dc->dcn_soc->urgent_out_of_order_return_per_channel,
			dc->dcn_soc->number_of_channels,
			dc->dcn_soc->vmm_page_size,
			dc->dcn_soc->dram_clock_change_latency * 1000,
			dc->dcn_soc->return_bus_width);
	DC_LOG_BANDWIDTH_CALCS("rob_buffer_size_in_kbyte: %f\n"
			"det_buffer_size_in_kbyte: %f\n"
			"dpp_output_buffer_pixels: %f\n"
			"opp_output_buffer_lines: %f\n"
			"pixel_chunk_size_in_kbyte: %f\n"
			"pte_enable: %d\n"
			"pte_chunk_size: %d kbytes\n"
			"meta_chunk_size: %d kbytes\n"
			"writeback_chunk_size: %d kbytes\n"
			"odm_capability: %d\n"
			"dsc_capability: %d\n"
			"line_buffer_size: %d bits\n"
			"max_line_buffer_lines: %d\n"
			"is_line_buffer_bpp_fixed: %d\n"
			"line_buffer_fixed_bpp: %d\n"
			"writeback_luma_buffer_size: %d kbytes\n"
			"writeback_chroma_buffer_size: %d kbytes\n"
			"max_num_dpp: %d\n"
			"max_num_writeback: %d\n"
			"max_dchub_topscl_throughput: %d pixels/dppclk\n"
			"max_pscl_tolb_throughput: %d pixels/dppclk\n"
			"max_lb_tovscl_throughput: %d pixels/dppclk\n"
			"max_vscl_tohscl_throughput: %d pixels/dppclk\n"
			"max_hscl_ratio: %f\n"
			"max_vscl_ratio: %f\n"
			"max_hscl_taps: %d\n"
			"max_vscl_taps: %d\n"
			"pte_buffer_size_in_requests: %d\n"
			"dispclk_ramping_margin: %f %%\n"
			"under_scan_factor: %f %%\n"
			"max_inter_dcn_tile_repeaters: %d\n"
			"can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one: %d\n"
			"bug_forcing_luma_and_chroma_request_to_same_size_fixed: %d\n"
			"dcfclk_cstate_latency: %d\n",
			dc->dcn_ip->rob_buffer_size_in_kbyte,
			dc->dcn_ip->det_buffer_size_in_kbyte,
			dc->dcn_ip->dpp_output_buffer_pixels,
			dc->dcn_ip->opp_output_buffer_lines,
			dc->dcn_ip->pixel_chunk_size_in_kbyte,
			dc->dcn_ip->pte_enable,
			dc->dcn_ip->pte_chunk_size,
			dc->dcn_ip->meta_chunk_size,
			dc->dcn_ip->writeback_chunk_size,
			dc->dcn_ip->odm_capability,
			dc->dcn_ip->dsc_capability,
			dc->dcn_ip->line_buffer_size,
			dc->dcn_ip->max_line_buffer_lines,
			dc->dcn_ip->is_line_buffer_bpp_fixed,
			dc->dcn_ip->line_buffer_fixed_bpp,
			dc->dcn_ip->writeback_luma_buffer_size,
			dc->dcn_ip->writeback_chroma_buffer_size,
			dc->dcn_ip->max_num_dpp,
			dc->dcn_ip->max_num_writeback,
			dc->dcn_ip->max_dchub_topscl_throughput,
			dc->dcn_ip->max_pscl_tolb_throughput,
			dc->dcn_ip->max_lb_tovscl_throughput,
			dc->dcn_ip->max_vscl_tohscl_throughput,
			dc->dcn_ip->max_hscl_ratio,
			dc->dcn_ip->max_vscl_ratio,
			dc->dcn_ip->max_hscl_taps,
			dc->dcn_ip->max_vscl_taps,
			dc->dcn_ip->pte_buffer_size_in_requests,
			dc->dcn_ip->dispclk_ramping_margin,
			dc->dcn_ip->under_scan_factor * 100,
			dc->dcn_ip->max_inter_dcn_tile_repeaters,
			dc->dcn_ip->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one,
			dc->dcn_ip->bug_forcing_luma_and_chroma_request_to_same_size_fixed,
			dc->dcn_ip->dcfclk_cstate_latency);

	dc->dml.soc.sr_exit_time_us = dc->dcn_soc->sr_exit_time;
	dc->dml.soc.sr_enter_plus_exit_time_us = dc->dcn_soc->sr_enter_plus_exit_time;
	dc->dml.soc.urgent_latency_us = dc->dcn_soc->urgent_latency;
	dc->dml.soc.writeback_latency_us = dc->dcn_soc->write_back_latency;
	dc->dml.soc.ideal_dram_bw_after_urgent_percent =
			dc->dcn_soc->percent_of_ideal_drambw_received_after_urg_latency;
	dc->dml.soc.max_request_size_bytes = dc->dcn_soc->max_request_size;
	dc->dml.soc.downspread_percent = dc->dcn_soc->downspreading;
	dc->dml.soc.round_trip_ping_latency_dcfclk_cycles =
			dc->dcn_soc->round_trip_ping_latency_cycles;
	dc->dml.soc.urgent_out_of_order_return_per_channel_bytes =
			dc->dcn_soc->urgent_out_of_order_return_per_channel;
	dc->dml.soc.num_chans = dc->dcn_soc->number_of_channels;
	dc->dml.soc.vmm_page_size_bytes = dc->dcn_soc->vmm_page_size;
	dc->dml.soc.dram_clock_change_latency_us = dc->dcn_soc->dram_clock_change_latency;
	dc->dml.soc.return_bus_width_bytes = dc->dcn_soc->return_bus_width;

	dc->dml.ip.rob_buffer_size_kbytes = dc->dcn_ip->rob_buffer_size_in_kbyte;
	dc->dml.ip.det_buffer_size_kbytes = dc->dcn_ip->det_buffer_size_in_kbyte;
	dc->dml.ip.dpp_output_buffer_pixels = dc->dcn_ip->dpp_output_buffer_pixels;
	dc->dml.ip.opp_output_buffer_lines = dc->dcn_ip->opp_output_buffer_lines;
	dc->dml.ip.pixel_chunk_size_kbytes = dc->dcn_ip->pixel_chunk_size_in_kbyte;
	dc->dml.ip.pte_enable = dc->dcn_ip->pte_enable == dcn_bw_yes;
	dc->dml.ip.pte_chunk_size_kbytes = dc->dcn_ip->pte_chunk_size;
	dc->dml.ip.meta_chunk_size_kbytes = dc->dcn_ip->meta_chunk_size;
	dc->dml.ip.writeback_chunk_size_kbytes = dc->dcn_ip->writeback_chunk_size;
	dc->dml.ip.line_buffer_size_bits = dc->dcn_ip->line_buffer_size;
	dc->dml.ip.max_line_buffer_lines = dc->dcn_ip->max_line_buffer_lines;
	dc->dml.ip.IsLineBufferBppFixed = dc->dcn_ip->is_line_buffer_bpp_fixed == dcn_bw_yes;
	dc->dml.ip.LineBufferFixedBpp = dc->dcn_ip->line_buffer_fixed_bpp;
	dc->dml.ip.writeback_luma_buffer_size_kbytes = dc->dcn_ip->writeback_luma_buffer_size;
	dc->dml.ip.writeback_chroma_buffer_size_kbytes = dc->dcn_ip->writeback_chroma_buffer_size;
	dc->dml.ip.max_num_dpp = dc->dcn_ip->max_num_dpp;
	dc->dml.ip.max_num_wb = dc->dcn_ip->max_num_writeback;
	dc->dml.ip.max_dchub_pscl_bw_pix_per_clk = dc->dcn_ip->max_dchub_topscl_throughput;
	dc->dml.ip.max_pscl_lb_bw_pix_per_clk = dc->dcn_ip->max_pscl_tolb_throughput;
	dc->dml.ip.max_lb_vscl_bw_pix_per_clk = dc->dcn_ip->max_lb_tovscl_throughput;
	dc->dml.ip.max_vscl_hscl_bw_pix_per_clk = dc->dcn_ip->max_vscl_tohscl_throughput;
	dc->dml.ip.max_hscl_ratio = dc->dcn_ip->max_hscl_ratio;
	dc->dml.ip.max_vscl_ratio = dc->dcn_ip->max_vscl_ratio;
	dc->dml.ip.max_hscl_taps = dc->dcn_ip->max_hscl_taps;
	dc->dml.ip.max_vscl_taps = dc->dcn_ip->max_vscl_taps;
	/*pte_buffer_size_in_requests missing in dml*/
	dc->dml.ip.dispclk_ramp_margin_percent = dc->dcn_ip->dispclk_ramping_margin;
	dc->dml.ip.underscan_factor = dc->dcn_ip->under_scan_factor;
	dc->dml.ip.max_inter_dcn_tile_repeaters = dc->dcn_ip->max_inter_dcn_tile_repeaters;
	dc->dml.ip.can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one =
		dc->dcn_ip->can_vstartup_lines_exceed_vsync_plus_back_porch_lines_minus_one == dcn_bw_yes;
	dc->dml.ip.bug_forcing_LC_req_same_size_fixed =
		dc->dcn_ip->bug_forcing_luma_and_chroma_request_to_same_size_fixed == dcn_bw_yes;
	dc->dml.ip.dcfclk_cstate_latency = dc->dcn_ip->dcfclk_cstate_latency;
}
