/*
 * Copyright (C) 2010, 2012-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_kernel_descriptor_mapping.h"
#include "mali_osk.h"
#include "mali_osk_bitops.h"
#include "mali_memory_types.h"
#include "mali_session.h"

#define MALI_PAD_INT(x) (((x) + (BITS_PER_LONG - 1)) & ~(BITS_PER_LONG - 1))

/**
 * Allocate a descriptor table capable of holding 'count' mappings
 * @param count Number of mappings in the table
 * @return Pointer to a new table, NULL on error
 */
static mali_descriptor_table *descriptor_table_alloc(int count);

/**
 * Free a descriptor table
 * @param table The table to free
 */
static void descriptor_table_free(mali_descriptor_table *table);

mali_descriptor_mapping *mali_descriptor_mapping_create(int init_entries, int max_entries)
{
	mali_descriptor_mapping *map = _mali_osk_calloc(1, sizeof(mali_descriptor_mapping));

	init_entries = MALI_PAD_INT(init_entries);
	max_entries = MALI_PAD_INT(max_entries);

	if (NULL != map) {
		map->table = descriptor_table_alloc(init_entries);
		if (NULL != map->table) {
			map->lock = _mali_osk_mutex_rw_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_DESCRIPTOR_MAP);
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

void mali_descriptor_mapping_destroy(mali_descriptor_mapping *map)
{
	descriptor_table_free(map->table);
	_mali_osk_mutex_rw_term(map->lock);
	_mali_osk_free(map);
}

_mali_osk_errcode_t mali_descriptor_mapping_allocate_mapping(mali_descriptor_mapping *map, void *target, int *odescriptor)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	int new_descriptor;
	mali_mem_allocation *descriptor;
	struct mali_session_data *session;

	MALI_DEBUG_ASSERT_POINTER(map);
	MALI_DEBUG_ASSERT_POINTER(odescriptor);
	MALI_DEBUG_ASSERT_POINTER(target);

	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RW);
	new_descriptor = _mali_osk_find_first_zero_bit(map->table->usage, map->current_nr_mappings);
	if (new_descriptor == map->current_nr_mappings) {
		/* no free descriptor, try to expand the table */
		mali_descriptor_table *new_table, * old_table;
		if (map->current_nr_mappings >= map->max_nr_mappings_allowed) goto unlock_and_exit;

		map->current_nr_mappings += BITS_PER_LONG;
		new_table = descriptor_table_alloc(map->current_nr_mappings);
		if (NULL == new_table) goto unlock_and_exit;

		old_table = map->table;
		_mali_osk_memcpy(new_table->usage, old_table->usage, (sizeof(unsigned long)*map->current_nr_mappings) / BITS_PER_LONG);
		_mali_osk_memcpy(new_table->mappings, old_table->mappings, map->current_nr_mappings * sizeof(void *));
		map->table = new_table;
		descriptor_table_free(old_table);
	}

	/* we have found a valid descriptor, set the value and usage bit */
	_mali_osk_set_nonatomic_bit(new_descriptor, map->table->usage);
	map->table->mappings[new_descriptor] = target;
	*odescriptor = new_descriptor;

	/* To calculate the mali mem usage for the session */
	descriptor = (mali_mem_allocation *)target;
	session = descriptor->session;

	MALI_DEBUG_ASSERT_POINTER(session);

	session->mali_mem_array[descriptor->type] += descriptor->size;
	if ((MALI_MEM_OS == descriptor->type || MALI_MEM_BLOCK == descriptor->type) &&
	    (session->mali_mem_array[MALI_MEM_OS] + session->mali_mem_array[MALI_MEM_BLOCK] > session->max_mali_mem_allocated)) {
		session->max_mali_mem_allocated = session->mali_mem_array[MALI_MEM_OS] + session->mali_mem_array[MALI_MEM_BLOCK];
	}
	err = _MALI_OSK_ERR_OK;

unlock_and_exit:
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RW);
	MALI_ERROR(err);
}

void mali_descriptor_mapping_call_for_each(mali_descriptor_mapping *map, void (*callback)(int, void *))
{
	int i;

	MALI_DEBUG_ASSERT_POINTER(map);
	MALI_DEBUG_ASSERT_POINTER(callback);

	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RO);
	/* id 0 is skipped as it's an reserved ID not mapping to anything */
	for (i = 1; i < map->current_nr_mappings; ++i) {
		if (_mali_osk_test_bit(i, map->table->usage)) {
			callback(i, map->table->mappings[i]);
		}
	}
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RO);
}

_mali_osk_errcode_t mali_descriptor_mapping_get(mali_descriptor_mapping *map, int descriptor, void **target)
{
	_mali_osk_errcode_t result = _MALI_OSK_ERR_FAULT;
	MALI_DEBUG_ASSERT_POINTER(map);
	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RO);
	if ((descriptor >= 0) && (descriptor < map->current_nr_mappings) && _mali_osk_test_bit(descriptor, map->table->usage)) {
		*target = map->table->mappings[descriptor];
		result = _MALI_OSK_ERR_OK;
	} else *target = NULL;
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RO);
	MALI_ERROR(result);
}

_mali_osk_errcode_t mali_descriptor_mapping_set(mali_descriptor_mapping *map, int descriptor, void *target)
{
	_mali_osk_errcode_t result = _MALI_OSK_ERR_FAULT;
	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RO);
	if ((descriptor >= 0) && (descriptor < map->current_nr_mappings) && _mali_osk_test_bit(descriptor, map->table->usage)) {
		map->table->mappings[descriptor] = target;
		result = _MALI_OSK_ERR_OK;
	}
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RO);
	MALI_ERROR(result);
}

void *mali_descriptor_mapping_free(mali_descriptor_mapping *map, int descriptor)
{
	void *old_value = NULL;
	mali_mem_allocation *tmp_descriptor;
	struct mali_session_data *session;

	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RW);
	if ((descriptor >= 0) && (descriptor < map->current_nr_mappings) && _mali_osk_test_bit(descriptor, map->table->usage)) {
		old_value = map->table->mappings[descriptor];
		map->table->mappings[descriptor] = NULL;
		_mali_osk_clear_nonatomic_bit(descriptor, map->table->usage);
	}
	if (NULL != old_value) {
		tmp_descriptor = (mali_mem_allocation *)old_value;
		session = tmp_descriptor->session;

		MALI_DEBUG_ASSERT_POINTER(session);

		MALI_DEBUG_ASSERT(session->mali_mem_array[tmp_descriptor->type] >= tmp_descriptor->size);

		session->mali_mem_array[tmp_descriptor->type] -= tmp_descriptor->size;
	}
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RW);

	return old_value;
}

static mali_descriptor_table *descriptor_table_alloc(int count)
{
	mali_descriptor_table *table;

	table = _mali_osk_calloc(1, sizeof(mali_descriptor_table) + ((sizeof(unsigned long) * count) / BITS_PER_LONG) + (sizeof(void *) * count));

	if (NULL != table) {
		table->usage = (u32 *)((u8 *)table + sizeof(mali_descriptor_table));
		table->mappings = (void **)((u8 *)table + sizeof(mali_descriptor_table) + ((sizeof(unsigned long) * count) / BITS_PER_LONG));
	}

	return table;
}

static void descriptor_table_free(mali_descriptor_table *table)
{
	_mali_osk_free(table);
}