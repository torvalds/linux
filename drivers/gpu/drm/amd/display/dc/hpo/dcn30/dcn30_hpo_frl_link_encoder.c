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


#include "core_types.h"
#include "dc_bios_types.h"
#include "dcn30_hpo_frl_link_encoder.h"
#include "reg_helper.h"
#include "dcn10/dcn10_link_encoder.h"

#define DC_LOGGER enc3->base.ctx->logger

#define REG(reg) (enc3->regs->reg)

#undef FN
#define FN(reg_name, field_name) enc3->hpo_le_shift->field_name, enc3->hpo_le_mask->field_name

#define CTX enc3->base.ctx

void hpo_frl_link_enc3_setup_link_encoder(struct hpo_frl_link_encoder *enc,
						 int lane_count)
{
	struct dcn30_hpo_frl_link_encoder *enc3 = DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(enc);

	DC_LOG_DEBUG("Entering [%s]\n", __func__);

	if (enc->ctx->dc->caps.ips_v2_support) {
		REG_UPDATE(HDMI_FRL_ENC_MEM_CTRL,
			METERBUFFER_MEM_PWR_DIS, 1);
		REG_WAIT(HDMI_FRL_ENC_MEM_CTRL,	METERBUFFER_MEM_PWR_STATE, 0, 1, 100);
	}
	/* Enable Link encoder clock */
	REG_UPDATE(HDMI_LINK_ENC_CLK_CTRL,
		   HDMI_LINK_ENC_CLOCK_EN, 1);

	/* Configure lane count of FRL encoder */
	REG_UPDATE(HDMI_FRL_ENC_CONFIG,
		   HDMI_LINK_LANE_COUNT, lane_count == 3 ? 0 : 1);

	/* Reset link encoder */
	REG_UPDATE_2(HDMI_LINK_ENC_CONTROL,
		     HDMI_LINK_ENC_ENABLE, 0,
		     HDMI_LINK_ENC_SOFT_RESET, 1);

	REG_UPDATE(HDMI_LINK_ENC_CONTROL,
		   HDMI_LINK_ENC_SOFT_RESET, 0);

	/* Enable link encoder */
	REG_UPDATE(HDMI_LINK_ENC_CONTROL,
		   HDMI_LINK_ENC_ENABLE, 1);

	DC_LOG_HDMI_FRL("Exiting [%s]\n", __func__);
}

void hpo_frl_link_enc3_set_training_pattern(struct hpo_frl_link_encoder *enc,
						   uint32_t lane0_pattern,
						   uint32_t lane1_pattern,
						   uint32_t lane2_pattern,
						   uint32_t lane3_pattern)
{
	struct dcn30_hpo_frl_link_encoder *enc3 = DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(enc);

	/* Configure lane count of FRL encoder */
	REG_UPDATE(HDMI_FRL_ENC_CONFIG,
		   HDMI_LINK_TRAINING_ENABLE, 1);

	if (lane0_pattern < 8)
		REG_UPDATE(HDMI_FRL_ENC_CONFIG,
			   HDMI_LINK_LANE0_TRAINING_PATTERN, lane0_pattern);

	if (lane1_pattern < 8)
		REG_UPDATE(HDMI_FRL_ENC_CONFIG,
			   HDMI_LINK_LANE1_TRAINING_PATTERN, lane1_pattern);

	if (lane2_pattern < 8)
		REG_UPDATE(HDMI_FRL_ENC_CONFIG,
			   HDMI_LINK_LANE2_TRAINING_PATTERN, lane2_pattern);

	if (lane3_pattern < 8)
		REG_UPDATE(HDMI_FRL_ENC_CONFIG,
			   HDMI_LINK_LANE3_TRAINING_PATTERN, lane3_pattern);
}

void hpo_frl_link_enc3_get_training_pattern(struct hpo_frl_link_encoder *enc,
						   uint32_t *lane0_pattern,
						   uint32_t *lane1_pattern,
						   uint32_t *lane2_pattern,
						   uint32_t *lane3_pattern)
{
	struct dcn30_hpo_frl_link_encoder *enc3 = DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(enc);

	/* Configure lane count of FRL encoder */
	REG_GET_4(HDMI_FRL_ENC_CONFIG,
		  HDMI_LINK_LANE0_TRAINING_PATTERN, lane0_pattern,
		  HDMI_LINK_LANE1_TRAINING_PATTERN, lane1_pattern,
		  HDMI_LINK_LANE2_TRAINING_PATTERN, lane2_pattern,
		  HDMI_LINK_LANE3_TRAINING_PATTERN, lane3_pattern);
}

static enum bp_result link_transmitter_control(struct dcn10_link_encoder *enc10,
					       struct bp_transmitter_control *cntl)
{
	struct dc_bios *bp = enc10->base.ctx->dc_bios;

	return bp->funcs->transmitter_control(bp, cntl);
}

static void hpo_frl_link_enc3_enable_phy_output(struct hpo_frl_link_encoder *hpo_enc,
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
	default:
		cntl.pixel_clock = 666667;
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

void hpo_frl_link_enc3_enable_output(struct hpo_frl_link_encoder *enc)
{
	struct dcn30_hpo_frl_link_encoder *enc3 = DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(enc);

	DC_LOG_HDMI_FRL("Entering [%s]\n", __func__);

	/* Enable FRL packet transmission */
	REG_UPDATE(HDMI_FRL_ENC_CONFIG,
		   HDMI_LINK_TRAINING_ENABLE, 0);
	DC_LOG_HDMI_FRL("Exiting [%s]\n", __func__);
}

void hpo_frl_link_enc3_disable(struct hpo_frl_link_encoder *enc)
{
	struct dcn30_hpo_frl_link_encoder *enc3 = DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(enc);

	DC_LOG_HDMI_FRL("Entering [%s]\n", __func__);

	REG_UPDATE_5(HDMI_FRL_ENC_CONFIG,
		     HDMI_LINK_TRAINING_ENABLE, 1,
		     HDMI_LINK_LANE0_TRAINING_PATTERN, 0,
		     HDMI_LINK_LANE1_TRAINING_PATTERN, 0,
		     HDMI_LINK_LANE2_TRAINING_PATTERN, 0,
		     HDMI_LINK_LANE3_TRAINING_PATTERN, 0);

	/* Disable link encoder */
	REG_UPDATE(HDMI_LINK_ENC_CONTROL,
		   HDMI_LINK_ENC_ENABLE, 0);

	/* Disable Link encoder clock */
	REG_UPDATE(HDMI_LINK_ENC_CLK_CTRL,
		   HDMI_LINK_ENC_CLOCK_EN, 0);
	REG_UPDATE(HDMI_FRL_ENC_CONFIG2,
		   HDMI_LINK_RC_COMPRESS_DISABLE, 0);

	DC_LOG_HDMI_FRL("Exiting [%s]\n", __func__);
}

void hpo_frl_link_enc3_read_state(struct hpo_frl_link_encoder *enc,
					 struct hpo_frl_link_enc_state *state)
{
	struct dcn30_hpo_frl_link_encoder *enc3 = DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(enc);
	unsigned int link_training_enabled;
	unsigned int lane_count_field;

	ASSERT(state);
	REG_GET(HDMI_LINK_ENC_CONTROL,
		HDMI_LINK_ENC_ENABLE, &state->link_enc_enabled);

	REG_GET_2(HDMI_FRL_ENC_CONFIG,
		  HDMI_LINK_TRAINING_ENABLE, &link_training_enabled,
			  HDMI_LINK_LANE_COUNT, &lane_count_field);

	state->link_active = link_training_enabled == 1;

	if (lane_count_field == 1)
		state->lane_count = 4;
	else
		state->lane_count = 3;
}

void hpo_frl_link_enc3_destroy(struct hpo_frl_link_encoder **enc)
{
	kfree(DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(*enc));
	*enc = NULL;
}

void hpo_frl_link_enc3_apply_vsdb_rcc_wa(struct hpo_frl_link_encoder *enc)
{
	struct dcn30_hpo_frl_link_encoder *enc3 =
			DCN30_HPO_FRL_LINK_ENC_FROM_HPO_FRL_LINK_ENC(enc);

	REG_UPDATE(HDMI_FRL_ENC_CONFIG2,
			HDMI_LINK_RC_COMPRESS_DISABLE, 1);
}

static struct hpo_frl_link_encoder_funcs dcn30_hpo_frl_link_encoder_funcs = {
	.setup_link_encoder = hpo_frl_link_enc3_setup_link_encoder,
	.set_hdmi_training_pattern = hpo_frl_link_enc3_set_training_pattern,
	.get_hdmi_training_pattern = hpo_frl_link_enc3_get_training_pattern,
	.enable_frl_phy_output = hpo_frl_link_enc3_enable_phy_output,
	.enable_output = hpo_frl_link_enc3_enable_output,
	.disable_link_encoder = hpo_frl_link_enc3_disable,
	.read_state = hpo_frl_link_enc3_read_state,
	.destroy = hpo_frl_link_enc3_destroy,
	.apply_vsdb_rcc_wa = hpo_frl_link_enc3_apply_vsdb_rcc_wa
};

void hpo_frl_link_encoder3_construct(struct dcn30_hpo_frl_link_encoder *enc3,
				     struct dc_context *ctx,
				     uint32_t inst,
				     const struct dcn30_hpo_frl_link_encoder_registers *hpo_le_regs,
				     const struct dcn30_hpo_frl_link_encoder_shift *hpo_le_shift,
				     const struct dcn30_hpo_frl_link_encoder_mask *hpo_le_mask)
{
	enc3->base.ctx = ctx;

	enc3->base.inst = inst;
	enc3->base.funcs = &dcn30_hpo_frl_link_encoder_funcs;

	enc3->regs = hpo_le_regs;
	enc3->hpo_le_shift = hpo_le_shift;
	enc3->hpo_le_mask = hpo_le_mask;
}
