/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "dcn30_dio_link_encoder.h"
#include "stream_encoder.h"
#include "dc_bios_types.h"
#include "gpio_service_interface.h"

#define CTX \
	enc10->base.ctx
#define DC_LOGGER \
	enc10->base.ctx->logger

#define REG(reg)\
	(enc10->link_regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc10->link_shift->field_name, enc10->link_mask->field_name

#define IND_REG(index) \
	(enc10->link_regs->index)


static bool dcn30_link_encoder_validate_hdmi_frl_output(
	const struct dcn10_link_encoder *enc10,
	const struct dc_crtc_timing *crtc_timing)
{
	enum dc_color_depth max_deep_color =
			enc10->base.features.max_hdmi_deep_color;

	if (!enc10->base.features.flags.bits.IS_HDMI_FRL_CAPABLE)
		return false;

	if (max_deep_color < crtc_timing->display_color_depth)
		return false;

	if (crtc_timing->display_color_depth < COLOR_DEPTH_888)
		return false;

	/* TODO: check if hdmi_charclk is above ASIC cap (10 GBS for DCN3AG) */

	return true;
}

bool dcn30_link_encoder_validate_output_with_stream(
	struct link_encoder *enc,
	const struct dc_stream_state *stream)
{
	if (dc_is_hdmi_frl_signal(stream->signal)) {
		struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

		return dcn30_link_encoder_validate_hdmi_frl_output(enc10, &stream->timing);
	} else {
		return dcn10_link_encoder_validate_output_with_stream(enc, stream);
	}
}

//---------------------------------------------------
// Task: Program EQ setting
// Note:
//      EQ setting can be dont during P2 state or P0 state
//      If set in P0 state, The values are latched in a single
//      cycle of txX_clk but will take maximum of 40 txX_clk symbols
//      to be reflected on the output. During this period the
//      analog serial lines might have a transitional behavior.
//---------------------------------------------------
void dpcs30_program_eq_setting(
		struct link_encoder *enc,
		uint8_t FFE_Level,
		bool de_emphasis_only,
		bool pre_shoot_only,
		bool no_ffe,
		const struct dc_hdmi_frl_link_settings *link_settings)
{
	(void)link_settings;
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	/* EQ setting for DP lane0 */
	uint32_t eq_main;
	uint32_t eq_pre;
	uint32_t eq_post;

	if (enc10->base.ctx->dc->debug.ignore_ffe)
		return;

	if (FFE_Level < 0x5)
		enc10->base.txffe_state = FFE_Level;

	if (FFE_Level == 0xEE) {
		enc10->base.txffe_state++;
		if (enc10->base.txffe_state > 3)
			enc10->base.txffe_state = 0;
	}

	switch (enc10->base.txffe_state) {
	case 0:
		eq_main = 0x31;
		if (de_emphasis_only)
			eq_main = 0x36;
		if (pre_shoot_only)
			eq_main = 0x39;
		eq_pre = 0x5;
		eq_post = 0x8;
		break;
	case 1:
		eq_main = 0x2F;
		if (de_emphasis_only)
			eq_main = 0x34;
		if (pre_shoot_only)
			eq_main = 0x39;
		eq_pre = 0x5;
		eq_post = 0xA;
		break;
	case 2:
		eq_main = 0x2C;
		if (de_emphasis_only)
			eq_main = 0x31;
		if (pre_shoot_only)
			eq_main = 0x39;
		eq_pre = 0x5;
		eq_post = 0xD;
		break;
	case 3:
		eq_main = 0x29;
		if (de_emphasis_only)
			eq_main = 0x2E;
		if (pre_shoot_only)
			eq_main = 0x39;
		eq_pre = 0x5;
		eq_post = 0x10;
		break;
	default:
		return;
	}

	eq_pre = de_emphasis_only ? 0 : eq_pre;
	eq_post = pre_shoot_only ? 0 : eq_post;

	if (no_ffe) {
		eq_pre = 0;
		eq_post = 0;
		eq_main = 0x3E;
	}

	REG_UPDATE_3(RDPCSTX_PHY_FUSE0,
			RDPCS_PHY_DP_TX0_EQ_MAIN, eq_main,
			RDPCS_PHY_DP_TX0_EQ_PRE, eq_pre,
			RDPCS_PHY_DP_TX0_EQ_POST, eq_post);

	REG_UPDATE_3(RDPCSTX_PHY_FUSE1,
			RDPCS_PHY_DP_TX1_EQ_MAIN, eq_main,
			RDPCS_PHY_DP_TX1_EQ_PRE, eq_pre,
			RDPCS_PHY_DP_TX1_EQ_POST, eq_post);

	REG_UPDATE_3(RDPCSTX_PHY_FUSE2,
			RDPCS_PHY_DP_TX2_EQ_MAIN, eq_main,
			RDPCS_PHY_DP_TX2_EQ_PRE, eq_pre,
			RDPCS_PHY_DP_TX2_EQ_POST, eq_post);

	REG_UPDATE_3(RDPCSTX_PHY_FUSE3,
			RDPCS_PHY_DP_TX3_EQ_MAIN, eq_main,
			RDPCS_PHY_DP_TX3_EQ_PRE, eq_pre,
			RDPCS_PHY_DP_TX3_EQ_POST, eq_post);
}

void dpcs30_get_txffe(
		struct link_encoder *enc,
		struct frl_txffe *lane_settings)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	/* EQ setting for DP lane0 */
	uint32_t eq_main;
	uint32_t eq_pre;
	uint32_t eq_post;

	REG_GET_3(RDPCSTX_PHY_FUSE0,
			RDPCS_PHY_DP_TX0_EQ_MAIN, &eq_main,
			RDPCS_PHY_DP_TX0_EQ_PRE, &eq_pre,
			RDPCS_PHY_DP_TX0_EQ_POST, &eq_post);

	lane_settings->amplitude[0] = eq_main;
	lane_settings->pre_emphasis[0] = eq_pre;
	lane_settings->post_emphasis[0] = eq_post;

	REG_GET_3(RDPCSTX_PHY_FUSE1,
			RDPCS_PHY_DP_TX1_EQ_MAIN, &eq_main,
			RDPCS_PHY_DP_TX1_EQ_PRE, &eq_pre,
			RDPCS_PHY_DP_TX1_EQ_POST, &eq_post);

	lane_settings->amplitude[1] = eq_main;
	lane_settings->pre_emphasis[1] = eq_pre;
	lane_settings->post_emphasis[1] = eq_post;

	REG_GET_3(RDPCSTX_PHY_FUSE2,
			RDPCS_PHY_DP_TX2_EQ_MAIN, &eq_main,
			RDPCS_PHY_DP_TX2_EQ_PRE, &eq_pre,
			RDPCS_PHY_DP_TX2_EQ_POST, &eq_post);

	lane_settings->amplitude[2] = eq_main;
	lane_settings->pre_emphasis[2] = eq_pre;
	lane_settings->post_emphasis[2] = eq_post;

	REG_GET_3(RDPCSTX_PHY_FUSE3,
			RDPCS_PHY_DP_TX3_EQ_MAIN, &eq_main,
			RDPCS_PHY_DP_TX3_EQ_PRE, &eq_pre,
			RDPCS_PHY_DP_TX3_EQ_POST, &eq_post);

	lane_settings->amplitude[3] = eq_main;
	lane_settings->pre_emphasis[3] = eq_pre;
	lane_settings->post_emphasis[3] = eq_post;

}

void dpcs30_set_txffe(
		struct link_encoder *enc,
		struct frl_txffe *lane_settings)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	/* EQ setting for DP lane0 */
	uint32_t eq_main;
	uint32_t eq_pre;
	uint32_t eq_post;

	eq_main = lane_settings->amplitude[0];
	eq_pre = lane_settings->pre_emphasis[0];
	eq_post = lane_settings->post_emphasis[0];

	REG_UPDATE_3(RDPCSTX_PHY_FUSE0,
			RDPCS_PHY_DP_TX0_EQ_MAIN, eq_main,
			RDPCS_PHY_DP_TX0_EQ_PRE, eq_pre,
			RDPCS_PHY_DP_TX0_EQ_POST, eq_post);

	eq_main = lane_settings->amplitude[1];
	eq_pre = lane_settings->pre_emphasis[1];
	eq_post = lane_settings->post_emphasis[1];

	REG_UPDATE_3(RDPCSTX_PHY_FUSE1,
			RDPCS_PHY_DP_TX1_EQ_MAIN, eq_main,
			RDPCS_PHY_DP_TX1_EQ_PRE, eq_pre,
			RDPCS_PHY_DP_TX1_EQ_POST, eq_post);

	eq_main = lane_settings->amplitude[2];
	eq_pre = lane_settings->pre_emphasis[2];
	eq_post = lane_settings->post_emphasis[2];

	REG_UPDATE_3(RDPCSTX_PHY_FUSE2,
			RDPCS_PHY_DP_TX2_EQ_MAIN, eq_main,
			RDPCS_PHY_DP_TX2_EQ_PRE, eq_pre,
			RDPCS_PHY_DP_TX2_EQ_POST, eq_post);

	eq_main = lane_settings->amplitude[3];
	eq_pre = lane_settings->pre_emphasis[3];
	eq_post = lane_settings->post_emphasis[3];

	REG_UPDATE_3(RDPCSTX_PHY_FUSE3,
			RDPCS_PHY_DP_TX3_EQ_MAIN, eq_main,
			RDPCS_PHY_DP_TX3_EQ_PRE, eq_pre,
			RDPCS_PHY_DP_TX3_EQ_POST, eq_post);
}

static const struct link_encoder_funcs dcn30_link_enc_funcs = {
	.read_state = link_enc2_read_state,
	.validate_output_with_stream =
			dcn30_link_encoder_validate_output_with_stream,
	.hw_init = enc3_hw_init,
	.setup = dcn10_link_encoder_setup,
	.enable_tmds_output = dcn10_link_encoder_enable_tmds_output,
	.enable_dp_output = dcn20_link_encoder_enable_dp_output,
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
	.is_in_alt_mode = dcn20_link_encoder_is_in_alt_mode,
	.get_max_link_cap = dcn20_link_encoder_get_max_link_cap,
	.dpcstx_set_order_invert_18_bit = NULL,
	.set_phy_source = NULL,
	.dpcs_initialize_phy = NULL,
	.dpcs_configure_phypll = NULL,
	.dpcs_configure_dpcs = NULL,
	.dpcs_enable_dpcs = NULL,
	.prog_eq_setting = dpcs30_program_eq_setting,
	.get_txffe = dpcs30_get_txffe,
	.set_txffe = dpcs30_set_txffe,
	.get_hpd_state = dcn10_get_hpd_state,
	.program_hpd_filter = dcn10_program_hpd_filter,
};

void dcn30_link_encoder_construct(
	struct dcn20_link_encoder *enc20,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask)
{
	struct bp_encoder_cap_info bp_cap_info = {0};
	const struct dc_vbios_funcs *bp_funcs = init_data->ctx->dc_bios->funcs;
	enum bp_result result = BP_RESULT_OK;
	struct dcn10_link_encoder *enc10 = &enc20->enc10;

	enc10->base.funcs = &dcn30_link_enc_funcs;
	enc10->base.ctx = init_data->ctx;
	enc10->base.id = init_data->encoder;

	enc10->base.hpd_gpio = init_data->hpd_gpio;
	enc10->base.hpd_source = init_data->hpd_source;
	enc10->base.connector = init_data->connector;

	enc10->base.preferred_engine = ENGINE_ID_UNKNOWN;

	enc10->base.features = *enc_features;

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

	/* For DCE 8.0 and 8.1, by design, UNIPHY is hardwired to DIG_BE.
	 * SW always assign DIG_FE 1:1 mapped to DIG_FE for non-MST UNIPHY.
	 * SW assign DIG_FE to non-MST UNIPHY first and MST last. So prefer
	 * DIG is per UNIPHY and used by SST DP, eDP, HDMI, DVI and LVDS.
	 * Prefer DIG assignment is decided by board design.
	 * For DCE 8.0, there are only max 6 UNIPHYs, we assume board design
	 * and VBIOS will filter out 7 UNIPHY for DCE 8.0.
	 * By this, adding DIGG should not hurt DCE 8.0.
	 * This will let DCE 8.1 share DCE 8.0 as much as possible
	 */

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
	case TRANSMITTER_UNIPHY_F:
		enc10->base.preferred_engine = ENGINE_ID_DIGF;
	break;
	case TRANSMITTER_UNIPHY_G:
		enc10->base.preferred_engine = ENGINE_ID_DIGG;
	break;
	default:
		ASSERT_CRITICAL(false);
		enc10->base.preferred_engine = ENGINE_ID_UNKNOWN;
	}

	/* default to one to mirror Windows behavior */
	enc10->base.features.flags.bits.HDMI_6GB_EN = 1;

	result = bp_funcs->get_encoder_cap_info(enc10->base.ctx->dc_bios,
						enc10->base.id, &bp_cap_info);

	/* Override features with DCE-specific values */
	if (result == BP_RESULT_OK) {
		enc10->base.features.flags.bits.IS_HBR2_CAPABLE =
				bp_cap_info.DP_HBR2_EN;
		enc10->base.features.flags.bits.IS_HBR3_CAPABLE =
				bp_cap_info.DP_HBR3_EN;
		enc10->base.features.flags.bits.HDMI_6GB_EN = bp_cap_info.HDMI_6GB_EN;
		enc10->base.features.flags.bits.IS_DP2_CAPABLE = bp_cap_info.IS_DP2_CAPABLE;
		enc10->base.features.flags.bits.IS_UHBR10_CAPABLE = bp_cap_info.DP_UHBR10_EN;
		enc10->base.features.flags.bits.IS_UHBR13_5_CAPABLE = bp_cap_info.DP_UHBR13_5_EN;
		enc10->base.features.flags.bits.IS_UHBR20_CAPABLE = bp_cap_info.DP_UHBR20_EN;
		enc10->base.features.flags.bits.DP_IS_USB_C =
				bp_cap_info.DP_IS_USB_C;

		enc10->base.features.flags.bits.IS_HDMI_FRL_CAPABLE = bp_cap_info.IS_HDMI_FRL_CAPABLE;
		enc10->base.features.flags.bits.IS_FRL_8G_CAPABLE = bp_cap_info.FRL_8G_EN;
		enc10->base.features.flags.bits.IS_FRL_10G_CAPABLE = bp_cap_info.FRL_10G_EN;
		enc10->base.features.flags.bits.IS_FRL_12G_CAPABLE = bp_cap_info.FRL_12G_EN;
		enc10->base.txffe_state = 0;
	} else {
		DC_LOG_WARNING("%s: Failed to get encoder_cap_info from VBIOS with error code %d!\n",
				__func__,
				result);
	}
	if (enc10->base.ctx->dc->debug.hdmi20_disable) {
		enc10->base.features.flags.bits.HDMI_6GB_EN = 0;
	}
	if (enc10->base.ctx->dc->config.force_hdmi21_frl_enc_enable) {
		enc10->base.features.flags.bits.IS_HDMI_FRL_CAPABLE = 1;
		enc10->base.features.flags.bits.IS_FRL_8G_CAPABLE = 1;
		enc10->base.features.flags.bits.IS_FRL_10G_CAPABLE = 1;
		enc10->base.features.flags.bits.IS_FRL_12G_CAPABLE = 1;
	}
}

#define AUX_REG(reg)\
	(enc10->aux_regs->reg)

#define AUX_REG_READ(reg_name) \
		dm_read_reg(CTX, AUX_REG(reg_name))

#define AUX_REG_WRITE(reg_name, val) \
			dm_write_reg(CTX, AUX_REG(reg_name), val)
void enc3_hw_init(struct link_encoder *enc)
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
