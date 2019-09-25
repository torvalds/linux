/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

#include "core_types.h"
#include "clk_mgr_internal.h"

#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"
#include "dce100/dce_clk_mgr.h"
#include "dce110/dce110_clk_mgr.h"
#include "dce112_clk_mgr.h"
#include "dal_asic_id.h"

/* set register offset */
#define SR(reg_name)\
	.reg_name = mm ## reg_name

/* set register offset with instance */
#define SRI(reg_name, block, id)\
	.reg_name = mm ## block ## id ## _ ## reg_name

static const struct clk_mgr_registers disp_clk_regs = {
		CLK_COMMON_REG_LIST_DCE_BASE()
};

static const struct clk_mgr_shift disp_clk_shift = {
		CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(__SHIFT)
};

static const struct clk_mgr_mask disp_clk_mask = {
		CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(_MASK)
};

static const struct state_dependent_clocks dce112_max_clks_by_state[] = {
/*ClocksStateInvalid - should not be used*/
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/*ClocksStateUltraLow - currently by HW design team not supposed to be used*/
{ .display_clk_khz = 389189, .pixel_clk_khz = 346672 },
/*ClocksStateLow*/
{ .display_clk_khz = 459000, .pixel_clk_khz = 400000 },
/*ClocksStateNominal*/
{ .display_clk_khz = 667000, .pixel_clk_khz = 600000 },
/*ClocksStatePerformance*/
{ .display_clk_khz = 1132000, .pixel_clk_khz = 600000 } };


//TODO: remove use the two broken down functions
int dce112_set_clock(struct clk_mgr *clk_mgr_base, int requested_clk_khz)
{
	struct clk_mgr_internal *clk_mgr_dce = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = clk_mgr_base->ctx->dc_bios;
	struct dc *core_dc = clk_mgr_base->ctx->dc;
	struct dmcu *dmcu = core_dc->res_pool->dmcu;
	int actual_clock = requested_clk_khz;
	/* Prepare to program display clock*/
	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = max(requested_clk_khz,
				clk_mgr_dce->base.dentist_vco_freq_khz / 62);

	dce_clk_params.target_clock_frequency = requested_clk_khz;
	dce_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DISPLAY_CLOCK;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);
	actual_clock = dce_clk_params.target_clock_frequency;

	/*
	 * from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.
	 */
	if (requested_clk_khz == 0)
		clk_mgr_dce->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;

	/*Program DP ref Clock*/
	/*VBIOS will determine DPREFCLK frequency, so we don't set it*/
	dce_clk_params.target_clock_frequency = 0;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DPREFCLK;
	if (!ASICREV_IS_VEGA20_P(clk_mgr_base->ctx->asic_id.hw_internal_rev))
		dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK =
			(dce_clk_params.pll_id ==
					CLOCK_SOURCE_COMBO_DISPLAY_PLL0);
	else
		dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK = false;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);

	if (!IS_FPGA_MAXIMUS_DC(core_dc->ctx->dce_environment)) {
		if (dmcu && dmcu->funcs->is_dmcu_initialized(dmcu)) {
			if (clk_mgr_dce->dfs_bypass_disp_clk != actual_clock)
				dmcu->funcs->set_psr_wait_loop(dmcu,
						actual_clock / 1000 / 7);
		}
	}

	clk_mgr_dce->dfs_bypass_disp_clk = actual_clock;
	return actual_clock;
}

int dce112_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_clk_khz)
{
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = clk_mgr->base.ctx->dc_bios;
	struct dc *core_dc = clk_mgr->base.ctx->dc;
	struct dmcu *dmcu = core_dc->res_pool->dmcu;
	int actual_clock = requested_clk_khz;
	/* Prepare to program display clock*/
	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = max(requested_clk_khz,
				clk_mgr->base.dentist_vco_freq_khz / 62);

	dce_clk_params.target_clock_frequency = requested_clk_khz;
	dce_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DISPLAY_CLOCK;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);
	actual_clock = dce_clk_params.target_clock_frequency;

	/*
	 * from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.
	 */
	if (requested_clk_khz == 0)
		clk_mgr->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;


	if (!IS_FPGA_MAXIMUS_DC(core_dc->ctx->dce_environment)) {
		if (dmcu && dmcu->funcs->is_dmcu_initialized(dmcu)) {
			if (clk_mgr->dfs_bypass_disp_clk != actual_clock)
				dmcu->funcs->set_psr_wait_loop(dmcu,
						actual_clock / 1000 / 7);
		}
	}

	clk_mgr->dfs_bypass_disp_clk = actual_clock;
	return actual_clock;

}

int dce112_set_dprefclk(struct clk_mgr_internal *clk_mgr)
{
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = clk_mgr->base.ctx->dc_bios;

	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/*Program DP ref Clock*/
	/*VBIOS will determine DPREFCLK frequency, so we don't set it*/
	dce_clk_params.target_clock_frequency = 0;
	dce_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DPREFCLK;
	if (!ASICREV_IS_VEGA20_P(clk_mgr->base.ctx->asic_id.hw_internal_rev))
		dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK =
			(dce_clk_params.pll_id ==
					CLOCK_SOURCE_COMBO_DISPLAY_PLL0);
	else
		dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK = false;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);

	/* Returns the dp_refclk that was set */
	return dce_clk_params.target_clock_frequency;
}

static void dce112_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr_dce = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dm_pp_power_level_change_request level_change_req;
	int patched_disp_clk = context->bw_ctx.bw.dce.dispclk_khz;

	/*TODO: W/A for dal3 linux, investigate why this works */
	if (!clk_mgr_dce->dfs_bypass_active)
		patched_disp_clk = patched_disp_clk * 115 / 100;

	level_change_req.power_level = dce_get_required_clocks_state(clk_mgr_base, context);
	/* get max clock state from PPLIB */
	if ((level_change_req.power_level < clk_mgr_dce->cur_min_clks_state && safe_to_lower)
			|| level_change_req.power_level > clk_mgr_dce->cur_min_clks_state) {
		if (dm_pp_apply_power_level_change_request(clk_mgr_base->ctx, &level_change_req))
			clk_mgr_dce->cur_min_clks_state = level_change_req.power_level;
	}

	if (should_set_clock(safe_to_lower, patched_disp_clk, clk_mgr_base->clks.dispclk_khz)) {
		patched_disp_clk = dce112_set_clock(clk_mgr_base, patched_disp_clk);
		clk_mgr_base->clks.dispclk_khz = patched_disp_clk;
	}
	dce11_pplib_apply_display_requirements(clk_mgr_base->ctx->dc, context);
}

static struct clk_mgr_funcs dce112_funcs = {
	.get_dp_ref_clk_frequency = dce_get_dp_ref_freq_khz,
	.update_clocks = dce112_update_clocks
};

void dce112_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr)
{
	dce_clk_mgr_construct(ctx, clk_mgr);

	memcpy(clk_mgr->max_clks_by_state,
		dce112_max_clks_by_state,
		sizeof(dce112_max_clks_by_state));

	clk_mgr->regs = &disp_clk_regs;
	clk_mgr->clk_mgr_shift = &disp_clk_shift;
	clk_mgr->clk_mgr_mask = &disp_clk_mask;
	clk_mgr->base.funcs = &dce112_funcs;
}
