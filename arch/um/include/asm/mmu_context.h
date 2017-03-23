/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __UM_MMU_CONTEXT_H
#define __UM_MMU_CONTEXT_H

#include <linux/sched.h>
#include <linux/mm_types.h>

#include <asm/mmu.h>

extern void uml_setup_stubs(struct mm_struct *mm);
/*
 * Needed since we do not use the asm-generic/mm_hooks.h:
 */
static inline void arch_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
{
	uml_setup_stubs(mm);
}
extern void arch_exit_mmap(struct mm_struct *mm);
static inline void arch_unmap(struct mm_struct *mm,
			struct vm_area_struct *vma,
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

static inline bool arch_pte_access_permitted(pte_t pte, bool write)
{
	/* by default, allow everything */
	return true;
}

/*
 * end asm-generic/mm_hooks.h functions
 */

#define deactivate_mm(tsk,mm)	do { } while (0)

extern void force_flush_all(void);

static inline void activate_mm(struct mm_struct *old, struct mm_struct *new)
{
	/*
	 * This is called by fs/exec.c and sys_unshare()
	 * when the new ->mm is used for the first time.
	 */
	__switch_mm(&new->context.id);
	down_write(&new->mmap_sem);
	uml_setup_stubs(new);
	up_write(&new->mmap_sem);
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();

	if(prev != next){
		cpumask_clear_cpu(cpu, mm_cpumask(prev));
		cpumask_set_cpu(cpu, mm_cpumask(next));
		if(next != &init_mm)
			__switch_mm(&next->context.id);
	}
}

static inline void enter_lazy_tlb(struct mm_struct *mm, 
				  struct task_struct *tsk)
{
}

extern int init_new_context(struct task_struct *task, struct mm_struct *mm);

extern void destroy_context(struct mm_struct *mm);

#endif
