/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_MFX_COMMANDS_H_
#define _XE_MFX_COMMANDS_H_

#include "instructions/xe_instr_defs.h"

#define MFX_CMD_SUBTYPE		REG_GENMASK(28, 27) /* A.K.A cmd pipe */
#define MFX_CMD_OPCODE		REG_GENMASK(26, 24)
#define MFX_CMD_SUB_OPCODE	REG_GENMASK(23, 16)
#define MFX_FLAGS_AND_LEN	REG_GENMASK(15, 0)

#define XE_MFX_INSTR(subtype, op, sub_op) \
	(XE_INSTR_VIDEOPIPE | \
	 REG_FIELD_PREP(MFX_CMD_SUBTYPE, subtype) | \
	 REG_FIELD_PREP(MFX_CMD_OPCODE, op) | \
	 REG_FIELD_PREP(MFX_CMD_SUB_OPCODE, sub_op))

#define MFX_WAIT				XE_MFX_INSTR(1, 0, 0)
#define MFX_WAIT_DW0_PXP_SYNC_CONTROL_FLAG	REG_BIT(9)
#define MFX_WAIT_DW0_MFX_SYNC_CONTROL_FLAG	REG_BIT(8)

#define CRYPTO_KEY_EXCHANGE			XE_MFX_INSTR(2, 6, 9)

#endif
