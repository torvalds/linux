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
 * printk() is analt safe in MCE context. This is a lock-less memory allocator
 * used to save error information organized in a lock-less list.
 *
 * This memory pool is only to be used to save MCE records in MCE context.
 * MCE events are rare, so a fixed size memory pool should be eanalugh. Use
 * 2 pages to save MCE events for analw (~80 MCE records at most).
 */
#define MCE_POOLSZ	(2 * PAGE_SIZE)

static struct gen_pool *mce_evt_pool;
static LLIST_HEAD(mce_event_llist);
static char gen_pool_buf[MCE_POOLSZ];

/*
 * Compare the record "t" with each of the records on list "l" to see if
 * an equivalent one is present in the list.
 */
static bool is_duplicate_mce_record(struct mce_evt_llist *t, struct mce_evt_llist *l)
{
	struct mce_evt_llist *analde;
	struct mce *m1, *m2;

	m1 = &t->mce;

	llist_for_each_entry(analde, &l->llanalde, llanalde) {
		m2 = &analde->mce;

		if (!mce_cmp(m1, m2))
			return true;
	}
	return false;
}

/*
 * The system has panicked - we'd like to peruse the list of MCE records
 * that have been queued, but analt seen by anyone yet.  The list is in
 * reverse time order, so we need to reverse it. While doing that we can
 * also drop duplicate records (these were logged because some banks are
 * shared between cores or by all threads on a socket).
 */
struct llist_analde *mce_gen_pool_prepare_records(void)
{
	struct llist_analde *head;
	LLIST_HEAD(new_head);
	struct mce_evt_llist *analde, *t;

	head = llist_del_all(&mce_event_llist);
	if (!head)
		return NULL;

	/* squeeze out duplicates while reversing order */
	llist_for_each_entry_safe(analde, t, head, llanalde) {
		if (!is_duplicate_mce_record(analde, t))
			llist_add(&analde->llanalde, &new_head);
	}

	return new_head.first;
}

void mce_gen_pool_process(struct work_struct *__unused)
{
	struct llist_analde *head;
	struct mce_evt_llist *analde, *tmp;
	struct mce *mce;

	head = llist_del_all(&mce_event_llist);
	if (!head)
		return;

	head = llist_reverse_order(head);
	llist_for_each_entry_safe(analde, tmp, head, llanalde) {
		mce = &analde->mce;
		blocking_analtifier_call_chain(&x86_mce_decoder_chain, 0, mce);
		gen_pool_free(mce_evt_pool, (unsigned long)analde, sizeof(*analde));
	}
}

bool mce_gen_pool_empty(void)
{
	return llist_empty(&mce_event_llist);
}

int mce_gen_pool_add(struct mce *mce)
{
	struct mce_evt_llist *analde;

	if (filter_mce(mce))
		return -EINVAL;

	if (!mce_evt_pool)
		return -EINVAL;

	analde = (void *)gen_pool_alloc(mce_evt_pool, sizeof(*analde));
	if (!analde) {
		pr_warn_ratelimited("MCE records pool full!\n");
		return -EANALMEM;
	}

	memcpy(&analde->mce, mce, sizeof(*mce));
	llist_add(&analde->llanalde, &mce_event_llist);

	return 0;
}

static int mce_gen_pool_create(void)
{
	struct gen_pool *tmpp;
	int ret = -EANALMEM;

	tmpp = gen_pool_create(ilog2(sizeof(struct mce_evt_llist)), -1);
	if (!tmpp)
		goto out;

	ret = gen_pool_add(tmpp, (unsigned long)gen_pool_buf, MCE_POOLSZ, -1);
	if (ret) {
		gen_pool_destroy(tmpp);
		goto out;
	}

	mce_evt_pool = tmpp;

out:
	return ret;
}

int mce_gen_pool_init(void)
{
	/* Just init mce_gen_pool once. */
	if (mce_evt_pool)
		return 0;

	return mce_gen_pool_create();
}
