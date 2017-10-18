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
#include "core_types.h"
#include "resource.h"
#include "custom_float.h"
#include "dcn10_hw_sequencer.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dce/dce_hwseq.h"
#include "abm.h"
#include "dcn10/dcn10_timing_generator.h"
#include "dcn10/dcn10_dpp.h"
#include "dcn10/dcn10_mpc.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "reg_helper.h"
#include "custom_float.h"
#include "dcn10_hubp.h"

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

static void log_mpc_crc(struct dc *dc)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct dce_hwseq *hws = dc->hwseq;

	if (REG(MPC_CRC_RESULT_GB))
		DTN_INFO("MPC_CRC_RESULT_GB:%d MPC_CRC_RESULT_C:%d MPC_CRC_RESULT_AR:%d\n",
		REG_READ(MPC_CRC_RESULT_GB), REG_READ(MPC_CRC_RESULT_C), REG_READ(MPC_CRC_RESULT_AR));
	if (REG(DPP_TOP0_DPP_CRC_VAL_B_A))
		DTN_INFO("DPP_TOP0_DPP_CRC_VAL_B_A:%d DPP_TOP0_DPP_CRC_VAL_R_G:%d\n",
		REG_READ(DPP_TOP0_DPP_CRC_VAL_B_A), REG_READ(DPP_TOP0_DPP_CRC_VAL_R_G));
}

void print_microsec(struct dc_context *dc_ctx, uint32_t ref_cycle)
{
	static const uint32_t ref_clk_mhz = 48;
	static const unsigned int frac = 10;
	uint32_t us_x10 = (ref_cycle * frac) / ref_clk_mhz;

	DTN_INFO("%d.%d \t ",
			us_x10 / frac,
			us_x10 % frac);
}

#define DTN_INFO_MICRO_SEC(ref_cycle) \
	print_microsec(dc_ctx, ref_cycle)

struct dcn_hubbub_wm_set {
	uint32_t wm_set;
	uint32_t data_urgent;
	uint32_t pte_meta_urgent;
	uint32_t sr_enter;
	uint32_t sr_exit;
	uint32_t dram_clk_chanage;
};

struct dcn_hubbub_wm {
	struct dcn_hubbub_wm_set sets[4];
};

static void dcn10_hubbub_wm_read_state(struct dce_hwseq *hws,
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

static void dcn10_log_hubbub_state(struct dc *dc)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct dcn_hubbub_wm wm;
	int i;

	dcn10_hubbub_wm_read_state(dc->hwseq, &wm);

	DTN_INFO("HUBBUB WM: \t data_urgent \t pte_meta_urgent \t "
			"sr_enter \t sr_exit \t dram_clk_change \n");

	for (i = 0; i < 4; i++) {
		struct dcn_hubbub_wm_set *s;

		s = &wm.sets[i];
		DTN_INFO("WM_Set[%d]:\t ", s->wm_set);
		DTN_INFO_MICRO_SEC(s->data_urgent);
		DTN_INFO_MICRO_SEC(s->pte_meta_urgent);
		DTN_INFO_MICRO_SEC(s->sr_enter);
		DTN_INFO_MICRO_SEC(s->sr_exit);
		DTN_INFO_MICRO_SEC(s->dram_clk_chanage);
		DTN_INFO("\n");
	}

	DTN_INFO("\n");
}

static void dcn10_log_hw_state(struct dc *dc)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct resource_pool *pool = dc->res_pool;
	int i;

	DTN_INFO_BEGIN();

	dcn10_log_hubbub_state(dc);

	DTN_INFO("HUBP:\t format \t addr_hi \t width \t height \t "
			"rotation \t mirror \t  sw_mode \t "
			"dcc_en \t blank_en \t ttu_dis \t underflow \t "
			"min_ttu_vblank \t qos_low_wm \t qos_high_wm \n");

	for (i = 0; i < pool->pipe_count; i++) {
		struct hubp *hubp = pool->hubps[i];
		struct dcn_hubp_state s;

		hubp1_read_state(TO_DCN10_HUBP(hubp), &s);

		DTN_INFO("[%d]:\t %xh \t %xh \t %d \t %d \t "
				"%xh \t %xh \t %xh \t "
				"%d \t %d \t %d \t %xh \t",
				i,
				s.pixel_format,
				s.inuse_addr_hi,
				s.viewport_width,
				s.viewport_height,
				s.rotation_angle,
				s.h_mirror_en,
				s.sw_mode,
				s.dcc_en,
				s.blank_en,
				s.ttu_disable,
				s.underflow_status);
		DTN_INFO_MICRO_SEC(s.min_ttu_vblank);
		DTN_INFO_MICRO_SEC(s.qos_level_low_wm);
		DTN_INFO_MICRO_SEC(s.qos_level_high_wm);
		DTN_INFO("\n");
	}
	DTN_INFO("\n");

	DTN_INFO("OTG:\t v_bs \t v_be \t v_ss \t v_se \t vpol \t vmax \t vmin \t "
			"h_bs \t h_be \t h_ss \t h_se \t hpol \t htot \t vtot \t underflow\n");

	for (i = 0; i < pool->res_cap->num_timing_generator; i++) {
		struct timing_generator *tg = pool->timing_generators[i];
		struct dcn_otg_state s = {0};

		tgn10_read_otg_state(DCN10TG_FROM_TG(tg), &s);

		//only print if OTG master is enabled
		if ((s.otg_enabled & 1) == 0)
			continue;

		DTN_INFO("[%d]:\t %d \t %d \t %d \t %d \t "
				"%d \t %d \t %d \t %d \t %d \t %d \t "
				"%d \t %d \t %d \t %d \t %d \t ",
				i,
				s.v_blank_start,
				s.v_blank_end,
				s.v_sync_a_start,
				s.v_sync_a_end,
				s.v_sync_a_pol,
				s.v_total_max,
				s.v_total_min,
				s.h_blank_start,
				s.h_blank_end,
				s.h_sync_a_start,
				s.h_sync_a_end,
				s.h_sync_a_pol,
				s.h_total,
				s.v_total,
				s.underflow_occurred_status);
		DTN_INFO("\n");
	}
	DTN_INFO("\n");

	log_mpc_crc(dc);

	DTN_INFO_END();
}

static void verify_allow_pstate_change_high(
	struct dce_hwseq *hws)
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
				dm_logger_write(hws->ctx->logger, LOG_WARNING,
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
		dcn10_log_hw_state(hws->ctx->dc);
	}

	dm_logger_write(hws->ctx->logger, LOG_WARNING,
			"pstate TEST_DEBUG_DATA: 0x%X\n",
			debug_data);
	BREAK_TO_DEBUGGER();
}

static void enable_dppclk(
	struct dce_hwseq *hws,
	uint8_t plane_id,
	uint32_t requested_pix_clk,
	bool dppclk_div)
{
	dm_logger_write(hws->ctx->logger, LOG_SURFACE,
			"dppclk_rate_control for pipe %d programed to %d\n",
			plane_id,
			dppclk_div);

	if (hws->shifts->DPPCLK_RATE_CONTROL)
		REG_UPDATE_2(DPP_CONTROL[plane_id],
			DPPCLK_RATE_CONTROL, dppclk_div,
			DPP_CLOCK_ENABLE, 1);
	else
		REG_UPDATE(DPP_CONTROL[plane_id],
			DPP_CLOCK_ENABLE, 1);
}

static void enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable)
{
	bool force_on = 1; /* disable power gating */

	if (enable)
		force_on = 0;

	/* DCHUBP0/1/2/3 */
	REG_UPDATE(DOMAIN0_PG_CONFIG, DOMAIN0_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN2_PG_CONFIG, DOMAIN2_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN4_PG_CONFIG, DOMAIN4_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN6_PG_CONFIG, DOMAIN6_POWER_FORCEON, force_on);

	/* DPP0/1/2/3 */
	REG_UPDATE(DOMAIN1_PG_CONFIG, DOMAIN1_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN3_PG_CONFIG, DOMAIN3_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN5_PG_CONFIG, DOMAIN5_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN7_PG_CONFIG, DOMAIN7_POWER_FORCEON, force_on);
}

static void disable_vga(
	struct dce_hwseq *hws)
{
	REG_WRITE(D1VGA_CONTROL, 0);
	REG_WRITE(D2VGA_CONTROL, 0);
	REG_WRITE(D3VGA_CONTROL, 0);
	REG_WRITE(D4VGA_CONTROL, 0);
}

static void dpp_pg_control(
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_dpp_power_gate)
		return;

	switch (dpp_inst) {
	case 0: /* DPP0 */
		REG_UPDATE(DOMAIN1_PG_CONFIG,
				DOMAIN1_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN1_PG_STATUS,
				DOMAIN1_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DPP1 */
		REG_UPDATE(DOMAIN3_PG_CONFIG,
				DOMAIN3_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN3_PG_STATUS,
				DOMAIN3_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DPP2 */
		REG_UPDATE(DOMAIN5_PG_CONFIG,
				DOMAIN5_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN5_PG_STATUS,
				DOMAIN5_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DPP3 */
		REG_UPDATE(DOMAIN7_PG_CONFIG,
				DOMAIN7_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN7_PG_STATUS,
				DOMAIN7_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
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

static void program_watermarks(
		struct dce_hwseq *hws,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz)
{
	uint32_t force_en = hws->ctx->dc->debug.disable_stutter ? 1 : 0;
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

	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"URGENCY_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->a.urgent_ns, prog_wm_value);

	prog_wm_value = convert_and_clamp(watermarks->a.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_A, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"PTE_META_URGENCY_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->a.pte_meta_urgent_ns, prog_wm_value);

	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A)) {
		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, prog_wm_value);
		dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_ENTER_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


		prog_wm_value = convert_and_clamp(
				watermarks->a.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
		dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_EXIT_WATERMARK_A calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->a.cstate_pstate.cstate_exit_ns, prog_wm_value);
	}

	prog_wm_value = convert_and_clamp(
			watermarks->a.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"DRAM_CLK_CHANGE_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n\n",
		watermarks->a.cstate_pstate.pstate_change_ns, prog_wm_value);


	/* clock state B */
	prog_wm_value = convert_and_clamp(
			watermarks->b.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"URGENCY_WATERMARK_B calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->b.urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->b.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"PTE_META_URGENCY_WATERMARK_B calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->b.pte_meta_urgent_ns, prog_wm_value);


	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B)) {
		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, prog_wm_value);
		dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_ENTER_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


		prog_wm_value = convert_and_clamp(
				watermarks->b.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, prog_wm_value);
		dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_EXIT_WATERMARK_B calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->b.cstate_pstate.cstate_exit_ns, prog_wm_value);
	}

	prog_wm_value = convert_and_clamp(
			watermarks->b.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"DRAM_CLK_CHANGE_WATERMARK_B calculated =%d\n\n"
		"HW register value = 0x%x\n",
		watermarks->b.cstate_pstate.pstate_change_ns, prog_wm_value);

	/* clock state C */
	prog_wm_value = convert_and_clamp(
			watermarks->c.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"URGENCY_WATERMARK_C calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->c.urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->c.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"PTE_META_URGENCY_WATERMARK_C calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->c.pte_meta_urgent_ns, prog_wm_value);


	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C)) {
		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, prog_wm_value);
		dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_ENTER_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


		prog_wm_value = convert_and_clamp(
				watermarks->c.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, prog_wm_value);
		dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_EXIT_WATERMARK_C calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->c.cstate_pstate.cstate_exit_ns, prog_wm_value);
	}

	prog_wm_value = convert_and_clamp(
			watermarks->c.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"DRAM_CLK_CHANGE_WATERMARK_C calculated =%d\n\n"
		"HW register value = 0x%x\n",
		watermarks->c.cstate_pstate.pstate_change_ns, prog_wm_value);

	/* clock state D */
	prog_wm_value = convert_and_clamp(
			watermarks->d.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"URGENCY_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->d.urgent_ns, prog_wm_value);

	prog_wm_value = convert_and_clamp(
			watermarks->d.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
		"PTE_META_URGENCY_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->d.pte_meta_urgent_ns, prog_wm_value);


	if (REG(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D)) {
		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, prog_wm_value);
		dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_ENTER_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


		prog_wm_value = convert_and_clamp(
				watermarks->d.cstate_pstate.cstate_exit_ns,
				refclk_mhz, 0x1fffff);
		REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, prog_wm_value);
		dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
			"SR_EXIT_WATERMARK_D calculated =%d\n"
			"HW register value = 0x%x\n",
			watermarks->d.cstate_pstate.cstate_exit_ns, prog_wm_value);
	}


	prog_wm_value = convert_and_clamp(
			watermarks->d.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, prog_wm_value);
	dm_logger_write(hws->ctx->logger, LOG_BANDWIDTH_CALCS,
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


static void dcn10_update_dchub(
	struct dce_hwseq *hws,
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

static void hubp_pg_control(
		struct dce_hwseq *hws,
		unsigned int hubp_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_hubp_power_gate)
		return;

	switch (hubp_inst) {
	case 0: /* DCHUBP0 */
		REG_UPDATE(DOMAIN0_PG_CONFIG,
				DOMAIN0_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN0_PG_STATUS,
				DOMAIN0_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DCHUBP1 */
		REG_UPDATE(DOMAIN2_PG_CONFIG,
				DOMAIN2_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN2_PG_STATUS,
				DOMAIN2_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DCHUBP2 */
		REG_UPDATE(DOMAIN4_PG_CONFIG,
				DOMAIN4_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN4_PG_STATUS,
				DOMAIN4_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DCHUBP3 */
		REG_UPDATE(DOMAIN6_PG_CONFIG,
				DOMAIN6_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN6_PG_STATUS,
				DOMAIN6_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}

static void power_on_plane(
	struct dce_hwseq *hws,
	int plane_id)
{
	if (REG(DC_IP_REQUEST_CNTL)) {
		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 1);
		dpp_pg_control(hws, plane_id, true);
		hubp_pg_control(hws, plane_id, true);
		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 0);
		dm_logger_write(hws->ctx->logger, LOG_DEBUG,
				"Un-gated front end for pipe %d\n", plane_id);
	}
}

static void undo_DEGVIDCN10_253_wa(struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = dc->res_pool->hubps[0];
	int pwr_status = 0;

	REG_GET(DOMAIN0_PG_STATUS, DOMAIN0_PGFSM_PWR_STATUS, &pwr_status);
	/* Don't need to blank if hubp is power gated*/
	if (pwr_status == 2)
		return;

	hubp->funcs->set_blank(hubp, true);

	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 1);

	hubp_pg_control(hws, 0, false);
	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 0);
}

static void apply_DEGVIDCN10_253_wa(struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = dc->res_pool->hubps[0];

	if (dc->debug.disable_stutter)
		return;

	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 1);

	hubp_pg_control(hws, 0, true);
	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 0);

	hubp->funcs->set_hubp_blank_en(hubp, false);
}

static void bios_golden_init(struct dc *dc)
{
	struct dc_bios *bp = dc->ctx->dc_bios;
	int i;

	/* initialize dcn global */
	bp->funcs->enable_disp_power_gating(bp,
			CONTROLLER_ID_D0, ASIC_PIPE_INIT);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		/* initialize dcn per pipe */
		bp->funcs->enable_disp_power_gating(bp,
				CONTROLLER_ID_D0 + i, ASIC_PIPE_DISABLE);
	}
}

static void dcn10_init_hw(struct dc *dc)
{
	int i;
	struct abm *abm = dc->res_pool->abm;
	struct dce_hwseq *hws = dc->hwseq;

	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		REG_WRITE(REFCLK_CNTL, 0);
		REG_UPDATE(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_ENABLE, 1);
		REG_WRITE(DIO_MEM_PWR_CTRL, 0);

		if (!dc->debug.disable_clock_gate) {
			/* enable all DCN clock gating */
			REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

			REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

			REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
		}

		enable_power_gating_plane(dc->hwseq, true);
		return;
	}
	/* end of FPGA. Below if real ASIC */

	bios_golden_init(dc);

	disable_vga(dc->hwseq);

	for (i = 0; i < dc->link_count; i++) {
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector).
		 */
		struct dc_link *link = dc->links[i];

		link->link_enc->funcs->hw_init(link->link_enc);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct dpp *dpp = dc->res_pool->dpps[i];
		struct timing_generator *tg = dc->res_pool->timing_generators[i];

		dpp->funcs->dpp_reset(dpp);
		dc->res_pool->mpc->funcs->remove(
				dc->res_pool->mpc, &(dc->res_pool->opps[i]->mpc_tree),
				dc->res_pool->opps[i]->inst, i);

		/* Blank controller using driver code instead of
		 * command table.
		 */
		tg->funcs->set_blank(tg, true);
		hwss_wait_for_blank_complete(tg);
	}

	for (i = 0; i < dc->res_pool->audio_count; i++) {
		struct audio *audio = dc->res_pool->audios[i];

		audio->funcs->hw_init(audio);
	}

	if (abm != NULL) {
		abm->funcs->init_backlight(abm);
		abm->funcs->abm_init(abm);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

		REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	enable_power_gating_plane(dc->hwseq, true);
}

static enum dc_status dcn10_prog_pixclk_crtc_otg(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	enum dc_color_space color_space;
	struct tg_color black_color = {0};
	bool enableStereo    = stream->timing.timing_3d_format == TIMING_3D_FORMAT_NONE ?
			false:true;
	bool rightEyePolarity = stream->timing.flags.RIGHT_EYE_3D_POLARITY;


	/* by upper caller loop, pipe0 is parent pipe and be called first.
	 * back end is set up by for pipe0. Other children pipe share back end
	 * with pipe 0. No program is needed.
	 */
	if (pipe_ctx->top_pipe != NULL)
		return DC_OK;

	/* TODO check if timing_changed, disable stream if timing changed */

	/* HW program guide assume display already disable
	 * by unplug sequence. OTG assume stop.
	 */
	pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, true);

	if (false == pipe_ctx->clock_source->funcs->program_pix_clk(
			pipe_ctx->clock_source,
			&pipe_ctx->stream_res.pix_clk_params,
			&pipe_ctx->pll_settings)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}
	pipe_ctx->stream_res.tg->dlg_otg_param.vready_offset = pipe_ctx->pipe_dlg_param.vready_offset;
	pipe_ctx->stream_res.tg->dlg_otg_param.vstartup_start = pipe_ctx->pipe_dlg_param.vstartup_start;
	pipe_ctx->stream_res.tg->dlg_otg_param.vupdate_offset = pipe_ctx->pipe_dlg_param.vupdate_offset;
	pipe_ctx->stream_res.tg->dlg_otg_param.vupdate_width = pipe_ctx->pipe_dlg_param.vupdate_width;

	pipe_ctx->stream_res.tg->dlg_otg_param.signal =  pipe_ctx->stream->signal;

	pipe_ctx->stream_res.tg->funcs->program_timing(
			pipe_ctx->stream_res.tg,
			&stream->timing,
			true);

	pipe_ctx->stream_res.opp->funcs->opp_set_stereo_polarity(
				pipe_ctx->stream_res.opp,
				enableStereo,
				rightEyePolarity);

#if 0 /* move to after enable_crtc */
	/* TODO: OPP FMT, ABM. etc. should be done here. */
	/* or FPGA now. instance 0 only. TODO: move to opp.c */

	inst_offset = reg_offsets[pipe_ctx->stream_res.tg->inst].fmt;

	pipe_ctx->stream_res.opp->funcs->opp_program_fmt(
				pipe_ctx->stream_res.opp,
				&stream->bit_depth_params,
				&stream->clamping);
#endif
	/* program otg blank color */
	color_space = stream->output_color_space;
	color_space_to_black_color(dc, color_space, &black_color);
	pipe_ctx->stream_res.tg->funcs->set_blank_color(
			pipe_ctx->stream_res.tg,
			&black_color);

	pipe_ctx->stream_res.tg->funcs->set_blank(pipe_ctx->stream_res.tg, true);
	hwss_wait_for_blank_complete(pipe_ctx->stream_res.tg);

	/* VTG is  within DCHUB command block. DCFCLK is always on */
	if (false == pipe_ctx->stream_res.tg->funcs->enable_crtc(pipe_ctx->stream_res.tg)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}

	/* TODO program crtc source select for non-virtual signal*/
	/* TODO program FMT */
	/* TODO setup link_enc */
	/* TODO set stream attributes */
	/* TODO program audio */
	/* TODO enable stream if timing changed */
	/* TODO unblank stream if DP */

	return DC_OK;
}

static void reset_back_end_for_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	int i;

	if (pipe_ctx->stream_res.stream_enc == NULL) {
		pipe_ctx->stream = NULL;
		return;
	}

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		/* DPMS may already disable */
		if (!pipe_ctx->stream->dpms_off)
			core_link_disable_stream(pipe_ctx, FREE_ACQUIRED_RESOURCE);
	}

	/* by upper caller loop, parent pipe: pipe0, will be reset last.
	 * back end share by all pipes and will be disable only when disable
	 * parent pipe.
	 */
	if (pipe_ctx->top_pipe == NULL) {
		pipe_ctx->stream_res.tg->funcs->disable_crtc(pipe_ctx->stream_res.tg);

		pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, false);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (&dc->current_state->res_ctx.pipe_ctx[i] == pipe_ctx)
			break;

	if (i == dc->res_pool->pipe_count)
		return;

	pipe_ctx->stream = NULL;
	dm_logger_write(dc->ctx->logger, LOG_DEBUG,
					"Reset back end for pipe %d, tg:%d\n",
					pipe_ctx->pipe_idx, pipe_ctx->stream_res.tg->inst);
}

/* trigger HW to start disconnect plane from stream on the next vsync */
static void plane_atomic_disconnect(struct dc *dc,
		int fe_idx)
{
	struct hubp *hubp = dc->res_pool->hubps[fe_idx];
	struct mpc *mpc = dc->res_pool->mpc;
	int opp_id, z_idx;
	int mpcc_id = -1;

	/* look at tree rather than mi here to know if we already reset */
	for (opp_id = 0; opp_id < dc->res_pool->pipe_count; opp_id++) {
		struct output_pixel_processor *opp = dc->res_pool->opps[opp_id];

		for (z_idx = 0; z_idx < opp->mpc_tree.num_pipes; z_idx++) {
			if (opp->mpc_tree.dpp[z_idx] == fe_idx) {
				mpcc_id = opp->mpc_tree.mpcc[z_idx];
				break;
			}
		}
		if (mpcc_id != -1)
			break;
	}
	/*Already reset*/
	if (opp_id == dc->res_pool->pipe_count)
		return;

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);
	hubp->funcs->dcc_control(hubp, false, false);
	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);

	mpc->funcs->remove(mpc, &(dc->res_pool->opps[opp_id]->mpc_tree),
			dc->res_pool->opps[opp_id]->inst, fe_idx);
}

/* disable HW used by plane.
 * note:  cannot disable until disconnect is complete */
static void plane_atomic_disable(struct dc *dc,
		int fe_idx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = dc->res_pool->hubps[fe_idx];
	struct mpc *mpc = dc->res_pool->mpc;
	int opp_id = hubp->opp_id;

	if (opp_id == 0xf)
		return;

	mpc->funcs->wait_for_idle(mpc, hubp->mpcc_id);
	dc->res_pool->opps[hubp->opp_id]->mpcc_disconnect_pending[hubp->mpcc_id] = false;
	/*dm_logger_write(dc->ctx->logger, LOG_ERROR,
			"[debug_mpo: atomic disable finished on mpcc %d]\n",
			fe_idx);*/

	hubp->funcs->set_blank(hubp, true);

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);

	REG_UPDATE(HUBP_CLK_CNTL[fe_idx],
			HUBP_CLOCK_ENABLE, 0);
	REG_UPDATE(DPP_CONTROL[fe_idx],
			DPP_CLOCK_ENABLE, 0);

	if (dc->res_pool->opps[opp_id]->mpc_tree.num_pipes == 0)
		REG_UPDATE(OPP_PIPE_CONTROL[opp_id],
				OPP_PIPE_CLOCK_EN, 0);

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);
}

/*
 * kill power to plane hw
 * note: cannot power down until plane is disable
 */
static void plane_atomic_power_down(struct dc *dc, int fe_idx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dpp *dpp = dc->res_pool->dpps[fe_idx];

	if (REG(DC_IP_REQUEST_CNTL)) {
		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 1);
		dpp_pg_control(hws, fe_idx, false);
		hubp_pg_control(hws, fe_idx, false);
		dpp->funcs->dpp_reset(dpp);
		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 0);
		dm_logger_write(dc->ctx->logger, LOG_DEBUG,
				"Power gated front end %d\n", fe_idx);

		if (dc->debug.sanity_checks)
			verify_allow_pstate_change_high(dc->hwseq);
	}
}


static void reset_front_end(
		struct dc *dc,
		int fe_idx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct timing_generator *tg;
	int opp_id = dc->res_pool->hubps[fe_idx]->opp_id;

	/*Already reset*/
	if (opp_id == 0xf)
		return;

	tg = dc->res_pool->timing_generators[opp_id];
	tg->funcs->lock(tg);

	plane_atomic_disconnect(dc, fe_idx);

	REG_UPDATE(OTG_GLOBAL_SYNC_STATUS[tg->inst], VUPDATE_NO_LOCK_EVENT_CLEAR, 1);
	tg->funcs->unlock(tg);

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(hws);

	if (tg->ctx->dce_environment != DCE_ENV_FPGA_MAXIMUS)
		REG_WAIT(OTG_GLOBAL_SYNC_STATUS[tg->inst],
				VUPDATE_NO_LOCK_EVENT_OCCURRED, 1,
				1, 100000);

	plane_atomic_disable(dc, fe_idx);

	dm_logger_write(dc->ctx->logger, LOG_DC,
					"Reset front end %d\n",
					fe_idx);
}

static void dcn10_power_down_fe(struct dc *dc, int fe_idx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dpp *dpp = dc->res_pool->dpps[fe_idx];

	reset_front_end(dc, fe_idx);

	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 1);
	dpp_pg_control(hws, fe_idx, false);
	hubp_pg_control(hws, fe_idx, false);
	dpp->funcs->dpp_reset(dpp);
	REG_SET(DC_IP_REQUEST_CNTL, 0,
			IP_REQUEST_EN, 0);
	dm_logger_write(dc->ctx->logger, LOG_DEBUG,
			"Power gated front end %d\n", fe_idx);

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);
}

static void reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context)
{
	int i;

	/* Reset Front End*/
	/* Lock*/
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *cur_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];
		struct timing_generator *tg = cur_pipe_ctx->stream_res.tg;

		if (cur_pipe_ctx->stream)
			tg->funcs->lock(tg);
	}
	/* Disconnect*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!pipe_ctx->stream ||
				!pipe_ctx->plane_state ||
				pipe_need_reprogram(pipe_ctx_old, pipe_ctx)) {

			plane_atomic_disconnect(dc, i);
		}
	}
	/* Unlock*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
		struct pipe_ctx *cur_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];
		struct timing_generator *tg = cur_pipe_ctx->stream_res.tg;

		if (cur_pipe_ctx->stream)
			tg->funcs->unlock(tg);
	}

	/* Disable and Powerdown*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/*if (!pipe_ctx_old->stream)
			continue;*/

		if (pipe_ctx->stream && pipe_ctx->plane_state
				&& !pipe_need_reprogram(pipe_ctx_old, pipe_ctx))
			continue;

		plane_atomic_disable(dc, i);

		if (!pipe_ctx->stream || !pipe_ctx->plane_state)
			plane_atomic_power_down(dc, i);
	}

	/* Reset Back End*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!pipe_ctx_old->stream)
			continue;

		if (pipe_ctx_old->top_pipe)
			continue;

		if (!pipe_ctx->stream ||
				pipe_need_reprogram(pipe_ctx_old, pipe_ctx)) {
			struct clock_source *old_clk = pipe_ctx_old->clock_source;

			reset_back_end_for_pipe(dc, pipe_ctx_old, dc->current_state);

			if (old_clk)
				old_clk->funcs->cs_power_down(old_clk);
		}
	}

}

static bool patch_address_for_sbs_tb_stereo(
		struct pipe_ctx *pipe_ctx, PHYSICAL_ADDRESS_LOC *addr)
{
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	bool sec_split = pipe_ctx->top_pipe &&
			pipe_ctx->top_pipe->plane_state == pipe_ctx->plane_state;
	if (sec_split && plane_state->address.type == PLN_ADDR_TYPE_GRPH_STEREO &&
		(pipe_ctx->stream->timing.timing_3d_format ==
		 TIMING_3D_FORMAT_SIDE_BY_SIDE ||
		 pipe_ctx->stream->timing.timing_3d_format ==
		 TIMING_3D_FORMAT_TOP_AND_BOTTOM)) {
		*addr = plane_state->address.grph_stereo.left_addr;
		plane_state->address.grph_stereo.left_addr =
		plane_state->address.grph_stereo.right_addr;
		return true;
	} else {
		if (pipe_ctx->stream->view_format != VIEW_3D_FORMAT_NONE &&
			plane_state->address.type != PLN_ADDR_TYPE_GRPH_STEREO) {
			plane_state->address.type = PLN_ADDR_TYPE_GRPH_STEREO;
			plane_state->address.grph_stereo.right_addr =
			plane_state->address.grph_stereo.left_addr;
		}
	}
	return false;
}

static void toggle_watermark_change_req(struct dce_hwseq *hws)
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

static void dcn10_update_plane_addr(const struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	bool addr_patched = false;
	PHYSICAL_ADDRESS_LOC addr;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;

	if (plane_state == NULL)
		return;
	addr_patched = patch_address_for_sbs_tb_stereo(pipe_ctx, &addr);
	pipe_ctx->plane_res.hubp->funcs->hubp_program_surface_flip_and_addr(
			pipe_ctx->plane_res.hubp,
			&plane_state->address,
			plane_state->flip_immediate);
	plane_state->status.requested_address = plane_state->address;
	if (addr_patched)
		pipe_ctx->plane_state->address.grph_stereo.left_addr = addr;
}

static bool dcn10_set_input_transfer_func(
	struct pipe_ctx *pipe_ctx, const struct dc_plane_state *plane_state)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	const struct dc_transfer_func *tf = NULL;
	bool result = true;

	if (dpp_base == NULL)
		return false;

	if (plane_state->in_transfer_func)
		tf = plane_state->in_transfer_func;

	if (plane_state->gamma_correction && dce_use_lut(plane_state))
		dpp_base->funcs->ipp_program_input_lut(dpp_base,
				plane_state->gamma_correction);

	if (tf == NULL)
		dpp_base->funcs->ipp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_BYPASS);
	else if (tf->type == TF_TYPE_PREDEFINED) {
		switch (tf->tf) {
		case TRANSFER_FUNCTION_SRGB:
			dpp_base->funcs->ipp_set_degamma(dpp_base,
					IPP_DEGAMMA_MODE_HW_sRGB);
			break;
		case TRANSFER_FUNCTION_BT709:
			dpp_base->funcs->ipp_set_degamma(dpp_base,
					IPP_DEGAMMA_MODE_HW_xvYCC);
			break;
		case TRANSFER_FUNCTION_LINEAR:
			dpp_base->funcs->ipp_set_degamma(dpp_base,
					IPP_DEGAMMA_MODE_BYPASS);
			break;
		case TRANSFER_FUNCTION_PQ:
			result = false;
			break;
		default:
			result = false;
			break;
		}
	} else if (tf->type == TF_TYPE_BYPASS) {
		dpp_base->funcs->ipp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_BYPASS);
	} else {
		/*TF_TYPE_DISTRIBUTED_POINTS*/
		result = false;
	}

	return result;
}
/*modify the method to handle rgb for arr_points*/
static bool convert_to_custom_float(
		struct pwl_result_data *rgb_resulted,
		struct curve_points *arr_points,
		uint32_t hw_points_num)
{
	struct custom_float_format fmt;

	struct pwl_result_data *rgb = rgb_resulted;

	uint32_t i = 0;

	fmt.exponenta_bits = 6;
	fmt.mantissa_bits = 12;
	fmt.sign = false;

	if (!convert_to_custom_float_format(
		arr_points[0].x,
		&fmt,
		&arr_points[0].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[0].offset,
		&fmt,
		&arr_points[0].custom_float_offset)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[0].slope,
		&fmt,
		&arr_points[0].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 10;
	fmt.sign = false;

	if (!convert_to_custom_float_format(
		arr_points[1].x,
		&fmt,
		&arr_points[1].custom_float_x)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[1].y,
		&fmt,
		&arr_points[1].custom_float_y)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!convert_to_custom_float_format(
		arr_points[1].slope,
		&fmt,
		&arr_points[1].custom_float_slope)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	fmt.mantissa_bits = 12;
	fmt.sign = true;

	while (i != hw_points_num) {
		if (!convert_to_custom_float_format(
			rgb->red,
			&fmt,
			&rgb->red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->green,
			&fmt,
			&rgb->green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->blue,
			&fmt,
			&rgb->blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_red,
			&fmt,
			&rgb->delta_red_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_green,
			&fmt,
			&rgb->delta_green_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (!convert_to_custom_float_format(
			rgb->delta_blue,
			&fmt,
			&rgb->delta_blue_reg)) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		++rgb;
		++i;
	}

	return true;
}
#define MAX_REGIONS_NUMBER 34
#define MAX_LOW_POINT      25
#define NUMBER_SEGMENTS    32

static bool dcn10_translate_regamma_to_hw_format(const struct dc_transfer_func
		*output_tf, struct pwl_params *regamma_params)
{
	struct curve_points *arr_points;
	struct pwl_result_data *rgb_resulted;
	struct pwl_result_data *rgb;
	struct pwl_result_data *rgb_plus_1;
	struct fixed31_32 y_r;
	struct fixed31_32 y_g;
	struct fixed31_32 y_b;
	struct fixed31_32 y1_min;
	struct fixed31_32 y3_max;

	int32_t segment_start, segment_end;
	int32_t i;
	uint32_t j, k, seg_distr[MAX_REGIONS_NUMBER], increment, start_index, hw_points;

	if (output_tf == NULL || regamma_params == NULL ||
			output_tf->type == TF_TYPE_BYPASS)
		return false;

	arr_points = regamma_params->arr_points;
	rgb_resulted = regamma_params->rgb_resulted;
	hw_points = 0;

	memset(regamma_params, 0, sizeof(struct pwl_params));
	memset(seg_distr, 0, sizeof(seg_distr));

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* 32 segments
		 * segments are from 2^-25 to 2^7
		 */
		for (i = 0; i < 32 ; i++)
			seg_distr[i] = 3;

		segment_start = -25;
		segment_end   = 7;
	} else {
		/* 10 segments
		 * segment is from 2^-10 to 2^0
		 * There are less than 256 points, for optimization
		 */
		seg_distr[0] = 3;
		seg_distr[1] = 4;
		seg_distr[2] = 4;
		seg_distr[3] = 4;
		seg_distr[4] = 4;
		seg_distr[5] = 4;
		seg_distr[6] = 4;
		seg_distr[7] = 4;
		seg_distr[8] = 5;
		seg_distr[9] = 5;

		segment_start = -10;
		segment_end = 0;
	}

	for (i = segment_end - segment_start; i < MAX_REGIONS_NUMBER ; i++)
		seg_distr[i] = -1;

	for (k = 0; k < MAX_REGIONS_NUMBER; k++) {
		if (seg_distr[k] != -1)
			hw_points += (1 << seg_distr[k]);
	}

	j = 0;
	for (k = 0; k < (segment_end - segment_start); k++) {
		increment = NUMBER_SEGMENTS / (1 << seg_distr[k]);
		start_index = (segment_start + k + MAX_LOW_POINT) * NUMBER_SEGMENTS;
		for (i = start_index; i < start_index + NUMBER_SEGMENTS; i += increment) {
			if (j == hw_points - 1)
				break;
			rgb_resulted[j].red = output_tf->tf_pts.red[i];
			rgb_resulted[j].green = output_tf->tf_pts.green[i];
			rgb_resulted[j].blue = output_tf->tf_pts.blue[i];
			j++;
		}
	}

	/* last point */
	start_index = (segment_end + MAX_LOW_POINT) * NUMBER_SEGMENTS;
	rgb_resulted[hw_points - 1].red =
			output_tf->tf_pts.red[start_index];
	rgb_resulted[hw_points - 1].green =
			output_tf->tf_pts.green[start_index];
	rgb_resulted[hw_points - 1].blue =
			output_tf->tf_pts.blue[start_index];

	arr_points[0].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_start));
	arr_points[1].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_end));
	arr_points[2].x = dal_fixed31_32_pow(dal_fixed31_32_from_int(2),
			dal_fixed31_32_from_int(segment_end));

	y_r = rgb_resulted[0].red;
	y_g = rgb_resulted[0].green;
	y_b = rgb_resulted[0].blue;

	y1_min = dal_fixed31_32_min(y_r, dal_fixed31_32_min(y_g, y_b));

	arr_points[0].y = y1_min;
	arr_points[0].slope = dal_fixed31_32_div(
					arr_points[0].y,
					arr_points[0].x);
	y_r = rgb_resulted[hw_points - 1].red;
	y_g = rgb_resulted[hw_points - 1].green;
	y_b = rgb_resulted[hw_points - 1].blue;

	/* see comment above, m_arrPoints[1].y should be the Y value for the
	 * region end (m_numOfHwPoints), not last HW point(m_numOfHwPoints - 1)
	 */
	y3_max = dal_fixed31_32_max(y_r, dal_fixed31_32_max(y_g, y_b));

	arr_points[1].y = y3_max;
	arr_points[2].y = y3_max;

	arr_points[1].slope = dal_fixed31_32_zero;
	arr_points[2].slope = dal_fixed31_32_zero;

	if (output_tf->tf == TRANSFER_FUNCTION_PQ) {
		/* for PQ, we want to have a straight line from last HW X point,
		 * and the slope to be such that we hit 1.0 at 10000 nits.
		 */
		const struct fixed31_32 end_value =
				dal_fixed31_32_from_int(125);

		arr_points[1].slope = dal_fixed31_32_div(
			dal_fixed31_32_sub(dal_fixed31_32_one, arr_points[1].y),
			dal_fixed31_32_sub(end_value, arr_points[1].x));
		arr_points[2].slope = dal_fixed31_32_div(
			dal_fixed31_32_sub(dal_fixed31_32_one, arr_points[1].y),
			dal_fixed31_32_sub(end_value, arr_points[1].x));
	}

	regamma_params->hw_points_num = hw_points;

	i = 1;
	for (k = 0; k < MAX_REGIONS_NUMBER && i < MAX_REGIONS_NUMBER; k++) {
		if (seg_distr[k] != -1) {
			regamma_params->arr_curve_points[k].segments_num =
					seg_distr[k];
			regamma_params->arr_curve_points[i].offset =
					regamma_params->arr_curve_points[k].
					offset + (1 << seg_distr[k]);
		}
		i++;
	}

	if (seg_distr[k] != -1)
		regamma_params->arr_curve_points[k].segments_num =
				seg_distr[k];

	rgb = rgb_resulted;
	rgb_plus_1 = rgb_resulted + 1;

	i = 1;

	while (i != hw_points + 1) {
		if (dal_fixed31_32_lt(rgb_plus_1->red, rgb->red))
			rgb_plus_1->red = rgb->red;
		if (dal_fixed31_32_lt(rgb_plus_1->green, rgb->green))
			rgb_plus_1->green = rgb->green;
		if (dal_fixed31_32_lt(rgb_plus_1->blue, rgb->blue))
			rgb_plus_1->blue = rgb->blue;

		rgb->delta_red = dal_fixed31_32_sub(
			rgb_plus_1->red,
			rgb->red);
		rgb->delta_green = dal_fixed31_32_sub(
			rgb_plus_1->green,
			rgb->green);
		rgb->delta_blue = dal_fixed31_32_sub(
			rgb_plus_1->blue,
			rgb->blue);

		++rgb_plus_1;
		++rgb;
		++i;
	}

	convert_to_custom_float(rgb_resulted, arr_points, hw_points);

	return true;
}

static bool dcn10_set_output_transfer_func(
	struct pipe_ctx *pipe_ctx,
	const struct dc_stream_state *stream)
{
	struct dpp *dpp = pipe_ctx->plane_res.dpp;

	if (dpp == NULL)
		return false;

	dpp->regamma_params.hw_points_num = GAMMA_HW_POINTS_NUM;

	if (stream->out_transfer_func &&
		stream->out_transfer_func->type ==
			TF_TYPE_PREDEFINED &&
		stream->out_transfer_func->tf ==
			TRANSFER_FUNCTION_SRGB) {
		dpp->funcs->opp_set_regamma_mode(dpp, OPP_REGAMMA_SRGB);
	} else if (dcn10_translate_regamma_to_hw_format(
				stream->out_transfer_func, &dpp->regamma_params)) {
			dpp->funcs->opp_program_regamma_pwl(dpp, &dpp->regamma_params);
			dpp->funcs->opp_set_regamma_mode(dpp, OPP_REGAMMA_USER);
	} else {
		dpp->funcs->opp_set_regamma_mode(dpp, OPP_REGAMMA_BYPASS);
	}

	return true;
}

static void dcn10_pipe_control_lock(
	struct dc *dc,
	struct pipe_ctx *pipe,
	bool lock)
{
	struct hubp *hubp = NULL;
	hubp = dc->res_pool->hubps[pipe->pipe_idx];
	/* use TG master update lock to lock everything on the TG
	 * therefore only top pipe need to lock
	 */
	if (pipe->top_pipe)
		return;

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);

	if (lock)
		pipe->stream_res.tg->funcs->lock(pipe->stream_res.tg);
	else
		pipe->stream_res.tg->funcs->unlock(pipe->stream_res.tg);

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);
}

static bool wait_for_reset_trigger_to_occur(
	struct dc_context *dc_ctx,
	struct timing_generator *tg)
{
	bool rc = false;

	/* To avoid endless loop we wait at most
	 * frames_to_wait_on_triggered_reset frames for the reset to occur. */
	const uint32_t frames_to_wait_on_triggered_reset = 10;
	int i;

	for (i = 0; i < frames_to_wait_on_triggered_reset; i++) {

		if (!tg->funcs->is_counter_moving(tg)) {
			DC_ERROR("TG counter is not moving!\n");
			break;
		}

		if (tg->funcs->did_triggered_reset_occur(tg)) {
			rc = true;
			/* usually occurs at i=1 */
			DC_SYNC_INFO("GSL: reset occurred at wait count: %d\n",
					i);
			break;
		}

		/* Wait for one frame. */
		tg->funcs->wait_for_state(tg, CRTC_STATE_VACTIVE);
		tg->funcs->wait_for_state(tg, CRTC_STATE_VBLANK);
	}

	if (false == rc)
		DC_ERROR("GSL: Timeout on reset trigger!\n");

	return rc;
}

static void dcn10_enable_timing_synchronization(
	struct dc *dc,
	int group_index,
	int group_size,
	struct pipe_ctx *grouped_pipes[])
{
	struct dc_context *dc_ctx = dc->ctx;
	int i;

	DC_SYNC_INFO("Setting up OTG reset trigger\n");

	for (i = 1; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->enable_reset_trigger(
				grouped_pipes[i]->stream_res.tg, grouped_pipes[0]->stream_res.tg->inst);


	DC_SYNC_INFO("Waiting for trigger\n");

	/* Need to get only check 1 pipe for having reset as all the others are
	 * synchronized. Look at last pipe programmed to reset.
	 */
	wait_for_reset_trigger_to_occur(dc_ctx, grouped_pipes[1]->stream_res.tg);
	for (i = 1; i < group_size; i++)
		grouped_pipes[i]->stream_res.tg->funcs->disable_reset_trigger(
				grouped_pipes[i]->stream_res.tg);

	DC_SYNC_INFO("Sync complete\n");
}

static void print_rq_dlg_ttu(
		struct dc *core_dc,
		struct pipe_ctx *pipe_ctx)
{
	dm_logger_write(core_dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\n============== DML TTU Output parameters [%d] ==============\n"
			"qos_level_low_wm: %d, \n"
			"qos_level_high_wm: %d, \n"
			"min_ttu_vblank: %d, \n"
			"qos_level_flip: %d, \n"
			"refcyc_per_req_delivery_l: %d, \n"
			"qos_level_fixed_l: %d, \n"
			"qos_ramp_disable_l: %d, \n"
			"refcyc_per_req_delivery_pre_l: %d, \n"
			"refcyc_per_req_delivery_c: %d, \n"
			"qos_level_fixed_c: %d, \n"
			"qos_ramp_disable_c: %d, \n"
			"refcyc_per_req_delivery_pre_c: %d\n"
			"=============================================================\n",
			pipe_ctx->pipe_idx,
			pipe_ctx->ttu_regs.qos_level_low_wm,
			pipe_ctx->ttu_regs.qos_level_high_wm,
			pipe_ctx->ttu_regs.min_ttu_vblank,
			pipe_ctx->ttu_regs.qos_level_flip,
			pipe_ctx->ttu_regs.refcyc_per_req_delivery_l,
			pipe_ctx->ttu_regs.qos_level_fixed_l,
			pipe_ctx->ttu_regs.qos_ramp_disable_l,
			pipe_ctx->ttu_regs.refcyc_per_req_delivery_pre_l,
			pipe_ctx->ttu_regs.refcyc_per_req_delivery_c,
			pipe_ctx->ttu_regs.qos_level_fixed_c,
			pipe_ctx->ttu_regs.qos_ramp_disable_c,
			pipe_ctx->ttu_regs.refcyc_per_req_delivery_pre_c
			);

	dm_logger_write(core_dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\n============== DML DLG Output parameters [%d] ==============\n"
			"refcyc_h_blank_end: %d, \n"
			"dlg_vblank_end: %d, \n"
			"min_dst_y_next_start: %d, \n"
			"refcyc_per_htotal: %d, \n"
			"refcyc_x_after_scaler: %d, \n"
			"dst_y_after_scaler: %d, \n"
			"dst_y_prefetch: %d, \n"
			"dst_y_per_vm_vblank: %d, \n"
			"dst_y_per_row_vblank: %d, \n"
			"ref_freq_to_pix_freq: %d, \n"
			"vratio_prefetch: %d, \n"
			"refcyc_per_pte_group_vblank_l: %d, \n"
			"refcyc_per_meta_chunk_vblank_l: %d, \n"
			"dst_y_per_pte_row_nom_l: %d, \n"
			"refcyc_per_pte_group_nom_l: %d, \n",
			pipe_ctx->pipe_idx,
			pipe_ctx->dlg_regs.refcyc_h_blank_end,
			pipe_ctx->dlg_regs.dlg_vblank_end,
			pipe_ctx->dlg_regs.min_dst_y_next_start,
			pipe_ctx->dlg_regs.refcyc_per_htotal,
			pipe_ctx->dlg_regs.refcyc_x_after_scaler,
			pipe_ctx->dlg_regs.dst_y_after_scaler,
			pipe_ctx->dlg_regs.dst_y_prefetch,
			pipe_ctx->dlg_regs.dst_y_per_vm_vblank,
			pipe_ctx->dlg_regs.dst_y_per_row_vblank,
			pipe_ctx->dlg_regs.ref_freq_to_pix_freq,
			pipe_ctx->dlg_regs.vratio_prefetch,
			pipe_ctx->dlg_regs.refcyc_per_pte_group_vblank_l,
			pipe_ctx->dlg_regs.refcyc_per_meta_chunk_vblank_l,
			pipe_ctx->dlg_regs.dst_y_per_pte_row_nom_l,
			pipe_ctx->dlg_regs.refcyc_per_pte_group_nom_l
			);

	dm_logger_write(core_dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\ndst_y_per_meta_row_nom_l: %d, \n"
			"refcyc_per_meta_chunk_nom_l: %d, \n"
			"refcyc_per_line_delivery_pre_l: %d, \n"
			"refcyc_per_line_delivery_l: %d, \n"
			"vratio_prefetch_c: %d, \n"
			"refcyc_per_pte_group_vblank_c: %d, \n"
			"refcyc_per_meta_chunk_vblank_c: %d, \n"
			"dst_y_per_pte_row_nom_c: %d, \n"
			"refcyc_per_pte_group_nom_c: %d, \n"
			"dst_y_per_meta_row_nom_c: %d, \n"
			"refcyc_per_meta_chunk_nom_c: %d, \n"
			"refcyc_per_line_delivery_pre_c: %d, \n"
			"refcyc_per_line_delivery_c: %d \n"
			"========================================================\n",
			pipe_ctx->dlg_regs.dst_y_per_meta_row_nom_l,
			pipe_ctx->dlg_regs.refcyc_per_meta_chunk_nom_l,
			pipe_ctx->dlg_regs.refcyc_per_line_delivery_pre_l,
			pipe_ctx->dlg_regs.refcyc_per_line_delivery_l,
			pipe_ctx->dlg_regs.vratio_prefetch_c,
			pipe_ctx->dlg_regs.refcyc_per_pte_group_vblank_c,
			pipe_ctx->dlg_regs.refcyc_per_meta_chunk_vblank_c,
			pipe_ctx->dlg_regs.dst_y_per_pte_row_nom_c,
			pipe_ctx->dlg_regs.refcyc_per_pte_group_nom_c,
			pipe_ctx->dlg_regs.dst_y_per_meta_row_nom_c,
			pipe_ctx->dlg_regs.refcyc_per_meta_chunk_nom_c,
			pipe_ctx->dlg_regs.refcyc_per_line_delivery_pre_c,
			pipe_ctx->dlg_regs.refcyc_per_line_delivery_c
			);

	dm_logger_write(core_dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\n============== DML RQ Output parameters [%d] ==============\n"
			"chunk_size: %d \n"
			"min_chunk_size: %d \n"
			"meta_chunk_size: %d \n"
			"min_meta_chunk_size: %d \n"
			"dpte_group_size: %d \n"
			"mpte_group_size: %d \n"
			"swath_height: %d \n"
			"pte_row_height_linear: %d \n"
			"========================================================\n",
			pipe_ctx->pipe_idx,
			pipe_ctx->rq_regs.rq_regs_l.chunk_size,
			pipe_ctx->rq_regs.rq_regs_l.min_chunk_size,
			pipe_ctx->rq_regs.rq_regs_l.meta_chunk_size,
			pipe_ctx->rq_regs.rq_regs_l.min_meta_chunk_size,
			pipe_ctx->rq_regs.rq_regs_l.dpte_group_size,
			pipe_ctx->rq_regs.rq_regs_l.mpte_group_size,
			pipe_ctx->rq_regs.rq_regs_l.swath_height,
			pipe_ctx->rq_regs.rq_regs_l.pte_row_height_linear
			);
}

static void dcn10_power_on_fe(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context)
{
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dce_hwseq *hws = dc->hwseq;

	if (dc->debug.sanity_checks) {
		verify_allow_pstate_change_high(dc->hwseq);
	}

	power_on_plane(dc->hwseq,
		pipe_ctx->pipe_idx);

	/* enable DCFCLK current DCHUB */
	REG_UPDATE(HUBP_CLK_CNTL[pipe_ctx->pipe_idx],
			HUBP_CLOCK_ENABLE, 1);

	/* make sure OPP_PIPE_CLOCK_EN = 1 */
	REG_UPDATE(OPP_PIPE_CONTROL[pipe_ctx->stream_res.tg->inst],
			OPP_PIPE_CLOCK_EN, 1);
	/*TODO: REG_UPDATE(DENTIST_DISPCLK_CNTL, DENTIST_DPPCLK_WDIVIDER, 0x1f);*/

	if (plane_state) {
		dm_logger_write(dc->ctx->logger, LOG_DC,
				"Pipe:%d 0x%x: addr hi:0x%x, "
				"addr low:0x%x, "
				"src: %d, %d, %d,"
				" %d; dst: %d, %d, %d, %d;\n",
				pipe_ctx->pipe_idx,
				plane_state,
				plane_state->address.grph.addr.high_part,
				plane_state->address.grph.addr.low_part,
				plane_state->src_rect.x,
				plane_state->src_rect.y,
				plane_state->src_rect.width,
				plane_state->src_rect.height,
				plane_state->dst_rect.x,
				plane_state->dst_rect.y,
				plane_state->dst_rect.width,
				plane_state->dst_rect.height);

		dm_logger_write(dc->ctx->logger, LOG_DC,
				"Pipe %d: width, height, x, y         format:%d\n"
				"viewport:%d, %d, %d, %d\n"
				"recout:  %d, %d, %d, %d\n",
				pipe_ctx->pipe_idx,
				plane_state->format,
				pipe_ctx->plane_res.scl_data.viewport.width,
				pipe_ctx->plane_res.scl_data.viewport.height,
				pipe_ctx->plane_res.scl_data.viewport.x,
				pipe_ctx->plane_res.scl_data.viewport.y,
				pipe_ctx->plane_res.scl_data.recout.width,
				pipe_ctx->plane_res.scl_data.recout.height,
				pipe_ctx->plane_res.scl_data.recout.x,
				pipe_ctx->plane_res.scl_data.recout.y);
		print_rq_dlg_ttu(dc, pipe_ctx);
	}

	if (dc->debug.sanity_checks) {
		verify_allow_pstate_change_high(dc->hwseq);
	}
}

static void program_gamut_remap(struct pipe_ctx *pipe_ctx)
{
	struct dpp_grph_csc_adjustment adjust;
	memset(&adjust, 0, sizeof(adjust));
	adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;


	if (pipe_ctx->stream->gamut_remap_matrix.enable_remap == true) {
		adjust.gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
		adjust.temperature_matrix[0] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[0];
		adjust.temperature_matrix[1] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[1];
		adjust.temperature_matrix[2] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[2];
		adjust.temperature_matrix[3] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[4];
		adjust.temperature_matrix[4] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[5];
		adjust.temperature_matrix[5] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[6];
		adjust.temperature_matrix[6] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[8];
		adjust.temperature_matrix[7] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[9];
		adjust.temperature_matrix[8] =
				pipe_ctx->stream->
				gamut_remap_matrix.matrix[10];
	}

	pipe_ctx->plane_res.dpp->funcs->dpp_set_gamut_remap(pipe_ctx->plane_res.dpp, &adjust);
}


static void program_csc_matrix(struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix)
{
	int i;
	struct out_csc_color_matrix tbl_entry;

	if (pipe_ctx->stream->csc_color_matrix.enable_adjustment
				== true) {
			enum dc_color_space color_space =
				pipe_ctx->stream->output_color_space;

			//uint16_t matrix[12];
			for (i = 0; i < 12; i++)
				tbl_entry.regval[i] = pipe_ctx->stream->csc_color_matrix.matrix[i];

			tbl_entry.color_space = color_space;
			//tbl_entry.regval = matrix;
			pipe_ctx->plane_res.dpp->funcs->opp_set_csc_adjustment(pipe_ctx->plane_res.dpp, &tbl_entry);
	}
}
static bool is_lower_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->plane_state->visible)
		return true;
	if (pipe_ctx->bottom_pipe && is_lower_pipe_tree_visible(pipe_ctx->bottom_pipe))
		return true;
	return false;
}

static bool is_upper_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->plane_state->visible)
		return true;
	if (pipe_ctx->top_pipe && is_upper_pipe_tree_visible(pipe_ctx->top_pipe))
		return true;
	return false;
}

static bool is_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->plane_state->visible)
		return true;
	if (pipe_ctx->top_pipe && is_upper_pipe_tree_visible(pipe_ctx->top_pipe))
		return true;
	if (pipe_ctx->bottom_pipe && is_lower_pipe_tree_visible(pipe_ctx->bottom_pipe))
		return true;
	return false;
}

static bool is_rgb_cspace(enum dc_color_space output_color_space)
{
	switch (output_color_space) {
	case COLOR_SPACE_SRGB:
	case COLOR_SPACE_SRGB_LIMITED:
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
	case COLOR_SPACE_ADOBERGB:
		return true;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
	case COLOR_SPACE_2020_YCBCR:
		return false;
	default:
		/* Add a case to switch */
		BREAK_TO_DEBUGGER();
		return false;
	}
}

static void dcn10_get_surface_visual_confirm_color(
		const struct pipe_ctx *pipe_ctx,
		struct tg_color *color)
{
	uint32_t color_value = MAX_TG_COLOR_VALUE;

	switch (pipe_ctx->plane_res.scl_data.format) {
	case PIXEL_FORMAT_ARGB8888:
		/* set boarder color to red */
		color->color_r_cr = color_value;
		break;

	case PIXEL_FORMAT_ARGB2101010:
		/* set boarder color to blue */
		color->color_b_cb = color_value;
		break;
	case PIXEL_FORMAT_420BPP8:
		/* set boarder color to green */
		color->color_g_y = color_value;
		break;
	case PIXEL_FORMAT_420BPP10:
		/* set boarder color to yellow */
		color->color_g_y = color_value;
		color->color_r_cr = color_value;
		break;
	case PIXEL_FORMAT_FP16:
		/* set boarder color to white */
		color->color_r_cr = color_value;
		color->color_b_cb = color_value;
		color->color_g_y = color_value;
		break;
	default:
		break;
	}
}

static void mmhub_read_vm_system_aperture_settings(struct dcn10_hubp *hubp1,
		struct vm_system_aperture_param *apt,
		struct dce_hwseq *hws)
{
	PHYSICAL_ADDRESS_LOC physical_page_number;
	uint32_t logical_addr_low;
	uint32_t logical_addr_high;

	REG_GET(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
			PHYSICAL_PAGE_NUMBER_MSB, &physical_page_number.high_part);
	REG_GET(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
			PHYSICAL_PAGE_NUMBER_LSB, &physical_page_number.low_part);

	REG_GET(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
			LOGICAL_ADDR, &logical_addr_low);

	REG_GET(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
			LOGICAL_ADDR, &logical_addr_high);

	apt->sys_default.quad_part =  physical_page_number.quad_part << 12;
	apt->sys_low.quad_part =  (int64_t)logical_addr_low << 18;
	apt->sys_high.quad_part =  (int64_t)logical_addr_high << 18;
}

/* Temporary read settings, future will get values from kmd directly */
static void mmhub_read_vm_context0_settings(struct dcn10_hubp *hubp1,
		struct vm_context0_param *vm0,
		struct dce_hwseq *hws)
{
	PHYSICAL_ADDRESS_LOC fb_base;
	PHYSICAL_ADDRESS_LOC fb_offset;
	uint32_t fb_base_value;
	uint32_t fb_offset_value;

	REG_GET(DCHUBBUB_SDPIF_FB_BASE, SDPIF_FB_BASE, &fb_base_value);
	REG_GET(DCHUBBUB_SDPIF_FB_OFFSET, SDPIF_FB_OFFSET, &fb_offset_value);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
			PAGE_DIRECTORY_ENTRY_HI32, &vm0->pte_base.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
			PAGE_DIRECTORY_ENTRY_LO32, &vm0->pte_base.low_part);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
			LOGICAL_PAGE_NUMBER_HI4, &vm0->pte_start.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
			LOGICAL_PAGE_NUMBER_LO32, &vm0->pte_start.low_part);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
			LOGICAL_PAGE_NUMBER_HI4, &vm0->pte_end.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
			LOGICAL_PAGE_NUMBER_LO32, &vm0->pte_end.low_part);

	REG_GET(VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
			PHYSICAL_PAGE_ADDR_HI4, &vm0->fault_default.high_part);
	REG_GET(VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
			PHYSICAL_PAGE_ADDR_LO32, &vm0->fault_default.low_part);

	/*
	 * The values in VM_CONTEXT0_PAGE_TABLE_BASE_ADDR is in UMA space.
	 * Therefore we need to do
	 * DCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR = VM_CONTEXT0_PAGE_TABLE_BASE_ADDR
	 * - DCHUBBUB_SDPIF_FB_OFFSET + DCHUBBUB_SDPIF_FB_BASE
	 */
	fb_base.quad_part = (uint64_t)fb_base_value << 24;
	fb_offset.quad_part = (uint64_t)fb_offset_value << 24;
	vm0->pte_base.quad_part += fb_base.quad_part;
	vm0->pte_base.quad_part -= fb_offset.quad_part;
}

static void dcn10_program_pte_vm(struct hubp *hubp,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		enum dc_rotation_angle rotation,
		struct dce_hwseq *hws)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	struct vm_system_aperture_param apt = { {{ 0 } } };
	struct vm_context0_param vm0 = { { { 0 } } };


	mmhub_read_vm_system_aperture_settings(hubp1, &apt, hws);
	mmhub_read_vm_context0_settings(hubp1, &vm0, hws);

	hubp->funcs->hubp_set_vm_system_aperture_settings(hubp, &apt);
	hubp->funcs->hubp_set_vm_context0_settings(hubp, &vm0);
}

static void update_dchubp_dpp(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	union plane_size size = plane_state->plane_size;
	struct mpcc_cfg mpcc_cfg = {0};
	struct pipe_ctx *top_pipe;
	bool per_pixel_alpha = plane_state->per_pixel_alpha && pipe_ctx->bottom_pipe;

	/* TODO: proper fix once fpga works */
	/* depends on DML calculation, DPP clock value may change dynamically */
	enable_dppclk(
		dc->hwseq,
		pipe_ctx->pipe_idx,
		pipe_ctx->stream_res.pix_clk_params.requested_pix_clk,
		context->bw.dcn.calc_clk.dppclk_div);
	dc->current_state->bw.dcn.cur_clk.dppclk_div =
			context->bw.dcn.calc_clk.dppclk_div;
	context->bw.dcn.cur_clk.dppclk_div = context->bw.dcn.calc_clk.dppclk_div;

	/* TODO: Need input parameter to tell current DCHUB pipe tie to which OTG
	 * VTG is within DCHUBBUB which is commond block share by each pipe HUBP.
	 * VTG is 1:1 mapping with OTG. Each pipe HUBP will select which VTG
	 */
	REG_UPDATE(DCHUBP_CNTL[pipe_ctx->pipe_idx], HUBP_VTG_SEL, pipe_ctx->stream_res.tg->inst);

	hubp->funcs->hubp_setup(
		hubp,
		&pipe_ctx->dlg_regs,
		&pipe_ctx->ttu_regs,
		&pipe_ctx->rq_regs,
		&pipe_ctx->pipe_dlg_param);

	size.grph.surface_size = pipe_ctx->plane_res.scl_data.viewport;

	if (dc->config.gpu_vm_support)
		dcn10_program_pte_vm(
				pipe_ctx->plane_res.hubp,
				plane_state->format,
				&plane_state->tiling_info,
				plane_state->rotation,
				hws
				);

	dpp->funcs->ipp_setup(dpp,
			plane_state->format,
			EXPANSION_MODE_ZERO);

	mpcc_cfg.dpp_id = hubp->inst;
	mpcc_cfg.opp_id = pipe_ctx->stream_res.opp->inst;
	mpcc_cfg.tree_cfg = &(pipe_ctx->stream_res.opp->mpc_tree);
	for (top_pipe = pipe_ctx->top_pipe; top_pipe; top_pipe = top_pipe->top_pipe)
		mpcc_cfg.z_index++;
	if (dc->debug.surface_visual_confirm)
		dcn10_get_surface_visual_confirm_color(
				pipe_ctx, &mpcc_cfg.black_color);
	else
		color_space_to_black_color(
			dc, pipe_ctx->stream->output_color_space,
			&mpcc_cfg.black_color);
	mpcc_cfg.per_pixel_alpha = per_pixel_alpha;
	/* DCN1.0 has output CM before MPC which seems to screw with
	 * pre-multiplied alpha.
	 */
	mpcc_cfg.pre_multiplied_alpha = is_rgb_cspace(
			pipe_ctx->stream->output_color_space)
					&& per_pixel_alpha;
	hubp->mpcc_id = dc->res_pool->mpc->funcs->add(dc->res_pool->mpc, &mpcc_cfg);
	hubp->opp_id = mpcc_cfg.opp_id;

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = per_pixel_alpha;
	pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_30BPP;
	/* scaler configuration */
	pipe_ctx->plane_res.dpp->funcs->dpp_set_scaler(
			pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data);

	hubp->funcs->mem_program_viewport(hubp,
			&pipe_ctx->plane_res.scl_data.viewport, &pipe_ctx->plane_res.scl_data.viewport_c);

	/*gamut remap*/
	program_gamut_remap(pipe_ctx);

	program_csc_matrix(pipe_ctx,
			pipe_ctx->stream->output_color_space,
			pipe_ctx->stream->csc_color_matrix.matrix);

	hubp->funcs->hubp_program_surface_config(
		hubp,
		plane_state->format,
		&plane_state->tiling_info,
		&size,
		plane_state->rotation,
		&plane_state->dcc,
		plane_state->horizontal_mirror);

	dc->hwss.update_plane_addr(dc, pipe_ctx);

	if (is_pipe_tree_visible(pipe_ctx))
		hubp->funcs->set_blank(hubp, false);
}


static void program_all_pipe_in_tree(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	unsigned int ref_clk_mhz = dc->res_pool->ref_clock_inKhz/1000;

	if (pipe_ctx->top_pipe == NULL) {

		/* lock otg_master_update to process all pipes associated with
		 * this OTG. this is done only one time.
		 */
		/* watermark is for all pipes */
		program_watermarks(dc->hwseq, &context->bw.dcn.watermarks, ref_clk_mhz);

		if (dc->debug.sanity_checks) {
			/* pstate stuck check after watermark update */
			verify_allow_pstate_change_high(dc->hwseq);
		}

		pipe_ctx->stream_res.tg->funcs->lock(pipe_ctx->stream_res.tg);

		pipe_ctx->stream_res.tg->dlg_otg_param.vready_offset = pipe_ctx->pipe_dlg_param.vready_offset;
		pipe_ctx->stream_res.tg->dlg_otg_param.vstartup_start = pipe_ctx->pipe_dlg_param.vstartup_start;
		pipe_ctx->stream_res.tg->dlg_otg_param.vupdate_offset = pipe_ctx->pipe_dlg_param.vupdate_offset;
		pipe_ctx->stream_res.tg->dlg_otg_param.vupdate_width = pipe_ctx->pipe_dlg_param.vupdate_width;
		pipe_ctx->stream_res.tg->dlg_otg_param.signal =  pipe_ctx->stream->signal;

		pipe_ctx->stream_res.tg->funcs->program_global_sync(
				pipe_ctx->stream_res.tg);
		pipe_ctx->stream_res.tg->funcs->set_blank(pipe_ctx->stream_res.tg, !is_pipe_tree_visible(pipe_ctx));
	}

	if (pipe_ctx->plane_state != NULL) {
		struct pipe_ctx *cur_pipe_ctx =
				&dc->current_state->res_ctx.pipe_ctx[pipe_ctx->pipe_idx];

		dcn10_power_on_fe(dc, pipe_ctx, context);

		/* temporary dcn1 wa:
		 *   watermark update requires toggle after a/b/c/d sets are programmed
		 *   if hubp is pg then wm value doesn't get properaged to hubp
		 *   need to toggle after ungate to ensure wm gets to hubp.
		 *
		 * final solution:  we need to get SMU to do the toggle as
		 * DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST is owned by SMU we should have
		 * both driver and fw accessing same register
		 */
		toggle_watermark_change_req(dc->hwseq);

		update_dchubp_dpp(dc, pipe_ctx, context);

		/* TODO: this is a hack w/a for switching from mpo to pipe split */
		if (pipe_ctx->stream->cursor_attributes.address.quad_part != 0) {
			struct dc_cursor_position position = { 0 };

			dc_stream_set_cursor_position(pipe_ctx->stream, &position);
			dc_stream_set_cursor_attributes(pipe_ctx->stream,
				&pipe_ctx->stream->cursor_attributes);
		}

		if (cur_pipe_ctx->plane_state != pipe_ctx->plane_state) {
			dc->hwss.set_input_transfer_func(
					pipe_ctx, pipe_ctx->plane_state);
			dc->hwss.set_output_transfer_func(
					pipe_ctx, pipe_ctx->stream);
		}
	}

	if (dc->debug.sanity_checks) {
		/* pstate stuck check after each pipe is programmed */
		verify_allow_pstate_change_high(dc->hwseq);
	}

	if (pipe_ctx->bottom_pipe != NULL && pipe_ctx->bottom_pipe != pipe_ctx)
		program_all_pipe_in_tree(dc, pipe_ctx->bottom_pipe, context);
}

static void dcn10_pplib_apply_display_requirements(
	struct dc *dc,
	struct dc_state *context)
{
	struct dm_pp_display_configuration *pp_display_cfg = &context->pp_display_cfg;

	pp_display_cfg->all_displays_in_sync = false;/*todo*/
	pp_display_cfg->nb_pstate_switch_disable = false;
	pp_display_cfg->min_engine_clock_khz = context->bw.dcn.cur_clk.dcfclk_khz;
	pp_display_cfg->min_memory_clock_khz = context->bw.dcn.cur_clk.fclk_khz;
	pp_display_cfg->min_engine_clock_deep_sleep_khz = context->bw.dcn.cur_clk.dcfclk_deep_sleep_khz;
	pp_display_cfg->min_dcfc_deep_sleep_clock_khz = context->bw.dcn.cur_clk.dcfclk_deep_sleep_khz;
	pp_display_cfg->avail_mclk_switch_time_us =
			context->bw.dcn.cur_clk.dram_ccm_us > 0 ? context->bw.dcn.cur_clk.dram_ccm_us : 0;
	pp_display_cfg->avail_mclk_switch_time_in_disp_active_us =
			context->bw.dcn.cur_clk.min_active_dram_ccm_us > 0 ? context->bw.dcn.cur_clk.min_active_dram_ccm_us : 0;
	pp_display_cfg->min_dcfclock_khz = context->bw.dcn.cur_clk.dcfclk_khz;
	pp_display_cfg->disp_clk_khz = context->bw.dcn.cur_clk.dispclk_khz;
	dce110_fill_display_configs(context, pp_display_cfg);

	if (memcmp(&dc->prev_display_config, pp_display_cfg, sizeof(
			struct dm_pp_display_configuration)) !=  0)
		dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);

	dc->prev_display_config = *pp_display_cfg;
}

static void optimize_shared_resources(struct dc *dc)
{
	if (dc->current_state->stream_count == 0) {
		apply_DEGVIDCN10_253_wa(dc);
		/* S0i2 message */
		dcn10_pplib_apply_display_requirements(dc, dc->current_state);
	}

	if (dc->debug.pplib_wm_report_mode == WM_REPORT_OVERRIDE)
		dcn_bw_notify_pplib_of_wm_ranges(dc);
}

static void ready_shared_resources(struct dc *dc, struct dc_state *context)
{
	if (dc->current_state->stream_count == 0 &&
			!dc->debug.disable_stutter)
		undo_DEGVIDCN10_253_wa(dc);

	/* S0i2 message */
	if (dc->current_state->stream_count == 0 &&
			context->stream_count != 0)
		dcn10_pplib_apply_display_requirements(dc, context);
}

static void dcn10_apply_ctx_for_surface(
		struct dc *dc,
		const struct dc_stream_state *stream,
		int num_planes,
		struct dc_state *context)
{
	int i, be_idx;

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);

	be_idx = -1;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (stream == context->res_ctx.pipe_ctx[i].stream) {
			be_idx = context->res_ctx.pipe_ctx[i].stream_res.tg->inst;
			break;
		}
	}

	ASSERT(be_idx != -1);

	if (num_planes == 0) {
		for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
			struct pipe_ctx *old_pipe_ctx =
							&dc->current_state->res_ctx.pipe_ctx[i];

			if (old_pipe_ctx->stream_res.tg && old_pipe_ctx->stream_res.tg->inst == be_idx) {
				old_pipe_ctx->stream_res.tg->funcs->set_blank(old_pipe_ctx->stream_res.tg, true);
				dcn10_power_down_fe(dc, old_pipe_ctx->pipe_idx);
			}
		}
		return;
	}

	/* reset unused mpcc */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe_ctx =
				&dc->current_state->res_ctx.pipe_ctx[i];

		if (!pipe_ctx->plane_state && !old_pipe_ctx->plane_state)
			continue;

		if (pipe_ctx->stream_res.tg &&
			pipe_ctx->stream_res.tg->inst == be_idx &&
			!pipe_ctx->top_pipe)
			pipe_ctx->stream_res.tg->funcs->lock(pipe_ctx->stream_res.tg);

		/*
		 * Powergate reused pipes that are not powergated
		 * fairly hacky right now, using opp_id as indicator
		 */

		if (pipe_ctx->plane_state && !old_pipe_ctx->plane_state) {
			if (pipe_ctx->plane_res.hubp->opp_id != 0xf && pipe_ctx->stream_res.tg->inst == be_idx) {
				dcn10_power_down_fe(dc, pipe_ctx->pipe_idx);
				/*
				 * power down fe will unlock when calling reset, need
				 * to lock it back here. Messy, need rework.
				 */
				pipe_ctx->stream_res.tg->funcs->lock(pipe_ctx->stream_res.tg);
			}
		}


		if ((!pipe_ctx->plane_state && old_pipe_ctx->plane_state)
				|| (!pipe_ctx->stream && old_pipe_ctx->stream)) {
			if (old_pipe_ctx->stream_res.tg->inst != be_idx)
				continue;

			if (!old_pipe_ctx->top_pipe) {
				ASSERT(0);
				continue;
			}

			/* reset mpc */
			dc->res_pool->mpc->funcs->remove(
					dc->res_pool->mpc,
					&(old_pipe_ctx->stream_res.opp->mpc_tree),
					old_pipe_ctx->stream_res.opp->inst,
					old_pipe_ctx->pipe_idx);
			old_pipe_ctx->stream_res.opp->mpcc_disconnect_pending[old_pipe_ctx->plane_res.hubp->mpcc_id] = true;

			/*dm_logger_write(dc->ctx->logger, LOG_ERROR,
					"[debug_mpo: apply_ctx disconnect pending on mpcc %d]\n",
					old_pipe_ctx->mpcc->inst);*/

			if (dc->debug.sanity_checks)
				verify_allow_pstate_change_high(dc->hwseq);

			old_pipe_ctx->top_pipe = NULL;
			old_pipe_ctx->bottom_pipe = NULL;
			old_pipe_ctx->plane_state = NULL;
			old_pipe_ctx->stream = NULL;

			dm_logger_write(dc->ctx->logger, LOG_DC,
					"Reset mpcc for pipe %d\n",
					old_pipe_ctx->pipe_idx);
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream != stream)
			continue;

		/* looking for top pipe to program */
		if (!pipe_ctx->top_pipe) {
			program_all_pipe_in_tree(dc, pipe_ctx, context);
			if (pipe_ctx->stream_res.tg &&
				pipe_ctx->stream_res.tg->inst == be_idx &&
				(pipe_ctx->plane_state || old_pipe_ctx->plane_state))
				pipe_ctx->stream_res.tg->funcs->unlock(pipe_ctx->stream_res.tg);
		}
	}

	dm_logger_write(dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\n============== Watermark parameters ==============\n"
			"a.urgent_ns: %d \n"
			"a.cstate_enter_plus_exit: %d \n"
			"a.cstate_exit: %d \n"
			"a.pstate_change: %d \n"
			"a.pte_meta_urgent: %d \n"
			"b.urgent_ns: %d \n"
			"b.cstate_enter_plus_exit: %d \n"
			"b.cstate_exit: %d \n"
			"b.pstate_change: %d \n"
			"b.pte_meta_urgent: %d \n",
			context->bw.dcn.watermarks.a.urgent_ns,
			context->bw.dcn.watermarks.a.cstate_pstate.cstate_enter_plus_exit_ns,
			context->bw.dcn.watermarks.a.cstate_pstate.cstate_exit_ns,
			context->bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns,
			context->bw.dcn.watermarks.a.pte_meta_urgent_ns,
			context->bw.dcn.watermarks.b.urgent_ns,
			context->bw.dcn.watermarks.b.cstate_pstate.cstate_enter_plus_exit_ns,
			context->bw.dcn.watermarks.b.cstate_pstate.cstate_exit_ns,
			context->bw.dcn.watermarks.b.cstate_pstate.pstate_change_ns,
			context->bw.dcn.watermarks.b.pte_meta_urgent_ns
			);
	dm_logger_write(dc->ctx->logger, LOG_BANDWIDTH_CALCS,
			"\nc.urgent_ns: %d \n"
			"c.cstate_enter_plus_exit: %d \n"
			"c.cstate_exit: %d \n"
			"c.pstate_change: %d \n"
			"c.pte_meta_urgent: %d \n"
			"d.urgent_ns: %d \n"
			"d.cstate_enter_plus_exit: %d \n"
			"d.cstate_exit: %d \n"
			"d.pstate_change: %d \n"
			"d.pte_meta_urgent: %d \n"
			"========================================================\n",
			context->bw.dcn.watermarks.c.urgent_ns,
			context->bw.dcn.watermarks.c.cstate_pstate.cstate_enter_plus_exit_ns,
			context->bw.dcn.watermarks.c.cstate_pstate.cstate_exit_ns,
			context->bw.dcn.watermarks.c.cstate_pstate.pstate_change_ns,
			context->bw.dcn.watermarks.c.pte_meta_urgent_ns,
			context->bw.dcn.watermarks.d.urgent_ns,
			context->bw.dcn.watermarks.d.cstate_pstate.cstate_enter_plus_exit_ns,
			context->bw.dcn.watermarks.d.cstate_pstate.cstate_exit_ns,
			context->bw.dcn.watermarks.d.cstate_pstate.pstate_change_ns,
			context->bw.dcn.watermarks.d.pte_meta_urgent_ns
			);

	if (dc->debug.sanity_checks)
		verify_allow_pstate_change_high(dc->hwseq);
}

static void dcn10_set_bandwidth(
		struct dc *dc,
		struct dc_state *context,
		bool decrease_allowed)
{
	struct pp_smu_display_requirement_rv *smu_req_cur =
			&dc->res_pool->pp_smu_req;
	struct pp_smu_display_requirement_rv smu_req = *smu_req_cur;
	struct pp_smu_funcs_rv *pp_smu = dc->res_pool->pp_smu;

	if (dc->debug.sanity_checks) {
		verify_allow_pstate_change_high(dc->hwseq);
	}

	if (IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		return;

	if (decrease_allowed || context->bw.dcn.calc_clk.dispclk_khz
			> dc->current_state->bw.dcn.cur_clk.dispclk_khz) {
		dc->res_pool->display_clock->funcs->set_clock(
				dc->res_pool->display_clock,
				context->bw.dcn.calc_clk.dispclk_khz);
		dc->current_state->bw.dcn.cur_clk.dispclk_khz =
				context->bw.dcn.calc_clk.dispclk_khz;
	}
	if (decrease_allowed || context->bw.dcn.calc_clk.dcfclk_khz
			> dc->current_state->bw.dcn.cur_clk.dcfclk_khz) {
		smu_req.hard_min_dcefclk_khz =
				context->bw.dcn.calc_clk.dcfclk_khz;
	}
	if (decrease_allowed || context->bw.dcn.calc_clk.fclk_khz
			> dc->current_state->bw.dcn.cur_clk.fclk_khz) {
		smu_req.hard_min_fclk_khz = context->bw.dcn.calc_clk.fclk_khz;
	}
	if (decrease_allowed || context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz
			> dc->current_state->bw.dcn.cur_clk.dcfclk_deep_sleep_khz) {
		dc->current_state->bw.dcn.calc_clk.dcfclk_deep_sleep_khz =
				context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz;
		context->bw.dcn.cur_clk.dcfclk_deep_sleep_khz =
				context->bw.dcn.calc_clk.dcfclk_deep_sleep_khz;
	}

	smu_req.display_count = context->stream_count;

	if (pp_smu->set_display_requirement)
		pp_smu->set_display_requirement(&pp_smu->pp_smu, &smu_req);

	*smu_req_cur = smu_req;

	/* Decrease in freq is increase in period so opposite comparison for dram_ccm */
	if (decrease_allowed || context->bw.dcn.calc_clk.dram_ccm_us
			< dc->current_state->bw.dcn.cur_clk.dram_ccm_us) {
		dc->current_state->bw.dcn.calc_clk.dram_ccm_us =
				context->bw.dcn.calc_clk.dram_ccm_us;
		context->bw.dcn.cur_clk.dram_ccm_us =
				context->bw.dcn.calc_clk.dram_ccm_us;
	}
	if (decrease_allowed || context->bw.dcn.calc_clk.min_active_dram_ccm_us
			< dc->current_state->bw.dcn.cur_clk.min_active_dram_ccm_us) {
		dc->current_state->bw.dcn.calc_clk.min_active_dram_ccm_us =
				context->bw.dcn.calc_clk.min_active_dram_ccm_us;
		context->bw.dcn.cur_clk.min_active_dram_ccm_us =
				context->bw.dcn.calc_clk.min_active_dram_ccm_us;
	}
	dcn10_pplib_apply_display_requirements(dc, context);

	if (dc->debug.sanity_checks) {
		verify_allow_pstate_change_high(dc->hwseq);
	}

	/* need to fix this function.  not doing the right thing here */
}

static void set_drr(struct pipe_ctx **pipe_ctx,
		int num_pipes, int vmin, int vmax)
{
	int i = 0;
	struct drr_params params = {0};

	params.vertical_total_max = vmax;
	params.vertical_total_min = vmin;

	/* TODO: If multiple pipes are to be supported, you need
	 * some GSL stuff
	 */
	for (i = 0; i < num_pipes; i++) {
		pipe_ctx[i]->stream_res.tg->funcs->set_drr(pipe_ctx[i]->stream_res.tg, &params);
	}
}

static void get_position(struct pipe_ctx **pipe_ctx,
		int num_pipes,
		struct crtc_position *position)
{
	int i = 0;

	/* TODO: handle pipes > 1
	 */
	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->stream_res.tg->funcs->get_position(pipe_ctx[i]->stream_res.tg, position);
}

static void set_static_screen_control(struct pipe_ctx **pipe_ctx,
		int num_pipes, const struct dc_static_screen_events *events)
{
	unsigned int i;
	unsigned int value = 0;

	if (events->surface_update)
		value |= 0x80;
	if (events->cursor_update)
		value |= 0x2;

	for (i = 0; i < num_pipes; i++)
		pipe_ctx[i]->stream_res.tg->funcs->
			set_static_screen_control(pipe_ctx[i]->stream_res.tg, value);
}

static void set_plane_config(
	const struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct resource_context *res_ctx)
{
	/* TODO */
	program_gamut_remap(pipe_ctx);
}

static void dcn10_config_stereo_parameters(
		struct dc_stream_state *stream, struct crtc_stereo_flags *flags)
{
	enum view_3d_format view_format = stream->view_format;
	enum dc_timing_3d_format timing_3d_format =\
			stream->timing.timing_3d_format;
	bool non_stereo_timing = false;

	if (timing_3d_format == TIMING_3D_FORMAT_NONE ||
		timing_3d_format == TIMING_3D_FORMAT_SIDE_BY_SIDE ||
		timing_3d_format == TIMING_3D_FORMAT_TOP_AND_BOTTOM)
		non_stereo_timing = true;

	if (non_stereo_timing == false &&
		view_format == VIEW_3D_FORMAT_FRAME_SEQUENTIAL) {

		flags->PROGRAM_STEREO         = 1;
		flags->PROGRAM_POLARITY       = 1;
		if (timing_3d_format == TIMING_3D_FORMAT_INBAND_FA ||
			timing_3d_format == TIMING_3D_FORMAT_DP_HDMI_INBAND_FA ||
			timing_3d_format == TIMING_3D_FORMAT_SIDEBAND_FA) {
			enum display_dongle_type dongle = \
					stream->sink->link->ddc->dongle_type;
			if (dongle == DISPLAY_DONGLE_DP_VGA_CONVERTER ||
				dongle == DISPLAY_DONGLE_DP_DVI_CONVERTER ||
				dongle == DISPLAY_DONGLE_DP_HDMI_CONVERTER)
				flags->DISABLE_STEREO_DP_SYNC = 1;
		}
		flags->RIGHT_EYE_POLARITY =\
				stream->timing.flags.RIGHT_EYE_3D_POLARITY;
		if (timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
			flags->FRAME_PACKED = 1;
	}

	return;
}

static void dcn10_setup_stereo(struct pipe_ctx *pipe_ctx, struct dc *dc)
{
	struct crtc_stereo_flags flags = { 0 };
	struct dc_stream_state *stream = pipe_ctx->stream;

	dcn10_config_stereo_parameters(stream, &flags);

	pipe_ctx->stream_res.opp->funcs->opp_set_stereo_polarity(
		pipe_ctx->stream_res.opp,
		flags.PROGRAM_STEREO == 1 ? true:false,
		stream->timing.flags.RIGHT_EYE_3D_POLARITY == 1 ? true:false);

	pipe_ctx->stream_res.tg->funcs->program_stereo(
		pipe_ctx->stream_res.tg,
		&stream->timing,
		&flags);

	return;
}

static void dcn10_wait_for_mpcc_disconnect(
		struct dc *dc,
		struct resource_pool *res_pool,
		struct pipe_ctx *pipe_ctx)
{
	int i;

	if (dc->debug.sanity_checks) {
		verify_allow_pstate_change_high(dc->hwseq);
	}

	if (!pipe_ctx->stream_res.opp)
		return;

	for (i = 0; i < MAX_PIPES; i++) {
		if (pipe_ctx->stream_res.opp->mpcc_disconnect_pending[i]) {
			res_pool->mpc->funcs->wait_for_idle(res_pool->mpc, i);
			pipe_ctx->stream_res.opp->mpcc_disconnect_pending[i] = false;
			res_pool->hubps[i]->funcs->set_blank(res_pool->hubps[i], true);
			/*dm_logger_write(dc->ctx->logger, LOG_ERROR,
					"[debug_mpo: wait_for_mpcc finished waiting on mpcc %d]\n",
					i);*/
		}
	}

	if (dc->debug.sanity_checks) {
		verify_allow_pstate_change_high(dc->hwseq);
	}

}

static bool dcn10_dummy_display_power_gating(
	struct dc *dc,
	uint8_t controller_id,
	struct dc_bios *dcb,
	enum pipe_gating_control power_gating)
{
	return true;
}

void dcn10_update_pending_status(struct pipe_ctx *pipe_ctx)
{
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;

	if (plane_state == NULL)
		return;

	plane_state->status.is_flip_pending =
			pipe_ctx->plane_res.hubp->funcs->hubp_is_flip_pending(
					pipe_ctx->plane_res.hubp);

	plane_state->status.current_address = pipe_ctx->plane_res.hubp->current_address;
	if (pipe_ctx->plane_res.hubp->current_address.type == PLN_ADDR_TYPE_GRPH_STEREO &&
			tg->funcs->is_stereo_left_eye) {
		plane_state->status.is_right_eye =
				!tg->funcs->is_stereo_left_eye(pipe_ctx->stream_res.tg);
	}
}



static const struct hw_sequencer_funcs dcn10_funcs = {
	.program_gamut_remap = program_gamut_remap,
	.program_csc_matrix = program_csc_matrix,
	.init_hw = dcn10_init_hw,
	.apply_ctx_to_hw = dce110_apply_ctx_to_hw,
	.apply_ctx_for_surface = dcn10_apply_ctx_for_surface,
	.set_plane_config = set_plane_config,
	.update_plane_addr = dcn10_update_plane_addr,
	.update_dchub = dcn10_update_dchub,
	.update_pending_status = dcn10_update_pending_status,
	.set_input_transfer_func = dcn10_set_input_transfer_func,
	.set_output_transfer_func = dcn10_set_output_transfer_func,
	.power_down = dce110_power_down,
	.enable_accelerated_mode = dce110_enable_accelerated_mode,
	.enable_timing_synchronization = dcn10_enable_timing_synchronization,
	.update_info_frame = dce110_update_info_frame,
	.enable_stream = dce110_enable_stream,
	.disable_stream = dce110_disable_stream,
	.unblank_stream = dce110_unblank_stream,
	.enable_display_power_gating = dcn10_dummy_display_power_gating,
	.power_down_front_end = dcn10_power_down_fe,
	.power_on_front_end = dcn10_power_on_fe,
	.pipe_control_lock = dcn10_pipe_control_lock,
	.set_bandwidth = dcn10_set_bandwidth,
	.reset_hw_ctx_wrap = reset_hw_ctx_wrap,
	.prog_pixclk_crtc_otg = dcn10_prog_pixclk_crtc_otg,
	.set_drr = set_drr,
	.get_position = get_position,
	.set_static_screen_control = set_static_screen_control,
	.setup_stereo = dcn10_setup_stereo,
	.set_avmute = dce110_set_avmute,
	.log_hw_state = dcn10_log_hw_state,
	.wait_for_mpcc_disconnect = dcn10_wait_for_mpcc_disconnect,
	.ready_shared_resources = ready_shared_resources,
	.optimize_shared_resources = optimize_shared_resources,
	.edp_backlight_control = hwss_edp_backlight_control,
	.edp_power_control = hwss_edp_power_control
};


void dcn10_hw_sequencer_construct(struct dc *dc)
{
	dc->hwss = dcn10_funcs;
}

