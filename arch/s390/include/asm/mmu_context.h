/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/mmu_context.h"
 */

#ifndef __S390_MMU_CONTEXT_H
#define __S390_MMU_CONTEXT_H

#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/tlbflush.h>
#include <asm/ctl_reg.h>

static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	spin_lock_init(&mm->context.list_lock);
	INIT_LIST_HEAD(&mm->context.pgtable_list);
	INIT_LIST_HEAD(&mm->context.gmap_list);
	cpumask_clear(&mm->context.cpu_attach_mask);
	atomic_set(&mm->context.attach_count, 0);
	mm->context.flush_mm = 0;
#ifdef CONFIG_PGSTE
	mm->context.alloc_pgste = page_table_allocate_pgste;
	mm->context.has_pgste = 0;
	mm->context.use_skey = 0;
#endif
	if (mm->context.asce_limit == 0) {
		/* context created by exec, set asce limit to 4TB */
		mm->context.asce_bits = _ASCE_TABLE_LENGTH |
			_ASCE_USER_BITS | _ASCE_TYPE_REGION3;
		mm->context.asce_limit = STACK_TOP_MAX;
	} else if (mm->context.asce_limit == (1UL << 31)) {
		mm_inc_nr_pmds(mm);
	}
	crst_table_init((unsigned long *) mm->pgd, pgd_entry_type(mm));
	return 0;
}

#define destroy_context(mm)             do { } while (0)

static inline void set_user_asce(struct mm_struct *mm)
{
	S390_lowcore.user_asce = mm->context.asce_bits | __pa(mm->pgd);
	if (current->thread.mm_segment.ar4)
		__ctl_load(S390_lowcore.user_asce, 7, 7);
	set_cpu_flag(CIF_ASCE);
}

static inline void clear_user_asce(void)
{
	S390_lowcore.user_asce = S390_lowcore.kernel_asce;

	__ctl_load(S390_lowcore.user_asce, 1, 1);
	__ctl_load(S390_lowcore.user_asce, 7, 7);
}

static inline void load_kernel_asce(void)
{
	unsigned long asce;

	__ctl_store(asce, 1, 1);
	if (asce != S390_lowcore.kernel_asce)
		__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	set_cpu_flag(CIF_ASCE);
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	int cpu = smp_processor_id();

	S390_lowcore.user_asce = next->context.asce_bits | __pa(next->pgd);
	if (prev == next)
		return;
	if (MACHINE_HAS_TLB_LC)
		cpumask_set_cpu(cpu, &next->context.cpu_attach_mask);
	/* Clear old ASCE by loading the kernel ASCE. */
	__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	__ctl_load(S390_lowcore.kernel_asce, 7, 7);
	atomic_inc(&next->context.attach_count);
	atomic_dec(&prev->context.attach_count);
	if (MACHINE_HAS_TLB_LC)
		cpumask_clear_cpu(cpu, &prev->context.cpu_attach_mask);
}

#define finish_arch_post_lock_switch finish_arch_post_lock_switch
static inline void finish_arch_post_lock_switch(void)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;

	load_kernel_asce();
	if (mm) {
		preempt_disable();
		while (atomic_read(&mm->context.attach_count) >> 16)
			cpu_relax();

		cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
		if (mm->context.flush_mm)
			__tlb_flush_mm(mm);
		preempt_enable();
	}
	set_fs(current->thread.mm_segment);
}

#define enter_lazy_tlb(mm,tsk)	do { } while (0)
#define deactivate_mm(tsk,mm)	do { } while (0)

static inline void activate_mm(struct mm_struct *prev,
                               struct mm_struct *next)
{
	switch_mm(prev, next, current);
	cpumask_set_cpu(smp_processor_id(), mm_cpumask(next));
	set_user_asce(next);
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
}

static inline void arch_bprm_mm_init(struct mm_struct *mm,
				     struct vm_area_struct *vma)
{
}

#endif /* __S390_MMU_CONTEXT_H */
