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

#include "dcn35_mmhubbub.h"
#include "reg_helper.h"

#define REG(reg)                                                             \
	((const struct dcn35_mmhubbub_registers *)(mcif_wb30->mcif_wb_regs)) \
		->reg

#define CTX mcif_wb30->base.ctx

#undef FN
#define FN(reg_name, field_name)                                                \
	((const struct dcn35_mmhubbub_shift *)(mcif_wb30->mcif_wb_shift))       \
		->field_name,                                                   \
		((const struct dcn35_mmhubbub_mask *)(mcif_wb30->mcif_wb_mask)) \
			->field_name

void dcn35_mmhubbub_construct(
	struct dcn30_mmhubbub *mcif_wb30, struct dc_context *ctx,
	const struct dcn35_mmhubbub_registers *mcif_wb_regs,
	const struct dcn35_mmhubbub_shift *mcif_wb_shift,
	const struct dcn35_mmhubbub_mask *mcif_wb_mask, int inst)
{
	dcn32_mmhubbub_construct(
		mcif_wb30, ctx,
		(const struct dcn30_mmhubbub_registers *)(mcif_wb_regs),
		(const struct dcn30_mmhubbub_shift *)(mcif_wb_shift),
		(const struct dcn30_mmhubbub_mask *)(mcif_wb_mask), inst);
}

void dcn35_mmhubbub_set_fgcg(struct dcn30_mmhubbub *mcif_wb30, bool enabled)
{
	REG_UPDATE(MMHUBBUB_CLOCK_CNTL, MMHUBBUB_FGCG_REP_DIS, !enabled);
}
