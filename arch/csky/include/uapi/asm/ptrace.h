/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef _CSKY_PTRACE_H
#define _CSKY_PTRACE_H

#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long	tls;
	unsigned long	lr;
	unsigned long	pc;
	unsigned long	sr;
	unsigned long	usp;

	/*
	 * a0, a1, a2, a3:
	 * abiv1: r2, r3, r4, r5
	 * abiv2: r0, r1, r2, r3
	 */
	unsigned long	orig_a0;
	unsigned long	a0;
	unsigned long	a1;
	unsigned long	a2;
	unsigned long	a3;

	/*
	 * ABIV2: r4 ~ r13
	 * ABIV1: r6 ~ r14, r1
	 */
	unsigned long	regs[10];

#if defined(__CSKYABIV2__)
	/* r16 ~ r30 */
	unsigned long	exregs[15];

	unsigned long	rhi;
	unsigned long	rlo;
	unsigned long	dcsr;
#endif
};

struct user_fp {
	unsigned long	vr[96];
	unsigned long	fcr;
	unsigned long	fesr;
	unsigned long	fid;
	unsigned long	reserved;
};

#ifdef __KERNEL__

#define PS_S	0x80000000 /* Supervisor Mode */

#define arch_has_single_step() (1)
#define current_pt_regs() \
({ (struct pt_regs *)((char *)current_thread_info() + THREAD_SIZE) - 1; })

#define user_stack_pointer(regs) ((regs)->usp)

#define user_mode(regs) (!((regs)->sr & PS_S))
#define instruction_pointer(regs) ((regs)->pc)
#define profile_pc(regs) instruction_pointer(regs)

#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */
#endif /* _CSKY_PTRACE_H */
