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
#include <linux/err.h>
#include <linux/string.h>
#include <linux/atomic.h>
#include <linux/blk-mq.h>
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


#define NVME_RDMA_CONNECT_TIMEOUT_MS	1000		/* 1 second */

#define NVME_RDMA_MAX_SEGMENT_SIZE	0xffffff	/* 24-bit SGL field */

#define NVME_RDMA_MAX_SEGMENTS		256

#define NVME_RDMA_MAX_INLINE_SEGMENTS	1

static const char *const nvme_rdma_cm_status_strs[] = {
	[NVME_RDMA_CM_INVALID_LEN]	= "invalid length",
	[NVME_RDMA_CM_INVALID_RECFMT]	= "invalid record format",
	[NVME_RDMA_CM_INVALID_QID]	= "invalid queue ID",
	[NVME_RDMA_CM_INVALID_HSQSIZE]	= "invalid host SQ size",
	[NVME_RDMA_CM_INVALID_HRQSIZE]	= "invalid host RQ size",
	[NVME_RDMA_CM_NO_RSC]		= "resource not found",
	[NVME_RDMA_CM_INVALID_IRD]	= "invalid IRD",
	[NVME_RDMA_CM_INVALID_ORD]	= "Invalid ORD",
};

static const char *nvme_rdma_cm_msg(enum nvme_rdma_cm_status status)
{
	size_t index = status;

	if (index < ARRAY_SIZE(nvme_rdma_cm_status_strs) &&
	    nvme_rdma_cm_status_strs[index])
		return nvme_rdma_cm_status_strs[index];
	else
		return "unrecognized reason";
};

/*
 * We handle AEN commands ourselves and don't even let the
 * block layer know about them.
 */
#define NVME_RDMA_NR_AEN_COMMANDS      1
#define NVME_RDMA_AQ_BLKMQ_DEPTH       \
	(NVMF_AQ_DEPTH - NVME_RDMA_NR_AEN_COMMANDS)

struct nvme_rdma_device {
	struct ib_device       *dev;
	struct ib_pd	       *pd;
	struct kref		ref;
	struct list_head	entry;
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
	struct ib_sge		sge[1 + NVME_RDMA_MAX_INLINE_SEGMENTS];
	u32			num_sge;
	int			nents;
	bool			inline_data;
	struct ib_reg_wr	reg_wr;
	struct ib_cqe		reg_cqe;
	struct nvme_rdma_queue  *queue;
	struct sg_table		sg_table;
	struct scatterlist	first_sgl[];
};

enum nvme_rdma_queue_flags {
	NVME_RDMA_Q_CONNECTED = (1 << 0),
	NVME_RDMA_IB_QUEUE_ALLOCATED = (1 << 1),
	NVME_RDMA_Q_DELETING = (1 << 2),
	NVME_RDMA_Q_LIVE = (1 << 3),
};

struct nvme_rdma_queue {
	struct nvme_rdma_qe	*rsp_ring;
	u8			sig_count;
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
	/* read and written in the hot path */
	spinlock_t		lock;

	/* read only in the hot path */
	struct nvme_rdma_queue	*queues;
	u32			queue_count;

	/* other member variables */
	struct blk_mq_tag_set	tag_set;
	struct work_struct	delete_work;
	struct work_struct	reset_work;
	struct work_struct	err_work;

	struct nvme_rdma_qe	async_event_sqe;

	int			reconnect_delay;
	struct delayed_work	reconnect_work;

	struct list_head	list;

	struct blk_mq_tag_set	admin_tag_set;
	struct nvme_rdma_device	*device;

	u64			cap;
	u32			max_fr_pages;

	union {
		struct sockaddr addr;
		struct sockaddr_in addr_in;
	};

	struct nvme_ctrl	ctrl;
};

static inline struct nvme_rdma_ctrl *to_rdma_ctrl(struct nvme_ctrl *ctrl)
{
	return container_of(ctrl, struct nvme_rdma_ctrl, ctrl);
}

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_mutex);

static LIST_HEAD(nvme_rdma_ctrl_list);
static DEFINE_MUTEX(nvme_rdma_ctrl_mutex);

static struct workqueue_struct *nvme_rdma_wq;

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
	init_attr.cap.max_send_sge = 1 + NVME_RDMA_MAX_INLINE_SEGMENTS;
	init_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	init_attr.qp_type = IB_QPT_RC;
	init_attr.send_cq = queue->ib_cq;
	init_attr.recv_cq = queue->ib_cq;

	ret = rdma_create_qp(queue->cm_id, dev->pd, &init_attr);

	queue->qp = queue->cm_id->qp;
	return ret;
}

static int nvme_rdma_reinit_request(void *data, struct request *rq)
{
	struct nvme_rdma_ctrl *ctrl = data;
	struct nvme_rdma_device *dev = ctrl->device;
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	int ret = 0;

	if (!req->mr->need_inval)
		goto out;

	ib_dereg_mr(req->mr);

	req->mr = ib_alloc_mr(dev->pd, IB_MR_TYPE_MEM_REG,
			ctrl->max_fr_pages);
	if (IS_ERR(req->mr)) {
		ret = PTR_ERR(req->mr);
		req->mr = NULL;
		goto out;
	}

	req->mr->need_inval = false;

out:
	return ret;
}

static void __nvme_rdma_exit_request(struct nvme_rdma_ctrl *ctrl,
		struct request *rq, unsigned int queue_idx)
{
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_queue *queue = &ctrl->queues[queue_idx];
	struct nvme_rdma_device *dev = queue->device;

	if (req->mr)
		ib_dereg_mr(req->mr);

	nvme_rdma_free_qe(dev->dev, &req->sqe, sizeof(struct nvme_command),
			DMA_TO_DEVICE);
}

static void nvme_rdma_exit_request(void *data, struct request *rq,
				unsigned int hctx_idx, unsigned int rq_idx)
{
	return __nvme_rdma_exit_request(data, rq, hctx_idx + 1);
}

static void nvme_rdma_exit_admin_request(void *data, struct request *rq,
				unsigned int hctx_idx, unsigned int rq_idx)
{
	return __nvme_rdma_exit_request(data, rq, 0);
}

static int __nvme_rdma_init_request(struct nvme_rdma_ctrl *ctrl,
		struct request *rq, unsigned int queue_idx)
{
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_queue *queue = &ctrl->queues[queue_idx];
	struct nvme_rdma_device *dev = queue->device;
	struct ib_device *ibdev = dev->dev;
	int ret;

	BUG_ON(queue_idx >= ctrl->queue_count);

	ret = nvme_rdma_alloc_qe(ibdev, &req->sqe, sizeof(struct nvme_command),
			DMA_TO_DEVICE);
	if (ret)
		return ret;

	req->mr = ib_alloc_mr(dev->pd, IB_MR_TYPE_MEM_REG,
			ctrl->max_fr_pages);
	if (IS_ERR(req->mr)) {
		ret = PTR_ERR(req->mr);
		goto out_free_qe;
	}

	req->queue = queue;

	return 0;

out_free_qe:
	nvme_rdma_free_qe(dev->dev, &req->sqe, sizeof(struct nvme_command),
			DMA_TO_DEVICE);
	return -ENOMEM;
}

static int nvme_rdma_init_request(void *data, struct request *rq,
				unsigned int hctx_idx, unsigned int rq_idx,
				unsigned int numa_node)
{
	return __nvme_rdma_init_request(data, rq, hctx_idx + 1);
}

static int nvme_rdma_init_admin_request(void *data, struct request *rq,
				unsigned int hctx_idx, unsigned int rq_idx,
				unsigned int numa_node)
{
	return __nvme_rdma_init_request(data, rq, 0);
}

static int nvme_rdma_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
		unsigned int hctx_idx)
{
	struct nvme_rdma_ctrl *ctrl = data;
	struct nvme_rdma_queue *queue = &ctrl->queues[hctx_idx + 1];

	BUG_ON(hctx_idx >= ctrl->queue_count);

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

	if (!test_and_clear_bit(NVME_RDMA_IB_QUEUE_ALLOCATED, &queue->flags))
		return;

	dev = queue->device;
	ibdev = dev->dev;
	rdma_destroy_qp(queue->cm_id);
	ib_free_cq(queue->ib_cq);

	nvme_rdma_free_ring(ibdev, queue->rsp_ring, queue->queue_size,
			sizeof(struct nvme_completion), DMA_FROM_DEVICE);

	nvme_rdma_dev_put(dev);
}

static int nvme_rdma_create_queue_ib(struct nvme_rdma_queue *queue,
		struct nvme_rdma_device *dev)
{
	struct ib_device *ibdev = dev->dev;
	const int send_wr_factor = 3;			/* MR, SEND, INV */
	const int cq_factor = send_wr_factor + 1;	/* + RECV */
	int comp_vector, idx = nvme_rdma_queue_idx(queue);

	int ret;

	queue->device = dev;

	/*
	 * The admin queue is barely used once the controller is live, so don't
	 * bother to spread it out.
	 */
	if (idx == 0)
		comp_vector = 0;
	else
		comp_vector = idx % ibdev->num_comp_vectors;


	/* +1 for ib_stop_cq */
	queue->ib_cq = ib_alloc_cq(dev->dev, queue,
				cq_factor * queue->queue_size + 1, comp_vector,
				IB_POLL_SOFTIRQ);
	if (IS_ERR(queue->ib_cq)) {
		ret = PTR_ERR(queue->ib_cq);
		goto out;
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
	set_bit(NVME_RDMA_IB_QUEUE_ALLOCATED, &queue->flags);

	return 0;

out_destroy_qp:
	ib_destroy_qp(queue->qp);
out_destroy_ib_cq:
	ib_free_cq(queue->ib_cq);
out:
	return ret;
}

static int nvme_rdma_init_queue(struct nvme_rdma_ctrl *ctrl,
		int idx, size_t queue_size)
{
	struct nvme_rdma_queue *queue;
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

	queue->cm_error = -ETIMEDOUT;
	ret = rdma_resolve_addr(queue->cm_id, NULL, &ctrl->addr,
			NVME_RDMA_CONNECT_TIMEOUT_MS);
	if (ret) {
		dev_info(ctrl->ctrl.device,
			"rdma_resolve_addr failed (%d).\n", ret);
		goto out_destroy_cm_id;
	}

	ret = nvme_rdma_wait_for_cm(queue);
	if (ret) {
		dev_info(ctrl->ctrl.device,
			"rdma_resolve_addr wait failed (%d).\n", ret);
		goto out_destroy_cm_id;
	}

	clear_bit(NVME_RDMA_Q_DELETING, &queue->flags);
	set_bit(NVME_RDMA_Q_CONNECTED, &queue->flags);

	return 0;

out_destroy_cm_id:
	nvme_rdma_destroy_queue_ib(queue);
	rdma_destroy_id(queue->cm_id);
	return ret;
}

static void nvme_rdma_stop_queue(struct nvme_rdma_queue *queue)
{
	rdma_disconnect(queue->cm_id);
	ib_drain_qp(queue->qp);
}

static void nvme_rdma_free_queue(struct nvme_rdma_queue *queue)
{
	nvme_rdma_destroy_queue_ib(queue);
	rdma_destroy_id(queue->cm_id);
}

static void nvme_rdma_stop_and_free_queue(struct nvme_rdma_queue *queue)
{
	if (test_and_set_bit(NVME_RDMA_Q_DELETING, &queue->flags))
		return;
	nvme_rdma_stop_queue(queue);
	nvme_rdma_free_queue(queue);
}

static void nvme_rdma_free_io_queues(struct nvme_rdma_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->queue_count; i++)
		nvme_rdma_stop_and_free_queue(&ctrl->queues[i]);
}

static int nvme_rdma_connect_io_queues(struct nvme_rdma_ctrl *ctrl)
{
	int i, ret = 0;

	for (i = 1; i < ctrl->queue_count; i++) {
		ret = nvmf_connect_io_queue(&ctrl->ctrl, i);
		if (ret) {
			dev_info(ctrl->ctrl.device,
				"failed to connect i/o queue: %d\n", ret);
			goto out_free_queues;
		}
		set_bit(NVME_RDMA_Q_LIVE, &ctrl->queues[i].flags);
	}

	return 0;

out_free_queues:
	nvme_rdma_free_io_queues(ctrl);
	return ret;
}

static int nvme_rdma_init_io_queues(struct nvme_rdma_ctrl *ctrl)
{
	int i, ret;

	for (i = 1; i < ctrl->queue_count; i++) {
		ret = nvme_rdma_init_queue(ctrl, i,
					   ctrl->ctrl.opts->queue_size);
		if (ret) {
			dev_info(ctrl->ctrl.device,
				"failed to initialize i/o queue: %d\n", ret);
			goto out_free_queues;
		}
	}

	return 0;

out_free_queues:
	for (i--; i >= 1; i--)
		nvme_rdma_stop_and_free_queue(&ctrl->queues[i]);

	return ret;
}

static void nvme_rdma_destroy_admin_queue(struct nvme_rdma_ctrl *ctrl)
{
	nvme_rdma_free_qe(ctrl->queues[0].device->dev, &ctrl->async_event_sqe,
			sizeof(struct nvme_command), DMA_TO_DEVICE);
	nvme_rdma_stop_and_free_queue(&ctrl->queues[0]);
	blk_cleanup_queue(ctrl->ctrl.admin_q);
	blk_mq_free_tag_set(&ctrl->admin_tag_set);
	nvme_rdma_dev_put(ctrl->device);
}

static void nvme_rdma_free_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(nctrl);

	if (list_empty(&ctrl->list))
		goto free_ctrl;

	mutex_lock(&nvme_rdma_ctrl_mutex);
	list_del(&ctrl->list);
	mutex_unlock(&nvme_rdma_ctrl_mutex);

	kfree(ctrl->queues);
	nvmf_free_options(nctrl->opts);
free_ctrl:
	kfree(ctrl);
}

static void nvme_rdma_reconnect_ctrl_work(struct work_struct *work)
{
	struct nvme_rdma_ctrl *ctrl = container_of(to_delayed_work(work),
			struct nvme_rdma_ctrl, reconnect_work);
	bool changed;
	int ret;

	if (ctrl->queue_count > 1) {
		nvme_rdma_free_io_queues(ctrl);

		ret = blk_mq_reinit_tagset(&ctrl->tag_set);
		if (ret)
			goto requeue;
	}

	nvme_rdma_stop_and_free_queue(&ctrl->queues[0]);

	ret = blk_mq_reinit_tagset(&ctrl->admin_tag_set);
	if (ret)
		goto requeue;

	ret = nvme_rdma_init_queue(ctrl, 0, NVMF_AQ_DEPTH);
	if (ret)
		goto requeue;

	blk_mq_start_stopped_hw_queues(ctrl->ctrl.admin_q, true);

	ret = nvmf_connect_admin_queue(&ctrl->ctrl);
	if (ret)
		goto stop_admin_q;

	set_bit(NVME_RDMA_Q_LIVE, &ctrl->queues[0].flags);

	ret = nvme_enable_ctrl(&ctrl->ctrl, ctrl->cap);
	if (ret)
		goto stop_admin_q;

	nvme_start_keep_alive(&ctrl->ctrl);

	if (ctrl->queue_count > 1) {
		ret = nvme_rdma_init_io_queues(ctrl);
		if (ret)
			goto stop_admin_q;

		ret = nvme_rdma_connect_io_queues(ctrl);
		if (ret)
			goto stop_admin_q;
	}

	changed = nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_LIVE);
	WARN_ON_ONCE(!changed);

	if (ctrl->queue_count > 1) {
		nvme_start_queues(&ctrl->ctrl);
		nvme_queue_scan(&ctrl->ctrl);
		nvme_queue_async_events(&ctrl->ctrl);
	}

	dev_info(ctrl->ctrl.device, "Successfully reconnected\n");

	return;

stop_admin_q:
	blk_mq_stop_hw_queues(ctrl->ctrl.admin_q);
requeue:
	/* Make sure we are not resetting/deleting */
	if (ctrl->ctrl.state == NVME_CTRL_RECONNECTING) {
		dev_info(ctrl->ctrl.device,
			"Failed reconnect attempt, requeueing...\n");
		queue_delayed_work(nvme_rdma_wq, &ctrl->reconnect_work,
					ctrl->reconnect_delay * HZ);
	}
}

static void nvme_rdma_error_recovery_work(struct work_struct *work)
{
	struct nvme_rdma_ctrl *ctrl = container_of(work,
			struct nvme_rdma_ctrl, err_work);
	int i;

	nvme_stop_keep_alive(&ctrl->ctrl);

	for (i = 0; i < ctrl->queue_count; i++) {
		clear_bit(NVME_RDMA_Q_CONNECTED, &ctrl->queues[i].flags);
		clear_bit(NVME_RDMA_Q_LIVE, &ctrl->queues[i].flags);
	}

	if (ctrl->queue_count > 1)
		nvme_stop_queues(&ctrl->ctrl);
	blk_mq_stop_hw_queues(ctrl->ctrl.admin_q);

	/* We must take care of fastfail/requeue all our inflight requests */
	if (ctrl->queue_count > 1)
		blk_mq_tagset_busy_iter(&ctrl->tag_set,
					nvme_cancel_request, &ctrl->ctrl);
	blk_mq_tagset_busy_iter(&ctrl->admin_tag_set,
				nvme_cancel_request, &ctrl->ctrl);

	dev_info(ctrl->ctrl.device, "reconnecting in %d seconds\n",
		ctrl->reconnect_delay);

	queue_delayed_work(nvme_rdma_wq, &ctrl->reconnect_work,
				ctrl->reconnect_delay * HZ);
}

static void nvme_rdma_error_recovery(struct nvme_rdma_ctrl *ctrl)
{
	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_RECONNECTING))
		return;

	queue_work(nvme_rdma_wq, &ctrl->err_work);
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
	if (unlikely(wc->status != IB_WC_SUCCESS))
		nvme_rdma_wr_error(cq, wc, "LOCAL_INV");
}

static int nvme_rdma_inv_rkey(struct nvme_rdma_queue *queue,
		struct nvme_rdma_request *req)
{
	struct ib_send_wr *bad_wr;
	struct ib_send_wr wr = {
		.opcode		    = IB_WR_LOCAL_INV,
		.next		    = NULL,
		.num_sge	    = 0,
		.send_flags	    = 0,
		.ex.invalidate_rkey = req->mr->rkey,
	};

	req->reg_cqe.done = nvme_rdma_inv_rkey_done;
	wr.wr_cqe = &req->reg_cqe;

	return ib_post_send(queue->qp, &wr, &bad_wr);
}

static void nvme_rdma_unmap_data(struct nvme_rdma_queue *queue,
		struct request *rq)
{
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_ctrl *ctrl = queue->ctrl;
	struct nvme_rdma_device *dev = queue->device;
	struct ib_device *ibdev = dev->dev;
	int res;

	if (!blk_rq_bytes(rq))
		return;

	if (req->mr->need_inval) {
		res = nvme_rdma_inv_rkey(queue, req);
		if (res < 0) {
			dev_err(ctrl->ctrl.device,
				"Queueing INV WR for rkey %#x failed (%d)\n",
				req->mr->rkey, res);
			nvme_rdma_error_recovery(queue->ctrl);
		}
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
		struct nvme_rdma_request *req, struct nvme_command *c)
{
	struct nvme_sgl_desc *sg = &c->common.dptr.sgl;

	req->sge[1].addr = sg_dma_address(req->sg_table.sgl);
	req->sge[1].length = sg_dma_len(req->sg_table.sgl);
	req->sge[1].lkey = queue->device->pd->local_dma_lkey;

	sg->addr = cpu_to_le64(queue->ctrl->ctrl.icdoff);
	sg->length = cpu_to_le32(sg_dma_len(req->sg_table.sgl));
	sg->type = (NVME_SGL_FMT_DATA_DESC << 4) | NVME_SGL_FMT_OFFSET;

	req->inline_data = true;
	req->num_sge++;
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

	nr = ib_map_mr_sg(req->mr, req->sg_table.sgl, count, NULL, PAGE_SIZE);
	if (nr < count) {
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

	req->mr->need_inval = true;

	sg->addr = cpu_to_le64(req->mr->iova);
	put_unaligned_le24(req->mr->length, sg->length);
	put_unaligned_le32(req->mr->rkey, sg->key);
	sg->type = (NVME_KEY_SGL_FMT_DATA_DESC << 4) |
			NVME_SGL_FMT_INVALIDATE;

	return 0;
}

static int nvme_rdma_map_data(struct nvme_rdma_queue *queue,
		struct request *rq, unsigned int map_len,
		struct nvme_command *c)
{
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_device *dev = queue->device;
	struct ib_device *ibdev = dev->dev;
	int count, ret;

	req->num_sge = 1;
	req->inline_data = false;
	req->mr->need_inval = false;

	c->common.flags |= NVME_CMD_SGL_METABUF;

	if (!blk_rq_bytes(rq))
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
		sg_free_table_chained(&req->sg_table, true);
		return -EIO;
	}

	if (count == 1) {
		if (rq_data_dir(rq) == WRITE &&
		    map_len <= nvme_rdma_inline_data_size(queue) &&
		    nvme_rdma_queue_idx(queue))
			return nvme_rdma_map_sg_inline(queue, req, c);

		if (dev->pd->flags & IB_PD_UNSAFE_GLOBAL_RKEY)
			return nvme_rdma_map_sg_single(queue, req, c);
	}

	return nvme_rdma_map_sg_fr(queue, req, c, count);
}

static void nvme_rdma_send_done(struct ib_cq *cq, struct ib_wc *wc)
{
	if (unlikely(wc->status != IB_WC_SUCCESS))
		nvme_rdma_wr_error(cq, wc, "SEND");
}

static int nvme_rdma_post_send(struct nvme_rdma_queue *queue,
		struct nvme_rdma_qe *qe, struct ib_sge *sge, u32 num_sge,
		struct ib_send_wr *first, bool flush)
{
	struct ib_send_wr wr, *bad_wr;
	int ret;

	sge->addr   = qe->dma;
	sge->length = sizeof(struct nvme_command),
	sge->lkey   = queue->device->pd->local_dma_lkey;

	qe->cqe.done = nvme_rdma_send_done;

	wr.next       = NULL;
	wr.wr_cqe     = &qe->cqe;
	wr.sg_list    = sge;
	wr.num_sge    = num_sge;
	wr.opcode     = IB_WR_SEND;
	wr.send_flags = 0;

	/*
	 * Unsignalled send completions are another giant desaster in the
	 * IB Verbs spec:  If we don't regularly post signalled sends
	 * the send queue will fill up and only a QP reset will rescue us.
	 * Would have been way to obvious to handle this in hardware or
	 * at least the RDMA stack..
	 *
	 * This messy and racy code sniplet is copy and pasted from the iSER
	 * initiator, and the magic '32' comes from there as well.
	 *
	 * Always signal the flushes. The magic request used for the flush
	 * sequencer is not allocated in our driver's tagset and it's
	 * triggered to be freed by blk_cleanup_queue(). So we need to
	 * always mark it as signaled to ensure that the "wr_cqe", which is
	 * embeded in request's payload, is not freed when __ib_process_cq()
	 * calls wr_cqe->done().
	 */
	if ((++queue->sig_count % 32) == 0 || flush)
		wr.send_flags |= IB_SEND_SIGNALED;

	if (first)
		first->next = &wr;
	else
		first = &wr;

	ret = ib_post_send(queue->qp, first, &bad_wr);
	if (ret) {
		dev_err(queue->ctrl->ctrl.device,
			     "%s failed with error code %d\n", __func__, ret);
	}
	return ret;
}

static int nvme_rdma_post_recv(struct nvme_rdma_queue *queue,
		struct nvme_rdma_qe *qe)
{
	struct ib_recv_wr wr, *bad_wr;
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

	ret = ib_post_recv(queue->qp, &wr, &bad_wr);
	if (ret) {
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

static void nvme_rdma_submit_async_event(struct nvme_ctrl *arg, int aer_idx)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(arg);
	struct nvme_rdma_queue *queue = &ctrl->queues[0];
	struct ib_device *dev = queue->device->dev;
	struct nvme_rdma_qe *sqe = &ctrl->async_event_sqe;
	struct nvme_command *cmd = sqe->data;
	struct ib_sge sge;
	int ret;

	if (WARN_ON_ONCE(aer_idx != 0))
		return;

	ib_dma_sync_single_for_cpu(dev, sqe->dma, sizeof(*cmd), DMA_TO_DEVICE);

	memset(cmd, 0, sizeof(*cmd));
	cmd->common.opcode = nvme_admin_async_event;
	cmd->common.command_id = NVME_RDMA_AQ_BLKMQ_DEPTH;
	cmd->common.flags |= NVME_CMD_SGL_METABUF;
	nvme_rdma_set_sg_null(cmd);

	ib_dma_sync_single_for_device(dev, sqe->dma, sizeof(*cmd),
			DMA_TO_DEVICE);

	ret = nvme_rdma_post_send(queue, sqe, &sge, 1, NULL, false);
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

	if (rq->tag == tag)
		ret = 1;

	if ((wc->wc_flags & IB_WC_WITH_INVALIDATE) &&
	    wc->ex.invalidate_rkey == req->mr->rkey)
		req->mr->need_inval = false;

	req->req.result = cqe->result;
	blk_mq_complete_request(rq, le16_to_cpu(cqe->status) >> 1);
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
			cqe->command_id >= NVME_RDMA_AQ_BLKMQ_DEPTH))
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
	struct nvme_rdma_device *dev;
	int ret;

	dev = nvme_rdma_find_get_device(queue->cm_id);
	if (!dev) {
		dev_err(queue->cm_id->device->dma_device,
			"no client data found!\n");
		return -ECONNREFUSED;
	}

	ret = nvme_rdma_create_queue_ib(queue, dev);
	if (ret) {
		nvme_rdma_dev_put(dev);
		goto out;
	}

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
out:
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
		priv.hrqsize = cpu_to_le16(NVMF_AQ_DEPTH);
		priv.hsqsize = cpu_to_le16(NVMF_AQ_DEPTH - 1);
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
		cm_error = nvme_rdma_conn_rejected(queue, ev);
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
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

	/* queue error recovery */
	nvme_rdma_error_recovery(req->queue->ctrl);

	/* fail with DNR on cmd timeout */
	rq->errors = NVME_SC_ABORT_REQ | NVME_SC_DNR;

	return BLK_EH_HANDLED;
}

/*
 * We cannot accept any other command until the Connect command has completed.
 */
static inline bool nvme_rdma_queue_is_ready(struct nvme_rdma_queue *queue,
		struct request *rq)
{
	if (unlikely(!test_bit(NVME_RDMA_Q_LIVE, &queue->flags))) {
		struct nvme_command *cmd = (struct nvme_command *)rq->cmd;

		if (rq->cmd_type != REQ_TYPE_DRV_PRIV ||
		    cmd->common.opcode != nvme_fabrics_command ||
		    cmd->fabrics.fctype != nvme_fabrics_type_connect)
			return false;
	}

	return true;
}

static int nvme_rdma_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_rdma_queue *queue = hctx->driver_data;
	struct request *rq = bd->rq;
	struct nvme_rdma_request *req = blk_mq_rq_to_pdu(rq);
	struct nvme_rdma_qe *sqe = &req->sqe;
	struct nvme_command *c = sqe->data;
	bool flush = false;
	struct ib_device *dev;
	unsigned int map_len;
	int ret;

	WARN_ON_ONCE(rq->tag < 0);

	if (!nvme_rdma_queue_is_ready(queue, rq))
		return BLK_MQ_RQ_QUEUE_BUSY;

	dev = queue->device->dev;
	ib_dma_sync_single_for_cpu(dev, sqe->dma,
			sizeof(struct nvme_command), DMA_TO_DEVICE);

	ret = nvme_setup_cmd(ns, rq, c);
	if (ret != BLK_MQ_RQ_QUEUE_OK)
		return ret;

	blk_mq_start_request(rq);

	map_len = nvme_map_len(rq);
	ret = nvme_rdma_map_data(queue, rq, map_len, c);
	if (ret < 0) {
		dev_err(queue->ctrl->ctrl.device,
			     "Failed to map data (%d)\n", ret);
		nvme_cleanup_cmd(rq);
		goto err;
	}

	ib_dma_sync_single_for_device(dev, sqe->dma,
			sizeof(struct nvme_command), DMA_TO_DEVICE);

	if (rq->cmd_type == REQ_TYPE_FS && req_op(rq) == REQ_OP_FLUSH)
		flush = true;
	ret = nvme_rdma_post_send(queue, sqe, req->sge, req->num_sge,
			req->mr->need_inval ? &req->reg_wr.wr : NULL, flush);
	if (ret) {
		nvme_rdma_unmap_data(queue, rq);
		goto err;
	}

	return BLK_MQ_RQ_QUEUE_OK;
err:
	return (ret == -ENOMEM || ret == -EAGAIN) ?
		BLK_MQ_RQ_QUEUE_BUSY : BLK_MQ_RQ_QUEUE_ERROR;
}

static int nvme_rdma_poll(struct blk_mq_hw_ctx *hctx, unsigned int tag)
{
	struct nvme_rdma_queue *queue = hctx->driver_data;
	struct ib_cq *cq = queue->ib_cq;
	struct ib_wc wc;
	int found = 0;

	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
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
	struct nvme_rdma_queue *queue = req->queue;
	int error = 0;

	nvme_rdma_unmap_data(queue, rq);

	if (unlikely(rq->errors)) {
		if (nvme_req_needs_retry(rq, rq->errors)) {
			nvme_requeue_req(rq);
			return;
		}

		if (rq->cmd_type == REQ_TYPE_DRV_PRIV)
			error = rq->errors;
		else
			error = nvme_error_status(rq->errors);
	}

	blk_mq_end_request(rq, error);
}

static struct blk_mq_ops nvme_rdma_mq_ops = {
	.queue_rq	= nvme_rdma_queue_rq,
	.complete	= nvme_rdma_complete_rq,
	.init_request	= nvme_rdma_init_request,
	.exit_request	= nvme_rdma_exit_request,
	.reinit_request	= nvme_rdma_reinit_request,
	.init_hctx	= nvme_rdma_init_hctx,
	.poll		= nvme_rdma_poll,
	.timeout	= nvme_rdma_timeout,
};

static struct blk_mq_ops nvme_rdma_admin_mq_ops = {
	.queue_rq	= nvme_rdma_queue_rq,
	.complete	= nvme_rdma_complete_rq,
	.init_request	= nvme_rdma_init_admin_request,
	.exit_request	= nvme_rdma_exit_admin_request,
	.reinit_request	= nvme_rdma_reinit_request,
	.init_hctx	= nvme_rdma_init_admin_hctx,
	.timeout	= nvme_rdma_timeout,
};

static int nvme_rdma_configure_admin_queue(struct nvme_rdma_ctrl *ctrl)
{
	int error;

	error = nvme_rdma_init_queue(ctrl, 0, NVMF_AQ_DEPTH);
	if (error)
		return error;

	ctrl->device = ctrl->queues[0].device;

	/*
	 * We need a reference on the device as long as the tag_set is alive,
	 * as the MRs in the request structures need a valid ib_device.
	 */
	error = -EINVAL;
	if (!nvme_rdma_dev_get(ctrl->device))
		goto out_free_queue;

	ctrl->max_fr_pages = min_t(u32, NVME_RDMA_MAX_SEGMENTS,
		ctrl->device->dev->attrs.max_fast_reg_page_list_len);

	memset(&ctrl->admin_tag_set, 0, sizeof(ctrl->admin_tag_set));
	ctrl->admin_tag_set.ops = &nvme_rdma_admin_mq_ops;
	ctrl->admin_tag_set.queue_depth = NVME_RDMA_AQ_BLKMQ_DEPTH;
	ctrl->admin_tag_set.reserved_tags = 2; /* connect + keep-alive */
	ctrl->admin_tag_set.numa_node = NUMA_NO_NODE;
	ctrl->admin_tag_set.cmd_size = sizeof(struct nvme_rdma_request) +
		SG_CHUNK_SIZE * sizeof(struct scatterlist);
	ctrl->admin_tag_set.driver_data = ctrl;
	ctrl->admin_tag_set.nr_hw_queues = 1;
	ctrl->admin_tag_set.timeout = ADMIN_TIMEOUT;

	error = blk_mq_alloc_tag_set(&ctrl->admin_tag_set);
	if (error)
		goto out_put_dev;

	ctrl->ctrl.admin_q = blk_mq_init_queue(&ctrl->admin_tag_set);
	if (IS_ERR(ctrl->ctrl.admin_q)) {
		error = PTR_ERR(ctrl->ctrl.admin_q);
		goto out_free_tagset;
	}

	error = nvmf_connect_admin_queue(&ctrl->ctrl);
	if (error)
		goto out_cleanup_queue;

	set_bit(NVME_RDMA_Q_LIVE, &ctrl->queues[0].flags);

	error = nvmf_reg_read64(&ctrl->ctrl, NVME_REG_CAP, &ctrl->cap);
	if (error) {
		dev_err(ctrl->ctrl.device,
			"prop_get NVME_REG_CAP failed\n");
		goto out_cleanup_queue;
	}

	ctrl->ctrl.sqsize =
		min_t(int, NVME_CAP_MQES(ctrl->cap) + 1, ctrl->ctrl.sqsize);

	error = nvme_enable_ctrl(&ctrl->ctrl, ctrl->cap);
	if (error)
		goto out_cleanup_queue;

	ctrl->ctrl.max_hw_sectors =
		(ctrl->max_fr_pages - 1) << (PAGE_SHIFT - 9);

	error = nvme_init_identify(&ctrl->ctrl);
	if (error)
		goto out_cleanup_queue;

	error = nvme_rdma_alloc_qe(ctrl->queues[0].device->dev,
			&ctrl->async_event_sqe, sizeof(struct nvme_command),
			DMA_TO_DEVICE);
	if (error)
		goto out_cleanup_queue;

	nvme_start_keep_alive(&ctrl->ctrl);

	return 0;

out_cleanup_queue:
	blk_cleanup_queue(ctrl->ctrl.admin_q);
out_free_tagset:
	/* disconnect and drain the queue before freeing the tagset */
	nvme_rdma_stop_queue(&ctrl->queues[0]);
	blk_mq_free_tag_set(&ctrl->admin_tag_set);
out_put_dev:
	nvme_rdma_dev_put(ctrl->device);
out_free_queue:
	nvme_rdma_free_queue(&ctrl->queues[0]);
	return error;
}

static void nvme_rdma_shutdown_ctrl(struct nvme_rdma_ctrl *ctrl)
{
	nvme_stop_keep_alive(&ctrl->ctrl);
	cancel_work_sync(&ctrl->err_work);
	cancel_delayed_work_sync(&ctrl->reconnect_work);

	if (ctrl->queue_count > 1) {
		nvme_stop_queues(&ctrl->ctrl);
		blk_mq_tagset_busy_iter(&ctrl->tag_set,
					nvme_cancel_request, &ctrl->ctrl);
		nvme_rdma_free_io_queues(ctrl);
	}

	if (test_bit(NVME_RDMA_Q_CONNECTED, &ctrl->queues[0].flags))
		nvme_shutdown_ctrl(&ctrl->ctrl);

	blk_mq_stop_hw_queues(ctrl->ctrl.admin_q);
	blk_mq_tagset_busy_iter(&ctrl->admin_tag_set,
				nvme_cancel_request, &ctrl->ctrl);
	nvme_rdma_destroy_admin_queue(ctrl);
}

static void __nvme_rdma_remove_ctrl(struct nvme_rdma_ctrl *ctrl, bool shutdown)
{
	nvme_uninit_ctrl(&ctrl->ctrl);
	if (shutdown)
		nvme_rdma_shutdown_ctrl(ctrl);

	if (ctrl->ctrl.tagset) {
		blk_cleanup_queue(ctrl->ctrl.connect_q);
		blk_mq_free_tag_set(&ctrl->tag_set);
		nvme_rdma_dev_put(ctrl->device);
	}

	nvme_put_ctrl(&ctrl->ctrl);
}

static void nvme_rdma_del_ctrl_work(struct work_struct *work)
{
	struct nvme_rdma_ctrl *ctrl = container_of(work,
				struct nvme_rdma_ctrl, delete_work);

	__nvme_rdma_remove_ctrl(ctrl, true);
}

static int __nvme_rdma_del_ctrl(struct nvme_rdma_ctrl *ctrl)
{
	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_DELETING))
		return -EBUSY;

	if (!queue_work(nvme_rdma_wq, &ctrl->delete_work))
		return -EBUSY;

	return 0;
}

static int nvme_rdma_del_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(nctrl);
	int ret = 0;

	/*
	 * Keep a reference until all work is flushed since
	 * __nvme_rdma_del_ctrl can free the ctrl mem
	 */
	if (!kref_get_unless_zero(&ctrl->ctrl.kref))
		return -EBUSY;
	ret = __nvme_rdma_del_ctrl(ctrl);
	if (!ret)
		flush_work(&ctrl->delete_work);
	nvme_put_ctrl(&ctrl->ctrl);
	return ret;
}

static void nvme_rdma_remove_ctrl_work(struct work_struct *work)
{
	struct nvme_rdma_ctrl *ctrl = container_of(work,
				struct nvme_rdma_ctrl, delete_work);

	__nvme_rdma_remove_ctrl(ctrl, false);
}

static void nvme_rdma_reset_ctrl_work(struct work_struct *work)
{
	struct nvme_rdma_ctrl *ctrl = container_of(work,
					struct nvme_rdma_ctrl, reset_work);
	int ret;
	bool changed;

	nvme_rdma_shutdown_ctrl(ctrl);

	ret = nvme_rdma_configure_admin_queue(ctrl);
	if (ret) {
		/* ctrl is already shutdown, just remove the ctrl */
		INIT_WORK(&ctrl->delete_work, nvme_rdma_remove_ctrl_work);
		goto del_dead_ctrl;
	}

	if (ctrl->queue_count > 1) {
		ret = blk_mq_reinit_tagset(&ctrl->tag_set);
		if (ret)
			goto del_dead_ctrl;

		ret = nvme_rdma_init_io_queues(ctrl);
		if (ret)
			goto del_dead_ctrl;

		ret = nvme_rdma_connect_io_queues(ctrl);
		if (ret)
			goto del_dead_ctrl;
	}

	changed = nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_LIVE);
	WARN_ON_ONCE(!changed);

	if (ctrl->queue_count > 1) {
		nvme_start_queues(&ctrl->ctrl);
		nvme_queue_scan(&ctrl->ctrl);
		nvme_queue_async_events(&ctrl->ctrl);
	}

	return;

del_dead_ctrl:
	/* Deleting this dead controller... */
	dev_warn(ctrl->ctrl.device, "Removing after reset failure\n");
	WARN_ON(!queue_work(nvme_rdma_wq, &ctrl->delete_work));
}

static int nvme_rdma_reset_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_rdma_ctrl *ctrl = to_rdma_ctrl(nctrl);

	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_RESETTING))
		return -EBUSY;

	if (!queue_work(nvme_rdma_wq, &ctrl->reset_work))
		return -EBUSY;

	flush_work(&ctrl->reset_work);

	return 0;
}

static const struct nvme_ctrl_ops nvme_rdma_ctrl_ops = {
	.name			= "rdma",
	.module			= THIS_MODULE,
	.is_fabrics		= true,
	.reg_read32		= nvmf_reg_read32,
	.reg_read64		= nvmf_reg_read64,
	.reg_write32		= nvmf_reg_write32,
	.reset_ctrl		= nvme_rdma_reset_ctrl,
	.free_ctrl		= nvme_rdma_free_ctrl,
	.submit_async_event	= nvme_rdma_submit_async_event,
	.delete_ctrl		= nvme_rdma_del_ctrl,
	.get_subsysnqn		= nvmf_get_subsysnqn,
	.get_address		= nvmf_get_address,
};

static int nvme_rdma_create_io_queues(struct nvme_rdma_ctrl *ctrl)
{
	struct nvmf_ctrl_options *opts = ctrl->ctrl.opts;
	int ret;

	ret = nvme_set_queue_count(&ctrl->ctrl, &opts->nr_io_queues);
	if (ret)
		return ret;

	ctrl->queue_count = opts->nr_io_queues + 1;
	if (ctrl->queue_count < 2)
		return 0;

	dev_info(ctrl->ctrl.device,
		"creating %d I/O queues.\n", opts->nr_io_queues);

	ret = nvme_rdma_init_io_queues(ctrl);
	if (ret)
		return ret;

	/*
	 * We need a reference on the device as long as the tag_set is alive,
	 * as the MRs in the request structures need a valid ib_device.
	 */
	ret = -EINVAL;
	if (!nvme_rdma_dev_get(ctrl->device))
		goto out_free_io_queues;

	memset(&ctrl->tag_set, 0, sizeof(ctrl->tag_set));
	ctrl->tag_set.ops = &nvme_rdma_mq_ops;
	ctrl->tag_set.queue_depth = ctrl->ctrl.opts->queue_size;
	ctrl->tag_set.reserved_tags = 1; /* fabric connect */
	ctrl->tag_set.numa_node = NUMA_NO_NODE;
	ctrl->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	ctrl->tag_set.cmd_size = sizeof(struct nvme_rdma_request) +
		SG_CHUNK_SIZE * sizeof(struct scatterlist);
	ctrl->tag_set.driver_data = ctrl;
	ctrl->tag_set.nr_hw_queues = ctrl->queue_count - 1;
	ctrl->tag_set.timeout = NVME_IO_TIMEOUT;

	ret = blk_mq_alloc_tag_set(&ctrl->tag_set);
	if (ret)
		goto out_put_dev;
	ctrl->ctrl.tagset = &ctrl->tag_set;

	ctrl->ctrl.connect_q = blk_mq_init_queue(&ctrl->tag_set);
	if (IS_ERR(ctrl->ctrl.connect_q)) {
		ret = PTR_ERR(ctrl->ctrl.connect_q);
		goto out_free_tag_set;
	}

	ret = nvme_rdma_connect_io_queues(ctrl);
	if (ret)
		goto out_cleanup_connect_q;

	return 0;

out_cleanup_connect_q:
	blk_cleanup_queue(ctrl->ctrl.connect_q);
out_free_tag_set:
	blk_mq_free_tag_set(&ctrl->tag_set);
out_put_dev:
	nvme_rdma_dev_put(ctrl->device);
out_free_io_queues:
	nvme_rdma_free_io_queues(ctrl);
	return ret;
}

static int nvme_rdma_parse_ipaddr(struct sockaddr_in *in_addr, char *p)
{
	u8 *addr = (u8 *)&in_addr->sin_addr.s_addr;
	size_t buflen = strlen(p);

	/* XXX: handle IPv6 addresses */

	if (buflen > INET_ADDRSTRLEN)
		return -EINVAL;
	if (in4_pton(p, buflen, addr, '\0', NULL) == 0)
		return -EINVAL;
	in_addr->sin_family = AF_INET;
	return 0;
}

static struct nvme_ctrl *nvme_rdma_create_ctrl(struct device *dev,
		struct nvmf_ctrl_options *opts)
{
	struct nvme_rdma_ctrl *ctrl;
	int ret;
	bool changed;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-ENOMEM);
	ctrl->ctrl.opts = opts;
	INIT_LIST_HEAD(&ctrl->list);

	ret = nvme_rdma_parse_ipaddr(&ctrl->addr_in, opts->traddr);
	if (ret) {
		pr_err("malformed IP address passed: %s\n", opts->traddr);
		goto out_free_ctrl;
	}

	if (opts->mask & NVMF_OPT_TRSVCID) {
		u16 port;

		ret = kstrtou16(opts->trsvcid, 0, &port);
		if (ret)
			goto out_free_ctrl;

		ctrl->addr_in.sin_port = cpu_to_be16(port);
	} else {
		ctrl->addr_in.sin_port = cpu_to_be16(NVME_RDMA_IP_PORT);
	}

	ret = nvme_init_ctrl(&ctrl->ctrl, dev, &nvme_rdma_ctrl_ops,
				0 /* no quirks, we're perfect! */);
	if (ret)
		goto out_free_ctrl;

	ctrl->reconnect_delay = opts->reconnect_delay;
	INIT_DELAYED_WORK(&ctrl->reconnect_work,
			nvme_rdma_reconnect_ctrl_work);
	INIT_WORK(&ctrl->err_work, nvme_rdma_error_recovery_work);
	INIT_WORK(&ctrl->delete_work, nvme_rdma_del_ctrl_work);
	INIT_WORK(&ctrl->reset_work, nvme_rdma_reset_ctrl_work);
	spin_lock_init(&ctrl->lock);

	ctrl->queue_count = opts->nr_io_queues + 1; /* +1 for admin queue */
	ctrl->ctrl.sqsize = opts->queue_size - 1;
	ctrl->ctrl.kato = opts->kato;

	ret = -ENOMEM;
	ctrl->queues = kcalloc(ctrl->queue_count, sizeof(*ctrl->queues),
				GFP_KERNEL);
	if (!ctrl->queues)
		goto out_uninit_ctrl;

	ret = nvme_rdma_configure_admin_queue(ctrl);
	if (ret)
		goto out_kfree_queues;

	/* sanity check icdoff */
	if (ctrl->ctrl.icdoff) {
		dev_err(ctrl->ctrl.device, "icdoff is not supported!\n");
		goto out_remove_admin_queue;
	}

	/* sanity check keyed sgls */
	if (!(ctrl->ctrl.sgls & (1 << 20))) {
		dev_err(ctrl->ctrl.device, "Mandatory keyed sgls are not support\n");
		goto out_remove_admin_queue;
	}

	if (opts->queue_size > ctrl->ctrl.maxcmd) {
		/* warn if maxcmd is lower than queue_size */
		dev_warn(ctrl->ctrl.device,
			"queue_size %zu > ctrl maxcmd %u, clamping down\n",
			opts->queue_size, ctrl->ctrl.maxcmd);
		opts->queue_size = ctrl->ctrl.maxcmd;
	}

	if (opts->queue_size > ctrl->ctrl.sqsize + 1) {
		/* warn if sqsize is lower than queue_size */
		dev_warn(ctrl->ctrl.device,
			"queue_size %zu > ctrl sqsize %u, clamping down\n",
			opts->queue_size, ctrl->ctrl.sqsize + 1);
		opts->queue_size = ctrl->ctrl.sqsize + 1;
	}

	if (opts->nr_io_queues) {
		ret = nvme_rdma_create_io_queues(ctrl);
		if (ret)
			goto out_remove_admin_queue;
	}

	changed = nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_LIVE);
	WARN_ON_ONCE(!changed);

	dev_info(ctrl->ctrl.device, "new ctrl: NQN \"%s\", addr %pISp\n",
		ctrl->ctrl.opts->subsysnqn, &ctrl->addr);

	kref_get(&ctrl->ctrl.kref);

	mutex_lock(&nvme_rdma_ctrl_mutex);
	list_add_tail(&ctrl->list, &nvme_rdma_ctrl_list);
	mutex_unlock(&nvme_rdma_ctrl_mutex);

	if (opts->nr_io_queues) {
		nvme_queue_scan(&ctrl->ctrl);
		nvme_queue_async_events(&ctrl->ctrl);
	}

	return &ctrl->ctrl;

out_remove_admin_queue:
	nvme_stop_keep_alive(&ctrl->ctrl);
	nvme_rdma_destroy_admin_queue(ctrl);
out_kfree_queues:
	kfree(ctrl->queues);
out_uninit_ctrl:
	nvme_uninit_ctrl(&ctrl->ctrl);
	nvme_put_ctrl(&ctrl->ctrl);
	if (ret > 0)
		ret = -EIO;
	return ERR_PTR(ret);
out_free_ctrl:
	kfree(ctrl);
	return ERR_PTR(ret);
}

static struct nvmf_transport_ops nvme_rdma_transport = {
	.name		= "rdma",
	.required_opts	= NVMF_OPT_TRADDR,
	.allowed_opts	= NVMF_OPT_TRSVCID | NVMF_OPT_RECONNECT_DELAY,
	.create_ctrl	= nvme_rdma_create_ctrl,
};

static void nvme_rdma_add_one(struct ib_device *ib_device)
{
}

static void nvme_rdma_remove_one(struct ib_device *ib_device, void *client_data)
{
	struct nvme_rdma_ctrl *ctrl;

	/* Delete all controllers using this device */
	mutex_lock(&nvme_rdma_ctrl_mutex);
	list_for_each_entry(ctrl, &nvme_rdma_ctrl_list, list) {
		if (ctrl->device->dev != ib_device)
			continue;
		dev_info(ctrl->ctrl.device,
			"Removing ctrl: NQN \"%s\", addr %pISp\n",
			ctrl->ctrl.opts->subsysnqn, &ctrl->addr);
		__nvme_rdma_del_ctrl(ctrl);
	}
	mutex_unlock(&nvme_rdma_ctrl_mutex);

	flush_workqueue(nvme_rdma_wq);
}

static struct ib_client nvme_rdma_ib_client = {
	.name   = "nvme_rdma",
	.add = nvme_rdma_add_one,
	.remove = nvme_rdma_remove_one
};

static int __init nvme_rdma_init_module(void)
{
	int ret;

	nvme_rdma_wq = create_workqueue("nvme_rdma_wq");
	if (!nvme_rdma_wq)
		return -ENOMEM;

	ret = ib_register_client(&nvme_rdma_ib_client);
	if (ret) {
		destroy_workqueue(nvme_rdma_wq);
		return ret;
	}

	nvmf_register_transport(&nvme_rdma_transport);
	return 0;
}

static void __exit nvme_rdma_cleanup_module(void)
{
	nvmf_unregister_transport(&nvme_rdma_transport);
	ib_unregister_client(&nvme_rdma_ib_client);
	destroy_workqueue(nvme_rdma_wq);
}

module_init(nvme_rdma_init_module);
module_exit(nvme_rdma_cleanup_module);

MODULE_LICENSE("GPL v2");
