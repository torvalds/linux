/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_INST_H
#define _ASM_INST_H

#include <linux/types.h>
#include <asm/asm.h>

#define ADDR_IMMMASK_LU52ID	0xFFF0000000000000
#define ADDR_IMMMASK_LU32ID	0x000FFFFF00000000
#define ADDR_IMMMASK_ADDU16ID	0x00000000FFFF0000

#define ADDR_IMMSHIFT_LU52ID	52
#define ADDR_IMMSHIFT_LU32ID	32
#define ADDR_IMMSHIFT_ADDU16ID	16

#define ADDR_IMM(addr, INSN)	((addr & ADDR_IMMMASK_##INSN) >> ADDR_IMMSHIFT_##INSN)

enum reg1i20_op {
	lu12iw_op	= 0x0a,
	lu32id_op	= 0x0b,
};

enum reg1i21_op {
	beqz_op		= 0x10,
	bnez_op		= 0x11,
};

enum reg2i12_op {
	addiw_op	= 0x0a,
	addid_op	= 0x0b,
	lu52id_op	= 0x0c,
	ldb_op		= 0xa0,
	ldh_op		= 0xa1,
	ldw_op		= 0xa2,
	ldd_op		= 0xa3,
	stb_op		= 0xa4,
	sth_op		= 0xa5,
	stw_op		= 0xa6,
	std_op		= 0xa7,
};

enum reg2i16_op {
	jirl_op		= 0x13,
	beq_op		= 0x16,
	bne_op		= 0x17,
	blt_op		= 0x18,
	bge_op		= 0x19,
	bltu_op		= 0x1a,
	bgeu_op		= 0x1b,
};

struct reg0i26_format {
	unsigned int immediate_h : 10;
	unsigned int immediate_l : 16;
	unsigned int opcode : 6;
};

struct reg1i20_format {
	unsigned int rd : 5;
	unsigned int immediate : 20;
	unsigned int opcode : 7;
};

struct reg1i21_format {
	unsigned int immediate_h  : 5;
	unsigned int rj : 5;
	unsigned int immediate_l : 16;
	unsigned int opcode : 6;
};

struct reg2i12_format {
	unsigned int rd : 5;
	unsigned int rj : 5;
	unsigned int immediate : 12;
	unsigned int opcode : 10;
};

struct reg2i16_format {
	unsigned int rd : 5;
	unsigned int rj : 5;
	unsigned int immediate : 16;
	unsigned int opcode : 6;
};

union loongarch_instruction {
	unsigned int word;
	struct reg0i26_format reg0i26_format;
	struct reg1i20_format reg1i20_format;
	struct reg1i21_format reg1i21_format;
	struct reg2i12_format reg2i12_format;
	struct reg2i16_format reg2i16_format;
};

#define LOONGARCH_INSN_SIZE	sizeof(union loongarch_instruction)

enum loongarch_gpr {
	LOONGARCH_GPR_ZERO = 0,
	LOONGARCH_GPR_RA = 1,
	LOONGARCH_GPR_TP = 2,
	LOONGARCH_GPR_SP = 3,
	LOONGARCH_GPR_A0 = 4,	/* Reused as V0 for return value */
	LOONGARCH_GPR_A1,	/* Reused as V1 for return value */
	LOONGARCH_GPR_A2,
	LOONGARCH_GPR_A3,
	LOONGARCH_GPR_A4,
	LOONGARCH_GPR_A5,
	LOONGARCH_GPR_A6,
	LOONGARCH_GPR_A7,
	LOONGARCH_GPR_T0 = 12,
	LOONGARCH_GPR_T1,
	LOONGARCH_GPR_T2,
	LOONGARCH_GPR_T3,
	LOONGARCH_GPR_T4,
	LOONGARCH_GPR_T5,
	LOONGARCH_GPR_T6,
	LOONGARCH_GPR_T7,
	LOONGARCH_GPR_T8,
	LOONGARCH_GPR_FP = 22,
	LOONGARCH_GPR_S0 = 23,
	LOONGARCH_GPR_S1,
	LOONGARCH_GPR_S2,
	LOONGARCH_GPR_S3,
	LOONGARCH_GPR_S4,
	LOONGARCH_GPR_S5,
	LOONGARCH_GPR_S6,
	LOONGARCH_GPR_S7,
	LOONGARCH_GPR_S8,
	LOONGARCH_GPR_MAX
};

#define is_imm12_negative(val)	is_imm_negative(val, 12)

static inline bool is_imm_negative(unsigned long val, unsigned int bit)
{
	return val & (1UL << (bit - 1));
}

static inline bool is_branch_ins(union loongarch_instruction *ip)
{
	return ip->reg1i21_format.opcode >= beqz_op &&
		ip->reg1i21_format.opcode <= bgeu_op;
}

static inline bool is_ra_save_ins(union loongarch_instruction *ip)
{
	/* st.d $ra, $sp, offset */
	return ip->reg2i12_format.opcode == std_op &&
		ip->reg2i12_format.rj == LOONGARCH_GPR_SP &&
		ip->reg2i12_format.rd == LOONGARCH_GPR_RA &&
		!is_imm12_negative(ip->reg2i12_format.immediate);
}

static inline bool is_stack_alloc_ins(union loongarch_instruction *ip)
{
	/* addi.d $sp, $sp, -imm */
	return ip->reg2i12_format.opcode == addid_op &&
		ip->reg2i12_format.rj == LOONGARCH_GPR_SP &&
		ip->reg2i12_format.rd == LOONGARCH_GPR_SP &&
		is_imm12_negative(ip->reg2i12_format.immediate);
}

u32 larch_insn_gen_lu32id(enum loongarch_gpr rd, int imm);
u32 larch_insn_gen_lu52id(enum loongarch_gpr rd, enum loongarch_gpr rj, int imm);
u32 larch_insn_gen_jirl(enum loongarch_gpr rd, enum loongarch_gpr rj, unsigned long pc, unsigned long dest);

#endif /* _ASM_INST_H */
