/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_PROCESSOR_H
#define __ASM_CSKY_PROCESSOR_H

#include <linux/bitops.h>
#include <linux/cache.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <abi/reg_ops.h>
#include <abi/regdef.h>
#include <abi/switch_context.h>
#ifdef CONFIG_CPU_HAS_FPU
#include <abi/fpu.h>
#endif

struct cpuinfo_csky {
	unsigned long asid_cache;
} __aligned(SMP_CACHE_BYTES);

extern struct cpuinfo_csky cpu_data[];

/*
 * User space process size: 2GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.  TASK_SIZE
 * for a 64 bit kernel expandable to 8192EB, of which the current CSKY
 * implementations will "only" be able to use 1TB ...
 */
#define TASK_SIZE	(PAGE_OFFSET - (PAGE_SIZE * 8))

#ifdef __KERNEL__
#define STACK_TOP       TASK_SIZE
#define STACK_TOP_MAX   STACK_TOP
#endif

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE      (TASK_SIZE / 3)

struct thread_struct {
	unsigned long  sp;        /* kernel stack pointer */
	unsigned long  trap_no;   /* saved status register */

	/* FPU regs */
	struct user_fp __aligned(16) user_fp;
};

#define INIT_THREAD  { \
	.sp = sizeof(init_stack) + (unsigned long) &init_stack, \
}

/*
 * Do necessary setup to start up a newly executed thread.
 *
 * pass the data segment into user programs if it exists,
 * it can't hurt anything as far as I can tell
 */
#define start_thread(_regs, _pc, _usp)					\
do {									\
	(_regs)->pc = (_pc);						\
	(_regs)->regs[1] = 0; /* ABIV1 is R7, uClibc_main rtdl arg */	\
	(_regs)->regs[2] = 0;						\
	(_regs)->regs[3] = 0; /* ABIV2 is R7, use it? */		\
	(_regs)->sr &= ~PS_S;						\
	(_regs)->usp = (_usp);						\
} while (0)

/* Forward declaration, a strange C thing */
struct task_struct;

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)    do { } while (0)

unsigned long __get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)		(task_pt_regs(tsk)->pc)
#define KSTK_ESP(tsk)		(task_pt_regs(tsk)->usp)

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE + task_stack_page(p)) - 1)

#define cpu_relax() barrier()

register unsigned long current_stack_pointer __asm__("sp");

#endif /* __ASM_CSKY_PROCESSOR_H */
