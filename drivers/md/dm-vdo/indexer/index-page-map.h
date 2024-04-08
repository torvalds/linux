/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_INDEX_PAGE_MAP_H
#define UDS_INDEX_PAGE_MAP_H

#include "geometry.h"
#include "io-factory.h"

/*
 * The index maintains a page map which records how the chapter delta lists are distributed among
 * the index pages for each chapter, allowing the volume to be efficient about reading only pages
 * that it knows it will need.
 */

struct index_page_map {
	const struct index_geometry *geometry;
	u64 last_update;
	u32 entries_per_chapter;
	u16 *entries;
};

int __must_check uds_make_index_page_map(const struct index_geometry *geometry,
					 struct index_page_map **map_ptr);

void uds_free_index_page_map(struct index_page_map *map);

int __must_check uds_read_index_page_map(struct index_page_map *map,
					 struct buffered_reader *reader);

int __must_check uds_write_index_page_map(struct index_page_map *map,
					  struct buffered_writer *writer);

void uds_update_index_page_map(struct index_page_map *map, u64 virtual_chapter_number,
			       u32 chapter_number, u32 index_page_number,
			       u32 delta_list_number);

u32 __must_check uds_find_index_page_number(const struct index_page_map *map,
					    const struct uds_record_name *name,
					    u32 chapter_number);

void uds_get_list_number_bounds(const struct index_page_map *map, u32 chapter_number,
				u32 index_page_number, u32 *lowest_list,
				u32 *highest_list);

u64 uds_compute_index_page_map_save_size(const struct index_geometry *geometry);

#endif /* UDS_INDEX_PAGE_MAP_H */
