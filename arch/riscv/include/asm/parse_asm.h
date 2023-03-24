/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 SiFive
 */

#ifndef _ASM_RISCV_INSN_H
#define _ASM_RISCV_INSN_H

#include <linux/bits.h>

/* The bit field of immediate value in I-type instruction */
#define I_IMM_SIGN_OPOFF	31
#define I_IMM_11_0_OPOFF	20
#define I_IMM_SIGN_OFF		12
#define I_IMM_11_0_OFF		0
#define I_IMM_11_0_MASK		GENMASK(11, 0)

/* The bit field of immediate value in J-type instruction */
#define J_IMM_SIGN_OPOFF	31
#define J_IMM_10_1_OPOFF	21
#define J_IMM_11_OPOFF		20
#define J_IMM_19_12_OPOFF	12
#define J_IMM_SIGN_OFF		20
#define J_IMM_10_1_OFF		1
#define J_IMM_11_OFF		11
#define J_IMM_19_12_OFF		12
#define J_IMM_10_1_MASK		GENMASK(9, 0)
#define J_IMM_11_MASK		GENMASK(0, 0)
#define J_IMM_19_12_MASK	GENMASK(7, 0)

/* The bit field of immediate value in B-type instruction */
#define B_IMM_SIGN_OPOFF	31
#define B_IMM_10_5_OPOFF	25
#define B_IMM_4_1_OPOFF		8
#define B_IMM_11_OPOFF		7
#define B_IMM_SIGN_OFF		12
#define B_IMM_10_5_OFF		5
#define B_IMM_4_1_OFF		1
#define B_IMM_11_OFF		11
#define B_IMM_10_5_MASK		GENMASK(5, 0)
#define B_IMM_4_1_MASK		GENMASK(3, 0)
#define B_IMM_11_MASK		GENMASK(0, 0)

/* The register offset in RVG instruction */
#define RVG_RS1_OPOFF		15
#define RVG_RS2_OPOFF		20
#define RVG_RD_OPOFF		7

/* The bit field of immediate value in RVC J instruction */
#define RVC_J_IMM_SIGN_OPOFF	12
#define RVC_J_IMM_4_OPOFF	11
#define RVC_J_IMM_9_8_OPOFF	9
#define RVC_J_IMM_10_OPOFF	8
#define RVC_J_IMM_6_OPOFF	7
#define RVC_J_IMM_7_OPOFF	6
#define RVC_J_IMM_3_1_OPOFF	3
#define RVC_J_IMM_5_OPOFF	2
#define RVC_J_IMM_SIGN_OFF	11
#define RVC_J_IMM_4_OFF		4
#define RVC_J_IMM_9_8_OFF	8
#define RVC_J_IMM_10_OFF	10
#define RVC_J_IMM_6_OFF		6
#define RVC_J_IMM_7_OFF		7
#define RVC_J_IMM_3_1_OFF	1
#define RVC_J_IMM_5_OFF		5
#define RVC_J_IMM_4_MASK	GENMASK(0, 0)
#define RVC_J_IMM_9_8_MASK	GENMASK(1, 0)
#define RVC_J_IMM_10_MASK	GENMASK(0, 0)
#define RVC_J_IMM_6_MASK	GENMASK(0, 0)
#define RVC_J_IMM_7_MASK	GENMASK(0, 0)
#define RVC_J_IMM_3_1_MASK	GENMASK(2, 0)
#define RVC_J_IMM_5_MASK	GENMASK(0, 0)

/* The bit field of immediate value in RVC B instruction */
#define RVC_B_IMM_SIGN_OPOFF	12
#define RVC_B_IMM_4_3_OPOFF	10
#define RVC_B_IMM_7_6_OPOFF	5
#define RVC_B_IMM_2_1_OPOFF	3
#define RVC_B_IMM_5_OPOFF	2
#define RVC_B_IMM_SIGN_OFF	8
#define RVC_B_IMM_4_3_OFF	3
#define RVC_B_IMM_7_6_OFF	6
#define RVC_B_IMM_2_1_OFF	1
#define RVC_B_IMM_5_OFF		5
#define RVC_B_IMM_4_3_MASK	GENMASK(1, 0)
#define RVC_B_IMM_7_6_MASK	GENMASK(1, 0)
#define RVC_B_IMM_2_1_MASK	GENMASK(1, 0)
#define RVC_B_IMM_5_MASK	GENMASK(0, 0)

/* The register offset in RVC op=C0 instruction */
#define RVC_C0_RS1_OPOFF	7
#define RVC_C0_RS2_OPOFF	2
#define RVC_C0_RD_OPOFF		2

/* The register offset in RVC op=C1 instruction */
#define RVC_C1_RS1_OPOFF	7
#define RVC_C1_RS2_OPOFF	2
#define RVC_C1_RD_OPOFF		7

/* The register offset in RVC op=C2 instruction */
#define RVC_C2_RS1_OPOFF	7
#define RVC_C2_RS2_OPOFF	2
#define RVC_C2_RD_OPOFF		7

/* parts of opcode for RVG*/
#define OPCODE_BRANCH		0x63
#define OPCODE_JALR		0x67
#define OPCODE_JAL		0x6f
#define OPCODE_SYSTEM		0x73

/* parts of opcode for RVC*/
#define OPCODE_C_0		0x0
#define OPCODE_C_1		0x1
#define OPCODE_C_2		0x2

/* parts of funct3 code for I, M, A extension*/
#define FUNCT3_JALR		0x0
#define FUNCT3_BEQ		0x0
#define FUNCT3_BNE		0x1000
#define FUNCT3_BLT		0x4000
#define FUNCT3_BGE		0x5000
#define FUNCT3_BLTU		0x6000
#define FUNCT3_BGEU		0x7000

/* parts of funct3 code for C extension*/
#define FUNCT3_C_BEQZ		0xc000
#define FUNCT3_C_BNEZ		0xe000
#define FUNCT3_C_J		0xa000
#define FUNCT3_C_JAL		0x2000
#define FUNCT4_C_JR		0x8000
#define FUNCT4_C_JALR		0xf000

#define FUNCT12_SRET		0x10200000

#define MATCH_JALR		(FUNCT3_JALR | OPCODE_JALR)
#define MATCH_JAL		(OPCODE_JAL)
#define MATCH_BEQ		(FUNCT3_BEQ | OPCODE_BRANCH)
#define MATCH_BNE		(FUNCT3_BNE | OPCODE_BRANCH)
#define MATCH_BLT		(FUNCT3_BLT | OPCODE_BRANCH)
#define MATCH_BGE		(FUNCT3_BGE | OPCODE_BRANCH)
#define MATCH_BLTU		(FUNCT3_BLTU | OPCODE_BRANCH)
#define MATCH_BGEU		(FUNCT3_BGEU | OPCODE_BRANCH)
#define MATCH_SRET		(FUNCT12_SRET | OPCODE_SYSTEM)
#define MATCH_C_BEQZ		(FUNCT3_C_BEQZ | OPCODE_C_1)
#define MATCH_C_BNEZ		(FUNCT3_C_BNEZ | OPCODE_C_1)
#define MATCH_C_J		(FUNCT3_C_J | OPCODE_C_1)
#define MATCH_C_JAL		(FUNCT3_C_JAL | OPCODE_C_1)
#define MATCH_C_JR		(FUNCT4_C_JR | OPCODE_C_2)
#define MATCH_C_JALR		(FUNCT4_C_JALR | OPCODE_C_2)

#define MASK_JALR		0x707f
#define MASK_JAL		0x7f
#define MASK_C_JALR		0xf07f
#define MASK_C_JR		0xf07f
#define MASK_C_JAL		0xe003
#define MASK_C_J		0xe003
#define MASK_BEQ		0x707f
#define MASK_BNE		0x707f
#define MASK_BLT		0x707f
#define MASK_BGE		0x707f
#define MASK_BLTU		0x707f
#define MASK_BGEU		0x707f
#define MASK_C_BEQZ		0xe003
#define MASK_C_BNEZ		0xe003
#define MASK_SRET		0xffffffff

#define __INSN_LENGTH_MASK	_UL(0x3)
#define __INSN_LENGTH_GE_32	_UL(0x3)
#define __INSN_OPCODE_MASK	_UL(0x7F)
#define __INSN_BRANCH_OPCODE	_UL(OPCODE_BRANCH)

/* Define a series of is_XXX_insn functions to check if the value INSN
 * is an instance of instruction XXX.
 */
#define DECLARE_INSN(INSN_NAME, INSN_MATCH, INSN_MASK) \
static inline bool is_ ## INSN_NAME ## _insn(long insn) \
{ \
	return (insn & (INSN_MASK)) == (INSN_MATCH); \
}

#define RV_IMM_SIGN(x) (-(((x) >> 31) & 1))
#define RVC_IMM_SIGN(x) (-(((x) >> 12) & 1))
#define RV_X(X, s, mask)  (((X) >> (s)) & (mask))
#define RVC_X(X, s, mask) RV_X(X, s, mask)

#define EXTRACT_JTYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RV_X(x_, J_IMM_10_1_OPOFF, J_IMM_10_1_MASK) << J_IMM_10_1_OFF) | \
	(RV_X(x_, J_IMM_11_OPOFF, J_IMM_11_MASK) << J_IMM_11_OFF) | \
	(RV_X(x_, J_IMM_19_12_OPOFF, J_IMM_19_12_MASK) << J_IMM_19_12_OFF) | \
	(RV_IMM_SIGN(x_) << J_IMM_SIGN_OFF); })

#define EXTRACT_ITYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RV_X(x_, I_IMM_11_0_OPOFF, I_IMM_11_0_MASK)) | \
	(RV_IMM_SIGN(x_) << I_IMM_SIGN_OFF); })

#define EXTRACT_BTYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RV_X(x_, B_IMM_4_1_OPOFF, B_IMM_4_1_MASK) << B_IMM_4_1_OFF) | \
	(RV_X(x_, B_IMM_10_5_OPOFF, B_IMM_10_5_MASK) << B_IMM_10_5_OFF) | \
	(RV_X(x_, B_IMM_11_OPOFF, B_IMM_11_MASK) << B_IMM_11_OFF) | \
	(RV_IMM_SIGN(x_) << B_IMM_SIGN_OFF); })

#define EXTRACT_RVC_J_IMM(x) \
	({typeof(x) x_ = (x); \
	(RVC_X(x_, RVC_J_IMM_3_1_OPOFF, RVC_J_IMM_3_1_MASK) << RVC_J_IMM_3_1_OFF) | \
	(RVC_X(x_, RVC_J_IMM_4_OPOFF, RVC_J_IMM_4_MASK) << RVC_J_IMM_4_OFF) | \
	(RVC_X(x_, RVC_J_IMM_5_OPOFF, RVC_J_IMM_5_MASK) << RVC_J_IMM_5_OFF) | \
	(RVC_X(x_, RVC_J_IMM_6_OPOFF, RVC_J_IMM_6_MASK) << RVC_J_IMM_6_OFF) | \
	(RVC_X(x_, RVC_J_IMM_7_OPOFF, RVC_J_IMM_7_MASK) << RVC_J_IMM_7_OFF) | \
	(RVC_X(x_, RVC_J_IMM_9_8_OPOFF, RVC_J_IMM_9_8_MASK) << RVC_J_IMM_9_8_OFF) | \
	(RVC_X(x_, RVC_J_IMM_10_OPOFF, RVC_J_IMM_10_MASK) << RVC_J_IMM_10_OFF) | \
	(RVC_IMM_SIGN(x_) << RVC_J_IMM_SIGN_OFF); })

#define EXTRACT_RVC_B_IMM(x) \
	({typeof(x) x_ = (x); \
	(RVC_X(x_, RVC_B_IMM_2_1_OPOFF, RVC_B_IMM_2_1_MASK) << RVC_B_IMM_2_1_OFF) | \
	(RVC_X(x_, RVC_B_IMM_4_3_OPOFF, RVC_B_IMM_4_3_MASK) << RVC_B_IMM_4_3_OFF) | \
	(RVC_X(x_, RVC_B_IMM_5_OPOFF, RVC_B_IMM_5_MASK) << RVC_B_IMM_5_OFF) | \
	(RVC_X(x_, RVC_B_IMM_7_6_OPOFF, RVC_B_IMM_7_6_MASK) << RVC_B_IMM_7_6_OFF) | \
	(RVC_IMM_SIGN(x_) << RVC_B_IMM_SIGN_OFF); })

#endif /* _ASM_RISCV_INSN_H */
