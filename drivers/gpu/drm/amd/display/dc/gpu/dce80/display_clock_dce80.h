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
#ifndef __DAL_DISPLAY_CLOCK_DCE80_H__
#define __DAL_DISPLAY_CLOCK_DCE80_H__

#include "display_clock_interface.h"

struct display_clock_dce80 {
	struct display_clock disp_clk;
	/* DFS input - GPUPLL VCO frequency - from VBIOS Firmware info. */
	uint32_t dentist_vco_freq_khz;
	/* GPU PLL SS percentage (if down-spread enabled)*/
	uint32_t gpu_pll_ss_percentage;
	/* GPU PLL SS percentage Divider (100 or 1000)*/
	uint32_t gpu_pll_ss_divider;
	/* Flag for Enabled SS on GPU PLL*/
	bool ss_on_gpu_pll;
	/* Current minimum display block clocks state*/
	enum clocks_state cur_min_clks_state;
	/* DFS-bypass feature variable
	 Cache the status of DFS-bypass feature*/
	bool dfs_bypass_enabled;
	/* Cache the display clock returned by VBIOS if DFS-bypass is enabled.
	 * This is basically "Crystal Frequency In KHz" (XTALIN) frequency */
	uint32_t dfs_bypass_disp_clk;
	bool use_max_disp_clk;
};

struct display_clock *dal_display_clock_dce80_create(
	struct dc_context *ctx);

#endif /* __DAL_DISPLAY_CLOCK_DCE80_H__ */
