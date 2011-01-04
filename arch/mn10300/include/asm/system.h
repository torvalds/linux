/* MN10300 System definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

#include <asm/cpu-regs.h>
#include <asm/intctl-regs.h>

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/kernel.h>
#include <linux/irqflags.h>
#include <asm/atomic.h>

#if !defined(CONFIG_LAZY_SAVE_FPU)
struct fpu_state_struct;
extern asmlinkage void fpu_save(struct fpu_state_struct *);
#define switch_fpu(prev, next)						\
	do {								\
		if ((prev)->thread.fpu_flags & THREAD_HAS_FPU) {	\
			(prev)->thread.fpu_flags &= ~THREAD_HAS_FPU;	\
			(prev)->thread.uregs->epsw &= ~EPSW_FE;		\
			fpu_save(&(prev)->thread.fpu_state);		\
		}							\
	} while (0)
#else
#define switch_fpu(prev, next) do {} while (0)
#endif

struct task_struct;
struct thread_struct;

extern asmlinkage
struct task_struct *__switch_to(struct thread_struct *prev,
				struct thread_struct *next,
				struct task_struct *prev_task);

/* context switching is now performed out-of-line in switch_to.S */
#define switch_to(prev, next, last)					\
do {									\
	switch_fpu(prev, next);						\
	current->thread.wchan = (u_long) __builtin_return_address(0);	\
	(last) = __switch_to(&(prev)->thread, &(next)->thread, (prev));	\
	mb();								\
	current->thread.wchan = 0;					\
} while (0)

#define arch_align_stack(x) (x)

#define nop() asm volatile ("nop")

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 *
 * For now, "wmb()" doesn't actually do anything, as all
 * Intel CPU's follow what Intel calls a *Processor Order*,
 * in which all writes are seen in the program order even
 * outside the CPU.
 *
 * I expect future Intel CPU's to have a weaker ordering,
 * but I'd also expect them to finally get their act together
 * and add some real memory barriers if so.
 *
 * Some non intel clones support out of order store. wmb() ceases to be a
 * nop for these.
 */

#define mb()	asm volatile ("": : :"memory")
#define rmb()	mb()
#define wmb()	asm volatile ("": : :"memory")

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define set_mb(var, value)  do { xchg(&var, value); } while (0)
#else  /* CONFIG_SMP */
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define set_mb(var, value)  do { var = value;  mb(); } while (0)
#endif /* CONFIG_SMP */

#define set_wmb(var, value) do { var = value; wmb(); } while (0)

#define read_barrier_depends()		do {} while (0)
#define smp_read_barrier_depends()	do {} while (0)

#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_SYSTEM_H */
