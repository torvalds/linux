/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Some useful macros for LoongArch assembler code
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1995, 1996, 1997, 1999, 2001 by Ralf Baechle
 * Copyright (C) 1999 by Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 * Copyright (C) 2002  Maciej W. Rozycki
 */
#ifndef __ASM_ASM_H
#define __ASM_ASM_H

/* LoongArch pref instruction. */
#ifdef CONFIG_CPU_HAS_PREFETCH

#define PREF(hint, addr, offs)				\
		preld	hint, addr, offs;		\

#define PREFX(hint, addr, index)			\
		preldx	hint, addr, index;		\

#else /* !CONFIG_CPU_HAS_PREFETCH */

#define PREF(hint, addr, offs)
#define PREFX(hint, addr, index)

#endif /* !CONFIG_CPU_HAS_PREFETCH */

/*
 * Stack alignment
 */
#define STACK_ALIGN	~(0xf)

/*
 * Macros to handle different pointer/register sizes for 32/64-bit code
 */

/*
 * Size of a register
 */
#ifndef __loongarch64
#define SZREG	4
#else
#define SZREG	8
#endif

/*
 * Use the following macros in assemblercode to load/store registers,
 * pointers etc.
 */
#if (SZREG == 4)
#define REG_L		ld.w
#define REG_S		st.w
#define REG_ADD		add.w
#define REG_SUB		sub.w
#else /* SZREG == 8 */
#define REG_L		ld.d
#define REG_S		st.d
#define REG_ADD		add.d
#define REG_SUB		sub.d
#endif

/*
 * How to add/sub/load/store/shift C int variables.
 */
#if (__SIZEOF_INT__ == 4)
#define INT_ADD		add.w
#define INT_ADDI	addi.w
#define INT_SUB		sub.w
#define INT_L		ld.w
#define INT_S		st.w
#define INT_SLLI	slli.w
#define INT_SLLV	sll.w
#define INT_SRLI	srli.w
#define INT_SRLV	srl.w
#define INT_SRAI	srai.w
#define INT_SRAV	sra.w
#endif

#if (__SIZEOF_INT__ == 8)
#define INT_ADD		add.d
#define INT_ADDI	addi.d
#define INT_SUB		sub.d
#define INT_L		ld.d
#define INT_S		st.d
#define INT_SLLI	slli.d
#define INT_SLLV	sll.d
#define INT_SRLI	srli.d
#define INT_SRLV	srl.d
#define INT_SRAI	srai.d
#define INT_SRAV	sra.d
#endif

/*
 * How to add/sub/load/store/shift C long variables.
 */
#if (__SIZEOF_LONG__ == 4)
#define LONG_ADD	add.w
#define LONG_ADDI	addi.w
#define LONG_ALSL	alsl.w
#define LONG_BSTRINS	bstrins.w
#define LONG_BSTRPICK	bstrpick.w
#define LONG_SUB	sub.w
#define LONG_L		ld.w
#define LONG_LI		li.w
#define LONG_LPTR	ld.w
#define LONG_S		st.w
#define LONG_SPTR	st.w
#define LONG_SLLI	slli.w
#define LONG_SLLV	sll.w
#define LONG_SRLI	srli.w
#define LONG_SRLV	srl.w
#define LONG_SRAI	srai.w
#define LONG_SRAV	sra.w
#define LONG_ROTR	rotr.w
#define LONG_ROTRI	rotri.w

#ifdef __ASSEMBLER__
#define LONG		.word
#endif
#define LONGSIZE	4
#define LONGMASK	3
#define LONGLOG		2
#endif

#if (__SIZEOF_LONG__ == 8)
#define LONG_ADD	add.d
#define LONG_ADDI	addi.d
#define LONG_ALSL	alsl.d
#define LONG_BSTRINS	bstrins.d
#define LONG_BSTRPICK	bstrpick.d
#define LONG_SUB	sub.d
#define LONG_L		ld.d
#define LONG_LI		li.d
#define LONG_LPTR	ldptr.d
#define LONG_S		st.d
#define LONG_SPTR	stptr.d
#define LONG_SLLI	slli.d
#define LONG_SLLV	sll.d
#define LONG_SRLI	srli.d
#define LONG_SRLV	srl.d
#define LONG_SRAI	srai.d
#define LONG_SRAV	sra.d
#define LONG_ROTR	rotr.d
#define LONG_ROTRI	rotri.d

#ifdef __ASSEMBLER__
#define LONG		.dword
#endif
#define LONGSIZE	8
#define LONGMASK	7
#define LONGLOG		3
#endif

/*
 * How to add/sub/load/store/shift pointers.
 */
#if (__SIZEOF_POINTER__ == 4)
#define PTR_ADD		add.w
#define PTR_ADDI	addi.w
#define PTR_ALSL	alsl.w
#define PTR_BSTRINS	bstrins.w
#define PTR_BSTRPICK	bstrpick.w
#define PTR_SUB		sub.w
#define PTR_L		ld.w
#define PTR_LI		li.w
#define PTR_LPTR	ld.w
#define PTR_S		st.w
#define PTR_SPTR	st.w
#define PTR_SLLI	slli.w
#define PTR_SLLV	sll.w
#define PTR_SRLI	srli.w
#define PTR_SRLV	srl.w
#define PTR_SRAI	srai.w
#define PTR_SRAV	sra.w
#define PTR_ROTR	rotr.w
#define PTR_ROTRI	rotri.w

#define PTR_SCALESHIFT	2

#ifdef __ASSEMBLER__
#define PTR		.word
#endif
#define PTRSIZE		4
#define PTRLOG		2
#endif

#if (__SIZEOF_POINTER__ == 8)
#define PTR_ADD		add.d
#define PTR_ADDI	addi.d
#define PTR_ALSL	alsl.d
#define PTR_BSTRINS	bstrins.d
#define PTR_BSTRPICK	bstrpick.d
#define PTR_SUB		sub.d
#define PTR_L		ld.d
#define PTR_LI		li.d
#define PTR_LPTR	ldptr.d
#define PTR_S		st.d
#define PTR_SPTR	stptr.d
#define PTR_SLLI	slli.d
#define PTR_SLLV	sll.d
#define PTR_SRLI	srli.d
#define PTR_SRLV	srl.d
#define PTR_SRAI	srai.d
#define PTR_SRAV	sra.d
#define PTR_ROTR	rotr.d
#define PTR_ROTRI	rotri.d

#define PTR_SCALESHIFT	3

#ifdef __ASSEMBLER__
#define PTR		.dword
#endif
#define PTRSIZE		8
#define PTRLOG		3
#endif

/* Annotate a function as being unsuitable for kprobes. */
#ifdef CONFIG_KPROBES
#ifdef CONFIG_32BIT
#define _ASM_NOKPROBE(name)				\
	.pushsection "_kprobe_blacklist", "aw";		\
	.long	name;					\
	.popsection
#else
#define _ASM_NOKPROBE(name)				\
	.pushsection "_kprobe_blacklist", "aw";		\
	.quad	name;					\
	.popsection
#endif
#else
#define _ASM_NOKPROBE(name)
#endif

#endif /* __ASM_ASM_H */
