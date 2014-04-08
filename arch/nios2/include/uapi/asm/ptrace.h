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

/* Text/data offsets, needed by gdbserver */
#define PT_TEXT_ADDR		(44*4)
#define PT_TEXT_END_ADDR	(45*4)
#define PT_DATA_ADDR		(46*4)

/* this struct defines the way the registers are stored on the
   stack during a system call.

   There is a fake_regs in setup.c that has to match pt_regs.*/

struct pt_regs {
	unsigned long  r8;		/* r8-r15 Caller-saved GP registers */
	unsigned long  r9;
	unsigned long  r10;
	unsigned long  r11;
	unsigned long  r12;
	unsigned long  r13;
	unsigned long  r14;
	unsigned long  r15;
	unsigned long  r1;		/* Assembler temporary */
	unsigned long  r2;		/* Retval LS 32bits */
	unsigned long  r3;		/* Retval MS 32bits */
	unsigned long  r4;		/* r4-r7 Register arguments */
	unsigned long  r5;
	unsigned long  r6;
	unsigned long  r7;
	unsigned long  orig_r2;		/* Copy of r2 ?? */
	unsigned long  ra;		/* Return address */
	unsigned long  fp;		/* Frame pointer */
	unsigned long  sp;		/* Stack pointer */
	unsigned long  gp;		/* Global pointer */
	unsigned long  estatus;
	unsigned long  ea;		/* Exception return address (pc) */
	unsigned long  orig_r7;
};

/*
 * This is the extended stack used by signal handlers and the context
 * switcher: it's pushed after the normal "struct pt_regs".
 */
struct switch_stack {
	unsigned long  r16;		/* r16-r23 Callee-saved GP registers */
	unsigned long  r17;
	unsigned long  r18;
	unsigned long  r19;
	unsigned long  r20;
	unsigned long  r21;
	unsigned long  r22;
	unsigned long  r23;
	unsigned long  fp;
	unsigned long  gp;
	unsigned long  ra;
};

#endif /* __ASSEMBLY__ */
#endif /* _UAPI_ASM_NIOS2_PTRACE_H */
