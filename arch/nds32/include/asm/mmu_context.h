/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASM_NDS32_MMU_CONTEXT_H
#define __ASM_NDS32_MMU_CONTEXT_H

#include <linux/spinlock.h>
#include <asm/tlbflush.h>
#include <asm/proc-fns.h>
#include <asm-generic/mm_hooks.h>

#define init_new_context init_new_context
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context.id = 0;
	return 0;
}

#define CID_BITS	9
extern spinlock_t cid_lock;
extern unsigned int cpu_last_cid;

static inline void __new_context(struct mm_struct *mm)
{
	unsigned int cid;
	unsigned long flags;

	spin_lock_irqsave(&cid_lock, flags);
	cid = cpu_last_cid;
	cpu_last_cid += 1 << TLB_MISC_offCID;
	if (cpu_last_cid == 0)
		cpu_last_cid = 1 << TLB_MISC_offCID << CID_BITS;

	if ((cid & TLB_MISC_mskCID) == 0)
		flush_tlb_all();
	spin_unlock_irqrestore(&cid_lock, flags);

	mm->context.id = cid;
}

static inline void check_context(struct mm_struct *mm)
{
	if (unlikely
	    ((mm->context.id ^ cpu_last_cid) >> TLB_MISC_offCID >> CID_BITS))
		__new_context(mm);
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();

	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) || prev != next) {
		check_context(next);
		cpu_switch_mm(next);
	}
}

#include <asm-generic/mmu_context.h>

#endif
