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
 * Authors: AMD
 *
 */

#ifndef __DCN35_DPP_H__
#define __DCN35_DPP_H__

#include "dcn32/dcn32_dpp.h"

#define DPP_REG_LIST_SH_MASK_DCN35(mask_sh)  \
	DPP_REG_LIST_SH_MASK_DCN30_COMMON(mask_sh), \
		TF_SF(DPP_TOP0_DPP_CONTROL, DPP_FGCG_REP_DIS, mask_sh), \
		TF_SF(DPP_TOP0_DPP_CONTROL, DPP_FGCG_REP_DIS, mask_sh), \
		TF_SF(DPP_TOP0_DPP_CONTROL, DISPCLK_R_GATE_DISABLE, mask_sh)

#define DPP_REG_FIELD_LIST_DCN35(type)         \
	struct {                               \
		DPP_REG_FIELD_LIST_DCN3(type); \
		type DPP_FGCG_REP_DIS;         \
	}

struct dcn35_dpp_shift {
	DPP_REG_FIELD_LIST_DCN35(uint8_t);
};

struct dcn35_dpp_mask {
	DPP_REG_FIELD_LIST_DCN35(uint32_t);
};

void dpp35_dppclk_control(
		struct dpp *dpp_base,
		bool dppclk_div,
		bool enable);

bool dpp35_construct(struct dcn3_dpp *dpp3, struct dc_context *ctx,
		     uint32_t inst, const struct dcn3_dpp_registers *tf_regs,
		     const struct dcn35_dpp_shift *tf_shift,
		     const struct dcn35_dpp_mask *tf_mask);

void dpp35_set_fgcg(struct dcn3_dpp *dpp, bool enable);

#endif // __DCN35_DPP_H
