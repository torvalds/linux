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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce110_clk_mgr.h"
#include "../clk_mgr/dce100/dce_clk_mgr.h"

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

static const struct state_dependent_clocks dce110_max_clks_by_state[] = {
/*ClocksStateInvalid - should not be used*/
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/*ClocksStateUltraLow - currently by HW design team not supposed to be used*/
{ .display_clk_khz = 352000, .pixel_clk_khz = 330000 },
/*ClocksStateLow*/
{ .display_clk_khz = 352000, .pixel_clk_khz = 330000 },
/*ClocksStateNominal*/
{ .display_clk_khz = 467000, .pixel_clk_khz = 400000 },
/*ClocksStatePerformance*/
{ .display_clk_khz = 643000, .pixel_clk_khz = 400000 } };

static int determine_sclk_from_bounding_box(
		const struct dc *dc,
		int required_sclk)
{
	int i;

	/*
	 * Some asics do not give us sclk levels, so we just report the actual
	 * required sclk
	 */
	if (dc->sclk_lvls.num_levels == 0)
		return required_sclk;

	for (i = 0; i < dc->sclk_lvls.num_levels; i++) {
		if (dc->sclk_lvls.clocks_in_khz[i] >= required_sclk)
			return dc->sclk_lvls.clocks_in_khz[i];
	}
	/*
	 * even maximum level could not satisfy requirement, this
	 * is unexpected at this stage, should have been caught at
	 * validation time
	 */
	ASSERT(0);
	return dc->sclk_lvls.clocks_in_khz[dc->sclk_lvls.num_levels - 1];
}

uint32_t dce110_get_min_vblank_time_us(const struct dc_state *context)
{
	uint8_t j;
	uint32_t min_vertical_blank_time = -1;

	for (j = 0; j < context->stream_count; j++) {
		struct dc_stream_state *stream = context->streams[j];
		uint32_t vertical_blank_in_pixels = 0;
		uint32_t vertical_blank_time = 0;

		vertical_blank_in_pixels = stream->timing.h_total *
			(stream->timing.v_total
			 - stream->timing.v_addressable);

		vertical_blank_time = vertical_blank_in_pixels
			* 10000 / stream->timing.pix_clk_100hz;

		if (min_vertical_blank_time > vertical_blank_time)
			min_vertical_blank_time = vertical_blank_time;
	}

	return min_vertical_blank_time;
}

void dce110_fill_display_configs(
	const struct dc_state *context,
	struct dm_pp_display_configuration *pp_display_cfg)
{
	int j;
	int num_cfgs = 0;

	for (j = 0; j < context->stream_count; j++) {
		int k;

		const struct dc_stream_state *stream = context->streams[j];
		struct dm_pp_single_disp_config *cfg =
			&pp_display_cfg->disp_configs[num_cfgs];
		const struct pipe_ctx *pipe_ctx = NULL;

		for (k = 0; k < MAX_PIPES; k++)
			if (stream == context->res_ctx.pipe_ctx[k].stream) {
				pipe_ctx = &context->res_ctx.pipe_ctx[k];
				break;
			}

		ASSERT(pipe_ctx != NULL);

		/* only notify active stream */
		if (stream->dpms_off)
			continue;

		num_cfgs++;
		cfg->signal = pipe_ctx->stream->signal;
		cfg->pipe_idx = pipe_ctx->stream_res.tg->inst;
		cfg->src_height = stream->src.height;
		cfg->src_width = stream->src.width;
		cfg->ddi_channel_mapping =
			stream->link->ddi_channel_mapping.raw;
		cfg->transmitter =
			stream->link->link_enc->transmitter;
		cfg->link_settings.lane_count =
			stream->link->cur_link_settings.lane_count;
		cfg->link_settings.link_rate =
			stream->link->cur_link_settings.link_rate;
		cfg->link_settings.link_spread =
			stream->link->cur_link_settings.link_spread;
		cfg->sym_clock = stream->phy_pix_clk;
		/* Round v_refresh*/
		cfg->v_refresh = stream->timing.pix_clk_100hz * 100;
		cfg->v_refresh /= stream->timing.h_total;
		cfg->v_refresh = (cfg->v_refresh + stream->timing.v_total / 2)
							/ stream->timing.v_total;
	}

	pp_display_cfg->display_count = num_cfgs;
}

void dce11_pplib_apply_display_requirements(
	struct dc *dc,
	struct dc_state *context)
{
	struct dm_pp_display_configuration *pp_display_cfg = &context->pp_display_cfg;

	pp_display_cfg->all_displays_in_sync =
		context->bw_ctx.bw.dce.all_displays_in_sync;
	pp_display_cfg->nb_pstate_switch_disable =
			context->bw_ctx.bw.dce.nbp_state_change_enable == false;
	pp_display_cfg->cpu_cc6_disable =
			context->bw_ctx.bw.dce.cpuc_state_change_enable == false;
	pp_display_cfg->cpu_pstate_disable =
			context->bw_ctx.bw.dce.cpup_state_change_enable == false;
	pp_display_cfg->cpu_pstate_separation_time =
			context->bw_ctx.bw.dce.blackout_recovery_time_us;

	pp_display_cfg->min_memory_clock_khz = context->bw_ctx.bw.dce.yclk_khz
		/ MEMORY_TYPE_MULTIPLIER_CZ;

	pp_display_cfg->min_engine_clock_khz = determine_sclk_from_bounding_box(
			dc,
			context->bw_ctx.bw.dce.sclk_khz);

	/*
	 * As workaround for >4x4K lightup set dcfclock to min_engine_clock value.
	 * This is not required for less than 5 displays,
	 * thus don't request decfclk in dc to avoid impact
	 * on power saving.
	 *
	 */
	pp_display_cfg->min_dcfclock_khz = (context->stream_count > 4) ?
			pp_display_cfg->min_engine_clock_khz : 0;

	pp_display_cfg->min_engine_clock_deep_sleep_khz
			= context->bw_ctx.bw.dce.sclk_deep_sleep_khz;

	pp_display_cfg->avail_mclk_switch_time_us =
						dce110_get_min_vblank_time_us(context);
	/* TODO: dce11.2*/
	pp_display_cfg->avail_mclk_switch_time_in_disp_active_us = 0;

	pp_display_cfg->disp_clk_khz = dc->clk_mgr->clks.dispclk_khz;

	dce110_fill_display_configs(context, pp_display_cfg);

	/* TODO: is this still applicable?*/
	if (pp_display_cfg->display_count == 1) {
		const struct dc_crtc_timing *timing =
			&context->streams[0]->timing;

		pp_display_cfg->crtc_index =
			pp_display_cfg->disp_configs[0].pipe_idx;
		pp_display_cfg->line_time_in_us = timing->h_total * 10000 / timing->pix_clk_100hz;
	}

	if (memcmp(&dc->current_state->pp_display_cfg, pp_display_cfg, sizeof(*pp_display_cfg)) !=  0)
		dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);
}

static void dce11_update_clocks(struct clk_mgr *clk_mgr_base,
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
		context->bw_ctx.bw.dce.dispclk_khz = dce_set_clock(clk_mgr_base, patched_disp_clk);
		clk_mgr_base->clks.dispclk_khz = patched_disp_clk;
	}
	dce11_pplib_apply_display_requirements(clk_mgr_base->ctx->dc, context);
}

static struct clk_mgr_funcs dce110_funcs = {
	.get_dp_ref_clk_frequency = dce_get_dp_ref_freq_khz,
	.update_clocks = dce11_update_clocks
};

void dce110_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr)
{
	dce_clk_mgr_construct(ctx, clk_mgr);

	memcpy(clk_mgr->max_clks_by_state,
		dce110_max_clks_by_state,
		sizeof(dce110_max_clks_by_state));

	clk_mgr->regs = &disp_clk_regs;
	clk_mgr->clk_mgr_shift = &disp_clk_shift;
	clk_mgr->clk_mgr_mask = &disp_clk_mask;
	clk_mgr->base.funcs = &dce110_funcs;

}
