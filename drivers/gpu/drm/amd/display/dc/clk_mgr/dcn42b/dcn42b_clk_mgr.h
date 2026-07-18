// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#ifndef __DCN42B_CLK_MGR_H__
#define __DCN42B_CLK_MGR_H__
#include "clk_mgr_internal.h"
#include "dcn42/dcn42_clk_mgr.h"

/* DCN42B reuses the following from DCN42:
 * - dcn42_update_clocks (dtbclk_en=false so all dtbclk branches are skipped)
 * - dcn42_get_dispclk_from_dentist (DENTIST_DISPCLK_CNTL has same DCN offset)
 * - dcn42_get_dpm_table_from_smu (identical, only SMU calls)
 * - dcn42_are_clock_states_equal
 * - dcn42_enable_pme_wa
 * - dcn42_update_clocks_update_dpp_dto
 * - dcn42_update_clocks_update_dtb_dto
 * - dcn42_build_watermark_ranges
 * - dcn42_is_spll_ssc_enabled
 * - dcn42_has_active_display
 * - dcn42_notify_wm_ranges
 * - dcn42_set_low_power_state
 * - dcn42_exit_low_power_state
 * - dcn42_get_max_clock_khz
 * - dcn42_is_smu_present
 *
 * CANNOT reuse from DCN42 (hardware register differences):
 * - dcn42_read_ss_info_from_lut (CLK8 vs CLK5 registers)
 * - dcn42_dump_clk_registers* (CLK8 vs CLK5 registers)
 * - dcn42_get_clock_freq_from_clkip (CLK8 vs CLK5 registers)
 * - dcn42_init_clocks (calls CLK8-specific functions, dtbclk logic)
 * - init_clk_states (dtbclk_en difference: true for dcn42, false for dcn42b)
 *
 * See dcn42_clk_mgr.h for declarations
 */

#define NUM_CLOCK_SOURCES 5

void dcn42b_init_clocks(struct clk_mgr *clk_mgr);

void dcn42b_clk_mgr_construct(struct dc_context *ctx,
			      struct clk_mgr_dcn42 *clk_mgr,
			      struct pp_smu_funcs *pp_smu,
			      struct dccg *dccg);
uint32_t dcn42b_get_clock_freq_from_clkip(struct clk_mgr *clk_mgr_base,
				  enum clock_type clock);

#endif //__DCN42B_CLK_MGR_H__
