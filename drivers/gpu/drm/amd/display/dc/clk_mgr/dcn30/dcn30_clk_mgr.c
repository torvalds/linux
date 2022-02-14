/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include "dcn30_clk_mgr_smu_msg.h"
#include "dcn20/dcn20_clk_mgr.h"
#include "dce100/dce_clk_mgr.h"
#include "reg_helper.h"
#include "core_types.h"
#include "dm_helpers.h"

#include "atomfirmware.h"


#include "sienna_cichlid_ip_offset.h"
#include "dcn/dcn_3_0_0_offset.h"
#include "dcn/dcn_3_0_0_sh_mask.h"

#include "nbio/nbio_7_4_offset.h"

#include "dpcs/dpcs_3_0_0_offset.h"
#include "dpcs/dpcs_3_0_0_sh_mask.h"

#include "mmhub/mmhub_2_0_0_offset.h"
#include "mmhub/mmhub_2_0_0_sh_mask.h"
/*we don't have clk folder yet*/
#include "dcn30/dcn30_clk_mgr.h"

#undef FN
#define FN(reg_name, field_name) \
	clk_mgr->clk_mgr_shift->field_name, clk_mgr->clk_mgr_mask->field_name

#define REG(reg) \
	(clk_mgr->regs->reg)

#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

#undef CLK_SRI
#define CLK_SRI(reg_name, block, inst)\
	.reg_name = mm ## block ## _ ## reg_name

static const struct clk_mgr_registers clk_mgr_regs = {
	CLK_REG_LIST_DCN3()
};

static const struct clk_mgr_shift clk_mgr_shift = {
	CLK_COMMON_MASK_SH_LIST_DCN20_BASE(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask = {
	CLK_COMMON_MASK_SH_LIST_DCN20_BASE(_MASK)
};


/* Query SMU for all clock states for a particular clock */
static void dcn3_init_single_clock(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, unsigned int *entry_0, unsigned int *num_levels)
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

static noinline void dcn3_build_wm_range_table(struct clk_mgr_internal *clk_mgr)
{
	/* defaults */
	double pstate_latency_us = clk_mgr->base.ctx->dc->dml.soc.dram_clock_change_latency_us;
	double sr_exit_time_us = clk_mgr->base.ctx->dc->dml.soc.sr_exit_time_us;
	double sr_enter_plus_exit_time_us = clk_mgr->base.ctx->dc->dml.soc.sr_enter_plus_exit_time_us;
	uint16_t min_uclk_mhz = clk_mgr->base.bw_params->clk_table.entries[0].memclk_mhz;

	/* Set A - Normal - default values*/
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.pstate_latency_us = pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.sr_exit_time_us = sr_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_dcfclk = 0;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set B - Performance - higher minimum clocks */
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].valid = true;
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.pstate_latency_us = pstate_latency_us;
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.sr_exit_time_us = sr_exit_time_us;
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_dcfclk = TUNED VALUE;
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_dcfclk = 0xFFFF;
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.min_uclk = TUNED VALUE;
//	clk_mgr->base.bw_params->wm_table.nv_entries[WM_B].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set C - Dummy P-State - P-State latency set to "dummy p-state" value */
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.pstate_latency_us = clk_mgr->base.ctx->dc->dml.soc.dummy_pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.sr_exit_time_us = sr_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].dml_input.sr_enter_plus_exit_time_us = sr_enter_plus_exit_time_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.wm_type = WATERMARKS_DUMMY_PSTATE;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_dcfclk = 0;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_C].pmfw_breakdown.max_uclk = 0xFFFF;

	/* Set D - MALL - SR enter and exit times adjusted for MALL */
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].valid = true;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.pstate_latency_us = pstate_latency_us;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.sr_exit_time_us = 2;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].dml_input.sr_enter_plus_exit_time_us = 4;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.wm_type = WATERMARKS_MALL;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_dcfclk = 0;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_dcfclk = 0xFFFF;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.min_uclk = min_uclk_mhz;
	clk_mgr->base.bw_params->wm_table.nv_entries[WM_D].pmfw_breakdown.max_uclk = 0xFFFF;
}

void dcn3_init_clocks(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	unsigned int num_levels;

	memset(&(clk_mgr_base->clks), 0, sizeof(struct dc_clocks));
	clk_mgr_base->clks.p_state_change_support = true;
	clk_mgr_base->clks.prev_p_state_change_support = true;
	clk_mgr->smu_present = false;

	if (!clk_mgr_base->bw_params)
		return;

	if (!clk_mgr_base->force_smu_not_present && dcn30_smu_get_smu_version(clk_mgr, &clk_mgr->smu_ver))
		clk_mgr->smu_present = true;

	if (!clk_mgr->smu_present)
		return;

	// do we fail if these fail? if so, how? do we not care to check?
	dcn30_smu_check_driver_if_version(clk_mgr);
	dcn30_smu_check_msg_header_version(clk_mgr);

	/* DCFCLK */
	dcn3_init_single_clock(clk_mgr, PPCLK_DCEFCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dcfclk_mhz,
			&num_levels);
	dcn30_smu_set_min_deep_sleep_dcef_clk(clk_mgr, 0);

	/* DTBCLK */
	dcn3_init_single_clock(clk_mgr, PPCLK_DTBCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz,
			&num_levels);

	/* SOCCLK */
	dcn3_init_single_clock(clk_mgr, PPCLK_SOCCLK,
					&clk_mgr_base->bw_params->clk_table.entries[0].socclk_mhz,
					&num_levels);
	// DPREFCLK ???

	/* DISPCLK */
	dcn3_init_single_clock(clk_mgr, PPCLK_DISPCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dispclk_mhz,
			&num_levels);

	/* DPPCLK */
	dcn3_init_single_clock(clk_mgr, PPCLK_PIXCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dppclk_mhz,
			&num_levels);

	/* PHYCLK */
	dcn3_init_single_clock(clk_mgr, PPCLK_PHYCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].phyclk_mhz,
			&num_levels);

	/* Get UCLK, update bounding box */
	clk_mgr_base->funcs->get_memclk_states_from_smu(clk_mgr_base);

	/* WM range table */
	DC_FP_START();
	dcn3_build_wm_range_table(clk_mgr);
	DC_FP_END();
}

static int dcn30_get_vco_frequency_from_reg(struct clk_mgr_internal *clk_mgr)
{
	/* get FbMult value */
	struct fixed31_32 pll_req;
	/* get FbMult value */
	uint32_t pll_req_reg = REG_READ(CLK0_CLK_PLL_REQ);

	/* set up a fixed-point number
	 * this works because the int part is on the right edge of the register
	 * and the frac part is on the left edge
	 */
	pll_req = dc_fixpt_from_int(pll_req_reg & clk_mgr->clk_mgr_mask->FbMult_int);
	pll_req.value |= pll_req_reg & clk_mgr->clk_mgr_mask->FbMult_frac;

	/* multiply by REFCLK period */
	pll_req = dc_fixpt_mul_int(pll_req, clk_mgr->dfs_ref_freq_khz);

	return dc_fixpt_floor(pll_req);
}

static void dcn3_update_clocks(struct clk_mgr *clk_mgr_base,
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
	bool update_pstate_unsupported_clk = false;
	struct dmcu *dmcu = clk_mgr_base->ctx->dc->res_pool->dmcu;
	bool force_reset = false;
	bool update_uclk = false;
	bool p_state_change_support;
	int total_plane_count;

	if (dc->work_arounds.skip_clock_update || !clk_mgr->smu_present)
		return;

	if (clk_mgr_base->clks.dispclk_khz == 0 ||
			(dc->debug.force_clock_mode & 0x1)) {
		/* this is from resume or boot up, if forced_clock cfg option used, we bypass program dispclk and DPPCLK, but need set them for S3. */
		force_reset = true;

		dcn2_read_clocks_from_hw_dentist(clk_mgr_base);

		/* force_clock_mode 0x1:  force reset the clock even it is the same clock as long as it is in Passive level. */
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
		dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DCEFCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_khz));
	}

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		dcn30_smu_set_min_deep_sleep_dcef_clk(clk_mgr, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_deep_sleep_khz));
	}

	if (should_set_clock(safe_to_lower, new_clocks->socclk_khz, clk_mgr_base->clks.socclk_khz))
		/* We don't actually care about socclk, don't notify SMU of hard min */
		clk_mgr_base->clks.socclk_khz = new_clocks->socclk_khz;

	clk_mgr_base->clks.prev_p_state_change_support = clk_mgr_base->clks.p_state_change_support;
	total_plane_count = clk_mgr_helper_get_active_plane_cnt(dc, context);
	p_state_change_support = new_clocks->p_state_change_support || (total_plane_count == 0);

	// invalidate the current P-State forced min in certain dc_mode_softmax situations
	if (dc->clk_mgr->dc_mode_softmax_enabled && safe_to_lower && !p_state_change_support) {
		if ((new_clocks->dramclk_khz <= dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000) !=
				(clk_mgr_base->clks.dramclk_khz <= dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000))
			update_pstate_unsupported_clk = true;
	}

	if (should_update_pstate_support(safe_to_lower, p_state_change_support, clk_mgr_base->clks.p_state_change_support) ||
			update_pstate_unsupported_clk) {
		clk_mgr_base->clks.p_state_change_support = p_state_change_support;

		/* to disable P-State switching, set UCLK min = max */
		if (!clk_mgr_base->clks.p_state_change_support) {
			if (dc->clk_mgr->dc_mode_softmax_enabled &&
				new_clocks->dramclk_khz <= dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000)
				dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					dc->clk_mgr->bw_params->dc_mode_softmax_memclk);
			else
				dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					clk_mgr_base->bw_params->clk_table.entries[clk_mgr_base->bw_params->clk_table.num_entries - 1].memclk_mhz);
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

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr_base->clks.dppclk_khz)) {
		if (clk_mgr_base->clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;

		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_PIXCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dppclk_khz));
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
			 * compare the current and new dppclk before calling this function.*/
			dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		}
	}

	if (update_dispclk && dmcu && dmcu->funcs->is_dmcu_initialized(dmcu))
		/*update dmcu for wait_loop count*/
		dmcu->funcs->set_psr_wait_loop(dmcu,
				clk_mgr_base->clks.dispclk_khz / 1000 / 7);
}


static void dcn3_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	unsigned int i;
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	WatermarksExternal_t *table = (WatermarksExternal_t *) clk_mgr->wm_range_table;

	if (!clk_mgr->smu_present)
		return;

	if (!table)
		// should log failure
		return;

	memset(table, 0, sizeof(*table));

	/* collect valid ranges, place in pmfw table */
	for (i = 0; i < WM_SET_COUNT; i++)
		if (clk_mgr->base.bw_params->wm_table.nv_entries[i].valid) {
			table->Watermarks.WatermarkRow[WM_DCEFCLK][i].MinClock = clk_mgr->base.bw_params->wm_table.nv_entries[i].pmfw_breakdown.min_dcfclk;
			table->Watermarks.WatermarkRow[WM_DCEFCLK][i].MaxClock = clk_mgr->base.bw_params->wm_table.nv_entries[i].pmfw_breakdown.max_dcfclk;
			table->Watermarks.WatermarkRow[WM_DCEFCLK][i].MinUclk = clk_mgr->base.bw_params->wm_table.nv_entries[i].pmfw_breakdown.min_uclk;
			table->Watermarks.WatermarkRow[WM_DCEFCLK][i].MaxUclk = clk_mgr->base.bw_params->wm_table.nv_entries[i].pmfw_breakdown.max_uclk;
			table->Watermarks.WatermarkRow[WM_DCEFCLK][i].WmSetting = i;
			table->Watermarks.WatermarkRow[WM_DCEFCLK][i].Flags = clk_mgr->base.bw_params->wm_table.nv_entries[i].pmfw_breakdown.wm_type;
		}

	dcn30_smu_set_dram_addr_high(clk_mgr, clk_mgr->wm_range_table_addr >> 32);
	dcn30_smu_set_dram_addr_low(clk_mgr, clk_mgr->wm_range_table_addr & 0xFFFFFFFF);
	dcn30_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

/* Set min memclk to minimum, either constrained by the current mode or DPM0 */
static void dcn3_set_hard_min_memclk(struct clk_mgr *clk_mgr_base, bool current_mode)
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
static void dcn3_set_hard_max_memclk(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_set_hard_max_by_freq(clk_mgr, PPCLK_UCLK,
			clk_mgr_base->bw_params->clk_table.entries[clk_mgr_base->bw_params->clk_table.num_entries - 1].memclk_mhz);
}

static void dcn3_set_max_memclk(struct clk_mgr *clk_mgr_base, unsigned int memclk_mhz)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_set_hard_max_by_freq(clk_mgr, PPCLK_UCLK, memclk_mhz);
}
static void dcn3_set_min_memclk(struct clk_mgr *clk_mgr_base, unsigned int memclk_mhz)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;
	dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK, memclk_mhz);
}

/* Get current memclk states, update bounding box */
static void dcn3_get_memclk_states_from_smu(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	unsigned int num_levels;

	if (!clk_mgr->smu_present)
		return;

	/* Refresh memclk states */
	dcn3_init_single_clock(clk_mgr, PPCLK_UCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].memclk_mhz,
			&num_levels);
	clk_mgr_base->bw_params->clk_table.num_entries = num_levels ? num_levels : 1;

	clk_mgr_base->bw_params->dc_mode_softmax_memclk = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_UCLK);

	/* Refresh bounding box */
	clk_mgr_base->ctx->dc->res_pool->funcs->update_bw_bounding_box(
			clk_mgr->base.ctx->dc, clk_mgr_base->bw_params);
}

static bool dcn3_is_smu_present(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	return clk_mgr->smu_present;
}

static bool dcn3_are_clock_states_equal(struct dc_clocks *a,
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

	return true;
}

static void dcn3_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_set_pme_workaround(clk_mgr);
}

/* Notify clk_mgr of a change in link rate, update phyclk frequency if necessary */
static void dcn30_notify_link_rate_change(struct clk_mgr *clk_mgr_base, struct dc_link *link)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	unsigned int i, max_phyclk_req = clk_mgr_base->bw_params->clk_table.entries[0].phyclk_mhz * 1000;

	if (!clk_mgr->smu_present)
		return;

	clk_mgr->cur_phyclk_req_table[link->link_index] = link->cur_link_settings.link_rate * LINK_RATE_REF_FREQ_IN_KHZ;

	for (i = 0; i < MAX_PIPES * 2; i++) {
		if (clk_mgr->cur_phyclk_req_table[i] > max_phyclk_req)
			max_phyclk_req = clk_mgr->cur_phyclk_req_table[i];
	}

	if (max_phyclk_req != clk_mgr_base->clks.phyclk_khz) {
		clk_mgr_base->clks.phyclk_khz = max_phyclk_req;
		dcn30_smu_set_hard_min_by_freq(clk_mgr, PPCLK_PHYCLK, khz_to_mhz_ceil(clk_mgr_base->clks.phyclk_khz));
	}
}

static struct clk_mgr_funcs dcn3_funcs = {
		.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
		.update_clocks = dcn3_update_clocks,
		.init_clocks = dcn3_init_clocks,
		.notify_wm_ranges = dcn3_notify_wm_ranges,
		.set_hard_min_memclk = dcn3_set_hard_min_memclk,
		.set_hard_max_memclk = dcn3_set_hard_max_memclk,
		.set_max_memclk = dcn3_set_max_memclk,
		.set_min_memclk = dcn3_set_min_memclk,
		.get_memclk_states_from_smu = dcn3_get_memclk_states_from_smu,
		.are_clock_states_equal = dcn3_are_clock_states_equal,
		.enable_pme_wa = dcn3_enable_pme_wa,
		.notify_link_rate_change = dcn30_notify_link_rate_change,
		.is_smu_present = dcn3_is_smu_present
};

static void dcn3_init_clocks_fpga(struct clk_mgr *clk_mgr)
{
	dcn2_init_clocks(clk_mgr);

/* TODO: Implement the functions and remove the ifndef guard */
}

struct clk_mgr_funcs dcn3_fpga_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = dcn2_update_clocks_fpga,
	.init_clocks = dcn3_init_clocks_fpga,
};

/*todo for dcn30 for clk register offset*/
void dcn3_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	clk_mgr->base.ctx = ctx;
	clk_mgr->base.funcs = &dcn3_funcs;
	clk_mgr->regs = &clk_mgr_regs;
	clk_mgr->clk_mgr_shift = &clk_mgr_shift;
	clk_mgr->clk_mgr_mask = &clk_mgr_mask;

	clk_mgr->dccg = dccg;
	clk_mgr->dfs_bypass_disp_clk = 0;

	clk_mgr->dprefclk_ss_percentage = 0;
	clk_mgr->dprefclk_ss_divider = 1000;
	clk_mgr->ss_on_dprefclk = false;
	clk_mgr->dfs_ref_freq_khz = 100000;

	clk_mgr->base.dprefclk_khz = 730000; // 700 MHz planned if VCO is 3.85 GHz, will be retrieved

	if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment)) {
		clk_mgr->base.funcs  = &dcn3_fpga_funcs;
		clk_mgr->base.dentist_vco_freq_khz = 3650000;

	} else {
		struct clk_state_registers_and_bypass s = { 0 };

		/* integer part is now VCO frequency in kHz */
		clk_mgr->base.dentist_vco_freq_khz = dcn30_get_vco_frequency_from_reg(clk_mgr);

		/* in case we don't get a value from the register, use default */
		if (clk_mgr->base.dentist_vco_freq_khz == 0)
			clk_mgr->base.dentist_vco_freq_khz = 3650000;
		/* Convert dprefclk units from MHz to KHz */
		/* Value already divided by 10, some resolution lost */

		/*TODO: uncomment assert once dcn3_dump_clk_registers is implemented */
		//ASSERT(s.dprefclk != 0);
		if (s.dprefclk != 0)
			clk_mgr->base.dprefclk_khz = s.dprefclk * 1000;
	}

	clk_mgr->dfs_bypass_enabled = false;

	clk_mgr->smu_present = false;

	dce_clock_read_ss_info(clk_mgr);

	clk_mgr->base.bw_params = kzalloc(sizeof(*clk_mgr->base.bw_params), GFP_KERNEL);

	/* need physical address of table to give to PMFW */
	clk_mgr->wm_range_table = dm_helpers_allocate_gpu_mem(clk_mgr->base.ctx,
			DC_MEM_ALLOC_TYPE_GART, sizeof(WatermarksExternal_t),
			&clk_mgr->wm_range_table_addr);
}

void dcn3_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr)
{
	kfree(clk_mgr->base.bw_params);

	if (clk_mgr->wm_range_table)
		dm_helpers_free_gpu_mem(clk_mgr->base.ctx, DC_MEM_ALLOC_TYPE_GART,
				clk_mgr->wm_range_table);
}
