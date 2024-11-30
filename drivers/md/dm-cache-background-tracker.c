// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-cache-background-tracker.h"

/*----------------------------------------------------------------*/

#define DM_MSG_PREFIX "dm-background-tracker"

struct background_tracker {
	unsigned int max_work;
	atomic_t pending_promotes;
	atomic_t pending_writebacks;
	atomic_t pending_demotes;

	struct list_head issued;
	struct list_head queued;
	struct rb_root pending;
};

struct kmem_cache *btracker_work_cache = NULL;

struct background_tracker *btracker_create(unsigned int max_work)
{
	struct background_tracker *b = kmalloc(sizeof(*b), GFP_KERNEL);

	if (!b) {
		DMERR("couldn't create background_tracker");
		return NULL;
	}

	b->max_work = max_work;
	atomic_set(&b->pending_promotes, 0);
	atomic_set(&b->pending_writebacks, 0);
	atomic_set(&b->pending_demotes, 0);

	INIT_LIST_HEAD(&b->issued);
	INIT_LIST_HEAD(&b->queued);

	b->pending = RB_ROOT;

	return b;
}
EXPORT_SYMBOL_GPL(btracker_create);

void btracker_destroy(struct background_tracker *b)
{
	struct bt_work *w, *tmp;

	BUG_ON(!list_empty(&b->issued));
	list_for_each_entry_safe (w, tmp, &b->queued, list) {
		list_del(&w->list);
		kmem_cache_free(btracker_work_cache, w);
	}

	kfree(b);
}
EXPORT_SYMBOL_GPL(btracker_destroy);

static int cmp_oblock(dm_oblock_t lhs, dm_oblock_t rhs)
{
	if (from_oblock(lhs) < from_oblock(rhs))
		return -1;

	if (from_oblock(rhs) < from_oblock(lhs))
		return 1;

	return 0;
}

static bool __insert_pending(struct background_tracker *b,
			     struct bt_work *nw)
{
	int cmp;
	struct bt_work *w;
	struct rb_node **new = &b->pending.rb_node, *parent = NULL;

	while (*new) {
		w = container_of(*new, struct bt_work, node);

		parent = *new;
		cmp = cmp_oblock(w->work.oblock, nw->work.oblock);
		if (cmp < 0)
			new = &((*new)->rb_left);

		else if (cmp > 0)
			new = &((*new)->rb_right);

		else
			/* already present */
			return false;
	}

	rb_link_node(&nw->node, parent, new);
	rb_insert_color(&nw->node, &b->pending);

	return true;
}

static struct bt_work *__find_pending(struct background_tracker *b,
				      dm_oblock_t oblock)
{
	int cmp;
	struct bt_work *w;
	struct rb_node **new = &b->pending.rb_node;

	while (*new) {
		w = container_of(*new, struct bt_work, node);

		cmp = cmp_oblock(w->work.oblock, oblock);
		if (cmp < 0)
			new = &((*new)->rb_left);

		else if (cmp > 0)
			new = &((*new)->rb_right);

		else
			break;
	}

	return *new ? w : NULL;
}


static void update_stats(struct background_tracker *b, struct policy_work *w, int delta)
{
	switch (w->op) {
	case POLICY_PROMOTE:
		atomic_add(delta, &b->pending_promotes);
		break;

	case POLICY_DEMOTE:
		atomic_add(delta, &b->pending_demotes);
		break;

	case POLICY_WRITEBACK:
		atomic_add(delta, &b->pending_writebacks);
		break;
	}
}

unsigned int btracker_nr_demotions_queued(struct background_tracker *b)
{
	return atomic_read(&b->pending_demotes);
}
EXPORT_SYMBOL_GPL(btracker_nr_demotions_queued);

static bool max_work_reached(struct background_tracker *b)
{
	return atomic_read(&b->pending_promotes) +
		atomic_read(&b->pending_writebacks) +
		atomic_read(&b->pending_demotes) >= b->max_work;
}

static struct bt_work *alloc_work(struct background_tracker *b)
{
	if (max_work_reached(b))
		return NULL;

	return kmem_cache_alloc(btracker_work_cache, GFP_NOWAIT);
}

int btracker_queue(struct background_tracker *b,
		   struct policy_work *work,
		   struct policy_work **pwork)
{
	struct bt_work *w;

	if (pwork)
		*pwork = NULL;

	w = alloc_work(b);
	if (!w)
		return -ENOMEM;

	memcpy(&w->work, work, sizeof(*work));

	if (!__insert_pending(b, w)) {
		/*
		 * There was a race, we'll just ignore this second
		 * bit of work for the same oblock.
		 */
		kmem_cache_free(btracker_work_cache, w);
		return -EINVAL;
	}

	if (pwork) {
		*pwork = &w->work;
		list_add(&w->list, &b->issued);
	} else
		list_add(&w->list, &b->queued);
	update_stats(b, &w->work, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(btracker_queue);

/*
 * Returns -ENODATA if there's no work.
 */
int btracker_issue(struct background_tracker *b, struct policy_work **work)
{
	struct bt_work *w;

	if (list_empty(&b->queued))
		return -ENODATA;

	w = list_first_entry(&b->queued, struct bt_work, list);
	list_move(&w->list, &b->issued);
	*work = &w->work;

	return 0;
}
EXPORT_SYMBOL_GPL(btracker_issue);

void btracker_complete(struct background_tracker *b,
		       struct policy_work *op)
{
	struct bt_work *w = container_of(op, struct bt_work, work);

	update_stats(b, &w->work, -1);
	rb_erase(&w->node, &b->pending);
	list_del(&w->list);
	kmem_cache_free(btracker_work_cache, w);
}
EXPORT_SYMBOL_GPL(btracker_complete);

bool btracker_promotion_already_present(struct background_tracker *b,
					dm_oblock_t oblock)
{
	return __find_pending(b, oblock) != NULL;
}
EXPORT_SYMBOL_GPL(btracker_promotion_already_present);

/*----------------------------------------------------------------*/
