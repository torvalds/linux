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
	AARCH64_INSN_IMM_MAX
};

enum aarch64_insn_register_type {
	AARCH64_INSN_REGTYPE_RT,
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

enum aarch64_insn_branch_type {
	AARCH64_INSN_BRANCH_NOLINK,
	AARCH64_INSN_BRANCH_LINK,
	AARCH64_INSN_BRANCH_COMP_ZERO,
	AARCH64_INSN_BRANCH_COMP_NONZERO,
};

#define	__AARCH64_INSN_FUNCS(abbr, mask, val)	\
static __always_inline bool aarch64_insn_is_##abbr(u32 code) \
{ return (code & (mask)) == (val); } \
static __always_inline u32 aarch64_insn_get_##abbr##_value(void) \
{ return (val); }

__AARCH64_INSN_FUNCS(b,		0xFC000000, 0x14000000)
__AARCH64_INSN_FUNCS(bl,	0xFC000000, 0x94000000)
__AARCH64_INSN_FUNCS(cbz,	0xFE000000, 0x34000000)
__AARCH64_INSN_FUNCS(cbnz,	0xFE000000, 0x35000000)
__AARCH64_INSN_FUNCS(svc,	0xFFE0001F, 0xD4000001)
__AARCH64_INSN_FUNCS(hvc,	0xFFE0001F, 0xD4000002)
__AARCH64_INSN_FUNCS(smc,	0xFFE0001F, 0xD4000003)
__AARCH64_INSN_FUNCS(brk,	0xFFE0001F, 0xD4200000)
__AARCH64_INSN_FUNCS(hint,	0xFFFFF01F, 0xD503201F)

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
u32 aarch64_insn_gen_hint(enum aarch64_insn_hint_op op);
u32 aarch64_insn_gen_nop(void);

bool aarch64_insn_hotpatch_safe(u32 old_insn, u32 new_insn);

int aarch64_insn_patch_text_nosync(void *addr, u32 insn);
int aarch64_insn_patch_text_sync(void *addrs[], u32 insns[], int cnt);
int aarch64_insn_patch_text(void *addrs[], u32 insns[], int cnt);
#endif /* __ASSEMBLY__ */

#endif	/* __ASM_INSN_H */
