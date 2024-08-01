/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include "dc_bios_types.h"
#include "dcn31/dcn31_hpo_dp_link_encoder.h"
#include "dcn32_hpo_dp_link_encoder.h"
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

static bool dcn32_hpo_dp_link_enc_is_in_alt_mode(
		struct hpo_dp_link_encoder *enc)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	uint32_t dp_alt_mode_disable = 0;

	ASSERT((enc->transmitter >= TRANSMITTER_UNIPHY_A) && (enc->transmitter <= TRANSMITTER_UNIPHY_E));

	/* if value == 1 alt mode is disabled, otherwise it is enabled */
	REG_GET(RDPCSTX_PHY_CNTL6[enc->transmitter], RDPCS_PHY_DPALT_DISABLE, &dp_alt_mode_disable);
	return (dp_alt_mode_disable == 0);
}



static struct hpo_dp_link_encoder_funcs dcn32_hpo_dp_link_encoder_funcs = {
	.enable_link_phy = dcn31_hpo_dp_link_enc_enable_dp_output,
	.disable_link_phy = dcn31_hpo_dp_link_enc_disable_output,
	.link_enable = dcn31_hpo_dp_link_enc_enable,
	.link_disable = dcn31_hpo_dp_link_enc_disable,
	.set_link_test_pattern = dcn31_hpo_dp_link_enc_set_link_test_pattern,
	.update_stream_allocation_table = dcn31_hpo_dp_link_enc_update_stream_allocation_table,
	.set_throttled_vcp_size = dcn31_hpo_dp_link_enc_set_throttled_vcp_size,
	.is_in_alt_mode = dcn32_hpo_dp_link_enc_is_in_alt_mode,
	.read_state = dcn31_hpo_dp_link_enc_read_state,
	.set_ffe = dcn31_hpo_dp_link_enc_set_ffe,
};

void hpo_dp_link_encoder32_construct(struct dcn31_hpo_dp_link_encoder *enc31,
		struct dc_context *ctx,
		uint32_t inst,
		const struct dcn31_hpo_dp_link_encoder_registers *hpo_le_regs,
		const struct dcn31_hpo_dp_link_encoder_shift *hpo_le_shift,
		const struct dcn31_hpo_dp_link_encoder_mask *hpo_le_mask)
{
	enc31->base.ctx = ctx;

	enc31->base.inst = inst;
	enc31->base.funcs = &dcn32_hpo_dp_link_encoder_funcs;
	enc31->base.hpd_source = HPD_SOURCEID_UNKNOWN;
	enc31->base.transmitter = TRANSMITTER_UNKNOWN;

	enc31->regs = hpo_le_regs;
	enc31->hpo_le_shift = hpo_le_shift;
	enc31->hpo_le_mask = hpo_le_mask;
}
