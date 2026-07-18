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
#include "dcn32/dcn32_dio_link_encoder.h"
#include "dcn401_dio_link_encoder.h"
#include "stream_encoder.h"
#include "dc_bios_types.h"

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

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

// HDMI FRL EQ Setting masks/shifts
// EQ level 0-32 bits[0:1]
#define HDMI_FRL_EQ__LEVEL__SHIFT 0x0
#define HDMI_FRL_EQ__LEVEL__MASK 0x3
// Enable no preshoot bit[5]
#define HDMI_FRL_EQ__NO_PRE__SHIFT 0x5
// Enable no demphasis bit[6]
#define HDMI_FRL_EQ__NO_DEMPH__SHIFT 0x6
// Enable no FFE bit[4]
#define HDMI_FRL_EQ__NO_FFE__SHIFT 0x4

void enc401_hw_init(struct link_encoder *enc)
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


void dcn401_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	if (!enc->ctx->dc->debug.avoid_vbios_exec_table) {
		dcn10_link_encoder_enable_dp_output(enc, link_settings, clock_source);
		return;
	}
}

static enum bp_result link_transmitter_control(
	struct dcn10_link_encoder *enc10,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result;
	struct dc_bios *bp = enc10->base.ctx->dc_bios;

	result = bp->funcs->transmitter_control(bp, cntl);

	return result;
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

void dpcs401_program_eq_setting(
		struct link_encoder *enc,
		uint8_t FFE_Level,
		bool de_emphasis_only,
		bool pre_shoot_only,
		bool no_ffe,
		const struct dc_hdmi_frl_link_settings *link_settings)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };

	if (enc10->base.ctx->dc->debug.ignore_ffe)
		return;

	if (FFE_Level < 0x5)
		enc10->base.txffe_state = FFE_Level;

	if (enc10->base.ctx->dc->debug.select_ffe)
		enc10->base.txffe_state =
				(uint8_t)enc10->base.ctx->dc->debug.select_ffe;

	if (FFE_Level == 0xEE) {
		enc10->base.txffe_state++;
		if (enc10->base.txffe_state > 3)
			enc10->base.txffe_state = 0;
	}

	if (no_ffe) {
		de_emphasis_only = true;
		pre_shoot_only = true;
	}
	/* Pass on the input params to DMCUB for proper calc of eq settings */
	cntl.lane_settings = ((de_emphasis_only ? 1 : 0) << HDMI_FRL_EQ__NO_PRE__SHIFT) |
						 ((pre_shoot_only ? 1 : 0) << HDMI_FRL_EQ__NO_DEMPH__SHIFT) |
						 ((enc10->base.txffe_state & HDMI_FRL_EQ__LEVEL__MASK)
						  << HDMI_FRL_EQ__LEVEL__SHIFT);
	cntl.lane_select = 0;
	cntl.action = TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS;
	cntl.transmitter = enc10->base.transmitter;
	cntl.connector_obj_id = enc10->base.connector;
	cntl.lanes_number = link_settings->frl_num_lanes;
	cntl.hpd_sel = enc10->base.hpd_source;
	/* Use below or dc_link_frl_bandwidth_kbps()? */
	switch (link_settings->frl_link_rate) {
	case HDMI_FRL_LINK_RATE_3GBPS:
		cntl.pixel_clock = 166667 / 10;
		break;
	case HDMI_FRL_LINK_RATE_6GBPS:
	case HDMI_FRL_LINK_RATE_6GBPS_4LANE:
		cntl.pixel_clock = 333333 / 10;
		break;
	case HDMI_FRL_LINK_RATE_8GBPS:
		cntl.pixel_clock = 444444 / 10;
		break;
	case HDMI_FRL_LINK_RATE_10GBPS:
		cntl.pixel_clock = 555555 / 10;
		break;
	case HDMI_FRL_LINK_RATE_12GBPS:
	default:
		cntl.pixel_clock = 666667 / 10;
		break;
	}
	/* call VBIOS table to set eq settings - voltage swing and pre-emphasis */
	link_transmitter_control(enc10, &cntl);
}

void dpcs401_get_txffe(
		struct link_encoder *enc,
		struct frl_txffe *lane_settings)
{
	(void)enc;
	/* EQ setting for DP lane0 */
	uint32_t eq_main = 0;
	uint32_t eq_pre = 0;
	uint32_t eq_post = 0;

	/* TODO */
	//REG_GET_3(RDPCSTX_PHY_FUSE0,
	//		RDPCS_PHY_DP_TX0_EQ_MAIN, &eq_main,
	//		RDPCS_PHY_DP_TX0_EQ_PRE, &eq_pre,
	//		RDPCS_PHY_DP_TX0_EQ_POST, &eq_post);

	lane_settings->amplitude[0] = eq_main;
	lane_settings->pre_emphasis[0] = eq_pre;
	lane_settings->post_emphasis[0] = eq_post;

	//REG_GET_3(RDPCSTX_PHY_FUSE1,
	//		RDPCS_PHY_DP_TX1_EQ_MAIN, &eq_main,
	//		RDPCS_PHY_DP_TX1_EQ_PRE, &eq_pre,
	//		RDPCS_PHY_DP_TX1_EQ_POST, &eq_post);

	lane_settings->amplitude[1] = eq_main;
	lane_settings->pre_emphasis[1] = eq_pre;
	lane_settings->post_emphasis[1] = eq_post;

	//REG_GET_3(RDPCSTX_PHY_FUSE2,
	//		RDPCS_PHY_DP_TX2_EQ_MAIN, &eq_main,
	//		RDPCS_PHY_DP_TX2_EQ_PRE, &eq_pre,
	//		RDPCS_PHY_DP_TX2_EQ_POST, &eq_post);

	lane_settings->amplitude[2] = eq_main;
	lane_settings->pre_emphasis[2] = eq_pre;
	lane_settings->post_emphasis[2] = eq_post;

	//REG_GET_3(RDPCSTX_PHY_FUSE3,
	//		RDPCS_PHY_DP_TX3_EQ_MAIN, &eq_main,
	//		RDPCS_PHY_DP_TX3_EQ_PRE, &eq_pre,
	//		RDPCS_PHY_DP_TX3_EQ_POST, &eq_post);

	lane_settings->amplitude[3] = eq_main;
	lane_settings->pre_emphasis[3] = eq_pre;
	lane_settings->post_emphasis[3] = eq_post;

}

void dpcs401_set_txffe(
		struct link_encoder *enc,
		struct frl_txffe *lane_settings)
{
	(void)enc;
	(void)lane_settings;
	//struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	/* EQ setting for DP lane0 */
	//TODO: Unused
	//uint32_t eq_main;
	//uint32_t eq_pre;
	//uint32_t eq_post;

	//eq_main = lane_settings->amplitude[0];
	//eq_pre = lane_settings->pre_emphasis[0];
	//eq_post = lane_settings->post_emphasis[0];

	/* TODO */
	//REG_UPDATE_3(RDPCSTX_PHY_FUSE0,
	//		RDPCS_PHY_DP_TX0_EQ_MAIN, eq_main,
	//		RDPCS_PHY_DP_TX0_EQ_PRE, eq_pre,
	//		RDPCS_PHY_DP_TX0_EQ_POST, eq_post);

	//eq_main = lane_settings->amplitude[1];
	//eq_pre = lane_settings->pre_emphasis[1];
	//eq_post = lane_settings->post_emphasis[1];

	//REG_UPDATE_3(RDPCSTX_PHY_FUSE1,
	//		RDPCS_PHY_DP_TX1_EQ_MAIN, eq_main,
	//		RDPCS_PHY_DP_TX1_EQ_PRE, eq_pre,
	//		RDPCS_PHY_DP_TX1_EQ_POST, eq_post);

	//eq_main = lane_settings->amplitude[2];
	//eq_pre = lane_settings->pre_emphasis[2];
	//eq_post = lane_settings->post_emphasis[2];

	//REG_UPDATE_3(RDPCSTX_PHY_FUSE2,
	//		RDPCS_PHY_DP_TX2_EQ_MAIN, eq_main,
	//		RDPCS_PHY_DP_TX2_EQ_PRE, eq_pre,
	//		RDPCS_PHY_DP_TX2_EQ_POST, eq_post);

	//eq_main = lane_settings->amplitude[3];
	//eq_pre = lane_settings->pre_emphasis[3];
	//eq_post = lane_settings->post_emphasis[3];

	//REG_UPDATE_3(RDPCSTX_PHY_FUSE3,
	//		RDPCS_PHY_DP_TX3_EQ_MAIN, eq_main,
	//		RDPCS_PHY_DP_TX3_EQ_PRE, eq_pre,
	//		RDPCS_PHY_DP_TX3_EQ_POST, eq_post);
}


void dcn401_link_encoder_setup(
	struct link_encoder *enc,
	enum signal_type signal)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	switch (signal) {
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		/* DP SST */
		REG_UPDATE(DIG_BE_CLK_CNTL, DIG_BE_MODE, 0);
		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		/* TMDS-DVI */
		REG_UPDATE(DIG_BE_CLK_CNTL, DIG_BE_MODE, 2);
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/* TMDS-HDMI */
		REG_UPDATE(DIG_BE_CLK_CNTL, DIG_BE_MODE, 3);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		/* DP MST */
		REG_UPDATE(DIG_BE_CLK_CNTL, DIG_BE_MODE, 5);
		break;
	default:
		ASSERT_CRITICAL(false);
		/* invalid mode ! */
		break;
	}
	REG_UPDATE(DIG_BE_CLK_CNTL, DIG_BE_CLK_EN, 1);
	REG_UPDATE(DIG_BE_EN_CNTL, DIG_BE_ENABLE, 1);
}

bool dcn401_is_dig_enabled(struct link_encoder *enc)
{
	uint32_t clk_enabled;
	uint32_t dig_enabled;
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	REG_GET(DIG_BE_CLK_CNTL, DIG_BE_CLK_EN, &clk_enabled);
	REG_GET(DIG_BE_EN_CNTL, DIG_BE_ENABLE, &dig_enabled);
	return (clk_enabled == 1 && dig_enabled == 1);
}

enum signal_type dcn401_get_dig_mode(
	struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	uint32_t value;
	REG_GET(DIG_BE_CLK_CNTL, DIG_BE_MODE, &value);
	switch (value) {
	case 0:
		return SIGNAL_TYPE_DISPLAY_PORT;
	case 2:
		return SIGNAL_TYPE_DVI_SINGLE_LINK;
	case 3:
		return SIGNAL_TYPE_HDMI_TYPE_A;
	case 5:
		return SIGNAL_TYPE_DISPLAY_PORT_MST;
	default:
		return SIGNAL_TYPE_NONE;
	}
}

static const struct link_encoder_funcs dcn401_link_enc_funcs = {
	.read_state = link_enc2_read_state,
	.validate_output_with_stream =
			dcn30_link_encoder_validate_output_with_stream,
	.hw_init = enc401_hw_init,
	.setup = dcn401_link_encoder_setup,
	.enable_tmds_output = dcn10_link_encoder_enable_tmds_output,
	.enable_dp_output = dcn401_link_encoder_enable_dp_output,
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
	.is_dig_enabled = dcn401_is_dig_enabled,
	.destroy = dcn10_link_encoder_destroy,
	.fec_set_enable = enc2_fec_set_enable,
	.fec_set_ready = enc2_fec_set_ready,
	.fec_is_active = enc2_fec_is_active,
	.get_dig_frontend = dcn10_get_dig_frontend,
	.get_dig_mode = dcn401_get_dig_mode,
	.is_in_alt_mode = dcn32_link_encoder_is_in_alt_mode,
	.get_max_link_cap = dcn32_link_encoder_get_max_link_cap,
	.dpcstx_set_order_invert_18_bit = NULL,
	.set_phy_source = NULL,
	.dpcs_initialize_phy = NULL,
	.dpcs_configure_phypll = NULL,
	.dpcs_configure_dpcs = NULL,
	.dpcs_enable_dpcs = NULL,
	.prog_eq_setting = dpcs401_program_eq_setting,
	.get_txffe = dpcs401_get_txffe,
	.set_txffe = dpcs401_set_txffe,
	.set_dio_phy_mux = dcn31_link_encoder_set_dio_phy_mux,
	.get_hpd_state = dcn10_get_hpd_state,
	.program_hpd_filter = dcn10_program_hpd_filter,
};

void dcn401_link_encoder_construct(
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

	enc10->base.funcs = &dcn401_link_enc_funcs;
	enc10->base.ctx = init_data->ctx;
	enc10->base.id = init_data->encoder;

	enc10->base.hpd_gpio = init_data->hpd_gpio;
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
		enc10->base.features.flags.bits.IS_HDMI_FRL_CAPABLE =
				bp_cap_info.FRL_8G_EN || bp_cap_info.FRL_10G_EN || bp_cap_info.FRL_12G_EN;
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
