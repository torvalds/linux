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

	ret = rv_insn_reg_get_val(regs, rs1_index, &base_addr);
	if (!ret)
		return ret;

	ret = rv_insn_reg_set_val(regs, rd_index, addr + 4);
	if (!ret)
		return ret;

	instruction_pointer_set(regs, (base_addr + sign_extend32((imm), 11))&~1);

	return ret;
}

#define auipc_rd_idx(opcode) \
	((opcode >> 7) & 0x1f)

#define auipc_imm(opcode) \
	((((opcode) >> 12) & 0xfffff) << 12)

#if __riscv_xlen == 64
#define auipc_offset(opcode)	sign_extend64(auipc_imm(opcode), 31)
#elif __riscv_xlen == 32
#define auipc_offset(opcode)	auipc_imm(opcode)
#else
#error "Unexpected __riscv_xlen"
#endif

bool __kprobes simulate_auipc(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	/*
	 * auipc instruction:
	 *  31        12 11 7 6      0
	 * | imm[31:12] | rd | opcode |
	 *        20       5     7
	 */

	u32 rd_idx = auipc_rd_idx(opcode);
	unsigned long rd_val = addr + auipc_offset(opcode);

	if (!rv_insn_reg_set_val(regs, rd_idx, rd_val))
		return false;

	instruction_pointer_set(regs, addr + 4);

	return true;
}

#define branch_rs1_idx(opcode) \
	(((opcode) >> 15) & 0x1f)

#define branch_rs2_idx(opcode) \
	(((opcode) >> 20) & 0x1f)

#define branch_funct3(opcode) \
	(((opcode) >> 12) & 0x7)

#define branch_imm(opcode) \
	(((((opcode) >>  8) & 0xf ) <<  1) | \
	 ((((opcode) >> 25) & 0x3f) <<  5) | \
	 ((((opcode) >>  7) & 0x1 ) << 11) | \
	 ((((opcode) >> 31) & 0x1 ) << 12))

#define branch_offset(opcode) \
	sign_extend32((branch_imm(opcode)), 12)

bool __kprobes simulate_branch(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	/*
	 * branch instructions:
	 *      31    30       25 24 20 19 15 14    12 11       8    7      6      0
	 * | imm[12] | imm[10:5] | rs2 | rs1 | funct3 | imm[4:1] | imm[11] | opcode |
	 *     1           6        5     5      3         4         1         7
	 *     imm[12|10:5]        rs2   rs1    000       imm[4:1|11]       1100011  BEQ
	 *     imm[12|10:5]        rs2   rs1    001       imm[4:1|11]       1100011  BNE
	 *     imm[12|10:5]        rs2   rs1    100       imm[4:1|11]       1100011  BLT
	 *     imm[12|10:5]        rs2   rs1    101       imm[4:1|11]       1100011  BGE
	 *     imm[12|10:5]        rs2   rs1    110       imm[4:1|11]       1100011  BLTU
	 *     imm[12|10:5]        rs2   rs1    111       imm[4:1|11]       1100011  BGEU
	 */

	s32 offset;
	s32 offset_tmp;
	unsigned long rs1_val;
	unsigned long rs2_val;

	if (!rv_insn_reg_get_val(regs, branch_rs1_idx(opcode), &rs1_val) ||
	    !rv_insn_reg_get_val(regs, branch_rs2_idx(opcode), &rs2_val))
		return false;

	offset_tmp = branch_offset(opcode);
	switch (branch_funct3(opcode)) {
	case RVG_FUNCT3_BEQ:
		offset = (rs1_val == rs2_val) ? offset_tmp : 4;
		break;
	case RVG_FUNCT3_BNE:
		offset = (rs1_val != rs2_val) ? offset_tmp : 4;
		break;
	case RVG_FUNCT3_BLT:
		offset = ((long)rs1_val < (long)rs2_val) ? offset_tmp : 4;
		break;
	case RVG_FUNCT3_BGE:
		offset = ((long)rs1_val >= (long)rs2_val) ? offset_tmp : 4;
		break;
	case RVG_FUNCT3_BLTU:
		offset = (rs1_val < rs2_val) ? offset_tmp : 4;
		break;
	case RVG_FUNCT3_BGEU:
		offset = (rs1_val >= rs2_val) ? offset_tmp : 4;
		break;
	default:
		return false;
	}

	instruction_pointer_set(regs, addr + offset);

	return true;
}
