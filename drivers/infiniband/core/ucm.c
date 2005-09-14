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
 * $Id: ucm.c 2594 2005-06-13 19:46:02Z libor $
 */
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/cdev.h>

#include <asm/uaccess.h>

#include "ucm.h"

MODULE_AUTHOR("Libor Michalek");
MODULE_DESCRIPTION("InfiniBand userspace Connection Manager access");
MODULE_LICENSE("Dual BSD/GPL");

static int ucm_debug_level;

module_param_named(debug_level, ucm_debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Enable debug tracing if > 0");

enum {
	IB_UCM_MAJOR = 231,
	IB_UCM_MINOR = 255
};

#define IB_UCM_DEV MKDEV(IB_UCM_MAJOR, IB_UCM_MINOR)

#define PFX "UCM: "

#define ucm_dbg(format, arg...)			\
	do {					\
		if (ucm_debug_level > 0)	\
			printk(KERN_DEBUG PFX format, ## arg); \
	} while (0)

static struct semaphore ctx_id_mutex;
static struct idr       ctx_id_table;

static struct ib_ucm_context *ib_ucm_ctx_get(struct ib_ucm_file *file, int id)
{
	struct ib_ucm_context *ctx;

	down(&ctx_id_mutex);
	ctx = idr_find(&ctx_id_table, id);
	if (!ctx)
		ctx = ERR_PTR(-ENOENT);
	else if (ctx->file != file)
		ctx = ERR_PTR(-EINVAL);
	else
		atomic_inc(&ctx->ref);
	up(&ctx_id_mutex);

	return ctx;
}

static void ib_ucm_ctx_put(struct ib_ucm_context *ctx)
{
	if (atomic_dec_and_test(&ctx->ref))
		wake_up(&ctx->wait);
}

static inline int ib_ucm_new_cm_id(int event)
{
	return event == IB_CM_REQ_RECEIVED || event == IB_CM_SIDR_REQ_RECEIVED;
}

static void ib_ucm_cleanup_events(struct ib_ucm_context *ctx)
{
	struct ib_ucm_event *uevent;

	down(&ctx->file->mutex);
	list_del(&ctx->file_list);
	while (!list_empty(&ctx->events)) {

		uevent = list_entry(ctx->events.next,
				    struct ib_ucm_event, ctx_list);
		list_del(&uevent->file_list);
		list_del(&uevent->ctx_list);

		/* clear incoming connections. */
		if (ib_ucm_new_cm_id(uevent->resp.event))
			ib_destroy_cm_id(uevent->cm_id);

		kfree(uevent);
	}
	up(&ctx->file->mutex);
}

static struct ib_ucm_context *ib_ucm_ctx_alloc(struct ib_ucm_file *file)
{
	struct ib_ucm_context *ctx;
	int result;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	memset(ctx, 0, sizeof *ctx);
	atomic_set(&ctx->ref, 1);
	init_waitqueue_head(&ctx->wait);
	ctx->file = file;
	INIT_LIST_HEAD(&ctx->events);

	do {
		result = idr_pre_get(&ctx_id_table, GFP_KERNEL);
		if (!result)
			goto error;

		down(&ctx_id_mutex);
		result = idr_get_new(&ctx_id_table, ctx, &ctx->id);
		up(&ctx_id_mutex);
	} while (result == -EAGAIN);

	if (result)
		goto error;

	list_add_tail(&ctx->file_list, &file->ctxs);
	ucm_dbg("Allocated CM ID <%d>\n", ctx->id);
	return ctx;

error:
	kfree(ctx);
	return NULL;
}
/*
 * Event portion of the API, handle CM events
 * and allow event polling.
 */
static void ib_ucm_event_path_get(struct ib_ucm_path_rec *upath,
				  struct ib_sa_path_rec	 *kpath)
{
	if (!kpath || !upath)
		return;

	memcpy(upath->dgid, kpath->dgid.raw, sizeof *upath->dgid);
	memcpy(upath->sgid, kpath->sgid.raw, sizeof *upath->sgid);

	upath->dlid             = kpath->dlid;
	upath->slid             = kpath->slid;
	upath->raw_traffic      = kpath->raw_traffic;
	upath->flow_label       = kpath->flow_label;
	upath->hop_limit        = kpath->hop_limit;
	upath->traffic_class    = kpath->traffic_class;
	upath->reversible       = kpath->reversible;
	upath->numb_path        = kpath->numb_path;
	upath->pkey             = kpath->pkey;
	upath->sl	        = kpath->sl;
	upath->mtu_selector     = kpath->mtu_selector;
	upath->mtu              = kpath->mtu;
	upath->rate_selector    = kpath->rate_selector;
	upath->rate             = kpath->rate;
	upath->packet_life_time = kpath->packet_life_time;
	upath->preference       = kpath->preference;

	upath->packet_life_time_selector =
		kpath->packet_life_time_selector;
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

	ib_ucm_event_path_get(&ureq->primary_path, kreq->primary_path);
	ib_ucm_event_path_get(&ureq->alternate_path, kreq->alternate_path);
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
		ib_ucm_event_path_get(&uvt->resp.u.lap_resp.path,
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
		uvt->data = kmalloc(uvt->data_len, GFP_KERNEL);
		if (!uvt->data)
			goto err1;

		memcpy(uvt->data, evt->private_data, uvt->data_len);
		uvt->resp.present |= IB_UCM_PRES_DATA;
	}

	if (uvt->info_len) {
		uvt->info = kmalloc(uvt->info_len, GFP_KERNEL);
		if (!uvt->info)
			goto err2;

		memcpy(uvt->info, info, uvt->info_len);
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

	uevent = kmalloc(sizeof(*uevent), GFP_KERNEL);
	if (!uevent)
		goto err1;

	memset(uevent, 0, sizeof(*uevent));
	uevent->ctx = ctx;
	uevent->cm_id = cm_id;
	uevent->resp.uid = ctx->uid;
	uevent->resp.id = ctx->id;
	uevent->resp.event = event->event;

	result = ib_ucm_event_process(event, uevent);
	if (result)
		goto err2;

	down(&ctx->file->mutex);
	list_add_tail(&uevent->file_list, &ctx->file->events);
	list_add_tail(&uevent->ctx_list, &ctx->events);
	wake_up_interruptible(&ctx->file->poll_wait);
	up(&ctx->file->mutex);
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
	/*
	 * wait
	 */
	down(&file->mutex);
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

		up(&file->mutex);
		schedule();
		down(&file->mutex);

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
	up(&file->mutex);
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

	down(&file->mutex);
	ctx = ib_ucm_ctx_alloc(file);
	up(&file->mutex);
	if (!ctx)
		return -ENOMEM;

	ctx->uid = cmd.uid;
	ctx->cm_id = ib_create_cm_id(ib_ucm_event_handler, ctx);
	if (IS_ERR(ctx->cm_id)) {
		result = PTR_ERR(ctx->cm_id);
		goto err;
	}

	resp.id = ctx->id;
	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp))) {
		result = -EFAULT;
		goto err;
	}

	return 0;

err:
	down(&ctx_id_mutex);
	idr_remove(&ctx_id_table, ctx->id);
	up(&ctx_id_mutex);

	if (!IS_ERR(ctx->cm_id))
		ib_destroy_cm_id(ctx->cm_id);

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

	down(&ctx_id_mutex);
	ctx = idr_find(&ctx_id_table, cmd.id);
	if (!ctx)
		ctx = ERR_PTR(-ENOENT);
	else if (ctx->file != file)
		ctx = ERR_PTR(-EINVAL);
	else
		idr_remove(&ctx_id_table, ctx->id);
	up(&ctx_id_mutex);

	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	atomic_dec(&ctx->ref);
	wait_event(ctx->wait, !atomic_read(&ctx->ref));

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

static void ib_ucm_copy_ah_attr(struct ib_ucm_ah_attr *dest_attr,
				struct ib_ah_attr *src_attr)
{
	memcpy(dest_attr->grh_dgid, src_attr->grh.dgid.raw,
	       sizeof src_attr->grh.dgid);
	dest_attr->grh_flow_label = src_attr->grh.flow_label;
	dest_attr->grh_sgid_index = src_attr->grh.sgid_index;
	dest_attr->grh_hop_limit = src_attr->grh.hop_limit;
	dest_attr->grh_traffic_class = src_attr->grh.traffic_class;

	dest_attr->dlid = src_attr->dlid;
	dest_attr->sl = src_attr->sl;
	dest_attr->src_path_bits = src_attr->src_path_bits;
	dest_attr->static_rate = src_attr->static_rate;
	dest_attr->is_global = (src_attr->ah_flags & IB_AH_GRH);
	dest_attr->port_num = src_attr->port_num;
}

static void ib_ucm_copy_qp_attr(struct ib_ucm_init_qp_attr_resp *dest_attr,
				struct ib_qp_attr *src_attr)
{
	dest_attr->cur_qp_state = src_attr->cur_qp_state;
	dest_attr->path_mtu = src_attr->path_mtu;
	dest_attr->path_mig_state = src_attr->path_mig_state;
	dest_attr->qkey = src_attr->qkey;
	dest_attr->rq_psn = src_attr->rq_psn;
	dest_attr->sq_psn = src_attr->sq_psn;
	dest_attr->dest_qp_num = src_attr->dest_qp_num;
	dest_attr->qp_access_flags = src_attr->qp_access_flags;

	dest_attr->max_send_wr = src_attr->cap.max_send_wr;
	dest_attr->max_recv_wr = src_attr->cap.max_recv_wr;
	dest_attr->max_send_sge = src_attr->cap.max_send_sge;
	dest_attr->max_recv_sge = src_attr->cap.max_recv_sge;
	dest_attr->max_inline_data = src_attr->cap.max_inline_data;

	ib_ucm_copy_ah_attr(&dest_attr->ah_attr, &src_attr->ah_attr);
	ib_ucm_copy_ah_attr(&dest_attr->alt_ah_attr, &src_attr->alt_ah_attr);

	dest_attr->pkey_index = src_attr->pkey_index;
	dest_attr->alt_pkey_index = src_attr->alt_pkey_index;
	dest_attr->en_sqd_async_notify = src_attr->en_sqd_async_notify;
	dest_attr->sq_draining = src_attr->sq_draining;
	dest_attr->max_rd_atomic = src_attr->max_rd_atomic;
	dest_attr->max_dest_rd_atomic = src_attr->max_dest_rd_atomic;
	dest_attr->min_rnr_timer = src_attr->min_rnr_timer;
	dest_attr->port_num = src_attr->port_num;
	dest_attr->timeout = src_attr->timeout;
	dest_attr->retry_cnt = src_attr->retry_cnt;
	dest_attr->rnr_retry = src_attr->rnr_retry;
	dest_attr->alt_port_num = src_attr->alt_port_num;
	dest_attr->alt_timeout = src_attr->alt_timeout;
}

static ssize_t ib_ucm_init_qp_attr(struct ib_ucm_file *file,
				   const char __user *inbuf,
				   int in_len, int out_len)
{
	struct ib_ucm_init_qp_attr_resp resp;
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

	ib_ucm_copy_qp_attr(&resp, &qp_attr);

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		result = -EFAULT;

out:
	ib_ucm_ctx_put(ctx);
	return result;
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

	result = ib_cm_listen(ctx->cm_id, cmd.service_id, cmd.service_mask);
	ib_ucm_ctx_put(ctx);
	return result;
}

static ssize_t ib_ucm_establish(struct ib_ucm_file *file,
				const char __user *inbuf,
				int in_len, int out_len)
{
	struct ib_ucm_establish cmd;
	struct ib_ucm_context *ctx;
	int result;

	if (copy_from_user(&cmd, inbuf, sizeof(cmd)))
		return -EFAULT;

	ctx = ib_ucm_ctx_get(file, cmd.id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	result = ib_cm_establish(ctx->cm_id);
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
	struct ib_ucm_path_rec ucm_path;
	struct ib_sa_path_rec  *sa_path;

	*path = NULL;

	if (!src)
		return 0;

	sa_path = kmalloc(sizeof(*sa_path), GFP_KERNEL);
	if (!sa_path)
		return -ENOMEM;

	if (copy_from_user(&ucm_path, (void __user *)(unsigned long)src,
			   sizeof(ucm_path))) {

		kfree(sa_path);
		return -EFAULT;
	}

	memcpy(sa_path->dgid.raw, ucm_path.dgid, sizeof sa_path->dgid);
	memcpy(sa_path->sgid.raw, ucm_path.sgid, sizeof sa_path->sgid);

	sa_path->dlid	          = ucm_path.dlid;
	sa_path->slid	          = ucm_path.slid;
	sa_path->raw_traffic      = ucm_path.raw_traffic;
	sa_path->flow_label       = ucm_path.flow_label;
	sa_path->hop_limit        = ucm_path.hop_limit;
	sa_path->traffic_class    = ucm_path.traffic_class;
	sa_path->reversible       = ucm_path.reversible;
	sa_path->numb_path        = ucm_path.numb_path;
	sa_path->pkey             = ucm_path.pkey;
	sa_path->sl               = ucm_path.sl;
	sa_path->mtu_selector     = ucm_path.mtu_selector;
	sa_path->mtu              = ucm_path.mtu;
	sa_path->rate_selector    = ucm_path.rate_selector;
	sa_path->rate             = ucm_path.rate;
	sa_path->packet_life_time = ucm_path.packet_life_time;
	sa_path->preference       = ucm_path.preference;

	sa_path->packet_life_time_selector =
		ucm_path.packet_life_time_selector;

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
	param.pkey             = cmd.pkey;

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
	[IB_USER_CM_CMD_ESTABLISH]     = ib_ucm_establish,
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

	ucm_dbg("Write. cmd <%d> in <%d> out <%d> len <%Zu>\n",
		hdr.cmd, hdr.in, hdr.out, len);

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

	init_MUTEX(&file->mutex);

	filp->private_data = file;
	file->filp = filp;

	ucm_dbg("Created struct\n");

	return 0;
}

static int ib_ucm_close(struct inode *inode, struct file *filp)
{
	struct ib_ucm_file *file = filp->private_data;
	struct ib_ucm_context *ctx;

	down(&file->mutex);
	while (!list_empty(&file->ctxs)) {
		ctx = list_entry(file->ctxs.next,
				 struct ib_ucm_context, file_list);
		up(&file->mutex);

		down(&ctx_id_mutex);
		idr_remove(&ctx_id_table, ctx->id);
		up(&ctx_id_mutex);

		ib_destroy_cm_id(ctx->cm_id);
		ib_ucm_cleanup_events(ctx);
		kfree(ctx);

		down(&file->mutex);
	}
	up(&file->mutex);
	kfree(file);
	return 0;
}

static struct file_operations ib_ucm_fops = {
	.owner 	 = THIS_MODULE,
	.open 	 = ib_ucm_open,
	.release = ib_ucm_close,
	.write 	 = ib_ucm_write,
	.poll    = ib_ucm_poll,
};


static struct class *ib_ucm_class;
static struct cdev	  ib_ucm_cdev;

static int __init ib_ucm_init(void)
{
	int result;

	result = register_chrdev_region(IB_UCM_DEV, 1, "infiniband_cm");
	if (result) {
		ucm_dbg("Error <%d> registering dev\n", result);
		goto err_chr;
	}

	cdev_init(&ib_ucm_cdev, &ib_ucm_fops);

	result = cdev_add(&ib_ucm_cdev, IB_UCM_DEV, 1);
	if (result) {
 		ucm_dbg("Error <%d> adding cdev\n", result);
		goto err_cdev;
	}

	ib_ucm_class = class_create(THIS_MODULE, "infiniband_cm");
	if (IS_ERR(ib_ucm_class)) {
		result = PTR_ERR(ib_ucm_class);
 		ucm_dbg("Error <%d> creating class\n", result);
		goto err_class;
	}

	class_device_create(ib_ucm_class, IB_UCM_DEV, NULL, "ucm");

	idr_init(&ctx_id_table);
	init_MUTEX(&ctx_id_mutex);

	return 0;
err_class:
	cdev_del(&ib_ucm_cdev);
err_cdev:
	unregister_chrdev_region(IB_UCM_DEV, 1);
err_chr:
	return result;
}

static void __exit ib_ucm_cleanup(void)
{
	class_device_destroy(ib_ucm_class, IB_UCM_DEV);
	class_destroy(ib_ucm_class);
	cdev_del(&ib_ucm_cdev);
	unregister_chrdev_region(IB_UCM_DEV, 1);
}

module_init(ib_ucm_init);
module_exit(ib_ucm_cleanup);
