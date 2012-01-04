/*
 * MM context support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef _ASM_MMU_CONTEXT_H
#define _ASM_MMU_CONTEXT_H

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/mem-layout.h>

static inline void destroy_context(struct mm_struct *mm)
{
}

/*
 * VM port hides all TLB management, so "lazy TLB" isn't very
 * meaningful.  Even for ports to architectures with visble TLBs,
 * this is almost invariably a null function.
 */
static inline void enter_lazy_tlb(struct mm_struct *mm,
	struct task_struct *tsk)
{
}

/*
 * Architecture-specific actions, if any, for memory map deactivation.
 */
static inline void deactivate_mm(struct task_struct *tsk,
	struct mm_struct *mm)
{
}

/**
 * init_new_context - initialize context related info for new mm_struct instance
 * @tsk: pointer to a task struct
 * @mm: pointer to a new mm struct
 */
static inline int init_new_context(struct task_struct *tsk,
					struct mm_struct *mm)
{
	/* mm->context is set up by pgd_alloc */
	return 0;
}

/*
 *  Switch active mm context
 */
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
				struct task_struct *tsk)
{
	int l1;

	/*
	 * For virtual machine, we have to update system map if it's been
	 * touched.
	 */
	if (next->context.generation < prev->context.generation) {
		for (l1 = MIN_KERNEL_SEG; l1 <= max_kernel_seg; l1++)
			next->pgd[l1] = init_mm.pgd[l1];

		next->context.generation = prev->context.generation;
	}

	__vmnewmap((void *)next->context.ptbase);
}

/*
 *  Activate new memory map for task
 */
static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm(prev, next, current_thread_info()->task);
	local_irq_restore(flags);
}

/*  Generic hooks for arch_dup_mmap and arch_exit_mmap  */
#include <asm-generic/mm_hooks.h>

#endif
