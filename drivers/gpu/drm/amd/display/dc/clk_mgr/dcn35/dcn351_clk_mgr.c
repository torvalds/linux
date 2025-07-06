/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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

#include "core_types.h"
#include "dcn35_clk_mgr.h"

#define DCN_BASE__INST0_SEG1 0x000000C0
#define mmCLK1_CLK_PLL_REQ 0x16E37

#define mmCLK1_CLK0_DFS_CNTL 0x16E69
#define mmCLK1_CLK1_DFS_CNTL 0x16E6C
#define mmCLK1_CLK2_DFS_CNTL 0x16E6F
#define mmCLK1_CLK3_DFS_CNTL 0x16E72
#define mmCLK1_CLK4_DFS_CNTL 0x16E75
#define mmCLK1_CLK5_DFS_CNTL 0x16E78

#define mmCLK1_CLK0_CURRENT_CNT 0x16EFC
#define mmCLK1_CLK1_CURRENT_CNT 0x16EFD
#define mmCLK1_CLK2_CURRENT_CNT 0x16EFE
#define mmCLK1_CLK3_CURRENT_CNT 0x16EFF
#define mmCLK1_CLK4_CURRENT_CNT 0x16F00
#define mmCLK1_CLK5_CURRENT_CNT 0x16F01

#define mmCLK1_CLK0_BYPASS_CNTL 0x16E8A
#define mmCLK1_CLK1_BYPASS_CNTL 0x16E93
#define mmCLK1_CLK2_BYPASS_CNTL 0x16E9C
#define mmCLK1_CLK3_BYPASS_CNTL 0x16EA5
#define mmCLK1_CLK4_BYPASS_CNTL 0x16EAE
#define mmCLK1_CLK5_BYPASS_CNTL 0x16EB7

#define mmCLK1_CLK0_DS_CNTL 0x16E83
#define mmCLK1_CLK1_DS_CNTL 0x16E8C
#define mmCLK1_CLK2_DS_CNTL 0x16E95
#define mmCLK1_CLK3_DS_CNTL 0x16E9E
#define mmCLK1_CLK4_DS_CNTL 0x16EA7
#define mmCLK1_CLK5_DS_CNTL 0x16EB0

#define mmCLK1_CLK0_ALLOW_DS 0x16E84
#define mmCLK1_CLK1_ALLOW_DS 0x16E8D
#define mmCLK1_CLK2_ALLOW_DS 0x16E96
#define mmCLK1_CLK3_ALLOW_DS 0x16E9F
#define mmCLK1_CLK4_ALLOW_DS 0x16EA8
#define mmCLK1_CLK5_ALLOW_DS 0x16EB1

#define mmCLK5_spll_field_8 0x1B04B
#define mmCLK6_spll_field_8 0x1B24B
#define mmDENTIST_DISPCLK_CNTL 0x0124
#define regDENTIST_DISPCLK_CNTL 0x0064
#define regDENTIST_DISPCLK_CNTL_BASE_IDX 1

#define CLK1_CLK_PLL_REQ__FbMult_int__SHIFT 0x0
#define CLK1_CLK_PLL_REQ__PllSpineDiv__SHIFT 0xc
#define CLK1_CLK_PLL_REQ__FbMult_frac__SHIFT 0x10
#define CLK1_CLK_PLL_REQ__FbMult_int_MASK 0x000001FFL
#define CLK1_CLK_PLL_REQ__PllSpineDiv_MASK 0x0000F000L
#define CLK1_CLK_PLL_REQ__FbMult_frac_MASK 0xFFFF0000L

#define CLK1_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL_MASK 0x00000007L

// DENTIST_DISPCLK_CNTL
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_WDIVIDER__SHIFT 0x0
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_RDIVIDER__SHIFT 0x8
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_CHG_DONE__SHIFT 0x13
#define DENTIST_DISPCLK_CNTL__DENTIST_DPPCLK_CHG_DONE__SHIFT 0x14
#define DENTIST_DISPCLK_CNTL__DENTIST_DPPCLK_WDIVIDER__SHIFT 0x18
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_WDIVIDER_MASK 0x0000007FL
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_RDIVIDER_MASK 0x00007F00L
#define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_CHG_DONE_MASK 0x00080000L
#define DENTIST_DISPCLK_CNTL__DENTIST_DPPCLK_CHG_DONE_MASK 0x00100000L
#define DENTIST_DISPCLK_CNTL__DENTIST_DPPCLK_WDIVIDER_MASK 0x7F000000L

#define CLK5_spll_field_8__spll_ssc_en_MASK 0x00002000L

#define REG(reg) \
	(clk_mgr->regs->reg)

#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
					reg ## reg_name

#define CLK_SR_DCN35(reg_name)\
	.reg_name = mm ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn351 = {
	CLK_REG_LIST_DCN35()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn351 = {
	CLK_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn351 = {
	CLK_COMMON_MASK_SH_LIST_DCN32(_MASK)
};

#define TO_CLK_MGR_DCN35(clk_mgr)\
	container_of(clk_mgr, struct clk_mgr_dcn35, base)


void dcn351_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_dcn35 *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	/*register offset changed*/
	clk_mgr->base.regs = &clk_mgr_regs_dcn351;
	clk_mgr->base.clk_mgr_shift = &clk_mgr_shift_dcn351;
	clk_mgr->base.clk_mgr_mask = &clk_mgr_mask_dcn351;

	dcn35_clk_mgr_construct(ctx,  clk_mgr, pp_smu, dccg);

}


