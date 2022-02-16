// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include "mali_kbase_csf_heap_context_alloc.h"

/* Size of one heap context structure, in bytes. */
#define HEAP_CTX_SIZE ((size_t)32)

/* Total size of the GPU memory region allocated for heap contexts, in bytes. */
#define HEAP_CTX_REGION_SIZE (MAX_TILER_HEAPS * HEAP_CTX_SIZE)

/**
 * sub_alloc - Sub-allocate a heap context from a GPU memory region
 *
 * @ctx_alloc: Pointer to the heap context allocator.
 *
 * Return: GPU virtual address of the allocated heap context or 0 on failure.
 */
static u64 sub_alloc(struct kbase_csf_heap_context_allocator *const ctx_alloc)
{
	struct kbase_context *const kctx = ctx_alloc->kctx;
	int heap_nr = 0;
	size_t ctx_offset = 0;
	u64 heap_gpu_va = 0;
	struct kbase_vmap_struct mapping;
	void *ctx_ptr = NULL;

	lockdep_assert_held(&ctx_alloc->lock);

	heap_nr = find_first_zero_bit(ctx_alloc->in_use,
		MAX_TILER_HEAPS);

	if (unlikely(heap_nr >= MAX_TILER_HEAPS)) {
		dev_dbg(kctx->kbdev->dev,
			"No free tiler heap contexts in the pool");
		return 0;
	}

	ctx_offset = heap_nr * HEAP_CTX_SIZE;
	heap_gpu_va = ctx_alloc->gpu_va + ctx_offset;
	ctx_ptr = kbase_vmap_prot(kctx, heap_gpu_va,
		HEAP_CTX_SIZE, KBASE_REG_CPU_WR, &mapping);

	if (unlikely(!ctx_ptr)) {
		dev_err(kctx->kbdev->dev,
			"Failed to map tiler heap context %d (0x%llX)\n",
			heap_nr, heap_gpu_va);
		return 0;
	}

	memset(ctx_ptr, 0, HEAP_CTX_SIZE);
	kbase_vunmap(ctx_ptr, &mapping);

	bitmap_set(ctx_alloc->in_use, heap_nr, 1);

	dev_dbg(kctx->kbdev->dev, "Allocated tiler heap context %d (0x%llX)\n",
		heap_nr, heap_gpu_va);

	return heap_gpu_va;
}

/**
 * sub_free - Free a heap context sub-allocated from a GPU memory region
 *
 * @ctx_alloc:   Pointer to the heap context allocator.
 * @heap_gpu_va: The GPU virtual address of a heap context structure to free.
 */
static void sub_free(struct kbase_csf_heap_context_allocator *const ctx_alloc,
	u64 const heap_gpu_va)
{
	struct kbase_context *const kctx = ctx_alloc->kctx;
	u64 ctx_offset = 0;
	unsigned int heap_nr = 0;

	lockdep_assert_held(&ctx_alloc->lock);

	if (WARN_ON(!ctx_alloc->region))
		return;

	if (WARN_ON(heap_gpu_va < ctx_alloc->gpu_va))
		return;

	ctx_offset = heap_gpu_va - ctx_alloc->gpu_va;

	if (WARN_ON(ctx_offset >= HEAP_CTX_REGION_SIZE) ||
		WARN_ON(ctx_offset % HEAP_CTX_SIZE))
		return;

	heap_nr = ctx_offset / HEAP_CTX_SIZE;
	dev_dbg(kctx->kbdev->dev,
		"Freed tiler heap context %d (0x%llX)\n", heap_nr, heap_gpu_va);

	bitmap_clear(ctx_alloc->in_use, heap_nr, 1);
}

int kbase_csf_heap_context_allocator_init(
	struct kbase_csf_heap_context_allocator *const ctx_alloc,
	struct kbase_context *const kctx)
{
	/* We cannot pre-allocate GPU memory here because the
	 * custom VA zone may not have been created yet.
	 */
	ctx_alloc->kctx = kctx;
	ctx_alloc->region = NULL;
	ctx_alloc->gpu_va = 0;

	mutex_init(&ctx_alloc->lock);
	bitmap_zero(ctx_alloc->in_use, MAX_TILER_HEAPS);

	dev_dbg(kctx->kbdev->dev,
		"Initialized a tiler heap context allocator\n");

	return 0;
}

void kbase_csf_heap_context_allocator_term(
	struct kbase_csf_heap_context_allocator *const ctx_alloc)
{
	struct kbase_context *const kctx = ctx_alloc->kctx;

	dev_dbg(kctx->kbdev->dev,
		"Terminating tiler heap context allocator\n");

	if (ctx_alloc->region) {
		kbase_gpu_vm_lock(kctx);
		ctx_alloc->region->flags &= ~KBASE_REG_NO_USER_FREE;
		kbase_mem_free_region(kctx, ctx_alloc->region);
		kbase_gpu_vm_unlock(kctx);
	}

	mutex_destroy(&ctx_alloc->lock);
}

u64 kbase_csf_heap_context_allocator_alloc(
	struct kbase_csf_heap_context_allocator *const ctx_alloc)
{
	struct kbase_context *const kctx = ctx_alloc->kctx;
	u64 flags = BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR |
		BASE_MEM_PROT_CPU_WR | BASEP_MEM_NO_USER_FREE;
	u64 nr_pages = PFN_UP(HEAP_CTX_REGION_SIZE);
	u64 heap_gpu_va = 0;

	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

#ifdef CONFIG_MALI_VECTOR_DUMP
	flags |= BASE_MEM_PROT_CPU_RD;
#endif

	mutex_lock(&ctx_alloc->lock);

	/* If the pool of heap contexts wasn't already allocated then
	 * allocate it.
	 */
	if (!ctx_alloc->region) {
		ctx_alloc->region = kbase_mem_alloc(kctx, nr_pages, nr_pages, 0, &flags,
						    &ctx_alloc->gpu_va, mmu_sync_info);
	}

	/* If the pool still isn't allocated then an error occurred. */
	if (unlikely(!ctx_alloc->region))
		dev_dbg(kctx->kbdev->dev, "Failed to allocate a pool of tiler heap contexts");
	else
		heap_gpu_va = sub_alloc(ctx_alloc);

	mutex_unlock(&ctx_alloc->lock);

	return heap_gpu_va;
}

void kbase_csf_heap_context_allocator_free(
	struct kbase_csf_heap_context_allocator *const ctx_alloc,
	u64 const heap_gpu_va)
{
	mutex_lock(&ctx_alloc->lock);
	sub_free(ctx_alloc, heap_gpu_va);
	mutex_unlock(&ctx_alloc->lock);
}
