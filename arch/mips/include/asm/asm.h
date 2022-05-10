/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 1999, 2001 by Ralf Baechle
 * Copyright (C) 1999 by Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 * Copyright (C) 2002  Maciej W. Rozycki
 *
 * Some useful macros for MIPS assembler code
 *
 * Some of the routines below contain useless nops that will be optimized
 * away by gas in -O mode. These nops are however required to fill delay
 * slots in noreorder mode.
 */
#ifndef __ASM_ASM_H
#define __ASM_ASM_H

#include <asm/sgidefs.h>
#include <asm/asm-eva.h>

#ifndef __VDSO__
/*
 * Emit CFI data in .debug_frame sections, not .eh_frame sections.
 * We don't do DWARF unwinding at runtime, so only the offline DWARF
 * information is useful to anyone. Note we should change this if we
 * ever decide to enable DWARF unwinding at runtime.
 */
#define CFI_SECTIONS	.cfi_sections .debug_frame
#else
 /*
  * For the vDSO, emit both runtime unwind information and debug
  * symbols for the .dbg file.
  */
#define CFI_SECTIONS
#endif

/*
 * LEAF - declare leaf routine
 */
#define LEAF(symbol)					\
		CFI_SECTIONS;				\
		.globl	symbol;				\
		.align	2;				\
		.type	symbol, @function;		\
		.ent	symbol, 0;			\
symbol:		.frame	sp, 0, ra;			\
		.cfi_startproc;				\
		.insn

/*
 * NESTED - declare nested routine entry point
 */
#define NESTED(symbol, framesize, rpc)			\
		CFI_SECTIONS;				\
		.globl	symbol;				\
		.align	2;				\
		.type	symbol, @function;		\
		.ent	symbol, 0;			\
symbol:		.frame	sp, framesize, rpc;		\
		.cfi_startproc;				\
		.insn

/*
 * END - mark end of function
 */
#define END(function)					\
		.cfi_endproc;				\
		.end	function;			\
		.size	function, .-function

/*
 * EXPORT - export definition of symbol
 */
#define EXPORT(symbol)					\
		.globl	symbol;				\
symbol:

/*
 * FEXPORT - export definition of a function symbol
 */
#define FEXPORT(symbol)					\
		.globl	symbol;				\
		.type	symbol, @function;		\
symbol:		.insn

/*
 * ABS - export absolute symbol
 */
#define ABS(symbol,value)				\
		.globl	symbol;				\
symbol		=	value

#define TEXT(msg)					\
		.pushsection .data;			\
8:		.asciiz msg;				\
		.popsection;

#define ASM_PANIC(msg)					\
		.set	push;				\
		.set	reorder;			\
		PTR_LA	a0, 8f;				\
		jal	panic;				\
9:		b	9b;				\
		.set	pop;				\
		TEXT(msg)

/*
 * Print formatted string
 */
#ifdef CONFIG_PRINTK
#define ASM_PRINT(string)				\
		.set	push;				\
		.set	reorder;			\
		PTR_LA	a0, 8f;				\
		jal	_printk;			\
		.set	pop;				\
		TEXT(string)
#else
#define ASM_PRINT(string)
#endif

/*
 * Stack alignment
 */
#if (_MIPS_SIM == _MIPS_SIM_ABI32)
#define ALSZ	7
#define ALMASK	~7
#endif
#if (_MIPS_SIM == _MIPS_SIM_NABI32) || (_MIPS_SIM == _MIPS_SIM_ABI64)
#define ALSZ	15
#define ALMASK	~15
#endif

/*
 * Macros to handle different pointer/register sizes for 32/64-bit code
 */

/*
 * Size of a register
 */
#ifdef __mips64
#define SZREG	8
#else
#define SZREG	4
#endif

/*
 * Use the following macros in assemblercode to load/store registers,
 * pointers etc.
 */
#if (_MIPS_SIM == _MIPS_SIM_ABI32)
#define REG_S		sw
#define REG_L		lw
#define REG_SUBU	subu
#define REG_ADDU	addu
#endif
#if (_MIPS_SIM == _MIPS_SIM_NABI32) || (_MIPS_SIM == _MIPS_SIM_ABI64)
#define REG_S		sd
#define REG_L		ld
#define REG_SUBU	dsubu
#define REG_ADDU	daddu
#endif

/*
 * How to add/sub/load/store/shift C int variables.
 */
#if (_MIPS_SZINT == 32)
#define INT_ADD		add
#define INT_ADDU	addu
#define INT_ADDI	addi
#define INT_ADDIU	addiu
#define INT_SUB		sub
#define INT_SUBU	subu
#define INT_L		lw
#define INT_S		sw
#define INT_SLL		sll
#define INT_SLLV	sllv
#define INT_SRL		srl
#define INT_SRLV	srlv
#define INT_SRA		sra
#define INT_SRAV	srav
#endif

#if (_MIPS_SZINT == 64)
#define INT_ADD		dadd
#define INT_ADDU	daddu
#define INT_ADDI	daddi
#define INT_ADDIU	daddiu
#define INT_SUB		dsub
#define INT_SUBU	dsubu
#define INT_L		ld
#define INT_S		sd
#define INT_SLL		dsll
#define INT_SLLV	dsllv
#define INT_SRL		dsrl
#define INT_SRLV	dsrlv
#define INT_SRA		dsra
#define INT_SRAV	dsrav
#endif

/*
 * How to add/sub/load/store/shift C long variables.
 */
#if (_MIPS_SZLONG == 32)
#define LONG_ADD	add
#define LONG_ADDU	addu
#define LONG_ADDI	addi
#define LONG_ADDIU	addiu
#define LONG_SUB	sub
#define LONG_SUBU	subu
#define LONG_L		lw
#define LONG_S		sw
#define LONG_SP		swp
#define LONG_SLL	sll
#define LONG_SLLV	sllv
#define LONG_SRL	srl
#define LONG_SRLV	srlv
#define LONG_SRA	sra
#define LONG_SRAV	srav

#ifdef __ASSEMBLY__
#define LONG		.word
#endif
#define LONGSIZE	4
#define LONGMASK	3
#define LONGLOG		2
#endif

#if (_MIPS_SZLONG == 64)
#define LONG_ADD	dadd
#define LONG_ADDU	daddu
#define LONG_ADDI	daddi
#define LONG_ADDIU	daddiu
#define LONG_SUB	dsub
#define LONG_SUBU	dsubu
#define LONG_L		ld
#define LONG_S		sd
#define LONG_SP		sdp
#define LONG_SLL	dsll
#define LONG_SLLV	dsllv
#define LONG_SRL	dsrl
#define LONG_SRLV	dsrlv
#define LONG_SRA	dsra
#define LONG_SRAV	dsrav

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
#if (_MIPS_SZPTR == 32)
#define PTR_ADD		add
#define PTR_ADDU	addu
#define PTR_ADDI	addi
#define PTR_ADDIU	addiu
#define PTR_SUB		sub
#define PTR_SUBU	subu
#define PTR_L		lw
#define PTR_S		sw
#define PTR_LA		la
#define PTR_LI		li
#define PTR_SLL		sll
#define PTR_SLLV	sllv
#define PTR_SRL		srl
#define PTR_SRLV	srlv
#define PTR_SRA		sra
#define PTR_SRAV	srav

#define PTR_SCALESHIFT	2

#define PTR_WD		.word
#define PTRSIZE		4
#define PTRLOG		2
#endif

#if (_MIPS_SZPTR == 64)
#define PTR_ADD		dadd
#define PTR_ADDU	daddu
#define PTR_ADDI	daddi
#define PTR_ADDIU	daddiu
#define PTR_SUB		dsub
#define PTR_SUBU	dsubu
#define PTR_L		ld
#define PTR_S		sd
#define PTR_LA		dla
#define PTR_LI		dli
#define PTR_SLL		dsll
#define PTR_SLLV	dsllv
#define PTR_SRL		dsrl
#define PTR_SRLV	dsrlv
#define PTR_SRA		dsra
#define PTR_SRAV	dsrav

#define PTR_SCALESHIFT	3

#define PTR_WD		.dword
#define PTRSIZE		8
#define PTRLOG		3
#endif

/*
 * Some cp0 registers were extended to 64bit for MIPS III.
 */
#if (_MIPS_SIM == _MIPS_SIM_ABI32)
#define MFC0		mfc0
#define MTC0		mtc0
#endif
#if (_MIPS_SIM == _MIPS_SIM_NABI32) || (_MIPS_SIM == _MIPS_SIM_ABI64)
#define MFC0		dmfc0
#define MTC0		dmtc0
#endif

#define SSNOP		sll zero, zero, 1

#ifdef CONFIG_SGI_IP28
/* Inhibit speculative stores to volatile (e.g.DMA) or invalid addresses. */
#include <asm/cacheops.h>
#define R10KCBARRIER(addr)  cache   Cache_Barrier, addr;
#else
#define R10KCBARRIER(addr)
#endif

#endif /* __ASM_ASM_H */
