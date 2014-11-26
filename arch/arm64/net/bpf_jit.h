/*
 * BPF JIT compiler for ARM64
 *
 * Copyright (C) 2014 Zi Shen Lim <zlim.lnx@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _BPF_JIT_H
#define _BPF_JIT_H

#include <asm/insn.h>

/* 5-bit Register Operand */
#define A64_R(x)	AARCH64_INSN_REG_##x
#define A64_FP		AARCH64_INSN_REG_FP
#define A64_LR		AARCH64_INSN_REG_LR
#define A64_ZR		AARCH64_INSN_REG_ZR
#define A64_SP		AARCH64_INSN_REG_SP

#define A64_VARIANT(sf) \
	((sf) ? AARCH64_INSN_VARIANT_64BIT : AARCH64_INSN_VARIANT_32BIT)

/* Compare & branch (immediate) */
#define A64_COMP_BRANCH(sf, Rt, offset, type) \
	aarch64_insn_gen_comp_branch_imm(0, offset, Rt, A64_VARIANT(sf), \
		AARCH64_INSN_BRANCH_COMP_##type)
#define A64_CBZ(sf, Rt, imm19) A64_COMP_BRANCH(sf, Rt, (imm19) << 2, ZERO)

/* Conditional branch (immediate) */
#define A64_COND_BRANCH(cond, offset) \
	aarch64_insn_gen_cond_branch_imm(0, offset, cond)
#define A64_COND_EQ	AARCH64_INSN_COND_EQ /* == */
#define A64_COND_NE	AARCH64_INSN_COND_NE /* != */
#define A64_COND_CS	AARCH64_INSN_COND_CS /* unsigned >= */
#define A64_COND_HI	AARCH64_INSN_COND_HI /* unsigned > */
#define A64_COND_GE	AARCH64_INSN_COND_GE /* signed >= */
#define A64_COND_GT	AARCH64_INSN_COND_GT /* signed > */
#define A64_B_(cond, imm19) A64_COND_BRANCH(cond, (imm19) << 2)

/* Unconditional branch (immediate) */
#define A64_BRANCH(offset, type) aarch64_insn_gen_branch_imm(0, offset, \
	AARCH64_INSN_BRANCH_##type)
#define A64_B(imm26)  A64_BRANCH((imm26) << 2, NOLINK)
#define A64_BL(imm26) A64_BRANCH((imm26) << 2, LINK)

/* Unconditional branch (register) */
#define A64_BLR(Rn) aarch64_insn_gen_branch_reg(Rn, AARCH64_INSN_BRANCH_LINK)
#define A64_RET(Rn) aarch64_insn_gen_branch_reg(Rn, AARCH64_INSN_BRANCH_RETURN)

/* Load/store register (register offset) */
#define A64_LS_REG(Rt, Rn, Rm, size, type) \
	aarch64_insn_gen_load_store_reg(Rt, Rn, Rm, \
		AARCH64_INSN_SIZE_##size, \
		AARCH64_INSN_LDST_##type##_REG_OFFSET)
#define A64_STRB(Wt, Xn, Xm)  A64_LS_REG(Wt, Xn, Xm, 8, STORE)
#define A64_LDRB(Wt, Xn, Xm)  A64_LS_REG(Wt, Xn, Xm, 8, LOAD)
#define A64_STRH(Wt, Xn, Xm)  A64_LS_REG(Wt, Xn, Xm, 16, STORE)
#define A64_LDRH(Wt, Xn, Xm)  A64_LS_REG(Wt, Xn, Xm, 16, LOAD)
#define A64_STR32(Wt, Xn, Xm) A64_LS_REG(Wt, Xn, Xm, 32, STORE)
#define A64_LDR32(Wt, Xn, Xm) A64_LS_REG(Wt, Xn, Xm, 32, LOAD)
#define A64_STR64(Xt, Xn, Xm) A64_LS_REG(Xt, Xn, Xm, 64, STORE)
#define A64_LDR64(Xt, Xn, Xm) A64_LS_REG(Xt, Xn, Xm, 64, LOAD)

/* Load/store register pair */
#define A64_LS_PAIR(Rt, Rt2, Rn, offset, ls, type) \
	aarch64_insn_gen_load_store_pair(Rt, Rt2, Rn, offset, \
		AARCH64_INSN_VARIANT_64BIT, \
		AARCH64_INSN_LDST_##ls##_PAIR_##type)
/* Rn -= 16; Rn[0] = Rt; Rn[8] = Rt2; */
#define A64_PUSH(Rt, Rt2, Rn) A64_LS_PAIR(Rt, Rt2, Rn, -16, STORE, PRE_INDEX)
/* Rt = Rn[0]; Rt2 = Rn[8]; Rn += 16; */
#define A64_POP(Rt, Rt2, Rn)  A64_LS_PAIR(Rt, Rt2, Rn, 16, LOAD, POST_INDEX)

/* Add/subtract (immediate) */
#define A64_ADDSUB_IMM(sf, Rd, Rn, imm12, type) \
	aarch64_insn_gen_add_sub_imm(Rd, Rn, imm12, \
		A64_VARIANT(sf), AARCH64_INSN_ADSB_##type)
/* Rd = Rn OP imm12 */
#define A64_ADD_I(sf, Rd, Rn, imm12) A64_ADDSUB_IMM(sf, Rd, Rn, imm12, ADD)
#define A64_SUB_I(sf, Rd, Rn, imm12) A64_ADDSUB_IMM(sf, Rd, Rn, imm12, SUB)
/* Rd = Rn */
#define A64_MOV(sf, Rd, Rn) A64_ADD_I(sf, Rd, Rn, 0)

/* Bitfield move */
#define A64_BITFIELD(sf, Rd, Rn, immr, imms, type) \
	aarch64_insn_gen_bitfield(Rd, Rn, immr, imms, \
		A64_VARIANT(sf), AARCH64_INSN_BITFIELD_MOVE_##type)
/* Signed, with sign replication to left and zeros to right */
#define A64_SBFM(sf, Rd, Rn, ir, is) A64_BITFIELD(sf, Rd, Rn, ir, is, SIGNED)
/* Unsigned, with zeros to left and right */
#define A64_UBFM(sf, Rd, Rn, ir, is) A64_BITFIELD(sf, Rd, Rn, ir, is, UNSIGNED)

/* Rd = Rn << shift */
#define A64_LSL(sf, Rd, Rn, shift) ({	\
	int sz = (sf) ? 64 : 32;	\
	A64_UBFM(sf, Rd, Rn, (unsigned)-(shift) % sz, sz - 1 - (shift)); \
})
/* Rd = Rn >> shift */
#define A64_LSR(sf, Rd, Rn, shift) A64_UBFM(sf, Rd, Rn, shift, (sf) ? 63 : 31)
/* Rd = Rn >> shift; signed */
#define A64_ASR(sf, Rd, Rn, shift) A64_SBFM(sf, Rd, Rn, shift, (sf) ? 63 : 31)

/* Move wide (immediate) */
#define A64_MOVEW(sf, Rd, imm16, shift, type) \
	aarch64_insn_gen_movewide(Rd, imm16, shift, \
		A64_VARIANT(sf), AARCH64_INSN_MOVEWIDE_##type)
/* Rd = Zeros (for MOVZ);
 * Rd |= imm16 << shift (where shift is {0, 16, 32, 48});
 * Rd = ~Rd; (for MOVN); */
#define A64_MOVN(sf, Rd, imm16, shift) A64_MOVEW(sf, Rd, imm16, shift, INVERSE)
#define A64_MOVZ(sf, Rd, imm16, shift) A64_MOVEW(sf, Rd, imm16, shift, ZERO)
#define A64_MOVK(sf, Rd, imm16, shift) A64_MOVEW(sf, Rd, imm16, shift, KEEP)

/* Add/subtract (shifted register) */
#define A64_ADDSUB_SREG(sf, Rd, Rn, Rm, type) \
	aarch64_insn_gen_add_sub_shifted_reg(Rd, Rn, Rm, 0, \
		A64_VARIANT(sf), AARCH64_INSN_ADSB_##type)
/* Rd = Rn OP Rm */
#define A64_ADD(sf, Rd, Rn, Rm)  A64_ADDSUB_SREG(sf, Rd, Rn, Rm, ADD)
#define A64_SUB(sf, Rd, Rn, Rm)  A64_ADDSUB_SREG(sf, Rd, Rn, Rm, SUB)
#define A64_SUBS(sf, Rd, Rn, Rm) A64_ADDSUB_SREG(sf, Rd, Rn, Rm, SUB_SETFLAGS)
/* Rd = -Rm */
#define A64_NEG(sf, Rd, Rm) A64_SUB(sf, Rd, A64_ZR, Rm)
/* Rn - Rm; set condition flags */
#define A64_CMP(sf, Rn, Rm) A64_SUBS(sf, A64_ZR, Rn, Rm)

/* Data-processing (1 source) */
#define A64_DATA1(sf, Rd, Rn, type) aarch64_insn_gen_data1(Rd, Rn, \
	A64_VARIANT(sf), AARCH64_INSN_DATA1_##type)
/* Rd = BSWAPx(Rn) */
#define A64_REV16(sf, Rd, Rn) A64_DATA1(sf, Rd, Rn, REVERSE_16)
#define A64_REV32(sf, Rd, Rn) A64_DATA1(sf, Rd, Rn, REVERSE_32)
#define A64_REV64(Rd, Rn)     A64_DATA1(1, Rd, Rn, REVERSE_64)

/* Data-processing (2 source) */
/* Rd = Rn OP Rm */
#define A64_DATA2(sf, Rd, Rn, Rm, type) aarch64_insn_gen_data2(Rd, Rn, Rm, \
	A64_VARIANT(sf), AARCH64_INSN_DATA2_##type)
#define A64_UDIV(sf, Rd, Rn, Rm) A64_DATA2(sf, Rd, Rn, Rm, UDIV)
#define A64_LSLV(sf, Rd, Rn, Rm) A64_DATA2(sf, Rd, Rn, Rm, LSLV)
#define A64_LSRV(sf, Rd, Rn, Rm) A64_DATA2(sf, Rd, Rn, Rm, LSRV)
#define A64_ASRV(sf, Rd, Rn, Rm) A64_DATA2(sf, Rd, Rn, Rm, ASRV)

/* Data-processing (3 source) */
/* Rd = Ra + Rn * Rm */
#define A64_MADD(sf, Rd, Ra, Rn, Rm) aarch64_insn_gen_data3(Rd, Ra, Rn, Rm, \
	A64_VARIANT(sf), AARCH64_INSN_DATA3_MADD)
/* Rd = Rn * Rm */
#define A64_MUL(sf, Rd, Rn, Rm) A64_MADD(sf, Rd, A64_ZR, Rn, Rm)

/* Logical (shifted register) */
#define A64_LOGIC_SREG(sf, Rd, Rn, Rm, type) \
	aarch64_insn_gen_logical_shifted_reg(Rd, Rn, Rm, 0, \
		A64_VARIANT(sf), AARCH64_INSN_LOGIC_##type)
/* Rd = Rn OP Rm */
#define A64_AND(sf, Rd, Rn, Rm)  A64_LOGIC_SREG(sf, Rd, Rn, Rm, AND)
#define A64_ORR(sf, Rd, Rn, Rm)  A64_LOGIC_SREG(sf, Rd, Rn, Rm, ORR)
#define A64_EOR(sf, Rd, Rn, Rm)  A64_LOGIC_SREG(sf, Rd, Rn, Rm, EOR)
#define A64_ANDS(sf, Rd, Rn, Rm) A64_LOGIC_SREG(sf, Rd, Rn, Rm, AND_SETFLAGS)
/* Rn & Rm; set condition flags */
#define A64_TST(sf, Rn, Rm) A64_ANDS(sf, A64_ZR, Rn, Rm)

#endif /* _BPF_JIT_H */
