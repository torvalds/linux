/*
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/jiffies.h>

#include <linux/atomic.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_tcq.h>
#include <scsi/srp.h>
#include <scsi/scsi_transport_srp.h>

#include "ib_srp.h"

#define DRV_NAME	"ib_srp"
#define PFX		DRV_NAME ": "
#define DRV_VERSION	"1.0"
#define DRV_RELDATE	"July 1, 2013"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("InfiniBand SCSI RDMA Protocol initiator "
		   "v" DRV_VERSION " (" DRV_RELDATE ")");
MODULE_LICENSE("Dual BSD/GPL");

static unsigned int srp_sg_tablesize;
static unsigned int cmd_sg_entries;
static unsigned int indirect_sg_entries;
static bool allow_ext_sg;
static bool prefer_fr;
static bool register_always;
static int topspin_workarounds = 1;

module_param(srp_sg_tablesize, uint, 0444);
MODULE_PARM_DESC(srp_sg_tablesize, "Deprecated name for cmd_sg_entries");

module_param(cmd_sg_entries, uint, 0444);
MODULE_PARM_DESC(cmd_sg_entries,
		 "Default number of gather/scatter entries in the SRP command (default is 12, max 255)");

module_param(indirect_sg_entries, uint, 0444);
MODULE_PARM_DESC(indirect_sg_entries,
		 "Default max number of gather/scatter entries (default is 12, max is " __stringify(SCSI_MAX_SG_CHAIN_SEGMENTS) ")");

module_param(allow_ext_sg, bool, 0444);
MODULE_PARM_DESC(allow_ext_sg,
		  "Default behavior when there are more than cmd_sg_entries S/G entries after mapping; fails the request when false (default false)");

module_param(topspin_workarounds, int, 0444);
MODULE_PARM_DESC(topspin_workarounds,
		 "Enable workarounds for Topspin/Cisco SRP target bugs if != 0");

module_param(prefer_fr, bool, 0444);
MODULE_PARM_DESC(prefer_fr,
"Whether to use fast registration if both FMR and fast registration are supported");

module_param(register_always, bool, 0444);
MODULE_PARM_DESC(register_always,
		 "Use memory registration even for contiguous memory regions");

static struct kernel_param_ops srp_tmo_ops;

static int srp_reconnect_delay = 10;
module_param_cb(reconnect_delay, &srp_tmo_ops, &srp_reconnect_delay,
		S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(reconnect_delay, "Time between successive reconnect attempts");

static int srp_fast_io_fail_tmo = 15;
module_param_cb(fast_io_fail_tmo, &srp_tmo_ops, &srp_fast_io_fail_tmo,
		S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(fast_io_fail_tmo,
		 "Number of seconds between the observation of a transport"
		 " layer error and failing all I/O. \"off\" means that this"
		 " functionality is disabled.");

static int srp_dev_loss_tmo = 600;
module_param_cb(dev_loss_tmo, &srp_tmo_ops, &srp_dev_loss_tmo,
		S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dev_loss_tmo,
		 "Maximum number of seconds that the SRP transport should"
		 " insulate transport layer errors. After this time has been"
		 " exceeded the SCSI host is removed. Should be"
		 " between 1 and " __stringify(SCSI_DEVICE_BLOCK_MAX_TIMEOUT)
		 " if fast_io_fail_tmo has not been set. \"off\" means that"
		 " this functionality is disabled.");

static void srp_add_one(struct ib_device *device);
static void srp_remove_one(struct ib_device *device);
static void srp_recv_completion(struct ib_cq *cq, void *target_ptr);
static void srp_send_completion(struct ib_cq *cq, void *target_ptr);
static int srp_cm_handler(struct ib_cm_id *cm_id, struct ib_cm_event *event);

static struct scsi_transport_template *ib_srp_transport_template;
static struct workqueue_struct *srp_remove_wq;

static struct ib_client srp_client = {
	.name   = "srp",
	.add    = srp_add_one,
	.remove = srp_remove_one
};

static struct ib_sa_client srp_sa_client;

static int srp_tmo_get(char *buffer, const struct kernel_param *kp)
{
	int tmo = *(int *)kp->arg;

	if (tmo >= 0)
		return sprintf(buffer, "%d", tmo);
	else
		return sprintf(buffer, "off");
}

static int srp_tmo_set(const char *val, const struct kernel_param *kp)
{
	int tmo, res;

	if (strncmp(val, "off", 3) != 0) {
		res = kstrtoint(val, 0, &tmo);
		if (res)
			goto out;
	} else {
		tmo = -1;
	}
	if (kp->arg == &srp_reconnect_delay)
		res = srp_tmo_valid(tmo, srp_fast_io_fail_tmo,
				    srp_dev_loss_tmo);
	else if (kp->arg == &srp_fast_io_fail_tmo)
		res = srp_tmo_valid(srp_reconnect_delay, tmo, srp_dev_loss_tmo);
	else
		res = srp_tmo_valid(srp_reconnect_delay, srp_fast_io_fail_tmo,
				    tmo);
	if (res)
		goto out;
	*(int *)kp->arg = tmo;

out:
	return res;
}

static struct kernel_param_ops srp_tmo_ops = {
	.get = srp_tmo_get,
	.set = srp_tmo_set,
};

static inline struct srp_target_port *host_to_target(struct Scsi_Host *host)
{
	return (struct srp_target_port *) host->hostdata;
}

static const char *srp_target_info(struct Scsi_Host *host)
{
	return host_to_target(host)->target_name;
}

static int srp_target_is_topspin(struct srp_target_port *target)
{
	static const u8 topspin_oui[3] = { 0x00, 0x05, 0xad };
	static const u8 cisco_oui[3]   = { 0x00, 0x1b, 0x0d };

	return topspin_workarounds &&
		(!memcmp(&target->ioc_guid, topspin_oui, sizeof topspin_oui) ||
		 !memcmp(&target->ioc_guid, cisco_oui, sizeof cisco_oui));
}

static struct srp_iu *srp_alloc_iu(struct srp_host *host, size_t size,
				   gfp_t gfp_mask,
				   enum dma_data_direction direction)
{
	struct srp_iu *iu;

	iu = kmalloc(sizeof *iu, gfp_mask);
	if (!iu)
		goto out;

	iu->buf = kzalloc(size, gfp_mask);
	if (!iu->buf)
		goto out_free_iu;

	iu->dma = ib_dma_map_single(host->srp_dev->dev, iu->buf, size,
				    direction);
	if (ib_dma_mapping_error(host->srp_dev->dev, iu->dma))
		goto out_free_buf;

	iu->size      = size;
	iu->direction = direction;

	return iu;

out_free_buf:
	kfree(iu->buf);
out_free_iu:
	kfree(iu);
out:
	return NULL;
}

static void srp_free_iu(struct srp_host *host, struct srp_iu *iu)
{
	if (!iu)
		return;

	ib_dma_unmap_single(host->srp_dev->dev, iu->dma, iu->size,
			    iu->direction);
	kfree(iu->buf);
	kfree(iu);
}

static void srp_qp_event(struct ib_event *event, void *context)
{
	pr_debug("QP event %d\n", event->event);
}

static int srp_init_qp(struct srp_target_port *target,
		       struct ib_qp *qp)
{
	struct ib_qp_attr *attr;
	int ret;

	attr = kmalloc(sizeof *attr, GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	ret = ib_find_pkey(target->srp_host->srp_dev->dev,
			   target->srp_host->port,
			   be16_to_cpu(target->path.pkey),
			   &attr->pkey_index);
	if (ret)
		goto out;

	attr->qp_state        = IB_QPS_INIT;
	attr->qp_access_flags = (IB_ACCESS_REMOTE_READ |
				    IB_ACCESS_REMOTE_WRITE);
	attr->port_num        = target->srp_host->port;

	ret = ib_modify_qp(qp, attr,
			   IB_QP_STATE		|
			   IB_QP_PKEY_INDEX	|
			   IB_QP_ACCESS_FLAGS	|
			   IB_QP_PORT);

out:
	kfree(attr);
	return ret;
}

static int srp_new_cm_id(struct srp_target_port *target)
{
	struct ib_cm_id *new_cm_id;

	new_cm_id = ib_create_cm_id(target->srp_host->srp_dev->dev,
				    srp_cm_handler, target);
	if (IS_ERR(new_cm_id))
		return PTR_ERR(new_cm_id);

	if (target->cm_id)
		ib_destroy_cm_id(target->cm_id);
	target->cm_id = new_cm_id;

	return 0;
}

static struct ib_fmr_pool *srp_alloc_fmr_pool(struct srp_target_port *target)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_fmr_pool_param fmr_param;

	memset(&fmr_param, 0, sizeof(fmr_param));
	fmr_param.pool_size	    = target->scsi_host->can_queue;
	fmr_param.dirty_watermark   = fmr_param.pool_size / 4;
	fmr_param.cache		    = 1;
	fmr_param.max_pages_per_fmr = dev->max_pages_per_mr;
	fmr_param.page_shift	    = ilog2(dev->mr_page_size);
	fmr_param.access	    = (IB_ACCESS_LOCAL_WRITE |
				       IB_ACCESS_REMOTE_WRITE |
				       IB_ACCESS_REMOTE_READ);

	return ib_create_fmr_pool(dev->pd, &fmr_param);
}

/**
 * srp_destroy_fr_pool() - free the resources owned by a pool
 * @pool: Fast registration pool to be destroyed.
 */
static void srp_destroy_fr_pool(struct srp_fr_pool *pool)
{
	int i;
	struct srp_fr_desc *d;

	if (!pool)
		return;

	for (i = 0, d = &pool->desc[0]; i < pool->size; i++, d++) {
		if (d->frpl)
			ib_free_fast_reg_page_list(d->frpl);
		if (d->mr)
			ib_dereg_mr(d->mr);
	}
	kfree(pool);
}

/**
 * srp_create_fr_pool() - allocate and initialize a pool for fast registration
 * @device:            IB device to allocate fast registration descriptors for.
 * @pd:                Protection domain associated with the FR descriptors.
 * @pool_size:         Number of descriptors to allocate.
 * @max_page_list_len: Maximum fast registration work request page list length.
 */
static struct srp_fr_pool *srp_create_fr_pool(struct ib_device *device,
					      struct ib_pd *pd, int pool_size,
					      int max_page_list_len)
{
	struct srp_fr_pool *pool;
	struct srp_fr_desc *d;
	struct ib_mr *mr;
	struct ib_fast_reg_page_list *frpl;
	int i, ret = -EINVAL;

	if (pool_size <= 0)
		goto err;
	ret = -ENOMEM;
	pool = kzalloc(sizeof(struct srp_fr_pool) +
		       pool_size * sizeof(struct srp_fr_desc), GFP_KERNEL);
	if (!pool)
		goto err;
	pool->size = pool_size;
	pool->max_page_list_len = max_page_list_len;
	spin_lock_init(&pool->lock);
	INIT_LIST_HEAD(&pool->free_list);

	for (i = 0, d = &pool->desc[0]; i < pool->size; i++, d++) {
		mr = ib_alloc_fast_reg_mr(pd, max_page_list_len);
		if (IS_ERR(mr)) {
			ret = PTR_ERR(mr);
			goto destroy_pool;
		}
		d->mr = mr;
		frpl = ib_alloc_fast_reg_page_list(device, max_page_list_len);
		if (IS_ERR(frpl)) {
			ret = PTR_ERR(frpl);
			goto destroy_pool;
		}
		d->frpl = frpl;
		list_add_tail(&d->entry, &pool->free_list);
	}

out:
	return pool;

destroy_pool:
	srp_destroy_fr_pool(pool);

err:
	pool = ERR_PTR(ret);
	goto out;
}

/**
 * srp_fr_pool_get() - obtain a descriptor suitable for fast registration
 * @pool: Pool to obtain descriptor from.
 */
static struct srp_fr_desc *srp_fr_pool_get(struct srp_fr_pool *pool)
{
	struct srp_fr_desc *d = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	if (!list_empty(&pool->free_list)) {
		d = list_first_entry(&pool->free_list, typeof(*d), entry);
		list_del(&d->entry);
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	return d;
}

/**
 * srp_fr_pool_put() - put an FR descriptor back in the free list
 * @pool: Pool the descriptor was allocated from.
 * @desc: Pointer to an array of fast registration descriptor pointers.
 * @n:    Number of descriptors to put back.
 *
 * Note: The caller must already have queued an invalidation request for
 * desc->mr->rkey before calling this function.
 */
static void srp_fr_pool_put(struct srp_fr_pool *pool, struct srp_fr_desc **desc,
			    int n)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&pool->lock, flags);
	for (i = 0; i < n; i++)
		list_add(&desc[i]->entry, &pool->free_list);
	spin_unlock_irqrestore(&pool->lock, flags);
}

static struct srp_fr_pool *srp_alloc_fr_pool(struct srp_target_port *target)
{
	struct srp_device *dev = target->srp_host->srp_dev;

	return srp_create_fr_pool(dev->dev, dev->pd,
				  target->scsi_host->can_queue,
				  dev->max_pages_per_mr);
}

static int srp_create_target_ib(struct srp_target_port *target)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_qp_init_attr *init_attr;
	struct ib_cq *recv_cq, *send_cq;
	struct ib_qp *qp;
	struct ib_fmr_pool *fmr_pool = NULL;
	struct srp_fr_pool *fr_pool = NULL;
	const int m = 1 + dev->use_fast_reg;
	int ret;

	init_attr = kzalloc(sizeof *init_attr, GFP_KERNEL);
	if (!init_attr)
		return -ENOMEM;

	recv_cq = ib_create_cq(dev->dev, srp_recv_completion, NULL, target,
			       target->queue_size, target->comp_vector);
	if (IS_ERR(recv_cq)) {
		ret = PTR_ERR(recv_cq);
		goto err;
	}

	send_cq = ib_create_cq(dev->dev, srp_send_completion, NULL, target,
			       m * target->queue_size, target->comp_vector);
	if (IS_ERR(send_cq)) {
		ret = PTR_ERR(send_cq);
		goto err_recv_cq;
	}

	ib_req_notify_cq(recv_cq, IB_CQ_NEXT_COMP);

	init_attr->event_handler       = srp_qp_event;
	init_attr->cap.max_send_wr     = m * target->queue_size;
	init_attr->cap.max_recv_wr     = target->queue_size;
	init_attr->cap.max_recv_sge    = 1;
	init_attr->cap.max_send_sge    = 1;
	init_attr->sq_sig_type         = IB_SIGNAL_REQ_WR;
	init_attr->qp_type             = IB_QPT_RC;
	init_attr->send_cq             = send_cq;
	init_attr->recv_cq             = recv_cq;

	qp = ib_create_qp(dev->pd, init_attr);
	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto err_send_cq;
	}

	ret = srp_init_qp(target, qp);
	if (ret)
		goto err_qp;

	if (dev->use_fast_reg && dev->has_fr) {
		fr_pool = srp_alloc_fr_pool(target);
		if (IS_ERR(fr_pool)) {
			ret = PTR_ERR(fr_pool);
			shost_printk(KERN_WARNING, target->scsi_host, PFX
				     "FR pool allocation failed (%d)\n", ret);
			goto err_qp;
		}
		if (target->fr_pool)
			srp_destroy_fr_pool(target->fr_pool);
		target->fr_pool = fr_pool;
	} else if (!dev->use_fast_reg && dev->has_fmr) {
		fmr_pool = srp_alloc_fmr_pool(target);
		if (IS_ERR(fmr_pool)) {
			ret = PTR_ERR(fmr_pool);
			shost_printk(KERN_WARNING, target->scsi_host, PFX
				     "FMR pool allocation failed (%d)\n", ret);
			goto err_qp;
		}
		if (target->fmr_pool)
			ib_destroy_fmr_pool(target->fmr_pool);
		target->fmr_pool = fmr_pool;
	}

	if (target->qp)
		ib_destroy_qp(target->qp);
	if (target->recv_cq)
		ib_destroy_cq(target->recv_cq);
	if (target->send_cq)
		ib_destroy_cq(target->send_cq);

	target->qp = qp;
	target->recv_cq = recv_cq;
	target->send_cq = send_cq;

	kfree(init_attr);
	return 0;

err_qp:
	ib_destroy_qp(qp);

err_send_cq:
	ib_destroy_cq(send_cq);

err_recv_cq:
	ib_destroy_cq(recv_cq);

err:
	kfree(init_attr);
	return ret;
}

/*
 * Note: this function may be called without srp_alloc_iu_bufs() having been
 * invoked. Hence the target->[rt]x_ring checks.
 */
static void srp_free_target_ib(struct srp_target_port *target)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	int i;

	if (dev->use_fast_reg) {
		if (target->fr_pool)
			srp_destroy_fr_pool(target->fr_pool);
	} else {
		if (target->fmr_pool)
			ib_destroy_fmr_pool(target->fmr_pool);
	}
	ib_destroy_qp(target->qp);
	ib_destroy_cq(target->send_cq);
	ib_destroy_cq(target->recv_cq);

	target->qp = NULL;
	target->send_cq = target->recv_cq = NULL;

	if (target->rx_ring) {
		for (i = 0; i < target->queue_size; ++i)
			srp_free_iu(target->srp_host, target->rx_ring[i]);
		kfree(target->rx_ring);
		target->rx_ring = NULL;
	}
	if (target->tx_ring) {
		for (i = 0; i < target->queue_size; ++i)
			srp_free_iu(target->srp_host, target->tx_ring[i]);
		kfree(target->tx_ring);
		target->tx_ring = NULL;
	}
}

static void srp_path_rec_completion(int status,
				    struct ib_sa_path_rec *pathrec,
				    void *target_ptr)
{
	struct srp_target_port *target = target_ptr;

	target->status = status;
	if (status)
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "Got failed path rec status %d\n", status);
	else
		target->path = *pathrec;
	complete(&target->done);
}

static int srp_lookup_path(struct srp_target_port *target)
{
	int ret;

	target->path.numb_path = 1;

	init_completion(&target->done);

	target->path_query_id = ib_sa_path_rec_get(&srp_sa_client,
						   target->srp_host->srp_dev->dev,
						   target->srp_host->port,
						   &target->path,
						   IB_SA_PATH_REC_SERVICE_ID	|
						   IB_SA_PATH_REC_DGID		|
						   IB_SA_PATH_REC_SGID		|
						   IB_SA_PATH_REC_NUMB_PATH	|
						   IB_SA_PATH_REC_PKEY,
						   SRP_PATH_REC_TIMEOUT_MS,
						   GFP_KERNEL,
						   srp_path_rec_completion,
						   target, &target->path_query);
	if (target->path_query_id < 0)
		return target->path_query_id;

	ret = wait_for_completion_interruptible(&target->done);
	if (ret < 0)
		return ret;

	if (target->status < 0)
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Path record query failed\n");

	return target->status;
}

static int srp_send_req(struct srp_target_port *target)
{
	struct {
		struct ib_cm_req_param param;
		struct srp_login_req   priv;
	} *req = NULL;
	int status;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->param.primary_path 	      = &target->path;
	req->param.alternate_path 	      = NULL;
	req->param.service_id 		      = target->service_id;
	req->param.qp_num 		      = target->qp->qp_num;
	req->param.qp_type 		      = target->qp->qp_type;
	req->param.private_data 	      = &req->priv;
	req->param.private_data_len 	      = sizeof req->priv;
	req->param.flow_control 	      = 1;

	get_random_bytes(&req->param.starting_psn, 4);
	req->param.starting_psn 	     &= 0xffffff;

	/*
	 * Pick some arbitrary defaults here; we could make these
	 * module parameters if anyone cared about setting them.
	 */
	req->param.responder_resources	      = 4;
	req->param.remote_cm_response_timeout = 20;
	req->param.local_cm_response_timeout  = 20;
	req->param.retry_count                = target->tl_retry_count;
	req->param.rnr_retry_count 	      = 7;
	req->param.max_cm_retries 	      = 15;

	req->priv.opcode     	= SRP_LOGIN_REQ;
	req->priv.tag        	= 0;
	req->priv.req_it_iu_len = cpu_to_be32(target->max_iu_len);
	req->priv.req_buf_fmt 	= cpu_to_be16(SRP_BUF_FORMAT_DIRECT |
					      SRP_BUF_FORMAT_INDIRECT);
	/*
	 * In the published SRP specification (draft rev. 16a), the
	 * port identifier format is 8 bytes of ID extension followed
	 * by 8 bytes of GUID.  Older drafts put the two halves in the
	 * opposite order, so that the GUID comes first.
	 *
	 * Targets conforming to these obsolete drafts can be
	 * recognized by the I/O Class they report.
	 */
	if (target->io_class == SRP_REV10_IB_IO_CLASS) {
		memcpy(req->priv.initiator_port_id,
		       &target->path.sgid.global.interface_id, 8);
		memcpy(req->priv.initiator_port_id + 8,
		       &target->initiator_ext, 8);
		memcpy(req->priv.target_port_id,     &target->ioc_guid, 8);
		memcpy(req->priv.target_port_id + 8, &target->id_ext, 8);
	} else {
		memcpy(req->priv.initiator_port_id,
		       &target->initiator_ext, 8);
		memcpy(req->priv.initiator_port_id + 8,
		       &target->path.sgid.global.interface_id, 8);
		memcpy(req->priv.target_port_id,     &target->id_ext, 8);
		memcpy(req->priv.target_port_id + 8, &target->ioc_guid, 8);
	}

	/*
	 * Topspin/Cisco SRP targets will reject our login unless we
	 * zero out the first 8 bytes of our initiator port ID and set
	 * the second 8 bytes to the local node GUID.
	 */
	if (srp_target_is_topspin(target)) {
		shost_printk(KERN_DEBUG, target->scsi_host,
			     PFX "Topspin/Cisco initiator port ID workaround "
			     "activated for target GUID %016llx\n",
			     (unsigned long long) be64_to_cpu(target->ioc_guid));
		memset(req->priv.initiator_port_id, 0, 8);
		memcpy(req->priv.initiator_port_id + 8,
		       &target->srp_host->srp_dev->dev->node_guid, 8);
	}

	status = ib_send_cm_req(target->cm_id, &req->param);

	kfree(req);

	return status;
}

static bool srp_queue_remove_work(struct srp_target_port *target)
{
	bool changed = false;

	spin_lock_irq(&target->lock);
	if (target->state != SRP_TARGET_REMOVED) {
		target->state = SRP_TARGET_REMOVED;
		changed = true;
	}
	spin_unlock_irq(&target->lock);

	if (changed)
		queue_work(srp_remove_wq, &target->remove_work);

	return changed;
}

static bool srp_change_conn_state(struct srp_target_port *target,
				  bool connected)
{
	bool changed = false;

	spin_lock_irq(&target->lock);
	if (target->connected != connected) {
		target->connected = connected;
		changed = true;
	}
	spin_unlock_irq(&target->lock);

	return changed;
}

static void srp_disconnect_target(struct srp_target_port *target)
{
	if (srp_change_conn_state(target, false)) {
		/* XXX should send SRP_I_LOGOUT request */

		if (ib_send_cm_dreq(target->cm_id, NULL, 0)) {
			shost_printk(KERN_DEBUG, target->scsi_host,
				     PFX "Sending CM DREQ failed\n");
		}
	}
}

static void srp_free_req_data(struct srp_target_port *target)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_device *ibdev = dev->dev;
	struct srp_request *req;
	int i;

	if (!target->req_ring)
		return;

	for (i = 0; i < target->req_ring_size; ++i) {
		req = &target->req_ring[i];
		if (dev->use_fast_reg)
			kfree(req->fr_list);
		else
			kfree(req->fmr_list);
		kfree(req->map_page);
		if (req->indirect_dma_addr) {
			ib_dma_unmap_single(ibdev, req->indirect_dma_addr,
					    target->indirect_size,
					    DMA_TO_DEVICE);
		}
		kfree(req->indirect_desc);
	}

	kfree(target->req_ring);
	target->req_ring = NULL;
}

static int srp_alloc_req_data(struct srp_target_port *target)
{
	struct srp_device *srp_dev = target->srp_host->srp_dev;
	struct ib_device *ibdev = srp_dev->dev;
	struct srp_request *req;
	void *mr_list;
	dma_addr_t dma_addr;
	int i, ret = -ENOMEM;

	INIT_LIST_HEAD(&target->free_reqs);

	target->req_ring = kzalloc(target->req_ring_size *
				   sizeof(*target->req_ring), GFP_KERNEL);
	if (!target->req_ring)
		goto out;

	for (i = 0; i < target->req_ring_size; ++i) {
		req = &target->req_ring[i];
		mr_list = kmalloc(target->cmd_sg_cnt * sizeof(void *),
				  GFP_KERNEL);
		if (!mr_list)
			goto out;
		if (srp_dev->use_fast_reg)
			req->fr_list = mr_list;
		else
			req->fmr_list = mr_list;
		req->map_page = kmalloc(srp_dev->max_pages_per_mr *
					sizeof(void *), GFP_KERNEL);
		if (!req->map_page)
			goto out;
		req->indirect_desc = kmalloc(target->indirect_size, GFP_KERNEL);
		if (!req->indirect_desc)
			goto out;

		dma_addr = ib_dma_map_single(ibdev, req->indirect_desc,
					     target->indirect_size,
					     DMA_TO_DEVICE);
		if (ib_dma_mapping_error(ibdev, dma_addr))
			goto out;

		req->indirect_dma_addr = dma_addr;
		req->index = i;
		list_add_tail(&req->list, &target->free_reqs);
	}
	ret = 0;

out:
	return ret;
}

/**
 * srp_del_scsi_host_attr() - Remove attributes defined in the host template.
 * @shost: SCSI host whose attributes to remove from sysfs.
 *
 * Note: Any attributes defined in the host template and that did not exist
 * before invocation of this function will be ignored.
 */
static void srp_del_scsi_host_attr(struct Scsi_Host *shost)
{
	struct device_attribute **attr;

	for (attr = shost->hostt->shost_attrs; attr && *attr; ++attr)
		device_remove_file(&shost->shost_dev, *attr);
}

static void srp_remove_target(struct srp_target_port *target)
{
	WARN_ON_ONCE(target->state != SRP_TARGET_REMOVED);

	srp_del_scsi_host_attr(target->scsi_host);
	srp_rport_get(target->rport);
	srp_remove_host(target->scsi_host);
	scsi_remove_host(target->scsi_host);
	srp_stop_rport_timers(target->rport);
	srp_disconnect_target(target);
	ib_destroy_cm_id(target->cm_id);
	srp_free_target_ib(target);
	cancel_work_sync(&target->tl_err_work);
	srp_rport_put(target->rport);
	srp_free_req_data(target);

	spin_lock(&target->srp_host->target_lock);
	list_del(&target->list);
	spin_unlock(&target->srp_host->target_lock);

	scsi_host_put(target->scsi_host);
}

static void srp_remove_work(struct work_struct *work)
{
	struct srp_target_port *target =
		container_of(work, struct srp_target_port, remove_work);

	WARN_ON_ONCE(target->state != SRP_TARGET_REMOVED);

	srp_remove_target(target);
}

static void srp_rport_delete(struct srp_rport *rport)
{
	struct srp_target_port *target = rport->lld_data;

	srp_queue_remove_work(target);
}

static int srp_connect_target(struct srp_target_port *target)
{
	int retries = 3;
	int ret;

	WARN_ON_ONCE(target->connected);

	target->qp_in_error = false;

	ret = srp_lookup_path(target);
	if (ret)
		return ret;

	while (1) {
		init_completion(&target->done);
		ret = srp_send_req(target);
		if (ret)
			return ret;
		ret = wait_for_completion_interruptible(&target->done);
		if (ret < 0)
			return ret;

		/*
		 * The CM event handling code will set status to
		 * SRP_PORT_REDIRECT if we get a port redirect REJ
		 * back, or SRP_DLID_REDIRECT if we get a lid/qp
		 * redirect REJ back.
		 */
		switch (target->status) {
		case 0:
			srp_change_conn_state(target, true);
			return 0;

		case SRP_PORT_REDIRECT:
			ret = srp_lookup_path(target);
			if (ret)
				return ret;
			break;

		case SRP_DLID_REDIRECT:
			break;

		case SRP_STALE_CONN:
			/* Our current CM id was stale, and is now in timewait.
			 * Try to reconnect with a new one.
			 */
			if (!retries-- || srp_new_cm_id(target)) {
				shost_printk(KERN_ERR, target->scsi_host, PFX
					     "giving up on stale connection\n");
				target->status = -ECONNRESET;
				return target->status;
			}

			shost_printk(KERN_ERR, target->scsi_host, PFX
				     "retrying stale connection\n");
			break;

		default:
			return target->status;
		}
	}
}

static int srp_inv_rkey(struct srp_target_port *target, u32 rkey)
{
	struct ib_send_wr *bad_wr;
	struct ib_send_wr wr = {
		.opcode		    = IB_WR_LOCAL_INV,
		.wr_id		    = LOCAL_INV_WR_ID_MASK,
		.next		    = NULL,
		.num_sge	    = 0,
		.send_flags	    = 0,
		.ex.invalidate_rkey = rkey,
	};

	return ib_post_send(target->qp, &wr, &bad_wr);
}

static void srp_unmap_data(struct scsi_cmnd *scmnd,
			   struct srp_target_port *target,
			   struct srp_request *req)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_device *ibdev = dev->dev;
	int i, res;

	if (!scsi_sglist(scmnd) ||
	    (scmnd->sc_data_direction != DMA_TO_DEVICE &&
	     scmnd->sc_data_direction != DMA_FROM_DEVICE))
		return;

	if (dev->use_fast_reg) {
		struct srp_fr_desc **pfr;

		for (i = req->nmdesc, pfr = req->fr_list; i > 0; i--, pfr++) {
			res = srp_inv_rkey(target, (*pfr)->mr->rkey);
			if (res < 0) {
				shost_printk(KERN_ERR, target->scsi_host, PFX
				  "Queueing INV WR for rkey %#x failed (%d)\n",
				  (*pfr)->mr->rkey, res);
				queue_work(system_long_wq,
					   &target->tl_err_work);
			}
		}
		if (req->nmdesc)
			srp_fr_pool_put(target->fr_pool, req->fr_list,
					req->nmdesc);
	} else {
		struct ib_pool_fmr **pfmr;

		for (i = req->nmdesc, pfmr = req->fmr_list; i > 0; i--, pfmr++)
			ib_fmr_pool_unmap(*pfmr);
	}

	ib_dma_unmap_sg(ibdev, scsi_sglist(scmnd), scsi_sg_count(scmnd),
			scmnd->sc_data_direction);
}

/**
 * srp_claim_req - Take ownership of the scmnd associated with a request.
 * @target: SRP target port.
 * @req: SRP request.
 * @sdev: If not NULL, only take ownership for this SCSI device.
 * @scmnd: If NULL, take ownership of @req->scmnd. If not NULL, only take
 *         ownership of @req->scmnd if it equals @scmnd.
 *
 * Return value:
 * Either NULL or a pointer to the SCSI command the caller became owner of.
 */
static struct scsi_cmnd *srp_claim_req(struct srp_target_port *target,
				       struct srp_request *req,
				       struct scsi_device *sdev,
				       struct scsi_cmnd *scmnd)
{
	unsigned long flags;

	spin_lock_irqsave(&target->lock, flags);
	if (req->scmnd &&
	    (!sdev || req->scmnd->device == sdev) &&
	    (!scmnd || req->scmnd == scmnd)) {
		scmnd = req->scmnd;
		req->scmnd = NULL;
	} else {
		scmnd = NULL;
	}
	spin_unlock_irqrestore(&target->lock, flags);

	return scmnd;
}

/**
 * srp_free_req() - Unmap data and add request to the free request list.
 * @target: SRP target port.
 * @req:    Request to be freed.
 * @scmnd:  SCSI command associated with @req.
 * @req_lim_delta: Amount to be added to @target->req_lim.
 */
static void srp_free_req(struct srp_target_port *target,
			 struct srp_request *req, struct scsi_cmnd *scmnd,
			 s32 req_lim_delta)
{
	unsigned long flags;

	srp_unmap_data(scmnd, target, req);

	spin_lock_irqsave(&target->lock, flags);
	target->req_lim += req_lim_delta;
	list_add_tail(&req->list, &target->free_reqs);
	spin_unlock_irqrestore(&target->lock, flags);
}

static void srp_finish_req(struct srp_target_port *target,
			   struct srp_request *req, struct scsi_device *sdev,
			   int result)
{
	struct scsi_cmnd *scmnd = srp_claim_req(target, req, sdev, NULL);

	if (scmnd) {
		srp_free_req(target, req, scmnd, 0);
		scmnd->result = result;
		scmnd->scsi_done(scmnd);
	}
}

static void srp_terminate_io(struct srp_rport *rport)
{
	struct srp_target_port *target = rport->lld_data;
	struct Scsi_Host *shost = target->scsi_host;
	struct scsi_device *sdev;
	int i;

	/*
	 * Invoking srp_terminate_io() while srp_queuecommand() is running
	 * is not safe. Hence the warning statement below.
	 */
	shost_for_each_device(sdev, shost)
		WARN_ON_ONCE(sdev->request_queue->request_fn_active);

	for (i = 0; i < target->req_ring_size; ++i) {
		struct srp_request *req = &target->req_ring[i];
		srp_finish_req(target, req, NULL, DID_TRANSPORT_FAILFAST << 16);
	}
}

/*
 * It is up to the caller to ensure that srp_rport_reconnect() calls are
 * serialized and that no concurrent srp_queuecommand(), srp_abort(),
 * srp_reset_device() or srp_reset_host() calls will occur while this function
 * is in progress. One way to realize that is not to call this function
 * directly but to call srp_reconnect_rport() instead since that last function
 * serializes calls of this function via rport->mutex and also blocks
 * srp_queuecommand() calls before invoking this function.
 */
static int srp_rport_reconnect(struct srp_rport *rport)
{
	struct srp_target_port *target = rport->lld_data;
	int i, ret;

	srp_disconnect_target(target);
	/*
	 * Now get a new local CM ID so that we avoid confusing the target in
	 * case things are really fouled up. Doing so also ensures that all CM
	 * callbacks will have finished before a new QP is allocated.
	 */
	ret = srp_new_cm_id(target);

	for (i = 0; i < target->req_ring_size; ++i) {
		struct srp_request *req = &target->req_ring[i];
		srp_finish_req(target, req, NULL, DID_RESET << 16);
	}

	/*
	 * Whether or not creating a new CM ID succeeded, create a new
	 * QP. This guarantees that all callback functions for the old QP have
	 * finished before any send requests are posted on the new QP.
	 */
	ret += srp_create_target_ib(target);

	INIT_LIST_HEAD(&target->free_tx);
	for (i = 0; i < target->queue_size; ++i)
		list_add(&target->tx_ring[i]->list, &target->free_tx);

	if (ret == 0)
		ret = srp_connect_target(target);

	if (ret == 0)
		shost_printk(KERN_INFO, target->scsi_host,
			     PFX "reconnect succeeded\n");

	return ret;
}

static void srp_map_desc(struct srp_map_state *state, dma_addr_t dma_addr,
			 unsigned int dma_len, u32 rkey)
{
	struct srp_direct_buf *desc = state->desc;

	desc->va = cpu_to_be64(dma_addr);
	desc->key = cpu_to_be32(rkey);
	desc->len = cpu_to_be32(dma_len);

	state->total_len += dma_len;
	state->desc++;
	state->ndesc++;
}

static int srp_map_finish_fmr(struct srp_map_state *state,
			      struct srp_target_port *target)
{
	struct ib_pool_fmr *fmr;
	u64 io_addr = 0;

	fmr = ib_fmr_pool_map_phys(target->fmr_pool, state->pages,
				   state->npages, io_addr);
	if (IS_ERR(fmr))
		return PTR_ERR(fmr);

	*state->next_fmr++ = fmr;
	state->nmdesc++;

	srp_map_desc(state, 0, state->dma_len, fmr->fmr->rkey);

	return 0;
}

static int srp_map_finish_fr(struct srp_map_state *state,
			     struct srp_target_port *target)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_send_wr *bad_wr;
	struct ib_send_wr wr;
	struct srp_fr_desc *desc;
	u32 rkey;

	desc = srp_fr_pool_get(target->fr_pool);
	if (!desc)
		return -ENOMEM;

	rkey = ib_inc_rkey(desc->mr->rkey);
	ib_update_fast_reg_key(desc->mr, rkey);

	memcpy(desc->frpl->page_list, state->pages,
	       sizeof(state->pages[0]) * state->npages);

	memset(&wr, 0, sizeof(wr));
	wr.opcode = IB_WR_FAST_REG_MR;
	wr.wr_id = FAST_REG_WR_ID_MASK;
	wr.wr.fast_reg.iova_start = state->base_dma_addr;
	wr.wr.fast_reg.page_list = desc->frpl;
	wr.wr.fast_reg.page_list_len = state->npages;
	wr.wr.fast_reg.page_shift = ilog2(dev->mr_page_size);
	wr.wr.fast_reg.length = state->dma_len;
	wr.wr.fast_reg.access_flags = (IB_ACCESS_LOCAL_WRITE |
				       IB_ACCESS_REMOTE_READ |
				       IB_ACCESS_REMOTE_WRITE);
	wr.wr.fast_reg.rkey = desc->mr->lkey;

	*state->next_fr++ = desc;
	state->nmdesc++;

	srp_map_desc(state, state->base_dma_addr, state->dma_len,
		     desc->mr->rkey);

	return ib_post_send(target->qp, &wr, &bad_wr);
}

static int srp_finish_mapping(struct srp_map_state *state,
			      struct srp_target_port *target)
{
	int ret = 0;

	if (state->npages == 0)
		return 0;

	if (state->npages == 1 && !register_always)
		srp_map_desc(state, state->base_dma_addr, state->dma_len,
			     target->rkey);
	else
		ret = target->srp_host->srp_dev->use_fast_reg ?
			srp_map_finish_fr(state, target) :
			srp_map_finish_fmr(state, target);

	if (ret == 0) {
		state->npages = 0;
		state->dma_len = 0;
	}

	return ret;
}

static void srp_map_update_start(struct srp_map_state *state,
				 struct scatterlist *sg, int sg_index,
				 dma_addr_t dma_addr)
{
	state->unmapped_sg = sg;
	state->unmapped_index = sg_index;
	state->unmapped_addr = dma_addr;
}

static int srp_map_sg_entry(struct srp_map_state *state,
			    struct srp_target_port *target,
			    struct scatterlist *sg, int sg_index,
			    bool use_mr)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_device *ibdev = dev->dev;
	dma_addr_t dma_addr = ib_sg_dma_address(ibdev, sg);
	unsigned int dma_len = ib_sg_dma_len(ibdev, sg);
	unsigned int len;
	int ret;

	if (!dma_len)
		return 0;

	if (!use_mr) {
		/*
		 * Once we're in direct map mode for a request, we don't
		 * go back to FMR or FR mode, so no need to update anything
		 * other than the descriptor.
		 */
		srp_map_desc(state, dma_addr, dma_len, target->rkey);
		return 0;
	}

	/*
	 * Since not all RDMA HW drivers support non-zero page offsets for
	 * FMR, if we start at an offset into a page, don't merge into the
	 * current FMR mapping. Finish it out, and use the kernel's MR for
	 * this sg entry.
	 */
	if ((!dev->use_fast_reg && dma_addr & ~dev->mr_page_mask) ||
	    dma_len > dev->mr_max_size) {
		ret = srp_finish_mapping(state, target);
		if (ret)
			return ret;

		srp_map_desc(state, dma_addr, dma_len, target->rkey);
		srp_map_update_start(state, NULL, 0, 0);
		return 0;
	}

	/*
	 * If this is the first sg that will be mapped via FMR or via FR, save
	 * our position. We need to know the first unmapped entry, its index,
	 * and the first unmapped address within that entry to be able to
	 * restart mapping after an error.
	 */
	if (!state->unmapped_sg)
		srp_map_update_start(state, sg, sg_index, dma_addr);

	while (dma_len) {
		unsigned offset = dma_addr & ~dev->mr_page_mask;
		if (state->npages == dev->max_pages_per_mr || offset != 0) {
			ret = srp_finish_mapping(state, target);
			if (ret)
				return ret;

			srp_map_update_start(state, sg, sg_index, dma_addr);
		}

		len = min_t(unsigned int, dma_len, dev->mr_page_size - offset);

		if (!state->npages)
			state->base_dma_addr = dma_addr;
		state->pages[state->npages++] = dma_addr & dev->mr_page_mask;
		state->dma_len += len;
		dma_addr += len;
		dma_len -= len;
	}

	/*
	 * If the last entry of the MR wasn't a full page, then we need to
	 * close it out and start a new one -- we can only merge at page
	 * boundries.
	 */
	ret = 0;
	if (len != dev->mr_page_size) {
		ret = srp_finish_mapping(state, target);
		if (!ret)
			srp_map_update_start(state, NULL, 0, 0);
	}
	return ret;
}

static int srp_map_sg(struct srp_map_state *state,
		      struct srp_target_port *target, struct srp_request *req,
		      struct scatterlist *scat, int count)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_device *ibdev = dev->dev;
	struct scatterlist *sg;
	int i;
	bool use_mr;

	state->desc	= req->indirect_desc;
	state->pages	= req->map_page;
	if (dev->use_fast_reg) {
		state->next_fr = req->fr_list;
		use_mr = !!target->fr_pool;
	} else {
		state->next_fmr = req->fmr_list;
		use_mr = !!target->fmr_pool;
	}

	for_each_sg(scat, sg, count, i) {
		if (srp_map_sg_entry(state, target, sg, i, use_mr)) {
			/*
			 * Memory registration failed, so backtrack to the
			 * first unmapped entry and continue on without using
			 * memory registration.
			 */
			dma_addr_t dma_addr;
			unsigned int dma_len;

backtrack:
			sg = state->unmapped_sg;
			i = state->unmapped_index;

			dma_addr = ib_sg_dma_address(ibdev, sg);
			dma_len = ib_sg_dma_len(ibdev, sg);
			dma_len -= (state->unmapped_addr - dma_addr);
			dma_addr = state->unmapped_addr;
			use_mr = false;
			srp_map_desc(state, dma_addr, dma_len, target->rkey);
		}
	}

	if (use_mr && srp_finish_mapping(state, target))
		goto backtrack;

	req->nmdesc = state->nmdesc;

	return 0;
}

static int srp_map_data(struct scsi_cmnd *scmnd, struct srp_target_port *target,
			struct srp_request *req)
{
	struct scatterlist *scat;
	struct srp_cmd *cmd = req->cmd->buf;
	int len, nents, count;
	struct srp_device *dev;
	struct ib_device *ibdev;
	struct srp_map_state state;
	struct srp_indirect_buf *indirect_hdr;
	u32 table_len;
	u8 fmt;

	if (!scsi_sglist(scmnd) || scmnd->sc_data_direction == DMA_NONE)
		return sizeof (struct srp_cmd);

	if (scmnd->sc_data_direction != DMA_FROM_DEVICE &&
	    scmnd->sc_data_direction != DMA_TO_DEVICE) {
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Unhandled data direction %d\n",
			     scmnd->sc_data_direction);
		return -EINVAL;
	}

	nents = scsi_sg_count(scmnd);
	scat  = scsi_sglist(scmnd);

	dev = target->srp_host->srp_dev;
	ibdev = dev->dev;

	count = ib_dma_map_sg(ibdev, scat, nents, scmnd->sc_data_direction);
	if (unlikely(count == 0))
		return -EIO;

	fmt = SRP_DATA_DESC_DIRECT;
	len = sizeof (struct srp_cmd) +	sizeof (struct srp_direct_buf);

	if (count == 1 && !register_always) {
		/*
		 * The midlayer only generated a single gather/scatter
		 * entry, or DMA mapping coalesced everything to a
		 * single entry.  So a direct descriptor along with
		 * the DMA MR suffices.
		 */
		struct srp_direct_buf *buf = (void *) cmd->add_data;

		buf->va  = cpu_to_be64(ib_sg_dma_address(ibdev, scat));
		buf->key = cpu_to_be32(target->rkey);
		buf->len = cpu_to_be32(ib_sg_dma_len(ibdev, scat));

		req->nmdesc = 0;
		goto map_complete;
	}

	/*
	 * We have more than one scatter/gather entry, so build our indirect
	 * descriptor table, trying to merge as many entries as we can.
	 */
	indirect_hdr = (void *) cmd->add_data;

	ib_dma_sync_single_for_cpu(ibdev, req->indirect_dma_addr,
				   target->indirect_size, DMA_TO_DEVICE);

	memset(&state, 0, sizeof(state));
	srp_map_sg(&state, target, req, scat, count);

	/* We've mapped the request, now pull as much of the indirect
	 * descriptor table as we can into the command buffer. If this
	 * target is not using an external indirect table, we are
	 * guaranteed to fit into the command, as the SCSI layer won't
	 * give us more S/G entries than we allow.
	 */
	if (state.ndesc == 1) {
		/*
		 * Memory registration collapsed the sg-list into one entry,
		 * so use a direct descriptor.
		 */
		struct srp_direct_buf *buf = (void *) cmd->add_data;

		*buf = req->indirect_desc[0];
		goto map_complete;
	}

	if (unlikely(target->cmd_sg_cnt < state.ndesc &&
						!target->allow_ext_sg)) {
		shost_printk(KERN_ERR, target->scsi_host,
			     "Could not fit S/G list into SRP_CMD\n");
		return -EIO;
	}

	count = min(state.ndesc, target->cmd_sg_cnt);
	table_len = state.ndesc * sizeof (struct srp_direct_buf);

	fmt = SRP_DATA_DESC_INDIRECT;
	len = sizeof(struct srp_cmd) + sizeof (struct srp_indirect_buf);
	len += count * sizeof (struct srp_direct_buf);

	memcpy(indirect_hdr->desc_list, req->indirect_desc,
	       count * sizeof (struct srp_direct_buf));

	indirect_hdr->table_desc.va = cpu_to_be64(req->indirect_dma_addr);
	indirect_hdr->table_desc.key = cpu_to_be32(target->rkey);
	indirect_hdr->table_desc.len = cpu_to_be32(table_len);
	indirect_hdr->len = cpu_to_be32(state.total_len);

	if (scmnd->sc_data_direction == DMA_TO_DEVICE)
		cmd->data_out_desc_cnt = count;
	else
		cmd->data_in_desc_cnt = count;

	ib_dma_sync_single_for_device(ibdev, req->indirect_dma_addr, table_len,
				      DMA_TO_DEVICE);

map_complete:
	if (scmnd->sc_data_direction == DMA_TO_DEVICE)
		cmd->buf_fmt = fmt << 4;
	else
		cmd->buf_fmt = fmt;

	return len;
}

/*
 * Return an IU and possible credit to the free pool
 */
static void srp_put_tx_iu(struct srp_target_port *target, struct srp_iu *iu,
			  enum srp_iu_type iu_type)
{
	unsigned long flags;

	spin_lock_irqsave(&target->lock, flags);
	list_add(&iu->list, &target->free_tx);
	if (iu_type != SRP_IU_RSP)
		++target->req_lim;
	spin_unlock_irqrestore(&target->lock, flags);
}

/*
 * Must be called with target->lock held to protect req_lim and free_tx.
 * If IU is not sent, it must be returned using srp_put_tx_iu().
 *
 * Note:
 * An upper limit for the number of allocated information units for each
 * request type is:
 * - SRP_IU_CMD: SRP_CMD_SQ_SIZE, since the SCSI mid-layer never queues
 *   more than Scsi_Host.can_queue requests.
 * - SRP_IU_TSK_MGMT: SRP_TSK_MGMT_SQ_SIZE.
 * - SRP_IU_RSP: 1, since a conforming SRP target never sends more than
 *   one unanswered SRP request to an initiator.
 */
static struct srp_iu *__srp_get_tx_iu(struct srp_target_port *target,
				      enum srp_iu_type iu_type)
{
	s32 rsv = (iu_type == SRP_IU_TSK_MGMT) ? 0 : SRP_TSK_MGMT_SQ_SIZE;
	struct srp_iu *iu;

	srp_send_completion(target->send_cq, target);

	if (list_empty(&target->free_tx))
		return NULL;

	/* Initiator responses to target requests do not consume credits */
	if (iu_type != SRP_IU_RSP) {
		if (target->req_lim <= rsv) {
			++target->zero_req_lim;
			return NULL;
		}

		--target->req_lim;
	}

	iu = list_first_entry(&target->free_tx, struct srp_iu, list);
	list_del(&iu->list);
	return iu;
}

static int srp_post_send(struct srp_target_port *target,
			 struct srp_iu *iu, int len)
{
	struct ib_sge list;
	struct ib_send_wr wr, *bad_wr;

	list.addr   = iu->dma;
	list.length = len;
	list.lkey   = target->lkey;

	wr.next       = NULL;
	wr.wr_id      = (uintptr_t) iu;
	wr.sg_list    = &list;
	wr.num_sge    = 1;
	wr.opcode     = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED;

	return ib_post_send(target->qp, &wr, &bad_wr);
}

static int srp_post_recv(struct srp_target_port *target, struct srp_iu *iu)
{
	struct ib_recv_wr wr, *bad_wr;
	struct ib_sge list;

	list.addr   = iu->dma;
	list.length = iu->size;
	list.lkey   = target->lkey;

	wr.next     = NULL;
	wr.wr_id    = (uintptr_t) iu;
	wr.sg_list  = &list;
	wr.num_sge  = 1;

	return ib_post_recv(target->qp, &wr, &bad_wr);
}

static void srp_process_rsp(struct srp_target_port *target, struct srp_rsp *rsp)
{
	struct srp_request *req;
	struct scsi_cmnd *scmnd;
	unsigned long flags;

	if (unlikely(rsp->tag & SRP_TAG_TSK_MGMT)) {
		spin_lock_irqsave(&target->lock, flags);
		target->req_lim += be32_to_cpu(rsp->req_lim_delta);
		spin_unlock_irqrestore(&target->lock, flags);

		target->tsk_mgmt_status = -1;
		if (be32_to_cpu(rsp->resp_data_len) >= 4)
			target->tsk_mgmt_status = rsp->data[3];
		complete(&target->tsk_mgmt_done);
	} else {
		req = &target->req_ring[rsp->tag];
		scmnd = srp_claim_req(target, req, NULL, NULL);
		if (!scmnd) {
			shost_printk(KERN_ERR, target->scsi_host,
				     "Null scmnd for RSP w/tag %016llx\n",
				     (unsigned long long) rsp->tag);

			spin_lock_irqsave(&target->lock, flags);
			target->req_lim += be32_to_cpu(rsp->req_lim_delta);
			spin_unlock_irqrestore(&target->lock, flags);

			return;
		}
		scmnd->result = rsp->status;

		if (rsp->flags & SRP_RSP_FLAG_SNSVALID) {
			memcpy(scmnd->sense_buffer, rsp->data +
			       be32_to_cpu(rsp->resp_data_len),
			       min_t(int, be32_to_cpu(rsp->sense_data_len),
				     SCSI_SENSE_BUFFERSIZE));
		}

		if (rsp->flags & (SRP_RSP_FLAG_DOOVER | SRP_RSP_FLAG_DOUNDER))
			scsi_set_resid(scmnd, be32_to_cpu(rsp->data_out_res_cnt));
		else if (rsp->flags & (SRP_RSP_FLAG_DIOVER | SRP_RSP_FLAG_DIUNDER))
			scsi_set_resid(scmnd, be32_to_cpu(rsp->data_in_res_cnt));

		srp_free_req(target, req, scmnd,
			     be32_to_cpu(rsp->req_lim_delta));

		scmnd->host_scribble = NULL;
		scmnd->scsi_done(scmnd);
	}
}

static int srp_response_common(struct srp_target_port *target, s32 req_delta,
			       void *rsp, int len)
{
	struct ib_device *dev = target->srp_host->srp_dev->dev;
	unsigned long flags;
	struct srp_iu *iu;
	int err;

	spin_lock_irqsave(&target->lock, flags);
	target->req_lim += req_delta;
	iu = __srp_get_tx_iu(target, SRP_IU_RSP);
	spin_unlock_irqrestore(&target->lock, flags);

	if (!iu) {
		shost_printk(KERN_ERR, target->scsi_host, PFX
			     "no IU available to send response\n");
		return 1;
	}

	ib_dma_sync_single_for_cpu(dev, iu->dma, len, DMA_TO_DEVICE);
	memcpy(iu->buf, rsp, len);
	ib_dma_sync_single_for_device(dev, iu->dma, len, DMA_TO_DEVICE);

	err = srp_post_send(target, iu, len);
	if (err) {
		shost_printk(KERN_ERR, target->scsi_host, PFX
			     "unable to post response: %d\n", err);
		srp_put_tx_iu(target, iu, SRP_IU_RSP);
	}

	return err;
}

static void srp_process_cred_req(struct srp_target_port *target,
				 struct srp_cred_req *req)
{
	struct srp_cred_rsp rsp = {
		.opcode = SRP_CRED_RSP,
		.tag = req->tag,
	};
	s32 delta = be32_to_cpu(req->req_lim_delta);

	if (srp_response_common(target, delta, &rsp, sizeof rsp))
		shost_printk(KERN_ERR, target->scsi_host, PFX
			     "problems processing SRP_CRED_REQ\n");
}

static void srp_process_aer_req(struct srp_target_port *target,
				struct srp_aer_req *req)
{
	struct srp_aer_rsp rsp = {
		.opcode = SRP_AER_RSP,
		.tag = req->tag,
	};
	s32 delta = be32_to_cpu(req->req_lim_delta);

	shost_printk(KERN_ERR, target->scsi_host, PFX
		     "ignoring AER for LUN %llu\n", be64_to_cpu(req->lun));

	if (srp_response_common(target, delta, &rsp, sizeof rsp))
		shost_printk(KERN_ERR, target->scsi_host, PFX
			     "problems processing SRP_AER_REQ\n");
}

static void srp_handle_recv(struct srp_target_port *target, struct ib_wc *wc)
{
	struct ib_device *dev = target->srp_host->srp_dev->dev;
	struct srp_iu *iu = (struct srp_iu *) (uintptr_t) wc->wr_id;
	int res;
	u8 opcode;

	ib_dma_sync_single_for_cpu(dev, iu->dma, target->max_ti_iu_len,
				   DMA_FROM_DEVICE);

	opcode = *(u8 *) iu->buf;

	if (0) {
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "recv completion, opcode 0x%02x\n", opcode);
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 8, 1,
			       iu->buf, wc->byte_len, true);
	}

	switch (opcode) {
	case SRP_RSP:
		srp_process_rsp(target, iu->buf);
		break;

	case SRP_CRED_REQ:
		srp_process_cred_req(target, iu->buf);
		break;

	case SRP_AER_REQ:
		srp_process_aer_req(target, iu->buf);
		break;

	case SRP_T_LOGOUT:
		/* XXX Handle target logout */
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Got target logout request\n");
		break;

	default:
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Unhandled SRP opcode 0x%02x\n", opcode);
		break;
	}

	ib_dma_sync_single_for_device(dev, iu->dma, target->max_ti_iu_len,
				      DMA_FROM_DEVICE);

	res = srp_post_recv(target, iu);
	if (res != 0)
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "Recv failed with error code %d\n", res);
}

/**
 * srp_tl_err_work() - handle a transport layer error
 * @work: Work structure embedded in an SRP target port.
 *
 * Note: This function may get invoked before the rport has been created,
 * hence the target->rport test.
 */
static void srp_tl_err_work(struct work_struct *work)
{
	struct srp_target_port *target;

	target = container_of(work, struct srp_target_port, tl_err_work);
	if (target->rport)
		srp_start_tl_fail_timers(target->rport);
}

static void srp_handle_qp_err(u64 wr_id, enum ib_wc_status wc_status,
			      bool send_err, struct srp_target_port *target)
{
	if (target->connected && !target->qp_in_error) {
		if (wr_id & LOCAL_INV_WR_ID_MASK) {
			shost_printk(KERN_ERR, target->scsi_host, PFX
				     "LOCAL_INV failed with status %d\n",
				     wc_status);
		} else if (wr_id & FAST_REG_WR_ID_MASK) {
			shost_printk(KERN_ERR, target->scsi_host, PFX
				     "FAST_REG_MR failed status %d\n",
				     wc_status);
		} else {
			shost_printk(KERN_ERR, target->scsi_host,
				     PFX "failed %s status %d for iu %p\n",
				     send_err ? "send" : "receive",
				     wc_status, (void *)(uintptr_t)wr_id);
		}
		queue_work(system_long_wq, &target->tl_err_work);
	}
	target->qp_in_error = true;
}

static void srp_recv_completion(struct ib_cq *cq, void *target_ptr)
{
	struct srp_target_port *target = target_ptr;
	struct ib_wc wc;

	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
	while (ib_poll_cq(cq, 1, &wc) > 0) {
		if (likely(wc.status == IB_WC_SUCCESS)) {
			srp_handle_recv(target, &wc);
		} else {
			srp_handle_qp_err(wc.wr_id, wc.status, false, target);
		}
	}
}

static void srp_send_completion(struct ib_cq *cq, void *target_ptr)
{
	struct srp_target_port *target = target_ptr;
	struct ib_wc wc;
	struct srp_iu *iu;

	while (ib_poll_cq(cq, 1, &wc) > 0) {
		if (likely(wc.status == IB_WC_SUCCESS)) {
			iu = (struct srp_iu *) (uintptr_t) wc.wr_id;
			list_add(&iu->list, &target->free_tx);
		} else {
			srp_handle_qp_err(wc.wr_id, wc.status, true, target);
		}
	}
}

static int srp_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(shost);
	struct srp_rport *rport = target->rport;
	struct srp_request *req;
	struct srp_iu *iu;
	struct srp_cmd *cmd;
	struct ib_device *dev;
	unsigned long flags;
	int len, ret;
	const bool in_scsi_eh = !in_interrupt() && current == shost->ehandler;

	/*
	 * The SCSI EH thread is the only context from which srp_queuecommand()
	 * can get invoked for blocked devices (SDEV_BLOCK /
	 * SDEV_CREATED_BLOCK). Avoid racing with srp_reconnect_rport() by
	 * locking the rport mutex if invoked from inside the SCSI EH.
	 */
	if (in_scsi_eh)
		mutex_lock(&rport->mutex);

	scmnd->result = srp_chkready(target->rport);
	if (unlikely(scmnd->result))
		goto err;

	spin_lock_irqsave(&target->lock, flags);
	iu = __srp_get_tx_iu(target, SRP_IU_CMD);
	if (!iu)
		goto err_unlock;

	req = list_first_entry(&target->free_reqs, struct srp_request, list);
	list_del(&req->list);
	spin_unlock_irqrestore(&target->lock, flags);

	dev = target->srp_host->srp_dev->dev;
	ib_dma_sync_single_for_cpu(dev, iu->dma, target->max_iu_len,
				   DMA_TO_DEVICE);

	scmnd->host_scribble = (void *) req;

	cmd = iu->buf;
	memset(cmd, 0, sizeof *cmd);

	cmd->opcode = SRP_CMD;
	cmd->lun    = cpu_to_be64((u64) scmnd->device->lun << 48);
	cmd->tag    = req->index;
	memcpy(cmd->cdb, scmnd->cmnd, scmnd->cmd_len);

	req->scmnd    = scmnd;
	req->cmd      = iu;

	len = srp_map_data(scmnd, target, req);
	if (len < 0) {
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "Failed to map data (%d)\n", len);
		/*
		 * If we ran out of memory descriptors (-ENOMEM) because an
		 * application is queuing many requests with more than
		 * max_pages_per_mr sg-list elements, tell the SCSI mid-layer
		 * to reduce queue depth temporarily.
		 */
		scmnd->result = len == -ENOMEM ?
			DID_OK << 16 | QUEUE_FULL << 1 : DID_ERROR << 16;
		goto err_iu;
	}

	ib_dma_sync_single_for_device(dev, iu->dma, target->max_iu_len,
				      DMA_TO_DEVICE);

	if (srp_post_send(target, iu, len)) {
		shost_printk(KERN_ERR, target->scsi_host, PFX "Send failed\n");
		goto err_unmap;
	}

	ret = 0;

unlock_rport:
	if (in_scsi_eh)
		mutex_unlock(&rport->mutex);

	return ret;

err_unmap:
	srp_unmap_data(scmnd, target, req);

err_iu:
	srp_put_tx_iu(target, iu, SRP_IU_CMD);

	/*
	 * Avoid that the loops that iterate over the request ring can
	 * encounter a dangling SCSI command pointer.
	 */
	req->scmnd = NULL;

	spin_lock_irqsave(&target->lock, flags);
	list_add(&req->list, &target->free_reqs);

err_unlock:
	spin_unlock_irqrestore(&target->lock, flags);

err:
	if (scmnd->result) {
		scmnd->scsi_done(scmnd);
		ret = 0;
	} else {
		ret = SCSI_MLQUEUE_HOST_BUSY;
	}

	goto unlock_rport;
}

/*
 * Note: the resources allocated in this function are freed in
 * srp_free_target_ib().
 */
static int srp_alloc_iu_bufs(struct srp_target_port *target)
{
	int i;

	target->rx_ring = kzalloc(target->queue_size * sizeof(*target->rx_ring),
				  GFP_KERNEL);
	if (!target->rx_ring)
		goto err_no_ring;
	target->tx_ring = kzalloc(target->queue_size * sizeof(*target->tx_ring),
				  GFP_KERNEL);
	if (!target->tx_ring)
		goto err_no_ring;

	for (i = 0; i < target->queue_size; ++i) {
		target->rx_ring[i] = srp_alloc_iu(target->srp_host,
						  target->max_ti_iu_len,
						  GFP_KERNEL, DMA_FROM_DEVICE);
		if (!target->rx_ring[i])
			goto err;
	}

	for (i = 0; i < target->queue_size; ++i) {
		target->tx_ring[i] = srp_alloc_iu(target->srp_host,
						  target->max_iu_len,
						  GFP_KERNEL, DMA_TO_DEVICE);
		if (!target->tx_ring[i])
			goto err;

		list_add(&target->tx_ring[i]->list, &target->free_tx);
	}

	return 0;

err:
	for (i = 0; i < target->queue_size; ++i) {
		srp_free_iu(target->srp_host, target->rx_ring[i]);
		srp_free_iu(target->srp_host, target->tx_ring[i]);
	}


err_no_ring:
	kfree(target->tx_ring);
	target->tx_ring = NULL;
	kfree(target->rx_ring);
	target->rx_ring = NULL;

	return -ENOMEM;
}

static uint32_t srp_compute_rq_tmo(struct ib_qp_attr *qp_attr, int attr_mask)
{
	uint64_t T_tr_ns, max_compl_time_ms;
	uint32_t rq_tmo_jiffies;

	/*
	 * According to section 11.2.4.2 in the IBTA spec (Modify Queue Pair,
	 * table 91), both the QP timeout and the retry count have to be set
	 * for RC QP's during the RTR to RTS transition.
	 */
	WARN_ON_ONCE((attr_mask & (IB_QP_TIMEOUT | IB_QP_RETRY_CNT)) !=
		     (IB_QP_TIMEOUT | IB_QP_RETRY_CNT));

	/*
	 * Set target->rq_tmo_jiffies to one second more than the largest time
	 * it can take before an error completion is generated. See also
	 * C9-140..142 in the IBTA spec for more information about how to
	 * convert the QP Local ACK Timeout value to nanoseconds.
	 */
	T_tr_ns = 4096 * (1ULL << qp_attr->timeout);
	max_compl_time_ms = qp_attr->retry_cnt * 4 * T_tr_ns;
	do_div(max_compl_time_ms, NSEC_PER_MSEC);
	rq_tmo_jiffies = msecs_to_jiffies(max_compl_time_ms + 1000);

	return rq_tmo_jiffies;
}

static void srp_cm_rep_handler(struct ib_cm_id *cm_id,
			       struct srp_login_rsp *lrsp,
			       struct srp_target_port *target)
{
	struct ib_qp_attr *qp_attr = NULL;
	int attr_mask = 0;
	int ret;
	int i;

	if (lrsp->opcode == SRP_LOGIN_RSP) {
		target->max_ti_iu_len = be32_to_cpu(lrsp->max_ti_iu_len);
		target->req_lim       = be32_to_cpu(lrsp->req_lim_delta);

		/*
		 * Reserve credits for task management so we don't
		 * bounce requests back to the SCSI mid-layer.
		 */
		target->scsi_host->can_queue
			= min(target->req_lim - SRP_TSK_MGMT_SQ_SIZE,
			      target->scsi_host->can_queue);
		target->scsi_host->cmd_per_lun
			= min_t(int, target->scsi_host->can_queue,
				target->scsi_host->cmd_per_lun);
	} else {
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Unhandled RSP opcode %#x\n", lrsp->opcode);
		ret = -ECONNRESET;
		goto error;
	}

	if (!target->rx_ring) {
		ret = srp_alloc_iu_bufs(target);
		if (ret)
			goto error;
	}

	ret = -ENOMEM;
	qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
	if (!qp_attr)
		goto error;

	qp_attr->qp_state = IB_QPS_RTR;
	ret = ib_cm_init_qp_attr(cm_id, qp_attr, &attr_mask);
	if (ret)
		goto error_free;

	ret = ib_modify_qp(target->qp, qp_attr, attr_mask);
	if (ret)
		goto error_free;

	for (i = 0; i < target->queue_size; i++) {
		struct srp_iu *iu = target->rx_ring[i];
		ret = srp_post_recv(target, iu);
		if (ret)
			goto error_free;
	}

	qp_attr->qp_state = IB_QPS_RTS;
	ret = ib_cm_init_qp_attr(cm_id, qp_attr, &attr_mask);
	if (ret)
		goto error_free;

	target->rq_tmo_jiffies = srp_compute_rq_tmo(qp_attr, attr_mask);

	ret = ib_modify_qp(target->qp, qp_attr, attr_mask);
	if (ret)
		goto error_free;

	ret = ib_send_cm_rtu(cm_id, NULL, 0);

error_free:
	kfree(qp_attr);

error:
	target->status = ret;
}

static void srp_cm_rej_handler(struct ib_cm_id *cm_id,
			       struct ib_cm_event *event,
			       struct srp_target_port *target)
{
	struct Scsi_Host *shost = target->scsi_host;
	struct ib_class_port_info *cpi;
	int opcode;

	switch (event->param.rej_rcvd.reason) {
	case IB_CM_REJ_PORT_CM_REDIRECT:
		cpi = event->param.rej_rcvd.ari;
		target->path.dlid = cpi->redirect_lid;
		target->path.pkey = cpi->redirect_pkey;
		cm_id->remote_cm_qpn = be32_to_cpu(cpi->redirect_qp) & 0x00ffffff;
		memcpy(target->path.dgid.raw, cpi->redirect_gid, 16);

		target->status = target->path.dlid ?
			SRP_DLID_REDIRECT : SRP_PORT_REDIRECT;
		break;

	case IB_CM_REJ_PORT_REDIRECT:
		if (srp_target_is_topspin(target)) {
			/*
			 * Topspin/Cisco SRP gateways incorrectly send
			 * reject reason code 25 when they mean 24
			 * (port redirect).
			 */
			memcpy(target->path.dgid.raw,
			       event->param.rej_rcvd.ari, 16);

			shost_printk(KERN_DEBUG, shost,
				     PFX "Topspin/Cisco redirect to target port GID %016llx%016llx\n",
				     (unsigned long long) be64_to_cpu(target->path.dgid.global.subnet_prefix),
				     (unsigned long long) be64_to_cpu(target->path.dgid.global.interface_id));

			target->status = SRP_PORT_REDIRECT;
		} else {
			shost_printk(KERN_WARNING, shost,
				     "  REJ reason: IB_CM_REJ_PORT_REDIRECT\n");
			target->status = -ECONNRESET;
		}
		break;

	case IB_CM_REJ_DUPLICATE_LOCAL_COMM_ID:
		shost_printk(KERN_WARNING, shost,
			    "  REJ reason: IB_CM_REJ_DUPLICATE_LOCAL_COMM_ID\n");
		target->status = -ECONNRESET;
		break;

	case IB_CM_REJ_CONSUMER_DEFINED:
		opcode = *(u8 *) event->private_data;
		if (opcode == SRP_LOGIN_REJ) {
			struct srp_login_rej *rej = event->private_data;
			u32 reason = be32_to_cpu(rej->reason);

			if (reason == SRP_LOGIN_REJ_REQ_IT_IU_LENGTH_TOO_LARGE)
				shost_printk(KERN_WARNING, shost,
					     PFX "SRP_LOGIN_REJ: requested max_it_iu_len too large\n");
			else
				shost_printk(KERN_WARNING, shost, PFX
					     "SRP LOGIN from %pI6 to %pI6 REJECTED, reason 0x%08x\n",
					     target->path.sgid.raw,
					     target->orig_dgid, reason);
		} else
			shost_printk(KERN_WARNING, shost,
				     "  REJ reason: IB_CM_REJ_CONSUMER_DEFINED,"
				     " opcode 0x%02x\n", opcode);
		target->status = -ECONNRESET;
		break;

	case IB_CM_REJ_STALE_CONN:
		shost_printk(KERN_WARNING, shost, "  REJ reason: stale connection\n");
		target->status = SRP_STALE_CONN;
		break;

	default:
		shost_printk(KERN_WARNING, shost, "  REJ reason 0x%x\n",
			     event->param.rej_rcvd.reason);
		target->status = -ECONNRESET;
	}
}

static int srp_cm_handler(struct ib_cm_id *cm_id, struct ib_cm_event *event)
{
	struct srp_target_port *target = cm_id->context;
	int comp = 0;

	switch (event->event) {
	case IB_CM_REQ_ERROR:
		shost_printk(KERN_DEBUG, target->scsi_host,
			     PFX "Sending CM REQ failed\n");
		comp = 1;
		target->status = -ECONNRESET;
		break;

	case IB_CM_REP_RECEIVED:
		comp = 1;
		srp_cm_rep_handler(cm_id, event->private_data, target);
		break;

	case IB_CM_REJ_RECEIVED:
		shost_printk(KERN_DEBUG, target->scsi_host, PFX "REJ received\n");
		comp = 1;

		srp_cm_rej_handler(cm_id, event, target);
		break;

	case IB_CM_DREQ_RECEIVED:
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "DREQ received - connection closed\n");
		srp_change_conn_state(target, false);
		if (ib_send_cm_drep(cm_id, NULL, 0))
			shost_printk(KERN_ERR, target->scsi_host,
				     PFX "Sending CM DREP failed\n");
		queue_work(system_long_wq, &target->tl_err_work);
		break;

	case IB_CM_TIMEWAIT_EXIT:
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "connection closed\n");
		comp = 1;

		target->status = 0;
		break;

	case IB_CM_MRA_RECEIVED:
	case IB_CM_DREQ_ERROR:
	case IB_CM_DREP_RECEIVED:
		break;

	default:
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Unhandled CM event %d\n", event->event);
		break;
	}

	if (comp)
		complete(&target->done);

	return 0;
}

/**
 * srp_change_queue_type - changing device queue tag type
 * @sdev: scsi device struct
 * @tag_type: requested tag type
 *
 * Returns queue tag type.
 */
static int
srp_change_queue_type(struct scsi_device *sdev, int tag_type)
{
	if (sdev->tagged_supported) {
		scsi_set_tag_type(sdev, tag_type);
		if (tag_type)
			scsi_activate_tcq(sdev, sdev->queue_depth);
		else
			scsi_deactivate_tcq(sdev, sdev->queue_depth);
	} else
		tag_type = 0;

	return tag_type;
}

/**
 * srp_change_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 * @reason: SCSI_QDEPTH_DEFAULT/SCSI_QDEPTH_QFULL/SCSI_QDEPTH_RAMP_UP
 * (see include/scsi/scsi_host.h for definition)
 *
 * Returns queue depth.
 */
static int
srp_change_queue_depth(struct scsi_device *sdev, int qdepth, int reason)
{
	struct Scsi_Host *shost = sdev->host;
	int max_depth;
	if (reason == SCSI_QDEPTH_DEFAULT || reason == SCSI_QDEPTH_RAMP_UP) {
		max_depth = shost->can_queue;
		if (!sdev->tagged_supported)
			max_depth = 1;
		if (qdepth > max_depth)
			qdepth = max_depth;
		scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
	} else if (reason == SCSI_QDEPTH_QFULL)
		scsi_track_queue_full(sdev, qdepth);
	else
		return -EOPNOTSUPP;

	return sdev->queue_depth;
}

static int srp_send_tsk_mgmt(struct srp_target_port *target,
			     u64 req_tag, unsigned int lun, u8 func)
{
	struct srp_rport *rport = target->rport;
	struct ib_device *dev = target->srp_host->srp_dev->dev;
	struct srp_iu *iu;
	struct srp_tsk_mgmt *tsk_mgmt;

	if (!target->connected || target->qp_in_error)
		return -1;

	init_completion(&target->tsk_mgmt_done);

	/*
	 * Lock the rport mutex to avoid that srp_create_target_ib() is
	 * invoked while a task management function is being sent.
	 */
	mutex_lock(&rport->mutex);
	spin_lock_irq(&target->lock);
	iu = __srp_get_tx_iu(target, SRP_IU_TSK_MGMT);
	spin_unlock_irq(&target->lock);

	if (!iu) {
		mutex_unlock(&rport->mutex);

		return -1;
	}

	ib_dma_sync_single_for_cpu(dev, iu->dma, sizeof *tsk_mgmt,
				   DMA_TO_DEVICE);
	tsk_mgmt = iu->buf;
	memset(tsk_mgmt, 0, sizeof *tsk_mgmt);

	tsk_mgmt->opcode 	= SRP_TSK_MGMT;
	tsk_mgmt->lun		= cpu_to_be64((u64) lun << 48);
	tsk_mgmt->tag		= req_tag | SRP_TAG_TSK_MGMT;
	tsk_mgmt->tsk_mgmt_func = func;
	tsk_mgmt->task_tag	= req_tag;

	ib_dma_sync_single_for_device(dev, iu->dma, sizeof *tsk_mgmt,
				      DMA_TO_DEVICE);
	if (srp_post_send(target, iu, sizeof *tsk_mgmt)) {
		srp_put_tx_iu(target, iu, SRP_IU_TSK_MGMT);
		mutex_unlock(&rport->mutex);

		return -1;
	}
	mutex_unlock(&rport->mutex);

	if (!wait_for_completion_timeout(&target->tsk_mgmt_done,
					 msecs_to_jiffies(SRP_ABORT_TIMEOUT_MS)))
		return -1;

	return 0;
}

static int srp_abort(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);
	struct srp_request *req = (struct srp_request *) scmnd->host_scribble;
	int ret;

	shost_printk(KERN_ERR, target->scsi_host, "SRP abort called\n");

	if (!req || !srp_claim_req(target, req, NULL, scmnd))
		return SUCCESS;
	if (srp_send_tsk_mgmt(target, req->index, scmnd->device->lun,
			      SRP_TSK_ABORT_TASK) == 0)
		ret = SUCCESS;
	else if (target->rport->state == SRP_RPORT_LOST)
		ret = FAST_IO_FAIL;
	else
		ret = FAILED;
	srp_free_req(target, req, scmnd, 0);
	scmnd->result = DID_ABORT << 16;
	scmnd->scsi_done(scmnd);

	return ret;
}

static int srp_reset_device(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);
	int i;

	shost_printk(KERN_ERR, target->scsi_host, "SRP reset_device called\n");

	if (srp_send_tsk_mgmt(target, SRP_TAG_NO_REQ, scmnd->device->lun,
			      SRP_TSK_LUN_RESET))
		return FAILED;
	if (target->tsk_mgmt_status)
		return FAILED;

	for (i = 0; i < target->req_ring_size; ++i) {
		struct srp_request *req = &target->req_ring[i];
		srp_finish_req(target, req, scmnd->device, DID_RESET << 16);
	}

	return SUCCESS;
}

static int srp_reset_host(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);

	shost_printk(KERN_ERR, target->scsi_host, PFX "SRP reset_host called\n");

	return srp_reconnect_rport(target->rport) == 0 ? SUCCESS : FAILED;
}

static int srp_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct srp_target_port *target = host_to_target(shost);
	struct request_queue *q = sdev->request_queue;
	unsigned long timeout;

	if (sdev->type == TYPE_DISK) {
		timeout = max_t(unsigned, 30 * HZ, target->rq_tmo_jiffies);
		blk_queue_rq_timeout(q, timeout);
	}

	return 0;
}

static ssize_t show_id_ext(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "0x%016llx\n",
		       (unsigned long long) be64_to_cpu(target->id_ext));
}

static ssize_t show_ioc_guid(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "0x%016llx\n",
		       (unsigned long long) be64_to_cpu(target->ioc_guid));
}

static ssize_t show_service_id(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "0x%016llx\n",
		       (unsigned long long) be64_to_cpu(target->service_id));
}

static ssize_t show_pkey(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "0x%04x\n", be16_to_cpu(target->path.pkey));
}

static ssize_t show_sgid(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%pI6\n", target->path.sgid.raw);
}

static ssize_t show_dgid(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%pI6\n", target->path.dgid.raw);
}

static ssize_t show_orig_dgid(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%pI6\n", target->orig_dgid);
}

static ssize_t show_req_lim(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%d\n", target->req_lim);
}

static ssize_t show_zero_req_lim(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%d\n", target->zero_req_lim);
}

static ssize_t show_local_ib_port(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%d\n", target->srp_host->port);
}

static ssize_t show_local_ib_device(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%s\n", target->srp_host->srp_dev->dev->name);
}

static ssize_t show_comp_vector(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%d\n", target->comp_vector);
}

static ssize_t show_tl_retry_count(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%d\n", target->tl_retry_count);
}

static ssize_t show_cmd_sg_entries(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%u\n", target->cmd_sg_cnt);
}

static ssize_t show_allow_ext_sg(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sprintf(buf, "%s\n", target->allow_ext_sg ? "true" : "false");
}

static DEVICE_ATTR(id_ext,	    S_IRUGO, show_id_ext,	   NULL);
static DEVICE_ATTR(ioc_guid,	    S_IRUGO, show_ioc_guid,	   NULL);
static DEVICE_ATTR(service_id,	    S_IRUGO, show_service_id,	   NULL);
static DEVICE_ATTR(pkey,	    S_IRUGO, show_pkey,		   NULL);
static DEVICE_ATTR(sgid,	    S_IRUGO, show_sgid,		   NULL);
static DEVICE_ATTR(dgid,	    S_IRUGO, show_dgid,		   NULL);
static DEVICE_ATTR(orig_dgid,	    S_IRUGO, show_orig_dgid,	   NULL);
static DEVICE_ATTR(req_lim,         S_IRUGO, show_req_lim,         NULL);
static DEVICE_ATTR(zero_req_lim,    S_IRUGO, show_zero_req_lim,	   NULL);
static DEVICE_ATTR(local_ib_port,   S_IRUGO, show_local_ib_port,   NULL);
static DEVICE_ATTR(local_ib_device, S_IRUGO, show_local_ib_device, NULL);
static DEVICE_ATTR(comp_vector,     S_IRUGO, show_comp_vector,     NULL);
static DEVICE_ATTR(tl_retry_count,  S_IRUGO, show_tl_retry_count,  NULL);
static DEVICE_ATTR(cmd_sg_entries,  S_IRUGO, show_cmd_sg_entries,  NULL);
static DEVICE_ATTR(allow_ext_sg,    S_IRUGO, show_allow_ext_sg,    NULL);

static struct device_attribute *srp_host_attrs[] = {
	&dev_attr_id_ext,
	&dev_attr_ioc_guid,
	&dev_attr_service_id,
	&dev_attr_pkey,
	&dev_attr_sgid,
	&dev_attr_dgid,
	&dev_attr_orig_dgid,
	&dev_attr_req_lim,
	&dev_attr_zero_req_lim,
	&dev_attr_local_ib_port,
	&dev_attr_local_ib_device,
	&dev_attr_comp_vector,
	&dev_attr_tl_retry_count,
	&dev_attr_cmd_sg_entries,
	&dev_attr_allow_ext_sg,
	NULL
};

static struct scsi_host_template srp_template = {
	.module				= THIS_MODULE,
	.name				= "InfiniBand SRP initiator",
	.proc_name			= DRV_NAME,
	.slave_configure		= srp_slave_configure,
	.info				= srp_target_info,
	.queuecommand			= srp_queuecommand,
	.change_queue_depth             = srp_change_queue_depth,
	.change_queue_type              = srp_change_queue_type,
	.eh_abort_handler		= srp_abort,
	.eh_device_reset_handler	= srp_reset_device,
	.eh_host_reset_handler		= srp_reset_host,
	.skip_settle_delay		= true,
	.sg_tablesize			= SRP_DEF_SG_TABLESIZE,
	.can_queue			= SRP_DEFAULT_CMD_SQ_SIZE,
	.this_id			= -1,
	.cmd_per_lun			= SRP_DEFAULT_CMD_SQ_SIZE,
	.use_clustering			= ENABLE_CLUSTERING,
	.shost_attrs			= srp_host_attrs
};

static int srp_add_target(struct srp_host *host, struct srp_target_port *target)
{
	struct srp_rport_identifiers ids;
	struct srp_rport *rport;

	sprintf(target->target_name, "SRP.T10:%016llX",
		 (unsigned long long) be64_to_cpu(target->id_ext));

	if (scsi_add_host(target->scsi_host, host->srp_dev->dev->dma_device))
		return -ENODEV;

	memcpy(ids.port_id, &target->id_ext, 8);
	memcpy(ids.port_id + 8, &target->ioc_guid, 8);
	ids.roles = SRP_RPORT_ROLE_TARGET;
	rport = srp_rport_add(target->scsi_host, &ids);
	if (IS_ERR(rport)) {
		scsi_remove_host(target->scsi_host);
		return PTR_ERR(rport);
	}

	rport->lld_data = target;
	target->rport = rport;

	spin_lock(&host->target_lock);
	list_add_tail(&target->list, &host->target_list);
	spin_unlock(&host->target_lock);

	target->state = SRP_TARGET_LIVE;

	scsi_scan_target(&target->scsi_host->shost_gendev,
			 0, target->scsi_id, SCAN_WILD_CARD, 0);

	return 0;
}

static void srp_release_dev(struct device *dev)
{
	struct srp_host *host =
		container_of(dev, struct srp_host, dev);

	complete(&host->released);
}

static struct class srp_class = {
	.name    = "infiniband_srp",
	.dev_release = srp_release_dev
};

/**
 * srp_conn_unique() - check whether the connection to a target is unique
 * @host:   SRP host.
 * @target: SRP target port.
 */
static bool srp_conn_unique(struct srp_host *host,
			    struct srp_target_port *target)
{
	struct srp_target_port *t;
	bool ret = false;

	if (target->state == SRP_TARGET_REMOVED)
		goto out;

	ret = true;

	spin_lock(&host->target_lock);
	list_for_each_entry(t, &host->target_list, list) {
		if (t != target &&
		    target->id_ext == t->id_ext &&
		    target->ioc_guid == t->ioc_guid &&
		    target->initiator_ext == t->initiator_ext) {
			ret = false;
			break;
		}
	}
	spin_unlock(&host->target_lock);

out:
	return ret;
}

/*
 * Target ports are added by writing
 *
 *     id_ext=<SRP ID ext>,ioc_guid=<SRP IOC GUID>,dgid=<dest GID>,
 *     pkey=<P_Key>,service_id=<service ID>
 *
 * to the add_target sysfs attribute.
 */
enum {
	SRP_OPT_ERR		= 0,
	SRP_OPT_ID_EXT		= 1 << 0,
	SRP_OPT_IOC_GUID	= 1 << 1,
	SRP_OPT_DGID		= 1 << 2,
	SRP_OPT_PKEY		= 1 << 3,
	SRP_OPT_SERVICE_ID	= 1 << 4,
	SRP_OPT_MAX_SECT	= 1 << 5,
	SRP_OPT_MAX_CMD_PER_LUN	= 1 << 6,
	SRP_OPT_IO_CLASS	= 1 << 7,
	SRP_OPT_INITIATOR_EXT	= 1 << 8,
	SRP_OPT_CMD_SG_ENTRIES	= 1 << 9,
	SRP_OPT_ALLOW_EXT_SG	= 1 << 10,
	SRP_OPT_SG_TABLESIZE	= 1 << 11,
	SRP_OPT_COMP_VECTOR	= 1 << 12,
	SRP_OPT_TL_RETRY_COUNT	= 1 << 13,
	SRP_OPT_QUEUE_SIZE	= 1 << 14,
	SRP_OPT_ALL		= (SRP_OPT_ID_EXT	|
				   SRP_OPT_IOC_GUID	|
				   SRP_OPT_DGID		|
				   SRP_OPT_PKEY		|
				   SRP_OPT_SERVICE_ID),
};

static const match_table_t srp_opt_tokens = {
	{ SRP_OPT_ID_EXT,		"id_ext=%s" 		},
	{ SRP_OPT_IOC_GUID,		"ioc_guid=%s" 		},
	{ SRP_OPT_DGID,			"dgid=%s" 		},
	{ SRP_OPT_PKEY,			"pkey=%x" 		},
	{ SRP_OPT_SERVICE_ID,		"service_id=%s"		},
	{ SRP_OPT_MAX_SECT,		"max_sect=%d" 		},
	{ SRP_OPT_MAX_CMD_PER_LUN,	"max_cmd_per_lun=%d" 	},
	{ SRP_OPT_IO_CLASS,		"io_class=%x"		},
	{ SRP_OPT_INITIATOR_EXT,	"initiator_ext=%s"	},
	{ SRP_OPT_CMD_SG_ENTRIES,	"cmd_sg_entries=%u"	},
	{ SRP_OPT_ALLOW_EXT_SG,		"allow_ext_sg=%u"	},
	{ SRP_OPT_SG_TABLESIZE,		"sg_tablesize=%u"	},
	{ SRP_OPT_COMP_VECTOR,		"comp_vector=%u"	},
	{ SRP_OPT_TL_RETRY_COUNT,	"tl_retry_count=%u"	},
	{ SRP_OPT_QUEUE_SIZE,		"queue_size=%d"		},
	{ SRP_OPT_ERR,			NULL 			}
};

static int srp_parse_options(const char *buf, struct srp_target_port *target)
{
	char *options, *sep_opt;
	char *p;
	char dgid[3];
	substring_t args[MAX_OPT_ARGS];
	int opt_mask = 0;
	int token;
	int ret = -EINVAL;
	int i;

	options = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	sep_opt = options;
	while ((p = strsep(&sep_opt, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, srp_opt_tokens, args);
		opt_mask |= token;

		switch (token) {
		case SRP_OPT_ID_EXT:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			target->id_ext = cpu_to_be64(simple_strtoull(p, NULL, 16));
			kfree(p);
			break;

		case SRP_OPT_IOC_GUID:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			target->ioc_guid = cpu_to_be64(simple_strtoull(p, NULL, 16));
			kfree(p);
			break;

		case SRP_OPT_DGID:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			if (strlen(p) != 32) {
				pr_warn("bad dest GID parameter '%s'\n", p);
				kfree(p);
				goto out;
			}

			for (i = 0; i < 16; ++i) {
				strlcpy(dgid, p + i * 2, 3);
				target->path.dgid.raw[i] = simple_strtoul(dgid, NULL, 16);
			}
			kfree(p);
			memcpy(target->orig_dgid, target->path.dgid.raw, 16);
			break;

		case SRP_OPT_PKEY:
			if (match_hex(args, &token)) {
				pr_warn("bad P_Key parameter '%s'\n", p);
				goto out;
			}
			target->path.pkey = cpu_to_be16(token);
			break;

		case SRP_OPT_SERVICE_ID:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			target->service_id = cpu_to_be64(simple_strtoull(p, NULL, 16));
			target->path.service_id = target->service_id;
			kfree(p);
			break;

		case SRP_OPT_MAX_SECT:
			if (match_int(args, &token)) {
				pr_warn("bad max sect parameter '%s'\n", p);
				goto out;
			}
			target->scsi_host->max_sectors = token;
			break;

		case SRP_OPT_QUEUE_SIZE:
			if (match_int(args, &token) || token < 1) {
				pr_warn("bad queue_size parameter '%s'\n", p);
				goto out;
			}
			target->scsi_host->can_queue = token;
			target->queue_size = token + SRP_RSP_SQ_SIZE +
					     SRP_TSK_MGMT_SQ_SIZE;
			if (!(opt_mask & SRP_OPT_MAX_CMD_PER_LUN))
				target->scsi_host->cmd_per_lun = token;
			break;

		case SRP_OPT_MAX_CMD_PER_LUN:
			if (match_int(args, &token) || token < 1) {
				pr_warn("bad max cmd_per_lun parameter '%s'\n",
					p);
				goto out;
			}
			target->scsi_host->cmd_per_lun = token;
			break;

		case SRP_OPT_IO_CLASS:
			if (match_hex(args, &token)) {
				pr_warn("bad IO class parameter '%s'\n", p);
				goto out;
			}
			if (token != SRP_REV10_IB_IO_CLASS &&
			    token != SRP_REV16A_IB_IO_CLASS) {
				pr_warn("unknown IO class parameter value %x specified (use %x or %x).\n",
					token, SRP_REV10_IB_IO_CLASS,
					SRP_REV16A_IB_IO_CLASS);
				goto out;
			}
			target->io_class = token;
			break;

		case SRP_OPT_INITIATOR_EXT:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			target->initiator_ext = cpu_to_be64(simple_strtoull(p, NULL, 16));
			kfree(p);
			break;

		case SRP_OPT_CMD_SG_ENTRIES:
			if (match_int(args, &token) || token < 1 || token > 255) {
				pr_warn("bad max cmd_sg_entries parameter '%s'\n",
					p);
				goto out;
			}
			target->cmd_sg_cnt = token;
			break;

		case SRP_OPT_ALLOW_EXT_SG:
			if (match_int(args, &token)) {
				pr_warn("bad allow_ext_sg parameter '%s'\n", p);
				goto out;
			}
			target->allow_ext_sg = !!token;
			break;

		case SRP_OPT_SG_TABLESIZE:
			if (match_int(args, &token) || token < 1 ||
					token > SCSI_MAX_SG_CHAIN_SEGMENTS) {
				pr_warn("bad max sg_tablesize parameter '%s'\n",
					p);
				goto out;
			}
			target->sg_tablesize = token;
			break;

		case SRP_OPT_COMP_VECTOR:
			if (match_int(args, &token) || token < 0) {
				pr_warn("bad comp_vector parameter '%s'\n", p);
				goto out;
			}
			target->comp_vector = token;
			break;

		case SRP_OPT_TL_RETRY_COUNT:
			if (match_int(args, &token) || token < 2 || token > 7) {
				pr_warn("bad tl_retry_count parameter '%s' (must be a number between 2 and 7)\n",
					p);
				goto out;
			}
			target->tl_retry_count = token;
			break;

		default:
			pr_warn("unknown parameter or missing value '%s' in target creation request\n",
				p);
			goto out;
		}
	}

	if ((opt_mask & SRP_OPT_ALL) == SRP_OPT_ALL)
		ret = 0;
	else
		for (i = 0; i < ARRAY_SIZE(srp_opt_tokens); ++i)
			if ((srp_opt_tokens[i].token & SRP_OPT_ALL) &&
			    !(srp_opt_tokens[i].token & opt_mask))
				pr_warn("target creation request is missing parameter '%s'\n",
					srp_opt_tokens[i].pattern);

	if (target->scsi_host->cmd_per_lun > target->scsi_host->can_queue
	    && (opt_mask & SRP_OPT_MAX_CMD_PER_LUN))
		pr_warn("cmd_per_lun = %d > queue_size = %d\n",
			target->scsi_host->cmd_per_lun,
			target->scsi_host->can_queue);

out:
	kfree(options);
	return ret;
}

static ssize_t srp_create_target(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct srp_host *host =
		container_of(dev, struct srp_host, dev);
	struct Scsi_Host *target_host;
	struct srp_target_port *target;
	struct srp_device *srp_dev = host->srp_dev;
	struct ib_device *ibdev = srp_dev->dev;
	int ret;

	target_host = scsi_host_alloc(&srp_template,
				      sizeof (struct srp_target_port));
	if (!target_host)
		return -ENOMEM;

	target_host->transportt  = ib_srp_transport_template;
	target_host->max_channel = 0;
	target_host->max_id      = 1;
	target_host->max_lun     = SRP_MAX_LUN;
	target_host->max_cmd_len = sizeof ((struct srp_cmd *) (void *) 0L)->cdb;

	target = host_to_target(target_host);

	target->io_class	= SRP_REV16A_IB_IO_CLASS;
	target->scsi_host	= target_host;
	target->srp_host	= host;
	target->lkey		= host->srp_dev->mr->lkey;
	target->rkey		= host->srp_dev->mr->rkey;
	target->cmd_sg_cnt	= cmd_sg_entries;
	target->sg_tablesize	= indirect_sg_entries ? : cmd_sg_entries;
	target->allow_ext_sg	= allow_ext_sg;
	target->tl_retry_count	= 7;
	target->queue_size	= SRP_DEFAULT_QUEUE_SIZE;

	mutex_lock(&host->add_target_mutex);

	ret = srp_parse_options(buf, target);
	if (ret)
		goto err;

	target->req_ring_size = target->queue_size - SRP_TSK_MGMT_SQ_SIZE;

	if (!srp_conn_unique(target->srp_host, target)) {
		shost_printk(KERN_INFO, target->scsi_host,
			     PFX "Already connected to target port with id_ext=%016llx;ioc_guid=%016llx;initiator_ext=%016llx\n",
			     be64_to_cpu(target->id_ext),
			     be64_to_cpu(target->ioc_guid),
			     be64_to_cpu(target->initiator_ext));
		ret = -EEXIST;
		goto err;
	}

	if (!srp_dev->has_fmr && !srp_dev->has_fr && !target->allow_ext_sg &&
	    target->cmd_sg_cnt < target->sg_tablesize) {
		pr_warn("No MR pool and no external indirect descriptors, limiting sg_tablesize to cmd_sg_cnt\n");
		target->sg_tablesize = target->cmd_sg_cnt;
	}

	target_host->sg_tablesize = target->sg_tablesize;
	target->indirect_size = target->sg_tablesize *
				sizeof (struct srp_direct_buf);
	target->max_iu_len = sizeof (struct srp_cmd) +
			     sizeof (struct srp_indirect_buf) +
			     target->cmd_sg_cnt * sizeof (struct srp_direct_buf);

	INIT_WORK(&target->tl_err_work, srp_tl_err_work);
	INIT_WORK(&target->remove_work, srp_remove_work);
	spin_lock_init(&target->lock);
	INIT_LIST_HEAD(&target->free_tx);
	ret = srp_alloc_req_data(target);
	if (ret)
		goto err_free_mem;

	ret = ib_query_gid(ibdev, host->port, 0, &target->path.sgid);
	if (ret)
		goto err_free_mem;

	ret = srp_create_target_ib(target);
	if (ret)
		goto err_free_mem;

	ret = srp_new_cm_id(target);
	if (ret)
		goto err_free_ib;

	ret = srp_connect_target(target);
	if (ret) {
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "Connection failed\n");
		goto err_cm_id;
	}

	ret = srp_add_target(host, target);
	if (ret)
		goto err_disconnect;

	shost_printk(KERN_DEBUG, target->scsi_host, PFX
		     "new target: id_ext %016llx ioc_guid %016llx pkey %04x service_id %016llx sgid %pI6 dgid %pI6\n",
		     be64_to_cpu(target->id_ext),
		     be64_to_cpu(target->ioc_guid),
		     be16_to_cpu(target->path.pkey),
		     be64_to_cpu(target->service_id),
		     target->path.sgid.raw, target->path.dgid.raw);

	ret = count;

out:
	mutex_unlock(&host->add_target_mutex);
	return ret;

err_disconnect:
	srp_disconnect_target(target);

err_cm_id:
	ib_destroy_cm_id(target->cm_id);

err_free_ib:
	srp_free_target_ib(target);

err_free_mem:
	srp_free_req_data(target);

err:
	scsi_host_put(target_host);
	goto out;
}

static DEVICE_ATTR(add_target, S_IWUSR, NULL, srp_create_target);

static ssize_t show_ibdev(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct srp_host *host = container_of(dev, struct srp_host, dev);

	return sprintf(buf, "%s\n", host->srp_dev->dev->name);
}

static DEVICE_ATTR(ibdev, S_IRUGO, show_ibdev, NULL);

static ssize_t show_port(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_host *host = container_of(dev, struct srp_host, dev);

	return sprintf(buf, "%d\n", host->port);
}

static DEVICE_ATTR(port, S_IRUGO, show_port, NULL);

static struct srp_host *srp_add_port(struct srp_device *device, u8 port)
{
	struct srp_host *host;

	host = kzalloc(sizeof *host, GFP_KERNEL);
	if (!host)
		return NULL;

	INIT_LIST_HEAD(&host->target_list);
	spin_lock_init(&host->target_lock);
	init_completion(&host->released);
	mutex_init(&host->add_target_mutex);
	host->srp_dev = device;
	host->port = port;

	host->dev.class = &srp_class;
	host->dev.parent = device->dev->dma_device;
	dev_set_name(&host->dev, "srp-%s-%d", device->dev->name, port);

	if (device_register(&host->dev))
		goto free_host;
	if (device_create_file(&host->dev, &dev_attr_add_target))
		goto err_class;
	if (device_create_file(&host->dev, &dev_attr_ibdev))
		goto err_class;
	if (device_create_file(&host->dev, &dev_attr_port))
		goto err_class;

	return host;

err_class:
	device_unregister(&host->dev);

free_host:
	kfree(host);

	return NULL;
}

static void srp_add_one(struct ib_device *device)
{
	struct srp_device *srp_dev;
	struct ib_device_attr *dev_attr;
	struct srp_host *host;
	int mr_page_shift, s, e, p;
	u64 max_pages_per_mr;

	dev_attr = kmalloc(sizeof *dev_attr, GFP_KERNEL);
	if (!dev_attr)
		return;

	if (ib_query_device(device, dev_attr)) {
		pr_warn("Query device failed for %s\n", device->name);
		goto free_attr;
	}

	srp_dev = kmalloc(sizeof *srp_dev, GFP_KERNEL);
	if (!srp_dev)
		goto free_attr;

	srp_dev->has_fmr = (device->alloc_fmr && device->dealloc_fmr &&
			    device->map_phys_fmr && device->unmap_fmr);
	srp_dev->has_fr = (dev_attr->device_cap_flags &
			   IB_DEVICE_MEM_MGT_EXTENSIONS);
	if (!srp_dev->has_fmr && !srp_dev->has_fr)
		dev_warn(&device->dev, "neither FMR nor FR is supported\n");

	srp_dev->use_fast_reg = (srp_dev->has_fr &&
				 (!srp_dev->has_fmr || prefer_fr));

	/*
	 * Use the smallest page size supported by the HCA, down to a
	 * minimum of 4096 bytes. We're unlikely to build large sglists
	 * out of smaller entries.
	 */
	mr_page_shift		= max(12, ffs(dev_attr->page_size_cap) - 1);
	srp_dev->mr_page_size	= 1 << mr_page_shift;
	srp_dev->mr_page_mask	= ~((u64) srp_dev->mr_page_size - 1);
	max_pages_per_mr	= dev_attr->max_mr_size;
	do_div(max_pages_per_mr, srp_dev->mr_page_size);
	srp_dev->max_pages_per_mr = min_t(u64, SRP_MAX_PAGES_PER_MR,
					  max_pages_per_mr);
	if (srp_dev->use_fast_reg) {
		srp_dev->max_pages_per_mr =
			min_t(u32, srp_dev->max_pages_per_mr,
			      dev_attr->max_fast_reg_page_list_len);
	}
	srp_dev->mr_max_size	= srp_dev->mr_page_size *
				   srp_dev->max_pages_per_mr;
	pr_debug("%s: mr_page_shift = %d, dev_attr->max_mr_size = %#llx, dev_attr->max_fast_reg_page_list_len = %u, max_pages_per_mr = %d, mr_max_size = %#x\n",
		 device->name, mr_page_shift, dev_attr->max_mr_size,
		 dev_attr->max_fast_reg_page_list_len,
		 srp_dev->max_pages_per_mr, srp_dev->mr_max_size);

	INIT_LIST_HEAD(&srp_dev->dev_list);

	srp_dev->dev = device;
	srp_dev->pd  = ib_alloc_pd(device);
	if (IS_ERR(srp_dev->pd))
		goto free_dev;

	srp_dev->mr = ib_get_dma_mr(srp_dev->pd,
				    IB_ACCESS_LOCAL_WRITE |
				    IB_ACCESS_REMOTE_READ |
				    IB_ACCESS_REMOTE_WRITE);
	if (IS_ERR(srp_dev->mr))
		goto err_pd;

	if (device->node_type == RDMA_NODE_IB_SWITCH) {
		s = 0;
		e = 0;
	} else {
		s = 1;
		e = device->phys_port_cnt;
	}

	for (p = s; p <= e; ++p) {
		host = srp_add_port(srp_dev, p);
		if (host)
			list_add_tail(&host->list, &srp_dev->dev_list);
	}

	ib_set_client_data(device, &srp_client, srp_dev);

	goto free_attr;

err_pd:
	ib_dealloc_pd(srp_dev->pd);

free_dev:
	kfree(srp_dev);

free_attr:
	kfree(dev_attr);
}

static void srp_remove_one(struct ib_device *device)
{
	struct srp_device *srp_dev;
	struct srp_host *host, *tmp_host;
	struct srp_target_port *target;

	srp_dev = ib_get_client_data(device, &srp_client);
	if (!srp_dev)
		return;

	list_for_each_entry_safe(host, tmp_host, &srp_dev->dev_list, list) {
		device_unregister(&host->dev);
		/*
		 * Wait for the sysfs entry to go away, so that no new
		 * target ports can be created.
		 */
		wait_for_completion(&host->released);

		/*
		 * Remove all target ports.
		 */
		spin_lock(&host->target_lock);
		list_for_each_entry(target, &host->target_list, list)
			srp_queue_remove_work(target);
		spin_unlock(&host->target_lock);

		/*
		 * Wait for tl_err and target port removal tasks.
		 */
		flush_workqueue(system_long_wq);
		flush_workqueue(srp_remove_wq);

		kfree(host);
	}

	ib_dereg_mr(srp_dev->mr);
	ib_dealloc_pd(srp_dev->pd);

	kfree(srp_dev);
}

static struct srp_function_template ib_srp_transport_functions = {
	.has_rport_state	 = true,
	.reset_timer_if_blocked	 = true,
	.reconnect_delay	 = &srp_reconnect_delay,
	.fast_io_fail_tmo	 = &srp_fast_io_fail_tmo,
	.dev_loss_tmo		 = &srp_dev_loss_tmo,
	.reconnect		 = srp_rport_reconnect,
	.rport_delete		 = srp_rport_delete,
	.terminate_rport_io	 = srp_terminate_io,
};

static int __init srp_init_module(void)
{
	int ret;

	BUILD_BUG_ON(FIELD_SIZEOF(struct ib_wc, wr_id) < sizeof(void *));

	if (srp_sg_tablesize) {
		pr_warn("srp_sg_tablesize is deprecated, please use cmd_sg_entries\n");
		if (!cmd_sg_entries)
			cmd_sg_entries = srp_sg_tablesize;
	}

	if (!cmd_sg_entries)
		cmd_sg_entries = SRP_DEF_SG_TABLESIZE;

	if (cmd_sg_entries > 255) {
		pr_warn("Clamping cmd_sg_entries to 255\n");
		cmd_sg_entries = 255;
	}

	if (!indirect_sg_entries)
		indirect_sg_entries = cmd_sg_entries;
	else if (indirect_sg_entries < cmd_sg_entries) {
		pr_warn("Bumping up indirect_sg_entries to match cmd_sg_entries (%u)\n",
			cmd_sg_entries);
		indirect_sg_entries = cmd_sg_entries;
	}

	srp_remove_wq = create_workqueue("srp_remove");
	if (IS_ERR(srp_remove_wq)) {
		ret = PTR_ERR(srp_remove_wq);
		goto out;
	}

	ret = -ENOMEM;
	ib_srp_transport_template =
		srp_attach_transport(&ib_srp_transport_functions);
	if (!ib_srp_transport_template)
		goto destroy_wq;

	ret = class_register(&srp_class);
	if (ret) {
		pr_err("couldn't register class infiniband_srp\n");
		goto release_tr;
	}

	ib_sa_register_client(&srp_sa_client);

	ret = ib_register_client(&srp_client);
	if (ret) {
		pr_err("couldn't register IB client\n");
		goto unreg_sa;
	}

out:
	return ret;

unreg_sa:
	ib_sa_unregister_client(&srp_sa_client);
	class_unregister(&srp_class);

release_tr:
	srp_release_transport(ib_srp_transport_template);

destroy_wq:
	destroy_workqueue(srp_remove_wq);
	goto out;
}

static void __exit srp_cleanup_module(void)
{
	ib_unregister_client(&srp_client);
	ib_sa_unregister_client(&srp_sa_client);
	class_unregister(&srp_class);
	srp_release_transport(ib_srp_transport_template);
	destroy_workqueue(srp_remove_wq);
}

module_init(srp_init_module);
module_exit(srp_cleanup_module);
