// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bug.h>
#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include <linux/gpu_buddy.h>

/**
 * gpu_buddy_assert - assert a condition in the buddy allocator
 * @condition: condition expected to be true
 *
 * When CONFIG_KUNIT is enabled, evaluates @condition and, if false, triggers
 * a WARN_ON() and also calls kunit_fail_current_test() so that any running
 * kunit test is properly marked as failed. The stringified condition is
 * included in the failure message for easy identification.
 *
 * When CONFIG_KUNIT is not enabled, this reduces to WARN_ON() so production
 * builds retain the same warning semantics as before.
 */
#if IS_ENABLED(CONFIG_KUNIT)
#include <kunit/test-bug.h>
#define gpu_buddy_assert(condition) do {						\
	if (WARN_ON(!(condition)))						\
		kunit_fail_current_test("gpu_buddy_assert(" #condition ")");	\
} while (0)
#else
#define gpu_buddy_assert(condition) WARN_ON(!(condition))
#endif

static struct kmem_cache *slab_blocks;
static struct kmem_cache *slab_extents;

/*
 * Dirty tracker
 * -------------
 *
 * The dirty tracker maintains an augmented interval rbtree of contiguous
 * dirty address ranges, decoupled from the buddy free trees.
 * Each node covers a maximal coalesced run; adjacent extents are merged
 * on insertion so the tree always holds the smallest possible number of
 * extents.  The augmentation field @subtree_max_size lets the allocator
 * locate the largest dirty extent in O(log E).
 *
 * Free trees (mm->free_tree[])
 * ----------------------------
 *
 * Per-order augmented rbtrees of FREE buddy blocks, keyed by offset.
 * Every node carries:
 *   - subtree_max_alignment: largest natural alignment in the subtree,
 *     used by aligned/range allocations to skip unsuitable subtrees in
 *     O(log N).
 *   - subtree_block_state: the highest clear class (DIRTY < MIXED < CLEAR)
 *     of any block in the subtree, maintained as a max augment. A value of
 *     >= MIXED means a clear-or-mixed block exists; == CLEAR means a
 *     fully-clear block exists.
 *
 * Block classes
 * -------------
 *
 * Each FREE block falls into one of three classes, determined in
 * mark_free() by querying the dirty tracker for the block's range:
 *
 *   clear   -- HEADER_CLEAR set; no dirty extent overlaps the range.
 *   mixed   -- HEADER_CLEAR unset; range has both dirty and clear bytes.
 *   dirty   -- HEADER_CLEAR unset; range is fully dirty.
 *
 * Clear allocation
 * ----------------
 *
 * A clear (CLEAR_ALLOCATION) request prefers clear -> mixed -> dirty.
 * Climbing from the requested order up to max_order, rbtree_last_clear_free_block()
 * returns, in one O(log N) descent per order, the right-most clear-or-mixed block
 * (fully-clear preferred over mixed) at the lowest order whose free tree contains
 * such a block. Only if no clear-or-mixed block exists at any order >= the
 * requested one does it fall back to a dirty block.
 *
 * Clear state is reported to the driver per whole block via HEADER_CLEAR, so a
 * fully-clear block of the requested order lets the driver skip the clear pass.
 *
 * The effective allocation preference depends on how the driver handles
 * freed blocks:
 *
 *   1) Never clear on free:
 *      No free block contains clear bytes, so clear allocations always
 *      fall back to dirty blocks.
 *
 *   2) Always clear on free:
 *      Freed blocks become clear while untouched blocks remain dirty.
 *      Merging clear and dirty buddies produces mixed blocks, which are
 *      reclassified when split. Over time, clear blocks become dominant,
 *      so clear allocations are typically satisfied from clear blocks,
 *      following a clear -> mixed -> dirty preference.
 *
 *   3) Selective clear on free:
 *      For each order examined, fully-clear blocks are preferred over
 *      mixed blocks, and mixed blocks are preferred over dirty blocks.
 *      If a clear or mixed block is found at an order, it is selected
 *      without searching higher orders. Dirty blocks are used only when
 *      no clear or mixed block exists at any eligible order.
 */

static u64 extent_size(struct gpu_dirty_extent *dirty_extent)
{
	return dirty_extent->end - dirty_extent->start;
}

RB_DECLARE_CALLBACKS_MAX(static, gpu_dirty_augment_cb,
			 struct gpu_dirty_extent, rb,
			 u64, subtree_max_size,
			 extent_size)

static struct gpu_dirty_extent *extent_alloc(struct gpu_dirty_tracker *dirty_tracker)
{
	return kmem_cache_alloc(slab_extents, GFP_KERNEL);
}

static void extent_free(struct gpu_dirty_tracker *dirty_tracker,
			struct gpu_dirty_extent *dirty_extent)
{
	kmem_cache_free(slab_extents, dirty_extent);
}

/* Return the rightmost extent whose start is strictly below @offset. */
static struct gpu_dirty_extent *
prev_extent(struct gpu_dirty_tracker *dirty_tracker, u64 offset)
{
	struct rb_node *rb = dirty_tracker->root.rb_node;
	struct gpu_dirty_extent *dirty_extent = NULL;

	while (rb) {
		struct gpu_dirty_extent *tmp_extent =
			rb_entry(rb, struct gpu_dirty_extent, rb);

		if (tmp_extent->start < offset) {
			dirty_extent = tmp_extent;
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	}

	return dirty_extent;
}

/* Return the leftmost extent whose start is at or above @offset. */
static struct gpu_dirty_extent *
next_extent(struct gpu_dirty_tracker *dirty_tracker, u64 offset)
{
	struct rb_node *rb = dirty_tracker->root.rb_node;
	struct gpu_dirty_extent *dirty_extent = NULL;

	while (rb) {
		struct gpu_dirty_extent *tmp_extent =
			rb_entry(rb, struct gpu_dirty_extent, rb);

		if (tmp_extent->start >= offset) {
			dirty_extent = tmp_extent;
			rb = rb->rb_left;
		} else {
			rb = rb->rb_right;
		}
	}

	return dirty_extent;
}

static void insert_extent(struct gpu_dirty_tracker *dirty_tracker,
			  struct gpu_dirty_extent *dirty_extent)
{
	struct rb_node **link = &dirty_tracker->root.rb_node;
	struct rb_node *parent = NULL;
	u64 size = extent_size(dirty_extent);

	while (*link) {
		struct gpu_dirty_extent *tmp_extent;

		parent = *link;
		tmp_extent = rb_entry(parent, struct gpu_dirty_extent, rb);

		if (tmp_extent->subtree_max_size < size)
			tmp_extent->subtree_max_size = size;

		if (dirty_extent->start < tmp_extent->start)
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	dirty_extent->subtree_max_size = size;
	rb_link_node(&dirty_extent->rb, parent, link);
	rb_insert_augmented(&dirty_extent->rb, &dirty_tracker->root, &gpu_dirty_augment_cb);
}

static void remove_extent(struct gpu_dirty_tracker *dirty_tracker,
			  struct gpu_dirty_extent *dirty_extent)
{
	rb_erase_augmented(&dirty_extent->rb, &dirty_tracker->root, &gpu_dirty_augment_cb);
	RB_CLEAR_NODE(&dirty_extent->rb);
}

static void gpu_dirty_tracker_init(struct gpu_dirty_tracker *dirty_tracker)
{
	dirty_tracker->root = RB_ROOT;
	dirty_tracker->total_dirty = 0;
}

static void gpu_dirty_tracker_empty(struct gpu_dirty_tracker *dirty_tracker)
{
	struct rb_node *rb;

	while ((rb = rb_first(&dirty_tracker->root))) {
		struct gpu_dirty_extent *dirty_extent =
			rb_entry(rb, struct gpu_dirty_extent, rb);

		remove_extent(dirty_tracker, dirty_extent);
		extent_free(dirty_tracker, dirty_extent);
	}

	dirty_tracker->total_dirty = 0;
}

static void gpu_dirty_tracker_fini(struct gpu_dirty_tracker *dirty_tracker)
{
	gpu_dirty_tracker_empty(dirty_tracker);
}

/*
 * Mark the range [start, start + size] as dirty. Merge with the neighbour on
 * each side if they are contiguous, so the tree never holds two adjacent ranges.
 */
static void gpu_dirty_tracker_mark_dirty(struct gpu_dirty_tracker *dirty_tracker,
					 u64 start, u64 size)
{
	struct gpu_dirty_extent *left, *right, *dirty_extent;
	u64 end = start + size;

	gpu_buddy_assert(size);

	/* Find contiguous neighbours, if any. */
	left = prev_extent(dirty_tracker, start);
	if (left && left->end != start)
		left = NULL;

	right = next_extent(dirty_tracker, end);
	if (right && right->start != end)
		right = NULL;

	if (left && right) {
		/* Merge left + new + right into a single extent. */
		remove_extent(dirty_tracker, left);
		remove_extent(dirty_tracker, right);
		left->end = right->end;
		extent_free(dirty_tracker, right);
		insert_extent(dirty_tracker, left);
	} else if (left) {
		/* Extend left neighbour rightwards. */
		remove_extent(dirty_tracker, left);
		left->end = end;
		insert_extent(dirty_tracker, left);
	} else if (right) {
		/* Extend right neighbour leftwards. */
		remove_extent(dirty_tracker, right);
		right->start = start;
		insert_extent(dirty_tracker, right);
	} else {
		/* Standalone extent. */
		dirty_extent = extent_alloc(dirty_tracker);
		if (!dirty_extent) {
			pr_warn_once("dirty extent allocation failed, skipping tracker update\n");
			return;
		}
		dirty_extent->start = start;
		dirty_extent->end   = end;
		insert_extent(dirty_tracker, dirty_extent);
	}

	dirty_tracker->total_dirty += size;
}

/*
 * Remove the range [start, start + size] from the dirty tracker. Punch the
 * range out of every overlapping dirty extent, splitting one extent in two if
 * the removed range falls strictly inside it.
 */
static void gpu_dirty_tracker_remove_range(struct gpu_dirty_tracker *dirty_tracker,
					   u64 start, u64 size)
{
	struct gpu_dirty_extent *dirty_extent, *next;
	u64 end = start + size;

	gpu_buddy_assert(size);

	dirty_extent = prev_extent(dirty_tracker, start + 1);
	if (!dirty_extent)
		dirty_extent = next_extent(dirty_tracker, start);

	while (dirty_extent && dirty_extent->start < end) {
		struct rb_node *next_node = rb_next(&dirty_extent->rb);
		u64 extent_start = dirty_extent->start;
		u64 extent_end = dirty_extent->end;

		if (next_node)
			next = rb_entry(next_node, struct gpu_dirty_extent, rb);
		else
			next = NULL;

		/* Skip a non-overlapping neighbour returned by prev_extent(). */
		if (extent_end <= start) {
			dirty_extent = next;
			continue;
		}

		if (extent_start < start && extent_end > end) {
			/*
			 * Removed range lies strictly inside this dirty extent:
			 * split it into the dirty left and right halves.
			 */
			struct gpu_dirty_extent *right = extent_alloc(dirty_tracker);

			if (!right) {
				pr_warn_once("dirty extent allocation failed, skipping tracker update\n");
				dirty_extent = next;
				continue;
			}

			remove_extent(dirty_tracker, dirty_extent);

			dirty_extent->end = start;
			right->start = end;
			right->end   = extent_end;

			insert_extent(dirty_tracker, dirty_extent);
			insert_extent(dirty_tracker, right);

			dirty_tracker->total_dirty -= size;
		} else if (extent_start >= start && extent_end <= end) {
			/* Extent fully covered: drop it. */
			remove_extent(dirty_tracker, dirty_extent);
			extent_free(dirty_tracker, dirty_extent);

			dirty_tracker->total_dirty -= (extent_end - extent_start);
		} else if (extent_start < start) {
			/* Extent overlaps from the left: trim its right end. */
			remove_extent(dirty_tracker, dirty_extent);
			dirty_extent->end = start;
			insert_extent(dirty_tracker, dirty_extent);

			dirty_tracker->total_dirty -= (extent_end - start);
		} else {
			/* Extent overlaps from the right: trim its left end. */
			remove_extent(dirty_tracker, dirty_extent);
			dirty_extent->start = end;
			insert_extent(dirty_tracker, dirty_extent);

			dirty_tracker->total_dirty -= (end - extent_start);
		}

		dirty_extent = next;
	}
}

static enum gpu_block_state
gpu_dirty_range_state(struct gpu_dirty_tracker *dirty_tracker,
		      u64 start, u64 size)
{
	struct gpu_dirty_extent *dirty_extent;
	u64 end = start + size;

	dirty_extent = prev_extent(dirty_tracker, start + 1);
	if (dirty_extent) {
		if (dirty_extent->start <= start && dirty_extent->end >= end)
			return GPU_BLOCK_DIRTY;
		if (dirty_extent->start < end && dirty_extent->end > start)
			return GPU_BLOCK_MIXED;
	}

	dirty_extent = next_extent(dirty_tracker, start);
	if (dirty_extent && dirty_extent->start < end)
		return GPU_BLOCK_MIXED;

	return GPU_BLOCK_CLEAR;
}

static struct rb_node *
dirty_tracker_descend_right(struct rb_node *node, u64 min_size)
{
	while (node->rb_right) {
		struct gpu_dirty_extent *tmp_extent;

		tmp_extent = rb_entry(node->rb_right, struct gpu_dirty_extent, rb);

		if (tmp_extent->subtree_max_size < min_size)
			break;
		node = node->rb_right;
	}

	return node;
}

static struct gpu_dirty_extent *
gpu_dirty_tracker_find(struct gpu_dirty_tracker *dirty_tracker,
		       u64 min_size, u64 *aligned_start_out)
{
	struct rb_node *rb = dirty_tracker->root.rb_node;
	struct gpu_dirty_extent *root_extent;
	struct rb_node *parent;

	if (!min_size || !is_power_of_2(min_size))
		return NULL;

	if (!rb)
		return NULL;

	root_extent = rb_entry(rb, struct gpu_dirty_extent, rb);
	if (root_extent->subtree_max_size < min_size)
		return NULL;

	rb = dirty_tracker_descend_right(rb, min_size);

	while (rb) {
		struct gpu_dirty_extent *dirty_extent;
		u64 aligned_start;

		dirty_extent = rb_entry(rb, struct gpu_dirty_extent, rb);
		aligned_start = ALIGN(dirty_extent->start, min_size);

		/* Check if a min_size block fits after the alignment skip. */
		if (aligned_start <= dirty_extent->end &&
		    dirty_extent->end - aligned_start >= min_size) {
			*aligned_start_out = aligned_start;
			return dirty_extent;
		}

		if (rb->rb_left) {
			struct gpu_dirty_extent *tmp_extent;

			tmp_extent = rb_entry(rb->rb_left, struct gpu_dirty_extent, rb);
			if (tmp_extent->subtree_max_size >= min_size) {
				rb = dirty_tracker_descend_right(rb->rb_left, min_size);
				continue;
			}
		}

		/* Walk up until we exit a node via its right child. */
		parent = rb_parent(rb);
		while (parent && parent->rb_right != rb) {
			rb = parent;
			parent = rb_parent(rb);
		}
		rb = parent;
	}

	return NULL;
}

static unsigned int
gpu_buddy_block_state(struct gpu_buddy_block *block)
{
	return block->header & GPU_BUDDY_HEADER_STATE;
}

static bool
gpu_buddy_block_is_allocated(struct gpu_buddy_block *block)
{
	return gpu_buddy_block_state(block) == GPU_BUDDY_ALLOCATED;
}

static bool
gpu_buddy_block_is_split(struct gpu_buddy_block *block)
{
	return gpu_buddy_block_state(block) == GPU_BUDDY_SPLIT;
}

static unsigned int gpu_buddy_block_offset_alignment(struct gpu_buddy_block *block)
{
	u64 offset = gpu_buddy_block_offset(block);

	if (!offset)
		/*
		 * __ffs64(0) is undefined; offset 0 is maximally aligned, so return
		 * a value greater than any possible alignment.
		 */
		return 64 + 1;

	return __ffs64(offset);
}

static inline enum gpu_block_state
gpu_block_cached_state(struct gpu_buddy_block *block)
{
	if (gpu_buddy_block_is_clear(block))
		return GPU_BLOCK_CLEAR;
	if (block->has_clear)
		return GPU_BLOCK_MIXED;
	return GPU_BLOCK_DIRTY;
}

static inline void gpu_buddy_augment_compute(struct gpu_buddy_block *block)
{
	enum gpu_block_state block_state;
	struct gpu_buddy_block *right;
	struct gpu_buddy_block *left;
	unsigned int max_align;

	max_align = gpu_buddy_block_offset_alignment(block);
	block_state = gpu_block_cached_state(block);

	left = rb_entry_safe(block->rb.rb_left, struct gpu_buddy_block, rb);
	if (left) {
		if (left->subtree_max_alignment > max_align)
			max_align = left->subtree_max_alignment;

		block_state = max(block_state, left->subtree_block_state);
	}

	right = rb_entry_safe(block->rb.rb_right, struct gpu_buddy_block, rb);
	if (right) {
		if (right->subtree_max_alignment > max_align)
			max_align = right->subtree_max_alignment;

		block_state = max(block_state, right->subtree_block_state);
	}

	block->subtree_max_alignment = max_align;
	block->subtree_block_state = block_state;
}

static void gpu_buddy_augment_propagate(struct rb_node *rb, struct rb_node *stop)
{
	while (rb != stop) {
		struct gpu_buddy_block *block;
		unsigned int old_align;
		enum gpu_block_state old_block_state;

		block = rb_entry(rb, struct gpu_buddy_block, rb);
		old_align = block->subtree_max_alignment;
		old_block_state = block->subtree_block_state;

		gpu_buddy_augment_compute(block);
		if (block->subtree_max_alignment == old_align &&
		    block->subtree_block_state == old_block_state)
			break;

		rb = rb_parent(&block->rb);
	}
}

static void gpu_buddy_augment_copy(struct rb_node *rb_old, struct rb_node *rb_new)
{
	struct gpu_buddy_block *old;
	struct gpu_buddy_block *new;

	old = rb_entry(rb_old, struct gpu_buddy_block, rb);
	new = rb_entry(rb_new, struct gpu_buddy_block, rb);

	new->subtree_max_alignment = old->subtree_max_alignment;
	new->subtree_block_state = old->subtree_block_state;
}

static void gpu_buddy_augment_rotate(struct rb_node *rb_old, struct rb_node *rb_new)
{
	struct gpu_buddy_block *old;
	struct gpu_buddy_block *new;

	old = rb_entry(rb_old, struct gpu_buddy_block, rb);
	new = rb_entry(rb_new, struct gpu_buddy_block, rb);

	new->subtree_max_alignment = old->subtree_max_alignment;
	new->subtree_block_state = old->subtree_block_state;

	gpu_buddy_augment_compute(old);
}

static const struct rb_augment_callbacks gpu_buddy_augment_cb = {
	.propagate = gpu_buddy_augment_propagate,
	.copy      = gpu_buddy_augment_copy,
	.rotate    = gpu_buddy_augment_rotate,
};

static struct gpu_buddy_block *gpu_block_alloc(struct gpu_buddy *mm,
					       struct gpu_buddy_block *parent,
					       unsigned int order,
					       u64 offset)
{
	struct gpu_buddy_block *block;

	BUG_ON(order > GPU_BUDDY_MAX_ORDER);

	block = kmem_cache_zalloc(slab_blocks, GFP_KERNEL);
	if (!block)
		return NULL;

	block->header = offset;
	block->header |= order;
	block->parent = parent;

	RB_CLEAR_NODE(&block->rb);

	BUG_ON(block->header & GPU_BUDDY_HEADER_UNUSED);
	return block;
}

static void gpu_block_free(struct gpu_buddy *mm,
			   struct gpu_buddy_block *block)
{
	kmem_cache_free(slab_blocks, block);
}

static struct gpu_buddy_block *
rbtree_get_free_block(const struct rb_node *node)
{
	return node ? rb_entry(node, struct gpu_buddy_block, rb) : NULL;
}

static struct gpu_buddy_block *
rbtree_last_free_block(struct rb_root *root)
{
	return rbtree_get_free_block(rb_last(root));
}

static struct gpu_buddy_block *
rbtree_last_clear_free_block(struct rb_root *root,
			     enum gpu_block_state min_block_state)
{
	struct rb_node *node = root->rb_node;
	struct gpu_buddy_block *block = NULL;
	struct gpu_buddy_block *root_block;
	enum gpu_block_state target_state;

	root_block = rbtree_get_free_block(node);
	if (!root_block || root_block->subtree_block_state < min_block_state)
		return NULL;

	target_state = root_block->subtree_block_state;

	while (node) {
		struct gpu_buddy_block *right_block;
		struct gpu_buddy_block *node_block;

		node_block = rbtree_get_free_block(node);
		right_block = rbtree_get_free_block(node->rb_right);

		if (right_block && right_block->subtree_block_state >= target_state) {
			node = node->rb_right;
			continue;
		}

		if (gpu_block_cached_state(node_block) == target_state) {
			block = node_block;
			break;
		}

		node = node->rb_left;
	}

	return block;
}

static void rbtree_insert(struct gpu_buddy *mm,
			  struct gpu_buddy_block *block)
{
	struct rb_node **link, *parent = NULL;
	enum gpu_block_state block_state;
	struct gpu_buddy_block *node;
	unsigned int block_alignment;
	struct rb_root *root;
	unsigned int order;

	order = gpu_buddy_block_order(block);
	block_alignment = gpu_buddy_block_offset_alignment(block);
	block_state = gpu_block_cached_state(block);

	root = &mm->free_tree[order];
	link = &root->rb_node;

	while (*link) {
		parent = *link;
		node = rbtree_get_free_block(parent);
		/*
		 * Manual augmentation update during insertion traversal. Required
		 * because rb_insert_augmented() only calls rotate callback during
		 * rotations. This ensures all ancestors on the insertion path have
		 * correct subtree_max_alignment / subtree_block_state values.
		 */
		if (node->subtree_max_alignment < block_alignment)
			node->subtree_max_alignment = block_alignment;
		if (node->subtree_block_state < block_state)
			node->subtree_block_state = block_state;

		if (gpu_buddy_block_offset(block) < gpu_buddy_block_offset(node))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	block->subtree_max_alignment = block_alignment;
	block->subtree_block_state = block_state;
	rb_link_node(&block->rb, parent, link);
	rb_insert_augmented(&block->rb, root, &gpu_buddy_augment_cb);
}

static void rbtree_remove(struct gpu_buddy *mm,
			  struct gpu_buddy_block *block)
{
	unsigned int order = gpu_buddy_block_order(block);

	rb_erase_augmented(&block->rb, &mm->free_tree[order], &gpu_buddy_augment_cb);
	RB_CLEAR_NODE(&block->rb);
}

static void mark_allocated(struct gpu_buddy *mm,
			   struct gpu_buddy_block *block)
{
	block->header &= ~GPU_BUDDY_HEADER_STATE;
	block->header |= GPU_BUDDY_ALLOCATED;

	block->has_clear = false;

	mm->free_scoreboard[gpu_buddy_block_order(block)]--;
	mm->used_scoreboard[gpu_buddy_block_order(block)]++;

	rbtree_remove(mm, block);
}

static void __mark_free(struct gpu_buddy *mm,
			struct gpu_buddy_block *block,
			enum gpu_block_state block_state)
{
	if (gpu_buddy_block_is_allocated(block))
		mm->used_scoreboard[gpu_buddy_block_order(block)]--;

	block->header &= ~GPU_BUDDY_HEADER_STATE;
	block->header |= GPU_BUDDY_FREE;

	block->header &= ~GPU_BUDDY_HEADER_CLEAR;

	block->has_clear = (block_state != GPU_BLOCK_DIRTY);
	if (block_state == GPU_BLOCK_CLEAR)
		block->header |= GPU_BUDDY_HEADER_CLEAR;

	mm->free_scoreboard[gpu_buddy_block_order(block)]++;

	rbtree_insert(mm, block);
}

static void mark_free(struct gpu_buddy *mm,
		      struct gpu_buddy_block *block)
{
	enum gpu_block_state block_state;

	block_state = gpu_dirty_range_state(&mm->dirty,
					    gpu_buddy_block_offset(block),
					    gpu_buddy_block_size(mm, block));
	__mark_free(mm, block, block_state);
}

static void mark_split(struct gpu_buddy *mm,
		       struct gpu_buddy_block *block)
{
	block->header &= ~GPU_BUDDY_HEADER_STATE;
	block->header |= GPU_BUDDY_SPLIT;

	mm->free_scoreboard[gpu_buddy_block_order(block)]--;

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

static struct gpu_buddy_block *
__get_buddy(struct gpu_buddy_block *block)
{
	struct gpu_buddy_block *parent;

	parent = block->parent;
	if (!parent)
		return NULL;

	if (parent->left == block)
		return parent->right;

	return parent->left;
}

static unsigned int __gpu_buddy_free(struct gpu_buddy *mm,
				     struct gpu_buddy_block *block)
{
	enum gpu_block_state block_state;
	struct gpu_buddy_block *parent;
	unsigned int order;

	block_state = gpu_block_cached_state(block);

	while ((parent = block->parent)) {
		struct gpu_buddy_block *buddy = __get_buddy(block);

		if (!gpu_buddy_block_is_free(buddy))
			break;

		if (block_state != GPU_BLOCK_MIXED) {
			enum gpu_block_state buddy_state;

			buddy_state = gpu_block_cached_state(buddy);

			if (buddy_state != block_state)
				block_state = GPU_BLOCK_MIXED;
		}

		rbtree_remove(mm, buddy);
		mm->free_scoreboard[gpu_buddy_block_order(buddy)]--;

		if (gpu_buddy_block_is_allocated(block))
			mm->used_scoreboard[gpu_buddy_block_order(block)]--;

		gpu_block_free(mm, block);
		gpu_block_free(mm, buddy);

		block = parent;
	}

	order = gpu_buddy_block_order(block);
	__mark_free(mm, block, block_state);

	return order;
}

/**
 * gpu_buddy_init - init memory manager
 *
 * @mm: GPU buddy manager to initialize
 * @size: size in bytes to manage
 * @chunk_size: minimum page size in bytes for our allocations
 *
 * Initializes the memory manager and its resources.
 *
 * Returns:
 * 0 on success, error code on failure.
 */
int gpu_buddy_init(struct gpu_buddy *mm, u64 size, u64 chunk_size)
{
	unsigned int root_count = 0;
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
	mm->chunk_size = chunk_size;
	mm->max_order = ilog2(size) - ilog2(chunk_size);

	BUG_ON(mm->max_order > GPU_BUDDY_MAX_ORDER);

	mm->free_scoreboard = kcalloc(mm->max_order + 1,
				      sizeof(*mm->free_scoreboard),
				      GFP_KERNEL);
	if (!mm->free_scoreboard)
		return -ENOMEM;

	mm->used_scoreboard = kcalloc(mm->max_order + 1,
				      sizeof(*mm->used_scoreboard),
				      GFP_KERNEL);
	if (!mm->used_scoreboard)
		goto out_free_free_scoreboard;

	mm->free_tree = kcalloc(mm->max_order + 1,
				sizeof(struct rb_root),
				GFP_KERNEL);
	if (!mm->free_tree)
		goto out_free_used_scoreboard;

	gpu_dirty_tracker_init(&mm->dirty);

	mm->n_roots = hweight64(size);

	mm->roots = kmalloc_objs(struct gpu_buddy_block *, mm->n_roots);
	if (!mm->roots)
		goto out_free_tree;

	/*
	 * Split into power-of-two blocks, in case we are given a size that is
	 * not itself a power-of-two.
	 */
	do {
		struct gpu_buddy_block *root;
		unsigned int order;
		u64 root_size;

		order = ilog2(size) - ilog2(chunk_size);
		root_size = chunk_size << order;

		root = gpu_block_alloc(mm, NULL, order, offset);
		if (!root)
			goto out_free_roots;

		gpu_dirty_tracker_mark_dirty(&mm->dirty, offset, root_size);
		__mark_free(mm, root, GPU_BLOCK_DIRTY);

		BUG_ON(root_count > mm->max_order);
		BUG_ON(gpu_buddy_block_size(mm, root) < chunk_size);

		mm->roots[root_count] = root;

		offset += root_size;
		size -= root_size;
		root_count++;
	} while (size);

#ifdef CONFIG_LOCKDEP
	mm->lock_dep_map = NULL;
#endif
	return 0;

out_free_roots:
	while (root_count--)
		gpu_block_free(mm, mm->roots[root_count]);
	kfree(mm->roots);
out_free_tree:
	gpu_dirty_tracker_fini(&mm->dirty);
	kfree(mm->free_tree);
out_free_used_scoreboard:
	kfree(mm->used_scoreboard);
out_free_free_scoreboard:
	kfree(mm->free_scoreboard);
	return -ENOMEM;
}
EXPORT_SYMBOL(gpu_buddy_init);

/**
 * gpu_buddy_fini - tear down the memory manager
 *
 * @mm: GPU buddy manager to free
 *
 * Cleanup memory manager resources and the freetree
 */
void gpu_buddy_fini(struct gpu_buddy *mm)
{
	u64 root_size, size;
	unsigned int order;
	int i;

	size = mm->size;

	for (i = 0; i < mm->n_roots; ++i) {
		order = ilog2(size) - ilog2(mm->chunk_size);
		root_size = mm->chunk_size << order;

		gpu_buddy_assert(gpu_buddy_block_is_free(mm->roots[i]));
		gpu_block_free(mm, mm->roots[i]);
		size -= root_size;
	}

	gpu_buddy_assert(mm->avail == mm->size);

	for (i = 0; i <= mm->max_order; ++i)
		gpu_buddy_assert(!mm->used_scoreboard[i]);

	gpu_dirty_tracker_fini(&mm->dirty);
	kfree(mm->free_tree);
	kfree(mm->roots);
	kfree(mm->free_scoreboard);
	kfree(mm->used_scoreboard);
}
EXPORT_SYMBOL(gpu_buddy_fini);

static int split_block(struct gpu_buddy *mm,
		       struct gpu_buddy_block *block)
{
	unsigned int block_order = gpu_buddy_block_order(block) - 1;
	u64 offset = gpu_buddy_block_offset(block);
	enum gpu_block_state parent_state;

	BUG_ON(!gpu_buddy_block_is_free(block));
	BUG_ON(!gpu_buddy_block_order(block));

	block->left = gpu_block_alloc(mm, block, block_order, offset);
	if (!block->left)
		return -ENOMEM;

	block->right = gpu_block_alloc(mm, block, block_order,
				       offset + (mm->chunk_size << block_order));
	if (!block->right) {
		gpu_block_free(mm, block->left);
		return -ENOMEM;
	}

	parent_state = gpu_block_cached_state(block);

	mark_split(mm, block);

	if (parent_state == GPU_BLOCK_MIXED) {
		mark_free(mm, block->left);
		mark_free(mm, block->right);
	} else {
		__mark_free(mm, block->left, parent_state);
		__mark_free(mm, block->right, parent_state);
	}

	return 0;
}

/**
 * gpu_buddy_reset_clear - reset blocks clear state
 *
 * @mm: GPU buddy manager
 * @is_clear: blocks clear state
 *
 * Reset the clear state based on @is_clear value for each block
 * in the freetree.
 */
void gpu_buddy_reset_clear(struct gpu_buddy *mm, bool is_clear)
{
	unsigned int i;

	gpu_buddy_driver_lock_held(mm);

	gpu_dirty_tracker_empty(&mm->dirty);

	for (i = 0; i <= mm->max_order; ++i) {
		struct gpu_buddy_block *block, *tmp;

		rbtree_postorder_for_each_entry_safe(block, tmp,
						     &mm->free_tree[i], rb) {
			if (is_clear) {
				if (!gpu_buddy_block_is_clear(block))
					block->header |= GPU_BUDDY_HEADER_CLEAR;
				block->has_clear = true;
			} else {
				block->header &= ~GPU_BUDDY_HEADER_CLEAR;
				block->has_clear = false;
				gpu_dirty_tracker_mark_dirty(&mm->dirty,
							     gpu_buddy_block_offset(block),
							     gpu_buddy_block_size(mm, block));
			}

			gpu_buddy_augment_compute(block);
		}
	}
}
EXPORT_SYMBOL(gpu_buddy_reset_clear);

static void __gpu_buddy_free_block_internal(struct gpu_buddy *mm,
					    struct gpu_buddy_block *block)
{
	u64 size = gpu_buddy_block_size(mm, block);

	gpu_buddy_driver_lock_held(mm);
	BUG_ON(!gpu_buddy_block_is_allocated(block));

	mm->avail += size;
	__gpu_buddy_free(mm, block);
}

/**
 * gpu_buddy_free_block - free a block
 *
 * @mm: GPU buddy manager
 * @block: block to be freed
 */
void gpu_buddy_free_block(struct gpu_buddy *mm,
			  struct gpu_buddy_block *block)
{
	if (!gpu_buddy_block_is_clear(block))
		gpu_dirty_tracker_mark_dirty(&mm->dirty,
					     gpu_buddy_block_offset(block),
					     gpu_buddy_block_size(mm, block));

	__gpu_buddy_free_block_internal(mm, block);
}
EXPORT_SYMBOL(gpu_buddy_free_block);

/**
 * gpu_buddy_allocated_addr_to_block - given relative address find the allocated block
 *
 * @mm: GPU buddy manager
 * @addr: Relative address
 *
 * Returns:
 * gpu_buddy_block on success, NULL or error code on failure
 */
struct gpu_buddy_block *gpu_buddy_allocated_addr_to_block(struct gpu_buddy *mm, u64 addr)
{
	struct gpu_buddy_block *block;
	LIST_HEAD(dfs);
	u64 end;
	int i;

	gpu_buddy_driver_lock_held(mm);

	end = addr + mm->chunk_size - 1;
	for (i = 0; i < mm->n_roots; ++i)
		list_add_tail(&mm->roots[i]->tmp_link, &dfs);

	do {
		u64 block_start;
		u64 block_end;

		block = list_first_entry_or_null(&dfs,
						 struct gpu_buddy_block,
						 tmp_link);
		if (!block)
			break;

		list_del(&block->tmp_link);

		block_start = gpu_buddy_block_offset(block);
		block_end = block_start + gpu_buddy_block_size(mm, block) - 1;

		if (!overlaps(addr, end, block_start, block_end))
			continue;

		if (gpu_buddy_block_is_allocated(block))
			return block;
		else if (gpu_buddy_block_is_free(block))
			return NULL;

		list_add(&block->right->tmp_link, &dfs);
		list_add(&block->left->tmp_link, &dfs);
	} while (1);

	return ERR_PTR(-ENXIO);
}
EXPORT_SYMBOL(gpu_buddy_allocated_addr_to_block);

static void __gpu_buddy_free_list(struct gpu_buddy *mm,
				  struct list_head *objects,
				  bool mark_clear,
				  bool mark_dirty)
{
	struct gpu_buddy_block *block, *on;
	u64 dirty_start = 0, dirty_size = 0;

	gpu_buddy_assert(!(mark_dirty && mark_clear));

	list_for_each_entry_safe(block, on, objects, link) {
		u64 offset = gpu_buddy_block_offset(block);
		u64 size = gpu_buddy_block_size(mm, block);

		if (mark_clear)
			block->header |= GPU_BUDDY_HEADER_CLEAR;
		else if (mark_dirty)
			block->header &= ~GPU_BUDDY_HEADER_CLEAR;

		/*
		 * Coalesce contiguous dirty blocks into one extent update so
		 * a multi-block contiguous free costs a single mark_dirty().
		 * Flush the pending extent and start over on a gap.
		 */
		if (!gpu_buddy_block_is_clear(block)) {
			if (dirty_size &&
			    (dirty_start + dirty_size == offset ||
			     offset + size == dirty_start)) {
				dirty_start = min(dirty_start, offset);
				dirty_size += size;
			} else {
				if (dirty_size)
					gpu_dirty_tracker_mark_dirty(&mm->dirty,
								     dirty_start,
								     dirty_size);
				dirty_start = offset;
				dirty_size = size;
			}
		}

		__gpu_buddy_free_block_internal(mm, block);
		cond_resched();
	}

	if (dirty_size)
		gpu_dirty_tracker_mark_dirty(&mm->dirty, dirty_start, dirty_size);

	INIT_LIST_HEAD(objects);
}

static void gpu_buddy_free_list_internal(struct gpu_buddy *mm,
					 struct list_head *objects)
{
	/*
	 * Don't touch the clear/dirty bit, since allocation is still internal
	 * at this point. For example we might have just failed part of the
	 * allocation.
	 */
	__gpu_buddy_free_list(mm, objects, false, false);
}

/**
 * gpu_buddy_free_list - free blocks
 *
 * @mm: GPU buddy manager
 * @objects: input list head to free blocks
 * @flags: optional flags like GPU_BUDDY_CLEARED
 */
void gpu_buddy_free_list(struct gpu_buddy *mm,
			 struct list_head *objects,
			 unsigned int flags)
{
	bool mark_clear = flags & GPU_BUDDY_CLEARED;

	gpu_buddy_driver_lock_held(mm);
	__gpu_buddy_free_list(mm, objects, mark_clear, !mark_clear);
}
EXPORT_SYMBOL(gpu_buddy_free_list);

static void __gpu_buddy_undo_splits(struct gpu_buddy *mm,
				    struct gpu_buddy_block *block)
{
	struct gpu_buddy_block *buddy = __get_buddy(block);

	if (buddy &&
	    (gpu_buddy_block_is_free(block) &&
	     gpu_buddy_block_is_free(buddy))) {
		rbtree_remove(mm, block);
		mm->free_scoreboard[gpu_buddy_block_order(block)]--;
		__gpu_buddy_free(mm, block);
	}
}

static struct gpu_buddy_block *
__alloc_range_bias(struct gpu_buddy *mm,
		   u64 start, u64 end,
		   unsigned int order,
		   unsigned long flags)
{
	u64 req_size = mm->chunk_size << order;
	struct gpu_buddy_block *block;
	LIST_HEAD(dfs);
	int err;
	int i;

	end = end - 1;

	/*
	 * This range-constrained search hands back the highest/right-most
	 * address that satisfies the request: the roots are seeded high-to-low
	 * and the right (higher-address) child is descended first, making
	 * top-down the default placement here. A non-top-down clear request is
	 * the only exception, where the descent is biased towards clear or
	 * clear-containing subtrees to satisfy the clear preference.
	 */
	for (i = mm->n_roots - 1; i >= 0; --i)
		list_add_tail(&mm->roots[i]->tmp_link, &dfs);

	do {
		u64 block_start;
		u64 block_end;

		block = list_first_entry_or_null(&dfs,
						 struct gpu_buddy_block,
						 tmp_link);
		if (!block)
			break;

		list_del(&block->tmp_link);

		if (gpu_buddy_block_order(block) < order)
			continue;

		block_start = gpu_buddy_block_offset(block);
		block_end = block_start + gpu_buddy_block_size(mm, block) - 1;

		if (!overlaps(start, end, block_start, block_end))
			continue;

		if (gpu_buddy_block_is_allocated(block))
			continue;

		if (block_start < start || block_end > end) {
			u64 adjusted_start = max(block_start, start);
			u64 adjusted_end = min(block_end, end);

			if (round_down(adjusted_end + 1, req_size) <=
			    round_up(adjusted_start, req_size))
				continue;
		}

		if (contains(start, end, block_start, block_end) &&
		    order == gpu_buddy_block_order(block)) {
			/*
			 * Find the free block within the range.
			 */
			if (gpu_buddy_block_is_free(block))
				return block;

			continue;
		}

		if (!gpu_buddy_block_is_split(block)) {
			err = split_block(mm, block);
			if (unlikely(err))
				goto err_undo;
		}

		/*
		 * Top-down is a strict address-placement policy, so when it is
		 * requested we ignore clear steering and simply descend the
		 * right (higher-address) child first. Only a non-top-down clear
		 * request biases the descent towards clear/has_clear subtrees.
		 */
		if ((flags & GPU_BUDDY_CLEAR_ALLOCATION) &&
		    !(flags & GPU_BUDDY_TOPDOWN_ALLOCATION)) {
			struct gpu_buddy_block *prefer;

			if (gpu_buddy_block_is_clear(block->right))
				prefer = block->right;
			else if (gpu_buddy_block_is_clear(block->left))
				prefer = block->left;
			else if (block->right->has_clear)
				prefer = block->right;
			else if (block->left->has_clear)
				prefer = block->left;
			else
				prefer = block->right;

			if (prefer == block->right) {
				list_add(&block->left->tmp_link, &dfs);
				list_add(&block->right->tmp_link, &dfs);
			} else {
				list_add(&block->right->tmp_link, &dfs);
				list_add(&block->left->tmp_link, &dfs);
			}
		} else {
			list_add(&block->left->tmp_link, &dfs);
			list_add(&block->right->tmp_link, &dfs);
		}
	} while (1);

	return ERR_PTR(-ENOSPC);

err_undo:
	/*
	 * We really don't want to leave around a bunch of split blocks, since
	 * bigger is better, so make sure we merge everything back before we
	 * free the allocated blocks.
	 */
	__gpu_buddy_undo_splits(mm, block);
	return ERR_PTR(err);
}

/* Return the highest-address free block of at least @order. */
static struct gpu_buddy_block *
get_maxblock(struct gpu_buddy *mm,
	     unsigned int order)
{
	struct gpu_buddy_block *max_block;
	struct gpu_buddy_block *block;
	unsigned int i;

	/*
	 * Top-down allocation is a strict address-placement policy: the block
	 * is chosen purely by offset, regardless of its clear/dirty state.
	 * Clear state is re-derived from the dirty tracker once the allocation
	 * completes, and the driver is responsible for issuing the clear pass
	 * if a clear region is required.
	 */
	max_block = NULL;

	for (i = order; i <= mm->max_order; ++i) {
		block = rbtree_last_free_block(&mm->free_tree[i]);
		if (!block)
			continue;

		if (!max_block ||
		    gpu_buddy_block_offset(block) > gpu_buddy_block_offset(max_block))
			max_block = block;
	}

	return max_block;
}

static struct gpu_buddy_block *
alloc_from_freetree(struct gpu_buddy *mm,
		    unsigned int order,
		    unsigned long flags)
{
	struct gpu_buddy_block *block = NULL;
	unsigned int tmp;
	int err;

	if (flags & GPU_BUDDY_TOPDOWN_ALLOCATION) {
		block = get_maxblock(mm, order);
		if (block)
			tmp = gpu_buddy_block_order(block);
	} else {
		if (flags & GPU_BUDDY_CLEAR_ALLOCATION) {
			for (tmp = order; tmp <= mm->max_order; ++tmp) {
				block = rbtree_last_clear_free_block(&mm->free_tree[tmp],
								     GPU_BLOCK_MIXED);
				if (block)
					break;
			}
		}
		if (!block) {
			for (tmp = order; tmp <= mm->max_order; ++tmp) {
				block = rbtree_last_free_block(&mm->free_tree[tmp]);
				if (block)
					break;
			}
		}
	}

	if (!block)
		return ERR_PTR(-ENOSPC);

	BUG_ON(!gpu_buddy_block_is_free(block));

	while (tmp != order) {
		err = split_block(mm, block);
		if (unlikely(err))
			goto err_undo;

		if ((flags & GPU_BUDDY_CLEAR_ALLOCATION) &&
		    !(flags & GPU_BUDDY_TOPDOWN_ALLOCATION)) {
			bool right_clear, left_clear;

			right_clear = gpu_buddy_block_is_clear(block->right);
			left_clear = gpu_buddy_block_is_clear(block->left);

			if (right_clear)
				block = block->right;
			else if (left_clear)
				block = block->left;
			else if (block->right->has_clear)
				block = block->right;
			else if (block->left->has_clear)
				block = block->left;
			else
				block = block->right;
		} else {
			block = block->right;
		}
		tmp--;
	}
	return block;

err_undo:
	__gpu_buddy_undo_splits(mm, block);
	return ERR_PTR(err);
}

static bool
gpu_buddy_can_offset_align(u64 size, u64 min_block_size)
{
	return size < min_block_size && is_power_of_2(size);
}

static bool gpu_buddy_subtree_can_satisfy(struct rb_node *node,
					  unsigned int alignment)
{
	struct gpu_buddy_block *block;

	block = rbtree_get_free_block(node);
	return block->subtree_max_alignment >= alignment;
}

static struct gpu_buddy_block *
gpu_buddy_find_block_aligned(struct gpu_buddy *mm,
			     unsigned int order,
			     unsigned int alignment)
{
	struct rb_root *root = &mm->free_tree[order];
	struct rb_node *rb = root->rb_node;

	while (rb) {
		struct gpu_buddy_block *block = rbtree_get_free_block(rb);
		struct rb_node *left_node = rb->rb_left, *right_node = rb->rb_right;

		if (right_node) {
			if (gpu_buddy_subtree_can_satisfy(right_node, alignment)) {
				rb = right_node;
				continue;
			}
		}

		if (gpu_buddy_block_offset_alignment(block) >= alignment)
			return block;

		if (left_node) {
			if (gpu_buddy_subtree_can_satisfy(left_node, alignment)) {
				rb = left_node;
				continue;
			}
		}

		break;
	}

	return NULL;
}

static struct gpu_buddy_block *
gpu_buddy_offset_aligned_allocation(struct gpu_buddy *mm,
				    u64 size,
				    u64 min_block_size)
{
	struct gpu_buddy_block *block = NULL;
	unsigned int order, tmp, alignment;
	unsigned long pages;
	int err;

	alignment = ilog2(min_block_size);
	pages = size >> ilog2(mm->chunk_size);
	order = fls(pages) - 1;

	/*
	 * Offset-aligned allocation is a strict address-placement policy: the
	 * block is chosen purely by its offset alignment, regardless of its
	 * clear/dirty state. Clear state is re-derived from the dirty tracker
	 * once the allocation completes, and the driver is responsible for
	 * issuing the clear pass if a clear region is required.
	 */
	for (tmp = order; tmp <= mm->max_order; ++tmp) {
		block = gpu_buddy_find_block_aligned(mm, tmp, alignment);
		if (block)
			break;
	}

	if (!block)
		return ERR_PTR(-ENOSPC);

	while (gpu_buddy_block_order(block) > order) {
		struct gpu_buddy_block *left, *right;

		err = split_block(mm, block);
		if (unlikely(err))
			goto err_undo;

		left  = block->left;
		right = block->right;

		if (gpu_buddy_block_offset_alignment(right) >= alignment)
			block = right;
		else
			block = left;
	}

	return block;

err_undo:
	/*
	 * We really don't want to leave around a bunch of split blocks, since
	 * bigger is better, so make sure we merge everything back before we
	 * free the allocated blocks.
	 */
	__gpu_buddy_undo_splits(mm, block);
	return ERR_PTR(err);
}

static int __alloc_range(struct gpu_buddy *mm,
			 struct list_head *dfs,
			 u64 start, u64 size,
			 unsigned long flags,
			 struct list_head *blocks,
			 u64 *total_allocated_on_err)
{
	struct gpu_buddy_block *block;
	u64 total_allocated = 0;
	LIST_HEAD(allocated);
	u64 end;
	int err;

	end = start + size - 1;

	do {
		u64 block_start;
		u64 block_end;

		block = list_first_entry_or_null(dfs,
						 struct gpu_buddy_block,
						 tmp_link);
		if (!block)
			break;

		list_del(&block->tmp_link);

		block_start = gpu_buddy_block_offset(block);
		block_end = block_start + gpu_buddy_block_size(mm, block) - 1;

		if (!overlaps(start, end, block_start, block_end))
			continue;

		if (gpu_buddy_block_is_allocated(block)) {
			err = -ENOSPC;
			goto err_free;
		}

		if (contains(start, end, block_start, block_end)) {
			if (gpu_buddy_block_is_free(block)) {
				u64 block_offset;
				u64 block_size;

				block_size = gpu_buddy_block_size(mm, block);
				block_offset = gpu_buddy_block_offset(block);

				if (!gpu_buddy_block_is_clear(block))
					gpu_dirty_tracker_remove_range(&mm->dirty,
								       block_offset,
								       block_size);

				mark_allocated(mm, block);
				total_allocated += block_size;
				mm->avail -= block_size;

				list_add_tail(&block->link, &allocated);
				continue;
			}
		}

		if (!gpu_buddy_block_is_split(block)) {
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
	__gpu_buddy_undo_splits(mm, block);

err_free:
	if (err == -ENOSPC && total_allocated_on_err) {
		list_splice_tail(&allocated, blocks);
		*total_allocated_on_err = total_allocated;
	} else {
		gpu_buddy_free_list_internal(mm, &allocated);
	}

	return err;
}

static int __gpu_buddy_alloc_range(struct gpu_buddy *mm,
				   u64 start,
				   u64 size,
				   unsigned long flags,
				   u64 *total_allocated_on_err,
				   struct list_head *blocks)
{
	LIST_HEAD(dfs);
	int i;

	for (i = 0; i < mm->n_roots; ++i)
		list_add_tail(&mm->roots[i]->tmp_link, &dfs);

	return __alloc_range(mm, &dfs, start, size, flags,
			     blocks, total_allocated_on_err);
}

static int __alloc_contig_aligned_retry(struct gpu_buddy *mm,
					u64 unaligned_offset,
					u64 size,
					u64 min_block_size,
					unsigned long flags,
					struct list_head *blocks)
{
	u64 aligned_offset = round_down(unaligned_offset, min_block_size);

	return __gpu_buddy_alloc_range(mm, aligned_offset, size, flags,
				       NULL, blocks);
}

static int __alloc_contig_try_harder(struct gpu_buddy *mm,
				     u64 size,
				     u64 min_block_size,
				     unsigned long flags,
				     struct list_head *blocks)
{
	u64 rhs_offset, lhs_offset, filled;
	struct gpu_buddy_block *block;
	struct rb_root *root;
	struct rb_node *iter;
	unsigned long pages;
	unsigned int order;
	u64 modify_size;
	int err;

	modify_size = rounddown_pow_of_two(size);
	pages = modify_size >> ilog2(mm->chunk_size);
	order = fls(pages) - 1;
	if (order == 0)
		return -ENOSPC;

	root = &mm->free_tree[order];
	if (RB_EMPTY_ROOT(root))
		return -ENOSPC;

	iter = rb_last(root);
	while (iter) {
		block = rbtree_get_free_block(iter);

		rhs_offset = gpu_buddy_block_offset(block);

		/* Allocate blocks traversing RHS */
		err =  __gpu_buddy_alloc_range(mm, rhs_offset, size,
					       flags, &filled, blocks);
		if (err && err != -ENOSPC)
			return err;
		if (!err && IS_ALIGNED(rhs_offset, min_block_size))
			return 0;
		if (!err) {
			/* Allocate the unaligned RHS offset using round_down */
			gpu_buddy_free_list_internal(mm, blocks);
			err = __alloc_contig_aligned_retry(mm, rhs_offset,
							   size,
							   min_block_size,
							   flags, blocks);
			if (!err)
				return 0;
			if (err != -ENOSPC) {
				gpu_buddy_free_list_internal(mm, blocks);
				return err;
			}
			goto next;
		}

		if (size - filled > rhs_offset)
			goto next;

		lhs_offset = rhs_offset - (size - filled);

		/* Allocate the unaligned LHS offset using round_down */
		gpu_buddy_free_list_internal(mm, blocks);
		err = __alloc_contig_aligned_retry(mm, lhs_offset,
						   size,
						   min_block_size,
						   flags, blocks);
		if (!err)
			return 0;
		if (err != -ENOSPC) {
			gpu_buddy_free_list_internal(mm, blocks);
			return err;
		}
next:
		gpu_buddy_free_list_internal(mm, blocks);
		iter = rb_prev(iter);
	}

	return -ENOSPC;
}

/**
 * gpu_buddy_block_trim - free unused pages
 *
 * @mm: GPU buddy manager
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
int gpu_buddy_block_trim(struct gpu_buddy *mm,
			 u64 *start,
			 u64 new_size,
			 struct list_head *blocks)
{
	struct gpu_buddy_block *parent;
	struct gpu_buddy_block *block;
	u64 block_start, block_end;
	LIST_HEAD(dfs);
	bool was_clear;
	u64 new_start;
	int err;

	gpu_buddy_driver_lock_held(mm);

	if (!list_is_singular(blocks))
		return -EINVAL;

	block = list_first_entry(blocks,
				 struct gpu_buddy_block,
				 link);

	block_start = gpu_buddy_block_offset(block);
	block_end = block_start + gpu_buddy_block_size(mm, block);

	if (WARN_ON(!gpu_buddy_block_is_allocated(block)))
		return -EINVAL;

	if (new_size > gpu_buddy_block_size(mm, block))
		return -EINVAL;

	if (!new_size || !IS_ALIGNED(new_size, mm->chunk_size))
		return -EINVAL;

	if (new_size == gpu_buddy_block_size(mm, block))
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

	was_clear = gpu_buddy_block_is_clear(block);

	if (!was_clear)
		gpu_dirty_tracker_mark_dirty(&mm->dirty,
					     gpu_buddy_block_offset(block),
					     gpu_buddy_block_size(mm, block));

	__mark_free(mm, block, was_clear ? GPU_BLOCK_CLEAR : GPU_BLOCK_DIRTY);
	mm->avail += gpu_buddy_block_size(mm, block);

	/* Prevent recursively freeing this node */
	parent = block->parent;
	block->parent = NULL;

	list_add(&block->tmp_link, &dfs);
	err =  __alloc_range(mm, &dfs, new_start, new_size,
			     was_clear ? GPU_BUDDY_CLEAR_ALLOCATION : 0,
			     blocks, NULL);
	if (err) {
		mark_allocated(mm, block);
		mm->avail -= gpu_buddy_block_size(mm, block);
		if (!was_clear) {
			gpu_dirty_tracker_remove_range(&mm->dirty,
						       gpu_buddy_block_offset(block),
						       gpu_buddy_block_size(mm, block));
		}
		if (was_clear)
			block->header |= GPU_BUDDY_HEADER_CLEAR;
		list_add(&block->link, blocks);
	}

	block->parent = parent;
	return err;
}
EXPORT_SYMBOL(gpu_buddy_block_trim);

static bool dirty_steer_window(struct gpu_buddy *mm, u64 req_size,
			       u64 *start, u64 *end, unsigned long *flags)
{
	u64 aligned_start;
	struct gpu_dirty_extent *ext =
		gpu_dirty_tracker_find(&mm->dirty, req_size, &aligned_start);

	if (!ext)
		return false;

	*start  = aligned_start;
	*end    = ext->end;
	*flags |= GPU_BUDDY_RANGE_ALLOCATION;
	return true;
}

static struct gpu_buddy_block *
__gpu_buddy_alloc_blocks(struct gpu_buddy *mm,
			 u64 start, u64 end,
			 u64 size, u64 min_block_size,
			 unsigned int order,
			 unsigned long flags)
{
	struct gpu_buddy_block *block;
	bool steered = false;

	/* Allocate from dirty tracker */
	if (!(flags & GPU_BUDDY_RANGE_ALLOCATION) &&
	    !(flags & GPU_BUDDY_CLEAR_ALLOCATION) &&
	    size >= min_block_size &&
	    gpu_buddy_clear_avail(mm) && mm->dirty.total_dirty) {
		u64 block_size = mm->chunk_size << order;

		steered = dirty_steer_window(mm, block_size,
					     &start, &end, &flags);
	}

	if (flags & GPU_BUDDY_RANGE_ALLOCATION) {
		/* Allocate traversing within the range */
		block = __alloc_range_bias(mm, start, end, order, flags);
		if (!IS_ERR(block) || !steered)
			return block;

		flags &= ~GPU_BUDDY_RANGE_ALLOCATION;
	}

	if (size < min_block_size)
		/* Allocate from an offset-aligned region without size rounding */
		return gpu_buddy_offset_aligned_allocation(mm, size,
							   min_block_size);

	/* Allocate from freetree */
	return alloc_from_freetree(mm, order, flags);
}

/**
 * gpu_buddy_alloc_blocks - allocate power-of-two blocks
 *
 * @mm: GPU buddy manager to allocate from
 * @start: start of the allowed range for this block
 * @end: end of the allowed range for this block
 * @size: size of the allocation in bytes
 * @min_block_size: alignment of the allocation
 * @blocks: output list head to add allocated blocks
 * @flags: GPU_BUDDY_*_ALLOCATION flags
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
int gpu_buddy_alloc_blocks(struct gpu_buddy *mm,
			   u64 start, u64 end, u64 size,
			   u64 min_block_size,
			   struct list_head *blocks,
			   unsigned long flags)
{
	struct gpu_buddy_block *block = NULL;
	u64 original_size, original_min_size;
	unsigned int min_order, order;
	LIST_HEAD(allocated);
	unsigned long pages;
	int err;

	gpu_buddy_driver_lock_held(mm);

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

		return __gpu_buddy_alloc_range(mm, start, size, flags, NULL, blocks);
	}

	original_size = size;
	original_min_size = min_block_size;

	/* Roundup the size to power of 2 */
	if (flags & GPU_BUDDY_CONTIGUOUS_ALLOCATION) {
		size = roundup_pow_of_two(size);
		min_block_size = size;
		/*
		 * Normalize the requested size to min_block_size for regular allocations.
		 * Offset-aligned allocations intentionally skip size rounding.
		 */
	} else if (!gpu_buddy_can_offset_align(size, min_block_size)) {
		size = round_up(size, min_block_size);
	}

	pages = size >> ilog2(mm->chunk_size);
	order = fls(pages) - 1;
	min_order = ilog2(min_block_size) - ilog2(mm->chunk_size);

	if (order > mm->max_order || size > mm->size) {
		if ((flags & GPU_BUDDY_CONTIGUOUS_ALLOCATION) &&
		    !(flags & GPU_BUDDY_RANGE_ALLOCATION))
			return __alloc_contig_try_harder(mm, original_size,
							 original_min_size,
							 flags, blocks);

		return -EINVAL;
	}

	do {
		order = min(order, (unsigned int)fls(pages) - 1);
		BUG_ON(order > mm->max_order);
		/*
		 * Regular allocations must not allocate blocks smaller than min_block_size.
		 * Offset-aligned allocations deliberately bypass this constraint.
		 */
		BUG_ON(size >= min_block_size && order < min_order);

		do {
			block = __gpu_buddy_alloc_blocks(mm, start,
							 end,
							 size,
							 min_block_size,
							 order,
							 flags);
			if (!IS_ERR(block))
				break;

			if (size >= min_block_size && order > min_order) {
				order--;
				continue;
			}

			/*
			 * Try contiguous block allocation through
			 * try harder method.
			 */
			if (flags & GPU_BUDDY_CONTIGUOUS_ALLOCATION &&
			    !(flags & GPU_BUDDY_RANGE_ALLOCATION)) {
				err = __alloc_contig_try_harder(mm,
								original_size,
								original_min_size,
								flags,
								blocks);
				if (!err)
					return 0;
				if (err != -ENOSPC)
					return err;
				goto err_free;
			}
			err = -ENOSPC;
			goto err_free;
		} while (1);

		if (!gpu_buddy_block_is_clear(block))
			gpu_dirty_tracker_remove_range(&mm->dirty,
						       gpu_buddy_block_offset(block),
						       gpu_buddy_block_size(mm, block));

		mark_allocated(mm, block);
		mm->avail -= gpu_buddy_block_size(mm, block);

		kmemleak_update_trace(block);
		list_add_tail(&block->link, &allocated);

		pages -= BIT(order);

		if (!pages)
			break;
	} while (1);

	/* Trim the allocated block to the required size */
	if (!(flags & GPU_BUDDY_TRIM_DISABLE) &&
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
			trim_size = gpu_buddy_block_size(mm, block) -
				(size - original_size);
		}

		gpu_buddy_block_trim(mm,
				     NULL,
				     trim_size,
				     trim_list);

		if (!list_empty(&temp))
			list_splice_tail(trim_list, &allocated);
	}

	list_splice_tail(&allocated, blocks);
	return 0;

err_free:
	gpu_buddy_free_list_internal(mm, &allocated);
	return err;
}
EXPORT_SYMBOL(gpu_buddy_alloc_blocks);

/**
 * gpu_buddy_block_print - print block information
 *
 * @mm: GPU buddy manager
 * @block: GPU buddy block
 */
void gpu_buddy_block_print(struct gpu_buddy *mm,
			   struct gpu_buddy_block *block)
{
	u64 start = gpu_buddy_block_offset(block);
	u64 size = gpu_buddy_block_size(mm, block);

	pr_info("%#018llx-%#018llx: %llu\n", start, start + size, size);
}
EXPORT_SYMBOL(gpu_buddy_block_print);

/**
 * gpu_buddy_print - print allocator state
 *
 * @mm: GPU buddy manager
 */
void gpu_buddy_print(struct gpu_buddy *mm)
{
	int order;

	gpu_buddy_driver_lock_held(mm);
	pr_info("chunk_size: %lluKiB, total: %lluMiB, free: %lluMiB, clear_free: %lluMiB\n",
		mm->chunk_size >> 10, mm->size >> 20, mm->avail >> 20,
		gpu_buddy_clear_avail(mm) >> 20);

	for (order = mm->max_order; order >= 0; order--) {
		u64 free_count = mm->free_scoreboard[order];
		u64 used_count = mm->used_scoreboard[order];
		u64 block_size = mm->chunk_size << order;
		u64 free = free_count * block_size;
		u64 used = used_count * block_size;

		if (block_size < SZ_1M)
			pr_info("order-%2d free: %8llu KiB, used: %8llu KiB, free_blocks: %llu, used_blocks: %llu\n",
				order, free >> 10, used >> 10, free_count, used_count);
		else
			pr_info("order-%2d free: %8llu MiB, used: %8llu MiB, free_blocks: %llu, used_blocks: %llu\n",
				order, free >> 20, used >> 20, free_count, used_count);
	}
}
EXPORT_SYMBOL(gpu_buddy_print);

static void gpu_buddy_module_exit(void)
{
	kmem_cache_destroy(slab_extents);
	kmem_cache_destroy(slab_blocks);
}

static int __init gpu_buddy_module_init(void)
{
	slab_blocks = KMEM_CACHE(gpu_buddy_block, 0);
	if (!slab_blocks)
		return -ENOMEM;

	slab_extents = KMEM_CACHE(gpu_dirty_extent, 0);
	if (!slab_extents)
		goto err_destroy_blocks;

	return 0;

err_destroy_blocks:
	kmem_cache_destroy(slab_blocks);
	return -ENOMEM;
}

module_init(gpu_buddy_module_init);
module_exit(gpu_buddy_module_exit);

MODULE_DESCRIPTION("GPU Buddy Allocator");
MODULE_LICENSE("Dual MIT/GPL");
