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
#include <linux/pkeys.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/slab.h>

#include <asm/mmu_context.h>
#include <asm/pgalloc.h>

static DEFINE_IDA(mmu_context_ida);

static int alloc_context_id(int min_id, int max_id)
{
	return ida_alloc_range(&mmu_context_ida, min_id, max_id, GFP_KERNEL);
}

void hash__reserve_context_id(int id)
{
	int result = ida_alloc_range(&mmu_context_ida, id, id, GFP_KERNEL);

	WARN(result != id, "mmu: Failed to reserve context id %d (rc %d)\n", id, result);
}

int hash__alloc_context_id(void)
{
	unsigned long max;

	if (mmu_has_feature(MMU_FTR_68_BIT_VA))
		max = MAX_USER_CONTEXT;
	else
		max = MAX_USER_CONTEXT_65BIT_VA;

	return alloc_context_id(MIN_USER_CONTEXT, max);
}
EXPORT_SYMBOL_GPL(hash__alloc_context_id);

void slb_setup_new_exec(void);

static int realloc_context_ids(mm_context_t *ctx)
{
	int i, id;

	/*
	 * id 0 (aka. ctx->id) is special, we always allocate a new one, even if
	 * there wasn't one allocated previously (which happens in the exec
	 * case where ctx is newly allocated).
	 *
	 * We have to be a bit careful here. We must keep the existing ids in
	 * the array, so that we can test if they're non-zero to decide if we
	 * need to allocate a new one. However in case of error we must free the
	 * ids we've allocated but *not* any of the existing ones (or risk a
	 * UAF). That's why we decrement i at the start of the error handling
	 * loop, to skip the id that we just tested but couldn't reallocate.
	 */
	for (i = 0; i < ARRAY_SIZE(ctx->extended_id); i++) {
		if (i == 0 || ctx->extended_id[i]) {
			id = hash__alloc_context_id();
			if (id < 0)
				goto error;

			ctx->extended_id[i] = id;
		}
	}

	/* The caller expects us to return id */
	return ctx->id;

error:
	for (i--; i >= 0; i--) {
		if (ctx->extended_id[i])
			ida_free(&mmu_context_ida, ctx->extended_id[i]);
	}

	return id;
}

static int hash__init_new_context(struct mm_struct *mm)
{
	int index;

	mm->context.hash_context = kmalloc(sizeof(struct hash_mm_context),
					   GFP_KERNEL);
	if (!mm->context.hash_context)
		return -ENOMEM;

	/*
	 * The old code would re-promote on fork, we don't do that when using
	 * slices as it could cause problem promoting slices that have been
	 * forced down to 4K.
	 *
	 * For book3s we have MMU_NO_CONTEXT set to be ~0. Hence check
	 * explicitly against context.id == 0. This ensures that we properly
	 * initialize context slice details for newly allocated mm's (which will
	 * have id == 0) and don't alter context slice inherited via fork (which
	 * will have id != 0).
	 *
	 * We should not be calling init_new_context() on init_mm. Hence a
	 * check against 0 is OK.
	 */
	if (mm->context.id == 0) {
		memset(mm->context.hash_context, 0, sizeof(struct hash_mm_context));
		slice_init_new_context_exec(mm);
	} else {
		/* This is fork. Copy hash_context details from current->mm */
		memcpy(mm->context.hash_context, current->mm->context.hash_context, sizeof(struct hash_mm_context));
#ifdef CONFIG_PPC_SUBPAGE_PROT
		/* inherit subpage prot detalis if we have one. */
		if (current->mm->context.hash_context->spt) {
			mm->context.hash_context->spt = kmalloc(sizeof(struct subpage_prot_table),
								GFP_KERNEL);
			if (!mm->context.hash_context->spt) {
				kfree(mm->context.hash_context);
				return -ENOMEM;
			}
		}
#endif
	}

	index = realloc_context_ids(&mm->context);
	if (index < 0) {
#ifdef CONFIG_PPC_SUBPAGE_PROT
		kfree(mm->context.hash_context->spt);
#endif
		kfree(mm->context.hash_context);
		return index;
	}

	pkey_mm_init(mm);
	return index;
}

void hash__setup_new_exec(void)
{
	slice_setup_new_exec();

	slb_setup_new_exec();
}

static int radix__init_new_context(struct mm_struct *mm)
{
	unsigned long rts_field;
	int index, max_id;

	max_id = (1 << mmu_pid_bits) - 1;
	index = alloc_context_id(mmu_base_pid, max_id);
	if (index < 0)
		return index;

	/*
	 * set the process table entry,
	 */
	rts_field = radix__get_tree_size();
	process_tb[index].prtb0 = cpu_to_be64(rts_field | __pa(mm->pgd) | RADIX_PGD_INDEX_SIZE);

	/*
	 * Order the above store with subsequent update of the PID
	 * register (at which point HW can start loading/caching
	 * the entry) and the corresponding load by the MMU from
	 * the L2 cache.
	 */
	asm volatile("ptesync;isync" : : : "memory");

	mm->context.npu_context = NULL;
	mm->context.hash_context = NULL;

	return index;
}

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	int index;

	if (radix_enabled())
		index = radix__init_new_context(mm);
	else
		index = hash__init_new_context(mm);

	if (index < 0)
		return index;

	mm->context.id = index;

	mm->context.pte_frag = NULL;
	mm->context.pmd_frag = NULL;
#ifdef CONFIG_SPAPR_TCE_IOMMU
	mm_iommu_init(mm);
#endif
	atomic_set(&mm->context.active_cpus, 0);
	atomic_set(&mm->context.copros, 0);

	return 0;
}

void __destroy_context(int context_id)
{
	ida_free(&mmu_context_ida, context_id);
}
EXPORT_SYMBOL_GPL(__destroy_context);

static void destroy_contexts(mm_context_t *ctx)
{
	int index, context_id;

	for (index = 0; index < ARRAY_SIZE(ctx->extended_id); index++) {
		context_id = ctx->extended_id[index];
		if (context_id)
			ida_free(&mmu_context_ida, context_id);
	}
	kfree(ctx->hash_context);
}

static void pmd_frag_destroy(void *pmd_frag)
{
	int count;
	struct page *page;

	page = virt_to_page(pmd_frag);
	/* drop all the pending references */
	count = ((unsigned long)pmd_frag & ~PAGE_MASK) >> PMD_FRAG_SIZE_SHIFT;
	/* We allow PTE_FRAG_NR fragments from a PTE page */
	if (atomic_sub_and_test(PMD_FRAG_NR - count, &page->pt_frag_refcount)) {
		pgtable_pmd_page_dtor(page);
		__free_page(page);
	}
}

static void destroy_pagetable_cache(struct mm_struct *mm)
{
	void *frag;

	frag = mm->context.pte_frag;
	if (frag)
		pte_frag_destroy(frag);

	frag = mm->context.pmd_frag;
	if (frag)
		pmd_frag_destroy(frag);
	return;
}

void destroy_context(struct mm_struct *mm)
{
#ifdef CONFIG_SPAPR_TCE_IOMMU
	WARN_ON_ONCE(!list_empty(&mm->context.iommu_group_mem_list));
#endif
	if (radix_enabled())
		WARN_ON(process_tb[mm->context.id].prtb0 != 0);
	else
		subpage_prot_free(mm);
	destroy_contexts(&mm->context);
	mm->context.id = MMU_NO_CONTEXT;
}

void arch_exit_mmap(struct mm_struct *mm)
{
	destroy_pagetable_cache(mm);

	if (radix_enabled()) {
		/*
		 * Radix doesn't have a valid bit in the process table
		 * entries. However we know that at least P9 implementation
		 * will avoid caching an entry with an invalid RTS field,
		 * and 0 is invalid. So this will do.
		 *
		 * This runs before the "fullmm" tlb flush in exit_mmap,
		 * which does a RIC=2 tlbie to clear the process table
		 * entry. See the "fullmm" comments in tlb-radix.c.
		 *
		 * No barrier required here after the store because
		 * this process will do the invalidate, which starts with
		 * ptesync.
		 */
		process_tb[mm->context.id].prtb0 = 0;
	}
}

#ifdef CONFIG_PPC_RADIX_MMU
void radix__switch_mmu_context(struct mm_struct *prev, struct mm_struct *next)
{
	mtspr(SPRN_PID, next->context.id);
	isync();
}
#endif
