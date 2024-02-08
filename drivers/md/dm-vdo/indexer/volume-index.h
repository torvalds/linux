/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_VOLUME_INDEX_H
#define UDS_VOLUME_INDEX_H

#include <linux/limits.h>

#include "thread-utils.h"

#include "config.h"
#include "delta-index.h"
#include "indexer.h"

/*
 * The volume index is the primary top-level index for UDS. It contains records which map a record
 * name to the chapter where a record with that name is stored. This mapping can definitively say
 * when no record exists. However, because we only use a subset of the name for this index, it
 * cannot definitively say that a record for the entry does exist. It can only say that if a record
 * exists, it will be in a particular chapter. The request can then be dispatched to that chapter
 * for further processing.
 *
 * If the volume_index_record does not actually match the record name, the index can store a more
 * specific collision record to disambiguate the new entry from the existing one. Index entries are
 * managed with volume_index_record structures.
 */

#define NO_CHAPTER U64_MAX

struct volume_index_stats {
	/* Nanoseconds spent rebalancing */
	ktime_t rebalance_time;
	/* Number of memory rebalances */
	u32 rebalance_count;
	/* The number of records in the index */
	u64 record_count;
	/* The number of collision records */
	u64 collision_count;
	/* The number of records removed */
	u64 discard_count;
	/* The number of UDS_OVERFLOWs detected */
	u64 overflow_count;
	/* The number of delta lists */
	u32 delta_lists;
	/* Number of early flushes */
	u64 early_flushes;
};

struct volume_sub_index_zone {
	u64 virtual_chapter_low;
	u64 virtual_chapter_high;
	u64 early_flushes;
} __aligned(L1_CACHE_BYTES);

struct volume_sub_index {
	/* The delta index */
	struct delta_index delta_index;
	/* The first chapter to be flushed in each zone */
	u64 *flush_chapters;
	/* The zones */
	struct volume_sub_index_zone *zones;
	/* The volume nonce */
	u64 volume_nonce;
	/* Expected size of a chapter (per zone) */
	u64 chapter_zone_bits;
	/* Maximum size of the index (per zone) */
	u64 max_zone_bits;
	/* The number of bits in address mask */
	u8 address_bits;
	/* Mask to get address within delta list */
	u32 address_mask;
	/* The number of bits in chapter number */
	u8 chapter_bits;
	/* The largest storable chapter number */
	u32 chapter_mask;
	/* The number of chapters used */
	u32 chapter_count;
	/* The number of delta lists */
	u32 list_count;
	/* The number of zones */
	unsigned int zone_count;
	/* The amount of memory allocated */
	u64 memory_size;
};

struct volume_index_zone {
	/* Protects the sampled index in this zone */
	struct mutex hook_mutex;
} __aligned(L1_CACHE_BYTES);

struct volume_index {
	u32 sparse_sample_rate;
	unsigned int zone_count;
	u64 memory_size;
	struct volume_sub_index vi_non_hook;
	struct volume_sub_index vi_hook;
	struct volume_index_zone *zones;
};

/*
 * The volume_index_record structure is used to facilitate processing of a record name. A client
 * first calls uds_get_volume_index_record() to find the volume index record for a record name. The
 * fields of the record can then be examined to determine the state of the record.
 *
 * If is_found is false, then the index did not find an entry for the record name. Calling
 * uds_put_volume_index_record() will insert a new entry for that name at the proper place.
 *
 * If is_found is true, then we did find an entry for the record name, and the virtual_chapter and
 * is_collision fields reflect the entry found. Subsequently, a call to
 * uds_remove_volume_index_record() will remove the entry, a call to
 * uds_set_volume_index_record_chapter() will update the existing entry, and a call to
 * uds_put_volume_index_record() will insert a new collision record after the existing entry.
 */
struct volume_index_record {
	/* Public fields */

	/* Chapter where the record info is found */
	u64 virtual_chapter;
	/* This record is a collision */
	bool is_collision;
	/* This record is the requested record */
	bool is_found;

	/* Private fields */

	/* Zone that contains this name */
	unsigned int zone_number;
	/* The volume index */
	struct volume_sub_index *sub_index;
	/* Mutex for accessing this delta index entry in the hook index */
	struct mutex *mutex;
	/* The record name to which this record refers */
	const struct uds_record_name *name;
	/* The delta index entry for this record */
	struct delta_index_entry delta_entry;
};

int __must_check uds_make_volume_index(const struct uds_configuration *config,
				       u64 volume_nonce,
				       struct volume_index **volume_index);

void uds_free_volume_index(struct volume_index *volume_index);

int __must_check uds_compute_volume_index_save_blocks(const struct uds_configuration *config,
						      size_t block_size,
						      u64 *block_count);

unsigned int __must_check uds_get_volume_index_zone(const struct volume_index *volume_index,
						    const struct uds_record_name *name);

bool __must_check uds_is_volume_index_sample(const struct volume_index *volume_index,
					     const struct uds_record_name *name);

/*
 * This function is only used to manage sparse cache membership. Most requests should use
 * uds_get_volume_index_record() to look up index records instead.
 */
u64 __must_check uds_lookup_volume_index_name(const struct volume_index *volume_index,
					      const struct uds_record_name *name);

int __must_check uds_get_volume_index_record(struct volume_index *volume_index,
					     const struct uds_record_name *name,
					     struct volume_index_record *record);

int __must_check uds_put_volume_index_record(struct volume_index_record *record,
					     u64 virtual_chapter);

int __must_check uds_remove_volume_index_record(struct volume_index_record *record);

int __must_check uds_set_volume_index_record_chapter(struct volume_index_record *record,
						     u64 virtual_chapter);

void uds_set_volume_index_open_chapter(struct volume_index *volume_index,
				       u64 virtual_chapter);

void uds_set_volume_index_zone_open_chapter(struct volume_index *volume_index,
					    unsigned int zone_number,
					    u64 virtual_chapter);

int __must_check uds_load_volume_index(struct volume_index *volume_index,
				       struct buffered_reader **readers,
				       unsigned int reader_count);

int __must_check uds_save_volume_index(struct volume_index *volume_index,
				       struct buffered_writer **writers,
				       unsigned int writer_count);

void uds_get_volume_index_stats(const struct volume_index *volume_index,
				struct volume_index_stats *stats);

#endif /* UDS_VOLUME_INDEX_H */
