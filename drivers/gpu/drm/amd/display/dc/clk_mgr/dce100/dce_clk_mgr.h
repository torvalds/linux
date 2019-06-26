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


#ifndef _DCE_CLK_MGR_H_
#define _DCE_CLK_MGR_H_

#include "dc.h"

/* Starting DID for each range */
enum dentist_base_divider_id {
	DENTIST_BASE_DID_1 = 0x08,
	DENTIST_BASE_DID_2 = 0x40,
	DENTIST_BASE_DID_3 = 0x60,
	DENTIST_BASE_DID_4 = 0x7e,
	DENTIST_MAX_DID = 0x7f
};

/* Starting point and step size for each divider range.*/
enum dentist_divider_range {
	DENTIST_DIVIDER_RANGE_1_START = 8,   /* 2.00  */
	DENTIST_DIVIDER_RANGE_1_STEP  = 1,   /* 0.25  */
	DENTIST_DIVIDER_RANGE_2_START = 64,  /* 16.00 */
	DENTIST_DIVIDER_RANGE_2_STEP  = 2,   /* 0.50  */
	DENTIST_DIVIDER_RANGE_3_START = 128, /* 32.00 */
	DENTIST_DIVIDER_RANGE_3_STEP  = 4,   /* 1.00  */
	DENTIST_DIVIDER_RANGE_4_START = 248, /* 62.00 */
	DENTIST_DIVIDER_RANGE_4_STEP  = 264, /* 66.00 */
	DENTIST_DIVIDER_RANGE_SCALE_FACTOR = 4
};

/* functions shared by other dce clk mgrs */
int dce_adjust_dp_ref_freq_for_ss(struct clk_mgr_internal *clk_mgr_dce, int dp_ref_clk_khz);
int dce_get_dp_ref_freq_khz(struct clk_mgr *clk_mgr_base);
enum dm_pp_clocks_state dce_get_required_clocks_state(
	struct clk_mgr *clk_mgr_base,
	struct dc_state *context);

uint32_t dce_get_max_pixel_clock_for_all_paths(struct dc_state *context);


void dce_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr_dce);

void dce_clock_read_ss_info(struct clk_mgr_internal *dccg_dce);

int dce12_get_dp_ref_freq_khz(struct clk_mgr *dccg);

int dce_set_clock(
	struct clk_mgr *clk_mgr_base,
	int requested_clk_khz);


void dce_clk_mgr_destroy(struct clk_mgr **clk_mgr);

int dentist_get_divider_from_did(int did);

#endif /* _DCE_CLK_MGR_H_ */
