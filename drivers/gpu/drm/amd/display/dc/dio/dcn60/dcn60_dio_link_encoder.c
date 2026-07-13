// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "reg_helper.h"

#include "core_types.h"
#include "link_encoder.h"
#include "dcn31/dcn31_dio_link_encoder.h"
#include "dcn32/dcn32_dio_link_encoder.h"
#include "dcn401/dcn401_dio_link_encoder.h"
#include "dcn42/dcn42_dio_link_encoder.h"
#include "dcn60_dio_link_encoder.h"
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

#define AUX_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_ex(CTX, \
				AUX_REG(reg_name), \
				n, __VA_ARGS__)

#define AUX_REG_UPDATE_1(reg_name, field, val)	\
		AUX_REG_UPDATE_N(reg_name, 1, \
				FN(reg_name, field), val)

#define HPD_REG(reg)\
	(enc10->hpd_regs->reg)

#define HPD_REG_GET(reg_name, field, val)	\
		generic_reg_get(CTX, HPD_REG(reg_name), \
				FN(reg_name, field), val)

#define HPD_REG_SET_N(reg_name, n, initial_val, ...)	\
		generic_reg_set_ex(CTX, \
				HPD_REG(reg_name), \
				initial_val, \
				n, __VA_ARGS__)

#define HPD_REG_SET_2(reg, init_value, f1, v1, f2, v2)	\
		HPD_REG_SET_N(reg, 2, init_value, \
				FN(reg, f1), v1,\
				FN(reg, f2), v2)

#define HPD_REG_SET_1(reg, init_value, f1, v1)	\
		HPD_REG_SET_N(reg, 1, init_value, \
				FN(reg, f1), v1)

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

// DP2/HDMI FRL PRESET_LEVEL EQ options for FFE byte
// Preset level 0-32 bits[0:4]
#define PRESET_LEVEL_BYTE__LEVEL__SHIFT 0x0u
#define PRESET_LEVEL_BYTE__LEVEL__MASK 0x1Fu
// Enable no preshoot bit[5]
#define PRESET_LEVEL_BYTE__NO_PRE__SHIFT 0x5u
#define PRESET_LEVEL_BYTE__NO_PRE__MASK 0x20u
// Enable no demphasis bit[6]
#define PRESET_LEVEL_BYTE__NO_DEMPH__SHIFT 0x6u
#define PRESET_LEVEL_BYTE__NO_DEMPH__MASK 0x40u
// Enable method 2 bit[7]
#define PRESET_LEVEL_BYTE__METHOD_2__SHIFT 0x7u
#define PRESET_LEVEL_BYTE__METHOD_2__MASK 0x80u

void enc60_hw_init(struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	switch (enc10->base.connector.id) {
	case CONNECTOR_ID_DISPLAY_PORT:
	case CONNECTOR_ID_EDP:
		AUX_REG_WRITE(AUX_DPHY_RX_CONTROL0, 0x103d1110);
		AUX_REG_WRITE(AUX_DPHY_TX_CONTROL, 0x21c7a);
		dcn10_aux_initialize(enc10);
		break;
	default:
		break;
	}
	REG_UPDATE(TMDS_CTL_BITS, TMDS_CTL0, 1);

	// Polarity update is handled by DMU init.
	// AUX_PAD_MODE update done by DMU init
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

void dpcs60_program_eq_setting(
		struct link_encoder *enc,
		uint8_t FFE_Level,
		bool de_emphasis_only,
		bool pre_shoot_only,
		bool no_ffe,
		const struct dc_hdmi_frl_link_settings *link_settings)
{
	const uint8_t max_ffe_level =
			(link_settings->frl_link_rate > HDMI_FRL_LINK_RATE_12GBPS) ? 0x7 : 0x3;
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };

	if (enc10->base.ctx->dc->debug.ignore_ffe)
		return;

	if (FFE_Level <= max_ffe_level)
		enc10->base.txffe_state = FFE_Level;

	if (enc10->base.ctx->dc->debug.select_ffe)
		enc10->base.txffe_state =
				(uint8_t)enc10->base.ctx->dc->debug.select_ffe;

	if (FFE_Level == 0xEE) {
		enc10->base.txffe_state++;
		if (enc10->base.txffe_state > max_ffe_level)
			enc10->base.txffe_state = 0;
	}

	if (no_ffe) {
		de_emphasis_only = true;
		pre_shoot_only = true;
	}
	/* Pass on the input params to DMCUB for proper calc of eq settings */
	cntl.lane_settings = ((de_emphasis_only ? 1u : 0u) << PRESET_LEVEL_BYTE__NO_PRE__SHIFT) |
			     ((pre_shoot_only ? 1u : 0u) << PRESET_LEVEL_BYTE__NO_DEMPH__SHIFT) |
			     ((enc10->base.txffe_state & PRESET_LEVEL_BYTE__LEVEL__MASK)
			      << PRESET_LEVEL_BYTE__LEVEL__SHIFT);
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
		cntl.pixel_clock = 666667 / 10;
		break;
	case HDMI_FRL_LINK_RATE_16GBPS:
		cntl.pixel_clock = 888889 / 10;
		break;
	case HDMI_FRL_LINK_RATE_20GBPS:
	default:
		cntl.pixel_clock = 1111111 / 10;
		break;
	}
	/* call VBIOS table to set eq settings - voltage swing and pre-emphasis */
	link_transmitter_control(enc10, &cntl);
}

static const struct link_encoder_funcs dcn60_link_enc_funcs = {
	.read_state = link_enc2_read_state,
	.validate_output_with_stream =
			dcn30_link_encoder_validate_output_with_stream,
	.hw_init = enc60_hw_init,
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
	.prog_eq_setting = dpcs60_program_eq_setting,
	.get_txffe = dpcs401_get_txffe,
	.set_txffe = dpcs401_set_txffe,
	.set_dio_phy_mux = dcn31_link_encoder_set_dio_phy_mux,
	.setup_ri_pj_check_in_sw_or_hw_mode = dcn401_setup_ri_pj_check_in_sw_or_hw_mode,
	.get_hpd_state = dcn42_get_hpd_state,
	.program_hpd_filter = dcn42_program_hpd_filter,
};

void dcn60_link_encoder_construct(
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

	enc10->base.funcs = &dcn60_link_enc_funcs;
	enc10->base.ctx = init_data->ctx;
	enc10->base.id = init_data->encoder;

	enc10->base.hpd_source = init_data->hpd_source;
	enc10->base.hpd_active_high = init_data->hpd_active_high;
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
				bp_cap_info.FRL_8G_EN || bp_cap_info.FRL_10G_EN || bp_cap_info.FRL_12G_EN ||
				bp_cap_info.FRL_16G_EN || bp_cap_info.FRL_20G_EN || bp_cap_info.FRL_24G_EN;
		enc10->base.features.flags.bits.IS_FRL_8G_CAPABLE = bp_cap_info.FRL_8G_EN;
		enc10->base.features.flags.bits.IS_FRL_10G_CAPABLE = bp_cap_info.FRL_10G_EN;
		enc10->base.features.flags.bits.IS_FRL_12G_CAPABLE = bp_cap_info.FRL_12G_EN;
		enc10->base.features.flags.bits.IS_FRL_16G_CAPABLE = bp_cap_info.FRL_16G_EN;
		enc10->base.features.flags.bits.IS_FRL_20G_CAPABLE = bp_cap_info.FRL_20G_EN;
		enc10->base.features.flags.bits.IS_FRL_24G_CAPABLE = bp_cap_info.FRL_24G_EN;
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
		enc10->base.features.flags.bits.IS_FRL_16G_CAPABLE = 1;
		enc10->base.features.flags.bits.IS_FRL_20G_CAPABLE = 1;
		enc10->base.features.flags.bits.IS_FRL_24G_CAPABLE = 0;
	}
}
