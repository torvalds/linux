/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include "dm_services.h"
#include "core_types.h"
#include "resource.h"
#include "dce/dce_hwseq.h"
#include "dcn10/dcn10_hwseq.h"
#include "reg_helper.h"
#include "hubp.h"
#include "dchubbub.h"
#include "timing_generator.h"
#include "opp.h"
#include "mpc.h"
#include "dcn42b_hwseq.h"

#define CTX \
	hws->ctx

#define REG(reg)\
	hws->regs->reg

#define DC_LOGGER \
	hws->ctx->logger

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

/*
 * dcn42b_init_pipes - Initialize pipes for dcn42b
 *
 * This function is modeled after dcn10_init_pipes but handles the case
 * where num_timing_generator != num_pipes (e.g., 3 TGs but 4 pipes).
 *
 * For dcn42b:
 * - num_timing_generator = 3
 * - num_pipes (num_dpp) = 4
 *
 * The key difference is that we iterate over timing generators separately
 * from pipes to avoid accessing timing_generators[i] when i >= num_timing_generator.
 */
void dcn42b_init_pipes(struct dc *dc, struct dc_state *context)
{
	uint8_t i;
	struct dce_hwseq *hws = dc->hwseq;
	struct hubbub *hubbub = dc->res_pool->hubbub;
	bool can_apply_seamless_boot = false;
	bool tg_enabled[MAX_PIPES] = {false};

	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i]->apply_seamless_boot_optimization) {
			can_apply_seamless_boot = true;
			break;
		}
	}

	for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* There is assumption that pipe_ctx is not mapping irregularly
		 * to non-preferred front end. If pipe_ctx->stream is not NULL,
		 * we will use the pipe, so don't disable
		 */
		if (pipe_ctx->stream != NULL && can_apply_seamless_boot)
			continue;

		/* Blank controller using driver code instead of
		 * command table.
		 */
		if (tg->funcs->is_tg_enabled(tg)) {
			if (hws->funcs.init_blank != NULL) {
				hws->funcs.init_blank(dc, tg);
				tg->funcs->lock(tg);
			} else {
				tg->funcs->lock(tg);
				tg->funcs->set_blank(tg, true);
				hwss_wait_for_blank_complete(tg);
			}
		}
	}

	/* Reset det size */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct hubp *hubp = dc->res_pool->hubps[i];

		/* Do not need to reset for seamless boot */
		if (pipe_ctx->stream != NULL && can_apply_seamless_boot)
			continue;

		if (hubbub && hubp) {
			if (hubbub->funcs->program_det_size)
				hubbub->funcs->program_det_size(hubbub, hubp->inst, 0);
			if (hubbub->funcs->program_det_segments)
				hubbub->funcs->program_det_segments(hubbub, hubp->inst, 0);
		}
	}

	/* num_opp will be equal to number of mpcc */
	for (i = 0; i < dc->res_pool->res_cap->num_opp; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* Cannot reset the MPC mux if seamless boot */
		if (pipe_ctx->stream != NULL && can_apply_seamless_boot)
			continue;

		dc->res_pool->mpc->funcs->mpc_init_single_inst(
				dc->res_pool->mpc, i);
	}

	/* initialize DWB pointer to MCIF_WB */
	for (i = 0; i < dc->res_pool->res_cap->num_dwb; i++)
		dc->res_pool->dwbc[i]->mcif = dc->res_pool->mcif_wb[i];

	for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];
		struct hubp *hubp = dc->res_pool->hubps[i];
		struct dpp *dpp = dc->res_pool->dpps[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* There is assumption that pipe_ctx is not mapping irregularly
		 * to non-preferred front end. If pipe_ctx->stream is not NULL,
		 * we will use the pipe, so don't disable
		 */
		if (can_apply_seamless_boot &&
			pipe_ctx->stream != NULL &&
			pipe_ctx->stream_res.tg->funcs->is_tg_enabled(
				pipe_ctx->stream_res.tg)) {
			// Enable double buffering for OTG_BLANK no matter if
			// seamless boot is enabled or not to suppress global sync
			// signals when OTG blanked. This is to prevent pipe from
			// requesting data while in PSR.
			tg->funcs->tg_init(tg);
			hubp->power_gated = true;
			tg_enabled[i] = true;
			continue;
		}

		/* Disable on the current state so the new one isn't cleared. */
		pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

		hubp->funcs->hubp_reset(hubp);
		dpp->funcs->dpp_reset(dpp);

		pipe_ctx->stream_res.tg = tg;
		pipe_ctx->pipe_idx = i;

		pipe_ctx->plane_res.hubp = hubp;
		pipe_ctx->plane_res.dpp = dpp;
		pipe_ctx->plane_res.mpcc_inst = (uint8_t)dpp->inst;
		hubp->mpcc_id = dpp->inst;
		hubp->opp_id = OPP_ID_INVALID;
		hubp->power_gated = false;

		dc->res_pool->opps[i]->mpc_tree_params.opp_id = dc->res_pool->opps[i]->inst;
		dc->res_pool->opps[i]->mpc_tree_params.opp_list = NULL;
		dc->res_pool->opps[i]->mpcc_disconnect_pending[pipe_ctx->plane_res.mpcc_inst] = true;
		pipe_ctx->stream_res.opp = dc->res_pool->opps[i];

		hws->funcs.plane_atomic_disconnect(dc, context, pipe_ctx);

		if (tg->funcs->is_tg_enabled(tg))
			tg->funcs->unlock(tg);

		dc->hwss.disable_plane(dc, context, pipe_ctx);

		pipe_ctx->stream_res.tg = NULL;
		pipe_ctx->plane_res.hubp = NULL;

		if (tg->funcs->is_tg_enabled(tg)) {
			if (tg->funcs->init_odm)
				tg->funcs->init_odm(tg);
		}

		tg->funcs->tg_init(tg);
	}

	/* Clean up MPC tree */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (tg_enabled[i]) {
			if (dc->res_pool->opps[i]->mpc_tree_params.opp_list) {
				if (dc->res_pool->opps[i]->mpc_tree_params.opp_list->mpcc_bot) {
					int bot_id = dc->res_pool->opps[i]->mpc_tree_params.opp_list->mpcc_bot->mpcc_id;

					if ((bot_id < MAX_MPCC) && (bot_id < MAX_PIPES) && (!tg_enabled[bot_id]))
						dc->res_pool->opps[i]->mpc_tree_params.opp_list = NULL;
				}
			}
		}
	}

	/* Power gate DSCs */
	if (hws->funcs.dsc_pg_control != NULL) {
		uint32_t num_opps = 0;
		uint32_t opp_id_src0 = OPP_ID_INVALID;
		uint32_t opp_id_src1 = OPP_ID_INVALID;

		// Step 1: To find out which OPTC is running & OPTC DSC is ON
		// We can't use res_pool->res_cap->num_timing_generator to check
		// Because it records display pipes default setting built in driver,
		// not display pipes of the current chip.
		// Some ASICs would be fused display pipes less than the default setting.
		// In dcnxx_resource_construct function, driver would obatin real information.
		for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
			uint32_t optc_dsc_state = 0;
			struct timing_generator *tg = dc->res_pool->timing_generators[i];

			if (tg->funcs->is_tg_enabled(tg)) {
				if (tg->funcs->get_dsc_status)
					tg->funcs->get_dsc_status(tg, &optc_dsc_state);
				// Only one OPTC with DSC is ON, so if we got one result, we would exit this block.
				// non-zero value is DSC enabled
				if (optc_dsc_state != 0) {
					tg->funcs->get_optc_source(tg, &num_opps, &opp_id_src0, &opp_id_src1);
					break;
				}
			}
		}

		// Step 2: To power down DSC but skip DSC  of running OPTC
		for (i = 0; i < dc->res_pool->res_cap->num_dsc; i++) {
			struct dcn_dsc_state s  = {0};

			dc->res_pool->dscs[i]->funcs->dsc_read_state(dc->res_pool->dscs[i], &s);

			if ((s.dsc_opp_source == opp_id_src0 || s.dsc_opp_source == opp_id_src1) &&
				s.dsc_clock_en && s.dsc_fw_en)
				continue;

			hws->funcs.dsc_pg_control(hws, dc->res_pool->dscs[i]->inst, false);
		}
	}
}
