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

#include "core_types.h"
#include "dcn35_dpp.h"
#include "reg_helper.h"

#define REG(reg) dpp->tf_regs->reg

#define CTX dpp->base.ctx

#undef FN
#define FN(reg_name, field_name)                                       \
	((const struct dcn35_dpp_shift *)(dpp->tf_shift))->field_name, \
	((const struct dcn35_dpp_mask *)(dpp->tf_mask))->field_name

bool dpp35_construct(struct dcn3_dpp *dpp, struct dc_context *ctx,
		     uint32_t inst, const struct dcn3_dpp_registers *tf_regs,
		     const struct dcn35_dpp_shift *tf_shift,
		     const struct dcn35_dpp_mask *tf_mask)
{
	return dpp32_construct(dpp, ctx, inst, tf_regs,
			      (const struct dcn3_dpp_shift *)(tf_shift),
			      (const struct dcn3_dpp_mask *)(tf_mask));
}

void dpp35_set_fgcg(struct dcn3_dpp *dpp, bool enable)
{
	REG_UPDATE(DPP_CONTROL, DPP_FGCG_REP_DIS, !enable);
}
