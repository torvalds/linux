/*
 * Copyright (c) 2006 Intel Corporation.  All rights reserved.
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

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/random.h>

#include <rdma/ib_cache.h>
#include "sa.h"

static void mcast_add_one(struct ib_device *device);
static void mcast_remove_one(struct ib_device *device, void *client_data);

static struct ib_client mcast_client = {
	.name   = "ib_multicast",
	.add    = mcast_add_one,
	.remove = mcast_remove_one
};

static struct ib_sa_client	sa_client;
static struct workqueue_struct	*mcast_wq;
static union ib_gid mgid0;

struct mcast_device;

struct mcast_port {
	struct mcast_device	*dev;
	spinlock_t		lock;
	struct rb_root		table;
	atomic_t		refcount;
	struct completion	comp;
	u8			port_num;
};

struct mcast_device {
	struct ib_device	*device;
	struct ib_event_handler	event_handler;
	int			start_port;
	int			end_port;
	struct mcast_port	port[0];
};

enum mcast_state {
	MCAST_JOINING,
	MCAST_MEMBER,
	MCAST_ERROR,
};

enum mcast_group_state {
	MCAST_IDLE,
	MCAST_BUSY,
	MCAST_GROUP_ERROR,
	MCAST_PKEY_EVENT
};

enum {
	MCAST_INVALID_PKEY_INDEX = 0xFFFF
};

struct mcast_member;

struct mcast_group {
	struct ib_sa_mcmember_rec rec;
	struct rb_node		node;
	struct mcast_port	*port;
	spinlock_t		lock;
	struct work_struct	work;
	struct list_head	pending_list;
	struct list_head	active_list;
	struct mcast_member	*last_join;
	int			members[NUM_JOIN_MEMBERSHIP_TYPES];
	atomic_t		refcount;
	enum mcast_group_state	state;
	struct ib_sa_query	*query;
	u16			pkey_index;
	u8			leave_state;
	int			retries;
};

struct mcast_member {
	struct ib_sa_multicast	multicast;
	struct ib_sa_client	*client;
	struct mcast_group	*group;
	struct list_head	list;
	enum mcast_state	state;
	atomic_t		refcount;
	struct completion	comp;
};

static void join_handler(int status, struct ib_sa_mcmember_rec *rec,
			 void *context);
static void leave_handler(int status, struct ib_sa_mcmember_rec *rec,
			  void *context);

static struct mcast_group *mcast_find(struct mcast_port *port,
				      union ib_gid *mgid)
{
	struct rb_node *node = port->table.rb_node;
	struct mcast_group *group;
	int ret;

	while (node) {
		group = rb_entry(node, struct mcast_group, node);
		ret = memcmp(mgid->raw, group->rec.mgid.raw, sizeof *mgid);
		if (!ret)
			return group;

		if (ret < 0)
			node = node->rb_left;
		else
			node = node->rb_right;
	}
	return NULL;
}

static struct mcast_group *mcast_insert(struct mcast_port *port,
					struct mcast_group *group,
					int allow_duplicates)
{
	struct rb_node **link = &port->table.rb_node;
	struct rb_node *parent = NULL;
	struct mcast_group *cur_group;
	int ret;

	while (*link) {
		parent = *link;
		cur_group = rb_entry(parent, struct mcast_group, node);

		ret = memcmp(group->rec.mgid.raw, cur_group->rec.mgid.raw,
			     sizeof group->rec.mgid);
		if (ret < 0)
			link = &(*link)->rb_left;
		else if (ret > 0)
			link = &(*link)->rb_right;
		else if (allow_duplicates)
			link = &(*link)->rb_left;
		else
			return cur_group;
	}
	rb_link_node(&group->node, parent, link);
	rb_insert_color(&group->node, &port->table);
	return NULL;
}

static void deref_port(struct mcast_port *port)
{
	if (atomic_dec_and_test(&port->refcount))
		complete(&port->comp);
}

static void release_group(struct mcast_group *group)
{
	struct mcast_port *port = group->port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	if (atomic_dec_and_test(&group->refcount)) {
		rb_erase(&group->node, &port->table);
		spin_unlock_irqrestore(&port->lock, flags);
		kfree(group);
		deref_port(port);
	} else
		spin_unlock_irqrestore(&port->lock, flags);
}

static void deref_member(struct mcast_member *member)
{
	if (atomic_dec_and_test(&member->refcount))
		complete(&member->comp);
}

static void queue_join(struct mcast_member *member)
{
	struct mcast_group *group = member->group;
	unsigned long flags;

	spin_lock_irqsave(&group->lock, flags);
	list_add_tail(&member->list, &group->pending_list);
	if (group->state == MCAST_IDLE) {
		group->state = MCAST_BUSY;
		atomic_inc(&group->refcount);
		queue_work(mcast_wq, &group->work);
	}
	spin_unlock_irqrestore(&group->lock, flags);
}

/*
 * A multicast group has four types of members: full member, non member,
 * sendonly non member and sendonly full member.
 * We need to keep track of the number of members of each
 * type based on their join state.  Adjust the number of members the belong to
 * the specified join states.
 */
static void adjust_membership(struct mcast_group *group, u8 join_state, int inc)
{
	int i;

	for (i = 0; i < NUM_JOIN_MEMBERSHIP_TYPES; i++, join_state >>= 1)
		if (join_state & 0x1)
			group->members[i] += inc;
}

/*
 * If a multicast group has zero members left for a particular join state, but
 * the group is still a member with the SA, we need to leave that join state.
 * Determine which join states we still belong to, but that do not have any
 * active members.
 */
static u8 get_leave_state(struct mcast_group *group)
{
	u8 leave_state = 0;
	int i;

	for (i = 0; i < NUM_JOIN_MEMBERSHIP_TYPES; i++)
		if (!group->members[i])
			leave_state |= (0x1 << i);

	return leave_state & group->rec.join_state;
}

static int check_selector(ib_sa_comp_mask comp_mask,
			  ib_sa_comp_mask selector_mask,
			  ib_sa_comp_mask value_mask,
			  u8 selector, u8 src_value, u8 dst_value)
{
	int err;

	if (!(comp_mask & selector_mask) || !(comp_mask & value_mask))
		return 0;

	switch (selector) {
	case IB_SA_GT:
		err = (src_value <= dst_value);
		break;
	case IB_SA_LT:
		err = (src_value >= dst_value);
		break;
	case IB_SA_EQ:
		err = (src_value != dst_value);
		break;
	default:
		err = 0;
		break;
	}

	return err;
}

static int cmp_rec(struct ib_sa_mcmember_rec *src,
		   struct ib_sa_mcmember_rec *dst, ib_sa_comp_mask comp_mask)
{
	/* MGID must already match */

	if (comp_mask & IB_SA_MCMEMBER_REC_PORT_GID &&
	    memcmp(&src->port_gid, &dst->port_gid, sizeof src->port_gid))
		return -EINVAL;
	if (comp_mask & IB_SA_MCMEMBER_REC_QKEY && src->qkey != dst->qkey)
		return -EINVAL;
	if (comp_mask & IB_SA_MCMEMBER_REC_MLID && src->mlid != dst->mlid)
		return -EINVAL;
	if (check_selector(comp_mask, IB_SA_MCMEMBER_REC_MTU_SELECTOR,
			   IB_SA_MCMEMBER_REC_MTU, dst->mtu_selector,
			   src->mtu, dst->mtu))
		return -EINVAL;
	if (comp_mask & IB_SA_MCMEMBER_REC_TRAFFIC_CLASS &&
	    src->traffic_class != dst->traffic_class)
		return -EINVAL;
	if (comp_mask & IB_SA_MCMEMBER_REC_PKEY && src->pkey != dst->pkey)
		return -EINVAL;
	if (check_selector(comp_mask, IB_SA_MCMEMBER_REC_RATE_SELECTOR,
			   IB_SA_MCMEMBER_REC_RATE, dst->rate_selector,
			   src->rate, dst->rate))
		return -EINVAL;
	if (check_selector(comp_mask,
			   IB_SA_MCMEMBER_REC_PACKET_LIFE_TIME_SELECTOR,
			   IB_SA_MCMEMBER_REC_PACKET_LIFE_TIME,
			   dst->packet_life_time_selector,
			   src->packet_life_time, dst->packet_life_time))
		return -EINVAL;
	if (comp_mask & IB_SA_MCMEMBER_REC_SL && src->sl != dst->sl)
		return -EINVAL;
	if (comp_mask & IB_SA_MCMEMBER_REC_FLOW_LABEL &&
	    src->flow_label != dst->flow_label)
		return -EINVAL;
	if (comp_mask & IB_SA_MCMEMBER_REC_HOP_LIMIT &&
	    src->hop_limit != dst->hop_limit)
		return -EINVAL;
	if (comp_mask & IB_SA_MCMEMBER_REC_SCOPE && src->scope != dst->scope)
		return -EINVAL;

	/* join_state checked separately, proxy_join ignored */

	return 0;
}

static int send_join(struct mcast_group *group, struct mcast_member *member)
{
	struct mcast_port *port = group->port;
	int ret;

	group->last_join = member;
	ret = ib_sa_mcmember_rec_query(&sa_client, port->dev->device,
				       port->port_num, IB_MGMT_METHOD_SET,
				       &member->multicast.rec,
				       member->multicast.comp_mask,
				       3000, GFP_KERNEL, join_handler, group,
				       &group->query);
	return (ret > 0) ? 0 : ret;
}

static int send_leave(struct mcast_group *group, u8 leave_state)
{
	struct mcast_port *port = group->port;
	struct ib_sa_mcmember_rec rec;
	int ret;

	rec = group->rec;
	rec.join_state = leave_state;
	group->leave_state = leave_state;

	ret = ib_sa_mcmember_rec_query(&sa_client, port->dev->device,
				       port->port_num, IB_SA_METHOD_DELETE, &rec,
				       IB_SA_MCMEMBER_REC_MGID     |
				       IB_SA_MCMEMBER_REC_PORT_GID |
				       IB_SA_MCMEMBER_REC_JOIN_STATE,
				       3000, GFP_KERNEL, leave_handler,
				       group, &group->query);
	return (ret > 0) ? 0 : ret;
}

static void join_group(struct mcast_group *group, struct mcast_member *member,
		       u8 join_state)
{
	member->state = MCAST_MEMBER;
	adjust_membership(group, join_state, 1);
	group->rec.join_state |= join_state;
	member->multicast.rec = group->rec;
	member->multicast.rec.join_state = join_state;
	list_move(&member->list, &group->active_list);
}

static int fail_join(struct mcast_group *group, struct mcast_member *member,
		     int status)
{
	spin_lock_irq(&group->lock);
	list_del_init(&member->list);
	spin_unlock_irq(&group->lock);
	return member->multicast.callback(status, &member->multicast);
}

static void process_group_error(struct mcast_group *group)
{
	struct mcast_member *member;
	int ret = 0;
	u16 pkey_index;

	if (group->state == MCAST_PKEY_EVENT)
		ret = ib_find_pkey(group->port->dev->device,
				   group->port->port_num,
				   be16_to_cpu(group->rec.pkey), &pkey_index);

	spin_lock_irq(&group->lock);
	if (group->state == MCAST_PKEY_EVENT && !ret &&
	    group->pkey_index == pkey_index)
		goto out;

	while (!list_empty(&group->active_list)) {
		member = list_entry(group->active_list.next,
				    struct mcast_member, list);
		atomic_inc(&member->refcount);
		list_del_init(&member->list);
		adjust_membership(group, member->multicast.rec.join_state, -1);
		member->state = MCAST_ERROR;
		spin_unlock_irq(&group->lock);

		ret = member->multicast.callback(-ENETRESET,
						 &member->multicast);
		deref_member(member);
		if (ret)
			ib_sa_free_multicast(&member->multicast);
		spin_lock_irq(&group->lock);
	}

	group->rec.join_state = 0;
out:
	group->state = MCAST_BUSY;
	spin_unlock_irq(&group->lock);
}

static void mcast_work_handler(struct work_struct *work)
{
	struct mcast_group *group;
	struct mcast_member *member;
	struct ib_sa_multicast *multicast;
	int status, ret;
	u8 join_state;

	group = container_of(work, typeof(*group), work);
retest:
	spin_lock_irq(&group->lock);
	while (!list_empty(&group->pending_list) ||
	       (group->state != MCAST_BUSY)) {

		if (group->state != MCAST_BUSY) {
			spin_unlock_irq(&group->lock);
			process_group_error(group);
			goto retest;
		}

		member = list_entry(group->pending_list.next,
				    struct mcast_member, list);
		multicast = &member->multicast;
		join_state = multicast->rec.join_state;
		atomic_inc(&member->refcount);

		if (join_state == (group->rec.join_state & join_state)) {
			status = cmp_rec(&group->rec, &multicast->rec,
					 multicast->comp_mask);
			if (!status)
				join_group(group, member, join_state);
			else
				list_del_init(&member->list);
			spin_unlock_irq(&group->lock);
			ret = multicast->callback(status, multicast);
		} else {
			spin_unlock_irq(&group->lock);
			status = send_join(group, member);
			if (!status) {
				deref_member(member);
				return;
			}
			ret = fail_join(group, member, status);
		}

		deref_member(member);
		if (ret)
			ib_sa_free_multicast(&member->multicast);
		spin_lock_irq(&group->lock);
	}

	join_state = get_leave_state(group);
	if (join_state) {
		group->rec.join_state &= ~join_state;
		spin_unlock_irq(&group->lock);
		if (send_leave(group, join_state))
			goto retest;
	} else {
		group->state = MCAST_IDLE;
		spin_unlock_irq(&group->lock);
		release_group(group);
	}
}

/*
 * Fail a join request if it is still active - at the head of the pending queue.
 */
static void process_join_error(struct mcast_group *group, int status)
{
	struct mcast_member *member;
	int ret;

	spin_lock_irq(&group->lock);
	member = list_entry(group->pending_list.next,
			    struct mcast_member, list);
	if (group->last_join == member) {
		atomic_inc(&member->refcount);
		list_del_init(&member->list);
		spin_unlock_irq(&group->lock);
		ret = member->multicast.callback(status, &member->multicast);
		deref_member(member);
		if (ret)
			ib_sa_free_multicast(&member->multicast);
	} else
		spin_unlock_irq(&group->lock);
}

static void join_handler(int status, struct ib_sa_mcmember_rec *rec,
			 void *context)
{
	struct mcast_group *group = context;
	u16 pkey_index = MCAST_INVALID_PKEY_INDEX;

	if (status)
		process_join_error(group, status);
	else {
		int mgids_changed, is_mgid0;
		ib_find_pkey(group->port->dev->device, group->port->port_num,
			     be16_to_cpu(rec->pkey), &pkey_index);

		spin_lock_irq(&group->port->lock);
		if (group->state == MCAST_BUSY &&
		    group->pkey_index == MCAST_INVALID_PKEY_INDEX)
			group->pkey_index = pkey_index;
		mgids_changed = memcmp(&rec->mgid, &group->rec.mgid,
				       sizeof(group->rec.mgid));
		group->rec = *rec;
		if (mgids_changed) {
			rb_erase(&group->node, &group->port->table);
			is_mgid0 = !memcmp(&mgid0, &group->rec.mgid,
					   sizeof(mgid0));
			mcast_insert(group->port, group, is_mgid0);
		}
		spin_unlock_irq(&group->port->lock);
	}
	mcast_work_handler(&group->work);
}

static void leave_handler(int status, struct ib_sa_mcmember_rec *rec,
			  void *context)
{
	struct mcast_group *group = context;

	if (status && group->retries > 0 &&
	    !send_leave(group, group->leave_state))
		group->retries--;
	else
		mcast_work_handler(&group->work);
}

static struct mcast_group *acquire_group(struct mcast_port *port,
					 union ib_gid *mgid, gfp_t gfp_mask)
{
	struct mcast_group *group, *cur_group;
	unsigned long flags;
	int is_mgid0;

	is_mgid0 = !memcmp(&mgid0, mgid, sizeof mgid0);
	if (!is_mgid0) {
		spin_lock_irqsave(&port->lock, flags);
		group = mcast_find(port, mgid);
		if (group)
			goto found;
		spin_unlock_irqrestore(&port->lock, flags);
	}

	group = kzalloc(sizeof *group, gfp_mask);
	if (!group)
		return NULL;

	group->retries = 3;
	group->port = port;
	group->rec.mgid = *mgid;
	group->pkey_index = MCAST_INVALID_PKEY_INDEX;
	INIT_LIST_HEAD(&group->pending_list);
	INIT_LIST_HEAD(&group->active_list);
	INIT_WORK(&group->work, mcast_work_handler);
	spin_lock_init(&group->lock);

	spin_lock_irqsave(&port->lock, flags);
	cur_group = mcast_insert(port, group, is_mgid0);
	if (cur_group) {
		kfree(group);
		group = cur_group;
	} else
		atomic_inc(&port->refcount);
found:
	atomic_inc(&group->refcount);
	spin_unlock_irqrestore(&port->lock, flags);
	return group;
}

/*
 * We serialize all join requests to a single group to make our lives much
 * easier.  Otherwise, two users could try to join the same group
 * simultaneously, with different configurations, one could leave while the
 * join is in progress, etc., which makes locking around error recovery
 * difficult.
 */
struct ib_sa_multicast *
ib_sa_join_multicast(struct ib_sa_client *client,
		     struct ib_device *device, u8 port_num,
		     struct ib_sa_mcmember_rec *rec,
		     ib_sa_comp_mask comp_mask, gfp_t gfp_mask,
		     int (*callback)(int status,
				     struct ib_sa_multicast *multicast),
		     void *context)
{
	struct mcast_device *dev;
	struct mcast_member *member;
	struct ib_sa_multicast *multicast;
	int ret;

	dev = ib_get_client_data(device, &mcast_client);
	if (!dev)
		return ERR_PTR(-ENODEV);

	member = kmalloc(sizeof *member, gfp_mask);
	if (!member)
		return ERR_PTR(-ENOMEM);

	ib_sa_client_get(client);
	member->client = client;
	member->multicast.rec = *rec;
	member->multicast.comp_mask = comp_mask;
	member->multicast.callback = callback;
	member->multicast.context = context;
	init_completion(&member->comp);
	atomic_set(&member->refcount, 1);
	member->state = MCAST_JOINING;

	member->group = acquire_group(&dev->port[port_num - dev->start_port],
				      &rec->mgid, gfp_mask);
	if (!member->group) {
		ret = -ENOMEM;
		goto err;
	}

	/*
	 * The user will get the multicast structure in their callback.  They
	 * could then free the multicast structure before we can return from
	 * this routine.  So we save the pointer to return before queuing
	 * any callback.
	 */
	multicast = &member->multicast;
	queue_join(member);
	return multicast;

err:
	ib_sa_client_put(client);
	kfree(member);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(ib_sa_join_multicast);

void ib_sa_free_multicast(struct ib_sa_multicast *multicast)
{
	struct mcast_member *member;
	struct mcast_group *group;

	member = container_of(multicast, struct mcast_member, multicast);
	group = member->group;

	spin_lock_irq(&group->lock);
	if (member->state == MCAST_MEMBER)
		adjust_membership(group, multicast->rec.join_state, -1);

	list_del_init(&member->list);

	if (group->state == MCAST_IDLE) {
		group->state = MCAST_BUSY;
		spin_unlock_irq(&group->lock);
		/* Continue to hold reference on group until callback */
		queue_work(mcast_wq, &group->work);
	} else {
		spin_unlock_irq(&group->lock);
		release_group(group);
	}

	deref_member(member);
	wait_for_completion(&member->comp);
	ib_sa_client_put(member->client);
	kfree(member);
}
EXPORT_SYMBOL(ib_sa_free_multicast);

int ib_sa_get_mcmember_rec(struct ib_device *device, u8 port_num,
			   union ib_gid *mgid, struct ib_sa_mcmember_rec *rec)
{
	struct mcast_device *dev;
	struct mcast_port *port;
	struct mcast_group *group;
	unsigned long flags;
	int ret = 0;

	dev = ib_get_client_data(device, &mcast_client);
	if (!dev)
		return -ENODEV;

	port = &dev->port[port_num - dev->start_port];
	spin_lock_irqsave(&port->lock, flags);
	group = mcast_find(port, mgid);
	if (group)
		*rec = group->rec;
	else
		ret = -EADDRNOTAVAIL;
	spin_unlock_irqrestore(&port->lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_sa_get_mcmember_rec);

int ib_init_ah_from_mcmember(struct ib_device *device, u8 port_num,
			     struct ib_sa_mcmember_rec *rec,
			     struct net_device *ndev,
			     enum ib_gid_type gid_type,
			     struct ib_ah_attr *ah_attr)
{
	int ret;
	u16 gid_index;
	u8 p;

	if (rdma_protocol_roce(device, port_num)) {
		ret = ib_find_cached_gid_by_port(device, &rec->port_gid,
						 gid_type, port_num,
						 ndev,
						 &gid_index);
	} else if (rdma_protocol_ib(device, port_num)) {
		ret = ib_find_cached_gid(device, &rec->port_gid,
					 IB_GID_TYPE_IB, NULL, &p,
					 &gid_index);
	} else {
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	memset(ah_attr, 0, sizeof *ah_attr);
	ah_attr->dlid = be16_to_cpu(rec->mlid);
	ah_attr->sl = rec->sl;
	ah_attr->port_num = port_num;
	ah_attr->static_rate = rec->rate;

	ah_attr->ah_flags = IB_AH_GRH;
	ah_attr->grh.dgid = rec->mgid;

	ah_attr->grh.sgid_index = (u8) gid_index;
	ah_attr->grh.flow_label = be32_to_cpu(rec->flow_label);
	ah_attr->grh.hop_limit = rec->hop_limit;
	ah_attr->grh.traffic_class = rec->traffic_class;

	return 0;
}
EXPORT_SYMBOL(ib_init_ah_from_mcmember);

static void mcast_groups_event(struct mcast_port *port,
			       enum mcast_group_state state)
{
	struct mcast_group *group;
	struct rb_node *node;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	for (node = rb_first(&port->table); node; node = rb_next(node)) {
		group = rb_entry(node, struct mcast_group, node);
		spin_lock(&group->lock);
		if (group->state == MCAST_IDLE) {
			atomic_inc(&group->refcount);
			queue_work(mcast_wq, &group->work);
		}
		if (group->state != MCAST_GROUP_ERROR)
			group->state = state;
		spin_unlock(&group->lock);
	}
	spin_unlock_irqrestore(&port->lock, flags);
}

static void mcast_event_handler(struct ib_event_handler *handler,
				struct ib_event *event)
{
	struct mcast_device *dev;
	int index;

	dev = container_of(handler, struct mcast_device, event_handler);
	if (!rdma_cap_ib_mcast(dev->device, event->element.port_num))
		return;

	index = event->element.port_num - dev->start_port;

	switch (event->event) {
	case IB_EVENT_PORT_ERR:
	case IB_EVENT_LID_CHANGE:
	case IB_EVENT_SM_CHANGE:
	case IB_EVENT_CLIENT_REREGISTER:
		mcast_groups_event(&dev->port[index], MCAST_GROUP_ERROR);
		break;
	case IB_EVENT_PKEY_CHANGE:
		mcast_groups_event(&dev->port[index], MCAST_PKEY_EVENT);
		break;
	default:
		break;
	}
}

static void mcast_add_one(struct ib_device *device)
{
	struct mcast_device *dev;
	struct mcast_port *port;
	int i;
	int count = 0;

	dev = kmalloc(sizeof *dev + device->phys_port_cnt * sizeof *port,
		      GFP_KERNEL);
	if (!dev)
		return;

	dev->start_port = rdma_start_port(device);
	dev->end_port = rdma_end_port(device);

	for (i = 0; i <= dev->end_port - dev->start_port; i++) {
		if (!rdma_cap_ib_mcast(device, dev->start_port + i))
			continue;
		port = &dev->port[i];
		port->dev = dev;
		port->port_num = dev->start_port + i;
		spin_lock_init(&port->lock);
		port->table = RB_ROOT;
		init_completion(&port->comp);
		atomic_set(&port->refcount, 1);
		++count;
	}

	if (!count) {
		kfree(dev);
		return;
	}

	dev->device = device;
	ib_set_client_data(device, &mcast_client, dev);

	INIT_IB_EVENT_HANDLER(&dev->event_handler, device, mcast_event_handler);
	ib_register_event_handler(&dev->event_handler);
}

static void mcast_remove_one(struct ib_device *device, void *client_data)
{
	struct mcast_device *dev = client_data;
	struct mcast_port *port;
	int i;

	if (!dev)
		return;

	ib_unregister_event_handler(&dev->event_handler);
	flush_workqueue(mcast_wq);

	for (i = 0; i <= dev->end_port - dev->start_port; i++) {
		if (rdma_cap_ib_mcast(device, dev->start_port + i)) {
			port = &dev->port[i];
			deref_port(port);
			wait_for_completion(&port->comp);
		}
	}

	kfree(dev);
}

int mcast_init(void)
{
	int ret;

	mcast_wq = create_singlethread_workqueue("ib_mcast");
	if (!mcast_wq)
		return -ENOMEM;

	ib_sa_register_client(&sa_client);

	ret = ib_register_client(&mcast_client);
	if (ret)
		goto err;
	return 0;

err:
	ib_sa_unregister_client(&sa_client);
	destroy_workqueue(mcast_wq);
	return ret;
}

void mcast_cleanup(void)
{
	ib_unregister_client(&mcast_client);
	ib_sa_unregister_client(&sa_client);
	destroy_workqueue(mcast_wq);
}
