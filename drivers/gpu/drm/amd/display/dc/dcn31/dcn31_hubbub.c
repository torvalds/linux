/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include "dcn31_hubbub.h"
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

#ifdef NUM_VMID
#undef NUM_VMID
#endif
#define NUM_VMID 16

#define DCN31_CRB_SEGMENT_SIZE_KB 64

static void dcn31_init_crb(struct hubbub *hubbub)
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
	REG_UPDATE(DCHUBBUB_DEBUG_CTRL_0, DET_DEPTH, 0x17F);
}

static void dcn31_program_det_size(struct hubbub *hubbub, int hubp_inst, unsigned int det_buffer_size_in_kbyte)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	unsigned int det_size_segments = (det_buffer_size_in_kbyte + DCN31_CRB_SEGMENT_SIZE_KB - 1) / DCN31_CRB_SEGMENT_SIZE_KB;

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
	/* Should never be hit, if it is we have an erroneous hw config*/
	ASSERT(hubbub2->det0_size + hubbub2->det1_size + hubbub2->det2_size
			+ hubbub2->det3_size + hubbub2->compbuf_size_segments <= hubbub2->crb_size_segs);
}

static void dcn31_program_compbuf_size(struct hubbub *hubbub, unsigned int compbuf_size_kb, bool safe_to_increase)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	unsigned int compbuf_size_segments = (compbuf_size_kb + DCN31_CRB_SEGMENT_SIZE_KB - 1) / DCN31_CRB_SEGMENT_SIZE_KB;

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

	if (ret_val > clamp_value) {
		/* clamping WMs is abnormal, unexpected and may lead to underflow*/
		ASSERT(0);
		ret_val = clamp_value;
	}

	return ret_val;
}

static bool hubbub31_program_urgent_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
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

static bool hubbub31_program_stutter_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
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

	if (safe_to_lower || watermarks->a.cstate_pstate.cstate_enter_plus_exit_z8_ns
			> hubbub2->watermarks.a.cstate_pstate.cstate_enter_plus_exit_z8_ns) {
		hubbub2->watermarks.a.cstate_pstate.cstate_enter_plus_exit_z8_ns =
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_z8_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_WATERMARK_Z8_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_enter_plus_exit_z8_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_enter_plus_exit_z8_ns
			< hubbub2->watermarks.a.cstate_pstate.cstate_enter_plus_exit_z8_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.cstate_pstate.cstate_exit_z8_ns
			> hubbub2->watermarks.a.cstate_pstate.cstate_exit_z8_ns) {
		hubbub2->watermarks.a.cstate_pstate.cstate_exit_z8_ns =
				watermarks->a.cstate_pstate.cstate_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_exit_z8_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_Z8_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_exit_z8_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_exit_z8_ns
			< hubbub2->watermarks.a.cstate_pstate.cstate_exit_z8_ns)
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

	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_enter_plus_exit_z8_ns
			> hubbub2->watermarks.b.cstate_pstate.cstate_enter_plus_exit_z8_ns) {
		hubbub2->watermarks.b.cstate_pstate.cstate_enter_plus_exit_z8_ns =
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_z8_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_WATERMARK_Z8_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_enter_plus_exit_z8_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_enter_plus_exit_z8_ns
			< hubbub2->watermarks.b.cstate_pstate.cstate_enter_plus_exit_z8_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_exit_z8_ns
			> hubbub2->watermarks.b.cstate_pstate.cstate_exit_z8_ns) {
		hubbub2->watermarks.b.cstate_pstate.cstate_exit_z8_ns =
				watermarks->b.cstate_pstate.cstate_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_exit_z8_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_Z8_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_exit_z8_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_exit_z8_ns
			< hubbub2->watermarks.b.cstate_pstate.cstate_exit_z8_ns)
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

	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_enter_plus_exit_z8_ns
			> hubbub2->watermarks.c.cstate_pstate.cstate_enter_plus_exit_z8_ns) {
		hubbub2->watermarks.c.cstate_pstate.cstate_enter_plus_exit_z8_ns =
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_z8_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_WATERMARK_Z8_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_enter_plus_exit_z8_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_enter_plus_exit_z8_ns
			< hubbub2->watermarks.c.cstate_pstate.cstate_enter_plus_exit_z8_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_exit_z8_ns
			> hubbub2->watermarks.c.cstate_pstate.cstate_exit_z8_ns) {
		hubbub2->watermarks.c.cstate_pstate.cstate_exit_z8_ns =
				watermarks->c.cstate_pstate.cstate_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_exit_z8_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_Z8_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_exit_z8_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_exit_z8_ns
			< hubbub2->watermarks.c.cstate_pstate.cstate_exit_z8_ns)
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

	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_enter_plus_exit_z8_ns
			> hubbub2->watermarks.d.cstate_pstate.cstate_enter_plus_exit_z8_ns) {
		hubbub2->watermarks.d.cstate_pstate.cstate_enter_plus_exit_z8_ns =
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_z8_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_WATERMARK_Z8_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_enter_plus_exit_z8_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.cstate_enter_plus_exit_z8_ns
			< hubbub2->watermarks.d.cstate_pstate.cstate_enter_plus_exit_z8_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_exit_z8_ns
			> hubbub2->watermarks.d.cstate_pstate.cstate_exit_z8_ns) {
		hubbub2->watermarks.d.cstate_pstate.cstate_exit_z8_ns =
				watermarks->d.cstate_pstate.cstate_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_exit_z8_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_Z8_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_exit_z8_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.cstate_exit_z8_ns
			< hubbub2->watermarks.d.cstate_pstate.cstate_exit_z8_ns)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub31_program_pstate_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;

	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->a.cstate_pstate.pstate_change_ns
			> hubbub2->watermarks.a.cstate_pstate.pstate_change_ns) {
		hubbub2->watermarks.a.cstate_pstate.pstate_change_ns =
				watermarks->a.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, prog_wm_value);
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
		REG_SET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->b.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.pstate_change_ns
			< hubbub2->watermarks.b.cstate_pstate.pstate_change_ns)
		wm_pending = false;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.pstate_change_ns
			> hubbub2->watermarks.c.cstate_pstate.pstate_change_ns) {
		hubbub2->watermarks.c.cstate_pstate.pstate_change_ns =
				watermarks->c.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0xffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, prog_wm_value);
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
		REG_SET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->d.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.pstate_change_ns
			< hubbub2->watermarks.d.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	return wm_pending;
}

static bool hubbub31_program_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	bool wm_pending = false;

	if (hubbub31_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub31_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub31_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
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

	hubbub1_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);
	return wm_pending;
}

static void hubbub3_get_blk256_size(unsigned int *blk256_width, unsigned int *blk256_height,
		unsigned int bytes_per_element)
{
	/* copied from DML.  might want to refactor DML to leverage from DML */
	/* DML : get_blk256_size */
	if (bytes_per_element == 1) {
		*blk256_width = 16;
		*blk256_height = 16;
	} else if (bytes_per_element == 2) {
		*blk256_width = 16;
		*blk256_height = 8;
	} else if (bytes_per_element == 4) {
		*blk256_width = 8;
		*blk256_height = 8;
	} else if (bytes_per_element == 8) {
		*blk256_width = 8;
		*blk256_height = 4;
	}
}

static void hubbub31_det_request_size(
		unsigned int detile_buf_size,
		unsigned int height,
		unsigned int width,
		unsigned int bpe,
		bool *req128_horz_wc,
		bool *req128_vert_wc)
{
	unsigned int blk256_height = 0;
	unsigned int blk256_width = 0;
	unsigned int swath_bytes_horz_wc, swath_bytes_vert_wc;

	hubbub3_get_blk256_size(&blk256_width, &blk256_height, bpe);

	swath_bytes_horz_wc = width * blk256_height * bpe;
	swath_bytes_vert_wc = height * blk256_width * bpe;

	*req128_horz_wc = (2 * swath_bytes_horz_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */

	*req128_vert_wc = (2 * swath_bytes_vert_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */
}

static bool hubbub31_get_dcc_compression_cap(struct hubbub *hubbub,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output)
{
	struct dc *dc = hubbub->ctx->dc;
	enum dcc_control dcc_control;
	unsigned int bpe;
	enum segment_order segment_order_horz, segment_order_vert;
	bool req128_horz_wc, req128_vert_wc;

	memset(output, 0, sizeof(*output));

	if (dc->debug.disable_dcc == DCC_DISABLE)
		return false;

	if (!hubbub->funcs->dcc_support_pixel_format(input->format,
			&bpe))
		return false;

	if (!hubbub->funcs->dcc_support_swizzle(input->swizzle_mode, bpe,
			&segment_order_horz, &segment_order_vert))
		return false;

	hubbub31_det_request_size(TO_DCN20_HUBBUB(hubbub)->detile_buf_size,
			input->surface_size.height,  input->surface_size.width,
			bpe, &req128_horz_wc, &req128_vert_wc);

	if (!req128_horz_wc && !req128_vert_wc) {
		dcc_control = dcc_control__256_256_xxx;
	} else if (input->scan == SCAN_DIRECTION_HORIZONTAL) {
		if (!req128_horz_wc)
			dcc_control = dcc_control__256_256_xxx;
		else if (segment_order_horz == segment_order__contiguous)
			dcc_control = dcc_control__128_128_xxx;
		else
			dcc_control = dcc_control__256_64_64;
	} else if (input->scan == SCAN_DIRECTION_VERTICAL) {
		if (!req128_vert_wc)
			dcc_control = dcc_control__256_256_xxx;
		else if (segment_order_vert == segment_order__contiguous)
			dcc_control = dcc_control__128_128_xxx;
		else
			dcc_control = dcc_control__256_64_64;
	} else {
		if ((req128_horz_wc &&
			segment_order_horz == segment_order__non_contiguous) ||
			(req128_vert_wc &&
			segment_order_vert == segment_order__non_contiguous))
			/* access_dir not known, must use most constraining */
			dcc_control = dcc_control__256_64_64;
		else
			/* reg128 is true for either horz and vert
			 * but segment_order is contiguous
			 */
			dcc_control = dcc_control__128_128_xxx;
	}

	/* Exception for 64KB_R_X */
	if ((bpe == 2) && (input->swizzle_mode == DC_SW_64KB_R_X))
		dcc_control = dcc_control__128_128_xxx;

	if (dc->debug.disable_dcc == DCC_HALF_REQ_DISALBE &&
		dcc_control != dcc_control__256_256_xxx)
		return false;

	switch (dcc_control) {
	case dcc_control__256_256_xxx:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 256;
		output->grph.rgb.independent_64b_blks = false;
		output->grph.rgb.dcc_controls.dcc_256_256_unconstrained = 1;
		output->grph.rgb.dcc_controls.dcc_256_128_128 = 1;
		break;
	case dcc_control__128_128_xxx:
		output->grph.rgb.max_uncompressed_blk_size = 128;
		output->grph.rgb.max_compressed_blk_size = 128;
		output->grph.rgb.independent_64b_blks = false;
		output->grph.rgb.dcc_controls.dcc_128_128_uncontrained = 1;
		output->grph.rgb.dcc_controls.dcc_256_128_128 = 1;
		break;
	case dcc_control__256_64_64:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 64;
		output->grph.rgb.independent_64b_blks = true;
		output->grph.rgb.dcc_controls.dcc_256_64_64 = 1;
		break;
	case dcc_control__256_128_128:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 128;
		output->grph.rgb.independent_64b_blks = false;
		output->grph.rgb.dcc_controls.dcc_256_128_128 = 1;
		break;
	}
	output->capable = true;
	output->const_color_support = true;

	return true;
}

static int hubbub31_init_dchub_sys_ctx(struct hubbub *hubbub,
		struct dcn_hubbub_phys_addr_config *pa_config)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	struct dcn_vmid_page_table_config phys_config;

	REG_SET(DCN_VM_FB_LOCATION_BASE, 0,
			FB_BASE, pa_config->system_aperture.fb_base >> 24);
	REG_SET(DCN_VM_FB_LOCATION_TOP, 0,
			FB_TOP, pa_config->system_aperture.fb_top >> 24);
	REG_SET(DCN_VM_FB_OFFSET, 0,
			FB_OFFSET, pa_config->system_aperture.fb_offset >> 24);
	REG_SET(DCN_VM_AGP_BOT, 0,
			AGP_BOT, pa_config->system_aperture.agp_bot >> 24);
	REG_SET(DCN_VM_AGP_TOP, 0,
			AGP_TOP, pa_config->system_aperture.agp_top >> 24);
	REG_SET(DCN_VM_AGP_BASE, 0,
			AGP_BASE, pa_config->system_aperture.agp_base >> 24);

	if (pa_config->gart_config.page_table_start_addr != pa_config->gart_config.page_table_end_addr) {
		phys_config.page_table_start_addr = pa_config->gart_config.page_table_start_addr >> 12;
		phys_config.page_table_end_addr = pa_config->gart_config.page_table_end_addr >> 12;
		phys_config.page_table_base_addr = pa_config->gart_config.page_table_base_addr;
		phys_config.depth = 0;
		phys_config.block_size = 0;
		// Init VMID 0 based on PA config
		dcn20_vmid_setup(&hubbub2->vmid[0], &phys_config);

		dcn20_vmid_setup(&hubbub2->vmid[15], &phys_config);
	}

	dcn21_dchvm_init(hubbub);

	return NUM_VMID;
}

static void hubbub31_get_dchub_ref_freq(struct hubbub *hubbub,
		unsigned int dccg_ref_freq_inKhz,
		unsigned int *dchub_ref_freq_inKhz)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t ref_div = 0;
	uint32_t ref_en = 0;
	unsigned int dc_refclk_khz = 24000;

	REG_GET_2(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, &ref_div,
			DCHUBBUB_GLOBAL_TIMER_ENABLE, &ref_en);

	if (ref_en) {
		if (ref_div == 2)
			*dchub_ref_freq_inKhz = dc_refclk_khz / 2;
		else
			*dchub_ref_freq_inKhz = dc_refclk_khz;

		/*
		 * The external Reference Clock may change based on the board or
		 * platform requirements and the programmable integer divide must
		 * be programmed to provide a suitable DLG RefClk frequency between
		 * a minimum of 20MHz and maximum of 50MHz
		 */
		if (*dchub_ref_freq_inKhz < 20000 || *dchub_ref_freq_inKhz > 50000)
			ASSERT_CRITICAL(false);

		return;
	} else {
		*dchub_ref_freq_inKhz = dc_refclk_khz;

		// HUBBUB global timer must be enabled.
		ASSERT_CRITICAL(false);
		return;
	}
}

static const struct hubbub_funcs hubbub31_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub31_init_dchub_sys_ctx,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle = hubbub3_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub2_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub31_get_dcc_compression_cap,
	.wm_read_state = hubbub21_wm_read_state,
	.get_dchub_ref_freq = hubbub31_get_dchub_ref_freq,
	.program_watermarks = hubbub31_program_watermarks,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.program_det_size = dcn31_program_det_size,
	.program_compbuf_size = dcn31_program_compbuf_size,
	.init_crb = dcn31_init_crb,
	.hubbub_read_state = hubbub2_read_state,
};

void hubbub31_construct(struct dcn20_hubbub *hubbub31,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask,
	int det_size_kb,
	int pixel_chunk_size_kb,
	int config_return_buffer_size_kb)
{

	hubbub3_construct(hubbub31, ctx, hubbub_regs, hubbub_shift, hubbub_mask);
	hubbub31->base.funcs = &hubbub31_funcs;
	hubbub31->detile_buf_size = det_size_kb * 1024;
	hubbub31->pixel_chunk_size = pixel_chunk_size_kb * 1024;
	hubbub31->crb_size_segs = config_return_buffer_size_kb / DCN31_CRB_SEGMENT_SIZE_KB;
}

