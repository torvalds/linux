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

#include <linux/slab.h>

#include "reg_helper.h"
#include "core_types.h"
#include "clk_mgr_internal.h"
#include "rv1_clk_mgr.h"
#include "dce100/dce_clk_mgr.h"
#include "dce112/dce112_clk_mgr.h"
#include "rv1_clk_mgr_vbios_smu.h"
#include "rv1_clk_mgr_clk.h"

void rv1_init_clocks(struct clk_mgr *clk_mgr)
{
	memset(&(clk_mgr->clks), 0, sizeof(struct dc_clocks));
}

static int rv1_determine_dppclk_threshold(struct clk_mgr_internal *clk_mgr, struct dc_clocks *new_clocks)
{
	bool request_dpp_div = new_clocks->dispclk_khz > new_clocks->dppclk_khz;
	bool dispclk_increase = new_clocks->dispclk_khz > clk_mgr->base.clks.dispclk_khz;
	int disp_clk_threshold = new_clocks->max_supported_dppclk_khz;
	bool cur_dpp_div = clk_mgr->base.clks.dispclk_khz > clk_mgr->base.clks.dppclk_khz;

	/* increase clock, looking for div is 0 for current, request div is 1*/
	if (dispclk_increase) {
		/* already divided by 2, no need to reach target clk with 2 steps*/
		if (cur_dpp_div)
			return new_clocks->dispclk_khz;

		/* request disp clk is lower than maximum supported dpp clk,
		 * no need to reach target clk with two steps.
		 */
		if (new_clocks->dispclk_khz <= disp_clk_threshold)
			return new_clocks->dispclk_khz;

		/* target dpp clk not request divided by 2, still within threshold */
		if (!request_dpp_div)
			return new_clocks->dispclk_khz;

	} else {
		/* decrease clock, looking for current dppclk divided by 2,
		 * request dppclk not divided by 2.
		 */

		/* current dpp clk not divided by 2, no need to ramp*/
		if (!cur_dpp_div)
			return new_clocks->dispclk_khz;

		/* current disp clk is lower than current maximum dpp clk,
		 * no need to ramp
		 */
		if (clk_mgr->base.clks.dispclk_khz <= disp_clk_threshold)
			return new_clocks->dispclk_khz;

		/* request dpp clk need to be divided by 2 */
		if (request_dpp_div)
			return new_clocks->dispclk_khz;
	}

	return disp_clk_threshold;
}

static void ramp_up_dispclk_with_dpp(
		struct clk_mgr_internal *clk_mgr,
		struct dc *dc,
		struct dc_clocks *new_clocks,
		bool safe_to_lower)
{
	int i;
	int dispclk_to_dpp_threshold = rv1_determine_dppclk_threshold(clk_mgr, new_clocks);
	bool request_dpp_div = new_clocks->dispclk_khz > new_clocks->dppclk_khz;

	/* this function is to change dispclk, dppclk and dprefclk according to
	 * bandwidth requirement. Its call stack is rv1_update_clocks -->
	 * update_clocks --> dcn10_prepare_bandwidth / dcn10_optimize_bandwidth
	 * --> prepare_bandwidth / optimize_bandwidth. before change dcn hw,
	 * prepare_bandwidth will be called first to allow enough clock,
	 * watermark for change, after end of dcn hw change, optimize_bandwidth
	 * is executed to lower clock to save power for new dcn hw settings.
	 *
	 * below is sequence of commit_planes_for_stream:
	 *
	 * step 1: prepare_bandwidth - raise clock to have enough bandwidth
	 * step 2: lock_doublebuffer_enable
	 * step 3: pipe_control_lock(true) - make dchubp register change will
	 * not take effect right way
	 * step 4: apply_ctx_for_surface - program dchubp
	 * step 5: pipe_control_lock(false) - dchubp register change take effect
	 * step 6: optimize_bandwidth --> dc_post_update_surfaces_to_stream
	 * for full_date, optimize clock to save power
	 *
	 * at end of step 1, dcn clocks (dprefclk, dispclk, dppclk) may be
	 * changed for new dchubp configuration. but real dcn hub dchubps are
	 * still running with old configuration until end of step 5. this need
	 * clocks settings at step 1 should not less than that before step 1.
	 * this is checked by two conditions: 1. if (should_set_clock(safe_to_lower
	 * , new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz) ||
	 * new_clocks->dispclk_khz == clk_mgr_base->clks.dispclk_khz)
	 * 2. request_dpp_div = new_clocks->dispclk_khz > new_clocks->dppclk_khz
	 *
	 * the second condition is based on new dchubp configuration. dppclk
	 * for new dchubp may be different from dppclk before step 1.
	 * for example, before step 1, dchubps are as below:
	 * pipe 0: recout=(0,40,1920,980) viewport=(0,0,1920,979)
	 * pipe 1: recout=(0,0,1920,1080) viewport=(0,0,1920,1080)
	 * for dppclk for pipe0 need dppclk = dispclk
	 *
	 * new dchubp pipe split configuration:
	 * pipe 0: recout=(0,0,960,1080) viewport=(0,0,960,1080)
	 * pipe 1: recout=(960,0,960,1080) viewport=(960,0,960,1080)
	 * dppclk only needs dppclk = dispclk /2.
	 *
	 * dispclk, dppclk are not lock by otg master lock. they take effect
	 * after step 1. during this transition, dispclk are the same, but
	 * dppclk is changed to half of previous clock for old dchubp
	 * configuration between step 1 and step 6. This may cause p-state
	 * warning intermittently.
	 *
	 * for new_clocks->dispclk_khz == clk_mgr_base->clks.dispclk_khz, we
	 * need make sure dppclk are not changed to less between step 1 and 6.
	 * for new_clocks->dispclk_khz > clk_mgr_base->clks.dispclk_khz,
	 * new display clock is raised, but we do not know ratio of
	 * new_clocks->dispclk_khz and clk_mgr_base->clks.dispclk_khz,
	 * new_clocks->dispclk_khz /2 does not guarantee equal or higher than
	 * old dppclk. we could ignore power saving different between
	 * dppclk = displck and dppclk = dispclk / 2 between step 1 and step 6.
	 * as long as safe_to_lower = false, set dpclk = dispclk to simplify
	 * condition check.
	 * todo: review this change for other asic.
	 **/
	if (!safe_to_lower)
		request_dpp_div = false;

	/* set disp clk to dpp clk threshold */

	clk_mgr->funcs->set_dispclk(clk_mgr, dispclk_to_dpp_threshold);
	clk_mgr->funcs->set_dprefclk(clk_mgr);


	/* update request dpp clk division option */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];

		if (!pipe_ctx->plane_state)
			continue;

		pipe_ctx->plane_res.dpp->funcs->dpp_dppclk_control(
				pipe_ctx->plane_res.dpp,
				request_dpp_div,
				true);
	}

	/* If target clk not same as dppclk threshold, set to target clock */
	if (dispclk_to_dpp_threshold != new_clocks->dispclk_khz) {
		clk_mgr->funcs->set_dispclk(clk_mgr, new_clocks->dispclk_khz);
		clk_mgr->funcs->set_dprefclk(clk_mgr);
	}


	clk_mgr->base.clks.dispclk_khz = new_clocks->dispclk_khz;
	clk_mgr->base.clks.dppclk_khz = new_clocks->dppclk_khz;
	clk_mgr->base.clks.max_supported_dppclk_khz = new_clocks->max_supported_dppclk_khz;
}

static void rv1_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc *dc = clk_mgr_base->ctx->dc;
	struct dc_debug_options *debug = &dc->debug;
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	struct pp_smu_funcs_rv *pp_smu = NULL;
	bool send_request_to_increase = false;
	bool send_request_to_lower = false;
	int display_count;

	bool enter_display_off = false;

	ASSERT(clk_mgr->pp_smu);

	if (dc->work_arounds.skip_clock_update)
		return;

	pp_smu = &clk_mgr->pp_smu->rv_funcs;

	display_count = clk_mgr_helper_get_active_display_cnt(dc, context);

	if (display_count == 0)
		enter_display_off = true;

	if (enter_display_off == safe_to_lower) {
		/*
		 * Notify SMU active displays
		 * if function pointer not set up, this message is
		 * sent as part of pplib_apply_display_requirements.
		 */
		if (pp_smu->set_display_count)
			pp_smu->set_display_count(&pp_smu->pp_smu, display_count);
	}

	if (new_clocks->dispclk_khz > clk_mgr_base->clks.dispclk_khz
			|| new_clocks->phyclk_khz > clk_mgr_base->clks.phyclk_khz
			|| new_clocks->fclk_khz > clk_mgr_base->clks.fclk_khz
			|| new_clocks->dcfclk_khz > clk_mgr_base->clks.dcfclk_khz)
		send_request_to_increase = true;

	if (should_set_clock(safe_to_lower, new_clocks->phyclk_khz, clk_mgr_base->clks.phyclk_khz)) {
		clk_mgr_base->clks.phyclk_khz = new_clocks->phyclk_khz;
		send_request_to_lower = true;
	}

	// F Clock
	if (debug->force_fclk_khz != 0)
		new_clocks->fclk_khz = debug->force_fclk_khz;

	if (should_set_clock(safe_to_lower, new_clocks->fclk_khz, clk_mgr_base->clks.fclk_khz)) {
		clk_mgr_base->clks.fclk_khz = new_clocks->fclk_khz;
		send_request_to_lower = true;
	}

	//DCF Clock
	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		send_request_to_lower = true;
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		send_request_to_lower = true;
	}

	/* make sure dcf clk is before dpp clk to
	 * make sure we have enough voltage to run dpp clk
	 */
	if (send_request_to_increase) {
		/*use dcfclk to request voltage*/
		if (pp_smu->set_hard_min_fclk_by_freq &&
				pp_smu->set_hard_min_dcfclk_by_freq &&
				pp_smu->set_min_deep_sleep_dcfclk) {
			pp_smu->set_hard_min_fclk_by_freq(&pp_smu->pp_smu, new_clocks->fclk_khz / 1000);
			pp_smu->set_hard_min_dcfclk_by_freq(&pp_smu->pp_smu, new_clocks->dcfclk_khz / 1000);
			pp_smu->set_min_deep_sleep_dcfclk(&pp_smu->pp_smu, (new_clocks->dcfclk_deep_sleep_khz + 999) / 1000);
		}
	}

	/* dcn1 dppclk is tied to dispclk */
	/* program dispclk on = as a w/a for sleep resume clock ramping issues */
	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)
			|| new_clocks->dispclk_khz == clk_mgr_base->clks.dispclk_khz) {
		ramp_up_dispclk_with_dpp(clk_mgr, dc, new_clocks, safe_to_lower);
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;
		send_request_to_lower = true;
	}

	if (!send_request_to_increase && send_request_to_lower) {
		/*use dcfclk to request voltage*/
		if (pp_smu->set_hard_min_fclk_by_freq &&
				pp_smu->set_hard_min_dcfclk_by_freq &&
				pp_smu->set_min_deep_sleep_dcfclk) {
			pp_smu->set_hard_min_fclk_by_freq(&pp_smu->pp_smu, new_clocks->fclk_khz / 1000);
			pp_smu->set_hard_min_dcfclk_by_freq(&pp_smu->pp_smu, new_clocks->dcfclk_khz / 1000);
			pp_smu->set_min_deep_sleep_dcfclk(&pp_smu->pp_smu, (new_clocks->dcfclk_deep_sleep_khz + 999) / 1000);
		}
	}
}

static void rv1_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct pp_smu_funcs_rv *pp_smu = NULL;

	if (clk_mgr->pp_smu) {
		pp_smu = &clk_mgr->pp_smu->rv_funcs;

		if (pp_smu->set_pme_wa_enable)
			pp_smu->set_pme_wa_enable(&pp_smu->pp_smu);
	}
}

static struct clk_mgr_funcs rv1_clk_funcs = {
	.init_clocks = rv1_init_clocks,
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = rv1_update_clocks,
	.enable_pme_wa = rv1_enable_pme_wa,
};

static struct clk_mgr_internal_funcs rv1_clk_internal_funcs = {
	.set_dispclk = rv1_vbios_smu_set_dispclk,
	.set_dprefclk = dce112_set_dprefclk
};

void rv1_clk_mgr_construct(struct dc_context *ctx, struct clk_mgr_internal *clk_mgr, struct pp_smu_funcs *pp_smu)
{
	struct dc_debug_options *debug = &ctx->dc->debug;
	struct dc_bios *bp = ctx->dc_bios;

	clk_mgr->base.ctx = ctx;
	clk_mgr->pp_smu = pp_smu;
	clk_mgr->base.funcs = &rv1_clk_funcs;
	clk_mgr->funcs = &rv1_clk_internal_funcs;

	clk_mgr->dfs_bypass_disp_clk = 0;

	clk_mgr->dprefclk_ss_percentage = 0;
	clk_mgr->dprefclk_ss_divider = 1000;
	clk_mgr->ss_on_dprefclk = false;
	clk_mgr->base.dprefclk_khz = 600000;

	if (bp->integrated_info)
		clk_mgr->base.dentist_vco_freq_khz = bp->integrated_info->dentist_vco_freq;
	if (bp->fw_info_valid && clk_mgr->base.dentist_vco_freq_khz == 0) {
		clk_mgr->base.dentist_vco_freq_khz = bp->fw_info.smu_gpu_pll_output_freq;
		if (clk_mgr->base.dentist_vco_freq_khz == 0)
			clk_mgr->base.dentist_vco_freq_khz = 3600000;
	}

	if (!debug->disable_dfs_bypass && bp->integrated_info)
		if (bp->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE)
			clk_mgr->dfs_bypass_enabled = true;

	dce_clock_read_ss_info(clk_mgr);
}


