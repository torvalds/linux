#ifndef __CRIS_MMU_CONTEXT_H
#define __CRIS_MMU_CONTEXT_H

#include <asm-generic/mm_hooks.h>

extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void get_mmu_context(struct mm_struct *mm);
extern void destroy_context(struct mm_struct *mm);
extern void switch_mm(struct mm_struct *prev, struct mm_struct *next,
		      struct task_struct *tsk);

#define deactivate_mm(tsk,mm)	do { } while (0)

#define activate_mm(prev,next) switch_mm((prev),(next),NULL)

/* current active pgd - this is similar to other processors pgd 
 * registers like cr3 on the i386
 */

/* defined in arch/cris/mm/fault.c */
DECLARE_PER_CPU(pgd_t *, current_pgd);

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

#endif
