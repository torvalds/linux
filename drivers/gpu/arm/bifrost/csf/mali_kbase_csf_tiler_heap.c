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

#include <tl/mali_kbase_tracepoints.h>

#include "mali_kbase_csf_tiler_heap.h"
#include "mali_kbase_csf_tiler_heap_def.h"
#include "mali_kbase_csf_heap_context_alloc.h"

/**
 * encode_chunk_ptr - Encode the address and size of a chunk as an integer.
 *
 * The size and address of the next chunk in a list are packed into a single
 * 64-bit value for storage in a chunk's header. This function returns that
 * value.
 *
 * @chunk_size: Size of a tiler heap chunk, in bytes.
 * @chunk_addr: GPU virtual address of the same tiler heap chunk.
 *
 * Return: Next chunk pointer suitable for writing into a chunk header.
 */
static u64 encode_chunk_ptr(u32 const chunk_size, u64 const chunk_addr)
{
	u64 encoded_size, encoded_addr;

	WARN_ON(chunk_size & ~CHUNK_SIZE_MASK);
	WARN_ON(chunk_addr & ~CHUNK_ADDR_MASK);

	encoded_size =
		(u64)(chunk_size >> CHUNK_HDR_NEXT_SIZE_ENCODE_SHIFT) <<
		CHUNK_HDR_NEXT_SIZE_POS;

	encoded_addr =
		(chunk_addr >> CHUNK_HDR_NEXT_ADDR_ENCODE_SHIFT) <<
		CHUNK_HDR_NEXT_ADDR_POS;

	return (encoded_size & CHUNK_HDR_NEXT_SIZE_MASK) |
		(encoded_addr & CHUNK_HDR_NEXT_ADDR_MASK);
}

/**
 * get_last_chunk - Get the last chunk of a tiler heap
 *
 * @heap:  Pointer to the tiler heap.
 *
 * Return: The address of the most recently-linked chunk, or NULL if none.
 */
static struct kbase_csf_tiler_heap_chunk *get_last_chunk(
	struct kbase_csf_tiler_heap *const heap)
{
	lockdep_assert_held(&heap->kctx->csf.tiler_heaps.lock);

	if (list_empty(&heap->chunks_list))
		return NULL;

	return list_last_entry(&heap->chunks_list,
		struct kbase_csf_tiler_heap_chunk, link);
}

/**
 * link_chunk - Link a chunk into a tiler heap
 *
 * Unless the @chunk is the first in the kernel's list of chunks belonging to
 * a given tiler heap, this function stores the size and address of the @chunk
 * in the header of the preceding chunk. This requires the GPU memory region
 * containing the header to be be mapped temporarily, which can fail.
 *
 * @heap:  Pointer to the tiler heap.
 * @chunk: Pointer to the heap chunk to be linked.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int link_chunk(struct kbase_csf_tiler_heap *const heap,
	struct kbase_csf_tiler_heap_chunk *const chunk)
{
	struct kbase_csf_tiler_heap_chunk *const prev = get_last_chunk(heap);

	if (prev) {
		struct kbase_context *const kctx = heap->kctx;
		struct kbase_vmap_struct map;
		u64 *const prev_hdr = kbase_vmap_prot(kctx, prev->gpu_va,
			sizeof(*prev_hdr), KBASE_REG_CPU_WR, &map);

		if (unlikely(!prev_hdr)) {
			dev_err(kctx->kbdev->dev,
				"Failed to map tiler heap chunk 0x%llX\n",
				prev->gpu_va);
			return -ENOMEM;
		}

		*prev_hdr = encode_chunk_ptr(heap->chunk_size, chunk->gpu_va);
		kbase_vunmap(kctx, &map);

		dev_dbg(kctx->kbdev->dev,
			"Linked tiler heap chunks, 0x%llX -> 0x%llX\n",
			prev->gpu_va, chunk->gpu_va);
	}

	return 0;
}

/**
 * init_chunk - Initialize and link a tiler heap chunk
 *
 * Zero-initialize a new chunk's header (including its pointer to the next
 * chunk, which doesn't exist yet) and then update the previous chunk's
 * header to link the new chunk into the chunk list.
 *
 * @heap:  Pointer to the tiler heap.
 * @chunk: Pointer to the heap chunk to be initialized and linked.
 * @link_with_prev: Flag to indicate if the new chunk needs to be linked with
 *                  the previously allocated chunk.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int init_chunk(struct kbase_csf_tiler_heap *const heap,
	struct kbase_csf_tiler_heap_chunk *const chunk, bool link_with_prev)
{
	struct kbase_vmap_struct map;
	struct u64 *chunk_hdr = NULL;
	struct kbase_context *const kctx = heap->kctx;

	if (unlikely(chunk->gpu_va & ~CHUNK_ADDR_MASK)) {
		dev_err(kctx->kbdev->dev,
			"Tiler heap chunk address is unusable\n");
		return -EINVAL;
	}

	chunk_hdr = kbase_vmap_prot(kctx,
		chunk->gpu_va, CHUNK_HDR_SIZE, KBASE_REG_CPU_WR, &map);

	if (unlikely(!chunk_hdr)) {
		dev_err(kctx->kbdev->dev,
			"Failed to map a tiler heap chunk header\n");
		return -ENOMEM;
	}

	memset(chunk_hdr, 0, CHUNK_HDR_SIZE);
	kbase_vunmap(kctx, &map);

	if (link_with_prev)
		return link_chunk(heap, chunk);
	else
		return 0;
}

/**
 * create_chunk - Create a tiler heap chunk
 *
 * This function allocates a chunk of memory for a tiler heap and adds it to
 * the end of the list of chunks associated with that heap. The size of the
 * chunk is not a parameter because it is configured per-heap not per-chunk.
 *
 * @heap: Pointer to the tiler heap for which to allocate memory.
 * @link_with_prev: Flag to indicate if the chunk to be allocated needs to be
 *                  linked with the previously allocated chunk.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int create_chunk(struct kbase_csf_tiler_heap *const heap,
		bool link_with_prev)
{
	int err = 0;
	struct kbase_context *const kctx = heap->kctx;
	u64 nr_pages = PFN_UP(heap->chunk_size);
	u64 flags = BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR |
		BASE_MEM_PROT_CPU_WR | BASEP_MEM_NO_USER_FREE |
		BASE_MEM_COHERENT_LOCAL;
	struct kbase_csf_tiler_heap_chunk *chunk = NULL;

	flags |= base_mem_group_id_set(kctx->jit_group_id);

#if defined(CONFIG_MALI_BIFROST_DEBUG) || defined(CONFIG_MALI_VECTOR_DUMP)
	flags |= BASE_MEM_PROT_CPU_RD;
#endif

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
	if (unlikely(!chunk)) {
		dev_err(kctx->kbdev->dev,
			"No kernel memory for a new tiler heap chunk\n");
		return -ENOMEM;
	}

	/* Allocate GPU memory for the new chunk. */
	INIT_LIST_HEAD(&chunk->link);
	chunk->region = kbase_mem_alloc(kctx, nr_pages, nr_pages, 0,
		&flags, &chunk->gpu_va);

	if (unlikely(!chunk->region)) {
		dev_err(kctx->kbdev->dev,
			"Failed to allocate a tiler heap chunk\n");
		err = -ENOMEM;
	} else {
		err = init_chunk(heap, chunk, link_with_prev);
		if (unlikely(err)) {
			kbase_gpu_vm_lock(kctx);
			chunk->region->flags &= ~KBASE_REG_NO_USER_FREE;
			kbase_mem_free_region(kctx, chunk->region);
			kbase_gpu_vm_unlock(kctx);
		}
	}

	if (unlikely(err)) {
		kfree(chunk);
	} else {
		list_add_tail(&chunk->link, &heap->chunks_list);
		heap->chunk_count++;

		dev_dbg(kctx->kbdev->dev, "Created tiler heap chunk 0x%llX\n",
			chunk->gpu_va);
	}

	return err;
}

/**
 * delete_chunk - Delete a tiler heap chunk
 *
 * This function frees a tiler heap chunk previously allocated by @create_chunk
 * and removes it from the list of chunks associated with the heap.
 *
 * WARNING: The deleted chunk is not unlinked from the list of chunks used by
 *          the GPU, therefore it is only safe to use this function when
 *          deleting a heap.
 *
 * @heap:  Pointer to the tiler heap for which @chunk was allocated.
 * @chunk: Pointer to a chunk to be deleted.
 */
static void delete_chunk(struct kbase_csf_tiler_heap *const heap,
	struct kbase_csf_tiler_heap_chunk *const chunk)
{
	struct kbase_context *const kctx = heap->kctx;

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	kbase_gpu_vm_lock(kctx);
	chunk->region->flags &= ~KBASE_REG_NO_USER_FREE;
	kbase_mem_free_region(kctx, chunk->region);
	kbase_gpu_vm_unlock(kctx);
	list_del(&chunk->link);
	heap->chunk_count--;
	kfree(chunk);
}

/**
 * delete_all_chunks - Delete all chunks belonging to a tiler heap
 *
 * This function empties the list of chunks associated with a tiler heap by
 * freeing all chunks previously allocated by @create_chunk.
 *
 * @heap: Pointer to a tiler heap.
 */
static void delete_all_chunks(struct kbase_csf_tiler_heap *heap)
{
	struct list_head *entry = NULL, *tmp = NULL;
	struct kbase_context *const kctx = heap->kctx;

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	list_for_each_safe(entry, tmp, &heap->chunks_list) {
		struct kbase_csf_tiler_heap_chunk *chunk = list_entry(
			entry, struct kbase_csf_tiler_heap_chunk, link);

		delete_chunk(heap, chunk);
	}
}

/**
 * create_initial_chunks - Create the initial list of chunks for a tiler heap
 *
 * This function allocates a given number of chunks for a tiler heap and
 * adds them to the list of chunks associated with that heap.
 *
 * @heap:    Pointer to the tiler heap for which to allocate memory.
 * @nchunks: Number of chunks to create.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int create_initial_chunks(struct kbase_csf_tiler_heap *const heap,
	u32 const nchunks)
{
	int err = 0;
	u32 i;

	for (i = 0; (i < nchunks) && likely(!err); i++)
		err = create_chunk(heap, true);

	if (unlikely(err))
		delete_all_chunks(heap);

	return err;
}

/**
 * delete_heap - Delete a tiler heap
 *
 * This function frees any chunks allocated for a tiler heap previously
 * initialized by @kbase_csf_tiler_heap_init and removes it from the list of
 * heaps associated with the kbase context. The heap context structure used by
 * the firmware is also freed.
 *
 * @heap: Pointer to a tiler heap to be deleted.
 */
static void delete_heap(struct kbase_csf_tiler_heap *heap)
{
	struct kbase_context *const kctx = heap->kctx;

	dev_dbg(kctx->kbdev->dev, "Deleting tiler heap 0x%llX\n", heap->gpu_va);

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	delete_all_chunks(heap);

	/* We could optimize context destruction by not freeing leaked heap
	 * contexts but it doesn't seem worth the extra complexity.
	 */
	kbase_csf_heap_context_allocator_free(&kctx->csf.tiler_heaps.ctx_alloc,
		heap->gpu_va);

	list_del(&heap->link);

	WARN_ON(heap->chunk_count);
	KBASE_TLSTREAM_AUX_TILER_HEAP_STATS(kctx->kbdev, kctx->id,
		heap->heap_id, 0, 0, heap->max_chunks, heap->chunk_size, 0,
		heap->target_in_flight, 0);

	kfree(heap);
}

/**
 * find_tiler_heap - Find a tiler heap from the address of its heap context
 *
 * Each tiler heap managed by the kernel has an associated heap context
 * structure used by the firmware. This function finds a tiler heap object from
 * the GPU virtual address of its associated heap context. The heap context
 * should have been allocated by @kbase_csf_heap_context_allocator_alloc in the
 * same @kctx.
 *
 * @kctx:        Pointer to the kbase context to search for a tiler heap.
 * @heap_gpu_va: GPU virtual address of a heap context structure.
 *
 * Return: pointer to the tiler heap object, or NULL if not found.
 */
static struct kbase_csf_tiler_heap *find_tiler_heap(
	struct kbase_context *const kctx, u64 const heap_gpu_va)
{
	struct kbase_csf_tiler_heap *heap = NULL;

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	list_for_each_entry(heap, &kctx->csf.tiler_heaps.list, link) {
		if (heap_gpu_va == heap->gpu_va)
			return heap;
	}

	dev_dbg(kctx->kbdev->dev, "Tiler heap 0x%llX was not found\n",
		heap_gpu_va);

	return NULL;
}

int kbase_csf_tiler_heap_context_init(struct kbase_context *const kctx)
{
	int err = kbase_csf_heap_context_allocator_init(
		&kctx->csf.tiler_heaps.ctx_alloc, kctx);

	if (unlikely(err))
		return err;

	INIT_LIST_HEAD(&kctx->csf.tiler_heaps.list);
	mutex_init(&kctx->csf.tiler_heaps.lock);

	dev_dbg(kctx->kbdev->dev, "Initialized a context for tiler heaps\n");

	return 0;
}

void kbase_csf_tiler_heap_context_term(struct kbase_context *const kctx)
{
	struct list_head *entry = NULL, *tmp = NULL;

	dev_dbg(kctx->kbdev->dev, "Terminating a context for tiler heaps\n");

	mutex_lock(&kctx->csf.tiler_heaps.lock);

	list_for_each_safe(entry, tmp, &kctx->csf.tiler_heaps.list) {
		struct kbase_csf_tiler_heap *heap = list_entry(
			entry, struct kbase_csf_tiler_heap, link);
		delete_heap(heap);
	}

	mutex_unlock(&kctx->csf.tiler_heaps.lock);
	mutex_destroy(&kctx->csf.tiler_heaps.lock);

	kbase_csf_heap_context_allocator_term(&kctx->csf.tiler_heaps.ctx_alloc);
}

int kbase_csf_tiler_heap_init(struct kbase_context *const kctx,
	u32 const chunk_size, u32 const initial_chunks, u32 const max_chunks,
	u16 const target_in_flight, u64 *const heap_gpu_va,
	u64 *const first_chunk_va)
{
	int err = 0;
	struct kbase_csf_tiler_heap *heap = NULL;
	struct kbase_csf_heap_context_allocator *const ctx_alloc =
		&kctx->csf.tiler_heaps.ctx_alloc;

	dev_dbg(kctx->kbdev->dev,
		"Creating a tiler heap with %u chunks (limit: %u) of size %u\n",
		initial_chunks, max_chunks, chunk_size);

	if (chunk_size == 0)
		return -EINVAL;

	if (chunk_size & ~CHUNK_SIZE_MASK)
		return -EINVAL;

	if (initial_chunks == 0)
		return -EINVAL;

	if (initial_chunks > max_chunks)
		return -EINVAL;

	if (target_in_flight == 0)
		return -EINVAL;

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (unlikely(!heap)) {
		dev_err(kctx->kbdev->dev,
			"No kernel memory for a new tiler heap\n");
		return -ENOMEM;
	}

	heap->kctx = kctx;
	heap->chunk_size = chunk_size;
	heap->max_chunks = max_chunks;
	heap->target_in_flight = target_in_flight;
	INIT_LIST_HEAD(&heap->chunks_list);

	heap->gpu_va = kbase_csf_heap_context_allocator_alloc(ctx_alloc);

	mutex_lock(&kctx->csf.tiler_heaps.lock);

	if (unlikely(!heap->gpu_va)) {
		dev_err(kctx->kbdev->dev,
			"Failed to allocate a tiler heap context\n");
		err = -ENOMEM;
	} else {
		err = create_initial_chunks(heap, initial_chunks);
		if (unlikely(err)) {
			kbase_csf_heap_context_allocator_free(ctx_alloc,
				heap->gpu_va);
		}
	}

	if (unlikely(err)) {
		kfree(heap);
	} else {
		struct kbase_csf_tiler_heap_chunk const *first_chunk =
			list_first_entry(&heap->chunks_list,
				struct kbase_csf_tiler_heap_chunk, link);

		kctx->csf.tiler_heaps.nr_of_heaps++;
		heap->heap_id = kctx->csf.tiler_heaps.nr_of_heaps;
		list_add(&heap->link, &kctx->csf.tiler_heaps.list);

		*heap_gpu_va = heap->gpu_va;
		*first_chunk_va = first_chunk->gpu_va;

		KBASE_TLSTREAM_AUX_TILER_HEAP_STATS(
			kctx->kbdev, kctx->id, heap->heap_id,
			PFN_UP(heap->chunk_size * heap->max_chunks),
			PFN_UP(heap->chunk_size * heap->chunk_count),
			heap->max_chunks, heap->chunk_size, heap->chunk_count,
			heap->target_in_flight, 0);

		dev_dbg(kctx->kbdev->dev, "Created tiler heap 0x%llX\n",
			heap->gpu_va);
	}

	mutex_unlock(&kctx->csf.tiler_heaps.lock);

	return err;
}

int kbase_csf_tiler_heap_term(struct kbase_context *const kctx,
	u64 const heap_gpu_va)
{
	int err = 0;
	struct kbase_csf_tiler_heap *heap = NULL;

	mutex_lock(&kctx->csf.tiler_heaps.lock);

	heap = find_tiler_heap(kctx, heap_gpu_va);
	if (likely(heap))
		delete_heap(heap);
	else
		err = -EINVAL;

	mutex_unlock(&kctx->csf.tiler_heaps.lock);

	return err;
}

/**
 * alloc_new_chunk - Allocate a new chunk for the tiler heap.
 *
 * This function will allocate a new chunk for the chunked tiler heap depending
 * on the settings provided by userspace when the heap was created and the
 * heap's statistics (like number of render passes in-flight).
 *
 * @heap:               Pointer to the tiler heap.
 * @nr_in_flight:       Number of render passes that are in-flight, must not be zero.
 * @pending_frag_count: Number of render passes in-flight with completed vertex/tiler stage.
 *                      The minimum value is zero but it must be less or equal to
 *                      the total number of render passes in flight
 * @new_chunk_ptr:      Where to store the GPU virtual address & size of the new
 *                      chunk allocated for the heap.
 *
 * Return: 0 if a new chunk was allocated otherwise an appropriate negative
 *         error code.
 */
static int alloc_new_chunk(struct kbase_csf_tiler_heap *heap,
		u32 nr_in_flight, u32 pending_frag_count, u64 *new_chunk_ptr)
{
	int err = -ENOMEM;

	lockdep_assert_held(&heap->kctx->csf.tiler_heaps.lock);

	if (WARN_ON(!nr_in_flight) ||
		WARN_ON(pending_frag_count > nr_in_flight))
		return -EINVAL;

	if (nr_in_flight <= heap->target_in_flight) {
		if (heap->chunk_count < heap->max_chunks) {
			/* Not exceeded the target number of render passes yet so be
			 * generous with memory.
			 */
			err = create_chunk(heap, false);

			if (likely(!err)) {
				struct kbase_csf_tiler_heap_chunk *new_chunk =
								get_last_chunk(heap);
				if (!WARN_ON(!new_chunk)) {
					*new_chunk_ptr =
						encode_chunk_ptr(heap->chunk_size,
								 new_chunk->gpu_va);
					return 0;
				}
			}
		} else if (pending_frag_count > 0) {
			err = -EBUSY;
		} else {
			err = -ENOMEM;
		}
	} else {
		/* Reached target number of render passes in flight.
		 * Wait for some of them to finish
		 */
		err = -EBUSY;
	}

	return err;
}

int kbase_csf_tiler_heap_alloc_new_chunk(struct kbase_context *kctx,
	u64 gpu_heap_va, u32 nr_in_flight, u32 pending_frag_count, u64 *new_chunk_ptr)
{
	struct kbase_csf_tiler_heap *heap;
	int err = -EINVAL;

	mutex_lock(&kctx->csf.tiler_heaps.lock);

	heap = find_tiler_heap(kctx, gpu_heap_va);

	if (likely(heap)) {
		err = alloc_new_chunk(heap, nr_in_flight, pending_frag_count,
			new_chunk_ptr);

		KBASE_TLSTREAM_AUX_TILER_HEAP_STATS(
			kctx->kbdev, kctx->id, heap->heap_id,
			PFN_UP(heap->chunk_size * heap->max_chunks),
			PFN_UP(heap->chunk_size * heap->chunk_count),
			heap->max_chunks, heap->chunk_size, heap->chunk_count,
			heap->target_in_flight, nr_in_flight);
	}

	mutex_unlock(&kctx->csf.tiler_heaps.lock);

	return err;
}
