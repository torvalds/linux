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
		return true;
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

bool __kprobes simulate_c_j(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	/*
	 *  15    13 12                            2 1      0
	 * | funct3 | offset[11|4|9:8|10|6|7|3:1|5] | opcode |
	 *     3                   11                    2
	 */

	s32 offset;

	offset  = ((opcode >> 3)  & 0x7) << 1;
	offset |= ((opcode >> 11) & 0x1) << 4;
	offset |= ((opcode >> 2)  & 0x1) << 5;
	offset |= ((opcode >> 7)  & 0x1) << 6;
	offset |= ((opcode >> 6)  & 0x1) << 7;
	offset |= ((opcode >> 9)  & 0x3) << 8;
	offset |= ((opcode >> 8)  & 0x1) << 10;
	offset |= ((opcode >> 12) & 0x1) << 11;

	instruction_pointer_set(regs, addr + sign_extend32(offset, 11));

	return true;
}

static bool __kprobes simulate_c_jr_jalr(u32 opcode, unsigned long addr, struct pt_regs *regs,
					 bool is_jalr)
{
	/*
	 *  15    12 11  7 6   2 1  0
	 * | funct4 | rs1 | rs2 | op |
	 *     4       5     5    2
	 */

	unsigned long jump_addr;

	u32 rs1 = (opcode >> 7) & 0x1f;

	if (rs1 == 0) /* C.JR is only valid when rs1 != x0 */
		return false;

	if (!rv_insn_reg_get_val(regs, rs1, &jump_addr))
		return false;

	if (is_jalr && !rv_insn_reg_set_val(regs, 1, addr + 2))
		return false;

	instruction_pointer_set(regs, jump_addr);

	return true;
}

bool __kprobes simulate_c_jr(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	return simulate_c_jr_jalr(opcode, addr, regs, false);
}

bool __kprobes simulate_c_jalr(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	return simulate_c_jr_jalr(opcode, addr, regs, true);
}

static bool __kprobes simulate_c_bnez_beqz(u32 opcode, unsigned long addr, struct pt_regs *regs,
					   bool is_bnez)
{
	/*
	 *  15    13 12           10 9    7 6                 2 1  0
	 * | funct3 | offset[8|4:3] | rs1' | offset[7:6|2:1|5] | op |
	 *     3            3          3             5           2
	 */

	s32 offset;
	u32 rs1;
	unsigned long rs1_val;

	rs1 = 0x8 | ((opcode >> 7) & 0x7);

	if (!rv_insn_reg_get_val(regs, rs1, &rs1_val))
		return false;

	if ((rs1_val != 0 && is_bnez) || (rs1_val == 0 && !is_bnez)) {
		offset =  ((opcode >> 3)  & 0x3) << 1;
		offset |= ((opcode >> 10) & 0x3) << 3;
		offset |= ((opcode >> 2)  & 0x1) << 5;
		offset |= ((opcode >> 5)  & 0x3) << 6;
		offset |= ((opcode >> 12) & 0x1) << 8;
		offset = sign_extend32(offset, 8);
	} else {
		offset = 2;
	}

	instruction_pointer_set(regs, addr + offset);

	return true;
}

bool __kprobes simulate_c_bnez(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	return simulate_c_bnez_beqz(opcode, addr, regs, true);
}

bool __kprobes simulate_c_beqz(u32 opcode, unsigned long addr, struct pt_regs *regs)
{
	return simulate_c_bnez_beqz(opcode, addr, regs, false);
}
