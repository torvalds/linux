// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dc_bios_types.h"
#include "dcn30/dcn30_hpo_frl_link_encoder.h"
#include "dcn60_hpo_frl_link_encoder.h"
#include "reg_helper.h"
#include "dcn10/dcn10_link_encoder.h"

#define DC_LOGGER enc3->base.ctx->logger

#define REG(reg) (enc3->regs->reg)

#undef FN
#define FN(reg_name, field_name) enc3->hpo_le_shift->field_name, enc3->hpo_le_mask->field_name

#define CTX enc3->base.ctx

static enum bp_result link_transmitter_control(struct dcn10_link_encoder *enc10,
					       struct bp_transmitter_control *cntl)
{
	struct dc_bios *bp = enc10->base.ctx->dc_bios;

	return bp->funcs->transmitter_control(bp, cntl);
}

static void hpo_frl_link_enc60_enable_phy_output(struct hpo_frl_link_encoder *hpo_enc,
						struct link_encoder *enc,
						enum clock_source_id clock_source,
						enum hdmi_frl_link_rate frl_link_rate)
{
	struct dcn30_hpo_frl_link_encoder *enc3 = DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(hpo_enc);
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */
	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = enc->preferred_engine;
	cntl.transmitter = enc10->base.transmitter;
	cntl.pll_id = clock_source;
	cntl.signal = SIGNAL_TYPE_HDMI_FRL;
	cntl.hpd_sel = enc10->base.hpd_source;

	switch (frl_link_rate) {
	case HDMI_FRL_LINK_RATE_3GBPS:
		cntl.pixel_clock = 166667;
		break;
	case HDMI_FRL_LINK_RATE_6GBPS:
	case HDMI_FRL_LINK_RATE_6GBPS_4LANE:
		cntl.pixel_clock = 333333;
		break;
	case HDMI_FRL_LINK_RATE_8GBPS:
		cntl.pixel_clock = 444444;
		break;
	case HDMI_FRL_LINK_RATE_10GBPS:
		cntl.pixel_clock = 555555;
		break;
	case HDMI_FRL_LINK_RATE_12GBPS:
		cntl.pixel_clock = 666667;
		break;
	case HDMI_FRL_LINK_RATE_16GBPS:
		cntl.pixel_clock = 888889;
		break;
	case HDMI_FRL_LINK_RATE_20GBPS:
	default:
		cntl.pixel_clock = 1111111;
		break;
	}

	cntl.hpo_engine_id = enc3->base.inst + ENGINE_ID_HPO_0;

	if (frl_link_rate <= HDMI_FRL_LINK_RATE_6GBPS)
		cntl.lanes_number = 3;
	else
		cntl.lanes_number = 4;

	result = link_transmitter_control(enc10, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_HDMI_FRL("%s: Failed to execute VBIOS command table!\n", __func__);
		BREAK_TO_DEBUGGER();
	}
}

static struct hpo_frl_link_encoder_funcs dcn60_hpo_frl_link_encoder_funcs = {
	.setup_link_encoder = hpo_frl_link_enc3_setup_link_encoder,
	.set_hdmi_training_pattern = hpo_frl_link_enc3_set_training_pattern,
	.get_hdmi_training_pattern = hpo_frl_link_enc3_get_training_pattern,
	.enable_frl_phy_output = hpo_frl_link_enc60_enable_phy_output,
	.enable_output = hpo_frl_link_enc3_enable_output,
	.disable_link_encoder = hpo_frl_link_enc3_disable,
	.read_state = hpo_frl_link_enc3_read_state,
	.destroy = hpo_frl_link_enc3_destroy,
	.apply_vsdb_rcc_wa = hpo_frl_link_enc3_apply_vsdb_rcc_wa
};

void hpo_frl_link_encoder60_construct(struct dcn30_hpo_frl_link_encoder *enc3,
				     struct dc_context *ctx,
				     uint32_t inst,
				     const struct dcn30_hpo_frl_link_encoder_registers *hpo_le_regs,
				     const struct dcn30_hpo_frl_link_encoder_shift *hpo_le_shift,
				     const struct dcn30_hpo_frl_link_encoder_mask *hpo_le_mask)
{
	enc3->base.ctx = ctx;

	enc3->base.inst = inst;
	enc3->base.funcs = &dcn60_hpo_frl_link_encoder_funcs;

	enc3->regs = hpo_le_regs;
	enc3->hpo_le_shift = hpo_le_shift;
	enc3->hpo_le_mask = hpo_le_mask;
}
