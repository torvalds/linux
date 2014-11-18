/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
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
#ifndef	__ASM_INSN_H
#define	__ASM_INSN_H
#include <linux/types.h>

/* A64 instructions are always 32 bits. */
#define	AARCH64_INSN_SIZE		4

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
	AARCH64_INSN_CLS_DP_IMM,	/* Data processing - immediate */
	AARCH64_INSN_CLS_DP_REG,	/* Data processing - register */
	AARCH64_INSN_CLS_DP_FPSIMD,	/* Data processing - SIMD and FP */
	AARCH64_INSN_CLS_LDST,		/* Loads and stores */
	AARCH64_INSN_CLS_BR_SYS,	/* Branch, exception generation and
					 * system instructions */
};

enum aarch64_insn_hint_op {
	AARCH64_INSN_HINT_NOP	= 0x0 << 5,
	AARCH64_INSN_HINT_YIELD	= 0x1 << 5,
	AARCH64_INSN_HINT_WFE	= 0x2 << 5,
	AARCH64_INSN_HINT_WFI	= 0x3 << 5,
	AARCH64_INSN_HINT_SEV	= 0x4 << 5,
	AARCH64_INSN_HINT_SEVL	= 0x5 << 5,
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
	AARCH64_INSN_IMM_MAX
};

enum aarch64_insn_register_type {
	AARCH64_INSN_REGTYPE_RT,
	AARCH64_INSN_REGTYPE_RN,
	AARCH64_INSN_REGTYPE_RT2,
	AARCH64_INSN_REGTYPE_RM,
	AARCH64_INSN_REGTYPE_RD,
	AARCH64_INSN_REGTYPE_RA,
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
	AARCH64_INSN_LDST_LOAD_PAIR_PRE_INDEX,
	AARCH64_INSN_LDST_STORE_PAIR_PRE_INDEX,
	AARCH64_INSN_LDST_LOAD_PAIR_POST_INDEX,
	AARCH64_INSN_LDST_STORE_PAIR_POST_INDEX,
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

#define	__AARCH64_INSN_FUNCS(abbr, mask, val)	\
static __always_inline bool aarch64_insn_is_##abbr(u32 code) \
{ return (code & (mask)) == (val); } \
static __always_inline u32 aarch64_insn_get_##abbr##_value(void) \
{ return (val); }

__AARCH64_INSN_FUNCS(str_reg,	0x3FE0EC00, 0x38206800)
__AARCH64_INSN_FUNCS(ldr_reg,	0x3FE0EC00, 0x38606800)
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
__AARCH64_INSN_FUNCS(orn,	0x7F200000, 0x2A200000)
__AARCH64_INSN_FUNCS(eor,	0x7F200000, 0x4A000000)
__AARCH64_INSN_FUNCS(eon,	0x7F200000, 0x4A200000)
__AARCH64_INSN_FUNCS(ands,	0x7F200000, 0x6A000000)
__AARCH64_INSN_FUNCS(bics,	0x7F200000, 0x6A200000)
__AARCH64_INSN_FUNCS(b,		0xFC000000, 0x14000000)
__AARCH64_INSN_FUNCS(bl,	0xFC000000, 0x94000000)
__AARCH64_INSN_FUNCS(cbz,	0xFE000000, 0x34000000)
__AARCH64_INSN_FUNCS(cbnz,	0xFE000000, 0x35000000)
__AARCH64_INSN_FUNCS(bcond,	0xFF000010, 0x54000000)
__AARCH64_INSN_FUNCS(svc,	0xFFE0001F, 0xD4000001)
__AARCH64_INSN_FUNCS(hvc,	0xFFE0001F, 0xD4000002)
__AARCH64_INSN_FUNCS(smc,	0xFFE0001F, 0xD4000003)
__AARCH64_INSN_FUNCS(brk,	0xFFE0001F, 0xD4200000)
__AARCH64_INSN_FUNCS(hint,	0xFFFFF01F, 0xD503201F)
__AARCH64_INSN_FUNCS(br,	0xFFFFFC1F, 0xD61F0000)
__AARCH64_INSN_FUNCS(blr,	0xFFFFFC1F, 0xD63F0000)
__AARCH64_INSN_FUNCS(ret,	0xFFFFFC1F, 0xD65F0000)

#undef	__AARCH64_INSN_FUNCS

bool aarch64_insn_is_nop(u32 insn);

int aarch64_insn_read(void *addr, u32 *insnp);
int aarch64_insn_write(void *addr, u32 insn);
enum aarch64_insn_encoding_class aarch64_get_insn_class(u32 insn);
u32 aarch64_insn_encode_immediate(enum aarch64_insn_imm_type type,
				  u32 insn, u64 imm);
u32 aarch64_insn_gen_branch_imm(unsigned long pc, unsigned long addr,
				enum aarch64_insn_branch_type type);
u32 aarch64_insn_gen_comp_branch_imm(unsigned long pc, unsigned long addr,
				     enum aarch64_insn_register reg,
				     enum aarch64_insn_variant variant,
				     enum aarch64_insn_branch_type type);
u32 aarch64_insn_gen_cond_branch_imm(unsigned long pc, unsigned long addr,
				     enum aarch64_insn_condition cond);
u32 aarch64_insn_gen_hint(enum aarch64_insn_hint_op op);
u32 aarch64_insn_gen_nop(void);
u32 aarch64_insn_gen_branch_reg(enum aarch64_insn_register reg,
				enum aarch64_insn_branch_type type);
u32 aarch64_insn_gen_load_store_reg(enum aarch64_insn_register reg,
				    enum aarch64_insn_register base,
				    enum aarch64_insn_register offset,
				    enum aarch64_insn_size_type size,
				    enum aarch64_insn_ldst_type type);
u32 aarch64_insn_gen_load_store_pair(enum aarch64_insn_register reg1,
				     enum aarch64_insn_register reg2,
				     enum aarch64_insn_register base,
				     int offset,
				     enum aarch64_insn_variant variant,
				     enum aarch64_insn_ldst_type type);
u32 aarch64_insn_gen_add_sub_imm(enum aarch64_insn_register dst,
				 enum aarch64_insn_register src,
				 int imm, enum aarch64_insn_variant variant,
				 enum aarch64_insn_adsb_type type);
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

bool aarch64_insn_hotpatch_safe(u32 old_insn, u32 new_insn);

int aarch64_insn_patch_text_nosync(void *addr, u32 insn);
int aarch64_insn_patch_text_sync(void *addrs[], u32 insns[], int cnt);
int aarch64_insn_patch_text(void *addrs[], u32 insns[], int cnt);

bool aarch32_insn_is_wide(u32 insn);

#define A32_RN_OFFSET	16
#define A32_RT_OFFSET	12
#define A32_RT2_OFFSET	 0

u32 aarch32_insn_extract_reg_num(u32 insn, int offset);
u32 aarch32_insn_mcr_extract_opc2(u32 insn);
u32 aarch32_insn_mcr_extract_crm(u32 insn);
#endif /* __ASSEMBLY__ */

#endif	/* __ASM_INSN_H */
