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
#include "link_hwss_dio.h"
#include "link_hwss_dio_fixed_vs_pe_retimer.h"
#include "link_enc_cfg.h"

uint8_t dp_dio_fixed_vs_pe_retimer_lane_cfg_to_hw_cfg(struct dc_link *link)
{
	// TODO: Get USB-C cable orientation
	if (link->cur_link_settings.lane_count == LANE_COUNT_FOUR)
		return 0xF2;
	else
		return 0x12;
}

void dp_dio_fixed_vs_pe_retimer_exit_manual_automation(struct dc_link *link)
{
	const uint8_t dp_type = dp_dio_fixed_vs_pe_retimer_lane_cfg_to_hw_cfg(link);
	const uint8_t vendor_lttpr_exit_manual_automation_0[4] = {0x1, 0x11, 0x0, 0x06};
	const uint8_t vendor_lttpr_exit_manual_automation_1[4] = {0x1, 0x50, dp_type, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_2[4] = {0x1, 0x50, 0x50, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_3[4] = {0x1, 0x51, 0x50, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_4[4] = {0x1, 0x10, 0x58, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_5[4] = {0x1, 0x10, 0x59, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_6[4] = {0x1, 0x30, 0x51, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_7[4] = {0x1, 0x30, 0x52, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_8[4] = {0x1, 0x30, 0x54, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_9[4] = {0x1, 0x30, 0x55, 0x0};

	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_0[0], sizeof(vendor_lttpr_exit_manual_automation_0));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_1[0], sizeof(vendor_lttpr_exit_manual_automation_1));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_2[0], sizeof(vendor_lttpr_exit_manual_automation_2));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_3[0], sizeof(vendor_lttpr_exit_manual_automation_3));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_4[0], sizeof(vendor_lttpr_exit_manual_automation_4));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_5[0], sizeof(vendor_lttpr_exit_manual_automation_5));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_6[0], sizeof(vendor_lttpr_exit_manual_automation_6));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_7[0], sizeof(vendor_lttpr_exit_manual_automation_7));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_8[0], sizeof(vendor_lttpr_exit_manual_automation_8));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_exit_manual_automation_9[0], sizeof(vendor_lttpr_exit_manual_automation_9));
}

static bool set_dio_fixed_vs_pe_retimer_dp_link_test_pattern_override(struct dc_link *link,
		const struct link_resource *link_res, struct encoder_set_dp_phy_pattern_param *tp_params,
		const struct link_hwss *link_hwss)
{
	struct encoder_set_dp_phy_pattern_param hw_tp_params = { 0 };
	const uint8_t pltpat_custom[10] = {0x1F, 0x7C, 0xF0, 0xC1, 0x07, 0x1F, 0x7C, 0xF0, 0xC1, 0x07};
	const uint8_t vendor_lttpr_write_data_pg0[4] = {0x1, 0x11, 0x0, 0x0};
	const uint8_t vendor_lttpr_exit_manual_automation_0[4] = {0x1, 0x11, 0x0, 0x06};

	if (!link->dpcd_caps.lttpr_caps.main_link_channel_coding.bits.DP_128b_132b_SUPPORTED)
		return false;

	if (tp_params == NULL)
		return false;

	if (IS_DP_PHY_SQUARE_PATTERN(link->current_test_pattern))
		// Deprogram overrides from previous test pattern
		dp_dio_fixed_vs_pe_retimer_exit_manual_automation(link);

	switch (tp_params->dp_phy_pattern) {
	case DP_TEST_PATTERN_80BIT_CUSTOM:
		if (tp_params->custom_pattern_size == 0 || memcmp(tp_params->custom_pattern,
				pltpat_custom, tp_params->custom_pattern_size) != 0)
			return false;
		hw_tp_params.custom_pattern = tp_params->custom_pattern;
		hw_tp_params.custom_pattern_size = tp_params->custom_pattern_size;
		break;
	case DP_TEST_PATTERN_D102:
		break;
	default:
		if (link->current_test_pattern == DP_TEST_PATTERN_80BIT_CUSTOM ||
				link->current_test_pattern == DP_TEST_PATTERN_D102)
			// Deprogram overrides from previous test pattern
			link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
					&vendor_lttpr_exit_manual_automation_0[0],
					sizeof(vendor_lttpr_exit_manual_automation_0));

		return false;
	}

	hw_tp_params.dp_phy_pattern = tp_params->dp_phy_pattern;
	hw_tp_params.dp_panel_mode = tp_params->dp_panel_mode;

	if (link_hwss->ext.set_dp_link_test_pattern)
		link_hwss->ext.set_dp_link_test_pattern(link, link_res, &hw_tp_params);

	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_pg0[0], sizeof(vendor_lttpr_write_data_pg0));

	return true;
}

static void set_dio_fixed_vs_pe_retimer_dp_link_test_pattern(struct dc_link *link,
		const struct link_resource *link_res,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	if (!set_dio_fixed_vs_pe_retimer_dp_link_test_pattern_override(
			link, link_res, tp_params, get_dio_link_hwss())) {
		link_enc->funcs->dp_set_phy_pattern(link_enc, tp_params);
	}
	link->dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_SET_SOURCE_PATTERN);
}

void enable_dio_fixed_vs_pe_retimer_program_4lane_output(struct dc_link *link)
{
	const uint8_t vendor_lttpr_write_data_4lane_1[4] = {0x1, 0x6E, 0xF2, 0x19};
	const uint8_t vendor_lttpr_write_data_4lane_2[4] = {0x1, 0x6B, 0xF2, 0x01};
	const uint8_t vendor_lttpr_write_data_4lane_3[4] = {0x1, 0x6D, 0xF2, 0x18};
	const uint8_t vendor_lttpr_write_data_4lane_4[4] = {0x1, 0x6C, 0xF2, 0x03};
	const uint8_t vendor_lttpr_write_data_4lane_5[4] = {0x1, 0x03, 0xF3, 0x06};

	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_4lane_1[0], sizeof(vendor_lttpr_write_data_4lane_1));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_4lane_2[0], sizeof(vendor_lttpr_write_data_4lane_2));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_4lane_3[0], sizeof(vendor_lttpr_write_data_4lane_3));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_4lane_4[0], sizeof(vendor_lttpr_write_data_4lane_4));
	link->dc->link_srv->configure_fixed_vs_pe_retimer(link->ddc,
			&vendor_lttpr_write_data_4lane_5[0], sizeof(vendor_lttpr_write_data_4lane_5));
}

static void enable_dio_fixed_vs_pe_retimer_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings)
{
	if (link_settings->lane_count == LANE_COUNT_FOUR)
		enable_dio_fixed_vs_pe_retimer_program_4lane_output(link);

	enable_dio_dp_link_output(link, link_res, signal, clock_source, link_settings);
}

static const struct link_hwss dio_fixed_vs_pe_retimer_link_hwss = {
	.setup_stream_encoder = setup_dio_stream_encoder,
	.reset_stream_encoder = reset_dio_stream_encoder,
	.setup_stream_attribute = setup_dio_stream_attribute,
	.disable_link_output = disable_dio_link_output,
	.setup_audio_output = setup_dio_audio_output,
	.enable_audio_packet = enable_dio_audio_packet,
	.disable_audio_packet = disable_dio_audio_packet,
	.ext = {
		.set_throttled_vcp_size = set_dio_throttled_vcp_size,
		.enable_dp_link_output = enable_dio_fixed_vs_pe_retimer_dp_link_output,
		.set_dp_link_test_pattern = set_dio_fixed_vs_pe_retimer_dp_link_test_pattern,
		.set_dp_lane_settings = set_dio_dp_lane_settings,
		.update_stream_allocation_table = update_dio_stream_allocation_table,
	},
};

bool requires_fixed_vs_pe_retimer_dio_link_hwss(const struct dc_link *link)
{
	return (link->chip_caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN);
}

const struct link_hwss *get_dio_fixed_vs_pe_retimer_link_hwss(void)
{
	return &dio_fixed_vs_pe_retimer_link_hwss;
}
