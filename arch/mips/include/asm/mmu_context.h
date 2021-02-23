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
#include <linux/mm_types.h>
#include <linux/smp.h>
#include <linux/slab.h>

#include <asm/barrier.h>
#include <asm/cacheflush.h>
#include <asm/dsemul.h>
#include <asm/ginvt.h>
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

extern void tlbmiss_handler_setup_pgd(unsigned long);
extern char tlbmiss_handler_setup_pgd_end[];

/* Note: This is also implemented with uasm in arch/mips/kvm/entry.c */
#define TLBMISS_HANDLER_SETUP_PGD(pgd)					\
do {									\
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

/*
 * The ginvt instruction will invalidate wired entries when its type field
 * targets anything other than the entire TLB. That means that if we were to
 * allow the kernel to create wired entries with the MMID of current->active_mm
 * then those wired entries could be invalidated when we later use ginvt to
 * invalidate TLB entries with that MMID.
 *
 * In order to prevent ginvt from trashing wired entries, we reserve one MMID
 * for use by the kernel when creating wired entries. This MMID will never be
 * assigned to a struct mm, and we'll never target it with a ginvt instruction.
 */
#define MMID_KERNEL_WIRED	0

/*
 *  All unused by hardware upper bits will be considered
 *  as a software asid extension.
 */
static inline u64 asid_version_mask(unsigned int cpu)
{
	unsigned long asid_mask = cpu_asid_mask(&cpu_data[cpu]);

	return ~(u64)(asid_mask | (asid_mask - 1));
}

static inline u64 asid_first_version(unsigned int cpu)
{
	return ~asid_version_mask(cpu) + 1;
}

static inline u64 cpu_context(unsigned int cpu, const struct mm_struct *mm)
{
	if (cpu_has_mmid)
		return atomic64_read(&mm->context.mmid);

	return mm->context.asid[cpu];
}

static inline void set_cpu_context(unsigned int cpu,
				   struct mm_struct *mm, u64 ctx)
{
	if (cpu_has_mmid)
		atomic64_set(&mm->context.mmid, ctx);
	else
		mm->context.asid[cpu] = ctx;
}

#define asid_cache(cpu)		(cpu_data[cpu].asid_cache)
#define cpu_asid(cpu, mm) \
	(cpu_context((cpu), (mm)) & cpu_asid_mask(&cpu_data[cpu]))

extern void get_new_mmu_context(struct mm_struct *mm);
extern void check_mmu_context(struct mm_struct *mm);
extern void check_switch_mmu_context(struct mm_struct *mm);

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
#define init_new_context init_new_context
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	int i;

	if (cpu_has_mmid) {
		set_cpu_context(0, mm, 0);
	} else {
		for_each_possible_cpu(i)
			set_cpu_context(i, mm, 0);
	}

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
	check_switch_mmu_context(next);

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
#define destroy_context destroy_context
static inline void destroy_context(struct mm_struct *mm)
{
	dsemul_mm_cleanup(mm);
}

static inline void
drop_mmu_context(struct mm_struct *mm)
{
	unsigned long flags;
	unsigned int cpu;
	u32 old_mmid;
	u64 ctx;

	local_irq_save(flags);

	cpu = smp_processor_id();
	ctx = cpu_context(cpu, mm);

	if (!ctx) {
		/* no-op */
	} else if (cpu_has_mmid) {
		/*
		 * Globally invalidating TLB entries associated with the MMID
		 * is pretty cheap using the GINVT instruction, so we'll do
		 * that rather than incur the overhead of allocating a new
		 * MMID. The latter would be especially difficult since MMIDs
		 * are global & other CPUs may be actively using ctx.
		 */
		htw_stop();
		old_mmid = read_c0_memorymapid();
		write_c0_memorymapid(ctx & cpu_asid_mask(&cpu_data[cpu]));
		mtc0_tlbw_hazard();
		ginvt_mmid();
		sync_ginv();
		write_c0_memorymapid(old_mmid);
		instruction_hazard();
		htw_start();
	} else if (cpumask_test_cpu(cpu, mm_cpumask(mm))) {
		/*
		 * mm is currently active, so we can't really drop it.
		 * Instead we bump the ASID.
		 */
		htw_stop();
		get_new_mmu_context(mm);
		write_c0_entryhi(cpu_asid(cpu, mm));
		htw_start();
	} else {
		/* will get a new context next time */
		set_cpu_context(cpu, mm, 0);
	}

	local_irq_restore(flags);
}

#include <asm-generic/mmu_context.h>

#endif /* _ASM_MMU_CONTEXT_H */
