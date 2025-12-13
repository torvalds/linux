/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __DCN314_CLK_MGR_H__
#define __DCN314_CLK_MGR_H__
#include "clk_mgr_internal.h"

#define DCN314_NUM_CLOCK_SOURCES   5

struct dcn314_watermarks;

struct dcn314_smu_watermark_set {
	struct dcn314_watermarks *wm_set;
	union large_integer mc_address;
};

struct clk_mgr_dcn314 {
	struct clk_mgr_internal base;
	struct dcn314_smu_watermark_set smu_wm_set;
};

struct dcn314_ss_info_table {
	uint32_t ss_divider;
	uint32_t ss_percentage[DCN314_NUM_CLOCK_SOURCES];
};

bool dcn314_are_clock_states_equal(struct dc_clocks *a,
		struct dc_clocks *b);

bool dcn314_is_spll_ssc_enabled(struct clk_mgr *clk_mgr_base);

void dcn314_init_clocks(struct clk_mgr *clk_mgr);

void dcn314_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower);

void dcn314_clk_mgr_construct(struct dc_context *ctx,
		struct clk_mgr_dcn314 *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg);

void dcn314_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr_int);


void dcn314_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info);


#endif //__DCN314_CLK_MGR_H__
