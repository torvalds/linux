/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_MI_COMMANDS_H_
#define _XE_MI_COMMANDS_H_

#include "instructions/xe_instr_defs.h"

/*
 * MI (Memory Interface) commands are supported by all GT engines.  They
 * provide general memory operations and command streamer control.  MI commands
 * have a command type of 0x0 (MI_COMMAND) in bits 31:29 of the instruction
 * header dword and a specific MI opcode in bits 28:23.
 */

#define MI_OPCODE			REG_GENMASK(28, 23)
#define MI_SUBOPCODE			REG_GENMASK(22, 17)  /* used with MI_EXPANSION */

#define __MI_INSTR(opcode) \
	(XE_INSTR_MI | REG_FIELD_PREP(MI_OPCODE, opcode))

#define MI_NOOP				__MI_INSTR(0x0)
#define MI_USER_INTERRUPT		__MI_INSTR(0x2)
#define MI_ARB_CHECK			__MI_INSTR(0x5)

#define MI_ARB_ON_OFF			__MI_INSTR(0x8)
#define   MI_ARB_ENABLE			REG_BIT(0)
#define   MI_ARB_DISABLE		0x0

#define MI_BATCH_BUFFER_END		__MI_INSTR(0xA)
#define MI_TOPOLOGY_FILTER		__MI_INSTR(0xD)
#define MI_FORCE_WAKEUP			__MI_INSTR(0x1D)

#define MI_STORE_DATA_IMM		__MI_INSTR(0x20)
#define   MI_SDI_GGTT			REG_BIT(22)
#define   MI_SDI_LEN_DW			GENMASK(9, 0)
#define   MI_SDI_NUM_DW(x)		REG_FIELD_PREP(MI_SDI_LEN_DW, (x) + 3 - 2)
#define   MI_SDI_NUM_QW(x)		(REG_FIELD_PREP(MI_SDI_LEN_DW, 2 * (x) + 3 - 2) | \
					 REG_BIT(21))

#define MI_LOAD_REGISTER_IMM		__MI_INSTR(0x22)
#define   MI_LRI_LRM_CS_MMIO		REG_BIT(19)
#define   MI_LRI_MMIO_REMAP_EN		REG_BIT(17)
#define   MI_LRI_NUM_REGS(x)		XE_INSTR_NUM_DW(2 * (x) + 1)
#define   MI_LRI_FORCE_POSTED		REG_BIT(12)

#define MI_FLUSH_DW			__MI_INSTR(0x26)
#define   MI_FLUSH_DW_STORE_INDEX	REG_BIT(21)
#define   MI_INVALIDATE_TLB		REG_BIT(18)
#define   MI_FLUSH_DW_CCS		REG_BIT(16)
#define   MI_FLUSH_DW_OP_STOREDW	REG_BIT(14)
#define   MI_FLUSH_DW_LEN_DW		REG_GENMASK(5, 0)
#define   MI_FLUSH_IMM_DW		REG_FIELD_PREP(MI_FLUSH_DW_LEN_DW, 4 - 2)
#define   MI_FLUSH_IMM_QW		REG_FIELD_PREP(MI_FLUSH_DW_LEN_DW, 5 - 2)
#define   MI_FLUSH_DW_USE_GTT		REG_BIT(2)

#define MI_BATCH_BUFFER_START		__MI_INSTR(0x31)

#endif
