/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * several functions that help interpret ARC instructions
 * used for unaligned accesses, kprobes and kgdb
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ARC_DISASM_H__
#define __ARC_DISASM_H__

enum {
	op_Bcc = 0, op_BLcc = 1, op_LD = 2, op_ST = 3, op_MAJOR_4 = 4,
	op_MAJOR_5 = 5, op_LD_ADD = 12, op_ADD_SUB_SHIFT = 13,
	op_ADD_MOV_CMP = 14, op_S = 15, op_LD_S = 16, op_LDB_S = 17,
	op_LDW_S = 18, op_LDWX_S = 19, op_ST_S = 20, op_STB_S = 21,
	op_STW_S = 22, op_Su5 = 23, op_SP = 24, op_GP = 25,
	op_Pcl = 26, op_MOV_S = 27, op_ADD_CMP = 28, op_BR_S = 29,
	op_B_S = 30, op_BL_S = 31
};

enum flow {
	noflow,
	direct_jump,
	direct_call,
	indirect_jump,
	indirect_call,
	invalid_instr
};

#define IS_BIT(word, n)		((word) & (1<<n))
#define BITS(word, s, e)	(((word) >> (s)) & (~((-2) << ((e) - (s)))))

#define MAJOR_OPCODE(word)	(BITS((word), 27, 31))
#define MINOR_OPCODE(word)	(BITS((word), 16, 21))
#define FIELD_A(word)		(BITS((word), 0, 5))
#define FIELD_B(word)		((BITS((word), 12, 14)<<3) | \
				(BITS((word), 24, 26)))
#define FIELD_C(word)		(BITS((word), 6, 11))
#define FIELD_u6(word)		FIELDC(word)
#define FIELD_s12(word)		sign_extend(((BITS((word), 0, 5) << 6) | \
					BITS((word), 6, 11)), 12)

/* note that for BL/BRcc these two macro's need another AND statement to mask
 * out bit 1 (make the result a multiple of 4) */
#define FIELD_s9(word)		sign_extend(((BITS(word, 15, 15) << 8) | \
					BITS(word, 16, 23)), 9)
#define FIELD_s21(word)		sign_extend(((BITS(word, 6, 15) << 11) | \
					(BITS(word, 17, 26) << 1)), 12)
#define FIELD_s25(word)		sign_extend(((BITS(word, 0, 3) << 21) | \
					(BITS(word, 6, 15) << 11) | \
					(BITS(word, 17, 26) << 1)), 12)

/* note: these operate on 16 bits! */
#define FIELD_S_A(word)		((BITS((word), 2, 2)<<3) | BITS((word), 0, 2))
#define FIELD_S_B(word)		((BITS((word), 10, 10)<<3) | \
				BITS((word), 8, 10))
#define FIELD_S_C(word)		((BITS((word), 7, 7)<<3) | BITS((word), 5, 7))
#define FIELD_S_H(word)		((BITS((word), 0, 2)<<3) | BITS((word), 5, 8))
#define FIELD_S_u5(word)	(BITS((word), 0, 4))
#define FIELD_S_u6(word)	(BITS((word), 0, 4) << 1)
#define FIELD_S_u7(word)	(BITS((word), 0, 4) << 2)
#define FIELD_S_u10(word)	(BITS((word), 0, 7) << 2)
#define FIELD_S_s7(word)	sign_extend(BITS((word), 0, 5) << 1, 9)
#define FIELD_S_s8(word)	sign_extend(BITS((word), 0, 7) << 1, 9)
#define FIELD_S_s9(word)	sign_extend(BITS((word), 0, 8), 9)
#define FIELD_S_s10(word)	sign_extend(BITS((word), 0, 8) << 1, 10)
#define FIELD_S_s11(word)	sign_extend(BITS((word), 0, 8) << 2, 11)
#define FIELD_S_s13(word)	sign_extend(BITS((word), 0, 10) << 2, 13)

#define STATUS32_L		0x00000100
#define REG_LIMM		62

struct disasm_state {
	/* generic info */
	unsigned long words[2];
	int instr_len;
	int major_opcode;
	/* info for branch/jump */
	int is_branch;
	int target;
	int delay_slot;
	enum flow flow;
	/* info for load/store */
	int src1, src2, src3, dest, wb_reg;
	int zz, aa, x, pref, di;
	int fault, write;
};

static inline int sign_extend(int value, int bits)
{
	if (IS_BIT(value, (bits - 1)))
		value |= (0xffffffff << bits);

	return value;
}

static inline int is_short_instr(unsigned long addr)
{
	uint16_t word = *((uint16_t *)addr);
	int opcode = (word >> 11) & 0x1F;
	return (opcode >= 0x0B);
}

void disasm_instr(unsigned long addr, struct disasm_state *state,
	int userspace, struct pt_regs *regs, struct callee_regs *cregs);
int disasm_next_pc(unsigned long pc, struct pt_regs *regs, struct callee_regs
	*cregs, unsigned long *fall_thru, unsigned long *target);
long get_reg(int reg, struct pt_regs *regs, struct callee_regs *cregs);
void set_reg(int reg, long val, struct pt_regs *regs,
		struct callee_regs *cregs);

#endif	/* __ARC_DISASM_H__ */
