/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_POINTER_MAP_H
#define VDO_POINTER_MAP_H

#include <linux/compiler.h>
#include <linux/types.h>

/*
 * A pointer_map associates pointer values (<code>void *</code>) with the data referenced by
 * pointer keys (<code>void *</code>). <code>NULL</code> pointer values are not supported. A
 * <code>NULL</code> key value is supported when the instance's key comparator and hasher functions
 * support it.
 *
 * The map is implemented as hash table, which should provide constant-time insert, query, and
 * remove operations, although the insert may occasionally grow the table, which is linear in the
 * number of entries in the map. The table will grow as needed to hold new entries, but will not
 * shrink as entries are removed.
 *
 * The key and value pointers passed to the map are retained and used by the map, but are not owned
 * by the map. Freeing the map does not attempt to free the pointers. The client is entirely
 * responsible for the memory management of the keys and values. The current interface and
 * implementation assume that keys will be properties of the values, or that keys will not be
 * memory managed, or that keys will not need to be freed as a result of being replaced when a key
 * is re-mapped.
 */

struct pointer_map;

/**
 * typedef pointer_key_comparator - The prototype of functions that compare the referents of two
 *                                  pointer keys for equality.
 * @this_key: The first element to compare.
 * @that_key: The second element to compare.
 *
 * If two keys are equal, then both keys must have the same the hash code associated with them by
 * the hasher function defined below.
 *
 * Return: true if and only if the referents of the two key pointers are to be treated as the same
 *         key by the map.
 */
typedef bool pointer_key_comparator(const void *this_key, const void *that_key);

/**
 * typedef pointer_key_hasher - The prototype of functions that get or calculate a hash code
 *                              associated with the referent of pointer key.
 * @key: The pointer key to hash.
 *
 * The hash code must be uniformly distributed over all u32 values. The hash code associated
 * with a given key must not change while the key is in the map. If the comparator function says
 * two keys are equal, then this function must return the same hash code for both keys. This
 * function may be called many times for a key while an entry is stored for it in the map.
 *
 * Return: The hash code for the key.
 */
typedef u32 pointer_key_hasher(const void *key);

int __must_check vdo_make_pointer_map(size_t initial_capacity,
				      unsigned int initial_load,
				      pointer_key_comparator comparator,
				      pointer_key_hasher hasher,
				      struct pointer_map **map_ptr);

void vdo_free_pointer_map(struct pointer_map *map);

size_t vdo_pointer_map_size(const struct pointer_map *map);

void *vdo_pointer_map_get(struct pointer_map *map, const void *key);

int __must_check vdo_pointer_map_put(struct pointer_map *map,
				     const void *key,
				     void *new_value,
				     bool update,
				     void **old_value_ptr);

void *vdo_pointer_map_remove(struct pointer_map *map, const void *key);

#endif /* VDO_POINTER_MAP_H */
