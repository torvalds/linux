/* SPDX-License-Identifier: GPL-2.0 */
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
#define init_new_context init_new_context
extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
#define destroy_context destroy_context
extern void destroy_context(struct mm_struct *mm);
#ifdef CONFIG_SPAPR_TCE_IOMMU
struct mm_iommu_table_group_mem_t;

extern bool mm_iommu_preregistered(struct mm_struct *mm);
extern long mm_iommu_new(struct mm_struct *mm,
		unsigned long ua, unsigned long entries,
		struct mm_iommu_table_group_mem_t **pmem);
extern long mm_iommu_newdev(struct mm_struct *mm, unsigned long ua,
		unsigned long entries, unsigned long dev_hpa,
		struct mm_iommu_table_group_mem_t **pmem);
extern long mm_iommu_put(struct mm_struct *mm,
		struct mm_iommu_table_group_mem_t *mem);
extern void mm_iommu_init(struct mm_struct *mm);
extern void mm_iommu_cleanup(struct mm_struct *mm);
extern struct mm_iommu_table_group_mem_t *mm_iommu_lookup(struct mm_struct *mm,
		unsigned long ua, unsigned long size);
extern struct mm_iommu_table_group_mem_t *mm_iommu_get(struct mm_struct *mm,
		unsigned long ua, unsigned long entries);
extern long mm_iommu_ua_to_hpa(struct mm_iommu_table_group_mem_t *mem,
		unsigned long ua, unsigned int pageshift, unsigned long *hpa);
extern bool mm_iommu_is_devmem(struct mm_struct *mm, unsigned long hpa,
		unsigned int pageshift, unsigned long *size);
extern long mm_iommu_mapped_inc(struct mm_iommu_table_group_mem_t *mem);
extern void mm_iommu_mapped_dec(struct mm_iommu_table_group_mem_t *mem);
#else
static inline bool mm_iommu_is_devmem(struct mm_struct *mm, unsigned long hpa,
		unsigned int pageshift, unsigned long *size)
{
	return false;
}
static inline void mm_iommu_init(struct mm_struct *mm) { }
#endif
extern void switch_slb(struct task_struct *tsk, struct mm_struct *mm);

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

extern int hash__alloc_context_id(void);
void __init hash__reserve_context_id(int id);
extern void __destroy_context(int context_id);
static inline void mmu_context_init(void) { }

#ifdef CONFIG_PPC_64S_HASH_MMU
static inline int alloc_extended_context(struct mm_struct *mm,
					 unsigned long ea)
{
	int context_id;

	int index = ea >> MAX_EA_BITS_PER_CONTEXT;

	context_id = hash__alloc_context_id();
	if (context_id < 0)
		return context_id;

	VM_WARN_ON(mm->context.extended_id[index]);
	mm->context.extended_id[index] = context_id;
	return context_id;
}

static inline bool need_extra_context(struct mm_struct *mm, unsigned long ea)
{
	int context_id;

	context_id = get_user_context(&mm->context, ea);
	if (!context_id)
		return true;
	return false;
}
#endif

#else
extern void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk);
extern unsigned long __init_new_context(void);
extern void __destroy_context(unsigned long context_id);
extern void mmu_context_init(void);
static inline int alloc_extended_context(struct mm_struct *mm,
					 unsigned long ea)
{
	/* non book3s_64 should never find this called */
	WARN_ON(1);
	return -ENOMEM;
}

static inline bool need_extra_context(struct mm_struct *mm, unsigned long ea)
{
	return false;
}
#endif

extern void switch_cop(struct mm_struct *next);
extern int use_cop(unsigned long acop, struct mm_struct *mm);
extern void drop_cop(unsigned long acop, struct mm_struct *mm);

#ifdef CONFIG_PPC_BOOK3S_64
static inline void inc_mm_active_cpus(struct mm_struct *mm)
{
	atomic_inc(&mm->context.active_cpus);
}

static inline void dec_mm_active_cpus(struct mm_struct *mm)
{
	atomic_dec(&mm->context.active_cpus);
}

static inline void mm_context_add_copro(struct mm_struct *mm)
{
	/*
	 * If any copro is in use, increment the active CPU count
	 * in order to force TLB invalidations to be global as to
	 * propagate to the Nest MMU.
	 */
	if (atomic_inc_return(&mm->context.copros) == 1)
		inc_mm_active_cpus(mm);
}

static inline void mm_context_remove_copro(struct mm_struct *mm)
{
	int c;

	/*
	 * When removing the last copro, we need to broadcast a global
	 * flush of the full mm, as the next TLBI may be local and the
	 * nMMU and/or PSL need to be cleaned up.
	 *
	 * Both the 'copros' and 'active_cpus' counts are looked at in
	 * flush_all_mm() to determine the scope (local/global) of the
	 * TLBIs, so we need to flush first before decrementing
	 * 'copros'. If this API is used by several callers for the
	 * same context, it can lead to over-flushing. It's hopefully
	 * not common enough to be a problem.
	 *
	 * Skip on hash, as we don't know how to do the proper flush
	 * for the time being. Invalidations will remain global if
	 * used on hash. Note that we can't drop 'copros' either, as
	 * it could make some invalidations local with no flush
	 * in-between.
	 */
	if (radix_enabled()) {
		flush_all_mm(mm);

		c = atomic_dec_if_positive(&mm->context.copros);
		/* Detect imbalance between add and remove */
		WARN_ON(c < 0);

		if (c == 0)
			dec_mm_active_cpus(mm);
	}
}

/*
 * vas_windows counter shows number of open windows in the mm
 * context. During context switch, use this counter to clear the
 * foreign real address mapping (CP_ABORT) for the thread / process
 * that intend to use COPY/PASTE. When a process closes all windows,
 * disable CP_ABORT which is expensive to run.
 *
 * For user context, register a copro so that TLBIs are seen by the
 * nest MMU. mm_context_add/remove_vas_window() are used only for user
 * space windows.
 */
static inline void mm_context_add_vas_window(struct mm_struct *mm)
{
	atomic_inc(&mm->context.vas_windows);
	mm_context_add_copro(mm);
}

static inline void mm_context_remove_vas_window(struct mm_struct *mm)
{
	int v;

	mm_context_remove_copro(mm);
	v = atomic_dec_if_positive(&mm->context.vas_windows);

	/* Detect imbalance between add and remove */
	WARN_ON(v < 0);
}
#else
static inline void inc_mm_active_cpus(struct mm_struct *mm) { }
static inline void dec_mm_active_cpus(struct mm_struct *mm) { }
static inline void mm_context_add_copro(struct mm_struct *mm) { }
static inline void mm_context_remove_copro(struct mm_struct *mm) { }
#endif

#if defined(CONFIG_KVM_BOOK3S_HV_POSSIBLE) && defined(CONFIG_PPC_RADIX_MMU)
void do_h_rpt_invalidate_prt(unsigned long pid, unsigned long lpid,
			     unsigned long type, unsigned long pg_sizes,
			     unsigned long start, unsigned long end);
#else
static inline void do_h_rpt_invalidate_prt(unsigned long pid,
					   unsigned long lpid,
					   unsigned long type,
					   unsigned long pg_sizes,
					   unsigned long start,
					   unsigned long end) { }
#endif

extern void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk);

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm_irqs_off(prev, next, tsk);
	local_irq_restore(flags);
}
#define switch_mm_irqs_off switch_mm_irqs_off

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
#define activate_mm activate_mm
static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	switch_mm_irqs_off(prev, next, current);
}

/* We don't currently use enter_lazy_tlb() for anything */
#ifdef CONFIG_PPC_BOOK3E_64
#define enter_lazy_tlb enter_lazy_tlb
static inline void enter_lazy_tlb(struct mm_struct *mm,
				  struct task_struct *tsk)
{
	/* 64-bit Book3E keeps track of current PGD in the PACA */
	get_paca()->pgd = NULL;
}
#endif

extern void arch_exit_mmap(struct mm_struct *mm);

static inline void arch_unmap(struct mm_struct *mm,
			      unsigned long start, unsigned long end)
{
	unsigned long vdso_base = (unsigned long)mm->context.vdso;

	if (start <= vdso_base && vdso_base < end)
		mm->context.vdso = NULL;
}

#ifdef CONFIG_PPC_MEM_KEYS
bool arch_vma_access_permitted(struct vm_area_struct *vma, bool write,
			       bool execute, bool foreign);
void arch_dup_pkeys(struct mm_struct *oldmm, struct mm_struct *mm);
#else /* CONFIG_PPC_MEM_KEYS */
static inline bool arch_vma_access_permitted(struct vm_area_struct *vma,
		bool write, bool execute, bool foreign)
{
	/* by default, allow everything */
	return true;
}

#define pkey_mm_init(mm)
#define arch_dup_pkeys(oldmm, mm)

static inline u64 pte_to_hpte_pkey_bits(u64 pteflags, unsigned long flags)
{
	return 0x0UL;
}

#endif /* CONFIG_PPC_MEM_KEYS */

static inline int arch_dup_mmap(struct mm_struct *oldmm,
				struct mm_struct *mm)
{
	arch_dup_pkeys(oldmm, mm);
	return 0;
}

#include <asm-generic/mmu_context.h>

#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_MMU_CONTEXT_H */
