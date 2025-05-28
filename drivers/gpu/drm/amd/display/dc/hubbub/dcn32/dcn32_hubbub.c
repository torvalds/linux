/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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


#include "dcn30/dcn30_hubbub.h"
#include "dcn32_hubbub.h"
#include "dm_services.h"
#include "reg_helper.h"


#define CTX \
	hubbub2->base.ctx
#define DC_LOGGER \
	hubbub2->base.ctx->logger
#define REG(reg)\
	hubbub2->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hubbub2->shifts->field_name, hubbub2->masks->field_name

/**
 * DCN32_CRB_SEGMENT_SIZE_KB: Maximum Configurable Return Buffer size for
 *                            DCN32
 */
#define DCN32_CRB_SEGMENT_SIZE_KB 64

static void dcn32_init_crb(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	REG_GET(DCHUBBUB_DET0_CTRL, DET0_SIZE_CURRENT,
		&hubbub2->det0_size);

	REG_GET(DCHUBBUB_DET1_CTRL, DET1_SIZE_CURRENT,
		&hubbub2->det1_size);

	REG_GET(DCHUBBUB_DET2_CTRL, DET2_SIZE_CURRENT,
		&hubbub2->det2_size);

	REG_GET(DCHUBBUB_DET3_CTRL, DET3_SIZE_CURRENT,
		&hubbub2->det3_size);

	REG_GET(DCHUBBUB_COMPBUF_CTRL, COMPBUF_SIZE_CURRENT,
		&hubbub2->compbuf_size_segments);

	REG_SET_2(COMPBUF_RESERVED_SPACE, 0,
			COMPBUF_RESERVED_SPACE_64B, hubbub2->pixel_chunk_size / 32,
			COMPBUF_RESERVED_SPACE_ZS, hubbub2->pixel_chunk_size / 128);
	REG_UPDATE(DCHUBBUB_DEBUG_CTRL_0, DET_DEPTH, 0x47F);
}

void hubbub32_set_request_limit(struct hubbub *hubbub, int memory_channel_count, int words_per_channel)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	uint32_t request_limit = 3 * memory_channel_count * words_per_channel / 4;

	ASSERT((request_limit & (~0xFFF)) == 0); //field is only 24 bits long
	ASSERT(request_limit > 0); //field is only 24 bits long

	if (request_limit > 0xFFF)
		request_limit = 0xFFF;

	if (request_limit > 0)
		REG_UPDATE(SDPIF_REQUEST_RATE_LIMIT, SDPIF_REQUEST_RATE_LIMIT, request_limit);
}


void dcn32_program_det_size(struct hubbub *hubbub, int hubp_inst, unsigned int det_buffer_size_in_kbyte)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	unsigned int det_size_segments = (det_buffer_size_in_kbyte + DCN32_CRB_SEGMENT_SIZE_KB - 1) / DCN32_CRB_SEGMENT_SIZE_KB;

	switch (hubp_inst) {
	case 0:
		REG_UPDATE(DCHUBBUB_DET0_CTRL,
					DET0_SIZE, det_size_segments);
		hubbub2->det0_size = det_size_segments;
		break;
	case 1:
		REG_UPDATE(DCHUBBUB_DET1_CTRL,
					DET1_SIZE, det_size_segments);
		hubbub2->det1_size = det_size_segments;
		break;
	case 2:
		REG_UPDATE(DCHUBBUB_DET2_CTRL,
					DET2_SIZE, det_size_segments);
		hubbub2->det2_size = det_size_segments;
		break;
	case 3:
		REG_UPDATE(DCHUBBUB_DET3_CTRL,
					DET3_SIZE, det_size_segments);
		hubbub2->det3_size = det_size_segments;
		break;
	default:
		break;
	}
	if (hubbub2->det0_size + hubbub2->det1_size + hubbub2->det2_size
			+ hubbub2->det3_size + hubbub2->compbuf_size_segments > hubbub2->crb_size_segs) {
		/* This may happen during seamless transition from ODM 2:1 to ODM4:1 */
		DC_LOG_WARNING("CRB Config Warning: DET size (%d,%d,%d,%d) + Compbuf size (%d) >  CRB segments (%d)\n",
						hubbub2->det0_size, hubbub2->det1_size, hubbub2->det2_size, hubbub2->det3_size,
						hubbub2->compbuf_size_segments, hubbub2->crb_size_segs);
	}
}

void dcn32_program_compbuf_size(struct hubbub *hubbub, unsigned int compbuf_size_kb, bool safe_to_increase)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	unsigned int compbuf_size_segments = (compbuf_size_kb + DCN32_CRB_SEGMENT_SIZE_KB - 1) / DCN32_CRB_SEGMENT_SIZE_KB;

	if (safe_to_increase || compbuf_size_segments <= hubbub2->compbuf_size_segments) {
		if (compbuf_size_segments > hubbub2->compbuf_size_segments) {
			REG_WAIT(DCHUBBUB_DET0_CTRL, DET0_SIZE_CURRENT, hubbub2->det0_size, 1, 100);
			REG_WAIT(DCHUBBUB_DET1_CTRL, DET1_SIZE_CURRENT, hubbub2->det1_size, 1, 100);
			REG_WAIT(DCHUBBUB_DET2_CTRL, DET2_SIZE_CURRENT, hubbub2->det2_size, 1, 100);
			REG_WAIT(DCHUBBUB_DET3_CTRL, DET3_SIZE_CURRENT, hubbub2->det3_size, 1, 100);
		}
		/* Should never be hit, if it is we have an erroneous hw config*/
		ASSERT(hubbub2->det0_size + hubbub2->det1_size + hubbub2->det2_size
				+ hubbub2->det3_size + compbuf_size_segments <= hubbub2->crb_size_segs);
		REG_UPDATE(DCHUBBUB_COMPBUF_CTRL, COMPBUF_SIZE, compbuf_size_segments);
		hubbub2->compbuf_size_segments = compbuf_size_segments;
		ASSERT(REG_GET(DCHUBBUB_COMPBUF_CTRL, CONFIG_ERROR, &compbuf_size_segments) && !compbuf_size_segments);
	}
}

static uint32_t convert_and_clamp(
	uint32_t wm_ns,
	uint32_t refclk_mhz,
	uint32_t clamp_value)
{
	uint32_t ret_val = 0;
	ret_val = wm_ns * refclk_mhz;

	ret_val /= 1000;

	if (ret_val > clamp_value)
		ret_val = clamp_value;

	return ret_val;
}

bool hubbub32_program_urgent_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;
	bool wm_pending = false;

	/* Repeat for water mark set A, B, C and D. */
	/* clock state A */
	if (safe_to_lower || watermarks->a.urgent_ns > hubbub2->watermarks.a.urgent_ns) {
		hubbub2->watermarks.a.urgent_ns = watermarks->a.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->a.urgent_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.urgent_ns, prog_wm_value);
	} else if (watermarks->a.urgent_ns < hubbub2->watermarks.a.urgent_ns)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->a.frac_urg_bw_flip
			> hubbub2->watermarks.a.frac_urg_bw_flip) {
		hubbub2->watermarks.a.frac_urg_bw_flip = watermarks->a.frac_urg_bw_flip;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A, watermarks->a.frac_urg_bw_flip);
	} else if (watermarks->a.frac_urg_bw_flip
			< hubbub2->watermarks.a.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.frac_urg_bw_nom
			> hubbub2->watermarks.a.frac_urg_bw_nom) {
		hubbub2->watermarks.a.frac_urg_bw_nom = watermarks->a.frac_urg_bw_nom;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_A, watermarks->a.frac_urg_bw_nom);
	} else if (watermarks->a.frac_urg_bw_nom
			< hubbub2->watermarks.a.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.urgent_latency_ns > hubbub2->watermarks.a.urgent_latency_ns) {
		hubbub2->watermarks.a.urgent_latency_ns = watermarks->a.urgent_latency_ns;
		prog_wm_value = convert_and_clamp(watermarks->a.urgent_latency_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A, prog_wm_value);
	} else if (watermarks->a.urgent_latency_ns < hubbub2->watermarks.a.urgent_latency_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.urgent_ns > hubbub2->watermarks.b.urgent_ns) {
		hubbub2->watermarks.b.urgent_ns = watermarks->b.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->b.urgent_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.urgent_ns, prog_wm_value);
	} else if (watermarks->b.urgent_ns < hubbub2->watermarks.b.urgent_ns)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->b.frac_urg_bw_flip
			> hubbub2->watermarks.b.frac_urg_bw_flip) {
		hubbub2->watermarks.b.frac_urg_bw_flip = watermarks->b.frac_urg_bw_flip;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, watermarks->b.frac_urg_bw_flip);
	} else if (watermarks->b.frac_urg_bw_flip
			< hubbub2->watermarks.b.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->b.frac_urg_bw_nom
			> hubbub2->watermarks.b.frac_urg_bw_nom) {
		hubbub2->watermarks.b.frac_urg_bw_nom = watermarks->b.frac_urg_bw_nom;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, watermarks->b.frac_urg_bw_nom);
	} else if (watermarks->b.frac_urg_bw_nom
			< hubbub2->watermarks.b.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->b.urgent_latency_ns > hubbub2->watermarks.b.urgent_latency_ns) {
		hubbub2->watermarks.b.urgent_latency_ns = watermarks->b.urgent_latency_ns;
		prog_wm_value = convert_and_clamp(watermarks->b.urgent_latency_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, prog_wm_value);
	} else if (watermarks->b.urgent_latency_ns < hubbub2->watermarks.b.urgent_latency_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.urgent_ns > hubbub2->watermarks.c.urgent_ns) {
		hubbub2->watermarks.c.urgent_ns = watermarks->c.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->c.urgent_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.urgent_ns, prog_wm_value);
	} else if (watermarks->c.urgent_ns < hubbub2->watermarks.c.urgent_ns)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->c.frac_urg_bw_flip
			> hubbub2->watermarks.c.frac_urg_bw_flip) {
		hubbub2->watermarks.c.frac_urg_bw_flip = watermarks->c.frac_urg_bw_flip;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C, watermarks->c.frac_urg_bw_flip);
	} else if (watermarks->c.frac_urg_bw_flip
			< hubbub2->watermarks.c.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->c.frac_urg_bw_nom
			> hubbub2->watermarks.c.frac_urg_bw_nom) {
		hubbub2->watermarks.c.frac_urg_bw_nom = watermarks->c.frac_urg_bw_nom;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_C, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_C, watermarks->c.frac_urg_bw_nom);
	} else if (watermarks->c.frac_urg_bw_nom
			< hubbub2->watermarks.c.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->c.urgent_latency_ns > hubbub2->watermarks.c.urgent_latency_ns) {
		hubbub2->watermarks.c.urgent_latency_ns = watermarks->c.urgent_latency_ns;
		prog_wm_value = convert_and_clamp(watermarks->c.urgent_latency_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C, prog_wm_value);
	} else if (watermarks->c.urgent_latency_ns < hubbub2->watermarks.c.urgent_latency_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.urgent_ns > hubbub2->watermarks.d.urgent_ns) {
		hubbub2->watermarks.d.urgent_ns = watermarks->d.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->d.urgent_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.urgent_ns, prog_wm_value);
	} else if (watermarks->d.urgent_ns < hubbub2->watermarks.d.urgent_ns)
		wm_pending = true;

	/* determine the transfer time for a quantity of data for a particular requestor.*/
	if (safe_to_lower || watermarks->d.frac_urg_bw_flip
			> hubbub2->watermarks.d.frac_urg_bw_flip) {
		hubbub2->watermarks.d.frac_urg_bw_flip = watermarks->d.frac_urg_bw_flip;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D, watermarks->d.frac_urg_bw_flip);
	} else if (watermarks->d.frac_urg_bw_flip
			< hubbub2->watermarks.d.frac_urg_bw_flip)
		wm_pending = true;

	if (safe_to_lower || watermarks->d.frac_urg_bw_nom
			> hubbub2->watermarks.d.frac_urg_bw_nom) {
		hubbub2->watermarks.d.frac_urg_bw_nom = watermarks->d.frac_urg_bw_nom;

		REG_SET(DCHUBBUB_ARB_FRAC_URG_BW_NOM_D, 0,
				DCHUBBUB_ARB_FRAC_URG_BW_NOM_D, watermarks->d.frac_urg_bw_nom);
	} else if (watermarks->d.frac_urg_bw_nom
			< hubbub2->watermarks.d.frac_urg_bw_nom)
		wm_pending = true;

	if (safe_to_lower || watermarks->d.urgent_latency_ns > hubbub2->watermarks.d.urgent_latency_ns) {
		hubbub2->watermarks.d.urgent_latency_ns = watermarks->d.urgent_latency_ns;
		prog_wm_value = convert_and_clamp(watermarks->d.urgent_latency_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D, 0,
				DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D, prog_wm_value);
	} else if (watermarks->d.urgent_latency_ns < hubbub2->watermarks.d.urgent_latency_ns)
		wm_pending = true;

	return wm_pending;
}

bool hubbub32_program_stutter_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub2->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub2->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub2->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.cstate_pstate.cstate_exit_ns
			> hubbub2->watermarks.a.cstate_pstate.cstate_exit_ns) {
		hubbub2->watermarks.a.cstate_pstate.cstate_exit_ns =
				watermarks->a.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_exit_ns
			< hubbub2->watermarks.a.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub2->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub2->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub2->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_exit_ns
			> hubbub2->watermarks.b.cstate_pstate.cstate_exit_ns) {
		hubbub2->watermarks.b.cstate_pstate.cstate_exit_ns =
				watermarks->b.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_exit_ns
			< hubbub2->watermarks.b.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub2->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub2->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub2->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_exit_ns
			> hubbub2->watermarks.c.cstate_pstate.cstate_exit_ns) {
		hubbub2->watermarks.c.cstate_pstate.cstate_exit_ns =
				watermarks->c.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_exit_ns
			< hubbub2->watermarks.c.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub2->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub2->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub2->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_exit_ns
			> hubbub2->watermarks.d.cstate_pstate.cstate_exit_ns) {
		hubbub2->watermarks.d.cstate_pstate.cstate_exit_ns =
				watermarks->d.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.cstate_exit_ns
			< hubbub2->watermarks.d.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	return wm_pending;
}


bool hubbub32_program_pstate_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;

	bool wm_pending = false;

	/* Section for UCLK_PSTATE_CHANGE_WATERMARKS */
	/* clock state A */
	if (safe_to_lower || watermarks->a.cstate_pstate.pstate_change_ns
			> hubbub2->watermarks.a.cstate_pstate.pstate_change_ns) {
		hubbub2->watermarks.a.cstate_pstate.pstate_change_ns =
				watermarks->a.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->a.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.pstate_change_ns
			< hubbub2->watermarks.a.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.cstate_pstate.pstate_change_ns
			> hubbub2->watermarks.b.cstate_pstate.pstate_change_ns) {
		hubbub2->watermarks.b.cstate_pstate.pstate_change_ns =
				watermarks->b.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->b.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.pstate_change_ns
			< hubbub2->watermarks.b.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.pstate_change_ns
			> hubbub2->watermarks.c.cstate_pstate.pstate_change_ns) {
		hubbub2->watermarks.c.cstate_pstate.pstate_change_ns =
				watermarks->c.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_C, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->c.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.pstate_change_ns
			< hubbub2->watermarks.c.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.cstate_pstate.pstate_change_ns
			> hubbub2->watermarks.d.cstate_pstate.pstate_change_ns) {
		hubbub2->watermarks.d.cstate_pstate.pstate_change_ns =
				watermarks->d.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_D, 0,
				DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->d.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.pstate_change_ns
			< hubbub2->watermarks.d.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* Section for FCLK_PSTATE_CHANGE_WATERMARKS */
	/* clock state A */
	if (safe_to_lower || watermarks->a.cstate_pstate.fclk_pstate_change_ns
			> hubbub2->watermarks.a.cstate_pstate.fclk_pstate_change_ns) {
		hubbub2->watermarks.a.cstate_pstate.fclk_pstate_change_ns =
				watermarks->a.cstate_pstate.fclk_pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.fclk_pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->a.cstate_pstate.fclk_pstate_change_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.fclk_pstate_change_ns
			< hubbub2->watermarks.a.cstate_pstate.fclk_pstate_change_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.cstate_pstate.fclk_pstate_change_ns
			> hubbub2->watermarks.b.cstate_pstate.fclk_pstate_change_ns) {
		hubbub2->watermarks.b.cstate_pstate.fclk_pstate_change_ns =
				watermarks->b.cstate_pstate.fclk_pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.fclk_pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->b.cstate_pstate.fclk_pstate_change_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.fclk_pstate_change_ns
			< hubbub2->watermarks.b.cstate_pstate.fclk_pstate_change_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.fclk_pstate_change_ns
			> hubbub2->watermarks.c.cstate_pstate.fclk_pstate_change_ns) {
		hubbub2->watermarks.c.cstate_pstate.fclk_pstate_change_ns =
				watermarks->c.cstate_pstate.fclk_pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.fclk_pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_C, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->c.cstate_pstate.fclk_pstate_change_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.fclk_pstate_change_ns
			< hubbub2->watermarks.c.cstate_pstate.fclk_pstate_change_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.cstate_pstate.fclk_pstate_change_ns
			> hubbub2->watermarks.d.cstate_pstate.fclk_pstate_change_ns) {
		hubbub2->watermarks.d.cstate_pstate.fclk_pstate_change_ns =
				watermarks->d.cstate_pstate.fclk_pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.fclk_pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_D, 0,
				DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("FCLK_CHANGE_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->d.cstate_pstate.fclk_pstate_change_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.fclk_pstate_change_ns
			< hubbub2->watermarks.d.cstate_pstate.fclk_pstate_change_ns)
		wm_pending = true;

	return wm_pending;
}


bool hubbub32_program_usr_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;

	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->a.usr_retraining_ns
			> hubbub2->watermarks.a.usr_retraining_ns) {
		hubbub2->watermarks.a.usr_retraining_ns = watermarks->a.usr_retraining_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.usr_retraining_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("USR_RETRAINING_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->a.usr_retraining_ns, prog_wm_value);
	} else if (watermarks->a.usr_retraining_ns
			< hubbub2->watermarks.a.usr_retraining_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.usr_retraining_ns
			> hubbub2->watermarks.b.usr_retraining_ns) {
		hubbub2->watermarks.b.usr_retraining_ns = watermarks->b.usr_retraining_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.usr_retraining_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("USR_RETRAINING_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->b.usr_retraining_ns, prog_wm_value);
	} else if (watermarks->b.usr_retraining_ns
			< hubbub2->watermarks.b.usr_retraining_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.usr_retraining_ns
			> hubbub2->watermarks.c.usr_retraining_ns) {
		hubbub2->watermarks.c.usr_retraining_ns =
				watermarks->c.usr_retraining_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.usr_retraining_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_C, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("USR_RETRAINING_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->c.usr_retraining_ns, prog_wm_value);
	} else if (watermarks->c.usr_retraining_ns
			< hubbub2->watermarks.c.usr_retraining_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.usr_retraining_ns
			> hubbub2->watermarks.d.usr_retraining_ns) {
		hubbub2->watermarks.d.usr_retraining_ns =
				watermarks->d.usr_retraining_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.usr_retraining_ns,
				refclk_mhz, 0x3fff);
		REG_SET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_D, 0,
				DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("USR_RETRAINING_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->d.usr_retraining_ns, prog_wm_value);
	} else if (watermarks->d.usr_retraining_ns
			< hubbub2->watermarks.d.usr_retraining_ns)
		wm_pending = true;

	return wm_pending;
}

void hubbub32_force_usr_retraining_allow(struct hubbub *hubbub, bool allow)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/*
	 * DCHUBBUB_ARB_ALLOW_USR_RETRAINING_FORCE_ENABLE = 1 means enabling forcing value
	 * DCHUBBUB_ARB_ALLOW_USR_RETRAINING_FORCE_VALUE = 1 or 0,  means value to be forced when force enable
	 */

	REG_UPDATE_2(DCHUBBUB_ARB_USR_RETRAINING_CNTL,
			DCHUBBUB_ARB_ALLOW_USR_RETRAINING_FORCE_VALUE, allow,
			DCHUBBUB_ARB_ALLOW_USR_RETRAINING_FORCE_ENABLE, allow);
}

static bool hubbub32_program_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	bool wm_pending = false;

	if (hubbub32_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub32_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub32_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub32_program_usr_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	/*
	 * The DCHub arbiter has a mechanism to dynamically rate limit the DCHub request stream to the fabric.
	 * If the memory controller is fully utilized and the DCHub requestors are
	 * well ahead of their amortized schedule, then it is safe to prevent the next winner
	 * from being committed and sent to the fabric.
	 * The utilization of the memory controller is approximated by ensuring that
	 * the number of outstanding requests is greater than a threshold specified
	 * by the ARB_MIN_REQ_OUTSTANDING. To determine that the DCHub requestors are well ahead of the amortized schedule,
	 * the slack of the next winner is compared with the ARB_SAT_LEVEL in DLG RefClk cycles.
	 *
	 * TODO: Revisit request limit after figure out right number. request limit for RM isn't decided yet, set maximum value (0x1FF)
	 * to turn off it for now.
	 */
	/*REG_SET(DCHUBBUB_ARB_SAT_LEVEL, 0,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 0x1FF);*/

	if (safe_to_lower || hubbub->ctx->dc->debug.disable_stutter)
		hubbub1_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);

	hubbub32_force_usr_retraining_allow(hubbub, hubbub->ctx->dc->debug.force_usr_allow);

	return wm_pending;
}

/* Copy values from WM set A to all other sets */
static void hubbub32_init_watermarks(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t reg;

	reg = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B, reg);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C, reg);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B, reg);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_NOM_C, reg);
	REG_WRITE(DCHUBBUB_ARB_FRAC_URG_BW_NOM_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A);
	REG_WRITE(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B, reg);
	REG_WRITE(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C, reg);
	REG_WRITE(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A);
	REG_WRITE(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, reg);
	REG_WRITE(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_C, reg);
	REG_WRITE(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_D, reg);
}

static void hubbub32_wm_read_state(struct hubbub *hubbub,
		struct dcn_hubbub_wm *wm)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	struct dcn_hubbub_wm_set *s;

	memset(wm, 0, sizeof(struct dcn_hubbub_wm));

	s = &wm->sets[0];
	s->wm_set = 0;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A,
			 DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A, &s->dram_clk_change);

	REG_GET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A,
			 DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A, &s->usr_retrain);

	REG_GET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A,
			 DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A, &s->fclk_pstate_change);

	s = &wm->sets[1];
	s->wm_set = 1;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B,
			DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B, &s->dram_clk_change);

	REG_GET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B,
			 DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B, &s->usr_retrain);

	REG_GET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B,
			DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B, &s->fclk_pstate_change);

	s = &wm->sets[2];
	s->wm_set = 2;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_C,
			DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_C, &s->dram_clk_change);

	REG_GET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_C,
			 DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_C, &s->usr_retrain);

	REG_GET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_C,
			DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_C, &s->fclk_pstate_change);

	s = &wm->sets[3];
	s->wm_set = 3;
	REG_GET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, &s->data_urgent);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, &s->sr_enter);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, &s->sr_exit);

	REG_GET(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_D,
			DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_D, &s->dram_clk_change);

	REG_GET(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_D,
			 DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_D, &s->usr_retrain);

	REG_GET(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_D,
			DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_D, &s->fclk_pstate_change);
}

void hubbub32_force_wm_propagate_to_pipes(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t refclk_mhz = hubbub->ctx->dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;
	uint32_t prog_wm_value = convert_and_clamp(hubbub2->watermarks.a.urgent_ns,
			refclk_mhz, 0x3fff);

	REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, 0,
			DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, prog_wm_value);
}

void hubbub32_get_mall_en(struct hubbub *hubbub, unsigned int *mall_in_use)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prefetch_complete, mall_en;

	REG_GET_2(DCHUBBUB_ARB_MALL_CNTL, MALL_IN_USE, &mall_en,
			  MALL_PREFETCH_COMPLETE, &prefetch_complete);

	*mall_in_use = prefetch_complete && mall_en;
}

void hubbub32_init(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	/* Enable clock gate*/
	if (hubbub->ctx->dc->debug.disable_clock_gate) {
		/*done in hwseq*/
		/*REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);*/

		REG_UPDATE_2(DCHUBBUB_CLOCK_CNTL,
			DISPCLK_R_DCHUBBUB_GATE_DIS, 1,
			DCFCLK_R_DCHUBBUB_GATE_DIS, 1);
	}
	/*
	ignore the "df_pre_cstate_req" from the SDP port control.
	only the DCN will determine when to connect the SDP port
	*/
	REG_UPDATE(DCHUBBUB_SDPIF_CFG0,
			SDPIF_PORT_CONTROL, 1);
	/*Set SDP's max outstanding request to 512
	must set the register back to 0 (max outstanding = 256) in zero frame buffer mode*/
	REG_UPDATE(DCHUBBUB_SDPIF_CFG1,
			SDPIF_MAX_NUM_OUTSTANDING, 1);
	/*must set the registers back to 256 in zero frame buffer mode*/
	REG_UPDATE_2(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MAX_REQ_OUTSTAND, 512,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 512);
}

static const struct hubbub_funcs hubbub32_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub3_init_dchub_sys_ctx,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle = hubbub3_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub2_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub3_get_dcc_compression_cap,
	.wm_read_state = hubbub32_wm_read_state,
	.get_dchub_ref_freq = hubbub2_get_dchub_ref_freq,
	.program_watermarks = hubbub32_program_watermarks,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.verify_allow_pstate_change_high = hubbub1_verify_allow_pstate_change_high,
	.force_wm_propagate_to_pipes = hubbub32_force_wm_propagate_to_pipes,
	.force_pstate_change_control = hubbub3_force_pstate_change_control,
	.init_watermarks = hubbub32_init_watermarks,
	.program_det_size = dcn32_program_det_size,
	.program_compbuf_size = dcn32_program_compbuf_size,
	.init_crb = dcn32_init_crb,
	.hubbub_read_state = hubbub2_read_state,
	.force_usr_retraining_allow = hubbub32_force_usr_retraining_allow,
	.set_request_limit = hubbub32_set_request_limit,
	.get_mall_en = hubbub32_get_mall_en,
};

void hubbub32_construct(struct dcn20_hubbub *hubbub2,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask,
	int det_size_kb,
	int pixel_chunk_size_kb,
	int config_return_buffer_size_kb)
{
	hubbub2->base.ctx = ctx;
	hubbub2->base.funcs = &hubbub32_funcs;
	hubbub2->regs = hubbub_regs;
	hubbub2->shifts = hubbub_shift;
	hubbub2->masks = hubbub_mask;

	hubbub2->debug_test_index_pstate = 0xB;
	hubbub2->detile_buf_size = det_size_kb * 1024;
	hubbub2->pixel_chunk_size = pixel_chunk_size_kb * 1024;
	hubbub2->crb_size_segs = config_return_buffer_size_kb / DCN32_CRB_SEGMENT_SIZE_KB;
}
