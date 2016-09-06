#ifndef __ASM_POWERPC_MMU_CONTEXT_H
#define __ASM_POWERPC_MMU_CONTEXT_H
#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <asm/mmu.h>	
#include <asm/cputable.h>
#include <asm/cputhreads.h>

/*
 * Most if the context management is out of line
 */
extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void destroy_context(struct mm_struct *mm);
#ifdef CONFIG_SPAPR_TCE_IOMMU
struct mm_iommu_table_group_mem_t;

extern int isolate_lru_page(struct page *page);	/* from internal.h */
extern bool mm_iommu_preregistered(void);
extern long mm_iommu_get(unsigned long ua, unsigned long entries,
		struct mm_iommu_table_group_mem_t **pmem);
extern long mm_iommu_put(struct mm_iommu_table_group_mem_t *mem);
extern void mm_iommu_init(mm_context_t *ctx);
extern void mm_iommu_cleanup(mm_context_t *ctx);
extern struct mm_iommu_table_group_mem_t *mm_iommu_lookup(unsigned long ua,
		unsigned long size);
extern struct mm_iommu_table_group_mem_t *mm_iommu_find(unsigned long ua,
		unsigned long entries);
extern long mm_iommu_ua_to_hpa(struct mm_iommu_table_group_mem_t *mem,
		unsigned long ua, unsigned long *hpa);
extern long mm_iommu_mapped_inc(struct mm_iommu_table_group_mem_t *mem);
extern void mm_iommu_mapped_dec(struct mm_iommu_table_group_mem_t *mem);
#endif
extern void switch_slb(struct task_struct *tsk, struct mm_struct *mm);
extern void set_context(unsigned long id, pgd_t *pgd);

#ifdef CONFIG_PPC_BOOK3S_64
extern void radix__switch_mmu_context(struct mm_struct *prev,
				     struct mm_struct *next);
static inline void switch_mmu_context(struct mm_struct *prev,
				      struct mm_struct *next,
				      struct task_struct *tsk)
{
	if (radix_enabled())
		return radix__switch_mmu_context(prev, next);
	return switch_slb(tsk, next);
}

extern int __init_new_context(void);
extern void __destroy_context(int context_id);
static inline void mmu_context_init(void) { }
#else
extern void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk);
extern unsigned long __init_new_context(void);
extern void __destroy_context(unsigned long context_id);
extern void mmu_context_init(void);
#endif

extern void switch_cop(struct mm_struct *next);
extern int use_cop(unsigned long acop, struct mm_struct *mm);
extern void drop_cop(unsigned long acop, struct mm_struct *mm);

/*
 * switch_mm is the entry point called from the architecture independent
 * code in kernel/sched/core.c
 */
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	/* Mark this context has been used on the new CPU */
	cpumask_set_cpu(smp_processor_id(), mm_cpumask(next));

	/* 32-bit keeps track of the current PGDIR in the thread struct */
#ifdef CONFIG_PPC32
	tsk->thread.pgdir = next->pgd;
#endif /* CONFIG_PPC32 */

	/* 64-bit Book3E keeps track of current PGD in the PACA */
#ifdef CONFIG_PPC_BOOK3E_64
	get_paca()->pgd = next->pgd;
#endif
	/* Nothing else to do if we aren't actually switching */
	if (prev == next)
		return;

#ifdef CONFIG_PPC_ICSWX
	/* Switch coprocessor context only if prev or next uses a coprocessor */
	if (prev->context.acop || next->context.acop)
		switch_cop(next);
#endif /* CONFIG_PPC_ICSWX */

	/* We must stop all altivec streams before changing the HW
	 * context
	 */
#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		asm volatile ("dssall");
#endif /* CONFIG_ALTIVEC */
	/*
	 * The actual HW switching method differs between the various
	 * sub architectures. Out of line for now
	 */
	switch_mmu_context(prev, next, tsk);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm(prev, next, current);
	local_irq_restore(flags);
}

/* We don't currently use enter_lazy_tlb() for anything */
static inline void enter_lazy_tlb(struct mm_struct *mm,
				  struct task_struct *tsk)
{
	/* 64-bit Book3E keeps track of current PGD in the PACA */
#ifdef CONFIG_PPC_BOOK3E_64
	get_paca()->pgd = NULL;
#endif
}

static inline void arch_dup_mmap(struct mm_struct *oldmm,
				 struct mm_struct *mm)
{
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
}

static inline void arch_unmap(struct mm_struct *mm,
			      struct vm_area_struct *vma,
			      unsigned long start, unsigned long end)
{
	if (start <= mm->context.vdso_base && mm->context.vdso_base < end)
		mm->context.vdso_base = 0;
}

static inline void arch_bprm_mm_init(struct mm_struct *mm,
				     struct vm_area_struct *vma)
{
}

static inline bool arch_vma_access_permitted(struct vm_area_struct *vma,
		bool write, bool execute, bool foreign)
{
	/* by default, allow everything */
	return true;
}

static inline bool arch_pte_access_permitted(pte_t pte, bool write)
{
	/* by default, allow everything */
	return true;
}
#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_MMU_CONTEXT_H */
