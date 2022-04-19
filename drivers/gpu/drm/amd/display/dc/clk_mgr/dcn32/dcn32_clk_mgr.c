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

#include "dccg.h"
#include "clk_mgr_internal.h"

#include "dcn32/dcn32_clk_mgr_smu_msg.h"
#include "dcn20/dcn20_clk_mgr.h"
#include "dce100/dce_clk_mgr.h"
#include "reg_helper.h"
#include "core_types.h"
#include "dm_helpers.h"

#include "atomfirmware.h"
#include "smu13_driver_if.h"

#include "dcn/dcn_3_2_0_offset.h"
#include "dcn/dcn_3_2_0_sh_mask.h"

#include "dcn32/dcn32_clk_mgr.h"

#define DCN_BASE__INST0_SEG1                       0x000000C0

#define mmCLK1_CLK_PLL_REQ                              0x16E37
#define mmCLK1_CLK0_DFS_CNTL                            0x16E69
#define mmCLK1_CLK1_DFS_CNTL                            0x16E6C
#define mmCLK1_CLK2_DFS_CNTL                            0x16E6F
#define mmCLK1_CLK3_DFS_CNTL                            0x16E72
#define mmCLK1_CLK4_DFS_CNTL                            0x16E75

#define CLK1_CLK_PLL_REQ__FbMult_int_MASK               0x000001ffUL
#define CLK1_CLK_PLL_REQ__PllSpineDiv_MASK              0x0000f000UL
#define CLK1_CLK_PLL_REQ__FbMult_frac_MASK              0xffff0000UL
#define CLK1_CLK_PLL_REQ__FbMult_int__SHIFT             0x00000000
#define CLK1_CLK_PLL_REQ__PllSpineDiv__SHIFT            0x0000000c
#define CLK1_CLK_PLL_REQ__FbMult_frac__SHIFT            0x00000010

#define mmCLK01_CLK0_CLK_PLL_REQ                        0x16E37
#define mmCLK01_CLK0_CLK0_DFS_CNTL                      0x16E64
#define mmCLK01_CLK0_CLK1_DFS_CNTL                      0x16E67
#define mmCLK01_CLK0_CLK2_DFS_CNTL                      0x16E6A
#define mmCLK01_CLK0_CLK3_DFS_CNTL                      0x16E6D
#define mmCLK01_CLK0_CLK4_DFS_CNTL                      0x16E70

#define CLK0_CLK_PLL_REQ__FbMult_int_MASK               0x000001ffL
#define CLK0_CLK_PLL_REQ__PllSpineDiv_MASK              0x0000f000L
#define CLK0_CLK_PLL_REQ__FbMult_frac_MASK              0xffff0000L
#define CLK0_CLK_PLL_REQ__FbMult_int__SHIFT             0x00000000
#define CLK0_CLK_PLL_REQ__PllSpineDiv__SHIFT            0x0000000c
#define CLK0_CLK_PLL_REQ__FbMult_frac__SHIFT            0x00000010

#undef FN
#define FN(reg_name, field_name) \
	clk_mgr->clk_mgr_shift->field_name, clk_mgr->clk_mgr_mask->field_name

#define REG(reg) \
	(clk_mgr->regs->reg)

#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
					reg ## reg_name

#define CLK_SR_DCN32(reg_name)\
	.reg_name = mm ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn32 = {
	CLK_REG_LIST_DCN32()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn32 = {
	CLK_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn32 = {
	CLK_COMMON_MASK_SH_LIST_DCN32(_MASK)
};


#define CLK_SR_DCN321(reg_name, block, inst)\
	.reg_name = mm ## block ## _ ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn321 = {
	CLK_REG_LIST_DCN321()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn321 = {
	CLK_COMMON_MASK_SH_LIST_DCN321(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn321 = {
	CLK_COMMON_MASK_SH_LIST_DCN321(_MASK)
};


/* Query SMU for all clock states for a particular clock */
static void dcn32_init_single_clock(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, unsigned int *entry_0,
		unsigned int *num_levels)
{
	unsigned int i;
	char *entry_i = (char *)entry_0;

	uint32_t ret = dcn30_smu_get_dpm_freq_by_index(clk_mgr, clk, 0xFF);

	if (ret & (1 << 31))
		/* fine-grained, only min and max */
		*num_levels = 2;
	else
		/* discrete, a number of fixed states */
		/* will set num_levels to 0 on failure */
		*num_levels = ret & 0xFF;

	/* if the initial message failed, num_levels will be 0 */
	for (i = 0; i < *num_levels; i++) {
		*((unsigned int *)entry_i) = (dcn30_smu_get_dpm_freq_by_index(clk_mgr, clk, i) & 0xFFFF);
		entry_i += sizeof(clk_mgr->base.bw_params->clk_table.entries[0]);
	}
}

static void dcn32_build_wm_range_table(struct clk_mgr_internal *clk_mgr)
{
	/* defaults */
	double pstate_latency_us = clk_mgr->base.ctx->dc->dml.soc.dram_clock_change_latency_us;
	double fclk_change_latency_us = clk_mgr->base.ctx->dc->dml.soc.fclk_change_latency_us;
	double sr_exit_time_us = clk_mgr->base.ctx->dc->dml.soc.sr_exit_time_us;
	double sr_enter_plus_exit_time_us = clk_mgr->base.ctx->dc->dml.soc.sr_enter_plus_exit_time_us;
	/* For min clocks use as reported by PM FW and report those as min */
	uint16_t min_uclk_mhz			= clk_mgr->base.bw_params->clk_table.entries[0].memclk_mhz;
	uint16_t min_dcfclk_mhz			= clk_mgr->base.bw_params->clk_table.entries[0].dcfclk_mhz;
	uint16_t setb_min_uclk_mhz		= min_uclk_mhz;
	uint16_t setb_min_dcfclk_mhz	= min_dcfclk_mhz;
	/* For Set B ranges use min clocks state 2 when available, and report those to PM FW */
	if (clk_mgr->base.ctx->dc->dml.soc.clock_limits[2].dcfclk_mhz)
		setb_min_dcfclk_mhz = clk_mgr->base.ctx->dc->dml.soc.clock_limits[2].dcfclk_mhz;
	if (clk_mgr->base.bw_params->clk_table.entries[2].memclk_mhz)
		setb_min_uclk_mhz = clk_mgr->base.bw_params->clk_table.entries[2].memclk_mhz;

	/* Set A - Normal - default values */
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us = pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.sr_exit_time_us = sr_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set B - Performance - higher clocks, using DPM[2] DCFCLK and UCLK */
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us = pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us = sr_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_dcfclk = setb_min_dcfclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_uclk = setb_min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set C - Dummy P-State - P-State latency set to "dummy p-state" value */
	/* 'DalDummyClockChangeLatencyNs' registry key option set to 0x7FFFFFFF can be used to disable Set C for dummy p-state */
	if (clk_mgr->base.ctx->dc->bb_overrides.dummy_clock_change_latency_ns != 0x7FFFFFFF) {
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].valid = true;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.pstate_latency_us = 38;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.fclk_change_latency_us = fclk_change_latency_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us = sr_exit_time_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.wm_type = WATERMARKS_DUMMY_PSTATE;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_dcfclk = 0xFFFF;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_uclk = min_uclk_mhz;
		clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_uclk = 0xFFFF;
		clk_mgr->base.bw_params->dummy_pstate_table[0].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[0].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[0].dummy_pstate_latency_us = 38;
		clk_mgr->base.bw_params->dummy_pstate_table[1].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[1].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[1].dummy_pstate_latency_us = 9;
		clk_mgr->base.bw_params->dummy_pstate_table[2].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[2].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[2].dummy_pstate_latency_us = 8;
		clk_mgr->base.bw_params->dummy_pstate_table[3].dram_speed_mts = clk_mgr->base.bw_params->clk_table.entries[3].memclk_mhz * 16;
		clk_mgr->base.bw_params->dummy_pstate_table[3].dummy_pstate_latency_us = 5;
	}
	/* Set D - MALL - SR enter and exit time specific to MALL, TBD after bringup or later phase for now use DRAM values / 2 */
	/* For MALL DRAM clock change latency is N/A, for watermak calculations use lowest value dummy P state latency */
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us = clk_mgr->base.bw_params->dummy_pstate_table[3].dummy_pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.fclk_change_latency_us = fclk_change_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us = sr_exit_time_us / 2; // TBD
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us / 2; // TBD
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.wm_type = WATERMARKS_MALL;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_uclk = 0xFFFF;
}

void dcn32_init_clocks(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	unsigned int num_levels;

	memset(&(clk_mgr_base->clks), 0, sizeof(struct dc_clocks));
	clk_mgr_base->clks.p_state_change_support = true;
	clk_mgr_base->clks.prev_p_state_change_support = true;
	clk_mgr_base->clks.fclk_prev_p_state_change_support = true;
	clk_mgr->smu_present = false;

	if (!clk_mgr_base->bw_params)
		return;

	if (!clk_mgr_base->force_smu_not_present && dcn30_smu_get_smu_version(clk_mgr, &clk_mgr->smu_ver))
		clk_mgr->smu_present = true;

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_check_driver_if_version(clk_mgr);
	dcn30_smu_check_msg_header_version(clk_mgr);

	/* DCFCLK */
	dcn32_init_single_clock(clk_mgr, PPCLK_DCFCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dcfclk_mhz,
			&num_levels);

	/* SOCCLK */
	dcn32_init_single_clock(clk_mgr, PPCLK_SOCCLK,
					&clk_mgr_base->bw_params->clk_table.entries[0].socclk_mhz,
					&num_levels);
	/* DTBCLK */
	dcn32_init_single_clock(clk_mgr, PPCLK_DTBCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz,
			&num_levels);

	/* DISPCLK */
	dcn32_init_single_clock(clk_mgr, PPCLK_DISPCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dispclk_mhz,
			&num_levels);


	/* Get UCLK, update bounding box */
	clk_mgr_base->funcs->get_memclk_states_from_smu(clk_mgr_base);

	/* WM range table */
	dcn32_build_wm_range_table(clk_mgr);
}

static void dcn32_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	struct dc *dc = clk_mgr_base->ctx->dc;
	int display_count;
	bool update_dppclk = false;
	bool update_dispclk = false;
	bool enter_display_off = false;
	bool dpp_clock_lowered = false;
	struct dmcu *dmcu = clk_mgr_base->ctx->dc->res_pool->dmcu;
	bool force_reset = false;
	bool update_uclk = false;
	bool p_state_change_support;
	bool fclk_p_state_change_support;
	int total_plane_count;

	if (dc->work_arounds.skip_clock_update)
		return;

	if (clk_mgr_base->clks.dispclk_khz == 0 ||
			(dc->debug.force_clock_mode & 0x1)) {
		/* This is from resume or boot up, if forced_clock cfg option used,
		 * we bypass program dispclk and DPPCLK, but need set them for S3.
		 */
		force_reset = true;

		dcn2_read_clocks_from_hw_dentist(clk_mgr_base);

		/* Force_clock_mode 0x1:  force reset the clock even it is the same clock
		 * as long as it is in Passive level.
		 */
	}
	display_count = clk_mgr_helper_get_active_display_cnt(dc, context);

	if (display_count == 0)
		enter_display_off = true;

	if (enter_display_off == safe_to_lower)
		dcn30_smu_set_num_of_displays(clk_mgr, display_count);

	if (dc->debug.force_min_dcfclk_mhz > 0)
		new_clocks->dcfclk_khz = (new_clocks->dcfclk_khz > (dc->debug.force_min_dcfclk_mhz * 1000)) ?
				new_clocks->dcfclk_khz : (dc->debug.force_min_dcfclk_mhz * 1000);

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DCFCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_khz));
	}

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		dcn30_smu_set_min_deep_sleep_dcef_clk(clk_mgr, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_deep_sleep_khz));
	}

	if (should_set_clock(safe_to_lower, new_clocks->socclk_khz, clk_mgr_base->clks.socclk_khz))
		/* We don't actually care about socclk, don't notify SMU of hard min */
		clk_mgr_base->clks.socclk_khz = new_clocks->socclk_khz;

	clk_mgr_base->clks.prev_p_state_change_support = clk_mgr_base->clks.p_state_change_support;
	clk_mgr_base->clks.fclk_prev_p_state_change_support = clk_mgr_base->clks.fclk_p_state_change_support;

	total_plane_count = clk_mgr_helper_get_active_plane_cnt(dc, context);
	p_state_change_support = new_clocks->p_state_change_support || (total_plane_count == 0);
	fclk_p_state_change_support = new_clocks->fclk_p_state_change_support || (total_plane_count == 0);
	if (should_update_pstate_support(safe_to_lower, p_state_change_support, clk_mgr_base->clks.p_state_change_support)) {
		clk_mgr_base->clks.p_state_change_support = p_state_change_support;

		/* to disable P-State switching, set UCLK min = max */
		if (!clk_mgr_base->clks.p_state_change_support)
			dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					clk_mgr_base->bw_params->clk_table.entries[clk_mgr_base->bw_params->clk_table.num_entries - 1].memclk_mhz);
	}

	if (clk_mgr_base->ctx->dce_version != DCN_VERSION_3_21 &&
			should_update_pstate_support(safe_to_lower, fclk_p_state_change_support, clk_mgr_base->clks.fclk_p_state_change_support)) {
		clk_mgr_base->clks.fclk_p_state_change_support = fclk_p_state_change_support;

		/* To disable FCLK P-state switching, send FCLK_PSTATE_NOTSUPPORTED message to PMFW */
		if (!clk_mgr_base->clks.fclk_p_state_change_support) {
			/* Handle code for sending a message to PMFW that FCLK P-state change is not supported */
			dcn32_smu_send_fclk_pstate_message(clk_mgr, FCLK_PSTATE_NOTSUPPORTED);
		}
	}

	/* Always update saved value, even if new value not set due to P-State switching unsupported */
	if (should_set_clock(safe_to_lower, new_clocks->dramclk_khz, clk_mgr_base->clks.dramclk_khz)) {
		clk_mgr_base->clks.dramclk_khz = new_clocks->dramclk_khz;
		update_uclk = true;
	}

	/* set UCLK to requested value if P-State switching is supported, or to re-enable P-State switching */
	if (clk_mgr_base->clks.p_state_change_support &&
			(update_uclk || !clk_mgr_base->clks.prev_p_state_change_support))
		dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz));

	if (clk_mgr_base->ctx->dce_version != DCN_VERSION_3_21 &&
			clk_mgr_base->clks.fclk_p_state_change_support &&
			(update_uclk || !clk_mgr_base->clks.fclk_prev_p_state_change_support)) {
		/* Handle the code for sending a message to PMFW that FCLK P-state change is supported */
		dcn32_smu_send_fclk_pstate_message(clk_mgr, FCLK_PSTATE_SUPPORTED);
	}

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr_base->clks.dppclk_khz)) {
		if (clk_mgr_base->clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;

		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DPPCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dppclk_khz));
		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;
		dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DISPCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dispclk_khz));
		update_dispclk = true;
	}

	if (dc->config.forced_clocks == false || (force_reset && safe_to_lower)) {
		if (dpp_clock_lowered) {
			/* if clock is being lowered, increase DTO before lowering refclk */
			dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
			dcn20_update_clocks_update_dentist(clk_mgr, context);
		} else {
			/* if clock is being raised, increase refclk before lowering DTO */
			if (update_dppclk || update_dispclk)
				dcn20_update_clocks_update_dentist(clk_mgr, context);
			/* There is a check inside dcn20_update_clocks_update_dpp_dto which ensures
			 * that we do not lower dto when it is not safe to lower. We do not need to
			 * compare the current and new dppclk before calling this function.
			 */
			dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		}
	}

	if (update_dispclk && dmcu && dmcu->funcs->is_dmcu_initialized(dmcu))
		/*update dmcu for wait_loop count*/
		dmcu->funcs->set_psr_wait_loop(dmcu,
				clk_mgr_base->clks.dispclk_khz / 1000 / 7);
}

void dcn32_clock_read_ss_info(struct clk_mgr_internal *clk_mgr)
{
	struct dc_bios *bp = clk_mgr->base.ctx->dc_bios;
	int ss_info_num = bp->funcs->get_ss_entry_number(
			bp, AS_SIGNAL_TYPE_GPU_PLL);

	if (ss_info_num) {
		struct spread_spectrum_info info = { { 0 } };
		enum bp_result result = bp->funcs->get_spread_spectrum_info(
				bp, AS_SIGNAL_TYPE_GPU_PLL, 0, &info);

		/* SSInfo.spreadSpectrumPercentage !=0 would be sign
		 * that SS is enabled
		 */
		if (result == BP_RESULT_OK &&
				info.spread_spectrum_percentage != 0) {
			clk_mgr->ss_on_dprefclk = true;
			clk_mgr->dprefclk_ss_divider = info.spread_percentage_divider;

			if (info.type.CENTER_MODE == 0) {
				/* Currently for DP Reference clock we
				 * need only SS percentage for
				 * downspread
				 */
				clk_mgr->dprefclk_ss_percentage =
						info.spread_spectrum_percentage;
			}
		}
	}
}
static void dcn32_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	unsigned int i;
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	WatermarksExternal_t *table = (WatermarksExternal_t *) clk_mgr->wm_range_table;

	if (!clk_mgr->smu_present)
		return;

	if (!table)
		return;

	memset(table, 0, sizeof(*table));

	/* collect valid ranges, place in pmfw table */
	for (i = 0; i < WM_SET_COUNT; i++)
		if (clk_mgr->base.bw_params->wm_table.nv_entries[i].valid) {
			table->Watermarks.WatermarkRow[i].WmSetting = i;
			table->Watermarks.WatermarkRow[i].Flags = clk_mgr->base.bw_params->wm_table.nv_entries[i].pmfw_breakdown.wm_type;
		}
	dcn30_smu_set_dram_addr_high(clk_mgr, clk_mgr->wm_range_table_addr >> 32);
	dcn30_smu_set_dram_addr_low(clk_mgr, clk_mgr->wm_range_table_addr & 0xFFFFFFFF);
	dcn32_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

/* Set min memclk to minimum, either constrained by the current mode or DPM0 */
static void dcn32_set_hard_min_memclk(struct clk_mgr *clk_mgr_base, bool current_mode)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	if (current_mode) {
		if (clk_mgr_base->clks.p_state_change_support)
			dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz));
		else
			dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					clk_mgr_base->bw_params->clk_table.entries[clk_mgr_base->bw_params->clk_table.num_entries - 1].memclk_mhz);
	} else {
		dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
				clk_mgr_base->bw_params->clk_table.entries[0].memclk_mhz);
	}
}

/* Set max memclk to highest DPM value */
static void dcn32_set_hard_max_memclk(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_set_hard_max_by_freq(clk_mgr, PPCLK_UCLK,
			clk_mgr_base->bw_params->clk_table.entries[clk_mgr_base->bw_params->clk_table.num_entries - 1].memclk_mhz);
}

/* Get current memclk states, update bounding box */
static void dcn32_get_memclk_states_from_smu(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	unsigned int num_levels;

	if (!clk_mgr->smu_present)
		return;

	/* Refresh memclk states */
	dcn32_init_single_clock(clk_mgr, PPCLK_UCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].memclk_mhz,
			&num_levels);
	clk_mgr_base->bw_params->clk_table.num_entries = num_levels ? num_levels : 1;

	/* Refresh bounding box */
	clk_mgr_base->ctx->dc->res_pool->funcs->update_bw_bounding_box(
			clk_mgr->base.ctx->dc, clk_mgr_base->bw_params);
}

static bool dcn32_are_clock_states_equal(struct dc_clocks *a,
					struct dc_clocks *b)
{
	if (a->dispclk_khz != b->dispclk_khz)
		return false;
	else if (a->dppclk_khz != b->dppclk_khz)
		return false;
	else if (a->dcfclk_khz != b->dcfclk_khz)
		return false;
	else if (a->dcfclk_deep_sleep_khz != b->dcfclk_deep_sleep_khz)
		return false;
	else if (a->dramclk_khz != b->dramclk_khz)
		return false;
	else if (a->p_state_change_support != b->p_state_change_support)
		return false;
	else if (a->fclk_p_state_change_support != b->fclk_p_state_change_support)
		return false;

	return true;
}

static void dcn32_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_set_pme_workaround(clk_mgr);
}

static bool dcn32_is_smu_present(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	return clk_mgr->smu_present;
}


static struct clk_mgr_funcs dcn32_funcs = {
		.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
		.update_clocks = dcn32_update_clocks,
		.init_clocks = dcn32_init_clocks,
		.notify_wm_ranges = dcn32_notify_wm_ranges,
		.set_hard_min_memclk = dcn32_set_hard_min_memclk,
		.set_hard_max_memclk = dcn32_set_hard_max_memclk,
		.get_memclk_states_from_smu = dcn32_get_memclk_states_from_smu,
		.are_clock_states_equal = dcn32_are_clock_states_equal,
		.enable_pme_wa = dcn32_enable_pme_wa,
		.is_smu_present = dcn32_is_smu_present,
};

void dcn32_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	clk_mgr->base.ctx = ctx;
	clk_mgr->base.funcs = &dcn32_funcs;
	if (ASICREV_IS_GC_11_0_2(clk_mgr->base.ctx->asic_id.hw_internal_rev)) {
		clk_mgr->regs = &clk_mgr_regs_dcn321;
		clk_mgr->clk_mgr_shift = &clk_mgr_shift_dcn321;
		clk_mgr->clk_mgr_mask = &clk_mgr_mask_dcn321;
	} else {
		clk_mgr->regs = &clk_mgr_regs_dcn32;
		clk_mgr->clk_mgr_shift = &clk_mgr_shift_dcn32;
		clk_mgr->clk_mgr_mask = &clk_mgr_mask_dcn32;
	}

	clk_mgr->dccg = dccg;
	clk_mgr->dfs_bypass_disp_clk = 0;

	clk_mgr->dprefclk_ss_percentage = 0;
	clk_mgr->dprefclk_ss_divider = 1000;
	clk_mgr->ss_on_dprefclk = false;
	clk_mgr->dfs_ref_freq_khz = 100000;

	clk_mgr->base.dprefclk_khz = 717000; /* Changed as per DCN3.2_clock_frequency doc */
	clk_mgr->dccg->ref_dtbclk_khz = 268750;

	/* integer part is now VCO frequency in kHz */
	clk_mgr->base.dentist_vco_freq_khz = 4300000;//dcn32_get_vco_frequency_from_reg(clk_mgr);
	/* in case we don't get a value from the register, use default */
	if (clk_mgr->base.dentist_vco_freq_khz == 0)
		clk_mgr->base.dentist_vco_freq_khz = 4300000; /* Updated as per HW docs */

	if (clk_mgr->base.boot_snapshot.dprefclk != 0) {
		//ASSERT(clk_mgr->base.dprefclk_khz == clk_mgr->base.boot_snapshot.dprefclk);
		//clk_mgr->base.dprefclk_khz = clk_mgr->base.boot_snapshot.dprefclk;
	}
	dcn32_clock_read_ss_info(clk_mgr);

	clk_mgr->dfs_bypass_enabled = false;

	clk_mgr->smu_present = false;

	clk_mgr->base.bw_params = kzalloc(sizeof(*clk_mgr->base.bw_params), GFP_KERNEL);

	/* need physical address of table to give to PMFW */
	clk_mgr->wm_range_table = dm_helpers_allocate_gpu_mem(clk_mgr->base.ctx,
			DC_MEM_ALLOC_TYPE_GART, sizeof(WatermarksExternal_t),
			&clk_mgr->wm_range_table_addr);
}

void dcn32_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr)
{
	if (clk_mgr->base.bw_params)
		kfree(clk_mgr->base.bw_params);

	if (clk_mgr->wm_range_table)
		dm_helpers_free_gpu_mem(clk_mgr->base.ctx, DC_MEM_ALLOC_TYPE_GART,
				clk_mgr->wm_range_table);
}

