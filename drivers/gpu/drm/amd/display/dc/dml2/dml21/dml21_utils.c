// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#include "dml2_internal_shared_types.h"
#include "dml21_translation_helper.h"
#include "dml2_internal_types.h"
#include "dml21_utils.h"
#include "dml2_dc_resource_mgmt.h"

#include "dml2_core_dcn4_calcs.h"


int dml21_helper_find_dml_pipe_idx_by_stream_id(struct dml2_context *ctx, unsigned int stream_id)
{
	int i;
	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id_valid[i] && ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id[i] == stream_id)
			return  i;
	}

	return -1;
}

int dml21_find_dml_pipe_idx_by_plane_id(struct dml2_context *ctx, unsigned int plane_id)
{
	int i;
	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id_valid[i] && ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id[i] == plane_id)
			return  i;
	}

	return -1;
}

bool dml21_get_plane_id(const struct dc_state *state, const struct dc_plane_state *plane, unsigned int *plane_id)
{
	int i, j;

	if (!plane_id)
		return false;

	for (i = 0; i < state->stream_count; i++) {
		for (j = 0; j < state->stream_status[i].plane_count; j++) {
			if (state->stream_status[i].plane_states[j] == plane) {
				*plane_id = (i << 16) | j;
				return true;
			}
		}
	}

	return false;
}

unsigned int dml21_get_dc_plane_idx_from_plane_id(unsigned int plane_id)
{
	return 0xffff & plane_id;
}

void find_valid_pipe_idx_for_stream_index(const struct dml2_context *dml_ctx, unsigned int *dml_pipe_idx, unsigned int stream_index)
{
	unsigned int i = 0;

	for (i = 0; i < __DML2_WRAPPER_MAX_STREAMS_PLANES__; i++) {
		if (dml_ctx->v21.mode_programming.programming->plane_programming[i].plane_descriptor->stream_index == stream_index) {
			*dml_pipe_idx = i;
			return;
		}
	}
}

void find_pipe_regs_idx(const struct dml2_context *dml_ctx,
		struct pipe_ctx *pipe, unsigned int *pipe_regs_idx)
{
	struct pipe_ctx *opp_head = dml_ctx->config.callbacks.get_opp_head(pipe);

	*pipe_regs_idx = dml_ctx->config.callbacks.get_odm_slice_index(opp_head);

	if (pipe->plane_state)
		*pipe_regs_idx += dml_ctx->config.callbacks.get_mpc_slice_index(pipe);
}

/* places pipe references into pipes arrays and returns number of pipes */
int dml21_find_dc_pipes_for_plane(const struct dc *in_dc,
		struct dc_state *context,
		struct dml2_context *dml_ctx,
		struct pipe_ctx *dc_main_pipes[__DML2_WRAPPER_MAX_STREAMS_PLANES__],
		struct pipe_ctx *dc_phantom_pipes[__DML2_WRAPPER_MAX_STREAMS_PLANES__],
		int dml_plane_idx)
{
	unsigned int dml_stream_index;
	unsigned int main_stream_id;
	unsigned int dc_plane_index;
	struct dc_stream_state *dc_main_stream;
	struct dc_stream_status *dc_main_stream_status;
	struct dc_plane_state *dc_main_plane;
	struct dc_stream_state *dc_phantom_stream;
	struct dc_stream_status *dc_phantom_stream_status;
	struct dc_plane_state *dc_phantom_plane;
	int num_pipes = 0;

	memset(dc_main_pipes, 0, sizeof(struct pipe_ctx *) * __DML2_WRAPPER_MAX_STREAMS_PLANES__);
	memset(dc_phantom_pipes, 0, sizeof(struct pipe_ctx *) * __DML2_WRAPPER_MAX_STREAMS_PLANES__);

	dml_stream_index = dml_ctx->v21.mode_programming.programming->plane_programming[dml_plane_idx].plane_descriptor->stream_index;
	main_stream_id = dml_ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id[dml_stream_index];

	dc_main_stream = dml_ctx->config.callbacks.get_stream_from_id(context, main_stream_id);
	dc_main_stream_status = dml_ctx->config.callbacks.get_stream_status(context, dc_main_stream);
	if (!dc_main_stream_status)
		return num_pipes;

	/* find main plane based on id */
	dc_plane_index = dml21_get_dc_plane_idx_from_plane_id(dml_ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id[dml_plane_idx]);
	dc_main_plane = dc_main_stream_status->plane_states[dc_plane_index];

	if (dc_main_plane) {
		num_pipes = dml_ctx->config.callbacks.get_dpp_pipes_for_plane(dc_main_plane, &context->res_ctx, dc_main_pipes);
	} else {
		/* stream was configured with dummy plane, so get pipes from opp head */
		struct pipe_ctx *otg_master_pipe = dml_ctx->config.callbacks.get_otg_master_for_stream(&context->res_ctx, dc_main_stream);
		if (otg_master_pipe != NULL)
			num_pipes = dml_ctx->config.callbacks.get_opp_heads_for_otg_master(otg_master_pipe, &context->res_ctx, dc_main_pipes);
	}

	/* if phantom exists, find associated pipes */
	dc_phantom_stream = dml_ctx->config.svp_pstate.callbacks.get_paired_subvp_stream(context, dc_main_stream);
	if (dc_phantom_stream && num_pipes > 0) {
		dc_phantom_stream_status = dml_ctx->config.callbacks.get_stream_status(context, dc_phantom_stream);

		if (dc_phantom_stream_status) {
			/* phantom plane will have same index as main */
			dc_phantom_plane = dc_phantom_stream_status->plane_states[dc_plane_index];

			if (dc_phantom_plane) {
				/* only care about phantom pipes if they contain the phantom plane */
				dml_ctx->config.callbacks.get_dpp_pipes_for_plane(dc_phantom_plane, &context->res_ctx, dc_phantom_pipes);
			}
		}
	}

	return num_pipes;
}


void dml21_update_pipe_ctx_dchub_regs(struct dml2_display_rq_regs *rq_regs,
	struct dml2_display_dlg_regs *disp_dlg_regs,
	struct dml2_display_ttu_regs *disp_ttu_regs,
	struct pipe_ctx *out)
{
	memset(&out->rq_regs, 0, sizeof(out->rq_regs));
	out->rq_regs.rq_regs_l.chunk_size = rq_regs->rq_regs_l.chunk_size;
	out->rq_regs.rq_regs_l.min_chunk_size = rq_regs->rq_regs_l.min_chunk_size;
	//out->rq_regs.rq_regs_l.meta_chunk_size = rq_regs->rq_regs_l.meta_chunk_size;
	//out->rq_regs.rq_regs_l.min_meta_chunk_size = rq_regs->rq_regs_l.min_meta_chunk_size;
	out->rq_regs.rq_regs_l.dpte_group_size = rq_regs->rq_regs_l.dpte_group_size;
	out->rq_regs.rq_regs_l.mpte_group_size = rq_regs->rq_regs_l.mpte_group_size;
	out->rq_regs.rq_regs_l.swath_height = rq_regs->rq_regs_l.swath_height;
	out->rq_regs.rq_regs_l.pte_row_height_linear = rq_regs->rq_regs_l.pte_row_height_linear;

	out->rq_regs.rq_regs_c.chunk_size = rq_regs->rq_regs_c.chunk_size;
	out->rq_regs.rq_regs_c.min_chunk_size = rq_regs->rq_regs_c.min_chunk_size;
	//out->rq_regs.rq_regs_c.meta_chunk_size = rq_regs->rq_regs_c.meta_chunk_size;
	//out->rq_regs.rq_regs_c.min_meta_chunk_size = rq_regs->rq_regs_c.min_meta_chunk_size;
	out->rq_regs.rq_regs_c.dpte_group_size = rq_regs->rq_regs_c.dpte_group_size;
	out->rq_regs.rq_regs_c.mpte_group_size = rq_regs->rq_regs_c.mpte_group_size;
	out->rq_regs.rq_regs_c.swath_height = rq_regs->rq_regs_c.swath_height;
	out->rq_regs.rq_regs_c.pte_row_height_linear = rq_regs->rq_regs_c.pte_row_height_linear;

	out->rq_regs.drq_expansion_mode = rq_regs->drq_expansion_mode;
	out->rq_regs.prq_expansion_mode = rq_regs->prq_expansion_mode;
	//out->rq_regs.mrq_expansion_mode = rq_regs->mrq_expansion_mode;
	out->rq_regs.crq_expansion_mode = rq_regs->crq_expansion_mode;
	out->rq_regs.plane1_base_address = rq_regs->plane1_base_address;
	out->unbounded_req = rq_regs->unbounded_request_enabled;

	memset(&out->dlg_regs, 0, sizeof(out->dlg_regs));
	out->dlg_regs.refcyc_h_blank_end = disp_dlg_regs->refcyc_h_blank_end;
	out->dlg_regs.dlg_vblank_end = disp_dlg_regs->dlg_vblank_end;
	out->dlg_regs.min_dst_y_next_start = disp_dlg_regs->min_dst_y_next_start;
	out->dlg_regs.refcyc_per_htotal = disp_dlg_regs->refcyc_per_htotal;
	out->dlg_regs.refcyc_x_after_scaler = disp_dlg_regs->refcyc_x_after_scaler;
	out->dlg_regs.dst_y_after_scaler = disp_dlg_regs->dst_y_after_scaler;
	out->dlg_regs.dst_y_prefetch = disp_dlg_regs->dst_y_prefetch;
	out->dlg_regs.dst_y_per_vm_vblank = disp_dlg_regs->dst_y_per_vm_vblank;
	out->dlg_regs.dst_y_per_row_vblank = disp_dlg_regs->dst_y_per_row_vblank;
	out->dlg_regs.dst_y_per_vm_flip = disp_dlg_regs->dst_y_per_vm_flip;
	out->dlg_regs.dst_y_per_row_flip = disp_dlg_regs->dst_y_per_row_flip;
	out->dlg_regs.ref_freq_to_pix_freq = disp_dlg_regs->ref_freq_to_pix_freq;
	out->dlg_regs.vratio_prefetch = disp_dlg_regs->vratio_prefetch;
	out->dlg_regs.vratio_prefetch_c = disp_dlg_regs->vratio_prefetch_c;
	out->dlg_regs.refcyc_per_tdlut_group = disp_dlg_regs->refcyc_per_tdlut_group;
	out->dlg_regs.refcyc_per_pte_group_vblank_l = disp_dlg_regs->refcyc_per_pte_group_vblank_l;
	out->dlg_regs.refcyc_per_pte_group_vblank_c = disp_dlg_regs->refcyc_per_pte_group_vblank_c;
	//out->dlg_regs.refcyc_per_meta_chunk_vblank_l = disp_dlg_regs->refcyc_per_meta_chunk_vblank_l;
	//out->dlg_regs.refcyc_per_meta_chunk_vblank_c = disp_dlg_regs->refcyc_per_meta_chunk_vblank_c;
	out->dlg_regs.refcyc_per_pte_group_flip_l = disp_dlg_regs->refcyc_per_pte_group_flip_l;
	out->dlg_regs.refcyc_per_pte_group_flip_c = disp_dlg_regs->refcyc_per_pte_group_flip_c;
	//out->dlg_regs.refcyc_per_meta_chunk_flip_l = disp_dlg_regs->refcyc_per_meta_chunk_flip_l;
	//out->dlg_regs.refcyc_per_meta_chunk_flip_c = disp_dlg_regs->refcyc_per_meta_chunk_flip_c;
	out->dlg_regs.dst_y_per_pte_row_nom_l = disp_dlg_regs->dst_y_per_pte_row_nom_l;
	out->dlg_regs.dst_y_per_pte_row_nom_c = disp_dlg_regs->dst_y_per_pte_row_nom_c;
	out->dlg_regs.refcyc_per_pte_group_nom_l = disp_dlg_regs->refcyc_per_pte_group_nom_l;
	out->dlg_regs.refcyc_per_pte_group_nom_c = disp_dlg_regs->refcyc_per_pte_group_nom_c;
	//out->dlg_regs.dst_y_per_meta_row_nom_l = disp_dlg_regs->dst_y_per_meta_row_nom_l;
	//out->dlg_regs.dst_y_per_meta_row_nom_c = disp_dlg_regs->dst_y_per_meta_row_nom_c;
	//out->dlg_regs.refcyc_per_meta_chunk_nom_l = disp_dlg_regs->refcyc_per_meta_chunk_nom_l;
	//out->dlg_regs.refcyc_per_meta_chunk_nom_c = disp_dlg_regs->refcyc_per_meta_chunk_nom_c;
	out->dlg_regs.refcyc_per_line_delivery_pre_l = disp_dlg_regs->refcyc_per_line_delivery_pre_l;
	out->dlg_regs.refcyc_per_line_delivery_pre_c = disp_dlg_regs->refcyc_per_line_delivery_pre_c;
	out->dlg_regs.refcyc_per_line_delivery_l = disp_dlg_regs->refcyc_per_line_delivery_l;
	out->dlg_regs.refcyc_per_line_delivery_c = disp_dlg_regs->refcyc_per_line_delivery_c;
	out->dlg_regs.refcyc_per_vm_group_vblank = disp_dlg_regs->refcyc_per_vm_group_vblank;
	out->dlg_regs.refcyc_per_vm_group_flip = disp_dlg_regs->refcyc_per_vm_group_flip;
	out->dlg_regs.refcyc_per_vm_req_vblank = disp_dlg_regs->refcyc_per_vm_req_vblank;
	out->dlg_regs.refcyc_per_vm_req_flip = disp_dlg_regs->refcyc_per_vm_req_flip;
	out->dlg_regs.dst_y_offset_cur0 = disp_dlg_regs->dst_y_offset_cur0;
	out->dlg_regs.chunk_hdl_adjust_cur0 = disp_dlg_regs->chunk_hdl_adjust_cur0;
	//out->dlg_regs.dst_y_offset_cur1 = disp_dlg_regs->dst_y_offset_cur1;
	//out->dlg_regs.chunk_hdl_adjust_cur1 = disp_dlg_regs->chunk_hdl_adjust_cur1;
	out->dlg_regs.vready_after_vcount0 = disp_dlg_regs->vready_after_vcount0;
	out->dlg_regs.dst_y_delta_drq_limit = disp_dlg_regs->dst_y_delta_drq_limit;
	out->dlg_regs.refcyc_per_vm_dmdata = disp_dlg_regs->refcyc_per_vm_dmdata;
	out->dlg_regs.dmdata_dl_delta = disp_dlg_regs->dmdata_dl_delta;

	memset(&out->ttu_regs, 0, sizeof(out->ttu_regs));
	out->ttu_regs.qos_level_low_wm = disp_ttu_regs->qos_level_low_wm;
	out->ttu_regs.qos_level_high_wm = disp_ttu_regs->qos_level_high_wm;
	out->ttu_regs.min_ttu_vblank = disp_ttu_regs->min_ttu_vblank;
	out->ttu_regs.qos_level_flip = disp_ttu_regs->qos_level_flip;
	out->ttu_regs.refcyc_per_req_delivery_l = disp_ttu_regs->refcyc_per_req_delivery_l;
	out->ttu_regs.refcyc_per_req_delivery_c = disp_ttu_regs->refcyc_per_req_delivery_c;
	out->ttu_regs.refcyc_per_req_delivery_cur0 = disp_ttu_regs->refcyc_per_req_delivery_cur0;
	//out->ttu_regs.refcyc_per_req_delivery_cur1 = disp_ttu_regs->refcyc_per_req_delivery_cur1;
	out->ttu_regs.refcyc_per_req_delivery_pre_l = disp_ttu_regs->refcyc_per_req_delivery_pre_l;
	out->ttu_regs.refcyc_per_req_delivery_pre_c = disp_ttu_regs->refcyc_per_req_delivery_pre_c;
	out->ttu_regs.refcyc_per_req_delivery_pre_cur0 = disp_ttu_regs->refcyc_per_req_delivery_pre_cur0;
	//out->ttu_regs.refcyc_per_req_delivery_pre_cur1 = disp_ttu_regs->refcyc_per_req_delivery_pre_cur1;
	out->ttu_regs.qos_level_fixed_l = disp_ttu_regs->qos_level_fixed_l;
	out->ttu_regs.qos_level_fixed_c = disp_ttu_regs->qos_level_fixed_c;
	out->ttu_regs.qos_level_fixed_cur0 = disp_ttu_regs->qos_level_fixed_cur0;
	//out->ttu_regs.qos_level_fixed_cur1 = disp_ttu_regs->qos_level_fixed_cur1;
	out->ttu_regs.qos_ramp_disable_l = disp_ttu_regs->qos_ramp_disable_l;
	out->ttu_regs.qos_ramp_disable_c = disp_ttu_regs->qos_ramp_disable_c;
	out->ttu_regs.qos_ramp_disable_cur0 = disp_ttu_regs->qos_ramp_disable_cur0;
	//out->ttu_regs.qos_ramp_disable_cur1 = disp_ttu_regs->qos_ramp_disable_cur1;
}

void dml21_populate_mall_allocation_size(struct dc_state *context,
		struct dml2_context *in_ctx,
		struct dml2_per_plane_programming *pln_prog,
		struct pipe_ctx *dc_pipe)
{

	/* Reuse MALL Allocation Sizes logic from dcn32_fpu.c */
	/* Count from active, top pipes per plane only. Only add mall_ss_size_bytes for each unique plane. */
	if (dc_pipe->stream && dc_pipe->plane_state &&
			(dc_pipe->top_pipe == NULL ||
			dc_pipe->plane_state != dc_pipe->top_pipe->plane_state) &&
			dc_pipe->prev_odm_pipe == NULL) {
		/* SS: all active surfaces stored in MALL */
		if (in_ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(context, dc_pipe) != SUBVP_PHANTOM) {
			dc_pipe->surface_size_in_mall_bytes = pln_prog->surface_size_mall_bytes;
			context->bw_ctx.bw.dcn.mall_ss_size_bytes += dc_pipe->surface_size_in_mall_bytes;
		} else {
			/* SUBVP: phantom surfaces only stored in MALL */
			dc_pipe->surface_size_in_mall_bytes = pln_prog->svp_size_mall_bytes;
			context->bw_ctx.bw.dcn.mall_subvp_size_bytes += dc_pipe->surface_size_in_mall_bytes;
		}
	}
}

bool check_dp2p0_output_encoder(const struct pipe_ctx *pipe_ctx)
{
	/* If this assert is hit then we have a link encoder dynamic management issue */
	ASSERT(pipe_ctx->stream_res.hpo_dp_stream_enc ? pipe_ctx->link_res.hpo_dp_link_enc != NULL : true);
	return (pipe_ctx->stream_res.hpo_dp_stream_enc &&
		pipe_ctx->link_res.hpo_dp_link_enc &&
		dc_is_dp_signal(pipe_ctx->stream->signal));
}

void dml21_program_dc_pipe(struct dml2_context *dml_ctx, struct dc_state *context, struct pipe_ctx *pipe_ctx, struct dml2_per_plane_programming *pln_prog,
		struct dml2_per_stream_programming *stream_prog)
{
	unsigned int pipe_reg_index = 0;

	dml21_populate_pipe_ctx_dlg_params(dml_ctx, context, pipe_ctx, stream_prog);
	find_pipe_regs_idx(dml_ctx, pipe_ctx, &pipe_reg_index);

	if (dml_ctx->config.svp_pstate.callbacks.get_pipe_subvp_type(context, pipe_ctx) == SUBVP_PHANTOM) {
		memcpy(&pipe_ctx->hubp_regs, pln_prog->phantom_plane.pipe_regs[pipe_reg_index], sizeof(struct dml2_dchub_per_pipe_register_set));
		pipe_ctx->unbounded_req = false;

		/* legacy only, should be removed later */
		dml21_update_pipe_ctx_dchub_regs(&pln_prog->phantom_plane.pipe_regs[pipe_reg_index]->rq_regs,
				&pln_prog->phantom_plane.pipe_regs[pipe_reg_index]->dlg_regs,
				&pln_prog->phantom_plane.pipe_regs[pipe_reg_index]->ttu_regs, pipe_ctx);

		pipe_ctx->det_buffer_size_kb = 0;
	} else {
		memcpy(&pipe_ctx->hubp_regs, pln_prog->pipe_regs[pipe_reg_index], sizeof(struct dml2_dchub_per_pipe_register_set));
		pipe_ctx->unbounded_req = pln_prog->pipe_regs[pipe_reg_index]->rq_regs.unbounded_request_enabled;

		/* legacy only, should be removed later */
		dml21_update_pipe_ctx_dchub_regs(&pln_prog->pipe_regs[pipe_reg_index]->rq_regs,
				&pln_prog->pipe_regs[pipe_reg_index]->dlg_regs,
				&pln_prog->pipe_regs[pipe_reg_index]->ttu_regs, pipe_ctx);

		pipe_ctx->det_buffer_size_kb = pln_prog->pipe_regs[pipe_reg_index]->det_size * 64;
	}

	pipe_ctx->plane_res.bw.dppclk_khz = pln_prog->min_clocks.dcn4.dppclk_khz;
	if (context->bw_ctx.bw.dcn.clk.dppclk_khz < pipe_ctx->plane_res.bw.dppclk_khz)
		context->bw_ctx.bw.dcn.clk.dppclk_khz = pipe_ctx->plane_res.bw.dppclk_khz;

	dml21_populate_mall_allocation_size(context, dml_ctx, pln_prog, pipe_ctx);
	memcpy(&context->bw_ctx.bw.dcn.mcache_allocations[pipe_ctx->pipe_idx], &pln_prog->mcache_allocation, sizeof(struct dml2_mcache_surface_allocation));
}

static struct dc_stream_state *dml21_add_phantom_stream(struct dml2_context *dml_ctx,
	const struct dc *dc,
	struct dc_state *context,
	struct dc_stream_state *main_stream,
	struct dml2_per_stream_programming *stream_programming)
{
	struct dc_stream_state *phantom_stream;
	struct dml2_stream_parameters *phantom_stream_descriptor = &stream_programming->phantom_stream.descriptor;

	phantom_stream = dml_ctx->config.svp_pstate.callbacks.create_phantom_stream(dc, context, main_stream);
	if (!phantom_stream)
		return NULL;

	/* copy details of phantom stream from main */
	memcpy(&phantom_stream->timing, &main_stream->timing, sizeof(phantom_stream->timing));
	memcpy(&phantom_stream->src, &main_stream->src, sizeof(phantom_stream->src));
	memcpy(&phantom_stream->dst, &main_stream->dst, sizeof(phantom_stream->dst));

	/* modify timing for phantom */
	phantom_stream->timing.v_front_porch = phantom_stream_descriptor->timing.v_front_porch;
	phantom_stream->timing.v_addressable = phantom_stream_descriptor->timing.v_active;
	phantom_stream->timing.v_total = phantom_stream_descriptor->timing.v_total;
	phantom_stream->timing.flags.DSC = 0; // phantom always has DSC disabled

	phantom_stream->dst.y = 0;
	phantom_stream->dst.height = stream_programming->phantom_stream.descriptor.timing.v_active;

	phantom_stream->src.y = 0;
	phantom_stream->src.height = (double)phantom_stream_descriptor->timing.v_active * (double)main_stream->src.height / (double)main_stream->dst.height;

	phantom_stream->use_dynamic_meta = false;

	dml_ctx->config.svp_pstate.callbacks.add_phantom_stream(dc, context, phantom_stream, main_stream);

	return phantom_stream;
}

static struct dc_plane_state *dml21_add_phantom_plane(struct dml2_context *dml_ctx,
	const struct dc *dc,
	struct dc_state *context,
	struct dc_stream_state *phantom_stream,
	struct dc_plane_state *main_plane,
	struct dml2_per_plane_programming *plane_programming)
{
	struct dc_plane_state *phantom_plane;

	phantom_plane = dml_ctx->config.svp_pstate.callbacks.create_phantom_plane(dc, context, main_plane);
	if (!phantom_plane)
		return NULL;

	phantom_plane->format = main_plane->format;
	phantom_plane->rotation = main_plane->rotation;
	phantom_plane->visible = main_plane->visible;

	memcpy(&phantom_plane->address, &main_plane->address, sizeof(phantom_plane->address));
	memcpy(&phantom_plane->scaling_quality, &main_plane->scaling_quality,
		sizeof(phantom_plane->scaling_quality));
	memcpy(&phantom_plane->src_rect, &main_plane->src_rect, sizeof(phantom_plane->src_rect));
	memcpy(&phantom_plane->dst_rect, &main_plane->dst_rect, sizeof(phantom_plane->dst_rect));
	memcpy(&phantom_plane->clip_rect, &main_plane->clip_rect, sizeof(phantom_plane->clip_rect));
	memcpy(&phantom_plane->plane_size, &main_plane->plane_size,
		sizeof(phantom_plane->plane_size));
	memcpy(&phantom_plane->tiling_info, &main_plane->tiling_info,
		sizeof(phantom_plane->tiling_info));
	memcpy(&phantom_plane->dcc, &main_plane->dcc, sizeof(phantom_plane->dcc));

	phantom_plane->format = main_plane->format;
	phantom_plane->rotation = main_plane->rotation;
	phantom_plane->visible = main_plane->visible;

	/* Shadow pipe has small viewport. */
	phantom_plane->clip_rect.y = 0;
	phantom_plane->clip_rect.height = phantom_stream->src.height;

	dml_ctx->config.svp_pstate.callbacks.add_phantom_plane(dc, phantom_stream, phantom_plane, context);

	return phantom_plane;
}

void dml21_handle_phantom_streams_planes(const struct dc *dc, struct dc_state *context, struct dml2_context *dml_ctx)
{
	unsigned int dml_stream_index, dml_plane_index, dc_plane_index;
	struct dc_stream_state *main_stream;
	struct dc_stream_status *main_stream_status;
	struct dc_stream_state *phantom_stream;
	struct dc_plane_state *main_plane;
	bool phantoms_added = false;

	/* create phantom streams and planes and add to context */
	for (dml_stream_index = 0; dml_stream_index < dml_ctx->v21.mode_programming.programming->display_config.num_streams; dml_stream_index++) {
		/* iterate through DML streams looking for phantoms */
		if (dml_ctx->v21.mode_programming.programming->stream_programming[dml_stream_index].phantom_stream.enabled) {
			/* find associated dc stream */
			main_stream = dml_ctx->config.callbacks.get_stream_from_id(context,
					dml_ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_stream_id[dml_stream_index]);

			main_stream_status = dml_ctx->config.callbacks.get_stream_status(context, main_stream);

			if (!main_stream_status || main_stream_status->plane_count == 0)
				continue;

			/* create phantom stream for subvp enabled stream */
			phantom_stream = dml21_add_phantom_stream(dml_ctx,
					dc,
					context,
					main_stream,
					&dml_ctx->v21.mode_programming.programming->stream_programming[dml_stream_index]);

			if (!phantom_stream)
				continue;

			/* iterate through DML planes associated with this stream */
			for (dml_plane_index = 0; dml_plane_index < dml_ctx->v21.mode_programming.programming->display_config.num_planes; dml_plane_index++) {
				if (dml_ctx->v21.mode_programming.programming->plane_programming[dml_plane_index].plane_descriptor->stream_index == dml_stream_index) {
					/* find associated dc plane */
					dc_plane_index = dml21_get_dc_plane_idx_from_plane_id(dml_ctx->v21.dml_to_dc_pipe_mapping.dml_pipe_idx_to_plane_id[dml_plane_index]);
					main_plane = main_stream_status->plane_states[dc_plane_index];

					/* create phantom planes for subvp enabled plane */
					dml21_add_phantom_plane(dml_ctx,
							dc,
							context,
							phantom_stream,
							main_plane,
							&dml_ctx->v21.mode_programming.programming->plane_programming[dml_plane_index]);

					phantoms_added = true;
				}
			}
		}
	}

	if (phantoms_added)
		dml2_map_dc_pipes(dml_ctx, context, NULL, &dml_ctx->v21.dml_to_dc_pipe_mapping, dc->current_state);
}

void dml21_build_fams2_programming(const struct dc *dc,
		struct dc_state *context,
		struct dml2_context *dml_ctx)
{
	int i, j, k;

	/* reset fams2 data */
	context->bw_ctx.bw.dcn.fams2_stream_count = 0;
	memset(&context->bw_ctx.bw.dcn.fams2_stream_params, 0, sizeof(struct dmub_fams2_stream_static_state) * DML2_MAX_PLANES);

	if (!dml_ctx->v21.mode_programming.programming->fams2_required)
		return;

	for (i = 0; i < context->stream_count; i++) {
		int dml_stream_idx;
		struct dc_stream_state *phantom_stream;
		struct dc_stream_status *phantom_status;

		struct dmub_fams2_stream_static_state *static_state = &context->bw_ctx.bw.dcn.fams2_stream_params[context->bw_ctx.bw.dcn.fams2_stream_count];

		struct dc_stream_state *stream = context->streams[i];

		if (context->stream_status[i].plane_count == 0 ||
				dml_ctx->config.svp_pstate.callbacks.get_stream_subvp_type(context, stream) == SUBVP_PHANTOM) {
			/* can ignore blanked or phantom streams */
			continue;
		}

		dml_stream_idx = dml21_helper_find_dml_pipe_idx_by_stream_id(dml_ctx, stream->stream_id);
		if (dml_stream_idx < 0) {
			ASSERT(dml_stream_idx >= 0);
			continue;
		}

		/* copy static state from PMO */
		memcpy(static_state,
				&dml_ctx->v21.mode_programming.programming->stream_programming[dml_stream_idx].fams2_params,
				sizeof(struct dmub_fams2_stream_static_state));

		/* get information from context */
		static_state->num_planes = context->stream_status[i].plane_count;
		static_state->otg_inst = context->stream_status[i].primary_otg_inst;

		/* populate pipe masks for planes */
		for (j = 0; j < context->stream_status[i].plane_count; j++) {
			for (k = 0; k < dc->res_pool->pipe_count; k++) {
				if (context->res_ctx.pipe_ctx[k].stream &&
						context->res_ctx.pipe_ctx[k].stream->stream_id == stream->stream_id &&
						context->res_ctx.pipe_ctx[k].plane_state == context->stream_status[i].plane_states[j]) {
					static_state->pipe_mask |= (1 << k);
					static_state->plane_pipe_masks[j] |= (1 << k);
				}
			}
		}

		/* get per method programming */
		switch (static_state->type) {
		case FAMS2_STREAM_TYPE_VBLANK:
		case FAMS2_STREAM_TYPE_VACTIVE:
		case FAMS2_STREAM_TYPE_DRR:
			break;
		case FAMS2_STREAM_TYPE_SUBVP:
			phantom_stream = dml_ctx->config.svp_pstate.callbacks.get_paired_subvp_stream(context, stream);
			if (!phantom_stream)
				break;

			phantom_status = dml_ctx->config.callbacks.get_stream_status(context, phantom_stream);

			/* phantom status should always be present */
			ASSERT(phantom_status);
			static_state->sub_state.subvp.phantom_otg_inst = phantom_status->primary_otg_inst;

			/* populate pipe masks for phantom planes */
			for (j = 0; j < phantom_status->plane_count; j++) {
				for (k = 0; k < dc->res_pool->pipe_count; k++) {
					if (context->res_ctx.pipe_ctx[k].stream &&
							context->res_ctx.pipe_ctx[k].stream->stream_id == phantom_stream->stream_id &&
							context->res_ctx.pipe_ctx[k].plane_state == phantom_status->plane_states[j]) {
						static_state->sub_state.subvp.phantom_pipe_mask |= (1 << k);
						static_state->sub_state.subvp.phantom_plane_pipe_masks[j] |= (1 << k);
					}
				}
			}
			break;
		default:
			ASSERT(false);
			break;
		}

		context->bw_ctx.bw.dcn.fams2_stream_count++;
	}

	context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching = context->bw_ctx.bw.dcn.fams2_stream_count > 0;
}

bool dml21_is_plane1_enabled(enum dml2_source_format_class source_format)
{
	return source_format >= dml2_420_8 && source_format <= dml2_rgbe_alpha;
}
