/*
 *  MMU context allocation for 64-bit kernels.
 *
 *  Copyright (C) 2004 Anton Blanchard, IBM Corp. <anton@samba.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/slab.h>

#include <asm/mmu_context.h>

#include "icswx.h"

static DEFINE_SPINLOCK(mmu_context_lock);
static DEFINE_IDA(mmu_context_ida);

/*
 * 256MB segment
 * The proto-VSID space has 2^(CONTEX_BITS + USER_ESID_BITS) - 1 segments
 * available for user mappings. Each segment contains 2^28 bytes. Each
 * context maps 2^46 bytes (64TB) so we can support 2^19-1 contexts
 * (19 == 37 + 28 - 46).
 */
#define MAX_CONTEXT	((1UL << CONTEXT_BITS) - 1)

int __init_new_context(void)
{
	int index;
	int err;

again:
	if (!ida_pre_get(&mmu_context_ida, GFP_KERNEL))
		return -ENOMEM;

	spin_lock(&mmu_context_lock);
	err = ida_get_new_above(&mmu_context_ida, 1, &index);
	spin_unlock(&mmu_context_lock);

	if (err == -EAGAIN)
		goto again;
	else if (err)
		return err;

	if (index > MAX_CONTEXT) {
		spin_lock(&mmu_context_lock);
		ida_remove(&mmu_context_ida, index);
		spin_unlock(&mmu_context_lock);
		return -ENOMEM;
	}

	return index;
}
EXPORT_SYMBOL_GPL(__init_new_context);

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	int index;

	index = __init_new_context();
	if (index < 0)
		return index;

	/* The old code would re-promote on fork, we don't do that
	 * when using slices as it could cause problem promoting slices
	 * that have been forced down to 4K
	 */
	if (slice_mm_new_context(mm))
		slice_set_user_psize(mm, mmu_virtual_psize);
	subpage_prot_init_new_context(mm);
	mm->context.id = index;
#ifdef CONFIG_PPC_ICSWX
	mm->context.cop_lockp = kmalloc(sizeof(spinlock_t), GFP_KERNEL);
	if (!mm->context.cop_lockp) {
		__destroy_context(index);
		subpage_prot_free(mm);
		mm->context.id = MMU_NO_CONTEXT;
		return -ENOMEM;
	}
	spin_lock_init(mm->context.cop_lockp);
#endif /* CONFIG_PPC_ICSWX */

	return 0;
}

void __destroy_context(int context_id)
{
	spin_lock(&mmu_context_lock);
	ida_remove(&mmu_context_ida, context_id);
	spin_unlock(&mmu_context_lock);
}
EXPORT_SYMBOL_GPL(__destroy_context);

void destroy_context(struct mm_struct *mm)
{
#ifdef CONFIG_PPC_ICSWX
	drop_cop(mm->context.acop, mm);
	kfree(mm->context.cop_lockp);
	mm->context.cop_lockp = NULL;
#endif /* CONFIG_PPC_ICSWX */
	__destroy_context(mm->context.id);
	subpage_prot_free(mm);
	mm->context.id = MMU_NO_CONTEXT;
}
