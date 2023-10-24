/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DCN35_MMHUBBUB_H
#define __DCN35_MMHUBBUB_H

#include "mcif_wb.h"
#include "dcn32/dcn32_mmhubbub.h"

#define MCIF_WB_REG_VARIABLE_LIST_DCN3_5  \
	MCIF_WB_REG_VARIABLE_LIST_DCN3_0; \
	uint32_t MMHUBBUB_CLOCK_CNTL

#define MCIF_WB_COMMON_MASK_SH_LIST_DCN3_5(mask_sh)                            \
	MCIF_WB_COMMON_MASK_SH_LIST_DCN32(mask_sh),                            \
		SF(MMHUBBUB_CLOCK_CNTL, MMHUBBUB_TEST_CLK_SEL, mask_sh),       \
		SF(MMHUBBUB_CLOCK_CNTL, DISPCLK_R_MMHUBBUB_GATE_DIS, mask_sh), \
		SF(MMHUBBUB_CLOCK_CNTL, DISPCLK_G_WBIF0_GATE_DIS, mask_sh),    \
		SF(MMHUBBUB_CLOCK_CNTL, SOCCLK_G_WBIF0_GATE_DIS, mask_sh),     \
		SF(MMHUBBUB_CLOCK_CNTL, MMHUBBUB_FGCG_REP_DIS, mask_sh)

#define MCIF_WB_REG_FIELD_LIST_DCN3_5(type)          \
	struct {                                     \
		MCIF_WB_REG_FIELD_LIST_DCN3_0(type); \
		type MMHUBBUB_TEST_CLK_SEL;          \
		type DISPCLK_R_MMHUBBUB_GATE_DIS;    \
		type DISPCLK_G_WBIF0_GATE_DIS;       \
		type SOCCLK_G_WBIF0_GATE_DIS;        \
		type MMHUBBUB_FGCG_REP_DIS;          \
	}

struct dcn35_mmhubbub_registers {
	MCIF_WB_REG_VARIABLE_LIST_DCN3_5;
};

struct dcn35_mmhubbub_mask {
	MCIF_WB_REG_FIELD_LIST_DCN3_5(uint32_t);
};

struct dcn35_mmhubbub_shift {
	MCIF_WB_REG_FIELD_LIST_DCN3_5(uint8_t);
};

void dcn35_mmhubbub_construct(
	struct dcn30_mmhubbub *mcif_wb30, struct dc_context *ctx,
	const struct dcn35_mmhubbub_registers *mcif_wb_regs,
	const struct dcn35_mmhubbub_shift *mcif_wb_shift,
	const struct dcn35_mmhubbub_mask *mcif_wb_mask, int inst);

void dcn35_mmhubbub_set_fgcg(struct dcn30_mmhubbub *mcif_wb30, bool enabled);

#endif // __DCN35_MMHUBBUB_H
