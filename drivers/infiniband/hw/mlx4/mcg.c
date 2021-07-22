/*
 * Copyright (c) 2012 Mellanox Technologies. All rights reserved.
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

#include <rdma/ib_mad.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_sa.h>

#include <linux/mlx4/cmd.h>
#include <linux/rbtree.h>
#include <linux/delay.h>

#include "mlx4_ib.h"

#define MAX_VFS		80
#define MAX_PEND_REQS_PER_FUNC 4
#define MAD_TIMEOUT_MS	2000

#define mcg_warn(fmt, arg...)	pr_warn("MCG WARNING: " fmt, ##arg)
#define mcg_error(fmt, arg...)	pr_err(fmt, ##arg)
#define mcg_warn_group(group, format, arg...) \
	pr_warn("%s-%d: %16s (port %d): WARNING: " format, __func__, __LINE__,\
	(group)->name, group->demux->port, ## arg)

#define mcg_debug_group(group, format, arg...) \
	pr_debug("%s-%d: %16s (port %d): WARNING: " format, __func__, __LINE__,\
		 (group)->name, (group)->demux->port, ## arg)

#define mcg_error_group(group, format, arg...) \
	pr_err("  %16s: " format, (group)->name, ## arg)


static union ib_gid mgid0;

static struct workqueue_struct *clean_wq;

enum mcast_state {
	MCAST_NOT_MEMBER = 0,
	MCAST_MEMBER,
};

enum mcast_group_state {
	MCAST_IDLE,
	MCAST_JOIN_SENT,
	MCAST_LEAVE_SENT,
	MCAST_RESP_READY
};

struct mcast_member {
	enum mcast_state state;
	uint8_t			join_state;
	int			num_pend_reqs;
	struct list_head	pending;
};

struct ib_sa_mcmember_data {
	union ib_gid	mgid;
	union ib_gid	port_gid;
	__be32		qkey;
	__be16		mlid;
	u8		mtusel_mtu;
	u8		tclass;
	__be16		pkey;
	u8		ratesel_rate;
	u8		lifetmsel_lifetm;
	__be32		sl_flowlabel_hoplimit;
	u8		scope_join_state;
	u8		proxy_join;
	u8		reserved[2];
} __packed __aligned(4);

struct mcast_group {
	struct ib_sa_mcmember_data rec;
	struct rb_node		node;
	struct list_head	mgid0_list;
	struct mlx4_ib_demux_ctx *demux;
	struct mcast_member	func[MAX_VFS];
	struct mutex		lock;
	struct work_struct	work;
	struct list_head	pending_list;
	int			members[3];
	enum mcast_group_state	state;
	enum mcast_group_state	prev_state;
	struct ib_sa_mad	response_sa_mad;
	__be64			last_req_tid;

	char			name[33]; /* MGID string */
	struct device_attribute	dentry;

	/* refcount is the reference count for the following:
	   1. Each queued request
	   2. Each invocation of the worker thread
	   3. Membership of the port at the SA
	*/
	atomic_t		refcount;

	/* delayed work to clean pending SM request */
	struct delayed_work	timeout_work;
	struct list_head	cleanup_list;
};

struct mcast_req {
	int			func;
	struct ib_sa_mad	sa_mad;
	struct list_head	group_list;
	struct list_head	func_list;
	struct mcast_group	*group;
	int			clean;
};


#define safe_atomic_dec(ref) \
	do {\
		if (atomic_dec_and_test(ref)) \
			mcg_warn_group(group, "did not expect to reach zero\n"); \
	} while (0)

static const char *get_state_string(enum mcast_group_state state)
{
	switch (state) {
	case MCAST_IDLE:
		return "MCAST_IDLE";
	case MCAST_JOIN_SENT:
		return "MCAST_JOIN_SENT";
	case MCAST_LEAVE_SENT:
		return "MCAST_LEAVE_SENT";
	case MCAST_RESP_READY:
		return "MCAST_RESP_READY";
	}
	return "Invalid State";
}

static struct mcast_group *mcast_find(struct mlx4_ib_demux_ctx *ctx,
				      union ib_gid *mgid)
{
	struct rb_node *node = ctx->mcg_table.rb_node;
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

static struct mcast_group *mcast_insert(struct mlx4_ib_demux_ctx *ctx,
					struct mcast_group *group)
{
	struct rb_node **link = &ctx->mcg_table.rb_node;
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
		else
			return cur_group;
	}
	rb_link_node(&group->node, parent, link);
	rb_insert_color(&group->node, &ctx->mcg_table);
	return NULL;
}

static int send_mad_to_wire(struct mlx4_ib_demux_ctx *ctx, struct ib_mad *mad)
{
	struct mlx4_ib_dev *dev = ctx->dev;
	struct rdma_ah_attr	ah_attr;
	unsigned long flags;

	spin_lock_irqsave(&dev->sm_lock, flags);
	if (!dev->sm_ah[ctx->port - 1]) {
		/* port is not yet Active, sm_ah not ready */
		spin_unlock_irqrestore(&dev->sm_lock, flags);
		return -EAGAIN;
	}
	mlx4_ib_query_ah(dev->sm_ah[ctx->port - 1], &ah_attr);
	spin_unlock_irqrestore(&dev->sm_lock, flags);
	return mlx4_ib_send_to_wire(dev, mlx4_master_func_num(dev->dev),
				    ctx->port, IB_QPT_GSI, 0, 1, IB_QP1_QKEY,
				    &ah_attr, NULL, 0xffff, mad);
}

static int send_mad_to_slave(int slave, struct mlx4_ib_demux_ctx *ctx,
			     struct ib_mad *mad)
{
	struct mlx4_ib_dev *dev = ctx->dev;
	struct ib_mad_agent *agent = dev->send_agent[ctx->port - 1][1];
	struct ib_wc wc;
	struct rdma_ah_attr ah_attr;

	/* Our agent might not yet be registered when mads start to arrive */
	if (!agent)
		return -EAGAIN;

	rdma_query_ah(dev->sm_ah[ctx->port - 1], &ah_attr);

	if (ib_find_cached_pkey(&dev->ib_dev, ctx->port, IB_DEFAULT_PKEY_FULL, &wc.pkey_index))
		return -EINVAL;
	wc.sl = 0;
	wc.dlid_path_bits = 0;
	wc.port_num = ctx->port;
	wc.slid = rdma_ah_get_dlid(&ah_attr);  /* opensm lid */
	wc.src_qp = 1;
	return mlx4_ib_send_to_slave(dev, slave, ctx->port, IB_QPT_GSI, &wc, NULL, mad);
}

static int send_join_to_wire(struct mcast_group *group, struct ib_sa_mad *sa_mad)
{
	struct ib_sa_mad mad;
	struct ib_sa_mcmember_data *sa_mad_data = (struct ib_sa_mcmember_data *)&mad.data;
	int ret;

	/* we rely on a mad request as arrived from a VF */
	memcpy(&mad, sa_mad, sizeof mad);

	/* fix port GID to be the real one (slave 0) */
	sa_mad_data->port_gid.global.interface_id = group->demux->guid_cache[0];

	/* assign our own TID */
	mad.mad_hdr.tid = mlx4_ib_get_new_demux_tid(group->demux);
	group->last_req_tid = mad.mad_hdr.tid; /* keep it for later validation */

	ret = send_mad_to_wire(group->demux, (struct ib_mad *)&mad);
	/* set timeout handler */
	if (!ret) {
		/* calls mlx4_ib_mcg_timeout_handler */
		queue_delayed_work(group->demux->mcg_wq, &group->timeout_work,
				msecs_to_jiffies(MAD_TIMEOUT_MS));
	}

	return ret;
}

static int send_leave_to_wire(struct mcast_group *group, u8 join_state)
{
	struct ib_sa_mad mad;
	struct ib_sa_mcmember_data *sa_data = (struct ib_sa_mcmember_data *)&mad.data;
	int ret;

	memset(&mad, 0, sizeof mad);
	mad.mad_hdr.base_version = 1;
	mad.mad_hdr.mgmt_class = IB_MGMT_CLASS_SUBN_ADM;
	mad.mad_hdr.class_version = 2;
	mad.mad_hdr.method = IB_SA_METHOD_DELETE;
	mad.mad_hdr.status = cpu_to_be16(0);
	mad.mad_hdr.class_specific = cpu_to_be16(0);
	mad.mad_hdr.tid = mlx4_ib_get_new_demux_tid(group->demux);
	group->last_req_tid = mad.mad_hdr.tid; /* keep it for later validation */
	mad.mad_hdr.attr_id = cpu_to_be16(IB_SA_ATTR_MC_MEMBER_REC);
	mad.mad_hdr.attr_mod = cpu_to_be32(0);
	mad.sa_hdr.sm_key = 0x0;
	mad.sa_hdr.attr_offset = cpu_to_be16(7);
	mad.sa_hdr.comp_mask = IB_SA_MCMEMBER_REC_MGID |
		IB_SA_MCMEMBER_REC_PORT_GID | IB_SA_MCMEMBER_REC_JOIN_STATE;

	*sa_data = group->rec;
	sa_data->scope_join_state = join_state;

	ret = send_mad_to_wire(group->demux, (struct ib_mad *)&mad);
	if (ret)
		group->state = MCAST_IDLE;

	/* set timeout handler */
	if (!ret) {
		/* calls mlx4_ib_mcg_timeout_handler */
		queue_delayed_work(group->demux->mcg_wq, &group->timeout_work,
				msecs_to_jiffies(MAD_TIMEOUT_MS));
	}

	return ret;
}

static int send_reply_to_slave(int slave, struct mcast_group *group,
		struct ib_sa_mad *req_sa_mad, u16 status)
{
	struct ib_sa_mad mad;
	struct ib_sa_mcmember_data *sa_data = (struct ib_sa_mcmember_data *)&mad.data;
	struct ib_sa_mcmember_data *req_sa_data = (struct ib_sa_mcmember_data *)&req_sa_mad->data;
	int ret;

	memset(&mad, 0, sizeof mad);
	mad.mad_hdr.base_version = 1;
	mad.mad_hdr.mgmt_class = IB_MGMT_CLASS_SUBN_ADM;
	mad.mad_hdr.class_version = 2;
	mad.mad_hdr.method = IB_MGMT_METHOD_GET_RESP;
	mad.mad_hdr.status = cpu_to_be16(status);
	mad.mad_hdr.class_specific = cpu_to_be16(0);
	mad.mad_hdr.tid = req_sa_mad->mad_hdr.tid;
	*(u8 *)&mad.mad_hdr.tid = 0; /* resetting tid to 0 */
	mad.mad_hdr.attr_id = cpu_to_be16(IB_SA_ATTR_MC_MEMBER_REC);
	mad.mad_hdr.attr_mod = cpu_to_be32(0);
	mad.sa_hdr.sm_key = req_sa_mad->sa_hdr.sm_key;
	mad.sa_hdr.attr_offset = cpu_to_be16(7);
	mad.sa_hdr.comp_mask = 0; /* ignored on responses, see IBTA spec */

	*sa_data = group->rec;

	/* reconstruct VF's requested join_state and port_gid */
	sa_data->scope_join_state &= 0xf0;
	sa_data->scope_join_state |= (group->func[slave].join_state & 0x0f);
	memcpy(&sa_data->port_gid, &req_sa_data->port_gid, sizeof req_sa_data->port_gid);

	ret = send_mad_to_slave(slave, group->demux, (struct ib_mad *)&mad);
	return ret;
}

static int check_selector(ib_sa_comp_mask comp_mask,
			  ib_sa_comp_mask selector_mask,
			  ib_sa_comp_mask value_mask,
			  u8 src_value, u8 dst_value)
{
	int err;
	u8 selector = dst_value >> 6;
	dst_value &= 0x3f;
	src_value &= 0x3f;

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

static u16 cmp_rec(struct ib_sa_mcmember_data *src,
		   struct ib_sa_mcmember_data *dst, ib_sa_comp_mask comp_mask)
{
	/* src is group record, dst is request record */
	/* MGID must already match */
	/* Port_GID we always replace to our Port_GID, so it is a match */

#define MAD_STATUS_REQ_INVALID 0x0200
	if (comp_mask & IB_SA_MCMEMBER_REC_QKEY && src->qkey != dst->qkey)
		return MAD_STATUS_REQ_INVALID;
	if (comp_mask & IB_SA_MCMEMBER_REC_MLID && src->mlid != dst->mlid)
		return MAD_STATUS_REQ_INVALID;
	if (check_selector(comp_mask, IB_SA_MCMEMBER_REC_MTU_SELECTOR,
				 IB_SA_MCMEMBER_REC_MTU,
				 src->mtusel_mtu, dst->mtusel_mtu))
		return MAD_STATUS_REQ_INVALID;
	if (comp_mask & IB_SA_MCMEMBER_REC_TRAFFIC_CLASS &&
	    src->tclass != dst->tclass)
		return MAD_STATUS_REQ_INVALID;
	if (comp_mask & IB_SA_MCMEMBER_REC_PKEY && src->pkey != dst->pkey)
		return MAD_STATUS_REQ_INVALID;
	if (check_selector(comp_mask, IB_SA_MCMEMBER_REC_RATE_SELECTOR,
				 IB_SA_MCMEMBER_REC_RATE,
				 src->ratesel_rate, dst->ratesel_rate))
		return MAD_STATUS_REQ_INVALID;
	if (check_selector(comp_mask,
				 IB_SA_MCMEMBER_REC_PACKET_LIFE_TIME_SELECTOR,
				 IB_SA_MCMEMBER_REC_PACKET_LIFE_TIME,
				 src->lifetmsel_lifetm, dst->lifetmsel_lifetm))
		return MAD_STATUS_REQ_INVALID;
	if (comp_mask & IB_SA_MCMEMBER_REC_SL &&
			(be32_to_cpu(src->sl_flowlabel_hoplimit) & 0xf0000000) !=
			(be32_to_cpu(dst->sl_flowlabel_hoplimit) & 0xf0000000))
		return MAD_STATUS_REQ_INVALID;
	if (comp_mask & IB_SA_MCMEMBER_REC_FLOW_LABEL &&
			(be32_to_cpu(src->sl_flowlabel_hoplimit) & 0x0fffff00) !=
			(be32_to_cpu(dst->sl_flowlabel_hoplimit) & 0x0fffff00))
		return MAD_STATUS_REQ_INVALID;
	if (comp_mask & IB_SA_MCMEMBER_REC_HOP_LIMIT &&
			(be32_to_cpu(src->sl_flowlabel_hoplimit) & 0x000000ff) !=
			(be32_to_cpu(dst->sl_flowlabel_hoplimit) & 0x000000ff))
		return MAD_STATUS_REQ_INVALID;
	if (comp_mask & IB_SA_MCMEMBER_REC_SCOPE &&
			(src->scope_join_state & 0xf0) !=
			(dst->scope_join_state & 0xf0))
		return MAD_STATUS_REQ_INVALID;

	/* join_state checked separately, proxy_join ignored */

	return 0;
}

/* release group, return 1 if this was last release and group is destroyed
 * timout work is canceled sync */
static int release_group(struct mcast_group *group, int from_timeout_handler)
{
	struct mlx4_ib_demux_ctx *ctx = group->demux;
	int nzgroup;

	mutex_lock(&ctx->mcg_table_lock);
	mutex_lock(&group->lock);
	if (atomic_dec_and_test(&group->refcount)) {
		if (!from_timeout_handler) {
			if (group->state != MCAST_IDLE &&
			    !cancel_delayed_work(&group->timeout_work)) {
				atomic_inc(&group->refcount);
				mutex_unlock(&group->lock);
				mutex_unlock(&ctx->mcg_table_lock);
				return 0;
			}
		}

		nzgroup = memcmp(&group->rec.mgid, &mgid0, sizeof mgid0);
		if (nzgroup)
			del_sysfs_port_mcg_attr(ctx->dev, ctx->port, &group->dentry.attr);
		if (!list_empty(&group->pending_list))
			mcg_warn_group(group, "releasing a group with non empty pending list\n");
		if (nzgroup)
			rb_erase(&group->node, &ctx->mcg_table);
		list_del_init(&group->mgid0_list);
		mutex_unlock(&group->lock);
		mutex_unlock(&ctx->mcg_table_lock);
		kfree(group);
		return 1;
	} else {
		mutex_unlock(&group->lock);
		mutex_unlock(&ctx->mcg_table_lock);
	}
	return 0;
}

static void adjust_membership(struct mcast_group *group, u8 join_state, int inc)
{
	int i;

	for (i = 0; i < 3; i++, join_state >>= 1)
		if (join_state & 0x1)
			group->members[i] += inc;
}

static u8 get_leave_state(struct mcast_group *group)
{
	u8 leave_state = 0;
	int i;

	for (i = 0; i < 3; i++)
		if (!group->members[i])
			leave_state |= (1 << i);

	return leave_state & (group->rec.scope_join_state & 0xf);
}

static int join_group(struct mcast_group *group, int slave, u8 join_mask)
{
	int ret = 0;
	u8 join_state;

	/* remove bits that slave is already member of, and adjust */
	join_state = join_mask & (~group->func[slave].join_state);
	adjust_membership(group, join_state, 1);
	group->func[slave].join_state |= join_state;
	if (group->func[slave].state != MCAST_MEMBER && join_state) {
		group->func[slave].state = MCAST_MEMBER;
		ret = 1;
	}
	return ret;
}

static int leave_group(struct mcast_group *group, int slave, u8 leave_state)
{
	int ret = 0;

	adjust_membership(group, leave_state, -1);
	group->func[slave].join_state &= ~leave_state;
	if (!group->func[slave].join_state) {
		group->func[slave].state = MCAST_NOT_MEMBER;
		ret = 1;
	}
	return ret;
}

static int check_leave(struct mcast_group *group, int slave, u8 leave_mask)
{
	if (group->func[slave].state != MCAST_MEMBER)
		return MAD_STATUS_REQ_INVALID;

	/* make sure we're not deleting unset bits */
	if (~group->func[slave].join_state & leave_mask)
		return MAD_STATUS_REQ_INVALID;

	if (!leave_mask)
		return MAD_STATUS_REQ_INVALID;

	return 0;
}

static void mlx4_ib_mcg_timeout_handler(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct mcast_group *group;
	struct mcast_req *req = NULL;

	group = container_of(delay, typeof(*group), timeout_work);

	mutex_lock(&group->lock);
	if (group->state == MCAST_JOIN_SENT) {
		if (!list_empty(&group->pending_list)) {
			req = list_first_entry(&group->pending_list, struct mcast_req, group_list);
			list_del(&req->group_list);
			list_del(&req->func_list);
			--group->func[req->func].num_pend_reqs;
			mutex_unlock(&group->lock);
			kfree(req);
			if (memcmp(&group->rec.mgid, &mgid0, sizeof mgid0)) {
				if (release_group(group, 1))
					return;
			} else {
				kfree(group);
				return;
			}
			mutex_lock(&group->lock);
		} else
			mcg_warn_group(group, "DRIVER BUG\n");
	} else if (group->state == MCAST_LEAVE_SENT) {
		if (group->rec.scope_join_state & 0xf)
			group->rec.scope_join_state &= 0xf0;
		group->state = MCAST_IDLE;
		mutex_unlock(&group->lock);
		if (release_group(group, 1))
			return;
		mutex_lock(&group->lock);
	} else
		mcg_warn_group(group, "invalid state %s\n", get_state_string(group->state));
	group->state = MCAST_IDLE;
	atomic_inc(&group->refcount);
	if (!queue_work(group->demux->mcg_wq, &group->work))
		safe_atomic_dec(&group->refcount);

	mutex_unlock(&group->lock);
}

static int handle_leave_req(struct mcast_group *group, u8 leave_mask,
			    struct mcast_req *req)
{
	u16 status;

	if (req->clean)
		leave_mask = group->func[req->func].join_state;

	status = check_leave(group, req->func, leave_mask);
	if (!status)
		leave_group(group, req->func, leave_mask);

	if (!req->clean)
		send_reply_to_slave(req->func, group, &req->sa_mad, status);
	--group->func[req->func].num_pend_reqs;
	list_del(&req->group_list);
	list_del(&req->func_list);
	kfree(req);
	return 1;
}

static int handle_join_req(struct mcast_group *group, u8 join_mask,
			   struct mcast_req *req)
{
	u8 group_join_state = group->rec.scope_join_state & 0xf;
	int ref = 0;
	u16 status;
	struct ib_sa_mcmember_data *sa_data = (struct ib_sa_mcmember_data *)req->sa_mad.data;

	if (join_mask == (group_join_state & join_mask)) {
		/* port's membership need not change */
		status = cmp_rec(&group->rec, sa_data, req->sa_mad.sa_hdr.comp_mask);
		if (!status)
			join_group(group, req->func, join_mask);

		--group->func[req->func].num_pend_reqs;
		send_reply_to_slave(req->func, group, &req->sa_mad, status);
		list_del(&req->group_list);
		list_del(&req->func_list);
		kfree(req);
		++ref;
	} else {
		/* port's membership needs to be updated */
		group->prev_state = group->state;
		if (send_join_to_wire(group, &req->sa_mad)) {
			--group->func[req->func].num_pend_reqs;
			list_del(&req->group_list);
			list_del(&req->func_list);
			kfree(req);
			ref = 1;
			group->state = group->prev_state;
		} else
			group->state = MCAST_JOIN_SENT;
	}

	return ref;
}

static void mlx4_ib_mcg_work_handler(struct work_struct *work)
{
	struct mcast_group *group;
	struct mcast_req *req = NULL;
	struct ib_sa_mcmember_data *sa_data;
	u8 req_join_state;
	int rc = 1; /* release_count - this is for the scheduled work */
	u16 status;
	u8 method;

	group = container_of(work, typeof(*group), work);

	mutex_lock(&group->lock);

	/* First, let's see if a response from SM is waiting regarding this group.
	 * If so, we need to update the group's REC. If this is a bad response, we
	 * may need to send a bad response to a VF waiting for it. If VF is waiting
	 * and this is a good response, the VF will be answered later in this func. */
	if (group->state == MCAST_RESP_READY) {
		/* cancels mlx4_ib_mcg_timeout_handler */
		cancel_delayed_work(&group->timeout_work);
		status = be16_to_cpu(group->response_sa_mad.mad_hdr.status);
		method = group->response_sa_mad.mad_hdr.method;
		if (group->last_req_tid != group->response_sa_mad.mad_hdr.tid) {
			mcg_warn_group(group, "Got MAD response to existing MGID but wrong TID, dropping. Resp TID=%llx, group TID=%llx\n",
				be64_to_cpu(group->response_sa_mad.mad_hdr.tid),
				be64_to_cpu(group->last_req_tid));
			group->state = group->prev_state;
			goto process_requests;
		}
		if (status) {
			if (!list_empty(&group->pending_list))
				req = list_first_entry(&group->pending_list,
						struct mcast_req, group_list);
			if (method == IB_MGMT_METHOD_GET_RESP) {
					if (req) {
						send_reply_to_slave(req->func, group, &req->sa_mad, status);
						--group->func[req->func].num_pend_reqs;
						list_del(&req->group_list);
						list_del(&req->func_list);
						kfree(req);
						++rc;
					} else
						mcg_warn_group(group, "no request for failed join\n");
			} else if (method == IB_SA_METHOD_DELETE_RESP && group->demux->flushing)
				++rc;
		} else {
			u8 resp_join_state;
			u8 cur_join_state;

			resp_join_state = ((struct ib_sa_mcmember_data *)
						group->response_sa_mad.data)->scope_join_state & 0xf;
			cur_join_state = group->rec.scope_join_state & 0xf;

			if (method == IB_MGMT_METHOD_GET_RESP) {
				/* successfull join */
				if (!cur_join_state && resp_join_state)
					--rc;
			} else if (!resp_join_state)
					++rc;
			memcpy(&group->rec, group->response_sa_mad.data, sizeof group->rec);
		}
		group->state = MCAST_IDLE;
	}

process_requests:
	/* We should now go over pending join/leave requests, as long as we are idle. */
	while (!list_empty(&group->pending_list) && group->state == MCAST_IDLE) {
		req = list_first_entry(&group->pending_list, struct mcast_req,
				       group_list);
		sa_data = (struct ib_sa_mcmember_data *)req->sa_mad.data;
		req_join_state = sa_data->scope_join_state & 0xf;

		/* For a leave request, we will immediately answer the VF, and
		 * update our internal counters. The actual leave will be sent
		 * to SM later, if at all needed. We dequeue the request now. */
		if (req->sa_mad.mad_hdr.method == IB_SA_METHOD_DELETE)
			rc += handle_leave_req(group, req_join_state, req);
		else
			rc += handle_join_req(group, req_join_state, req);
	}

	/* Handle leaves */
	if (group->state == MCAST_IDLE) {
		req_join_state = get_leave_state(group);
		if (req_join_state) {
			group->rec.scope_join_state &= ~req_join_state;
			group->prev_state = group->state;
			if (send_leave_to_wire(group, req_join_state)) {
				group->state = group->prev_state;
				++rc;
			} else
				group->state = MCAST_LEAVE_SENT;
		}
	}

	if (!list_empty(&group->pending_list) && group->state == MCAST_IDLE)
		goto process_requests;
	mutex_unlock(&group->lock);

	while (rc--)
		release_group(group, 0);
}

static struct mcast_group *search_relocate_mgid0_group(struct mlx4_ib_demux_ctx *ctx,
						       __be64 tid,
						       union ib_gid *new_mgid)
{
	struct mcast_group *group = NULL, *cur_group, *n;
	struct mcast_req *req;

	mutex_lock(&ctx->mcg_table_lock);
	list_for_each_entry_safe(group, n, &ctx->mcg_mgid0_list, mgid0_list) {
		mutex_lock(&group->lock);
		if (group->last_req_tid == tid) {
			if (memcmp(new_mgid, &mgid0, sizeof mgid0)) {
				group->rec.mgid = *new_mgid;
				sprintf(group->name, "%016llx%016llx",
						be64_to_cpu(group->rec.mgid.global.subnet_prefix),
						be64_to_cpu(group->rec.mgid.global.interface_id));
				list_del_init(&group->mgid0_list);
				cur_group = mcast_insert(ctx, group);
				if (cur_group) {
					/* A race between our code and SM. Silently cleaning the new one */
					req = list_first_entry(&group->pending_list,
							       struct mcast_req, group_list);
					--group->func[req->func].num_pend_reqs;
					list_del(&req->group_list);
					list_del(&req->func_list);
					kfree(req);
					mutex_unlock(&group->lock);
					mutex_unlock(&ctx->mcg_table_lock);
					release_group(group, 0);
					return NULL;
				}

				atomic_inc(&group->refcount);
				add_sysfs_port_mcg_attr(ctx->dev, ctx->port, &group->dentry.attr);
				mutex_unlock(&group->lock);
				mutex_unlock(&ctx->mcg_table_lock);
				return group;
			} else {
				struct mcast_req *tmp1, *tmp2;

				list_del(&group->mgid0_list);
				if (!list_empty(&group->pending_list) && group->state != MCAST_IDLE)
					cancel_delayed_work_sync(&group->timeout_work);

				list_for_each_entry_safe(tmp1, tmp2, &group->pending_list, group_list) {
					list_del(&tmp1->group_list);
					kfree(tmp1);
				}
				mutex_unlock(&group->lock);
				mutex_unlock(&ctx->mcg_table_lock);
				kfree(group);
				return NULL;
			}
		}
		mutex_unlock(&group->lock);
	}
	mutex_unlock(&ctx->mcg_table_lock);

	return NULL;
}

static ssize_t sysfs_show_group(struct device *dev,
		struct device_attribute *attr, char *buf);

static struct mcast_group *acquire_group(struct mlx4_ib_demux_ctx *ctx,
					 union ib_gid *mgid, int create)
{
	struct mcast_group *group, *cur_group;
	int is_mgid0;
	int i;

	is_mgid0 = !memcmp(&mgid0, mgid, sizeof mgid0);
	if (!is_mgid0) {
		group = mcast_find(ctx, mgid);
		if (group)
			goto found;
	}

	if (!create)
		return ERR_PTR(-ENOENT);

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	group->demux = ctx;
	group->rec.mgid = *mgid;
	INIT_LIST_HEAD(&group->pending_list);
	INIT_LIST_HEAD(&group->mgid0_list);
	for (i = 0; i < MAX_VFS; ++i)
		INIT_LIST_HEAD(&group->func[i].pending);
	INIT_WORK(&group->work, mlx4_ib_mcg_work_handler);
	INIT_DELAYED_WORK(&group->timeout_work, mlx4_ib_mcg_timeout_handler);
	mutex_init(&group->lock);
	sprintf(group->name, "%016llx%016llx",
			be64_to_cpu(group->rec.mgid.global.subnet_prefix),
			be64_to_cpu(group->rec.mgid.global.interface_id));
	sysfs_attr_init(&group->dentry.attr);
	group->dentry.show = sysfs_show_group;
	group->dentry.store = NULL;
	group->dentry.attr.name = group->name;
	group->dentry.attr.mode = 0400;
	group->state = MCAST_IDLE;

	if (is_mgid0) {
		list_add(&group->mgid0_list, &ctx->mcg_mgid0_list);
		goto found;
	}

	cur_group = mcast_insert(ctx, group);
	if (cur_group) {
		mcg_warn("group just showed up %s - confused\n", cur_group->name);
		kfree(group);
		return ERR_PTR(-EINVAL);
	}

	add_sysfs_port_mcg_attr(ctx->dev, ctx->port, &group->dentry.attr);

found:
	atomic_inc(&group->refcount);
	return group;
}

static void queue_req(struct mcast_req *req)
{
	struct mcast_group *group = req->group;

	atomic_inc(&group->refcount); /* for the request */
	atomic_inc(&group->refcount); /* for scheduling the work */
	list_add_tail(&req->group_list, &group->pending_list);
	list_add_tail(&req->func_list, &group->func[req->func].pending);
	/* calls mlx4_ib_mcg_work_handler */
	if (!queue_work(group->demux->mcg_wq, &group->work))
		safe_atomic_dec(&group->refcount);
}

int mlx4_ib_mcg_demux_handler(struct ib_device *ibdev, int port, int slave,
			      struct ib_sa_mad *mad)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct ib_sa_mcmember_data *rec = (struct ib_sa_mcmember_data *)mad->data;
	struct mlx4_ib_demux_ctx *ctx = &dev->sriov.demux[port - 1];
	struct mcast_group *group;

	switch (mad->mad_hdr.method) {
	case IB_MGMT_METHOD_GET_RESP:
	case IB_SA_METHOD_DELETE_RESP:
		mutex_lock(&ctx->mcg_table_lock);
		group = acquire_group(ctx, &rec->mgid, 0);
		mutex_unlock(&ctx->mcg_table_lock);
		if (IS_ERR(group)) {
			if (mad->mad_hdr.method == IB_MGMT_METHOD_GET_RESP) {
				__be64 tid = mad->mad_hdr.tid;
				*(u8 *)(&tid) = (u8)slave; /* in group we kept the modified TID */
				group = search_relocate_mgid0_group(ctx, tid, &rec->mgid);
			} else
				group = NULL;
		}

		if (!group)
			return 1;

		mutex_lock(&group->lock);
		group->response_sa_mad = *mad;
		group->prev_state = group->state;
		group->state = MCAST_RESP_READY;
		/* calls mlx4_ib_mcg_work_handler */
		atomic_inc(&group->refcount);
		if (!queue_work(ctx->mcg_wq, &group->work))
			safe_atomic_dec(&group->refcount);
		mutex_unlock(&group->lock);
		release_group(group, 0);
		return 1; /* consumed */
	case IB_MGMT_METHOD_SET:
	case IB_SA_METHOD_GET_TABLE:
	case IB_SA_METHOD_GET_TABLE_RESP:
	case IB_SA_METHOD_DELETE:
		return 0; /* not consumed, pass-through to guest over tunnel */
	default:
		mcg_warn("In demux, port %d: unexpected MCMember method: 0x%x, dropping\n",
			port, mad->mad_hdr.method);
		return 1; /* consumed */
	}
}

int mlx4_ib_mcg_multiplex_handler(struct ib_device *ibdev, int port,
				  int slave, struct ib_sa_mad *sa_mad)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct ib_sa_mcmember_data *rec = (struct ib_sa_mcmember_data *)sa_mad->data;
	struct mlx4_ib_demux_ctx *ctx = &dev->sriov.demux[port - 1];
	struct mcast_group *group;
	struct mcast_req *req;
	int may_create = 0;

	if (ctx->flushing)
		return -EAGAIN;

	switch (sa_mad->mad_hdr.method) {
	case IB_MGMT_METHOD_SET:
		may_create = 1;
		fallthrough;
	case IB_SA_METHOD_DELETE:
		req = kzalloc(sizeof *req, GFP_KERNEL);
		if (!req)
			return -ENOMEM;

		req->func = slave;
		req->sa_mad = *sa_mad;

		mutex_lock(&ctx->mcg_table_lock);
		group = acquire_group(ctx, &rec->mgid, may_create);
		mutex_unlock(&ctx->mcg_table_lock);
		if (IS_ERR(group)) {
			kfree(req);
			return PTR_ERR(group);
		}
		mutex_lock(&group->lock);
		if (group->func[slave].num_pend_reqs > MAX_PEND_REQS_PER_FUNC) {
			mutex_unlock(&group->lock);
			mcg_debug_group(group, "Port %d, Func %d has too many pending requests (%d), dropping\n",
					port, slave, MAX_PEND_REQS_PER_FUNC);
			release_group(group, 0);
			kfree(req);
			return -ENOMEM;
		}
		++group->func[slave].num_pend_reqs;
		req->group = group;
		queue_req(req);
		mutex_unlock(&group->lock);
		release_group(group, 0);
		return 1; /* consumed */
	case IB_SA_METHOD_GET_TABLE:
	case IB_MGMT_METHOD_GET_RESP:
	case IB_SA_METHOD_GET_TABLE_RESP:
	case IB_SA_METHOD_DELETE_RESP:
		return 0; /* not consumed, pass-through */
	default:
		mcg_warn("In multiplex, port %d, func %d: unexpected MCMember method: 0x%x, dropping\n",
			port, slave, sa_mad->mad_hdr.method);
		return 1; /* consumed */
	}
}

static ssize_t sysfs_show_group(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mcast_group *group =
		container_of(attr, struct mcast_group, dentry);
	struct mcast_req *req = NULL;
	char state_str[40];
	char pending_str[40];
	int len;
	int i;
	u32 hoplimit;

	if (group->state == MCAST_IDLE)
		scnprintf(state_str, sizeof(state_str), "%s",
			  get_state_string(group->state));
	else
		scnprintf(state_str, sizeof(state_str), "%s(TID=0x%llx)",
			  get_state_string(group->state),
			  be64_to_cpu(group->last_req_tid));

	if (list_empty(&group->pending_list)) {
		scnprintf(pending_str, sizeof(pending_str), "No");
	} else {
		req = list_first_entry(&group->pending_list, struct mcast_req,
				       group_list);
		scnprintf(pending_str, sizeof(pending_str), "Yes(TID=0x%llx)",
			  be64_to_cpu(req->sa_mad.mad_hdr.tid));
	}

	len = sysfs_emit(buf, "%1d [%02d,%02d,%02d] %4d %4s %5s     ",
			 group->rec.scope_join_state & 0xf,
			 group->members[2],
			 group->members[1],
			 group->members[0],
			 atomic_read(&group->refcount),
			 pending_str,
			 state_str);

	for (i = 0; i < MAX_VFS; i++) {
		if (group->func[i].state == MCAST_MEMBER)
			len += sysfs_emit_at(buf, len, "%d[%1x] ", i,
					     group->func[i].join_state);
	}

	hoplimit = be32_to_cpu(group->rec.sl_flowlabel_hoplimit);
	len += sysfs_emit_at(buf, len,
			     "\t\t(%4hx %4x %2x %2x %2x %2x %2x %4x %4x %2x %2x)\n",
			     be16_to_cpu(group->rec.pkey),
			     be32_to_cpu(group->rec.qkey),
			     (group->rec.mtusel_mtu & 0xc0) >> 6,
			     (group->rec.mtusel_mtu & 0x3f),
			     group->rec.tclass,
			     (group->rec.ratesel_rate & 0xc0) >> 6,
			     (group->rec.ratesel_rate & 0x3f),
			     (hoplimit & 0xf0000000) >> 28,
			     (hoplimit & 0x0fffff00) >> 8,
			     (hoplimit & 0x000000ff),
			     group->rec.proxy_join);

	return len;
}

int mlx4_ib_mcg_port_init(struct mlx4_ib_demux_ctx *ctx)
{
	char name[20];

	atomic_set(&ctx->tid, 0);
	sprintf(name, "mlx4_ib_mcg%d", ctx->port);
	ctx->mcg_wq = alloc_ordered_workqueue(name, WQ_MEM_RECLAIM);
	if (!ctx->mcg_wq)
		return -ENOMEM;

	mutex_init(&ctx->mcg_table_lock);
	ctx->mcg_table = RB_ROOT;
	INIT_LIST_HEAD(&ctx->mcg_mgid0_list);
	ctx->flushing = 0;

	return 0;
}

static void force_clean_group(struct mcast_group *group)
{
	struct mcast_req *req, *tmp
		;
	list_for_each_entry_safe(req, tmp, &group->pending_list, group_list) {
		list_del(&req->group_list);
		kfree(req);
	}
	del_sysfs_port_mcg_attr(group->demux->dev, group->demux->port, &group->dentry.attr);
	rb_erase(&group->node, &group->demux->mcg_table);
	kfree(group);
}

static void _mlx4_ib_mcg_port_cleanup(struct mlx4_ib_demux_ctx *ctx, int destroy_wq)
{
	int i;
	struct rb_node *p;
	struct mcast_group *group;
	unsigned long end;
	int count;

	for (i = 0; i < MAX_VFS; ++i)
		clean_vf_mcast(ctx, i);

	end = jiffies + msecs_to_jiffies(MAD_TIMEOUT_MS + 3000);
	do {
		count = 0;
		mutex_lock(&ctx->mcg_table_lock);
		for (p = rb_first(&ctx->mcg_table); p; p = rb_next(p))
			++count;
		mutex_unlock(&ctx->mcg_table_lock);
		if (!count)
			break;

		usleep_range(1000, 2000);
	} while (time_after(end, jiffies));

	flush_workqueue(ctx->mcg_wq);
	if (destroy_wq)
		destroy_workqueue(ctx->mcg_wq);

	mutex_lock(&ctx->mcg_table_lock);
	while ((p = rb_first(&ctx->mcg_table)) != NULL) {
		group = rb_entry(p, struct mcast_group, node);
		if (atomic_read(&group->refcount))
			mcg_debug_group(group, "group refcount %d!!! (pointer %p)\n",
					atomic_read(&group->refcount), group);

		force_clean_group(group);
	}
	mutex_unlock(&ctx->mcg_table_lock);
}

struct clean_work {
	struct work_struct work;
	struct mlx4_ib_demux_ctx *ctx;
	int destroy_wq;
};

static void mcg_clean_task(struct work_struct *work)
{
	struct clean_work *cw = container_of(work, struct clean_work, work);

	_mlx4_ib_mcg_port_cleanup(cw->ctx, cw->destroy_wq);
	cw->ctx->flushing = 0;
	kfree(cw);
}

void mlx4_ib_mcg_port_cleanup(struct mlx4_ib_demux_ctx *ctx, int destroy_wq)
{
	struct clean_work *work;

	if (ctx->flushing)
		return;

	ctx->flushing = 1;

	if (destroy_wq) {
		_mlx4_ib_mcg_port_cleanup(ctx, destroy_wq);
		ctx->flushing = 0;
		return;
	}

	work = kmalloc(sizeof *work, GFP_KERNEL);
	if (!work) {
		ctx->flushing = 0;
		return;
	}

	work->ctx = ctx;
	work->destroy_wq = destroy_wq;
	INIT_WORK(&work->work, mcg_clean_task);
	queue_work(clean_wq, &work->work);
}

static void build_leave_mad(struct mcast_req *req)
{
	struct ib_sa_mad *mad = &req->sa_mad;

	mad->mad_hdr.method = IB_SA_METHOD_DELETE;
}


static void clear_pending_reqs(struct mcast_group *group, int vf)
{
	struct mcast_req *req, *tmp, *group_first = NULL;
	int clear;
	int pend = 0;

	if (!list_empty(&group->pending_list))
		group_first = list_first_entry(&group->pending_list, struct mcast_req, group_list);

	list_for_each_entry_safe(req, tmp, &group->func[vf].pending, func_list) {
		clear = 1;
		if (group_first == req &&
		    (group->state == MCAST_JOIN_SENT ||
		     group->state == MCAST_LEAVE_SENT)) {
			clear = cancel_delayed_work(&group->timeout_work);
			pend = !clear;
			group->state = MCAST_IDLE;
		}
		if (clear) {
			--group->func[vf].num_pend_reqs;
			list_del(&req->group_list);
			list_del(&req->func_list);
			kfree(req);
			atomic_dec(&group->refcount);
		}
	}

	if (!pend && (!list_empty(&group->func[vf].pending) || group->func[vf].num_pend_reqs)) {
		mcg_warn_group(group, "DRIVER BUG: list_empty %d, num_pend_reqs %d\n",
			       list_empty(&group->func[vf].pending), group->func[vf].num_pend_reqs);
	}
}

static int push_deleteing_req(struct mcast_group *group, int slave)
{
	struct mcast_req *req;
	struct mcast_req *pend_req;

	if (!group->func[slave].join_state)
		return 0;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	if (!list_empty(&group->func[slave].pending)) {
		pend_req = list_entry(group->func[slave].pending.prev, struct mcast_req, group_list);
		if (pend_req->clean) {
			kfree(req);
			return 0;
		}
	}

	req->clean = 1;
	req->func = slave;
	req->group = group;
	++group->func[slave].num_pend_reqs;
	build_leave_mad(req);
	queue_req(req);
	return 0;
}

void clean_vf_mcast(struct mlx4_ib_demux_ctx *ctx, int slave)
{
	struct mcast_group *group;
	struct rb_node *p;

	mutex_lock(&ctx->mcg_table_lock);
	for (p = rb_first(&ctx->mcg_table); p; p = rb_next(p)) {
		group = rb_entry(p, struct mcast_group, node);
		mutex_lock(&group->lock);
		if (atomic_read(&group->refcount)) {
			/* clear pending requests of this VF */
			clear_pending_reqs(group, slave);
			push_deleteing_req(group, slave);
		}
		mutex_unlock(&group->lock);
	}
	mutex_unlock(&ctx->mcg_table_lock);
}


int mlx4_ib_mcg_init(void)
{
	clean_wq = alloc_ordered_workqueue("mlx4_ib_mcg", WQ_MEM_RECLAIM);
	if (!clean_wq)
		return -ENOMEM;

	return 0;
}

void mlx4_ib_mcg_destroy(void)
{
	destroy_workqueue(clean_wq);
}
