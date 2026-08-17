/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Advanced Micro Devices, Inc. */

#include "../dmub_srv.h"
#include "dmub_reg.h"
#include "dmub_dcn42b.h"

#include "dcn/dcn_4_2_1_offset.h"
#include "dcn/dcn_4_2_1_sh_mask.h"

#define BASE_INNER(seg) ctx->dcn_reg_offsets[seg]
#define CTX dmub
#define REGS dmub->regs_dcn42
#define REG_OFFSET_EXP(reg_name) BASE(reg##reg_name##_BASE_IDX) + reg##reg_name

void dmub_srv_dcn42b_regs_init(struct dmub_srv *dmub, struct dc_context *ctx)
{
	struct dmub_srv_dcn42_regs *regs = dmub->regs_dcn42;
#define REG_STRUCT regs

#define DMUB_SR(reg) REG_STRUCT->offset.reg = REG_OFFSET_EXP(reg);
	DMUB_DCN42_REGS()
	DMCUB_INTERNAL_REGS()
#undef DMUB_SR

#define DMUB_SF(reg, field) REG_STRUCT->mask.reg##__##field = FD_MASK(reg, field);
	DMUB_DCN42_FIELDS()
#undef DMUB_SF

#define DMUB_SF(reg, field) REG_STRUCT->shift.reg##__##field = FD_SHIFT(reg, field);
	DMUB_DCN42_FIELDS()
#undef DMUB_SF
#undef REG_STRUCT
}
