/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#ifndef DM_CACHE_BACKGROUND_WORK_H
#define DM_CACHE_BACKGROUND_WORK_H

#include <linux/vmalloc.h>
#include "dm-cache-policy.h"

/*----------------------------------------------------------------*/

/*
 * The cache policy decides what background work should be performed,
 * such as promotions, demotions and writebacks. The core cache target
 * is in charge of performing the work, and does so when it sees fit.
 *
 * The background_tracker acts as a go between. Keeping track of future
 * work that the policy has decided upon, and handing (issuing) it to
 * the core target when requested.
 *
 * There is no locking in this, so calls will probably need to be
 * protected with a spinlock.
 */

struct bt_work {
	struct list_head list;
	struct rb_node node;
	struct policy_work work;
};

extern struct kmem_cache *btracker_work_cache;

struct background_work;
struct background_tracker;

/*
 * Create a new tracker, it will not be able to queue more than
 * 'max_work' entries.
 */
struct background_tracker *btracker_create(unsigned int max_work);

/*
 * Destroy the tracker. No issued, but not complete, work should
 * exist when this is called. It is fine to have queued but unissued
 * work.
 */
void btracker_destroy(struct background_tracker *b);

unsigned int btracker_nr_writebacks_queued(struct background_tracker *b);
unsigned int btracker_nr_demotions_queued(struct background_tracker *b);

/*
 * Queue some work within the tracker. 'work' should point to the work
 * to queue, this will be copied (ownership doesn't pass).  If pwork
 * is not NULL then it will be set to point to the tracker's internal
 * copy of the work.
 *
 * returns -EINVAL iff the work is already queued.  -ENOMEM if the work
 * couldn't be queued for another reason.
 */
int btracker_queue(struct background_tracker *b,
		   struct policy_work *work,
		   struct policy_work **pwork);

/*
 * Hands out the next piece of work to be performed.
 * Returns -ENODATA if there's no work.
 */
int btracker_issue(struct background_tracker *b, struct policy_work **work);

/*
 * Informs the tracker that the work has been completed and it may forget
 * about it.
 */
void btracker_complete(struct background_tracker *b, struct policy_work *op);

/*
 * Predicate to see if an origin block is already scheduled for promotion.
 */
bool btracker_promotion_already_present(struct background_tracker *b,
					dm_oblock_t oblock);

/*----------------------------------------------------------------*/

#endif
