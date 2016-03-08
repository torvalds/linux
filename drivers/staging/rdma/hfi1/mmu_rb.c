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
#include <linux/rbtree.h>

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

static struct mmu_rb_handler *find_mmu_handler(struct rb_root *);
static inline void mmu_notifier_page(struct mmu_notifier *, struct mm_struct *,
				     unsigned long);
static inline void mmu_notifier_range_start(struct mmu_notifier *,
					    struct mm_struct *,
					    unsigned long, unsigned long);
static void mmu_notifier_mem_invalidate(struct mmu_notifier *,
					unsigned long, unsigned long);
static struct mmu_rb_node *__mmu_rb_search(struct mmu_rb_handler *,
					   unsigned long, unsigned long);

static struct mmu_notifier_ops mn_opts = {
	.invalidate_page = mmu_notifier_page,
	.invalidate_range_start = mmu_notifier_range_start,
};

int hfi1_mmu_rb_register(struct rb_root *root, struct mmu_rb_ops *ops)
{
	struct mmu_rb_handler *handlr;
	unsigned long flags;

	if (!ops->compare || !ops->invalidate)
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
				handler->ops->remove(root, rbnode);
		}
	}

	if (current->mm)
		mmu_notifier_unregister(&handler->mn, current->mm);
	kfree(handler);
}

int hfi1_mmu_rb_insert(struct rb_root *root, struct mmu_rb_node *mnode)
{
	struct rb_node **new, *parent = NULL;
	struct mmu_rb_handler *handler = find_mmu_handler(root);
	struct mmu_rb_node *this;
	unsigned long flags;
	int res, ret = 0;

	if (!handler)
		return -EINVAL;

	new = &handler->root->rb_node;
	spin_lock_irqsave(&handler->lock, flags);
	while (*new) {
		this = container_of(*new, struct mmu_rb_node, node);
		res = handler->ops->compare(this, mnode->addr, mnode->len);
		parent = *new;

		if (res < 0) {
			new = &((*new)->rb_left);
		} else if (res > 0) {
			new = &((*new)->rb_right);
		} else {
			ret = 1;
			goto unlock;
		}
	}

	if (handler->ops->insert) {
		ret = handler->ops->insert(root, mnode);
		if (ret)
			goto unlock;
	}

	rb_link_node(&mnode->node, parent, new);
	rb_insert_color(&mnode->node, root);
unlock:
	spin_unlock_irqrestore(&handler->lock, flags);
	return ret;
}

/* Caller must host handler lock */
static struct mmu_rb_node *__mmu_rb_search(struct mmu_rb_handler *handler,
					   unsigned long addr,
					   unsigned long len)
{
	struct rb_node *node = handler->root->rb_node;
	struct mmu_rb_node *mnode;
	int res;

	while (node) {
		mnode = container_of(node, struct mmu_rb_node, node);
		res = handler->ops->compare(mnode, addr, len);

		if (res < 0)
			node = node->rb_left;
		else if (res > 0)
			node = node->rb_right;
		else
			return mnode;
	}
	return NULL;
}

static void __mmu_rb_remove(struct mmu_rb_handler *handler,
			    struct mmu_rb_node *node)
{
	/* Validity of handler and node pointers has been checked by caller. */
	rb_erase(&node->node, handler->root);
	if (handler->ops->remove)
		handler->ops->remove(handler->root, node);
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
	unsigned long flags;

	if (!handler || !node)
		return;

	spin_lock_irqsave(&handler->lock, flags);
	__mmu_rb_remove(handler, node);
	spin_unlock_irqrestore(&handler->lock, flags);
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
	mmu_notifier_mem_invalidate(mn, addr, addr + PAGE_SIZE);
}

static inline void mmu_notifier_range_start(struct mmu_notifier *mn,
					    struct mm_struct *mm,
					    unsigned long start,
					    unsigned long end)
{
	mmu_notifier_mem_invalidate(mn, start, end);
}

static void mmu_notifier_mem_invalidate(struct mmu_notifier *mn,
					unsigned long start, unsigned long end)
{
	struct mmu_rb_handler *handler =
		container_of(mn, struct mmu_rb_handler, mn);
	struct rb_root *root = handler->root;
	struct mmu_rb_node *node;
	unsigned long addr = start, naddr, nlen, flags;

	spin_lock_irqsave(&handler->lock, flags);
	while (addr < end) {
		/*
		 * There is no good way to provide a reasonable length to the
		 * search function at this point. Using the remaining length in
		 * the invalidation range is not the right thing to do.
		 * We have to rely on the fact that the insertion algorithm
		 * takes care of any overlap or length restrictions by using the
		 * actual size of each node. Therefore, we can use a page as an
		 * arbitrary, non-zero value.
		 */
		node = __mmu_rb_search(handler, addr, PAGE_SIZE);

		if (!node) {
			/*
			 * Didn't find a node at this address. However, the
			 * range could be bigger than what we have registered
			 * so we have to keep looking.
			 */
			addr += PAGE_SIZE;
			continue;
		}

		naddr = node->addr;
		nlen = node->len;
		if (handler->ops->invalidate(root, node))
			__mmu_rb_remove(handler, node);

		/*
		 * The next address to be looked up is computed based
		 * on the node's starting address. This is due to the
		 * fact that the range where we start might be in the
		 * middle of the node's buffer so simply incrementing
		 * the address by the node's size would result is a
		 * bad address.
		 */
		addr = naddr + nlen;
	}
	spin_unlock_irqrestore(&handler->lock, flags);
}
