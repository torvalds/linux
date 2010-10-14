/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_SYSTEM_H
#define _ASM_TILE_SYSTEM_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/irqflags.h>

/* NOTE: we can't include <linux/ptrace.h> due to #include dependencies. */
#include <asm/ptrace.h>

#include <arch/chip.h>
#include <arch/sim_def.h>
#include <arch/spr_def.h>

/*
 * read_barrier_depends - Flush all pending reads that subsequents reads
 * depend on.
 *
 * No data-dependent reads from memory-like regions are ever reordered
 * over this barrier.  All reads preceding this primitive are guaranteed
 * to access memory (but not necessarily other CPUs' caches) before any
 * reads following this primitive that depend on the data return by
 * any of the preceding reads.  This primitive is much lighter weight than
 * rmb() on most CPUs, and is never heavier weight than is
 * rmb().
 *
 * These ordering constraints are respected by both the local CPU
 * and the compiler.
 *
 * Ordering is not guaranteed by anything other than these primitives,
 * not even by data dependencies.  See the documentation for
 * memory_barrier() for examples and URLs to more information.
 *
 * For example, the following code would force ordering (the initial
 * value of "a" is zero, "b" is one, and "p" is "&a"):
 *
 * <programlisting>
 *	CPU 0				CPU 1
 *
 *	b = 2;
 *	memory_barrier();
 *	p = &b;				q = p;
 *					read_barrier_depends();
 *					d = *q;
 * </programlisting>
 *
 * because the read of "*q" depends on the read of "p" and these
 * two reads are separated by a read_barrier_depends().  However,
 * the following code, with the same initial values for "a" and "b":
 *
 * <programlisting>
 *	CPU 0				CPU 1
 *
 *	a = 2;
 *	memory_barrier();
 *	b = 3;				y = b;
 *					read_barrier_depends();
 *					x = a;
 * </programlisting>
 *
 * does not enforce ordering, since there is no data dependency between
 * the read of "a" and the read of "b".  Therefore, on some CPUs, such
 * as Alpha, "y" could be set to 3 and "x" to 0.  Use rmb()
 * in cases like this where there are no data dependencies.
 */

#define read_barrier_depends()	do { } while (0)

#define __sync()	__insn_mf()

#if CHIP_HAS_SPLIT_CYCLE()
#define get_cycles_low() __insn_mfspr(SPR_CYCLE_LOW)
#else
#define get_cycles_low() __insn_mfspr(SPR_CYCLE)   /* just get all 64 bits */
#endif

#if !CHIP_HAS_MF_WAITS_FOR_VICTIMS()
int __mb_incoherent(void);  /* Helper routine for mb_incoherent(). */
#endif

/* Fence to guarantee visibility of stores to incoherent memory. */
static inline void
mb_incoherent(void)
{
	__insn_mf();

#if !CHIP_HAS_MF_WAITS_FOR_VICTIMS()
	{
#if CHIP_HAS_TILE_WRITE_PENDING()
		const unsigned long WRITE_TIMEOUT_CYCLES = 400;
		unsigned long start = get_cycles_low();
		do {
			if (__insn_mfspr(SPR_TILE_WRITE_PENDING) == 0)
				return;
		} while ((get_cycles_low() - start) < WRITE_TIMEOUT_CYCLES);
#endif /* CHIP_HAS_TILE_WRITE_PENDING() */
		(void) __mb_incoherent();
	}
#endif /* CHIP_HAS_MF_WAITS_FOR_VICTIMS() */
}

#define fast_wmb()	__sync()
#define fast_rmb()	__sync()
#define fast_mb()	__sync()
#define fast_iob()	mb_incoherent()

#define wmb()		fast_wmb()
#define rmb()		fast_rmb()
#define mb()		fast_mb()
#define iob()		fast_iob()

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while (0)
#endif

#define set_mb(var, value) \
	do { var = value; mb(); } while (0)

/*
 * Pause the DMA engine and static network before task switching.
 */
#define prepare_arch_switch(next) _prepare_arch_switch(next)
void _prepare_arch_switch(struct task_struct *next);


/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * The number of callee-saved registers saved on the kernel stack
 * is defined here for use in copy_thread() and must agree with __switch_to().
 */
#endif /* !__ASSEMBLY__ */
#define CALLEE_SAVED_FIRST_REG 30
#define CALLEE_SAVED_REGS_COUNT 24   /* r30 to r52, plus an empty to align */
#ifndef __ASSEMBLY__
struct task_struct;
#define switch_to(prev, next, last) ((last) = _switch_to((prev), (next)))
extern struct task_struct *_switch_to(struct task_struct *prev,
				      struct task_struct *next);

/* Helper function for _switch_to(). */
extern struct task_struct *__switch_to(struct task_struct *prev,
				       struct task_struct *next,
				       unsigned long new_system_save_k_0);

/* Address that switched-away from tasks are at. */
extern unsigned long get_switch_to_pc(void);

/*
 * On SMP systems, when the scheduler does migration-cost autodetection,
 * it needs a way to flush as much of the CPU's caches as possible:
 *
 * TODO: fill this in!
 */
static inline void sched_cacheflush(void)
{
}

#define arch_align_stack(x) (x)

/*
 * Is the kernel doing fixups of unaligned accesses?  If <0, no kernel
 * intervention occurs and SIGBUS is delivered with no data address
 * info.  If 0, the kernel single-steps the instruction to discover
 * the data address to provide with the SIGBUS.  If 1, the kernel does
 * a fixup.
 */
extern int unaligned_fixup;

/* Is the kernel printing on each unaligned fixup? */
extern int unaligned_printk;

/* Number of unaligned fixups performed */
extern unsigned int unaligned_fixup_count;

/* Init-time routine to do tile-specific per-cpu setup. */
void setup_cpu(int boot);

/* User-level DMA management functions */
void grant_dma_mpls(void);
void restrict_dma_mpls(void);

#ifdef CONFIG_HARDWALL
/* User-level network management functions */
void reset_network_state(void);
void grant_network_mpls(void);
void restrict_network_mpls(void);
int hardwall_deactivate(struct task_struct *task);

/* Hook hardwall code into changes in affinity. */
#define arch_set_cpus_allowed(p, new_mask) do { \
	if (p->thread.hardwall && !cpumask_equal(&p->cpus_allowed, new_mask)) \
		hardwall_deactivate(p); \
} while (0)
#endif

/*
 * Kernel threads can check to see if they need to migrate their
 * stack whenever they return from a context switch; for user
 * threads, we defer until they are returning to user-space.
 */
#define finish_arch_switch(prev) do {                                     \
	if (unlikely((prev)->state == TASK_DEAD))                         \
		__insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_OS_EXIT |       \
			((prev)->pid << _SIM_CONTROL_OPERATOR_BITS));     \
	__insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_OS_SWITCH |             \
		(current->pid << _SIM_CONTROL_OPERATOR_BITS));            \
	if (current->mm == NULL && !kstack_hash &&                        \
	    current_thread_info()->homecache_cpu != smp_processor_id())   \
		homecache_migrate_kthread();                              \
} while (0)

/* Support function for forking a new task. */
void ret_from_fork(void);

/* Called from ret_from_fork() when a new process starts up. */
struct task_struct *sim_notify_fork(struct task_struct *prev);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_TILE_SYSTEM_H */
