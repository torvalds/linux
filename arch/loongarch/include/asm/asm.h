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
#define INT_SLL		slli.w
#define INT_SLLV	sll.w
#define INT_SRL		srli.w
#define INT_SRLV	srl.w
#define INT_SRA		srai.w
#define INT_SRAV	sra.w
#endif

#if (__SIZEOF_INT__ == 8)
#define INT_ADD		add.d
#define INT_ADDI	addi.d
#define INT_SUB		sub.d
#define INT_L		ld.d
#define INT_S		st.d
#define INT_SLL		slli.d
#define INT_SLLV	sll.d
#define INT_SRL		srli.d
#define INT_SRLV	srl.d
#define INT_SRA		srai.d
#define INT_SRAV	sra.d
#endif

/*
 * How to add/sub/load/store/shift C long variables.
 */
#if (__SIZEOF_LONG__ == 4)
#define LONG_ADD	add.w
#define LONG_ADDI	addi.w
#define LONG_SUB	sub.w
#define LONG_L		ld.w
#define LONG_S		st.w
#define LONG_SLL	slli.w
#define LONG_SLLV	sll.w
#define LONG_SRL	srli.w
#define LONG_SRLV	srl.w
#define LONG_SRA	srai.w
#define LONG_SRAV	sra.w

#ifdef __ASSEMBLY__
#define LONG		.word
#endif
#define LONGSIZE	4
#define LONGMASK	3
#define LONGLOG		2
#endif

#if (__SIZEOF_LONG__ == 8)
#define LONG_ADD	add.d
#define LONG_ADDI	addi.d
#define LONG_SUB	sub.d
#define LONG_L		ld.d
#define LONG_S		st.d
#define LONG_SLL	slli.d
#define LONG_SLLV	sll.d
#define LONG_SRL	srli.d
#define LONG_SRLV	srl.d
#define LONG_SRA	srai.d
#define LONG_SRAV	sra.d

#ifdef __ASSEMBLY__
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
#define PTR_SUB		sub.w
#define PTR_L		ld.w
#define PTR_S		st.w
#define PTR_LI		li.w
#define PTR_SLL		slli.w
#define PTR_SLLV	sll.w
#define PTR_SRL		srli.w
#define PTR_SRLV	srl.w
#define PTR_SRA		srai.w
#define PTR_SRAV	sra.w

#define PTR_SCALESHIFT	2

#ifdef __ASSEMBLY__
#define PTR		.word
#endif
#define PTRSIZE		4
#define PTRLOG		2
#endif

#if (__SIZEOF_POINTER__ == 8)
#define PTR_ADD		add.d
#define PTR_ADDI	addi.d
#define PTR_SUB		sub.d
#define PTR_L		ld.d
#define PTR_S		st.d
#define PTR_LI		li.d
#define PTR_SLL		slli.d
#define PTR_SLLV	sll.d
#define PTR_SRL		srli.d
#define PTR_SRLV	srl.d
#define PTR_SRA		srai.d
#define PTR_SRAV	sra.d

#define PTR_SCALESHIFT	3

#ifdef __ASSEMBLY__
#define PTR		.dword
#endif
#define PTRSIZE		8
#define PTRLOG		3
#endif

#endif /* __ASM_ASM_H */
