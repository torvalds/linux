/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/mmu_context.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_MMU_CONTEXT_H__
#define __UNICORE_MMU_CONTEXT_H__

#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/cpu-single.h>

#define init_new_context(tsk, mm)	0

#define destroy_context(mm)		do { } while (0)

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
	unsigned int cpu = smp_processor_id();

	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) || prev != next)
		cpu_switch_mm(next->pgd, next);
}

#define deactivate_mm(tsk, mm)	do { } while (0)
#define activate_mm(prev, next)	switch_mm(prev, next, NULL)

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
		vmacache_invalidate(mm); \
		mm->map_count--; \
		remove_vma(high_vma); \
	} \
} while (0)

static inline int arch_dup_mmap(struct mm_struct *oldmm,
				struct mm_struct *mm)
{
	return 0;
}

static inline void arch_unmap(struct mm_struct *mm,
			unsigned long start, unsigned long end)
{
}

static inline void arch_bprm_mm_init(struct mm_struct *mm,
				     struct vm_area_struct *vma)
{
}

static inline bool arch_vma_access_permitted(struct vm_area_struct *vma,
		bool write, bool execute, bool foreign)
{
	/* by default, allow everything */
	return true;
}
#endif
