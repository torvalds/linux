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
#ifndef __DAL_DISPLAY_CLOCK_DCE110_H__
#define __DAL_DISPLAY_CLOCK_DCE110_H__

#include "gpu/display_clock.h"

struct display_clock_dce110 {
	struct display_clock disp_clk_base;
	/* Max display block clocks state*/
	enum clocks_state max_clks_state;
	bool use_max_disp_clk;
	uint32_t dentist_vco_freq_khz;
	/* Cache the status of DFS-bypass feature*/
	bool dfs_bypass_enabled;
	/* GPU PLL SS percentage (if down-spread enabled) */
	uint32_t gpu_pll_ss_percentage;
	/* GPU PLL SS percentage Divider (100 or 1000) */
	uint32_t gpu_pll_ss_divider;
	/* Flag for Enabled SS on GPU PLL */
	bool ss_on_gpu_pll;
	/* Cache the display clock returned by VBIOS if DFS-bypass is enabled.
	 * This is basically "Crystal Frequency In KHz" (XTALIN) frequency */
	uint32_t dfs_bypass_disp_clk;
	struct display_clock_state clock_state;
};

#define DCLCK110_FROM_BASE(dc_base) \
	container_of(dc_base, struct display_clock_dce110, disp_clk_base)

#endif /* __DAL_DISPLAY_CLOCK_DCE110_H__ */
