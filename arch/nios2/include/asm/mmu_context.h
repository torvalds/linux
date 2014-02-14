/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 1996, 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 *
 * based on MIPS asm/mmu_context.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_MMU_CONTEXT_H
#define _ASM_NIOS2_MMU_CONTEXT_H

#include <asm-generic/mm_hooks.h>

extern void mmu_context_init(void);
extern unsigned long get_pid_from_context(mm_context_t *ctx);

/*
 * For the fast tlb miss handlers, we keep a pointer to the current pgd.
 * processor.
 */
extern pgd_t *pgd_current;

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 * Initialize the context related info for a new mm_struct instance.
 *
 * Set all new contexts to 0, that way the generation will never match
 * the currently running generation when this context is switched in.
 */
static inline int init_new_context(struct task_struct *tsk,
					struct mm_struct *mm)
{
	mm->context = 0;
	return 0;
}

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
		struct task_struct *tsk);

static inline void deactivate_mm(struct task_struct *tsk,
				struct mm_struct *mm)
{
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
void activate_mm(struct mm_struct *prev, struct mm_struct *next);

#endif /* _ASM_NIOS2_MMU_CONTEXT_H */
