/* MN10300 Exception frame layout and ptrace constants
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

#define PT_A3		0
#define PT_A2		1
#define PT_D3		2
#define	PT_D2		3
#define PT_MCVF		4
#define	PT_MCRL		5
#define PT_MCRH		6
#define	PT_MDRQ		7
#define	PT_E1		8
#define	PT_E0		9
#define	PT_E7		10
#define	PT_E6		11
#define	PT_E5		12
#define	PT_E4		13
#define	PT_E3		14
#define	PT_E2		15
#define	PT_SP		16
#define	PT_LAR		17
#define	PT_LIR		18
#define	PT_MDR		19
#define	PT_A1		20
#define	PT_A0		21
#define	PT_D1		22
#define	PT_D0		23
#define PT_ORIG_D0	24
#define	PT_EPSW		25
#define	PT_PC		26
#define NR_PTREGS	27

#ifndef __ASSEMBLY__
/*
 * This defines the way registers are stored in the event of an exception
 * - the strange order is due to the MOVM instruction
 */
struct pt_regs {
	unsigned long		a3;		/* syscall arg 3 */
	unsigned long		a2;		/* syscall arg 4 */
	unsigned long		d3;		/* syscall arg 5 */
	unsigned long		d2;		/* syscall arg 6 */
	unsigned long		mcvf;
	unsigned long		mcrl;
	unsigned long		mcrh;
	unsigned long		mdrq;
	unsigned long		e1;
	unsigned long		e0;
	unsigned long		e7;
	unsigned long		e6;
	unsigned long		e5;
	unsigned long		e4;
	unsigned long		e3;
	unsigned long		e2;
	unsigned long		sp;
	unsigned long		lar;
	unsigned long		lir;
	unsigned long		mdr;
	unsigned long		a1;
	unsigned long		a0;		/* syscall arg 1 */
	unsigned long		d1;		/* syscall arg 2 */
	unsigned long		d0;		/* syscall ret */
	struct pt_regs		*next;		/* next frame pointer */
	unsigned long		orig_d0;	/* syscall number */
	unsigned long		epsw;
	unsigned long		pc;
};
#endif

extern struct pt_regs *__frame; /* current frame pointer */

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13
#define PTRACE_GETFPREGS          14
#define PTRACE_SETFPREGS          15

/* options set using PTRACE_SETOPTIONS */
#define PTRACE_O_TRACESYSGOOD     0x00000001

#if defined(__KERNEL__)

#if !defined(__ASSEMBLY__)
#define user_mode(regs)			(((regs)->epsw & EPSW_nSL) == EPSW_nSL)
#define instruction_pointer(regs)	((regs)->pc)
extern void show_regs(struct pt_regs *);
#endif  /*  !__ASSEMBLY  */

#define profile_pc(regs) ((regs)->pc)

#endif  /*  __KERNEL__  */

#endif /* _ASM_PTRACE_H */
