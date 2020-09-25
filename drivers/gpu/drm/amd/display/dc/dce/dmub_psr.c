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
#include "dmub/dmub_srv.h"
#include "core_types.h"

#define MAX_PIPES 6

/**
 * Convert dmcub psr state to dmcu psr state.
 */
static void convert_psr_state(uint32_t *psr_state)
{
	if (*psr_state == 0)
		*psr_state = 0;
	else if (*psr_state == 0x10)
		*psr_state = 1;
	else if (*psr_state == 0x11)
		*psr_state = 2;
	else if (*psr_state == 0x20)
		*psr_state = 3;
	else if (*psr_state == 0x21)
		*psr_state = 4;
	else if (*psr_state == 0x30)
		*psr_state = 5;
	else if (*psr_state == 0x31)
		*psr_state = 6;
	else if (*psr_state == 0x40)
		*psr_state = 7;
	else if (*psr_state == 0x41)
		*psr_state = 8;
	else if (*psr_state == 0x42)
		*psr_state = 9;
	else if (*psr_state == 0x43)
		*psr_state = 10;
	else if (*psr_state == 0x44)
		*psr_state = 11;
	else if (*psr_state == 0x50)
		*psr_state = 12;
	else if (*psr_state == 0x51)
		*psr_state = 13;
	else if (*psr_state == 0x52)
		*psr_state = 14;
	else if (*psr_state == 0x53)
		*psr_state = 15;
}

/**
 * Get PSR state from firmware.
 */
static void dmub_psr_get_state(struct dmub_psr *dmub, uint32_t *psr_state)
{
	struct dmub_srv *srv = dmub->ctx->dmub_srv->dmub;

	// Send gpint command and wait for ack
	dmub_srv_send_gpint_command(srv, DMUB_GPINT__GET_PSR_STATE, 0, 30);

	dmub_srv_get_gpint_response(srv, psr_state);

	convert_psr_state(psr_state);
}

/**
 * Set PSR version.
 */
static bool dmub_psr_set_version(struct dmub_psr *dmub, struct dc_stream_state *stream)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	if (stream->link->psr_settings.psr_version == DC_PSR_VERSION_UNSUPPORTED)
		return false;

	cmd.psr_set_version.header.type = DMUB_CMD__PSR;
	cmd.psr_set_version.header.sub_type = DMUB_CMD__PSR_SET_VERSION;
	switch (stream->link->psr_settings.psr_version) {
	case DC_PSR_VERSION_1:
		cmd.psr_set_version.psr_set_version_data.version = PSR_VERSION_1;
		break;
	case DC_PSR_VERSION_UNSUPPORTED:
	default:
		cmd.psr_set_version.psr_set_version_data.version = PSR_VERSION_UNSUPPORTED;
		break;
	}
	cmd.psr_set_version.header.payload_bytes = sizeof(struct dmub_cmd_psr_set_version_data);

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);

	return true;
}

/**
 * Enable/Disable PSR.
 */
static void dmub_psr_enable(struct dmub_psr *dmub, bool enable, bool wait)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;
	uint32_t retry_count, psr_state = 0;

	cmd.psr_enable.header.type = DMUB_CMD__PSR;

	if (enable)
		cmd.psr_enable.header.sub_type = DMUB_CMD__PSR_ENABLE;
	else
		cmd.psr_enable.header.sub_type = DMUB_CMD__PSR_DISABLE;

	cmd.psr_enable.header.payload_bytes = 0; // Send header only

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);

	/* Below loops 1000 x 500us = 500 ms.
	 *  Exit PSR may need to wait 1-2 frames to power up. Timeout after at
	 *  least a few frames. Should never hit the max retry assert below.
	 */
	if (wait) {
		for (retry_count = 0; retry_count <= 1000; retry_count++) {
			dmub_psr_get_state(dmub, &psr_state);

			if (enable) {
				if (psr_state != 0)
					break;
			} else {
				if (psr_state == 0)
					break;
			}

			udelay(500);
		}

		/* assert if max retry hit */
		if (retry_count >= 1000)
			ASSERT(0);
	}
}

/**
 * Set PSR level.
 */
static void dmub_psr_set_level(struct dmub_psr *dmub, uint16_t psr_level)
{
	union dmub_rb_cmd cmd;
	uint32_t psr_state = 0;
	struct dc_context *dc = dmub->ctx;

	dmub_psr_get_state(dmub, &psr_state);

	if (psr_state == 0)
		return;

	cmd.psr_set_level.header.type = DMUB_CMD__PSR;
	cmd.psr_set_level.header.sub_type = DMUB_CMD__PSR_SET_LEVEL;
	cmd.psr_set_level.header.payload_bytes = sizeof(struct dmub_cmd_psr_set_level_data);
	cmd.psr_set_level.psr_set_level_data.psr_level = psr_level;

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);
}

/**
 * Setup PSR by programming phy registers and sending psr hw context values to firmware.
 */
static bool dmub_psr_copy_settings(struct dmub_psr *dmub,
		struct dc_link *link,
		struct psr_context *psr_context)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;
	struct dmub_cmd_psr_copy_settings_data *copy_settings_data
		= &cmd.psr_copy_settings.psr_copy_settings_data;
	struct pipe_ctx *pipe_ctx = NULL;
	struct resource_context *res_ctx = &link->ctx->dc->current_state->res_ctx;
	int i = 0;

	for (i = 0; i < MAX_PIPES; i++) {
		if (res_ctx->pipe_ctx[i].stream &&
		    res_ctx->pipe_ctx[i].stream->link == link &&
		    res_ctx->pipe_ctx[i].stream->link->connector_signal == SIGNAL_TYPE_EDP) {
			pipe_ctx = &res_ctx->pipe_ctx[i];
			break;
		}
	}

	if (!pipe_ctx)
		return false;

	// First, set the psr version
	if (!dmub_psr_set_version(dmub, pipe_ctx->stream))
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
	copy_settings_data->dpphy_inst				= psr_context->transmitterId;
	copy_settings_data->aux_inst				= psr_context->channel;
	copy_settings_data->digfe_inst				= psr_context->engineId;
	copy_settings_data->digbe_inst				= psr_context->transmitterId;

	copy_settings_data->mpcc_inst				= pipe_ctx->plane_res.mpcc_inst;

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
	copy_settings_data->smu_optimizations_en		= psr_context->allow_smu_optimizations;
	copy_settings_data->frame_delay				= psr_context->frame_delay;
	copy_settings_data->frame_cap_ind			= psr_context->psrFrameCaptureIndicationReq;
	copy_settings_data->init_sdp_deadline			= psr_context->sdpTransmitLineNumDeadline;
	copy_settings_data->debug.u32All = 0;
	copy_settings_data->debug.bitfields.visual_confirm	= dc->dc->debug.visual_confirm == VISUAL_CONFIRM_PSR ?
									true : false;
	copy_settings_data->debug.bitfields.use_hw_lock_mgr		= 1;

	dc_dmub_srv_cmd_queue(dc->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->dmub_srv);
	dc_dmub_srv_wait_idle(dc->dmub_srv);

	return true;
}

static const struct dmub_psr_funcs psr_funcs = {
	.psr_copy_settings		= dmub_psr_copy_settings,
	.psr_enable			= dmub_psr_enable,
	.psr_get_state			= dmub_psr_get_state,
	.psr_set_level			= dmub_psr_set_level,
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
	kfree(*dmub);
	*dmub = NULL;
}
