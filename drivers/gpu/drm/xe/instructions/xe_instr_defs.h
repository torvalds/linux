/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_INSTR_DEFS_H_
#define _XE_INSTR_DEFS_H_

#include "regs/xe_reg_defs.h"

/*
 * The first dword of any GPU instruction is the "instruction header."  Bits
 * 31:29 identify the general type of the command and determine how exact
 * opcodes and sub-opcodes will be encoded in the remaining bits.
 */
#define XE_INSTR_CMD_TYPE		GENMASK(31, 29)
#define   XE_INSTR_MI			REG_FIELD_PREP(XE_INSTR_CMD_TYPE, 0x0)
#define   XE_INSTR_GSC			REG_FIELD_PREP(XE_INSTR_CMD_TYPE, 0x2)
#define   XE_INSTR_VIDEOPIPE		REG_FIELD_PREP(XE_INSTR_CMD_TYPE, 0x3)
#define   XE_INSTR_GFXPIPE		REG_FIELD_PREP(XE_INSTR_CMD_TYPE, 0x3)
#define   XE_INSTR_GFX_STATE		REG_FIELD_PREP(XE_INSTR_CMD_TYPE, 0x4)

/*
 * Most (but not all) instructions have a "length" field in the instruction
 * header.  The value expected is the total number of dwords for the
 * instruction, minus two.
 *
 * Some instructions have length fields longer or shorter than 8 bits, but
 * those are rare.  This definition can be used for the common case where
 * the length field is from 7:0.
 */
#define XE_INSTR_LEN_MASK		GENMASK(7, 0)
#define XE_INSTR_NUM_DW(x)		REG_FIELD_PREP(XE_INSTR_LEN_MASK, (x) - 2)

#endif
