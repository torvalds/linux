/* registers.h: register frame declarations
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * notes:
 *
 * (1) that the members of all these structures are carefully aligned to permit
 *     usage of STD/STDF instructions
 *
 * (2) if you change these structures, you must change the code in
 *     arch/frvnommu/kernel/{break.S,entry.S,switch_to.S,gdb-stub.c}
 *
 *
 * the kernel stack space block looks like this:
 *
 *	+0x2000	+----------------------
 *		| union {
 *		|	struct frv_frame0 {
 *		|		struct user_context {
 *		|			struct user_int_regs
 *		|			struct user_fpmedia_regs
 *		|		}
 *		|		struct frv_debug_regs
 *		|	}
 *		|	struct pt_regs [user exception]
 *		| }
 *		+---------------------- <-- __kernel_frame0_ptr (maybe GR28)
 *		|
 *		| kernel stack
 *		|
 *		|......................
 *		| struct pt_regs [kernel exception]
 *		|...................... <-- __kernel_frame0_ptr (maybe GR28)
 *		|
 *		| kernel stack
 *		|
 *		|...................... <-- stack pointer (GR1)
 *		|
 *		| unused stack space
 *		|
 *		+----------------------
 *		| struct thread_info
 *	+0x0000	+---------------------- <-- __current_thread_info (GR15);
 *
 * note that GR28 points to the current exception frame
 */

#ifndef _ASM_REGISTERS_H
#define _ASM_REGISTERS_H

#ifndef __ASSEMBLY__
#define __OFFSET(X,N)	((X)+(N)*4)
#define __OFFSETC(X,N)	xxxxxxxxxxxxxxxxxxxxxxxx
#else
#define __OFFSET(X,N)	((X)+(N)*4)
#define __OFFSETC(X,N)	((X)+(N))
#endif

/*****************************************************************************/
/*
 * Exception/Interrupt frame
 * - held on kernel stack
 * - 8-byte aligned on stack (old SP is saved in frame)
 * - GR0 is fixed 0, so we don't save it
 */
#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long		psr;		/* Processor Status Register */
	unsigned long		isr;		/* Integer Status Register */
	unsigned long		ccr;		/* Condition Code Register */
	unsigned long		cccr;		/* Condition Code for Conditional Insns Register */
	unsigned long		lr;		/* Link Register */
	unsigned long		lcr;		/* Loop Count Register */
	unsigned long		pc;		/* Program Counter Register */
	unsigned long		__status;	/* exception status */
	unsigned long		syscallno;	/* syscall number or -1 */
	unsigned long		orig_gr8;	/* original syscall arg #1 */
	unsigned long		gner0;
	unsigned long		gner1;
	unsigned long long	iacc0;
	unsigned long		tbr;		/* GR0 is fixed zero, so we use this for TBR */
	unsigned long		sp;		/* GR1: USP/KSP */
	unsigned long		fp;		/* GR2: FP */
	unsigned long		gr3;
	unsigned long		gr4;
	unsigned long		gr5;
	unsigned long		gr6;
	unsigned long		gr7;		/* syscall number */
	unsigned long		gr8;		/* 1st syscall param; syscall return */
	unsigned long		gr9;		/* 2nd syscall param */
	unsigned long		gr10;		/* 3rd syscall param */
	unsigned long		gr11;		/* 4th syscall param */
	unsigned long		gr12;		/* 5th syscall param */
	unsigned long		gr13;		/* 6th syscall param */
	unsigned long		gr14;
	unsigned long		gr15;
	unsigned long		gr16;		/* GP pointer */
	unsigned long		gr17;		/* small data */
	unsigned long		gr18;		/* PIC/PID */
	unsigned long		gr19;
	unsigned long		gr20;
	unsigned long		gr21;
	unsigned long		gr22;
	unsigned long		gr23;
	unsigned long		gr24;
	unsigned long		gr25;
	unsigned long		gr26;
	unsigned long		gr27;
	struct pt_regs		*next_frame;	/* GR28 - next exception frame */
	unsigned long		gr29;		/* GR29 - OS reserved */
	unsigned long		gr30;		/* GR30 - OS reserved */
	unsigned long		gr31;		/* GR31 - OS reserved */
} __attribute__((aligned(8)));

#endif

#define REG__STATUS_STEP	0x00000001	/* - reenable single stepping on return */
#define REG__STATUS_STEPPED	0x00000002	/* - single step caused exception */
#define REG__STATUS_BROKE	0x00000004	/* - BREAK insn caused exception */
#define REG__STATUS_SYSC_ENTRY	0x40000000	/* - T on syscall entry (ptrace.c only) */
#define REG__STATUS_SYSC_EXIT	0x80000000	/* - T on syscall exit (ptrace.c only) */

#define REG_GR(R)	__OFFSET(REG_GR0, (R))

#define REG_SP		REG_GR(1)
#define REG_FP		REG_GR(2)
#define REG_PREV_FRAME	REG_GR(28)	/* previous exception frame pointer (old gr28 value) */
#define REG_CURR_TASK	REG_GR(29)	/* current task */

/*****************************************************************************/
/*
 * debugging registers
 */
#ifndef __ASSEMBLY__

struct frv_debug_regs
{
	unsigned long		dcr;
	unsigned long		ibar[4] __attribute__((aligned(8)));
	unsigned long		dbar[4] __attribute__((aligned(8)));
	unsigned long		dbdr[4][4] __attribute__((aligned(8)));
	unsigned long		dbmr[4][4] __attribute__((aligned(8)));
} __attribute__((aligned(8)));

#endif

/*****************************************************************************/
/*
 * userspace registers
 */
#ifndef __ASSEMBLY__

struct user_int_regs
{
	/* integer registers
	 * - up to gr[31] mirror pt_regs
	 * - total size must be multiple of 8 bytes
	 */
	unsigned long		psr;		/* Processor Status Register */
	unsigned long		isr;		/* Integer Status Register */
	unsigned long		ccr;		/* Condition Code Register */
	unsigned long		cccr;		/* Condition Code for Conditional Insns Register */
	unsigned long		lr;		/* Link Register */
	unsigned long		lcr;		/* Loop Count Register */
	unsigned long		pc;		/* Program Counter Register */
	unsigned long		__status;	/* exception status */
	unsigned long		syscallno;	/* syscall number or -1 */
	unsigned long		orig_gr8;	/* original syscall arg #1 */
	unsigned long		gner[2];
	unsigned long long	iacc[1];

	union {
		unsigned long	tbr;
		unsigned long	gr[64];
	};
};

struct user_fpmedia_regs
{
	/* FP/Media registers */
	unsigned long	fr[64];
	unsigned long	fner[2];
	unsigned long	msr[2];
	unsigned long	acc[8];
	unsigned char	accg[8];
	unsigned long	fsr[1];
};

struct user_context
{
	struct user_int_regs		i;
	struct user_fpmedia_regs	f;

	/* we provide a context extension so that we can save the regs for CPUs that
	 * implement many more of Fujitsu's lavish register spec
	 */
	void *extension;
} __attribute__((aligned(8)));

struct frv_frame0 {
	union {
		struct pt_regs		regs;
		struct user_context	uc;
	};

	struct frv_debug_regs		debug;

} __attribute__((aligned(32)));

#endif

#define __INT_GR(R)		__OFFSET(__INT_GR0,		(R))

#define __FPMEDIA_FR(R)		__OFFSET(__FPMEDIA_FR0,		(R))
#define __FPMEDIA_FNER(R)	__OFFSET(__FPMEDIA_FNER0,	(R))
#define __FPMEDIA_MSR(R)	__OFFSET(__FPMEDIA_MSR0,	(R))
#define __FPMEDIA_ACC(R)	__OFFSET(__FPMEDIA_ACC0,	(R))
#define __FPMEDIA_ACCG(R)	__OFFSETC(__FPMEDIA_ACCG0,	(R))
#define __FPMEDIA_FSR(R)	__OFFSET(__FPMEDIA_FSR0,	(R))

#define __THREAD_GR(R)		__OFFSET(__THREAD_GR16,		(R) - 16)

#endif /* _ASM_REGISTERS_H */
