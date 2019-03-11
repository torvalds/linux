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

#include "dcn10_clk_mgr.h"

#include "reg_helper.h"
#include "core_types.h"

#define TO_DCE_CLK_MGR(clocks)\
	container_of(clocks, struct dce_clk_mgr, base)

#define REG(reg) \
	(clk_mgr_dce->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	clk_mgr_dce->clk_mgr_shift->field_name, clk_mgr_dce->clk_mgr_mask->field_name

#define CTX \
	clk_mgr_dce->base.ctx
#define DC_LOGGER \
	clk_mgr->ctx->logger

void dcn1_pplib_apply_display_requirements(
	struct dc *dc,
	struct dc_state *context)
{
	struct dm_pp_display_configuration *pp_display_cfg = &context->pp_display_cfg;

	pp_display_cfg->min_engine_clock_khz = dc->res_pool->clk_mgr->clks.dcfclk_khz;
	pp_display_cfg->min_memory_clock_khz = dc->res_pool->clk_mgr->clks.fclk_khz;
	pp_display_cfg->min_engine_clock_deep_sleep_khz = dc->res_pool->clk_mgr->clks.dcfclk_deep_sleep_khz;
	pp_display_cfg->min_dcfc_deep_sleep_clock_khz = dc->res_pool->clk_mgr->clks.dcfclk_deep_sleep_khz;
	pp_display_cfg->min_dcfclock_khz = dc->res_pool->clk_mgr->clks.dcfclk_khz;
	pp_display_cfg->disp_clk_khz = dc->res_pool->clk_mgr->clks.dispclk_khz;
	dce110_fill_display_configs(context, pp_display_cfg);

	dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);
}

static int dcn1_determine_dppclk_threshold(struct clk_mgr *clk_mgr, struct dc_clocks *new_clocks)
{
	bool request_dpp_div = new_clocks->dispclk_khz > new_clocks->dppclk_khz;
	bool dispclk_increase = new_clocks->dispclk_khz > clk_mgr->clks.dispclk_khz;
	int disp_clk_threshold = new_clocks->max_supported_dppclk_khz;
	bool cur_dpp_div = clk_mgr->clks.dispclk_khz > clk_mgr->clks.dppclk_khz;

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
		if (clk_mgr->clks.dispclk_khz <= disp_clk_threshold)
			return new_clocks->dispclk_khz;

		/* request dpp clk need to be divided by 2 */
		if (request_dpp_div)
			return new_clocks->dispclk_khz;
	}

	return disp_clk_threshold;
}

static void dcn1_ramp_up_dispclk_with_dpp(struct clk_mgr *clk_mgr, struct dc_clocks *new_clocks)
{
	struct dc *dc = clk_mgr->ctx->dc;
	int dispclk_to_dpp_threshold = dcn1_determine_dppclk_threshold(clk_mgr, new_clocks);
	bool request_dpp_div = new_clocks->dispclk_khz > new_clocks->dppclk_khz;
	int i;

	/* set disp clk to dpp clk threshold */
	dce112_set_clock(clk_mgr, dispclk_to_dpp_threshold);

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
	if (dispclk_to_dpp_threshold != new_clocks->dispclk_khz)
		dce112_set_clock(clk_mgr, new_clocks->dispclk_khz);

	clk_mgr->clks.dispclk_khz = new_clocks->dispclk_khz;
	clk_mgr->clks.dppclk_khz = new_clocks->dppclk_khz;
	clk_mgr->clks.max_supported_dppclk_khz = new_clocks->max_supported_dppclk_khz;
}

static int get_active_display_cnt(
		struct dc *dc,
		struct dc_state *context)
{
	int i, display_count;

	display_count = 0;
	for (i = 0; i < context->stream_count; i++) {
		const struct dc_stream_state *stream = context->streams[i];

		/*
		 * Only notify active stream or virtual stream.
		 * Need to notify virtual stream to work around
		 * headless case. HPD does not fire when system is in
		 * S0i2.
		 */
		if (!stream->dpms_off || stream->signal == SIGNAL_TYPE_VIRTUAL)
			display_count++;
	}

	return display_count;
}

static void dcn1_update_clocks(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct dc *dc = clk_mgr->ctx->dc;
	struct dc_debug_options *debug = &dc->debug;
	struct dc_clocks *new_clocks = &context->bw.dcn.clk;
	struct pp_smu_display_requirement_rv *smu_req_cur =
			&dc->res_pool->pp_smu_req;
	struct pp_smu_display_requirement_rv smu_req = *smu_req_cur;
	struct pp_smu_funcs_rv *pp_smu = dc->res_pool->pp_smu;
	bool send_request_to_increase = false;
	bool send_request_to_lower = false;
	int display_count;

	bool enter_display_off = false;

	display_count = get_active_display_cnt(dc, context);

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

		smu_req.display_count = display_count;
	}

	if (new_clocks->dispclk_khz > clk_mgr->clks.dispclk_khz
			|| new_clocks->phyclk_khz > clk_mgr->clks.phyclk_khz
			|| new_clocks->fclk_khz > clk_mgr->clks.fclk_khz
			|| new_clocks->dcfclk_khz > clk_mgr->clks.dcfclk_khz)
		send_request_to_increase = true;

	if (should_set_clock(safe_to_lower, new_clocks->phyclk_khz, clk_mgr->clks.phyclk_khz)) {
		clk_mgr->clks.phyclk_khz = new_clocks->phyclk_khz;

		send_request_to_lower = true;
	}

	// F Clock
	if (debug->force_fclk_khz != 0)
		new_clocks->fclk_khz = debug->force_fclk_khz;

	if (should_set_clock(safe_to_lower, new_clocks->fclk_khz, clk_mgr->clks.fclk_khz)) {
		clk_mgr->clks.fclk_khz = new_clocks->fclk_khz;
		smu_req.hard_min_fclk_mhz = new_clocks->fclk_khz / 1000;

		send_request_to_lower = true;
	}

	//DCF Clock
	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr->clks.dcfclk_khz)) {
		clk_mgr->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		smu_req.hard_min_dcefclk_mhz = new_clocks->dcfclk_khz / 1000;

		send_request_to_lower = true;
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		smu_req.min_deep_sleep_dcefclk_mhz = (new_clocks->dcfclk_deep_sleep_khz + 999) / 1000;

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

			pp_smu->set_hard_min_fclk_by_freq(&pp_smu->pp_smu, smu_req.hard_min_fclk_mhz);
			pp_smu->set_hard_min_dcfclk_by_freq(&pp_smu->pp_smu, smu_req.hard_min_dcefclk_mhz);
			pp_smu->set_min_deep_sleep_dcfclk(&pp_smu->pp_smu, smu_req.min_deep_sleep_dcefclk_mhz);
		} else {
			if (pp_smu->set_display_requirement)
				pp_smu->set_display_requirement(&pp_smu->pp_smu, &smu_req);
			dcn1_pplib_apply_display_requirements(dc, context);
		}
	}

	/* dcn1 dppclk is tied to dispclk */
	/* program dispclk on = as a w/a for sleep resume clock ramping issues */
	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr->clks.dispclk_khz)
			|| new_clocks->dispclk_khz == clk_mgr->clks.dispclk_khz) {
		dcn1_ramp_up_dispclk_with_dpp(clk_mgr, new_clocks);
		clk_mgr->clks.dispclk_khz = new_clocks->dispclk_khz;

		send_request_to_lower = true;
	}

	if (!send_request_to_increase && send_request_to_lower) {
		/*use dcfclk to request voltage*/
		if (pp_smu->set_hard_min_fclk_by_freq &&
				pp_smu->set_hard_min_dcfclk_by_freq &&
				pp_smu->set_min_deep_sleep_dcfclk) {

			pp_smu->set_hard_min_fclk_by_freq(&pp_smu->pp_smu, smu_req.hard_min_fclk_mhz);
			pp_smu->set_hard_min_dcfclk_by_freq(&pp_smu->pp_smu, smu_req.hard_min_dcefclk_mhz);
			pp_smu->set_min_deep_sleep_dcfclk(&pp_smu->pp_smu, smu_req.min_deep_sleep_dcefclk_mhz);
		} else {
			if (pp_smu->set_display_requirement)
				pp_smu->set_display_requirement(&pp_smu->pp_smu, &smu_req);
			dcn1_pplib_apply_display_requirements(dc, context);
		}
	}

	*smu_req_cur = smu_req;
}
static const struct clk_mgr_funcs dcn1_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = dcn1_update_clocks
};
struct clk_mgr *dcn1_clk_mgr_create(struct dc_context *ctx)
{
	struct dc_debug_options *debug = &ctx->dc->debug;
	struct dc_bios *bp = ctx->dc_bios;
	struct dc_firmware_info fw_info = { { 0 } };
	struct dce_clk_mgr *clk_mgr_dce = kzalloc(sizeof(*clk_mgr_dce), GFP_KERNEL);

	if (clk_mgr_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	clk_mgr_dce->base.ctx = ctx;
	clk_mgr_dce->base.funcs = &dcn1_funcs;

	clk_mgr_dce->dfs_bypass_disp_clk = 0;

	clk_mgr_dce->dprefclk_ss_percentage = 0;
	clk_mgr_dce->dprefclk_ss_divider = 1000;
	clk_mgr_dce->ss_on_dprefclk = false;

	clk_mgr_dce->dprefclk_khz = 600000;
	if (bp->integrated_info)
		clk_mgr_dce->dentist_vco_freq_khz = bp->integrated_info->dentist_vco_freq;
	if (clk_mgr_dce->dentist_vco_freq_khz == 0) {
		bp->funcs->get_firmware_info(bp, &fw_info);
		clk_mgr_dce->dentist_vco_freq_khz = fw_info.smu_gpu_pll_output_freq;
		if (clk_mgr_dce->dentist_vco_freq_khz == 0)
			clk_mgr_dce->dentist_vco_freq_khz = 3600000;
	}

	if (!debug->disable_dfs_bypass && bp->integrated_info)
		if (bp->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE)
			clk_mgr_dce->dfs_bypass_enabled = true;

	dce_clock_read_ss_info(clk_mgr_dce);

	return &clk_mgr_dce->base;
}


