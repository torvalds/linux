/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * Copyright (C) 2014 Zi Shen Lim <zlim.lnx@gmail.com>
 */
#ifndef	__ASM_INSN_H
#define	__ASM_INSN_H
#include <linux/build_bug.h>
#include <linux/types.h>

#include <asm/insn-def.h>

#ifndef __ASSEMBLY__
/*
 * ARM Architecture Reference Manual for ARMv8 Profile-A, Issue A.a
 * Section C3.1 "A64 instruction index by encoding":
 * AArch64 main encoding table
 *  Bit position
 *   28 27 26 25	Encoding Group
 *   0  0  -  -		Unallocated
 *   1  0  0  -		Data processing, immediate
 *   1  0  1  -		Branch, exception generation and system instructions
 *   -  1  -  0		Loads and stores
 *   -  1  0  1		Data processing - register
 *   0  1  1  1		Data processing - SIMD and floating point
 *   1  1  1  1		Data processing - SIMD and floating point
 * "-" means "don't care"
 */
enum aarch64_insn_encoding_class {
	AARCH64_INSN_CLS_UNKNOWN,	/* UNALLOCATED */
	AARCH64_INSN_CLS_SVE,		/* SVE instructions */
	AARCH64_INSN_CLS_DP_IMM,	/* Data processing - immediate */
	AARCH64_INSN_CLS_DP_REG,	/* Data processing - register */
	AARCH64_INSN_CLS_DP_FPSIMD,	/* Data processing - SIMD and FP */
	AARCH64_INSN_CLS_LDST,		/* Loads and stores */
	AARCH64_INSN_CLS_BR_SYS,	/* Branch, exception generation and
					 * system instructions */
};

enum aarch64_insn_hint_cr_op {
	AARCH64_INSN_HINT_NOP	= 0x0 << 5,
	AARCH64_INSN_HINT_YIELD	= 0x1 << 5,
	AARCH64_INSN_HINT_WFE	= 0x2 << 5,
	AARCH64_INSN_HINT_WFI	= 0x3 << 5,
	AARCH64_INSN_HINT_SEV	= 0x4 << 5,
	AARCH64_INSN_HINT_SEVL	= 0x5 << 5,

	AARCH64_INSN_HINT_XPACLRI    = 0x07 << 5,
	AARCH64_INSN_HINT_PACIA_1716 = 0x08 << 5,
	AARCH64_INSN_HINT_PACIB_1716 = 0x0A << 5,
	AARCH64_INSN_HINT_AUTIA_1716 = 0x0C << 5,
	AARCH64_INSN_HINT_AUTIB_1716 = 0x0E << 5,
	AARCH64_INSN_HINT_PACIAZ     = 0x18 << 5,
	AARCH64_INSN_HINT_PACIASP    = 0x19 << 5,
	AARCH64_INSN_HINT_PACIBZ     = 0x1A << 5,
	AARCH64_INSN_HINT_PACIBSP    = 0x1B << 5,
	AARCH64_INSN_HINT_AUTIAZ     = 0x1C << 5,
	AARCH64_INSN_HINT_AUTIASP    = 0x1D << 5,
	AARCH64_INSN_HINT_AUTIBZ     = 0x1E << 5,
	AARCH64_INSN_HINT_AUTIBSP    = 0x1F << 5,

	AARCH64_INSN_HINT_ESB  = 0x10 << 5,
	AARCH64_INSN_HINT_PSB  = 0x11 << 5,
	AARCH64_INSN_HINT_TSB  = 0x12 << 5,
	AARCH64_INSN_HINT_CSDB = 0x14 << 5,
	AARCH64_INSN_HINT_CLEARBHB = 0x16 << 5,

	AARCH64_INSN_HINT_BTI   = 0x20 << 5,
	AARCH64_INSN_HINT_BTIC  = 0x22 << 5,
	AARCH64_INSN_HINT_BTIJ  = 0x24 << 5,
	AARCH64_INSN_HINT_BTIJC = 0x26 << 5,
};

enum aarch64_insn_imm_type {
	AARCH64_INSN_IMM_ADR,
	AARCH64_INSN_IMM_26,
	AARCH64_INSN_IMM_19,
	AARCH64_INSN_IMM_16,
	AARCH64_INSN_IMM_14,
	AARCH64_INSN_IMM_12,
	AARCH64_INSN_IMM_9,
	AARCH64_INSN_IMM_7,
	AARCH64_INSN_IMM_6,
	AARCH64_INSN_IMM_S,
	AARCH64_INSN_IMM_R,
	AARCH64_INSN_IMM_N,
	AARCH64_INSN_IMM_MAX
};

enum aarch64_insn_register_type {
	AARCH64_INSN_REGTYPE_RT,
	AARCH64_INSN_REGTYPE_RN,
	AARCH64_INSN_REGTYPE_RT2,
	AARCH64_INSN_REGTYPE_RM,
	AARCH64_INSN_REGTYPE_RD,
	AARCH64_INSN_REGTYPE_RA,
	AARCH64_INSN_REGTYPE_RS,
};

enum aarch64_insn_register {
	AARCH64_INSN_REG_0  = 0,
	AARCH64_INSN_REG_1  = 1,
	AARCH64_INSN_REG_2  = 2,
	AARCH64_INSN_REG_3  = 3,
	AARCH64_INSN_REG_4  = 4,
	AARCH64_INSN_REG_5  = 5,
	AARCH64_INSN_REG_6  = 6,
	AARCH64_INSN_REG_7  = 7,
	AARCH64_INSN_REG_8  = 8,
	AARCH64_INSN_REG_9  = 9,
	AARCH64_INSN_REG_10 = 10,
	AARCH64_INSN_REG_11 = 11,
	AARCH64_INSN_REG_12 = 12,
	AARCH64_INSN_REG_13 = 13,
	AARCH64_INSN_REG_14 = 14,
	AARCH64_INSN_REG_15 = 15,
	AARCH64_INSN_REG_16 = 16,
	AARCH64_INSN_REG_17 = 17,
	AARCH64_INSN_REG_18 = 18,
	AARCH64_INSN_REG_19 = 19,
	AARCH64_INSN_REG_20 = 20,
	AARCH64_INSN_REG_21 = 21,
	AARCH64_INSN_REG_22 = 22,
	AARCH64_INSN_REG_23 = 23,
	AARCH64_INSN_REG_24 = 24,
	AARCH64_INSN_REG_25 = 25,
	AARCH64_INSN_REG_26 = 26,
	AARCH64_INSN_REG_27 = 27,
	AARCH64_INSN_REG_28 = 28,
	AARCH64_INSN_REG_29 = 29,
	AARCH64_INSN_REG_FP = 29, /* Frame pointer */
	AARCH64_INSN_REG_30 = 30,
	AARCH64_INSN_REG_LR = 30, /* Link register */
	AARCH64_INSN_REG_ZR = 31, /* Zero: as source register */
	AARCH64_INSN_REG_SP = 31  /* Stack pointer: as load/store base reg */
};

enum aarch64_insn_special_register {
	AARCH64_INSN_SPCLREG_SPSR_EL1	= 0xC200,
	AARCH64_INSN_SPCLREG_ELR_EL1	= 0xC201,
	AARCH64_INSN_SPCLREG_SP_EL0	= 0xC208,
	AARCH64_INSN_SPCLREG_SPSEL	= 0xC210,
	AARCH64_INSN_SPCLREG_CURRENTEL	= 0xC212,
	AARCH64_INSN_SPCLREG_DAIF	= 0xDA11,
	AARCH64_INSN_SPCLREG_NZCV	= 0xDA10,
	AARCH64_INSN_SPCLREG_FPCR	= 0xDA20,
	AARCH64_INSN_SPCLREG_DSPSR_EL0	= 0xDA28,
	AARCH64_INSN_SPCLREG_DLR_EL0	= 0xDA29,
	AARCH64_INSN_SPCLREG_SPSR_EL2	= 0xE200,
	AARCH64_INSN_SPCLREG_ELR_EL2	= 0xE201,
	AARCH64_INSN_SPCLREG_SP_EL1	= 0xE208,
	AARCH64_INSN_SPCLREG_SPSR_INQ	= 0xE218,
	AARCH64_INSN_SPCLREG_SPSR_ABT	= 0xE219,
	AARCH64_INSN_SPCLREG_SPSR_UND	= 0xE21A,
	AARCH64_INSN_SPCLREG_SPSR_FIQ	= 0xE21B,
	AARCH64_INSN_SPCLREG_SPSR_EL3	= 0xF200,
	AARCH64_INSN_SPCLREG_ELR_EL3	= 0xF201,
	AARCH64_INSN_SPCLREG_SP_EL2	= 0xF210
};

enum aarch64_insn_variant {
	AARCH64_INSN_VARIANT_32BIT,
	AARCH64_INSN_VARIANT_64BIT
};

enum aarch64_insn_condition {
	AARCH64_INSN_COND_EQ = 0x0, /* == */
	AARCH64_INSN_COND_NE = 0x1, /* != */
	AARCH64_INSN_COND_CS = 0x2, /* unsigned >= */
	AARCH64_INSN_COND_CC = 0x3, /* unsigned < */
	AARCH64_INSN_COND_MI = 0x4, /* < 0 */
	AARCH64_INSN_COND_PL = 0x5, /* >= 0 */
	AARCH64_INSN_COND_VS = 0x6, /* overflow */
	AARCH64_INSN_COND_VC = 0x7, /* no overflow */
	AARCH64_INSN_COND_HI = 0x8, /* unsigned > */
	AARCH64_INSN_COND_LS = 0x9, /* unsigned <= */
	AARCH64_INSN_COND_GE = 0xa, /* signed >= */
	AARCH64_INSN_COND_LT = 0xb, /* signed < */
	AARCH64_INSN_COND_GT = 0xc, /* signed > */
	AARCH64_INSN_COND_LE = 0xd, /* signed <= */
	AARCH64_INSN_COND_AL = 0xe, /* always */
};

enum aarch64_insn_branch_type {
	AARCH64_INSN_BRANCH_NOLINK,
	AARCH64_INSN_BRANCH_LINK,
	AARCH64_INSN_BRANCH_RETURN,
	AARCH64_INSN_BRANCH_COMP_ZERO,
	AARCH64_INSN_BRANCH_COMP_NONZERO,
};

enum aarch64_insn_size_type {
	AARCH64_INSN_SIZE_8,
	AARCH64_INSN_SIZE_16,
	AARCH64_INSN_SIZE_32,
	AARCH64_INSN_SIZE_64,
};

enum aarch64_insn_ldst_type {
	AARCH64_INSN_LDST_LOAD_REG_OFFSET,
	AARCH64_INSN_LDST_STORE_REG_OFFSET,
	AARCH64_INSN_LDST_LOAD_IMM_OFFSET,
	AARCH64_INSN_LDST_STORE_IMM_OFFSET,
	AARCH64_INSN_LDST_LOAD_PAIR_PRE_INDEX,
	AARCH64_INSN_LDST_STORE_PAIR_PRE_INDEX,
	AARCH64_INSN_LDST_LOAD_PAIR_POST_INDEX,
	AARCH64_INSN_LDST_STORE_PAIR_POST_INDEX,
	AARCH64_INSN_LDST_LOAD_EX,
	AARCH64_INSN_LDST_LOAD_ACQ_EX,
	AARCH64_INSN_LDST_STORE_EX,
	AARCH64_INSN_LDST_STORE_REL_EX,
};

enum aarch64_insn_adsb_type {
	AARCH64_INSN_ADSB_ADD,
	AARCH64_INSN_ADSB_SUB,
	AARCH64_INSN_ADSB_ADD_SETFLAGS,
	AARCH64_INSN_ADSB_SUB_SETFLAGS
};

enum aarch64_insn_movewide_type {
	AARCH64_INSN_MOVEWIDE_ZERO,
	AARCH64_INSN_MOVEWIDE_KEEP,
	AARCH64_INSN_MOVEWIDE_INVERSE
};

enum aarch64_insn_bitfield_type {
	AARCH64_INSN_BITFIELD_MOVE,
	AARCH64_INSN_BITFIELD_MOVE_UNSIGNED,
	AARCH64_INSN_BITFIELD_MOVE_SIGNED
};

enum aarch64_insn_data1_type {
	AARCH64_INSN_DATA1_REVERSE_16,
	AARCH64_INSN_DATA1_REVERSE_32,
	AARCH64_INSN_DATA1_REVERSE_64,
};

enum aarch64_insn_data2_type {
	AARCH64_INSN_DATA2_UDIV,
	AARCH64_INSN_DATA2_SDIV,
	AARCH64_INSN_DATA2_LSLV,
	AARCH64_INSN_DATA2_LSRV,
	AARCH64_INSN_DATA2_ASRV,
	AARCH64_INSN_DATA2_RORV,
};

enum aarch64_insn_data3_type {
	AARCH64_INSN_DATA3_MADD,
	AARCH64_INSN_DATA3_MSUB,
};

enum aarch64_insn_logic_type {
	AARCH64_INSN_LOGIC_AND,
	AARCH64_INSN_LOGIC_BIC,
	AARCH64_INSN_LOGIC_ORR,
	AARCH64_INSN_LOGIC_ORN,
	AARCH64_INSN_LOGIC_EOR,
	AARCH64_INSN_LOGIC_EON,
	AARCH64_INSN_LOGIC_AND_SETFLAGS,
	AARCH64_INSN_LOGIC_BIC_SETFLAGS
};

enum aarch64_insn_prfm_type {
	AARCH64_INSN_PRFM_TYPE_PLD,
	AARCH64_INSN_PRFM_TYPE_PLI,
	AARCH64_INSN_PRFM_TYPE_PST,
};

enum aarch64_insn_prfm_target {
	AARCH64_INSN_PRFM_TARGET_L1,
	AARCH64_INSN_PRFM_TARGET_L2,
	AARCH64_INSN_PRFM_TARGET_L3,
};

enum aarch64_insn_prfm_policy {
	AARCH64_INSN_PRFM_POLICY_KEEP,
	AARCH64_INSN_PRFM_POLICY_STRM,
};

enum aarch64_insn_adr_type {
	AARCH64_INSN_ADR_TYPE_ADRP,
	AARCH64_INSN_ADR_TYPE_ADR,
};

enum aarch64_insn_mem_atomic_op {
	AARCH64_INSN_MEM_ATOMIC_ADD,
	AARCH64_INSN_MEM_ATOMIC_CLR,
	AARCH64_INSN_MEM_ATOMIC_EOR,
	AARCH64_INSN_MEM_ATOMIC_SET,
	AARCH64_INSN_MEM_ATOMIC_SWP,
};

enum aarch64_insn_mem_order_type {
	AARCH64_INSN_MEM_ORDER_NONE,
	AARCH64_INSN_MEM_ORDER_ACQ,
	AARCH64_INSN_MEM_ORDER_REL,
	AARCH64_INSN_MEM_ORDER_ACQREL,
};

enum aarch64_insn_mb_type {
	AARCH64_INSN_MB_SY,
	AARCH64_INSN_MB_ST,
	AARCH64_INSN_MB_LD,
	AARCH64_INSN_MB_ISH,
	AARCH64_INSN_MB_ISHST,
	AARCH64_INSN_MB_ISHLD,
	AARCH64_INSN_MB_NSH,
	AARCH64_INSN_MB_NSHST,
	AARCH64_INSN_MB_NSHLD,
	AARCH64_INSN_MB_OSH,
	AARCH64_INSN_MB_OSHST,
	AARCH64_INSN_MB_OSHLD,
};

#define	__AARCH64_INSN_FUNCS(abbr, mask, val)				\
static __always_inline bool aarch64_insn_is_##abbr(u32 code)		\
{									\
	BUILD_BUG_ON(~(mask) & (val));					\
	return (code & (mask)) == (val);				\
}									\
static __always_inline u32 aarch64_insn_get_##abbr##_value(void)	\
{									\
	return (val);							\
}

__AARCH64_INSN_FUNCS(adr,	0x9F000000, 0x10000000)
__AARCH64_INSN_FUNCS(adrp,	0x9F000000, 0x90000000)
__AARCH64_INSN_FUNCS(prfm,	0x3FC00000, 0x39800000)
__AARCH64_INSN_FUNCS(prfm_lit,	0xFF000000, 0xD8000000)
__AARCH64_INSN_FUNCS(store_imm,	0x3FC00000, 0x39000000)
__AARCH64_INSN_FUNCS(load_imm,	0x3FC00000, 0x39400000)
__AARCH64_INSN_FUNCS(store_pre,	0x3FE00C00, 0x38000C00)
__AARCH64_INSN_FUNCS(load_pre,	0x3FE00C00, 0x38400C00)
__AARCH64_INSN_FUNCS(store_post,	0x3FE00C00, 0x38000400)
__AARCH64_INSN_FUNCS(load_post,	0x3FE00C00, 0x38400400)
__AARCH64_INSN_FUNCS(str_reg,	0x3FE0EC00, 0x38206800)
__AARCH64_INSN_FUNCS(str_imm,	0x3FC00000, 0x39000000)
__AARCH64_INSN_FUNCS(ldadd,	0x3F20FC00, 0x38200000)
__AARCH64_INSN_FUNCS(ldclr,	0x3F20FC00, 0x38201000)
__AARCH64_INSN_FUNCS(ldeor,	0x3F20FC00, 0x38202000)
__AARCH64_INSN_FUNCS(ldset,	0x3F20FC00, 0x38203000)
__AARCH64_INSN_FUNCS(swp,	0x3F20FC00, 0x38208000)
__AARCH64_INSN_FUNCS(cas,	0x3FA07C00, 0x08A07C00)
__AARCH64_INSN_FUNCS(ldr_reg,	0x3FE0EC00, 0x38606800)
__AARCH64_INSN_FUNCS(ldr_imm,	0x3FC00000, 0x39400000)
__AARCH64_INSN_FUNCS(ldr_lit,	0xBF000000, 0x18000000)
__AARCH64_INSN_FUNCS(ldrsw_lit,	0xFF000000, 0x98000000)
__AARCH64_INSN_FUNCS(exclusive,	0x3F800000, 0x08000000)
__AARCH64_INSN_FUNCS(load_ex,	0x3F400000, 0x08400000)
__AARCH64_INSN_FUNCS(store_ex,	0x3F400000, 0x08000000)
__AARCH64_INSN_FUNCS(stp,	0x7FC00000, 0x29000000)
__AARCH64_INSN_FUNCS(ldp,	0x7FC00000, 0x29400000)
__AARCH64_INSN_FUNCS(stp_post,	0x7FC00000, 0x28800000)
__AARCH64_INSN_FUNCS(ldp_post,	0x7FC00000, 0x28C00000)
__AARCH64_INSN_FUNCS(stp_pre,	0x7FC00000, 0x29800000)
__AARCH64_INSN_FUNCS(ldp_pre,	0x7FC00000, 0x29C00000)
__AARCH64_INSN_FUNCS(add_imm,	0x7F000000, 0x11000000)
__AARCH64_INSN_FUNCS(adds_imm,	0x7F000000, 0x31000000)
__AARCH64_INSN_FUNCS(sub_imm,	0x7F000000, 0x51000000)
__AARCH64_INSN_FUNCS(subs_imm,	0x7F000000, 0x71000000)
__AARCH64_INSN_FUNCS(movn,	0x7F800000, 0x12800000)
__AARCH64_INSN_FUNCS(sbfm,	0x7F800000, 0x13000000)
__AARCH64_INSN_FUNCS(bfm,	0x7F800000, 0x33000000)
__AARCH64_INSN_FUNCS(movz,	0x7F800000, 0x52800000)
__AARCH64_INSN_FUNCS(ubfm,	0x7F800000, 0x53000000)
__AARCH64_INSN_FUNCS(movk,	0x7F800000, 0x72800000)
__AARCH64_INSN_FUNCS(add,	0x7F200000, 0x0B000000)
__AARCH64_INSN_FUNCS(adds,	0x7F200000, 0x2B000000)
__AARCH64_INSN_FUNCS(sub,	0x7F200000, 0x4B000000)
__AARCH64_INSN_FUNCS(subs,	0x7F200000, 0x6B000000)
__AARCH64_INSN_FUNCS(madd,	0x7FE08000, 0x1B000000)
__AARCH64_INSN_FUNCS(msub,	0x7FE08000, 0x1B008000)
__AARCH64_INSN_FUNCS(udiv,	0x7FE0FC00, 0x1AC00800)
__AARCH64_INSN_FUNCS(sdiv,	0x7FE0FC00, 0x1AC00C00)
__AARCH64_INSN_FUNCS(lslv,	0x7FE0FC00, 0x1AC02000)
__AARCH64_INSN_FUNCS(lsrv,	0x7FE0FC00, 0x1AC02400)
__AARCH64_INSN_FUNCS(asrv,	0x7FE0FC00, 0x1AC02800)
__AARCH64_INSN_FUNCS(rorv,	0x7FE0FC00, 0x1AC02C00)
__AARCH64_INSN_FUNCS(rev16,	0x7FFFFC00, 0x5AC00400)
__AARCH64_INSN_FUNCS(rev32,	0x7FFFFC00, 0x5AC00800)
__AARCH64_INSN_FUNCS(rev64,	0x7FFFFC00, 0x5AC00C00)
__AARCH64_INSN_FUNCS(and,	0x7F200000, 0x0A000000)
__AARCH64_INSN_FUNCS(bic,	0x7F200000, 0x0A200000)
__AARCH64_INSN_FUNCS(orr,	0x7F200000, 0x2A000000)
__AARCH64_INSN_FUNCS(mov_reg,	0x7FE0FFE0, 0x2A0003E0)
__AARCH64_INSN_FUNCS(orn,	0x7F200000, 0x2A200000)
__AARCH64_INSN_FUNCS(eor,	0x7F200000, 0x4A000000)
__AARCH64_INSN_FUNCS(eon,	0x7F200000, 0x4A200000)
__AARCH64_INSN_FUNCS(ands,	0x7F200000, 0x6A000000)
__AARCH64_INSN_FUNCS(bics,	0x7F200000, 0x6A200000)
__AARCH64_INSN_FUNCS(and_imm,	0x7F800000, 0x12000000)
__AARCH64_INSN_FUNCS(orr_imm,	0x7F800000, 0x32000000)
__AARCH64_INSN_FUNCS(eor_imm,	0x7F800000, 0x52000000)
__AARCH64_INSN_FUNCS(ands_imm,	0x7F800000, 0x72000000)
__AARCH64_INSN_FUNCS(extr,	0x7FA00000, 0x13800000)
__AARCH64_INSN_FUNCS(b,		0xFC000000, 0x14000000)
__AARCH64_INSN_FUNCS(bl,	0xFC000000, 0x94000000)
__AARCH64_INSN_FUNCS(cbz,	0x7F000000, 0x34000000)
__AARCH64_INSN_FUNCS(cbnz,	0x7F000000, 0x35000000)
__AARCH64_INSN_FUNCS(tbz,	0x7F000000, 0x36000000)
__AARCH64_INSN_FUNCS(tbnz,	0x7F000000, 0x37000000)
__AARCH64_INSN_FUNCS(bcond,	0xFF000010, 0x54000000)
__AARCH64_INSN_FUNCS(svc,	0xFFE0001F, 0xD4000001)
__AARCH64_INSN_FUNCS(hvc,	0xFFE0001F, 0xD4000002)
__AARCH64_INSN_FUNCS(smc,	0xFFE0001F, 0xD4000003)
__AARCH64_INSN_FUNCS(brk,	0xFFE0001F, 0xD4200000)
__AARCH64_INSN_FUNCS(exception,	0xFF000000, 0xD4000000)
__AARCH64_INSN_FUNCS(hint,	0xFFFFF01F, 0xD503201F)
__AARCH64_INSN_FUNCS(br,	0xFFFFFC1F, 0xD61F0000)
__AARCH64_INSN_FUNCS(br_auth,	0xFEFFF800, 0xD61F0800)
__AARCH64_INSN_FUNCS(blr,	0xFFFFFC1F, 0xD63F0000)
__AARCH64_INSN_FUNCS(blr_auth,	0xFEFFF800, 0xD63F0800)
__AARCH64_INSN_FUNCS(ret,	0xFFFFFC1F, 0xD65F0000)
__AARCH64_INSN_FUNCS(ret_auth,	0xFFFFFBFF, 0xD65F0BFF)
__AARCH64_INSN_FUNCS(eret,	0xFFFFFFFF, 0xD69F03E0)
__AARCH64_INSN_FUNCS(eret_auth,	0xFFFFFBFF, 0xD69F0BFF)
__AARCH64_INSN_FUNCS(mrs,	0xFFF00000, 0xD5300000)
__AARCH64_INSN_FUNCS(msr_imm,	0xFFF8F01F, 0xD500401F)
__AARCH64_INSN_FUNCS(msr_reg,	0xFFF00000, 0xD5100000)
__AARCH64_INSN_FUNCS(dmb,	0xFFFFF0FF, 0xD50330BF)
__AARCH64_INSN_FUNCS(dsb_base,	0xFFFFF0FF, 0xD503309F)
__AARCH64_INSN_FUNCS(dsb_nxs,	0xFFFFF3FF, 0xD503323F)
__AARCH64_INSN_FUNCS(isb,	0xFFFFF0FF, 0xD50330DF)
__AARCH64_INSN_FUNCS(sb,	0xFFFFFFFF, 0xD50330FF)
__AARCH64_INSN_FUNCS(clrex,	0xFFFFF0FF, 0xD503305F)
__AARCH64_INSN_FUNCS(ssbb,	0xFFFFFFFF, 0xD503309F)
__AARCH64_INSN_FUNCS(pssbb,	0xFFFFFFFF, 0xD503349F)

#undef	__AARCH64_INSN_FUNCS

bool aarch64_insn_is_steppable_hint(u32 insn);
bool aarch64_insn_is_branch_imm(u32 insn);

static inline bool aarch64_insn_is_adr_adrp(u32 insn)
{
	return aarch64_insn_is_adr(insn) || aarch64_insn_is_adrp(insn);
}

static inline bool aarch64_insn_is_dsb(u32 insn)
{
	return aarch64_insn_is_dsb_base(insn) || aarch64_insn_is_dsb_nxs(insn);
}

static inline bool aarch64_insn_is_barrier(u32 insn)
{
	return aarch64_insn_is_dmb(insn) || aarch64_insn_is_dsb(insn) ||
	       aarch64_insn_is_isb(insn) || aarch64_insn_is_sb(insn) ||
	       aarch64_insn_is_clrex(insn) || aarch64_insn_is_ssbb(insn) ||
	       aarch64_insn_is_pssbb(insn);
}

static inline bool aarch64_insn_is_store_single(u32 insn)
{
	return aarch64_insn_is_store_imm(insn) ||
	       aarch64_insn_is_store_pre(insn) ||
	       aarch64_insn_is_store_post(insn);
}

static inline bool aarch64_insn_is_store_pair(u32 insn)
{
	return aarch64_insn_is_stp(insn) ||
	       aarch64_insn_is_stp_pre(insn) ||
	       aarch64_insn_is_stp_post(insn);
}

static inline bool aarch64_insn_is_load_single(u32 insn)
{
	return aarch64_insn_is_load_imm(insn) ||
	       aarch64_insn_is_load_pre(insn) ||
	       aarch64_insn_is_load_post(insn);
}

static inline bool aarch64_insn_is_load_pair(u32 insn)
{
	return aarch64_insn_is_ldp(insn) ||
	       aarch64_insn_is_ldp_pre(insn) ||
	       aarch64_insn_is_ldp_post(insn);
}

enum aarch64_insn_encoding_class aarch64_get_insn_class(u32 insn);
bool aarch64_insn_uses_literal(u32 insn);
bool aarch64_insn_is_branch(u32 insn);
u64 aarch64_insn_decode_immediate(enum aarch64_insn_imm_type type, u32 insn);
u32 aarch64_insn_encode_immediate(enum aarch64_insn_imm_type type,
				  u32 insn, u64 imm);
u32 aarch64_insn_decode_register(enum aarch64_insn_register_type type,
					 u32 insn);
u32 aarch64_insn_gen_branch_imm(unsigned long pc, unsigned long addr,
				enum aarch64_insn_branch_type type);
u32 aarch64_insn_gen_comp_branch_imm(unsigned long pc, unsigned long addr,
				     enum aarch64_insn_register reg,
				     enum aarch64_insn_variant variant,
				     enum aarch64_insn_branch_type type);
u32 aarch64_insn_gen_cond_branch_imm(unsigned long pc, unsigned long addr,
				     enum aarch64_insn_condition cond);
u32 aarch64_insn_gen_hint(enum aarch64_insn_hint_cr_op op);
u32 aarch64_insn_gen_nop(void);
u32 aarch64_insn_gen_branch_reg(enum aarch64_insn_register reg,
				enum aarch64_insn_branch_type type);
u32 aarch64_insn_gen_load_store_reg(enum aarch64_insn_register reg,
				    enum aarch64_insn_register base,
				    enum aarch64_insn_register offset,
				    enum aarch64_insn_size_type size,
				    enum aarch64_insn_ldst_type type);
u32 aarch64_insn_gen_load_store_imm(enum aarch64_insn_register reg,
				    enum aarch64_insn_register base,
				    unsigned int imm,
				    enum aarch64_insn_size_type size,
				    enum aarch64_insn_ldst_type type);
u32 aarch64_insn_gen_load_literal(unsigned long pc, unsigned long addr,
				  enum aarch64_insn_register reg,
				  bool is64bit);
u32 aarch64_insn_gen_load_store_pair(enum aarch64_insn_register reg1,
				     enum aarch64_insn_register reg2,
				     enum aarch64_insn_register base,
				     int offset,
				     enum aarch64_insn_variant variant,
				     enum aarch64_insn_ldst_type type);
u32 aarch64_insn_gen_load_store_ex(enum aarch64_insn_register reg,
				   enum aarch64_insn_register base,
				   enum aarch64_insn_register state,
				   enum aarch64_insn_size_type size,
				   enum aarch64_insn_ldst_type type);
u32 aarch64_insn_gen_add_sub_imm(enum aarch64_insn_register dst,
				 enum aarch64_insn_register src,
				 int imm, enum aarch64_insn_variant variant,
				 enum aarch64_insn_adsb_type type);
u32 aarch64_insn_gen_adr(unsigned long pc, unsigned long addr,
			 enum aarch64_insn_register reg,
			 enum aarch64_insn_adr_type type);
u32 aarch64_insn_gen_bitfield(enum aarch64_insn_register dst,
			      enum aarch64_insn_register src,
			      int immr, int imms,
			      enum aarch64_insn_variant variant,
			      enum aarch64_insn_bitfield_type type);
u32 aarch64_insn_gen_movewide(enum aarch64_insn_register dst,
			      int imm, int shift,
			      enum aarch64_insn_variant variant,
			      enum aarch64_insn_movewide_type type);
u32 aarch64_insn_gen_add_sub_shifted_reg(enum aarch64_insn_register dst,
					 enum aarch64_insn_register src,
					 enum aarch64_insn_register reg,
					 int shift,
					 enum aarch64_insn_variant variant,
					 enum aarch64_insn_adsb_type type);
u32 aarch64_insn_gen_data1(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data1_type type);
u32 aarch64_insn_gen_data2(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_register reg,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data2_type type);
u32 aarch64_insn_gen_data3(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_register reg1,
			   enum aarch64_insn_register reg2,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data3_type type);
u32 aarch64_insn_gen_logical_shifted_reg(enum aarch64_insn_register dst,
					 enum aarch64_insn_register src,
					 enum aarch64_insn_register reg,
					 int shift,
					 enum aarch64_insn_variant variant,
					 enum aarch64_insn_logic_type type);
u32 aarch64_insn_gen_move_reg(enum aarch64_insn_register dst,
			      enum aarch64_insn_register src,
			      enum aarch64_insn_variant variant);
u32 aarch64_insn_gen_logical_immediate(enum aarch64_insn_logic_type type,
				       enum aarch64_insn_variant variant,
				       enum aarch64_insn_register Rn,
				       enum aarch64_insn_register Rd,
				       u64 imm);
u32 aarch64_insn_gen_extr(enum aarch64_insn_variant variant,
			  enum aarch64_insn_register Rm,
			  enum aarch64_insn_register Rn,
			  enum aarch64_insn_register Rd,
			  u8 lsb);
u32 aarch64_insn_gen_prefetch(enum aarch64_insn_register base,
			      enum aarch64_insn_prfm_type type,
			      enum aarch64_insn_prfm_target target,
			      enum aarch64_insn_prfm_policy policy);
#ifdef CONFIG_ARM64_LSE_ATOMICS
u32 aarch64_insn_gen_atomic_ld_op(enum aarch64_insn_register result,
				  enum aarch64_insn_register address,
				  enum aarch64_insn_register value,
				  enum aarch64_insn_size_type size,
				  enum aarch64_insn_mem_atomic_op op,
				  enum aarch64_insn_mem_order_type order);
u32 aarch64_insn_gen_cas(enum aarch64_insn_register result,
			 enum aarch64_insn_register address,
			 enum aarch64_insn_register value,
			 enum aarch64_insn_size_type size,
			 enum aarch64_insn_mem_order_type order);
#else
static inline
u32 aarch64_insn_gen_atomic_ld_op(enum aarch64_insn_register result,
				  enum aarch64_insn_register address,
				  enum aarch64_insn_register value,
				  enum aarch64_insn_size_type size,
				  enum aarch64_insn_mem_atomic_op op,
				  enum aarch64_insn_mem_order_type order)
{
	return AARCH64_BREAK_FAULT;
}

static inline
u32 aarch64_insn_gen_cas(enum aarch64_insn_register result,
			 enum aarch64_insn_register address,
			 enum aarch64_insn_register value,
			 enum aarch64_insn_size_type size,
			 enum aarch64_insn_mem_order_type order)
{
	return AARCH64_BREAK_FAULT;
}
#endif
u32 aarch64_insn_gen_dmb(enum aarch64_insn_mb_type type);

s32 aarch64_get_branch_offset(u32 insn);
u32 aarch64_set_branch_offset(u32 insn, s32 offset);

s32 aarch64_insn_adrp_get_offset(u32 insn);
u32 aarch64_insn_adrp_set_offset(u32 insn, s32 offset);

bool aarch32_insn_is_wide(u32 insn);

#define A32_RN_OFFSET	16
#define A32_RT_OFFSET	12
#define A32_RT2_OFFSET	 0

u32 aarch64_insn_extract_system_reg(u32 insn);
u32 aarch32_insn_extract_reg_num(u32 insn, int offset);
u32 aarch32_insn_mcr_extract_opc2(u32 insn);
u32 aarch32_insn_mcr_extract_crm(u32 insn);

typedef bool (pstate_check_t)(unsigned long);
extern pstate_check_t * const aarch32_opcode_cond_checks[16];

#endif /* __ASSEMBLY__ */

#endif	/* __ASM_INSN_H */
