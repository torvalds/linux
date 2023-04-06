/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include "dccg.h"
#include "dce/dce_hwseq.h"
#include "dcn30/dcn30_cm_common.h"
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
#include "dcn32_hwseq.h"
#include "clk_mgr.h"
#include "dsc.h"
#include "dcn20/dcn20_optc.h"
#include "dmub_subvp_state.h"
#include "dce/dmub_hw_lock_mgr.h"
#include "dcn32_resource.h"
#include "link.h"
#include "dmub/inc/dmub_subvp_state.h"

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

void dcn32_dsc_pg_control(
		struct dce_hwseq *hws,
		unsigned int dsc_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl = 0;

	if (hws->ctx->dc->debug.disable_dsc_power_gate)
		return;

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	switch (dsc_inst) {
	case 0: /* DSC0 */
		REG_UPDATE(DOMAIN16_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN16_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DSC1 */
		REG_UPDATE(DOMAIN17_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN17_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DSC2 */
		REG_UPDATE(DOMAIN18_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN18_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DSC3 */
		REG_UPDATE(DOMAIN19_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN19_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);
}


void dcn32_enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable)
{
	bool force_on = true; /* disable power gating */
	uint32_t org_ip_request_cntl = 0;

	if (enable)
		force_on = false;

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	/* DCHUBP0/1/2/3 */
	REG_UPDATE(DOMAIN0_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN1_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN2_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN3_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);

	/* DCS0/1/2/3 */
	REG_UPDATE(DOMAIN16_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN17_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN18_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN19_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);
}

void dcn32_hubp_pg_control(struct dce_hwseq *hws, unsigned int hubp_inst, bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_hubp_power_gate)
		return;

	if (REG(DOMAIN0_PG_CONFIG) == 0)
		return;

	switch (hubp_inst) {
	case 0:
		REG_SET(DOMAIN0_PG_CONFIG, 0, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 1:
		REG_SET(DOMAIN1_PG_CONFIG, 0, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 2:
		REG_SET(DOMAIN2_PG_CONFIG, 0, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 3:
		REG_SET(DOMAIN3_PG_CONFIG, 0, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

static bool dcn32_check_no_memory_request_for_cab(struct dc *dc)
{
	int i;

    /* First, check no-memory-request case */
	for (i = 0; i < dc->current_state->stream_count; i++) {
		if ((dc->current_state->stream_status[i].plane_count) &&
			(dc->current_state->streams[i]->link->psr_settings.psr_version == DC_PSR_VERSION_UNSUPPORTED))
			/* Fail eligibility on a visible stream */
			break;
	}

	if (i == dc->current_state->stream_count)
		return true;

	return false;
}


/* This function loops through every surface that needs to be cached in CAB for SS,
 * and calculates the total number of ways required to store all surfaces (primary,
 * meta, cursor).
 */
static uint32_t dcn32_calculate_cab_allocation(struct dc *dc, struct dc_state *ctx)
{
	int i;
	uint8_t num_ways = 0;
	uint32_t mall_ss_size_bytes = 0;

	mall_ss_size_bytes = ctx->bw_ctx.bw.dcn.mall_ss_size_bytes;
	// TODO add additional logic for PSR active stream exclusion optimization
	// mall_ss_psr_active_size_bytes = ctx->bw_ctx.bw.dcn.mall_ss_psr_active_size_bytes;

	// Include cursor size for CAB allocation
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &ctx->res_ctx.pipe_ctx[i];

		if (!pipe->stream || !pipe->plane_state)
			continue;

		mall_ss_size_bytes += dcn32_helper_calculate_mall_bytes_for_cursor(dc, pipe, false);
	}

	// Convert number of cache lines required to number of ways
	if (dc->debug.force_mall_ss_num_ways > 0) {
		num_ways = dc->debug.force_mall_ss_num_ways;
	} else {
		num_ways = dcn32_helper_mall_bytes_to_ways(dc, mall_ss_size_bytes);
	}

	return num_ways;
}

bool dcn32_apply_idle_power_optimizations(struct dc *dc, bool enable)
{
	union dmub_rb_cmd cmd;
	uint8_t ways, i;
	int j;
	bool mall_ss_unsupported = false;
	struct dc_plane_state *plane = NULL;

	if (!dc->ctx->dmub_srv)
		return false;

	for (i = 0; i < dc->current_state->stream_count; i++) {
		/* MALL SS messaging is not supported with PSR at this time */
		if (dc->current_state->streams[i] != NULL &&
				dc->current_state->streams[i]->link->psr_settings.psr_version != DC_PSR_VERSION_UNSUPPORTED)
			return false;
	}

	if (enable) {
		if (dc->current_state) {

			/* 1. Check no memory request case for CAB.
			 * If no memory request case, send CAB_ACTION NO_DF_REQ DMUB message
			 */
			if (dcn32_check_no_memory_request_for_cab(dc)) {
				/* Enable no-memory-requests case */
				memset(&cmd, 0, sizeof(cmd));
				cmd.cab.header.type = DMUB_CMD__CAB_FOR_SS;
				cmd.cab.header.sub_type = DMUB_CMD__CAB_NO_DCN_REQ;
				cmd.cab.header.payload_bytes = sizeof(cmd.cab) - sizeof(cmd.cab.header);

				dm_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_NO_WAIT);

				return true;
			}

			/* 2. Check if all surfaces can fit in CAB.
			 * If surfaces can fit into CAB, send CAB_ACTION_ALLOW DMUB message
			 * and configure HUBP's to fetch from MALL
			 */
			ways = dcn32_calculate_cab_allocation(dc, dc->current_state);

			/* MALL not supported with Stereo3D or TMZ surface. If any plane is using stereo,
			 * or TMZ surface, don't try to enter MALL.
			 */
			for (i = 0; i < dc->current_state->stream_count; i++) {
				for (j = 0; j < dc->current_state->stream_status[i].plane_count; j++) {
					plane = dc->current_state->stream_status[i].plane_states[j];

					if (plane->address.type == PLN_ADDR_TYPE_GRPH_STEREO ||
							plane->address.tmz_surface) {
						mall_ss_unsupported = true;
						break;
					}
				}
				if (mall_ss_unsupported)
					break;
			}
			if (ways <= dc->caps.cache_num_ways && !mall_ss_unsupported) {
				memset(&cmd, 0, sizeof(cmd));
				cmd.cab.header.type = DMUB_CMD__CAB_FOR_SS;
				cmd.cab.header.sub_type = DMUB_CMD__CAB_DCN_SS_FIT_IN_CAB;
				cmd.cab.header.payload_bytes = sizeof(cmd.cab) - sizeof(cmd.cab.header);
				cmd.cab.cab_alloc_ways = ways;

				dm_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_NO_WAIT);

				return true;
			}

		}
		return false;
	}

	/* Disable CAB */
	memset(&cmd, 0, sizeof(cmd));
	cmd.cab.header.type = DMUB_CMD__CAB_FOR_SS;
	cmd.cab.header.sub_type = DMUB_CMD__CAB_NO_IDLE_OPTIMIZATION;
	cmd.cab.header.payload_bytes =
			sizeof(cmd.cab) - sizeof(cmd.cab.header);

	dm_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

/* Send DMCUB message with SubVP pipe info
 * - For each pipe in context, populate payload with required SubVP information
 *   if the pipe is using SubVP for MCLK switch
 * - This function must be called while the DMUB HW lock is acquired by driver
 */
void dcn32_commit_subvp_config(struct dc *dc, struct dc_state *context)
{
	int i;
	bool enable_subvp = false;

	if (!dc->ctx || !dc->ctx->dmub_srv)
		return;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream && pipe_ctx->stream->mall_stream_config.paired_stream &&
				pipe_ctx->stream->mall_stream_config.type == SUBVP_MAIN) {
			// There is at least 1 SubVP pipe, so enable SubVP
			enable_subvp = true;
			break;
		}
	}
	dc_dmub_setup_subvp_dmub_command(dc, context, enable_subvp);
}

/* Sub-Viewport DMUB lock needs to be acquired by driver whenever SubVP is active and:
 * 1. Any full update for any SubVP main pipe
 * 2. Any immediate flip for any SubVP pipe
 * 3. Any flip for DRR pipe
 * 4. If SubVP was previously in use (i.e. in old context)
 */
void dcn32_subvp_pipe_control_lock(struct dc *dc,
		struct dc_state *context,
		bool lock,
		bool should_lock_all_pipes,
		struct pipe_ctx *top_pipe_to_program,
		bool subvp_prev_use)
{
	unsigned int i = 0;
	bool subvp_immediate_flip = false;
	bool subvp_in_use = false;
	struct pipe_ctx *pipe;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream && pipe->plane_state && pipe->stream->mall_stream_config.type == SUBVP_MAIN) {
			subvp_in_use = true;
			break;
		}
	}

	if (top_pipe_to_program && top_pipe_to_program->stream && top_pipe_to_program->plane_state) {
		if (top_pipe_to_program->stream->mall_stream_config.type == SUBVP_MAIN &&
				top_pipe_to_program->plane_state->flip_immediate)
			subvp_immediate_flip = true;
	}

	// Don't need to lock for DRR VSYNC flips -- FW will wait for DRR pending update cleared.
	if ((subvp_in_use && (should_lock_all_pipes || subvp_immediate_flip)) || (!subvp_in_use && subvp_prev_use)) {
		union dmub_inbox0_cmd_lock_hw hw_lock_cmd = { 0 };

		if (!lock) {
			for (i = 0; i < dc->res_pool->pipe_count; i++) {
				pipe = &context->res_ctx.pipe_ctx[i];
				if (pipe->stream && pipe->plane_state && pipe->stream->mall_stream_config.type == SUBVP_MAIN &&
						should_lock_all_pipes)
					pipe->stream_res.tg->funcs->wait_for_state(pipe->stream_res.tg, CRTC_STATE_VBLANK);
			}
		}

		hw_lock_cmd.bits.command_code = DMUB_INBOX0_CMD__HW_LOCK;
		hw_lock_cmd.bits.hw_lock_client = HW_LOCK_CLIENT_DRIVER;
		hw_lock_cmd.bits.lock = lock;
		hw_lock_cmd.bits.should_release = !lock;
		dmub_hw_lock_mgr_inbox0_cmd(dc->ctx->dmub_srv, hw_lock_cmd);
	}
}

void dcn32_subvp_pipe_control_lock_fast(union block_sequence_params *params)
{
	struct dc *dc = params->subvp_pipe_control_lock_fast_params.dc;
	bool lock = params->subvp_pipe_control_lock_fast_params.lock;
	struct pipe_ctx *pipe_ctx = params->subvp_pipe_control_lock_fast_params.pipe_ctx;
	bool subvp_immediate_flip = false;

	if (pipe_ctx && pipe_ctx->stream && pipe_ctx->plane_state) {
		if (pipe_ctx->stream->mall_stream_config.type == SUBVP_MAIN &&
				pipe_ctx->plane_state->flip_immediate)
			subvp_immediate_flip = true;
	}

	// Don't need to lock for DRR VSYNC flips -- FW will wait for DRR pending update cleared.
	if (subvp_immediate_flip) {
		union dmub_inbox0_cmd_lock_hw hw_lock_cmd = { 0 };

		hw_lock_cmd.bits.command_code = DMUB_INBOX0_CMD__HW_LOCK;
		hw_lock_cmd.bits.hw_lock_client = HW_LOCK_CLIENT_DRIVER;
		hw_lock_cmd.bits.lock = lock;
		hw_lock_cmd.bits.should_release = !lock;
		dmub_hw_lock_mgr_inbox0_cmd(dc->ctx->dmub_srv, hw_lock_cmd);
	}
}

bool dcn32_set_mpc_shaper_3dlut(
	struct pipe_ctx *pipe_ctx, const struct dc_stream_state *stream)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;
	bool result = false;

	const struct pwl_params *shaper_lut = NULL;
	//get the shaper lut params
	if (stream->func_shaper) {
		if (stream->func_shaper->type == TF_TYPE_HWPWL)
			shaper_lut = &stream->func_shaper->pwl;
		else if (stream->func_shaper->type == TF_TYPE_DISTRIBUTED_POINTS) {
			cm_helper_translate_curve_to_hw_format(stream->ctx,
					stream->func_shaper,
					&dpp_base->shaper_params, true);
			shaper_lut = &dpp_base->shaper_params;
		}
	}

	if (stream->lut3d_func &&
		stream->lut3d_func->state.bits.initialized == 1) {

		result = mpc->funcs->program_3dlut(mpc,
								&stream->lut3d_func->lut_3d,
								mpcc_id);

		result = mpc->funcs->program_shaper(mpc,
								shaper_lut,
								mpcc_id);
	}

	return result;
}

bool dcn32_set_mcm_luts(
	struct pipe_ctx *pipe_ctx, const struct dc_plane_state *plane_state)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;
	bool result = true;
	struct pwl_params *lut_params = NULL;

	// 1D LUT
	if (plane_state->blend_tf) {
		if (plane_state->blend_tf->type == TF_TYPE_HWPWL)
			lut_params = &plane_state->blend_tf->pwl;
		else if (plane_state->blend_tf->type == TF_TYPE_DISTRIBUTED_POINTS) {
			cm_helper_translate_curve_to_hw_format(plane_state->ctx,
					plane_state->blend_tf,
					&dpp_base->regamma_params, false);
			lut_params = &dpp_base->regamma_params;
		}
	}
	result = mpc->funcs->program_1dlut(mpc, lut_params, mpcc_id);

	// Shaper
	if (plane_state->in_shaper_func) {
		if (plane_state->in_shaper_func->type == TF_TYPE_HWPWL)
			lut_params = &plane_state->in_shaper_func->pwl;
		else if (plane_state->in_shaper_func->type == TF_TYPE_DISTRIBUTED_POINTS) {
			// TODO: dpp_base replace
			ASSERT(false);
			cm_helper_translate_curve_to_hw_format(plane_state->ctx,
					plane_state->in_shaper_func,
					&dpp_base->shaper_params, true);
			lut_params = &dpp_base->shaper_params;
		}
	}

	result = mpc->funcs->program_shaper(mpc, lut_params, mpcc_id);

	// 3D
	if (plane_state->lut3d_func && plane_state->lut3d_func->state.bits.initialized == 1)
		result = mpc->funcs->program_3dlut(mpc, &plane_state->lut3d_func->lut_3d, mpcc_id);
	else
		result = mpc->funcs->program_3dlut(mpc, NULL, mpcc_id);

	return result;
}

bool dcn32_set_input_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct mpc *mpc = dc->res_pool->mpc;
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;

	enum dc_transfer_func_predefined tf;
	bool result = true;
	struct pwl_params *params = NULL;

	if (mpc == NULL || plane_state == NULL)
		return false;

	tf = TRANSFER_FUNCTION_UNITY;

	if (plane_state->in_transfer_func &&
		plane_state->in_transfer_func->type == TF_TYPE_PREDEFINED)
		tf = plane_state->in_transfer_func->tf;

	dpp_base->funcs->dpp_set_pre_degam(dpp_base, tf);

	if (plane_state->in_transfer_func) {
		if (plane_state->in_transfer_func->type == TF_TYPE_HWPWL)
			params = &plane_state->in_transfer_func->pwl;
		else if (plane_state->in_transfer_func->type == TF_TYPE_DISTRIBUTED_POINTS &&
			cm3_helper_translate_curve_to_hw_format(plane_state->in_transfer_func,
					&dpp_base->degamma_params, false))
			params = &dpp_base->degamma_params;
	}

	dpp_base->funcs->dpp_program_gamcor_lut(dpp_base, params);

	if (pipe_ctx->stream_res.opp &&
			pipe_ctx->stream_res.opp->ctx &&
			hws->funcs.set_mcm_luts)
		result = hws->funcs.set_mcm_luts(pipe_ctx, plane_state);

	return result;
}

bool dcn32_set_output_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream)
{
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;
	struct pwl_params *params = NULL;
	bool ret = false;

	/* program OGAM or 3DLUT only for the top pipe*/
	if (pipe_ctx->top_pipe == NULL) {
		/*program shaper and 3dlut in MPC*/
		ret = dcn32_set_mpc_shaper_3dlut(pipe_ctx, stream);
		if (ret == false && mpc->funcs->set_output_gamma && stream->out_transfer_func) {
			if (stream->out_transfer_func->type == TF_TYPE_HWPWL)
				params = &stream->out_transfer_func->pwl;
			else if (pipe_ctx->stream->out_transfer_func->type ==
					TF_TYPE_DISTRIBUTED_POINTS &&
					cm3_helper_translate_curve_to_hw_format(
					stream->out_transfer_func,
					&mpc->blender_params, false))
				params = &mpc->blender_params;
			/* there are no ROM LUTs in OUTGAM */
			if (stream->out_transfer_func->type == TF_TYPE_PREDEFINED)
				BREAK_TO_DEBUGGER();
		}
	}

	mpc->funcs->set_output_gamma(mpc, mpcc_id, params);
	return ret;
}

/* Program P-State force value according to if pipe is using SubVP / FPO or not:
 * 1. Reset P-State force on all pipes first
 * 2. For each main pipe, force P-State disallow (P-State allow moderated by DMUB)
 */
void dcn32_update_force_pstate(struct dc *dc, struct dc_state *context)
{
	int i;

	/* Unforce p-state for each pipe if it is not FPO or SubVP.
	 * For FPO and SubVP, if it's already forced disallow, leave
	 * it as disallow.
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct hubp *hubp = pipe->plane_res.hubp;

		if (!pipe->stream || !(pipe->stream->mall_stream_config.type == SUBVP_MAIN ||
		    pipe->stream->fpo_in_use)) {
			if (hubp && hubp->funcs->hubp_update_force_pstate_disallow)
				hubp->funcs->hubp_update_force_pstate_disallow(hubp, false);
		}

		/* Today only FPO uses cursor P-State force. Only clear cursor P-State force
		 * if it's not FPO.
		 */
		if (!pipe->stream || !pipe->stream->fpo_in_use) {
			if (hubp && hubp->funcs->hubp_update_force_cursor_pstate_disallow)
				hubp->funcs->hubp_update_force_cursor_pstate_disallow(hubp, false);
		}
	}

	/* Loop through each pipe -- for each subvp main pipe force p-state allow equal to false.
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct hubp *hubp = pipe->plane_res.hubp;

		if (pipe->stream && pipe->plane_state && pipe->stream->mall_stream_config.type == SUBVP_MAIN) {
			if (hubp && hubp->funcs->hubp_update_force_pstate_disallow)
				hubp->funcs->hubp_update_force_pstate_disallow(hubp, true);
		}

		if (pipe->stream && pipe->stream->fpo_in_use) {
			if (hubp && hubp->funcs->hubp_update_force_pstate_disallow)
				hubp->funcs->hubp_update_force_pstate_disallow(hubp, true);
			/* For now only force cursor p-state disallow for FPO
			 * Needs to be added for subvp once FW side gets updated
			 */
			if (hubp && hubp->funcs->hubp_update_force_cursor_pstate_disallow)
				hubp->funcs->hubp_update_force_cursor_pstate_disallow(hubp, true);
		}
	}
}

/* Update MALL_SEL register based on if pipe / plane
 * is a phantom pipe, main pipe, and if using MALL
 * for SS.
 */
void dcn32_update_mall_sel(struct dc *dc, struct dc_state *context)
{
	int i;
	unsigned int num_ways = dcn32_calculate_cab_allocation(dc, context);
	bool cache_cursor = false;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct hubp *hubp = pipe->plane_res.hubp;

		if (pipe->stream && pipe->plane_state && hubp && hubp->funcs->hubp_update_mall_sel) {
			int cursor_size = hubp->curs_attr.pitch * hubp->curs_attr.height;

			switch (hubp->curs_attr.color_format) {
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
			default:
				cursor_size *= 8;
				break;
			}

			if (cursor_size > 16384)
				cache_cursor = true;

			if (pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
					hubp->funcs->hubp_update_mall_sel(hubp, 1, false);
			} else {
				// MALL not supported with Stereo3D
				hubp->funcs->hubp_update_mall_sel(hubp,
					num_ways <= dc->caps.cache_num_ways &&
					pipe->stream->link->psr_settings.psr_version == DC_PSR_VERSION_UNSUPPORTED &&
					pipe->plane_state->address.type !=  PLN_ADDR_TYPE_GRPH_STEREO &&
					!pipe->plane_state->address.tmz_surface ? 2 : 0,
							cache_cursor);
			}
		}
	}
}

/* Program the sub-viewport pipe configuration after the main / phantom pipes
 * have been programmed in hardware.
 * 1. Update force P-State for all the main pipes (disallow P-state)
 * 2. Update MALL_SEL register
 * 3. Program FORCE_ONE_ROW_FOR_FRAME for main subvp pipes
 */
void dcn32_program_mall_pipe_config(struct dc *dc, struct dc_state *context)
{
	int i;
	struct dce_hwseq *hws = dc->hwseq;

	// Don't force p-state disallow -- can't block dummy p-state

	// Update MALL_SEL register for each pipe
	if (hws && hws->funcs.update_mall_sel)
		hws->funcs.update_mall_sel(dc, context);

	// Program FORCE_ONE_ROW_FOR_FRAME and CURSOR_REQ_MODE for main subvp pipes
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct hubp *hubp = pipe->plane_res.hubp;

		if (pipe->stream && hubp && hubp->funcs->hubp_prepare_subvp_buffering) {
			/* TODO - remove setting CURSOR_REQ_MODE to 0 for legacy cases
			 *      - need to investigate single pipe MPO + SubVP case to
			 *        see if CURSOR_REQ_MODE will be back to 1 for SubVP
			 *        when it should be 0 for MPO
			 */
			if (pipe->stream->mall_stream_config.type == SUBVP_MAIN) {
				hubp->funcs->hubp_prepare_subvp_buffering(hubp, true);
			}
		}
	}
}

static void dcn32_initialize_min_clocks(struct dc *dc)
{
	struct dc_clocks *clocks = &dc->current_state->bw_ctx.bw.dcn.clk;

	clocks->dcfclk_deep_sleep_khz = DCN3_2_DCFCLK_DS_INIT_KHZ;
	clocks->dcfclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz * 1000;
	clocks->socclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].socclk_mhz * 1000;
	clocks->dramclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].memclk_mhz * 1000;
	clocks->dppclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dppclk_mhz * 1000;
	clocks->ref_dtbclk_khz = dc->clk_mgr->bw_params->clk_table.entries[0].dtbclk_mhz * 1000;
	clocks->fclk_p_state_change_support = true;
	clocks->p_state_change_support = true;
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

	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			dc->current_state,
			true);
}

void dcn32_init_hw(struct dc *dc)
{
	struct abm **abms = dc->res_pool->multiple_abms;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	int i;
	int edp_num;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;

	if (dc->clk_mgr && dc->clk_mgr->funcs->init_clocks)
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		hws->funcs.bios_golden_init(dc);
		hws->funcs.disable_vga(dc->hwseq);
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

		if (res_pool->dccg && res_pool->hubbub) {
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

		dcn32_initialize_min_clocks(dc);

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
						dc->hwss.power_down &&
						dc->hwss.edp_power_control) {
					dc->hwss.edp_backlight_control(edp_link, false);
					dc->hwss.power_down(dc);
					dc->hwss.edp_power_control(edp_link, false);
				}
			}
		} else {
			for (i = 0; i < dc->link_count; i++) {
				struct dc_link *link = dc->links[i];

				if (link->link_enc->funcs->is_dig_enabled &&
						link->link_enc->funcs->is_dig_enabled(link->link_enc) &&
						dc->hwss.power_down) {
					dc->hwss.power_down(dc);
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

		if (link->panel_cntl)
			backlight = link->panel_cntl->funcs->hw_init(link->panel_cntl);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (abms[i] != NULL && abms[i]->funcs != NULL)
			abms[i]->funcs->abm_init(abms[i], backlight);
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

	if (dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

	if (dc->clk_mgr->funcs->set_hard_max_memclk && !dc->clk_mgr->dc_mode_softmax_enabled)
		dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);

	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
				dc->res_pool->hubbub, false, false);

	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);

	if (dc->res_pool->hubbub->funcs->set_request_limit && dc->config.sdpif_request_limit_words_per_umc > 0)
		dc->res_pool->hubbub->funcs->set_request_limit(dc->res_pool->hubbub, dc->ctx->dc_bios->vram_info.num_chans, dc->config.sdpif_request_limit_words_per_umc);

	// Get DMCUB capabilities
	if (dc->ctx->dmub_srv) {
		dc_dmub_srv_query_caps_cmd(dc->ctx->dmub_srv);
		dc->caps.dmub_caps.psr = dc->ctx->dmub_srv->dmub->feature_caps.psr;
		dc->caps.dmub_caps.subvp_psr = dc->ctx->dmub_srv->dmub->feature_caps.subvp_psr_support;
		dc->caps.dmub_caps.gecc_enable = dc->ctx->dmub_srv->dmub->feature_caps.gecc_enable;
		dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch;
	}
}

static int calc_mpc_flow_ctrl_cnt(const struct dc_stream_state *stream,
		int opp_cnt)
{
	bool hblank_halved = optc2_is_two_pixels_per_containter(&stream->timing);
	int flow_ctrl_cnt;

	if (opp_cnt >= 2)
		hblank_halved = true;

	flow_ctrl_cnt = stream->timing.h_total - stream->timing.h_addressable -
			stream->timing.h_border_left -
			stream->timing.h_border_right;

	if (hblank_halved)
		flow_ctrl_cnt /= 2;

	/* ODM combine 4:1 case */
	if (opp_cnt == 4)
		flow_ctrl_cnt /= 2;

	return flow_ctrl_cnt;
}

static void update_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;

	ASSERT(dsc);
	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		opp_cnt++;

	if (enable) {
		struct dsc_config dsc_cfg;
		struct dsc_optc_config dsc_optc_cfg;
		enum optc_dsc_mode optc_dsc_mode;

		/* Enable DSC hw block */
		dsc_cfg.pic_width = (stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right) / opp_cnt;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
		ASSERT(dsc_cfg.dc_dsc_cfg.num_slices_h % opp_cnt == 0);
		dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;

		dsc->funcs->dsc_set_config(dsc, &dsc_cfg, &dsc_optc_cfg);
		dsc->funcs->dsc_enable(dsc, pipe_ctx->stream_res.opp->inst);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			struct display_stream_compressor *odm_dsc = odm_pipe->stream_res.dsc;

			ASSERT(odm_dsc);
			odm_dsc->funcs->dsc_set_config(odm_dsc, &dsc_cfg, &dsc_optc_cfg);
			odm_dsc->funcs->dsc_enable(odm_dsc, odm_pipe->stream_res.opp->inst);
		}
		dsc_cfg.dc_dsc_cfg.num_slices_h *= opp_cnt;
		dsc_cfg.pic_width *= opp_cnt;

		optc_dsc_mode = dsc_optc_cfg.is_pixel_format_444 ? OPTC_DSC_ENABLED_444 : OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED;

		/* Enable DSC in OPTC */
		DC_LOG_DSC("Setting optc DSC config for tg instance %d:", pipe_ctx->stream_res.tg->inst);
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(pipe_ctx->stream_res.tg,
							optc_dsc_mode,
							dsc_optc_cfg.bytes_per_pixel,
							dsc_optc_cfg.slice_width);
	} else {
		/* disable DSC in OPTC */
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(
				pipe_ctx->stream_res.tg,
				OPTC_DSC_DISABLED, 0, 0);

		/* disable DSC block */
		dsc->funcs->dsc_disable(pipe_ctx->stream_res.dsc);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			ASSERT(odm_pipe->stream_res.dsc);
			odm_pipe->stream_res.dsc->funcs->dsc_disable(odm_pipe->stream_res.dsc);
		}
	}
}

/*
* Given any pipe_ctx, return the total ODM combine factor, and optionally return
* the OPPids which are used
* */
static unsigned int get_odm_config(struct pipe_ctx *pipe_ctx, unsigned int *opp_instances)
{
	unsigned int opp_count = 1;
	struct pipe_ctx *odm_pipe;

	/* First get to the top pipe */
	for (odm_pipe = pipe_ctx; odm_pipe->prev_odm_pipe; odm_pipe = odm_pipe->prev_odm_pipe)
		;

	/* First pipe is always used */
	if (opp_instances)
		opp_instances[0] = odm_pipe->stream_res.opp->inst;

	/* Find and count odm pipes, if any */
	for (odm_pipe = odm_pipe->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		if (opp_instances)
			opp_instances[opp_count] = odm_pipe->stream_res.opp->inst;
		opp_count++;
	}

	return opp_count;
}

void dcn32_update_odm(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 0;
	int opp_inst[MAX_PIPES] = {0};
	bool rate_control_2x_pclk = (pipe_ctx->stream->timing.flags.INTERLACE || optc2_is_two_pixels_per_containter(&pipe_ctx->stream->timing));
	struct mpc_dwb_flow_control flow_control;
	struct mpc *mpc = dc->res_pool->mpc;
	int i;

	opp_cnt = get_odm_config(pipe_ctx, opp_inst);

	if (opp_cnt > 1)
		pipe_ctx->stream_res.tg->funcs->set_odm_combine(
				pipe_ctx->stream_res.tg,
				opp_inst, opp_cnt,
				&pipe_ctx->stream->timing);
	else
		pipe_ctx->stream_res.tg->funcs->set_odm_bypass(
				pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);

	rate_control_2x_pclk = rate_control_2x_pclk || opp_cnt > 1;
	flow_control.flow_ctrl_mode = 0;
	flow_control.flow_ctrl_cnt0 = 0x80;
	flow_control.flow_ctrl_cnt1 = calc_mpc_flow_ctrl_cnt(pipe_ctx->stream, opp_cnt);
	if (mpc->funcs->set_out_rate_control) {
		for (i = 0; i < opp_cnt; ++i) {
			mpc->funcs->set_out_rate_control(
					mpc, opp_inst[i],
					true,
					rate_control_2x_pclk,
					&flow_control);
		}
	}

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		odm_pipe->stream_res.opp->funcs->opp_pipe_clock_control(
				odm_pipe->stream_res.opp,
				true);
	}

	if (pipe_ctx->stream_res.dsc) {
		struct pipe_ctx *current_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[pipe_ctx->pipe_idx];

		update_dsc_on_stream(pipe_ctx, pipe_ctx->stream->timing.flags.DSC);

		/* Check if no longer using pipe for ODM, then need to disconnect DSC for that pipe */
		if (!pipe_ctx->next_odm_pipe && current_pipe_ctx->next_odm_pipe &&
				current_pipe_ctx->next_odm_pipe->stream_res.dsc) {
			struct display_stream_compressor *dsc = current_pipe_ctx->next_odm_pipe->stream_res.dsc;
			/* disconnect DSC block from stream */
			dsc->funcs->dsc_disconnect(dsc);
		}
	}
}

unsigned int dcn32_calculate_dccg_k1_k2_values(struct pipe_ctx *pipe_ctx, unsigned int *k1_div, unsigned int *k2_div)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	unsigned int odm_combine_factor = 0;
	bool two_pix_per_container = false;

	// For phantom pipes, use the same programming as the main pipes
	if (pipe_ctx->stream->mall_stream_config.type == SUBVP_PHANTOM) {
		stream = pipe_ctx->stream->mall_stream_config.paired_stream;
	}
	two_pix_per_container = optc2_is_two_pixels_per_containter(&stream->timing);
	odm_combine_factor = get_odm_config(pipe_ctx, NULL);

	if (stream->ctx->dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		*k1_div = PIXEL_RATE_DIV_BY_1;
		*k2_div = PIXEL_RATE_DIV_BY_1;
	} else if (dc_is_hdmi_tmds_signal(stream->signal) || dc_is_dvi_signal(stream->signal)) {
		*k1_div = PIXEL_RATE_DIV_BY_1;
		if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
			*k2_div = PIXEL_RATE_DIV_BY_2;
		else
			*k2_div = PIXEL_RATE_DIV_BY_4;
	} else if (dc_is_dp_signal(stream->signal) || dc_is_virtual_signal(stream->signal)) {
		if (two_pix_per_container) {
			*k1_div = PIXEL_RATE_DIV_BY_1;
			*k2_div = PIXEL_RATE_DIV_BY_2;
		} else {
			*k1_div = PIXEL_RATE_DIV_BY_1;
			*k2_div = PIXEL_RATE_DIV_BY_4;
			if ((odm_combine_factor == 2) || dcn32_is_dp_dig_pixel_rate_div_policy(pipe_ctx))
				*k2_div = PIXEL_RATE_DIV_BY_2;
		}
	}

	if ((*k1_div == PIXEL_RATE_DIV_NA) && (*k2_div == PIXEL_RATE_DIV_NA))
		ASSERT(false);

	return odm_combine_factor;
}

void dcn32_set_pixels_per_cycle(struct pipe_ctx *pipe_ctx)
{
	uint32_t pix_per_cycle = 1;
	uint32_t odm_combine_factor = 1;

	if (!pipe_ctx || !pipe_ctx->stream || !pipe_ctx->stream_res.stream_enc)
		return;

	odm_combine_factor = get_odm_config(pipe_ctx, NULL);
	if (optc2_is_two_pixels_per_containter(&pipe_ctx->stream->timing) || odm_combine_factor > 1
		|| dcn32_is_dp_dig_pixel_rate_div_policy(pipe_ctx))
		pix_per_cycle = 2;

	if (pipe_ctx->stream_res.stream_enc->funcs->set_input_mode)
		pipe_ctx->stream_res.stream_enc->funcs->set_input_mode(pipe_ctx->stream_res.stream_enc,
				pix_per_cycle);
}

void dcn32_resync_fifo_dccg_dio(struct dce_hwseq *hws, struct dc *dc, struct dc_state *context)
{
	unsigned int i;
	struct pipe_ctx *pipe = NULL;
	bool otg_disabled[MAX_PIPES] = {false};

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		if (pipe->stream && (pipe->stream->dpms_off || dc_is_virtual_signal(pipe->stream->signal))
			&& pipe->stream->mall_stream_config.type != SUBVP_PHANTOM) {
			pipe->stream_res.tg->funcs->disable_crtc(pipe->stream_res.tg);
			reset_sync_context_for_pipe(dc, context, i);
			otg_disabled[i] = true;
		}
	}

	hws->ctx->dc->res_pool->dccg->funcs->trigger_dio_fifo_resync(hws->ctx->dc->res_pool->dccg);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (otg_disabled[i])
			pipe->stream_res.tg->funcs->enable_crtc(pipe->stream_res.tg);
	}
}

void dcn32_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings)
{
	struct encoder_unblank_param params = {0};
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dce_hwseq *hws = link->dc->hwseq;
	struct pipe_ctx *odm_pipe;
	uint32_t pix_per_cycle = 1;

	params.opp_cnt = 1;
	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		params.opp_cnt++;

	/* only 3 items below are used by unblank */
	params.timing = pipe_ctx->stream->timing;

	params.link_settings.link_rate = link_settings->link_rate;

	if (link->dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		/* TODO - DP2.0 HW: Set ODM mode in dp hpo encoder here */
		pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_unblank(
				pipe_ctx->stream_res.hpo_dp_stream_enc,
				pipe_ctx->stream_res.tg->inst);
	} else if (dc_is_dp_signal(pipe_ctx->stream->signal)) {
		if (optc2_is_two_pixels_per_containter(&stream->timing) || params.opp_cnt > 1
			|| dcn32_is_dp_dig_pixel_rate_div_policy(pipe_ctx)) {
			params.timing.pix_clk_100hz /= 2;
			pix_per_cycle = 2;
		}
		pipe_ctx->stream_res.stream_enc->funcs->dp_set_odm_combine(
				pipe_ctx->stream_res.stream_enc, pix_per_cycle > 1);
		pipe_ctx->stream_res.stream_enc->funcs->dp_unblank(link, pipe_ctx->stream_res.stream_enc, &params);
	}

	if (link->local_sink && link->local_sink->sink_signal == SIGNAL_TYPE_EDP)
		hws->funcs.edp_backlight_control(link, true);
}

bool dcn32_is_dp_dig_pixel_rate_div_policy(struct pipe_ctx *pipe_ctx)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;

	if (!is_h_timing_divisible_by_2(pipe_ctx->stream))
		return false;

	if (dc_is_dp_signal(pipe_ctx->stream->signal) && !dc->link_srv->dp_is_128b_132b_signal(pipe_ctx) &&
		dc->debug.enable_dp_dig_pixel_rate_div_policy)
		return true;
	return false;
}

static void apply_symclk_on_tx_off_wa(struct dc_link *link)
{
	/* There are use cases where SYMCLK is referenced by OTG. For instance
	 * for TMDS signal, OTG relies SYMCLK even if TX video output is off.
	 * However current link interface will power off PHY when disabling link
	 * output. This will turn off SYMCLK generated by PHY. The workaround is
	 * to identify such case where SYMCLK is still in use by OTG when we
	 * power off PHY. When this is detected, we will temporarily power PHY
	 * back on and move PHY's SYMCLK state to SYMCLK_ON_TX_OFF by calling
	 * program_pix_clk interface. When OTG is disabled, we will then power
	 * off PHY by calling disable link output again.
	 *
	 * In future dcn generations, we plan to rework transmitter control
	 * interface so that we could have an option to set SYMCLK ON TX OFF
	 * state in one step without this workaround
	 */

	struct dc *dc = link->ctx->dc;
	struct pipe_ctx *pipe_ctx = NULL;
	uint8_t i;

	if (link->phy_state.symclk_ref_cnts.otg > 0) {
		for (i = 0; i < MAX_PIPES; i++) {
			pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx->stream && pipe_ctx->stream->link == link && pipe_ctx->top_pipe == NULL) {
				pipe_ctx->clock_source->funcs->program_pix_clk(
						pipe_ctx->clock_source,
						&pipe_ctx->stream_res.pix_clk_params,
						dc->link_srv->dp_get_encoding_format(
								&pipe_ctx->link_config.dp_link_settings),
						&pipe_ctx->pll_settings);
				link->phy_state.symclk_state = SYMCLK_ON_TX_OFF;
				break;
			}
		}
	}
}

void dcn32_disable_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	struct dc *dc = link->ctx->dc;
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);
	struct dmcu *dmcu = dc->res_pool->dmcu;

	if (signal == SIGNAL_TYPE_EDP &&
			link->dc->hwss.edp_backlight_control)
		link->dc->hwss.edp_backlight_control(link, false);
	else if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->lock_phy(dmcu);

	link_hwss->disable_link_output(link, link_res, signal);
	link->phy_state.symclk_state = SYMCLK_OFF_TX_OFF;

	if (signal == SIGNAL_TYPE_EDP &&
			link->dc->hwss.edp_backlight_control)
		link->dc->hwss.edp_power_control(link, false);
	else if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->unlock_phy(dmcu);

	dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_DISABLE_LINK_PHY);

	apply_symclk_on_tx_off_wa(link);
}

/* For SubVP the main pipe can have a viewport position change
 * without a full update. In this case we must also update the
 * viewport positions for the phantom pipe accordingly.
 */
void dcn32_update_phantom_vp_position(struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *phantom_pipe)
{
	uint32_t i;
	struct dc_plane_state *phantom_plane = phantom_pipe->plane_state;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->stream && pipe->stream->mall_stream_config.type == SUBVP_MAIN &&
				pipe->stream->mall_stream_config.paired_stream == phantom_pipe->stream) {
			if (pipe->plane_state && pipe->plane_state->update_flags.bits.position_change) {

				phantom_plane->src_rect.x = pipe->plane_state->src_rect.x;
				phantom_plane->src_rect.y = pipe->plane_state->src_rect.y;
				phantom_plane->clip_rect.x = pipe->plane_state->clip_rect.x;
				phantom_plane->dst_rect.x = pipe->plane_state->dst_rect.x;
				phantom_plane->dst_rect.y = pipe->plane_state->dst_rect.y;

				phantom_pipe->plane_state->update_flags.bits.position_change = 1;
				resource_build_scaling_params(phantom_pipe);
				return;
			}
		}
	}
}

/* Treat the phantom pipe as if it needs to be fully enabled.
 * If the pipe was previously in use but not phantom, it would
 * have been disabled earlier in the sequence so we need to run
 * the full enable sequence.
 */
void dcn32_apply_update_flags_for_phantom(struct pipe_ctx *phantom_pipe)
{
	phantom_pipe->update_flags.raw = 0;
	if (phantom_pipe->stream && phantom_pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
		if (phantom_pipe->stream && phantom_pipe->plane_state) {
			phantom_pipe->update_flags.bits.enable = 1;
			phantom_pipe->update_flags.bits.mpcc = 1;
			phantom_pipe->update_flags.bits.dppclk = 1;
			phantom_pipe->update_flags.bits.hubp_interdependent = 1;
			phantom_pipe->update_flags.bits.hubp_rq_dlg_ttu = 1;
			phantom_pipe->update_flags.bits.gamut_remap = 1;
			phantom_pipe->update_flags.bits.scaler = 1;
			phantom_pipe->update_flags.bits.viewport = 1;
			phantom_pipe->update_flags.bits.det_size = 1;
			if (!phantom_pipe->top_pipe && !phantom_pipe->prev_odm_pipe) {
				phantom_pipe->update_flags.bits.odm = 1;
				phantom_pipe->update_flags.bits.global_sync = 1;
			}
		}
	}
}

bool dcn32_dsc_pg_status(
		struct dce_hwseq *hws,
		unsigned int dsc_inst)
{
	uint32_t pwr_status = 0;

	switch (dsc_inst) {
	case 0: /* DSC0 */
		REG_GET(DOMAIN16_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 1: /* DSC1 */

		REG_GET(DOMAIN17_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 2: /* DSC2 */
		REG_GET(DOMAIN18_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	case 3: /* DSC3 */
		REG_GET(DOMAIN19_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, &pwr_status);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	return pwr_status == 0;
}

void dcn32_update_dsc_pg(struct dc *dc,
		struct dc_state *context,
		bool safe_to_disable)
{
	struct dce_hwseq *hws = dc->hwseq;
	int i;

	for (i = 0; i < dc->res_pool->res_cap->num_dsc; i++) {
		struct display_stream_compressor *dsc = dc->res_pool->dscs[i];
		bool is_dsc_ungated = hws->funcs.dsc_pg_status(hws, dsc->inst);

		if (context->res_ctx.is_dsc_acquired[i]) {
			if (!is_dsc_ungated) {
				hws->funcs.dsc_pg_control(hws, dsc->inst, true);
			}
		} else if (safe_to_disable) {
			if (is_dsc_ungated) {
				hws->funcs.dsc_pg_control(hws, dsc->inst, false);
			}
		}
	}
}

void dcn32_enable_phantom_streams(struct dc *dc, struct dc_state *context)
{
	unsigned int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		/* If an active, non-phantom pipe is being transitioned into a phantom
		 * pipe, wait for the double buffer update to complete first before we do
		 * ANY phantom pipe programming.
		 */
		if (pipe->stream && pipe->stream->mall_stream_config.type == SUBVP_PHANTOM &&
				old_pipe->stream && old_pipe->stream->mall_stream_config.type != SUBVP_PHANTOM) {
			old_pipe->stream_res.tg->funcs->wait_for_state(
					old_pipe->stream_res.tg,
					CRTC_STATE_VBLANK);
			old_pipe->stream_res.tg->funcs->wait_for_state(
					old_pipe->stream_res.tg,
					CRTC_STATE_VACTIVE);
		}
	}
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *new_pipe = &context->res_ctx.pipe_ctx[i];

		if (new_pipe->stream && new_pipe->stream->mall_stream_config.type == SUBVP_PHANTOM) {
			// If old context or new context has phantom pipes, apply
			// the phantom timings now. We can't change the phantom
			// pipe configuration safely without driver acquiring
			// the DMCUB lock first.
			dc->hwss.apply_ctx_to_hw(dc, context);
			break;
		}
	}
}

/* Blank pixel data during initialization */
void dcn32_init_blank(
		struct dc *dc,
		struct timing_generator *tg)
{
	struct dce_hwseq *hws = dc->hwseq;
	enum dc_color_space color_space;
	struct tg_color black_color = {0};
	struct output_pixel_processor *opp = NULL;
	struct output_pixel_processor *bottom_opp = NULL;
	uint32_t num_opps, opp_id_src0, opp_id_src1;
	uint32_t otg_active_width, otg_active_height;
	uint32_t i;

	/* program opp dpg blank color */
	color_space = COLOR_SPACE_SRGB;
	color_space_to_black_color(dc, color_space, &black_color);

	/* get the OTG active size */
	tg->funcs->get_otg_active_size(tg,
			&otg_active_width,
			&otg_active_height);

	/* get the OPTC source */
	tg->funcs->get_optc_source(tg, &num_opps, &opp_id_src0, &opp_id_src1);

	if (opp_id_src0 >= dc->res_pool->res_cap->num_opp) {
		ASSERT(false);
		return;
	}

	for (i = 0; i < dc->res_pool->res_cap->num_opp; i++) {
		if (dc->res_pool->opps[i] != NULL && dc->res_pool->opps[i]->inst == opp_id_src0) {
			opp = dc->res_pool->opps[i];
			break;
		}
	}

	if (num_opps == 2) {
		otg_active_width = otg_active_width / 2;

		if (opp_id_src1 >= dc->res_pool->res_cap->num_opp) {
			ASSERT(false);
			return;
		}
		for (i = 0; i < dc->res_pool->res_cap->num_opp; i++) {
			if (dc->res_pool->opps[i] != NULL && dc->res_pool->opps[i]->inst == opp_id_src1) {
				bottom_opp = dc->res_pool->opps[i];
				break;
			}
		}
	}

	if (opp && opp->funcs->opp_set_disp_pattern_generator)
		opp->funcs->opp_set_disp_pattern_generator(
				opp,
				CONTROLLER_DP_TEST_PATTERN_SOLID_COLOR,
				CONTROLLER_DP_COLOR_SPACE_UDEFINED,
				COLOR_DEPTH_UNDEFINED,
				&black_color,
				otg_active_width,
				otg_active_height,
				0);

	if (num_opps == 2) {
		if (bottom_opp && bottom_opp->funcs->opp_set_disp_pattern_generator) {
			bottom_opp->funcs->opp_set_disp_pattern_generator(
					bottom_opp,
					CONTROLLER_DP_TEST_PATTERN_SOLID_COLOR,
					CONTROLLER_DP_COLOR_SPACE_UDEFINED,
					COLOR_DEPTH_UNDEFINED,
					&black_color,
					otg_active_width,
					otg_active_height,
					0);
			hws->funcs.wait_for_blank_complete(bottom_opp);
		}
	}

	if (opp)
		hws->funcs.wait_for_blank_complete(opp);
}
