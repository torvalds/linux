/*
 * Copyright 2015 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */
#ifndef __ASM_TILE_INSN_H
#define __ASM_TILE_INSN_H

#include <arch/opcode.h>

static inline tilegx_bundle_bits NOP(void)
{
	return create_UnaryOpcodeExtension_X0(FNOP_UNARY_OPCODE_X0) |
		create_RRROpcodeExtension_X0(UNARY_RRR_0_OPCODE_X0) |
		create_Opcode_X0(RRR_0_OPCODE_X0) |
		create_UnaryOpcodeExtension_X1(NOP_UNARY_OPCODE_X1) |
		create_RRROpcodeExtension_X1(UNARY_RRR_0_OPCODE_X1) |
		create_Opcode_X1(RRR_0_OPCODE_X1);
}

static inline tilegx_bundle_bits tilegx_gen_branch(unsigned long pc,
					    unsigned long addr,
					    bool link)
{
	tilegx_bundle_bits opcode_x0, opcode_x1;
	long pcrel_by_instr = (addr - pc) >> TILEGX_LOG2_BUNDLE_SIZE_IN_BYTES;

	if (link) {
		/* opcode: jal addr */
		opcode_x1 =
			create_Opcode_X1(JUMP_OPCODE_X1) |
			create_JumpOpcodeExtension_X1(JAL_JUMP_OPCODE_X1) |
			create_JumpOff_X1(pcrel_by_instr);
	} else {
		/* opcode: j addr */
		opcode_x1 =
			create_Opcode_X1(JUMP_OPCODE_X1) |
			create_JumpOpcodeExtension_X1(J_JUMP_OPCODE_X1) |
			create_JumpOff_X1(pcrel_by_instr);
	}

	/* opcode: fnop */
	opcode_x0 =
		create_UnaryOpcodeExtension_X0(FNOP_UNARY_OPCODE_X0) |
		create_RRROpcodeExtension_X0(UNARY_RRR_0_OPCODE_X0) |
		create_Opcode_X0(RRR_0_OPCODE_X0);

	return opcode_x1 | opcode_x0;
}

#endif /* __ASM_TILE_INSN_H */
