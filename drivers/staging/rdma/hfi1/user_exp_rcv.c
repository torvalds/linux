/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
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
#include <asm/page.h>

#include "user_exp_rcv.h"
#include "trace.h"

struct tid_group {
	struct list_head list;
	unsigned base;
	u8 size;
	u8 used;
	u8 map;
};

struct mmu_rb_node {
	struct rb_node rbnode;
	unsigned long virt;
	unsigned long phys;
	unsigned long len;
	struct tid_group *grp;
	u32 rcventry;
	dma_addr_t dma_addr;
	bool freed;
	unsigned npages;
	struct page *pages[0];
};

enum mmu_call_types {
	MMU_INVALIDATE_PAGE = 0,
	MMU_INVALIDATE_RANGE = 1
};

static const char * const mmu_types[] = {
	"PAGE",
	"RANGE"
};

#define EXP_TID_SET_EMPTY(set) (set.count == 0 && list_empty(&set.list))

static inline int mmu_addr_cmp(struct mmu_rb_node *, unsigned long,
			       unsigned long);
static struct mmu_rb_node *mmu_rb_search_by_addr(struct rb_root *,
						 unsigned long) __maybe_unused;
static inline struct mmu_rb_node *mmu_rb_search_by_entry(struct rb_root *,
							 u32);
static int mmu_rb_insert_by_addr(struct rb_root *,
				 struct mmu_rb_node *) __maybe_unused;
static int mmu_rb_insert_by_entry(struct rb_root *,
				  struct mmu_rb_node *) __maybe_unused;
static void mmu_notifier_mem_invalidate(struct mmu_notifier *,
					unsigned long, unsigned long,
					enum mmu_call_types);
static inline void mmu_notifier_page(struct mmu_notifier *, struct mm_struct *,
				     unsigned long);
static inline void mmu_notifier_range_start(struct mmu_notifier *,
					    struct mm_struct *,
					    unsigned long, unsigned long);

static inline void exp_tid_group_init(struct exp_tid_set *set)
{
	INIT_LIST_HEAD(&set->list);
	set->count = 0;
}

static inline void tid_group_remove(struct tid_group *grp,
				    struct exp_tid_set *set)
{
	list_del_init(&grp->list);
	set->count--;
}

static inline void tid_group_add_tail(struct tid_group *grp,
				      struct exp_tid_set *set)
{
	list_add_tail(&grp->list, &set->list);
	set->count++;
}

static inline struct tid_group *tid_group_pop(struct exp_tid_set *set)
{
	struct tid_group *grp =
		list_first_entry(&set->list, struct tid_group, list);
	list_del_init(&grp->list);
	set->count--;
	return grp;
}

static inline void tid_group_move(struct tid_group *group,
				  struct exp_tid_set *s1,
				  struct exp_tid_set *s2)
{
	tid_group_remove(group, s1);
	tid_group_add_tail(group, s2);
}

static struct mmu_notifier_ops __maybe_unused mn_opts = {
	.invalidate_page = mmu_notifier_page,
	.invalidate_range_start = mmu_notifier_range_start,
};

/*
 * Initialize context and file private data needed for Expected
 * receive caching. This needs to be done after the context has
 * been configured with the eager/expected RcvEntry counts.
 */
int hfi1_user_exp_rcv_init(struct file *fp)
{
	return -EINVAL;
}

int hfi1_user_exp_rcv_free(struct hfi1_filedata *fd)
{
	return -EINVAL;
}

/*
 * Write an "empty" RcvArray entry.
 * This function exists so the TID registaration code can use it
 * to write to unused/unneeded entries and still take advantage
 * of the WC performance improvements. The HFI will ignore this
 * write to the RcvArray entry.
 */
static inline void rcv_array_wc_fill(struct hfi1_devdata *dd, u32 index)
{
	/*
	 * Doing the WC fill writes only makes sense if the device is
	 * present and the RcvArray has been mapped as WC memory.
	 */
	if ((dd->flags & HFI1_PRESENT) && dd->rcvarray_wc)
		writeq(0, dd->rcvarray_wc + (index * 8));
}

int hfi1_user_exp_rcv_setup(struct file *fp, struct hfi1_tid_info *tinfo)
{
	return -EINVAL;
}

int hfi1_user_exp_rcv_clear(struct file *fp, struct hfi1_tid_info *tinfo)
{
	return -EINVAL;
}

int hfi1_user_exp_rcv_invalid(struct file *fp, struct hfi1_tid_info *tinfo)
{
	return -EINVAL;
}

static inline void mmu_notifier_page(struct mmu_notifier *mn,
				     struct mm_struct *mm, unsigned long addr)
{
	mmu_notifier_mem_invalidate(mn, addr, addr + PAGE_SIZE,
				    MMU_INVALIDATE_PAGE);
}

static inline void mmu_notifier_range_start(struct mmu_notifier *mn,
					    struct mm_struct *mm,
					    unsigned long start,
					    unsigned long end)
{
	mmu_notifier_mem_invalidate(mn, start, end, MMU_INVALIDATE_RANGE);
}

static void mmu_notifier_mem_invalidate(struct mmu_notifier *mn,
					unsigned long start, unsigned long end,
					enum mmu_call_types type)
{
	/* Stub for now */
}

static inline int mmu_addr_cmp(struct mmu_rb_node *node, unsigned long addr,
			       unsigned long len)
{
	if ((addr + len) <= node->virt)
		return -1;
	else if (addr >= node->virt && addr < (node->virt + node->len))
		return 0;
	else
		return 1;
}

static inline int mmu_entry_cmp(struct mmu_rb_node *node, u32 entry)
{
	if (entry < node->rcventry)
		return -1;
	else if (entry > node->rcventry)
		return 1;
	else
		return 0;
}

static struct mmu_rb_node *mmu_rb_search_by_addr(struct rb_root *root,
						 unsigned long addr)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct mmu_rb_node *mnode =
			container_of(node, struct mmu_rb_node, rbnode);
		/*
		 * When searching, use at least one page length for size. The
		 * MMU notifier will not give us anything less than that. We
		 * also don't need anything more than a page because we are
		 * guaranteed to have non-overlapping buffers in the tree.
		 */
		int result = mmu_addr_cmp(mnode, addr, PAGE_SIZE);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return mnode;
	}
	return NULL;
}

static inline struct mmu_rb_node *mmu_rb_search_by_entry(struct rb_root *root,
							 u32 index)
{
	struct mmu_rb_node *rbnode;
	struct rb_node *node;

	if (root && !RB_EMPTY_ROOT(root))
		for (node = rb_first(root); node; node = rb_next(node)) {
			rbnode = rb_entry(node, struct mmu_rb_node, rbnode);
			if (rbnode->rcventry == index)
				return rbnode;
		}
	return NULL;
}

static int mmu_rb_insert_by_entry(struct rb_root *root,
				  struct mmu_rb_node *node)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;

	while (*new) {
		struct mmu_rb_node *this =
			container_of(*new, struct mmu_rb_node, rbnode);
		int result = mmu_entry_cmp(this, node->rcventry);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return 1;
	}

	rb_link_node(&node->rbnode, parent, new);
	rb_insert_color(&node->rbnode, root);
	return 0;
}

static int mmu_rb_insert_by_addr(struct rb_root *root, struct mmu_rb_node *node)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct mmu_rb_node *this =
			container_of(*new, struct mmu_rb_node, rbnode);
		int result = mmu_addr_cmp(this, node->virt, node->len);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return 1;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&node->rbnode, parent, new);
	rb_insert_color(&node->rbnode, root);

	return 0;
}
