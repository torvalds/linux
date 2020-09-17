/*
 * NVMe over Fabrics RDMA host code.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <rdma/mr_pool.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/atomic.h>
#include <linux/blk-mq.h>
#include <linux/blk-mq-rdma.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/nvme.h>
#include <asm/unaligned.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <linux/nvme-rdma.h>

#include "nvme.h"
#include "fabrics.h"


#define NVME_RDMA_CONNECT_TIMEOUT_MS	3000		/* 3 second */

#define NVME_RDMA_MAX_SEGMENTS		256

#define NVME_RDMA_MAX_INLINE_SEGMENTS	4

struct nvme_rdma_device {
	struct ib_device	*dev;
	struct ib_pd		*pd;
	struct kref		ref;
	struct list_head	entry;
	unsigned int		num_inline_segments;
};

struct nvme_rdma_qe {
	struct ib_cqe		cqe;
	void			*data;
	u64			dma;
};

struct nvme_rdma_queue;
struct nvme_rdma_request {
	struct nvme_request	req;
	struct ib_mr		*mr;
	struct nvme_rdma_qe	sqe;
	union nvme_result	result;
	__le16			status;
	refcount_t		ref;
	struct ib_sge		sge[1 + NVME_RDMA_MAX_INLINE_SEGMENTS];
	u32			num_sge;
	int			nents;
	struct ib_reg_wr	reg_wr;
	struct ib_cqe		reg_cqe;
	struct nvme_rdma_queue  *queue;
	struct sg_table		sg_table;
	struct scatterlist	first_sgl[];
};

enum nvme_rdma_queue_flags {
	NVME_RDMA_Q_ALLOCATED		= 0,
	NVME_RDMA_Q_LIVE		= 1,
	NVME_RDMA_Q_TR_READY		= 2,
};

struct nvme_rdma_queue {
	struct nvme_rdma_qe	*rsp_ring;
	int			queue_size;
	size_t			cmnd_capsule_len;
	struct nvme_rdma_ctrl	*ctrl;
	struct nvme_rdma_device	*device;
	struct ib_cq		*ib_cq;
	struct ib_qp		*qp;

	unsigned long		flags;
	struct rdma_cm_id	*cm_id;
	int			cm_error;
	struct completion	cm_done;
};

struct nvme_rdma_ctrl {
	/* read only in the hot path */
	struct nvme_rdma_queue	*queues;

	/* other member variables */
	struct blk_mq_tag_set	tag_set;
	struct work_struct	err_work;

	struct nvme_rdma_qe	async_event_sqe;

	struct delayed_work	reconnect_work;

	struct list_head	list;

	struct blk_mq_tag_set	admin_tag_set;
	struct nvme_rdma_device	*device;

	u32			max_fr_pages;

	struct sockaddr_storage addr;
	struct sockaddr_storage src_addr;

	struct nvme_ctrl	ctrl;
	struct mutex		teardown_lock;
	bool			use_inline_data;
};

static inline struct nvme_rdma_ctrl *to_rdma_ctrl(struct nvme_ctrl *ctrl)
{
	return container_of(ctrl, struct nvme_rdma_ctrl, ctrl);
}

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_mutex);

static LIST_HEAD(nvme_rdma_ctrl_list);
static DEFINE_MUTEX(nvme_rdma_ctrl_mutex);

/*
 * Disabling this option makes small I/O goes faster, but is fundamentally
 * unsafe.  With it turned off we will have to register a global rkey that
 * allows read and write access to all physical memory.
 */
static bool register_always = true;
module_param(register_always, bool, 0444);
MODULE_PARM_DESC(register_always,
	 "Use memory registration even for contiguous memory regions");

static int nvme_rdma_cm_handler(struct rdma_cm_id *cm_id,
		struct rdma_cm_event *event);
static void nvme_rdma_recv_done(struct ib_cq *cq, struct ib_wc *wc);

static const struct blk_mq_ops nvme_rdma_mq_ops;
static const struct blk_mq_ops nvme_rdma_admin_mq_ops;

/* XXX: really should move to a generic header sooner or later.. */
static inline void put_unaligned_le24(u32 val, u8 *p)
{
	*p++ = val;
	*p++ = val >> 8;
	*p++ = val >> 16;
}

static inline int nvme_rdma_queue_idx(struct nvme_rdma_queue *queue)
{
	return queue - queue->ctrl->queues;
}

static inline size_t nvme_rdma_inline_data_size(struct nvme_rdma_queue *queue)
{
	return queue->cmnd_capsule_len - sizeof(struct nvme_command);
}

static void nvme_rdma_free_qe(struct ib_device *ibdev, struct nvme_rdma_qe *qe,
		size_t capsule_size, enum dma_data_direction dir)
{
	ib_dma_unmap_single(ibdev, qe->dma, capsule_size, dir);
	kfree(qe->data);
}

static int nvme_rdma_alloc_qe(struct ib_device *ibdev, struct nvme_rdma_qe *qe,
		size_t capsule_size, enum dma_data_direction dir)
{
	qe->data = kzalloc(capsule_size, GFP_KERNEL);
	if (!qe->data)
		return -ENOMEM;

	qe->dma = ib_dma_map_single(ibdev, qe->data, capsule_size, dir);
	if (ib_dma_mapping_error(ibdev, qe->dma)) {
		kfree(qe->data);
		qe->data = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void nvme_rdma_free_ring(struct ib_device *ibdev,
		struct nvme_rdma_qe *ring, size_t ib_queue_size,
		size_t capsule_size, enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < ib_queue_size; i++)
		nvme_rdma_free_qe(ibdev, &ring[i], capsule_size, dir);
	kfree(ring);
}

static struct nvme_rdma_qe *nvme_rdma_alloc_ring(struct ib_device *ibdev,
		size_t ib_queue_size, size_t capsule_size,
		enum dma_data_direction dir)
{
	struct nvme_rdma_qe *ring;
	int i;

	ring = kcalloc(ib_queue_size, sizeof(struct nvme_rdma_qe), GFP_KERNEL);
	if (!ring)
		return NULL;

	for (i = 0; i < ib_queue_size; i++) {
		if (nvme_rdma_alloc_qe(ibdev, &ring[i], capsule_size, dir))
			goto out_free_ring;
	}

	return ring;

out_free_ring:
	nvme_rdma_free_ring(ibdev, ring, i, capsule_size, dir);
	return NULL;
}

static void nvme_rdma_qp_event(struct ib_event *event, void *context)
{
	pr_debug("QP event %s (%d)\n",
		 ib_event_msg(event->event), event->event);

}

static int nvme_rdma_wait_for_cm(struct nvme_rdma_queue *queue)
{
	wait_for_completion_interruptible_timeout(&queue->cm_done,
			msecs_to_jiffies(NVME_RDMA_CONNECT_TIMEOUT_MS) + 1);
	return queue->cm_error;
}

static int nvme_rdma_create_qp(struct nvme_rdma_queue *queue, const int factor)
{
	struct nvme_rdma_device *dev = queue->device;
	struct ib_qp_init_attr init_attr;
	int ret;

	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.event_handler = nvme_rdma_qp_event;
	/* +1 for drain */
	init_attr.cap.max_send_wr = factor * queue->queue_size + 1;
	/* +1 for drain */
	init_attr.cap.max_recv_wr = queue->queue_size + 1;
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_sge = 1 + dev->num_inline_segments;
	init_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	init_attr.qp_type = IB_QPT_RC;
	init_attr.send_cq = queue->ib_cq;
	init_attr.recv_cq = queue->ib_cq;

	ret = rdma_create_qp(queue->cm_id, dev->pd, &init_attr);

	queue->qp = queue->cm_id->qp;
	return ret;
}

static void nvme_rdma_exit_request(struct blk_mq_tag_set *set,
		struct request *rq, unsigned int hctx_idx)
{
	struct nvme_rdma_ctrl *ctrl = set->driver_data;
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	int queue_idx = (set == &ctrl->tag_set) ? hctx_idx + 1 : 0;
	struct nvme_rdma_queue *queue = &ctrl->queues[queue_idx];
	struct nvme_rdma_device *dev = queue->device;

	nvme_rdma_free_qe(dev->dev, &req->sqe, sizeof(struct nvme_command),
			DMA_TO_DEVICE);
}

static int nvme_rdma_init_request(struct blk_mq_tag_set *set,
		struct request *rq, unsigned int hctx_idx,
		unsigned int numa_node)
{
	struct nvme_rdma_ctrl *ctrl = set->driver_data;
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	int queue_idx = (set == &ctrl->tag_set) ? hctx_idx + 1 : 0;
	struct nvme_rdma_queue *queue = &ctrl->queues[queue_idx];
	struct nvme_rdma_device *dev = queue->device;
	struct ib_device *ibdev = dev->dev;
	int ret;

	nvme_req(rq)->ctrl = &ctrl->ctrl;
	ret = nvme_rdma_alloc_qe(ibdev, &req->sqe, sizeof(struct nvme_command),
			DMA_TO_DEVICE);
	if (ret)
		return ret;

	req->queue = queue;

	return 0;
}

static int nvme_rdma_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
		unsigned int hctx_idx)
{
	struct nvme_rdma_ctrl *ctrl = data;
	struct nvme_rdma_queue *queue = &ctrl->queues[hctx_idx + 1];

	BUG_ON(hctx_idx >= ctrl->ctrl.queue_count);

	hctx->driver_data = queue;
	return 0;
}

static int nvme_rdma_init_admin_hctx(struct blk_mq_hw_ctx *hctx, void *data,
		unsigned int hctx_idx)
{
	struct nvme_rdma_ctrl *ctrl = data;
	struct nvme_rdma_queue *queue = &ctrl->queues[0];

	BUG_ON(hctx_idx != 0);

	hctx->driver_data = queue;
	return 0;
}

static void nvme_rdma_free_dev(struct kref *ref)
{
	struct nvme_rdma_device *ndev =
		container_of(ref, struct nvme_rdma_device, ref);

	mutex_lock(&device_list_mutex);
	list_del(&ndev->entry);
	mutex_unlock(&device_list_mutex);

	ib_dealloc_pd(ndev->pd);
	kfree(ndev);
}

static void nvme_rdma_dev_put(struct nvme_rdma_device *dev)
{
	kref_put(&dev->ref, nvme_rdma_free_dev);
}

static int nvme_rdma_dev_get(struct nvme_rdma_device *dev)
{
	return kref_get_unless_zero(&dev->ref);
}

static struct nvme_rdma_device *
nvme_rdma_find_get_device(struct rdma_cm_id *cm_id)
{
	struct nvme_rdma_device *ndev;

	mutex_lock(&device_list_mutex);
	list_for_each_entry(ndev, &device_list, entry) {
		if (ndev->dev->node_guid == cm_id->device->node_guid &&
		    nvme_rdma_dev_get(ndev))
			goto out_unlock;
	}

	ndev = kzalloc(sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		goto out_err;

	ndev->dev = cm_id->device;
	kref_init(&ndev->ref);

	ndev->pd = ib_alloc_pd(ndev->dev,
		register_always ? 0 : IB_PD_UNSAFE_GLOBAL_RKEY);
	if (IS_ERR(ndev->pd))
		goto out_free_dev;

	if (!(ndev->dev->attrs.device_cap_flags &
	      IB_DEVICE_MEM_MGT_EXTENSIONS)) {
		dev_err(&ndev->dev->dev,
			"Memory registrations not supported.\n");
		goto out_free_pd;
	}

	ndev->num_inline_segments = min(NVME_RDMA_MAX_INLINE_SEGMENTS,
					ndev->dev->attrs.max_send_sge - 1);
	list_add(&ndev->entry, &device_list);
out_unlock:
	mutex_unlock(&device_list_mutex);
	return ndev;

out_free_pd:
	ib_dealloc_pd(ndev->pd);
out_free_dev:
	kfree(ndev);
out_err:
	mutex_unlock(&device_list_mutex);
	return NULL;
}

static void nvme_rdma_destroy_queue_ib(struct nvme_rdma_queue *queue)
{
	struct nvme_rdma_device *dev;
	struct ib_device *ibdev;

	if (!test_and_clear_bit(NVME_RDMA_Q_TR_READY, &queue->flags))
		return;

	dev = queue->device;
	ibdev = dev->dev;

	ib_mr_pool_destroy(queue->qp, &queue->qp->rdma_mrs);

	/*
	 * The cm_id object might have been destroyed during RDMA connection
	 * establishment error flow to avoid getting other cma events, thus
	 * the destruction of the QP shouldn't use rdma_cm API.
	 */
	ib_destroy_qp(queue->qp);
	ib_free_cq(queue->ib_cq);

	nvme_rdma_free_ring(ibdev, queue->rsp_ring, queue->queue_size,
			sizeof(struct nvme_completion), DMA_FROM_DEVICE);

	nvme_rdma_dev_put(dev);
}

static int nvme_rdma_get_max_fr_pages(struct ib_device *ibdev)
{
	return min_t(u32, NVME_RDMA_MAX_SEGMENTS,
		     ibdev->attrs.max_fast_reg_page_list_len);
}

static int nvme_rdma_create_queue_ib(struct nvme_rdma_queue *queue)
{
	struct ib_device *ibdev;
	const int send_wr_factor = 3;			/* MR, SEND, INV */
	const int cq_factor = send_wr_factor + 1;	/* + RECV */
	int comp_vector, idx = nvme_rdma_queue_idx(queue);
	int ret;

	queue->device = nvme_rdma_find_get_device(queue->cm_id);
	if (!queue->device) {
		dev_err(queue->cm_id->device->dev.parent,
			"no client data found!\n");
		return -ECONNREFUSED;
	}
	ibdev = queue->device->dev;

	/*
	 * Spread I/O queues completion vectors according their queue index.
	 * Admin queues can always go on completion vector 0.
	 */
	comp_vector = (idx == 0 ? idx : idx - 1) % ibdev->num_comp_vectors;

	/* +1 for ib_stop_cq */
	queue->ib_cq = ib_alloc_cq(ibdev, queue,
				cq_factor * queue->queue_size + 1,
				comp_vector, IB_POLL_SOFTIRQ);
	if (IS_ERR(queue->ib_cq)) {
		ret = PTR_ERR(queue->ib_cq);
		goto out_put_dev;
	}

	ret = nvme_rdma_create_qp(queue, send_wr_factor);
	if (ret)
		goto out_destroy_ib_cq;

	queue->rsp_ring = nvme_rdma_alloc_ring(ibdev, queue->queue_size,
			sizeof(struct nvme_completion), DMA_FROM_DEVICE);
	if (!queue->rsp_ring) {
		ret = -ENOMEM;
		goto out_destroy_qp;
	}

	ret = ib_mr_pool_init(queue->qp, &queue->qp->rdma_mrs,
			      queue->queue_size,
			      IB_MR_TYPE_MEM_REG,
			      nvme_rdma_get_max_fr_pages(ibdev));
	if (ret) {
		dev_err(queue->ctrl->ctrl.device,
			"failed to initialize MR pool sized %d for QID %d\n",
			queue->queue_size, idx);
		goto out_destroy_ring;
	}

	set_bit(NVME_RDMA_Q_TR_READY, &queue->flags);

	return 0;

out_destroy_ring:
	nvme_rdma_free_ring(ibdev, queue->rsp_ring, queue->queue_size,
			    sizeof(struct nvme_completion), DMA_FROM_DEVICE);
out_destroy_qp:
	rdma_destroy_qp(queue->cm_id);
out_destroy_ib_cq:
	ib_free_cq(queue->ib_cq);
out_put_dev:
	nvme_rdma_dev_put(queue->device);
	return ret;
}

static int nvme_rdma_alloc_queue(struct nvme_rdma_ctrl *ctrl,
		int idx, size_t queue_size)
{
	struct nvme_rdma_queue *queue;
	struct sockaddr *src_addr = NULL;
	int ret;

	queue = &ctrl->queues[idx];
	queue->ctrl = ctrl;
	init_completion(&queue->cm_done);

	if (idx > 0)
		queue->cmnd_capsule_len = ctrl->ctrl.ioccsz * 16;
	else
		queue->cmnd_capsule_len = sizeof(struct nvme_command);

	queue->queue_size = queue_size;

	queue->cm_id = rdma_create_id(&init_net, nvme_rdma_cm_handler, queue,
			RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(queue->cm_id)) {
		dev_info(ctrl->ctrl.device,
			"failed to create CM ID: %ld\n", PTR_ERR(queue->cm_id));
		return PTR_ERR(queue->cm_id);
	}

	if (ctrl->ctrl.opts->mask & NVMF_OPT_HOST_TRADDR)
		src_addr = (struct sockaddr *)&ctrl->src_addr;

	queue->cm_error = -ETIMEDOUT;
	ret = rdma_resolve_addr(queue->cm_id, src_addr,
			(struct sockaddr *)&ctrl->addr,
			NVME_RDMA_CONNECT_TIMEOUT_MS);
	if (ret) {
		dev_info(ctrl->ctrl.device,
			"rdma_resolve_addr failed (%d).\n", ret);
		goto out_destroy_cm_id;
	}

	ret = nvme_rdma_wait_for_cm(queue);
	if (ret) {
		dev_info(ctrl->ctrl.device,
			"rdma connection establishment failed (%d)\n", ret);
		goto out_destroy_cm_id;
	}

	set_bit(NVME_RDMA_Q_ALLOCATED, &queue->flags);

	return 0;

out_destroy_cm_id:
	rdma_destroy_id(queue->cm_id);
	nvme_rdma_destroy_queue_ib(queue);
	return ret;
}

static void nvme_rdma_stop_queue(struct nvme_rdma_queue *queue)
{
	if (!test_and_clear_bit(NVME_RDMA_Q_LIVE, &queue->flags))
		return;

	rdma_disconnect(queue->cm_id);
	ib_drain_qp(queue->qp);
}

static void nvme_rdma_free_queue(struct nvme_rdma_queue *queue)
{
	if (!test_and_clear_bit(NVME_RDMA_Q_ALLOCATED, &queue->flags))
		return;

	nvme_rdma_destroy_queue_ib(queue);
	rdma_destroy_id(queue->cm_id);
}

static void nvme_rdma_free_io_queues(struct nvme_rdma_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->ctrl.queue_count; i++)
		nvme_rdma_free_queue(&ctrl->queues[i]);
}

static void nvme_rdma_stop_io_queues(struct nvme_rdma_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->ctrl.queue_count; i++)
		nvme_rdma_stop_queue(&ctrl->queues[i]);
}

static int nvme_rdma_start_queue(struct nvme_rdma_ctrl *ctrl, int idx)
{
	int ret;

	if (idx)
		ret = nvmf_connect_io_queue(&ctrl->ctrl, idx);
	else
		ret = nvmf_connect_admin_queue(&ctrl->ctrl);

	if (!ret)
		set_bit(NVME_RDMA_Q_LIVE, &ctrl->queues[idx].flags);
	else
		dev_info(ctrl->ctrl.device,
			"failed to connect queue: %d ret=%d\n", idx, ret);
	return ret;
}

static int nvme_rdma_start_io_queues(struct nvme_rdma_ctrl *ctrl)
{
	int i, ret = 0;

	for (i = 1; i < ctrl->ctrl.queue_count; i++) {
		ret = nvme_rdma_start_queue(ctrl, i);
		if (ret)
			goto out_stop_queues;
	}

	return 0;

out_stop_queues:
	for (i--; i >= 1; i--)
		nvme_rdma_stop_queue(&ctrl->queues[i]);
	return ret;
}

static int nvme_rdma_alloc_io_queues(struct nvme_rdma_ctrl *ctrl)
{
	struct nvmf_ctrl_options *opts = ctrl->ctrl.opts;
	struct ib_device *ibdev = ctrl->device->dev;
	unsigned int nr_io_queues;
	int i, ret;

	nr_io_queues = min(opts->nr_io_queues, num_online_cpus());

	/*
	 * we map queues according to the device irq vectors for
	 * optimal locality so we don't need more queues than
	 * completion vectors.
	 */
	nr_io_queues = min_t(unsigned int, nr_io_queues,
				ibdev->num_comp_vectors);

	ret = nvme_set_queue_count(&ctrl->ctrl, &nr_io_queues);
	if (ret)
		return ret;

	ctrl->ctrl.queue_count = nr_io_queues + 1;
	if (ctrl->ctrl.queue_count < 2)
		return 0;

	dev_info(ctrl->ctrl.device,
		"creating %d I/O queues.\n", nr_io_queues);

	for (i = 1; i < ctrl->ctrl.queue_count; i++) {
		ret = nvme_rdma_alloc_queue(ctrl, i,
				ctrl->ctrl.sqsize + 1);
		if (ret)
			goto out_free_queues;
	}

	return 0;

out_free_queues:
	for (i--; i >= 1; i--)
		nvme_rdma_free_queue(&ctrl->queues[i]);

	return ret;
}

static void nvme_rdma_free_tagset(struct nvme_ctrl *nctrl,
		struct blk_mq_tag_set *set)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(nctrl);

	blk_mq_free_tag_set(set);
	nvme_rdma_dev_put(ctrl->device);
}

static struct blk_mq_tag_set *nvme_rdma_alloc_tagset(struct nvme_ctrl *nctrl,
		bool admin)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(nctrl);
	struct blk_mq_tag_set *set;
	int ret;

	if (admin) {
		set = &ctrl->admin_tag_set;
		memset(set, 0, sizeof(*set));
		set->ops = &nvme_rdma_admin_mq_ops;
		set->queue_depth = NVME_AQ_MQ_TAG_DEPTH;
		set->reserved_tags = 2; /* connect + keep-alive */
		set->numa_node = NUMA_NO_NODE;
		set->cmd_size = sizeof(struct nvme_rdma_request) +
			SG_CHUNK_SIZE * sizeof(struct scatterlist);
		set->driver_data = ctrl;
		set->nr_hw_queues = 1;
		set->timeout = ADMIN_TIMEOUT;
		set->flags = BLK_MQ_F_NO_SCHED;
	} else {
		set = &ctrl->tag_set;
		memset(set, 0, sizeof(*set));
		set->ops = &nvme_rdma_mq_ops;
		set->queue_depth = nctrl->sqsize + 1;
		set->reserved_tags = 1; /* fabric connect */
		set->numa_node = NUMA_NO_NODE;
		set->flags = BLK_MQ_F_SHOULD_MERGE;
		set->cmd_size = sizeof(struct nvme_rdma_request) +
			SG_CHUNK_SIZE * sizeof(struct scatterlist);
		set->driver_data = ctrl;
		set->nr_hw_queues = nctrl->queue_count - 1;
		set->timeout = NVME_IO_TIMEOUT;
	}

	ret = blk_mq_alloc_tag_set(set);
	if (ret)
		goto out;

	/*
	 * We need a reference on the device as long as the tag_set is alive,
	 * as the MRs in the request structures need a valid ib_device.
	 */
	ret = nvme_rdma_dev_get(ctrl->device);
	if (!ret) {
		ret = -EINVAL;
		goto out_free_tagset;
	}

	return set;

out_free_tagset:
	blk_mq_free_tag_set(set);
out:
	return ERR_PTR(ret);
}

static void nvme_rdma_destroy_admin_queue(struct nvme_rdma_ctrl *ctrl,
		bool remove)
{
	if (remove) {
		blk_cleanup_queue(ctrl->ctrl.admin_q);
		nvme_rdma_free_tagset(&ctrl->ctrl, ctrl->ctrl.admin_tagset);
	}
	if (ctrl->async_event_sqe.data) {
		nvme_rdma_free_qe(ctrl->device->dev, &ctrl->async_event_sqe,
				sizeof(struct nvme_command), DMA_TO_DEVICE);
		ctrl->async_event_sqe.data = NULL;
	}
	nvme_rdma_free_queue(&ctrl->queues[0]);
}

static int nvme_rdma_configure_admin_queue(struct nvme_rdma_ctrl *ctrl,
		bool new)
{
	int error;

	error = nvme_rdma_alloc_queue(ctrl, 0, NVME_AQ_DEPTH);
	if (error)
		return error;

	ctrl->device = ctrl->queues[0].device;

	ctrl->max_fr_pages = nvme_rdma_get_max_fr_pages(ctrl->device->dev);

	error = nvme_rdma_alloc_qe(ctrl->device->dev, &ctrl->async_event_sqe,
			sizeof(struct nvme_command), DMA_TO_DEVICE);
	if (error)
		goto out_free_queue;

	if (new) {
		ctrl->ctrl.admin_tagset = nvme_rdma_alloc_tagset(&ctrl->ctrl, true);
		if (IS_ERR(ctrl->ctrl.admin_tagset)) {
			error = PTR_ERR(ctrl->ctrl.admin_tagset);
			goto out_free_async_qe;
		}

		ctrl->ctrl.admin_q = blk_mq_init_queue(&ctrl->admin_tag_set);
		if (IS_ERR(ctrl->ctrl.admin_q)) {
			error = PTR_ERR(ctrl->ctrl.admin_q);
			goto out_free_tagset;
		}
	}

	error = nvme_rdma_start_queue(ctrl, 0);
	if (error)
		goto out_cleanup_queue;

	error = ctrl->ctrl.ops->reg_read64(&ctrl->ctrl, NVME_REG_CAP,
			&ctrl->ctrl.cap);
	if (error) {
		dev_err(ctrl->ctrl.device,
			"prop_get NVME_REG_CAP failed\n");
		goto out_stop_queue;
	}

	ctrl->ctrl.sqsize =
		min_t(int, NVME_CAP_MQES(ctrl->ctrl.cap), ctrl->ctrl.sqsize);

	error = nvme_enable_ctrl(&ctrl->ctrl, ctrl->ctrl.cap);
	if (error)
		goto out_stop_queue;

	ctrl->ctrl.max_hw_sectors =
		(ctrl->max_fr_pages - 1) << (ilog2(SZ_4K) - 9);

	error = nvme_init_identify(&ctrl->ctrl);
	if (error)
		goto out_stop_queue;

	return 0;

out_stop_queue:
	nvme_rdma_stop_queue(&ctrl->queues[0]);
out_cleanup_queue:
	if (new)
		blk_cleanup_queue(ctrl->ctrl.admin_q);
out_free_tagset:
	if (new)
		nvme_rdma_free_tagset(&ctrl->ctrl, ctrl->ctrl.admin_tagset);
out_free_async_qe:
	if (ctrl->async_event_sqe.data) {
		nvme_rdma_free_qe(ctrl->device->dev, &ctrl->async_event_sqe,
			sizeof(struct nvme_command), DMA_TO_DEVICE);
		ctrl->async_event_sqe.data = NULL;
	}
out_free_queue:
	nvme_rdma_free_queue(&ctrl->queues[0]);
	return error;
}

static void nvme_rdma_destroy_io_queues(struct nvme_rdma_ctrl *ctrl,
		bool remove)
{
	if (remove) {
		blk_cleanup_queue(ctrl->ctrl.connect_q);
		nvme_rdma_free_tagset(&ctrl->ctrl, ctrl->ctrl.tagset);
	}
	nvme_rdma_free_io_queues(ctrl);
}

static int nvme_rdma_configure_io_queues(struct nvme_rdma_ctrl *ctrl, bool new)
{
	int ret;

	ret = nvme_rdma_alloc_io_queues(ctrl);
	if (ret)
		return ret;

	if (new) {
		ctrl->ctrl.tagset = nvme_rdma_alloc_tagset(&ctrl->ctrl, false);
		if (IS_ERR(ctrl->ctrl.tagset)) {
			ret = PTR_ERR(ctrl->ctrl.tagset);
			goto out_free_io_queues;
		}

		ctrl->ctrl.connect_q = blk_mq_init_queue(&ctrl->tag_set);
		if (IS_ERR(ctrl->ctrl.connect_q)) {
			ret = PTR_ERR(ctrl->ctrl.connect_q);
			goto out_free_tag_set;
		}
	} else {
		blk_mq_update_nr_hw_queues(&ctrl->tag_set,
			ctrl->ctrl.queue_count - 1);
	}

	ret = nvme_rdma_start_io_queues(ctrl);
	if (ret)
		goto out_cleanup_connect_q;

	return 0;

out_cleanup_connect_q:
	if (new)
		blk_cleanup_queue(ctrl->ctrl.connect_q);
out_free_tag_set:
	if (new)
		nvme_rdma_free_tagset(&ctrl->ctrl, ctrl->ctrl.tagset);
out_free_io_queues:
	nvme_rdma_free_io_queues(ctrl);
	return ret;
}

static void nvme_rdma_teardown_admin_queue(struct nvme_rdma_ctrl *ctrl,
		bool remove)
{
	mutex_lock(&ctrl->teardown_lock);
	blk_mq_quiesce_queue(ctrl->ctrl.admin_q);
	nvme_rdma_stop_queue(&ctrl->queues[0]);
	if (ctrl->ctrl.admin_tagset)
		blk_mq_tagset_busy_iter(ctrl->ctrl.admin_tagset,
			nvme_cancel_request, &ctrl->ctrl);
	blk_mq_unquiesce_queue(ctrl->ctrl.admin_q);
	nvme_rdma_destroy_admin_queue(ctrl, remove);
	mutex_unlock(&ctrl->teardown_lock);
}

static void nvme_rdma_teardown_io_queues(struct nvme_rdma_ctrl *ctrl,
		bool remove)
{
	mutex_lock(&ctrl->teardown_lock);
	if (ctrl->ctrl.queue_count > 1) {
		nvme_stop_queues(&ctrl->ctrl);
		nvme_rdma_stop_io_queues(ctrl);
		if (ctrl->ctrl.tagset)
			blk_mq_tagset_busy_iter(ctrl->ctrl.tagset,
				nvme_cancel_request, &ctrl->ctrl);
		if (remove)
			nvme_start_queues(&ctrl->ctrl);
		nvme_rdma_destroy_io_queues(ctrl, remove);
	}
	mutex_unlock(&ctrl->teardown_lock);
}

static void nvme_rdma_stop_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(nctrl);

	cancel_work_sync(&ctrl->err_work);
	cancel_delayed_work_sync(&ctrl->reconnect_work);
}

static void nvme_rdma_free_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(nctrl);

	if (list_empty(&ctrl->list))
		goto free_ctrl;

	mutex_lock(&nvme_rdma_ctrl_mutex);
	list_del(&ctrl->list);
	mutex_unlock(&nvme_rdma_ctrl_mutex);

	nvmf_free_options(nctrl->opts);
free_ctrl:
	kfree(ctrl->queues);
	kfree(ctrl);
}

static void nvme_rdma_reconnect_or_remove(struct nvme_rdma_ctrl *ctrl)
{
	/* If we are resetting/deleting then do nothing */
	if (ctrl->ctrl.state != NVME_CTRL_CONNECTING) {
		WARN_ON_ONCE(ctrl->ctrl.state == NVME_CTRL_NEW ||
			ctrl->ctrl.state == NVME_CTRL_LIVE);
		return;
	}

	if (nvmf_should_reconnect(&ctrl->ctrl)) {
		dev_info(ctrl->ctrl.device, "Reconnecting in %d seconds...\n",
			ctrl->ctrl.opts->reconnect_delay);
		queue_delayed_work(nvme_wq, &ctrl->reconnect_work,
				ctrl->ctrl.opts->reconnect_delay * HZ);
	} else {
		nvme_delete_ctrl(&ctrl->ctrl);
	}
}

static int nvme_rdma_setup_ctrl(struct nvme_rdma_ctrl *ctrl, bool new)
{
	int ret = -EINVAL;
	bool changed;

	ret = nvme_rdma_configure_admin_queue(ctrl, new);
	if (ret)
		return ret;

	if (ctrl->ctrl.icdoff) {
		dev_err(ctrl->ctrl.device, "icdoff is not supported!\n");
		goto destroy_admin;
	}

	if (!(ctrl->ctrl.sgls & (1 << 2))) {
		dev_err(ctrl->ctrl.device,
			"Mandatory keyed sgls are not supported!\n");
		goto destroy_admin;
	}

	if (ctrl->ctrl.opts->queue_size > ctrl->ctrl.sqsize + 1) {
		dev_warn(ctrl->ctrl.device,
			"queue_size %zu > ctrl sqsize %u, clamping down\n",
			ctrl->ctrl.opts->queue_size, ctrl->ctrl.sqsize + 1);
	}

	if (ctrl->ctrl.sqsize + 1 > ctrl->ctrl.maxcmd) {
		dev_warn(ctrl->ctrl.device,
			"sqsize %u > ctrl maxcmd %u, clamping down\n",
			ctrl->ctrl.sqsize + 1, ctrl->ctrl.maxcmd);
		ctrl->ctrl.sqsize = ctrl->ctrl.maxcmd - 1;
	}

	if (ctrl->ctrl.sgls & (1 << 20))
		ctrl->use_inline_data = true;

	if (ctrl->ctrl.queue_count > 1) {
		ret = nvme_rdma_configure_io_queues(ctrl, new);
		if (ret)
			goto destroy_admin;
	}

	changed = nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_LIVE);
	if (!changed) {
		/* state change failure is ok if we're in DELETING state */
		WARN_ON_ONCE(ctrl->ctrl.state != NVME_CTRL_DELETING);
		ret = -EINVAL;
		goto destroy_io;
	}

	nvme_start_ctrl(&ctrl->ctrl);
	return 0;

destroy_io:
	if (ctrl->ctrl.queue_count > 1)
		nvme_rdma_destroy_io_queues(ctrl, new);
destroy_admin:
	nvme_rdma_stop_queue(&ctrl->queues[0]);
	nvme_rdma_destroy_admin_queue(ctrl, new);
	return ret;
}

static void nvme_rdma_reconnect_ctrl_work(struct work_struct *work)
{
	struct nvme_rdma_ctrl *ctrl = container_of(to_delayed_work(work),
			struct nvme_rdma_ctrl, reconnect_work);

	++ctrl->ctrl.nr_reconnects;

	if (nvme_rdma_setup_ctrl(ctrl, false))
		goto requeue;

	dev_info(ctrl->ctrl.device, "Successfully reconnected (%d attempts)\n",
			ctrl->ctrl.nr_reconnects);

	ctrl->ctrl.nr_reconnects = 0;

	return;

requeue:
	dev_info(ctrl->ctrl.device, "Failed reconnect attempt %d\n",
			ctrl->ctrl.nr_reconnects);
	nvme_rdma_reconnect_or_remove(ctrl);
}

static void nvme_rdma_error_recovery_work(struct work_struct *work)
{
	struct nvme_rdma_ctrl *ctrl = container_of(work,
			struct nvme_rdma_ctrl, err_work);

	nvme_stop_keep_alive(&ctrl->ctrl);
	nvme_rdma_teardown_io_queues(ctrl, false);
	nvme_start_queues(&ctrl->ctrl);
	nvme_rdma_teardown_admin_queue(ctrl, false);

	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_CONNECTING)) {
		/* state change failure is ok if we're in DELETING state */
		WARN_ON_ONCE(ctrl->ctrl.state != NVME_CTRL_DELETING);
		return;
	}

	nvme_rdma_reconnect_or_remove(ctrl);
}

static void nvme_rdma_error_recovery(struct nvme_rdma_ctrl *ctrl)
{
	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_RESETTING))
		return;

	queue_work(nvme_wq, &ctrl->err_work);
}

static void nvme_rdma_wr_error(struct ib_cq *cq, struct ib_wc *wc,
		const char *op)
{
	struct nvme_rdma_queue *queue = cq->cq_context;
	struct nvme_rdma_ctrl *ctrl = queue->ctrl;

	if (ctrl->ctrl.state == NVME_CTRL_LIVE)
		dev_info(ctrl->ctrl.device,
			     "%s for CQE 0x%p failed with status %s (%d)\n",
			     op, wc->wr_cqe,
			     ib_wc_status_msg(wc->status), wc->status);
	nvme_rdma_error_recovery(ctrl);
}

static void nvme_rdma_memreg_done(struct ib_cq *cq, struct ib_wc *wc)
{
	if (unlikely(wc->status != IB_WC_SUCCESS))
		nvme_rdma_wr_error(cq, wc, "MEMREG");
}

static void nvme_rdma_inv_rkey_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvme_rdma_request *req =
		container_of(wc->wr_cqe, struct nvme_rdma_request, reg_cqe);
	struct request *rq = blk_mq_rq_from_pdu(req);

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		nvme_rdma_wr_error(cq, wc, "LOCAL_INV");
		return;
	}

	if (refcount_dec_and_test(&req->ref))
		nvme_end_request(rq, req->status, req->result);

}

static int nvme_rdma_inv_rkey(struct nvme_rdma_queue *queue,
		struct nvme_rdma_request *req)
{
	struct ib_send_wr wr = {
		.opcode		    = IB_WR_LOCAL_INV,
		.next		    = NULL,
		.num_sge	    = 0,
		.send_flags	    = IB_SEND_SIGNALED,
		.ex.invalidate_rkey = req->mr->rkey,
	};

	req->reg_cqe.done = nvme_rdma_inv_rkey_done;
	wr.wr_cqe = &req->reg_cqe;

	return ib_post_send(queue->qp, &wr, NULL);
}

static void nvme_rdma_unmap_data(struct nvme_rdma_queue *queue,
		struct request *rq)
{
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_device *dev = queue->device;
	struct ib_device *ibdev = dev->dev;

	if (!blk_rq_payload_bytes(rq))
		return;

	if (req->mr) {
		ib_mr_pool_put(queue->qp, &queue->qp->rdma_mrs, req->mr);
		req->mr = NULL;
	}

	ib_dma_unmap_sg(ibdev, req->sg_table.sgl,
			req->nents, rq_data_dir(rq) ==
				    WRITE ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

	nvme_cleanup_cmd(rq);
	sg_free_table_chained(&req->sg_table, true);
}

static int nvme_rdma_set_sg_null(struct nvme_command *c)
{
	struct nvme_keyed_sgl_desc *sg = &c->common.dptr.ksgl;

	sg->addr = 0;
	put_unaligned_le24(0, sg->length);
	put_unaligned_le32(0, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;
	return 0;
}

static int nvme_rdma_map_sg_inline(struct nvme_rdma_queue *queue,
		struct nvme_rdma_request *req, struct nvme_command *c,
		int count)
{
	struct nvme_sgl_desc *sg = &c->common.dptr.sgl;
	struct scatterlist *sgl = req->sg_table.sgl;
	struct ib_sge *sge = &req->sge[1];
	u32 len = 0;
	int i;

	for (i = 0; i < count; i++, sgl++, sge++) {
		sge->addr = sg_dma_address(sgl);
		sge->length = sg_dma_len(sgl);
		sge->lkey = queue->device->pd->local_dma_lkey;
		len += sge->length;
	}

	sg->addr = cpu_to_le64(queue->ctrl->ctrl.icdoff);
	sg->length = cpu_to_le32(len);
	sg->type = (NVME_SGL_FMT_DATA_DESC << 4) | NVME_SGL_FMT_OFFSET;

	req->num_sge += count;
	return 0;
}

static int nvme_rdma_map_sg_single(struct nvme_rdma_queue *queue,
		struct nvme_rdma_request *req, struct nvme_command *c)
{
	struct nvme_keyed_sgl_desc *sg = &c->common.dptr.ksgl;

	sg->addr = cpu_to_le64(sg_dma_address(req->sg_table.sgl));
	put_unaligned_le24(sg_dma_len(req->sg_table.sgl), sg->length);
	put_unaligned_le32(queue->device->pd->unsafe_global_rkey, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;
	return 0;
}

static int nvme_rdma_map_sg_fr(struct nvme_rdma_queue *queue,
		struct nvme_rdma_request *req, struct nvme_command *c,
		int count)
{
	struct nvme_keyed_sgl_desc *sg = &c->common.dptr.ksgl;
	int nr;

	req->mr = ib_mr_pool_get(queue->qp, &queue->qp->rdma_mrs);
	if (WARN_ON_ONCE(!req->mr))
		return -EAGAIN;

	/*
	 * Align the MR to a 4K page size to match the ctrl page size and
	 * the block virtual boundary.
	 */
	nr = ib_map_mr_sg(req->mr, req->sg_table.sgl, count, NULL, SZ_4K);
	if (unlikely(nr < count)) {
		ib_mr_pool_put(queue->qp, &queue->qp->rdma_mrs, req->mr);
		req->mr = NULL;
		if (nr < 0)
			return nr;
		return -EINVAL;
	}

	ib_update_fast_reg_key(req->mr, ib_inc_rkey(req->mr->rkey));

	req->reg_cqe.done = nvme_rdma_memreg_done;
	memset(&req->reg_wr, 0, sizeof(req->reg_wr));
	req->reg_wr.wr.opcode = IB_WR_REG_MR;
	req->reg_wr.wr.wr_cqe = &req->reg_cqe;
	req->reg_wr.wr.num_sge = 0;
	req->reg_wr.mr = req->mr;
	req->reg_wr.key = req->mr->rkey;
	req->reg_wr.access = IB_ACCESS_LOCAL_WRITE |
			     IB_ACCESS_REMOTE_READ |
			     IB_ACCESS_REMOTE_WRITE;

	sg->addr = cpu_to_le64(req->mr->iova);
	put_unaligned_le24(req->mr->length, sg->length);
	put_unaligned_le32(req->mr->rkey, sg->key);
	sg->type = (NVME_KEY_SGL_FMT_DATA_DESC << 4) |
			NVME_SGL_FMT_INVALIDATE;

	return 0;
}

static int nvme_rdma_map_data(struct nvme_rdma_queue *queue,
		struct request *rq, struct nvme_command *c)
{
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_device *dev = queue->device;
	struct ib_device *ibdev = dev->dev;
	int count, ret;

	req->num_sge = 1;
	refcount_set(&req->ref, 2); /* send and recv completions */

	c->common.flags |= NVME_CMD_SGL_METABUF;

	if (!blk_rq_payload_bytes(rq))
		return nvme_rdma_set_sg_null(c);

	req->sg_table.sgl = req->first_sgl;
	ret = sg_alloc_table_chained(&req->sg_table,
			blk_rq_nr_phys_segments(rq), req->sg_table.sgl);
	if (ret)
		return -ENOMEM;

	req->nents = blk_rq_map_sg(rq->q, rq, req->sg_table.sgl);

	count = ib_dma_map_sg(ibdev, req->sg_table.sgl, req->nents,
		    rq_data_dir(rq) == WRITE ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (unlikely(count <= 0)) {
		ret = -EIO;
		goto out_free_table;
	}

	if (count <= dev->num_inline_segments) {
		if (rq_data_dir(rq) == WRITE && nvme_rdma_queue_idx(queue) &&
		    queue->ctrl->use_inline_data &&
		    blk_rq_payload_bytes(rq) <=
				nvme_rdma_inline_data_size(queue)) {
			ret = nvme_rdma_map_sg_inline(queue, req, c, count);
			goto out;
		}

		if (count == 1 && dev->pd->flags & IB_PD_UNSAFE_GLOBAL_RKEY) {
			ret = nvme_rdma_map_sg_single(queue, req, c);
			goto out;
		}
	}

	ret = nvme_rdma_map_sg_fr(queue, req, c, count);
out:
	if (unlikely(ret))
		goto out_unmap_sg;

	return 0;

out_unmap_sg:
	ib_dma_unmap_sg(ibdev, req->sg_table.sgl,
			req->nents, rq_data_dir(rq) ==
			WRITE ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
out_free_table:
	sg_free_table_chained(&req->sg_table, true);
	return ret;
}

static void nvme_rdma_send_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvme_rdma_qe *qe =
		container_of(wc->wr_cqe, struct nvme_rdma_qe, cqe);
	struct nvme_rdma_request *req =
		container_of(qe, struct nvme_rdma_request, sqe);
	struct request *rq = blk_mq_rq_from_pdu(req);

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		nvme_rdma_wr_error(cq, wc, "SEND");
		return;
	}

	if (refcount_dec_and_test(&req->ref))
		nvme_end_request(rq, req->status, req->result);
}

static int nvme_rdma_post_send(struct nvme_rdma_queue *queue,
		struct nvme_rdma_qe *qe, struct ib_sge *sge, u32 num_sge,
		struct ib_send_wr *first)
{
	struct ib_send_wr wr;
	int ret;

	sge->addr   = qe->dma;
	sge->length = sizeof(struct nvme_command),
	sge->lkey   = queue->device->pd->local_dma_lkey;

	wr.next       = NULL;
	wr.wr_cqe     = &qe->cqe;
	wr.sg_list    = sge;
	wr.num_sge    = num_sge;
	wr.opcode     = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED;

	if (first)
		first->next = &wr;
	else
		first = &wr;

	ret = ib_post_send(queue->qp, first, NULL);
	if (unlikely(ret)) {
		dev_err(queue->ctrl->ctrl.device,
			     "%s failed with error code %d\n", __func__, ret);
	}
	return ret;
}

static int nvme_rdma_post_recv(struct nvme_rdma_queue *queue,
		struct nvme_rdma_qe *qe)
{
	struct ib_recv_wr wr;
	struct ib_sge list;
	int ret;

	list.addr   = qe->dma;
	list.length = sizeof(struct nvme_completion);
	list.lkey   = queue->device->pd->local_dma_lkey;

	qe->cqe.done = nvme_rdma_recv_done;

	wr.next     = NULL;
	wr.wr_cqe   = &qe->cqe;
	wr.sg_list  = &list;
	wr.num_sge  = 1;

	ret = ib_post_recv(queue->qp, &wr, NULL);
	if (unlikely(ret)) {
		dev_err(queue->ctrl->ctrl.device,
			"%s failed with error code %d\n", __func__, ret);
	}
	return ret;
}

static struct blk_mq_tags *nvme_rdma_tagset(struct nvme_rdma_queue *queue)
{
	u32 queue_idx = nvme_rdma_queue_idx(queue);

	if (queue_idx == 0)
		return queue->ctrl->admin_tag_set.tags[queue_idx];
	return queue->ctrl->tag_set.tags[queue_idx - 1];
}

static void nvme_rdma_async_done(struct ib_cq *cq, struct ib_wc *wc)
{
	if (unlikely(wc->status != IB_WC_SUCCESS))
		nvme_rdma_wr_error(cq, wc, "ASYNC");
}

static void nvme_rdma_submit_async_event(struct nvme_ctrl *arg)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(arg);
	struct nvme_rdma_queue *queue = &ctrl->queues[0];
	struct ib_device *dev = queue->device->dev;
	struct nvme_rdma_qe *sqe = &ctrl->async_event_sqe;
	struct nvme_command *cmd = sqe->data;
	struct ib_sge sge;
	int ret;

	ib_dma_sync_single_for_cpu(dev, sqe->dma, sizeof(*cmd), DMA_TO_DEVICE);

	memset(cmd, 0, sizeof(*cmd));
	cmd->common.opcode = nvme_admin_async_event;
	cmd->common.command_id = NVME_AQ_BLK_MQ_DEPTH;
	cmd->common.flags |= NVME_CMD_SGL_METABUF;
	nvme_rdma_set_sg_null(cmd);

	sqe->cqe.done = nvme_rdma_async_done;

	ib_dma_sync_single_for_device(dev, sqe->dma, sizeof(*cmd),
			DMA_TO_DEVICE);

	ret = nvme_rdma_post_send(queue, sqe, &sge, 1, NULL);
	WARN_ON_ONCE(ret);
}

static int nvme_rdma_process_nvme_rsp(struct nvme_rdma_queue *queue,
		struct nvme_completion *cqe, struct ib_wc *wc, int tag)
{
	struct request *rq;
	struct nvme_rdma_request *req;
	int ret = 0;

	rq = blk_mq_tag_to_rq(nvme_rdma_tagset(queue), cqe->command_id);
	if (!rq) {
		dev_err(queue->ctrl->ctrl.device,
			"tag 0x%x on QP %#x not found\n",
			cqe->command_id, queue->qp->qp_num);
		nvme_rdma_error_recovery(queue->ctrl);
		return ret;
	}
	req = blk_mq_rq_to_pdu(rq);

	req->status = cqe->status;
	req->result = cqe->result;

	if (wc->wc_flags & IB_WC_WITH_INVALIDATE) {
		if (unlikely(wc->ex.invalidate_rkey != req->mr->rkey)) {
			dev_err(queue->ctrl->ctrl.device,
				"Bogus remote invalidation for rkey %#x\n",
				req->mr->rkey);
			nvme_rdma_error_recovery(queue->ctrl);
		}
	} else if (req->mr) {
		ret = nvme_rdma_inv_rkey(queue, req);
		if (unlikely(ret < 0)) {
			dev_err(queue->ctrl->ctrl.device,
				"Queueing INV WR for rkey %#x failed (%d)\n",
				req->mr->rkey, ret);
			nvme_rdma_error_recovery(queue->ctrl);
		}
		/* the local invalidation completion will end the request */
		return 0;
	}

	if (refcount_dec_and_test(&req->ref)) {
		if (rq->tag == tag)
			ret = 1;
		nvme_end_request(rq, req->status, req->result);
	}

	return ret;
}

static int __nvme_rdma_recv_done(struct ib_cq *cq, struct ib_wc *wc, int tag)
{
	struct nvme_rdma_qe *qe =
		container_of(wc->wr_cqe, struct nvme_rdma_qe, cqe);
	struct nvme_rdma_queue *queue = cq->cq_context;
	struct ib_device *ibdev = queue->device->dev;
	struct nvme_completion *cqe = qe->data;
	const size_t len = sizeof(struct nvme_completion);
	int ret = 0;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		nvme_rdma_wr_error(cq, wc, "RECV");
		return 0;
	}

	ib_dma_sync_single_for_cpu(ibdev, qe->dma, len, DMA_FROM_DEVICE);
	/*
	 * AEN requests are special as they don't time out and can
	 * survive any kind of queue freeze and often don't respond to
	 * aborts.  We don't even bother to allocate a struct request
	 * for them but rather special case them here.
	 */
	if (unlikely(nvme_rdma_queue_idx(queue) == 0 &&
			cqe->command_id >= NVME_AQ_BLK_MQ_DEPTH))
		nvme_complete_async_event(&queue->ctrl->ctrl, cqe->status,
				&cqe->result);
	else
		ret = nvme_rdma_process_nvme_rsp(queue, cqe, wc, tag);
	ib_dma_sync_single_for_device(ibdev, qe->dma, len, DMA_FROM_DEVICE);

	nvme_rdma_post_recv(queue, qe);
	return ret;
}

static void nvme_rdma_recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	__nvme_rdma_recv_done(cq, wc, -1);
}

static int nvme_rdma_conn_established(struct nvme_rdma_queue *queue)
{
	int ret, i;

	for (i = 0; i < queue->queue_size; i++) {
		ret = nvme_rdma_post_recv(queue, &queue->rsp_ring[i]);
		if (ret)
			goto out_destroy_queue_ib;
	}

	return 0;

out_destroy_queue_ib:
	nvme_rdma_destroy_queue_ib(queue);
	return ret;
}

static int nvme_rdma_conn_rejected(struct nvme_rdma_queue *queue,
		struct rdma_cm_event *ev)
{
	struct rdma_cm_id *cm_id = queue->cm_id;
	int status = ev->status;
	const char *rej_msg;
	const struct nvme_rdma_cm_rej *rej_data;
	u8 rej_data_len;

	rej_msg = rdma_reject_msg(cm_id, status);
	rej_data = rdma_consumer_reject_data(cm_id, ev, &rej_data_len);

	if (rej_data && rej_data_len >= sizeof(u16)) {
		u16 sts = le16_to_cpu(rej_data->sts);

		dev_err(queue->ctrl->ctrl.device,
		      "Connect rejected: status %d (%s) nvme status %d (%s).\n",
		      status, rej_msg, sts, nvme_rdma_cm_msg(sts));
	} else {
		dev_err(queue->ctrl->ctrl.device,
			"Connect rejected: status %d (%s).\n", status, rej_msg);
	}

	return -ECONNRESET;
}

static int nvme_rdma_addr_resolved(struct nvme_rdma_queue *queue)
{
	int ret;

	ret = nvme_rdma_create_queue_ib(queue);
	if (ret)
		return ret;

	ret = rdma_resolve_route(queue->cm_id, NVME_RDMA_CONNECT_TIMEOUT_MS);
	if (ret) {
		dev_err(queue->ctrl->ctrl.device,
			"rdma_resolve_route failed (%d).\n",
			queue->cm_error);
		goto out_destroy_queue;
	}

	return 0;

out_destroy_queue:
	nvme_rdma_destroy_queue_ib(queue);
	return ret;
}

static int nvme_rdma_route_resolved(struct nvme_rdma_queue *queue)
{
	struct nvme_rdma_ctrl *ctrl = queue->ctrl;
	struct rdma_conn_param param = { };
	struct nvme_rdma_cm_req priv = { };
	int ret;

	param.qp_num = queue->qp->qp_num;
	param.flow_control = 1;

	param.responder_resources = queue->device->dev->attrs.max_qp_rd_atom;
	/* maximum retry count */
	param.retry_count = 7;
	param.rnr_retry_count = 7;
	param.private_data = &priv;
	param.private_data_len = sizeof(priv);

	priv.recfmt = cpu_to_le16(NVME_RDMA_CM_FMT_1_0);
	priv.qid = cpu_to_le16(nvme_rdma_queue_idx(queue));
	/*
	 * set the admin queue depth to the minimum size
	 * specified by the Fabrics standard.
	 */
	if (priv.qid == 0) {
		priv.hrqsize = cpu_to_le16(NVME_AQ_DEPTH);
		priv.hsqsize = cpu_to_le16(NVME_AQ_DEPTH - 1);
	} else {
		/*
		 * current interpretation of the fabrics spec
		 * is at minimum you make hrqsize sqsize+1, or a
		 * 1's based representation of sqsize.
		 */
		priv.hrqsize = cpu_to_le16(queue->queue_size);
		priv.hsqsize = cpu_to_le16(queue->ctrl->ctrl.sqsize);
	}

	ret = rdma_connect(queue->cm_id, &param);
	if (ret) {
		dev_err(ctrl->ctrl.device,
			"rdma_connect failed (%d).\n", ret);
		goto out_destroy_queue_ib;
	}

	return 0;

out_destroy_queue_ib:
	nvme_rdma_destroy_queue_ib(queue);
	return ret;
}

static int nvme_rdma_cm_handler(struct rdma_cm_id *cm_id,
		struct rdma_cm_event *ev)
{
	struct nvme_rdma_queue *queue = cm_id->context;
	int cm_error = 0;

	dev_dbg(queue->ctrl->ctrl.device, "%s (%d): status %d id %p\n",
		rdma_event_msg(ev->event), ev->event,
		ev->status, cm_id);

	switch (ev->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		cm_error = nvme_rdma_addr_resolved(queue);
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		cm_error = nvme_rdma_route_resolved(queue);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		queue->cm_error = nvme_rdma_conn_established(queue);
		/* complete cm_done regardless of success/failure */
		complete(&queue->cm_done);
		return 0;
	case RDMA_CM_EVENT_REJECTED:
		nvme_rdma_destroy_queue_ib(queue);
		cm_error = nvme_rdma_conn_rejected(queue, ev);
		break;
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
		nvme_rdma_destroy_queue_ib(queue);
		/* fall through */
	case RDMA_CM_EVENT_ADDR_ERROR:
		dev_dbg(queue->ctrl->ctrl.device,
			"CM error event %d\n", ev->event);
		cm_error = -ECONNRESET;
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		dev_dbg(queue->ctrl->ctrl.device,
			"disconnect received - connection closed\n");
		nvme_rdma_error_recovery(queue->ctrl);
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		/* device removal is handled via the ib_client API */
		break;
	default:
		dev_err(queue->ctrl->ctrl.device,
			"Unexpected RDMA CM event (%d)\n", ev->event);
		nvme_rdma_error_recovery(queue->ctrl);
		break;
	}

	if (cm_error) {
		queue->cm_error = cm_error;
		complete(&queue->cm_done);
	}

	return 0;
}

static enum blk_eh_timer_return
nvme_rdma_timeout(struct request *rq, bool reserved)
{
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_queue *queue = req->queue;
	struct nvme_rdma_ctrl *ctrl = queue->ctrl;

	dev_warn(ctrl->ctrl.device, "I/O %d QID %d timeout\n",
		 rq->tag, nvme_rdma_queue_idx(queue));

	if (ctrl->ctrl.state != NVME_CTRL_LIVE) {
		/*
		 * Teardown immediately if controller times out while starting
		 * or we are already started error recovery. all outstanding
		 * requests are completed on shutdown, so we return BLK_EH_DONE.
		 */
		flush_work(&ctrl->err_work);
		nvme_rdma_teardown_io_queues(ctrl, false);
		nvme_rdma_teardown_admin_queue(ctrl, false);
		return BLK_EH_DONE;
	}

	dev_warn(ctrl->ctrl.device, "starting error recovery\n");
	nvme_rdma_error_recovery(ctrl);

	return BLK_EH_RESET_TIMER;
}

static blk_status_t nvme_rdma_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_rdma_queue *queue = hctx->driver_data;
	struct request *rq = bd->rq;
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_qe *sqe = &req->sqe;
	struct nvme_command *c = sqe->data;
	struct ib_device *dev;
	bool queue_ready = test_bit(NVME_RDMA_Q_LIVE, &queue->flags);
	blk_status_t ret;
	int err;

	WARN_ON_ONCE(rq->tag < 0);

	if (!nvmf_check_ready(&queue->ctrl->ctrl, rq, queue_ready))
		return nvmf_fail_nonready_command(&queue->ctrl->ctrl, rq);

	dev = queue->device->dev;
	ib_dma_sync_single_for_cpu(dev, sqe->dma,
			sizeof(struct nvme_command), DMA_TO_DEVICE);

	ret = nvme_setup_cmd(ns, rq, c);
	if (ret)
		return ret;

	blk_mq_start_request(rq);

	err = nvme_rdma_map_data(queue, rq, c);
	if (unlikely(err < 0)) {
		dev_err(queue->ctrl->ctrl.device,
			     "Failed to map data (%d)\n", err);
		nvme_cleanup_cmd(rq);
		goto err;
	}

	sqe->cqe.done = nvme_rdma_send_done;

	ib_dma_sync_single_for_device(dev, sqe->dma,
			sizeof(struct nvme_command), DMA_TO_DEVICE);

	err = nvme_rdma_post_send(queue, sqe, req->sge, req->num_sge,
			req->mr ? &req->reg_wr.wr : NULL);
	if (unlikely(err)) {
		nvme_rdma_unmap_data(queue, rq);
		goto err;
	}

	return BLK_STS_OK;
err:
	if (err == -ENOMEM || err == -EAGAIN)
		return BLK_STS_RESOURCE;
	return BLK_STS_IOERR;
}

static int nvme_rdma_poll(struct blk_mq_hw_ctx *hctx, unsigned int tag)
{
	struct nvme_rdma_queue *queue = hctx->driver_data;
	struct ib_cq *cq = queue->ib_cq;
	struct ib_wc wc;
	int found = 0;

	while (ib_poll_cq(cq, 1, &wc) > 0) {
		struct ib_cqe *cqe = wc.wr_cqe;

		if (cqe) {
			if (cqe->done == nvme_rdma_recv_done)
				found |= __nvme_rdma_recv_done(cq, &wc, tag);
			else
				cqe->done(cq, &wc);
		}
	}

	return found;
}

static void nvme_rdma_complete_rq(struct request *rq)
{
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);

	nvme_rdma_unmap_data(req->queue, rq);
	nvme_complete_rq(rq);
}

static int nvme_rdma_map_queues(struct blk_mq_tag_set *set)
{
	struct nvme_rdma_ctrl *ctrl = set->driver_data;

	return blk_mq_rdma_map_queues(set, ctrl->device->dev, 0);
}

static const struct blk_mq_ops nvme_rdma_mq_ops = {
	.queue_rq	= nvme_rdma_queue_rq,
	.complete	= nvme_rdma_complete_rq,
	.init_request	= nvme_rdma_init_request,
	.exit_request	= nvme_rdma_exit_request,
	.init_hctx	= nvme_rdma_init_hctx,
	.poll		= nvme_rdma_poll,
	.timeout	= nvme_rdma_timeout,
	.map_queues	= nvme_rdma_map_queues,
};

static const struct blk_mq_ops nvme_rdma_admin_mq_ops = {
	.queue_rq	= nvme_rdma_queue_rq,
	.complete	= nvme_rdma_complete_rq,
	.init_request	= nvme_rdma_init_request,
	.exit_request	= nvme_rdma_exit_request,
	.init_hctx	= nvme_rdma_init_admin_hctx,
	.timeout	= nvme_rdma_timeout,
};

static void nvme_rdma_shutdown_ctrl(struct nvme_rdma_ctrl *ctrl, bool shutdown)
{
	nvme_rdma_teardown_io_queues(ctrl, shutdown);
	if (shutdown)
		nvme_shutdown_ctrl(&ctrl->ctrl);
	else
		nvme_disable_ctrl(&ctrl->ctrl, ctrl->ctrl.cap);
	nvme_rdma_teardown_admin_queue(ctrl, shutdown);
}

static void nvme_rdma_delete_ctrl(struct nvme_ctrl *ctrl)
{
	nvme_rdma_shutdown_ctrl(to_rdma_ctrl(ctrl), true);
}

static void nvme_rdma_reset_ctrl_work(struct work_struct *work)
{
	struct nvme_rdma_ctrl *ctrl =
		container_of(work, struct nvme_rdma_ctrl, ctrl.reset_work);

	nvme_stop_ctrl(&ctrl->ctrl);
	nvme_rdma_shutdown_ctrl(ctrl, false);

	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_CONNECTING)) {
		/* state change failure should never happen */
		WARN_ON_ONCE(1);
		return;
	}

	if (nvme_rdma_setup_ctrl(ctrl, false))
		goto out_fail;

	return;

out_fail:
	++ctrl->ctrl.nr_reconnects;
	nvme_rdma_reconnect_or_remove(ctrl);
}

static const struct nvme_ctrl_ops nvme_rdma_ctrl_ops = {
	.name			= "rdma",
	.module			= THIS_MODULE,
	.flags			= NVME_F_FABRICS,
	.reg_read32		= nvmf_reg_read32,
	.reg_read64		= nvmf_reg_read64,
	.reg_write32		= nvmf_reg_write32,
	.free_ctrl		= nvme_rdma_free_ctrl,
	.submit_async_event	= nvme_rdma_submit_async_event,
	.delete_ctrl		= nvme_rdma_delete_ctrl,
	.get_address		= nvmf_get_address,
	.stop_ctrl		= nvme_rdma_stop_ctrl,
};

static inline bool
__nvme_rdma_options_match(struct nvme_rdma_ctrl *ctrl,
	struct nvmf_ctrl_options *opts)
{
	char *stdport = __stringify(NVME_RDMA_IP_PORT);


	if (!nvmf_ctlr_matches_baseopts(&ctrl->ctrl, opts) ||
	    strcmp(opts->traddr, ctrl->ctrl.opts->traddr))
		return false;

	if (opts->mask & NVMF_OPT_TRSVCID &&
	    ctrl->ctrl.opts->mask & NVMF_OPT_TRSVCID) {
		if (strcmp(opts->trsvcid, ctrl->ctrl.opts->trsvcid))
			return false;
	} else if (opts->mask & NVMF_OPT_TRSVCID) {
		if (strcmp(opts->trsvcid, stdport))
			return false;
	} else if (ctrl->ctrl.opts->mask & NVMF_OPT_TRSVCID) {
		if (strcmp(stdport, ctrl->ctrl.opts->trsvcid))
			return false;
	}
	/* else, it's a match as both have stdport. Fall to next checks */

	/*
	 * checking the local address is rough. In most cases, one
	 * is not specified and the host port is selected by the stack.
	 *
	 * Assume no match if:
	 *  local address is specified and address is not the same
	 *  local address is not specified but remote is, or vice versa
	 *    (admin using specific host_traddr when it matters).
	 */
	if (opts->mask & NVMF_OPT_HOST_TRADDR &&
	    ctrl->ctrl.opts->mask & NVMF_OPT_HOST_TRADDR) {
		if (strcmp(opts->host_traddr, ctrl->ctrl.opts->host_traddr))
			return false;
	} else if (opts->mask & NVMF_OPT_HOST_TRADDR ||
		   ctrl->ctrl.opts->mask & NVMF_OPT_HOST_TRADDR)
		return false;
	/*
	 * if neither controller had an host port specified, assume it's
	 * a match as everything else matched.
	 */

	return true;
}

/*
 * Fails a connection request if it matches an existing controller
 * (association) with the same tuple:
 * <Host NQN, Host ID, local address, remote address, remote port, SUBSYS NQN>
 *
 * if local address is not specified in the request, it will match an
 * existing controller with all the other parameters the same and no
 * local port address specified as well.
 *
 * The ports don't need to be compared as they are intrinsically
 * already matched by the port pointers supplied.
 */
static bool
nvme_rdma_existing_controller(struct nvmf_ctrl_options *opts)
{
	struct nvme_rdma_ctrl *ctrl;
	bool found = false;

	mutex_lock(&nvme_rdma_ctrl_mutex);
	list_for_each_entry(ctrl, &nvme_rdma_ctrl_list, list) {
		found = __nvme_rdma_options_match(ctrl, opts);
		if (found)
			break;
	}
	mutex_unlock(&nvme_rdma_ctrl_mutex);

	return found;
}

static struct nvme_ctrl *nvme_rdma_create_ctrl(struct device *dev,
		struct nvmf_ctrl_options *opts)
{
	struct nvme_rdma_ctrl *ctrl;
	int ret;
	bool changed;
	char *port;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-ENOMEM);
	ctrl->ctrl.opts = opts;
	INIT_LIST_HEAD(&ctrl->list);
	mutex_init(&ctrl->teardown_lock);

	if (opts->mask & NVMF_OPT_TRSVCID)
		port = opts->trsvcid;
	else
		port = __stringify(NVME_RDMA_IP_PORT);

	ret = inet_pton_with_scope(&init_net, AF_UNSPEC,
			opts->traddr, port, &ctrl->addr);
	if (ret) {
		pr_err("malformed address passed: %s:%s\n", opts->traddr, port);
		goto out_free_ctrl;
	}

	if (opts->mask & NVMF_OPT_HOST_TRADDR) {
		ret = inet_pton_with_scope(&init_net, AF_UNSPEC,
			opts->host_traddr, NULL, &ctrl->src_addr);
		if (ret) {
			pr_err("malformed src address passed: %s\n",
			       opts->host_traddr);
			goto out_free_ctrl;
		}
	}

	if (!opts->duplicate_connect && nvme_rdma_existing_controller(opts)) {
		ret = -EALREADY;
		goto out_free_ctrl;
	}

	INIT_DELAYED_WORK(&ctrl->reconnect_work,
			nvme_rdma_reconnect_ctrl_work);
	INIT_WORK(&ctrl->err_work, nvme_rdma_error_recovery_work);
	INIT_WORK(&ctrl->ctrl.reset_work, nvme_rdma_reset_ctrl_work);

	ctrl->ctrl.queue_count = opts->nr_io_queues + 1; /* +1 for admin queue */
	ctrl->ctrl.sqsize = opts->queue_size - 1;
	ctrl->ctrl.kato = opts->kato;

	ret = -ENOMEM;
	ctrl->queues = kcalloc(ctrl->ctrl.queue_count, sizeof(*ctrl->queues),
				GFP_KERNEL);
	if (!ctrl->queues)
		goto out_free_ctrl;

	ret = nvme_init_ctrl(&ctrl->ctrl, dev, &nvme_rdma_ctrl_ops,
				0 /* no quirks, we're perfect! */);
	if (ret)
		goto out_kfree_queues;

	changed = nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_CONNECTING);
	WARN_ON_ONCE(!changed);

	ret = nvme_rdma_setup_ctrl(ctrl, true);
	if (ret)
		goto out_uninit_ctrl;

	dev_info(ctrl->ctrl.device, "new ctrl: NQN \"%s\", addr %pISpcs\n",
		ctrl->ctrl.opts->subsysnqn, &ctrl->addr);

	nvme_get_ctrl(&ctrl->ctrl);

	mutex_lock(&nvme_rdma_ctrl_mutex);
	list_add_tail(&ctrl->list, &nvme_rdma_ctrl_list);
	mutex_unlock(&nvme_rdma_ctrl_mutex);

	return &ctrl->ctrl;

out_uninit_ctrl:
	nvme_uninit_ctrl(&ctrl->ctrl);
	nvme_put_ctrl(&ctrl->ctrl);
	if (ret > 0)
		ret = -EIO;
	return ERR_PTR(ret);
out_kfree_queues:
	kfree(ctrl->queues);
out_free_ctrl:
	kfree(ctrl);
	return ERR_PTR(ret);
}

static struct nvmf_transport_ops nvme_rdma_transport = {
	.name		= "rdma",
	.module		= THIS_MODULE,
	.required_opts	= NVMF_OPT_TRADDR,
	.allowed_opts	= NVMF_OPT_TRSVCID | NVMF_OPT_RECONNECT_DELAY |
			  NVMF_OPT_HOST_TRADDR | NVMF_OPT_CTRL_LOSS_TMO,
	.create_ctrl	= nvme_rdma_create_ctrl,
};

static void nvme_rdma_remove_one(struct ib_device *ib_device, void *client_data)
{
	struct nvme_rdma_ctrl *ctrl;
	struct nvme_rdma_device *ndev;
	bool found = false;

	mutex_lock(&device_list_mutex);
	list_for_each_entry(ndev, &device_list, entry) {
		if (ndev->dev == ib_device) {
			found = true;
			break;
		}
	}
	mutex_unlock(&device_list_mutex);

	if (!found)
		return;

	/* Delete all controllers using this device */
	mutex_lock(&nvme_rdma_ctrl_mutex);
	list_for_each_entry(ctrl, &nvme_rdma_ctrl_list, list) {
		if (ctrl->device->dev != ib_device)
			continue;
		nvme_delete_ctrl(&ctrl->ctrl);
	}
	mutex_unlock(&nvme_rdma_ctrl_mutex);

	flush_workqueue(nvme_delete_wq);
}

static struct ib_client nvme_rdma_ib_client = {
	.name   = "nvme_rdma",
	.remove = nvme_rdma_remove_one
};

static int __init nvme_rdma_init_module(void)
{
	int ret;

	ret = ib_register_client(&nvme_rdma_ib_client);
	if (ret)
		return ret;

	ret = nvmf_register_transport(&nvme_rdma_transport);
	if (ret)
		goto err_unreg_client;

	return 0;

err_unreg_client:
	ib_unregister_client(&nvme_rdma_ib_client);
	return ret;
}

static void __exit nvme_rdma_cleanup_module(void)
{
	nvmf_unregister_transport(&nvme_rdma_transport);
	ib_unregister_client(&nvme_rdma_ib_client);
}

module_init(nvme_rdma_init_module);
module_exit(nvme_rdma_cleanup_module);

MODULE_LICENSE("GPL v2");
