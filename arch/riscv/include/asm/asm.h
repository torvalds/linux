/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_ASM_H
#define _ASM_RISCV_ASM_H

#ifdef __ASSEMBLY__
#define __ASM_STR(x)	x
#else
#define __ASM_STR(x)	#x
#endif

#if __riscv_xlen == 64
#define __REG_SEL(a, b)	__ASM_STR(a)
#elif __riscv_xlen == 32
#define __REG_SEL(a, b)	__ASM_STR(b)
#else
#error "Unexpected __riscv_xlen"
#endif

#define REG_L		__REG_SEL(ld, lw)
#define REG_S		__REG_SEL(sd, sw)
#define REG_SC		__REG_SEL(sc.d, sc.w)
#define REG_AMOSWAP_AQ	__REG_SEL(amoswap.d.aq, amoswap.w.aq)
#define REG_ASM		__REG_SEL(.dword, .word)
#define SZREG		__REG_SEL(8, 4)
#define LGREG		__REG_SEL(3, 2)

#if __SIZEOF_POINTER__ == 8
#ifdef __ASSEMBLY__
#define RISCV_PTR		.dword
#define RISCV_SZPTR		8
#define RISCV_LGPTR		3
#else
#define RISCV_PTR		".dword"
#define RISCV_SZPTR		"8"
#define RISCV_LGPTR		"3"
#endif
#elif __SIZEOF_POINTER__ == 4
#ifdef __ASSEMBLY__
#define RISCV_PTR		.word
#define RISCV_SZPTR		4
#define RISCV_LGPTR		2
#else
#define RISCV_PTR		".word"
#define RISCV_SZPTR		"4"
#define RISCV_LGPTR		"2"
#endif
#else
#error "Unexpected __SIZEOF_POINTER__"
#endif

#if (__SIZEOF_INT__ == 4)
#define RISCV_INT		__ASM_STR(.word)
#define RISCV_SZINT		__ASM_STR(4)
#define RISCV_LGINT		__ASM_STR(2)
#else
#error "Unexpected __SIZEOF_INT__"
#endif

#if (__SIZEOF_SHORT__ == 2)
#define RISCV_SHORT		__ASM_STR(.half)
#define RISCV_SZSHORT		__ASM_STR(2)
#define RISCV_LGSHORT		__ASM_STR(1)
#else
#error "Unexpected __SIZEOF_SHORT__"
#endif

#ifdef __ASSEMBLY__
#include <asm/asm-offsets.h>

/* Common assembly source macros */

/*
 * NOP sequence
 */
.macro	nops, num
	.rept	\num
	nop
	.endr
.endm

#ifdef CONFIG_SMP
#ifdef CONFIG_32BIT
#define PER_CPU_OFFSET_SHIFT 2
#else
#define PER_CPU_OFFSET_SHIFT 3
#endif

.macro asm_per_cpu dst sym tmp
	REG_L \tmp, TASK_TI_CPU_NUM(tp)
	slli  \tmp, \tmp, PER_CPU_OFFSET_SHIFT
	la    \dst, __per_cpu_offset
	add   \dst, \dst, \tmp
	REG_L \tmp, 0(\dst)
	la    \dst, \sym
	add   \dst, \dst, \tmp
.endm
#else /* CONFIG_SMP */
.macro asm_per_cpu dst sym tmp
	la    \dst, \sym
.endm
#endif /* CONFIG_SMP */

	/* save all GPs except x1 ~ x5 */
	.macro save_from_x6_to_x31
	REG_S x6,  PT_T1(sp)
	REG_S x7,  PT_T2(sp)
	REG_S x8,  PT_S0(sp)
	REG_S x9,  PT_S1(sp)
	REG_S x10, PT_A0(sp)
	REG_S x11, PT_A1(sp)
	REG_S x12, PT_A2(sp)
	REG_S x13, PT_A3(sp)
	REG_S x14, PT_A4(sp)
	REG_S x15, PT_A5(sp)
	REG_S x16, PT_A6(sp)
	REG_S x17, PT_A7(sp)
	REG_S x18, PT_S2(sp)
	REG_S x19, PT_S3(sp)
	REG_S x20, PT_S4(sp)
	REG_S x21, PT_S5(sp)
	REG_S x22, PT_S6(sp)
	REG_S x23, PT_S7(sp)
	REG_S x24, PT_S8(sp)
	REG_S x25, PT_S9(sp)
	REG_S x26, PT_S10(sp)
	REG_S x27, PT_S11(sp)
	REG_S x28, PT_T3(sp)
	REG_S x29, PT_T4(sp)
	REG_S x30, PT_T5(sp)
	REG_S x31, PT_T6(sp)
	.endm

	/* restore all GPs except x1 ~ x5 */
	.macro restore_from_x6_to_x31
	REG_L x6,  PT_T1(sp)
	REG_L x7,  PT_T2(sp)
	REG_L x8,  PT_S0(sp)
	REG_L x9,  PT_S1(sp)
	REG_L x10, PT_A0(sp)
	REG_L x11, PT_A1(sp)
	REG_L x12, PT_A2(sp)
	REG_L x13, PT_A3(sp)
	REG_L x14, PT_A4(sp)
	REG_L x15, PT_A5(sp)
	REG_L x16, PT_A6(sp)
	REG_L x17, PT_A7(sp)
	REG_L x18, PT_S2(sp)
	REG_L x19, PT_S3(sp)
	REG_L x20, PT_S4(sp)
	REG_L x21, PT_S5(sp)
	REG_L x22, PT_S6(sp)
	REG_L x23, PT_S7(sp)
	REG_L x24, PT_S8(sp)
	REG_L x25, PT_S9(sp)
	REG_L x26, PT_S10(sp)
	REG_L x27, PT_S11(sp)
	REG_L x28, PT_T3(sp)
	REG_L x29, PT_T4(sp)
	REG_L x30, PT_T5(sp)
	REG_L x31, PT_T6(sp)
	.endm

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_ASM_H */
