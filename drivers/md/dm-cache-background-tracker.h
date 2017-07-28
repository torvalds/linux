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

struct background_work;
struct background_tracker;

/*
 * FIXME: discuss lack of locking in all methods.
 */
struct background_tracker *btracker_create(unsigned max_work);
void btracker_destroy(struct background_tracker *b);

unsigned btracker_nr_writebacks_queued(struct background_tracker *b);
unsigned btracker_nr_demotions_queued(struct background_tracker *b);

/*
 * returns -EINVAL iff the work is already queued.  -ENOMEM if the work
 * couldn't be queued for another reason.
 */
int btracker_queue(struct background_tracker *b,
		   struct policy_work *work,
		   struct policy_work **pwork);

/*
 * Returns -ENODATA if there's no work.
 */
int btracker_issue(struct background_tracker *b, struct policy_work **work);
void btracker_complete(struct background_tracker *b,
		       struct policy_work *op);
bool btracker_promotion_already_present(struct background_tracker *b,
					dm_oblock_t oblock);

/*----------------------------------------------------------------*/

#endif
