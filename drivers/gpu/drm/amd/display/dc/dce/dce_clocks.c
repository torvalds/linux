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
#include "fixed32_32.h"
#include "bios_parser_interface.h"
#include "dc.h"

#define TO_DCE_CLOCKS(clocks)\
	container_of(clocks, struct dce_disp_clk, base)

#define REG(reg) \
	(clk_dce->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	clk_dce->clk_shift->field_name, clk_dce->clk_mask->field_name

#define CTX \
	clk_dce->base.ctx

/* Max clock values for each state indexed by "enum clocks_state": */
static struct state_dependent_clocks dce80_max_clks_by_state[] = {
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

static struct state_dependent_clocks dce110_max_clks_by_state[] = {
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

static struct state_dependent_clocks dce112_max_clks_by_state[] = {
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

static struct state_dependent_clocks dce120_max_clks_by_state[] = {
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

/* Starting point for each divider range.*/
enum dce_divider_range_start {
	DIVIDER_RANGE_01_START = 200, /* 2.00*/
	DIVIDER_RANGE_02_START = 1600, /* 16.00*/
	DIVIDER_RANGE_03_START = 3200, /* 32.00*/
	DIVIDER_RANGE_SCALE_FACTOR = 100 /* Results are scaled up by 100.*/
};

/* Ranges for divider identifiers (Divider ID or DID)
 mmDENTIST_DISPCLK_CNTL.DENTIST_DISPCLK_WDIVIDER*/
enum dce_divider_id_register_setting {
	DIVIDER_RANGE_01_BASE_DIVIDER_ID = 0X08,
	DIVIDER_RANGE_02_BASE_DIVIDER_ID = 0X40,
	DIVIDER_RANGE_03_BASE_DIVIDER_ID = 0X60,
	DIVIDER_RANGE_MAX_DIVIDER_ID = 0X80
};

/* Step size between each divider within a range.
 Incrementing the DENTIST_DISPCLK_WDIVIDER by one
 will increment the divider by this much.*/
enum dce_divider_range_step_size {
	DIVIDER_RANGE_01_STEP_SIZE = 25, /* 0.25*/
	DIVIDER_RANGE_02_STEP_SIZE = 50, /* 0.50*/
	DIVIDER_RANGE_03_STEP_SIZE = 100 /* 1.00 */
};

static bool dce_divider_range_construct(
	struct dce_divider_range *div_range,
	int range_start,
	int range_step,
	int did_min,
	int did_max)
{
	div_range->div_range_start = range_start;
	div_range->div_range_step = range_step;
	div_range->did_min = did_min;
	div_range->did_max = did_max;

	if (div_range->div_range_step == 0) {
		div_range->div_range_step = 1;
		/*div_range_step cannot be zero*/
		BREAK_TO_DEBUGGER();
	}
	/* Calculate this based on the other inputs.*/
	/* See DividerRange.h for explanation of */
	/* the relationship between divider id (DID) and a divider.*/
	/* Number of Divider IDs = (Maximum Divider ID - Minimum Divider ID)*/
	/* Maximum divider identified in this range =
	 * (Number of Divider IDs)*Step size between dividers
	 *  + The start of this range.*/
	div_range->div_range_end = (did_max - did_min) * range_step
		+ range_start;
	return true;
}

static int dce_divider_range_calc_divider(
	struct dce_divider_range *div_range,
	int did)
{
	/* Is this DID within our range?*/
	if ((did < div_range->did_min) || (did >= div_range->did_max))
		return INVALID_DIVIDER;

	return ((did - div_range->did_min) * div_range->div_range_step)
			+ div_range->div_range_start;

}

static int dce_divider_range_get_divider(
	struct dce_divider_range *div_range,
	int ranges_num,
	int did)
{
	int div = INVALID_DIVIDER;
	int i;

	for (i = 0; i < ranges_num; i++) {
		/* Calculate divider with given divider ID*/
		div = dce_divider_range_calc_divider(&div_range[i], did);
		/* Found a valid return divider*/
		if (div != INVALID_DIVIDER)
			break;
	}
	return div;
}

static int dce_clocks_get_dp_ref_freq(struct display_clock *clk)
{
	struct dce_disp_clk *clk_dce = TO_DCE_CLOCKS(clk);
	int dprefclk_wdivider;
	int dprefclk_src_sel;
	int dp_ref_clk_khz = 600000;
	int target_div = INVALID_DIVIDER;

	/* ASSERT DP Reference Clock source is from DFS*/
	REG_GET(DPREFCLK_CNTL, DPREFCLK_SRC_SEL, &dprefclk_src_sel);
	ASSERT(dprefclk_src_sel == 0);

	/* Read the mmDENTIST_DISPCLK_CNTL to get the currently
	 * programmed DID DENTIST_DPREFCLK_WDIVIDER*/
	REG_GET(DENTIST_DISPCLK_CNTL, DENTIST_DPREFCLK_WDIVIDER, &dprefclk_wdivider);

	/* Convert DENTIST_DPREFCLK_WDIVIDERto actual divider*/
	target_div = dce_divider_range_get_divider(
			clk_dce->divider_ranges,
			DIVIDER_RANGE_MAX,
			dprefclk_wdivider);

	if (target_div != INVALID_DIVIDER) {
		/* Calculate the current DFS clock, in kHz.*/
		dp_ref_clk_khz = (DIVIDER_RANGE_SCALE_FACTOR
			* clk_dce->dentist_vco_freq_khz) / target_div;
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
	if (clk_dce->ss_on_dprefclk && clk_dce->dprefclk_ss_divider != 0) {
		struct fixed32_32 ss_percentage = dal_fixed32_32_div_int(
				dal_fixed32_32_from_fraction(
						clk_dce->dprefclk_ss_percentage,
						clk_dce->dprefclk_ss_divider), 200);
		struct fixed32_32 adj_dp_ref_clk_khz;

		ss_percentage = dal_fixed32_32_sub(dal_fixed32_32_one,
								ss_percentage);
		adj_dp_ref_clk_khz =
			dal_fixed32_32_mul_int(
				ss_percentage,
				dp_ref_clk_khz);
		dp_ref_clk_khz = dal_fixed32_32_floor(adj_dp_ref_clk_khz);
	}

	return dp_ref_clk_khz;
}

static enum dm_pp_clocks_state dce_get_required_clocks_state(
	struct display_clock *clk,
	struct state_dependent_clocks *req_clocks)
{
	struct dce_disp_clk *clk_dce = TO_DCE_CLOCKS(clk);
	int i;
	enum dm_pp_clocks_state low_req_clk;

	/* Iterate from highest supported to lowest valid state, and update
	 * lowest RequiredState with the lowest state that satisfies
	 * all required clocks
	 */
	for (i = clk->max_clks_state; i >= DM_PP_CLOCKS_STATE_ULTRA_LOW; i--)
		if (req_clocks->display_clk_khz >
				clk_dce->max_clks_by_state[i].display_clk_khz
			|| req_clocks->pixel_clk_khz >
				clk_dce->max_clks_by_state[i].pixel_clk_khz)
			break;

	low_req_clk = i + 1;
	if (low_req_clk > clk->max_clks_state) {
		dm_logger_write(clk->ctx->logger, LOG_WARNING,
				"%s: clocks unsupported", __func__);
		low_req_clk = DM_PP_CLOCKS_STATE_INVALID;
	}

	return low_req_clk;
}

static bool dce_clock_set_min_clocks_state(
	struct display_clock *clk,
	enum dm_pp_clocks_state clocks_state)
{
	struct dm_pp_power_level_change_request level_change_req = {
			clocks_state };

	if (clocks_state > clk->max_clks_state) {
		/*Requested state exceeds max supported state.*/
		dm_logger_write(clk->ctx->logger, LOG_WARNING,
				"Requested state exceeds max supported state");
		return false;
	} else if (clocks_state == clk->cur_min_clks_state) {
		/*if we're trying to set the same state, we can just return
		 * since nothing needs to be done*/
		return true;
	}

	/* get max clock state from PPLIB */
	if (dm_pp_apply_power_level_change_request(clk->ctx, &level_change_req))
		clk->cur_min_clks_state = clocks_state;

	return true;
}

static void dce_set_clock(
	struct display_clock *clk,
	int requested_clk_khz)
{
	struct dce_disp_clk *clk_dce = TO_DCE_CLOCKS(clk);
	struct bp_pixel_clock_parameters pxl_clk_params = { 0 };
	struct dc_bios *bp = clk->ctx->dc_bios;

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
	}

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		clk->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;
}

#define PSR_SET_WAITLOOP 0x31

union dce110_dmcu_psr_config_data_wait_loop_reg1 {
	struct {
		unsigned int wait_loop:16; /* [15:0] */
		unsigned int reserved:16; /* [31:16] */
	} bits;
	unsigned int u32;
};

static void dce_psr_wait_loop(
		struct dce_disp_clk *clk_dce, unsigned int display_clk_khz)
{
	struct dc_context *ctx = clk_dce->base.ctx;
	union dce110_dmcu_psr_config_data_wait_loop_reg1 masterCmdData1;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 100, 100);

	masterCmdData1.u32 = 0;
	masterCmdData1.bits.wait_loop = display_clk_khz / 1000 / 7;
	dm_write_reg(ctx, REG(MASTER_COMM_DATA_REG1), masterCmdData1.u32);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, PSR_SET_WAITLOOP);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
}

static void dce_psr_set_clock(
	struct display_clock *clk,
	int requested_clk_khz)
{
	struct dce_disp_clk *clk_dce = TO_DCE_CLOCKS(clk);

	dce_set_clock(clk, requested_clk_khz);
	dce_psr_wait_loop(clk_dce, requested_clk_khz);
}

static void dce112_set_clock(
	struct display_clock *clk,
	int requested_clk_khz)
{
	struct dce_disp_clk *clk_dce = TO_DCE_CLOCKS(clk);
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = clk->ctx->dc_bios;

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

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		clk->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;

	/*Program DP ref Clock*/
	/*VBIOS will determine DPREFCLK frequency, so we don't set it*/
	dce_clk_params.target_clock_frequency = 0;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DPREFCLK;
	dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK =
			(dce_clk_params.pll_id ==
					CLOCK_SOURCE_COMBO_DISPLAY_PLL0);

	bp->funcs->set_dce_clock(bp, &dce_clk_params);

}

static void dce_clock_read_integrated_info(struct dce_disp_clk *clk_dce)
{
	struct dc_debug *debug = &clk_dce->base.ctx->dc->debug;
	struct dc_bios *bp = clk_dce->base.ctx->dc_bios;
	struct integrated_info info = { { { 0 } } };
	struct firmware_info fw_info = { { 0 } };
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

	clk_dce->use_max_disp_clk = debug->max_disp_clk;
}

static void dce_clock_read_ss_info(struct dce_disp_clk *clk_dce)
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

static bool dce_apply_clock_voltage_request(
	struct display_clock *clk,
	enum dm_pp_clock_type clocks_type,
	int clocks_in_khz,
	bool pre_mode_set,
	bool update_dp_phyclk)
{
	bool send_request = false;
	struct dm_pp_clock_for_voltage_req clock_voltage_req = {0};

	switch (clocks_type) {
	case DM_PP_CLOCK_TYPE_DISPLAY_CLK:
	case DM_PP_CLOCK_TYPE_PIXELCLK:
	case DM_PP_CLOCK_TYPE_DISPLAYPHYCLK:
		break;
	default:
		BREAK_TO_DEBUGGER();
		return false;
	}

	clock_voltage_req.clk_type = clocks_type;
	clock_voltage_req.clocks_in_khz = clocks_in_khz;

	/* to pplib */
	if (pre_mode_set) {
		switch (clocks_type) {
		case DM_PP_CLOCK_TYPE_DISPLAY_CLK:
			if (clocks_in_khz > clk->cur_clocks_value.dispclk_in_khz) {
				clk->cur_clocks_value.dispclk_notify_pplib_done = true;
				send_request = true;
			} else
				clk->cur_clocks_value.dispclk_notify_pplib_done = false;
			/* no matter incrase or decrase clock, update current clock value */
			clk->cur_clocks_value.dispclk_in_khz = clocks_in_khz;
			break;
		case DM_PP_CLOCK_TYPE_PIXELCLK:
			if (clocks_in_khz > clk->cur_clocks_value.max_pixelclk_in_khz) {
				clk->cur_clocks_value.pixelclk_notify_pplib_done = true;
				send_request = true;
			} else
				clk->cur_clocks_value.pixelclk_notify_pplib_done = false;
			/* no matter incrase or decrase clock, update current clock value */
			clk->cur_clocks_value.max_pixelclk_in_khz = clocks_in_khz;
			break;
		case DM_PP_CLOCK_TYPE_DISPLAYPHYCLK:
			if (clocks_in_khz > clk->cur_clocks_value.max_non_dp_phyclk_in_khz) {
				clk->cur_clocks_value.phyclk_notigy_pplib_done = true;
				send_request = true;
			} else
				clk->cur_clocks_value.phyclk_notigy_pplib_done = false;
			/* no matter incrase or decrase clock, update current clock value */
			clk->cur_clocks_value.max_non_dp_phyclk_in_khz = clocks_in_khz;
			break;
		default:
			ASSERT(0);
			break;
		}

	} else {
		switch (clocks_type) {
		case DM_PP_CLOCK_TYPE_DISPLAY_CLK:
			if (!clk->cur_clocks_value.dispclk_notify_pplib_done)
				send_request = true;
			break;
		case DM_PP_CLOCK_TYPE_PIXELCLK:
			if (!clk->cur_clocks_value.pixelclk_notify_pplib_done)
				send_request = true;
			break;
		case DM_PP_CLOCK_TYPE_DISPLAYPHYCLK:
			if (!clk->cur_clocks_value.phyclk_notigy_pplib_done)
				send_request = true;
			break;
		default:
			ASSERT(0);
			break;
		}
	}
	if (send_request) {
		dm_pp_apply_clock_for_voltage_request(
			clk->ctx, &clock_voltage_req);
	}
	if (update_dp_phyclk && (clocks_in_khz >
	clk->cur_clocks_value.max_dp_phyclk_in_khz))
		clk->cur_clocks_value.max_dp_phyclk_in_khz = clocks_in_khz;

	return true;
}


static const struct display_clock_funcs dce120_funcs = {
	.get_dp_ref_clk_frequency = dce_clocks_get_dp_ref_freq,
	.apply_clock_voltage_request = dce_apply_clock_voltage_request,
	.set_clock = dce112_set_clock
};

static const struct display_clock_funcs dce112_funcs = {
	.get_dp_ref_clk_frequency = dce_clocks_get_dp_ref_freq,
	.get_required_clocks_state = dce_get_required_clocks_state,
	.set_min_clocks_state = dce_clock_set_min_clocks_state,
	.set_clock = dce112_set_clock
};

static const struct display_clock_funcs dce110_funcs = {
	.get_dp_ref_clk_frequency = dce_clocks_get_dp_ref_freq,
	.get_required_clocks_state = dce_get_required_clocks_state,
	.set_min_clocks_state = dce_clock_set_min_clocks_state,
	.set_clock = dce_psr_set_clock
};

static const struct display_clock_funcs dce_funcs = {
	.get_dp_ref_clk_frequency = dce_clocks_get_dp_ref_freq,
	.get_required_clocks_state = dce_get_required_clocks_state,
	.set_min_clocks_state = dce_clock_set_min_clocks_state,
	.set_clock = dce_set_clock
};

static void dce_disp_clk_construct(
	struct dce_disp_clk *clk_dce,
	struct dc_context *ctx,
	const struct dce_disp_clk_registers *regs,
	const struct dce_disp_clk_shift *clk_shift,
	const struct dce_disp_clk_mask *clk_mask)
{
	struct display_clock *base = &clk_dce->base;

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

	dce_divider_range_construct(
		&clk_dce->divider_ranges[DIVIDER_RANGE_01],
		DIVIDER_RANGE_01_START,
		DIVIDER_RANGE_01_STEP_SIZE,
		DIVIDER_RANGE_01_BASE_DIVIDER_ID,
		DIVIDER_RANGE_02_BASE_DIVIDER_ID);
	dce_divider_range_construct(
		&clk_dce->divider_ranges[DIVIDER_RANGE_02],
		DIVIDER_RANGE_02_START,
		DIVIDER_RANGE_02_STEP_SIZE,
		DIVIDER_RANGE_02_BASE_DIVIDER_ID,
		DIVIDER_RANGE_03_BASE_DIVIDER_ID);
	dce_divider_range_construct(
		&clk_dce->divider_ranges[DIVIDER_RANGE_03],
		DIVIDER_RANGE_03_START,
		DIVIDER_RANGE_03_STEP_SIZE,
		DIVIDER_RANGE_03_BASE_DIVIDER_ID,
		DIVIDER_RANGE_MAX_DIVIDER_ID);
}

struct display_clock *dce_disp_clk_create(
	struct dc_context *ctx,
	const struct dce_disp_clk_registers *regs,
	const struct dce_disp_clk_shift *clk_shift,
	const struct dce_disp_clk_mask *clk_mask)
{
	struct dce_disp_clk *clk_dce = dm_alloc(sizeof(*clk_dce));

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_dce->max_clks_by_state,
		dce80_max_clks_by_state,
		sizeof(dce80_max_clks_by_state));

	dce_disp_clk_construct(
		clk_dce, ctx, regs, clk_shift, clk_mask);

	return &clk_dce->base;
}

struct display_clock *dce110_disp_clk_create(
	struct dc_context *ctx,
	const struct dce_disp_clk_registers *regs,
	const struct dce_disp_clk_shift *clk_shift,
	const struct dce_disp_clk_mask *clk_mask)
{
	struct dce_disp_clk *clk_dce = dm_alloc(sizeof(*clk_dce));

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_dce->max_clks_by_state,
		dce110_max_clks_by_state,
		sizeof(dce110_max_clks_by_state));

	dce_disp_clk_construct(
		clk_dce, ctx, regs, clk_shift, clk_mask);

	clk_dce->base.funcs = &dce110_funcs;

	return &clk_dce->base;
}

struct display_clock *dce112_disp_clk_create(
	struct dc_context *ctx,
	const struct dce_disp_clk_registers *regs,
	const struct dce_disp_clk_shift *clk_shift,
	const struct dce_disp_clk_mask *clk_mask)
{
	struct dce_disp_clk *clk_dce = dm_alloc(sizeof(*clk_dce));

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_dce->max_clks_by_state,
		dce112_max_clks_by_state,
		sizeof(dce112_max_clks_by_state));

	dce_disp_clk_construct(
		clk_dce, ctx, regs, clk_shift, clk_mask);

	clk_dce->base.funcs = &dce112_funcs;

	return &clk_dce->base;
}

struct display_clock *dce120_disp_clk_create(
		struct dc_context *ctx,
		const struct dce_disp_clk_registers *regs,
		const struct dce_disp_clk_shift *clk_shift,
		const struct dce_disp_clk_mask *clk_mask)
{
	struct dce_disp_clk *clk_dce = dm_alloc(sizeof(*clk_dce));
	struct dm_pp_clock_levels_with_voltage clk_level_info = {0};

	if (clk_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	memcpy(clk_dce->max_clks_by_state,
		dce120_max_clks_by_state,
		sizeof(dce120_max_clks_by_state));

	dce_disp_clk_construct(
		clk_dce, ctx, regs, clk_shift, clk_mask);

	clk_dce->base.funcs = &dce120_funcs;

	/* new in dce120 */
	if (!ctx->dc->debug.disable_pplib_clock_request  &&
			dm_pp_get_clock_levels_by_type_with_voltage(
			ctx, DM_PP_CLOCK_TYPE_DISPLAY_CLK, &clk_level_info)
						&& clk_level_info.num_levels)
		clk_dce->max_displ_clk_in_khz =
			clk_level_info.data[clk_level_info.num_levels - 1].clocks_in_khz;
	else
		clk_dce->max_displ_clk_in_khz = 1133000;

	return &clk_dce->base;
}

void dce_disp_clk_destroy(struct display_clock **disp_clk)
{
	struct dce_disp_clk *clk_dce = TO_DCE_CLOCKS(*disp_clk);

	dm_free(clk_dce);
	*disp_clk = NULL;
}
