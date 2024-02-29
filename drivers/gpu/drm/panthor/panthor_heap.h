/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2023 Collabora ltd. */

#ifndef __PANTHOR_HEAP_H__
#define __PANTHOR_HEAP_H__

#include <linux/types.h>

struct panthor_device;
struct panthor_heap_pool;
struct panthor_vm;

int panthor_heap_create(struct panthor_heap_pool *pool,
			u32 initial_chunk_count,
			u32 chunk_size,
			u32 max_chunks,
			u32 target_in_flight,
			u64 *heap_ctx_gpu_va,
			u64 *first_chunk_gpu_va);
int panthor_heap_destroy(struct panthor_heap_pool *pool, u32 handle);

struct panthor_heap_pool *
panthor_heap_pool_create(struct panthor_device *ptdev, struct panthor_vm *vm);
void panthor_heap_pool_destroy(struct panthor_heap_pool *pool);

struct panthor_heap_pool *
panthor_heap_pool_get(struct panthor_heap_pool *pool);
void panthor_heap_pool_put(struct panthor_heap_pool *pool);

int panthor_heap_grow(struct panthor_heap_pool *pool,
		      u64 heap_gpu_va,
		      u32 renderpasses_in_flight,
		      u32 pending_frag_count,
		      u64 *new_chunk_gpu_va);
int panthor_heap_return_chunk(struct panthor_heap_pool *pool,
			      u64 heap_gpu_va,
			      u64 chunk_gpu_va);

#endif
