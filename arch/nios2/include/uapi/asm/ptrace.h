/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * based on m68k asm/processor.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _UAPI_ASM_NIOS2_PTRACE_H
#define _UAPI_ASM_NIOS2_PTRACE_H

#ifndef __ASSEMBLY__

/*
 * Register numbers used by 'ptrace' system call interface.
 */

/* GP registers */
#define PTR_R0		0
#define PTR_R1		1
#define PTR_R2		2
#define PTR_R3		3
#define PTR_R4		4
#define PTR_R5		5
#define PTR_R6		6
#define PTR_R7		7
#define PTR_R8		8
#define PTR_R9		9
#define PTR_R10		10
#define PTR_R11		11
#define PTR_R12		12
#define PTR_R13		13
#define PTR_R14		14
#define PTR_R15		15
#define PTR_R16		16
#define PTR_R17		17
#define PTR_R18		18
#define PTR_R19		19
#define PTR_R20		20
#define PTR_R21		21
#define PTR_R22		22
#define PTR_R23		23
#define PTR_R24		24
#define PTR_R25		25
#define PTR_GP		26
#define PTR_SP		27
#define PTR_FP		28
#define PTR_EA		29
#define PTR_BA		30
#define PTR_RA		31
/* Control registers */
#define PTR_PC		32
#define PTR_STATUS	33
#define PTR_ESTATUS	34
#define PTR_BSTATUS	35
#define PTR_IENABLE	36
#define PTR_IPENDING	37
#define PTR_CPUID	38
#define PTR_CTL6	39
#define PTR_CTL7	40
#define PTR_PTEADDR	41
#define PTR_TLBACC	42
#define PTR_TLBMISC	43

#define NUM_PTRACE_REG (PTR_TLBMISC + 1)

/* User structures for general purpose registers.  */
struct user_pt_regs {
	__u32		regs[49];
};

#endif /* __ASSEMBLY__ */
#endif /* _UAPI_ASM_NIOS2_PTRACE_H */
