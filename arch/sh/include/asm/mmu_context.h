/*
 * Copyright (C) 1999 Niibe Yutaka
 * Copyright (C) 2003 - 2007 Paul Mundt
 *
 * ASID handling idea taken from MIPS implementation.
 */
#ifndef __ASM_SH_MMU_CONTEXT_H
#define __ASM_SH_MMU_CONTEXT_H

#ifdef __KERNEL__
#include <cpu/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm-generic/mm_hooks.h>

/*
 * The MMU "context" consists of two things:
 *    (a) TLB cache version (or round, cycle whatever expression you like)
 *    (b) ASID (Address Space IDentifier)
 */
#ifdef CONFIG_CPU_HAS_PTEAEX
#define MMU_CONTEXT_ASID_MASK		0x0000ffff
#else
#define MMU_CONTEXT_ASID_MASK		0x000000ff
#endif

#define MMU_CONTEXT_VERSION_MASK	(~0UL & ~MMU_CONTEXT_ASID_MASK)
#define MMU_CONTEXT_FIRST_VERSION	(MMU_CONTEXT_ASID_MASK + 1)

/* Impossible ASID value, to differentiate from NO_CONTEXT. */
#define MMU_NO_ASID			MMU_CONTEXT_FIRST_VERSION
#define NO_CONTEXT			0UL

#define asid_cache(cpu)		(cpu_data[cpu].asid_cache)

#ifdef CONFIG_MMU
#define cpu_context(cpu, mm)	((mm)->context.id[cpu])

#define cpu_asid(cpu, mm)	\
	(cpu_context((cpu), (mm)) & MMU_CONTEXT_ASID_MASK)

/*
 * Virtual Page Number mask
 */
#define MMU_VPN_MASK	0xfffff000

#if defined(CONFIG_SUPERH32)
#include <asm/mmu_context_32.h>
#else
#include <asm/mmu_context_64.h>
#endif

/*
 * Get MMU context if needed.
 */
static inline void get_mmu_context(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long asid = asid_cache(cpu);

	/* Check if we have old version of context. */
	if (((cpu_context(cpu, mm) ^ asid) & MMU_CONTEXT_VERSION_MASK) == 0)
		/* It's up to date, do nothing */
		return;

	/* It's old, we need to get new context with new version. */
	if (!(++asid & MMU_CONTEXT_ASID_MASK)) {
		/*
		 * We exhaust ASID of this version.
		 * Flush all TLB and start new cycle.
		 */
		local_flush_tlb_all();

#ifdef CONFIG_SUPERH64
		/*
		 * The SH-5 cache uses the ASIDs, requiring both the I and D
		 * cache to be flushed when the ASID is exhausted. Weak.
		 */
		flush_cache_all();
#endif

		/*
		 * Fix version; Note that we avoid version #0
		 * to distingush NO_CONTEXT.
		 */
		if (!asid)
			asid = MMU_CONTEXT_FIRST_VERSION;
	}

	cpu_context(cpu, mm) = asid_cache(cpu) = asid;
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	int i;

	for (i = 0; i < num_online_cpus(); i++)
		cpu_context(i, mm) = NO_CONTEXT;

	return 0;
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_context(struct mm_struct *mm, unsigned int cpu)
{
	get_mmu_context(mm, cpu);
	set_asid(cpu_asid(cpu, mm));
}

static inline void switch_mm(struct mm_struct *prev,
			     struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();

	if (likely(prev != next)) {
		cpumask_set_cpu(cpu, mm_cpumask(next));
		set_TTB(next->pgd);
		activate_context(next, cpu);
	} else
		if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)))
			activate_context(next, cpu);
}

#define activate_mm(prev, next)		switch_mm((prev),(next),NULL)
#define deactivate_mm(tsk,mm)		do { } while (0)
#define enter_lazy_tlb(mm,tsk)		do { } while (0)

#else

#define set_asid(asid)			do { } while (0)
#define get_asid()			(0)
#define cpu_asid(cpu, mm)		({ (void)cpu; NO_CONTEXT; })
#define switch_and_save_asid(asid)	(0)
#define set_TTB(pgd)			do { } while (0)
#define get_TTB()			(0)

#include <asm-generic/mmu_context.h>

#endif /* CONFIG_MMU */

#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4)
/*
 * If this processor has an MMU, we need methods to turn it off/on ..
 * paging_init() will also have to be updated for the processor in
 * question.
 */
static inline void enable_mmu(void)
{
	unsigned int cpu = smp_processor_id();

	/* Enable MMU */
	__raw_writel(MMU_CONTROL_INIT, MMUCR);
	ctrl_barrier();

	if (asid_cache(cpu) == NO_CONTEXT)
		asid_cache(cpu) = MMU_CONTEXT_FIRST_VERSION;

	set_asid(asid_cache(cpu) & MMU_CONTEXT_ASID_MASK);
}

static inline void disable_mmu(void)
{
	unsigned long cr;

	cr = __raw_readl(MMUCR);
	cr &= ~MMU_CONTROL_INIT;
	__raw_writel(cr, MMUCR);

	ctrl_barrier();
}
#else
/*
 * MMU control handlers for processors lacking memory
 * management hardware.
 */
#define enable_mmu()	do { } while (0)
#define disable_mmu()	do { } while (0)
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_SH_MMU_CONTEXT_H */
