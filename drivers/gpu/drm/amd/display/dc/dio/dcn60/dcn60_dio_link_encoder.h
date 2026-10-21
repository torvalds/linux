// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DC_LINK_ENCODER__DCN60_H__
#define __DC_LINK_ENCODER__DCN60_H__

#include "dcn30/dcn30_dio_link_encoder.h"

#define LINK_ENCODER_MASK_SH_LIST_DCN60(mask_sh) \
	LE_SF(DIG0_HDCP_I2C_CONTROL_0, HDCP_I2C_DISABLE, mask_sh),\
	LE_SF(DIG0_HDCP_I2C_CONTROL_0, HDCP_I2C_DDC_SELECT, mask_sh),\
	LE_SF(DIG0_HDCP_INT_CONTROL, HDCP_I2C_XFER_REQ_MASK, mask_sh),\
	LE_SF(HPD0_DC_HPD_INT_STATUS, DC_HPD_SENSE, mask_sh),\
	LE_SF(HPD0_DC_HPD_TOGGLE_FILT_CNTL, DC_HPD_CONNECT_INT_DELAY, mask_sh),\
	LE_SF(HPD0_DC_HPD_TOGGLE_FILT_CNTL, DC_HPD_DISCONNECT_INT_DELAY, mask_sh),\
	LE_SF(DC_GPIO_DDC1_MASK, AUX_PAD1_MODE, mask_sh),\
	SF(HPD_CTRL, HPD1_Y_POL_INVERT, mask_sh),\
	SF(HPD_CTRL, HPD2_Y_POL_INVERT, mask_sh),\
	SF(HPD_CTRL, HPD3_Y_POL_INVERT, mask_sh),\
	SF(HPD_CTRL, HPD4_Y_POL_INVERT, mask_sh)

void dcn60_link_encoder_construct(
	struct dcn20_link_encoder *enc20,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask);

void dpcs60_program_eq_setting(
		struct link_encoder *enc,
		uint8_t FFE_Level,
		bool de_emphasis_only,
		bool pre_shoot_only,
		bool no_ffe,
		const struct dc_hdmi_frl_link_settings *link_settings);

void enc60_hw_init(struct link_encoder *enc);

#endif /* __DC_LINK_ENCODER__DCN60_H__ */
