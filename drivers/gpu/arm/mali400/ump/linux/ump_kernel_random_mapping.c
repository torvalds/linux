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
#include "ump_osk.h"
#include "ump_kernel_common.h"
#include "ump_kernel_types.h"
#include "ump_kernel_random_mapping.h"

#include <linux/random.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/jiffies.h>


static ump_dd_mem *search(struct rb_root *root, int id)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		ump_dd_mem *e = container_of(node, ump_dd_mem, node);

		if (id < e->secure_id) {
			node = node->rb_left;
		} else if (id > e->secure_id) {
			node = node->rb_right;
		} else {
			return e;
		}
	}

	return NULL;
}

static mali_bool insert(struct rb_root *root, int id, ump_dd_mem *mem)
{
	struct rb_node **new = &(root->rb_node);
	struct rb_node *parent = NULL;

	while (*new) {
		ump_dd_mem *this = container_of(*new, ump_dd_mem, node);

		parent = *new;
		if (id < this->secure_id) {
			new = &((*new)->rb_left);
		} else if (id > this->secure_id) {
			new = &((*new)->rb_right);
		} else {
			printk(KERN_ERR "UMP: ID already used %x\n", id);
			return MALI_FALSE;
		}
	}

	rb_link_node(&mem->node, parent, new);
	rb_insert_color(&mem->node, root);

	return MALI_TRUE;
}


ump_random_mapping *ump_random_mapping_create(void)
{
	ump_random_mapping *map = _mali_osk_calloc(1, sizeof(ump_random_mapping));

	if (NULL == map)
		return NULL;

	map->lock = _mali_osk_mutex_rw_init(_MALI_OSK_LOCKFLAG_ORDERED,
					    _MALI_OSK_LOCK_ORDER_DESCRIPTOR_MAP);
	if (NULL != map->lock) {
		map->root = RB_ROOT;
#if UMP_RANDOM_MAP_DELAY
		map->failed.count = 0;
		map->failed.timestamp = jiffies;
#endif
		return map;
	}
	return NULL;
}

void ump_random_mapping_destroy(ump_random_mapping *map)
{
	_mali_osk_mutex_rw_term(map->lock);
	_mali_osk_free(map);
}

int ump_random_mapping_insert(ump_random_mapping *map, ump_dd_mem *mem)
{
	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RW);

	while (1) {
		u32 id;

		get_random_bytes(&id, sizeof(id));

		/* Try a new random number if id happened to be the invalid
		 * secure ID (-1). */
		if (unlikely(id == UMP_INVALID_SECURE_ID))
			continue;

		/* Insert into the tree. If the id was already in use, get a
		 * new random id and try again. */
		if (insert(&map->root, id, mem)) {
			mem->secure_id = id;
			break;
		}
	}
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RW);

	return 0;
}

ump_dd_mem *ump_random_mapping_get(ump_random_mapping *map, int id)
{
	ump_dd_mem *mem = NULL;
#if UMP_RANDOM_MAP_DELAY
	int do_delay = 0;
#endif

	DEBUG_ASSERT(map);

	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RO);
	mem = search(&map->root, id);

	if (unlikely(NULL == mem)) {
#if UMP_RANDOM_MAP_DELAY
		map->failed.count++;

		if (time_is_before_jiffies(map->failed.timestamp +
					   UMP_FAILED_LOOKUP_DELAY * HZ)) {
			/* If it is a long time since last failure, reset
			 * the counter and skip the delay this time. */
			map->failed.count = 0;
		} else if (map->failed.count > UMP_FAILED_LOOKUPS_ALLOWED) {
			do_delay = 1;
		}

		map->failed.timestamp = jiffies;
#endif /* UMP_RANDOM_MAP_DELAY */
	} else {
		ump_dd_reference_add(mem);
	}
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RO);

#if UMP_RANDOM_MAP_DELAY
	if (do_delay) {
		/* Apply delay */
		schedule_timeout_killable(UMP_FAILED_LOOKUP_DELAY);
	}
#endif /* UMP_RANDOM_MAP_DELAY */

	return mem;
}

static ump_dd_mem *ump_random_mapping_remove_internal(ump_random_mapping *map, int id)
{
	ump_dd_mem *mem = NULL;

	mem = search(&map->root, id);

	if (mem) {
		rb_erase(&mem->node, &map->root);
	}

	return mem;
}

void ump_random_mapping_put(ump_dd_mem *mem)
{
	int new_ref;

	_mali_osk_mutex_rw_wait(device.secure_id_map->lock, _MALI_OSK_LOCKMODE_RW);

	new_ref = _ump_osk_atomic_dec_and_read(&mem->ref_count);
	DBG_MSG(5, ("Memory reference decremented. ID: %u, new value: %d\n",
		    mem->secure_id, new_ref));

	if (0 == new_ref) {
		DBG_MSG(3, ("Final release of memory. ID: %u\n", mem->secure_id));

		ump_random_mapping_remove_internal(device.secure_id_map, mem->secure_id);

		mem->release_func(mem->ctx, mem);
		_mali_osk_free(mem);
	}

	_mali_osk_mutex_rw_signal(device.secure_id_map->lock, _MALI_OSK_LOCKMODE_RW);
}

ump_dd_mem *ump_random_mapping_remove(ump_random_mapping *map, int descriptor)
{
	ump_dd_mem *mem;

	_mali_osk_mutex_rw_wait(map->lock, _MALI_OSK_LOCKMODE_RW);
	mem = ump_random_mapping_remove_internal(map, descriptor);
	_mali_osk_mutex_rw_signal(map->lock, _MALI_OSK_LOCKMODE_RW);

	return mem;
}
