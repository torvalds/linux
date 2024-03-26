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

/* FILE POLICY AND INTENDED USAGE:
 * This file owns the programming sequence of stream's dpms state associated
 * with the link and link's enable/disable sequences as result of the stream's
 * dpms state change.
 *
 * TODO - The reason link owns stream's dpms programming sequence is
 * because dpms programming sequence is highly dependent on underlying signal
 * specific link protocols. This unfortunately causes link to own a portion of
 * stream state programming sequence. This creates a gray area where the
 * boundary between link and stream is not clearly defined.
 */

#include "link_dpms.h"
#include "link_hwss.h"
#include "link_validation.h"
#include "accessories/link_dp_trace.h"
#include "protocols/link_dpcd.h"
#include "protocols/link_ddc.h"
#include "protocols/link_hpd.h"
#include "protocols/link_dp_phy.h"
#include "protocols/link_dp_capability.h"
#include "protocols/link_dp_training.h"
#include "protocols/link_edp_panel_control.h"
#include "protocols/link_dp_dpia_bw.h"

#include "dm_helpers.h"
#include "link_enc_cfg.h"
#include "resource.h"
#include "dsc.h"
#include "dccg.h"
#include "clk_mgr.h"
#include "atomfirmware.h"
#define DC_LOGGER \
	dc_logger
#define DC_LOGGER_INIT(logger) \
	struct dal_logger *dc_logger = logger

#define LINK_INFO(...) \
	DC_LOG_HW_HOTPLUG(  \
		__VA_ARGS__)

#define RETIMER_REDRIVER_INFO(...) \
	DC_LOG_RETIMER_REDRIVER(  \
		__VA_ARGS__)
#include "dc/dcn30/dcn30_vpg.h"

#define MAX_MTP_SLOT_COUNT 64
#define LINK_TRAINING_ATTEMPTS 4
#define PEAK_FACTOR_X1000 1006

void link_blank_all_dp_displays(struct dc *dc)
{
	unsigned int i;
	uint8_t dpcd_power_state = '\0';
	enum dc_status status = DC_ERROR_UNEXPECTED;

	for (i = 0; i < dc->link_count; i++) {
		if ((dc->links[i]->connector_signal != SIGNAL_TYPE_DISPLAY_PORT) ||
			(dc->links[i]->priv == NULL) || (dc->links[i]->local_sink == NULL))
			continue;

		/* DP 2.0 spec requires that we read LTTPR caps first */
		dp_retrieve_lttpr_cap(dc->links[i]);
		/* if any of the displays are lit up turn them off */
		status = core_link_read_dpcd(dc->links[i], DP_SET_POWER,
							&dpcd_power_state, sizeof(dpcd_power_state));

		if (status == DC_OK && dpcd_power_state == DP_POWER_STATE_D0)
			link_blank_dp_stream(dc->links[i], true);
	}

}

void link_blank_all_edp_displays(struct dc *dc)
{
	unsigned int i;
	uint8_t dpcd_power_state = '\0';
	enum dc_status status = DC_ERROR_UNEXPECTED;

	for (i = 0; i < dc->link_count; i++) {
		if ((dc->links[i]->connector_signal != SIGNAL_TYPE_EDP) ||
			(!dc->links[i]->edp_sink_present))
			continue;

		/* if any of the displays are lit up turn them off */
		status = core_link_read_dpcd(dc->links[i], DP_SET_POWER,
							&dpcd_power_state, sizeof(dpcd_power_state));

		if (status == DC_OK && dpcd_power_state == DP_POWER_STATE_D0)
			link_blank_dp_stream(dc->links[i], true);
	}
}

void link_blank_dp_stream(struct dc_link *link, bool hw_init)
{
	unsigned int j;
	struct dc  *dc = link->ctx->dc;
	enum signal_type signal = link->connector_signal;

	if ((signal == SIGNAL_TYPE_EDP) ||
		(signal == SIGNAL_TYPE_DISPLAY_PORT)) {
		if (link->ep_type == DISPLAY_ENDPOINT_PHY &&
			link->link_enc->funcs->get_dig_frontend &&
			link->link_enc->funcs->is_dig_enabled(link->link_enc)) {
			unsigned int fe = link->link_enc->funcs->get_dig_frontend(link->link_enc);

			if (fe != ENGINE_ID_UNKNOWN)
				for (j = 0; j < dc->res_pool->stream_enc_count; j++) {
					if (fe == dc->res_pool->stream_enc[j]->id) {
						dc->res_pool->stream_enc[j]->funcs->dp_blank(link,
									dc->res_pool->stream_enc[j]);
						break;
					}
				}
		}

		if ((!link->wa_flags.dp_keep_receiver_powered) || hw_init)
			dpcd_write_rx_power_ctrl(link, false);
	}
}

void link_set_all_streams_dpms_off_for_link(struct dc_link *link)
{
	struct pipe_ctx *pipes[MAX_PIPES];
	struct dc_state *state = link->dc->current_state;
	uint8_t count;
	int i;
	struct dc_stream_update stream_update;
	bool dpms_off = true;
	struct link_resource link_res = {0};

	memset(&stream_update, 0, sizeof(stream_update));
	stream_update.dpms_off = &dpms_off;

	link_get_master_pipes_with_dpms_on(link, state, &count, pipes);

	for (i = 0; i < count; i++) {
		stream_update.stream = pipes[i]->stream;
		dc_commit_updates_for_stream(link->ctx->dc, NULL, 0,
				pipes[i]->stream, &stream_update,
				state);
	}

	/* link can be also enabled by vbios. In this case it is not recorded
	 * in pipe_ctx. Disable link phy here to make sure it is completely off
	 */
	dp_disable_link_phy(link, &link_res, link->connector_signal);
}

void link_resume(struct dc_link *link)
{
	if (link->connector_signal != SIGNAL_TYPE_VIRTUAL)
		program_hpd_filter(link);
}

/* This function returns true if the pipe is used to feed video signal directly
 * to the link.
 */
static bool is_master_pipe_for_link(const struct dc_link *link,
		const struct pipe_ctx *pipe)
{
	return resource_is_pipe_type(pipe, OTG_MASTER) &&
			pipe->stream->link == link;
}

/*
 * This function finds all master pipes feeding to a given link with dpms set to
 * on in given dc state.
 */
void link_get_master_pipes_with_dpms_on(const struct dc_link *link,
		struct dc_state *state,
		uint8_t *count,
		struct pipe_ctx *pipes[MAX_PIPES])
{
	int i;
	struct pipe_ctx *pipe = NULL;

	*count = 0;
	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &state->res_ctx.pipe_ctx[i];

		if (is_master_pipe_for_link(link, pipe) &&
				pipe->stream->dpms_off == false) {
			pipes[(*count)++] = pipe;
		}
	}
}

static bool get_ext_hdmi_settings(struct pipe_ctx *pipe_ctx,
		enum engine_id eng_id,
		struct ext_hdmi_settings *settings)
{
	bool result = false;
	int i = 0;
	struct integrated_info *integrated_info =
			pipe_ctx->stream->ctx->dc_bios->integrated_info;

	if (integrated_info == NULL)
		return false;

	/*
	 * Get retimer settings from sbios for passing SI eye test for DCE11
	 * The setting values are varied based on board revision and port id
	 * Therefore the setting values of each ports is passed by sbios.
	 */

	// Check if current bios contains ext Hdmi settings
	if (integrated_info->gpu_cap_info & 0x20) {
		switch (eng_id) {
		case ENGINE_ID_DIGA:
			settings->slv_addr = integrated_info->dp0_ext_hdmi_slv_addr;
			settings->reg_num = integrated_info->dp0_ext_hdmi_6g_reg_num;
			settings->reg_num_6g = integrated_info->dp0_ext_hdmi_6g_reg_num;
			memmove(settings->reg_settings,
					integrated_info->dp0_ext_hdmi_reg_settings,
					sizeof(integrated_info->dp0_ext_hdmi_reg_settings));
			memmove(settings->reg_settings_6g,
					integrated_info->dp0_ext_hdmi_6g_reg_settings,
					sizeof(integrated_info->dp0_ext_hdmi_6g_reg_settings));
			result = true;
			break;
		case ENGINE_ID_DIGB:
			settings->slv_addr = integrated_info->dp1_ext_hdmi_slv_addr;
			settings->reg_num = integrated_info->dp1_ext_hdmi_6g_reg_num;
			settings->reg_num_6g = integrated_info->dp1_ext_hdmi_6g_reg_num;
			memmove(settings->reg_settings,
					integrated_info->dp1_ext_hdmi_reg_settings,
					sizeof(integrated_info->dp1_ext_hdmi_reg_settings));
			memmove(settings->reg_settings_6g,
					integrated_info->dp1_ext_hdmi_6g_reg_settings,
					sizeof(integrated_info->dp1_ext_hdmi_6g_reg_settings));
			result = true;
			break;
		case ENGINE_ID_DIGC:
			settings->slv_addr = integrated_info->dp2_ext_hdmi_slv_addr;
			settings->reg_num = integrated_info->dp2_ext_hdmi_6g_reg_num;
			settings->reg_num_6g = integrated_info->dp2_ext_hdmi_6g_reg_num;
			memmove(settings->reg_settings,
					integrated_info->dp2_ext_hdmi_reg_settings,
					sizeof(integrated_info->dp2_ext_hdmi_reg_settings));
			memmove(settings->reg_settings_6g,
					integrated_info->dp2_ext_hdmi_6g_reg_settings,
					sizeof(integrated_info->dp2_ext_hdmi_6g_reg_settings));
			result = true;
			break;
		case ENGINE_ID_DIGD:
			settings->slv_addr = integrated_info->dp3_ext_hdmi_slv_addr;
			settings->reg_num = integrated_info->dp3_ext_hdmi_6g_reg_num;
			settings->reg_num_6g = integrated_info->dp3_ext_hdmi_6g_reg_num;
			memmove(settings->reg_settings,
					integrated_info->dp3_ext_hdmi_reg_settings,
					sizeof(integrated_info->dp3_ext_hdmi_reg_settings));
			memmove(settings->reg_settings_6g,
					integrated_info->dp3_ext_hdmi_6g_reg_settings,
					sizeof(integrated_info->dp3_ext_hdmi_6g_reg_settings));
			result = true;
			break;
		default:
			break;
		}

		if (result == true) {
			// Validate settings from bios integrated info table
			if (settings->slv_addr == 0)
				return false;
			if (settings->reg_num > 9)
				return false;
			if (settings->reg_num_6g > 3)
				return false;

			for (i = 0; i < settings->reg_num; i++) {
				if (settings->reg_settings[i].i2c_reg_index > 0x20)
					return false;
			}

			for (i = 0; i < settings->reg_num_6g; i++) {
				if (settings->reg_settings_6g[i].i2c_reg_index > 0x20)
					return false;
			}
		}
	}

	return result;
}

static bool write_i2c(struct pipe_ctx *pipe_ctx,
		uint8_t address, uint8_t *buffer, uint32_t length)
{
	struct i2c_command cmd = {0};
	struct i2c_payload payload = {0};

	memset(&payload, 0, sizeof(payload));
	memset(&cmd, 0, sizeof(cmd));

	cmd.number_of_payloads = 1;
	cmd.engine = I2C_COMMAND_ENGINE_DEFAULT;
	cmd.speed = pipe_ctx->stream->ctx->dc->caps.i2c_speed_in_khz;

	payload.address = address;
	payload.data = buffer;
	payload.length = length;
	payload.write = true;
	cmd.payloads = &payload;

	if (dm_helpers_submit_i2c(pipe_ctx->stream->ctx,
			pipe_ctx->stream->link, &cmd))
		return true;

	return false;
}

static void write_i2c_retimer_setting(
		struct pipe_ctx *pipe_ctx,
		bool is_vga_mode,
		bool is_over_340mhz,
		struct ext_hdmi_settings *settings)
{
	uint8_t slave_address = (settings->slv_addr >> 1);
	uint8_t buffer[2];
	const uint8_t apply_rx_tx_change = 0x4;
	uint8_t offset = 0xA;
	uint8_t value = 0;
	int i = 0;
	bool i2c_success = false;
	DC_LOGGER_INIT(pipe_ctx->stream->ctx->logger);

	memset(&buffer, 0, sizeof(buffer));

	/* Start Ext-Hdmi programming*/

	for (i = 0; i < settings->reg_num; i++) {
		/* Apply 3G settings */
		if (settings->reg_settings[i].i2c_reg_index <= 0x20) {

			buffer[0] = settings->reg_settings[i].i2c_reg_index;
			buffer[1] = settings->reg_settings[i].i2c_reg_val;
			i2c_success = write_i2c(pipe_ctx, slave_address,
						buffer, sizeof(buffer));
			RETIMER_REDRIVER_INFO("retimer write to slave_address = 0x%x,\
				offset = 0x%x, reg_val= 0x%x, i2c_success = %d\n",
				slave_address, buffer[0], buffer[1], i2c_success?1:0);

			if (!i2c_success)
				goto i2c_write_fail;

			/* Based on DP159 specs, APPLY_RX_TX_CHANGE bit in 0x0A
			 * needs to be set to 1 on every 0xA-0xC write.
			 */
			if (settings->reg_settings[i].i2c_reg_index == 0xA ||
				settings->reg_settings[i].i2c_reg_index == 0xB ||
				settings->reg_settings[i].i2c_reg_index == 0xC) {

				/* Query current value from offset 0xA */
				if (settings->reg_settings[i].i2c_reg_index == 0xA)
					value = settings->reg_settings[i].i2c_reg_val;
				else {
					i2c_success =
						link_query_ddc_data(
						pipe_ctx->stream->link->ddc,
						slave_address, &offset, 1, &value, 1);
					if (!i2c_success)
						goto i2c_write_fail;
				}

				buffer[0] = offset;
				/* Set APPLY_RX_TX_CHANGE bit to 1 */
				buffer[1] = value | apply_rx_tx_change;
				i2c_success = write_i2c(pipe_ctx, slave_address,
						buffer, sizeof(buffer));
				RETIMER_REDRIVER_INFO("retimer write to slave_address = 0x%x,\
					offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
					slave_address, buffer[0], buffer[1], i2c_success?1:0);
				if (!i2c_success)
					goto i2c_write_fail;
			}
		}
	}

	/* Apply 3G settings */
	if (is_over_340mhz) {
		for (i = 0; i < settings->reg_num_6g; i++) {
			/* Apply 3G settings */
			if (settings->reg_settings[i].i2c_reg_index <= 0x20) {

				buffer[0] = settings->reg_settings_6g[i].i2c_reg_index;
				buffer[1] = settings->reg_settings_6g[i].i2c_reg_val;
				i2c_success = write_i2c(pipe_ctx, slave_address,
							buffer, sizeof(buffer));
				RETIMER_REDRIVER_INFO("above 340Mhz: retimer write to slave_address = 0x%x,\
					offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
					slave_address, buffer[0], buffer[1], i2c_success?1:0);

				if (!i2c_success)
					goto i2c_write_fail;

				/* Based on DP159 specs, APPLY_RX_TX_CHANGE bit in 0x0A
				 * needs to be set to 1 on every 0xA-0xC write.
				 */
				if (settings->reg_settings_6g[i].i2c_reg_index == 0xA ||
					settings->reg_settings_6g[i].i2c_reg_index == 0xB ||
					settings->reg_settings_6g[i].i2c_reg_index == 0xC) {

					/* Query current value from offset 0xA */
					if (settings->reg_settings_6g[i].i2c_reg_index == 0xA)
						value = settings->reg_settings_6g[i].i2c_reg_val;
					else {
						i2c_success =
								link_query_ddc_data(
								pipe_ctx->stream->link->ddc,
								slave_address, &offset, 1, &value, 1);
						if (!i2c_success)
							goto i2c_write_fail;
					}

					buffer[0] = offset;
					/* Set APPLY_RX_TX_CHANGE bit to 1 */
					buffer[1] = value | apply_rx_tx_change;
					i2c_success = write_i2c(pipe_ctx, slave_address,
							buffer, sizeof(buffer));
					RETIMER_REDRIVER_INFO("retimer write to slave_address = 0x%x,\
						offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
						slave_address, buffer[0], buffer[1], i2c_success?1:0);
					if (!i2c_success)
						goto i2c_write_fail;
				}
			}
		}
	}

	if (is_vga_mode) {
		/* Program additional settings if using 640x480 resolution */

		/* Write offset 0xFF to 0x01 */
		buffer[0] = 0xff;
		buffer[1] = 0x01;
		i2c_success = write_i2c(pipe_ctx, slave_address,
				buffer, sizeof(buffer));
		RETIMER_REDRIVER_INFO("retimer write to slave_address = 0x%x,\
				offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
				slave_address, buffer[0], buffer[1], i2c_success?1:0);
		if (!i2c_success)
			goto i2c_write_fail;

		/* Write offset 0x00 to 0x23 */
		buffer[0] = 0x00;
		buffer[1] = 0x23;
		i2c_success = write_i2c(pipe_ctx, slave_address,
				buffer, sizeof(buffer));
		RETIMER_REDRIVER_INFO("retimer write to slave_address = 0x%x,\
			offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
			slave_address, buffer[0], buffer[1], i2c_success?1:0);
		if (!i2c_success)
			goto i2c_write_fail;

		/* Write offset 0xff to 0x00 */
		buffer[0] = 0xff;
		buffer[1] = 0x00;
		i2c_success = write_i2c(pipe_ctx, slave_address,
				buffer, sizeof(buffer));
		RETIMER_REDRIVER_INFO("retimer write to slave_address = 0x%x,\
			offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
			slave_address, buffer[0], buffer[1], i2c_success?1:0);
		if (!i2c_success)
			goto i2c_write_fail;

	}

	return;

i2c_write_fail:
	DC_LOG_DEBUG("Set retimer failed");
}

static void write_i2c_default_retimer_setting(
		struct pipe_ctx *pipe_ctx,
		bool is_vga_mode,
		bool is_over_340mhz)
{
	uint8_t slave_address = (0xBA >> 1);
	uint8_t buffer[2];
	bool i2c_success = false;
	DC_LOGGER_INIT(pipe_ctx->stream->ctx->logger);

	memset(&buffer, 0, sizeof(buffer));

	/* Program Slave Address for tuning single integrity */
	/* Write offset 0x0A to 0x13 */
	buffer[0] = 0x0A;
	buffer[1] = 0x13;
	i2c_success = write_i2c(pipe_ctx, slave_address,
			buffer, sizeof(buffer));
	RETIMER_REDRIVER_INFO("retimer writes default setting to slave_address = 0x%x,\
		offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
		slave_address, buffer[0], buffer[1], i2c_success?1:0);
	if (!i2c_success)
		goto i2c_write_fail;

	/* Write offset 0x0A to 0x17 */
	buffer[0] = 0x0A;
	buffer[1] = 0x17;
	i2c_success = write_i2c(pipe_ctx, slave_address,
			buffer, sizeof(buffer));
	RETIMER_REDRIVER_INFO("retimer write to slave_addr = 0x%x,\
		offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
		slave_address, buffer[0], buffer[1], i2c_success?1:0);
	if (!i2c_success)
		goto i2c_write_fail;

	/* Write offset 0x0B to 0xDA or 0xD8 */
	buffer[0] = 0x0B;
	buffer[1] = is_over_340mhz ? 0xDA : 0xD8;
	i2c_success = write_i2c(pipe_ctx, slave_address,
			buffer, sizeof(buffer));
	RETIMER_REDRIVER_INFO("retimer write to slave_addr = 0x%x,\
		offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
		slave_address, buffer[0], buffer[1], i2c_success?1:0);
	if (!i2c_success)
		goto i2c_write_fail;

	/* Write offset 0x0A to 0x17 */
	buffer[0] = 0x0A;
	buffer[1] = 0x17;
	i2c_success = write_i2c(pipe_ctx, slave_address,
			buffer, sizeof(buffer));
	RETIMER_REDRIVER_INFO("retimer write to slave_addr = 0x%x,\
		offset = 0x%x, reg_val= 0x%x, i2c_success = %d\n",
		slave_address, buffer[0], buffer[1], i2c_success?1:0);
	if (!i2c_success)
		goto i2c_write_fail;

	/* Write offset 0x0C to 0x1D or 0x91 */
	buffer[0] = 0x0C;
	buffer[1] = is_over_340mhz ? 0x1D : 0x91;
	i2c_success = write_i2c(pipe_ctx, slave_address,
			buffer, sizeof(buffer));
	RETIMER_REDRIVER_INFO("retimer write to slave_addr = 0x%x,\
		offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
		slave_address, buffer[0], buffer[1], i2c_success?1:0);
	if (!i2c_success)
		goto i2c_write_fail;

	/* Write offset 0x0A to 0x17 */
	buffer[0] = 0x0A;
	buffer[1] = 0x17;
	i2c_success = write_i2c(pipe_ctx, slave_address,
			buffer, sizeof(buffer));
	RETIMER_REDRIVER_INFO("retimer write to slave_addr = 0x%x,\
		offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
		slave_address, buffer[0], buffer[1], i2c_success?1:0);
	if (!i2c_success)
		goto i2c_write_fail;


	if (is_vga_mode) {
		/* Program additional settings if using 640x480 resolution */

		/* Write offset 0xFF to 0x01 */
		buffer[0] = 0xff;
		buffer[1] = 0x01;
		i2c_success = write_i2c(pipe_ctx, slave_address,
				buffer, sizeof(buffer));
		RETIMER_REDRIVER_INFO("retimer write to slave_addr = 0x%x,\
			offset = 0x%x, reg_val = 0x%x, i2c_success = %d\n",
			slave_address, buffer[0], buffer[1], i2c_success?1:0);
		if (!i2c_success)
			goto i2c_write_fail;

		/* Write offset 0x00 to 0x23 */
		buffer[0] = 0x00;
		buffer[1] = 0x23;
		i2c_success = write_i2c(pipe_ctx, slave_address,
				buffer, sizeof(buffer));
		RETIMER_REDRIVER_INFO("retimer write to slave_addr = 0x%x,\
			offset = 0x%x, reg_val= 0x%x, i2c_success = %d\n",
			slave_address, buffer[0], buffer[1], i2c_success?1:0);
		if (!i2c_success)
			goto i2c_write_fail;

		/* Write offset 0xff to 0x00 */
		buffer[0] = 0xff;
		buffer[1] = 0x00;
		i2c_success = write_i2c(pipe_ctx, slave_address,
				buffer, sizeof(buffer));
		RETIMER_REDRIVER_INFO("retimer write default setting to slave_addr = 0x%x,\
			offset = 0x%x, reg_val= 0x%x, i2c_success = %d end here\n",
			slave_address, buffer[0], buffer[1], i2c_success?1:0);
		if (!i2c_success)
			goto i2c_write_fail;
	}

	return;

i2c_write_fail:
	DC_LOG_DEBUG("Set default retimer failed");
}

static void write_i2c_redriver_setting(
		struct pipe_ctx *pipe_ctx,
		bool is_over_340mhz)
{
	uint8_t slave_address = (0xF0 >> 1);
	uint8_t buffer[16];
	bool i2c_success = false;
	DC_LOGGER_INIT(pipe_ctx->stream->ctx->logger);

	memset(&buffer, 0, sizeof(buffer));

	// Program Slave Address for tuning single integrity
	buffer[3] = 0x4E;
	buffer[4] = 0x4E;
	buffer[5] = 0x4E;
	buffer[6] = is_over_340mhz ? 0x4E : 0x4A;

	i2c_success = write_i2c(pipe_ctx, slave_address,
					buffer, sizeof(buffer));
	RETIMER_REDRIVER_INFO("redriver write 0 to all 16 reg offset expect following:\n\
		\t slave_addr = 0x%x, offset[3] = 0x%x, offset[4] = 0x%x,\
		offset[5] = 0x%x,offset[6] is_over_340mhz = 0x%x,\
		i2c_success = %d\n",
		slave_address, buffer[3], buffer[4], buffer[5], buffer[6], i2c_success?1:0);

	if (!i2c_success)
		DC_LOG_DEBUG("Set redriver failed");
}

static void update_psp_stream_config(struct pipe_ctx *pipe_ctx, bool dpms_off)
{
	struct cp_psp *cp_psp = &pipe_ctx->stream->ctx->cp_psp;
	struct link_encoder *link_enc = NULL;
	struct cp_psp_stream_config config = {0};
	enum dp_panel_mode panel_mode =
			dp_get_panel_mode(pipe_ctx->stream->link);

	if (cp_psp == NULL || cp_psp->funcs.update_stream_config == NULL)
		return;

	link_enc = link_enc_cfg_get_link_enc(pipe_ctx->stream->link);
	ASSERT(link_enc);
	if (link_enc == NULL)
		return;

	/* otg instance */
	config.otg_inst = (uint8_t) pipe_ctx->stream_res.tg->inst;

	/* dig front end */
	config.dig_fe = (uint8_t) pipe_ctx->stream_res.stream_enc->stream_enc_inst;

	/* stream encoder index */
	config.stream_enc_idx = pipe_ctx->stream_res.stream_enc->id - ENGINE_ID_DIGA;
	if (dp_is_128b_132b_signal(pipe_ctx))
		config.stream_enc_idx =
				pipe_ctx->stream_res.hpo_dp_stream_enc->id - ENGINE_ID_HPO_DP_0;

	/* dig back end */
	config.dig_be = pipe_ctx->stream->link->link_enc_hw_inst;

	/* link encoder index */
	config.link_enc_idx = link_enc->transmitter - TRANSMITTER_UNIPHY_A;
	if (dp_is_128b_132b_signal(pipe_ctx))
		config.link_enc_idx = pipe_ctx->link_res.hpo_dp_link_enc->inst;

	/* dio output index is dpia index for DPIA endpoint & dcio index by default */
	if (pipe_ctx->stream->link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA)
		config.dio_output_idx = pipe_ctx->stream->link->link_id.enum_id - ENUM_ID_1;
	else
		config.dio_output_idx = link_enc->transmitter - TRANSMITTER_UNIPHY_A;


	/* phy index */
	config.phy_idx = resource_transmitter_to_phy_idx(
			pipe_ctx->stream->link->dc, link_enc->transmitter);
	if (pipe_ctx->stream->link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA)
		/* USB4 DPIA doesn't use PHY in our soc, initialize it to 0 */
		config.phy_idx = 0;

	/* stream properties */
	config.assr_enabled = (panel_mode == DP_PANEL_MODE_EDP) ? 1 : 0;
	config.mst_enabled = (pipe_ctx->stream->signal ==
			SIGNAL_TYPE_DISPLAY_PORT_MST) ? 1 : 0;
	config.dp2_enabled = dp_is_128b_132b_signal(pipe_ctx) ? 1 : 0;
	config.usb4_enabled = (pipe_ctx->stream->link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA) ?
			1 : 0;
	config.dpms_off = dpms_off;

	/* dm stream context */
	config.dm_stream_ctx = pipe_ctx->stream->dm_stream_context;

	cp_psp->funcs.update_stream_config(cp_psp->handle, &config);
}

static void set_avmute(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct dc  *dc = pipe_ctx->stream->ctx->dc;

	if (!dc_is_hdmi_signal(pipe_ctx->stream->signal))
		return;

	dc->hwss.set_avmute(pipe_ctx, enable);
}

static void enable_mst_on_sink(struct dc_link *link, bool enable)
{
	unsigned char mstmCntl;

	core_link_read_dpcd(link, DP_MSTM_CTRL, &mstmCntl, 1);
	if (enable)
		mstmCntl |= DP_MST_EN;
	else
		mstmCntl &= (~DP_MST_EN);

	core_link_write_dpcd(link, DP_MSTM_CTRL, &mstmCntl, 1);
}

static void dsc_optc_config_log(struct display_stream_compressor *dsc,
		struct dsc_optc_config *config)
{
	uint32_t precision = 1 << 28;
	uint32_t bytes_per_pixel_int = config->bytes_per_pixel / precision;
	uint32_t bytes_per_pixel_mod = config->bytes_per_pixel % precision;
	uint64_t ll_bytes_per_pix_fraq = bytes_per_pixel_mod;
	DC_LOGGER_INIT(dsc->ctx->logger);

	/* 7 fractional digits decimal precision for bytes per pixel is enough because DSC
	 * bits per pixel precision is 1/16th of a pixel, which means bytes per pixel precision is
	 * 1/16/8 = 1/128 of a byte, or 0.0078125 decimal
	 */
	ll_bytes_per_pix_fraq *= 10000000;
	ll_bytes_per_pix_fraq /= precision;

	DC_LOG_DSC("\tbytes_per_pixel 0x%08x (%d.%07d)",
			config->bytes_per_pixel, bytes_per_pixel_int, (uint32_t)ll_bytes_per_pix_fraq);
	DC_LOG_DSC("\tis_pixel_format_444 %d", config->is_pixel_format_444);
	DC_LOG_DSC("\tslice_width %d", config->slice_width);
}

static bool dp_set_dsc_on_rx(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	bool result = false;

	if (dc_is_virtual_signal(stream->signal))
		result = true;
	else
		result = dm_helpers_dp_write_dsc_enable(dc->ctx, stream, enable);
	return result;
}

/* The stream with these settings can be sent (unblanked) only after DSC was enabled on RX first,
 * i.e. after dp_enable_dsc_on_rx() had been called
 */
void link_set_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable)
{
	/* TODO: Move this to HWSS as this is hardware programming sequence not a
	 * link layer sequence
	 */
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
	DC_LOGGER_INIT(dsc->ctx->logger);

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
		if (should_use_dto_dscclk)
			dccg->funcs->set_dto_dscclk(dccg, dsc->inst);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			struct display_stream_compressor *odm_dsc = odm_pipe->stream_res.dsc;

			odm_dsc->funcs->dsc_set_config(odm_dsc, &dsc_cfg, &dsc_optc_cfg);
			odm_dsc->funcs->dsc_enable(odm_dsc, odm_pipe->stream_res.opp->inst);
			if (should_use_dto_dscclk)
				dccg->funcs->set_dto_dscclk(dccg, odm_dsc->inst);
		}
		dsc_cfg.dc_dsc_cfg.num_slices_h *= opp_cnt;
		dsc_cfg.pic_width *= opp_cnt;

		optc_dsc_mode = dsc_optc_cfg.is_pixel_format_444 ? OPTC_DSC_ENABLED_444 : OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED;

		/* Enable DSC in encoder */
		if (dc_is_dp_signal(stream->signal) && !dp_is_128b_132b_signal(pipe_ctx)) {
			DC_LOG_DSC("Setting stream encoder DSC config for engine %d:", (int)pipe_ctx->stream_res.stream_enc->id);
			dsc_optc_config_log(dsc, &dsc_optc_cfg);
			pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_config(pipe_ctx->stream_res.stream_enc,
									optc_dsc_mode,
									dsc_optc_cfg.bytes_per_pixel,
									dsc_optc_cfg.slice_width);

			/* PPS SDP is set elsewhere because it has to be done after DIG FE is connected to DIG BE */
		}

		/* Enable DSC in OPTC */
		DC_LOG_DSC("Setting optc DSC config for tg instance %d:", pipe_ctx->stream_res.tg->inst);
		dsc_optc_config_log(dsc, &dsc_optc_cfg);
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(pipe_ctx->stream_res.tg,
							optc_dsc_mode,
							dsc_optc_cfg.bytes_per_pixel,
							dsc_optc_cfg.slice_width);
	} else {
		/* disable DSC in OPTC */
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(
				pipe_ctx->stream_res.tg,
				OPTC_DSC_DISABLED, 0, 0);

		/* disable DSC in stream encoder */
		if (dc_is_dp_signal(stream->signal)) {
			if (dp_is_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										false,
										NULL,
										true);
			else {
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_config(
						pipe_ctx->stream_res.stream_enc,
						OPTC_DSC_DISABLED, 0, 0);
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
							pipe_ctx->stream_res.stream_enc, false, NULL, true);
			}
		}

		/* disable DSC block */
		if (dccg->funcs->set_ref_dscclk)
			dccg->funcs->set_ref_dscclk(dccg, pipe_ctx->stream_res.dsc->inst);
		pipe_ctx->stream_res.dsc->funcs->dsc_disable(pipe_ctx->stream_res.dsc);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			if (dccg->funcs->set_ref_dscclk)
				dccg->funcs->set_ref_dscclk(dccg, odm_pipe->stream_res.dsc->inst);
			odm_pipe->stream_res.dsc->funcs->dsc_disable(odm_pipe->stream_res.dsc);
		}
	}
}

/*
 * For dynamic bpp change case, dsc is programmed with MASTER_UPDATE_LOCK enabled;
 * hence PPS info packet update need to use frame update instead of immediate update.
 * Added parameter immediate_update for this purpose.
 * The decision to use frame update is hard-coded in function dp_update_dsc_config(),
 * which is the only place where a "false" would be passed in for param immediate_update.
 *
 * immediate_update is only applicable when DSC is enabled.
 */
bool link_set_dsc_pps_packet(struct pipe_ctx *pipe_ctx, bool enable, bool immediate_update)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc_stream_state *stream = pipe_ctx->stream;

	if (!pipe_ctx->stream->timing.flags.DSC)
		return false;

	if (!dsc)
		return false;

	DC_LOGGER_INIT(dsc->ctx->logger);

	if (enable) {
		struct dsc_config dsc_cfg;
		uint8_t dsc_packed_pps[128];

		memset(&dsc_cfg, 0, sizeof(dsc_cfg));
		memset(dsc_packed_pps, 0, 128);

		/* Enable DSC hw block */
		dsc_cfg.pic_width = stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;

		dsc->funcs->dsc_get_packed_pps(dsc, &dsc_cfg, &dsc_packed_pps[0]);
		memcpy(&stream->dsc_packed_pps[0], &dsc_packed_pps[0], sizeof(stream->dsc_packed_pps));
		if (dc_is_dp_signal(stream->signal)) {
			DC_LOG_DSC("Setting stream encoder DSC PPS SDP for engine %d\n", (int)pipe_ctx->stream_res.stream_enc->id);
			if (dp_is_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										true,
										&dsc_packed_pps[0],
										immediate_update);
			else
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
						pipe_ctx->stream_res.stream_enc,
						true,
						&dsc_packed_pps[0],
						immediate_update);
		}
	} else {
		/* disable DSC PPS in stream encoder */
		memset(&stream->dsc_packed_pps[0], 0, sizeof(stream->dsc_packed_pps));
		if (dc_is_dp_signal(stream->signal)) {
			if (dp_is_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_set_dsc_pps_info_packet(
										pipe_ctx->stream_res.hpo_dp_stream_enc,
										false,
										NULL,
										true);
			else
				pipe_ctx->stream_res.stream_enc->funcs->dp_set_dsc_pps_info_packet(
						pipe_ctx->stream_res.stream_enc, false, NULL, true);
		}
	}

	return true;
}

bool link_set_dsc_enable(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	bool result = false;

	if (!pipe_ctx->stream->timing.flags.DSC)
		goto out;
	if (!dsc)
		goto out;

	if (enable) {
		{
			link_set_dsc_on_stream(pipe_ctx, true);
			result = true;
		}
	} else {
		dp_set_dsc_on_rx(pipe_ctx, false);
		link_set_dsc_on_stream(pipe_ctx, false);
		result = true;
	}
out:
	return result;
}

bool link_update_dsc_config(struct pipe_ctx *pipe_ctx)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;

	if (!pipe_ctx->stream->timing.flags.DSC)
		return false;
	if (!dsc)
		return false;

	link_set_dsc_on_stream(pipe_ctx, true);
	link_set_dsc_pps_packet(pipe_ctx, true, false);
	return true;
}

static void enable_stream_features(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;

	if (pipe_ctx->stream->signal != SIGNAL_TYPE_DISPLAY_PORT_MST) {
		struct dc_link *link = stream->link;
		union down_spread_ctrl old_downspread;
		union down_spread_ctrl new_downspread;

		memset(&old_downspread, 0, sizeof(old_downspread));

		core_link_read_dpcd(link, DP_DOWNSPREAD_CTRL,
				&old_downspread.raw, sizeof(old_downspread));

		new_downspread.raw = old_downspread.raw;

		new_downspread.bits.IGNORE_MSA_TIMING_PARAM =
				(stream->ignore_msa_timing_param) ? 1 : 0;

		if (new_downspread.raw != old_downspread.raw) {
			core_link_write_dpcd(link, DP_DOWNSPREAD_CTRL,
				&new_downspread.raw, sizeof(new_downspread));
		}

	} else {
		dm_helpers_mst_enable_stream_features(stream);
	}
}

static void log_vcp_x_y(const struct dc_link *link, struct fixed31_32 avg_time_slots_per_mtp)
{
	const uint32_t VCP_Y_PRECISION = 1000;
	uint64_t vcp_x, vcp_y;
	DC_LOGGER_INIT(link->ctx->logger);

	// Add 0.5*(1/VCP_Y_PRECISION) to round up to decimal precision
	avg_time_slots_per_mtp = dc_fixpt_add(
			avg_time_slots_per_mtp,
			dc_fixpt_from_fraction(
				1,
				2*VCP_Y_PRECISION));

	vcp_x = dc_fixpt_floor(
			avg_time_slots_per_mtp);
	vcp_y = dc_fixpt_floor(
			dc_fixpt_mul_int(
				dc_fixpt_sub_int(
					avg_time_slots_per_mtp,
					dc_fixpt_floor(
							avg_time_slots_per_mtp)),
				VCP_Y_PRECISION));


	if (link->type == dc_connection_mst_branch)
		DC_LOG_DP2("MST Update Payload: set_throttled_vcp_size slot X.Y for MST stream "
				"X: %llu "
				"Y: %llu/%d",
				vcp_x,
				vcp_y,
				VCP_Y_PRECISION);
	else
		DC_LOG_DP2("SST Update Payload: set_throttled_vcp_size slot X.Y for SST stream "
				"X: %llu "
				"Y: %llu/%d",
				vcp_x,
				vcp_y,
				VCP_Y_PRECISION);
}

static struct fixed31_32 get_pbn_per_slot(struct dc_stream_state *stream)
{
	struct fixed31_32 mbytes_per_sec;
	uint32_t link_rate_in_mbytes_per_sec = dp_link_bandwidth_kbps(stream->link,
			&stream->link->cur_link_settings);
	link_rate_in_mbytes_per_sec /= 8000; /* Kbits to MBytes */

	mbytes_per_sec = dc_fixpt_from_int(link_rate_in_mbytes_per_sec);

	return dc_fixpt_div_int(mbytes_per_sec, 54);
}

static struct fixed31_32 get_pbn_from_bw_in_kbps(uint64_t kbps)
{
	struct fixed31_32 peak_kbps;
	uint32_t numerator = 0;
	uint32_t denominator = 1;

	/*
	 * The 1.006 factor (margin 5300ppm + 300ppm ~ 0.6% as per spec) is not
	 * required when determining PBN/time slot utilization on the link between
	 * us and the branch, since that overhead is already accounted for in
	 * the get_pbn_per_slot function.
	 *
	 * The unit of 54/64Mbytes/sec is an arbitrary unit chosen based on
	 * common multiplier to render an integer PBN for all link rate/lane
	 * counts combinations
	 * calculate
	 * peak_kbps *= (64/54)
	 * peak_kbps /= (8 * 1000) convert to bytes
	 */

	numerator = 64;
	denominator = 54 * 8 * 1000;
	kbps *= numerator;
	peak_kbps = dc_fixpt_from_fraction(kbps, denominator);

	return peak_kbps;
}

static struct fixed31_32 get_pbn_from_timing(struct pipe_ctx *pipe_ctx)
{
	uint64_t kbps;
	enum dc_link_encoding_format link_encoding;

	if (dp_is_128b_132b_signal(pipe_ctx))
		link_encoding = DC_LINK_ENCODING_DP_128b_132b;
	else
		link_encoding = DC_LINK_ENCODING_DP_8b_10b;

	kbps = dc_bandwidth_in_kbps_from_timing(&pipe_ctx->stream->timing, link_encoding);
	return get_pbn_from_bw_in_kbps(kbps);
}


// TODO - DP2.0 Link: Fix get_lane_status to handle LTTPR offset (SST and MST)
static void get_lane_status(
	struct dc_link *link,
	uint32_t lane_count,
	union lane_status *status,
	union lane_align_status_updated *status_updated)
{
	unsigned int lane;
	uint8_t dpcd_buf[3] = {0};

	if (status == NULL || status_updated == NULL) {
		return;
	}

	core_link_read_dpcd(
			link,
			DP_LANE0_1_STATUS,
			dpcd_buf,
			sizeof(dpcd_buf));

	for (lane = 0; lane < lane_count; lane++) {
		status[lane].raw = dp_get_nibble_at_index(&dpcd_buf[0], lane);
	}

	status_updated->raw = dpcd_buf[2];
}

static bool poll_for_allocation_change_trigger(struct dc_link *link)
{
	/*
	 * wait for ACT handled
	 */
	int i;
	const int act_retries = 30;
	enum act_return_status result = ACT_FAILED;
	union payload_table_update_status update_status = {0};
	union lane_status dpcd_lane_status[LANE_COUNT_DP_MAX];
	union lane_align_status_updated lane_status_updated;
	DC_LOGGER_INIT(link->ctx->logger);

	if (link->aux_access_disabled)
		return true;
	for (i = 0; i < act_retries; i++) {
		get_lane_status(link, link->cur_link_settings.lane_count, dpcd_lane_status, &lane_status_updated);

		if (!dp_is_cr_done(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_ch_eq_done(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_symbol_locked(link->cur_link_settings.lane_count, dpcd_lane_status) ||
				!dp_is_interlane_aligned(lane_status_updated)) {
			DC_LOG_ERROR("SST Update Payload: Link loss occurred while "
					"polling for ACT handled.");
			result = ACT_LINK_LOST;
			break;
		}
		core_link_read_dpcd(
				link,
				DP_PAYLOAD_TABLE_UPDATE_STATUS,
				&update_status.raw,
				1);

		if (update_status.bits.ACT_HANDLED == 1) {
			DC_LOG_DP2("SST Update Payload: ACT handled by downstream.");
			result = ACT_SUCCESS;
			break;
		}

		fsleep(5000);
	}

	if (result == ACT_FAILED) {
		DC_LOG_ERROR("SST Update Payload: ACT still not handled after retries, "
				"continue on. Something is wrong with the branch.");
	}

	return (result == ACT_SUCCESS);
}

static void update_mst_stream_alloc_table(
	struct dc_link *link,
	struct stream_encoder *stream_enc,
	struct hpo_dp_stream_encoder *hpo_dp_stream_enc, // TODO: Rename stream_enc to dio_stream_enc?
	const struct dc_dp_mst_stream_allocation_table *proposed_table)
{
	struct link_mst_stream_allocation work_table[MAX_CONTROLLER_NUM] = { 0 };
	struct link_mst_stream_allocation *dc_alloc;

	int i;
	int j;

	/* if DRM proposed_table has more than one new payload */
	ASSERT(proposed_table->stream_count -
			link->mst_stream_alloc_table.stream_count < 2);

	/* copy proposed_table to link, add stream encoder */
	for (i = 0; i < proposed_table->stream_count; i++) {

		for (j = 0; j < link->mst_stream_alloc_table.stream_count; j++) {
			dc_alloc =
			&link->mst_stream_alloc_table.stream_allocations[j];

			if (dc_alloc->vcp_id ==
				proposed_table->stream_allocations[i].vcp_id) {

				work_table[i] = *dc_alloc;
				work_table[i].slot_count = proposed_table->stream_allocations[i].slot_count;
				break; /* exit j loop */
			}
		}

		/* new vcp_id */
		if (j == link->mst_stream_alloc_table.stream_count) {
			work_table[i].vcp_id =
				proposed_table->stream_allocations[i].vcp_id;
			work_table[i].slot_count =
				proposed_table->stream_allocations[i].slot_count;
			work_table[i].stream_enc = stream_enc;
			work_table[i].hpo_dp_stream_enc = hpo_dp_stream_enc;
		}
	}

	/* update link->mst_stream_alloc_table with work_table */
	link->mst_stream_alloc_table.stream_count =
			proposed_table->stream_count;
	for (i = 0; i < MAX_CONTROLLER_NUM; i++)
		link->mst_stream_alloc_table.stream_allocations[i] =
				work_table[i];
}

static void remove_stream_from_alloc_table(
		struct dc_link *link,
		struct stream_encoder *dio_stream_enc,
		struct hpo_dp_stream_encoder *hpo_dp_stream_enc)
{
	int i = 0;
	struct link_mst_stream_allocation_table *table =
			&link->mst_stream_alloc_table;

	if (hpo_dp_stream_enc) {
		for (; i < table->stream_count; i++)
			if (hpo_dp_stream_enc == table->stream_allocations[i].hpo_dp_stream_enc)
				break;
	} else {
		for (; i < table->stream_count; i++)
			if (dio_stream_enc == table->stream_allocations[i].stream_enc)
				break;
	}

	if (i < table->stream_count) {
		i++;
		for (; i < table->stream_count; i++)
			table->stream_allocations[i-1] = table->stream_allocations[i];
		memset(&table->stream_allocations[table->stream_count-1], 0,
				sizeof(struct link_mst_stream_allocation));
		table->stream_count--;
	}
}

static enum dc_status deallocate_mst_payload(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dc_dp_mst_stream_allocation_table proposed_table = {0};
	struct fixed31_32 avg_time_slots_per_mtp = dc_fixpt_from_int(0);
	int i;
	bool mst_mode = (link->type == dc_connection_mst_branch);
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	const struct dc_link_settings empty_link_settings = {0};
	DC_LOGGER_INIT(link->ctx->logger);

	/* deallocate_mst_payload is called before disable link. When mode or
	 * disable/enable monitor, new stream is created which is not in link
	 * stream[] yet. For this, payload is not allocated yet, so de-alloc
	 * should not done. For new mode set, map_resources will get engine
	 * for new stream, so stream_enc->id should be validated until here.
	 */

	/* slot X.Y */
	if (link_hwss->ext.set_throttled_vcp_size)
		link_hwss->ext.set_throttled_vcp_size(pipe_ctx, avg_time_slots_per_mtp);
	if (link_hwss->ext.set_hblank_min_symbol_width)
		link_hwss->ext.set_hblank_min_symbol_width(pipe_ctx,
				&empty_link_settings,
				avg_time_slots_per_mtp);

	if (mst_mode) {
		/* when link is in mst mode, reply on mst manager to remove
		 * payload
		 */
		if (dm_helpers_dp_mst_write_payload_allocation_table(
				stream->ctx,
				stream,
				&proposed_table,
				false))
			update_mst_stream_alloc_table(
					link,
					pipe_ctx->stream_res.stream_enc,
					pipe_ctx->stream_res.hpo_dp_stream_enc,
					&proposed_table);
		else
			DC_LOG_WARNING("Failed to update"
					"MST allocation table for"
					"pipe idx:%d\n",
					pipe_ctx->pipe_idx);
	} else {
		/* when link is no longer in mst mode (mst hub unplugged),
		 * remove payload with default dc logic
		 */
		remove_stream_from_alloc_table(link, pipe_ctx->stream_res.stream_enc,
				pipe_ctx->stream_res.hpo_dp_stream_enc);
	}

	DC_LOG_MST("%s"
			"stream_count: %d: ",
			__func__,
			link->mst_stream_alloc_table.stream_count);

	for (i = 0; i < MAX_CONTROLLER_NUM; i++) {
		DC_LOG_MST("stream_enc[%d]: %p      "
		"stream[%d].hpo_dp_stream_enc: %p      "
		"stream[%d].vcp_id: %d      "
		"stream[%d].slot_count: %d\n",
		i,
		(void *) link->mst_stream_alloc_table.stream_allocations[i].stream_enc,
		i,
		(void *) link->mst_stream_alloc_table.stream_allocations[i].hpo_dp_stream_enc,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].vcp_id,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].slot_count);
	}

	/* update mst stream allocation table hardware state */
	if (link_hwss->ext.update_stream_allocation_table == NULL ||
			link_dp_get_encoding_format(&link->cur_link_settings) == DP_UNKNOWN_ENCODING) {
		DC_LOG_DEBUG("Unknown encoding format\n");
		return DC_ERROR_UNEXPECTED;
	}

	link_hwss->ext.update_stream_allocation_table(link, &pipe_ctx->link_res,
			&link->mst_stream_alloc_table);

	if (mst_mode)
		dm_helpers_dp_mst_poll_for_allocation_change_trigger(
			stream->ctx,
			stream);

	dm_helpers_dp_mst_update_mst_mgr_for_deallocation(
			stream->ctx,
			stream);

	return DC_OK;
}

/* convert link_mst_stream_alloc_table to dm dp_mst_stream_alloc_table
 * because stream_encoder is not exposed to dm
 */
static enum dc_status allocate_mst_payload(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dc_dp_mst_stream_allocation_table proposed_table = {0};
	struct fixed31_32 avg_time_slots_per_mtp;
	struct fixed31_32 pbn;
	struct fixed31_32 pbn_per_slot;
	int i;
	enum act_return_status ret;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	DC_LOGGER_INIT(link->ctx->logger);

	/* enable_link_dp_mst already check link->enabled_stream_count
	 * and stream is in link->stream[]. This is called during set mode,
	 * stream_enc is available.
	 */

	/* get calculate VC payload for stream: stream_alloc */
	if (dm_helpers_dp_mst_write_payload_allocation_table(
		stream->ctx,
		stream,
		&proposed_table,
		true))
		update_mst_stream_alloc_table(
					link,
					pipe_ctx->stream_res.stream_enc,
					pipe_ctx->stream_res.hpo_dp_stream_enc,
					&proposed_table);
	else
		DC_LOG_WARNING("Failed to update"
				"MST allocation table for"
				"pipe idx:%d\n",
				pipe_ctx->pipe_idx);

	DC_LOG_MST("%s  "
			"stream_count: %d: \n ",
			__func__,
			link->mst_stream_alloc_table.stream_count);

	for (i = 0; i < MAX_CONTROLLER_NUM; i++) {
		DC_LOG_MST("stream_enc[%d]: %p      "
		"stream[%d].hpo_dp_stream_enc: %p      "
		"stream[%d].vcp_id: %d      "
		"stream[%d].slot_count: %d\n",
		i,
		(void *) link->mst_stream_alloc_table.stream_allocations[i].stream_enc,
		i,
		(void *) link->mst_stream_alloc_table.stream_allocations[i].hpo_dp_stream_enc,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].vcp_id,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].slot_count);
	}

	ASSERT(proposed_table.stream_count > 0);

	/* program DP source TX for payload */
	if (link_hwss->ext.update_stream_allocation_table == NULL ||
			link_dp_get_encoding_format(&link->cur_link_settings) == DP_UNKNOWN_ENCODING) {
		DC_LOG_ERROR("Failure: unknown encoding format\n");
		return DC_ERROR_UNEXPECTED;
	}

	link_hwss->ext.update_stream_allocation_table(link,
			&pipe_ctx->link_res,
			&link->mst_stream_alloc_table);

	/* send down message */
	ret = dm_helpers_dp_mst_poll_for_allocation_change_trigger(
			stream->ctx,
			stream);

	if (ret != ACT_LINK_LOST)
		dm_helpers_dp_mst_send_payload_allocation(
				stream->ctx,
				stream);

	/* slot X.Y for only current stream */
	pbn_per_slot = get_pbn_per_slot(stream);
	if (pbn_per_slot.value == 0) {
		DC_LOG_ERROR("Failure: pbn_per_slot==0 not allowed. Cannot continue, returning DC_UNSUPPORTED_VALUE.\n");
		return DC_UNSUPPORTED_VALUE;
	}
	pbn = get_pbn_from_timing(pipe_ctx);
	avg_time_slots_per_mtp = dc_fixpt_div(pbn, pbn_per_slot);

	log_vcp_x_y(link, avg_time_slots_per_mtp);

	if (link_hwss->ext.set_throttled_vcp_size)
		link_hwss->ext.set_throttled_vcp_size(pipe_ctx, avg_time_slots_per_mtp);
	if (link_hwss->ext.set_hblank_min_symbol_width)
		link_hwss->ext.set_hblank_min_symbol_width(pipe_ctx,
				&link->cur_link_settings,
				avg_time_slots_per_mtp);

	return DC_OK;
}

struct fixed31_32 link_calculate_sst_avg_time_slots_per_mtp(
		const struct dc_stream_state *stream,
		const struct dc_link *link)
{
	struct fixed31_32 link_bw_effective =
			dc_fixpt_from_int(
					dp_link_bandwidth_kbps(link, &link->cur_link_settings));
	struct fixed31_32 timeslot_bw_effective =
			dc_fixpt_div_int(link_bw_effective, MAX_MTP_SLOT_COUNT);
	struct fixed31_32 timing_bw =
			dc_fixpt_from_int(
					dc_bandwidth_in_kbps_from_timing(&stream->timing,
							dc_link_get_highest_encoding_format(link)));
	struct fixed31_32 avg_time_slots_per_mtp =
			dc_fixpt_div(timing_bw, timeslot_bw_effective);

	return avg_time_slots_per_mtp;
}


static bool write_128b_132b_sst_payload_allocation_table(
		const struct dc_stream_state *stream,
		struct dc_link *link,
		struct link_mst_stream_allocation_table *proposed_table,
		bool allocate)
{
	const uint8_t vc_id = 1; /// VC ID always 1 for SST
	const uint8_t start_time_slot = 0; /// Always start at time slot 0 for SST
	bool result = false;
	uint8_t req_slot_count = 0;
	struct fixed31_32 avg_time_slots_per_mtp = { 0 };
	union payload_table_update_status update_status = { 0 };
	const uint32_t max_retries = 30;
	uint32_t retries = 0;
	DC_LOGGER_INIT(link->ctx->logger);

	if (allocate)	{
		avg_time_slots_per_mtp = link_calculate_sst_avg_time_slots_per_mtp(stream, link);
		req_slot_count = dc_fixpt_ceil(avg_time_slots_per_mtp);
		/// Validation should filter out modes that exceed link BW
		ASSERT(req_slot_count <= MAX_MTP_SLOT_COUNT);
		if (req_slot_count > MAX_MTP_SLOT_COUNT)
			return false;
	} else {
		/// Leave req_slot_count = 0 if allocate is false.
	}

	proposed_table->stream_count = 1; /// Always 1 stream for SST
	proposed_table->stream_allocations[0].slot_count = req_slot_count;
	proposed_table->stream_allocations[0].vcp_id = vc_id;

	if (link->aux_access_disabled)
		return true;

	/// Write DPCD 2C0 = 1 to start updating
	update_status.bits.VC_PAYLOAD_TABLE_UPDATED = 1;
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_TABLE_UPDATE_STATUS,
			&update_status.raw,
			1);

	/// Program the changes in DPCD 1C0 - 1C2
	ASSERT(vc_id == 1);
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_SET,
			&vc_id,
			1);

	ASSERT(start_time_slot == 0);
	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_START_TIME_SLOT,
			&start_time_slot,
			1);

	core_link_write_dpcd(
			link,
			DP_PAYLOAD_ALLOCATE_TIME_SLOT_COUNT,
			&req_slot_count,
			1);

	/// Poll till DPCD 2C0 read 1
	/// Try for at least 150ms (30 retries, with 5ms delay after each attempt)

	while (retries < max_retries) {
		if (core_link_read_dpcd(
				link,
				DP_PAYLOAD_TABLE_UPDATE_STATUS,
				&update_status.raw,
				1) == DC_OK) {
			if (update_status.bits.VC_PAYLOAD_TABLE_UPDATED == 1) {
				DC_LOG_DP2("SST Update Payload: downstream payload table updated.");
				result = true;
				break;
			}
		} else {
			union dpcd_rev dpcdRev;

			if (core_link_read_dpcd(
					link,
					DP_DPCD_REV,
					&dpcdRev.raw,
					1) != DC_OK) {
				DC_LOG_ERROR("SST Update Payload: Unable to read DPCD revision "
						"of sink while polling payload table "
						"updated status bit.");
				break;
			}
		}
		retries++;
		fsleep(5000);
	}

	if (!result && retries == max_retries) {
		DC_LOG_ERROR("SST Update Payload: Payload table not updated after retries, "
				"continue on. Something is wrong with the branch.");
		// TODO - DP2.0 Payload: Read and log the payload table from downstream branch
	}

	return result;
}

/*
 * Payload allocation/deallocation for SST introduced in DP2.0
 */
static enum dc_status update_sst_payload(struct pipe_ctx *pipe_ctx,
						 bool allocate)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct link_mst_stream_allocation_table proposed_table = {0};
	struct fixed31_32 avg_time_slots_per_mtp;
	const struct dc_link_settings empty_link_settings = {0};
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	DC_LOGGER_INIT(link->ctx->logger);

	/* slot X.Y for SST payload deallocate */
	if (!allocate) {
		avg_time_slots_per_mtp = dc_fixpt_from_int(0);

		log_vcp_x_y(link, avg_time_slots_per_mtp);

		if (link_hwss->ext.set_throttled_vcp_size)
			link_hwss->ext.set_throttled_vcp_size(pipe_ctx,
					avg_time_slots_per_mtp);
		if (link_hwss->ext.set_hblank_min_symbol_width)
			link_hwss->ext.set_hblank_min_symbol_width(pipe_ctx,
					&empty_link_settings,
					avg_time_slots_per_mtp);
	}

	/* calculate VC payload and update branch with new payload allocation table*/
	if (!write_128b_132b_sst_payload_allocation_table(
			stream,
			link,
			&proposed_table,
			allocate)) {
		DC_LOG_ERROR("SST Update Payload: Failed to update "
						"allocation table for "
						"pipe idx: %d\n",
						pipe_ctx->pipe_idx);
		return DC_FAIL_DP_PAYLOAD_ALLOCATION;
	}

	proposed_table.stream_allocations[0].hpo_dp_stream_enc = pipe_ctx->stream_res.hpo_dp_stream_enc;

	ASSERT(proposed_table.stream_count == 1);

	//TODO - DP2.0 Logging: Instead of hpo_dp_stream_enc pointer, log instance id
	DC_LOG_DP2("SST Update Payload: hpo_dp_stream_enc: %p      "
		"vcp_id: %d      "
		"slot_count: %d\n",
		(void *) proposed_table.stream_allocations[0].hpo_dp_stream_enc,
		proposed_table.stream_allocations[0].vcp_id,
		proposed_table.stream_allocations[0].slot_count);

	/* program DP source TX for payload */
	link_hwss->ext.update_stream_allocation_table(link, &pipe_ctx->link_res,
			&proposed_table);

	/* poll for ACT handled */
	if (!poll_for_allocation_change_trigger(link)) {
		// Failures will result in blackscreen and errors logged
		BREAK_TO_DEBUGGER();
	}

	/* slot X.Y for SST payload allocate */
	if (allocate && link_dp_get_encoding_format(&link->cur_link_settings) ==
			DP_128b_132b_ENCODING) {
		avg_time_slots_per_mtp = link_calculate_sst_avg_time_slots_per_mtp(stream, link);

		log_vcp_x_y(link, avg_time_slots_per_mtp);

		if (link_hwss->ext.set_throttled_vcp_size)
			link_hwss->ext.set_throttled_vcp_size(pipe_ctx,
					avg_time_slots_per_mtp);
		if (link_hwss->ext.set_hblank_min_symbol_width)
			link_hwss->ext.set_hblank_min_symbol_width(pipe_ctx,
					&link->cur_link_settings,
					avg_time_slots_per_mtp);
	}

	/* Always return DC_OK.
	 * If part of sequence fails, log failure(s) and show blackscreen
	 */
	return DC_OK;
}

enum dc_status link_reduce_mst_payload(struct pipe_ctx *pipe_ctx, uint32_t bw_in_kbps)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct fixed31_32 avg_time_slots_per_mtp;
	struct fixed31_32 pbn;
	struct fixed31_32 pbn_per_slot;
	struct dc_dp_mst_stream_allocation_table proposed_table = {0};
	uint8_t i;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	DC_LOGGER_INIT(link->ctx->logger);

	/* decrease throttled vcp size */
	pbn_per_slot = get_pbn_per_slot(stream);
	pbn = get_pbn_from_bw_in_kbps(bw_in_kbps);
	avg_time_slots_per_mtp = dc_fixpt_div(pbn, pbn_per_slot);

	if (link_hwss->ext.set_throttled_vcp_size)
		link_hwss->ext.set_throttled_vcp_size(pipe_ctx, avg_time_slots_per_mtp);
	if (link_hwss->ext.set_hblank_min_symbol_width)
		link_hwss->ext.set_hblank_min_symbol_width(pipe_ctx,
				&link->cur_link_settings,
				avg_time_slots_per_mtp);

	/* send ALLOCATE_PAYLOAD sideband message with updated pbn */
	dm_helpers_dp_mst_send_payload_allocation(
			stream->ctx,
			stream);

	/* notify immediate branch device table update */
	if (dm_helpers_dp_mst_write_payload_allocation_table(
			stream->ctx,
			stream,
			&proposed_table,
			true)) {
		/* update mst stream allocation table software state */
		update_mst_stream_alloc_table(
				link,
				pipe_ctx->stream_res.stream_enc,
				pipe_ctx->stream_res.hpo_dp_stream_enc,
				&proposed_table);
	} else {
		DC_LOG_WARNING("Failed to update"
				"MST allocation table for"
				"pipe idx:%d\n",
				pipe_ctx->pipe_idx);
	}

	DC_LOG_MST("%s  "
			"stream_count: %d: \n ",
			__func__,
			link->mst_stream_alloc_table.stream_count);

	for (i = 0; i < MAX_CONTROLLER_NUM; i++) {
		DC_LOG_MST("stream_enc[%d]: %p      "
		"stream[%d].hpo_dp_stream_enc: %p      "
		"stream[%d].vcp_id: %d      "
		"stream[%d].slot_count: %d\n",
		i,
		(void *) link->mst_stream_alloc_table.stream_allocations[i].stream_enc,
		i,
		(void *) link->mst_stream_alloc_table.stream_allocations[i].hpo_dp_stream_enc,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].vcp_id,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].slot_count);
	}

	ASSERT(proposed_table.stream_count > 0);

	/* update mst stream allocation table hardware state */
	if (link_hwss->ext.update_stream_allocation_table == NULL ||
			link_dp_get_encoding_format(&link->cur_link_settings) == DP_UNKNOWN_ENCODING) {
		DC_LOG_ERROR("Failure: unknown encoding format\n");
		return DC_ERROR_UNEXPECTED;
	}

	link_hwss->ext.update_stream_allocation_table(link, &pipe_ctx->link_res,
			&link->mst_stream_alloc_table);

	/* poll for immediate branch device ACT handled */
	dm_helpers_dp_mst_poll_for_allocation_change_trigger(
			stream->ctx,
			stream);

	return DC_OK;
}

enum dc_status link_increase_mst_payload(struct pipe_ctx *pipe_ctx, uint32_t bw_in_kbps)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct fixed31_32 avg_time_slots_per_mtp;
	struct fixed31_32 pbn;
	struct fixed31_32 pbn_per_slot;
	struct dc_dp_mst_stream_allocation_table proposed_table = {0};
	uint8_t i;
	enum act_return_status ret;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	DC_LOGGER_INIT(link->ctx->logger);

	/* notify immediate branch device table update */
	if (dm_helpers_dp_mst_write_payload_allocation_table(
				stream->ctx,
				stream,
				&proposed_table,
				true)) {
		/* update mst stream allocation table software state */
		update_mst_stream_alloc_table(
				link,
				pipe_ctx->stream_res.stream_enc,
				pipe_ctx->stream_res.hpo_dp_stream_enc,
				&proposed_table);
	}

	DC_LOG_MST("%s  "
			"stream_count: %d: \n ",
			__func__,
			link->mst_stream_alloc_table.stream_count);

	for (i = 0; i < MAX_CONTROLLER_NUM; i++) {
		DC_LOG_MST("stream_enc[%d]: %p      "
		"stream[%d].hpo_dp_stream_enc: %p      "
		"stream[%d].vcp_id: %d      "
		"stream[%d].slot_count: %d\n",
		i,
		(void *) link->mst_stream_alloc_table.stream_allocations[i].stream_enc,
		i,
		(void *) link->mst_stream_alloc_table.stream_allocations[i].hpo_dp_stream_enc,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].vcp_id,
		i,
		link->mst_stream_alloc_table.stream_allocations[i].slot_count);
	}

	ASSERT(proposed_table.stream_count > 0);

	/* update mst stream allocation table hardware state */
	if (link_hwss->ext.update_stream_allocation_table == NULL ||
			link_dp_get_encoding_format(&link->cur_link_settings) == DP_UNKNOWN_ENCODING) {
		DC_LOG_ERROR("Failure: unknown encoding format\n");
		return DC_ERROR_UNEXPECTED;
	}

	link_hwss->ext.update_stream_allocation_table(link, &pipe_ctx->link_res,
			&link->mst_stream_alloc_table);

	/* poll for immediate branch device ACT handled */
	ret = dm_helpers_dp_mst_poll_for_allocation_change_trigger(
			stream->ctx,
			stream);

	if (ret != ACT_LINK_LOST) {
		/* send ALLOCATE_PAYLOAD sideband message with updated pbn */
		dm_helpers_dp_mst_send_payload_allocation(
				stream->ctx,
				stream);
	}

	/* increase throttled vcp size */
	pbn = get_pbn_from_bw_in_kbps(bw_in_kbps);
	pbn_per_slot = get_pbn_per_slot(stream);
	avg_time_slots_per_mtp = dc_fixpt_div(pbn, pbn_per_slot);

	if (link_hwss->ext.set_throttled_vcp_size)
		link_hwss->ext.set_throttled_vcp_size(pipe_ctx, avg_time_slots_per_mtp);
	if (link_hwss->ext.set_hblank_min_symbol_width)
		link_hwss->ext.set_hblank_min_symbol_width(pipe_ctx,
				&link->cur_link_settings,
				avg_time_slots_per_mtp);

	return DC_OK;
}

static void disable_link_dp(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	struct dc_link_settings link_settings = link->cur_link_settings;

	if (signal == SIGNAL_TYPE_DISPLAY_PORT_MST &&
			link->mst_stream_alloc_table.stream_count > 0)
		/* disable MST link only when last vc payload is deallocated */
		return;

	dp_disable_link_phy(link, link_res, signal);

	if (link->connector_signal == SIGNAL_TYPE_EDP) {
		if (!link->skip_implict_edp_power_control)
			link->dc->hwss.edp_power_control(link, false);
	}

	if (signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		/* set the sink to SST mode after disabling the link */
		enable_mst_on_sink(link, false);

	if (link_dp_get_encoding_format(&link_settings) ==
			DP_8b_10b_ENCODING) {
		dp_set_fec_enable(link, false);
		dp_set_fec_ready(link, link_res, false);
	}
}

static void disable_link(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	if (dc_is_dp_signal(signal)) {
		disable_link_dp(link, link_res, signal);
	} else if (signal != SIGNAL_TYPE_VIRTUAL) {
		link->dc->hwss.disable_link_output(link, link_res, signal);
	}

	if (signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		/* MST disable link only when no stream use the link */
		if (link->mst_stream_alloc_table.stream_count <= 0)
			link->link_status.link_active = false;
	} else {
		link->link_status.link_active = false;
	}
}

static void enable_link_hdmi(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	enum dc_color_depth display_color_depth;
	enum engine_id eng_id;
	struct ext_hdmi_settings settings = {0};
	bool is_over_340mhz = false;
	bool is_vga_mode = (stream->timing.h_addressable == 640)
			&& (stream->timing.v_addressable == 480);
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);

	if (stream->phy_pix_clk == 0)
		stream->phy_pix_clk = stream->timing.pix_clk_100hz / 10;
	if (stream->phy_pix_clk > 340000)
		is_over_340mhz = true;

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal)) {
		unsigned short masked_chip_caps = pipe_ctx->stream->link->chip_caps &
				EXT_DISPLAY_PATH_CAPS__EXT_CHIP_MASK;
		if (masked_chip_caps == EXT_DISPLAY_PATH_CAPS__HDMI20_TISN65DP159RSBT) {
			/* DP159, Retimer settings */
			eng_id = pipe_ctx->stream_res.stream_enc->id;

			if (get_ext_hdmi_settings(pipe_ctx, eng_id, &settings)) {
				write_i2c_retimer_setting(pipe_ctx,
						is_vga_mode, is_over_340mhz, &settings);
			} else {
				write_i2c_default_retimer_setting(pipe_ctx,
						is_vga_mode, is_over_340mhz);
			}
		} else if (masked_chip_caps == EXT_DISPLAY_PATH_CAPS__HDMI20_PI3EQX1204) {
			/* PI3EQX1204, Redriver settings */
			write_i2c_redriver_setting(pipe_ctx, is_over_340mhz);
		}
	}

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal))
		write_scdc_data(
			stream->link->ddc,
			stream->phy_pix_clk,
			stream->timing.flags.LTE_340MCSC_SCRAMBLE);

	memset(&stream->link->cur_link_settings, 0,
			sizeof(struct dc_link_settings));

	display_color_depth = stream->timing.display_color_depth;
	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
		display_color_depth = COLOR_DEPTH_888;

	/* We need to enable stream encoder for TMDS first to apply 1/4 TMDS
	 * character clock in case that beyond 340MHz.
	 */
	if (dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal))
		link_hwss->setup_stream_encoder(pipe_ctx);

	dc->hwss.enable_tmds_link_output(
			link,
			&pipe_ctx->link_res,
			pipe_ctx->stream->signal,
			pipe_ctx->clock_source->id,
			display_color_depth,
			stream->phy_pix_clk);

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal))
		read_scdc_data(link->ddc);
}

static enum dc_status enable_link_dp(struct dc_state *state,
				     struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	enum dc_status status;
	bool skip_video_pattern;
	struct dc_link *link = stream->link;
	const struct dc_link_settings *link_settings =
			&pipe_ctx->link_config.dp_link_settings;
	bool fec_enable;
	int i;
	bool apply_seamless_boot_optimization = false;
	uint32_t bl_oled_enable_delay = 50; // in ms
	uint32_t post_oui_delay = 30; // 30ms
	/* Reduce link bandwidth between failed link training attempts. */
	bool do_fallback = false;
	int lt_attempts = LINK_TRAINING_ATTEMPTS;

	// Increase retry count if attempting DP1.x on FIXED_VS link
	if ((link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN) &&
			link_dp_get_encoding_format(link_settings) == DP_8b_10b_ENCODING)
		lt_attempts = 10;

	// check for seamless boot
	for (i = 0; i < state->stream_count; i++) {
		if (state->streams[i]->apply_seamless_boot_optimization) {
			apply_seamless_boot_optimization = true;
			break;
		}
	}

	/* Train with fallback when enabling DPIA link. Conventional links are
	 * trained with fallback during sink detection.
	 */
	if (link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA)
		do_fallback = true;

	/*
	 * Temporary w/a to get DP2.0 link rates to work with SST.
	 * TODO DP2.0 - Workaround: Remove w/a if and when the issue is resolved.
	 */
	if (link_dp_get_encoding_format(link_settings) == DP_128b_132b_ENCODING &&
			pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT &&
			link->dc->debug.set_mst_en_for_sst) {
		enable_mst_on_sink(link, true);
	}
	if (pipe_ctx->stream->signal == SIGNAL_TYPE_EDP) {
		/*in case it is not on*/
		if (!link->dc->config.edp_no_power_sequencing)
			link->dc->hwss.edp_power_control(link, true);
		link->dc->hwss.edp_wait_for_hpd_ready(link, true);
	}

	if (link_dp_get_encoding_format(link_settings) == DP_128b_132b_ENCODING) {
		/* TODO - DP2.0 HW: calculate 32 symbol clock for HPO encoder */
	} else {
		pipe_ctx->stream_res.pix_clk_params.requested_sym_clk =
				link_settings->link_rate * LINK_RATE_REF_FREQ_IN_KHZ;
		if (state->clk_mgr && !apply_seamless_boot_optimization)
			state->clk_mgr->funcs->update_clocks(state->clk_mgr,
					state, false);
	}

	// during mode switch we do DP_SET_POWER off then on, and OUI is lost
	dpcd_set_source_specific_data(link);
	if (link->dpcd_sink_ext_caps.raw != 0) {
		post_oui_delay += link->panel_config.pps.extra_post_OUI_ms;
		msleep(post_oui_delay);
	}

	// similarly, mode switch can cause loss of cable ID
	dpcd_write_cable_id_to_dprx(link);

	skip_video_pattern = true;

	if (link_settings->link_rate == LINK_RATE_LOW)
		skip_video_pattern = false;

	if (perform_link_training_with_retries(link_settings,
					       skip_video_pattern,
					       lt_attempts,
					       pipe_ctx,
					       pipe_ctx->stream->signal,
					       do_fallback)) {
		status = DC_OK;
	} else {
		status = DC_FAIL_DP_LINK_TRAINING;
	}

	if (link->preferred_training_settings.fec_enable)
		fec_enable = *link->preferred_training_settings.fec_enable;
	else
		fec_enable = true;

	if (link_dp_get_encoding_format(link_settings) == DP_8b_10b_ENCODING)
		dp_set_fec_enable(link, fec_enable);

	// during mode set we do DP_SET_POWER off then on, aux writes are lost
	if (link->dpcd_sink_ext_caps.bits.oled == 1 ||
		link->dpcd_sink_ext_caps.bits.sdr_aux_backlight_control == 1 ||
		link->dpcd_sink_ext_caps.bits.hdr_aux_backlight_control == 1) {
		set_default_brightness_aux(link);
		if (link->dpcd_sink_ext_caps.bits.oled == 1)
			msleep(bl_oled_enable_delay);
		edp_backlight_enable_aux(link, true);
	}

	return status;
}

static enum dc_status enable_link_edp(
		struct dc_state *state,
		struct pipe_ctx *pipe_ctx)
{
	return enable_link_dp(state, pipe_ctx);
}

static void enable_link_lvds(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dc *dc = stream->ctx->dc;

	if (stream->phy_pix_clk == 0)
		stream->phy_pix_clk = stream->timing.pix_clk_100hz / 10;

	memset(&stream->link->cur_link_settings, 0,
			sizeof(struct dc_link_settings));
	dc->hwss.enable_lvds_link_output(
			link,
			&pipe_ctx->link_res,
			pipe_ctx->clock_source->id,
			stream->phy_pix_clk);

}

static enum dc_status enable_link_dp_mst(
		struct dc_state *state,
		struct pipe_ctx *pipe_ctx)
{
	struct dc_link *link = pipe_ctx->stream->link;
	unsigned char mstm_cntl;

	/* sink signal type after MST branch is MST. Multiple MST sinks
	 * share one link. Link DP PHY is enable or training only once.
	 */
	if (link->link_status.link_active)
		return DC_OK;

	/* clear payload table */
	core_link_read_dpcd(link, DP_MSTM_CTRL, &mstm_cntl, 1);
	if (mstm_cntl & DP_MST_EN)
		dm_helpers_dp_mst_clear_payload_allocation_table(link->ctx, link);

	/* to make sure the pending down rep can be processed
	 * before enabling the link
	 */
	dm_helpers_dp_mst_poll_pending_down_reply(link->ctx, link);

	/* set the sink to MST mode before enabling the link */
	enable_mst_on_sink(link, true);

	return enable_link_dp(state, pipe_ctx);
}

static enum dc_status enable_link(
		struct dc_state *state,
		struct pipe_ctx *pipe_ctx)
{
	enum dc_status status = DC_ERROR_UNEXPECTED;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;

	/* There's some scenarios where driver is unloaded with display
	 * still enabled. When driver is reloaded, it may cause a display
	 * to not light up if there is a mismatch between old and new
	 * link settings. Need to call disable first before enabling at
	 * new link settings.
	 */
	if (link->link_status.link_active)
		disable_link(link, &pipe_ctx->link_res, pipe_ctx->stream->signal);

	switch (pipe_ctx->stream->signal) {
	case SIGNAL_TYPE_DISPLAY_PORT:
		status = enable_link_dp(state, pipe_ctx);
		break;
	case SIGNAL_TYPE_EDP:
		status = enable_link_edp(state, pipe_ctx);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		status = enable_link_dp_mst(state, pipe_ctx);
		msleep(200);
		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		enable_link_hdmi(pipe_ctx);
		status = DC_OK;
		break;
	case SIGNAL_TYPE_LVDS:
		enable_link_lvds(pipe_ctx);
		status = DC_OK;
		break;
	case SIGNAL_TYPE_VIRTUAL:
		status = DC_OK;
		break;
	default:
		break;
	}

	if (status == DC_OK) {
		pipe_ctx->stream->link->link_status.link_active = true;
	}

	return status;
}

static bool allocate_usb4_bandwidth_for_stream(struct dc_stream_state *stream, int bw)
{
	struct dc_link *link = stream->sink->link;
	int req_bw = bw;

	DC_LOGGER_INIT(link->ctx->logger);

	if (!link->dpia_bw_alloc_config.bw_alloc_enabled)
		return false;

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		int sink_index = 0;
		int i = 0;

		for (i = 0; i < link->sink_count; i++) {
			if (link->remote_sinks[i] == NULL)
				continue;

			if (stream->sink->sink_id != link->remote_sinks[i]->sink_id)
				req_bw += link->dpia_bw_alloc_config.remote_sink_req_bw[i];
			else
				sink_index = i;
		}

		link->dpia_bw_alloc_config.remote_sink_req_bw[sink_index] = bw;
	}

	/* get dp overhead for dp tunneling */
	link->dpia_bw_alloc_config.dp_overhead = link_dp_dpia_get_dp_overhead_in_dp_tunneling(link);
	req_bw += link->dpia_bw_alloc_config.dp_overhead;

	if (link_dp_dpia_allocate_usb4_bandwidth_for_stream(link, req_bw)) {
		if (req_bw <= link->dpia_bw_alloc_config.allocated_bw) {
			DC_LOG_DEBUG("%s, Success in allocate bw for link(%d), allocated_bw(%d), dp_overhead(%d)\n",
					__func__, link->link_index, link->dpia_bw_alloc_config.allocated_bw,
					link->dpia_bw_alloc_config.dp_overhead);
		} else {
			// Cannot get the required bandwidth.
			DC_LOG_ERROR("%s, Failed to allocate bw for link(%d), allocated_bw(%d), dp_overhead(%d)\n",
					__func__, link->link_index, link->dpia_bw_alloc_config.allocated_bw,
					link->dpia_bw_alloc_config.dp_overhead);
			return false;
		}
	} else {
		DC_LOG_DEBUG("%s, usb4 request bw timeout\n", __func__);
		return false;
	}

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		int i = 0;

		for (i = 0; i < link->sink_count; i++) {
			if (link->remote_sinks[i] == NULL)
				continue;
			DC_LOG_DEBUG("%s, remote_sink=%s, request_bw=%d\n", __func__,
					(const char *)(&link->remote_sinks[i]->edid_caps.display_name[0]),
					link->dpia_bw_alloc_config.remote_sink_req_bw[i]);
		}
	}

	return true;
}

static bool allocate_usb4_bandwidth(struct dc_stream_state *stream)
{
	bool ret;

	int bw = dc_bandwidth_in_kbps_from_timing(&stream->timing,
			dc_link_get_highest_encoding_format(stream->sink->link));

	ret = allocate_usb4_bandwidth_for_stream(stream, bw);

	return ret;
}

static bool deallocate_usb4_bandwidth(struct dc_stream_state *stream)
{
	bool ret;

	ret = allocate_usb4_bandwidth_for_stream(stream, 0);

	return ret;
}

void link_set_dpms_off(struct pipe_ctx *pipe_ctx)
{
	struct dc  *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->sink->link;
	struct vpg *vpg = pipe_ctx->stream_res.stream_enc->vpg;

	DC_LOGGER_INIT(pipe_ctx->stream->ctx->logger);

	ASSERT(is_master_pipe_for_link(link, pipe_ctx));

	if (dp_is_128b_132b_signal(pipe_ctx))
		vpg = pipe_ctx->stream_res.hpo_dp_stream_enc->vpg;
	if (dc_is_virtual_signal(pipe_ctx->stream->signal))
		return;

	if (pipe_ctx->stream->sink) {
		if (pipe_ctx->stream->sink->sink_signal != SIGNAL_TYPE_VIRTUAL &&
			pipe_ctx->stream->sink->sink_signal != SIGNAL_TYPE_NONE) {
			DC_LOG_DC("%s pipe_ctx dispname=%s signal=%x\n", __func__,
			pipe_ctx->stream->sink->edid_caps.display_name,
			pipe_ctx->stream->signal);
		}
	}

	if (!pipe_ctx->stream->sink->edid_caps.panel_patch.skip_avmute) {
		if (dc_is_hdmi_signal(pipe_ctx->stream->signal))
			set_avmute(pipe_ctx, true);
	}

	dc->hwss.disable_audio_stream(pipe_ctx);

	update_psp_stream_config(pipe_ctx, true);
	dc->hwss.blank_stream(pipe_ctx);

	if (pipe_ctx->stream->link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA)
		deallocate_usb4_bandwidth(pipe_ctx->stream);

	if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		deallocate_mst_payload(pipe_ctx);
	else if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT &&
			dp_is_128b_132b_signal(pipe_ctx))
		update_sst_payload(pipe_ctx, false);

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal)) {
		struct ext_hdmi_settings settings = {0};
		enum engine_id eng_id = pipe_ctx->stream_res.stream_enc->id;

		unsigned short masked_chip_caps = link->chip_caps &
				EXT_DISPLAY_PATH_CAPS__EXT_CHIP_MASK;
		//Need to inform that sink is going to use legacy HDMI mode.
		write_scdc_data(
			link->ddc,
			165000,//vbios only handles 165Mhz.
			false);
		if (masked_chip_caps == EXT_DISPLAY_PATH_CAPS__HDMI20_TISN65DP159RSBT) {
			/* DP159, Retimer settings */
			if (get_ext_hdmi_settings(pipe_ctx, eng_id, &settings))
				write_i2c_retimer_setting(pipe_ctx,
						false, false, &settings);
			else
				write_i2c_default_retimer_setting(pipe_ctx,
						false, false);
		} else if (masked_chip_caps == EXT_DISPLAY_PATH_CAPS__HDMI20_PI3EQX1204) {
			/* PI3EQX1204, Redriver settings */
			write_i2c_redriver_setting(pipe_ctx, false);
		}
	}

	if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT &&
			!dp_is_128b_132b_signal(pipe_ctx)) {

		/* In DP1.x SST mode, our encoder will go to TPS1
		 * when link is on but stream is off.
		 * Disabling link before stream will avoid exposing TPS1 pattern
		 * during the disable sequence as it will confuse some receivers
		 * state machine.
		 * In DP2 or MST mode, our encoder will stay video active
		 */
		disable_link(pipe_ctx->stream->link, &pipe_ctx->link_res, pipe_ctx->stream->signal);
		dc->hwss.disable_stream(pipe_ctx);
	} else {
		dc->hwss.disable_stream(pipe_ctx);
		disable_link(pipe_ctx->stream->link, &pipe_ctx->link_res, pipe_ctx->stream->signal);
	}

	if (pipe_ctx->stream->timing.flags.DSC) {
		if (dc_is_dp_signal(pipe_ctx->stream->signal))
			link_set_dsc_enable(pipe_ctx, false);
	}
	if (dp_is_128b_132b_signal(pipe_ctx)) {
		if (pipe_ctx->stream_res.tg->funcs->set_out_mux)
			pipe_ctx->stream_res.tg->funcs->set_out_mux(pipe_ctx->stream_res.tg, OUT_MUX_DIO);
	}

	if (vpg && vpg->funcs->vpg_powerdown)
		vpg->funcs->vpg_powerdown(vpg);

	/* for psp not exist case */
	if (link->connector_signal == SIGNAL_TYPE_EDP && dc->debug.psp_disabled_wa) {
		/* reset internal save state to default since eDP is  off */
		enum dp_panel_mode panel_mode = dp_get_panel_mode(pipe_ctx->stream->link);
		/* since current psp not loaded, we need to reset it to default*/
		link->panel_mode = panel_mode;
	}
}

void link_set_dpms_on(
		struct dc_state *state,
		struct pipe_ctx *pipe_ctx)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->sink->link;
	enum dc_status status;
	struct link_encoder *link_enc;
	enum otg_out_mux_dest otg_out_dest = OUT_MUX_DIO;
	struct vpg *vpg = pipe_ctx->stream_res.stream_enc->vpg;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	bool apply_edp_fast_boot_optimization =
		pipe_ctx->stream->apply_edp_fast_boot_optimization;

	DC_LOGGER_INIT(pipe_ctx->stream->ctx->logger);

	ASSERT(is_master_pipe_for_link(link, pipe_ctx));

	if (dp_is_128b_132b_signal(pipe_ctx))
		vpg = pipe_ctx->stream_res.hpo_dp_stream_enc->vpg;
	if (dc_is_virtual_signal(pipe_ctx->stream->signal))
		return;

	if (pipe_ctx->stream->sink) {
		if (pipe_ctx->stream->sink->sink_signal != SIGNAL_TYPE_VIRTUAL &&
			pipe_ctx->stream->sink->sink_signal != SIGNAL_TYPE_NONE) {
			DC_LOG_DC("%s pipe_ctx dispname=%s signal=%x\n", __func__,
			pipe_ctx->stream->sink->edid_caps.display_name,
			pipe_ctx->stream->signal);
		}
	}

	link_enc = link_enc_cfg_get_link_enc(link);
	ASSERT(link_enc);

	if (!dc_is_virtual_signal(pipe_ctx->stream->signal)
			&& !dp_is_128b_132b_signal(pipe_ctx)) {
		struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;

		if (link_enc)
			link_enc->funcs->setup(
				link_enc,
				pipe_ctx->stream->signal);

		if (stream_enc && stream_enc->funcs->dig_stream_enable)
			stream_enc->funcs->dig_stream_enable(
				stream_enc,
				pipe_ctx->stream->signal, 1);
	}

	pipe_ctx->stream->link->link_state_valid = true;

	if (pipe_ctx->stream_res.tg->funcs->set_out_mux) {
		if (dp_is_128b_132b_signal(pipe_ctx))
			otg_out_dest = OUT_MUX_HPO_DP;
		else
			otg_out_dest = OUT_MUX_DIO;
		pipe_ctx->stream_res.tg->funcs->set_out_mux(pipe_ctx->stream_res.tg, otg_out_dest);
	}

	link_hwss->setup_stream_attribute(pipe_ctx);

	pipe_ctx->stream->apply_edp_fast_boot_optimization = false;

	// Enable VPG before building infoframe
	if (vpg && vpg->funcs->vpg_poweron)
		vpg->funcs->vpg_poweron(vpg);

	resource_build_info_frame(pipe_ctx);
	dc->hwss.update_info_frame(pipe_ctx);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_UPDATE_INFO_FRAME);

	/* Do not touch link on seamless boot optimization. */
	if (pipe_ctx->stream->apply_seamless_boot_optimization) {
		pipe_ctx->stream->dpms_off = false;

		/* Still enable stream features & audio on seamless boot for DP external displays */
		if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT) {
			enable_stream_features(pipe_ctx);
			dc->hwss.enable_audio_stream(pipe_ctx);
		}

		update_psp_stream_config(pipe_ctx, false);
		return;
	}

	/* eDP lit up by bios already, no need to enable again. */
	if (pipe_ctx->stream->signal == SIGNAL_TYPE_EDP &&
				apply_edp_fast_boot_optimization &&
				!pipe_ctx->stream->timing.flags.DSC &&
				!pipe_ctx->next_odm_pipe) {
		pipe_ctx->stream->dpms_off = false;
		update_psp_stream_config(pipe_ctx, false);
		return;
	}

	if (pipe_ctx->stream->dpms_off)
		return;

	/* Have to setup DSC before DIG FE and BE are connected (which happens before the
	 * link training). This is to make sure the bandwidth sent to DIG BE won't be
	 * bigger than what the link and/or DIG BE can handle. VBID[6]/CompressedStream_flag
	 * will be automatically set at a later time when the video is enabled
	 * (DP_VID_STREAM_EN = 1).
	 */
	if (pipe_ctx->stream->timing.flags.DSC) {
		if (dc_is_dp_signal(pipe_ctx->stream->signal) ||
		    dc_is_virtual_signal(pipe_ctx->stream->signal))
			link_set_dsc_enable(pipe_ctx, true);
	}

	status = enable_link(state, pipe_ctx);

	if (status != DC_OK) {
		DC_LOG_WARNING("enabling link %u failed: %d\n",
		pipe_ctx->stream->link->link_index,
		status);

		/* Abort stream enable *unless* the failure was due to
		 * DP link training - some DP monitors will recover and
		 * show the stream anyway. But MST displays can't proceed
		 * without link training.
		 */
		if (status != DC_FAIL_DP_LINK_TRAINING ||
				pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
			if (false == stream->link->link_status.link_active)
				disable_link(stream->link, &pipe_ctx->link_res,
						pipe_ctx->stream->signal);
			BREAK_TO_DEBUGGER();
			return;
		}
	}

	/* turn off otg test pattern if enable */
	if (pipe_ctx->stream_res.tg->funcs->set_test_pattern)
		pipe_ctx->stream_res.tg->funcs->set_test_pattern(pipe_ctx->stream_res.tg,
				CONTROLLER_DP_TEST_PATTERN_VIDEOMODE,
				COLOR_DEPTH_UNDEFINED);

	/* This second call is needed to reconfigure the DIG
	 * as a workaround for the incorrect value being applied
	 * from transmitter control.
	 */
	if (!(dc_is_virtual_signal(pipe_ctx->stream->signal) ||
			dp_is_128b_132b_signal(pipe_ctx))) {
			struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;

			if (link_enc)
				link_enc->funcs->setup(
					link_enc,
					pipe_ctx->stream->signal);

			if (stream_enc && stream_enc->funcs->dig_stream_enable)
				stream_enc->funcs->dig_stream_enable(
					stream_enc,
					pipe_ctx->stream->signal, 1);

		}

	dc->hwss.enable_stream(pipe_ctx);

	/* Set DPS PPS SDP (AKA "info frames") */
	if (pipe_ctx->stream->timing.flags.DSC) {
		if (dc_is_dp_signal(pipe_ctx->stream->signal) ||
				dc_is_virtual_signal(pipe_ctx->stream->signal)) {
			dp_set_dsc_on_rx(pipe_ctx, true);
			link_set_dsc_pps_packet(pipe_ctx, true, true);
		}
	}

	if (pipe_ctx->stream->link->ep_type == DISPLAY_ENDPOINT_USB4_DPIA)
		allocate_usb4_bandwidth(pipe_ctx->stream);

	if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)
		allocate_mst_payload(pipe_ctx);
	else if (pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT &&
			dp_is_128b_132b_signal(pipe_ctx))
		update_sst_payload(pipe_ctx, true);

	dc->hwss.unblank_stream(pipe_ctx,
		&pipe_ctx->stream->link->cur_link_settings);

	if (stream->sink_patches.delay_ignore_msa > 0)
		msleep(stream->sink_patches.delay_ignore_msa);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		enable_stream_features(pipe_ctx);
	update_psp_stream_config(pipe_ctx, false);

	dc->hwss.enable_audio_stream(pipe_ctx);

	if (dc_is_hdmi_signal(pipe_ctx->stream->signal)) {
		set_avmute(pipe_ctx, false);
	}
}
