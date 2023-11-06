// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2020 Cornelis Networks, Inc.
 * Copyright(c) 2016 - 2017 Intel Corporation.
 */

#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/mmu_notifier.h>
#include <linux/interval_tree_generic.h>
#include <linux/sched/mm.h>

#include "mmu_rb.h"
#include "trace.h"

static unsigned long mmu_node_start(struct mmu_rb_node *);
static unsigned long mmu_node_last(struct mmu_rb_node *);
static int mmu_notifier_range_start(struct mmu_notifier *,
		const struct mmu_notifier_range *);
static struct mmu_rb_node *__mmu_rb_search(struct mmu_rb_handler *,
					   unsigned long, unsigned long);
static void release_immediate(struct kref *refcount);
static void handle_remove(struct work_struct *work);

static const struct mmu_notifier_ops mn_opts = {
	.invalidate_range_start = mmu_notifier_range_start,
};

INTERVAL_TREE_DEFINE(struct mmu_rb_node, node, unsigned long, __last,
		     mmu_node_start, mmu_node_last, static, __mmu_int_rb);

static unsigned long mmu_node_start(struct mmu_rb_node *node)
{
	return node->addr & PAGE_MASK;
}

static unsigned long mmu_node_last(struct mmu_rb_node *node)
{
	return PAGE_ALIGN(node->addr + node->len) - 1;
}

int hfi1_mmu_rb_register(void *ops_arg,
			 struct mmu_rb_ops *ops,
			 struct workqueue_struct *wq,
			 struct mmu_rb_handler **handler)
{
	struct mmu_rb_handler *h;
	void *free_ptr;
	int ret;

	free_ptr = kzalloc(sizeof(*h) + cache_line_size() - 1, GFP_KERNEL);
	if (!free_ptr)
		return -ENOMEM;

	h = PTR_ALIGN(free_ptr, cache_line_size());
	h->root = RB_ROOT_CACHED;
	h->ops = ops;
	h->ops_arg = ops_arg;
	INIT_HLIST_NODE(&h->mn.hlist);
	spin_lock_init(&h->lock);
	h->mn.ops = &mn_opts;
	INIT_WORK(&h->del_work, handle_remove);
	INIT_LIST_HEAD(&h->del_list);
	INIT_LIST_HEAD(&h->lru_list);
	h->wq = wq;
	h->free_ptr = free_ptr;

	ret = mmu_notifier_register(&h->mn, current->mm);
	if (ret) {
		kfree(free_ptr);
		return ret;
	}

	*handler = h;
	return 0;
}

void hfi1_mmu_rb_unregister(struct mmu_rb_handler *handler)
{
	struct mmu_rb_node *rbnode;
	struct rb_node *node;
	unsigned long flags;
	struct list_head del_list;

	/* Prevent freeing of mm until we are completely finished. */
	mmgrab(handler->mn.mm);

	/* Unregister first so we don't get any more notifications. */
	mmu_notifier_unregister(&handler->mn, handler->mn.mm);

	/*
	 * Make sure the wq delete handler is finished running.  It will not
	 * be triggered once the mmu notifiers are unregistered above.
	 */
	flush_work(&handler->del_work);

	INIT_LIST_HEAD(&del_list);

	spin_lock_irqsave(&handler->lock, flags);
	while ((node = rb_first_cached(&handler->root))) {
		rbnode = rb_entry(node, struct mmu_rb_node, node);
		rb_erase_cached(node, &handler->root);
		/* move from LRU list to delete list */
		list_move(&rbnode->list, &del_list);
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	while (!list_empty(&del_list)) {
		rbnode = list_first_entry(&del_list, struct mmu_rb_node, list);
		list_del(&rbnode->list);
		kref_put(&rbnode->refcount, release_immediate);
	}

	/* Now the mm may be freed. */
	mmdrop(handler->mn.mm);

	kfree(handler->free_ptr);
}

int hfi1_mmu_rb_insert(struct mmu_rb_handler *handler,
		       struct mmu_rb_node *mnode)
{
	struct mmu_rb_node *node;
	unsigned long flags;
	int ret = 0;

	trace_hfi1_mmu_rb_insert(mnode);

	if (current->mm != handler->mn.mm)
		return -EPERM;

	spin_lock_irqsave(&handler->lock, flags);
	node = __mmu_rb_search(handler, mnode->addr, mnode->len);
	if (node) {
		ret = -EEXIST;
		goto unlock;
	}
	__mmu_int_rb_insert(mnode, &handler->root);
	list_add_tail(&mnode->list, &handler->lru_list);
	mnode->handler = handler;
unlock:
	spin_unlock_irqrestore(&handler->lock, flags);
	return ret;
}

/* Caller must hold handler lock */
struct mmu_rb_node *hfi1_mmu_rb_get_first(struct mmu_rb_handler *handler,
					  unsigned long addr, unsigned long len)
{
	struct mmu_rb_node *node;

	trace_hfi1_mmu_rb_search(addr, len);
	node = __mmu_int_rb_iter_first(&handler->root, addr, (addr + len) - 1);
	if (node)
		list_move_tail(&node->list, &handler->lru_list);
	return node;
}

/* Caller must hold handler lock */
static struct mmu_rb_node *__mmu_rb_search(struct mmu_rb_handler *handler,
					   unsigned long addr,
					   unsigned long len)
{
	struct mmu_rb_node *node = NULL;

	trace_hfi1_mmu_rb_search(addr, len);
	if (!handler->ops->filter) {
		node = __mmu_int_rb_iter_first(&handler->root, addr,
					       (addr + len) - 1);
	} else {
		for (node = __mmu_int_rb_iter_first(&handler->root, addr,
						    (addr + len) - 1);
		     node;
		     node = __mmu_int_rb_iter_next(node, addr,
						   (addr + len) - 1)) {
			if (handler->ops->filter(node, addr, len))
				return node;
		}
	}
	return node;
}

/*
 * Must NOT call while holding mnode->handler->lock.
 * mnode->handler->ops->remove() may sleep and mnode->handler->lock is a
 * spinlock.
 */
static void release_immediate(struct kref *refcount)
{
	struct mmu_rb_node *mnode =
		container_of(refcount, struct mmu_rb_node, refcount);
	trace_hfi1_mmu_release_node(mnode);
	mnode->handler->ops->remove(mnode->handler->ops_arg, mnode);
}

/* Caller must hold mnode->handler->lock */
static void release_nolock(struct kref *refcount)
{
	struct mmu_rb_node *mnode =
		container_of(refcount, struct mmu_rb_node, refcount);
	list_move(&mnode->list, &mnode->handler->del_list);
	queue_work(mnode->handler->wq, &mnode->handler->del_work);
}

/*
 * struct mmu_rb_node->refcount kref_put() callback.
 * Adds mmu_rb_node to mmu_rb_node->handler->del_list and queues
 * handler->del_work on handler->wq.
 * Does not remove mmu_rb_node from handler->lru_list or handler->rb_root.
 * Acquires mmu_rb_node->handler->lock; do not call while already holding
 * handler->lock.
 */
void hfi1_mmu_rb_release(struct kref *refcount)
{
	struct mmu_rb_node *mnode =
		container_of(refcount, struct mmu_rb_node, refcount);
	struct mmu_rb_handler *handler = mnode->handler;
	unsigned long flags;

	spin_lock_irqsave(&handler->lock, flags);
	list_move(&mnode->list, &mnode->handler->del_list);
	spin_unlock_irqrestore(&handler->lock, flags);
	queue_work(handler->wq, &handler->del_work);
}

void hfi1_mmu_rb_evict(struct mmu_rb_handler *handler, void *evict_arg)
{
	struct mmu_rb_node *rbnode, *ptr;
	struct list_head del_list;
	unsigned long flags;
	bool stop = false;

	if (current->mm != handler->mn.mm)
		return;

	INIT_LIST_HEAD(&del_list);

	spin_lock_irqsave(&handler->lock, flags);
	list_for_each_entry_safe(rbnode, ptr, &handler->lru_list, list) {
		/* refcount == 1 implies mmu_rb_handler has only rbnode ref */
		if (kref_read(&rbnode->refcount) > 1)
			continue;

		if (handler->ops->evict(handler->ops_arg, rbnode, evict_arg,
					&stop)) {
			__mmu_int_rb_remove(rbnode, &handler->root);
			/* move from LRU list to delete list */
			list_move(&rbnode->list, &del_list);
		}
		if (stop)
			break;
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	list_for_each_entry_safe(rbnode, ptr, &del_list, list) {
		trace_hfi1_mmu_rb_evict(rbnode);
		kref_put(&rbnode->refcount, release_immediate);
	}
}

static int mmu_notifier_range_start(struct mmu_notifier *mn,
		const struct mmu_notifier_range *range)
{
	struct mmu_rb_handler *handler =
		container_of(mn, struct mmu_rb_handler, mn);
	struct rb_root_cached *root = &handler->root;
	struct mmu_rb_node *node, *ptr = NULL;
	unsigned long flags;

	spin_lock_irqsave(&handler->lock, flags);
	for (node = __mmu_int_rb_iter_first(root, range->start, range->end-1);
	     node; node = ptr) {
		/* Guard against node removal. */
		ptr = __mmu_int_rb_iter_next(node, range->start,
					     range->end - 1);
		trace_hfi1_mmu_mem_invalidate(node);
		/* Remove from rb tree and lru_list. */
		__mmu_int_rb_remove(node, root);
		list_del_init(&node->list);
		kref_put(&node->refcount, release_nolock);
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	return 0;
}

/*
 * Work queue function to remove all nodes that have been queued up to
 * be removed.  The key feature is that mm->mmap_lock is not being held
 * and the remove callback can sleep while taking it, if needed.
 */
static void handle_remove(struct work_struct *work)
{
	struct mmu_rb_handler *handler = container_of(work,
						struct mmu_rb_handler,
						del_work);
	struct list_head del_list;
	unsigned long flags;
	struct mmu_rb_node *node;

	/* remove anything that is queued to get removed */
	spin_lock_irqsave(&handler->lock, flags);
	list_replace_init(&handler->del_list, &del_list);
	spin_unlock_irqrestore(&handler->lock, flags);

	while (!list_empty(&del_list)) {
		node = list_first_entry(&del_list, struct mmu_rb_node, list);
		list_del(&node->list);
		trace_hfi1_mmu_release_node(node);
		handler->ops->remove(handler->ops_arg, node);
	}
}
