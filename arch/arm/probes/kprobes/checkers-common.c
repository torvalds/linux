/*
 * arch/arm/probes/kprobes/checkers-common.c
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

enum probes_insn checker_stack_use_none(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	asi->stack_space = 0;
	return INSN_GOOD_NO_SLOT;
}

enum probes_insn checker_stack_use_unknown(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	asi->stack_space = -1;
	return INSN_GOOD_NO_SLOT;
}

#ifdef CONFIG_THUMB2_KERNEL
enum probes_insn checker_stack_use_imm_0xx(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	int imm = insn & 0xff;
	asi->stack_space = imm;
	return INSN_GOOD_NO_SLOT;
}

/*
 * Different from other insn uses imm8, the real addressing offset of
 * STRD in T32 encoding should be imm8 * 4. See ARMARM description.
 */
enum probes_insn checker_stack_use_t32strd(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	int imm = insn & 0xff;
	asi->stack_space = imm << 2;
	return INSN_GOOD_NO_SLOT;
}
#else
enum probes_insn checker_stack_use_imm_x0x(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	int imm = ((insn & 0xf00) >> 4) + (insn & 0xf);
	asi->stack_space = imm;
	return INSN_GOOD_NO_SLOT;
}
#endif

enum probes_insn checker_stack_use_imm_xxx(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	int imm = insn & 0xfff;
	asi->stack_space = imm;
	return INSN_GOOD_NO_SLOT;
}

enum probes_insn checker_stack_use_stmdx(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	unsigned int reglist = insn & 0xffff;
	int pbit = insn & (1 << 24);
	asi->stack_space = (hweight32(reglist) - (!pbit ? 1 : 0)) * 4;

	return INSN_GOOD_NO_SLOT;
}

const union decode_action stack_check_actions[] = {
	[STACK_USE_NONE] = {.decoder = checker_stack_use_none},
	[STACK_USE_UNKNOWN] = {.decoder = checker_stack_use_unknown},
#ifdef CONFIG_THUMB2_KERNEL
	[STACK_USE_FIXED_0XX] = {.decoder = checker_stack_use_imm_0xx},
	[STACK_USE_T32STRD] = {.decoder = checker_stack_use_t32strd},
#else
	[STACK_USE_FIXED_X0X] = {.decoder = checker_stack_use_imm_x0x},
#endif
	[STACK_USE_FIXED_XXX] = {.decoder = checker_stack_use_imm_xxx},
	[STACK_USE_STMDX] = {.decoder = checker_stack_use_stmdx},
};
