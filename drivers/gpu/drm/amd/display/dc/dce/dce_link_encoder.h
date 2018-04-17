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

#ifndef __DC_LINK_ENCODER__DCE110_H__
#define __DC_LINK_ENCODER__DCE110_H__

#include "link_encoder.h"

#define TO_DCE110_LINK_ENC(link_encoder)\
	container_of(link_encoder, struct dce110_link_encoder, base)

/* Not found regs in dce120 spec
 * BIOS_SCRATCH_2
 * DP_DPHY_INTERNAL_CTRL
 */

#define AUX_REG_LIST(id)\
	SRI(AUX_CONTROL, DP_AUX, id), \
	SRI(AUX_DPHY_RX_CONTROL0, DP_AUX, id)

#define HPD_REG_LIST(id)\
	SRI(DC_HPD_CONTROL, HPD, id)

#define LE_COMMON_REG_LIST_BASE(id) \
	SR(DMCU_RAM_ACCESS_CTRL), \
	SR(DMCU_IRAM_RD_CTRL), \
	SR(DMCU_IRAM_RD_DATA), \
	SR(DMCU_INTERRUPT_TO_UC_EN_MASK), \
	SRI(DIG_BE_CNTL, DIG, id), \
	SRI(DIG_BE_EN_CNTL, DIG, id), \
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
	SRI(DP_SEC_CNTL1, DP, id)

#define LE_COMMON_REG_LIST(id)\
	LE_COMMON_REG_LIST_BASE(id), \
	SRI(DP_DPHY_BS_SR_SWAP_CNTL, DP, id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id), \
	SR(DCI_MEM_PWR_STATUS)

#define LE_DCE80_REG_LIST(id)\
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id), \
	LE_COMMON_REG_LIST_BASE(id)

#define LE_DCE100_REG_LIST(id)\
	LE_COMMON_REG_LIST_BASE(id), \
	SRI(DP_DPHY_BS_SR_SWAP_CNTL, DP, id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id), \
	SR(DCI_MEM_PWR_STATUS)

#define LE_DCE110_REG_LIST(id)\
	LE_COMMON_REG_LIST_BASE(id), \
	SRI(DP_DPHY_BS_SR_SWAP_CNTL, DP, id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id), \
	SRI(DP_DPHY_HBR2_PATTERN_CONTROL, DP, id), \
	SR(DCI_MEM_PWR_STATUS)

#define LE_DCE120_REG_LIST(id)\
	LE_COMMON_REG_LIST_BASE(id), \
	SRI(DP_DPHY_BS_SR_SWAP_CNTL, DP, id), \
	SRI(DP_DPHY_HBR2_PATTERN_CONTROL, DP, id), \
	SR(DCI_MEM_PWR_STATUS)

#define LE_DCN10_REG_LIST(id)\
	LE_COMMON_REG_LIST_BASE(id), \
	SRI(DP_DPHY_BS_SR_SWAP_CNTL, DP, id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id), \
	SRI(DP_DPHY_HBR2_PATTERN_CONTROL, DP, id)

struct dce110_link_enc_aux_registers {
	uint32_t AUX_CONTROL;
	uint32_t AUX_DPHY_RX_CONTROL0;
};

struct dce110_link_enc_hpd_registers {
	uint32_t DC_HPD_CONTROL;
};

struct dce110_link_enc_registers {
	/* DMCU registers */
	uint32_t MASTER_COMM_DATA_REG1;
	uint32_t MASTER_COMM_DATA_REG2;
	uint32_t MASTER_COMM_DATA_REG3;
	uint32_t MASTER_COMM_CMD_REG;
	uint32_t MASTER_COMM_CNTL_REG;
	uint32_t DMCU_RAM_ACCESS_CTRL;
	uint32_t DCI_MEM_PWR_STATUS;
	uint32_t DMU_MEM_PWR_CNTL;
	uint32_t DMCU_IRAM_RD_CTRL;
	uint32_t DMCU_IRAM_RD_DATA;
	uint32_t DMCU_INTERRUPT_TO_UC_EN_MASK;

	/* Common DP registers */
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
};

struct dce110_link_encoder {
	struct link_encoder base;
	const struct dce110_link_enc_registers *link_regs;
	const struct dce110_link_enc_aux_registers *aux_regs;
	const struct dce110_link_enc_hpd_registers *hpd_regs;
};


void dce110_link_encoder_construct(
	struct dce110_link_encoder *enc110,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dce110_link_enc_registers *link_regs,
	const struct dce110_link_enc_aux_registers *aux_regs,
	const struct dce110_link_enc_hpd_registers *hpd_regs);

bool dce110_link_encoder_validate_dvi_output(
	const struct dce110_link_encoder *enc110,
	enum signal_type connector_signal,
	enum signal_type signal,
	const struct dc_crtc_timing *crtc_timing);

bool dce110_link_encoder_validate_rgb_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing);

bool dce110_link_encoder_validate_dp_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing);

bool dce110_link_encoder_validate_wireless_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing);

bool dce110_link_encoder_validate_output_with_stream(
	struct link_encoder *enc,
	const struct dc_stream_state *stream);

/****************** HW programming ************************/

/* initialize HW */  /* why do we initialze aux in here? */
void dce110_link_encoder_hw_init(struct link_encoder *enc);

void dce110_link_encoder_destroy(struct link_encoder **enc);

/* program DIG_MODE in DIG_BE */
/* TODO can this be combined with enable_output? */
void dce110_link_encoder_setup(
	struct link_encoder *enc,
	enum signal_type signal);

/* enables TMDS PHY output */
/* TODO: still need depth or just pass in adjusted pixel clock? */
void dce110_link_encoder_enable_tmds_output(
	struct link_encoder *enc,
	enum clock_source_id clock_source,
	enum dc_color_depth color_depth,
	enum signal_type signal,
	uint32_t pixel_clock);

/* enables DP PHY output */
void dce110_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source);

/* enables DP PHY output in MST mode */
void dce110_link_encoder_enable_dp_mst_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source);

/* disable PHY output */
void dce110_link_encoder_disable_output(
	struct link_encoder *enc,
	enum signal_type signal);

/* set DP lane settings */
void dce110_link_encoder_dp_set_lane_settings(
	struct link_encoder *enc,
	const struct link_training_settings *link_settings);

void dce110_link_encoder_dp_set_phy_pattern(
	struct link_encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param);

/* programs DP MST VC payload allocation */
void dce110_link_encoder_update_mst_stream_allocation_table(
	struct link_encoder *enc,
	const struct link_mst_stream_allocation_table *table);

void dce110_link_encoder_connect_dig_be_to_fe(
	struct link_encoder *enc,
	enum engine_id engine,
	bool connect);

void dce110_link_encoder_set_dp_phy_pattern_training_pattern(
	struct link_encoder *enc,
	uint32_t index);

void dce110_link_encoder_enable_hpd(struct link_encoder *enc);

void dce110_link_encoder_disable_hpd(struct link_encoder *enc);

void dce110_psr_program_dp_dphy_fast_training(struct link_encoder *enc,
			bool exit_link_training_required);

void dce110_psr_program_secondary_packet(struct link_encoder *enc,
			unsigned int sdp_transmit_line_num_deadline);

#endif /* __DC_LINK_ENCODER__DCE110_H__ */
