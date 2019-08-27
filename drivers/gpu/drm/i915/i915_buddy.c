// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/kmemleak.h>
#include <linux/slab.h>

#include "i915_buddy.h"

#include "i915_gem.h"
#include "i915_globals.h"
#include "i915_utils.h"

static struct i915_global_block {
	struct i915_global base;
	struct kmem_cache *slab_blocks;
} global;

static void i915_global_buddy_shrink(void)
{
	kmem_cache_shrink(global.slab_blocks);
}

static void i915_global_buddy_exit(void)
{
	kmem_cache_destroy(global.slab_blocks);
}

static struct i915_global_block global = { {
	.shrink = i915_global_buddy_shrink,
	.exit = i915_global_buddy_exit,
} };

int __init i915_global_buddy_init(void)
{
	global.slab_blocks = KMEM_CACHE(i915_buddy_block, SLAB_HWCACHE_ALIGN);
	if (!global.slab_blocks)
		return -ENOMEM;

	return 0;
}

static struct i915_buddy_block *i915_block_alloc(struct i915_buddy_block *parent,
						 unsigned int order,
						 u64 offset)
{
	struct i915_buddy_block *block;

	block = kmem_cache_zalloc(global.slab_blocks, GFP_KERNEL);
	if (!block)
		return NULL;

	block->header = offset;
	block->header |= order;
	block->parent = parent;

	return block;
}

static void i915_block_free(struct i915_buddy_block *block)
{
	kmem_cache_free(global.slab_blocks, block);
}

static void mark_allocated(struct i915_buddy_block *block)
{
	block->header &= ~I915_BUDDY_HEADER_STATE;
	block->header |= I915_BUDDY_ALLOCATED;

	list_del(&block->link);
}

static void mark_free(struct i915_buddy_mm *mm,
		      struct i915_buddy_block *block)
{
	block->header &= ~I915_BUDDY_HEADER_STATE;
	block->header |= I915_BUDDY_FREE;

	list_add(&block->link,
		 &mm->free_list[i915_buddy_block_order(block)]);
}

static void mark_split(struct i915_buddy_block *block)
{
	block->header &= ~I915_BUDDY_HEADER_STATE;
	block->header |= I915_BUDDY_SPLIT;

	list_del(&block->link);
}

int i915_buddy_init(struct i915_buddy_mm *mm, u64 size, u64 chunk_size)
{
	unsigned int i;
	u64 offset;

	if (size < chunk_size)
		return -EINVAL;

	if (chunk_size < PAGE_SIZE)
		return -EINVAL;

	if (!is_power_of_2(chunk_size))
		return -EINVAL;

	size = round_down(size, chunk_size);

	mm->size = size;
	mm->chunk_size = chunk_size;
	mm->max_order = ilog2(size) - ilog2(chunk_size);

	GEM_BUG_ON(mm->max_order > I915_BUDDY_MAX_ORDER);

	mm->free_list = kmalloc_array(mm->max_order + 1,
				      sizeof(struct list_head),
				      GFP_KERNEL);
	if (!mm->free_list)
		return -ENOMEM;

	for (i = 0; i <= mm->max_order; ++i)
		INIT_LIST_HEAD(&mm->free_list[i]);

	mm->n_roots = hweight64(size);

	mm->roots = kmalloc_array(mm->n_roots,
				  sizeof(struct i915_buddy_block *),
				  GFP_KERNEL);
	if (!mm->roots)
		goto out_free_list;

	offset = 0;
	i = 0;

	/*
	 * Split into power-of-two blocks, in case we are given a size that is
	 * not itself a power-of-two.
	 */
	do {
		struct i915_buddy_block *root;
		unsigned int order;
		u64 root_size;

		root_size = rounddown_pow_of_two(size);
		order = ilog2(root_size) - ilog2(chunk_size);

		root = i915_block_alloc(NULL, order, offset);
		if (!root)
			goto out_free_roots;

		mark_free(mm, root);

		GEM_BUG_ON(i > mm->max_order);
		GEM_BUG_ON(i915_buddy_block_size(mm, root) < chunk_size);

		mm->roots[i] = root;

		offset += root_size;
		size -= root_size;
		i++;
	} while (size);

	return 0;

out_free_roots:
	while (i--)
		i915_block_free(mm->roots[i]);
	kfree(mm->roots);
out_free_list:
	kfree(mm->free_list);
	return -ENOMEM;
}

void i915_buddy_fini(struct i915_buddy_mm *mm)
{
	int i;

	for (i = 0; i < mm->n_roots; ++i) {
		GEM_WARN_ON(!i915_buddy_block_is_free(mm->roots[i]));
		i915_block_free(mm->roots[i]);
	}

	kfree(mm->roots);
	kfree(mm->free_list);
}

static int split_block(struct i915_buddy_mm *mm,
		       struct i915_buddy_block *block)
{
	unsigned int block_order = i915_buddy_block_order(block) - 1;
	u64 offset = i915_buddy_block_offset(block);

	GEM_BUG_ON(!i915_buddy_block_is_free(block));
	GEM_BUG_ON(!i915_buddy_block_order(block));

	block->left = i915_block_alloc(block, block_order, offset);
	if (!block->left)
		return -ENOMEM;

	block->right = i915_block_alloc(block, block_order,
					offset + (mm->chunk_size << block_order));
	if (!block->right) {
		i915_block_free(block->left);
		return -ENOMEM;
	}

	mark_free(mm, block->left);
	mark_free(mm, block->right);

	mark_split(block);

	return 0;
}

static struct i915_buddy_block *
get_buddy(struct i915_buddy_block *block)
{
	struct i915_buddy_block *parent;

	parent = block->parent;
	if (!parent)
		return NULL;

	if (parent->left == block)
		return parent->right;

	return parent->left;
}

static void __i915_buddy_free(struct i915_buddy_mm *mm,
			      struct i915_buddy_block *block)
{
	struct i915_buddy_block *parent;

	while ((parent = block->parent)) {
		struct i915_buddy_block *buddy;

		buddy = get_buddy(block);

		if (!i915_buddy_block_is_free(buddy))
			break;

		list_del(&buddy->link);

		i915_block_free(block);
		i915_block_free(buddy);

		block = parent;
	}

	mark_free(mm, block);
}

void i915_buddy_free(struct i915_buddy_mm *mm,
		     struct i915_buddy_block *block)
{
	GEM_BUG_ON(!i915_buddy_block_is_allocated(block));
	__i915_buddy_free(mm, block);
}

void i915_buddy_free_list(struct i915_buddy_mm *mm, struct list_head *objects)
{
	struct i915_buddy_block *block, *on;

	list_for_each_entry_safe(block, on, objects, link)
		i915_buddy_free(mm, block);
	INIT_LIST_HEAD(objects);
}

/*
 * Allocate power-of-two block. The order value here translates to:
 *
 *   0 = 2^0 * mm->chunk_size
 *   1 = 2^1 * mm->chunk_size
 *   2 = 2^2 * mm->chunk_size
 *   ...
 */
struct i915_buddy_block *
i915_buddy_alloc(struct i915_buddy_mm *mm, unsigned int order)
{
	struct i915_buddy_block *block = NULL;
	unsigned int i;
	int err;

	for (i = order; i <= mm->max_order; ++i) {
		block = list_first_entry_or_null(&mm->free_list[i],
						 struct i915_buddy_block,
						 link);
		if (block)
			break;
	}

	if (!block)
		return ERR_PTR(-ENOSPC);

	GEM_BUG_ON(!i915_buddy_block_is_free(block));

	while (i != order) {
		err = split_block(mm, block);
		if (unlikely(err))
			goto out_free;

		/* Go low */
		block = block->left;
		i--;
	}

	mark_allocated(block);
	kmemleak_update_trace(block);
	return block;

out_free:
	__i915_buddy_free(mm, block);
	return ERR_PTR(err);
}

static inline bool overlaps(u64 s1, u64 e1, u64 s2, u64 e2)
{
	return s1 <= e2 && e1 >= s2;
}

static inline bool contains(u64 s1, u64 e1, u64 s2, u64 e2)
{
	return s1 <= s2 && e1 >= e2;
}

/*
 * Allocate range. Note that it's safe to chain together multiple alloc_ranges
 * with the same blocks list.
 *
 * Intended for pre-allocating portions of the address space, for example to
 * reserve a block for the initial framebuffer or similar, hence the expectation
 * here is that i915_buddy_alloc() is still the main vehicle for
 * allocations, so if that's not the case then the drm_mm range allocator is
 * probably a much better fit, and so you should probably go use that instead.
 */
int i915_buddy_alloc_range(struct i915_buddy_mm *mm,
			   struct list_head *blocks,
			   u64 start, u64 size)
{
	struct i915_buddy_block *block;
	struct i915_buddy_block *buddy;
	LIST_HEAD(allocated);
	LIST_HEAD(dfs);
	u64 end;
	int err;
	int i;

	if (size < mm->chunk_size)
		return -EINVAL;

	if (!IS_ALIGNED(size | start, mm->chunk_size))
		return -EINVAL;

	if (range_overflows(start, size, mm->size))
		return -EINVAL;

	for (i = 0; i < mm->n_roots; ++i)
		list_add_tail(&mm->roots[i]->tmp_link, &dfs);

	end = start + size - 1;

	do {
		u64 block_start;
		u64 block_end;

		block = list_first_entry_or_null(&dfs,
						 struct i915_buddy_block,
						 tmp_link);
		if (!block)
			break;

		list_del(&block->tmp_link);

		block_start = i915_buddy_block_offset(block);
		block_end = block_start + i915_buddy_block_size(mm, block) - 1;

		if (!overlaps(start, end, block_start, block_end))
			continue;

		if (i915_buddy_block_is_allocated(block)) {
			err = -ENOSPC;
			goto err_free;
		}

		if (contains(start, end, block_start, block_end)) {
			if (!i915_buddy_block_is_free(block)) {
				err = -ENOSPC;
				goto err_free;
			}

			mark_allocated(block);
			list_add_tail(&block->link, &allocated);
			continue;
		}

		if (!i915_buddy_block_is_split(block)) {
			err = split_block(mm, block);
			if (unlikely(err))
				goto err_undo;
		}

		list_add(&block->right->tmp_link, &dfs);
		list_add(&block->left->tmp_link, &dfs);
	} while (1);

	list_splice_tail(&allocated, blocks);
	return 0;

err_undo:
	/*
	 * We really don't want to leave around a bunch of split blocks, since
	 * bigger is better, so make sure we merge everything back before we
	 * free the allocated blocks.
	 */
	buddy = get_buddy(block);
	if (buddy &&
	    (i915_buddy_block_is_free(block) &&
	     i915_buddy_block_is_free(buddy)))
		__i915_buddy_free(mm, block);

err_free:
	i915_buddy_free_list(mm, &allocated);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_buddy.c"
#endif
