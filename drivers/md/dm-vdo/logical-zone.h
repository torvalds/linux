/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_LOGICAL_ZONE_H
#define VDO_LOGICAL_ZONE_H

#include <linux/list.h>

#include "admin-state.h"
#include "int-map.h"
#include "types.h"

struct physical_zone;

struct logical_zone {
	/* The completion for flush notifications */
	struct vdo_completion completion;
	/* The owner of this zone */
	struct logical_zones *zones;
	/* Which logical zone this is */
	zone_count_t zone_number;
	/* The thread id for this zone */
	thread_id_t thread_id;
	/* In progress operations keyed by LBN */
	struct int_map *lbn_operations;
	/* The logical to physical map */
	struct block_map_zone *block_map_zone;
	/* The current flush generation */
	sequence_number_t flush_generation;
	/*
	 * The oldest active generation in this zone. This is mutated only on the logical zone
	 * thread but is queried from the flusher thread.
	 */
	sequence_number_t oldest_active_generation;
	/* The number of IOs in the current flush generation */
	block_count_t ios_in_flush_generation;
	/* The youngest generation of the current notification */
	sequence_number_t notification_generation;
	/* Whether a notification is in progress */
	bool notifying;
	/* The queue of active data write VIOs */
	struct list_head write_vios;
	/* The administrative state of the zone */
	struct admin_state state;
	/* The physical zone from which to allocate */
	struct physical_zone *allocation_zone;
	/* The number of allocations done from the current allocation_zone */
	block_count_t allocation_count;
	/* The next zone */
	struct logical_zone *next;
};

struct logical_zones {
	/* The vdo whose zones these are */
	struct vdo *vdo;
	/* The manager for administrative actions */
	struct action_manager *manager;
	/* The number of zones */
	zone_count_t zone_count;
	/* The logical zones themselves */
	struct logical_zone zones[];
};

int __must_check vdo_make_logical_zones(struct vdo *vdo,
					struct logical_zones **zones_ptr);

void vdo_free_logical_zones(struct logical_zones *zones);

void vdo_drain_logical_zones(struct logical_zones *zones,
			     const struct admin_state_code *operation,
			     struct vdo_completion *completion);

void vdo_resume_logical_zones(struct logical_zones *zones,
			      struct vdo_completion *parent);

void vdo_increment_logical_zone_flush_generation(struct logical_zone *zone,
						 sequence_number_t expected_generation);

void vdo_acquire_flush_generation_lock(struct data_vio *data_vio);

void vdo_release_flush_generation_lock(struct data_vio *data_vio);

struct physical_zone * __must_check vdo_get_next_allocation_zone(struct logical_zone *zone);

void vdo_dump_logical_zone(const struct logical_zone *zone);

#endif /* VDO_LOGICAL_ZONE_H */
