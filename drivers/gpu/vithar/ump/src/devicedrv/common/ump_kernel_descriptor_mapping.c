/*
 *
 * (C) COPYRIGHT 2008-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */




#include <common/ump_kernel_descriptor_mapping.h>
#include <common/ump_kernel_priv.h>

#define MALI_PAD_INT(x) (((x) + (BITS_PER_LONG - 1)) & ~(BITS_PER_LONG - 1))

/**
 * Allocate a descriptor table capable of holding 'count' mappings
 * @param count Number of mappings in the table
 * @return Pointer to a new table, NULL on error
 */
static umpp_descriptor_table * descriptor_table_alloc(unsigned int count);

/**
 * Free a descriptor table
 * @param table The table to free
 */
static void descriptor_table_free(umpp_descriptor_table * table);

umpp_descriptor_mapping * umpp_descriptor_mapping_create(unsigned int init_entries, unsigned int max_entries)
{
	umpp_descriptor_mapping * map = kzalloc(sizeof(umpp_descriptor_mapping), GFP_KERNEL);

	init_entries = MALI_PAD_INT(init_entries);
	max_entries = MALI_PAD_INT(max_entries);

	if (NULL != map)
	{
		map->table = descriptor_table_alloc(init_entries);
		if (NULL != map->table)
		{
			init_rwsem( &map->lock);
			set_bit(0, map->table->usage);
			map->max_nr_mappings_allowed = max_entries;
			map->current_nr_mappings = init_entries;
			return map;

			descriptor_table_free(map->table);
		}
		kfree(map);
	}
	return NULL;
}

void umpp_descriptor_mapping_destroy(umpp_descriptor_mapping * map)
{
	UMP_ASSERT(NULL != map);
	descriptor_table_free(map->table);
	kfree(map);
}

unsigned int umpp_descriptor_mapping_allocate(umpp_descriptor_mapping * map, void * target)
{
 	int descriptor = 0;
	UMP_ASSERT(NULL != map);
	down_write( &map->lock);
	descriptor = find_first_zero_bit(map->table->usage, map->current_nr_mappings);
	if (descriptor == map->current_nr_mappings)
	{
		/* no free descriptor, try to expand the table */
		umpp_descriptor_table * new_table;
		umpp_descriptor_table * old_table = map->table;
		int nr_mappings_new = map->current_nr_mappings + BITS_PER_LONG;

		if (map->current_nr_mappings >= map->max_nr_mappings_allowed)
		{
			descriptor = 0;
			goto unlock_and_exit;
		}

		new_table = descriptor_table_alloc(nr_mappings_new);
		if (NULL == new_table)
		{
			descriptor = 0;
			goto unlock_and_exit;
		}

 		memcpy(new_table->usage, old_table->usage, (sizeof(unsigned long)*map->current_nr_mappings) / BITS_PER_LONG);
 		memcpy(new_table->mappings, old_table->mappings, map->current_nr_mappings * sizeof(void*));

 		map->table = new_table;
		map->current_nr_mappings = nr_mappings_new;
		descriptor_table_free(old_table);
	}

	/* we have found a valid descriptor, set the value and usage bit */
	set_bit(descriptor, map->table->usage);
	map->table->mappings[descriptor] = target;

unlock_and_exit:
	up_write(&map->lock);
	return descriptor;
}

int umpp_descriptor_mapping_lookup(umpp_descriptor_mapping * map, unsigned int descriptor, void** target)
{
	int result = -EINVAL;
 	UMP_ASSERT(map);
	UMP_ASSERT(target);
 	down_read(&map->lock);
 	if ( (descriptor > 0) && (descriptor < map->current_nr_mappings) && test_bit(descriptor, map->table->usage) )
 	{
		*target = map->table->mappings[descriptor];
		result = 0;
	}
	/* keep target untouched if the descriptor was not found */
	up_read(&map->lock);
	return result;
}

void umpp_descriptor_mapping_remove(umpp_descriptor_mapping * map, unsigned int descriptor)
{
	UMP_ASSERT(map);
 	down_write(&map->lock);
 	if ( (descriptor > 0) && (descriptor < map->current_nr_mappings) && test_bit(descriptor, map->table->usage) )
 	{
		map->table->mappings[descriptor] = NULL;
		clear_bit(descriptor, map->table->usage);
	}
	up_write(&map->lock);
}

static umpp_descriptor_table * descriptor_table_alloc(unsigned int count)
{
	umpp_descriptor_table * table;

	table = kzalloc(sizeof(umpp_descriptor_table) + ((sizeof(unsigned long) * count)/BITS_PER_LONG) + (sizeof(void*) * count), __GFP_HARDWALL | GFP_KERNEL );

	if (NULL != table)
	{
		table->usage = (unsigned long*)((u8*)table + sizeof(umpp_descriptor_table));
		table->mappings = (void**)((u8*)table + sizeof(umpp_descriptor_table) + ((sizeof(unsigned long) * count)/BITS_PER_LONG));
	}

	return table;
}

static void descriptor_table_free(umpp_descriptor_table * table)
{
 	UMP_ASSERT(table);
	kfree(table);
}

