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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/jiffies.h>

#include <asm/atomic.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_dbg.h>
#include <scsi/srp.h>
#include <scsi/scsi_transport_srp.h>

#include "ib_srp.h"

#define DRV_NAME	"ib_srp"
#define PFX		DRV_NAME ": "
#define DRV_VERSION	"0.2"
#define DRV_RELDATE	"November 1, 2005"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("InfiniBand SCSI RDMA Protocol initiator "
		   "v" DRV_VERSION " (" DRV_RELDATE ")");
MODULE_LICENSE("Dual BSD/GPL");

static int srp_sg_tablesize = SRP_DEF_SG_TABLESIZE;
static int srp_max_iu_len;

module_param(srp_sg_tablesize, int, 0444);
MODULE_PARM_DESC(srp_sg_tablesize,
		 "Max number of gather/scatter entries per I/O (default is 12, max 255)");

static int topspin_workarounds = 1;

module_param(topspin_workarounds, int, 0444);
MODULE_PARM_DESC(topspin_workarounds,
		 "Enable workarounds for Topspin/Cisco SRP target bugs if != 0");

static int mellanox_workarounds = 1;

module_param(mellanox_workarounds, int, 0444);
MODULE_PARM_DESC(mellanox_workarounds,
		 "Enable workarounds for Mellanox SRP target bugs if != 0");

static void srp_add_one(struct ib_device *device);
static void srp_remove_one(struct ib_device *device);
static void srp_recv_completion(struct ib_cq *cq, void *target_ptr);
static void srp_send_completion(struct ib_cq *cq, void *target_ptr);
static int srp_cm_handler(struct ib_cm_id *cm_id, struct ib_cm_event *event);

static struct scsi_transport_template *ib_srp_transport_template;

static struct ib_client srp_client = {
	.name   = "srp",
	.add    = srp_add_one,
	.remove = srp_remove_one
};

static struct ib_sa_client srp_sa_client;

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

static int srp_target_is_mellanox(struct srp_target_port *target)
{
	static const u8 mellanox_oui[3] = { 0x00, 0x02, 0xc9 };

	return mellanox_workarounds &&
		!memcmp(&target->ioc_guid, mellanox_oui, sizeof mellanox_oui);
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
	printk(KERN_ERR PFX "QP event %d\n", event->event);
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

static int srp_create_target_ib(struct srp_target_port *target)
{
	struct ib_qp_init_attr *init_attr;
	int ret;

	init_attr = kzalloc(sizeof *init_attr, GFP_KERNEL);
	if (!init_attr)
		return -ENOMEM;

	target->recv_cq = ib_create_cq(target->srp_host->srp_dev->dev,
				       srp_recv_completion, NULL, target, SRP_RQ_SIZE, 0);
	if (IS_ERR(target->recv_cq)) {
		ret = PTR_ERR(target->recv_cq);
		goto err;
	}

	target->send_cq = ib_create_cq(target->srp_host->srp_dev->dev,
				       srp_send_completion, NULL, target, SRP_SQ_SIZE, 0);
	if (IS_ERR(target->send_cq)) {
		ret = PTR_ERR(target->send_cq);
		goto err_recv_cq;
	}

	ib_req_notify_cq(target->recv_cq, IB_CQ_NEXT_COMP);

	init_attr->event_handler       = srp_qp_event;
	init_attr->cap.max_send_wr     = SRP_SQ_SIZE;
	init_attr->cap.max_recv_wr     = SRP_RQ_SIZE;
	init_attr->cap.max_recv_sge    = 1;
	init_attr->cap.max_send_sge    = 1;
	init_attr->sq_sig_type         = IB_SIGNAL_ALL_WR;
	init_attr->qp_type             = IB_QPT_RC;
	init_attr->send_cq             = target->send_cq;
	init_attr->recv_cq             = target->recv_cq;

	target->qp = ib_create_qp(target->srp_host->srp_dev->pd, init_attr);
	if (IS_ERR(target->qp)) {
		ret = PTR_ERR(target->qp);
		goto err_send_cq;
	}

	ret = srp_init_qp(target, target->qp);
	if (ret)
		goto err_qp;

	kfree(init_attr);
	return 0;

err_qp:
	ib_destroy_qp(target->qp);

err_send_cq:
	ib_destroy_cq(target->send_cq);

err_recv_cq:
	ib_destroy_cq(target->recv_cq);

err:
	kfree(init_attr);
	return ret;
}

static void srp_free_target_ib(struct srp_target_port *target)
{
	int i;

	ib_destroy_qp(target->qp);
	ib_destroy_cq(target->send_cq);
	ib_destroy_cq(target->recv_cq);

	for (i = 0; i < SRP_RQ_SIZE; ++i)
		srp_free_iu(target->srp_host, target->rx_ring[i]);
	for (i = 0; i < SRP_SQ_SIZE + 1; ++i)
		srp_free_iu(target->srp_host, target->tx_ring[i]);
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

	wait_for_completion(&target->done);

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
	req->param.retry_count 		      = 7;
	req->param.rnr_retry_count 	      = 7;
	req->param.max_cm_retries 	      = 15;

	req->priv.opcode     	= SRP_LOGIN_REQ;
	req->priv.tag        	= 0;
	req->priv.req_it_iu_len = cpu_to_be32(srp_max_iu_len);
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

static void srp_disconnect_target(struct srp_target_port *target)
{
	/* XXX should send SRP_I_LOGOUT request */

	init_completion(&target->done);
	if (ib_send_cm_dreq(target->cm_id, NULL, 0)) {
		shost_printk(KERN_DEBUG, target->scsi_host,
			     PFX "Sending CM DREQ failed\n");
		return;
	}
	wait_for_completion(&target->done);
}

static void srp_remove_work(struct work_struct *work)
{
	struct srp_target_port *target =
		container_of(work, struct srp_target_port, work);

	spin_lock_irq(target->scsi_host->host_lock);
	if (target->state != SRP_TARGET_DEAD) {
		spin_unlock_irq(target->scsi_host->host_lock);
		return;
	}
	target->state = SRP_TARGET_REMOVED;
	spin_unlock_irq(target->scsi_host->host_lock);

	spin_lock(&target->srp_host->target_lock);
	list_del(&target->list);
	spin_unlock(&target->srp_host->target_lock);

	srp_remove_host(target->scsi_host);
	scsi_remove_host(target->scsi_host);
	ib_destroy_cm_id(target->cm_id);
	srp_free_target_ib(target);
	scsi_host_put(target->scsi_host);
}

static int srp_connect_target(struct srp_target_port *target)
{
	int retries = 3;
	int ret;

	ret = srp_lookup_path(target);
	if (ret)
		return ret;

	while (1) {
		init_completion(&target->done);
		ret = srp_send_req(target);
		if (ret)
			return ret;
		wait_for_completion(&target->done);

		/*
		 * The CM event handling code will set status to
		 * SRP_PORT_REDIRECT if we get a port redirect REJ
		 * back, or SRP_DLID_REDIRECT if we get a lid/qp
		 * redirect REJ back.
		 */
		switch (target->status) {
		case 0:
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

static void srp_unmap_data(struct scsi_cmnd *scmnd,
			   struct srp_target_port *target,
			   struct srp_request *req)
{
	if (!scsi_sglist(scmnd) ||
	    (scmnd->sc_data_direction != DMA_TO_DEVICE &&
	     scmnd->sc_data_direction != DMA_FROM_DEVICE))
		return;

	if (req->fmr) {
		ib_fmr_pool_unmap(req->fmr);
		req->fmr = NULL;
	}

	ib_dma_unmap_sg(target->srp_host->srp_dev->dev, scsi_sglist(scmnd),
			scsi_sg_count(scmnd), scmnd->sc_data_direction);
}

static void srp_remove_req(struct srp_target_port *target, struct srp_request *req)
{
	srp_unmap_data(req->scmnd, target, req);
	list_move_tail(&req->list, &target->free_reqs);
}

static void srp_reset_req(struct srp_target_port *target, struct srp_request *req)
{
	req->scmnd->result = DID_RESET << 16;
	req->scmnd->scsi_done(req->scmnd);
	srp_remove_req(target, req);
}

static int srp_reconnect_target(struct srp_target_port *target)
{
	struct ib_qp_attr qp_attr;
	struct srp_request *req, *tmp;
	struct ib_wc wc;
	int ret;

	spin_lock_irq(target->scsi_host->host_lock);
	if (target->state != SRP_TARGET_LIVE) {
		spin_unlock_irq(target->scsi_host->host_lock);
		return -EAGAIN;
	}
	target->state = SRP_TARGET_CONNECTING;
	spin_unlock_irq(target->scsi_host->host_lock);

	srp_disconnect_target(target);
	/*
	 * Now get a new local CM ID so that we avoid confusing the
	 * target in case things are really fouled up.
	 */
	ret = srp_new_cm_id(target);
	if (ret)
		goto err;

	qp_attr.qp_state = IB_QPS_RESET;
	ret = ib_modify_qp(target->qp, &qp_attr, IB_QP_STATE);
	if (ret)
		goto err;

	ret = srp_init_qp(target, target->qp);
	if (ret)
		goto err;

	while (ib_poll_cq(target->recv_cq, 1, &wc) > 0)
		; /* nothing */
	while (ib_poll_cq(target->send_cq, 1, &wc) > 0)
		; /* nothing */

	spin_lock_irq(target->scsi_host->host_lock);
	list_for_each_entry_safe(req, tmp, &target->req_queue, list)
		srp_reset_req(target, req);
	spin_unlock_irq(target->scsi_host->host_lock);

	target->rx_head	 = 0;
	target->tx_head	 = 0;
	target->tx_tail  = 0;

	target->qp_in_error = 0;
	ret = srp_connect_target(target);
	if (ret)
		goto err;

	spin_lock_irq(target->scsi_host->host_lock);
	if (target->state == SRP_TARGET_CONNECTING) {
		ret = 0;
		target->state = SRP_TARGET_LIVE;
	} else
		ret = -EAGAIN;
	spin_unlock_irq(target->scsi_host->host_lock);

	return ret;

err:
	shost_printk(KERN_ERR, target->scsi_host,
		     PFX "reconnect failed (%d), removing target port.\n", ret);

	/*
	 * We couldn't reconnect, so kill our target port off.
	 * However, we have to defer the real removal because we might
	 * be in the context of the SCSI error handler now, which
	 * would deadlock if we call scsi_remove_host().
	 */
	spin_lock_irq(target->scsi_host->host_lock);
	if (target->state == SRP_TARGET_CONNECTING) {
		target->state = SRP_TARGET_DEAD;
		INIT_WORK(&target->work, srp_remove_work);
		schedule_work(&target->work);
	}
	spin_unlock_irq(target->scsi_host->host_lock);

	return ret;
}

static int srp_map_fmr(struct srp_target_port *target, struct scatterlist *scat,
		       int sg_cnt, struct srp_request *req,
		       struct srp_direct_buf *buf)
{
	u64 io_addr = 0;
	u64 *dma_pages;
	u32 len;
	int page_cnt;
	int i, j;
	int ret;
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_device *ibdev = dev->dev;
	struct scatterlist *sg;

	if (!dev->fmr_pool)
		return -ENODEV;

	if (srp_target_is_mellanox(target) &&
	    (ib_sg_dma_address(ibdev, &scat[0]) & ~dev->fmr_page_mask))
		return -EINVAL;

	len = page_cnt = 0;
	scsi_for_each_sg(req->scmnd, sg, sg_cnt, i) {
		unsigned int dma_len = ib_sg_dma_len(ibdev, sg);

		if (ib_sg_dma_address(ibdev, sg) & ~dev->fmr_page_mask) {
			if (i > 0)
				return -EINVAL;
			else
				++page_cnt;
		}
		if ((ib_sg_dma_address(ibdev, sg) + dma_len) &
		    ~dev->fmr_page_mask) {
			if (i < sg_cnt - 1)
				return -EINVAL;
			else
				++page_cnt;
		}

		len += dma_len;
	}

	page_cnt += len >> dev->fmr_page_shift;
	if (page_cnt > SRP_FMR_SIZE)
		return -ENOMEM;

	dma_pages = kmalloc(sizeof (u64) * page_cnt, GFP_ATOMIC);
	if (!dma_pages)
		return -ENOMEM;

	page_cnt = 0;
	scsi_for_each_sg(req->scmnd, sg, sg_cnt, i) {
		unsigned int dma_len = ib_sg_dma_len(ibdev, sg);

		for (j = 0; j < dma_len; j += dev->fmr_page_size)
			dma_pages[page_cnt++] =
				(ib_sg_dma_address(ibdev, sg) &
				 dev->fmr_page_mask) + j;
	}

	req->fmr = ib_fmr_pool_map_phys(dev->fmr_pool,
					dma_pages, page_cnt, io_addr);
	if (IS_ERR(req->fmr)) {
		ret = PTR_ERR(req->fmr);
		req->fmr = NULL;
		goto out;
	}

	buf->va  = cpu_to_be64(ib_sg_dma_address(ibdev, &scat[0]) &
			       ~dev->fmr_page_mask);
	buf->key = cpu_to_be32(req->fmr->fmr->rkey);
	buf->len = cpu_to_be32(len);

	ret = 0;

out:
	kfree(dma_pages);

	return ret;
}

static int srp_map_data(struct scsi_cmnd *scmnd, struct srp_target_port *target,
			struct srp_request *req)
{
	struct scatterlist *scat;
	struct srp_cmd *cmd = req->cmd->buf;
	int len, nents, count;
	u8 fmt = SRP_DATA_DESC_DIRECT;
	struct srp_device *dev;
	struct ib_device *ibdev;

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

	fmt = SRP_DATA_DESC_DIRECT;
	len = sizeof (struct srp_cmd) +	sizeof (struct srp_direct_buf);

	if (count == 1) {
		/*
		 * The midlayer only generated a single gather/scatter
		 * entry, or DMA mapping coalesced everything to a
		 * single entry.  So a direct descriptor along with
		 * the DMA MR suffices.
		 */
		struct srp_direct_buf *buf = (void *) cmd->add_data;

		buf->va  = cpu_to_be64(ib_sg_dma_address(ibdev, scat));
		buf->key = cpu_to_be32(dev->mr->rkey);
		buf->len = cpu_to_be32(ib_sg_dma_len(ibdev, scat));
	} else if (srp_map_fmr(target, scat, count, req,
			       (void *) cmd->add_data)) {
		/*
		 * FMR mapping failed, and the scatterlist has more
		 * than one entry.  Generate an indirect memory
		 * descriptor.
		 */
		struct srp_indirect_buf *buf = (void *) cmd->add_data;
		struct scatterlist *sg;
		u32 datalen = 0;
		int i;

		fmt = SRP_DATA_DESC_INDIRECT;
		len = sizeof (struct srp_cmd) +
			sizeof (struct srp_indirect_buf) +
			count * sizeof (struct srp_direct_buf);

		scsi_for_each_sg(scmnd, sg, count, i) {
			unsigned int dma_len = ib_sg_dma_len(ibdev, sg);

			buf->desc_list[i].va  =
				cpu_to_be64(ib_sg_dma_address(ibdev, sg));
			buf->desc_list[i].key =
				cpu_to_be32(dev->mr->rkey);
			buf->desc_list[i].len = cpu_to_be32(dma_len);
			datalen += dma_len;
		}

		if (scmnd->sc_data_direction == DMA_TO_DEVICE)
			cmd->data_out_desc_cnt = count;
		else
			cmd->data_in_desc_cnt = count;

		buf->table_desc.va  =
			cpu_to_be64(req->cmd->dma + sizeof *cmd + sizeof *buf);
		buf->table_desc.key =
			cpu_to_be32(target->srp_host->srp_dev->mr->rkey);
		buf->table_desc.len =
			cpu_to_be32(count * sizeof (struct srp_direct_buf));

		buf->len = cpu_to_be32(datalen);
	}

	if (scmnd->sc_data_direction == DMA_TO_DEVICE)
		cmd->buf_fmt = fmt << 4;
	else
		cmd->buf_fmt = fmt;

	return len;
}

static int srp_post_recv(struct srp_target_port *target)
{
	unsigned long flags;
	struct srp_iu *iu;
	struct ib_sge list;
	struct ib_recv_wr wr, *bad_wr;
	unsigned int next;
	int ret;

	spin_lock_irqsave(target->scsi_host->host_lock, flags);

	next	 = target->rx_head & (SRP_RQ_SIZE - 1);
	wr.wr_id = next;
	iu	 = target->rx_ring[next];

	list.addr   = iu->dma;
	list.length = iu->size;
	list.lkey   = target->srp_host->srp_dev->mr->lkey;

	wr.next     = NULL;
	wr.sg_list  = &list;
	wr.num_sge  = 1;

	ret = ib_post_recv(target->qp, &wr, &bad_wr);
	if (!ret)
		++target->rx_head;

	spin_unlock_irqrestore(target->scsi_host->host_lock, flags);

	return ret;
}

static void srp_process_rsp(struct srp_target_port *target, struct srp_rsp *rsp)
{
	struct srp_request *req;
	struct scsi_cmnd *scmnd;
	unsigned long flags;
	s32 delta;

	delta = (s32) be32_to_cpu(rsp->req_lim_delta);

	spin_lock_irqsave(target->scsi_host->host_lock, flags);

	target->req_lim += delta;

	req = &target->req_ring[rsp->tag & ~SRP_TAG_TSK_MGMT];

	if (unlikely(rsp->tag & SRP_TAG_TSK_MGMT)) {
		if (be32_to_cpu(rsp->resp_data_len) < 4)
			req->tsk_status = -1;
		else
			req->tsk_status = rsp->data[3];
		complete(&req->done);
	} else {
		scmnd = req->scmnd;
		if (!scmnd)
			shost_printk(KERN_ERR, target->scsi_host,
				     "Null scmnd for RSP w/tag %016llx\n",
				     (unsigned long long) rsp->tag);
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

		if (!req->tsk_mgmt) {
			scmnd->host_scribble = (void *) -1L;
			scmnd->scsi_done(scmnd);

			srp_remove_req(target, req);
		} else
			req->cmd_done = 1;
	}

	spin_unlock_irqrestore(target->scsi_host->host_lock, flags);
}

static void srp_handle_recv(struct srp_target_port *target, struct ib_wc *wc)
{
	struct ib_device *dev;
	struct srp_iu *iu;
	int res;
	u8 opcode;

	iu = target->rx_ring[wc->wr_id];

	dev = target->srp_host->srp_dev->dev;
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

	res = srp_post_recv(target);
	if (res != 0)
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "Recv failed with error code %d\n", res);
}

static void srp_recv_completion(struct ib_cq *cq, void *target_ptr)
{
	struct srp_target_port *target = target_ptr;
	struct ib_wc wc;

	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
	while (ib_poll_cq(cq, 1, &wc) > 0) {
		if (wc.status) {
			shost_printk(KERN_ERR, target->scsi_host,
				     PFX "failed receive status %d\n",
				     wc.status);
			target->qp_in_error = 1;
			break;
		}

		srp_handle_recv(target, &wc);
	}
}

static void srp_send_completion(struct ib_cq *cq, void *target_ptr)
{
	struct srp_target_port *target = target_ptr;
	struct ib_wc wc;

	while (ib_poll_cq(cq, 1, &wc) > 0) {
		if (wc.status) {
			shost_printk(KERN_ERR, target->scsi_host,
				     PFX "failed send status %d\n",
				     wc.status);
			target->qp_in_error = 1;
			break;
		}

		++target->tx_tail;
	}
}

/*
 * Must be called with target->scsi_host->host_lock held to protect
 * req_lim and tx_head.  Lock cannot be dropped between call here and
 * call to __srp_post_send().
 */
static struct srp_iu *__srp_get_tx_iu(struct srp_target_port *target,
					enum srp_request_type req_type)
{
	s32 min = (req_type == SRP_REQ_TASK_MGMT) ? 1 : 2;

	srp_send_completion(target->send_cq, target);

	if (target->tx_head - target->tx_tail >= SRP_SQ_SIZE)
		return NULL;

	if (target->req_lim < min) {
		++target->zero_req_lim;
		return NULL;
	}

	return target->tx_ring[target->tx_head & SRP_SQ_SIZE];
}

/*
 * Must be called with target->scsi_host->host_lock held to protect
 * req_lim and tx_head.
 */
static int __srp_post_send(struct srp_target_port *target,
			   struct srp_iu *iu, int len)
{
	struct ib_sge list;
	struct ib_send_wr wr, *bad_wr;
	int ret = 0;

	list.addr   = iu->dma;
	list.length = len;
	list.lkey   = target->srp_host->srp_dev->mr->lkey;

	wr.next       = NULL;
	wr.wr_id      = target->tx_head & SRP_SQ_SIZE;
	wr.sg_list    = &list;
	wr.num_sge    = 1;
	wr.opcode     = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED;

	ret = ib_post_send(target->qp, &wr, &bad_wr);

	if (!ret) {
		++target->tx_head;
		--target->req_lim;
	}

	return ret;
}

static int srp_queuecommand(struct scsi_cmnd *scmnd,
			    void (*done)(struct scsi_cmnd *))
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);
	struct srp_request *req;
	struct srp_iu *iu;
	struct srp_cmd *cmd;
	struct ib_device *dev;
	int len;

	if (target->state == SRP_TARGET_CONNECTING)
		goto err;

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED) {
		scmnd->result = DID_BAD_TARGET << 16;
		done(scmnd);
		return 0;
	}

	iu = __srp_get_tx_iu(target, SRP_REQ_NORMAL);
	if (!iu)
		goto err;

	dev = target->srp_host->srp_dev->dev;
	ib_dma_sync_single_for_cpu(dev, iu->dma, srp_max_iu_len,
				   DMA_TO_DEVICE);

	req = list_entry(target->free_reqs.next, struct srp_request, list);

	scmnd->scsi_done     = done;
	scmnd->result        = 0;
	scmnd->host_scribble = (void *) (long) req->index;

	cmd = iu->buf;
	memset(cmd, 0, sizeof *cmd);

	cmd->opcode = SRP_CMD;
	cmd->lun    = cpu_to_be64((u64) scmnd->device->lun << 48);
	cmd->tag    = req->index;
	memcpy(cmd->cdb, scmnd->cmnd, scmnd->cmd_len);

	req->scmnd    = scmnd;
	req->cmd      = iu;
	req->cmd_done = 0;
	req->tsk_mgmt = NULL;

	len = srp_map_data(scmnd, target, req);
	if (len < 0) {
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "Failed to map data\n");
		goto err;
	}

	ib_dma_sync_single_for_device(dev, iu->dma, srp_max_iu_len,
				      DMA_TO_DEVICE);

	if (__srp_post_send(target, iu, len)) {
		shost_printk(KERN_ERR, target->scsi_host, PFX "Send failed\n");
		goto err_unmap;
	}

	list_move_tail(&req->list, &target->req_queue);

	return 0;

err_unmap:
	srp_unmap_data(scmnd, target, req);

err:
	return SCSI_MLQUEUE_HOST_BUSY;
}

static int srp_alloc_iu_bufs(struct srp_target_port *target)
{
	int i;

	for (i = 0; i < SRP_RQ_SIZE; ++i) {
		target->rx_ring[i] = srp_alloc_iu(target->srp_host,
						  target->max_ti_iu_len,
						  GFP_KERNEL, DMA_FROM_DEVICE);
		if (!target->rx_ring[i])
			goto err;
	}

	for (i = 0; i < SRP_SQ_SIZE + 1; ++i) {
		target->tx_ring[i] = srp_alloc_iu(target->srp_host,
						  srp_max_iu_len,
						  GFP_KERNEL, DMA_TO_DEVICE);
		if (!target->tx_ring[i])
			goto err;
	}

	return 0;

err:
	for (i = 0; i < SRP_RQ_SIZE; ++i) {
		srp_free_iu(target->srp_host, target->rx_ring[i]);
		target->rx_ring[i] = NULL;
	}

	for (i = 0; i < SRP_SQ_SIZE + 1; ++i) {
		srp_free_iu(target->srp_host, target->tx_ring[i]);
		target->tx_ring[i] = NULL;
	}

	return -ENOMEM;
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
				shost_printk(KERN_WARNING, shost,
					    PFX "SRP LOGIN REJECTED, reason 0x%08x\n", reason);
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
	struct ib_qp_attr *qp_attr = NULL;
	int attr_mask = 0;
	int comp = 0;
	int opcode = 0;
	int i;

	switch (event->event) {
	case IB_CM_REQ_ERROR:
		shost_printk(KERN_DEBUG, target->scsi_host,
			     PFX "Sending CM REQ failed\n");
		comp = 1;
		target->status = -ECONNRESET;
		break;

	case IB_CM_REP_RECEIVED:
		comp = 1;
		opcode = *(u8 *) event->private_data;

		if (opcode == SRP_LOGIN_RSP) {
			struct srp_login_rsp *rsp = event->private_data;

			target->max_ti_iu_len = be32_to_cpu(rsp->max_ti_iu_len);
			target->req_lim       = be32_to_cpu(rsp->req_lim_delta);

			target->scsi_host->can_queue = min(target->req_lim,
							   target->scsi_host->can_queue);
		} else {
			shost_printk(KERN_WARNING, target->scsi_host,
				    PFX "Unhandled RSP opcode %#x\n", opcode);
			target->status = -ECONNRESET;
			break;
		}

		if (!target->rx_ring[0]) {
			target->status = srp_alloc_iu_bufs(target);
			if (target->status)
				break;
		}

		qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
		if (!qp_attr) {
			target->status = -ENOMEM;
			break;
		}

		qp_attr->qp_state = IB_QPS_RTR;
		target->status = ib_cm_init_qp_attr(cm_id, qp_attr, &attr_mask);
		if (target->status)
			break;

		target->status = ib_modify_qp(target->qp, qp_attr, attr_mask);
		if (target->status)
			break;

		for (i = 0; i < SRP_RQ_SIZE; i++) {
			target->status = srp_post_recv(target);
			if (target->status)
				break;
		}
		if (target->status)
			break;

		qp_attr->qp_state = IB_QPS_RTS;
		target->status = ib_cm_init_qp_attr(cm_id, qp_attr, &attr_mask);
		if (target->status)
			break;

		target->status = ib_modify_qp(target->qp, qp_attr, attr_mask);
		if (target->status)
			break;

		target->status = ib_send_cm_rtu(cm_id, NULL, 0);
		if (target->status)
			break;

		break;

	case IB_CM_REJ_RECEIVED:
		shost_printk(KERN_DEBUG, target->scsi_host, PFX "REJ received\n");
		comp = 1;

		srp_cm_rej_handler(cm_id, event, target);
		break;

	case IB_CM_DREQ_RECEIVED:
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "DREQ received - connection closed\n");
		if (ib_send_cm_drep(cm_id, NULL, 0))
			shost_printk(KERN_ERR, target->scsi_host,
				     PFX "Sending CM DREP failed\n");
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

	kfree(qp_attr);

	return 0;
}

static int srp_send_tsk_mgmt(struct srp_target_port *target,
			     struct srp_request *req, u8 func)
{
	struct srp_iu *iu;
	struct srp_tsk_mgmt *tsk_mgmt;

	spin_lock_irq(target->scsi_host->host_lock);

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED) {
		req->scmnd->result = DID_BAD_TARGET << 16;
		goto out;
	}

	init_completion(&req->done);

	iu = __srp_get_tx_iu(target, SRP_REQ_TASK_MGMT);
	if (!iu)
		goto out;

	tsk_mgmt = iu->buf;
	memset(tsk_mgmt, 0, sizeof *tsk_mgmt);

	tsk_mgmt->opcode 	= SRP_TSK_MGMT;
	tsk_mgmt->lun 		= cpu_to_be64((u64) req->scmnd->device->lun << 48);
	tsk_mgmt->tag 		= req->index | SRP_TAG_TSK_MGMT;
	tsk_mgmt->tsk_mgmt_func = func;
	tsk_mgmt->task_tag 	= req->index;

	if (__srp_post_send(target, iu, sizeof *tsk_mgmt))
		goto out;

	req->tsk_mgmt = iu;

	spin_unlock_irq(target->scsi_host->host_lock);

	if (!wait_for_completion_timeout(&req->done,
					 msecs_to_jiffies(SRP_ABORT_TIMEOUT_MS)))
		return -1;

	return 0;

out:
	spin_unlock_irq(target->scsi_host->host_lock);
	return -1;
}

static int srp_find_req(struct srp_target_port *target,
			struct scsi_cmnd *scmnd,
			struct srp_request **req)
{
	if (scmnd->host_scribble == (void *) -1L)
		return -1;

	*req = &target->req_ring[(long) scmnd->host_scribble];

	return 0;
}

static int srp_abort(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);
	struct srp_request *req;
	int ret = SUCCESS;

	shost_printk(KERN_ERR, target->scsi_host, "SRP abort called\n");

	if (target->qp_in_error)
		return FAILED;
	if (srp_find_req(target, scmnd, &req))
		return FAILED;
	if (srp_send_tsk_mgmt(target, req, SRP_TSK_ABORT_TASK))
		return FAILED;

	spin_lock_irq(target->scsi_host->host_lock);

	if (req->cmd_done) {
		srp_remove_req(target, req);
		scmnd->scsi_done(scmnd);
	} else if (!req->tsk_status) {
		srp_remove_req(target, req);
		scmnd->result = DID_ABORT << 16;
	} else
		ret = FAILED;

	spin_unlock_irq(target->scsi_host->host_lock);

	return ret;
}

static int srp_reset_device(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);
	struct srp_request *req, *tmp;

	shost_printk(KERN_ERR, target->scsi_host, "SRP reset_device called\n");

	if (target->qp_in_error)
		return FAILED;
	if (srp_find_req(target, scmnd, &req))
		return FAILED;
	if (srp_send_tsk_mgmt(target, req, SRP_TSK_LUN_RESET))
		return FAILED;
	if (req->tsk_status)
		return FAILED;

	spin_lock_irq(target->scsi_host->host_lock);

	list_for_each_entry_safe(req, tmp, &target->req_queue, list)
		if (req->scmnd->device == scmnd->device)
			srp_reset_req(target, req);

	spin_unlock_irq(target->scsi_host->host_lock);

	return SUCCESS;
}

static int srp_reset_host(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);
	int ret = FAILED;

	shost_printk(KERN_ERR, target->scsi_host, PFX "SRP reset_host called\n");

	if (!srp_reconnect_target(target))
		ret = SUCCESS;

	return ret;
}

static ssize_t show_id_ext(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED)
		return -ENODEV;

	return sprintf(buf, "0x%016llx\n",
		       (unsigned long long) be64_to_cpu(target->id_ext));
}

static ssize_t show_ioc_guid(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED)
		return -ENODEV;

	return sprintf(buf, "0x%016llx\n",
		       (unsigned long long) be64_to_cpu(target->ioc_guid));
}

static ssize_t show_service_id(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED)
		return -ENODEV;

	return sprintf(buf, "0x%016llx\n",
		       (unsigned long long) be64_to_cpu(target->service_id));
}

static ssize_t show_pkey(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED)
		return -ENODEV;

	return sprintf(buf, "0x%04x\n", be16_to_cpu(target->path.pkey));
}

static ssize_t show_dgid(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED)
		return -ENODEV;

	return sprintf(buf, "%pI6\n", target->path.dgid.raw);
}

static ssize_t show_orig_dgid(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED)
		return -ENODEV;

	return sprintf(buf, "%pI6\n", target->orig_dgid);
}

static ssize_t show_req_lim(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED)
		return -ENODEV;

	return sprintf(buf, "%d\n", target->req_lim);
}

static ssize_t show_zero_req_lim(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->state == SRP_TARGET_DEAD ||
	    target->state == SRP_TARGET_REMOVED)
		return -ENODEV;

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

static DEVICE_ATTR(id_ext,	    S_IRUGO, show_id_ext,	   NULL);
static DEVICE_ATTR(ioc_guid,	    S_IRUGO, show_ioc_guid,	   NULL);
static DEVICE_ATTR(service_id,	    S_IRUGO, show_service_id,	   NULL);
static DEVICE_ATTR(pkey,	    S_IRUGO, show_pkey,		   NULL);
static DEVICE_ATTR(dgid,	    S_IRUGO, show_dgid,		   NULL);
static DEVICE_ATTR(orig_dgid,	    S_IRUGO, show_orig_dgid,	   NULL);
static DEVICE_ATTR(req_lim,         S_IRUGO, show_req_lim,         NULL);
static DEVICE_ATTR(zero_req_lim,    S_IRUGO, show_zero_req_lim,	   NULL);
static DEVICE_ATTR(local_ib_port,   S_IRUGO, show_local_ib_port,   NULL);
static DEVICE_ATTR(local_ib_device, S_IRUGO, show_local_ib_device, NULL);

static struct device_attribute *srp_host_attrs[] = {
	&dev_attr_id_ext,
	&dev_attr_ioc_guid,
	&dev_attr_service_id,
	&dev_attr_pkey,
	&dev_attr_dgid,
	&dev_attr_orig_dgid,
	&dev_attr_req_lim,
	&dev_attr_zero_req_lim,
	&dev_attr_local_ib_port,
	&dev_attr_local_ib_device,
	NULL
};

static struct scsi_host_template srp_template = {
	.module				= THIS_MODULE,
	.name				= "InfiniBand SRP initiator",
	.proc_name			= DRV_NAME,
	.info				= srp_target_info,
	.queuecommand			= srp_queuecommand,
	.eh_abort_handler		= srp_abort,
	.eh_device_reset_handler	= srp_reset_device,
	.eh_host_reset_handler		= srp_reset_host,
	.can_queue			= SRP_SQ_SIZE,
	.this_id			= -1,
	.cmd_per_lun			= SRP_SQ_SIZE,
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
				printk(KERN_WARNING PFX "bad dest GID parameter '%s'\n", p);
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
				printk(KERN_WARNING PFX "bad P_Key parameter '%s'\n", p);
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
				printk(KERN_WARNING PFX "bad max sect parameter '%s'\n", p);
				goto out;
			}
			target->scsi_host->max_sectors = token;
			break;

		case SRP_OPT_MAX_CMD_PER_LUN:
			if (match_int(args, &token)) {
				printk(KERN_WARNING PFX "bad max cmd_per_lun parameter '%s'\n", p);
				goto out;
			}
			target->scsi_host->cmd_per_lun = min(token, SRP_SQ_SIZE);
			break;

		case SRP_OPT_IO_CLASS:
			if (match_hex(args, &token)) {
				printk(KERN_WARNING PFX "bad  IO class parameter '%s' \n", p);
				goto out;
			}
			if (token != SRP_REV10_IB_IO_CLASS &&
			    token != SRP_REV16A_IB_IO_CLASS) {
				printk(KERN_WARNING PFX "unknown IO class parameter value"
				       " %x specified (use %x or %x).\n",
				       token, SRP_REV10_IB_IO_CLASS, SRP_REV16A_IB_IO_CLASS);
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

		default:
			printk(KERN_WARNING PFX "unknown parameter or missing value "
			       "'%s' in target creation request\n", p);
			goto out;
		}
	}

	if ((opt_mask & SRP_OPT_ALL) == SRP_OPT_ALL)
		ret = 0;
	else
		for (i = 0; i < ARRAY_SIZE(srp_opt_tokens); ++i)
			if ((srp_opt_tokens[i].token & SRP_OPT_ALL) &&
			    !(srp_opt_tokens[i].token & opt_mask))
				printk(KERN_WARNING PFX "target creation request is "
				       "missing parameter '%s'\n",
				       srp_opt_tokens[i].pattern);

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
	int ret;
	int i;

	target_host = scsi_host_alloc(&srp_template,
				      sizeof (struct srp_target_port));
	if (!target_host)
		return -ENOMEM;

	target_host->transportt = ib_srp_transport_template;
	target_host->max_lun     = SRP_MAX_LUN;
	target_host->max_cmd_len = sizeof ((struct srp_cmd *) (void *) 0L)->cdb;

	target = host_to_target(target_host);

	target->io_class   = SRP_REV16A_IB_IO_CLASS;
	target->scsi_host  = target_host;
	target->srp_host   = host;

	INIT_LIST_HEAD(&target->free_reqs);
	INIT_LIST_HEAD(&target->req_queue);
	for (i = 0; i < SRP_SQ_SIZE; ++i) {
		target->req_ring[i].index = i;
		list_add_tail(&target->req_ring[i].list, &target->free_reqs);
	}

	ret = srp_parse_options(buf, target);
	if (ret)
		goto err;

	ib_query_gid(host->srp_dev->dev, host->port, 0, &target->path.sgid);

	shost_printk(KERN_DEBUG, target->scsi_host, PFX
		     "new target: id_ext %016llx ioc_guid %016llx pkey %04x "
		     "service_id %016llx dgid %pI6\n",
	       (unsigned long long) be64_to_cpu(target->id_ext),
	       (unsigned long long) be64_to_cpu(target->ioc_guid),
	       be16_to_cpu(target->path.pkey),
	       (unsigned long long) be64_to_cpu(target->service_id),
	       target->path.dgid.raw);

	ret = srp_create_target_ib(target);
	if (ret)
		goto err;

	ret = srp_new_cm_id(target);
	if (ret)
		goto err_free;

	target->qp_in_error = 0;
	ret = srp_connect_target(target);
	if (ret) {
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "Connection failed\n");
		goto err_cm_id;
	}

	ret = srp_add_target(host, target);
	if (ret)
		goto err_disconnect;

	return count;

err_disconnect:
	srp_disconnect_target(target);

err_cm_id:
	ib_destroy_cm_id(target->cm_id);

err_free:
	srp_free_target_ib(target);

err:
	scsi_host_put(target_host);

	return ret;
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
	struct ib_fmr_pool_param fmr_param;
	struct srp_host *host;
	int s, e, p;

	dev_attr = kmalloc(sizeof *dev_attr, GFP_KERNEL);
	if (!dev_attr)
		return;

	if (ib_query_device(device, dev_attr)) {
		printk(KERN_WARNING PFX "Query device failed for %s\n",
		       device->name);
		goto free_attr;
	}

	srp_dev = kmalloc(sizeof *srp_dev, GFP_KERNEL);
	if (!srp_dev)
		goto free_attr;

	/*
	 * Use the smallest page size supported by the HCA, down to a
	 * minimum of 512 bytes (which is the smallest sector that a
	 * SCSI command will ever carry).
	 */
	srp_dev->fmr_page_shift = max(9, ffs(dev_attr->page_size_cap) - 1);
	srp_dev->fmr_page_size  = 1 << srp_dev->fmr_page_shift;
	srp_dev->fmr_page_mask  = ~((u64) srp_dev->fmr_page_size - 1);

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

	memset(&fmr_param, 0, sizeof fmr_param);
	fmr_param.pool_size	    = SRP_FMR_POOL_SIZE;
	fmr_param.dirty_watermark   = SRP_FMR_DIRTY_SIZE;
	fmr_param.cache		    = 1;
	fmr_param.max_pages_per_fmr = SRP_FMR_SIZE;
	fmr_param.page_shift	    = srp_dev->fmr_page_shift;
	fmr_param.access	    = (IB_ACCESS_LOCAL_WRITE |
				       IB_ACCESS_REMOTE_WRITE |
				       IB_ACCESS_REMOTE_READ);

	srp_dev->fmr_pool = ib_create_fmr_pool(srp_dev->pd, &fmr_param);
	if (IS_ERR(srp_dev->fmr_pool))
		srp_dev->fmr_pool = NULL;

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
	LIST_HEAD(target_list);
	struct srp_target_port *target, *tmp_target;

	srp_dev = ib_get_client_data(device, &srp_client);

	list_for_each_entry_safe(host, tmp_host, &srp_dev->dev_list, list) {
		device_unregister(&host->dev);
		/*
		 * Wait for the sysfs entry to go away, so that no new
		 * target ports can be created.
		 */
		wait_for_completion(&host->released);

		/*
		 * Mark all target ports as removed, so we stop queueing
		 * commands and don't try to reconnect.
		 */
		spin_lock(&host->target_lock);
		list_for_each_entry(target, &host->target_list, list) {
			spin_lock_irq(target->scsi_host->host_lock);
			target->state = SRP_TARGET_REMOVED;
			spin_unlock_irq(target->scsi_host->host_lock);
		}
		spin_unlock(&host->target_lock);

		/*
		 * Wait for any reconnection tasks that may have
		 * started before we marked our target ports as
		 * removed, and any target port removal tasks.
		 */
		flush_scheduled_work();

		list_for_each_entry_safe(target, tmp_target,
					 &host->target_list, list) {
			srp_remove_host(target->scsi_host);
			scsi_remove_host(target->scsi_host);
			srp_disconnect_target(target);
			ib_destroy_cm_id(target->cm_id);
			srp_free_target_ib(target);
			scsi_host_put(target->scsi_host);
		}

		kfree(host);
	}

	if (srp_dev->fmr_pool)
		ib_destroy_fmr_pool(srp_dev->fmr_pool);
	ib_dereg_mr(srp_dev->mr);
	ib_dealloc_pd(srp_dev->pd);

	kfree(srp_dev);
}

static struct srp_function_template ib_srp_transport_functions = {
};

static int __init srp_init_module(void)
{
	int ret;

	if (srp_sg_tablesize > 255) {
		printk(KERN_WARNING PFX "Clamping srp_sg_tablesize to 255\n");
		srp_sg_tablesize = 255;
	}

	ib_srp_transport_template =
		srp_attach_transport(&ib_srp_transport_functions);
	if (!ib_srp_transport_template)
		return -ENOMEM;

	srp_template.sg_tablesize = srp_sg_tablesize;
	srp_max_iu_len = (sizeof (struct srp_cmd) +
			  sizeof (struct srp_indirect_buf) +
			  srp_sg_tablesize * 16);

	ret = class_register(&srp_class);
	if (ret) {
		printk(KERN_ERR PFX "couldn't register class infiniband_srp\n");
		srp_release_transport(ib_srp_transport_template);
		return ret;
	}

	ib_sa_register_client(&srp_sa_client);

	ret = ib_register_client(&srp_client);
	if (ret) {
		printk(KERN_ERR PFX "couldn't register IB client\n");
		srp_release_transport(ib_srp_transport_template);
		ib_sa_unregister_client(&srp_sa_client);
		class_unregister(&srp_class);
		return ret;
	}

	return 0;
}

static void __exit srp_cleanup_module(void)
{
	ib_unregister_client(&srp_client);
	ib_sa_unregister_client(&srp_sa_client);
	class_unregister(&srp_class);
	srp_release_transport(ib_srp_transport_template);
}

module_init(srp_init_module);
module_exit(srp_cleanup_module);
