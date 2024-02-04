/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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


#include "reg_helper.h"

#include "core_types.h"
#include "link_encoder.h"
#include "dcn31/dcn31_dio_link_encoder.h"
#include "dcn32_dio_link_encoder.h"
#include "stream_encoder.h"
#include "dc_bios_types.h"
#include "link_enc_cfg.h"

#include "gpio_service_interface.h"

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

#define CTX \
	enc10->base.ctx
#define DC_LOGGER \
	enc10->base.ctx->logger

#define REG(reg)\
	(enc10->link_regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc10->link_shift->field_name, enc10->link_mask->field_name

#define AUX_REG(reg)\
	(enc10->aux_regs->reg)

#define AUX_REG_READ(reg_name) \
		dm_read_reg(CTX, AUX_REG(reg_name))

#define AUX_REG_WRITE(reg_name, val) \
			dm_write_reg(CTX, AUX_REG(reg_name), val)


void enc32_hw_init(struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

/*
	00 - DP_AUX_DPHY_RX_DETECTION_THRESHOLD__1to2 : 1/2
	01 - DP_AUX_DPHY_RX_DETECTION_THRESHOLD__3to4 : 3/4
	02 - DP_AUX_DPHY_RX_DETECTION_THRESHOLD__7to8 : 7/8
	03 - DP_AUX_DPHY_RX_DETECTION_THRESHOLD__15to16 : 15/16
	04 - DP_AUX_DPHY_RX_DETECTION_THRESHOLD__31to32 : 31/32
	05 - DP_AUX_DPHY_RX_DETECTION_THRESHOLD__63to64 : 63/64
	06 - DP_AUX_DPHY_RX_DETECTION_THRESHOLD__127to128 : 127/128
	07 - DP_AUX_DPHY_RX_DETECTION_THRESHOLD__255to256 : 255/256
*/

/*
	AUX_REG_UPDATE_5(AUX_DPHY_RX_CONTROL0,
	AUX_RX_START_WINDOW = 1 [6:4]
	AUX_RX_RECEIVE_WINDOW = 1 default is 2 [10:8]
	AUX_RX_HALF_SYM_DETECT_LEN  = 1 [13:12] default is 1
	AUX_RX_TRANSITION_FILTER_EN = 1 [16] default is 1
	AUX_RX_ALLOW_BELOW_THRESHOLD_PHASE_DETECT [17] is 0  default is 0
	AUX_RX_ALLOW_BELOW_THRESHOLD_START [18] is 1  default is 1
	AUX_RX_ALLOW_BELOW_THRESHOLD_STOP [19] is 1  default is 1
	AUX_RX_PHASE_DETECT_LEN,  [21,20] = 0x3 default is 3
	AUX_RX_DETECTION_THRESHOLD [30:28] = 1
*/
	AUX_REG_WRITE(AUX_DPHY_RX_CONTROL0, 0x103d1110);

	AUX_REG_WRITE(AUX_DPHY_TX_CONTROL, 0x21c7a);

	//AUX_DPHY_TX_REF_CONTROL'AUX_TX_REF_DIV HW default is 0x32;
	// Set AUX_TX_REF_DIV Divider to generate 2 MHz reference from refclk
	// 27MHz -> 0xd
	// 100MHz -> 0x32
	// 48MHz -> 0x18

	// Set TMDS_CTL0 to 1.  This is a legacy setting.
	REG_UPDATE(TMDS_CTL_BITS, TMDS_CTL0, 1);

	dcn10_aux_initialize(enc10);
}


void dcn32_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	if (!enc->ctx->dc->debug.avoid_vbios_exec_table) {
		dcn10_link_encoder_enable_dp_output(enc, link_settings, clock_source);
		return;
	}
}

static bool dcn32_link_encoder_is_in_alt_mode(struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	uint32_t dp_alt_mode_disable = 0;
	bool is_usb_c_alt_mode = false;

	if (enc->features.flags.bits.DP_IS_USB_C) {
		/* if value == 1 alt mode is disabled, otherwise it is enabled */
		REG_GET(RDPCSPIPE_PHY_CNTL6, RDPCS_PHY_DPALT_DISABLE, &dp_alt_mode_disable);
		is_usb_c_alt_mode = (dp_alt_mode_disable == 0);
	}

	return is_usb_c_alt_mode;
}

static void dcn32_link_encoder_get_max_link_cap(struct link_encoder *enc,
	struct dc_link_settings *link_settings)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	uint32_t is_in_usb_c_dp4_mode = 0;

	dcn10_link_encoder_get_max_link_cap(enc, link_settings);

	/* in usb c dp2 mode, max lane count is 2 */
	if (enc->funcs->is_in_alt_mode && enc->funcs->is_in_alt_mode(enc)) {
		REG_GET(RDPCSPIPE_PHY_CNTL6, RDPCS_PHY_DPALT_DP4, &is_in_usb_c_dp4_mode);
		if (!is_in_usb_c_dp4_mode)
			link_settings->lane_count = MIN(LANE_COUNT_TWO, link_settings->lane_count);
	}

}

static const struct link_encoder_funcs dcn32_link_enc_funcs = {
	.read_state = link_enc2_read_state,
	.validate_output_with_stream =
			dcn30_link_encoder_validate_output_with_stream,
	.hw_init = enc32_hw_init,
	.setup = dcn10_link_encoder_setup,
	.enable_tmds_output = dcn10_link_encoder_enable_tmds_output,
	.enable_dp_output = dcn32_link_encoder_enable_dp_output,
	.enable_dp_mst_output = dcn10_link_encoder_enable_dp_mst_output,
	.disable_output = dcn10_link_encoder_disable_output,
	.dp_set_lane_settings = dcn10_link_encoder_dp_set_lane_settings,
	.dp_set_phy_pattern = dcn10_link_encoder_dp_set_phy_pattern,
	.update_mst_stream_allocation_table =
		dcn10_link_encoder_update_mst_stream_allocation_table,
	.psr_program_dp_dphy_fast_training =
			dcn10_psr_program_dp_dphy_fast_training,
	.psr_program_secondary_packet = dcn10_psr_program_secondary_packet,
	.connect_dig_be_to_fe = dcn10_link_encoder_connect_dig_be_to_fe,
	.enable_hpd = dcn10_link_encoder_enable_hpd,
	.disable_hpd = dcn10_link_encoder_disable_hpd,
	.is_dig_enabled = dcn10_is_dig_enabled,
	.destroy = dcn10_link_encoder_destroy,
	.fec_set_enable = enc2_fec_set_enable,
	.fec_set_ready = enc2_fec_set_ready,
	.fec_is_active = enc2_fec_is_active,
	.get_dig_frontend = dcn10_get_dig_frontend,
	.get_dig_mode = dcn10_get_dig_mode,
	.is_in_alt_mode = dcn32_link_encoder_is_in_alt_mode,
	.get_max_link_cap = dcn32_link_encoder_get_max_link_cap,
	.set_dio_phy_mux = dcn31_link_encoder_set_dio_phy_mux,
};

void dcn32_link_encoder_construct(
	struct dcn20_link_encoder *enc20,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask)
{
	struct bp_connector_speed_cap_info bp_cap_info = {0};
	const struct dc_vbios_funcs *bp_funcs = init_data->ctx->dc_bios->funcs;
	enum bp_result result = BP_RESULT_OK;
	struct dcn10_link_encoder *enc10 = &enc20->enc10;

	enc10->base.funcs = &dcn32_link_enc_funcs;
	enc10->base.ctx = init_data->ctx;
	enc10->base.id = init_data->encoder;

	enc10->base.hpd_source = init_data->hpd_source;
	enc10->base.connector = init_data->connector;


	enc10->base.preferred_engine = ENGINE_ID_UNKNOWN;

	enc10->base.features = *enc_features;
	if (enc10->base.connector.id == CONNECTOR_ID_USBC)
		enc10->base.features.flags.bits.DP_IS_USB_C = 1;

	enc10->base.transmitter = init_data->transmitter;

	/* set the flag to indicate whether driver poll the I2C data pin
	 * while doing the DP sink detect
	 */

/*	if (dal_adapter_service_is_feature_supported(as,
		FEATURE_DP_SINK_DETECT_POLL_DATA_PIN))
		enc10->base.features.flags.bits.
			DP_SINK_DETECT_POLL_DATA_PIN = true;*/

	enc10->base.output_signals =
		SIGNAL_TYPE_DVI_SINGLE_LINK |
		SIGNAL_TYPE_DVI_DUAL_LINK |
		SIGNAL_TYPE_LVDS |
		SIGNAL_TYPE_DISPLAY_PORT |
		SIGNAL_TYPE_DISPLAY_PORT_MST |
		SIGNAL_TYPE_EDP |
		SIGNAL_TYPE_HDMI_TYPE_A;

	enc10->link_regs = link_regs;
	enc10->aux_regs = aux_regs;
	enc10->hpd_regs = hpd_regs;
	enc10->link_shift = link_shift;
	enc10->link_mask = link_mask;

	switch (enc10->base.transmitter) {
	case TRANSMITTER_UNIPHY_A:
		enc10->base.preferred_engine = ENGINE_ID_DIGA;
	break;
	case TRANSMITTER_UNIPHY_B:
		enc10->base.preferred_engine = ENGINE_ID_DIGB;
	break;
	case TRANSMITTER_UNIPHY_C:
		enc10->base.preferred_engine = ENGINE_ID_DIGC;
	break;
	case TRANSMITTER_UNIPHY_D:
		enc10->base.preferred_engine = ENGINE_ID_DIGD;
	break;
	case TRANSMITTER_UNIPHY_E:
		enc10->base.preferred_engine = ENGINE_ID_DIGE;
	break;
	default:
		ASSERT_CRITICAL(false);
		enc10->base.preferred_engine = ENGINE_ID_UNKNOWN;
	}

	/* default to one to mirror Windows behavior */
	enc10->base.features.flags.bits.HDMI_6GB_EN = 1;

	if (bp_funcs->get_connector_speed_cap_info)
		result = bp_funcs->get_connector_speed_cap_info(enc10->base.ctx->dc_bios,
						enc10->base.connector, &bp_cap_info);

	/* Override features with DCE-specific values */
	if (result == BP_RESULT_OK) {
		enc10->base.features.flags.bits.IS_HBR2_CAPABLE =
				bp_cap_info.DP_HBR2_EN;
		enc10->base.features.flags.bits.IS_HBR3_CAPABLE =
				bp_cap_info.DP_HBR3_EN;
		enc10->base.features.flags.bits.HDMI_6GB_EN = bp_cap_info.HDMI_6GB_EN;
		enc10->base.features.flags.bits.IS_DP2_CAPABLE = 1;
		enc10->base.features.flags.bits.IS_UHBR10_CAPABLE = bp_cap_info.DP_UHBR10_EN;
		enc10->base.features.flags.bits.IS_UHBR13_5_CAPABLE = bp_cap_info.DP_UHBR13_5_EN;
		enc10->base.features.flags.bits.IS_UHBR20_CAPABLE = bp_cap_info.DP_UHBR20_EN;
	} else {
		DC_LOG_WARNING("%s: Failed to get encoder_cap_info from VBIOS with error code %d!\n",
				__func__,
				result);
	}
	if (enc10->base.ctx->dc->debug.hdmi20_disable) {
		enc10->base.features.flags.bits.HDMI_6GB_EN = 0;
	}
}
