/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation. All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <net/addrconf.h>

#include <rdma/ib_cache.h>

#include "core_priv.h"

struct ib_pkey_cache {
	int             table_len;
	u16             table[];
};

struct ib_update_work {
	struct work_struct work;
	struct ib_event event;
	bool enforce_security;
};

union ib_gid zgid;
EXPORT_SYMBOL(zgid);

enum gid_attr_find_mask {
	GID_ATTR_FIND_MASK_GID          = 1UL << 0,
	GID_ATTR_FIND_MASK_NETDEV	= 1UL << 1,
	GID_ATTR_FIND_MASK_DEFAULT	= 1UL << 2,
	GID_ATTR_FIND_MASK_GID_TYPE	= 1UL << 3,
};

enum gid_table_entry_state {
	GID_TABLE_ENTRY_INVALID		= 1,
	GID_TABLE_ENTRY_VALID		= 2,
	/*
	 * Indicates that entry is pending to be removed, there may
	 * be active users of this GID entry.
	 * When last user of the GID entry releases reference to it,
	 * GID entry is detached from the table.
	 */
	GID_TABLE_ENTRY_PENDING_DEL	= 3,
};

struct roce_gid_ndev_storage {
	struct rcu_head rcu_head;
	struct net_device *ndev;
};

struct ib_gid_table_entry {
	struct kref			kref;
	struct work_struct		del_work;
	struct ib_gid_attr		attr;
	void				*context;
	/* Store the ndev pointer to release reference later on in
	 * call_rcu context because by that time gid_table_entry
	 * and attr might be already freed. So keep a copy of it.
	 * ndev_storage is freed by rcu callback.
	 */
	struct roce_gid_ndev_storage	*ndev_storage;
	enum gid_table_entry_state	state;
};

struct ib_gid_table {
	int				sz;
	/* In RoCE, adding a GID to the table requires:
	 * (a) Find if this GID is already exists.
	 * (b) Find a free space.
	 * (c) Write the new GID
	 *
	 * Delete requires different set of operations:
	 * (a) Find the GID
	 * (b) Delete it.
	 *
	 **/
	/* Any writer to data_vec must hold this lock and the write side of
	 * rwlock. Readers must hold only rwlock. All writers must be in a
	 * sleepable context.
	 */
	struct mutex			lock;
	/* rwlock protects data_vec[ix]->state and entry pointer.
	 */
	rwlock_t			rwlock;
	struct ib_gid_table_entry	**data_vec;
	/* bit field, each bit indicates the index of default GID */
	u32				default_gid_indices;
};

static void dispatch_gid_change_event(struct ib_device *ib_dev, u32 port)
{
	struct ib_event event;

	event.device		= ib_dev;
	event.element.port_num	= port;
	event.event		= IB_EVENT_GID_CHANGE;

	ib_dispatch_event_clients(&event);
}

static const char * const gid_type_str[] = {
	/* IB/RoCE v1 value is set for IB_GID_TYPE_IB and IB_GID_TYPE_ROCE for
	 * user space compatibility reasons.
	 */
	[IB_GID_TYPE_IB]	= "IB/RoCE v1",
	[IB_GID_TYPE_ROCE]	= "IB/RoCE v1",
	[IB_GID_TYPE_ROCE_UDP_ENCAP]	= "RoCE v2",
};

const char *ib_cache_gid_type_str(enum ib_gid_type gid_type)
{
	if (gid_type < ARRAY_SIZE(gid_type_str) && gid_type_str[gid_type])
		return gid_type_str[gid_type];

	return "Invalid GID type";
}
EXPORT_SYMBOL(ib_cache_gid_type_str);

/** rdma_is_zero_gid - Check if given GID is zero or not.
 * @gid:	GID to check
 * Returns true if given GID is zero, returns false otherwise.
 */
bool rdma_is_zero_gid(const union ib_gid *gid)
{
	return !memcmp(gid, &zgid, sizeof(*gid));
}
EXPORT_SYMBOL(rdma_is_zero_gid);

/** is_gid_index_default - Check if a given index belongs to
 * reserved default GIDs or not.
 * @table:	GID table pointer
 * @index:	Index to check in GID table
 * Returns true if index is one of the reserved default GID index otherwise
 * returns false.
 */
static bool is_gid_index_default(const struct ib_gid_table *table,
				 unsigned int index)
{
	return index < 32 && (BIT(index) & table->default_gid_indices);
}

int ib_cache_gid_parse_type_str(const char *buf)
{
	unsigned int i;
	size_t len;
	int err = -EINVAL;

	len = strlen(buf);
	if (len == 0)
		return -EINVAL;

	if (buf[len - 1] == '\n')
		len--;

	for (i = 0; i < ARRAY_SIZE(gid_type_str); ++i)
		if (gid_type_str[i] && !strncmp(buf, gid_type_str[i], len) &&
		    len == strlen(gid_type_str[i])) {
			err = i;
			break;
		}

	return err;
}
EXPORT_SYMBOL(ib_cache_gid_parse_type_str);

static struct ib_gid_table *rdma_gid_table(struct ib_device *device, u32 port)
{
	return device->port_data[port].cache.gid;
}

static bool is_gid_entry_free(const struct ib_gid_table_entry *entry)
{
	return !entry;
}

static bool is_gid_entry_valid(const struct ib_gid_table_entry *entry)
{
	return entry && entry->state == GID_TABLE_ENTRY_VALID;
}

static void schedule_free_gid(struct kref *kref)
{
	struct ib_gid_table_entry *entry =
			container_of(kref, struct ib_gid_table_entry, kref);

	queue_work(ib_wq, &entry->del_work);
}

static void put_gid_ndev(struct rcu_head *head)
{
	struct roce_gid_ndev_storage *storage =
		container_of(head, struct roce_gid_ndev_storage, rcu_head);

	WARN_ON(!storage->ndev);
	/* At this point its safe to release netdev reference,
	 * as all callers working on gid_attr->ndev are done
	 * using this netdev.
	 */
	dev_put(storage->ndev);
	kfree(storage);
}

static void free_gid_entry_locked(struct ib_gid_table_entry *entry)
{
	struct ib_device *device = entry->attr.device;
	u32 port_num = entry->attr.port_num;
	struct ib_gid_table *table = rdma_gid_table(device, port_num);

	dev_dbg(&device->dev, "%s port=%u index=%u gid %pI6\n", __func__,
		port_num, entry->attr.index, entry->attr.gid.raw);

	write_lock_irq(&table->rwlock);

	/*
	 * The only way to avoid overwriting NULL in table is
	 * by comparing if it is same entry in table or not!
	 * If new entry in table is added by the time we free here,
	 * don't overwrite the table entry.
	 */
	if (entry == table->data_vec[entry->attr.index])
		table->data_vec[entry->attr.index] = NULL;
	/* Now this index is ready to be allocated */
	write_unlock_irq(&table->rwlock);

	if (entry->ndev_storage)
		call_rcu(&entry->ndev_storage->rcu_head, put_gid_ndev);
	kfree(entry);
}

static void free_gid_entry(struct kref *kref)
{
	struct ib_gid_table_entry *entry =
			container_of(kref, struct ib_gid_table_entry, kref);

	free_gid_entry_locked(entry);
}

/**
 * free_gid_work - Release reference to the GID entry
 * @work: Work structure to refer to GID entry which needs to be
 * deleted.
 *
 * free_gid_work() frees the entry from the HCA's hardware table
 * if provider supports it. It releases reference to netdevice.
 */
static void free_gid_work(struct work_struct *work)
{
	struct ib_gid_table_entry *entry =
		container_of(work, struct ib_gid_table_entry, del_work);
	struct ib_device *device = entry->attr.device;
	u32 port_num = entry->attr.port_num;
	struct ib_gid_table *table = rdma_gid_table(device, port_num);

	mutex_lock(&table->lock);
	free_gid_entry_locked(entry);
	mutex_unlock(&table->lock);
}

static struct ib_gid_table_entry *
alloc_gid_entry(const struct ib_gid_attr *attr)
{
	struct ib_gid_table_entry *entry;
	struct net_device *ndev;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	ndev = rcu_dereference_protected(attr->ndev, 1);
	if (ndev) {
		entry->ndev_storage = kzalloc(sizeof(*entry->ndev_storage),
					      GFP_KERNEL);
		if (!entry->ndev_storage) {
			kfree(entry);
			return NULL;
		}
		dev_hold(ndev);
		entry->ndev_storage->ndev = ndev;
	}
	kref_init(&entry->kref);
	memcpy(&entry->attr, attr, sizeof(*attr));
	INIT_WORK(&entry->del_work, free_gid_work);
	entry->state = GID_TABLE_ENTRY_INVALID;
	return entry;
}

static void store_gid_entry(struct ib_gid_table *table,
			    struct ib_gid_table_entry *entry)
{
	entry->state = GID_TABLE_ENTRY_VALID;

	dev_dbg(&entry->attr.device->dev, "%s port=%u index=%u gid %pI6\n",
		__func__, entry->attr.port_num, entry->attr.index,
		entry->attr.gid.raw);

	lockdep_assert_held(&table->lock);
	write_lock_irq(&table->rwlock);
	table->data_vec[entry->attr.index] = entry;
	write_unlock_irq(&table->rwlock);
}

static void get_gid_entry(struct ib_gid_table_entry *entry)
{
	kref_get(&entry->kref);
}

static void put_gid_entry(struct ib_gid_table_entry *entry)
{
	kref_put(&entry->kref, schedule_free_gid);
}

static void put_gid_entry_locked(struct ib_gid_table_entry *entry)
{
	kref_put(&entry->kref, free_gid_entry);
}

static int add_roce_gid(struct ib_gid_table_entry *entry)
{
	const struct ib_gid_attr *attr = &entry->attr;
	int ret;

	if (!attr->ndev) {
		dev_err(&attr->device->dev, "%s NULL netdev port=%u index=%u\n",
			__func__, attr->port_num, attr->index);
		return -EINVAL;
	}
	if (rdma_cap_roce_gid_table(attr->device, attr->port_num)) {
		ret = attr->device->ops.add_gid(attr, &entry->context);
		if (ret) {
			dev_err(&attr->device->dev,
				"%s GID add failed port=%u index=%u\n",
				__func__, attr->port_num, attr->index);
			return ret;
		}
	}
	return 0;
}

/**
 * del_gid - Delete GID table entry
 *
 * @ib_dev:	IB device whose GID entry to be deleted
 * @port:	Port number of the IB device
 * @table:	GID table of the IB device for a port
 * @ix:		GID entry index to delete
 *
 */
static void del_gid(struct ib_device *ib_dev, u32 port,
		    struct ib_gid_table *table, int ix)
{
	struct roce_gid_ndev_storage *ndev_storage;
	struct ib_gid_table_entry *entry;

	lockdep_assert_held(&table->lock);

	dev_dbg(&ib_dev->dev, "%s port=%u index=%d gid %pI6\n", __func__, port,
		ix, table->data_vec[ix]->attr.gid.raw);

	write_lock_irq(&table->rwlock);
	entry = table->data_vec[ix];
	entry->state = GID_TABLE_ENTRY_PENDING_DEL;
	/*
	 * For non RoCE protocol, GID entry slot is ready to use.
	 */
	if (!rdma_protocol_roce(ib_dev, port))
		table->data_vec[ix] = NULL;
	write_unlock_irq(&table->rwlock);

	ndev_storage = entry->ndev_storage;
	if (ndev_storage) {
		entry->ndev_storage = NULL;
		rcu_assign_pointer(entry->attr.ndev, NULL);
		call_rcu(&ndev_storage->rcu_head, put_gid_ndev);
	}

	if (rdma_cap_roce_gid_table(ib_dev, port))
		ib_dev->ops.del_gid(&entry->attr, &entry->context);

	put_gid_entry_locked(entry);
}

/**
 * add_modify_gid - Add or modify GID table entry
 *
 * @table:	GID table in which GID to be added or modified
 * @attr:	Attributes of the GID
 *
 * Returns 0 on success or appropriate error code. It accepts zero
 * GID addition for non RoCE ports for HCA's who report them as valid
 * GID. However such zero GIDs are not added to the cache.
 */
static int add_modify_gid(struct ib_gid_table *table,
			  const struct ib_gid_attr *attr)
{
	struct ib_gid_table_entry *entry;
	int ret = 0;

	/*
	 * Invalidate any old entry in the table to make it safe to write to
	 * this index.
	 */
	if (is_gid_entry_valid(table->data_vec[attr->index]))
		del_gid(attr->device, attr->port_num, table, attr->index);

	/*
	 * Some HCA's report multiple GID entries with only one valid GID, and
	 * leave other unused entries as the zero GID. Convert zero GIDs to
	 * empty table entries instead of storing them.
	 */
	if (rdma_is_zero_gid(&attr->gid))
		return 0;

	entry = alloc_gid_entry(attr);
	if (!entry)
		return -ENOMEM;

	if (rdma_protocol_roce(attr->device, attr->port_num)) {
		ret = add_roce_gid(entry);
		if (ret)
			goto done;
	}

	store_gid_entry(table, entry);
	return 0;

done:
	put_gid_entry(entry);
	return ret;
}

/* rwlock should be read locked, or lock should be held */
static int find_gid(struct ib_gid_table *table, const union ib_gid *gid,
		    const struct ib_gid_attr *val, bool default_gid,
		    unsigned long mask, int *pempty)
{
	int i = 0;
	int found = -1;
	int empty = pempty ? -1 : 0;

	while (i < table->sz && (found < 0 || empty < 0)) {
		struct ib_gid_table_entry *data = table->data_vec[i];
		struct ib_gid_attr *attr;
		int curr_index = i;

		i++;

		/* find_gid() is used during GID addition where it is expected
		 * to return a free entry slot which is not duplicate.
		 * Free entry slot is requested and returned if pempty is set,
		 * so lookup free slot only if requested.
		 */
		if (pempty && empty < 0) {
			if (is_gid_entry_free(data) &&
			    default_gid ==
				is_gid_index_default(table, curr_index)) {
				/*
				 * Found an invalid (free) entry; allocate it.
				 * If default GID is requested, then our
				 * found slot must be one of the DEFAULT
				 * reserved slots or we fail.
				 * This ensures that only DEFAULT reserved
				 * slots are used for default property GIDs.
				 */
				empty = curr_index;
			}
		}

		/*
		 * Additionally find_gid() is used to find valid entry during
		 * lookup operation; so ignore the entries which are marked as
		 * pending for removal and the entries which are marked as
		 * invalid.
		 */
		if (!is_gid_entry_valid(data))
			continue;

		if (found >= 0)
			continue;

		attr = &data->attr;
		if (mask & GID_ATTR_FIND_MASK_GID_TYPE &&
		    attr->gid_type != val->gid_type)
			continue;

		if (mask & GID_ATTR_FIND_MASK_GID &&
		    memcmp(gid, &data->attr.gid, sizeof(*gid)))
			continue;

		if (mask & GID_ATTR_FIND_MASK_NETDEV &&
		    attr->ndev != val->ndev)
			continue;

		if (mask & GID_ATTR_FIND_MASK_DEFAULT &&
		    is_gid_index_default(table, curr_index) != default_gid)
			continue;

		found = curr_index;
	}

	if (pempty)
		*pempty = empty;

	return found;
}

static void make_default_gid(struct  net_device *dev, union ib_gid *gid)
{
	gid->global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	addrconf_ifid_eui48(&gid->raw[8], dev);
}

static int __ib_cache_gid_add(struct ib_device *ib_dev, u32 port,
			      union ib_gid *gid, struct ib_gid_attr *attr,
			      unsigned long mask, bool default_gid)
{
	struct ib_gid_table *table;
	int ret = 0;
	int empty;
	int ix;

	/* Do not allow adding zero GID in support of
	 * IB spec version 1.3 section 4.1.1 point (6) and
	 * section 12.7.10 and section 12.7.20
	 */
	if (rdma_is_zero_gid(gid))
		return -EINVAL;

	table = rdma_gid_table(ib_dev, port);

	mutex_lock(&table->lock);

	ix = find_gid(table, gid, attr, default_gid, mask, &empty);
	if (ix >= 0)
		goto out_unlock;

	if (empty < 0) {
		ret = -ENOSPC;
		goto out_unlock;
	}
	attr->device = ib_dev;
	attr->index = empty;
	attr->port_num = port;
	attr->gid = *gid;
	ret = add_modify_gid(table, attr);
	if (!ret)
		dispatch_gid_change_event(ib_dev, port);

out_unlock:
	mutex_unlock(&table->lock);
	if (ret)
		pr_warn("%s: unable to add gid %pI6 error=%d\n",
			__func__, gid->raw, ret);
	return ret;
}

int ib_cache_gid_add(struct ib_device *ib_dev, u32 port,
		     union ib_gid *gid, struct ib_gid_attr *attr)
{
	unsigned long mask = GID_ATTR_FIND_MASK_GID |
			     GID_ATTR_FIND_MASK_GID_TYPE |
			     GID_ATTR_FIND_MASK_NETDEV;

	return __ib_cache_gid_add(ib_dev, port, gid, attr, mask, false);
}

static int
_ib_cache_gid_del(struct ib_device *ib_dev, u32 port,
		  union ib_gid *gid, struct ib_gid_attr *attr,
		  unsigned long mask, bool default_gid)
{
	struct ib_gid_table *table;
	int ret = 0;
	int ix;

	table = rdma_gid_table(ib_dev, port);

	mutex_lock(&table->lock);

	ix = find_gid(table, gid, attr, default_gid, mask, NULL);
	if (ix < 0) {
		ret = -EINVAL;
		goto out_unlock;
	}

	del_gid(ib_dev, port, table, ix);
	dispatch_gid_change_event(ib_dev, port);

out_unlock:
	mutex_unlock(&table->lock);
	if (ret)
		pr_debug("%s: can't delete gid %pI6 error=%d\n",
			 __func__, gid->raw, ret);
	return ret;
}

int ib_cache_gid_del(struct ib_device *ib_dev, u32 port,
		     union ib_gid *gid, struct ib_gid_attr *attr)
{
	unsigned long mask = GID_ATTR_FIND_MASK_GID	  |
			     GID_ATTR_FIND_MASK_GID_TYPE |
			     GID_ATTR_FIND_MASK_DEFAULT  |
			     GID_ATTR_FIND_MASK_NETDEV;

	return _ib_cache_gid_del(ib_dev, port, gid, attr, mask, false);
}

int ib_cache_gid_del_all_netdev_gids(struct ib_device *ib_dev, u32 port,
				     struct net_device *ndev)
{
	struct ib_gid_table *table;
	int ix;
	bool deleted = false;

	table = rdma_gid_table(ib_dev, port);

	mutex_lock(&table->lock);

	for (ix = 0; ix < table->sz; ix++) {
		if (is_gid_entry_valid(table->data_vec[ix]) &&
		    table->data_vec[ix]->attr.ndev == ndev) {
			del_gid(ib_dev, port, table, ix);
			deleted = true;
		}
	}

	mutex_unlock(&table->lock);

	if (deleted)
		dispatch_gid_change_event(ib_dev, port);

	return 0;
}

/**
 * rdma_find_gid_by_port - Returns the GID entry attributes when it finds
 * a valid GID entry for given search parameters. It searches for the specified
 * GID value in the local software cache.
 * @ib_dev: The device to query.
 * @gid: The GID value to search for.
 * @gid_type: The GID type to search for.
 * @port: The port number of the device where the GID value should be searched.
 * @ndev: In RoCE, the net device of the device. NULL means ignore.
 *
 * Returns sgid attributes if the GID is found with valid reference or
 * returns ERR_PTR for the error.
 * The caller must invoke rdma_put_gid_attr() to release the reference.
 */
const struct ib_gid_attr *
rdma_find_gid_by_port(struct ib_device *ib_dev,
		      const union ib_gid *gid,
		      enum ib_gid_type gid_type,
		      u32 port, struct net_device *ndev)
{
	int local_index;
	struct ib_gid_table *table;
	unsigned long mask = GID_ATTR_FIND_MASK_GID |
			     GID_ATTR_FIND_MASK_GID_TYPE;
	struct ib_gid_attr val = {.ndev = ndev, .gid_type = gid_type};
	const struct ib_gid_attr *attr;
	unsigned long flags;

	if (!rdma_is_port_valid(ib_dev, port))
		return ERR_PTR(-ENOENT);

	table = rdma_gid_table(ib_dev, port);

	if (ndev)
		mask |= GID_ATTR_FIND_MASK_NETDEV;

	read_lock_irqsave(&table->rwlock, flags);
	local_index = find_gid(table, gid, &val, false, mask, NULL);
	if (local_index >= 0) {
		get_gid_entry(table->data_vec[local_index]);
		attr = &table->data_vec[local_index]->attr;
		read_unlock_irqrestore(&table->rwlock, flags);
		return attr;
	}

	read_unlock_irqrestore(&table->rwlock, flags);
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(rdma_find_gid_by_port);

/**
 * rdma_find_gid_by_filter - Returns the GID table attribute where a
 * specified GID value occurs
 * @ib_dev: The device to query.
 * @gid: The GID value to search for.
 * @port: The port number of the device where the GID value could be
 *   searched.
 * @filter: The filter function is executed on any matching GID in the table.
 *   If the filter function returns true, the corresponding index is returned,
 *   otherwise, we continue searching the GID table. It's guaranteed that
 *   while filter is executed, ndev field is valid and the structure won't
 *   change. filter is executed in an atomic context. filter must not be NULL.
 * @context: Private data to pass into the call-back.
 *
 * rdma_find_gid_by_filter() searches for the specified GID value
 * of which the filter function returns true in the port's GID table.
 *
 */
const struct ib_gid_attr *rdma_find_gid_by_filter(
	struct ib_device *ib_dev, const union ib_gid *gid, u32 port,
	bool (*filter)(const union ib_gid *gid, const struct ib_gid_attr *,
		       void *),
	void *context)
{
	const struct ib_gid_attr *res = ERR_PTR(-ENOENT);
	struct ib_gid_table *table;
	unsigned long flags;
	unsigned int i;

	if (!rdma_is_port_valid(ib_dev, port))
		return ERR_PTR(-EINVAL);

	table = rdma_gid_table(ib_dev, port);

	read_lock_irqsave(&table->rwlock, flags);
	for (i = 0; i < table->sz; i++) {
		struct ib_gid_table_entry *entry = table->data_vec[i];

		if (!is_gid_entry_valid(entry))
			continue;

		if (memcmp(gid, &entry->attr.gid, sizeof(*gid)))
			continue;

		if (filter(gid, &entry->attr, context)) {
			get_gid_entry(entry);
			res = &entry->attr;
			break;
		}
	}
	read_unlock_irqrestore(&table->rwlock, flags);
	return res;
}

static struct ib_gid_table *alloc_gid_table(int sz)
{
	struct ib_gid_table *table = kzalloc(sizeof(*table), GFP_KERNEL);

	if (!table)
		return NULL;

	table->data_vec = kcalloc(sz, sizeof(*table->data_vec), GFP_KERNEL);
	if (!table->data_vec)
		goto err_free_table;

	mutex_init(&table->lock);

	table->sz = sz;
	rwlock_init(&table->rwlock);
	return table;

err_free_table:
	kfree(table);
	return NULL;
}

static void release_gid_table(struct ib_device *device,
			      struct ib_gid_table *table)
{
	bool leak = false;
	int i;

	if (!table)
		return;

	for (i = 0; i < table->sz; i++) {
		if (is_gid_entry_free(table->data_vec[i]))
			continue;
		if (kref_read(&table->data_vec[i]->kref) > 1) {
			dev_err(&device->dev,
				"GID entry ref leak for index %d ref=%u\n", i,
				kref_read(&table->data_vec[i]->kref));
			leak = true;
		}
	}
	if (leak)
		return;

	mutex_destroy(&table->lock);
	kfree(table->data_vec);
	kfree(table);
}

static void cleanup_gid_table_port(struct ib_device *ib_dev, u32 port,
				   struct ib_gid_table *table)
{
	int i;

	if (!table)
		return;

	mutex_lock(&table->lock);
	for (i = 0; i < table->sz; ++i) {
		if (is_gid_entry_valid(table->data_vec[i]))
			del_gid(ib_dev, port, table, i);
	}
	mutex_unlock(&table->lock);
}

void ib_cache_gid_set_default_gid(struct ib_device *ib_dev, u32 port,
				  struct net_device *ndev,
				  unsigned long gid_type_mask,
				  enum ib_cache_gid_default_mode mode)
{
	union ib_gid gid = { };
	struct ib_gid_attr gid_attr;
	unsigned int gid_type;
	unsigned long mask;

	mask = GID_ATTR_FIND_MASK_GID_TYPE |
	       GID_ATTR_FIND_MASK_DEFAULT |
	       GID_ATTR_FIND_MASK_NETDEV;
	memset(&gid_attr, 0, sizeof(gid_attr));
	gid_attr.ndev = ndev;

	for (gid_type = 0; gid_type < IB_GID_TYPE_SIZE; ++gid_type) {
		if (1UL << gid_type & ~gid_type_mask)
			continue;

		gid_attr.gid_type = gid_type;

		if (mode == IB_CACHE_GID_DEFAULT_MODE_SET) {
			make_default_gid(ndev, &gid);
			__ib_cache_gid_add(ib_dev, port, &gid,
					   &gid_attr, mask, true);
		} else if (mode == IB_CACHE_GID_DEFAULT_MODE_DELETE) {
			_ib_cache_gid_del(ib_dev, port, &gid,
					  &gid_attr, mask, true);
		}
	}
}

static void gid_table_reserve_default(struct ib_device *ib_dev, u32 port,
				      struct ib_gid_table *table)
{
	unsigned int i;
	unsigned long roce_gid_type_mask;
	unsigned int num_default_gids;

	roce_gid_type_mask = roce_gid_type_mask_support(ib_dev, port);
	num_default_gids = hweight_long(roce_gid_type_mask);
	/* Reserve starting indices for default GIDs */
	for (i = 0; i < num_default_gids && i < table->sz; i++)
		table->default_gid_indices |= BIT(i);
}


static void gid_table_release_one(struct ib_device *ib_dev)
{
	u32 p;

	rdma_for_each_port (ib_dev, p) {
		release_gid_table(ib_dev, ib_dev->port_data[p].cache.gid);
		ib_dev->port_data[p].cache.gid = NULL;
	}
}

static int _gid_table_setup_one(struct ib_device *ib_dev)
{
	struct ib_gid_table *table;
	u32 rdma_port;

	rdma_for_each_port (ib_dev, rdma_port) {
		table = alloc_gid_table(
			ib_dev->port_data[rdma_port].immutable.gid_tbl_len);
		if (!table)
			goto rollback_table_setup;

		gid_table_reserve_default(ib_dev, rdma_port, table);
		ib_dev->port_data[rdma_port].cache.gid = table;
	}
	return 0;

rollback_table_setup:
	gid_table_release_one(ib_dev);
	return -ENOMEM;
}

static void gid_table_cleanup_one(struct ib_device *ib_dev)
{
	u32 p;

	rdma_for_each_port (ib_dev, p)
		cleanup_gid_table_port(ib_dev, p,
				       ib_dev->port_data[p].cache.gid);
}

static int gid_table_setup_one(struct ib_device *ib_dev)
{
	int err;

	err = _gid_table_setup_one(ib_dev);

	if (err)
		return err;

	rdma_roce_rescan_device(ib_dev);

	return err;
}

/**
 * rdma_query_gid - Read the GID content from the GID software cache
 * @device:		Device to query the GID
 * @port_num:		Port number of the device
 * @index:		Index of the GID table entry to read
 * @gid:		Pointer to GID where to store the entry's GID
 *
 * rdma_query_gid() only reads the GID entry content for requested device,
 * port and index. It reads for IB, RoCE and iWarp link layers.  It doesn't
 * hold any reference to the GID table entry in the HCA or software cache.
 *
 * Returns 0 on success or appropriate error code.
 *
 */
int rdma_query_gid(struct ib_device *device, u32 port_num,
		   int index, union ib_gid *gid)
{
	struct ib_gid_table *table;
	unsigned long flags;
	int res = -EINVAL;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	table = rdma_gid_table(device, port_num);
	read_lock_irqsave(&table->rwlock, flags);

	if (index < 0 || index >= table->sz ||
	    !is_gid_entry_valid(table->data_vec[index]))
		goto done;

	memcpy(gid, &table->data_vec[index]->attr.gid, sizeof(*gid));
	res = 0;

done:
	read_unlock_irqrestore(&table->rwlock, flags);
	return res;
}
EXPORT_SYMBOL(rdma_query_gid);

/**
 * rdma_read_gid_hw_context - Read the HW GID context from GID attribute
 * @attr:		Potinter to the GID attribute
 *
 * rdma_read_gid_hw_context() reads the drivers GID HW context corresponding
 * to the SGID attr. Callers are required to already be holding the reference
 * to an existing GID entry.
 *
 * Returns the HW GID context
 *
 */
void *rdma_read_gid_hw_context(const struct ib_gid_attr *attr)
{
	return container_of(attr, struct ib_gid_table_entry, attr)->context;
}
EXPORT_SYMBOL(rdma_read_gid_hw_context);

/**
 * rdma_find_gid - Returns SGID attributes if the matching GID is found.
 * @device: The device to query.
 * @gid: The GID value to search for.
 * @gid_type: The GID type to search for.
 * @ndev: In RoCE, the net device of the device. NULL means ignore.
 *
 * rdma_find_gid() searches for the specified GID value in the software cache.
 *
 * Returns GID attributes if a valid GID is found or returns ERR_PTR for the
 * error. The caller must invoke rdma_put_gid_attr() to release the reference.
 *
 */
const struct ib_gid_attr *rdma_find_gid(struct ib_device *device,
					const union ib_gid *gid,
					enum ib_gid_type gid_type,
					struct net_device *ndev)
{
	unsigned long mask = GID_ATTR_FIND_MASK_GID |
			     GID_ATTR_FIND_MASK_GID_TYPE;
	struct ib_gid_attr gid_attr_val = {.ndev = ndev, .gid_type = gid_type};
	u32 p;

	if (ndev)
		mask |= GID_ATTR_FIND_MASK_NETDEV;

	rdma_for_each_port(device, p) {
		struct ib_gid_table *table;
		unsigned long flags;
		int index;

		table = device->port_data[p].cache.gid;
		read_lock_irqsave(&table->rwlock, flags);
		index = find_gid(table, gid, &gid_attr_val, false, mask, NULL);
		if (index >= 0) {
			const struct ib_gid_attr *attr;

			get_gid_entry(table->data_vec[index]);
			attr = &table->data_vec[index]->attr;
			read_unlock_irqrestore(&table->rwlock, flags);
			return attr;
		}
		read_unlock_irqrestore(&table->rwlock, flags);
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(rdma_find_gid);

int ib_get_cached_pkey(struct ib_device *device,
		       u32               port_num,
		       int               index,
		       u16              *pkey)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int ret = 0;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache_lock, flags);

	cache = device->port_data[port_num].cache.pkey;

	if (!cache || index < 0 || index >= cache->table_len)
		ret = -EINVAL;
	else
		*pkey = cache->table[index];

	read_unlock_irqrestore(&device->cache_lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_pkey);

void ib_get_cached_subnet_prefix(struct ib_device *device, u32 port_num,
				u64 *sn_pfx)
{
	unsigned long flags;

	read_lock_irqsave(&device->cache_lock, flags);
	*sn_pfx = device->port_data[port_num].cache.subnet_prefix;
	read_unlock_irqrestore(&device->cache_lock, flags);
}
EXPORT_SYMBOL(ib_get_cached_subnet_prefix);

int ib_find_cached_pkey(struct ib_device *device, u32 port_num,
			u16 pkey, u16 *index)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int i;
	int ret = -ENOENT;
	int partial_ix = -1;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache_lock, flags);

	cache = device->port_data[port_num].cache.pkey;
	if (!cache) {
		ret = -EINVAL;
		goto err;
	}

	*index = -1;

	for (i = 0; i < cache->table_len; ++i)
		if ((cache->table[i] & 0x7fff) == (pkey & 0x7fff)) {
			if (cache->table[i] & 0x8000) {
				*index = i;
				ret = 0;
				break;
			} else {
				partial_ix = i;
			}
		}

	if (ret && partial_ix >= 0) {
		*index = partial_ix;
		ret = 0;
	}

err:
	read_unlock_irqrestore(&device->cache_lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_find_cached_pkey);

int ib_find_exact_cached_pkey(struct ib_device *device, u32 port_num,
			      u16 pkey, u16 *index)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int i;
	int ret = -ENOENT;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache_lock, flags);

	cache = device->port_data[port_num].cache.pkey;
	if (!cache) {
		ret = -EINVAL;
		goto err;
	}

	*index = -1;

	for (i = 0; i < cache->table_len; ++i)
		if (cache->table[i] == pkey) {
			*index = i;
			ret = 0;
			break;
		}

err:
	read_unlock_irqrestore(&device->cache_lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_find_exact_cached_pkey);

int ib_get_cached_lmc(struct ib_device *device, u32 port_num, u8 *lmc)
{
	unsigned long flags;
	int ret = 0;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache_lock, flags);
	*lmc = device->port_data[port_num].cache.lmc;
	read_unlock_irqrestore(&device->cache_lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_lmc);

int ib_get_cached_port_state(struct ib_device *device, u32 port_num,
			     enum ib_port_state *port_state)
{
	unsigned long flags;
	int ret = 0;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache_lock, flags);
	*port_state = device->port_data[port_num].cache.port_state;
	read_unlock_irqrestore(&device->cache_lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_port_state);

/**
 * rdma_get_gid_attr - Returns GID attributes for a port of a device
 * at a requested gid_index, if a valid GID entry exists.
 * @device:		The device to query.
 * @port_num:		The port number on the device where the GID value
 *			is to be queried.
 * @index:		Index of the GID table entry whose attributes are to
 *                      be queried.
 *
 * rdma_get_gid_attr() acquires reference count of gid attributes from the
 * cached GID table. Caller must invoke rdma_put_gid_attr() to release
 * reference to gid attribute regardless of link layer.
 *
 * Returns pointer to valid gid attribute or ERR_PTR for the appropriate error
 * code.
 */
const struct ib_gid_attr *
rdma_get_gid_attr(struct ib_device *device, u32 port_num, int index)
{
	const struct ib_gid_attr *attr = ERR_PTR(-ENODATA);
	struct ib_gid_table *table;
	unsigned long flags;

	if (!rdma_is_port_valid(device, port_num))
		return ERR_PTR(-EINVAL);

	table = rdma_gid_table(device, port_num);
	if (index < 0 || index >= table->sz)
		return ERR_PTR(-EINVAL);

	read_lock_irqsave(&table->rwlock, flags);
	if (!is_gid_entry_valid(table->data_vec[index]))
		goto done;

	get_gid_entry(table->data_vec[index]);
	attr = &table->data_vec[index]->attr;
done:
	read_unlock_irqrestore(&table->rwlock, flags);
	return attr;
}
EXPORT_SYMBOL(rdma_get_gid_attr);

/**
 * rdma_query_gid_table - Reads GID table entries of all the ports of a device up to max_entries.
 * @device: The device to query.
 * @entries: Entries where GID entries are returned.
 * @max_entries: Maximum number of entries that can be returned.
 * Entries array must be allocated to hold max_entries number of entries.
 *
 * Returns number of entries on success or appropriate error code.
 */
ssize_t rdma_query_gid_table(struct ib_device *device,
			     struct ib_uverbs_gid_entry *entries,
			     size_t max_entries)
{
	const struct ib_gid_attr *gid_attr;
	ssize_t num_entries = 0, ret;
	struct ib_gid_table *table;
	u32 port_num, i;
	struct net_device *ndev;
	unsigned long flags;

	rdma_for_each_port(device, port_num) {
		table = rdma_gid_table(device, port_num);
		read_lock_irqsave(&table->rwlock, flags);
		for (i = 0; i < table->sz; i++) {
			if (!is_gid_entry_valid(table->data_vec[i]))
				continue;
			if (num_entries >= max_entries) {
				ret = -EINVAL;
				goto err;
			}

			gid_attr = &table->data_vec[i]->attr;

			memcpy(&entries->gid, &gid_attr->gid,
			       sizeof(gid_attr->gid));
			entries->gid_index = gid_attr->index;
			entries->port_num = gid_attr->port_num;
			entries->gid_type = gid_attr->gid_type;
			ndev = rcu_dereference_protected(
				gid_attr->ndev,
				lockdep_is_held(&table->rwlock));
			if (ndev)
				entries->netdev_ifindex = ndev->ifindex;

			num_entries++;
			entries++;
		}
		read_unlock_irqrestore(&table->rwlock, flags);
	}

	return num_entries;
err:
	read_unlock_irqrestore(&table->rwlock, flags);
	return ret;
}
EXPORT_SYMBOL(rdma_query_gid_table);

/**
 * rdma_put_gid_attr - Release reference to the GID attribute
 * @attr:		Pointer to the GID attribute whose reference
 *			needs to be released.
 *
 * rdma_put_gid_attr() must be used to release reference whose
 * reference is acquired using rdma_get_gid_attr() or any APIs
 * which returns a pointer to the ib_gid_attr regardless of link layer
 * of IB or RoCE.
 *
 */
void rdma_put_gid_attr(const struct ib_gid_attr *attr)
{
	struct ib_gid_table_entry *entry =
		container_of(attr, struct ib_gid_table_entry, attr);

	put_gid_entry(entry);
}
EXPORT_SYMBOL(rdma_put_gid_attr);

/**
 * rdma_hold_gid_attr - Get reference to existing GID attribute
 *
 * @attr:		Pointer to the GID attribute whose reference
 *			needs to be taken.
 *
 * Increase the reference count to a GID attribute to keep it from being
 * freed. Callers are required to already be holding a reference to attribute.
 *
 */
void rdma_hold_gid_attr(const struct ib_gid_attr *attr)
{
	struct ib_gid_table_entry *entry =
		container_of(attr, struct ib_gid_table_entry, attr);

	get_gid_entry(entry);
}
EXPORT_SYMBOL(rdma_hold_gid_attr);

/**
 * rdma_read_gid_attr_ndev_rcu - Read GID attribute netdevice
 * which must be in UP state.
 *
 * @attr:Pointer to the GID attribute
 *
 * Returns pointer to netdevice if the netdevice was attached to GID and
 * netdevice is in UP state. Caller must hold RCU lock as this API
 * reads the netdev flags which can change while netdevice migrates to
 * different net namespace. Returns ERR_PTR with error code otherwise.
 *
 */
struct net_device *rdma_read_gid_attr_ndev_rcu(const struct ib_gid_attr *attr)
{
	struct ib_gid_table_entry *entry =
			container_of(attr, struct ib_gid_table_entry, attr);
	struct ib_device *device = entry->attr.device;
	struct net_device *ndev = ERR_PTR(-EINVAL);
	u32 port_num = entry->attr.port_num;
	struct ib_gid_table *table;
	unsigned long flags;
	bool valid;

	table = rdma_gid_table(device, port_num);

	read_lock_irqsave(&table->rwlock, flags);
	valid = is_gid_entry_valid(table->data_vec[attr->index]);
	if (valid) {
		ndev = rcu_dereference(attr->ndev);
		if (!ndev)
			ndev = ERR_PTR(-ENODEV);
	}
	read_unlock_irqrestore(&table->rwlock, flags);
	return ndev;
}
EXPORT_SYMBOL(rdma_read_gid_attr_ndev_rcu);

static int get_lower_dev_vlan(struct net_device *lower_dev,
			      struct netdev_nested_priv *priv)
{
	u16 *vlan_id = (u16 *)priv->data;

	if (is_vlan_dev(lower_dev))
		*vlan_id = vlan_dev_vlan_id(lower_dev);

	/* We are interested only in first level vlan device, so
	 * always return 1 to stop iterating over next level devices.
	 */
	return 1;
}

/**
 * rdma_read_gid_l2_fields - Read the vlan ID and source MAC address
 *			     of a GID entry.
 *
 * @attr:	GID attribute pointer whose L2 fields to be read
 * @vlan_id:	Pointer to vlan id to fill up if the GID entry has
 *		vlan id. It is optional.
 * @smac:	Pointer to smac to fill up for a GID entry. It is optional.
 *
 * rdma_read_gid_l2_fields() returns 0 on success and returns vlan id
 * (if gid entry has vlan) and source MAC, or returns error.
 */
int rdma_read_gid_l2_fields(const struct ib_gid_attr *attr,
			    u16 *vlan_id, u8 *smac)
{
	struct netdev_nested_priv priv = {
		.data = (void *)vlan_id,
	};
	struct net_device *ndev;

	rcu_read_lock();
	ndev = rcu_dereference(attr->ndev);
	if (!ndev) {
		rcu_read_unlock();
		return -ENODEV;
	}
	if (smac)
		ether_addr_copy(smac, ndev->dev_addr);
	if (vlan_id) {
		*vlan_id = 0xffff;
		if (is_vlan_dev(ndev)) {
			*vlan_id = vlan_dev_vlan_id(ndev);
		} else {
			/* If the netdev is upper device and if it's lower
			 * device is vlan device, consider vlan id of the
			 * the lower vlan device for this gid entry.
			 */
			netdev_walk_all_lower_dev_rcu(attr->ndev,
					get_lower_dev_vlan, &priv);
		}
	}
	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(rdma_read_gid_l2_fields);

static int config_non_roce_gid_cache(struct ib_device *device,
				     u32 port, struct ib_port_attr *tprops)
{
	struct ib_gid_attr gid_attr = {};
	struct ib_gid_table *table;
	int ret = 0;
	int i;

	gid_attr.device = device;
	gid_attr.port_num = port;
	table = rdma_gid_table(device, port);

	mutex_lock(&table->lock);
	for (i = 0; i < tprops->gid_tbl_len; ++i) {
		if (!device->ops.query_gid)
			continue;
		ret = device->ops.query_gid(device, port, i, &gid_attr.gid);
		if (ret) {
			dev_warn(&device->dev,
				 "query_gid failed (%d) for index %d\n", ret,
				 i);
			goto err;
		}
		gid_attr.index = i;
		tprops->subnet_prefix =
			be64_to_cpu(gid_attr.gid.global.subnet_prefix);
		add_modify_gid(table, &gid_attr);
	}
err:
	mutex_unlock(&table->lock);
	return ret;
}

static int
ib_cache_update(struct ib_device *device, u32 port, bool update_gids,
		bool update_pkeys, bool enforce_security)
{
	struct ib_port_attr       *tprops = NULL;
	struct ib_pkey_cache      *pkey_cache = NULL;
	struct ib_pkey_cache      *old_pkey_cache = NULL;
	int                        i;
	int                        ret;

	if (!rdma_is_port_valid(device, port))
		return -EINVAL;

	tprops = kmalloc(sizeof *tprops, GFP_KERNEL);
	if (!tprops)
		return -ENOMEM;

	ret = ib_query_port(device, port, tprops);
	if (ret) {
		dev_warn(&device->dev, "ib_query_port failed (%d)\n", ret);
		goto err;
	}

	if (!rdma_protocol_roce(device, port) && update_gids) {
		ret = config_non_roce_gid_cache(device, port,
						tprops);
		if (ret)
			goto err;
	}

	update_pkeys &= !!tprops->pkey_tbl_len;

	if (update_pkeys) {
		pkey_cache = kmalloc(struct_size(pkey_cache, table,
						 tprops->pkey_tbl_len),
				     GFP_KERNEL);
		if (!pkey_cache) {
			ret = -ENOMEM;
			goto err;
		}

		pkey_cache->table_len = tprops->pkey_tbl_len;

		for (i = 0; i < pkey_cache->table_len; ++i) {
			ret = ib_query_pkey(device, port, i,
					    pkey_cache->table + i);
			if (ret) {
				dev_warn(&device->dev,
					 "ib_query_pkey failed (%d) for index %d\n",
					 ret, i);
				goto err;
			}
		}
	}

	write_lock_irq(&device->cache_lock);

	if (update_pkeys) {
		old_pkey_cache = device->port_data[port].cache.pkey;
		device->port_data[port].cache.pkey = pkey_cache;
	}
	device->port_data[port].cache.lmc = tprops->lmc;
	device->port_data[port].cache.port_state = tprops->state;

	device->port_data[port].cache.subnet_prefix = tprops->subnet_prefix;
	write_unlock_irq(&device->cache_lock);

	if (enforce_security)
		ib_security_cache_change(device,
					 port,
					 tprops->subnet_prefix);

	kfree(old_pkey_cache);
	kfree(tprops);
	return 0;

err:
	kfree(pkey_cache);
	kfree(tprops);
	return ret;
}

static void ib_cache_event_task(struct work_struct *_work)
{
	struct ib_update_work *work =
		container_of(_work, struct ib_update_work, work);
	int ret;

	/* Before distributing the cache update event, first sync
	 * the cache.
	 */
	ret = ib_cache_update(work->event.device, work->event.element.port_num,
			      work->event.event == IB_EVENT_GID_CHANGE,
			      work->event.event == IB_EVENT_PKEY_CHANGE,
			      work->enforce_security);

	/* GID event is notified already for individual GID entries by
	 * dispatch_gid_change_event(). Hence, notifiy for rest of the
	 * events.
	 */
	if (!ret && work->event.event != IB_EVENT_GID_CHANGE)
		ib_dispatch_event_clients(&work->event);

	kfree(work);
}

static void ib_generic_event_task(struct work_struct *_work)
{
	struct ib_update_work *work =
		container_of(_work, struct ib_update_work, work);

	ib_dispatch_event_clients(&work->event);
	kfree(work);
}

static bool is_cache_update_event(const struct ib_event *event)
{
	return (event->event == IB_EVENT_PORT_ERR    ||
		event->event == IB_EVENT_PORT_ACTIVE ||
		event->event == IB_EVENT_LID_CHANGE  ||
		event->event == IB_EVENT_PKEY_CHANGE ||
		event->event == IB_EVENT_CLIENT_REREGISTER ||
		event->event == IB_EVENT_GID_CHANGE);
}

/**
 * ib_dispatch_event - Dispatch an asynchronous event
 * @event:Event to dispatch
 *
 * Low-level drivers must call ib_dispatch_event() to dispatch the
 * event to all registered event handlers when an asynchronous event
 * occurs.
 */
void ib_dispatch_event(const struct ib_event *event)
{
	struct ib_update_work *work;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;

	if (is_cache_update_event(event))
		INIT_WORK(&work->work, ib_cache_event_task);
	else
		INIT_WORK(&work->work, ib_generic_event_task);

	work->event = *event;
	if (event->event == IB_EVENT_PKEY_CHANGE ||
	    event->event == IB_EVENT_GID_CHANGE)
		work->enforce_security = true;

	queue_work(ib_wq, &work->work);
}
EXPORT_SYMBOL(ib_dispatch_event);

int ib_cache_setup_one(struct ib_device *device)
{
	u32 p;
	int err;

	err = gid_table_setup_one(device);
	if (err)
		return err;

	rdma_for_each_port (device, p) {
		err = ib_cache_update(device, p, true, true, true);
		if (err)
			return err;
	}

	return 0;
}

void ib_cache_release_one(struct ib_device *device)
{
	u32 p;

	/*
	 * The release function frees all the cache elements.
	 * This function should be called as part of freeing
	 * all the device's resources when the cache could no
	 * longer be accessed.
	 */
	rdma_for_each_port (device, p)
		kfree(device->port_data[p].cache.pkey);

	gid_table_release_one(device);
}

void ib_cache_cleanup_one(struct ib_device *device)
{
	/* The cleanup function waits for all in-progress workqueue
	 * elements and cleans up the GID cache. This function should be
	 * called after the device was removed from the devices list and
	 * all clients were removed, so the cache exists but is
	 * non-functional and shouldn't be updated anymore.
	 */
	flush_workqueue(ib_wq);
	gid_table_cleanup_one(device);

	/*
	 * Flush the wq second time for any pending GID delete work.
	 */
	flush_workqueue(ib_wq);
}
