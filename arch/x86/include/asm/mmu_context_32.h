#ifndef _ASM_X86_MMU_CONTEXT_32_H
#define _ASM_X86_MMU_CONTEXT_32_H

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
#ifdef CONFIG_SMP
	if (x86_read_percpu(cpu_tlbstate.state) == TLBSTATE_OK)
		x86_write_percpu(cpu_tlbstate.state, TLBSTATE_LAZY);
#endif
}

static inline void switch_mm(struct mm_struct *prev,
			     struct mm_struct *next,
			     struct task_struct *tsk)
{
	int cpu = smp_processor_id();

	if (likely(prev != next)) {
		/* stop flush ipis for the previous mm */
		cpu_clear(cpu, prev->cpu_vm_mask);
#ifdef CONFIG_SMP
		x86_write_percpu(cpu_tlbstate.state, TLBSTATE_OK);
		x86_write_percpu(cpu_tlbstate.active_mm, next);
#endif
		cpu_set(cpu, next->cpu_vm_mask);

		/* Re-load page tables */
		load_cr3(next->pgd);

		/*
		 * load the LDT, if the LDT is different:
		 */
		if (unlikely(prev->context.ldt != next->context.ldt))
			load_LDT_nolock(&next->context);
	}
#ifdef CONFIG_SMP
	else {
		x86_write_percpu(cpu_tlbstate.state, TLBSTATE_OK);
		BUG_ON(x86_read_percpu(cpu_tlbstate.active_mm) != next);

		if (!cpu_test_and_set(cpu, next->cpu_vm_mask)) {
			/* We were in lazy tlb mode and leave_mm disabled
			 * tlb flush IPI delivery. We must reload %cr3.
			 */
			load_cr3(next->pgd);
			load_LDT_nolock(&next->context);
		}
	}
#endif
}

#define deactivate_mm(tsk, mm)			\
	asm("movl %0,%%gs": :"r" (0));

#endif /* _ASM_X86_MMU_CONTEXT_32_H */
