/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_DEDUPE_H
#define VDO_DEDUPE_H

#include <linux/list.h>
#include <linux/timer.h>

#include "indexer.h"

#include "admin-state.h"
#include "constants.h"
#include "statistics.h"
#include "types.h"
#include "wait-queue.h"

struct dedupe_context {
	struct hash_zone *zone;
	struct uds_request request;
	struct list_head list_entry;
	struct funnel_queue_entry queue_entry;
	u64 submission_jiffies;
	struct data_vio *requestor;
	atomic_t state;
};

struct hash_lock;

struct hash_zone {
	/* Which hash zone this is */
	zone_count_t zone_number;

	/* The administrative state of the zone */
	struct admin_state state;

	/* The thread ID for this zone */
	thread_id_t thread_id;

	/* Mapping from record name fields to hash_locks */
	struct int_map *hash_lock_map;

	/* List containing all unused hash_locks */
	struct list_head lock_pool;

	/*
	 * Statistics shared by all hash locks in this zone. Only modified on the hash zone thread,
	 * but queried by other threads.
	 */
	struct hash_lock_statistics statistics;

	/* Array of all hash_locks */
	struct hash_lock *lock_array;

	/* These fields are used to manage the dedupe contexts */
	struct list_head available;
	struct list_head pending;
	struct funnel_queue *timed_out_complete;
	struct timer_list timer;
	struct vdo_completion completion;
	unsigned int active;
	atomic_t timer_state;

	/* The dedupe contexts for querying the index from this zone */
	struct dedupe_context contexts[MAXIMUM_VDO_USER_VIOS];
};

struct hash_zones;

struct pbn_lock * __must_check vdo_get_duplicate_lock(struct data_vio *data_vio);

void vdo_acquire_hash_lock(struct vdo_completion *completion);
void vdo_continue_hash_lock(struct vdo_completion *completion);
void vdo_release_hash_lock(struct data_vio *data_vio);
void vdo_clean_failed_hash_lock(struct data_vio *data_vio);
void vdo_share_compressed_write_lock(struct data_vio *data_vio,
				     struct pbn_lock *pbn_lock);

int __must_check vdo_make_hash_zones(struct vdo *vdo, struct hash_zones **zones_ptr);

void vdo_free_hash_zones(struct hash_zones *zones);

void vdo_drain_hash_zones(struct hash_zones *zones, struct vdo_completion *parent);

void vdo_get_dedupe_statistics(struct hash_zones *zones, struct vdo_statistics *stats);

struct hash_zone * __must_check vdo_select_hash_zone(struct hash_zones *zones,
						     const struct uds_record_name *name);

void vdo_dump_hash_zones(struct hash_zones *zones);

const char *vdo_get_dedupe_index_state_name(struct hash_zones *zones);

u64 vdo_get_dedupe_index_timeout_count(struct hash_zones *zones);

int vdo_message_dedupe_index(struct hash_zones *zones, const char *name);

void vdo_set_dedupe_state_normal(struct hash_zones *zones);

void vdo_start_dedupe_index(struct hash_zones *zones, bool create_flag);

void vdo_resume_hash_zones(struct hash_zones *zones, struct vdo_completion *parent);

void vdo_finish_dedupe_index(struct hash_zones *zones);

/* Interval (in milliseconds) from submission until switching to fast path and skipping UDS. */
extern unsigned int vdo_dedupe_index_timeout_interval;

/*
 * Minimum time interval (in milliseconds) between timer invocations to check for requests waiting
 * for UDS that should now time out.
 */
extern unsigned int vdo_dedupe_index_min_timer_interval;

void vdo_set_dedupe_index_timeout_interval(unsigned int value);
void vdo_set_dedupe_index_min_timer_interval(unsigned int value);

#endif /* VDO_DEDUPE_H */
