/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef __DAL_DCN31_HPO_DP_LINK_ENCODER_H__
#define __DAL_DCN31_HPO_DP_LINK_ENCODER_H__

#include "link_encoder.h"


#define DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(hpo_dp_link_encoder)\
	container_of(hpo_dp_link_encoder, struct dcn31_hpo_dp_link_encoder, base)


#define DCN3_1_HPO_DP_LINK_ENC_REG_LIST(id) \
	SRI(DP_LINK_ENC_CLOCK_CONTROL, DP_LINK_ENC, id), \
	SRI(DP_DPHY_SYM32_CONTROL, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_STATUS, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CONFIG, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_PRBS_SEED0, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_PRBS_SEED1, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_PRBS_SEED2, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_PRBS_SEED3, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_SQ_PULSE, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM0, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM1, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM2, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM3, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM4, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM5, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM6, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM7, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM8, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM9, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_TP_CUSTOM10, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_SAT_VC0, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_SAT_VC1, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_SAT_VC2, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_SAT_VC3, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_VC_RATE_CNTL0, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_VC_RATE_CNTL1, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_VC_RATE_CNTL2, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_VC_RATE_CNTL3, DP_DPHY_SYM32, id), \
	SRI(DP_DPHY_SYM32_SAT_UPDATE, DP_DPHY_SYM32, id)

#define DCN3_1_RDPCSTX_REG_LIST(id) \
	SRII(RDPCSTX_PHY_CNTL6, RDPCSTX, id)


#define DCN3_1_HPO_DP_LINK_ENC_REGS \
	uint32_t DP_LINK_ENC_CLOCK_CONTROL;\
	uint32_t DP_DPHY_SYM32_CONTROL;\
	uint32_t DP_DPHY_SYM32_STATUS;\
	uint32_t DP_DPHY_SYM32_TP_CONFIG;\
	uint32_t DP_DPHY_SYM32_TP_PRBS_SEED0;\
	uint32_t DP_DPHY_SYM32_TP_PRBS_SEED1;\
	uint32_t DP_DPHY_SYM32_TP_PRBS_SEED2;\
	uint32_t DP_DPHY_SYM32_TP_PRBS_SEED3;\
	uint32_t DP_DPHY_SYM32_TP_SQ_PULSE;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM0;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM1;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM2;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM3;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM4;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM5;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM6;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM7;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM8;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM9;\
	uint32_t DP_DPHY_SYM32_TP_CUSTOM10;\
	uint32_t DP_DPHY_SYM32_SAT_VC0;\
	uint32_t DP_DPHY_SYM32_SAT_VC1;\
	uint32_t DP_DPHY_SYM32_SAT_VC2;\
	uint32_t DP_DPHY_SYM32_SAT_VC3;\
	uint32_t DP_DPHY_SYM32_VC_RATE_CNTL0;\
	uint32_t DP_DPHY_SYM32_VC_RATE_CNTL1;\
	uint32_t DP_DPHY_SYM32_VC_RATE_CNTL2;\
	uint32_t DP_DPHY_SYM32_VC_RATE_CNTL3;\
	uint32_t DP_DPHY_SYM32_SAT_UPDATE

struct dcn31_hpo_dp_link_encoder_registers {
	DCN3_1_HPO_DP_LINK_ENC_REGS;
	uint32_t RDPCSTX_PHY_CNTL6[5];
};

#define DCN3_1_HPO_DP_LINK_ENC_RDPCSTX_MASK_SH_LIST(mask_sh)\
	SE_SF(RDPCSTX0_RDPCSTX_PHY_CNTL6, RDPCS_PHY_DPALT_DISABLE, mask_sh)

#define DCN3_1_HPO_DP_LINK_ENC_COMMON_MASK_SH_LIST(mask_sh)\
	SE_SF(DP_LINK_ENC0_DP_LINK_ENC_CLOCK_CONTROL, DP_LINK_ENC_CLOCK_EN, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_CONTROL, DPHY_RESET, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_CONTROL, DPHY_ENABLE, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_CONTROL, PRECODER_ENABLE, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_CONTROL, MODE, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_CONTROL, NUM_LANES, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_STATUS, STATUS, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_STATUS, SAT_UPDATE_PENDING, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_STATUS, RATE_UPDATE_PENDING, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CUSTOM0, TP_CUSTOM, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CONFIG, TP_SELECT0, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CONFIG, TP_SELECT1, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CONFIG, TP_SELECT2, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CONFIG, TP_SELECT3, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CONFIG, TP_PRBS_SEL0, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CONFIG, TP_PRBS_SEL1, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CONFIG, TP_PRBS_SEL2, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_CONFIG, TP_PRBS_SEL3, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_TP_SQ_PULSE, TP_SQ_PULSE_WIDTH, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_SAT_VC0, SAT_STREAM_SOURCE, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_SAT_VC0, SAT_SLOT_COUNT, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_VC_RATE_CNTL0, STREAM_VC_RATE_X, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_VC_RATE_CNTL0, STREAM_VC_RATE_Y, mask_sh),\
	SE_SF(DP_DPHY_SYM320_DP_DPHY_SYM32_SAT_UPDATE, SAT_UPDATE, mask_sh)

#define DCN3_1_HPO_DP_LINK_ENC_MASK_SH_LIST(mask_sh)\
	DCN3_1_HPO_DP_LINK_ENC_COMMON_MASK_SH_LIST(mask_sh),\
	DCN3_1_HPO_DP_LINK_ENC_RDPCSTX_MASK_SH_LIST(mask_sh)\

#define DCN3_1_HPO_DP_LINK_ENC_REG_FIELD_LIST(type) \
	type DP_LINK_ENC_CLOCK_EN;\
	type DPHY_RESET;\
	type DPHY_ENABLE;\
	type PRECODER_ENABLE;\
	type NUM_LANES;\
	type MODE;\
	type STATUS;\
	type SAT_UPDATE_PENDING;\
	type RATE_UPDATE_PENDING;\
	type TP_CUSTOM;\
	type TP_SELECT0;\
	type TP_SELECT1;\
	type TP_SELECT2;\
	type TP_SELECT3;\
	type TP_PRBS_SEL0;\
	type TP_PRBS_SEL1;\
	type TP_PRBS_SEL2;\
	type TP_PRBS_SEL3;\
	type TP_SQ_PULSE_WIDTH;\
	type SAT_STREAM_SOURCE;\
	type SAT_SLOT_COUNT;\
	type STREAM_VC_RATE_X;\
	type STREAM_VC_RATE_Y;\
	type SAT_UPDATE;\
	type RDPCS_PHY_DPALT_DISABLE


struct dcn31_hpo_dp_link_encoder_shift {
	DCN3_1_HPO_DP_LINK_ENC_REG_FIELD_LIST(uint8_t);
};

struct dcn31_hpo_dp_link_encoder_mask {
	DCN3_1_HPO_DP_LINK_ENC_REG_FIELD_LIST(uint32_t);
};

struct dcn31_hpo_dp_link_encoder {
	struct hpo_dp_link_encoder base;
	const struct dcn31_hpo_dp_link_encoder_registers *regs;
	const struct dcn31_hpo_dp_link_encoder_shift *hpo_le_shift;
	const struct dcn31_hpo_dp_link_encoder_mask *hpo_le_mask;
};

void hpo_dp_link_encoder31_construct(struct dcn31_hpo_dp_link_encoder *enc31,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn31_hpo_dp_link_encoder_registers *hpo_le_regs,
	const struct dcn31_hpo_dp_link_encoder_shift *hpo_le_shift,
	const struct dcn31_hpo_dp_link_encoder_mask *hpo_le_mask);

void dcn31_hpo_dp_link_enc_enable_dp_output(
	struct hpo_dp_link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum transmitter transmitter,
	enum hpd_source_id hpd_source);

void dcn31_hpo_dp_link_enc_disable_output(
	struct hpo_dp_link_encoder *enc,
	enum signal_type signal);

void dcn31_hpo_dp_link_enc_enable(
	struct hpo_dp_link_encoder *enc,
	enum dc_lane_count num_lanes);

void dcn31_hpo_dp_link_enc_disable(
	struct hpo_dp_link_encoder *enc);

void dcn31_hpo_dp_link_enc_set_link_test_pattern(
	struct hpo_dp_link_encoder *enc,
	struct encoder_set_dp_phy_pattern_param *tp_params);

void dcn31_hpo_dp_link_enc_update_stream_allocation_table(
	struct hpo_dp_link_encoder *enc,
	const struct link_mst_stream_allocation_table *table);

void dcn31_hpo_dp_link_enc_set_throttled_vcp_size(
	struct hpo_dp_link_encoder *enc,
	uint32_t stream_encoder_inst,
	struct fixed31_32 avg_time_slots_per_mtp);

void dcn31_hpo_dp_link_enc_read_state(
	struct hpo_dp_link_encoder *enc,
	struct hpo_dp_link_enc_state *state);

void dcn31_hpo_dp_link_enc_set_ffe(
	struct hpo_dp_link_encoder *enc,
	const struct dc_link_settings *link_settings,
	uint8_t ffe_preset);

#endif   // __DAL_DCN31_HPO_LINK_ENCODER_H__
