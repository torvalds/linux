/**
 * Copyright (C) Advanced Micro Devices, Inc. All rights reserved.
 *
 * You may not use this software and documentation (if any) (collectively, the
 * "Materials") except in compliance with the terms and conditions of the
 * Software License Agreement included with the Materials or otherwise as set
 * forth in writing and signed by you and an authorized signatory of AMD.
 *
 * If you do not have a copy of the Software License Agreement, contact your AMD
 * representative for a copy. You agree that you will not reverse engineer or
 * decompile the Materials, in whole or in part, except as allowed by applicable
 * law.
 *
 * THE MATERIALS ARE DISTRIBUTED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
 */
#ifndef DC_MEMORY_POOL_H
#define DC_MEMORY_POOL_H

#include "os_types.h"

/**
 * Non-resizable lock-free memory pool with fixed capacity.
 *
 * Custom implementation is used over Linux llist, as llist requires external
 * locking if more than one thread pops from the list.
 *
 * Emplace and erase operations do not use any mutexes or spinlocks, which
 * guarantees forward progress even if another thread
 * has been suspended in the middle of the call. High contention might result
 * in multiple internal retries, but will eventually either succeed once other
 * threads stop actively modifying the pool, or fail if the pool is empty.
 *
 * Implemented as Treiber stack using indexed free list and head with generation
 * counter to solve ABA problem, as each modification of the head increments
 * the generation, preventing issue of `push(A)` being indistinguishable from
 * `push(A); { push(B); pop(B); }` to another thread, corrupting data structure.
 */
struct dc_memory_pool;

/**
 * Create dc_memory_pool.
 *
 * @param size Non-zero pool block size, all acquires will be of this size.
 * @param capacity Non-zero number of blocks that can be acquired from the pool.
 * @return Pointer to the pool if succeeded, null if failed.
 */
__must_check struct dc_memory_pool *dc_memory_pool_create(size_t size,
							  size_t capacity);

/**
 * Destroy given pool and free all allocated memory, invalidating any pointers.
 *
 * This operation is not synchronized, calling any other operation on the pool
 * while it is being destroyed results in Undefined Behavior.
 *
 * @param pool Can be null to support common destruction patterns.
 */
void dc_memory_pool_destroy(struct dc_memory_pool *pool);

/**
 * Acquire single block from pool.
 *
 * @param pool Cannot be null.
 * @return Pointer to block if succeeded, null if empty.
 */
__must_check void *dc_memory_pool_acquire(struct dc_memory_pool *pool);

/**
 * Release previously acquired block, freeing it for others to acquire.
 *
 * @param pool Cannot be null.
 * @param memory Cannot be null, has to be owned by the pool.
 */
void dc_memory_pool_release(struct dc_memory_pool *pool, void *memory);

/**
 * Check if given memory is owned by the pool to facilitate arenas.
 *
 * @warning if (owns(pool, p)) release(pool, p);` pattern is not safe
 * if used with same `p` from multiple unsynchronized threads.
 * If multiple owning threads are desired, use krefs to assure single release.
 *
 * @param pool Cannot be null.
 * @param memory Cannot be null.
 * @return True if the memory was previously acquired from the pool.
 */
__must_check bool dc_memory_pool_owns(const struct dc_memory_pool *pool,
				      const void *memory);

/**
 * Get pool block size.
 *
 * @param pool Cannot be null.
 * @return Pool block size as given to dc_memory_pool_create().
 */
__must_check size_t dc_memory_pool_size(const struct dc_memory_pool *pool);

/**
 * Get pool block count.
 *
 * @param pool Cannot be null.
 * @return Pool block count as given to dc_memory_pool_create().
 */
__must_check size_t dc_memory_pool_capacity(const struct dc_memory_pool *pool);

#endif // Header guard
