/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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


#include "dcn20/dcn20_clk_mgr.h"
#include "rn_clk_mgr.h"


#include "dce100/dce_clk_mgr.h"
#include "rn_clk_mgr_vbios_smu.h"
#include "reg_helper.h"
#include "core_types.h"
#include "dm_helpers.h"

#include "atomfirmware.h"
#include "clk/clk_10_0_2_offset.h"
#include "clk/clk_10_0_2_sh_mask.h"
#include "renoir_ip_offset.h"


/* Constants */

#define LPDDR_MEM_RETRAIN_LATENCY 4.977 /* Number obtained from LPDDR4 Training Counter Requirement doc */
#define SMU_VER_55_51_0 0x373300 /* SMU Version that is able to set DISPCLK below 100MHz */

/* Macros */

#define REG(reg_name) \
	(CLK_BASE.instance[0].segment[mm ## reg_name ## _BASE_IDX] + mm ## reg_name)


/* TODO: evaluate how to lower or disable all dcn clocks in screen off case */
static int rn_get_active_display_cnt_wa(struct dc *dc, struct dc_state *context)
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

static void rn_set_low_power_state(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	rn_vbios_smu_set_dcn_low_power_state(clk_mgr, DCN_PWR_STATE_LOW_POWER);
	/* update power state */
	clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_LOW_POWER;
}

static void rn_update_clocks_update_dpp_dto(struct clk_mgr_internal *clk_mgr,
		struct dc_state *context, int ref_dpp_clk, bool safe_to_lower)
{
	int i;

	clk_mgr->dccg->ref_dppclk = ref_dpp_clk;

	for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
		int dpp_inst, dppclk_khz, prev_dppclk_khz;

		/* Loop index may not match dpp->inst if some pipes disabled,
		 * so select correct inst from res_pool
		 */
		dpp_inst = clk_mgr->base.ctx->dc->res_pool->dpps[i]->inst;
		dppclk_khz = context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz;

		prev_dppclk_khz = clk_mgr->dccg->pipe_dppclk_khz[i];

		if (safe_to_lower || prev_dppclk_khz < dppclk_khz)
			clk_mgr->dccg->funcs->update_dpp_dto(
							clk_mgr->dccg, dpp_inst, dppclk_khz);
	}
}


static void rn_update_clocks(struct clk_mgr *clk_mgr_base,
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

	struct dmcu *dmcu = clk_mgr_base->ctx->dc->res_pool->dmcu;

	if (dc->work_arounds.skip_clock_update)
		return;

	/*
	 * if it is safe to lower, but we are already in the lower state, we don't have to do anything
	 * also if safe to lower is false, we just go in the higher state
	 */
	if (safe_to_lower && !dc->debug.disable_48mhz_pwrdwn) {
		/* check that we're not already in lower */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_LOW_POWER) {

			display_count = rn_get_active_display_cnt_wa(dc, context);

			/* if we can go lower, go lower */
			if (display_count == 0) {
				rn_vbios_smu_set_dcn_low_power_state(clk_mgr, DCN_PWR_STATE_LOW_POWER);
				/* update power state */
				clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_LOW_POWER;
			}
		}
	} else {
		/* check that we're not already in D0 */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_MISSION_MODE) {
			rn_vbios_smu_set_dcn_low_power_state(clk_mgr, DCN_PWR_STATE_MISSION_MODE);
			/* update power state */
			clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_MISSION_MODE;
		}
	}

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		rn_vbios_smu_set_hard_min_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_khz);
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		rn_vbios_smu_set_min_deep_sleep_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_deep_sleep_khz);
	}

	// workaround: Limit dppclk to 100Mhz to avoid lower eDP panel switch to plus 4K monitor underflow.
	// Do not adjust dppclk if dppclk is 0 to avoid unexpected result
	if (new_clocks->dppclk_khz < 100000 && new_clocks->dppclk_khz > 0)
		new_clocks->dppclk_khz = 100000;

	/*
	 * Temporally ignore thew 0 cases for disp and dpp clks.
	 * We may have a new feature that requires 0 clks in the future.
	 */
	if (new_clocks->dppclk_khz == 0 || new_clocks->dispclk_khz == 0) {
		new_clocks->dppclk_khz = clk_mgr_base->clks.dppclk_khz;
		new_clocks->dispclk_khz = clk_mgr_base->clks.dispclk_khz;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr_base->clks.dppclk_khz)) {
		if (clk_mgr_base->clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;
		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;
		clk_mgr_base->clks.actual_dispclk_khz = rn_vbios_smu_set_dispclk(clk_mgr, clk_mgr_base->clks.dispclk_khz);

		update_dispclk = true;
	}

	if (dpp_clock_lowered) {
		// increase per DPP DTO before lowering global dppclk with requested dppclk
		rn_update_clocks_update_dpp_dto(
				clk_mgr,
				context,
				clk_mgr_base->clks.dppclk_khz,
				safe_to_lower);

		clk_mgr_base->clks.actual_dppclk_khz =
				rn_vbios_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);

		//update dpp dto with actual dpp clk.
		rn_update_clocks_update_dpp_dto(
				clk_mgr,
				context,
				clk_mgr_base->clks.actual_dppclk_khz,
				safe_to_lower);

	} else {
		// increase global DPPCLK before lowering per DPP DTO
		if (update_dppclk || update_dispclk)
			clk_mgr_base->clks.actual_dppclk_khz =
					rn_vbios_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);

		// always update dtos unless clock is lowered and not safe to lower
		rn_update_clocks_update_dpp_dto(
				clk_mgr,
				context,
				clk_mgr_base->clks.actual_dppclk_khz,
				safe_to_lower);
	}

	if (update_dispclk &&
			dmcu && dmcu->funcs->is_dmcu_initialized(dmcu)) {
		/*update dmcu for wait_loop count*/
		dmcu->funcs->set_psr_wait_loop(dmcu,
			clk_mgr_base->clks.dispclk_khz / 1000 / 7);
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

	REG_GET(CLK1_CLK_PLL_REQ, FbMult_frac, &fbmult_frac_val); /* 16 bit fractional part*/
	REG_GET(CLK1_CLK_PLL_REQ, FbMult_int, &fbmult_int_val); /* 8 bit integer part */

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

static void rn_dump_clk_registers_internal(struct rn_clk_internal *internal, struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	internal->CLK1_CLK3_CURRENT_CNT = REG_READ(CLK1_CLK3_CURRENT_CNT);
	internal->CLK1_CLK3_BYPASS_CNTL = REG_READ(CLK1_CLK3_BYPASS_CNTL);

	internal->CLK1_CLK3_DS_CNTL = REG_READ(CLK1_CLK3_DS_CNTL);	//dcf deep sleep divider
	internal->CLK1_CLK3_ALLOW_DS = REG_READ(CLK1_CLK3_ALLOW_DS);

	internal->CLK1_CLK1_CURRENT_CNT = REG_READ(CLK1_CLK1_CURRENT_CNT);
	internal->CLK1_CLK1_BYPASS_CNTL = REG_READ(CLK1_CLK1_BYPASS_CNTL);

	internal->CLK1_CLK2_CURRENT_CNT = REG_READ(CLK1_CLK2_CURRENT_CNT);
	internal->CLK1_CLK2_BYPASS_CNTL = REG_READ(CLK1_CLK2_BYPASS_CNTL);

	internal->CLK1_CLK0_CURRENT_CNT = REG_READ(CLK1_CLK0_CURRENT_CNT);
	internal->CLK1_CLK0_BYPASS_CNTL = REG_READ(CLK1_CLK0_BYPASS_CNTL);
}

/* This function collect raw clk register values */
static void rn_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info)
{
	struct rn_clk_internal internal = {0};
	char *bypass_clks[5] = {"0x0 DFS", "0x1 REFCLK", "0x2 ERROR", "0x3 400 FCH", "0x4 600 FCH"};
	unsigned int chars_printed = 0;
	unsigned int remaining_buffer = log_info->bufSize;

	rn_dump_clk_registers_internal(&internal, clk_mgr_base);

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

static void rn_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	rn_vbios_smu_enable_pme_wa(clk_mgr);
}

static void rn_init_clocks(struct clk_mgr *clk_mgr)
{
	memset(&(clk_mgr->clks), 0, sizeof(struct dc_clocks));
	// Assumption is that boot state always supports pstate
	clk_mgr->clks.p_state_change_support = true;
	clk_mgr->clks.prev_p_state_change_support = true;
	clk_mgr->clks.pwr_state = DCN_PWR_STATE_UNKNOWN;
}

static void build_watermark_ranges(struct clk_bw_params *bw_params, struct pp_smu_wm_range_sets *ranges)
{
	int i, num_valid_sets;

	num_valid_sets = 0;

	for (i = 0; i < WM_SET_COUNT; i++) {
		/* skip empty entries, the smu array has no holes*/
		if (!bw_params->wm_table.entries[i].valid)
			continue;

		ranges->reader_wm_sets[num_valid_sets].wm_inst = bw_params->wm_table.entries[i].wm_inst;
		ranges->reader_wm_sets[num_valid_sets].wm_type = bw_params->wm_table.entries[i].wm_type;
		/* We will not select WM based on fclk, so leave it as unconstrained */
		ranges->reader_wm_sets[num_valid_sets].min_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
		ranges->reader_wm_sets[num_valid_sets].max_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;
		/* dcfclk wil be used to select WM*/

		if (ranges->reader_wm_sets[num_valid_sets].wm_type == WM_TYPE_PSTATE_CHG) {
			if (i == 0)
				ranges->reader_wm_sets[num_valid_sets].min_drain_clk_mhz = 0;
			else {
				/* add 1 to make it non-overlapping with next lvl */
				ranges->reader_wm_sets[num_valid_sets].min_drain_clk_mhz = bw_params->clk_table.entries[i - 1].dcfclk_mhz + 1;
			}
			ranges->reader_wm_sets[num_valid_sets].max_drain_clk_mhz = bw_params->clk_table.entries[i].dcfclk_mhz;

		} else {
			/* unconstrained for memory retraining */
			ranges->reader_wm_sets[num_valid_sets].min_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
			ranges->reader_wm_sets[num_valid_sets].max_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;

			/* Modify previous watermark range to cover up to max */
			ranges->reader_wm_sets[num_valid_sets - 1].max_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;
		}
		num_valid_sets++;
	}

	ASSERT(num_valid_sets != 0); /* Must have at least one set of valid watermarks */
	ranges->num_reader_wm_sets = num_valid_sets;

	/* modify the min and max to make sure we cover the whole range*/
	ranges->reader_wm_sets[0].min_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
	ranges->reader_wm_sets[0].min_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
	ranges->reader_wm_sets[ranges->num_reader_wm_sets - 1].max_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;
	ranges->reader_wm_sets[ranges->num_reader_wm_sets - 1].max_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;

	/* This is for writeback only, does not matter currently as no writeback support*/
	ranges->num_writer_wm_sets = 1;
	ranges->writer_wm_sets[0].wm_inst = WM_A;
	ranges->writer_wm_sets[0].min_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
	ranges->writer_wm_sets[0].max_fill_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;
	ranges->writer_wm_sets[0].min_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MIN;
	ranges->writer_wm_sets[0].max_drain_clk_mhz = PP_SMU_WM_SET_RANGE_CLK_UNCONSTRAINED_MAX;

}

static void rn_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	struct dc_debug_options *debug = &clk_mgr_base->ctx->dc->debug;
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct pp_smu_funcs *pp_smu = clk_mgr->pp_smu;

	if (!debug->disable_pplib_wm_range) {
		build_watermark_ranges(clk_mgr_base->bw_params, &clk_mgr_base->ranges);

		/* Notify PP Lib/SMU which Watermarks to use for which clock ranges */
		if (pp_smu && pp_smu->rn_funcs.set_wm_ranges)
			pp_smu->rn_funcs.set_wm_ranges(&pp_smu->rn_funcs.pp_smu, &clk_mgr_base->ranges);
	}

}

static bool rn_are_clock_states_equal(struct dc_clocks *a,
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


/* Notify clk_mgr of a change in link rate, update phyclk frequency if necessary */
static void rn_notify_link_rate_change(struct clk_mgr *clk_mgr_base, struct dc_link *link)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	unsigned int i, max_phyclk_req = 0;

	clk_mgr->cur_phyclk_req_table[link->link_index] = link->cur_link_settings.link_rate * LINK_RATE_REF_FREQ_IN_KHZ;

	for (i = 0; i < MAX_PIPES * 2; i++) {
		if (clk_mgr->cur_phyclk_req_table[i] > max_phyclk_req)
			max_phyclk_req = clk_mgr->cur_phyclk_req_table[i];
	}

	if (max_phyclk_req != clk_mgr_base->clks.phyclk_khz) {
		clk_mgr_base->clks.phyclk_khz = max_phyclk_req;
		rn_vbios_smu_set_phyclk(clk_mgr, clk_mgr_base->clks.phyclk_khz);
	}
}

static struct clk_mgr_funcs dcn21_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = rn_update_clocks,
	.init_clocks = rn_init_clocks,
	.enable_pme_wa = rn_enable_pme_wa,
	.are_clock_states_equal = rn_are_clock_states_equal,
	.set_low_power_state = rn_set_low_power_state,
	.notify_wm_ranges = rn_notify_wm_ranges,
	.notify_link_rate_change = rn_notify_link_rate_change,
};

static struct clk_bw_params rn_bw_params = {
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

static struct wm_table ddr4_wm_table_gs = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 7.09,
			.sr_enter_plus_exit_time_us = 8.14,
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

static struct wm_table lpddr4_wm_table_gs = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 5.32,
			.sr_enter_plus_exit_time_us = 6.38,
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

static struct wm_table lpddr4_wm_table_with_disabled_ppt = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 8.32,
			.sr_enter_plus_exit_time_us = 9.38,
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

static struct wm_table ddr4_wm_table_rn = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 11.90,
			.sr_enter_plus_exit_time_us = 12.80,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 13.18,
			.sr_enter_plus_exit_time_us = 14.30,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 13.18,
			.sr_enter_plus_exit_time_us = 14.30,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 13.18,
			.sr_enter_plus_exit_time_us = 14.30,
			.valid = true,
		},
	}
};

static struct wm_table ddr4_1R_wm_table_rn = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 13.90,
			.sr_enter_plus_exit_time_us = 14.80,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 13.90,
			.sr_enter_plus_exit_time_us = 14.80,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 13.90,
			.sr_enter_plus_exit_time_us = 14.80,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 13.90,
			.sr_enter_plus_exit_time_us = 14.80,
			.valid = true,
		},
	}
};

static struct wm_table lpddr4_wm_table_rn = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 7.32,
			.sr_enter_plus_exit_time_us = 8.38,
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

static unsigned int find_socclk_for_voltage(struct dpm_clocks *clock_table, unsigned int voltage)
{
	int i;

	for (i = 0; i < PP_SMU_NUM_SOCCLK_DPM_LEVELS; i++) {
		if (clock_table->SocClocks[i].Vol == voltage)
			return clock_table->SocClocks[i].Freq;
	}

	ASSERT(0);
	return 0;
}

static unsigned int find_dcfclk_for_voltage(struct dpm_clocks *clock_table, unsigned int voltage)
{
	int i;

	for (i = 0; i < PP_SMU_NUM_DCFCLK_DPM_LEVELS; i++) {
		if (clock_table->DcfClocks[i].Vol == voltage)
			return clock_table->DcfClocks[i].Freq;
	}

	ASSERT(0);
	return 0;
}

static void rn_clk_mgr_helper_populate_bw_params(struct clk_bw_params *bw_params, struct dpm_clocks *clock_table, struct integrated_info *bios_info)
{
	int i, j = 0;

	j = -1;

	ASSERT(PP_SMU_NUM_FCLK_DPM_LEVELS <= MAX_NUM_DPM_LVL);

	/* Find lowest DPM, FCLK is filled in reverse order*/

	for (i = PP_SMU_NUM_FCLK_DPM_LEVELS - 1; i >= 0; i--) {
		if (clock_table->FClocks[i].Freq != 0 && clock_table->FClocks[i].Vol != 0) {
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
		bw_params->clk_table.entries[i].fclk_mhz = clock_table->FClocks[j].Freq;
		bw_params->clk_table.entries[i].memclk_mhz = clock_table->MemClocks[j].Freq;
		bw_params->clk_table.entries[i].voltage = clock_table->FClocks[j].Vol;
		bw_params->clk_table.entries[i].dcfclk_mhz = find_dcfclk_for_voltage(clock_table, clock_table->FClocks[j].Vol);
		bw_params->clk_table.entries[i].socclk_mhz = find_socclk_for_voltage(clock_table,
									bw_params->clk_table.entries[i].voltage);
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

void rn_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	struct dc_debug_options *debug = &ctx->dc->debug;
	struct dpm_clocks clock_table = { 0 };
	enum pp_smu_status status = 0;
	int is_green_sardine = 0;

#if defined(CONFIG_DRM_AMD_DC_DCN)
	is_green_sardine = ASICREV_IS_GREEN_SARDINE(ctx->asic_id.hw_internal_rev);
#endif

	clk_mgr->base.ctx = ctx;
	clk_mgr->base.funcs = &dcn21_funcs;

	clk_mgr->pp_smu = pp_smu;

	clk_mgr->dccg = dccg;
	clk_mgr->dfs_bypass_disp_clk = 0;

	clk_mgr->dprefclk_ss_percentage = 0;
	clk_mgr->dprefclk_ss_divider = 1000;
	clk_mgr->ss_on_dprefclk = false;
	clk_mgr->dfs_ref_freq_khz = 48000;

	clk_mgr->smu_ver = rn_vbios_smu_get_smu_version(clk_mgr);

	if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment)) {
		dcn21_funcs.update_clocks = dcn2_update_clocks_fpga;
		clk_mgr->base.dentist_vco_freq_khz = 3600000;
	} else {
		struct clk_log_info log_info = {0};

		clk_mgr->periodic_retraining_disabled = rn_vbios_smu_is_periodic_retraining_disabled(clk_mgr);

		/* SMU Version 55.51.0 and up no longer have an issue
		 * that needs to limit minimum dispclk */
		if (clk_mgr->smu_ver >= SMU_VER_55_51_0)
			debug->min_disp_clk_khz = 0;

		/* TODO: Check we get what we expect during bringup */
		clk_mgr->base.dentist_vco_freq_khz = get_vco_frequency_from_reg(clk_mgr);

		/* in case we don't get a value from the register, use default */
		if (clk_mgr->base.dentist_vco_freq_khz == 0)
			clk_mgr->base.dentist_vco_freq_khz = 3600000;

		if (ctx->dc_bios->integrated_info->memory_type == LpDdr4MemType) {
			if (clk_mgr->periodic_retraining_disabled) {
				rn_bw_params.wm_table = lpddr4_wm_table_with_disabled_ppt;
			} else {
				if (is_green_sardine)
					rn_bw_params.wm_table = lpddr4_wm_table_gs;
				else
					rn_bw_params.wm_table = lpddr4_wm_table_rn;
			}
		} else {
			if (is_green_sardine)
				rn_bw_params.wm_table = ddr4_wm_table_gs;
			else {
				if (ctx->dc->config.is_single_rank_dimm)
					rn_bw_params.wm_table = ddr4_1R_wm_table_rn;
				else
					rn_bw_params.wm_table = ddr4_wm_table_rn;
			}
		}
		/* Saved clocks configured at boot for debug purposes */
		rn_dump_clk_registers(&clk_mgr->base.boot_snapshot, &clk_mgr->base, &log_info);
	}

	clk_mgr->base.dprefclk_khz = 600000;
	dce_clock_read_ss_info(clk_mgr);


	clk_mgr->base.bw_params = &rn_bw_params;

	if (pp_smu && pp_smu->rn_funcs.get_dpm_clock_table) {
		status = pp_smu->rn_funcs.get_dpm_clock_table(&pp_smu->rn_funcs.pp_smu, &clock_table);

		if (status == PP_SMU_RESULT_OK &&
		    ctx->dc_bios && ctx->dc_bios->integrated_info) {
			rn_clk_mgr_helper_populate_bw_params (clk_mgr->base.bw_params, &clock_table, ctx->dc_bios->integrated_info);
			/* treat memory config as single channel if memory is asymmetrics. */
			if (ctx->dc->config.is_asymmetric_memory)
				clk_mgr->base.bw_params->num_channels = 1;
		}
	}

	if (!IS_FPGA_MAXIMUS_DC(ctx->dce_environment) && clk_mgr->smu_ver >= 0x00371500) {
		/* enable powerfeatures when displaycount goes to 0 */
		rn_vbios_smu_enable_48mhz_tmdp_refclk_pwrdwn(clk_mgr, !debug->disable_48mhz_pwrdwn);
	}
}

