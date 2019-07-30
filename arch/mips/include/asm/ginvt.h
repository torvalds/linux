/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MIPS_ASM_GINVT_H__
#define __MIPS_ASM_GINVT_H__

#include <asm/mipsregs.h>

enum ginvt_type {
	GINVT_FULL,
	GINVT_VA,
	GINVT_MMID,
};

#ifdef TOOLCHAIN_SUPPORTS_GINV
# define _ASM_SET_GINV	".set	ginv\n"
#else
_ASM_MACRO_1R1I(ginvt, rs, type,
		_ASM_INSN_IF_MIPS(0x7c0000bd | (__rs << 21) | (\\type << 8))
		_ASM_INSN32_IF_MM(0x0000717c | (__rs << 16) | (\\type << 9)));
# define _ASM_SET_GINV
#endif

static inline void ginvt(unsigned long addr, enum ginvt_type type)
{
	asm volatile(
		".set	push\n"
		_ASM_SET_GINV
		"	ginvt	%0, %1\n"
		".set	pop"
		: /* no outputs */
		: "r"(addr), "i"(type)
		: "memory");
}

static inline void ginvt_full(void)
{
	ginvt(0, GINVT_FULL);
}

static inline void ginvt_va(unsigned long addr)
{
	addr &= PAGE_MASK << 1;
	ginvt(addr, GINVT_VA);
}

static inline void ginvt_mmid(void)
{
	ginvt(0, GINVT_MMID);
}

static inline void ginvt_va_mmid(unsigned long addr)
{
	addr &= PAGE_MASK << 1;
	ginvt(addr, GINVT_VA | GINVT_MMID);
}

#endif /* __MIPS_ASM_GINVT_H__ */
