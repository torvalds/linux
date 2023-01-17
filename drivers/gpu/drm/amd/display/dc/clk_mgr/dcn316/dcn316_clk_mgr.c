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
#include "dcn316_clk_mgr.h"
#include "reg_helper.h"
#include "core_types.h"
#include "dcn316_smu.h"
#include "dm_helpers.h"
#include "dc_dmub_srv.h"
#include "dc_link_dp.h"

// DCN316 this is CLK1 instance
#define MAX_INSTANCE                                        7
#define MAX_SEGMENT                                         6

struct IP_BASE_INSTANCE
{
    unsigned int segment[MAX_SEGMENT];
};

struct IP_BASE
{
    struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
};

static const struct IP_BASE CLK_BASE = { { { { 0x00016C00, 0x02401800, 0, 0, 0, 0 } },
                                        { { 0x00016E00, 0x02401C00, 0, 0, 0, 0 } },
                                        { { 0x00017000, 0x02402000, 0, 0, 0, 0 } },
                                        { { 0x00017200, 0x02402400, 0, 0, 0, 0 } },
                                        { { 0x0001B000, 0x0242D800, 0, 0, 0, 0 } },
                                        { { 0x0001B200, 0x0242DC00, 0, 0, 0, 0 } },
                                        { { 0x0001B400, 0x0242E000, 0, 0, 0, 0 } } } };

#define regCLK1_CLK_PLL_REQ						0x0237
#define regCLK1_CLK_PLL_REQ_BASE_IDX			0

#define CLK1_CLK_PLL_REQ__FbMult_int__SHIFT		0x0
#define CLK1_CLK_PLL_REQ__PllSpineDiv__SHIFT	0xc
#define CLK1_CLK_PLL_REQ__FbMult_frac__SHIFT	0x10
#define CLK1_CLK_PLL_REQ__FbMult_int_MASK		0x000001FFL
#define CLK1_CLK_PLL_REQ__PllSpineDiv_MASK		0x0000F000L
#define CLK1_CLK_PLL_REQ__FbMult_frac_MASK		0xFFFF0000L

#define REG(reg_name) \
	(CLK_BASE.instance[0].segment[reg ## reg_name ## _BASE_IDX] + reg ## reg_name)

#define TO_CLK_MGR_DCN316(clk_mgr)\
	container_of(clk_mgr, struct clk_mgr_dcn316, base)

static int dcn316_get_active_display_cnt_wa(
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

static void dcn316_disable_otg_wa(struct clk_mgr *clk_mgr_base, struct dc_state *context, bool disable)
{
	struct dc *dc = clk_mgr_base->ctx->dc;
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; ++i) {
		struct pipe_ctx *pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->top_pipe || pipe->prev_odm_pipe)
			continue;
		if (pipe->stream && (pipe->stream->dpms_off || pipe->plane_state == NULL ||
				     dc_is_virtual_signal(pipe->stream->signal))) {
			if (disable) {
				pipe->stream_res.tg->funcs->immediate_disable_crtc(pipe->stream_res.tg);
				reset_sync_context_for_pipe(dc, context, i);
			} else
				pipe->stream_res.tg->funcs->enable_crtc(pipe->stream_res.tg);
		}
	}
}

static void dcn316_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	dcn316_smu_enable_pme_wa(clk_mgr);
}

static void dcn316_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower)
{
	union dmub_rb_cmd cmd;
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
	clk_mgr_base->clks.zstate_support = new_clocks->zstate_support;
	if (safe_to_lower) {
		if (clk_mgr_base->clks.dtbclk_en && !new_clocks->dtbclk_en) {
			dcn316_smu_set_dtbclk(clk_mgr, false);
			clk_mgr_base->clks.dtbclk_en = new_clocks->dtbclk_en;
		}
		/* check that we're not already in lower */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_LOW_POWER) {
			display_count = dcn316_get_active_display_cnt_wa(dc, context);
			/* if we can go lower, go lower */
			if (display_count == 0) {
				union display_idle_optimization_u idle_info = { 0 };
				idle_info.idle_info.df_request_disabled = 1;
				idle_info.idle_info.phy_ref_clk_off = 1;
				idle_info.idle_info.s0i2_rdy = 1;
				dcn316_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
				/* update power state */
				clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_LOW_POWER;
			}
		}
	} else {
		if (!clk_mgr_base->clks.dtbclk_en && new_clocks->dtbclk_en) {
			dcn316_smu_set_dtbclk(clk_mgr, true);
			clk_mgr_base->clks.dtbclk_en = new_clocks->dtbclk_en;
		}

		/* check that we're not already in D0 */
		if (clk_mgr_base->clks.pwr_state != DCN_PWR_STATE_MISSION_MODE) {
			union display_idle_optimization_u idle_info = { 0 };
			dcn316_smu_set_display_idle_optimization(clk_mgr, idle_info.data);
			/* update power state */
			clk_mgr_base->clks.pwr_state = DCN_PWR_STATE_MISSION_MODE;
		}
	}

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		dcn316_smu_set_hard_min_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_khz);
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		dcn316_smu_set_min_deep_sleep_dcfclk(clk_mgr, clk_mgr_base->clks.dcfclk_deep_sleep_khz);
	}

	// workaround: Limit dppclk to 100Mhz to avoid lower eDP panel switch to plus 4K monitor underflow.
	if (!IS_DIAG_DC(dc->ctx->dce_environment)) {
		if (new_clocks->dppclk_khz < 100000)
			new_clocks->dppclk_khz = 100000;
		if (new_clocks->dispclk_khz < 100000)
			new_clocks->dispclk_khz = 100000;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr->base.clks.dppclk_khz)) {
		if (clk_mgr->base.clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;
		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		dcn316_disable_otg_wa(clk_mgr_base, context, true);

		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;
		dcn316_smu_set_dispclk(clk_mgr, clk_mgr_base->clks.dispclk_khz);
		dcn316_disable_otg_wa(clk_mgr_base, context, false);

		update_dispclk = true;
	}

	if (dpp_clock_lowered) {
		// increase per DPP DTO before lowering global dppclk
		dcn20_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		dcn316_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
	} else {
		// increase global DPPCLK before lowering per DPP DTO
		if (update_dppclk || update_dispclk)
			dcn316_smu_set_dppclk(clk_mgr, clk_mgr_base->clks.dppclk_khz);
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

	dc_dmub_srv_cmd_queue(dc->ctx->dmub_srv, &cmd);
	dc_dmub_srv_cmd_execute(dc->ctx->dmub_srv);
	dc_dmub_srv_wait_idle(dc->ctx->dmub_srv);
}

static void dcn316_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info)
{
	return;
}

static struct clk_bw_params dcn316_bw_params = {
	.vram_type = Ddr4MemType,
	.num_channels = 1,
	.clk_table = {
		.num_entries = 5,
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
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 11.5,
			.sr_enter_plus_exit_time_us = 14.5,
			.valid = true,
		},
	}
};

static DpmClocks_316_t dummy_clocks;

static struct dcn316_watermarks dummy_wms = { 0 };

static void dcn316_build_watermark_ranges(struct clk_bw_params *bw_params, struct dcn316_watermarks *table)
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

static void dcn316_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_mgr_dcn316 *clk_mgr_dcn316 = TO_CLK_MGR_DCN316(clk_mgr);
	struct dcn316_watermarks *table = clk_mgr_dcn316->smu_wm_set.wm_set;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || clk_mgr_dcn316->smu_wm_set.mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn316_build_watermark_ranges(clk_mgr_base->bw_params, table);

	dcn316_smu_set_dram_addr_high(clk_mgr,
			clk_mgr_dcn316->smu_wm_set.mc_address.high_part);
	dcn316_smu_set_dram_addr_low(clk_mgr,
			clk_mgr_dcn316->smu_wm_set.mc_address.low_part);
	dcn316_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

static void dcn316_get_dpm_table_from_smu(struct clk_mgr_internal *clk_mgr,
		struct dcn316_smu_dpm_clks *smu_dpm_clks)
{
	DpmClocks_316_t *table = smu_dpm_clks->dpm_clks;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || smu_dpm_clks->mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn316_smu_set_dram_addr_high(clk_mgr,
			smu_dpm_clks->mc_address.high_part);
	dcn316_smu_set_dram_addr_low(clk_mgr,
			smu_dpm_clks->mc_address.low_part);
	dcn316_smu_transfer_dpm_table_smu_2_dram(clk_mgr);
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

static unsigned int find_clk_for_voltage(
		const DpmClocks_316_t *clock_table,
		const uint32_t clocks[],
		unsigned int voltage)
{
	int i;
	int max_voltage = 0;
	int clock = 0;

	for (i = 0; i < NUM_SOC_VOLTAGE_LEVELS; i++) {
		if (clock_table->SocVoltage[i] == voltage) {
			return clocks[i];
		} else if (clock_table->SocVoltage[i] >= max_voltage &&
				clock_table->SocVoltage[i] < voltage) {
			max_voltage = clock_table->SocVoltage[i];
			clock = clocks[i];
		}
	}

	ASSERT(clock);
	return clock;
}

static void dcn316_clk_mgr_helper_populate_bw_params(
		struct clk_mgr_internal *clk_mgr,
		struct integrated_info *bios_info,
		const DpmClocks_316_t *clock_table)
{
	int i, j;
	struct clk_bw_params *bw_params = clk_mgr->base.bw_params;
	uint32_t max_dispclk = 0, max_dppclk = 0;

	j = -1;

	ASSERT(NUM_DF_PSTATE_LEVELS <= MAX_NUM_DPM_LVL);

	/* Find lowest DPM, FCLK is filled in reverse order*/

	for (i = NUM_DF_PSTATE_LEVELS - 1; i >= 0; i--) {
		if (clock_table->DfPstateTable[i].FClk != 0) {
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

	/* dispclk and dppclk can be max at any voltage, same number of levels for both */
	if (clock_table->NumDispClkLevelsEnabled <= NUM_DISPCLK_DPM_LEVELS &&
	    clock_table->NumDispClkLevelsEnabled <= NUM_DPPCLK_DPM_LEVELS) {
		max_dispclk = find_max_clk_value(clock_table->DispClocks, clock_table->NumDispClkLevelsEnabled);
		max_dppclk = find_max_clk_value(clock_table->DppClocks, clock_table->NumDispClkLevelsEnabled);
	} else {
		ASSERT(0);
	}

	for (i = 0; i < bw_params->clk_table.num_entries; i++, j--) {
		int temp;

		bw_params->clk_table.entries[i].fclk_mhz = clock_table->DfPstateTable[j].FClk;
		bw_params->clk_table.entries[i].memclk_mhz = clock_table->DfPstateTable[j].MemClk;
		bw_params->clk_table.entries[i].voltage = clock_table->DfPstateTable[j].Voltage;
		switch (clock_table->DfPstateTable[j].WckRatio) {
		case WCK_RATIO_1_2:
			bw_params->clk_table.entries[i].wck_ratio = 2;
			break;
		case WCK_RATIO_1_4:
			bw_params->clk_table.entries[i].wck_ratio = 4;
			break;
		default:
			bw_params->clk_table.entries[i].wck_ratio = 1;
		}
		temp = find_clk_for_voltage(clock_table, clock_table->DcfClocks, clock_table->DfPstateTable[j].Voltage);
		if (temp)
			bw_params->clk_table.entries[i].dcfclk_mhz = temp;
		temp = find_clk_for_voltage(clock_table, clock_table->SocClocks, clock_table->DfPstateTable[j].Voltage);
		if (temp)
			bw_params->clk_table.entries[i].socclk_mhz = temp;
		bw_params->clk_table.entries[i].dispclk_mhz = max_dispclk;
		bw_params->clk_table.entries[i].dppclk_mhz = max_dppclk;
	}

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



static struct clk_mgr_funcs dcn316_funcs = {
	.enable_pme_wa = dcn316_enable_pme_wa,
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.get_dtb_ref_clk_frequency = dcn31_get_dtb_ref_freq_khz,
	.update_clocks = dcn316_update_clocks,
	.init_clocks = dcn31_init_clocks,
	.are_clock_states_equal = dcn31_are_clock_states_equal,
	.notify_wm_ranges = dcn316_notify_wm_ranges
};
extern struct clk_mgr_funcs dcn3_fpga_funcs;

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

void dcn316_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_dcn316 *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	struct dcn316_smu_dpm_clks smu_dpm_clks = { 0 };

	clk_mgr->base.base.ctx = ctx;
	clk_mgr->base.base.funcs = &dcn316_funcs;

	clk_mgr->base.pp_smu = pp_smu;

	clk_mgr->base.dccg = dccg;
	clk_mgr->base.dfs_bypass_disp_clk = 0;

	clk_mgr->base.dprefclk_ss_percentage = 0;
	clk_mgr->base.dprefclk_ss_divider = 1000;
	clk_mgr->base.ss_on_dprefclk = false;
	clk_mgr->base.dfs_ref_freq_khz = 48000;

	clk_mgr->smu_wm_set.wm_set = (struct dcn316_watermarks *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				sizeof(struct dcn316_watermarks),
				&clk_mgr->smu_wm_set.mc_address.quad_part);

	if (!clk_mgr->smu_wm_set.wm_set) {
		clk_mgr->smu_wm_set.wm_set = &dummy_wms;
		clk_mgr->smu_wm_set.mc_address.quad_part = 0;
	}
	ASSERT(clk_mgr->smu_wm_set.wm_set);

	smu_dpm_clks.dpm_clks = (DpmClocks_316_t *)dm_helpers_allocate_gpu_mem(
				clk_mgr->base.base.ctx,
				DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				sizeof(DpmClocks_316_t),
				&smu_dpm_clks.mc_address.quad_part);

	if (smu_dpm_clks.dpm_clks == NULL) {
		smu_dpm_clks.dpm_clks = &dummy_clocks;
		smu_dpm_clks.mc_address.quad_part = 0;
	}

	ASSERT(smu_dpm_clks.dpm_clks);

	if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment)) {
		clk_mgr->base.base.funcs = &dcn3_fpga_funcs;
		clk_mgr->base.base.dentist_vco_freq_khz = 2500000;
	} else {
		struct clk_log_info log_info = {0};

		clk_mgr->base.smu_ver = dcn316_smu_get_smu_version(&clk_mgr->base);

		if (clk_mgr->base.smu_ver > 0)
			clk_mgr->base.smu_present = true;

		// Skip this for now as it did not work on DCN315, renable during bring up
		clk_mgr->base.base.dentist_vco_freq_khz = get_vco_frequency_from_reg(&clk_mgr->base);

		/* in case we don't get a value from the register, use default */
		if (clk_mgr->base.base.dentist_vco_freq_khz == 0)
			clk_mgr->base.base.dentist_vco_freq_khz = 2500000; /* 2400MHz */


		if (ctx->dc_bios->integrated_info->memory_type == LpDdr5MemType) {
			dcn316_bw_params.wm_table = lpddr5_wm_table;
		} else {
			dcn316_bw_params.wm_table = ddr4_wm_table;
		}
		/* Saved clocks configured at boot for debug purposes */
		dcn316_dump_clk_registers(&clk_mgr->base.base.boot_snapshot,
					  &clk_mgr->base.base, &log_info);

	}

	clk_mgr->base.base.dprefclk_khz = 600000;
	clk_mgr->base.base.dprefclk_khz = dcn316_smu_get_dpref_clk(&clk_mgr->base);
	clk_mgr->base.base.clks.ref_dtbclk_khz = clk_mgr->base.base.dprefclk_khz;
	dce_clock_read_ss_info(&clk_mgr->base);
	/*clk_mgr->base.dccg->ref_dtbclk_khz =
	dce_adjust_dp_ref_freq_for_ss(&clk_mgr->base, clk_mgr->base.base.dprefclk_khz);*/

	clk_mgr->base.base.bw_params = &dcn316_bw_params;

	if (clk_mgr->base.base.ctx->dc->debug.pstate_enabled) {
		dcn316_get_dpm_table_from_smu(&clk_mgr->base, &smu_dpm_clks);

		if (ctx->dc_bios && ctx->dc_bios->integrated_info) {
			dcn316_clk_mgr_helper_populate_bw_params(
					&clk_mgr->base,
					ctx->dc_bios->integrated_info,
					smu_dpm_clks.dpm_clks);
		}
	}

	if (smu_dpm_clks.dpm_clks && smu_dpm_clks.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr->base.base.ctx, DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				smu_dpm_clks.dpm_clks);
}

void dcn316_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr_int)
{
	struct clk_mgr_dcn316 *clk_mgr = TO_CLK_MGR_DCN316(clk_mgr_int);

	if (clk_mgr->smu_wm_set.wm_set && clk_mgr->smu_wm_set.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr_int->base.ctx, DC_MEM_ALLOC_TYPE_FRAME_BUFFER,
				clk_mgr->smu_wm_set.wm_set);
}
