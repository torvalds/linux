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

#ifndef __DAL_DCN30_HPO_FRL_LINK_ENCODER_H__
#define __DAL_DCN30_HPO_FRL_LINK_ENCODER_H__

#include "link_encoder.h"


#define DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(hpo_frl_link_encoder)\
	container_of(hpo_frl_link_encoder, struct dcn30_hpo_frl_link_encoder, base)


#define DCN3_0_HPO_FRL_LINK_ENC_REG_LIST(id) \
	SR(HDMI_LINK_ENC_CLK_CTRL), \
	SR(HDMI_LINK_ENC_CONTROL), \
	SR(HDMI_FRL_ENC_CONFIG), \
	SR(HDMI_FRL_ENC_CONFIG2),\
	SR(HDMI_FRL_ENC_MEM_CTRL)

struct dcn30_hpo_frl_link_encoder_registers {
	uint32_t HDMI_LINK_ENC_CLK_CTRL;
	uint32_t HDMI_LINK_ENC_CONTROL;
	uint32_t HDMI_FRL_ENC_CONFIG;
	uint32_t HDMI_FRL_ENC_CONFIG2;
	uint32_t HDMI_FRL_ENC_MEM_CTRL;
};

#define DCN3_0_HPO_FRL_LINK_ENC_MASK_SH_LIST(mask_sh)\
	SE_SF(HDMI_LINK_ENC_CLK_CTRL, HDMI_LINK_ENC_CLOCK_EN, mask_sh),\
	SE_SF(HDMI_LINK_ENC_CONTROL, HDMI_LINK_ENC_ENABLE, mask_sh),\
	SE_SF(HDMI_LINK_ENC_CONTROL, HDMI_LINK_ENC_SOFT_RESET, mask_sh),\
	SE_SF(HDMI_FRL_ENC_MEM_CTRL, METERBUFFER_MEM_PWR_DIS, mask_sh),\
	SE_SF(HDMI_FRL_ENC_MEM_CTRL, METERBUFFER_MEM_PWR_FORCE, mask_sh),\
	SE_SF(HDMI_FRL_ENC_MEM_CTRL, METERBUFFER_MEM_PWR_STATE, mask_sh),\
	SE_SF(HDMI_FRL_ENC_MEM_CTRL, METERBUFFER_MEM_DEFAULT_MEM_LOW_POWER_STATE, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG, HDMI_LINK_LANE_COUNT, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG, HDMI_LINK_TRAINING_ENABLE, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG, HDMI_LINK_LANE0_TRAINING_PATTERN, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG, HDMI_LINK_LANE1_TRAINING_PATTERN, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG, HDMI_LINK_LANE2_TRAINING_PATTERN, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG, HDMI_LINK_LANE3_TRAINING_PATTERN, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG2, HDMI_LINK_MAX_JITTER_VALUE, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG2, HDMI_LINK_JITTER_THRESHOLD, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG2, HDMI_LINK_JITTER_CAL_EN, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG2, HDMI_LINK_RC_COMPRESS_DISABLE, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG2, HDMI_FRL_HDMISTREAMCLK_DB_SEL, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG2, HDMI_LINK_MAX_JITTER_VALUE_RESET, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG2, HDMI_LINK_JITTER_EXCEED_STATUS, mask_sh),\
	SE_SF(HDMI_FRL_ENC_CONFIG2, HDMI_LINK_METER_BUFFER_OVERFLOW_STATUS, mask_sh)

#define HPO_FRL_LINK_ENC_DCN3_REG_FIELD_LIST(type) \
	type HDMI_LINK_ENC_CLOCK_EN;\
	type HDMI_LINK_ENC_ENABLE;\
	type HDMI_LINK_ENC_SOFT_RESET;\
	type HDMI_LINK_LANE_COUNT;\
	type HDMI_LINK_TRAINING_ENABLE;\
	type HDMI_LINK_LANE0_TRAINING_PATTERN;\
	type HDMI_LINK_LANE1_TRAINING_PATTERN;\
	type HDMI_LINK_LANE2_TRAINING_PATTERN;\
	type HDMI_LINK_LANE3_TRAINING_PATTERN;\
	type HDMI_LINK_MAX_JITTER_VALUE;\
	type HDMI_LINK_JITTER_THRESHOLD;\
	type HDMI_LINK_JITTER_CAL_EN;\
	type HDMI_LINK_RC_COMPRESS_DISABLE;\
	type METERBUFFER_MEM_PWR_DIS;\
	type METERBUFFER_MEM_PWR_STATE;\
	type METERBUFFER_MEM_PWR_FORCE;\
	type METERBUFFER_MEM_DEFAULT_MEM_LOW_POWER_STATE;\
	type HDMI_FRL_HDMISTREAMCLK_DB_SEL;\
	type HDMI_LINK_MAX_JITTER_VALUE_RESET;\
	type HDMI_LINK_JITTER_EXCEED_STATUS;\
	type HDMI_LINK_METER_BUFFER_OVERFLOW_STATUS


struct dcn30_hpo_frl_link_encoder_shift {
	HPO_FRL_LINK_ENC_DCN3_REG_FIELD_LIST(uint8_t);
};

struct dcn30_hpo_frl_link_encoder_mask {
	HPO_FRL_LINK_ENC_DCN3_REG_FIELD_LIST(uint32_t);
};

struct dcn30_hpo_frl_link_encoder {
	struct hpo_frl_link_encoder base;
	const struct dcn30_hpo_frl_link_encoder_registers *regs;
	const struct dcn30_hpo_frl_link_encoder_shift *hpo_le_shift;
	const struct dcn30_hpo_frl_link_encoder_mask *hpo_le_mask;
};

void hpo_frl_link_enc3_setup_link_encoder(struct hpo_frl_link_encoder *enc,
						 int lane_count);

void hpo_frl_link_enc3_set_training_pattern(struct hpo_frl_link_encoder *enc,
						uint32_t lane0_pattern,
						uint32_t lane1_pattern,
						uint32_t lane2_pattern,
						uint32_t lane3_pattern);

void hpo_frl_link_enc3_get_training_pattern(struct hpo_frl_link_encoder *enc,
						uint32_t *lane0_pattern,
						uint32_t *lane1_pattern,
						uint32_t *lane2_pattern,
						uint32_t *lane3_pattern);

void hpo_frl_link_enc3_enable_output(struct hpo_frl_link_encoder *enc);

void hpo_frl_link_enc3_disable(struct hpo_frl_link_encoder *enc);

void hpo_frl_link_enc3_read_state(struct hpo_frl_link_encoder *enc,
					 struct hpo_frl_link_enc_state *state);

void hpo_frl_link_enc3_destroy(struct hpo_frl_link_encoder **enc);

void hpo_frl_link_enc3_apply_vsdb_rcc_wa(struct hpo_frl_link_encoder *enc);

void hpo_frl_link_encoder3_construct(struct dcn30_hpo_frl_link_encoder *enc3,
				     struct dc_context *ctx,
				     uint32_t inst,
				     const struct dcn30_hpo_frl_link_encoder_registers *hpo_le_regs,
				     const struct dcn30_hpo_frl_link_encoder_shift *hpo_le_shift,
				     const struct dcn30_hpo_frl_link_encoder_mask *hpo_le_mask);

#endif
