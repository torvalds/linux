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

#include "dcn35_opp.h"
#include "reg_helper.h"

#define REG(reg) ((const struct dcn35_opp_registers *)(oppn20->regs))->reg

#undef FN
#define FN(reg_name, field_name)                                           \
	((const struct dcn35_opp_shift *)(oppn20->opp_shift))->field_name, \
		((const struct dcn35_opp_mask *)(oppn20->opp_mask))->field_name

#define CTX oppn20->base.ctx

void dcn35_opp_construct(struct dcn20_opp *oppn20, struct dc_context *ctx,
			 uint32_t inst, const struct dcn35_opp_registers *regs,
			 const struct dcn35_opp_shift *opp_shift,
			 const struct dcn35_opp_mask *opp_mask)
{
	dcn20_opp_construct(oppn20, ctx, inst,
			    (const struct dcn20_opp_registers *)regs,
			    (const struct dcn20_opp_shift *)opp_shift,
			    (const struct dcn20_opp_mask *)opp_mask);
}

void dcn35_opp_set_fgcg(struct dcn20_opp *oppn20, bool enable)
{
	REG_UPDATE(OPP_TOP_CLK_CONTROL, OPP_FGCG_REP_DIS, !enable);
}
