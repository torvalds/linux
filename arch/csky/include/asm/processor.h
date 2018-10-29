/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_PROCESSOR_H
#define __ASM_CSKY_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l; })

#include <linux/bitops.h>
#include <asm/segment.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/cache.h>
#include <abi/reg_ops.h>
#include <abi/regdef.h>
#ifdef CONFIG_CPU_HAS_FPU
#include <abi/fpu.h>
#endif

struct cpuinfo_csky {
	unsigned long udelay_val;
	unsigned long asid_cache;
	/*
	 * Capability and feature descriptor structure for CSKY CPU
	 */
	unsigned long options;
	unsigned int processor_id[4];
	unsigned int fpu_id;
} __aligned(SMP_CACHE_BYTES);

extern struct cpuinfo_csky cpu_data[];

/*
 * User space process size: 2GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.  TASK_SIZE
 * for a 64 bit kernel expandable to 8192EB, of which the current CSKY
 * implementations will "only" be able to use 1TB ...
 */
#define TASK_SIZE       0x7fff8000UL

#ifdef __KERNEL__
#define STACK_TOP       TASK_SIZE
#define STACK_TOP_MAX   STACK_TOP
#endif

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE      (TASK_SIZE / 3)

struct thread_struct {
	unsigned long  ksp;       /* kernel stack pointer */
	unsigned long  sr;        /* saved status register */
	unsigned long  esp0;      /* points to SR of stack frame */
	unsigned long  hi;
	unsigned long  lo;

	/* Other stuff associated with the thread. */
	unsigned long address;      /* Last user fault */
	unsigned long error_code;

	/* FPU regs */
	struct user_fp __aligned(16) user_fp;
};

#define INIT_THREAD  { \
	.ksp = (unsigned long) init_thread_union.stack + THREAD_SIZE, \
	.sr = DEFAULT_PSR_VALUE, \
}

/*
 * Do necessary setup to start up a newly executed thread.
 *
 * pass the data segment into user programs if it exists,
 * it can't hurt anything as far as I can tell
 */
#define start_thread(_regs, _pc, _usp)					\
do {									\
	set_fs(USER_DS); /* reads from user space */			\
	(_regs)->pc = (_pc);						\
	(_regs)->regs[1] = 0; /* ABIV1 is R7, uClibc_main rtdl arg */	\
	(_regs)->regs[2] = 0;						\
	(_regs)->regs[3] = 0; /* ABIV2 is R7, use it? */		\
	(_regs)->sr &= ~PS_S;						\
	(_regs)->usp = (_usp);						\
} while (0)

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)    do { } while (0)

extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

#define copy_segments(tsk, mm)		do { } while (0)
#define release_segments(mm)		do { } while (0)
#define forget_segments()		do { } while (0)

extern unsigned long thread_saved_pc(struct task_struct *tsk);

unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)		(task_pt_regs(tsk)->pc)
#define KSTK_ESP(tsk)		(task_pt_regs(tsk)->usp)

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE + p->stack) - 1)

#define cpu_relax() barrier()

#endif /* __ASM_CSKY_PROCESSOR_H */
