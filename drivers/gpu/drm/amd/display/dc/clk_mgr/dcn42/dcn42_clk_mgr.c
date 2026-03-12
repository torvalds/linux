// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dcn42_clk_mgr.h"

#include "dccg.h"
#include "clk_mgr_internal.h"

// For dce12_get_dp_ref_freq_khz
#include "dce100/dce_clk_mgr.h"

// For dcn20_update_clocks_update_dpp_dto
#include "dcn20/dcn20_clk_mgr.h"




#include "reg_helper.h"
#include "core_types.h"
#include "dcn42_smu.h"
#include "dm_helpers.h"

/* TODO: remove this include once we ported over remaining clk mgr functions*/
#include "dcn30/dcn30_clk_mgr.h"
#include "dcn31/dcn31_clk_mgr.h"

#include "dcn35/dcn35_clk_mgr.h"

#include "dc_dmub_srv.h"
#include "link_service.h"
#include "logger_types.h"


#undef DC_LOGGER
#define DC_LOGGER \
	clk_mgr->base.base.ctx->logger


#define DCN_BASE__INST0_SEG1                       0x000000C0

#define regCLK8_CLK2_BYPASS_CNTL			 0x4c2a
#define regCLK8_CLK2_BYPASS_CNTL_BASE_IDX	0
#define CLK8_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL__SHIFT	0x0
#define CLK8_CLK2_BYPASS_CNTL__CLK2_BYPASS_DIV__SHIFT	0x10
#define CLK8_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL_MASK		0x00000007L
#define CLK8_CLK2_BYPASS_CNTL__CLK2_BYPASS_DIV_MASK		0x000F0000L

#define regDENTIST_DISPCLK_CNTL 0x0064
#define regDENTIST_DISPCLK_CNTL_BASE_IDX 1

// DENTIST_DISPCLK_CNTL
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_WDIVIDER__SHIFT 0x0
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_RDIVIDER__SHIFT 0x8
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_CHG_DONE__SHIFT 0x13
#define DENTIST_DISPCLK_CNTL__DENTIST_DPPCLK_CHG_DONE__SHIFT 0x14
#define DENTIST_DISPCLK_CNTL__DENTIST_DPPCLK_WDIVIDER__SHIFT 0x18
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_WDIVIDER_MASK 0x0000007FL
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_RDIVIDER_MASK 0x00007F00L
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_CHG_DONE_MASK 0x00080000L
#define DENTIST_DISPCLK_CNTL__DENTIST_DPPCLK_CHG_DONE_MASK 0x00100000L
#define DENTIST_DISPCLK_CNTL__DENTIST_DPPCLK_WDIVIDER_MASK 0x7F000000L
#define mmDENTIST_DISPCLK_CNTL 0x0124
#define mmCLK8_CLK_TICK_CNT_CONFIG_REG                  0x1B851
#define mmCLK8_CLK0_CURRENT_CNT                         0x1B853
#define mmCLK8_CLK1_CURRENT_CNT                         0x1B854
#define mmCLK8_CLK2_CURRENT_CNT                         0x1B855
#define mmCLK8_CLK3_CURRENT_CNT                         0x1B856
#define mmCLK8_CLK4_CURRENT_CNT                         0x1B857


#define mmCLK8_CLK0_BYPASS_CNTL                         0x1B81A
#define mmCLK8_CLK1_BYPASS_CNTL                         0x1B822
#define mmCLK8_CLK2_BYPASS_CNTL                         0x1B82A
#define mmCLK8_CLK3_BYPASS_CNTL                         0x1B832
#define mmCLK8_CLK4_BYPASS_CNTL                         0x1B83A


#define mmCLK8_CLK0_DS_CNTL                             0x1B814
#define mmCLK8_CLK1_DS_CNTL                             0x1B81C
#define mmCLK8_CLK2_DS_CNTL                             0x1B824
#define mmCLK8_CLK3_DS_CNTL                             0x1B82C
#define mmCLK8_CLK4_DS_CNTL                             0x1B834




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

#define CLK_SR_DCN42(reg_name)\
	.reg_name = mm ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn42 = {
	CLK_REG_LIST_DCN42()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn42 = {
	CLK_COMMON_MASK_SH_LIST_DCN42(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn42 = {
	CLK_COMMON_MASK_SH_LIST_DCN42(_MASK)
};



#define TO_CLK_MGR_DCN42(clk_mgr_int)\
	container_of(clk_mgr_int, struct clk_mgr_dcn42, base)

int dcn42_get_active_display_cnt_wa(
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

void dcn42_update_clocks_update_dtb_dto(struct clk_mgr_internal *clk_mgr,
		struct dc_state *context,
		int ref_dtbclk_khz)
{
	/* DCN42 does not implement set_dtbclk_dto function, so this is a no-op */
}

void dcn42_update_clocks_update_dpp_dto(struct clk_mgr_internal *clk_mgr,
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

void dcn42_update_clocks(struct clk_mgr *clk_mgr_base,
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

	display_count = dcn42_get_active_display_cnt_wa(dc, context, &all_active_disps);

	/*dml21 issue*/
	ASSERT(new_clocks->dtbclk_en && new_clocks->ref_dtbclk_khz > 590000); //remove this section if assert is hit
	if (new_clocks->dtbclk_en && new_clocks->ref_dtbclk_khz < 590000)
		new_clocks->ref_dtbclk_khz = 600000;

	/*
	 * if it is safe to lower, but we are already in the lower state, we don't have to do anything
	 * also if safe to lower is false, we just go in the higher state
	 */
	if (safe_to_lower) {
		if (new_clocks->zstate_support != DCN_ZSTATE_SUPPORT_DISALLOW &&
				new_clocks->zstate_support != clk_mgr_base->clks.zstate_support) {
			dcn42_smu_set_zstate_support(clk_mgr, new_clocks->zstate_support);
			clk_mgr_base->clks.zstate_support = new_clocks->zstate_support;
		}

		if (clk_mgr_base->clks.dtbclk_en && !new_clocks->dtbclk_en) {
			if (clk_mgr->base.ctx->dc->config.allow_0_dtb_clk)
				dcn42_smu_set_dtbclk(clk_mgr, false);
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
			dcn42_smu_set_zstate_support(clk_mgr, DCN_ZSTATE_SUPPORT_DISALLOW);
			clk_mgr_base->clks.zstate_support = new_clocks->zstate_support;
		}
		if (!clk_mgr_base->clks.dtbclk_en && new_clocks->dtbclk_en) {
			int actual_dtbclk = 0;

			dcn42_update_clocks_update_dtb_dto(clk_mgr, context, new_clocks->ref_dtbclk_khz);
			dcn42_smu_set_dtbclk(clk_mgr, true);
			if (clk_mgr_base->boot_snapshot.timer_threhold)
				actual_dtbclk = REG_READ(CLK8_CLK4_CURRENT_CNT) / (clk_mgr_base->boot_snapshot.timer_threhold / 48000);


			if (actual_dtbclk > 590000) {
				clk_mgr_base->clks.ref_dtbclk_khz = new_clocks->ref_dtbclk_khz;
				clk_mgr_base->clks.dtbclk_en = new_clocks->dtbclk_en;
			}
		}

		/* check that we're not already in D0 */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_MISSION_MODE) {
			union display_idle_optimization_u idle_info = { 0 };

			dcn42_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
			/* update power state */
			clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_MISSION_MODE;
		}
	}
	if (dc->debug.force_min_dcfclk_mhz > 0)
		new_clocks->dcfclk_khz = (new_clocks->dcfclk_khz > (dc->debug.force_min_dcfclk_mhz * 1000)) ?
				new_clocks->dcfclk_khz : (dc->debug.force_min_dcfclk_mhz * 1000);

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		clk_mgr_base->clks.fclk_khz = new_clocks->fclk_khz;
		clk_mgr_base->clks.dramclk_khz = new_clocks->dramclk_khz;
		dcn42_smu_set_hard_min_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_khz);
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		dcn42_smu_set_min_deep_sleep_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_deep_sleep_khz);
	}

	// workaround: Limit dppclk to 100Mhz to avoid lower eDP panel switch to plus 4K monitor underflow.

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr->base.clks.dppclk_khz)) {
		if (clk_mgr->base.clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;
		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz) &&
	    (new_clocks->dispclk_khz > 0 || (safe_to_lower && display_count == 0))) {
		int requested_dispclk_khz = new_clocks->dispclk_khz;

		dcn35_disable_otg_wa(clk_mgr_base, context, safe_to_lower, true);

		/* Clamp the requested clock to PMFW based on their limit. */
		if (dc->debug.min_disp_clk_khz > 0 && requested_dispclk_khz < dc->debug.min_disp_clk_khz)
			requested_dispclk_khz = dc->debug.min_disp_clk_khz;

		dcn42_smu_set_dispclk(clk_mgr, requested_dispclk_khz);
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;
		dcn35_disable_otg_wa(clk_mgr_base, context, safe_to_lower, false);

		update_dispclk = true;
	}

	/* clock limits are received with MHz precision, divide by 1000 to prevent setting clocks at every call */
	if (!dc->debug.disable_dtb_ref_clk_switch &&
	    should_set_clock(safe_to_lower, new_clocks->ref_dtbclk_khz / 1000,
			     clk_mgr_base->clks.ref_dtbclk_khz / 1000)) {
		dcn42_update_clocks_update_dtb_dto(clk_mgr, context, new_clocks->ref_dtbclk_khz);
		clk_mgr_base->clks.ref_dtbclk_khz = new_clocks->ref_dtbclk_khz;
	}

	if (dpp_clock_lowered) {
		// increase per DPP DTO before lowering global dppclk
		dcn42_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		dcn42_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
	} else {
		// increase global DPPCLK before lowering per DPP DTO
		if (update_dppclk || update_dispclk)
			dcn42_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
		dcn42_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
	}
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


void dcn42_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	dcn42_smu_enable_pme_wa(clk_mgr);
}


bool dcn42_are_clock_states_equal(struct dc_clocks *a,
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

static void dcn42_dump_clk_registers_internal(struct dcn42_clk_internal *internal, struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint32_t ratio = 1;

	internal->CLK8_CLK_TICK_CNT__TIMER_THRESHOLD = REG_READ(CLK8_CLK_TICK_CNT_CONFIG_REG) & 0xFFFFFF;

	ratio = internal->CLK8_CLK_TICK_CNT__TIMER_THRESHOLD / 48000;
	ASSERT(ratio != 0);

	if (ratio) {
		// read dcf deep sleep divider
		internal->CLK8_CLK0_DS_CNTL = REG_READ(CLK8_CLK0_DS_CNTL);
		internal->CLK8_CLK3_DS_CNTL = REG_READ(CLK8_CLK3_DS_CNTL);
		// read dispclk
		internal->CLK8_CLK0_CURRENT_CNT = REG_READ(CLK8_CLK0_CURRENT_CNT) / ratio;
		internal->CLK8_CLK0_BYPASS_CNTL = REG_READ(CLK8_CLK0_BYPASS_CNTL);
		// read dppclk
		internal->CLK8_CLK1_CURRENT_CNT = REG_READ(CLK8_CLK1_CURRENT_CNT) / ratio;
		internal->CLK8_CLK1_BYPASS_CNTL = REG_READ(CLK8_CLK1_BYPASS_CNTL);
		// read dprefclk
		internal->CLK8_CLK2_CURRENT_CNT = REG_READ(CLK8_CLK2_CURRENT_CNT) / ratio;
		internal->CLK8_CLK2_BYPASS_CNTL = REG_READ(CLK8_CLK2_BYPASS_CNTL);
		// read dcfclk
		internal->CLK8_CLK3_CURRENT_CNT = REG_READ(CLK8_CLK3_CURRENT_CNT) / ratio;
		internal->CLK8_CLK3_BYPASS_CNTL = REG_READ(CLK8_CLK3_BYPASS_CNTL);
		// read dtbclk
		internal->CLK8_CLK4_CURRENT_CNT = REG_READ(CLK8_CLK4_CURRENT_CNT) / ratio;
		internal->CLK8_CLK4_BYPASS_CNTL = REG_READ(CLK8_CLK4_BYPASS_CNTL);
	}

}

static void dcn42_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr_dcn42 *clk_mgr)
{
	struct dcn42_clk_internal internal = {0};
	char *bypass_clks[5] = {"0x0 DFS", "0x1 REFCLK", "0x2 ERROR", "0x3 400 FCH", "0x4 600 FCH"};

	dcn42_dump_clk_registers_internal(&internal, &clk_mgr->base.base);
	regs_and_bypass->timer_threhold = internal.CLK8_CLK_TICK_CNT__TIMER_THRESHOLD;
	regs_and_bypass->dcfclk = internal.CLK8_CLK3_CURRENT_CNT / 10;
	regs_and_bypass->dcf_deep_sleep_divider = internal.CLK8_CLK3_DS_CNTL / 10;
	regs_and_bypass->dcf_deep_sleep_allow = internal.CLK8_CLK3_DS_CNTL & 0x10; /*bit 4: CLK0_ALLOW_DS*/
	regs_and_bypass->dprefclk = internal.CLK8_CLK2_CURRENT_CNT / 10;
	regs_and_bypass->dispclk = internal.CLK8_CLK0_CURRENT_CNT / 10;
	regs_and_bypass->dppclk = internal.CLK8_CLK1_CURRENT_CNT / 10;
	regs_and_bypass->dtbclk = internal.CLK8_CLK4_CURRENT_CNT / 10;

	regs_and_bypass->dppclk_bypass = internal.CLK8_CLK1_BYPASS_CNTL & 0x0007;
	if (regs_and_bypass->dppclk_bypass > 4)
		regs_and_bypass->dppclk_bypass = 0;
	regs_and_bypass->dcfclk_bypass = internal.CLK8_CLK3_BYPASS_CNTL & 0x0007;
	if (regs_and_bypass->dcfclk_bypass > 4)
		regs_and_bypass->dcfclk_bypass = 0;
	regs_and_bypass->dispclk_bypass = internal.CLK8_CLK0_BYPASS_CNTL & 0x0007;
	if (regs_and_bypass->dispclk_bypass > 4)
		regs_and_bypass->dispclk_bypass = 0;
	regs_and_bypass->dprefclk_bypass = internal.CLK8_CLK2_BYPASS_CNTL & 0x0007;
	if (regs_and_bypass->dprefclk_bypass > 4)
		regs_and_bypass->dprefclk_bypass = 0;

	if (clk_mgr->base.base.ctx->dc->debug.pstate_enabled) {
		DC_LOG_SMU("clk_type,clk_value,deepsleep_cntl,deepsleep_allow,bypass\n");

		DC_LOG_SMU("dcfclk,%d,%d,%d,%s\n",
				   regs_and_bypass->dcfclk,
				   regs_and_bypass->dcf_deep_sleep_divider,
				   regs_and_bypass->dcf_deep_sleep_allow,
				   bypass_clks[(int) regs_and_bypass->dcfclk_bypass]);

		DC_LOG_SMU("dprefclk,%d,N/A,N/A,%s\n",
			regs_and_bypass->dprefclk,
			bypass_clks[(int) regs_and_bypass->dprefclk_bypass]);

		DC_LOG_SMU("dispclk,%d,N/A,N/A,%s\n",
			regs_and_bypass->dispclk,
			bypass_clks[(int) regs_and_bypass->dispclk_bypass]);

		//split
		DC_LOG_SMU("SPLIT\n");

		// REGISTER VALUES
		DC_LOG_SMU("reg_name,value,clk_type\n");

		DC_LOG_SMU("CLK1_CLK3_CURRENT_CNT,%d,dcfclk\n",
				internal.CLK8_CLK3_CURRENT_CNT);

		DC_LOG_SMU("CLK1_CLK3_DS_CNTL,%d,dcf_deep_sleep_divider\n",
					internal.CLK8_CLK3_DS_CNTL);

		DC_LOG_SMU("CLK1_CLK3_ALLOW_DS,%d,dcf_deep_sleep_allow\n",
					(internal.CLK8_CLK3_DS_CNTL & 0x10));

		DC_LOG_SMU("CLK1_CLK2_CURRENT_CNT,%d,dprefclk\n",
					internal.CLK8_CLK2_CURRENT_CNT);

		DC_LOG_SMU("CLK1_CLK0_CURRENT_CNT,%d,dispclk\n",
					internal.CLK8_CLK0_CURRENT_CNT);

		DC_LOG_SMU("CLK1_CLK1_CURRENT_CNT,%d,dppclk\n",
					internal.CLK8_CLK1_CURRENT_CNT);

		DC_LOG_SMU("CLK1_CLK3_BYPASS_CNTL,%d,dcfclk_bypass\n",
					internal.CLK8_CLK3_BYPASS_CNTL);

		DC_LOG_SMU("CLK1_CLK2_BYPASS_CNTL,%d,dprefclk_bypass\n",
					internal.CLK8_CLK2_BYPASS_CNTL);

		DC_LOG_SMU("CLK1_CLK0_BYPASS_CNTL,%d,dispclk_bypass\n",
					internal.CLK8_CLK0_BYPASS_CNTL);

		DC_LOG_SMU("CLK1_CLK1_BYPASS_CNTL,%d,dppclk_bypass\n",
					internal.CLK8_CLK1_BYPASS_CNTL);
	}
}

bool dcn42_is_spll_ssc_enabled(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc_context *ctx = clk_mgr->base.ctx;


	if (ctx->dc->config.ignore_dpref_ss) {
		/*revert bios's ss info for test only*/
		return (clk_mgr->dprefclk_ss_percentage == 0);
	}
	/*need to update after BU*/
	return false;
}

static void init_clk_states(struct clk_mgr *clk_mgr)
{
	uint32_t ref_dtbclk = clk_mgr->clks.ref_dtbclk_khz;

	memset(&(clk_mgr->clks), 0, sizeof(struct dc_clocks));

	clk_mgr->clks.dtbclk_en = true; // request DTBCLK disable on first commit
	clk_mgr->clks.ref_dtbclk_khz = ref_dtbclk;      // restore ref_dtbclk
	clk_mgr->clks.p_state_change_support = true;
	clk_mgr->clks.prev_p_state_change_support = true;
	clk_mgr->clks.pwr_state = DCN_PWR_STATE_UNKNOWN;
	clk_mgr->clks.zstate_support = DCN_ZSTATE_SUPPORT_UNKNOWN;
}

static void dcn42_get_dpm_table_from_smu(struct clk_mgr_internal *clk_mgr,
		struct dcn42_smu_dpm_clks *smu_dpm_clks)
{
	DpmClocks_t_dcn42 *table = smu_dpm_clks->dpm_clks;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || smu_dpm_clks->mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn42_smu_set_dram_addr_high(clk_mgr,
			smu_dpm_clks->mc_address.high_part);
	dcn42_smu_set_dram_addr_low(clk_mgr,
			smu_dpm_clks->mc_address.low_part);
	dcn42_smu_transfer_dpm_table_smu_2_dram(clk_mgr);
}

void dcn42_init_single_clock(unsigned int *entry_0,
		uint32_t *smu_entry_0,
		uint8_t num_levels)
{
	int i;
	char *entry_i = (char *)entry_0;

	ASSERT(num_levels <= MAX_NUM_DPM_LVL);
	if (num_levels > MAX_NUM_DPM_LVL)
		num_levels = MAX_NUM_DPM_LVL;


	for (i = 0; i < num_levels; i++) {
		*((unsigned int *)entry_i) = smu_entry_0[i];
		entry_i += sizeof(struct clk_limit_table_entry);
	}
}

unsigned int dcn42_convert_wck_ratio(uint8_t wck_ratio)
{
	switch (wck_ratio) {
	case WCK_RATIO_1_2:
		return 2;

	case WCK_RATIO_1_4:
		return 4;

	default:
			break;
	}

	return 1;
}

void dcn42_init_clocks(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr_int = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_mgr_dcn42 *clk_mgr = TO_CLK_MGR_DCN42(clk_mgr_int);
	struct dcn42_smu_dpm_clks smu_dpm_clks = { 0 };

	init_clk_states(clk_mgr_base);

	// to adjust dp_dto reference clock if ssc is enable otherwise to apply dprefclk
	if (dcn42_is_spll_ssc_enabled(clk_mgr_base))
		clk_mgr_base->dp_dto_source_clock_in_khz =
			dce_adjust_dp_ref_freq_for_ss(clk_mgr_int, clk_mgr_base->dprefclk_khz);
	else
		clk_mgr_base->dp_dto_source_clock_in_khz = clk_mgr_base->dprefclk_khz;

	dcn42_dump_clk_registers(&clk_mgr_base->boot_snapshot, clk_mgr);

	clk_mgr_base->clks.ref_dtbclk_khz =  clk_mgr_base->boot_snapshot.dtbclk * 10;
	if (clk_mgr_base->boot_snapshot.dtbclk > 59000) {
		/*dtbclk enabled based on*/
		clk_mgr_base->clks.dtbclk_en = true;
	}

	smu_dpm_clks.dpm_clks = (DpmClocks_t_dcn42 *)dm_helpers_allocate_gpu_mem(
				clk_mgr_base->ctx,
				DC_MEM_ALLOC_TYPE_GART,
				sizeof(DpmClocks_t_dcn42),
				&smu_dpm_clks.mc_address.quad_part);

	ASSERT(smu_dpm_clks.dpm_clks);
	if (clk_mgr_base->ctx->dc->debug.pstate_enabled && clk_mgr_int->smu_present && smu_dpm_clks.mc_address.quad_part != 0) {
		int i;
		DpmClocks_t_dcn42 *dpm_clks = smu_dpm_clks.dpm_clks;

		dcn42_get_dpm_table_from_smu(clk_mgr_int, &smu_dpm_clks);
		DC_LOG_SMU("NumDcfClkLevelsEnabled: %d\n"
				   "NumDispClkLevelsEnabled: %d\n"
				   "NumSocClkLevelsEnabled: %d\n"
				   "VcnClkLevelsEnabled: %d\n"
				   "FClkLevelsEnabled: %d\n"
				   "NumMemPstatesEnabled: %d\n"
				   "MinGfxClk: %d\n"
				   "MaxGfxClk: %d\n",
				   dpm_clks->NumDcfClkLevelsEnabled,
				   dpm_clks->NumDispClkLevelsEnabled,
				   dpm_clks->NumSocClkLevelsEnabled,
				   dpm_clks->VcnClkLevelsEnabled,
				   dpm_clks->NumFclkLevelsEnabled,
				   dpm_clks->NumMemPstatesEnabled,
				   dpm_clks->MinGfxClk,
				   dpm_clks->MaxGfxClk);

		for (i = 0; i < NUM_DCFCLK_DPM_LEVELS; i++) {
			DC_LOG_SMU("dpm_clks->DcfClocks[%d] = %d\n",
					   i,
					   dpm_clks->DcfClocks[i]);
		}
		for (i = 0; i < NUM_DISPCLK_DPM_LEVELS; i++) {
			DC_LOG_SMU("dpm_clks->DispClocks[%d] = %d\n",
					   i, dpm_clks->DispClocks[i]);
		}
		for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++) {
			DC_LOG_SMU("dpm_clks->SocClocks[%d] = %d\n",
					   i, dpm_clks->SocClocks[i]);
		}
		for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++) {
			DC_LOG_SMU("dpm_clks->FclkClocks_Freq[%d] = %d\n",
					   i, dpm_clks->FclkClocks_Freq[i]);
			DC_LOG_SMU("dpm_clks->FclkClocks_Voltage[%d] = %d\n",
					   i, dpm_clks->FclkClocks_Voltage[i]);
		}
		for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++)
			DC_LOG_SMU("dpm_clks->SocVoltage[%d] = %d\n",
					   i, dpm_clks->SocVoltage[i]);

		for (i = 0; i < NUM_MEM_PSTATE_LEVELS; i++) {
			DC_LOG_SMU("dpm_clks.MemPstateTable[%d].UClk = %d\n"
					   "dpm_clks->MemPstateTable[%d].MemClk= %d\n"
					   "dpm_clks->MemPstateTable[%d].Voltage = %d\n",
					   i, dpm_clks->MemPstateTable[i].UClk,
					   i, dpm_clks->MemPstateTable[i].MemClk,
					   i, dpm_clks->MemPstateTable[i].Voltage);
		}

		if (clk_mgr_base->ctx->dc_bios->integrated_info && clk_mgr_base->ctx->dc->config.use_default_clock_table == false) {
			/* DCFCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].dcfclk_mhz,
					dpm_clks->DcfClocks,
					dpm_clks->NumDcfClkLevelsEnabled);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dcfclk_levels = dpm_clks->NumDcfClkLevelsEnabled;

			/* SOCCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].socclk_mhz,
					dpm_clks->SocClocks,
					dpm_clks->NumSocClkLevelsEnabled);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_socclk_levels = dpm_clks->NumSocClkLevelsEnabled;

			/* DISPCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].dispclk_mhz,
					dpm_clks->DispClocks,
					dpm_clks->NumDispClkLevelsEnabled);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dispclk_levels = dpm_clks->NumDispClkLevelsEnabled;

			/* DPPCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].dppclk_mhz,
					dpm_clks->DppClocks,
					dpm_clks->NumDispClkLevelsEnabled);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dppclk_levels = dpm_clks->NumDispClkLevelsEnabled;

			/* FCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].fclk_mhz,
					dpm_clks->FclkClocks_Freq,
					NUM_FCLK_DPM_LEVELS);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_fclk_levels = dpm_clks->NumFclkLevelsEnabled;
			clk_mgr_base->bw_params->clk_table.num_entries = dpm_clks->NumFclkLevelsEnabled;

			/* Memory Pstate table is in reverse order*/
			ASSERT(dpm_clks->NumMemPstatesEnabled <= NUM_MEM_PSTATE_LEVELS);
			if (dpm_clks->NumMemPstatesEnabled > NUM_MEM_PSTATE_LEVELS)
				dpm_clks->NumMemPstatesEnabled = NUM_MEM_PSTATE_LEVELS;
			for (i = 0; i < dpm_clks->NumMemPstatesEnabled; i++) {
				clk_mgr_base->bw_params->clk_table.entries[dpm_clks->NumMemPstatesEnabled - 1 - i].memclk_mhz = dpm_clks->MemPstateTable[i].UClk;
				clk_mgr_base->bw_params->clk_table.entries[dpm_clks->NumMemPstatesEnabled - 1 - i].wck_ratio = dcn42_convert_wck_ratio(dpm_clks->MemPstateTable[i].WckRatio)	;
			}
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_memclk_levels = dpm_clks->NumMemPstatesEnabled;

			/* DTBCLK*/
			clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz = clk_mgr_base->clks.ref_dtbclk_khz / 1000;
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dtbclk_levels = 1;

			/* Refresh bounding box */
			clk_mgr_base->ctx->dc->res_pool->funcs->update_bw_bounding_box(
					clk_mgr_base->ctx->dc, clk_mgr_base->bw_params);
		}
	}
	if (smu_dpm_clks.dpm_clks && smu_dpm_clks.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr_base->ctx, DC_MEM_ALLOC_TYPE_GART,
				smu_dpm_clks.dpm_clks);
}

static struct clk_bw_params dcn42_bw_params = {
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

struct dcn42_ss_info_table dcn42_ss_info_table = {
	.ss_divider = 1000,
	.ss_percentage = {0, 0, 375, 375, 375}
};

static void dcn42_read_ss_info_from_lut(struct clk_mgr_internal *clk_mgr)
{
	uint32_t clock_source;

	clock_source = (REG_READ(CLK8_CLK2_BYPASS_CNTL) & CLK8_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL_MASK);
	// If it's DFS mode, clock_source is 0.
	if (dcn42_is_spll_ssc_enabled(&clk_mgr->base) && (clock_source < ARRAY_SIZE(dcn42_ss_info_table.ss_percentage))) {
		clk_mgr->dprefclk_ss_percentage = dcn42_ss_info_table.ss_percentage[clock_source];

		if (clk_mgr->dprefclk_ss_percentage != 0) {
			clk_mgr->ss_on_dprefclk = true;
			clk_mgr->dprefclk_ss_divider = dcn42_ss_info_table.ss_divider;
		}
	}
}

/* Exposed for dcn42b reuse */
void dcn42_build_watermark_ranges(struct clk_bw_params *bw_params, struct dcn42_watermarks *table)
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
			if (num_valid_sets > 0)
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

void dcn42_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_mgr_dcn42 *clk_mgr_dcn42 = TO_CLK_MGR_DCN42(clk_mgr);
	struct dcn42_watermarks *table = clk_mgr_dcn42->smu_wm_set.wm_set;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || clk_mgr_dcn42->smu_wm_set.mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn42_build_watermark_ranges(clk_mgr_base->bw_params, table);

	dcn42_smu_set_dram_addr_high(clk_mgr,
			clk_mgr_dcn42->smu_wm_set.mc_address.high_part);
	dcn42_smu_set_dram_addr_low(clk_mgr,
			clk_mgr_dcn42->smu_wm_set.mc_address.low_part);
	dcn42_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

void dcn42_set_low_power_state(struct clk_mgr *clk_mgr_base)
{
	int display_count;
	struct dc *dc = clk_mgr_base->ctx->dc;
	struct dc_state *context = dc->current_state;

	if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_LOW_POWER) {
		display_count = dcn42_get_active_display_cnt_wa(dc, context, NULL);
		/* if we can go lower, go lower */
		if (display_count == 0)
			clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_LOW_POWER;
	}

	if (clk_mgr_base->clks.pwr_state == DCN_PWR_STATE_LOW_POWER) {
		union display_idle_optimization_u idle_info = { 0 };
		struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

		idle_info.idle_info.df_request_disabled = 1;
		idle_info.idle_info.phy_ref_clk_off = 1;
		idle_info.idle_info.s0i2_rdy = 1;
		dcn42_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
	}
}

void dcn42_exit_low_power_state(struct clk_mgr *clk_mgr_base)
{

}

static void dcn42_init_clocks_fpga(struct clk_mgr *clk_mgr)
{
	init_clk_states(clk_mgr);

}

static void dcn42_update_clocks_fpga(struct clk_mgr *clk_mgr,
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

	if (should_set_clock(safe_to_lower, new_clocks->phyclk_khz, clk_mgr->clks.phyclk_khz))
		clk_mgr->clks.phyclk_khz = new_clocks->phyclk_khz;

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr->clks.dcfclk_khz))
		clk_mgr->clks.dcfclk_khz = new_clocks->dcfclk_khz;

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr->clks.dcfclk_deep_sleep_khz))
		clk_mgr->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;

	if (should_set_clock(safe_to_lower, new_clocks->socclk_khz, clk_mgr->clks.socclk_khz))
		clk_mgr->clks.socclk_khz = new_clocks->socclk_khz;

	if (should_set_clock(safe_to_lower, new_clocks->dramclk_khz, clk_mgr->clks.dramclk_khz))
		clk_mgr->clks.dramclk_khz = new_clocks->dramclk_khz;

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr->clks.dppclk_khz))
		clk_mgr->clks.dppclk_khz = new_clocks->dppclk_khz;

	if (should_set_clock(safe_to_lower, fclk_adj, clk_mgr->clks.fclk_khz))
		clk_mgr->clks.fclk_khz = fclk_adj;

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr->clks.dispclk_khz))
		clk_mgr->clks.dispclk_khz = new_clocks->dispclk_khz;

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
	dcn42_update_clocks_update_dpp_dto(clk_mgr_int, context, safe_to_lower);

	dcn42_update_clocks_update_dtb_dto(clk_mgr_int, context, clk_mgr->clks.ref_dtbclk_khz);
}

unsigned int dcn42_get_max_clock_khz(struct clk_mgr *clk_mgr_base, enum clk_type clk_type)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	unsigned int num_clk_levels;

	switch (clk_type) {
	case CLK_TYPE_DISPCLK:
		num_clk_levels = clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_dispclk_levels;
		return num_clk_levels ?
				clk_mgr->base.bw_params->clk_table.entries[num_clk_levels - 1].dispclk_mhz * 1000 :
				clk_mgr->base.boot_snapshot.dispclk;
	case CLK_TYPE_DPPCLK:
		num_clk_levels = clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_dppclk_levels;
		return num_clk_levels ?
				clk_mgr->base.bw_params->clk_table.entries[num_clk_levels - 1].dppclk_mhz * 1000 :
				clk_mgr->base.boot_snapshot.dppclk;
	case CLK_TYPE_DSCCLK:
		num_clk_levels = clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_dispclk_levels;
		return num_clk_levels ?
				clk_mgr->base.bw_params->clk_table.entries[num_clk_levels - 1].dispclk_mhz * 1000 / 3 :
				clk_mgr->base.boot_snapshot.dispclk / 3;
	default:
		break;
	}

	return 0;
}

static int dcn42_get_dispclk_from_dentist(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint32_t dispclk_wdivider;
	int disp_divider;

	REG_GET(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, &dispclk_wdivider);
	disp_divider = dentist_get_divider_from_did(dispclk_wdivider);

	/* Return DISPCLK freq in Khz */
	if (disp_divider)
		return (DENTIST_DIVIDER_RANGE_SCALE_FACTOR * clk_mgr->base.dentist_vco_freq_khz) / disp_divider;

	return 0;
}
bool dcn42_is_smu_present(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	return clk_mgr->smu_present;
}

static struct clk_mgr_funcs dcn42_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.get_dtb_ref_clk_frequency = dcn31_get_dtb_ref_freq_khz,
	.update_clocks = dcn42_update_clocks,
	.init_clocks = dcn42_init_clocks,
	.enable_pme_wa = dcn42_enable_pme_wa,
	.are_clock_states_equal = dcn42_are_clock_states_equal,
	.notify_wm_ranges = dcn42_notify_wm_ranges,
	.set_low_power_state = dcn42_set_low_power_state,
	.exit_low_power_state = dcn42_exit_low_power_state,
	.get_max_clock_khz = dcn42_get_max_clock_khz,
	.get_dispclk_from_dentist = dcn42_get_dispclk_from_dentist,
	.is_smu_present = dcn42_is_smu_present,
};

struct clk_mgr_funcs dcn42_fpga_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = dcn42_update_clocks_fpga,
	.init_clocks = dcn42_init_clocks_fpga,
	.get_dtb_ref_clk_frequency = dcn31_get_dtb_ref_freq_khz,
};

void dcn42_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_dcn42 *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	clk_mgr->base.base.ctx = ctx;
	clk_mgr->base.base.funcs = &dcn42_funcs;
	clk_mgr->base.regs = &clk_mgr_regs_dcn42;
	clk_mgr->base.clk_mgr_shift = &clk_mgr_shift_dcn42;
	clk_mgr->base.clk_mgr_mask = &clk_mgr_mask_dcn42;

	clk_mgr->base.pp_smu = pp_smu;

	clk_mgr->base.dccg = dccg;
	clk_mgr->base.dfs_bypass_disp_clk = 0;

	clk_mgr->base.dprefclk_ss_percentage = 0;
	clk_mgr->base.dprefclk_ss_divider = 1000;
	clk_mgr->base.ss_on_dprefclk = false;
	clk_mgr->base.dfs_ref_freq_khz = 48000; /*sync with pmfw*/

	clk_mgr->smu_wm_set.wm_set = (struct dcn42_watermarks *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_GART,
				sizeof(struct dcn42_watermarks),
				&clk_mgr->smu_wm_set.mc_address.quad_part);

	ASSERT(clk_mgr->smu_wm_set.wm_set);

	/* Changed from DCN3.2_clock_frequency doc to match
	 * dcn32_dump_clk_registers from 4 * dentist_vco_freq_khz /
	 * dprefclk DID divider
	 */
	clk_mgr->base.base.dprefclk_khz = 600000;

		clk_mgr->base.smu_present = false;

		if (ctx->dc_bios->integrated_info) {
			clk_mgr->base.base.dentist_vco_freq_khz = ctx->dc_bios->integrated_info->dentist_vco_freq;

			if (ctx->dc_bios->integrated_info->memory_type == LpDdr5MemType)
				dcn42_bw_params.wm_table = lpddr5_wm_table;
			else
				dcn42_bw_params.wm_table = ddr5_wm_table;
			dcn42_bw_params.vram_type = ctx->dc_bios->integrated_info->memory_type;
			dcn42_bw_params.dram_channel_width_bytes = ctx->dc_bios->integrated_info->memory_type == 0x22 ? 8 : 4;
			dcn42_bw_params.num_channels = ctx->dc_bios->integrated_info->ma_channel_number ? ctx->dc_bios->integrated_info->ma_channel_number : 4;
		}
		/* in case we don't get a value from the BIOS, use default */
		if (clk_mgr->base.base.dentist_vco_freq_khz == 0)
			clk_mgr->base.base.dentist_vco_freq_khz = 3000000; /* 3000MHz */

		/* Saved clocks configured at boot for debug purposes */
		dcn42_dump_clk_registers(&clk_mgr->base.base.boot_snapshot, clk_mgr);

	if (clk_mgr->base.smu_present)
		clk_mgr->base.base.dprefclk_khz = dcn42_smu_get_dprefclk(&clk_mgr->base);
	clk_mgr->base.base.clks.ref_dtbclk_khz = 600000;
	dce_clock_read_ss_info(&clk_mgr->base);
	/*when clk src is from FCH, it could have ss, same clock src as DPREF clk*/

	dcn42_read_ss_info_from_lut(&clk_mgr->base);

	clk_mgr->base.base.bw_params = &dcn42_bw_params;
}

void dcn42_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr_int)
{
	struct clk_mgr_dcn42 *clk_mgr = TO_CLK_MGR_DCN42(clk_mgr_int);

	if (clk_mgr->smu_wm_set.wm_set && clk_mgr->smu_wm_set.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr_int->base.ctx, DC_MEM_ALLOC_TYPE_GART,
				clk_mgr->smu_wm_set.wm_set);
}
