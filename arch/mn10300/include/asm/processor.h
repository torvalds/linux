/* MN10300 Processor specifics
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_PROCESSOR_H
#define _ASM_PROCESSOR_H

#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/cpu-regs.h>
#include <linux/threads.h>

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr()			\
({						\
	void *__pc;				\
	asm("mov pc,%0" : "=a"(__pc));		\
	__pc;					\
})

extern void show_registers(struct pt_regs *regs);

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head.S, so think twice
 *  before touching them. [mj]
 */

struct mn10300_cpuinfo {
	int		type;
	unsigned long	loops_per_sec;
	char		hard_math;
	unsigned long	*pgd_quick;
	unsigned long	*pte_quick;
	unsigned long	pgtable_cache_sz;
};

extern struct mn10300_cpuinfo boot_cpu_data;

#define cpu_data &boot_cpu_data
#define current_cpu_data boot_cpu_data

extern void identify_cpu(struct mn10300_cpuinfo *);
extern void print_cpu_info(struct mn10300_cpuinfo *);
extern void dodgy_tsc(void);
#define cpu_relax() barrier()

/*
 * User space process size: 1.75GB (default).
 */
#define TASK_SIZE		0x70000000

/*
 * Where to put the userspace stack by default
 */
#define STACK_TOP		0x70000000
#define STACK_TOP_MAX		STACK_TOP

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	0x30000000

typedef struct {
	unsigned long	seg;
} mm_segment_t;

struct fpu_state_struct {
	unsigned long	fs[32];		/* fpu registers */
	unsigned long	fpcr;		/* fpu control register */
};

struct thread_struct {
	struct pt_regs		*uregs;		/* userspace register frame */
	unsigned long		pc;		/* kernel PC */
	unsigned long		sp;		/* kernel SP */
	unsigned long		a3;		/* kernel FP */
	unsigned long		wchan;
	unsigned long		usp;
	struct pt_regs		*__frame;
	unsigned long		fpu_flags;
#define THREAD_USING_FPU	0x00000001	/* T if this task is using the FPU */
	struct fpu_state_struct	fpu_state;
};

#define INIT_THREAD				\
{						\
	.uregs		= init_uregs,		\
	.pc		= 0,			\
	.sp		= 0,			\
	.a3		= 0,			\
	.wchan		= 0,			\
	.__frame	= NULL,			\
}

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, \
  NULL, NULL }

/*
 * do necessary setup to start up a newly executed thread
 * - need to discard the frame stacked by the kernel thread invoking the execve
 *   syscall (see RESTORE_ALL macro)
 */
#define start_thread(regs, new_pc, new_sp) do {		\
	set_fs(USER_DS);				\
	__frame = current->thread.uregs;		\
	__frame->epsw = EPSW_nSL | EPSW_IE | EPSW_IM;	\
	__frame->pc = new_pc;				\
	__frame->sp = new_sp;				\
} while (0)

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
extern void prepare_to_copy(struct task_struct *tsk);

/*
 * create a kernel thread without removing it from tasklists
 */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

/*
 * Return saved PC of a blocked thread.
 */
extern unsigned long thread_saved_pc(struct task_struct *tsk);

unsigned long get_wchan(struct task_struct *p);

#define task_pt_regs(task) ((task)->thread.uregs)
#define KSTK_EIP(task) (task_pt_regs(task)->pc)
#define KSTK_ESP(task) (task_pt_regs(task)->sp)

#define KSTK_TOP(info)				\
({						\
	(unsigned long)(info) + THREAD_SIZE;	\
})

#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW

static inline void prefetch(const void *x)
{
#ifndef CONFIG_MN10300_CACHE_DISABLED
#ifdef CONFIG_MN10300_PROC_MN103E010
	asm volatile ("nop; nop; dcpf (%0)" : : "r"(x));
#else
	asm volatile ("dcpf (%0)" : : "r"(x));
#endif
#endif
}

static inline void prefetchw(const void *x)
{
#ifndef CONFIG_MN10300_CACHE_DISABLED
#ifdef CONFIG_MN10300_PROC_MN103E010
	asm volatile ("nop; nop; dcpf (%0)" : : "r"(x));
#else
	asm volatile ("dcpf (%0)" : : "r"(x));
#endif
#endif
}

#endif /* _ASM_PROCESSOR_H */
