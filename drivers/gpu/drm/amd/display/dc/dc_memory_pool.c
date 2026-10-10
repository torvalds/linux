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
#include "dc_memory_pool.h"

#include <linux/atomic.h>

enum {
	DC_MEMORY_POOL_SENTINEL = -1,
	DC_MEMORY_POOL_ACQUIRED = -2,
	DC_MEMORY_POOL_RELEASED = -3,

	// Correct autoincrement for negative numbers
	DC_MEMORY_POOL_ENUM_SIZE_IMPL,
	DC_MEMORY_POOL_ENUM_SIZE = 1 - DC_MEMORY_POOL_ENUM_SIZE_IMPL,
};

// Align everything to size of page to avoid false sharing
struct dc_memory_pool_page {
	__aligned(PAGE_SIZE) char _dummy[PAGE_SIZE];
};

// Generation counter protects against ABA-problem
union index_t {
	struct {
		int32_t value;
		uint32_t generation;
	} s;

	int64_t raw;
};

struct dc_memory_pool {
	// Values and pointers constant after initialization, can share page
	__aligned(PAGE_SIZE) size_t size;
	size_t capacity;

	void *unaligned_pool;
	void *unaligned_memory;
	struct dc_memory_pool_page *memory;
	atomic_t *free_list;

	// Updated every operation, use separate page to avoid false sharing
	__aligned(PAGE_SIZE) atomic64_t free_head; // index_t
	char _reserved2[PAGE_SIZE - sizeof(atomic64_t)];
};

static_assert(
		sizeof(struct dc_memory_pool) == 2 * PAGE_SIZE,
		"dc_memory_pool size should be 2 pages"
);
static_assert(
		offsetof(struct dc_memory_pool, free_head) == PAGE_SIZE,
		"dc_memory_pool free_head should be aligned to page"
);

static size_t divide_ceiling(size_t x, size_t d)
{
	return (x + d - 1) / d;
}

static size_t round_up_to_multiple(size_t x, size_t m)
{
	return divide_ceiling(x, m) * m;
}

static size_t size_in_pages(size_t x)
{
	return divide_ceiling(x, PAGE_SIZE);
}

static void *page_align_up(void *p)
{
	intptr_t i = (intptr_t)p;

	i = (intptr_t)round_up_to_multiple((size_t)i, PAGE_SIZE);
	return (void *)i;
}

__must_check struct dc_memory_pool *dc_memory_pool_create(size_t size,
							  size_t capacity)
{
	if (!size || !capacity)
		return NULL;

	// Limited by split between size and generation in index_t
	if (capacity >= (uint32_t)-DC_MEMORY_POOL_ENUM_SIZE)
		return NULL;

	// uint32_t because that's what alloc functions take
	const uint32_t block_pages = (uint32_t)size_in_pages(size);
	const uint32_t lines = (uint32_t)capacity * block_pages;
	const uint32_t padded_struct_size =
		sizeof(struct dc_memory_pool) + PAGE_SIZE;

	void *unaligned_pool = kzalloc(padded_struct_size, GFP_KERNEL);

	if (!unaligned_pool)
		return NULL;

	struct dc_memory_pool *pool = page_align_up(unaligned_pool);

	// Compound literals create temporary in debug driver, exceeding stack size
	pool->size = size;
	pool->capacity = capacity;
	pool->unaligned_pool = unaligned_pool;
	pool->unaligned_memory = kcalloc(lines + 1, PAGE_SIZE, GFP_KERNEL);
	pool->memory = page_align_up(pool->unaligned_memory);
	pool->free_list = kcalloc((uint32_t)capacity, sizeof(atomic_t), GFP_KERNEL);
	pool->free_head = (atomic64_t)ATOMIC_INIT(0);

	if (!pool->unaligned_memory || !pool->free_list) {
		dc_memory_pool_destroy(pool);
		return NULL;
	}

	for (int32_t i = 0; i < (int32_t)capacity; i++)
		atomic_set_release(&pool->free_list[i], i + 1);

	atomic_set_release(&pool->free_list[capacity - 1],
			   DC_MEMORY_POOL_SENTINEL);

	return pool;
}

void dc_memory_pool_destroy(struct dc_memory_pool *pool)
{
	if (!pool)
		return;

	kfree(pool->free_list);
	kfree(pool->unaligned_memory);
	kfree(pool->unaligned_pool);
}

static int32_t dc_memory_pool_get_index(const struct dc_memory_pool *pool,
					const void *memory)
{
	if (!pool || !memory) {
		ASSERT(false);
		return DC_MEMORY_POOL_SENTINEL;
	}

	// Direct pointer arithmetic would be UB if called by false `owns()`
	if ((uintptr_t)memory < (uintptr_t)pool->memory)
		return DC_MEMORY_POOL_SENTINEL;

	const uintptr_t distance = (uintptr_t)memory - (uintptr_t)pool->memory;
	const size_t block_size = size_in_pages(pool->size) * PAGE_SIZE;
	const size_t i = (size_t)distance / block_size;

	if (distance % (uintptr_t)block_size != 0)
		return DC_MEMORY_POOL_SENTINEL;

	if (i >= pool->capacity)
		return DC_MEMORY_POOL_SENTINEL;

	return (int32_t)i;
}

static void *dc_memory_pool_get_page(const struct dc_memory_pool *pool,
				     int32_t index)
{
	if (!pool) {
		ASSERT(false);
		return NULL;
	}

	if (index < 0 || index >= (int32_t)pool->capacity) {
		ASSERT(false);
		return NULL;
	}

	return &pool->memory[(size_t)index * size_in_pages(pool->size)];
}

__must_check void *dc_memory_pool_acquire(struct dc_memory_pool *pool)
{
	if (!pool) {
		ASSERT(false);
		return NULL;
	}

	// CAS-atomic `out = head; head = head->next;`
	union index_t old_head = {
		.raw = atomic64_read_acquire(&pool->free_head),
	};
	union index_t new_head = {
		.raw = 0,
	};
	int32_t i = 0;

	do {
		i = old_head.s.value;
		if (i == DC_MEMORY_POOL_SENTINEL)
			return NULL;

		new_head = (union index_t){
			.s.value = atomic_read_acquire(&pool->free_list[i]),
			.s.generation = old_head.s.generation + 1,
		};
	} while (!atomic64_try_cmpxchg(&pool->free_head, &old_head.raw,
				       new_head.raw));

	atomic_set_release(&pool->free_list[i], DC_MEMORY_POOL_ACQUIRED);
	return dc_memory_pool_get_page(pool, i);
}

void dc_memory_pool_release(struct dc_memory_pool *pool, void *memory)
{
	if (!dc_memory_pool_owns(pool, memory)) {
		// Likely acquired in different pool or (racing?) double free
		ASSERT(false);
		return;
	}

	const int32_t i = dc_memory_pool_get_index(pool, memory);

	if (atomic_xchg(&pool->free_list[i], DC_MEMORY_POOL_RELEASED) !=
	    DC_MEMORY_POOL_ACQUIRED) {
		// Double free from two racing threads, use krefs to sync
		ASSERT(false);
		return;
	}

	// CAS-atomic `in->next = head; head = in;`
	union index_t old_head = {
		.raw = atomic64_read_acquire(&pool->free_head),
	};
	union index_t new_head = {
		.raw = 0,
	};

	do {
		atomic_set_release(&pool->free_list[i], old_head.s.value);
		new_head = (union index_t){
			.s.value = i,
			.s.generation = old_head.s.generation + 1,
		};
	} while (!atomic64_try_cmpxchg(&pool->free_head, &old_head.raw,
				       new_head.raw));
}

__must_check bool dc_memory_pool_owns(const struct dc_memory_pool *pool,
				      const void *memory)
{
	const int32_t i = dc_memory_pool_get_index(pool, memory);

	if (i < 0)
		return false;

	if (atomic_read_acquire(&pool->free_list[i]) != DC_MEMORY_POOL_ACQUIRED)
		return false;

	return true;
}

__must_check size_t dc_memory_pool_size(const struct dc_memory_pool *pool)
{
	ASSERT(pool);
	return pool->size;
}

__must_check size_t dc_memory_pool_capacity(const struct dc_memory_pool *pool)
{
	ASSERT(pool);
	return pool->capacity;
}
