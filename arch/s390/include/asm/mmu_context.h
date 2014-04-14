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
	cpumask_clear(&mm->context.cpu_attach_mask);
	atomic_set(&mm->context.attach_count, 0);
	mm->context.flush_mm = 0;
	mm->context.asce_bits = _ASCE_TABLE_LENGTH | _ASCE_USER_BITS;
#ifdef CONFIG_64BIT
	mm->context.asce_bits |= _ASCE_TYPE_REGION3;
#endif
	mm->context.has_pgste = 0;
	mm->context.asce_limit = STACK_TOP_MAX;
	crst_table_init((unsigned long *) mm->pgd, pgd_entry_type(mm));
	return 0;
}

#define destroy_context(mm)             do { } while (0)

static inline void update_user_asce(struct mm_struct *mm, int load_primary)
{
	pgd_t *pgd = mm->pgd;

	S390_lowcore.user_asce = mm->context.asce_bits | __pa(pgd);
	if (load_primary)
		__ctl_load(S390_lowcore.user_asce, 1, 1);
	set_fs(current->thread.mm_segment);
}

static inline void clear_user_asce(struct mm_struct *mm, int load_primary)
{
	S390_lowcore.user_asce = S390_lowcore.kernel_asce;

	if (load_primary)
		__ctl_load(S390_lowcore.user_asce, 1, 1);
	__ctl_load(S390_lowcore.user_asce, 7, 7);
}

static inline void update_primary_asce(struct task_struct *tsk)
{
	unsigned long asce;

	__ctl_store(asce, 1, 1);
	if (asce != S390_lowcore.kernel_asce)
		__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	set_tsk_thread_flag(tsk, TIF_ASCE);
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	int cpu = smp_processor_id();

	update_primary_asce(tsk);
	if (prev == next)
		return;
	if (MACHINE_HAS_TLB_LC)
		cpumask_set_cpu(cpu, &next->context.cpu_attach_mask);
	if (atomic_inc_return(&next->context.attach_count) >> 16) {
		/* Delay update_user_asce until all TLB flushes are done. */
		set_tsk_thread_flag(tsk, TIF_TLB_WAIT);
		/* Clear old ASCE by loading the kernel ASCE. */
		clear_user_asce(next, 0);
	} else {
		cpumask_set_cpu(cpu, mm_cpumask(next));
		update_user_asce(next, 0);
		if (next->context.flush_mm)
			/* Flush pending TLBs */
			__tlb_flush_mm(next);
	}
	atomic_dec(&prev->context.attach_count);
	WARN_ON(atomic_read(&prev->context.attach_count) < 0);
	if (MACHINE_HAS_TLB_LC)
		cpumask_clear_cpu(cpu, &prev->context.cpu_attach_mask);
}

#define finish_arch_post_lock_switch finish_arch_post_lock_switch
static inline void finish_arch_post_lock_switch(void)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;

	if (!test_tsk_thread_flag(tsk, TIF_TLB_WAIT))
		return;
	preempt_disable();
	clear_tsk_thread_flag(tsk, TIF_TLB_WAIT);
	while (atomic_read(&mm->context.attach_count) >> 16)
		cpu_relax();

	cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
	update_user_asce(mm, 0);
	if (mm->context.flush_mm)
		__tlb_flush_mm(mm);
	preempt_enable();
}

#define enter_lazy_tlb(mm,tsk)	do { } while (0)
#define deactivate_mm(tsk,mm)	do { } while (0)

static inline void activate_mm(struct mm_struct *prev,
                               struct mm_struct *next)
{
        switch_mm(prev, next, current);
}

static inline void arch_dup_mmap(struct mm_struct *oldmm,
				 struct mm_struct *mm)
{
#ifdef CONFIG_64BIT
	if (oldmm->context.asce_limit < mm->context.asce_limit)
		crst_table_downgrade(mm, oldmm->context.asce_limit);
#endif
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
}

#endif /* __S390_MMU_CONTEXT_H */
