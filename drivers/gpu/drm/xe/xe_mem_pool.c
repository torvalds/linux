// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <linux/kernel.h>

#include <drm/drm_managed.h>

#include "instructions/xe_mi_commands.h"
#include "xe_bo.h"
#include "xe_device_types.h"
#include "xe_map.h"
#include "xe_mem_pool.h"
#include "xe_mem_pool_types.h"
#include "xe_tile_printk.h"

/**
 * struct xe_mem_pool - DRM MM pool for sub-allocating memory from a BO on an
 * XE tile.
 *
 * The XE memory pool is a DRM MM manager that provides sub-allocation of memory
 * from a backing buffer object (BO) on a specific XE tile. It is designed to
 * manage memory for GPU workloads, allowing for efficient allocation and
 * deallocation of memory regions within the BO.
 *
 * The memory pool maintains a primary BO that is pinned in the GGTT and mapped
 * into the CPU address space for direct access. Optionally, it can also maintain
 * a shadow BO that can be used for atomic updates to the primary BO's contents.
 *
 * The API provided by the memory pool allows clients to allocate and free memory
 * regions, retrieve GPU and CPU addresses, and synchronize data between the
 * primary and shadow BOs as needed.
 */
struct xe_mem_pool {
	/** @base: Range allocator over [0, @size) in bytes */
	struct drm_mm base;
	/** @bo: Active pool BO (GGTT-pinned, CPU-mapped). */
	struct xe_bo *bo;
	/** @shadow: Shadow BO for atomic command updates. */
	struct xe_bo *shadow;
	/** @swap_guard: Timeline guard updating @bo and @shadow */
	struct mutex swap_guard;
	/** @cpu_addr: CPU virtual address of the active BO. */
	void *cpu_addr;
	/** @is_iomem: Indicates if the BO mapping is I/O memory. */
	bool is_iomem;
};

static struct xe_mem_pool *node_to_pool(struct xe_mem_pool_node *node)
{
	return container_of(node->sa_node.mm, struct xe_mem_pool, base);
}

static struct xe_tile *pool_to_tile(struct xe_mem_pool *pool)
{
	return pool->bo->tile;
}

static void fini_pool_action(struct drm_device *drm, void *arg)
{
	struct xe_mem_pool *pool = arg;

	if (pool->is_iomem)
		kvfree(pool->cpu_addr);

	drm_mm_takedown(&pool->base);
}

static int pool_shadow_init(struct xe_mem_pool *pool)
{
	struct xe_tile *tile = pool->bo->tile;
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_bo *shadow;
	int ret;

	xe_assert(xe, !pool->shadow);

	ret = drmm_mutex_init(&xe->drm, &pool->swap_guard);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		fs_reclaim_acquire(GFP_KERNEL);
		might_lock(&pool->swap_guard);
		fs_reclaim_release(GFP_KERNEL);
	}
	shadow = xe_managed_bo_create_pin_map(xe, tile,
					      xe_bo_size(pool->bo),
					      XE_BO_FLAG_VRAM_IF_DGFX(tile) |
					      XE_BO_FLAG_GGTT |
					      XE_BO_FLAG_GGTT_INVALIDATE |
					      XE_BO_FLAG_PINNED_NORESTORE);
	if (IS_ERR(shadow))
		return PTR_ERR(shadow);

	pool->shadow = shadow;

	return 0;
}

/**
 * xe_mem_pool_init() - Initialize memory pool.
 * @tile: the &xe_tile where allocate.
 * @size: number of bytes to allocate.
 * @guard: the size of the guard region at the end of the BO that is not
 * sub-allocated, in bytes.
 * @flags: flags to use to create shadow pool.
 *
 * Initializes a memory pool for sub-allocating memory from a backing BO on the
 * specified XE tile. The backing BO is pinned in the GGTT and mapped into
 * the CPU address space for direct access. Optionally, a shadow BO can also be
 * initialized for atomic updates to the primary BO's contents.
 *
 * Returns: a pointer to the &xe_mem_pool, or an error pointer on failure.
 */
struct xe_mem_pool *xe_mem_pool_init(struct xe_tile *tile, u32 size,
				     u32 guard, int flags)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_mem_pool *pool;
	struct xe_bo *bo;
	u32 managed_size;
	int ret;

	xe_tile_assert(tile, size > guard);
	managed_size = size - guard;

	pool = drmm_kzalloc(&xe->drm, sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	bo = xe_managed_bo_create_pin_map(xe, tile, size,
					  XE_BO_FLAG_VRAM_IF_DGFX(tile) |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_INVALIDATE |
					  XE_BO_FLAG_PINNED_NORESTORE);
	if (IS_ERR(bo)) {
		xe_tile_err(tile, "Failed to prepare %uKiB BO for mem pool (%pe)\n",
			    size / SZ_1K, bo);
		return ERR_CAST(bo);
	}
	pool->bo = bo;
	pool->is_iomem = bo->vmap.is_iomem;

	if (pool->is_iomem) {
		pool->cpu_addr = kvzalloc(size, GFP_KERNEL);
		if (!pool->cpu_addr)
			return ERR_PTR(-ENOMEM);
	} else {
		pool->cpu_addr = bo->vmap.vaddr;
	}

	if (flags & XE_MEM_POOL_BO_FLAG_INIT_SHADOW_COPY) {
		ret = pool_shadow_init(pool);

		if (ret)
			goto out_err;
	}

	drm_mm_init(&pool->base, 0, managed_size);
	ret = drmm_add_action_or_reset(&xe->drm, fini_pool_action, pool);
	if (ret)
		return ERR_PTR(ret);

	return pool;

out_err:
	if (flags & XE_MEM_POOL_BO_FLAG_INIT_SHADOW_COPY)
		xe_tile_err(tile,
			    "Failed to initialize shadow BO for mem pool (%d)\n", ret);
	if (bo->vmap.is_iomem)
		kvfree(pool->cpu_addr);
	return ERR_PTR(ret);
}

/**
 * xe_mem_pool_sync() - Copy the entire contents of the main pool to shadow pool.
 * @pool: the memory pool containing the primary and shadow BOs.
 *
 * Copies the entire contents of the primary pool to the shadow pool. This must
 * be done after xe_mem_pool_init() with the XE_MEM_POOL_BO_FLAG_INIT_SHADOW_COPY
 * flag to ensure that the shadow pool has the same initial contents as the primary
 * pool. After this initial synchronization, clients can choose to synchronize the
 * shadow pool with the primary pool on a node  basis using
 * xe_mem_pool_sync_shadow_locked() as needed.
 *
 * Return: None.
 */
void xe_mem_pool_sync(struct xe_mem_pool *pool)
{
	struct xe_tile *tile = pool_to_tile(pool);
	struct xe_device *xe = tile_to_xe(tile);

	xe_tile_assert(tile, pool->shadow);

	xe_map_memcpy_to(xe, &pool->shadow->vmap, 0,
			 pool->cpu_addr, xe_bo_size(pool->bo));
}

/**
 * xe_mem_pool_swap_shadow_locked() - Swap the primary BO with the shadow BO.
 * @pool: the memory pool containing the primary and shadow BOs.
 *
 * Swaps the primary buffer object with the shadow buffer object in the mem
 * pool. This allows for atomic updates to the contents of the primary BO
 * by first writing to the shadow BO and then swapping it with the primary BO.
 * Swap_guard must be held to ensure synchronization with any concurrent swap
 * operations.
 *
 * Return: None.
 */
void xe_mem_pool_swap_shadow_locked(struct xe_mem_pool *pool)
{
	struct xe_tile *tile = pool_to_tile(pool);

	xe_tile_assert(tile, pool->shadow);
	lockdep_assert_held(&pool->swap_guard);

	swap(pool->bo, pool->shadow);
	if (!pool->bo->vmap.is_iomem)
		pool->cpu_addr = pool->bo->vmap.vaddr;
}

/**
 * xe_mem_pool_sync_shadow_locked() - Copy node from primary pool to shadow pool.
 * @node: the node allocated in the memory pool.
 *
 * Copies the specified batch buffer from the primary pool to the shadow pool.
 * Swap_guard must be held to ensure synchronization with any concurrent swap
 * operations.
 *
 * Return: None.
 */
void xe_mem_pool_sync_shadow_locked(struct xe_mem_pool_node *node)
{
	struct xe_mem_pool *pool = node_to_pool(node);
	struct xe_tile *tile = pool_to_tile(pool);
	struct xe_device *xe = tile_to_xe(tile);
	struct drm_mm_node *sa_node = &node->sa_node;

	xe_tile_assert(tile, pool->shadow);
	lockdep_assert_held(&pool->swap_guard);

	xe_map_memcpy_to(xe, &pool->shadow->vmap,
			 sa_node->start,
			 pool->cpu_addr + sa_node->start,
			 sa_node->size);
}

/**
 * xe_mem_pool_gpu_addr() - Retrieve GPU address of memory pool.
 * @pool: the memory pool
 *
 * Returns: GGTT address of the memory pool.
 */
u64 xe_mem_pool_gpu_addr(struct xe_mem_pool *pool)
{
	return xe_bo_ggtt_addr(pool->bo);
}

/**
 * xe_mem_pool_cpu_addr() - Retrieve CPU address of manager pool.
 * @pool: the memory pool
 *
 * Returns: CPU virtual address of memory pool.
 */
void *xe_mem_pool_cpu_addr(struct xe_mem_pool *pool)
{
	return pool->cpu_addr;
}

/**
 * xe_mem_pool_bo_swap_guard() - Retrieve the mutex used to guard swap
 * operations on a memory pool.
 * @pool: the memory pool
 *
 * Returns: Swap guard mutex or NULL if shadow pool is not created.
 */
struct mutex *xe_mem_pool_bo_swap_guard(struct xe_mem_pool *pool)
{
	if (!pool->shadow)
		return NULL;

	return &pool->swap_guard;
}

/**
 * xe_mem_pool_bo_flush_write() - Copy the data from the sub-allocation
 * to the GPU memory.
 * @node: the node allocated in the memory pool to flush.
 */
void xe_mem_pool_bo_flush_write(struct xe_mem_pool_node *node)
{
	struct xe_mem_pool *pool = node_to_pool(node);
	struct xe_tile *tile = pool_to_tile(pool);
	struct xe_device *xe = tile_to_xe(tile);
	struct drm_mm_node *sa_node = &node->sa_node;

	if (!pool->bo->vmap.is_iomem)
		return;

	xe_map_memcpy_to(xe, &pool->bo->vmap, sa_node->start,
			 pool->cpu_addr + sa_node->start,
			 sa_node->size);
}

/**
 * xe_mem_pool_bo_sync_read() - Copy the data from GPU memory to the
 * sub-allocation.
 * @node: the node allocated in the memory pool to read back.
 */
void xe_mem_pool_bo_sync_read(struct xe_mem_pool_node *node)
{
	struct xe_mem_pool *pool = node_to_pool(node);
	struct xe_tile *tile = pool_to_tile(pool);
	struct xe_device *xe = tile_to_xe(tile);
	struct drm_mm_node *sa_node = &node->sa_node;

	if (!pool->bo->vmap.is_iomem)
		return;

	xe_map_memcpy_from(xe, pool->cpu_addr + sa_node->start,
			   &pool->bo->vmap, sa_node->start, sa_node->size);
}

/**
 * xe_mem_pool_alloc_node() - Allocate a new node for use with xe_mem_pool.
 *
 * Returns: node structure or an ERR_PTR(-ENOMEM).
 */
struct xe_mem_pool_node *xe_mem_pool_alloc_node(void)
{
	struct xe_mem_pool_node *node = kzalloc_obj(*node);

	if (!node)
		return ERR_PTR(-ENOMEM);

	return node;
}

/**
 * xe_mem_pool_insert_node() - Insert a node into the memory pool.
 * @pool: the memory pool to insert into
 * @node: the node to insert
 * @size: the size of the node to be allocated in bytes.
 *
 * Inserts a node into the specified memory pool using drm_mm for
 * allocation.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int xe_mem_pool_insert_node(struct xe_mem_pool *pool,
			    struct xe_mem_pool_node *node, u32 size)
{
	if (!pool)
		return -EINVAL;

	return drm_mm_insert_node(&pool->base, &node->sa_node, size);
}

/**
 * xe_mem_pool_free_node() - Free a node allocated from the memory pool.
 * @node: the node to free
 *
 * Returns: None.
 */
void xe_mem_pool_free_node(struct xe_mem_pool_node *node)
{
	if (!node)
		return;

	drm_mm_remove_node(&node->sa_node);
	kfree(node);
}

/**
 * xe_mem_pool_node_cpu_addr() - Retrieve CPU address of the node.
 * @node: the node allocated in the memory pool
 *
 * Returns: CPU virtual address of the node.
 */
void *xe_mem_pool_node_cpu_addr(struct xe_mem_pool_node *node)
{
	struct xe_mem_pool *pool = node_to_pool(node);

	return xe_mem_pool_cpu_addr(pool) + node->sa_node.start;
}

/**
 * xe_mem_pool_dump() - Dump the state of the DRM MM manager for debugging.
 * @pool: the memory pool info be dumped.
 * @p: The DRM printer to use for output.
 *
 * Only the drm managed region is dumped, not the state of the BOs or any other
 * pool information.
 *
 * Returns: None.
 */
void xe_mem_pool_dump(struct xe_mem_pool *pool, struct drm_printer *p)
{
	drm_mm_print(&pool->base, p);
}
