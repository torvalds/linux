// SPDX-License-Identifier: MIT
/* Copyright 2025 Advanced Micro Devices, Inc. */

#include "dcn60_opp.h"
#include "reg_helper.h"

#define REG(reg) ((const struct dcn60_opp_registers *)(oppn20->regs))->reg

#undef FN
#define FN(reg_name, field_name)                                           \
	((const struct dcn60_opp_shift *)(oppn20->opp_shift))->field_name, \
		((const struct dcn60_opp_mask *)(oppn20->opp_mask))->field_name

#define CTX oppn20->base.ctx

void dcn60_opp_construct(struct dcn20_opp *oppn20, struct dc_context *ctx,
		 uint32_t inst, const struct dcn60_opp_registers *regs,
		 const struct dcn60_opp_shift *opp_shift,
		 const struct dcn60_opp_mask *opp_mask)
{
	dcn20_opp_construct(oppn20, ctx, inst,
			    (const struct dcn20_opp_registers *)regs,
			    (const struct dcn20_opp_shift *)opp_shift,
			    (const struct dcn20_opp_mask *)opp_mask);
}
