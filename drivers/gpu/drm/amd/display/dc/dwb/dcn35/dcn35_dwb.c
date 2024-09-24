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
#include "reg_helper.h"
#include "dcn35_dwb.h"

#define REG(reg)\
	dwbc30->dwbc_regs->reg

#define CTX \
	dwbc30->base.ctx

#undef FN
#define FN(reg_name, field_name)                                             \
	((const struct dcn35_dwbc_shift *)(dwbc30->dwbc_shift))->field_name, \
		((const struct dcn35_dwbc_mask *)(dwbc30->dwbc_mask))        \
			->field_name

#define DC_LOGGER \
	dwbc30->base.ctx->logger

void dcn35_dwbc_construct(struct dcn30_dwbc *dwbc30,
	struct dc_context *ctx,
	const struct dcn30_dwbc_registers *dwbc_regs,
	const struct dcn35_dwbc_shift *dwbc_shift,
	const struct dcn35_dwbc_mask *dwbc_mask,
	int inst)
{
	dcn30_dwbc_construct(dwbc30, ctx, dwbc_regs,
			     (const struct dcn30_dwbc_shift *)dwbc_shift,
			     (const struct dcn30_dwbc_mask *)dwbc_mask, inst);
}

void dcn35_dwbc_set_fgcg(struct dcn30_dwbc *dwbc30, bool enable)
{
	REG_UPDATE(DWB_ENABLE_CLK_CTRL, DWB_FGCG_REP_DIS, !enable);
}
