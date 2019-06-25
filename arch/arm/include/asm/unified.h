/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/asm-arm/unified.h - Unified Assembler Syntax helper macros
 *
 * Copyright (C) 2008 ARM Limited
 */

#ifndef __ASM_UNIFIED_H
#define __ASM_UNIFIED_H

#if defined(__ASSEMBLY__)
	.syntax unified
#else
__asm__(".syntax unified");
#endif

#ifdef CONFIG_CPU_V7M
#define AR_CLASS(x...)
#define M_CLASS(x...)	x
#else
#define AR_CLASS(x...)	x
#define M_CLASS(x...)
#endif

#ifdef CONFIG_THUMB2_KERNEL

#if __GNUC__ < 4
#error Thumb-2 kernel requires gcc >= 4
#endif

/* The CPSR bit describing the instruction set (Thumb) */
#define PSR_ISETSTATE	PSR_T_BIT

#define ARM(x...)
#define THUMB(x...)	x
#ifdef __ASSEMBLY__
#define W(instr)	instr.w
#else
#define WASM(instr)	#instr ".w"
#endif

#else	/* !CONFIG_THUMB2_KERNEL */

/* The CPSR bit describing the instruction set (ARM) */
#define PSR_ISETSTATE	0

#define ARM(x...)	x
#define THUMB(x...)
#ifdef __ASSEMBLY__
#define W(instr)	instr
#else
#define WASM(instr)	#instr
#endif

#endif	/* CONFIG_THUMB2_KERNEL */

#endif	/* !__ASM_UNIFIED_H */
