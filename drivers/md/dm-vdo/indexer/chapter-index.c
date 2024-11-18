// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "chapter-index.h"

#include "errors.h"
#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "hash-utils.h"
#include "indexer.h"

int uds_make_open_chapter_index(struct open_chapter_index **chapter_index,
				const struct index_geometry *geometry, u64 volume_nonce)
{
	int result;
	size_t memory_size;
	struct open_chapter_index *index;

	result = vdo_allocate(1, struct open_chapter_index, "open chapter index", &index);
	if (result != VDO_SUCCESS)
		return result;

	/*
	 * The delta index will rebalance delta lists when memory gets tight,
	 * so give the chapter index one extra page.
	 */
	memory_size = ((geometry->index_pages_per_chapter + 1) * geometry->bytes_per_page);
	index->geometry = geometry;
	index->volume_nonce = volume_nonce;
	result = uds_initialize_delta_index(&index->delta_index, 1,
					    geometry->delta_lists_per_chapter,
					    geometry->chapter_mean_delta,
					    geometry->chapter_payload_bits,
					    memory_size, 'm');
	if (result != UDS_SUCCESS) {
		vdo_free(index);
		return result;
	}

	index->memory_size = index->delta_index.memory_size + sizeof(struct open_chapter_index);
	*chapter_index = index;
	return UDS_SUCCESS;
}

void uds_free_open_chapter_index(struct open_chapter_index *chapter_index)
{
	if (chapter_index == NULL)
		return;

	uds_uninitialize_delta_index(&chapter_index->delta_index);
	vdo_free(chapter_index);
}

/* Re-initialize an open chapter index for a new chapter. */
void uds_empty_open_chapter_index(struct open_chapter_index *chapter_index,
				  u64 virtual_chapter_number)
{
	uds_reset_delta_index(&chapter_index->delta_index);
	chapter_index->virtual_chapter_number = virtual_chapter_number;
}

static inline bool was_entry_found(const struct delta_index_entry *entry, u32 address)
{
	return (!entry->at_end) && (entry->key == address);
}

/* Associate a record name with the record page containing its metadata. */
int uds_put_open_chapter_index_record(struct open_chapter_index *chapter_index,
				      const struct uds_record_name *name,
				      u32 page_number)
{
	int result;
	struct delta_index_entry entry;
	u32 address;
	u32 list_number;
	const u8 *found_name;
	bool found;
	const struct index_geometry *geometry = chapter_index->geometry;
	u64 chapter_number = chapter_index->virtual_chapter_number;
	u32 record_pages = geometry->record_pages_per_chapter;

	result = VDO_ASSERT(page_number < record_pages,
			    "Page number within chapter (%u) exceeds the maximum value %u",
			    page_number, record_pages);
	if (result != VDO_SUCCESS)
		return UDS_INVALID_ARGUMENT;

	address = uds_hash_to_chapter_delta_address(name, geometry);
	list_number = uds_hash_to_chapter_delta_list(name, geometry);
	result = uds_get_delta_index_entry(&chapter_index->delta_index, list_number,
					   address, name->name, &entry);
	if (result != UDS_SUCCESS)
		return result;

	found = was_entry_found(&entry, address);
	result = VDO_ASSERT(!(found && entry.is_collision),
			    "Chunk appears more than once in chapter %llu",
			    (unsigned long long) chapter_number);
	if (result != VDO_SUCCESS)
		return UDS_BAD_STATE;

	found_name = (found ? name->name : NULL);
	return uds_put_delta_index_entry(&entry, address, page_number, found_name);
}

/*
 * Pack a section of an open chapter index into a chapter index page. A range of delta lists
 * (starting with a specified list index) is copied from the open chapter index into a memory page.
 * The number of lists copied onto the page is returned to the caller on success.
 *
 * @chapter_index: The open chapter index
 * @memory: The memory page to use
 * @first_list: The first delta list number to be copied
 * @last_page: If true, this is the last page of the chapter index and all the remaining lists must
 *             be packed onto this page
 * @lists_packed: The number of delta lists that were packed onto this page
 */
int uds_pack_open_chapter_index_page(struct open_chapter_index *chapter_index,
				     u8 *memory, u32 first_list, bool last_page,
				     u32 *lists_packed)
{
	int result;
	struct delta_index *delta_index = &chapter_index->delta_index;
	struct delta_index_stats stats;
	u64 nonce = chapter_index->volume_nonce;
	u64 chapter_number = chapter_index->virtual_chapter_number;
	const struct index_geometry *geometry = chapter_index->geometry;
	u32 list_count = geometry->delta_lists_per_chapter;
	unsigned int removals = 0;
	struct delta_index_entry entry;
	u32 next_list;
	s32 list_number;

	for (;;) {
		result = uds_pack_delta_index_page(delta_index, nonce, memory,
						   geometry->bytes_per_page,
						   chapter_number, first_list,
						   lists_packed);
		if (result != UDS_SUCCESS)
			return result;

		if ((first_list + *lists_packed) == list_count) {
			/* All lists are packed. */
			break;
		} else if (*lists_packed == 0) {
			/*
			 * The next delta list does not fit on a page. This delta list will be
			 * removed.
			 */
		} else if (last_page) {
			/*
			 * This is the last page and there are lists left unpacked, but all of the
			 * remaining lists must fit on the page. Find a list that contains entries
			 * and remove the entire list. Try the first list that does not fit. If it
			 * is empty, we will select the last list that already fits and has any
			 * entries.
			 */
		} else {
			/* This page is done. */
			break;
		}

		if (removals == 0) {
			uds_get_delta_index_stats(delta_index, &stats);
			vdo_log_warning("The chapter index for chapter %llu contains %llu entries with %llu collisions",
					(unsigned long long) chapter_number,
					(unsigned long long) stats.record_count,
					(unsigned long long) stats.collision_count);
		}

		list_number = *lists_packed;
		do {
			if (list_number < 0)
				return UDS_OVERFLOW;

			next_list = first_list + list_number--;
			result = uds_start_delta_index_search(delta_index, next_list, 0,
							      &entry);
			if (result != UDS_SUCCESS)
				return result;

			result = uds_next_delta_index_entry(&entry);
			if (result != UDS_SUCCESS)
				return result;
		} while (entry.at_end);

		do {
			result = uds_remove_delta_index_entry(&entry);
			if (result != UDS_SUCCESS)
				return result;

			removals++;
		} while (!entry.at_end);
	}

	if (removals > 0) {
		vdo_log_warning("To avoid chapter index page overflow in chapter %llu, %u entries were removed from the chapter index",
				(unsigned long long) chapter_number, removals);
	}

	return UDS_SUCCESS;
}

/* Make a new chapter index page, initializing it with the data from a given index_page buffer. */
int uds_initialize_chapter_index_page(struct delta_index_page *index_page,
				      const struct index_geometry *geometry,
				      u8 *page_buffer, u64 volume_nonce)
{
	return uds_initialize_delta_index_page(index_page, volume_nonce,
					       geometry->chapter_mean_delta,
					       geometry->chapter_payload_bits,
					       page_buffer, geometry->bytes_per_page);
}

/* Validate a chapter index page read during rebuild. */
int uds_validate_chapter_index_page(const struct delta_index_page *index_page,
				    const struct index_geometry *geometry)
{
	int result;
	const struct delta_index *delta_index = &index_page->delta_index;
	u32 first = index_page->lowest_list_number;
	u32 last = index_page->highest_list_number;
	u32 list_number;

	/* We walk every delta list from start to finish. */
	for (list_number = first; list_number <= last; list_number++) {
		struct delta_index_entry entry;

		result = uds_start_delta_index_search(delta_index, list_number - first,
						      0, &entry);
		if (result != UDS_SUCCESS)
			return result;

		for (;;) {
			result = uds_next_delta_index_entry(&entry);
			if (result != UDS_SUCCESS) {
				/*
				 * A random bit stream is highly likely to arrive here when we go
				 * past the end of the delta list.
				 */
				return result;
			}

			if (entry.at_end)
				break;

			/* Also make sure that the record page field contains a plausible value. */
			if (uds_get_delta_entry_value(&entry) >=
			    geometry->record_pages_per_chapter) {
				/*
				 * Do not log this as an error. It happens in normal operation when
				 * we are doing a rebuild but haven't written the entire volume
				 * once.
				 */
				return UDS_CORRUPT_DATA;
			}
		}
	}
	return UDS_SUCCESS;
}

/*
 * Search a chapter index page for a record name, returning the record page number that may contain
 * the name.
 */
int uds_search_chapter_index_page(struct delta_index_page *index_page,
				  const struct index_geometry *geometry,
				  const struct uds_record_name *name,
				  u16 *record_page_ptr)
{
	int result;
	struct delta_index *delta_index = &index_page->delta_index;
	u32 address = uds_hash_to_chapter_delta_address(name, geometry);
	u32 delta_list_number = uds_hash_to_chapter_delta_list(name, geometry);
	u32 sub_list_number = delta_list_number - index_page->lowest_list_number;
	struct delta_index_entry entry;

	result = uds_get_delta_index_entry(delta_index, sub_list_number, address,
					   name->name, &entry);
	if (result != UDS_SUCCESS)
		return result;

	if (was_entry_found(&entry, address))
		*record_page_ptr = uds_get_delta_entry_value(&entry);
	else
		*record_page_ptr = NO_CHAPTER_INDEX_ENTRY;

	return UDS_SUCCESS;
}
