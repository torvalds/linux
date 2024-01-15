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

#ifndef __DCN35_DSC_H__
#define __DCN35_DSC_H__

#include "dcn20/dcn20_dsc.h"

#define DSC_REG_LIST_SH_MASK_DCN35(mask_sh)  \
	DSC_REG_LIST_SH_MASK_DCN20(mask_sh), \
		DSC_SF(DSC_TOP0_DSC_TOP_CONTROL, DSC_FGCG_REP_DIS, mask_sh)

#define DSC_FIELD_LIST_DCN35(type)          \
	struct {                            \
		DSC_FIELD_LIST_DCN20(type); \
		type DSC_FGCG_REP_DIS;      \
	}

struct dcn35_dsc_shift {
	DSC_FIELD_LIST_DCN35(uint8_t);
};

struct dcn35_dsc_mask {
	DSC_FIELD_LIST_DCN35(uint32_t);
};

void dsc35_construct(struct dcn20_dsc *dsc,
		struct dc_context *ctx,
		int inst,
		const struct dcn20_dsc_registers *dsc_regs,
		const struct dcn35_dsc_shift *dsc_shift,
		const struct dcn35_dsc_mask *dsc_mask);

void dsc35_set_fgcg(struct dcn20_dsc *dsc20, bool enable);

#endif /* __DCN35_DSC_H__ */
