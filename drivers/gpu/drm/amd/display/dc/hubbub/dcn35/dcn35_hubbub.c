/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "dcn31/dcn31_hubbub.h"
#include "dcn32/dcn32_hubbub.h"
#include "dcn35_hubbub.h"
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

#define DCN35_CRB_SEGMENT_SIZE_KB 64

void dcn35_init_crb(struct hubbub *hubbub)
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
	REG_UPDATE(DCHUBBUB_DEBUG_CTRL_0, DET_DEPTH, 0x5FF);
}

void dcn35_program_compbuf_size(struct hubbub *hubbub, unsigned int compbuf_size_kb, bool safe_to_increase)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	unsigned int compbuf_size_segments = (compbuf_size_kb + DCN35_CRB_SEGMENT_SIZE_KB - 1) / DCN35_CRB_SEGMENT_SIZE_KB;

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

static bool hubbub35_program_stutter_z8_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	uint32_t prog_wm_value;
	bool wm_pending = false;

	/* clock state A */
	if (watermarks->a.cstate_pstate.cstate_enter_plus_exit_z8_ns
			> hubbub2->watermarks.a.cstate_pstate.cstate_enter_plus_exit_z8_ns) {
		hubbub2->watermarks.a.cstate_pstate.cstate_enter_plus_exit_z8_ns =
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_z8_ns,
				refclk_mhz, 0xfffff);
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
				refclk_mhz, 0xfffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_Z8_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_exit_z8_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_exit_z8_ns
			< hubbub2->watermarks.a.cstate_pstate.cstate_exit_z8_ns)
		wm_pending = true;

	/* clock state B */

	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_enter_plus_exit_z8_ns
			> hubbub2->watermarks.b.cstate_pstate.cstate_enter_plus_exit_z8_ns) {
		hubbub2->watermarks.b.cstate_pstate.cstate_enter_plus_exit_z8_ns =
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_z8_ns,
				refclk_mhz, 0xfffff);
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
				refclk_mhz, 0xfffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_Z8_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_exit_z8_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_exit_z8_ns
			< hubbub2->watermarks.b.cstate_pstate.cstate_exit_z8_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_enter_plus_exit_z8_ns
			> hubbub2->watermarks.c.cstate_pstate.cstate_enter_plus_exit_z8_ns) {
		hubbub2->watermarks.c.cstate_pstate.cstate_enter_plus_exit_z8_ns =
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_z8_ns,
				refclk_mhz, 0xfffff);
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
				refclk_mhz, 0xfffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_Z8_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_exit_z8_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_exit_z8_ns
			< hubbub2->watermarks.c.cstate_pstate.cstate_exit_z8_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_enter_plus_exit_z8_ns
			> hubbub2->watermarks.d.cstate_pstate.cstate_enter_plus_exit_z8_ns) {
		hubbub2->watermarks.d.cstate_pstate.cstate_enter_plus_exit_z8_ns =
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_z8_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_z8_ns,
				refclk_mhz, 0xfffff);
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
				refclk_mhz, 0xfffff);
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

void hubbub35_get_dchub_ref_freq(struct hubbub *hubbub,
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
		/*init sequence issue on bringup patch*/
		REG_UPDATE_2(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, 1,
			DCHUBBUB_GLOBAL_TIMER_ENABLE, 1);
		// HUBBUB global timer must be enabled.
		ASSERT_CRITICAL(false);
		return;
	}
}


bool hubbub35_program_watermarks(
		struct hubbub *hubbub,
		union dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	bool wm_pending = false;
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	if (hubbub32_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub32_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub32_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub32_program_usr_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub35_program_stutter_z8_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	REG_SET(DCHUBBUB_ARB_SAT_LEVEL, 0,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE_2(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 0xFF,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND_COMMIT_THRESHOLD, 0xA);/*hw delta*/
	REG_UPDATE(DCHUBBUB_ARB_HOSTVM_CNTL, DCHUBBUB_ARB_MAX_QOS_COMMIT_THRESHOLD, 0xF);

	if (safe_to_lower || hubbub->ctx->dc->debug.disable_stutter)
		hubbub1_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);

	hubbub32_force_usr_retraining_allow(hubbub, hubbub->ctx->dc->debug.force_usr_allow);

	return wm_pending;
}

/* Copy values from WM set A to all other sets */
void hubbub35_init_watermarks(struct hubbub *hubbub)
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

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_C, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_D, reg);

	reg = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C, reg);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_D, reg);

}

void hubbub35_wm_read_state(struct hubbub *hubbub,
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

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_A, &s->sr_enter_exit_Z8);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_A,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_A, &s->sr_enter_Z8);
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

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_B, &s->sr_enter_exit_Z8);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_B,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_B, &s->sr_enter_Z8);

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

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_C, &s->sr_enter_exit_Z8);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_C,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_C, &s->sr_enter_Z8);

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

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_D,
			DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_Z8_D, &s->sr_enter_exit_Z8);

	REG_GET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_D,
			DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_Z8_D, &s->sr_enter_Z8);
}

static void hubbub35_set_fgcg(struct dcn20_hubbub *hubbub2, bool enable)
{
	REG_UPDATE(DCHUBBUB_CLOCK_CNTL, DCHUBBUB_FGCG_REP_DIS, !enable);
}

void hubbub35_init(struct hubbub *hubbub)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);
	/*Enable clock gaters*/
	if (hubbub->ctx->dc->debug.disable_clock_gate) {
		/*done in hwseq*/
		/*REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);*/

		REG_UPDATE_2(DCHUBBUB_CLOCK_CNTL,
			DISPCLK_R_DCHUBBUB_GATE_DIS, 1,
			DCFCLK_R_DCHUBBUB_GATE_DIS, 1);
	}
	hubbub35_set_fgcg(hubbub2,
			  hubbub->ctx->dc->debug.enable_fine_grain_clock_gating
				  .bits.dchubbub);
	/*
	ignore the "df_pre_cstate_req" from the SDP port control.
	only the DCN will determine when to connect the SDP port
	*/
	REG_UPDATE(DCHUBBUB_SDPIF_CFG0,
			SDPIF_PORT_CONTROL, 1);
	/*Set SDP's max outstanding request
	When set to 1: Max outstanding is 512
	When set to 0: Max outstanding is 256
	must set the register back to 0 (max outstanding = 256) in zero frame buffer mode*/
	REG_UPDATE(DCHUBBUB_SDPIF_CFG1,
			SDPIF_MAX_NUM_OUTSTANDING, 0);

	REG_UPDATE_2(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MAX_REQ_OUTSTAND, 256,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 256);

	memset(&hubbub2->watermarks.a.cstate_pstate, 0, sizeof(hubbub2->watermarks.a.cstate_pstate));
}

/*static void hubbub35_set_request_limit(struct hubbub *hubbub,
				       int memory_channel_count,
				       int words_per_channel)
{
	struct dcn20_hubbub *hubbub2 = TO_DCN20_HUBBUB(hubbub);

	uint32_t request_limit = 3 * memory_channel_count * words_per_channel / 4;

	ASSERT((request_limit & (~0xFFF)) == 0); //field is only 24 bits long
	ASSERT(request_limit > 0); //field is only 24 bits long

	if (request_limit > 0xFFF)
		request_limit = 0xFFF;

	if (request_limit > 0)
		REG_UPDATE(SDPIF_REQUEST_RATE_LIMIT, SDPIF_REQUEST_RATE_LIMIT, request_limit);
}*/

static const struct hubbub_funcs hubbub35_funcs = {
	.update_dchub = hubbub2_update_dchub,
	.init_dchub_sys_ctx = hubbub31_init_dchub_sys_ctx,
	.init_vm_ctx = hubbub2_init_vm_ctx,
	.dcc_support_swizzle = hubbub3_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub2_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub3_get_dcc_compression_cap,
	.wm_read_state = hubbub35_wm_read_state,
	.get_dchub_ref_freq = hubbub35_get_dchub_ref_freq,
	.program_watermarks = hubbub35_program_watermarks,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.verify_allow_pstate_change_high = hubbub1_verify_allow_pstate_change_high,
	.force_wm_propagate_to_pipes = hubbub32_force_wm_propagate_to_pipes,
	.force_pstate_change_control = hubbub3_force_pstate_change_control,
	.init_watermarks = hubbub35_init_watermarks,
	.program_det_size = dcn32_program_det_size,
	.program_compbuf_size = dcn35_program_compbuf_size,
	.init_crb = dcn35_init_crb,
	.hubbub_read_state = hubbub2_read_state,
	.force_usr_retraining_allow = hubbub32_force_usr_retraining_allow,
	.dchubbub_init = hubbub35_init,
	.get_det_sizes = hubbub3_get_det_sizes,
	.compbuf_config_error = hubbub3_compbuf_config_error,
};

void hubbub35_construct(struct dcn20_hubbub *hubbub2,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask,
	int det_size_kb,
	int pixel_chunk_size_kb,
	int config_return_buffer_size_kb)
{
	hubbub2->base.ctx = ctx;
	hubbub2->base.funcs = &hubbub35_funcs;
	hubbub2->regs = hubbub_regs;
	hubbub2->shifts = hubbub_shift;
	hubbub2->masks = hubbub_mask;

	hubbub2->debug_test_index_pstate = 0xB;
	hubbub2->detile_buf_size = det_size_kb * 1024;
	hubbub2->pixel_chunk_size = pixel_chunk_size_kb * 1024;
	hubbub2->crb_size_segs = config_return_buffer_size_kb / DCN35_CRB_SEGMENT_SIZE_KB; /*todo*/
}
