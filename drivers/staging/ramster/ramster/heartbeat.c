/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2004, 2005, 2012 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/configfs.h>

#include "heartbeat.h"
#include "tcp.h"
#include "nodemanager.h"

#include "masklog.h"

/*
 * The first heartbeat pass had one global thread that would serialize all hb
 * callback calls.  This global serializing sem should only be removed once
 * we've made sure that all callees can deal with being called concurrently
 * from multiple hb region threads.
 */
static DECLARE_RWSEM(r2hb_callback_sem);

/*
 * multiple hb threads are watching multiple regions.  A node is live
 * whenever any of the threads sees activity from the node in its region.
 */
static DEFINE_SPINLOCK(r2hb_live_lock);
static unsigned long r2hb_live_node_bitmap[BITS_TO_LONGS(R2NM_MAX_NODES)];

static struct r2hb_callback {
	struct list_head list;
} r2hb_callbacks[R2HB_NUM_CB];

enum r2hb_heartbeat_modes {
	R2HB_HEARTBEAT_LOCAL		= 0,
	R2HB_HEARTBEAT_GLOBAL,
	R2HB_HEARTBEAT_NUM_MODES,
};

char *r2hb_heartbeat_mode_desc[R2HB_HEARTBEAT_NUM_MODES] = {
		"local",	/* R2HB_HEARTBEAT_LOCAL */
		"global",	/* R2HB_HEARTBEAT_GLOBAL */
};

unsigned int r2hb_dead_threshold = R2HB_DEFAULT_DEAD_THRESHOLD;
unsigned int r2hb_heartbeat_mode = R2HB_HEARTBEAT_LOCAL;

/* Only sets a new threshold if there are no active regions.
 *
 * No locking or otherwise interesting code is required for reading
 * r2hb_dead_threshold as it can't change once regions are active and
 * it's not interesting to anyone until then anyway. */
static void r2hb_dead_threshold_set(unsigned int threshold)
{
	if (threshold > R2HB_MIN_DEAD_THRESHOLD) {
		spin_lock(&r2hb_live_lock);
		r2hb_dead_threshold = threshold;
		spin_unlock(&r2hb_live_lock);
	}
}

static int r2hb_global_hearbeat_mode_set(unsigned int hb_mode)
{
	int ret = -1;

	if (hb_mode < R2HB_HEARTBEAT_NUM_MODES) {
		spin_lock(&r2hb_live_lock);
		r2hb_heartbeat_mode = hb_mode;
		ret = 0;
		spin_unlock(&r2hb_live_lock);
	}

	return ret;
}

void r2hb_exit(void)
{
}

int r2hb_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(r2hb_callbacks); i++)
		INIT_LIST_HEAD(&r2hb_callbacks[i].list);

	memset(r2hb_live_node_bitmap, 0, sizeof(r2hb_live_node_bitmap));

	return 0;
}

/* if we're already in a callback then we're already serialized by the sem */
static void r2hb_fill_node_map_from_callback(unsigned long *map,
					     unsigned bytes)
{
	BUG_ON(bytes < (BITS_TO_LONGS(R2NM_MAX_NODES) * sizeof(unsigned long)));

	memcpy(map, &r2hb_live_node_bitmap, bytes);
}

/*
 * get a map of all nodes that are heartbeating in any regions
 */
void r2hb_fill_node_map(unsigned long *map, unsigned bytes)
{
	/* callers want to serialize this map and callbacks so that they
	 * can trust that they don't miss nodes coming to the party */
	down_read(&r2hb_callback_sem);
	spin_lock(&r2hb_live_lock);
	r2hb_fill_node_map_from_callback(map, bytes);
	spin_unlock(&r2hb_live_lock);
	up_read(&r2hb_callback_sem);
}
EXPORT_SYMBOL_GPL(r2hb_fill_node_map);

/*
 * heartbeat configfs bits.  The heartbeat set is a default set under
 * the cluster set in nodemanager.c.
 */

/* heartbeat set */

struct r2hb_hb_group {
	struct config_group hs_group;
	/* some stuff? */
};

static struct r2hb_hb_group *to_r2hb_hb_group(struct config_group *group)
{
	return group ?
		container_of(group, struct r2hb_hb_group, hs_group)
		: NULL;
}

static struct config_item r2hb_config_item;

static struct config_item *r2hb_hb_group_make_item(struct config_group *group,
							  const char *name)
{
	int ret;

	if (strlen(name) > R2HB_MAX_REGION_NAME_LEN) {
		ret = -ENAMETOOLONG;
		goto free;
	}

	config_item_put(&r2hb_config_item);

	return &r2hb_config_item;
free:
	return ERR_PTR(ret);
}

static void r2hb_hb_group_drop_item(struct config_group *group,
					   struct config_item *item)
{
	if (r2hb_global_heartbeat_active()) {
		pr_notice("ramster: Heartbeat %s on region %s (%s)\n",
			"stopped/aborted", config_item_name(item),
			"no region");
	}

	config_item_put(item);
}

struct r2hb_hb_group_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(struct r2hb_hb_group *, char *);
	ssize_t (*store)(struct r2hb_hb_group *, const char *, size_t);
};

static ssize_t r2hb_hb_group_show(struct config_item *item,
					 struct configfs_attribute *attr,
					 char *page)
{
	struct r2hb_hb_group *reg = to_r2hb_hb_group(to_config_group(item));
	struct r2hb_hb_group_attribute *r2hb_hb_group_attr =
		container_of(attr, struct r2hb_hb_group_attribute, attr);
	ssize_t ret = 0;

	if (r2hb_hb_group_attr->show)
		ret = r2hb_hb_group_attr->show(reg, page);
	return ret;
}

static ssize_t r2hb_hb_group_store(struct config_item *item,
					  struct configfs_attribute *attr,
					  const char *page, size_t count)
{
	struct r2hb_hb_group *reg = to_r2hb_hb_group(to_config_group(item));
	struct r2hb_hb_group_attribute *r2hb_hb_group_attr =
		container_of(attr, struct r2hb_hb_group_attribute, attr);
	ssize_t ret = -EINVAL;

	if (r2hb_hb_group_attr->store)
		ret = r2hb_hb_group_attr->store(reg, page, count);
	return ret;
}

static ssize_t r2hb_hb_group_threshold_show(struct r2hb_hb_group *group,
						     char *page)
{
	return sprintf(page, "%u\n", r2hb_dead_threshold);
}

static ssize_t r2hb_hb_group_threshold_store(struct r2hb_hb_group *group,
						    const char *page,
						    size_t count)
{
	unsigned long tmp;
	char *p = (char *)page;
	int err;

	err = kstrtoul(p, 10, &tmp);
	if (err)
		return err;

	/* this will validate ranges for us. */
	r2hb_dead_threshold_set((unsigned int) tmp);

	return count;
}

static
ssize_t r2hb_hb_group_mode_show(struct r2hb_hb_group *group,
				       char *page)
{
	return sprintf(page, "%s\n",
		       r2hb_heartbeat_mode_desc[r2hb_heartbeat_mode]);
}

static
ssize_t r2hb_hb_group_mode_store(struct r2hb_hb_group *group,
					const char *page, size_t count)
{
	unsigned int i;
	int ret;
	size_t len;

	len = (page[count - 1] == '\n') ? count - 1 : count;
	if (!len)
		return -EINVAL;

	for (i = 0; i < R2HB_HEARTBEAT_NUM_MODES; ++i) {
		if (strnicmp(page, r2hb_heartbeat_mode_desc[i], len))
			continue;

		ret = r2hb_global_hearbeat_mode_set(i);
		if (!ret)
			pr_notice("ramster: Heartbeat mode set to %s\n",
			       r2hb_heartbeat_mode_desc[i]);
		return count;
	}

	return -EINVAL;

}

static struct r2hb_hb_group_attribute r2hb_hb_group_attr_threshold = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "dead_threshold",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2hb_hb_group_threshold_show,
	.store	= r2hb_hb_group_threshold_store,
};

static struct r2hb_hb_group_attribute r2hb_hb_group_attr_mode = {
	.attr   = { .ca_owner = THIS_MODULE,
		.ca_name = "mode",
		.ca_mode = S_IRUGO | S_IWUSR },
	.show   = r2hb_hb_group_mode_show,
	.store  = r2hb_hb_group_mode_store,
};

static struct configfs_attribute *r2hb_hb_group_attrs[] = {
	&r2hb_hb_group_attr_threshold.attr,
	&r2hb_hb_group_attr_mode.attr,
	NULL,
};

static struct configfs_item_operations r2hb_hearbeat_group_item_ops = {
	.show_attribute		= r2hb_hb_group_show,
	.store_attribute	= r2hb_hb_group_store,
};

static struct configfs_group_operations r2hb_hb_group_group_ops = {
	.make_item	= r2hb_hb_group_make_item,
	.drop_item	= r2hb_hb_group_drop_item,
};

static struct config_item_type r2hb_hb_group_type = {
	.ct_group_ops	= &r2hb_hb_group_group_ops,
	.ct_item_ops	= &r2hb_hearbeat_group_item_ops,
	.ct_attrs	= r2hb_hb_group_attrs,
	.ct_owner	= THIS_MODULE,
};

/* this is just here to avoid touching group in heartbeat.h which the
 * entire damn world #includes */
struct config_group *r2hb_alloc_hb_set(void)
{
	struct r2hb_hb_group *hs = NULL;
	struct config_group *ret = NULL;

	hs = kzalloc(sizeof(struct r2hb_hb_group), GFP_KERNEL);
	if (hs == NULL)
		goto out;

	config_group_init_type_name(&hs->hs_group, "heartbeat",
				    &r2hb_hb_group_type);

	ret = &hs->hs_group;
out:
	if (ret == NULL)
		kfree(hs);
	return ret;
}

void r2hb_free_hb_set(struct config_group *group)
{
	struct r2hb_hb_group *hs = to_r2hb_hb_group(group);
	kfree(hs);
}

/* hb callback registration and issuing */

static struct r2hb_callback *hbcall_from_type(enum r2hb_callback_type type)
{
	if (type == R2HB_NUM_CB)
		return ERR_PTR(-EINVAL);

	return &r2hb_callbacks[type];
}

void r2hb_setup_callback(struct r2hb_callback_func *hc,
			 enum r2hb_callback_type type,
			 r2hb_cb_func *func,
			 void *data,
			 int priority)
{
	INIT_LIST_HEAD(&hc->hc_item);
	hc->hc_func = func;
	hc->hc_data = data;
	hc->hc_priority = priority;
	hc->hc_type = type;
	hc->hc_magic = R2HB_CB_MAGIC;
}
EXPORT_SYMBOL_GPL(r2hb_setup_callback);

int r2hb_register_callback(const char *region_uuid,
			   struct r2hb_callback_func *hc)
{
	struct r2hb_callback_func *tmp;
	struct list_head *iter;
	struct r2hb_callback *hbcall;
	int ret;

	BUG_ON(hc->hc_magic != R2HB_CB_MAGIC);
	BUG_ON(!list_empty(&hc->hc_item));

	hbcall = hbcall_from_type(hc->hc_type);
	if (IS_ERR(hbcall)) {
		ret = PTR_ERR(hbcall);
		goto out;
	}

	down_write(&r2hb_callback_sem);

	list_for_each(iter, &hbcall->list) {
		tmp = list_entry(iter, struct r2hb_callback_func, hc_item);
		if (hc->hc_priority < tmp->hc_priority) {
			list_add_tail(&hc->hc_item, iter);
			break;
		}
	}
	if (list_empty(&hc->hc_item))
		list_add_tail(&hc->hc_item, &hbcall->list);

	up_write(&r2hb_callback_sem);
	ret = 0;
out:
	mlog(ML_CLUSTER, "returning %d on behalf of %p for funcs %p\n",
	     ret, __builtin_return_address(0), hc);
	return ret;
}
EXPORT_SYMBOL_GPL(r2hb_register_callback);

void r2hb_unregister_callback(const char *region_uuid,
			      struct r2hb_callback_func *hc)
{
	BUG_ON(hc->hc_magic != R2HB_CB_MAGIC);

	mlog(ML_CLUSTER, "on behalf of %p for funcs %p\n",
	     __builtin_return_address(0), hc);

	/* XXX Can this happen _with_ a region reference? */
	if (list_empty(&hc->hc_item))
		return;

	down_write(&r2hb_callback_sem);

	list_del_init(&hc->hc_item);

	up_write(&r2hb_callback_sem);
}
EXPORT_SYMBOL_GPL(r2hb_unregister_callback);

int r2hb_check_node_heartbeating_from_callback(u8 node_num)
{
	unsigned long testing_map[BITS_TO_LONGS(R2NM_MAX_NODES)];

	r2hb_fill_node_map_from_callback(testing_map, sizeof(testing_map));
	if (!test_bit(node_num, testing_map)) {
		mlog(ML_HEARTBEAT,
		     "node (%u) does not have heartbeating enabled.\n",
		     node_num);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(r2hb_check_node_heartbeating_from_callback);

void r2hb_stop_all_regions(void)
{
}
EXPORT_SYMBOL_GPL(r2hb_stop_all_regions);

/*
 * this is just a hack until we get the plumbing which flips file systems
 * read only and drops the hb ref instead of killing the node dead.
 */
int r2hb_global_heartbeat_active(void)
{
	return (r2hb_heartbeat_mode == R2HB_HEARTBEAT_GLOBAL);
}
EXPORT_SYMBOL(r2hb_global_heartbeat_active);

/* added for RAMster */
void r2hb_manual_set_node_heartbeating(int node_num)
{
	if (node_num < R2NM_MAX_NODES)
		set_bit(node_num, r2hb_live_node_bitmap);
}
EXPORT_SYMBOL(r2hb_manual_set_node_heartbeating);
