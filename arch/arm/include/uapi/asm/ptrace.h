/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  arch/arm/include/asm/ptrace.h
 *
 *  Copyright (C) 1996-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _UAPI__ASM_ARM_PTRACE_H
#define _UAPI__ASM_ARM_PTRACE_H

#include <asm/hwcap.h>

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETFPREGS	14
#define PTRACE_SETFPREGS	15
/* PTRACE_ATTACH is 16 */
/* PTRACE_DETACH is 17 */
#define PTRACE_GETWMMXREGS	18
#define PTRACE_SETWMMXREGS	19
/* 20 is unused */
#define PTRACE_OLDSETOPTIONS	21
#define PTRACE_GET_THREAD_AREA	22
#define PTRACE_SET_SYSCALL	23
/* PTRACE_SYSCALL is 24 */
#define PTRACE_GETCRUNCHREGS	25
#define PTRACE_SETCRUNCHREGS	26
#define PTRACE_GETVFPREGS	27
#define PTRACE_SETVFPREGS	28
#define PTRACE_GETHBPREGS	29
#define PTRACE_SETHBPREGS	30
#define PTRACE_GETFDPIC		31

#define PTRACE_GETFDPIC_EXEC	0
#define PTRACE_GETFDPIC_INTERP	1

/*
 * PSR bits
 * Note on V7M there is no mode contained in the PSR
 */
#define USR26_MODE	0x00000000
#define FIQ26_MODE	0x00000001
#define IRQ26_MODE	0x00000002
#define SVC26_MODE	0x00000003
#if defined(__KERNEL__) && defined(CONFIG_CPU_V7M)
/*
 * Use 0 here to get code right that creates a userspace
 * or kernel space thread.
 */
#define USR_MODE	0x00000000
#define SVC_MODE	0x00000000
#else
#define USR_MODE	0x00000010
#define SVC_MODE	0x00000013
#endif
#define FIQ_MODE	0x00000011
#define IRQ_MODE	0x00000012
#define ABT_MODE	0x00000017
#define HYP_MODE	0x0000001a
#define UND_MODE	0x0000001b
#define SYSTEM_MODE	0x0000001f
#define MODE32_BIT	0x00000010
#define MODE_MASK	0x0000001f

#define V4_PSR_T_BIT	0x00000020	/* >= V4T, but not V7M */
#define V7M_PSR_T_BIT	0x01000000
#if defined(__KERNEL__) && defined(CONFIG_CPU_V7M)
#define PSR_T_BIT	V7M_PSR_T_BIT
#else
/* for compatibility */
#define PSR_T_BIT	V4_PSR_T_BIT
#endif

#define PSR_F_BIT	0x00000040	/* >= V4, but not V7M */
#define PSR_I_BIT	0x00000080	/* >= V4, but not V7M */
#define PSR_A_BIT	0x00000100	/* >= V6, but not V7M */
#define PSR_E_BIT	0x00000200	/* >= V6, but not V7M */
#define PSR_J_BIT	0x01000000	/* >= V5J, but not V7M */
#define PSR_Q_BIT	0x08000000	/* >= V5E, including V7M */
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_N_BIT	0x80000000

/*
 * Groups of PSR bits
 */
#define PSR_f		0xff000000	/* Flags		*/
#define PSR_s		0x00ff0000	/* Status		*/
#define PSR_x		0x0000ff00	/* Extension		*/
#define PSR_c		0x000000ff	/* Control		*/

/*
 * ARMv7 groups of PSR bits
 */
#define APSR_MASK	0xf80f0000	/* N, Z, C, V, Q and GE flags */
#define PSR_ISET_MASK	0x01000010	/* ISA state (J, T) mask */
#define PSR_IT_MASK	0x0600fc00	/* If-Then execution state mask */
#define PSR_ENDIAN_MASK	0x00000200	/* Endianness state mask */

/*
 * Default endianness state
 */
#ifdef CONFIG_CPU_ENDIAN_BE8
#define PSR_ENDSTATE	PSR_E_BIT
#else
#define PSR_ENDSTATE	0
#endif

/* 
 * These are 'magic' values for PTRACE_PEEKUSR that return info about where a
 * process is located in memory.
 */
#define PT_TEXT_ADDR		0x10000
#define PT_DATA_ADDR		0x10004
#define PT_TEXT_END_ADDR	0x10008

#ifndef __ASSEMBLY__

/*
 * This struct defines the way the registers are stored on the
 * stack during a system call.  Note that sizeof(struct pt_regs)
 * has to be a multiple of 8.
 */
#ifndef __KERNEL__
struct pt_regs {
	long uregs[18];
};
#endif /* __KERNEL__ */

#define ARM_cpsr	uregs[16]
#define ARM_pc		uregs[15]
#define ARM_lr		uregs[14]
#define ARM_sp		uregs[13]
#define ARM_ip		uregs[12]
#define ARM_fp		uregs[11]
#define ARM_r10		uregs[10]
#define ARM_r9		uregs[9]
#define ARM_r8		uregs[8]
#define ARM_r7		uregs[7]
#define ARM_r6		uregs[6]
#define ARM_r5		uregs[5]
#define ARM_r4		uregs[4]
#define ARM_r3		uregs[3]
#define ARM_r2		uregs[2]
#define ARM_r1		uregs[1]
#define ARM_r0		uregs[0]
#define ARM_ORIG_r0	uregs[17]

/*
 * The size of the user-visible VFP state as seen by PTRACE_GET/SETVFPREGS
 * and core dumps.
 */
#define ARM_VFPREGS_SIZE ( 32 * 8 /*fpregs*/ + 4 /*fpscr*/ )


#endif /* __ASSEMBLY__ */

#endif /* _UAPI__ASM_ARM_PTRACE_H */
