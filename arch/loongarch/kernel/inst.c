// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <asm/inst.h>

u32 larch_insn_gen_lu32id(enum loongarch_gpr rd, int imm)
{
	union loongarch_instruction insn;

	insn.reg1i20_format.opcode = lu32id_op;
	insn.reg1i20_format.rd = rd;
	insn.reg1i20_format.immediate = imm;

	return insn.word;
}

u32 larch_insn_gen_lu52id(enum loongarch_gpr rd, enum loongarch_gpr rj, int imm)
{
	union loongarch_instruction insn;

	insn.reg2i12_format.opcode = lu52id_op;
	insn.reg2i12_format.rd = rd;
	insn.reg2i12_format.rj = rj;
	insn.reg2i12_format.immediate = imm;

	return insn.word;
}

u32 larch_insn_gen_jirl(enum loongarch_gpr rd, enum loongarch_gpr rj, unsigned long pc, unsigned long dest)
{
	union loongarch_instruction insn;

	insn.reg2i16_format.opcode = jirl_op;
	insn.reg2i16_format.rd = rd;
	insn.reg2i16_format.rj = rj;
	insn.reg2i16_format.immediate = (dest - pc) >> 2;

	return insn.word;
}
