/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_BUDDY_H__
#define __I915_BUDDY_H__

#include <linux/bitops.h>
#include <linux/list.h>

struct i915_buddy_block {
#define I915_BUDDY_HEADER_OFFSET GENMASK_ULL(63, 12)
#define I915_BUDDY_HEADER_STATE  GENMASK_ULL(11, 10)
#define   I915_BUDDY_ALLOCATED	   (1 << 10)
#define   I915_BUDDY_FREE	   (2 << 10)
#define   I915_BUDDY_SPLIT	   (3 << 10)
#define I915_BUDDY_HEADER_ORDER  GENMASK_ULL(9, 0)
	u64 header;

	struct i915_buddy_block *left;
	struct i915_buddy_block *right;
	struct i915_buddy_block *parent;

	void *private; /* owned by creator */

	/*
	 * While the block is allocated by the user through i915_buddy_alloc*,
	 * the user has ownership of the link, for example to maintain within
	 * a list, if so desired. As soon as the block is freed with
	 * i915_buddy_free* ownership is given back to the mm.
	 */
	struct list_head link;
	struct list_head tmp_link;
};

#define I915_BUDDY_MAX_ORDER  I915_BUDDY_HEADER_ORDER

/*
 * Binary Buddy System.
 *
 * Locking should be handled by the user, a simple mutex around
 * i915_buddy_alloc* and i915_buddy_free* should suffice.
 */
struct i915_buddy_mm {
	/* Maintain a free list for each order. */
	struct list_head *free_list;

	/*
	 * Maintain explicit binary tree(s) to track the allocation of the
	 * address space. This gives us a simple way of finding a buddy block
	 * and performing the potentially recursive merge step when freeing a
	 * block.  Nodes are either allocated or free, in which case they will
	 * also exist on the respective free list.
	 */
	struct i915_buddy_block **roots;

	/*
	 * Anything from here is public, and remains static for the lifetime of
	 * the mm. Everything above is considered do-not-touch.
	 */
	unsigned int n_roots;
	unsigned int max_order;

	/* Must be at least PAGE_SIZE */
	u64 chunk_size;
	u64 size;
};

static inline u64
i915_buddy_block_offset(struct i915_buddy_block *block)
{
	return block->header & I915_BUDDY_HEADER_OFFSET;
}

static inline unsigned int
i915_buddy_block_order(struct i915_buddy_block *block)
{
	return block->header & I915_BUDDY_HEADER_ORDER;
}

static inline unsigned int
i915_buddy_block_state(struct i915_buddy_block *block)
{
	return block->header & I915_BUDDY_HEADER_STATE;
}

static inline bool
i915_buddy_block_is_allocated(struct i915_buddy_block *block)
{
	return i915_buddy_block_state(block) == I915_BUDDY_ALLOCATED;
}

static inline bool
i915_buddy_block_is_free(struct i915_buddy_block *block)
{
	return i915_buddy_block_state(block) == I915_BUDDY_FREE;
}

static inline bool
i915_buddy_block_is_split(struct i915_buddy_block *block)
{
	return i915_buddy_block_state(block) == I915_BUDDY_SPLIT;
}

static inline u64
i915_buddy_block_size(struct i915_buddy_mm *mm,
		      struct i915_buddy_block *block)
{
	return mm->chunk_size << i915_buddy_block_order(block);
}

int i915_buddy_init(struct i915_buddy_mm *mm, u64 size, u64 chunk_size);

void i915_buddy_fini(struct i915_buddy_mm *mm);

struct i915_buddy_block *
i915_buddy_alloc(struct i915_buddy_mm *mm, unsigned int order);

int i915_buddy_alloc_range(struct i915_buddy_mm *mm,
			   struct list_head *blocks,
			   u64 start, u64 size);

void i915_buddy_free(struct i915_buddy_mm *mm, struct i915_buddy_block *block);

void i915_buddy_free_list(struct i915_buddy_mm *mm, struct list_head *objects);

#endif
