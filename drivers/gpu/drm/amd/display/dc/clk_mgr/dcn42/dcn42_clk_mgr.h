/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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
 */

#ifndef __DCN42_CLK_MGR_H__
#define __DCN42_CLK_MGR_H__
#include "clk_mgr_internal.h"

#define NUM_CLOCK_SOURCES 5

struct dcn42_watermarks;

struct dcn42_smu_watermark_set {
	struct dcn42_watermarks *wm_set;
	union large_integer mc_address;
};

struct dcn42_ss_info_table {
	uint32_t ss_divider;
	uint32_t ss_percentage[NUM_CLOCK_SOURCES];
};

struct clk_mgr_dcn42 {
	struct clk_mgr_internal base;
	struct dcn42_smu_watermark_set smu_wm_set;
};

bool dcn42_are_clock_states_equal(struct dc_clocks *a,
								  struct dc_clocks *b);
void dcn42_init_clocks(struct clk_mgr *clk_mgr);
void dcn42_update_clocks(struct clk_mgr *clk_mgr_base,
						 struct dc_state *context,
						 bool safe_to_lower);

void dcn42_clk_mgr_construct(struct dc_context *ctx,
							 struct clk_mgr_dcn42 *clk_mgr,
							 struct pp_smu_funcs *pp_smu,
							 struct dccg *dccg);

void dcn42_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr_int);

/* Exposed for dcn42b reuse */
void dcn42_init_single_clock(unsigned int *entry_0,
			      uint32_t *smu_entry_0,
			      uint8_t num_levels);
unsigned int dcn42_convert_wck_ratio(uint8_t wck_ratio);
extern struct dcn42_ss_info_table dcn42_ss_info_table;
void dcn42_build_watermark_ranges(struct clk_bw_params *bw_params, struct dcn42_watermarks *table);
void dcn42_enable_pme_wa(struct clk_mgr *clk_mgr_base);
void dcn42_notify_wm_ranges(struct clk_mgr *clk_mgr_base);
void dcn42_set_low_power_state(struct clk_mgr *clk_mgr_base);
void dcn42_exit_low_power_state(struct clk_mgr *clk_mgr_base);
unsigned int dcn42_get_max_clock_khz(struct clk_mgr *clk_mgr_base, enum clk_type clk_type);
bool dcn42_is_smu_present(struct clk_mgr *clk_mgr_base);
int dcn42_get_active_display_cnt_wa(struct dc *dc, struct dc_state *context, int *all_active_disps);
void dcn42_update_clocks_update_dpp_dto(struct clk_mgr_internal *clk_mgr, struct dc_state *context, bool safe_to_lower);
void dcn42_update_clocks_update_dtb_dto(struct clk_mgr_internal *clk_mgr, struct dc_state *context, int ref_dtbclk_khz);
bool dcn42_is_spll_ssc_enabled(struct clk_mgr *clk_mgr_base);
#endif //__DCN42_CLK_MGR_H__
