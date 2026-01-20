// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <kunit/test-bug.h>

#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/module.h>
#include <linux/sizes.h>

#include <drm/drm_buddy.h>
#include <drm/drm_print.h>

enum drm_buddy_free_tree {
	DRM_BUDDY_CLEAR_TREE = 0,
	DRM_BUDDY_DIRTY_TREE,
	DRM_BUDDY_MAX_FREE_TREES,
};

static struct kmem_cache *slab_blocks;

#define for_each_free_tree(tree) \
	for ((tree) = 0; (tree) < DRM_BUDDY_MAX_FREE_TREES; (tree)++)

static struct drm_buddy_block *drm_block_alloc(struct drm_buddy *mm,
					       struct drm_buddy_block *parent,
					       unsigned int order,
					       u64 offset)
{
	struct drm_buddy_block *block;

	BUG_ON(order > DRM_BUDDY_MAX_ORDER);

	block = kmem_cache_zalloc(slab_blocks, GFP_KERNEL);
	if (!block)
		return NULL;

	block->header = offset;
	block->header |= order;
	block->parent = parent;

	RB_CLEAR_NODE(&block->rb);

	BUG_ON(block->header & DRM_BUDDY_HEADER_UNUSED);
	return block;
}

static void drm_block_free(struct drm_buddy *mm,
			   struct drm_buddy_block *block)
{
	kmem_cache_free(slab_blocks, block);
}

static enum drm_buddy_free_tree
get_block_tree(struct drm_buddy_block *block)
{
	return drm_buddy_block_is_clear(block) ?
	       DRM_BUDDY_CLEAR_TREE : DRM_BUDDY_DIRTY_TREE;
}

static struct drm_buddy_block *
rbtree_get_free_block(const struct rb_node *node)
{
	return node ? rb_entry(node, struct drm_buddy_block, rb) : NULL;
}

static struct drm_buddy_block *
rbtree_last_free_block(struct rb_root *root)
{
	return rbtree_get_free_block(rb_last(root));
}

static bool rbtree_is_empty(struct rb_root *root)
{
	return RB_EMPTY_ROOT(root);
}

static bool drm_buddy_block_offset_less(const struct drm_buddy_block *block,
					const struct drm_buddy_block *node)
{
	return drm_buddy_block_offset(block) < drm_buddy_block_offset(node);
}

static bool rbtree_block_offset_less(struct rb_node *block,
				     const struct rb_node *node)
{
	return drm_buddy_block_offset_less(rbtree_get_free_block(block),
					   rbtree_get_free_block(node));
}

static void rbtree_insert(struct drm_buddy *mm,
			  struct drm_buddy_block *block,
			  enum drm_buddy_free_tree tree)
{
	rb_add(&block->rb,
	       &mm->free_trees[tree][drm_buddy_block_order(block)],
	       rbtree_block_offset_less);
}

static void rbtree_remove(struct drm_buddy *mm,
			  struct drm_buddy_block *block)
{
	unsigned int order = drm_buddy_block_order(block);
	enum drm_buddy_free_tree tree;
	struct rb_root *root;

	tree = get_block_tree(block);
	root = &mm->free_trees[tree][order];

	rb_erase(&block->rb, root);
	RB_CLEAR_NODE(&block->rb);
}

static void clear_reset(struct drm_buddy_block *block)
{
	block->header &= ~DRM_BUDDY_HEADER_CLEAR;
}

static void mark_cleared(struct drm_buddy_block *block)
{
	block->header |= DRM_BUDDY_HEADER_CLEAR;
}

static void mark_allocated(struct drm_buddy *mm,
			   struct drm_buddy_block *block)
{
	block->header &= ~DRM_BUDDY_HEADER_STATE;
	block->header |= DRM_BUDDY_ALLOCATED;

	rbtree_remove(mm, block);
}

static void mark_free(struct drm_buddy *mm,
		      struct drm_buddy_block *block)
{
	enum drm_buddy_free_tree tree;

	block->header &= ~DRM_BUDDY_HEADER_STATE;
	block->header |= DRM_BUDDY_FREE;

	tree = get_block_tree(block);
	rbtree_insert(mm, block, tree);
}

static void mark_split(struct drm_buddy *mm,
		       struct drm_buddy_block *block)
{
	block->header &= ~DRM_BUDDY_HEADER_STATE;
	block->header |= DRM_BUDDY_SPLIT;

	rbtree_remove(mm, block);
}

static inline bool overlaps(u64 s1, u64 e1, u64 s2, u64 e2)
{
	return s1 <= e2 && e1 >= s2;
}

static inline bool contains(u64 s1, u64 e1, u64 s2, u64 e2)
{
	return s1 <= s2 && e1 >= e2;
}

static struct drm_buddy_block *
__get_buddy(struct drm_buddy_block *block)
{
	struct drm_buddy_block *parent;

	parent = block->parent;
	if (!parent)
		return NULL;

	if (parent->left == block)
		return parent->right;

	return parent->left;
}

static unsigned int __drm_buddy_free(struct drm_buddy *mm,
				     struct drm_buddy_block *block,
				     bool force_merge)
{
	struct drm_buddy_block *parent;
	unsigned int order;

	while ((parent = block->parent)) {
		struct drm_buddy_block *buddy;

		buddy = __get_buddy(block);

		if (!drm_buddy_block_is_free(buddy))
			break;

		if (!force_merge) {
			/*
			 * Check the block and its buddy clear state and exit
			 * the loop if they both have the dissimilar state.
			 */
			if (drm_buddy_block_is_clear(block) !=
			    drm_buddy_block_is_clear(buddy))
				break;

			if (drm_buddy_block_is_clear(block))
				mark_cleared(parent);
		}

		rbtree_remove(mm, buddy);
		if (force_merge && drm_buddy_block_is_clear(buddy))
			mm->clear_avail -= drm_buddy_block_size(mm, buddy);

		drm_block_free(mm, block);
		drm_block_free(mm, buddy);

		block = parent;
	}

	order = drm_buddy_block_order(block);
	mark_free(mm, block);

	return order;
}

static int __force_merge(struct drm_buddy *mm,
			 u64 start,
			 u64 end,
			 unsigned int min_order)
{
	unsigned int tree, order;
	int i;

	if (!min_order)
		return -ENOMEM;

	if (min_order > mm->max_order)
		return -EINVAL;

	for_each_free_tree(tree) {
		for (i = min_order - 1; i >= 0; i--) {
			struct rb_node *iter = rb_last(&mm->free_trees[tree][i]);

			while (iter) {
				struct drm_buddy_block *block, *buddy;
				u64 block_start, block_end;

				block = rbtree_get_free_block(iter);
				iter = rb_prev(iter);

				if (!block || !block->parent)
					continue;

				block_start = drm_buddy_block_offset(block);
				block_end = block_start + drm_buddy_block_size(mm, block) - 1;

				if (!contains(start, end, block_start, block_end))
					continue;

				buddy = __get_buddy(block);
				if (!drm_buddy_block_is_free(buddy))
					continue;

				WARN_ON(drm_buddy_block_is_clear(block) ==
					drm_buddy_block_is_clear(buddy));

				/*
				 * Advance to the next node when the current node is the buddy,
				 * as freeing the block will also remove its buddy from the tree.
				 */
				if (iter == &buddy->rb)
					iter = rb_prev(iter);

				rbtree_remove(mm, block);
				if (drm_buddy_block_is_clear(block))
					mm->clear_avail -= drm_buddy_block_size(mm, block);

				order = __drm_buddy_free(mm, block, true);
				if (order >= min_order)
					return 0;
			}
		}
	}

	return -ENOMEM;
}

/**
 * drm_buddy_init - init memory manager
 *
 * @mm: DRM buddy manager to initialize
 * @size: size in bytes to manage
 * @chunk_size: minimum page size in bytes for our allocations
 *
 * Initializes the memory manager and its resources.
 *
 * Returns:
 * 0 on success, error code on failure.
 */
int drm_buddy_init(struct drm_buddy *mm, u64 size, u64 chunk_size)
{
	unsigned int i, j, root_count = 0;
	u64 offset = 0;

	if (size < chunk_size)
		return -EINVAL;

	if (chunk_size < SZ_4K)
		return -EINVAL;

	if (!is_power_of_2(chunk_size))
		return -EINVAL;

	size = round_down(size, chunk_size);

	mm->size = size;
	mm->avail = size;
	mm->clear_avail = 0;
	mm->chunk_size = chunk_size;
	mm->max_order = ilog2(size) - ilog2(chunk_size);

	BUG_ON(mm->max_order > DRM_BUDDY_MAX_ORDER);

	mm->free_trees = kmalloc_array(DRM_BUDDY_MAX_FREE_TREES,
				       sizeof(*mm->free_trees),
				       GFP_KERNEL);
	if (!mm->free_trees)
		return -ENOMEM;

	for_each_free_tree(i) {
		mm->free_trees[i] = kmalloc_array(mm->max_order + 1,
						  sizeof(struct rb_root),
						  GFP_KERNEL);
		if (!mm->free_trees[i])
			goto out_free_tree;

		for (j = 0; j <= mm->max_order; ++j)
			mm->free_trees[i][j] = RB_ROOT;
	}

	mm->n_roots = hweight64(size);

	mm->roots = kmalloc_array(mm->n_roots,
				  sizeof(struct drm_buddy_block *),
				  GFP_KERNEL);
	if (!mm->roots)
		goto out_free_tree;

	/*
	 * Split into power-of-two blocks, in case we are given a size that is
	 * not itself a power-of-two.
	 */
	do {
		struct drm_buddy_block *root;
		unsigned int order;
		u64 root_size;

		order = ilog2(size) - ilog2(chunk_size);
		root_size = chunk_size << order;

		root = drm_block_alloc(mm, NULL, order, offset);
		if (!root)
			goto out_free_roots;

		mark_free(mm, root);

		BUG_ON(root_count > mm->max_order);
		BUG_ON(drm_buddy_block_size(mm, root) < chunk_size);

		mm->roots[root_count] = root;

		offset += root_size;
		size -= root_size;
		root_count++;
	} while (size);

	return 0;

out_free_roots:
	while (root_count--)
		drm_block_free(mm, mm->roots[root_count]);
	kfree(mm->roots);
out_free_tree:
	while (i--)
		kfree(mm->free_trees[i]);
	kfree(mm->free_trees);
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_buddy_init);

/**
 * drm_buddy_fini - tear down the memory manager
 *
 * @mm: DRM buddy manager to free
 *
 * Cleanup memory manager resources and the freetree
 */
void drm_buddy_fini(struct drm_buddy *mm)
{
	u64 root_size, size, start;
	unsigned int order;
	int i;

	size = mm->size;

	for (i = 0; i < mm->n_roots; ++i) {
		order = ilog2(size) - ilog2(mm->chunk_size);
		start = drm_buddy_block_offset(mm->roots[i]);
		__force_merge(mm, start, start + size, order);

		if (WARN_ON(!drm_buddy_block_is_free(mm->roots[i])))
			kunit_fail_current_test("buddy_fini() root");

		drm_block_free(mm, mm->roots[i]);

		root_size = mm->chunk_size << order;
		size -= root_size;
	}

	WARN_ON(mm->avail != mm->size);

	for_each_free_tree(i)
		kfree(mm->free_trees[i]);
	kfree(mm->roots);
}
EXPORT_SYMBOL(drm_buddy_fini);

static int split_block(struct drm_buddy *mm,
		       struct drm_buddy_block *block)
{
	unsigned int block_order = drm_buddy_block_order(block) - 1;
	u64 offset = drm_buddy_block_offset(block);

	BUG_ON(!drm_buddy_block_is_free(block));
	BUG_ON(!drm_buddy_block_order(block));

	block->left = drm_block_alloc(mm, block, block_order, offset);
	if (!block->left)
		return -ENOMEM;

	block->right = drm_block_alloc(mm, block, block_order,
				       offset + (mm->chunk_size << block_order));
	if (!block->right) {
		drm_block_free(mm, block->left);
		return -ENOMEM;
	}

	mark_split(mm, block);

	if (drm_buddy_block_is_clear(block)) {
		mark_cleared(block->left);
		mark_cleared(block->right);
		clear_reset(block);
	}

	mark_free(mm, block->left);
	mark_free(mm, block->right);

	return 0;
}

/**
 * drm_get_buddy - get buddy address
 *
 * @block: DRM buddy block
 *
 * Returns the corresponding buddy block for @block, or NULL
 * if this is a root block and can't be merged further.
 * Requires some kind of locking to protect against
 * any concurrent allocate and free operations.
 */
struct drm_buddy_block *
drm_get_buddy(struct drm_buddy_block *block)
{
	return __get_buddy(block);
}
EXPORT_SYMBOL(drm_get_buddy);

/**
 * drm_buddy_reset_clear - reset blocks clear state
 *
 * @mm: DRM buddy manager
 * @is_clear: blocks clear state
 *
 * Reset the clear state based on @is_clear value for each block
 * in the freetree.
 */
void drm_buddy_reset_clear(struct drm_buddy *mm, bool is_clear)
{
	enum drm_buddy_free_tree src_tree, dst_tree;
	u64 root_size, size, start;
	unsigned int order;
	int i;

	size = mm->size;
	for (i = 0; i < mm->n_roots; ++i) {
		order = ilog2(size) - ilog2(mm->chunk_size);
		start = drm_buddy_block_offset(mm->roots[i]);
		__force_merge(mm, start, start + size, order);

		root_size = mm->chunk_size << order;
		size -= root_size;
	}

	src_tree = is_clear ? DRM_BUDDY_DIRTY_TREE : DRM_BUDDY_CLEAR_TREE;
	dst_tree = is_clear ? DRM_BUDDY_CLEAR_TREE : DRM_BUDDY_DIRTY_TREE;

	for (i = 0; i <= mm->max_order; ++i) {
		struct rb_root *root = &mm->free_trees[src_tree][i];
		struct drm_buddy_block *block, *tmp;

		rbtree_postorder_for_each_entry_safe(block, tmp, root, rb) {
			rbtree_remove(mm, block);
			if (is_clear) {
				mark_cleared(block);
				mm->clear_avail += drm_buddy_block_size(mm, block);
			} else {
				clear_reset(block);
				mm->clear_avail -= drm_buddy_block_size(mm, block);
			}

			rbtree_insert(mm, block, dst_tree);
		}
	}
}
EXPORT_SYMBOL(drm_buddy_reset_clear);

/**
 * drm_buddy_free_block - free a block
 *
 * @mm: DRM buddy manager
 * @block: block to be freed
 */
void drm_buddy_free_block(struct drm_buddy *mm,
			  struct drm_buddy_block *block)
{
	BUG_ON(!drm_buddy_block_is_allocated(block));
	mm->avail += drm_buddy_block_size(mm, block);
	if (drm_buddy_block_is_clear(block))
		mm->clear_avail += drm_buddy_block_size(mm, block);

	__drm_buddy_free(mm, block, false);
}
EXPORT_SYMBOL(drm_buddy_free_block);

static void __drm_buddy_free_list(struct drm_buddy *mm,
				  struct list_head *objects,
				  bool mark_clear,
				  bool mark_dirty)
{
	struct drm_buddy_block *block, *on;

	WARN_ON(mark_dirty && mark_clear);

	list_for_each_entry_safe(block, on, objects, link) {
		if (mark_clear)
			mark_cleared(block);
		else if (mark_dirty)
			clear_reset(block);
		drm_buddy_free_block(mm, block);
		cond_resched();
	}
	INIT_LIST_HEAD(objects);
}

static void drm_buddy_free_list_internal(struct drm_buddy *mm,
					 struct list_head *objects)
{
	/*
	 * Don't touch the clear/dirty bit, since allocation is still internal
	 * at this point. For example we might have just failed part of the
	 * allocation.
	 */
	__drm_buddy_free_list(mm, objects, false, false);
}

/**
 * drm_buddy_free_list - free blocks
 *
 * @mm: DRM buddy manager
 * @objects: input list head to free blocks
 * @flags: optional flags like DRM_BUDDY_CLEARED
 */
void drm_buddy_free_list(struct drm_buddy *mm,
			 struct list_head *objects,
			 unsigned int flags)
{
	bool mark_clear = flags & DRM_BUDDY_CLEARED;

	__drm_buddy_free_list(mm, objects, mark_clear, !mark_clear);
}
EXPORT_SYMBOL(drm_buddy_free_list);

static bool block_incompatible(struct drm_buddy_block *block, unsigned int flags)
{
	bool needs_clear = flags & DRM_BUDDY_CLEAR_ALLOCATION;

	return needs_clear != drm_buddy_block_is_clear(block);
}

static struct drm_buddy_block *
__alloc_range_bias(struct drm_buddy *mm,
		   u64 start, u64 end,
		   unsigned int order,
		   unsigned long flags,
		   bool fallback)
{
	u64 req_size = mm->chunk_size << order;
	struct drm_buddy_block *block;
	struct drm_buddy_block *buddy;
	LIST_HEAD(dfs);
	int err;
	int i;

	end = end - 1;

	for (i = 0; i < mm->n_roots; ++i)
		list_add_tail(&mm->roots[i]->tmp_link, &dfs);

	do {
		u64 block_start;
		u64 block_end;

		block = list_first_entry_or_null(&dfs,
						 struct drm_buddy_block,
						 tmp_link);
		if (!block)
			break;

		list_del(&block->tmp_link);

		if (drm_buddy_block_order(block) < order)
			continue;

		block_start = drm_buddy_block_offset(block);
		block_end = block_start + drm_buddy_block_size(mm, block) - 1;

		if (!overlaps(start, end, block_start, block_end))
			continue;

		if (drm_buddy_block_is_allocated(block))
			continue;

		if (block_start < start || block_end > end) {
			u64 adjusted_start = max(block_start, start);
			u64 adjusted_end = min(block_end, end);

			if (round_down(adjusted_end + 1, req_size) <=
			    round_up(adjusted_start, req_size))
				continue;
		}

		if (!fallback && block_incompatible(block, flags))
			continue;

		if (contains(start, end, block_start, block_end) &&
		    order == drm_buddy_block_order(block)) {
			/*
			 * Find the free block within the range.
			 */
			if (drm_buddy_block_is_free(block))
				return block;

			continue;
		}

		if (!drm_buddy_block_is_split(block)) {
			err = split_block(mm, block);
			if (unlikely(err))
				goto err_undo;
		}

		list_add(&block->right->tmp_link, &dfs);
		list_add(&block->left->tmp_link, &dfs);
	} while (1);

	return ERR_PTR(-ENOSPC);

err_undo:
	/*
	 * We really don't want to leave around a bunch of split blocks, since
	 * bigger is better, so make sure we merge everything back before we
	 * free the allocated blocks.
	 */
	buddy = __get_buddy(block);
	if (buddy &&
	    (drm_buddy_block_is_free(block) &&
	     drm_buddy_block_is_free(buddy)))
		__drm_buddy_free(mm, block, false);
	return ERR_PTR(err);
}

static struct drm_buddy_block *
__drm_buddy_alloc_range_bias(struct drm_buddy *mm,
			     u64 start, u64 end,
			     unsigned int order,
			     unsigned long flags)
{
	struct drm_buddy_block *block;
	bool fallback = false;

	block = __alloc_range_bias(mm, start, end, order,
				   flags, fallback);
	if (IS_ERR(block))
		return __alloc_range_bias(mm, start, end, order,
					  flags, !fallback);

	return block;
}

static struct drm_buddy_block *
get_maxblock(struct drm_buddy *mm,
	     unsigned int order,
	     enum drm_buddy_free_tree tree)
{
	struct drm_buddy_block *max_block = NULL, *block = NULL;
	struct rb_root *root;
	unsigned int i;

	for (i = order; i <= mm->max_order; ++i) {
		root = &mm->free_trees[tree][i];
		block = rbtree_last_free_block(root);
		if (!block)
			continue;

		if (!max_block) {
			max_block = block;
			continue;
		}

		if (drm_buddy_block_offset(block) >
		    drm_buddy_block_offset(max_block)) {
			max_block = block;
		}
	}

	return max_block;
}

static struct drm_buddy_block *
alloc_from_freetree(struct drm_buddy *mm,
		    unsigned int order,
		    unsigned long flags)
{
	struct drm_buddy_block *block = NULL;
	struct rb_root *root;
	enum drm_buddy_free_tree tree;
	unsigned int tmp;
	int err;

	tree = (flags & DRM_BUDDY_CLEAR_ALLOCATION) ?
		DRM_BUDDY_CLEAR_TREE : DRM_BUDDY_DIRTY_TREE;

	if (flags & DRM_BUDDY_TOPDOWN_ALLOCATION) {
		block = get_maxblock(mm, order, tree);
		if (block)
			/* Store the obtained block order */
			tmp = drm_buddy_block_order(block);
	} else {
		for (tmp = order; tmp <= mm->max_order; ++tmp) {
			/* Get RB tree root for this order and tree */
			root = &mm->free_trees[tree][tmp];
			block = rbtree_last_free_block(root);
			if (block)
				break;
		}
	}

	if (!block) {
		/* Try allocating from the other tree */
		tree = (tree == DRM_BUDDY_CLEAR_TREE) ?
			DRM_BUDDY_DIRTY_TREE : DRM_BUDDY_CLEAR_TREE;

		for (tmp = order; tmp <= mm->max_order; ++tmp) {
			root = &mm->free_trees[tree][tmp];
			block = rbtree_last_free_block(root);
			if (block)
				break;
		}

		if (!block)
			return ERR_PTR(-ENOSPC);
	}

	BUG_ON(!drm_buddy_block_is_free(block));

	while (tmp != order) {
		err = split_block(mm, block);
		if (unlikely(err))
			goto err_undo;

		block = block->right;
		tmp--;
	}
	return block;

err_undo:
	if (tmp != order)
		__drm_buddy_free(mm, block, false);
	return ERR_PTR(err);
}

static int __alloc_range(struct drm_buddy *mm,
			 struct list_head *dfs,
			 u64 start, u64 size,
			 struct list_head *blocks,
			 u64 *total_allocated_on_err)
{
	struct drm_buddy_block *block;
	struct drm_buddy_block *buddy;
	u64 total_allocated = 0;
	LIST_HEAD(allocated);
	u64 end;
	int err;

	end = start + size - 1;

	do {
		u64 block_start;
		u64 block_end;

		block = list_first_entry_or_null(dfs,
						 struct drm_buddy_block,
						 tmp_link);
		if (!block)
			break;

		list_del(&block->tmp_link);

		block_start = drm_buddy_block_offset(block);
		block_end = block_start + drm_buddy_block_size(mm, block) - 1;

		if (!overlaps(start, end, block_start, block_end))
			continue;

		if (drm_buddy_block_is_allocated(block)) {
			err = -ENOSPC;
			goto err_free;
		}

		if (contains(start, end, block_start, block_end)) {
			if (drm_buddy_block_is_free(block)) {
				mark_allocated(mm, block);
				total_allocated += drm_buddy_block_size(mm, block);
				mm->avail -= drm_buddy_block_size(mm, block);
				if (drm_buddy_block_is_clear(block))
					mm->clear_avail -= drm_buddy_block_size(mm, block);
				list_add_tail(&block->link, &allocated);
				continue;
			} else if (!mm->clear_avail) {
				err = -ENOSPC;
				goto err_free;
			}
		}

		if (!drm_buddy_block_is_split(block)) {
			err = split_block(mm, block);
			if (unlikely(err))
				goto err_undo;
		}

		list_add(&block->right->tmp_link, dfs);
		list_add(&block->left->tmp_link, dfs);
	} while (1);

	if (total_allocated < size) {
		err = -ENOSPC;
		goto err_free;
	}

	list_splice_tail(&allocated, blocks);

	return 0;

err_undo:
	/*
	 * We really don't want to leave around a bunch of split blocks, since
	 * bigger is better, so make sure we merge everything back before we
	 * free the allocated blocks.
	 */
	buddy = __get_buddy(block);
	if (buddy &&
	    (drm_buddy_block_is_free(block) &&
	     drm_buddy_block_is_free(buddy)))
		__drm_buddy_free(mm, block, false);

err_free:
	if (err == -ENOSPC && total_allocated_on_err) {
		list_splice_tail(&allocated, blocks);
		*total_allocated_on_err = total_allocated;
	} else {
		drm_buddy_free_list_internal(mm, &allocated);
	}

	return err;
}

static int __drm_buddy_alloc_range(struct drm_buddy *mm,
				   u64 start,
				   u64 size,
				   u64 *total_allocated_on_err,
				   struct list_head *blocks)
{
	LIST_HEAD(dfs);
	int i;

	for (i = 0; i < mm->n_roots; ++i)
		list_add_tail(&mm->roots[i]->tmp_link, &dfs);

	return __alloc_range(mm, &dfs, start, size,
			     blocks, total_allocated_on_err);
}

static int __alloc_contig_try_harder(struct drm_buddy *mm,
				     u64 size,
				     u64 min_block_size,
				     struct list_head *blocks)
{
	u64 rhs_offset, lhs_offset, lhs_size, filled;
	struct drm_buddy_block *block;
	unsigned int tree, order;
	LIST_HEAD(blocks_lhs);
	unsigned long pages;
	u64 modify_size;
	int err;

	modify_size = rounddown_pow_of_two(size);
	pages = modify_size >> ilog2(mm->chunk_size);
	order = fls(pages) - 1;
	if (order == 0)
		return -ENOSPC;

	for_each_free_tree(tree) {
		struct rb_root *root;
		struct rb_node *iter;

		root = &mm->free_trees[tree][order];
		if (rbtree_is_empty(root))
			continue;

		iter = rb_last(root);
		while (iter) {
			block = rbtree_get_free_block(iter);

			/* Allocate blocks traversing RHS */
			rhs_offset = drm_buddy_block_offset(block);
			err =  __drm_buddy_alloc_range(mm, rhs_offset, size,
						       &filled, blocks);
			if (!err || err != -ENOSPC)
				return err;

			lhs_size = max((size - filled), min_block_size);
			if (!IS_ALIGNED(lhs_size, min_block_size))
				lhs_size = round_up(lhs_size, min_block_size);

			/* Allocate blocks traversing LHS */
			lhs_offset = drm_buddy_block_offset(block) - lhs_size;
			err =  __drm_buddy_alloc_range(mm, lhs_offset, lhs_size,
						       NULL, &blocks_lhs);
			if (!err) {
				list_splice(&blocks_lhs, blocks);
				return 0;
			} else if (err != -ENOSPC) {
				drm_buddy_free_list_internal(mm, blocks);
				return err;
			}
			/* Free blocks for the next iteration */
			drm_buddy_free_list_internal(mm, blocks);

			iter = rb_prev(iter);
		}
	}

	return -ENOSPC;
}

/**
 * drm_buddy_block_trim - free unused pages
 *
 * @mm: DRM buddy manager
 * @start: start address to begin the trimming.
 * @new_size: original size requested
 * @blocks: Input and output list of allocated blocks.
 * MUST contain single block as input to be trimmed.
 * On success will contain the newly allocated blocks
 * making up the @new_size. Blocks always appear in
 * ascending order
 *
 * For contiguous allocation, we round up the size to the nearest
 * power of two value, drivers consume *actual* size, so remaining
 * portions are unused and can be optionally freed with this function
 *
 * Returns:
 * 0 on success, error code on failure.
 */
int drm_buddy_block_trim(struct drm_buddy *mm,
			 u64 *start,
			 u64 new_size,
			 struct list_head *blocks)
{
	struct drm_buddy_block *parent;
	struct drm_buddy_block *block;
	u64 block_start, block_end;
	LIST_HEAD(dfs);
	u64 new_start;
	int err;

	if (!list_is_singular(blocks))
		return -EINVAL;

	block = list_first_entry(blocks,
				 struct drm_buddy_block,
				 link);

	block_start = drm_buddy_block_offset(block);
	block_end = block_start + drm_buddy_block_size(mm, block);

	if (WARN_ON(!drm_buddy_block_is_allocated(block)))
		return -EINVAL;

	if (new_size > drm_buddy_block_size(mm, block))
		return -EINVAL;

	if (!new_size || !IS_ALIGNED(new_size, mm->chunk_size))
		return -EINVAL;

	if (new_size == drm_buddy_block_size(mm, block))
		return 0;

	new_start = block_start;
	if (start) {
		new_start = *start;

		if (new_start < block_start)
			return -EINVAL;

		if (!IS_ALIGNED(new_start, mm->chunk_size))
			return -EINVAL;

		if (range_overflows(new_start, new_size, block_end))
			return -EINVAL;
	}

	list_del(&block->link);
	mark_free(mm, block);
	mm->avail += drm_buddy_block_size(mm, block);
	if (drm_buddy_block_is_clear(block))
		mm->clear_avail += drm_buddy_block_size(mm, block);

	/* Prevent recursively freeing this node */
	parent = block->parent;
	block->parent = NULL;

	list_add(&block->tmp_link, &dfs);
	err =  __alloc_range(mm, &dfs, new_start, new_size, blocks, NULL);
	if (err) {
		mark_allocated(mm, block);
		mm->avail -= drm_buddy_block_size(mm, block);
		if (drm_buddy_block_is_clear(block))
			mm->clear_avail -= drm_buddy_block_size(mm, block);
		list_add(&block->link, blocks);
	}

	block->parent = parent;
	return err;
}
EXPORT_SYMBOL(drm_buddy_block_trim);

static struct drm_buddy_block *
__drm_buddy_alloc_blocks(struct drm_buddy *mm,
			 u64 start, u64 end,
			 unsigned int order,
			 unsigned long flags)
{
	if (flags & DRM_BUDDY_RANGE_ALLOCATION)
		/* Allocate traversing within the range */
		return  __drm_buddy_alloc_range_bias(mm, start, end,
						     order, flags);
	else
		/* Allocate from freetree */
		return alloc_from_freetree(mm, order, flags);
}

/**
 * drm_buddy_alloc_blocks - allocate power-of-two blocks
 *
 * @mm: DRM buddy manager to allocate from
 * @start: start of the allowed range for this block
 * @end: end of the allowed range for this block
 * @size: size of the allocation in bytes
 * @min_block_size: alignment of the allocation
 * @blocks: output list head to add allocated blocks
 * @flags: DRM_BUDDY_*_ALLOCATION flags
 *
 * alloc_range_bias() called on range limitations, which traverses
 * the tree and returns the desired block.
 *
 * alloc_from_freetree() called when *no* range restrictions
 * are enforced, which picks the block from the freetree.
 *
 * Returns:
 * 0 on success, error code on failure.
 */
int drm_buddy_alloc_blocks(struct drm_buddy *mm,
			   u64 start, u64 end, u64 size,
			   u64 min_block_size,
			   struct list_head *blocks,
			   unsigned long flags)
{
	struct drm_buddy_block *block = NULL;
	u64 original_size, original_min_size;
	unsigned int min_order, order;
	LIST_HEAD(allocated);
	unsigned long pages;
	int err;

	if (size < mm->chunk_size)
		return -EINVAL;

	if (min_block_size < mm->chunk_size)
		return -EINVAL;

	if (!is_power_of_2(min_block_size))
		return -EINVAL;

	if (!IS_ALIGNED(start | end | size, mm->chunk_size))
		return -EINVAL;

	if (end > mm->size)
		return -EINVAL;

	if (range_overflows(start, size, mm->size))
		return -EINVAL;

	/* Actual range allocation */
	if (start + size == end) {
		if (!IS_ALIGNED(start | end, min_block_size))
			return -EINVAL;

		return __drm_buddy_alloc_range(mm, start, size, NULL, blocks);
	}

	original_size = size;
	original_min_size = min_block_size;

	/* Roundup the size to power of 2 */
	if (flags & DRM_BUDDY_CONTIGUOUS_ALLOCATION) {
		size = roundup_pow_of_two(size);
		min_block_size = size;
	/* Align size value to min_block_size */
	} else if (!IS_ALIGNED(size, min_block_size)) {
		size = round_up(size, min_block_size);
	}

	pages = size >> ilog2(mm->chunk_size);
	order = fls(pages) - 1;
	min_order = ilog2(min_block_size) - ilog2(mm->chunk_size);

	do {
		order = min(order, (unsigned int)fls(pages) - 1);
		BUG_ON(order > mm->max_order);
		BUG_ON(order < min_order);

		do {
			block = __drm_buddy_alloc_blocks(mm, start,
							 end,
							 order,
							 flags);
			if (!IS_ERR(block))
				break;

			if (order-- == min_order) {
				/* Try allocation through force merge method */
				if (mm->clear_avail &&
				    !__force_merge(mm, start, end, min_order)) {
					block = __drm_buddy_alloc_blocks(mm, start,
									 end,
									 min_order,
									 flags);
					if (!IS_ERR(block)) {
						order = min_order;
						break;
					}
				}

				/*
				 * Try contiguous block allocation through
				 * try harder method.
				 */
				if (flags & DRM_BUDDY_CONTIGUOUS_ALLOCATION &&
				    !(flags & DRM_BUDDY_RANGE_ALLOCATION))
					return __alloc_contig_try_harder(mm,
									 original_size,
									 original_min_size,
									 blocks);
				err = -ENOSPC;
				goto err_free;
			}
		} while (1);

		mark_allocated(mm, block);
		mm->avail -= drm_buddy_block_size(mm, block);
		if (drm_buddy_block_is_clear(block))
			mm->clear_avail -= drm_buddy_block_size(mm, block);
		kmemleak_update_trace(block);
		list_add_tail(&block->link, &allocated);

		pages -= BIT(order);

		if (!pages)
			break;
	} while (1);

	/* Trim the allocated block to the required size */
	if (!(flags & DRM_BUDDY_TRIM_DISABLE) &&
	    original_size != size) {
		struct list_head *trim_list;
		LIST_HEAD(temp);
		u64 trim_size;

		trim_list = &allocated;
		trim_size = original_size;

		if (!list_is_singular(&allocated)) {
			block = list_last_entry(&allocated, typeof(*block), link);
			list_move(&block->link, &temp);
			trim_list = &temp;
			trim_size = drm_buddy_block_size(mm, block) -
				(size - original_size);
		}

		drm_buddy_block_trim(mm,
				     NULL,
				     trim_size,
				     trim_list);

		if (!list_empty(&temp))
			list_splice_tail(trim_list, &allocated);
	}

	list_splice_tail(&allocated, blocks);
	return 0;

err_free:
	drm_buddy_free_list_internal(mm, &allocated);
	return err;
}
EXPORT_SYMBOL(drm_buddy_alloc_blocks);

/**
 * drm_buddy_block_print - print block information
 *
 * @mm: DRM buddy manager
 * @block: DRM buddy block
 * @p: DRM printer to use
 */
void drm_buddy_block_print(struct drm_buddy *mm,
			   struct drm_buddy_block *block,
			   struct drm_printer *p)
{
	u64 start = drm_buddy_block_offset(block);
	u64 size = drm_buddy_block_size(mm, block);

	drm_printf(p, "%#018llx-%#018llx: %llu\n", start, start + size, size);
}
EXPORT_SYMBOL(drm_buddy_block_print);

/**
 * drm_buddy_print - print allocator state
 *
 * @mm: DRM buddy manager
 * @p: DRM printer to use
 */
void drm_buddy_print(struct drm_buddy *mm, struct drm_printer *p)
{
	int order;

	drm_printf(p, "chunk_size: %lluKiB, total: %lluMiB, free: %lluMiB, clear_free: %lluMiB\n",
		   mm->chunk_size >> 10, mm->size >> 20, mm->avail >> 20, mm->clear_avail >> 20);

	for (order = mm->max_order; order >= 0; order--) {
		struct drm_buddy_block *block, *tmp;
		struct rb_root *root;
		u64 count = 0, free;
		unsigned int tree;

		for_each_free_tree(tree) {
			root = &mm->free_trees[tree][order];

			rbtree_postorder_for_each_entry_safe(block, tmp, root, rb) {
				BUG_ON(!drm_buddy_block_is_free(block));
				count++;
			}
		}

		drm_printf(p, "order-%2d ", order);

		free = count * (mm->chunk_size << order);
		if (free < SZ_1M)
			drm_printf(p, "free: %8llu KiB", free >> 10);
		else
			drm_printf(p, "free: %8llu MiB", free >> 20);

		drm_printf(p, ", blocks: %llu\n", count);
	}
}
EXPORT_SYMBOL(drm_buddy_print);

static void drm_buddy_module_exit(void)
{
	kmem_cache_destroy(slab_blocks);
}

static int __init drm_buddy_module_init(void)
{
	slab_blocks = KMEM_CACHE(drm_buddy_block, 0);
	if (!slab_blocks)
		return -ENOMEM;

	return 0;
}

module_init(drm_buddy_module_init);
module_exit(drm_buddy_module_exit);

MODULE_DESCRIPTION("DRM Buddy Allocator");
MODULE_LICENSE("Dual MIT/GPL");
