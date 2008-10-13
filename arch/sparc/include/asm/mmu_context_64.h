#ifndef __SPARC64_MMU_CONTEXT_H
#define __SPARC64_MMU_CONTEXT_H

/* Derived heavily from Linus's Alpha/AXP ASN code... */

#ifndef __ASSEMBLY__

#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/spitfire.h>
#include <asm-generic/mm_hooks.h>

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

extern spinlock_t ctx_alloc_lock;
extern unsigned long tlb_context_cache;
extern unsigned long mmu_context_bmap[];

extern void get_new_mmu_context(struct mm_struct *mm);
#ifdef CONFIG_SMP
extern void smp_new_mmu_context_version(void);
#else
#define smp_new_mmu_context_version() do { } while (0)
#endif

extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void destroy_context(struct mm_struct *mm);

extern void __tsb_context_switch(unsigned long pgd_pa,
				 struct tsb_config *tsb_base,
				 struct tsb_config *tsb_huge,
				 unsigned long tsb_descr_pa);

static inline void tsb_context_switch(struct mm_struct *mm)
{
	__tsb_context_switch(__pa(mm->pgd),
			     &mm->context.tsb_block[0],
#ifdef CONFIG_HUGETLB_PAGE
			     (mm->context.tsb_block[1].tsb ?
			      &mm->context.tsb_block[1] :
			      NULL)
#else
			     NULL
#endif
			     , __pa(&mm->context.tsb_descr[0]));
}

extern void tsb_grow(struct mm_struct *mm, unsigned long tsb_index, unsigned long mm_rss);
#ifdef CONFIG_SMP
extern void smp_tsb_sync(struct mm_struct *mm);
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

extern void __flush_tlb_mm(unsigned long, unsigned long);

/* Switch the current MM context.  Interrupts are disabled.  */
static inline void switch_mm(struct mm_struct *old_mm, struct mm_struct *mm, struct task_struct *tsk)
{
	unsigned long ctx_valid, flags;
	int cpu;

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
	load_secondary_context(mm);
	tsb_context_switch(mm);

	/* Any time a processor runs a context on an address space
	 * for the first time, we must flush that context out of the
	 * local TLB.
	 */
	cpu = smp_processor_id();
	if (!ctx_valid || !cpu_isset(cpu, mm->cpu_vm_mask)) {
		cpu_set(cpu, mm->cpu_vm_mask);
		__flush_tlb_mm(CTX_HWBITS(mm->context),
			       SECONDARY_CONTEXT);
	}
	spin_unlock_irqrestore(&mm->context.lock, flags);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/* Activate a new MM instance for the current task. */
static inline void activate_mm(struct mm_struct *active_mm, struct mm_struct *mm)
{
	unsigned long flags;
	int cpu;

	spin_lock_irqsave(&mm->context.lock, flags);
	if (!CTX_VALID(mm->context))
		get_new_mmu_context(mm);
	cpu = smp_processor_id();
	if (!cpu_isset(cpu, mm->cpu_vm_mask))
		cpu_set(cpu, mm->cpu_vm_mask);

	load_secondary_context(mm);
	__flush_tlb_mm(CTX_HWBITS(mm->context), SECONDARY_CONTEXT);
	tsb_context_switch(mm);
	spin_unlock_irqrestore(&mm->context.lock, flags);
}

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_MMU_CONTEXT_H) */
