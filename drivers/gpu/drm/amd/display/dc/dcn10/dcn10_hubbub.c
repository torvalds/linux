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
	hubbub->ctx
#define REG(reg)\
	hubbub->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hubbub->shifts->field_name, hubbub->masks->field_name

void hubbub1_wm_read_state(struct hubbub *hubbub,
		struct dcn_hubbub_wm *wm)
{
	struct dcn_hubbub_wm_set *s;

	s = &wm->sets[0];
	s->wm_set = 0;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A);
	s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_A);
	s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A);
	s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A);
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A);

	s = &wm->sets[1];
	s->wm_set = 1;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B);
	s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B);
	s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B);
	s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B);
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B);

	s = &wm->sets[2];
	s->wm_set = 2;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C);
	s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C);
	s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C);
	s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C);
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C);

	s = &wm->sets[3];
	s->wm_set = 3;
	s->data_urgent = REG_READ(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D);
	s->pte_meta_urgent = REG_READ(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D);
	s->sr_enter = REG_READ(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D);
	s->sr_exit = REG_READ(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D);
	s->dram_clk_chanage = REG_READ(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D);
}

void verify_allow_pstate_change_high(
	struct hubbub *hubbub)
{
	/* pstate latency is ~20us so if we wait over 40us and pstate allow
	 * still not asserted, we are probably stuck and going to hang
	 *
	 * TODO: Figure out why it takes ~100us on linux
	 * pstate takes around ~100us on linux. Unknown currently as to
	 * why it takes that long on linux
	 */
	static unsigned int pstate_wait_timeout_us = 200;
	static unsigned int pstate_wait_expected_timeout_us = 40;
	static unsigned int max_sampled_pstate_wait_us; /* data collection */
	static bool forced_pstate_allow; /* help with revert wa */
	static bool should_log_hw_state; /* prevent hw state log by default */

	unsigned int debug_index = 0x7;
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

	/* description "3-0:   Pipe0 cursor0 QOS
	 * 7-4:   Pipe1 cursor0 QOS
	 * 11-8:  Pipe2 cursor0 QOS
	 * 15-12: Pipe3 cursor0 QOS
	 * 16:    Pipe0 Plane0 Allow Pstate Change
	 * 17:    Pipe1 Plane0 Allow Pstate Change
	 * 18:    Pipe2 Plane0 Allow Pstate Change
	 * 19:    Pipe3 Plane0 Allow Pstate Change
	 * 20:    Pipe0 Plane1 Allow Pstate Change
	 * 21:    Pipe1 Plane1 Allow Pstate Change
	 * 22:    Pipe2 Plane1 Allow Pstate Change
	 * 23:    Pipe3 Plane1 Allow Pstate Change
	 * 24:    Pipe0 cursor0 Allow Pstate Change
	 * 25:    Pipe1 cursor0 Allow Pstate Change
	 * 26:    Pipe2 cursor0 Allow Pstate Change
	 * 27:    Pipe3 cursor0 Allow Pstate Change
	 * 28:    WB0 Allow Pstate Change
	 * 29:    WB1 Allow Pstate Change
	 * 30:    Arbiter's allow_pstate_change
	 * 31:    SOC pstate change request
	 */

	REG_WRITE(DCHUBBUB_TEST_DEBUG_INDEX, debug_index);

	for (i = 0; i < pstate_wait_timeout_us; i++) {
		debug_data = REG_READ(DCHUBBUB_TEST_DEBUG_DATA);

		if (debug_data & (1 << 30)) {

			if (i > pstate_wait_expected_timeout_us)
				dm_logger_write(hubbub->ctx->logger, LOG_WARNING,
						"pstate took longer than expected ~%dus\n",
						i);

			return;
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

	if (should_log_hw_state) {
		dcn10_log_hw_state(hubbub->ctx->dc);
	}

	dm_logger_write(hubbub->ctx->logger, LOG_WARNING,
			"pstate TEST_DEBUG_DATA: 0x%X\n",
			debug_data);
	BREAK_TO_DEBUGGER();
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


void program_watermarks(
		struct hubbub *hubbub,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz)
{
	uint32_t force_en = hubbub->ctx->dc->debug.disable_stutter ? 1 : 0;
	/*
	 * Need to clamp to max of the register values (i.e. no wrap)
	 * for dcn1, all wm registers are 21-bit wide
	 */
	uint32_t prog_wm_value;

	REG_UPDATE(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 0);

	/* Repeat for water mark set A, B, C and D. */
	/* clock state A */
	prog_wm_value = convert_and_clamp(watermarks->a.urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, prog_wm_value);

	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"URGENCY_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->a.urgent_ns, prog_wm_value);

	prog_wm_value = convert_and_clamp(watermarks->a.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_A, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"PTE_META_URGENCY_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->a.pte_meta_urgent_ns, prog_wm_value);

	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A)) {
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, prog_wm_value);
		dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_ENTER_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
		dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_exit_ns, prog_wm_value);
	}

	prog_wm_value = convert_and_clamp(
			watermarks->a.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"DRAM_CLK_CHANGE_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n\n",
		watermarks->a.cstate_pstate.pstate_change_ns, prog_wm_value);


	/* clock state B */
	prog_wm_value = convert_and_clamp(
			watermarks->b.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"URGENCY_WATERMARK_B calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->b.urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->b.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"PTE_META_URGENCY_WATERMARK_B calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->b.pte_meta_urgent_ns, prog_wm_value);


	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B)) {
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, prog_wm_value);
		dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_ENTER_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, prog_wm_value);
		dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_exit_ns, prog_wm_value);
	}

	prog_wm_value = convert_and_clamp(
			watermarks->b.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"DRAM_CLK_CHANGE_WATERMARK_B calculated =%d\n\n"
		"HW register value = 0x%x\n",
		watermarks->b.cstate_pstate.pstate_change_ns, prog_wm_value);

	/* clock state C */
	prog_wm_value = convert_and_clamp(
			watermarks->c.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"URGENCY_WATERMARK_C calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->c.urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->c.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"PTE_META_URGENCY_WATERMARK_C calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->c.pte_meta_urgent_ns, prog_wm_value);


	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C)) {
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, prog_wm_value);
		dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_ENTER_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, prog_wm_value);
		dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_EXIT_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_exit_ns, prog_wm_value);
	}

	prog_wm_value = convert_and_clamp(
			watermarks->c.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"DRAM_CLK_CHANGE_WATERMARK_C calculated =%d\n\n"
		"HW register value = 0x%x\n",
		watermarks->c.cstate_pstate.pstate_change_ns, prog_wm_value);

	/* clock state D */
	prog_wm_value = convert_and_clamp(
			watermarks->d.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"URGENCY_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->d.urgent_ns, prog_wm_value);

	prog_wm_value = convert_and_clamp(
			watermarks->d.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"PTE_META_URGENCY_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->d.pte_meta_urgent_ns, prog_wm_value);


	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D)) {
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, prog_wm_value);
		dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_ENTER_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, prog_wm_value);
		dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_EXIT_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_exit_ns, prog_wm_value);
	}


	prog_wm_value = convert_and_clamp(
			watermarks->d.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, prog_wm_value);
	dm_logger_write(hubbub->ctx->logger, LOG_BANDWIDTH_CALCS,
		"DRAM_CLK_CHANGE_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n\n",
		watermarks->d.cstate_pstate.pstate_change_ns, prog_wm_value);

	REG_UPDATE(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 1);

	REG_UPDATE(DCHUBBUB_ARB_SAT_LEVEL,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 68);

	REG_UPDATE_2(DCHUBBUB_ARB_DRAM_STATE_CNTL,
			DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_VALUE, 0,
			DCHUBBUB_ARB_ALLOW_SELF_REFRESH_FORCE_ENABLE, force_en);

#if 0
	REG_UPDATE_2(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_DONE_INTERRUPT_DISABLE, 1,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 1);
#endif
}

void hubbub1_update_dchub(
	struct hubbub *hubbub,
	struct dchub_init_data *dh_data)
{
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

void toggle_watermark_change_req(struct hubbub *hubbub)
{
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

static const struct hubbub_funcs hubbub1_funcs = {
	.update_dchub = hubbub1_update_dchub
};

void hubbub1_construct(struct hubbub *hubbub,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask)
{
	hubbub->ctx = ctx;

	hubbub->funcs = &hubbub1_funcs;

	hubbub->regs = hubbub_regs;
	hubbub->shifts = hubbub_shift;
	hubbub->masks = hubbub_mask;

}

