#ifndef __METAG_MMU_CONTEXT_H
#define __METAG_MMU_CONTEXT_H

#include <asm-generic/mm_hooks.h>

#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

#include <linux/io.h>

static inline void enter_lazy_tlb(struct mm_struct *mm,
				  struct task_struct *tsk)
{
}

static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
#ifndef CONFIG_METAG_META21_MMU
	/* We use context to store a pointer to the page holding the
	 * pgd of a process while it is running. While a process is not
	 * running the pgd and context fields should be equal.
	 */
	mm->context.pgd_base = (unsigned long) mm->pgd;
#endif
#ifdef CONFIG_METAG_USER_TCM
	INIT_LIST_HEAD(&mm->context.tcm);
#endif
	return 0;
}

#ifdef CONFIG_METAG_USER_TCM

#include <linux/slab.h>
#include <asm/tcm.h>

static inline void destroy_context(struct mm_struct *mm)
{
	struct tcm_allocation *pos, *n;

	list_for_each_entry_safe(pos, n,  &mm->context.tcm, list) {
		tcm_free(pos->tag, pos->addr, pos->size);
		list_del(&pos->list);
		kfree(pos);
	}
}
#else
#define destroy_context(mm)		do { } while (0)
#endif

#ifdef CONFIG_METAG_META21_MMU
static inline void load_pgd(pgd_t *pgd, int thread)
{
	unsigned long phys0 = mmu_phys0_addr(thread);
	unsigned long phys1 = mmu_phys1_addr(thread);

	/*
	 *  0x900 2Gb address space
	 *  The permission bits apply to MMU table region which gives a 2MB
	 *  window into physical memory. We especially don't want userland to be
	 *  able to access this.
	 */
	metag_out32(0x900 | _PAGE_CACHEABLE | _PAGE_PRIV | _PAGE_WRITE |
		    _PAGE_PRESENT, phys0);
	/* Set new MMU base address */
	metag_out32(__pa(pgd) & MMCU_TBLPHYS1_ADDR_BITS, phys1);
}
#endif

static inline void switch_mmu(struct mm_struct *prev, struct mm_struct *next)
{
#ifdef CONFIG_METAG_META21_MMU
	load_pgd(next->pgd, hard_processor_id());
#else
	unsigned int i;

	/* prev->context == prev->pgd in the case where we are initially
	   switching from the init task to the first process. */
	if (prev->context.pgd_base != (unsigned long) prev->pgd) {
		for (i = FIRST_USER_PGD_NR; i < USER_PTRS_PER_PGD; i++)
			((pgd_t *) prev->context.pgd_base)[i] = prev->pgd[i];
	} else
		prev->pgd = (pgd_t *)mmu_get_base();

	next->pgd = prev->pgd;
	prev->pgd = (pgd_t *) prev->context.pgd_base;

	for (i = FIRST_USER_PGD_NR; i < USER_PTRS_PER_PGD; i++)
		next->pgd[i] = ((pgd_t *) next->context.pgd_base)[i];

	flush_cache_all();
#endif
	flush_tlb_all();
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	if (prev != next)
		switch_mmu(prev, next);
}

static inline void activate_mm(struct mm_struct *prev_mm,
			       struct mm_struct *next_mm)
{
	switch_mmu(prev_mm, next_mm);
}

#define deactivate_mm(tsk, mm)   do { } while (0)

#endif
