/*
 * Copyright (c) 2004-2007 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
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
 *
 * $Id: cm.c 4311 2005-12-05 18:42:01Z sean.hefty $
 */

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#include <rdma/ib_cache.h>
#include <rdma/ib_cm.h>
#include "cm_msgs.h"

MODULE_AUTHOR("Sean Hefty");
MODULE_DESCRIPTION("InfiniBand CM");
MODULE_LICENSE("Dual BSD/GPL");

static void cm_add_one(struct ib_device *device);
static void cm_remove_one(struct ib_device *device);

static struct ib_client cm_client = {
	.name   = "cm",
	.add    = cm_add_one,
	.remove = cm_remove_one
};

static struct ib_cm {
	spinlock_t lock;
	struct list_head device_list;
	rwlock_t device_lock;
	struct rb_root listen_service_table;
	u64 listen_service_id;
	/* struct rb_root peer_service_table; todo: fix peer to peer */
	struct rb_root remote_qp_table;
	struct rb_root remote_id_table;
	struct rb_root remote_sidr_table;
	struct idr local_id_table;
	__be32 random_id_operand;
	struct list_head timewait_list;
	struct workqueue_struct *wq;
} cm;

/* Counter indexes ordered by attribute ID */
enum {
	CM_REQ_COUNTER,
	CM_MRA_COUNTER,
	CM_REJ_COUNTER,
	CM_REP_COUNTER,
	CM_RTU_COUNTER,
	CM_DREQ_COUNTER,
	CM_DREP_COUNTER,
	CM_SIDR_REQ_COUNTER,
	CM_SIDR_REP_COUNTER,
	CM_LAP_COUNTER,
	CM_APR_COUNTER,
	CM_ATTR_COUNT,
	CM_ATTR_ID_OFFSET = 0x0010,
};

enum {
	CM_XMIT,
	CM_XMIT_RETRIES,
	CM_RECV,
	CM_RECV_DUPLICATES,
	CM_COUNTER_GROUPS
};

static char const counter_group_names[CM_COUNTER_GROUPS]
				     [sizeof("cm_rx_duplicates")] = {
	"cm_tx_msgs", "cm_tx_retries",
	"cm_rx_msgs", "cm_rx_duplicates"
};

struct cm_counter_group {
	struct kobject obj;
	atomic_long_t counter[CM_ATTR_COUNT];
};

struct cm_counter_attribute {
	struct attribute attr;
	int index;
};

#define CM_COUNTER_ATTR(_name, _index) \
struct cm_counter_attribute cm_##_name##_counter_attr = { \
	.attr = { .name = __stringify(_name), .mode = 0444, .owner = THIS_MODULE }, \
	.index = _index \
}

static CM_COUNTER_ATTR(req, CM_REQ_COUNTER);
static CM_COUNTER_ATTR(mra, CM_MRA_COUNTER);
static CM_COUNTER_ATTR(rej, CM_REJ_COUNTER);
static CM_COUNTER_ATTR(rep, CM_REP_COUNTER);
static CM_COUNTER_ATTR(rtu, CM_RTU_COUNTER);
static CM_COUNTER_ATTR(dreq, CM_DREQ_COUNTER);
static CM_COUNTER_ATTR(drep, CM_DREP_COUNTER);
static CM_COUNTER_ATTR(sidr_req, CM_SIDR_REQ_COUNTER);
static CM_COUNTER_ATTR(sidr_rep, CM_SIDR_REP_COUNTER);
static CM_COUNTER_ATTR(lap, CM_LAP_COUNTER);
static CM_COUNTER_ATTR(apr, CM_APR_COUNTER);

static struct attribute *cm_counter_default_attrs[] = {
	&cm_req_counter_attr.attr,
	&cm_mra_counter_attr.attr,
	&cm_rej_counter_attr.attr,
	&cm_rep_counter_attr.attr,
	&cm_rtu_counter_attr.attr,
	&cm_dreq_counter_attr.attr,
	&cm_drep_counter_attr.attr,
	&cm_sidr_req_counter_attr.attr,
	&cm_sidr_rep_counter_attr.attr,
	&cm_lap_counter_attr.attr,
	&cm_apr_counter_attr.attr,
	NULL
};

struct cm_port {
	struct cm_device *cm_dev;
	struct ib_mad_agent *mad_agent;
	struct kobject port_obj;
	u8 port_num;
	struct cm_counter_group counter_group[CM_COUNTER_GROUPS];
};

struct cm_device {
	struct list_head list;
	struct ib_device *device;
	struct kobject dev_obj;
	u8 ack_delay;
	struct cm_port *port[0];
};

struct cm_av {
	struct cm_port *port;
	union ib_gid dgid;
	struct ib_ah_attr ah_attr;
	u16 pkey_index;
	u8 timeout;
};

struct cm_work {
	struct delayed_work work;
	struct list_head list;
	struct cm_port *port;
	struct ib_mad_recv_wc *mad_recv_wc;	/* Received MADs */
	__be32 local_id;			/* Established / timewait */
	__be32 remote_id;
	struct ib_cm_event cm_event;
	struct ib_sa_path_rec path[0];
};

struct cm_timewait_info {
	struct cm_work work;			/* Must be first. */
	struct list_head list;
	struct rb_node remote_qp_node;
	struct rb_node remote_id_node;
	__be64 remote_ca_guid;
	__be32 remote_qpn;
	u8 inserted_remote_qp;
	u8 inserted_remote_id;
};

struct cm_id_private {
	struct ib_cm_id	id;

	struct rb_node service_node;
	struct rb_node sidr_id_node;
	spinlock_t lock;	/* Do not acquire inside cm.lock */
	struct completion comp;
	atomic_t refcount;

	struct ib_mad_send_buf *msg;
	struct cm_timewait_info *timewait_info;
	/* todo: use alternate port on send failure */
	struct cm_av av;
	struct cm_av alt_av;
	struct ib_cm_compare_data *compare_data;

	void *private_data;
	__be64 tid;
	__be32 local_qpn;
	__be32 remote_qpn;
	enum ib_qp_type qp_type;
	__be32 sq_psn;
	__be32 rq_psn;
	int timeout_ms;
	enum ib_mtu path_mtu;
	__be16 pkey;
	u8 private_data_len;
	u8 max_cm_retries;
	u8 peer_to_peer;
	u8 responder_resources;
	u8 initiator_depth;
	u8 retry_count;
	u8 rnr_retry_count;
	u8 service_timeout;
	u8 target_ack_delay;

	struct list_head work_list;
	atomic_t work_count;
};

static void cm_work_handler(struct work_struct *work);

static inline void cm_deref_id(struct cm_id_private *cm_id_priv)
{
	if (atomic_dec_and_test(&cm_id_priv->refcount))
		complete(&cm_id_priv->comp);
}

static int cm_alloc_msg(struct cm_id_private *cm_id_priv,
			struct ib_mad_send_buf **msg)
{
	struct ib_mad_agent *mad_agent;
	struct ib_mad_send_buf *m;
	struct ib_ah *ah;

	mad_agent = cm_id_priv->av.port->mad_agent;
	ah = ib_create_ah(mad_agent->qp->pd, &cm_id_priv->av.ah_attr);
	if (IS_ERR(ah))
		return PTR_ERR(ah);

	m = ib_create_send_mad(mad_agent, cm_id_priv->id.remote_cm_qpn,
			       cm_id_priv->av.pkey_index,
			       0, IB_MGMT_MAD_HDR, IB_MGMT_MAD_DATA,
			       GFP_ATOMIC);
	if (IS_ERR(m)) {
		ib_destroy_ah(ah);
		return PTR_ERR(m);
	}

	/* Timeout set by caller if response is expected. */
	m->ah = ah;
	m->retries = cm_id_priv->max_cm_retries;

	atomic_inc(&cm_id_priv->refcount);
	m->context[0] = cm_id_priv;
	*msg = m;
	return 0;
}

static int cm_alloc_response_msg(struct cm_port *port,
				 struct ib_mad_recv_wc *mad_recv_wc,
				 struct ib_mad_send_buf **msg)
{
	struct ib_mad_send_buf *m;
	struct ib_ah *ah;

	ah = ib_create_ah_from_wc(port->mad_agent->qp->pd, mad_recv_wc->wc,
				  mad_recv_wc->recv_buf.grh, port->port_num);
	if (IS_ERR(ah))
		return PTR_ERR(ah);

	m = ib_create_send_mad(port->mad_agent, 1, mad_recv_wc->wc->pkey_index,
			       0, IB_MGMT_MAD_HDR, IB_MGMT_MAD_DATA,
			       GFP_ATOMIC);
	if (IS_ERR(m)) {
		ib_destroy_ah(ah);
		return PTR_ERR(m);
	}
	m->ah = ah;
	*msg = m;
	return 0;
}

static void cm_free_msg(struct ib_mad_send_buf *msg)
{
	ib_destroy_ah(msg->ah);
	if (msg->context[0])
		cm_deref_id(msg->context[0]);
	ib_free_send_mad(msg);
}

static void * cm_copy_private_data(const void *private_data,
				   u8 private_data_len)
{
	void *data;

	if (!private_data || !private_data_len)
		return NULL;

	data = kmemdup(private_data, private_data_len, GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	return data;
}

static void cm_set_private_data(struct cm_id_private *cm_id_priv,
				 void *private_data, u8 private_data_len)
{
	if (cm_id_priv->private_data && cm_id_priv->private_data_len)
		kfree(cm_id_priv->private_data);

	cm_id_priv->private_data = private_data;
	cm_id_priv->private_data_len = private_data_len;
}

static void cm_init_av_for_response(struct cm_port *port, struct ib_wc *wc,
				    struct ib_grh *grh, struct cm_av *av)
{
	av->port = port;
	av->pkey_index = wc->pkey_index;
	ib_init_ah_from_wc(port->cm_dev->device, port->port_num, wc,
			   grh, &av->ah_attr);
}

static int cm_init_av_by_path(struct ib_sa_path_rec *path, struct cm_av *av)
{
	struct cm_device *cm_dev;
	struct cm_port *port = NULL;
	unsigned long flags;
	int ret;
	u8 p;

	read_lock_irqsave(&cm.device_lock, flags);
	list_for_each_entry(cm_dev, &cm.device_list, list) {
		if (!ib_find_cached_gid(cm_dev->device, &path->sgid,
					&p, NULL)) {
			port = cm_dev->port[p-1];
			break;
		}
	}
	read_unlock_irqrestore(&cm.device_lock, flags);

	if (!port)
		return -EINVAL;

	ret = ib_find_cached_pkey(cm_dev->device, port->port_num,
				  be16_to_cpu(path->pkey), &av->pkey_index);
	if (ret)
		return ret;

	av->port = port;
	ib_init_ah_from_path(cm_dev->device, port->port_num, path,
			     &av->ah_attr);
	av->timeout = path->packet_life_time + 1;
	return 0;
}

static int cm_alloc_id(struct cm_id_private *cm_id_priv)
{
	unsigned long flags;
	int ret, id;
	static int next_id;

	do {
		spin_lock_irqsave(&cm.lock, flags);
		ret = idr_get_new_above(&cm.local_id_table, cm_id_priv,
					next_id, &id);
		if (!ret)
			next_id = ((unsigned) id + 1) & MAX_ID_MASK;
		spin_unlock_irqrestore(&cm.lock, flags);
	} while( (ret == -EAGAIN) && idr_pre_get(&cm.local_id_table, GFP_KERNEL) );

	cm_id_priv->id.local_id = (__force __be32) (id ^ cm.random_id_operand);
	return ret;
}

static void cm_free_id(__be32 local_id)
{
	spin_lock_irq(&cm.lock);
	idr_remove(&cm.local_id_table,
		   (__force int) (local_id ^ cm.random_id_operand));
	spin_unlock_irq(&cm.lock);
}

static struct cm_id_private * cm_get_id(__be32 local_id, __be32 remote_id)
{
	struct cm_id_private *cm_id_priv;

	cm_id_priv = idr_find(&cm.local_id_table,
			      (__force int) (local_id ^ cm.random_id_operand));
	if (cm_id_priv) {
		if (cm_id_priv->id.remote_id == remote_id)
			atomic_inc(&cm_id_priv->refcount);
		else
			cm_id_priv = NULL;
	}

	return cm_id_priv;
}

static struct cm_id_private * cm_acquire_id(__be32 local_id, __be32 remote_id)
{
	struct cm_id_private *cm_id_priv;

	spin_lock_irq(&cm.lock);
	cm_id_priv = cm_get_id(local_id, remote_id);
	spin_unlock_irq(&cm.lock);

	return cm_id_priv;
}

static void cm_mask_copy(u8 *dst, u8 *src, u8 *mask)
{
	int i;

	for (i = 0; i < IB_CM_COMPARE_SIZE / sizeof(unsigned long); i++)
		((unsigned long *) dst)[i] = ((unsigned long *) src)[i] &
					     ((unsigned long *) mask)[i];
}

static int cm_compare_data(struct ib_cm_compare_data *src_data,
			   struct ib_cm_compare_data *dst_data)
{
	u8 src[IB_CM_COMPARE_SIZE];
	u8 dst[IB_CM_COMPARE_SIZE];

	if (!src_data || !dst_data)
		return 0;

	cm_mask_copy(src, src_data->data, dst_data->mask);
	cm_mask_copy(dst, dst_data->data, src_data->mask);
	return memcmp(src, dst, IB_CM_COMPARE_SIZE);
}

static int cm_compare_private_data(u8 *private_data,
				   struct ib_cm_compare_data *dst_data)
{
	u8 src[IB_CM_COMPARE_SIZE];

	if (!dst_data)
		return 0;

	cm_mask_copy(src, private_data, dst_data->mask);
	return memcmp(src, dst_data->data, IB_CM_COMPARE_SIZE);
}

static struct cm_id_private * cm_insert_listen(struct cm_id_private *cm_id_priv)
{
	struct rb_node **link = &cm.listen_service_table.rb_node;
	struct rb_node *parent = NULL;
	struct cm_id_private *cur_cm_id_priv;
	__be64 service_id = cm_id_priv->id.service_id;
	__be64 service_mask = cm_id_priv->id.service_mask;
	int data_cmp;

	while (*link) {
		parent = *link;
		cur_cm_id_priv = rb_entry(parent, struct cm_id_private,
					  service_node);
		data_cmp = cm_compare_data(cm_id_priv->compare_data,
					   cur_cm_id_priv->compare_data);
		if ((cur_cm_id_priv->id.service_mask & service_id) ==
		    (service_mask & cur_cm_id_priv->id.service_id) &&
		    (cm_id_priv->id.device == cur_cm_id_priv->id.device) &&
		    !data_cmp)
			return cur_cm_id_priv;

		if (cm_id_priv->id.device < cur_cm_id_priv->id.device)
			link = &(*link)->rb_left;
		else if (cm_id_priv->id.device > cur_cm_id_priv->id.device)
			link = &(*link)->rb_right;
		else if (service_id < cur_cm_id_priv->id.service_id)
			link = &(*link)->rb_left;
		else if (service_id > cur_cm_id_priv->id.service_id)
			link = &(*link)->rb_right;
		else if (data_cmp < 0)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}
	rb_link_node(&cm_id_priv->service_node, parent, link);
	rb_insert_color(&cm_id_priv->service_node, &cm.listen_service_table);
	return NULL;
}

static struct cm_id_private * cm_find_listen(struct ib_device *device,
					     __be64 service_id,
					     u8 *private_data)
{
	struct rb_node *node = cm.listen_service_table.rb_node;
	struct cm_id_private *cm_id_priv;
	int data_cmp;

	while (node) {
		cm_id_priv = rb_entry(node, struct cm_id_private, service_node);
		data_cmp = cm_compare_private_data(private_data,
						   cm_id_priv->compare_data);
		if ((cm_id_priv->id.service_mask & service_id) ==
		     cm_id_priv->id.service_id &&
		    (cm_id_priv->id.device == device) && !data_cmp)
			return cm_id_priv;

		if (device < cm_id_priv->id.device)
			node = node->rb_left;
		else if (device > cm_id_priv->id.device)
			node = node->rb_right;
		else if (service_id < cm_id_priv->id.service_id)
			node = node->rb_left;
		else if (service_id > cm_id_priv->id.service_id)
			node = node->rb_right;
		else if (data_cmp < 0)
			node = node->rb_left;
		else
			node = node->rb_right;
	}
	return NULL;
}

static struct cm_timewait_info * cm_insert_remote_id(struct cm_timewait_info
						     *timewait_info)
{
	struct rb_node **link = &cm.remote_id_table.rb_node;
	struct rb_node *parent = NULL;
	struct cm_timewait_info *cur_timewait_info;
	__be64 remote_ca_guid = timewait_info->remote_ca_guid;
	__be32 remote_id = timewait_info->work.remote_id;

	while (*link) {
		parent = *link;
		cur_timewait_info = rb_entry(parent, struct cm_timewait_info,
					     remote_id_node);
		if (remote_id < cur_timewait_info->work.remote_id)
			link = &(*link)->rb_left;
		else if (remote_id > cur_timewait_info->work.remote_id)
			link = &(*link)->rb_right;
		else if (remote_ca_guid < cur_timewait_info->remote_ca_guid)
			link = &(*link)->rb_left;
		else if (remote_ca_guid > cur_timewait_info->remote_ca_guid)
			link = &(*link)->rb_right;
		else
			return cur_timewait_info;
	}
	timewait_info->inserted_remote_id = 1;
	rb_link_node(&timewait_info->remote_id_node, parent, link);
	rb_insert_color(&timewait_info->remote_id_node, &cm.remote_id_table);
	return NULL;
}

static struct cm_timewait_info * cm_find_remote_id(__be64 remote_ca_guid,
						   __be32 remote_id)
{
	struct rb_node *node = cm.remote_id_table.rb_node;
	struct cm_timewait_info *timewait_info;

	while (node) {
		timewait_info = rb_entry(node, struct cm_timewait_info,
					 remote_id_node);
		if (remote_id < timewait_info->work.remote_id)
			node = node->rb_left;
		else if (remote_id > timewait_info->work.remote_id)
			node = node->rb_right;
		else if (remote_ca_guid < timewait_info->remote_ca_guid)
			node = node->rb_left;
		else if (remote_ca_guid > timewait_info->remote_ca_guid)
			node = node->rb_right;
		else
			return timewait_info;
	}
	return NULL;
}

static struct cm_timewait_info * cm_insert_remote_qpn(struct cm_timewait_info
						      *timewait_info)
{
	struct rb_node **link = &cm.remote_qp_table.rb_node;
	struct rb_node *parent = NULL;
	struct cm_timewait_info *cur_timewait_info;
	__be64 remote_ca_guid = timewait_info->remote_ca_guid;
	__be32 remote_qpn = timewait_info->remote_qpn;

	while (*link) {
		parent = *link;
		cur_timewait_info = rb_entry(parent, struct cm_timewait_info,
					     remote_qp_node);
		if (remote_qpn < cur_timewait_info->remote_qpn)
			link = &(*link)->rb_left;
		else if (remote_qpn > cur_timewait_info->remote_qpn)
			link = &(*link)->rb_right;
		else if (remote_ca_guid < cur_timewait_info->remote_ca_guid)
			link = &(*link)->rb_left;
		else if (remote_ca_guid > cur_timewait_info->remote_ca_guid)
			link = &(*link)->rb_right;
		else
			return cur_timewait_info;
	}
	timewait_info->inserted_remote_qp = 1;
	rb_link_node(&timewait_info->remote_qp_node, parent, link);
	rb_insert_color(&timewait_info->remote_qp_node, &cm.remote_qp_table);
	return NULL;
}

static struct cm_id_private * cm_insert_remote_sidr(struct cm_id_private
						    *cm_id_priv)
{
	struct rb_node **link = &cm.remote_sidr_table.rb_node;
	struct rb_node *parent = NULL;
	struct cm_id_private *cur_cm_id_priv;
	union ib_gid *port_gid = &cm_id_priv->av.dgid;
	__be32 remote_id = cm_id_priv->id.remote_id;

	while (*link) {
		parent = *link;
		cur_cm_id_priv = rb_entry(parent, struct cm_id_private,
					  sidr_id_node);
		if (remote_id < cur_cm_id_priv->id.remote_id)
			link = &(*link)->rb_left;
		else if (remote_id > cur_cm_id_priv->id.remote_id)
			link = &(*link)->rb_right;
		else {
			int cmp;
			cmp = memcmp(port_gid, &cur_cm_id_priv->av.dgid,
				     sizeof *port_gid);
			if (cmp < 0)
				link = &(*link)->rb_left;
			else if (cmp > 0)
				link = &(*link)->rb_right;
			else
				return cur_cm_id_priv;
		}
	}
	rb_link_node(&cm_id_priv->sidr_id_node, parent, link);
	rb_insert_color(&cm_id_priv->sidr_id_node, &cm.remote_sidr_table);
	return NULL;
}

static void cm_reject_sidr_req(struct cm_id_private *cm_id_priv,
			       enum ib_cm_sidr_status status)
{
	struct ib_cm_sidr_rep_param param;

	memset(&param, 0, sizeof param);
	param.status = status;
	ib_send_cm_sidr_rep(&cm_id_priv->id, &param);
}

struct ib_cm_id *ib_create_cm_id(struct ib_device *device,
				 ib_cm_handler cm_handler,
				 void *context)
{
	struct cm_id_private *cm_id_priv;
	int ret;

	cm_id_priv = kzalloc(sizeof *cm_id_priv, GFP_KERNEL);
	if (!cm_id_priv)
		return ERR_PTR(-ENOMEM);

	cm_id_priv->id.state = IB_CM_IDLE;
	cm_id_priv->id.device = device;
	cm_id_priv->id.cm_handler = cm_handler;
	cm_id_priv->id.context = context;
	cm_id_priv->id.remote_cm_qpn = 1;
	ret = cm_alloc_id(cm_id_priv);
	if (ret)
		goto error;

	spin_lock_init(&cm_id_priv->lock);
	init_completion(&cm_id_priv->comp);
	INIT_LIST_HEAD(&cm_id_priv->work_list);
	atomic_set(&cm_id_priv->work_count, -1);
	atomic_set(&cm_id_priv->refcount, 1);
	return &cm_id_priv->id;

error:
	kfree(cm_id_priv);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(ib_create_cm_id);

static struct cm_work * cm_dequeue_work(struct cm_id_private *cm_id_priv)
{
	struct cm_work *work;

	if (list_empty(&cm_id_priv->work_list))
		return NULL;

	work = list_entry(cm_id_priv->work_list.next, struct cm_work, list);
	list_del(&work->list);
	return work;
}

static void cm_free_work(struct cm_work *work)
{
	if (work->mad_recv_wc)
		ib_free_recv_mad(work->mad_recv_wc);
	kfree(work);
}

static inline int cm_convert_to_ms(int iba_time)
{
	/* approximate conversion to ms from 4.096us x 2^iba_time */
	return 1 << max(iba_time - 8, 0);
}

/*
 * calculate: 4.096x2^ack_timeout = 4.096x2^ack_delay + 2x4.096x2^life_time
 * Because of how ack_timeout is stored, adding one doubles the timeout.
 * To avoid large timeouts, select the max(ack_delay, life_time + 1), and
 * increment it (round up) only if the other is within 50%.
 */
static u8 cm_ack_timeout(u8 ca_ack_delay, u8 packet_life_time)
{
	int ack_timeout = packet_life_time + 1;

	if (ack_timeout >= ca_ack_delay)
		ack_timeout += (ca_ack_delay >= (ack_timeout - 1));
	else
		ack_timeout = ca_ack_delay +
			      (ack_timeout >= (ca_ack_delay - 1));

	return min(31, ack_timeout);
}

static void cm_cleanup_timewait(struct cm_timewait_info *timewait_info)
{
	if (timewait_info->inserted_remote_id) {
		rb_erase(&timewait_info->remote_id_node, &cm.remote_id_table);
		timewait_info->inserted_remote_id = 0;
	}

	if (timewait_info->inserted_remote_qp) {
		rb_erase(&timewait_info->remote_qp_node, &cm.remote_qp_table);
		timewait_info->inserted_remote_qp = 0;
	}
}

static struct cm_timewait_info * cm_create_timewait_info(__be32 local_id)
{
	struct cm_timewait_info *timewait_info;

	timewait_info = kzalloc(sizeof *timewait_info, GFP_KERNEL);
	if (!timewait_info)
		return ERR_PTR(-ENOMEM);

	timewait_info->work.local_id = local_id;
	INIT_DELAYED_WORK(&timewait_info->work.work, cm_work_handler);
	timewait_info->work.cm_event.event = IB_CM_TIMEWAIT_EXIT;
	return timewait_info;
}

static void cm_enter_timewait(struct cm_id_private *cm_id_priv)
{
	int wait_time;
	unsigned long flags;

	spin_lock_irqsave(&cm.lock, flags);
	cm_cleanup_timewait(cm_id_priv->timewait_info);
	list_add_tail(&cm_id_priv->timewait_info->list, &cm.timewait_list);
	spin_unlock_irqrestore(&cm.lock, flags);

	/*
	 * The cm_id could be destroyed by the user before we exit timewait.
	 * To protect against this, we search for the cm_id after exiting
	 * timewait before notifying the user that we've exited timewait.
	 */
	cm_id_priv->id.state = IB_CM_TIMEWAIT;
	wait_time = cm_convert_to_ms(cm_id_priv->av.timeout);
	queue_delayed_work(cm.wq, &cm_id_priv->timewait_info->work.work,
			   msecs_to_jiffies(wait_time));
	cm_id_priv->timewait_info = NULL;
}

static void cm_reset_to_idle(struct cm_id_private *cm_id_priv)
{
	unsigned long flags;

	cm_id_priv->id.state = IB_CM_IDLE;
	if (cm_id_priv->timewait_info) {
		spin_lock_irqsave(&cm.lock, flags);
		cm_cleanup_timewait(cm_id_priv->timewait_info);
		spin_unlock_irqrestore(&cm.lock, flags);
		kfree(cm_id_priv->timewait_info);
		cm_id_priv->timewait_info = NULL;
	}
}

static void cm_destroy_id(struct ib_cm_id *cm_id, int err)
{
	struct cm_id_private *cm_id_priv;
	struct cm_work *work;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
retest:
	spin_lock_irq(&cm_id_priv->lock);
	switch (cm_id->state) {
	case IB_CM_LISTEN:
		cm_id->state = IB_CM_IDLE;
		spin_unlock_irq(&cm_id_priv->lock);
		spin_lock_irq(&cm.lock);
		rb_erase(&cm_id_priv->service_node, &cm.listen_service_table);
		spin_unlock_irq(&cm.lock);
		break;
	case IB_CM_SIDR_REQ_SENT:
		cm_id->state = IB_CM_IDLE;
		ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
		spin_unlock_irq(&cm_id_priv->lock);
		break;
	case IB_CM_SIDR_REQ_RCVD:
		spin_unlock_irq(&cm_id_priv->lock);
		cm_reject_sidr_req(cm_id_priv, IB_SIDR_REJECT);
		break;
	case IB_CM_REQ_SENT:
		ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
		spin_unlock_irq(&cm_id_priv->lock);
		ib_send_cm_rej(cm_id, IB_CM_REJ_TIMEOUT,
			       &cm_id_priv->id.device->node_guid,
			       sizeof cm_id_priv->id.device->node_guid,
			       NULL, 0);
		break;
	case IB_CM_REQ_RCVD:
		if (err == -ENOMEM) {
			/* Do not reject to allow future retries. */
			cm_reset_to_idle(cm_id_priv);
			spin_unlock_irq(&cm_id_priv->lock);
		} else {
			spin_unlock_irq(&cm_id_priv->lock);
			ib_send_cm_rej(cm_id, IB_CM_REJ_CONSUMER_DEFINED,
				       NULL, 0, NULL, 0);
		}
		break;
	case IB_CM_MRA_REQ_RCVD:
	case IB_CM_REP_SENT:
	case IB_CM_MRA_REP_RCVD:
		ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
		/* Fall through */
	case IB_CM_MRA_REQ_SENT:
	case IB_CM_REP_RCVD:
	case IB_CM_MRA_REP_SENT:
		spin_unlock_irq(&cm_id_priv->lock);
		ib_send_cm_rej(cm_id, IB_CM_REJ_CONSUMER_DEFINED,
			       NULL, 0, NULL, 0);
		break;
	case IB_CM_ESTABLISHED:
		spin_unlock_irq(&cm_id_priv->lock);
		ib_send_cm_dreq(cm_id, NULL, 0);
		goto retest;
	case IB_CM_DREQ_SENT:
		ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
		cm_enter_timewait(cm_id_priv);
		spin_unlock_irq(&cm_id_priv->lock);
		break;
	case IB_CM_DREQ_RCVD:
		spin_unlock_irq(&cm_id_priv->lock);
		ib_send_cm_drep(cm_id, NULL, 0);
		break;
	default:
		spin_unlock_irq(&cm_id_priv->lock);
		break;
	}

	cm_free_id(cm_id->local_id);
	cm_deref_id(cm_id_priv);
	wait_for_completion(&cm_id_priv->comp);
	while ((work = cm_dequeue_work(cm_id_priv)) != NULL)
		cm_free_work(work);
	kfree(cm_id_priv->compare_data);
	kfree(cm_id_priv->private_data);
	kfree(cm_id_priv);
}

void ib_destroy_cm_id(struct ib_cm_id *cm_id)
{
	cm_destroy_id(cm_id, 0);
}
EXPORT_SYMBOL(ib_destroy_cm_id);

int ib_cm_listen(struct ib_cm_id *cm_id, __be64 service_id, __be64 service_mask,
		 struct ib_cm_compare_data *compare_data)
{
	struct cm_id_private *cm_id_priv, *cur_cm_id_priv;
	unsigned long flags;
	int ret = 0;

	service_mask = service_mask ? service_mask :
		       __constant_cpu_to_be64(~0ULL);
	service_id &= service_mask;
	if ((service_id & IB_SERVICE_ID_AGN_MASK) == IB_CM_ASSIGN_SERVICE_ID &&
	    (service_id != IB_CM_ASSIGN_SERVICE_ID))
		return -EINVAL;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	if (cm_id->state != IB_CM_IDLE)
		return -EINVAL;

	if (compare_data) {
		cm_id_priv->compare_data = kzalloc(sizeof *compare_data,
						   GFP_KERNEL);
		if (!cm_id_priv->compare_data)
			return -ENOMEM;
		cm_mask_copy(cm_id_priv->compare_data->data,
			     compare_data->data, compare_data->mask);
		memcpy(cm_id_priv->compare_data->mask, compare_data->mask,
		       IB_CM_COMPARE_SIZE);
	}

	cm_id->state = IB_CM_LISTEN;

	spin_lock_irqsave(&cm.lock, flags);
	if (service_id == IB_CM_ASSIGN_SERVICE_ID) {
		cm_id->service_id = cpu_to_be64(cm.listen_service_id++);
		cm_id->service_mask = __constant_cpu_to_be64(~0ULL);
	} else {
		cm_id->service_id = service_id;
		cm_id->service_mask = service_mask;
	}
	cur_cm_id_priv = cm_insert_listen(cm_id_priv);
	spin_unlock_irqrestore(&cm.lock, flags);

	if (cur_cm_id_priv) {
		cm_id->state = IB_CM_IDLE;
		kfree(cm_id_priv->compare_data);
		cm_id_priv->compare_data = NULL;
		ret = -EBUSY;
	}
	return ret;
}
EXPORT_SYMBOL(ib_cm_listen);

static __be64 cm_form_tid(struct cm_id_private *cm_id_priv,
			  enum cm_msg_sequence msg_seq)
{
	u64 hi_tid, low_tid;

	hi_tid   = ((u64) cm_id_priv->av.port->mad_agent->hi_tid) << 32;
	low_tid  = (u64) ((__force u32)cm_id_priv->id.local_id |
			  (msg_seq << 30));
	return cpu_to_be64(hi_tid | low_tid);
}

static void cm_format_mad_hdr(struct ib_mad_hdr *hdr,
			      __be16 attr_id, __be64 tid)
{
	hdr->base_version  = IB_MGMT_BASE_VERSION;
	hdr->mgmt_class	   = IB_MGMT_CLASS_CM;
	hdr->class_version = IB_CM_CLASS_VERSION;
	hdr->method	   = IB_MGMT_METHOD_SEND;
	hdr->attr_id	   = attr_id;
	hdr->tid	   = tid;
}

static void cm_format_req(struct cm_req_msg *req_msg,
			  struct cm_id_private *cm_id_priv,
			  struct ib_cm_req_param *param)
{
	cm_format_mad_hdr(&req_msg->hdr, CM_REQ_ATTR_ID,
			  cm_form_tid(cm_id_priv, CM_MSG_SEQUENCE_REQ));

	req_msg->local_comm_id = cm_id_priv->id.local_id;
	req_msg->service_id = param->service_id;
	req_msg->local_ca_guid = cm_id_priv->id.device->node_guid;
	cm_req_set_local_qpn(req_msg, cpu_to_be32(param->qp_num));
	cm_req_set_resp_res(req_msg, param->responder_resources);
	cm_req_set_init_depth(req_msg, param->initiator_depth);
	cm_req_set_remote_resp_timeout(req_msg,
				       param->remote_cm_response_timeout);
	cm_req_set_qp_type(req_msg, param->qp_type);
	cm_req_set_flow_ctrl(req_msg, param->flow_control);
	cm_req_set_starting_psn(req_msg, cpu_to_be32(param->starting_psn));
	cm_req_set_local_resp_timeout(req_msg,
				      param->local_cm_response_timeout);
	cm_req_set_retry_count(req_msg, param->retry_count);
	req_msg->pkey = param->primary_path->pkey;
	cm_req_set_path_mtu(req_msg, param->primary_path->mtu);
	cm_req_set_rnr_retry_count(req_msg, param->rnr_retry_count);
	cm_req_set_max_cm_retries(req_msg, param->max_cm_retries);
	cm_req_set_srq(req_msg, param->srq);

	req_msg->primary_local_lid = param->primary_path->slid;
	req_msg->primary_remote_lid = param->primary_path->dlid;
	req_msg->primary_local_gid = param->primary_path->sgid;
	req_msg->primary_remote_gid = param->primary_path->dgid;
	cm_req_set_primary_flow_label(req_msg, param->primary_path->flow_label);
	cm_req_set_primary_packet_rate(req_msg, param->primary_path->rate);
	req_msg->primary_traffic_class = param->primary_path->traffic_class;
	req_msg->primary_hop_limit = param->primary_path->hop_limit;
	cm_req_set_primary_sl(req_msg, param->primary_path->sl);
	cm_req_set_primary_subnet_local(req_msg, 1); /* local only... */
	cm_req_set_primary_local_ack_timeout(req_msg,
		cm_ack_timeout(cm_id_priv->av.port->cm_dev->ack_delay,
			       param->primary_path->packet_life_time));

	if (param->alternate_path) {
		req_msg->alt_local_lid = param->alternate_path->slid;
		req_msg->alt_remote_lid = param->alternate_path->dlid;
		req_msg->alt_local_gid = param->alternate_path->sgid;
		req_msg->alt_remote_gid = param->alternate_path->dgid;
		cm_req_set_alt_flow_label(req_msg,
					  param->alternate_path->flow_label);
		cm_req_set_alt_packet_rate(req_msg, param->alternate_path->rate);
		req_msg->alt_traffic_class = param->alternate_path->traffic_class;
		req_msg->alt_hop_limit = param->alternate_path->hop_limit;
		cm_req_set_alt_sl(req_msg, param->alternate_path->sl);
		cm_req_set_alt_subnet_local(req_msg, 1); /* local only... */
		cm_req_set_alt_local_ack_timeout(req_msg,
			cm_ack_timeout(cm_id_priv->av.port->cm_dev->ack_delay,
				       param->alternate_path->packet_life_time));
	}

	if (param->private_data && param->private_data_len)
		memcpy(req_msg->private_data, param->private_data,
		       param->private_data_len);
}

static int cm_validate_req_param(struct ib_cm_req_param *param)
{
	/* peer-to-peer not supported */
	if (param->peer_to_peer)
		return -EINVAL;

	if (!param->primary_path)
		return -EINVAL;

	if (param->qp_type != IB_QPT_RC && param->qp_type != IB_QPT_UC)
		return -EINVAL;

	if (param->private_data &&
	    param->private_data_len > IB_CM_REQ_PRIVATE_DATA_SIZE)
		return -EINVAL;

	if (param->alternate_path &&
	    (param->alternate_path->pkey != param->primary_path->pkey ||
	     param->alternate_path->mtu != param->primary_path->mtu))
		return -EINVAL;

	return 0;
}

int ib_send_cm_req(struct ib_cm_id *cm_id,
		   struct ib_cm_req_param *param)
{
	struct cm_id_private *cm_id_priv;
	struct cm_req_msg *req_msg;
	unsigned long flags;
	int ret;

	ret = cm_validate_req_param(param);
	if (ret)
		return ret;

	/* Verify that we're not in timewait. */
	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state != IB_CM_IDLE) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		ret = -EINVAL;
		goto out;
	}
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);

	cm_id_priv->timewait_info = cm_create_timewait_info(cm_id_priv->
							    id.local_id);
	if (IS_ERR(cm_id_priv->timewait_info)) {
		ret = PTR_ERR(cm_id_priv->timewait_info);
		goto out;
	}

	ret = cm_init_av_by_path(param->primary_path, &cm_id_priv->av);
	if (ret)
		goto error1;
	if (param->alternate_path) {
		ret = cm_init_av_by_path(param->alternate_path,
					 &cm_id_priv->alt_av);
		if (ret)
			goto error1;
	}
	cm_id->service_id = param->service_id;
	cm_id->service_mask = __constant_cpu_to_be64(~0ULL);
	cm_id_priv->timeout_ms = cm_convert_to_ms(
				    param->primary_path->packet_life_time) * 2 +
				 cm_convert_to_ms(
				    param->remote_cm_response_timeout);
	cm_id_priv->max_cm_retries = param->max_cm_retries;
	cm_id_priv->initiator_depth = param->initiator_depth;
	cm_id_priv->responder_resources = param->responder_resources;
	cm_id_priv->retry_count = param->retry_count;
	cm_id_priv->path_mtu = param->primary_path->mtu;
	cm_id_priv->pkey = param->primary_path->pkey;
	cm_id_priv->qp_type = param->qp_type;

	ret = cm_alloc_msg(cm_id_priv, &cm_id_priv->msg);
	if (ret)
		goto error1;

	req_msg = (struct cm_req_msg *) cm_id_priv->msg->mad;
	cm_format_req(req_msg, cm_id_priv, param);
	cm_id_priv->tid = req_msg->hdr.tid;
	cm_id_priv->msg->timeout_ms = cm_id_priv->timeout_ms;
	cm_id_priv->msg->context[1] = (void *) (unsigned long) IB_CM_REQ_SENT;

	cm_id_priv->local_qpn = cm_req_get_local_qpn(req_msg);
	cm_id_priv->rq_psn = cm_req_get_starting_psn(req_msg);

	spin_lock_irqsave(&cm_id_priv->lock, flags);
	ret = ib_post_send_mad(cm_id_priv->msg, NULL);
	if (ret) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		goto error2;
	}
	BUG_ON(cm_id->state != IB_CM_IDLE);
	cm_id->state = IB_CM_REQ_SENT;
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return 0;

error2:	cm_free_msg(cm_id_priv->msg);
error1:	kfree(cm_id_priv->timewait_info);
out:	return ret;
}
EXPORT_SYMBOL(ib_send_cm_req);

static int cm_issue_rej(struct cm_port *port,
			struct ib_mad_recv_wc *mad_recv_wc,
			enum ib_cm_rej_reason reason,
			enum cm_msg_response msg_rejected,
			void *ari, u8 ari_length)
{
	struct ib_mad_send_buf *msg = NULL;
	struct cm_rej_msg *rej_msg, *rcv_msg;
	int ret;

	ret = cm_alloc_response_msg(port, mad_recv_wc, &msg);
	if (ret)
		return ret;

	/* We just need common CM header information.  Cast to any message. */
	rcv_msg = (struct cm_rej_msg *) mad_recv_wc->recv_buf.mad;
	rej_msg = (struct cm_rej_msg *) msg->mad;

	cm_format_mad_hdr(&rej_msg->hdr, CM_REJ_ATTR_ID, rcv_msg->hdr.tid);
	rej_msg->remote_comm_id = rcv_msg->local_comm_id;
	rej_msg->local_comm_id = rcv_msg->remote_comm_id;
	cm_rej_set_msg_rejected(rej_msg, msg_rejected);
	rej_msg->reason = cpu_to_be16(reason);

	if (ari && ari_length) {
		cm_rej_set_reject_info_len(rej_msg, ari_length);
		memcpy(rej_msg->ari, ari, ari_length);
	}

	ret = ib_post_send_mad(msg, NULL);
	if (ret)
		cm_free_msg(msg);

	return ret;
}

static inline int cm_is_active_peer(__be64 local_ca_guid, __be64 remote_ca_guid,
				    __be32 local_qpn, __be32 remote_qpn)
{
	return (be64_to_cpu(local_ca_guid) > be64_to_cpu(remote_ca_guid) ||
		((local_ca_guid == remote_ca_guid) &&
		 (be32_to_cpu(local_qpn) > be32_to_cpu(remote_qpn))));
}

static void cm_format_paths_from_req(struct cm_req_msg *req_msg,
					    struct ib_sa_path_rec *primary_path,
					    struct ib_sa_path_rec *alt_path)
{
	memset(primary_path, 0, sizeof *primary_path);
	primary_path->dgid = req_msg->primary_local_gid;
	primary_path->sgid = req_msg->primary_remote_gid;
	primary_path->dlid = req_msg->primary_local_lid;
	primary_path->slid = req_msg->primary_remote_lid;
	primary_path->flow_label = cm_req_get_primary_flow_label(req_msg);
	primary_path->hop_limit = req_msg->primary_hop_limit;
	primary_path->traffic_class = req_msg->primary_traffic_class;
	primary_path->reversible = 1;
	primary_path->pkey = req_msg->pkey;
	primary_path->sl = cm_req_get_primary_sl(req_msg);
	primary_path->mtu_selector = IB_SA_EQ;
	primary_path->mtu = cm_req_get_path_mtu(req_msg);
	primary_path->rate_selector = IB_SA_EQ;
	primary_path->rate = cm_req_get_primary_packet_rate(req_msg);
	primary_path->packet_life_time_selector = IB_SA_EQ;
	primary_path->packet_life_time =
		cm_req_get_primary_local_ack_timeout(req_msg);
	primary_path->packet_life_time -= (primary_path->packet_life_time > 0);

	if (req_msg->alt_local_lid) {
		memset(alt_path, 0, sizeof *alt_path);
		alt_path->dgid = req_msg->alt_local_gid;
		alt_path->sgid = req_msg->alt_remote_gid;
		alt_path->dlid = req_msg->alt_local_lid;
		alt_path->slid = req_msg->alt_remote_lid;
		alt_path->flow_label = cm_req_get_alt_flow_label(req_msg);
		alt_path->hop_limit = req_msg->alt_hop_limit;
		alt_path->traffic_class = req_msg->alt_traffic_class;
		alt_path->reversible = 1;
		alt_path->pkey = req_msg->pkey;
		alt_path->sl = cm_req_get_alt_sl(req_msg);
		alt_path->mtu_selector = IB_SA_EQ;
		alt_path->mtu = cm_req_get_path_mtu(req_msg);
		alt_path->rate_selector = IB_SA_EQ;
		alt_path->rate = cm_req_get_alt_packet_rate(req_msg);
		alt_path->packet_life_time_selector = IB_SA_EQ;
		alt_path->packet_life_time =
			cm_req_get_alt_local_ack_timeout(req_msg);
		alt_path->packet_life_time -= (alt_path->packet_life_time > 0);
	}
}

static void cm_format_req_event(struct cm_work *work,
				struct cm_id_private *cm_id_priv,
				struct ib_cm_id *listen_id)
{
	struct cm_req_msg *req_msg;
	struct ib_cm_req_event_param *param;

	req_msg = (struct cm_req_msg *)work->mad_recv_wc->recv_buf.mad;
	param = &work->cm_event.param.req_rcvd;
	param->listen_id = listen_id;
	param->port = cm_id_priv->av.port->port_num;
	param->primary_path = &work->path[0];
	if (req_msg->alt_local_lid)
		param->alternate_path = &work->path[1];
	else
		param->alternate_path = NULL;
	param->remote_ca_guid = req_msg->local_ca_guid;
	param->remote_qkey = be32_to_cpu(req_msg->local_qkey);
	param->remote_qpn = be32_to_cpu(cm_req_get_local_qpn(req_msg));
	param->qp_type = cm_req_get_qp_type(req_msg);
	param->starting_psn = be32_to_cpu(cm_req_get_starting_psn(req_msg));
	param->responder_resources = cm_req_get_init_depth(req_msg);
	param->initiator_depth = cm_req_get_resp_res(req_msg);
	param->local_cm_response_timeout =
					cm_req_get_remote_resp_timeout(req_msg);
	param->flow_control = cm_req_get_flow_ctrl(req_msg);
	param->remote_cm_response_timeout =
					cm_req_get_local_resp_timeout(req_msg);
	param->retry_count = cm_req_get_retry_count(req_msg);
	param->rnr_retry_count = cm_req_get_rnr_retry_count(req_msg);
	param->srq = cm_req_get_srq(req_msg);
	work->cm_event.private_data = &req_msg->private_data;
}

static void cm_process_work(struct cm_id_private *cm_id_priv,
			    struct cm_work *work)
{
	int ret;

	/* We will typically only have the current event to report. */
	ret = cm_id_priv->id.cm_handler(&cm_id_priv->id, &work->cm_event);
	cm_free_work(work);

	while (!ret && !atomic_add_negative(-1, &cm_id_priv->work_count)) {
		spin_lock_irq(&cm_id_priv->lock);
		work = cm_dequeue_work(cm_id_priv);
		spin_unlock_irq(&cm_id_priv->lock);
		BUG_ON(!work);
		ret = cm_id_priv->id.cm_handler(&cm_id_priv->id,
						&work->cm_event);
		cm_free_work(work);
	}
	cm_deref_id(cm_id_priv);
	if (ret)
		cm_destroy_id(&cm_id_priv->id, ret);
}

static void cm_format_mra(struct cm_mra_msg *mra_msg,
			  struct cm_id_private *cm_id_priv,
			  enum cm_msg_response msg_mraed, u8 service_timeout,
			  const void *private_data, u8 private_data_len)
{
	cm_format_mad_hdr(&mra_msg->hdr, CM_MRA_ATTR_ID, cm_id_priv->tid);
	cm_mra_set_msg_mraed(mra_msg, msg_mraed);
	mra_msg->local_comm_id = cm_id_priv->id.local_id;
	mra_msg->remote_comm_id = cm_id_priv->id.remote_id;
	cm_mra_set_service_timeout(mra_msg, service_timeout);

	if (private_data && private_data_len)
		memcpy(mra_msg->private_data, private_data, private_data_len);
}

static void cm_format_rej(struct cm_rej_msg *rej_msg,
			  struct cm_id_private *cm_id_priv,
			  enum ib_cm_rej_reason reason,
			  void *ari,
			  u8 ari_length,
			  const void *private_data,
			  u8 private_data_len)
{
	cm_format_mad_hdr(&rej_msg->hdr, CM_REJ_ATTR_ID, cm_id_priv->tid);
	rej_msg->remote_comm_id = cm_id_priv->id.remote_id;

	switch(cm_id_priv->id.state) {
	case IB_CM_REQ_RCVD:
		rej_msg->local_comm_id = 0;
		cm_rej_set_msg_rejected(rej_msg, CM_MSG_RESPONSE_REQ);
		break;
	case IB_CM_MRA_REQ_SENT:
		rej_msg->local_comm_id = cm_id_priv->id.local_id;
		cm_rej_set_msg_rejected(rej_msg, CM_MSG_RESPONSE_REQ);
		break;
	case IB_CM_REP_RCVD:
	case IB_CM_MRA_REP_SENT:
		rej_msg->local_comm_id = cm_id_priv->id.local_id;
		cm_rej_set_msg_rejected(rej_msg, CM_MSG_RESPONSE_REP);
		break;
	default:
		rej_msg->local_comm_id = cm_id_priv->id.local_id;
		cm_rej_set_msg_rejected(rej_msg, CM_MSG_RESPONSE_OTHER);
		break;
	}

	rej_msg->reason = cpu_to_be16(reason);
	if (ari && ari_length) {
		cm_rej_set_reject_info_len(rej_msg, ari_length);
		memcpy(rej_msg->ari, ari, ari_length);
	}

	if (private_data && private_data_len)
		memcpy(rej_msg->private_data, private_data, private_data_len);
}

static void cm_dup_req_handler(struct cm_work *work,
			       struct cm_id_private *cm_id_priv)
{
	struct ib_mad_send_buf *msg = NULL;
	int ret;

	atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
			counter[CM_REQ_COUNTER]);

	/* Quick state check to discard duplicate REQs. */
	if (cm_id_priv->id.state == IB_CM_REQ_RCVD)
		return;

	ret = cm_alloc_response_msg(work->port, work->mad_recv_wc, &msg);
	if (ret)
		return;

	spin_lock_irq(&cm_id_priv->lock);
	switch (cm_id_priv->id.state) {
	case IB_CM_MRA_REQ_SENT:
		cm_format_mra((struct cm_mra_msg *) msg->mad, cm_id_priv,
			      CM_MSG_RESPONSE_REQ, cm_id_priv->service_timeout,
			      cm_id_priv->private_data,
			      cm_id_priv->private_data_len);
		break;
	case IB_CM_TIMEWAIT:
		cm_format_rej((struct cm_rej_msg *) msg->mad, cm_id_priv,
			      IB_CM_REJ_STALE_CONN, NULL, 0, NULL, 0);
		break;
	default:
		goto unlock;
	}
	spin_unlock_irq(&cm_id_priv->lock);

	ret = ib_post_send_mad(msg, NULL);
	if (ret)
		goto free;
	return;

unlock:	spin_unlock_irq(&cm_id_priv->lock);
free:	cm_free_msg(msg);
}

static struct cm_id_private * cm_match_req(struct cm_work *work,
					   struct cm_id_private *cm_id_priv)
{
	struct cm_id_private *listen_cm_id_priv, *cur_cm_id_priv;
	struct cm_timewait_info *timewait_info;
	struct cm_req_msg *req_msg;

	req_msg = (struct cm_req_msg *)work->mad_recv_wc->recv_buf.mad;

	/* Check for possible duplicate REQ. */
	spin_lock_irq(&cm.lock);
	timewait_info = cm_insert_remote_id(cm_id_priv->timewait_info);
	if (timewait_info) {
		cur_cm_id_priv = cm_get_id(timewait_info->work.local_id,
					   timewait_info->work.remote_id);
		spin_unlock_irq(&cm.lock);
		if (cur_cm_id_priv) {
			cm_dup_req_handler(work, cur_cm_id_priv);
			cm_deref_id(cur_cm_id_priv);
		}
		return NULL;
	}

	/* Check for stale connections. */
	timewait_info = cm_insert_remote_qpn(cm_id_priv->timewait_info);
	if (timewait_info) {
		cm_cleanup_timewait(cm_id_priv->timewait_info);
		spin_unlock_irq(&cm.lock);
		cm_issue_rej(work->port, work->mad_recv_wc,
			     IB_CM_REJ_STALE_CONN, CM_MSG_RESPONSE_REQ,
			     NULL, 0);
		return NULL;
	}

	/* Find matching listen request. */
	listen_cm_id_priv = cm_find_listen(cm_id_priv->id.device,
					   req_msg->service_id,
					   req_msg->private_data);
	if (!listen_cm_id_priv) {
		cm_cleanup_timewait(cm_id_priv->timewait_info);
		spin_unlock_irq(&cm.lock);
		cm_issue_rej(work->port, work->mad_recv_wc,
			     IB_CM_REJ_INVALID_SERVICE_ID, CM_MSG_RESPONSE_REQ,
			     NULL, 0);
		goto out;
	}
	atomic_inc(&listen_cm_id_priv->refcount);
	atomic_inc(&cm_id_priv->refcount);
	cm_id_priv->id.state = IB_CM_REQ_RCVD;
	atomic_inc(&cm_id_priv->work_count);
	spin_unlock_irq(&cm.lock);
out:
	return listen_cm_id_priv;
}

static int cm_req_handler(struct cm_work *work)
{
	struct ib_cm_id *cm_id;
	struct cm_id_private *cm_id_priv, *listen_cm_id_priv;
	struct cm_req_msg *req_msg;
	int ret;

	req_msg = (struct cm_req_msg *)work->mad_recv_wc->recv_buf.mad;

	cm_id = ib_create_cm_id(work->port->cm_dev->device, NULL, NULL);
	if (IS_ERR(cm_id))
		return PTR_ERR(cm_id);

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	cm_id_priv->id.remote_id = req_msg->local_comm_id;
	cm_init_av_for_response(work->port, work->mad_recv_wc->wc,
				work->mad_recv_wc->recv_buf.grh,
				&cm_id_priv->av);
	cm_id_priv->timewait_info = cm_create_timewait_info(cm_id_priv->
							    id.local_id);
	if (IS_ERR(cm_id_priv->timewait_info)) {
		ret = PTR_ERR(cm_id_priv->timewait_info);
		goto destroy;
	}
	cm_id_priv->timewait_info->work.remote_id = req_msg->local_comm_id;
	cm_id_priv->timewait_info->remote_ca_guid = req_msg->local_ca_guid;
	cm_id_priv->timewait_info->remote_qpn = cm_req_get_local_qpn(req_msg);

	listen_cm_id_priv = cm_match_req(work, cm_id_priv);
	if (!listen_cm_id_priv) {
		ret = -EINVAL;
		kfree(cm_id_priv->timewait_info);
		goto destroy;
	}

	cm_id_priv->id.cm_handler = listen_cm_id_priv->id.cm_handler;
	cm_id_priv->id.context = listen_cm_id_priv->id.context;
	cm_id_priv->id.service_id = req_msg->service_id;
	cm_id_priv->id.service_mask = __constant_cpu_to_be64(~0ULL);

	cm_format_paths_from_req(req_msg, &work->path[0], &work->path[1]);
	ret = cm_init_av_by_path(&work->path[0], &cm_id_priv->av);
	if (ret) {
		ib_get_cached_gid(work->port->cm_dev->device,
				  work->port->port_num, 0, &work->path[0].sgid);
		ib_send_cm_rej(cm_id, IB_CM_REJ_INVALID_GID,
			       &work->path[0].sgid, sizeof work->path[0].sgid,
			       NULL, 0);
		goto rejected;
	}
	if (req_msg->alt_local_lid) {
		ret = cm_init_av_by_path(&work->path[1], &cm_id_priv->alt_av);
		if (ret) {
			ib_send_cm_rej(cm_id, IB_CM_REJ_INVALID_ALT_GID,
				       &work->path[0].sgid,
				       sizeof work->path[0].sgid, NULL, 0);
			goto rejected;
		}
	}
	cm_id_priv->tid = req_msg->hdr.tid;
	cm_id_priv->timeout_ms = cm_convert_to_ms(
					cm_req_get_local_resp_timeout(req_msg));
	cm_id_priv->max_cm_retries = cm_req_get_max_cm_retries(req_msg);
	cm_id_priv->remote_qpn = cm_req_get_local_qpn(req_msg);
	cm_id_priv->initiator_depth = cm_req_get_resp_res(req_msg);
	cm_id_priv->responder_resources = cm_req_get_init_depth(req_msg);
	cm_id_priv->path_mtu = cm_req_get_path_mtu(req_msg);
	cm_id_priv->pkey = req_msg->pkey;
	cm_id_priv->sq_psn = cm_req_get_starting_psn(req_msg);
	cm_id_priv->retry_count = cm_req_get_retry_count(req_msg);
	cm_id_priv->rnr_retry_count = cm_req_get_rnr_retry_count(req_msg);
	cm_id_priv->qp_type = cm_req_get_qp_type(req_msg);

	cm_format_req_event(work, cm_id_priv, &listen_cm_id_priv->id);
	cm_process_work(cm_id_priv, work);
	cm_deref_id(listen_cm_id_priv);
	return 0;

rejected:
	atomic_dec(&cm_id_priv->refcount);
	cm_deref_id(listen_cm_id_priv);
destroy:
	ib_destroy_cm_id(cm_id);
	return ret;
}

static void cm_format_rep(struct cm_rep_msg *rep_msg,
			  struct cm_id_private *cm_id_priv,
			  struct ib_cm_rep_param *param)
{
	cm_format_mad_hdr(&rep_msg->hdr, CM_REP_ATTR_ID, cm_id_priv->tid);
	rep_msg->local_comm_id = cm_id_priv->id.local_id;
	rep_msg->remote_comm_id = cm_id_priv->id.remote_id;
	cm_rep_set_local_qpn(rep_msg, cpu_to_be32(param->qp_num));
	cm_rep_set_starting_psn(rep_msg, cpu_to_be32(param->starting_psn));
	rep_msg->resp_resources = param->responder_resources;
	rep_msg->initiator_depth = param->initiator_depth;
	cm_rep_set_target_ack_delay(rep_msg,
				    cm_id_priv->av.port->cm_dev->ack_delay);
	cm_rep_set_failover(rep_msg, param->failover_accepted);
	cm_rep_set_flow_ctrl(rep_msg, param->flow_control);
	cm_rep_set_rnr_retry_count(rep_msg, param->rnr_retry_count);
	cm_rep_set_srq(rep_msg, param->srq);
	rep_msg->local_ca_guid = cm_id_priv->id.device->node_guid;

	if (param->private_data && param->private_data_len)
		memcpy(rep_msg->private_data, param->private_data,
		       param->private_data_len);
}

int ib_send_cm_rep(struct ib_cm_id *cm_id,
		   struct ib_cm_rep_param *param)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	struct cm_rep_msg *rep_msg;
	unsigned long flags;
	int ret;

	if (param->private_data &&
	    param->private_data_len > IB_CM_REP_PRIVATE_DATA_SIZE)
		return -EINVAL;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state != IB_CM_REQ_RCVD &&
	    cm_id->state != IB_CM_MRA_REQ_SENT) {
		ret = -EINVAL;
		goto out;
	}

	ret = cm_alloc_msg(cm_id_priv, &msg);
	if (ret)
		goto out;

	rep_msg = (struct cm_rep_msg *) msg->mad;
	cm_format_rep(rep_msg, cm_id_priv, param);
	msg->timeout_ms = cm_id_priv->timeout_ms;
	msg->context[1] = (void *) (unsigned long) IB_CM_REP_SENT;

	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		cm_free_msg(msg);
		return ret;
	}

	cm_id->state = IB_CM_REP_SENT;
	cm_id_priv->msg = msg;
	cm_id_priv->initiator_depth = param->initiator_depth;
	cm_id_priv->responder_resources = param->responder_resources;
	cm_id_priv->rq_psn = cm_rep_get_starting_psn(rep_msg);
	cm_id_priv->local_qpn = cm_rep_get_local_qpn(rep_msg);

out:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_rep);

static void cm_format_rtu(struct cm_rtu_msg *rtu_msg,
			  struct cm_id_private *cm_id_priv,
			  const void *private_data,
			  u8 private_data_len)
{
	cm_format_mad_hdr(&rtu_msg->hdr, CM_RTU_ATTR_ID, cm_id_priv->tid);
	rtu_msg->local_comm_id = cm_id_priv->id.local_id;
	rtu_msg->remote_comm_id = cm_id_priv->id.remote_id;

	if (private_data && private_data_len)
		memcpy(rtu_msg->private_data, private_data, private_data_len);
}

int ib_send_cm_rtu(struct ib_cm_id *cm_id,
		   const void *private_data,
		   u8 private_data_len)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	unsigned long flags;
	void *data;
	int ret;

	if (private_data && private_data_len > IB_CM_RTU_PRIVATE_DATA_SIZE)
		return -EINVAL;

	data = cm_copy_private_data(private_data, private_data_len);
	if (IS_ERR(data))
		return PTR_ERR(data);

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state != IB_CM_REP_RCVD &&
	    cm_id->state != IB_CM_MRA_REP_SENT) {
		ret = -EINVAL;
		goto error;
	}

	ret = cm_alloc_msg(cm_id_priv, &msg);
	if (ret)
		goto error;

	cm_format_rtu((struct cm_rtu_msg *) msg->mad, cm_id_priv,
		      private_data, private_data_len);

	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		cm_free_msg(msg);
		kfree(data);
		return ret;
	}

	cm_id->state = IB_CM_ESTABLISHED;
	cm_set_private_data(cm_id_priv, data, private_data_len);
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return 0;

error:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	kfree(data);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_rtu);

static void cm_format_rep_event(struct cm_work *work)
{
	struct cm_rep_msg *rep_msg;
	struct ib_cm_rep_event_param *param;

	rep_msg = (struct cm_rep_msg *)work->mad_recv_wc->recv_buf.mad;
	param = &work->cm_event.param.rep_rcvd;
	param->remote_ca_guid = rep_msg->local_ca_guid;
	param->remote_qkey = be32_to_cpu(rep_msg->local_qkey);
	param->remote_qpn = be32_to_cpu(cm_rep_get_local_qpn(rep_msg));
	param->starting_psn = be32_to_cpu(cm_rep_get_starting_psn(rep_msg));
	param->responder_resources = rep_msg->initiator_depth;
	param->initiator_depth = rep_msg->resp_resources;
	param->target_ack_delay = cm_rep_get_target_ack_delay(rep_msg);
	param->failover_accepted = cm_rep_get_failover(rep_msg);
	param->flow_control = cm_rep_get_flow_ctrl(rep_msg);
	param->rnr_retry_count = cm_rep_get_rnr_retry_count(rep_msg);
	param->srq = cm_rep_get_srq(rep_msg);
	work->cm_event.private_data = &rep_msg->private_data;
}

static void cm_dup_rep_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_rep_msg *rep_msg;
	struct ib_mad_send_buf *msg = NULL;
	int ret;

	rep_msg = (struct cm_rep_msg *) work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_id(rep_msg->remote_comm_id,
				   rep_msg->local_comm_id);
	if (!cm_id_priv)
		return;

	atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
			counter[CM_REP_COUNTER]);
	ret = cm_alloc_response_msg(work->port, work->mad_recv_wc, &msg);
	if (ret)
		goto deref;

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->id.state == IB_CM_ESTABLISHED)
		cm_format_rtu((struct cm_rtu_msg *) msg->mad, cm_id_priv,
			      cm_id_priv->private_data,
			      cm_id_priv->private_data_len);
	else if (cm_id_priv->id.state == IB_CM_MRA_REP_SENT)
		cm_format_mra((struct cm_mra_msg *) msg->mad, cm_id_priv,
			      CM_MSG_RESPONSE_REP, cm_id_priv->service_timeout,
			      cm_id_priv->private_data,
			      cm_id_priv->private_data_len);
	else
		goto unlock;
	spin_unlock_irq(&cm_id_priv->lock);

	ret = ib_post_send_mad(msg, NULL);
	if (ret)
		goto free;
	goto deref;

unlock:	spin_unlock_irq(&cm_id_priv->lock);
free:	cm_free_msg(msg);
deref:	cm_deref_id(cm_id_priv);
}

static int cm_rep_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_rep_msg *rep_msg;
	int ret;

	rep_msg = (struct cm_rep_msg *)work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_id(rep_msg->remote_comm_id, 0);
	if (!cm_id_priv) {
		cm_dup_rep_handler(work);
		return -EINVAL;
	}

	cm_format_rep_event(work);

	spin_lock_irq(&cm_id_priv->lock);
	switch (cm_id_priv->id.state) {
	case IB_CM_REQ_SENT:
	case IB_CM_MRA_REQ_RCVD:
		break;
	default:
		spin_unlock_irq(&cm_id_priv->lock);
		ret = -EINVAL;
		goto error;
	}

	cm_id_priv->timewait_info->work.remote_id = rep_msg->local_comm_id;
	cm_id_priv->timewait_info->remote_ca_guid = rep_msg->local_ca_guid;
	cm_id_priv->timewait_info->remote_qpn = cm_rep_get_local_qpn(rep_msg);

	spin_lock(&cm.lock);
	/* Check for duplicate REP. */
	if (cm_insert_remote_id(cm_id_priv->timewait_info)) {
		spin_unlock(&cm.lock);
		spin_unlock_irq(&cm_id_priv->lock);
		ret = -EINVAL;
		goto error;
	}
	/* Check for a stale connection. */
	if (cm_insert_remote_qpn(cm_id_priv->timewait_info)) {
		rb_erase(&cm_id_priv->timewait_info->remote_id_node,
			 &cm.remote_id_table);
		cm_id_priv->timewait_info->inserted_remote_id = 0;
		spin_unlock(&cm.lock);
		spin_unlock_irq(&cm_id_priv->lock);
		cm_issue_rej(work->port, work->mad_recv_wc,
			     IB_CM_REJ_STALE_CONN, CM_MSG_RESPONSE_REP,
			     NULL, 0);
		ret = -EINVAL;
		goto error;
	}
	spin_unlock(&cm.lock);

	cm_id_priv->id.state = IB_CM_REP_RCVD;
	cm_id_priv->id.remote_id = rep_msg->local_comm_id;
	cm_id_priv->remote_qpn = cm_rep_get_local_qpn(rep_msg);
	cm_id_priv->initiator_depth = rep_msg->resp_resources;
	cm_id_priv->responder_resources = rep_msg->initiator_depth;
	cm_id_priv->sq_psn = cm_rep_get_starting_psn(rep_msg);
	cm_id_priv->rnr_retry_count = cm_rep_get_rnr_retry_count(rep_msg);
	cm_id_priv->target_ack_delay = cm_rep_get_target_ack_delay(rep_msg);
	cm_id_priv->av.timeout =
			cm_ack_timeout(cm_id_priv->target_ack_delay,
				       cm_id_priv->av.timeout - 1);
	cm_id_priv->alt_av.timeout =
			cm_ack_timeout(cm_id_priv->target_ack_delay,
				       cm_id_priv->alt_av.timeout - 1);

	/* todo: handle peer_to_peer */

	ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;

error:
	cm_deref_id(cm_id_priv);
	return ret;
}

static int cm_establish_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	int ret;

	/* See comment in cm_establish about lookup. */
	cm_id_priv = cm_acquire_id(work->local_id, work->remote_id);
	if (!cm_id_priv)
		return -EINVAL;

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->id.state != IB_CM_ESTABLISHED) {
		spin_unlock_irq(&cm_id_priv->lock);
		goto out;
	}

	ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;
out:
	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

static int cm_rtu_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_rtu_msg *rtu_msg;
	int ret;

	rtu_msg = (struct cm_rtu_msg *)work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_id(rtu_msg->remote_comm_id,
				   rtu_msg->local_comm_id);
	if (!cm_id_priv)
		return -EINVAL;

	work->cm_event.private_data = &rtu_msg->private_data;

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->id.state != IB_CM_REP_SENT &&
	    cm_id_priv->id.state != IB_CM_MRA_REP_RCVD) {
		spin_unlock_irq(&cm_id_priv->lock);
		atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
				counter[CM_RTU_COUNTER]);
		goto out;
	}
	cm_id_priv->id.state = IB_CM_ESTABLISHED;

	ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;
out:
	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

static void cm_format_dreq(struct cm_dreq_msg *dreq_msg,
			  struct cm_id_private *cm_id_priv,
			  const void *private_data,
			  u8 private_data_len)
{
	cm_format_mad_hdr(&dreq_msg->hdr, CM_DREQ_ATTR_ID,
			  cm_form_tid(cm_id_priv, CM_MSG_SEQUENCE_DREQ));
	dreq_msg->local_comm_id = cm_id_priv->id.local_id;
	dreq_msg->remote_comm_id = cm_id_priv->id.remote_id;
	cm_dreq_set_remote_qpn(dreq_msg, cm_id_priv->remote_qpn);

	if (private_data && private_data_len)
		memcpy(dreq_msg->private_data, private_data, private_data_len);
}

int ib_send_cm_dreq(struct ib_cm_id *cm_id,
		    const void *private_data,
		    u8 private_data_len)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	unsigned long flags;
	int ret;

	if (private_data && private_data_len > IB_CM_DREQ_PRIVATE_DATA_SIZE)
		return -EINVAL;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state != IB_CM_ESTABLISHED) {
		ret = -EINVAL;
		goto out;
	}

	ret = cm_alloc_msg(cm_id_priv, &msg);
	if (ret) {
		cm_enter_timewait(cm_id_priv);
		goto out;
	}

	cm_format_dreq((struct cm_dreq_msg *) msg->mad, cm_id_priv,
		       private_data, private_data_len);
	msg->timeout_ms = cm_id_priv->timeout_ms;
	msg->context[1] = (void *) (unsigned long) IB_CM_DREQ_SENT;

	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		cm_enter_timewait(cm_id_priv);
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		cm_free_msg(msg);
		return ret;
	}

	cm_id->state = IB_CM_DREQ_SENT;
	cm_id_priv->msg = msg;
out:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_dreq);

static void cm_format_drep(struct cm_drep_msg *drep_msg,
			  struct cm_id_private *cm_id_priv,
			  const void *private_data,
			  u8 private_data_len)
{
	cm_format_mad_hdr(&drep_msg->hdr, CM_DREP_ATTR_ID, cm_id_priv->tid);
	drep_msg->local_comm_id = cm_id_priv->id.local_id;
	drep_msg->remote_comm_id = cm_id_priv->id.remote_id;

	if (private_data && private_data_len)
		memcpy(drep_msg->private_data, private_data, private_data_len);
}

int ib_send_cm_drep(struct ib_cm_id *cm_id,
		    const void *private_data,
		    u8 private_data_len)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	unsigned long flags;
	void *data;
	int ret;

	if (private_data && private_data_len > IB_CM_DREP_PRIVATE_DATA_SIZE)
		return -EINVAL;

	data = cm_copy_private_data(private_data, private_data_len);
	if (IS_ERR(data))
		return PTR_ERR(data);

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state != IB_CM_DREQ_RCVD) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		kfree(data);
		return -EINVAL;
	}

	cm_set_private_data(cm_id_priv, data, private_data_len);
	cm_enter_timewait(cm_id_priv);

	ret = cm_alloc_msg(cm_id_priv, &msg);
	if (ret)
		goto out;

	cm_format_drep((struct cm_drep_msg *) msg->mad, cm_id_priv,
		       private_data, private_data_len);

	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		cm_free_msg(msg);
		return ret;
	}

out:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_drep);

static int cm_issue_drep(struct cm_port *port,
			 struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_mad_send_buf *msg = NULL;
	struct cm_dreq_msg *dreq_msg;
	struct cm_drep_msg *drep_msg;
	int ret;

	ret = cm_alloc_response_msg(port, mad_recv_wc, &msg);
	if (ret)
		return ret;

	dreq_msg = (struct cm_dreq_msg *) mad_recv_wc->recv_buf.mad;
	drep_msg = (struct cm_drep_msg *) msg->mad;

	cm_format_mad_hdr(&drep_msg->hdr, CM_DREP_ATTR_ID, dreq_msg->hdr.tid);
	drep_msg->remote_comm_id = dreq_msg->local_comm_id;
	drep_msg->local_comm_id = dreq_msg->remote_comm_id;

	ret = ib_post_send_mad(msg, NULL);
	if (ret)
		cm_free_msg(msg);

	return ret;
}

static int cm_dreq_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_dreq_msg *dreq_msg;
	struct ib_mad_send_buf *msg = NULL;
	int ret;

	dreq_msg = (struct cm_dreq_msg *)work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_id(dreq_msg->remote_comm_id,
				   dreq_msg->local_comm_id);
	if (!cm_id_priv) {
		atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
				counter[CM_DREQ_COUNTER]);
		cm_issue_drep(work->port, work->mad_recv_wc);
		return -EINVAL;
	}

	work->cm_event.private_data = &dreq_msg->private_data;

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->local_qpn != cm_dreq_get_remote_qpn(dreq_msg))
		goto unlock;

	switch (cm_id_priv->id.state) {
	case IB_CM_REP_SENT:
	case IB_CM_DREQ_SENT:
		ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
		break;
	case IB_CM_ESTABLISHED:
	case IB_CM_MRA_REP_RCVD:
		break;
	case IB_CM_TIMEWAIT:
		atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
				counter[CM_DREQ_COUNTER]);
		if (cm_alloc_response_msg(work->port, work->mad_recv_wc, &msg))
			goto unlock;

		cm_format_drep((struct cm_drep_msg *) msg->mad, cm_id_priv,
			       cm_id_priv->private_data,
			       cm_id_priv->private_data_len);
		spin_unlock_irq(&cm_id_priv->lock);

		if (ib_post_send_mad(msg, NULL))
			cm_free_msg(msg);
		goto deref;
	case IB_CM_DREQ_RCVD:
		atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
				counter[CM_DREQ_COUNTER]);
		goto unlock;
	default:
		goto unlock;
	}
	cm_id_priv->id.state = IB_CM_DREQ_RCVD;
	cm_id_priv->tid = dreq_msg->hdr.tid;
	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;

unlock:	spin_unlock_irq(&cm_id_priv->lock);
deref:	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

static int cm_drep_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_drep_msg *drep_msg;
	int ret;

	drep_msg = (struct cm_drep_msg *)work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_id(drep_msg->remote_comm_id,
				   drep_msg->local_comm_id);
	if (!cm_id_priv)
		return -EINVAL;

	work->cm_event.private_data = &drep_msg->private_data;

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->id.state != IB_CM_DREQ_SENT &&
	    cm_id_priv->id.state != IB_CM_DREQ_RCVD) {
		spin_unlock_irq(&cm_id_priv->lock);
		goto out;
	}
	cm_enter_timewait(cm_id_priv);

	ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;
out:
	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

int ib_send_cm_rej(struct ib_cm_id *cm_id,
		   enum ib_cm_rej_reason reason,
		   void *ari,
		   u8 ari_length,
		   const void *private_data,
		   u8 private_data_len)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	unsigned long flags;
	int ret;

	if ((private_data && private_data_len > IB_CM_REJ_PRIVATE_DATA_SIZE) ||
	    (ari && ari_length > IB_CM_REJ_ARI_LENGTH))
		return -EINVAL;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);

	spin_lock_irqsave(&cm_id_priv->lock, flags);
	switch (cm_id->state) {
	case IB_CM_REQ_SENT:
	case IB_CM_MRA_REQ_RCVD:
	case IB_CM_REQ_RCVD:
	case IB_CM_MRA_REQ_SENT:
	case IB_CM_REP_RCVD:
	case IB_CM_MRA_REP_SENT:
		ret = cm_alloc_msg(cm_id_priv, &msg);
		if (!ret)
			cm_format_rej((struct cm_rej_msg *) msg->mad,
				      cm_id_priv, reason, ari, ari_length,
				      private_data, private_data_len);

		cm_reset_to_idle(cm_id_priv);
		break;
	case IB_CM_REP_SENT:
	case IB_CM_MRA_REP_RCVD:
		ret = cm_alloc_msg(cm_id_priv, &msg);
		if (!ret)
			cm_format_rej((struct cm_rej_msg *) msg->mad,
				      cm_id_priv, reason, ari, ari_length,
				      private_data, private_data_len);

		cm_enter_timewait(cm_id_priv);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	if (ret)
		goto out;

	ret = ib_post_send_mad(msg, NULL);
	if (ret)
		cm_free_msg(msg);

out:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_rej);

static void cm_format_rej_event(struct cm_work *work)
{
	struct cm_rej_msg *rej_msg;
	struct ib_cm_rej_event_param *param;

	rej_msg = (struct cm_rej_msg *)work->mad_recv_wc->recv_buf.mad;
	param = &work->cm_event.param.rej_rcvd;
	param->ari = rej_msg->ari;
	param->ari_length = cm_rej_get_reject_info_len(rej_msg);
	param->reason = __be16_to_cpu(rej_msg->reason);
	work->cm_event.private_data = &rej_msg->private_data;
}

static struct cm_id_private * cm_acquire_rejected_id(struct cm_rej_msg *rej_msg)
{
	struct cm_timewait_info *timewait_info;
	struct cm_id_private *cm_id_priv;
	__be32 remote_id;

	remote_id = rej_msg->local_comm_id;

	if (__be16_to_cpu(rej_msg->reason) == IB_CM_REJ_TIMEOUT) {
		spin_lock_irq(&cm.lock);
		timewait_info = cm_find_remote_id( *((__be64 *) rej_msg->ari),
						  remote_id);
		if (!timewait_info) {
			spin_unlock_irq(&cm.lock);
			return NULL;
		}
		cm_id_priv = idr_find(&cm.local_id_table, (__force int)
				      (timewait_info->work.local_id ^
				       cm.random_id_operand));
		if (cm_id_priv) {
			if (cm_id_priv->id.remote_id == remote_id)
				atomic_inc(&cm_id_priv->refcount);
			else
				cm_id_priv = NULL;
		}
		spin_unlock_irq(&cm.lock);
	} else if (cm_rej_get_msg_rejected(rej_msg) == CM_MSG_RESPONSE_REQ)
		cm_id_priv = cm_acquire_id(rej_msg->remote_comm_id, 0);
	else
		cm_id_priv = cm_acquire_id(rej_msg->remote_comm_id, remote_id);

	return cm_id_priv;
}

static int cm_rej_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_rej_msg *rej_msg;
	int ret;

	rej_msg = (struct cm_rej_msg *)work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_rejected_id(rej_msg);
	if (!cm_id_priv)
		return -EINVAL;

	cm_format_rej_event(work);

	spin_lock_irq(&cm_id_priv->lock);
	switch (cm_id_priv->id.state) {
	case IB_CM_REQ_SENT:
	case IB_CM_MRA_REQ_RCVD:
	case IB_CM_REP_SENT:
	case IB_CM_MRA_REP_RCVD:
		ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
		/* fall through */
	case IB_CM_REQ_RCVD:
	case IB_CM_MRA_REQ_SENT:
		if (__be16_to_cpu(rej_msg->reason) == IB_CM_REJ_STALE_CONN)
			cm_enter_timewait(cm_id_priv);
		else
			cm_reset_to_idle(cm_id_priv);
		break;
	case IB_CM_DREQ_SENT:
		ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
		/* fall through */
	case IB_CM_REP_RCVD:
	case IB_CM_MRA_REP_SENT:
	case IB_CM_ESTABLISHED:
		cm_enter_timewait(cm_id_priv);
		break;
	default:
		spin_unlock_irq(&cm_id_priv->lock);
		ret = -EINVAL;
		goto out;
	}

	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;
out:
	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

int ib_send_cm_mra(struct ib_cm_id *cm_id,
		   u8 service_timeout,
		   const void *private_data,
		   u8 private_data_len)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	enum ib_cm_state cm_state;
	enum ib_cm_lap_state lap_state;
	enum cm_msg_response msg_response;
	void *data;
	unsigned long flags;
	int ret;

	if (private_data && private_data_len > IB_CM_MRA_PRIVATE_DATA_SIZE)
		return -EINVAL;

	data = cm_copy_private_data(private_data, private_data_len);
	if (IS_ERR(data))
		return PTR_ERR(data);

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);

	spin_lock_irqsave(&cm_id_priv->lock, flags);
	switch(cm_id_priv->id.state) {
	case IB_CM_REQ_RCVD:
		cm_state = IB_CM_MRA_REQ_SENT;
		lap_state = cm_id->lap_state;
		msg_response = CM_MSG_RESPONSE_REQ;
		break;
	case IB_CM_REP_RCVD:
		cm_state = IB_CM_MRA_REP_SENT;
		lap_state = cm_id->lap_state;
		msg_response = CM_MSG_RESPONSE_REP;
		break;
	case IB_CM_ESTABLISHED:
		cm_state = cm_id->state;
		lap_state = IB_CM_MRA_LAP_SENT;
		msg_response = CM_MSG_RESPONSE_OTHER;
		break;
	default:
		ret = -EINVAL;
		goto error1;
	}

	if (!(service_timeout & IB_CM_MRA_FLAG_DELAY)) {
		ret = cm_alloc_msg(cm_id_priv, &msg);
		if (ret)
			goto error1;

		cm_format_mra((struct cm_mra_msg *) msg->mad, cm_id_priv,
			      msg_response, service_timeout,
			      private_data, private_data_len);
		ret = ib_post_send_mad(msg, NULL);
		if (ret)
			goto error2;
	}

	cm_id->state = cm_state;
	cm_id->lap_state = lap_state;
	cm_id_priv->service_timeout = service_timeout;
	cm_set_private_data(cm_id_priv, data, private_data_len);
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return 0;

error1:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	kfree(data);
	return ret;

error2:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	kfree(data);
	cm_free_msg(msg);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_mra);

static struct cm_id_private * cm_acquire_mraed_id(struct cm_mra_msg *mra_msg)
{
	switch (cm_mra_get_msg_mraed(mra_msg)) {
	case CM_MSG_RESPONSE_REQ:
		return cm_acquire_id(mra_msg->remote_comm_id, 0);
	case CM_MSG_RESPONSE_REP:
	case CM_MSG_RESPONSE_OTHER:
		return cm_acquire_id(mra_msg->remote_comm_id,
				     mra_msg->local_comm_id);
	default:
		return NULL;
	}
}

static int cm_mra_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_mra_msg *mra_msg;
	int timeout, ret;

	mra_msg = (struct cm_mra_msg *)work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_mraed_id(mra_msg);
	if (!cm_id_priv)
		return -EINVAL;

	work->cm_event.private_data = &mra_msg->private_data;
	work->cm_event.param.mra_rcvd.service_timeout =
					cm_mra_get_service_timeout(mra_msg);
	timeout = cm_convert_to_ms(cm_mra_get_service_timeout(mra_msg)) +
		  cm_convert_to_ms(cm_id_priv->av.timeout);

	spin_lock_irq(&cm_id_priv->lock);
	switch (cm_id_priv->id.state) {
	case IB_CM_REQ_SENT:
		if (cm_mra_get_msg_mraed(mra_msg) != CM_MSG_RESPONSE_REQ ||
		    ib_modify_mad(cm_id_priv->av.port->mad_agent,
				  cm_id_priv->msg, timeout))
			goto out;
		cm_id_priv->id.state = IB_CM_MRA_REQ_RCVD;
		break;
	case IB_CM_REP_SENT:
		if (cm_mra_get_msg_mraed(mra_msg) != CM_MSG_RESPONSE_REP ||
		    ib_modify_mad(cm_id_priv->av.port->mad_agent,
				  cm_id_priv->msg, timeout))
			goto out;
		cm_id_priv->id.state = IB_CM_MRA_REP_RCVD;
		break;
	case IB_CM_ESTABLISHED:
		if (cm_mra_get_msg_mraed(mra_msg) != CM_MSG_RESPONSE_OTHER ||
		    cm_id_priv->id.lap_state != IB_CM_LAP_SENT ||
		    ib_modify_mad(cm_id_priv->av.port->mad_agent,
				  cm_id_priv->msg, timeout)) {
			if (cm_id_priv->id.lap_state == IB_CM_MRA_LAP_RCVD)
				atomic_long_inc(&work->port->
						counter_group[CM_RECV_DUPLICATES].
						counter[CM_MRA_COUNTER]);
			goto out;
		}
		cm_id_priv->id.lap_state = IB_CM_MRA_LAP_RCVD;
		break;
	case IB_CM_MRA_REQ_RCVD:
	case IB_CM_MRA_REP_RCVD:
		atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
				counter[CM_MRA_COUNTER]);
		/* fall through */
	default:
		goto out;
	}

	cm_id_priv->msg->context[1] = (void *) (unsigned long)
				      cm_id_priv->id.state;
	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;
out:
	spin_unlock_irq(&cm_id_priv->lock);
	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

static void cm_format_lap(struct cm_lap_msg *lap_msg,
			  struct cm_id_private *cm_id_priv,
			  struct ib_sa_path_rec *alternate_path,
			  const void *private_data,
			  u8 private_data_len)
{
	cm_format_mad_hdr(&lap_msg->hdr, CM_LAP_ATTR_ID,
			  cm_form_tid(cm_id_priv, CM_MSG_SEQUENCE_LAP));
	lap_msg->local_comm_id = cm_id_priv->id.local_id;
	lap_msg->remote_comm_id = cm_id_priv->id.remote_id;
	cm_lap_set_remote_qpn(lap_msg, cm_id_priv->remote_qpn);
	/* todo: need remote CM response timeout */
	cm_lap_set_remote_resp_timeout(lap_msg, 0x1F);
	lap_msg->alt_local_lid = alternate_path->slid;
	lap_msg->alt_remote_lid = alternate_path->dlid;
	lap_msg->alt_local_gid = alternate_path->sgid;
	lap_msg->alt_remote_gid = alternate_path->dgid;
	cm_lap_set_flow_label(lap_msg, alternate_path->flow_label);
	cm_lap_set_traffic_class(lap_msg, alternate_path->traffic_class);
	lap_msg->alt_hop_limit = alternate_path->hop_limit;
	cm_lap_set_packet_rate(lap_msg, alternate_path->rate);
	cm_lap_set_sl(lap_msg, alternate_path->sl);
	cm_lap_set_subnet_local(lap_msg, 1); /* local only... */
	cm_lap_set_local_ack_timeout(lap_msg,
		cm_ack_timeout(cm_id_priv->av.port->cm_dev->ack_delay,
			       alternate_path->packet_life_time));

	if (private_data && private_data_len)
		memcpy(lap_msg->private_data, private_data, private_data_len);
}

int ib_send_cm_lap(struct ib_cm_id *cm_id,
		   struct ib_sa_path_rec *alternate_path,
		   const void *private_data,
		   u8 private_data_len)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	unsigned long flags;
	int ret;

	if (private_data && private_data_len > IB_CM_LAP_PRIVATE_DATA_SIZE)
		return -EINVAL;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state != IB_CM_ESTABLISHED ||
	    (cm_id->lap_state != IB_CM_LAP_UNINIT &&
	     cm_id->lap_state != IB_CM_LAP_IDLE)) {
		ret = -EINVAL;
		goto out;
	}

	ret = cm_init_av_by_path(alternate_path, &cm_id_priv->alt_av);
	if (ret)
		goto out;
	cm_id_priv->alt_av.timeout =
			cm_ack_timeout(cm_id_priv->target_ack_delay,
				       cm_id_priv->alt_av.timeout - 1);

	ret = cm_alloc_msg(cm_id_priv, &msg);
	if (ret)
		goto out;

	cm_format_lap((struct cm_lap_msg *) msg->mad, cm_id_priv,
		      alternate_path, private_data, private_data_len);
	msg->timeout_ms = cm_id_priv->timeout_ms;
	msg->context[1] = (void *) (unsigned long) IB_CM_ESTABLISHED;

	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		cm_free_msg(msg);
		return ret;
	}

	cm_id->lap_state = IB_CM_LAP_SENT;
	cm_id_priv->msg = msg;

out:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_lap);

static void cm_format_path_from_lap(struct cm_id_private *cm_id_priv,
				    struct ib_sa_path_rec *path,
				    struct cm_lap_msg *lap_msg)
{
	memset(path, 0, sizeof *path);
	path->dgid = lap_msg->alt_local_gid;
	path->sgid = lap_msg->alt_remote_gid;
	path->dlid = lap_msg->alt_local_lid;
	path->slid = lap_msg->alt_remote_lid;
	path->flow_label = cm_lap_get_flow_label(lap_msg);
	path->hop_limit = lap_msg->alt_hop_limit;
	path->traffic_class = cm_lap_get_traffic_class(lap_msg);
	path->reversible = 1;
	path->pkey = cm_id_priv->pkey;
	path->sl = cm_lap_get_sl(lap_msg);
	path->mtu_selector = IB_SA_EQ;
	path->mtu = cm_id_priv->path_mtu;
	path->rate_selector = IB_SA_EQ;
	path->rate = cm_lap_get_packet_rate(lap_msg);
	path->packet_life_time_selector = IB_SA_EQ;
	path->packet_life_time = cm_lap_get_local_ack_timeout(lap_msg);
	path->packet_life_time -= (path->packet_life_time > 0);
}

static int cm_lap_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_lap_msg *lap_msg;
	struct ib_cm_lap_event_param *param;
	struct ib_mad_send_buf *msg = NULL;
	int ret;

	/* todo: verify LAP request and send reject APR if invalid. */
	lap_msg = (struct cm_lap_msg *)work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_id(lap_msg->remote_comm_id,
				   lap_msg->local_comm_id);
	if (!cm_id_priv)
		return -EINVAL;

	param = &work->cm_event.param.lap_rcvd;
	param->alternate_path = &work->path[0];
	cm_format_path_from_lap(cm_id_priv, param->alternate_path, lap_msg);
	work->cm_event.private_data = &lap_msg->private_data;

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->id.state != IB_CM_ESTABLISHED)
		goto unlock;

	switch (cm_id_priv->id.lap_state) {
	case IB_CM_LAP_UNINIT:
	case IB_CM_LAP_IDLE:
		break;
	case IB_CM_MRA_LAP_SENT:
		atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
				counter[CM_LAP_COUNTER]);
		if (cm_alloc_response_msg(work->port, work->mad_recv_wc, &msg))
			goto unlock;

		cm_format_mra((struct cm_mra_msg *) msg->mad, cm_id_priv,
			      CM_MSG_RESPONSE_OTHER,
			      cm_id_priv->service_timeout,
			      cm_id_priv->private_data,
			      cm_id_priv->private_data_len);
		spin_unlock_irq(&cm_id_priv->lock);

		if (ib_post_send_mad(msg, NULL))
			cm_free_msg(msg);
		goto deref;
	case IB_CM_LAP_RCVD:
		atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
				counter[CM_LAP_COUNTER]);
		goto unlock;
	default:
		goto unlock;
	}

	cm_id_priv->id.lap_state = IB_CM_LAP_RCVD;
	cm_id_priv->tid = lap_msg->hdr.tid;
	cm_init_av_for_response(work->port, work->mad_recv_wc->wc,
				work->mad_recv_wc->recv_buf.grh,
				&cm_id_priv->av);
	cm_init_av_by_path(param->alternate_path, &cm_id_priv->alt_av);
	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;

unlock:	spin_unlock_irq(&cm_id_priv->lock);
deref:	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

static void cm_format_apr(struct cm_apr_msg *apr_msg,
			  struct cm_id_private *cm_id_priv,
			  enum ib_cm_apr_status status,
			  void *info,
			  u8 info_length,
			  const void *private_data,
			  u8 private_data_len)
{
	cm_format_mad_hdr(&apr_msg->hdr, CM_APR_ATTR_ID, cm_id_priv->tid);
	apr_msg->local_comm_id = cm_id_priv->id.local_id;
	apr_msg->remote_comm_id = cm_id_priv->id.remote_id;
	apr_msg->ap_status = (u8) status;

	if (info && info_length) {
		apr_msg->info_length = info_length;
		memcpy(apr_msg->info, info, info_length);
	}

	if (private_data && private_data_len)
		memcpy(apr_msg->private_data, private_data, private_data_len);
}

int ib_send_cm_apr(struct ib_cm_id *cm_id,
		   enum ib_cm_apr_status status,
		   void *info,
		   u8 info_length,
		   const void *private_data,
		   u8 private_data_len)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	unsigned long flags;
	int ret;

	if ((private_data && private_data_len > IB_CM_APR_PRIVATE_DATA_SIZE) ||
	    (info && info_length > IB_CM_APR_INFO_LENGTH))
		return -EINVAL;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state != IB_CM_ESTABLISHED ||
	    (cm_id->lap_state != IB_CM_LAP_RCVD &&
	     cm_id->lap_state != IB_CM_MRA_LAP_SENT)) {
		ret = -EINVAL;
		goto out;
	}

	ret = cm_alloc_msg(cm_id_priv, &msg);
	if (ret)
		goto out;

	cm_format_apr((struct cm_apr_msg *) msg->mad, cm_id_priv, status,
		      info, info_length, private_data, private_data_len);
	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		cm_free_msg(msg);
		return ret;
	}

	cm_id->lap_state = IB_CM_LAP_IDLE;
out:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_apr);

static int cm_apr_handler(struct cm_work *work)
{
	struct cm_id_private *cm_id_priv;
	struct cm_apr_msg *apr_msg;
	int ret;

	apr_msg = (struct cm_apr_msg *)work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_id(apr_msg->remote_comm_id,
				   apr_msg->local_comm_id);
	if (!cm_id_priv)
		return -EINVAL; /* Unmatched reply. */

	work->cm_event.param.apr_rcvd.ap_status = apr_msg->ap_status;
	work->cm_event.param.apr_rcvd.apr_info = &apr_msg->info;
	work->cm_event.param.apr_rcvd.info_len = apr_msg->info_length;
	work->cm_event.private_data = &apr_msg->private_data;

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->id.state != IB_CM_ESTABLISHED ||
	    (cm_id_priv->id.lap_state != IB_CM_LAP_SENT &&
	     cm_id_priv->id.lap_state != IB_CM_MRA_LAP_RCVD)) {
		spin_unlock_irq(&cm_id_priv->lock);
		goto out;
	}
	cm_id_priv->id.lap_state = IB_CM_LAP_IDLE;
	ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
	cm_id_priv->msg = NULL;

	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;
out:
	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

static int cm_timewait_handler(struct cm_work *work)
{
	struct cm_timewait_info *timewait_info;
	struct cm_id_private *cm_id_priv;
	int ret;

	timewait_info = (struct cm_timewait_info *)work;
	spin_lock_irq(&cm.lock);
	list_del(&timewait_info->list);
	spin_unlock_irq(&cm.lock);

	cm_id_priv = cm_acquire_id(timewait_info->work.local_id,
				   timewait_info->work.remote_id);
	if (!cm_id_priv)
		return -EINVAL;

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->id.state != IB_CM_TIMEWAIT ||
	    cm_id_priv->remote_qpn != timewait_info->remote_qpn) {
		spin_unlock_irq(&cm_id_priv->lock);
		goto out;
	}
	cm_id_priv->id.state = IB_CM_IDLE;
	ret = atomic_inc_and_test(&cm_id_priv->work_count);
	if (!ret)
		list_add_tail(&work->list, &cm_id_priv->work_list);
	spin_unlock_irq(&cm_id_priv->lock);

	if (ret)
		cm_process_work(cm_id_priv, work);
	else
		cm_deref_id(cm_id_priv);
	return 0;
out:
	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

static void cm_format_sidr_req(struct cm_sidr_req_msg *sidr_req_msg,
			       struct cm_id_private *cm_id_priv,
			       struct ib_cm_sidr_req_param *param)
{
	cm_format_mad_hdr(&sidr_req_msg->hdr, CM_SIDR_REQ_ATTR_ID,
			  cm_form_tid(cm_id_priv, CM_MSG_SEQUENCE_SIDR));
	sidr_req_msg->request_id = cm_id_priv->id.local_id;
	sidr_req_msg->pkey = cpu_to_be16(param->path->pkey);
	sidr_req_msg->service_id = param->service_id;

	if (param->private_data && param->private_data_len)
		memcpy(sidr_req_msg->private_data, param->private_data,
		       param->private_data_len);
}

int ib_send_cm_sidr_req(struct ib_cm_id *cm_id,
			struct ib_cm_sidr_req_param *param)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	unsigned long flags;
	int ret;

	if (!param->path || (param->private_data &&
	     param->private_data_len > IB_CM_SIDR_REQ_PRIVATE_DATA_SIZE))
		return -EINVAL;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	ret = cm_init_av_by_path(param->path, &cm_id_priv->av);
	if (ret)
		goto out;

	cm_id->service_id = param->service_id;
	cm_id->service_mask = __constant_cpu_to_be64(~0ULL);
	cm_id_priv->timeout_ms = param->timeout_ms;
	cm_id_priv->max_cm_retries = param->max_cm_retries;
	ret = cm_alloc_msg(cm_id_priv, &msg);
	if (ret)
		goto out;

	cm_format_sidr_req((struct cm_sidr_req_msg *) msg->mad, cm_id_priv,
			   param);
	msg->timeout_ms = cm_id_priv->timeout_ms;
	msg->context[1] = (void *) (unsigned long) IB_CM_SIDR_REQ_SENT;

	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state == IB_CM_IDLE)
		ret = ib_post_send_mad(msg, NULL);
	else
		ret = -EINVAL;

	if (ret) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		cm_free_msg(msg);
		goto out;
	}
	cm_id->state = IB_CM_SIDR_REQ_SENT;
	cm_id_priv->msg = msg;
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
out:
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_sidr_req);

static void cm_format_sidr_req_event(struct cm_work *work,
				     struct ib_cm_id *listen_id)
{
	struct cm_sidr_req_msg *sidr_req_msg;
	struct ib_cm_sidr_req_event_param *param;

	sidr_req_msg = (struct cm_sidr_req_msg *)
				work->mad_recv_wc->recv_buf.mad;
	param = &work->cm_event.param.sidr_req_rcvd;
	param->pkey = __be16_to_cpu(sidr_req_msg->pkey);
	param->listen_id = listen_id;
	param->port = work->port->port_num;
	work->cm_event.private_data = &sidr_req_msg->private_data;
}

static int cm_sidr_req_handler(struct cm_work *work)
{
	struct ib_cm_id *cm_id;
	struct cm_id_private *cm_id_priv, *cur_cm_id_priv;
	struct cm_sidr_req_msg *sidr_req_msg;
	struct ib_wc *wc;

	cm_id = ib_create_cm_id(work->port->cm_dev->device, NULL, NULL);
	if (IS_ERR(cm_id))
		return PTR_ERR(cm_id);
	cm_id_priv = container_of(cm_id, struct cm_id_private, id);

	/* Record SGID/SLID and request ID for lookup. */
	sidr_req_msg = (struct cm_sidr_req_msg *)
				work->mad_recv_wc->recv_buf.mad;
	wc = work->mad_recv_wc->wc;
	cm_id_priv->av.dgid.global.subnet_prefix = cpu_to_be64(wc->slid);
	cm_id_priv->av.dgid.global.interface_id = 0;
	cm_init_av_for_response(work->port, work->mad_recv_wc->wc,
				work->mad_recv_wc->recv_buf.grh,
				&cm_id_priv->av);
	cm_id_priv->id.remote_id = sidr_req_msg->request_id;
	cm_id_priv->tid = sidr_req_msg->hdr.tid;
	atomic_inc(&cm_id_priv->work_count);

	spin_lock_irq(&cm.lock);
	cur_cm_id_priv = cm_insert_remote_sidr(cm_id_priv);
	if (cur_cm_id_priv) {
		spin_unlock_irq(&cm.lock);
		atomic_long_inc(&work->port->counter_group[CM_RECV_DUPLICATES].
				counter[CM_SIDR_REQ_COUNTER]);
		goto out; /* Duplicate message. */
	}
	cm_id_priv->id.state = IB_CM_SIDR_REQ_RCVD;
	cur_cm_id_priv = cm_find_listen(cm_id->device,
					sidr_req_msg->service_id,
					sidr_req_msg->private_data);
	if (!cur_cm_id_priv) {
		spin_unlock_irq(&cm.lock);
		cm_reject_sidr_req(cm_id_priv, IB_SIDR_UNSUPPORTED);
		goto out; /* No match. */
	}
	atomic_inc(&cur_cm_id_priv->refcount);
	spin_unlock_irq(&cm.lock);

	cm_id_priv->id.cm_handler = cur_cm_id_priv->id.cm_handler;
	cm_id_priv->id.context = cur_cm_id_priv->id.context;
	cm_id_priv->id.service_id = sidr_req_msg->service_id;
	cm_id_priv->id.service_mask = __constant_cpu_to_be64(~0ULL);

	cm_format_sidr_req_event(work, &cur_cm_id_priv->id);
	cm_process_work(cm_id_priv, work);
	cm_deref_id(cur_cm_id_priv);
	return 0;
out:
	ib_destroy_cm_id(&cm_id_priv->id);
	return -EINVAL;
}

static void cm_format_sidr_rep(struct cm_sidr_rep_msg *sidr_rep_msg,
			       struct cm_id_private *cm_id_priv,
			       struct ib_cm_sidr_rep_param *param)
{
	cm_format_mad_hdr(&sidr_rep_msg->hdr, CM_SIDR_REP_ATTR_ID,
			  cm_id_priv->tid);
	sidr_rep_msg->request_id = cm_id_priv->id.remote_id;
	sidr_rep_msg->status = param->status;
	cm_sidr_rep_set_qpn(sidr_rep_msg, cpu_to_be32(param->qp_num));
	sidr_rep_msg->service_id = cm_id_priv->id.service_id;
	sidr_rep_msg->qkey = cpu_to_be32(param->qkey);

	if (param->info && param->info_length)
		memcpy(sidr_rep_msg->info, param->info, param->info_length);

	if (param->private_data && param->private_data_len)
		memcpy(sidr_rep_msg->private_data, param->private_data,
		       param->private_data_len);
}

int ib_send_cm_sidr_rep(struct ib_cm_id *cm_id,
			struct ib_cm_sidr_rep_param *param)
{
	struct cm_id_private *cm_id_priv;
	struct ib_mad_send_buf *msg;
	unsigned long flags;
	int ret;

	if ((param->info && param->info_length > IB_CM_SIDR_REP_INFO_LENGTH) ||
	    (param->private_data &&
	     param->private_data_len > IB_CM_SIDR_REP_PRIVATE_DATA_SIZE))
		return -EINVAL;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state != IB_CM_SIDR_REQ_RCVD) {
		ret = -EINVAL;
		goto error;
	}

	ret = cm_alloc_msg(cm_id_priv, &msg);
	if (ret)
		goto error;

	cm_format_sidr_rep((struct cm_sidr_rep_msg *) msg->mad, cm_id_priv,
			   param);
	ret = ib_post_send_mad(msg, NULL);
	if (ret) {
		spin_unlock_irqrestore(&cm_id_priv->lock, flags);
		cm_free_msg(msg);
		return ret;
	}
	cm_id->state = IB_CM_IDLE;
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);

	spin_lock_irqsave(&cm.lock, flags);
	rb_erase(&cm_id_priv->sidr_id_node, &cm.remote_sidr_table);
	spin_unlock_irqrestore(&cm.lock, flags);
	return 0;

error:	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_send_cm_sidr_rep);

static void cm_format_sidr_rep_event(struct cm_work *work)
{
	struct cm_sidr_rep_msg *sidr_rep_msg;
	struct ib_cm_sidr_rep_event_param *param;

	sidr_rep_msg = (struct cm_sidr_rep_msg *)
				work->mad_recv_wc->recv_buf.mad;
	param = &work->cm_event.param.sidr_rep_rcvd;
	param->status = sidr_rep_msg->status;
	param->qkey = be32_to_cpu(sidr_rep_msg->qkey);
	param->qpn = be32_to_cpu(cm_sidr_rep_get_qpn(sidr_rep_msg));
	param->info = &sidr_rep_msg->info;
	param->info_len = sidr_rep_msg->info_length;
	work->cm_event.private_data = &sidr_rep_msg->private_data;
}

static int cm_sidr_rep_handler(struct cm_work *work)
{
	struct cm_sidr_rep_msg *sidr_rep_msg;
	struct cm_id_private *cm_id_priv;

	sidr_rep_msg = (struct cm_sidr_rep_msg *)
				work->mad_recv_wc->recv_buf.mad;
	cm_id_priv = cm_acquire_id(sidr_rep_msg->request_id, 0);
	if (!cm_id_priv)
		return -EINVAL; /* Unmatched reply. */

	spin_lock_irq(&cm_id_priv->lock);
	if (cm_id_priv->id.state != IB_CM_SIDR_REQ_SENT) {
		spin_unlock_irq(&cm_id_priv->lock);
		goto out;
	}
	cm_id_priv->id.state = IB_CM_IDLE;
	ib_cancel_mad(cm_id_priv->av.port->mad_agent, cm_id_priv->msg);
	spin_unlock_irq(&cm_id_priv->lock);

	cm_format_sidr_rep_event(work);
	cm_process_work(cm_id_priv, work);
	return 0;
out:
	cm_deref_id(cm_id_priv);
	return -EINVAL;
}

static void cm_process_send_error(struct ib_mad_send_buf *msg,
				  enum ib_wc_status wc_status)
{
	struct cm_id_private *cm_id_priv;
	struct ib_cm_event cm_event;
	enum ib_cm_state state;
	int ret;

	memset(&cm_event, 0, sizeof cm_event);
	cm_id_priv = msg->context[0];

	/* Discard old sends or ones without a response. */
	spin_lock_irq(&cm_id_priv->lock);
	state = (enum ib_cm_state) (unsigned long) msg->context[1];
	if (msg != cm_id_priv->msg || state != cm_id_priv->id.state)
		goto discard;

	switch (state) {
	case IB_CM_REQ_SENT:
	case IB_CM_MRA_REQ_RCVD:
		cm_reset_to_idle(cm_id_priv);
		cm_event.event = IB_CM_REQ_ERROR;
		break;
	case IB_CM_REP_SENT:
	case IB_CM_MRA_REP_RCVD:
		cm_reset_to_idle(cm_id_priv);
		cm_event.event = IB_CM_REP_ERROR;
		break;
	case IB_CM_DREQ_SENT:
		cm_enter_timewait(cm_id_priv);
		cm_event.event = IB_CM_DREQ_ERROR;
		break;
	case IB_CM_SIDR_REQ_SENT:
		cm_id_priv->id.state = IB_CM_IDLE;
		cm_event.event = IB_CM_SIDR_REQ_ERROR;
		break;
	default:
		goto discard;
	}
	spin_unlock_irq(&cm_id_priv->lock);
	cm_event.param.send_status = wc_status;

	/* No other events can occur on the cm_id at this point. */
	ret = cm_id_priv->id.cm_handler(&cm_id_priv->id, &cm_event);
	cm_free_msg(msg);
	if (ret)
		ib_destroy_cm_id(&cm_id_priv->id);
	return;
discard:
	spin_unlock_irq(&cm_id_priv->lock);
	cm_free_msg(msg);
}

static void cm_send_handler(struct ib_mad_agent *mad_agent,
			    struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_mad_send_buf *msg = mad_send_wc->send_buf;
	struct cm_port *port;
	u16 attr_index;

	port = mad_agent->context;
	attr_index = be16_to_cpu(((struct ib_mad_hdr *)
				  msg->mad)->attr_id) - CM_ATTR_ID_OFFSET;

	/*
	 * If the send was in response to a received message (context[0] is not
	 * set to a cm_id), and is not a REJ, then it is a send that was
	 * manually retried.
	 */
	if (!msg->context[0] && (attr_index != CM_REJ_COUNTER))
		msg->retries = 1;

	atomic_long_add(1 + msg->retries,
			&port->counter_group[CM_XMIT].counter[attr_index]);
	if (msg->retries)
		atomic_long_add(msg->retries,
				&port->counter_group[CM_XMIT_RETRIES].
				counter[attr_index]);

	switch (mad_send_wc->status) {
	case IB_WC_SUCCESS:
	case IB_WC_WR_FLUSH_ERR:
		cm_free_msg(msg);
		break;
	default:
		if (msg->context[0] && msg->context[1])
			cm_process_send_error(msg, mad_send_wc->status);
		else
			cm_free_msg(msg);
		break;
	}
}

static void cm_work_handler(struct work_struct *_work)
{
	struct cm_work *work = container_of(_work, struct cm_work, work.work);
	int ret;

	switch (work->cm_event.event) {
	case IB_CM_REQ_RECEIVED:
		ret = cm_req_handler(work);
		break;
	case IB_CM_MRA_RECEIVED:
		ret = cm_mra_handler(work);
		break;
	case IB_CM_REJ_RECEIVED:
		ret = cm_rej_handler(work);
		break;
	case IB_CM_REP_RECEIVED:
		ret = cm_rep_handler(work);
		break;
	case IB_CM_RTU_RECEIVED:
		ret = cm_rtu_handler(work);
		break;
	case IB_CM_USER_ESTABLISHED:
		ret = cm_establish_handler(work);
		break;
	case IB_CM_DREQ_RECEIVED:
		ret = cm_dreq_handler(work);
		break;
	case IB_CM_DREP_RECEIVED:
		ret = cm_drep_handler(work);
		break;
	case IB_CM_SIDR_REQ_RECEIVED:
		ret = cm_sidr_req_handler(work);
		break;
	case IB_CM_SIDR_REP_RECEIVED:
		ret = cm_sidr_rep_handler(work);
		break;
	case IB_CM_LAP_RECEIVED:
		ret = cm_lap_handler(work);
		break;
	case IB_CM_APR_RECEIVED:
		ret = cm_apr_handler(work);
		break;
	case IB_CM_TIMEWAIT_EXIT:
		ret = cm_timewait_handler(work);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret)
		cm_free_work(work);
}

static int cm_establish(struct ib_cm_id *cm_id)
{
	struct cm_id_private *cm_id_priv;
	struct cm_work *work;
	unsigned long flags;
	int ret = 0;

	work = kmalloc(sizeof *work, GFP_ATOMIC);
	if (!work)
		return -ENOMEM;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	switch (cm_id->state)
	{
	case IB_CM_REP_SENT:
	case IB_CM_MRA_REP_RCVD:
		cm_id->state = IB_CM_ESTABLISHED;
		break;
	case IB_CM_ESTABLISHED:
		ret = -EISCONN;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);

	if (ret) {
		kfree(work);
		goto out;
	}

	/*
	 * The CM worker thread may try to destroy the cm_id before it
	 * can execute this work item.  To prevent potential deadlock,
	 * we need to find the cm_id once we're in the context of the
	 * worker thread, rather than holding a reference on it.
	 */
	INIT_DELAYED_WORK(&work->work, cm_work_handler);
	work->local_id = cm_id->local_id;
	work->remote_id = cm_id->remote_id;
	work->mad_recv_wc = NULL;
	work->cm_event.event = IB_CM_USER_ESTABLISHED;
	queue_delayed_work(cm.wq, &work->work, 0);
out:
	return ret;
}

static int cm_migrate(struct ib_cm_id *cm_id)
{
	struct cm_id_private *cm_id_priv;
	unsigned long flags;
	int ret = 0;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	spin_lock_irqsave(&cm_id_priv->lock, flags);
	if (cm_id->state == IB_CM_ESTABLISHED &&
	    (cm_id->lap_state == IB_CM_LAP_UNINIT ||
	     cm_id->lap_state == IB_CM_LAP_IDLE)) {
		cm_id->lap_state = IB_CM_LAP_IDLE;
		cm_id_priv->av = cm_id_priv->alt_av;
	} else
		ret = -EINVAL;
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);

	return ret;
}

int ib_cm_notify(struct ib_cm_id *cm_id, enum ib_event_type event)
{
	int ret;

	switch (event) {
	case IB_EVENT_COMM_EST:
		ret = cm_establish(cm_id);
		break;
	case IB_EVENT_PATH_MIG:
		ret = cm_migrate(cm_id);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}
EXPORT_SYMBOL(ib_cm_notify);

static void cm_recv_handler(struct ib_mad_agent *mad_agent,
			    struct ib_mad_recv_wc *mad_recv_wc)
{
	struct cm_port *port = mad_agent->context;
	struct cm_work *work;
	enum ib_cm_event_type event;
	u16 attr_id;
	int paths = 0;

	switch (mad_recv_wc->recv_buf.mad->mad_hdr.attr_id) {
	case CM_REQ_ATTR_ID:
		paths = 1 + (((struct cm_req_msg *) mad_recv_wc->recv_buf.mad)->
						    alt_local_lid != 0);
		event = IB_CM_REQ_RECEIVED;
		break;
	case CM_MRA_ATTR_ID:
		event = IB_CM_MRA_RECEIVED;
		break;
	case CM_REJ_ATTR_ID:
		event = IB_CM_REJ_RECEIVED;
		break;
	case CM_REP_ATTR_ID:
		event = IB_CM_REP_RECEIVED;
		break;
	case CM_RTU_ATTR_ID:
		event = IB_CM_RTU_RECEIVED;
		break;
	case CM_DREQ_ATTR_ID:
		event = IB_CM_DREQ_RECEIVED;
		break;
	case CM_DREP_ATTR_ID:
		event = IB_CM_DREP_RECEIVED;
		break;
	case CM_SIDR_REQ_ATTR_ID:
		event = IB_CM_SIDR_REQ_RECEIVED;
		break;
	case CM_SIDR_REP_ATTR_ID:
		event = IB_CM_SIDR_REP_RECEIVED;
		break;
	case CM_LAP_ATTR_ID:
		paths = 1;
		event = IB_CM_LAP_RECEIVED;
		break;
	case CM_APR_ATTR_ID:
		event = IB_CM_APR_RECEIVED;
		break;
	default:
		ib_free_recv_mad(mad_recv_wc);
		return;
	}

	attr_id = be16_to_cpu(mad_recv_wc->recv_buf.mad->mad_hdr.attr_id);
	atomic_long_inc(&port->counter_group[CM_RECV].
			counter[attr_id - CM_ATTR_ID_OFFSET]);

	work = kmalloc(sizeof *work + sizeof(struct ib_sa_path_rec) * paths,
		       GFP_KERNEL);
	if (!work) {
		ib_free_recv_mad(mad_recv_wc);
		return;
	}

	INIT_DELAYED_WORK(&work->work, cm_work_handler);
	work->cm_event.event = event;
	work->mad_recv_wc = mad_recv_wc;
	work->port = port;
	queue_delayed_work(cm.wq, &work->work, 0);
}

static int cm_init_qp_init_attr(struct cm_id_private *cm_id_priv,
				struct ib_qp_attr *qp_attr,
				int *qp_attr_mask)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cm_id_priv->lock, flags);
	switch (cm_id_priv->id.state) {
	case IB_CM_REQ_SENT:
	case IB_CM_MRA_REQ_RCVD:
	case IB_CM_REQ_RCVD:
	case IB_CM_MRA_REQ_SENT:
	case IB_CM_REP_RCVD:
	case IB_CM_MRA_REP_SENT:
	case IB_CM_REP_SENT:
	case IB_CM_MRA_REP_RCVD:
	case IB_CM_ESTABLISHED:
		*qp_attr_mask = IB_QP_STATE | IB_QP_ACCESS_FLAGS |
				IB_QP_PKEY_INDEX | IB_QP_PORT;
		qp_attr->qp_access_flags = IB_ACCESS_REMOTE_WRITE;
		if (cm_id_priv->responder_resources)
			qp_attr->qp_access_flags |= IB_ACCESS_REMOTE_READ |
						    IB_ACCESS_REMOTE_ATOMIC;
		qp_attr->pkey_index = cm_id_priv->av.pkey_index;
		qp_attr->port_num = cm_id_priv->av.port->port_num;
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}

static int cm_init_qp_rtr_attr(struct cm_id_private *cm_id_priv,
			       struct ib_qp_attr *qp_attr,
			       int *qp_attr_mask)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cm_id_priv->lock, flags);
	switch (cm_id_priv->id.state) {
	case IB_CM_REQ_RCVD:
	case IB_CM_MRA_REQ_SENT:
	case IB_CM_REP_RCVD:
	case IB_CM_MRA_REP_SENT:
	case IB_CM_REP_SENT:
	case IB_CM_MRA_REP_RCVD:
	case IB_CM_ESTABLISHED:
		*qp_attr_mask = IB_QP_STATE | IB_QP_AV | IB_QP_PATH_MTU |
				IB_QP_DEST_QPN | IB_QP_RQ_PSN;
		qp_attr->ah_attr = cm_id_priv->av.ah_attr;
		qp_attr->path_mtu = cm_id_priv->path_mtu;
		qp_attr->dest_qp_num = be32_to_cpu(cm_id_priv->remote_qpn);
		qp_attr->rq_psn = be32_to_cpu(cm_id_priv->rq_psn);
		if (cm_id_priv->qp_type == IB_QPT_RC) {
			*qp_attr_mask |= IB_QP_MAX_DEST_RD_ATOMIC |
					 IB_QP_MIN_RNR_TIMER;
			qp_attr->max_dest_rd_atomic =
					cm_id_priv->responder_resources;
			qp_attr->min_rnr_timer = 0;
		}
		if (cm_id_priv->alt_av.ah_attr.dlid) {
			*qp_attr_mask |= IB_QP_ALT_PATH;
			qp_attr->alt_port_num = cm_id_priv->alt_av.port->port_num;
			qp_attr->alt_pkey_index = cm_id_priv->alt_av.pkey_index;
			qp_attr->alt_timeout = cm_id_priv->alt_av.timeout;
			qp_attr->alt_ah_attr = cm_id_priv->alt_av.ah_attr;
		}
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}

static int cm_init_qp_rts_attr(struct cm_id_private *cm_id_priv,
			       struct ib_qp_attr *qp_attr,
			       int *qp_attr_mask)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cm_id_priv->lock, flags);
	switch (cm_id_priv->id.state) {
	/* Allow transition to RTS before sending REP */
	case IB_CM_REQ_RCVD:
	case IB_CM_MRA_REQ_SENT:

	case IB_CM_REP_RCVD:
	case IB_CM_MRA_REP_SENT:
	case IB_CM_REP_SENT:
	case IB_CM_MRA_REP_RCVD:
	case IB_CM_ESTABLISHED:
		if (cm_id_priv->id.lap_state == IB_CM_LAP_UNINIT) {
			*qp_attr_mask = IB_QP_STATE | IB_QP_SQ_PSN;
			qp_attr->sq_psn = be32_to_cpu(cm_id_priv->sq_psn);
			if (cm_id_priv->qp_type == IB_QPT_RC) {
				*qp_attr_mask |= IB_QP_TIMEOUT | IB_QP_RETRY_CNT |
						 IB_QP_RNR_RETRY |
						 IB_QP_MAX_QP_RD_ATOMIC;
				qp_attr->timeout = cm_id_priv->av.timeout;
				qp_attr->retry_cnt = cm_id_priv->retry_count;
				qp_attr->rnr_retry = cm_id_priv->rnr_retry_count;
				qp_attr->max_rd_atomic =
					cm_id_priv->initiator_depth;
			}
			if (cm_id_priv->alt_av.ah_attr.dlid) {
				*qp_attr_mask |= IB_QP_PATH_MIG_STATE;
				qp_attr->path_mig_state = IB_MIG_REARM;
			}
		} else {
			*qp_attr_mask = IB_QP_ALT_PATH | IB_QP_PATH_MIG_STATE;
			qp_attr->alt_port_num = cm_id_priv->alt_av.port->port_num;
			qp_attr->alt_pkey_index = cm_id_priv->alt_av.pkey_index;
			qp_attr->alt_timeout = cm_id_priv->alt_av.timeout;
			qp_attr->alt_ah_attr = cm_id_priv->alt_av.ah_attr;
			qp_attr->path_mig_state = IB_MIG_REARM;
		}
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&cm_id_priv->lock, flags);
	return ret;
}

int ib_cm_init_qp_attr(struct ib_cm_id *cm_id,
		       struct ib_qp_attr *qp_attr,
		       int *qp_attr_mask)
{
	struct cm_id_private *cm_id_priv;
	int ret;

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);
	switch (qp_attr->qp_state) {
	case IB_QPS_INIT:
		ret = cm_init_qp_init_attr(cm_id_priv, qp_attr, qp_attr_mask);
		break;
	case IB_QPS_RTR:
		ret = cm_init_qp_rtr_attr(cm_id_priv, qp_attr, qp_attr_mask);
		break;
	case IB_QPS_RTS:
		ret = cm_init_qp_rts_attr(cm_id_priv, qp_attr, qp_attr_mask);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}
EXPORT_SYMBOL(ib_cm_init_qp_attr);

static void cm_get_ack_delay(struct cm_device *cm_dev)
{
	struct ib_device_attr attr;

	if (ib_query_device(cm_dev->device, &attr))
		cm_dev->ack_delay = 0; /* acks will rely on packet life time */
	else
		cm_dev->ack_delay = attr.local_ca_ack_delay;
}

static ssize_t cm_show_counter(struct kobject *obj, struct attribute *attr,
			       char *buf)
{
	struct cm_counter_group *group;
	struct cm_counter_attribute *cm_attr;

	group = container_of(obj, struct cm_counter_group, obj);
	cm_attr = container_of(attr, struct cm_counter_attribute, attr);

	return sprintf(buf, "%ld\n",
		       atomic_long_read(&group->counter[cm_attr->index]));
}

static struct sysfs_ops cm_counter_ops = {
	.show = cm_show_counter
};

static struct kobj_type cm_counter_obj_type = {
	.sysfs_ops = &cm_counter_ops,
	.default_attrs = cm_counter_default_attrs
};

static void cm_release_port_obj(struct kobject *obj)
{
	struct cm_port *cm_port;

	printk(KERN_ERR "free cm port\n");

	cm_port = container_of(obj, struct cm_port, port_obj);
	kfree(cm_port);
}

static struct kobj_type cm_port_obj_type = {
	.release = cm_release_port_obj
};

static void cm_release_dev_obj(struct kobject *obj)
{
	struct cm_device *cm_dev;

	printk(KERN_ERR "free cm dev\n");

	cm_dev = container_of(obj, struct cm_device, dev_obj);
	kfree(cm_dev);
}

static struct kobj_type cm_dev_obj_type = {
	.release = cm_release_dev_obj
};

struct class cm_class = {
	.name    = "infiniband_cm",
};
EXPORT_SYMBOL(cm_class);

static void cm_remove_fs_obj(struct kobject *obj)
{
	kobject_put(obj->parent);
	kobject_put(obj);
}

static int cm_create_port_fs(struct cm_port *port)
{
	int i, ret;

	ret = kobject_init_and_add(&port->port_obj, &cm_port_obj_type,
				   kobject_get(&port->cm_dev->dev_obj),
				   "%d", port->port_num);
	if (ret) {
		kfree(port);
		return ret;
	}

	for (i = 0; i < CM_COUNTER_GROUPS; i++) {
		ret = kobject_init_and_add(&port->counter_group[i].obj,
					   &cm_counter_obj_type,
					   kobject_get(&port->port_obj),
					   "%s", counter_group_names[i]);
		if (ret)
			goto error;
	}

	return 0;

error:
	while (i--)
		cm_remove_fs_obj(&port->counter_group[i].obj);
	cm_remove_fs_obj(&port->port_obj);
	return ret;

}

static void cm_remove_port_fs(struct cm_port *port)
{
	int i;

	for (i = 0; i < CM_COUNTER_GROUPS; i++)
		cm_remove_fs_obj(&port->counter_group[i].obj);

	cm_remove_fs_obj(&port->port_obj);
}

static void cm_add_one(struct ib_device *device)
{
	struct cm_device *cm_dev;
	struct cm_port *port;
	struct ib_mad_reg_req reg_req = {
		.mgmt_class = IB_MGMT_CLASS_CM,
		.mgmt_class_version = IB_CM_CLASS_VERSION
	};
	struct ib_port_modify port_modify = {
		.set_port_cap_mask = IB_PORT_CM_SUP
	};
	unsigned long flags;
	int ret;
	u8 i;

	if (rdma_node_get_transport(device->node_type) != RDMA_TRANSPORT_IB)
		return;

	cm_dev = kzalloc(sizeof(*cm_dev) + sizeof(*port) *
			 device->phys_port_cnt, GFP_KERNEL);
	if (!cm_dev)
		return;

	cm_dev->device = device;
	cm_get_ack_delay(cm_dev);

	ret = kobject_init_and_add(&cm_dev->dev_obj, &cm_dev_obj_type,
				   &cm_class.subsys.kobj, "%s", device->name);
	if (ret) {
		kfree(cm_dev);
		return;
	}

	set_bit(IB_MGMT_METHOD_SEND, reg_req.method_mask);
	for (i = 1; i <= device->phys_port_cnt; i++) {
		port = kzalloc(sizeof *port, GFP_KERNEL);
		if (!port)
			goto error1;

		cm_dev->port[i-1] = port;
		port->cm_dev = cm_dev;
		port->port_num = i;

		ret = cm_create_port_fs(port);
		if (ret)
			goto error1;

		port->mad_agent = ib_register_mad_agent(device, i,
							IB_QPT_GSI,
							&reg_req,
							0,
							cm_send_handler,
							cm_recv_handler,
							port);
		if (IS_ERR(port->mad_agent))
			goto error2;

		ret = ib_modify_port(device, i, 0, &port_modify);
		if (ret)
			goto error3;
	}
	ib_set_client_data(device, &cm_client, cm_dev);

	write_lock_irqsave(&cm.device_lock, flags);
	list_add_tail(&cm_dev->list, &cm.device_list);
	write_unlock_irqrestore(&cm.device_lock, flags);
	return;

error3:
	ib_unregister_mad_agent(port->mad_agent);
error2:
	cm_remove_port_fs(port);
error1:
	port_modify.set_port_cap_mask = 0;
	port_modify.clr_port_cap_mask = IB_PORT_CM_SUP;
	while (--i) {
		port = cm_dev->port[i-1];
		ib_modify_port(device, port->port_num, 0, &port_modify);
		ib_unregister_mad_agent(port->mad_agent);
		cm_remove_port_fs(port);
	}
	cm_remove_fs_obj(&cm_dev->dev_obj);
}

static void cm_remove_one(struct ib_device *device)
{
	struct cm_device *cm_dev;
	struct cm_port *port;
	struct ib_port_modify port_modify = {
		.clr_port_cap_mask = IB_PORT_CM_SUP
	};
	unsigned long flags;
	int i;

	cm_dev = ib_get_client_data(device, &cm_client);
	if (!cm_dev)
		return;

	write_lock_irqsave(&cm.device_lock, flags);
	list_del(&cm_dev->list);
	write_unlock_irqrestore(&cm.device_lock, flags);

	for (i = 1; i <= device->phys_port_cnt; i++) {
		port = cm_dev->port[i-1];
		ib_modify_port(device, port->port_num, 0, &port_modify);
		ib_unregister_mad_agent(port->mad_agent);
		cm_remove_port_fs(port);
	}
	cm_remove_fs_obj(&cm_dev->dev_obj);
}

static int __init ib_cm_init(void)
{
	int ret;

	memset(&cm, 0, sizeof cm);
	INIT_LIST_HEAD(&cm.device_list);
	rwlock_init(&cm.device_lock);
	spin_lock_init(&cm.lock);
	cm.listen_service_table = RB_ROOT;
	cm.listen_service_id = __constant_be64_to_cpu(IB_CM_ASSIGN_SERVICE_ID);
	cm.remote_id_table = RB_ROOT;
	cm.remote_qp_table = RB_ROOT;
	cm.remote_sidr_table = RB_ROOT;
	idr_init(&cm.local_id_table);
	get_random_bytes(&cm.random_id_operand, sizeof cm.random_id_operand);
	idr_pre_get(&cm.local_id_table, GFP_KERNEL);
	INIT_LIST_HEAD(&cm.timewait_list);

	ret = class_register(&cm_class);
	if (ret)
		return -ENOMEM;

	cm.wq = create_workqueue("ib_cm");
	if (!cm.wq) {
		ret = -ENOMEM;
		goto error1;
	}

	ret = ib_register_client(&cm_client);
	if (ret)
		goto error2;

	return 0;
error2:
	destroy_workqueue(cm.wq);
error1:
	class_unregister(&cm_class);
	return ret;
}

static void __exit ib_cm_cleanup(void)
{
	struct cm_timewait_info *timewait_info, *tmp;

	spin_lock_irq(&cm.lock);
	list_for_each_entry(timewait_info, &cm.timewait_list, list)
		cancel_delayed_work(&timewait_info->work.work);
	spin_unlock_irq(&cm.lock);

	destroy_workqueue(cm.wq);

	list_for_each_entry_safe(timewait_info, tmp, &cm.timewait_list, list) {
		list_del(&timewait_info->list);
		kfree(timewait_info);
	}

	ib_unregister_client(&cm_client);
	class_unregister(&cm_class);
	idr_destroy(&cm.local_id_table);
}

module_init(ib_cm_init);
module_exit(ib_cm_cleanup);

