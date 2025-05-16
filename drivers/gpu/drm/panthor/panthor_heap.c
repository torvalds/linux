// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2023 Collabora ltd. */

#include <linux/iosys-map.h>
#include <linux/rwsem.h>

#include <drm/panthor_drm.h>

#include "panthor_device.h"
#include "panthor_gem.h"
#include "panthor_heap.h"
#include "panthor_mmu.h"
#include "panthor_regs.h"

/*
 * The GPU heap context is an opaque structure used by the GPU to track the
 * heap allocations. The driver should only touch it to initialize it (zero all
 * fields). Because the CPU and GPU can both access this structure it is
 * required to be GPU cache line aligned.
 */
#define HEAP_CONTEXT_SIZE	32

/**
 * struct panthor_heap_chunk_header - Heap chunk header
 */
struct panthor_heap_chunk_header {
	/**
	 * @next: Next heap chunk in the list.
	 *
	 * This is a GPU VA.
	 */
	u64 next;

	/** @unknown: MBZ. */
	u32 unknown[14];
};

/**
 * struct panthor_heap_chunk - Structure used to keep track of allocated heap chunks.
 */
struct panthor_heap_chunk {
	/** @node: Used to insert the heap chunk in panthor_heap::chunks. */
	struct list_head node;

	/** @bo: Buffer object backing the heap chunk. */
	struct panthor_kernel_bo *bo;
};

/**
 * struct panthor_heap - Structure used to manage tiler heap contexts.
 */
struct panthor_heap {
	/** @chunks: List containing all heap chunks allocated so far. */
	struct list_head chunks;

	/** @lock: Lock protecting insertion in the chunks list. */
	struct mutex lock;

	/** @chunk_size: Size of each chunk. */
	u32 chunk_size;

	/** @max_chunks: Maximum number of chunks. */
	u32 max_chunks;

	/**
	 * @target_in_flight: Number of in-flight render passes after which
	 * we'd let the FW wait for fragment job to finish instead of allocating new chunks.
	 */
	u32 target_in_flight;

	/** @chunk_count: Number of heap chunks currently allocated. */
	u32 chunk_count;
};

#define MAX_HEAPS_PER_POOL    128

/**
 * struct panthor_heap_pool - Pool of heap contexts
 *
 * The pool is attached to a panthor_file and can't be shared across processes.
 */
struct panthor_heap_pool {
	/** @refcount: Reference count. */
	struct kref refcount;

	/** @ptdev: Device. */
	struct panthor_device *ptdev;

	/** @vm: VM this pool is bound to. */
	struct panthor_vm *vm;

	/** @lock: Lock protecting access to @xa. */
	struct rw_semaphore lock;

	/** @xa: Array storing panthor_heap objects. */
	struct xarray xa;

	/** @gpu_contexts: Buffer object containing the GPU heap contexts. */
	struct panthor_kernel_bo *gpu_contexts;

	/** @size: Size of all chunks across all heaps in the pool. */
	atomic_t size;
};

static int panthor_heap_ctx_stride(struct panthor_device *ptdev)
{
	u32 l2_features = ptdev->gpu_info.l2_features;
	u32 gpu_cache_line_size = GPU_L2_FEATURES_LINE_SIZE(l2_features);

	return ALIGN(HEAP_CONTEXT_SIZE, gpu_cache_line_size);
}

static int panthor_get_heap_ctx_offset(struct panthor_heap_pool *pool, int id)
{
	return panthor_heap_ctx_stride(pool->ptdev) * id;
}

static void *panthor_get_heap_ctx(struct panthor_heap_pool *pool, int id)
{
	return pool->gpu_contexts->kmap +
	       panthor_get_heap_ctx_offset(pool, id);
}

static void panthor_free_heap_chunk(struct panthor_heap_pool *pool,
				    struct panthor_heap *heap,
				    struct panthor_heap_chunk *chunk)
{
	mutex_lock(&heap->lock);
	list_del(&chunk->node);
	heap->chunk_count--;
	mutex_unlock(&heap->lock);

	atomic_sub(heap->chunk_size, &pool->size);

	panthor_kernel_bo_destroy(chunk->bo);
	kfree(chunk);
}

static int panthor_alloc_heap_chunk(struct panthor_heap_pool *pool,
				    struct panthor_heap *heap,
				    bool initial_chunk)
{
	struct panthor_heap_chunk *chunk;
	struct panthor_heap_chunk_header *hdr;
	int ret;

	chunk = kmalloc(sizeof(*chunk), GFP_KERNEL);
	if (!chunk)
		return -ENOMEM;

	chunk->bo = panthor_kernel_bo_create(pool->ptdev, pool->vm, heap->chunk_size,
					     DRM_PANTHOR_BO_NO_MMAP,
					     DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC,
					     PANTHOR_VM_KERNEL_AUTO_VA,
					     "Tiler heap chunk");
	if (IS_ERR(chunk->bo)) {
		ret = PTR_ERR(chunk->bo);
		goto err_free_chunk;
	}

	ret = panthor_kernel_bo_vmap(chunk->bo);
	if (ret)
		goto err_destroy_bo;

	hdr = chunk->bo->kmap;
	memset(hdr, 0, sizeof(*hdr));

	if (initial_chunk && !list_empty(&heap->chunks)) {
		struct panthor_heap_chunk *prev_chunk;
		u64 prev_gpuva;

		prev_chunk = list_first_entry(&heap->chunks,
					      struct panthor_heap_chunk,
					      node);

		prev_gpuva = panthor_kernel_bo_gpuva(prev_chunk->bo);
		hdr->next = (prev_gpuva & GENMASK_ULL(63, 12)) |
			    (heap->chunk_size >> 12);
	}

	panthor_kernel_bo_vunmap(chunk->bo);

	mutex_lock(&heap->lock);
	list_add(&chunk->node, &heap->chunks);
	heap->chunk_count++;
	mutex_unlock(&heap->lock);

	atomic_add(heap->chunk_size, &pool->size);

	return 0;

err_destroy_bo:
	panthor_kernel_bo_destroy(chunk->bo);

err_free_chunk:
	kfree(chunk);

	return ret;
}

static void panthor_free_heap_chunks(struct panthor_heap_pool *pool,
				     struct panthor_heap *heap)
{
	struct panthor_heap_chunk *chunk, *tmp;

	list_for_each_entry_safe(chunk, tmp, &heap->chunks, node)
		panthor_free_heap_chunk(pool, heap, chunk);
}

static int panthor_alloc_heap_chunks(struct panthor_heap_pool *pool,
				     struct panthor_heap *heap,
				     u32 chunk_count)
{
	int ret;
	u32 i;

	for (i = 0; i < chunk_count; i++) {
		ret = panthor_alloc_heap_chunk(pool, heap, true);
		if (ret)
			return ret;
	}

	return 0;
}

static int
panthor_heap_destroy_locked(struct panthor_heap_pool *pool, u32 handle)
{
	struct panthor_heap *heap;

	heap = xa_erase(&pool->xa, handle);
	if (!heap)
		return -EINVAL;

	panthor_free_heap_chunks(pool, heap);
	mutex_destroy(&heap->lock);
	kfree(heap);
	return 0;
}

/**
 * panthor_heap_destroy() - Destroy a heap context
 * @pool: Pool this context belongs to.
 * @handle: Handle returned by panthor_heap_create().
 */
int panthor_heap_destroy(struct panthor_heap_pool *pool, u32 handle)
{
	int ret;

	down_write(&pool->lock);
	ret = panthor_heap_destroy_locked(pool, handle);
	up_write(&pool->lock);

	return ret;
}

/**
 * panthor_heap_create() - Create a heap context
 * @pool: Pool to instantiate the heap context from.
 * @initial_chunk_count: Number of chunk allocated at initialization time.
 * Must be at least 1.
 * @chunk_size: The size of each chunk. Must be page-aligned and lie in the
 * [128k:8M] range.
 * @max_chunks: Maximum number of chunks that can be allocated.
 * @target_in_flight: Maximum number of in-flight render passes.
 * @heap_ctx_gpu_va: Pointer holding the GPU address of the allocated heap
 * context.
 * @first_chunk_gpu_va: Pointer holding the GPU address of the first chunk
 * assigned to the heap context.
 *
 * Return: a positive handle on success, a negative error otherwise.
 */
int panthor_heap_create(struct panthor_heap_pool *pool,
			u32 initial_chunk_count,
			u32 chunk_size,
			u32 max_chunks,
			u32 target_in_flight,
			u64 *heap_ctx_gpu_va,
			u64 *first_chunk_gpu_va)
{
	struct panthor_heap *heap;
	struct panthor_heap_chunk *first_chunk;
	struct panthor_vm *vm;
	int ret = 0;
	u32 id;

	if (initial_chunk_count == 0)
		return -EINVAL;

	if (initial_chunk_count > max_chunks)
		return -EINVAL;

	if (!IS_ALIGNED(chunk_size, PAGE_SIZE) ||
	    chunk_size < SZ_128K || chunk_size > SZ_8M)
		return -EINVAL;

	down_read(&pool->lock);
	vm = panthor_vm_get(pool->vm);
	up_read(&pool->lock);

	/* The pool has been destroyed, we can't create a new heap. */
	if (!vm)
		return -EINVAL;

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap) {
		ret = -ENOMEM;
		goto err_put_vm;
	}

	mutex_init(&heap->lock);
	INIT_LIST_HEAD(&heap->chunks);
	heap->chunk_size = chunk_size;
	heap->max_chunks = max_chunks;
	heap->target_in_flight = target_in_flight;

	ret = panthor_alloc_heap_chunks(pool, heap, initial_chunk_count);
	if (ret)
		goto err_free_heap;

	first_chunk = list_first_entry(&heap->chunks,
				       struct panthor_heap_chunk,
				       node);
	*first_chunk_gpu_va = panthor_kernel_bo_gpuva(first_chunk->bo);

	down_write(&pool->lock);
	/* The pool has been destroyed, we can't create a new heap. */
	if (!pool->vm) {
		ret = -EINVAL;
	} else {
		ret = xa_alloc(&pool->xa, &id, heap,
			       XA_LIMIT(0, MAX_HEAPS_PER_POOL - 1), GFP_KERNEL);
		if (!ret) {
			void *gpu_ctx = panthor_get_heap_ctx(pool, id);

			memset(gpu_ctx, 0, panthor_heap_ctx_stride(pool->ptdev));
			*heap_ctx_gpu_va = panthor_kernel_bo_gpuva(pool->gpu_contexts) +
					   panthor_get_heap_ctx_offset(pool, id);
		}
	}
	up_write(&pool->lock);

	if (ret)
		goto err_free_heap;

	panthor_vm_put(vm);
	return id;

err_free_heap:
	panthor_free_heap_chunks(pool, heap);
	mutex_destroy(&heap->lock);
	kfree(heap);

err_put_vm:
	panthor_vm_put(vm);
	return ret;
}

/**
 * panthor_heap_return_chunk() - Return an unused heap chunk
 * @pool: The pool this heap belongs to.
 * @heap_gpu_va: The GPU address of the heap context.
 * @chunk_gpu_va: The chunk VA to return.
 *
 * This function is used when a chunk allocated with panthor_heap_grow()
 * couldn't be linked to the heap context through the FW interface because
 * the group requesting the allocation was scheduled out in the meantime.
 */
int panthor_heap_return_chunk(struct panthor_heap_pool *pool,
			      u64 heap_gpu_va,
			      u64 chunk_gpu_va)
{
	u64 offset = heap_gpu_va - panthor_kernel_bo_gpuva(pool->gpu_contexts);
	u32 heap_id = (u32)offset / panthor_heap_ctx_stride(pool->ptdev);
	struct panthor_heap_chunk *chunk, *tmp, *removed = NULL;
	struct panthor_heap *heap;
	int ret;

	if (offset > U32_MAX || heap_id >= MAX_HEAPS_PER_POOL)
		return -EINVAL;

	down_read(&pool->lock);
	heap = xa_load(&pool->xa, heap_id);
	if (!heap) {
		ret = -EINVAL;
		goto out_unlock;
	}

	chunk_gpu_va &= GENMASK_ULL(63, 12);

	mutex_lock(&heap->lock);
	list_for_each_entry_safe(chunk, tmp, &heap->chunks, node) {
		if (panthor_kernel_bo_gpuva(chunk->bo) == chunk_gpu_va) {
			removed = chunk;
			list_del(&chunk->node);
			heap->chunk_count--;
			atomic_sub(heap->chunk_size, &pool->size);
			break;
		}
	}
	mutex_unlock(&heap->lock);

	if (removed) {
		panthor_kernel_bo_destroy(chunk->bo);
		kfree(chunk);
		ret = 0;
	} else {
		ret = -EINVAL;
	}

out_unlock:
	up_read(&pool->lock);
	return ret;
}

/**
 * panthor_heap_grow() - Make a heap context grow.
 * @pool: The pool this heap belongs to.
 * @heap_gpu_va: The GPU address of the heap context.
 * @renderpasses_in_flight: Number of render passes currently in-flight.
 * @pending_frag_count: Number of fragment jobs waiting for execution/completion.
 * @new_chunk_gpu_va: Pointer used to return the chunk VA.
 *
 * Return:
 * - 0 if a new heap was allocated
 * - -ENOMEM if the tiler context reached the maximum number of chunks
 *   or if too many render passes are in-flight
 *   or if the allocation failed
 * - -EINVAL if any of the arguments passed to panthor_heap_grow() is invalid
 */
int panthor_heap_grow(struct panthor_heap_pool *pool,
		      u64 heap_gpu_va,
		      u32 renderpasses_in_flight,
		      u32 pending_frag_count,
		      u64 *new_chunk_gpu_va)
{
	u64 offset = heap_gpu_va - panthor_kernel_bo_gpuva(pool->gpu_contexts);
	u32 heap_id = (u32)offset / panthor_heap_ctx_stride(pool->ptdev);
	struct panthor_heap_chunk *chunk;
	struct panthor_heap *heap;
	int ret;

	if (offset > U32_MAX || heap_id >= MAX_HEAPS_PER_POOL)
		return -EINVAL;

	down_read(&pool->lock);
	heap = xa_load(&pool->xa, heap_id);
	if (!heap) {
		ret = -EINVAL;
		goto out_unlock;
	}

	/* If we reached the target in-flight render passes, or if we
	 * reached the maximum number of chunks, let the FW figure another way to
	 * find some memory (wait for render passes to finish, or call the exception
	 * handler provided by the userspace driver, if any).
	 */
	if (renderpasses_in_flight > heap->target_in_flight ||
	    heap->chunk_count >= heap->max_chunks) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	/* FIXME: panthor_alloc_heap_chunk() triggers a kernel BO creation,
	 * which goes through the blocking allocation path. Ultimately, we
	 * want a non-blocking allocation, so we can immediately report to the
	 * FW when the system is running out of memory. In that case, the FW
	 * can call a user-provided exception handler, which might try to free
	 * some tiler memory by issuing an intermediate fragment job. If the
	 * exception handler can't do anything, it will flag the queue as
	 * faulty so the job that triggered this tiler chunk allocation and all
	 * further jobs in this queue fail immediately instead of having to
	 * wait for the job timeout.
	 */
	ret = panthor_alloc_heap_chunk(pool, heap, false);
	if (ret)
		goto out_unlock;

	chunk = list_first_entry(&heap->chunks,
				 struct panthor_heap_chunk,
				 node);
	*new_chunk_gpu_va = (panthor_kernel_bo_gpuva(chunk->bo) & GENMASK_ULL(63, 12)) |
			    (heap->chunk_size >> 12);
	ret = 0;

out_unlock:
	up_read(&pool->lock);
	return ret;
}

static void panthor_heap_pool_release(struct kref *refcount)
{
	struct panthor_heap_pool *pool =
		container_of(refcount, struct panthor_heap_pool, refcount);

	xa_destroy(&pool->xa);
	kfree(pool);
}

/**
 * panthor_heap_pool_put() - Release a heap pool reference
 * @pool: Pool to release the reference on. Can be NULL.
 */
void panthor_heap_pool_put(struct panthor_heap_pool *pool)
{
	if (pool)
		kref_put(&pool->refcount, panthor_heap_pool_release);
}

/**
 * panthor_heap_pool_get() - Get a heap pool reference
 * @pool: Pool to get the reference on. Can be NULL.
 *
 * Return: @pool.
 */
struct panthor_heap_pool *
panthor_heap_pool_get(struct panthor_heap_pool *pool)
{
	if (pool)
		kref_get(&pool->refcount);

	return pool;
}

/**
 * panthor_heap_pool_create() - Create a heap pool
 * @ptdev: Device.
 * @vm: The VM this heap pool will be attached to.
 *
 * Heap pools might contain up to 128 heap contexts, and are per-VM.
 *
 * Return: A valid pointer on success, a negative error code otherwise.
 */
struct panthor_heap_pool *
panthor_heap_pool_create(struct panthor_device *ptdev, struct panthor_vm *vm)
{
	size_t bosize = ALIGN(MAX_HEAPS_PER_POOL *
			      panthor_heap_ctx_stride(ptdev),
			      4096);
	struct panthor_heap_pool *pool;
	int ret = 0;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	/* We want a weak ref here: the heap pool belongs to the VM, so we're
	 * sure that, as long as the heap pool exists, the VM exists too.
	 */
	pool->vm = vm;
	pool->ptdev = ptdev;
	init_rwsem(&pool->lock);
	xa_init_flags(&pool->xa, XA_FLAGS_ALLOC);
	kref_init(&pool->refcount);

	pool->gpu_contexts = panthor_kernel_bo_create(ptdev, vm, bosize,
						      DRM_PANTHOR_BO_NO_MMAP,
						      DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC,
						      PANTHOR_VM_KERNEL_AUTO_VA,
						      "Heap pool");
	if (IS_ERR(pool->gpu_contexts)) {
		ret = PTR_ERR(pool->gpu_contexts);
		goto err_destroy_pool;
	}

	ret = panthor_kernel_bo_vmap(pool->gpu_contexts);
	if (ret)
		goto err_destroy_pool;

	atomic_add(pool->gpu_contexts->obj->size, &pool->size);

	return pool;

err_destroy_pool:
	panthor_heap_pool_destroy(pool);
	return ERR_PTR(ret);
}

/**
 * panthor_heap_pool_destroy() - Destroy a heap pool.
 * @pool: Pool to destroy.
 *
 * This function destroys all heap contexts and their resources. Thus
 * preventing any use of the heap context or the chunk attached to them
 * after that point.
 *
 * If the GPU still has access to some heap contexts, a fault should be
 * triggered, which should flag the command stream groups using these
 * context as faulty.
 *
 * The heap pool object is only released when all references to this pool
 * are released.
 */
void panthor_heap_pool_destroy(struct panthor_heap_pool *pool)
{
	struct panthor_heap *heap;
	unsigned long i;

	if (!pool)
		return;

	down_write(&pool->lock);
	xa_for_each(&pool->xa, i, heap)
		drm_WARN_ON(&pool->ptdev->base, panthor_heap_destroy_locked(pool, i));

	if (!IS_ERR_OR_NULL(pool->gpu_contexts)) {
		atomic_sub(pool->gpu_contexts->obj->size, &pool->size);
		panthor_kernel_bo_destroy(pool->gpu_contexts);
	}

	/* Reflects the fact the pool has been destroyed. */
	pool->vm = NULL;
	up_write(&pool->lock);

	panthor_heap_pool_put(pool);
}

/**
 * panthor_heap_pool_size() - Get a heap pool's total size
 * @pool: Pool whose total chunks size to return
 *
 * Returns the aggregated size of all chunks for all heaps in the pool
 *
 */
size_t panthor_heap_pool_size(struct panthor_heap_pool *pool)
{
	if (!pool)
		return 0;

	return atomic_read(&pool->size);
}
