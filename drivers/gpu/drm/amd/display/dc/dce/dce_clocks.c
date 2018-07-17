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

#include "dce_clocks.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "fixed31_32.h"
#include "bios_parser_interface.h"
#include "dc.h"
#include "dmcu.h"
#ifdef CONFIG_X86
#include "dcn_calcs.h"
#endif
#include "core_types.h"
#include "dc_types.h"
#include "dal_asic_id.h"

#define TO_DCE_CLOCKS(clocks)\
	container_of(clocks, struct dce_dccg, base)

#define REG(reg) \
	(clk_dce->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	clk_dce->clk_shift->field_name, clk_dce->clk_mask->field_name

#define CTX \
	clk_dce->base.ctx
#define DC_LOGGER \
	clk->ctx->logger

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

/* Starting DID for each range */
enum dentist_base_divider_id {
	DENTIST_BASE_DID_1 = 0x08,
	DENTIST_BASE_DID_2 = 0x40,
	DENTIST_BASE_DID_3 = 0x60,
	DENTIST_MAX_DID    = 0x80
};

/* Starting point and step size for each divider range.*/
enum dentist_divider_range {
	DENTIST_DIVIDER_RANGE_1_START = 8,   /* 2.00  */
	DENTIST_DIVIDER_RANGE_1_STEP  = 1,   /* 0.25  */
	DENTIST_DIVIDER_RANGE_2_START = 64,  /* 16.00 */
	DENTIST_DIVIDER_RANGE_2_STEP  = 2,   /* 0.50  */
	DENTIST_DIVIDER_RANGE_3_START = 128, /* 32.00 */
	DENTIST_DIVIDER_RANGE_3_STEP  = 4,   /* 1.00  */
	DENTIST_DIVIDER_RANGE_SCALE_FACTOR = 4
};

static int dentist_get_divider_from_did(int did)
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
	} else {
		return DENTIST_DIVIDER_RANGE_3_START + DENTIST_DIVIDER_RANGE_3_STEP
							* (did - DENTIST_BASE_DID_3);
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
static int dccg_adjust_dp_ref_freq_for_ss(struct dce_dccg *clk_dce, int dp_ref_clk_khz)
{
	if (clk_dce->ss_on_dprefclk && clk_dce->dprefclk_ss_divider != 0) {
		struct fixed31_32 ss_percentage = dc_fixpt_div_int(
				dc_fixpt_from_fraction(clk_dce->dprefclk_ss_percentage,
							clk_dce->dprefclk_ss_divider), 200);
		struct fixed31_32 adj_dp_ref_clk_khz;

		ss_percentage = dc_fixpt_sub(dc_fixpt_one, ss_percentage);
		adj_dp_ref_clk_khz = dc_fixpt_mul_int(ss_percentage, dp_ref_clk_khz);
		dp_ref_clk_khz = dc_fixpt_floor(adj_dp_ref_clk_khz);
	}
	return dp_ref_clk_khz;
}

static int dce_get_dp_ref_freq_khz(struct dccg *clk)
{
	struct dce_dccg *clk_dce = TO_DCE_CLOCKS(clk);
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
		* clk_dce->dentist_vco_freq_khz) / target_div;

	return dccg_adjust_dp_ref_freq_for_ss(clk_dce, dp_ref_clk_khz);
}

static int dce12_get_dp_ref_freq_khz(struct dccg *clk)
{
	struct dce_dccg *clk_dce = TO_DCE_CLOCKS(clk);

	return dccg_adjust_dp_ref_freq_for_ss(clk_dce, 600000);
}

static enum dm_pp_clocks_state dce_get_required_clocks_state(
	struct dccg *clk,
	struct dc_clocks *req_clocks)
{
	struct dce_dccg *clk_dce = TO_DCE_CLOCKS(clk);
	int i;
	enum dm_pp_clocks_state low_req_clk;

	/* Iterate from highest supported to lowest valid state, and update
	 * lowest RequiredState with the lowest state that satisfies
	 * all required clocks
	 */
	for (i = clk->max_clks_state; i >= DM_PP_CLOCKS_STATE_ULTRA_LOW; i--)
		if (req_clocks->dispclk_khz >
				clk_dce->max_clks_by_state[i].display_clk_khz
			|| req_clocks->phyclk_khz >
				clk_dce->max_clks_by_state[i].pixel_clk_khz)
			break;

	low_req_clk = i + 1;
	if (low_req_clk > clk->max_clks_state) {
		/* set max clock state for high phyclock, invalid on exceeding display clock */
		if (clk_dce->max_clks_by_state[clk->max_clks_state].display_clk_khz
				< req_clocks->dispclk_khz)
			low_req_clk = DM_PP_CLOCKS_STATE_INVALID;
		else
			low_req_clk = clk->max_clks_state;
	}

	return low_req_clk;
}

static int dce_set_clock(
	struct dccg *clk,
	int requested_clk_khz)
{
	struct dce_dccg *clk_dce = TO_DCE_CLOCKS(clk);
	struct bp_pixel_clock_parameters pxl_clk_params = { 0 };
	struct dc_bios *bp = clk->ctx->dc_bios;
	int actual_clock = requested_clk_khz;

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = max(requested_clk_khz,
				clk_dce->dentist_vco_freq_khz / 64);

	/* Prepare to program display clock*/
	pxl_clk_params.target_pixel_clock = requested_clk_khz;
	pxl_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;

	bp->funcs->program_display_engine_pll(bp, &pxl_clk_params);

	if (clk_dce->dfs_bypass_enabled) {

		/* Cache the fixed display clock*/
		clk_dce->dfs_bypass_disp_clk =
			pxl_clk_params.dfs_bypass_display_clock;
		actual_clock = pxl_clk_params.dfs_bypass_display_clock;
	}

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		clk->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;
	return actual_clock;
}

static int dce_psr_set_clock(
	struct dccg *clk,
	int requested_clk_khz)
{
	struct dce_dccg *clk_dce = TO_DCE_CLOCKS(clk);
	struct dc_context *ctx = clk_dce->base.ctx;
	struct dc *core_dc = ctx->dc;
	struct dmcu *dmcu = core_dc->res_pool->dmcu;
	int actual_clk_khz = requested_clk_khz;

	actual_clk_khz = dce_set_clock(clk, requested_clk_khz);

	dmcu->funcs->set_psr_wait_loop(dmcu, actual_clk_khz / 1000 / 7);
	return actual_clk_khz;
}

static int dce112_set_clock(
	struct dccg *clk,
	int requested_clk_khz)
{
	struct dce_dccg *clk_dce = TO_DCE_CLOCKS(clk);
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = clk->ctx->dc_bios;
	struct dc *core_dc = clk->ctx->dc;
	struct dmcu *dmcu = core_dc->res_pool->dmcu;
	int actual_clock = requested_clk_khz;
	/* Prepare to program display clock*/
	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = max(requested_clk_khz,
				clk_dce->dentist_vco_freq_khz / 62);

	dce_clk_params.target_clock_frequency = requested_clk_khz;
	dce_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DISPLAY_CLOCK;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);
	actual_clock = dce_clk_params.target_clock_frequency;

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		clk->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;

	/*Program DP ref Clock*/
	/*VBIOS will determine DPREFCLK frequency, so we don't set it*/
	dce_clk_params.target_clock_frequency = 0;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DPREFCLK;
	if (!ASICREV_IS_VEGA20_P(clk->ctx->asic_id.hw_internal_rev))
		dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK =
			(dce_clk_params.pll_id ==
					CLOCK_SOURCE_COMBO_DISPLAY_PLL0);
	else
		dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK = false;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);

	if (!IS_FPGA_MAXIMUS_DC(core_dc->ctx->dce_environment)) {
		if (clk_dce->dfs_bypass_disp_clk != actual_clock)
			dmcu->funcs->set_psr_wait_loop(dmcu,
					actual_clock / 1000 / 7);
	}

	clk_dce->dfs_bypass_disp_clk = actual_clock;
	return actual_clock;
}

static void dce_clock_read_integrated_info(struct dce_dccg *clk_dce)
{
	struct dc_debug *debug = &clk_dce->base.ctx->dc->debug;
	struct dc_bios *bp = clk_dce->base.ctx->dc_bios;
	struct integrated_info info = { { { 0 } } };
	struct dc_firmware_info fw_info = { { 0 } };
	int i;

	if (bp->integrated_info)
		info = *bp->integrated_info;

	clk_dce->dentist_vco_freq_khz = info.dentist_vco_freq;
	if (clk_dce->dentist_vco_freq_khz == 0) {
		bp->funcs->get_firmware_info(bp, &fw_info);
		clk_dce->dentist_vco_freq_khz =
			fw_info.smu_gpu_pll_output_freq;
		if (clk_dce->dentist_vco_freq_khz == 0)
			clk_dce->dentist_vco_freq_khz = 3600000;
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
			clk_dce->max_clks_by_state[clk_state].display_clk_khz =
				info.disp_clk_voltage[i].max_supported_clk;
	}

	if (!debug->disable_dfs_bypass && bp->integrated_info)
		if (bp->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE)
			clk_dce->dfs_bypass_enabled = true;
}

static void dce_clock_read_ss_info(struct dce_dccg *clk_dce)
{
	struct dc_bios *bp = clk_dce->base.ctx->dc_bios;
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
			clk_dce->ss_on_dprefclk = true;
			clk_dce->dprefclk_ss_divider = info.spread_percentage_divider;

			if (info.type.CENTER_MODE == 0) {
				/* TODO: Currently for DP Reference clock we
				 * need only SS percentage for
				 * downspread */
				clk_dce->dprefclk_ss_percentage =
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
			clk_dce->ss_on_dprefclk = true;
			clk_dce->dprefclk_ss_divider = info.spread_percentage_divider;

			if (info.type.CENTER_MODE == 0) {
				/* Currently for DP Reference clock we
				 * need only SS percentage for
				 * downspread */
				clk_dce->dprefclk_ss_percentage =
						info.spread_spectrum_percentage;
			}
		}
	}
}

static inline bool should_set_clock(bool safe_to_lower, int calc_clk, int cur_clk)
{
	return ((safe_to_lower && calc_clk < cur_clk) || calc_clk > cur_clk);
}

static void dce12_update_clocks(struct dccg *dccg,
			struct dc_clocks *new_clocks,
			bool safe_to_lower)
{
	struct dm_pp_clock_for_voltage_req clock_voltage_req = {0};

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, dccg->clks.dispclk_khz)) {
		clock_voltage_req.clk_type = DM_PP_CLOCK_TYPE_DISPLAY_CLK;
		clock_voltage_req.clocks_in_khz = new_clocks->dispclk_khz;
		dccg->funcs->set_dispclk(dccg, new_clocks->dispclk_khz);
		dccg->clks.dispclk_khz = new_clocks->dispclk_khz;

		dm_pp_apply_clock_for_voltage_request(dccg->ctx, &clock_voltage_req);
	}

	if (should_set_clock(safe_to_lower, new_clocks->phyclk_khz, dccg->clks.phyclk_khz)) {
		clock_voltage_req.clk_type = DM_PP_CLOCK_TYPE_DISPLAYPHYCLK;
		clock_voltage_req.clocks_in_khz = new_clocks->phyclk_khz;
		dccg->clks.phyclk_khz = new_clocks->phyclk_khz;

		dm_pp_apply_clock_for_voltage_request(dccg->ctx, &clock_voltage_req);
	}
}

#ifdef CONFIG_X86
static int dcn1_determine_dppclk_threshold(struct dccg *dccg, struct dc_clocks *new_clocks)
{
	bool request_dpp_div = new_clocks->dispclk_khz > new_clocks->dppclk_khz;
	bool dispclk_increase = new_clocks->dispclk_khz > dccg->clks.dispclk_khz;
	int disp_clk_threshold = new_clocks->max_supported_dppclk_khz;
	bool cur_dpp_div = dccg->clks.dispclk_khz > dccg->clks.dppclk_khz;

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
		if (dccg->clks.dispclk_khz <= disp_clk_threshold)
			return new_clocks->dispclk_khz;

		/* request dpp clk need to be divided by 2 */
		if (request_dpp_div)
			return new_clocks->dispclk_khz;
	}

	return disp_clk_threshold;
}

static void dcn1_ramp_up_dispclk_with_dpp(struct dccg *dccg, struct dc_clocks *new_clocks)
{
	struct dc *dc = dccg->ctx->dc;
	int dispclk_to_dpp_threshold = dcn1_determine_dppclk_threshold(dccg, new_clocks);
	bool request_dpp_div = new_clocks->dispclk_khz > new_clocks->dppclk_khz;
	int i;

	/* set disp clk to dpp clk threshold */
	dccg->funcs->set_dispclk(dccg, dispclk_to_dpp_threshold);

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
		dccg->funcs->set_dispclk(dccg, new_clocks->dispclk_khz);

	dccg->clks.dispclk_khz = new_clocks->dispclk_khz;
	dccg->clks.dppclk_khz = new_clocks->dppclk_khz;
	dccg->clks.max_supported_dppclk_khz = new_clocks->max_supported_dppclk_khz;
}

static void dcn1_update_clocks(struct dccg *dccg,
			struct dc_clocks *new_clocks,
			bool safe_to_lower)
{
	struct dc *dc = dccg->ctx->dc;
	struct pp_smu_display_requirement_rv *smu_req_cur =
			&dc->res_pool->pp_smu_req;
	struct pp_smu_display_requirement_rv smu_req = *smu_req_cur;
	struct pp_smu_funcs_rv *pp_smu = dc->res_pool->pp_smu;
	struct dm_pp_clock_for_voltage_req clock_voltage_req = {0};
	bool send_request_to_increase = false;
	bool send_request_to_lower = false;

	if (new_clocks->phyclk_khz)
		smu_req.display_count = 1;
	else
		smu_req.display_count = 0;

	if (new_clocks->dispclk_khz > dccg->clks.dispclk_khz
			|| new_clocks->phyclk_khz > dccg->clks.phyclk_khz
			|| new_clocks->fclk_khz > dccg->clks.fclk_khz
			|| new_clocks->dcfclk_khz > dccg->clks.dcfclk_khz)
		send_request_to_increase = true;

	if (should_set_clock(safe_to_lower, new_clocks->phyclk_khz, dccg->clks.phyclk_khz)) {
		dccg->clks.phyclk_khz = new_clocks->phyclk_khz;

		send_request_to_lower = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->fclk_khz, dccg->clks.fclk_khz)) {
		dccg->clks.fclk_khz = new_clocks->fclk_khz;
		clock_voltage_req.clk_type = DM_PP_CLOCK_TYPE_FCLK;
		clock_voltage_req.clocks_in_khz = new_clocks->fclk_khz;
		smu_req.hard_min_fclk_khz = new_clocks->fclk_khz;

		dm_pp_apply_clock_for_voltage_request(dccg->ctx, &clock_voltage_req);
		send_request_to_lower = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, dccg->clks.dcfclk_khz)) {
		dccg->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		smu_req.hard_min_dcefclk_khz = new_clocks->dcfclk_khz;

		send_request_to_lower = true;
	}

	if (should_set_clock(safe_to_lower,
			new_clocks->dcfclk_deep_sleep_khz, dccg->clks.dcfclk_deep_sleep_khz)) {
		dccg->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		smu_req.min_deep_sleep_dcefclk_mhz = new_clocks->dcfclk_deep_sleep_khz;

		send_request_to_lower = true;
	}

	/* make sure dcf clk is before dpp clk to
	 * make sure we have enough voltage to run dpp clk
	 */
	if (send_request_to_increase) {
		/*use dcfclk to request voltage*/
		clock_voltage_req.clk_type = DM_PP_CLOCK_TYPE_DCFCLK;
		clock_voltage_req.clocks_in_khz = dcn_find_dcfclk_suits_all(dc, new_clocks);
		dm_pp_apply_clock_for_voltage_request(dccg->ctx, &clock_voltage_req);
		if (pp_smu->set_display_requirement)
			pp_smu->set_display_requirement(&pp_smu->pp_smu, &smu_req);
	}

	/* dcn1 dppclk is tied to dispclk */
	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, dccg->clks.dispclk_khz)) {
		dcn1_ramp_up_dispclk_with_dpp(dccg, new_clocks);
		dccg->clks.dispclk_khz = new_clocks->dispclk_khz;

		send_request_to_lower = true;
	}

	if (!send_request_to_increase && send_request_to_lower) {
		/*use dcfclk to request voltage*/
		clock_voltage_req.clk_type = DM_PP_CLOCK_TYPE_DCFCLK;
		clock_voltage_req.clocks_in_khz = dcn_find_dcfclk_suits_all(dc, new_clocks);
		dm_pp_apply_clock_for_voltage_request(dccg->ctx, &clock_voltage_req);
		if (pp_smu->set_display_requirement)
			pp_smu->set_display_requirement(&pp_smu->pp_smu, &smu_req);
	}


	*smu_req_cur = smu_req;
}
#endif

static void dce_update_clocks(struct dccg *dccg,
			struct dc_clocks *new_clocks,
			bool safe_to_lower)
{
	struct dm_pp_power_level_change_request level_change_req;

	level_change_req.power_level = dce_get_required_clocks_state(dccg, new_clocks);
	/* get max clock state from PPLIB */
	if ((level_change_req.power_level < dccg->cur_min_clks_state && safe_to_lower)
			|| level_change_req.power_level > dccg->cur_min_clks_state) {
		if (dm_pp_apply_power_level_change_request(dccg->ctx, &level_change_req))
			dccg->cur_min_clks_state = level_change_req.power_level;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, dccg->clks.dispclk_khz)) {
		dccg->funcs->set_dispclk(dccg, new_clocks->dispclk_khz);
		dccg->clks.dispclk_khz = new_clocks->dispclk_khz;
	}
}

#ifdef CONFIG_X86
static const struct display_clock_funcs dcn1_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.set_dispclk = dce112_set_clock,
	.update_clocks = dcn1_update_clocks
};
#endif

static const struct display_clock_funcs dce120_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.set_dispclk = dce112_set_clock,
	.update_clocks = dce12_update_clocks
};

static const struct display_clock_funcs dce112_funcs = {
	.get_dp_ref_clk_frequency = dce_get_dp_ref_freq_khz,
	.set_dispclk = dce112_set_clock,
	.update_clocks = dce_update_clocks
};

static const struct display_clock_funcs dce110_funcs = {
	.get_dp_ref_clk_frequency = dce_get_dp_ref_freq_khz,
	.set_dispclk = dce_psr_set_clock,
	.update_clocks = dce_update_clocks
};

static const struct display_clock_funcs dce_funcs = {
	.get_dp_ref_clk_frequency = dce_get_dp_ref_freq_khz,
	.set_dispclk = dce_set_clock,
	.update_clocks = dce_update_clocks
};

static void dce_dccg_construct(
	struct dce_dccg *clk_dce,
	struct dc_context *ctx,
	const struct dccg_registers *regs,
	const struct dccg_shift *clk_shift,
	const struct dccg_mask *clk_mask)
{
	struct dccg *base = &clk_dce->base;

	base->ctx = ctx;
	base->funcs = &dce_funcs;

	clk_dce->regs = regs;
	clk_dce->clk_shift = clk_shift;
	clk_dce->clk_mask = clk_mask;

	clk_dce->dfs_bypass_disp_clk = 0;

	clk_dce->dprefclk_ss_percentage = 0;
	clk_dce->dprefclk_ss_divider = 1000;
	clk_dce->ss_on_dprefclk = false;

	base->max_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;
	base->cur_min_clks_state = DM_PP_CLOCKS_STATE_INVALID;

	dce_clock_read_integrated_info(clk_dce);
	dce_clock_read_ss_info(clk_dce);
}

struct dccg *dce_dccg_create(
	struct dc_context *ctx,
	const struct dccg_registers *regs,
	const struct dccg_shift *clk_shift,
	const struct dccg_mask *clk_mask)
{
	struct dce_dccg *clk_dce = kzalloc(sizeof(*clk_dce), GFP_KERNEL);

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_dce->max_clks_by_state,
		dce80_max_clks_by_state,
		sizeof(dce80_max_clks_by_state));

	dce_dccg_construct(
		clk_dce, ctx, regs, clk_shift, clk_mask);

	return &clk_dce->base;
}

struct dccg *dce110_dccg_create(
	struct dc_context *ctx,
	const struct dccg_registers *regs,
	const struct dccg_shift *clk_shift,
	const struct dccg_mask *clk_mask)
{
	struct dce_dccg *clk_dce = kzalloc(sizeof(*clk_dce), GFP_KERNEL);

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_dce->max_clks_by_state,
		dce110_max_clks_by_state,
		sizeof(dce110_max_clks_by_state));

	dce_dccg_construct(
		clk_dce, ctx, regs, clk_shift, clk_mask);

	clk_dce->base.funcs = &dce110_funcs;

	return &clk_dce->base;
}

struct dccg *dce112_dccg_create(
	struct dc_context *ctx,
	const struct dccg_registers *regs,
	const struct dccg_shift *clk_shift,
	const struct dccg_mask *clk_mask)
{
	struct dce_dccg *clk_dce = kzalloc(sizeof(*clk_dce), GFP_KERNEL);

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_dce->max_clks_by_state,
		dce112_max_clks_by_state,
		sizeof(dce112_max_clks_by_state));

	dce_dccg_construct(
		clk_dce, ctx, regs, clk_shift, clk_mask);

	clk_dce->base.funcs = &dce112_funcs;

	return &clk_dce->base;
}

struct dccg *dce120_dccg_create(struct dc_context *ctx)
{
	struct dce_dccg *clk_dce = kzalloc(sizeof(*clk_dce), GFP_KERNEL);

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_dce->max_clks_by_state,
		dce120_max_clks_by_state,
		sizeof(dce120_max_clks_by_state));

	dce_dccg_construct(
		clk_dce, ctx, NULL, NULL, NULL);

	clk_dce->base.funcs = &dce120_funcs;

	return &clk_dce->base;
}

#ifdef CONFIG_X86
struct dccg *dcn1_dccg_create(struct dc_context *ctx)
{
	struct dc_debug *debug = &ctx->dc->debug;
	struct dc_bios *bp = ctx->dc_bios;
	struct dc_firmware_info fw_info = { { 0 } };
	struct dce_dccg *clk_dce = kzalloc(sizeof(*clk_dce), GFP_KERNEL);

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	clk_dce->base.ctx = ctx;
	clk_dce->base.funcs = &dcn1_funcs;

	clk_dce->dfs_bypass_disp_clk = 0;

	clk_dce->dprefclk_ss_percentage = 0;
	clk_dce->dprefclk_ss_divider = 1000;
	clk_dce->ss_on_dprefclk = false;

	if (bp->integrated_info)
		clk_dce->dentist_vco_freq_khz = bp->integrated_info->dentist_vco_freq;
	if (clk_dce->dentist_vco_freq_khz == 0) {
		bp->funcs->get_firmware_info(bp, &fw_info);
		clk_dce->dentist_vco_freq_khz = fw_info.smu_gpu_pll_output_freq;
		if (clk_dce->dentist_vco_freq_khz == 0)
			clk_dce->dentist_vco_freq_khz = 3600000;
	}

	if (!debug->disable_dfs_bypass && bp->integrated_info)
		if (bp->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE)
			clk_dce->dfs_bypass_enabled = true;

	dce_clock_read_ss_info(clk_dce);

	return &clk_dce->base;
}
#endif

void dce_dccg_destroy(struct dccg **dccg)
{
	struct dce_dccg *clk_dce = TO_DCE_CLOCKS(*dccg);

	kfree(clk_dce);
	*dccg = NULL;
}
