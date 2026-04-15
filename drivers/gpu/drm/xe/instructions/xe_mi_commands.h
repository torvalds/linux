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
#define MI_MATH(n)			(__MI_INSTR(0x1A) | XE_INSTR_NUM_DW((n) + 1))

#define MI_SEMAPHORE_WAIT		(__MI_INSTR(0x1c) | XE_INSTR_NUM_DW(5))
#define   MI_SEMW_GGTT			REG_BIT(22)
#define   MI_SEMW_POLL			REG_BIT(15)
#define   MI_SEMW_COMPARE_OP_MASK	REG_GENMASK(14, 12)
#define     COMPARE_OP_SAD_GT_SDD	0
#define     COMPARE_OP_SAD_GTE_SDD	1
#define     COMPARE_OP_SAD_LT_SDD	2
#define     COMPARE_OP_SAD_LTE_SDD	3
#define     COMPARE_OP_SAD_EQ_SDD	4
#define     COMPARE_OP_SAD_NEQ_SDD	5
#define   MI_SEMW_COMPARE(OP)		REG_FIELD_PREP(MI_SEMW_COMPARE_OP_MASK, COMPARE_OP_##OP)
#define   MI_SEMW_TOKEN(token)		REG_FIELD_PREP(REG_GENMASK(9, 2), (token))

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
#define   MI_LRI_LEN(x)			(((x) & 0xff) + 1)

#define MI_STORE_REGISTER_MEM		(__MI_INSTR(0x24) | XE_INSTR_NUM_DW(4))
#define   MI_SRM_USE_GGTT		REG_BIT(22)
#define   MI_SRM_ADD_CS_OFFSET		REG_BIT(19)

#define MI_FLUSH_DW			__MI_INSTR(0x26)
#define   MI_FLUSH_DW_PROTECTED_MEM_EN	REG_BIT(22)
#define   MI_FLUSH_DW_STORE_INDEX	REG_BIT(21)
#define   MI_INVALIDATE_TLB		REG_BIT(18)
#define   MI_FLUSH_DW_CCS		REG_BIT(16)
#define   MI_FLUSH_DW_OP_STOREDW	REG_BIT(14)
#define   MI_FLUSH_DW_LEN_DW		REG_GENMASK(5, 0)
#define   MI_FLUSH_IMM_DW		REG_FIELD_PREP(MI_FLUSH_DW_LEN_DW, 4 - 2)
#define   MI_FLUSH_IMM_QW		REG_FIELD_PREP(MI_FLUSH_DW_LEN_DW, 5 - 2)
#define   MI_FLUSH_DW_USE_GTT		REG_BIT(2)

#define MI_LOAD_REGISTER_MEM		(__MI_INSTR(0x29) | XE_INSTR_NUM_DW(4))
#define   MI_LRM_USE_GGTT		REG_BIT(22)
#define   MI_LRM_ASYNC			REG_BIT(21)

#define MI_LOAD_REGISTER_REG		(__MI_INSTR(0x2a) | XE_INSTR_NUM_DW(3))
#define   MI_LRR_DST_CS_MMIO		REG_BIT(19)
#define   MI_LRR_SRC_CS_MMIO		REG_BIT(18)

#define MI_COPY_MEM_MEM			(__MI_INSTR(0x2e) | XE_INSTR_NUM_DW(5))
#define   MI_COPY_MEM_MEM_SRC_GGTT	REG_BIT(22)
#define   MI_COPY_MEM_MEM_DST_GGTT	REG_BIT(21)

#define MI_BATCH_BUFFER_START		__MI_INSTR(0x31)

#define MI_SET_APPID			__MI_INSTR(0x0e)
#define MI_SET_APPID_SESSION_ID_MASK	REG_GENMASK(6, 0)
#define MI_SET_APPID_SESSION_ID(x)	REG_FIELD_PREP(MI_SET_APPID_SESSION_ID_MASK, x)

#define MI_SEMAPHORE_WAIT_TOKEN		(__MI_INSTR(0x1c) | XE_INSTR_NUM_DW(5)) /* XeLP+ */
#define   MI_SEMAPHORE_REGISTER_POLL	REG_BIT(16)
#define   MI_SEMAPHORE_POLL		REG_BIT(15)
#define   MI_SEMAPHORE_CMP_OP_MASK	REG_GENMASK(14, 12)
#define   MI_SEMAPHORE_SAD_EQ_SDD	REG_FIELD_PREP(MI_SEMAPHORE_CMP_OP_MASK, 4)

#endif
