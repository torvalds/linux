/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_ALU_COMMANDS_H_
#define _XE_ALU_COMMANDS_H_

#include "instructions/xe_instr_defs.h"

/* Instruction Opcodes */
#define CS_ALU_OPCODE_NOOP			0x000
#define CS_ALU_OPCODE_FENCE_RD			0x001
#define CS_ALU_OPCODE_FENCE_WR			0x002
#define CS_ALU_OPCODE_LOAD			0x080
#define CS_ALU_OPCODE_LOADINV			0x480
#define CS_ALU_OPCODE_LOAD0			0x081
#define CS_ALU_OPCODE_LOAD1			0x481
#define CS_ALU_OPCODE_LOADIND			0x082
#define CS_ALU_OPCODE_ADD			0x100
#define CS_ALU_OPCODE_SUB			0x101
#define CS_ALU_OPCODE_AND			0x102
#define CS_ALU_OPCODE_OR			0x103
#define CS_ALU_OPCODE_XOR			0x104
#define CS_ALU_OPCODE_SHL			0x105
#define CS_ALU_OPCODE_SHR			0x106
#define CS_ALU_OPCODE_SAR			0x107
#define CS_ALU_OPCODE_STORE			0x180
#define CS_ALU_OPCODE_STOREINV			0x580
#define CS_ALU_OPCODE_STOREIND			0x181

/* Instruction Operands */
#define CS_ALU_OPERAND_REG(n)			REG_FIELD_PREP(GENMASK(3, 0), (n))
#define CS_ALU_OPERAND_REG0			0x0
#define CS_ALU_OPERAND_REG1			0x1
#define CS_ALU_OPERAND_REG2			0x2
#define CS_ALU_OPERAND_REG3			0x3
#define CS_ALU_OPERAND_REG4			0x4
#define CS_ALU_OPERAND_REG5			0x5
#define CS_ALU_OPERAND_REG6			0x6
#define CS_ALU_OPERAND_REG7			0x7
#define CS_ALU_OPERAND_REG8			0x8
#define CS_ALU_OPERAND_REG9			0x9
#define CS_ALU_OPERAND_REG10			0xa
#define CS_ALU_OPERAND_REG11			0xb
#define CS_ALU_OPERAND_REG12			0xc
#define CS_ALU_OPERAND_REG13			0xd
#define CS_ALU_OPERAND_REG14			0xe
#define CS_ALU_OPERAND_REG15			0xf
#define CS_ALU_OPERAND_SRCA			0x20
#define CS_ALU_OPERAND_SRCB			0x21
#define CS_ALU_OPERAND_ACCU			0x31
#define CS_ALU_OPERAND_ZF			0x32
#define CS_ALU_OPERAND_CF			0x33
#define CS_ALU_OPERAND_NA			0 /* N/A operand */

/* Command Streamer ALU Instructions */
#define CS_ALU_INSTR(opcode, op1, op2)		(REG_FIELD_PREP(GENMASK(31, 20), (opcode)) | \
						 REG_FIELD_PREP(GENMASK(19, 10), (op1)) | \
						 REG_FIELD_PREP(GENMASK(9, 0), (op2)))

#define __CS_ALU_INSTR(opcode, op1, op2)	CS_ALU_INSTR(CS_ALU_OPCODE_##opcode, \
							     CS_ALU_OPERAND_##op1, \
							     CS_ALU_OPERAND_##op2)

#define CS_ALU_INSTR_NOOP			__CS_ALU_INSTR(NOOP, NA, NA)
#define CS_ALU_INSTR_LOAD(op1, op2)		__CS_ALU_INSTR(LOAD, op1, op2)
#define CS_ALU_INSTR_LOADINV(op1, op2)		__CS_ALU_INSTR(LOADINV, op1, op2)
#define CS_ALU_INSTR_LOAD0(op1)			__CS_ALU_INSTR(LOAD0, op1, NA)
#define CS_ALU_INSTR_LOAD1(op1)			__CS_ALU_INSTR(LOAD1, op1, NA)
#define CS_ALU_INSTR_ADD			__CS_ALU_INSTR(ADD, NA, NA)
#define CS_ALU_INSTR_SUB			__CS_ALU_INSTR(SUB, NA, NA)
#define CS_ALU_INSTR_AND			__CS_ALU_INSTR(AND, NA, NA)
#define CS_ALU_INSTR_OR				__CS_ALU_INSTR(OR, NA, NA)
#define CS_ALU_INSTR_XOR			__CS_ALU_INSTR(XOR, NA, NA)
#define CS_ALU_INSTR_STORE(op1, op2)		__CS_ALU_INSTR(STORE, op1, op2)
#define CS_ALU_INSTR_STOREINV(op1, op2)		__CS_ALU_INSTR(STOREINV, op1, op2)

#endif
