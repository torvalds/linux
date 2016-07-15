#ifndef _ASM_X86_MMU_CONTEXT_H
#define _ASM_X86_MMU_CONTEXT_H

#include <asm/desc.h>
#include <linux/atomic.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/paravirt.h>
#ifndef CONFIG_PARAVIRT
#include <asm-generic/mm_hooks.h>

static inline void paravirt_activate_mm(struct mm_struct *prev,
					struct mm_struct *next)
{
}
#endif	/* !CONFIG_PARAVIRT */

/*
 * Used for LDT copy/destruction.
 */
int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void destroy_context(struct mm_struct *mm);


static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
#ifdef CONFIG_SMP
	if (this_cpu_read(cpu_tlbstate.state) == TLBSTATE_OK)
		this_cpu_write(cpu_tlbstate.state, TLBSTATE_LAZY);
#endif
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();

	if (likely(prev != next)) {
#ifdef CONFIG_SMP
		this_cpu_write(cpu_tlbstate.state, TLBSTATE_OK);
		this_cpu_write(cpu_tlbstate.active_mm, next);
#endif
		cpumask_set_cpu(cpu, mm_cpumask(next));

		/*
		 * Re-load page tables.
		 *
		 * This logic has an ordering constraint:
		 *
		 *  CPU 0: Write to a PTE for 'next'
		 *  CPU 0: load bit 1 in mm_cpumask.  if nonzero, send IPI.
		 *  CPU 1: set bit 1 in next's mm_cpumask
		 *  CPU 1: load from the PTE that CPU 0 writes (implicit)
		 *
		 * We need to prevent an outcome in which CPU 1 observes
		 * the new PTE value and CPU 0 observes bit 1 clear in
		 * mm_cpumask.  (If that occurs, then the IPI will never
		 * be sent, and CPU 0's TLB will contain a stale entry.)
		 *
		 * The bad outcome can occur if either CPU's load is
		 * reordered before that CPU's store, so both CPUs much
		 * execute full barriers to prevent this from happening.
		 *
		 * Thus, switch_mm needs a full barrier between the
		 * store to mm_cpumask and any operation that could load
		 * from next->pgd.  This barrier synchronizes with
		 * remote TLB flushers.  Fortunately, load_cr3 is
		 * serializing and thus acts as a full barrier.
		 *
		 */
		load_cr3(next->pgd);

		/* Stop flush ipis for the previous mm */
		cpumask_clear_cpu(cpu, mm_cpumask(prev));

		/* Load the LDT, if the LDT is different: */
		if (unlikely(prev->context.ldt != next->context.ldt))
			load_LDT_nolock(&next->context);
	}
#ifdef CONFIG_SMP
	  else {
		this_cpu_write(cpu_tlbstate.state, TLBSTATE_OK);
		BUG_ON(this_cpu_read(cpu_tlbstate.active_mm) != next);

		if (!cpumask_test_cpu(cpu, mm_cpumask(next))) {
			/*
			 * On established mms, the mm_cpumask is only changed
			 * from irq context, from ptep_clear_flush() while in
			 * lazy tlb mode, and here. Irqs are blocked during
			 * schedule, protecting us from simultaneous changes.
			 */
			cpumask_set_cpu(cpu, mm_cpumask(next));

			/*
			 * We were in lazy tlb mode and leave_mm disabled
			 * tlb flush IPI delivery. We must reload CR3
			 * to make sure to use no freed page tables.
			 *
			 * As above, this is a barrier that forces
			 * TLB repopulation to be ordered after the
			 * store to mm_cpumask.
			 */
			load_cr3(next->pgd);
			load_LDT_nolock(&next->context);
		}
	}
#endif
}

#define activate_mm(prev, next)			\
do {						\
	paravirt_activate_mm((prev), (next));	\
	switch_mm((prev), (next), NULL);	\
} while (0);

#ifdef CONFIG_X86_32
#define deactivate_mm(tsk, mm)			\
do {						\
	lazy_load_gs(0);			\
} while (0)
#else
#define deactivate_mm(tsk, mm)			\
do {						\
	load_gs_index(0);			\
	loadsegment(fs, 0);			\
} while (0)
#endif

#endif /* _ASM_X86_MMU_CONTEXT_H */
