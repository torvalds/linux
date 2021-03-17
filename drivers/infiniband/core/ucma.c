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
#include <linux/nsproxy.h>

#include <linux/nospec.h>

#include <rdma/rdma_user_cm.h>
#include <rdma/ib_marshall.h>
#include <rdma/rdma_cm.h>
#include <rdma/rdma_cm_ib.h>
#include <rdma/ib_addr.h>
#include <rdma/ib.h>
#include <rdma/ib_cm.h>
#include <rdma/rdma_netlink.h>
#include "core_priv.h"

MODULE_AUTHOR("Sean Hefty");
MODULE_DESCRIPTION("RDMA Userspace Connection Manager Access");
MODULE_LICENSE("Dual BSD/GPL");

static unsigned int max_backlog = 1024;

static struct ctl_table_header *ucma_ctl_table_hdr;
static struct ctl_table ucma_ctl_table[] = {
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
	u32			id;
	struct completion	comp;
	refcount_t		ref;
	int			events_reported;
	atomic_t		backlog;

	struct ucma_file	*file;
	struct rdma_cm_id	*cm_id;
	struct mutex		mutex;
	u64			uid;

	struct list_head	list;
	struct work_struct	close_work;
};

struct ucma_multicast {
	struct ucma_context	*ctx;
	u32			id;
	int			events_reported;

	u64			uid;
	u8			join_state;
	struct sockaddr_storage	addr;
};

struct ucma_event {
	struct ucma_context	*ctx;
	struct ucma_context	*conn_req_ctx;
	struct ucma_multicast	*mc;
	struct list_head	list;
	struct rdma_ucm_event_resp resp;
};

static DEFINE_XARRAY_ALLOC(ctx_table);
static DEFINE_XARRAY_ALLOC(multicast_table);

static const struct file_operations ucma_fops;
static int ucma_destroy_private_ctx(struct ucma_context *ctx);

static inline struct ucma_context *_ucma_find_context(int id,
						      struct ucma_file *file)
{
	struct ucma_context *ctx;

	ctx = xa_load(&ctx_table, id);
	if (!ctx)
		ctx = ERR_PTR(-ENOENT);
	else if (ctx->file != file)
		ctx = ERR_PTR(-EINVAL);
	return ctx;
}

static struct ucma_context *ucma_get_ctx(struct ucma_file *file, int id)
{
	struct ucma_context *ctx;

	xa_lock(&ctx_table);
	ctx = _ucma_find_context(id, file);
	if (!IS_ERR(ctx))
		if (!refcount_inc_not_zero(&ctx->ref))
			ctx = ERR_PTR(-ENXIO);
	xa_unlock(&ctx_table);
	return ctx;
}

static void ucma_put_ctx(struct ucma_context *ctx)
{
	if (refcount_dec_and_test(&ctx->ref))
		complete(&ctx->comp);
}

/*
 * Same as ucm_get_ctx but requires that ->cm_id->device is valid, eg that the
 * CM_ID is bound.
 */
static struct ucma_context *ucma_get_ctx_dev(struct ucma_file *file, int id)
{
	struct ucma_context *ctx = ucma_get_ctx(file, id);

	if (IS_ERR(ctx))
		return ctx;
	if (!ctx->cm_id->device) {
		ucma_put_ctx(ctx);
		return ERR_PTR(-EINVAL);
	}
	return ctx;
}

static void ucma_close_id(struct work_struct *work)
{
	struct ucma_context *ctx =  container_of(work, struct ucma_context, close_work);

	/* once all inflight tasks are finished, we close all underlying
	 * resources. The context is still alive till its explicit destryoing
	 * by its creator. This puts back the xarray's reference.
	 */
	ucma_put_ctx(ctx);
	wait_for_completion(&ctx->comp);
	/* No new events will be generated after destroying the id. */
	rdma_destroy_id(ctx->cm_id);

	/* Reading the cm_id without holding a positive ref is not allowed */
	ctx->cm_id = NULL;
}

static struct ucma_context *ucma_alloc_ctx(struct ucma_file *file)
{
	struct ucma_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	INIT_WORK(&ctx->close_work, ucma_close_id);
	init_completion(&ctx->comp);
	/* So list_del() will work if we don't do ucma_finish_ctx() */
	INIT_LIST_HEAD(&ctx->list);
	ctx->file = file;
	mutex_init(&ctx->mutex);

	if (xa_alloc(&ctx_table, &ctx->id, NULL, xa_limit_32b, GFP_KERNEL)) {
		kfree(ctx);
		return NULL;
	}
	return ctx;
}

static void ucma_set_ctx_cm_id(struct ucma_context *ctx,
			       struct rdma_cm_id *cm_id)
{
	refcount_set(&ctx->ref, 1);
	ctx->cm_id = cm_id;
}

static void ucma_finish_ctx(struct ucma_context *ctx)
{
	lockdep_assert_held(&ctx->file->mut);
	list_add_tail(&ctx->list, &ctx->file->ctx_list);
	xa_store(&ctx_table, ctx->id, ctx, GFP_KERNEL);
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

static void ucma_copy_ud_event(struct ib_device *device,
			       struct rdma_ucm_ud_param *dst,
			       struct rdma_ud_param *src)
{
	if (src->private_data_len)
		memcpy(dst->private_data, src->private_data,
		       src->private_data_len);
	dst->private_data_len = src->private_data_len;
	ib_copy_ah_attr_to_user(device, &dst->ah_attr, &src->ah_attr);
	dst->qp_num = src->qp_num;
	dst->qkey = src->qkey;
}

static struct ucma_event *ucma_create_uevent(struct ucma_context *ctx,
					     struct rdma_cm_event *event)
{
	struct ucma_event *uevent;

	uevent = kzalloc(sizeof(*uevent), GFP_KERNEL);
	if (!uevent)
		return NULL;

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
	uevent->resp.event = event->event;
	uevent->resp.status = event->status;
	if (ctx->cm_id->qp_type == IB_QPT_UD)
		ucma_copy_ud_event(ctx->cm_id->device, &uevent->resp.param.ud,
				   &event->param.ud);
	else
		ucma_copy_conn_event(&uevent->resp.param.conn,
				     &event->param.conn);

	uevent->resp.ece.vendor_id = event->ece.vendor_id;
	uevent->resp.ece.attr_mod = event->ece.attr_mod;
	return uevent;
}

static int ucma_connect_event_handler(struct rdma_cm_id *cm_id,
				      struct rdma_cm_event *event)
{
	struct ucma_context *listen_ctx = cm_id->context;
	struct ucma_context *ctx;
	struct ucma_event *uevent;

	if (!atomic_add_unless(&listen_ctx->backlog, -1, 0))
		return -ENOMEM;
	ctx = ucma_alloc_ctx(listen_ctx->file);
	if (!ctx)
		goto err_backlog;
	ucma_set_ctx_cm_id(ctx, cm_id);

	uevent = ucma_create_uevent(listen_ctx, event);
	if (!uevent)
		goto err_alloc;
	uevent->conn_req_ctx = ctx;
	uevent->resp.id = ctx->id;

	ctx->cm_id->context = ctx;

	mutex_lock(&ctx->file->mut);
	ucma_finish_ctx(ctx);
	list_add_tail(&uevent->list, &ctx->file->event_list);
	mutex_unlock(&ctx->file->mut);
	wake_up_interruptible(&ctx->file->poll_wait);
	return 0;

err_alloc:
	ucma_destroy_private_ctx(ctx);
err_backlog:
	atomic_inc(&listen_ctx->backlog);
	/* Returning error causes the new ID to be destroyed */
	return -ENOMEM;
}

static int ucma_event_handler(struct rdma_cm_id *cm_id,
			      struct rdma_cm_event *event)
{
	struct ucma_event *uevent;
	struct ucma_context *ctx = cm_id->context;

	if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST)
		return ucma_connect_event_handler(cm_id, event);

	/*
	 * We ignore events for new connections until userspace has set their
	 * context.  This can only happen if an error occurs on a new connection
	 * before the user accepts it.  This is okay, since the accept will just
	 * fail later. However, we do need to release the underlying HW
	 * resources in case of a device removal event.
	 */
	if (ctx->uid) {
		uevent = ucma_create_uevent(ctx, event);
		if (!uevent)
			return 0;

		mutex_lock(&ctx->file->mut);
		list_add_tail(&uevent->list, &ctx->file->event_list);
		mutex_unlock(&ctx->file->mut);
		wake_up_interruptible(&ctx->file->poll_wait);
	}

	if (event->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
		xa_lock(&ctx_table);
		if (xa_load(&ctx_table, ctx->id) == ctx)
			queue_work(system_unbound_wq, &ctx->close_work);
		xa_unlock(&ctx_table);
	}
	return 0;
}

static ssize_t ucma_get_event(struct ucma_file *file, const char __user *inbuf,
			      int in_len, int out_len)
{
	struct rdma_ucm_get_event cmd;
	struct ucma_event *uevent;

	/*
	 * Old 32 bit user space does not send the 4 byte padding in the
	 * reserved field. We don't care, allow it to keep working.
	 */
	if (out_len < sizeof(uevent->resp) - sizeof(uevent->resp.reserved) -
			      sizeof(uevent->resp.ece))
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

	uevent = list_first_entry(&file->event_list, struct ucma_event, list);

	if (copy_to_user(u64_to_user_ptr(cmd.response),
			 &uevent->resp,
			 min_t(size_t, out_len, sizeof(uevent->resp)))) {
		mutex_unlock(&file->mut);
		return -EFAULT;
	}

	list_del(&uevent->list);
	uevent->ctx->events_reported++;
	if (uevent->mc)
		uevent->mc->events_reported++;
	if (uevent->resp.event == RDMA_CM_EVENT_CONNECT_REQUEST)
		atomic_inc(&uevent->ctx->backlog);
	mutex_unlock(&file->mut);

	kfree(uevent);
	return 0;
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
	struct rdma_cm_id *cm_id;
	enum ib_qp_type qp_type;
	int ret;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ret = ucma_get_qp_type(&cmd, &qp_type);
	if (ret)
		return ret;

	ctx = ucma_alloc_ctx(file);
	if (!ctx)
		return -ENOMEM;

	ctx->uid = cmd.uid;
	cm_id = rdma_create_user_id(ucma_event_handler, ctx, cmd.ps, qp_type);
	if (IS_ERR(cm_id)) {
		ret = PTR_ERR(cm_id);
		goto err1;
	}
	ucma_set_ctx_cm_id(ctx, cm_id);

	resp.id = ctx->id;
	if (copy_to_user(u64_to_user_ptr(cmd.response),
			 &resp, sizeof(resp))) {
		ucma_destroy_private_ctx(ctx);
		return -EFAULT;
	}

	mutex_lock(&file->mut);
	ucma_finish_ctx(ctx);
	mutex_unlock(&file->mut);
	return 0;

err1:
	ucma_destroy_private_ctx(ctx);
	return ret;
}

static void ucma_cleanup_multicast(struct ucma_context *ctx)
{
	struct ucma_multicast *mc;
	unsigned long index;

	xa_for_each(&multicast_table, index, mc) {
		if (mc->ctx != ctx)
			continue;
		/*
		 * At this point mc->ctx->ref is 0 so the mc cannot leave the
		 * lock on the reader and this is enough serialization
		 */
		xa_erase(&multicast_table, index);
		kfree(mc);
	}
}

static void ucma_cleanup_mc_events(struct ucma_multicast *mc)
{
	struct ucma_event *uevent, *tmp;

	rdma_lock_handler(mc->ctx->cm_id);
	mutex_lock(&mc->ctx->file->mut);
	list_for_each_entry_safe(uevent, tmp, &mc->ctx->file->event_list, list) {
		if (uevent->mc != mc)
			continue;

		list_del(&uevent->list);
		kfree(uevent);
	}
	mutex_unlock(&mc->ctx->file->mut);
	rdma_unlock_handler(mc->ctx->cm_id);
}

static int ucma_cleanup_ctx_events(struct ucma_context *ctx)
{
	int events_reported;
	struct ucma_event *uevent, *tmp;
	LIST_HEAD(list);

	/* Cleanup events not yet reported to the user.*/
	mutex_lock(&ctx->file->mut);
	list_for_each_entry_safe(uevent, tmp, &ctx->file->event_list, list) {
		if (uevent->ctx != ctx)
			continue;

		if (uevent->resp.event == RDMA_CM_EVENT_CONNECT_REQUEST &&
		    xa_cmpxchg(&ctx_table, uevent->conn_req_ctx->id,
			       uevent->conn_req_ctx, XA_ZERO_ENTRY,
			       GFP_KERNEL) == uevent->conn_req_ctx) {
			list_move_tail(&uevent->list, &list);
			continue;
		}
		list_del(&uevent->list);
		kfree(uevent);
	}
	list_del(&ctx->list);
	events_reported = ctx->events_reported;
	mutex_unlock(&ctx->file->mut);

	/*
	 * If this was a listening ID then any connections spawned from it that
	 * have not been delivered to userspace are cleaned up too. Must be done
	 * outside any locks.
	 */
	list_for_each_entry_safe(uevent, tmp, &list, list) {
		ucma_destroy_private_ctx(uevent->conn_req_ctx);
		kfree(uevent);
	}
	return events_reported;
}

/*
 * When this is called the xarray must have a XA_ZERO_ENTRY in the ctx->id (ie
 * the ctx is not public to the user). This either because:
 *  - ucma_finish_ctx() hasn't been called
 *  - xa_cmpxchg() succeed to remove the entry (only one thread can succeed)
 */
static int ucma_destroy_private_ctx(struct ucma_context *ctx)
{
	int events_reported;

	/*
	 * Destroy the underlying cm_id. New work queuing is prevented now by
	 * the removal from the xarray. Once the work is cancled ref will either
	 * be 0 because the work ran to completion and consumed the ref from the
	 * xarray, or it will be positive because we still have the ref from the
	 * xarray. This can also be 0 in cases where cm_id was never set
	 */
	cancel_work_sync(&ctx->close_work);
	if (refcount_read(&ctx->ref))
		ucma_close_id(&ctx->close_work);

	events_reported = ucma_cleanup_ctx_events(ctx);
	ucma_cleanup_multicast(ctx);

	WARN_ON(xa_cmpxchg(&ctx_table, ctx->id, XA_ZERO_ENTRY, NULL,
			   GFP_KERNEL) != NULL);
	mutex_destroy(&ctx->mutex);
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

	xa_lock(&ctx_table);
	ctx = _ucma_find_context(cmd.id, file);
	if (!IS_ERR(ctx)) {
		if (__xa_cmpxchg(&ctx_table, ctx->id, ctx, XA_ZERO_ENTRY,
				 GFP_KERNEL) != ctx)
			ctx = ERR_PTR(-ENOENT);
	}
	xa_unlock(&ctx_table);

	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	resp.events_reported = ucma_destroy_private_ctx(ctx);
	if (copy_to_user(u64_to_user_ptr(cmd.response),
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

	if (!rdma_addr_size_in6(&cmd.addr))
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
	ret = rdma_bind_addr(ctx->cm_id, (struct sockaddr *) &cmd.addr);
	mutex_unlock(&ctx->mutex);

	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_bind(struct ucma_file *file, const char __user *inbuf,
			 int in_len, int out_len)
{
	struct rdma_ucm_bind cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.reserved || !cmd.addr_size ||
	    cmd.addr_size != rdma_addr_size_kss(&cmd.addr))
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
	ret = rdma_bind_addr(ctx->cm_id, (struct sockaddr *) &cmd.addr);
	mutex_unlock(&ctx->mutex);
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

	if ((cmd.src_addr.sin6_family && !rdma_addr_size_in6(&cmd.src_addr)) ||
	    !rdma_addr_size_in6(&cmd.dst_addr))
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
	ret = rdma_resolve_addr(ctx->cm_id, (struct sockaddr *) &cmd.src_addr,
				(struct sockaddr *) &cmd.dst_addr, cmd.timeout_ms);
	mutex_unlock(&ctx->mutex);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_resolve_addr(struct ucma_file *file,
				 const char __user *inbuf,
				 int in_len, int out_len)
{
	struct rdma_ucm_resolve_addr cmd;
	struct ucma_context *ctx;
	int ret;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.reserved ||
	    (cmd.src_size && (cmd.src_size != rdma_addr_size_kss(&cmd.src_addr))) ||
	    !cmd.dst_size || (cmd.dst_size != rdma_addr_size_kss(&cmd.dst_addr)))
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
	ret = rdma_resolve_addr(ctx->cm_id, (struct sockaddr *) &cmd.src_addr,
				(struct sockaddr *) &cmd.dst_addr, cmd.timeout_ms);
	mutex_unlock(&ctx->mutex);
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

	ctx = ucma_get_ctx_dev(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
	ret = rdma_resolve_route(ctx->cm_id, cmd.timeout_ms);
	mutex_unlock(&ctx->mutex);
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
		fallthrough;
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

	resp->num_paths = route->num_paths;
	switch (route->num_paths) {
	case 0:
		rdma_ip2gid((struct sockaddr *)&route->addr.dst_addr,
			    (union ib_gid *)&resp->ib_route[0].dgid);
		rdma_ip2gid((struct sockaddr *)&route->addr.src_addr,
			    (union ib_gid *)&resp->ib_route[0].sgid);
		resp->ib_route[0].pkey = cpu_to_be16(0xffff);
		break;
	case 2:
		ib_copy_path_rec_to_user(&resp->ib_route[1],
					 &route->path_rec[1]);
		fallthrough;
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

	if (out_len < offsetof(struct rdma_ucm_query_route_resp, ibdev_index))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
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
	resp.ibdev_index = ctx->cm_id->device->index;
	resp.port_num = ctx->cm_id->port_num;

	if (rdma_cap_ib_sa(ctx->cm_id->device, ctx->cm_id->port_num))
		ucma_copy_ib_route(&resp, &ctx->cm_id->route);
	else if (rdma_protocol_roce(ctx->cm_id->device, ctx->cm_id->port_num))
		ucma_copy_iboe_route(&resp, &ctx->cm_id->route);
	else if (rdma_protocol_iwarp(ctx->cm_id->device, ctx->cm_id->port_num))
		ucma_copy_iw_route(&resp, &ctx->cm_id->route);

out:
	mutex_unlock(&ctx->mutex);
	if (copy_to_user(u64_to_user_ptr(cmd.response), &resp,
			 min_t(size_t, out_len, sizeof(resp))))
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
	resp->ibdev_index = cm_id->device->index;
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

	if (out_len < offsetof(struct rdma_ucm_query_addr_resp, ibdev_index))
		return -ENOSPC;

	memset(&resp, 0, sizeof resp);

	addr = (struct sockaddr *) &ctx->cm_id->route.addr.src_addr;
	resp.src_size = rdma_addr_size(addr);
	memcpy(&resp.src_addr, addr, resp.src_size);

	addr = (struct sockaddr *) &ctx->cm_id->route.addr.dst_addr;
	resp.dst_size = rdma_addr_size(addr);
	memcpy(&resp.dst_addr, addr, resp.dst_size);

	ucma_query_device_addr(ctx->cm_id, &resp);

	if (copy_to_user(response, &resp, min_t(size_t, out_len, sizeof(resp))))
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
		struct sa_path_rec *rec = &ctx->cm_id->route.path_rec[i];

		resp->path_data[i].flags = IB_PATH_GMP | IB_PATH_PRIMARY |
					   IB_PATH_BIDIRECTIONAL;
		if (rec->rec_type == SA_PATH_REC_TYPE_OPA) {
			struct sa_path_rec ib;

			sa_convert_path_opa_to_ib(&ib, rec);
			ib_sa_pack_path(&ib, &resp->path_data[i].path_rec);

		} else {
			ib_sa_pack_path(rec, &resp->path_data[i].path_rec);
		}
	}

	if (copy_to_user(response, resp, struct_size(resp, path_data, i)))
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

	if (out_len < offsetof(struct rdma_ucm_query_addr_resp, ibdev_index))
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
		rdma_read_gids(ctx->cm_id, (union ib_gid *)&addr->sib_addr,
			       NULL);
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
		rdma_read_gids(ctx->cm_id, NULL,
			       (union ib_gid *)&addr->sib_addr);
		addr->sib_sid = rdma_get_service_id(ctx->cm_id, (struct sockaddr *)
						    &ctx->cm_id->route.addr.dst_addr);
	}

	if (copy_to_user(response, &resp, min_t(size_t, out_len, sizeof(resp))))
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

	response = u64_to_user_ptr(cmd.response);
	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
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
	mutex_unlock(&ctx->mutex);

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
	dst->qp_num = src->qp_num & 0xFFFFFF;
	dst->qkey = (id->route.addr.src_addr.ss_family == AF_IB) ? src->qkey : 0;
}

static ssize_t ucma_connect(struct ucma_file *file, const char __user *inbuf,
			    int in_len, int out_len)
{
	struct rdma_conn_param conn_param;
	struct rdma_ucm_ece ece = {};
	struct rdma_ucm_connect cmd;
	struct ucma_context *ctx;
	size_t in_size;
	int ret;

	if (in_len < offsetofend(typeof(cmd), reserved))
		return -EINVAL;
	in_size = min_t(size_t, in_len, sizeof(cmd));
	if (copy_from_user(&cmd, inbuf, in_size))
		return -EFAULT;

	if (!cmd.conn_param.valid)
		return -EINVAL;

	ctx = ucma_get_ctx_dev(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ucma_copy_conn_param(ctx->cm_id, &conn_param, &cmd.conn_param);
	if (offsetofend(typeof(cmd), ece) <= in_size) {
		ece.vendor_id = cmd.ece.vendor_id;
		ece.attr_mod = cmd.ece.attr_mod;
	}

	mutex_lock(&ctx->mutex);
	ret = rdma_connect_ece(ctx->cm_id, &conn_param, &ece);
	mutex_unlock(&ctx->mutex);
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

	if (cmd.backlog <= 0 || cmd.backlog > max_backlog)
		cmd.backlog = max_backlog;
	atomic_set(&ctx->backlog, cmd.backlog);

	mutex_lock(&ctx->mutex);
	ret = rdma_listen(ctx->cm_id, cmd.backlog);
	mutex_unlock(&ctx->mutex);
	ucma_put_ctx(ctx);
	return ret;
}

static ssize_t ucma_accept(struct ucma_file *file, const char __user *inbuf,
			   int in_len, int out_len)
{
	struct rdma_ucm_accept cmd;
	struct rdma_conn_param conn_param;
	struct rdma_ucm_ece ece = {};
	struct ucma_context *ctx;
	size_t in_size;
	int ret;

	if (in_len < offsetofend(typeof(cmd), reserved))
		return -EINVAL;
	in_size = min_t(size_t, in_len, sizeof(cmd));
	if (copy_from_user(&cmd, inbuf, in_size))
		return -EFAULT;

	ctx = ucma_get_ctx_dev(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (offsetofend(typeof(cmd), ece) <= in_size) {
		ece.vendor_id = cmd.ece.vendor_id;
		ece.attr_mod = cmd.ece.attr_mod;
	}

	if (cmd.conn_param.valid) {
		ucma_copy_conn_param(ctx->cm_id, &conn_param, &cmd.conn_param);
		mutex_lock(&ctx->mutex);
		rdma_lock_handler(ctx->cm_id);
		ret = rdma_accept_ece(ctx->cm_id, &conn_param, &ece);
		if (!ret) {
			/* The uid must be set atomically with the handler */
			ctx->uid = cmd.uid;
		}
		rdma_unlock_handler(ctx->cm_id);
		mutex_unlock(&ctx->mutex);
	} else {
		mutex_lock(&ctx->mutex);
		rdma_lock_handler(ctx->cm_id);
		ret = rdma_accept_ece(ctx->cm_id, NULL, &ece);
		rdma_unlock_handler(ctx->cm_id);
		mutex_unlock(&ctx->mutex);
	}
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

	if (!cmd.reason)
		cmd.reason = IB_CM_REJ_CONSUMER_DEFINED;

	switch (cmd.reason) {
	case IB_CM_REJ_CONSUMER_DEFINED:
	case IB_CM_REJ_VENDOR_OPTION_NOT_SUPPORTED:
		break;
	default:
		return -EINVAL;
	}

	ctx = ucma_get_ctx_dev(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
	ret = rdma_reject(ctx->cm_id, cmd.private_data, cmd.private_data_len,
			  cmd.reason);
	mutex_unlock(&ctx->mutex);
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

	ctx = ucma_get_ctx_dev(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
	ret = rdma_disconnect(ctx->cm_id);
	mutex_unlock(&ctx->mutex);
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

	if (cmd.qp_state > IB_QPS_ERR)
		return -EINVAL;

	ctx = ucma_get_ctx_dev(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	resp.qp_attr_mask = 0;
	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.qp_state = cmd.qp_state;
	mutex_lock(&ctx->mutex);
	ret = rdma_init_qp_attr(ctx->cm_id, &qp_attr, &resp.qp_attr_mask);
	mutex_unlock(&ctx->mutex);
	if (ret)
		goto out;

	ib_copy_qp_attr_to_user(ctx->cm_id->device, &resp, &qp_attr);
	if (copy_to_user(u64_to_user_ptr(cmd.response),
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
	case RDMA_OPTION_ID_ACK_TIMEOUT:
		if (optlen != sizeof(u8)) {
			ret = -EINVAL;
			break;
		}
		ret = rdma_set_ack_timeout(ctx->cm_id, *((u8 *)optval));
		break;
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static int ucma_set_ib_path(struct ucma_context *ctx,
			    struct ib_path_rec_data *path_data, size_t optlen)
{
	struct sa_path_rec sa_path;
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

	if (!ctx->cm_id->device)
		return -EINVAL;

	memset(&sa_path, 0, sizeof(sa_path));

	sa_path.rec_type = SA_PATH_REC_TYPE_IB;
	ib_sa_unpack_path(path_data->path_rec, &sa_path);

	if (rdma_cap_opa_ah(ctx->cm_id->device, ctx->cm_id->port_num)) {
		struct sa_path_rec opa;

		sa_convert_path_ib_to_opa(&opa, &sa_path);
		mutex_lock(&ctx->mutex);
		ret = rdma_set_ib_path(ctx->cm_id, &opa);
		mutex_unlock(&ctx->mutex);
	} else {
		mutex_lock(&ctx->mutex);
		ret = rdma_set_ib_path(ctx->cm_id, &sa_path);
		mutex_unlock(&ctx->mutex);
	}
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
		mutex_lock(&ctx->mutex);
		ret = ucma_set_option_id(ctx, optname, optval, optlen);
		mutex_unlock(&ctx->mutex);
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

	if (unlikely(cmd.optlen > KMALLOC_MAX_SIZE))
		return -EINVAL;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	optval = memdup_user(u64_to_user_ptr(cmd.optval),
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
	int ret = -EINVAL;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ucma_get_ctx(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mutex_lock(&ctx->mutex);
	if (ctx->cm_id->device)
		ret = rdma_notify(ctx->cm_id, (enum ib_event_type)cmd.event);
	mutex_unlock(&ctx->mutex);

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
	u8 join_state;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	addr = (struct sockaddr *) &cmd->addr;
	if (cmd->addr_size != rdma_addr_size(addr))
		return -EINVAL;

	if (cmd->join_flags == RDMA_MC_JOIN_FLAG_FULLMEMBER)
		join_state = BIT(FULLMEMBER_JOIN);
	else if (cmd->join_flags == RDMA_MC_JOIN_FLAG_SENDONLY_FULLMEMBER)
		join_state = BIT(SENDONLY_FULLMEMBER_JOIN);
	else
		return -EINVAL;

	ctx = ucma_get_ctx_dev(file, cmd->id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mc = kzalloc(sizeof(*mc), GFP_KERNEL);
	if (!mc) {
		ret = -ENOMEM;
		goto err_put_ctx;
	}

	mc->ctx = ctx;
	mc->join_state = join_state;
	mc->uid = cmd->uid;
	memcpy(&mc->addr, addr, cmd->addr_size);

	if (xa_alloc(&multicast_table, &mc->id, NULL, xa_limit_32b,
		     GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_free_mc;
	}

	mutex_lock(&ctx->mutex);
	ret = rdma_join_multicast(ctx->cm_id, (struct sockaddr *)&mc->addr,
				  join_state, mc);
	mutex_unlock(&ctx->mutex);
	if (ret)
		goto err_xa_erase;

	resp.id = mc->id;
	if (copy_to_user(u64_to_user_ptr(cmd->response),
			 &resp, sizeof(resp))) {
		ret = -EFAULT;
		goto err_leave_multicast;
	}

	xa_store(&multicast_table, mc->id, mc, 0);

	ucma_put_ctx(ctx);
	return 0;

err_leave_multicast:
	mutex_lock(&ctx->mutex);
	rdma_leave_multicast(ctx->cm_id, (struct sockaddr *) &mc->addr);
	mutex_unlock(&ctx->mutex);
	ucma_cleanup_mc_events(mc);
err_xa_erase:
	xa_erase(&multicast_table, mc->id);
err_free_mc:
	kfree(mc);
err_put_ctx:
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
	join_cmd.addr_size = rdma_addr_size_in6(&cmd.addr);
	if (!join_cmd.addr_size)
		return -EINVAL;

	join_cmd.join_flags = RDMA_MC_JOIN_FLAG_FULLMEMBER;
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

	if (!rdma_addr_size_kss(&cmd.addr))
		return -EINVAL;

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

	xa_lock(&multicast_table);
	mc = xa_load(&multicast_table, cmd.id);
	if (!mc)
		mc = ERR_PTR(-ENOENT);
	else if (READ_ONCE(mc->ctx->file) != file)
		mc = ERR_PTR(-EINVAL);
	else if (!refcount_inc_not_zero(&mc->ctx->ref))
		mc = ERR_PTR(-ENXIO);
	else
		__xa_erase(&multicast_table, mc->id);
	xa_unlock(&multicast_table);

	if (IS_ERR(mc)) {
		ret = PTR_ERR(mc);
		goto out;
	}

	mutex_lock(&mc->ctx->mutex);
	rdma_leave_multicast(mc->ctx->cm_id, (struct sockaddr *) &mc->addr);
	mutex_unlock(&mc->ctx->mutex);

	ucma_cleanup_mc_events(mc);

	ucma_put_ctx(mc->ctx);
	resp.events_reported = mc->events_reported;
	kfree(mc);

	if (copy_to_user(u64_to_user_ptr(cmd.response),
			 &resp, sizeof(resp)))
		ret = -EFAULT;
out:
	return ret;
}

static ssize_t ucma_migrate_id(struct ucma_file *new_file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	struct rdma_ucm_migrate_id cmd;
	struct rdma_ucm_migrate_resp resp;
	struct ucma_event *uevent, *tmp;
	struct ucma_context *ctx;
	LIST_HEAD(event_list);
	struct fd f;
	struct ucma_file *cur_file;
	int ret = 0;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	/* Get current fd to protect against it being closed */
	f = fdget(cmd.fd);
	if (!f.file)
		return -ENOENT;
	if (f.file->f_op != &ucma_fops) {
		ret = -EINVAL;
		goto file_put;
	}
	cur_file = f.file->private_data;

	/* Validate current fd and prevent destruction of id. */
	ctx = ucma_get_ctx(cur_file, cmd.id);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto file_put;
	}

	rdma_lock_handler(ctx->cm_id);
	/*
	 * ctx->file can only be changed under the handler & xa_lock. xa_load()
	 * must be checked again to ensure the ctx hasn't begun destruction
	 * since the ucma_get_ctx().
	 */
	xa_lock(&ctx_table);
	if (_ucma_find_context(cmd.id, cur_file) != ctx) {
		xa_unlock(&ctx_table);
		ret = -ENOENT;
		goto err_unlock;
	}
	ctx->file = new_file;
	xa_unlock(&ctx_table);

	mutex_lock(&cur_file->mut);
	list_del(&ctx->list);
	/*
	 * At this point lock_handler() prevents addition of new uevents for
	 * this ctx.
	 */
	list_for_each_entry_safe(uevent, tmp, &cur_file->event_list, list)
		if (uevent->ctx == ctx)
			list_move_tail(&uevent->list, &event_list);
	resp.events_reported = ctx->events_reported;
	mutex_unlock(&cur_file->mut);

	mutex_lock(&new_file->mut);
	list_add_tail(&ctx->list, &new_file->ctx_list);
	list_splice_tail(&event_list, &new_file->event_list);
	mutex_unlock(&new_file->mut);

	if (copy_to_user(u64_to_user_ptr(cmd.response),
			 &resp, sizeof(resp)))
		ret = -EFAULT;

err_unlock:
	rdma_unlock_handler(ctx->cm_id);
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

	if (!ib_safe_file_access(filp)) {
		pr_err_once("ucma_write: process %d (%s) changed security contexts after opening file descriptor, this is not allowed.\n",
			    task_tgid_vnr(current), current->comm);
		return -EACCES;
	}

	if (len < sizeof(hdr))
		return -EINVAL;

	if (copy_from_user(&hdr, buf, sizeof(hdr)))
		return -EFAULT;

	if (hdr.cmd >= ARRAY_SIZE(ucma_cmd_table))
		return -EINVAL;
	hdr.cmd = array_index_nospec(hdr.cmd, ARRAY_SIZE(ucma_cmd_table));

	if (hdr.in + sizeof(hdr) > len)
		return -EINVAL;

	if (!ucma_cmd_table[hdr.cmd])
		return -ENOSYS;

	ret = ucma_cmd_table[hdr.cmd](file, buf + sizeof(hdr), hdr.in, hdr.out);
	if (!ret)
		ret = len;

	return ret;
}

static __poll_t ucma_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct ucma_file *file = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &file->poll_wait, wait);

	if (!list_empty(&file->event_list))
		mask = EPOLLIN | EPOLLRDNORM;

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

	return stream_open(inode, filp);
}

static int ucma_close(struct inode *inode, struct file *filp)
{
	struct ucma_file *file = filp->private_data;

	/*
	 * All paths that touch ctx_list or ctx_list starting from write() are
	 * prevented by this being a FD release function. The list_add_tail() in
	 * ucma_connect_event_handler() can run concurrently, however it only
	 * adds to the list *after* a listening ID. By only reading the first of
	 * the list, and relying on ucma_destroy_private_ctx() to block
	 * ucma_connect_event_handler(), no additional locking is needed.
	 */
	while (!list_empty(&file->ctx_list)) {
		struct ucma_context *ctx = list_first_entry(
			&file->ctx_list, struct ucma_context, list);

		WARN_ON(xa_cmpxchg(&ctx_table, ctx->id, ctx, XA_ZERO_ENTRY,
				   GFP_KERNEL) != ctx);
		ucma_destroy_private_ctx(ctx);
	}
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

static int ucma_get_global_nl_info(struct ib_client_nl_info *res)
{
	res->abi = RDMA_USER_CM_ABI_VERSION;
	res->cdev = ucma_misc.this_device;
	return 0;
}

static struct ib_client rdma_cma_client = {
	.name = "rdma_cm",
	.get_global_nl_info = ucma_get_global_nl_info,
};
MODULE_ALIAS_RDMA_CLIENT("rdma_cm");

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
		pr_err("rdma_ucm: couldn't create abi_version attr\n");
		goto err1;
	}

	ucma_ctl_table_hdr = register_net_sysctl(&init_net, "net/rdma_ucm", ucma_ctl_table);
	if (!ucma_ctl_table_hdr) {
		pr_err("rdma_ucm: couldn't register sysctl paths\n");
		ret = -ENOMEM;
		goto err2;
	}

	ret = ib_register_client(&rdma_cma_client);
	if (ret)
		goto err3;

	return 0;
err3:
	unregister_net_sysctl_table(ucma_ctl_table_hdr);
err2:
	device_remove_file(ucma_misc.this_device, &dev_attr_abi_version);
err1:
	misc_deregister(&ucma_misc);
	return ret;
}

static void __exit ucma_cleanup(void)
{
	ib_unregister_client(&rdma_cma_client);
	unregister_net_sysctl_table(ucma_ctl_table_hdr);
	device_remove_file(ucma_misc.this_device, &dev_attr_abi_version);
	misc_deregister(&ucma_misc);
}

module_init(ucma_init);
module_exit(ucma_cleanup);
