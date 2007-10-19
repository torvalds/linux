/*
 * Device Mapper Uevent Support (dm-uevent)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2007
 * 	Author: Mike Anderson <andmike@linux.vnet.ibm.com>
 */
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/kobject.h>

#include "dm.h"
#include "dm-uevent.h"

#define DM_MSG_PREFIX "uevent"

static struct kmem_cache *_dm_event_cache;

struct dm_uevent {
	struct mapped_device *md;
	enum kobject_action action;
	struct kobj_uevent_env ku_env;
	struct list_head elist;
};

static void dm_uevent_free(struct dm_uevent *event)
{
	kmem_cache_free(_dm_event_cache, event);
}

static struct dm_uevent *dm_uevent_alloc(struct mapped_device *md)
{
	struct dm_uevent *event;

	event = kmem_cache_zalloc(_dm_event_cache, GFP_ATOMIC);
	if (!event)
		return NULL;

	INIT_LIST_HEAD(&event->elist);
	event->md = md;

	return event;
}

int dm_uevent_init(void)
{
	_dm_event_cache = KMEM_CACHE(dm_uevent, 0);
	if (!_dm_event_cache)
		return -ENOMEM;

	DMINFO("version 1.0.3");

	return 0;
}

void dm_uevent_exit(void)
{
	kmem_cache_destroy(_dm_event_cache);
}
