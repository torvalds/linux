/*
 * Copyright (c) 2005-2006 Intel Corporation.  All rights reserved.
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
 *	copyright notice, this list of conditions and the following
 *	disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials
 *	provided with the distribution.
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
#include <linux/file.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/idr.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/module.h>

#include <rdma/rdma_user_cm.h>
#include <rdma/ib_marshall.h>
#include <rdma/rdma_cm.h>
#include <rdma/rdma_cm_ib.h>
#include <rdma/ib_addr.h>
#include <rdma/ib.h>

MODULE_AUTHOR("Sean Hefty");
MODULE_DESCRIPTION("RDMA Userspace Connection Manager Access");
MODULE_LICENSE("Dual BSD/GPL");

static unsigned int max_backlog = 1024;

static struct ctl_table_header *ucma_ctl_table_hdr;
static ctl_table ucma_ctl_table[] = {
	{
		.procname	= "max_backlog",
		.data		= &max_backlog,
		.maxlen		= sizeof max_backlog,
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

struct ucma_file {
	struct mutex		mut;
	struct file		*filp;
	struct list_head	ctx_list;
	struct list_head	event_list;
	wait_queue_head_t	poll_wait;
};

struct ucma_context {
	int			id;
	struct completion	comp;
	atomic_t		ref;
	int			events_reported;
	int			backlog;

	struct ucma_file	*file;
	struct rdma_cm_id	*cm_id;
	u64			uid;

	struct list_head	list;
	struct list_head	mc_list;
};

struct ucma_multicast {
	struct ucma_context	*ctx;
	int			id;
	int			events_reported;

	u64			uid;
	struct list_head	list;
	struct sockaddr_storage	addr;
};

struct ucma_event {
	struct ucma_context	*ctx;
	struct ucma_multicast	*mc;
	struct list_head	list;
	struct rdma_cm_id	*cm_id;
	struct rdma_ucm_event_resp resp;
};

static DEFINE_MUTEX(mut);
static DEFINE_IDR(ctx_idr);
static DEFINE_IDR(multicast_idr);

static inline struct ucma_context *_ucma_find_context(int id,
						      struct ucma_file *file)
{
	struct ucma_context *ctx;

	ctx = idr_find(&ctx_idr, id);
	if (!ctx)
		ctx = ERR_PTR(-ENOENT);
	else if (ctx->file != file)
		ctx = ERR_PTR(-EINVAL);
	return ctx;
}

static struct ucma_context *ucma_get_ctx(struct ucma_file *file, int id)
{
	struct ucma_context *ctx;

	mutex_lock(&mut);
	ctx = _ucma_find_context(id, file);
	if (!IS_ERR(ctx))
		atomic_inc(&ctx->ref);
	mutex_unlock(&mut);
	return ctx;
}

static void ucma_put_ctx(struct ucma_context *ctx)
{
	if (atomic_dec_and_test(&ctx->ref))
		complete(&ctx->comp);
}

static struct ucma_context *ucma_alloc_ctx(struct ucma_file *file)
{
	struct ucma_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	atomic_set(&ctx->ref, 1);
	init_completion(&ctx->comp);
	INIT_LIST_HEAD(&ctx->mc_list);
	ctx->file = file;

	mutex_lock(&mut);
	ctx->id = idr_alloc(&ctx_idr, ctx, 0, 0, GFP_KERNEL);
	mutex_unlock(&mut);
	if (ctx->id < 0)
		goto error;

	list_add_tail(&ctx->list, &file->ctx_list);
	return ctx;

error:
	kfree(ctx);
	return NULL;
}

static struct ucma_multicast* ucma_alloc_multicast(struct ucma_context *ctx)
{
	struct ucma_multicast *mc;

	mc = kzalloc(sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return NULL;

	mutex_lock(&mut);
	mc->id = idr_alloc(&multicast_idr, mc, 0, 0, GFP_KERNEL);
	mutex_unlock(&mut);
	if (mc->id < 0)
		goto error;

	mc->ctx = ctx;
	list_add_tail(&mc->list, &ctx->mc_list);
	return mc;

error:
	kfree(mc);
	return NULL;
}

static void ucma_copy_conn_event(struct rdma_ucm_conn_param *dst,
				 struct rdma_conn_param *src)
{
	if (src->private_data_len)
		memcpy(dst->private_data, src->private_data,
		       src->private_data_len);
	dst->private_data_len = src->private_data_len;
	dst->responder_resources =src->responder_resources;
	dst->initiator_depth = src->initiator_depth;
	dst->flow_control = src->flow_control;
	dst->retry_count = src->retry_count;
	dst->rnr_retry_count = src->rnr_retry_count;
	dst->srq = src->srq;
	dst->qp_num = src->qp_num;
}

static void ucma_copy_ud_event(struct rdma_ucm_ud_param *dst,
			       struct rdma_ud_param *src)
{
	if (src->private_data_len)
		memcpy(dst->private_data, src->private_data,
		       src->private_data_len);
	dst->private_data_len = src->private_data_len;
	ib_copy_ah_attr_to_user(&dst->ah_attr, &src->ah_attr);
	dst->qp_num = src->qp_num;
	dst->qkey = src->qkey;
}

static void ucma_set_event_context(struct ucma_context *ctx,
				   struct rdma_cm_event *event,
				   struct ucma_event *uevent)
{
	uevent->ctx = ctx;
	switch (event->event) {
	case RDMA_CM_EVENT_MULTICAST_JOIN:
	case RDMA_CM_EVENT_MULTICAST_ERROR:
		uevent->mc = (struct ucma_multicast *)
			     event->param.ud.private_data;
		uevent->resp.uid = uevent->mc->uid;
		uevent->resp.id = uevent->mc->id;
		break;
	default:
		uevent->resp.uid = ctx->uid;
		uevent->resp.id = ctx->id;
		break;
	}
}

static int ucma_event_handler(struct rdma_cm_id *cm_id,
			      struct rdma_cm_event *event)
{
	struct ucma_event *uevent;
	struct ucma_context *ctx = cm_id->context;
	int ret = 0;

	uevent = kzalloc(sizeof(*uevent), GFP_KERNEL);
	if (!uevent)
		return event->event == RDMA_CM_EVENT_CONNECT_REQUEST;

	mutex_lock(&ctx->file->mut);
	uevent->cm_id = cm_id;
	ucma_set_event_context(ctx, event, uevent);
	uevent->resp.event = event->event;
	uevent->resp.status = event->status;
	if (cm_id->qp_type == IB_QPT_UD)
		ucma_copy_ud_event(&uevent->resp.param.ud, &event->param.ud);
	else
		ucma_copy_conn_event(&uevent->resp.param.conn,
				     &event->param.conn);

	if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
		if (!ctx->backlog) {
			ret = -ENOMEM;
			kfree(uevent);
			goto out;
		}
		ctx->backlog--;
	} else if (!ctx->uid) {
		/*
		 * We ignore events for new connections until userspace has set
		 * their context.  This can only happen if an error occurs on a
		 * new connection before the user accepts it.  This is okay,
		 * since the accept will just fail later.
		 */
		kfree(uevent);
		goto out;
	}

	list_add_tail(&uevent->list, &ctx->file->event_list);
	wake_up_interruptible(&ctx->file->poll_wait);
out:
	mutex_unlock(&ctx->file->mut);
	return ret;
}

static ssize_t ucma_get_event(struct ucma_file *file, const char __user *inbuf,
			      int in_len, int out_len)
{
	struct ucma_context *ctx;
	struct rdma_ucm_get_event cmd;
	struct ucma_event *uevent;
	int ret = 0;

	if (out_len < sizeof uevent->resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	mutex_lock(&file->mut);
	while (list_empty(&file->event_list)) {
		mutex_unlock(&file->mut);

		if (file->filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(file->poll_wait,
					     !list_empty(&file->event_list)))
			return -ERESTARTSYS;

		mutex_lock(&file->mut);
	}

	uevent = list_entry(file->event_list.next, struct ucma_event, list);

	if (uevent->resp.event == RDMA_CM_EVENT_CONNECT_REQUEST) {
		ctx = ucma_alloc_ctx(file);
		if (!ctx) {
			ret = -ENOMEM;
			goto done;
		}
		uevent->ctx->backlog++;
		ctx->cm_id = uevent->cm_id;
		ctx->cm_id->context = ctx;
		uevent->resp.id = ctx->id;
	}

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &uevent->resp, sizeof uevent->resp)) {
		ret = -EFAULT;
		goto done;
	}

	list_del(&uevent->list);
	uevent->ctx->events_reported++;
	if (uevent->mc)
		uevent->mc->events_reported++;
	kfree(uevent);
done:
	mutex_unlock(&file->mut);
	return ret;
}

static int ucma_get_qp_type(struct rdma_ucm_create_id *cmd, enum ib_qp_type *qp_type)
{
	switch (cmd->ps) {
	case RDMA_PS_TCP:
		*qp_type = IB_QPT_RC;
		return 0;
	case RDMA_PS_UDP:
	case RDMA_PS_IPOIB:
		*qp_type = IB_QPT_UD;
		return 0;
	case RDMA_PS_IB:
		*qp_type = cmd->qp_type;
		return 0;
	default:
		return -EINVAL;
	}
}

static ssize_t ucma_create_id(struct ucma_file *file, const char __user *inbuf,
			      int in_len, int out_len)
{
	struct rdma_ucm_create_id cmd;
	struct rdma_ucm_create_id_resp resp;
	struct ucma_context *ctx;
	enum ib_qp_type qp_type;
	int ret;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ret = ucma_get_qp_type(&cmd, &qp_type);
	if (ret)
		return ret;

	mutex_lock(&file->mut);
	ctx = ucma_alloc_ctx(file);
	mutex_unlock(&file->mut);
	if (!ctx)
		return -ENOMEM;

	ctx->uid = cmd.uid;
	ctx->cm_id = rdma_create_id(ucma_event_handler, ctx, cmd.ps, qp_type);
	if (IS_ERR(ctx->cm_id)) {
		ret = PTR_ERR(ctx->cm_id);
		goto err1;
	}

	resp.id = ctx->id;
	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp))) {
		ret = -EFAULT;
		goto err2;
	}
	return 0;

err2:
	rdma_destroy_id(ctx->cm_id);
err1:
	mutex_lock(&mut);
	idr_remove(&ctx_idr, ctx->id);
	mutex_unlock(&mut);
	kfree(ctx);
	return ret;
}

static void ucma_cleanup_multicast(struct ucma_context *ctx)
{
	struct ucma_multicast *mc, *tmp;

	mutex_lock(&mut);
	list_for_each_entry_safe(mc, tmp, &ctx->mc_list, list) {
		list_del(&mc->list);
		idr_remove(&multicast_idr, mc->id);
		kfree(mc);
	}
	mutex_unlock(&mut);
}

static void ucma_cleanup_mc_events(struct ucma_multicast *mc)
{
	struct ucma_event *uevent, *tmp;

	list_for_each_entry_safe(uevent, tmp, &mc->ctx->file->event_list, list) {
		if (uevent->mc != mc)
			continue;

		list_del(&uevent->list);
		kfree(uevent);
	}
}

/*
 * We cannot hold file->mut when calling rdma_destroy_id() or we can
 * deadlock.  We also acquire file->mut in ucma_event_handler(), and
 * rdma_destroy_id() will wait until all callbacks have completed.
 */
static int ucma_free_ctx(struct ucma_context *ctx)
{
	int events_reported;
	struct ucma_event *uevent, *tmp;
	LIST_HEAD(list);

	/* No new events will be generated after destroying the id. */
	rdma_destroy_id(ctx->cm_id);

	ucma_cleanup_multicast(ctx);

	/* Cleanup events not yet reported to the user. */
	mutex_lock(&ctx->file->mut);
	list_for_each_entry_safe(uevent, tmp, &ctx->file->event_list, list) {
		if (uevent->ctx == ctx)
			list_move_tail(&uevent->list, &list);
	}
	list_del(&ctx->list);
	mutex_unlock(&ctx->file->mut);

	list_for_each_entry_safe(uevent, tmp, &list, list) {
		list_del(&uevent->list);
		if (uevent->resp.event == RDMA_CM_EVENT_CONNECT_REQUEST)
			rdma_destroy_id(uevent->cm_id);
		kfree(uevent);
	}

	events_reported = ctx->events_reported;
	kfree(ctx);
	return events_reported;
}

static ssize_t ucma_destroy_id(struct ucma_file *file, const char __user *inbuf,
			       int in_len, int out_len)
{
	struct rdma_ucm_destroy_id cmd;
	struct rdma_ucm_destroy_id_resp resp;
	struct ucma_context *ctx;
	int ret = 0;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	mutex_lock(&mut);
	ctx = _ucma_find_context(cmd.id, file);
	if (!IS_ERR(ctx))
		idr_remove(&ctx_idr, ctx->id);
	mutex_unlock(&mut);

	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ucma_put_ctx(ctx);
	wait_for_completion(&ctx->comp);
	resp.events_reported = ucma_free_ctx(ctx);

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		ret = -EFAULT;

	return ret;
}

static ssize_t ucma_bind_ip(struct ucma_file *file, const char __user *inbuf,
			      int in_len, int out_len)
{
	struct rdma_ucm_bind_ip cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = rdma_bind_addr(ctx->cm_id, (struct sockaddr *) &cmd.addr);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_bind(struct ucma_file *file, const char __user *inbuf,
			 int in_len, int out_len)
{
	struct rdma_ucm_bind cmd;
	struct sockaddr *addr;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	addr = (struct sockaddr *) &cmd.addr;
	if (cmd.reserved || !cmd.addr_size || (cmd.addr_size != rdma_addr_size(addr)))
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = rdma_bind_addr(ctx->cm_id, addr);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_resolve_ip(struct ucma_file *file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	struct rdma_ucm_resolve_ip cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = rdma_resolve_addr(ctx->cm_id, (struct sockaddr *) &cmd.src_addr,
				(struct sockaddr *) &cmd.dst_addr,
				cmd.timeout_ms);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_resolve_addr(struct ucma_file *file,
				 const char __user *inbuf,
				 int in_len, int out_len)
{
	struct rdma_ucm_resolve_addr cmd;
	struct sockaddr *src, *dst;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	src = (struct sockaddr *) &cmd.src_addr;
	dst = (struct sockaddr *) &cmd.dst_addr;
	if (cmd.reserved || (cmd.src_size && (cmd.src_size != rdma_addr_size(src))) ||
	    !cmd.dst_size || (cmd.dst_size != rdma_addr_size(dst)))
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = rdma_resolve_addr(ctx->cm_id, src, dst, cmd.timeout_ms);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_resolve_route(struct ucma_file *file,
				  const char __user *inbuf,
				  int in_len, int out_len)
{
	struct rdma_ucm_resolve_route cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = rdma_resolve_route(ctx->cm_id, cmd.timeout_ms);
	ucma_put_ctx(ctx);
	return ret;
}

static void ucma_copy_ib_route(struct rdma_ucm_query_route_resp *resp,
			       struct rdma_route *route)
{
	struct rdma_dev_addr *dev_addr;

	resp->num_paths = route->num_paths;
	switch (route->num_paths) {
	case 0:
		dev_addr = &route->addr.dev_addr;
		rdma_addr_get_dgid(dev_addr,
				   (union ib_gid *) &resp->ib_route[0].dgid);
		rdma_addr_get_sgid(dev_addr,
				   (union ib_gid *) &resp->ib_route[0].sgid);
		resp->ib_route[0].pkey = cpu_to_be16(ib_addr_get_pkey(dev_addr));
		break;
	case 2:
		ib_copy_path_rec_to_user(&resp->ib_route[1],
					 &route->path_rec[1]);
		/* fall through */
	case 1:
		ib_copy_path_rec_to_user(&resp->ib_route[0],
					 &route->path_rec[0]);
		break;
	default:
		break;
	}
}

static void ucma_copy_iboe_route(struct rdma_ucm_query_route_resp *resp,
				 struct rdma_route *route)
{
	struct rdma_dev_addr *dev_addr;
	struct net_device *dev;
	u16 vid = 0;

	resp->num_paths = route->num_paths;
	switch (route->num_paths) {
	case 0:
		dev_addr = &route->addr.dev_addr;
		dev = dev_get_by_index(&init_net, dev_addr->bound_dev_if);
			if (dev) {
				vid = rdma_vlan_dev_vlan_id(dev);
				dev_put(dev);
			}

		iboe_mac_vlan_to_ll((union ib_gid *) &resp->ib_route[0].dgid,
				    dev_addr->dst_dev_addr, vid);
		iboe_addr_get_sgid(dev_addr,
				   (union ib_gid *) &resp->ib_route[0].sgid);
		resp->ib_route[0].pkey = cpu_to_be16(0xffff);
		break;
	case 2:
		ib_copy_path_rec_to_user(&resp->ib_route[1],
					 &route->path_rec[1]);
		/* fall through */
	case 1:
		ib_copy_path_rec_to_user(&resp->ib_route[0],
					 &route->path_rec[0]);
		break;
	default:
		break;
	}
}

static void ucma_copy_iw_route(struct rdma_ucm_query_route_resp *resp,
			       struct rdma_route *route)
{
	struct rdma_dev_addr *dev_addr;

	dev_addr = &route->addr.dev_addr;
	rdma_addr_get_dgid(dev_addr, (union ib_gid *) &resp->ib_route[0].dgid);
	rdma_addr_get_sgid(dev_addr, (union ib_gid *) &resp->ib_route[0].sgid);
}

static ssize_t ucma_query_route(struct ucma_file *file,
				const char __user *inbuf,
				int in_len, int out_len)
{
	struct rdma_ucm_query cmd;
	struct rdma_ucm_query_route_resp resp;
	struct ucma_context *ctx;
	struct sockaddr *addr;
	int ret = 0;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	memset(&resp, 0, sizeof resp);
	addr = (struct sockaddr *) &ctx->cm_id->route.addr.src_addr;
	memcpy(&resp.src_addr, addr, addr->sa_family == AF_INET ?
				     sizeof(struct sockaddr_in) :
				     sizeof(struct sockaddr_in6));
	addr = (struct sockaddr *) &ctx->cm_id->route.addr.dst_addr;
	memcpy(&resp.dst_addr, addr, addr->sa_family == AF_INET ?
				     sizeof(struct sockaddr_in) :
				     sizeof(struct sockaddr_in6));
	if (!ctx->cm_id->device)
		goto out;

	resp.node_guid = (__force __u64) ctx->cm_id->device->node_guid;
	resp.port_num = ctx->cm_id->port_num;
	switch (rdma_node_get_transport(ctx->cm_id->device->node_type)) {
	case RDMA_TRANSPORT_IB:
		switch (rdma_port_get_link_layer(ctx->cm_id->device,
			ctx->cm_id->port_num)) {
		case IB_LINK_LAYER_INFINIBAND:
			ucma_copy_ib_route(&resp, &ctx->cm_id->route);
			break;
		case IB_LINK_LAYER_ETHERNET:
			ucma_copy_iboe_route(&resp, &ctx->cm_id->route);
			break;
		default:
			break;
		}
		break;
	case RDMA_TRANSPORT_IWARP:
		ucma_copy_iw_route(&resp, &ctx->cm_id->route);
		break;
	default:
		break;
	}

out:
	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		ret = -EFAULT;

	ucma_put_ctx(ctx);
	return ret;
}

static void ucma_query_device_addr(struct rdma_cm_id *cm_id,
				   struct rdma_ucm_query_addr_resp *resp)
{
	if (!cm_id->device)
		return;

	resp->node_guid = (__force __u64) cm_id->device->node_guid;
	resp->port_num = cm_id->port_num;
	resp->pkey = (__force __u16) cpu_to_be16(
		     ib_addr_get_pkey(&cm_id->route.addr.dev_addr));
}

static ssize_t ucma_query_addr(struct ucma_context *ctx,
			       void __user *response, int out_len)
{
	struct rdma_ucm_query_addr_resp resp;
	struct sockaddr *addr;
	int ret = 0;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	memset(&resp, 0, sizeof resp);

	addr = (struct sockaddr *) &ctx->cm_id->route.addr.src_addr;
	resp.src_size = rdma_addr_size(addr);
	memcpy(&resp.src_addr, addr, resp.src_size);

	addr = (struct sockaddr *) &ctx->cm_id->route.addr.dst_addr;
	resp.dst_size = rdma_addr_size(addr);
	memcpy(&resp.dst_addr, addr, resp.dst_size);

	ucma_query_device_addr(ctx->cm_id, &resp);

	if (copy_to_user(response, &resp, sizeof(resp)))
		ret = -EFAULT;

	return ret;
}

static ssize_t ucma_query_path(struct ucma_context *ctx,
			       void __user *response, int out_len)
{
	struct rdma_ucm_query_path_resp *resp;
	int i, ret = 0;

	if (out_len < sizeof(*resp))
		return -ENOSPC;

	resp = kzalloc(out_len, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	resp->num_paths = ctx->cm_id->route.num_paths;
	for (i = 0, out_len -= sizeof(*resp);
	     i < resp->num_paths && out_len > sizeof(struct ib_path_rec_data);
	     i++, out_len -= sizeof(struct ib_path_rec_data)) {

		resp->path_data[i].flags = IB_PATH_GMP | IB_PATH_PRIMARY |
					   IB_PATH_BIDIRECTIONAL;
		ib_sa_pack_path(&ctx->cm_id->route.path_rec[i],
				&resp->path_data[i].path_rec);
	}

	if (copy_to_user(response, resp,
			 sizeof(*resp) + (i * sizeof(struct ib_path_rec_data))))
		ret = -EFAULT;

	kfree(resp);
	return ret;
}

static ssize_t ucma_query_gid(struct ucma_context *ctx,
			      void __user *response, int out_len)
{
	struct rdma_ucm_query_addr_resp resp;
	struct sockaddr_ib *addr;
	int ret = 0;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	memset(&resp, 0, sizeof resp);

	ucma_query_device_addr(ctx->cm_id, &resp);

	addr = (struct sockaddr_ib *) &resp.src_addr;
	resp.src_size = sizeof(*addr);
	if (ctx->cm_id->route.addr.src_addr.ss_family == AF_IB) {
		memcpy(addr, &ctx->cm_id->route.addr.src_addr, resp.src_size);
	} else {
		addr->sib_family = AF_IB;
		addr->sib_pkey = (__force __be16) resp.pkey;
		rdma_addr_get_sgid(&ctx->cm_id->route.addr.dev_addr,
				   (union ib_gid *) &addr->sib_addr);
		addr->sib_sid = rdma_get_service_id(ctx->cm_id, (struct sockaddr *)
						    &ctx->cm_id->route.addr.src_addr);
	}

	addr = (struct sockaddr_ib *) &resp.dst_addr;
	resp.dst_size = sizeof(*addr);
	if (ctx->cm_id->route.addr.dst_addr.ss_family == AF_IB) {
		memcpy(addr, &ctx->cm_id->route.addr.dst_addr, resp.dst_size);
	} else {
		addr->sib_family = AF_IB;
		addr->sib_pkey = (__force __be16) resp.pkey;
		rdma_addr_get_dgid(&ctx->cm_id->route.addr.dev_addr,
				   (union ib_gid *) &addr->sib_addr);
		addr->sib_sid = rdma_get_service_id(ctx->cm_id, (struct sockaddr *)
						    &ctx->cm_id->route.addr.dst_addr);
	}

	if (copy_to_user(response, &resp, sizeof(resp)))
		ret = -EFAULT;

	return ret;
}

static ssize_t ucma_query(struct ucma_file *file,
			  const char __user *inbuf,
			  int in_len, int out_len)
{
	struct rdma_ucm_query cmd;
	struct ucma_context *ctx;
	void __user *response;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	response = (void __user *)(unsigned long) cmd.response;
	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	switch (cmd.option) {
	case RDMA_USER_CM_QUERY_ADDR:
		ret = ucma_query_addr(ctx, response, out_len);
		break;
	case RDMA_USER_CM_QUERY_PATH:
		ret = ucma_query_path(ctx, response, out_len);
		break;
	case RDMA_USER_CM_QUERY_GID:
		ret = ucma_query_gid(ctx, response, out_len);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	ucma_put_ctx(ctx);
	return ret;
}

static void ucma_copy_conn_param(struct rdma_cm_id *id,
				 struct rdma_conn_param *dst,
				 struct rdma_ucm_conn_param *src)
{
	dst->private_data = src->private_data;
	dst->private_data_len = src->private_data_len;
	dst->responder_resources =src->responder_resources;
	dst->initiator_depth = src->initiator_depth;
	dst->flow_control = src->flow_control;
	dst->retry_count = src->retry_count;
	dst->rnr_retry_count = src->rnr_retry_count;
	dst->srq = src->srq;
	dst->qp_num = src->qp_num;
	dst->qkey = (id->route.addr.src_addr.ss_family == AF_IB) ? src->qkey : 0;
}

static ssize_t ucma_connect(struct ucma_file *file, const char __user *inbuf,
			    int in_len, int out_len)
{
	struct rdma_ucm_connect cmd;
	struct rdma_conn_param conn_param;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	if (!cmd.conn_param.valid)
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ucma_copy_conn_param(ctx->cm_id, &conn_param, &cmd.conn_param);
	ret = rdma_connect(ctx->cm_id, &conn_param);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_listen(struct ucma_file *file, const char __user *inbuf,
			   int in_len, int out_len)
{
	struct rdma_ucm_listen cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->backlog = cmd.backlog > 0 && cmd.backlog < max_backlog ?
		       cmd.backlog : max_backlog;
	ret = rdma_listen(ctx->cm_id, ctx->backlog);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_accept(struct ucma_file *file, const char __user *inbuf,
			   int in_len, int out_len)
{
	struct rdma_ucm_accept cmd;
	struct rdma_conn_param conn_param;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (cmd.conn_param.valid) {
		ucma_copy_conn_param(ctx->cm_id, &conn_param, &cmd.conn_param);
		mutex_lock(&file->mut);
		ret = rdma_accept(ctx->cm_id, &conn_param);
		if (!ret)
			ctx->uid = cmd.uid;
		mutex_unlock(&file->mut);
	} else
		ret = rdma_accept(ctx->cm_id, NULL);

	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_reject(struct ucma_file *file, const char __user *inbuf,
			   int in_len, int out_len)
{
	struct rdma_ucm_reject cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = rdma_reject(ctx->cm_id, cmd.private_data, cmd.private_data_len);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_disconnect(struct ucma_file *file, const char __user *inbuf,
			       int in_len, int out_len)
{
	struct rdma_ucm_disconnect cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = rdma_disconnect(ctx->cm_id);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_init_qp_attr(struct ucma_file *file,
				 const char __user *inbuf,
				 int in_len, int out_len)
{
	struct rdma_ucm_init_qp_attr cmd;
	struct ib_uverbs_qp_attr resp;
	struct ucma_context *ctx;
	struct ib_qp_attr qp_attr;
	int ret;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	resp.qp_attr_mask = 0;
	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.qp_state = cmd.qp_state;
	ret = rdma_init_qp_attr(ctx->cm_id, &qp_attr, &resp.qp_attr_mask);
	if (ret)
		goto out;

	ib_copy_qp_attr_to_user(&resp, &qp_attr);
	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		ret = -EFAULT;

out:
	ucma_put_ctx(ctx);
	return ret;
}

static int ucma_set_option_id(struct ucma_context *ctx, int optname,
			      void *optval, size_t optlen)
{
	int ret = 0;

	switch (optname) {
	case RDMA_OPTION_ID_TOS:
		if (optlen != sizeof(u8)) {
			ret = -EINVAL;
			break;
		}
		rdma_set_service_type(ctx->cm_id, *((u8 *) optval));
		break;
	case RDMA_OPTION_ID_REUSEADDR:
		if (optlen != sizeof(int)) {
			ret = -EINVAL;
			break;
		}
		ret = rdma_set_reuseaddr(ctx->cm_id, *((int *) optval) ? 1 : 0);
		break;
	case RDMA_OPTION_ID_AFONLY:
		if (optlen != sizeof(int)) {
			ret = -EINVAL;
			break;
		}
		ret = rdma_set_afonly(ctx->cm_id, *((int *) optval) ? 1 : 0);
		break;
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static int ucma_set_ib_path(struct ucma_context *ctx,
			    struct ib_path_rec_data *path_data, size_t optlen)
{
	struct ib_sa_path_rec sa_path;
	struct rdma_cm_event event;
	int ret;

	if (optlen % sizeof(*path_data))
		return -EINVAL;

	for (; optlen; optlen -= sizeof(*path_data), path_data++) {
		if (path_data->flags == (IB_PATH_GMP | IB_PATH_PRIMARY |
					 IB_PATH_BIDIRECTIONAL))
			break;
	}

	if (!optlen)
		return -EINVAL;

	ib_sa_unpack_path(path_data->path_rec, &sa_path);
	ret = rdma_set_ib_paths(ctx->cm_id, &sa_path, 1);
	if (ret)
		return ret;

	memset(&event, 0, sizeof event);
	event.event = RDMA_CM_EVENT_ROUTE_RESOLVED;
	return ucma_event_handler(ctx->cm_id, &event);
}

static int ucma_set_option_ib(struct ucma_context *ctx, int optname,
			      void *optval, size_t optlen)
{
	int ret;

	switch (optname) {
	case RDMA_OPTION_IB_PATH:
		ret = ucma_set_ib_path(ctx, optval, optlen);
		break;
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static int ucma_set_option_level(struct ucma_context *ctx, int level,
				 int optname, void *optval, size_t optlen)
{
	int ret;

	switch (level) {
	case RDMA_OPTION_ID:
		ret = ucma_set_option_id(ctx, optname, optval, optlen);
		break;
	case RDMA_OPTION_IB:
		ret = ucma_set_option_ib(ctx, optname, optval, optlen);
		break;
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static ssize_t ucma_set_option(struct ucma_file *file, const char __user *inbuf,
			       int in_len, int out_len)
{
	struct rdma_ucm_set_option cmd;
	struct ucma_context *ctx;
	void *optval;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	optval = memdup_user((void __user *) (unsigned long) cmd.optval,
			     cmd.optlen);
	if (IS_ERR(optval)) {
		ret = PTR_ERR(optval);
		goto out;
	}

	ret = ucma_set_option_level(ctx, cmd.level, cmd.optname, optval,
				    cmd.optlen);
	kfree(optval);

out:
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_notify(struct ucma_file *file, const char __user *inbuf,
			   int in_len, int out_len)
{
	struct rdma_ucm_notify cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = rdma_notify(ctx->cm_id, (enum ib_event_type) cmd.event);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_process_join(struct ucma_file *file,
				 struct rdma_ucm_join_mcast *cmd,  int out_len)
{
	struct rdma_ucm_create_id_resp resp;
	struct ucma_context *ctx;
	struct ucma_multicast *mc;
	struct sockaddr *addr;
	int ret;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	addr = (struct sockaddr *) &cmd->addr;
	if (cmd->reserved || !cmd->addr_size || (cmd->addr_size != rdma_addr_size(addr)))
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd->id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&file->mut);
	mc = ucma_alloc_multicast(ctx);
	if (!mc) {
		ret = -ENOMEM;
		goto err1;
	}

	mc->uid = cmd->uid;
	memcpy(&mc->addr, addr, cmd->addr_size);
	ret = rdma_join_multicast(ctx->cm_id, (struct sockaddr *) &mc->addr, mc);
	if (ret)
		goto err2;

	resp.id = mc->id;
	if (copy_to_user((void __user *)(unsigned long) cmd->response,
			 &resp, sizeof(resp))) {
		ret = -EFAULT;
		goto err3;
	}

	mutex_unlock(&file->mut);
	ucma_put_ctx(ctx);
	return 0;

err3:
	rdma_leave_multicast(ctx->cm_id, (struct sockaddr *) &mc->addr);
	ucma_cleanup_mc_events(mc);
err2:
	mutex_lock(&mut);
	idr_remove(&multicast_idr, mc->id);
	mutex_unlock(&mut);
	list_del(&mc->list);
	kfree(mc);
err1:
	mutex_unlock(&file->mut);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_join_ip_multicast(struct ucma_file *file,
				      const char __user *inbuf,
				      int in_len, int out_len)
{
	struct rdma_ucm_join_ip_mcast cmd;
	struct rdma_ucm_join_mcast join_cmd;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	join_cmd.response = cmd.response;
	join_cmd.uid = cmd.uid;
	join_cmd.id = cmd.id;
	join_cmd.addr_size = rdma_addr_size((struct sockaddr *) &cmd.addr);
	join_cmd.reserved = 0;
	memcpy(&join_cmd.addr, &cmd.addr, join_cmd.addr_size);

	return ucma_process_join(file, &join_cmd, out_len);
}

static ssize_t ucma_join_multicast(struct ucma_file *file,
				   const char __user *inbuf,
				   int in_len, int out_len)
{
	struct rdma_ucm_join_mcast cmd;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	return ucma_process_join(file, &cmd, out_len);
}

static ssize_t ucma_leave_multicast(struct ucma_file *file,
				    const char __user *inbuf,
				    int in_len, int out_len)
{
	struct rdma_ucm_destroy_id cmd;
	struct rdma_ucm_destroy_id_resp resp;
	struct ucma_multicast *mc;
	int ret = 0;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	mutex_lock(&mut);
	mc = idr_find(&multicast_idr, cmd.id);
	if (!mc)
		mc = ERR_PTR(-ENOENT);
	else if (mc->ctx->file != file)
		mc = ERR_PTR(-EINVAL);
	else {
		idr_remove(&multicast_idr, mc->id);
		atomic_inc(&mc->ctx->ref);
	}
	mutex_unlock(&mut);

	if (IS_ERR(mc)) {
		ret = PTR_ERR(mc);
		goto out;
	}

	rdma_leave_multicast(mc->ctx->cm_id, (struct sockaddr *) &mc->addr);
	mutex_lock(&mc->ctx->file->mut);
	ucma_cleanup_mc_events(mc);
	list_del(&mc->list);
	mutex_unlock(&mc->ctx->file->mut);

	ucma_put_ctx(mc->ctx);
	resp.events_reported = mc->events_reported;
	kfree(mc);

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		ret = -EFAULT;
out:
	return ret;
}

static void ucma_lock_files(struct ucma_file *file1, struct ucma_file *file2)
{
	/* Acquire mutex's based on pointer comparison to prevent deadlock. */
	if (file1 < file2) {
		mutex_lock(&file1->mut);
		mutex_lock(&file2->mut);
	} else {
		mutex_lock(&file2->mut);
		mutex_lock(&file1->mut);
	}
}

static void ucma_unlock_files(struct ucma_file *file1, struct ucma_file *file2)
{
	if (file1 < file2) {
		mutex_unlock(&file2->mut);
		mutex_unlock(&file1->mut);
	} else {
		mutex_unlock(&file1->mut);
		mutex_unlock(&file2->mut);
	}
}

static void ucma_move_events(struct ucma_context *ctx, struct ucma_file *file)
{
	struct ucma_event *uevent, *tmp;

	list_for_each_entry_safe(uevent, tmp, &ctx->file->event_list, list)
		if (uevent->ctx == ctx)
			list_move_tail(&uevent->list, &file->event_list);
}

static ssize_t ucma_migrate_id(struct ucma_file *new_file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	struct rdma_ucm_migrate_id cmd;
	struct rdma_ucm_migrate_resp resp;
	struct ucma_context *ctx;
	struct fd f;
	struct ucma_file *cur_file;
	int ret = 0;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	/* Get current fd to protect against it being closed */
	f = fdget(cmd.fd);
	if (!f.file)
		return -ENOENT;

	/* Validate current fd and prevent destruction of id. */
	ctx = ucma_get_ctx(f.file->private_data, cmd.id);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto file_put;
	}

	cur_file = ctx->file;
	if (cur_file == new_file) {
		resp.events_reported = ctx->events_reported;
		goto response;
	}

	/*
	 * Migrate events between fd's, maintaining order, and avoiding new
	 * events being added before existing events.
	 */
	ucma_lock_files(cur_file, new_file);
	mutex_lock(&mut);

	list_move_tail(&ctx->list, &new_file->ctx_list);
	ucma_move_events(ctx, new_file);
	ctx->file = new_file;
	resp.events_reported = ctx->events_reported;

	mutex_unlock(&mut);
	ucma_unlock_files(cur_file, new_file);

response:
	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		ret = -EFAULT;

	ucma_put_ctx(ctx);
file_put:
	fdput(f);
	return ret;
}

static ssize_t (*ucma_cmd_table[])(struct ucma_file *file,
				   const char __user *inbuf,
				   int in_len, int out_len) = {
	[RDMA_USER_CM_CMD_CREATE_ID] 	 = ucma_create_id,
	[RDMA_USER_CM_CMD_DESTROY_ID]	 = ucma_destroy_id,
	[RDMA_USER_CM_CMD_BIND_IP]	 = ucma_bind_ip,
	[RDMA_USER_CM_CMD_RESOLVE_IP]	 = ucma_resolve_ip,
	[RDMA_USER_CM_CMD_RESOLVE_ROUTE] = ucma_resolve_route,
	[RDMA_USER_CM_CMD_QUERY_ROUTE]	 = ucma_query_route,
	[RDMA_USER_CM_CMD_CONNECT]	 = ucma_connect,
	[RDMA_USER_CM_CMD_LISTEN]	 = ucma_listen,
	[RDMA_USER_CM_CMD_ACCEPT]	 = ucma_accept,
	[RDMA_USER_CM_CMD_REJECT]	 = ucma_reject,
	[RDMA_USER_CM_CMD_DISCONNECT]	 = ucma_disconnect,
	[RDMA_USER_CM_CMD_INIT_QP_ATTR]	 = ucma_init_qp_attr,
	[RDMA_USER_CM_CMD_GET_EVENT]	 = ucma_get_event,
	[RDMA_USER_CM_CMD_GET_OPTION]	 = NULL,
	[RDMA_USER_CM_CMD_SET_OPTION]	 = ucma_set_option,
	[RDMA_USER_CM_CMD_NOTIFY]	 = ucma_notify,
	[RDMA_USER_CM_CMD_JOIN_IP_MCAST] = ucma_join_ip_multicast,
	[RDMA_USER_CM_CMD_LEAVE_MCAST]	 = ucma_leave_multicast,
	[RDMA_USER_CM_CMD_MIGRATE_ID]	 = ucma_migrate_id,
	[RDMA_USER_CM_CMD_QUERY]	 = ucma_query,
	[RDMA_USER_CM_CMD_BIND]		 = ucma_bind,
	[RDMA_USER_CM_CMD_RESOLVE_ADDR]	 = ucma_resolve_addr,
	[RDMA_USER_CM_CMD_JOIN_MCAST]	 = ucma_join_multicast
};

static ssize_t ucma_write(struct file *filp, const char __user *buf,
			  size_t len, loff_t *pos)
{
	struct ucma_file *file = filp->private_data;
	struct rdma_ucm_cmd_hdr hdr;
	ssize_t ret;

	if (len < sizeof(hdr))
		return -EINVAL;

	if (copy_from_user(&hdr, buf, sizeof(hdr)))
		return -EFAULT;

	if (hdr.cmd >= ARRAY_SIZE(ucma_cmd_table))
		return -EINVAL;

	if (hdr.in + sizeof(hdr) > len)
		return -EINVAL;

	if (!ucma_cmd_table[hdr.cmd])
		return -ENOSYS;

	ret = ucma_cmd_table[hdr.cmd](file, buf + sizeof(hdr), hdr.in, hdr.out);
	if (!ret)
		ret = len;

	return ret;
}

static unsigned int ucma_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct ucma_file *file = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &file->poll_wait, wait);

	if (!list_empty(&file->event_list))
		mask = POLLIN | POLLRDNORM;

	return mask;
}

/*
 * ucma_open() does not need the BKL:
 *
 *  - no global state is referred to;
 *  - there is no ioctl method to race against;
 *  - no further module initialization is required for open to work
 *    after the device is registered.
 */
static int ucma_open(struct inode *inode, struct file *filp)
{
	struct ucma_file *file;

	file = kmalloc(sizeof *file, GFP_KERNEL);
	if (!file)
		return -ENOMEM;

	INIT_LIST_HEAD(&file->event_list);
	INIT_LIST_HEAD(&file->ctx_list);
	init_waitqueue_head(&file->poll_wait);
	mutex_init(&file->mut);

	filp->private_data = file;
	file->filp = filp;

	return nonseekable_open(inode, filp);
}

static int ucma_close(struct inode *inode, struct file *filp)
{
	struct ucma_file *file = filp->private_data;
	struct ucma_context *ctx, *tmp;

	mutex_lock(&file->mut);
	list_for_each_entry_safe(ctx, tmp, &file->ctx_list, list) {
		mutex_unlock(&file->mut);

		mutex_lock(&mut);
		idr_remove(&ctx_idr, ctx->id);
		mutex_unlock(&mut);

		ucma_free_ctx(ctx);
		mutex_lock(&file->mut);
	}
	mutex_unlock(&file->mut);
	kfree(file);
	return 0;
}

static const struct file_operations ucma_fops = {
	.owner 	 = THIS_MODULE,
	.open 	 = ucma_open,
	.release = ucma_close,
	.write	 = ucma_write,
	.poll    = ucma_poll,
	.llseek	 = no_llseek,
};

static struct miscdevice ucma_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "rdma_cm",
	.nodename	= "infiniband/rdma_cm",
	.mode		= 0666,
	.fops		= &ucma_fops,
};

static ssize_t show_abi_version(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d\n", RDMA_USER_CM_ABI_VERSION);
}
static DEVICE_ATTR(abi_version, S_IRUGO, show_abi_version, NULL);

static int __init ucma_init(void)
{
	int ret;

	ret = misc_register(&ucma_misc);
	if (ret)
		return ret;

	ret = device_create_file(ucma_misc.this_device, &dev_attr_abi_version);
	if (ret) {
		printk(KERN_ERR "rdma_ucm: couldn't create abi_version attr\n");
		goto err1;
	}

	ucma_ctl_table_hdr = register_net_sysctl(&init_net, "net/rdma_ucm", ucma_ctl_table);
	if (!ucma_ctl_table_hdr) {
		printk(KERN_ERR "rdma_ucm: couldn't register sysctl paths\n");
		ret = -ENOMEM;
		goto err2;
	}
	return 0;
err2:
	device_remove_file(ucma_misc.this_device, &dev_attr_abi_version);
err1:
	misc_deregister(&ucma_misc);
	return ret;
}

static void __exit ucma_cleanup(void)
{
	unregister_net_sysctl_table(ucma_ctl_table_hdr);
	device_remove_file(ucma_misc.this_device, &dev_attr_abi_version);
	misc_deregister(&ucma_misc);
	idr_destroy(&ctx_idr);
}

module_init(ucma_init);
module_exit(ucma_cleanup);
