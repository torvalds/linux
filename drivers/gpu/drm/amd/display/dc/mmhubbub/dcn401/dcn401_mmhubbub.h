/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Advanced Micro Devices, Inc. */


#ifndef __DCN401_MMHUBBUB_H
#define __DCN401_MMHUBBUB_H

#include "mcif_wb.h"
#include "dcn32/dcn32_mmhubbub.h"
#include "dcn35/dcn35_mmhubbub.h"

#define MCIF_WB_REG_VARIABLE_LIST_DCN4_01  \
	MCIF_WB_REG_VARIABLE_LIST_DCN3_5;

#define MCIF_WB_COMMON_MASK_SH_LIST_DCN4_01(mask_sh)                            \
	MCIF_WB_COMMON_MASK_SH_LIST_DCN3_5(mask_sh),                            \
    SF(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK, NB_PSTATE_CHANGE_WATERMARK_TYPE, mask_sh)

#define MCIF_WB_REG_FIELD_LIST_DCN4_01(type)          \
	struct {                                     \
		MCIF_WB_REG_FIELD_LIST_DCN3_5(type); \
		type NB_PSTATE_CHANGE_WATERMARK_TYPE;          \
	}

struct dcn401_mmhubbub_mask {
	MCIF_WB_REG_FIELD_LIST_DCN4_01(uint32_t);
};

struct dcn401_mmhubbub_shift {
	MCIF_WB_REG_FIELD_LIST_DCN4_01(uint8_t);
};

void dcn401_mmhubbub_construct(struct dcn30_mmhubbub *mcif_wb30,
		struct dc_context *ctx,
		const struct dcn35_mmhubbub_registers *mcif_wb_regs,
		const struct dcn401_mmhubbub_shift *mcif_wb_shift,
		const struct dcn401_mmhubbub_mask *mcif_wb_mask,
		int inst);

#endif // __DCN401_MMHUBBUB_H
