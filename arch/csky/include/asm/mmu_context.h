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

static inline void tlbmiss_handler_setup_pgd(unsigned long pgd, bool kernel)
{
	pgd -= PAGE_OFFSET;
	pgd += phys_offset;
	pgd |= 1;
	setup_pgd(pgd, kernel);
}

#define TLBMISS_HANDLER_SETUP_PGD(pgd) \
	tlbmiss_handler_setup_pgd((unsigned long)pgd, 0)
#define TLBMISS_HANDLER_SETUP_PGD_KERNEL(pgd) \
	tlbmiss_handler_setup_pgd((unsigned long)pgd, 1)

static inline unsigned long tlb_get_pgd(void)
{
	return ((get_pgd() - phys_offset) & ~1) + PAGE_OFFSET;
}

#define cpu_context(cpu, mm)	((mm)->context.asid[cpu])
#define cpu_asid(cpu, mm)	(cpu_context((cpu), (mm)) & ASID_MASK)
#define asid_cache(cpu)		(cpu_data[cpu].asid_cache)

#define ASID_FIRST_VERSION	(1 << CONFIG_CPU_ASID_BITS)
#define ASID_INC		0x1
#define ASID_MASK		(ASID_FIRST_VERSION - 1)
#define ASID_VERSION_MASK	~ASID_MASK

#define destroy_context(mm)		do {} while (0)
#define enter_lazy_tlb(mm, tsk)		do {} while (0)
#define deactivate_mm(tsk, mm)		do {} while (0)

/*
 *  All unused by hardware upper bits will be considered
 *  as a software asid extension.
 */
static inline void
get_new_mmu_context(struct mm_struct *mm, unsigned long cpu)
{
	unsigned long asid = asid_cache(cpu);

	asid += ASID_INC;
	if (!(asid & ASID_MASK)) {
		flush_tlb_all();	/* start new asid cycle */
		if (!asid)		/* fix version if needed */
			asid = ASID_FIRST_VERSION;
	}
	cpu_context(cpu, mm) = asid_cache(cpu) = asid;
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	int i;

	for_each_online_cpu(i)
		cpu_context(i, mm) = 0;
	return 0;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();
	unsigned long flags;

	local_irq_save(flags);
	/* Check if our ASID is of an older version and thus invalid */
	if ((cpu_context(cpu, next) ^ asid_cache(cpu)) & ASID_VERSION_MASK)
		get_new_mmu_context(next, cpu);
	write_mmu_entryhi(cpu_asid(cpu, next));
	TLBMISS_HANDLER_SETUP_PGD(next->pgd);

	/*
	 * Mark current->active_mm as not "active" anymore.
	 * We don't want to mislead possible IPI tlb flush routines.
	 */
	cpumask_clear_cpu(cpu, mm_cpumask(prev));
	cpumask_set_cpu(cpu, mm_cpumask(next));

	local_irq_restore(flags);
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void
activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned long flags;
	int cpu = smp_processor_id();

	local_irq_save(flags);

	/* Unconditionally get a new ASID.  */
	get_new_mmu_context(next, cpu);

	write_mmu_entryhi(cpu_asid(cpu, next));
	TLBMISS_HANDLER_SETUP_PGD(next->pgd);

	/* mark mmu ownership change */
	cpumask_clear_cpu(cpu, mm_cpumask(prev));
	cpumask_set_cpu(cpu, mm_cpumask(next));

	local_irq_restore(flags);
}

/*
 * If mm is currently active_mm, we can't really drop it. Instead,
 * we will get a new one for it.
 */
static inline void
drop_mmu_context(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long flags;

	local_irq_save(flags);

	if (cpumask_test_cpu(cpu, mm_cpumask(mm)))  {
		get_new_mmu_context(mm, cpu);
		write_mmu_entryhi(cpu_asid(cpu, mm));
	} else {
		/* will get a new context next time */
		cpu_context(cpu, mm) = 0;
	}

	local_irq_restore(flags);
}

#endif /* __ASM_CSKY_MMU_CONTEXT_H */
