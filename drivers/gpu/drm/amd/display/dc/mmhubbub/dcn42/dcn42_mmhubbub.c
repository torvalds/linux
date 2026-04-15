// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dcn35/dcn35_mmhubbub.h"
#include "dcn42_mmhubbub.h"
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

void dcn42_mmhubbub_set_fgcg(struct dcn30_mmhubbub *mcif_wb30, bool enabled)
{
	REG_UPDATE(MMHUBBUB_CLOCK_CNTL, MMHUBBUB_FGCG_REP_DIS, !enabled);
}
