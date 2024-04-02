/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_INT_MAP_H
#define VDO_INT_MAP_H

#include <linux/compiler.h>
#include <linux/types.h>

/**
 * DOC: int_map
 *
 * An int_map associates pointers (void *) with integer keys (u64). NULL pointer values are
 * not supported.
 *
 * The map is implemented as hash table, which should provide constant-time insert, query, and
 * remove operations, although the insert may occasionally grow the table, which is linear in the
 * number of entries in the map. The table will grow as needed to hold new entries, but will not
 * shrink as entries are removed.
 */

struct int_map;

int __must_check vdo_int_map_create(size_t initial_capacity, struct int_map **map_ptr);

void vdo_int_map_free(struct int_map *map);

size_t vdo_int_map_size(const struct int_map *map);

void *vdo_int_map_get(struct int_map *map, u64 key);

int __must_check vdo_int_map_put(struct int_map *map, u64 key, void *new_value,
				 bool update, void **old_value_ptr);

void *vdo_int_map_remove(struct int_map *map, u64 key);

#endif /* VDO_INT_MAP_H */
