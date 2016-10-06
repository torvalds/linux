/* processor.h: FRV processor definitions
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_PROCESSOR_H
#define _ASM_PROCESSOR_H

#include <asm/mem-layout.h>

#ifndef __ASSEMBLY__
/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <asm/sections.h>
#include <asm/segment.h>
#include <asm/fpu.h>
#include <asm/registers.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/cache.h>

/* Forward declaration, a strange C thing */
struct task_struct;

/*
 * Bus types
 */
#define EISA_bus 0

struct thread_struct {
	struct pt_regs		*frame;		/* [GR28] exception frame ptr for this thread */
	struct task_struct	*curr;		/* [GR29] current pointer for this thread */
	unsigned long		sp;		/* [GR1 ] kernel stack pointer */
	unsigned long		fp;		/* [GR2 ] kernel frame pointer */
	unsigned long		lr;		/* link register */
	unsigned long		pc;		/* program counter */
	unsigned long		gr[12];		/* [GR16-GR27] */
	unsigned long		sched_lr;	/* LR from schedule() */

	union {
		struct pt_regs		*frame0;	/* top (user) stack frame */
		struct user_context	*user;		/* userspace context */
	};
} __attribute__((aligned(8)));

extern struct pt_regs *__kernel_frame0_ptr;
extern struct task_struct *__kernel_current_task;

#endif

#ifndef __ASSEMBLY__
#define INIT_THREAD_FRAME0 \
	((struct pt_regs *) \
	(sizeof(init_stack) + (unsigned long) init_stack - sizeof(struct user_context)))

#define INIT_THREAD {				\
	NULL,					\
	(struct task_struct *) init_stack,	\
	0, 0, 0, 0,				\
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	\
	0,					\
	{ INIT_THREAD_FRAME0 },			\
}

/*
 * do necessary setup to start up a newly executed thread.
 */
#define start_thread(_regs, _pc, _usp)			\
do {							\
	_regs->pc	= (_pc);			\
	_regs->psr	&= ~PSR_S;			\
	_regs->sp	= (_usp);			\
} while(0)

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

extern asmlinkage void save_user_regs(struct user_context *target);
extern asmlinkage void *restore_user_regs(const struct user_context *target, ...);

#define copy_segments(tsk, mm)		do { } while (0)
#define release_segments(mm)		do { } while (0)
#define forget_segments()		do { } while (0)

/*
 * Return saved PC of a blocked thread.
 */
extern unsigned long thread_saved_pc(struct task_struct *tsk);

unsigned long get_wchan(struct task_struct *p);

#define	KSTK_EIP(tsk)	((tsk)->thread.frame0->pc)
#define	KSTK_ESP(tsk)	((tsk)->thread.frame0->sp)

#define cpu_relax() barrier()
#define cpu_relax_lowlatency() cpu_relax()

/* data cache prefetch */
#define ARCH_HAS_PREFETCH
static inline void prefetch(const void *x)
{
	asm volatile("dcpl %0,gr0,#0" : : "r"(x));
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_PROCESSOR_H */
