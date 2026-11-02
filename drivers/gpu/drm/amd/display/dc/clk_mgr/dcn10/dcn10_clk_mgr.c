// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "clk_mgr_internal.h"
#include "core_types.h"
#include "dal_asic_id.h"
#include "dcn10_clk_mgr.h"

unsigned int dcn10_dentist_get_divider_from_did(unsigned int did)
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
int dcn10_adjust_dp_ref_freq_for_ss(struct clk_mgr_internal *clk_mgr_dce, int dp_ref_clk_khz)
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

int dcn10_get_dp_ref_freq_khz(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr_dce = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	return dcn10_adjust_dp_ref_freq_for_ss(clk_mgr_dce, clk_mgr_base->dprefclk_khz);
}

void dcn10_clock_read_ss_info(struct clk_mgr_internal *clk_mgr_dce)
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
			if (clk_mgr_dce->base.ctx->dc->config.ignore_dpref_ss)
				clk_mgr_dce->dprefclk_ss_percentage = 0;
		}
	}
}

int dcn10_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_clk_khz)
{
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = clk_mgr->base.ctx->dc_bios;
	struct dc *dc = clk_mgr->base.ctx->dc;
	struct dmcu *dmcu = dc->res_pool->dmcu;
	int actual_clock;
	/* Prepare to program display clock*/
	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/* Make sure requested clock isn't lower than minimum threshold*/
	if (requested_clk_khz > 0)
		requested_clk_khz = max(requested_clk_khz,
				clk_mgr->base.dentist_vco_freq_khz / 62);

	dce_clk_params.target_clock_frequency = requested_clk_khz;
	dce_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DISPLAY_CLOCK;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);
	actual_clock = dce_clk_params.target_clock_frequency;

		if (dmcu && dmcu->funcs->is_dmcu_initialized(dmcu)) {
			if (clk_mgr->dfs_bypass_disp_clk != actual_clock)
				dmcu->funcs->set_psr_wait_loop(dmcu,
						actual_clock / 1000 / 7);
		}

	clk_mgr->dfs_bypass_disp_clk = actual_clock;
	return actual_clock;

}

int dcn10_set_dprefclk(struct clk_mgr_internal *clk_mgr)
{
	struct bp_set_dce_clock_parameters dce_clk_params;
	struct dc_bios *bp = clk_mgr->base.ctx->dc_bios;

	memset(&dce_clk_params, 0, sizeof(dce_clk_params));

	/*Program DP ref Clock*/
	/*VBIOS will determine DPREFCLK frequency, so we don't set it*/
	dce_clk_params.target_clock_frequency = 0;
	dce_clk_params.pll_id = CLOCK_SOURCE_ID_DFS;
	dce_clk_params.clock_type = DCECLOCK_TYPE_DPREFCLK;
	if (!((clk_mgr->base.ctx->asic_id.chip_family == FAMILY_AI) &&
	       ASICREV_IS_VEGA20_P(clk_mgr->base.ctx->asic_id.hw_internal_rev)))
		dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK =
			(dce_clk_params.pll_id ==
					CLOCK_SOURCE_COMBO_DISPLAY_PLL0);
	else
		dce_clk_params.flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK = false;

	bp->funcs->set_dce_clock(bp, &dce_clk_params);

	/* Returns the dp_refclk that was set */
	return dce_clk_params.target_clock_frequency;
}
