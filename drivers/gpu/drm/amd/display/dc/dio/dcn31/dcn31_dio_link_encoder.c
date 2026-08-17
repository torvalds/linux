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


#include "reg_helper.h"

#include "core_types.h"
#include "link_encoder.h"
#include "dcn31_dio_link_encoder.h"
#include "stream_encoder.h"
#include "dc_bios_types.h"

#include "gpio_service_interface.h"

#include "link_enc_cfg.h"
#include "dc_dmub_srv.h"
#include "dal_asic_id.h"
#include "link_service.h"

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

static uint8_t phy_id_from_transmitter(enum transmitter t)
{
	uint8_t phy_id;

	switch (t) {
	case TRANSMITTER_UNIPHY_A:
		phy_id = 0;
		break;
	case TRANSMITTER_UNIPHY_B:
		phy_id = 1;
		break;
	case TRANSMITTER_UNIPHY_C:
		phy_id = 2;
		break;
	case TRANSMITTER_UNIPHY_D:
		phy_id = 3;
		break;
	case TRANSMITTER_UNIPHY_E:
		phy_id = 4;
		break;
	case TRANSMITTER_UNIPHY_F:
		phy_id = 5;
		break;
	case TRANSMITTER_UNIPHY_G:
		phy_id = 6;
		break;
	default:
		phy_id = 0;
		break;
	}
	return phy_id;
}

static bool has_query_dp_alt(struct link_encoder *enc)
{
	struct dc_dmub_srv *dc_dmub_srv = enc->ctx->dmub_srv;

	if (enc->ctx->dce_version >= DCN_VERSION_3_15)
		return true;

	/* Supports development firmware and firmware >= 4.0.11 */
	return dc_dmub_srv &&
	       !(dc_dmub_srv->dmub->fw_version >= DMUB_FW_VERSION(4, 0, 0) &&
		 dc_dmub_srv->dmub->fw_version <= DMUB_FW_VERSION(4, 0, 10));
}

static bool query_dp_alt_from_dmub(struct link_encoder *enc,
				   union dmub_rb_cmd *cmd)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	memset(cmd, 0, sizeof(*cmd));
	cmd->query_dp_alt.header.type = DMUB_CMD__VBIOS;
	cmd->query_dp_alt.header.sub_type =
		DMUB_CMD__VBIOS_TRANSMITTER_QUERY_DP_ALT;
	cmd->query_dp_alt.header.payload_bytes = sizeof(cmd->query_dp_alt.data);
	cmd->query_dp_alt.data.phy_id = phy_id_from_transmitter(enc10->base.transmitter);

	if (!dc_wake_and_execute_dmub_cmd(enc->ctx, cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		return false;

	return true;
}

void dcn31_link_encoder_set_dio_phy_mux(
	struct link_encoder *enc,
	enum encoder_type_select sel,
	uint32_t hpo_inst)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	switch (enc->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		if (sel == ENCODER_TYPE_HDMI_FRL)
			REG_UPDATE(DIO_LINKA_CNTL,
					HPO_HDMI_ENC_SEL, hpo_inst);
		else if (sel == ENCODER_TYPE_DP_128B132B)
			REG_UPDATE(DIO_LINKA_CNTL,
					HPO_DP_ENC_SEL, hpo_inst);
		REG_UPDATE(DIO_LINKA_CNTL,
				ENC_TYPE_SEL, sel);
		break;
	case TRANSMITTER_UNIPHY_B:
		if (sel == ENCODER_TYPE_HDMI_FRL)
			REG_UPDATE(DIO_LINKB_CNTL,
					HPO_HDMI_ENC_SEL, hpo_inst);
		else if (sel == ENCODER_TYPE_DP_128B132B)
			REG_UPDATE(DIO_LINKB_CNTL,
					HPO_DP_ENC_SEL, hpo_inst);
		REG_UPDATE(DIO_LINKB_CNTL,
				ENC_TYPE_SEL, sel);
		break;
	case TRANSMITTER_UNIPHY_C:
		if (sel == ENCODER_TYPE_HDMI_FRL)
			REG_UPDATE(DIO_LINKC_CNTL,
					HPO_HDMI_ENC_SEL, hpo_inst);
		else if (sel == ENCODER_TYPE_DP_128B132B)
			REG_UPDATE(DIO_LINKC_CNTL,
					HPO_DP_ENC_SEL, hpo_inst);
		REG_UPDATE(DIO_LINKC_CNTL,
				ENC_TYPE_SEL, sel);
		break;
	case TRANSMITTER_UNIPHY_D:
		if (sel == ENCODER_TYPE_HDMI_FRL)
			REG_UPDATE(DIO_LINKD_CNTL,
					HPO_HDMI_ENC_SEL, hpo_inst);
		else if (sel == ENCODER_TYPE_DP_128B132B)
			REG_UPDATE(DIO_LINKD_CNTL,
					HPO_DP_ENC_SEL, hpo_inst);
		REG_UPDATE(DIO_LINKD_CNTL,
				ENC_TYPE_SEL, sel);
		break;
	case TRANSMITTER_UNIPHY_E:
		if (sel == ENCODER_TYPE_HDMI_FRL)
			REG_UPDATE(DIO_LINKE_CNTL,
					HPO_HDMI_ENC_SEL, hpo_inst);
		else if (sel == ENCODER_TYPE_DP_128B132B)
			REG_UPDATE(DIO_LINKE_CNTL,
					HPO_DP_ENC_SEL, hpo_inst);
		REG_UPDATE(DIO_LINKE_CNTL,
				ENC_TYPE_SEL, sel);
		break;
	case TRANSMITTER_UNIPHY_F:
		if (sel == ENCODER_TYPE_HDMI_FRL)
			REG_UPDATE(DIO_LINKF_CNTL,
					HPO_HDMI_ENC_SEL, hpo_inst);
		else if (sel == ENCODER_TYPE_DP_128B132B)
			REG_UPDATE(DIO_LINKF_CNTL,
					HPO_DP_ENC_SEL, hpo_inst);
		REG_UPDATE(DIO_LINKF_CNTL,
				ENC_TYPE_SEL, sel);
		break;
	default:
		/* Do nothing */
		break;
	}
}

void enc31_hw_init(struct link_encoder *enc)
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
	// dmub will read AUX_DPHY_RX_CONTROL0/AUX_DPHY_TX_CONTROL from vbios table in dp_aux_init

	//AUX_DPHY_TX_REF_CONTROL'AUX_TX_REF_DIV HW default is 0x32;
	// Set AUX_TX_REF_DIV Divider to generate 2 MHz reference from refclk
	// 27MHz -> 0xd
	// 100MHz -> 0x32
	// 48MHz -> 0x18

	// Set TMDS_CTL0 to 1.  This is a legacy setting.
	REG_UPDATE(TMDS_CTL_BITS, TMDS_CTL0, 1);

	dcn10_aux_initialize(enc10);
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
// Task: Program EQ setting in HDMI FRL mode
// Note:
//      EQ setting can be dont during P2 state or P0 state
//      If set in P0 state, The values are latched in a single
//      cycle of txX_clk but will take maximum of 40 txX_clk symbols
//      to be reflected on the output. During this period the
//      analog serial lines might have a transitional behavior.
//---------------------------------------------------
void dpcs31_program_eq_setting(
		struct link_encoder *enc,
		uint8_t FFE_Level,
		bool de_emphasis_only,
		bool pre_shoot_only,
		bool no_ffe,
		const struct dc_hdmi_frl_link_settings *link_settings)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	/* EQ setting for DP lane0 */

	if (enc10->base.ctx->dc->debug.ignore_ffe)
		return;

	if (FFE_Level < 0x5)
		enc10->base.txffe_state = FFE_Level;

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

static const struct link_encoder_funcs dcn31_link_enc_funcs = {
	.read_state = link_enc2_read_state,
	.validate_output_with_stream =
			dcn30_link_encoder_validate_output_with_stream,
	.hw_init = enc31_hw_init,
	.setup = dcn10_link_encoder_setup,
	.enable_tmds_output = dcn10_link_encoder_enable_tmds_output,
	.enable_dp_output = dcn31_link_encoder_enable_dp_output,
	.enable_dp_mst_output = dcn31_link_encoder_enable_dp_mst_output,
	.disable_output = dcn31_link_encoder_disable_output,
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
	.is_in_alt_mode = dcn31_link_encoder_is_in_alt_mode,
	.get_max_link_cap = dcn31_link_encoder_get_max_link_cap,
	.dpcstx_set_order_invert_18_bit = NULL,
	.set_phy_source = NULL,
	.dpcs_initialize_phy = NULL,
	.dpcs_configure_phypll = NULL,
	.dpcs_configure_dpcs = NULL,
	.dpcs_enable_dpcs = NULL,
	.prog_eq_setting = dpcs31_program_eq_setting,
	.get_txffe = dpcs30_get_txffe,
	.set_txffe = dpcs30_set_txffe,
	.set_dio_phy_mux = dcn31_link_encoder_set_dio_phy_mux,
	.get_hpd_state = dcn10_get_hpd_state,
	.program_hpd_filter = dcn10_program_hpd_filter,
};

void dcn31_link_encoder_construct(
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

	enc10->base.funcs = &dcn31_link_enc_funcs;
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

void dcn31_link_encoder_construct_minimal(
	struct dcn20_link_encoder *enc20,
	struct dc_context *ctx,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	enum engine_id eng_id)
{
	struct dcn10_link_encoder *enc10 = &enc20->enc10;

	enc10->base.funcs = &dcn31_link_enc_funcs;
	enc10->base.ctx = ctx;
	enc10->base.id.type = OBJECT_TYPE_ENCODER;
	enc10->base.hpd_source = HPD_SOURCEID_UNKNOWN;
	enc10->base.connector.type = OBJECT_TYPE_CONNECTOR;
	enc10->base.preferred_engine = eng_id;
	enc10->base.features = *enc_features;
	enc10->base.transmitter = TRANSMITTER_UNKNOWN;
	enc10->link_regs = link_regs;

	enc10->base.output_signals =
		SIGNAL_TYPE_DISPLAY_PORT |
		SIGNAL_TYPE_DISPLAY_PORT_MST |
		SIGNAL_TYPE_EDP;
}

/* DPIA equivalent of link_transmitter_control. */
static bool link_dpia_control(struct dc_context *dc_ctx,
	struct dmub_cmd_dig_dpia_control_data *dpia_control)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.dig1_dpia_control.header.type = DMUB_CMD__DPIA;
	cmd.dig1_dpia_control.header.sub_type =
			DMUB_CMD__DPIA_DIG1_DPIA_CONTROL;
	cmd.dig1_dpia_control.header.payload_bytes =
		sizeof(cmd.dig1_dpia_control) -
		sizeof(cmd.dig1_dpia_control.header);

	cmd.dig1_dpia_control.dpia_control = *dpia_control;

	dc_wake_and_execute_dmub_cmd(dc_ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

static void link_encoder_disable(struct dcn10_link_encoder *enc10)
{
	/* reset training complete */
	REG_UPDATE(DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE, 0);
}

void dcn31_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	/* Enable transmitter and encoder. */
	if (!link_enc_cfg_is_transmitter_mappable(enc->ctx->dc, enc)) {

		DC_LOG_DEBUG("%s: enc_id(%d)\n", __func__, enc->preferred_engine);
		dcn20_link_encoder_enable_dp_output(enc, link_settings, clock_source);

	} else {

		struct dmub_cmd_dig_dpia_control_data dpia_control = { 0 };
		struct dc_link *link;

		link = link_enc_cfg_get_link_using_link_enc(enc->ctx->dc, enc->preferred_engine);

		enc1_configure_encoder(enc10, link_settings);

		dpia_control.action = (uint8_t)TRANSMITTER_CONTROL_ENABLE;
		dpia_control.enc_id = enc->preferred_engine;
		dpia_control.mode_laneset.digmode = 0; /* 0 for SST; 5 for MST */
		dpia_control.lanenum = (uint8_t)link_settings->lane_count;
		dpia_control.symclk_10khz = link_settings->link_rate *
				LINK_RATE_REF_FREQ_IN_KHZ / 10;
		/* DIG_BE_CNTL.DIG_HPD_SELECT set to 5 (hpdsel - 1) to indicate HPD pin
		 * unused by DPIA.
		 */
		dpia_control.hpdsel = 6;

		if (link) {
			dpia_control.dpia_id = link->ddc_hw_inst;
			dpia_control.fec_rdy = link->dc->link_srv->dp_should_enable_fec(link);
		} else {
			DC_LOG_ERROR("%s: Failed to execute DPIA enable DMUB command.\n", __func__);
			BREAK_TO_DEBUGGER();
			return;
		}

		DC_LOG_DEBUG("%s: DPIA(%d) - enc_id(%d)\n", __func__, dpia_control.dpia_id, dpia_control.enc_id);
		link_dpia_control(enc->ctx, &dpia_control);
	}
}

void dcn31_link_encoder_enable_dp_mst_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	/* Enable transmitter and encoder. */
	if (!link_enc_cfg_is_transmitter_mappable(enc->ctx->dc, enc)) {

		DC_LOG_DEBUG("%s: enc_id(%d)\n", __func__, enc->preferred_engine);
		dcn10_link_encoder_enable_dp_mst_output(enc, link_settings, clock_source);

	} else {

		struct dmub_cmd_dig_dpia_control_data dpia_control = { 0 };
		struct dc_link *link;

		link = link_enc_cfg_get_link_using_link_enc(enc->ctx->dc, enc->preferred_engine);

		enc1_configure_encoder(enc10, link_settings);

		dpia_control.action = (uint8_t)TRANSMITTER_CONTROL_ENABLE;
		dpia_control.enc_id = enc->preferred_engine;
		dpia_control.mode_laneset.digmode = 5; /* 0 for SST; 5 for MST */
		dpia_control.lanenum = (uint8_t)link_settings->lane_count;
		dpia_control.symclk_10khz = link_settings->link_rate *
				LINK_RATE_REF_FREQ_IN_KHZ / 10;
		/* DIG_BE_CNTL.DIG_HPD_SELECT set to 5 (hpdsel - 1) to indicate HPD pin
		 * unused by DPIA.
		 */
		dpia_control.hpdsel = 6;

		if (link) {
			dpia_control.dpia_id = link->ddc_hw_inst;
			dpia_control.fec_rdy = link->dc->link_srv->dp_should_enable_fec(link);
		} else {
			DC_LOG_ERROR("%s: Failed to execute DPIA enable DMUB command.\n", __func__);
			BREAK_TO_DEBUGGER();
			return;
		}

		DC_LOG_DEBUG("%s: DPIA(%d) - enc_id(%d)\n", __func__, dpia_control.dpia_id, dpia_control.enc_id);
		link_dpia_control(enc->ctx, &dpia_control);
	}
}

void dcn31_link_encoder_disable_output(
	struct link_encoder *enc,
	enum signal_type signal)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	/* Disable transmitter and encoder. */
	if (!link_enc_cfg_is_transmitter_mappable(enc->ctx->dc, enc)) {

		DC_LOG_DEBUG("%s: enc_id(%d)\n", __func__, enc->preferred_engine);
		dcn10_link_encoder_disable_output(enc, signal);

	} else {

		struct dmub_cmd_dig_dpia_control_data dpia_control = { 0 };
		struct dc_link *link;

		if (enc->funcs->is_dig_enabled && !enc->funcs->is_dig_enabled(enc))
			return;

		link = link_enc_cfg_get_link_using_link_enc(enc->ctx->dc, enc->preferred_engine);

		dpia_control.action = (uint8_t)TRANSMITTER_CONTROL_DISABLE;
		dpia_control.enc_id = enc->preferred_engine;
		if (signal == SIGNAL_TYPE_DISPLAY_PORT) {
			dpia_control.mode_laneset.digmode = 0; /* 0 for SST; 5 for MST */
		} else if (signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
			dpia_control.mode_laneset.digmode = 5; /* 0 for SST; 5 for MST */
		} else {
			DC_LOG_ERROR("%s: USB4 DPIA only supports DisplayPort.\n", __func__);
			BREAK_TO_DEBUGGER();
		}

		if (link) {
			dpia_control.dpia_id = link->ddc_hw_inst;
		} else {
			DC_LOG_ERROR("%s: Failed to execute DPIA enable DMUB command.\n", __func__);
			BREAK_TO_DEBUGGER();
			return;
		}

		DC_LOG_DEBUG("%s: DPIA(%d) - enc_id(%d)\n", __func__, dpia_control.dpia_id, dpia_control.enc_id);
		link_dpia_control(enc->ctx, &dpia_control);

		link_encoder_disable(enc10);
	}
}

bool dcn31_link_encoder_is_in_alt_mode(struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	union dmub_rb_cmd cmd;
	uint32_t dp_alt_mode_disable;

	/* Only applicable to USB-C PHY. */
	if (!enc->features.flags.bits.DP_IS_USB_C)
		return false;

	/*
	 * Use the new interface from DMCUB if available.
	 * Avoids hanging the RDCPSPIPE if DMCUB wasn't already running.
	 */
	if (has_query_dp_alt(enc)) {
		if (!query_dp_alt_from_dmub(enc, &cmd))
			return false;

		return (cmd.query_dp_alt.data.is_dp_alt_disable == 0);
	}

	/* Legacy path, avoid if possible. */
	if (enc->ctx->asic_id.hw_internal_rev != YELLOW_CARP_B0) {
		REG_GET(RDPCSTX_PHY_CNTL6, RDPCS_PHY_DPALT_DISABLE,
			&dp_alt_mode_disable);
	} else {
		/*
		 * B0 phys use a new set of registers to check whether alt mode is disabled.
		 * if value == 1 alt mode is disabled, otherwise it is enabled.
		 */
		if ((enc10->base.transmitter == TRANSMITTER_UNIPHY_A) ||
		    (enc10->base.transmitter == TRANSMITTER_UNIPHY_B) ||
		    (enc10->base.transmitter == TRANSMITTER_UNIPHY_E)) {
			REG_GET(RDPCSTX_PHY_CNTL6, RDPCS_PHY_DPALT_DISABLE,
				&dp_alt_mode_disable);
		} else {
			REG_GET(RDPCSPIPE_PHY_CNTL6, RDPCS_PHY_DPALT_DISABLE,
				&dp_alt_mode_disable);
		}
	}

	return (dp_alt_mode_disable == 0);
}

void dcn31_link_encoder_get_max_link_cap(struct link_encoder *enc, struct dc_link_settings *link_settings)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	union dmub_rb_cmd cmd;
	uint32_t is_in_usb_c_dp4_mode = 0;

	dcn10_link_encoder_get_max_link_cap(enc, link_settings);

	/* Take the link cap directly if not USB */
	if (!enc->features.flags.bits.DP_IS_USB_C)
		return;

	/*
	 * Use the new interface from DMCUB if available.
	 * Avoids hanging the RDCPSPIPE if DMCUB wasn't already running.
	 */
	if (has_query_dp_alt(enc)) {
		if (!query_dp_alt_from_dmub(enc, &cmd))
			return;

		if (cmd.query_dp_alt.data.is_dp_alt_disable == 0 &&
				cmd.query_dp_alt.data.is_usb &&
				cmd.query_dp_alt.data.is_dp4 == 0)
			link_settings->lane_count = MIN(LANE_COUNT_TWO, link_settings->lane_count);

		return;
	}

	/* Legacy path, avoid if possible. */
	if (enc->ctx->asic_id.hw_internal_rev != YELLOW_CARP_B0) {
		REG_GET(RDPCSTX_PHY_CNTL6, RDPCS_PHY_DPALT_DP4,
			&is_in_usb_c_dp4_mode);
	} else {
		if ((enc10->base.transmitter == TRANSMITTER_UNIPHY_A) ||
		    (enc10->base.transmitter == TRANSMITTER_UNIPHY_B) ||
		    (enc10->base.transmitter == TRANSMITTER_UNIPHY_E)) {
			REG_GET(RDPCSTX_PHY_CNTL6, RDPCS_PHY_DPALT_DP4,
				&is_in_usb_c_dp4_mode);
		} else {
			REG_GET(RDPCSPIPE_PHY_CNTL6, RDPCS_PHY_DPALT_DP4,
				&is_in_usb_c_dp4_mode);
		}
	}

	if (!is_in_usb_c_dp4_mode)
		link_settings->lane_count = MIN(LANE_COUNT_TWO, link_settings->lane_count);
}
