/*
 *  arch/arm/include/asm/mmu_context.h
 *
 *  Copyright (C) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#include <linux/compiler.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>
#include <asm/cachetype.h>
#include <asm/proc-fns.h>

void __check_kvm_seq(struct mm_struct *mm);

#ifdef CONFIG_CPU_HAS_ASID

/*
 * On ARMv6, we have the following structure in the Context ID:
 *
 * 31                         7          0
 * +-------------------------+-----------+
 * |      process ID         |   ASID    |
 * +-------------------------+-----------+
 * |              context ID             |
 * +-------------------------------------+
 *
 * The ASID is used to tag entries in the CPU caches and TLBs.
 * The context ID is used by debuggers and trace logic, and
 * should be unique within all running processes.
 */
#define ASID_BITS		8
#define ASID_MASK		((~0) << ASID_BITS)
#define ASID_FIRST_VERSION	(1 << ASID_BITS)

extern unsigned int cpu_last_asid;
#ifdef CONFIG_SMP
DECLARE_PER_CPU(struct mm_struct *, current_mm);
#endif

void __init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void __new_context(struct mm_struct *mm);

static inline void check_context(struct mm_struct *mm)
{
	/*
	 * This code is executed with interrupts enabled. Therefore,
	 * mm->context.id cannot be updated to the latest ASID version
	 * on a different CPU (and condition below not triggered)
	 * without first getting an IPI to reset the context. The
	 * alternative is to take a read_lock on mm->context.id_lock
	 * (after changing its type to rwlock_t).
	 */
	if (unlikely((mm->context.id ^ cpu_last_asid) >> ASID_BITS))
		__new_context(mm);

	if (unlikely(mm->context.kvm_seq != init_mm.context.kvm_seq))
		__check_kvm_seq(mm);
}

#define init_new_context(tsk,mm)	(__init_new_context(tsk,mm),0)

#else

static inline void check_context(struct mm_struct *mm)
{
#ifdef CONFIG_MMU
	if (unlikely(mm->context.kvm_seq != init_mm.context.kvm_seq))
		__check_kvm_seq(mm);
#endif
}

#define init_new_context(tsk,mm)	0

#endif

#define destroy_context(mm)		do { } while(0)

/*
 * This is called when "tsk" is about to enter lazy TLB mode.
 *
 * mm:  describes the currently active mm context
 * tsk: task which is entering lazy tlb
 * cpu: cpu number which is entering lazy tlb
 *
 * tsk->mm will be NULL
 */
static inline void
enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.  We avoid
 * calling the CPU specific function when the mm hasn't
 * actually changed.
 */
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
#ifdef CONFIG_MMU
	unsigned int cpu = smp_processor_id();

#ifdef CONFIG_SMP
	/* check for possible thread migration */
	if (!cpumask_empty(mm_cpumask(next)) &&
	    !cpumask_test_cpu(cpu, mm_cpumask(next)))
		__flush_icache_all();
#endif
	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) || prev != next) {
#ifdef CONFIG_SMP
		struct mm_struct **crt_mm = &per_cpu(current_mm, cpu);
		*crt_mm = next;
#endif
		check_context(next);
		cpu_switch_mm(next->pgd, next);
		if (cache_is_vivt())
			cpumask_clear_cpu(cpu, mm_cpumask(prev));
	}
#endif
}

#define deactivate_mm(tsk,mm)	do { } while (0)
#define activate_mm(prev,next)	switch_mm(prev, next, NULL)

/*
 * We are inserting a "fake" vma for the user-accessible vector page so
 * gdb and friends can get to it through ptrace and /proc/<pid>/mem.
 * But we also want to remove it before the generic code gets to see it
 * during process exit or the unmapping of it would  cause total havoc.
 * (the macro is used as remove_vma() is static to mm/mmap.c)
 */
#define arch_exit_mmap(mm) \
do { \
	struct vm_area_struct *high_vma = find_vma(mm, 0xffff0000); \
	if (high_vma) { \
		BUG_ON(high_vma->vm_next);  /* it should be last */ \
		if (high_vma->vm_prev) \
			high_vma->vm_prev->vm_next = NULL; \
		else \
			mm->mmap = NULL; \
		rb_erase(&high_vma->vm_rb, &mm->mm_rb); \
		mm->mmap_cache = NULL; \
		mm->map_count--; \
		remove_vma(high_vma); \
	} \
} while (0)

static inline void arch_dup_mmap(struct mm_struct *oldmm,
				 struct mm_struct *mm)
{
}

#endif
