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
#include <asm/hazards.h>
#include <asm/tlbflush.h>
#ifdef CONFIG_MIPS_MT_SMTC
#include <asm/mipsmtregs.h>
#include <asm/smtc.h>
#endif /* SMTC */
#include <asm-generic/mm_hooks.h>

#ifdef CONFIG_MIPS_PGD_C0_CONTEXT

#define TLBMISS_HANDLER_SETUP_PGD(pgd)					\
do {									\
	void (*tlbmiss_handler_setup_pgd)(unsigned long);		\
	extern u32 tlbmiss_handler_setup_pgd_array[16];			\
									\
	tlbmiss_handler_setup_pgd =					\
		(__typeof__(tlbmiss_handler_setup_pgd)) tlbmiss_handler_setup_pgd_array; \
	tlbmiss_handler_setup_pgd((unsigned long)(pgd));		\
} while (0)

#define TLBMISS_HANDLER_SETUP()						\
	do {								\
		TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir);		\
		write_c0_xcontext((unsigned long) smp_processor_id() << 51); \
	} while (0)

#else /* CONFIG_MIPS_PGD_C0_CONTEXT: using  pgd_current*/

/*
 * For the fast tlb miss handlers, we keep a per cpu array of pointers
 * to the current pgd for each processor. Also, the proc. id is stuffed
 * into the context register.
 */
extern unsigned long pgd_current[];

#define TLBMISS_HANDLER_SETUP_PGD(pgd) \
	pgd_current[smp_processor_id()] = (unsigned long)(pgd)

#ifdef CONFIG_32BIT
#define TLBMISS_HANDLER_SETUP()						\
	write_c0_context((unsigned long) smp_processor_id() << 25);	\
	back_to_back_c0_hazard();					\
	TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir)
#endif
#ifdef CONFIG_64BIT
#define TLBMISS_HANDLER_SETUP()						\
	write_c0_context((unsigned long) smp_processor_id() << 26);	\
	back_to_back_c0_hazard();					\
	TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir)
#endif
#endif /* CONFIG_MIPS_PGD_C0_CONTEXT*/
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

#define ASID_INC	0x40
#define ASID_MASK	0xfc0

#elif defined(CONFIG_CPU_R8000)

#define ASID_INC	0x10
#define ASID_MASK	0xff0

#elif defined(CONFIG_MIPS_MT_SMTC)

#define ASID_INC	0x1
extern unsigned long smtc_asid_mask;
#define ASID_MASK	(smtc_asid_mask)
#define HW_ASID_MASK	0xff
/* End SMTC/34K debug hack */
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

#ifndef CONFIG_MIPS_MT_SMTC
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

#else /* CONFIG_MIPS_MT_SMTC */

#define get_new_mmu_context(mm, cpu) smtc_get_new_mmu_context((mm), (cpu))

#endif /* CONFIG_MIPS_MT_SMTC */

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

	return 0;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();
	unsigned long flags;
#ifdef CONFIG_MIPS_MT_SMTC
	unsigned long oldasid;
	unsigned long mtflags;
	int mytlb = (smtc_status & SMTC_TLB_SHARED) ? 0 : cpu_data[cpu].vpe_id;
	local_irq_save(flags);
	mtflags = dvpe();
#else /* Not SMTC */
	local_irq_save(flags);
#endif /* CONFIG_MIPS_MT_SMTC */

	/* Check if our ASID is of an older version and thus invalid */
	if ((cpu_context(cpu, next) ^ asid_cache(cpu)) & ASID_VERSION_MASK)
		get_new_mmu_context(next, cpu);
#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 * If the EntryHi ASID being replaced happens to be
	 * the value flagged at ASID recycling time as having
	 * an extended life, clear the bit showing it being
	 * in use by this "CPU", and if that's the last bit,
	 * free up the ASID value for use and flush any old
	 * instances of it from the TLB.
	 */
	oldasid = (read_c0_entryhi() & ASID_MASK);
	if(smtc_live_asid[mytlb][oldasid]) {
		smtc_live_asid[mytlb][oldasid] &= ~(0x1 << cpu);
		if(smtc_live_asid[mytlb][oldasid] == 0)
			smtc_flush_tlb_asid(oldasid);
	}
	/*
	 * Tread softly on EntryHi, and so long as we support
	 * having ASID_MASK smaller than the hardware maximum,
	 * make sure no "soft" bits become "hard"...
	 */
	write_c0_entryhi((read_c0_entryhi() & ~HW_ASID_MASK) |
			 cpu_asid(cpu, next));
	ehb(); /* Make sure it propagates to TCStatus */
	evpe(mtflags);
#else
	write_c0_entryhi(cpu_asid(cpu, next));
#endif /* CONFIG_MIPS_MT_SMTC */
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
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
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

#ifdef CONFIG_MIPS_MT_SMTC
	unsigned long oldasid;
	unsigned long mtflags;
	int mytlb = (smtc_status & SMTC_TLB_SHARED) ? 0 : cpu_data[cpu].vpe_id;
#endif /* CONFIG_MIPS_MT_SMTC */

	local_irq_save(flags);

	/* Unconditionally get a new ASID.  */
	get_new_mmu_context(next, cpu);

#ifdef CONFIG_MIPS_MT_SMTC
	/* See comments for similar code above */
	mtflags = dvpe();
	oldasid = read_c0_entryhi() & ASID_MASK;
	if(smtc_live_asid[mytlb][oldasid]) {
		smtc_live_asid[mytlb][oldasid] &= ~(0x1 << cpu);
		if(smtc_live_asid[mytlb][oldasid] == 0)
			 smtc_flush_tlb_asid(oldasid);
	}
	/* See comments for similar code above */
	write_c0_entryhi((read_c0_entryhi() & ~HW_ASID_MASK) |
			 cpu_asid(cpu, next));
	ehb(); /* Make sure it propagates to TCStatus */
	evpe(mtflags);
#else
	write_c0_entryhi(cpu_asid(cpu, next));
#endif /* CONFIG_MIPS_MT_SMTC */
	TLBMISS_HANDLER_SETUP_PGD(next->pgd);

	/* mark mmu ownership change */
	cpumask_clear_cpu(cpu, mm_cpumask(prev));
	cpumask_set_cpu(cpu, mm_cpumask(next));

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
#ifdef CONFIG_MIPS_MT_SMTC
	unsigned long oldasid;
	/* Can't use spinlock because called from TLB flush within DVPE */
	unsigned int prevvpe;
	int mytlb = (smtc_status & SMTC_TLB_SHARED) ? 0 : cpu_data[cpu].vpe_id;
#endif /* CONFIG_MIPS_MT_SMTC */

	local_irq_save(flags);

	if (cpumask_test_cpu(cpu, mm_cpumask(mm)))  {
		get_new_mmu_context(mm, cpu);
#ifdef CONFIG_MIPS_MT_SMTC
		/* See comments for similar code above */
		prevvpe = dvpe();
		oldasid = (read_c0_entryhi() & ASID_MASK);
		if (smtc_live_asid[mytlb][oldasid]) {
			smtc_live_asid[mytlb][oldasid] &= ~(0x1 << cpu);
			if(smtc_live_asid[mytlb][oldasid] == 0)
				smtc_flush_tlb_asid(oldasid);
		}
		/* See comments for similar code above */
		write_c0_entryhi((read_c0_entryhi() & ~HW_ASID_MASK)
				| cpu_asid(cpu, mm));
		ehb(); /* Make sure it propagates to TCStatus */
		evpe(prevvpe);
#else /* not CONFIG_MIPS_MT_SMTC */
		write_c0_entryhi(cpu_asid(cpu, mm));
#endif /* CONFIG_MIPS_MT_SMTC */
	} else {
		/* will get a new context next time */
#ifndef CONFIG_MIPS_MT_SMTC
		cpu_context(cpu, mm) = 0;
#else /* SMTC */
		int i;

		/* SMTC shares the TLB (and ASIDs) across VPEs */
		for_each_online_cpu(i) {
		    if((smtc_status & SMTC_TLB_SHARED)
		    || (cpu_data[i].vpe_id == cpu_data[cpu].vpe_id))
			cpu_context(i, mm) = 0;
		}
#endif /* CONFIG_MIPS_MT_SMTC */
	}
	local_irq_restore(flags);
}

#endif /* _ASM_MMU_CONTEXT_H */
