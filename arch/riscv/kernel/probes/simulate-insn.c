// SPDX-License-Identifier: GPL-2.0+

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

#include "decode-insn.h"
#include "simulate-insn.h"

static inline bool rv_insn_reg_get_val(struct pt_regs *regs, u32 index,
				       unsigned long *ptr)
{
	if (index == 0)
		*ptr = 0;
	else if (index <= 31)
		*ptr = *((unsigned long *)regs + index);
	else
		return false;

	return true;
}

static inline bool rv_insn_reg_set_val(struct pt_regs *regs, u32 index,
				       unsigned long val)
{
	if (index == 0)
		return false;
	else if (index <= 31)
		*((unsigned long *)regs + index) = val;
	else
		return false;

	return true;
}

bool __kprobes simulate_jal(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	/*
	 *     31    30       21    20     19        12 11 7 6      0
	 * imm [20] | imm[10:1] | imm[11] | imm[19:12] | rd | opcode
	 *     1         10          1           8       5    JAL/J
	 */
	bool ret;
	u32 imm;
	u32 index = (opcode >> 7) & 0x1f;

	ret = rv_insn_reg_set_val(regs, index, addr + 4);
	if (!ret)
		return ret;

	imm  = ((opcode >> 21) & 0x3ff) << 1;
	imm |= ((opcode >> 20) & 0x1)   << 11;
	imm |= ((opcode >> 12) & 0xff)  << 12;
	imm |= ((opcode >> 31) & 0x1)   << 20;

	instruction_pointer_set(regs, addr + sign_extend32((imm), 20));

	return ret;
}

bool __kprobes simulate_jalr(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	/*
	 * 31          20 19 15 14 12 11 7 6      0
	 *  offset[11:0] | rs1 | 010 | rd | opcode
	 *      12         5      3    5    JALR/JR
	 */
	bool ret;
	unsigned long base_addr;
	u32 imm = (opcode >> 20) & 0xfff;
	u32 rd_index = (opcode >> 7) & 0x1f;
	u32 rs1_index = (opcode >> 15) & 0x1f;

	ret = rv_insn_reg_set_val(regs, rd_index, addr + 4);
	if (!ret)
		return ret;

	ret = rv_insn_reg_get_val(regs, rs1_index, &base_addr);
	if (!ret)
		return ret;

	instruction_pointer_set(regs, (base_addr + sign_extend32((imm), 11))&~1);

	return ret;
}
