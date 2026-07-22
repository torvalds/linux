// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dccg.h"
#include "dce/dce_hwseq.h"
#include "reg_helper.h"
#include "abm.h"
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
#include "clk_mgr.h"
#include "dsc.h"
#include "link_service.h"

#include "dce/dmub_hw_lock_mgr.h"
#include "dcn10/dcn10_cm_common.h"
#include "dcn20/dcn20_optc.h"
#include "dcn30/dcn30_cm_common.h"
#include "dcn32/dcn32_hwseq.h"
#include "dcn50_hwseq.h"
#include "dcn60/dcn60_resource.h"
#include "dcn401/dcn401_hwseq.h"
#include "dcn401/dcn401_resource.h"
#include "dc_state_priv.h"
#include "link_enc_cfg.h"
#include "dio/dcn10/dcn10_dio.h"

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

static void dcn50_initialize_min_clocks(struct dc *dc)
{
	struct dc_clocks *clocks = &dc->current_state->bw_ctx.bw.dcn.clk;

	clocks->dcfclk_deep_sleep_khz = DCN3_2_DCFCLK_DS_INIT_KHZ;
	clocks->dcfclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz * 1000;
	clocks->socclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].socclk_mhz * 1000;
	clocks->dramclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].memclk_mhz * 1000;
	clocks->dppclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dppclk_mhz * 1000;
	if (dc->debug.disable_boot_optimizations) {
		clocks->dispclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dispclk_mhz * 1000;
	} else {
		/* Even though DPG_EN = 1 for the connected display, it still requires the
		 * correct timing so we cannot set DISPCLK to min freq or it could cause
		 * audio corruption. Read current DISPCLK from DENTIST and request the same
		 * freq to ensure that the timing is valid and unchanged.
		 */
		clocks->dispclk_khz = dc->clk_mgr->funcs->get_dispclk_from_dentist(dc->clk_mgr);
	}
	clocks->ref_dtbclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dtbclk_mhz * 1000;
	clocks->fclk_p_state_change_support = true;
	clocks->p_state_change_support = true;

	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			dc->current_state,
			true);
}

void dcn50_update_dchubp_dpp(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dccg *dccg = dc->res_pool->dccg;
	bool viewport_changed = false;
	enum mall_stream_type pipe_mall_type = dc_state_get_pipe_subvp_type(context, pipe_ctx);

	if (pipe_ctx->update_flags.bits.dppclk)
		dpp->funcs->dpp_dppclk_control(dpp, false, true);

	if (pipe_ctx->update_flags.bits.enable)
		dccg->funcs->update_dpp_dto(dccg, dpp->inst, pipe_ctx->plane_res.bw.dppclk_khz);

	/* TODO: Need input parameter to tell current DCHUB pipe tie to which OTG
	 * VTG is within DCHUBBUB which is commond block share by each pipe HUBP.
	 * VTG is 1:1 mapping with OTG. Each pipe HUBP will select which VTG
	 */

	if (pipe_ctx->update_flags.bits.hubp_rq_dlg_ttu) {
		hubp->funcs->hubp_vtg_sel(hubp, pipe_ctx->stream_res.tg->inst);

		if (hubp->funcs->hubp_setup2) {
			hubp->funcs->hubp_setup2(
				hubp,
				&pipe_ctx->hubp_regs,
				&pipe_ctx->global_sync,
				&pipe_ctx->stream->timing);
		} else {
			hubp->funcs->hubp_setup(
				hubp,
				&pipe_ctx->dlg_regs,
				&pipe_ctx->ttu_regs,
				&pipe_ctx->rq_regs,
				&pipe_ctx->pipe_dlg_param);
		}
	}

	if (pipe_ctx->update_flags.bits.unbounded_req && hubp->funcs->set_unbounded_requesting)
		hubp->funcs->set_unbounded_requesting(hubp, pipe_ctx->unbounded_req);

	if (pipe_ctx->update_flags.bits.hubp_interdependent) {
		if (hubp->funcs->hubp_setup_interdependent2) {
			hubp->funcs->hubp_setup_interdependent2(
				hubp,
				&pipe_ctx->hubp_regs);
		} else {
			hubp->funcs->hubp_setup_interdependent(
				hubp,
				&pipe_ctx->dlg_regs,
				&pipe_ctx->ttu_regs);
		}
	}

	if (pipe_ctx->update_flags.bits.enable ||
		pipe_ctx->update_flags.bits.plane_changed ||
		plane_state->update_bits.bpp_change ||
		plane_state->update_bits.input_csc_change ||
		plane_state->update_bits.color_space_change ||
		plane_state->update_bits.coeff_reduction_change) {
		struct dc_bias_and_scale bns_params = plane_state->bias_and_scale;

		// program the input csc
		dpp->funcs->dpp_setup(dpp,
			plane_state->format,
			EXPANSION_MODE_ZERO,
			plane_state->input_csc_color_matrix,
			plane_state->color_space,
			NULL);

		if (dpp->funcs->set_cursor_matrix) {
			dpp->funcs->set_cursor_matrix(dpp,
				plane_state->color_space,
				plane_state->cursor_csc_color_matrix);
		}
		if (dpp->funcs->dpp_program_bias_and_scale) {
			//TODO :for CNVC set scale and bias registers if necessary
			dpp->funcs->dpp_program_bias_and_scale(dpp, &bns_params);
		}
	}

	if (pipe_ctx->update_flags.bits.mpcc
		|| pipe_ctx->update_flags.bits.plane_changed
		|| plane_state->update_bits.global_alpha_change
		|| plane_state->update_bits.per_pixel_alpha_change) {
		// MPCC inst is equal to pipe index in practice
		hws->funcs.update_mpcc(dc, pipe_ctx);
	}

	if (pipe_ctx->update_flags.bits.scaler ||
		plane_state->update_bits.scaling_change ||
		plane_state->update_bits.position_change ||
		plane_state->update_bits.per_pixel_alpha_change ||
		pipe_ctx->stream->update_flags.bits.scaling) {
		pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->plane_state->per_pixel_alpha;
		ASSERT(pipe_ctx->plane_res.scl_data.lb_params.depth == LB_PIXEL_DEPTH_36BPP);
		/* scaler configuration */
		pipe_ctx->plane_res.dpp->funcs->dpp_set_scaler(
			pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data);
	}

	if (pipe_ctx->update_flags.bits.viewport ||
		(context == dc->current_state && plane_state->update_bits.position_change) ||
		(context == dc->current_state && plane_state->update_bits.scaling_change) ||
		(context == dc->current_state && pipe_ctx->stream->update_flags.bits.scaling)) {

		hubp->funcs->mem_program_viewport(
			hubp,
			&pipe_ctx->plane_res.scl_data.viewport,
			&pipe_ctx->plane_res.scl_data.viewport_c);
		viewport_changed = true;
	}

	/* Any updates are handled in dc interface, just need to apply existing for plane enable */
	if ((pipe_ctx->update_flags.bits.enable || pipe_ctx->update_flags.bits.opp_changed ||
		pipe_ctx->update_flags.bits.scaler || viewport_changed == true) &&
		pipe_ctx->stream->cursor_attributes.address.quad_part != 0) {
		if (dc->hwss.abort_cursor_offload_update)
			dc->hwss.abort_cursor_offload_update(dc, pipe_ctx);

		dc->hwss.set_cursor_attribute(pipe_ctx);
		dc->hwss.set_cursor_position(pipe_ctx);

		if (dc->hwss.set_cursor_sdr_white_level)
			dc->hwss.set_cursor_sdr_white_level(pipe_ctx);
	}

	/* Any updates are handled in dc interface, just need
	 * to apply existing for plane enable / opp change */
	if (pipe_ctx->update_flags.bits.enable || pipe_ctx->update_flags.bits.opp_changed
		|| pipe_ctx->update_flags.bits.plane_changed
		|| pipe_ctx->stream->update_flags.bits.gamut_remap
		|| plane_state->update_bits.gamut_remap_change
		|| pipe_ctx->stream->update_flags.bits.out_csc) {
		/* dpp/cm gamut remap*/
		hwss_program_gamut_remap(pipe_ctx);

		/*call the dcn2 method which uses mpc csc*/
		dc->hwss.program_output_csc(dc,
			pipe_ctx,
			pipe_ctx->stream->output_color_space,
			pipe_ctx->stream->csc_color_matrix.matrix,
			hubp->opp_id);
	}

	if (pipe_ctx->update_flags.bits.enable ||
		pipe_ctx->update_flags.bits.plane_changed ||
		plane_state->update_bits.addr_update) {
		if (resource_is_pipe_type(pipe_ctx, OTG_MASTER) &&
			pipe_mall_type == SUBVP_MAIN) {
			union block_sequence_params params;

			params.subvp_save_surf_addr.dc_dmub_srv = dc->ctx->dmub_srv;
			params.subvp_save_surf_addr.addr = &pipe_ctx->plane_state->address;
			params.subvp_save_surf_addr.subvp_index = pipe_ctx->subvp_index;
			hwss_subvp_save_surf_addr(&params);
		}
		dc->hwss.update_plane_addr(dc, pipe_ctx);
	}

	if (pipe_ctx->update_flags.bits.enable)
		hubp->funcs->set_blank(hubp, false);
	/* If the stream paired with this plane is phantom, the plane is also phantom */
	if (pipe_mall_type == SUBVP_PHANTOM && hubp->funcs->phantom_hubp_post_enable)
		hubp->funcs->phantom_hubp_post_enable(hubp);
}

void dcn50_update_dchubp_dpp_sequence(struct dc *dc,
				       struct pipe_ctx *pipe_ctx,
				       struct dc_state *context,
				       struct block_sequence_state *seq_state)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dccg *dccg = dc->res_pool->dccg;
	bool viewport_changed = false;
	enum mall_stream_type pipe_mall_type = dc_state_get_pipe_subvp_type(context, pipe_ctx);

	if (!hubp || !dpp || !plane_state)
		return;

	/* Step 1: DPP DPPCLK control */
	if (pipe_ctx->update_flags.bits.dppclk)
		hwss_add_dpp_dppclk_control(seq_state, dpp, false, true);

	/* Step 2: DCCG update DPP DTO */
	if (pipe_ctx->update_flags.bits.enable)
		hwss_add_dccg_update_dpp_dto(seq_state, dccg, dpp->inst, pipe_ctx->plane_res.bw.dppclk_khz);

	/* Step 3: HUBP VTG selection */
	if (pipe_ctx->update_flags.bits.hubp_rq_dlg_ttu) {
		hwss_add_hubp_vtg_sel(seq_state, hubp, pipe_ctx->stream_res.tg->inst);

		/* Step 4: HUBP setup (choose setup2 or setup) */
		if (hubp->funcs->hubp_setup2) {
			hwss_add_hubp_setup2(seq_state, hubp, &pipe_ctx->hubp_regs,
				&pipe_ctx->global_sync, &pipe_ctx->stream->timing);
		} else if (hubp->funcs->hubp_setup) {
			hwss_add_hubp_setup(seq_state, hubp, &pipe_ctx->dlg_regs,
				&pipe_ctx->ttu_regs, &pipe_ctx->rq_regs, &pipe_ctx->pipe_dlg_param);
		}
	}

	/* Step 5: Set unbounded requesting */
	if (pipe_ctx->update_flags.bits.unbounded_req && hubp->funcs->set_unbounded_requesting)
		hwss_add_hubp_set_unbounded_requesting(seq_state, hubp, pipe_ctx->unbounded_req);

	/* Step 6: HUBP interdependent setup */
	if (pipe_ctx->update_flags.bits.hubp_interdependent) {
		if (hubp->funcs->hubp_setup_interdependent2)
			hwss_add_hubp_setup_interdependent2(seq_state, hubp, &pipe_ctx->hubp_regs);
		else if (hubp->funcs->hubp_setup_interdependent)
			hwss_add_hubp_setup_interdependent(seq_state, hubp, &pipe_ctx->dlg_regs, &pipe_ctx->ttu_regs);
	}

	/* Step 7: DPP setup - input CSC and format setup */
	if (pipe_ctx->update_flags.bits.enable ||
			pipe_ctx->update_flags.bits.plane_changed ||
			plane_state->update_bits.bpp_change ||
			plane_state->update_bits.input_csc_change ||
			plane_state->update_bits.color_space_change ||
			plane_state->update_bits.coeff_reduction_change) {
		hwss_add_dpp_setup_dpp(seq_state, pipe_ctx);

		/* Step 8: DPP cursor matrix setup */
		if (dpp->funcs->set_cursor_matrix) {
			hwss_add_dpp_set_cursor_matrix(seq_state, dpp, plane_state->color_space,
				&plane_state->cursor_csc_color_matrix);
		}

		/* Step 9: DPP program bias and scale */
		if (dpp->funcs->dpp_program_bias_and_scale)
			hwss_add_dpp_program_bias_and_scale(seq_state, pipe_ctx);
	}

	/* Step 10: MPCC updates */
	if (pipe_ctx->update_flags.bits.mpcc ||
	     pipe_ctx->update_flags.bits.plane_changed ||
	     plane_state->update_bits.global_alpha_change ||
	     plane_state->update_bits.per_pixel_alpha_change) {

		/* Check if update_mpcc_sequence is implemented and prefer it over single MPC_UPDATE_MPCC step */
		if (hws->funcs.update_mpcc_sequence)
			hws->funcs.update_mpcc_sequence(dc, pipe_ctx, seq_state);
	}

	/* Step 11: DPP scaler setup */
	if (pipe_ctx->update_flags.bits.scaler ||
			plane_state->update_bits.scaling_change ||
			plane_state->update_bits.position_change ||
			plane_state->update_bits.per_pixel_alpha_change ||
			pipe_ctx->stream->update_flags.bits.scaling) {
		pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->plane_state->per_pixel_alpha;
		ASSERT(pipe_ctx->plane_res.scl_data.lb_params.depth == LB_PIXEL_DEPTH_36BPP);
		hwss_add_dpp_set_scaler(seq_state, pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data);
	}

	/* Step 12: HUBP viewport programming */
	if (pipe_ctx->update_flags.bits.viewport ||
	     (context == dc->current_state && plane_state->update_bits.position_change) ||
	     (context == dc->current_state && plane_state->update_bits.scaling_change) ||
	     (context == dc->current_state && pipe_ctx->stream->update_flags.bits.scaling)) {
		hwss_add_hubp_mem_program_viewport(seq_state, hubp,
			&pipe_ctx->plane_res.scl_data.viewport, &pipe_ctx->plane_res.scl_data.viewport_c);
		viewport_changed = true;
	}

	/* Step 13: Cursor attribute setup */
	if ((pipe_ctx->update_flags.bits.enable || pipe_ctx->update_flags.bits.opp_changed ||
	     pipe_ctx->update_flags.bits.scaler || viewport_changed == true) &&
	    pipe_ctx->stream->cursor_attributes.address.quad_part != 0) {

		hwss_add_abort_cursor_offload_update(seq_state, dc, pipe_ctx);

		hwss_add_set_cursor_attribute(seq_state, dc, pipe_ctx);

		/* Step 14: Cursor position setup */
		hwss_add_set_cursor_position(seq_state, dc, pipe_ctx);

		/* Step 15: Cursor SDR white level */
		if (dc->hwss.set_cursor_sdr_white_level)
			hwss_add_set_cursor_sdr_white_level(seq_state, dc, pipe_ctx);
	}

	/* Step 16: Gamut remap and output CSC */
	if (pipe_ctx->update_flags.bits.enable || pipe_ctx->update_flags.bits.opp_changed ||
			pipe_ctx->update_flags.bits.plane_changed ||
			pipe_ctx->stream->update_flags.bits.gamut_remap ||
			plane_state->update_bits.gamut_remap_change ||
			pipe_ctx->stream->update_flags.bits.out_csc) {

		/* Gamut remap */
		hwss_add_dpp_program_gamut_remap(seq_state, pipe_ctx);

		/* Output CSC */
		hwss_add_program_output_csc(seq_state, dc, pipe_ctx, pipe_ctx->stream->output_color_space,
			pipe_ctx->stream->csc_color_matrix.matrix, hubp->opp_id);
	}

	/* Step 17: Update plane address (with SubVP support) */
	if (pipe_ctx->update_flags.bits.enable ||
	     pipe_ctx->update_flags.bits.plane_changed ||
	     plane_state->update_bits.addr_update) {

		/* SubVP save surface address if needed */
		if (resource_is_pipe_type(pipe_ctx, OTG_MASTER) && pipe_mall_type == SUBVP_MAIN) {
			hwss_add_dmub_subvp_save_surf_addr(seq_state, dc->ctx->dmub_srv,
				&pipe_ctx->plane_state->address, pipe_ctx->subvp_index);
		}

		/* Update plane address */
		hwss_add_hubp_update_plane_addr(seq_state, dc, pipe_ctx);
	}

	/* Step 18: HUBP set blank - enable plane */
	if (pipe_ctx->update_flags.bits.enable)
		hwss_add_hubp_set_blank(seq_state, hubp, false);

	/* Step 19: Phantom HUBP post enable */
	if (pipe_mall_type == SUBVP_PHANTOM && hubp->funcs->phantom_hubp_post_enable)
		hwss_add_phantom_hubp_post_enable(seq_state, hubp);
}

void dcn50_update_mpcc_sequence(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				struct block_sequence_state *seq_state)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct mpcc_blnd_cfg blnd_cfg = {0};
	bool per_pixel_alpha = pipe_ctx->plane_state->per_pixel_alpha;
	int mpcc_id;
	struct mpcc *new_mpcc;
	struct mpc *mpc = dc->res_pool->mpc;
	struct mpc_tree *mpc_tree_params = &(pipe_ctx->stream_res.opp->mpc_tree_params);

	if (!hubp || !pipe_ctx->plane_state)
		return;

	/* Initialize blend configuration */
	blnd_cfg.overlap_only = false;
	blnd_cfg.global_gain = 0xfff;

	if (per_pixel_alpha) {
		blnd_cfg.pre_multiplied_alpha = pipe_ctx->plane_state->pre_multiplied_alpha;
		if (pipe_ctx->plane_state->global_alpha) {
			blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN;
			blnd_cfg.global_gain = pipe_ctx->plane_state->global_alpha_value;
		} else {
			blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA;
		}
	} else {
		blnd_cfg.pre_multiplied_alpha = false;
		blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
	}

	if (pipe_ctx->plane_state->global_alpha)
		blnd_cfg.global_alpha = pipe_ctx->plane_state->global_alpha_value;
	else
		blnd_cfg.global_alpha = 0xfff;

	blnd_cfg.background_color_bpc = 4;
	blnd_cfg.bottom_gain_mode = 0;
	blnd_cfg.top_gain = 0x1f000;
	blnd_cfg.bottom_inside_gain = 0x1f000;
	blnd_cfg.bottom_outside_gain = 0x1f000;

	if (pipe_ctx->plane_state->format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA)
		blnd_cfg.pre_multiplied_alpha = false;

	/* MPCC instance is equal to HUBP instance */
	mpcc_id = hubp->inst;

	/* Step 1: Update blending if no full update needed */
	if (!pipe_ctx->plane_state->update_bits.full_update &&
	    !pipe_ctx->update_flags.bits.mpcc) {

		/* Update blending configuration */
		hwss_add_mpc_update_blending(seq_state, mpc, blnd_cfg, mpcc_id);

		/* Update visual confirm color */
		hwss_add_mpc_update_visual_confirm(seq_state, dc, pipe_ctx, mpcc_id);
		return;
	}

	/* Step 2: Get existing MPCC for DPP */
	new_mpcc = mpc->funcs->get_mpcc_for_dpp(mpc_tree_params, mpcc_id);

	/* Step 3: Remove MPCC if being used */
	if (new_mpcc != NULL) {
		hwss_add_mpc_remove_mpcc(seq_state, mpc, mpc_tree_params, new_mpcc);
	} else {
		/* Step 4: Assert MPCC idle (debug only) */
		if (dc->debug.sanity_checks)
			hwss_add_mpc_assert_idle_mpcc(seq_state, mpc, mpcc_id);
	}

	/* Step 5: Insert new plane into MPC tree */
	hwss_add_mpc_insert_plane(seq_state, mpc, mpc_tree_params, blnd_cfg, NULL, NULL, hubp->inst, mpcc_id);

	/* Step 6: Update visual confirm color */
	hwss_add_mpc_update_visual_confirm(seq_state, dc, pipe_ctx, mpcc_id);

	/* Step 7: Set HUBP OPP and MPCC IDs */
	hubp->opp_id = pipe_ctx->stream_res.opp->inst;
	hubp->mpcc_id = mpcc_id;
}

static void dcn50_setup_hpo_hw_control(const struct dce_hwseq *hws, bool enable)
{
	REG_UPDATE(HPO_TOP_HW_CONTROL, HPO_IO_EN, enable);
}

void dcn50_program_front_end_for_ctx(
	struct dc *dc,
	struct dc_state *context)
{
	if (resource_is_pipe_topology_changed(dc->current_state, context))
		resource_log_pipe_topology_update(dc, context);

	hwss_build_full_sequence(dc,
		context->block_sequence,
		&(context->block_sequence_steps),
		context, false);
	hwss_execute_sequence(dc,
		context->block_sequence,
		context->block_sequence_steps);
}

void dcn50_post_unlock_program_front_end(
	struct dc *dc,
	struct dc_state *context)
{
	hwss_build_post_unlock_full_sequence(dc,
		context->block_sequence,
		&(context->block_sequence_steps),
		context);
	hwss_execute_sequence(dc,
		context->block_sequence,
		context->block_sequence_steps);
}

void dcn50_init_hw(struct dc *dc)
{
	struct abm **abms = dc->res_pool->multiple_abms;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	unsigned int i;
	unsigned int edp_num;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;
	uint32_t user_level = MAX_BACKLIGHT_LEVEL;
	int current_dchub_ref_freq = 0;

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->init_clocks) {
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

		// mark dcmode limits present if any clock has distinct AC and DC values from SMU
		dc->caps.dcmode_power_limits_present = dc->clk_mgr->funcs->is_dc_mode_present &&
				dc->clk_mgr->funcs->is_dc_mode_present(dc->clk_mgr);
	}

	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	// Disable DMUB Initialization until IPS state programming is finalized
	//if (!dcb->funcs->is_accelerated_mode(dcb)) {
	//	hws->funcs.bios_golden_init(dc);
	//}

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

			current_dchub_ref_freq = res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;

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
			link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
			if (link->link_enc->funcs->fec_is_active &&
					link->link_enc->funcs->fec_is_active(link->link_enc))
				link->fec_state = dc_link_fec_enabled;
		}
	}

	/* enable_power_gating_plane before dsc_pg_control because
	 * FORCEON = 1 with hw default value on bootup, resume from s3
	 */
	if (hws->funcs.enable_power_gating_plane)
		hws->funcs.enable_power_gating_plane(dc->hwseq, true);

	/* we want to turn off all dp displays before doing detection */
	dc->link_srv->blank_all_dp_displays(dc);

	/* If taking control over from VBIOS, we may want to optimize our first
	 * mode set, so we need to skip powering down pipes until we know which
	 * pipes we want to use.
	 * Otherwise, if taking control is not possible, we need to power
	 * everything down.
	 */
	if (dcb->funcs->is_accelerated_mode(dcb) || !dc->config.seamless_boot_edp_requested) {
		/* Disable boot optimizations means power down everything including PHY, DIG,
		 * and OTG (i.e. the boot is not optimized because we do a full power down).
		 */
		if (dc->hwss.enable_accelerated_mode && dc->debug.disable_boot_optimizations)
			dc->hwss.enable_accelerated_mode(dc, dc->current_state);
		else
			hws->funcs.init_pipes(dc, dc->current_state);

		if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
					!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);

		dcn50_initialize_min_clocks(dc);

		/* On HW init, allow idle optimizations after pipes have been turned off.
		 *
		 * In certain D3 cases (i.e. BOCO / BOMACO) it's possible that hardware state
		 * is reset (i.e. not in idle at the time hw init is called), but software state
		 * still has idle_optimizations = true, so we must disable idle optimizations first
		 * (i.e. set false), then re-enable (set true).
		 */
		dc_allow_idle_optimizations(dc, false);
		dc_allow_idle_optimizations(dc, true);
	}

	/* In headless boot cases, DIG may be turned
	 * on which causes HW/SW discrepancies.
	 * To avoid this, power down hardware on boot
	 * if DIG is turned on and seamless boot not enabled
	 */
	if (!dc->config.seamless_boot_edp_requested) {
		struct dc_link *edp_links[MAX_NUM_EDP];
		struct dc_link *edp_link;

		dc_get_edp_links(dc, edp_links, &edp_num);
		if (edp_num) {
			for (i = 0; i < edp_num; i++) {
				edp_link = edp_links[i];
				if (edp_link->link_enc->funcs->is_dig_enabled &&
						edp_link->link_enc->funcs->is_dig_enabled(edp_link->link_enc) &&
						dc->hwss.edp_backlight_control &&
						hws->funcs.power_down &&
						dc->hwss.edp_power_control) {
					dc->hwss.edp_backlight_control(edp_link, false);
					hws->funcs.power_down(dc);
					dc->hwss.edp_power_control(edp_link, false);
				}
			}
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
		if (abms[i] != NULL && abms[i]->funcs != NULL)
			abms[i]->funcs->abm_init(abms[i], backlight, user_level);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	if (dc->res_pool->dio && dc->res_pool->dio->funcs->mem_pwr_ctrl)
		dc->res_pool->dio->funcs->mem_pwr_ctrl(dc->res_pool->dio, false);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		if (dc->res_pool->dccg && dc->res_pool->dccg->funcs && dc->res_pool->dccg->funcs->allow_clock_gating)
			dc->res_pool->dccg->funcs->allow_clock_gating(dc->res_pool->dccg, true);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	dcn50_setup_hpo_hw_control(hws, true);

	if (!dcb->funcs->is_accelerated_mode(dcb) && dc->res_pool->hubbub->funcs->init_watermarks)
		dc->res_pool->hubbub->funcs->init_watermarks(dc->res_pool->hubbub);

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
				dc->res_pool->hubbub, false, false);

	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);

	if (dc->res_pool->hubbub->funcs->set_request_limit && dc->config.sdpif_request_limit_words_per_umc > 0)
		dc->res_pool->hubbub->funcs->set_request_limit(dc->res_pool->hubbub,
			dc->ctx->dc_bios->vram_info.num_chans, dc->config.sdpif_request_limit_words_per_umc);

	// Get DMCUB capabilities
	if (dc->ctx->dmub_srv) {
		dc_dmub_srv_query_caps_cmd(dc->ctx->dmub_srv);
		dc->caps.dmub_caps.psr = dc->ctx->dmub_srv->dmub->feature_caps.psr;
		dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver > 0;
		dc->caps.dmub_caps.fams_ver = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver;
		dc->debug.fams2_config.bits.enable &=
				dc->caps.dmub_caps.fams_ver == dc->debug.fams_version.ver; // sw & fw fams versions must match for support
		if ((!dc->debug.fams2_config.bits.enable && dc->res_pool->funcs->update_bw_bounding_box)
			|| res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000 != current_dchub_ref_freq) {
			/* update bounding box if FAMS2 disabled, or if dchub clk has changed */
			if (dc->clk_mgr)
				dc->res_pool->funcs->update_bw_bounding_box(dc, dc->clk_mgr->bw_params);
		}
	}
}
