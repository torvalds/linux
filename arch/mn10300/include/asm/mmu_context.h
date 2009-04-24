/* MN10300 MMU context management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Modified by David Howells (dhowells@redhat.com)
 * - Derived from include/asm-m32r/mmu_context.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 *
 *
 * This implements an algorithm to provide TLB PID mappings to provide
 * selective access to the TLB for processes, thus reducing the number of TLB
 * flushes required.
 *
 * Note, however, that the M32R algorithm is technically broken as it does not
 * handle version wrap-around, and could, theoretically, have a problem with a
 * very long lived program that sleeps long enough for the version number to
 * wrap all the way around so that its TLB mappings appear valid once again.
 */
#ifndef _ASM_MMU_CONTEXT_H
#define _ASM_MMU_CONTEXT_H

#include <asm/atomic.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm-generic/mm_hooks.h>

#define MMU_CONTEXT_TLBPID_MASK		0x000000ffUL
#define MMU_CONTEXT_VERSION_MASK	0xffffff00UL
#define MMU_CONTEXT_FIRST_VERSION	0x00000100UL
#define MMU_NO_CONTEXT			0x00000000UL

extern unsigned long mmu_context_cache[NR_CPUS];
#define mm_context(mm)	(mm->context.tlbpid[smp_processor_id()])

#define enter_lazy_tlb(mm, tsk)	do {} while (0)

#ifdef CONFIG_SMP
#define cpu_ran_vm(cpu, task) \
	cpu_set((cpu), (task)->cpu_vm_mask)
#define cpu_maybe_ran_vm(cpu, task) \
	cpu_test_and_set((cpu), (task)->cpu_vm_mask)
#else
#define cpu_ran_vm(cpu, task)		do {} while (0)
#define cpu_maybe_ran_vm(cpu, task)	true
#endif /* CONFIG_SMP */

/*
 * allocate an MMU context
 */
static inline unsigned long allocate_mmu_context(struct mm_struct *mm)
{
	unsigned long *pmc = &mmu_context_cache[smp_processor_id()];
	unsigned long mc = ++(*pmc);

	if (!(mc & MMU_CONTEXT_TLBPID_MASK)) {
		/* we exhausted the TLB PIDs of this version on this CPU, so we
		 * flush this CPU's TLB in its entirety and start new cycle */
		flush_tlb_all();

		/* fix the TLB version if needed (we avoid version #0 so as to
		 * distingush MMU_NO_CONTEXT) */
		if (!mc)
			*pmc = mc = MMU_CONTEXT_FIRST_VERSION;
	}
	mm_context(mm) = mc;
	return mc;
}

/*
 * get an MMU context if one is needed
 */
static inline unsigned long get_mmu_context(struct mm_struct *mm)
{
	unsigned long mc = MMU_NO_CONTEXT, cache;

	if (mm) {
		cache = mmu_context_cache[smp_processor_id()];
		mc = mm_context(mm);

		/* if we have an old version of the context, replace it */
		if ((mc ^ cache) & MMU_CONTEXT_VERSION_MASK)
			mc = allocate_mmu_context(mm);
	}
	return mc;
}

/*
 * initialise the context related info for a new mm_struct instance
 */
static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	int num_cpus = NR_CPUS, i;

	for (i = 0; i < num_cpus; i++)
		mm->context.tlbpid[i] = MMU_NO_CONTEXT;
	return 0;
}

/*
 * destroy context related info for an mm_struct that is about to be put to
 * rest
 */
#define destroy_context(mm)	do { } while (0)

/*
 * after we have set current->mm to a new value, this activates the context for
 * the new mm so we see the new mappings.
 */
static inline void activate_context(struct mm_struct *mm, int cpu)
{
	PIDR = get_mmu_context(mm) & MMU_CONTEXT_TLBPID_MASK;
}

/*
 * change between virtual memory sets
 */
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	int cpu = smp_processor_id();

	if (prev != next) {
		cpu_ran_vm(cpu, next);
		activate_context(next, cpu);
		PTBR = (unsigned long) next->pgd;
	} else if (!cpu_maybe_ran_vm(cpu, next)) {
		activate_context(next, cpu);
	}
}

#define deactivate_mm(tsk, mm)	do {} while (0)
#define activate_mm(prev, next)	switch_mm((prev), (next), NULL)

#endif /* _ASM_MMU_CONTEXT_H */
