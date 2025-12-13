/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "dcn31/dcn31_clk_mgr.h"
#include "dcn315_clk_mgr.h"

#include "core_types.h"
#include "dcn315_smu.h"
#include "dm_helpers.h"

#include "dc_dmub_srv.h"

#include "logger_types.h"
#undef DC_LOGGER
#define DC_LOGGER \
	clk_mgr->base.base.ctx->logger

#include "link_service.h"

#define TO_CLK_MGR_DCN315(clk_mgr)\
	container_of(clk_mgr, struct clk_mgr_dcn315, base)

#define UNSUPPORTED_DCFCLK 10000000
#define MIN_DPP_DISP_CLK     100000

static int dcn315_get_active_display_cnt_wa(
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
		if (link->link_enc && link->link_enc->funcs->is_dig_enabled &&
				link->link_enc->funcs->is_dig_enabled(link->link_enc))
			display_count++;
	}

	/* WA for hang on HDMI after display off back back on*/
	if (display_count == 0 && tmds_present)
		display_count = 1;

	return display_count;
}

static bool should_disable_otg(struct pipe_ctx *pipe)
{
	bool ret = true;

	if (pipe->stream->link->link_enc && pipe->stream->link->link_enc->funcs->is_dig_enabled &&
			pipe->stream->link->link_enc->funcs->is_dig_enabled(pipe->stream->link->link_enc))
		ret = false;
	return ret;
}

static void dcn315_disable_otg_wa(struct clk_mgr *clk_mgr_base, struct dc_state *context, bool disable)
{
	struct dc *dc = clk_mgr_base->ctx->dc;
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; ++i) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->top_pipe || pipe->prev_odm_pipe)
			continue;
		if (pipe->stream && (pipe->stream->dpms_off || pipe->plane_state == NULL ||
					dc_is_virtual_signal(pipe->stream->signal))) {

			/* This w/a should not trigger when we have a dig active */
			if (should_disable_otg(pipe)) {
				if (disable) {
					pipe->stream_res.tg->funcs->immediate_disable_crtc(pipe->stream_res.tg);
					reset_sync_context_for_pipe(dc, context, i);
				} else
					pipe->stream_res.tg->funcs->enable_crtc(pipe->stream_res.tg);
			}
		}
	}
}

static void dcn315_update_clocks(struct clk_mgr *clk_mgr_base,
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

	if (dc->work_arounds.skip_clock_update)
		return;

	clk_mgr_base->clks.zstate_support = new_clocks->zstate_support;
	/*
	 * if it is safe to lower, but we are already in the lower state, we don't have to do anything
	 * also if safe to lower is false, we just go in the higher state
	 */
	clk_mgr_base->clks.zstate_support = new_clocks->zstate_support;
	if (safe_to_lower) {
		if (clk_mgr_base->clks.dtbclk_en && !new_clocks->dtbclk_en) {
			dcn315_smu_set_dtbclk(clk_mgr, false);
			clk_mgr_base->clks.dtbclk_en = new_clocks->dtbclk_en;
		}
		/* check that we're not already in lower */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_LOW_POWER) {
			display_count = dcn315_get_active_display_cnt_wa(dc, context);
			/* if we can go lower, go lower */
			if (display_count == 0) {
				union display_idle_optimization_u idle_info = { 0 };
				idle_info.idle_info.df_request_disabled = 1;
				idle_info.idle_info.phy_ref_clk_off = 1;
				idle_info.idle_info.s0i2_rdy = 1;
				dcn315_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
				/* update power state */
				clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_LOW_POWER;
			}
		}
	} else {
		if (!clk_mgr_base->clks.dtbclk_en && new_clocks->dtbclk_en) {
			dcn315_smu_set_dtbclk(clk_mgr, true);
			clk_mgr_base->clks.dtbclk_en = new_clocks->dtbclk_en;
		}
		/* check that we're not already in D0 */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_MISSION_MODE) {
			union display_idle_optimization_u idle_info = { 0 };
			dcn315_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
			/* update power state */
			clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_MISSION_MODE;
		}
	}

	/* Lock pstate by requesting unsupported dcfclk if change is unsupported */
	if (!new_clocks->p_state_change_support)
		new_clocks->dcfclk_khz = UNSUPPORTED_DCFCLK;
	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		dcn315_smu_set_hard_min_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_khz);
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		dcn315_smu_set_min_deep_sleep_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_deep_sleep_khz);
	}

	// workaround: Limit dppclk to 100Mhz to avoid lower eDP panel switch to plus 4K monitor underflow.
	if (new_clocks->dppclk_khz < MIN_DPP_DISP_CLK)
		new_clocks->dppclk_khz = MIN_DPP_DISP_CLK;

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr->base.clks.dppclk_khz)) {
		if (clk_mgr->base.clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;
		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz) &&
	    (new_clocks->dispclk_khz > 0 || (safe_to_lower && display_count == 0))) {
		int requested_dispclk_khz = new_clocks->dispclk_khz;

		dcn315_disable_otg_wa(clk_mgr_base, context, true);

		/* Clamp the requested clock to PMFW based on their limit. */
		if (dc->debug.min_disp_clk_khz > 0 && requested_dispclk_khz < dc->debug.min_disp_clk_khz)
			requested_dispclk_khz = dc->debug.min_disp_clk_khz;

		dcn315_smu_set_dispclk(clk_mgr, requested_dispclk_khz);
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;
		dcn315_disable_otg_wa(clk_mgr_base, context, false);

		update_dispclk = true;
	}

	if (dpp_clock_lowered) {
		// increase per DPP DTO before lowering global dppclk
		dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		dcn315_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
	} else {
		// increase global DPPCLK before lowering per DPP DTO
		if (update_dppclk || update_dispclk)
			dcn315_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
		// always update dtos unless clock is lowered and not safe to lower
		if (new_clocks->dppclk_khz >= dc->current_state->bw_ctx.bw.dcn.clk.dppclk_khz)
			dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
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

static void dcn315_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info)
{
	return;
}

static struct clk_bw_params dcn315_bw_params = {
	.vram_type = Ddr4MemType,
	.num_channels = 2,
	.clk_table = {
		.entries = {
			{
				.voltage = 0,
				.dispclk_mhz = 640,
				.dppclk_mhz = 640,
				.phyclk_mhz = 810,
				.phyclk_d18_mhz = 667,
				.dtbclk_mhz = 600,
			},
			{
				.voltage = 1,
				.dispclk_mhz = 739,
				.dppclk_mhz = 739,
				.phyclk_mhz = 810,
				.phyclk_d18_mhz = 667,
				.dtbclk_mhz = 600,
			},
			{
				.voltage = 2,
				.dispclk_mhz = 960,
				.dppclk_mhz = 960,
				.phyclk_mhz = 810,
				.phyclk_d18_mhz = 667,
				.dtbclk_mhz = 600,
			},
			{
				.voltage = 3,
				.dispclk_mhz = 1200,
				.dppclk_mhz = 1200,
				.phyclk_mhz = 810,
				.phyclk_d18_mhz = 667,
				.dtbclk_mhz = 600,
			},
			{
				.voltage = 4,
				.dispclk_mhz = 1372,
				.dppclk_mhz = 1372,
				.phyclk_mhz = 810,
				.phyclk_d18_mhz = 667,
				.dtbclk_mhz = 600,
			},
		},
		.num_entries = 5,
	},

};

static struct wm_table ddr5_wm_table = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 129.0,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 129.0,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 129.0,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 129.0,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
	}
};

static struct wm_table lpddr5_wm_table = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 129.0,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 129.0,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 129.0,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 129.0,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
	}
};

/* Temporary Place holder until we can get them from fuse */
static DpmClocks_315_t dummy_clocks = { 0 };
static struct dcn315_watermarks dummy_wms = { 0 };

static void dcn315_build_watermark_ranges(struct clk_bw_params *bw_params, struct dcn315_watermarks *table)
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

static void dcn315_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_mgr_dcn315 *clk_mgr_dcn315 = TO_CLK_MGR_DCN315(clk_mgr);
	struct dcn315_watermarks *table = clk_mgr_dcn315->smu_wm_set.wm_set;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || clk_mgr_dcn315->smu_wm_set.mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn315_build_watermark_ranges(clk_mgr_base->bw_params, table);

	dcn315_smu_set_dram_addr_high(clk_mgr,
			clk_mgr_dcn315->smu_wm_set.mc_address.high_part);
	dcn315_smu_set_dram_addr_low(clk_mgr,
			clk_mgr_dcn315->smu_wm_set.mc_address.low_part);
	dcn315_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

static void dcn315_get_dpm_table_from_smu(struct clk_mgr_internal *clk_mgr,
		struct dcn315_smu_dpm_clks *smu_dpm_clks)
{
	DpmClocks_315_t *table = smu_dpm_clks->dpm_clks;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || smu_dpm_clks->mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn315_smu_set_dram_addr_high(clk_mgr,
			smu_dpm_clks->mc_address.high_part);
	dcn315_smu_set_dram_addr_low(clk_mgr,
			smu_dpm_clks->mc_address.low_part);
	dcn315_smu_transfer_dpm_table_smu_2_dram(clk_mgr);
}

static void dcn315_clk_mgr_helper_populate_bw_params(
		struct clk_mgr_internal *clk_mgr,
		struct integrated_info *bios_info,
		const DpmClocks_315_t *clock_table)
{
	int i;
	struct clk_bw_params *bw_params = clk_mgr->base.bw_params;
	uint32_t max_pstate = clock_table->NumDfPstatesEnabled - 1;
	struct clk_limit_table_entry def_max = bw_params->clk_table.entries[bw_params->clk_table.num_entries - 1];

	/* For 315 we want to base clock table on dcfclk, need at least one entry regardless of pmfw table */
	for (i = 0; i < clock_table->NumDcfClkLevelsEnabled; i++) {
		int j;

		/* DF table is sorted with clocks decreasing */
		for (j = clock_table->NumDfPstatesEnabled - 2; j >= 0; j--) {
			if (clock_table->DfPstateTable[j].Voltage <= clock_table->SocVoltage[i])
				max_pstate = j;
		}
		/* Max DCFCLK should match up with max pstate */
		if (i == clock_table->NumDcfClkLevelsEnabled - 1)
			max_pstate = 0;

		/* First search defaults for the clocks we don't read using closest lower or equal default dcfclk */
		for (j = bw_params->clk_table.num_entries - 1; j > 0; j--)
			if (bw_params->clk_table.entries[j].dcfclk_mhz <= clock_table->DcfClocks[i])
				break;
		bw_params->clk_table.entries[i].phyclk_mhz = bw_params->clk_table.entries[j].phyclk_mhz;
		bw_params->clk_table.entries[i].phyclk_d18_mhz = bw_params->clk_table.entries[j].phyclk_d18_mhz;
		bw_params->clk_table.entries[i].dtbclk_mhz = bw_params->clk_table.entries[j].dtbclk_mhz;

		/* Now update clocks we do read */
		bw_params->clk_table.entries[i].fclk_mhz = clock_table->DfPstateTable[max_pstate].FClk;
		bw_params->clk_table.entries[i].memclk_mhz = clock_table->DfPstateTable[max_pstate].MemClk;
		bw_params->clk_table.entries[i].voltage = clock_table->SocVoltage[i];
		bw_params->clk_table.entries[i].dcfclk_mhz = clock_table->DcfClocks[i];
		bw_params->clk_table.entries[i].socclk_mhz = clock_table->SocClocks[i];
		bw_params->clk_table.entries[i].dispclk_mhz = clock_table->DispClocks[i];
		bw_params->clk_table.entries[i].dppclk_mhz = clock_table->DppClocks[i];
		bw_params->clk_table.entries[i].wck_ratio = 1;
	}

	/* Make sure to include at least one entry */
	if (i == 0) {
		bw_params->clk_table.entries[i].fclk_mhz = clock_table->DfPstateTable[0].FClk;
		bw_params->clk_table.entries[i].memclk_mhz = clock_table->DfPstateTable[0].MemClk;
		bw_params->clk_table.entries[i].voltage = clock_table->DfPstateTable[0].Voltage;
		bw_params->clk_table.entries[i].dcfclk_mhz = clock_table->DcfClocks[0];
		bw_params->clk_table.entries[i].wck_ratio = 1;
		i++;
	} else if (clock_table->NumDcfClkLevelsEnabled != clock_table->NumSocClkLevelsEnabled) {
		bw_params->clk_table.entries[i-1].voltage = clock_table->SocVoltage[clock_table->NumSocClkLevelsEnabled - 1];
		bw_params->clk_table.entries[i-1].socclk_mhz = clock_table->SocClocks[clock_table->NumSocClkLevelsEnabled - 1];
		bw_params->clk_table.entries[i-1].dispclk_mhz = clock_table->DispClocks[clock_table->NumDispClkLevelsEnabled - 1];
		bw_params->clk_table.entries[i-1].dppclk_mhz = clock_table->DppClocks[clock_table->NumDispClkLevelsEnabled - 1];
	}
	bw_params->clk_table.num_entries = i;

	/* Set any 0 clocks to max default setting. Not an issue for
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
		if (!bw_params->clk_table.entries[i].phyclk_mhz)
			bw_params->clk_table.entries[i].phyclk_mhz = def_max.phyclk_mhz;
		if (!bw_params->clk_table.entries[i].phyclk_d18_mhz)
			bw_params->clk_table.entries[i].phyclk_d18_mhz = def_max.phyclk_d18_mhz;
		if (!bw_params->clk_table.entries[i].dtbclk_mhz)
			bw_params->clk_table.entries[i].dtbclk_mhz = def_max.dtbclk_mhz;
	}

	/* Make sure all highest default clocks are included*/
	ASSERT(bw_params->clk_table.entries[i-1].phyclk_mhz == def_max.phyclk_mhz);
	ASSERT(bw_params->clk_table.entries[i-1].phyclk_d18_mhz == def_max.phyclk_d18_mhz);
	ASSERT(bw_params->clk_table.entries[i-1].dtbclk_mhz == def_max.dtbclk_mhz);
	ASSERT(bw_params->clk_table.entries[i-1].dcfclk_mhz);
	bw_params->vram_type = bios_info->memory_type;
	bw_params->num_channels = bios_info->ma_channel_number;
	bw_params->dram_channel_width_bytes = bios_info->memory_type == 0x22 ? 8 : 4;

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

static void dcn315_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	dcn315_smu_enable_pme_wa(clk_mgr);
}

static struct clk_mgr_funcs dcn315_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.get_dtb_ref_clk_frequency = dcn31_get_dtb_ref_freq_khz,
	.update_clocks = dcn315_update_clocks,
	.init_clocks = dcn31_init_clocks,
	.enable_pme_wa = dcn315_enable_pme_wa,
	.are_clock_states_equal = dcn31_are_clock_states_equal,
	.notify_wm_ranges = dcn315_notify_wm_ranges
};
extern struct clk_mgr_funcs dcn3_fpga_funcs;

void dcn315_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_dcn315 *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	struct dcn315_smu_dpm_clks smu_dpm_clks = { 0 };
	struct clk_log_info log_info = {0};

	clk_mgr->base.base.ctx = ctx;
	clk_mgr->base.base.funcs = &dcn315_funcs;

	clk_mgr->base.pp_smu = pp_smu;

	clk_mgr->base.dccg = dccg;
	clk_mgr->base.dfs_bypass_disp_clk = 0;

	clk_mgr->base.dprefclk_ss_percentage = 0;
	clk_mgr->base.dprefclk_ss_divider = 1000;
	clk_mgr->base.ss_on_dprefclk = false;
	clk_mgr->base.dfs_ref_freq_khz = 48000;

	clk_mgr->smu_wm_set.wm_set = (struct dcn315_watermarks *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				sizeof(struct dcn315_watermarks),
				&clk_mgr->smu_wm_set.mc_address.quad_part);

	if (!clk_mgr->smu_wm_set.wm_set) {
		clk_mgr->smu_wm_set.wm_set = &dummy_wms;
		clk_mgr->smu_wm_set.mc_address.quad_part = 0;
	}
	ASSERT(clk_mgr->smu_wm_set.wm_set);

	smu_dpm_clks.dpm_clks = (DpmClocks_315_t *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				sizeof(DpmClocks_315_t),
				&smu_dpm_clks.mc_address.quad_part);

	if (smu_dpm_clks.dpm_clks == NULL) {
		smu_dpm_clks.dpm_clks = &dummy_clocks;
		smu_dpm_clks.mc_address.quad_part = 0;
	}

	ASSERT(smu_dpm_clks.dpm_clks);

	clk_mgr->base.smu_ver = dcn315_smu_get_smu_version(&clk_mgr->base);

	if (clk_mgr->base.smu_ver > 0)
		clk_mgr->base.smu_present = true;

	if (ctx->dc_bios->integrated_info->memory_type == LpDdr5MemType) {
		dcn315_bw_params.wm_table = lpddr5_wm_table;
	} else {
		dcn315_bw_params.wm_table = ddr5_wm_table;
	}
	/* Saved clocks configured at boot for debug purposes */
	dcn315_dump_clk_registers(&clk_mgr->base.base.boot_snapshot,
				  &clk_mgr->base.base, &log_info);

	clk_mgr->base.base.dprefclk_khz = 600000;
	clk_mgr->base.base.dprefclk_khz = dcn315_smu_get_dpref_clk(&clk_mgr->base);
	clk_mgr->base.base.clks.ref_dtbclk_khz = clk_mgr->base.base.dprefclk_khz;
	dce_clock_read_ss_info(&clk_mgr->base);
	clk_mgr->base.base.clks.ref_dtbclk_khz = dce_adjust_dp_ref_freq_for_ss(&clk_mgr->base, clk_mgr->base.base.dprefclk_khz);

	clk_mgr->base.base.bw_params = &dcn315_bw_params;

	if (clk_mgr->base.base.ctx->dc->debug.pstate_enabled) {
		int i;

		dcn315_get_dpm_table_from_smu(&clk_mgr->base, &smu_dpm_clks);
		DC_LOG_SMU("NumDcfClkLevelsEnabled: %d\n"
				   "NumDispClkLevelsEnabled: %d\n"
				   "NumSocClkLevelsEnabled: %d\n"
				   "VcnClkLevelsEnabled: %d\n"
				   "NumDfPst atesEnabled: %d\n"
				   "MinGfxClk: %d\n"
				   "MaxGfxClk: %d\n",
				   smu_dpm_clks.dpm_clks->NumDcfClkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->NumDispClkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->NumSocClkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->VcnClkLevelsEnabled,
				   smu_dpm_clks.dpm_clks->NumDfPstatesEnabled,
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
		for (i = 0; i < NUM_SOC_VOLTAGE_LEVELS; i++)
			DC_LOG_SMU("smu_dpm_clks.dpm_clks->SocVoltage[%d] = %d\n",
					   i, smu_dpm_clks.dpm_clks->SocVoltage[i]);

		for (i = 0; i < NUM_DF_PSTATE_LEVELS; i++) {
			DC_LOG_SMU("smu_dpm_clks.dpm_clks.DfPstateTable[%d].FClk = %d\n"
					   "smu_dpm_clks.dpm_clks->DfPstateTable[%d].MemClk= %d\n"
					   "smu_dpm_clks.dpm_clks->DfPstateTable[%d].Voltage = %d\n",
					   i, smu_dpm_clks.dpm_clks->DfPstateTable[i].FClk,
					   i, smu_dpm_clks.dpm_clks->DfPstateTable[i].MemClk,
					   i, smu_dpm_clks.dpm_clks->DfPstateTable[i].Voltage);
		}

		if (ctx->dc_bios->integrated_info) {
			dcn315_clk_mgr_helper_populate_bw_params(
					&clk_mgr->base,
					ctx->dc_bios->integrated_info,
					smu_dpm_clks.dpm_clks);
		}
	}

	if (smu_dpm_clks.dpm_clks && smu_dpm_clks.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr->base.base.ctx, DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				smu_dpm_clks.dpm_clks);
}

void dcn315_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr_int)
{
	struct clk_mgr_dcn315 *clk_mgr = TO_CLK_MGR_DCN315(clk_mgr_int);

	if (clk_mgr->smu_wm_set.wm_set && clk_mgr->smu_wm_set.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr_int->base.ctx, DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				clk_mgr->smu_wm_set.wm_set);
}
