// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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

/* Tiler heap shrink stop limit for maintaining a minimum number of chunks */
#define HEAP_SHRINK_STOP_LIMIT (1)

/**
 * struct kbase_csf_gpu_buffer_heap - A gpu buffer object specific to tiler heap
 *
 * @cdsbp_0:       Descriptor_type and buffer_type
 * @size:          The size of the current heap chunk
 * @pointer:       Pointer to the current heap chunk
 * @low_pointer:   Pointer to low end of current heap chunk
 * @high_pointer:  Pointer to high end of current heap chunk
 */
struct kbase_csf_gpu_buffer_heap {
	u32 cdsbp_0;
	u32 size;
	u64 pointer;
	u64 low_pointer;
	u64 high_pointer;
} __packed;

/**
 * encode_chunk_ptr - Encode the address and size of a chunk as an integer.
 *
 * @chunk_size: Size of a tiler heap chunk, in bytes.
 * @chunk_addr: GPU virtual address of the same tiler heap chunk.
 *
 * The size and address of the next chunk in a list are packed into a single
 * 64-bit value for storage in a chunk's header. This function returns that
 * value.
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
	if (list_empty(&heap->chunks_list))
		return NULL;

	return list_last_entry(&heap->chunks_list,
		struct kbase_csf_tiler_heap_chunk, link);
}

/**
 * remove_external_chunk_mappings - Remove external mappings from a chunk that
 *                                  is being transitioned to the tiler heap
 *                                  memory system.
 *
 * @kctx:  kbase context the chunk belongs to.
 * @chunk: The chunk whose external mappings are going to be removed.
 *
 * This function marks the region as DONT NEED. Along with KBASE_REG_NO_USER_FREE, this indicates
 * that the VA region is owned by the tiler heap and could potentially be shrunk at any time. Other
 * parts of kbase outside of tiler heap management should not take references on its physical
 * pages, and should not modify them.
 */
static void remove_external_chunk_mappings(struct kbase_context *const kctx,
					   struct kbase_csf_tiler_heap_chunk *chunk)
{
	lockdep_assert_held(&kctx->reg_lock);

	if (chunk->region->cpu_alloc != NULL) {
		kbase_mem_shrink_cpu_mapping(kctx, chunk->region, 0,
					     chunk->region->cpu_alloc->nents);
	}
#if !defined(CONFIG_MALI_VECTOR_DUMP)
	chunk->region->flags |= KBASE_REG_DONT_NEED;
#endif

	dev_dbg(kctx->kbdev->dev, "Removed external mappings from chunk 0x%llX", chunk->gpu_va);
}

/**
 * link_chunk - Link a chunk into a tiler heap
 *
 * @heap:  Pointer to the tiler heap.
 * @chunk: Pointer to the heap chunk to be linked.
 *
 * Unless the @chunk is the first in the kernel's list of chunks belonging to
 * a given tiler heap, this function stores the size and address of the @chunk
 * in the header of the preceding chunk. This requires the GPU memory region
 * containing the header to be mapped temporarily, which can fail.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int link_chunk(struct kbase_csf_tiler_heap *const heap,
	struct kbase_csf_tiler_heap_chunk *const chunk)
{
	struct kbase_csf_tiler_heap_chunk *const prev = get_last_chunk(heap);

	if (prev) {
		struct kbase_context *const kctx = heap->kctx;
		u64 *prev_hdr = prev->map.addr;

		WARN((prev->region->flags & KBASE_REG_CPU_CACHED),
		     "Cannot support CPU cached chunks without sync operations");

		*prev_hdr = encode_chunk_ptr(heap->chunk_size, chunk->gpu_va);

		dev_dbg(kctx->kbdev->dev,
			"Linked tiler heap chunks, 0x%llX -> 0x%llX\n",
			prev->gpu_va, chunk->gpu_va);
	}

	return 0;
}

/**
 * init_chunk - Initialize and link a tiler heap chunk
 *
 * @heap:  Pointer to the tiler heap.
 * @chunk: Pointer to the heap chunk to be initialized and linked.
 * @link_with_prev: Flag to indicate if the new chunk needs to be linked with
 *                  the previously allocated chunk.
 *
 * Zero-initialize a new chunk's header (including its pointer to the next
 * chunk, which doesn't exist yet) and then update the previous chunk's
 * header to link the new chunk into the chunk list.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int init_chunk(struct kbase_csf_tiler_heap *const heap,
	struct kbase_csf_tiler_heap_chunk *const chunk, bool link_with_prev)
{
	int err = 0;
	u64 *chunk_hdr;
	struct kbase_context *const kctx = heap->kctx;

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	if (unlikely(chunk->gpu_va & ~CHUNK_ADDR_MASK)) {
		dev_err(kctx->kbdev->dev,
			"Tiler heap chunk address is unusable\n");
		return -EINVAL;
	}

	WARN((chunk->region->flags & KBASE_REG_CPU_CACHED),
	     "Cannot support CPU cached chunks without sync operations");
	chunk_hdr = chunk->map.addr;
	if (WARN(chunk->map.size < CHUNK_HDR_SIZE,
		 "Tiler chunk kernel mapping was not large enough for zero-init")) {
		return -EINVAL;
	}

	memset(chunk_hdr, 0, CHUNK_HDR_SIZE);
	INIT_LIST_HEAD(&chunk->link);

	if (link_with_prev)
		err = link_chunk(heap, chunk);

	if (unlikely(err)) {
		dev_err(kctx->kbdev->dev, "Failed to link a chunk to a tiler heap\n");
		return -EINVAL;
	}

	list_add_tail(&chunk->link, &heap->chunks_list);
	heap->chunk_count++;

	return err;
}

/**
 * remove_unlinked_chunk - Remove a chunk that is not currently linked into a
 *                         heap.
 *
 * @kctx:  Kbase context that was used to allocate the memory.
 * @chunk: Chunk that has been allocated, but not linked into a heap.
 */
static void remove_unlinked_chunk(struct kbase_context *kctx,
				  struct kbase_csf_tiler_heap_chunk *chunk)
{
	if (WARN_ON(!list_empty(&chunk->link)))
		return;

	kbase_gpu_vm_lock(kctx);
	kbase_vunmap(kctx, &chunk->map);
	/* KBASE_REG_DONT_NEED regions will be confused with ephemeral regions (inc freed JIT
	 * regions), and so we must clear that flag too before freeing
	 */
#if !defined(CONFIG_MALI_VECTOR_DUMP)
	chunk->region->flags &= ~(KBASE_REG_NO_USER_FREE | KBASE_REG_DONT_NEED);
#else
	chunk->region->flags &= ~KBASE_REG_NO_USER_FREE;
#endif
	kbase_mem_free_region(kctx, chunk->region);
	kbase_gpu_vm_unlock(kctx);

	kfree(chunk);
}

/**
 * alloc_new_chunk - Allocate new chunk metadata for the tiler heap, reserve a fully backed VA
 *                   region for the chunk, and provide a kernel mapping.
 * @kctx:       kbase context with which the chunk will be linked
 * @chunk_size: the size of the chunk from the corresponding heap
 *
 * Allocate the chunk tracking metadata and a corresponding fully backed VA region for the
 * chunk. The kernel may need to invoke the reclaim path while trying to fulfill the allocation, so
 * we cannot hold any lock that would be held in the shrinker paths (JIT evict lock or tiler heap
 * lock).
 *
 * Since the chunk may have its physical backing removed, to prevent use-after-free scenarios we
 * ensure that it is protected from being mapped by other parts of kbase.
 *
 * The chunk's GPU memory can be accessed via its 'map' member, but should only be done so by the
 * shrinker path, as it may be otherwise shrunk at any time.
 *
 * Return: pointer to kbase_csf_tiler_heap_chunk on success or a NULL pointer
 *         on failure
 */
static struct kbase_csf_tiler_heap_chunk *alloc_new_chunk(struct kbase_context *kctx,
							  u64 chunk_size)
{
	u64 nr_pages = PFN_UP(chunk_size);
	u64 flags = BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR | BASE_MEM_PROT_CPU_WR |
		    BASEP_MEM_NO_USER_FREE | BASE_MEM_COHERENT_LOCAL | BASE_MEM_PROT_CPU_RD;
	struct kbase_csf_tiler_heap_chunk *chunk = NULL;
	/* The chunk kernel mapping needs to be large enough to:
	 * - initially zero the CHUNK_HDR_SIZE area
	 * - on shrinking, access the NEXT_CHUNK_ADDR_SIZE area
	 */
	const size_t chunk_kernel_map_size = max(CHUNK_HDR_SIZE, NEXT_CHUNK_ADDR_SIZE);

	/* Calls to this function are inherently synchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_SYNC;
	flags |= kbase_mem_group_id_set(kctx->jit_group_id);

	chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
	if (unlikely(!chunk)) {
		dev_err(kctx->kbdev->dev,
			"No kernel memory for a new tiler heap chunk\n");
		return NULL;
	}

	/* Allocate GPU memory for the new chunk. */
	chunk->region =
		kbase_mem_alloc(kctx, nr_pages, nr_pages, 0, &flags, &chunk->gpu_va, mmu_sync_info);

	if (unlikely(!chunk->region)) {
		dev_err(kctx->kbdev->dev, "Failed to allocate a tiler heap chunk!\n");
		goto unroll_chunk;
	}

	kbase_gpu_vm_lock(kctx);

	/* Some checks done here as KBASE_REG_NO_USER_FREE still allows such things to be made
	 * whilst we had dropped the region lock
	 */
	if (unlikely(atomic_read(&chunk->region->gpu_alloc->kernel_mappings) > 0)) {
		dev_err(kctx->kbdev->dev, "Chunk region has active kernel mappings!\n");
		goto unroll_region;
	}

	/* Whilst we can be sure of a number of other restrictions due to BASEP_MEM_NO_USER_FREE
	 * being requested, it's useful to document in code what those restrictions are, and ensure
	 * they remain in place in future.
	 */
	if (WARN(!chunk->region->gpu_alloc,
		 "KBASE_REG_NO_USER_FREE chunks should not have had their alloc freed")) {
		goto unroll_region;
	}

	if (WARN(chunk->region->gpu_alloc->type != KBASE_MEM_TYPE_NATIVE,
		 "KBASE_REG_NO_USER_FREE chunks should not have been freed and then reallocated as imported/non-native regions")) {
		goto unroll_region;
	}

	if (WARN((chunk->region->flags & KBASE_REG_ACTIVE_JIT_ALLOC),
		 "KBASE_REG_NO_USER_FREE chunks should not have been freed and then reallocated as JIT regions")) {
		goto unroll_region;
	}

	if (WARN((chunk->region->flags & KBASE_REG_DONT_NEED),
		 "KBASE_REG_NO_USER_FREE chunks should not have been made ephemeral")) {
		goto unroll_region;
	}

	if (WARN(atomic_read(&chunk->region->cpu_alloc->gpu_mappings) > 1,
		 "KBASE_REG_NO_USER_FREE chunks should not have been aliased")) {
		goto unroll_region;
	}

	if (unlikely(!kbase_vmap_reg(kctx, chunk->region, chunk->gpu_va, chunk_kernel_map_size,
				     (KBASE_REG_CPU_RD | KBASE_REG_CPU_WR), &chunk->map,
				     KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING))) {
		dev_err(kctx->kbdev->dev, "Failed to map chunk header for shrinking!\n");
		goto unroll_region;
	}

	remove_external_chunk_mappings(kctx, chunk);
	kbase_gpu_vm_unlock(kctx);

	return chunk;

unroll_region:
	/* KBASE_REG_DONT_NEED regions will be confused with ephemeral regions (inc freed JIT
	 * regions), and so we must clear that flag too before freeing.
	 */
#if !defined(CONFIG_MALI_VECTOR_DUMP)
	chunk->region->flags &= ~(KBASE_REG_NO_USER_FREE | KBASE_REG_DONT_NEED);
#else
	chunk->region->flags &= ~KBASE_REG_NO_USER_FREE;
#endif
	kbase_mem_free_region(kctx, chunk->region);
	kbase_gpu_vm_unlock(kctx);
unroll_chunk:
	kfree(chunk);
	return NULL;
}

/**
 * create_chunk - Create a tiler heap chunk
 *
 * @heap: Pointer to the tiler heap for which to allocate memory.
 *
 * This function allocates a chunk of memory for a tiler heap, adds it to the
 * the list of chunks associated with that heap both on the host side and in GPU
 * memory.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int create_chunk(struct kbase_csf_tiler_heap *const heap)
{
	int err = 0;
	struct kbase_csf_tiler_heap_chunk *chunk = NULL;

	chunk = alloc_new_chunk(heap->kctx, heap->chunk_size);
	if (unlikely(!chunk)) {
		err = -ENOMEM;
		goto allocation_failure;
	}

	mutex_lock(&heap->kctx->csf.tiler_heaps.lock);
	err = init_chunk(heap, chunk, true);
	mutex_unlock(&heap->kctx->csf.tiler_heaps.lock);

	if (unlikely(err))
		goto initialization_failure;

	dev_dbg(heap->kctx->kbdev->dev, "Created tiler heap chunk 0x%llX\n", chunk->gpu_va);

	return 0;
initialization_failure:
	remove_unlinked_chunk(heap->kctx, chunk);
allocation_failure:
	return err;
}

/**
 * delete_all_chunks - Delete all chunks belonging to an unlinked tiler heap
 *
 * @heap: Pointer to a tiler heap.
 *
 * This function empties the list of chunks associated with a tiler heap by freeing all chunks
 * previously allocated by @create_chunk.
 *
 * The heap must not be reachable from a &struct kbase_context.csf.tiler_heaps.list, as the
 * tiler_heaps lock cannot be held whilst deleting its chunks due to also needing the &struct
 * kbase_context.region_lock.
 *
 * WARNING: Whilst the deleted chunks are unlinked from host memory, they are not unlinked from the
 *          list of chunks used by the GPU, therefore it is only safe to use this function when
 *          deleting a heap.
 */
static void delete_all_chunks(struct kbase_csf_tiler_heap *heap)
{
	struct kbase_context *const kctx = heap->kctx;
	struct list_head *entry = NULL, *tmp = NULL;

	WARN(!list_empty(&heap->link),
	     "Deleting a heap's chunks when that heap is still linked requires the tiler_heaps lock, which cannot be held by the caller");

	list_for_each_safe(entry, tmp, &heap->chunks_list) {
		struct kbase_csf_tiler_heap_chunk *chunk = list_entry(
			entry, struct kbase_csf_tiler_heap_chunk, link);

		list_del_init(&chunk->link);
		heap->chunk_count--;

		remove_unlinked_chunk(kctx, chunk);
	}
}

/**
 * create_initial_chunks - Create the initial list of chunks for a tiler heap
 *
 * @heap:    Pointer to the tiler heap for which to allocate memory.
 * @nchunks: Number of chunks to create.
 *
 * This function allocates a given number of chunks for a tiler heap and
 * adds them to the list of chunks associated with that heap.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int create_initial_chunks(struct kbase_csf_tiler_heap *const heap,
	u32 const nchunks)
{
	int err = 0;
	u32 i;

	for (i = 0; (i < nchunks) && likely(!err); i++)
		err = create_chunk(heap);

	if (unlikely(err))
		delete_all_chunks(heap);

	return err;
}

/**
 * delete_heap - Delete an unlinked tiler heap
 *
 * @heap: Pointer to a tiler heap to be deleted.
 *
 * This function frees any chunks allocated for a tiler heap previously
 * initialized by @kbase_csf_tiler_heap_init. The heap context structure used by
 * the firmware is also freed.
 *
 * The heap must not be reachable from a &struct kbase_context.csf.tiler_heaps.list, as the
 * tiler_heaps lock cannot be held whilst deleting it due to also needing the &struct
 * kbase_context.region_lock.
 */
static void delete_heap(struct kbase_csf_tiler_heap *heap)
{
	struct kbase_context *const kctx = heap->kctx;

	dev_dbg(kctx->kbdev->dev, "Deleting tiler heap 0x%llX\n", heap->gpu_va);

	WARN(!list_empty(&heap->link),
	     "Deleting a heap that is still linked requires the tiler_heaps lock, which cannot be held by the caller");

	/* Make sure that all of the VA regions corresponding to the chunks are
	 * freed at this time and that the work queue is not trying to access freed
	 * memory.
	 *
	 * Note: since the heap is unlinked, and that no references are made to chunks other
	 * than from their heap, there is no need to separately move the chunks out of the
	 * heap->chunks_list to delete them.
	 */
	delete_all_chunks(heap);

	kbase_vunmap(kctx, &heap->gpu_va_map);
	/* We could optimize context destruction by not freeing leaked heap
	 * contexts but it doesn't seem worth the extra complexity. After this
	 * point, the suballocation is returned to the heap context allocator and
	 * may be overwritten with new data, meaning heap->gpu_va should not
	 * be used past this point.
	 */
	kbase_csf_heap_context_allocator_free(&kctx->csf.tiler_heaps.ctx_alloc,
		heap->gpu_va);

	WARN_ON(heap->chunk_count);
	KBASE_TLSTREAM_AUX_TILER_HEAP_STATS(kctx->kbdev, kctx->id,
		heap->heap_id, 0, 0, heap->max_chunks, heap->chunk_size, 0,
		heap->target_in_flight, 0);

	if (heap->buf_desc_reg) {
		kbase_vunmap(kctx, &heap->buf_desc_map);
		kbase_gpu_vm_lock(kctx);
		heap->buf_desc_reg->flags &= ~KBASE_REG_NO_USER_FREE;
		kbase_gpu_vm_unlock(kctx);
	}

	kfree(heap);
}

/**
 * find_tiler_heap - Find a tiler heap from the address of its heap context
 *
 * @kctx:        Pointer to the kbase context to search for a tiler heap.
 * @heap_gpu_va: GPU virtual address of a heap context structure.
 *
 * Each tiler heap managed by the kernel has an associated heap context
 * structure used by the firmware. This function finds a tiler heap object from
 * the GPU virtual address of its associated heap context. The heap context
 * should have been allocated by @kbase_csf_heap_context_allocator_alloc in the
 * same @kctx.
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

static struct kbase_csf_tiler_heap_chunk *find_chunk(struct kbase_csf_tiler_heap *heap,
						     u64 const chunk_gpu_va)
{
	struct kbase_csf_tiler_heap_chunk *chunk = NULL;

	lockdep_assert_held(&heap->kctx->csf.tiler_heaps.lock);

	list_for_each_entry(chunk, &heap->chunks_list, link) {
		if (chunk->gpu_va == chunk_gpu_va)
			return chunk;
	}

	dev_dbg(heap->kctx->kbdev->dev, "Tiler heap chunk 0x%llX was not found\n", chunk_gpu_va);

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
	LIST_HEAD(local_heaps_list);
	struct list_head *entry = NULL, *tmp = NULL;

	dev_dbg(kctx->kbdev->dev, "Terminating a context for tiler heaps\n");

	mutex_lock(&kctx->csf.tiler_heaps.lock);
	list_splice_init(&kctx->csf.tiler_heaps.list, &local_heaps_list);
	mutex_unlock(&kctx->csf.tiler_heaps.lock);

	list_for_each_safe(entry, tmp, &local_heaps_list) {
		struct kbase_csf_tiler_heap *heap = list_entry(
			entry, struct kbase_csf_tiler_heap, link);

		list_del_init(&heap->link);
		delete_heap(heap);
	}

	mutex_destroy(&kctx->csf.tiler_heaps.lock);

	kbase_csf_heap_context_allocator_term(&kctx->csf.tiler_heaps.ctx_alloc);
}

/**
 * kbasep_is_buffer_descriptor_region_suitable - Check if a VA region chosen to house
 *                                               the tiler heap buffer descriptor
 *                                               is suitable for the purpose.
 * @kctx: kbase context of the tiler heap
 * @reg:  VA region being checked for suitability
 *
 * The tiler heap buffer descriptor memory does not admit page faults according
 * to its design, so it must have the entirety of the backing upon allocation,
 * and it has to remain alive as long as the tiler heap is alive, meaning it
 * cannot be allocated from JIT/Ephemeral, or user freeable memory.
 *
 * Return: true on suitability, false otherwise.
 */
static bool kbasep_is_buffer_descriptor_region_suitable(struct kbase_context *const kctx,
							struct kbase_va_region *const reg)
{
	if (kbase_is_region_invalid_or_free(reg)) {
		dev_err(kctx->kbdev->dev, "Region is either invalid or free!\n");
		return false;
	}

	if (!(reg->flags & KBASE_REG_CPU_RD) || (reg->flags & KBASE_REG_DONT_NEED) ||
	    (reg->flags & KBASE_REG_PF_GROW) || (reg->flags & KBASE_REG_ACTIVE_JIT_ALLOC)) {
		dev_err(kctx->kbdev->dev, "Region has invalid flags: 0x%lX!\n", reg->flags);
		return false;
	}

	if (reg->gpu_alloc->type != KBASE_MEM_TYPE_NATIVE) {
		dev_err(kctx->kbdev->dev, "Region has invalid type!\n");
		return false;
	}

	if ((reg->nr_pages != kbase_reg_current_backed_size(reg)) ||
	    (reg->nr_pages < PFN_UP(sizeof(struct kbase_csf_gpu_buffer_heap)))) {
		dev_err(kctx->kbdev->dev, "Region has invalid backing!\n");
		return false;
	}

	return true;
}

#define TILER_BUF_DESC_SIZE (sizeof(struct kbase_csf_gpu_buffer_heap))

int kbase_csf_tiler_heap_init(struct kbase_context *const kctx, u32 const chunk_size,
			      u32 const initial_chunks, u32 const max_chunks,
			      u16 const target_in_flight, u64 const buf_desc_va,
			      u64 *const heap_gpu_va, u64 *const first_chunk_va)
{
	int err = 0;
	struct kbase_csf_tiler_heap *heap = NULL;
	struct kbase_csf_heap_context_allocator *const ctx_alloc =
		&kctx->csf.tiler_heaps.ctx_alloc;
	struct kbase_csf_tiler_heap_chunk *chunk = NULL;
	struct kbase_va_region *gpu_va_reg = NULL;
	void *vmap_ptr = NULL;

	dev_dbg(kctx->kbdev->dev,
		"Creating a tiler heap with %u chunks (limit: %u) of size %u, buf_desc_va: 0x%llx\n",
		initial_chunks, max_chunks, chunk_size, buf_desc_va);

	if (!kbase_mem_allow_alloc(kctx))
		return -EINVAL;

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
		dev_err(kctx->kbdev->dev, "No kernel memory for a new tiler heap");
		return -ENOMEM;
	}

	heap->kctx = kctx;
	heap->chunk_size = chunk_size;
	heap->max_chunks = max_chunks;
	heap->target_in_flight = target_in_flight;
	heap->buf_desc_checked = false;
	INIT_LIST_HEAD(&heap->chunks_list);
	INIT_LIST_HEAD(&heap->link);

	/* Check on the buffer descriptor virtual Address */
	if (buf_desc_va) {
		struct kbase_va_region *buf_desc_reg;

		kbase_gpu_vm_lock(kctx);
		buf_desc_reg =
			kbase_region_tracker_find_region_enclosing_address(kctx, buf_desc_va);

		if (!kbasep_is_buffer_descriptor_region_suitable(kctx, buf_desc_reg)) {
			kbase_gpu_vm_unlock(kctx);
			dev_err(kctx->kbdev->dev,
				"Could not find a suitable VA region for the tiler heap buf desc!\n");
			err = -EINVAL;
			goto buf_desc_not_suitable;
		}

		/* If we don't prevent userspace from unmapping this, we may run into
		 * use-after-free, as we don't check for the existence of the region throughout.
		 */
		buf_desc_reg->flags |= KBASE_REG_NO_USER_FREE;

		heap->buf_desc_va = buf_desc_va;
		heap->buf_desc_reg = buf_desc_reg;

		vmap_ptr = kbase_vmap_reg(kctx, buf_desc_reg, buf_desc_va, TILER_BUF_DESC_SIZE,
					  KBASE_REG_CPU_RD, &heap->buf_desc_map,
					  KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING);
		kbase_gpu_vm_unlock(kctx);

		if (unlikely(!vmap_ptr)) {
			dev_err(kctx->kbdev->dev,
				"Could not vmap buffer descriptor into kernel memory (err %d)\n",
				err);
			err = -ENOMEM;
			goto buf_desc_vmap_failed;
		}
	}

	heap->gpu_va = kbase_csf_heap_context_allocator_alloc(ctx_alloc);
	if (unlikely(!heap->gpu_va)) {
		dev_dbg(kctx->kbdev->dev, "Failed to allocate a tiler heap context\n");
		err = -ENOMEM;
		goto heap_context_alloc_failed;
	}

	gpu_va_reg = ctx_alloc->region;

	kbase_gpu_vm_lock(kctx);
	/* gpu_va_reg was created with BASEP_MEM_NO_USER_FREE, the code to unset this only happens
	 * on kctx termination (after all syscalls on kctx have finished), and so it is safe to
	 * assume that gpu_va_reg is still present.
	 */
	vmap_ptr = kbase_vmap_reg(kctx, gpu_va_reg, heap->gpu_va, NEXT_CHUNK_ADDR_SIZE,
				  (KBASE_REG_CPU_RD | KBASE_REG_CPU_WR), &heap->gpu_va_map,
				  KBASE_VMAP_FLAG_PERMANENT_MAP_ACCOUNTING);
	kbase_gpu_vm_unlock(kctx);
	if (unlikely(!vmap_ptr)) {
		dev_dbg(kctx->kbdev->dev, "Failed to vmap the correct heap GPU VA address\n");
		err = -ENOMEM;
		goto heap_context_vmap_failed;
	}

	err = create_initial_chunks(heap, initial_chunks);
	if (unlikely(err)) {
		dev_dbg(kctx->kbdev->dev, "Failed to create the initial tiler heap chunks\n");
		goto create_chunks_failed;
	}
	chunk = list_first_entry(&heap->chunks_list, struct kbase_csf_tiler_heap_chunk, link);

	*heap_gpu_va = heap->gpu_va;
	*first_chunk_va = chunk->gpu_va;

	mutex_lock(&kctx->csf.tiler_heaps.lock);
	kctx->csf.tiler_heaps.nr_of_heaps++;
	heap->heap_id = kctx->csf.tiler_heaps.nr_of_heaps;
	list_add(&heap->link, &kctx->csf.tiler_heaps.list);

	KBASE_TLSTREAM_AUX_TILER_HEAP_STATS(kctx->kbdev, kctx->id, heap->heap_id,
					    PFN_UP(heap->chunk_size * heap->max_chunks),
					    PFN_UP(heap->chunk_size * heap->chunk_count),
					    heap->max_chunks, heap->chunk_size, heap->chunk_count,
					    heap->target_in_flight, 0);

#if defined(CONFIG_MALI_VECTOR_DUMP)
	list_for_each_entry(chunk, &heap->chunks_list, link) {
		KBASE_TLSTREAM_JD_TILER_HEAP_CHUNK_ALLOC(kctx->kbdev, kctx->id, heap->heap_id,
							 chunk->gpu_va);
	}
#endif
	kctx->running_total_tiler_heap_nr_chunks += heap->chunk_count;
	kctx->running_total_tiler_heap_memory += (u64)heap->chunk_size * heap->chunk_count;
	if (kctx->running_total_tiler_heap_memory > kctx->peak_total_tiler_heap_memory)
		kctx->peak_total_tiler_heap_memory = kctx->running_total_tiler_heap_memory;

	dev_dbg(kctx->kbdev->dev,
		"Created tiler heap 0x%llX, buffer descriptor 0x%llX, ctx_%d_%d\n", heap->gpu_va,
		buf_desc_va, kctx->tgid, kctx->id);
	mutex_unlock(&kctx->csf.tiler_heaps.lock);

	return 0;

create_chunks_failed:
	kbase_vunmap(kctx, &heap->gpu_va_map);
heap_context_vmap_failed:
	kbase_csf_heap_context_allocator_free(ctx_alloc, heap->gpu_va);
heap_context_alloc_failed:
	if (heap->buf_desc_reg)
		kbase_vunmap(kctx, &heap->buf_desc_map);
buf_desc_vmap_failed:
	if (heap->buf_desc_reg) {
		kbase_gpu_vm_lock(kctx);
		heap->buf_desc_reg->flags &= ~KBASE_REG_NO_USER_FREE;
		kbase_gpu_vm_unlock(kctx);
	}
buf_desc_not_suitable:
	kfree(heap);
	return err;
}

int kbase_csf_tiler_heap_term(struct kbase_context *const kctx,
	u64 const heap_gpu_va)
{
	int err = 0;
	struct kbase_csf_tiler_heap *heap = NULL;
	u32 chunk_count = 0;
	u64 heap_size = 0;

	mutex_lock(&kctx->csf.tiler_heaps.lock);
	heap = find_tiler_heap(kctx, heap_gpu_va);
	if (likely(heap)) {
		chunk_count = heap->chunk_count;
		heap_size = heap->chunk_size * chunk_count;

		list_del_init(&heap->link);
	} else {
		err = -EINVAL;
	}

	/* Update stats whilst still holding the lock so they are in sync with the tiler_heaps.list
	 * at all times
	 */
	if (likely(kctx->running_total_tiler_heap_memory >= heap_size))
		kctx->running_total_tiler_heap_memory -= heap_size;
	else
		dev_warn(kctx->kbdev->dev,
			 "Running total tiler heap memory lower than expected!");
	if (likely(kctx->running_total_tiler_heap_nr_chunks >= chunk_count))
		kctx->running_total_tiler_heap_nr_chunks -= chunk_count;
	else
		dev_warn(kctx->kbdev->dev,
			 "Running total tiler chunk count lower than expected!");
	if (!err)
		dev_dbg(kctx->kbdev->dev,
			"Terminated tiler heap 0x%llX, buffer descriptor 0x%llX, ctx_%d_%d\n",
			heap->gpu_va, heap->buf_desc_va, kctx->tgid, kctx->id);
	mutex_unlock(&kctx->csf.tiler_heaps.lock);

	/* Deletion requires the kctx->reg_lock, so must only operate on it whilst unlinked from
	 * the kctx's csf.tiler_heaps.list, and without holding the csf.tiler_heaps.lock
	 */
	if (likely(heap))
		delete_heap(heap);

	return err;
}

static int validate_allocation_request(struct kbase_csf_tiler_heap *heap, u32 nr_in_flight,
				       u32 pending_frag_count)
{
	lockdep_assert_held(&heap->kctx->csf.tiler_heaps.lock);

	if (WARN_ON(!nr_in_flight) || WARN_ON(pending_frag_count > nr_in_flight))
		return -EINVAL;

	if (nr_in_flight <= heap->target_in_flight) {
		if (heap->chunk_count < heap->max_chunks) {
			/* Not exceeded the target number of render passes yet so be
			 * generous with memory.
			 */
			return 0;
		} else if (pending_frag_count > 0) {
			return -EBUSY;
		} else {
			return -ENOMEM;
		}
	} else {
		/* Reached target number of render passes in flight.
		 * Wait for some of them to finish
		 */
		return -EBUSY;
	}
	return -ENOMEM;
}

int kbase_csf_tiler_heap_alloc_new_chunk(struct kbase_context *kctx,
	u64 gpu_heap_va, u32 nr_in_flight, u32 pending_frag_count, u64 *new_chunk_ptr)
{
	struct kbase_csf_tiler_heap *heap;
	struct kbase_csf_tiler_heap_chunk *chunk;
	int err = -EINVAL;
	u64 chunk_size = 0;
	u64 heap_id = 0;

	/* To avoid potential locking issues during allocation, this is handled
	 * in three phases:
	 * 1. Take the lock, find the corresponding heap, and find its chunk size
	 * (this is always 2 MB, but may change down the line).
	 * 2. Allocate memory for the chunk and its region.
	 * 3. If the heap still exists, link it to the end of the list. If it
	 * doesn't, roll back the allocation.
	 */

	mutex_lock(&kctx->csf.tiler_heaps.lock);
	heap = find_tiler_heap(kctx, gpu_heap_va);
	if (likely(heap)) {
		chunk_size = heap->chunk_size;
		heap_id = heap->heap_id;
	} else {
		dev_err(kctx->kbdev->dev, "Heap 0x%llX does not exist", gpu_heap_va);
		mutex_unlock(&kctx->csf.tiler_heaps.lock);
		goto prelink_failure;
	}

	err = validate_allocation_request(heap, nr_in_flight, pending_frag_count);
	if (unlikely(err)) {
		dev_err(kctx->kbdev->dev,
			"Not allocating new chunk for heap 0x%llX due to current heap state (err %d)",
			gpu_heap_va, err);
		mutex_unlock(&kctx->csf.tiler_heaps.lock);
		goto prelink_failure;
	}
	mutex_unlock(&kctx->csf.tiler_heaps.lock);
	/* this heap must not be used whilst we have dropped the lock */
	heap = NULL;

	chunk = alloc_new_chunk(kctx, chunk_size);
	if (unlikely(!chunk)) {
		dev_err(kctx->kbdev->dev, "Could not allocate chunk of size %lld for ctx %d_%d",
			chunk_size, kctx->tgid, kctx->id);
		goto prelink_failure;
	}

	/* After this point, the heap that we were targeting could already have had the needed
	 * chunks allocated, if we were handling multiple OoM events on multiple threads, so
	 * we need to revalidate the need for the allocation.
	 */
	mutex_lock(&kctx->csf.tiler_heaps.lock);
	heap = find_tiler_heap(kctx, gpu_heap_va);

	if (unlikely(!heap)) {
		dev_err(kctx->kbdev->dev, "Tiler heap 0x%llX no longer exists!\n", gpu_heap_va);
		mutex_unlock(&kctx->csf.tiler_heaps.lock);
		goto unroll_chunk;
	}

	if (heap_id != heap->heap_id) {
		dev_err(kctx->kbdev->dev,
			"Tiler heap 0x%llX was removed from ctx %d_%d while allocating chunk of size %lld!",
			gpu_heap_va, kctx->tgid, kctx->id, chunk_size);
		mutex_unlock(&kctx->csf.tiler_heaps.lock);
		goto unroll_chunk;
	}

	if (WARN_ON(chunk_size != heap->chunk_size)) {
		mutex_unlock(&kctx->csf.tiler_heaps.lock);
		goto unroll_chunk;
	}

	err = validate_allocation_request(heap, nr_in_flight, pending_frag_count);
	if (unlikely(err)) {
		dev_warn(
			kctx->kbdev->dev,
			"Aborting linking chunk to heap 0x%llX: heap state changed during allocation (err %d)",
			gpu_heap_va, err);
		mutex_unlock(&kctx->csf.tiler_heaps.lock);
		goto unroll_chunk;
	}

	err = init_chunk(heap, chunk, false);

	/* On error, the chunk would not be linked, so we can still treat it as an unlinked
	 * chunk for error handling.
	 */
	if (unlikely(err)) {
		dev_err(kctx->kbdev->dev,
			"Could not link chunk(0x%llX) with tiler heap 0%llX in ctx %d_%d due to error %d",
			chunk->gpu_va, gpu_heap_va, kctx->tgid, kctx->id, err);
		mutex_unlock(&kctx->csf.tiler_heaps.lock);
		goto unroll_chunk;
	}

	*new_chunk_ptr = encode_chunk_ptr(heap->chunk_size, chunk->gpu_va);

	/* update total and peak tiler heap memory record */
	kctx->running_total_tiler_heap_nr_chunks++;
	kctx->running_total_tiler_heap_memory += heap->chunk_size;

	if (kctx->running_total_tiler_heap_memory > kctx->peak_total_tiler_heap_memory)
		kctx->peak_total_tiler_heap_memory = kctx->running_total_tiler_heap_memory;

	KBASE_TLSTREAM_AUX_TILER_HEAP_STATS(kctx->kbdev, kctx->id, heap->heap_id,
					    PFN_UP(heap->chunk_size * heap->max_chunks),
					    PFN_UP(heap->chunk_size * heap->chunk_count),
					    heap->max_chunks, heap->chunk_size, heap->chunk_count,
					    heap->target_in_flight, nr_in_flight);

	mutex_unlock(&kctx->csf.tiler_heaps.lock);

	return err;
unroll_chunk:
	remove_unlinked_chunk(kctx, chunk);
prelink_failure:
	return err;
}

static bool delete_chunk_physical_pages(struct kbase_csf_tiler_heap *heap, u64 chunk_gpu_va,
					u64 *hdr_val)
{
	int err;
	u64 *chunk_hdr;
	struct kbase_context *kctx = heap->kctx;
	struct kbase_csf_tiler_heap_chunk *chunk = NULL;

	lockdep_assert_held(&heap->kctx->csf.tiler_heaps.lock);

	chunk = find_chunk(heap, chunk_gpu_va);
	if (unlikely(!chunk)) {
		dev_warn(kctx->kbdev->dev,
			 "Failed to find tiler heap(0x%llX) chunk(0x%llX) for reclaim-delete\n",
			 heap->gpu_va, chunk_gpu_va);
		return false;
	}

	WARN((chunk->region->flags & KBASE_REG_CPU_CACHED),
	     "Cannot support CPU cached chunks without sync operations");
	chunk_hdr = chunk->map.addr;
	*hdr_val = *chunk_hdr;

	dev_dbg(kctx->kbdev->dev,
		"Reclaim: delete chunk(0x%llx) in heap(0x%llx), header value(0x%llX)\n",
		chunk_gpu_va, heap->gpu_va, *hdr_val);

	err = kbase_mem_shrink_gpu_mapping(kctx, chunk->region, 0, chunk->region->gpu_alloc->nents);
	if (unlikely(err)) {
		dev_warn(
			kctx->kbdev->dev,
			"Reclaim: shrinking GPU mapping failed on chunk(0x%llx) in heap(0x%llx) (err %d)\n",
			chunk_gpu_va, heap->gpu_va, err);

		/* Cannot free the pages whilst references on the GPU remain, so keep the chunk on
		 * the heap's chunk list and try a different heap.
		 */

		return false;
	}
	/* Destroy the mapping before the physical pages which are mapped are destroyed. */
	kbase_vunmap(kctx, &chunk->map);

	err = kbase_free_phy_pages_helper(chunk->region->gpu_alloc,
					  chunk->region->gpu_alloc->nents);
	if (unlikely(err)) {
		dev_warn(
			kctx->kbdev->dev,
			"Reclaim: remove physical backing failed on chunk(0x%llx) in heap(0x%llx) (err %d), continuing with deferred removal\n",
			chunk_gpu_va, heap->gpu_va, err);

		/* kbase_free_phy_pages_helper() should only fail on invalid input, and WARNs
		 * anyway, so continue instead of returning early.
		 *
		 * Indeed, we don't want to leave the chunk on the heap's chunk list whilst it has
		 * its mapping removed, as that could lead to problems. It's safest to instead
		 * continue with deferred destruction of the chunk.
		 */
	}

	dev_dbg(kctx->kbdev->dev,
		"Reclaim: delete chunk(0x%llx) in heap(0x%llx), header value(0x%llX)\n",
		chunk_gpu_va, heap->gpu_va, *hdr_val);

	mutex_lock(&heap->kctx->jit_evict_lock);
	list_move(&chunk->region->jit_node, &kctx->jit_destroy_head);
	mutex_unlock(&heap->kctx->jit_evict_lock);

	list_del(&chunk->link);
	heap->chunk_count--;
	kfree(chunk);

	return true;
}

static void sanity_check_gpu_buffer_heap(struct kbase_csf_tiler_heap *heap,
					 struct kbase_csf_gpu_buffer_heap *desc)
{
	u64 first_hoarded_chunk_gpu_va = desc->pointer & CHUNK_ADDR_MASK;

	lockdep_assert_held(&heap->kctx->csf.tiler_heaps.lock);

	if (first_hoarded_chunk_gpu_va) {
		struct kbase_csf_tiler_heap_chunk *chunk =
			find_chunk(heap, first_hoarded_chunk_gpu_va);

		if (likely(chunk)) {
			dev_dbg(heap->kctx->kbdev->dev,
				"Buffer descriptor 0x%llX sanity check ok, HW reclaim allowed\n",
				heap->buf_desc_va);

			heap->buf_desc_checked = true;
			return;
		}
	}
	/* If there is no match, defer the check to next time */
	dev_dbg(heap->kctx->kbdev->dev, "Buffer descriptor 0x%llX runtime sanity check deferred\n",
		heap->buf_desc_va);
}

static bool can_read_hw_gpu_buffer_heap(struct kbase_csf_tiler_heap *heap, u64 *chunk_gpu_va_ptr)
{
	struct kbase_context *kctx = heap->kctx;

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	/* Initialize the descriptor pointer value to 0 */
	*chunk_gpu_va_ptr = 0;

	/* The BufferDescriptor on heap is a hint on creation, do a sanity check at runtime */
	if (heap->buf_desc_reg && !heap->buf_desc_checked) {
		struct kbase_csf_gpu_buffer_heap *desc = heap->buf_desc_map.addr;

		/* BufferDescriptor is supplied by userspace, so could be CPU-cached */
		if (heap->buf_desc_map.flags & KBASE_VMAP_FLAG_SYNC_NEEDED)
			kbase_sync_mem_regions(kctx, &heap->buf_desc_map, KBASE_SYNC_TO_CPU);

		sanity_check_gpu_buffer_heap(heap, desc);
		if (heap->buf_desc_checked)
			*chunk_gpu_va_ptr = desc->pointer & CHUNK_ADDR_MASK;
	}

	return heap->buf_desc_checked;
}

static u32 delete_hoarded_chunks(struct kbase_csf_tiler_heap *heap)
{
	u32 freed = 0;
	u64 chunk_gpu_va = 0;
	struct kbase_context *kctx = heap->kctx;
	struct kbase_csf_tiler_heap_chunk *chunk = NULL;

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	if (can_read_hw_gpu_buffer_heap(heap, &chunk_gpu_va)) {
		u64 chunk_hdr_val;
		u64 *hw_hdr;

		if (!chunk_gpu_va) {
			struct kbase_csf_gpu_buffer_heap *desc = heap->buf_desc_map.addr;

			/* BufferDescriptor is supplied by userspace, so could be CPU-cached */
			if (heap->buf_desc_map.flags & KBASE_VMAP_FLAG_SYNC_NEEDED)
				kbase_sync_mem_regions(kctx, &heap->buf_desc_map,
						       KBASE_SYNC_TO_CPU);
			chunk_gpu_va = desc->pointer & CHUNK_ADDR_MASK;

			if (!chunk_gpu_va) {
				dev_dbg(kctx->kbdev->dev,
					"Buffer descriptor 0x%llX has no chunks (NULL) for reclaim scan\n",
					heap->buf_desc_va);
				goto out;
			}
		}

		chunk = find_chunk(heap, chunk_gpu_va);
		if (unlikely(!chunk))
			goto out;

		WARN((chunk->region->flags & KBASE_REG_CPU_CACHED),
		     "Cannot support CPU cached chunks without sync operations");
		hw_hdr = chunk->map.addr;

		/* Move onto the next chunk relevant information */
		chunk_hdr_val = *hw_hdr;
		chunk_gpu_va = chunk_hdr_val & CHUNK_ADDR_MASK;

		while (chunk_gpu_va && heap->chunk_count > HEAP_SHRINK_STOP_LIMIT) {
			bool success =
				delete_chunk_physical_pages(heap, chunk_gpu_va, &chunk_hdr_val);

			if (!success)
				break;

			freed++;
			/* On success, chunk_hdr_val is updated, extract the next chunk address */
			chunk_gpu_va = chunk_hdr_val & CHUNK_ADDR_MASK;
		}

		/* Update the existing hardware chunk header, after reclaim deletion of chunks */
		*hw_hdr = chunk_hdr_val;

		dev_dbg(heap->kctx->kbdev->dev,
			"HW reclaim scan freed chunks: %u, set hw_hdr[0]: 0x%llX\n", freed,
			chunk_hdr_val);
	} else {
		dev_dbg(kctx->kbdev->dev,
			"Skip HW reclaim scan, (disabled: buffer descriptor 0x%llX)\n",
			heap->buf_desc_va);
	}
out:
	return freed;
}

static u64 delete_unused_chunk_pages(struct kbase_csf_tiler_heap *heap)
{
	u32 freed_chunks = 0;
	u64 freed_pages = 0;
	u64 chunk_gpu_va;
	u64 chunk_hdr_val;
	struct kbase_context *kctx = heap->kctx;
	u64 *ctx_ptr;

	lockdep_assert_held(&kctx->csf.tiler_heaps.lock);

	WARN(heap->gpu_va_map.flags & KBASE_VMAP_FLAG_SYNC_NEEDED,
	     "Cannot support CPU cached heap context without sync operations");

	ctx_ptr = heap->gpu_va_map.addr;

	/* Extract the first chunk address from the context's free_list_head */
	chunk_hdr_val = *ctx_ptr;
	chunk_gpu_va = chunk_hdr_val & CHUNK_ADDR_MASK;

	while (chunk_gpu_va) {
		u64 hdr_val;
		bool success = delete_chunk_physical_pages(heap, chunk_gpu_va, &hdr_val);

		if (!success)
			break;

		freed_chunks++;
		chunk_hdr_val = hdr_val;
		/* extract the next chunk address */
		chunk_gpu_va = chunk_hdr_val & CHUNK_ADDR_MASK;
	}

	/* Update the post-scan deletion to context header */
	*ctx_ptr = chunk_hdr_val;

	/* Try to scan the HW hoarded list of unused chunks */
	freed_chunks += delete_hoarded_chunks(heap);
	freed_pages = freed_chunks * PFN_UP(heap->chunk_size);
	dev_dbg(heap->kctx->kbdev->dev,
		"Scan reclaim freed chunks/pages %u/%llu, set heap-ctx_u64[0]: 0x%llX\n",
		freed_chunks, freed_pages, chunk_hdr_val);

	/* Update context tiler heaps memory usage */
	kctx->running_total_tiler_heap_memory -= freed_pages << PAGE_SHIFT;
	kctx->running_total_tiler_heap_nr_chunks -= freed_chunks;
	return freed_pages;
}

u32 kbase_csf_tiler_heap_scan_kctx_unused_pages(struct kbase_context *kctx, u32 to_free)
{
	u64 freed = 0;
	struct kbase_csf_tiler_heap *heap;

	mutex_lock(&kctx->csf.tiler_heaps.lock);

	list_for_each_entry(heap, &kctx->csf.tiler_heaps.list, link) {
		freed += delete_unused_chunk_pages(heap);

		/* If freed enough, then stop here */
		if (freed >= to_free)
			break;
	}

	mutex_unlock(&kctx->csf.tiler_heaps.lock);
	/* The scan is surely not more than 4-G pages, but for logic flow limit it */
	if (WARN_ON(unlikely(freed > U32_MAX)))
		return U32_MAX;
	else
		return (u32)freed;
}

static u64 count_unused_heap_pages(struct kbase_csf_tiler_heap *heap)
{
	u32 chunk_cnt = 0;
	u64 page_cnt = 0;

	lockdep_assert_held(&heap->kctx->csf.tiler_heaps.lock);

	/* Here the count is basically an informed estimate, avoiding the costly mapping/unmaping
	 * in the chunk list walk. The downside is that the number is a less reliable guide for
	 * later on scan (free) calls on this heap for what actually is freeable.
	 */
	if (heap->chunk_count > HEAP_SHRINK_STOP_LIMIT) {
		chunk_cnt = heap->chunk_count - HEAP_SHRINK_STOP_LIMIT;
		page_cnt = chunk_cnt * PFN_UP(heap->chunk_size);
	}

	dev_dbg(heap->kctx->kbdev->dev,
		"Reclaim count chunks/pages %u/%llu (estimated), heap_va: 0x%llX\n", chunk_cnt,
		page_cnt, heap->gpu_va);

	return page_cnt;
}

u32 kbase_csf_tiler_heap_count_kctx_unused_pages(struct kbase_context *kctx)
{
	u64 page_cnt = 0;
	struct kbase_csf_tiler_heap *heap;

	mutex_lock(&kctx->csf.tiler_heaps.lock);

	list_for_each_entry(heap, &kctx->csf.tiler_heaps.list, link)
		page_cnt += count_unused_heap_pages(heap);

	mutex_unlock(&kctx->csf.tiler_heaps.lock);

	/* The count is surely not more than 4-G pages, but for logic flow limit it */
	if (WARN_ON(unlikely(page_cnt > U32_MAX)))
		return U32_MAX;
	else
		return (u32)page_cnt;
}
