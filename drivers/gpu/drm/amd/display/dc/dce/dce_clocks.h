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

#include "display_clock_interface.h"
#include "../gpu/divider_range.h"


#define TO_DCE_CLOCKS(clocks)\
	container_of(clocks, struct dce_disp_clk, base)

#define CLK_COMMON_REG_LIST_DCE_BASE() \
	.DPREFCLK_CNTL = mmDPREFCLK_CNTL, \
	.DENTIST_DISPCLK_CNTL = mmDENTIST_DISPCLK_CNTL, \
	.MASTER_COMM_DATA_REG1 = mmMASTER_COMM_DATA_REG1, \
	.MASTER_COMM_CMD_REG = mmMASTER_COMM_CMD_REG, \
	.MASTER_COMM_CNTL_REG = mmMASTER_COMM_CNTL_REG

#define CLK_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh) \
	CLK_SF(DPREFCLK_CNTL, DPREFCLK_SRC_SEL, mask_sh), \
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DPREFCLK_WDIVIDER, mask_sh), \
	CLK_SF(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, mask_sh), \
	CLK_SF(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, mask_sh)


#define CLK_REG_FIELD_LIST(type) \
	type DPREFCLK_SRC_SEL; \
	type DENTIST_DPREFCLK_WDIVIDER; \
	type MASTER_COMM_CMD_REG_BYTE0; \
	type MASTER_COMM_INTERRUPT

struct dce_disp_clk_shift {
	CLK_REG_FIELD_LIST(uint8_t);
};

struct dce_disp_clk_mask {
	CLK_REG_FIELD_LIST(uint32_t);
};

struct dce_disp_clk_registers {
	uint32_t DPREFCLK_CNTL;
	uint32_t DENTIST_DISPCLK_CNTL;
	uint32_t MASTER_COMM_DATA_REG1;
	uint32_t MASTER_COMM_CMD_REG;
	uint32_t MASTER_COMM_CNTL_REG;
};

/* Array identifiers and count for the divider ranges.*/
enum divider_range_count {
	DIVIDER_RANGE_01 = 0,
	DIVIDER_RANGE_02,
	DIVIDER_RANGE_03,
	DIVIDER_RANGE_MAX /* == 3*/
};

struct dce_disp_clk {
	struct display_clock base;
	const struct dce_disp_clk_registers *regs;
	const struct dce_disp_clk_shift *clk_shift;
	const struct dce_disp_clk_mask *clk_mask;

	struct state_dependent_clocks max_clks_by_state[DM_PP_CLOCKS_MAX_STATES];
	struct divider_range divider_ranges[DIVIDER_RANGE_MAX];

	bool use_max_disp_clk;
	uint32_t dentist_vco_freq_khz;

	/* Cache the status of DFS-bypass feature*/
	bool dfs_bypass_enabled;
	/* Cache the display clock returned by VBIOS if DFS-bypass is enabled.
	 * This is basically "Crystal Frequency In KHz" (XTALIN) frequency */
	uint32_t dfs_bypass_disp_clk;

	/* Flag for Enabled SS on GPU PLL */
	bool ss_on_gpu_pll;
	/* GPU PLL SS percentage (if down-spread enabled) */
	uint32_t gpu_pll_ss_percentage;
	/* GPU PLL SS percentage Divider (100 or 1000) */
	uint32_t gpu_pll_ss_divider;


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

void dce_disp_clk_destroy(struct display_clock **disp_clk);

#endif /* _DCE_CLOCKS_H_ */
