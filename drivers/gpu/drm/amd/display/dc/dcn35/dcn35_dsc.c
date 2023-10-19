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

#include "dcn35_dsc.h"
#include "reg_helper.h"

/* Macro definitios for REG_SET macros*/
#define CTX \
	dsc20->base.ctx

#define REG(reg)\
	dsc20->dsc_regs->reg

#undef FN
#define FN(reg_name, field_name)                                          \
	((const struct dcn35_dsc_shift *)(dsc20->dsc_shift))->field_name, \
		((const struct dcn35_dsc_mask *)(dsc20->dsc_mask))->field_name

#define DC_LOGGER \
	dsc->ctx->logger

void dsc35_construct(struct dcn20_dsc *dsc,
		struct dc_context *ctx,
		int inst,
		const struct dcn20_dsc_registers *dsc_regs,
		const struct dcn35_dsc_shift *dsc_shift,
		const struct dcn35_dsc_mask *dsc_mask)
{
	dsc2_construct(dsc, ctx, inst, dsc_regs,
		(const struct dcn20_dsc_shift *)(dsc_shift),
		(const struct dcn20_dsc_mask *)(dsc_mask));
}

void dsc35_set_fgcg(struct dcn20_dsc *dsc20, bool enable)
{
	REG_UPDATE(DSC_TOP_CONTROL, DSC_FGCG_REP_DIS, !enable);
}
