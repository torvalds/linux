/*
 * Copyright (C) 2010-2011, 2013-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_kernel_random_mapping.h
 */

#ifndef __UMP_KERNEL_RANDOM_MAPPING_H__
#define __UMP_KERNEL_RANDOM_MAPPING_H__

#include "mali_osk.h"
#include <linux/rbtree.h>

#define UMP_RANDOM_MAP_DELAY 1
#define UMP_FAILED_LOOKUP_DELAY 10 /* ms */
#define UMP_FAILED_LOOKUPS_ALLOWED 10 /* number of allowed failed lookups */

/**
 * The random mapping object
 * Provides a separate namespace where we can map an integer to a pointer
 */
typedef struct ump_random_mapping {
	_mali_osk_mutex_rw_t *lock; /**< Lock protecting access to the mapping object */
	struct rb_root root;
#if UMP_RANDOM_MAP_DELAY
	struct {
		unsigned long count;
		unsigned long timestamp;
	} failed;
#endif
} ump_random_mapping;

/**
 * Create a random mapping object
 * Create a random mapping capable of holding 2^20 entries
 * @return Pointer to a random mapping object, NULL on failure
 */
ump_random_mapping *ump_random_mapping_create(void);

/**
 * Destroy a random mapping object
 * @param map The map to free
 */
void ump_random_mapping_destroy(ump_random_mapping *map);

/**
 * Allocate a new mapping entry (random ID)
 * Allocates a new entry in the map.
 * @param map The map to allocate a new entry in
 * @param target The value to map to
 * @return The random allocated, a negative value on error
 */
int ump_random_mapping_insert(ump_random_mapping *map, ump_dd_mem *mem);

/**
 * Get the value mapped to by a random ID
 *
 * If the lookup fails, punish the calling thread by applying a delay.
 *
 * @param map The map to lookup the random id in
 * @param id The ID to lookup
 * @param target Pointer to a pointer which will receive the stored value
 * @return ump_dd_mem pointer on successful lookup, NULL on error
 */
ump_dd_mem *ump_random_mapping_get(ump_random_mapping *map, int id);

void ump_random_mapping_put(ump_dd_mem *mem);

/**
 * Free the random ID
 * For the random to be reused it has to be freed
 * @param map The map to free the random from
 * @param id The ID to free
 */
ump_dd_mem *ump_random_mapping_remove(ump_random_mapping *map, int id);

#endif /* __UMP_KERNEL_RANDOM_MAPPING_H__ */
