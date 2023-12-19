/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

//#include "dml2_utils.h"
#include "display_mode_core.h"
#include "dml_display_rq_dlg_calc.h"
#include "dml2_internal_types.h"
#include "dml2_translation_helper.h"
#include "dml2_utils.h"

void dml2_util_copy_dml_timing(struct dml_timing_cfg_st *dml_timing_array, unsigned int dst_index, unsigned int src_index)
{
	dml_timing_array->HTotal[dst_index] = dml_timing_array->HTotal[src_index];
	dml_timing_array->VTotal[dst_index] = dml_timing_array->VTotal[src_index];
	dml_timing_array->HBlankEnd[dst_index] = dml_timing_array->HBlankEnd[src_index];
	dml_timing_array->VBlankEnd[dst_index] = dml_timing_array->VBlankEnd[src_index];
	dml_timing_array->RefreshRate[dst_index] = dml_timing_array->RefreshRate[src_index];
	dml_timing_array->VFrontPorch[dst_index] = dml_timing_array->VFrontPorch[src_index];
	dml_timing_array->PixelClock[dst_index] = dml_timing_array->PixelClock[src_index];
	dml_timing_array->HActive[dst_index] = dml_timing_array->HActive[src_index];
	dml_timing_array->VActive[dst_index] = dml_timing_array->VActive[src_index];
	dml_timing_array->Interlace[dst_index] = dml_timing_array->Interlace[src_index];
	dml_timing_array->DRRDisplay[dst_index] = dml_timing_array->DRRDisplay[src_index];
	dml_timing_array->VBlankNom[dst_index] = dml_timing_array->VBlankNom[src_index];
}

void dml2_util_copy_dml_plane(struct dml_plane_cfg_st *dml_plane_array, unsigned int dst_index, unsigned int src_index)
{
	dml_plane_array->GPUVMMinPageSizeKBytes[dst_index] = dml_plane_array->GPUVMMinPageSizeKBytes[src_index];
	dml_plane_array->ForceOneRowForFrame[dst_index] = dml_plane_array->ForceOneRowForFrame[src_index];
	dml_plane_array->PTEBufferModeOverrideEn[dst_index] = dml_plane_array->PTEBufferModeOverrideEn[src_index];
	dml_plane_array->PTEBufferMode[dst_index] = dml_plane_array->PTEBufferMode[src_index];
	dml_plane_array->ViewportWidth[dst_index] = dml_plane_array->ViewportWidth[src_index];
	dml_plane_array->ViewportHeight[dst_index] = dml_plane_array->ViewportHeight[src_index];
	dml_plane_array->ViewportWidthChroma[dst_index] = dml_plane_array->ViewportWidthChroma[src_index];
	dml_plane_array->ViewportHeightChroma[dst_index] = dml_plane_array->ViewportHeightChroma[src_index];
	dml_plane_array->ViewportXStart[dst_index] = dml_plane_array->ViewportXStart[src_index];
	dml_plane_array->ViewportXStartC[dst_index] = dml_plane_array->ViewportXStartC[src_index];
	dml_plane_array->ViewportYStart[dst_index] = dml_plane_array->ViewportYStart[src_index];
	dml_plane_array->ViewportYStartC[dst_index] = dml_plane_array->ViewportYStartC[src_index];
	dml_plane_array->ViewportStationary[dst_index] = dml_plane_array->ViewportStationary[src_index];

	dml_plane_array->ScalerEnabled[dst_index] = dml_plane_array->ScalerEnabled[src_index];
	dml_plane_array->HRatio[dst_index] = dml_plane_array->HRatio[src_index];
	dml_plane_array->VRatio[dst_index] = dml_plane_array->VRatio[src_index];
	dml_plane_array->HRatioChroma[dst_index] = dml_plane_array->HRatioChroma[src_index];
	dml_plane_array->VRatioChroma[dst_index] = dml_plane_array->VRatioChroma[src_index];
	dml_plane_array->HTaps[dst_index] = dml_plane_array->HTaps[src_index];
	dml_plane_array->VTaps[dst_index] = dml_plane_array->VTaps[src_index];
	dml_plane_array->HTapsChroma[dst_index] = dml_plane_array->HTapsChroma[src_index];
	dml_plane_array->VTapsChroma[dst_index] = dml_plane_array->VTapsChroma[src_index];
	dml_plane_array->LBBitPerPixel[dst_index] = dml_plane_array->LBBitPerPixel[src_index];

	dml_plane_array->SourceScan[dst_index] = dml_plane_array->SourceScan[src_index];
	dml_plane_array->ScalerRecoutWidth[dst_index] = dml_plane_array->ScalerRecoutWidth[src_index];

	dml_plane_array->DynamicMetadataEnable[dst_index] = dml_plane_array->DynamicMetadataEnable[src_index];
	dml_plane_array->DynamicMetadataLinesBeforeActiveRequired[dst_index] = dml_plane_array->DynamicMetadataLinesBeforeActiveRequired[src_index];
	dml_plane_array->DynamicMetadataTransmittedBytes[dst_index] = dml_plane_array->DynamicMetadataTransmittedBytes[src_index];
	dml_plane_array->DETSizeOverride[dst_index] = dml_plane_array->DETSizeOverride[src_index];

	dml_plane_array->NumberOfCursors[dst_index] = dml_plane_array->NumberOfCursors[src_index];
	dml_plane_array->CursorWidth[dst_index] = dml_plane_array->CursorWidth[src_index];
	dml_plane_array->CursorBPP[dst_index] = dml_plane_array->CursorBPP[src_index];

	dml_plane_array->UseMALLForStaticScreen[dst_index] = dml_plane_array->UseMALLForStaticScreen[src_index];
	dml_plane_array->UseMALLForPStateChange[dst_index] = dml_plane_array->UseMALLForPStateChange[src_index];

	dml_plane_array->BlendingAndTiming[dst_index] = dml_plane_array->BlendingAndTiming[src_index];
}

void dml2_util_copy_dml_surface(struct dml_surface_cfg_st *dml_surface_array, unsigned int dst_index, unsigned int src_index)
{
	dml_surface_array->SurfaceTiling[dst_index] = dml_surface_array->SurfaceTiling[src_index];
	dml_surface_array->SourcePixelFormat[dst_index] = dml_surface_array->SourcePixelFormat[src_index];
	dml_surface_array->PitchY[dst_index] = dml_surface_array->PitchY[src_index];
	dml_surface_array->SurfaceWidthY[dst_index] = dml_surface_array->SurfaceWidthY[src_index];
	dml_surface_array->SurfaceHeightY[dst_index] = dml_surface_array->SurfaceHeightY[src_index];
	dml_surface_array->PitchC[dst_index] = dml_surface_array->PitchC[src_index];
	dml_surface_array->SurfaceWidthC[dst_index] = dml_surface_array->SurfaceWidthC[src_index];
	dml_surface_array->SurfaceHeightC[dst_index] = dml_surface_array->SurfaceHeightC[src_index];

	dml_surface_array->DCCEnable[dst_index] = dml_surface_array->DCCEnable[src_index];
	dml_surface_array->DCCMetaPitchY[dst_index] = dml_surface_array->DCCMetaPitchY[src_index];
	dml_surface_array->DCCMetaPitchC[dst_index] = dml_surface_array->DCCMetaPitchC[src_index];

	dml_surface_array->DCCRateLuma[dst_index] = dml_surface_array->DCCRateLuma[src_index];
	dml_surface_array->DCCRateChroma[dst_index] = dml_surface_array->DCCRateChroma[src_index];
	dml_surface_array->DCCFractionOfZeroSizeRequestsLuma[dst_index] = dml_surface_array->DCCFractionOfZeroSizeRequestsLuma[src_index];
	dml_surface_array->DCCFractionOfZeroSizeRequestsChroma[dst_index] = dml_surface_array->DCCFractionOfZeroSizeRequestsChroma[src_index];
}

void dml2_util_copy_dml_output(struct dml_output_cfg_st *dml_output_array, unsigned int dst_index, unsigned int src_index)
{
	dml_output_array->DSCInputBitPerComponent[dst_index] = dml_output_array->DSCInputBitPerComponent[src_index];
	dml_output_array->OutputFormat[dst_index] = dml_output_array->OutputFormat[src_index];
	dml_output_array->OutputEncoder[dst_index] = dml_output_array->OutputEncoder[src_index];
	dml_output_array->OutputMultistreamId[dst_index] = dml_output_array->OutputMultistreamId[src_index];
	dml_output_array->OutputMultistreamEn[dst_index] = dml_output_array->OutputMultistreamEn[src_index];
	dml_output_array->OutputBpp[dst_index] = dml_output_array->OutputBpp[src_index];
	dml_output_array->PixelClockBackEnd[dst_index] = dml_output_array->PixelClockBackEnd[src_index];
	dml_output_array->DSCEnable[dst_index] = dml_output_array->DSCEnable[src_index];
	dml_output_array->OutputLinkDPLanes[dst_index] = dml_output_array->OutputLinkDPLanes[src_index];
	dml_output_array->OutputLinkDPRate[dst_index] = dml_output_array->OutputLinkDPRate[src_index];
	dml_output_array->ForcedOutputLinkBPP[dst_index] = dml_output_array->ForcedOutputLinkBPP[src_index];
	dml_output_array->AudioSampleRate[dst_index] = dml_output_array->AudioSampleRate[src_index];
	dml_output_array->AudioSampleLayout[dst_index] = dml_output_array->AudioSampleLayout[src_index];
}

unsigned int dml2_util_get_maximum_odm_combine_for_output(bool force_odm_4to1, enum dml_output_encoder_class encoder, bool dsc_enabled)
{
	switch (encoder) {
	case dml_dp:
	case dml_edp:
		return 2;
	case dml_dp2p0:
	if (dsc_enabled || force_odm_4to1)
		return 4;
	else
		return 2;
	case dml_hdmi:
		return 1;
	case dml_hdmifrl:
	if (force_odm_4to1)
		return 4;
	else
		return 2;
	default:
		return 1;
	}
}

bool is_dp2p0_output_encoder(const struct pipe_ctx *pipe_ctx)
{
	/* If this assert is hit then we have a link encoder dynamic management issue */
	ASSERT(pipe_ctx->stream_res.hpo_dp_stream_enc ? pipe_ctx->link_res.hpo_dp_link_enc != NULL : true);

	if (pipe_ctx->stream == NULL)
		return false;

	return (pipe_ctx->stream_res.hpo_dp_stream_enc &&
		pipe_ctx->link_res.hpo_dp_link_enc &&
		dc_is_dp_signal(pipe_ctx->stream->signal));
}

bool is_dtbclk_required(const struct dc *dc, struct dc_state *context)
{
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;
		if (is_dp2p0_output_encoder(&context->res_ctx.pipe_ctx[i]))
			return true;
	}
	return false;
}

void dml2_copy_clocks_to_dc_state(struct dml2_dcn_clocks *out_clks, struct dc_state *context)
{
	context->bw_ctx.bw.dcn.clk.dispclk_khz = out_clks->dispclk_khz;
	context->bw_ctx.bw.dcn.clk.dcfclk_khz = out_clks->dcfclk_khz;
	context->bw_ctx.bw.dcn.clk.dramclk_khz = out_clks->uclk_mts / 16;
	context->bw_ctx.bw.dcn.clk.fclk_khz = out_clks->fclk_khz;
	context->bw_ctx.bw.dcn.clk.phyclk_khz = out_clks->phyclk_khz;
	context->bw_ctx.bw.dcn.clk.socclk_khz = out_clks->socclk_khz;
	context->bw_ctx.bw.dcn.clk.ref_dtbclk_khz = out_clks->ref_dtbclk_khz;
	context->bw_ctx.bw.dcn.clk.p_state_change_support = out_clks->p_state_supported;
}

int dml2_helper_find_dml_pipe_idx_by_stream_id(struct dml2_context *ctx, unsigned int stream_id)
{
	int i;
	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id_valid[i] && ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id[i] == stream_id)
			return  i;
	}

	return -1;
}

static int find_dml_pipe_idx_by_plane_id(struct dml2_context *ctx, unsigned int plane_id)
{
	int i;
	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id_valid[i] && ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id[i] == plane_id)
			return  i;
	}

	return -1;
}

static bool get_plane_id(struct dml2_context *dml2, const struct dc_state *state, const struct dc_plane_state *plane,
	unsigned int stream_id, unsigned int plane_index, unsigned int *plane_id)
{
	int i, j;
	bool is_plane_duplicate = dml2->v20.scratch.plane_duplicate_exists;

	if (!plane_id)
		return false;

	for (i = 0; i < state->stream_count; i++) {
		if (state->streams[i]->stream_id == stream_id) {
			for (j = 0; j < state->stream_status[i].plane_count; j++) {
				if (state->stream_status[i].plane_states[j] == plane &&
					(!is_plane_duplicate || (is_plane_duplicate && (j == plane_index)))) {
					*plane_id = (i << 16) | j;
					return true;
				}
			}
		}
	}

	return false;
}

static void populate_pipe_ctx_dlg_params_from_dml(struct pipe_ctx *pipe_ctx, struct display_mode_lib_st *mode_lib, dml_uint_t pipe_idx)
{
	unsigned int hactive, vactive, hblank_start, vblank_start, hblank_end, vblank_end;
	struct dc_crtc_timing *timing = &pipe_ctx->stream->timing;

	hactive = timing->h_addressable + timing->h_border_left + timing->h_border_right;
	vactive = timing->v_addressable + timing->v_border_bottom + timing->v_border_top;
	hblank_start = pipe_ctx->stream->timing.h_total - pipe_ctx->stream->timing.h_front_porch;
	vblank_start = pipe_ctx->stream->timing.v_total - pipe_ctx->stream->timing.v_front_porch;

	hblank_end = hblank_start - timing->h_addressable - timing->h_border_left - timing->h_border_right;
	vblank_end = vblank_start - timing->v_addressable - timing->v_border_top - timing->v_border_bottom;

	pipe_ctx->pipe_dlg_param.vstartup_start = dml_get_vstartup_calculated(mode_lib,	pipe_idx);
	pipe_ctx->pipe_dlg_param.vupdate_offset = dml_get_vupdate_offset(mode_lib, pipe_idx);
	pipe_ctx->pipe_dlg_param.vupdate_width = dml_get_vupdate_width(mode_lib, pipe_idx);
	pipe_ctx->pipe_dlg_param.vready_offset = dml_get_vready_offset(mode_lib, pipe_idx);

	pipe_ctx->pipe_dlg_param.otg_inst = pipe_ctx->stream_res.tg->inst;

	pipe_ctx->pipe_dlg_param.hactive = hactive;
	pipe_ctx->pipe_dlg_param.vactive = vactive;
	pipe_ctx->pipe_dlg_param.htotal = pipe_ctx->stream->timing.h_total;
	pipe_ctx->pipe_dlg_param.vtotal = pipe_ctx->stream->timing.v_total;
	pipe_ctx->pipe_dlg_param.hblank_end = hblank_end;
	pipe_ctx->pipe_dlg_param.vblank_end = vblank_end;
	pipe_ctx->pipe_dlg_param.hblank_start = hblank_start;
	pipe_ctx->pipe_dlg_param.vblank_start = vblank_start;
	pipe_ctx->pipe_dlg_param.vfront_porch = pipe_ctx->stream->timing.v_front_porch;
	pipe_ctx->pipe_dlg_param.pixel_rate_mhz = pipe_ctx->stream->timing.pix_clk_100hz / 10000.00;
	pipe_ctx->pipe_dlg_param.refresh_rate = ((timing->pix_clk_100hz * 100) / timing->h_total) / timing->v_total;
	pipe_ctx->pipe_dlg_param.vtotal_max = pipe_ctx->stream->adjust.v_total_max;
	pipe_ctx->pipe_dlg_param.vtotal_min = pipe_ctx->stream->adjust.v_total_min;
	pipe_ctx->pipe_dlg_param.recout_height = pipe_ctx->plane_res.scl_data.recout.height;
	pipe_ctx->pipe_dlg_param.recout_width = pipe_ctx->plane_res.scl_data.recout.width;
	pipe_ctx->pipe_dlg_param.full_recout_height = pipe_ctx->plane_res.scl_data.recout.height;
	pipe_ctx->pipe_dlg_param.full_recout_width = pipe_ctx->plane_res.scl_data.recout.width;
}

void dml2_calculate_rq_and_dlg_params(const struct dc *dc, struct dc_state *context, struct resource_context *out_new_hw_state, struct dml2_context *in_ctx, unsigned int pipe_cnt)
{
	unsigned int dc_pipe_ctx_index, dml_pipe_idx, plane_id;
	bool unbounded_req_enabled = false;
	struct dml2_calculate_rq_and_dlg_params_scratch *s = &in_ctx->v20.scratch.calculate_rq_and_dlg_params_scratch;

	context->bw_ctx.bw.dcn.clk.dcfclk_deep_sleep_khz = (unsigned int)in_ctx->v20.dml_core_ctx.mp.DCFCLKDeepSleep * 1000;
	context->bw_ctx.bw.dcn.clk.dppclk_khz = 0;

	if (in_ctx->v20.dml_core_ctx.ms.support.FCLKChangeSupport[in_ctx->v20.scratch.mode_support_params.out_lowest_state_idx] == dml_fclock_change_unsupported)
		context->bw_ctx.bw.dcn.clk.fclk_p_state_change_support = false;
	else
		context->bw_ctx.bw.dcn.clk.fclk_p_state_change_support = true;

	if (context->bw_ctx.bw.dcn.clk.dispclk_khz < dc->debug.min_disp_clk_khz)
		context->bw_ctx.bw.dcn.clk.dispclk_khz = dc->debug.min_disp_clk_khz;

	unbounded_req_enabled = in_ctx->v20.dml_core_ctx.ms.UnboundedRequestEnabledThisState;

	if (unbounded_req_enabled && pipe_cnt > 1) {
		// Unbounded requesting should not ever be used when more than 1 pipe is enabled.
		//ASSERT(false);
		unbounded_req_enabled = false;
	}

	context->bw_ctx.bw.dcn.compbuf_size_kb = in_ctx->v20.dml_core_ctx.ip.config_return_buffer_size_in_kbytes;

	for (dc_pipe_ctx_index = 0; dc_pipe_ctx_index < pipe_cnt; dc_pipe_ctx_index++) {
		if (!context->res_ctx.pipe_ctx[dc_pipe_ctx_index].stream)
			continue;
		/* The DML2 and the DC logic of determining pipe indices are different from each other so
		 * there is a need to know which DML pipe index maps to which DC pipe. The code below
		 * finds a dml_pipe_index from the plane id if a plane is valid. If a plane is not valid then
		 * it finds a dml_pipe_index from the stream id. */
		if (get_plane_id(in_ctx, context, context->res_ctx.pipe_ctx[dc_pipe_ctx_index].plane_state,
			context->res_ctx.pipe_ctx[dc_pipe_ctx_index].stream->stream_id,
			in_ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_index[context->res_ctx.pipe_ctx[dc_pipe_ctx_index].pipe_idx], &plane_id)) {
			dml_pipe_idx = find_dml_pipe_idx_by_plane_id(in_ctx, plane_id);
		} else {
			dml_pipe_idx = dml2_helper_find_dml_pipe_idx_by_stream_id(in_ctx, context->res_ctx.pipe_ctx[dc_pipe_ctx_index].stream->stream_id);
		}

		ASSERT(in_ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id_valid[dml_pipe_idx]);
		ASSERT(in_ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id[dml_pipe_idx] == context->res_ctx.pipe_ctx[dc_pipe_ctx_index].stream->stream_id);

		/* Use the dml_pipe_index here for the getters to fetch the correct values and dc_pipe_index in the pipe_ctx to populate them
		 * at the right locations.
		 */
		populate_pipe_ctx_dlg_params_from_dml(&context->res_ctx.pipe_ctx[dc_pipe_ctx_index], &context->bw_ctx.dml2->v20.dml_core_ctx, dml_pipe_idx);

		if (context->res_ctx.pipe_ctx[dc_pipe_ctx_index].stream->mall_stream_config.type == SUBVP_PHANTOM) {
			// Phantom pipe requires that DET_SIZE = 0 and no unbounded requests
			context->res_ctx.pipe_ctx[dc_pipe_ctx_index].det_buffer_size_kb = 0;
			context->res_ctx.pipe_ctx[dc_pipe_ctx_index].unbounded_req = false;
		} else {
			context->res_ctx.pipe_ctx[dc_pipe_ctx_index].det_buffer_size_kb = dml_get_det_buffer_size_kbytes(&context->bw_ctx.dml2->v20.dml_core_ctx, dml_pipe_idx);
			context->res_ctx.pipe_ctx[dc_pipe_ctx_index].unbounded_req = unbounded_req_enabled;
		}

		context->bw_ctx.bw.dcn.compbuf_size_kb -= context->res_ctx.pipe_ctx[dc_pipe_ctx_index].det_buffer_size_kb;
		context->res_ctx.pipe_ctx[dc_pipe_ctx_index].plane_res.bw.dppclk_khz = dml_get_dppclk_calculated(&context->bw_ctx.dml2->v20.dml_core_ctx, dml_pipe_idx) * 1000;
		if (context->bw_ctx.bw.dcn.clk.dppclk_khz < context->res_ctx.pipe_ctx[dc_pipe_ctx_index].plane_res.bw.dppclk_khz)
			context->bw_ctx.bw.dcn.clk.dppclk_khz = context->res_ctx.pipe_ctx[dc_pipe_ctx_index].plane_res.bw.dppclk_khz;

		dml_rq_dlg_get_rq_reg(&s->rq_regs, &in_ctx->v20.dml_core_ctx, dml_pipe_idx);
		dml_rq_dlg_get_dlg_reg(&s->disp_dlg_regs, &s->disp_ttu_regs, &in_ctx->v20.dml_core_ctx, dml_pipe_idx);
		dml2_update_pipe_ctx_dchub_regs(&s->rq_regs, &s->disp_dlg_regs, &s->disp_ttu_regs, &out_new_hw_state->pipe_ctx[dc_pipe_ctx_index]);

		context->res_ctx.pipe_ctx[dc_pipe_ctx_index].surface_size_in_mall_bytes = dml_get_surface_size_for_mall(&context->bw_ctx.dml2->v20.dml_core_ctx, dml_pipe_idx);

		/* Reuse MALL Allocation Sizes logic from dcn32_fpu.c */
		/* Count from active, top pipes per plane only. Only add mall_ss_size_bytes for each unique plane. */
		if (context->res_ctx.pipe_ctx[dc_pipe_ctx_index].stream && context->res_ctx.pipe_ctx[dc_pipe_ctx_index].plane_state &&
			(context->res_ctx.pipe_ctx[dc_pipe_ctx_index].top_pipe == NULL ||
			context->res_ctx.pipe_ctx[dc_pipe_ctx_index].plane_state != context->res_ctx.pipe_ctx[dc_pipe_ctx_index].top_pipe->plane_state) &&
			context->res_ctx.pipe_ctx[dc_pipe_ctx_index].prev_odm_pipe == NULL) {
			/* SS: all active surfaces stored in MALL */
			if (context->res_ctx.pipe_ctx[dc_pipe_ctx_index].stream->mall_stream_config.type != SUBVP_PHANTOM) {
				context->bw_ctx.bw.dcn.mall_ss_size_bytes += context->res_ctx.pipe_ctx[dc_pipe_ctx_index].surface_size_in_mall_bytes;
			} else {
				/* SUBVP: phantom surfaces only stored in MALL */
				context->bw_ctx.bw.dcn.mall_subvp_size_bytes += context->res_ctx.pipe_ctx[dc_pipe_ctx_index].surface_size_in_mall_bytes;
			}
		}
	}

	context->bw_ctx.bw.dcn.clk.bw_dppclk_khz = context->bw_ctx.bw.dcn.clk.dppclk_khz;
	context->bw_ctx.bw.dcn.clk.bw_dispclk_khz = context->bw_ctx.bw.dcn.clk.dispclk_khz;
	context->bw_ctx.bw.dcn.clk.max_supported_dppclk_khz = in_ctx->v20.dml_core_ctx.states.state_array[in_ctx->v20.scratch.mode_support_params.out_lowest_state_idx].dppclk_mhz
		* 1000;
	context->bw_ctx.bw.dcn.clk.max_supported_dispclk_khz = in_ctx->v20.dml_core_ctx.states.state_array[in_ctx->v20.scratch.mode_support_params.out_lowest_state_idx].dispclk_mhz
		* 1000;
}

void dml2_extract_watermark_set(struct dcn_watermarks *watermark, struct display_mode_lib_st *dml_core_ctx)
{
	watermark->urgent_ns = dml_get_wm_urgent(dml_core_ctx) * 1000;
	watermark->cstate_pstate.cstate_enter_plus_exit_ns = dml_get_wm_stutter_enter_exit(dml_core_ctx) * 1000;
	watermark->cstate_pstate.cstate_exit_ns = dml_get_wm_stutter_exit(dml_core_ctx) * 1000;
	watermark->cstate_pstate.pstate_change_ns = dml_get_wm_dram_clock_change(dml_core_ctx) * 1000;
	watermark->pte_meta_urgent_ns = dml_get_wm_memory_trip(dml_core_ctx) * 1000;
	watermark->frac_urg_bw_nom = dml_get_fraction_of_urgent_bandwidth(dml_core_ctx) * 1000;
	watermark->frac_urg_bw_flip = dml_get_fraction_of_urgent_bandwidth_imm_flip(dml_core_ctx) * 1000;
	watermark->urgent_latency_ns = dml_get_urgent_latency(dml_core_ctx) * 1000;
	watermark->cstate_pstate.fclk_pstate_change_ns = dml_get_wm_fclk_change(dml_core_ctx) * 1000;
	watermark->usr_retraining_ns = dml_get_wm_usr_retraining(dml_core_ctx) * 1000;
	watermark->cstate_pstate.cstate_enter_plus_exit_z8_ns = dml_get_wm_z8_stutter_enter_exit(dml_core_ctx) * 1000;
	watermark->cstate_pstate.cstate_exit_z8_ns = dml_get_wm_z8_stutter(dml_core_ctx) * 1000;
}

void dml2_initialize_det_scratch(struct dml2_context *in_ctx)
{
	int i;

	for (i = 0; i < MAX_PLANES; i++) {
		in_ctx->det_helper_scratch.dpps_per_surface[i] = 1;
	}
}

static unsigned int find_planes_per_stream_and_stream_count(struct dml2_context *in_ctx, struct dml_display_cfg_st *dml_dispcfg, int *num_of_planes_per_stream)
{
	unsigned int plane_index, stream_index = 0, num_of_streams;

	for (plane_index = 0; plane_index < dml_dispcfg->num_surfaces; plane_index++) {
		/* Number of planes per stream */
		num_of_planes_per_stream[stream_index] += 1;

		if (plane_index + 1 < dml_dispcfg->num_surfaces && dml_dispcfg->plane.BlendingAndTiming[plane_index] != dml_dispcfg->plane.BlendingAndTiming[plane_index + 1])
			stream_index++;
	}

	num_of_streams = stream_index + 1;

	return num_of_streams;
}

void dml2_apply_det_buffer_allocation_policy(struct dml2_context *in_ctx, struct dml_display_cfg_st *dml_dispcfg)
{
	unsigned int num_of_streams = 0, plane_index = 0, max_det_size, stream_index = 0;
	int num_of_planes_per_stream[__DML_NUM_PLANES__] = { 0 };

	max_det_size = in_ctx->config.det_segment_size * in_ctx->config.max_segments_per_hubp;

	num_of_streams = find_planes_per_stream_and_stream_count(in_ctx, dml_dispcfg, num_of_planes_per_stream);

	for (plane_index = 0; plane_index < dml_dispcfg->num_surfaces; plane_index++) {

		if (in_ctx->config.override_det_buffer_size_kbytes)
			dml_dispcfg->plane.DETSizeOverride[plane_index] = max_det_size / in_ctx->config.dcn_pipe_count;
		else {
			dml_dispcfg->plane.DETSizeOverride[plane_index] = ((max_det_size / num_of_streams) / num_of_planes_per_stream[stream_index] / in_ctx->det_helper_scratch.dpps_per_surface[plane_index]);

			/* If the override size is not divisible by det_segment_size then round off to nearest number divisible by det_segment_size as
				* this is a requirement.
				*/
			if (dml_dispcfg->plane.DETSizeOverride[plane_index] % in_ctx->config.det_segment_size != 0) {
				dml_dispcfg->plane.DETSizeOverride[plane_index] = dml_dispcfg->plane.DETSizeOverride[plane_index] & ~0x3F;
			}

			if (plane_index + 1 < dml_dispcfg->num_surfaces && dml_dispcfg->plane.BlendingAndTiming[plane_index] != dml_dispcfg->plane.BlendingAndTiming[plane_index + 1])
				stream_index++;
		}
	}
}

bool dml2_verify_det_buffer_configuration(struct dml2_context *in_ctx, struct dc_state *display_state, struct dml2_helper_det_policy_scratch *det_scratch)
{
	unsigned int i = 0, dml_pipe_idx = 0, plane_id = 0;
	unsigned int max_det_size, total_det_allocated = 0;
	bool need_recalculation = false;

	max_det_size = in_ctx->config.det_segment_size * in_ctx->config.max_segments_per_hubp;

	for (i = 0; i < MAX_PIPES; i++) {
		if (!display_state->res_ctx.pipe_ctx[i].stream)
			continue;
		if (get_plane_id(in_ctx, display_state, display_state->res_ctx.pipe_ctx[i].plane_state,
			display_state->res_ctx.pipe_ctx[i].stream->stream_id,
			in_ctx->v20.scratch.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_index[display_state->res_ctx.pipe_ctx[i].pipe_idx], &plane_id))
			dml_pipe_idx = find_dml_pipe_idx_by_plane_id(in_ctx, plane_id);
		else
			dml_pipe_idx = dml2_helper_find_dml_pipe_idx_by_stream_id(in_ctx, display_state->res_ctx.pipe_ctx[i].stream->stream_id);
		total_det_allocated += dml_get_det_buffer_size_kbytes(&in_ctx->v20.dml_core_ctx, dml_pipe_idx);
		if (total_det_allocated > max_det_size) {
			need_recalculation = true;
		}
	}

	/* Store the DPPPerSurface for correctly determining the number of planes in the next call. */
	for (i = 0; i < MAX_PLANES; i++) {
		det_scratch->dpps_per_surface[i] = in_ctx->v20.scratch.cur_display_config.hw.DPPPerSurface[i];
	}

	return need_recalculation;
}

bool dml2_is_stereo_timing(const struct dc_stream_state *stream)
{
	bool is_stereo = false;

	if ((stream->view_format ==
			VIEW_3D_FORMAT_SIDE_BY_SIDE ||
			stream->view_format ==
			VIEW_3D_FORMAT_TOP_AND_BOTTOM) &&
			(stream->timing.timing_3d_format ==
			TIMING_3D_FORMAT_TOP_AND_BOTTOM ||
			stream->timing.timing_3d_format ==
			TIMING_3D_FORMAT_SIDE_BY_SIDE))
		is_stereo = true;

	return is_stereo;
}
