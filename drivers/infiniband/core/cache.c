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
	u16             table[0];
};

struct ib_update_work {
	struct work_struct work;
	struct ib_device  *device;
	u8                 port_num;
	bool		   enforce_security;
};

union ib_gid zgid;
EXPORT_SYMBOL(zgid);

enum gid_attr_find_mask {
	GID_ATTR_FIND_MASK_GID          = 1UL << 0,
	GID_ATTR_FIND_MASK_NETDEV	= 1UL << 1,
	GID_ATTR_FIND_MASK_DEFAULT	= 1UL << 2,
	GID_ATTR_FIND_MASK_GID_TYPE	= 1UL << 3,
};

enum gid_table_entry_props {
	GID_TABLE_ENTRY_INVALID		= 1UL << 0,
	GID_TABLE_ENTRY_DEFAULT		= 1UL << 1,
};

struct ib_gid_table_entry {
	unsigned long	    props;
	union ib_gid        gid;
	struct ib_gid_attr  attr;
	void		   *context;
};

struct ib_gid_table {
	int                  sz;
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
	 * rwlock. readers must hold only rwlock. All writers must be in a
	 * sleepable context.
	 */
	struct mutex         lock;
	/* rwlock protects data_vec[ix]->props. */
	rwlock_t	     rwlock;
	struct ib_gid_table_entry *data_vec;
};

static void dispatch_gid_change_event(struct ib_device *ib_dev, u8 port)
{
	struct ib_event event;

	event.device		= ib_dev;
	event.element.port_num	= port;
	event.event		= IB_EVENT_GID_CHANGE;

	ib_dispatch_event(&event);
}

static const char * const gid_type_str[] = {
	[IB_GID_TYPE_IB]	= "IB/RoCE v1",
	[IB_GID_TYPE_ROCE_UDP_ENCAP]	= "RoCE v2",
};

const char *ib_cache_gid_type_str(enum ib_gid_type gid_type)
{
	if (gid_type < ARRAY_SIZE(gid_type_str) && gid_type_str[gid_type])
		return gid_type_str[gid_type];

	return "Invalid GID type";
}
EXPORT_SYMBOL(ib_cache_gid_type_str);

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

static void del_roce_gid(struct ib_device *device, u8 port_num,
			 struct ib_gid_table *table, int ix)
{
	pr_debug("%s device=%s port=%d index=%d gid %pI6\n", __func__,
		 device->name, port_num, ix,
		 table->data_vec[ix].gid.raw);

	if (rdma_cap_roce_gid_table(device, port_num))
		device->del_gid(&table->data_vec[ix].attr,
				&table->data_vec[ix].context);
	dev_put(table->data_vec[ix].attr.ndev);
}

static int add_roce_gid(struct ib_gid_table *table,
			const union ib_gid *gid,
			const struct ib_gid_attr *attr)
{
	struct ib_gid_table_entry *entry;
	int ix = attr->index;
	int ret = 0;

	if (!attr->ndev) {
		pr_err("%s NULL netdev device=%s port=%d index=%d\n",
		       __func__, attr->device->name, attr->port_num,
		       attr->index);
		return -EINVAL;
	}

	entry = &table->data_vec[ix];
	if ((entry->props & GID_TABLE_ENTRY_INVALID) == 0) {
		WARN(1, "GID table corruption device=%s port=%d index=%d\n",
		     attr->device->name, attr->port_num,
		     attr->index);
		return -EINVAL;
	}

	if (rdma_cap_roce_gid_table(attr->device, attr->port_num)) {
		ret = attr->device->add_gid(gid, attr, &entry->context);
		if (ret) {
			pr_err("%s GID add failed device=%s port=%d index=%d\n",
			       __func__, attr->device->name, attr->port_num,
			       attr->index);
			goto add_err;
		}
	}
	dev_hold(attr->ndev);

add_err:
	if (!ret)
		pr_debug("%s device=%s port=%d index=%d gid %pI6\n", __func__,
			 attr->device->name, attr->port_num, ix, gid->raw);
	return ret;
}

/**
 * add_modify_gid - Add or modify GID table entry
 *
 * @table:	GID table in which GID to be added or modified
 * @gid:	GID content
 * @attr:	Attributes of the GID
 *
 * Returns 0 on success or appropriate error code. It accepts zero
 * GID addition for non RoCE ports for HCA's who report them as valid
 * GID. However such zero GIDs are not added to the cache.
 */
static int add_modify_gid(struct ib_gid_table *table,
			  const union ib_gid *gid,
			  const struct ib_gid_attr *attr)
{
	int ret;

	if (rdma_protocol_roce(attr->device, attr->port_num)) {
		ret = add_roce_gid(table, gid, attr);
		if (ret)
			return ret;
	} else {
		/*
		 * Some HCA's report multiple GID entries with only one
		 * valid GID, but remaining as zero GID.
		 * So ignore such behavior for IB link layer and don't
		 * fail the call, but don't add such entry to GID cache.
		 */
		if (!memcmp(gid, &zgid, sizeof(*gid)))
			return 0;
	}

	lockdep_assert_held(&table->lock);
	memcpy(&table->data_vec[attr->index].gid, gid, sizeof(*gid));
	memcpy(&table->data_vec[attr->index].attr, attr, sizeof(*attr));

	write_lock_irq(&table->rwlock);
	table->data_vec[attr->index].props &= ~GID_TABLE_ENTRY_INVALID;
	write_unlock_irq(&table->rwlock);
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
static void del_gid(struct ib_device *ib_dev, u8 port,
		    struct ib_gid_table *table, int ix)
{
	lockdep_assert_held(&table->lock);
	write_lock_irq(&table->rwlock);
	table->data_vec[ix].props |= GID_TABLE_ENTRY_INVALID;
	write_unlock_irq(&table->rwlock);

	if (rdma_protocol_roce(ib_dev, port))
		del_roce_gid(ib_dev, port, table, ix);
	memcpy(&table->data_vec[ix].gid, &zgid, sizeof(zgid));
	memset(&table->data_vec[ix].attr, 0, sizeof(table->data_vec[ix].attr));
	table->data_vec[ix].context = NULL;
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
		struct ib_gid_table_entry *data = &table->data_vec[i];
		struct ib_gid_attr *attr = &data->attr;
		int curr_index = i;

		i++;

		/* find_gid() is used during GID addition where it is expected
		 * to return a free entry slot which is not duplicate.
		 * Free entry slot is requested and returned if pempty is set,
		 * so lookup free slot only if requested.
		 */
		if (pempty && empty < 0) {
			if (data->props & GID_TABLE_ENTRY_INVALID &&
			    (default_gid ==
			     !!(data->props & GID_TABLE_ENTRY_DEFAULT))) {
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
		 * lookup operation, where validity needs to be checked. So
		 * find the empty entry first to continue to search for a free
		 * slot and ignore its INVALID flag.
		 */
		if (data->props & GID_TABLE_ENTRY_INVALID)
			continue;

		if (found >= 0)
			continue;

		if (mask & GID_ATTR_FIND_MASK_GID_TYPE &&
		    attr->gid_type != val->gid_type)
			continue;

		if (mask & GID_ATTR_FIND_MASK_GID &&
		    memcmp(gid, &data->gid, sizeof(*gid)))
			continue;

		if (mask & GID_ATTR_FIND_MASK_NETDEV &&
		    attr->ndev != val->ndev)
			continue;

		if (mask & GID_ATTR_FIND_MASK_DEFAULT &&
		    !!(data->props & GID_TABLE_ENTRY_DEFAULT) !=
		    default_gid)
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

static int __ib_cache_gid_add(struct ib_device *ib_dev, u8 port,
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
	if (!memcmp(gid, &zgid, sizeof(*gid)))
		return -EINVAL;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

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
	ret = add_modify_gid(table, gid, attr);
	if (!ret)
		dispatch_gid_change_event(ib_dev, port);

out_unlock:
	mutex_unlock(&table->lock);
	if (ret)
		pr_warn("%s: unable to add gid %pI6 error=%d\n",
			__func__, gid->raw, ret);
	return ret;
}

int ib_cache_gid_add(struct ib_device *ib_dev, u8 port,
		     union ib_gid *gid, struct ib_gid_attr *attr)
{
	struct net_device *idev;
	unsigned long mask;
	int ret;

	if (ib_dev->get_netdev) {
		idev = ib_dev->get_netdev(ib_dev, port);
		if (idev && attr->ndev != idev) {
			union ib_gid default_gid;

			/* Adding default GIDs in not permitted */
			make_default_gid(idev, &default_gid);
			if (!memcmp(gid, &default_gid, sizeof(*gid))) {
				dev_put(idev);
				return -EPERM;
			}
		}
		if (idev)
			dev_put(idev);
	}

	mask = GID_ATTR_FIND_MASK_GID |
	       GID_ATTR_FIND_MASK_GID_TYPE |
	       GID_ATTR_FIND_MASK_NETDEV;

	ret = __ib_cache_gid_add(ib_dev, port, gid, attr, mask, false);
	return ret;
}

static int
_ib_cache_gid_del(struct ib_device *ib_dev, u8 port,
		  union ib_gid *gid, struct ib_gid_attr *attr,
		  unsigned long mask, bool default_gid)
{
	struct ib_gid_table *table;
	int ret = 0;
	int ix;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

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

int ib_cache_gid_del(struct ib_device *ib_dev, u8 port,
		     union ib_gid *gid, struct ib_gid_attr *attr)
{
	unsigned long mask = GID_ATTR_FIND_MASK_GID	  |
			     GID_ATTR_FIND_MASK_GID_TYPE |
			     GID_ATTR_FIND_MASK_DEFAULT  |
			     GID_ATTR_FIND_MASK_NETDEV;

	return _ib_cache_gid_del(ib_dev, port, gid, attr, mask, false);
}

int ib_cache_gid_del_all_netdev_gids(struct ib_device *ib_dev, u8 port,
				     struct net_device *ndev)
{
	struct ib_gid_table *table;
	int ix;
	bool deleted = false;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

	mutex_lock(&table->lock);

	for (ix = 0; ix < table->sz; ix++) {
		if (table->data_vec[ix].attr.ndev == ndev) {
			del_gid(ib_dev, port, table, ix);
			deleted = true;
		}
	}

	mutex_unlock(&table->lock);

	if (deleted)
		dispatch_gid_change_event(ib_dev, port);

	return 0;
}

static int __ib_cache_gid_get(struct ib_device *ib_dev, u8 port, int index,
			      union ib_gid *gid, struct ib_gid_attr *attr)
{
	struct ib_gid_table *table;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

	if (index < 0 || index >= table->sz)
		return -EINVAL;

	if (table->data_vec[index].props & GID_TABLE_ENTRY_INVALID)
		return -EAGAIN;

	memcpy(gid, &table->data_vec[index].gid, sizeof(*gid));
	if (attr) {
		memcpy(attr, &table->data_vec[index].attr, sizeof(*attr));
		if (attr->ndev)
			dev_hold(attr->ndev);
	}

	return 0;
}

static int _ib_cache_gid_table_find(struct ib_device *ib_dev,
				    const union ib_gid *gid,
				    const struct ib_gid_attr *val,
				    unsigned long mask,
				    u8 *port, u16 *index)
{
	struct ib_gid_table *table;
	u8 p;
	int local_index;
	unsigned long flags;

	for (p = 0; p < ib_dev->phys_port_cnt; p++) {
		table = ib_dev->cache.ports[p].gid;
		read_lock_irqsave(&table->rwlock, flags);
		local_index = find_gid(table, gid, val, false, mask, NULL);
		if (local_index >= 0) {
			if (index)
				*index = local_index;
			if (port)
				*port = p + rdma_start_port(ib_dev);
			read_unlock_irqrestore(&table->rwlock, flags);
			return 0;
		}
		read_unlock_irqrestore(&table->rwlock, flags);
	}

	return -ENOENT;
}

static int ib_cache_gid_find(struct ib_device *ib_dev,
			     const union ib_gid *gid,
			     enum ib_gid_type gid_type,
			     struct net_device *ndev, u8 *port,
			     u16 *index)
{
	unsigned long mask = GID_ATTR_FIND_MASK_GID |
			     GID_ATTR_FIND_MASK_GID_TYPE;
	struct ib_gid_attr gid_attr_val = {.ndev = ndev, .gid_type = gid_type};

	if (ndev)
		mask |= GID_ATTR_FIND_MASK_NETDEV;

	return _ib_cache_gid_table_find(ib_dev, gid, &gid_attr_val,
					mask, port, index);
}

/**
 * ib_find_cached_gid_by_port - Returns the GID table index where a specified
 * GID value occurs. It searches for the specified GID value in the local
 * software cache.
 * @device: The device to query.
 * @gid: The GID value to search for.
 * @gid_type: The GID type to search for.
 * @port_num: The port number of the device where the GID value should be
 *   searched.
 * @ndev: In RoCE, the net device of the device. Null means ignore.
 * @index: The index into the cached GID table where the GID was found. This
 *   parameter may be NULL.
 */
int ib_find_cached_gid_by_port(struct ib_device *ib_dev,
			       const union ib_gid *gid,
			       enum ib_gid_type gid_type,
			       u8 port, struct net_device *ndev,
			       u16 *index)
{
	int local_index;
	struct ib_gid_table *table;
	unsigned long mask = GID_ATTR_FIND_MASK_GID |
			     GID_ATTR_FIND_MASK_GID_TYPE;
	struct ib_gid_attr val = {.ndev = ndev, .gid_type = gid_type};
	unsigned long flags;

	if (!rdma_is_port_valid(ib_dev, port))
		return -ENOENT;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

	if (ndev)
		mask |= GID_ATTR_FIND_MASK_NETDEV;

	read_lock_irqsave(&table->rwlock, flags);
	local_index = find_gid(table, gid, &val, false, mask, NULL);
	if (local_index >= 0) {
		if (index)
			*index = local_index;
		read_unlock_irqrestore(&table->rwlock, flags);
		return 0;
	}

	read_unlock_irqrestore(&table->rwlock, flags);
	return -ENOENT;
}
EXPORT_SYMBOL(ib_find_cached_gid_by_port);

/**
 * ib_cache_gid_find_by_filter - Returns the GID table index where a specified
 * GID value occurs
 * @device: The device to query.
 * @gid: The GID value to search for.
 * @port_num: The port number of the device where the GID value could be
 *   searched.
 * @filter: The filter function is executed on any matching GID in the table.
 *   If the filter function returns true, the corresponding index is returned,
 *   otherwise, we continue searching the GID table. It's guaranteed that
 *   while filter is executed, ndev field is valid and the structure won't
 *   change. filter is executed in an atomic context. filter must not be NULL.
 * @index: The index into the cached GID table where the GID was found. This
 *   parameter may be NULL.
 *
 * ib_cache_gid_find_by_filter() searches for the specified GID value
 * of which the filter function returns true in the port's GID table.
 * This function is only supported on RoCE ports.
 *
 */
static int ib_cache_gid_find_by_filter(struct ib_device *ib_dev,
				       const union ib_gid *gid,
				       u8 port,
				       bool (*filter)(const union ib_gid *,
						      const struct ib_gid_attr *,
						      void *),
				       void *context,
				       u16 *index)
{
	struct ib_gid_table *table;
	unsigned int i;
	unsigned long flags;
	bool found = false;


	if (!rdma_is_port_valid(ib_dev, port) ||
	    !rdma_protocol_roce(ib_dev, port))
		return -EPROTONOSUPPORT;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

	read_lock_irqsave(&table->rwlock, flags);
	for (i = 0; i < table->sz; i++) {
		struct ib_gid_attr attr;

		if (table->data_vec[i].props & GID_TABLE_ENTRY_INVALID)
			continue;

		if (memcmp(gid, &table->data_vec[i].gid, sizeof(*gid)))
			continue;

		memcpy(&attr, &table->data_vec[i].attr, sizeof(attr));

		if (filter(gid, &attr, context)) {
			found = true;
			if (index)
				*index = i;
			break;
		}
	}
	read_unlock_irqrestore(&table->rwlock, flags);

	if (!found)
		return -ENOENT;
	return 0;
}

static struct ib_gid_table *alloc_gid_table(int sz)
{
	struct ib_gid_table *table =
		kzalloc(sizeof(struct ib_gid_table), GFP_KERNEL);
	int i;

	if (!table)
		return NULL;

	table->data_vec = kcalloc(sz, sizeof(*table->data_vec), GFP_KERNEL);
	if (!table->data_vec)
		goto err_free_table;

	mutex_init(&table->lock);

	table->sz = sz;
	rwlock_init(&table->rwlock);

	/* Mark all entries as invalid so that allocator can allocate
	 * one of the invalid (free) entry.
	 */
	for (i = 0; i < sz; i++)
		table->data_vec[i].props |= GID_TABLE_ENTRY_INVALID;
	return table;

err_free_table:
	kfree(table);
	return NULL;
}

static void release_gid_table(struct ib_gid_table *table)
{
	if (table) {
		kfree(table->data_vec);
		kfree(table);
	}
}

static void cleanup_gid_table_port(struct ib_device *ib_dev, u8 port,
				   struct ib_gid_table *table)
{
	int i;
	bool deleted = false;

	if (!table)
		return;

	mutex_lock(&table->lock);
	for (i = 0; i < table->sz; ++i) {
		if (memcmp(&table->data_vec[i].gid, &zgid,
			   sizeof(table->data_vec[i].gid))) {
			del_gid(ib_dev, port, table, i);
			deleted = true;
		}
	}
	mutex_unlock(&table->lock);

	if (deleted)
		dispatch_gid_change_event(ib_dev, port);
}

void ib_cache_gid_set_default_gid(struct ib_device *ib_dev, u8 port,
				  struct net_device *ndev,
				  unsigned long gid_type_mask,
				  enum ib_cache_gid_default_mode mode)
{
	union ib_gid gid = { };
	struct ib_gid_attr gid_attr;
	struct ib_gid_table *table;
	unsigned int gid_type;
	unsigned long mask;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

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

static int gid_table_reserve_default(struct ib_device *ib_dev, u8 port,
				     struct ib_gid_table *table)
{
	unsigned int i;
	unsigned long roce_gid_type_mask;
	unsigned int num_default_gids;
	unsigned int current_gid = 0;

	roce_gid_type_mask = roce_gid_type_mask_support(ib_dev, port);
	num_default_gids = hweight_long(roce_gid_type_mask);
	for (i = 0; i < num_default_gids && i < table->sz; i++) {
		struct ib_gid_table_entry *entry =
			&table->data_vec[i];

		entry->props |= GID_TABLE_ENTRY_DEFAULT;
		current_gid = find_next_bit(&roce_gid_type_mask,
					    BITS_PER_LONG,
					    current_gid);
		entry->attr.gid_type = current_gid++;
	}

	return 0;
}

static int _gid_table_setup_one(struct ib_device *ib_dev)
{
	u8 port;
	struct ib_gid_table *table;
	int err = 0;

	for (port = 0; port < ib_dev->phys_port_cnt; port++) {
		u8 rdma_port = port + rdma_start_port(ib_dev);

		table =
			alloc_gid_table(
				ib_dev->port_immutable[rdma_port].gid_tbl_len);
		if (!table) {
			err = -ENOMEM;
			goto rollback_table_setup;
		}

		err = gid_table_reserve_default(ib_dev,
						port + rdma_start_port(ib_dev),
						table);
		if (err)
			goto rollback_table_setup;
		ib_dev->cache.ports[port].gid = table;
	}

	return 0;

rollback_table_setup:
	for (port = 0; port < ib_dev->phys_port_cnt; port++) {
		table = ib_dev->cache.ports[port].gid;

		cleanup_gid_table_port(ib_dev, port + rdma_start_port(ib_dev),
				       table);
		release_gid_table(table);
	}

	return err;
}

static void gid_table_release_one(struct ib_device *ib_dev)
{
	struct ib_gid_table *table;
	u8 port;

	for (port = 0; port < ib_dev->phys_port_cnt; port++) {
		table = ib_dev->cache.ports[port].gid;
		release_gid_table(table);
		ib_dev->cache.ports[port].gid = NULL;
	}
}

static void gid_table_cleanup_one(struct ib_device *ib_dev)
{
	struct ib_gid_table *table;
	u8 port;

	for (port = 0; port < ib_dev->phys_port_cnt; port++) {
		table = ib_dev->cache.ports[port].gid;
		cleanup_gid_table_port(ib_dev, port + rdma_start_port(ib_dev),
				       table);
	}
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

int ib_get_cached_gid(struct ib_device *device,
		      u8                port_num,
		      int               index,
		      union ib_gid     *gid,
		      struct ib_gid_attr *gid_attr)
{
	int res;
	unsigned long flags;
	struct ib_gid_table *table;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	table = device->cache.ports[port_num - rdma_start_port(device)].gid;
	read_lock_irqsave(&table->rwlock, flags);
	res = __ib_cache_gid_get(device, port_num, index, gid, gid_attr);
	read_unlock_irqrestore(&table->rwlock, flags);

	return res;
}
EXPORT_SYMBOL(ib_get_cached_gid);

/**
 * ib_find_cached_gid - Returns the port number and GID table index where
 *   a specified GID value occurs.
 * @device: The device to query.
 * @gid: The GID value to search for.
 * @gid_type: The GID type to search for.
 * @ndev: In RoCE, the net device of the device. NULL means ignore.
 * @port_num: The port number of the device where the GID value was found.
 * @index: The index into the cached GID table where the GID was found.  This
 *   parameter may be NULL.
 *
 * ib_find_cached_gid() searches for the specified GID value in
 * the local software cache.
 */
int ib_find_cached_gid(struct ib_device *device,
		       const union ib_gid *gid,
		       enum ib_gid_type gid_type,
		       struct net_device *ndev,
		       u8               *port_num,
		       u16              *index)
{
	return ib_cache_gid_find(device, gid, gid_type, ndev, port_num, index);
}
EXPORT_SYMBOL(ib_find_cached_gid);

int ib_find_gid_by_filter(struct ib_device *device,
			  const union ib_gid *gid,
			  u8 port_num,
			  bool (*filter)(const union ib_gid *gid,
					 const struct ib_gid_attr *,
					 void *),
			  void *context, u16 *index)
{
	/* Only RoCE GID table supports filter function */
	if (!rdma_protocol_roce(device, port_num) && filter)
		return -EPROTONOSUPPORT;

	return ib_cache_gid_find_by_filter(device, gid,
					   port_num, filter,
					   context, index);
}

int ib_get_cached_pkey(struct ib_device *device,
		       u8                port_num,
		       int               index,
		       u16              *pkey)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int ret = 0;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	cache = device->cache.ports[port_num - rdma_start_port(device)].pkey;

	if (index < 0 || index >= cache->table_len)
		ret = -EINVAL;
	else
		*pkey = cache->table[index];

	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_pkey);

int ib_get_cached_subnet_prefix(struct ib_device *device,
				u8                port_num,
				u64              *sn_pfx)
{
	unsigned long flags;
	int p;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	p = port_num - rdma_start_port(device);
	read_lock_irqsave(&device->cache.lock, flags);
	*sn_pfx = device->cache.ports[p].subnet_prefix;
	read_unlock_irqrestore(&device->cache.lock, flags);

	return 0;
}
EXPORT_SYMBOL(ib_get_cached_subnet_prefix);

int ib_find_cached_pkey(struct ib_device *device,
			u8                port_num,
			u16               pkey,
			u16              *index)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int i;
	int ret = -ENOENT;
	int partial_ix = -1;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	cache = device->cache.ports[port_num - rdma_start_port(device)].pkey;

	*index = -1;

	for (i = 0; i < cache->table_len; ++i)
		if ((cache->table[i] & 0x7fff) == (pkey & 0x7fff)) {
			if (cache->table[i] & 0x8000) {
				*index = i;
				ret = 0;
				break;
			} else
				partial_ix = i;
		}

	if (ret && partial_ix >= 0) {
		*index = partial_ix;
		ret = 0;
	}

	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_find_cached_pkey);

int ib_find_exact_cached_pkey(struct ib_device *device,
			      u8                port_num,
			      u16               pkey,
			      u16              *index)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int i;
	int ret = -ENOENT;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	cache = device->cache.ports[port_num - rdma_start_port(device)].pkey;

	*index = -1;

	for (i = 0; i < cache->table_len; ++i)
		if (cache->table[i] == pkey) {
			*index = i;
			ret = 0;
			break;
		}

	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_find_exact_cached_pkey);

int ib_get_cached_lmc(struct ib_device *device,
		      u8                port_num,
		      u8                *lmc)
{
	unsigned long flags;
	int ret = 0;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);
	*lmc = device->cache.ports[port_num - rdma_start_port(device)].lmc;
	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_lmc);

int ib_get_cached_port_state(struct ib_device   *device,
			     u8                  port_num,
			     enum ib_port_state *port_state)
{
	unsigned long flags;
	int ret = 0;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);
	*port_state = device->cache.ports[port_num
		- rdma_start_port(device)].port_state;
	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_port_state);

static int config_non_roce_gid_cache(struct ib_device *device,
				     u8 port, int gid_tbl_len)
{
	struct ib_gid_attr gid_attr = {};
	struct ib_gid_table *table;
	union ib_gid gid;
	int ret = 0;
	int i;

	gid_attr.device = device;
	gid_attr.port_num = port;
	table = device->cache.ports[port - rdma_start_port(device)].gid;

	mutex_lock(&table->lock);
	for (i = 0; i < gid_tbl_len; ++i) {
		if (!device->query_gid)
			continue;
		ret = device->query_gid(device, port, i, &gid);
		if (ret) {
			pr_warn("query_gid failed (%d) for %s (index %d)\n",
				ret, device->name, i);
			goto err;
		}
		gid_attr.index = i;
		add_modify_gid(table, &gid, &gid_attr);
	}
err:
	mutex_unlock(&table->lock);
	return ret;
}

static void ib_cache_update(struct ib_device *device,
			    u8                port,
			    bool	      enforce_security)
{
	struct ib_port_attr       *tprops = NULL;
	struct ib_pkey_cache      *pkey_cache = NULL, *old_pkey_cache;
	int                        i;
	int                        ret;
	struct ib_gid_table	  *table;

	if (!rdma_is_port_valid(device, port))
		return;

	table = device->cache.ports[port - rdma_start_port(device)].gid;

	tprops = kmalloc(sizeof *tprops, GFP_KERNEL);
	if (!tprops)
		return;

	ret = ib_query_port(device, port, tprops);
	if (ret) {
		pr_warn("ib_query_port failed (%d) for %s\n",
			ret, device->name);
		goto err;
	}

	if (!rdma_protocol_roce(device, port)) {
		ret = config_non_roce_gid_cache(device, port,
						tprops->gid_tbl_len);
		if (ret)
			goto err;
	}

	pkey_cache = kmalloc(sizeof *pkey_cache + tprops->pkey_tbl_len *
			     sizeof *pkey_cache->table, GFP_KERNEL);
	if (!pkey_cache)
		goto err;

	pkey_cache->table_len = tprops->pkey_tbl_len;

	for (i = 0; i < pkey_cache->table_len; ++i) {
		ret = ib_query_pkey(device, port, i, pkey_cache->table + i);
		if (ret) {
			pr_warn("ib_query_pkey failed (%d) for %s (index %d)\n",
				ret, device->name, i);
			goto err;
		}
	}

	write_lock_irq(&device->cache.lock);

	old_pkey_cache = device->cache.ports[port -
		rdma_start_port(device)].pkey;

	device->cache.ports[port - rdma_start_port(device)].pkey = pkey_cache;
	device->cache.ports[port - rdma_start_port(device)].lmc = tprops->lmc;
	device->cache.ports[port - rdma_start_port(device)].port_state =
		tprops->state;

	device->cache.ports[port - rdma_start_port(device)].subnet_prefix =
							tprops->subnet_prefix;
	write_unlock_irq(&device->cache.lock);

	if (enforce_security)
		ib_security_cache_change(device,
					 port,
					 tprops->subnet_prefix);

	kfree(old_pkey_cache);
	kfree(tprops);
	return;

err:
	kfree(pkey_cache);
	kfree(tprops);
}

static void ib_cache_task(struct work_struct *_work)
{
	struct ib_update_work *work =
		container_of(_work, struct ib_update_work, work);

	ib_cache_update(work->device,
			work->port_num,
			work->enforce_security);
	kfree(work);
}

static void ib_cache_event(struct ib_event_handler *handler,
			   struct ib_event *event)
{
	struct ib_update_work *work;

	if (event->event == IB_EVENT_PORT_ERR    ||
	    event->event == IB_EVENT_PORT_ACTIVE ||
	    event->event == IB_EVENT_LID_CHANGE  ||
	    event->event == IB_EVENT_PKEY_CHANGE ||
	    event->event == IB_EVENT_SM_CHANGE   ||
	    event->event == IB_EVENT_CLIENT_REREGISTER ||
	    event->event == IB_EVENT_GID_CHANGE) {
		work = kmalloc(sizeof *work, GFP_ATOMIC);
		if (work) {
			INIT_WORK(&work->work, ib_cache_task);
			work->device   = event->device;
			work->port_num = event->element.port_num;
			if (event->event == IB_EVENT_PKEY_CHANGE ||
			    event->event == IB_EVENT_GID_CHANGE)
				work->enforce_security = true;
			else
				work->enforce_security = false;

			queue_work(ib_wq, &work->work);
		}
	}
}

int ib_cache_setup_one(struct ib_device *device)
{
	int p;
	int err;

	rwlock_init(&device->cache.lock);

	device->cache.ports =
		kzalloc(sizeof(*device->cache.ports) *
			(rdma_end_port(device) - rdma_start_port(device) + 1), GFP_KERNEL);
	if (!device->cache.ports)
		return -ENOMEM;

	err = gid_table_setup_one(device);
	if (err) {
		kfree(device->cache.ports);
		device->cache.ports = NULL;
		return err;
	}

	for (p = 0; p <= rdma_end_port(device) - rdma_start_port(device); ++p)
		ib_cache_update(device, p + rdma_start_port(device), true);

	INIT_IB_EVENT_HANDLER(&device->cache.event_handler,
			      device, ib_cache_event);
	ib_register_event_handler(&device->cache.event_handler);
	return 0;
}

void ib_cache_release_one(struct ib_device *device)
{
	int p;

	/*
	 * The release function frees all the cache elements.
	 * This function should be called as part of freeing
	 * all the device's resources when the cache could no
	 * longer be accessed.
	 */
	for (p = 0; p <= rdma_end_port(device) - rdma_start_port(device); ++p)
		kfree(device->cache.ports[p].pkey);

	gid_table_release_one(device);
	kfree(device->cache.ports);
}

void ib_cache_cleanup_one(struct ib_device *device)
{
	/* The cleanup function unregisters the event handler,
	 * waits for all in-progress workqueue elements and cleans
	 * up the GID cache. This function should be called after
	 * the device was removed from the devices list and all
	 * clients were removed, so the cache exists but is
	 * non-functional and shouldn't be updated anymore.
	 */
	ib_unregister_event_handler(&device->cache.event_handler);
	flush_workqueue(ib_wq);
	gid_table_cleanup_one(device);
}

void __init ib_cache_setup(void)
{
	roce_gid_mgmt_init();
}

void __exit ib_cache_cleanup(void)
{
	roce_gid_mgmt_cleanup();
}
