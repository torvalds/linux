/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
#include "dm_services_types.h"

#include "virtual_link_encoder.h"

static bool virtual_link_encoder_validate_output_with_stream(
	struct link_encoder *enc,
	struct pipe_ctx *pipe_ctx) { return true; }

static void virtual_link_encoder_hw_init(struct link_encoder *enc) {}

static void virtual_link_encoder_setup(
	struct link_encoder *enc,
	enum signal_type signal) {}

static void virtual_link_encoder_enable_tmds_output(
	struct link_encoder *enc,
	enum clock_source_id clock_source,
	enum dc_color_depth color_depth,
	bool hdmi,
	bool dual_link,
	uint32_t pixel_clock) {}

static void virtual_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source) {}

static void virtual_link_encoder_enable_dp_mst_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source) {}

static void virtual_link_encoder_disable_output(
	struct link_encoder *link_enc,
	enum signal_type signal) {}

static void virtual_link_encoder_dp_set_lane_settings(
	struct link_encoder *enc,
	const struct link_training_settings *link_settings) {}

static void virtual_link_encoder_dp_set_phy_pattern(
	struct link_encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param) {}

static void virtual_link_encoder_update_mst_stream_allocation_table(
	struct link_encoder *enc,
	const struct link_mst_stream_allocation_table *table) {}

static void virtual_link_encoder_edp_backlight_control(
	struct link_encoder *enc,
	bool enable) {}

static void virtual_link_encoder_edp_power_control(
	struct link_encoder *enc,
	bool power_up) {}

static void virtual_link_encoder_connect_dig_be_to_fe(
	struct link_encoder *enc,
	enum engine_id engine,
	bool connect) {}

static void virtual_link_encoder_destroy(struct link_encoder **enc)
{
	dm_free(*enc);
	*enc = NULL;
}


static const struct link_encoder_funcs virtual_lnk_enc_funcs = {
	.validate_output_with_stream =
		virtual_link_encoder_validate_output_with_stream,
	.hw_init = virtual_link_encoder_hw_init,
	.setup = virtual_link_encoder_setup,
	.enable_tmds_output = virtual_link_encoder_enable_tmds_output,
	.enable_dp_output = virtual_link_encoder_enable_dp_output,
	.enable_dp_mst_output = virtual_link_encoder_enable_dp_mst_output,
	.disable_output = virtual_link_encoder_disable_output,
	.dp_set_lane_settings = virtual_link_encoder_dp_set_lane_settings,
	.dp_set_phy_pattern = virtual_link_encoder_dp_set_phy_pattern,
	.update_mst_stream_allocation_table =
		virtual_link_encoder_update_mst_stream_allocation_table,
	.backlight_control = virtual_link_encoder_edp_backlight_control,
	.power_control = virtual_link_encoder_edp_power_control,
	.connect_dig_be_to_fe = virtual_link_encoder_connect_dig_be_to_fe,
	.destroy = virtual_link_encoder_destroy
};

bool virtual_link_encoder_construct(
	struct link_encoder *enc, const struct encoder_init_data *init_data)
{
	enc->funcs = &virtual_lnk_enc_funcs;
	enc->ctx = init_data->ctx;
	enc->id = init_data->encoder;

	enc->hpd_source = init_data->hpd_source;
	enc->connector = init_data->connector;

	enc->transmitter = init_data->transmitter;

	enc->output_signals = SIGNAL_TYPE_VIRTUAL;

	enc->preferred_engine = ENGINE_ID_VIRTUAL;

	return true;
}


