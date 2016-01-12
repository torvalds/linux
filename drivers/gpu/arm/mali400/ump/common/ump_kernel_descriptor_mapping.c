/*
 * Copyright (C) 2010-2011, 2013-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_bitops.h"
#include "ump_kernel_common.h"
#include "ump_kernel_descriptor_mapping.h"

#define MALI_PAD_INT(x) (((x) + (BITS_PER_LONG - 1)) & ~(BITS_PER_LONG - 1))

/**
 * Allocate a descriptor table capable of holding 'count' mappings
 * @param count Number of mappings in the table
 * @return Pointer to a new table, NULL on error
 */
static ump_descriptor_table *descriptor_table_alloc(int count);

/**
 * Free a descriptor table
 * @param table The table to free
 */
static void descriptor_table_free(ump_descriptor_table *table);

ump_descriptor_mapping *ump_descriptor_mapping_create(int init_entries, int max_entries)
{
	ump_descriptor_mapping *map = _mali_osk_calloc(1, sizeof(ump_descriptor_mapping));

	init_entries = MALI_PAD_INT(init_entries);
	max_entries = MALI_PAD_INT(max_entries);

	if (NULL != map) {
		map->table = descriptor_table_alloc(init_entries);
		if (NULL != map->table) {
			map->lock = _mali_osk_mutex_rw_init(_MALI_OSK_LOCKFLAG_UNORDERED, 0);
			if (NULL != map->lock) {
				_mali_osk_set_nonatomic_bit(0, map->table->usage); /* reserve bit 0 to prevent NULL/zero logic to kick in */
				map->max_nr_mappings_allowed = max_entries;
				map->current_nr_mappings = init_entries;
				return map;
			}
			descriptor_table_free(map->table);
		}
		_mali_osk_free(map);
	}
	return NULL;
}

void ump_descriptor_mapping_destroy(ump_descriptor_mapping *map)
{
	descriptor_table_free(map->table);
	_mali_osk_mutex_rw_term(map->lock);
	_mali_osk_free(map);
}

int ump_descriptor_mapping_allocate_mapping(ump_descriptor_mapping *map, void *target)
{
	int descriptor = -1;/*-EFAULT;*/
	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RW);
	descriptor = _mali_osk_find_first_zero_bit(map->table->usage, map->current_nr_mappings);
	if (descriptor == map->current_nr_mappings) {
		int nr_mappings_new;
		/* no free descriptor, try to expand the table */
		ump_descriptor_table *new_table;
		ump_descriptor_table *old_table = map->table;
		nr_mappings_new = map->current_nr_mappings * 2;

		if (map->current_nr_mappings >= map->max_nr_mappings_allowed) {
			descriptor = -1;
			goto unlock_and_exit;
		}

		new_table = descriptor_table_alloc(nr_mappings_new);
		if (NULL == new_table) {
			descriptor = -1;
			goto unlock_and_exit;
		}

		_mali_osk_memcpy(new_table->usage, old_table->usage, (sizeof(unsigned long)*map->current_nr_mappings) / BITS_PER_LONG);
		_mali_osk_memcpy(new_table->mappings, old_table->mappings, map->current_nr_mappings * sizeof(void *));
		map->table = new_table;
		map->current_nr_mappings = nr_mappings_new;
		descriptor_table_free(old_table);
	}

	/* we have found a valid descriptor, set the value and usage bit */
	_mali_osk_set_nonatomic_bit(descriptor, map->table->usage);
	map->table->mappings[descriptor] = target;

unlock_and_exit:
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RW);
	return descriptor;
}

int ump_descriptor_mapping_get(ump_descriptor_mapping *map, int descriptor, void **target)
{
	int result = -1;/*-EFAULT;*/
	DEBUG_ASSERT(map);
	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RO);
	if ((descriptor > 0) && (descriptor < map->current_nr_mappings) && _mali_osk_test_bit(descriptor, map->table->usage)) {
		*target = map->table->mappings[descriptor];
		result = 0;
	} else *target = NULL;
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RO);
	return result;
}

int ump_descriptor_mapping_set(ump_descriptor_mapping *map, int descriptor, void *target)
{
	int result = -1;/*-EFAULT;*/
	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RO);
	if ((descriptor > 0) && (descriptor < map->current_nr_mappings) && _mali_osk_test_bit(descriptor, map->table->usage)) {
		map->table->mappings[descriptor] = target;
		result = 0;
	}
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RO);
	return result;
}

void ump_descriptor_mapping_free(ump_descriptor_mapping *map, int descriptor)
{
	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RW);
	if ((descriptor > 0) && (descriptor < map->current_nr_mappings) && _mali_osk_test_bit(descriptor, map->table->usage)) {
		map->table->mappings[descriptor] = NULL;
		_mali_osk_clear_nonatomic_bit(descriptor, map->table->usage);
	}
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RW);
}

static ump_descriptor_table *descriptor_table_alloc(int count)
{
	ump_descriptor_table *table;

	table = _mali_osk_calloc(1, sizeof(ump_descriptor_table) + ((sizeof(unsigned long) * count) / BITS_PER_LONG) + (sizeof(void *) * count));

	if (NULL != table) {
		table->usage = (u32 *)((u8 *)table + sizeof(ump_descriptor_table));
		table->mappings = (void **)((u8 *)table + sizeof(ump_descriptor_table) + ((sizeof(unsigned long) * count) / BITS_PER_LONG));
	}

	return table;
}

static void descriptor_table_free(ump_descriptor_table *table)
{
	_mali_osk_free(table);
}

