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

#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"

#include "include/bios_parser_interface.h"
#include "include/fixed32_32.h"
#include "include/logger_interface.h"

#include "../divider_range.h"

#include "display_clock_dce112.h"

#define FROM_DISPLAY_CLOCK(base) \
	container_of(base, struct display_clock_dce112, disp_clk_base)

static struct state_dependent_clocks max_clks_by_state[] = {
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

static struct divider_range divider_ranges[DIVIDER_RANGE_MAX];


void dispclk_dce112_destroy(struct display_clock **base)
{
	struct display_clock_dce112 *dc112;

	dc112 = DCLCK112_FROM_BASE(*base);

	dm_free(dc112);

	*base = NULL;
}

static bool display_clock_integrated_info_construct(
	struct display_clock_dce112 *disp_clk)
{
	struct integrated_info info;
	uint32_t i;
	struct display_clock *base = &disp_clk->disp_clk_base;

	memset(&info, 0, sizeof(struct integrated_info));

	disp_clk->dentist_vco_freq_khz = info.dentist_vco_freq;
	if (disp_clk->dentist_vco_freq_khz == 0)
		disp_clk->dentist_vco_freq_khz = 3600000;

	base->min_display_clk_threshold_khz =
		disp_clk->dentist_vco_freq_khz / 64;

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
		if (info.disp_clk_voltage[i].max_supported_clk >= 100000) {
			(disp_clk->max_clks_by_state + clk_state)->
					display_clk_khz =
				info.disp_clk_voltage[i].max_supported_clk;
		}
	}

	return true;
}

void dce112_set_clock(
	struct display_clock *base,
	uint32_t requested_clk_khz)
{
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = base->ctx->dc_bios;

	/* Prepare to program display clock*/
	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = dm_max(requested_clk_khz,
				base->min_display_clk_threshold_khz);

	dce_clk_params.target_clock_frequency = requested_clk_khz;
	dce_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DISPLAY_CLOCK;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);

	/* from power down, we need mark the clock state as ClocksStateNominal
	 * from HWReset, so when resume we will call pplib voltage regulator.*/
	if (requested_clk_khz == 0)
		base->cur_min_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;

	/*Program DP ref Clock*/
	/*VBIOS will determine DPREFCLK frequency, so we don't set it*/
	dce_clk_params.target_clock_frequency = 0;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DPREFCLK;
	dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK =
			(dce_clk_params.pll_id ==
					CLOCK_SOURCE_COMBO_DISPLAY_PLL0);

	bp->funcs->set_dce_clock(bp, &dce_clk_params);
}

static const struct display_clock_funcs funcs = {
	.destroy = dispclk_dce112_destroy,
	.set_clock = dce112_set_clock,
};

bool dal_display_clock_dce112_construct(
	struct display_clock_dce112 *dc112,
	struct dc_context *ctx)
{
	struct display_clock *dc_base = &dc112->disp_clk_base;

	dc_base->ctx = ctx;
	dc_base->min_display_clk_threshold_khz = 0;

	dc_base->cur_min_clks_state = DM_PP_CLOCKS_STATE_INVALID;

	dc_base->funcs = &funcs;

	dc112->dfs_bypass_disp_clk = 0;

	if (!display_clock_integrated_info_construct(dc112))
		dm_logger_write(dc_base->ctx->logger, LOG_WARNING,
			"Cannot obtain VBIOS integrated info\n");

	dc112->gpu_pll_ss_percentage = 0;
	dc112->gpu_pll_ss_divider = 1000;
	dc112->ss_on_gpu_pll = false;

/* Initially set max clocks state to nominal.  This should be updated by
 * via a pplib call to DAL IRI eventually calling a
 * DisplayEngineClock_dce112::StoreMaxClocksState().  This call will come in
 * on PPLIB init. This is from DCE5x. in case HW wants to use mixed method.*/
	dc_base->max_clks_state = DM_PP_CLOCKS_STATE_NOMINAL;

	dc112->disp_clk_base.min_display_clk_threshold_khz =
			(dc112->dentist_vco_freq_khz / 62);

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
			ctx->dc_bios->funcs->
			get_ss_entry_number(ctx->dc_bios, AS_SIGNAL_TYPE_GPU_PLL);

		if (ss_info_num) {
			struct spread_spectrum_info info;
			bool result;

			memset(&info, 0, sizeof(info));

			result =
					(BP_RESULT_OK == ctx->dc_bios->funcs->
					get_spread_spectrum_info(ctx->dc_bios,
					AS_SIGNAL_TYPE_GPU_PLL, 0, &info)) ? true : false;


			/* Based on VBIOS, VBIOS will keep entry for GPU PLL SS
			 * even if SS not enabled and in that case
			 * SSInfo.spreadSpectrumPercentage !=0 would be sign
			 * that SS is enabled
			 */
			if (result && info.spread_spectrum_percentage != 0) {
				dc112->ss_on_gpu_pll = true;
				dc112->gpu_pll_ss_divider =
					info.spread_percentage_divider;

				if (info.type.CENTER_MODE == 0) {
					/* Currently for DP Reference clock we
					 * need only SS percentage for
					 * downspread */
					dc112->gpu_pll_ss_percentage =
						info.spread_spectrum_percentage;
				}
			}

		}
	}

	dc112->use_max_disp_clk = true;
	dc112->max_clks_by_state = max_clks_by_state;

	return true;
}

