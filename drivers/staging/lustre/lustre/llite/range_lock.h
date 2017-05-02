/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Range lock is used to allow multiple threads writing a single shared
 * file given each thread is writing to a non-overlapping portion of the
 * file.
 *
 * Refer to the possible upstream kernel version of range lock by
 * Jan Kara <jack@suse.cz>: https://lkml.org/lkml/2013/1/31/480
 *
 * This file could later replaced by the upstream kernel version.
 */
/*
 * Author: Prakash Surya <surya1@llnl.gov>
 * Author: Bobi Jam <bobijam.xu@intel.com>
 */
#ifndef _RANGE_LOCK_H
#define _RANGE_LOCK_H

#include "../../include/linux/libcfs/libcfs.h"
#include "../include/interval_tree.h"

struct range_lock {
	struct interval_node	rl_node;
	/**
	 * Process to enqueue this lock.
	 */
	struct task_struct	*rl_task;
	/**
	 * List of locks with the same range.
	 */
	struct list_head	rl_next_lock;
	/**
	 * Number of locks in the list rl_next_lock
	 */
	unsigned int		rl_lock_count;
	/**
	 * Number of ranges which are blocking acquisition of the lock
	 */
	unsigned int		rl_blocking_ranges;
	/**
	 * Sequence number of range lock. This number is used to get to know
	 * the order the locks are queued; this is required for range_cancel().
	 */
	__u64			rl_sequence;
};

static inline struct range_lock *node2rangelock(const struct interval_node *n)
{
	return container_of(n, struct range_lock, rl_node);
}

struct range_lock_tree {
	struct interval_node	*rlt_root;
	spinlock_t		 rlt_lock;	/* protect range lock tree */
	__u64			 rlt_sequence;
};

void range_lock_tree_init(struct range_lock_tree *tree);
int range_lock_init(struct range_lock *lock, __u64 start, __u64 end);
int  range_lock(struct range_lock_tree *tree, struct range_lock *lock);
void range_unlock(struct range_lock_tree *tree, struct range_lock *lock);
#endif
