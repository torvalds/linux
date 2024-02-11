/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_RANGE_FENCE_H_
#define _XE_RANGE_FENCE_H_

#include <linux/dma-fence.h>
#include <linux/rbtree.h>
#include <linux/types.h>

struct xe_range_fence_tree;
struct xe_range_fence;

/** struct xe_range_fence_ops - XE range fence ops */
struct xe_range_fence_ops {
	/** @free: free range fence op */
	void (*free)(struct xe_range_fence *rfence);
};

/** struct xe_range_fence - XE range fence (address conflict tracking) */
struct xe_range_fence {
	/** @rb: RB tree node inserted into interval tree */
	struct rb_node rb;
	/** @start: start address of range fence is interval tree */
	u64 start;
	/** @last: last address (inclusive) of range fence is interval tree */
	u64 last;
	/** @__subtree_last: interval tree internal usage */
	u64 __subtree_last;
	/**
	 * @fence: fence signals address in range fence no longer has conflict
	 */
	struct dma_fence *fence;
	/** @tree: interval tree which range fence belongs to */
	struct xe_range_fence_tree *tree;
	/**
	 * @cb: callback when fence signals to remove range fence free from interval tree
	 */
	struct dma_fence_cb cb;
	/** @link: used to defer free of range fence to non-irq context */
	struct llist_node link;
	/** @ops: range fence ops */
	const struct xe_range_fence_ops *ops;
};

/** struct xe_range_fence_tree - interval tree to store range fences */
struct xe_range_fence_tree {
	/** @root: interval tree root */
	struct rb_root_cached root;
	/** @list: list of pending range fences to be freed */
	struct llist_head list;
};

extern const struct xe_range_fence_ops xe_range_fence_kfree_ops;

struct xe_range_fence *
xe_range_fence_tree_first(struct xe_range_fence_tree *tree, u64 start,
			  u64 last);

struct xe_range_fence *
xe_range_fence_tree_next(struct xe_range_fence *rfence, u64 start, u64 last);

void xe_range_fence_tree_init(struct xe_range_fence_tree *tree);

void xe_range_fence_tree_fini(struct xe_range_fence_tree *tree);

int xe_range_fence_insert(struct xe_range_fence_tree *tree,
			  struct xe_range_fence *rfence,
			  const struct xe_range_fence_ops *ops,
			  u64 start, u64 end,
			  struct dma_fence *fence);

#endif
