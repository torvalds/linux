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

#define DC_TRACE_LEVEL_MESSAGE(...)	do {} while (0) /* do nothing */

#define MAX_PIPES 6

static const uint8_t DP_SINK_DEVICE_STR_ID_1[] = {7, 1, 8, 7, 3};
static const uint8_t DP_SINK_DEVICE_STR_ID_2[] = {7, 1, 8, 7, 5};
static const uint8_t DP_SINK_DEVICE_STR_ID_3[] = {0x42, 0x61, 0x6c, 0x73, 0x61};

/*
 * Convert dmcub psr state to dmcu psr state.
 */
static enum dc_psr_state convert_psr_state(uint32_t raw_state)
{
	enum dc_psr_state state = PSR_STATE0;

	if (raw_state == 0)
		state = PSR_STATE0;
	else if (raw_state == 0x10)
		state = PSR_STATE1;
	else if (raw_state == 0x11)
		state = PSR_STATE1a;
	else if (raw_state == 0x20)
		state = PSR_STATE2;
	else if (raw_state == 0x21)
		state = PSR_STATE2a;
	else if (raw_state == 0x22)
		state = PSR_STATE2b;
	else if (raw_state == 0x30)
		state = PSR_STATE3;
	else if (raw_state == 0x31)
		state = PSR_STATE3Init;
	else if (raw_state == 0x40)
		state = PSR_STATE4;
	else if (raw_state == 0x41)
		state = PSR_STATE4a;
	else if (raw_state == 0x42)
		state = PSR_STATE4b;
	else if (raw_state == 0x43)
		state = PSR_STATE4c;
	else if (raw_state == 0x44)
		state = PSR_STATE4d;
	else if (raw_state == 0x50)
		state = PSR_STATE5;
	else if (raw_state == 0x51)
		state = PSR_STATE5a;
	else if (raw_state == 0x52)
		state = PSR_STATE5b;
	else if (raw_state == 0x53)
		state = PSR_STATE5c;
	else if (raw_state == 0x4A)
		state = PSR_STATE4_FULL_FRAME;
	else if (raw_state == 0x4B)
		state = PSR_STATE4a_FULL_FRAME;
	else if (raw_state == 0x4C)
		state = PSR_STATE4b_FULL_FRAME;
	else if (raw_state == 0x4D)
		state = PSR_STATE4c_FULL_FRAME;
	else if (raw_state == 0x4E)
		state = PSR_STATE4_FULL_FRAME_POWERUP;
	else if (raw_state == 0x4F)
		state = PSR_STATE4_FULL_FRAME_HW_LOCK;
	else if (raw_state == 0x60)
		state = PSR_STATE_HWLOCK_MGR;
	else if (raw_state == 0x61)
		state = PSR_STATE_POLLVUPDATE;
	else
		state = PSR_STATE_INVALID;

	return state;
}

/*
 * Get PSR state from firmware.
 */
static void dmub_psr_get_state(struct dmub_psr *dmub, enum dc_psr_state *state, uint8_t panel_inst)
{
	struct dmub_srv *srv = dmub->ctx->dmub_srv->dmub;
	uint32_t raw_state = 0;
	uint32_t retry_count = 0;
	enum dmub_status status;

	do {
		// Send gpint command and wait for ack
		status = dmub_srv_send_gpint_command(srv, DMUB_GPINT__GET_PSR_STATE, panel_inst, 30);

		if (status == DMUB_STATUS_OK) {
			// GPINT was executed, get response
			dmub_srv_get_gpint_response(srv, &raw_state);
			*state = convert_psr_state(raw_state);
		} else
			// Return invalid state when GPINT times out
			*state = PSR_STATE_INVALID;

	} while (++retry_count <= 1000 && *state == PSR_STATE_INVALID);

	// Assert if max retry hit
	if (retry_count >= 1000 && *state == PSR_STATE_INVALID) {
		ASSERT(0);
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
				WPP_BIT_FLAG_Firmware_PsrState,
				"Unable to get PSR state from FW.");
	} else
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_VERBOSE,
				WPP_BIT_FLAG_Firmware_PsrState,
				"Got PSR state from FW. PSR state: %d, Retry count: %d",
				*state, retry_count);
}

/*
 * Set PSR version.
 */
static bool dmub_psr_set_version(struct dmub_psr *dmub, struct dc_stream_state *stream, uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	if (stream->link->psr_settings.psr_version == DC_PSR_VERSION_UNSUPPORTED)
		return false;

	memset(&cmd, 0, sizeof(cmd));
	cmd.psr_set_version.header.type = DMUB_CMD__PSR;
	cmd.psr_set_version.header.sub_type = DMUB_CMD__PSR_SET_VERSION;
	switch (stream->link->psr_settings.psr_version) {
	case DC_PSR_VERSION_1:
		cmd.psr_set_version.psr_set_version_data.version = PSR_VERSION_1;
		break;
	case DC_PSR_VERSION_SU_1:
		cmd.psr_set_version.psr_set_version_data.version = PSR_VERSION_SU_1;
		break;
	case DC_PSR_VERSION_UNSUPPORTED:
	default:
		cmd.psr_set_version.psr_set_version_data.version = PSR_VERSION_UNSUPPORTED;
		break;
	}

	if (cmd.psr_set_version.psr_set_version_data.version == PSR_VERSION_UNSUPPORTED)
		return false;

	cmd.psr_set_version.psr_set_version_data.cmd_version = DMUB_CMD_PSR_CONTROL_VERSION_1;
	cmd.psr_set_version.psr_set_version_data.panel_inst = panel_inst;
	cmd.psr_set_version.header.payload_bytes = sizeof(struct dmub_cmd_psr_set_version_data);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

/*
 * Enable/Disable PSR.
 */
static void dmub_psr_enable(struct dmub_psr *dmub, bool enable, bool wait, uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;
	uint32_t retry_count;
	enum dc_psr_state state = PSR_STATE0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.psr_enable.header.type = DMUB_CMD__PSR;

	cmd.psr_enable.data.cmd_version = DMUB_CMD_PSR_CONTROL_VERSION_1;
	cmd.psr_enable.data.panel_inst = panel_inst;

	if (enable)
		cmd.psr_enable.header.sub_type = DMUB_CMD__PSR_ENABLE;
	else
		cmd.psr_enable.header.sub_type = DMUB_CMD__PSR_DISABLE;

	cmd.psr_enable.header.payload_bytes = 0; // Send header only

	dm_execute_dmub_cmd(dc->dmub_srv->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	/* Below loops 1000 x 500us = 500 ms.
	 *  Exit PSR may need to wait 1-2 frames to power up. Timeout after at
	 *  least a few frames. Should never hit the max retry assert below.
	 */
	if (wait) {
		for (retry_count = 0; retry_count <= 1000; retry_count++) {
			dmub_psr_get_state(dmub, &state, panel_inst);

			if (enable) {
				if (state != PSR_STATE0)
					break;
			} else {
				if (state == PSR_STATE0)
					break;
			}

			/* must *not* be fsleep - this can be called from high irq levels */
			udelay(500);
		}

		/* assert if max retry hit */
		if (retry_count >= 1000)
			ASSERT(0);
	}
}

/*
 * Set PSR level.
 */
static void dmub_psr_set_level(struct dmub_psr *dmub, uint16_t psr_level, uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	enum dc_psr_state state = PSR_STATE0;
	struct dc_context *dc = dmub->ctx;

	dmub_psr_get_state(dmub, &state, panel_inst);

	if (state == PSR_STATE0)
		return;

	memset(&cmd, 0, sizeof(cmd));
	cmd.psr_set_level.header.type = DMUB_CMD__PSR;
	cmd.psr_set_level.header.sub_type = DMUB_CMD__PSR_SET_LEVEL;
	cmd.psr_set_level.header.payload_bytes = sizeof(struct dmub_cmd_psr_set_level_data);
	cmd.psr_set_level.psr_set_level_data.psr_level = psr_level;
	cmd.psr_set_level.psr_set_level_data.cmd_version = DMUB_CMD_PSR_CONTROL_VERSION_1;
	cmd.psr_set_level.psr_set_level_data.panel_inst = panel_inst;
	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

/*
 * Set PSR vtotal requirement for FreeSync PSR.
 */
static void dmub_psr_set_sink_vtotal_in_psr_active(struct dmub_psr *dmub,
		uint16_t psr_vtotal_idle, uint16_t psr_vtotal_su)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.psr_set_vtotal.header.type = DMUB_CMD__PSR;
	cmd.psr_set_vtotal.header.sub_type = DMUB_CMD__SET_SINK_VTOTAL_IN_PSR_ACTIVE;
	cmd.psr_set_vtotal.header.payload_bytes = sizeof(struct dmub_cmd_psr_set_vtotal_data);
	cmd.psr_set_vtotal.psr_set_vtotal_data.psr_vtotal_idle = psr_vtotal_idle;
	cmd.psr_set_vtotal.psr_set_vtotal_data.psr_vtotal_su = psr_vtotal_su;

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

/*
 * Set PSR power optimization flags.
 */
static void dmub_psr_set_power_opt(struct dmub_psr *dmub, unsigned int power_opt, uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.psr_set_power_opt.header.type = DMUB_CMD__PSR;
	cmd.psr_set_power_opt.header.sub_type = DMUB_CMD__SET_PSR_POWER_OPT;
	cmd.psr_set_power_opt.header.payload_bytes = sizeof(struct dmub_cmd_psr_set_power_opt_data);
	cmd.psr_set_power_opt.psr_set_power_opt_data.cmd_version = DMUB_CMD_PSR_CONTROL_VERSION_1;
	cmd.psr_set_power_opt.psr_set_power_opt_data.power_opt = power_opt;
	cmd.psr_set_power_opt.psr_set_power_opt_data.panel_inst = panel_inst;

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

/*
 * Setup PSR by programming phy registers and sending psr hw context values to firmware.
 */
static bool dmub_psr_copy_settings(struct dmub_psr *dmub,
		struct dc_link *link,
		struct psr_context *psr_context,
		uint8_t panel_inst)
{
	union dmub_rb_cmd cmd = { 0 };
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
			//TODO: refactor for multi edp support
			break;
		}
	}

	if (!pipe_ctx)
		return false;

	// First, set the psr version
	if (!dmub_psr_set_version(dmub, pipe_ctx->stream, panel_inst))
		return false;

	// Program DP DPHY fast training registers
	link->link_enc->funcs->psr_program_dp_dphy_fast_training(link->link_enc,
			psr_context->psrExitLinkTrainingRequired);

	// Program DP_SEC_CNTL1 register to set transmission GPS0 line num and priority to high
	link->link_enc->funcs->psr_program_secondary_packet(link->link_enc,
			psr_context->sdpTransmitLineNumDeadline);

	memset(&cmd, 0, sizeof(cmd));
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
	copy_settings_data->use_phy_fsm             = link->ctx->dc->debug.psr_power_use_phy_fsm;
	copy_settings_data->psr_level				= psr_context->psr_level.u32all;
	copy_settings_data->smu_optimizations_en		= psr_context->allow_smu_optimizations;
	copy_settings_data->multi_disp_optimizations_en	= psr_context->allow_multi_disp_optimizations;
	copy_settings_data->frame_delay				= psr_context->frame_delay;
	copy_settings_data->frame_cap_ind			= psr_context->psrFrameCaptureIndicationReq;
	copy_settings_data->init_sdp_deadline			= psr_context->sdpTransmitLineNumDeadline;
	copy_settings_data->debug.u32All = 0;
	copy_settings_data->debug.bitfields.visual_confirm	= dc->dc->debug.visual_confirm == VISUAL_CONFIRM_PSR;
	copy_settings_data->debug.bitfields.use_hw_lock_mgr		= 1;
	copy_settings_data->debug.bitfields.force_full_frame_update	= 0;

	if (psr_context->su_granularity_required == 0)
		copy_settings_data->su_y_granularity = 0;
	else
		copy_settings_data->su_y_granularity = psr_context->su_y_granularity;

	copy_settings_data->line_capture_indication = 0;
	copy_settings_data->line_time_in_us = psr_context->line_time_in_us;
	copy_settings_data->rate_control_caps = psr_context->rate_control_caps;
	copy_settings_data->fec_enable_status = (link->fec_state == dc_link_fec_enabled);
	copy_settings_data->fec_enable_delay_in100us = link->dc->debug.fec_enable_delay_in100us;
	copy_settings_data->cmd_version =  DMUB_CMD_PSR_CONTROL_VERSION_1;
	copy_settings_data->panel_inst = panel_inst;
	copy_settings_data->dsc_enable_status = (pipe_ctx->stream->timing.flags.DSC == 1);

	/**
	 * WA for PSRSU+DSC on specific TCON, if DSC is enabled, force PSRSU as ffu mode(full frame update)
	 * Note that PSRSU+DSC is still under development.
	 */
	if (copy_settings_data->dsc_enable_status &&
		link->dpcd_caps.sink_dev_id == DP_DEVICE_ID_38EC11 &&
		!memcmp(link->dpcd_caps.sink_dev_id_str, DP_SINK_DEVICE_STR_ID_1,
			sizeof(DP_SINK_DEVICE_STR_ID_1)))
		link->psr_settings.force_ffu_mode = 1;
	else
		link->psr_settings.force_ffu_mode = 0;
	copy_settings_data->force_ffu_mode = link->psr_settings.force_ffu_mode;

	if (((link->dpcd_caps.fec_cap.bits.FEC_CAPABLE &&
		!link->dc->debug.disable_fec) &&
		(link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_SUPPORT &&
		!link->panel_config.dsc.disable_dsc_edp &&
		link->dc->caps.edp_dsc_support)) &&
		link->dpcd_caps.sink_dev_id == DP_DEVICE_ID_38EC11 &&
		(!memcmp(link->dpcd_caps.sink_dev_id_str, DP_SINK_DEVICE_STR_ID_1,
			sizeof(DP_SINK_DEVICE_STR_ID_1)) ||
		!memcmp(link->dpcd_caps.sink_dev_id_str, DP_SINK_DEVICE_STR_ID_2,
			sizeof(DP_SINK_DEVICE_STR_ID_2))))
		copy_settings_data->debug.bitfields.force_wakeup_by_tps3 = 1;
	else
		copy_settings_data->debug.bitfields.force_wakeup_by_tps3 = 0;

	if (link->psr_settings.psr_version == DC_PSR_VERSION_1 &&
		link->dpcd_caps.sink_dev_id == DP_DEVICE_ID_0022B9 &&
		!memcmp(link->dpcd_caps.sink_dev_id_str, DP_SINK_DEVICE_STR_ID_3,
			sizeof(DP_SINK_DEVICE_STR_ID_3))) {
		copy_settings_data->poweroff_before_vertical_line = 16;
	}

	//WA for PSR1 on specific TCON, require frame delay for frame re-lock
	copy_settings_data->relock_delay_frame_cnt = 0;
	if (link->dpcd_caps.sink_dev_id == DP_BRANCH_DEVICE_ID_001CF8)
		copy_settings_data->relock_delay_frame_cnt = 2;
	copy_settings_data->dsc_slice_height = psr_context->dsc_slice_height;

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

/*
 * Send command to PSR to force static ENTER and ignore all state changes until exit
 */
static void dmub_psr_force_static(struct dmub_psr *dmub, uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	memset(&cmd, 0, sizeof(cmd));

	cmd.psr_force_static.psr_force_static_data.panel_inst = panel_inst;
	cmd.psr_force_static.psr_force_static_data.cmd_version = DMUB_CMD_PSR_CONTROL_VERSION_1;
	cmd.psr_force_static.header.type = DMUB_CMD__PSR;
	cmd.psr_force_static.header.sub_type = DMUB_CMD__PSR_FORCE_STATIC;
	cmd.psr_enable.header.payload_bytes = 0;

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

/*
 * Get PSR residency from firmware.
 */
static void dmub_psr_get_residency(struct dmub_psr *dmub, uint32_t *residency, uint8_t panel_inst)
{
	struct dmub_srv *srv = dmub->ctx->dmub_srv->dmub;
	uint16_t param = (uint16_t)(panel_inst << 8);

	/* Send gpint command and wait for ack */
	dmub_srv_send_gpint_command(srv, DMUB_GPINT__PSR_RESIDENCY, param, 30);

	dmub_srv_get_gpint_response(srv, residency);
}

static const struct dmub_psr_funcs psr_funcs = {
	.psr_copy_settings		= dmub_psr_copy_settings,
	.psr_enable			= dmub_psr_enable,
	.psr_get_state			= dmub_psr_get_state,
	.psr_set_level			= dmub_psr_set_level,
	.psr_force_static		= dmub_psr_force_static,
	.psr_get_residency		= dmub_psr_get_residency,
	.psr_set_sink_vtotal_in_psr_active	= dmub_psr_set_sink_vtotal_in_psr_active,
	.psr_set_power_opt		= dmub_psr_set_power_opt,
};

/*
 * Construct PSR object.
 */
static void dmub_psr_construct(struct dmub_psr *psr, struct dc_context *ctx)
{
	psr->ctx = ctx;
	psr->funcs = &psr_funcs;
}

/*
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

/*
 * Deallocate PSR object.
 */
void dmub_psr_destroy(struct dmub_psr **dmub)
{
	kfree(*dmub);
	*dmub = NULL;
}
