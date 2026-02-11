/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#include "link_dp_panel_replay.h"
#include "link_edp_panel_control.h"
#include "link_dpcd.h"
#include "dm_helpers.h"
#include "dc/dc_dmub_srv.h"
#include "dce/dmub_replay.h"

#define DC_LOGGER \
	link->ctx->logger

#define DP_SINK_PR_ENABLE_AND_CONFIGURATION		0x37B

static bool dp_setup_panel_replay(struct dc_link *link, const struct dc_stream_state *stream)
{
	/* To-do: Setup Replay */
	struct dc *dc;
	struct dmub_replay *replay;
	int i;
	unsigned int panel_inst;
	struct replay_context replay_context = { 0 };
	unsigned int lineTimeInNs = 0;

	union panel_replay_enable_and_configuration_1 pr_config_1 = { 0 };
	union panel_replay_enable_and_configuration_2 pr_config_2 = { 0 };

	union dpcd_alpm_configuration alpm_config;

	replay_context.controllerId = CONTROLLER_ID_UNDEFINED;

	if (!link)
		return false;

	//Clear Panel Replay enable & config
	dm_helpers_dp_write_dpcd(link->ctx, link,
		DP_PANEL_REPLAY_ENABLE_AND_CONFIGURATION_1,
		(uint8_t *)&(pr_config_1.raw), sizeof(uint8_t));

	dm_helpers_dp_write_dpcd(link->ctx, link,
		DP_PANEL_REPLAY_ENABLE_AND_CONFIGURATION_2,
		(uint8_t *)&(pr_config_2.raw), sizeof(uint8_t));

	if (!(link->replay_settings.config.replay_supported))
		return false;

	dc = link->ctx->dc;

	//not sure should keep or not
	replay = dc->res_pool->replay;

	if (!replay)
		return false;

	if (!dp_pr_get_panel_inst(dc, link, &panel_inst))
		return false;

	replay_context.aux_inst = link->ddc->ddc_pin->hw_info.ddc_channel;
	replay_context.digbe_inst = link->link_enc->transmitter;
	replay_context.digfe_inst = link->link_enc->preferred_engine;

	for (i = 0; i < MAX_PIPES; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].stream
				== stream) {
			/* dmcu -1 for all controller id values,
			 * therefore +1 here
			 */
			replay_context.controllerId =
				dc->current_state->res_ctx.pipe_ctx[i].stream_res.tg->inst + 1;
			break;
		}
	}

	lineTimeInNs =
		((stream->timing.h_total * 1000000) /
			(stream->timing.pix_clk_100hz / 10)) + 1;

	replay_context.line_time_in_ns = lineTimeInNs;

	link->replay_settings.replay_feature_enabled = dp_pr_copy_settings(link, &replay_context);

	if (link->replay_settings.replay_feature_enabled) {
		if (dc_is_embedded_signal(link->connector_signal)) {
			pr_config_1.bits.PANEL_REPLAY_ENABLE = 1;
			pr_config_1.bits.PANEL_REPLAY_CRC_ENABLE = 1;
			pr_config_1.bits.IRQ_HPD_ASSDP_MISSING = 1;
			pr_config_1.bits.IRQ_HPD_VSCSDP_UNCORRECTABLE_ERROR = 1;
			pr_config_1.bits.IRQ_HPD_RFB_ERROR = 1;
			pr_config_1.bits.IRQ_HPD_ACTIVE_FRAME_CRC_ERROR = 1;
			pr_config_1.bits.PANEL_REPLAY_SELECTIVE_UPDATE_ENABLE = 1;
			pr_config_1.bits.PANEL_REPLAY_EARLY_TRANSPORT_ENABLE = 1;
		} else {
			pr_config_1.bits.PANEL_REPLAY_ENABLE = 1;
		}

		pr_config_2.bits.SINK_REFRESH_RATE_UNLOCK_GRANTED = 0;

		if (link->dpcd_caps.vesa_replay_caps.bits.SU_Y_GRANULARITY_EXT_CAP_SUPPORTED)
			pr_config_2.bits.SU_Y_GRANULARITY_EXT_VALUE_ENABLED = 1;

		pr_config_2.bits.SU_REGION_SCAN_LINE_CAPTURE_INDICATION = 0;

		dm_helpers_dp_write_dpcd(link->ctx, link,
			DP_PANEL_REPLAY_ENABLE_AND_CONFIGURATION_1,
			(uint8_t *)&(pr_config_1.raw), sizeof(uint8_t));

		dm_helpers_dp_write_dpcd(link->ctx, link,
			DP_PANEL_REPLAY_ENABLE_AND_CONFIGURATION_2,
			(uint8_t *)&(pr_config_2.raw), sizeof(uint8_t));

		//ALPM Setup
		memset(&alpm_config, 0, sizeof(alpm_config));
		alpm_config.bits.ENABLE = link->replay_settings.config.alpm_mode != DC_ALPM_UNSUPPORTED ? 1 : 0;

		if (link->replay_settings.config.alpm_mode == DC_ALPM_AUXLESS) {
			alpm_config.bits.ALPM_MODE_SEL = 1;
			alpm_config.bits.ACDS_PERIOD_DURATION = 1;
		}

		dm_helpers_dp_write_dpcd(
			link->ctx,
			link,
			DP_RECEIVER_ALPM_CONFIG,
			&alpm_config.raw,
			sizeof(alpm_config.raw));
	}

	return true;
}


bool dp_pr_get_panel_inst(const struct dc *dc,
		const struct dc_link *link,
		unsigned int *inst_out)
{
	if (!dc || !link || !inst_out)
		return false;

	if (!dc_is_dp_sst_signal(link->connector_signal)) /* only supoprt DP sst (eDP included) for now */
		return false;

	for (unsigned int i = 0; i < MAX_PIPES; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].stream &&
			dc->current_state->res_ctx.pipe_ctx[i].stream->link == link) {
			/* *inst_out is equal to otg number */
			if (dc->current_state->res_ctx.pipe_ctx[i].stream_res.tg)
				*inst_out = dc->current_state->res_ctx.pipe_ctx[i].stream_res.tg->inst;
			else
				*inst_out = 0;

			return true;
		}
	}

	return false;
}

bool dp_setup_replay(struct dc_link *link, const struct dc_stream_state *stream)
{
	if (!link)
		return false;
	if (link->replay_settings.config.replay_version == DC_VESA_PANEL_REPLAY)
		return dp_setup_panel_replay(link, stream);
	else if (link->replay_settings.config.replay_version == DC_FREESYNC_REPLAY)
		return edp_setup_freesync_replay(link, stream);
	else
		return false;
}

bool dp_pr_enable(struct dc_link *link, bool enable)
{
	struct dc *dc = link->ctx->dc;
	unsigned int panel_inst = 0;
	union dmub_rb_cmd cmd;

	if (!dp_pr_get_panel_inst(dc, link, &panel_inst))
		return false;

	if (link->replay_settings.replay_allow_active != enable) {
		//for sending PR enable commands to DMUB
		memset(&cmd, 0, sizeof(cmd));

		cmd.pr_enable.header.type = DMUB_CMD__PR;
		cmd.pr_enable.header.sub_type = DMUB_CMD__PR_ENABLE;
		cmd.pr_enable.header.payload_bytes = sizeof(struct dmub_cmd_pr_enable_data);
		cmd.pr_enable.data.panel_inst = panel_inst;
		cmd.pr_enable.data.enable = enable ? 1 : 0;

		dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

		link->replay_settings.replay_allow_active = enable;
	}
	return true;
}

bool dp_pr_copy_settings(struct dc_link *link, struct replay_context *replay_context)
{
	struct dc *dc = link->ctx->dc;
	unsigned int panel_inst = 0;
	union dmub_rb_cmd cmd;
	struct pipe_ctx *pipe_ctx = NULL;

	if (!dp_pr_get_panel_inst(dc, link, &panel_inst))
		return false;

	for (unsigned int i = 0; i < MAX_PIPES; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].stream &&
			dc->current_state->res_ctx.pipe_ctx[i].stream->link &&
			dc->current_state->res_ctx.pipe_ctx[i].stream->link == link &&
			dc_is_dp_sst_signal(dc->current_state->res_ctx.pipe_ctx[i].stream->link->connector_signal)) {
			pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];
			/* todo: need update for MST */
			break;
		}
	}

	if (!pipe_ctx)
		return false;

	memset(&cmd, 0, sizeof(cmd));
	cmd.pr_copy_settings.header.type = DMUB_CMD__PR;
	cmd.pr_copy_settings.header.sub_type = DMUB_CMD__PR_COPY_SETTINGS;
	cmd.pr_copy_settings.header.payload_bytes = sizeof(struct dmub_cmd_pr_copy_settings_data);
	cmd.pr_copy_settings.data.panel_inst = panel_inst;
	// HW inst
	cmd.pr_copy_settings.data.aux_inst = replay_context->aux_inst;
	cmd.pr_copy_settings.data.digbe_inst = replay_context->digbe_inst;
	cmd.pr_copy_settings.data.digfe_inst = replay_context->digfe_inst;
	if (pipe_ctx->plane_res.dpp)
		cmd.pr_copy_settings.data.dpp_inst = pipe_ctx->plane_res.dpp->inst;
	else
		cmd.pr_copy_settings.data.dpp_inst = 0;
	if (pipe_ctx->stream_res.tg)
		cmd.pr_copy_settings.data.otg_inst = pipe_ctx->stream_res.tg->inst;
	else
		cmd.pr_copy_settings.data.otg_inst = 0;

	cmd.pr_copy_settings.data.dpphy_inst = link->link_enc->transmitter;

	cmd.pr_copy_settings.data.line_time_in_ns = replay_context->line_time_in_ns;
	cmd.pr_copy_settings.data.flags.bitfields.fec_enable_status = (link->fec_state == dc_link_fec_enabled);
	cmd.pr_copy_settings.data.flags.bitfields.dsc_enable_status = (pipe_ctx->stream->timing.flags.DSC == 1);
	cmd.pr_copy_settings.data.debug.u32All = link->replay_settings.config.debug_flags;

	cmd.pr_copy_settings.data.su_granularity_needed = link->dpcd_caps.vesa_replay_caps.bits.PR_SU_GRANULARITY_NEEDED;
	cmd.pr_copy_settings.data.su_x_granularity = link->dpcd_caps.vesa_replay_su_info.pr_su_x_granularity;
	cmd.pr_copy_settings.data.su_y_granularity = link->dpcd_caps.vesa_replay_su_info.pr_su_y_granularity;
	cmd.pr_copy_settings.data.su_y_granularity_extended_caps =
		link->dpcd_caps.vesa_replay_su_info.pr_su_y_granularity_extended_caps;

	if (pipe_ctx->stream->timing.dsc_cfg.num_slices_v > 0)
		cmd.pr_copy_settings.data.dsc_slice_height = (pipe_ctx->stream->timing.v_addressable +
			pipe_ctx->stream->timing.v_border_top + pipe_ctx->stream->timing.v_border_bottom) /
			pipe_ctx->stream->timing.dsc_cfg.num_slices_v;

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
	return true;
}

bool dp_pr_update_state(struct dc_link *link, struct dmub_cmd_pr_update_state_data *update_state_data)
{
	struct dc *dc = link->ctx->dc;
	unsigned int panel_inst = 0;
	union dmub_rb_cmd cmd;

	if (!dp_pr_get_panel_inst(dc, link, &panel_inst))
		return false;

	memset(&cmd, 0, sizeof(cmd));
	memcpy(&cmd.pr_update_state.data, update_state_data, sizeof(struct dmub_cmd_pr_update_state_data));

	cmd.pr_update_state.header.type = DMUB_CMD__PR;
	cmd.pr_update_state.header.sub_type = DMUB_CMD__PR_UPDATE_STATE;
	cmd.pr_update_state.header.payload_bytes = sizeof(struct dmub_cmd_pr_update_state_data);
	cmd.pr_update_state.data.panel_inst = panel_inst;

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
	return true;
}

bool dp_pr_set_general_cmd(struct dc_link *link, struct dmub_cmd_pr_general_cmd_data *general_cmd_data)
{
	struct dc *dc = link->ctx->dc;
	unsigned int panel_inst = 0;
	union dmub_rb_cmd cmd;

	if (!dp_pr_get_panel_inst(dc, link, &panel_inst))
		return false;

	memset(&cmd, 0, sizeof(cmd));
	memcpy(&cmd.pr_general_cmd.data, general_cmd_data, sizeof(struct dmub_cmd_pr_general_cmd_data));

	cmd.pr_general_cmd.header.type = DMUB_CMD__PR;
	cmd.pr_general_cmd.header.sub_type = DMUB_CMD__PR_GENERAL_CMD;
	cmd.pr_general_cmd.header.payload_bytes = sizeof(struct dmub_cmd_pr_general_cmd_data);
	cmd.pr_general_cmd.data.panel_inst = panel_inst;

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
	return true;
}

bool dp_pr_get_state(const struct dc_link *link, uint64_t *state)
{
	const struct dc *dc = link->ctx->dc;
	unsigned int panel_inst = 0;
	uint32_t retry_count = 0;
	uint32_t replay_state = 0;

	if (!dp_pr_get_panel_inst(dc, link, &panel_inst))
		return false;

	do {
		// Send gpint command and wait for ack
		if (!dc_wake_and_execute_gpint(dc->ctx, DMUB_GPINT__GET_REPLAY_STATE, panel_inst,
					       &replay_state, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY)) {
			// Return invalid state when GPINT times out
			replay_state = PR_STATE_INVALID;
		}
		/* Copy 32-bit result into 64-bit output */
		*state = replay_state;
	} while (++retry_count <= 1000 && *state == PR_STATE_INVALID);

	// Assert if max retry hit
	if (retry_count >= 1000 && *state == PR_STATE_INVALID) {
		ASSERT(0);
		/* To-do: Add retry fail log */
	}

	return true;
}
