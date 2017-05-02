/*
 * Internal header file _only_ for device mapper core
 *
 * Copyright (C) 2016 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the LGPL.
 */

#ifndef DM_CORE_INTERNAL_H
#define DM_CORE_INTERNAL_H

#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/blk-mq.h>

#include <trace/events/block.h>

#include "dm.h"

#define DM_RESERVED_MAX_IOS		1024

struct dm_kobject_holder {
	struct kobject kobj;
	struct completion completion;
};

/*
 * DM core internal structure that used directly by dm.c and dm-rq.c
 * DM targets must _not_ deference a mapped_device to directly access its members!
 */
struct mapped_device {
	struct srcu_struct io_barrier;
	struct mutex suspend_lock;

	/*
	 * The current mapping (struct dm_table *).
	 * Use dm_get_live_table{_fast} or take suspend_lock for
	 * dereference.
	 */
	void __rcu *map;

	struct list_head table_devices;
	struct mutex table_devices_lock;

	unsigned long flags;

	struct request_queue *queue;
	int numa_node_id;

	unsigned type;
	/* Protect queue and type against concurrent access. */
	struct mutex type_lock;

	atomic_t holders;
	atomic_t open_count;

	struct dm_target *immutable_target;
	struct target_type *immutable_target_type;

	struct gendisk *disk;
	char name[16];

	void *interface_ptr;

	/*
	 * A list of ios that arrived while we were suspended.
	 */
	atomic_t pending[2];
	wait_queue_head_t wait;
	struct work_struct work;
	spinlock_t deferred_lock;
	struct bio_list deferred;

	/*
	 * Event handling.
	 */
	wait_queue_head_t eventq;
	atomic_t event_nr;
	atomic_t uevent_seq;
	struct list_head uevent_list;
	spinlock_t uevent_lock; /* Protect access to uevent_list */

	/* the number of internal suspends */
	unsigned internal_suspend_count;

	/*
	 * Processing queue (flush)
	 */
	struct workqueue_struct *wq;

	/*
	 * io objects are allocated from here.
	 */
	mempool_t *io_pool;

	struct bio_set *bs;

	/*
	 * freeze/thaw support require holding onto a super block
	 */
	struct super_block *frozen_sb;

	/* forced geometry settings */
	struct hd_geometry geometry;

	struct block_device *bdev;

	/* kobject and completion */
	struct dm_kobject_holder kobj_holder;

	/* zero-length flush that will be cloned and submitted to targets */
	struct bio flush_bio;

	struct dm_stats stats;

	struct kthread_worker kworker;
	struct task_struct *kworker_task;

	/* for request-based merge heuristic in dm_request_fn() */
	unsigned seq_rq_merge_deadline_usecs;
	int last_rq_rw;
	sector_t last_rq_pos;
	ktime_t last_rq_start_time;

	/* for blk-mq request-based DM support */
	struct blk_mq_tag_set *tag_set;
	bool use_blk_mq:1;
	bool init_tio_pdu:1;
};

void dm_init_md_queue(struct mapped_device *md);
void dm_init_normal_md_queue(struct mapped_device *md);
int md_in_flight(struct mapped_device *md);
void disable_write_same(struct mapped_device *md);

static inline struct completion *dm_get_completion_from_kobject(struct kobject *kobj)
{
	return &container_of(kobj, struct dm_kobject_holder, kobj)->completion;
}

unsigned __dm_get_module_param(unsigned *module_param, unsigned def, unsigned max);

static inline bool dm_message_test_buffer_overflow(char *result, unsigned maxlen)
{
	return !maxlen || strlen(result) + 1 >= maxlen;
}

#endif
