// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "index-page-map.h"

#include "errors.h"
#include "logger.h"
#include "memory-alloc.h"
#include "numeric.h"
#include "permassert.h"
#include "string-utils.h"
#include "thread-utils.h"

#include "hash-utils.h"
#include "indexer.h"

/*
 * The index page map is conceptually a two-dimensional array indexed by chapter number and index
 * page number within the chapter. Each entry contains the number of the last delta list on that
 * index page. In order to save memory, the information for the last page in each chapter is not
 * recorded, as it is known from the geometry.
 */

static const u8 PAGE_MAP_MAGIC[] = "ALBIPM02";

#define PAGE_MAP_MAGIC_LENGTH (sizeof(PAGE_MAP_MAGIC) - 1)

static inline u32 get_entry_count(const struct index_geometry *geometry)
{
	return geometry->chapters_per_volume * (geometry->index_pages_per_chapter - 1);
}

int uds_make_index_page_map(const struct index_geometry *geometry,
			    struct index_page_map **map_ptr)
{
	int result;
	struct index_page_map *map;

	result = vdo_allocate(1, struct index_page_map, "page map", &map);
	if (result != VDO_SUCCESS)
		return result;

	map->geometry = geometry;
	map->entries_per_chapter = geometry->index_pages_per_chapter - 1;
	result = vdo_allocate(get_entry_count(geometry), u16, "Index Page Map Entries",
			      &map->entries);
	if (result != VDO_SUCCESS) {
		uds_free_index_page_map(map);
		return result;
	}

	*map_ptr = map;
	return UDS_SUCCESS;
}

void uds_free_index_page_map(struct index_page_map *map)
{
	if (map != NULL) {
		vdo_free(map->entries);
		vdo_free(map);
	}
}

void uds_update_index_page_map(struct index_page_map *map, u64 virtual_chapter_number,
			       u32 chapter_number, u32 index_page_number,
			       u32 delta_list_number)
{
	size_t slot;

	map->last_update = virtual_chapter_number;
	if (index_page_number == map->entries_per_chapter)
		return;

	slot = (chapter_number * map->entries_per_chapter) + index_page_number;
	map->entries[slot] = delta_list_number;
}

u32 uds_find_index_page_number(const struct index_page_map *map,
			       const struct uds_record_name *name, u32 chapter_number)
{
	u32 delta_list_number = uds_hash_to_chapter_delta_list(name, map->geometry);
	u32 slot = chapter_number * map->entries_per_chapter;
	u32 page;

	for (page = 0; page < map->entries_per_chapter; page++) {
		if (delta_list_number <= map->entries[slot + page])
			break;
	}

	return page;
}

void uds_get_list_number_bounds(const struct index_page_map *map, u32 chapter_number,
				u32 index_page_number, u32 *lowest_list,
				u32 *highest_list)
{
	u32 slot = chapter_number * map->entries_per_chapter;

	*lowest_list = ((index_page_number == 0) ?
			0 : map->entries[slot + index_page_number - 1] + 1);
	*highest_list = ((index_page_number < map->entries_per_chapter) ?
			 map->entries[slot + index_page_number] :
			 map->geometry->delta_lists_per_chapter - 1);
}

u64 uds_compute_index_page_map_save_size(const struct index_geometry *geometry)
{
	return PAGE_MAP_MAGIC_LENGTH + sizeof(u64) + sizeof(u16) * get_entry_count(geometry);
}

int uds_write_index_page_map(struct index_page_map *map, struct buffered_writer *writer)
{
	int result;
	u8 *buffer;
	size_t offset = 0;
	u64 saved_size = uds_compute_index_page_map_save_size(map->geometry);
	u32 i;

	result = vdo_allocate(saved_size, u8, "page map data", &buffer);
	if (result != VDO_SUCCESS)
		return result;

	memcpy(buffer, PAGE_MAP_MAGIC, PAGE_MAP_MAGIC_LENGTH);
	offset += PAGE_MAP_MAGIC_LENGTH;
	encode_u64_le(buffer, &offset, map->last_update);
	for (i = 0; i < get_entry_count(map->geometry); i++)
		encode_u16_le(buffer, &offset, map->entries[i]);

	result = uds_write_to_buffered_writer(writer, buffer, offset);
	vdo_free(buffer);
	if (result != UDS_SUCCESS)
		return result;

	return uds_flush_buffered_writer(writer);
}

int uds_read_index_page_map(struct index_page_map *map, struct buffered_reader *reader)
{
	int result;
	u8 magic[PAGE_MAP_MAGIC_LENGTH];
	u8 *buffer;
	size_t offset = 0;
	u64 saved_size = uds_compute_index_page_map_save_size(map->geometry);
	u32 i;

	result = vdo_allocate(saved_size, u8, "page map data", &buffer);
	if (result != VDO_SUCCESS)
		return result;

	result = uds_read_from_buffered_reader(reader, buffer, saved_size);
	if (result != UDS_SUCCESS) {
		vdo_free(buffer);
		return result;
	}

	memcpy(&magic, buffer, PAGE_MAP_MAGIC_LENGTH);
	offset += PAGE_MAP_MAGIC_LENGTH;
	if (memcmp(magic, PAGE_MAP_MAGIC, PAGE_MAP_MAGIC_LENGTH) != 0) {
		vdo_free(buffer);
		return UDS_CORRUPT_DATA;
	}

	decode_u64_le(buffer, &offset, &map->last_update);
	for (i = 0; i < get_entry_count(map->geometry); i++)
		decode_u16_le(buffer, &offset, &map->entries[i]);

	vdo_free(buffer);
	vdo_log_debug("read index page map, last update %llu",
		      (unsigned long long) map->last_update);
	return UDS_SUCCESS;
}
