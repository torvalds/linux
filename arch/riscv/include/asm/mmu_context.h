/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_MMU_CONTEXT_H
#define _ASM_RISCV_MMU_CONTEXT_H

#include <linux/mm_types.h>
#include <asm-generic/mm_hooks.h>

#include <linux/mm.h>
#include <linux/sched.h>

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	struct task_struct *task);

#define activate_mm activate_mm
static inline void activate_mm(struct mm_struct *prev,
			       struct mm_struct *next)
{
#ifdef CONFIG_RISCV_ISA_SUPM
	next->context.pmlen = 0;
#endif
	switch_mm(prev, next, NULL);
}

#define init_new_context init_new_context
static inline int init_new_context(struct task_struct *tsk,
			struct mm_struct *mm)
{
#ifdef CONFIG_MMU
	atomic_long_set(&mm->context.id, 0);
#endif
	if (IS_ENABLED(CONFIG_RISCV_ISA_SUPM))
		clear_bit(MM_CONTEXT_LOCK_PMLEN, &mm->context.flags);
	return 0;
}

DECLARE_STATIC_KEY_FALSE(use_asid_allocator);

#ifdef CONFIG_RISCV_ISA_SUPM
#define mm_untag_mask mm_untag_mask
static inline unsigned long mm_untag_mask(struct mm_struct *mm)
{
	return -1UL >> mm->context.pmlen;
}
#endif

#include <asm-generic/mmu_context.h>

#endif /* _ASM_RISCV_MMU_CONTEXT_H */
