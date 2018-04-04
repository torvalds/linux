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


#ifndef _DCE_CLOCKS_H_
#define _DCE_CLOCKS_H_

#include "display_clock.h"

#define CLK_COMMON_REG_LIST_DCE_BASE() \
	.DPREFCLK_CNTL = mmDPREFCLK_CNTL, \
	.DENTIST_DISPCLK_CNTL = mmDENTIST_DISPCLK_CNTL

#define CLK_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh) \
	CLK_SF(DPREFCLK_CNTL, DPREFCLK_SRC_SEL, mask_sh), \
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DPREFCLK_WDIVIDER, mask_sh)

#define CLK_REG_FIELD_LIST(type) \
	type DPREFCLK_SRC_SEL; \
	type DENTIST_DPREFCLK_WDIVIDER;

struct dce_disp_clk_shift {
	CLK_REG_FIELD_LIST(uint8_t)
};

struct dce_disp_clk_mask {
	CLK_REG_FIELD_LIST(uint32_t)
};

struct dce_disp_clk_registers {
	uint32_t DPREFCLK_CNTL;
	uint32_t DENTIST_DISPCLK_CNTL;
};

/* Array identifiers and count for the divider ranges.*/
enum dce_divider_range_count {
	DIVIDER_RANGE_01 = 0,
	DIVIDER_RANGE_02,
	DIVIDER_RANGE_03,
	DIVIDER_RANGE_MAX /* == 3*/
};

enum dce_divider_error_types {
	INVALID_DID = 0,
	INVALID_DIVIDER = 1
};

struct dce_divider_range {
	int div_range_start;
	/* The end of this range of dividers.*/
	int div_range_end;
	/* The distance between each divider in this range.*/
	int div_range_step;
	/* The divider id for the lowest divider.*/
	int did_min;
	/* The divider id for the highest divider.*/
	int did_max;
};

struct dce_disp_clk {
	struct display_clock base;
	const struct dce_disp_clk_registers *regs;
	const struct dce_disp_clk_shift *clk_shift;
	const struct dce_disp_clk_mask *clk_mask;

	struct state_dependent_clocks max_clks_by_state[DM_PP_CLOCKS_MAX_STATES];
	struct dce_divider_range divider_ranges[DIVIDER_RANGE_MAX];

	bool use_max_disp_clk;
	int dentist_vco_freq_khz;

	/* Cache the status of DFS-bypass feature*/
	bool dfs_bypass_enabled;
	/* Cache the display clock returned by VBIOS if DFS-bypass is enabled.
	 * This is basically "Crystal Frequency In KHz" (XTALIN) frequency */
	int dfs_bypass_disp_clk;

	/* Flag for Enabled SS on DPREFCLK */
	bool ss_on_dprefclk;
	/* DPREFCLK SS percentage (if down-spread enabled) */
	int dprefclk_ss_percentage;
	/* DPREFCLK SS percentage Divider (100 or 1000) */
	int dprefclk_ss_divider;

	/* max disp_clk from PPLIB for max validation display clock*/
	int max_displ_clk_in_khz;
};


struct display_clock *dce_disp_clk_create(
	struct dc_context *ctx,
	const struct dce_disp_clk_registers *regs,
	const struct dce_disp_clk_shift *clk_shift,
	const struct dce_disp_clk_mask *clk_mask);

struct display_clock *dce110_disp_clk_create(
	struct dc_context *ctx,
	const struct dce_disp_clk_registers *regs,
	const struct dce_disp_clk_shift *clk_shift,
	const struct dce_disp_clk_mask *clk_mask);

struct display_clock *dce112_disp_clk_create(
	struct dc_context *ctx,
	const struct dce_disp_clk_registers *regs,
	const struct dce_disp_clk_shift *clk_shift,
	const struct dce_disp_clk_mask *clk_mask);

struct display_clock *dce120_disp_clk_create(struct dc_context *ctx);

void dce_disp_clk_destroy(struct display_clock **disp_clk);

#endif /* _DCE_CLOCKS_H_ */
