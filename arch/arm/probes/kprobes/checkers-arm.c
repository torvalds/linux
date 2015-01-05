/*
 * arch/arm/probes/kprobes/checkers-arm.c
 *
 * Copyright (C) 2014 Huawei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include "../decode.h"
#include "../decode-arm.h"
#include "checkers.h"

static enum probes_insn __kprobes arm_check_stack(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	/*
	 * PROBES_LDRSTRD, PROBES_LDMSTM, PROBES_STORE,
	 * PROBES_STORE_EXTRA may get here. Simply mark all normal
	 * insns as STACK_USE_NONE.
	 */
	static const union decode_item table[] = {
		/*
		 * 'STR{,D,B,H}, Rt, [Rn, Rm]' should be marked as UNKNOWN
		 * if Rn or Rm is SP.
		 *                                 x
		 * STR (register)	cccc 011x x0x0 xxxx xxxx xxxx xxxx xxxx
		 * STRB (register)	cccc 011x x1x0 xxxx xxxx xxxx xxxx xxxx
		 */
		DECODE_OR	(0x0e10000f, 0x0600000d),
		DECODE_OR	(0x0e1f0000, 0x060d0000),

		/*
		 *                                                     x
		 * STRD (register)	cccc 000x x0x0 xxxx xxxx xxxx 1111 xxxx
		 * STRH (register)	cccc 000x x0x0 xxxx xxxx xxxx 1011 xxxx
		 */
		DECODE_OR	(0x0e5000bf, 0x000000bd),
		DECODE_CUSTOM	(0x0e5f00b0, 0x000d00b0, STACK_USE_UNKNOWN),

		/*
		 * For PROBES_LDMSTM, only stmdx sp, [...] need to examine
		 *
		 * Bit B/A (bit 24) encodes arithmetic operation order. 1 means
		 * before, 0 means after.
		 * Bit I/D (bit 23) encodes arithmetic operation. 1 means
		 * increment, 0 means decrement.
		 *
		 * So:
		 *                              B I
		 *                              / /
		 *                              A D   | Rn |
		 * STMDX SP, [...]	cccc 100x 00x0 xxxx xxxx xxxx xxxx xxxx
		 */
		DECODE_CUSTOM	(0x0edf0000, 0x080d0000, STACK_USE_STMDX),

		/*                              P U W | Rn | Rt |     imm12    |*/
		/* STR (immediate)	cccc 010x x0x0 1101 xxxx xxxx xxxx xxxx */
		/* STRB (immediate)	cccc 010x x1x0 1101 xxxx xxxx xxxx xxxx */
		/*                              P U W | Rn | Rt |imm4|    |imm4|*/
		/* STRD (immediate)	cccc 000x x1x0 1101 xxxx xxxx 1111 xxxx */
		/* STRH (immediate)	cccc 000x x1x0 1101 xxxx xxxx 1011 xxxx */
		/*
		 * index = (P == '1'); add = (U == '1').
		 * Above insns with:
		 *    index == 0 (str{,d,h} rx, [sp], #+/-imm) or
		 *    add == 1 (str{,d,h} rx, [sp, #+<imm>])
		 * should be STACK_USE_NONE.
		 * Only str{,b,d,h} rx,[sp,#-n] (P == 1 and U == 0) are
		 * required to be examined.
		 */
		/* STR{,B} Rt,[SP,#-n]	cccc 0101 0xx0 1101 xxxx xxxx xxxx xxxx */
		DECODE_CUSTOM	(0x0f9f0000, 0x050d0000, STACK_USE_FIXED_XXX),

		/* STR{D,H} Rt,[SP,#-n]	cccc 0001 01x0 1101 xxxx xxxx 1x11 xxxx */
		DECODE_CUSTOM	(0x0fdf00b0, 0x014d00b0, STACK_USE_FIXED_X0X),

		/* fall through */
		DECODE_CUSTOM	(0, 0, STACK_USE_NONE),
		DECODE_END
	};

	return probes_decode_insn(insn, asi, table, false, false, stack_check_actions, NULL);
}

const struct decode_checker arm_stack_checker[NUM_PROBES_ARM_ACTIONS] = {
	[PROBES_LDRSTRD] = {.checker = arm_check_stack},
	[PROBES_STORE_EXTRA] = {.checker = arm_check_stack},
	[PROBES_STORE] = {.checker = arm_check_stack},
	[PROBES_LDMSTM] = {.checker = arm_check_stack},
};

static enum probes_insn __kprobes arm_check_regs_nouse(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	asi->register_usage_flags = 0;
	return INSN_GOOD;
}

static enum probes_insn arm_check_regs_normal(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	u32 regs = h->type_regs.bits >> DECODE_TYPE_BITS;
	int i;

	asi->register_usage_flags = 0;
	for (i = 0; i < 5; regs >>= 4, insn >>= 4, i++)
		if (regs & 0xf)
			asi->register_usage_flags |= 1 << (insn & 0xf);

	return INSN_GOOD;
}


static enum probes_insn arm_check_regs_ldmstm(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	unsigned int reglist = insn & 0xffff;
	unsigned int rn = (insn >> 16) & 0xf;
	asi->register_usage_flags = reglist | (1 << rn);
	return INSN_GOOD;
}

static enum probes_insn arm_check_regs_mov_ip_sp(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	/* Instruction is 'mov ip, sp' i.e. 'mov r12, r13' */
	asi->register_usage_flags = (1 << 12) | (1<< 13);
	return INSN_GOOD;
}

/*
 *                                    | Rn |Rt/d|         | Rm |
 * LDRD (register)      cccc 000x x0x0 xxxx xxxx xxxx 1101 xxxx
 * STRD (register)      cccc 000x x0x0 xxxx xxxx xxxx 1111 xxxx
 *                                    | Rn |Rt/d|         |imm4L|
 * LDRD (immediate)     cccc 000x x1x0 xxxx xxxx xxxx 1101 xxxx
 * STRD (immediate)     cccc 000x x1x0 xxxx xxxx xxxx 1111 xxxx
 *
 * Such instructions access Rt/d and its next register, so different
 * from others, a specific checker is required to handle this extra
 * implicit register usage.
 */
static enum probes_insn arm_check_regs_ldrdstrd(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	int rdt = (insn >> 12) & 0xf;
	arm_check_regs_normal(insn, asi, h);
	asi->register_usage_flags |= 1 << (rdt + 1);
	return INSN_GOOD;
}


const struct decode_checker arm_regs_checker[NUM_PROBES_ARM_ACTIONS] = {
	[PROBES_MRS] = {.checker = arm_check_regs_normal},
	[PROBES_SATURATING_ARITHMETIC] = {.checker = arm_check_regs_normal},
	[PROBES_MUL1] = {.checker = arm_check_regs_normal},
	[PROBES_MUL2] = {.checker = arm_check_regs_normal},
	[PROBES_MUL_ADD_LONG] = {.checker = arm_check_regs_normal},
	[PROBES_MUL_ADD] = {.checker = arm_check_regs_normal},
	[PROBES_LOAD] = {.checker = arm_check_regs_normal},
	[PROBES_LOAD_EXTRA] = {.checker = arm_check_regs_normal},
	[PROBES_STORE] = {.checker = arm_check_regs_normal},
	[PROBES_STORE_EXTRA] = {.checker = arm_check_regs_normal},
	[PROBES_DATA_PROCESSING_REG] = {.checker = arm_check_regs_normal},
	[PROBES_DATA_PROCESSING_IMM] = {.checker = arm_check_regs_normal},
	[PROBES_SEV] = {.checker = arm_check_regs_nouse},
	[PROBES_WFE] = {.checker = arm_check_regs_nouse},
	[PROBES_SATURATE] = {.checker = arm_check_regs_normal},
	[PROBES_REV] = {.checker = arm_check_regs_normal},
	[PROBES_MMI] = {.checker = arm_check_regs_normal},
	[PROBES_PACK] = {.checker = arm_check_regs_normal},
	[PROBES_EXTEND] = {.checker = arm_check_regs_normal},
	[PROBES_EXTEND_ADD] = {.checker = arm_check_regs_normal},
	[PROBES_BITFIELD] = {.checker = arm_check_regs_normal},
	[PROBES_LDMSTM] = {.checker = arm_check_regs_ldmstm},
	[PROBES_MOV_IP_SP] = {.checker = arm_check_regs_mov_ip_sp},
	[PROBES_LDRSTRD] = {.checker = arm_check_regs_ldrdstrd},
};
