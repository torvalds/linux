/*
 * Copyright(c) 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/mmu_notifier.h>
#include <linux/interval_tree_generic.h>

#include "mmu_rb.h"
#include "trace.h"

struct mmu_rb_handler {
	struct mmu_notifier mn;
	struct rb_root root;
	void *ops_arg;
	spinlock_t lock;        /* protect the RB tree */
	struct mmu_rb_ops *ops;
	struct mm_struct *mm;
	struct list_head lru_list;
	struct work_struct del_work;
	struct list_head del_list;
	struct workqueue_struct *wq;
};

static unsigned long mmu_node_start(struct mmu_rb_node *);
static unsigned long mmu_node_last(struct mmu_rb_node *);
static inline void mmu_notifier_page(struct mmu_notifier *, struct mm_struct *,
				     unsigned long);
static inline void mmu_notifier_range_start(struct mmu_notifier *,
					    struct mm_struct *,
					    unsigned long, unsigned long);
static void mmu_notifier_mem_invalidate(struct mmu_notifier *,
					struct mm_struct *,
					unsigned long, unsigned long);
static struct mmu_rb_node *__mmu_rb_search(struct mmu_rb_handler *,
					   unsigned long, unsigned long);
static void do_remove(struct mmu_rb_handler *handler,
		      struct list_head *del_list);
static void handle_remove(struct work_struct *work);

static const struct mmu_notifier_ops mn_opts = {
	.invalidate_page = mmu_notifier_page,
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

int hfi1_mmu_rb_register(void *ops_arg, struct mm_struct *mm,
			 struct mmu_rb_ops *ops,
			 struct workqueue_struct *wq,
			 struct mmu_rb_handler **handler)
{
	struct mmu_rb_handler *handlr;
	int ret;

	handlr = kmalloc(sizeof(*handlr), GFP_KERNEL);
	if (!handlr)
		return -ENOMEM;

	handlr->root = RB_ROOT;
	handlr->ops = ops;
	handlr->ops_arg = ops_arg;
	INIT_HLIST_NODE(&handlr->mn.hlist);
	spin_lock_init(&handlr->lock);
	handlr->mn.ops = &mn_opts;
	handlr->mm = mm;
	INIT_WORK(&handlr->del_work, handle_remove);
	INIT_LIST_HEAD(&handlr->del_list);
	INIT_LIST_HEAD(&handlr->lru_list);
	handlr->wq = wq;

	ret = mmu_notifier_register(&handlr->mn, handlr->mm);
	if (ret) {
		kfree(handlr);
		return ret;
	}

	*handler = handlr;
	return 0;
}

void hfi1_mmu_rb_unregister(struct mmu_rb_handler *handler)
{
	struct mmu_rb_node *rbnode;
	struct rb_node *node;
	unsigned long flags;
	struct list_head del_list;

	/* Unregister first so we don't get any more notifications. */
	mmu_notifier_unregister(&handler->mn, handler->mm);

	/*
	 * Make sure the wq delete handler is finished running.  It will not
	 * be triggered once the mmu notifiers are unregistered above.
	 */
	flush_work(&handler->del_work);

	INIT_LIST_HEAD(&del_list);

	spin_lock_irqsave(&handler->lock, flags);
	while ((node = rb_first(&handler->root))) {
		rbnode = rb_entry(node, struct mmu_rb_node, node);
		rb_erase(node, &handler->root);
		/* move from LRU list to delete list */
		list_move(&rbnode->list, &del_list);
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	do_remove(handler, &del_list);

	kfree(handler);
}

int hfi1_mmu_rb_insert(struct mmu_rb_handler *handler,
		       struct mmu_rb_node *mnode)
{
	struct mmu_rb_node *node;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&handler->lock, flags);
	hfi1_cdbg(MMU, "Inserting node addr 0x%llx, len %u", mnode->addr,
		  mnode->len);
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

	hfi1_cdbg(MMU, "Searching for addr 0x%llx, len %u", addr, len);
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

struct mmu_rb_node *hfi1_mmu_rb_extract(struct mmu_rb_handler *handler,
					unsigned long addr, unsigned long len)
{
	struct mmu_rb_node *node;
	unsigned long flags;

	spin_lock_irqsave(&handler->lock, flags);
	node = __mmu_rb_search(handler, addr, len);
	if (node) {
		__mmu_int_rb_remove(node, &handler->root);
		list_del(&node->list); /* remove from LRU list */
	}
	spin_unlock_irqrestore(&handler->lock, flags);

	return node;
}

void hfi1_mmu_rb_evict(struct mmu_rb_handler *handler, void *evict_arg)
{
	struct mmu_rb_node *rbnode, *ptr;
	struct list_head del_list;
	unsigned long flags;
	bool stop = false;

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

	/* Validity of handler and node pointers has been checked by caller. */
	hfi1_cdbg(MMU, "Removing node addr 0x%llx, len %u", node->addr,
		  node->len);
	spin_lock_irqsave(&handler->lock, flags);
	__mmu_int_rb_remove(node, &handler->root);
	list_del(&node->list); /* remove from LRU list */
	spin_unlock_irqrestore(&handler->lock, flags);

	handler->ops->remove(handler->ops_arg, node);
}

static inline void mmu_notifier_page(struct mmu_notifier *mn,
				     struct mm_struct *mm, unsigned long addr)
{
	mmu_notifier_mem_invalidate(mn, mm, addr, addr + PAGE_SIZE);
}

static inline void mmu_notifier_range_start(struct mmu_notifier *mn,
					    struct mm_struct *mm,
					    unsigned long start,
					    unsigned long end)
{
	mmu_notifier_mem_invalidate(mn, mm, start, end);
}

static void mmu_notifier_mem_invalidate(struct mmu_notifier *mn,
					struct mm_struct *mm,
					unsigned long start, unsigned long end)
{
	struct mmu_rb_handler *handler =
		container_of(mn, struct mmu_rb_handler, mn);
	struct rb_root *root = &handler->root;
	struct mmu_rb_node *node, *ptr = NULL;
	unsigned long flags;
	bool added = false;

	spin_lock_irqsave(&handler->lock, flags);
	for (node = __mmu_int_rb_iter_first(root, start, end - 1);
	     node; node = ptr) {
		/* Guard against node removal. */
		ptr = __mmu_int_rb_iter_next(node, start, end - 1);
		hfi1_cdbg(MMU, "Invalidating node addr 0x%llx, len %u",
			  node->addr, node->len);
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
 * be removed.  The key feature is that mm->mmap_sem is not being held
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
