/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#ifndef __DC_LINK_ENCODER__DCN10_H__
#define __DC_LINK_ENCODER__DCN10_H__

#include "link_encoder.h"

#define TO_DCN10_LINK_ENC(link_encoder)\
	container_of(link_encoder, struct dcn10_link_encoder, base)


#define AUX_REG_LIST(id)\
	SRI(AUX_CONTROL, DP_AUX, id), \
	SRI(AUX_DPHY_RX_CONTROL0, DP_AUX, id)

#define HPD_REG_LIST(id)\
	SRI(DC_HPD_CONTROL, HPD, id)

#define LE_DCN_COMMON_REG_LIST(id) \
	SRI(DIG_BE_CNTL, DIG, id), \
	SRI(DIG_BE_EN_CNTL, DIG, id), \
	SRI(TMDS_CTL_BITS, DIG, id), \
	SRI(DP_CONFIG, DP, id), \
	SRI(DP_DPHY_CNTL, DP, id), \
	SRI(DP_DPHY_PRBS_CNTL, DP, id), \
	SRI(DP_DPHY_SCRAM_CNTL, DP, id),\
	SRI(DP_DPHY_SYM0, DP, id), \
	SRI(DP_DPHY_SYM1, DP, id), \
	SRI(DP_DPHY_SYM2, DP, id), \
	SRI(DP_DPHY_TRAINING_PATTERN_SEL, DP, id), \
	SRI(DP_LINK_CNTL, DP, id), \
	SRI(DP_LINK_FRAMING_CNTL, DP, id), \
	SRI(DP_MSE_SAT0, DP, id), \
	SRI(DP_MSE_SAT1, DP, id), \
	SRI(DP_MSE_SAT2, DP, id), \
	SRI(DP_MSE_SAT_UPDATE, DP, id), \
	SRI(DP_SEC_CNTL, DP, id), \
	SRI(DP_VID_STREAM_CNTL, DP, id), \
	SRI(DP_DPHY_FAST_TRAINING, DP, id), \
	SRI(DP_SEC_CNTL1, DP, id), \
	SRI(DP_DPHY_BS_SR_SWAP_CNTL, DP, id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id), \
	SRI(DP_DPHY_HBR2_PATTERN_CONTROL, DP, id)


#define LE_DCN10_REG_LIST(id)\
	LE_DCN_COMMON_REG_LIST(id)

struct dcn10_link_enc_aux_registers {
	uint32_t AUX_CONTROL;
	uint32_t AUX_DPHY_RX_CONTROL0;
};

struct dcn10_link_enc_hpd_registers {
	uint32_t DC_HPD_CONTROL;
};

struct dcn10_link_enc_registers {
	uint32_t DIG_BE_CNTL;
	uint32_t DIG_BE_EN_CNTL;
	uint32_t DP_CONFIG;
	uint32_t DP_DPHY_CNTL;
	uint32_t DP_DPHY_INTERNAL_CTRL;
	uint32_t DP_DPHY_PRBS_CNTL;
	uint32_t DP_DPHY_SCRAM_CNTL;
	uint32_t DP_DPHY_SYM0;
	uint32_t DP_DPHY_SYM1;
	uint32_t DP_DPHY_SYM2;
	uint32_t DP_DPHY_TRAINING_PATTERN_SEL;
	uint32_t DP_LINK_CNTL;
	uint32_t DP_LINK_FRAMING_CNTL;
	uint32_t DP_MSE_SAT0;
	uint32_t DP_MSE_SAT1;
	uint32_t DP_MSE_SAT2;
	uint32_t DP_MSE_SAT_UPDATE;
	uint32_t DP_SEC_CNTL;
	uint32_t DP_VID_STREAM_CNTL;
	uint32_t DP_DPHY_FAST_TRAINING;
	uint32_t DP_DPHY_BS_SR_SWAP_CNTL;
	uint32_t DP_DPHY_HBR2_PATTERN_CONTROL;
	uint32_t DP_SEC_CNTL1;
	uint32_t TMDS_CTL_BITS;
};

#define LE_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define LINK_ENCODER_MASK_SH_LIST_DCN10(mask_sh)\
	LE_SF(DIG0_DIG_BE_EN_CNTL, DIG_ENABLE, mask_sh),\
	LE_SF(DIG0_DIG_BE_CNTL, DIG_HPD_SELECT, mask_sh),\
	LE_SF(DIG0_DIG_BE_CNTL, DIG_MODE, mask_sh),\
	LE_SF(DIG0_DIG_BE_CNTL, DIG_FE_SOURCE_SELECT, mask_sh),\
	LE_SF(DIG0_TMDS_CTL_BITS, TMDS_CTL0, mask_sh), \
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_BYPASS, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE0, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE1, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE2, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE3, mask_sh),\
	LE_SF(DP0_DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN, mask_sh),\
	LE_SF(DP0_DP_DPHY_PRBS_CNTL, DPHY_PRBS_SEL, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM0, DPHY_SYM1, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM0, DPHY_SYM2, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM0, DPHY_SYM3, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM1, DPHY_SYM4, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM1, DPHY_SYM5, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM1, DPHY_SYM6, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM2, DPHY_SYM7, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM2, DPHY_SYM8, mask_sh),\
	LE_SF(DP0_DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_BS_COUNT, mask_sh),\
	LE_SF(DP0_DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_ADVANCE, mask_sh),\
	LE_SF(DP0_DP_DPHY_FAST_TRAINING, DPHY_RX_FAST_TRAINING_CAPABLE, mask_sh),\
	LE_SF(DP0_DP_DPHY_BS_SR_SWAP_CNTL, DPHY_LOAD_BS_COUNT, mask_sh),\
	LE_SF(DP0_DP_DPHY_TRAINING_PATTERN_SEL, DPHY_TRAINING_PATTERN_SEL, mask_sh),\
	LE_SF(DP0_DP_DPHY_HBR2_PATTERN_CONTROL, DP_DPHY_HBR2_PATTERN_CONTROL, mask_sh),\
	LE_SF(DP0_DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE, mask_sh),\
	LE_SF(DP0_DP_LINK_FRAMING_CNTL, DP_IDLE_BS_INTERVAL, mask_sh),\
	LE_SF(DP0_DP_LINK_FRAMING_CNTL, DP_VBID_DISABLE, mask_sh),\
	LE_SF(DP0_DP_LINK_FRAMING_CNTL, DP_VID_ENHANCED_FRAME_MODE, mask_sh),\
	LE_SF(DP0_DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, mask_sh),\
	LE_SF(DP0_DP_CONFIG, DP_UDI_LANES, mask_sh),\
	LE_SF(DP0_DP_SEC_CNTL1, DP_SEC_GSP0_LINE_NUM, mask_sh),\
	LE_SF(DP0_DP_SEC_CNTL1, DP_SEC_GSP0_PRIORITY, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT0, DP_MSE_SAT_SRC0, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT0, DP_MSE_SAT_SRC1, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT0, DP_MSE_SAT_SLOT_COUNT0, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT0, DP_MSE_SAT_SLOT_COUNT1, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT1, DP_MSE_SAT_SRC2, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT1, DP_MSE_SAT_SRC3, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT1, DP_MSE_SAT_SLOT_COUNT2, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT1, DP_MSE_SAT_SLOT_COUNT3, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT_UPDATE, DP_MSE_SAT_UPDATE, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT_UPDATE, DP_MSE_16_MTP_KEEPOUT, mask_sh),\
	LE_SF(DP_AUX0_AUX_CONTROL, AUX_HPD_SEL, mask_sh),\
	LE_SF(DP_AUX0_AUX_CONTROL, AUX_LS_READ_EN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_RECEIVE_WINDOW, mask_sh),\
	LE_SF(HPD0_DC_HPD_CONTROL, DC_HPD_EN, mask_sh)

#define DCN_LINK_ENCODER_REG_FIELD_LIST(type) \
	type DIG_ENABLE;\
	type DIG_HPD_SELECT;\
	type DIG_MODE;\
	type DIG_FE_SOURCE_SELECT;\
	type DPHY_BYPASS;\
	type DPHY_ATEST_SEL_LANE0;\
	type DPHY_ATEST_SEL_LANE1;\
	type DPHY_ATEST_SEL_LANE2;\
	type DPHY_ATEST_SEL_LANE3;\
	type DPHY_PRBS_EN;\
	type DPHY_PRBS_SEL;\
	type DPHY_SYM1;\
	type DPHY_SYM2;\
	type DPHY_SYM3;\
	type DPHY_SYM4;\
	type DPHY_SYM5;\
	type DPHY_SYM6;\
	type DPHY_SYM7;\
	type DPHY_SYM8;\
	type DPHY_SCRAMBLER_BS_COUNT;\
	type DPHY_SCRAMBLER_ADVANCE;\
	type DPHY_RX_FAST_TRAINING_CAPABLE;\
	type DPHY_LOAD_BS_COUNT;\
	type DPHY_TRAINING_PATTERN_SEL;\
	type DP_DPHY_HBR2_PATTERN_CONTROL;\
	type DP_LINK_TRAINING_COMPLETE;\
	type DP_IDLE_BS_INTERVAL;\
	type DP_VBID_DISABLE;\
	type DP_VID_ENHANCED_FRAME_MODE;\
	type DP_VID_STREAM_ENABLE;\
	type DP_UDI_LANES;\
	type DP_SEC_GSP0_LINE_NUM;\
	type DP_SEC_GSP0_PRIORITY;\
	type DP_MSE_SAT_SRC0;\
	type DP_MSE_SAT_SRC1;\
	type DP_MSE_SAT_SRC2;\
	type DP_MSE_SAT_SRC3;\
	type DP_MSE_SAT_SLOT_COUNT0;\
	type DP_MSE_SAT_SLOT_COUNT1;\
	type DP_MSE_SAT_SLOT_COUNT2;\
	type DP_MSE_SAT_SLOT_COUNT3;\
	type DP_MSE_SAT_UPDATE;\
	type DP_MSE_16_MTP_KEEPOUT;\
	type DC_HPD_EN;\
	type TMDS_CTL0;\
	type AUX_HPD_SEL;\
	type AUX_LS_READ_EN;\
	type AUX_RX_RECEIVE_WINDOW

struct dcn10_link_enc_shift {
	DCN_LINK_ENCODER_REG_FIELD_LIST(uint8_t);
};

struct dcn10_link_enc_mask {
	DCN_LINK_ENCODER_REG_FIELD_LIST(uint32_t);
};

struct dcn10_link_encoder {
	struct link_encoder base;
	const struct dcn10_link_enc_registers *link_regs;
	const struct dcn10_link_enc_aux_registers *aux_regs;
	const struct dcn10_link_enc_hpd_registers *hpd_regs;
	const struct dcn10_link_enc_shift *link_shift;
	const struct dcn10_link_enc_mask *link_mask;
};


void dcn10_link_encoder_construct(
	struct dcn10_link_encoder *enc10,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask);

bool dcn10_link_encoder_validate_dvi_output(
	const struct dcn10_link_encoder *enc10,
	enum signal_type connector_signal,
	enum signal_type signal,
	const struct dc_crtc_timing *crtc_timing);

bool dcn10_link_encoder_validate_rgb_output(
	const struct dcn10_link_encoder *enc10,
	const struct dc_crtc_timing *crtc_timing);

bool dcn10_link_encoder_validate_dp_output(
	const struct dcn10_link_encoder *enc10,
	const struct dc_crtc_timing *crtc_timing);

bool dcn10_link_encoder_validate_wireless_output(
	const struct dcn10_link_encoder *enc10,
	const struct dc_crtc_timing *crtc_timing);

bool dcn10_link_encoder_validate_output_with_stream(
	struct link_encoder *enc,
	const struct dc_stream_state *stream);

/****************** HW programming ************************/

/* initialize HW */  /* why do we initialze aux in here? */
void dcn10_link_encoder_hw_init(struct link_encoder *enc);

void dcn10_link_encoder_destroy(struct link_encoder **enc);

/* program DIG_MODE in DIG_BE */
/* TODO can this be combined with enable_output? */
void dcn10_link_encoder_setup(
	struct link_encoder *enc,
	enum signal_type signal);

void configure_encoder(
	struct dcn10_link_encoder *enc10,
	const struct dc_link_settings *link_settings);

/* enables TMDS PHY output */
/* TODO: still need depth or just pass in adjusted pixel clock? */
void dcn10_link_encoder_enable_tmds_output(
	struct link_encoder *enc,
	enum clock_source_id clock_source,
	enum dc_color_depth color_depth,
	enum signal_type signal,
	uint32_t pixel_clock);

/* enables DP PHY output */
void dcn10_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source);

/* enables DP PHY output in MST mode */
void dcn10_link_encoder_enable_dp_mst_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source);

/* disable PHY output */
void dcn10_link_encoder_disable_output(
	struct link_encoder *enc,
	enum signal_type signal);

/* set DP lane settings */
void dcn10_link_encoder_dp_set_lane_settings(
	struct link_encoder *enc,
	const struct link_training_settings *link_settings);

void dcn10_link_encoder_dp_set_phy_pattern(
	struct link_encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param);

/* programs DP MST VC payload allocation */
void dcn10_link_encoder_update_mst_stream_allocation_table(
	struct link_encoder *enc,
	const struct link_mst_stream_allocation_table *table);

void dcn10_link_encoder_connect_dig_be_to_fe(
	struct link_encoder *enc,
	enum engine_id engine,
	bool connect);

void dcn10_link_encoder_set_dp_phy_pattern_training_pattern(
	struct link_encoder *enc,
	uint32_t index);

void dcn10_link_encoder_enable_hpd(struct link_encoder *enc);

void dcn10_link_encoder_disable_hpd(struct link_encoder *enc);

void dcn10_psr_program_dp_dphy_fast_training(struct link_encoder *enc,
			bool exit_link_training_required);

void dcn10_psr_program_secondary_packet(struct link_encoder *enc,
			unsigned int sdp_transmit_line_num_deadline);

bool dcn10_is_dig_enabled(struct link_encoder *enc);

void dcn10_aux_initialize(struct dcn10_link_encoder *enc10);

#endif /* __DC_LINK_ENCODER__DCN10_H__ */
