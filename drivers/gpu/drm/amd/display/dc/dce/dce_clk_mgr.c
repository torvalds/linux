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

#include "dce_clk_mgr.h"

#include "reg_helper.h"
#include "dmcu.h"
#include "core_types.h"
#include "dal_asic_id.h"

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

/* Max clock values for each state indexed by "enum clocks_state": */
static const struct state_dependent_clocks dce80_max_clks_by_state[] = {
/* ClocksStateInvalid - should not be used */
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/* ClocksStateUltraLow - not expected to be used for DCE 8.0 */
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/* ClocksStateLow */
{ .display_clk_khz = 352000, .pixel_clk_khz = 330000},
/* ClocksStateNominal */
{ .display_clk_khz = 600000, .pixel_clk_khz = 400000 },
/* ClocksStatePerformance */
{ .display_clk_khz = 600000, .pixel_clk_khz = 400000 } };

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

static const struct state_dependent_clocks dce120_max_clks_by_state[] = {
/*ClocksStateInvalid - should not be used*/
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/*ClocksStateUltraLow - currently by HW design team not supposed to be used*/
{ .display_clk_khz = 0, .pixel_clk_khz = 0 },
/*ClocksStateLow*/
{ .display_clk_khz = 460000, .pixel_clk_khz = 400000 },
/*ClocksStateNominal*/
{ .display_clk_khz = 670000, .pixel_clk_khz = 600000 },
/*ClocksStatePerformance*/
{ .display_clk_khz = 1133000, .pixel_clk_khz = 600000 } };

int dentist_get_divider_from_did(int did)
{
	if (did < DENTIST_BASE_DID_1)
		did = DENTIST_BASE_DID_1;
	if (did > DENTIST_MAX_DID)
		did = DENTIST_MAX_DID;

	if (did < DENTIST_BASE_DID_2) {
		return DENTIST_DIVIDER_RANGE_1_START + DENTIST_DIVIDER_RANGE_1_STEP
							* (did - DENTIST_BASE_DID_1);
	} else if (did < DENTIST_BASE_DID_3) {
		return DENTIST_DIVIDER_RANGE_2_START + DENTIST_DIVIDER_RANGE_2_STEP
							* (did - DENTIST_BASE_DID_2);
	} else if (did < DENTIST_BASE_DID_4) {
		return DENTIST_DIVIDER_RANGE_3_START + DENTIST_DIVIDER_RANGE_3_STEP
							* (did - DENTIST_BASE_DID_3);
	} else {
		return DENTIST_DIVIDER_RANGE_4_START + DENTIST_DIVIDER_RANGE_4_STEP
							* (did - DENTIST_BASE_DID_4);
	}
}

/* SW will adjust DP REF Clock average value for all purposes
 * (DP DTO / DP Audio DTO and DP GTC)
 if clock is spread for all cases:
 -if SS enabled on DP Ref clock and HW de-spreading enabled with SW
 calculations for DS_INCR/DS_MODULO (this is planned to be default case)
 -if SS enabled on DP Ref clock and HW de-spreading enabled with HW
 calculations (not planned to be used, but average clock should still
 be valid)
 -if SS enabled on DP Ref clock and HW de-spreading disabled
 (should not be case with CIK) then SW should program all rates
 generated according to average value (case as with previous ASICs)
  */
static int clk_mgr_adjust_dp_ref_freq_for_ss(struct dce_clk_mgr *clk_mgr_dce, int dp_ref_clk_khz)
{
	if (clk_mgr_dce->ss_on_dprefclk && clk_mgr_dce->dprefclk_ss_divider != 0) {
		struct fixed31_32 ss_percentage = dc_fixpt_div_int(
				dc_fixpt_from_fraction(clk_mgr_dce->dprefclk_ss_percentage,
							clk_mgr_dce->dprefclk_ss_divider), 200);
		struct fixed31_32 adj_dp_ref_clk_khz;

		ss_percentage = dc_fixpt_sub(dc_fixpt_one, ss_percentage);
		adj_dp_ref_clk_khz = dc_fixpt_mul_int(ss_percentage, dp_ref_clk_khz);
		dp_ref_clk_khz = dc_fixpt_floor(adj_dp_ref_clk_khz);
	}
	return dp_ref_clk_khz;
}

static int dce_get_dp_ref_freq_khz(struct clk_mgr *clk_mgr)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);
	int dprefclk_wdivider;
	int dprefclk_src_sel;
	int dp_ref_clk_khz = 600000;
	int target_div;

	/* ASSERT DP Reference Clock source is from DFS*/
	REG_GET(DPREFCLK_CNTL, DPREFCLK_SRC_SEL, &dprefclk_src_sel);
	ASSERT(dprefclk_src_sel == 0);

	/* Read the mmDENTIST_DISPCLK_CNTL to get the currently
	 * programmed DID DENTIST_DPREFCLK_WDIVIDER*/
	REG_GET(DENTIST_DISPCLK_CNTL, DENTIST_DPREFCLK_WDIVIDER, &dprefclk_wdivider);

	/* Convert DENTIST_DPREFCLK_WDIVIDERto actual divider*/
	target_div = dentist_get_divider_from_did(dprefclk_wdivider);

	/* Calculate the current DFS clock, in kHz.*/
	dp_ref_clk_khz = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
		* clk_mgr_dce->dentist_vco_freq_khz) / target_div;

	return clk_mgr_adjust_dp_ref_freq_for_ss(clk_mgr_dce, dp_ref_clk_khz);
}

int dce12_get_dp_ref_freq_khz(struct clk_mgr *clk_mgr)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);

	return clk_mgr_adjust_dp_ref_freq_for_ss(clk_mgr_dce, clk_mgr_dce->dprefclk_khz);
}

/* unit: in_khz before mode set, get pixel clock from context. ASIC register
 * may not be programmed yet
 */
static uint32_t get_max_pixel_clock_for_all_paths(struct dc_state *context)
{
	uint32_t max_pix_clk = 0;
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		/* do not check under lay */
		if (pipe_ctx->top_pipe)
			continue;

		if (pipe_ctx->stream_res.pix_clk_params.requested_pix_clk > max_pix_clk)
			max_pix_clk = pipe_ctx->stream_res.pix_clk_params.requested_pix_clk;

		/* raise clock state for HBR3/2 if required. Confirmed with HW DCE/DPCS
		 * logic for HBR3 still needs Nominal (0.8V) on VDDC rail
		 */
		if (dc_is_dp_signal(pipe_ctx->stream->signal) &&
				pipe_ctx->stream_res.pix_clk_params.requested_sym_clk > max_pix_clk)
			max_pix_clk = pipe_ctx->stream_res.pix_clk_params.requested_sym_clk;
	}

	return max_pix_clk;
}

static enum dm_pp_clocks_state dce_get_required_clocks_state(
	struct clk_mgr *clk_mgr,
	struct dc_state *context)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);
	int i;
	enum dm_pp_clocks_state low_req_clk;
	int max_pix_clk = get_max_pixel_clock_for_all_paths(context);

	/* Iterate from highest supported to lowest valid state, and update
	 * lowest RequiredState with the lowest state that satisfies
	 * all required clocks
	 */
	for (i = clk_mgr_dce->max_clks_state; i >= DM_PP_CLOCKS_STATE_ULTRA_LOW; i--)
		if (context->bw.dce.dispclk_khz >
				clk_mgr_dce->max_clks_by_state[i].display_clk_khz
			|| max_pix_clk >
				clk_mgr_dce->max_clks_by_state[i].pixel_clk_khz)
			break;

	low_req_clk = i + 1;
	if (low_req_clk > clk_mgr_dce->max_clks_state) {
		/* set max clock state for high phyclock, invalid on exceeding display clock */
		if (clk_mgr_dce->max_clks_by_state[clk_mgr_dce->max_clks_state].display_clk_khz
				< context->bw.dce.dispclk_khz)
			low_req_clk = DM_PP_CLOCKS_STATE_INVALID;
		else
			low_req_clk = clk_mgr_dce->max_clks_state;
	}

	return low_req_clk;
}

static int dce_set_clock(
	struct clk_mgr *clk_mgr,
	int requested_clk_khz)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);
	struct bp_pixel_clock_parameters pxl_clk_params = { 0 };
	struct dc_bios *bp = clk_mgr->ctx->dc_bios;
	int actual_clock = requested_clk_khz;
	struct dmcu *dmcu = clk_mgr_dce->base.ctx->dc->res_pool->dmcu;

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = max(requested_clk_khz,
				clk_mgr_dce->dentist_vco_freq_khz / 64);

	/* Prepare to program display clock*/
	pxl_clk_params.target_pixel_clock = requested_clk_khz;
	pxl_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;

	if (clk_mgr_dce->dfs_bypass_active)
		pxl_clk_params.flags.SET_DISPCLK_DFS_BYPASS = true;

	bp->funcs->program_display_engine_pll(bp, &pxl_clk_params);

	if (clk_mgr_dce->dfs_bypass_active) {
		/* Cache the fixed display clock*/
		clk_mgr_dce->dfs_bypass_disp_clk =
			pxl_clk_params.dfs_bypass_display_clock;
		actual_clock = pxl_clk_params.dfs_bypass_display_clock;
	}

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		clk_mgr_dce->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;

	if (dmcu && dmcu->funcs->is_dmcu_initialized(dmcu))
		dmcu->funcs->set_psr_wait_loop(dmcu, actual_clock / 1000 / 7);

	return actual_clock;
}

int dce112_set_clock(struct clk_mgr *clk_mgr, int requested_clk_khz)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = clk_mgr->ctx->dc_bios;
	struct dc *core_dc = clk_mgr->ctx->dc;
	struct dmcu *dmcu = core_dc->res_pool->dmcu;
	int actual_clock = requested_clk_khz;
	/* Prepare to program display clock*/
	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = max(requested_clk_khz,
				clk_mgr_dce->dentist_vco_freq_khz / 62);

	dce_clk_params.target_clock_frequency = requested_clk_khz;
	dce_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DISPLAY_CLOCK;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);
	actual_clock = dce_clk_params.target_clock_frequency;

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		clk_mgr_dce->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;

	/*Program DP ref Clock*/
	/*VBIOS will determine DPREFCLK frequency, so we don't set it*/
	dce_clk_params.target_clock_frequency = 0;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DPREFCLK;
	if (!ASICREV_IS_VEGA20_P(clk_mgr->ctx->asic_id.hw_internal_rev))
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

static void dce_clock_read_integrated_info(struct dce_clk_mgr *clk_mgr_dce)
{
	struct dc_debug_options *debug = &clk_mgr_dce->base.ctx->dc->debug;
	struct dc_bios *bp = clk_mgr_dce->base.ctx->dc_bios;
	struct integrated_info info = { { { 0 } } };
	struct dc_firmware_info fw_info = { { 0 } };
	int i;

	if (bp->integrated_info)
		info = *bp->integrated_info;

	clk_mgr_dce->dentist_vco_freq_khz = info.dentist_vco_freq;
	if (clk_mgr_dce->dentist_vco_freq_khz == 0) {
		bp->funcs->get_firmware_info(bp, &fw_info);
		clk_mgr_dce->dentist_vco_freq_khz =
			fw_info.smu_gpu_pll_output_freq;
		if (clk_mgr_dce->dentist_vco_freq_khz == 0)
			clk_mgr_dce->dentist_vco_freq_khz = 3600000;
	}

	/*update the maximum display clock for each power state*/
	for (i = 0; i < NUMBER_OF_DISP_CLK_VOLTAGE; ++i) {
		enum dm_pp_clocks_state clk_state = DM_PP_CLOCKS_STATE_INVALID;

		switch (i) {
		case 0:
			clk_state = DM_PP_CLOCKS_STATE_ULTRA_LOW;
			break;

		case 1:
			clk_state = DM_PP_CLOCKS_STATE_LOW;
			break;

		case 2:
			clk_state = DM_PP_CLOCKS_STATE_NOMINAL;
			break;

		case 3:
			clk_state = DM_PP_CLOCKS_STATE_PERFORMANCE;
			break;

		default:
			clk_state = DM_PP_CLOCKS_STATE_INVALID;
			break;
		}

		/*Do not allow bad VBIOS/SBIOS to override with invalid values,
		 * check for > 100MHz*/
		if (info.disp_clk_voltage[i].max_supported_clk >= 100000)
			clk_mgr_dce->max_clks_by_state[clk_state].display_clk_khz =
				info.disp_clk_voltage[i].max_supported_clk;
	}

	if (!debug->disable_dfs_bypass && bp->integrated_info)
		if (bp->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE)
			clk_mgr_dce->dfs_bypass_enabled = true;
}

void dce_clock_read_ss_info(struct dce_clk_mgr *clk_mgr_dce)
{
	struct dc_bios *bp = clk_mgr_dce->base.ctx->dc_bios;
	int ss_info_num = bp->funcs->get_ss_entry_number(
			bp, AS_SIGNAL_TYPE_GPU_PLL);

	if (ss_info_num) {
		struct spread_spectrum_info info = { { 0 } };
		enum bp_result result = bp->funcs->get_spread_spectrum_info(
				bp, AS_SIGNAL_TYPE_GPU_PLL, 0, &info);

		/* Based on VBIOS, VBIOS will keep entry for GPU PLL SS
		 * even if SS not enabled and in that case
		 * SSInfo.spreadSpectrumPercentage !=0 would be sign
		 * that SS is enabled
		 */
		if (result == BP_RESULT_OK &&
				info.spread_spectrum_percentage != 0) {
			clk_mgr_dce->ss_on_dprefclk = true;
			clk_mgr_dce->dprefclk_ss_divider = info.spread_percentage_divider;

			if (info.type.CENTER_MODE == 0) {
				/* TODO: Currently for DP Reference clock we
				 * need only SS percentage for
				 * downspread */
				clk_mgr_dce->dprefclk_ss_percentage =
						info.spread_spectrum_percentage;
			}

			return;
		}

		result = bp->funcs->get_spread_spectrum_info(
				bp, AS_SIGNAL_TYPE_DISPLAY_PORT, 0, &info);

		/* Based on VBIOS, VBIOS will keep entry for DPREFCLK SS
		 * even if SS not enabled and in that case
		 * SSInfo.spreadSpectrumPercentage !=0 would be sign
		 * that SS is enabled
		 */
		if (result == BP_RESULT_OK &&
				info.spread_spectrum_percentage != 0) {
			clk_mgr_dce->ss_on_dprefclk = true;
			clk_mgr_dce->dprefclk_ss_divider = info.spread_percentage_divider;

			if (info.type.CENTER_MODE == 0) {
				/* Currently for DP Reference clock we
				 * need only SS percentage for
				 * downspread */
				clk_mgr_dce->dprefclk_ss_percentage =
						info.spread_spectrum_percentage;
			}
		}
	}
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
			stream->sink->link->ddi_channel_mapping.raw;
		cfg->transmitter =
			stream->sink->link->link_enc->transmitter;
		cfg->link_settings.lane_count =
			stream->sink->link->cur_link_settings.lane_count;
		cfg->link_settings.link_rate =
			stream->sink->link->cur_link_settings.link_rate;
		cfg->link_settings.link_spread =
			stream->sink->link->cur_link_settings.link_spread;
		cfg->sym_clock = stream->phy_pix_clk;
		/* Round v_refresh*/
		cfg->v_refresh = stream->timing.pix_clk_khz * 1000;
		cfg->v_refresh /= stream->timing.h_total;
		cfg->v_refresh = (cfg->v_refresh + stream->timing.v_total / 2)
							/ stream->timing.v_total;
	}

	pp_display_cfg->display_count = num_cfgs;
}

static uint32_t dce110_get_min_vblank_time_us(const struct dc_state *context)
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
			* 1000 / stream->timing.pix_clk_khz;

		if (min_vertical_blank_time > vertical_blank_time)
			min_vertical_blank_time = vertical_blank_time;
	}

	return min_vertical_blank_time;
}

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

static void dce_pplib_apply_display_requirements(
	struct dc *dc,
	struct dc_state *context)
{
	struct dm_pp_display_configuration *pp_display_cfg = &context->pp_display_cfg;

	pp_display_cfg->avail_mclk_switch_time_us = dce110_get_min_vblank_time_us(context);

	dce110_fill_display_configs(context, pp_display_cfg);

	if (memcmp(&dc->current_state->pp_display_cfg, pp_display_cfg, sizeof(*pp_display_cfg)) !=  0)
		dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);
}

static void dce11_pplib_apply_display_requirements(
	struct dc *dc,
	struct dc_state *context)
{
	struct dm_pp_display_configuration *pp_display_cfg = &context->pp_display_cfg;

	pp_display_cfg->all_displays_in_sync =
		context->bw.dce.all_displays_in_sync;
	pp_display_cfg->nb_pstate_switch_disable =
			context->bw.dce.nbp_state_change_enable == false;
	pp_display_cfg->cpu_cc6_disable =
			context->bw.dce.cpuc_state_change_enable == false;
	pp_display_cfg->cpu_pstate_disable =
			context->bw.dce.cpup_state_change_enable == false;
	pp_display_cfg->cpu_pstate_separation_time =
			context->bw.dce.blackout_recovery_time_us;

	pp_display_cfg->min_memory_clock_khz = context->bw.dce.yclk_khz
		/ MEMORY_TYPE_MULTIPLIER_CZ;

	pp_display_cfg->min_engine_clock_khz = determine_sclk_from_bounding_box(
			dc,
			context->bw.dce.sclk_khz);

	pp_display_cfg->min_dcfclock_khz = pp_display_cfg->min_engine_clock_khz;

	pp_display_cfg->min_engine_clock_deep_sleep_khz
			= context->bw.dce.sclk_deep_sleep_khz;

	pp_display_cfg->avail_mclk_switch_time_us =
						dce110_get_min_vblank_time_us(context);
	/* TODO: dce11.2*/
	pp_display_cfg->avail_mclk_switch_time_in_disp_active_us = 0;

	pp_display_cfg->disp_clk_khz = dc->res_pool->clk_mgr->clks.dispclk_khz;

	dce110_fill_display_configs(context, pp_display_cfg);

	/* TODO: is this still applicable?*/
	if (pp_display_cfg->display_count == 1) {
		const struct dc_crtc_timing *timing =
			&context->streams[0]->timing;

		pp_display_cfg->crtc_index =
			pp_display_cfg->disp_configs[0].pipe_idx;
		pp_display_cfg->line_time_in_us = timing->h_total * 1000 / timing->pix_clk_khz;
	}

	if (memcmp(&dc->current_state->pp_display_cfg, pp_display_cfg, sizeof(*pp_display_cfg)) !=  0)
		dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);
}

static void dce_update_clocks(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);
	struct dm_pp_power_level_change_request level_change_req;
	int unpatched_disp_clk = context->bw.dce.dispclk_khz;

	/*TODO: W/A for dal3 linux, investigate why this works */
	if (!clk_mgr_dce->dfs_bypass_active)
		context->bw.dce.dispclk_khz = context->bw.dce.dispclk_khz * 115 / 100;

	level_change_req.power_level = dce_get_required_clocks_state(clk_mgr, context);
	/* get max clock state from PPLIB */
	if ((level_change_req.power_level < clk_mgr_dce->cur_min_clks_state && safe_to_lower)
			|| level_change_req.power_level > clk_mgr_dce->cur_min_clks_state) {
		if (dm_pp_apply_power_level_change_request(clk_mgr->ctx, &level_change_req))
			clk_mgr_dce->cur_min_clks_state = level_change_req.power_level;
	}

	if (should_set_clock(safe_to_lower, context->bw.dce.dispclk_khz, clk_mgr->clks.dispclk_khz)) {
		context->bw.dce.dispclk_khz = dce_set_clock(clk_mgr, context->bw.dce.dispclk_khz);
		clk_mgr->clks.dispclk_khz = context->bw.dce.dispclk_khz; 
	}
	dce_pplib_apply_display_requirements(clk_mgr->ctx->dc, context);

	context->bw.dce.dispclk_khz = unpatched_disp_clk;
}

static void dce11_update_clocks(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);
	struct dm_pp_power_level_change_request level_change_req;

	level_change_req.power_level = dce_get_required_clocks_state(clk_mgr, context);
	/* get max clock state from PPLIB */
	if ((level_change_req.power_level < clk_mgr_dce->cur_min_clks_state && safe_to_lower)
			|| level_change_req.power_level > clk_mgr_dce->cur_min_clks_state) {
		if (dm_pp_apply_power_level_change_request(clk_mgr->ctx, &level_change_req))
			clk_mgr_dce->cur_min_clks_state = level_change_req.power_level;
	}

	if (should_set_clock(safe_to_lower, context->bw.dce.dispclk_khz, clk_mgr->clks.dispclk_khz)) {
		context->bw.dce.dispclk_khz = dce_set_clock(clk_mgr, context->bw.dce.dispclk_khz);
		clk_mgr->clks.dispclk_khz = context->bw.dce.dispclk_khz;
	}
	dce11_pplib_apply_display_requirements(clk_mgr->ctx->dc, context);
}

static void dce112_update_clocks(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);
	struct dm_pp_power_level_change_request level_change_req;

	level_change_req.power_level = dce_get_required_clocks_state(clk_mgr, context);
	/* get max clock state from PPLIB */
	if ((level_change_req.power_level < clk_mgr_dce->cur_min_clks_state && safe_to_lower)
			|| level_change_req.power_level > clk_mgr_dce->cur_min_clks_state) {
		if (dm_pp_apply_power_level_change_request(clk_mgr->ctx, &level_change_req))
			clk_mgr_dce->cur_min_clks_state = level_change_req.power_level;
	}

	if (should_set_clock(safe_to_lower, context->bw.dce.dispclk_khz, clk_mgr->clks.dispclk_khz)) {
		context->bw.dce.dispclk_khz = dce112_set_clock(clk_mgr, context->bw.dce.dispclk_khz);
		clk_mgr->clks.dispclk_khz = context->bw.dce.dispclk_khz;
	}
	dce11_pplib_apply_display_requirements(clk_mgr->ctx->dc, context);
}

static void dce12_update_clocks(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(clk_mgr);
	struct dm_pp_clock_for_voltage_req clock_voltage_req = {0};
	int max_pix_clk = get_max_pixel_clock_for_all_paths(context);
	int unpatched_disp_clk = context->bw.dce.dispclk_khz;

	/*TODO: W/A for dal3 linux, investigate why this works */
	if (!clk_mgr_dce->dfs_bypass_active)
		context->bw.dce.dispclk_khz = context->bw.dce.dispclk_khz * 115 / 100;

	if (should_set_clock(safe_to_lower, context->bw.dce.dispclk_khz, clk_mgr->clks.dispclk_khz)) {
		clock_voltage_req.clk_type = DM_PP_CLOCK_TYPE_DISPLAY_CLK;
		clock_voltage_req.clocks_in_khz = context->bw.dce.dispclk_khz;
		context->bw.dce.dispclk_khz = dce112_set_clock(clk_mgr, context->bw.dce.dispclk_khz);
		clk_mgr->clks.dispclk_khz = context->bw.dce.dispclk_khz;

		dm_pp_apply_clock_for_voltage_request(clk_mgr->ctx, &clock_voltage_req);
	}

	if (should_set_clock(safe_to_lower, max_pix_clk, clk_mgr->clks.phyclk_khz)) {
		clock_voltage_req.clk_type = DM_PP_CLOCK_TYPE_DISPLAYPHYCLK;
		clock_voltage_req.clocks_in_khz = max_pix_clk;
		clk_mgr->clks.phyclk_khz = max_pix_clk;

		dm_pp_apply_clock_for_voltage_request(clk_mgr->ctx, &clock_voltage_req);
	}
	dce11_pplib_apply_display_requirements(clk_mgr->ctx->dc, context);

	context->bw.dce.dispclk_khz = unpatched_disp_clk;
}

static const struct clk_mgr_funcs dce120_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.update_clocks = dce12_update_clocks
};

static const struct clk_mgr_funcs dce112_funcs = {
	.get_dp_ref_clk_frequency = dce_get_dp_ref_freq_khz,
	.update_clocks = dce112_update_clocks
};

static const struct clk_mgr_funcs dce110_funcs = {
	.get_dp_ref_clk_frequency = dce_get_dp_ref_freq_khz,
	.update_clocks = dce11_update_clocks,
};

static const struct clk_mgr_funcs dce_funcs = {
	.get_dp_ref_clk_frequency = dce_get_dp_ref_freq_khz,
	.update_clocks = dce_update_clocks
};

static void dce_clk_mgr_construct(
	struct dce_clk_mgr *clk_mgr_dce,
	struct dc_context *ctx,
	const struct clk_mgr_registers *regs,
	const struct clk_mgr_shift *clk_shift,
	const struct clk_mgr_mask *clk_mask)
{
	struct clk_mgr *base = &clk_mgr_dce->base;
	struct dm_pp_static_clock_info static_clk_info = {0};

	base->ctx = ctx;
	base->funcs = &dce_funcs;

	clk_mgr_dce->regs = regs;
	clk_mgr_dce->clk_mgr_shift = clk_shift;
	clk_mgr_dce->clk_mgr_mask = clk_mask;

	clk_mgr_dce->dfs_bypass_disp_clk = 0;

	clk_mgr_dce->dprefclk_ss_percentage = 0;
	clk_mgr_dce->dprefclk_ss_divider = 1000;
	clk_mgr_dce->ss_on_dprefclk = false;


	if (dm_pp_get_static_clocks(ctx, &static_clk_info))
		clk_mgr_dce->max_clks_state = static_clk_info.max_clocks_state;
	else
		clk_mgr_dce->max_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;
	clk_mgr_dce->cur_min_clks_state = DM_PP_CLOCKS_STATE_INVALID;

	dce_clock_read_integrated_info(clk_mgr_dce);
	dce_clock_read_ss_info(clk_mgr_dce);
}

struct clk_mgr *dce_clk_mgr_create(
	struct dc_context *ctx,
	const struct clk_mgr_registers *regs,
	const struct clk_mgr_shift *clk_shift,
	const struct clk_mgr_mask *clk_mask)
{
	struct dce_clk_mgr *clk_mgr_dce = kzalloc(sizeof(*clk_mgr_dce), GFP_KERNEL);

	if (clk_mgr_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_mgr_dce->max_clks_by_state,
		dce80_max_clks_by_state,
		sizeof(dce80_max_clks_by_state));

	dce_clk_mgr_construct(
		clk_mgr_dce, ctx, regs, clk_shift, clk_mask);

	return &clk_mgr_dce->base;
}

struct clk_mgr *dce110_clk_mgr_create(
	struct dc_context *ctx,
	const struct clk_mgr_registers *regs,
	const struct clk_mgr_shift *clk_shift,
	const struct clk_mgr_mask *clk_mask)
{
	struct dce_clk_mgr *clk_mgr_dce = kzalloc(sizeof(*clk_mgr_dce), GFP_KERNEL);

	if (clk_mgr_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_mgr_dce->max_clks_by_state,
		dce110_max_clks_by_state,
		sizeof(dce110_max_clks_by_state));

	dce_clk_mgr_construct(
		clk_mgr_dce, ctx, regs, clk_shift, clk_mask);

	clk_mgr_dce->base.funcs = &dce110_funcs;

	return &clk_mgr_dce->base;
}

struct clk_mgr *dce112_clk_mgr_create(
	struct dc_context *ctx,
	const struct clk_mgr_registers *regs,
	const struct clk_mgr_shift *clk_shift,
	const struct clk_mgr_mask *clk_mask)
{
	struct dce_clk_mgr *clk_mgr_dce = kzalloc(sizeof(*clk_mgr_dce), GFP_KERNEL);

	if (clk_mgr_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_mgr_dce->max_clks_by_state,
		dce112_max_clks_by_state,
		sizeof(dce112_max_clks_by_state));

	dce_clk_mgr_construct(
		clk_mgr_dce, ctx, regs, clk_shift, clk_mask);

	clk_mgr_dce->base.funcs = &dce112_funcs;

	return &clk_mgr_dce->base;
}

struct clk_mgr *dce120_clk_mgr_create(struct dc_context *ctx)
{
	struct dce_clk_mgr *clk_mgr_dce = kzalloc(sizeof(*clk_mgr_dce), GFP_KERNEL);

	if (clk_mgr_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_mgr_dce->max_clks_by_state,
		dce120_max_clks_by_state,
		sizeof(dce120_max_clks_by_state));

	dce_clk_mgr_construct(
		clk_mgr_dce, ctx, NULL, NULL, NULL);

	clk_mgr_dce->dprefclk_khz = 600000;
	clk_mgr_dce->base.funcs = &dce120_funcs;

	return &clk_mgr_dce->base;
}

void dce_clk_mgr_destroy(struct clk_mgr **clk_mgr)
{
	struct dce_clk_mgr *clk_mgr_dce = TO_DCE_CLK_MGR(*clk_mgr);

	kfree(clk_mgr_dce);
	*clk_mgr = NULL;
}
