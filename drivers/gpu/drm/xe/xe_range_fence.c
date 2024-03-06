// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/dma-fence.h>
#include <linux/interval_tree_generic.h>
#include <linux/slab.h>

#include "xe_macros.h"
#include "xe_range_fence.h"

#define XE_RANGE_TREE_START(_node)	((_node)->start)
#define XE_RANGE_TREE_LAST(_node)	((_node)->last)

INTERVAL_TREE_DEFINE(struct xe_range_fence, rb, u64, __subtree_last,
		     XE_RANGE_TREE_START, XE_RANGE_TREE_LAST, static,
		     xe_range_fence_tree);

static void
xe_range_fence_signal_notify(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct xe_range_fence *rfence = container_of(cb, typeof(*rfence), cb);
	struct xe_range_fence_tree *tree = rfence->tree;

	llist_add(&rfence->link, &tree->list);
}

static bool __xe_range_fence_tree_cleanup(struct xe_range_fence_tree *tree)
{
	struct llist_node *node = llist_del_all(&tree->list);
	struct xe_range_fence *rfence, *next;

	llist_for_each_entry_safe(rfence, next, node, link) {
		xe_range_fence_tree_remove(rfence, &tree->root);
		dma_fence_put(rfence->fence);
		kfree(rfence);
	}

	return !!node;
}

/**
 * xe_range_fence_insert() - range fence insert
 * @tree: range fence tree to insert intoi
 * @rfence: range fence
 * @ops: range fence ops
 * @start: start address of range fence
 * @last: last address of range fence
 * @fence: dma fence which signals range fence can be removed + freed
 *
 * Return: 0 on success, non-zero on failure
 */
int xe_range_fence_insert(struct xe_range_fence_tree *tree,
			  struct xe_range_fence *rfence,
			  const struct xe_range_fence_ops *ops,
			  u64 start, u64 last, struct dma_fence *fence)
{
	int err = 0;

	__xe_range_fence_tree_cleanup(tree);

	if (dma_fence_is_signaled(fence))
		goto free;

	rfence->ops = ops;
	rfence->start = start;
	rfence->last = last;
	rfence->tree = tree;
	rfence->fence = dma_fence_get(fence);
	err = dma_fence_add_callback(fence, &rfence->cb,
				     xe_range_fence_signal_notify);
	if (err == -ENOENT) {
		dma_fence_put(fence);
		err = 0;
		goto free;
	} else if (err == 0) {
		xe_range_fence_tree_insert(rfence, &tree->root);
		return 0;
	}

free:
	if (ops->free)
		ops->free(rfence);

	return err;
}

static void xe_range_fence_tree_remove_all(struct xe_range_fence_tree *tree)
{
	struct xe_range_fence *rfence;
	bool retry = true;

	rfence = xe_range_fence_tree_iter_first(&tree->root, 0, U64_MAX);
	while (rfence) {
		/* Should be ok with the minimalistic callback */
		if (dma_fence_remove_callback(rfence->fence, &rfence->cb))
			llist_add(&rfence->link, &tree->list);
		rfence = xe_range_fence_tree_iter_next(rfence, 0, U64_MAX);
	}

	while (retry)
		retry = __xe_range_fence_tree_cleanup(tree);
}

/**
 * xe_range_fence_tree_init() - Init range fence tree
 * @tree: range fence tree
 */
void xe_range_fence_tree_init(struct xe_range_fence_tree *tree)
{
	memset(tree, 0, sizeof(*tree));
}

/**
 * xe_range_fence_tree_fini() - Fini range fence tree
 * @tree: range fence tree
 */
void xe_range_fence_tree_fini(struct xe_range_fence_tree *tree)
{
	xe_range_fence_tree_remove_all(tree);
	XE_WARN_ON(!RB_EMPTY_ROOT(&tree->root.rb_root));
}

/**
 * xe_range_fence_tree_first() - range fence tree iterator first
 * @tree: range fence tree
 * @start: start address of range fence
 * @last: last address of range fence
 *
 * Return: first range fence found in range or NULL
 */
struct xe_range_fence *
xe_range_fence_tree_first(struct xe_range_fence_tree *tree, u64 start,
			  u64 last)
{
	return xe_range_fence_tree_iter_first(&tree->root, start, last);
}

/**
 * xe_range_fence_tree_next() - range fence tree iterator next
 * @rfence: current range fence
 * @start: start address of range fence
 * @last: last address of range fence
 *
 * Return: next range fence found in range or NULL
 */
struct xe_range_fence *
xe_range_fence_tree_next(struct xe_range_fence *rfence, u64 start, u64 last)
{
	return xe_range_fence_tree_iter_next(rfence, start, last);
}

static void xe_range_fence_free(struct xe_range_fence *rfence)
{
	kfree(rfence);
}

const struct xe_range_fence_ops xe_range_fence_kfree_ops = {
	.free = xe_range_fence_free,
};
