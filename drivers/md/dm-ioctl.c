/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 * Copyright (C) 2004 - 2006 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/dm-ioctl.h>
#include <linux/hdreg.h>
#include <linux/compat.h>

#include <asm/uaccess.h>

#define DM_MSG_PREFIX "ioctl"
#define DM_DRIVER_EMAIL "dm-devel@redhat.com"

/*-----------------------------------------------------------------
 * The ioctl interface needs to be able to look up devices by
 * name or uuid.
 *---------------------------------------------------------------*/
struct hash_cell {
	struct list_head name_list;
	struct list_head uuid_list;

	char *name;
	char *uuid;
	struct mapped_device *md;
	struct dm_table *new_map;
};

/*
 * A dummy definition to make RCU happy.
 * struct dm_table should never be dereferenced in this file.
 */
struct dm_table {
	int undefined__;
};

struct vers_iter {
    size_t param_size;
    struct dm_target_versions *vers, *old_vers;
    char *end;
    uint32_t flags;
};


#define NUM_BUCKETS 64
#define MASK_BUCKETS (NUM_BUCKETS - 1)
static struct list_head _name_buckets[NUM_BUCKETS];
static struct list_head _uuid_buckets[NUM_BUCKETS];

static void dm_hash_remove_all(bool keep_open_devices, bool mark_deferred, bool only_deferred);

/*
 * Guards access to both hash tables.
 */
static DECLARE_RWSEM(_hash_lock);

/*
 * Protects use of mdptr to obtain hash cell name and uuid from mapped device.
 */
static DEFINE_MUTEX(dm_hash_cells_mutex);

static void init_buckets(struct list_head *buckets)
{
	unsigned int i;

	for (i = 0; i < NUM_BUCKETS; i++)
		INIT_LIST_HEAD(buckets + i);
}

static int dm_hash_init(void)
{
	init_buckets(_name_buckets);
	init_buckets(_uuid_buckets);
	return 0;
}

static void dm_hash_exit(void)
{
	dm_hash_remove_all(false, false, false);
}

/*-----------------------------------------------------------------
 * Hash function:
 * We're not really concerned with the str hash function being
 * fast since it's only used by the ioctl interface.
 *---------------------------------------------------------------*/
static unsigned int hash_str(const char *str)
{
	const unsigned int hash_mult = 2654435387U;
	unsigned int h = 0;

	while (*str)
		h = (h + (unsigned int) *str++) * hash_mult;

	return h & MASK_BUCKETS;
}

/*-----------------------------------------------------------------
 * Code for looking up a device by name
 *---------------------------------------------------------------*/
static struct hash_cell *__get_name_cell(const char *str)
{
	struct hash_cell *hc;
	unsigned int h = hash_str(str);

	list_for_each_entry (hc, _name_buckets + h, name_list)
		if (!strcmp(hc->name, str)) {
			dm_get(hc->md);
			return hc;
		}

	return NULL;
}

static struct hash_cell *__get_uuid_cell(const char *str)
{
	struct hash_cell *hc;
	unsigned int h = hash_str(str);

	list_for_each_entry (hc, _uuid_buckets + h, uuid_list)
		if (!strcmp(hc->uuid, str)) {
			dm_get(hc->md);
			return hc;
		}

	return NULL;
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

/*-----------------------------------------------------------------
 * Inserting, removing and renaming a device.
 *---------------------------------------------------------------*/
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

	INIT_LIST_HEAD(&hc->name_list);
	INIT_LIST_HEAD(&hc->uuid_list);
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

	list_add(&cell->name_list, _name_buckets + hash_str(name));

	if (uuid) {
		hc = __get_uuid_cell(uuid);
		if (hc) {
			list_del(&cell->name_list);
			dm_put(hc->md);
			goto bad;
		}
		list_add(&cell->uuid_list, _uuid_buckets + hash_str(uuid));
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

	/* remove from the dev hash */
	list_del(&hc->uuid_list);
	list_del(&hc->name_list);
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
	int i, dev_skipped;
	struct hash_cell *hc;
	struct mapped_device *md;
	struct dm_table *t;

retry:
	dev_skipped = 0;

	down_write(&_hash_lock);

	for (i = 0; i < NUM_BUCKETS; i++) {
		list_for_each_entry(hc, _name_buckets + i, name_list) {
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

	list_add(&hc->uuid_list, _uuid_buckets + hash_str(new_uuid));
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
	list_del(&hc->name_list);
	old_name = hc->name;

	mutex_lock(&dm_hash_cells_mutex);
	hc->name = new_name;
	mutex_unlock(&dm_hash_cells_mutex);

	list_add(&hc->name_list, _name_buckets + hash_str(new_name));

	return old_name;
}

static struct mapped_device *dm_hash_rename(struct dm_ioctl *param,
					    const char *new)
{
	char *new_data, *old_name = NULL;
	struct hash_cell *hc;
	struct dm_table *table;
	struct mapped_device *md;
	unsigned change_uuid = (param->flags & DM_UUID_FLAG) ? 1 : 0;
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
		DMWARN("Unable to change %s on mapped device %s to one that "
		       "already exists: %s",
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
		DMWARN("Unable to rename non-existent device, %s to %s%s",
		       param->name, change_uuid ? "uuid " : "", new);
		up_write(&_hash_lock);
		kfree(new_data);
		return ERR_PTR(-ENXIO);
	}

	/*
	 * Does this device already have a uuid?
	 */
	if (change_uuid && hc->uuid) {
		DMWARN("Unable to change uuid of mapped device %s to %s "
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

	if (!dm_kobject_uevent(hc->md, KOBJ_CHANGE, param->event_nr))
		param->flags |= DM_UEVENT_GENERATED_FLAG;

	md = hc->md;
	up_write(&_hash_lock);
	kfree(old_name);

	return md;
}

void dm_deferred_remove(void)
{
	dm_hash_remove_all(true, false, true);
}

/*-----------------------------------------------------------------
 * Implementation of the ioctl commands
 *---------------------------------------------------------------*/
/*
 * All the ioctl commands get dispatched to functions with this
 * prototype.
 */
typedef int (*ioctl_fn)(struct dm_ioctl *param, size_t param_size);

static int remove_all(struct dm_ioctl *param, size_t param_size)
{
	dm_hash_remove_all(true, !!(param->flags & DM_DEFERRED_REMOVE), false);
	param->data_size = 0;
	return 0;
}

/*
 * Round up the ptr to an 8-byte boundary.
 */
#define ALIGN_MASK 7
static inline void *align_ptr(void *ptr)
{
	return (void *) (((size_t) (ptr + ALIGN_MASK)) & ~ALIGN_MASK);
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

static int list_devices(struct dm_ioctl *param, size_t param_size)
{
	unsigned int i;
	struct hash_cell *hc;
	size_t len, needed = 0;
	struct gendisk *disk;
	struct dm_name_list *nl, *old_nl = NULL;

	down_write(&_hash_lock);

	/*
	 * Loop through all the devices working out how much
	 * space we need.
	 */
	for (i = 0; i < NUM_BUCKETS; i++) {
		list_for_each_entry (hc, _name_buckets + i, name_list) {
			needed += sizeof(struct dm_name_list);
			needed += strlen(hc->name) + 1;
			needed += ALIGN_MASK;
		}
	}

	/*
	 * Grab our output buffer.
	 */
	nl = get_result_buffer(param, param_size, &len);
	if (len < needed) {
		param->flags |= DM_BUFFER_FULL_FLAG;
		goto out;
	}
	param->data_size = param->data_start + needed;

	nl->dev = 0;	/* Flags no data */

	/*
	 * Now loop through filling out the names.
	 */
	for (i = 0; i < NUM_BUCKETS; i++) {
		list_for_each_entry (hc, _name_buckets + i, name_list) {
			if (old_nl)
				old_nl->next = (uint32_t) ((void *) nl -
							   (void *) old_nl);
			disk = dm_disk(hc->md);
			nl->dev = huge_encode_dev(disk_devt(disk));
			nl->next = 0;
			strcpy(nl->name, hc->name);

			old_nl = nl;
			nl = align_ptr(((void *) ++nl) + strlen(hc->name) + 1);
		}
	}

 out:
	up_write(&_hash_lock);
	return 0;
}

static void list_version_get_needed(struct target_type *tt, void *needed_param)
{
    size_t *needed = needed_param;

    *needed += sizeof(struct dm_target_versions);
    *needed += strlen(tt->name);
    *needed += ALIGN_MASK;
}

static void list_version_get_info(struct target_type *tt, void *param)
{
    struct vers_iter *info = param;

    /* Check space - it might have changed since the first iteration */
    if ((char *)info->vers + sizeof(tt->version) + strlen(tt->name) + 1 >
	info->end) {

	info->flags = DM_BUFFER_FULL_FLAG;
	return;
    }

    if (info->old_vers)
	info->old_vers->next = (uint32_t) ((void *)info->vers -
					   (void *)info->old_vers);
    info->vers->version[0] = tt->version[0];
    info->vers->version[1] = tt->version[1];
    info->vers->version[2] = tt->version[2];
    info->vers->next = 0;
    strcpy(info->vers->name, tt->name);

    info->old_vers = info->vers;
    info->vers = align_ptr(((void *) ++info->vers) + strlen(tt->name) + 1);
}

static int list_versions(struct dm_ioctl *param, size_t param_size)
{
	size_t len, needed = 0;
	struct dm_target_versions *vers;
	struct vers_iter iter_info;

	/*
	 * Loop through all the devices working out how much
	 * space we need.
	 */
	dm_target_iterate(list_version_get_needed, &needed);

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
	iter_info.end = (char *)vers+len;

	/*
	 * Now loop through filling out the names & versions.
	 */
	dm_target_iterate(list_version_get_info, &iter_info);
	param->flags |= iter_info.flags;

 out:
	return 0;
}

static int check_name(const char *name)
{
	if (strchr(name, '/')) {
		DMWARN("invalid device name");
		return -EINVAL;
	}

	return 0;
}

/*
 * On successful return, the caller must not attempt to acquire
 * _hash_lock without first calling dm_table_put, because dm_table_destroy
 * waits for this dm_table_put and could be called under this lock.
 */
static struct dm_table *dm_get_inactive_table(struct mapped_device *md, int *srcu_idx)
{
	struct hash_cell *hc;
	struct dm_table *table = NULL;

	/* increment rcu count, we don't care about the table pointer */
	dm_get_live_table(md, srcu_idx);

	down_read(&_hash_lock);
	hc = dm_get_mdptr(md);
	if (!hc || hc->md != md) {
		DMWARN("device has been removed from the dev hash table.");
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
			  DM_ACTIVE_PRESENT_FLAG);

	if (dm_suspended_md(md))
		param->flags |= DM_SUSPEND_FLAG;

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
			param->target_count = dm_table_get_num_targets(table);
		}

		param->flags |= DM_ACTIVE_PRESENT_FLAG;
	}
	dm_put_live_table(md, srcu_idx);

	if (param->flags & DM_QUERY_INACTIVE_TABLE_FLAG) {
		int srcu_idx;
		table = dm_get_inactive_table(md, &srcu_idx);
		if (table) {
			if (!(dm_table_get_mode(table) & FMODE_WRITE))
				param->flags |= DM_READONLY_FLAG;
			param->target_count = dm_table_get_num_targets(table);
		}
		dm_put_live_table(md, srcu_idx);
	}
}

static int dev_create(struct dm_ioctl *param, size_t param_size)
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
		if (*param->name || param->dev)
			return NULL;

		hc = __get_uuid_cell(param->uuid);
		if (!hc)
			return NULL;
	} else if (*param->name) {
		if (param->dev)
			return NULL;

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
	strlcpy(param->name, hc->name, sizeof(param->name));
	if (hc->uuid)
		strlcpy(param->uuid, hc->uuid, sizeof(param->uuid));
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

static int dev_remove(struct dm_ioctl *param, size_t param_size)
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

	if (!dm_kobject_uevent(md, KOBJ_REMOVE, param->event_nr))
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

static int dev_rename(struct dm_ioctl *param, size_t param_size)
{
	int r;
	char *new_data = (char *) param + param->data_start;
	struct mapped_device *md;
	unsigned change_uuid = (param->flags & DM_UUID_FLAG) ? 1 : 0;

	if (new_data < param->data ||
	    invalid_str(new_data, (void *) param + param_size) || !*new_data ||
	    strlen(new_data) > (change_uuid ? DM_UUID_LEN - 1 : DM_NAME_LEN - 1)) {
		DMWARN("Invalid new mapped device name or uuid string supplied.");
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

static int dev_set_geometry(struct dm_ioctl *param, size_t param_size)
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
		DMWARN("Invalid geometry supplied.");
		goto out;
	}

	x = sscanf(geostr, "%lu %lu %lu %lu%c", indata,
		   indata + 1, indata + 2, indata + 3, &dummy);

	if (x != 4) {
		DMWARN("Unable to interpret geometry settings.");
		goto out;
	}

	if (indata[0] > 65535 || indata[1] > 255 ||
	    indata[2] > 255 || indata[3] > ULONG_MAX) {
		DMWARN("Geometry exceeds range limits.");
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
	unsigned suspend_flags = DM_SUSPEND_LOCKFS_FLAG;
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
	unsigned suspend_flags = DM_SUSPEND_LOCKFS_FLAG;
	struct hash_cell *hc;
	struct mapped_device *md;
	struct dm_table *new_map, *old_map = NULL;

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
		/* Suspend if it isn't already suspended */
		if (param->flags & DM_SKIP_LOCKFS_FLAG)
			suspend_flags &= ~DM_SUSPEND_LOCKFS_FLAG;
		if (param->flags & DM_NOFLUSH_FLAG)
			suspend_flags |= DM_SUSPEND_NOFLUSH_FLAG;
		if (!dm_suspended_md(md))
			dm_suspend(md, suspend_flags);

		old_map = dm_swap_table(md, new_map);
		if (IS_ERR(old_map)) {
			dm_sync_table(md);
			dm_table_destroy(new_map);
			dm_put(md);
			return PTR_ERR(old_map);
		}

		if (dm_table_get_mode(new_map) & FMODE_WRITE)
			set_disk_ro(dm_disk(md), 0);
		else
			set_disk_ro(dm_disk(md), 1);
	}

	if (dm_suspended_md(md)) {
		r = dm_resume(md);
		if (!r && !dm_kobject_uevent(md, KOBJ_CHANGE, param->event_nr))
			param->flags |= DM_UEVENT_GENERATED_FLAG;
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
static int dev_suspend(struct dm_ioctl *param, size_t param_size)
{
	if (param->flags & DM_SUSPEND_FLAG)
		return do_suspend(param);

	return do_resume(param);
}

/*
 * Copies device info back to user space, used by
 * the create and info ioctls.
 */
static int dev_status(struct dm_ioctl *param, size_t param_size)
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
	unsigned status_flags = 0;

	outptr = outbuf = get_result_buffer(param, param_size, &len);

	if (param->flags & DM_STATUS_TABLE_FLAG)
		type = STATUSTYPE_TABLE;
	else
		type = STATUSTYPE_INFO;

	/* Get all the target info */
	num_targets = dm_table_get_num_targets(table);
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
			sizeof(spec->target_type));

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
static int dev_wait(struct dm_ioctl *param, size_t param_size)
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

static inline fmode_t get_mode(struct dm_ioctl *param)
{
	fmode_t mode = FMODE_READ | FMODE_WRITE;

	if (param->flags & DM_READONLY_FLAG)
		mode = FMODE_READ;

	return mode;
}

static int next_target(struct dm_target_spec *last, uint32_t next, void *end,
		       struct dm_target_spec **spec, char **target_params)
{
	*spec = (struct dm_target_spec *) ((unsigned char *) last + next);
	*target_params = (char *) (*spec + 1);

	if (*spec < (last + 1))
		return -EINVAL;

	return invalid_str(*target_params, end);
}

static int populate_table(struct dm_table *table,
			  struct dm_ioctl *param, size_t param_size)
{
	int r;
	unsigned int i = 0;
	struct dm_target_spec *spec = (struct dm_target_spec *) param;
	uint32_t next = param->data_start;
	void *end = (void *) param + param_size;
	char *target_params;

	if (!param->target_count) {
		DMWARN("populate_table: no targets specified");
		return -EINVAL;
	}

	for (i = 0; i < param->target_count; i++) {

		r = next_target(spec, next, end, &spec, &target_params);
		if (r) {
			DMWARN("unable to find target");
			return r;
		}

		r = dm_table_add_target(table, spec->target_type,
					(sector_t) spec->sector_start,
					(sector_t) spec->length,
					target_params);
		if (r) {
			DMWARN("error adding target to table");
			return r;
		}

		next = spec->next;
	}

	return dm_table_complete(table);
}

static int table_load(struct dm_ioctl *param, size_t param_size)
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

	immutable_target_type = dm_get_immutable_target_type(md);
	if (immutable_target_type &&
	    (immutable_target_type != dm_table_get_immutable_target_type(t))) {
		DMWARN("can't replace immutable target type %s",
		       immutable_target_type->name);
		r = -EINVAL;
		goto err_unlock_md_type;
	}

	if (dm_get_md_type(md) == DM_TYPE_NONE)
		/* Initial table load: acquire type of table. */
		dm_set_md_type(md, dm_table_get_type(t));
	else if (dm_get_md_type(md) != dm_table_get_type(t)) {
		DMWARN("can't change device type after initial table load.");
		r = -EINVAL;
		goto err_unlock_md_type;
	}

	/* setup md->queue to reflect md's type (may block) */
	r = dm_setup_md_queue(md);
	if (r) {
		DMWARN("unable to set up device queue for new table.");
		goto err_unlock_md_type;
	}
	dm_unlock_md_type(md);

	/* stage inactive table */
	down_write(&_hash_lock);
	hc = dm_get_mdptr(md);
	if (!hc || hc->md != md) {
		DMWARN("device has been removed from the dev hash table.");
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

static int table_clear(struct dm_ioctl *param, size_t param_size)
{
	struct hash_cell *hc;
	struct mapped_device *md;
	struct dm_table *old_map = NULL;

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
	}

	param->flags &= ~DM_INACTIVE_PRESENT_FLAG;

	__dev_status(hc->md, param);
	md = hc->md;
	up_write(&_hash_lock);
	if (old_map) {
		dm_sync_table(md);
		dm_table_destroy(old_map);
	}
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
	list_for_each (tmp, dm_table_get_devices(table))
		count++;

	/*
	 * Check we have enough space.
	 */
	needed = sizeof(*deps) + (sizeof(*deps->dev) * count);
	if (len < needed) {
		param->flags |= DM_BUFFER_FULL_FLAG;
		return;
	}

	/*
	 * Fill in the devices.
	 */
	deps->count = count;
	count = 0;
	list_for_each_entry (dd, dm_table_get_devices(table), list)
		deps->dev[count++] = huge_encode_dev(dd->dm_dev->bdev->bd_dev);

	param->data_size = param->data_start + needed;
}

static int table_deps(struct dm_ioctl *param, size_t param_size)
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
static int table_status(struct dm_ioctl *param, size_t param_size)
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
static int message_for_md(struct mapped_device *md, unsigned argc, char **argv,
			  char *result, unsigned maxlen)
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
static int target_message(struct dm_ioctl *param, size_t param_size)
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
		DMWARN("Invalid target message parameters.");
		r = -EINVAL;
		goto out;
	}

	r = dm_split_args(&argc, &argv, tmsg->message);
	if (r) {
		DMWARN("Failed to split target message parameters");
		goto out;
	}

	if (!argc) {
		DMWARN("Empty message received.");
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
	if (!dm_target_is_valid(ti)) {
		DMWARN("Target message sector outside device.");
		r = -EINVAL;
	} else if (ti->type->message)
		r = ti->type->message(ti, argc, argv);
	else {
		DMWARN("Target type does not support messages");
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
#define IOCTL_FLAGS_NO_PARAMS	1

/*-----------------------------------------------------------------
 * Implementation of open/close/ioctl on the special char
 * device.
 *---------------------------------------------------------------*/
static ioctl_fn lookup_ioctl(unsigned int cmd, int *ioctl_flags)
{
	static struct {
		int cmd;
		int flags;
		ioctl_fn fn;
	} _ioctls[] = {
		{DM_VERSION_CMD, 0, NULL}, /* version is dealt with elsewhere */
		{DM_REMOVE_ALL_CMD, IOCTL_FLAGS_NO_PARAMS, remove_all},
		{DM_LIST_DEVICES_CMD, 0, list_devices},

		{DM_DEV_CREATE_CMD, IOCTL_FLAGS_NO_PARAMS, dev_create},
		{DM_DEV_REMOVE_CMD, IOCTL_FLAGS_NO_PARAMS, dev_remove},
		{DM_DEV_RENAME_CMD, 0, dev_rename},
		{DM_DEV_SUSPEND_CMD, IOCTL_FLAGS_NO_PARAMS, dev_suspend},
		{DM_DEV_STATUS_CMD, IOCTL_FLAGS_NO_PARAMS, dev_status},
		{DM_DEV_WAIT_CMD, 0, dev_wait},

		{DM_TABLE_LOAD_CMD, 0, table_load},
		{DM_TABLE_CLEAR_CMD, IOCTL_FLAGS_NO_PARAMS, table_clear},
		{DM_TABLE_DEPS_CMD, 0, table_deps},
		{DM_TABLE_STATUS_CMD, 0, table_status},

		{DM_LIST_VERSIONS_CMD, 0, list_versions},

		{DM_TARGET_MSG_CMD, 0, target_message},
		{DM_DEV_SET_GEOMETRY_CMD, 0, dev_set_geometry}
	};

	if (unlikely(cmd >= ARRAY_SIZE(_ioctls)))
		return NULL;

	*ioctl_flags = _ioctls[cmd].flags;
	return _ioctls[cmd].fn;
}

/*
 * As well as checking the version compatibility this always
 * copies the kernel interface version out.
 */
static int check_version(unsigned int cmd, struct dm_ioctl __user *user)
{
	uint32_t version[3];
	int r = 0;

	if (copy_from_user(version, user->version, sizeof(version)))
		return -EFAULT;

	if ((DM_VERSION_MAJOR != version[0]) ||
	    (DM_VERSION_MINOR < version[1])) {
		DMWARN("ioctl interface mismatch: "
		       "kernel(%u.%u.%u), user(%u.%u.%u), cmd(%d)",
		       DM_VERSION_MAJOR, DM_VERSION_MINOR,
		       DM_VERSION_PATCHLEVEL,
		       version[0], version[1], version[2], cmd);
		r = -EINVAL;
	}

	/*
	 * Fill in the kernel version.
	 */
	version[0] = DM_VERSION_MAJOR;
	version[1] = DM_VERSION_MINOR;
	version[2] = DM_VERSION_PATCHLEVEL;
	if (copy_to_user(user->version, version, sizeof(version)))
		return -EFAULT;

	return r;
}

#define DM_PARAMS_KMALLOC	0x0001	/* Params alloced with kmalloc */
#define DM_PARAMS_VMALLOC	0x0002	/* Params alloced with vmalloc */
#define DM_WIPE_BUFFER		0x0010	/* Wipe input buffer before returning from ioctl */

static void free_params(struct dm_ioctl *param, size_t param_size, int param_flags)
{
	if (param_flags & DM_WIPE_BUFFER)
		memset(param, 0, param_size);

	if (param_flags & DM_PARAMS_KMALLOC)
		kfree(param);
	if (param_flags & DM_PARAMS_VMALLOC)
		vfree(param);
}

static int copy_params(struct dm_ioctl __user *user, struct dm_ioctl *param_kernel,
		       int ioctl_flags,
		       struct dm_ioctl **param, int *param_flags)
{
	struct dm_ioctl *dmi;
	int secure_data;
	const size_t minimum_data_size = sizeof(*param_kernel) - sizeof(param_kernel->data);

	if (copy_from_user(param_kernel, user, minimum_data_size))
		return -EFAULT;

	if (param_kernel->data_size < minimum_data_size)
		return -EINVAL;

	secure_data = param_kernel->flags & DM_SECURE_DATA_FLAG;

	*param_flags = secure_data ? DM_WIPE_BUFFER : 0;

	if (ioctl_flags & IOCTL_FLAGS_NO_PARAMS) {
		dmi = param_kernel;
		dmi->data_size = minimum_data_size;
		goto data_copied;
	}

	/*
	 * Try to avoid low memory issues when a device is suspended.
	 * Use kmalloc() rather than vmalloc() when we can.
	 */
	dmi = NULL;
	if (param_kernel->data_size <= KMALLOC_MAX_SIZE) {
		dmi = kmalloc(param_kernel->data_size, GFP_NOIO | __GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN);
		if (dmi)
			*param_flags |= DM_PARAMS_KMALLOC;
	}

	if (!dmi) {
		unsigned noio_flag;
		noio_flag = memalloc_noio_save();
		dmi = __vmalloc(param_kernel->data_size, GFP_NOIO | __GFP_REPEAT | __GFP_HIGH | __GFP_HIGHMEM, PAGE_KERNEL);
		memalloc_noio_restore(noio_flag);
		if (dmi)
			*param_flags |= DM_PARAMS_VMALLOC;
	}

	if (!dmi) {
		if (secure_data && clear_user(user, param_kernel->data_size))
			return -EFAULT;
		return -ENOMEM;
	}

	if (copy_from_user(dmi, user, param_kernel->data_size))
		goto bad;

data_copied:
	/*
	 * Abort if something changed the ioctl data while it was being copied.
	 */
	if (dmi->data_size != param_kernel->data_size) {
		DMERR("rejecting ioctl: data size modified while processing parameters");
		goto bad;
	}

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

	if ((cmd == DM_DEV_CREATE_CMD)) {
		if (!*param->name) {
			DMWARN("name not supplied when creating device");
			return -EINVAL;
		}
	} else if ((*param->uuid && *param->name)) {
		DMWARN("only supply one of name or uuid, cmd(%u)", cmd);
		return -EINVAL;
	}

	/* Ensure strings are terminated */
	param->name[DM_NAME_LEN - 1] = '\0';
	param->uuid[DM_UUID_LEN - 1] = '\0';

	return 0;
}

static int ctl_ioctl(uint command, struct dm_ioctl __user *user)
{
	int r = 0;
	int ioctl_flags;
	int param_flags;
	unsigned int cmd;
	struct dm_ioctl *uninitialized_var(param);
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
	r = check_version(cmd, user);
	if (r)
		return r;

	/*
	 * Nothing more to do for the version command.
	 */
	if (cmd == DM_VERSION_CMD)
		return 0;

	fn = lookup_ioctl(cmd, &ioctl_flags);
	if (!fn) {
		DMWARN("dm_ctl_ioctl: unknown command 0x%x", command);
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

	param->data_size = sizeof(*param);
	r = fn(param, input_param_size);

	if (unlikely(param->flags & DM_BUFFER_FULL_FLAG) &&
	    unlikely(ioctl_flags & IOCTL_FLAGS_NO_PARAMS))
		DMERR("ioctl %d tried to output some data but has IOCTL_FLAGS_NO_PARAMS set", cmd);

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
	return (long)ctl_ioctl(command, (struct dm_ioctl __user *)u);
}

#ifdef CONFIG_COMPAT
static long dm_compat_ctl_ioctl(struct file *file, uint command, ulong u)
{
	return (long)dm_ctl_ioctl(file, command, (ulong) compat_ptr(u));
}
#else
#define dm_compat_ctl_ioctl NULL
#endif

static const struct file_operations _ctl_fops = {
	.open = nonseekable_open,
	.unlocked_ioctl	 = dm_ctl_ioctl,
	.compat_ioctl = dm_compat_ctl_ioctl,
	.owner	 = THIS_MODULE,
	.llseek  = noop_llseek,
};

static struct miscdevice _dm_misc = {
	.minor		= MAPPER_CTRL_MINOR,
	.name  		= DM_NAME,
	.nodename	= DM_DIR "/" DM_CONTROL_NODE,
	.fops  		= &_ctl_fops
};

MODULE_ALIAS_MISCDEV(MAPPER_CTRL_MINOR);
MODULE_ALIAS("devname:" DM_DIR "/" DM_CONTROL_NODE);

/*
 * Create misc character device and link to DM_DIR/control.
 */
int __init dm_interface_init(void)
{
	int r;

	r = dm_hash_init();
	if (r)
		return r;

	r = misc_register(&_dm_misc);
	if (r) {
		DMERR("misc_register failed for control device");
		dm_hash_exit();
		return r;
	}

	DMINFO("%d.%d.%d%s initialised: %s", DM_VERSION_MAJOR,
	       DM_VERSION_MINOR, DM_VERSION_PATCHLEVEL, DM_VERSION_EXTRA,
	       DM_DRIVER_EMAIL);
	return 0;
}

void dm_interface_exit(void)
{
	if (misc_deregister(&_dm_misc) < 0)
		DMERR("misc_deregister failed for control device");

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
	if (!hc || hc->md != md) {
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
