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

// For dce12_get_dp_ref_freq_khz
#include "dce100/dce_clk_mgr.h"

// For dcn20_update_clocks_update_dpp_dto
#include "dcn20/dcn20_clk_mgr.h"

#include "vg_clk_mgr.h"
#include "dcn301_smu.h"
#include "reg_helper.h"
#include "core_types.h"
#include "dm_helpers.h"

#include "atomfirmware.h"
#include "vangogh_ip_offset.h"
#include "clk/clk_11_5_0_offset.h"
#include "clk/clk_11_5_0_sh_mask.h"

/* Constants */

#define LPDDR_MEM_RETRAIN_LATENCY 4.977 /* Number obtained from LPDDR4 Training Counter Requirement doc */

/* Macros */

#define TO_CLK_MGR_VGH(clk_mgr)\
	container_of(clk_mgr, struct clk_mgr_vgh, base)

#define REG(reg_name) \
	(CLK_BASE.instance[0].segment[mm ## reg_name ## _BASE_IDX] + mm ## reg_name)

/* TODO: evaluate how to lower or disable all dcn clocks in screen off case */
static int vg_get_active_display_cnt_wa(
		struct dc *dc,
		struct dc_state *context)
{
	int i, display_count;
	bool tmds_present = false;

	display_count = 0;
	for (i = 0; i < context->stream_count; i++) {
		const struct dc_stream_state *stream = context->streams[i];

		if (stream->signal == SIGNAL_TYPE_HDMI_TYPE_A ||
				stream->signal == SIGNAL_TYPE_DVI_SINGLE_LINK ||
				stream->signal == SIGNAL_TYPE_DVI_DUAL_LINK)
			tmds_present = true;
	}

	for (i = 0; i < dc->link_count; i++) {
		const struct dc_link *link = dc->links[i];

		/* abusing the fact that the dig and phy are coupled to see if the phy is enabled */
		if (link->link_enc->funcs->is_dig_enabled &&
				link->link_enc->funcs->is_dig_enabled(link->link_enc))
			display_count++;
	}

	/* WA for hang on HDMI after display off back back on*/
	if (display_count == 0 && tmds_present)
		display_count = 1;

	return display_count;
}

void vg_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	struct dc *dc = clk_mgr_base->ctx->dc;
	int display_count;
	bool update_dppclk = false;
	bool update_dispclk = false;
	bool dpp_clock_lowered = false;

	if (dc->work_arounds.skip_clock_update)
		return;

	/*
	 * if it is safe to lower, but we are already in the lower state, we don't have to do anything
	 * also if safe to lower is false, we just go in the higher state
	 */
	if (safe_to_lower) {
		/* check that we're not already in lower */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_LOW_POWER) {

			display_count = vg_get_active_display_cnt_wa(dc, context);
			/* if we can go lower, go lower */
			if (display_count == 0 && !IS_DIAG_DC(dc->ctx->dce_environment)) {
				union display_idle_optimization_u idle_info = { 0 };

				idle_info.idle_info.df_request_disabled = 1;
				idle_info.idle_info.phy_ref_clk_off = 1;

				dcn301_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
				/* update power state */
				clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_LOW_POWER;
			}
		}
	} else {
		/* check that we're not already in D0 */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_MISSION_MODE) {
			union display_idle_optimization_u idle_info = { 0 };

			dcn301_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
			/* update power state */
			clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_MISSION_MODE;
		}
	}

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz) && !dc->debug.disable_min_fclk) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		dcn301_smu_set_hard_min_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_khz);
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz) && !dc->debug.disable_min_fclk) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		dcn301_smu_set_min_deep_sleep_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_deep_sleep_khz);
	}

	// workaround: Limit dppclk to 100Mhz to avoid lower eDP panel switch to plus 4K monitor underflow.
	if (!IS_DIAG_DC(dc->ctx->dce_environment)) {
		if (new_clocks->dppclk_khz < 100000)
			new_clocks->dppclk_khz = 100000;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr->base.clks.dppclk_khz)) {
		if (clk_mgr->base.clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;
		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;
		dcn301_smu_set_dispclk(clk_mgr, clk_mgr_base->clks.dispclk_khz);

		update_dispclk = true;
	}

	if (dpp_clock_lowered) {
		// increase per DPP DTO before lowering global dppclk
		dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		dcn301_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
	} else {
		// increase global DPPCLK before lowering per DPP DTO
		if (update_dppclk || update_dispclk)
			dcn301_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
		// always update dtos unless clock is lowered and not safe to lower
		if (new_clocks->dppclk_khz >= dc->current_state->bw_ctx.bw.dcn.clk.dppclk_khz)
			dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
	}
}


static int get_vco_frequency_from_reg(struct clk_mgr_internal *clk_mgr)
{
	/* get FbMult value */
	struct fixed31_32 pll_req;
	unsigned int fbmult_frac_val = 0;
	unsigned int fbmult_int_val = 0;


	/*
	 * Register value of fbmult is in 8.16 format, we are converting to 31.32
	 * to leverage the fix point operations available in driver
	 */

	REG_GET(CLK1_0_CLK1_CLK_PLL_REQ, FbMult_frac, &fbmult_frac_val); /* 16 bit fractional part*/
	REG_GET(CLK1_0_CLK1_CLK_PLL_REQ, FbMult_int, &fbmult_int_val); /* 8 bit integer part */

	pll_req = dc_fixpt_from_int(fbmult_int_val);

	/*
	 * since fractional part is only 16 bit in register definition but is 32 bit
	 * in our fix point definiton, need to shift left by 16 to obtain correct value
	 */
	pll_req.value |= fbmult_frac_val << 16;

	/* multiply by REFCLK period */
	pll_req = dc_fixpt_mul_int(pll_req, clk_mgr->dfs_ref_freq_khz);

	/* integer part is now VCO frequency in kHz */
	return dc_fixpt_floor(pll_req);
}

static void vg_dump_clk_registers_internal(struct dcn301_clk_internal *internal, struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	internal->CLK1_CLK3_CURRENT_CNT = REG_READ(CLK1_0_CLK1_CLK3_CURRENT_CNT);
	internal->CLK1_CLK3_BYPASS_CNTL = REG_READ(CLK1_0_CLK1_CLK3_BYPASS_CNTL);

	internal->CLK1_CLK3_DS_CNTL = REG_READ(CLK1_0_CLK1_CLK3_DS_CNTL);	//dcf deep sleep divider
	internal->CLK1_CLK3_ALLOW_DS = REG_READ(CLK1_0_CLK1_CLK3_ALLOW_DS);

	internal->CLK1_CLK1_CURRENT_CNT = REG_READ(CLK1_0_CLK1_CLK1_CURRENT_CNT);
	internal->CLK1_CLK1_BYPASS_CNTL = REG_READ(CLK1_0_CLK1_CLK1_BYPASS_CNTL);

	internal->CLK1_CLK2_CURRENT_CNT = REG_READ(CLK1_0_CLK1_CLK2_CURRENT_CNT);
	internal->CLK1_CLK2_BYPASS_CNTL = REG_READ(CLK1_0_CLK1_CLK2_BYPASS_CNTL);

	internal->CLK1_CLK0_CURRENT_CNT = REG_READ(CLK1_0_CLK1_CLK0_CURRENT_CNT);
	internal->CLK1_CLK0_BYPASS_CNTL = REG_READ(CLK1_0_CLK1_CLK0_BYPASS_CNTL);
}

/* This function collect raw clk register values */
static void vg_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info)
{
	struct dcn301_clk_internal internal = {0};
	char *bypass_clks[5] = {"0x0 DFS", "0x1 REFCLK", "0x2 ERROR", "0x3 400 FCH", "0x4 600 FCH"};
	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = log_info->bufSize;

	vg_dump_clk_registers_internal(&internal, clk_mgr_base);

	regs_and_bypass->dcfclk = internal.CLK1_CLK3_CURRENT_CNT / 10;
	regs_and_bypass->dcf_deep_sleep_divider = internal.CLK1_CLK3_DS_CNTL / 10;
	regs_and_bypass->dcf_deep_sleep_allow = internal.CLK1_CLK3_ALLOW_DS;
	regs_and_bypass->dprefclk = internal.CLK1_CLK2_CURRENT_CNT / 10;
	regs_and_bypass->dispclk = internal.CLK1_CLK0_CURRENT_CNT / 10;
	regs_and_bypass->dppclk = internal.CLK1_CLK1_CURRENT_CNT / 10;

	regs_and_bypass->dppclk_bypass = internal.CLK1_CLK1_BYPASS_CNTL & 0x0007;
	if (regs_and_bypass->dppclk_bypass < 0 || regs_and_bypass->dppclk_bypass > 4)
		regs_and_bypass->dppclk_bypass = 0;
	regs_and_bypass->dcfclk_bypass = internal.CLK1_CLK3_BYPASS_CNTL & 0x0007;
	if (regs_and_bypass->dcfclk_bypass < 0 || regs_and_bypass->dcfclk_bypass > 4)
		regs_and_bypass->dcfclk_bypass = 0;
	regs_and_bypass->dispclk_bypass = internal.CLK1_CLK0_BYPASS_CNTL & 0x0007;
	if (regs_and_bypass->dispclk_bypass < 0 || regs_and_bypass->dispclk_bypass > 4)
		regs_and_bypass->dispclk_bypass = 0;
	regs_and_bypass->dprefclk_bypass = internal.CLK1_CLK2_BYPASS_CNTL & 0x0007;
	if (regs_and_bypass->dprefclk_bypass < 0 || regs_and_bypass->dprefclk_bypass > 4)
		regs_and_bypass->dprefclk_bypass = 0;

	if (log_info->enabled) {
		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "clk_type,clk_value,deepsleep_cntl,deepsleep_allow,bypass\n");
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "dcfclk,%d,%d,%d,%s\n",
			regs_and_bypass->dcfclk,
			regs_and_bypass->dcf_deep_sleep_divider,
			regs_and_bypass->dcf_deep_sleep_allow,
			bypass_clks[(int) regs_and_bypass->dcfclk_bypass]);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "dprefclk,%d,N/A,N/A,%s\n",
			regs_and_bypass->dprefclk,
			bypass_clks[(int) regs_and_bypass->dprefclk_bypass]);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "dispclk,%d,N/A,N/A,%s\n",
			regs_and_bypass->dispclk,
			bypass_clks[(int) regs_and_bypass->dispclk_bypass]);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		//split
		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "SPLIT\n");
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		// REGISTER VALUES
		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "reg_name,value,clk_type\n");
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK3_CURRENT_CNT,%d,dcfclk\n",
				internal.CLK1_CLK3_CURRENT_CNT);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK3_DS_CNTL,%d,dcf_deep_sleep_divider\n",
					internal.CLK1_CLK3_DS_CNTL);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK3_ALLOW_DS,%d,dcf_deep_sleep_allow\n",
					internal.CLK1_CLK3_ALLOW_DS);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK2_CURRENT_CNT,%d,dprefclk\n",
					internal.CLK1_CLK2_CURRENT_CNT);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK0_CURRENT_CNT,%d,dispclk\n",
					internal.CLK1_CLK0_CURRENT_CNT);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK1_CURRENT_CNT,%d,dppclk\n",
					internal.CLK1_CLK1_CURRENT_CNT);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK3_BYPASS_CNTL,%d,dcfclk_bypass\n",
					internal.CLK1_CLK3_BYPASS_CNTL);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK2_BYPASS_CNTL,%d,dprefclk_bypass\n",
					internal.CLK1_CLK2_BYPASS_CNTL);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK0_BYPASS_CNTL,%d,dispclk_bypass\n",
					internal.CLK1_CLK0_BYPASS_CNTL);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;

		chars_printed = snprintf_count(log_info->pBuf, remaining_buffer, "CLK1_CLK1_BYPASS_CNTL,%d,dppclk_bypass\n",
					internal.CLK1_CLK1_BYPASS_CNTL);
		remaining_buffer -= chars_printed;
		*log_info->sum_chars_printed += chars_printed;
		log_info->pBuf += chars_printed;
	}
}

/* This function produce translated logical clk state values*/
void vg_get_clk_states(struct clk_mgr *clk_mgr_base, struct clk_states *s)
{

	struct clk_state_registers_and_bypass sb = { 0 };
	struct clk_log_info log_info = { 0 };

	vg_dump_clk_registers(&sb, clk_mgr_base, &log_info);

	s->dprefclk_khz = sb.dprefclk * 1000;
}

static void vg_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	dcn301_smu_enable_pme_wa(clk_mgr);
}

void vg_init_clocks(struct clk_mgr *clk_mgr)
{
	memset(&(clk_mgr->clks), 0, sizeof(struct dc_clocks));
	// Assumption is that boot state always supports pstate
	clk_mgr->clks.p_state_change_support = true;
	clk_mgr->clks.prev_p_state_change_support = true;
	clk_mgr->clks.pwr_state = DCN_PWR_STATE_UNKNOWN;
}

static void vg_build_watermark_ranges(struct clk_bw_params *bw_params, struct watermarks *table)
{
	int i, num_valid_sets;

	num_valid_sets = 0;

	for (i = 0; i < WM_SET_COUNT; i++) {
		/* skip empty entries, the smu array has no holes*/
		if (!bw_params->wm_table.entries[i].valid)
			continue;

		table->WatermarkRow[WM_DCFCLK][num_valid_sets].WmSetting = bw_params->wm_table.entries[i].wm_inst;
		table->WatermarkRow[WM_DCFCLK][num_valid_sets].WmType = bw_params->wm_table.entries[i].wm_type;
		/* We will not select WM based on fclk, so leave it as unconstrained */
		table->WatermarkRow[WM_DCFCLK][num_valid_sets].MinClock = 0;
		table->WatermarkRow[WM_DCFCLK][num_valid_sets].MaxClock = 0xFFFF;

		if (table->WatermarkRow[WM_DCFCLK][num_valid_sets].WmType == WM_TYPE_PSTATE_CHG) {
			if (i == 0)
				table->WatermarkRow[WM_DCFCLK][num_valid_sets].MinMclk = 0;
			else {
				/* add 1 to make it non-overlapping with next lvl */
				table->WatermarkRow[WM_DCFCLK][num_valid_sets].MinMclk =
						bw_params->clk_table.entries[i - 1].dcfclk_mhz + 1;
			}
			table->WatermarkRow[WM_DCFCLK][num_valid_sets].MaxMclk =
					bw_params->clk_table.entries[i].dcfclk_mhz;

		} else {
			/* unconstrained for memory retraining */
			table->WatermarkRow[WM_DCFCLK][num_valid_sets].MinClock = 0;
			table->WatermarkRow[WM_DCFCLK][num_valid_sets].MaxClock = 0xFFFF;

			/* Modify previous watermark range to cover up to max */
			table->WatermarkRow[WM_DCFCLK][num_valid_sets - 1].MaxClock = 0xFFFF;
		}
		num_valid_sets++;
	}

	ASSERT(num_valid_sets != 0); /* Must have at least one set of valid watermarks */

	/* modify the min and max to make sure we cover the whole range*/
	table->WatermarkRow[WM_DCFCLK][0].MinMclk = 0;
	table->WatermarkRow[WM_DCFCLK][0].MinClock = 0;
	table->WatermarkRow[WM_DCFCLK][num_valid_sets - 1].MaxMclk = 0xFFFF;
	table->WatermarkRow[WM_DCFCLK][num_valid_sets - 1].MaxClock = 0xFFFF;

	/* This is for writeback only, does not matter currently as no writeback support*/
	table->WatermarkRow[WM_SOCCLK][0].WmSetting = WM_A;
	table->WatermarkRow[WM_SOCCLK][0].MinClock = 0;
	table->WatermarkRow[WM_SOCCLK][0].MaxClock = 0xFFFF;
	table->WatermarkRow[WM_SOCCLK][0].MinMclk = 0;
	table->WatermarkRow[WM_SOCCLK][0].MaxMclk = 0xFFFF;
}


static void vg_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_mgr_vgh *clk_mgr_vgh = TO_CLK_MGR_VGH(clk_mgr);
	struct watermarks *table = clk_mgr_vgh->smu_wm_set.wm_set;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || clk_mgr_vgh->smu_wm_set.mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	vg_build_watermark_ranges(clk_mgr_base->bw_params, table);

	dcn301_smu_set_dram_addr_high(clk_mgr,
			clk_mgr_vgh->smu_wm_set.mc_address.high_part);
	dcn301_smu_set_dram_addr_low(clk_mgr,
			clk_mgr_vgh->smu_wm_set.mc_address.low_part);
	dcn301_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

static bool vg_are_clock_states_equal(struct dc_clocks *a,
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

	return true;
}


static struct clk_mgr_funcs vg_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = vg_update_clocks,
	.init_clocks = vg_init_clocks,
	.enable_pme_wa = vg_enable_pme_wa,
	.are_clock_states_equal = vg_are_clock_states_equal,
	.notify_wm_ranges = vg_notify_wm_ranges
};

static struct clk_bw_params vg_bw_params = {
	.vram_type = Ddr4MemType,
	.num_channels = 1,
	.clk_table = {
		.entries = {
			{
				.voltage = 0,
				.dcfclk_mhz = 400,
				.fclk_mhz = 400,
				.memclk_mhz = 800,
				.socclk_mhz = 0,
			},
			{
				.voltage = 0,
				.dcfclk_mhz = 483,
				.fclk_mhz = 800,
				.memclk_mhz = 1600,
				.socclk_mhz = 0,
			},
			{
				.voltage = 0,
				.dcfclk_mhz = 602,
				.fclk_mhz = 1067,
				.memclk_mhz = 1067,
				.socclk_mhz = 0,
			},
			{
				.voltage = 0,
				.dcfclk_mhz = 738,
				.fclk_mhz = 1333,
				.memclk_mhz = 1600,
				.socclk_mhz = 0,
			},
		},

		.num_entries = 4,
	},

};

static struct wm_table ddr4_wm_table = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 6.09,
			.sr_enter_plus_exit_time_us = 7.14,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 10.12,
			.sr_enter_plus_exit_time_us = 11.48,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 10.12,
			.sr_enter_plus_exit_time_us = 11.48,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 10.12,
			.sr_enter_plus_exit_time_us = 11.48,
			.valid = true,
		},
	}
};

static struct wm_table lpddr5_wm_table = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 7.95,
			.sr_enter_plus_exit_time_us = 9,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 9.82,
			.sr_enter_plus_exit_time_us = 11.196,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 9.89,
			.sr_enter_plus_exit_time_us = 11.24,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 9.748,
			.sr_enter_plus_exit_time_us = 11.102,
			.valid = true,
		},
	}
};


static unsigned int find_dcfclk_for_voltage(const struct vg_dpm_clocks *clock_table,
		unsigned int voltage)
{
	int i;

	for (i = 0; i < VG_NUM_SOC_VOLTAGE_LEVELS; i++) {
		if (clock_table->SocVoltage[i] == voltage)
			return clock_table->DcfClocks[i];
	}

	ASSERT(0);
	return 0;
}

static void vg_clk_mgr_helper_populate_bw_params(
		struct clk_mgr_internal *clk_mgr,
		struct integrated_info *bios_info,
		const struct vg_dpm_clocks *clock_table)
{
	int i, j;
	struct clk_bw_params *bw_params = clk_mgr->base.bw_params;

	j = -1;

	ASSERT(VG_NUM_FCLK_DPM_LEVELS <= MAX_NUM_DPM_LVL);

	/* Find lowest DPM, FCLK is filled in reverse order*/

	for (i = VG_NUM_FCLK_DPM_LEVELS - 1; i >= 0; i--) {
		if (clock_table->DfPstateTable[i].fclk != 0) {
			j = i;
			break;
		}
	}

	if (j == -1) {
		/* clock table is all 0s, just use our own hardcode */
		ASSERT(0);
		return;
	}

	bw_params->clk_table.num_entries = j + 1;

	for (i = 0; i < bw_params->clk_table.num_entries; i++, j--) {
		bw_params->clk_table.entries[i].fclk_mhz = clock_table->DfPstateTable[j].fclk;
		bw_params->clk_table.entries[i].memclk_mhz = clock_table->DfPstateTable[j].memclk;
		bw_params->clk_table.entries[i].voltage = clock_table->DfPstateTable[j].voltage;
		bw_params->clk_table.entries[i].dcfclk_mhz = find_dcfclk_for_voltage(clock_table, clock_table->DfPstateTable[j].voltage);
	}

	bw_params->vram_type = bios_info->memory_type;
	bw_params->num_channels = bios_info->ma_channel_number;

	for (i = 0; i < WM_SET_COUNT; i++) {
		bw_params->wm_table.entries[i].wm_inst = i;

		if (i >= bw_params->clk_table.num_entries) {
			bw_params->wm_table.entries[i].valid = false;
			continue;
		}

		bw_params->wm_table.entries[i].wm_type = WM_TYPE_PSTATE_CHG;
		bw_params->wm_table.entries[i].valid = true;
	}

	if (bw_params->vram_type == LpDdr4MemType) {
		/*
		 * WM set D will be re-purposed for memory retraining
		 */
		bw_params->wm_table.entries[WM_D].pstate_latency_us = LPDDR_MEM_RETRAIN_LATENCY;
		bw_params->wm_table.entries[WM_D].wm_inst = WM_D;
		bw_params->wm_table.entries[WM_D].wm_type = WM_TYPE_RETRAINING;
		bw_params->wm_table.entries[WM_D].valid = true;
	}

}

/* Temporary Place holder until we can get them from fuse */
static struct vg_dpm_clocks dummy_clocks = {
		.DcfClocks = { 201, 403, 403, 403, 403, 403, 403 },
		.SocClocks = { 400, 600, 600, 600, 600, 600, 600 },
		.SocVoltage = { 2800, 2860, 2860, 2860, 2860, 2860, 2860, 2860 },
		.DfPstateTable = {
				{ .fclk = 400,  .memclk = 400, .voltage = 2800 },
				{ .fclk = 400,  .memclk = 400, .voltage = 2800 },
				{ .fclk = 400,  .memclk = 400, .voltage = 2800 },
				{ .fclk = 400,  .memclk = 400, .voltage = 2800 }
		}
};

static struct watermarks dummy_wms = { 0 };

static void vg_get_dpm_table_from_smu(struct clk_mgr_internal *clk_mgr,
		struct smu_dpm_clks *smu_dpm_clks)
{
	struct vg_dpm_clocks *table = smu_dpm_clks->dpm_clks;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || smu_dpm_clks->mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn301_smu_set_dram_addr_high(clk_mgr,
			smu_dpm_clks->mc_address.high_part);
	dcn301_smu_set_dram_addr_low(clk_mgr,
			smu_dpm_clks->mc_address.low_part);
	dcn301_smu_transfer_dpm_table_smu_2_dram(clk_mgr);
}

void vg_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_vgh *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	struct smu_dpm_clks smu_dpm_clks = { 0 };

	clk_mgr->base.base.ctx = ctx;
	clk_mgr->base.base.funcs = &vg_funcs;

	clk_mgr->base.pp_smu = pp_smu;

	clk_mgr->base.dccg = dccg;
	clk_mgr->base.dfs_bypass_disp_clk = 0;

	clk_mgr->base.dprefclk_ss_percentage = 0;
	clk_mgr->base.dprefclk_ss_divider = 1000;
	clk_mgr->base.ss_on_dprefclk = false;
	clk_mgr->base.dfs_ref_freq_khz = 48000;

	clk_mgr->smu_wm_set.wm_set = (struct watermarks *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				sizeof(struct watermarks),
				&clk_mgr->smu_wm_set.mc_address.quad_part);

	if (clk_mgr->smu_wm_set.wm_set == 0) {
		clk_mgr->smu_wm_set.wm_set = &dummy_wms;
		clk_mgr->smu_wm_set.mc_address.quad_part = 0;
	}
	ASSERT(clk_mgr->smu_wm_set.wm_set);

	smu_dpm_clks.dpm_clks = (struct vg_dpm_clocks *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				sizeof(struct vg_dpm_clocks),
				&smu_dpm_clks.mc_address.quad_part);

	if (smu_dpm_clks.dpm_clks == NULL) {
		smu_dpm_clks.dpm_clks = &dummy_clocks;
		smu_dpm_clks.mc_address.quad_part = 0;
	}

	ASSERT(smu_dpm_clks.dpm_clks);

	if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment)) {
		vg_funcs.update_clocks = dcn2_update_clocks_fpga;
		clk_mgr->base.base.dentist_vco_freq_khz = 3600000;
	} else {
		struct clk_log_info log_info = {0};

		clk_mgr->base.smu_ver = dcn301_smu_get_smu_version(&clk_mgr->base);

		if (clk_mgr->base.smu_ver)
			clk_mgr->base.smu_present = true;

		/* TODO: Check we get what we expect during bringup */
		clk_mgr->base.base.dentist_vco_freq_khz = get_vco_frequency_from_reg(&clk_mgr->base);

		/* in case we don't get a value from the register, use default */
		if (clk_mgr->base.base.dentist_vco_freq_khz == 0)
			clk_mgr->base.base.dentist_vco_freq_khz = 3600000;

		if (ctx->dc_bios->integrated_info->memory_type == LpDdr5MemType) {
			vg_bw_params.wm_table = lpddr5_wm_table;
		} else {
			vg_bw_params.wm_table = ddr4_wm_table;
		}
		/* Saved clocks configured at boot for debug purposes */
		vg_dump_clk_registers(&clk_mgr->base.base.boot_snapshot, &clk_mgr->base.base, &log_info);
	}

	clk_mgr->base.base.dprefclk_khz = 600000;
	dce_clock_read_ss_info(&clk_mgr->base);

	clk_mgr->base.base.bw_params = &vg_bw_params;

	vg_get_dpm_table_from_smu(&clk_mgr->base, &smu_dpm_clks);
	if (ctx->dc_bios && ctx->dc_bios->integrated_info) {
		vg_clk_mgr_helper_populate_bw_params(
				&clk_mgr->base,
				ctx->dc_bios->integrated_info,
				smu_dpm_clks.dpm_clks);
	}

	if (smu_dpm_clks.dpm_clks && smu_dpm_clks.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr->base.base.ctx, DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				smu_dpm_clks.dpm_clks);
/*
	if (!IS_FPGA_MAXIMUS_DC(ctx->dce_environment) && clk_mgr->base.smu_ver) {
		 enable powerfeatures when displaycount goes to 0
		dcn301_smu_enable_phy_refclk_pwrdwn(clk_mgr, !debug->disable_48mhz_pwrdwn);
	}
*/
}

void vg_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr_int)
{
	struct clk_mgr_vgh *clk_mgr = TO_CLK_MGR_VGH(clk_mgr_int);

	if (clk_mgr->smu_wm_set.wm_set && clk_mgr->smu_wm_set.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr_int->base.ctx, DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				clk_mgr->smu_wm_set.wm_set);
}
