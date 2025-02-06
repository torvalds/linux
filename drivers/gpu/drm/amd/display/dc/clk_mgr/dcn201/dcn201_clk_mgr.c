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

#include "reg_helper.h"
#include "core_types.h"
#include "dccg.h"
#include "clk_mgr_internal.h"
#include "dcn201_clk_mgr.h"
#include "dcn20/dcn20_clk_mgr.h"
#include "dce100/dce_clk_mgr.h"
#include "dm_helpers.h"
#include "dm_services.h"

#include "cyan_skillfish_ip_offset.h"
#include "dcn/dcn_2_0_1_offset.h"
#include "dcn/dcn_2_0_1_sh_mask.h"
#include "clk/clk_11_0_1_offset.h"
#include "clk/clk_11_0_1_sh_mask.h"

#define REG(reg) \
	(clk_mgr->regs->reg)

#define BASE_INNER(seg) DMU_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

#define CLK_BASE_INNER(seg) \
	CLK_BASE__INST0_SEG ## seg

#undef FN
#define FN(reg_name, field_name) \
	clk_mgr->clk_mgr_shift->field_name, clk_mgr->clk_mgr_mask->field_name

#define CTX \
	clk_mgr->base.ctx

static const struct clk_mgr_registers clk_mgr_regs = {
		CLK_COMMON_REG_LIST_DCN_201()
};

static const struct clk_mgr_shift clk_mgr_shift = {
	CLK_COMMON_MASK_SH_LIST_DCN201_BASE(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask = {
	CLK_COMMON_MASK_SH_LIST_DCN201_BASE(_MASK)
};

static void dcn201_init_clocks(struct clk_mgr *clk_mgr)
{
	memset(&(clk_mgr->clks), 0, sizeof(struct dc_clocks));
	clk_mgr->clks.p_state_change_support = true;
	clk_mgr->clks.prev_p_state_change_support = true;
	clk_mgr->clks.max_supported_dppclk_khz = 1200000;
	clk_mgr->clks.max_supported_dispclk_khz = 1200000;
}

static void dcn201_update_clocks(struct clk_mgr *clk_mgr_base,
	struct dc_state *context,
	bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	struct dc *dc = clk_mgr_base->ctx->dc;
	bool update_dppclk = false;
	bool update_dispclk = false;
	bool dpp_clock_lowered = false;
	bool force_reset = false;
	bool p_state_change_support;
	int total_plane_count;

	if (dc->work_arounds.skip_clock_update)
		return;

	if (clk_mgr_base->clks.dispclk_khz == 0 ||
	    dc->debug.force_clock_mode & 0x1) {
		/* this is from resume or boot up, if forced_clock cfg option
		 * used, we bypass program dispclk and DPPCLK, but need set them
		 * for S3.
		 */

		force_reset = true;
		/* force_clock_mode 0x1:  force reset the clock even it is the
		 * same clock as long as it is in Passive level.
		 */

		dcn2_read_clocks_from_hw_dentist(clk_mgr_base);
	}

	if (should_set_clock(safe_to_lower, new_clocks->phyclk_khz, clk_mgr_base->clks.phyclk_khz))
		clk_mgr_base->clks.phyclk_khz = new_clocks->phyclk_khz;

	if (dc->debug.force_min_dcfclk_mhz > 0)
		new_clocks->dcfclk_khz = (new_clocks->dcfclk_khz > (dc->debug.force_min_dcfclk_mhz * 1000)) ?
		new_clocks->dcfclk_khz : (dc->debug.force_min_dcfclk_mhz * 1000);

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz))
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;

	if (should_set_clock(safe_to_lower,
		new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz))
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;

	if (should_set_clock(safe_to_lower, new_clocks->socclk_khz, clk_mgr_base->clks.socclk_khz))
		clk_mgr_base->clks.socclk_khz = new_clocks->socclk_khz;

	total_plane_count = clk_mgr_helper_get_active_plane_cnt(dc, context);
	p_state_change_support = new_clocks->p_state_change_support || (total_plane_count == 0);
	if (should_update_pstate_support(safe_to_lower, p_state_change_support, clk_mgr_base->clks.p_state_change_support)) {
		clk_mgr_base->clks.prev_p_state_change_support = clk_mgr_base->clks.p_state_change_support;
		clk_mgr_base->clks.p_state_change_support = p_state_change_support;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dramclk_khz, clk_mgr_base->clks.dramclk_khz))
		clk_mgr_base->clks.dramclk_khz = new_clocks->dramclk_khz;

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr->base.clks.dppclk_khz)) {
		if (clk_mgr->base.clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;
		clk_mgr->base.clks.dppclk_khz = new_clocks->dppclk_khz;

		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;

		update_dispclk = true;
	}

	if (dc->config.forced_clocks == false || (force_reset && safe_to_lower)) {
		if (dpp_clock_lowered) {
			// if clock is being lowered, increase DTO before lowering refclk
			dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
			dcn20_update_clocks_update_dentist(clk_mgr, context);
		} else {
			// if clock is being raised, increase refclk before lowering DTO
			if (update_dppclk || update_dispclk)
				dcn20_update_clocks_update_dentist(clk_mgr, context);
			// always update dtos unless clock is lowered and not safe to lower
			dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		}
	}
}

static struct clk_mgr_funcs dcn201_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = dcn201_update_clocks,
	.init_clocks = dcn201_init_clocks,
	.get_clock = dcn2_get_clock,
};

void dcn201_clk_mgr_construct(struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	struct dc_debug_options *debug = &ctx->dc->debug;
	struct dc_bios *bp = ctx->dc_bios;
	clk_mgr->base.ctx = ctx;
	clk_mgr->base.funcs = &dcn201_funcs;
	clk_mgr->regs = &clk_mgr_regs;
	clk_mgr->clk_mgr_shift = &clk_mgr_shift;
	clk_mgr->clk_mgr_mask = &clk_mgr_mask;

	clk_mgr->dccg = dccg;

	clk_mgr->dfs_bypass_disp_clk = 0;

	clk_mgr->dprefclk_ss_percentage = 0;
	clk_mgr->dprefclk_ss_divider = 1000;
	clk_mgr->ss_on_dprefclk = false;

	clk_mgr->base.dprefclk_khz = REG_READ(CLK4_CLK2_CURRENT_CNT);
	clk_mgr->base.dprefclk_khz *= 100;

	if (clk_mgr->base.dprefclk_khz == 0)
		clk_mgr->base.dprefclk_khz = 600000;

	REG_GET(CLK4_CLK_PLL_REQ, FbMult_int, &clk_mgr->base.dentist_vco_freq_khz);
	clk_mgr->base.dentist_vco_freq_khz *= 100000;

	if (clk_mgr->base.dentist_vco_freq_khz == 0)
		clk_mgr->base.dentist_vco_freq_khz = 3000000;

	if (!debug->disable_dfs_bypass && bp->integrated_info)
		if (bp->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE)
			clk_mgr->dfs_bypass_enabled = true;

	dce_clock_read_ss_info(clk_mgr);
}
