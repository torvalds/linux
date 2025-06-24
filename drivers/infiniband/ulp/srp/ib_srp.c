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
#include <linux/lockdep.h>
#include <linux/inet.h>
#include <rdma/ib_cache.h>

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

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("InfiniBand SCSI RDMA Protocol initiator");
MODULE_LICENSE("Dual BSD/GPL");

static unsigned int srp_sg_tablesize;
static unsigned int cmd_sg_entries;
static unsigned int indirect_sg_entries;
static bool allow_ext_sg;
static bool register_always = true;
static bool never_register;
static int topspin_workarounds = 1;

module_param(srp_sg_tablesize, uint, 0444);
MODULE_PARM_DESC(srp_sg_tablesize, "Deprecated name for cmd_sg_entries");

module_param(cmd_sg_entries, uint, 0444);
MODULE_PARM_DESC(cmd_sg_entries,
		 "Default number of gather/scatter entries in the SRP command (default is 12, max 255)");

module_param(indirect_sg_entries, uint, 0444);
MODULE_PARM_DESC(indirect_sg_entries,
		 "Default max number of gather/scatter entries (default is 12, max is " __stringify(SG_MAX_SEGMENTS) ")");

module_param(allow_ext_sg, bool, 0444);
MODULE_PARM_DESC(allow_ext_sg,
		  "Default behavior when there are more than cmd_sg_entries S/G entries after mapping; fails the request when false (default false)");

module_param(topspin_workarounds, int, 0444);
MODULE_PARM_DESC(topspin_workarounds,
		 "Enable workarounds for Topspin/Cisco SRP target bugs if != 0");

module_param(register_always, bool, 0444);
MODULE_PARM_DESC(register_always,
		 "Use memory registration even for contiguous memory regions");

module_param(never_register, bool, 0444);
MODULE_PARM_DESC(never_register, "Never register memory");

static const struct kernel_param_ops srp_tmo_ops;

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

static bool srp_use_imm_data = true;
module_param_named(use_imm_data, srp_use_imm_data, bool, 0644);
MODULE_PARM_DESC(use_imm_data,
		 "Whether or not to request permission to use immediate data during SRP login.");

static unsigned int srp_max_imm_data = 8 * 1024;
module_param_named(max_imm_data, srp_max_imm_data, uint, 0644);
MODULE_PARM_DESC(max_imm_data, "Maximum immediate data size.");

static unsigned ch_count;
module_param(ch_count, uint, 0444);
MODULE_PARM_DESC(ch_count,
		 "Number of RDMA channels to use for communication with an SRP target. Using more than one channel improves performance if the HCA supports multiple completion vectors. The default value is the minimum of four times the number of online CPU sockets and the number of completion vectors supported by the HCA.");

static int srp_add_one(struct ib_device *device);
static void srp_remove_one(struct ib_device *device, void *client_data);
static void srp_rename_dev(struct ib_device *device, void *client_data);
static void srp_recv_done(struct ib_cq *cq, struct ib_wc *wc);
static void srp_handle_qp_err(struct ib_cq *cq, struct ib_wc *wc,
		const char *opname);
static int srp_ib_cm_handler(struct ib_cm_id *cm_id,
			     const struct ib_cm_event *event);
static int srp_rdma_cm_handler(struct rdma_cm_id *cm_id,
			       struct rdma_cm_event *event);

static struct scsi_transport_template *ib_srp_transport_template;
static struct workqueue_struct *srp_remove_wq;

static struct ib_client srp_client = {
	.name   = "srp",
	.add    = srp_add_one,
	.remove = srp_remove_one,
	.rename = srp_rename_dev
};

static struct ib_sa_client srp_sa_client;

static int srp_tmo_get(char *buffer, const struct kernel_param *kp)
{
	int tmo = *(int *)kp->arg;

	if (tmo >= 0)
		return sysfs_emit(buffer, "%d\n", tmo);
	else
		return sysfs_emit(buffer, "off\n");
}

static int srp_tmo_set(const char *val, const struct kernel_param *kp)
{
	int tmo, res;

	res = srp_parse_tmo(&tmo, val);
	if (res)
		goto out;

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

static const struct kernel_param_ops srp_tmo_ops = {
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
	pr_debug("QP event %s (%d)\n",
		 ib_event_msg(event->event), event->event);
}

static int srp_init_ib_qp(struct srp_target_port *target,
			  struct ib_qp *qp)
{
	struct ib_qp_attr *attr;
	int ret;

	attr = kmalloc(sizeof *attr, GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	ret = ib_find_cached_pkey(target->srp_host->srp_dev->dev,
				  target->srp_host->port,
				  be16_to_cpu(target->ib_cm.pkey),
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

static int srp_new_ib_cm_id(struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;
	struct ib_cm_id *new_cm_id;

	new_cm_id = ib_create_cm_id(target->srp_host->srp_dev->dev,
				    srp_ib_cm_handler, ch);
	if (IS_ERR(new_cm_id))
		return PTR_ERR(new_cm_id);

	if (ch->ib_cm.cm_id)
		ib_destroy_cm_id(ch->ib_cm.cm_id);
	ch->ib_cm.cm_id = new_cm_id;
	if (rdma_cap_opa_ah(target->srp_host->srp_dev->dev,
			    target->srp_host->port))
		ch->ib_cm.path.rec_type = SA_PATH_REC_TYPE_OPA;
	else
		ch->ib_cm.path.rec_type = SA_PATH_REC_TYPE_IB;
	ch->ib_cm.path.sgid = target->sgid;
	ch->ib_cm.path.dgid = target->ib_cm.orig_dgid;
	ch->ib_cm.path.pkey = target->ib_cm.pkey;
	ch->ib_cm.path.service_id = target->ib_cm.service_id;

	return 0;
}

static int srp_new_rdma_cm_id(struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;
	struct rdma_cm_id *new_cm_id;
	int ret;

	new_cm_id = rdma_create_id(target->net, srp_rdma_cm_handler, ch,
				   RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(new_cm_id)) {
		ret = PTR_ERR(new_cm_id);
		new_cm_id = NULL;
		goto out;
	}

	init_completion(&ch->done);
	ret = rdma_resolve_addr(new_cm_id, target->rdma_cm.src_specified ?
				&target->rdma_cm.src.sa : NULL,
				&target->rdma_cm.dst.sa,
				SRP_PATH_REC_TIMEOUT_MS);
	if (ret) {
		pr_err("No route available from %pISpsc to %pISpsc (%d)\n",
		       &target->rdma_cm.src, &target->rdma_cm.dst, ret);
		goto out;
	}
	ret = wait_for_completion_interruptible(&ch->done);
	if (ret < 0)
		goto out;

	ret = ch->status;
	if (ret) {
		pr_err("Resolving address %pISpsc failed (%d)\n",
		       &target->rdma_cm.dst, ret);
		goto out;
	}

	swap(ch->rdma_cm.cm_id, new_cm_id);

out:
	if (new_cm_id)
		rdma_destroy_id(new_cm_id);

	return ret;
}

static int srp_new_cm_id(struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;

	return target->using_rdma_cm ? srp_new_rdma_cm_id(ch) :
		srp_new_ib_cm_id(ch);
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
	int i, ret = -EINVAL;
	enum ib_mr_type mr_type;

	if (pool_size <= 0)
		goto err;
	ret = -ENOMEM;
	pool = kzalloc(struct_size(pool, desc, pool_size), GFP_KERNEL);
	if (!pool)
		goto err;
	pool->size = pool_size;
	pool->max_page_list_len = max_page_list_len;
	spin_lock_init(&pool->lock);
	INIT_LIST_HEAD(&pool->free_list);

	if (device->attrs.kernel_cap_flags & IBK_SG_GAPS_REG)
		mr_type = IB_MR_TYPE_SG_GAPS;
	else
		mr_type = IB_MR_TYPE_MEM_REG;

	for (i = 0, d = &pool->desc[0]; i < pool->size; i++, d++) {
		mr = ib_alloc_mr(pd, mr_type, max_page_list_len);
		if (IS_ERR(mr)) {
			ret = PTR_ERR(mr);
			if (ret == -ENOMEM)
				pr_info("%s: ib_alloc_mr() failed. Try to reduce max_cmd_per_lun, max_sect or ch_count\n",
					dev_name(&device->dev));
			goto destroy_pool;
		}
		d->mr = mr;
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

	return srp_create_fr_pool(dev->dev, dev->pd, target->mr_pool_size,
				  dev->max_pages_per_mr);
}

/**
 * srp_destroy_qp() - destroy an RDMA queue pair
 * @ch: SRP RDMA channel.
 *
 * Drain the qp before destroying it.  This avoids that the receive
 * completion handler can access the queue pair while it is
 * being destroyed.
 */
static void srp_destroy_qp(struct srp_rdma_ch *ch)
{
	spin_lock_irq(&ch->lock);
	ib_process_cq_direct(ch->send_cq, -1);
	spin_unlock_irq(&ch->lock);

	ib_drain_qp(ch->qp);
	ib_destroy_qp(ch->qp);
}

static int srp_create_ch_ib(struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;
	struct srp_device *dev = target->srp_host->srp_dev;
	const struct ib_device_attr *attr = &dev->dev->attrs;
	struct ib_qp_init_attr *init_attr;
	struct ib_cq *recv_cq, *send_cq;
	struct ib_qp *qp;
	struct srp_fr_pool *fr_pool = NULL;
	const int m = 1 + dev->use_fast_reg * target->mr_per_cmd * 2;
	int ret;

	init_attr = kzalloc(sizeof *init_attr, GFP_KERNEL);
	if (!init_attr)
		return -ENOMEM;

	/* queue_size + 1 for ib_drain_rq() */
	recv_cq = ib_alloc_cq(dev->dev, ch, target->queue_size + 1,
				ch->comp_vector, IB_POLL_SOFTIRQ);
	if (IS_ERR(recv_cq)) {
		ret = PTR_ERR(recv_cq);
		goto err;
	}

	send_cq = ib_alloc_cq(dev->dev, ch, m * target->queue_size,
				ch->comp_vector, IB_POLL_DIRECT);
	if (IS_ERR(send_cq)) {
		ret = PTR_ERR(send_cq);
		goto err_recv_cq;
	}

	init_attr->event_handler       = srp_qp_event;
	init_attr->cap.max_send_wr     = m * target->queue_size;
	init_attr->cap.max_recv_wr     = target->queue_size + 1;
	init_attr->cap.max_recv_sge    = 1;
	init_attr->cap.max_send_sge    = min(SRP_MAX_SGE, attr->max_send_sge);
	init_attr->sq_sig_type         = IB_SIGNAL_REQ_WR;
	init_attr->qp_type             = IB_QPT_RC;
	init_attr->send_cq             = send_cq;
	init_attr->recv_cq             = recv_cq;

	ch->max_imm_sge = min(init_attr->cap.max_send_sge - 1U, 255U);

	if (target->using_rdma_cm) {
		ret = rdma_create_qp(ch->rdma_cm.cm_id, dev->pd, init_attr);
		qp = ch->rdma_cm.cm_id->qp;
	} else {
		qp = ib_create_qp(dev->pd, init_attr);
		if (!IS_ERR(qp)) {
			ret = srp_init_ib_qp(target, qp);
			if (ret)
				ib_destroy_qp(qp);
		} else {
			ret = PTR_ERR(qp);
		}
	}
	if (ret) {
		pr_err("QP creation failed for dev %s: %d\n",
		       dev_name(&dev->dev->dev), ret);
		goto err_send_cq;
	}

	if (dev->use_fast_reg) {
		fr_pool = srp_alloc_fr_pool(target);
		if (IS_ERR(fr_pool)) {
			ret = PTR_ERR(fr_pool);
			shost_printk(KERN_WARNING, target->scsi_host, PFX
				     "FR pool allocation failed (%d)\n", ret);
			goto err_qp;
		}
	}

	if (ch->qp)
		srp_destroy_qp(ch);
	if (ch->recv_cq)
		ib_free_cq(ch->recv_cq);
	if (ch->send_cq)
		ib_free_cq(ch->send_cq);

	ch->qp = qp;
	ch->recv_cq = recv_cq;
	ch->send_cq = send_cq;

	if (dev->use_fast_reg) {
		if (ch->fr_pool)
			srp_destroy_fr_pool(ch->fr_pool);
		ch->fr_pool = fr_pool;
	}

	kfree(init_attr);
	return 0;

err_qp:
	if (target->using_rdma_cm)
		rdma_destroy_qp(ch->rdma_cm.cm_id);
	else
		ib_destroy_qp(qp);

err_send_cq:
	ib_free_cq(send_cq);

err_recv_cq:
	ib_free_cq(recv_cq);

err:
	kfree(init_attr);
	return ret;
}

/*
 * Note: this function may be called without srp_alloc_iu_bufs() having been
 * invoked. Hence the ch->[rt]x_ring checks.
 */
static void srp_free_ch_ib(struct srp_target_port *target,
			   struct srp_rdma_ch *ch)
{
	struct srp_device *dev = target->srp_host->srp_dev;
	int i;

	if (!ch->target)
		return;

	if (target->using_rdma_cm) {
		if (ch->rdma_cm.cm_id) {
			rdma_destroy_id(ch->rdma_cm.cm_id);
			ch->rdma_cm.cm_id = NULL;
		}
	} else {
		if (ch->ib_cm.cm_id) {
			ib_destroy_cm_id(ch->ib_cm.cm_id);
			ch->ib_cm.cm_id = NULL;
		}
	}

	/* If srp_new_cm_id() succeeded but srp_create_ch_ib() not, return. */
	if (!ch->qp)
		return;

	if (dev->use_fast_reg) {
		if (ch->fr_pool)
			srp_destroy_fr_pool(ch->fr_pool);
	}

	srp_destroy_qp(ch);
	ib_free_cq(ch->send_cq);
	ib_free_cq(ch->recv_cq);

	/*
	 * Avoid that the SCSI error handler tries to use this channel after
	 * it has been freed. The SCSI error handler can namely continue
	 * trying to perform recovery actions after scsi_remove_host()
	 * returned.
	 */
	ch->target = NULL;

	ch->qp = NULL;
	ch->send_cq = ch->recv_cq = NULL;

	if (ch->rx_ring) {
		for (i = 0; i < target->queue_size; ++i)
			srp_free_iu(target->srp_host, ch->rx_ring[i]);
		kfree(ch->rx_ring);
		ch->rx_ring = NULL;
	}
	if (ch->tx_ring) {
		for (i = 0; i < target->queue_size; ++i)
			srp_free_iu(target->srp_host, ch->tx_ring[i]);
		kfree(ch->tx_ring);
		ch->tx_ring = NULL;
	}
}

static void srp_path_rec_completion(int status,
				    struct sa_path_rec *pathrec,
				    unsigned int num_paths, void *ch_ptr)
{
	struct srp_rdma_ch *ch = ch_ptr;
	struct srp_target_port *target = ch->target;

	ch->status = status;
	if (status)
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "Got failed path rec status %d\n", status);
	else
		ch->ib_cm.path = *pathrec;
	complete(&ch->done);
}

static int srp_ib_lookup_path(struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;
	int ret;

	ch->ib_cm.path.numb_path = 1;

	init_completion(&ch->done);

	ch->ib_cm.path_query_id = ib_sa_path_rec_get(&srp_sa_client,
					       target->srp_host->srp_dev->dev,
					       target->srp_host->port,
					       &ch->ib_cm.path,
					       IB_SA_PATH_REC_SERVICE_ID |
					       IB_SA_PATH_REC_DGID	 |
					       IB_SA_PATH_REC_SGID	 |
					       IB_SA_PATH_REC_NUMB_PATH	 |
					       IB_SA_PATH_REC_PKEY,
					       SRP_PATH_REC_TIMEOUT_MS,
					       GFP_KERNEL,
					       srp_path_rec_completion,
					       ch, &ch->ib_cm.path_query);
	if (ch->ib_cm.path_query_id < 0)
		return ch->ib_cm.path_query_id;

	ret = wait_for_completion_interruptible(&ch->done);
	if (ret < 0)
		return ret;

	if (ch->status < 0)
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Path record query failed: sgid %pI6, dgid %pI6, pkey %#04x, service_id %#16llx\n",
			     ch->ib_cm.path.sgid.raw, ch->ib_cm.path.dgid.raw,
			     be16_to_cpu(target->ib_cm.pkey),
			     be64_to_cpu(target->ib_cm.service_id));

	return ch->status;
}

static int srp_rdma_lookup_path(struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;
	int ret;

	init_completion(&ch->done);

	ret = rdma_resolve_route(ch->rdma_cm.cm_id, SRP_PATH_REC_TIMEOUT_MS);
	if (ret)
		return ret;

	wait_for_completion_interruptible(&ch->done);

	if (ch->status != 0)
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Path resolution failed\n");

	return ch->status;
}

static int srp_lookup_path(struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;

	return target->using_rdma_cm ? srp_rdma_lookup_path(ch) :
		srp_ib_lookup_path(ch);
}

static u8 srp_get_subnet_timeout(struct srp_host *host)
{
	struct ib_port_attr attr;
	int ret;
	u8 subnet_timeout = 18;

	ret = ib_query_port(host->srp_dev->dev, host->port, &attr);
	if (ret == 0)
		subnet_timeout = attr.subnet_timeout;

	if (unlikely(subnet_timeout < 15))
		pr_warn("%s: subnet timeout %d may cause SRP login to fail.\n",
			dev_name(&host->srp_dev->dev->dev), subnet_timeout);

	return subnet_timeout;
}

static int srp_send_req(struct srp_rdma_ch *ch, uint32_t max_iu_len,
			bool multich)
{
	struct srp_target_port *target = ch->target;
	struct {
		struct rdma_conn_param	  rdma_param;
		struct srp_login_req_rdma rdma_req;
		struct ib_cm_req_param	  ib_param;
		struct srp_login_req	  ib_req;
	} *req = NULL;
	char *ipi, *tpi;
	int status;

	req = kzalloc(sizeof *req, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->ib_param.flow_control = 1;
	req->ib_param.retry_count = target->tl_retry_count;

	/*
	 * Pick some arbitrary defaults here; we could make these
	 * module parameters if anyone cared about setting them.
	 */
	req->ib_param.responder_resources = 4;
	req->ib_param.rnr_retry_count = 7;
	req->ib_param.max_cm_retries = 15;

	req->ib_req.opcode = SRP_LOGIN_REQ;
	req->ib_req.tag = 0;
	req->ib_req.req_it_iu_len = cpu_to_be32(max_iu_len);
	req->ib_req.req_buf_fmt	= cpu_to_be16(SRP_BUF_FORMAT_DIRECT |
					      SRP_BUF_FORMAT_INDIRECT);
	req->ib_req.req_flags = (multich ? SRP_MULTICHAN_MULTI :
				 SRP_MULTICHAN_SINGLE);
	if (srp_use_imm_data) {
		req->ib_req.req_flags |= SRP_IMMED_REQUESTED;
		req->ib_req.imm_data_offset = cpu_to_be16(SRP_IMM_DATA_OFFSET);
	}

	if (target->using_rdma_cm) {
		req->rdma_param.flow_control = req->ib_param.flow_control;
		req->rdma_param.responder_resources =
			req->ib_param.responder_resources;
		req->rdma_param.initiator_depth = req->ib_param.initiator_depth;
		req->rdma_param.retry_count = req->ib_param.retry_count;
		req->rdma_param.rnr_retry_count = req->ib_param.rnr_retry_count;
		req->rdma_param.private_data = &req->rdma_req;
		req->rdma_param.private_data_len = sizeof(req->rdma_req);

		req->rdma_req.opcode = req->ib_req.opcode;
		req->rdma_req.tag = req->ib_req.tag;
		req->rdma_req.req_it_iu_len = req->ib_req.req_it_iu_len;
		req->rdma_req.req_buf_fmt = req->ib_req.req_buf_fmt;
		req->rdma_req.req_flags	= req->ib_req.req_flags;
		req->rdma_req.imm_data_offset = req->ib_req.imm_data_offset;

		ipi = req->rdma_req.initiator_port_id;
		tpi = req->rdma_req.target_port_id;
	} else {
		u8 subnet_timeout;

		subnet_timeout = srp_get_subnet_timeout(target->srp_host);

		req->ib_param.primary_path = &ch->ib_cm.path;
		req->ib_param.alternate_path = NULL;
		req->ib_param.service_id = target->ib_cm.service_id;
		get_random_bytes(&req->ib_param.starting_psn, 4);
		req->ib_param.starting_psn &= 0xffffff;
		req->ib_param.qp_num = ch->qp->qp_num;
		req->ib_param.qp_type = ch->qp->qp_type;
		req->ib_param.local_cm_response_timeout = subnet_timeout + 2;
		req->ib_param.remote_cm_response_timeout = subnet_timeout + 2;
		req->ib_param.private_data = &req->ib_req;
		req->ib_param.private_data_len = sizeof(req->ib_req);

		ipi = req->ib_req.initiator_port_id;
		tpi = req->ib_req.target_port_id;
	}

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
		memcpy(ipi,     &target->sgid.global.interface_id, 8);
		memcpy(ipi + 8, &target->initiator_ext, 8);
		memcpy(tpi,     &target->ioc_guid, 8);
		memcpy(tpi + 8, &target->id_ext, 8);
	} else {
		memcpy(ipi,     &target->initiator_ext, 8);
		memcpy(ipi + 8, &target->sgid.global.interface_id, 8);
		memcpy(tpi,     &target->id_ext, 8);
		memcpy(tpi + 8, &target->ioc_guid, 8);
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
			     be64_to_cpu(target->ioc_guid));
		memset(ipi, 0, 8);
		memcpy(ipi + 8, &target->srp_host->srp_dev->dev->node_guid, 8);
	}

	if (target->using_rdma_cm)
		status = rdma_connect(ch->rdma_cm.cm_id, &req->rdma_param);
	else
		status = ib_send_cm_req(ch->ib_cm.cm_id, &req->ib_param);

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

static void srp_disconnect_target(struct srp_target_port *target)
{
	struct srp_rdma_ch *ch;
	int i, ret;

	/* XXX should send SRP_I_LOGOUT request */

	for (i = 0; i < target->ch_count; i++) {
		ch = &target->ch[i];
		ch->connected = false;
		ret = 0;
		if (target->using_rdma_cm) {
			if (ch->rdma_cm.cm_id)
				rdma_disconnect(ch->rdma_cm.cm_id);
		} else {
			if (ch->ib_cm.cm_id)
				ret = ib_send_cm_dreq(ch->ib_cm.cm_id,
						      NULL, 0);
		}
		if (ret < 0) {
			shost_printk(KERN_DEBUG, target->scsi_host,
				     PFX "Sending CM DREQ failed\n");
		}
	}
}

static int srp_exit_cmd_priv(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	struct srp_target_port *target = host_to_target(shost);
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_device *ibdev = dev->dev;
	struct srp_request *req = scsi_cmd_priv(cmd);

	kfree(req->fr_list);
	if (req->indirect_dma_addr) {
		ib_dma_unmap_single(ibdev, req->indirect_dma_addr,
				    target->indirect_size,
				    DMA_TO_DEVICE);
	}
	kfree(req->indirect_desc);

	return 0;
}

static int srp_init_cmd_priv(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	struct srp_target_port *target = host_to_target(shost);
	struct srp_device *srp_dev = target->srp_host->srp_dev;
	struct ib_device *ibdev = srp_dev->dev;
	struct srp_request *req = scsi_cmd_priv(cmd);
	dma_addr_t dma_addr;
	int ret = -ENOMEM;

	if (srp_dev->use_fast_reg) {
		req->fr_list = kmalloc_array(target->mr_per_cmd, sizeof(void *),
					GFP_KERNEL);
		if (!req->fr_list)
			goto out;
	}
	req->indirect_desc = kmalloc(target->indirect_size, GFP_KERNEL);
	if (!req->indirect_desc)
		goto out;

	dma_addr = ib_dma_map_single(ibdev, req->indirect_desc,
				     target->indirect_size,
				     DMA_TO_DEVICE);
	if (ib_dma_mapping_error(ibdev, dma_addr)) {
		srp_exit_cmd_priv(shost, cmd);
		goto out;
	}

	req->indirect_dma_addr = dma_addr;
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
	const struct attribute_group **g;
	struct attribute **attr;

	for (g = shost->hostt->shost_groups; *g; ++g) {
		for (attr = (*g)->attrs; *attr; ++attr) {
			struct device_attribute *dev_attr =
				container_of(*attr, typeof(*dev_attr), attr);

			device_remove_file(&shost->shost_dev, dev_attr);
		}
	}
}

static void srp_remove_target(struct srp_target_port *target)
{
	struct srp_rdma_ch *ch;
	int i;

	WARN_ON_ONCE(target->state != SRP_TARGET_REMOVED);

	srp_del_scsi_host_attr(target->scsi_host);
	srp_rport_get(target->rport);
	srp_remove_host(target->scsi_host);
	scsi_remove_host(target->scsi_host);
	srp_stop_rport_timers(target->rport);
	srp_disconnect_target(target);
	kobj_ns_drop(KOBJ_NS_TYPE_NET, target->net);
	for (i = 0; i < target->ch_count; i++) {
		ch = &target->ch[i];
		srp_free_ch_ib(target, ch);
	}
	cancel_work_sync(&target->tl_err_work);
	srp_rport_put(target->rport);
	kfree(target->ch);
	target->ch = NULL;

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

/**
 * srp_connected_ch() - number of connected channels
 * @target: SRP target port.
 */
static int srp_connected_ch(struct srp_target_port *target)
{
	int i, c = 0;

	for (i = 0; i < target->ch_count; i++)
		c += target->ch[i].connected;

	return c;
}

static int srp_connect_ch(struct srp_rdma_ch *ch, uint32_t max_iu_len,
			  bool multich)
{
	struct srp_target_port *target = ch->target;
	int ret;

	WARN_ON_ONCE(!multich && srp_connected_ch(target) > 0);

	ret = srp_lookup_path(ch);
	if (ret)
		goto out;

	while (1) {
		init_completion(&ch->done);
		ret = srp_send_req(ch, max_iu_len, multich);
		if (ret)
			goto out;
		ret = wait_for_completion_interruptible(&ch->done);
		if (ret < 0)
			goto out;

		/*
		 * The CM event handling code will set status to
		 * SRP_PORT_REDIRECT if we get a port redirect REJ
		 * back, or SRP_DLID_REDIRECT if we get a lid/qp
		 * redirect REJ back.
		 */
		ret = ch->status;
		switch (ret) {
		case 0:
			ch->connected = true;
			goto out;

		case SRP_PORT_REDIRECT:
			ret = srp_lookup_path(ch);
			if (ret)
				goto out;
			break;

		case SRP_DLID_REDIRECT:
			break;

		case SRP_STALE_CONN:
			shost_printk(KERN_ERR, target->scsi_host, PFX
				     "giving up on stale connection\n");
			ret = -ECONNRESET;
			goto out;

		default:
			goto out;
		}
	}

out:
	return ret <= 0 ? ret : -ENODEV;
}

static void srp_inv_rkey_err_done(struct ib_cq *cq, struct ib_wc *wc)
{
	srp_handle_qp_err(cq, wc, "INV RKEY");
}

static int srp_inv_rkey(struct srp_request *req, struct srp_rdma_ch *ch,
		u32 rkey)
{
	struct ib_send_wr wr = {
		.opcode		    = IB_WR_LOCAL_INV,
		.next		    = NULL,
		.num_sge	    = 0,
		.send_flags	    = 0,
		.ex.invalidate_rkey = rkey,
	};

	wr.wr_cqe = &req->reg_cqe;
	req->reg_cqe.done = srp_inv_rkey_err_done;
	return ib_post_send(ch->qp, &wr, NULL);
}

static void srp_unmap_data(struct scsi_cmnd *scmnd,
			   struct srp_rdma_ch *ch,
			   struct srp_request *req)
{
	struct srp_target_port *target = ch->target;
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
			res = srp_inv_rkey(req, ch, (*pfr)->mr->rkey);
			if (res < 0) {
				shost_printk(KERN_ERR, target->scsi_host, PFX
				  "Queueing INV WR for rkey %#x failed (%d)\n",
				  (*pfr)->mr->rkey, res);
				queue_work(system_long_wq,
					   &target->tl_err_work);
			}
		}
		if (req->nmdesc)
			srp_fr_pool_put(ch->fr_pool, req->fr_list,
					req->nmdesc);
	}

	ib_dma_unmap_sg(ibdev, scsi_sglist(scmnd), scsi_sg_count(scmnd),
			scmnd->sc_data_direction);
}

/**
 * srp_claim_req - Take ownership of the scmnd associated with a request.
 * @ch: SRP RDMA channel.
 * @req: SRP request.
 * @sdev: If not NULL, only take ownership for this SCSI device.
 * @scmnd: If NULL, take ownership of @req->scmnd. If not NULL, only take
 *         ownership of @req->scmnd if it equals @scmnd.
 *
 * Return value:
 * Either NULL or a pointer to the SCSI command the caller became owner of.
 */
static struct scsi_cmnd *srp_claim_req(struct srp_rdma_ch *ch,
				       struct srp_request *req,
				       struct scsi_device *sdev,
				       struct scsi_cmnd *scmnd)
{
	unsigned long flags;

	spin_lock_irqsave(&ch->lock, flags);
	if (req->scmnd &&
	    (!sdev || req->scmnd->device == sdev) &&
	    (!scmnd || req->scmnd == scmnd)) {
		scmnd = req->scmnd;
		req->scmnd = NULL;
	} else {
		scmnd = NULL;
	}
	spin_unlock_irqrestore(&ch->lock, flags);

	return scmnd;
}

/**
 * srp_free_req() - Unmap data and adjust ch->req_lim.
 * @ch:     SRP RDMA channel.
 * @req:    Request to be freed.
 * @scmnd:  SCSI command associated with @req.
 * @req_lim_delta: Amount to be added to @target->req_lim.
 */
static void srp_free_req(struct srp_rdma_ch *ch, struct srp_request *req,
			 struct scsi_cmnd *scmnd, s32 req_lim_delta)
{
	unsigned long flags;

	srp_unmap_data(scmnd, ch, req);

	spin_lock_irqsave(&ch->lock, flags);
	ch->req_lim += req_lim_delta;
	spin_unlock_irqrestore(&ch->lock, flags);
}

static void srp_finish_req(struct srp_rdma_ch *ch, struct srp_request *req,
			   struct scsi_device *sdev, int result)
{
	struct scsi_cmnd *scmnd = srp_claim_req(ch, req, sdev, NULL);

	if (scmnd) {
		srp_free_req(ch, req, scmnd, 0);
		scmnd->result = result;
		scsi_done(scmnd);
	}
}

struct srp_terminate_context {
	struct srp_target_port *srp_target;
	int scsi_result;
};

static bool srp_terminate_cmd(struct scsi_cmnd *scmnd, void *context_ptr)
{
	struct srp_terminate_context *context = context_ptr;
	struct srp_target_port *target = context->srp_target;
	u32 tag = blk_mq_unique_tag(scsi_cmd_to_rq(scmnd));
	struct srp_rdma_ch *ch = &target->ch[blk_mq_unique_tag_to_hwq(tag)];
	struct srp_request *req = scsi_cmd_priv(scmnd);

	srp_finish_req(ch, req, NULL, context->scsi_result);

	return true;
}

static void srp_terminate_io(struct srp_rport *rport)
{
	struct srp_target_port *target = rport->lld_data;
	struct srp_terminate_context context = { .srp_target = target,
		.scsi_result = DID_TRANSPORT_FAILFAST << 16 };

	scsi_host_busy_iter(target->scsi_host, srp_terminate_cmd, &context);
}

/* Calculate maximum initiator to target information unit length. */
static uint32_t srp_max_it_iu_len(int cmd_sg_cnt, bool use_imm_data,
				  uint32_t max_it_iu_size)
{
	uint32_t max_iu_len = sizeof(struct srp_cmd) + SRP_MAX_ADD_CDB_LEN +
		sizeof(struct srp_indirect_buf) +
		cmd_sg_cnt * sizeof(struct srp_direct_buf);

	if (use_imm_data)
		max_iu_len = max(max_iu_len, SRP_IMM_DATA_OFFSET +
				 srp_max_imm_data);

	if (max_it_iu_size)
		max_iu_len = min(max_iu_len, max_it_iu_size);

	pr_debug("max_iu_len = %d\n", max_iu_len);

	return max_iu_len;
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
	struct srp_rdma_ch *ch;
	uint32_t max_iu_len = srp_max_it_iu_len(target->cmd_sg_cnt,
						srp_use_imm_data,
						target->max_it_iu_size);
	int i, j, ret = 0;
	bool multich = false;

	srp_disconnect_target(target);

	if (target->state == SRP_TARGET_SCANNING)
		return -ENODEV;

	/*
	 * Now get a new local CM ID so that we avoid confusing the target in
	 * case things are really fouled up. Doing so also ensures that all CM
	 * callbacks will have finished before a new QP is allocated.
	 */
	for (i = 0; i < target->ch_count; i++) {
		ch = &target->ch[i];
		ret += srp_new_cm_id(ch);
	}
	{
		struct srp_terminate_context context = {
			.srp_target = target, .scsi_result = DID_RESET << 16};

		scsi_host_busy_iter(target->scsi_host, srp_terminate_cmd,
				    &context);
	}
	for (i = 0; i < target->ch_count; i++) {
		ch = &target->ch[i];
		/*
		 * Whether or not creating a new CM ID succeeded, create a new
		 * QP. This guarantees that all completion callback function
		 * invocations have finished before request resetting starts.
		 */
		ret += srp_create_ch_ib(ch);

		INIT_LIST_HEAD(&ch->free_tx);
		for (j = 0; j < target->queue_size; ++j)
			list_add(&ch->tx_ring[j]->list, &ch->free_tx);
	}

	target->qp_in_error = false;

	for (i = 0; i < target->ch_count; i++) {
		ch = &target->ch[i];
		if (ret)
			break;
		ret = srp_connect_ch(ch, max_iu_len, multich);
		multich = true;
	}

	if (ret == 0)
		shost_printk(KERN_INFO, target->scsi_host,
			     PFX "reconnect succeeded\n");

	return ret;
}

static void srp_map_desc(struct srp_map_state *state, dma_addr_t dma_addr,
			 unsigned int dma_len, u32 rkey)
{
	struct srp_direct_buf *desc = state->desc;

	WARN_ON_ONCE(!dma_len);

	desc->va = cpu_to_be64(dma_addr);
	desc->key = cpu_to_be32(rkey);
	desc->len = cpu_to_be32(dma_len);

	state->total_len += dma_len;
	state->desc++;
	state->ndesc++;
}

static void srp_reg_mr_err_done(struct ib_cq *cq, struct ib_wc *wc)
{
	srp_handle_qp_err(cq, wc, "FAST REG");
}

/*
 * Map up to sg_nents elements of state->sg where *sg_offset_p is the offset
 * where to start in the first element. If sg_offset_p != NULL then
 * *sg_offset_p is updated to the offset in state->sg[retval] of the first
 * byte that has not yet been mapped.
 */
static int srp_map_finish_fr(struct srp_map_state *state,
			     struct srp_request *req,
			     struct srp_rdma_ch *ch, int sg_nents,
			     unsigned int *sg_offset_p)
{
	struct srp_target_port *target = ch->target;
	struct srp_device *dev = target->srp_host->srp_dev;
	struct ib_reg_wr wr;
	struct srp_fr_desc *desc;
	u32 rkey;
	int n, err;

	if (state->fr.next >= state->fr.end) {
		shost_printk(KERN_ERR, ch->target->scsi_host,
			     PFX "Out of MRs (mr_per_cmd = %d)\n",
			     ch->target->mr_per_cmd);
		return -ENOMEM;
	}

	WARN_ON_ONCE(!dev->use_fast_reg);

	if (sg_nents == 1 && target->global_rkey) {
		unsigned int sg_offset = sg_offset_p ? *sg_offset_p : 0;

		srp_map_desc(state, sg_dma_address(state->sg) + sg_offset,
			     sg_dma_len(state->sg) - sg_offset,
			     target->global_rkey);
		if (sg_offset_p)
			*sg_offset_p = 0;
		return 1;
	}

	desc = srp_fr_pool_get(ch->fr_pool);
	if (!desc)
		return -ENOMEM;

	rkey = ib_inc_rkey(desc->mr->rkey);
	ib_update_fast_reg_key(desc->mr, rkey);

	n = ib_map_mr_sg(desc->mr, state->sg, sg_nents, sg_offset_p,
			 dev->mr_page_size);
	if (unlikely(n < 0)) {
		srp_fr_pool_put(ch->fr_pool, &desc, 1);
		pr_debug("%s: ib_map_mr_sg(%d, %d) returned %d.\n",
			 dev_name(&req->scmnd->device->sdev_gendev), sg_nents,
			 sg_offset_p ? *sg_offset_p : -1, n);
		return n;
	}

	WARN_ON_ONCE(desc->mr->length == 0);

	req->reg_cqe.done = srp_reg_mr_err_done;

	wr.wr.next = NULL;
	wr.wr.opcode = IB_WR_REG_MR;
	wr.wr.wr_cqe = &req->reg_cqe;
	wr.wr.num_sge = 0;
	wr.wr.send_flags = 0;
	wr.mr = desc->mr;
	wr.key = desc->mr->rkey;
	wr.access = (IB_ACCESS_LOCAL_WRITE |
		     IB_ACCESS_REMOTE_READ |
		     IB_ACCESS_REMOTE_WRITE);

	*state->fr.next++ = desc;
	state->nmdesc++;

	srp_map_desc(state, desc->mr->iova,
		     desc->mr->length, desc->mr->rkey);

	err = ib_post_send(ch->qp, &wr.wr, NULL);
	if (unlikely(err)) {
		WARN_ON_ONCE(err == -ENOMEM);
		return err;
	}

	return n;
}

static int srp_map_sg_fr(struct srp_map_state *state, struct srp_rdma_ch *ch,
			 struct srp_request *req, struct scatterlist *scat,
			 int count)
{
	unsigned int sg_offset = 0;

	state->fr.next = req->fr_list;
	state->fr.end = req->fr_list + ch->target->mr_per_cmd;
	state->sg = scat;

	if (count == 0)
		return 0;

	while (count) {
		int i, n;

		n = srp_map_finish_fr(state, req, ch, count, &sg_offset);
		if (unlikely(n < 0))
			return n;

		count -= n;
		for (i = 0; i < n; i++)
			state->sg = sg_next(state->sg);
	}

	return 0;
}

static int srp_map_sg_dma(struct srp_map_state *state, struct srp_rdma_ch *ch,
			  struct srp_request *req, struct scatterlist *scat,
			  int count)
{
	struct srp_target_port *target = ch->target;
	struct scatterlist *sg;
	int i;

	for_each_sg(scat, sg, count, i) {
		srp_map_desc(state, sg_dma_address(sg), sg_dma_len(sg),
			     target->global_rkey);
	}

	return 0;
}

/*
 * Register the indirect data buffer descriptor with the HCA.
 *
 * Note: since the indirect data buffer descriptor has been allocated with
 * kmalloc() it is guaranteed that this buffer is a physically contiguous
 * memory buffer.
 */
static int srp_map_idb(struct srp_rdma_ch *ch, struct srp_request *req,
		       void **next_mr, void **end_mr, u32 idb_len,
		       __be32 *idb_rkey)
{
	struct srp_target_port *target = ch->target;
	struct srp_device *dev = target->srp_host->srp_dev;
	struct srp_map_state state;
	struct srp_direct_buf idb_desc;
	struct scatterlist idb_sg[1];
	int ret;

	memset(&state, 0, sizeof(state));
	memset(&idb_desc, 0, sizeof(idb_desc));
	state.gen.next = next_mr;
	state.gen.end = end_mr;
	state.desc = &idb_desc;
	state.base_dma_addr = req->indirect_dma_addr;
	state.dma_len = idb_len;

	if (dev->use_fast_reg) {
		state.sg = idb_sg;
		sg_init_one(idb_sg, req->indirect_desc, idb_len);
		idb_sg->dma_address = req->indirect_dma_addr; /* hack! */
#ifdef CONFIG_NEED_SG_DMA_LENGTH
		idb_sg->dma_length = idb_sg->length;	      /* hack^2 */
#endif
		ret = srp_map_finish_fr(&state, req, ch, 1, NULL);
		if (ret < 0)
			return ret;
		WARN_ON_ONCE(ret < 1);
	} else {
		return -EINVAL;
	}

	*idb_rkey = idb_desc.key;

	return 0;
}

static void srp_check_mapping(struct srp_map_state *state,
			      struct srp_rdma_ch *ch, struct srp_request *req,
			      struct scatterlist *scat, int count)
{
	struct srp_device *dev = ch->target->srp_host->srp_dev;
	struct srp_fr_desc **pfr;
	u64 desc_len = 0, mr_len = 0;
	int i;

	for (i = 0; i < state->ndesc; i++)
		desc_len += be32_to_cpu(req->indirect_desc[i].len);
	if (dev->use_fast_reg)
		for (i = 0, pfr = req->fr_list; i < state->nmdesc; i++, pfr++)
			mr_len += (*pfr)->mr->length;
	if (desc_len != scsi_bufflen(req->scmnd) ||
	    mr_len > scsi_bufflen(req->scmnd))
		pr_err("Inconsistent: scsi len %d <> desc len %lld <> mr len %lld; ndesc %d; nmdesc = %d\n",
		       scsi_bufflen(req->scmnd), desc_len, mr_len,
		       state->ndesc, state->nmdesc);
}

/**
 * srp_map_data() - map SCSI data buffer onto an SRP request
 * @scmnd: SCSI command to map
 * @ch: SRP RDMA channel
 * @req: SRP request
 *
 * Returns the length in bytes of the SRP_CMD IU or a negative value if
 * mapping failed. The size of any immediate data is not included in the
 * return value.
 */
static int srp_map_data(struct scsi_cmnd *scmnd, struct srp_rdma_ch *ch,
			struct srp_request *req)
{
	struct srp_target_port *target = ch->target;
	struct scatterlist *scat, *sg;
	struct srp_cmd *cmd = req->cmd->buf;
	int i, len, nents, count, ret;
	struct srp_device *dev;
	struct ib_device *ibdev;
	struct srp_map_state state;
	struct srp_indirect_buf *indirect_hdr;
	u64 data_len;
	u32 idb_len, table_len;
	__be32 idb_rkey;
	u8 fmt;

	req->cmd->num_sge = 1;

	if (!scsi_sglist(scmnd) || scmnd->sc_data_direction == DMA_NONE)
		return sizeof(struct srp_cmd) + cmd->add_cdb_len;

	if (scmnd->sc_data_direction != DMA_FROM_DEVICE &&
	    scmnd->sc_data_direction != DMA_TO_DEVICE) {
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Unhandled data direction %d\n",
			     scmnd->sc_data_direction);
		return -EINVAL;
	}

	nents = scsi_sg_count(scmnd);
	scat  = scsi_sglist(scmnd);
	data_len = scsi_bufflen(scmnd);

	dev = target->srp_host->srp_dev;
	ibdev = dev->dev;

	count = ib_dma_map_sg(ibdev, scat, nents, scmnd->sc_data_direction);
	if (unlikely(count == 0))
		return -EIO;

	if (ch->use_imm_data &&
	    count <= ch->max_imm_sge &&
	    SRP_IMM_DATA_OFFSET + data_len <= ch->max_it_iu_len &&
	    scmnd->sc_data_direction == DMA_TO_DEVICE) {
		struct srp_imm_buf *buf;
		struct ib_sge *sge = &req->cmd->sge[1];

		fmt = SRP_DATA_DESC_IMM;
		len = SRP_IMM_DATA_OFFSET;
		req->nmdesc = 0;
		buf = (void *)cmd->add_data + cmd->add_cdb_len;
		buf->len = cpu_to_be32(data_len);
		WARN_ON_ONCE((void *)(buf + 1) > (void *)cmd + len);
		for_each_sg(scat, sg, count, i) {
			sge[i].addr   = sg_dma_address(sg);
			sge[i].length = sg_dma_len(sg);
			sge[i].lkey   = target->lkey;
		}
		req->cmd->num_sge += count;
		goto map_complete;
	}

	fmt = SRP_DATA_DESC_DIRECT;
	len = sizeof(struct srp_cmd) + cmd->add_cdb_len +
		sizeof(struct srp_direct_buf);

	if (count == 1 && target->global_rkey) {
		/*
		 * The midlayer only generated a single gather/scatter
		 * entry, or DMA mapping coalesced everything to a
		 * single entry.  So a direct descriptor along with
		 * the DMA MR suffices.
		 */
		struct srp_direct_buf *buf;

		buf = (void *)cmd->add_data + cmd->add_cdb_len;
		buf->va  = cpu_to_be64(sg_dma_address(scat));
		buf->key = cpu_to_be32(target->global_rkey);
		buf->len = cpu_to_be32(sg_dma_len(scat));

		req->nmdesc = 0;
		goto map_complete;
	}

	/*
	 * We have more than one scatter/gather entry, so build our indirect
	 * descriptor table, trying to merge as many entries as we can.
	 */
	indirect_hdr = (void *)cmd->add_data + cmd->add_cdb_len;

	ib_dma_sync_single_for_cpu(ibdev, req->indirect_dma_addr,
				   target->indirect_size, DMA_TO_DEVICE);

	memset(&state, 0, sizeof(state));
	state.desc = req->indirect_desc;
	if (dev->use_fast_reg)
		ret = srp_map_sg_fr(&state, ch, req, scat, count);
	else
		ret = srp_map_sg_dma(&state, ch, req, scat, count);
	req->nmdesc = state.nmdesc;
	if (ret < 0)
		goto unmap;

	{
		DEFINE_DYNAMIC_DEBUG_METADATA(ddm,
			"Memory mapping consistency check");
		if (DYNAMIC_DEBUG_BRANCH(ddm))
			srp_check_mapping(&state, ch, req, scat, count);
	}

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
		struct srp_direct_buf *buf;

		buf = (void *)cmd->add_data + cmd->add_cdb_len;
		*buf = req->indirect_desc[0];
		goto map_complete;
	}

	if (unlikely(target->cmd_sg_cnt < state.ndesc &&
						!target->allow_ext_sg)) {
		shost_printk(KERN_ERR, target->scsi_host,
			     "Could not fit S/G list into SRP_CMD\n");
		ret = -EIO;
		goto unmap;
	}

	count = min(state.ndesc, target->cmd_sg_cnt);
	table_len = state.ndesc * sizeof (struct srp_direct_buf);
	idb_len = sizeof(struct srp_indirect_buf) + table_len;

	fmt = SRP_DATA_DESC_INDIRECT;
	len = sizeof(struct srp_cmd) + cmd->add_cdb_len +
		sizeof(struct srp_indirect_buf);
	len += count * sizeof (struct srp_direct_buf);

	memcpy(indirect_hdr->desc_list, req->indirect_desc,
	       count * sizeof (struct srp_direct_buf));

	if (!target->global_rkey) {
		ret = srp_map_idb(ch, req, state.gen.next, state.gen.end,
				  idb_len, &idb_rkey);
		if (ret < 0)
			goto unmap;
		req->nmdesc++;
	} else {
		idb_rkey = cpu_to_be32(target->global_rkey);
	}

	indirect_hdr->table_desc.va = cpu_to_be64(req->indirect_dma_addr);
	indirect_hdr->table_desc.key = idb_rkey;
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

unmap:
	srp_unmap_data(scmnd, ch, req);
	if (ret == -ENOMEM && req->nmdesc >= target->mr_pool_size)
		ret = -E2BIG;
	return ret;
}

/*
 * Return an IU and possible credit to the free pool
 */
static void srp_put_tx_iu(struct srp_rdma_ch *ch, struct srp_iu *iu,
			  enum srp_iu_type iu_type)
{
	unsigned long flags;

	spin_lock_irqsave(&ch->lock, flags);
	list_add(&iu->list, &ch->free_tx);
	if (iu_type != SRP_IU_RSP)
		++ch->req_lim;
	spin_unlock_irqrestore(&ch->lock, flags);
}

/*
 * Must be called with ch->lock held to protect req_lim and free_tx.
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
static struct srp_iu *__srp_get_tx_iu(struct srp_rdma_ch *ch,
				      enum srp_iu_type iu_type)
{
	struct srp_target_port *target = ch->target;
	s32 rsv = (iu_type == SRP_IU_TSK_MGMT) ? 0 : SRP_TSK_MGMT_SQ_SIZE;
	struct srp_iu *iu;

	lockdep_assert_held(&ch->lock);

	ib_process_cq_direct(ch->send_cq, -1);

	if (list_empty(&ch->free_tx))
		return NULL;

	/* Initiator responses to target requests do not consume credits */
	if (iu_type != SRP_IU_RSP) {
		if (ch->req_lim <= rsv) {
			++target->zero_req_lim;
			return NULL;
		}

		--ch->req_lim;
	}

	iu = list_first_entry(&ch->free_tx, struct srp_iu, list);
	list_del(&iu->list);
	return iu;
}

/*
 * Note: if this function is called from inside ib_drain_sq() then it will
 * be called without ch->lock being held. If ib_drain_sq() dequeues a WQE
 * with status IB_WC_SUCCESS then that's a bug.
 */
static void srp_send_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct srp_iu *iu = container_of(wc->wr_cqe, struct srp_iu, cqe);
	struct srp_rdma_ch *ch = cq->cq_context;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		srp_handle_qp_err(cq, wc, "SEND");
		return;
	}

	lockdep_assert_held(&ch->lock);

	list_add(&iu->list, &ch->free_tx);
}

/**
 * srp_post_send() - send an SRP information unit
 * @ch: RDMA channel over which to send the information unit.
 * @iu: Information unit to send.
 * @len: Length of the information unit excluding immediate data.
 */
static int srp_post_send(struct srp_rdma_ch *ch, struct srp_iu *iu, int len)
{
	struct srp_target_port *target = ch->target;
	struct ib_send_wr wr;

	if (WARN_ON_ONCE(iu->num_sge > SRP_MAX_SGE))
		return -EINVAL;

	iu->sge[0].addr   = iu->dma;
	iu->sge[0].length = len;
	iu->sge[0].lkey   = target->lkey;

	iu->cqe.done = srp_send_done;

	wr.next       = NULL;
	wr.wr_cqe     = &iu->cqe;
	wr.sg_list    = &iu->sge[0];
	wr.num_sge    = iu->num_sge;
	wr.opcode     = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED;

	return ib_post_send(ch->qp, &wr, NULL);
}

static int srp_post_recv(struct srp_rdma_ch *ch, struct srp_iu *iu)
{
	struct srp_target_port *target = ch->target;
	struct ib_recv_wr wr;
	struct ib_sge list;

	list.addr   = iu->dma;
	list.length = iu->size;
	list.lkey   = target->lkey;

	iu->cqe.done = srp_recv_done;

	wr.next     = NULL;
	wr.wr_cqe   = &iu->cqe;
	wr.sg_list  = &list;
	wr.num_sge  = 1;

	return ib_post_recv(ch->qp, &wr, NULL);
}

static void srp_process_rsp(struct srp_rdma_ch *ch, struct srp_rsp *rsp)
{
	struct srp_target_port *target = ch->target;
	struct srp_request *req;
	struct scsi_cmnd *scmnd;
	unsigned long flags;

	if (unlikely(rsp->tag & SRP_TAG_TSK_MGMT)) {
		spin_lock_irqsave(&ch->lock, flags);
		ch->req_lim += be32_to_cpu(rsp->req_lim_delta);
		if (rsp->tag == ch->tsk_mgmt_tag) {
			ch->tsk_mgmt_status = -1;
			if (be32_to_cpu(rsp->resp_data_len) >= 4)
				ch->tsk_mgmt_status = rsp->data[3];
			complete(&ch->tsk_mgmt_done);
		} else {
			shost_printk(KERN_ERR, target->scsi_host,
				     "Received tsk mgmt response too late for tag %#llx\n",
				     rsp->tag);
		}
		spin_unlock_irqrestore(&ch->lock, flags);
	} else {
		scmnd = scsi_host_find_tag(target->scsi_host, rsp->tag);
		if (scmnd) {
			req = scsi_cmd_priv(scmnd);
			scmnd = srp_claim_req(ch, req, NULL, scmnd);
		}
		if (!scmnd) {
			shost_printk(KERN_ERR, target->scsi_host,
				     "Null scmnd for RSP w/tag %#016llx received on ch %td / QP %#x\n",
				     rsp->tag, ch - target->ch, ch->qp->qp_num);

			spin_lock_irqsave(&ch->lock, flags);
			ch->req_lim += be32_to_cpu(rsp->req_lim_delta);
			spin_unlock_irqrestore(&ch->lock, flags);

			return;
		}
		scmnd->result = rsp->status;

		if (rsp->flags & SRP_RSP_FLAG_SNSVALID) {
			memcpy(scmnd->sense_buffer, rsp->data +
			       be32_to_cpu(rsp->resp_data_len),
			       min_t(int, be32_to_cpu(rsp->sense_data_len),
				     SCSI_SENSE_BUFFERSIZE));
		}

		if (unlikely(rsp->flags & SRP_RSP_FLAG_DIUNDER))
			scsi_set_resid(scmnd, be32_to_cpu(rsp->data_in_res_cnt));
		else if (unlikely(rsp->flags & SRP_RSP_FLAG_DOUNDER))
			scsi_set_resid(scmnd, be32_to_cpu(rsp->data_out_res_cnt));

		srp_free_req(ch, req, scmnd,
			     be32_to_cpu(rsp->req_lim_delta));

		scsi_done(scmnd);
	}
}

static int srp_response_common(struct srp_rdma_ch *ch, s32 req_delta,
			       void *rsp, int len)
{
	struct srp_target_port *target = ch->target;
	struct ib_device *dev = target->srp_host->srp_dev->dev;
	unsigned long flags;
	struct srp_iu *iu;
	int err;

	spin_lock_irqsave(&ch->lock, flags);
	ch->req_lim += req_delta;
	iu = __srp_get_tx_iu(ch, SRP_IU_RSP);
	spin_unlock_irqrestore(&ch->lock, flags);

	if (!iu) {
		shost_printk(KERN_ERR, target->scsi_host, PFX
			     "no IU available to send response\n");
		return 1;
	}

	iu->num_sge = 1;
	ib_dma_sync_single_for_cpu(dev, iu->dma, len, DMA_TO_DEVICE);
	memcpy(iu->buf, rsp, len);
	ib_dma_sync_single_for_device(dev, iu->dma, len, DMA_TO_DEVICE);

	err = srp_post_send(ch, iu, len);
	if (err) {
		shost_printk(KERN_ERR, target->scsi_host, PFX
			     "unable to post response: %d\n", err);
		srp_put_tx_iu(ch, iu, SRP_IU_RSP);
	}

	return err;
}

static void srp_process_cred_req(struct srp_rdma_ch *ch,
				 struct srp_cred_req *req)
{
	struct srp_cred_rsp rsp = {
		.opcode = SRP_CRED_RSP,
		.tag = req->tag,
	};
	s32 delta = be32_to_cpu(req->req_lim_delta);

	if (srp_response_common(ch, delta, &rsp, sizeof(rsp)))
		shost_printk(KERN_ERR, ch->target->scsi_host, PFX
			     "problems processing SRP_CRED_REQ\n");
}

static void srp_process_aer_req(struct srp_rdma_ch *ch,
				struct srp_aer_req *req)
{
	struct srp_target_port *target = ch->target;
	struct srp_aer_rsp rsp = {
		.opcode = SRP_AER_RSP,
		.tag = req->tag,
	};
	s32 delta = be32_to_cpu(req->req_lim_delta);

	shost_printk(KERN_ERR, target->scsi_host, PFX
		     "ignoring AER for LUN %llu\n", scsilun_to_int(&req->lun));

	if (srp_response_common(ch, delta, &rsp, sizeof(rsp)))
		shost_printk(KERN_ERR, target->scsi_host, PFX
			     "problems processing SRP_AER_REQ\n");
}

static void srp_recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct srp_iu *iu = container_of(wc->wr_cqe, struct srp_iu, cqe);
	struct srp_rdma_ch *ch = cq->cq_context;
	struct srp_target_port *target = ch->target;
	struct ib_device *dev = target->srp_host->srp_dev->dev;
	int res;
	u8 opcode;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		srp_handle_qp_err(cq, wc, "RECV");
		return;
	}

	ib_dma_sync_single_for_cpu(dev, iu->dma, ch->max_ti_iu_len,
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
		srp_process_rsp(ch, iu->buf);
		break;

	case SRP_CRED_REQ:
		srp_process_cred_req(ch, iu->buf);
		break;

	case SRP_AER_REQ:
		srp_process_aer_req(ch, iu->buf);
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

	ib_dma_sync_single_for_device(dev, iu->dma, ch->max_ti_iu_len,
				      DMA_FROM_DEVICE);

	res = srp_post_recv(ch, iu);
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

static void srp_handle_qp_err(struct ib_cq *cq, struct ib_wc *wc,
		const char *opname)
{
	struct srp_rdma_ch *ch = cq->cq_context;
	struct srp_target_port *target = ch->target;

	if (ch->connected && !target->qp_in_error) {
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "failed %s status %s (%d) for CQE %p\n",
			     opname, ib_wc_status_msg(wc->status), wc->status,
			     wc->wr_cqe);
		queue_work(system_long_wq, &target->tl_err_work);
	}
	target->qp_in_error = true;
}

static int srp_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *scmnd)
{
	struct request *rq = scsi_cmd_to_rq(scmnd);
	struct srp_target_port *target = host_to_target(shost);
	struct srp_rdma_ch *ch;
	struct srp_request *req = scsi_cmd_priv(scmnd);
	struct srp_iu *iu;
	struct srp_cmd *cmd;
	struct ib_device *dev;
	unsigned long flags;
	u32 tag;
	int len, ret;

	scmnd->result = srp_chkready(target->rport);
	if (unlikely(scmnd->result))
		goto err;

	WARN_ON_ONCE(rq->tag < 0);
	tag = blk_mq_unique_tag(rq);
	ch = &target->ch[blk_mq_unique_tag_to_hwq(tag)];

	spin_lock_irqsave(&ch->lock, flags);
	iu = __srp_get_tx_iu(ch, SRP_IU_CMD);
	spin_unlock_irqrestore(&ch->lock, flags);

	if (!iu)
		goto err;

	dev = target->srp_host->srp_dev->dev;
	ib_dma_sync_single_for_cpu(dev, iu->dma, ch->max_it_iu_len,
				   DMA_TO_DEVICE);

	cmd = iu->buf;
	memset(cmd, 0, sizeof *cmd);

	cmd->opcode = SRP_CMD;
	int_to_scsilun(scmnd->device->lun, &cmd->lun);
	cmd->tag    = tag;
	memcpy(cmd->cdb, scmnd->cmnd, scmnd->cmd_len);
	if (unlikely(scmnd->cmd_len > sizeof(cmd->cdb))) {
		cmd->add_cdb_len = round_up(scmnd->cmd_len - sizeof(cmd->cdb),
					    4);
		if (WARN_ON_ONCE(cmd->add_cdb_len > SRP_MAX_ADD_CDB_LEN))
			goto err_iu;
	}

	req->scmnd    = scmnd;
	req->cmd      = iu;

	len = srp_map_data(scmnd, ch, req);
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
			DID_OK << 16 | SAM_STAT_TASK_SET_FULL : DID_ERROR << 16;
		goto err_iu;
	}

	ib_dma_sync_single_for_device(dev, iu->dma, ch->max_it_iu_len,
				      DMA_TO_DEVICE);

	if (srp_post_send(ch, iu, len)) {
		shost_printk(KERN_ERR, target->scsi_host, PFX "Send failed\n");
		scmnd->result = DID_ERROR << 16;
		goto err_unmap;
	}

	return 0;

err_unmap:
	srp_unmap_data(scmnd, ch, req);

err_iu:
	srp_put_tx_iu(ch, iu, SRP_IU_CMD);

	/*
	 * Avoid that the loops that iterate over the request ring can
	 * encounter a dangling SCSI command pointer.
	 */
	req->scmnd = NULL;

err:
	if (scmnd->result) {
		scsi_done(scmnd);
		ret = 0;
	} else {
		ret = SCSI_MLQUEUE_HOST_BUSY;
	}

	return ret;
}

/*
 * Note: the resources allocated in this function are freed in
 * srp_free_ch_ib().
 */
static int srp_alloc_iu_bufs(struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;
	int i;

	ch->rx_ring = kcalloc(target->queue_size, sizeof(*ch->rx_ring),
			      GFP_KERNEL);
	if (!ch->rx_ring)
		goto err_no_ring;
	ch->tx_ring = kcalloc(target->queue_size, sizeof(*ch->tx_ring),
			      GFP_KERNEL);
	if (!ch->tx_ring)
		goto err_no_ring;

	for (i = 0; i < target->queue_size; ++i) {
		ch->rx_ring[i] = srp_alloc_iu(target->srp_host,
					      ch->max_ti_iu_len,
					      GFP_KERNEL, DMA_FROM_DEVICE);
		if (!ch->rx_ring[i])
			goto err;
	}

	for (i = 0; i < target->queue_size; ++i) {
		ch->tx_ring[i] = srp_alloc_iu(target->srp_host,
					      ch->max_it_iu_len,
					      GFP_KERNEL, DMA_TO_DEVICE);
		if (!ch->tx_ring[i])
			goto err;

		list_add(&ch->tx_ring[i]->list, &ch->free_tx);
	}

	return 0;

err:
	for (i = 0; i < target->queue_size; ++i) {
		srp_free_iu(target->srp_host, ch->rx_ring[i]);
		srp_free_iu(target->srp_host, ch->tx_ring[i]);
	}


err_no_ring:
	kfree(ch->tx_ring);
	ch->tx_ring = NULL;
	kfree(ch->rx_ring);
	ch->rx_ring = NULL;

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
			       const struct srp_login_rsp *lrsp,
			       struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;
	struct ib_qp_attr *qp_attr = NULL;
	int attr_mask = 0;
	int ret = 0;
	int i;

	if (lrsp->opcode == SRP_LOGIN_RSP) {
		ch->max_ti_iu_len = be32_to_cpu(lrsp->max_ti_iu_len);
		ch->req_lim       = be32_to_cpu(lrsp->req_lim_delta);
		ch->use_imm_data  = srp_use_imm_data &&
			(lrsp->rsp_flags & SRP_LOGIN_RSP_IMMED_SUPP);
		ch->max_it_iu_len = srp_max_it_iu_len(target->cmd_sg_cnt,
						      ch->use_imm_data,
						      target->max_it_iu_size);
		WARN_ON_ONCE(ch->max_it_iu_len >
			     be32_to_cpu(lrsp->max_it_iu_len));

		if (ch->use_imm_data)
			shost_printk(KERN_DEBUG, target->scsi_host,
				     PFX "using immediate data\n");

		/*
		 * Reserve credits for task management so we don't
		 * bounce requests back to the SCSI mid-layer.
		 */
		target->scsi_host->can_queue
			= min(ch->req_lim - SRP_TSK_MGMT_SQ_SIZE,
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

	if (!ch->rx_ring) {
		ret = srp_alloc_iu_bufs(ch);
		if (ret)
			goto error;
	}

	for (i = 0; i < target->queue_size; i++) {
		struct srp_iu *iu = ch->rx_ring[i];

		ret = srp_post_recv(ch, iu);
		if (ret)
			goto error;
	}

	if (!target->using_rdma_cm) {
		ret = -ENOMEM;
		qp_attr = kmalloc(sizeof(*qp_attr), GFP_KERNEL);
		if (!qp_attr)
			goto error;

		qp_attr->qp_state = IB_QPS_RTR;
		ret = ib_cm_init_qp_attr(cm_id, qp_attr, &attr_mask);
		if (ret)
			goto error_free;

		ret = ib_modify_qp(ch->qp, qp_attr, attr_mask);
		if (ret)
			goto error_free;

		qp_attr->qp_state = IB_QPS_RTS;
		ret = ib_cm_init_qp_attr(cm_id, qp_attr, &attr_mask);
		if (ret)
			goto error_free;

		target->rq_tmo_jiffies = srp_compute_rq_tmo(qp_attr, attr_mask);

		ret = ib_modify_qp(ch->qp, qp_attr, attr_mask);
		if (ret)
			goto error_free;

		ret = ib_send_cm_rtu(cm_id, NULL, 0);
	}

error_free:
	kfree(qp_attr);

error:
	ch->status = ret;
}

static void srp_ib_cm_rej_handler(struct ib_cm_id *cm_id,
				  const struct ib_cm_event *event,
				  struct srp_rdma_ch *ch)
{
	struct srp_target_port *target = ch->target;
	struct Scsi_Host *shost = target->scsi_host;
	struct ib_class_port_info *cpi;
	int opcode;
	u16 dlid;

	switch (event->param.rej_rcvd.reason) {
	case IB_CM_REJ_PORT_CM_REDIRECT:
		cpi = event->param.rej_rcvd.ari;
		dlid = be16_to_cpu(cpi->redirect_lid);
		sa_path_set_dlid(&ch->ib_cm.path, dlid);
		ch->ib_cm.path.pkey = cpi->redirect_pkey;
		cm_id->remote_cm_qpn = be32_to_cpu(cpi->redirect_qp) & 0x00ffffff;
		memcpy(ch->ib_cm.path.dgid.raw, cpi->redirect_gid, 16);

		ch->status = dlid ? SRP_DLID_REDIRECT : SRP_PORT_REDIRECT;
		break;

	case IB_CM_REJ_PORT_REDIRECT:
		if (srp_target_is_topspin(target)) {
			union ib_gid *dgid = &ch->ib_cm.path.dgid;

			/*
			 * Topspin/Cisco SRP gateways incorrectly send
			 * reject reason code 25 when they mean 24
			 * (port redirect).
			 */
			memcpy(dgid->raw, event->param.rej_rcvd.ari, 16);

			shost_printk(KERN_DEBUG, shost,
				     PFX "Topspin/Cisco redirect to target port GID %016llx%016llx\n",
				     be64_to_cpu(dgid->global.subnet_prefix),
				     be64_to_cpu(dgid->global.interface_id));

			ch->status = SRP_PORT_REDIRECT;
		} else {
			shost_printk(KERN_WARNING, shost,
				     "  REJ reason: IB_CM_REJ_PORT_REDIRECT\n");
			ch->status = -ECONNRESET;
		}
		break;

	case IB_CM_REJ_DUPLICATE_LOCAL_COMM_ID:
		shost_printk(KERN_WARNING, shost,
			    "  REJ reason: IB_CM_REJ_DUPLICATE_LOCAL_COMM_ID\n");
		ch->status = -ECONNRESET;
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
					     target->sgid.raw,
					     target->ib_cm.orig_dgid.raw,
					     reason);
		} else
			shost_printk(KERN_WARNING, shost,
				     "  REJ reason: IB_CM_REJ_CONSUMER_DEFINED,"
				     " opcode 0x%02x\n", opcode);
		ch->status = -ECONNRESET;
		break;

	case IB_CM_REJ_STALE_CONN:
		shost_printk(KERN_WARNING, shost, "  REJ reason: stale connection\n");
		ch->status = SRP_STALE_CONN;
		break;

	default:
		shost_printk(KERN_WARNING, shost, "  REJ reason 0x%x\n",
			     event->param.rej_rcvd.reason);
		ch->status = -ECONNRESET;
	}
}

static int srp_ib_cm_handler(struct ib_cm_id *cm_id,
			     const struct ib_cm_event *event)
{
	struct srp_rdma_ch *ch = cm_id->context;
	struct srp_target_port *target = ch->target;
	int comp = 0;

	switch (event->event) {
	case IB_CM_REQ_ERROR:
		shost_printk(KERN_DEBUG, target->scsi_host,
			     PFX "Sending CM REQ failed\n");
		comp = 1;
		ch->status = -ECONNRESET;
		break;

	case IB_CM_REP_RECEIVED:
		comp = 1;
		srp_cm_rep_handler(cm_id, event->private_data, ch);
		break;

	case IB_CM_REJ_RECEIVED:
		shost_printk(KERN_DEBUG, target->scsi_host, PFX "REJ received\n");
		comp = 1;

		srp_ib_cm_rej_handler(cm_id, event, ch);
		break;

	case IB_CM_DREQ_RECEIVED:
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "DREQ received - connection closed\n");
		ch->connected = false;
		if (ib_send_cm_drep(cm_id, NULL, 0))
			shost_printk(KERN_ERR, target->scsi_host,
				     PFX "Sending CM DREP failed\n");
		queue_work(system_long_wq, &target->tl_err_work);
		break;

	case IB_CM_TIMEWAIT_EXIT:
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "connection closed\n");
		comp = 1;

		ch->status = 0;
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
		complete(&ch->done);

	return 0;
}

static void srp_rdma_cm_rej_handler(struct srp_rdma_ch *ch,
				    struct rdma_cm_event *event)
{
	struct srp_target_port *target = ch->target;
	struct Scsi_Host *shost = target->scsi_host;
	int opcode;

	switch (event->status) {
	case IB_CM_REJ_DUPLICATE_LOCAL_COMM_ID:
		shost_printk(KERN_WARNING, shost,
			    "  REJ reason: IB_CM_REJ_DUPLICATE_LOCAL_COMM_ID\n");
		ch->status = -ECONNRESET;
		break;

	case IB_CM_REJ_CONSUMER_DEFINED:
		opcode = *(u8 *) event->param.conn.private_data;
		if (opcode == SRP_LOGIN_REJ) {
			struct srp_login_rej *rej =
				(struct srp_login_rej *)
				event->param.conn.private_data;
			u32 reason = be32_to_cpu(rej->reason);

			if (reason == SRP_LOGIN_REJ_REQ_IT_IU_LENGTH_TOO_LARGE)
				shost_printk(KERN_WARNING, shost,
					     PFX "SRP_LOGIN_REJ: requested max_it_iu_len too large\n");
			else
				shost_printk(KERN_WARNING, shost,
					    PFX "SRP LOGIN REJECTED, reason 0x%08x\n", reason);
		} else {
			shost_printk(KERN_WARNING, shost,
				     "  REJ reason: IB_CM_REJ_CONSUMER_DEFINED, opcode 0x%02x\n",
				     opcode);
		}
		ch->status = -ECONNRESET;
		break;

	case IB_CM_REJ_STALE_CONN:
		shost_printk(KERN_WARNING, shost,
			     "  REJ reason: stale connection\n");
		ch->status = SRP_STALE_CONN;
		break;

	default:
		shost_printk(KERN_WARNING, shost, "  REJ reason 0x%x\n",
			     event->status);
		ch->status = -ECONNRESET;
		break;
	}
}

static int srp_rdma_cm_handler(struct rdma_cm_id *cm_id,
			       struct rdma_cm_event *event)
{
	struct srp_rdma_ch *ch = cm_id->context;
	struct srp_target_port *target = ch->target;
	int comp = 0;

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		ch->status = 0;
		comp = 1;
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
		ch->status = -ENXIO;
		comp = 1;
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ch->status = 0;
		comp = 1;
		break;

	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
		ch->status = -EHOSTUNREACH;
		comp = 1;
		break;

	case RDMA_CM_EVENT_CONNECT_ERROR:
		shost_printk(KERN_DEBUG, target->scsi_host,
			     PFX "Sending CM REQ failed\n");
		comp = 1;
		ch->status = -ECONNRESET;
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		comp = 1;
		srp_cm_rep_handler(NULL, event->param.conn.private_data, ch);
		break;

	case RDMA_CM_EVENT_REJECTED:
		shost_printk(KERN_DEBUG, target->scsi_host, PFX "REJ received\n");
		comp = 1;

		srp_rdma_cm_rej_handler(ch, event);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		if (ch->connected) {
			shost_printk(KERN_WARNING, target->scsi_host,
				     PFX "received DREQ\n");
			rdma_disconnect(ch->rdma_cm.cm_id);
			comp = 1;
			ch->status = 0;
			queue_work(system_long_wq, &target->tl_err_work);
		}
		break;

	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		shost_printk(KERN_ERR, target->scsi_host,
			     PFX "connection closed\n");

		comp = 1;
		ch->status = 0;
		break;

	default:
		shost_printk(KERN_WARNING, target->scsi_host,
			     PFX "Unhandled CM event %d\n", event->event);
		break;
	}

	if (comp)
		complete(&ch->done);

	return 0;
}

/**
 * srp_change_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 *
 * Returns queue depth.
 */
static int
srp_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	if (!sdev->tagged_supported)
		qdepth = 1;
	return scsi_change_queue_depth(sdev, qdepth);
}

static int srp_send_tsk_mgmt(struct srp_rdma_ch *ch, u64 req_tag, u64 lun,
			     u8 func, u8 *status)
{
	struct srp_target_port *target = ch->target;
	struct srp_rport *rport = target->rport;
	struct ib_device *dev = target->srp_host->srp_dev->dev;
	struct srp_iu *iu;
	struct srp_tsk_mgmt *tsk_mgmt;
	int res;

	if (!ch->connected || target->qp_in_error)
		return -1;

	/*
	 * Lock the rport mutex to avoid that srp_create_ch_ib() is
	 * invoked while a task management function is being sent.
	 */
	mutex_lock(&rport->mutex);
	spin_lock_irq(&ch->lock);
	iu = __srp_get_tx_iu(ch, SRP_IU_TSK_MGMT);
	spin_unlock_irq(&ch->lock);

	if (!iu) {
		mutex_unlock(&rport->mutex);

		return -1;
	}

	iu->num_sge = 1;

	ib_dma_sync_single_for_cpu(dev, iu->dma, sizeof *tsk_mgmt,
				   DMA_TO_DEVICE);
	tsk_mgmt = iu->buf;
	memset(tsk_mgmt, 0, sizeof *tsk_mgmt);

	tsk_mgmt->opcode 	= SRP_TSK_MGMT;
	int_to_scsilun(lun, &tsk_mgmt->lun);
	tsk_mgmt->tsk_mgmt_func = func;
	tsk_mgmt->task_tag	= req_tag;

	spin_lock_irq(&ch->lock);
	ch->tsk_mgmt_tag = (ch->tsk_mgmt_tag + 1) | SRP_TAG_TSK_MGMT;
	tsk_mgmt->tag = ch->tsk_mgmt_tag;
	spin_unlock_irq(&ch->lock);

	init_completion(&ch->tsk_mgmt_done);

	ib_dma_sync_single_for_device(dev, iu->dma, sizeof *tsk_mgmt,
				      DMA_TO_DEVICE);
	if (srp_post_send(ch, iu, sizeof(*tsk_mgmt))) {
		srp_put_tx_iu(ch, iu, SRP_IU_TSK_MGMT);
		mutex_unlock(&rport->mutex);

		return -1;
	}
	res = wait_for_completion_timeout(&ch->tsk_mgmt_done,
					msecs_to_jiffies(SRP_ABORT_TIMEOUT_MS));
	if (res > 0 && status)
		*status = ch->tsk_mgmt_status;
	mutex_unlock(&rport->mutex);

	WARN_ON_ONCE(res < 0);

	return res > 0 ? 0 : -1;
}

static int srp_abort(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);
	struct srp_request *req = scsi_cmd_priv(scmnd);
	u32 tag;
	u16 ch_idx;
	struct srp_rdma_ch *ch;

	shost_printk(KERN_ERR, target->scsi_host, "SRP abort called\n");

	tag = blk_mq_unique_tag(scsi_cmd_to_rq(scmnd));
	ch_idx = blk_mq_unique_tag_to_hwq(tag);
	if (WARN_ON_ONCE(ch_idx >= target->ch_count))
		return SUCCESS;
	ch = &target->ch[ch_idx];
	if (!srp_claim_req(ch, req, NULL, scmnd))
		return SUCCESS;
	shost_printk(KERN_ERR, target->scsi_host,
		     "Sending SRP abort for tag %#x\n", tag);
	if (srp_send_tsk_mgmt(ch, tag, scmnd->device->lun,
			      SRP_TSK_ABORT_TASK, NULL) == 0) {
		srp_free_req(ch, req, scmnd, 0);
		return SUCCESS;
	}
	if (target->rport->state == SRP_RPORT_LOST)
		return FAST_IO_FAIL;

	return FAILED;
}

static int srp_reset_device(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);
	struct srp_rdma_ch *ch;
	u8 status;

	shost_printk(KERN_ERR, target->scsi_host, "SRP reset_device called\n");

	ch = &target->ch[0];
	if (srp_send_tsk_mgmt(ch, SRP_TAG_NO_REQ, scmnd->device->lun,
			      SRP_TSK_LUN_RESET, &status))
		return FAILED;
	if (status)
		return FAILED;

	return SUCCESS;
}

static int srp_reset_host(struct scsi_cmnd *scmnd)
{
	struct srp_target_port *target = host_to_target(scmnd->device->host);

	shost_printk(KERN_ERR, target->scsi_host, PFX "SRP reset_host called\n");

	return srp_reconnect_rport(target->rport) == 0 ? SUCCESS : FAILED;
}

static int srp_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct srp_target_port *target = host_to_target(shost);

	if (target->target_can_queue)
		starget->can_queue = target->target_can_queue;
	return 0;
}

static int srp_sdev_configure(struct scsi_device *sdev,
			      struct queue_limits *lim)
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

static ssize_t id_ext_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "0x%016llx\n", be64_to_cpu(target->id_ext));
}

static DEVICE_ATTR_RO(id_ext);

static ssize_t ioc_guid_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "0x%016llx\n", be64_to_cpu(target->ioc_guid));
}

static DEVICE_ATTR_RO(ioc_guid);

static ssize_t service_id_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->using_rdma_cm)
		return -ENOENT;
	return sysfs_emit(buf, "0x%016llx\n",
			  be64_to_cpu(target->ib_cm.service_id));
}

static DEVICE_ATTR_RO(service_id);

static ssize_t pkey_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->using_rdma_cm)
		return -ENOENT;

	return sysfs_emit(buf, "0x%04x\n", be16_to_cpu(target->ib_cm.pkey));
}

static DEVICE_ATTR_RO(pkey);

static ssize_t sgid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%pI6\n", target->sgid.raw);
}

static DEVICE_ATTR_RO(sgid);

static ssize_t dgid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));
	struct srp_rdma_ch *ch = &target->ch[0];

	if (target->using_rdma_cm)
		return -ENOENT;

	return sysfs_emit(buf, "%pI6\n", ch->ib_cm.path.dgid.raw);
}

static DEVICE_ATTR_RO(dgid);

static ssize_t orig_dgid_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	if (target->using_rdma_cm)
		return -ENOENT;

	return sysfs_emit(buf, "%pI6\n", target->ib_cm.orig_dgid.raw);
}

static DEVICE_ATTR_RO(orig_dgid);

static ssize_t req_lim_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));
	struct srp_rdma_ch *ch;
	int i, req_lim = INT_MAX;

	for (i = 0; i < target->ch_count; i++) {
		ch = &target->ch[i];
		req_lim = min(req_lim, ch->req_lim);
	}

	return sysfs_emit(buf, "%d\n", req_lim);
}

static DEVICE_ATTR_RO(req_lim);

static ssize_t zero_req_lim_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%d\n", target->zero_req_lim);
}

static DEVICE_ATTR_RO(zero_req_lim);

static ssize_t local_ib_port_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%u\n", target->srp_host->port);
}

static DEVICE_ATTR_RO(local_ib_port);

static ssize_t local_ib_device_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%s\n",
			  dev_name(&target->srp_host->srp_dev->dev->dev));
}

static DEVICE_ATTR_RO(local_ib_device);

static ssize_t ch_count_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%d\n", target->ch_count);
}

static DEVICE_ATTR_RO(ch_count);

static ssize_t comp_vector_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%d\n", target->comp_vector);
}

static DEVICE_ATTR_RO(comp_vector);

static ssize_t tl_retry_count_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%d\n", target->tl_retry_count);
}

static DEVICE_ATTR_RO(tl_retry_count);

static ssize_t cmd_sg_entries_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%u\n", target->cmd_sg_cnt);
}

static DEVICE_ATTR_RO(cmd_sg_entries);

static ssize_t allow_ext_sg_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct srp_target_port *target = host_to_target(class_to_shost(dev));

	return sysfs_emit(buf, "%s\n", target->allow_ext_sg ? "true" : "false");
}

static DEVICE_ATTR_RO(allow_ext_sg);

static struct attribute *srp_host_attrs[] = {
	&dev_attr_id_ext.attr,
	&dev_attr_ioc_guid.attr,
	&dev_attr_service_id.attr,
	&dev_attr_pkey.attr,
	&dev_attr_sgid.attr,
	&dev_attr_dgid.attr,
	&dev_attr_orig_dgid.attr,
	&dev_attr_req_lim.attr,
	&dev_attr_zero_req_lim.attr,
	&dev_attr_local_ib_port.attr,
	&dev_attr_local_ib_device.attr,
	&dev_attr_ch_count.attr,
	&dev_attr_comp_vector.attr,
	&dev_attr_tl_retry_count.attr,
	&dev_attr_cmd_sg_entries.attr,
	&dev_attr_allow_ext_sg.attr,
	NULL
};

ATTRIBUTE_GROUPS(srp_host);

static const struct scsi_host_template srp_template = {
	.module				= THIS_MODULE,
	.name				= "InfiniBand SRP initiator",
	.proc_name			= DRV_NAME,
	.target_alloc			= srp_target_alloc,
	.sdev_configure			= srp_sdev_configure,
	.info				= srp_target_info,
	.init_cmd_priv			= srp_init_cmd_priv,
	.exit_cmd_priv			= srp_exit_cmd_priv,
	.queuecommand			= srp_queuecommand,
	.change_queue_depth             = srp_change_queue_depth,
	.eh_timed_out			= srp_timed_out,
	.eh_abort_handler		= srp_abort,
	.eh_device_reset_handler	= srp_reset_device,
	.eh_host_reset_handler		= srp_reset_host,
	.skip_settle_delay		= true,
	.sg_tablesize			= SRP_DEF_SG_TABLESIZE,
	.can_queue			= SRP_DEFAULT_CMD_SQ_SIZE,
	.this_id			= -1,
	.cmd_per_lun			= SRP_DEFAULT_CMD_SQ_SIZE,
	.shost_groups			= srp_host_groups,
	.track_queue_depth		= 1,
	.cmd_size			= sizeof(struct srp_request),
};

static int srp_sdev_count(struct Scsi_Host *host)
{
	struct scsi_device *sdev;
	int c = 0;

	shost_for_each_device(sdev, host)
		c++;

	return c;
}

/*
 * Return values:
 * < 0 upon failure. Caller is responsible for SRP target port cleanup.
 * 0 and target->state == SRP_TARGET_REMOVED if asynchronous target port
 *    removal has been scheduled.
 * 0 and target->state != SRP_TARGET_REMOVED upon success.
 */
static int srp_add_target(struct srp_host *host, struct srp_target_port *target)
{
	struct srp_rport_identifiers ids;
	struct srp_rport *rport;

	target->state = SRP_TARGET_SCANNING;
	sprintf(target->target_name, "SRP.T10:%016llX",
		be64_to_cpu(target->id_ext));

	if (scsi_add_host(target->scsi_host, host->srp_dev->dev->dev.parent))
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

	scsi_scan_target(&target->scsi_host->shost_gendev,
			 0, target->scsi_id, SCAN_WILD_CARD, SCSI_SCAN_INITIAL);

	if (srp_connected_ch(target) < target->ch_count ||
	    target->qp_in_error) {
		shost_printk(KERN_INFO, target->scsi_host,
			     PFX "SCSI scan failed - removing SCSI host\n");
		srp_queue_remove_work(target);
		goto out;
	}

	pr_debug("%s: SCSI scan succeeded - detected %d LUNs\n",
		 dev_name(&target->scsi_host->shost_gendev),
		 srp_sdev_count(target->scsi_host));

	spin_lock_irq(&target->lock);
	if (target->state == SRP_TARGET_SCANNING)
		target->state = SRP_TARGET_LIVE;
	spin_unlock_irq(&target->lock);

out:
	return 0;
}

static void srp_release_dev(struct device *dev)
{
	struct srp_host *host =
		container_of(dev, struct srp_host, dev);

	kfree(host);
}

static struct attribute *srp_class_attrs[];

ATTRIBUTE_GROUPS(srp_class);

static struct class srp_class = {
	.name    = "infiniband_srp",
	.dev_groups = srp_class_groups,
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
 * or
 *     id_ext=<SRP ID ext>,ioc_guid=<SRP IOC GUID>,
 *     [src=<IPv4 address>,]dest=<IPv4 address>:<port number>
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
	SRP_OPT_IP_SRC		= 1 << 15,
	SRP_OPT_IP_DEST		= 1 << 16,
	SRP_OPT_TARGET_CAN_QUEUE= 1 << 17,
	SRP_OPT_MAX_IT_IU_SIZE  = 1 << 18,
	SRP_OPT_CH_COUNT	= 1 << 19,
};

static unsigned int srp_opt_mandatory[] = {
	SRP_OPT_ID_EXT		|
	SRP_OPT_IOC_GUID	|
	SRP_OPT_DGID		|
	SRP_OPT_PKEY		|
	SRP_OPT_SERVICE_ID,
	SRP_OPT_ID_EXT		|
	SRP_OPT_IOC_GUID	|
	SRP_OPT_IP_DEST,
};

static const match_table_t srp_opt_tokens = {
	{ SRP_OPT_ID_EXT,		"id_ext=%s" 		},
	{ SRP_OPT_IOC_GUID,		"ioc_guid=%s" 		},
	{ SRP_OPT_DGID,			"dgid=%s" 		},
	{ SRP_OPT_PKEY,			"pkey=%x" 		},
	{ SRP_OPT_SERVICE_ID,		"service_id=%s"		},
	{ SRP_OPT_MAX_SECT,		"max_sect=%d" 		},
	{ SRP_OPT_MAX_CMD_PER_LUN,	"max_cmd_per_lun=%d" 	},
	{ SRP_OPT_TARGET_CAN_QUEUE,	"target_can_queue=%d"	},
	{ SRP_OPT_IO_CLASS,		"io_class=%x"		},
	{ SRP_OPT_INITIATOR_EXT,	"initiator_ext=%s"	},
	{ SRP_OPT_CMD_SG_ENTRIES,	"cmd_sg_entries=%u"	},
	{ SRP_OPT_ALLOW_EXT_SG,		"allow_ext_sg=%u"	},
	{ SRP_OPT_SG_TABLESIZE,		"sg_tablesize=%u"	},
	{ SRP_OPT_COMP_VECTOR,		"comp_vector=%u"	},
	{ SRP_OPT_TL_RETRY_COUNT,	"tl_retry_count=%u"	},
	{ SRP_OPT_QUEUE_SIZE,		"queue_size=%d"		},
	{ SRP_OPT_IP_SRC,		"src=%s"		},
	{ SRP_OPT_IP_DEST,		"dest=%s"		},
	{ SRP_OPT_MAX_IT_IU_SIZE,	"max_it_iu_size=%d"	},
	{ SRP_OPT_CH_COUNT,		"ch_count=%u",		},
	{ SRP_OPT_ERR,			NULL 			}
};

/**
 * srp_parse_in - parse an IP address and port number combination
 * @net:	   [in]  Network namespace.
 * @sa:		   [out] Address family, IP address and port number.
 * @addr_port_str: [in]  IP address and port number.
 * @has_port:	   [out] Whether or not @addr_port_str includes a port number.
 *
 * Parse the following address formats:
 * - IPv4: <ip_address>:<port>, e.g. 1.2.3.4:5.
 * - IPv6: \[<ipv6_address>\]:<port>, e.g. [1::2:3%4]:5.
 */
static int srp_parse_in(struct net *net, struct sockaddr_storage *sa,
			const char *addr_port_str, bool *has_port)
{
	char *addr_end, *addr = kstrdup(addr_port_str, GFP_KERNEL);
	char *port_str;
	int ret;

	if (!addr)
		return -ENOMEM;
	port_str = strrchr(addr, ':');
	if (port_str && strchr(port_str, ']'))
		port_str = NULL;
	if (port_str)
		*port_str++ = '\0';
	if (has_port)
		*has_port = port_str != NULL;
	ret = inet_pton_with_scope(net, AF_INET, addr, port_str, sa);
	if (ret && addr[0]) {
		addr_end = addr + strlen(addr) - 1;
		if (addr[0] == '[' && *addr_end == ']') {
			*addr_end = '\0';
			ret = inet_pton_with_scope(net, AF_INET6, addr + 1,
						   port_str, sa);
		}
	}
	kfree(addr);
	pr_debug("%s -> %pISpfsc\n", addr_port_str, sa);
	return ret;
}

static int srp_parse_options(struct net *net, const char *buf,
			     struct srp_target_port *target)
{
	char *options, *sep_opt;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	unsigned long long ull;
	bool has_port;
	int opt_mask = 0;
	int token;
	int ret = -EINVAL;
	int i;

	options = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	sep_opt = options;
	while ((p = strsep(&sep_opt, ",\n")) != NULL) {
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
			ret = kstrtoull(p, 16, &ull);
			if (ret) {
				pr_warn("invalid id_ext parameter '%s'\n", p);
				kfree(p);
				goto out;
			}
			target->id_ext = cpu_to_be64(ull);
			kfree(p);
			break;

		case SRP_OPT_IOC_GUID:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			ret = kstrtoull(p, 16, &ull);
			if (ret) {
				pr_warn("invalid ioc_guid parameter '%s'\n", p);
				kfree(p);
				goto out;
			}
			target->ioc_guid = cpu_to_be64(ull);
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

			ret = hex2bin(target->ib_cm.orig_dgid.raw, p, 16);
			kfree(p);
			if (ret < 0)
				goto out;
			break;

		case SRP_OPT_PKEY:
			ret = match_hex(args, &token);
			if (ret) {
				pr_warn("bad P_Key parameter '%s'\n", p);
				goto out;
			}
			target->ib_cm.pkey = cpu_to_be16(token);
			break;

		case SRP_OPT_SERVICE_ID:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			ret = kstrtoull(p, 16, &ull);
			if (ret) {
				pr_warn("bad service_id parameter '%s'\n", p);
				kfree(p);
				goto out;
			}
			target->ib_cm.service_id = cpu_to_be64(ull);
			kfree(p);
			break;

		case SRP_OPT_IP_SRC:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			ret = srp_parse_in(net, &target->rdma_cm.src.ss, p,
					   NULL);
			if (ret < 0) {
				pr_warn("bad source parameter '%s'\n", p);
				kfree(p);
				goto out;
			}
			target->rdma_cm.src_specified = true;
			kfree(p);
			break;

		case SRP_OPT_IP_DEST:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			ret = srp_parse_in(net, &target->rdma_cm.dst.ss, p,
					   &has_port);
			if (!has_port)
				ret = -EINVAL;
			if (ret < 0) {
				pr_warn("bad dest parameter '%s'\n", p);
				kfree(p);
				goto out;
			}
			target->using_rdma_cm = true;
			kfree(p);
			break;

		case SRP_OPT_MAX_SECT:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("bad max sect parameter '%s'\n", p);
				goto out;
			}
			target->scsi_host->max_sectors = token;
			break;

		case SRP_OPT_QUEUE_SIZE:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for queue_size parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 1) {
				pr_warn("bad queue_size parameter '%s'\n", p);
				ret = -EINVAL;
				goto out;
			}
			target->scsi_host->can_queue = token;
			target->queue_size = token + SRP_RSP_SQ_SIZE +
					     SRP_TSK_MGMT_SQ_SIZE;
			if (!(opt_mask & SRP_OPT_MAX_CMD_PER_LUN))
				target->scsi_host->cmd_per_lun = token;
			break;

		case SRP_OPT_MAX_CMD_PER_LUN:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for max cmd_per_lun parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 1) {
				pr_warn("bad max cmd_per_lun parameter '%s'\n",
					p);
				ret = -EINVAL;
				goto out;
			}
			target->scsi_host->cmd_per_lun = token;
			break;

		case SRP_OPT_TARGET_CAN_QUEUE:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for max target_can_queue parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 1) {
				pr_warn("bad max target_can_queue parameter '%s'\n",
					p);
				ret = -EINVAL;
				goto out;
			}
			target->target_can_queue = token;
			break;

		case SRP_OPT_IO_CLASS:
			ret = match_hex(args, &token);
			if (ret) {
				pr_warn("bad IO class parameter '%s'\n", p);
				goto out;
			}
			if (token != SRP_REV10_IB_IO_CLASS &&
			    token != SRP_REV16A_IB_IO_CLASS) {
				pr_warn("unknown IO class parameter value %x specified (use %x or %x).\n",
					token, SRP_REV10_IB_IO_CLASS,
					SRP_REV16A_IB_IO_CLASS);
				ret = -EINVAL;
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
			ret = kstrtoull(p, 16, &ull);
			if (ret) {
				pr_warn("bad initiator_ext value '%s'\n", p);
				kfree(p);
				goto out;
			}
			target->initiator_ext = cpu_to_be64(ull);
			kfree(p);
			break;

		case SRP_OPT_CMD_SG_ENTRIES:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for max cmd_sg_entries parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 1 || token > 255) {
				pr_warn("bad max cmd_sg_entries parameter '%s'\n",
					p);
				ret = -EINVAL;
				goto out;
			}
			target->cmd_sg_cnt = token;
			break;

		case SRP_OPT_ALLOW_EXT_SG:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("bad allow_ext_sg parameter '%s'\n", p);
				goto out;
			}
			target->allow_ext_sg = !!token;
			break;

		case SRP_OPT_SG_TABLESIZE:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for max sg_tablesize parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 1 || token > SG_MAX_SEGMENTS) {
				pr_warn("bad max sg_tablesize parameter '%s'\n",
					p);
				ret = -EINVAL;
				goto out;
			}
			target->sg_tablesize = token;
			break;

		case SRP_OPT_COMP_VECTOR:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for comp_vector parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 0) {
				pr_warn("bad comp_vector parameter '%s'\n", p);
				ret = -EINVAL;
				goto out;
			}
			target->comp_vector = token;
			break;

		case SRP_OPT_TL_RETRY_COUNT:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for tl_retry_count parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 2 || token > 7) {
				pr_warn("bad tl_retry_count parameter '%s' (must be a number between 2 and 7)\n",
					p);
				ret = -EINVAL;
				goto out;
			}
			target->tl_retry_count = token;
			break;

		case SRP_OPT_MAX_IT_IU_SIZE:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for max it_iu_size parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 0) {
				pr_warn("bad maximum initiator to target IU size '%s'\n", p);
				ret = -EINVAL;
				goto out;
			}
			target->max_it_iu_size = token;
			break;

		case SRP_OPT_CH_COUNT:
			ret = match_int(args, &token);
			if (ret) {
				pr_warn("match_int() failed for channel count parameter '%s', Error %d\n",
					p, ret);
				goto out;
			}
			if (token < 1) {
				pr_warn("bad channel count %s\n", p);
				ret = -EINVAL;
				goto out;
			}
			target->ch_count = token;
			break;

		default:
			pr_warn("unknown parameter or missing value '%s' in target creation request\n",
				p);
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0; i < ARRAY_SIZE(srp_opt_mandatory); i++) {
		if ((opt_mask & srp_opt_mandatory[i]) == srp_opt_mandatory[i]) {
			ret = 0;
			break;
		}
	}
	if (ret)
		pr_warn("target creation request is missing one or more parameters\n");

	if (target->scsi_host->cmd_per_lun > target->scsi_host->can_queue
	    && (opt_mask & SRP_OPT_MAX_CMD_PER_LUN))
		pr_warn("cmd_per_lun = %d > queue_size = %d\n",
			target->scsi_host->cmd_per_lun,
			target->scsi_host->can_queue);

out:
	kfree(options);
	return ret;
}

static ssize_t add_target_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct srp_host *host =
		container_of(dev, struct srp_host, dev);
	struct Scsi_Host *target_host;
	struct srp_target_port *target;
	struct srp_rdma_ch *ch;
	struct srp_device *srp_dev = host->srp_dev;
	struct ib_device *ibdev = srp_dev->dev;
	int ret, i, ch_idx;
	unsigned int max_sectors_per_mr, mr_per_cmd = 0;
	bool multich = false;
	uint32_t max_iu_len;

	target_host = scsi_host_alloc(&srp_template,
				      sizeof (struct srp_target_port));
	if (!target_host)
		return -ENOMEM;

	target_host->transportt  = ib_srp_transport_template;
	target_host->max_channel = 0;
	target_host->max_id      = 1;
	target_host->max_lun     = -1LL;
	target_host->max_cmd_len = sizeof ((struct srp_cmd *) (void *) 0L)->cdb;

	if (ibdev->attrs.kernel_cap_flags & IBK_SG_GAPS_REG)
		target_host->max_segment_size = ib_dma_max_seg_size(ibdev);
	else
		target_host->virt_boundary_mask = ~srp_dev->mr_page_mask;

	target = host_to_target(target_host);

	target->net		= kobj_ns_grab_current(KOBJ_NS_TYPE_NET);
	target->io_class	= SRP_REV16A_IB_IO_CLASS;
	target->scsi_host	= target_host;
	target->srp_host	= host;
	target->lkey		= host->srp_dev->pd->local_dma_lkey;
	target->global_rkey	= host->srp_dev->global_rkey;
	target->cmd_sg_cnt	= cmd_sg_entries;
	target->sg_tablesize	= indirect_sg_entries ? : cmd_sg_entries;
	target->allow_ext_sg	= allow_ext_sg;
	target->tl_retry_count	= 7;
	target->queue_size	= SRP_DEFAULT_QUEUE_SIZE;

	/*
	 * Avoid that the SCSI host can be removed by srp_remove_target()
	 * before this function returns.
	 */
	scsi_host_get(target->scsi_host);

	ret = mutex_lock_interruptible(&host->add_target_mutex);
	if (ret < 0)
		goto put;

	ret = srp_parse_options(target->net, buf, target);
	if (ret)
		goto out;

	if (!srp_conn_unique(target->srp_host, target)) {
		if (target->using_rdma_cm) {
			shost_printk(KERN_INFO, target->scsi_host,
				     PFX "Already connected to target port with id_ext=%016llx;ioc_guid=%016llx;dest=%pIS\n",
				     be64_to_cpu(target->id_ext),
				     be64_to_cpu(target->ioc_guid),
				     &target->rdma_cm.dst);
		} else {
			shost_printk(KERN_INFO, target->scsi_host,
				     PFX "Already connected to target port with id_ext=%016llx;ioc_guid=%016llx;initiator_ext=%016llx\n",
				     be64_to_cpu(target->id_ext),
				     be64_to_cpu(target->ioc_guid),
				     be64_to_cpu(target->initiator_ext));
		}
		ret = -EEXIST;
		goto out;
	}

	if (!srp_dev->has_fr && !target->allow_ext_sg &&
	    target->cmd_sg_cnt < target->sg_tablesize) {
		pr_warn("No MR pool and no external indirect descriptors, limiting sg_tablesize to cmd_sg_cnt\n");
		target->sg_tablesize = target->cmd_sg_cnt;
	}

	if (srp_dev->use_fast_reg) {
		bool gaps_reg = ibdev->attrs.kernel_cap_flags &
				 IBK_SG_GAPS_REG;

		max_sectors_per_mr = srp_dev->max_pages_per_mr <<
				  (ilog2(srp_dev->mr_page_size) - 9);
		if (!gaps_reg) {
			/*
			 * FR can only map one HCA page per entry. If the start
			 * address is not aligned on a HCA page boundary two
			 * entries will be used for the head and the tail
			 * although these two entries combined contain at most
			 * one HCA page of data. Hence the "+ 1" in the
			 * calculation below.
			 *
			 * The indirect data buffer descriptor is contiguous
			 * so the memory for that buffer will only be
			 * registered if register_always is true. Hence add
			 * one to mr_per_cmd if register_always has been set.
			 */
			mr_per_cmd = register_always +
				(target->scsi_host->max_sectors + 1 +
				 max_sectors_per_mr - 1) / max_sectors_per_mr;
		} else {
			mr_per_cmd = register_always +
				(target->sg_tablesize +
				 srp_dev->max_pages_per_mr - 1) /
				srp_dev->max_pages_per_mr;
		}
		pr_debug("max_sectors = %u; max_pages_per_mr = %u; mr_page_size = %u; max_sectors_per_mr = %u; mr_per_cmd = %u\n",
			 target->scsi_host->max_sectors, srp_dev->max_pages_per_mr, srp_dev->mr_page_size,
			 max_sectors_per_mr, mr_per_cmd);
	}

	target_host->sg_tablesize = target->sg_tablesize;
	target->mr_pool_size = target->scsi_host->can_queue * mr_per_cmd;
	target->mr_per_cmd = mr_per_cmd;
	target->indirect_size = target->sg_tablesize *
				sizeof (struct srp_direct_buf);
	max_iu_len = srp_max_it_iu_len(target->cmd_sg_cnt,
				       srp_use_imm_data,
				       target->max_it_iu_size);

	INIT_WORK(&target->tl_err_work, srp_tl_err_work);
	INIT_WORK(&target->remove_work, srp_remove_work);
	spin_lock_init(&target->lock);
	ret = rdma_query_gid(ibdev, host->port, 0, &target->sgid);
	if (ret)
		goto out;

	ret = -ENOMEM;
	if (target->ch_count == 0) {
		target->ch_count =
			min(ch_count ?:
				max(4 * num_online_nodes(),
				    ibdev->num_comp_vectors),
				num_online_cpus());
	}

	target->ch = kcalloc(target->ch_count, sizeof(*target->ch),
			     GFP_KERNEL);
	if (!target->ch)
		goto out;

	for (ch_idx = 0; ch_idx < target->ch_count; ++ch_idx) {
		ch = &target->ch[ch_idx];
		ch->target = target;
		ch->comp_vector = ch_idx % ibdev->num_comp_vectors;
		spin_lock_init(&ch->lock);
		INIT_LIST_HEAD(&ch->free_tx);
		ret = srp_new_cm_id(ch);
		if (ret)
			goto err_disconnect;

		ret = srp_create_ch_ib(ch);
		if (ret)
			goto err_disconnect;

		ret = srp_connect_ch(ch, max_iu_len, multich);
		if (ret) {
			char dst[64];

			if (target->using_rdma_cm)
				snprintf(dst, sizeof(dst), "%pIS",
					&target->rdma_cm.dst);
			else
				snprintf(dst, sizeof(dst), "%pI6",
					target->ib_cm.orig_dgid.raw);
			shost_printk(KERN_ERR, target->scsi_host,
				PFX "Connection %d/%d to %s failed\n",
				ch_idx,
				target->ch_count, dst);
			if (ch_idx == 0) {
				goto free_ch;
			} else {
				srp_free_ch_ib(target, ch);
				target->ch_count = ch - target->ch;
				goto connected;
			}
		}
		multich = true;
	}

connected:
	target->scsi_host->nr_hw_queues = target->ch_count;

	ret = srp_add_target(host, target);
	if (ret)
		goto err_disconnect;

	if (target->state != SRP_TARGET_REMOVED) {
		if (target->using_rdma_cm) {
			shost_printk(KERN_DEBUG, target->scsi_host, PFX
				     "new target: id_ext %016llx ioc_guid %016llx sgid %pI6 dest %pIS\n",
				     be64_to_cpu(target->id_ext),
				     be64_to_cpu(target->ioc_guid),
				     target->sgid.raw, &target->rdma_cm.dst);
		} else {
			shost_printk(KERN_DEBUG, target->scsi_host, PFX
				     "new target: id_ext %016llx ioc_guid %016llx pkey %04x service_id %016llx sgid %pI6 dgid %pI6\n",
				     be64_to_cpu(target->id_ext),
				     be64_to_cpu(target->ioc_guid),
				     be16_to_cpu(target->ib_cm.pkey),
				     be64_to_cpu(target->ib_cm.service_id),
				     target->sgid.raw,
				     target->ib_cm.orig_dgid.raw);
		}
	}

	ret = count;

out:
	mutex_unlock(&host->add_target_mutex);

put:
	scsi_host_put(target->scsi_host);
	if (ret < 0) {
		/*
		 * If a call to srp_remove_target() has not been scheduled,
		 * drop the network namespace reference now that was obtained
		 * earlier in this function.
		 */
		if (target->state != SRP_TARGET_REMOVED)
			kobj_ns_drop(KOBJ_NS_TYPE_NET, target->net);
		scsi_host_put(target->scsi_host);
	}

	return ret;

err_disconnect:
	srp_disconnect_target(target);

free_ch:
	for (i = 0; i < target->ch_count; i++) {
		ch = &target->ch[i];
		srp_free_ch_ib(target, ch);
	}

	kfree(target->ch);
	goto out;
}

static DEVICE_ATTR_WO(add_target);

static ssize_t ibdev_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct srp_host *host = container_of(dev, struct srp_host, dev);

	return sysfs_emit(buf, "%s\n", dev_name(&host->srp_dev->dev->dev));
}

static DEVICE_ATTR_RO(ibdev);

static ssize_t port_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct srp_host *host = container_of(dev, struct srp_host, dev);

	return sysfs_emit(buf, "%u\n", host->port);
}

static DEVICE_ATTR_RO(port);

static struct attribute *srp_class_attrs[] = {
	&dev_attr_add_target.attr,
	&dev_attr_ibdev.attr,
	&dev_attr_port.attr,
	NULL
};

static struct srp_host *srp_add_port(struct srp_device *device, u32 port)
{
	struct srp_host *host;

	host = kzalloc(sizeof *host, GFP_KERNEL);
	if (!host)
		return NULL;

	INIT_LIST_HEAD(&host->target_list);
	spin_lock_init(&host->target_lock);
	mutex_init(&host->add_target_mutex);
	host->srp_dev = device;
	host->port = port;

	device_initialize(&host->dev);
	host->dev.class = &srp_class;
	host->dev.parent = device->dev->dev.parent;
	if (dev_set_name(&host->dev, "srp-%s-%u", dev_name(&device->dev->dev),
			 port))
		goto put_host;
	if (device_add(&host->dev))
		goto put_host;

	return host;

put_host:
	put_device(&host->dev);
	return NULL;
}

static void srp_rename_dev(struct ib_device *device, void *client_data)
{
	struct srp_device *srp_dev = client_data;
	struct srp_host *host, *tmp_host;

	list_for_each_entry_safe(host, tmp_host, &srp_dev->dev_list, list) {
		char name[IB_DEVICE_NAME_MAX + 8];

		snprintf(name, sizeof(name), "srp-%s-%u",
			 dev_name(&device->dev), host->port);
		device_rename(&host->dev, name);
	}
}

static int srp_add_one(struct ib_device *device)
{
	struct srp_device *srp_dev;
	struct ib_device_attr *attr = &device->attrs;
	struct srp_host *host;
	int mr_page_shift;
	u32 p;
	u64 max_pages_per_mr;
	unsigned int flags = 0;

	srp_dev = kzalloc(sizeof(*srp_dev), GFP_KERNEL);
	if (!srp_dev)
		return -ENOMEM;

	/*
	 * Use the smallest page size supported by the HCA, down to a
	 * minimum of 4096 bytes. We're unlikely to build large sglists
	 * out of smaller entries.
	 */
	mr_page_shift		= max(12, ffs(attr->page_size_cap) - 1);
	srp_dev->mr_page_size	= 1 << mr_page_shift;
	srp_dev->mr_page_mask	= ~((u64) srp_dev->mr_page_size - 1);
	max_pages_per_mr	= attr->max_mr_size;
	do_div(max_pages_per_mr, srp_dev->mr_page_size);
	pr_debug("%s: %llu / %u = %llu <> %u\n", __func__,
		 attr->max_mr_size, srp_dev->mr_page_size,
		 max_pages_per_mr, SRP_MAX_PAGES_PER_MR);
	srp_dev->max_pages_per_mr = min_t(u64, SRP_MAX_PAGES_PER_MR,
					  max_pages_per_mr);

	srp_dev->has_fr = (attr->device_cap_flags &
			   IB_DEVICE_MEM_MGT_EXTENSIONS);
	if (!never_register && !srp_dev->has_fr)
		dev_warn(&device->dev, "FR is not supported\n");
	else if (!never_register &&
		 attr->max_mr_size >= 2 * srp_dev->mr_page_size)
		srp_dev->use_fast_reg = srp_dev->has_fr;

	if (never_register || !register_always || !srp_dev->has_fr)
		flags |= IB_PD_UNSAFE_GLOBAL_RKEY;

	if (srp_dev->use_fast_reg) {
		srp_dev->max_pages_per_mr =
			min_t(u32, srp_dev->max_pages_per_mr,
			      attr->max_fast_reg_page_list_len);
	}
	srp_dev->mr_max_size	= srp_dev->mr_page_size *
				   srp_dev->max_pages_per_mr;
	pr_debug("%s: mr_page_shift = %d, device->max_mr_size = %#llx, device->max_fast_reg_page_list_len = %u, max_pages_per_mr = %d, mr_max_size = %#x\n",
		 dev_name(&device->dev), mr_page_shift, attr->max_mr_size,
		 attr->max_fast_reg_page_list_len,
		 srp_dev->max_pages_per_mr, srp_dev->mr_max_size);

	INIT_LIST_HEAD(&srp_dev->dev_list);

	srp_dev->dev = device;
	srp_dev->pd  = ib_alloc_pd(device, flags);
	if (IS_ERR(srp_dev->pd)) {
		int ret = PTR_ERR(srp_dev->pd);

		kfree(srp_dev);
		return ret;
	}

	if (flags & IB_PD_UNSAFE_GLOBAL_RKEY) {
		srp_dev->global_rkey = srp_dev->pd->unsafe_global_rkey;
		WARN_ON_ONCE(srp_dev->global_rkey == 0);
	}

	rdma_for_each_port (device, p) {
		host = srp_add_port(srp_dev, p);
		if (host)
			list_add_tail(&host->list, &srp_dev->dev_list);
	}

	ib_set_client_data(device, &srp_client, srp_dev);
	return 0;
}

static void srp_remove_one(struct ib_device *device, void *client_data)
{
	struct srp_device *srp_dev;
	struct srp_host *host, *tmp_host;
	struct srp_target_port *target;

	srp_dev = client_data;

	list_for_each_entry_safe(host, tmp_host, &srp_dev->dev_list, list) {
		/*
		 * Remove the add_target sysfs entry so that no new target ports
		 * can be created.
		 */
		device_del(&host->dev);

		/*
		 * Remove all target ports.
		 */
		spin_lock(&host->target_lock);
		list_for_each_entry(target, &host->target_list, list)
			srp_queue_remove_work(target);
		spin_unlock(&host->target_lock);

		/*
		 * srp_queue_remove_work() queues a call to
		 * srp_remove_target(). The latter function cancels
		 * target->tl_err_work so waiting for the remove works to
		 * finish is sufficient.
		 */
		flush_workqueue(srp_remove_wq);

		put_device(&host->dev);
	}

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

	BUILD_BUG_ON(sizeof(struct srp_aer_req) != 36);
	BUILD_BUG_ON(sizeof(struct srp_cmd) != 48);
	BUILD_BUG_ON(sizeof(struct srp_imm_buf) != 4);
	BUILD_BUG_ON(sizeof(struct srp_indirect_buf) != 20);
	BUILD_BUG_ON(sizeof(struct srp_login_req) != 64);
	BUILD_BUG_ON(sizeof(struct srp_login_req_rdma) != 56);
	BUILD_BUG_ON(sizeof(struct srp_rsp) != 36);

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

	if (indirect_sg_entries > SG_MAX_SEGMENTS) {
		pr_warn("Clamping indirect_sg_entries to %u\n",
			SG_MAX_SEGMENTS);
		indirect_sg_entries = SG_MAX_SEGMENTS;
	}

	srp_remove_wq = create_workqueue("srp_remove");
	if (!srp_remove_wq) {
		ret = -ENOMEM;
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
