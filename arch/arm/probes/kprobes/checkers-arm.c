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
