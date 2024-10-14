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
#include "dcn31_hpo_dp_link_encoder.h"
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

enum {
	DP_SAT_UPDATE_MAX_RETRY = 200
};

void dcn31_hpo_dp_link_enc_enable(
		struct hpo_dp_link_encoder *enc,
		enum dc_lane_count num_lanes)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	uint32_t dp_link_enabled;

	/* get current status of link enabled */
	REG_GET(DP_DPHY_SYM32_STATUS,
			STATUS, &dp_link_enabled);

	/* Enable clocks first */
	REG_UPDATE(DP_LINK_ENC_CLOCK_CONTROL, DP_LINK_ENC_CLOCK_EN, 1);

	/* Reset DPHY.  Only reset if going from disable to enable */
	if (!dp_link_enabled) {
		REG_UPDATE(DP_DPHY_SYM32_CONTROL, DPHY_RESET, 1);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL, DPHY_RESET, 0);
	}

	/* Configure DPHY settings */
	REG_UPDATE_3(DP_DPHY_SYM32_CONTROL,
			DPHY_ENABLE, 1,
			PRECODER_ENABLE, 1,
			NUM_LANES, num_lanes == LANE_COUNT_ONE ? 0 : num_lanes == LANE_COUNT_TWO ? 1 : 3);
}

void dcn31_hpo_dp_link_enc_disable(
		struct hpo_dp_link_encoder *enc)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);

	/* Configure DPHY settings */
	REG_UPDATE(DP_DPHY_SYM32_CONTROL,
			DPHY_ENABLE, 0);

	/* Shut down clock last */
	REG_UPDATE(DP_LINK_ENC_CLOCK_CONTROL, DP_LINK_ENC_CLOCK_EN, 0);
}

void dcn31_hpo_dp_link_enc_set_link_test_pattern(
		struct hpo_dp_link_encoder *enc,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	uint32_t tp_custom;

	switch (tp_params->dp_phy_pattern) {
	case DP_TEST_PATTERN_VIDEO_MODE:
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_LINK_ACTIVE);
		break;
	case DP_TEST_PATTERN_128b_132b_TPS1_TRAINING_MODE:
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_LINK_TRAINING_TPS1);
		break;
	case DP_TEST_PATTERN_128b_132b_TPS2_TRAINING_MODE:
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_LINK_TRAINING_TPS2);
		break;
	case DP_TEST_PATTERN_128b_132b_TPS1:
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_TPS1,
				TP_SELECT1, DP_DPHY_TP_SELECT_TPS1,
				TP_SELECT2, DP_DPHY_TP_SELECT_TPS1,
				TP_SELECT3, DP_DPHY_TP_SELECT_TPS1);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_128b_132b_TPS2:
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_TPS2,
				TP_SELECT1, DP_DPHY_TP_SELECT_TPS2,
				TP_SELECT2, DP_DPHY_TP_SELECT_TPS2,
				TP_SELECT3, DP_DPHY_TP_SELECT_TPS2);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_PRBS7:
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_PRBS_SEL0, DP_DPHY_TP_PRBS7,
				TP_PRBS_SEL1, DP_DPHY_TP_PRBS7,
				TP_PRBS_SEL2, DP_DPHY_TP_PRBS7,
				TP_PRBS_SEL3, DP_DPHY_TP_PRBS7);
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT1, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT2, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT3, DP_DPHY_TP_SELECT_PRBS);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_PRBS9:
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_PRBS_SEL0, DP_DPHY_TP_PRBS9,
				TP_PRBS_SEL1, DP_DPHY_TP_PRBS9,
				TP_PRBS_SEL2, DP_DPHY_TP_PRBS9,
				TP_PRBS_SEL3, DP_DPHY_TP_PRBS9);
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT1, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT2, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT3, DP_DPHY_TP_SELECT_PRBS);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_PRBS11:
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_PRBS_SEL0, DP_DPHY_TP_PRBS11,
				TP_PRBS_SEL1, DP_DPHY_TP_PRBS11,
				TP_PRBS_SEL2, DP_DPHY_TP_PRBS11,
				TP_PRBS_SEL3, DP_DPHY_TP_PRBS11);
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT1, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT2, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT3, DP_DPHY_TP_SELECT_PRBS);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_PRBS15:
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_PRBS_SEL0, DP_DPHY_TP_PRBS15,
				TP_PRBS_SEL1, DP_DPHY_TP_PRBS15,
				TP_PRBS_SEL2, DP_DPHY_TP_PRBS15,
				TP_PRBS_SEL3, DP_DPHY_TP_PRBS15);
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT1, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT2, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT3, DP_DPHY_TP_SELECT_PRBS);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_PRBS23:
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_PRBS_SEL0, DP_DPHY_TP_PRBS23,
				TP_PRBS_SEL1, DP_DPHY_TP_PRBS23,
				TP_PRBS_SEL2, DP_DPHY_TP_PRBS23,
				TP_PRBS_SEL3, DP_DPHY_TP_PRBS23);
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT1, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT2, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT3, DP_DPHY_TP_SELECT_PRBS);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_PRBS31:
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_PRBS_SEL0, DP_DPHY_TP_PRBS31,
				TP_PRBS_SEL1, DP_DPHY_TP_PRBS31,
				TP_PRBS_SEL2, DP_DPHY_TP_PRBS31,
				TP_PRBS_SEL3, DP_DPHY_TP_PRBS31);
		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT1, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT2, DP_DPHY_TP_SELECT_PRBS,
				TP_SELECT3, DP_DPHY_TP_SELECT_PRBS);
		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_264BIT_CUSTOM:
		tp_custom = (tp_params->custom_pattern[2] << 16) | (tp_params->custom_pattern[1] << 8) | tp_params->custom_pattern[0];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM0, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[5] << 16) | (tp_params->custom_pattern[4] << 8) | tp_params->custom_pattern[3];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM1, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[8] << 16) | (tp_params->custom_pattern[7] << 8) | tp_params->custom_pattern[6];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM2, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[11] << 16) | (tp_params->custom_pattern[10] << 8) | tp_params->custom_pattern[9];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM3, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[14] << 16) | (tp_params->custom_pattern[13] << 8) | tp_params->custom_pattern[12];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM4, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[17] << 16) | (tp_params->custom_pattern[16] << 8) | tp_params->custom_pattern[15];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM5, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[20] << 16) | (tp_params->custom_pattern[19] << 8) | tp_params->custom_pattern[18];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM6, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[23] << 16) | (tp_params->custom_pattern[22] << 8) | tp_params->custom_pattern[21];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM7, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[26] << 16) | (tp_params->custom_pattern[25] << 8) | tp_params->custom_pattern[24];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM8, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[29] << 16) | (tp_params->custom_pattern[28] << 8) | tp_params->custom_pattern[27];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM9, 0, TP_CUSTOM, tp_custom);
		tp_custom = (tp_params->custom_pattern[32] << 16) | (tp_params->custom_pattern[31] << 8) | tp_params->custom_pattern[30];
		REG_SET(DP_DPHY_SYM32_TP_CUSTOM10, 0, TP_CUSTOM, tp_custom);

		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_CUSTOM,
				TP_SELECT1, DP_DPHY_TP_SELECT_CUSTOM,
				TP_SELECT2, DP_DPHY_TP_SELECT_CUSTOM,
				TP_SELECT3, DP_DPHY_TP_SELECT_CUSTOM);

		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	case DP_TEST_PATTERN_SQUARE:
	case DP_TEST_PATTERN_SQUARE_PRESHOOT_DISABLED:
	case DP_TEST_PATTERN_SQUARE_DEEMPHASIS_DISABLED:
	case DP_TEST_PATTERN_SQUARE_PRESHOOT_DEEMPHASIS_DISABLED:
		REG_SET(DP_DPHY_SYM32_TP_SQ_PULSE, 0,
				TP_SQ_PULSE_WIDTH, tp_params->custom_pattern[0]);

		REG_UPDATE_4(DP_DPHY_SYM32_TP_CONFIG,
				TP_SELECT0, DP_DPHY_TP_SELECT_SQUARE,
				TP_SELECT1, DP_DPHY_TP_SELECT_SQUARE,
				TP_SELECT2, DP_DPHY_TP_SELECT_SQUARE,
				TP_SELECT3, DP_DPHY_TP_SELECT_SQUARE);

		REG_UPDATE(DP_DPHY_SYM32_CONTROL,
				MODE, DP2_TEST_PATTERN);
		break;
	default:
		break;
	}
}

static void fill_stream_allocation_row_info(
		const struct link_mst_stream_allocation *stream_allocation,
		uint32_t *src,
		uint32_t *slots)
{
	const struct hpo_dp_stream_encoder *stream_enc = stream_allocation->hpo_dp_stream_enc;

	if (stream_enc && (stream_enc->id >= ENGINE_ID_HPO_DP_0)) {
		*src = stream_enc->id - ENGINE_ID_HPO_DP_0;
		*slots = stream_allocation->slot_count;
	} else {
		*src = 0;
		*slots = 0;
	}
}

/* programs DP VC payload allocation */
void dcn31_hpo_dp_link_enc_update_stream_allocation_table(
		struct hpo_dp_link_encoder *enc,
		const struct link_mst_stream_allocation_table *table)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	uint32_t slots = 0;
	uint32_t src = 0;

	/* --- Set MSE Stream Attribute -
	 * Setup VC Payload Table on Tx Side,
	 * Issue allocation change trigger
	 * to commit payload on both tx and rx side
	 */

	/* we should clean-up table each time */

	if (table->stream_count >= 1) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[0],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_DPHY_SYM32_SAT_VC0,
			SAT_STREAM_SOURCE, src,
			SAT_SLOT_COUNT, slots);

	if (table->stream_count >= 2) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[1],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_DPHY_SYM32_SAT_VC1,
			SAT_STREAM_SOURCE, src,
			SAT_SLOT_COUNT, slots);

	if (table->stream_count >= 3) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[2],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_DPHY_SYM32_SAT_VC2,
			SAT_STREAM_SOURCE, src,
			SAT_SLOT_COUNT, slots);

	if (table->stream_count >= 4) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[3],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_DPHY_SYM32_SAT_VC3,
			SAT_STREAM_SOURCE, src,
			SAT_SLOT_COUNT, slots);

	/* --- wait for transaction finish */

	/* send allocation change trigger (ACT)
	 * this step first sends the ACT,
	 * then double buffers the SAT into the hardware
	 * making the new allocation active on the DP MST mode link
	 */

	/* SAT_UPDATE:
	 * 0 - No Action
	 * 1 - Update SAT with trigger
	 * 2 - Update SAT without trigger
	 */
	REG_UPDATE(DP_DPHY_SYM32_SAT_UPDATE,
			SAT_UPDATE, 1);

	/* wait for update to complete
	 * (i.e. SAT_UPDATE_PENDING field is set to 0)
	 * No need for HW to enforce keepout.
	 */
	/* Best case and worst case wait time for SAT_UPDATE_PENDING
	 *   best: 109 us
	 *   worst: 868 us
	 */
	REG_WAIT(DP_DPHY_SYM32_STATUS,
			SAT_UPDATE_PENDING, 0,
			100, DP_SAT_UPDATE_MAX_RETRY);
}

void dcn31_hpo_dp_link_enc_set_throttled_vcp_size(
		struct hpo_dp_link_encoder *enc,
		uint32_t stream_encoder_inst,
		struct fixed31_32 avg_time_slots_per_mtp)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	uint32_t x = dc_fixpt_floor(
		avg_time_slots_per_mtp);
	uint32_t y = dc_fixpt_ceil(
		dc_fixpt_shl(
			dc_fixpt_sub_int(
				avg_time_slots_per_mtp,
				x),
			25));

	// If y rounds up to integer, carry it over to x.
	if (y >> 25) {
		x += 1;
		y = 0;
	}

	switch (stream_encoder_inst) {
	case 0:
		REG_SET_2(DP_DPHY_SYM32_VC_RATE_CNTL0, 0,
				STREAM_VC_RATE_X, x,
				STREAM_VC_RATE_Y, y);
		break;
	case 1:
		REG_SET_2(DP_DPHY_SYM32_VC_RATE_CNTL1, 0,
				STREAM_VC_RATE_X, x,
				STREAM_VC_RATE_Y, y);
		break;
	case 2:
		REG_SET_2(DP_DPHY_SYM32_VC_RATE_CNTL2, 0,
				STREAM_VC_RATE_X, x,
				STREAM_VC_RATE_Y, y);
		break;
	case 3:
		REG_SET_2(DP_DPHY_SYM32_VC_RATE_CNTL3, 0,
				STREAM_VC_RATE_X, x,
				STREAM_VC_RATE_Y, y);
		break;
	default:
		ASSERT(0);
	}

	/* Best case and worst case wait time for RATE_UPDATE_PENDING
	 *   best: 116 ns
	 *   worst: 903 ns
	 */
	/* wait for update to be completed on the link */
	REG_WAIT(DP_DPHY_SYM32_STATUS,
			RATE_UPDATE_PENDING, 0,
			1, 10);
}

static bool dcn31_hpo_dp_link_enc_is_in_alt_mode(
		struct hpo_dp_link_encoder *enc)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	uint32_t dp_alt_mode_disable = 0;

	ASSERT((enc->transmitter >= TRANSMITTER_UNIPHY_A) && (enc->transmitter <= TRANSMITTER_UNIPHY_E));

	/* if value == 1 alt mode is disabled, otherwise it is enabled */
	REG_GET(RDPCSTX_PHY_CNTL6[enc->transmitter], RDPCS_PHY_DPALT_DISABLE, &dp_alt_mode_disable);
	return (dp_alt_mode_disable == 0);
}

void dcn31_hpo_dp_link_enc_read_state(
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
	REG_GET_2(DP_DPHY_SYM32_SAT_VC3,
			SAT_STREAM_SOURCE, &state->stream_src[3],
			SAT_SLOT_COUNT, &state->slot_count[3]);

	REG_GET_2(DP_DPHY_SYM32_VC_RATE_CNTL0,
			STREAM_VC_RATE_X, &state->vc_rate_x[0],
			STREAM_VC_RATE_Y, &state->vc_rate_y[0]);
	REG_GET_2(DP_DPHY_SYM32_VC_RATE_CNTL1,
			STREAM_VC_RATE_X, &state->vc_rate_x[1],
			STREAM_VC_RATE_Y, &state->vc_rate_y[1]);
	REG_GET_2(DP_DPHY_SYM32_VC_RATE_CNTL2,
			STREAM_VC_RATE_X, &state->vc_rate_x[2],
			STREAM_VC_RATE_Y, &state->vc_rate_y[2]);
	REG_GET_2(DP_DPHY_SYM32_VC_RATE_CNTL3,
			STREAM_VC_RATE_X, &state->vc_rate_x[3],
			STREAM_VC_RATE_Y, &state->vc_rate_y[3]);
}

static enum bp_result link_transmitter_control(
	struct dcn31_hpo_dp_link_encoder *enc3,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result;
	struct dc_bios *bp = enc3->base.ctx->dc_bios;

	result = bp->funcs->transmitter_control(bp, cntl);

	return result;
}

/* enables DP PHY output for 128b132b encoding */
void dcn31_hpo_dp_link_enc_enable_dp_output(
	struct hpo_dp_link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum transmitter transmitter,
	enum hpd_source_id hpd_source)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Set the transmitter */
	enc3->base.transmitter = transmitter;

	/* Set the hpd source */
	enc3->base.hpd_source = hpd_source;

	/* Enable the PHY */
	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc3->base.transmitter;
	//cntl.pll_id = clock_source;
	cntl.signal = SIGNAL_TYPE_DISPLAY_PORT_MST;
	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = enc3->base.hpd_source;
	cntl.pixel_clock = link_settings->link_rate * 1000;
	cntl.color_depth = COLOR_DEPTH_UNDEFINED;
	cntl.hpo_engine_id = enc->inst + ENGINE_ID_HPO_DP_0;

	result = link_transmitter_control(enc3, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}

void dcn31_hpo_dp_link_enc_disable_output(
	struct hpo_dp_link_encoder *enc,
	enum signal_type signal)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* disable transmitter */
	cntl.action = TRANSMITTER_CONTROL_DISABLE;
	cntl.transmitter = enc3->base.transmitter;
	cntl.hpd_sel = enc3->base.hpd_source;
	cntl.signal = signal;

	result = link_transmitter_control(enc3, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
		return;
	}

	/* disable encoder */
	dcn31_hpo_dp_link_enc_disable(enc);
}

void dcn31_hpo_dp_link_enc_set_ffe(
	struct hpo_dp_link_encoder *enc,
	const struct dc_link_settings *link_settings,
	uint8_t ffe_preset)
{
	struct dcn31_hpo_dp_link_encoder *enc3 = DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* disable transmitter */
	cntl.transmitter = enc3->base.transmitter;
	cntl.action = TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS;
	cntl.signal = SIGNAL_TYPE_DISPLAY_PORT_MST;
	cntl.lanes_number = link_settings->lane_count;
	cntl.pixel_clock = link_settings->link_rate * 1000;
	cntl.lane_settings = ffe_preset;

	result = link_transmitter_control(enc3, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
		return;
	}
}

static struct hpo_dp_link_encoder_funcs dcn31_hpo_dp_link_encoder_funcs = {
	.enable_link_phy = dcn31_hpo_dp_link_enc_enable_dp_output,
	.disable_link_phy = dcn31_hpo_dp_link_enc_disable_output,
	.link_enable = dcn31_hpo_dp_link_enc_enable,
	.link_disable = dcn31_hpo_dp_link_enc_disable,
	.set_link_test_pattern = dcn31_hpo_dp_link_enc_set_link_test_pattern,
	.update_stream_allocation_table = dcn31_hpo_dp_link_enc_update_stream_allocation_table,
	.set_throttled_vcp_size = dcn31_hpo_dp_link_enc_set_throttled_vcp_size,
	.is_in_alt_mode = dcn31_hpo_dp_link_enc_is_in_alt_mode,
	.read_state = dcn31_hpo_dp_link_enc_read_state,
	.set_ffe = dcn31_hpo_dp_link_enc_set_ffe,
};

void hpo_dp_link_encoder31_construct(struct dcn31_hpo_dp_link_encoder *enc31,
		struct dc_context *ctx,
		uint32_t inst,
		const struct dcn31_hpo_dp_link_encoder_registers *hpo_le_regs,
		const struct dcn31_hpo_dp_link_encoder_shift *hpo_le_shift,
		const struct dcn31_hpo_dp_link_encoder_mask *hpo_le_mask)
{
	enc31->base.ctx = ctx;

	enc31->base.inst = inst;
	enc31->base.funcs = &dcn31_hpo_dp_link_encoder_funcs;
	enc31->base.hpd_source = HPD_SOURCEID_UNKNOWN;
	enc31->base.transmitter = TRANSMITTER_UNKNOWN;

	enc31->regs = hpo_le_regs;
	enc31->hpo_le_shift = hpo_le_shift;
	enc31->hpo_le_mask = hpo_le_mask;
}
