// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 * Copyright (C) 2004 - 2006 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-core.h"
#include "dm-ima.h"
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/sched/mm.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/dm-ioctl.h>
#include <linux/hdreg.h>
#include <linux/compat.h>
#include <linux/nospec.h>

#include <linux/uaccess.h>
#include <linux/ima.h>

#define DM_MSG_PREFIX "ioctl"
#define DM_DRIVER_EMAIL "dm-devel@redhat.com"

struct dm_file {
	/*
	 * poll will wait until the global event number is greater than
	 * this value.
	 */
	volatile unsigned int global_event_nr;
};

/*
 *---------------------------------------------------------------
 * The ioctl interface needs to be able to look up devices by
 * name or uuid.
 *---------------------------------------------------------------
 */
struct hash_cell {
	struct rb_node name_node;
	struct rb_node uuid_node;
	bool name_set;
	bool uuid_set;

	char *name;
	char *uuid;
	struct mapped_device *md;
	struct dm_table *new_map;
};

struct vers_iter {
	size_t param_size;
	struct dm_target_versions *vers, *old_vers;
	char *end;
	uint32_t flags;
};


static struct rb_root name_rb_tree = RB_ROOT;
static struct rb_root uuid_rb_tree = RB_ROOT;

static void dm_hash_remove_all(bool keep_open_devices, bool mark_deferred, bool only_deferred);

/*
 * Guards access to both hash tables.
 */
static DECLARE_RWSEM(_hash_lock);

/*
 * Protects use of mdptr to obtain hash cell name and uuid from mapped device.
 */
static DEFINE_MUTEX(dm_hash_cells_mutex);

static void dm_hash_exit(void)
{
	dm_hash_remove_all(false, false, false);
}

/*
 *---------------------------------------------------------------
 * Code for looking up a device by name
 *---------------------------------------------------------------
 */
static struct hash_cell *__get_name_cell(const char *str)
{
	struct rb_node *n = name_rb_tree.rb_node;

	while (n) {
		struct hash_cell *hc = container_of(n, struct hash_cell, name_node);
		int c;

		c = strcmp(hc->name, str);
		if (!c) {
			dm_get(hc->md);
			return hc;
		}
		n = c >= 0 ? n->rb_left : n->rb_right;
	}

	return NULL;
}

static struct hash_cell *__get_uuid_cell(const char *str)
{
	struct rb_node *n = uuid_rb_tree.rb_node;

	while (n) {
		struct hash_cell *hc = container_of(n, struct hash_cell, uuid_node);
		int c;

		c = strcmp(hc->uuid, str);
		if (!c) {
			dm_get(hc->md);
			return hc;
		}
		n = c >= 0 ? n->rb_left : n->rb_right;
	}

	return NULL;
}

static void __unlink_name(struct hash_cell *hc)
{
	if (hc->name_set) {
		hc->name_set = false;
		rb_erase(&hc->name_node, &name_rb_tree);
	}
}

static void __unlink_uuid(struct hash_cell *hc)
{
	if (hc->uuid_set) {
		hc->uuid_set = false;
		rb_erase(&hc->uuid_node, &uuid_rb_tree);
	}
}

static void __link_name(struct hash_cell *new_hc)
{
	struct rb_node **n, *parent;

	__unlink_name(new_hc);

	new_hc->name_set = true;

	n = &name_rb_tree.rb_node;
	parent = NULL;

	while (*n) {
		struct hash_cell *hc = container_of(*n, struct hash_cell, name_node);
		int c;

		c = strcmp(hc->name, new_hc->name);
		BUG_ON(!c);
		parent = *n;
		n = c >= 0 ? &hc->name_node.rb_left : &hc->name_node.rb_right;
	}

	rb_link_node(&new_hc->name_node, parent, n);
	rb_insert_color(&new_hc->name_node, &name_rb_tree);
}

static void __link_uuid(struct hash_cell *new_hc)
{
	struct rb_node **n, *parent;

	__unlink_uuid(new_hc);

	new_hc->uuid_set = true;

	n = &uuid_rb_tree.rb_node;
	parent = NULL;

	while (*n) {
		struct hash_cell *hc = container_of(*n, struct hash_cell, uuid_node);
		int c;

		c = strcmp(hc->uuid, new_hc->uuid);
		BUG_ON(!c);
		parent = *n;
		n = c > 0 ? &hc->uuid_node.rb_left : &hc->uuid_node.rb_right;
	}

	rb_link_node(&new_hc->uuid_node, parent, n);
	rb_insert_color(&new_hc->uuid_node, &uuid_rb_tree);
}

static struct hash_cell *__get_dev_cell(uint64_t dev)
{
	struct mapped_device *md;
	struct hash_cell *hc;

	md = dm_get_md(huge_decode_dev(dev));
	if (!md)
		return NULL;

	hc = dm_get_mdptr(md);
	if (!hc) {
		dm_put(md);
		return NULL;
	}

	return hc;
}

/*
 *---------------------------------------------------------------
 * Inserting, removing and renaming a device.
 *---------------------------------------------------------------
 */
static struct hash_cell *alloc_cell(const char *name, const char *uuid,
				    struct mapped_device *md)
{
	struct hash_cell *hc;

	hc = kmalloc(sizeof(*hc), GFP_KERNEL);
	if (!hc)
		return NULL;

	hc->name = kstrdup(name, GFP_KERNEL);
	if (!hc->name) {
		kfree(hc);
		return NULL;
	}

	if (!uuid)
		hc->uuid = NULL;

	else {
		hc->uuid = kstrdup(uuid, GFP_KERNEL);
		if (!hc->uuid) {
			kfree(hc->name);
			kfree(hc);
			return NULL;
		}
	}

	hc->name_set = hc->uuid_set = false;
	hc->md = md;
	hc->new_map = NULL;
	return hc;
}

static void free_cell(struct hash_cell *hc)
{
	if (hc) {
		kfree(hc->name);
		kfree(hc->uuid);
		kfree(hc);
	}
}

/*
 * The kdev_t and uuid of a device can never change once it is
 * initially inserted.
 */
static int dm_hash_insert(const char *name, const char *uuid, struct mapped_device *md)
{
	struct hash_cell *cell, *hc;

	/*
	 * Allocate the new cells.
	 */
	cell = alloc_cell(name, uuid, md);
	if (!cell)
		return -ENOMEM;

	/*
	 * Insert the cell into both hash tables.
	 */
	down_write(&_hash_lock);
	hc = __get_name_cell(name);
	if (hc) {
		dm_put(hc->md);
		goto bad;
	}

	__link_name(cell);

	if (uuid) {
		hc = __get_uuid_cell(uuid);
		if (hc) {
			__unlink_name(cell);
			dm_put(hc->md);
			goto bad;
		}
		__link_uuid(cell);
	}
	dm_get(md);
	mutex_lock(&dm_hash_cells_mutex);
	dm_set_mdptr(md, cell);
	mutex_unlock(&dm_hash_cells_mutex);
	up_write(&_hash_lock);

	return 0;

 bad:
	up_write(&_hash_lock);
	free_cell(cell);
	return -EBUSY;
}

static struct dm_table *__hash_remove(struct hash_cell *hc)
{
	struct dm_table *table;
	int srcu_idx;

	lockdep_assert_held(&_hash_lock);

	/* remove from the dev trees */
	__unlink_name(hc);
	__unlink_uuid(hc);
	mutex_lock(&dm_hash_cells_mutex);
	dm_set_mdptr(hc->md, NULL);
	mutex_unlock(&dm_hash_cells_mutex);

	table = dm_get_live_table(hc->md, &srcu_idx);
	if (table)
		dm_table_event(table);
	dm_put_live_table(hc->md, srcu_idx);

	table = NULL;
	if (hc->new_map)
		table = hc->new_map;
	dm_put(hc->md);
	free_cell(hc);

	return table;
}

static void dm_hash_remove_all(bool keep_open_devices, bool mark_deferred, bool only_deferred)
{
	int dev_skipped;
	struct rb_node *n;
	struct hash_cell *hc;
	struct mapped_device *md;
	struct dm_table *t;

retry:
	dev_skipped = 0;

	down_write(&_hash_lock);

	for (n = rb_first(&name_rb_tree); n; n = rb_next(n)) {
		hc = container_of(n, struct hash_cell, name_node);
		md = hc->md;
		dm_get(md);

		if (keep_open_devices &&
		    dm_lock_for_deletion(md, mark_deferred, only_deferred)) {
			dm_put(md);
			dev_skipped++;
			continue;
		}

		t = __hash_remove(hc);

		up_write(&_hash_lock);

		if (t) {
			dm_sync_table(md);
			dm_table_destroy(t);
		}
		dm_ima_measure_on_device_remove(md, true);
		dm_put(md);
		if (likely(keep_open_devices))
			dm_destroy(md);
		else
			dm_destroy_immediate(md);

		/*
		 * Some mapped devices may be using other mapped
		 * devices, so repeat until we make no further
		 * progress.  If a new mapped device is created
		 * here it will also get removed.
		 */
		goto retry;
	}

	up_write(&_hash_lock);

	if (dev_skipped)
		DMWARN("remove_all left %d open device(s)", dev_skipped);
}

/*
 * Set the uuid of a hash_cell that isn't already set.
 */
static void __set_cell_uuid(struct hash_cell *hc, char *new_uuid)
{
	mutex_lock(&dm_hash_cells_mutex);
	hc->uuid = new_uuid;
	mutex_unlock(&dm_hash_cells_mutex);

	__link_uuid(hc);
}

/*
 * Changes the name of a hash_cell and returns the old name for
 * the caller to free.
 */
static char *__change_cell_name(struct hash_cell *hc, char *new_name)
{
	char *old_name;

	/*
	 * Rename and move the name cell.
	 */
	__unlink_name(hc);
	old_name = hc->name;

	mutex_lock(&dm_hash_cells_mutex);
	hc->name = new_name;
	mutex_unlock(&dm_hash_cells_mutex);

	__link_name(hc);

	return old_name;
}

static struct mapped_device *dm_hash_rename(struct dm_ioctl *param,
					    const char *new)
{
	char *new_data, *old_name = NULL;
	struct hash_cell *hc;
	struct dm_table *table;
	struct mapped_device *md;
	unsigned int change_uuid = (param->flags & DM_UUID_FLAG) ? 1 : 0;
	int srcu_idx;

	/*
	 * duplicate new.
	 */
	new_data = kstrdup(new, GFP_KERNEL);
	if (!new_data)
		return ERR_PTR(-ENOMEM);

	down_write(&_hash_lock);

	/*
	 * Is new free ?
	 */
	if (change_uuid)
		hc = __get_uuid_cell(new);
	else
		hc = __get_name_cell(new);

	if (hc) {
		DMERR("Unable to change %s on mapped device %s to one that already exists: %s",
		      change_uuid ? "uuid" : "name",
		      param->name, new);
		dm_put(hc->md);
		up_write(&_hash_lock);
		kfree(new_data);
		return ERR_PTR(-EBUSY);
	}

	/*
	 * Is there such a device as 'old' ?
	 */
	hc = __get_name_cell(param->name);
	if (!hc) {
		DMERR("Unable to rename non-existent device, %s to %s%s",
		      param->name, change_uuid ? "uuid " : "", new);
		up_write(&_hash_lock);
		kfree(new_data);
		return ERR_PTR(-ENXIO);
	}

	/*
	 * Does this device already have a uuid?
	 */
	if (change_uuid && hc->uuid) {
		DMERR("Unable to change uuid of mapped device %s to %s "
		      "because uuid is already set to %s",
		      param->name, new, hc->uuid);
		dm_put(hc->md);
		up_write(&_hash_lock);
		kfree(new_data);
		return ERR_PTR(-EINVAL);
	}

	if (change_uuid)
		__set_cell_uuid(hc, new_data);
	else
		old_name = __change_cell_name(hc, new_data);

	/*
	 * Wake up any dm event waiters.
	 */
	table = dm_get_live_table(hc->md, &srcu_idx);
	if (table)
		dm_table_event(table);
	dm_put_live_table(hc->md, srcu_idx);

	if (!dm_kobject_uevent(hc->md, KOBJ_CHANGE, param->event_nr, false))
		param->flags |= DM_UEVENT_GENERATED_FLAG;

	md = hc->md;

	dm_ima_measure_on_device_rename(md);

	up_write(&_hash_lock);
	kfree(old_name);

	return md;
}

void dm_deferred_remove(void)
{
	dm_hash_remove_all(true, false, true);
}

/*
 *---------------------------------------------------------------
 * Implementation of the ioctl commands
 *---------------------------------------------------------------
 */
/*
 * All the ioctl commands get dispatched to functions with this
 * prototype.
 */
typedef int (*ioctl_fn)(struct file *filp, struct dm_ioctl *param, size_t param_size);

static int remove_all(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	dm_hash_remove_all(true, !!(param->flags & DM_DEFERRED_REMOVE), false);
	param->data_size = 0;
	return 0;
}

/*
 * Round up the ptr to an 8-byte boundary.
 */
#define ALIGN_MASK 7
static inline size_t align_val(size_t val)
{
	return (val + ALIGN_MASK) & ~ALIGN_MASK;
}
static inline void *align_ptr(void *ptr)
{
	return (void *)align_val((size_t)ptr);
}

/*
 * Retrieves the data payload buffer from an already allocated
 * struct dm_ioctl.
 */
static void *get_result_buffer(struct dm_ioctl *param, size_t param_size,
			       size_t *len)
{
	param->data_start = align_ptr(param + 1) - (void *) param;

	if (param->data_start < param_size)
		*len = param_size - param->data_start;
	else
		*len = 0;

	return ((void *) param) + param->data_start;
}

static bool filter_device(struct hash_cell *hc, const char *pfx_name, const char *pfx_uuid)
{
	const char *val;
	size_t val_len, pfx_len;

	val = hc->name;
	val_len = strlen(val);
	pfx_len = strnlen(pfx_name, DM_NAME_LEN);
	if (pfx_len > val_len)
		return false;
	if (memcmp(val, pfx_name, pfx_len))
		return false;

	val = hc->uuid ? hc->uuid : "";
	val_len = strlen(val);
	pfx_len = strnlen(pfx_uuid, DM_UUID_LEN);
	if (pfx_len > val_len)
		return false;
	if (memcmp(val, pfx_uuid, pfx_len))
		return false;

	return true;
}

static int list_devices(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	struct rb_node *n;
	struct hash_cell *hc;
	size_t len, needed = 0;
	struct gendisk *disk;
	struct dm_name_list *orig_nl, *nl, *old_nl = NULL;
	uint32_t *event_nr;

	down_write(&_hash_lock);

	/*
	 * Loop through all the devices working out how much
	 * space we need.
	 */
	for (n = rb_first(&name_rb_tree); n; n = rb_next(n)) {
		hc = container_of(n, struct hash_cell, name_node);
		if (!filter_device(hc, param->name, param->uuid))
			continue;
		needed += align_val(offsetof(struct dm_name_list, name) + strlen(hc->name) + 1);
		needed += align_val(sizeof(uint32_t) * 2);
		if (param->flags & DM_UUID_FLAG && hc->uuid)
			needed += align_val(strlen(hc->uuid) + 1);
	}

	/*
	 * Grab our output buffer.
	 */
	nl = orig_nl = get_result_buffer(param, param_size, &len);
	if (len < needed || len < sizeof(nl->dev)) {
		param->flags |= DM_BUFFER_FULL_FLAG;
		goto out;
	}
	param->data_size = param->data_start + needed;

	nl->dev = 0;	/* Flags no data */

	/*
	 * Now loop through filling out the names.
	 */
	for (n = rb_first(&name_rb_tree); n; n = rb_next(n)) {
		void *uuid_ptr;

		hc = container_of(n, struct hash_cell, name_node);
		if (!filter_device(hc, param->name, param->uuid))
			continue;
		if (old_nl)
			old_nl->next = (uint32_t) ((void *) nl -
						   (void *) old_nl);
		disk = dm_disk(hc->md);
		nl->dev = huge_encode_dev(disk_devt(disk));
		nl->next = 0;
		strcpy(nl->name, hc->name);

		old_nl = nl;
		event_nr = align_ptr(nl->name + strlen(hc->name) + 1);
		event_nr[0] = dm_get_event_nr(hc->md);
		event_nr[1] = 0;
		uuid_ptr = align_ptr(event_nr + 2);
		if (param->flags & DM_UUID_FLAG) {
			if (hc->uuid) {
				event_nr[1] |= DM_NAME_LIST_FLAG_HAS_UUID;
				strcpy(uuid_ptr, hc->uuid);
				uuid_ptr = align_ptr(uuid_ptr + strlen(hc->uuid) + 1);
			} else {
				event_nr[1] |= DM_NAME_LIST_FLAG_DOESNT_HAVE_UUID;
			}
		}
		nl = uuid_ptr;
	}
	/*
	 * If mismatch happens, security may be compromised due to buffer
	 * overflow, so it's better to crash.
	 */
	BUG_ON((char *)nl - (char *)orig_nl != needed);

 out:
	up_write(&_hash_lock);
	return 0;
}

static void list_version_get_needed(struct target_type *tt, void *needed_param)
{
	size_t *needed = needed_param;

	*needed += sizeof(struct dm_target_versions);
	*needed += strlen(tt->name) + 1;
	*needed += ALIGN_MASK;
}

static void list_version_get_info(struct target_type *tt, void *param)
{
	struct vers_iter *info = param;

	/* Check space - it might have changed since the first iteration */
	if ((char *)info->vers + sizeof(tt->version) + strlen(tt->name) + 1 > info->end) {
		info->flags = DM_BUFFER_FULL_FLAG;
		return;
	}

	if (info->old_vers)
		info->old_vers->next = (uint32_t) ((void *)info->vers - (void *)info->old_vers);

	info->vers->version[0] = tt->version[0];
	info->vers->version[1] = tt->version[1];
	info->vers->version[2] = tt->version[2];
	info->vers->next = 0;
	strcpy(info->vers->name, tt->name);

	info->old_vers = info->vers;
	info->vers = align_ptr((void *)(info->vers + 1) + strlen(tt->name) + 1);
}

static int __list_versions(struct dm_ioctl *param, size_t param_size, const char *name)
{
	size_t len, needed = 0;
	struct dm_target_versions *vers;
	struct vers_iter iter_info;
	struct target_type *tt = NULL;

	if (name) {
		tt = dm_get_target_type(name);
		if (!tt)
			return -EINVAL;
	}

	/*
	 * Loop through all the devices working out how much
	 * space we need.
	 */
	if (!tt)
		dm_target_iterate(list_version_get_needed, &needed);
	else
		list_version_get_needed(tt, &needed);

	/*
	 * Grab our output buffer.
	 */
	vers = get_result_buffer(param, param_size, &len);
	if (len < needed) {
		param->flags |= DM_BUFFER_FULL_FLAG;
		goto out;
	}
	param->data_size = param->data_start + needed;

	iter_info.param_size = param_size;
	iter_info.old_vers = NULL;
	iter_info.vers = vers;
	iter_info.flags = 0;
	iter_info.end = (char *)vers + needed;

	/*
	 * Now loop through filling out the names & versions.
	 */
	if (!tt)
		dm_target_iterate(list_version_get_info, &iter_info);
	else
		list_version_get_info(tt, &iter_info);
	param->flags |= iter_info.flags;

 out:
	if (tt)
		dm_put_target_type(tt);
	return 0;
}

static int list_versions(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	return __list_versions(param, param_size, NULL);
}

static int get_target_version(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	return __list_versions(param, param_size, param->name);
}

static int check_name(const char *name)
{
	if (strchr(name, '/')) {
		DMERR("device name cannot contain '/'");
		return -EINVAL;
	}

	if (strcmp(name, DM_CONTROL_NODE) == 0 ||
	    strcmp(name, ".") == 0 ||
	    strcmp(name, "..") == 0) {
		DMERR("device name cannot be \"%s\", \".\", or \"..\"", DM_CONTROL_NODE);
		return -EINVAL;
	}

	return 0;
}

/*
 * On successful return, the caller must not attempt to acquire
 * _hash_lock without first calling dm_put_live_table, because dm_table_destroy
 * waits for this dm_put_live_table and could be called under this lock.
 */
static struct dm_table *dm_get_inactive_table(struct mapped_device *md, int *srcu_idx)
{
	struct hash_cell *hc;
	struct dm_table *table = NULL;

	/* increment rcu count, we don't care about the table pointer */
	dm_get_live_table(md, srcu_idx);

	down_read(&_hash_lock);
	hc = dm_get_mdptr(md);
	if (!hc) {
		DMERR("device has been removed from the dev hash table.");
		goto out;
	}

	table = hc->new_map;

out:
	up_read(&_hash_lock);

	return table;
}

static struct dm_table *dm_get_live_or_inactive_table(struct mapped_device *md,
						      struct dm_ioctl *param,
						      int *srcu_idx)
{
	return (param->flags & DM_QUERY_INACTIVE_TABLE_FLAG) ?
		dm_get_inactive_table(md, srcu_idx) : dm_get_live_table(md, srcu_idx);
}

/*
 * Fills in a dm_ioctl structure, ready for sending back to
 * userland.
 */
static void __dev_status(struct mapped_device *md, struct dm_ioctl *param)
{
	struct gendisk *disk = dm_disk(md);
	struct dm_table *table;
	int srcu_idx;

	param->flags &= ~(DM_SUSPEND_FLAG | DM_READONLY_FLAG |
			  DM_ACTIVE_PRESENT_FLAG | DM_INTERNAL_SUSPEND_FLAG);

	if (dm_suspended_md(md))
		param->flags |= DM_SUSPEND_FLAG;

	if (dm_suspended_internally_md(md))
		param->flags |= DM_INTERNAL_SUSPEND_FLAG;

	if (dm_test_deferred_remove_flag(md))
		param->flags |= DM_DEFERRED_REMOVE;

	param->dev = huge_encode_dev(disk_devt(disk));

	/*
	 * Yes, this will be out of date by the time it gets back
	 * to userland, but it is still very useful for
	 * debugging.
	 */
	param->open_count = dm_open_count(md);

	param->event_nr = dm_get_event_nr(md);
	param->target_count = 0;

	table = dm_get_live_table(md, &srcu_idx);
	if (table) {
		if (!(param->flags & DM_QUERY_INACTIVE_TABLE_FLAG)) {
			if (get_disk_ro(disk))
				param->flags |= DM_READONLY_FLAG;
			param->target_count = table->num_targets;
		}

		param->flags |= DM_ACTIVE_PRESENT_FLAG;
	}
	dm_put_live_table(md, srcu_idx);

	if (param->flags & DM_QUERY_INACTIVE_TABLE_FLAG) {
		int srcu_idx;

		table = dm_get_inactive_table(md, &srcu_idx);
		if (table) {
			if (!(dm_table_get_mode(table) & BLK_OPEN_WRITE))
				param->flags |= DM_READONLY_FLAG;
			param->target_count = table->num_targets;
		}
		dm_put_live_table(md, srcu_idx);
	}
}

static int dev_create(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	int r, m = DM_ANY_MINOR;
	struct mapped_device *md;

	r = check_name(param->name);
	if (r)
		return r;

	if (param->flags & DM_PERSISTENT_DEV_FLAG)
		m = MINOR(huge_decode_dev(param->dev));

	r = dm_create(m, &md);
	if (r)
		return r;

	r = dm_hash_insert(param->name, *param->uuid ? param->uuid : NULL, md);
	if (r) {
		dm_put(md);
		dm_destroy(md);
		return r;
	}

	param->flags &= ~DM_INACTIVE_PRESENT_FLAG;

	__dev_status(md, param);

	dm_put(md);

	return 0;
}

/*
 * Always use UUID for lookups if it's present, otherwise use name or dev.
 */
static struct hash_cell *__find_device_hash_cell(struct dm_ioctl *param)
{
	struct hash_cell *hc = NULL;

	if (*param->uuid) {
		if (*param->name || param->dev) {
			DMERR("Invalid ioctl structure: uuid %s, name %s, dev %llx",
			      param->uuid, param->name, (unsigned long long)param->dev);
			return NULL;
		}

		hc = __get_uuid_cell(param->uuid);
		if (!hc)
			return NULL;
	} else if (*param->name) {
		if (param->dev) {
			DMERR("Invalid ioctl structure: name %s, dev %llx",
			      param->name, (unsigned long long)param->dev);
			return NULL;
		}

		hc = __get_name_cell(param->name);
		if (!hc)
			return NULL;
	} else if (param->dev) {
		hc = __get_dev_cell(param->dev);
		if (!hc)
			return NULL;
	} else
		return NULL;

	/*
	 * Sneakily write in both the name and the uuid
	 * while we have the cell.
	 */
	strscpy(param->name, hc->name, sizeof(param->name));
	if (hc->uuid)
		strscpy(param->uuid, hc->uuid, sizeof(param->uuid));
	else
		param->uuid[0] = '\0';

	if (hc->new_map)
		param->flags |= DM_INACTIVE_PRESENT_FLAG;
	else
		param->flags &= ~DM_INACTIVE_PRESENT_FLAG;

	return hc;
}

static struct mapped_device *find_device(struct dm_ioctl *param)
{
	struct hash_cell *hc;
	struct mapped_device *md = NULL;

	down_read(&_hash_lock);
	hc = __find_device_hash_cell(param);
	if (hc)
		md = hc->md;
	up_read(&_hash_lock);

	return md;
}

static int dev_remove(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	struct hash_cell *hc;
	struct mapped_device *md;
	int r;
	struct dm_table *t;

	down_write(&_hash_lock);
	hc = __find_device_hash_cell(param);

	if (!hc) {
		DMDEBUG_LIMIT("device doesn't appear to be in the dev hash table.");
		up_write(&_hash_lock);
		return -ENXIO;
	}

	md = hc->md;

	/*
	 * Ensure the device is not open and nothing further can open it.
	 */
	r = dm_lock_for_deletion(md, !!(param->flags & DM_DEFERRED_REMOVE), false);
	if (r) {
		if (r == -EBUSY && param->flags & DM_DEFERRED_REMOVE) {
			up_write(&_hash_lock);
			dm_put(md);
			return 0;
		}
		DMDEBUG_LIMIT("unable to remove open device %s", hc->name);
		up_write(&_hash_lock);
		dm_put(md);
		return r;
	}

	t = __hash_remove(hc);
	up_write(&_hash_lock);

	if (t) {
		dm_sync_table(md);
		dm_table_destroy(t);
	}

	param->flags &= ~DM_DEFERRED_REMOVE;

	dm_ima_measure_on_device_remove(md, false);

	if (!dm_kobject_uevent(md, KOBJ_REMOVE, param->event_nr, false))
		param->flags |= DM_UEVENT_GENERATED_FLAG;

	dm_put(md);
	dm_destroy(md);
	return 0;
}

/*
 * Check a string doesn't overrun the chunk of
 * memory we copied from userland.
 */
static int invalid_str(char *str, void *end)
{
	while ((void *) str < end)
		if (!*str++)
			return 0;

	return -EINVAL;
}

static int dev_rename(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	int r;
	char *new_data = (char *) param + param->data_start;
	struct mapped_device *md;
	unsigned int change_uuid = (param->flags & DM_UUID_FLAG) ? 1 : 0;

	if (new_data < param->data ||
	    invalid_str(new_data, (void *) param + param_size) || !*new_data ||
	    strlen(new_data) > (change_uuid ? DM_UUID_LEN - 1 : DM_NAME_LEN - 1)) {
		DMERR("Invalid new mapped device name or uuid string supplied.");
		return -EINVAL;
	}

	if (!change_uuid) {
		r = check_name(new_data);
		if (r)
			return r;
	}

	md = dm_hash_rename(param, new_data);
	if (IS_ERR(md))
		return PTR_ERR(md);

	__dev_status(md, param);
	dm_put(md);

	return 0;
}

static int dev_set_geometry(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	int r = -EINVAL, x;
	struct mapped_device *md;
	struct hd_geometry geometry;
	unsigned long indata[4];
	char *geostr = (char *) param + param->data_start;
	char dummy;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	if (geostr < param->data ||
	    invalid_str(geostr, (void *) param + param_size)) {
		DMERR("Invalid geometry supplied.");
		goto out;
	}

	x = sscanf(geostr, "%lu %lu %lu %lu%c", indata,
		   indata + 1, indata + 2, indata + 3, &dummy);

	if (x != 4) {
		DMERR("Unable to interpret geometry settings.");
		goto out;
	}

	if (indata[0] > 65535 || indata[1] > 255 || indata[2] > 255) {
		DMERR("Geometry exceeds range limits.");
		goto out;
	}

	geometry.cylinders = indata[0];
	geometry.heads = indata[1];
	geometry.sectors = indata[2];
	geometry.start = indata[3];

	r = dm_set_geometry(md, &geometry);

	param->data_size = 0;

out:
	dm_put(md);
	return r;
}

static int do_suspend(struct dm_ioctl *param)
{
	int r = 0;
	unsigned int suspend_flags = DM_SUSPEND_LOCKFS_FLAG;
	struct mapped_device *md;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	if (param->flags & DM_SKIP_LOCKFS_FLAG)
		suspend_flags &= ~DM_SUSPEND_LOCKFS_FLAG;
	if (param->flags & DM_NOFLUSH_FLAG)
		suspend_flags |= DM_SUSPEND_NOFLUSH_FLAG;

	if (!dm_suspended_md(md)) {
		r = dm_suspend(md, suspend_flags);
		if (r)
			goto out;
	}

	__dev_status(md, param);

out:
	dm_put(md);

	return r;
}

static int do_resume(struct dm_ioctl *param)
{
	int r = 0;
	unsigned int suspend_flags = DM_SUSPEND_LOCKFS_FLAG;
	struct hash_cell *hc;
	struct mapped_device *md;
	struct dm_table *new_map, *old_map = NULL;
	bool need_resize_uevent = false;

	down_write(&_hash_lock);

	hc = __find_device_hash_cell(param);
	if (!hc) {
		DMDEBUG_LIMIT("device doesn't appear to be in the dev hash table.");
		up_write(&_hash_lock);
		return -ENXIO;
	}

	md = hc->md;

	new_map = hc->new_map;
	hc->new_map = NULL;
	param->flags &= ~DM_INACTIVE_PRESENT_FLAG;

	up_write(&_hash_lock);

	/* Do we need to load a new map ? */
	if (new_map) {
		sector_t old_size, new_size;
		int srcu_idx;

		/* Suspend if it isn't already suspended */
		old_map = dm_get_live_table(md, &srcu_idx);
		if ((param->flags & DM_SKIP_LOCKFS_FLAG) || !old_map)
			suspend_flags &= ~DM_SUSPEND_LOCKFS_FLAG;
		dm_put_live_table(md, srcu_idx);
		if (param->flags & DM_NOFLUSH_FLAG)
			suspend_flags |= DM_SUSPEND_NOFLUSH_FLAG;
		if (!dm_suspended_md(md))
			dm_suspend(md, suspend_flags);

		old_size = dm_get_size(md);
		old_map = dm_swap_table(md, new_map);
		if (IS_ERR(old_map)) {
			dm_sync_table(md);
			dm_table_destroy(new_map);
			dm_put(md);
			return PTR_ERR(old_map);
		}
		new_size = dm_get_size(md);
		if (old_size && new_size && old_size != new_size)
			need_resize_uevent = true;

		if (dm_table_get_mode(new_map) & BLK_OPEN_WRITE)
			set_disk_ro(dm_disk(md), 0);
		else
			set_disk_ro(dm_disk(md), 1);
	}

	if (dm_suspended_md(md)) {
		r = dm_resume(md);
		if (!r) {
			dm_ima_measure_on_device_resume(md, new_map ? true : false);

			if (!dm_kobject_uevent(md, KOBJ_CHANGE, param->event_nr, need_resize_uevent))
				param->flags |= DM_UEVENT_GENERATED_FLAG;
		}
	}

	/*
	 * Since dm_swap_table synchronizes RCU, nobody should be in
	 * read-side critical section already.
	 */
	if (old_map)
		dm_table_destroy(old_map);

	if (!r)
		__dev_status(md, param);

	dm_put(md);
	return r;
}

/*
 * Set or unset the suspension state of a device.
 * If the device already is in the requested state we just return its status.
 */
static int dev_suspend(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	if (param->flags & DM_SUSPEND_FLAG)
		return do_suspend(param);

	return do_resume(param);
}

/*
 * Copies device info back to user space, used by
 * the create and info ioctls.
 */
static int dev_status(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	struct mapped_device *md;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	__dev_status(md, param);
	dm_put(md);

	return 0;
}

/*
 * Build up the status struct for each target
 */
static void retrieve_status(struct dm_table *table,
			    struct dm_ioctl *param, size_t param_size)
{
	unsigned int i, num_targets;
	struct dm_target_spec *spec;
	char *outbuf, *outptr;
	status_type_t type;
	size_t remaining, len, used = 0;
	unsigned int status_flags = 0;

	outptr = outbuf = get_result_buffer(param, param_size, &len);

	if (param->flags & DM_STATUS_TABLE_FLAG)
		type = STATUSTYPE_TABLE;
	else if (param->flags & DM_IMA_MEASUREMENT_FLAG)
		type = STATUSTYPE_IMA;
	else
		type = STATUSTYPE_INFO;

	/* Get all the target info */
	num_targets = table->num_targets;
	for (i = 0; i < num_targets; i++) {
		struct dm_target *ti = dm_table_get_target(table, i);
		size_t l;

		remaining = len - (outptr - outbuf);
		if (remaining <= sizeof(struct dm_target_spec)) {
			param->flags |= DM_BUFFER_FULL_FLAG;
			break;
		}

		spec = (struct dm_target_spec *) outptr;

		spec->status = 0;
		spec->sector_start = ti->begin;
		spec->length = ti->len;
		strncpy(spec->target_type, ti->type->name,
			sizeof(spec->target_type) - 1);

		outptr += sizeof(struct dm_target_spec);
		remaining = len - (outptr - outbuf);
		if (remaining <= 0) {
			param->flags |= DM_BUFFER_FULL_FLAG;
			break;
		}

		/* Get the status/table string from the target driver */
		if (ti->type->status) {
			if (param->flags & DM_NOFLUSH_FLAG)
				status_flags |= DM_STATUS_NOFLUSH_FLAG;
			ti->type->status(ti, type, status_flags, outptr, remaining);
		} else
			outptr[0] = '\0';

		l = strlen(outptr) + 1;
		if (l == remaining) {
			param->flags |= DM_BUFFER_FULL_FLAG;
			break;
		}

		outptr += l;
		used = param->data_start + (outptr - outbuf);

		outptr = align_ptr(outptr);
		spec->next = outptr - outbuf;
	}

	if (used)
		param->data_size = used;

	param->target_count = num_targets;
}

/*
 * Wait for a device to report an event
 */
static int dev_wait(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	int r = 0;
	struct mapped_device *md;
	struct dm_table *table;
	int srcu_idx;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	/*
	 * Wait for a notification event
	 */
	if (dm_wait_event(md, param->event_nr)) {
		r = -ERESTARTSYS;
		goto out;
	}

	/*
	 * The userland program is going to want to know what
	 * changed to trigger the event, so we may as well tell
	 * him and save an ioctl.
	 */
	__dev_status(md, param);

	table = dm_get_live_or_inactive_table(md, param, &srcu_idx);
	if (table)
		retrieve_status(table, param, param_size);
	dm_put_live_table(md, srcu_idx);

out:
	dm_put(md);

	return r;
}

/*
 * Remember the global event number and make it possible to poll
 * for further events.
 */
static int dev_arm_poll(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	struct dm_file *priv = filp->private_data;

	priv->global_event_nr = atomic_read(&dm_global_event_nr);

	return 0;
}

static inline blk_mode_t get_mode(struct dm_ioctl *param)
{
	blk_mode_t mode = BLK_OPEN_READ | BLK_OPEN_WRITE;

	if (param->flags & DM_READONLY_FLAG)
		mode = BLK_OPEN_READ;

	return mode;
}

static int next_target(struct dm_target_spec *last, uint32_t next, const char *end,
		       struct dm_target_spec **spec, char **target_params)
{
	static_assert(__alignof__(struct dm_target_spec) <= 8,
		"struct dm_target_spec must not require more than 8-byte alignment");

	/*
	 * Number of bytes remaining, starting with last. This is always
	 * sizeof(struct dm_target_spec) or more, as otherwise *last was
	 * out of bounds already.
	 */
	size_t remaining = end - (char *)last;

	/*
	 * There must be room for both the next target spec and the
	 * NUL-terminator of the target itself.
	 */
	if (remaining - sizeof(struct dm_target_spec) <= next) {
		DMERR("Target spec extends beyond end of parameters");
		return -EINVAL;
	}

	if (next % __alignof__(struct dm_target_spec)) {
		DMERR("Next dm_target_spec (offset %u) is not %zu-byte aligned",
		      next, __alignof__(struct dm_target_spec));
		return -EINVAL;
	}

	*spec = (struct dm_target_spec *) ((unsigned char *) last + next);
	*target_params = (char *) (*spec + 1);

	return 0;
}

static int populate_table(struct dm_table *table,
			  struct dm_ioctl *param, size_t param_size)
{
	int r;
	unsigned int i = 0;
	struct dm_target_spec *spec = (struct dm_target_spec *) param;
	uint32_t next = param->data_start;
	const char *const end = (const char *) param + param_size;
	char *target_params;
	size_t min_size = sizeof(struct dm_ioctl);

	if (!param->target_count) {
		DMERR("%s: no targets specified", __func__);
		return -EINVAL;
	}

	for (i = 0; i < param->target_count; i++) {
		const char *nul_terminator;

		if (next < min_size) {
			DMERR("%s: next target spec (offset %u) overlaps %s",
			      __func__, next, i ? "previous target" : "'struct dm_ioctl'");
			return -EINVAL;
		}

		r = next_target(spec, next, end, &spec, &target_params);
		if (r) {
			DMERR("unable to find target");
			return r;
		}

		nul_terminator = memchr(target_params, 0, (size_t)(end - target_params));
		if (nul_terminator == NULL) {
			DMERR("%s: target parameters not NUL-terminated", __func__);
			return -EINVAL;
		}

		/* Add 1 for NUL terminator */
		min_size = (size_t)(nul_terminator - (const char *)spec) + 1;

		r = dm_table_add_target(table, spec->target_type,
					(sector_t) spec->sector_start,
					(sector_t) spec->length,
					target_params);
		if (r) {
			DMERR("error adding target to table");
			return r;
		}

		next = spec->next;
	}

	return dm_table_complete(table);
}

static bool is_valid_type(enum dm_queue_mode cur, enum dm_queue_mode new)
{
	if (cur == new ||
	    (cur == DM_TYPE_BIO_BASED && new == DM_TYPE_DAX_BIO_BASED))
		return true;

	return false;
}

static int table_load(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	int r;
	struct hash_cell *hc;
	struct dm_table *t, *old_map = NULL;
	struct mapped_device *md;
	struct target_type *immutable_target_type;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	r = dm_table_create(&t, get_mode(param), param->target_count, md);
	if (r)
		goto err;

	/* Protect md->type and md->queue against concurrent table loads. */
	dm_lock_md_type(md);
	r = populate_table(t, param, param_size);
	if (r)
		goto err_unlock_md_type;

	dm_ima_measure_on_table_load(t, STATUSTYPE_IMA);

	immutable_target_type = dm_get_immutable_target_type(md);
	if (immutable_target_type &&
	    (immutable_target_type != dm_table_get_immutable_target_type(t)) &&
	    !dm_table_get_wildcard_target(t)) {
		DMERR("can't replace immutable target type %s",
		      immutable_target_type->name);
		r = -EINVAL;
		goto err_unlock_md_type;
	}

	if (dm_get_md_type(md) == DM_TYPE_NONE) {
		/* setup md->queue to reflect md's type (may block) */
		r = dm_setup_md_queue(md, t);
		if (r) {
			DMERR("unable to set up device queue for new table.");
			goto err_unlock_md_type;
		}
	} else if (!is_valid_type(dm_get_md_type(md), dm_table_get_type(t))) {
		DMERR("can't change device type (old=%u vs new=%u) after initial table load.",
		      dm_get_md_type(md), dm_table_get_type(t));
		r = -EINVAL;
		goto err_unlock_md_type;
	}

	dm_unlock_md_type(md);

	/* stage inactive table */
	down_write(&_hash_lock);
	hc = dm_get_mdptr(md);
	if (!hc) {
		DMERR("device has been removed from the dev hash table.");
		up_write(&_hash_lock);
		r = -ENXIO;
		goto err_destroy_table;
	}

	if (hc->new_map)
		old_map = hc->new_map;
	hc->new_map = t;
	up_write(&_hash_lock);

	param->flags |= DM_INACTIVE_PRESENT_FLAG;
	__dev_status(md, param);

	if (old_map) {
		dm_sync_table(md);
		dm_table_destroy(old_map);
	}

	dm_put(md);

	return 0;

err_unlock_md_type:
	dm_unlock_md_type(md);
err_destroy_table:
	dm_table_destroy(t);
err:
	dm_put(md);

	return r;
}

static int table_clear(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	struct hash_cell *hc;
	struct mapped_device *md;
	struct dm_table *old_map = NULL;
	bool has_new_map = false;

	down_write(&_hash_lock);

	hc = __find_device_hash_cell(param);
	if (!hc) {
		DMDEBUG_LIMIT("device doesn't appear to be in the dev hash table.");
		up_write(&_hash_lock);
		return -ENXIO;
	}

	if (hc->new_map) {
		old_map = hc->new_map;
		hc->new_map = NULL;
		has_new_map = true;
	}

	md = hc->md;
	up_write(&_hash_lock);

	param->flags &= ~DM_INACTIVE_PRESENT_FLAG;
	__dev_status(md, param);

	if (old_map) {
		dm_sync_table(md);
		dm_table_destroy(old_map);
	}
	dm_ima_measure_on_table_clear(md, has_new_map);
	dm_put(md);

	return 0;
}

/*
 * Retrieves a list of devices used by a particular dm device.
 */
static void retrieve_deps(struct dm_table *table,
			  struct dm_ioctl *param, size_t param_size)
{
	unsigned int count = 0;
	struct list_head *tmp;
	size_t len, needed;
	struct dm_dev_internal *dd;
	struct dm_target_deps *deps;

	deps = get_result_buffer(param, param_size, &len);

	/*
	 * Count the devices.
	 */
	list_for_each(tmp, dm_table_get_devices(table))
		count++;

	/*
	 * Check we have enough space.
	 */
	needed = struct_size(deps, dev, count);
	if (len < needed) {
		param->flags |= DM_BUFFER_FULL_FLAG;
		return;
	}

	/*
	 * Fill in the devices.
	 */
	deps->count = count;
	count = 0;
	list_for_each_entry(dd, dm_table_get_devices(table), list)
		deps->dev[count++] = huge_encode_dev(dd->dm_dev->bdev->bd_dev);

	param->data_size = param->data_start + needed;
}

static int table_deps(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	struct mapped_device *md;
	struct dm_table *table;
	int srcu_idx;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	__dev_status(md, param);

	table = dm_get_live_or_inactive_table(md, param, &srcu_idx);
	if (table)
		retrieve_deps(table, param, param_size);
	dm_put_live_table(md, srcu_idx);

	dm_put(md);

	return 0;
}

/*
 * Return the status of a device as a text string for each
 * target.
 */
static int table_status(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	struct mapped_device *md;
	struct dm_table *table;
	int srcu_idx;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	__dev_status(md, param);

	table = dm_get_live_or_inactive_table(md, param, &srcu_idx);
	if (table)
		retrieve_status(table, param, param_size);
	dm_put_live_table(md, srcu_idx);

	dm_put(md);

	return 0;
}

/*
 * Process device-mapper dependent messages.  Messages prefixed with '@'
 * are processed by the DM core.  All others are delivered to the target.
 * Returns a number <= 1 if message was processed by device mapper.
 * Returns 2 if message should be delivered to the target.
 */
static int message_for_md(struct mapped_device *md, unsigned int argc, char **argv,
			  char *result, unsigned int maxlen)
{
	int r;

	if (**argv != '@')
		return 2; /* no '@' prefix, deliver to target */

	if (!strcasecmp(argv[0], "@cancel_deferred_remove")) {
		if (argc != 1) {
			DMERR("Invalid arguments for @cancel_deferred_remove");
			return -EINVAL;
		}
		return dm_cancel_deferred_remove(md);
	}

	r = dm_stats_message(md, argc, argv, result, maxlen);
	if (r < 2)
		return r;

	DMERR("Unsupported message sent to DM core: %s", argv[0]);
	return -EINVAL;
}

/*
 * Pass a message to the target that's at the supplied device offset.
 */
static int target_message(struct file *filp, struct dm_ioctl *param, size_t param_size)
{
	int r, argc;
	char **argv;
	struct mapped_device *md;
	struct dm_table *table;
	struct dm_target *ti;
	struct dm_target_msg *tmsg = (void *) param + param->data_start;
	size_t maxlen;
	char *result = get_result_buffer(param, param_size, &maxlen);
	int srcu_idx;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	if (tmsg < (struct dm_target_msg *) param->data ||
	    invalid_str(tmsg->message, (void *) param + param_size)) {
		DMERR("Invalid target message parameters.");
		r = -EINVAL;
		goto out;
	}

	r = dm_split_args(&argc, &argv, tmsg->message);
	if (r) {
		DMERR("Failed to split target message parameters");
		goto out;
	}

	if (!argc) {
		DMERR("Empty message received.");
		r = -EINVAL;
		goto out_argv;
	}

	r = message_for_md(md, argc, argv, result, maxlen);
	if (r <= 1)
		goto out_argv;

	table = dm_get_live_table(md, &srcu_idx);
	if (!table)
		goto out_table;

	if (dm_deleting_md(md)) {
		r = -ENXIO;
		goto out_table;
	}

	ti = dm_table_find_target(table, tmsg->sector);
	if (!ti) {
		DMERR("Target message sector outside device.");
		r = -EINVAL;
	} else if (ti->type->message)
		r = ti->type->message(ti, argc, argv, result, maxlen);
	else {
		DMERR("Target type does not support messages");
		r = -EINVAL;
	}

 out_table:
	dm_put_live_table(md, srcu_idx);
 out_argv:
	kfree(argv);
 out:
	if (r >= 0)
		__dev_status(md, param);

	if (r == 1) {
		param->flags |= DM_DATA_OUT_FLAG;
		if (dm_message_test_buffer_overflow(result, maxlen))
			param->flags |= DM_BUFFER_FULL_FLAG;
		else
			param->data_size = param->data_start + strlen(result) + 1;
		r = 0;
	}

	dm_put(md);
	return r;
}

/*
 * The ioctl parameter block consists of two parts, a dm_ioctl struct
 * followed by a data buffer.  This flag is set if the second part,
 * which has a variable size, is not used by the function processing
 * the ioctl.
 */
#define IOCTL_FLAGS_NO_PARAMS		1
#define IOCTL_FLAGS_ISSUE_GLOBAL_EVENT	2

/*
 *---------------------------------------------------------------
 * Implementation of open/close/ioctl on the special char device.
 *---------------------------------------------------------------
 */
static ioctl_fn lookup_ioctl(unsigned int cmd, int *ioctl_flags)
{
	static const struct {
		int cmd;
		int flags;
		ioctl_fn fn;
	} _ioctls[] = {
		{DM_VERSION_CMD, 0, NULL}, /* version is dealt with elsewhere */
		{DM_REMOVE_ALL_CMD, IOCTL_FLAGS_NO_PARAMS | IOCTL_FLAGS_ISSUE_GLOBAL_EVENT, remove_all},
		{DM_LIST_DEVICES_CMD, 0, list_devices},

		{DM_DEV_CREATE_CMD, IOCTL_FLAGS_NO_PARAMS | IOCTL_FLAGS_ISSUE_GLOBAL_EVENT, dev_create},
		{DM_DEV_REMOVE_CMD, IOCTL_FLAGS_NO_PARAMS | IOCTL_FLAGS_ISSUE_GLOBAL_EVENT, dev_remove},
		{DM_DEV_RENAME_CMD, IOCTL_FLAGS_ISSUE_GLOBAL_EVENT, dev_rename},
		{DM_DEV_SUSPEND_CMD, IOCTL_FLAGS_NO_PARAMS, dev_suspend},
		{DM_DEV_STATUS_CMD, IOCTL_FLAGS_NO_PARAMS, dev_status},
		{DM_DEV_WAIT_CMD, 0, dev_wait},

		{DM_TABLE_LOAD_CMD, 0, table_load},
		{DM_TABLE_CLEAR_CMD, IOCTL_FLAGS_NO_PARAMS, table_clear},
		{DM_TABLE_DEPS_CMD, 0, table_deps},
		{DM_TABLE_STATUS_CMD, 0, table_status},

		{DM_LIST_VERSIONS_CMD, 0, list_versions},

		{DM_TARGET_MSG_CMD, 0, target_message},
		{DM_DEV_SET_GEOMETRY_CMD, 0, dev_set_geometry},
		{DM_DEV_ARM_POLL_CMD, IOCTL_FLAGS_NO_PARAMS, dev_arm_poll},
		{DM_GET_TARGET_VERSION_CMD, 0, get_target_version},
	};

	if (unlikely(cmd >= ARRAY_SIZE(_ioctls)))
		return NULL;

	cmd = array_index_nospec(cmd, ARRAY_SIZE(_ioctls));
	*ioctl_flags = _ioctls[cmd].flags;
	return _ioctls[cmd].fn;
}

/*
 * As well as checking the version compatibility this always
 * copies the kernel interface version out.
 */
static int check_version(unsigned int cmd, struct dm_ioctl __user *user,
			 struct dm_ioctl *kernel_params)
{
	int r = 0;

	/* Make certain version is first member of dm_ioctl struct */
	BUILD_BUG_ON(offsetof(struct dm_ioctl, version) != 0);

	if (copy_from_user(kernel_params->version, user->version, sizeof(kernel_params->version)))
		return -EFAULT;

	if ((kernel_params->version[0] != DM_VERSION_MAJOR) ||
	    (kernel_params->version[1] > DM_VERSION_MINOR)) {
		DMERR("ioctl interface mismatch: kernel(%u.%u.%u), user(%u.%u.%u), cmd(%d)",
		      DM_VERSION_MAJOR, DM_VERSION_MINOR,
		      DM_VERSION_PATCHLEVEL,
		      kernel_params->version[0],
		      kernel_params->version[1],
		      kernel_params->version[2],
		      cmd);
		r = -EINVAL;
	}

	/*
	 * Fill in the kernel version.
	 */
	kernel_params->version[0] = DM_VERSION_MAJOR;
	kernel_params->version[1] = DM_VERSION_MINOR;
	kernel_params->version[2] = DM_VERSION_PATCHLEVEL;
	if (copy_to_user(user->version, kernel_params->version, sizeof(kernel_params->version)))
		return -EFAULT;

	return r;
}

#define DM_PARAMS_MALLOC	0x0001	/* Params allocated with kvmalloc() */
#define DM_WIPE_BUFFER		0x0010	/* Wipe input buffer before returning from ioctl */

static void free_params(struct dm_ioctl *param, size_t param_size, int param_flags)
{
	if (param_flags & DM_WIPE_BUFFER)
		memset(param, 0, param_size);

	if (param_flags & DM_PARAMS_MALLOC)
		kvfree(param);
}

static int copy_params(struct dm_ioctl __user *user, struct dm_ioctl *param_kernel,
		       int ioctl_flags, struct dm_ioctl **param, int *param_flags)
{
	struct dm_ioctl *dmi;
	int secure_data;
	const size_t minimum_data_size = offsetof(struct dm_ioctl, data);

	/* check_version() already copied version from userspace, avoid TOCTOU */
	if (copy_from_user((char *)param_kernel + sizeof(param_kernel->version),
			   (char __user *)user + sizeof(param_kernel->version),
			   minimum_data_size - sizeof(param_kernel->version)))
		return -EFAULT;

	if (param_kernel->data_size < minimum_data_size) {
		DMERR("Invalid data size in the ioctl structure: %u",
		      param_kernel->data_size);
		return -EINVAL;
	}

	secure_data = param_kernel->flags & DM_SECURE_DATA_FLAG;

	*param_flags = secure_data ? DM_WIPE_BUFFER : 0;

	if (ioctl_flags & IOCTL_FLAGS_NO_PARAMS) {
		dmi = param_kernel;
		dmi->data_size = minimum_data_size;
		goto data_copied;
	}

	/*
	 * Use __GFP_HIGH to avoid low memory issues when a device is
	 * suspended and the ioctl is needed to resume it.
	 * Use kmalloc() rather than vmalloc() when we can.
	 */
	dmi = NULL;
	dmi = kvmalloc(param_kernel->data_size, GFP_NOIO | __GFP_HIGH);

	if (!dmi) {
		if (secure_data && clear_user(user, param_kernel->data_size))
			return -EFAULT;
		return -ENOMEM;
	}

	*param_flags |= DM_PARAMS_MALLOC;

	/* Copy from param_kernel (which was already copied from user) */
	memcpy(dmi, param_kernel, minimum_data_size);

	if (copy_from_user(&dmi->data, (char __user *)user + minimum_data_size,
			   param_kernel->data_size - minimum_data_size))
		goto bad;
data_copied:
	/* Wipe the user buffer so we do not return it to userspace */
	if (secure_data && clear_user(user, param_kernel->data_size))
		goto bad;

	*param = dmi;
	return 0;

bad:
	free_params(dmi, param_kernel->data_size, *param_flags);

	return -EFAULT;
}

static int validate_params(uint cmd, struct dm_ioctl *param)
{
	/* Always clear this flag */
	param->flags &= ~DM_BUFFER_FULL_FLAG;
	param->flags &= ~DM_UEVENT_GENERATED_FLAG;
	param->flags &= ~DM_SECURE_DATA_FLAG;
	param->flags &= ~DM_DATA_OUT_FLAG;

	/* Ignores parameters */
	if (cmd == DM_REMOVE_ALL_CMD ||
	    cmd == DM_LIST_DEVICES_CMD ||
	    cmd == DM_LIST_VERSIONS_CMD)
		return 0;

	if (cmd == DM_DEV_CREATE_CMD) {
		if (!*param->name) {
			DMERR("name not supplied when creating device");
			return -EINVAL;
		}
	} else if (*param->uuid && *param->name) {
		DMERR("only supply one of name or uuid, cmd(%u)", cmd);
		return -EINVAL;
	}

	/* Ensure strings are terminated */
	param->name[DM_NAME_LEN - 1] = '\0';
	param->uuid[DM_UUID_LEN - 1] = '\0';

	return 0;
}

static int ctl_ioctl(struct file *file, uint command, struct dm_ioctl __user *user)
{
	int r = 0;
	int ioctl_flags;
	int param_flags;
	unsigned int cmd;
	struct dm_ioctl *param;
	ioctl_fn fn = NULL;
	size_t input_param_size;
	struct dm_ioctl param_kernel;

	/* only root can play with this */
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (_IOC_TYPE(command) != DM_IOCTL)
		return -ENOTTY;

	cmd = _IOC_NR(command);

	/*
	 * Check the interface version passed in.  This also
	 * writes out the kernel's interface version.
	 */
	r = check_version(cmd, user, &param_kernel);
	if (r)
		return r;

	/*
	 * Nothing more to do for the version command.
	 */
	if (cmd == DM_VERSION_CMD)
		return 0;

	fn = lookup_ioctl(cmd, &ioctl_flags);
	if (!fn) {
		DMERR("dm_ctl_ioctl: unknown command 0x%x", command);
		return -ENOTTY;
	}

	/*
	 * Copy the parameters into kernel space.
	 */
	r = copy_params(user, &param_kernel, ioctl_flags, &param, &param_flags);

	if (r)
		return r;

	input_param_size = param->data_size;
	r = validate_params(cmd, param);
	if (r)
		goto out;

	param->data_size = offsetof(struct dm_ioctl, data);
	r = fn(file, param, input_param_size);

	if (unlikely(param->flags & DM_BUFFER_FULL_FLAG) &&
	    unlikely(ioctl_flags & IOCTL_FLAGS_NO_PARAMS))
		DMERR("ioctl %d tried to output some data but has IOCTL_FLAGS_NO_PARAMS set", cmd);

	if (!r && ioctl_flags & IOCTL_FLAGS_ISSUE_GLOBAL_EVENT)
		dm_issue_global_event();

	/*
	 * Copy the results back to userland.
	 */
	if (!r && copy_to_user(user, param, param->data_size))
		r = -EFAULT;

out:
	free_params(param, input_param_size, param_flags);
	return r;
}

static long dm_ctl_ioctl(struct file *file, uint command, ulong u)
{
	return (long)ctl_ioctl(file, command, (struct dm_ioctl __user *)u);
}

#ifdef CONFIG_COMPAT
static long dm_compat_ctl_ioctl(struct file *file, uint command, ulong u)
{
	return (long)dm_ctl_ioctl(file, command, (ulong) compat_ptr(u));
}
#else
#define dm_compat_ctl_ioctl NULL
#endif

static int dm_open(struct inode *inode, struct file *filp)
{
	int r;
	struct dm_file *priv;

	r = nonseekable_open(inode, filp);
	if (unlikely(r))
		return r;

	priv = filp->private_data = kmalloc(sizeof(struct dm_file), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->global_event_nr = atomic_read(&dm_global_event_nr);

	return 0;
}

static int dm_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

static __poll_t dm_poll(struct file *filp, poll_table *wait)
{
	struct dm_file *priv = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &dm_global_eventq, wait);

	if ((int)(atomic_read(&dm_global_event_nr) - priv->global_event_nr) > 0)
		mask |= EPOLLIN;

	return mask;
}

static const struct file_operations _ctl_fops = {
	.open    = dm_open,
	.release = dm_release,
	.poll    = dm_poll,
	.unlocked_ioctl	 = dm_ctl_ioctl,
	.compat_ioctl = dm_compat_ctl_ioctl,
	.owner	 = THIS_MODULE,
	.llseek  = noop_llseek,
};

static struct miscdevice _dm_misc = {
	.minor		= MAPPER_CTRL_MINOR,
	.name		= DM_NAME,
	.nodename	= DM_DIR "/" DM_CONTROL_NODE,
	.fops		= &_ctl_fops
};

MODULE_ALIAS_MISCDEV(MAPPER_CTRL_MINOR);
MODULE_ALIAS("devname:" DM_DIR "/" DM_CONTROL_NODE);

/*
 * Create misc character device and link to DM_DIR/control.
 */
int __init dm_interface_init(void)
{
	int r;

	r = misc_register(&_dm_misc);
	if (r) {
		DMERR("misc_register failed for control device");
		return r;
	}

	DMINFO("%d.%d.%d%s initialised: %s", DM_VERSION_MAJOR,
	       DM_VERSION_MINOR, DM_VERSION_PATCHLEVEL, DM_VERSION_EXTRA,
	       DM_DRIVER_EMAIL);
	return 0;
}

void dm_interface_exit(void)
{
	misc_deregister(&_dm_misc);
	dm_hash_exit();
}

/**
 * dm_copy_name_and_uuid - Copy mapped device name & uuid into supplied buffers
 * @md: Pointer to mapped_device
 * @name: Buffer (size DM_NAME_LEN) for name
 * @uuid: Buffer (size DM_UUID_LEN) for uuid or empty string if uuid not defined
 */
int dm_copy_name_and_uuid(struct mapped_device *md, char *name, char *uuid)
{
	int r = 0;
	struct hash_cell *hc;

	if (!md)
		return -ENXIO;

	mutex_lock(&dm_hash_cells_mutex);
	hc = dm_get_mdptr(md);
	if (!hc) {
		r = -ENXIO;
		goto out;
	}

	if (name)
		strcpy(name, hc->name);
	if (uuid)
		strcpy(uuid, hc->uuid ? : "");

out:
	mutex_unlock(&dm_hash_cells_mutex);

	return r;
}
EXPORT_SYMBOL_GPL(dm_copy_name_and_uuid);

/**
 * dm_early_create - create a mapped device in early boot.
 *
 * @dmi: Contains main information of the device mapping to be created.
 * @spec_array: array of pointers to struct dm_target_spec. Describes the
 * mapping table of the device.
 * @target_params_array: array of strings with the parameters to a specific
 * target.
 *
 * Instead of having the struct dm_target_spec and the parameters for every
 * target embedded at the end of struct dm_ioctl (as performed in a normal
 * ioctl), pass them as arguments, so the caller doesn't need to serialize them.
 * The size of the spec_array and target_params_array is given by
 * @dmi->target_count.
 * This function is supposed to be called in early boot, so locking mechanisms
 * to protect against concurrent loads are not required.
 */
int __init dm_early_create(struct dm_ioctl *dmi,
			   struct dm_target_spec **spec_array,
			   char **target_params_array)
{
	int r, m = DM_ANY_MINOR;
	struct dm_table *t, *old_map;
	struct mapped_device *md;
	unsigned int i;

	if (!dmi->target_count)
		return -EINVAL;

	r = check_name(dmi->name);
	if (r)
		return r;

	if (dmi->flags & DM_PERSISTENT_DEV_FLAG)
		m = MINOR(huge_decode_dev(dmi->dev));

	/* alloc dm device */
	r = dm_create(m, &md);
	if (r)
		return r;

	/* hash insert */
	r = dm_hash_insert(dmi->name, *dmi->uuid ? dmi->uuid : NULL, md);
	if (r)
		goto err_destroy_dm;

	/* alloc table */
	r = dm_table_create(&t, get_mode(dmi), dmi->target_count, md);
	if (r)
		goto err_hash_remove;

	/* add targets */
	for (i = 0; i < dmi->target_count; i++) {
		r = dm_table_add_target(t, spec_array[i]->target_type,
					(sector_t) spec_array[i]->sector_start,
					(sector_t) spec_array[i]->length,
					target_params_array[i]);
		if (r) {
			DMERR("error adding target to table");
			goto err_destroy_table;
		}
	}

	/* finish table */
	r = dm_table_complete(t);
	if (r)
		goto err_destroy_table;

	/* setup md->queue to reflect md's type (may block) */
	r = dm_setup_md_queue(md, t);
	if (r) {
		DMERR("unable to set up device queue for new table.");
		goto err_destroy_table;
	}

	/* Set new map */
	dm_suspend(md, 0);
	old_map = dm_swap_table(md, t);
	if (IS_ERR(old_map)) {
		r = PTR_ERR(old_map);
		goto err_destroy_table;
	}
	set_disk_ro(dm_disk(md), !!(dmi->flags & DM_READONLY_FLAG));

	/* resume device */
	r = dm_resume(md);
	if (r)
		goto err_destroy_table;

	DMINFO("%s (%s) is ready", md->disk->disk_name, dmi->name);
	dm_put(md);
	return 0;

err_destroy_table:
	dm_table_destroy(t);
err_hash_remove:
	down_write(&_hash_lock);
	(void) __hash_remove(__get_name_cell(dmi->name));
	up_write(&_hash_lock);
	/* release reference from __get_name_cell */
	dm_put(md);
err_destroy_dm:
	dm_put(md);
	dm_destroy(md);
	return r;
}
