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

#include "dc.h"
#include "dc_dmub_srv.h"
#include "dmub/dmub_srv.h"
#include "core_types.h"
#include "dmub_replay.h"

#define DC_TRACE_LEVEL_MESSAGE(...) /* do nothing */

#define MAX_PIPES 6

/*
 * Get Replay state from firmware.
 */
static void dmub_replay_get_state(struct dmub_replay *dmub, enum replay_state *state, uint8_t panel_inst)
{
	struct dmub_srv *srv = dmub->ctx->dmub_srv->dmub;
	/* uint32_t raw_state = 0; */
	uint32_t retry_count = 0;
	enum dmub_status status;

	do {
		// Send gpint command and wait for ack
		status = dmub_srv_send_gpint_command(srv, DMUB_GPINT__GET_REPLAY_STATE, panel_inst, 30);

		if (status == DMUB_STATUS_OK) {
			// GPINT was executed, get response
			dmub_srv_get_gpint_response(srv, (uint32_t *)state);
		} else
			// Return invalid state when GPINT times out
			*state = REPLAY_STATE_INVALID;
	} while (++retry_count <= 1000 && *state == REPLAY_STATE_INVALID);

	// Assert if max retry hit
	if (retry_count >= 1000 && *state == REPLAY_STATE_INVALID) {
		ASSERT(0);
		/* To-do: Add retry fail log */
	}
}

/*
 * Enable/Disable Replay.
 */
static void dmub_replay_enable(struct dmub_replay *dmub, bool enable, bool wait, uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;
	uint32_t retry_count;
	enum replay_state state = REPLAY_STATE_0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.replay_enable.header.type = DMUB_CMD__REPLAY;
	cmd.replay_enable.data.panel_inst = panel_inst;

	cmd.replay_enable.header.sub_type = DMUB_CMD__REPLAY_ENABLE;
	if (enable)
		cmd.replay_enable.data.enable = REPLAY_ENABLE;
	else
		cmd.replay_enable.data.enable = REPLAY_DISABLE;

	cmd.replay_enable.header.payload_bytes = sizeof(struct dmub_rb_cmd_replay_enable_data);

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	/* Below loops 1000 x 500us = 500 ms.
	 *  Exit REPLAY may need to wait 1-2 frames to power up. Timeout after at
	 *  least a few frames. Should never hit the max retry assert below.
	 */
	if (wait) {
		for (retry_count = 0; retry_count <= 1000; retry_count++) {
			dmub_replay_get_state(dmub, &state, panel_inst);

			if (enable) {
				if (state != REPLAY_STATE_0)
					break;
			} else {
				if (state == REPLAY_STATE_0)
					break;
			}

			fsleep(500);
		}

		/* assert if max retry hit */
		if (retry_count >= 1000)
			ASSERT(0);
	}

}

/*
 * Set REPLAY power optimization flags.
 */
static void dmub_replay_set_power_opt(struct dmub_replay *dmub, unsigned int power_opt, uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.replay_set_power_opt.header.type = DMUB_CMD__REPLAY;
	cmd.replay_set_power_opt.header.sub_type = DMUB_CMD__SET_REPLAY_POWER_OPT;
	cmd.replay_set_power_opt.header.payload_bytes = sizeof(struct dmub_cmd_replay_set_power_opt_data);
	cmd.replay_set_power_opt.replay_set_power_opt_data.power_opt = power_opt;
	cmd.replay_set_power_opt.replay_set_power_opt_data.panel_inst = panel_inst;

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

/*
 * Setup Replay by programming phy registers and sending replay hw context values to firmware.
 */
static bool dmub_replay_copy_settings(struct dmub_replay *dmub,
	struct dc_link *link,
	struct replay_context *replay_context,
	uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;
	struct dmub_cmd_replay_copy_settings_data *copy_settings_data
		= &cmd.replay_copy_settings.replay_copy_settings_data;
	struct pipe_ctx *pipe_ctx = NULL;
	struct resource_context *res_ctx = &link->ctx->dc->current_state->res_ctx;
	int i = 0;

	for (i = 0; i < MAX_PIPES; i++) {
		if (res_ctx &&
			res_ctx->pipe_ctx[i].stream &&
			res_ctx->pipe_ctx[i].stream->link &&
			res_ctx->pipe_ctx[i].stream->link == link &&
			res_ctx->pipe_ctx[i].stream->link->connector_signal == SIGNAL_TYPE_EDP) {
			pipe_ctx = &res_ctx->pipe_ctx[i];
			//TODO: refactor for multi edp support
			break;
		}
	}

	if (!pipe_ctx)
		return false;

	memset(&cmd, 0, sizeof(cmd));
	cmd.replay_copy_settings.header.type = DMUB_CMD__REPLAY;
	cmd.replay_copy_settings.header.sub_type = DMUB_CMD__REPLAY_COPY_SETTINGS;
	cmd.replay_copy_settings.header.payload_bytes = sizeof(struct dmub_cmd_replay_copy_settings_data);

	// HW insts
	copy_settings_data->aux_inst				= replay_context->aux_inst;
	copy_settings_data->digbe_inst				= replay_context->digbe_inst;
	copy_settings_data->digfe_inst				= replay_context->digfe_inst;

	if (pipe_ctx->plane_res.dpp)
		copy_settings_data->dpp_inst			= pipe_ctx->plane_res.dpp->inst;
	else
		copy_settings_data->dpp_inst			= 0;
	if (pipe_ctx->stream_res.tg)
		copy_settings_data->otg_inst			= pipe_ctx->stream_res.tg->inst;
	else
		copy_settings_data->otg_inst			= 0;

	copy_settings_data->dpphy_inst				= link->link_enc->transmitter;

	// Misc
	copy_settings_data->line_time_in_ns			= replay_context->line_time_in_ns;
	copy_settings_data->panel_inst				= panel_inst;
	copy_settings_data->debug.u32All			= link->replay_settings.config.debug_flags;
	copy_settings_data->pixel_deviation_per_line		= link->dpcd_caps.pr_info.pixel_deviation_per_line;
	copy_settings_data->max_deviation_line			= link->dpcd_caps.pr_info.max_deviation_line;
	copy_settings_data->smu_optimizations_en		= link->replay_settings.replay_smu_opt_enable;
	copy_settings_data->replay_timing_sync_supported = link->replay_settings.config.replay_timing_sync_supported;

	copy_settings_data->flags.u32All = 0;
	copy_settings_data->flags.bitfields.fec_enable_status = (link->fec_state == dc_link_fec_enabled);
	copy_settings_data->flags.bitfields.dsc_enable_status = (pipe_ctx->stream->timing.flags.DSC == 1);
	// WA for PSRSU+DSC on specific TCON, if DSC is enabled, force PSRSU as ffu mode(full frame update)
	if (((link->dpcd_caps.fec_cap.bits.FEC_CAPABLE &&
		!link->dc->debug.disable_fec) &&
		(link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_SUPPORT &&
		!link->panel_config.dsc.disable_dsc_edp &&
		link->dc->caps.edp_dsc_support)) &&
		link->dpcd_caps.sink_dev_id == DP_DEVICE_ID_38EC11 /*&&
		(!memcmp(link->dpcd_caps.sink_dev_id_str, DP_SINK_DEVICE_STR_ID_1,
			sizeof(DP_SINK_DEVICE_STR_ID_1)) ||
		!memcmp(link->dpcd_caps.sink_dev_id_str, DP_SINK_DEVICE_STR_ID_2,
			sizeof(DP_SINK_DEVICE_STR_ID_2)))*/)
		copy_settings_data->flags.bitfields.force_wakeup_by_tps3 = 1;
	else
		copy_settings_data->flags.bitfields.force_wakeup_by_tps3 = 0;


	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

/*
 * Set coasting vtotal.
 */
static void dmub_replay_set_coasting_vtotal(struct dmub_replay *dmub,
		uint16_t coasting_vtotal,
		uint8_t panel_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.replay_set_coasting_vtotal.header.type = DMUB_CMD__REPLAY;
	cmd.replay_set_coasting_vtotal.header.sub_type = DMUB_CMD__REPLAY_SET_COASTING_VTOTAL;
	cmd.replay_set_coasting_vtotal.header.payload_bytes = sizeof(struct dmub_cmd_replay_set_coasting_vtotal_data);
	cmd.replay_set_coasting_vtotal.replay_set_coasting_vtotal_data.coasting_vtotal = coasting_vtotal;

	dm_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

/*
 * Get Replay residency from firmware.
 */
static void dmub_replay_residency(struct dmub_replay *dmub, uint8_t panel_inst,
	uint32_t *residency, const bool is_start, const bool is_alpm)
{
	struct dmub_srv *srv = dmub->ctx->dmub_srv->dmub;
	uint16_t param = (uint16_t)(panel_inst << 8);

	if (is_alpm)
		param |= REPLAY_RESIDENCY_MODE_ALPM;

	if (is_start)
		param |= REPLAY_RESIDENCY_ENABLE;

	// Send gpint command and wait for ack
	dmub_srv_send_gpint_command(srv, DMUB_GPINT__REPLAY_RESIDENCY, param, 30);

	if (!is_start)
		dmub_srv_get_gpint_response(srv, residency);
	else
		*residency = 0;
}

/**
 * Set REPLAY power optimization flags and coasting vtotal.
 */
static void dmub_replay_set_power_opt_and_coasting_vtotal(struct dmub_replay *dmub,
		unsigned int power_opt, uint8_t panel_inst, uint16_t coasting_vtotal)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = dmub->ctx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.replay_set_power_opt_and_coasting_vtotal.header.type = DMUB_CMD__REPLAY;
	cmd.replay_set_power_opt_and_coasting_vtotal.header.sub_type =
		DMUB_CMD__REPLAY_SET_POWER_OPT_AND_COASTING_VTOTAL;
	cmd.replay_set_power_opt_and_coasting_vtotal.header.payload_bytes =
		sizeof(struct dmub_rb_cmd_replay_set_power_opt_and_coasting_vtotal);
	cmd.replay_set_power_opt_and_coasting_vtotal.replay_set_power_opt_data.power_opt = power_opt;
	cmd.replay_set_power_opt_and_coasting_vtotal.replay_set_power_opt_data.panel_inst = panel_inst;
	cmd.replay_set_power_opt_and_coasting_vtotal.replay_set_coasting_vtotal_data.coasting_vtotal = coasting_vtotal;

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

/**
 * send Replay general cmd to DMUB.
 */
static void dmub_replay_send_cmd(struct dmub_replay *dmub,
		enum replay_FW_Message_type msg, union dmub_replay_cmd_set *cmd_element)
{
	union dmub_rb_cmd cmd;
	struct dc_context *ctx = NULL;

	if (dmub == NULL || cmd_element == NULL)
		return;

	ctx = dmub->ctx;
	if (ctx != NULL) {

		if (msg != Replay_Msg_Not_Support) {
			memset(&cmd, 0, sizeof(cmd));
			//Header
			cmd.replay_set_timing_sync.header.type = DMUB_CMD__REPLAY;
		} else
			return;
	} else
		return;

	switch (msg) {
	case Replay_Set_Timing_Sync_Supported:
		//Header
		cmd.replay_set_timing_sync.header.sub_type =
			DMUB_CMD__REPLAY_SET_TIMING_SYNC_SUPPORTED;
		cmd.replay_set_timing_sync.header.payload_bytes =
			sizeof(struct dmub_rb_cmd_replay_set_timing_sync);
		//Cmd Body
		cmd.replay_set_timing_sync.replay_set_timing_sync_data.panel_inst =
						cmd_element->sync_data.panel_inst;
		cmd.replay_set_timing_sync.replay_set_timing_sync_data.timing_sync_supported =
						cmd_element->sync_data.timing_sync_supported;
		break;
	case Replay_Set_Residency_Frameupdate_Timer:
		//Header
		cmd.replay_set_frameupdate_timer.header.sub_type =
			DMUB_CMD__REPLAY_SET_RESIDENCY_FRAMEUPDATE_TIMER;
		cmd.replay_set_frameupdate_timer.header.payload_bytes =
			sizeof(struct dmub_rb_cmd_replay_set_frameupdate_timer);
		//Cmd Body
		cmd.replay_set_frameupdate_timer.data.panel_inst =
						cmd_element->panel_inst;
		cmd.replay_set_frameupdate_timer.data.enable =
						cmd_element->timer_data.enable;
		cmd.replay_set_frameupdate_timer.data.frameupdate_count =
						cmd_element->timer_data.frameupdate_count;
		break;
	case Replay_Msg_Not_Support:
	default:
		return;
		break;
	}

	dc_wake_and_execute_dmub_cmd(ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static const struct dmub_replay_funcs replay_funcs = {
	.replay_copy_settings				= dmub_replay_copy_settings,
	.replay_enable					= dmub_replay_enable,
	.replay_get_state				= dmub_replay_get_state,
	.replay_set_power_opt				= dmub_replay_set_power_opt,
	.replay_set_coasting_vtotal			= dmub_replay_set_coasting_vtotal,
	.replay_residency				= dmub_replay_residency,
	.replay_set_power_opt_and_coasting_vtotal	= dmub_replay_set_power_opt_and_coasting_vtotal,
	.replay_send_cmd				= dmub_replay_send_cmd,
};

/*
 * Construct Replay object.
 */
static void dmub_replay_construct(struct dmub_replay *replay, struct dc_context *ctx)
{
	replay->ctx = ctx;
	replay->funcs = &replay_funcs;
}

/*
 * Allocate and initialize Replay object.
 */
struct dmub_replay *dmub_replay_create(struct dc_context *ctx)
{
	struct dmub_replay *replay = kzalloc(sizeof(struct dmub_replay), GFP_KERNEL);

	if (replay == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dmub_replay_construct(replay, ctx);

	return replay;
}

/*
 * Deallocate Replay object.
 */
void dmub_replay_destroy(struct dmub_replay **dmub)
{
	kfree(*dmub);
	*dmub = NULL;
}
