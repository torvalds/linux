/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_MMU_CONTEXT_H
#define __ASM_CSKY_MMU_CONTEXT_H

#include <asm-generic/mm_hooks.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <abi/ckmmu.h>

#define TLBMISS_HANDLER_SETUP_PGD(pgd) \
	setup_pgd(__pa(pgd), false)

#define TLBMISS_HANDLER_SETUP_PGD_KERNEL(pgd) \
	setup_pgd(__pa(pgd), true)

#define ASID_MASK		((1 << CONFIG_CPU_ASID_BITS) - 1)
#define cpu_asid(mm)		(atomic64_read(&mm->context.asid) & ASID_MASK)

#define init_new_context(tsk,mm)	({ atomic64_set(&(mm)->context.asid, 0); 0; })
#define activate_mm(prev,next)		switch_mm(prev, next, current)

#define destroy_context(mm)		do {} while (0)
#define enter_lazy_tlb(mm, tsk)		do {} while (0)
#define deactivate_mm(tsk, mm)		do {} while (0)

void check_and_switch_context(struct mm_struct *mm, unsigned int cpu);

static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();

	if (prev != next)
		check_and_switch_context(next, cpu);

	TLBMISS_HANDLER_SETUP_PGD(next->pgd);
	write_mmu_entryhi(next->context.asid.counter);
}
#endif /* __ASM_CSKY_MMU_CONTEXT_H */
