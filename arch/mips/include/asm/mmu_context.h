/*
 * Switch a MMU context.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_MMU_CONTEXT_H
#define _ASM_MMU_CONTEXT_H

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/dsemul.h>
#include <asm/hazards.h>
#include <asm/tlbflush.h>
#include <asm-generic/mm_hooks.h>

#define htw_set_pwbase(pgd)						\
do {									\
	if (cpu_has_htw) {						\
		write_c0_pwbase(pgd);					\
		back_to_back_c0_hazard();				\
	}								\
} while (0)

#define TLBMISS_HANDLER_SETUP_PGD(pgd)					\
do {									\
	extern void tlbmiss_handler_setup_pgd(unsigned long);		\
	tlbmiss_handler_setup_pgd((unsigned long)(pgd));		\
	htw_set_pwbase((unsigned long)pgd);				\
} while (0)

#ifdef CONFIG_MIPS_PGD_C0_CONTEXT

#define TLBMISS_HANDLER_RESTORE()					\
	write_c0_xcontext((unsigned long) smp_processor_id() <<		\
			  SMP_CPUID_REGSHIFT)

#define TLBMISS_HANDLER_SETUP()						\
	do {								\
		TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir);		\
		TLBMISS_HANDLER_RESTORE();				\
	} while (0)

#else /* !CONFIG_MIPS_PGD_C0_CONTEXT: using  pgd_current*/

/*
 * For the fast tlb miss handlers, we keep a per cpu array of pointers
 * to the current pgd for each processor. Also, the proc. id is stuffed
 * into the context register.
 */
extern unsigned long pgd_current[];

#define TLBMISS_HANDLER_RESTORE()					\
	write_c0_context((unsigned long) smp_processor_id() <<		\
			 SMP_CPUID_REGSHIFT)

#define TLBMISS_HANDLER_SETUP()						\
	TLBMISS_HANDLER_RESTORE();					\
	back_to_back_c0_hazard();					\
	TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir)
#endif /* CONFIG_MIPS_PGD_C0_CONTEXT*/
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

#define ASID_INC	0x40
#define ASID_MASK	0xfc0

#elif defined(CONFIG_CPU_R8000)

#define ASID_INC	0x10
#define ASID_MASK	0xff0

#else /* FIXME: not correct for R6000 */

#define ASID_INC	0x1
#define ASID_MASK	0xff

#endif

#define cpu_context(cpu, mm)	((mm)->context.asid[cpu])
#define cpu_asid(cpu, mm)	(cpu_context((cpu), (mm)) & ASID_MASK)
#define asid_cache(cpu)		(cpu_data[cpu].asid_cache)

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 *  All unused by hardware upper bits will be considered
 *  as a software asid extension.
 */
#define ASID_VERSION_MASK  ((unsigned long)~(ASID_MASK|(ASID_MASK-1)))
#define ASID_FIRST_VERSION ((unsigned long)(~ASID_VERSION_MASK) + 1)

/* Normal, classic MIPS get_new_mmu_context */
static inline void
get_new_mmu_context(struct mm_struct *mm, unsigned long cpu)
{
	extern void kvm_local_flush_tlb_all(void);
	unsigned long asid = asid_cache(cpu);

	if (! ((asid += ASID_INC) & ASID_MASK) ) {
		if (cpu_has_vtag_icache)
			flush_icache_all();
#ifdef CONFIG_KVM
		kvm_local_flush_tlb_all();      /* start new asid cycle */
#else
		local_flush_tlb_all();	/* start new asid cycle */
#endif
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

	for_each_possible_cpu(i)
		cpu_context(i, mm) = 0;

	atomic_set(&mm->context.fp_mode_switching, 0);

	mm->context.bd_emupage_allocmap = NULL;
	spin_lock_init(&mm->context.bd_emupage_lock);
	init_waitqueue_head(&mm->context.bd_emupage_queue);

	return 0;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();
	unsigned long flags;
	local_irq_save(flags);

	htw_stop();
	/* Check if our ASID is of an older version and thus invalid */
	if ((cpu_context(cpu, next) ^ asid_cache(cpu)) & ASID_VERSION_MASK)
		get_new_mmu_context(next, cpu);
	write_c0_entryhi(cpu_asid(cpu, next));
	TLBMISS_HANDLER_SETUP_PGD(next->pgd);

	/*
	 * Mark current->active_mm as not "active" anymore.
	 * We don't want to mislead possible IPI tlb flush routines.
	 */
	cpumask_clear_cpu(cpu, mm_cpumask(prev));
	cpumask_set_cpu(cpu, mm_cpumask(next));
	htw_start();

	local_irq_restore(flags);
}

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	dsemul_mm_cleanup(mm);
}

#define deactivate_mm(tsk, mm)	do { } while (0)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void
activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned long flags;
	unsigned int cpu = smp_processor_id();

	local_irq_save(flags);

	htw_stop();
	/* Unconditionally get a new ASID.  */
	get_new_mmu_context(next, cpu);

	write_c0_entryhi(cpu_asid(cpu, next));
	TLBMISS_HANDLER_SETUP_PGD(next->pgd);

	/* mark mmu ownership change */
	cpumask_clear_cpu(cpu, mm_cpumask(prev));
	cpumask_set_cpu(cpu, mm_cpumask(next));
	htw_start();

	local_irq_restore(flags);
}

/*
 * If mm is currently active_mm, we can't really drop it.  Instead,
 * we will get a new one for it.
 */
static inline void
drop_mmu_context(struct mm_struct *mm, unsigned cpu)
{
	unsigned long flags;

	local_irq_save(flags);
	htw_stop();

	if (cpumask_test_cpu(cpu, mm_cpumask(mm)))  {
		get_new_mmu_context(mm, cpu);
		write_c0_entryhi(cpu_asid(cpu, mm));
	} else {
		/* will get a new context next time */
		cpu_context(cpu, mm) = 0;
	}
	htw_start();
	local_irq_restore(flags);
}

#endif /* _ASM_MMU_CONTEXT_H */
