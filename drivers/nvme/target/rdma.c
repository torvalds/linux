/*
 * NVMe over Fabrics RDMA target.
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
#include <linux/atomic.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/nvme.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/inet.h>
#include <asm/unaligned.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <rdma/rw.h>

#include <linux/nvme-rdma.h>
#include "nvmet.h"

/*
 * We allow up to a page of inline data to go with the SQE
 */
#define NVMET_RDMA_INLINE_DATA_SIZE	PAGE_SIZE

struct nvmet_rdma_cmd {
	struct ib_sge		sge[2];
	struct ib_cqe		cqe;
	struct ib_recv_wr	wr;
	struct scatterlist	inline_sg;
	struct page		*inline_page;
	struct nvme_command     *nvme_cmd;
	struct nvmet_rdma_queue	*queue;
};

enum {
	NVMET_RDMA_REQ_INLINE_DATA	= (1 << 0),
	NVMET_RDMA_REQ_INVALIDATE_RKEY	= (1 << 1),
};

struct nvmet_rdma_rsp {
	struct ib_sge		send_sge;
	struct ib_cqe		send_cqe;
	struct ib_send_wr	send_wr;

	struct nvmet_rdma_cmd	*cmd;
	struct nvmet_rdma_queue	*queue;

	struct ib_cqe		read_cqe;
	struct rdma_rw_ctx	rw;

	struct nvmet_req	req;

	u8			n_rdma;
	u32			flags;
	u32			invalidate_rkey;

	struct list_head	wait_list;
	struct list_head	free_list;
};

enum nvmet_rdma_queue_state {
	NVMET_RDMA_Q_CONNECTING,
	NVMET_RDMA_Q_LIVE,
	NVMET_RDMA_Q_DISCONNECTING,
	NVMET_RDMA_IN_DEVICE_REMOVAL,
};

struct nvmet_rdma_queue {
	struct rdma_cm_id	*cm_id;
	struct nvmet_port	*port;
	struct ib_cq		*cq;
	atomic_t		sq_wr_avail;
	struct nvmet_rdma_device *dev;
	spinlock_t		state_lock;
	enum nvmet_rdma_queue_state state;
	struct nvmet_cq		nvme_cq;
	struct nvmet_sq		nvme_sq;

	struct nvmet_rdma_rsp	*rsps;
	struct list_head	free_rsps;
	spinlock_t		rsps_lock;
	struct nvmet_rdma_cmd	*cmds;

	struct work_struct	release_work;
	struct list_head	rsp_wait_list;
	struct list_head	rsp_wr_wait_list;
	spinlock_t		rsp_wr_wait_lock;

	int			idx;
	int			host_qid;
	int			recv_queue_size;
	int			send_queue_size;

	struct list_head	queue_list;
};

struct nvmet_rdma_device {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct ib_srq		*srq;
	struct nvmet_rdma_cmd	*srq_cmds;
	size_t			srq_size;
	struct kref		ref;
	struct list_head	entry;
};

static bool nvmet_rdma_use_srq;
module_param_named(use_srq, nvmet_rdma_use_srq, bool, 0444);
MODULE_PARM_DESC(use_srq, "Use shared receive queue.");

static DEFINE_IDA(nvmet_rdma_queue_ida);
static LIST_HEAD(nvmet_rdma_queue_list);
static DEFINE_MUTEX(nvmet_rdma_queue_mutex);

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_mutex);

static bool nvmet_rdma_execute_command(struct nvmet_rdma_rsp *rsp);
static void nvmet_rdma_send_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_recv_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_read_data_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_qp_event(struct ib_event *event, void *priv);
static void nvmet_rdma_queue_disconnect(struct nvmet_rdma_queue *queue);

static struct nvmet_fabrics_ops nvmet_rdma_ops;

/* XXX: really should move to a generic header sooner or later.. */
static inline u32 get_unaligned_le24(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16;
}

static inline bool nvmet_rdma_need_data_in(struct nvmet_rdma_rsp *rsp)
{
	return nvme_is_write(rsp->req.cmd) &&
		rsp->req.data_len &&
		!(rsp->flags & NVMET_RDMA_REQ_INLINE_DATA);
}

static inline bool nvmet_rdma_need_data_out(struct nvmet_rdma_rsp *rsp)
{
	return !nvme_is_write(rsp->req.cmd) &&
		rsp->req.data_len &&
		!rsp->req.rsp->status &&
		!(rsp->flags & NVMET_RDMA_REQ_INLINE_DATA);
}

static inline struct nvmet_rdma_rsp *
nvmet_rdma_get_rsp(struct nvmet_rdma_queue *queue)
{
	struct nvmet_rdma_rsp *rsp;
	unsigned long flags;

	spin_lock_irqsave(&queue->rsps_lock, flags);
	rsp = list_first_entry(&queue->free_rsps,
				struct nvmet_rdma_rsp, free_list);
	list_del(&rsp->free_list);
	spin_unlock_irqrestore(&queue->rsps_lock, flags);

	return rsp;
}

static inline void
nvmet_rdma_put_rsp(struct nvmet_rdma_rsp *rsp)
{
	unsigned long flags;

	spin_lock_irqsave(&rsp->queue->rsps_lock, flags);
	list_add_tail(&rsp->free_list, &rsp->queue->free_rsps);
	spin_unlock_irqrestore(&rsp->queue->rsps_lock, flags);
}

static void nvmet_rdma_free_sgl(struct scatterlist *sgl, unsigned int nents)
{
	struct scatterlist *sg;
	int count;

	if (!sgl || !nents)
		return;

	for_each_sg(sgl, sg, nents, count)
		__free_page(sg_page(sg));
	kfree(sgl);
}

static int nvmet_rdma_alloc_sgl(struct scatterlist **sgl, unsigned int *nents,
		u32 length)
{
	struct scatterlist *sg;
	struct page *page;
	unsigned int nent;
	int i = 0;

	nent = DIV_ROUND_UP(length, PAGE_SIZE);
	sg = kmalloc_array(nent, sizeof(struct scatterlist), GFP_KERNEL);
	if (!sg)
		goto out;

	sg_init_table(sg, nent);

	while (length) {
		u32 page_len = min_t(u32, length, PAGE_SIZE);

		page = alloc_page(GFP_KERNEL);
		if (!page)
			goto out_free_pages;

		sg_set_page(&sg[i], page, page_len, 0);
		length -= page_len;
		i++;
	}
	*sgl = sg;
	*nents = nent;
	return 0;

out_free_pages:
	while (i > 0) {
		i--;
		__free_page(sg_page(&sg[i]));
	}
	kfree(sg);
out:
	return NVME_SC_INTERNAL;
}

static int nvmet_rdma_alloc_cmd(struct nvmet_rdma_device *ndev,
			struct nvmet_rdma_cmd *c, bool admin)
{
	/* NVMe command / RDMA RECV */
	c->nvme_cmd = kmalloc(sizeof(*c->nvme_cmd), GFP_KERNEL);
	if (!c->nvme_cmd)
		goto out;

	c->sge[0].addr = ib_dma_map_single(ndev->device, c->nvme_cmd,
			sizeof(*c->nvme_cmd), DMA_FROM_DEVICE);
	if (ib_dma_mapping_error(ndev->device, c->sge[0].addr))
		goto out_free_cmd;

	c->sge[0].length = sizeof(*c->nvme_cmd);
	c->sge[0].lkey = ndev->pd->local_dma_lkey;

	if (!admin) {
		c->inline_page = alloc_pages(GFP_KERNEL,
				get_order(NVMET_RDMA_INLINE_DATA_SIZE));
		if (!c->inline_page)
			goto out_unmap_cmd;
		c->sge[1].addr = ib_dma_map_page(ndev->device,
				c->inline_page, 0, NVMET_RDMA_INLINE_DATA_SIZE,
				DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(ndev->device, c->sge[1].addr))
			goto out_free_inline_page;
		c->sge[1].length = NVMET_RDMA_INLINE_DATA_SIZE;
		c->sge[1].lkey = ndev->pd->local_dma_lkey;
	}

	c->cqe.done = nvmet_rdma_recv_done;

	c->wr.wr_cqe = &c->cqe;
	c->wr.sg_list = c->sge;
	c->wr.num_sge = admin ? 1 : 2;

	return 0;

out_free_inline_page:
	if (!admin) {
		__free_pages(c->inline_page,
				get_order(NVMET_RDMA_INLINE_DATA_SIZE));
	}
out_unmap_cmd:
	ib_dma_unmap_single(ndev->device, c->sge[0].addr,
			sizeof(*c->nvme_cmd), DMA_FROM_DEVICE);
out_free_cmd:
	kfree(c->nvme_cmd);

out:
	return -ENOMEM;
}

static void nvmet_rdma_free_cmd(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_cmd *c, bool admin)
{
	if (!admin) {
		ib_dma_unmap_page(ndev->device, c->sge[1].addr,
				NVMET_RDMA_INLINE_DATA_SIZE, DMA_FROM_DEVICE);
		__free_pages(c->inline_page,
				get_order(NVMET_RDMA_INLINE_DATA_SIZE));
	}
	ib_dma_unmap_single(ndev->device, c->sge[0].addr,
				sizeof(*c->nvme_cmd), DMA_FROM_DEVICE);
	kfree(c->nvme_cmd);
}

static struct nvmet_rdma_cmd *
nvmet_rdma_alloc_cmds(struct nvmet_rdma_device *ndev,
		int nr_cmds, bool admin)
{
	struct nvmet_rdma_cmd *cmds;
	int ret = -EINVAL, i;

	cmds = kcalloc(nr_cmds, sizeof(struct nvmet_rdma_cmd), GFP_KERNEL);
	if (!cmds)
		goto out;

	for (i = 0; i < nr_cmds; i++) {
		ret = nvmet_rdma_alloc_cmd(ndev, cmds + i, admin);
		if (ret)
			goto out_free;
	}

	return cmds;

out_free:
	while (--i >= 0)
		nvmet_rdma_free_cmd(ndev, cmds + i, admin);
	kfree(cmds);
out:
	return ERR_PTR(ret);
}

static void nvmet_rdma_free_cmds(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_cmd *cmds, int nr_cmds, bool admin)
{
	int i;

	for (i = 0; i < nr_cmds; i++)
		nvmet_rdma_free_cmd(ndev, cmds + i, admin);
	kfree(cmds);
}

static int nvmet_rdma_alloc_rsp(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_rsp *r)
{
	/* NVMe CQE / RDMA SEND */
	r->req.rsp = kmalloc(sizeof(*r->req.rsp), GFP_KERNEL);
	if (!r->req.rsp)
		goto out;

	r->send_sge.addr = ib_dma_map_single(ndev->device, r->req.rsp,
			sizeof(*r->req.rsp), DMA_TO_DEVICE);
	if (ib_dma_mapping_error(ndev->device, r->send_sge.addr))
		goto out_free_rsp;

	r->send_sge.length = sizeof(*r->req.rsp);
	r->send_sge.lkey = ndev->pd->local_dma_lkey;

	r->send_cqe.done = nvmet_rdma_send_done;

	r->send_wr.wr_cqe = &r->send_cqe;
	r->send_wr.sg_list = &r->send_sge;
	r->send_wr.num_sge = 1;
	r->send_wr.send_flags = IB_SEND_SIGNALED;

	/* Data In / RDMA READ */
	r->read_cqe.done = nvmet_rdma_read_data_done;
	return 0;

out_free_rsp:
	kfree(r->req.rsp);
out:
	return -ENOMEM;
}

static void nvmet_rdma_free_rsp(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_rsp *r)
{
	ib_dma_unmap_single(ndev->device, r->send_sge.addr,
				sizeof(*r->req.rsp), DMA_TO_DEVICE);
	kfree(r->req.rsp);
}

static int
nvmet_rdma_alloc_rsps(struct nvmet_rdma_queue *queue)
{
	struct nvmet_rdma_device *ndev = queue->dev;
	int nr_rsps = queue->recv_queue_size * 2;
	int ret = -EINVAL, i;

	queue->rsps = kcalloc(nr_rsps, sizeof(struct nvmet_rdma_rsp),
			GFP_KERNEL);
	if (!queue->rsps)
		goto out;

	for (i = 0; i < nr_rsps; i++) {
		struct nvmet_rdma_rsp *rsp = &queue->rsps[i];

		ret = nvmet_rdma_alloc_rsp(ndev, rsp);
		if (ret)
			goto out_free;

		list_add_tail(&rsp->free_list, &queue->free_rsps);
	}

	return 0;

out_free:
	while (--i >= 0) {
		struct nvmet_rdma_rsp *rsp = &queue->rsps[i];

		list_del(&rsp->free_list);
		nvmet_rdma_free_rsp(ndev, rsp);
	}
	kfree(queue->rsps);
out:
	return ret;
}

static void nvmet_rdma_free_rsps(struct nvmet_rdma_queue *queue)
{
	struct nvmet_rdma_device *ndev = queue->dev;
	int i, nr_rsps = queue->recv_queue_size * 2;

	for (i = 0; i < nr_rsps; i++) {
		struct nvmet_rdma_rsp *rsp = &queue->rsps[i];

		list_del(&rsp->free_list);
		nvmet_rdma_free_rsp(ndev, rsp);
	}
	kfree(queue->rsps);
}

static int nvmet_rdma_post_recv(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_cmd *cmd)
{
	struct ib_recv_wr *bad_wr;

	ib_dma_sync_single_for_device(ndev->device,
		cmd->sge[0].addr, cmd->sge[0].length,
		DMA_FROM_DEVICE);

	if (ndev->srq)
		return ib_post_srq_recv(ndev->srq, &cmd->wr, &bad_wr);
	return ib_post_recv(cmd->queue->cm_id->qp, &cmd->wr, &bad_wr);
}

static void nvmet_rdma_process_wr_wait_list(struct nvmet_rdma_queue *queue)
{
	spin_lock(&queue->rsp_wr_wait_lock);
	while (!list_empty(&queue->rsp_wr_wait_list)) {
		struct nvmet_rdma_rsp *rsp;
		bool ret;

		rsp = list_entry(queue->rsp_wr_wait_list.next,
				struct nvmet_rdma_rsp, wait_list);
		list_del(&rsp->wait_list);

		spin_unlock(&queue->rsp_wr_wait_lock);
		ret = nvmet_rdma_execute_command(rsp);
		spin_lock(&queue->rsp_wr_wait_lock);

		if (!ret) {
			list_add(&rsp->wait_list, &queue->rsp_wr_wait_list);
			break;
		}
	}
	spin_unlock(&queue->rsp_wr_wait_lock);
}


static void nvmet_rdma_release_rsp(struct nvmet_rdma_rsp *rsp)
{
	struct nvmet_rdma_queue *queue = rsp->queue;

	atomic_add(1 + rsp->n_rdma, &queue->sq_wr_avail);

	if (rsp->n_rdma) {
		rdma_rw_ctx_destroy(&rsp->rw, queue->cm_id->qp,
				queue->cm_id->port_num, rsp->req.sg,
				rsp->req.sg_cnt, nvmet_data_dir(&rsp->req));
	}

	if (rsp->req.sg != &rsp->cmd->inline_sg)
		nvmet_rdma_free_sgl(rsp->req.sg, rsp->req.sg_cnt);

	if (unlikely(!list_empty_careful(&queue->rsp_wr_wait_list)))
		nvmet_rdma_process_wr_wait_list(queue);

	nvmet_rdma_put_rsp(rsp);
}

static void nvmet_rdma_error_comp(struct nvmet_rdma_queue *queue)
{
	if (queue->nvme_sq.ctrl) {
		nvmet_ctrl_fatal_error(queue->nvme_sq.ctrl);
	} else {
		/*
		 * we didn't setup the controller yet in case
		 * of admin connect error, just disconnect and
		 * cleanup the queue
		 */
		nvmet_rdma_queue_disconnect(queue);
	}
}

static void nvmet_rdma_send_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(wc->wr_cqe, struct nvmet_rdma_rsp, send_cqe);

	nvmet_rdma_release_rsp(rsp);

	if (unlikely(wc->status != IB_WC_SUCCESS &&
		     wc->status != IB_WC_WR_FLUSH_ERR)) {
		pr_err("SEND for CQE 0x%p failed with status %s (%d).\n",
			wc->wr_cqe, ib_wc_status_msg(wc->status), wc->status);
		nvmet_rdma_error_comp(rsp->queue);
	}
}

static void nvmet_rdma_queue_response(struct nvmet_req *req)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(req, struct nvmet_rdma_rsp, req);
	struct rdma_cm_id *cm_id = rsp->queue->cm_id;
	struct ib_send_wr *first_wr, *bad_wr;

	if (rsp->flags & NVMET_RDMA_REQ_INVALIDATE_RKEY) {
		rsp->send_wr.opcode = IB_WR_SEND_WITH_INV;
		rsp->send_wr.ex.invalidate_rkey = rsp->invalidate_rkey;
	} else {
		rsp->send_wr.opcode = IB_WR_SEND;
	}

	if (nvmet_rdma_need_data_out(rsp))
		first_wr = rdma_rw_ctx_wrs(&rsp->rw, cm_id->qp,
				cm_id->port_num, NULL, &rsp->send_wr);
	else
		first_wr = &rsp->send_wr;

	nvmet_rdma_post_recv(rsp->queue->dev, rsp->cmd);

	ib_dma_sync_single_for_device(rsp->queue->dev->device,
		rsp->send_sge.addr, rsp->send_sge.length,
		DMA_TO_DEVICE);

	if (ib_post_send(cm_id->qp, first_wr, &bad_wr)) {
		pr_err("sending cmd response failed\n");
		nvmet_rdma_release_rsp(rsp);
	}
}

static void nvmet_rdma_read_data_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(wc->wr_cqe, struct nvmet_rdma_rsp, read_cqe);
	struct nvmet_rdma_queue *queue = cq->cq_context;

	WARN_ON(rsp->n_rdma <= 0);
	atomic_add(rsp->n_rdma, &queue->sq_wr_avail);
	rdma_rw_ctx_destroy(&rsp->rw, queue->cm_id->qp,
			queue->cm_id->port_num, rsp->req.sg,
			rsp->req.sg_cnt, nvmet_data_dir(&rsp->req));
	rsp->n_rdma = 0;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		nvmet_req_uninit(&rsp->req);
		nvmet_rdma_release_rsp(rsp);
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			pr_info("RDMA READ for CQE 0x%p failed with status %s (%d).\n",
				wc->wr_cqe, ib_wc_status_msg(wc->status), wc->status);
			nvmet_rdma_error_comp(queue);
		}
		return;
	}

	rsp->req.execute(&rsp->req);
}

static void nvmet_rdma_use_inline_sg(struct nvmet_rdma_rsp *rsp, u32 len,
		u64 off)
{
	sg_init_table(&rsp->cmd->inline_sg, 1);
	sg_set_page(&rsp->cmd->inline_sg, rsp->cmd->inline_page, len, off);
	rsp->req.sg = &rsp->cmd->inline_sg;
	rsp->req.sg_cnt = 1;
}

static u16 nvmet_rdma_map_sgl_inline(struct nvmet_rdma_rsp *rsp)
{
	struct nvme_sgl_desc *sgl = &rsp->req.cmd->common.dptr.sgl;
	u64 off = le64_to_cpu(sgl->addr);
	u32 len = le32_to_cpu(sgl->length);

	if (!nvme_is_write(rsp->req.cmd))
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;

	if (off + len > NVMET_RDMA_INLINE_DATA_SIZE) {
		pr_err("invalid inline data offset!\n");
		return NVME_SC_SGL_INVALID_OFFSET | NVME_SC_DNR;
	}

	/* no data command? */
	if (!len)
		return 0;

	nvmet_rdma_use_inline_sg(rsp, len, off);
	rsp->flags |= NVMET_RDMA_REQ_INLINE_DATA;
	return 0;
}

static u16 nvmet_rdma_map_sgl_keyed(struct nvmet_rdma_rsp *rsp,
		struct nvme_keyed_sgl_desc *sgl, bool invalidate)
{
	struct rdma_cm_id *cm_id = rsp->queue->cm_id;
	u64 addr = le64_to_cpu(sgl->addr);
	u32 len = get_unaligned_le24(sgl->length);
	u32 key = get_unaligned_le32(sgl->key);
	int ret;
	u16 status;

	/* no data command? */
	if (!len)
		return 0;

	status = nvmet_rdma_alloc_sgl(&rsp->req.sg, &rsp->req.sg_cnt,
			len);
	if (status)
		return status;

	ret = rdma_rw_ctx_init(&rsp->rw, cm_id->qp, cm_id->port_num,
			rsp->req.sg, rsp->req.sg_cnt, 0, addr, key,
			nvmet_data_dir(&rsp->req));
	if (ret < 0)
		return NVME_SC_INTERNAL;
	rsp->n_rdma += ret;

	if (invalidate) {
		rsp->invalidate_rkey = key;
		rsp->flags |= NVMET_RDMA_REQ_INVALIDATE_RKEY;
	}

	return 0;
}

static u16 nvmet_rdma_map_sgl(struct nvmet_rdma_rsp *rsp)
{
	struct nvme_keyed_sgl_desc *sgl = &rsp->req.cmd->common.dptr.ksgl;

	switch (sgl->type >> 4) {
	case NVME_SGL_FMT_DATA_DESC:
		switch (sgl->type & 0xf) {
		case NVME_SGL_FMT_OFFSET:
			return nvmet_rdma_map_sgl_inline(rsp);
		default:
			pr_err("invalid SGL subtype: %#x\n", sgl->type);
			return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		}
	case NVME_KEY_SGL_FMT_DATA_DESC:
		switch (sgl->type & 0xf) {
		case NVME_SGL_FMT_ADDRESS | NVME_SGL_FMT_INVALIDATE:
			return nvmet_rdma_map_sgl_keyed(rsp, sgl, true);
		case NVME_SGL_FMT_ADDRESS:
			return nvmet_rdma_map_sgl_keyed(rsp, sgl, false);
		default:
			pr_err("invalid SGL subtype: %#x\n", sgl->type);
			return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		}
	default:
		pr_err("invalid SGL type: %#x\n", sgl->type);
		return NVME_SC_SGL_INVALID_TYPE | NVME_SC_DNR;
	}
}

static bool nvmet_rdma_execute_command(struct nvmet_rdma_rsp *rsp)
{
	struct nvmet_rdma_queue *queue = rsp->queue;

	if (unlikely(atomic_sub_return(1 + rsp->n_rdma,
			&queue->sq_wr_avail) < 0)) {
		pr_debug("IB send queue full (needed %d): queue %u cntlid %u\n",
				1 + rsp->n_rdma, queue->idx,
				queue->nvme_sq.ctrl->cntlid);
		atomic_add(1 + rsp->n_rdma, &queue->sq_wr_avail);
		return false;
	}

	if (nvmet_rdma_need_data_in(rsp)) {
		if (rdma_rw_ctx_post(&rsp->rw, queue->cm_id->qp,
				queue->cm_id->port_num, &rsp->read_cqe, NULL))
			nvmet_req_complete(&rsp->req, NVME_SC_DATA_XFER_ERROR);
	} else {
		rsp->req.execute(&rsp->req);
	}

	return true;
}

static void nvmet_rdma_handle_command(struct nvmet_rdma_queue *queue,
		struct nvmet_rdma_rsp *cmd)
{
	u16 status;

	ib_dma_sync_single_for_cpu(queue->dev->device,
		cmd->cmd->sge[0].addr, cmd->cmd->sge[0].length,
		DMA_FROM_DEVICE);
	ib_dma_sync_single_for_cpu(queue->dev->device,
		cmd->send_sge.addr, cmd->send_sge.length,
		DMA_TO_DEVICE);

	if (!nvmet_req_init(&cmd->req, &queue->nvme_cq,
			&queue->nvme_sq, &nvmet_rdma_ops))
		return;

	status = nvmet_rdma_map_sgl(cmd);
	if (status)
		goto out_err;

	if (unlikely(!nvmet_rdma_execute_command(cmd))) {
		spin_lock(&queue->rsp_wr_wait_lock);
		list_add_tail(&cmd->wait_list, &queue->rsp_wr_wait_list);
		spin_unlock(&queue->rsp_wr_wait_lock);
	}

	return;

out_err:
	nvmet_req_complete(&cmd->req, status);
}

static void nvmet_rdma_recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvmet_rdma_cmd *cmd =
		container_of(wc->wr_cqe, struct nvmet_rdma_cmd, cqe);
	struct nvmet_rdma_queue *queue = cq->cq_context;
	struct nvmet_rdma_rsp *rsp;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			pr_err("RECV for CQE 0x%p failed with status %s (%d)\n",
				wc->wr_cqe, ib_wc_status_msg(wc->status),
				wc->status);
			nvmet_rdma_error_comp(queue);
		}
		return;
	}

	if (unlikely(wc->byte_len < sizeof(struct nvme_command))) {
		pr_err("Ctrl Fatal Error: capsule size less than 64 bytes\n");
		nvmet_rdma_error_comp(queue);
		return;
	}

	cmd->queue = queue;
	rsp = nvmet_rdma_get_rsp(queue);
	rsp->queue = queue;
	rsp->cmd = cmd;
	rsp->flags = 0;
	rsp->req.cmd = cmd->nvme_cmd;
	rsp->req.port = queue->port;
	rsp->n_rdma = 0;

	if (unlikely(queue->state != NVMET_RDMA_Q_LIVE)) {
		unsigned long flags;

		spin_lock_irqsave(&queue->state_lock, flags);
		if (queue->state == NVMET_RDMA_Q_CONNECTING)
			list_add_tail(&rsp->wait_list, &queue->rsp_wait_list);
		else
			nvmet_rdma_put_rsp(rsp);
		spin_unlock_irqrestore(&queue->state_lock, flags);
		return;
	}

	nvmet_rdma_handle_command(queue, rsp);
}

static void nvmet_rdma_destroy_srq(struct nvmet_rdma_device *ndev)
{
	if (!ndev->srq)
		return;

	nvmet_rdma_free_cmds(ndev, ndev->srq_cmds, ndev->srq_size, false);
	ib_destroy_srq(ndev->srq);
}

static int nvmet_rdma_init_srq(struct nvmet_rdma_device *ndev)
{
	struct ib_srq_init_attr srq_attr = { NULL, };
	struct ib_srq *srq;
	size_t srq_size;
	int ret, i;

	srq_size = 4095;	/* XXX: tune */

	srq_attr.attr.max_wr = srq_size;
	srq_attr.attr.max_sge = 2;
	srq_attr.attr.srq_limit = 0;
	srq_attr.srq_type = IB_SRQT_BASIC;
	srq = ib_create_srq(ndev->pd, &srq_attr);
	if (IS_ERR(srq)) {
		/*
		 * If SRQs aren't supported we just go ahead and use normal
		 * non-shared receive queues.
		 */
		pr_info("SRQ requested but not supported.\n");
		return 0;
	}

	ndev->srq_cmds = nvmet_rdma_alloc_cmds(ndev, srq_size, false);
	if (IS_ERR(ndev->srq_cmds)) {
		ret = PTR_ERR(ndev->srq_cmds);
		goto out_destroy_srq;
	}

	ndev->srq = srq;
	ndev->srq_size = srq_size;

	for (i = 0; i < srq_size; i++)
		nvmet_rdma_post_recv(ndev, &ndev->srq_cmds[i]);

	return 0;

out_destroy_srq:
	ib_destroy_srq(srq);
	return ret;
}

static void nvmet_rdma_free_dev(struct kref *ref)
{
	struct nvmet_rdma_device *ndev =
		container_of(ref, struct nvmet_rdma_device, ref);

	mutex_lock(&device_list_mutex);
	list_del(&ndev->entry);
	mutex_unlock(&device_list_mutex);

	nvmet_rdma_destroy_srq(ndev);
	ib_dealloc_pd(ndev->pd);

	kfree(ndev);
}

static struct nvmet_rdma_device *
nvmet_rdma_find_get_device(struct rdma_cm_id *cm_id)
{
	struct nvmet_rdma_device *ndev;
	int ret;

	mutex_lock(&device_list_mutex);
	list_for_each_entry(ndev, &device_list, entry) {
		if (ndev->device->node_guid == cm_id->device->node_guid &&
		    kref_get_unless_zero(&ndev->ref))
			goto out_unlock;
	}

	ndev = kzalloc(sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		goto out_err;

	ndev->device = cm_id->device;
	kref_init(&ndev->ref);

	ndev->pd = ib_alloc_pd(ndev->device, 0);
	if (IS_ERR(ndev->pd))
		goto out_free_dev;

	if (nvmet_rdma_use_srq) {
		ret = nvmet_rdma_init_srq(ndev);
		if (ret)
			goto out_free_pd;
	}

	list_add(&ndev->entry, &device_list);
out_unlock:
	mutex_unlock(&device_list_mutex);
	pr_debug("added %s.\n", ndev->device->name);
	return ndev;

out_free_pd:
	ib_dealloc_pd(ndev->pd);
out_free_dev:
	kfree(ndev);
out_err:
	mutex_unlock(&device_list_mutex);
	return NULL;
}

static int nvmet_rdma_create_queue_ib(struct nvmet_rdma_queue *queue)
{
	struct ib_qp_init_attr qp_attr;
	struct nvmet_rdma_device *ndev = queue->dev;
	int comp_vector, nr_cqe, ret, i;

	/*
	 * Spread the io queues across completion vectors,
	 * but still keep all admin queues on vector 0.
	 */
	comp_vector = !queue->host_qid ? 0 :
		queue->idx % ndev->device->num_comp_vectors;

	/*
	 * Reserve CQ slots for RECV + RDMA_READ/RDMA_WRITE + RDMA_SEND.
	 */
	nr_cqe = queue->recv_queue_size + 2 * queue->send_queue_size;

	queue->cq = ib_alloc_cq(ndev->device, queue,
			nr_cqe + 1, comp_vector,
			IB_POLL_WORKQUEUE);
	if (IS_ERR(queue->cq)) {
		ret = PTR_ERR(queue->cq);
		pr_err("failed to create CQ cqe= %d ret= %d\n",
		       nr_cqe + 1, ret);
		goto out;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_context = queue;
	qp_attr.event_handler = nvmet_rdma_qp_event;
	qp_attr.send_cq = queue->cq;
	qp_attr.recv_cq = queue->cq;
	qp_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_attr.qp_type = IB_QPT_RC;
	/* +1 for drain */
	qp_attr.cap.max_send_wr = queue->send_queue_size + 1;
	qp_attr.cap.max_rdma_ctxs = queue->send_queue_size;
	qp_attr.cap.max_send_sge = max(ndev->device->attrs.max_sge_rd,
					ndev->device->attrs.max_sge);

	if (ndev->srq) {
		qp_attr.srq = ndev->srq;
	} else {
		/* +1 for drain */
		qp_attr.cap.max_recv_wr = 1 + queue->recv_queue_size;
		qp_attr.cap.max_recv_sge = 2;
	}

	ret = rdma_create_qp(queue->cm_id, ndev->pd, &qp_attr);
	if (ret) {
		pr_err("failed to create_qp ret= %d\n", ret);
		goto err_destroy_cq;
	}

	atomic_set(&queue->sq_wr_avail, qp_attr.cap.max_send_wr);

	pr_debug("%s: max_cqe= %d max_sge= %d sq_size = %d cm_id= %p\n",
		 __func__, queue->cq->cqe, qp_attr.cap.max_send_sge,
		 qp_attr.cap.max_send_wr, queue->cm_id);

	if (!ndev->srq) {
		for (i = 0; i < queue->recv_queue_size; i++) {
			queue->cmds[i].queue = queue;
			nvmet_rdma_post_recv(ndev, &queue->cmds[i]);
		}
	}

out:
	return ret;

err_destroy_cq:
	ib_free_cq(queue->cq);
	goto out;
}

static void nvmet_rdma_destroy_queue_ib(struct nvmet_rdma_queue *queue)
{
	ib_drain_qp(queue->cm_id->qp);
	rdma_destroy_qp(queue->cm_id);
	ib_free_cq(queue->cq);
}

static void nvmet_rdma_free_queue(struct nvmet_rdma_queue *queue)
{
	pr_info("freeing queue %d\n", queue->idx);

	nvmet_sq_destroy(&queue->nvme_sq);

	nvmet_rdma_destroy_queue_ib(queue);
	if (!queue->dev->srq) {
		nvmet_rdma_free_cmds(queue->dev, queue->cmds,
				queue->recv_queue_size,
				!queue->host_qid);
	}
	nvmet_rdma_free_rsps(queue);
	ida_simple_remove(&nvmet_rdma_queue_ida, queue->idx);
	kfree(queue);
}

static void nvmet_rdma_release_queue_work(struct work_struct *w)
{
	struct nvmet_rdma_queue *queue =
		container_of(w, struct nvmet_rdma_queue, release_work);
	struct rdma_cm_id *cm_id = queue->cm_id;
	struct nvmet_rdma_device *dev = queue->dev;
	enum nvmet_rdma_queue_state state = queue->state;

	nvmet_rdma_free_queue(queue);

	if (state != NVMET_RDMA_IN_DEVICE_REMOVAL)
		rdma_destroy_id(cm_id);

	kref_put(&dev->ref, nvmet_rdma_free_dev);
}

static int
nvmet_rdma_parse_cm_connect_req(struct rdma_conn_param *conn,
				struct nvmet_rdma_queue *queue)
{
	struct nvme_rdma_cm_req *req;

	req = (struct nvme_rdma_cm_req *)conn->private_data;
	if (!req || conn->private_data_len == 0)
		return NVME_RDMA_CM_INVALID_LEN;

	if (le16_to_cpu(req->recfmt) != NVME_RDMA_CM_FMT_1_0)
		return NVME_RDMA_CM_INVALID_RECFMT;

	queue->host_qid = le16_to_cpu(req->qid);

	/*
	 * req->hsqsize corresponds to our recv queue size plus 1
	 * req->hrqsize corresponds to our send queue size
	 */
	queue->recv_queue_size = le16_to_cpu(req->hsqsize) + 1;
	queue->send_queue_size = le16_to_cpu(req->hrqsize);

	if (!queue->host_qid && queue->recv_queue_size > NVME_AQ_DEPTH)
		return NVME_RDMA_CM_INVALID_HSQSIZE;

	/* XXX: Should we enforce some kind of max for IO queues? */

	return 0;
}

static int nvmet_rdma_cm_reject(struct rdma_cm_id *cm_id,
				enum nvme_rdma_cm_status status)
{
	struct nvme_rdma_cm_rej rej;

	pr_debug("rejecting connect request: status %d (%s)\n",
		 status, nvme_rdma_cm_msg(status));

	rej.recfmt = cpu_to_le16(NVME_RDMA_CM_FMT_1_0);
	rej.sts = cpu_to_le16(status);

	return rdma_reject(cm_id, (void *)&rej, sizeof(rej));
}

static struct nvmet_rdma_queue *
nvmet_rdma_alloc_queue(struct nvmet_rdma_device *ndev,
		struct rdma_cm_id *cm_id,
		struct rdma_cm_event *event)
{
	struct nvmet_rdma_queue *queue;
	int ret;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_reject;
	}

	ret = nvmet_sq_init(&queue->nvme_sq);
	if (ret) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_free_queue;
	}

	ret = nvmet_rdma_parse_cm_connect_req(&event->param.conn, queue);
	if (ret)
		goto out_destroy_sq;

	/*
	 * Schedules the actual release because calling rdma_destroy_id from
	 * inside a CM callback would trigger a deadlock. (great API design..)
	 */
	INIT_WORK(&queue->release_work, nvmet_rdma_release_queue_work);
	queue->dev = ndev;
	queue->cm_id = cm_id;

	spin_lock_init(&queue->state_lock);
	queue->state = NVMET_RDMA_Q_CONNECTING;
	INIT_LIST_HEAD(&queue->rsp_wait_list);
	INIT_LIST_HEAD(&queue->rsp_wr_wait_list);
	spin_lock_init(&queue->rsp_wr_wait_lock);
	INIT_LIST_HEAD(&queue->free_rsps);
	spin_lock_init(&queue->rsps_lock);
	INIT_LIST_HEAD(&queue->queue_list);

	queue->idx = ida_simple_get(&nvmet_rdma_queue_ida, 0, 0, GFP_KERNEL);
	if (queue->idx < 0) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_destroy_sq;
	}

	ret = nvmet_rdma_alloc_rsps(queue);
	if (ret) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_ida_remove;
	}

	if (!ndev->srq) {
		queue->cmds = nvmet_rdma_alloc_cmds(ndev,
				queue->recv_queue_size,
				!queue->host_qid);
		if (IS_ERR(queue->cmds)) {
			ret = NVME_RDMA_CM_NO_RSC;
			goto out_free_responses;
		}
	}

	ret = nvmet_rdma_create_queue_ib(queue);
	if (ret) {
		pr_err("%s: creating RDMA queue failed (%d).\n",
			__func__, ret);
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_free_cmds;
	}

	return queue;

out_free_cmds:
	if (!ndev->srq) {
		nvmet_rdma_free_cmds(queue->dev, queue->cmds,
				queue->recv_queue_size,
				!queue->host_qid);
	}
out_free_responses:
	nvmet_rdma_free_rsps(queue);
out_ida_remove:
	ida_simple_remove(&nvmet_rdma_queue_ida, queue->idx);
out_destroy_sq:
	nvmet_sq_destroy(&queue->nvme_sq);
out_free_queue:
	kfree(queue);
out_reject:
	nvmet_rdma_cm_reject(cm_id, ret);
	return NULL;
}

static void nvmet_rdma_qp_event(struct ib_event *event, void *priv)
{
	struct nvmet_rdma_queue *queue = priv;

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		rdma_notify(queue->cm_id, event->event);
		break;
	default:
		pr_err("received IB QP event: %s (%d)\n",
		       ib_event_msg(event->event), event->event);
		break;
	}
}

static int nvmet_rdma_cm_accept(struct rdma_cm_id *cm_id,
		struct nvmet_rdma_queue *queue,
		struct rdma_conn_param *p)
{
	struct rdma_conn_param  param = { };
	struct nvme_rdma_cm_rep priv = { };
	int ret = -ENOMEM;

	param.rnr_retry_count = 7;
	param.flow_control = 1;
	param.initiator_depth = min_t(u8, p->initiator_depth,
		queue->dev->device->attrs.max_qp_init_rd_atom);
	param.private_data = &priv;
	param.private_data_len = sizeof(priv);
	priv.recfmt = cpu_to_le16(NVME_RDMA_CM_FMT_1_0);
	priv.crqsize = cpu_to_le16(queue->recv_queue_size);

	ret = rdma_accept(cm_id, &param);
	if (ret)
		pr_err("rdma_accept failed (error code = %d)\n", ret);

	return ret;
}

static int nvmet_rdma_queue_connect(struct rdma_cm_id *cm_id,
		struct rdma_cm_event *event)
{
	struct nvmet_rdma_device *ndev;
	struct nvmet_rdma_queue *queue;
	int ret = -EINVAL;

	ndev = nvmet_rdma_find_get_device(cm_id);
	if (!ndev) {
		nvmet_rdma_cm_reject(cm_id, NVME_RDMA_CM_NO_RSC);
		return -ECONNREFUSED;
	}

	queue = nvmet_rdma_alloc_queue(ndev, cm_id, event);
	if (!queue) {
		ret = -ENOMEM;
		goto put_device;
	}
	queue->port = cm_id->context;

	if (queue->host_qid == 0) {
		/* Let inflight controller teardown complete */
		flush_scheduled_work();
	}

	ret = nvmet_rdma_cm_accept(cm_id, queue, &event->param.conn);
	if (ret)
		goto release_queue;

	mutex_lock(&nvmet_rdma_queue_mutex);
	list_add_tail(&queue->queue_list, &nvmet_rdma_queue_list);
	mutex_unlock(&nvmet_rdma_queue_mutex);

	return 0;

release_queue:
	nvmet_rdma_free_queue(queue);
put_device:
	kref_put(&ndev->ref, nvmet_rdma_free_dev);

	return ret;
}

static void nvmet_rdma_queue_established(struct nvmet_rdma_queue *queue)
{
	unsigned long flags;

	spin_lock_irqsave(&queue->state_lock, flags);
	if (queue->state != NVMET_RDMA_Q_CONNECTING) {
		pr_warn("trying to establish a connected queue\n");
		goto out_unlock;
	}
	queue->state = NVMET_RDMA_Q_LIVE;

	while (!list_empty(&queue->rsp_wait_list)) {
		struct nvmet_rdma_rsp *cmd;

		cmd = list_first_entry(&queue->rsp_wait_list,
					struct nvmet_rdma_rsp, wait_list);
		list_del(&cmd->wait_list);

		spin_unlock_irqrestore(&queue->state_lock, flags);
		nvmet_rdma_handle_command(queue, cmd);
		spin_lock_irqsave(&queue->state_lock, flags);
	}

out_unlock:
	spin_unlock_irqrestore(&queue->state_lock, flags);
}

static void __nvmet_rdma_queue_disconnect(struct nvmet_rdma_queue *queue)
{
	bool disconnect = false;
	unsigned long flags;

	pr_debug("cm_id= %p queue->state= %d\n", queue->cm_id, queue->state);

	spin_lock_irqsave(&queue->state_lock, flags);
	switch (queue->state) {
	case NVMET_RDMA_Q_CONNECTING:
	case NVMET_RDMA_Q_LIVE:
		queue->state = NVMET_RDMA_Q_DISCONNECTING;
	case NVMET_RDMA_IN_DEVICE_REMOVAL:
		disconnect = true;
		break;
	case NVMET_RDMA_Q_DISCONNECTING:
		break;
	}
	spin_unlock_irqrestore(&queue->state_lock, flags);

	if (disconnect) {
		rdma_disconnect(queue->cm_id);
		schedule_work(&queue->release_work);
	}
}

static void nvmet_rdma_queue_disconnect(struct nvmet_rdma_queue *queue)
{
	bool disconnect = false;

	mutex_lock(&nvmet_rdma_queue_mutex);
	if (!list_empty(&queue->queue_list)) {
		list_del_init(&queue->queue_list);
		disconnect = true;
	}
	mutex_unlock(&nvmet_rdma_queue_mutex);

	if (disconnect)
		__nvmet_rdma_queue_disconnect(queue);
}

static void nvmet_rdma_queue_connect_fail(struct rdma_cm_id *cm_id,
		struct nvmet_rdma_queue *queue)
{
	WARN_ON_ONCE(queue->state != NVMET_RDMA_Q_CONNECTING);

	mutex_lock(&nvmet_rdma_queue_mutex);
	if (!list_empty(&queue->queue_list))
		list_del_init(&queue->queue_list);
	mutex_unlock(&nvmet_rdma_queue_mutex);

	pr_err("failed to connect queue %d\n", queue->idx);
	schedule_work(&queue->release_work);
}

/**
 * nvme_rdma_device_removal() - Handle RDMA device removal
 * @cm_id:	rdma_cm id, used for nvmet port
 * @queue:      nvmet rdma queue (cm id qp_context)
 *
 * DEVICE_REMOVAL event notifies us that the RDMA device is about
 * to unplug. Note that this event can be generated on a normal
 * queue cm_id and/or a device bound listener cm_id (where in this
 * case queue will be null).
 *
 * We registered an ib_client to handle device removal for queues,
 * so we only need to handle the listening port cm_ids. In this case
 * we nullify the priv to prevent double cm_id destruction and destroying
 * the cm_id implicitely by returning a non-zero rc to the callout.
 */
static int nvmet_rdma_device_removal(struct rdma_cm_id *cm_id,
		struct nvmet_rdma_queue *queue)
{
	struct nvmet_port *port;

	if (queue) {
		/*
		 * This is a queue cm_id. we have registered
		 * an ib_client to handle queues removal
		 * so don't interfear and just return.
		 */
		return 0;
	}

	port = cm_id->context;

	/*
	 * This is a listener cm_id. Make sure that
	 * future remove_port won't invoke a double
	 * cm_id destroy. use atomic xchg to make sure
	 * we don't compete with remove_port.
	 */
	if (xchg(&port->priv, NULL) != cm_id)
		return 0;

	/*
	 * We need to return 1 so that the core will destroy
	 * it's own ID.  What a great API design..
	 */
	return 1;
}

static int nvmet_rdma_cm_handler(struct rdma_cm_id *cm_id,
		struct rdma_cm_event *event)
{
	struct nvmet_rdma_queue *queue = NULL;
	int ret = 0;

	if (cm_id->qp)
		queue = cm_id->qp->qp_context;

	pr_debug("%s (%d): status %d id %p\n",
		rdma_event_msg(event->event), event->event,
		event->status, cm_id);

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = nvmet_rdma_queue_connect(cm_id, event);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		nvmet_rdma_queue_established(queue);
		break;
	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_DISCONNECTED:
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		/*
		 * We might end up here when we already freed the qp
		 * which means queue release sequence is in progress,
		 * so don't get in the way...
		 */
		if (queue)
			nvmet_rdma_queue_disconnect(queue);
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		ret = nvmet_rdma_device_removal(cm_id, queue);
		break;
	case RDMA_CM_EVENT_REJECTED:
		pr_debug("Connection rejected: %s\n",
			 rdma_reject_msg(cm_id, event->status));
		/* FALLTHROUGH */
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_CONNECT_ERROR:
		nvmet_rdma_queue_connect_fail(cm_id, queue);
		break;
	default:
		pr_err("received unrecognized RDMA CM event %d\n",
			event->event);
		break;
	}

	return ret;
}

static void nvmet_rdma_delete_ctrl(struct nvmet_ctrl *ctrl)
{
	struct nvmet_rdma_queue *queue;

restart:
	mutex_lock(&nvmet_rdma_queue_mutex);
	list_for_each_entry(queue, &nvmet_rdma_queue_list, queue_list) {
		if (queue->nvme_sq.ctrl == ctrl) {
			list_del_init(&queue->queue_list);
			mutex_unlock(&nvmet_rdma_queue_mutex);

			__nvmet_rdma_queue_disconnect(queue);
			goto restart;
		}
	}
	mutex_unlock(&nvmet_rdma_queue_mutex);
}

static int nvmet_rdma_add_port(struct nvmet_port *port)
{
	struct rdma_cm_id *cm_id;
	struct sockaddr_storage addr = { };
	__kernel_sa_family_t af;
	int ret;

	switch (port->disc_addr.adrfam) {
	case NVMF_ADDR_FAMILY_IP4:
		af = AF_INET;
		break;
	case NVMF_ADDR_FAMILY_IP6:
		af = AF_INET6;
		break;
	default:
		pr_err("address family %d not supported\n",
				port->disc_addr.adrfam);
		return -EINVAL;
	}

	ret = inet_pton_with_scope(&init_net, af, port->disc_addr.traddr,
			port->disc_addr.trsvcid, &addr);
	if (ret) {
		pr_err("malformed ip/port passed: %s:%s\n",
			port->disc_addr.traddr, port->disc_addr.trsvcid);
		return ret;
	}

	cm_id = rdma_create_id(&init_net, nvmet_rdma_cm_handler, port,
			RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(cm_id)) {
		pr_err("CM ID creation failed\n");
		return PTR_ERR(cm_id);
	}

	/*
	 * Allow both IPv4 and IPv6 sockets to bind a single port
	 * at the same time.
	 */
	ret = rdma_set_afonly(cm_id, 1);
	if (ret) {
		pr_err("rdma_set_afonly failed (%d)\n", ret);
		goto out_destroy_id;
	}

	ret = rdma_bind_addr(cm_id, (struct sockaddr *)&addr);
	if (ret) {
		pr_err("binding CM ID to %pISpcs failed (%d)\n",
			(struct sockaddr *)&addr, ret);
		goto out_destroy_id;
	}

	ret = rdma_listen(cm_id, 128);
	if (ret) {
		pr_err("listening to %pISpcs failed (%d)\n",
			(struct sockaddr *)&addr, ret);
		goto out_destroy_id;
	}

	pr_info("enabling port %d (%pISpcs)\n",
		le16_to_cpu(port->disc_addr.portid), (struct sockaddr *)&addr);
	port->priv = cm_id;
	return 0;

out_destroy_id:
	rdma_destroy_id(cm_id);
	return ret;
}

static void nvmet_rdma_remove_port(struct nvmet_port *port)
{
	struct rdma_cm_id *cm_id = xchg(&port->priv, NULL);

	if (cm_id)
		rdma_destroy_id(cm_id);
}

static struct nvmet_fabrics_ops nvmet_rdma_ops = {
	.owner			= THIS_MODULE,
	.type			= NVMF_TRTYPE_RDMA,
	.sqe_inline_size	= NVMET_RDMA_INLINE_DATA_SIZE,
	.msdbd			= 1,
	.has_keyed_sgls		= 1,
	.add_port		= nvmet_rdma_add_port,
	.remove_port		= nvmet_rdma_remove_port,
	.queue_response		= nvmet_rdma_queue_response,
	.delete_ctrl		= nvmet_rdma_delete_ctrl,
};

static void nvmet_rdma_remove_one(struct ib_device *ib_device, void *client_data)
{
	struct nvmet_rdma_queue *queue;

	/* Device is being removed, delete all queues using this device */
	mutex_lock(&nvmet_rdma_queue_mutex);
	list_for_each_entry(queue, &nvmet_rdma_queue_list, queue_list) {
		if (queue->dev->device != ib_device)
			continue;

		pr_info("Removing queue %d\n", queue->idx);
		__nvmet_rdma_queue_disconnect(queue);
	}
	mutex_unlock(&nvmet_rdma_queue_mutex);

	flush_scheduled_work();
}

static struct ib_client nvmet_rdma_ib_client = {
	.name   = "nvmet_rdma",
	.remove = nvmet_rdma_remove_one
};

static int __init nvmet_rdma_init(void)
{
	int ret;

	ret = ib_register_client(&nvmet_rdma_ib_client);
	if (ret)
		return ret;

	ret = nvmet_register_transport(&nvmet_rdma_ops);
	if (ret)
		goto err_ib_client;

	return 0;

err_ib_client:
	ib_unregister_client(&nvmet_rdma_ib_client);
	return ret;
}

static void __exit nvmet_rdma_exit(void)
{
	struct nvmet_rdma_queue *queue;

	nvmet_unregister_transport(&nvmet_rdma_ops);

	flush_scheduled_work();

	mutex_lock(&nvmet_rdma_queue_mutex);
	while ((queue = list_first_entry_or_null(&nvmet_rdma_queue_list,
			struct nvmet_rdma_queue, queue_list))) {
		list_del_init(&queue->queue_list);

		mutex_unlock(&nvmet_rdma_queue_mutex);
		__nvmet_rdma_queue_disconnect(queue);
		mutex_lock(&nvmet_rdma_queue_mutex);
	}
	mutex_unlock(&nvmet_rdma_queue_mutex);

	flush_scheduled_work();
	ib_unregister_client(&nvmet_rdma_ib_client);
	ida_destroy(&nvmet_rdma_queue_ida);
}

module_init(nvmet_rdma_init);
module_exit(nvmet_rdma_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("nvmet-transport-1"); /* 1 == NVMF_TRTYPE_RDMA */
