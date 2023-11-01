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

#ifndef __DCN35_DWB_H
#define __DCN35_DWB_H

#include "resource.h"
#include "dwb.h"
#include "dcn30/dcn30_dwb.h"

#define DWBC_COMMON_MASK_SH_LIST_DCN35(mask_sh) \
	DWBC_COMMON_MASK_SH_LIST_DCN30(mask_sh), \
	SF_DWB2(DWB_ENABLE_CLK_CTRL, DWB_TOP, 0, DWB_FGCG_REP_DIS, mask_sh)

#define DWBC_REG_FIELD_LIST_DCN3_5(type)          \
	struct {                                  \
		DWBC_REG_FIELD_LIST_DCN3_0(type); \
		type DWB_FGCG_REP_DIS;            \
	}

struct dcn35_dwbc_mask {
	DWBC_REG_FIELD_LIST_DCN3_5(uint32_t);
};

struct dcn35_dwbc_shift {
	DWBC_REG_FIELD_LIST_DCN3_5(uint8_t);
};

void dcn35_dwbc_construct(struct dcn30_dwbc *dwbc30,
	struct dc_context *ctx,
	const struct dcn30_dwbc_registers *dwbc_regs,
	const struct dcn35_dwbc_shift *dwbc_shift,
	const struct dcn35_dwbc_mask *dwbc_mask,
	int inst);

void dcn35_dwbc_set_fgcg(struct dcn30_dwbc *dwbc30, bool enable);

#endif
