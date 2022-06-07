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
static void do_remove(struct mmu_rb_handler *handler,
		      struct list_head *del_list);
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
	int ret;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

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

	ret = mmu_notifier_register(&h->mn, current->mm);
	if (ret) {
		kfree(h);
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

	do_remove(handler, &del_list);

	/* Now the mm may be freed. */
	mmdrop(handler->mn.mm);

	kfree(handler);
}

int hfi1_mmu_rb_insert(struct mmu_rb_handler *handler,
		       struct mmu_rb_node *mnode)
{
	struct mmu_rb_node *node;
	unsigned long flags;
	int ret = 0;

	trace_hfi1_mmu_rb_insert(mnode->addr, mnode->len);

	if (current->mm != handler->mn.mm)
		return -EPERM;

	spin_lock_irqsave(&handler->lock, flags);
	node = __mmu_rb_search(handler, mnode->addr, mnode->len);
	if (node) {
		ret = -EINVAL;
		goto unlock;
	}
	__mmu_int_rb_insert(mnode, &handler->root);
	list_add(&mnode->list, &handler->lru_list);

	ret = handler->ops->insert(handler->ops_arg, mnode);
	if (ret) {
		__mmu_int_rb_remove(mnode, &handler->root);
		list_del(&mnode->list); /* remove from LRU list */
	}
	mnode->handler = handler;
unlock:
	spin_unlock_irqrestore(&handler->lock, flags);
	return ret;
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

bool hfi1_mmu_rb_remove_unless_exact(struct mmu_rb_handler *handler,
				     unsigned long addr, unsigned long len,
				     struct mmu_rb_node **rb_node)
{
	struct mmu_rb_node *node;
	unsigned long flags;
	bool ret = false;

	if (current->mm != handler->mn.mm)
		return ret;

	spin_lock_irqsave(&handler->lock, flags);
	node = __mmu_rb_search(handler, addr, len);
	if (node) {
		if (node->addr == addr && node->len == len)
			goto unlock;
		__mmu_int_rb_remove(node, &handler->root);
		list_del(&node->list); /* remove from LRU list */
		ret = true;
	}
unlock:
	spin_unlock_irqrestore(&handler->lock, flags);
	*rb_node = node;
	return ret;
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
	list_for_each_entry_safe_reverse(rbnode, ptr, &handler->lru_list,
					 list) {
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

	while (!list_empty(&del_list)) {
		rbnode = list_first_entry(&del_list, struct mmu_rb_node, list);
		list_del(&rbnode->list);
		handler->ops->remove(handler->ops_arg, rbnode);
	}
}

/*
 * It is up to the caller to ensure that this function does not race with the
 * mmu invalidate notifier which may be calling the users remove callback on
 * 'node'.
 */
void hfi1_mmu_rb_remove(struct mmu_rb_handler *handler,
			struct mmu_rb_node *node)
{
	unsigned long flags;

	if (current->mm != handler->mn.mm)
		return;

	/* Validity of handler and node pointers has been checked by caller. */
	trace_hfi1_mmu_rb_remove(node->addr, node->len);
	spin_lock_irqsave(&handler->lock, flags);
	__mmu_int_rb_remove(node, &handler->root);
	list_del(&node->list); /* remove from LRU list */
	spin_unlock_irqrestore(&handler->lock, flags);

	handler->ops->remove(handler->ops_arg, node);
}

static int mmu_notifier_range_start(struct mmu_notifier *mn,
		const struct mmu_notifier_range *range)
{
	struct mmu_rb_handler *handler =
		container_of(mn, struct mmu_rb_handler, mn);
	struct rb_root_cached *root = &handler->root;
	struct mmu_rb_node *node, *ptr = NULL;
	unsigned long flags;
	bool added = false;

	spin_lock_irqsave(&handler->lock, flags);
	for (node = __mmu_int_rb_iter_first(root, range->start, range->end-1);
	     node; node = ptr) {
		/* Guard against node removal. */
		ptr = __mmu_int_rb_iter_next(node, range->start,
					     range->end - 1);
		trace_hfi1_mmu_mem_invalidate(node->addr, node->len);
		if (handler->ops->invalidate(handler->ops_arg, node)) {
			__mmu_int_rb_remove(node, root);
			/* move from LRU list to delete list */
			list_move(&node->list, &handler->del_list);
			added = true;
		}
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	if (added)
		queue_work(handler->wq, &handler->del_work);

	return 0;
}

/*
 * Call the remove function for the given handler and the list.  This
 * is expected to be called with a delete list extracted from handler.
 * The caller should not be holding the handler lock.
 */
static void do_remove(struct mmu_rb_handler *handler,
		      struct list_head *del_list)
{
	struct mmu_rb_node *node;

	while (!list_empty(del_list)) {
		node = list_first_entry(del_list, struct mmu_rb_node, list);
		list_del(&node->list);
		handler->ops->remove(handler->ops_arg, node);
	}
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

	/* remove anything that is queued to get removed */
	spin_lock_irqsave(&handler->lock, flags);
	list_replace_init(&handler->del_list, &del_list);
	spin_unlock_irqrestore(&handler->lock, flags);

	do_remove(handler, &del_list);
}
