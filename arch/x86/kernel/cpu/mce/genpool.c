// SPDX-License-Identifier: GPL-2.0-only
/*
 * MCE event pool management in MCE context
 *
 * Copyright (C) 2015 Intel Corp.
 * Author: Chen, Gong <gong.chen@linux.intel.com>
 */
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/genalloc.h>
#include <linux/llist.h>
#include "internal.h"

/*
 * printk() is not safe in MCE context. This is a lock-less memory allocator
 * used to save error information organized in a lock-less list.
 *
 * This memory pool is only to be used to save MCE records in MCE context.
 * MCE events are rare, so a fixed size memory pool should be enough.
 * Allocate on a sliding scale based on number of CPUs.
 */
#define MCE_MIN_ENTRIES	80
#define MCE_PER_CPU	2

static struct gen_pool *mce_evt_pool;
static LLIST_HEAD(mce_event_llist);

/*
 * Compare the record "t" with each of the records on list "l" to see if
 * an equivalent one is present in the list.
 */
static bool is_duplicate_mce_record(struct mce_evt_llist *t, struct mce_evt_llist *l)
{
	struct mce_hw_err *err1, *err2;
	struct mce_evt_llist *node;

	err1 = &t->err;

	llist_for_each_entry(node, &l->llnode, llnode) {
		err2 = &node->err;

		if (!mce_cmp(&err1->m, &err2->m))
			return true;
	}
	return false;
}

/*
 * The system has panicked - we'd like to peruse the list of MCE records
 * that have been queued, but not seen by anyone yet.  The list is in
 * reverse time order, so we need to reverse it. While doing that we can
 * also drop duplicate records (these were logged because some banks are
 * shared between cores or by all threads on a socket).
 */
struct llist_node *mce_gen_pool_prepare_records(void)
{
	struct llist_node *head;
	LLIST_HEAD(new_head);
	struct mce_evt_llist *node, *t;

	head = llist_del_all(&mce_event_llist);
	if (!head)
		return NULL;

	/* squeeze out duplicates while reversing order */
	llist_for_each_entry_safe(node, t, head, llnode) {
		if (!is_duplicate_mce_record(node, t))
			llist_add(&node->llnode, &new_head);
	}

	return new_head.first;
}

void mce_gen_pool_process(struct work_struct *__unused)
{
	struct mce_evt_llist *node, *tmp;
	struct llist_node *head;
	struct mce *mce;

	head = llist_del_all(&mce_event_llist);
	if (!head)
		return;

	head = llist_reverse_order(head);
	llist_for_each_entry_safe(node, tmp, head, llnode) {
		mce = &node->err.m;
		blocking_notifier_call_chain(&x86_mce_decoder_chain, 0, mce);
		gen_pool_free(mce_evt_pool, (unsigned long)node, sizeof(*node));
	}
}

bool mce_gen_pool_empty(void)
{
	return llist_empty(&mce_event_llist);
}

int mce_gen_pool_add(struct mce_hw_err *err)
{
	struct mce_evt_llist *node;

	if (filter_mce(&err->m))
		return -EINVAL;

	if (!mce_evt_pool)
		return -EINVAL;

	node = (void *)gen_pool_alloc(mce_evt_pool, sizeof(*node));
	if (!node) {
		pr_warn_ratelimited("MCE records pool full!\n");
		return -ENOMEM;
	}

	memcpy(&node->err, err, sizeof(*err));
	llist_add(&node->llnode, &mce_event_llist);

	return 0;
}

static int mce_gen_pool_create(void)
{
	int mce_numrecords, mce_poolsz, order;
	struct gen_pool *gpool;
	int ret = -ENOMEM;
	void *mce_pool;

	order = order_base_2(sizeof(struct mce_evt_llist));
	gpool = gen_pool_create(order, -1);
	if (!gpool)
		return ret;

	mce_numrecords = max(MCE_MIN_ENTRIES, num_possible_cpus() * MCE_PER_CPU);
	mce_poolsz = mce_numrecords * (1 << order);
	mce_pool = kmalloc(mce_poolsz, GFP_KERNEL);
	if (!mce_pool) {
		gen_pool_destroy(gpool);
		return ret;
	}
	ret = gen_pool_add(gpool, (unsigned long)mce_pool, mce_poolsz, -1);
	if (ret) {
		gen_pool_destroy(gpool);
		kfree(mce_pool);
		return ret;
	}

	mce_evt_pool = gpool;

	return ret;
}

int mce_gen_pool_init(void)
{
	/* Just init mce_gen_pool once. */
	if (mce_evt_pool)
		return 0;

	return mce_gen_pool_create();
}
