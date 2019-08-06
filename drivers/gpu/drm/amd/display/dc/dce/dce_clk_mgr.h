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

#include "clk_mgr.h"
#include "dccg.h"

#define MEMORY_TYPE_MULTIPLIER_CZ 4

#define CLK_COMMON_REG_LIST_DCE_BASE() \
	.DPREFCLK_CNTL = mmDPREFCLK_CNTL, \
	.DENTIST_DISPCLK_CNTL = mmDENTIST_DISPCLK_CNTL

#define CLK_COMMON_REG_LIST_DCN_BASE() \
	SR(DENTIST_DISPCLK_CNTL)

#define CLK_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define CLK_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh) \
	CLK_SF(DPREFCLK_CNTL, DPREFCLK_SRC_SEL, mask_sh), \
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DPREFCLK_WDIVIDER, mask_sh)

#define CLK_COMMON_MASK_SH_LIST_DCN_COMMON_BASE(mask_sh) \
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, mask_sh),\
	CLK_SF(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_DONE, mask_sh)

#define CLK_REG_FIELD_LIST(type) \
	type DPREFCLK_SRC_SEL; \
	type DENTIST_DPREFCLK_WDIVIDER; \
	type DENTIST_DISPCLK_WDIVIDER; \
	type DENTIST_DISPCLK_CHG_DONE;

struct clk_mgr_shift {
	CLK_REG_FIELD_LIST(uint8_t)
};

struct clk_mgr_mask {
	CLK_REG_FIELD_LIST(uint32_t)
};

struct clk_mgr_registers {
	uint32_t DPREFCLK_CNTL;
	uint32_t DENTIST_DISPCLK_CNTL;
};

struct state_dependent_clocks {
	int display_clk_khz;
	int pixel_clk_khz;
};

struct dce_clk_mgr {
	struct clk_mgr base;
	const struct clk_mgr_registers *regs;
	const struct clk_mgr_shift *clk_mgr_shift;
	const struct clk_mgr_mask *clk_mgr_mask;

	struct dccg *dccg;

	struct state_dependent_clocks max_clks_by_state[DM_PP_CLOCKS_MAX_STATES];

	int dentist_vco_freq_khz;

	/* Cache the status of DFS-bypass feature*/
	bool dfs_bypass_enabled;
	/* True if the DFS-bypass feature is enabled and active. */
	bool dfs_bypass_active;
	/* Cache the display clock returned by VBIOS if DFS-bypass is enabled.
	 * This is basically "Crystal Frequency In KHz" (XTALIN) frequency */
	int dfs_bypass_disp_clk;

	/* Flag for Enabled SS on DPREFCLK */
	bool ss_on_dprefclk;
	/* DPREFCLK SS percentage (if down-spread enabled) */
	int dprefclk_ss_percentage;
	/* DPREFCLK SS percentage Divider (100 or 1000) */
	int dprefclk_ss_divider;
	int dprefclk_khz;

	enum dm_pp_clocks_state max_clks_state;
	enum dm_pp_clocks_state cur_min_clks_state;
};

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

static inline bool should_set_clock(bool safe_to_lower, int calc_clk, int cur_clk)
{
	return ((safe_to_lower && calc_clk < cur_clk) || calc_clk > cur_clk);
}

void dce_clock_read_ss_info(struct dce_clk_mgr *dccg_dce);

int dce12_get_dp_ref_freq_khz(struct clk_mgr *dccg);

void dce110_fill_display_configs(
	const struct dc_state *context,
	struct dm_pp_display_configuration *pp_display_cfg);

int dce112_set_clock(struct clk_mgr *dccg, int requested_clk_khz);

struct clk_mgr *dce_clk_mgr_create(
	struct dc_context *ctx,
	const struct clk_mgr_registers *regs,
	const struct clk_mgr_shift *clk_shift,
	const struct clk_mgr_mask *clk_mask);

struct clk_mgr *dce110_clk_mgr_create(
	struct dc_context *ctx,
	const struct clk_mgr_registers *regs,
	const struct clk_mgr_shift *clk_shift,
	const struct clk_mgr_mask *clk_mask);

struct clk_mgr *dce112_clk_mgr_create(
	struct dc_context *ctx,
	const struct clk_mgr_registers *regs,
	const struct clk_mgr_shift *clk_shift,
	const struct clk_mgr_mask *clk_mask);

struct clk_mgr *dce120_clk_mgr_create(struct dc_context *ctx);

void dce_clk_mgr_destroy(struct clk_mgr **clk_mgr);

int dentist_get_divider_from_did(int did);

#endif /* _DCE_CLK_MGR_H_ */
