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

#include "dm_services.h"
#include "dcn10_hubp.h"
#include "dcn10_hubbub.h"
#include "reg_helper.h"

#define CTX \
	hubbub1->base.ctx
#define DC_LOGGER \
	hubbub1->base.ctx->logger
#define REG(reg)\
	hubbub1->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hubbub1->shifts->field_name, hubbub1->masks->field_name

void hubbub1_wm_read_state(struct hubbub *hubbub,
		struct dcn_hubbub_wm *wm)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);
	struct dcn_hubbub_wm_set *s;

	memset(wm, 0, sizeof(struct dcn_hubbub_wm));

	s = &wm->sets[0];
	s->wm_set = 0;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A);
	s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_A);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A);
	}
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A);

	s = &wm->sets[1];
	s->wm_set = 1;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B);
	s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B);
	}
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B);

	s = &wm->sets[2];
	s->wm_set = 2;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C);
	s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C);
	}
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C);

	s = &wm->sets[3];
	s->wm_set = 3;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D);
	s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D);
	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D)) {
		s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D);
		s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D);
	}
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D);
}

void hubbub1_allow_self_refresh_control(struct hubbub *hubbub, bool allow)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);
	/*
	 * DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE = 1 means do not allow stutter
	 * DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE = 0 means allow stutter
	 */

	REG_UPDATE_2(DCHUBBUB_ARB_DRAM_STATE_CNTL,
			DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_VALUE, 0,
			DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE, !allow);
}

bool hubbub1_is_allow_self_refresh_enabled(struct hubbub *hubbub)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);
	uint32_t enable = 0;

	REG_GET(DCHUBBUB_ARB_DRAM_STATE_CNTL,
			DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE, &enable);

	return enable ? true : false;
}


bool hubbub1_verify_allow_pstate_change_high(
	struct hubbub *hubbub)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);

	/* pstate latency is ~20us so if we wait over 40us and pstate allow
	 * still not asserted, we are probably stuck and going to hang
	 *
	 * TODO: Figure out why it takes ~100us on linux
	 * pstate takes around ~100us (up to 200us) on linux. Unknown currently
	 * as to why it takes that long on linux
	 */
	const unsigned int pstate_wait_timeout_us = 200;
	const unsigned int pstate_wait_expected_timeout_us = 180;
	static unsigned int max_sampled_pstate_wait_us; /* data collection */
	static bool forced_pstate_allow; /* help with revert wa */

	unsigned int debug_data;
	unsigned int i;

	if (forced_pstate_allow) {
		/* we hacked to force pstate allow to prevent hang last time
		 * we verify_allow_pstate_change_high.  so disable force
		 * here so we can check status
		 */
		REG_UPDATE_2(DCHUBBUB_ARB_DRAM_STATE_CNTL,
			     DCHUBBUB_ARB_ALLOW_PSTATE_CHANGE_FORCE_VALUE, 0,
			     DCHUBBUB_ARB_ALLOW_PSTATE_CHANGE_FORCE_ENABLE, 0);
		forced_pstate_allow = false;
	}

	/* The following table only applies to DCN1 and DCN2,
	 * for newer DCNs, need to consult with HW IP folks to read RTL
	 * HUBBUB:DCHUBBUB_TEST_ARB_DEBUG10 DCHUBBUBDEBUGIND:0xB
	 * description
	 * 0:     Pipe0 Plane0 Allow Pstate Change
	 * 1:     Pipe0 Plane1 Allow Pstate Change
	 * 2:     Pipe0 Cursor0 Allow Pstate Change
	 * 3:     Pipe0 Cursor1 Allow Pstate Change
	 * 4:     Pipe1 Plane0 Allow Pstate Change
	 * 5:     Pipe1 Plane1 Allow Pstate Change
	 * 6:     Pipe1 Cursor0 Allow Pstate Change
	 * 7:     Pipe1 Cursor1 Allow Pstate Change
	 * 8:     Pipe2 Plane0 Allow Pstate Change
	 * 9:     Pipe2 Plane1 Allow Pstate Change
	 * 10:    Pipe2 Cursor0 Allow Pstate Change
	 * 11:    Pipe2 Cursor1 Allow Pstate Change
	 * 12:    Pipe3 Plane0 Allow Pstate Change
	 * 13:    Pipe3 Plane1 Allow Pstate Change
	 * 14:    Pipe3 Cursor0 Allow Pstate Change
	 * 15:    Pipe3 Cursor1 Allow Pstate Change
	 * 16:    Pipe4 Plane0 Allow Pstate Change
	 * 17:    Pipe4 Plane1 Allow Pstate Change
	 * 18:    Pipe4 Cursor0 Allow Pstate Change
	 * 19:    Pipe4 Cursor1 Allow Pstate Change
	 * 20:    Pipe5 Plane0 Allow Pstate Change
	 * 21:    Pipe5 Plane1 Allow Pstate Change
	 * 22:    Pipe5 Cursor0 Allow Pstate Change
	 * 23:    Pipe5 Cursor1 Allow Pstate Change
	 * 24:    Pipe6 Plane0 Allow Pstate Change
	 * 25:    Pipe6 Plane1 Allow Pstate Change
	 * 26:    Pipe6 Cursor0 Allow Pstate Change
	 * 27:    Pipe6 Cursor1 Allow Pstate Change
	 * 28:    WB0 Allow Pstate Change
	 * 29:    WB1 Allow Pstate Change
	 * 30:    Arbiter's allow_pstate_change
	 * 31:    SOC pstate change request
	 */

	REG_WRITE(DCHUBBUB_TEST_DEBUG_INDEX, hubbub1->debug_test_index_pstate);

	for (i = 0; i < pstate_wait_timeout_us; i++) {
		debug_data = REG_READ(DCHUBBUB_TEST_DEBUG_DATA);

		if (debug_data & (1 << 30)) {

			if (i > pstate_wait_expected_timeout_us)
				DC_LOG_WARNING("pstate took longer than expected ~%dus\n",
						i);

			return true;
		}
		if (max_sampled_pstate_wait_us < i)
			max_sampled_pstate_wait_us = i;

		udelay(1);
	}

	/* force pstate allow to prevent system hang
	 * and break to debugger to investigate
	 */
	REG_UPDATE_2(DCHUBBUB_ARB_DRAM_STATE_CNTL,
		     DCHUBBUB_ARB_ALLOW_PSTATE_CHANGE_FORCE_VALUE, 1,
		     DCHUBBUB_ARB_ALLOW_PSTATE_CHANGE_FORCE_ENABLE, 1);
	forced_pstate_allow = true;

	DC_LOG_WARNING("pstate TEST_DEBUG_DATA: 0x%X\n",
			debug_data);

	return false;
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


void hubbub1_wm_change_req_wa(struct hubbub *hubbub)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);

	REG_UPDATE_SEQ_2(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 0,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 1);
}

bool hubbub1_program_urgent_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);
	uint32_t prog_wm_value;
	bool wm_pending = false;

	/* Repeat for water mark set A, B, C and D. */
	/* clock state A */
	if (safe_to_lower || watermarks->a.urgent_ns > hubbub1->watermarks.a.urgent_ns) {
		hubbub1->watermarks.a.urgent_ns = watermarks->a.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->a.urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.urgent_ns, prog_wm_value);
	} else if (watermarks->a.urgent_ns < hubbub1->watermarks.a.urgent_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.pte_meta_urgent_ns > hubbub1->watermarks.a.pte_meta_urgent_ns) {
		hubbub1->watermarks.a.pte_meta_urgent_ns = watermarks->a.pte_meta_urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->a.pte_meta_urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("PTE_META_URGENCY_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.pte_meta_urgent_ns, prog_wm_value);
	} else if (watermarks->a.pte_meta_urgent_ns < hubbub1->watermarks.a.pte_meta_urgent_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.urgent_ns > hubbub1->watermarks.b.urgent_ns) {
		hubbub1->watermarks.b.urgent_ns = watermarks->b.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->b.urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.urgent_ns, prog_wm_value);
	} else if (watermarks->b.urgent_ns < hubbub1->watermarks.b.urgent_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->b.pte_meta_urgent_ns > hubbub1->watermarks.b.pte_meta_urgent_ns) {
		hubbub1->watermarks.b.pte_meta_urgent_ns = watermarks->b.pte_meta_urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->b.pte_meta_urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("PTE_META_URGENCY_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.pte_meta_urgent_ns, prog_wm_value);
	} else if (watermarks->b.pte_meta_urgent_ns < hubbub1->watermarks.b.pte_meta_urgent_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.urgent_ns > hubbub1->watermarks.c.urgent_ns) {
		hubbub1->watermarks.c.urgent_ns = watermarks->c.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->c.urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.urgent_ns, prog_wm_value);
	} else if (watermarks->c.urgent_ns < hubbub1->watermarks.c.urgent_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->c.pte_meta_urgent_ns > hubbub1->watermarks.c.pte_meta_urgent_ns) {
		hubbub1->watermarks.c.pte_meta_urgent_ns = watermarks->c.pte_meta_urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->c.pte_meta_urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("PTE_META_URGENCY_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.pte_meta_urgent_ns, prog_wm_value);
	} else if (watermarks->c.pte_meta_urgent_ns < hubbub1->watermarks.c.pte_meta_urgent_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.urgent_ns > hubbub1->watermarks.d.urgent_ns) {
		hubbub1->watermarks.d.urgent_ns = watermarks->d.urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->d.urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, 0,
				DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, prog_wm_value);

		DC_LOG_BANDWIDTH_CALCS("URGENCY_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.urgent_ns, prog_wm_value);
	} else if (watermarks->d.urgent_ns < hubbub1->watermarks.d.urgent_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->d.pte_meta_urgent_ns > hubbub1->watermarks.d.pte_meta_urgent_ns) {
		hubbub1->watermarks.d.pte_meta_urgent_ns = watermarks->d.pte_meta_urgent_ns;
		prog_wm_value = convert_and_clamp(watermarks->d.pte_meta_urgent_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("PTE_META_URGENCY_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.pte_meta_urgent_ns, prog_wm_value);
	} else if (watermarks->d.pte_meta_urgent_ns < hubbub1->watermarks.d.pte_meta_urgent_ns)
		wm_pending = true;

	return wm_pending;
}

bool hubbub1_program_stutter_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);
	uint32_t prog_wm_value;
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub1->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub1->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub1->watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->a.cstate_pstate.cstate_exit_ns
			> hubbub1->watermarks.a.cstate_pstate.cstate_exit_ns) {
		hubbub1->watermarks.a.cstate_pstate.cstate_exit_ns =
				watermarks->a.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.cstate_exit_ns
			< hubbub1->watermarks.a.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub1->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub1->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub1->watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->b.cstate_pstate.cstate_exit_ns
			> hubbub1->watermarks.b.cstate_pstate.cstate_exit_ns) {
		hubbub1->watermarks.b.cstate_pstate.cstate_exit_ns =
				watermarks->b.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.cstate_exit_ns
			< hubbub1->watermarks.b.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub1->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub1->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub1->watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->c.cstate_pstate.cstate_exit_ns
			> hubbub1->watermarks.c.cstate_pstate.cstate_exit_ns) {
		hubbub1->watermarks.c.cstate_pstate.cstate_exit_ns =
				watermarks->c.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.cstate_exit_ns
			< hubbub1->watermarks.c.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns
			> hubbub1->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns) {
		hubbub1->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns =
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_ENTER_EXIT_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns
			< hubbub1->watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns)
		wm_pending = true;

	if (safe_to_lower || watermarks->d.cstate_pstate.cstate_exit_ns
			> hubbub1->watermarks.d.cstate_pstate.cstate_exit_ns) {
		hubbub1->watermarks.d.cstate_pstate.cstate_exit_ns =
				watermarks->d.cstate_pstate.cstate_exit_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("SR_EXIT_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_exit_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.cstate_exit_ns
			< hubbub1->watermarks.d.cstate_pstate.cstate_exit_ns)
		wm_pending = true;

	return wm_pending;
}

bool hubbub1_program_pstate_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);
	uint32_t prog_wm_value;
	bool wm_pending = false;

	/* clock state A */
	if (safe_to_lower || watermarks->a.cstate_pstate.pstate_change_ns
			> hubbub1->watermarks.a.cstate_pstate.pstate_change_ns) {
		hubbub1->watermarks.a.cstate_pstate.pstate_change_ns =
				watermarks->a.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->a.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->a.cstate_pstate.pstate_change_ns
			< hubbub1->watermarks.a.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* clock state B */
	if (safe_to_lower || watermarks->b.cstate_pstate.pstate_change_ns
			> hubbub1->watermarks.b.cstate_pstate.pstate_change_ns) {
		hubbub1->watermarks.b.cstate_pstate.pstate_change_ns =
				watermarks->b.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->b.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->b.cstate_pstate.pstate_change_ns
			< hubbub1->watermarks.b.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* clock state C */
	if (safe_to_lower || watermarks->c.cstate_pstate.pstate_change_ns
			> hubbub1->watermarks.c.cstate_pstate.pstate_change_ns) {
		hubbub1->watermarks.c.cstate_pstate.pstate_change_ns =
				watermarks->c.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->c.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->c.cstate_pstate.pstate_change_ns
			< hubbub1->watermarks.c.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	/* clock state D */
	if (safe_to_lower || watermarks->d.cstate_pstate.pstate_change_ns
			> hubbub1->watermarks.d.cstate_pstate.pstate_change_ns) {
		hubbub1->watermarks.d.cstate_pstate.pstate_change_ns =
				watermarks->d.cstate_pstate.pstate_change_ns;
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.pstate_change_ns,
				refclk_mhz, 0x1fffff);
		REG_SET(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, 0,
				DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, prog_wm_value);
		DC_LOG_BANDWIDTH_CALCS("DRAM_CLK_CHANGE_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n\n",
			watermarks->d.cstate_pstate.pstate_change_ns, prog_wm_value);
	} else if (watermarks->d.cstate_pstate.pstate_change_ns
			< hubbub1->watermarks.d.cstate_pstate.pstate_change_ns)
		wm_pending = true;

	return wm_pending;
}

bool hubbub1_program_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz,
		bool safe_to_lower)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);
	bool wm_pending = false;
	/*
	 * Need to clamp to max of the register values (i.e. no wrap)
	 * for dcn1, all wm registers are 21-bit wide
	 */
	if (hubbub1_program_urgent_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub1_program_stutter_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	if (hubbub1_program_pstate_watermarks(hubbub, watermarks, refclk_mhz, safe_to_lower))
		wm_pending = true;

	REG_UPDATE(DCHUBBUB_ARB_SAT_LEVEL,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 68);

	hubbub1_allow_self_refresh_control(hubbub, !hubbub->ctx->dc->debug.disable_stutter);

#if 0
	REG_UPDATE_2(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_DONE_INTERRUPT_DISABLE, 1,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 1);
#endif
	return wm_pending;
}

void hubbub1_update_dchub(
	struct hubbub *hubbub,
	struct dchub_init_data *dh_data)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);

	if (REG(DCHUBBUB_SDPIF_FB_TOP) == 0) {
		ASSERT(false);
		/*should not come here*/
		return;
	}
	/* TODO: port code from dal2 */
	switch (dh_data->fb_mode) {
	case FRAME_BUFFER_MODE_ZFB_ONLY:
		/*For ZFB case need to put DCHUB FB BASE and TOP upside down to indicate ZFB mode*/
		REG_UPDATE(DCHUBBUB_SDPIF_FB_TOP,
				SDPIF_FB_TOP, 0);

		REG_UPDATE(DCHUBBUB_SDPIF_FB_BASE,
				SDPIF_FB_BASE, 0x0FFFF);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, dh_data->zfb_phys_addr_base >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, dh_data->zfb_mc_base_addr >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, (dh_data->zfb_mc_base_addr +
						dh_data->zfb_size_in_byte - 1) >> 22);
		break;
	case FRAME_BUFFER_MODE_MIXED_ZFB_AND_LOCAL:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, dh_data->zfb_phys_addr_base >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, dh_data->zfb_mc_base_addr >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, (dh_data->zfb_mc_base_addr +
						dh_data->zfb_size_in_byte - 1) >> 22);
		break;
	case FRAME_BUFFER_MODE_LOCAL_ONLY:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/
		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, 0);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, 0X03FFFF);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, 0);
		break;
	default:
		break;
	}

	dh_data->dchub_initialzied = true;
	dh_data->dchub_info_valid = false;
}

void hubbub1_toggle_watermark_change_req(struct hubbub *hubbub)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);

	uint32_t watermark_change_req;

	REG_GET(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, &watermark_change_req);

	if (watermark_change_req)
		watermark_change_req = 0;
	else
		watermark_change_req = 1;

	REG_UPDATE(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, watermark_change_req);
}

void hubbub1_soft_reset(struct hubbub *hubbub, bool reset)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);

	uint32_t reset_en = reset ? 1 : 0;

	REG_UPDATE(DCHUBBUB_SOFT_RESET,
			DCHUBBUB_GLOBAL_SOFT_RESET, reset_en);
}

static bool hubbub1_dcc_support_swizzle(
		enum swizzle_mode_values swizzle,
		unsigned int bytes_per_element,
		enum segment_order *segment_order_horz,
		enum segment_order *segment_order_vert)
{
	bool standard_swizzle = false;
	bool display_swizzle = false;

	switch (swizzle) {
	case DC_SW_4KB_S:
	case DC_SW_64KB_S:
	case DC_SW_VAR_S:
	case DC_SW_4KB_S_X:
	case DC_SW_64KB_S_X:
	case DC_SW_VAR_S_X:
		standard_swizzle = true;
		break;
	case DC_SW_4KB_D:
	case DC_SW_64KB_D:
	case DC_SW_VAR_D:
	case DC_SW_4KB_D_X:
	case DC_SW_64KB_D_X:
	case DC_SW_VAR_D_X:
		display_swizzle = true;
		break;
	default:
		break;
	}

	if (bytes_per_element == 1 && standard_swizzle) {
		*segment_order_horz = segment_order__contiguous;
		*segment_order_vert = segment_order__na;
		return true;
	}
	if (bytes_per_element == 2 && standard_swizzle) {
		*segment_order_horz = segment_order__non_contiguous;
		*segment_order_vert = segment_order__contiguous;
		return true;
	}
	if (bytes_per_element == 4 && standard_swizzle) {
		*segment_order_horz = segment_order__non_contiguous;
		*segment_order_vert = segment_order__contiguous;
		return true;
	}
	if (bytes_per_element == 8 && standard_swizzle) {
		*segment_order_horz = segment_order__na;
		*segment_order_vert = segment_order__contiguous;
		return true;
	}
	if (bytes_per_element == 8 && display_swizzle) {
		*segment_order_horz = segment_order__contiguous;
		*segment_order_vert = segment_order__non_contiguous;
		return true;
	}

	return false;
}

static bool hubbub1_dcc_support_pixel_format(
		enum surface_pixel_format format,
		unsigned int *bytes_per_element)
{
	/* DML: get_bytes_per_element */
	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		*bytes_per_element = 2;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		*bytes_per_element = 4;
		return true;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		*bytes_per_element = 8;
		return true;
	default:
		return false;
	}
}

static void hubbub1_get_blk256_size(unsigned int *blk256_width, unsigned int *blk256_height,
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

static void hubbub1_det_request_size(
		unsigned int height,
		unsigned int width,
		unsigned int bpe,
		bool *req128_horz_wc,
		bool *req128_vert_wc)
{
	unsigned int detile_buf_size = 164 * 1024;  /* 164KB for DCN1.0 */

	unsigned int blk256_height = 0;
	unsigned int blk256_width = 0;
	unsigned int swath_bytes_horz_wc, swath_bytes_vert_wc;

	hubbub1_get_blk256_size(&blk256_width, &blk256_height, bpe);

	swath_bytes_horz_wc = width * blk256_height * bpe;
	swath_bytes_vert_wc = height * blk256_width * bpe;

	*req128_horz_wc = (2 * swath_bytes_horz_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */

	*req128_vert_wc = (2 * swath_bytes_vert_wc <= detile_buf_size) ?
			false : /* full 256B request */
			true; /* half 128b request */
}

static bool hubbub1_get_dcc_compression_cap(struct hubbub *hubbub,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);
	struct dc *dc = hubbub1->base.ctx->dc;

	/* implement section 1.6.2.1 of DCN1_Programming_Guide.docx */
	enum dcc_control dcc_control;
	unsigned int bpe;
	enum segment_order segment_order_horz, segment_order_vert;
	bool req128_horz_wc, req128_vert_wc;

	memset(output, 0, sizeof(*output));

	if (dc->debug.disable_dcc == DCC_DISABLE)
		return false;

	if (!hubbub1->base.funcs->dcc_support_pixel_format(input->format, &bpe))
		return false;

	if (!hubbub1->base.funcs->dcc_support_swizzle(input->swizzle_mode, bpe,
			&segment_order_horz, &segment_order_vert))
		return false;

	hubbub1_det_request_size(input->surface_size.height,  input->surface_size.width,
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

	if (dc->debug.disable_dcc == DCC_HALF_REQ_DISALBE &&
		dcc_control != dcc_control__256_256_xxx)
		return false;

	switch (dcc_control) {
	case dcc_control__256_256_xxx:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 256;
		output->grph.rgb.independent_64b_blks = false;
		break;
	case dcc_control__128_128_xxx:
		output->grph.rgb.max_uncompressed_blk_size = 128;
		output->grph.rgb.max_compressed_blk_size = 128;
		output->grph.rgb.independent_64b_blks = false;
		break;
	case dcc_control__256_64_64:
		output->grph.rgb.max_uncompressed_blk_size = 256;
		output->grph.rgb.max_compressed_blk_size = 64;
		output->grph.rgb.independent_64b_blks = true;
		break;
	default:
		ASSERT(false);
		break;
	}

	output->capable = true;
	output->const_color_support = false;

	return true;
}

static const struct hubbub_funcs hubbub1_funcs = {
	.update_dchub = hubbub1_update_dchub,
	.dcc_support_swizzle = hubbub1_dcc_support_swizzle,
	.dcc_support_pixel_format = hubbub1_dcc_support_pixel_format,
	.get_dcc_compression_cap = hubbub1_get_dcc_compression_cap,
	.wm_read_state = hubbub1_wm_read_state,
	.program_watermarks = hubbub1_program_watermarks,
	.is_allow_self_refresh_enabled = hubbub1_is_allow_self_refresh_enabled,
	.allow_self_refresh_control = hubbub1_allow_self_refresh_control,
	.verify_allow_pstate_change_high = hubbub1_verify_allow_pstate_change_high,
};

void hubbub1_construct(struct hubbub *hubbub,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask)
{
	struct dcn10_hubbub *hubbub1 = TO_DCN10_HUBBUB(hubbub);

	hubbub1->base.ctx = ctx;

	hubbub1->base.funcs = &hubbub1_funcs;

	hubbub1->regs = hubbub_regs;
	hubbub1->shifts = hubbub_shift;
	hubbub1->masks = hubbub_mask;

	hubbub1->debug_test_index_pstate = 0x7;
	if (ctx->dce_version == DCN_VERSION_1_01)
		hubbub1->debug_test_index_pstate = 0xB;
}

