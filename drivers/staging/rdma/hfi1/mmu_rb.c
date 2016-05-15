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
#include <linux/mmu_notifier.h>
#include <linux/interval_tree_generic.h>

#include "mmu_rb.h"
#include "trace.h"

struct mmu_rb_handler {
	struct list_head list;
	struct mmu_notifier mn;
	struct rb_root *root;
	spinlock_t lock;        /* protect the RB tree */
	struct mmu_rb_ops *ops;
};

static LIST_HEAD(mmu_rb_handlers);
static DEFINE_SPINLOCK(mmu_rb_lock); /* protect mmu_rb_handlers list */

static unsigned long mmu_node_start(struct mmu_rb_node *);
static unsigned long mmu_node_last(struct mmu_rb_node *);
static struct mmu_rb_handler *find_mmu_handler(struct rb_root *);
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

static struct mmu_notifier_ops mn_opts = {
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
	return PAGE_ALIGN((node->addr & PAGE_MASK) + node->len) - 1;
}

int hfi1_mmu_rb_register(struct rb_root *root, struct mmu_rb_ops *ops)
{
	struct mmu_rb_handler *handlr;
	unsigned long flags;

	if (!ops->invalidate)
		return -EINVAL;

	handlr = kmalloc(sizeof(*handlr), GFP_KERNEL);
	if (!handlr)
		return -ENOMEM;

	handlr->root = root;
	handlr->ops = ops;
	INIT_HLIST_NODE(&handlr->mn.hlist);
	spin_lock_init(&handlr->lock);
	handlr->mn.ops = &mn_opts;
	spin_lock_irqsave(&mmu_rb_lock, flags);
	list_add_tail(&handlr->list, &mmu_rb_handlers);
	spin_unlock_irqrestore(&mmu_rb_lock, flags);

	return mmu_notifier_register(&handlr->mn, current->mm);
}

void hfi1_mmu_rb_unregister(struct rb_root *root)
{
	struct mmu_rb_handler *handler = find_mmu_handler(root);
	unsigned long flags;

	if (!handler)
		return;

	spin_lock_irqsave(&mmu_rb_lock, flags);
	list_del(&handler->list);
	spin_unlock_irqrestore(&mmu_rb_lock, flags);

	if (!RB_EMPTY_ROOT(root)) {
		struct rb_node *node;
		struct mmu_rb_node *rbnode;

		while ((node = rb_first(root))) {
			rbnode = rb_entry(node, struct mmu_rb_node, node);
			rb_erase(node, root);
			if (handler->ops->remove)
				handler->ops->remove(root, rbnode, NULL);
		}
	}

	if (current->mm)
		mmu_notifier_unregister(&handler->mn, current->mm);
	kfree(handler);
}

int hfi1_mmu_rb_insert(struct rb_root *root, struct mmu_rb_node *mnode)
{
	struct mmu_rb_handler *handler = find_mmu_handler(root);
	struct mmu_rb_node *node;
	unsigned long flags;
	int ret = 0;

	if (!handler)
		return -EINVAL;

	spin_lock_irqsave(&handler->lock, flags);
	hfi1_cdbg(MMU, "Inserting node addr 0x%llx, len %u", mnode->addr,
		  mnode->len);
	node = __mmu_rb_search(handler, mnode->addr, mnode->len);
	if (node) {
		ret = -EINVAL;
		goto unlock;
	}
	__mmu_int_rb_insert(mnode, root);

	if (handler->ops->insert) {
		ret = handler->ops->insert(root, mnode);
		if (ret)
			__mmu_int_rb_remove(mnode, root);
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
		node = __mmu_int_rb_iter_first(handler->root, addr,
					       (addr + len) - 1);
	} else {
		for (node = __mmu_int_rb_iter_first(handler->root, addr,
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

/* Caller must *not* hold handler lock. */
static void __mmu_rb_remove(struct mmu_rb_handler *handler,
			    struct mmu_rb_node *node, struct mm_struct *mm)
{
	unsigned long flags;

	/* Validity of handler and node pointers has been checked by caller. */
	hfi1_cdbg(MMU, "Removing node addr 0x%llx, len %u", node->addr,
		  node->len);
	spin_lock_irqsave(&handler->lock, flags);
	__mmu_int_rb_remove(node, handler->root);
	spin_unlock_irqrestore(&handler->lock, flags);

	if (handler->ops->remove)
		handler->ops->remove(handler->root, node, mm);
}

struct mmu_rb_node *hfi1_mmu_rb_search(struct rb_root *root, unsigned long addr,
				       unsigned long len)
{
	struct mmu_rb_handler *handler = find_mmu_handler(root);
	struct mmu_rb_node *node;
	unsigned long flags;

	if (!handler)
		return ERR_PTR(-EINVAL);

	spin_lock_irqsave(&handler->lock, flags);
	node = __mmu_rb_search(handler, addr, len);
	spin_unlock_irqrestore(&handler->lock, flags);

	return node;
}

void hfi1_mmu_rb_remove(struct rb_root *root, struct mmu_rb_node *node)
{
	struct mmu_rb_handler *handler = find_mmu_handler(root);

	if (!handler || !node)
		return;

	__mmu_rb_remove(handler, node, NULL);
}

static struct mmu_rb_handler *find_mmu_handler(struct rb_root *root)
{
	struct mmu_rb_handler *handler;
	unsigned long flags;

	spin_lock_irqsave(&mmu_rb_lock, flags);
	list_for_each_entry(handler, &mmu_rb_handlers, list) {
		if (handler->root == root)
			goto unlock;
	}
	handler = NULL;
unlock:
	spin_unlock_irqrestore(&mmu_rb_lock, flags);
	return handler;
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
	struct rb_root *root = handler->root;
	struct mmu_rb_node *node, *ptr = NULL;
	unsigned long flags;

	spin_lock_irqsave(&handler->lock, flags);
	for (node = __mmu_int_rb_iter_first(root, start, end - 1);
	     node; node = ptr) {
		/* Guard against node removal. */
		ptr = __mmu_int_rb_iter_next(node, start, end - 1);
		hfi1_cdbg(MMU, "Invalidating node addr 0x%llx, len %u",
			  node->addr, node->len);
		if (handler->ops->invalidate(root, node)) {
			spin_unlock_irqrestore(&handler->lock, flags);
			__mmu_rb_remove(handler, node, mm);
			spin_lock_irqsave(&handler->lock, flags);
		}
	}
	spin_unlock_irqrestore(&handler->lock, flags);
}
