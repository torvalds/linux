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

#include "dm_services.h"
#include "dc.h"
#include "dc_dmub_srv.h"
#include "../dmub/dmub_srv.h"
#include "dm_helpers.h"
#include "dc_hw_types.h"
#include "core_types.h"
#include "../basics/conversion.h"
#include "cursor_reg_cache.h"
#include "resource.h"
#include "clk_mgr.h"
#include "dc_state_priv.h"
#include "dc_plane_priv.h"

#define CTX dc_dmub_srv->ctx
#define DC_LOGGER CTX->logger

static void dc_dmub_srv_construct(struct dc_dmub_srv *dc_srv, struct dc *dc,
				  struct dmub_srv *dmub)
{
	dc_srv->dmub = dmub;
	dc_srv->ctx = dc->ctx;
}

struct dc_dmub_srv *dc_dmub_srv_create(struct dc *dc, struct dmub_srv *dmub)
{
	struct dc_dmub_srv *dc_srv =
		kzalloc(sizeof(struct dc_dmub_srv), GFP_KERNEL);

	if (dc_srv == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dc_dmub_srv_construct(dc_srv, dc, dmub);

	return dc_srv;
}

void dc_dmub_srv_destroy(struct dc_dmub_srv **dmub_srv)
{
	if (*dmub_srv) {
		kfree(*dmub_srv);
		*dmub_srv = NULL;
	}
}

void dc_dmub_srv_wait_idle(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	do {
		status = dmub_srv_wait_for_idle(dmub, 100000);
	} while (dc_dmub_srv->ctx->dc->debug.disable_timeout && status != DMUB_STATUS_OK);

	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error waiting for DMUB idle: status=%d\n", status);
		dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
	}
}

void dc_dmub_srv_clear_inbox0_ack(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status = DMUB_STATUS_OK;

	status = dmub_srv_clear_inbox0_ack(dmub);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error clearing INBOX0 ack: status=%d\n", status);
		dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
	}
}

void dc_dmub_srv_wait_for_inbox0_ack(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status = DMUB_STATUS_OK;

	status = dmub_srv_wait_for_inbox0_ack(dmub, 100000);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error waiting for INBOX0 HW Lock Ack\n");
		dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
	}
}

void dc_dmub_srv_send_inbox0_cmd(struct dc_dmub_srv *dc_dmub_srv,
				 union dmub_inbox0_data_register data)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status = DMUB_STATUS_OK;

	status = dmub_srv_send_inbox0_cmd(dmub, data);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error sending INBOX0 cmd\n");
		dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
	}
}

bool dc_dmub_srv_cmd_list_queue_execute(struct dc_dmub_srv *dc_dmub_srv,
		unsigned int count,
		union dmub_rb_cmd *cmd_list)
{
	struct dc_context *dc_ctx;
	struct dmub_srv *dmub;
	enum dmub_status status;
	int i;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dc_ctx = dc_dmub_srv->ctx;
	dmub = dc_dmub_srv->dmub;

	for (i = 0 ; i < count; i++) {
		// Queue command
		status = dmub_srv_cmd_queue(dmub, &cmd_list[i]);

		if (status == DMUB_STATUS_QUEUE_FULL) {
			/* Execute and wait for queue to become empty again. */
			status = dmub_srv_cmd_execute(dmub);
			if (status == DMUB_STATUS_POWER_STATE_D3)
				return false;

			do {
				status = dmub_srv_wait_for_idle(dmub, 100000);
			} while (dc_dmub_srv->ctx->dc->debug.disable_timeout && status != DMUB_STATUS_OK);

			/* Requeue the command. */
			status = dmub_srv_cmd_queue(dmub, &cmd_list[i]);
		}

		if (status != DMUB_STATUS_OK) {
			if (status != DMUB_STATUS_POWER_STATE_D3) {
				DC_ERROR("Error queueing DMUB command: status=%d\n", status);
				dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
			}
			return false;
		}
	}

	status = dmub_srv_cmd_execute(dmub);
	if (status != DMUB_STATUS_OK) {
		if (status != DMUB_STATUS_POWER_STATE_D3) {
			DC_ERROR("Error starting DMUB execution: status=%d\n", status);
			dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
		}
		return false;
	}

	return true;
}

bool dc_dmub_srv_wait_for_idle(struct dc_dmub_srv *dc_dmub_srv,
		enum dm_dmub_wait_type wait_type,
		union dmub_rb_cmd *cmd_list)
{
	struct dmub_srv *dmub;
	enum dmub_status status;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;

	// Wait for DMUB to process command
	if (wait_type != DM_DMUB_WAIT_TYPE_NO_WAIT) {
		do {
			status = dmub_srv_wait_for_idle(dmub, 100000);
		} while (dc_dmub_srv->ctx->dc->debug.disable_timeout && status != DMUB_STATUS_OK);

		if (status != DMUB_STATUS_OK) {
			DC_LOG_DEBUG("No reply for DMUB command: status=%d\n", status);
			if (!dmub->debug.timeout_occured) {
				dmub->debug.timeout_occured = true;
				dmub->debug.timeout_cmd = *cmd_list;
				dmub->debug.timestamp = dm_get_timestamp(dc_dmub_srv->ctx);
			}
			dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
			return false;
		}

		// Copy data back from ring buffer into command
		if (wait_type == DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY)
			dmub_rb_get_return_data(&dmub->inbox1_rb, cmd_list);
	}

	return true;
}

bool dc_dmub_srv_cmd_run(struct dc_dmub_srv *dc_dmub_srv, union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type)
{
	return dc_dmub_srv_cmd_run_list(dc_dmub_srv, 1, cmd, wait_type);
}

bool dc_dmub_srv_cmd_run_list(struct dc_dmub_srv *dc_dmub_srv, unsigned int count, union dmub_rb_cmd *cmd_list, enum dm_dmub_wait_type wait_type)
{
	struct dc_context *dc_ctx;
	struct dmub_srv *dmub;
	enum dmub_status status;
	int i;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dc_ctx = dc_dmub_srv->ctx;
	dmub = dc_dmub_srv->dmub;

	for (i = 0 ; i < count; i++) {
		// Queue command
		status = dmub_srv_cmd_queue(dmub, &cmd_list[i]);

		if (status == DMUB_STATUS_QUEUE_FULL) {
			/* Execute and wait for queue to become empty again. */
			status = dmub_srv_cmd_execute(dmub);
			if (status == DMUB_STATUS_POWER_STATE_D3)
				return false;

			status = dmub_srv_wait_for_idle(dmub, 100000);
			if (status != DMUB_STATUS_OK)
				return false;

			/* Requeue the command. */
			status = dmub_srv_cmd_queue(dmub, &cmd_list[i]);
		}

		if (status != DMUB_STATUS_OK) {
			if (status != DMUB_STATUS_POWER_STATE_D3) {
				DC_ERROR("Error queueing DMUB command: status=%d\n", status);
				dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
			}
			return false;
		}
	}

	status = dmub_srv_cmd_execute(dmub);
	if (status != DMUB_STATUS_OK) {
		if (status != DMUB_STATUS_POWER_STATE_D3) {
			DC_ERROR("Error starting DMUB execution: status=%d\n", status);
			dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
		}
		return false;
	}

	// Wait for DMUB to process command
	if (wait_type != DM_DMUB_WAIT_TYPE_NO_WAIT) {
		if (dc_dmub_srv->ctx->dc->debug.disable_timeout) {
			do {
				status = dmub_srv_wait_for_idle(dmub, 100000);
			} while (status != DMUB_STATUS_OK);
		} else
			status = dmub_srv_wait_for_idle(dmub, 100000);

		if (status != DMUB_STATUS_OK) {
			DC_LOG_DEBUG("No reply for DMUB command: status=%d\n", status);
			dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
			return false;
		}

		// Copy data back from ring buffer into command
		if (wait_type == DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY)
			dmub_rb_get_return_data(&dmub->inbox1_rb, cmd_list);
	}

	return true;
}

bool dc_dmub_srv_optimized_init_done(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub;
	struct dc_context *dc_ctx;
	union dmub_fw_boot_status boot_status;
	enum dmub_status status;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;
	dc_ctx = dc_dmub_srv->ctx;

	status = dmub_srv_get_fw_boot_status(dmub, &boot_status);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error querying DMUB boot status: error=%d\n", status);
		return false;
	}

	return boot_status.bits.optimized_init_done;
}

bool dc_dmub_srv_notify_stream_mask(struct dc_dmub_srv *dc_dmub_srv,
				    unsigned int stream_mask)
{
	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	return dc_wake_and_execute_gpint(dc_dmub_srv->ctx, DMUB_GPINT__IDLE_OPT_NOTIFY_STREAM_MASK,
					 stream_mask, NULL, DM_DMUB_WAIT_TYPE_WAIT);
}

bool dc_dmub_srv_is_restore_required(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub;
	struct dc_context *dc_ctx;
	union dmub_fw_boot_status boot_status;
	enum dmub_status status;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;
	dc_ctx = dc_dmub_srv->ctx;

	status = dmub_srv_get_fw_boot_status(dmub, &boot_status);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error querying DMUB boot status: error=%d\n", status);
		return false;
	}

	return boot_status.bits.restore_required;
}

bool dc_dmub_srv_get_dmub_outbox0_msg(const struct dc *dc, struct dmcub_trace_buf_entry *entry)
{
	struct dmub_srv *dmub = dc->ctx->dmub_srv->dmub;
	return dmub_srv_get_outbox0_msg(dmub, entry);
}

void dc_dmub_trace_event_control(struct dc *dc, bool enable)
{
	dm_helpers_dmub_outbox_interrupt_control(dc->ctx, enable);
}

void dc_dmub_srv_drr_update_cmd(struct dc *dc, uint32_t tg_inst, uint32_t vtotal_min, uint32_t vtotal_max)
{
	union dmub_rb_cmd cmd = { 0 };

	cmd.drr_update.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.drr_update.header.sub_type = DMUB_CMD__FAMS_DRR_UPDATE;
	cmd.drr_update.dmub_optc_state_req.v_total_max = vtotal_max;
	cmd.drr_update.dmub_optc_state_req.v_total_min = vtotal_min;
	cmd.drr_update.dmub_optc_state_req.tg_inst = tg_inst;

	cmd.drr_update.header.payload_bytes = sizeof(cmd.drr_update) - sizeof(cmd.drr_update.header);

	// Send the command to the DMCUB.
	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

void dc_dmub_srv_set_drr_manual_trigger_cmd(struct dc *dc, uint32_t tg_inst)
{
	union dmub_rb_cmd cmd = { 0 };

	cmd.drr_update.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.drr_update.header.sub_type = DMUB_CMD__FAMS_SET_MANUAL_TRIGGER;
	cmd.drr_update.dmub_optc_state_req.tg_inst = tg_inst;

	cmd.drr_update.header.payload_bytes = sizeof(cmd.drr_update) - sizeof(cmd.drr_update.header);

	// Send the command to the DMCUB.
	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static uint8_t dc_dmub_srv_get_pipes_for_stream(struct dc *dc, struct dc_stream_state *stream)
{
	uint8_t pipes = 0;
	int i = 0;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->stream == stream && pipe->stream_res.tg)
			pipes = i;
	}
	return pipes;
}

static void dc_dmub_srv_populate_fams_pipe_info(struct dc *dc, struct dc_state *context,
		struct pipe_ctx *head_pipe,
		struct dmub_cmd_fw_assisted_mclk_switch_pipe_data *fams_pipe_data)
{
	int j;
	int pipe_idx = 0;

	fams_pipe_data->pipe_index[pipe_idx++] = head_pipe->plane_res.hubp->inst;
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *split_pipe = &context->res_ctx.pipe_ctx[j];

		if (split_pipe->stream == head_pipe->stream && (split_pipe->top_pipe || split_pipe->prev_odm_pipe)) {
			fams_pipe_data->pipe_index[pipe_idx++] = split_pipe->plane_res.hubp->inst;
		}
	}
	fams_pipe_data->pipe_count = pipe_idx;
}

bool dc_dmub_srv_p_state_delegate(struct dc *dc, bool should_manage_pstate, struct dc_state *context)
{
	union dmub_rb_cmd cmd = { 0 };
	struct dmub_cmd_fw_assisted_mclk_switch_config *config_data = &cmd.fw_assisted_mclk_switch.config_data;
	int i = 0, k = 0;
	int ramp_up_num_steps = 1; // TODO: Ramp is currently disabled. Reenable it.
	uint8_t visual_confirm_enabled;
	int pipe_idx = 0;
	struct dc_stream_status *stream_status = NULL;

	if (dc == NULL)
		return false;

	visual_confirm_enabled = dc->debug.visual_confirm == VISUAL_CONFIRM_FAMS;

	// Format command.
	cmd.fw_assisted_mclk_switch.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.fw_assisted_mclk_switch.header.sub_type = DMUB_CMD__FAMS_SETUP_FW_CTRL;
	cmd.fw_assisted_mclk_switch.config_data.fams_enabled = should_manage_pstate;
	cmd.fw_assisted_mclk_switch.config_data.visual_confirm_enabled = visual_confirm_enabled;

	if (should_manage_pstate) {
		for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

			if (!pipe->stream)
				continue;

			/* If FAMS is being used to support P-State and there is a stream
			 * that does not use FAMS, we are in an FPO + VActive scenario.
			 * Assign vactive stretch margin in this case.
			 */
			stream_status = dc_state_get_stream_status(context, pipe->stream);
			if (stream_status && !stream_status->fpo_in_use) {
				cmd.fw_assisted_mclk_switch.config_data.vactive_stretch_margin_us = dc->debug.fpo_vactive_margin_us;
				break;
			}
			pipe_idx++;
		}
	}

	for (i = 0, k = 0; context && i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (!resource_is_pipe_type(pipe, OTG_MASTER))
			continue;

		stream_status = dc_state_get_stream_status(context, pipe->stream);
		if (stream_status && stream_status->fpo_in_use) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
			uint8_t min_refresh_in_hz = (pipe->stream->timing.min_refresh_in_uhz + 999999) / 1000000;

			config_data->pipe_data[k].pix_clk_100hz = pipe->stream->timing.pix_clk_100hz;
			config_data->pipe_data[k].min_refresh_in_hz = min_refresh_in_hz;
			config_data->pipe_data[k].max_ramp_step = ramp_up_num_steps;
			config_data->pipe_data[k].pipes = dc_dmub_srv_get_pipes_for_stream(dc, pipe->stream);
			dc_dmub_srv_populate_fams_pipe_info(dc, context, pipe, &config_data->pipe_data[k]);
			k++;
		}
	}
	cmd.fw_assisted_mclk_switch.header.payload_bytes =
		sizeof(cmd.fw_assisted_mclk_switch) - sizeof(cmd.fw_assisted_mclk_switch.header);

	// Send the command to the DMCUB.
	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

void dc_dmub_srv_query_caps_cmd(struct dc_dmub_srv *dc_dmub_srv)
{
	union dmub_rb_cmd cmd = { 0 };

	if (dc_dmub_srv->ctx->dc->debug.dmcub_emulation)
		return;

	memset(&cmd, 0, sizeof(cmd));

	/* Prepare fw command */
	cmd.query_feature_caps.header.type = DMUB_CMD__QUERY_FEATURE_CAPS;
	cmd.query_feature_caps.header.sub_type = 0;
	cmd.query_feature_caps.header.ret_status = 1;
	cmd.query_feature_caps.header.payload_bytes = sizeof(struct dmub_cmd_query_feature_caps_data);

	/* If command was processed, copy feature caps to dmub srv */
	if (dc_wake_and_execute_dmub_cmd(dc_dmub_srv->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY) &&
	    cmd.query_feature_caps.header.ret_status == 0) {
		memcpy(&dc_dmub_srv->dmub->feature_caps,
		       &cmd.query_feature_caps.query_feature_caps_data,
		       sizeof(struct dmub_feature_caps));
	}
}

void dc_dmub_srv_get_visual_confirm_color_cmd(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	union dmub_rb_cmd cmd = { 0 };
	unsigned int panel_inst = 0;

	if (!dc_get_edp_link_panel_inst(dc, pipe_ctx->stream->link, &panel_inst) &&
			dc->debug.visual_confirm == VISUAL_CONFIRM_DISABLE)
		return;

	memset(&cmd, 0, sizeof(cmd));

	// Prepare fw command
	cmd.visual_confirm_color.header.type = DMUB_CMD__GET_VISUAL_CONFIRM_COLOR;
	cmd.visual_confirm_color.header.sub_type = 0;
	cmd.visual_confirm_color.header.ret_status = 1;
	cmd.visual_confirm_color.header.payload_bytes = sizeof(struct dmub_cmd_visual_confirm_color_data);
	cmd.visual_confirm_color.visual_confirm_color_data.visual_confirm_color.panel_inst = panel_inst;

	// If command was processed, copy feature caps to dmub srv
	if (dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY) &&
		cmd.visual_confirm_color.header.ret_status == 0) {
		memcpy(&dc->ctx->dmub_srv->dmub->visual_confirm_color,
			&cmd.visual_confirm_color.visual_confirm_color_data,
			sizeof(struct dmub_visual_confirm_color));
	}
}

/**
 * populate_subvp_cmd_drr_info - Helper to populate DRR pipe info for the DMCUB subvp command
 *
 * @dc: [in] pointer to dc object
 * @subvp_pipe: [in] pipe_ctx for the SubVP pipe
 * @vblank_pipe: [in] pipe_ctx for the DRR pipe
 * @pipe_data: [in] Pipe data which stores the VBLANK/DRR info
 * @context: [in] DC state for access to phantom stream
 *
 * Populate the DMCUB SubVP command with DRR pipe info. All the information
 * required for calculating the SubVP + DRR microschedule is populated here.
 *
 * High level algorithm:
 * 1. Get timing for SubVP pipe, phantom pipe, and DRR pipe
 * 2. Calculate the min and max vtotal which supports SubVP + DRR microschedule
 * 3. Populate the drr_info with the min and max supported vtotal values
 */
static void populate_subvp_cmd_drr_info(struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *subvp_pipe,
		struct pipe_ctx *vblank_pipe,
		struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 *pipe_data)
{
	struct dc_stream_state *phantom_stream = dc_state_get_paired_subvp_stream(context, subvp_pipe->stream);
	struct dc_crtc_timing *main_timing = &subvp_pipe->stream->timing;
	struct dc_crtc_timing *phantom_timing;
	struct dc_crtc_timing *drr_timing = &vblank_pipe->stream->timing;
	uint16_t drr_frame_us = 0;
	uint16_t min_drr_supported_us = 0;
	uint16_t max_drr_supported_us = 0;
	uint16_t max_drr_vblank_us = 0;
	uint16_t max_drr_mallregion_us = 0;
	uint16_t mall_region_us = 0;
	uint16_t prefetch_us = 0;
	uint16_t subvp_active_us = 0;
	uint16_t drr_active_us = 0;
	uint16_t min_vtotal_supported = 0;
	uint16_t max_vtotal_supported = 0;

	if (!phantom_stream)
		return;

	phantom_timing = &phantom_stream->timing;

	pipe_data->pipe_config.vblank_data.drr_info.drr_in_use = true;
	pipe_data->pipe_config.vblank_data.drr_info.use_ramping = false; // for now don't use ramping
	pipe_data->pipe_config.vblank_data.drr_info.drr_window_size_ms = 4; // hardcode 4ms DRR window for now

	drr_frame_us = div64_u64(((uint64_t)drr_timing->v_total * drr_timing->h_total * 1000000),
			(((uint64_t)drr_timing->pix_clk_100hz * 100)));
	// P-State allow width and FW delays already included phantom_timing->v_addressable
	mall_region_us = div64_u64(((uint64_t)phantom_timing->v_addressable * phantom_timing->h_total * 1000000),
			(((uint64_t)phantom_timing->pix_clk_100hz * 100)));
	min_drr_supported_us = drr_frame_us + mall_region_us + SUBVP_DRR_MARGIN_US;
	min_vtotal_supported = div64_u64(((uint64_t)drr_timing->pix_clk_100hz * 100 * min_drr_supported_us),
			(((uint64_t)drr_timing->h_total * 1000000)));

	prefetch_us = div64_u64(((uint64_t)(phantom_timing->v_total - phantom_timing->v_front_porch) * phantom_timing->h_total * 1000000),
			(((uint64_t)phantom_timing->pix_clk_100hz * 100) + dc->caps.subvp_prefetch_end_to_mall_start_us));
	subvp_active_us = div64_u64(((uint64_t)main_timing->v_addressable * main_timing->h_total * 1000000),
			(((uint64_t)main_timing->pix_clk_100hz * 100)));
	drr_active_us = div64_u64(((uint64_t)drr_timing->v_addressable * drr_timing->h_total * 1000000),
			(((uint64_t)drr_timing->pix_clk_100hz * 100)));
	max_drr_vblank_us = div64_u64((subvp_active_us - prefetch_us -
			dc->caps.subvp_fw_processing_delay_us - drr_active_us), 2) + drr_active_us;
	max_drr_mallregion_us = subvp_active_us - prefetch_us - mall_region_us - dc->caps.subvp_fw_processing_delay_us;
	max_drr_supported_us = max_drr_vblank_us > max_drr_mallregion_us ? max_drr_vblank_us : max_drr_mallregion_us;
	max_vtotal_supported = div64_u64(((uint64_t)drr_timing->pix_clk_100hz * 100 * max_drr_supported_us),
			(((uint64_t)drr_timing->h_total * 1000000)));

	/* When calculating the max vtotal supported for SubVP + DRR cases, add
	 * margin due to possible rounding errors (being off by 1 line in the
	 * FW calculation can incorrectly push the P-State switch to wait 1 frame
	 * longer).
	 */
	max_vtotal_supported = max_vtotal_supported - dc->caps.subvp_drr_max_vblank_margin_us;

	pipe_data->pipe_config.vblank_data.drr_info.min_vtotal_supported = min_vtotal_supported;
	pipe_data->pipe_config.vblank_data.drr_info.max_vtotal_supported = max_vtotal_supported;
	pipe_data->pipe_config.vblank_data.drr_info.drr_vblank_start_margin = dc->caps.subvp_drr_vblank_start_margin_us;
}

/**
 * populate_subvp_cmd_vblank_pipe_info - Helper to populate VBLANK pipe info for the DMUB subvp command
 *
 * @dc: [in] current dc state
 * @context: [in] new dc state
 * @cmd: [in] DMUB cmd to be populated with SubVP info
 * @vblank_pipe: [in] pipe_ctx for the VBLANK pipe
 * @cmd_pipe_index: [in] index for the pipe array in DMCUB SubVP cmd
 *
 * Populate the DMCUB SubVP command with VBLANK pipe info. All the information
 * required to calculate the microschedule for SubVP + VBLANK case is stored in
 * the pipe_data (subvp_data and vblank_data).  Also check if the VBLANK pipe
 * is a DRR display -- if it is make a call to populate drr_info.
 */
static void populate_subvp_cmd_vblank_pipe_info(struct dc *dc,
		struct dc_state *context,
		union dmub_rb_cmd *cmd,
		struct pipe_ctx *vblank_pipe,
		uint8_t cmd_pipe_index)
{
	uint32_t i;
	struct pipe_ctx *pipe = NULL;
	struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 *pipe_data =
			&cmd->fw_assisted_mclk_switch_v2.config_data.pipe_data[cmd_pipe_index];

	// Find the SubVP pipe
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		// We check for master pipe, but it shouldn't matter since we only need
		// the pipe for timing info (stream should be same for any pipe splits)
		if (!resource_is_pipe_type(pipe, OTG_MASTER) ||
				!resource_is_pipe_type(pipe, DPP_PIPE))
			continue;

		// Find the SubVP pipe
		if (dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_MAIN)
			break;
	}

	pipe_data->mode = VBLANK;
	pipe_data->pipe_config.vblank_data.pix_clk_100hz = vblank_pipe->stream->timing.pix_clk_100hz;
	pipe_data->pipe_config.vblank_data.vblank_start = vblank_pipe->stream->timing.v_total -
							vblank_pipe->stream->timing.v_front_porch;
	pipe_data->pipe_config.vblank_data.vtotal = vblank_pipe->stream->timing.v_total;
	pipe_data->pipe_config.vblank_data.htotal = vblank_pipe->stream->timing.h_total;
	pipe_data->pipe_config.vblank_data.vblank_pipe_index = vblank_pipe->pipe_idx;
	pipe_data->pipe_config.vblank_data.vstartup_start = vblank_pipe->pipe_dlg_param.vstartup_start;
	pipe_data->pipe_config.vblank_data.vblank_end =
			vblank_pipe->stream->timing.v_total - vblank_pipe->stream->timing.v_front_porch - vblank_pipe->stream->timing.v_addressable;

	if (vblank_pipe->stream->ignore_msa_timing_param &&
		(vblank_pipe->stream->allow_freesync || vblank_pipe->stream->vrr_active_variable || vblank_pipe->stream->vrr_active_fixed))
		populate_subvp_cmd_drr_info(dc, context, pipe, vblank_pipe, pipe_data);
}

/**
 * update_subvp_prefetch_end_to_mall_start - Helper for SubVP + SubVP case
 *
 * @dc: [in] current dc state
 * @context: [in] new dc state
 * @cmd: [in] DMUB cmd to be populated with SubVP info
 * @subvp_pipes: [in] Array of SubVP pipes (should always be length 2)
 *
 * For SubVP + SubVP, we use a single vertical interrupt to start the
 * microschedule for both SubVP pipes. In order for this to work correctly, the
 * MALL REGION of both SubVP pipes must start at the same time. This function
 * lengthens the prefetch end to mall start delay of the SubVP pipe that has
 * the shorter prefetch so that both MALL REGION's will start at the same time.
 */
static void update_subvp_prefetch_end_to_mall_start(struct dc *dc,
		struct dc_state *context,
		union dmub_rb_cmd *cmd,
		struct pipe_ctx *subvp_pipes[])
{
	uint32_t subvp0_prefetch_us = 0;
	uint32_t subvp1_prefetch_us = 0;
	uint32_t prefetch_delta_us = 0;
	struct dc_stream_state *phantom_stream0 = NULL;
	struct dc_stream_state *phantom_stream1 = NULL;
	struct dc_crtc_timing *phantom_timing0 = NULL;
	struct dc_crtc_timing *phantom_timing1 = NULL;
	struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 *pipe_data = NULL;

	phantom_stream0 = dc_state_get_paired_subvp_stream(context, subvp_pipes[0]->stream);
	if (!phantom_stream0)
		return;

	phantom_stream1 = dc_state_get_paired_subvp_stream(context, subvp_pipes[1]->stream);
	if (!phantom_stream1)
		return;

	phantom_timing0 = &phantom_stream0->timing;
	phantom_timing1 = &phantom_stream1->timing;

	subvp0_prefetch_us = div64_u64(((uint64_t)(phantom_timing0->v_total - phantom_timing0->v_front_porch) *
			(uint64_t)phantom_timing0->h_total * 1000000),
			(((uint64_t)phantom_timing0->pix_clk_100hz * 100) + dc->caps.subvp_prefetch_end_to_mall_start_us));
	subvp1_prefetch_us = div64_u64(((uint64_t)(phantom_timing1->v_total - phantom_timing1->v_front_porch) *
			(uint64_t)phantom_timing1->h_total * 1000000),
			(((uint64_t)phantom_timing1->pix_clk_100hz * 100) + dc->caps.subvp_prefetch_end_to_mall_start_us));

	// Whichever SubVP PIPE has the smaller prefetch (including the prefetch end to mall start time)
	// should increase it's prefetch time to match the other
	if (subvp0_prefetch_us > subvp1_prefetch_us) {
		pipe_data = &cmd->fw_assisted_mclk_switch_v2.config_data.pipe_data[1];
		prefetch_delta_us = subvp0_prefetch_us - subvp1_prefetch_us;
		pipe_data->pipe_config.subvp_data.prefetch_to_mall_start_lines =
				div64_u64(((uint64_t)(dc->caps.subvp_prefetch_end_to_mall_start_us + prefetch_delta_us) *
					((uint64_t)phantom_timing1->pix_clk_100hz * 100) + ((uint64_t)phantom_timing1->h_total * 1000000 - 1)),
					((uint64_t)phantom_timing1->h_total * 1000000));

	} else if (subvp1_prefetch_us >  subvp0_prefetch_us) {
		pipe_data = &cmd->fw_assisted_mclk_switch_v2.config_data.pipe_data[0];
		prefetch_delta_us = subvp1_prefetch_us - subvp0_prefetch_us;
		pipe_data->pipe_config.subvp_data.prefetch_to_mall_start_lines =
				div64_u64(((uint64_t)(dc->caps.subvp_prefetch_end_to_mall_start_us + prefetch_delta_us) *
					((uint64_t)phantom_timing0->pix_clk_100hz * 100) + ((uint64_t)phantom_timing0->h_total * 1000000 - 1)),
					((uint64_t)phantom_timing0->h_total * 1000000));
	}
}

/**
 * populate_subvp_cmd_pipe_info - Helper to populate the SubVP pipe info for the DMUB subvp command
 *
 * @dc: [in] current dc state
 * @context: [in] new dc state
 * @cmd: [in] DMUB cmd to be populated with SubVP info
 * @subvp_pipe: [in] pipe_ctx for the SubVP pipe
 * @cmd_pipe_index: [in] index for the pipe array in DMCUB SubVP cmd
 *
 * Populate the DMCUB SubVP command with SubVP pipe info. All the information
 * required to calculate the microschedule for the SubVP pipe is stored in the
 * pipe_data of the DMCUB SubVP command.
 */
static void populate_subvp_cmd_pipe_info(struct dc *dc,
		struct dc_state *context,
		union dmub_rb_cmd *cmd,
		struct pipe_ctx *subvp_pipe,
		uint8_t cmd_pipe_index)
{
	uint32_t j;
	struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 *pipe_data =
			&cmd->fw_assisted_mclk_switch_v2.config_data.pipe_data[cmd_pipe_index];
	struct dc_stream_state *phantom_stream = dc_state_get_paired_subvp_stream(context, subvp_pipe->stream);
	struct dc_crtc_timing *main_timing = &subvp_pipe->stream->timing;
	struct dc_crtc_timing *phantom_timing;
	uint32_t out_num_stream, out_den_stream, out_num_plane, out_den_plane, out_num, out_den;

	if (!phantom_stream)
		return;

	phantom_timing = &phantom_stream->timing;

	pipe_data->mode = SUBVP;
	pipe_data->pipe_config.subvp_data.pix_clk_100hz = subvp_pipe->stream->timing.pix_clk_100hz;
	pipe_data->pipe_config.subvp_data.htotal = subvp_pipe->stream->timing.h_total;
	pipe_data->pipe_config.subvp_data.vtotal = subvp_pipe->stream->timing.v_total;
	pipe_data->pipe_config.subvp_data.main_vblank_start =
			main_timing->v_total - main_timing->v_front_porch;
	pipe_data->pipe_config.subvp_data.main_vblank_end =
			main_timing->v_total - main_timing->v_front_porch - main_timing->v_addressable;
	pipe_data->pipe_config.subvp_data.mall_region_lines = phantom_timing->v_addressable;
	pipe_data->pipe_config.subvp_data.main_pipe_index = subvp_pipe->stream_res.tg->inst;
	pipe_data->pipe_config.subvp_data.is_drr = subvp_pipe->stream->ignore_msa_timing_param &&
		(subvp_pipe->stream->allow_freesync || subvp_pipe->stream->vrr_active_variable || subvp_pipe->stream->vrr_active_fixed);

	/* Calculate the scaling factor from the src and dst height.
	 * e.g. If 3840x2160 being downscaled to 1920x1080, the scaling factor is 1/2.
	 * Reduce the fraction 1080/2160 = 1/2 for the "scaling factor"
	 *
	 * Make sure to combine stream and plane scaling together.
	 */
	reduce_fraction(subvp_pipe->stream->src.height, subvp_pipe->stream->dst.height,
			&out_num_stream, &out_den_stream);
	reduce_fraction(subvp_pipe->plane_state->src_rect.height, subvp_pipe->plane_state->dst_rect.height,
			&out_num_plane, &out_den_plane);
	reduce_fraction(out_num_stream * out_num_plane, out_den_stream * out_den_plane, &out_num, &out_den);
	pipe_data->pipe_config.subvp_data.scale_factor_numerator = out_num;
	pipe_data->pipe_config.subvp_data.scale_factor_denominator = out_den;

	// Prefetch lines is equal to VACTIVE + BP + VSYNC
	pipe_data->pipe_config.subvp_data.prefetch_lines =
			phantom_timing->v_total - phantom_timing->v_front_porch;

	// Round up
	pipe_data->pipe_config.subvp_data.prefetch_to_mall_start_lines =
			div64_u64(((uint64_t)dc->caps.subvp_prefetch_end_to_mall_start_us * ((uint64_t)phantom_timing->pix_clk_100hz * 100) +
					((uint64_t)phantom_timing->h_total * 1000000 - 1)), ((uint64_t)phantom_timing->h_total * 1000000));
	pipe_data->pipe_config.subvp_data.processing_delay_lines =
			div64_u64(((uint64_t)(dc->caps.subvp_fw_processing_delay_us) * ((uint64_t)phantom_timing->pix_clk_100hz * 100) +
					((uint64_t)phantom_timing->h_total * 1000000 - 1)), ((uint64_t)phantom_timing->h_total * 1000000));

	if (subvp_pipe->bottom_pipe) {
		pipe_data->pipe_config.subvp_data.main_split_pipe_index = subvp_pipe->bottom_pipe->pipe_idx;
	} else if (subvp_pipe->next_odm_pipe) {
		pipe_data->pipe_config.subvp_data.main_split_pipe_index = subvp_pipe->next_odm_pipe->pipe_idx;
	} else {
		pipe_data->pipe_config.subvp_data.main_split_pipe_index = 0xF;
	}

	// Find phantom pipe index based on phantom stream
	for (j = 0; j < dc->res_pool->pipe_count; j++) {
		struct pipe_ctx *phantom_pipe = &context->res_ctx.pipe_ctx[j];

		if (resource_is_pipe_type(phantom_pipe, OTG_MASTER) &&
				phantom_pipe->stream == dc_state_get_paired_subvp_stream(context, subvp_pipe->stream)) {
			pipe_data->pipe_config.subvp_data.phantom_pipe_index = phantom_pipe->stream_res.tg->inst;
			if (phantom_pipe->bottom_pipe) {
				pipe_data->pipe_config.subvp_data.phantom_split_pipe_index = phantom_pipe->bottom_pipe->plane_res.hubp->inst;
			} else if (phantom_pipe->next_odm_pipe) {
				pipe_data->pipe_config.subvp_data.phantom_split_pipe_index = phantom_pipe->next_odm_pipe->plane_res.hubp->inst;
			} else {
				pipe_data->pipe_config.subvp_data.phantom_split_pipe_index = 0xF;
			}
			break;
		}
	}
}

/**
 * dc_dmub_setup_subvp_dmub_command - Populate the DMCUB SubVP command
 *
 * @dc: [in] current dc state
 * @context: [in] new dc state
 * @enable: [in] if true enables the pipes population
 *
 * This function loops through each pipe and populates the DMUB SubVP CMD info
 * based on the pipe (e.g. SubVP, VBLANK).
 */
void dc_dmub_setup_subvp_dmub_command(struct dc *dc,
		struct dc_state *context,
		bool enable)
{
	uint8_t cmd_pipe_index = 0;
	uint32_t i, pipe_idx;
	uint8_t subvp_count = 0;
	union dmub_rb_cmd cmd;
	struct pipe_ctx *subvp_pipes[2];
	uint32_t wm_val_refclk = 0;
	enum mall_stream_type pipe_mall_type;

	memset(&cmd, 0, sizeof(cmd));
	// FW command for SUBVP
	cmd.fw_assisted_mclk_switch_v2.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.fw_assisted_mclk_switch_v2.header.sub_type = DMUB_CMD__HANDLE_SUBVP_CMD;
	cmd.fw_assisted_mclk_switch_v2.header.payload_bytes =
			sizeof(cmd.fw_assisted_mclk_switch_v2) - sizeof(cmd.fw_assisted_mclk_switch_v2.header);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		/* For SubVP pipe count, only count the top most (ODM / MPC) pipe
		 */
		if (resource_is_pipe_type(pipe, OTG_MASTER) &&
				resource_is_pipe_type(pipe, DPP_PIPE) &&
				dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_MAIN)
			subvp_pipes[subvp_count++] = pipe;
	}

	if (enable) {
		// For each pipe that is a "main" SUBVP pipe, fill in pipe data for DMUB SUBVP cmd
		for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
			pipe_mall_type = dc_state_get_pipe_subvp_type(context, pipe);

			if (!pipe->stream)
				continue;

			/* When populating subvp cmd info, only pass in the top most (ODM / MPC) pipe.
			 * Any ODM or MPC splits being used in SubVP will be handled internally in
			 * populate_subvp_cmd_pipe_info
			 */
			if (resource_is_pipe_type(pipe, OTG_MASTER) &&
					resource_is_pipe_type(pipe, DPP_PIPE) &&
					pipe_mall_type == SUBVP_MAIN) {
				populate_subvp_cmd_pipe_info(dc, context, &cmd, pipe, cmd_pipe_index++);
			} else if (resource_is_pipe_type(pipe, OTG_MASTER) &&
					resource_is_pipe_type(pipe, DPP_PIPE) &&
					pipe_mall_type == SUBVP_NONE) {
				// Don't need to check for ActiveDRAMClockChangeMargin < 0, not valid in cases where
				// we run through DML without calculating "natural" P-state support
				populate_subvp_cmd_vblank_pipe_info(dc, context, &cmd, pipe, cmd_pipe_index++);

			}
			pipe_idx++;
		}
		if (subvp_count == 2) {
			update_subvp_prefetch_end_to_mall_start(dc, context, &cmd, subvp_pipes);
		}
		cmd.fw_assisted_mclk_switch_v2.config_data.pstate_allow_width_us = dc->caps.subvp_pstate_allow_width_us;
		cmd.fw_assisted_mclk_switch_v2.config_data.vertical_int_margin_us = dc->caps.subvp_vertical_int_margin_us;

		// Store the original watermark value for this SubVP config so we can lower it when the
		// MCLK switch starts
		wm_val_refclk = context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns *
				(dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000) / 1000;

		cmd.fw_assisted_mclk_switch_v2.config_data.watermark_a_cache = wm_val_refclk < 0xFFFF ? wm_val_refclk : 0xFFFF;
	}

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

bool dc_dmub_srv_get_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv, struct dmub_diagnostic_data *diag_data)
{
	if (!dc_dmub_srv || !dc_dmub_srv->dmub || !diag_data)
		return false;
	return dmub_srv_get_diagnostic_data(dc_dmub_srv->dmub, diag_data);
}

void dc_dmub_srv_log_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_diagnostic_data diag_data = {0};
	uint32_t i;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub) {
		DC_LOG_ERROR("%s: invalid parameters.", __func__);
		return;
	}

	DC_LOG_ERROR("%s: DMCUB error - collecting diagnostic data\n", __func__);

	if (!dc_dmub_srv_get_diagnostic_data(dc_dmub_srv, &diag_data)) {
		DC_LOG_ERROR("%s: dc_dmub_srv_get_diagnostic_data failed.", __func__);
		return;
	}

	DC_LOG_DEBUG("DMCUB STATE:");
	DC_LOG_DEBUG("    dmcub_version      : %08x", diag_data.dmcub_version);
	DC_LOG_DEBUG("    scratch  [0]       : %08x", diag_data.scratch[0]);
	DC_LOG_DEBUG("    scratch  [1]       : %08x", diag_data.scratch[1]);
	DC_LOG_DEBUG("    scratch  [2]       : %08x", diag_data.scratch[2]);
	DC_LOG_DEBUG("    scratch  [3]       : %08x", diag_data.scratch[3]);
	DC_LOG_DEBUG("    scratch  [4]       : %08x", diag_data.scratch[4]);
	DC_LOG_DEBUG("    scratch  [5]       : %08x", diag_data.scratch[5]);
	DC_LOG_DEBUG("    scratch  [6]       : %08x", diag_data.scratch[6]);
	DC_LOG_DEBUG("    scratch  [7]       : %08x", diag_data.scratch[7]);
	DC_LOG_DEBUG("    scratch  [8]       : %08x", diag_data.scratch[8]);
	DC_LOG_DEBUG("    scratch  [9]       : %08x", diag_data.scratch[9]);
	DC_LOG_DEBUG("    scratch [10]       : %08x", diag_data.scratch[10]);
	DC_LOG_DEBUG("    scratch [11]       : %08x", diag_data.scratch[11]);
	DC_LOG_DEBUG("    scratch [12]       : %08x", diag_data.scratch[12]);
	DC_LOG_DEBUG("    scratch [13]       : %08x", diag_data.scratch[13]);
	DC_LOG_DEBUG("    scratch [14]       : %08x", diag_data.scratch[14]);
	DC_LOG_DEBUG("    scratch [15]       : %08x", diag_data.scratch[15]);
	for (i = 0; i < DMUB_PC_SNAPSHOT_COUNT; i++)
		DC_LOG_DEBUG("    pc[%d]             : %08x", i, diag_data.pc[i]);
	DC_LOG_DEBUG("    unk_fault_addr     : %08x", diag_data.undefined_address_fault_addr);
	DC_LOG_DEBUG("    inst_fault_addr    : %08x", diag_data.inst_fetch_fault_addr);
	DC_LOG_DEBUG("    data_fault_addr    : %08x", diag_data.data_write_fault_addr);
	DC_LOG_DEBUG("    inbox1_rptr        : %08x", diag_data.inbox1_rptr);
	DC_LOG_DEBUG("    inbox1_wptr        : %08x", diag_data.inbox1_wptr);
	DC_LOG_DEBUG("    inbox1_size        : %08x", diag_data.inbox1_size);
	DC_LOG_DEBUG("    inbox0_rptr        : %08x", diag_data.inbox0_rptr);
	DC_LOG_DEBUG("    inbox0_wptr        : %08x", diag_data.inbox0_wptr);
	DC_LOG_DEBUG("    inbox0_size        : %08x", diag_data.inbox0_size);
	DC_LOG_DEBUG("    outbox1_rptr       : %08x", diag_data.outbox1_rptr);
	DC_LOG_DEBUG("    outbox1_wptr       : %08x", diag_data.outbox1_wptr);
	DC_LOG_DEBUG("    outbox1_size       : %08x", diag_data.outbox1_size);
	DC_LOG_DEBUG("    is_enabled         : %d", diag_data.is_dmcub_enabled);
	DC_LOG_DEBUG("    is_soft_reset      : %d", diag_data.is_dmcub_soft_reset);
	DC_LOG_DEBUG("    is_secure_reset    : %d", diag_data.is_dmcub_secure_reset);
	DC_LOG_DEBUG("    is_traceport_en    : %d", diag_data.is_traceport_en);
	DC_LOG_DEBUG("    is_cw0_en          : %d", diag_data.is_cw0_enabled);
	DC_LOG_DEBUG("    is_cw6_en          : %d", diag_data.is_cw6_enabled);
}

static bool dc_can_pipe_disable_cursor(struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *test_pipe, *split_pipe;
	const struct scaler_data *scl_data = &pipe_ctx->plane_res.scl_data;
	struct rect r1 = scl_data->recout, r2, r2_half;
	int r1_r = r1.x + r1.width, r1_b = r1.y + r1.height, r2_r, r2_b;
	int cur_layer = pipe_ctx->plane_state->layer_index;

	/**
	 * Disable the cursor if there's another pipe above this with a
	 * plane that contains this pipe's viewport to prevent double cursor
	 * and incorrect scaling artifacts.
	 */
	for (test_pipe = pipe_ctx->top_pipe; test_pipe;
	     test_pipe = test_pipe->top_pipe) {
		// Skip invisible layer and pipe-split plane on same layer
		if (!test_pipe->plane_state->visible || test_pipe->plane_state->layer_index == cur_layer)
			continue;

		r2 = test_pipe->plane_res.scl_data.recout;
		r2_r = r2.x + r2.width;
		r2_b = r2.y + r2.height;

		/**
		 * There is another half plane on same layer because of
		 * pipe-split, merge together per same height.
		 */
		for (split_pipe = pipe_ctx->top_pipe; split_pipe;
		     split_pipe = split_pipe->top_pipe)
			if (split_pipe->plane_state->layer_index == test_pipe->plane_state->layer_index) {
				r2_half = split_pipe->plane_res.scl_data.recout;
				r2.x = (r2_half.x < r2.x) ? r2_half.x : r2.x;
				r2.width = r2.width + r2_half.width;
				r2_r = r2.x + r2.width;
				break;
			}

		if (r1.x >= r2.x && r1.y >= r2.y && r1_r <= r2_r && r1_b <= r2_b)
			return true;
	}

	return false;
}

static bool dc_dmub_should_update_cursor_data(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->plane_state != NULL) {
		if (pipe_ctx->plane_state->address.type == PLN_ADDR_TYPE_VIDEO_PROGRESSIVE)
			return false;

		if (dc_can_pipe_disable_cursor(pipe_ctx))
			return false;
	}

	if ((pipe_ctx->stream->link->psr_settings.psr_version == DC_PSR_VERSION_SU_1 ||
		pipe_ctx->stream->link->psr_settings.psr_version == DC_PSR_VERSION_1) &&
		pipe_ctx->stream->ctx->dce_version >= DCN_VERSION_3_1)
		return true;

	if (pipe_ctx->stream->link->replay_settings.config.replay_supported)
		return true;

	return false;
}

static void dc_build_cursor_update_payload0(
		struct pipe_ctx *pipe_ctx, uint8_t p_idx,
		struct dmub_cmd_update_cursor_payload0 *payload)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	unsigned int panel_inst = 0;

	if (!dc_get_edp_link_panel_inst(hubp->ctx->dc,
		pipe_ctx->stream->link, &panel_inst))
		return;

	/* Payload: Cursor Rect is built from position & attribute
	 * x & y are obtained from postion
	 */
	payload->cursor_rect.x = hubp->cur_rect.x;
	payload->cursor_rect.y = hubp->cur_rect.y;
	/* w & h are obtained from attribute */
	payload->cursor_rect.width  = hubp->cur_rect.w;
	payload->cursor_rect.height = hubp->cur_rect.h;

	payload->enable      = hubp->pos.cur_ctl.bits.cur_enable;
	payload->pipe_idx    = p_idx;
	payload->cmd_version = DMUB_CMD_PSR_CONTROL_VERSION_1;
	payload->panel_inst  = panel_inst;
}

static void dc_build_cursor_position_update_payload0(
		struct dmub_cmd_update_cursor_payload0 *pl, const uint8_t p_idx,
		const struct hubp *hubp, const struct dpp *dpp)
{
	/* Hubp */
	pl->position_cfg.pHubp.cur_ctl.raw  = hubp->pos.cur_ctl.raw;
	pl->position_cfg.pHubp.position.raw = hubp->pos.position.raw;
	pl->position_cfg.pHubp.hot_spot.raw = hubp->pos.hot_spot.raw;
	pl->position_cfg.pHubp.dst_offset.raw = hubp->pos.dst_offset.raw;

	/* dpp */
	pl->position_cfg.pDpp.cur0_ctl.raw = dpp->pos.cur0_ctl.raw;
	pl->position_cfg.pipe_idx = p_idx;
}

static void dc_build_cursor_attribute_update_payload1(
		struct dmub_cursor_attributes_cfg *pl_A, const uint8_t p_idx,
		const struct hubp *hubp, const struct dpp *dpp)
{
	/* Hubp */
	pl_A->aHubp.SURFACE_ADDR_HIGH = hubp->att.SURFACE_ADDR_HIGH;
	pl_A->aHubp.SURFACE_ADDR = hubp->att.SURFACE_ADDR;
	pl_A->aHubp.cur_ctl.raw  = hubp->att.cur_ctl.raw;
	pl_A->aHubp.size.raw     = hubp->att.size.raw;
	pl_A->aHubp.settings.raw = hubp->att.settings.raw;

	/* dpp */
	pl_A->aDpp.cur0_ctl.raw = dpp->att.cur0_ctl.raw;
}

/**
 * dc_send_update_cursor_info_to_dmu - Populate the DMCUB Cursor update info command
 *
 * @pCtx: [in] pipe context
 * @pipe_idx: [in] pipe index
 *
 * This function would store the cursor related information and pass it into
 * dmub
 */
void dc_send_update_cursor_info_to_dmu(
		struct pipe_ctx *pCtx, uint8_t pipe_idx)
{
	union dmub_rb_cmd cmd[2];
	union dmub_cmd_update_cursor_info_data *update_cursor_info_0 =
					&cmd[0].update_cursor_info.update_cursor_info_data;

	memset(cmd, 0, sizeof(cmd));

	if (!dc_dmub_should_update_cursor_data(pCtx))
		return;
	/*
	 * Since we use multi_cmd_pending for dmub command, the 2nd command is
	 * only assigned to store cursor attributes info.
	 * 1st command can view as 2 parts, 1st is for PSR/Replay data, the other
	 * is to store cursor position info.
	 *
	 * Command heaer type must be the same type if using  multi_cmd_pending.
	 * Besides, while process 2nd command in DMU, the sub type is useless.
	 * So it's meanless to pass the sub type header with different type.
	 */

	{
		/* Build Payload#0 Header */
		cmd[0].update_cursor_info.header.type = DMUB_CMD__UPDATE_CURSOR_INFO;
		cmd[0].update_cursor_info.header.payload_bytes =
				sizeof(cmd[0].update_cursor_info.update_cursor_info_data);
		cmd[0].update_cursor_info.header.multi_cmd_pending = 1; //To combine multi dmu cmd, 1st cmd

		/* Prepare Payload */
		dc_build_cursor_update_payload0(pCtx, pipe_idx, &update_cursor_info_0->payload0);

		dc_build_cursor_position_update_payload0(&update_cursor_info_0->payload0, pipe_idx,
				pCtx->plane_res.hubp, pCtx->plane_res.dpp);
		}
	{
		/* Build Payload#1 Header */
		cmd[1].update_cursor_info.header.type = DMUB_CMD__UPDATE_CURSOR_INFO;
		cmd[1].update_cursor_info.header.payload_bytes = sizeof(struct cursor_attributes_cfg);
		cmd[1].update_cursor_info.header.multi_cmd_pending = 0; //Indicate it's the last command.

		dc_build_cursor_attribute_update_payload1(
				&cmd[1].update_cursor_info.update_cursor_info_data.payload1.attribute_cfg,
				pipe_idx, pCtx->plane_res.hubp, pCtx->plane_res.dpp);

		/* Combine 2nd cmds update_curosr_info to DMU */
		dc_wake_and_execute_dmub_cmd_list(pCtx->stream->ctx, 2, cmd, DM_DMUB_WAIT_TYPE_WAIT);
	}
}

bool dc_dmub_check_min_version(struct dmub_srv *srv)
{
	if (!srv->hw_funcs.is_psrsu_supported)
		return true;
	return srv->hw_funcs.is_psrsu_supported(srv);
}

void dc_dmub_srv_enable_dpia_trace(const struct dc *dc)
{
	struct dc_dmub_srv *dc_dmub_srv = dc->ctx->dmub_srv;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub) {
		DC_LOG_ERROR("%s: invalid parameters.", __func__);
		return;
	}

	if (!dc_wake_and_execute_gpint(dc->ctx, DMUB_GPINT__SET_TRACE_BUFFER_MASK_WORD1,
				       0x0010, NULL, DM_DMUB_WAIT_TYPE_WAIT)) {
		DC_LOG_ERROR("timeout updating trace buffer mask word\n");
		return;
	}

	if (!dc_wake_and_execute_gpint(dc->ctx, DMUB_GPINT__UPDATE_TRACE_BUFFER_MASK,
				       0x0000, NULL, DM_DMUB_WAIT_TYPE_WAIT)) {
		DC_LOG_ERROR("timeout updating trace buffer mask word\n");
		return;
	}

	DC_LOG_DEBUG("Enabled DPIA trace\n");
}

void dc_dmub_srv_subvp_save_surf_addr(const struct dc_dmub_srv *dc_dmub_srv, const struct dc_plane_address *addr, uint8_t subvp_index)
{
	dmub_srv_subvp_save_surf_addr(dc_dmub_srv->dmub, addr, subvp_index);
}

bool dc_dmub_srv_is_hw_pwr_up(struct dc_dmub_srv *dc_dmub_srv, bool wait)
{
	struct dc_context *dc_ctx;
	enum dmub_status status;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return true;

	if (dc_dmub_srv->ctx->dc->debug.dmcub_emulation)
		return true;

	dc_ctx = dc_dmub_srv->ctx;

	if (wait) {
		if (dc_dmub_srv->ctx->dc->debug.disable_timeout) {
			do {
				status = dmub_srv_wait_for_hw_pwr_up(dc_dmub_srv->dmub, 500000);
			} while (status != DMUB_STATUS_OK);
		} else {
			status = dmub_srv_wait_for_hw_pwr_up(dc_dmub_srv->dmub, 500000);
			if (status != DMUB_STATUS_OK) {
				DC_ERROR("Error querying DMUB hw power up status: error=%d\n", status);
				return false;
			}
		}
	} else
		return dmub_srv_is_hw_pwr_up(dc_dmub_srv->dmub);

	return true;
}

static int count_active_streams(const struct dc *dc)
{
	int i, count = 0;

	for (i = 0; i < dc->current_state->stream_count; ++i) {
		struct dc_stream_state *stream = dc->current_state->streams[i];

		if (stream && (!stream->dpms_off || dc->config.disable_ips_in_dpms_off))
			count += 1;
	}

	return count;
}

static void dc_dmub_srv_notify_idle(const struct dc *dc, bool allow_idle)
{
	volatile const struct dmub_shared_state_ips_fw *ips_fw;
	struct dc_dmub_srv *dc_dmub_srv;
	union dmub_rb_cmd cmd = {0};

	if (dc->debug.dmcub_emulation)
		return;

	if (!dc->ctx->dmub_srv || !dc->ctx->dmub_srv->dmub)
		return;

	dc_dmub_srv = dc->ctx->dmub_srv;
	ips_fw = &dc_dmub_srv->dmub->shared_state[DMUB_SHARED_SHARE_FEATURE__IPS_FW].data.ips_fw;

	memset(&cmd, 0, sizeof(cmd));
	cmd.idle_opt_notify_idle.header.type = DMUB_CMD__IDLE_OPT;
	cmd.idle_opt_notify_idle.header.sub_type = DMUB_CMD__IDLE_OPT_DCN_NOTIFY_IDLE;
	cmd.idle_opt_notify_idle.header.payload_bytes =
		sizeof(cmd.idle_opt_notify_idle) -
		sizeof(cmd.idle_opt_notify_idle.header);

	cmd.idle_opt_notify_idle.cntl_data.driver_idle = allow_idle;

	if (dc->work_arounds.skip_psr_ips_crtc_disable)
		cmd.idle_opt_notify_idle.cntl_data.skip_otg_disable = true;

	if (allow_idle) {
		volatile struct dmub_shared_state_ips_driver *ips_driver =
			&dc_dmub_srv->dmub->shared_state[DMUB_SHARED_SHARE_FEATURE__IPS_DRIVER].data.ips_driver;
		union dmub_shared_state_ips_driver_signals new_signals;

		DC_LOG_IPS(
			"%s wait idle (ips1_commit=%u ips2_commit=%u)",
			__func__,
			ips_fw->signals.bits.ips1_commit,
			ips_fw->signals.bits.ips2_commit);

		dc_dmub_srv_wait_idle(dc->ctx->dmub_srv);

		memset(&new_signals, 0, sizeof(new_signals));

		new_signals.bits.allow_idle = 1; /* always set */

		if (dc->config.disable_ips == DMUB_IPS_ENABLE ||
		    dc->config.disable_ips == DMUB_IPS_DISABLE_DYNAMIC) {
			new_signals.bits.allow_pg = 1;
			new_signals.bits.allow_ips1 = 1;
			new_signals.bits.allow_ips2 = 1;
			new_signals.bits.allow_z10 = 1;
		} else if (dc->config.disable_ips == DMUB_IPS_DISABLE_IPS1) {
			new_signals.bits.allow_ips1 = 1;
		} else if (dc->config.disable_ips == DMUB_IPS_DISABLE_IPS2) {
			new_signals.bits.allow_pg = 1;
			new_signals.bits.allow_ips1 = 1;
		} else if (dc->config.disable_ips == DMUB_IPS_DISABLE_IPS2_Z10) {
			new_signals.bits.allow_pg = 1;
			new_signals.bits.allow_ips1 = 1;
			new_signals.bits.allow_ips2 = 1;
		} else if (dc->config.disable_ips == DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF) {
			/* TODO: Move this logic out to hwseq */
			if (count_active_streams(dc) == 0) {
				/* IPS2 - Display off */
				new_signals.bits.allow_pg = 1;
				new_signals.bits.allow_ips1 = 1;
				new_signals.bits.allow_ips2 = 1;
				new_signals.bits.allow_z10 = 1;
			} else {
				/* RCG only */
				new_signals.bits.allow_pg = 0;
				new_signals.bits.allow_ips1 = 1;
				new_signals.bits.allow_ips2 = 0;
				new_signals.bits.allow_z10 = 0;
			}
		}

		ips_driver->signals = new_signals;
		dc_dmub_srv->driver_signals = ips_driver->signals;
	}

	DC_LOG_IPS(
		"%s send allow_idle=%d (ips1_commit=%u ips2_commit=%u)",
		__func__,
		allow_idle,
		ips_fw->signals.bits.ips1_commit,
		ips_fw->signals.bits.ips2_commit);

	/* NOTE: This does not use the "wake" interface since this is part of the wake path. */
	/* We also do not perform a wait since DMCUB could enter idle after the notification. */
	dm_execute_dmub_cmd(dc->ctx, &cmd, allow_idle ? DM_DMUB_WAIT_TYPE_NO_WAIT : DM_DMUB_WAIT_TYPE_WAIT);

	/* Register access should stop at this point. */
	if (allow_idle)
		dc_dmub_srv->needs_idle_wake = true;
}

static void dc_dmub_srv_exit_low_power_state(const struct dc *dc)
{
	struct dc_dmub_srv *dc_dmub_srv;
	uint32_t rcg_exit_count = 0, ips1_exit_count = 0, ips2_exit_count = 0;

	if (dc->debug.dmcub_emulation)
		return;

	if (!dc->ctx->dmub_srv || !dc->ctx->dmub_srv->dmub)
		return;

	dc_dmub_srv = dc->ctx->dmub_srv;

	if (dc->clk_mgr->funcs->exit_low_power_state) {
		volatile const struct dmub_shared_state_ips_fw *ips_fw =
			&dc_dmub_srv->dmub->shared_state[DMUB_SHARED_SHARE_FEATURE__IPS_FW].data.ips_fw;
		volatile struct dmub_shared_state_ips_driver *ips_driver =
			&dc_dmub_srv->dmub->shared_state[DMUB_SHARED_SHARE_FEATURE__IPS_DRIVER].data.ips_driver;
		union dmub_shared_state_ips_driver_signals prev_driver_signals = ips_driver->signals;

		rcg_exit_count = ips_fw->rcg_exit_count;
		ips1_exit_count = ips_fw->ips1_exit_count;
		ips2_exit_count = ips_fw->ips2_exit_count;

		ips_driver->signals.all = 0;
		dc_dmub_srv->driver_signals = ips_driver->signals;

		DC_LOG_IPS(
			"%s (allow ips1=%u ips2=%u) (commit ips1=%u ips2=%u) (count rcg=%u ips1=%u ips2=%u)",
			__func__,
			ips_driver->signals.bits.allow_ips1,
			ips_driver->signals.bits.allow_ips2,
			ips_fw->signals.bits.ips1_commit,
			ips_fw->signals.bits.ips2_commit,
			ips_fw->rcg_entry_count,
			ips_fw->ips1_entry_count,
			ips_fw->ips2_entry_count);

		/* Note: register access has technically not resumed for DCN here, but we
		 * need to be message PMFW through our standard register interface.
		 */
		dc_dmub_srv->needs_idle_wake = false;

		if ((prev_driver_signals.bits.allow_ips2 || prev_driver_signals.all == 0) &&
		    (!dc->debug.optimize_ips_handshake ||
		     ips_fw->signals.bits.ips2_commit || !ips_fw->signals.bits.in_idle)) {
			DC_LOG_IPS(
				"wait IPS2 eval (ips1_commit=%u ips2_commit=%u)",
				ips_fw->signals.bits.ips1_commit,
				ips_fw->signals.bits.ips2_commit);

			if (!dc->debug.optimize_ips_handshake || !ips_fw->signals.bits.ips2_commit)
				udelay(dc->debug.ips2_eval_delay_us);

			if (ips_fw->signals.bits.ips2_commit) {
				DC_LOG_IPS(
					"exit IPS2 #1 (ips1_commit=%u ips2_commit=%u)",
					ips_fw->signals.bits.ips1_commit,
					ips_fw->signals.bits.ips2_commit);

				// Tell PMFW to exit low power state
				dc->clk_mgr->funcs->exit_low_power_state(dc->clk_mgr);

				DC_LOG_IPS(
					"wait IPS2 entry delay (ips1_commit=%u ips2_commit=%u)",
					ips_fw->signals.bits.ips1_commit,
					ips_fw->signals.bits.ips2_commit);

				// Wait for IPS2 entry upper bound
				udelay(dc->debug.ips2_entry_delay_us);

				DC_LOG_IPS(
					"exit IPS2 #2 (ips1_commit=%u ips2_commit=%u)",
					ips_fw->signals.bits.ips1_commit,
					ips_fw->signals.bits.ips2_commit);

				dc->clk_mgr->funcs->exit_low_power_state(dc->clk_mgr);

				DC_LOG_IPS(
					"wait IPS2 commit clear (ips1_commit=%u ips2_commit=%u)",
					ips_fw->signals.bits.ips1_commit,
					ips_fw->signals.bits.ips2_commit);

				while (ips_fw->signals.bits.ips2_commit)
					udelay(1);

				DC_LOG_IPS(
					"wait hw_pwr_up (ips1_commit=%u ips2_commit=%u)",
					ips_fw->signals.bits.ips1_commit,
					ips_fw->signals.bits.ips2_commit);

				if (!dc_dmub_srv_is_hw_pwr_up(dc->ctx->dmub_srv, true))
					ASSERT(0);

				DC_LOG_IPS(
					"resync inbox1 (ips1_commit=%u ips2_commit=%u)",
					ips_fw->signals.bits.ips1_commit,
					ips_fw->signals.bits.ips2_commit);

				dmub_srv_sync_inbox1(dc->ctx->dmub_srv->dmub);
			}
		}

		dc_dmub_srv_notify_idle(dc, false);
		if (prev_driver_signals.bits.allow_ips1 || prev_driver_signals.all == 0) {
			DC_LOG_IPS(
				"wait for IPS1 commit clear (ips1_commit=%u ips2_commit=%u)",
				ips_fw->signals.bits.ips1_commit,
				ips_fw->signals.bits.ips2_commit);

			while (ips_fw->signals.bits.ips1_commit)
				udelay(1);

			DC_LOG_IPS(
				"wait for IPS1 commit clear done (ips1_commit=%u ips2_commit=%u)",
				ips_fw->signals.bits.ips1_commit,
				ips_fw->signals.bits.ips2_commit);
		}
	}

	if (!dc_dmub_srv_is_hw_pwr_up(dc->ctx->dmub_srv, true))
		ASSERT(0);

	DC_LOG_IPS("%s exit (count rcg=%u ips1=%u ips2=%u)",
		__func__,
		rcg_exit_count,
		ips1_exit_count,
		ips2_exit_count);
}

void dc_dmub_srv_set_power_state(struct dc_dmub_srv *dc_dmub_srv, enum dc_acpi_cm_power_state power_state)
{
	struct dmub_srv *dmub;

	if (!dc_dmub_srv)
		return;

	dmub = dc_dmub_srv->dmub;

	if (power_state == DC_ACPI_CM_POWER_STATE_D0)
		dmub_srv_set_power_state(dmub, DMUB_POWER_STATE_D0);
	else
		dmub_srv_set_power_state(dmub, DMUB_POWER_STATE_D3);
}

void dc_dmub_srv_notify_fw_dc_power_state(struct dc_dmub_srv *dc_dmub_srv,
					  enum dc_acpi_cm_power_state power_state)
{
	union dmub_rb_cmd cmd;

	if (!dc_dmub_srv)
		return;

	memset(&cmd, 0, sizeof(cmd));

	cmd.idle_opt_set_dc_power_state.header.type = DMUB_CMD__IDLE_OPT;
	cmd.idle_opt_set_dc_power_state.header.sub_type = DMUB_CMD__IDLE_OPT_SET_DC_POWER_STATE;
	cmd.idle_opt_set_dc_power_state.header.payload_bytes =
		sizeof(cmd.idle_opt_set_dc_power_state) - sizeof(cmd.idle_opt_set_dc_power_state.header);

	if (power_state == DC_ACPI_CM_POWER_STATE_D0) {
		cmd.idle_opt_set_dc_power_state.data.power_state = DMUB_IDLE_OPT_DC_POWER_STATE_D0;
	} else if (power_state == DC_ACPI_CM_POWER_STATE_D3) {
		cmd.idle_opt_set_dc_power_state.data.power_state = DMUB_IDLE_OPT_DC_POWER_STATE_D3;
	} else {
		cmd.idle_opt_set_dc_power_state.data.power_state = DMUB_IDLE_OPT_DC_POWER_STATE_UNKNOWN;
	}

	dc_wake_and_execute_dmub_cmd(dc_dmub_srv->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

bool dc_dmub_srv_should_detect(struct dc_dmub_srv *dc_dmub_srv)
{
	volatile const struct dmub_shared_state_ips_fw *ips_fw;
	bool reallow_idle = false, should_detect = false;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	if (dc_dmub_srv->dmub->shared_state &&
	    dc_dmub_srv->dmub->meta_info.feature_bits.bits.shared_state_link_detection) {
		ips_fw = &dc_dmub_srv->dmub->shared_state[DMUB_SHARED_SHARE_FEATURE__IPS_FW].data.ips_fw;
		return ips_fw->signals.bits.detection_required;
	}

	/* Detection may require reading scratch 0 - exit out of idle prior to the read. */
	if (dc_dmub_srv->idle_allowed) {
		dc_dmub_srv_apply_idle_power_optimizations(dc_dmub_srv->ctx->dc, false);
		reallow_idle = true;
	}

	should_detect = dmub_srv_should_detect(dc_dmub_srv->dmub);

	/* Re-enter idle if we're not about to immediately redetect links. */
	if (!should_detect && reallow_idle && dc_dmub_srv->idle_exit_counter == 0 &&
	    !dc_dmub_srv->ctx->dc->debug.disable_dmub_reallow_idle)
		dc_dmub_srv_apply_idle_power_optimizations(dc_dmub_srv->ctx->dc, true);

	return should_detect;
}

void dc_dmub_srv_apply_idle_power_optimizations(const struct dc *dc, bool allow_idle)
{
	struct dc_dmub_srv *dc_dmub_srv = dc->ctx->dmub_srv;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return;

	allow_idle &= (!dc->debug.ips_disallow_entry);

	if (dc_dmub_srv->idle_allowed == allow_idle)
		return;

	DC_LOG_IPS("%s state change: old=%d new=%d", __func__, dc_dmub_srv->idle_allowed, allow_idle);

	/*
	 * Entering a low power state requires a driver notification.
	 * Powering up the hardware requires notifying PMFW and DMCUB.
	 * Clearing the driver idle allow requires a DMCUB command.
	 * DMCUB commands requires the DMCUB to be powered up and restored.
	 */

	if (!allow_idle) {
		dc_dmub_srv->idle_exit_counter += 1;

		dc_dmub_srv_exit_low_power_state(dc);
		/*
		 * Idle is considered fully exited only after the sequence above
		 * fully completes. If we have a race of two threads exiting
		 * at the same time then it's safe to perform the sequence
		 * twice as long as we're not re-entering.
		 *
		 * Infinite command submission is avoided by using the
		 * dm_execute_dmub_cmd submission instead of the "wake" helpers.
		 */
		dc_dmub_srv->idle_allowed = false;

		dc_dmub_srv->idle_exit_counter -= 1;
		if (dc_dmub_srv->idle_exit_counter < 0) {
			ASSERT(0);
			dc_dmub_srv->idle_exit_counter = 0;
		}
	} else {
		/* Consider idle as notified prior to the actual submission to
		 * prevent multiple entries. */
		dc_dmub_srv->idle_allowed = true;

		dc_dmub_srv_notify_idle(dc, allow_idle);
	}
}

bool dc_wake_and_execute_dmub_cmd(const struct dc_context *ctx, union dmub_rb_cmd *cmd,
				  enum dm_dmub_wait_type wait_type)
{
	return dc_wake_and_execute_dmub_cmd_list(ctx, 1, cmd, wait_type);
}

bool dc_wake_and_execute_dmub_cmd_list(const struct dc_context *ctx, unsigned int count,
				       union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type)
{
	struct dc_dmub_srv *dc_dmub_srv = ctx->dmub_srv;
	bool result = false, reallow_idle = false;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	if (count == 0)
		return true;

	if (dc_dmub_srv->idle_allowed) {
		dc_dmub_srv_apply_idle_power_optimizations(ctx->dc, false);
		reallow_idle = true;
	}

	/*
	 * These may have different implementations in DM, so ensure
	 * that we guide it to the expected helper.
	 */
	if (count > 1)
		result = dm_execute_dmub_cmd_list(ctx, count, cmd, wait_type);
	else
		result = dm_execute_dmub_cmd(ctx, cmd, wait_type);

	if (result && reallow_idle && dc_dmub_srv->idle_exit_counter == 0 &&
	    !ctx->dc->debug.disable_dmub_reallow_idle)
		dc_dmub_srv_apply_idle_power_optimizations(ctx->dc, true);

	return result;
}

static bool dc_dmub_execute_gpint(const struct dc_context *ctx, enum dmub_gpint_command command_code,
				  uint16_t param, uint32_t *response, enum dm_dmub_wait_type wait_type)
{
	struct dc_dmub_srv *dc_dmub_srv = ctx->dmub_srv;
	const uint32_t wait_us = wait_type == DM_DMUB_WAIT_TYPE_NO_WAIT ? 0 : 30;
	enum dmub_status status;

	if (response)
		*response = 0;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	status = dmub_srv_send_gpint_command(dc_dmub_srv->dmub, command_code, param, wait_us);
	if (status != DMUB_STATUS_OK) {
		if (status == DMUB_STATUS_TIMEOUT && wait_type == DM_DMUB_WAIT_TYPE_NO_WAIT)
			return true;

		return false;
	}

	if (response && wait_type == DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY)
		dmub_srv_get_gpint_response(dc_dmub_srv->dmub, response);

	return true;
}

bool dc_wake_and_execute_gpint(const struct dc_context *ctx, enum dmub_gpint_command command_code,
			       uint16_t param, uint32_t *response, enum dm_dmub_wait_type wait_type)
{
	struct dc_dmub_srv *dc_dmub_srv = ctx->dmub_srv;
	bool result = false, reallow_idle = false;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	if (dc_dmub_srv->idle_allowed) {
		dc_dmub_srv_apply_idle_power_optimizations(ctx->dc, false);
		reallow_idle = true;
	}

	result = dc_dmub_execute_gpint(ctx, command_code, param, response, wait_type);

	if (result && reallow_idle && dc_dmub_srv->idle_exit_counter == 0 &&
	    !ctx->dc->debug.disable_dmub_reallow_idle)
		dc_dmub_srv_apply_idle_power_optimizations(ctx->dc, true);

	return result;
}

void dc_dmub_srv_fams2_update_config(struct dc *dc,
		struct dc_state *context,
		bool enable)
{
	uint8_t num_cmds = 1;
	uint32_t i;
	union dmub_rb_cmd cmd[MAX_STREAMS + 1];
	struct dmub_rb_cmd_fams2 *global_cmd = &cmd[0].fams2_config;

	memset(cmd, 0, sizeof(union dmub_rb_cmd) * (MAX_STREAMS + 1));
	/* fill in generic command header */
	global_cmd->header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	global_cmd->header.sub_type = DMUB_CMD__FAMS2_CONFIG;
	global_cmd->header.payload_bytes = sizeof(struct dmub_rb_cmd_fams2) - sizeof(struct dmub_cmd_header);

	if (enable) {
		/* send global configuration parameters */
		memcpy(&global_cmd->config.global, &context->bw_ctx.bw.dcn.fams2_global_config, sizeof(struct dmub_cmd_fams2_global_config));

		/* copy static feature configuration overrides */
		global_cmd->config.global.features.bits.enable_stall_recovery = dc->debug.fams2_config.bits.enable_stall_recovery;
		global_cmd->config.global.features.bits.enable_debug = dc->debug.fams2_config.bits.enable_debug;
		global_cmd->config.global.features.bits.enable_offload_flip = dc->debug.fams2_config.bits.enable_offload_flip;

		/* construct per-stream configs */
		for (i = 0; i < context->bw_ctx.bw.dcn.fams2_global_config.num_streams; i++) {
			struct dmub_rb_cmd_fams2 *stream_cmd = &cmd[i+1].fams2_config;

			/* configure command header */
			stream_cmd->header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
			stream_cmd->header.sub_type = DMUB_CMD__FAMS2_CONFIG;
			stream_cmd->header.payload_bytes = sizeof(struct dmub_rb_cmd_fams2) - sizeof(struct dmub_cmd_header);
			stream_cmd->header.multi_cmd_pending = 1;
			/* copy stream static state */
			memcpy(&stream_cmd->config.stream,
					&context->bw_ctx.bw.dcn.fams2_stream_params[i],
					sizeof(struct dmub_fams2_stream_static_state));
		}
	}

	/* apply feature configuration based on current driver state */
	global_cmd->config.global.features.bits.enable_visual_confirm = dc->debug.visual_confirm == VISUAL_CONFIRM_FAMS2;
	global_cmd->config.global.features.bits.enable = enable;

	if (enable && context->bw_ctx.bw.dcn.fams2_global_config.features.bits.enable) {
		/* set multi pending for global, and unset for last stream cmd */
		global_cmd->header.multi_cmd_pending = 1;
		cmd[context->bw_ctx.bw.dcn.fams2_global_config.num_streams].fams2_config.header.multi_cmd_pending = 0;
		num_cmds += context->bw_ctx.bw.dcn.fams2_global_config.num_streams;
	}

	dm_execute_dmub_cmd_list(dc->ctx, num_cmds, cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

void dc_dmub_srv_fams2_drr_update(struct dc *dc,
		uint32_t tg_inst,
		uint32_t vtotal_min,
		uint32_t vtotal_max,
		uint32_t vtotal_mid,
		uint32_t vtotal_mid_frame_num,
		bool program_manual_trigger)
{
	union dmub_rb_cmd cmd = { 0 };

	cmd.fams2_drr_update.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
	cmd.fams2_drr_update.header.sub_type = DMUB_CMD__FAMS2_DRR_UPDATE;
	cmd.fams2_drr_update.dmub_optc_state_req.tg_inst = tg_inst;
	cmd.fams2_drr_update.dmub_optc_state_req.v_total_max = vtotal_max;
	cmd.fams2_drr_update.dmub_optc_state_req.v_total_min = vtotal_min;
	cmd.fams2_drr_update.dmub_optc_state_req.v_total_mid = vtotal_mid;
	cmd.fams2_drr_update.dmub_optc_state_req.v_total_mid_frame_num = vtotal_mid_frame_num;
	cmd.fams2_drr_update.dmub_optc_state_req.program_manual_trigger = program_manual_trigger;

	cmd.fams2_drr_update.header.payload_bytes = sizeof(cmd.fams2_drr_update) - sizeof(cmd.fams2_drr_update.header);

	dm_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

void dc_dmub_srv_fams2_passthrough_flip(
		struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *stream,
		struct dc_surface_update *srf_updates,
		int surface_count)
{
	int plane_index;
	union dmub_rb_cmd cmds[MAX_PLANES];
	struct dc_plane_address *address;
	struct dc_plane_state *plane_state;
	int num_cmds = 0;
	struct dc_stream_status *stream_status = dc_stream_get_status(stream);

	if (surface_count <= 0 || stream_status == NULL)
		return;

	memset(cmds, 0, sizeof(union dmub_rb_cmd) * MAX_PLANES);

	/* build command for each surface update */
	for (plane_index = 0; plane_index < surface_count; plane_index++) {
		plane_state = srf_updates[plane_index].surface;
		address = &plane_state->address;

		/* skip if there is no address update for plane */
		if (!srf_updates[plane_index].flip_addr)
			continue;

		/* build command header */
		cmds[num_cmds].fams2_flip.header.type = DMUB_CMD__FW_ASSISTED_MCLK_SWITCH;
		cmds[num_cmds].fams2_flip.header.sub_type = DMUB_CMD__FAMS2_FLIP;
		cmds[num_cmds].fams2_flip.header.payload_bytes = sizeof(struct dmub_rb_cmd_fams2_flip);

		/* for chaining multiple commands, all but last command should set to 1 */
		cmds[num_cmds].fams2_flip.header.multi_cmd_pending = 1;

		/* set topology info */
		cmds[num_cmds].fams2_flip.flip_info.pipe_mask = dc_plane_get_pipe_mask(state, plane_state);
		if (stream_status)
			cmds[num_cmds].fams2_flip.flip_info.otg_inst = stream_status->primary_otg_inst;

		cmds[num_cmds].fams2_flip.flip_info.config.bits.is_immediate = plane_state->flip_immediate;

		/* build address info for command */
		switch (address->type) {
		case PLN_ADDR_TYPE_GRAPHICS:
			if (address->grph.addr.quad_part == 0) {
				BREAK_TO_DEBUGGER();
				break;
			}

			cmds[num_cmds].fams2_flip.flip_info.addr_info.meta_addr_lo =
					address->grph.meta_addr.low_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.meta_addr_hi =
					(uint16_t)address->grph.meta_addr.high_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.surf_addr_lo =
					address->grph.addr.low_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.surf_addr_hi =
					(uint16_t)address->grph.addr.high_part;
			break;
		case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
			if (address->video_progressive.luma_addr.quad_part == 0 ||
				address->video_progressive.chroma_addr.quad_part == 0) {
				BREAK_TO_DEBUGGER();
				break;
			}

			cmds[num_cmds].fams2_flip.flip_info.addr_info.meta_addr_lo =
					address->video_progressive.luma_meta_addr.low_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.meta_addr_hi =
					(uint16_t)address->video_progressive.luma_meta_addr.high_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.meta_addr_c_lo =
					address->video_progressive.chroma_meta_addr.low_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.meta_addr_c_hi =
					(uint16_t)address->video_progressive.chroma_meta_addr.high_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.surf_addr_lo =
					address->video_progressive.luma_addr.low_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.surf_addr_hi =
					(uint16_t)address->video_progressive.luma_addr.high_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.surf_addr_c_lo =
					address->video_progressive.chroma_addr.low_part;
			cmds[num_cmds].fams2_flip.flip_info.addr_info.surf_addr_c_hi =
					(uint16_t)address->video_progressive.chroma_addr.high_part;
			break;
		default:
			// Should never be hit
			BREAK_TO_DEBUGGER();
			break;
		}

		num_cmds++;
	}

	if (num_cmds > 0)  {
		cmds[num_cmds - 1].fams2_flip.header.multi_cmd_pending = 0;
		dm_execute_dmub_cmd_list(dc->ctx, num_cmds, cmds, DM_DMUB_WAIT_TYPE_WAIT);
	}
}

bool dc_dmub_srv_ips_residency_cntl(struct dc_dmub_srv *dc_dmub_srv, bool start_measurement)
{
	bool result;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	result = dc_wake_and_execute_gpint(dc_dmub_srv->ctx, DMUB_GPINT__IPS_RESIDENCY,
					   start_measurement, NULL, DM_DMUB_WAIT_TYPE_WAIT);

	return result;
}

void dc_dmub_srv_ips_query_residency_info(struct dc_dmub_srv *dc_dmub_srv, struct ips_residency_info *output)
{
	uint32_t i;
	enum dmub_gpint_command command_code;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return;

	switch (output->ips_mode) {
	case DMUB_IPS_MODE_IPS1_MAX:
		command_code = DMUB_GPINT__GET_IPS1_HISTOGRAM_COUNTER;
		break;
	case DMUB_IPS_MODE_IPS2:
		command_code = DMUB_GPINT__GET_IPS2_HISTOGRAM_COUNTER;
		break;
	case DMUB_IPS_MODE_IPS1_RCG:
		command_code = DMUB_GPINT__GET_IPS1_RCG_HISTOGRAM_COUNTER;
		break;
	case DMUB_IPS_MODE_IPS1_ONO2_ON:
		command_code = DMUB_GPINT__GET_IPS1_ONO2_ON_HISTOGRAM_COUNTER;
		break;
	default:
		command_code = DMUB_GPINT__INVALID_COMMAND;
		break;
	}

	if (command_code == DMUB_GPINT__INVALID_COMMAND)
		return;

	// send gpint commands and wait for ack
	if (!dc_wake_and_execute_gpint(dc_dmub_srv->ctx, DMUB_GPINT__GET_IPS_RESIDENCY_PERCENT,
				      (uint16_t)(output->ips_mode),
				       &output->residency_percent, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		output->residency_percent = 0;

	if (!dc_wake_and_execute_gpint(dc_dmub_srv->ctx, DMUB_GPINT__GET_IPS_RESIDENCY_ENTRY_COUNTER,
				      (uint16_t)(output->ips_mode),
				       &output->entry_counter, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		output->entry_counter = 0;

	if (!dc_wake_and_execute_gpint(dc_dmub_srv->ctx, DMUB_GPINT__GET_IPS_RESIDENCY_DURATION_US_LO,
				      (uint16_t)(output->ips_mode),
				       &output->total_active_time_us[0], DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		output->total_active_time_us[0] = 0;
	if (!dc_wake_and_execute_gpint(dc_dmub_srv->ctx, DMUB_GPINT__GET_IPS_RESIDENCY_DURATION_US_HI,
				      (uint16_t)(output->ips_mode),
				       &output->total_active_time_us[1], DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		output->total_active_time_us[1] = 0;

	if (!dc_wake_and_execute_gpint(dc_dmub_srv->ctx, DMUB_GPINT__GET_IPS_INACTIVE_RESIDENCY_DURATION_US_LO,
				      (uint16_t)(output->ips_mode),
				       &output->total_inactive_time_us[0], DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		output->total_inactive_time_us[0] = 0;
	if (!dc_wake_and_execute_gpint(dc_dmub_srv->ctx, DMUB_GPINT__GET_IPS_INACTIVE_RESIDENCY_DURATION_US_HI,
				      (uint16_t)(output->ips_mode),
				       &output->total_inactive_time_us[1], DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		output->total_inactive_time_us[1] = 0;

	// NUM_IPS_HISTOGRAM_BUCKETS = 16
	for (i = 0; i < 16; i++)
		if (!dc_wake_and_execute_gpint(dc_dmub_srv->ctx, command_code, i, &output->histogram[i],
					       DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
			output->histogram[i] = 0;
}
