/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dcn30_hwseq.h"
#include "dccg.h"
#include "dce/dce_hwseq.h"
#include "dcn30/dcn30_mpc.h"
#include "dcn30/dcn30_dpp.h"
#include "dcn10/dcn10_cm_common.h"
#include "dcn30/dcn30_cm_common.h"
#include "reg_helper.h"
#include "abm.h"
#include "clk_mgr.h"
#include "hubp.h"
#include "dchubbub.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "mcif_wb.h"
#include "dc_dmub_srv.h"
#include "link_hwss.h"
#include "dpcd_defs.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn30/dcn30_resource.h"
#include "link_service.h"
#include "dc_state_priv.h"



#define DC_LOGGER_INIT(logger)

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg
#define DC_LOGGER \
		dc->ctx->logger


#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

void dcn30_log_color_state(struct dc *dc,
			   struct dc_log_buffer_ctx *log_ctx)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct resource_pool *pool = dc->res_pool;
	bool is_gamut_remap_available = false;
	int i;

	DTN_INFO("DPP:  DGAM ROM  DGAM ROM type  DGAM LUT  SHAPER mode"
		 "  3DLUT mode  3DLUT bit depth  3DLUT size  RGAM mode"
		 "  GAMUT adjust  "
		 "C11        C12        C13        C14        "
		 "C21        C22        C23        C24        "
		 "C31        C32        C33        C34        \n");

	for (i = 0; i < pool->pipe_count; i++) {
		struct dpp *dpp = pool->dpps[i];
		struct dcn_dpp_state s = {0};

		dpp->funcs->dpp_read_state(dpp, &s);

		if (dpp->funcs->dpp_get_gamut_remap) {
			dpp->funcs->dpp_get_gamut_remap(dpp, &s.gamut_remap);
			is_gamut_remap_available = true;
		}

		if (!s.is_enabled)
			continue;

		DTN_INFO("[%2d]:  %7x  %13s  %8s  %11s  %10s  %15s  %10s  %9s",
			dpp->inst,
			s.pre_dgam_mode,
			(s.pre_dgam_select == 0) ? "sRGB" :
			 ((s.pre_dgam_select == 1) ? "Gamma 2.2" :
			 ((s.pre_dgam_select == 2) ? "Gamma 2.4" :
			 ((s.pre_dgam_select == 3) ? "Gamma 2.6" :
			 ((s.pre_dgam_select == 4) ? "BT.709" :
			 ((s.pre_dgam_select == 5) ? "PQ" :
			 ((s.pre_dgam_select == 6) ? "HLG" :
						     "Unknown")))))),
			(s.gamcor_mode == 0) ? "Bypass" :
			 ((s.gamcor_mode == 1) ? "RAM A" :
						 "RAM B"),
			(s.shaper_lut_mode == 1) ? "RAM A" :
			 ((s.shaper_lut_mode == 2) ? "RAM B" :
						     "Bypass"),
			(s.lut3d_mode == 1) ? "RAM A" :
			 ((s.lut3d_mode == 2) ? "RAM B" :
						"Bypass"),
			(s.lut3d_bit_depth <= 0) ? "12-bit" : "10-bit",
			(s.lut3d_size == 0) ? "17x17x17" : "9x9x9",
			(s.rgam_lut_mode == 0) ? "Bypass" :
			 ((s.rgam_lut_mode == 1) ? "RAM A" :
						   "RAM B"));

		if (is_gamut_remap_available) {
			DTN_INFO("  %12s  "
				 "%010lld %010lld %010lld %010lld "
				 "%010lld %010lld %010lld %010lld "
				 "%010lld %010lld %010lld %010lld",

			(s.gamut_remap.gamut_adjust_type == 0) ? "Bypass" :
				((s.gamut_remap.gamut_adjust_type == 1) ? "HW" :
									  "SW"),
			s.gamut_remap.temperature_matrix[0].value,
			s.gamut_remap.temperature_matrix[1].value,
			s.gamut_remap.temperature_matrix[2].value,
			s.gamut_remap.temperature_matrix[3].value,
			s.gamut_remap.temperature_matrix[4].value,
			s.gamut_remap.temperature_matrix[5].value,
			s.gamut_remap.temperature_matrix[6].value,
			s.gamut_remap.temperature_matrix[7].value,
			s.gamut_remap.temperature_matrix[8].value,
			s.gamut_remap.temperature_matrix[9].value,
			s.gamut_remap.temperature_matrix[10].value,
			s.gamut_remap.temperature_matrix[11].value);
		}

		DTN_INFO("\n");
	}
	DTN_INFO("\n");
	DTN_INFO("DPP Color Caps: input_lut_shared:%d  icsc:%d"
		 "  dgam_ram:%d  dgam_rom: srgb:%d,bt2020:%d,gamma2_2:%d,pq:%d,hlg:%d"
		 "  post_csc:%d  gamcor:%d  dgam_rom_for_yuv:%d  3d_lut:%d"
		 "  blnd_lut:%d  oscs:%d\n\n",
		 dc->caps.color.dpp.input_lut_shared,
		 dc->caps.color.dpp.icsc,
		 dc->caps.color.dpp.dgam_ram,
		 dc->caps.color.dpp.dgam_rom_caps.srgb,
		 dc->caps.color.dpp.dgam_rom_caps.bt2020,
		 dc->caps.color.dpp.dgam_rom_caps.gamma2_2,
		 dc->caps.color.dpp.dgam_rom_caps.pq,
		 dc->caps.color.dpp.dgam_rom_caps.hlg,
		 dc->caps.color.dpp.post_csc,
		 dc->caps.color.dpp.gamma_corr,
		 dc->caps.color.dpp.dgam_rom_for_yuv,
		 dc->caps.color.dpp.hw_3d_lut,
		 dc->caps.color.dpp.ogam_ram,
		 dc->caps.color.dpp.ocsc);

	DTN_INFO("MPCC:  OPP  DPP  MPCCBOT  MODE  ALPHA_MODE  PREMULT  OVERLAP_ONLY  IDLE"
		 "  SHAPER mode  3DLUT mode  3DLUT bit-depth  3DLUT size  OGAM mode  OGAM LUT"
		 "  GAMUT adjust  "
		 "C11        C12        C13        C14        "
		 "C21        C22        C23        C24        "
		 "C31        C32        C33        C34        \n");

	for (i = 0; i < pool->mpcc_count; i++) {
		struct mpcc_state s = {0};

		pool->mpc->funcs->read_mpcc_state(pool->mpc, i, &s);
		mpc3_get_gamut_remap(pool->mpc, i,  &s.gamut_remap);

		if (s.opp_id != 0xf)
			DTN_INFO("[%2d]:  %2xh  %2xh  %6xh  %4d  %10d  %7d  %12d  %4d  %11s %11s %16s %11s %10s %9s"
				 "  %-12s  "
				 "%010lld %010lld %010lld %010lld "
				 "%010lld %010lld %010lld %010lld "
				 "%010lld %010lld %010lld %010lld\n",
				i, s.opp_id, s.dpp_id, s.bot_mpcc_id,
				s.mode, s.alpha_mode, s.pre_multiplied_alpha, s.overlap_only,
				s.idle,
				(s.shaper_lut_mode == 1) ? "RAM A" :
				 ((s.shaper_lut_mode == 2) ? "RAM B" :
							     "Bypass"),
				(s.lut3d_mode == 1) ? "RAM A" :
				 ((s.lut3d_mode == 2) ? "RAM B" :
							"Bypass"),
				(s.lut3d_bit_depth <= 0) ? "12-bit" : "10-bit",
				(s.lut3d_size == 0) ? "17x17x17" : "9x9x9",
				(s.rgam_mode == 0) ? "Bypass" :
				 ((s.rgam_mode == 2) ? "RAM" :
						       "Unknown"),
				(s.rgam_mode == 1) ? "B" : "A",
				(s.gamut_remap.gamut_adjust_type == 0) ? "Bypass" :
					((s.gamut_remap.gamut_adjust_type == 1) ? "HW" :
										  "SW"),
				s.gamut_remap.temperature_matrix[0].value,
				s.gamut_remap.temperature_matrix[1].value,
				s.gamut_remap.temperature_matrix[2].value,
				s.gamut_remap.temperature_matrix[3].value,
				s.gamut_remap.temperature_matrix[4].value,
				s.gamut_remap.temperature_matrix[5].value,
				s.gamut_remap.temperature_matrix[6].value,
				s.gamut_remap.temperature_matrix[7].value,
				s.gamut_remap.temperature_matrix[8].value,
				s.gamut_remap.temperature_matrix[9].value,
				s.gamut_remap.temperature_matrix[10].value,
				s.gamut_remap.temperature_matrix[11].value);

	}
	DTN_INFO("\n");
	DTN_INFO("MPC Color Caps: gamut_remap:%d, 3dlut:%d, ogam_ram:%d, ocsc:%d\n\n",
		 dc->caps.color.mpc.gamut_remap,
		 dc->caps.color.mpc.num_3dluts,
		 dc->caps.color.mpc.ogam_ram,
		 dc->caps.color.mpc.ocsc);
}

bool dcn30_set_blend_lut(
	struct pipe_ctx *pipe_ctx, const struct dc_plane_state *plane_state)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	bool result = true;
	const struct pwl_params *blend_lut = NULL;

	if (plane_state->blend_tf.type == TF_TYPE_HWPWL)
		blend_lut = &plane_state->blend_tf.pwl;
	else if (plane_state->blend_tf.type == TF_TYPE_DISTRIBUTED_POINTS) {
		result = cm3_helper_translate_curve_to_hw_format(
				&plane_state->blend_tf, &dpp_base->regamma_params, false);
		if (!result)
			return result;

		blend_lut = &dpp_base->regamma_params;
	}
	result = dpp_base->funcs->dpp_program_blnd_lut(dpp_base, blend_lut);

	return result;
}

static bool dcn30_set_mpc_shaper_3dlut(struct pipe_ctx *pipe_ctx,
				       const struct dc_stream_state *stream)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;
	bool result = false;
	int acquired_rmu = 0;
	int mpcc_id_projected = 0;

	const struct pwl_params *shaper_lut = NULL;
	//get the shaper lut params
	if (stream->func_shaper) {
		if (stream->func_shaper->type == TF_TYPE_HWPWL) {
			shaper_lut = &stream->func_shaper->pwl;
		} else if (stream->func_shaper->type == TF_TYPE_DISTRIBUTED_POINTS) {
			cm_helper_translate_curve_to_hw_format(stream->ctx, stream->func_shaper,
							       &dpp_base->shaper_params, true);
			shaper_lut = &dpp_base->shaper_params;
		}
	}

	if (stream->lut3d_func &&
	    stream->lut3d_func->state.bits.initialized == 1 &&
	    stream->lut3d_func->state.bits.rmu_idx_valid == 1) {
		if (stream->lut3d_func->state.bits.rmu_mux_num == 0)
			mpcc_id_projected = stream->lut3d_func->state.bits.mpc_rmu0_mux;
		else if (stream->lut3d_func->state.bits.rmu_mux_num == 1)
			mpcc_id_projected = stream->lut3d_func->state.bits.mpc_rmu1_mux;
		else if (stream->lut3d_func->state.bits.rmu_mux_num == 2)
			mpcc_id_projected = stream->lut3d_func->state.bits.mpc_rmu2_mux;
		if (mpcc_id_projected != mpcc_id)
			BREAK_TO_DEBUGGER();
		/* find the reason why logical layer assigned a different
		 * mpcc_id into acquire_post_bldn_3dlut
		 */
		acquired_rmu = mpc->funcs->acquire_rmu(mpc, mpcc_id,
						       stream->lut3d_func->state.bits.rmu_mux_num);
		if (acquired_rmu != stream->lut3d_func->state.bits.rmu_mux_num)
			BREAK_TO_DEBUGGER();

		result = mpc->funcs->program_3dlut(mpc, &stream->lut3d_func->lut_3d,
						   stream->lut3d_func->state.bits.rmu_mux_num);
		if (!result)
			DC_LOG_ERROR("%s: program_3dlut failed\n", __func__);

		result = mpc->funcs->program_shaper(mpc, shaper_lut,
						    stream->lut3d_func->state.bits.rmu_mux_num);
		if (!result)
			DC_LOG_ERROR("%s: program_shaper failed\n", __func__);

	} else {
		// loop through the available mux and release the requested mpcc_id
		mpc->funcs->release_rmu(mpc, mpcc_id);
	}

	return result;
}

bool dcn30_set_input_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	enum dc_transfer_func_predefined tf;
	bool result = true;
	const struct pwl_params *params = NULL;

	if (dpp_base == NULL || plane_state == NULL)
		return false;

	tf = TRANSFER_FUNCTION_UNITY;

	if (plane_state->in_transfer_func.type == TF_TYPE_PREDEFINED)
		tf = plane_state->in_transfer_func.tf;

	dpp_base->funcs->dpp_set_pre_degam(dpp_base, tf);

	if (plane_state->in_transfer_func.type == TF_TYPE_HWPWL)
		params = &plane_state->in_transfer_func.pwl;
	else if (plane_state->in_transfer_func.type == TF_TYPE_DISTRIBUTED_POINTS &&
		cm3_helper_translate_curve_to_hw_format(&plane_state->in_transfer_func,
				&dpp_base->degamma_params, false))
		params = &dpp_base->degamma_params;

	result = dpp_base->funcs->dpp_program_gamcor_lut(dpp_base, params);

	if (pipe_ctx->stream_res.opp && pipe_ctx->stream_res.opp->ctx) {
		if (dpp_base->funcs->dpp_program_blnd_lut)
			hws->funcs.set_blend_lut(pipe_ctx, plane_state);
		if (dpp_base->funcs->dpp_program_shaper_lut &&
				dpp_base->funcs->dpp_program_3dlut)
			hws->funcs.set_shaper_3dlut(pipe_ctx, plane_state);
	}

	return result;
}

void dcn30_program_gamut_remap(struct pipe_ctx *pipe_ctx)
{
	int i = 0;
	struct dpp_grph_csc_adjustment dpp_adjust;
	struct mpc_grph_gamut_adjustment mpc_adjust;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;

	memset(&dpp_adjust, 0, sizeof(dpp_adjust));
	dpp_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;

	if (pipe_ctx->plane_state &&
	    pipe_ctx->plane_state->gamut_remap_matrix.enable_remap == true) {
		dpp_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
			dpp_adjust.temperature_matrix[i] =
				pipe_ctx->plane_state->gamut_remap_matrix.matrix[i];
	}

	pipe_ctx->plane_res.dpp->funcs->dpp_set_gamut_remap(pipe_ctx->plane_res.dpp,
							    &dpp_adjust);

	memset(&mpc_adjust, 0, sizeof(mpc_adjust));
	mpc_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;

	if (pipe_ctx->top_pipe == NULL) {
		if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
			mpc_adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
			for (i = 0; i < CSC_TEMPERATURE_MATRIX_SIZE; i++)
				mpc_adjust.temperature_matrix[i] =
					pipe_ctx->stream->gamut_remap_matrix.matrix[i];
		}
	}

	mpc->funcs->set_gamut_remap(mpc, mpcc_id, &mpc_adjust);
}

bool dcn30_set_output_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream)
{
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;
	const struct pwl_params *params = NULL;
	bool ret = false;

	/* program OGAM or 3DLUT only for the top pipe*/
	if (pipe_ctx->top_pipe == NULL) {
		/*program rmu shaper and 3dlut in MPC*/
		ret = dcn30_set_mpc_shaper_3dlut(pipe_ctx, stream);
		if (ret == false && mpc->funcs->set_output_gamma) {
			if (stream->out_transfer_func.type == TF_TYPE_HWPWL)
				params = &stream->out_transfer_func.pwl;
			else if (pipe_ctx->stream->out_transfer_func.type ==
					TF_TYPE_DISTRIBUTED_POINTS &&
					cm3_helper_translate_curve_to_hw_format(
					&stream->out_transfer_func,
					&mpc->blender_params, false))
				params = &mpc->blender_params;
			 /* there are no ROM LUTs in OUTGAM */
			if (stream->out_transfer_func.type == TF_TYPE_PREDEFINED)
				BREAK_TO_DEBUGGER();
		}
	}

	if (mpc->funcs->set_output_gamma)
		mpc->funcs->set_output_gamma(mpc, mpcc_id, params);
	else
		DC_LOG_ERROR("%s: set_output_gamma function pointer is NULL.\n", __func__);

	return ret;
}

static void dcn30_set_writeback(
		struct dc *dc,
		struct dc_writeback_info *wb_info,
		struct dc_state *context)
{
	struct mcif_wb *mcif_wb;
	struct mcif_buf_params *mcif_buf_params;

	ASSERT(wb_info->dwb_pipe_inst < MAX_DWB_PIPES);
	ASSERT(wb_info->wb_enabled);
	ASSERT(wb_info->mpcc_inst >= 0);
	ASSERT(wb_info->mpcc_inst < dc->res_pool->mpcc_count);
	mcif_wb = dc->res_pool->mcif_wb[wb_info->dwb_pipe_inst];
	mcif_buf_params = &wb_info->mcif_buf_params;

	/* set DWB MPC mux */
	dc->res_pool->mpc->funcs->set_dwb_mux(dc->res_pool->mpc,
			wb_info->dwb_pipe_inst, wb_info->mpcc_inst);
	/* set MCIF_WB buffer and arbitration configuration */
	mcif_wb->funcs->config_mcif_buf(mcif_wb, mcif_buf_params, wb_info->dwb_params.dest_height);
	mcif_wb->funcs->config_mcif_arb(mcif_wb, &context->bw_ctx.bw.dcn.bw_writeback.mcif_wb_arb[wb_info->dwb_pipe_inst]);
}

void dcn30_update_writeback(
		struct dc *dc,
		struct dc_writeback_info *wb_info,
		struct dc_state *context)
{
	struct dwbc *dwb;
	dwb = dc->res_pool->dwbc[wb_info->dwb_pipe_inst];
	DC_LOG_DWB("%s dwb_pipe_inst = %d, mpcc_inst = %d",\
		__func__, wb_info->dwb_pipe_inst,\
		wb_info->mpcc_inst);

	dcn30_set_writeback(dc, wb_info, context);

	/* update DWB */
	dwb->funcs->update(dwb, &wb_info->dwb_params);
}

bool dcn30_mmhubbub_warmup(
	struct dc *dc,
	unsigned int num_dwb,
	struct dc_writeback_info *wb_info)
{
	struct dwbc *dwb;
	struct mcif_wb *mcif_wb;
	struct mcif_warmup_params warmup_params = {0};
	unsigned int  i, i_buf;
	/* make sure there is no active DWB enabled */
	for (i = 0; i < num_dwb; i++) {
		dwb = dc->res_pool->dwbc[wb_info[i].dwb_pipe_inst];
		if (dwb->dwb_is_efc_transition || dwb->dwb_is_drc) {
			/*can not do warmup while any dwb enabled*/
			return false;
		}
	}

	if (wb_info->mcif_warmup_params.p_vmid == 0)
		return false;

	/*check whether this is new interface: warmup big buffer once*/
	if (wb_info->mcif_warmup_params.start_address.quad_part != 0 &&
		wb_info->mcif_warmup_params.region_size != 0) {
		/*mmhubbub is shared, so it does not matter which MCIF*/
		mcif_wb = dc->res_pool->mcif_wb[0];
		/*warmup a big chunk of VM buffer at once*/
		warmup_params.start_address.quad_part = wb_info->mcif_warmup_params.start_address.quad_part;
		warmup_params.address_increment =  wb_info->mcif_warmup_params.region_size;
		warmup_params.region_size = wb_info->mcif_warmup_params.region_size;
		warmup_params.p_vmid = wb_info->mcif_warmup_params.p_vmid;

		if (warmup_params.address_increment == 0)
			warmup_params.address_increment = dc->dml.soc.vmm_page_size_bytes;

		mcif_wb->funcs->warmup_mcif(mcif_wb, &warmup_params);
		return true;
	}
	/*following is the original: warmup each DWB's mcif buffer*/
	for (i = 0; i < num_dwb; i++) {
		mcif_wb = dc->res_pool->mcif_wb[wb_info[i].dwb_pipe_inst];
		/*warmup is for VM mode only*/
		if (wb_info[i].mcif_buf_params.p_vmid == 0)
			return false;

		/* Warmup MCIF_WB */
		for (i_buf = 0; i_buf < MCIF_BUF_COUNT; i_buf++) {
			warmup_params.start_address.quad_part = wb_info[i].mcif_buf_params.luma_address[i_buf];
			warmup_params.address_increment = dc->dml.soc.vmm_page_size_bytes;
			warmup_params.region_size = wb_info[i].mcif_buf_params.luma_pitch * wb_info[i].dwb_params.dest_height;
			warmup_params.p_vmid = wb_info[i].mcif_buf_params.p_vmid;
			mcif_wb->funcs->warmup_mcif(mcif_wb, &warmup_params);
		}
	}
	return true;
}

void dcn30_enable_writeback(
		struct dc *dc,
		struct dc_writeback_info *wb_info,
		struct dc_state *context)
{
	struct dwbc *dwb;
	struct mcif_wb *mcif_wb;

	dwb = dc->res_pool->dwbc[wb_info->dwb_pipe_inst];
	mcif_wb = dc->res_pool->mcif_wb[wb_info->dwb_pipe_inst];

	DC_LOG_DWB("%s dwb_pipe_inst = %d, mpcc_inst = %d",\
		__func__, wb_info->dwb_pipe_inst,\
		wb_info->mpcc_inst);

	/* Warmup interface */
	dcn30_mmhubbub_warmup(dc, 1, wb_info);

	/* Update writeback pipe */
	dcn30_set_writeback(dc, wb_info, context);

	/* Enable MCIF_WB */
	mcif_wb->funcs->enable_mcif(mcif_wb);
	/* Enable DWB */
	dwb->funcs->enable(dwb, &wb_info->dwb_params);
}

void dcn30_disable_writeback(
		struct dc *dc,
		unsigned int dwb_pipe_inst)
{
	struct dwbc *dwb;
	struct mcif_wb *mcif_wb;

	ASSERT(dwb_pipe_inst < MAX_DWB_PIPES);
	dwb = dc->res_pool->dwbc[dwb_pipe_inst];
	mcif_wb = dc->res_pool->mcif_wb[dwb_pipe_inst];
	DC_LOG_DWB("%s dwb_pipe_inst = %d",\
		__func__, dwb_pipe_inst);

	/* disable DWB */
	dwb->funcs->disable(dwb);
	/* disable MCIF */
	mcif_wb->funcs->disable_mcif(mcif_wb);
	/* disable MPC DWB mux */
	dc->res_pool->mpc->funcs->disable_dwb_mux(dc->res_pool->mpc, dwb_pipe_inst);
}

void dcn30_program_all_writeback_pipes_in_tree(
		struct dc *dc,
		const struct dc_stream_state *stream,
		struct dc_state *context)
{
	struct dc_writeback_info wb_info;
	struct dwbc *dwb;
	struct dc_stream_status *stream_status = NULL;
	int i_wb, i_pipe, i_stream;
	DC_LOG_DWB("%s", __func__);

	ASSERT(stream);
	for (i_stream = 0; i_stream < context->stream_count; i_stream++) {
		if (context->streams[i_stream] == stream) {
			stream_status = &context->stream_status[i_stream];
			break;
		}
	}
	ASSERT(stream_status);

	ASSERT(stream->num_wb_info <= dc->res_pool->res_cap->num_dwb);
	/* For each writeback pipe */
	for (i_wb = 0; i_wb < stream->num_wb_info; i_wb++) {

		/* copy writeback info to local non-const so mpcc_inst can be set */
		wb_info = stream->writeback_info[i_wb];
		if (wb_info.wb_enabled) {

			/* get the MPCC instance for writeback_source_plane */
			wb_info.mpcc_inst = -1;
			for (i_pipe = 0; i_pipe < dc->res_pool->pipe_count; i_pipe++) {
				struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i_pipe];

				if (!pipe_ctx->plane_state)
					continue;

				if (pipe_ctx->plane_state == wb_info.writeback_source_plane) {
					wb_info.mpcc_inst = pipe_ctx->plane_res.mpcc_inst;
					break;
				}
			}

			if (wb_info.mpcc_inst == -1) {
				/* Disable writeback pipe and disconnect from MPCC
				 * if source plane has been removed
				 */
				dc->hwss.disable_writeback(dc, wb_info.dwb_pipe_inst);
				continue;
			}

			ASSERT(wb_info.dwb_pipe_inst < dc->res_pool->res_cap->num_dwb);
			dwb = dc->res_pool->dwbc[wb_info.dwb_pipe_inst];
			if (dwb->funcs->is_enabled(dwb)) {
				/* writeback pipe already enabled, only need to update */
				dc->hwss.update_writeback(dc, &wb_info, context);
			} else {
				/* Enable writeback pipe and connect to MPCC */
				dc->hwss.enable_writeback(dc, &wb_info, context);
			}
		} else {
			/* Disable writeback pipe and disconnect from MPCC */
			dc->hwss.disable_writeback(dc, wb_info.dwb_pipe_inst);
		}
	}
}

void dcn30_init_hw(struct dc *dc)
{
	struct abm **abms = dc->res_pool->multiple_abms;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	int i;
	int edp_num;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;
	uint32_t user_level = MAX_BACKLIGHT_LEVEL;

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->init_clocks)
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		hws->funcs.bios_golden_init(dc);
		hws->funcs.disable_vga(dc->hwseq);
	}

	if (dc->debug.enable_mem_low_power.bits.dmcu) {
		// Force ERAM to shutdown if DMCU is not enabled
		if (dc->debug.disable_dmcu || dc->config.disable_dmcu) {
			REG_UPDATE(DMU_MEM_PWR_CNTL, DMCU_ERAM_MEM_PWR_FORCE, 3);
		}
	}

	// Set default OPTC memory power states
	if (dc->debug.enable_mem_low_power.bits.optc) {
		// Shutdown when unassigned and light sleep in VBLANK
		REG_SET_2(ODM_MEM_PWR_CTRL3, 0, ODM_MEM_UNASSIGNED_PWR_MODE, 3, ODM_MEM_VBLANK_PWR_MODE, 1);
	}

	if (dc->debug.enable_mem_low_power.bits.vga) {
		// Power down VGA memory
		REG_UPDATE(MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, 1);
	}

	if (dc->ctx->dc_bios->fw_info_valid) {
		res_pool->ref_clocks.xtalin_clock_inKhz =
				dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency;

		if (res_pool->hubbub) {

			(res_pool->dccg->funcs->get_dccg_ref_freq)(res_pool->dccg,
					dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency,
					&res_pool->ref_clocks.dccg_ref_clock_inKhz);

			(res_pool->hubbub->funcs->get_dchub_ref_freq)(res_pool->hubbub,
					res_pool->ref_clocks.dccg_ref_clock_inKhz,
					&res_pool->ref_clocks.dchub_ref_clock_inKhz);
		} else {
			// Not all ASICs have DCCG sw component
			res_pool->ref_clocks.dccg_ref_clock_inKhz =
					res_pool->ref_clocks.xtalin_clock_inKhz;
			res_pool->ref_clocks.dchub_ref_clock_inKhz =
					res_pool->ref_clocks.xtalin_clock_inKhz;
		}
	} else
		ASSERT_CRITICAL(false);

	for (i = 0; i < dc->link_count; i++) {
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector).
		 */
		struct dc_link *link = dc->links[i];

		link->link_enc->funcs->hw_init(link->link_enc);

		/* Check for enabled DIG to identify enabled display */
		if (link->link_enc->funcs->is_dig_enabled &&
			link->link_enc->funcs->is_dig_enabled(link->link_enc)) {
			link->link_status.link_active = true;
			if (link->link_enc->funcs->fec_is_active &&
					link->link_enc->funcs->fec_is_active(link->link_enc))
				link->fec_state = dc_link_fec_enabled;
		}
	}

	/* we want to turn off all dp displays before doing detection */
	dc->link_srv->blank_all_dp_displays(dc);

	if (hws->funcs.enable_power_gating_plane)
		hws->funcs.enable_power_gating_plane(dc->hwseq, true);

	/* If taking control over from VBIOS, we may want to optimize our first
	 * mode set, so we need to skip powering down pipes until we know which
	 * pipes we want to use.
	 * Otherwise, if taking control is not possible, we need to power
	 * everything down.
	 */
	if (dcb->funcs->is_accelerated_mode(dcb) || !dc->config.seamless_boot_edp_requested) {
		hws->funcs.init_pipes(dc, dc->current_state);
		if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
					!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);
	}

	/* In headless boot cases, DIG may be turned
	 * on which causes HW/SW discrepancies.
	 * To avoid this, power down hardware on boot
	 * if DIG is turned on and seamless boot not enabled
	 */
	if (!dc->config.seamless_boot_edp_requested) {
		struct dc_link *edp_links[MAX_NUM_EDP];
		struct dc_link *edp_link = NULL;

		dc_get_edp_links(dc, edp_links, &edp_num);
		if (edp_num)
			edp_link = edp_links[0];
		if (edp_link && edp_link->link_enc->funcs->is_dig_enabled &&
				edp_link->link_enc->funcs->is_dig_enabled(edp_link->link_enc) &&
				dc->hwss.edp_backlight_control &&
				hws->funcs.power_down &&
				dc->hwss.edp_power_control) {
			dc->hwss.edp_backlight_control(edp_link, false);
			hws->funcs.power_down(dc);
			dc->hwss.edp_power_control(edp_link, false);
		} else {
			for (i = 0; i < dc->link_count; i++) {
				struct dc_link *link = dc->links[i];

				if (link->link_enc->funcs->is_dig_enabled &&
						link->link_enc->funcs->is_dig_enabled(link->link_enc) &&
						hws->funcs.power_down) {
					hws->funcs.power_down(dc);
					break;
				}

			}
		}
	}

	for (i = 0; i < res_pool->audio_count; i++) {
		struct audio *audio = res_pool->audios[i];

		audio->funcs->hw_init(audio);
	}

	for (i = 0; i < dc->link_count; i++) {
		struct dc_link *link = dc->links[i];

		if (link->panel_cntl) {
			backlight = link->panel_cntl->funcs->hw_init(link->panel_cntl);
			user_level = link->panel_cntl->stored_backlight_registers.USER_LEVEL;
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (abms[i] != NULL)
			abms[i]->funcs->abm_init(abms[i], backlight, user_level);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

		REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	if (!dcb->funcs->is_accelerated_mode(dcb) && dc->res_pool->hubbub->funcs->init_watermarks)
		dc->res_pool->hubbub->funcs->init_watermarks(dc->res_pool->hubbub);

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

	//if softmax is enabled then hardmax will be set by a different call
	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->set_hard_max_memclk &&
	    !dc->clk_mgr->dc_mode_softmax_enabled)
		dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);

	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
				dc->res_pool->hubbub, false, false);
	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);

	// Get DMCUB capabilities
	dc_dmub_srv_query_caps_cmd(dc->ctx->dmub_srv);
	dc->caps.dmub_caps.psr = dc->ctx->dmub_srv->dmub->feature_caps.psr;
	dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver;
}

void dcn30_set_avmute(struct pipe_ctx *pipe_ctx, bool enable)
{
	if (pipe_ctx == NULL)
		return;

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal) && pipe_ctx->stream_res.stream_enc != NULL) {
		pipe_ctx->stream_res.stream_enc->funcs->set_avmute(
				pipe_ctx->stream_res.stream_enc,
				enable);

		/* Wait for two frame to make sure AV mute is sent out */
		if (enable && pipe_ctx->stream_res.tg->funcs->is_tg_enabled(pipe_ctx->stream_res.tg)) {
			pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg, CRTC_STATE_VACTIVE);
			pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg, CRTC_STATE_VBLANK);
			pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg, CRTC_STATE_VACTIVE);
			pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg, CRTC_STATE_VBLANK);
			pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg, CRTC_STATE_VACTIVE);
		}
	}
}

void dcn30_update_info_frame(struct pipe_ctx *pipe_ctx)
{
	bool is_hdmi_tmds;
	bool is_dp;

	ASSERT(pipe_ctx->stream);

	if (pipe_ctx->stream_res.stream_enc == NULL)
		return;  /* this is not root pipe */

	is_hdmi_tmds = dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal);
	is_dp = dc_is_dp_signal(pipe_ctx->stream->signal);

	if (!is_hdmi_tmds && !is_dp)
		return;

	if (is_hdmi_tmds)
		pipe_ctx->stream_res.stream_enc->funcs->update_hdmi_info_packets(
			pipe_ctx->stream_res.stream_enc,
			&pipe_ctx->stream_res.encoder_info_frame);
	else {
		if (pipe_ctx->stream_res.stream_enc->funcs->update_dp_info_packets_sdp_line_num)
			pipe_ctx->stream_res.stream_enc->funcs->update_dp_info_packets_sdp_line_num(
				pipe_ctx->stream_res.stream_enc,
				&pipe_ctx->stream_res.encoder_info_frame);

		pipe_ctx->stream_res.stream_enc->funcs->update_dp_info_packets(
			pipe_ctx->stream_res.stream_enc,
			&pipe_ctx->stream_res.encoder_info_frame);
	}
}

void dcn30_program_dmdata_engine(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state    *stream     = pipe_ctx->stream;
	struct hubp               *hubp       = pipe_ctx->plane_res.hubp;
	bool                       enable     = false;
	struct stream_encoder     *stream_enc = pipe_ctx->stream_res.stream_enc;
	enum dynamic_metadata_mode mode       = dc_is_dp_signal(stream->signal)
							? dmdata_dp
							: dmdata_hdmi;

	/* if using dynamic meta, don't set up generic infopackets */
	if (pipe_ctx->stream->dmdata_address.quad_part != 0) {
		pipe_ctx->stream_res.encoder_info_frame.hdrsmd.valid = false;
		enable = true;
	}

	if (!hubp)
		return;

	if (!stream_enc || !stream_enc->funcs->set_dynamic_metadata)
		return;

	stream_enc->funcs->set_dynamic_metadata(stream_enc, enable,
							hubp->inst, mode);
}

bool dcn30_apply_idle_power_optimizations(struct dc *dc, bool enable)
{
	union dmub_rb_cmd cmd;
	uint32_t tmr_delay = 0, tmr_scale = 0;
	struct dc_cursor_attributes cursor_attr = {0};
	bool cursor_cache_enable = false;
	struct dc_stream_state *stream = NULL;
	struct dc_plane_state *plane = NULL;

	if (!dc->ctx->dmub_srv)
		return false;

	if (enable) {
		if (dc->current_state) {
			int i;

			/* First, check no-memory-requests case */
			for (i = 0; i < dc->current_state->stream_count; i++) {
				if (dc->current_state->stream_status[i].plane_count)
					/* Fail eligibility on a visible stream */
					break;
			}

			if (i == dc->current_state->stream_count) {
				/* Enable no-memory-requests case */
				memset(&cmd, 0, sizeof(cmd));
				cmd.mall.header.type = DMUB_CMD__MALL;
				cmd.mall.header.sub_type = DMUB_CMD__MALL_ACTION_NO_DF_REQ;
				cmd.mall.header.payload_bytes = sizeof(cmd.mall) - sizeof(cmd.mall.header);

				dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_NO_WAIT);

				return true;
			}

			stream = dc->current_state->streams[0];
			plane = (stream ? dc->current_state->stream_status[0].plane_states[0] : NULL);

			if (!stream || !plane)
				return false;

			if (stream && plane) {
				cursor_cache_enable = stream->cursor_position.enable &&
						plane->address.grph.cursor_cache_addr.quad_part;
				cursor_attr = stream->cursor_attributes;
			}

			/*
			 * Second, check MALL eligibility
			 *
			 * single display only, single surface only, 8 and 16 bit formats only, no VM,
			 * do not use MALL for displays that support PSR as they use D0i3.2 in DMCUB FW
			 *
			 * TODO: When we implement multi-display, PSR displays will be allowed if there is
			 * a non-PSR display present, since in that case we can't do D0i3.2
			 */
			if (dc->current_state->stream_count == 1 &&
					stream->link->psr_settings.psr_version == DC_PSR_VERSION_UNSUPPORTED &&
					dc->current_state->stream_status[0].plane_count == 1 &&
					plane->format <= SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F &&
					plane->format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB8888 &&
					plane->address.page_table_base.quad_part == 0 &&
					dc->hwss.does_plane_fit_in_mall &&
					dc->hwss.does_plane_fit_in_mall(dc, plane->plane_size.surface_pitch,
							plane->plane_size.surface_size.height, plane->format,
							cursor_cache_enable ? &cursor_attr : NULL)) {
				unsigned int v_total = stream->adjust.v_total_max ?
						stream->adjust.v_total_max : stream->timing.v_total;
				unsigned int refresh_hz = div_u64((unsigned long long) stream->timing.pix_clk_100hz *
						100LL, (v_total * stream->timing.h_total));

				/*
				 * one frame time in microsec:
				 * Delay_Us = 1000000 / refresh
				 * dynamic_delay_us = 1000000 / refresh + 2 * stutter_period
				 *
				 * one frame time modified by 'additional timer percent' (p):
				 * Delay_Us_modified = dynamic_delay_us + dynamic_delay_us * p / 100
				 *                   = dynamic_delay_us * (1 + p / 100)
				 *                   = (1000000 / refresh + 2 * stutter_period) * (100 + p) / 100
				 *                   = (1000000 + 2 * stutter_period * refresh) * (100 + p) / (100 * refresh)
				 *
				 * formula for timer duration based on parameters, from regspec:
				 * dynamic_delay_us = 65.28 * (64 + MallFrameCacheTmrDly) * 2^MallFrameCacheTmrScale
				 *
				 * dynamic_delay_us / 65.28 = (64 + MallFrameCacheTmrDly) * 2^MallFrameCacheTmrScale
				 * (dynamic_delay_us / 65.28) / 2^MallFrameCacheTmrScale = 64 + MallFrameCacheTmrDly
				 * MallFrameCacheTmrDly = ((dynamic_delay_us / 65.28) / 2^MallFrameCacheTmrScale) - 64
				 *                      = (1000000 + 2 * stutter_period * refresh) * (100 + p) / (100 * refresh) / 65.28 / 2^MallFrameCacheTmrScale - 64
				 *                      = (1000000 + 2 * stutter_period * refresh) * (100 + p) / (refresh * 6528 * 2^MallFrameCacheTmrScale) - 64
				 *
				 * need to round up the result of the division before the subtraction
				 */
				unsigned int denom = refresh_hz * 6528;
				unsigned int stutter_period = dc->current_state->perf_params.stutter_period_us;

				tmr_delay = div_u64(((1000000LL + 2 * stutter_period * refresh_hz) *
						(100LL + dc->debug.mall_additional_timer_percent) + denom - 1),
						denom) - 64LL;

				/* In some cases the stutter period is really big (tiny modes) in these
				 * cases MALL cant be enabled, So skip these cases to avoid a ASSERT()
				 *
				 * We can check if stutter_period is more than 1/10th the frame time to
				 * consider if we can actually meet the range of hysteresis timer
				 */
				if (stutter_period > 100000/refresh_hz)
					return false;

				/* scale should be increased until it fits into 6 bits */
				while (tmr_delay & ~0x3F) {
					tmr_scale++;

					if (tmr_scale > 3) {
						/* Delay exceeds range of hysteresis timer */
						ASSERT(false);
						return false;
					}

					denom *= 2;
					tmr_delay = div_u64(((1000000LL + 2 * stutter_period * refresh_hz) *
							(100LL + dc->debug.mall_additional_timer_percent) + denom - 1),
							denom) - 64LL;
				}

				/* Copy HW cursor */
				if (cursor_cache_enable) {
					memset(&cmd, 0, sizeof(cmd));
					cmd.mall.header.type = DMUB_CMD__MALL;
					cmd.mall.header.sub_type = DMUB_CMD__MALL_ACTION_COPY_CURSOR;
					cmd.mall.header.payload_bytes =
							sizeof(cmd.mall) - sizeof(cmd.mall.header);

					switch (cursor_attr.color_format) {
					case CURSOR_MODE_MONO:
						cmd.mall.cursor_bpp = 2;
						break;
					case CURSOR_MODE_COLOR_1BIT_AND:
					case CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA:
					case CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA:
						cmd.mall.cursor_bpp = 32;
						break;

					case CURSOR_MODE_COLOR_64BIT_FP_PRE_MULTIPLIED:
					case CURSOR_MODE_COLOR_64BIT_FP_UN_PRE_MULTIPLIED:
						cmd.mall.cursor_bpp = 64;
						break;
					}

					cmd.mall.cursor_copy_src.quad_part = cursor_attr.address.quad_part;
					cmd.mall.cursor_copy_dst.quad_part =
							(plane->address.grph.cursor_cache_addr.quad_part + 2047) & ~2047;
					cmd.mall.cursor_width = cursor_attr.width;
					cmd.mall.cursor_height = cursor_attr.height;
					cmd.mall.cursor_pitch = cursor_attr.pitch;

					dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

					/* Use copied cursor, and it's okay to not switch back */
					cursor_attr.address.quad_part = cmd.mall.cursor_copy_dst.quad_part;
					dc_stream_program_cursor_attributes(stream, &cursor_attr);
				}

				/* Enable MALL */
				memset(&cmd, 0, sizeof(cmd));
				cmd.mall.header.type = DMUB_CMD__MALL;
				cmd.mall.header.sub_type = DMUB_CMD__MALL_ACTION_ALLOW;
				cmd.mall.header.payload_bytes = sizeof(cmd.mall) - sizeof(cmd.mall.header);
				cmd.mall.tmr_delay = tmr_delay;
				cmd.mall.tmr_scale = tmr_scale;
				cmd.mall.debug_bits = dc->debug.mall_error_as_fatal;

				dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_NO_WAIT);

				return true;
			}
		}

		/* No applicable optimizations */
		return false;
	}

	/* Disable MALL */
	memset(&cmd, 0, sizeof(cmd));
	cmd.mall.header.type = DMUB_CMD__MALL;
	cmd.mall.header.sub_type = DMUB_CMD__MALL_ACTION_DISALLOW;
	cmd.mall.header.payload_bytes =
		sizeof(cmd.mall) - sizeof(cmd.mall.header);

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

bool dcn30_does_plane_fit_in_mall(struct dc *dc,
		unsigned int pitch,
		unsigned int height,
		enum surface_pixel_format format,
		struct dc_cursor_attributes *cursor_attr)
{
	// add meta size?
	unsigned int surface_size = pitch * height *
			(format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616 ? 8 : 4);
	unsigned int mall_size = dc->caps.mall_size_total;
	unsigned int cursor_size = 0;

	if (dc->debug.mall_size_override)
		mall_size = 1024 * 1024 * dc->debug.mall_size_override;

	if (cursor_attr) {
		cursor_size = dc->caps.max_cursor_size * dc->caps.max_cursor_size;

		switch (cursor_attr->color_format) {
		case CURSOR_MODE_MONO:
			cursor_size /= 2;
			break;
		case CURSOR_MODE_COLOR_1BIT_AND:
		case CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA:
		case CURSOR_MODE_COLOR_UN_PRE_MULTIPLIED_ALPHA:
			cursor_size *= 4;
			break;

		case CURSOR_MODE_COLOR_64BIT_FP_PRE_MULTIPLIED:
		case CURSOR_MODE_COLOR_64BIT_FP_UN_PRE_MULTIPLIED:
			cursor_size *= 8;
			break;
		}
	}

	return (surface_size + cursor_size) < mall_size;
}

void dcn30_hardware_release(struct dc *dc)
{
	bool subvp_in_use = false;
	uint32_t i;

	dc_dmub_srv_p_state_delegate(dc, false, NULL);
	dc_dmub_setup_subvp_dmub_command(dc, dc->current_state, false);

	/* SubVP treated the same way as FPO. If driver disable and
	 * we are using a SubVP config, disable and force on DCN side
	 * to prevent P-State hang on driver enable.
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (!pipe->stream)
			continue;

		if (dc_state_get_pipe_subvp_type(dc->current_state, pipe) == SUBVP_MAIN) {
			subvp_in_use = true;
			break;
		}
	}
	/* If pstate unsupported, or still supported
	 * by firmware, force it supported by dcn
	 */
	if (dc->current_state)
		if ((!dc->clk_mgr->clks.p_state_change_support || subvp_in_use ||
				dc->current_state->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) &&
				dc->res_pool->hubbub->funcs->force_pstate_change_control)
			dc->res_pool->hubbub->funcs->force_pstate_change_control(
					dc->res_pool->hubbub, true, true);
}

void dcn30_set_disp_pattern_generator(const struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum controller_dp_test_pattern test_pattern,
		enum controller_dp_color_space color_space,
		enum dc_color_depth color_depth,
		const struct tg_color *solid_color,
		int width, int height, int offset)
{
	pipe_ctx->stream_res.opp->funcs->opp_set_disp_pattern_generator(pipe_ctx->stream_res.opp, test_pattern,
			color_space, color_depth, solid_color, width, height, offset);
}

void dcn30_prepare_bandwidth(struct dc *dc,
	struct dc_state *context)
{
	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching && !dc->clk_mgr->clks.fw_based_mclk_switching) {
		dc->optimized_required = true;
		context->bw_ctx.bw.dcn.clk.p_state_change_support = false;
	}

	if (dc->clk_mgr->dc_mode_softmax_enabled)
		if (dc->clk_mgr->clks.dramclk_khz <= dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000 &&
				context->bw_ctx.bw.dcn.clk.dramclk_khz > dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000)
			dc->clk_mgr->funcs->set_max_memclk(dc->clk_mgr, dc->clk_mgr->bw_params->clk_table.entries[dc->clk_mgr->bw_params->clk_table.num_entries - 1].memclk_mhz);

	dcn20_prepare_bandwidth(dc, context);

	if (!dc->clk_mgr->clks.fw_based_mclk_switching)
		dc_dmub_srv_p_state_delegate(dc, false, context);
}

void dcn30_wait_for_all_pending_updates(const struct pipe_ctx *pipe_ctx)
{
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	bool pending_updates = false;
	unsigned int i;

	if (tg && tg->funcs->is_tg_enabled(tg)) {
		// Poll for 100ms maximum
		for (i = 0; i < 100000; i++) {
			pending_updates = false;
			if (tg->funcs->get_optc_double_buffer_pending)
				pending_updates |= tg->funcs->get_optc_double_buffer_pending(tg);

			if (tg->funcs->get_otg_double_buffer_pending)
				pending_updates |= tg->funcs->get_otg_double_buffer_pending(tg);

			if (tg->funcs->get_pipe_update_pending && pipe_ctx->plane_state)
				pending_updates |= tg->funcs->get_pipe_update_pending(tg);

			if (!pending_updates)
				break;

			udelay(1);
		}
	}
}

void dcn30_get_underflow_debug_data(const struct dc *dc,
	struct timing_generator *tg,
	struct dc_underflow_debug_data *out_data)
{
	struct hubbub *hubbub = dc->res_pool->hubbub;

	if (tg) {
		uint32_t v_blank_start = 0, v_blank_end = 0;

		out_data->otg_inst = tg->inst;

		tg->funcs->get_scanoutpos(tg,
					  &v_blank_start,
					  &v_blank_end,
					  &out_data->h_position,
					  &out_data->v_position);

		out_data->otg_frame_count = tg->funcs->get_frame_count(tg);

		out_data->otg_underflow = tg->funcs->is_optc_underflow_occurred(tg);
	}

	for (int i = 0; i < MAX_PIPES; i++) {
		struct hubp *hubp = dc->res_pool->hubps[i];

		if (hubp) {
			if (hubp->funcs->hubp_get_underflow_status)
				out_data->hubps[i].hubp_underflow = hubp->funcs->hubp_get_underflow_status(hubp);

			if (hubp->funcs->hubp_in_blank)
				out_data->hubps[i].hubp_in_blank = hubp->funcs->hubp_in_blank(hubp);

			if (hubp->funcs->hubp_get_current_read_line)
				out_data->hubps[i].hubp_readline = hubp->funcs->hubp_get_current_read_line(hubp);

			if (hubp->funcs->hubp_get_det_config_error)
				out_data->hubps[i].det_config_error = hubp->funcs->hubp_get_det_config_error(hubp);
		}
	}

	if (hubbub->funcs->get_det_sizes)
		hubbub->funcs->get_det_sizes(hubbub, out_data->curr_det_sizes, out_data->target_det_sizes);

	if (hubbub->funcs->compbuf_config_error)
		out_data->compbuf_config_error = hubbub->funcs->compbuf_config_error(hubbub);

}
