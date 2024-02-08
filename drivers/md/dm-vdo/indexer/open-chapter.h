/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_OPEN_CHAPTER_H
#define UDS_OPEN_CHAPTER_H

#include "chapter-index.h"
#include "geometry.h"
#include "index.h"
#include "volume.h"

/*
 * The open chapter tracks the newest records in memory. Like the index as a whole, each open
 * chapter is divided into a number of independent zones which are interleaved when the chapter is
 * committed to the volume.
 */

enum {
	OPEN_CHAPTER_RECORD_NUMBER_BITS = 23,
};

struct open_chapter_zone_slot {
	/* If non-zero, the record number addressed by this hash slot */
	unsigned int record_number : OPEN_CHAPTER_RECORD_NUMBER_BITS;
	/* If true, the record at the index of this hash slot was deleted */
	bool deleted : 1;
} __packed;

struct open_chapter_zone {
	/* The maximum number of records that can be stored */
	unsigned int capacity;
	/* The number of records stored */
	unsigned int size;
	/* The number of deleted records */
	unsigned int deletions;
	/* Array of chunk records, 1-based */
	struct uds_volume_record *records;
	/* The number of slots in the hash table */
	unsigned int slot_count;
	/* The hash table slots, referencing virtual record numbers */
	struct open_chapter_zone_slot slots[];
};

int __must_check uds_make_open_chapter(const struct index_geometry *geometry,
				       unsigned int zone_count,
				       struct open_chapter_zone **open_chapter_ptr);

void uds_reset_open_chapter(struct open_chapter_zone *open_chapter);

void uds_search_open_chapter(struct open_chapter_zone *open_chapter,
			     const struct uds_record_name *name,
			     struct uds_record_data *metadata, bool *found);

int __must_check uds_put_open_chapter(struct open_chapter_zone *open_chapter,
				      const struct uds_record_name *name,
				      const struct uds_record_data *metadata);

void uds_remove_from_open_chapter(struct open_chapter_zone *open_chapter,
				  const struct uds_record_name *name);

void uds_free_open_chapter(struct open_chapter_zone *open_chapter);

int __must_check uds_close_open_chapter(struct open_chapter_zone **chapter_zones,
					unsigned int zone_count, struct volume *volume,
					struct open_chapter_index *chapter_index,
					struct uds_volume_record *collated_records,
					u64 virtual_chapter_number);

int __must_check uds_save_open_chapter(struct uds_index *index,
				       struct buffered_writer *writer);

int __must_check uds_load_open_chapter(struct uds_index *index,
				       struct buffered_reader *reader);

u64 uds_compute_saved_open_chapter_size(struct index_geometry *geometry);

#endif /* UDS_OPEN_CHAPTER_H */
