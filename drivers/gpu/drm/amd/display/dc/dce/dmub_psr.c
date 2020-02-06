/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "dmub_psr.h"
#include "dc.h"
#include "dc_dmub_srv.h"
#include "../../dmub/inc/dmub_srv.h"
#include "dmub_fw_state.h"
#include "core_types.h"
#include "ipp.h"

#define MAX_PIPES 6

/**
 * Get PSR state from firmware.
 */
static void dmub_get_psr_state(uint32_t *psr_state)
{
	// Not yet implemented
	// Trigger GPINT interrupt from firmware
}

/**
 * Enable/Disable PSR.
 */
static void dmub_set_psr_enable(struct dmub_psr *dmub, bool enable)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	cmd.psr_enable.header.type = DMUB_CMD__PSR;

	if (enable)
		cmd.psr_enable.header.sub_type = DMUB_CMD__PSR_ENABLE;
	else
		cmd.psr_enable.header.sub_type = DMUB_CMD__PSR_DISABLE;

	cmd.psr_enable.header.payload_bytes = 0; // Send header only

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd.psr_enable.header);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);
}

/**
 * Set PSR level.
 */
static void dmub_set_psr_level(struct dmub_psr *dmub, uint16_t psr_level)
{
	union dmub_rb_cmd cmd;
	uint32_t psr_state = 0;
	struct dc_context *dc = dmub->ctx;

	dmub_get_psr_state(&psr_state);

	if (psr_state == 0)
		return;

	cmd.psr_set_level.header.type = DMUB_CMD__PSR;
	cmd.psr_set_level.header.sub_type = DMUB_CMD__PSR_SET_LEVEL;
	cmd.psr_set_level.header.payload_bytes = sizeof(struct dmub_cmd_psr_set_level_data);
	cmd.psr_set_level.psr_set_level_data.psr_level = psr_level;

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd.psr_set_level.header);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);
}

/**
 * Setup PSR by programming phy registers and sending psr hw context values to firmware.
 */
static bool dmub_setup_psr(struct dmub_psr *dmub,
		struct dc_link *link,
		struct psr_context *psr_context)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;
	struct dmub_cmd_psr_copy_settings_data *copy_settings_data
		= &cmd.psr_copy_settings.psr_copy_settings_data;
	struct pipe_ctx *pipe_ctx = NULL;
	struct resource_context *res_ctx = &link->ctx->dc->current_state->res_ctx;

	for (int i = 0; i < MAX_PIPES; i++) {
		if (res_ctx &&
				res_ctx->pipe_ctx[i].stream &&
				res_ctx->pipe_ctx[i].stream->link &&
				res_ctx->pipe_ctx[i].stream->link == link &&
				res_ctx->pipe_ctx[i].stream->link->connector_signal == SIGNAL_TYPE_EDP) {
			pipe_ctx = &res_ctx->pipe_ctx[i];
			break;
		}
	}

	if (!pipe_ctx ||
			!&pipe_ctx->plane_res ||
			!&pipe_ctx->stream_res)
		return false;

	// Program DP DPHY fast training registers
	link->link_enc->funcs->psr_program_dp_dphy_fast_training(link->link_enc,
			psr_context->psrExitLinkTrainingRequired);

	// Program DP_SEC_CNTL1 register to set transmission GPS0 line num and priority to high
	link->link_enc->funcs->psr_program_secondary_packet(link->link_enc,
			psr_context->sdpTransmitLineNumDeadline);

	cmd.psr_copy_settings.header.type = DMUB_CMD__PSR;
	cmd.psr_copy_settings.header.sub_type = DMUB_CMD__PSR_COPY_SETTINGS;
	cmd.psr_copy_settings.header.payload_bytes = sizeof(struct dmub_cmd_psr_copy_settings_data);

	// Hw insts
	copy_settings_data->dpphy_inst				= psr_context->phyType;
	copy_settings_data->aux_inst				= psr_context->channel;
	copy_settings_data->digfe_inst				= psr_context->engineId;
	copy_settings_data->digbe_inst				= psr_context->transmitterId;

	copy_settings_data->mpcc_inst				= pipe_ctx->plane_res.mpcc_inst;

	if (pipe_ctx->plane_res.hubp)
		copy_settings_data->hubp_inst			= pipe_ctx->plane_res.hubp->inst;
	else
		copy_settings_data->hubp_inst			= 0;
	if (pipe_ctx->plane_res.dpp)
		copy_settings_data->dpp_inst			= pipe_ctx->plane_res.dpp->inst;
	else
		copy_settings_data->dpp_inst			= 0;
	if (pipe_ctx->stream_res.opp)
		copy_settings_data->opp_inst			= pipe_ctx->stream_res.opp->inst;
	else
		copy_settings_data->opp_inst			= 0;
	if (pipe_ctx->stream_res.tg)
		copy_settings_data->otg_inst			= pipe_ctx->stream_res.tg->inst;
	else
		copy_settings_data->otg_inst			= 0;

	// Misc
	copy_settings_data->psr_level				= psr_context->psr_level.u32all;
	copy_settings_data->hyst_frames				= psr_context->timehyst_frames;
	copy_settings_data->hyst_lines				= psr_context->hyst_lines;
	copy_settings_data->phy_type				= psr_context->phyType;
	copy_settings_data->aux_repeat				= psr_context->aux_repeats;
	copy_settings_data->smu_optimizations_en	= psr_context->allow_smu_optimizations;
	copy_settings_data->skip_wait_for_pll_lock	= psr_context->skipPsrWaitForPllLock;
	copy_settings_data->frame_delay				= psr_context->frame_delay;
	copy_settings_data->smu_phy_id				= psr_context->smuPhyId;
	copy_settings_data->num_of_controllers		= psr_context->numberOfControllers;
	copy_settings_data->frame_cap_ind			= psr_context->psrFrameCaptureIndicationReq;
	copy_settings_data->phy_num					= psr_context->frame_delay & 0x7;
	copy_settings_data->link_rate				= psr_context->frame_delay & 0xF;

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd.psr_copy_settings.header);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);

	return true;
}

static const struct dmub_psr_funcs psr_funcs = {
	.set_psr_enable			= dmub_set_psr_enable,
	.setup_psr				= dmub_setup_psr,
	.get_psr_state			= dmub_get_psr_state,
	.set_psr_level			= dmub_set_psr_level,
};

/**
 * Construct PSR object.
 */
static void dmub_psr_construct(struct dmub_psr *psr, struct dc_context *ctx)
{
	psr->ctx = ctx;
	psr->funcs = &psr_funcs;
}

/**
 * Allocate and initialize PSR object.
 */
struct dmub_psr *dmub_psr_create(struct dc_context *ctx)
{
	struct dmub_psr *psr = kzalloc(sizeof(struct dmub_psr), GFP_KERNEL);

	if (psr == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dmub_psr_construct(psr, ctx);

	return psr;
}

/**
 * Deallocate PSR object.
 */
void dmub_psr_destroy(struct dmub_psr **dmub)
{
	kfree(dmub);
	*dmub = NULL;
}
