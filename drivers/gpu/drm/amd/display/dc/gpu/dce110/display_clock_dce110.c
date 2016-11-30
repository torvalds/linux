/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dm_services.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/bios_parser_interface.h"
#include "include/fixed32_32.h"
#include "include/logger_interface.h"

#include "../divider_range.h"

#include "display_clock_dce110.h"
#include "dc.h"

#define FROM_DISPLAY_CLOCK(base) \
	container_of(base, struct display_clock_dce110, disp_clk_base)

#define PSR_SET_WAITLOOP 0x31

static struct state_dependent_clocks max_clks_by_state[] = {
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

/* Starting point for each divider range.*/
enum divider_range_start {
	DIVIDER_RANGE_01_START = 200, /* 2.00*/
	DIVIDER_RANGE_02_START = 1600, /* 16.00*/
	DIVIDER_RANGE_03_START = 3200, /* 32.00*/
	DIVIDER_RANGE_SCALE_FACTOR = 100 /* Results are scaled up by 100.*/
};

/* Array identifiers and count for the divider ranges.*/
enum divider_range_count {
	DIVIDER_RANGE_01 = 0,
	DIVIDER_RANGE_02,
	DIVIDER_RANGE_03,
	DIVIDER_RANGE_MAX /* == 3*/
};

/* Ranges for divider identifiers (Divider ID or DID)
 mmDENTIST_DISPCLK_CNTL.DENTIST_DISPCLK_WDIVIDER*/
enum divider_id_register_setting {
	DIVIDER_RANGE_01_BASE_DIVIDER_ID = 0X08,
	DIVIDER_RANGE_02_BASE_DIVIDER_ID = 0X40,
	DIVIDER_RANGE_03_BASE_DIVIDER_ID = 0X60,
	DIVIDER_RANGE_MAX_DIVIDER_ID = 0X80
};

/* Step size between each divider within a range.
 Incrementing the DENTIST_DISPCLK_WDIVIDER by one
 will increment the divider by this much.*/
enum divider_range_step_size {
	DIVIDER_RANGE_01_STEP_SIZE = 25, /* 0.25*/
	DIVIDER_RANGE_02_STEP_SIZE = 50, /* 0.50*/
	DIVIDER_RANGE_03_STEP_SIZE = 100 /* 1.00 */
};

union dce110_dmcu_psr_config_data_wait_loop_reg1 {
	struct {
		unsigned int waitLoop:16; /* [15:0] */
		unsigned int reserved:16; /* [31:16] */
	} bits;
	unsigned int u32All;
};

static struct divider_range divider_ranges[DIVIDER_RANGE_MAX];

#define DCE110_DFS_BYPASS_THRESHOLD_KHZ 400000
/*****************************************************************************
 * static functions
 *****************************************************************************/

static bool dce110_set_min_clocks_state(
	struct display_clock *dc,
	enum clocks_state clocks_state)
{
	struct dm_pp_power_level_change_request level_change_req = {
			DM_PP_POWER_LEVEL_INVALID};

	if (clocks_state > dc->max_clks_state) {
		/*Requested state exceeds max supported state.*/
		dm_logger_write(dc->ctx->logger, LOG_WARNING,
				"Requested state exceeds max supported state");
		return false;
	} else if (clocks_state == dc->cur_min_clks_state) {
		/*if we're trying to set the same state, we can just return
		 * since nothing needs to be done*/
		return true;
	}

	switch (clocks_state) {
	case CLOCKS_STATE_ULTRA_LOW:
		level_change_req.power_level = DM_PP_POWER_LEVEL_ULTRA_LOW;
		break;
	case CLOCKS_STATE_LOW:
		level_change_req.power_level = DM_PP_POWER_LEVEL_LOW;
		break;
	case CLOCKS_STATE_NOMINAL:
		level_change_req.power_level = DM_PP_POWER_LEVEL_NOMINAL;
		break;
	case CLOCKS_STATE_PERFORMANCE:
		level_change_req.power_level = DM_PP_POWER_LEVEL_PERFORMANCE;
		break;
	case CLOCKS_DPM_STATE_LEVEL_4:
		level_change_req.power_level = DM_PP_POWER_LEVEL_4;
		break;
	case CLOCKS_DPM_STATE_LEVEL_5:
		level_change_req.power_level = DM_PP_POWER_LEVEL_5;
		break;
	case CLOCKS_DPM_STATE_LEVEL_6:
		level_change_req.power_level = DM_PP_POWER_LEVEL_6;
		break;
	case CLOCKS_DPM_STATE_LEVEL_7:
		level_change_req.power_level = DM_PP_POWER_LEVEL_7;
		break;
	case CLOCKS_STATE_INVALID:
	default:
		dm_logger_write(dc->ctx->logger, LOG_WARNING,
				"Requested state invalid state");
		return false;
	}

	/* get max clock state from PPLIB */
	if (dm_pp_apply_power_level_change_request(dc->ctx, &level_change_req))
		dc->cur_min_clks_state = clocks_state;

	return true;
}

static uint32_t get_dp_ref_clk_frequency(struct display_clock *dc)
{
	uint32_t dispclk_cntl_value;
	uint32_t dp_ref_clk_cntl_value;
	uint32_t dp_ref_clk_cntl_src_sel_value;
	uint32_t dp_ref_clk_khz = 600000;
	uint32_t target_div = INVALID_DIVIDER;
	struct display_clock_dce110 *disp_clk = FROM_DISPLAY_CLOCK(dc);

	/* ASSERT DP Reference Clock source is from DFS*/
	dp_ref_clk_cntl_value = dm_read_reg(dc->ctx,
			mmDPREFCLK_CNTL);

	dp_ref_clk_cntl_src_sel_value =
			get_reg_field_value(
				dp_ref_clk_cntl_value,
				DPREFCLK_CNTL, DPREFCLK_SRC_SEL);

	ASSERT(dp_ref_clk_cntl_src_sel_value == 0);

	/* Read the mmDENTIST_DISPCLK_CNTL to get the currently
	 * programmed DID DENTIST_DPREFCLK_WDIVIDER*/
	dispclk_cntl_value = dm_read_reg(dc->ctx,
			mmDENTIST_DISPCLK_CNTL);

	/* Convert DENTIST_DPREFCLK_WDIVIDERto actual divider*/
	target_div = dal_divider_range_get_divider(
		divider_ranges,
		DIVIDER_RANGE_MAX,
		get_reg_field_value(dispclk_cntl_value,
			DENTIST_DISPCLK_CNTL,
			DENTIST_DPREFCLK_WDIVIDER));

	if (target_div != INVALID_DIVIDER) {
		/* Calculate the current DFS clock, in kHz.*/
		dp_ref_clk_khz = (DIVIDER_RANGE_SCALE_FACTOR
			* disp_clk->dentist_vco_freq_khz) / target_div;
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
	if ((disp_clk->ss_on_gpu_pll) && (disp_clk->gpu_pll_ss_divider != 0)) {
		struct fixed32_32 ss_percentage = dal_fixed32_32_div_int(
				dal_fixed32_32_from_fraction(
					disp_clk->gpu_pll_ss_percentage,
					disp_clk->gpu_pll_ss_divider), 200);
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

static void destroy(struct display_clock **base)
{
	struct display_clock_dce110 *dc110;

	dc110 = DCLCK110_FROM_BASE(*base);

	dm_free(dc110);

	*base = NULL;
}

static bool display_clock_integrated_info_construct(
	struct display_clock_dce110 *disp_clk)
{
	struct dc_debug *debug = &disp_clk->disp_clk_base.ctx->dc->debug;
	struct dc_bios *bp = disp_clk->disp_clk_base.ctx->dc_bios;
	struct integrated_info info;
	struct firmware_info fw_info;
	uint32_t i;
	struct display_clock *base = &disp_clk->disp_clk_base;

	memset(&info, 0, sizeof(struct integrated_info));
	memset(&fw_info, 0, sizeof(struct firmware_info));

	if (bp->integrated_info)
		info = *bp->integrated_info;

	disp_clk->dentist_vco_freq_khz = info.dentist_vco_freq;
	if (disp_clk->dentist_vco_freq_khz == 0) {
		bp->funcs->get_firmware_info(bp, &fw_info);
		disp_clk->dentist_vco_freq_khz =
			fw_info.smu_gpu_pll_output_freq;
		if (disp_clk->dentist_vco_freq_khz == 0)
			disp_clk->dentist_vco_freq_khz = 3600000;
	}

	base->min_display_clk_threshold_khz =
		disp_clk->dentist_vco_freq_khz / 64;

	if (bp->integrated_info == NULL)
		return false;

	/*update the maximum display clock for each power state*/
	for (i = 0; i < NUMBER_OF_DISP_CLK_VOLTAGE; ++i) {
		enum clocks_state clk_state = CLOCKS_STATE_INVALID;

		switch (i) {
		case 0:
			clk_state = CLOCKS_STATE_ULTRA_LOW;
			break;

		case 1:
			clk_state = CLOCKS_STATE_LOW;
			break;

		case 2:
			clk_state = CLOCKS_STATE_NOMINAL;
			break;

		case 3:
			clk_state = CLOCKS_STATE_PERFORMANCE;
			break;

		default:
			clk_state = CLOCKS_STATE_INVALID;
			break;
		}

		/*Do not allow bad VBIOS/SBIOS to override with invalid values,
		 * check for > 100MHz*/
		if (info.disp_clk_voltage[i].max_supported_clk >= 100000) {
			max_clks_by_state[clk_state].display_clk_khz =
				info.disp_clk_voltage[i].max_supported_clk;
		}
	}

	disp_clk->dfs_bypass_enabled = false;
	if (!debug->disable_dfs_bypass)
		if (bp->integrated_info->gpu_cap_info & DFS_BYPASS_ENABLE)
			disp_clk->dfs_bypass_enabled = true;

	disp_clk->use_max_disp_clk = debug->max_disp_clk;

	return true;
}

static enum clocks_state get_required_clocks_state(
		struct display_clock *dc,
		struct state_dependent_clocks *req_clocks)
{
	int32_t i;
	struct display_clock_dce110 *disp_clk = DCLCK110_FROM_BASE(dc);
	enum clocks_state low_req_clk = dc->max_clks_state;

	if (!req_clocks) {
		/* NULL pointer*/
		dm_logger_write(dc->ctx->logger, LOG_WARNING,
				"%s: Invalid parameter",
				__func__);
		return CLOCKS_STATE_INVALID;
	}

	/* Iterate from highest supported to lowest valid state, and update
	 * lowest RequiredState with the lowest state that satisfies
	 * all required clocks
	 */
	for (i = dc->max_clks_state; i >= CLOCKS_STATE_ULTRA_LOW; --i) {
		if ((req_clocks->display_clk_khz <=
			max_clks_by_state[i].display_clk_khz) &&
			(req_clocks->pixel_clk_khz <=
				max_clks_by_state[i].pixel_clk_khz))
			low_req_clk = i;
	}
	return low_req_clk;
}

static void psr_wait_loop(struct dc_context *ctx, unsigned int display_clk_khz)
{
	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;
	unsigned int regValue;
	uint32_t masterCmd;
	uint32_t masterComCntl;
	union dce110_dmcu_psr_config_data_wait_loop_reg1 masterCmdData1;

	/* waitDMCUReadyForCmd */
	do {
		dm_delay_in_microseconds(ctx, dmcu_wait_reg_ready_interval);
		regValue = dm_read_reg(ctx, mmMASTER_COMM_CNTL_REG);
		dmcu_max_retry_on_wait_reg_ready--;
	} while
	/* expected value is 0, loop while not 0*/
	((MASTER_COMM_CNTL_REG__MASTER_COMM_INTERRUPT_MASK & regValue) &&
		dmcu_max_retry_on_wait_reg_ready > 0);

	masterCmdData1.u32All = 0;
	masterCmdData1.bits.waitLoop = display_clk_khz / 1000 / 7;
	dm_write_reg(ctx, mmMASTER_COMM_DATA_REG1, masterCmdData1.u32All);

	/* setDMCUParam_Cmd */
	masterCmd = dm_read_reg(ctx, mmMASTER_COMM_CMD_REG);
	set_reg_field_value(
		masterCmd,
		PSR_SET_WAITLOOP,
		MASTER_COMM_CMD_REG,
		MASTER_COMM_CMD_REG_BYTE0);

	dm_write_reg(ctx, mmMASTER_COMM_CMD_REG, masterCmd);

	/* notifyDMCUMsg */
	masterComCntl = dm_read_reg(ctx, mmMASTER_COMM_CNTL_REG);
	set_reg_field_value(
		masterComCntl,
		1,
		MASTER_COMM_CNTL_REG,
		MASTER_COMM_INTERRUPT);
	dm_write_reg(ctx, mmMASTER_COMM_CNTL_REG, masterComCntl);
}

static void dce110_set_clock(
	struct display_clock *base,
	uint32_t requested_clk_khz)
{
	struct bp_pixel_clock_parameters pxl_clk_params;
	struct display_clock_dce110 *dc = DCLCK110_FROM_BASE(base);
	struct dc_bios *bp = base->ctx->dc_bios;

	/* Prepare to program display clock*/
	memset(&pxl_clk_params, 0, sizeof(pxl_clk_params));

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = dm_max(requested_clk_khz,
				base->min_display_clk_threshold_khz);

	pxl_clk_params.target_pixel_clock = requested_clk_khz;
	pxl_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;

	bp->funcs->program_display_engine_pll(bp, &pxl_clk_params);

	if (dc->dfs_bypass_enabled) {

		/* Cache the fixed display clock*/
		dc->dfs_bypass_disp_clk =
			pxl_clk_params.dfs_bypass_display_clock;
	}

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		base->cur_min_clks_state = CLOCKS_STATE_NOMINAL;

	psr_wait_loop(base->ctx, requested_clk_khz);
}


static const struct display_clock_funcs funcs = {
	.destroy = destroy,
	.get_dp_ref_clk_frequency = get_dp_ref_clk_frequency,
	.get_required_clocks_state = get_required_clocks_state,
	.set_clock = dce110_set_clock,
	.set_min_clocks_state = dce110_set_min_clocks_state
};

static bool dal_display_clock_dce110_construct(
	struct display_clock_dce110 *dc110,
	struct dc_context *ctx)
{
	struct display_clock *dc_base = &dc110->disp_clk_base;
	struct dc_bios *bp = ctx->dc_bios;

	dc_base->ctx = ctx;
	dc_base->min_display_clk_threshold_khz = 0;

	dc_base->cur_min_clks_state = CLOCKS_STATE_INVALID;

	dc_base->funcs = &funcs;

	dc110->dfs_bypass_disp_clk = 0;

	if (!display_clock_integrated_info_construct(dc110))
		dm_logger_write(dc_base->ctx->logger, LOG_WARNING,
			"Cannot obtain VBIOS integrated info\n");

	dc110->gpu_pll_ss_percentage = 0;
	dc110->gpu_pll_ss_divider = 1000;
	dc110->ss_on_gpu_pll = false;

/* Initially set max clocks state to nominal.  This should be updated by
 * via a pplib call to DAL IRI eventually calling a
 * DisplayEngineClock_Dce110::StoreMaxClocksState().  This call will come in
 * on PPLIB init. This is from DCE5x. in case HW wants to use mixed method.*/
	dc_base->max_clks_state = CLOCKS_STATE_NOMINAL;

	dal_divider_range_construct(
		&divider_ranges[DIVIDER_RANGE_01],
		DIVIDER_RANGE_01_START,
		DIVIDER_RANGE_01_STEP_SIZE,
		DIVIDER_RANGE_01_BASE_DIVIDER_ID,
		DIVIDER_RANGE_02_BASE_DIVIDER_ID);
	dal_divider_range_construct(
		&divider_ranges[DIVIDER_RANGE_02],
		DIVIDER_RANGE_02_START,
		DIVIDER_RANGE_02_STEP_SIZE,
		DIVIDER_RANGE_02_BASE_DIVIDER_ID,
		DIVIDER_RANGE_03_BASE_DIVIDER_ID);
	dal_divider_range_construct(
		&divider_ranges[DIVIDER_RANGE_03],
		DIVIDER_RANGE_03_START,
		DIVIDER_RANGE_03_STEP_SIZE,
		DIVIDER_RANGE_03_BASE_DIVIDER_ID,
		DIVIDER_RANGE_MAX_DIVIDER_ID);

	{
		uint32_t ss_info_num =
				bp->funcs->get_ss_entry_number(bp,
				AS_SIGNAL_TYPE_GPU_PLL);

		if (ss_info_num) {
			struct spread_spectrum_info info;
			enum bp_result result;

			memset(&info, 0, sizeof(info));

			result = bp->funcs->get_spread_spectrum_info(bp,
					AS_SIGNAL_TYPE_GPU_PLL,
					0,
					&info);

			/* Based on VBIOS, VBIOS will keep entry for GPU PLL SS
			 * even if SS not enabled and in that case
			 * SSInfo.spreadSpectrumPercentage !=0 would be sign
			 * that SS is enabled
			 */
			if (result == BP_RESULT_OK &&
					info.spread_spectrum_percentage != 0) {
				dc110->ss_on_gpu_pll = true;
				dc110->gpu_pll_ss_divider =
					info.spread_percentage_divider;

				if (info.type.CENTER_MODE == 0) {
					/* Currently for DP Reference clock we
					 * need only SS percentage for
					 * downspread */
					dc110->gpu_pll_ss_percentage =
						info.spread_spectrum_percentage;
				}
			}

		}
	}

	return true;
}

/*****************************************************************************
 * public functions
 *****************************************************************************/

struct display_clock *dal_display_clock_dce110_create(
	struct dc_context *ctx)
{
	struct display_clock_dce110 *dc110;

	dc110 = dm_alloc(sizeof(struct display_clock_dce110));

	if (dc110 == NULL)
		return NULL;

	if (dal_display_clock_dce110_construct(dc110, ctx))
		return &dc110->disp_clk_base;

	dm_free(dc110);

	return NULL;
}
