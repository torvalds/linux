/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC64_MMU_CONTEXT_H
#define __SPARC64_MMU_CONTEXT_H

/* Derived heavily from Linus's Alpha/AXP ASN code... */

#ifndef __ASSEMBLY__

#include <linux/spinlock.h>
#include <linux/mm_types.h>

#include <asm/spitfire.h>
#include <asm-generic/mm_hooks.h>

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

extern spinlock_t ctx_alloc_lock;
extern unsigned long tlb_context_cache;
extern unsigned long mmu_context_bmap[];

DECLARE_PER_CPU(struct mm_struct *, per_cpu_secondary_mm);
void get_new_mmu_context(struct mm_struct *mm);
int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void destroy_context(struct mm_struct *mm);

void __tsb_context_switch(unsigned long pgd_pa,
			  struct tsb_config *tsb_base,
			  struct tsb_config *tsb_huge,
			  unsigned long tsb_descr_pa,
			  unsigned long secondary_ctx);

static inline void tsb_context_switch_ctx(struct mm_struct *mm,
					  unsigned long ctx)
{
	__tsb_context_switch(__pa(mm->pgd),
			     &mm->context.tsb_block[MM_TSB_BASE],
#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
			     (mm->context.tsb_block[MM_TSB_HUGE].tsb ?
			      &mm->context.tsb_block[MM_TSB_HUGE] :
			      NULL)
#else
			     NULL
#endif
			     , __pa(&mm->context.tsb_descr[MM_TSB_BASE]),
			     ctx);
}

#define tsb_context_switch(X) tsb_context_switch_ctx(X, 0)

void tsb_grow(struct mm_struct *mm,
	      unsigned long tsb_index,
	      unsigned long mm_rss);
#ifdef CONFIG_SMP
void smp_tsb_sync(struct mm_struct *mm);
#else
#define smp_tsb_sync(__mm) do { } while (0)
#endif

/* Set MMU context in the actual hardware. */
#define load_secondary_context(__mm) \
	__asm__ __volatile__( \
	"\n661:	stxa		%0, [%1] %2\n" \
	"	.section	.sun4v_1insn_patch, \"ax\"\n" \
	"	.word		661b\n" \
	"	stxa		%0, [%1] %3\n" \
	"	.previous\n" \
	"	flush		%%g6\n" \
	: /* No outputs */ \
	: "r" (CTX_HWBITS((__mm)->context)), \
	  "r" (SECONDARY_CONTEXT), "i" (ASI_DMMU), "i" (ASI_MMU))

void __flush_tlb_mm(unsigned long, unsigned long);

/* Switch the current MM context. */
static inline void switch_mm(struct mm_struct *old_mm, struct mm_struct *mm, struct task_struct *tsk)
{
	unsigned long ctx_valid, flags;
	int cpu = smp_processor_id();

	per_cpu(per_cpu_secondary_mm, cpu) = mm;
	if (unlikely(mm == &init_mm))
		return;

	spin_lock_irqsave(&mm->context.lock, flags);
	ctx_valid = CTX_VALID(mm->context);
	if (!ctx_valid)
		get_new_mmu_context(mm);

	/* We have to be extremely careful here or else we will miss
	 * a TSB grow if we switch back and forth between a kernel
	 * thread and an address space which has it's TSB size increased
	 * on another processor.
	 *
	 * It is possible to play some games in order to optimize the
	 * switch, but the safest thing to do is to unconditionally
	 * perform the secondary context load and the TSB context switch.
	 *
	 * For reference the bad case is, for address space "A":
	 *
	 *		CPU 0			CPU 1
	 *	run address space A
	 *	set cpu0's bits in cpu_vm_mask
	 *	switch to kernel thread, borrow
	 *	address space A via entry_lazy_tlb
	 *					run address space A
	 *					set cpu1's bit in cpu_vm_mask
	 *					flush_tlb_pending()
	 *					reset cpu_vm_mask to just cpu1
	 *					TSB grow
	 *	run address space A
	 *	context was valid, so skip
	 *	TSB context switch
	 *
	 * At that point cpu0 continues to use a stale TSB, the one from
	 * before the TSB grow performed on cpu1.  cpu1 did not cross-call
	 * cpu0 to update it's TSB because at that point the cpu_vm_mask
	 * only had cpu1 set in it.
	 */
	tsb_context_switch_ctx(mm, CTX_HWBITS(mm->context));

	/* Any time a processor runs a context on an address space
	 * for the first time, we must flush that context out of the
	 * local TLB.
	 */
	if (!ctx_valid || !cpumask_test_cpu(cpu, mm_cpumask(mm))) {
		cpumask_set_cpu(cpu, mm_cpumask(mm));
		__flush_tlb_mm(CTX_HWBITS(mm->context),
			       SECONDARY_CONTEXT);
	}
	spin_unlock_irqrestore(&mm->context.lock, flags);
}

#define deactivate_mm(tsk,mm)	do { } while (0)
#define activate_mm(active_mm, mm) switch_mm(active_mm, mm, NULL)
#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_MMU_CONTEXT_H) */
