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
};

union ib_gid zgid;
EXPORT_SYMBOL(zgid);

static const struct ib_gid_attr zattr;

enum gid_attr_find_mask {
	GID_ATTR_FIND_MASK_GID          = 1UL << 0,
	GID_ATTR_FIND_MASK_NETDEV	= 1UL << 1,
	GID_ATTR_FIND_MASK_DEFAULT	= 1UL << 2,
};

enum gid_table_entry_props {
	GID_TABLE_ENTRY_INVALID		= 1UL << 0,
	GID_TABLE_ENTRY_DEFAULT		= 1UL << 1,
};

enum gid_table_write_action {
	GID_TABLE_WRITE_ACTION_ADD,
	GID_TABLE_WRITE_ACTION_DEL,
	/* MODIFY only updates the GID table. Currently only used by
	 * ib_cache_update.
	 */
	GID_TABLE_WRITE_ACTION_MODIFY
};

struct ib_gid_table_entry {
	/* This lock protects an entry from being
	 * read and written simultaneously.
	 */
	rwlock_t	    lock;
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
	 * Add/delete should be carried out atomically.
	 * This is done by locking this mutex from multiple
	 * writers. We don't need this lock for IB, as the MAD
	 * layer replaces all entries. All data_vec entries
	 * are locked by this lock.
	 **/
	struct mutex         lock;
	struct ib_gid_table_entry *data_vec;
};

static int write_gid(struct ib_device *ib_dev, u8 port,
		     struct ib_gid_table *table, int ix,
		     const union ib_gid *gid,
		     const struct ib_gid_attr *attr,
		     enum gid_table_write_action action,
		     bool  default_gid)
{
	int ret = 0;
	struct net_device *old_net_dev;
	unsigned long flags;

	/* in rdma_cap_roce_gid_table, this funciton should be protected by a
	 * sleep-able lock.
	 */
	write_lock_irqsave(&table->data_vec[ix].lock, flags);

	if (rdma_cap_roce_gid_table(ib_dev, port)) {
		table->data_vec[ix].props |= GID_TABLE_ENTRY_INVALID;
		write_unlock_irqrestore(&table->data_vec[ix].lock, flags);
		/* GID_TABLE_WRITE_ACTION_MODIFY currently isn't supported by
		 * RoCE providers and thus only updates the cache.
		 */
		if (action == GID_TABLE_WRITE_ACTION_ADD)
			ret = ib_dev->add_gid(ib_dev, port, ix, gid, attr,
					      &table->data_vec[ix].context);
		else if (action == GID_TABLE_WRITE_ACTION_DEL)
			ret = ib_dev->del_gid(ib_dev, port, ix,
					      &table->data_vec[ix].context);
		write_lock_irqsave(&table->data_vec[ix].lock, flags);
	}

	old_net_dev = table->data_vec[ix].attr.ndev;
	if (old_net_dev && old_net_dev != attr->ndev)
		dev_put(old_net_dev);
	/* if modify_gid failed, just delete the old gid */
	if (ret || action == GID_TABLE_WRITE_ACTION_DEL) {
		gid = &zgid;
		attr = &zattr;
		table->data_vec[ix].context = NULL;
	}
	if (default_gid)
		table->data_vec[ix].props |= GID_TABLE_ENTRY_DEFAULT;
	memcpy(&table->data_vec[ix].gid, gid, sizeof(*gid));
	memcpy(&table->data_vec[ix].attr, attr, sizeof(*attr));
	if (table->data_vec[ix].attr.ndev &&
	    table->data_vec[ix].attr.ndev != old_net_dev)
		dev_hold(table->data_vec[ix].attr.ndev);

	table->data_vec[ix].props &= ~GID_TABLE_ENTRY_INVALID;

	write_unlock_irqrestore(&table->data_vec[ix].lock, flags);

	if (!ret && rdma_cap_roce_gid_table(ib_dev, port)) {
		struct ib_event event;

		event.device		= ib_dev;
		event.element.port_num	= port;
		event.event		= IB_EVENT_GID_CHANGE;

		ib_dispatch_event(&event);
	}
	return ret;
}

static int add_gid(struct ib_device *ib_dev, u8 port,
		   struct ib_gid_table *table, int ix,
		   const union ib_gid *gid,
		   const struct ib_gid_attr *attr,
		   bool  default_gid) {
	return write_gid(ib_dev, port, table, ix, gid, attr,
			 GID_TABLE_WRITE_ACTION_ADD, default_gid);
}

static int modify_gid(struct ib_device *ib_dev, u8 port,
		      struct ib_gid_table *table, int ix,
		      const union ib_gid *gid,
		      const struct ib_gid_attr *attr,
		      bool  default_gid) {
	return write_gid(ib_dev, port, table, ix, gid, attr,
			 GID_TABLE_WRITE_ACTION_MODIFY, default_gid);
}

static int del_gid(struct ib_device *ib_dev, u8 port,
		   struct ib_gid_table *table, int ix,
		   bool  default_gid) {
	return write_gid(ib_dev, port, table, ix, &zgid, &zattr,
			 GID_TABLE_WRITE_ACTION_DEL, default_gid);
}

static int find_gid(struct ib_gid_table *table, const union ib_gid *gid,
		    const struct ib_gid_attr *val, bool default_gid,
		    unsigned long mask)
{
	int i;

	for (i = 0; i < table->sz; i++) {
		unsigned long flags;
		struct ib_gid_attr *attr = &table->data_vec[i].attr;

		read_lock_irqsave(&table->data_vec[i].lock, flags);

		if (table->data_vec[i].props & GID_TABLE_ENTRY_INVALID)
			goto next;

		if (mask & GID_ATTR_FIND_MASK_GID &&
		    memcmp(gid, &table->data_vec[i].gid, sizeof(*gid)))
			goto next;

		if (mask & GID_ATTR_FIND_MASK_NETDEV &&
		    attr->ndev != val->ndev)
			goto next;

		if (mask & GID_ATTR_FIND_MASK_DEFAULT &&
		    !!(table->data_vec[i].props & GID_TABLE_ENTRY_DEFAULT) !=
		    default_gid)
			goto next;

		read_unlock_irqrestore(&table->data_vec[i].lock, flags);
		return i;
next:
		read_unlock_irqrestore(&table->data_vec[i].lock, flags);
	}

	return -1;
}

static void make_default_gid(struct  net_device *dev, union ib_gid *gid)
{
	gid->global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	addrconf_ifid_eui48(&gid->raw[8], dev);
}

int ib_cache_gid_add(struct ib_device *ib_dev, u8 port,
		     union ib_gid *gid, struct ib_gid_attr *attr)
{
	struct ib_gid_table **ports_table = ib_dev->cache.gid_cache;
	struct ib_gid_table *table;
	int ix;
	int ret = 0;
	struct net_device *idev;

	table = ports_table[port - rdma_start_port(ib_dev)];

	if (!memcmp(gid, &zgid, sizeof(*gid)))
		return -EINVAL;

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

	mutex_lock(&table->lock);

	ix = find_gid(table, gid, attr, false, GID_ATTR_FIND_MASK_GID |
		      GID_ATTR_FIND_MASK_NETDEV);
	if (ix >= 0)
		goto out_unlock;

	ix = find_gid(table, &zgid, NULL, false, GID_ATTR_FIND_MASK_GID |
		      GID_ATTR_FIND_MASK_DEFAULT);
	if (ix < 0) {
		ret = -ENOSPC;
		goto out_unlock;
	}

	add_gid(ib_dev, port, table, ix, gid, attr, false);

out_unlock:
	mutex_unlock(&table->lock);
	return ret;
}

int ib_cache_gid_del(struct ib_device *ib_dev, u8 port,
		     union ib_gid *gid, struct ib_gid_attr *attr)
{
	struct ib_gid_table **ports_table = ib_dev->cache.gid_cache;
	struct ib_gid_table *table;
	int ix;

	table = ports_table[port - rdma_start_port(ib_dev)];

	mutex_lock(&table->lock);

	ix = find_gid(table, gid, attr, false,
		      GID_ATTR_FIND_MASK_GID	  |
		      GID_ATTR_FIND_MASK_NETDEV	  |
		      GID_ATTR_FIND_MASK_DEFAULT);
	if (ix < 0)
		goto out_unlock;

	del_gid(ib_dev, port, table, ix, false);

out_unlock:
	mutex_unlock(&table->lock);
	return 0;
}

int ib_cache_gid_del_all_netdev_gids(struct ib_device *ib_dev, u8 port,
				     struct net_device *ndev)
{
	struct ib_gid_table **ports_table = ib_dev->cache.gid_cache;
	struct ib_gid_table *table;
	int ix;

	table  = ports_table[port - rdma_start_port(ib_dev)];

	mutex_lock(&table->lock);

	for (ix = 0; ix < table->sz; ix++)
		if (table->data_vec[ix].attr.ndev == ndev)
			del_gid(ib_dev, port, table, ix, false);

	mutex_unlock(&table->lock);
	return 0;
}

static int __ib_cache_gid_get(struct ib_device *ib_dev, u8 port, int index,
			      union ib_gid *gid, struct ib_gid_attr *attr)
{
	struct ib_gid_table **ports_table = ib_dev->cache.gid_cache;
	struct ib_gid_table *table;
	unsigned long flags;

	table = ports_table[port - rdma_start_port(ib_dev)];

	if (index < 0 || index >= table->sz)
		return -EINVAL;

	read_lock_irqsave(&table->data_vec[index].lock, flags);
	if (table->data_vec[index].props & GID_TABLE_ENTRY_INVALID) {
		read_unlock_irqrestore(&table->data_vec[index].lock, flags);
		return -EAGAIN;
	}

	memcpy(gid, &table->data_vec[index].gid, sizeof(*gid));
	if (attr) {
		memcpy(attr, &table->data_vec[index].attr, sizeof(*attr));
		if (attr->ndev)
			dev_hold(attr->ndev);
	}

	read_unlock_irqrestore(&table->data_vec[index].lock, flags);
	return 0;
}

static int _ib_cache_gid_table_find(struct ib_device *ib_dev,
				    const union ib_gid *gid,
				    const struct ib_gid_attr *val,
				    unsigned long mask,
				    u8 *port, u16 *index)
{
	struct ib_gid_table **ports_table = ib_dev->cache.gid_cache;
	struct ib_gid_table *table;
	u8 p;
	int local_index;

	for (p = 0; p < ib_dev->phys_port_cnt; p++) {
		table = ports_table[p];
		local_index = find_gid(table, gid, val, false, mask);
		if (local_index >= 0) {
			if (index)
				*index = local_index;
			if (port)
				*port = p + rdma_start_port(ib_dev);
			return 0;
		}
	}

	return -ENOENT;
}

static int ib_cache_gid_find(struct ib_device *ib_dev,
			     const union ib_gid *gid,
			     struct net_device *ndev, u8 *port,
			     u16 *index)
{
	unsigned long mask = GID_ATTR_FIND_MASK_GID;
	struct ib_gid_attr gid_attr_val = {.ndev = ndev};

	if (ndev)
		mask |= GID_ATTR_FIND_MASK_NETDEV;

	return _ib_cache_gid_table_find(ib_dev, gid, &gid_attr_val,
					mask, port, index);
}

int ib_find_cached_gid_by_port(struct ib_device *ib_dev,
			       const union ib_gid *gid,
			       u8 port, struct net_device *ndev,
			       u16 *index)
{
	int local_index;
	struct ib_gid_table **ports_table = ib_dev->cache.gid_cache;
	struct ib_gid_table *table;
	unsigned long mask = GID_ATTR_FIND_MASK_GID;
	struct ib_gid_attr val = {.ndev = ndev};

	if (port < rdma_start_port(ib_dev) ||
	    port > rdma_end_port(ib_dev))
		return -ENOENT;

	table = ports_table[port - rdma_start_port(ib_dev)];

	if (ndev)
		mask |= GID_ATTR_FIND_MASK_NETDEV;

	local_index = find_gid(table, gid, &val, false, mask);
	if (local_index >= 0) {
		if (index)
			*index = local_index;
		return 0;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(ib_find_cached_gid_by_port);

/**
 * ib_find_gid_by_filter - Returns the GID table index where a specified
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
 * @index: The index into the cached GID table where the GID was found.  This
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
	struct ib_gid_table **ports_table = ib_dev->cache.gid_cache;
	struct ib_gid_table *table;
	unsigned int i;
	bool found = false;

	if (!ports_table)
		return -EOPNOTSUPP;

	if (port < rdma_start_port(ib_dev) ||
	    port > rdma_end_port(ib_dev) ||
	    !rdma_protocol_roce(ib_dev, port))
		return -EPROTONOSUPPORT;

	table = ports_table[port - rdma_start_port(ib_dev)];

	for (i = 0; i < table->sz; i++) {
		struct ib_gid_attr attr;
		unsigned long flags;

		read_lock_irqsave(&table->data_vec[i].lock, flags);
		if (table->data_vec[i].props & GID_TABLE_ENTRY_INVALID)
			goto next;

		if (memcmp(gid, &table->data_vec[i].gid, sizeof(*gid)))
			goto next;

		memcpy(&attr, &table->data_vec[i].attr, sizeof(attr));

		if (filter(gid, &attr, context))
			found = true;

next:
		read_unlock_irqrestore(&table->data_vec[i].lock, flags);

		if (found)
			break;
	}

	if (!found)
		return -ENOENT;

	if (index)
		*index = i;
	return 0;
}

static struct ib_gid_table *alloc_gid_table(int sz)
{
	unsigned int i;
	struct ib_gid_table *table =
		kzalloc(sizeof(struct ib_gid_table), GFP_KERNEL);
	if (!table)
		return NULL;

	table->data_vec = kcalloc(sz, sizeof(*table->data_vec), GFP_KERNEL);
	if (!table->data_vec)
		goto err_free_table;

	mutex_init(&table->lock);

	table->sz = sz;

	for (i = 0; i < sz; i++)
		rwlock_init(&table->data_vec[i].lock);

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

	if (!table)
		return;

	for (i = 0; i < table->sz; ++i) {
		if (memcmp(&table->data_vec[i].gid, &zgid,
			   sizeof(table->data_vec[i].gid)))
			del_gid(ib_dev, port, table, i,
				table->data_vec[i].props &
				GID_ATTR_FIND_MASK_DEFAULT);
	}
}

void ib_cache_gid_set_default_gid(struct ib_device *ib_dev, u8 port,
				  struct net_device *ndev,
				  enum ib_cache_gid_default_mode mode)
{
	struct ib_gid_table **ports_table = ib_dev->cache.gid_cache;
	union ib_gid gid;
	struct ib_gid_attr gid_attr;
	struct ib_gid_table *table;
	int ix;
	union ib_gid current_gid;
	struct ib_gid_attr current_gid_attr = {};

	table  = ports_table[port - rdma_start_port(ib_dev)];

	make_default_gid(ndev, &gid);
	memset(&gid_attr, 0, sizeof(gid_attr));
	gid_attr.ndev = ndev;

	mutex_lock(&table->lock);
	ix = find_gid(table, NULL, NULL, true, GID_ATTR_FIND_MASK_DEFAULT);

	/* Coudn't find default GID location */
	WARN_ON(ix < 0);

	if (!__ib_cache_gid_get(ib_dev, port, ix,
				&current_gid, &current_gid_attr) &&
	    mode == IB_CACHE_GID_DEFAULT_MODE_SET &&
	    !memcmp(&gid, &current_gid, sizeof(gid)) &&
	    !memcmp(&gid_attr, &current_gid_attr, sizeof(gid_attr)))
		goto unlock;

	if ((memcmp(&current_gid, &zgid, sizeof(current_gid)) ||
	     memcmp(&current_gid_attr, &zattr,
		    sizeof(current_gid_attr))) &&
	    del_gid(ib_dev, port, table, ix, true)) {
		pr_warn("ib_cache_gid: can't delete index %d for default gid %pI6\n",
			ix, gid.raw);
		goto unlock;
	}

	if (mode == IB_CACHE_GID_DEFAULT_MODE_SET)
		if (add_gid(ib_dev, port, table, ix, &gid, &gid_attr, true))
			pr_warn("ib_cache_gid: unable to add default gid %pI6\n",
				gid.raw);

unlock:
	if (current_gid_attr.ndev)
		dev_put(current_gid_attr.ndev);
	mutex_unlock(&table->lock);
}

static int gid_table_reserve_default(struct ib_device *ib_dev, u8 port,
				     struct ib_gid_table *table)
{
	if (rdma_protocol_roce(ib_dev, port)) {
		struct ib_gid_table_entry *entry = &table->data_vec[0];

		entry->props |= GID_TABLE_ENTRY_DEFAULT;
	}

	return 0;
}

static int _gid_table_setup_one(struct ib_device *ib_dev)
{
	u8 port;
	struct ib_gid_table **table;
	int err = 0;

	table = kcalloc(ib_dev->phys_port_cnt, sizeof(*table), GFP_KERNEL);

	if (!table) {
		pr_warn("failed to allocate ib gid cache for %s\n",
			ib_dev->name);
		return -ENOMEM;
	}

	for (port = 0; port < ib_dev->phys_port_cnt; port++) {
		u8 rdma_port = port + rdma_start_port(ib_dev);

		table[port] =
			alloc_gid_table(
				ib_dev->port_immutable[rdma_port].gid_tbl_len);
		if (!table[port]) {
			err = -ENOMEM;
			goto rollback_table_setup;
		}

		err = gid_table_reserve_default(ib_dev,
						port + rdma_start_port(ib_dev),
						table[port]);
		if (err)
			goto rollback_table_setup;
	}

	ib_dev->cache.gid_cache = table;
	return 0;

rollback_table_setup:
	for (port = 0; port < ib_dev->phys_port_cnt; port++) {
		cleanup_gid_table_port(ib_dev, port + rdma_start_port(ib_dev),
				       table[port]);
		release_gid_table(table[port]);
	}

	kfree(table);
	return err;
}

static void gid_table_release_one(struct ib_device *ib_dev)
{
	struct ib_gid_table **table = ib_dev->cache.gid_cache;
	u8 port;

	if (!table)
		return;

	for (port = 0; port < ib_dev->phys_port_cnt; port++)
		release_gid_table(table[port]);

	kfree(table);
	ib_dev->cache.gid_cache = NULL;
}

static void gid_table_cleanup_one(struct ib_device *ib_dev)
{
	struct ib_gid_table **table = ib_dev->cache.gid_cache;
	u8 port;

	if (!table)
		return;

	for (port = 0; port < ib_dev->phys_port_cnt; port++)
		cleanup_gid_table_port(ib_dev, port + rdma_start_port(ib_dev),
				       table[port]);
}

static int gid_table_setup_one(struct ib_device *ib_dev)
{
	int err;

	err = _gid_table_setup_one(ib_dev);

	if (err)
		return err;

	err = roce_rescan_device(ib_dev);

	if (err) {
		gid_table_cleanup_one(ib_dev);
		gid_table_release_one(ib_dev);
	}

	return err;
}

int ib_get_cached_gid(struct ib_device *device,
		      u8                port_num,
		      int               index,
		      union ib_gid     *gid,
		      struct ib_gid_attr *gid_attr)
{
	if (port_num < rdma_start_port(device) || port_num > rdma_end_port(device))
		return -EINVAL;

	return __ib_cache_gid_get(device, port_num, index, gid, gid_attr);
}
EXPORT_SYMBOL(ib_get_cached_gid);

int ib_find_cached_gid(struct ib_device *device,
		       const union ib_gid *gid,
		       struct net_device *ndev,
		       u8               *port_num,
		       u16              *index)
{
	return ib_cache_gid_find(device, gid, ndev, port_num, index);
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
	if (!rdma_cap_roce_gid_table(device, port_num) && filter)
		return -EPROTONOSUPPORT;

	return ib_cache_gid_find_by_filter(device, gid,
					   port_num, filter,
					   context, index);
}
EXPORT_SYMBOL(ib_find_gid_by_filter);

int ib_get_cached_pkey(struct ib_device *device,
		       u8                port_num,
		       int               index,
		       u16              *pkey)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int ret = 0;

	if (port_num < rdma_start_port(device) || port_num > rdma_end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	cache = device->cache.pkey_cache[port_num - rdma_start_port(device)];

	if (index < 0 || index >= cache->table_len)
		ret = -EINVAL;
	else
		*pkey = cache->table[index];

	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_pkey);

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

	if (port_num < rdma_start_port(device) || port_num > rdma_end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	cache = device->cache.pkey_cache[port_num - rdma_start_port(device)];

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

	if (port_num < rdma_start_port(device) || port_num > rdma_end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	cache = device->cache.pkey_cache[port_num - rdma_start_port(device)];

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

	if (port_num < rdma_start_port(device) || port_num > rdma_end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);
	*lmc = device->cache.lmc_cache[port_num - rdma_start_port(device)];
	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_lmc);

static void ib_cache_update(struct ib_device *device,
			    u8                port)
{
	struct ib_port_attr       *tprops = NULL;
	struct ib_pkey_cache      *pkey_cache = NULL, *old_pkey_cache;
	struct ib_gid_cache {
		int             table_len;
		union ib_gid    table[0];
	}			  *gid_cache = NULL;
	int                        i;
	int                        ret;
	struct ib_gid_table	  *table;
	struct ib_gid_table	 **ports_table = device->cache.gid_cache;
	bool			   use_roce_gid_table =
					rdma_cap_roce_gid_table(device, port);

	if (port < rdma_start_port(device) || port > rdma_end_port(device))
		return;

	table = ports_table[port - rdma_start_port(device)];

	tprops = kmalloc(sizeof *tprops, GFP_KERNEL);
	if (!tprops)
		return;

	ret = ib_query_port(device, port, tprops);
	if (ret) {
		printk(KERN_WARNING "ib_query_port failed (%d) for %s\n",
		       ret, device->name);
		goto err;
	}

	pkey_cache = kmalloc(sizeof *pkey_cache + tprops->pkey_tbl_len *
			     sizeof *pkey_cache->table, GFP_KERNEL);
	if (!pkey_cache)
		goto err;

	pkey_cache->table_len = tprops->pkey_tbl_len;

	if (!use_roce_gid_table) {
		gid_cache = kmalloc(sizeof(*gid_cache) + tprops->gid_tbl_len *
			    sizeof(*gid_cache->table), GFP_KERNEL);
		if (!gid_cache)
			goto err;

		gid_cache->table_len = tprops->gid_tbl_len;
	}

	for (i = 0; i < pkey_cache->table_len; ++i) {
		ret = ib_query_pkey(device, port, i, pkey_cache->table + i);
		if (ret) {
			printk(KERN_WARNING "ib_query_pkey failed (%d) for %s (index %d)\n",
			       ret, device->name, i);
			goto err;
		}
	}

	if (!use_roce_gid_table) {
		for (i = 0;  i < gid_cache->table_len; ++i) {
			ret = ib_query_gid(device, port, i,
					   gid_cache->table + i, NULL);
			if (ret) {
				printk(KERN_WARNING "ib_query_gid failed (%d) for %s (index %d)\n",
				       ret, device->name, i);
				goto err;
			}
		}
	}

	write_lock_irq(&device->cache.lock);

	old_pkey_cache = device->cache.pkey_cache[port - rdma_start_port(device)];

	device->cache.pkey_cache[port - rdma_start_port(device)] = pkey_cache;
	if (!use_roce_gid_table) {
		for (i = 0; i < gid_cache->table_len; i++) {
			modify_gid(device, port, table, i, gid_cache->table + i,
				   &zattr, false);
		}
	}

	device->cache.lmc_cache[port - rdma_start_port(device)] = tprops->lmc;

	write_unlock_irq(&device->cache.lock);

	kfree(gid_cache);
	kfree(old_pkey_cache);
	kfree(tprops);
	return;

err:
	kfree(pkey_cache);
	kfree(gid_cache);
	kfree(tprops);
}

static void ib_cache_task(struct work_struct *_work)
{
	struct ib_update_work *work =
		container_of(_work, struct ib_update_work, work);

	ib_cache_update(work->device, work->port_num);
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
			queue_work(ib_wq, &work->work);
		}
	}
}

int ib_cache_setup_one(struct ib_device *device)
{
	int p;
	int err;

	rwlock_init(&device->cache.lock);

	device->cache.pkey_cache =
		kzalloc(sizeof *device->cache.pkey_cache *
			(rdma_end_port(device) - rdma_start_port(device) + 1), GFP_KERNEL);
	device->cache.lmc_cache = kmalloc(sizeof *device->cache.lmc_cache *
					  (rdma_end_port(device) -
					   rdma_start_port(device) + 1),
					  GFP_KERNEL);
	if (!device->cache.pkey_cache ||
	    !device->cache.lmc_cache) {
		printk(KERN_WARNING "Couldn't allocate cache "
		       "for %s\n", device->name);
		return -ENOMEM;
	}

	err = gid_table_setup_one(device);
	if (err)
		/* Allocated memory will be cleaned in the release function */
		return err;

	for (p = 0; p <= rdma_end_port(device) - rdma_start_port(device); ++p)
		ib_cache_update(device, p + rdma_start_port(device));

	INIT_IB_EVENT_HANDLER(&device->cache.event_handler,
			      device, ib_cache_event);
	err = ib_register_event_handler(&device->cache.event_handler);
	if (err)
		goto err;

	return 0;

err:
	gid_table_cleanup_one(device);
	return err;
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
	if (device->cache.pkey_cache)
		for (p = 0;
		     p <= rdma_end_port(device) - rdma_start_port(device); ++p)
			kfree(device->cache.pkey_cache[p]);

	gid_table_release_one(device);
	kfree(device->cache.pkey_cache);
	kfree(device->cache.lmc_cache);
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
