// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dc_bios_types.h"
#include "dcn31/dcn31_hpo_dp_link_encoder.h"
#include "dcn32/dcn32_hpo_dp_link_encoder.h"
#include "dcn42_hpo_dp_link_encoder.h"
#include "reg_helper.h"
#include "stream_encoder.h"

#define DC_LOGGER \
		enc3->base.ctx->logger

#define REG(reg)\
	(enc3->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc3->hpo_le_shift->field_name, enc3->hpo_le_mask->field_name


#define CTX \
	enc3->base.ctx


static void dcn42_hpo_dp_link_enc_read_state(
		struct hpo_dp_link_encoder *enc,
		struct hpo_dp_link_enc_state *state)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);

	ASSERT(state);

	REG_GET(DP_DPHY_SYM32_STATUS,
			STATUS, &state->link_enc_enabled);
	REG_GET(DP_DPHY_SYM32_CONTROL,
			NUM_LANES, &state->lane_count);
	REG_GET(DP_DPHY_SYM32_CONTROL,
			MODE, (uint32_t *)&state->link_mode);

	REG_GET_2(DP_DPHY_SYM32_SAT_VC0,
			SAT_STREAM_SOURCE, &state->stream_src[0],
			SAT_SLOT_COUNT, &state->slot_count[0]);
	REG_GET_2(DP_DPHY_SYM32_SAT_VC1,
			SAT_STREAM_SOURCE, &state->stream_src[1],
			SAT_SLOT_COUNT, &state->slot_count[1]);
	REG_GET_2(DP_DPHY_SYM32_SAT_VC2,
			SAT_STREAM_SOURCE, &state->stream_src[2],
			SAT_SLOT_COUNT, &state->slot_count[2]);

	REG_GET_2(DP_DPHY_SYM32_VC_RATE_CNTL0,
			STREAM_VC_RATE_X, &state->vc_rate_x[0],
			STREAM_VC_RATE_Y, &state->vc_rate_y[0]);
	REG_GET_2(DP_DPHY_SYM32_VC_RATE_CNTL1,
			STREAM_VC_RATE_X, &state->vc_rate_x[1],
			STREAM_VC_RATE_Y, &state->vc_rate_y[1]);
	REG_GET_2(DP_DPHY_SYM32_VC_RATE_CNTL2,
			STREAM_VC_RATE_X, &state->vc_rate_x[2],
			STREAM_VC_RATE_Y, &state->vc_rate_y[2]);
}

static struct hpo_dp_link_encoder_funcs dcn42_hpo_dp_link_encoder_funcs = {
	.enable_link_phy = dcn31_hpo_dp_link_enc_enable_dp_output,
	.disable_link_phy = dcn31_hpo_dp_link_enc_disable_output,
	.link_enable = dcn31_hpo_dp_link_enc_enable,
	.link_disable = dcn31_hpo_dp_link_enc_disable,
	.set_link_test_pattern = dcn31_hpo_dp_link_enc_set_link_test_pattern,
	.update_stream_allocation_table = dcn31_hpo_dp_link_enc_update_stream_allocation_table,
	.set_throttled_vcp_size = dcn31_hpo_dp_link_enc_set_throttled_vcp_size,
	.is_in_alt_mode = dcn32_hpo_dp_link_enc_is_in_alt_mode,
	.read_state = dcn42_hpo_dp_link_enc_read_state,
	.set_ffe = dcn31_hpo_dp_link_enc_set_ffe,
};

void hpo_dp_link_encoder42_construct(struct dcn31_hpo_dp_link_encoder *enc31,
		struct dc_context *ctx,
		uint32_t inst,
		const struct dcn31_hpo_dp_link_encoder_registers *hpo_le_regs,
		const struct dcn31_hpo_dp_link_encoder_shift *hpo_le_shift,
		const struct dcn31_hpo_dp_link_encoder_mask *hpo_le_mask)
{
	enc31->base.ctx = ctx;

	enc31->base.inst = inst;
	enc31->base.funcs = &dcn42_hpo_dp_link_encoder_funcs;
	enc31->base.hpd_source = HPD_SOURCEID_UNKNOWN;
	enc31->base.transmitter = TRANSMITTER_UNKNOWN;

	enc31->regs = hpo_le_regs;
	enc31->hpo_le_shift = hpo_le_shift;
	enc31->hpo_le_mask = hpo_le_mask;
}
