/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
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
 *
 * $Id: ucm.c 4311 2005-12-05 18:42:01Z sean.hefty $
 */

#include <linux/completion.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>

#include <rdma/ib_cm.h>
#include <rdma/ib_user_cm.h>
#include <rdma/ib_marshall.h>

MODULE_AUTHOR("Libor Michalek");
MODULE_DESCRIPTION("InfiniBand userspace Connection Manager access");
MODULE_LICENSE("Dual BSD/GPL");

struct ib_ucm_device {
	int			devnum;
	struct cdev		dev;
	struct class_device	class_dev;
	struct ib_device	*ib_dev;
};

struct ib_ucm_file {
	struct mutex file_mutex;
	struct file *filp;
	struct ib_ucm_device *device;

	struct list_head  ctxs;
	struct list_head  events;
	wait_queue_head_t poll_wait;
};

struct ib_ucm_context {
	int                 id;
	struct completion   comp;
	atomic_t            ref;
	int		    events_reported;

	struct ib_ucm_file *file;
	struct ib_cm_id    *cm_id;
	__u64		   uid;

	struct list_head    events;    /* list of pending events. */
	struct list_head    file_list; /* member in file ctx list */
};

struct ib_ucm_event {
	struct ib_ucm_context *ctx;
	struct list_head file_list; /* member in file event list */
	struct list_head ctx_list;  /* member in ctx event list */

	struct ib_cm_id *cm_id;
	struct ib_ucm_event_resp resp;
	void *data;
	void *info;
	int data_len;
	int info_len;
};

enum {
	IB_UCM_MAJOR = 231,
	IB_UCM_BASE_MINOR = 224,
	IB_UCM_MAX_DEVICES = 32
};

#define IB_UCM_BASE_DEV MKDEV(IB_UCM_MAJOR, IB_UCM_BASE_MINOR)

static void ib_ucm_add_one(struct ib_device *device);
static void ib_ucm_remove_one(struct ib_device *device);

static struct ib_client ucm_client = {
	.name   = "ucm",
	.add    = ib_ucm_add_one,
	.remove = ib_ucm_remove_one
};

static DEFINE_MUTEX(ctx_id_mutex);
static DEFINE_IDR(ctx_id_table);
static DECLARE_BITMAP(dev_map, IB_UCM_MAX_DEVICES);

static struct ib_ucm_context *ib_ucm_ctx_get(struct ib_ucm_file *file, int id)
{
	struct ib_ucm_context *ctx;

	mutex_lock(&ctx_id_mutex);
	ctx = idr_find(&ctx_id_table, id);
	if (!ctx)
		ctx = ERR_PTR(-ENOENT);
	else if (ctx->file != file)
		ctx = ERR_PTR(-EINVAL);
	else
		atomic_inc(&ctx->ref);
	mutex_unlock(&ctx_id_mutex);

	return ctx;
}

static void ib_ucm_ctx_put(struct ib_ucm_context *ctx)
{
	if (atomic_dec_and_test(&ctx->ref))
		complete(&ctx->comp);
}

static inline int ib_ucm_new_cm_id(int event)
{
	return event == IB_CM_REQ_RECEIVED || event == IB_CM_SIDR_REQ_RECEIVED;
}

static void ib_ucm_cleanup_events(struct ib_ucm_context *ctx)
{
	struct ib_ucm_event *uevent;

	mutex_lock(&ctx->file->file_mutex);
	list_del(&ctx->file_list);
	while (!list_empty(&ctx->events)) {

		uevent = list_entry(ctx->events.next,
				    struct ib_ucm_event, ctx_list);
		list_del(&uevent->file_list);
		list_del(&uevent->ctx_list);
		mutex_unlock(&ctx->file->file_mutex);

		/* clear incoming connections. */
		if (ib_ucm_new_cm_id(uevent->resp.event))
			ib_destroy_cm_id(uevent->cm_id);

		kfree(uevent);
		mutex_lock(&ctx->file->file_mutex);
	}
	mutex_unlock(&ctx->file->file_mutex);
}

static struct ib_ucm_context *ib_ucm_ctx_alloc(struct ib_ucm_file *file)
{
	struct ib_ucm_context *ctx;
	int result;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return NULL;

	atomic_set(&ctx->ref, 1);
	init_completion(&ctx->comp);
	ctx->file = file;
	INIT_LIST_HEAD(&ctx->events);

	do {
		result = idr_pre_get(&ctx_id_table, GFP_KERNEL);
		if (!result)
			goto error;

		mutex_lock(&ctx_id_mutex);
		result = idr_get_new(&ctx_id_table, ctx, &ctx->id);
		mutex_unlock(&ctx_id_mutex);
	} while (result == -EAGAIN);

	if (result)
		goto error;

	list_add_tail(&ctx->file_list, &file->ctxs);
	return ctx;

error:
	kfree(ctx);
	return NULL;
}

static void ib_ucm_event_req_get(struct ib_ucm_req_event_resp *ureq,
				 struct ib_cm_req_event_param *kreq)
{
	ureq->remote_ca_guid             = kreq->remote_ca_guid;
	ureq->remote_qkey                = kreq->remote_qkey;
	ureq->remote_qpn                 = kreq->remote_qpn;
	ureq->qp_type                    = kreq->qp_type;
	ureq->starting_psn               = kreq->starting_psn;
	ureq->responder_resources        = kreq->responder_resources;
	ureq->initiator_depth            = kreq->initiator_depth;
	ureq->local_cm_response_timeout  = kreq->local_cm_response_timeout;
	ureq->flow_control               = kreq->flow_control;
	ureq->remote_cm_response_timeout = kreq->remote_cm_response_timeout;
	ureq->retry_count                = kreq->retry_count;
	ureq->rnr_retry_count            = kreq->rnr_retry_count;
	ureq->srq                        = kreq->srq;
	ureq->port			 = kreq->port;

	ib_copy_path_rec_to_user(&ureq->primary_path, kreq->primary_path);
	if (kreq->alternate_path)
		ib_copy_path_rec_to_user(&ureq->alternate_path,
					 kreq->alternate_path);
}

static void ib_ucm_event_rep_get(struct ib_ucm_rep_event_resp *urep,
				 struct ib_cm_rep_event_param *krep)
{
	urep->remote_ca_guid      = krep->remote_ca_guid;
	urep->remote_qkey         = krep->remote_qkey;
	urep->remote_qpn          = krep->remote_qpn;
	urep->starting_psn        = krep->starting_psn;
	urep->responder_resources = krep->responder_resources;
	urep->initiator_depth     = krep->initiator_depth;
	urep->target_ack_delay    = krep->target_ack_delay;
	urep->failover_accepted   = krep->failover_accepted;
	urep->flow_control        = krep->flow_control;
	urep->rnr_retry_count     = krep->rnr_retry_count;
	urep->srq                 = krep->srq;
}

static void ib_ucm_event_sidr_rep_get(struct ib_ucm_sidr_rep_event_resp *urep,
				      struct ib_cm_sidr_rep_event_param *krep)
{
	urep->status = krep->status;
	urep->qkey   = krep->qkey;
	urep->qpn    = krep->qpn;
};

static int ib_ucm_event_process(struct ib_cm_event *evt,
				struct ib_ucm_event *uvt)
{
	void *info = NULL;

	switch (evt->event) {
	case IB_CM_REQ_RECEIVED:
		ib_ucm_event_req_get(&uvt->resp.u.req_resp,
				     &evt->param.req_rcvd);
		uvt->data_len      = IB_CM_REQ_PRIVATE_DATA_SIZE;
		uvt->resp.present  = IB_UCM_PRES_PRIMARY;
		uvt->resp.present |= (evt->param.req_rcvd.alternate_path ?
				      IB_UCM_PRES_ALTERNATE : 0);
		break;
	case IB_CM_REP_RECEIVED:
		ib_ucm_event_rep_get(&uvt->resp.u.rep_resp,
				     &evt->param.rep_rcvd);
		uvt->data_len = IB_CM_REP_PRIVATE_DATA_SIZE;
		break;
	case IB_CM_RTU_RECEIVED:
		uvt->data_len = IB_CM_RTU_PRIVATE_DATA_SIZE;
		uvt->resp.u.send_status = evt->param.send_status;
		break;
	case IB_CM_DREQ_RECEIVED:
		uvt->data_len = IB_CM_DREQ_PRIVATE_DATA_SIZE;
		uvt->resp.u.send_status = evt->param.send_status;
		break;
	case IB_CM_DREP_RECEIVED:
		uvt->data_len = IB_CM_DREP_PRIVATE_DATA_SIZE;
		uvt->resp.u.send_status = evt->param.send_status;
		break;
	case IB_CM_MRA_RECEIVED:
		uvt->resp.u.mra_resp.timeout =
					evt->param.mra_rcvd.service_timeout;
		uvt->data_len = IB_CM_MRA_PRIVATE_DATA_SIZE;
		break;
	case IB_CM_REJ_RECEIVED:
		uvt->resp.u.rej_resp.reason = evt->param.rej_rcvd.reason;
		uvt->data_len = IB_CM_REJ_PRIVATE_DATA_SIZE;
		uvt->info_len = evt->param.rej_rcvd.ari_length;
		info	      = evt->param.rej_rcvd.ari;
		break;
	case IB_CM_LAP_RECEIVED:
		ib_copy_path_rec_to_user(&uvt->resp.u.lap_resp.path,
					 evt->param.lap_rcvd.alternate_path);
		uvt->data_len = IB_CM_LAP_PRIVATE_DATA_SIZE;
		uvt->resp.present = IB_UCM_PRES_ALTERNATE;
		break;
	case IB_CM_APR_RECEIVED:
		uvt->resp.u.apr_resp.status = evt->param.apr_rcvd.ap_status;
		uvt->data_len = IB_CM_APR_PRIVATE_DATA_SIZE;
		uvt->info_len = evt->param.apr_rcvd.info_len;
		info	      = evt->param.apr_rcvd.apr_info;
		break;
	case IB_CM_SIDR_REQ_RECEIVED:
		uvt->resp.u.sidr_req_resp.pkey =
					evt->param.sidr_req_rcvd.pkey;
		uvt->resp.u.sidr_req_resp.port =
					evt->param.sidr_req_rcvd.port;
		uvt->data_len = IB_CM_SIDR_REQ_PRIVATE_DATA_SIZE;
		break;
	case IB_CM_SIDR_REP_RECEIVED:
		ib_ucm_event_sidr_rep_get(&uvt->resp.u.sidr_rep_resp,
					  &evt->param.sidr_rep_rcvd);
		uvt->data_len = IB_CM_SIDR_REP_PRIVATE_DATA_SIZE;
		uvt->info_len = evt->param.sidr_rep_rcvd.info_len;
		info	      = evt->param.sidr_rep_rcvd.info;
		break;
	default:
		uvt->resp.u.send_status = evt->param.send_status;
		break;
	}

	if (uvt->data_len) {
		uvt->data = kmemdup(evt->private_data, uvt->data_len, GFP_KERNEL);
		if (!uvt->data)
			goto err1;

		uvt->resp.present |= IB_UCM_PRES_DATA;
	}

	if (uvt->info_len) {
		uvt->info = kmemdup(info, uvt->info_len, GFP_KERNEL);
		if (!uvt->info)
			goto err2;

		uvt->resp.present |= IB_UCM_PRES_INFO;
	}
	return 0;

err2:
	kfree(uvt->data);
err1:
	return -ENOMEM;
}

static int ib_ucm_event_handler(struct ib_cm_id *cm_id,
				struct ib_cm_event *event)
{
	struct ib_ucm_event *uevent;
	struct ib_ucm_context *ctx;
	int result = 0;

	ctx = cm_id->context;

	uevent = kzalloc(sizeof *uevent, GFP_KERNEL);
	if (!uevent)
		goto err1;

	uevent->ctx = ctx;
	uevent->cm_id = cm_id;
	uevent->resp.uid = ctx->uid;
	uevent->resp.id = ctx->id;
	uevent->resp.event = event->event;

	result = ib_ucm_event_process(event, uevent);
	if (result)
		goto err2;

	mutex_lock(&ctx->file->file_mutex);
	list_add_tail(&uevent->file_list, &ctx->file->events);
	list_add_tail(&uevent->ctx_list, &ctx->events);
	wake_up_interruptible(&ctx->file->poll_wait);
	mutex_unlock(&ctx->file->file_mutex);
	return 0;

err2:
	kfree(uevent);
err1:
	/* Destroy new cm_id's */
	return ib_ucm_new_cm_id(event->event);
}

static ssize_t ib_ucm_event(struct ib_ucm_file *file,
			    const char __user *inbuf,
			    int in_len, int out_len)
{
	struct ib_ucm_context *ctx;
	struct ib_ucm_event_get cmd;
	struct ib_ucm_event *uevent;
	int result = 0;
	DEFINE_WAIT(wait);

	if (out_len < sizeof(struct ib_ucm_event_resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	mutex_lock(&file->file_mutex);
	while (list_empty(&file->events)) {

		if (file->filp->f_flags & O_NONBLOCK) {
			result = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			result = -ERESTARTSYS;
			break;
		}

		prepare_to_wait(&file->poll_wait, &wait, TASK_INTERRUPTIBLE);

		mutex_unlock(&file->file_mutex);
		schedule();
		mutex_lock(&file->file_mutex);

		finish_wait(&file->poll_wait, &wait);
	}

	if (result)
		goto done;

	uevent = list_entry(file->events.next, struct ib_ucm_event, file_list);

	if (ib_ucm_new_cm_id(uevent->resp.event)) {
		ctx = ib_ucm_ctx_alloc(file);
		if (!ctx) {
			result = -ENOMEM;
			goto done;
		}

		ctx->cm_id = uevent->cm_id;
		ctx->cm_id->context = ctx;
		uevent->resp.id = ctx->id;
	}

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &uevent->resp, sizeof(uevent->resp))) {
		result = -EFAULT;
		goto done;
	}

	if (uevent->data) {
		if (cmd.data_len < uevent->data_len) {
			result = -ENOMEM;
			goto done;
		}
		if (copy_to_user((void __user *)(unsigned long)cmd.data,
				 uevent->data, uevent->data_len)) {
			result = -EFAULT;
			goto done;
		}
	}

	if (uevent->info) {
		if (cmd.info_len < uevent->info_len) {
			result = -ENOMEM;
			goto done;
		}
		if (copy_to_user((void __user *)(unsigned long)cmd.info,
				 uevent->info, uevent->info_len)) {
			result = -EFAULT;
			goto done;
		}
	}

	list_del(&uevent->file_list);
	list_del(&uevent->ctx_list);
	uevent->ctx->events_reported++;

	kfree(uevent->data);
	kfree(uevent->info);
	kfree(uevent);
done:
	mutex_unlock(&file->file_mutex);
	return result;
}

static ssize_t ib_ucm_create_id(struct ib_ucm_file *file,
				const char __user *inbuf,
				int in_len, int out_len)
{
	struct ib_ucm_create_id cmd;
	struct ib_ucm_create_id_resp resp;
	struct ib_ucm_context *ctx;
	int result;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	mutex_lock(&file->file_mutex);
	ctx = ib_ucm_ctx_alloc(file);
	mutex_unlock(&file->file_mutex);
	if (!ctx)
		return -ENOMEM;

	ctx->uid = cmd.uid;
	ctx->cm_id = ib_create_cm_id(file->device->ib_dev,
				     ib_ucm_event_handler, ctx);
	if (IS_ERR(ctx->cm_id)) {
		result = PTR_ERR(ctx->cm_id);
		goto err1;
	}

	resp.id = ctx->id;
	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp))) {
		result = -EFAULT;
		goto err2;
	}
	return 0;

err2:
	ib_destroy_cm_id(ctx->cm_id);
err1:
	mutex_lock(&ctx_id_mutex);
	idr_remove(&ctx_id_table, ctx->id);
	mutex_unlock(&ctx_id_mutex);
	kfree(ctx);
	return result;
}

static ssize_t ib_ucm_destroy_id(struct ib_ucm_file *file,
				 const char __user *inbuf,
				 int in_len, int out_len)
{
	struct ib_ucm_destroy_id cmd;
	struct ib_ucm_destroy_id_resp resp;
	struct ib_ucm_context *ctx;
	int result = 0;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	mutex_lock(&ctx_id_mutex);
	ctx = idr_find(&ctx_id_table, cmd.id);
	if (!ctx)
		ctx = ERR_PTR(-ENOENT);
	else if (ctx->file != file)
		ctx = ERR_PTR(-EINVAL);
	else
		idr_remove(&ctx_id_table, ctx->id);
	mutex_unlock(&ctx_id_mutex);

	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ib_ucm_ctx_put(ctx);
	wait_for_completion(&ctx->comp);

	/* No new events will be generated after destroying the cm_id. */
	ib_destroy_cm_id(ctx->cm_id);
	/* Cleanup events not yet reported to the user. */
	ib_ucm_cleanup_events(ctx);

	resp.events_reported = ctx->events_reported;
	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		result = -EFAULT;

	kfree(ctx);
	return result;
}

static ssize_t ib_ucm_attr_id(struct ib_ucm_file *file,
			      const char __user *inbuf,
			      int in_len, int out_len)
{
	struct ib_ucm_attr_id_resp resp;
	struct ib_ucm_attr_id cmd;
	struct ib_ucm_context *ctx;
	int result = 0;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	resp.service_id   = ctx->cm_id->service_id;
	resp.service_mask = ctx->cm_id->service_mask;
	resp.local_id     = ctx->cm_id->local_id;
	resp.remote_id    = ctx->cm_id->remote_id;

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		result = -EFAULT;

	ib_ucm_ctx_put(ctx);
	return result;
}

static ssize_t ib_ucm_init_qp_attr(struct ib_ucm_file *file,
				   const char __user *inbuf,
				   int in_len, int out_len)
{
	struct ib_uverbs_qp_attr resp;
	struct ib_ucm_init_qp_attr cmd;
	struct ib_ucm_context *ctx;
	struct ib_qp_attr qp_attr;
	int result = 0;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	resp.qp_attr_mask = 0;
	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.qp_state = cmd.qp_state;
	result = ib_cm_init_qp_attr(ctx->cm_id, &qp_attr, &resp.qp_attr_mask);
	if (result)
		goto out;

	ib_copy_qp_attr_to_user(&resp, &qp_attr);

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		result = -EFAULT;

out:
	ib_ucm_ctx_put(ctx);
	return result;
}

static int ucm_validate_listen(__be64 service_id, __be64 service_mask)
{
	service_id &= service_mask;

	if (((service_id & IB_CMA_SERVICE_ID_MASK) == IB_CMA_SERVICE_ID) ||
	    ((service_id & IB_SDP_SERVICE_ID_MASK) == IB_SDP_SERVICE_ID))
		return -EINVAL;

	return 0;
}

static ssize_t ib_ucm_listen(struct ib_ucm_file *file,
			     const char __user *inbuf,
			     int in_len, int out_len)
{
	struct ib_ucm_listen cmd;
	struct ib_ucm_context *ctx;
	int result;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	result = ucm_validate_listen(cmd.service_id, cmd.service_mask);
	if (result)
		goto out;

	result = ib_cm_listen(ctx->cm_id, cmd.service_id, cmd.service_mask,
			      NULL);
out:
	ib_ucm_ctx_put(ctx);
	return result;
}

static ssize_t ib_ucm_notify(struct ib_ucm_file *file,
			     const char __user *inbuf,
			     int in_len, int out_len)
{
	struct ib_ucm_notify cmd;
	struct ib_ucm_context *ctx;
	int result;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	result = ib_cm_notify(ctx->cm_id, (enum ib_event_type) cmd.event);
	ib_ucm_ctx_put(ctx);
	return result;
}

static int ib_ucm_alloc_data(const void **dest, u64 src, u32 len)
{
	void *data;

	*dest = NULL;

	if (!len)
		return 0;

	data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (copy_from_user(data, (void __user *)(unsigned long)src, len)) {
		kfree(data);
		return -EFAULT;
	}

	*dest = data;
	return 0;
}

static int ib_ucm_path_get(struct ib_sa_path_rec **path, u64 src)
{
	struct ib_user_path_rec upath;
	struct ib_sa_path_rec  *sa_path;

	*path = NULL;

	if (!src)
		return 0;

	sa_path = kmalloc(sizeof(*sa_path), GFP_KERNEL);
	if (!sa_path)
		return -ENOMEM;

	if (copy_from_user(&upath, (void __user *)(unsigned long)src,
			   sizeof(upath))) {

		kfree(sa_path);
		return -EFAULT;
	}

	ib_copy_path_rec_from_user(sa_path, &upath);
	*path = sa_path;
	return 0;
}

static ssize_t ib_ucm_send_req(struct ib_ucm_file *file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	struct ib_cm_req_param param;
	struct ib_ucm_context *ctx;
	struct ib_ucm_req cmd;
	int result;

	param.private_data   = NULL;
	param.primary_path   = NULL;
	param.alternate_path = NULL;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	result = ib_ucm_alloc_data(&param.private_data, cmd.data, cmd.len);
	if (result)
		goto done;

	result = ib_ucm_path_get(&param.primary_path, cmd.primary_path);
	if (result)
		goto done;

	result = ib_ucm_path_get(&param.alternate_path, cmd.alternate_path);
	if (result)
		goto done;

	param.private_data_len           = cmd.len;
	param.service_id                 = cmd.sid;
	param.qp_num                     = cmd.qpn;
	param.qp_type                    = cmd.qp_type;
	param.starting_psn               = cmd.psn;
	param.peer_to_peer               = cmd.peer_to_peer;
	param.responder_resources        = cmd.responder_resources;
	param.initiator_depth            = cmd.initiator_depth;
	param.remote_cm_response_timeout = cmd.remote_cm_response_timeout;
	param.flow_control               = cmd.flow_control;
	param.local_cm_response_timeout  = cmd.local_cm_response_timeout;
	param.retry_count                = cmd.retry_count;
	param.rnr_retry_count            = cmd.rnr_retry_count;
	param.max_cm_retries             = cmd.max_cm_retries;
	param.srq                        = cmd.srq;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (!IS_ERR(ctx)) {
		result = ib_send_cm_req(ctx->cm_id, &param);
		ib_ucm_ctx_put(ctx);
	} else
		result = PTR_ERR(ctx);

done:
	kfree(param.private_data);
	kfree(param.primary_path);
	kfree(param.alternate_path);
	return result;
}

static ssize_t ib_ucm_send_rep(struct ib_ucm_file *file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	struct ib_cm_rep_param param;
	struct ib_ucm_context *ctx;
	struct ib_ucm_rep cmd;
	int result;

	param.private_data = NULL;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	result = ib_ucm_alloc_data(&param.private_data, cmd.data, cmd.len);
	if (result)
		return result;

	param.qp_num              = cmd.qpn;
	param.starting_psn        = cmd.psn;
	param.private_data_len    = cmd.len;
	param.responder_resources = cmd.responder_resources;
	param.initiator_depth     = cmd.initiator_depth;
	param.target_ack_delay    = cmd.target_ack_delay;
	param.failover_accepted   = cmd.failover_accepted;
	param.flow_control        = cmd.flow_control;
	param.rnr_retry_count     = cmd.rnr_retry_count;
	param.srq                 = cmd.srq;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (!IS_ERR(ctx)) {
		ctx->uid = cmd.uid;
		result = ib_send_cm_rep(ctx->cm_id, &param);
		ib_ucm_ctx_put(ctx);
	} else
		result = PTR_ERR(ctx);

	kfree(param.private_data);
	return result;
}

static ssize_t ib_ucm_send_private_data(struct ib_ucm_file *file,
					const char __user *inbuf, int in_len,
					int (*func)(struct ib_cm_id *cm_id,
						    const void *private_data,
						    u8 private_data_len))
{
	struct ib_ucm_private_data cmd;
	struct ib_ucm_context *ctx;
	const void *private_data = NULL;
	int result;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	result = ib_ucm_alloc_data(&private_data, cmd.data, cmd.len);
	if (result)
		return result;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (!IS_ERR(ctx)) {
		result = func(ctx->cm_id, private_data, cmd.len);
		ib_ucm_ctx_put(ctx);
	} else
		result = PTR_ERR(ctx);

	kfree(private_data);
	return result;
}

static ssize_t ib_ucm_send_rtu(struct ib_ucm_file *file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	return ib_ucm_send_private_data(file, inbuf, in_len, ib_send_cm_rtu);
}

static ssize_t ib_ucm_send_dreq(struct ib_ucm_file *file,
				const char __user *inbuf,
				int in_len, int out_len)
{
	return ib_ucm_send_private_data(file, inbuf, in_len, ib_send_cm_dreq);
}

static ssize_t ib_ucm_send_drep(struct ib_ucm_file *file,
				const char __user *inbuf,
				int in_len, int out_len)
{
	return ib_ucm_send_private_data(file, inbuf, in_len, ib_send_cm_drep);
}

static ssize_t ib_ucm_send_info(struct ib_ucm_file *file,
				const char __user *inbuf, int in_len,
				int (*func)(struct ib_cm_id *cm_id,
					    int status,
					    const void *info,
					    u8 info_len,
					    const void *data,
					    u8 data_len))
{
	struct ib_ucm_context *ctx;
	struct ib_ucm_info cmd;
	const void *data = NULL;
	const void *info = NULL;
	int result;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	result = ib_ucm_alloc_data(&data, cmd.data, cmd.data_len);
	if (result)
		goto done;

	result = ib_ucm_alloc_data(&info, cmd.info, cmd.info_len);
	if (result)
		goto done;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (!IS_ERR(ctx)) {
		result = func(ctx->cm_id, cmd.status, info, cmd.info_len,
			      data, cmd.data_len);
		ib_ucm_ctx_put(ctx);
	} else
		result = PTR_ERR(ctx);

done:
	kfree(data);
	kfree(info);
	return result;
}

static ssize_t ib_ucm_send_rej(struct ib_ucm_file *file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	return ib_ucm_send_info(file, inbuf, in_len, (void *)ib_send_cm_rej);
}

static ssize_t ib_ucm_send_apr(struct ib_ucm_file *file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	return ib_ucm_send_info(file, inbuf, in_len, (void *)ib_send_cm_apr);
}

static ssize_t ib_ucm_send_mra(struct ib_ucm_file *file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	struct ib_ucm_context *ctx;
	struct ib_ucm_mra cmd;
	const void *data = NULL;
	int result;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	result = ib_ucm_alloc_data(&data, cmd.data, cmd.len);
	if (result)
		return result;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (!IS_ERR(ctx)) {
		result = ib_send_cm_mra(ctx->cm_id, cmd.timeout, data, cmd.len);
		ib_ucm_ctx_put(ctx);
	} else
		result = PTR_ERR(ctx);

	kfree(data);
	return result;
}

static ssize_t ib_ucm_send_lap(struct ib_ucm_file *file,
			       const char __user *inbuf,
			       int in_len, int out_len)
{
	struct ib_ucm_context *ctx;
	struct ib_sa_path_rec *path = NULL;
	struct ib_ucm_lap cmd;
	const void *data = NULL;
	int result;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	result = ib_ucm_alloc_data(&data, cmd.data, cmd.len);
	if (result)
		goto done;

	result = ib_ucm_path_get(&path, cmd.path);
	if (result)
		goto done;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (!IS_ERR(ctx)) {
		result = ib_send_cm_lap(ctx->cm_id, path, data, cmd.len);
		ib_ucm_ctx_put(ctx);
	} else
		result = PTR_ERR(ctx);

done:
	kfree(data);
	kfree(path);
	return result;
}

static ssize_t ib_ucm_send_sidr_req(struct ib_ucm_file *file,
				    const char __user *inbuf,
				    int in_len, int out_len)
{
	struct ib_cm_sidr_req_param param;
	struct ib_ucm_context *ctx;
	struct ib_ucm_sidr_req cmd;
	int result;

	param.private_data = NULL;
	param.path = NULL;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	result = ib_ucm_alloc_data(&param.private_data, cmd.data, cmd.len);
	if (result)
		goto done;

	result = ib_ucm_path_get(&param.path, cmd.path);
	if (result)
		goto done;

	param.private_data_len = cmd.len;
	param.service_id       = cmd.sid;
	param.timeout_ms       = cmd.timeout;
	param.max_cm_retries   = cmd.max_cm_retries;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (!IS_ERR(ctx)) {
		result = ib_send_cm_sidr_req(ctx->cm_id, &param);
		ib_ucm_ctx_put(ctx);
	} else
		result = PTR_ERR(ctx);

done:
	kfree(param.private_data);
	kfree(param.path);
	return result;
}

static ssize_t ib_ucm_send_sidr_rep(struct ib_ucm_file *file,
				    const char __user *inbuf,
				    int in_len, int out_len)
{
	struct ib_cm_sidr_rep_param param;
	struct ib_ucm_sidr_rep cmd;
	struct ib_ucm_context *ctx;
	int result;

	param.info = NULL;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	result = ib_ucm_alloc_data(&param.private_data,
				   cmd.data, cmd.data_len);
	if (result)
		goto done;

	result = ib_ucm_alloc_data(&param.info, cmd.info, cmd.info_len);
	if (result)
		goto done;

	param.qp_num		= cmd.qpn;
	param.qkey		= cmd.qkey;
	param.status		= cmd.status;
	param.info_length	= cmd.info_len;
	param.private_data_len	= cmd.data_len;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (!IS_ERR(ctx)) {
		result = ib_send_cm_sidr_rep(ctx->cm_id, &param);
		ib_ucm_ctx_put(ctx);
	} else
		result = PTR_ERR(ctx);

done:
	kfree(param.private_data);
	kfree(param.info);
	return result;
}

static ssize_t (*ucm_cmd_table[])(struct ib_ucm_file *file,
				  const char __user *inbuf,
				  int in_len, int out_len) = {
	[IB_USER_CM_CMD_CREATE_ID]     = ib_ucm_create_id,
	[IB_USER_CM_CMD_DESTROY_ID]    = ib_ucm_destroy_id,
	[IB_USER_CM_CMD_ATTR_ID]       = ib_ucm_attr_id,
	[IB_USER_CM_CMD_LISTEN]        = ib_ucm_listen,
	[IB_USER_CM_CMD_NOTIFY]        = ib_ucm_notify,
	[IB_USER_CM_CMD_SEND_REQ]      = ib_ucm_send_req,
	[IB_USER_CM_CMD_SEND_REP]      = ib_ucm_send_rep,
	[IB_USER_CM_CMD_SEND_RTU]      = ib_ucm_send_rtu,
	[IB_USER_CM_CMD_SEND_DREQ]     = ib_ucm_send_dreq,
	[IB_USER_CM_CMD_SEND_DREP]     = ib_ucm_send_drep,
	[IB_USER_CM_CMD_SEND_REJ]      = ib_ucm_send_rej,
	[IB_USER_CM_CMD_SEND_MRA]      = ib_ucm_send_mra,
	[IB_USER_CM_CMD_SEND_LAP]      = ib_ucm_send_lap,
	[IB_USER_CM_CMD_SEND_APR]      = ib_ucm_send_apr,
	[IB_USER_CM_CMD_SEND_SIDR_REQ] = ib_ucm_send_sidr_req,
	[IB_USER_CM_CMD_SEND_SIDR_REP] = ib_ucm_send_sidr_rep,
	[IB_USER_CM_CMD_EVENT]	       = ib_ucm_event,
	[IB_USER_CM_CMD_INIT_QP_ATTR]  = ib_ucm_init_qp_attr,
};

static ssize_t ib_ucm_write(struct file *filp, const char __user *buf,
			    size_t len, loff_t *pos)
{
	struct ib_ucm_file *file = filp->private_data;
	struct ib_ucm_cmd_hdr hdr;
	ssize_t result;

	if (len < sizeof(hdr))
		return -EINVAL;

	if (copy_from_user(&hdr, buf, sizeof(hdr)))
		return -EFAULT;

	if (hdr.cmd < 0 || hdr.cmd >= ARRAY_SIZE(ucm_cmd_table))
		return -EINVAL;

	if (hdr.in + sizeof(hdr) > len)
		return -EINVAL;

	result = ucm_cmd_table[hdr.cmd](file, buf + sizeof(hdr),
					hdr.in, hdr.out);
	if (!result)
		result = len;

	return result;
}

static unsigned int ib_ucm_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	struct ib_ucm_file *file = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &file->poll_wait, wait);

	if (!list_empty(&file->events))
		mask = POLLIN | POLLRDNORM;

	return mask;
}

static int ib_ucm_open(struct inode *inode, struct file *filp)
{
	struct ib_ucm_file *file;

	file = kmalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		return -ENOMEM;

	INIT_LIST_HEAD(&file->events);
	INIT_LIST_HEAD(&file->ctxs);
	init_waitqueue_head(&file->poll_wait);

	mutex_init(&file->file_mutex);

	filp->private_data = file;
	file->filp = filp;
	file->device = container_of(inode->i_cdev, struct ib_ucm_device, dev);

	return 0;
}

static int ib_ucm_close(struct inode *inode, struct file *filp)
{
	struct ib_ucm_file *file = filp->private_data;
	struct ib_ucm_context *ctx;

	mutex_lock(&file->file_mutex);
	while (!list_empty(&file->ctxs)) {
		ctx = list_entry(file->ctxs.next,
				 struct ib_ucm_context, file_list);
		mutex_unlock(&file->file_mutex);

		mutex_lock(&ctx_id_mutex);
		idr_remove(&ctx_id_table, ctx->id);
		mutex_unlock(&ctx_id_mutex);

		ib_destroy_cm_id(ctx->cm_id);
		ib_ucm_cleanup_events(ctx);
		kfree(ctx);

		mutex_lock(&file->file_mutex);
	}
	mutex_unlock(&file->file_mutex);
	kfree(file);
	return 0;
}

static void ib_ucm_release_class_dev(struct class_device *class_dev)
{
	struct ib_ucm_device *dev;

	dev = container_of(class_dev, struct ib_ucm_device, class_dev);
	cdev_del(&dev->dev);
	clear_bit(dev->devnum, dev_map);
	kfree(dev);
}

static struct file_operations ucm_fops = {
	.owner 	 = THIS_MODULE,
	.open 	 = ib_ucm_open,
	.release = ib_ucm_close,
	.write 	 = ib_ucm_write,
	.poll    = ib_ucm_poll,
};

static struct class ucm_class = {
	.name    = "infiniband_cm",
	.release = ib_ucm_release_class_dev
};

static ssize_t show_ibdev(struct class_device *class_dev, char *buf)
{
	struct ib_ucm_device *dev;

	dev = container_of(class_dev, struct ib_ucm_device, class_dev);
	return sprintf(buf, "%s\n", dev->ib_dev->name);
}
static CLASS_DEVICE_ATTR(ibdev, S_IRUGO, show_ibdev, NULL);

static void ib_ucm_add_one(struct ib_device *device)
{
	struct ib_ucm_device *ucm_dev;

	if (!device->alloc_ucontext ||
	    rdma_node_get_transport(device->node_type) != RDMA_TRANSPORT_IB)
		return;

	ucm_dev = kzalloc(sizeof *ucm_dev, GFP_KERNEL);
	if (!ucm_dev)
		return;

	ucm_dev->ib_dev = device;

	ucm_dev->devnum = find_first_zero_bit(dev_map, IB_UCM_MAX_DEVICES);
	if (ucm_dev->devnum >= IB_UCM_MAX_DEVICES)
		goto err;

	set_bit(ucm_dev->devnum, dev_map);

	cdev_init(&ucm_dev->dev, &ucm_fops);
	ucm_dev->dev.owner = THIS_MODULE;
	kobject_set_name(&ucm_dev->dev.kobj, "ucm%d", ucm_dev->devnum);
	if (cdev_add(&ucm_dev->dev, IB_UCM_BASE_DEV + ucm_dev->devnum, 1))
		goto err;

	ucm_dev->class_dev.class = &ucm_class;
	ucm_dev->class_dev.dev = device->dma_device;
	ucm_dev->class_dev.devt = ucm_dev->dev.dev;
	snprintf(ucm_dev->class_dev.class_id, BUS_ID_SIZE, "ucm%d",
		 ucm_dev->devnum);
	if (class_device_register(&ucm_dev->class_dev))
		goto err_cdev;

	if (class_device_create_file(&ucm_dev->class_dev,
				     &class_device_attr_ibdev))
		goto err_class;

	ib_set_client_data(device, &ucm_client, ucm_dev);
	return;

err_class:
	class_device_unregister(&ucm_dev->class_dev);
err_cdev:
	cdev_del(&ucm_dev->dev);
	clear_bit(ucm_dev->devnum, dev_map);
err:
	kfree(ucm_dev);
	return;
}

static void ib_ucm_remove_one(struct ib_device *device)
{
	struct ib_ucm_device *ucm_dev = ib_get_client_data(device, &ucm_client);

	if (!ucm_dev)
		return;

	class_device_unregister(&ucm_dev->class_dev);
}

static ssize_t show_abi_version(struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", IB_USER_CM_ABI_VERSION);
}
static CLASS_ATTR(abi_version, S_IRUGO, show_abi_version, NULL);

static int __init ib_ucm_init(void)
{
	int ret;

	ret = register_chrdev_region(IB_UCM_BASE_DEV, IB_UCM_MAX_DEVICES,
				     "infiniband_cm");
	if (ret) {
		printk(KERN_ERR "ucm: couldn't register device number\n");
		goto err;
	}

	ret = class_register(&ucm_class);
	if (ret) {
		printk(KERN_ERR "ucm: couldn't create class infiniband_cm\n");
		goto err_chrdev;
	}

	ret = class_create_file(&ucm_class, &class_attr_abi_version);
	if (ret) {
		printk(KERN_ERR "ucm: couldn't create abi_version attribute\n");
		goto err_class;
	}

	ret = ib_register_client(&ucm_client);
	if (ret) {
		printk(KERN_ERR "ucm: couldn't register client\n");
		goto err_class;
	}
	return 0;

err_class:
	class_unregister(&ucm_class);
err_chrdev:
	unregister_chrdev_region(IB_UCM_BASE_DEV, IB_UCM_MAX_DEVICES);
err:
	return ret;
}

static void __exit ib_ucm_cleanup(void)
{
	ib_unregister_client(&ucm_client);
	class_unregister(&ucm_class);
	unregister_chrdev_region(IB_UCM_BASE_DEV, IB_UCM_MAX_DEVICES);
	idr_destroy(&ctx_id_table);
}

module_init(ib_ucm_init);
module_exit(ib_ucm_cleanup);
