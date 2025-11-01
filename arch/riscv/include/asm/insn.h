/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 SiFive
 */

#ifndef _ASM_RISCV_INSN_H
#define _ASM_RISCV_INSN_H

#include <linux/bits.h>

#define RV_INSN_FUNCT3_MASK	GENMASK(14, 12)
#define RV_INSN_FUNCT3_OPOFF	12
#define RV_INSN_OPCODE_MASK	GENMASK(6, 0)
#define RV_INSN_OPCODE_OPOFF	0
#define RV_INSN_FUNCT12_OPOFF	20

#define RV_ENCODE_FUNCT3(f_)	(RVG_FUNCT3_##f_ << RV_INSN_FUNCT3_OPOFF)
#define RV_ENCODE_FUNCT12(f_)	(RVG_FUNCT12_##f_ << RV_INSN_FUNCT12_OPOFF)

/* The bit field of immediate value in I-type instruction */
#define RV_I_IMM_SIGN_OPOFF	31
#define RV_I_IMM_11_0_OPOFF	20
#define RV_I_IMM_SIGN_OFF	12
#define RV_I_IMM_11_0_OFF	0
#define RV_I_IMM_11_0_MASK	GENMASK(11, 0)

/* The bit field of immediate value in J-type instruction */
#define RV_J_IMM_SIGN_OPOFF	31
#define RV_J_IMM_10_1_OPOFF	21
#define RV_J_IMM_11_OPOFF	20
#define RV_J_IMM_19_12_OPOFF	12
#define RV_J_IMM_SIGN_OFF	20
#define RV_J_IMM_10_1_OFF	1
#define RV_J_IMM_11_OFF		11
#define RV_J_IMM_19_12_OFF	12
#define RV_J_IMM_10_1_MASK	GENMASK(9, 0)
#define RV_J_IMM_11_MASK	GENMASK(0, 0)
#define RV_J_IMM_19_12_MASK	GENMASK(7, 0)

/*
 * U-type IMMs contain the upper 20bits [31:20] of an immediate with
 * the rest filled in by zeros, so no shifting required. Similarly,
 * bit31 contains the signed state, so no sign extension necessary.
 */
#define RV_U_IMM_SIGN_OPOFF	31
#define RV_U_IMM_31_12_OPOFF	0
#define RV_U_IMM_31_12_MASK	GENMASK(31, 12)

/* The bit field of immediate value in B-type instruction */
#define RV_B_IMM_SIGN_OPOFF	31
#define RV_B_IMM_10_5_OPOFF	25
#define RV_B_IMM_4_1_OPOFF	8
#define RV_B_IMM_11_OPOFF	7
#define RV_B_IMM_SIGN_OFF	12
#define RV_B_IMM_10_5_OFF	5
#define RV_B_IMM_4_1_OFF	1
#define RV_B_IMM_11_OFF		11
#define RV_B_IMM_10_5_MASK	GENMASK(5, 0)
#define RV_B_IMM_4_1_MASK	GENMASK(3, 0)
#define RV_B_IMM_11_MASK	GENMASK(0, 0)

/* The register offset in RVG instruction */
#define RVG_RS1_OPOFF		15
#define RVG_RS2_OPOFF		20
#define RVG_RD_OPOFF		7
#define RVG_RS1_MASK		GENMASK(4, 0)
#define RVG_RS2_MASK		GENMASK(4, 0)
#define RVG_RD_MASK		GENMASK(4, 0)

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

#define RVC_INSN_FUNCT4_MASK	GENMASK(15, 12)
#define RVC_INSN_FUNCT4_OPOFF	12
#define RVC_INSN_FUNCT3_MASK	GENMASK(15, 13)
#define RVC_INSN_FUNCT3_OPOFF	13
#define RVC_INSN_J_RS1_MASK	GENMASK(11, 7)
#define RVC_INSN_J_RS2_MASK	GENMASK(6, 2)
#define RVC_INSN_OPCODE_MASK	GENMASK(1, 0)
#define RVC_ENCODE_FUNCT3(f_)	(RVC_FUNCT3_##f_ << RVC_INSN_FUNCT3_OPOFF)
#define RVC_ENCODE_FUNCT4(f_)	(RVC_FUNCT4_##f_ << RVC_INSN_FUNCT4_OPOFF)

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
#define RVC_C2_RS1_MASK		GENMASK(4, 0)

/* parts of opcode for RVG*/
#define RVG_OPCODE_FENCE	0x0f
#define RVG_OPCODE_AUIPC	0x17
#define RVG_OPCODE_BRANCH	0x63
#define RVG_OPCODE_JALR		0x67
#define RVG_OPCODE_JAL		0x6f
#define RVG_OPCODE_SYSTEM	0x73
#define RVG_SYSTEM_CSR_OFF	20
#define RVG_SYSTEM_CSR_MASK	GENMASK(12, 0)

/* parts of opcode for RVF, RVD and RVQ */
#define RVFDQ_FL_FS_WIDTH_OFF	12
#define RVFDQ_FL_FS_WIDTH_MASK	GENMASK(2, 0)
#define RVFDQ_FL_FS_WIDTH_W	2
#define RVFDQ_FL_FS_WIDTH_D	3
#define RVFDQ_LS_FS_WIDTH_Q	4
#define RVFDQ_OPCODE_FL		0x07
#define RVFDQ_OPCODE_FS		0x27

/* parts of opcode for RVV */
#define RVV_OPCODE_VECTOR	0x57
#define RVV_VL_VS_WIDTH_8	0
#define RVV_VL_VS_WIDTH_16	5
#define RVV_VL_VS_WIDTH_32	6
#define RVV_VL_VS_WIDTH_64	7
#define RVV_OPCODE_VL		RVFDQ_OPCODE_FL
#define RVV_OPCODE_VS		RVFDQ_OPCODE_FS

/* parts of opcode for RVC*/
#define RVC_OPCODE_C0		0x0
#define RVC_OPCODE_C1		0x1
#define RVC_OPCODE_C2		0x2

/* parts of funct3 code for I, M, A extension*/
#define RVG_FUNCT3_JALR		0x0
#define RVG_FUNCT3_BEQ		0x0
#define RVG_FUNCT3_BNE		0x1
#define RVG_FUNCT3_BLT		0x4
#define RVG_FUNCT3_BGE		0x5
#define RVG_FUNCT3_BLTU		0x6
#define RVG_FUNCT3_BGEU		0x7

/* parts of funct3 code for C extension*/
#define RVC_FUNCT3_C_BEQZ	0x6
#define RVC_FUNCT3_C_BNEZ	0x7
#define RVC_FUNCT3_C_J		0x5
#define RVC_FUNCT3_C_JAL	0x1
#define RVC_FUNCT4_C_JR		0x8
#define RVC_FUNCT4_C_JALR	0x9
#define RVC_FUNCT4_C_EBREAK	0x9

#define RVG_FUNCT12_EBREAK	0x1
#define RVG_FUNCT12_SRET	0x102

#define RVG_MATCH_AUIPC		(RVG_OPCODE_AUIPC)
#define RVG_MATCH_JALR		(RV_ENCODE_FUNCT3(JALR) | RVG_OPCODE_JALR)
#define RVG_MATCH_JAL		(RVG_OPCODE_JAL)
#define RVG_MATCH_FENCE		(RVG_OPCODE_FENCE)
#define RVG_MATCH_BEQ		(RV_ENCODE_FUNCT3(BEQ) | RVG_OPCODE_BRANCH)
#define RVG_MATCH_BNE		(RV_ENCODE_FUNCT3(BNE) | RVG_OPCODE_BRANCH)
#define RVG_MATCH_BLT		(RV_ENCODE_FUNCT3(BLT) | RVG_OPCODE_BRANCH)
#define RVG_MATCH_BGE		(RV_ENCODE_FUNCT3(BGE) | RVG_OPCODE_BRANCH)
#define RVG_MATCH_BLTU		(RV_ENCODE_FUNCT3(BLTU) | RVG_OPCODE_BRANCH)
#define RVG_MATCH_BGEU		(RV_ENCODE_FUNCT3(BGEU) | RVG_OPCODE_BRANCH)
#define RVG_MATCH_EBREAK	(RV_ENCODE_FUNCT12(EBREAK) | RVG_OPCODE_SYSTEM)
#define RVG_MATCH_SRET		(RV_ENCODE_FUNCT12(SRET) | RVG_OPCODE_SYSTEM)
#define RVC_MATCH_C_BEQZ	(RVC_ENCODE_FUNCT3(C_BEQZ) | RVC_OPCODE_C1)
#define RVC_MATCH_C_BNEZ	(RVC_ENCODE_FUNCT3(C_BNEZ) | RVC_OPCODE_C1)
#define RVC_MATCH_C_J		(RVC_ENCODE_FUNCT3(C_J) | RVC_OPCODE_C1)
#define RVC_MATCH_C_JAL		(RVC_ENCODE_FUNCT3(C_JAL) | RVC_OPCODE_C1)
#define RVC_MATCH_C_JR		(RVC_ENCODE_FUNCT4(C_JR) | RVC_OPCODE_C2)
#define RVC_MATCH_C_JALR	(RVC_ENCODE_FUNCT4(C_JALR) | RVC_OPCODE_C2)
#define RVC_MATCH_C_EBREAK	(RVC_ENCODE_FUNCT4(C_EBREAK) | RVC_OPCODE_C2)

#define RVG_MASK_AUIPC		(RV_INSN_OPCODE_MASK)
#define RVG_MASK_JALR		(RV_INSN_FUNCT3_MASK | RV_INSN_OPCODE_MASK)
#define RVG_MASK_JAL		(RV_INSN_OPCODE_MASK)
#define RVG_MASK_FENCE		(RV_INSN_OPCODE_MASK)
#define RVC_MASK_C_JALR		(RVC_INSN_FUNCT4_MASK | RVC_INSN_J_RS2_MASK | RVC_INSN_OPCODE_MASK)
#define RVC_MASK_C_JR		(RVC_INSN_FUNCT4_MASK | RVC_INSN_J_RS2_MASK | RVC_INSN_OPCODE_MASK)
#define RVC_MASK_C_JAL		(RVC_INSN_FUNCT3_MASK | RVC_INSN_OPCODE_MASK)
#define RVC_MASK_C_J		(RVC_INSN_FUNCT3_MASK | RVC_INSN_OPCODE_MASK)
#define RVG_MASK_BEQ		(RV_INSN_FUNCT3_MASK | RV_INSN_OPCODE_MASK)
#define RVG_MASK_BNE		(RV_INSN_FUNCT3_MASK | RV_INSN_OPCODE_MASK)
#define RVG_MASK_BLT		(RV_INSN_FUNCT3_MASK | RV_INSN_OPCODE_MASK)
#define RVG_MASK_BGE		(RV_INSN_FUNCT3_MASK | RV_INSN_OPCODE_MASK)
#define RVG_MASK_BLTU		(RV_INSN_FUNCT3_MASK | RV_INSN_OPCODE_MASK)
#define RVG_MASK_BGEU		(RV_INSN_FUNCT3_MASK | RV_INSN_OPCODE_MASK)
#define RVC_MASK_C_BEQZ		(RVC_INSN_FUNCT3_MASK | RVC_INSN_OPCODE_MASK)
#define RVC_MASK_C_BNEZ		(RVC_INSN_FUNCT3_MASK | RVC_INSN_OPCODE_MASK)
#define RVC_MASK_C_EBREAK	0xffff
#define RVG_MASK_EBREAK		0xffffffff
#define RVG_MASK_SRET		0xffffffff

#define __INSN_LENGTH_MASK	_UL(0x3)
#define __INSN_LENGTH_GE_32	_UL(0x3)
#define __INSN_OPCODE_MASK	_UL(0x7F)
#define __INSN_BRANCH_OPCODE	_UL(RVG_OPCODE_BRANCH)

#define __RISCV_INSN_FUNCS(name, mask, val)				\
static __always_inline bool riscv_insn_is_##name(u32 code)		\
{									\
	BUILD_BUG_ON(~(mask) & (val));					\
	return (code & (mask)) == (val);				\
}									\

#if __riscv_xlen == 32
/* C.JAL is an RV32C-only instruction */
__RISCV_INSN_FUNCS(c_jal, RVC_MASK_C_JAL, RVC_MATCH_C_JAL)
#else
#define riscv_insn_is_c_jal(opcode) 0
#endif
__RISCV_INSN_FUNCS(auipc, RVG_MASK_AUIPC, RVG_MATCH_AUIPC)
__RISCV_INSN_FUNCS(jalr, RVG_MASK_JALR, RVG_MATCH_JALR)
__RISCV_INSN_FUNCS(jal, RVG_MASK_JAL, RVG_MATCH_JAL)
__RISCV_INSN_FUNCS(c_j, RVC_MASK_C_J, RVC_MATCH_C_J)
__RISCV_INSN_FUNCS(beq, RVG_MASK_BEQ, RVG_MATCH_BEQ)
__RISCV_INSN_FUNCS(bne, RVG_MASK_BNE, RVG_MATCH_BNE)
__RISCV_INSN_FUNCS(blt, RVG_MASK_BLT, RVG_MATCH_BLT)
__RISCV_INSN_FUNCS(bge, RVG_MASK_BGE, RVG_MATCH_BGE)
__RISCV_INSN_FUNCS(bltu, RVG_MASK_BLTU, RVG_MATCH_BLTU)
__RISCV_INSN_FUNCS(bgeu, RVG_MASK_BGEU, RVG_MATCH_BGEU)
__RISCV_INSN_FUNCS(c_beqz, RVC_MASK_C_BEQZ, RVC_MATCH_C_BEQZ)
__RISCV_INSN_FUNCS(c_bnez, RVC_MASK_C_BNEZ, RVC_MATCH_C_BNEZ)
__RISCV_INSN_FUNCS(c_ebreak, RVC_MASK_C_EBREAK, RVC_MATCH_C_EBREAK)
__RISCV_INSN_FUNCS(ebreak, RVG_MASK_EBREAK, RVG_MATCH_EBREAK)
__RISCV_INSN_FUNCS(sret, RVG_MASK_SRET, RVG_MATCH_SRET)
__RISCV_INSN_FUNCS(fence, RVG_MASK_FENCE, RVG_MATCH_FENCE);

/* special case to catch _any_ system instruction */
static __always_inline bool riscv_insn_is_system(u32 code)
{
	return (code & RV_INSN_OPCODE_MASK) == RVG_OPCODE_SYSTEM;
}

/* special case to catch _any_ branch instruction */
static __always_inline bool riscv_insn_is_branch(u32 code)
{
	return (code & RV_INSN_OPCODE_MASK) == RVG_OPCODE_BRANCH;
}

static __always_inline bool riscv_insn_is_c_jr(u32 code)
{
	return (code & RVC_MASK_C_JR) == RVC_MATCH_C_JR &&
	       (code & RVC_INSN_J_RS1_MASK) != 0;
}

static __always_inline bool riscv_insn_is_c_jalr(u32 code)
{
	return (code & RVC_MASK_C_JALR) == RVC_MATCH_C_JALR &&
	       (code & RVC_INSN_J_RS1_MASK) != 0;
}

#define INSN_MATCH_LB		0x3
#define INSN_MASK_LB		0x707f
#define INSN_MATCH_LH		0x1003
#define INSN_MASK_LH		0x707f
#define INSN_MATCH_LW		0x2003
#define INSN_MASK_LW		0x707f
#define INSN_MATCH_LD		0x3003
#define INSN_MASK_LD		0x707f
#define INSN_MATCH_LBU		0x4003
#define INSN_MASK_LBU		0x707f
#define INSN_MATCH_LHU		0x5003
#define INSN_MASK_LHU		0x707f
#define INSN_MATCH_LWU		0x6003
#define INSN_MASK_LWU		0x707f
#define INSN_MATCH_SB		0x23
#define INSN_MASK_SB		0x707f
#define INSN_MATCH_SH		0x1023
#define INSN_MASK_SH		0x707f
#define INSN_MATCH_SW		0x2023
#define INSN_MASK_SW		0x707f
#define INSN_MATCH_SD		0x3023
#define INSN_MASK_SD		0x707f

#define INSN_MATCH_C_LD		0x6000
#define INSN_MASK_C_LD		0xe003
#define INSN_MATCH_C_SD		0xe000
#define INSN_MASK_C_SD		0xe003
#define INSN_MATCH_C_LW		0x4000
#define INSN_MASK_C_LW		0xe003
#define INSN_MATCH_C_SW		0xc000
#define INSN_MASK_C_SW		0xe003
#define INSN_MATCH_C_LDSP	0x6002
#define INSN_MASK_C_LDSP	0xe003
#define INSN_MATCH_C_SDSP	0xe002
#define INSN_MASK_C_SDSP	0xe003
#define INSN_MATCH_C_LWSP	0x4002
#define INSN_MASK_C_LWSP	0xe003
#define INSN_MATCH_C_SWSP	0xc002
#define INSN_MASK_C_SWSP	0xe003

#define INSN_OPCODE_MASK	0x007c
#define INSN_OPCODE_SHIFT	2
#define INSN_OPCODE_SYSTEM	28

#define INSN_MASK_WFI		0xffffffff
#define INSN_MATCH_WFI		0x10500073

#define INSN_MASK_WRS		0xffffffff
#define INSN_MATCH_WRS		0x00d00073

#define INSN_MATCH_CSRRW	0x1073
#define INSN_MASK_CSRRW		0x707f
#define INSN_MATCH_CSRRS	0x2073
#define INSN_MASK_CSRRS		0x707f
#define INSN_MATCH_CSRRC	0x3073
#define INSN_MASK_CSRRC		0x707f
#define INSN_MATCH_CSRRWI	0x5073
#define INSN_MASK_CSRRWI	0x707f
#define INSN_MATCH_CSRRSI	0x6073
#define INSN_MASK_CSRRSI	0x707f
#define INSN_MATCH_CSRRCI	0x7073
#define INSN_MASK_CSRRCI	0x707f

#define INSN_MATCH_FLW		0x2007
#define INSN_MASK_FLW		0x707f
#define INSN_MATCH_FLD		0x3007
#define INSN_MASK_FLD		0x707f
#define INSN_MATCH_FLQ		0x4007
#define INSN_MASK_FLQ		0x707f
#define INSN_MATCH_FSW		0x2027
#define INSN_MASK_FSW		0x707f
#define INSN_MATCH_FSD		0x3027
#define INSN_MASK_FSD		0x707f
#define INSN_MATCH_FSQ		0x4027
#define INSN_MASK_FSQ		0x707f

#define INSN_MATCH_C_FLD	0x2000
#define INSN_MASK_C_FLD		0xe003
#define INSN_MATCH_C_FLW	0x6000
#define INSN_MASK_C_FLW		0xe003
#define INSN_MATCH_C_FSD	0xa000
#define INSN_MASK_C_FSD		0xe003
#define INSN_MATCH_C_FSW	0xe000
#define INSN_MASK_C_FSW		0xe003
#define INSN_MATCH_C_FLDSP	0x2002
#define INSN_MASK_C_FLDSP	0xe003
#define INSN_MATCH_C_FSDSP	0xa002
#define INSN_MASK_C_FSDSP	0xe003
#define INSN_MATCH_C_FLWSP	0x6002
#define INSN_MASK_C_FLWSP	0xe003
#define INSN_MATCH_C_FSWSP	0xe002
#define INSN_MASK_C_FSWSP	0xe003

#define INSN_MATCH_C_LHU		0x8400
#define INSN_MASK_C_LHU			0xfc43
#define INSN_MATCH_C_LH			0x8440
#define INSN_MASK_C_LH			0xfc43
#define INSN_MATCH_C_SH			0x8c00
#define INSN_MASK_C_SH			0xfc43

#define INSN_16BIT_MASK		0x3
#define INSN_IS_16BIT(insn)	(((insn) & INSN_16BIT_MASK) != INSN_16BIT_MASK)
#define INSN_LEN(insn)		(INSN_IS_16BIT(insn) ? 2 : 4)

#define SHIFT_RIGHT(x, y)		\
	((y) < 0 ? ((x) << -(y)) : ((x) >> (y)))

#define REG_MASK			\
	((1 << (5 + LOG_REGBYTES)) - (1 << LOG_REGBYTES))

#define REG_OFFSET(insn, pos)		\
	(SHIFT_RIGHT((insn), (pos) - LOG_REGBYTES) & REG_MASK)

#define REG_PTR(insn, pos, regs)	\
	((ulong *)((ulong)(regs) + REG_OFFSET(insn, pos)))

#define GET_RS1(insn, regs)	(*REG_PTR(insn, SH_RS1, regs))
#define GET_RS2(insn, regs)	(*REG_PTR(insn, SH_RS2, regs))
#define GET_RS1S(insn, regs)	(*REG_PTR(RVC_RS1S(insn), 0, regs))
#define GET_RS2S(insn, regs)	(*REG_PTR(RVC_RS2S(insn), 0, regs))
#define GET_RS2C(insn, regs)	(*REG_PTR(insn, SH_RS2C, regs))
#define GET_SP(regs)		(*REG_PTR(2, 0, regs))
#define SET_RD(insn, regs, val)	(*REG_PTR(insn, SH_RD, regs) = (val))
#define IMM_I(insn)		((s32)(insn) >> 20)
#define IMM_S(insn)		(((s32)(insn) >> 25 << 5) | \
				 (s32)(((insn) >> 7) & 0x1f))

#define SH_RD			7
#define SH_RS1			15
#define SH_RS2			20
#define SH_RS2C			2
#define MASK_RX			0x1f

#if defined(CONFIG_64BIT)
#define LOG_REGBYTES		3
#else
#define LOG_REGBYTES		2
#endif

#define MASK_FUNCT3		0x7000

#define GET_FUNCT3(insn)	(((insn) >> 12) & 7)

#define RV_IMM_SIGN(x)		(-(((x) >> 31) & 1))
#define RVC_IMM_SIGN(x)		(-(((x) >> 12) & 1))
#define RV_X_MASK(X, s, mask)	(((X) >> (s)) & (mask))
#define RV_X(X, s, n)		RV_X_MASK(X, s, ((1 << (n)) - 1))
#define RVC_LW_IMM(x)		((RV_X(x, 6, 1) << 2) | \
				 (RV_X(x, 10, 3) << 3) | \
				 (RV_X(x, 5, 1) << 6))
#define RVC_LD_IMM(x)		((RV_X(x, 10, 3) << 3) | \
				 (RV_X(x, 5, 2) << 6))
#define RVC_LWSP_IMM(x)		((RV_X(x, 4, 3) << 2) | \
				 (RV_X(x, 12, 1) << 5) | \
				 (RV_X(x, 2, 2) << 6))
#define RVC_LDSP_IMM(x)		((RV_X(x, 5, 2) << 3) | \
				 (RV_X(x, 12, 1) << 5) | \
				 (RV_X(x, 2, 3) << 6))
#define RVC_SWSP_IMM(x)		((RV_X(x, 9, 4) << 2) | \
				 (RV_X(x, 7, 2) << 6))
#define RVC_SDSP_IMM(x)		((RV_X(x, 10, 3) << 3) | \
				 (RV_X(x, 7, 3) << 6))
#define RVC_RS1S(insn)		(8 + RV_X(insn, SH_RD, 3))
#define RVC_RS2S(insn)		(8 + RV_X(insn, SH_RS2C, 3))
#define RVC_RS2(insn)		RV_X(insn, SH_RS2C, 5)
#define RVC_X(X, s, mask)	RV_X_MASK(X, s, mask)

#define RV_EXTRACT_FUNCT3(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RV_INSN_FUNCT3_OPOFF, \
		   RV_INSN_FUNCT3_MASK >> RV_INSN_FUNCT3_OPOFF)); })

#define RV_EXTRACT_RS1_REG(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RVG_RS1_OPOFF, RVG_RS1_MASK)); })

#define RV_EXTRACT_RS2_REG(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RVG_RS2_OPOFF, RVG_RS2_MASK)); })

#define RV_EXTRACT_RD_REG(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RVG_RD_OPOFF, RVG_RD_MASK)); })

#define RV_EXTRACT_UTYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RV_U_IMM_31_12_OPOFF, RV_U_IMM_31_12_MASK)); })

#define RV_EXTRACT_JTYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RV_J_IMM_10_1_OPOFF, RV_J_IMM_10_1_MASK) << RV_J_IMM_10_1_OFF) | \
	(RV_X_MASK(x_, RV_J_IMM_11_OPOFF, RV_J_IMM_11_MASK) << RV_J_IMM_11_OFF) | \
	(RV_X_MASK(x_, RV_J_IMM_19_12_OPOFF, RV_J_IMM_19_12_MASK) << RV_J_IMM_19_12_OFF) | \
	(RV_IMM_SIGN(x_) << RV_J_IMM_SIGN_OFF); })

#define RV_EXTRACT_ITYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RV_I_IMM_11_0_OPOFF, RV_I_IMM_11_0_MASK)) | \
	(RV_IMM_SIGN(x_) << RV_I_IMM_SIGN_OFF); })

#define RV_EXTRACT_BTYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RV_B_IMM_4_1_OPOFF, RV_B_IMM_4_1_MASK) << RV_B_IMM_4_1_OFF) | \
	(RV_X_MASK(x_, RV_B_IMM_10_5_OPOFF, RV_B_IMM_10_5_MASK) << RV_B_IMM_10_5_OFF) | \
	(RV_X_MASK(x_, RV_B_IMM_11_OPOFF, RV_B_IMM_11_MASK) << RV_B_IMM_11_OFF) | \
	(RV_IMM_SIGN(x_) << RV_B_IMM_SIGN_OFF); })

#define RVC_EXTRACT_C2_RS1_REG(x) \
	({typeof(x) x_ = (x); \
	(RV_X_MASK(x_, RVC_C2_RS1_OPOFF, RVC_C2_RS1_MASK)); })

#define RVC_EXTRACT_JTYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RVC_X(x_, RVC_J_IMM_3_1_OPOFF, RVC_J_IMM_3_1_MASK) << RVC_J_IMM_3_1_OFF) | \
	(RVC_X(x_, RVC_J_IMM_4_OPOFF, RVC_J_IMM_4_MASK) << RVC_J_IMM_4_OFF) | \
	(RVC_X(x_, RVC_J_IMM_5_OPOFF, RVC_J_IMM_5_MASK) << RVC_J_IMM_5_OFF) | \
	(RVC_X(x_, RVC_J_IMM_6_OPOFF, RVC_J_IMM_6_MASK) << RVC_J_IMM_6_OFF) | \
	(RVC_X(x_, RVC_J_IMM_7_OPOFF, RVC_J_IMM_7_MASK) << RVC_J_IMM_7_OFF) | \
	(RVC_X(x_, RVC_J_IMM_9_8_OPOFF, RVC_J_IMM_9_8_MASK) << RVC_J_IMM_9_8_OFF) | \
	(RVC_X(x_, RVC_J_IMM_10_OPOFF, RVC_J_IMM_10_MASK) << RVC_J_IMM_10_OFF) | \
	(RVC_IMM_SIGN(x_) << RVC_J_IMM_SIGN_OFF); })

#define RVC_EXTRACT_BTYPE_IMM(x) \
	({typeof(x) x_ = (x); \
	(RVC_X(x_, RVC_B_IMM_2_1_OPOFF, RVC_B_IMM_2_1_MASK) << RVC_B_IMM_2_1_OFF) | \
	(RVC_X(x_, RVC_B_IMM_4_3_OPOFF, RVC_B_IMM_4_3_MASK) << RVC_B_IMM_4_3_OFF) | \
	(RVC_X(x_, RVC_B_IMM_5_OPOFF, RVC_B_IMM_5_MASK) << RVC_B_IMM_5_OFF) | \
	(RVC_X(x_, RVC_B_IMM_7_6_OPOFF, RVC_B_IMM_7_6_MASK) << RVC_B_IMM_7_6_OFF) | \
	(RVC_IMM_SIGN(x_) << RVC_B_IMM_SIGN_OFF); })

#define RVG_EXTRACT_SYSTEM_CSR(x) \
	({typeof(x) x_ = (x); RV_X_MASK(x_, RVG_SYSTEM_CSR_OFF, RVG_SYSTEM_CSR_MASK); })

#define RVFDQ_EXTRACT_FL_FS_WIDTH(x) \
	({typeof(x) x_ = (x); RV_X_MASK(x_, RVFDQ_FL_FS_WIDTH_OFF, \
				   RVFDQ_FL_FS_WIDTH_MASK); })

#define RVV_EXTRACT_VL_VS_WIDTH(x) RVFDQ_EXTRACT_FL_FS_WIDTH(x)

/*
 * Get the immediate from a J-type instruction.
 *
 * @insn: instruction to process
 * Return: immediate
 */
static inline s32 riscv_insn_extract_jtype_imm(u32 insn)
{
	return RV_EXTRACT_JTYPE_IMM(insn);
}

/*
 * Update a J-type instruction with an immediate value.
 *
 * @insn: pointer to the jtype instruction
 * @imm: the immediate to insert into the instruction
 */
static inline void riscv_insn_insert_jtype_imm(u32 *insn, s32 imm)
{
	/* drop the old IMMs, all jal IMM bits sit at 31:12 */
	*insn &= ~GENMASK(31, 12);
	*insn |= (RV_X_MASK(imm, RV_J_IMM_10_1_OFF, RV_J_IMM_10_1_MASK) << RV_J_IMM_10_1_OPOFF) |
		 (RV_X_MASK(imm, RV_J_IMM_11_OFF, RV_J_IMM_11_MASK) << RV_J_IMM_11_OPOFF) |
		 (RV_X_MASK(imm, RV_J_IMM_19_12_OFF, RV_J_IMM_19_12_MASK) << RV_J_IMM_19_12_OPOFF) |
		 (RV_X_MASK(imm, RV_J_IMM_SIGN_OFF, 1) << RV_J_IMM_SIGN_OPOFF);
}

/*
 * Put together one immediate from a U-type and I-type instruction pair.
 *
 * The U-type contains an upper immediate, meaning bits[31:12] with [11:0]
 * being zero, while the I-type contains a 12bit immediate.
 * Combined these can encode larger 32bit values and are used for example
 * in auipc + jalr pairs to allow larger jumps.
 *
 * @utype_insn: instruction containing the upper immediate
 * @itype_insn: instruction
 * Return: combined immediate
 */
static inline s32 riscv_insn_extract_utype_itype_imm(u32 utype_insn, u32 itype_insn)
{
	s32 imm;

	imm = RV_EXTRACT_UTYPE_IMM(utype_insn);
	imm += RV_EXTRACT_ITYPE_IMM(itype_insn);

	return imm;
}

/*
 * Update a set of two instructions (U-type + I-type) with an immediate value.
 *
 * Used for example in auipc+jalrs pairs the U-type instructions contains
 * a 20bit upper immediate representing bits[31:12], while the I-type
 * instruction contains a 12bit immediate representing bits[11:0].
 *
 * This also takes into account that both separate immediates are
 * considered as signed values, so if the I-type immediate becomes
 * negative (BIT(11) set) the U-type part gets adjusted.
 *
 * @utype_insn: pointer to the utype instruction of the pair
 * @itype_insn: pointer to the itype instruction of the pair
 * @imm: the immediate to insert into the two instructions
 */
static inline void riscv_insn_insert_utype_itype_imm(u32 *utype_insn, u32 *itype_insn, s32 imm)
{
	/* drop possible old IMM values */
	*utype_insn &= ~(RV_U_IMM_31_12_MASK);
	*itype_insn &= ~(RV_I_IMM_11_0_MASK << RV_I_IMM_11_0_OPOFF);

	/* add the adapted IMMs */
	*utype_insn |= (imm & RV_U_IMM_31_12_MASK) + ((imm & BIT(11)) << 1);
	*itype_insn |= ((imm & RV_I_IMM_11_0_MASK) << RV_I_IMM_11_0_OPOFF);
}
#endif /* _ASM_RISCV_INSN_H */
