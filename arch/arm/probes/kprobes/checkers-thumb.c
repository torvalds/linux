// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/probes/kprobes/checkers-thumb.c
 *
 * Copyright (C) 2014 Huawei Inc.
 */

#include <linux/kernel.h>
#include "../decode.h"
#include "../decode-thumb.h"
#include "checkers.h"

static enum probes_insn __kprobes t32_check_stack(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	/*
	 * PROBES_T32_LDMSTM, PROBES_T32_LDRDSTRD and PROBES_T32_LDRSTR
	 * may get here. Simply mark all normal insns as STACK_USE_NONE.
	 */
	static const union decode_item table[] = {

		/*
		 * First, filter out all ldr insns to make our life easier.
		 * Following load insns may come here:
		 * LDM, LDRD, LDR.
		 * In T32 encoding, bit 20 is enough for distinguishing
		 * load and store. All load insns have this bit set, when
		 * all store insns have this bit clear.
		 */
		DECODE_CUSTOM	(0x00100000, 0x00100000, STACK_USE_NONE),

		/*
		 * Mark all 'STR{,B,H}, Rt, [Rn, Rm]' as STACK_USE_UNKNOWN
		 * if Rn or Rm is SP. T32 doesn't encode STRD.
		 */
		/*                                 xx | Rn | Rt |         | Rm |*/
		/* STR (register)	1111 1000 0100 xxxx xxxx 0000 00xx xxxx */
		/* STRB (register)	1111 1000 0000 xxxx xxxx 0000 00xx xxxx */
		/* STRH (register)	1111 1000 0010 xxxx xxxx 0000 00xx xxxx */
		/* INVALID INSN		1111 1000 0110 xxxx xxxx 0000 00xx xxxx */
		/* By Introducing INVALID INSN, bit 21 and 22 can be ignored. */
		DECODE_OR	(0xff9f0fc0, 0xf80d0000),
		DECODE_CUSTOM	(0xff900fcf, 0xf800000d, STACK_USE_UNKNOWN),


		/*                                 xx | Rn | Rt | PUW|   imm8  |*/
		/* STR (imm 8)		1111 1000 0100 1101 xxxx 110x xxxx xxxx */
		/* STRB (imm 8)		1111 1000 0000 1101 xxxx 110x xxxx xxxx */
		/* STRH (imm 8)		1111 1000 0010 1101 xxxx 110x xxxx xxxx */
		/* INVALID INSN		1111 1000 0110 1101 xxxx 110x xxxx xxxx */
		/* Only consider U == 0 and P == 1: strx rx, [sp, #-<imm>] */
		DECODE_CUSTOM	(0xff9f0e00, 0xf80d0c00, STACK_USE_FIXED_0XX),

		/* For STR{,B,H} (imm 12), offset is always positive, so ignore them. */

		/*                              P U W | Rn | Rt | Rt2|   imm8  |*/
		/* STRD (immediate)	1110 1001 01x0 1101 xxxx xxxx xxxx xxxx */
		/*
		 * Only consider U == 0 and P == 1.
		 * Also note that STRD in T32 encoding is special:
		 * imm = ZeroExtend(imm8:'00', 32)
		 */
		DECODE_CUSTOM	(0xffdf0000, 0xe94d0000, STACK_USE_T32STRD),

		/*                                    | Rn | */
		/* STMDB		1110 1001 00x0 1101 xxxx xxxx xxxx xxxx */
		DECODE_CUSTOM	(0xffdf0000, 0xe90d0000, STACK_USE_STMDX),

		/* fall through */
		DECODE_CUSTOM	(0, 0, STACK_USE_NONE),
		DECODE_END
	};

	return probes_decode_insn(insn, asi, table, false, false, stack_check_actions, NULL);
}

const struct decode_checker t32_stack_checker[NUM_PROBES_T32_ACTIONS] = {
	[PROBES_T32_LDMSTM] = {.checker = t32_check_stack},
	[PROBES_T32_LDRDSTRD] = {.checker = t32_check_stack},
	[PROBES_T32_LDRSTR] = {.checker = t32_check_stack},
};

/*
 * See following comments. This insn must be 'push'.
 */
static enum probes_insn __kprobes t16_check_stack(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	unsigned int reglist = insn & 0x1ff;
	asi->stack_space = hweight32(reglist) * 4;
	return INSN_GOOD;
}

/*
 * T16 encoding is simple: only the 'push' insn can need extra stack space.
 * Other insns, like str, can only use r0-r7 as Rn.
 */
const struct decode_checker t16_stack_checker[NUM_PROBES_T16_ACTIONS] = {
	[PROBES_T16_PUSH] = {.checker = t16_check_stack},
};
