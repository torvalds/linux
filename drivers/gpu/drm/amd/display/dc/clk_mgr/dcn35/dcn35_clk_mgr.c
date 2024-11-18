/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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


#include "dcn35_clk_mgr.h"

#include "dccg.h"
#include "clk_mgr_internal.h"

// For dce12_get_dp_ref_freq_khz
#include "dce100/dce_clk_mgr.h"

// For dcn20_update_clocks_update_dpp_dto
#include "dcn20/dcn20_clk_mgr.h"




#include "reg_helper.h"
#include "core_types.h"
#include "dcn35_smu.h"
#include "dm_helpers.h"

/* TODO: remove this include once we ported over remaining clk mgr functions*/
#include "dcn30/dcn30_clk_mgr.h"
#include "dcn31/dcn31_clk_mgr.h"

#include "dc_dmub_srv.h"
#include "link.h"
#include "logger_types.h"

#undef DC_LOGGER
#define DC_LOGGER \
	clk_mgr->base.base.ctx->logger


#define regCLK1_CLK_PLL_REQ			0x0237
#define regCLK1_CLK_PLL_REQ_BASE_IDX		0

#define CLK1_CLK_PLL_REQ__FbMult_int__SHIFT	0x0
#define CLK1_CLK_PLL_REQ__PllSpineDiv__SHIFT	0xc
#define CLK1_CLK_PLL_REQ__FbMult_frac__SHIFT	0x10
#define CLK1_CLK_PLL_REQ__FbMult_int_MASK	0x000001FFL
#define CLK1_CLK_PLL_REQ__PllSpineDiv_MASK	0x0000F000L
#define CLK1_CLK_PLL_REQ__FbMult_frac_MASK	0xFFFF0000L

#define regCLK1_CLK2_BYPASS_CNTL			0x029c
#define regCLK1_CLK2_BYPASS_CNTL_BASE_IDX	0

#define CLK1_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL__SHIFT	0x0
#define CLK1_CLK2_BYPASS_CNTL__CLK2_BYPASS_DIV__SHIFT	0x10
#define CLK1_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL_MASK		0x00000007L
#define CLK1_CLK2_BYPASS_CNTL__CLK2_BYPASS_DIV_MASK		0x000F0000L

#define regCLK5_0_CLK5_spll_field_8				0x464b
#define regCLK5_0_CLK5_spll_field_8_BASE_IDX	0

#define CLK5_0_CLK5_spll_field_8__spll_ssc_en__SHIFT	0xd
#define CLK5_0_CLK5_spll_field_8__spll_ssc_en_MASK		0x00002000L

#define SMU_VER_THRESHOLD 0x5D4A00 //93.74.0

#define REG(reg_name) \
	(ctx->clk_reg_offsets[reg ## reg_name ## _BASE_IDX] + reg ## reg_name)

#define TO_CLK_MGR_DCN35(clk_mgr)\
	container_of(clk_mgr, struct clk_mgr_dcn35, base)

static int dcn35_get_active_display_cnt_wa(
		struct dc *dc,
		struct dc_state *context,
		int *all_active_disps)
{
	int i, display_count = 0;
	bool tmds_present = false;

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
		if (link->link_enc && link->link_enc->funcs->is_dig_enabled &&
				link->link_enc->funcs->is_dig_enabled(link->link_enc))
			display_count++;
	}
	if (all_active_disps != NULL)
		*all_active_disps = display_count;
	/* WA for hang on HDMI after display off back on*/
	if (display_count == 0 && tmds_present)
		display_count = 1;

	return display_count;
}
static void dcn35_disable_otg_wa(struct clk_mgr *clk_mgr_base, struct dc_state *context,
		bool safe_to_lower, bool disable)
{
	struct dc *dc = clk_mgr_base->ctx->dc;
	int i;

	if (dc->ctx->dce_environment == DCE_ENV_DIAG)
		return;

	for (i = 0; i < dc->res_pool->pipe_count; ++i) {
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *new_pipe = &context->res_ctx.pipe_ctx[i];
		struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
		struct dccg *dccg = clk_mgr_internal->dccg;
		struct pipe_ctx *pipe = safe_to_lower
			? &context->res_ctx.pipe_ctx[i]
			: &dc->current_state->res_ctx.pipe_ctx[i];
		bool stream_changed_otg_dig_on = false;
		if (pipe->top_pipe || pipe->prev_odm_pipe)
			continue;
		stream_changed_otg_dig_on = old_pipe->stream && new_pipe->stream &&
		old_pipe->stream != new_pipe->stream &&
		old_pipe->stream_res.tg == new_pipe->stream_res.tg &&
		new_pipe->stream->link_enc && !new_pipe->stream->dpms_off &&
		new_pipe->stream->link_enc->funcs->is_dig_enabled &&
		new_pipe->stream->link_enc->funcs->is_dig_enabled(
		new_pipe->stream->link_enc) &&
		new_pipe->stream_res.stream_enc &&
		new_pipe->stream_res.stream_enc->funcs->is_fifo_enabled &&
		new_pipe->stream_res.stream_enc->funcs->is_fifo_enabled(new_pipe->stream_res.stream_enc);

		bool has_active_hpo = false;

		if (old_pipe->stream && new_pipe->stream && old_pipe->stream == new_pipe->stream) {
			has_active_hpo =  dccg->ctx->dc->link_srv->dp_is_128b_132b_signal(old_pipe) &&
			dccg->ctx->dc->link_srv->dp_is_128b_132b_signal(new_pipe);

		 }


		if (!has_active_hpo && !dccg->ctx->dc->link_srv->dp_is_128b_132b_signal(pipe) &&
					(pipe->stream && (pipe->stream->dpms_off || dc_is_virtual_signal(pipe->stream->signal) ||
					!pipe->stream->link_enc) && !stream_changed_otg_dig_on)) {


			/* This w/a should not trigger when we have a dig active */
			if (disable) {
				if (pipe->stream_res.tg && pipe->stream_res.tg->funcs->immediate_disable_crtc)
					pipe->stream_res.tg->funcs->immediate_disable_crtc(pipe->stream_res.tg);

				reset_sync_context_for_pipe(dc, context, i);
			} else {
				pipe->stream_res.tg->funcs->enable_crtc(pipe->stream_res.tg);
			}
		}
	}
}

static void dcn35_update_clocks_update_dtb_dto(struct clk_mgr_internal *clk_mgr,
			struct dc_state *context,
			int ref_dtbclk_khz)
{
	struct dccg *dccg = clk_mgr->dccg;
	uint32_t tg_mask = 0;
	int i;

	for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct dtbclk_dto_params dto_params = {0};

		/* use mask to program DTO once per tg */
		if (pipe_ctx->stream_res.tg &&
				!(tg_mask & (1 << pipe_ctx->stream_res.tg->inst))) {
			tg_mask |= (1 << pipe_ctx->stream_res.tg->inst);

			dto_params.otg_inst = pipe_ctx->stream_res.tg->inst;
			dto_params.ref_dtbclk_khz = ref_dtbclk_khz;

			dccg->funcs->set_dtbclk_dto(clk_mgr->dccg, &dto_params);
			//dccg->funcs->set_audio_dtbclk_dto(clk_mgr->dccg, &dto_params);
		}
	}
}

static void dcn35_update_clocks_update_dpp_dto(struct clk_mgr_internal *clk_mgr,
		struct dc_state *context, bool safe_to_lower)
{
	int i;
	bool dppclk_active[MAX_PIPES] = {0};


	clk_mgr->dccg->ref_dppclk = clk_mgr->base.clks.dppclk_khz;
	for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
		int dpp_inst = 0, dppclk_khz, prev_dppclk_khz;

		dppclk_khz = context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz;

		if (context->res_ctx.pipe_ctx[i].plane_res.dpp)
			dpp_inst = context->res_ctx.pipe_ctx[i].plane_res.dpp->inst;
		else if (!context->res_ctx.pipe_ctx[i].plane_res.dpp && dppclk_khz == 0) {
			/* dpp == NULL && dppclk_khz == 0 is valid because of pipe harvesting.
			 * In this case just continue in loop
			 */
			continue;
		} else if (!context->res_ctx.pipe_ctx[i].plane_res.dpp && dppclk_khz > 0) {
			/* The software state is not valid if dpp resource is NULL and
			 * dppclk_khz > 0.
			 */
			ASSERT(false);
			continue;
		}

		prev_dppclk_khz = clk_mgr->dccg->pipe_dppclk_khz[i];

		if (safe_to_lower || prev_dppclk_khz < dppclk_khz)
			clk_mgr->dccg->funcs->update_dpp_dto(
							clk_mgr->dccg, dpp_inst, dppclk_khz);
		dppclk_active[dpp_inst] = true;
	}
	if (safe_to_lower)
		for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
			struct dpp *old_dpp = clk_mgr->base.ctx->dc->current_state->res_ctx.pipe_ctx[i].plane_res.dpp;

			if (old_dpp && !dppclk_active[old_dpp->inst])
				clk_mgr->dccg->funcs->update_dpp_dto(clk_mgr->dccg, old_dpp->inst, 0);
		}
}

static uint8_t get_lowest_dpia_index(const struct dc_link *link)
{
	const struct dc *dc_struct = link->dc;
	uint8_t idx = 0xFF;
	int i;

	for (i = 0; i < MAX_PIPES * 2; ++i) {
		if (!dc_struct->links[i] || dc_struct->links[i]->ep_type != DISPLAY_ENDPOINT_USB4_DPIA)
			continue;

		if (idx > dc_struct->links[i]->link_index)
			idx = dc_struct->links[i]->link_index;
	}

	return idx;
}

static void dcn35_notify_host_router_bw(struct clk_mgr *clk_mgr_base, struct dc_state *context,
					bool safe_to_lower)
{
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint32_t host_router_bw_kbps[MAX_HOST_ROUTERS_NUM] = { 0 };
	int i;
	for (i = 0; i < context->stream_count; ++i) {
		const struct dc_stream_state *stream = context->streams[i];
		const struct dc_link *link = stream->link;
		uint8_t lowest_dpia_index = 0;
		unsigned int hr_index = 0;

		if (!link)
			continue;

		lowest_dpia_index = get_lowest_dpia_index(link);
		if (link->link_index < lowest_dpia_index)
			continue;

		hr_index = (link->link_index - lowest_dpia_index) / 2;
		if (hr_index >= MAX_HOST_ROUTERS_NUM)
			continue;
		host_router_bw_kbps[hr_index] += dc_bandwidth_in_kbps_from_timing(
			&stream->timing, dc_link_get_highest_encoding_format(link));
	}

	for (i = 0; i < MAX_HOST_ROUTERS_NUM; ++i) {
		new_clocks->host_router_bw_kbps[i] = host_router_bw_kbps[i];
		if (should_set_clock(safe_to_lower, new_clocks->host_router_bw_kbps[i], clk_mgr_base->clks.host_router_bw_kbps[i])) {
			clk_mgr_base->clks.host_router_bw_kbps[i] = new_clocks->host_router_bw_kbps[i];
			dcn35_smu_notify_host_router_bw(clk_mgr, i, new_clocks->host_router_bw_kbps[i]);
		}
	}
}

void dcn35_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower)
{
	union dmub_rb_cmd cmd;
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	struct dc *dc = clk_mgr_base->ctx->dc;
	int display_count = 0;
	bool update_dppclk = false;
	bool update_dispclk = false;
	bool dpp_clock_lowered = false;
	int all_active_disps = 0;

	if (dc->work_arounds.skip_clock_update)
		return;

	display_count = dcn35_get_active_display_cnt_wa(dc, context, &all_active_disps);
	if (new_clocks->dtbclk_en && !new_clocks->ref_dtbclk_khz)
		new_clocks->ref_dtbclk_khz = 600000;

	/*
	 * if it is safe to lower, but we are already in the lower state, we don't have to do anything
	 * also if safe to lower is false, we just go in the higher state
	 */
	if (safe_to_lower) {
		if (new_clocks->zstate_support != DCN_ZSTATE_SUPPORT_DISALLOW &&
				new_clocks->zstate_support != clk_mgr_base->clks.zstate_support) {
			dcn35_smu_set_zstate_support(clk_mgr, new_clocks->zstate_support);
			dm_helpers_enable_periodic_detection(clk_mgr_base->ctx, true);
			clk_mgr_base->clks.zstate_support = new_clocks->zstate_support;
		}

		if (clk_mgr_base->clks.dtbclk_en && !new_clocks->dtbclk_en) {
			if (clk_mgr->base.ctx->dc->config.allow_0_dtb_clk)
				dcn35_smu_set_dtbclk(clk_mgr, false);
			clk_mgr_base->clks.dtbclk_en = new_clocks->dtbclk_en;
		}
		/* check that we're not already in lower */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_LOW_POWER) {
			/* if we can go lower, go lower */
			if (display_count == 0)
				clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_LOW_POWER;
		}
	} else {
		if (new_clocks->zstate_support == DCN_ZSTATE_SUPPORT_DISALLOW &&
				new_clocks->zstate_support != clk_mgr_base->clks.zstate_support) {
			dcn35_smu_set_zstate_support(clk_mgr, DCN_ZSTATE_SUPPORT_DISALLOW);
			dm_helpers_enable_periodic_detection(clk_mgr_base->ctx, false);
			clk_mgr_base->clks.zstate_support = new_clocks->zstate_support;
		}

		if (!clk_mgr_base->clks.dtbclk_en && new_clocks->dtbclk_en) {
			dcn35_smu_set_dtbclk(clk_mgr, true);
			clk_mgr_base->clks.dtbclk_en = new_clocks->dtbclk_en;

			dcn35_update_clocks_update_dtb_dto(clk_mgr, context, new_clocks->ref_dtbclk_khz);
			clk_mgr_base->clks.ref_dtbclk_khz = new_clocks->ref_dtbclk_khz;
		}

		/* check that we're not already in D0 */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_MISSION_MODE) {
			union display_idle_optimization_u idle_info = { 0 };

			dcn35_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
			/* update power state */
			clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_MISSION_MODE;
		}
	}
	if (dc->debug.force_min_dcfclk_mhz > 0)
		new_clocks->dcfclk_khz = (new_clocks->dcfclk_khz > (dc->debug.force_min_dcfclk_mhz * 1000)) ?
				new_clocks->dcfclk_khz : (dc->debug.force_min_dcfclk_mhz * 1000);

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		dcn35_smu_set_hard_min_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_khz);
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		dcn35_smu_set_min_deep_sleep_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_deep_sleep_khz);
	}

	// workaround: Limit dppclk to 100Mhz to avoid lower eDP panel switch to plus 4K monitor underflow.
	if (new_clocks->dppclk_khz < 100000)
		new_clocks->dppclk_khz = 100000;

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr->base.clks.dppclk_khz)) {
		if (clk_mgr->base.clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;
		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		dcn35_disable_otg_wa(clk_mgr_base, context, safe_to_lower, true);

		if (dc->debug.min_disp_clk_khz > 0 && new_clocks->dispclk_khz < dc->debug.min_disp_clk_khz)
			new_clocks->dispclk_khz = dc->debug.min_disp_clk_khz;

		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;
		dcn35_smu_set_dispclk(clk_mgr, clk_mgr_base->clks.dispclk_khz);
		dcn35_disable_otg_wa(clk_mgr_base, context, safe_to_lower, false);

		update_dispclk = true;
	}

	/* clock limits are received with MHz precision, divide by 1000 to prevent setting clocks at every call */
	if (!dc->debug.disable_dtb_ref_clk_switch &&
	    should_set_clock(safe_to_lower, new_clocks->ref_dtbclk_khz / 1000,
			     clk_mgr_base->clks.ref_dtbclk_khz / 1000)) {
		dcn35_update_clocks_update_dtb_dto(clk_mgr, context, new_clocks->ref_dtbclk_khz);
		clk_mgr_base->clks.ref_dtbclk_khz = new_clocks->ref_dtbclk_khz;
	}

	if (dpp_clock_lowered) {
		// increase per DPP DTO before lowering global dppclk
		dcn35_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		dcn35_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
	} else {
		// increase global DPPCLK before lowering per DPP DTO
		if (update_dppclk || update_dispclk)
			dcn35_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
		dcn35_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
	}

	// notify PMFW of bandwidth per DPIA tunnel
	if (dc->debug.notify_dpia_hr_bw)
		dcn35_notify_host_router_bw(clk_mgr_base, context, safe_to_lower);

	// notify DMCUB of latest clocks
	memset(&cmd, 0, sizeof(cmd));
	cmd.notify_clocks.header.type = DMUB_CMD__CLK_MGR;
	cmd.notify_clocks.header.sub_type = DMUB_CMD__CLK_MGR_NOTIFY_CLOCKS;
	cmd.notify_clocks.clocks.dcfclk_khz = clk_mgr_base->clks.dcfclk_khz;
	cmd.notify_clocks.clocks.dcfclk_deep_sleep_khz =
		clk_mgr_base->clks.dcfclk_deep_sleep_khz;
	cmd.notify_clocks.clocks.dispclk_khz = clk_mgr_base->clks.dispclk_khz;
	cmd.notify_clocks.clocks.dppclk_khz = clk_mgr_base->clks.dppclk_khz;

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static int get_vco_frequency_from_reg(struct clk_mgr_internal *clk_mgr)
{
	/* get FbMult value */
	struct fixed31_32 pll_req;
	unsigned int fbmult_frac_val = 0;
	unsigned int fbmult_int_val = 0;
	struct dc_context *ctx = clk_mgr->base.ctx;

	/*
	 * Register value of fbmult is in 8.16 format, we are converting to 314.32
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

static void dcn35_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	dcn35_smu_enable_pme_wa(clk_mgr);
}


bool dcn35_are_clock_states_equal(struct dc_clocks *a,
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
	else if (a->zstate_support != b->zstate_support)
		return false;
	else if (a->dtbclk_en != b->dtbclk_en)
		return false;

	return true;
}

static void dcn35_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr_dcn35 *clk_mgr)
{
}

static bool dcn35_is_spll_ssc_enabled(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc_context *ctx = clk_mgr->base.ctx;
	uint32_t ssc_enable;

	REG_GET(CLK5_0_CLK5_spll_field_8, spll_ssc_en, &ssc_enable);

	return ssc_enable == 1;
}

static void init_clk_states(struct clk_mgr *clk_mgr)
{
	struct clk_mgr_internal *clk_mgr_int = TO_CLK_MGR_INTERNAL(clk_mgr);
	uint32_t ref_dtbclk = clk_mgr->clks.ref_dtbclk_khz;
	memset(&(clk_mgr->clks), 0, sizeof(struct dc_clocks));

	if (clk_mgr_int->smu_ver >= SMU_VER_THRESHOLD)
		clk_mgr->clks.dtbclk_en = true; // request DTBCLK disable on first commit
	clk_mgr->clks.ref_dtbclk_khz = ref_dtbclk;	// restore ref_dtbclk
	clk_mgr->clks.p_state_change_support = true;
	clk_mgr->clks.prev_p_state_change_support = true;
	clk_mgr->clks.pwr_state = DCN_PWR_STATE_UNKNOWN;
	clk_mgr->clks.zstate_support = DCN_ZSTATE_SUPPORT_UNKNOWN;
}

void dcn35_init_clocks(struct clk_mgr *clk_mgr)
{
	struct clk_mgr_internal *clk_mgr_int = TO_CLK_MGR_INTERNAL(clk_mgr);
	init_clk_states(clk_mgr);

	// to adjust dp_dto reference clock if ssc is enable otherwise to apply dprefclk
	if (dcn35_is_spll_ssc_enabled(clk_mgr))
		clk_mgr->dp_dto_source_clock_in_khz =
			dce_adjust_dp_ref_freq_for_ss(clk_mgr_int, clk_mgr->dprefclk_khz);
	else
		clk_mgr->dp_dto_source_clock_in_khz = clk_mgr->dprefclk_khz;

}
static struct clk_bw_params dcn35_bw_params = {
	.vram_type = Ddr4MemType,
	.num_channels = 1,
	.clk_table = {
		.num_entries = 4,
	},

};

static struct wm_table ddr5_wm_table = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
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
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
	}
};

static DpmClocks_t_dcn35 dummy_clocks;
static DpmClocks_t_dcn351 dummy_clocks_dcn351;

static struct dcn35_watermarks dummy_wms = { 0 };

static struct dcn35_ss_info_table ss_info_table = {
	.ss_divider = 1000,
	.ss_percentage = {0, 0, 375, 375, 375}
};

static void dcn35_read_ss_info_from_lut(struct clk_mgr_internal *clk_mgr)
{
	struct dc_context *ctx = clk_mgr->base.ctx;
	uint32_t clock_source;

	REG_GET(CLK1_CLK2_BYPASS_CNTL, CLK2_BYPASS_SEL, &clock_source);
	// If it's DFS mode, clock_source is 0.
	if (dcn35_is_spll_ssc_enabled(&clk_mgr->base) && (clock_source < ARRAY_SIZE(ss_info_table.ss_percentage))) {
		clk_mgr->dprefclk_ss_percentage = ss_info_table.ss_percentage[clock_source];

		if (clk_mgr->dprefclk_ss_percentage != 0) {
			clk_mgr->ss_on_dprefclk = true;
			clk_mgr->dprefclk_ss_divider = ss_info_table.ss_divider;
		}
	}
}

static void dcn35_build_watermark_ranges(struct clk_bw_params *bw_params, struct dcn35_watermarks *table)
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

static void dcn35_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_mgr_dcn35 *clk_mgr_dcn35 = TO_CLK_MGR_DCN35(clk_mgr);
	struct dcn35_watermarks *table = clk_mgr_dcn35->smu_wm_set.wm_set;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || clk_mgr_dcn35->smu_wm_set.mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn35_build_watermark_ranges(clk_mgr_base->bw_params, table);

	dcn35_smu_set_dram_addr_high(clk_mgr,
			clk_mgr_dcn35->smu_wm_set.mc_address.high_part);
	dcn35_smu_set_dram_addr_low(clk_mgr,
			clk_mgr_dcn35->smu_wm_set.mc_address.low_part);
	dcn35_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

static void dcn35_get_dpm_table_from_smu(struct clk_mgr_internal *clk_mgr,
		struct dcn35_smu_dpm_clks *smu_dpm_clks)
{
	DpmClocks_t_dcn35 *table = smu_dpm_clks->dpm_clks;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || smu_dpm_clks->mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn35_smu_set_dram_addr_high(clk_mgr,
			smu_dpm_clks->mc_address.high_part);
	dcn35_smu_set_dram_addr_low(clk_mgr,
			smu_dpm_clks->mc_address.low_part);
	dcn35_smu_transfer_dpm_table_smu_2_dram(clk_mgr);
}

static void dcn351_get_dpm_table_from_smu(struct clk_mgr_internal *clk_mgr,
		struct dcn351_smu_dpm_clks *smu_dpm_clks)
{
	DpmClocks_t_dcn351 *table = smu_dpm_clks->dpm_clks;

	if (!clk_mgr->smu_ver)
		return;
	if (!table || smu_dpm_clks->mc_address.quad_part == 0)
		return;
	memset(table, 0, sizeof(*table));
	dcn35_smu_set_dram_addr_high(clk_mgr,
			smu_dpm_clks->mc_address.high_part);
	dcn35_smu_set_dram_addr_low(clk_mgr,
			smu_dpm_clks->mc_address.low_part);
	dcn35_smu_transfer_dpm_table_smu_2_dram(clk_mgr);
}
static uint32_t find_max_clk_value(const uint32_t clocks[], uint32_t num_clocks)
{
	uint32_t max = 0;
	int i;

	for (i = 0; i < num_clocks; ++i) {
		if (clocks[i] > max)
			max = clocks[i];
	}

	return max;
}

static inline bool is_valid_clock_value(uint32_t clock_value)
{
	return clock_value > 1 && clock_value < 100000;
}

static unsigned int convert_wck_ratio(uint8_t wck_ratio)
{
	switch (wck_ratio) {
	case WCK_RATIO_1_2:
		return 2;

	case WCK_RATIO_1_4:
		return 4;
	/* Find lowest DPM, FCLK is filled in reverse order*/

	default:
			break;
	}

	return 1;
}

static inline uint32_t calc_dram_speed_mts(const MemPstateTable_t *entry)
{
	return entry->UClk * convert_wck_ratio(entry->WckRatio) * 2;
}

static void dcn35_clk_mgr_helper_populate_bw_params(struct clk_mgr_internal *clk_mgr,
						    struct integrated_info *bios_info,
						    DpmClocks_t_dcn35 *clock_table)
{
	struct clk_bw_params *bw_params = clk_mgr->base.bw_params;
	struct clk_limit_table_entry def_max = bw_params->clk_table.entries[bw_params->clk_table.num_entries - 1];
	uint32_t max_fclk = 0, min_pstate = 0, max_dispclk = 0, max_dppclk = 0;
	uint32_t max_pstate = 0, max_dram_speed_mts = 0, min_dram_speed_mts = 0;
	uint32_t num_memps, num_fclk, num_dcfclk;
	int i;

	/* Determine min/max p-state values. */
	num_memps = (clock_table->NumMemPstatesEnabled > NUM_MEM_PSTATE_LEVELS) ? NUM_MEM_PSTATE_LEVELS :
		clock_table->NumMemPstatesEnabled;
	for (i = 0; i < num_memps; i++) {
		uint32_t dram_speed_mts = calc_dram_speed_mts(&clock_table->MemPstateTable[i]);

		if (is_valid_clock_value(dram_speed_mts) && dram_speed_mts > max_dram_speed_mts) {
			max_dram_speed_mts = dram_speed_mts;
			max_pstate = i;
		}
	}

	min_dram_speed_mts = max_dram_speed_mts;
	min_pstate = max_pstate;

	for (i = 0; i < num_memps; i++) {
		uint32_t dram_speed_mts = calc_dram_speed_mts(&clock_table->MemPstateTable[i]);

		if (is_valid_clock_value(dram_speed_mts) && dram_speed_mts < min_dram_speed_mts) {
			min_dram_speed_mts = dram_speed_mts;
			min_pstate = i;
		}
	}

	/* We expect the table to contain at least one valid P-state entry. */
	ASSERT(clock_table->NumMemPstatesEnabled &&
	       is_valid_clock_value(max_dram_speed_mts) &&
	       is_valid_clock_value(min_dram_speed_mts));

	/* dispclk and dppclk can be max at any voltage, same number of levels for both */
	if (clock_table->NumDispClkLevelsEnabled <= NUM_DISPCLK_DPM_LEVELS &&
	    clock_table->NumDispClkLevelsEnabled <= NUM_DPPCLK_DPM_LEVELS) {
		max_dispclk = find_max_clk_value(clock_table->DispClocks,
			clock_table->NumDispClkLevelsEnabled);
		max_dppclk = find_max_clk_value(clock_table->DppClocks,
			clock_table->NumDispClkLevelsEnabled);
	} else {
		/* Invalid number of entries in the table from PMFW. */
		ASSERT(0);
	}

	/* Base the clock table on dcfclk, need at least one entry regardless of pmfw table */
	ASSERT(clock_table->NumDcfClkLevelsEnabled > 0);

	num_fclk = (clock_table->NumFclkLevelsEnabled > NUM_FCLK_DPM_LEVELS) ? NUM_FCLK_DPM_LEVELS :
		clock_table->NumFclkLevelsEnabled;
	max_fclk = find_max_clk_value(clock_table->FclkClocks_Freq, num_fclk);

	num_dcfclk = (clock_table->NumDcfClkLevelsEnabled > NUM_DCFCLK_DPM_LEVELS) ? NUM_DCFCLK_DPM_LEVELS :
		clock_table->NumDcfClkLevelsEnabled;
	for (i = 0; i < num_dcfclk; i++) {
		int j;

		/* First search defaults for the clocks we don't read using closest lower or equal default dcfclk */
		for (j = bw_params->clk_table.num_entries - 1; j > 0; j--)
			if (bw_params->clk_table.entries[j].dcfclk_mhz <= clock_table->DcfClocks[i])
				break;

		bw_params->clk_table.entries[i].phyclk_mhz = bw_params->clk_table.entries[j].phyclk_mhz;
		bw_params->clk_table.entries[i].phyclk_d18_mhz = bw_params->clk_table.entries[j].phyclk_d18_mhz;
		bw_params->clk_table.entries[i].dtbclk_mhz = bw_params->clk_table.entries[j].dtbclk_mhz;

		/* Now update clocks we do read */
		bw_params->clk_table.entries[i].memclk_mhz = clock_table->MemPstateTable[min_pstate].MemClk;
		bw_params->clk_table.entries[i].voltage = clock_table->MemPstateTable[min_pstate].Voltage;
		bw_params->clk_table.entries[i].dcfclk_mhz = clock_table->DcfClocks[i];
		bw_params->clk_table.entries[i].socclk_mhz = clock_table->SocClocks[i];
		bw_params->clk_table.entries[i].dispclk_mhz = max_dispclk;
		bw_params->clk_table.entries[i].dppclk_mhz = max_dppclk;
		bw_params->clk_table.entries[i].wck_ratio =
			convert_wck_ratio(clock_table->MemPstateTable[min_pstate].WckRatio);

		/* Dcfclk and Fclk are tied, but at a different ratio */
		bw_params->clk_table.entries[i].fclk_mhz = min(max_fclk, 2 * clock_table->DcfClocks[i]);
	}

	/* Make sure to include at least one entry at highest pstate */
	if (max_pstate != min_pstate || i == 0) {
		if (i > MAX_NUM_DPM_LVL - 1)
			i = MAX_NUM_DPM_LVL - 1;

		bw_params->clk_table.entries[i].fclk_mhz = max_fclk;
		bw_params->clk_table.entries[i].memclk_mhz = clock_table->MemPstateTable[max_pstate].MemClk;
		bw_params->clk_table.entries[i].voltage = clock_table->MemPstateTable[max_pstate].Voltage;
		bw_params->clk_table.entries[i].dcfclk_mhz =
			find_max_clk_value(clock_table->DcfClocks, NUM_DCFCLK_DPM_LEVELS);
		bw_params->clk_table.entries[i].socclk_mhz =
			find_max_clk_value(clock_table->SocClocks, NUM_SOCCLK_DPM_LEVELS);
		bw_params->clk_table.entries[i].dispclk_mhz = max_dispclk;
		bw_params->clk_table.entries[i].dppclk_mhz = max_dppclk;
		bw_params->clk_table.entries[i].wck_ratio = convert_wck_ratio(
			clock_table->MemPstateTable[max_pstate].WckRatio);
		i++;
	}
	bw_params->clk_table.num_entries = i--;

	/* Make sure all highest clocks are included*/
	bw_params->clk_table.entries[i].socclk_mhz =
		find_max_clk_value(clock_table->SocClocks, NUM_SOCCLK_DPM_LEVELS);
	bw_params->clk_table.entries[i].dispclk_mhz =
		find_max_clk_value(clock_table->DispClocks, NUM_DISPCLK_DPM_LEVELS);
	bw_params->clk_table.entries[i].dppclk_mhz =
		find_max_clk_value(clock_table->DppClocks, NUM_DPPCLK_DPM_LEVELS);
	bw_params->clk_table.entries[i].fclk_mhz =
		find_max_clk_value(clock_table->FclkClocks_Freq, NUM_FCLK_DPM_LEVELS);
	ASSERT(clock_table->DcfClocks[i] == find_max_clk_value(clock_table->DcfClocks, NUM_DCFCLK_DPM_LEVELS));
	bw_params->clk_table.entries[i].phyclk_mhz = def_max.phyclk_mhz;
	bw_params->clk_table.entries[i].phyclk_d18_mhz = def_max.phyclk_d18_mhz;
	bw_params->clk_table.entries[i].dtbclk_mhz = def_max.dtbclk_mhz;
	bw_params->clk_table.num_entries_per_clk.num_dcfclk_levels = clock_table->NumDcfClkLevelsEnabled;
	bw_params->clk_table.num_entries_per_clk.num_dispclk_levels = clock_table->NumDispClkLevelsEnabled;
	bw_params->clk_table.num_entries_per_clk.num_dppclk_levels = clock_table->NumDispClkLevelsEnabled;
	bw_params->clk_table.num_entries_per_clk.num_fclk_levels = clock_table->NumFclkLevelsEnabled;
	bw_params->clk_table.num_entries_per_clk.num_memclk_levels = clock_table->NumMemPstatesEnabled;
	bw_params->clk_table.num_entries_per_clk.num_socclk_levels = clock_table->NumSocClkLevelsEnabled;

	/*
	 * Set any 0 clocks to max default setting. Not an issue for
	 * power since we aren't doing switching in such case anyway
	 */
	for (i = 0; i < bw_params->clk_table.num_entries; i++) {
		if (!bw_params->clk_table.entries[i].fclk_mhz) {
			bw_params->clk_table.entries[i].fclk_mhz = def_max.fclk_mhz;
			bw_params->clk_table.entries[i].memclk_mhz = def_max.memclk_mhz;
			bw_params->clk_table.entries[i].voltage = def_max.voltage;
		}
		if (!bw_params->clk_table.entries[i].dcfclk_mhz)
			bw_params->clk_table.entries[i].dcfclk_mhz = def_max.dcfclk_mhz;
		if (!bw_params->clk_table.entries[i].socclk_mhz)
			bw_params->clk_table.entries[i].socclk_mhz = def_max.socclk_mhz;
		if (!bw_params->clk_table.entries[i].dispclk_mhz)
			bw_params->clk_table.entries[i].dispclk_mhz = def_max.dispclk_mhz;
		if (!bw_params->clk_table.entries[i].dppclk_mhz)
			bw_params->clk_table.entries[i].dppclk_mhz = def_max.dppclk_mhz;
		if (!bw_params->clk_table.entries[i].fclk_mhz)
			bw_params->clk_table.entries[i].fclk_mhz = def_max.fclk_mhz;
		if (!bw_params->clk_table.entries[i].phyclk_mhz)
			bw_params->clk_table.entries[i].phyclk_mhz = def_max.phyclk_mhz;
		if (!bw_params->clk_table.entries[i].phyclk_d18_mhz)
			bw_params->clk_table.entries[i].phyclk_d18_mhz = def_max.phyclk_d18_mhz;
		if (!bw_params->clk_table.entries[i].dtbclk_mhz)
			bw_params->clk_table.entries[i].dtbclk_mhz = def_max.dtbclk_mhz;
	}
	ASSERT(bw_params->clk_table.entries[i-1].dcfclk_mhz);
	bw_params->vram_type = bios_info->memory_type;
	bw_params->dram_channel_width_bytes = bios_info->memory_type == 0x22 ? 8 : 4;
	bw_params->num_channels = bios_info->ma_channel_number ? bios_info->ma_channel_number : 4;

	for (i = 0; i < WM_SET_COUNT; i++) {
		bw_params->wm_table.entries[i].wm_inst = i;

		if (i >= bw_params->clk_table.num_entries) {
			bw_params->wm_table.entries[i].valid = false;
			continue;
		}

		bw_params->wm_table.entries[i].wm_type = WM_TYPE_PSTATE_CHG;
		bw_params->wm_table.entries[i].valid = true;
	}
}

static void dcn35_set_low_power_state(struct clk_mgr *clk_mgr_base)
{
	int display_count;
	struct dc *dc = clk_mgr_base->ctx->dc;
	struct dc_state *context = dc->current_state;

	if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_LOW_POWER) {
		display_count = dcn35_get_active_display_cnt_wa(dc, context, NULL);
		/* if we can go lower, go lower */
		if (display_count == 0)
			clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_LOW_POWER;
	}
}

static void dcn35_exit_low_power_state(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	//SMU optimization is performed part of low power state exit.
	dcn35_smu_exit_low_power_state(clk_mgr);

}

static bool dcn35_is_ips_supported(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	return dcn35_smu_get_ips_supported(clk_mgr) ? true : false;
}

static void dcn35_init_clocks_fpga(struct clk_mgr *clk_mgr)
{
	init_clk_states(clk_mgr);

/* TODO: Implement the functions and remove the ifndef guard */
}

static void dcn35_update_clocks_fpga(struct clk_mgr *clk_mgr,
		struct dc_state *context,
		bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr_int = TO_CLK_MGR_INTERNAL(clk_mgr);
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	int fclk_adj = new_clocks->fclk_khz;

	/* TODO: remove this after correctly set by DML */
	new_clocks->dcfclk_khz = 400000;
	new_clocks->socclk_khz = 400000;

	/* Min fclk = 1.2GHz since all the extra scemi logic seems to run off of it */
	//int fclk_adj = new_clocks->fclk_khz > 1200000 ? new_clocks->fclk_khz : 1200000;
	new_clocks->fclk_khz = 4320000;

	if (should_set_clock(safe_to_lower, new_clocks->phyclk_khz, clk_mgr->clks.phyclk_khz)) {
		clk_mgr->clks.phyclk_khz = new_clocks->phyclk_khz;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr->clks.dcfclk_khz)) {
		clk_mgr->clks.dcfclk_khz = new_clocks->dcfclk_khz;
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
	}

	if (should_set_clock(safe_to_lower, new_clocks->socclk_khz, clk_mgr->clks.socclk_khz)) {
		clk_mgr->clks.socclk_khz = new_clocks->socclk_khz;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dramclk_khz, clk_mgr->clks.dramclk_khz)) {
		clk_mgr->clks.dramclk_khz = new_clocks->dramclk_khz;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr->clks.dppclk_khz)) {
		clk_mgr->clks.dppclk_khz = new_clocks->dppclk_khz;
	}

	if (should_set_clock(safe_to_lower, fclk_adj, clk_mgr->clks.fclk_khz)) {
		clk_mgr->clks.fclk_khz = fclk_adj;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr->clks.dispclk_khz)) {
		clk_mgr->clks.dispclk_khz = new_clocks->dispclk_khz;
	}

	/* Both fclk and ref_dppclk run on the same scemi clock.
	 * So take the higher value since the DPP DTO is typically programmed
	 * such that max dppclk is 1:1 with ref_dppclk.
	 */
	if (clk_mgr->clks.fclk_khz > clk_mgr->clks.dppclk_khz)
		clk_mgr->clks.dppclk_khz = clk_mgr->clks.fclk_khz;
	if (clk_mgr->clks.dppclk_khz > clk_mgr->clks.fclk_khz)
		clk_mgr->clks.fclk_khz = clk_mgr->clks.dppclk_khz;

	// Both fclk and ref_dppclk run on the same scemi clock.
	clk_mgr_int->dccg->ref_dppclk = clk_mgr->clks.fclk_khz;

	/* TODO: set dtbclk in correct place */
	clk_mgr->clks.dtbclk_en = true;
	dm_set_dcn_clocks(clk_mgr->ctx, &clk_mgr->clks);
	dcn35_update_clocks_update_dpp_dto(clk_mgr_int, context, safe_to_lower);

	dcn35_update_clocks_update_dtb_dto(clk_mgr_int, context, clk_mgr->clks.ref_dtbclk_khz);
}

static struct clk_mgr_funcs dcn35_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.get_dtb_ref_clk_frequency = dcn31_get_dtb_ref_freq_khz,
	.update_clocks = dcn35_update_clocks,
	.init_clocks = dcn35_init_clocks,
	.enable_pme_wa = dcn35_enable_pme_wa,
	.are_clock_states_equal = dcn35_are_clock_states_equal,
	.notify_wm_ranges = dcn35_notify_wm_ranges,
	.set_low_power_state = dcn35_set_low_power_state,
	.exit_low_power_state = dcn35_exit_low_power_state,
	.is_ips_supported = dcn35_is_ips_supported,
};

struct clk_mgr_funcs dcn35_fpga_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = dcn35_update_clocks_fpga,
	.init_clocks = dcn35_init_clocks_fpga,
	.get_dtb_ref_clk_frequency = dcn31_get_dtb_ref_freq_khz,
};

static void translate_to_DpmClocks_t_dcn35(struct dcn351_smu_dpm_clks *smu_dpm_clks_a,
		struct dcn35_smu_dpm_clks *smu_dpm_clks_b)
{
	/*translate two structures and only take need clock tables*/
	uint8_t i;

	if (smu_dpm_clks_a == NULL || smu_dpm_clks_b == NULL ||
		smu_dpm_clks_a->dpm_clks == NULL || smu_dpm_clks_b->dpm_clks == NULL)
		return;

	for (i = 0; i < NUM_DCFCLK_DPM_LEVELS; i++)
		smu_dpm_clks_b->dpm_clks->DcfClocks[i] = smu_dpm_clks_a->dpm_clks->DcfClocks[i];

	for (i = 0; i < NUM_DISPCLK_DPM_LEVELS; i++)
		smu_dpm_clks_b->dpm_clks->DispClocks[i] = smu_dpm_clks_a->dpm_clks->DispClocks[i];

	for (i = 0; i < NUM_DPPCLK_DPM_LEVELS; i++)
		smu_dpm_clks_b->dpm_clks->DppClocks[i] = smu_dpm_clks_a->dpm_clks->DppClocks[i];

	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++) {
		smu_dpm_clks_b->dpm_clks->FclkClocks_Freq[i] = smu_dpm_clks_a->dpm_clks->FclkClocks_Freq[i];
		smu_dpm_clks_b->dpm_clks->FclkClocks_Voltage[i] = smu_dpm_clks_a->dpm_clks->FclkClocks_Voltage[i];
	}
	for (i = 0; i < NUM_MEM_PSTATE_LEVELS; i++) {
		smu_dpm_clks_b->dpm_clks->MemPstateTable[i].MemClk =
			smu_dpm_clks_a->dpm_clks->MemPstateTable[i].MemClk;
		smu_dpm_clks_b->dpm_clks->MemPstateTable[i].UClk =
			smu_dpm_clks_a->dpm_clks->MemPstateTable[i].UClk;
		smu_dpm_clks_b->dpm_clks->MemPstateTable[i].Voltage =
			smu_dpm_clks_a->dpm_clks->MemPstateTable[i].Voltage;
		smu_dpm_clks_b->dpm_clks->MemPstateTable[i].WckRatio =
			smu_dpm_clks_a->dpm_clks->MemPstateTable[i].WckRatio;
	}
	smu_dpm_clks_b->dpm_clks->MaxGfxClk = smu_dpm_clks_a->dpm_clks->MaxGfxClk;
	smu_dpm_clks_b->dpm_clks->MinGfxClk = smu_dpm_clks_a->dpm_clks->MinGfxClk;
	smu_dpm_clks_b->dpm_clks->NumDcfClkLevelsEnabled =
		smu_dpm_clks_a->dpm_clks->NumDcfClkLevelsEnabled;
	smu_dpm_clks_b->dpm_clks->NumDispClkLevelsEnabled =
		smu_dpm_clks_a->dpm_clks->NumDispClkLevelsEnabled;
	smu_dpm_clks_b->dpm_clks->NumFclkLevelsEnabled =
		smu_dpm_clks_a->dpm_clks->NumFclkLevelsEnabled;
	smu_dpm_clks_b->dpm_clks->NumMemPstatesEnabled =
		smu_dpm_clks_a->dpm_clks->NumMemPstatesEnabled;
	smu_dpm_clks_b->dpm_clks->NumSocClkLevelsEnabled =
		smu_dpm_clks_a->dpm_clks->NumSocClkLevelsEnabled;

	for (i = 0; i < NUM_SOC_VOLTAGE_LEVELS; i++) {
		smu_dpm_clks_b->dpm_clks->SocClocks[i] = smu_dpm_clks_a->dpm_clks->SocClocks[i];
		smu_dpm_clks_b->dpm_clks->SocVoltage[i] = smu_dpm_clks_a->dpm_clks->SocVoltage[i];
	}
}
void dcn35_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_dcn35 *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	struct dcn35_smu_dpm_clks smu_dpm_clks = { 0 };
	struct dcn351_smu_dpm_clks smu_dpm_clks_dcn351 = { 0 };
	clk_mgr->base.base.ctx = ctx;
	clk_mgr->base.base.funcs = &dcn35_funcs;

	clk_mgr->base.pp_smu = pp_smu;

	clk_mgr->base.dccg = dccg;
	clk_mgr->base.dfs_bypass_disp_clk = 0;

	clk_mgr->base.dprefclk_ss_percentage = 0;
	clk_mgr->base.dprefclk_ss_divider = 1000;
	clk_mgr->base.ss_on_dprefclk = false;
	clk_mgr->base.dfs_ref_freq_khz = 48000;

	clk_mgr->smu_wm_set.wm_set = (struct dcn35_watermarks *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_GART,
				sizeof(struct dcn35_watermarks),
				&clk_mgr->smu_wm_set.mc_address.quad_part);

	if (!clk_mgr->smu_wm_set.wm_set) {
		clk_mgr->smu_wm_set.wm_set = &dummy_wms;
		clk_mgr->smu_wm_set.mc_address.quad_part = 0;
	}
	ASSERT(clk_mgr->smu_wm_set.wm_set);

	smu_dpm_clks.dpm_clks = (DpmClocks_t_dcn35 *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_GART,
				sizeof(DpmClocks_t_dcn35),
				&smu_dpm_clks.mc_address.quad_part);
	if (smu_dpm_clks.dpm_clks == NULL) {
		smu_dpm_clks.dpm_clks = &dummy_clocks;
		smu_dpm_clks.mc_address.quad_part = 0;
	}
	ASSERT(smu_dpm_clks.dpm_clks);

	if (ctx->dce_version == DCN_VERSION_3_51) {
		smu_dpm_clks_dcn351.dpm_clks = (DpmClocks_t_dcn351 *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_GART,
				sizeof(DpmClocks_t_dcn351),
				&smu_dpm_clks_dcn351.mc_address.quad_part);
		if (smu_dpm_clks_dcn351.dpm_clks == NULL) {
			smu_dpm_clks_dcn351.dpm_clks = &dummy_clocks_dcn351;
			smu_dpm_clks_dcn351.mc_address.quad_part = 0;
		}
	}

	clk_mgr->base.smu_ver = dcn35_smu_get_smu_version(&clk_mgr->base);

	if (clk_mgr->base.smu_ver)
		clk_mgr->base.smu_present = true;

	/* TODO: Check we get what we expect during bringup */
	clk_mgr->base.base.dentist_vco_freq_khz = get_vco_frequency_from_reg(&clk_mgr->base);

	if (ctx->dc_bios->integrated_info->memory_type == LpDdr5MemType) {
		dcn35_bw_params.wm_table = lpddr5_wm_table;
	} else {
		dcn35_bw_params.wm_table = ddr5_wm_table;
	}
	/* Saved clocks configured at boot for debug purposes */
	dcn35_dump_clk_registers(&clk_mgr->base.base.boot_snapshot, clk_mgr);

	clk_mgr->base.base.dprefclk_khz = dcn35_smu_get_dprefclk(&clk_mgr->base);
	clk_mgr->base.base.clks.ref_dtbclk_khz = 600000;

	dce_clock_read_ss_info(&clk_mgr->base);
	/*when clk src is from FCH, it could have ss, same clock src as DPREF clk*/

	dcn35_read_ss_info_from_lut(&clk_mgr->base);

	clk_mgr->base.base.bw_params = &dcn35_bw_params;

	if (clk_mgr->base.base.ctx->dc->debug.pstate_enabled) {
		int i;
		if (ctx->dce_version == DCN_VERSION_3_51) {
			dcn351_get_dpm_table_from_smu(&clk_mgr->base, &smu_dpm_clks_dcn351);
			translate_to_DpmClocks_t_dcn35(&smu_dpm_clks_dcn351, &smu_dpm_clks);
		} else
			dcn35_get_dpm_table_from_smu(&clk_mgr->base, &smu_dpm_clks);
		DC_LOG_SMU("NumDcfClkLevelsEnabled: %d\n"
				   "NumDispClkLevelsEnabled: %d\n"
				   "NumSocClkLevelsEnabled: %d\n"
				   "VcnClkLevelsEnabled: %d\n"
				   "FClkLevelsEnabled: %d\n"
				   "NumMemPstatesEnabled: %d\n"
				   "MinGfxClk: %d\n"
				   "MaxGfxClk: %d\n",
				   smu_dpm_clks.dpm_clks->NumDcfClkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->NumDispClkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->NumSocClkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->VcnClkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->NumFclkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->NumMemPstatesEnabled,
				   smu_dpm_clks.dpm_clks->MinGfxClk,
				   smu_dpm_clks.dpm_clks->MaxGfxClk);
		for (i = 0; i < smu_dpm_clks.dpm_clks->NumDcfClkLevelsEnabled; i++) {
			DC_LOG_SMU("smu_dpm_clks.dpm_clks->DcfClocks[%d] = %d\n",
					   i,
					   smu_dpm_clks.dpm_clks->DcfClocks[i]);
		}
		for (i = 0; i < smu_dpm_clks.dpm_clks->NumDispClkLevelsEnabled; i++) {
			DC_LOG_SMU("smu_dpm_clks.dpm_clks->DispClocks[%d] = %d\n",
					   i, smu_dpm_clks.dpm_clks->DispClocks[i]);
		}
		for (i = 0; i < smu_dpm_clks.dpm_clks->NumSocClkLevelsEnabled; i++) {
			DC_LOG_SMU("smu_dpm_clks.dpm_clks->SocClocks[%d] = %d\n",
					   i, smu_dpm_clks.dpm_clks->SocClocks[i]);
		}
		for (i = 0; i < smu_dpm_clks.dpm_clks->NumFclkLevelsEnabled; i++) {
			DC_LOG_SMU("smu_dpm_clks.dpm_clks->FclkClocks_Freq[%d] = %d\n",
					   i, smu_dpm_clks.dpm_clks->FclkClocks_Freq[i]);
			DC_LOG_SMU("smu_dpm_clks.dpm_clks->FclkClocks_Voltage[%d] = %d\n",
					   i, smu_dpm_clks.dpm_clks->FclkClocks_Voltage[i]);
		}
		for (i = 0; i < smu_dpm_clks.dpm_clks->NumSocClkLevelsEnabled; i++)
			DC_LOG_SMU("smu_dpm_clks.dpm_clks->SocVoltage[%d] = %d\n",
					   i, smu_dpm_clks.dpm_clks->SocVoltage[i]);

		for (i = 0; i < smu_dpm_clks.dpm_clks->NumMemPstatesEnabled; i++) {
			DC_LOG_SMU("smu_dpm_clks.dpm_clks.MemPstateTable[%d].UClk = %d\n"
					   "smu_dpm_clks.dpm_clks->MemPstateTable[%d].MemClk= %d\n"
					   "smu_dpm_clks.dpm_clks->MemPstateTable[%d].Voltage = %d\n",
					   i, smu_dpm_clks.dpm_clks->MemPstateTable[i].UClk,
					   i, smu_dpm_clks.dpm_clks->MemPstateTable[i].MemClk,
					   i, smu_dpm_clks.dpm_clks->MemPstateTable[i].Voltage);
		}

		if (ctx->dc_bios->integrated_info && ctx->dc->config.use_default_clock_table == false) {
			dcn35_clk_mgr_helper_populate_bw_params(
					&clk_mgr->base,
					ctx->dc_bios->integrated_info,
					smu_dpm_clks.dpm_clks);
		}
	}

	if (smu_dpm_clks.dpm_clks && smu_dpm_clks.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr->base.base.ctx, DC_MEM_ALLOC_TYPE_GART,
				smu_dpm_clks.dpm_clks);

	if (smu_dpm_clks_dcn351.dpm_clks && smu_dpm_clks_dcn351.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr->base.base.ctx, DC_MEM_ALLOC_TYPE_GART,
				smu_dpm_clks_dcn351.dpm_clks);

	if (ctx->dc->config.disable_ips != DMUB_IPS_DISABLE_ALL) {
		bool ips_support = false;

		/*avoid call pmfw at init*/
		ips_support = dcn35_smu_get_ips_supported(&clk_mgr->base);
		if (ips_support) {
			ctx->dc->debug.ignore_pg = false;
			ctx->dc->debug.disable_dpp_power_gate = false;
			ctx->dc->debug.disable_hubp_power_gate = false;
			ctx->dc->debug.disable_dsc_power_gate = false;

			/* Disable dynamic IPS2 in older PMFW (93.12) for Z8 interop. */
			if (ctx->dc->config.disable_ips == DMUB_IPS_ENABLE &&
			    ctx->dce_version == DCN_VERSION_3_5 &&
			    ((clk_mgr->base.smu_ver & 0x00FFFFFF) <= 0x005d0c00))
				ctx->dc->config.disable_ips = DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF;
		} else {
			/*let's reset the config control flag*/
			ctx->dc->config.disable_ips = DMUB_IPS_DISABLE_ALL; /*pmfw not support it, disable it all*/
		}
	}
}

void dcn35_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr_int)
{
	struct clk_mgr_dcn35 *clk_mgr = TO_CLK_MGR_DCN35(clk_mgr_int);

	if (clk_mgr->smu_wm_set.wm_set && clk_mgr->smu_wm_set.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr_int->base.ctx, DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				clk_mgr->smu_wm_set.wm_set);
}
