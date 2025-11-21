/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __UM_MMU_CONTEXT_H
#define __UM_MMU_CONTEXT_H

#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>

#include <asm/mm_hooks.h>
#include <asm/mmu.h>

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk)
{
}

#define init_new_context init_new_context
extern int init_new_context(struct task_struct *task, struct mm_struct *mm);

#define destroy_context destroy_context
extern void destroy_context(struct mm_struct *mm);

#include <asm-generic/mmu_context.h>

#endif
