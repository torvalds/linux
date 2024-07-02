// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "open-chapter.h"

#include <linux/log2.h>

#include "logger.h"
#include "memory-alloc.h"
#include "numeric.h"
#include "permassert.h"

#include "config.h"
#include "hash-utils.h"

/*
 * Each index zone has a dedicated open chapter zone structure which gets an equal share of the
 * open chapter space. Records are assigned to zones based on their record name. Within each zone,
 * records are stored in an array in the order they arrive. Additionally, a reference to each
 * record is stored in a hash table to help determine if a new record duplicates an existing one.
 * If new metadata for an existing name arrives, the record is altered in place. The array of
 * records is 1-based so that record number 0 can be used to indicate an unused hash slot.
 *
 * Deleted records are marked with a flag rather than actually removed to simplify hash table
 * management. The array of deleted flags overlays the array of hash slots, but the flags are
 * indexed by record number instead of by record name. The number of hash slots will always be a
 * power of two that is greater than the number of records to be indexed, guaranteeing that hash
 * insertion cannot fail, and that there are sufficient flags for all records.
 *
 * Once any open chapter zone fills its available space, the chapter is closed. The records from
 * each zone are interleaved to attempt to preserve temporal locality and assigned to record pages.
 * Empty or deleted records are replaced by copies of a valid record so that the record pages only
 * contain valid records. The chapter then constructs a delta index which maps each record name to
 * the record page on which that record can be found, which is split into index pages. These
 * structures are then passed to the volume to be recorded on storage.
 *
 * When the index is saved, the open chapter records are saved in a single array, once again
 * interleaved to attempt to preserve temporal locality. When the index is reloaded, there may be a
 * different number of zones than previously, so the records must be parcelled out to their new
 * zones. In addition, depending on the distribution of record names, a new zone may have more
 * records than it has space. In this case, the latest records for that zone will be discarded.
 */

static const u8 OPEN_CHAPTER_MAGIC[] = "ALBOC";
static const u8 OPEN_CHAPTER_VERSION[] = "02.00";

#define OPEN_CHAPTER_MAGIC_LENGTH (sizeof(OPEN_CHAPTER_MAGIC) - 1)
#define OPEN_CHAPTER_VERSION_LENGTH (sizeof(OPEN_CHAPTER_VERSION) - 1)
#define LOAD_RATIO 2

static inline size_t records_size(const struct open_chapter_zone *open_chapter)
{
	return sizeof(struct uds_volume_record) * (1 + open_chapter->capacity);
}

static inline size_t slots_size(size_t slot_count)
{
	return sizeof(struct open_chapter_zone_slot) * slot_count;
}

int uds_make_open_chapter(const struct index_geometry *geometry, unsigned int zone_count,
			  struct open_chapter_zone **open_chapter_ptr)
{
	int result;
	struct open_chapter_zone *open_chapter;
	size_t capacity = geometry->records_per_chapter / zone_count;
	size_t slot_count = (1 << bits_per(capacity * LOAD_RATIO));

	result = vdo_allocate_extended(struct open_chapter_zone, slot_count,
				       struct open_chapter_zone_slot, "open chapter",
				       &open_chapter);
	if (result != VDO_SUCCESS)
		return result;

	open_chapter->slot_count = slot_count;
	open_chapter->capacity = capacity;
	result = vdo_allocate_cache_aligned(records_size(open_chapter), "record pages",
					    &open_chapter->records);
	if (result != VDO_SUCCESS) {
		uds_free_open_chapter(open_chapter);
		return result;
	}

	*open_chapter_ptr = open_chapter;
	return UDS_SUCCESS;
}

void uds_reset_open_chapter(struct open_chapter_zone *open_chapter)
{
	open_chapter->size = 0;
	open_chapter->deletions = 0;

	memset(open_chapter->records, 0, records_size(open_chapter));
	memset(open_chapter->slots, 0, slots_size(open_chapter->slot_count));
}

static unsigned int probe_chapter_slots(struct open_chapter_zone *open_chapter,
					const struct uds_record_name *name)
{
	struct uds_volume_record *record;
	unsigned int slot_count = open_chapter->slot_count;
	unsigned int slot = uds_name_to_hash_slot(name, slot_count);
	unsigned int record_number;
	unsigned int attempts = 1;

	while (true) {
		record_number = open_chapter->slots[slot].record_number;

		/*
		 * If the hash slot is empty, we've reached the end of a chain without finding the
		 * record and should terminate the search.
		 */
		if (record_number == 0)
			return slot;

		/*
		 * If the name of the record referenced by the slot matches and has not been
		 * deleted, then we've found the requested name.
		 */
		record = &open_chapter->records[record_number];
		if ((memcmp(&record->name, name, UDS_RECORD_NAME_SIZE) == 0) &&
		    !open_chapter->slots[record_number].deleted)
			return slot;

		/*
		 * Quadratic probing: advance the probe by 1, 2, 3, etc. and try again. This
		 * performs better than linear probing and works best for 2^N slots.
		 */
		slot = (slot + attempts++) % slot_count;
	}
}

void uds_search_open_chapter(struct open_chapter_zone *open_chapter,
			     const struct uds_record_name *name,
			     struct uds_record_data *metadata, bool *found)
{
	unsigned int slot;
	unsigned int record_number;

	slot = probe_chapter_slots(open_chapter, name);
	record_number = open_chapter->slots[slot].record_number;
	if (record_number == 0) {
		*found = false;
	} else {
		*found = true;
		*metadata = open_chapter->records[record_number].data;
	}
}

/* Add a record to the open chapter zone and return the remaining space. */
int uds_put_open_chapter(struct open_chapter_zone *open_chapter,
			 const struct uds_record_name *name,
			 const struct uds_record_data *metadata)
{
	unsigned int slot;
	unsigned int record_number;
	struct uds_volume_record *record;

	if (open_chapter->size >= open_chapter->capacity)
		return 0;

	slot = probe_chapter_slots(open_chapter, name);
	record_number = open_chapter->slots[slot].record_number;

	if (record_number == 0) {
		record_number = ++open_chapter->size;
		open_chapter->slots[slot].record_number = record_number;
	}

	record = &open_chapter->records[record_number];
	record->name = *name;
	record->data = *metadata;

	return open_chapter->capacity - open_chapter->size;
}

void uds_remove_from_open_chapter(struct open_chapter_zone *open_chapter,
				  const struct uds_record_name *name)
{
	unsigned int slot;
	unsigned int record_number;

	slot = probe_chapter_slots(open_chapter, name);
	record_number = open_chapter->slots[slot].record_number;

	if (record_number > 0) {
		open_chapter->slots[record_number].deleted = true;
		open_chapter->deletions += 1;
	}
}

void uds_free_open_chapter(struct open_chapter_zone *open_chapter)
{
	if (open_chapter != NULL) {
		vdo_free(open_chapter->records);
		vdo_free(open_chapter);
	}
}

/* Map each record name to its record page number in the delta chapter index. */
static int fill_delta_chapter_index(struct open_chapter_zone **chapter_zones,
				    unsigned int zone_count,
				    struct open_chapter_index *index,
				    struct uds_volume_record *collated_records)
{
	int result;
	unsigned int records_per_chapter;
	unsigned int records_per_page;
	unsigned int record_index;
	unsigned int records = 0;
	u32 page_number;
	unsigned int z;
	int overflow_count = 0;
	struct uds_volume_record *fill_record = NULL;

	/*
	 * The record pages should not have any empty space, so find a record with which to fill
	 * the chapter zone if it was closed early, and also to replace any deleted records. The
	 * last record in any filled zone is guaranteed to not have been deleted, so use one of
	 * those.
	 */
	for (z = 0; z < zone_count; z++) {
		struct open_chapter_zone *zone = chapter_zones[z];

		if (zone->size == zone->capacity) {
			fill_record = &zone->records[zone->size];
			break;
		}
	}

	records_per_chapter = index->geometry->records_per_chapter;
	records_per_page = index->geometry->records_per_page;

	for (records = 0; records < records_per_chapter; records++) {
		struct uds_volume_record *record = &collated_records[records];
		struct open_chapter_zone *open_chapter;

		/* The record arrays in the zones are 1-based. */
		record_index = 1 + (records / zone_count);
		page_number = records / records_per_page;
		open_chapter = chapter_zones[records % zone_count];

		/* Use the fill record in place of an unused record. */
		if (record_index > open_chapter->size ||
		    open_chapter->slots[record_index].deleted) {
			*record = *fill_record;
			continue;
		}

		*record = open_chapter->records[record_index];
		result = uds_put_open_chapter_index_record(index, &record->name,
							   page_number);
		switch (result) {
		case UDS_SUCCESS:
			break;
		case UDS_OVERFLOW:
			overflow_count++;
			break;
		default:
			vdo_log_error_strerror(result,
					       "failed to build open chapter index");
			return result;
		}
	}

	if (overflow_count > 0)
		vdo_log_warning("Failed to add %d entries to chapter index",
				overflow_count);

	return UDS_SUCCESS;
}

int uds_close_open_chapter(struct open_chapter_zone **chapter_zones,
			   unsigned int zone_count, struct volume *volume,
			   struct open_chapter_index *chapter_index,
			   struct uds_volume_record *collated_records,
			   u64 virtual_chapter_number)
{
	int result;

	uds_empty_open_chapter_index(chapter_index, virtual_chapter_number);
	result = fill_delta_chapter_index(chapter_zones, zone_count, chapter_index,
					  collated_records);
	if (result != UDS_SUCCESS)
		return result;

	return uds_write_chapter(volume, chapter_index, collated_records);
}

int uds_save_open_chapter(struct uds_index *index, struct buffered_writer *writer)
{
	int result;
	struct open_chapter_zone *open_chapter;
	struct uds_volume_record *record;
	u8 record_count_data[sizeof(u32)];
	u32 record_count = 0;
	unsigned int record_index;
	unsigned int z;

	result = uds_write_to_buffered_writer(writer, OPEN_CHAPTER_MAGIC,
					      OPEN_CHAPTER_MAGIC_LENGTH);
	if (result != UDS_SUCCESS)
		return result;

	result = uds_write_to_buffered_writer(writer, OPEN_CHAPTER_VERSION,
					      OPEN_CHAPTER_VERSION_LENGTH);
	if (result != UDS_SUCCESS)
		return result;

	for (z = 0; z < index->zone_count; z++) {
		open_chapter = index->zones[z]->open_chapter;
		record_count += open_chapter->size - open_chapter->deletions;
	}

	put_unaligned_le32(record_count, record_count_data);
	result = uds_write_to_buffered_writer(writer, record_count_data,
					      sizeof(record_count_data));
	if (result != UDS_SUCCESS)
		return result;

	record_index = 1;
	while (record_count > 0) {
		for (z = 0; z < index->zone_count; z++) {
			open_chapter = index->zones[z]->open_chapter;
			if (record_index > open_chapter->size)
				continue;

			if (open_chapter->slots[record_index].deleted)
				continue;

			record = &open_chapter->records[record_index];
			result = uds_write_to_buffered_writer(writer, (u8 *) record,
							      sizeof(*record));
			if (result != UDS_SUCCESS)
				return result;

			record_count--;
		}

		record_index++;
	}

	return uds_flush_buffered_writer(writer);
}

u64 uds_compute_saved_open_chapter_size(struct index_geometry *geometry)
{
	unsigned int records_per_chapter = geometry->records_per_chapter;

	return OPEN_CHAPTER_MAGIC_LENGTH + OPEN_CHAPTER_VERSION_LENGTH + sizeof(u32) +
		records_per_chapter * sizeof(struct uds_volume_record);
}

static int load_version20(struct uds_index *index, struct buffered_reader *reader)
{
	int result;
	u32 record_count;
	u8 record_count_data[sizeof(u32)];
	struct uds_volume_record record;

	/*
	 * Track which zones cannot accept any more records. If the open chapter had a different
	 * number of zones previously, some new zones may have more records than they have space
	 * for. These overflow records will be discarded.
	 */
	bool full_flags[MAX_ZONES] = {
		false,
	};

	result = uds_read_from_buffered_reader(reader, (u8 *) &record_count_data,
					       sizeof(record_count_data));
	if (result != UDS_SUCCESS)
		return result;

	record_count = get_unaligned_le32(record_count_data);
	while (record_count-- > 0) {
		unsigned int zone = 0;

		result = uds_read_from_buffered_reader(reader, (u8 *) &record,
						       sizeof(record));
		if (result != UDS_SUCCESS)
			return result;

		if (index->zone_count > 1)
			zone = uds_get_volume_index_zone(index->volume_index,
							 &record.name);

		if (!full_flags[zone]) {
			struct open_chapter_zone *open_chapter;
			unsigned int remaining;

			open_chapter = index->zones[zone]->open_chapter;
			remaining = uds_put_open_chapter(open_chapter, &record.name,
							 &record.data);
			/* Do not allow any zone to fill completely. */
			full_flags[zone] = (remaining <= 1);
		}
	}

	return UDS_SUCCESS;
}

int uds_load_open_chapter(struct uds_index *index, struct buffered_reader *reader)
{
	u8 version[OPEN_CHAPTER_VERSION_LENGTH];
	int result;

	result = uds_verify_buffered_data(reader, OPEN_CHAPTER_MAGIC,
					  OPEN_CHAPTER_MAGIC_LENGTH);
	if (result != UDS_SUCCESS)
		return result;

	result = uds_read_from_buffered_reader(reader, version, sizeof(version));
	if (result != UDS_SUCCESS)
		return result;

	if (memcmp(OPEN_CHAPTER_VERSION, version, sizeof(version)) != 0) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "Invalid open chapter version: %.*s",
					      (int) sizeof(version), version);
	}

	return load_version20(index, reader);
}
