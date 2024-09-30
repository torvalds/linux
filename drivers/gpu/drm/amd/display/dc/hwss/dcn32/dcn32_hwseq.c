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
#include "dce/dmub_hw_lock_mgr.h"
#include "dcn32/dcn32_resource.h"
#include "link.h"
#include "../dcn20/dcn20_hwseq.h"
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

void dcn32_dsc_pg_control(
		struct dce_hwseq *hws,
		unsigned int dsc_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl = 0;
	struct dc *dc = hws->ctx->dc;

	if (dc->debug.disable_dsc_power_gate)
		return;

	if (!dc->debug.enable_double_buffered_dsc_pg_support)
		return;

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	DC_LOG_DSC("%s DSC power gate for inst %d", power_gate ? "enable" : "disable", dsc_inst);
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
	uint32_t num_ways = 0;
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
	} else if (dc->res_pool->funcs->calculate_mall_ways_from_bytes) {
		num_ways = dc->res_pool->funcs->calculate_mall_ways_from_bytes(dc, mall_ss_size_bytes);
	} else {
		num_ways = 0;
	}

	return num_ways;
}

bool dcn32_apply_idle_power_optimizations(struct dc *dc, bool enable)
{
	union dmub_rb_cmd cmd;
	uint8_t i;
	uint32_t ways;
	int j;
	bool mall_ss_unsupported = false;
	struct dc_plane_state *plane = NULL;

	if (!dc->ctx->dmub_srv)
		return false;

	for (i = 0; i < dc->current_state->stream_count; i++) {
		/* MALL SS messaging is not supported with PSR at this time */
		if (dc->current_state->streams[i] != NULL &&
				dc->current_state->streams[i]->link->psr_settings.psr_version != DC_PSR_VERSION_UNSUPPORTED &&
				(dc->current_state->stream_count > 1 || (!dc->current_state->streams[i]->dpms_off &&
						dc->current_state->stream_status[i].plane_count > 0)))
			return false;
	}

	if (enable) {
		/* 1. Check no memory request case for CAB.
		 * If no memory request case, send CAB_ACTION NO_DF_REQ DMUB message
		 */
		if (dcn32_check_no_memory_request_for_cab(dc)) {
			/* Enable no-memory-requests case */
			memset(&cmd, 0, sizeof(cmd));
			cmd.cab.header.type = DMUB_CMD__CAB_FOR_SS;
			cmd.cab.header.sub_type = DMUB_CMD__CAB_NO_DCN_REQ;
			cmd.cab.header.payload_bytes = sizeof(cmd.cab) - sizeof(cmd.cab.header);

			dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_NO_WAIT);

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
			cmd.cab.cab_alloc_ways = (uint8_t)ways;

			dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_NO_WAIT);

			return true;
		}

		return false;
	}

	/* Disable CAB */
	memset(&cmd, 0, sizeof(cmd));
	cmd.cab.header.type = DMUB_CMD__CAB_FOR_SS;
	cmd.cab.header.sub_type = DMUB_CMD__CAB_NO_IDLE_OPTIMIZATION;
	cmd.cab.header.payload_bytes =
			sizeof(cmd.cab) - sizeof(cmd.cab.header);

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

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

		if (pipe_ctx->stream && dc_state_get_pipe_subvp_type(context, pipe_ctx) == SUBVP_MAIN) {
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
	enum mall_stream_type pipe_mall_type = SUBVP_NONE;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];
		pipe_mall_type = dc_state_get_pipe_subvp_type(context, pipe);

		if (pipe->stream && pipe->plane_state && pipe_mall_type == SUBVP_MAIN) {
			subvp_in_use = true;
			break;
		}
	}

	if (top_pipe_to_program && top_pipe_to_program->stream && top_pipe_to_program->plane_state) {
		if (dc_state_get_pipe_subvp_type(context, top_pipe_to_program) == SUBVP_MAIN &&
				top_pipe_to_program->plane_state->flip_immediate)
			subvp_immediate_flip = true;
	}

	// Don't need to lock for DRR VSYNC flips -- FW will wait for DRR pending update cleared.
	if ((subvp_in_use && (should_lock_all_pipes || subvp_immediate_flip)) || (!subvp_in_use && subvp_prev_use)) {
		union dmub_inbox0_cmd_lock_hw hw_lock_cmd = { 0 };

		if (!lock) {
			for (i = 0; i < dc->res_pool->pipe_count; i++) {
				pipe = &context->res_ctx.pipe_ctx[i];
				if (pipe->stream && pipe->plane_state && pipe_mall_type == SUBVP_MAIN &&
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
	bool subvp_immediate_flip = params->subvp_pipe_control_lock_fast_params.subvp_immediate_flip;

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
	const struct pwl_params *lut_params = NULL;

	// 1D LUT
	if (plane_state->blend_tf.type == TF_TYPE_HWPWL)
		lut_params = &plane_state->blend_tf.pwl;
	else if (plane_state->blend_tf.type == TF_TYPE_DISTRIBUTED_POINTS) {
		result = cm3_helper_translate_curve_to_hw_format(&plane_state->blend_tf,
				&dpp_base->regamma_params, false);
		if (!result)
			return result;

		lut_params = &dpp_base->regamma_params;
	}
	mpc->funcs->program_1dlut(mpc, lut_params, mpcc_id);
	lut_params = NULL;

	// Shaper
	if (plane_state->in_shaper_func.type == TF_TYPE_HWPWL)
		lut_params = &plane_state->in_shaper_func.pwl;
	else if (plane_state->in_shaper_func.type == TF_TYPE_DISTRIBUTED_POINTS) {
		// TODO: dpp_base replace
		ASSERT(false);
		cm3_helper_translate_curve_to_hw_format(&plane_state->in_shaper_func,
				&dpp_base->shaper_params, true);
		lut_params = &dpp_base->shaper_params;
	}

	mpc->funcs->program_shaper(mpc, lut_params, mpcc_id);

	// 3D
	if (plane_state->lut3d_func.state.bits.initialized == 1)
		result = mpc->funcs->program_3dlut(mpc, &plane_state->lut3d_func.lut_3d, mpcc_id);
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
	const struct pwl_params *params = NULL;

	if (mpc == NULL || plane_state == NULL)
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
	const struct pwl_params *params = NULL;
	bool ret = false;

	/* program OGAM or 3DLUT only for the top pipe*/
	if (resource_is_pipe_type(pipe_ctx, OPP_HEAD)) {
		/*program shaper and 3dlut in MPC*/
		ret = dcn32_set_mpc_shaper_3dlut(pipe_ctx, stream);
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
		struct dc_stream_status *stream_status = NULL;

		if (pipe->stream)
			stream_status = dc_state_get_stream_status(context, pipe->stream);

		if (!pipe->stream || !(dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_MAIN ||
		    (stream_status && stream_status->fpo_in_use))) {
			if (hubp && hubp->funcs->hubp_update_force_pstate_disallow)
				hubp->funcs->hubp_update_force_pstate_disallow(hubp, false);
			if (hubp && hubp->funcs->hubp_update_force_cursor_pstate_disallow)
				hubp->funcs->hubp_update_force_cursor_pstate_disallow(hubp, false);
		}
	}

	/* Loop through each pipe -- for each subvp main pipe force p-state allow equal to false.
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		struct hubp *hubp = pipe->plane_res.hubp;
		struct dc_stream_status *stream_status = NULL;
		struct dc_stream_status *old_stream_status = NULL;

		/* Today for MED update type we do not call update clocks. However, for FPO
		 * the assumption is that update clocks should be called to disable P-State
		 * switch before any HW programming since FPO in FW and driver are not
		 * synchronized. This causes an issue where on a MED update, an FPO P-State
		 * switch could be taking place, then driver forces P-State disallow in the below
		 * code and prevents FPO from completing the sequence. In this case we add a check
		 * to avoid re-programming (and thus re-setting) the P-State force register by
		 * only reprogramming if the pipe was not previously Subvp or FPO. The assumption
		 * is that the P-State force register should be programmed correctly the first
		 * time SubVP / FPO was enabled, so there's no need to update / reset it if the
		 * pipe config has never exited SubVP / FPO.
		 */
		if (pipe->stream)
			stream_status = dc_state_get_stream_status(context, pipe->stream);
		if (old_pipe->stream)
			old_stream_status = dc_state_get_stream_status(dc->current_state, old_pipe->stream);

		if (pipe->stream && (dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_MAIN ||
				(stream_status && stream_status->fpo_in_use)) &&
				(!old_pipe->stream || (dc_state_get_pipe_subvp_type(dc->current_state, old_pipe) != SUBVP_MAIN &&
				(old_stream_status && !old_stream_status->fpo_in_use)))) {
			if (hubp && hubp->funcs->hubp_update_force_pstate_disallow)
				hubp->funcs->hubp_update_force_pstate_disallow(hubp, true);
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

			if (dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM) {
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
			if (dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_MAIN)
				hubp->funcs->hubp_prepare_subvp_buffering(hubp, true);
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

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->set_hard_max_memclk &&
	    !dc->clk_mgr->dc_mode_softmax_enabled)
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
		dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver;

		/* for DCN401 testing only */
		dc->caps.dmub_caps.fams_ver = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver;
		if (dc->caps.dmub_caps.fams_ver == 2) {
			/* FAMS2 is enabled */
			dc->debug.fams2_config.bits.enable &= true;
		} else if (dc->ctx->dmub_srv->dmub->fw_version <
				DMUB_FW_VERSION(7, 0, 35)) {
			/* FAMS2 is disabled */
			dc->debug.fams2_config.bits.enable = false;
			if (dc->debug.using_dml2 && dc->res_pool->funcs->update_bw_bounding_box) {
				/* update bounding box if FAMS2 disabled */
				dc->res_pool->funcs->update_bw_bounding_box(dc, dc->clk_mgr->bw_params);
			}
			dc->debug.force_disable_subvp = true;
			dc->debug.disable_fpo_optimizations = true;
		}
	}
}

void dcn32_update_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;
	struct dccg *dccg = dc->res_pool->dccg;
	/* It has been found that when DSCCLK is lower than 16Mhz, we will get DCN
	 * register access hung. When DSCCLk is based on refclk, DSCCLk is always a
	 * fixed value higher than 16Mhz so the issue doesn't occur. When DSCCLK is
	 * generated by DTO, DSCCLK would be based on 1/3 dispclk. For small timings
	 * with DSC such as 480p60Hz, the dispclk could be low enough to trigger
	 * this problem. We are implementing a workaround here to keep using dscclk
	 * based on fixed value refclk when timing is smaller than 3x16Mhz (i.e
	 * 48Mhz) pixel clock to avoid hitting this problem.
	 */
	bool should_use_dto_dscclk = (dccg->funcs->set_dto_dscclk != NULL) &&
			stream->timing.pix_clk_100hz > 480000;

	ASSERT(dsc);
	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		opp_cnt++;

	if (enable) {
		struct dsc_config dsc_cfg;
		struct dsc_optc_config dsc_optc_cfg = {0};
		enum optc_dsc_mode optc_dsc_mode;
		struct dcn_dsc_state dsc_state = {0};

		if (!dsc) {
			DC_LOG_DSC("DSC is NULL for tg instance %d:", pipe_ctx->stream_res.tg->inst);
			return;
		}

		if (dsc->funcs->dsc_read_state) {
			dsc->funcs->dsc_read_state(dsc, &dsc_state);
			if (!dsc_state.dsc_fw_en) {
				DC_LOG_DSC("DSC has been disabled for tg instance %d:", pipe_ctx->stream_res.tg->inst);
				return;
			}
		}

		/* Enable DSC hw block */
		dsc_cfg.pic_width = (stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right) / opp_cnt;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
		ASSERT(dsc_cfg.dc_dsc_cfg.num_slices_h % opp_cnt == 0);
		dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;

		if (should_use_dto_dscclk)
			dccg->funcs->set_dto_dscclk(dccg, dsc->inst);
		dsc->funcs->dsc_set_config(dsc, &dsc_cfg, &dsc_optc_cfg);
		dsc->funcs->dsc_enable(dsc, pipe_ctx->stream_res.opp->inst);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			struct display_stream_compressor *odm_dsc = odm_pipe->stream_res.dsc;

			ASSERT(odm_dsc);
			if (should_use_dto_dscclk)
				dccg->funcs->set_dto_dscclk(dccg, odm_dsc->inst);
			odm_dsc->funcs->dsc_set_config(odm_dsc, &dsc_cfg, &dsc_optc_cfg);
			odm_dsc->funcs->dsc_enable(odm_dsc, odm_pipe->stream_res.opp->inst);
		}
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

		/* only disconnect DSC block, DSC is disabled when OPP head pipe is reset */
		dsc->funcs->dsc_disconnect(pipe_ctx->stream_res.dsc);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			ASSERT(odm_pipe->stream_res.dsc);
			odm_pipe->stream_res.dsc->funcs->dsc_disconnect(odm_pipe->stream_res.dsc);
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
	int odm_slice_width = resource_get_odm_slice_dst_width(pipe_ctx, false);
	int last_odm_slice_width = resource_get_odm_slice_dst_width(pipe_ctx, true);

	opp_cnt = get_odm_config(pipe_ctx, opp_inst);

	if (opp_cnt > 1)
		pipe_ctx->stream_res.tg->funcs->set_odm_combine(
				pipe_ctx->stream_res.tg,
				opp_inst, opp_cnt,
				odm_slice_width, last_odm_slice_width);
	else
		pipe_ctx->stream_res.tg->funcs->set_odm_bypass(
				pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		odm_pipe->stream_res.opp->funcs->opp_pipe_clock_control(
				odm_pipe->stream_res.opp,
				true);
		odm_pipe->stream_res.opp->funcs->opp_program_left_edge_extra_pixel(
				odm_pipe->stream_res.opp,
				pipe_ctx->stream->timing.pixel_encoding,
				resource_is_pipe_type(odm_pipe, OTG_MASTER));
	}

	if (pipe_ctx->stream_res.dsc) {
		struct pipe_ctx *current_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[pipe_ctx->pipe_idx];

		dcn32_update_dsc_on_stream(pipe_ctx, pipe_ctx->stream->timing.flags.DSC);

		/* Check if no longer using pipe for ODM, then need to disconnect DSC for that pipe */
		if (!pipe_ctx->next_odm_pipe && current_pipe_ctx->next_odm_pipe &&
				current_pipe_ctx->next_odm_pipe->stream_res.dsc) {
			struct display_stream_compressor *dsc = current_pipe_ctx->next_odm_pipe->stream_res.dsc;

			/* disconnect DSC block from stream */
			dsc->funcs->dsc_disconnect(dsc);
		}
	}

	if (!resource_is_pipe_type(pipe_ctx, DPP_PIPE))
		/*
		 * blank pattern is generated by OPP, reprogram blank pattern
		 * due to OPP count change
		 */
		dc->hwseq->funcs.blank_pixel_data(dc, pipe_ctx, true);
}

unsigned int dcn32_calculate_dccg_k1_k2_values(struct pipe_ctx *pipe_ctx, unsigned int *k1_div, unsigned int *k2_div)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	unsigned int odm_combine_factor = 0;
	bool two_pix_per_container = false;

	two_pix_per_container = pipe_ctx->stream_res.tg->funcs->is_two_pixels_per_container(&stream->timing);
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

void dcn32_calculate_pix_rate_divider(
		struct dc *dc,
		struct dc_state *context,
		const struct dc_stream_state *stream)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct pipe_ctx *pipe_ctx = NULL;
	unsigned int k1_div = PIXEL_RATE_DIV_NA;
	unsigned int k2_div = PIXEL_RATE_DIV_NA;

	pipe_ctx = resource_get_otg_master_for_stream(&context->res_ctx, stream);

	if (pipe_ctx) {

		if (hws->funcs.calculate_dccg_k1_k2_values)
			hws->funcs.calculate_dccg_k1_k2_values(pipe_ctx, &k1_div, &k2_div);

		pipe_ctx->pixel_rate_divider.div_factor1 = k1_div;
		pipe_ctx->pixel_rate_divider.div_factor2 = k2_div;
	}
}

void dcn32_resync_fifo_dccg_dio(struct dce_hwseq *hws, struct dc *dc, struct dc_state *context, unsigned int current_pipe_idx)
{
	unsigned int i;
	struct pipe_ctx *pipe = NULL;
	bool otg_disabled[MAX_PIPES] = {false};
	struct dc_state *dc_state = NULL;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (i <= current_pipe_idx) {
			pipe = &context->res_ctx.pipe_ctx[i];
			dc_state = context;
		} else {
			pipe = &dc->current_state->res_ctx.pipe_ctx[i];
			dc_state = dc->current_state;
		}

		if (!resource_is_pipe_type(pipe, OTG_MASTER))
			continue;

		if ((pipe->stream->dpms_off || dc_is_virtual_signal(pipe->stream->signal))
			&& dc_state_get_pipe_subvp_type(dc_state, pipe) != SUBVP_PHANTOM) {
			pipe->stream_res.tg->funcs->disable_crtc(pipe->stream_res.tg);
			reset_sync_context_for_pipe(dc, context, i);
			otg_disabled[i] = true;
		}
	}

	hws->ctx->dc->res_pool->dccg->funcs->trigger_dio_fifo_resync(hws->ctx->dc->res_pool->dccg);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (i <= current_pipe_idx)
			pipe = &context->res_ctx.pipe_ctx[i];
		else
			pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (otg_disabled[i]) {
			int opp_inst[MAX_PIPES] = { pipe->stream_res.opp->inst };
			int opp_cnt = 1;
			int last_odm_slice_width = resource_get_odm_slice_dst_width(pipe, true);
			int odm_slice_width = resource_get_odm_slice_dst_width(pipe, false);
			struct pipe_ctx *odm_pipe;

			for (odm_pipe = pipe->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
				opp_inst[opp_cnt] = odm_pipe->stream_res.opp->inst;
				opp_cnt++;
			}
			if (opp_cnt > 1)
				pipe->stream_res.tg->funcs->set_odm_combine(
						pipe->stream_res.tg,
						opp_inst, opp_cnt,
						odm_slice_width,
						last_odm_slice_width);
			pipe->stream_res.tg->funcs->enable_crtc(pipe->stream_res.tg);
		}
	}

	dc_trigger_sync(dc, dc->current_state);
}

void dcn32_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings)
{
	struct encoder_unblank_param params = {0};
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dce_hwseq *hws = link->dc->hwseq;
	struct pipe_ctx *odm_pipe;

	params.opp_cnt = 1;
	params.pix_per_cycle = pipe_ctx->stream_res.pix_clk_params.dio_se_pix_per_cycle;

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
		if (pipe_ctx->stream_res.tg->funcs->is_two_pixels_per_container(&stream->timing) ||
			params.opp_cnt > 1) {
			params.timing.pix_clk_100hz /= 2;
			params.pix_per_cycle = 2;
		}
		pipe_ctx->stream_res.stream_enc->funcs->dp_set_odm_combine(
				pipe_ctx->stream_res.stream_enc, params.pix_per_cycle > 1);
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
			if (resource_is_pipe_type(pipe_ctx, OPP_HEAD) && pipe_ctx->stream->link == link) {
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
			link->dc->hwss.edp_backlight_control &&
			!link->skip_implict_edp_power_control)
		link->dc->hwss.edp_backlight_control(link, false);
	else if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->lock_phy(dmcu);

	link_hwss->disable_link_output(link, link_res, signal);
	link->phy_state.symclk_state = SYMCLK_OFF_TX_OFF;

	if (signal == SIGNAL_TYPE_EDP &&
			link->dc->hwss.edp_backlight_control &&
			!link->skip_implict_edp_power_control)
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

		if (pipe->stream && dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_MAIN &&
				dc_state_get_paired_subvp_stream(context, pipe->stream) == phantom_pipe->stream) {
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
	if (resource_is_pipe_type(phantom_pipe, DPP_PIPE)) {
		phantom_pipe->update_flags.bits.enable = 1;
		phantom_pipe->update_flags.bits.mpcc = 1;
		phantom_pipe->update_flags.bits.dppclk = 1;
		phantom_pipe->update_flags.bits.hubp_interdependent = 1;
		phantom_pipe->update_flags.bits.hubp_rq_dlg_ttu = 1;
		phantom_pipe->update_flags.bits.gamut_remap = 1;
		phantom_pipe->update_flags.bits.scaler = 1;
		phantom_pipe->update_flags.bits.viewport = 1;
		phantom_pipe->update_flags.bits.det_size = 1;
		if (resource_is_pipe_type(phantom_pipe, OTG_MASTER)) {
			phantom_pipe->update_flags.bits.odm = 1;
			phantom_pipe->update_flags.bits.global_sync = 1;
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

void dcn32_disable_phantom_streams(struct dc *dc, struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	int i;

	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!pipe_ctx_old->stream)
			continue;

		if (dc_state_get_pipe_subvp_type(dc->current_state, pipe_ctx_old) != SUBVP_PHANTOM)
			continue;

		if (pipe_ctx_old->top_pipe || pipe_ctx_old->prev_odm_pipe)
			continue;

		if (!pipe_ctx->stream || pipe_need_reprogram(pipe_ctx_old, pipe_ctx) ||
				(pipe_ctx->stream && dc_state_get_pipe_subvp_type(context, pipe_ctx) != SUBVP_PHANTOM)) {
			struct clock_source *old_clk = pipe_ctx_old->clock_source;

			if (hws->funcs.reset_back_end_for_pipe)
				hws->funcs.reset_back_end_for_pipe(dc, pipe_ctx_old, dc->current_state);
			if (hws->funcs.enable_stream_gating)
				hws->funcs.enable_stream_gating(dc, pipe_ctx_old);
			if (old_clk)
				old_clk->funcs->cs_power_down(old_clk);
		}
	}
}

void dcn32_enable_phantom_streams(struct dc *dc, struct dc_state *context)
{
	unsigned int i;
	enum dc_status status = DC_OK;
	struct dce_hwseq *hws = dc->hwseq;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		/* If an active, non-phantom pipe is being transitioned into a phantom
		 * pipe, wait for the double buffer update to complete first before we do
		 * ANY phantom pipe programming.
		 */
		if (pipe->stream && dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM &&
				old_pipe->stream && dc_state_get_pipe_subvp_type(dc->current_state, old_pipe) != SUBVP_PHANTOM) {
			old_pipe->stream_res.tg->funcs->wait_for_state(
					old_pipe->stream_res.tg,
					CRTC_STATE_VBLANK);
			old_pipe->stream_res.tg->funcs->wait_for_state(
					old_pipe->stream_res.tg,
					CRTC_STATE_VACTIVE);
		}
	}
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx_old =
					&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		if (dc_state_get_pipe_subvp_type(context, pipe_ctx) != SUBVP_PHANTOM)
			continue;

		if (pipe_ctx->stream == pipe_ctx_old->stream &&
			pipe_ctx->stream->link->link_state_valid) {
			continue;
		}

		if (pipe_ctx_old->stream && !pipe_need_reprogram(pipe_ctx_old, pipe_ctx))
			continue;

		if (pipe_ctx->top_pipe || pipe_ctx->prev_odm_pipe)
			continue;

		if (hws->funcs.apply_single_controller_ctx_to_hw)
			status = hws->funcs.apply_single_controller_ctx_to_hw(
					pipe_ctx,
					context,
					dc);

		ASSERT(status == DC_OK);

#ifdef CONFIG_DRM_AMD_DC_FP
		if (hws->funcs.resync_fifo_dccg_dio)
			hws->funcs.resync_fifo_dccg_dio(hws, dc, context, i);
#endif
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
	uint32_t otg_active_width = 0, otg_active_height = 0;
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

void dcn32_blank_phantom(struct dc *dc,
		struct timing_generator *tg,
		int width,
		int height)
{
	struct dce_hwseq *hws = dc->hwseq;
	enum dc_color_space color_space;
	struct tg_color black_color = {0};
	struct output_pixel_processor *opp = NULL;
	uint32_t num_opps, opp_id_src0, opp_id_src1;
	uint32_t otg_active_width, otg_active_height;
	uint32_t i;

	/* program opp dpg blank color */
	color_space = COLOR_SPACE_SRGB;
	color_space_to_black_color(dc, color_space, &black_color);

	otg_active_width = width;
	otg_active_height = height;

	/* get the OPTC source */
	tg->funcs->get_optc_source(tg, &num_opps, &opp_id_src0, &opp_id_src1);
	ASSERT(opp_id_src0 < dc->res_pool->res_cap->num_opp);

	for (i = 0; i < dc->res_pool->res_cap->num_opp; i++) {
		if (dc->res_pool->opps[i] != NULL && dc->res_pool->opps[i]->inst == opp_id_src0) {
			opp = dc->res_pool->opps[i];
			break;
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

	if (tg->funcs->is_tg_enabled(tg))
		hws->funcs.wait_for_blank_complete(opp);
}

/* phantom stream id's can change often, but can be identical between contexts.
*  This function checks for the condition the streams are identical to avoid
*  redundant pipe transitions.
*/
static bool is_subvp_phantom_topology_transition_seamless(
	const struct dc_state *cur_ctx,
	const struct dc_state *new_ctx,
	const struct pipe_ctx *cur_pipe,
	const struct pipe_ctx *new_pipe)
{
	enum mall_stream_type cur_pipe_type = dc_state_get_pipe_subvp_type(cur_ctx, cur_pipe);
	enum mall_stream_type new_pipe_type = dc_state_get_pipe_subvp_type(new_ctx, new_pipe);

	const struct dc_stream_state *cur_paired_stream = dc_state_get_paired_subvp_stream(cur_ctx, cur_pipe->stream);
	const struct dc_stream_state *new_paired_stream = dc_state_get_paired_subvp_stream(new_ctx, new_pipe->stream);

	return cur_pipe_type == SUBVP_PHANTOM &&
			cur_pipe_type == new_pipe_type &&
			cur_paired_stream && new_paired_stream &&
			cur_paired_stream->stream_id == new_paired_stream->stream_id;
}

bool dcn32_is_pipe_topology_transition_seamless(struct dc *dc,
		const struct dc_state *cur_ctx,
		const struct dc_state *new_ctx)
{
	int i;
	const struct pipe_ctx *cur_pipe, *new_pipe;
	bool is_seamless = true;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		cur_pipe = &cur_ctx->res_ctx.pipe_ctx[i];
		new_pipe = &new_ctx->res_ctx.pipe_ctx[i];

		if (resource_is_pipe_type(cur_pipe, FREE_PIPE) ||
				resource_is_pipe_type(new_pipe, FREE_PIPE))
			/* adding or removing free pipes is always seamless */
			continue;
		else if (resource_is_pipe_type(cur_pipe, OTG_MASTER)) {
			if (resource_is_pipe_type(new_pipe, OTG_MASTER))
				if (cur_pipe->stream->stream_id == new_pipe->stream->stream_id ||
						is_subvp_phantom_topology_transition_seamless(cur_ctx, new_ctx, cur_pipe, new_pipe))
				/* OTG master with the same stream is seamless */
					continue;
		} else if (resource_is_pipe_type(cur_pipe, OPP_HEAD)) {
			if (resource_is_pipe_type(new_pipe, OPP_HEAD)) {
				if (cur_pipe->stream_res.tg == new_pipe->stream_res.tg)
					/*
					 * OPP heads sharing the same timing
					 * generator is seamless
					 */
					continue;
			}
		} else if (resource_is_pipe_type(cur_pipe, DPP_PIPE)) {
			if (resource_is_pipe_type(new_pipe, DPP_PIPE)) {
				if (cur_pipe->stream_res.opp == new_pipe->stream_res.opp)
					/*
					 * DPP pipes sharing the same OPP head is
					 * seamless
					 */
					continue;
			}
		}

		/*
		 * This pipe's transition doesn't fall under any seamless
		 * conditions
		 */
		is_seamless = false;
		break;
	}

	return is_seamless;
}

void dcn32_prepare_bandwidth(struct dc *dc,
	struct dc_state *context)
{
	bool p_state_change_support = context->bw_ctx.bw.dcn.clk.p_state_change_support;
	/* Any transition into an FPO config should disable MCLK switching first to avoid
	 * driver and FW P-State synchronization issues.
	 */
	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching || dc->clk_mgr->clks.fw_based_mclk_switching) {
		dc->optimized_required = true;
		context->bw_ctx.bw.dcn.clk.p_state_change_support = false;
	}

	if (dc->clk_mgr->dc_mode_softmax_enabled)
		if (dc->clk_mgr->clks.dramclk_khz <= dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000 &&
				context->bw_ctx.bw.dcn.clk.dramclk_khz > dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000)
			dc->clk_mgr->funcs->set_max_memclk(dc->clk_mgr, dc->clk_mgr->bw_params->clk_table.entries[dc->clk_mgr->bw_params->clk_table.num_entries - 1].memclk_mhz);

	dcn20_prepare_bandwidth(dc, context);

	if (!context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching)
		dc_dmub_srv_p_state_delegate(dc, false, context);

	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching || dc->clk_mgr->clks.fw_based_mclk_switching) {
		/* After disabling P-State, restore the original value to ensure we get the correct P-State
		 * on the next optimize.
		 */
		context->bw_ctx.bw.dcn.clk.p_state_change_support = p_state_change_support;
	}
}

void dcn32_interdependent_update_lock(struct dc *dc,
		struct dc_state *context, bool lock)
{
	unsigned int i;
	struct pipe_ctx *pipe;
	struct timing_generator *tg;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];
		tg = pipe->stream_res.tg;

		if (!resource_is_pipe_type(pipe, OTG_MASTER) ||
				!tg->funcs->is_tg_enabled(tg) ||
				dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM)
			continue;

		if (lock)
			dc->hwss.pipe_control_lock(dc, pipe, true);
		else
			dc->hwss.pipe_control_lock(dc, pipe, false);
	}
}

void dcn32_program_outstanding_updates(struct dc *dc,
		struct dc_state *context)
{
	struct hubbub *hubbub = dc->res_pool->hubbub;

	/* update compbuf if required */
	if (hubbub->funcs->program_compbuf_size)
		hubbub->funcs->program_compbuf_size(hubbub, context->bw_ctx.bw.dcn.compbuf_size_kb, true);
}
