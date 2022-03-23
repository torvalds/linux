// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Common implementation of switch_mm_irqs_off
 *
 *  Copyright IBM Corp. 2017
 */

#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/sched/mm.h>

#include <asm/mmu_context.h>
#include <asm/pgalloc.h>

#if defined(CONFIG_PPC32)
static inline void switch_mm_pgdir(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	/* 32-bit keeps track of the current PGDIR in the thread struct */
	tsk->thread.pgdir = mm->pgd;
#ifdef CONFIG_PPC_BOOK3S_32
	tsk->thread.sr0 = mm->context.sr0;
#endif
#if defined(CONFIG_BOOKE_OR_40x) && defined(CONFIG_PPC_KUAP)
	tsk->thread.pid = mm->context.id;
#endif
}
#elif defined(CONFIG_PPC_BOOK3E_64)
static inline void switch_mm_pgdir(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	/* 64-bit Book3E keeps track of current PGD in the PACA */
	get_paca()->pgd = mm->pgd;
#ifdef CONFIG_PPC_KUAP
	tsk->thread.pid = mm->context.id;
#endif
}
#else
static inline void switch_mm_pgdir(struct task_struct *tsk,
				   struct mm_struct *mm) { }
#endif

void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	bool new_on_cpu = false;

	/* Mark this context has been used on the new CPU */
	if (!cpumask_test_cpu(smp_processor_id(), mm_cpumask(next))) {
		cpumask_set_cpu(smp_processor_id(), mm_cpumask(next));
		inc_mm_active_cpus(next);

		/*
		 * This full barrier orders the store to the cpumask above vs
		 * a subsequent load which allows this CPU/MMU to begin loading
		 * translations for 'next' from page table PTEs into the TLB.
		 *
		 * When using the radix MMU, that operation is the load of the
		 * MMU context id, which is then moved to SPRN_PID.
		 *
		 * For the hash MMU it is either the first load from slb_cache
		 * in switch_slb() to preload the SLBs, or the load of
		 * get_user_context which loads the context for the VSID hash
		 * to insert a new SLB, in the SLB fault handler.
		 *
		 * On the other side, the barrier is in mm/tlb-radix.c for
		 * radix which orders earlier stores to clear the PTEs before
		 * the load of mm_cpumask to check which CPU TLBs should be
		 * flushed. For hash, pte_xchg to clear the PTE includes the
		 * barrier.
		 *
		 * This full barrier is also needed by membarrier when
		 * switching between processes after store to rq->curr, before
		 * user-space memory accesses.
		 */
		smp_mb();

		new_on_cpu = true;
	}

	/* Some subarchs need to track the PGD elsewhere */
	switch_mm_pgdir(tsk, next);

	/* Nothing else to do if we aren't actually switching */
	if (prev == next)
		return;

	/*
	 * We must stop all altivec streams before changing the HW
	 * context
	 */
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		asm volatile (PPC_DSSALL);

	if (!new_on_cpu)
		membarrier_arch_switch_mm(prev, next, tsk);

	/*
	 * The actual HW switching method differs between the various
	 * sub architectures. Out of line for now
	 */
	switch_mmu_context(prev, next, tsk);
}

#ifndef CONFIG_PPC_BOOK3S_64
void arch_exit_mmap(struct mm_struct *mm)
{
	void *frag = pte_frag_get(&mm->context);

	if (frag)
		pte_frag_destroy(frag);
}
#endif
