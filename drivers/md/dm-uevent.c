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
#include <linux/dm-ioctl.h>

#include "dm.h"
#include "dm-uevent.h"

#define DM_MSG_PREFIX "uevent"

static const struct {
	enum dm_uevent_type type;
	enum kobject_action action;
	char *name;
} _dm_uevent_type_names[] = {
	{DM_UEVENT_PATH_FAILED, KOBJ_CHANGE, "PATH_FAILED"},
	{DM_UEVENT_PATH_REINSTATED, KOBJ_CHANGE, "PATH_REINSTATED"},
};

static struct kmem_cache *_dm_event_cache;

struct dm_uevent {
	struct mapped_device *md;
	enum kobject_action action;
	struct kobj_uevent_env ku_env;
	struct list_head elist;
	char name[DM_NAME_LEN];
	char uuid[DM_UUID_LEN];
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

static struct dm_uevent *dm_build_path_uevent(struct mapped_device *md,
					      struct dm_target *ti,
					      enum kobject_action action,
					      const char *dm_action,
					      const char *path,
					      unsigned nr_valid_paths)
{
	struct dm_uevent *event;

	event = dm_uevent_alloc(md);
	if (!event) {
		DMERR("%s: dm_uevent_alloc() failed", __func__);
		goto err_nomem;
	}

	event->action = action;

	if (add_uevent_var(&event->ku_env, "DM_TARGET=%s", ti->type->name)) {
		DMERR("%s: add_uevent_var() for DM_TARGET failed",
		      __func__);
		goto err_add;
	}

	if (add_uevent_var(&event->ku_env, "DM_ACTION=%s", dm_action)) {
		DMERR("%s: add_uevent_var() for DM_ACTION failed",
		      __func__);
		goto err_add;
	}

	if (add_uevent_var(&event->ku_env, "DM_SEQNUM=%u",
			   dm_next_uevent_seq(md))) {
		DMERR("%s: add_uevent_var() for DM_SEQNUM failed",
		      __func__);
		goto err_add;
	}

	if (add_uevent_var(&event->ku_env, "DM_PATH=%s", path)) {
		DMERR("%s: add_uevent_var() for DM_PATH failed", __func__);
		goto err_add;
	}

	if (add_uevent_var(&event->ku_env, "DM_NR_VALID_PATHS=%d",
			   nr_valid_paths)) {
		DMERR("%s: add_uevent_var() for DM_NR_VALID_PATHS failed",
		      __func__);
		goto err_add;
	}

	return event;

err_add:
	dm_uevent_free(event);
err_nomem:
	return ERR_PTR(-ENOMEM);
}

/**
 * dm_send_uevents - send uevents for given list
 *
 * @events:	list of events to send
 * @kobj:	kobject generating event
 *
 */
void dm_send_uevents(struct list_head *events, struct kobject *kobj)
{
	int r;
	struct dm_uevent *event, *next;

	list_for_each_entry_safe(event, next, events, elist) {
		list_del_init(&event->elist);

		/*
		 * Need to call dm_copy_name_and_uuid from here for now.
		 * Context of previous var adds and locking used for
		 * hash_cell not compatable.
		 */
		if (dm_copy_name_and_uuid(event->md, event->name,
					  event->uuid)) {
			DMERR("%s: dm_copy_name_and_uuid() failed",
			      __func__);
			goto uevent_free;
		}

		if (add_uevent_var(&event->ku_env, "DM_NAME=%s", event->name)) {
			DMERR("%s: add_uevent_var() for DM_NAME failed",
			      __func__);
			goto uevent_free;
		}

		if (add_uevent_var(&event->ku_env, "DM_UUID=%s", event->uuid)) {
			DMERR("%s: add_uevent_var() for DM_UUID failed",
			      __func__);
			goto uevent_free;
		}

		r = kobject_uevent_env(kobj, event->action, event->ku_env.envp);
		if (r)
			DMERR("%s: kobject_uevent_env failed", __func__);
uevent_free:
		dm_uevent_free(event);
	}
}
EXPORT_SYMBOL_GPL(dm_send_uevents);

/**
 * dm_path_uevent - called to create a new path event and queue it
 *
 * @event_type:	path event type enum
 * @ti:			pointer to a dm_target
 * @path:		string containing pathname
 * @nr_valid_paths:	number of valid paths remaining
 *
 */
void dm_path_uevent(enum dm_uevent_type event_type, struct dm_target *ti,
		   const char *path, unsigned nr_valid_paths)
{
	struct mapped_device *md = dm_table_get_md(ti->table);
	struct dm_uevent *event;

	if (event_type >= ARRAY_SIZE(_dm_uevent_type_names)) {
		DMERR("%s: Invalid event_type %d", __func__, event_type);
		goto out;
	}

	event = dm_build_path_uevent(md, ti,
				     _dm_uevent_type_names[event_type].action,
				     _dm_uevent_type_names[event_type].name,
				     path, nr_valid_paths);
	if (IS_ERR(event))
		goto out;

	dm_uevent_add(md, &event->elist);

out:
	dm_put(md);
}
EXPORT_SYMBOL_GPL(dm_path_uevent);

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
