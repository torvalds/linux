/*
 * Switch an MMU context.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2013 Tensilica Inc.
 */

#ifndef _XTENSA_MMU_CONTEXT_H
#define _XTENSA_MMU_CONTEXT_H

#ifndef CONFIG_MMU
#include <asm/nommu_context.h>
#else

#include <linux/stringify.h>
#include <linux/sched.h>
#include <linux/mm_types.h>

#include <asm/vectors.h>

#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm-generic/mm_hooks.h>
#include <asm-generic/percpu.h>

#if (XCHAL_HAVE_TLBS != 1)
# error "Linux must have an MMU!"
#endif

DECLARE_PER_CPU(unsigned long, asid_cache);
#define cpu_asid_cache(cpu) per_cpu(asid_cache, cpu)

/*
 * NO_CONTEXT is the invalid ASID value that we don't ever assign to
 * any user or kernel context.  We use the reserved values in the
 * ASID_INSERT macro below.
 *
 * 0 invalid
 * 1 kernel
 * 2 reserved
 * 3 reserved
 * 4...255 available
 */

#define NO_CONTEXT	0
#define ASID_USER_FIRST	4
#define ASID_MASK	((1 << XCHAL_MMU_ASID_BITS) - 1)
#define ASID_INSERT(x)	(0x03020001 | (((x) & ASID_MASK) << 8))

void init_mmu(void);
void init_kio(void);

static inline void set_rasid_register (unsigned long val)
{
	__asm__ __volatile__ (" wsr %0, rasid\n\t"
			      " isync\n" : : "a" (val));
}

static inline unsigned long get_rasid_register (void)
{
	unsigned long tmp;
	__asm__ __volatile__ (" rsr %0, rasid\n\t" : "=a" (tmp));
	return tmp;
}

static inline void get_new_mmu_context(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long asid = cpu_asid_cache(cpu);
	if ((++asid & ASID_MASK) == 0) {
		/*
		 * Start new asid cycle; continue counting with next
		 * incarnation bits; skipping over 0, 1, 2, 3.
		 */
		local_flush_tlb_all();
		asid += ASID_USER_FIRST;
	}
	cpu_asid_cache(cpu) = asid;
	mm->context.asid[cpu] = asid;
	mm->context.cpu = cpu;
}

static inline void get_mmu_context(struct mm_struct *mm, unsigned int cpu)
{
	/*
	 * Check if our ASID is of an older version and thus invalid.
	 */

	if (mm) {
		unsigned long asid = mm->context.asid[cpu];

		if (asid == NO_CONTEXT ||
				((asid ^ cpu_asid_cache(cpu)) & ~ASID_MASK))
			get_new_mmu_context(mm, cpu);
	}
}

static inline void activate_context(struct mm_struct *mm, unsigned int cpu)
{
	get_mmu_context(mm, cpu);
	set_rasid_register(ASID_INSERT(mm->context.asid[cpu]));
	invalidate_page_directory();
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.  Valid cpu values are 0..(NR_CPUS-1), so initializing
 * to -1 says the process has never run on any core.
 */

static inline int init_new_context(struct task_struct *tsk,
		struct mm_struct *mm)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		mm->context.asid[cpu] = NO_CONTEXT;
	}
	mm->context.cpu = -1;
	return 0;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();
	int migrated = next->context.cpu != cpu;
	/* Flush the icache if we migrated to a new core. */
	if (migrated) {
		__invalidate_icache_all();
		next->context.cpu = cpu;
	}
	if (migrated || prev != next)
		activate_context(next, cpu);
}

#define activate_mm(prev, next)	switch_mm((prev), (next), NULL)
#define deactivate_mm(tsk, mm)	do { } while (0)

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	invalidate_page_directory();
}


static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
	/* Nothing to do. */

}

#endif /* CONFIG_MMU */
#endif /* _XTENSA_MMU_CONTEXT_H */
