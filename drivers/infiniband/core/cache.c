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

static const struct ib_gid_attr zattr;

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

enum gid_table_write_action {
	GID_TABLE_WRITE_ACTION_ADD,
	GID_TABLE_WRITE_ACTION_DEL,
	/* MODIFY only updates the GID table. Currently only used by
	 * ib_cache_update.
	 */
	GID_TABLE_WRITE_ACTION_MODIFY
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
	 * Add/delete should be carried out atomically.
	 * This is done by locking this mutex from multiple
	 * writers. We don't need this lock for IB, as the MAD
	 * layer replaces all entries. All data_vec entries
	 * are locked by this lock.
	 **/
	struct mutex         lock;
	/* This lock protects the table entries from being
	 * read and written simultaneously.
	 */
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

/* This function expects that rwlock will be write locked in all
 * scenarios and that lock will be locked in sleep-able (RoCE)
 * scenarios.
 */
static int write_gid(struct ib_device *ib_dev, u8 port,
		     struct ib_gid_table *table, int ix,
		     const union ib_gid *gid,
		     const struct ib_gid_attr *attr,
		     enum gid_table_write_action action,
		     bool  default_gid)
	__releases(&table->rwlock) __acquires(&table->rwlock)
{
	int ret = 0;
	struct net_device *old_net_dev;
	enum ib_gid_type old_gid_type;

	/* in rdma_cap_roce_gid_table, this funciton should be protected by a
	 * sleep-able lock.
	 */

	if (rdma_cap_roce_gid_table(ib_dev, port)) {
		table->data_vec[ix].props |= GID_TABLE_ENTRY_INVALID;
		write_unlock_irq(&table->rwlock);
		/* GID_TABLE_WRITE_ACTION_MODIFY currently isn't supported by
		 * RoCE providers and thus only updates the cache.
		 */
		if (action == GID_TABLE_WRITE_ACTION_ADD)
			ret = ib_dev->add_gid(ib_dev, port, ix, gid, attr,
					      &table->data_vec[ix].context);
		else if (action == GID_TABLE_WRITE_ACTION_DEL)
			ret = ib_dev->del_gid(ib_dev, port, ix,
					      &table->data_vec[ix].context);
		write_lock_irq(&table->rwlock);
	}

	old_net_dev = table->data_vec[ix].attr.ndev;
	old_gid_type = table->data_vec[ix].attr.gid_type;
	if (old_net_dev && old_net_dev != attr->ndev)
		dev_put(old_net_dev);
	/* if modify_gid failed, just delete the old gid */
	if (ret || action == GID_TABLE_WRITE_ACTION_DEL) {
		gid = &zgid;
		attr = &zattr;
		table->data_vec[ix].context = NULL;
	}

	memcpy(&table->data_vec[ix].gid, gid, sizeof(*gid));
	memcpy(&table->data_vec[ix].attr, attr, sizeof(*attr));
	if (default_gid) {
		table->data_vec[ix].props |= GID_TABLE_ENTRY_DEFAULT;
		if (action == GID_TABLE_WRITE_ACTION_DEL)
			table->data_vec[ix].attr.gid_type = old_gid_type;
	}
	if (table->data_vec[ix].attr.ndev &&
	    table->data_vec[ix].attr.ndev != old_net_dev)
		dev_hold(table->data_vec[ix].attr.ndev);

	table->data_vec[ix].props &= ~GID_TABLE_ENTRY_INVALID;

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

/* rwlock should be read locked */
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

		if (data->props & GID_TABLE_ENTRY_INVALID)
			continue;

		if (empty < 0)
			if (!memcmp(&data->gid, &zgid, sizeof(*gid)) &&
			    !memcmp(attr, &zattr, sizeof(*attr)) &&
			    !data->props)
				empty = curr_index;

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

int ib_cache_gid_add(struct ib_device *ib_dev, u8 port,
		     union ib_gid *gid, struct ib_gid_attr *attr)
{
	struct ib_gid_table *table;
	int ix;
	int ret = 0;
	struct net_device *idev;
	int empty;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

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
	write_lock_irq(&table->rwlock);

	ix = find_gid(table, gid, attr, false, GID_ATTR_FIND_MASK_GID |
		      GID_ATTR_FIND_MASK_GID_TYPE |
		      GID_ATTR_FIND_MASK_NETDEV, &empty);
	if (ix >= 0)
		goto out_unlock;

	if (empty < 0) {
		ret = -ENOSPC;
		goto out_unlock;
	}

	ret = add_gid(ib_dev, port, table, empty, gid, attr, false);
	if (!ret)
		dispatch_gid_change_event(ib_dev, port);

out_unlock:
	write_unlock_irq(&table->rwlock);
	mutex_unlock(&table->lock);
	return ret;
}

int ib_cache_gid_del(struct ib_device *ib_dev, u8 port,
		     union ib_gid *gid, struct ib_gid_attr *attr)
{
	struct ib_gid_table *table;
	int ix;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

	mutex_lock(&table->lock);
	write_lock_irq(&table->rwlock);

	ix = find_gid(table, gid, attr, false,
		      GID_ATTR_FIND_MASK_GID	  |
		      GID_ATTR_FIND_MASK_GID_TYPE |
		      GID_ATTR_FIND_MASK_NETDEV	  |
		      GID_ATTR_FIND_MASK_DEFAULT,
		      NULL);
	if (ix < 0)
		goto out_unlock;

	if (!del_gid(ib_dev, port, table, ix, false))
		dispatch_gid_change_event(ib_dev, port);

out_unlock:
	write_unlock_irq(&table->rwlock);
	mutex_unlock(&table->lock);
	return 0;
}

int ib_cache_gid_del_all_netdev_gids(struct ib_device *ib_dev, u8 port,
				     struct net_device *ndev)
{
	struct ib_gid_table *table;
	int ix;
	bool deleted = false;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

	mutex_lock(&table->lock);
	write_lock_irq(&table->rwlock);

	for (ix = 0; ix < table->sz; ix++)
		if (table->data_vec[ix].attr.ndev == ndev)
			if (!del_gid(ib_dev, port, table, ix,
				     !!(table->data_vec[ix].props &
					GID_TABLE_ENTRY_DEFAULT)))
				deleted = true;

	write_unlock_irq(&table->rwlock);
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

	write_lock_irq(&table->rwlock);
	for (i = 0; i < table->sz; ++i) {
		if (memcmp(&table->data_vec[i].gid, &zgid,
			   sizeof(table->data_vec[i].gid)))
			if (!del_gid(ib_dev, port, table, i,
				     table->data_vec[i].props &
				     GID_ATTR_FIND_MASK_DEFAULT))
				deleted = true;
	}
	write_unlock_irq(&table->rwlock);

	if (deleted)
		dispatch_gid_change_event(ib_dev, port);
}

void ib_cache_gid_set_default_gid(struct ib_device *ib_dev, u8 port,
				  struct net_device *ndev,
				  unsigned long gid_type_mask,
				  enum ib_cache_gid_default_mode mode)
{
	union ib_gid gid;
	struct ib_gid_attr gid_attr;
	struct ib_gid_attr zattr_type = zattr;
	struct ib_gid_table *table;
	unsigned int gid_type;

	table = ib_dev->cache.ports[port - rdma_start_port(ib_dev)].gid;

	make_default_gid(ndev, &gid);
	memset(&gid_attr, 0, sizeof(gid_attr));
	gid_attr.ndev = ndev;

	for (gid_type = 0; gid_type < IB_GID_TYPE_SIZE; ++gid_type) {
		int ix;
		union ib_gid current_gid;
		struct ib_gid_attr current_gid_attr = {};

		if (1UL << gid_type & ~gid_type_mask)
			continue;

		gid_attr.gid_type = gid_type;

		mutex_lock(&table->lock);
		write_lock_irq(&table->rwlock);
		ix = find_gid(table, NULL, &gid_attr, true,
			      GID_ATTR_FIND_MASK_GID_TYPE |
			      GID_ATTR_FIND_MASK_DEFAULT,
			      NULL);

		/* Coudn't find default GID location */
		if (WARN_ON(ix < 0))
			goto release;

		zattr_type.gid_type = gid_type;

		if (!__ib_cache_gid_get(ib_dev, port, ix,
					&current_gid, &current_gid_attr) &&
		    mode == IB_CACHE_GID_DEFAULT_MODE_SET &&
		    !memcmp(&gid, &current_gid, sizeof(gid)) &&
		    !memcmp(&gid_attr, &current_gid_attr, sizeof(gid_attr)))
			goto release;

		if (memcmp(&current_gid, &zgid, sizeof(current_gid)) ||
		    memcmp(&current_gid_attr, &zattr_type,
			   sizeof(current_gid_attr))) {
			if (del_gid(ib_dev, port, table, ix, true)) {
				pr_warn("ib_cache_gid: can't delete index %d for default gid %pI6\n",
					ix, gid.raw);
				goto release;
			} else {
				dispatch_gid_change_event(ib_dev, port);
			}
		}

		if (mode == IB_CACHE_GID_DEFAULT_MODE_SET) {
			if (add_gid(ib_dev, port, table, ix, &gid, &gid_attr, true))
				pr_warn("ib_cache_gid: unable to add default gid %pI6\n",
					gid.raw);
			else
				dispatch_gid_change_event(ib_dev, port);
		}

release:
		if (current_gid_attr.ndev)
			dev_put(current_gid_attr.ndev);
		write_unlock_irq(&table->rwlock);
		mutex_unlock(&table->lock);
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

static void ib_cache_update(struct ib_device *device,
			    u8                port,
			    bool	      enforce_security)
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
	bool			   use_roce_gid_table =
					rdma_cap_roce_gid_table(device, port);

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
			pr_warn("ib_query_pkey failed (%d) for %s (index %d)\n",
				ret, device->name, i);
			goto err;
		}
	}

	if (!use_roce_gid_table) {
		for (i = 0;  i < gid_cache->table_len; ++i) {
			ret = ib_query_gid(device, port, i,
					   gid_cache->table + i, NULL);
			if (ret) {
				pr_warn("ib_query_gid failed (%d) for %s (index %d)\n",
					ret, device->name, i);
				goto err;
			}
		}
	}

	write_lock_irq(&device->cache.lock);

	old_pkey_cache = device->cache.ports[port -
		rdma_start_port(device)].pkey;

	device->cache.ports[port - rdma_start_port(device)].pkey = pkey_cache;
	if (!use_roce_gid_table) {
		write_lock(&table->rwlock);
		for (i = 0; i < gid_cache->table_len; i++) {
			modify_gid(device, port, table, i, gid_cache->table + i,
				   &zattr, false);
		}
		write_unlock(&table->rwlock);
	}

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
