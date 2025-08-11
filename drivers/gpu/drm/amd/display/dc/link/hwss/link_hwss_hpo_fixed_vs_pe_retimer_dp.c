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
#include "link_hwss_hpo_dp.h"
#include "link_hwss_hpo_fixed_vs_pe_retimer_dp.h"
#include "link_hwss_dio_fixed_vs_pe_retimer.h"

static void dp_hpo_fixed_vs_pe_retimer_set_tx_ffe(struct dc_link *link,
		const struct dc_lane_settings *hw_lane_settings)
{
	const uint8_t vendor_ffe_preset_table[16] = {
											0x01, 0x41, 0x61, 0x81,
											0xB1, 0x05, 0x35, 0x65,
											0x85, 0xA5, 0x09, 0x39,
											0x59, 0x89, 0x0F, 0x24};

	const uint8_t ffe_mask[4] = {
			(hw_lane_settings[0].FFE_PRESET.settings.no_deemphasis != 0 ? 0x0F : 0xFF)
				& (hw_lane_settings[0].FFE_PRESET.settings.no_preshoot != 0 ? 0xF1 : 0xFF),
			(hw_lane_settings[1].FFE_PRESET.settings.no_deemphasis != 0 ? 0x0F : 0xFF)
				& (hw_lane_settings[1].FFE_PRESET.settings.no_preshoot != 0 ? 0xF1 : 0xFF),
			(hw_lane_settings[2].FFE_PRESET.settings.no_deemphasis != 0 ? 0x0F : 0xFF)
				& (hw_lane_settings[2].FFE_PRESET.settings.no_preshoot != 0 ? 0xF1 : 0xFF),
			(hw_lane_settings[3].FFE_PRESET.settings.no_deemphasis != 0 ? 0x0F : 0xFF)
				& (hw_lane_settings[3].FFE_PRESET.settings.no_preshoot != 0 ? 0xF1 : 0xFF)};

	const uint8_t ffe_cfg[4] = {
			vendor_ffe_preset_table[hw_lane_settings[0].FFE_PRESET.settings.level] & ffe_mask[0],
			vendor_ffe_preset_table[hw_lane_settings[1].FFE_PRESET.settings.level] & ffe_mask[1],
			vendor_ffe_preset_table[hw_lane_settings[2].FFE_PRESET.settings.level] & ffe_mask[2],
			vendor_ffe_preset_table[hw_lane_settings[3].FFE_PRESET.settings.level] & ffe_mask[3]};

	const uint8_t dp_type = dp_dio_fixed_vs_pe_retimer_lane_cfg_to_hw_cfg(link);

	const uint8_t vendor_lttpr_write_data_ffe1[4] = {0x01, 0x50, dp_type, 0x0F};
	const uint8_t vendor_lttpr_write_data_ffe2[4] = {0x01, 0x55, dp_type, ffe_cfg[0]};
	const uint8_t vendor_lttpr_write_data_ffe3[4] = {0x01, 0x56, dp_type, ffe_cfg[1]};
	const uint8_t vendor_lttpr_write_data_ffe4[4] = {0x01, 0x57, dp_type, ffe_cfg[2]};
	const uint8_t vendor_lttpr_write_data_ffe5[4] = {0x01, 0x58, dp_type, ffe_cfg[3]};

	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_ffe1[0], sizeof(vendor_lttpr_write_data_ffe1));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_ffe2[0], sizeof(vendor_lttpr_write_data_ffe2));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_ffe3[0], sizeof(vendor_lttpr_write_data_ffe3));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_ffe4[0], sizeof(vendor_lttpr_write_data_ffe4));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_ffe5[0], sizeof(vendor_lttpr_write_data_ffe5));
}

static void dp_hpo_fixed_vs_pe_retimer_program_override_test_pattern(struct dc_link *link,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	uint8_t clk_src = 0xC4;
	uint8_t pattern = 0x4F; /* SQ128 */

	const uint8_t vendor_lttpr_write_data_pg0[4] = {0x1, 0x11, 0x0, 0x0};
	const uint8_t vendor_lttpr_write_data_pg1[4] = {0x1, 0x50, 0x50, clk_src};
	const uint8_t vendor_lttpr_write_data_pg2[4] = {0x1, 0x51, 0x50, clk_src};
	const uint8_t vendor_lttpr_write_data_pg3[4]  = {0x1, 0x10, 0x58, 0x21};
	const uint8_t vendor_lttpr_write_data_pg4[4]  = {0x1, 0x10, 0x59, 0x21};
	const uint8_t vendor_lttpr_write_data_pg5[4] = {0x1, 0x1C, 0x58, pattern};
	const uint8_t vendor_lttpr_write_data_pg6[4] = {0x1, 0x1C, 0x59, pattern};
	const uint8_t vendor_lttpr_write_data_pg7[4]  = {0x1, 0x30, 0x51, 0x20};
	const uint8_t vendor_lttpr_write_data_pg8[4]  = {0x1, 0x30, 0x52, 0x20};
	const uint8_t vendor_lttpr_write_data_pg9[4]  = {0x1, 0x30, 0x54, 0x20};
	const uint8_t vendor_lttpr_write_data_pg10[4] = {0x1, 0x30, 0x55, 0x20};

	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg0[0], sizeof(vendor_lttpr_write_data_pg0));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg1[0], sizeof(vendor_lttpr_write_data_pg1));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg2[0], sizeof(vendor_lttpr_write_data_pg2));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg3[0], sizeof(vendor_lttpr_write_data_pg3));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg4[0], sizeof(vendor_lttpr_write_data_pg4));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg5[0], sizeof(vendor_lttpr_write_data_pg5));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg6[0], sizeof(vendor_lttpr_write_data_pg6));

	if (link->cur_link_settings.lane_count == LANE_COUNT_FOUR)
		link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_pg7[0], sizeof(vendor_lttpr_write_data_pg7));

	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg8[0], sizeof(vendor_lttpr_write_data_pg8));

	if (link->cur_link_settings.lane_count == LANE_COUNT_FOUR)
		link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
				&vendor_lttpr_write_data_pg9[0], sizeof(vendor_lttpr_write_data_pg9));

	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg10[0], sizeof(vendor_lttpr_write_data_pg10));
}

static bool dp_hpo_fixed_vs_pe_retimer_set_override_test_pattern(struct dc_link *link,
		const struct link_resource *link_res, struct encoder_set_dp_phy_pattern_param *tp_params,
		const struct link_hwss *link_hwss)
{
	struct encoder_set_dp_phy_pattern_param hw_tp_params = { 0 };
	const uint8_t vendor_lttpr_exit_manual_automation_0[4] = {0x1, 0x11, 0x0, 0x06};

	if (!link->dpcd_caps.lttpr_caps.main_link_channel_coding.bits.DP_128b_132b_SUPPORTED)
		return false;

	if (tp_params == NULL)
		return false;

	if (!IS_DP_PHY_SQUARE_PATTERN(tp_params->dp_phy_pattern)) {
		// Deprogram overrides from previously set square wave override
		if (link->current_test_pattern == DP_TEST_PATTERN_80BIT_CUSTOM ||
				link->current_test_pattern == DP_TEST_PATTERN_D102)
			link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
					&vendor_lttpr_exit_manual_automation_0[0],
					sizeof(vendor_lttpr_exit_manual_automation_0));
		else if (IS_DP_PHY_SQUARE_PATTERN(link->current_test_pattern))
			dp_dio_fixed_vs_pe_retimer_exit_manual_automation(link);

		return false;
	}

	hw_tp_params.dp_phy_pattern = DP_TEST_PATTERN_PRBS31;
	hw_tp_params.dp_panel_mode = tp_params->dp_panel_mode;

	if (link_hwss->ext.set_dp_link_test_pattern)
		link_hwss->ext.set_dp_link_test_pattern(link, link_res, &hw_tp_params);

	dp_hpo_fixed_vs_pe_retimer_program_override_test_pattern(link, tp_params);

	return true;
}

static void set_hpo_fixed_vs_pe_retimer_dp_link_test_pattern(struct dc_link *link,
		const struct link_resource *link_res,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	if (!dp_hpo_fixed_vs_pe_retimer_set_override_test_pattern(
			link, link_res, tp_params, get_hpo_dp_link_hwss())) {
		link_res->hpo_dp_link_enc->funcs->set_link_test_pattern(
				link_res->hpo_dp_link_enc, tp_params);
	}

	link->dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_SET_SOURCE_PATTERN);

	// Give retimer extra time to lock before updating DP_TRAINING_PATTERN_SET to TPS1 or phy pattern
	if (tp_params->dp_phy_pattern != DP_TEST_PATTERN_128b_132b_TPS2_TRAINING_MODE)
		msleep(50);
}

static void set_hpo_fixed_vs_pe_retimer_dp_lane_settings(struct dc_link *link,
		const struct link_resource *link_res,
		const struct dc_link_settings *link_settings,
		const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
	// Don't update our HW FFE when outputting phy test patterns
	if (IS_DP_PHY_PATTERN(link->pending_test_pattern)) {
		// Directly program FIXED_VS retimer FFE for SQ128 override
		if (IS_DP_PHY_SQUARE_PATTERN(link->pending_test_pattern)) {
			dp_hpo_fixed_vs_pe_retimer_set_tx_ffe(link, &lane_settings[0]);
		}
	} else {
		link_res->hpo_dp_link_enc->funcs->set_ffe(
				link_res->hpo_dp_link_enc,
				link_settings,
				lane_settings[0].FFE_PRESET.raw);
	}
}

static void enable_hpo_fixed_vs_pe_retimer_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings)
{
	if (link_settings->lane_count == LANE_COUNT_FOUR)
		enable_dio_fixed_vs_pe_retimer_program_4lane_output(link);

	enable_hpo_dp_link_output(link, link_res, signal, clock_source, link_settings);
}

static const struct link_hwss hpo_fixed_vs_pe_retimer_dp_link_hwss = {
	.setup_stream_encoder = setup_hpo_dp_stream_encoder,
	.reset_stream_encoder = reset_hpo_dp_stream_encoder,
	.setup_stream_attribute = setup_hpo_dp_stream_attribute,
	.disable_link_output = disable_hpo_dp_link_output,
	.setup_audio_output = setup_hpo_dp_audio_output,
	.enable_audio_packet = enable_hpo_dp_audio_packet,
	.disable_audio_packet = disable_hpo_dp_audio_packet,
	.ext = {
		.set_throttled_vcp_size = set_hpo_dp_throttled_vcp_size,
		.set_hblank_min_symbol_width = set_hpo_dp_hblank_min_symbol_width,
		.enable_dp_link_output = enable_hpo_fixed_vs_pe_retimer_dp_link_output,
		.set_dp_link_test_pattern  = set_hpo_fixed_vs_pe_retimer_dp_link_test_pattern,
		.set_dp_lane_settings = set_hpo_fixed_vs_pe_retimer_dp_lane_settings,
		.update_stream_allocation_table = update_hpo_dp_stream_allocation_table,
	},
};

bool requires_fixed_vs_pe_retimer_hpo_link_hwss(const struct dc_link *link)
{
	return requires_fixed_vs_pe_retimer_dio_link_hwss(link);
}

const struct link_hwss *get_hpo_fixed_vs_pe_retimer_dp_link_hwss(void)
{
	return &hpo_fixed_vs_pe_retimer_dp_link_hwss;
}
