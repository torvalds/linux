/*
 *  include/asm-s390/mmu_context.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/mmu_context.h"
 */

#ifndef __S390_MMU_CONTEXT_H
#define __S390_MMU_CONTEXT_H

#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm-generic/mm_hooks.h>

static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	mm->context.asce_bits = _ASCE_TABLE_LENGTH | _ASCE_USER_BITS;
#ifdef CONFIG_64BIT
	mm->context.asce_bits |= _ASCE_TYPE_REGION3;
#endif
	if (current->mm->context.alloc_pgste) {
		/*
		 * alloc_pgste indicates, that any NEW context will be created
		 * with extended page tables. The old context is unchanged. The
		 * page table allocation and the page table operations will
		 * look at has_pgste to distinguish normal and extended page
		 * tables. The only way to create extended page tables is to
		 * set alloc_pgste and then create a new context (e.g. dup_mm).
		 * The page table allocation is called after init_new_context
		 * and if has_pgste is set, it will create extended page
		 * tables.
		 */
		mm->context.noexec = 0;
		mm->context.has_pgste = 1;
		mm->context.alloc_pgste = 1;
	} else {
		mm->context.noexec = s390_noexec;
		mm->context.has_pgste = 0;
		mm->context.alloc_pgste = 0;
	}
	mm->context.asce_limit = STACK_TOP_MAX;
	crst_table_init((unsigned long *) mm->pgd, pgd_entry_type(mm));
	return 0;
}

#define destroy_context(mm)             do { } while (0)

#ifndef __s390x__
#define LCTL_OPCODE "lctl"
#else
#define LCTL_OPCODE "lctlg"
#endif

static inline void update_mm(struct mm_struct *mm, struct task_struct *tsk)
{
	pgd_t *pgd = mm->pgd;

	S390_lowcore.user_asce = mm->context.asce_bits | __pa(pgd);
	if (switch_amode) {
		/* Load primary space page table origin. */
		pgd = mm->context.noexec ? get_shadow_table(pgd) : pgd;
		S390_lowcore.user_exec_asce = mm->context.asce_bits | __pa(pgd);
		asm volatile(LCTL_OPCODE" 1,1,%0\n"
			     : : "m" (S390_lowcore.user_exec_asce) );
	} else
		/* Load home space page table origin. */
		asm volatile(LCTL_OPCODE" 13,13,%0"
			     : : "m" (S390_lowcore.user_asce) );
	set_fs(current->thread.mm_segment);
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	cpu_set(smp_processor_id(), next->cpu_vm_mask);
	update_mm(next, tsk);
}

#define enter_lazy_tlb(mm,tsk)	do { } while (0)
#define deactivate_mm(tsk,mm)	do { } while (0)

static inline void activate_mm(struct mm_struct *prev,
                               struct mm_struct *next)
{
        switch_mm(prev, next, current);
}

#endif /* __S390_MMU_CONTEXT_H */
