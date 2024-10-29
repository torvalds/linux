// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe over Fabrics RDMA target.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/atomic.h>
#include <linux/blk-integrity.h>
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
#include <rdma/ib_cm.h>

#include <linux/nvme-rdma.h>
#include "nvmet.h"

/*
 * We allow at least 1 page, up to 4 SGEs, and up to 16KB of inline data
 */
#define NVMET_RDMA_DEFAULT_INLINE_DATA_SIZE	PAGE_SIZE
#define NVMET_RDMA_MAX_INLINE_SGE		4
#define NVMET_RDMA_MAX_INLINE_DATA_SIZE		max_t(int, SZ_16K, PAGE_SIZE)

/* Assume mpsmin == device_page_size == 4KB */
#define NVMET_RDMA_MAX_MDTS			8
#define NVMET_RDMA_MAX_METADATA_MDTS		5

struct nvmet_rdma_srq;

struct nvmet_rdma_cmd {
	struct ib_sge		sge[NVMET_RDMA_MAX_INLINE_SGE + 1];
	struct ib_cqe		cqe;
	struct ib_recv_wr	wr;
	struct scatterlist	inline_sg[NVMET_RDMA_MAX_INLINE_SGE];
	struct nvme_command     *nvme_cmd;
	struct nvmet_rdma_queue	*queue;
	struct nvmet_rdma_srq   *nsrq;
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
	struct ib_cqe		write_cqe;
	struct rdma_rw_ctx	rw;

	struct nvmet_req	req;

	bool			allocated;
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
};

struct nvmet_rdma_queue {
	struct rdma_cm_id	*cm_id;
	struct ib_qp		*qp;
	struct nvmet_port	*port;
	struct ib_cq		*cq;
	atomic_t		sq_wr_avail;
	struct nvmet_rdma_device *dev;
	struct nvmet_rdma_srq   *nsrq;
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
	int			comp_vector;
	int			recv_queue_size;
	int			send_queue_size;

	struct list_head	queue_list;
};

struct nvmet_rdma_port {
	struct nvmet_port	*nport;
	struct sockaddr_storage addr;
	struct rdma_cm_id	*cm_id;
	struct delayed_work	repair_work;
};

struct nvmet_rdma_srq {
	struct ib_srq            *srq;
	struct nvmet_rdma_cmd    *cmds;
	struct nvmet_rdma_device *ndev;
};

struct nvmet_rdma_device {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct nvmet_rdma_srq	**srqs;
	int			srq_count;
	size_t			srq_size;
	struct kref		ref;
	struct list_head	entry;
	int			inline_data_size;
	int			inline_page_count;
};

static bool nvmet_rdma_use_srq;
module_param_named(use_srq, nvmet_rdma_use_srq, bool, 0444);
MODULE_PARM_DESC(use_srq, "Use shared receive queue.");

static int srq_size_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops srq_size_ops = {
	.set = srq_size_set,
	.get = param_get_int,
};

static int nvmet_rdma_srq_size = 1024;
module_param_cb(srq_size, &srq_size_ops, &nvmet_rdma_srq_size, 0644);
MODULE_PARM_DESC(srq_size, "set Shared Receive Queue (SRQ) size, should >= 256 (default: 1024)");

static DEFINE_IDA(nvmet_rdma_queue_ida);
static LIST_HEAD(nvmet_rdma_queue_list);
static DEFINE_MUTEX(nvmet_rdma_queue_mutex);

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_mutex);

static bool nvmet_rdma_execute_command(struct nvmet_rdma_rsp *rsp);
static void nvmet_rdma_send_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_recv_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_read_data_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_write_data_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_qp_event(struct ib_event *event, void *priv);
static void nvmet_rdma_queue_disconnect(struct nvmet_rdma_queue *queue);
static void nvmet_rdma_free_rsp(struct nvmet_rdma_device *ndev,
				struct nvmet_rdma_rsp *r);
static int nvmet_rdma_alloc_rsp(struct nvmet_rdma_device *ndev,
				struct nvmet_rdma_rsp *r);

static const struct nvmet_fabrics_ops nvmet_rdma_ops;

static int srq_size_set(const char *val, const struct kernel_param *kp)
{
	int n = 0, ret;

	ret = kstrtoint(val, 10, &n);
	if (ret != 0 || n < 256)
		return -EINVAL;

	return param_set_int(val, kp);
}

static int num_pages(int len)
{
	return 1 + (((len - 1) & PAGE_MASK) >> PAGE_SHIFT);
}

static inline bool nvmet_rdma_need_data_in(struct nvmet_rdma_rsp *rsp)
{
	return nvme_is_write(rsp->req.cmd) &&
		rsp->req.transfer_len &&
		!(rsp->flags & NVMET_RDMA_REQ_INLINE_DATA);
}

static inline bool nvmet_rdma_need_data_out(struct nvmet_rdma_rsp *rsp)
{
	return !nvme_is_write(rsp->req.cmd) &&
		rsp->req.transfer_len &&
		!rsp->req.cqe->status &&
		!(rsp->flags & NVMET_RDMA_REQ_INLINE_DATA);
}

static inline struct nvmet_rdma_rsp *
nvmet_rdma_get_rsp(struct nvmet_rdma_queue *queue)
{
	struct nvmet_rdma_rsp *rsp;
	unsigned long flags;

	spin_lock_irqsave(&queue->rsps_lock, flags);
	rsp = list_first_entry_or_null(&queue->free_rsps,
				struct nvmet_rdma_rsp, free_list);
	if (likely(rsp))
		list_del(&rsp->free_list);
	spin_unlock_irqrestore(&queue->rsps_lock, flags);

	if (unlikely(!rsp)) {
		int ret;

		rsp = kzalloc(sizeof(*rsp), GFP_KERNEL);
		if (unlikely(!rsp))
			return NULL;
		ret = nvmet_rdma_alloc_rsp(queue->dev, rsp);
		if (unlikely(ret)) {
			kfree(rsp);
			return NULL;
		}

		rsp->allocated = true;
	}

	return rsp;
}

static inline void
nvmet_rdma_put_rsp(struct nvmet_rdma_rsp *rsp)
{
	unsigned long flags;

	if (unlikely(rsp->allocated)) {
		nvmet_rdma_free_rsp(rsp->queue->dev, rsp);
		kfree(rsp);
		return;
	}

	spin_lock_irqsave(&rsp->queue->rsps_lock, flags);
	list_add_tail(&rsp->free_list, &rsp->queue->free_rsps);
	spin_unlock_irqrestore(&rsp->queue->rsps_lock, flags);
}

static void nvmet_rdma_free_inline_pages(struct nvmet_rdma_device *ndev,
				struct nvmet_rdma_cmd *c)
{
	struct scatterlist *sg;
	struct ib_sge *sge;
	int i;

	if (!ndev->inline_data_size)
		return;

	sg = c->inline_sg;
	sge = &c->sge[1];

	for (i = 0; i < ndev->inline_page_count; i++, sg++, sge++) {
		if (sge->length)
			ib_dma_unmap_page(ndev->device, sge->addr,
					sge->length, DMA_FROM_DEVICE);
		if (sg_page(sg))
			__free_page(sg_page(sg));
	}
}

static int nvmet_rdma_alloc_inline_pages(struct nvmet_rdma_device *ndev,
				struct nvmet_rdma_cmd *c)
{
	struct scatterlist *sg;
	struct ib_sge *sge;
	struct page *pg;
	int len;
	int i;

	if (!ndev->inline_data_size)
		return 0;

	sg = c->inline_sg;
	sg_init_table(sg, ndev->inline_page_count);
	sge = &c->sge[1];
	len = ndev->inline_data_size;

	for (i = 0; i < ndev->inline_page_count; i++, sg++, sge++) {
		pg = alloc_page(GFP_KERNEL);
		if (!pg)
			goto out_err;
		sg_assign_page(sg, pg);
		sge->addr = ib_dma_map_page(ndev->device,
			pg, 0, PAGE_SIZE, DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(ndev->device, sge->addr))
			goto out_err;
		sge->length = min_t(int, len, PAGE_SIZE);
		sge->lkey = ndev->pd->local_dma_lkey;
		len -= sge->length;
	}

	return 0;
out_err:
	for (; i >= 0; i--, sg--, sge--) {
		if (sge->length)
			ib_dma_unmap_page(ndev->device, sge->addr,
					sge->length, DMA_FROM_DEVICE);
		if (sg_page(sg))
			__free_page(sg_page(sg));
	}
	return -ENOMEM;
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

	if (!admin && nvmet_rdma_alloc_inline_pages(ndev, c))
		goto out_unmap_cmd;

	c->cqe.done = nvmet_rdma_recv_done;

	c->wr.wr_cqe = &c->cqe;
	c->wr.sg_list = c->sge;
	c->wr.num_sge = admin ? 1 : ndev->inline_page_count + 1;

	return 0;

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
	if (!admin)
		nvmet_rdma_free_inline_pages(ndev, c);
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
	r->req.cqe = kmalloc(sizeof(*r->req.cqe), GFP_KERNEL);
	if (!r->req.cqe)
		goto out;

	r->send_sge.addr = ib_dma_map_single(ndev->device, r->req.cqe,
			sizeof(*r->req.cqe), DMA_TO_DEVICE);
	if (ib_dma_mapping_error(ndev->device, r->send_sge.addr))
		goto out_free_rsp;

	if (ib_dma_pci_p2p_dma_supported(ndev->device))
		r->req.p2p_client = &ndev->device->dev;
	r->send_sge.length = sizeof(*r->req.cqe);
	r->send_sge.lkey = ndev->pd->local_dma_lkey;

	r->send_cqe.done = nvmet_rdma_send_done;

	r->send_wr.wr_cqe = &r->send_cqe;
	r->send_wr.sg_list = &r->send_sge;
	r->send_wr.num_sge = 1;
	r->send_wr.send_flags = IB_SEND_SIGNALED;

	/* Data In / RDMA READ */
	r->read_cqe.done = nvmet_rdma_read_data_done;
	/* Data Out / RDMA WRITE */
	r->write_cqe.done = nvmet_rdma_write_data_done;

	return 0;

out_free_rsp:
	kfree(r->req.cqe);
out:
	return -ENOMEM;
}

static void nvmet_rdma_free_rsp(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_rsp *r)
{
	ib_dma_unmap_single(ndev->device, r->send_sge.addr,
				sizeof(*r->req.cqe), DMA_TO_DEVICE);
	kfree(r->req.cqe);
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
	while (--i >= 0)
		nvmet_rdma_free_rsp(ndev, &queue->rsps[i]);
	kfree(queue->rsps);
out:
	return ret;
}

static void nvmet_rdma_free_rsps(struct nvmet_rdma_queue *queue)
{
	struct nvmet_rdma_device *ndev = queue->dev;
	int i, nr_rsps = queue->recv_queue_size * 2;

	for (i = 0; i < nr_rsps; i++)
		nvmet_rdma_free_rsp(ndev, &queue->rsps[i]);
	kfree(queue->rsps);
}

static int nvmet_rdma_post_recv(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_cmd *cmd)
{
	int ret;

	ib_dma_sync_single_for_device(ndev->device,
		cmd->sge[0].addr, cmd->sge[0].length,
		DMA_FROM_DEVICE);

	if (cmd->nsrq)
		ret = ib_post_srq_recv(cmd->nsrq->srq, &cmd->wr, NULL);
	else
		ret = ib_post_recv(cmd->queue->qp, &cmd->wr, NULL);

	if (unlikely(ret))
		pr_err("post_recv cmd failed\n");

	return ret;
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

static u16 nvmet_rdma_check_pi_status(struct ib_mr *sig_mr)
{
	struct ib_mr_status mr_status;
	int ret;
	u16 status = 0;

	ret = ib_check_mr_status(sig_mr, IB_MR_CHECK_SIG_STATUS, &mr_status);
	if (ret) {
		pr_err("ib_check_mr_status failed, ret %d\n", ret);
		return NVME_SC_INVALID_PI;
	}

	if (mr_status.fail_status & IB_MR_CHECK_SIG_STATUS) {
		switch (mr_status.sig_err.err_type) {
		case IB_SIG_BAD_GUARD:
			status = NVME_SC_GUARD_CHECK;
			break;
		case IB_SIG_BAD_REFTAG:
			status = NVME_SC_REFTAG_CHECK;
			break;
		case IB_SIG_BAD_APPTAG:
			status = NVME_SC_APPTAG_CHECK;
			break;
		}
		pr_err("PI error found type %d expected 0x%x vs actual 0x%x\n",
		       mr_status.sig_err.err_type,
		       mr_status.sig_err.expected,
		       mr_status.sig_err.actual);
	}

	return status;
}

static void nvmet_rdma_set_sig_domain(struct blk_integrity *bi,
		struct nvme_command *cmd, struct ib_sig_domain *domain,
		u16 control, u8 pi_type)
{
	domain->sig_type = IB_SIG_TYPE_T10_DIF;
	domain->sig.dif.bg_type = IB_T10DIF_CRC;
	domain->sig.dif.pi_interval = 1 << bi->interval_exp;
	domain->sig.dif.ref_tag = le32_to_cpu(cmd->rw.reftag);
	if (control & NVME_RW_PRINFO_PRCHK_REF)
		domain->sig.dif.ref_remap = true;

	domain->sig.dif.app_tag = le16_to_cpu(cmd->rw.apptag);
	domain->sig.dif.apptag_check_mask = le16_to_cpu(cmd->rw.appmask);
	domain->sig.dif.app_escape = true;
	if (pi_type == NVME_NS_DPS_PI_TYPE3)
		domain->sig.dif.ref_escape = true;
}

static void nvmet_rdma_set_sig_attrs(struct nvmet_req *req,
				     struct ib_sig_attrs *sig_attrs)
{
	struct nvme_command *cmd = req->cmd;
	u16 control = le16_to_cpu(cmd->rw.control);
	u8 pi_type = req->ns->pi_type;
	struct blk_integrity *bi;

	bi = bdev_get_integrity(req->ns->bdev);

	memset(sig_attrs, 0, sizeof(*sig_attrs));

	if (control & NVME_RW_PRINFO_PRACT) {
		/* for WRITE_INSERT/READ_STRIP no wire domain */
		sig_attrs->wire.sig_type = IB_SIG_TYPE_NONE;
		nvmet_rdma_set_sig_domain(bi, cmd, &sig_attrs->mem, control,
					  pi_type);
		/* Clear the PRACT bit since HCA will generate/verify the PI */
		control &= ~NVME_RW_PRINFO_PRACT;
		cmd->rw.control = cpu_to_le16(control);
		/* PI is added by the HW */
		req->transfer_len += req->metadata_len;
	} else {
		/* for WRITE_PASS/READ_PASS both wire/memory domains exist */
		nvmet_rdma_set_sig_domain(bi, cmd, &sig_attrs->wire, control,
					  pi_type);
		nvmet_rdma_set_sig_domain(bi, cmd, &sig_attrs->mem, control,
					  pi_type);
	}

	if (control & NVME_RW_PRINFO_PRCHK_REF)
		sig_attrs->check_mask |= IB_SIG_CHECK_REFTAG;
	if (control & NVME_RW_PRINFO_PRCHK_GUARD)
		sig_attrs->check_mask |= IB_SIG_CHECK_GUARD;
	if (control & NVME_RW_PRINFO_PRCHK_APP)
		sig_attrs->check_mask |= IB_SIG_CHECK_APPTAG;
}

static int nvmet_rdma_rw_ctx_init(struct nvmet_rdma_rsp *rsp, u64 addr, u32 key,
				  struct ib_sig_attrs *sig_attrs)
{
	struct rdma_cm_id *cm_id = rsp->queue->cm_id;
	struct nvmet_req *req = &rsp->req;
	int ret;

	if (req->metadata_len)
		ret = rdma_rw_ctx_signature_init(&rsp->rw, cm_id->qp,
			cm_id->port_num, req->sg, req->sg_cnt,
			req->metadata_sg, req->metadata_sg_cnt, sig_attrs,
			addr, key, nvmet_data_dir(req));
	else
		ret = rdma_rw_ctx_init(&rsp->rw, cm_id->qp, cm_id->port_num,
				       req->sg, req->sg_cnt, 0, addr, key,
				       nvmet_data_dir(req));

	return ret;
}

static void nvmet_rdma_rw_ctx_destroy(struct nvmet_rdma_rsp *rsp)
{
	struct rdma_cm_id *cm_id = rsp->queue->cm_id;
	struct nvmet_req *req = &rsp->req;

	if (req->metadata_len)
		rdma_rw_ctx_destroy_signature(&rsp->rw, cm_id->qp,
			cm_id->port_num, req->sg, req->sg_cnt,
			req->metadata_sg, req->metadata_sg_cnt,
			nvmet_data_dir(req));
	else
		rdma_rw_ctx_destroy(&rsp->rw, cm_id->qp, cm_id->port_num,
				    req->sg, req->sg_cnt, nvmet_data_dir(req));
}

static void nvmet_rdma_release_rsp(struct nvmet_rdma_rsp *rsp)
{
	struct nvmet_rdma_queue *queue = rsp->queue;

	atomic_add(1 + rsp->n_rdma, &queue->sq_wr_avail);

	if (rsp->n_rdma)
		nvmet_rdma_rw_ctx_destroy(rsp);

	if (rsp->req.sg != rsp->cmd->inline_sg)
		nvmet_req_free_sgls(&rsp->req);

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
	struct nvmet_rdma_queue *queue = wc->qp->qp_context;

	nvmet_rdma_release_rsp(rsp);

	if (unlikely(wc->status != IB_WC_SUCCESS &&
		     wc->status != IB_WC_WR_FLUSH_ERR)) {
		pr_err("SEND for CQE 0x%p failed with status %s (%d).\n",
			wc->wr_cqe, ib_wc_status_msg(wc->status), wc->status);
		nvmet_rdma_error_comp(queue);
	}
}

static void nvmet_rdma_queue_response(struct nvmet_req *req)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(req, struct nvmet_rdma_rsp, req);
	struct rdma_cm_id *cm_id = rsp->queue->cm_id;
	struct ib_send_wr *first_wr;

	if (rsp->flags & NVMET_RDMA_REQ_INVALIDATE_RKEY) {
		rsp->send_wr.opcode = IB_WR_SEND_WITH_INV;
		rsp->send_wr.ex.invalidate_rkey = rsp->invalidate_rkey;
	} else {
		rsp->send_wr.opcode = IB_WR_SEND;
	}

	if (nvmet_rdma_need_data_out(rsp)) {
		if (rsp->req.metadata_len)
			first_wr = rdma_rw_ctx_wrs(&rsp->rw, cm_id->qp,
					cm_id->port_num, &rsp->write_cqe, NULL);
		else
			first_wr = rdma_rw_ctx_wrs(&rsp->rw, cm_id->qp,
					cm_id->port_num, NULL, &rsp->send_wr);
	} else {
		first_wr = &rsp->send_wr;
	}

	nvmet_rdma_post_recv(rsp->queue->dev, rsp->cmd);

	ib_dma_sync_single_for_device(rsp->queue->dev->device,
		rsp->send_sge.addr, rsp->send_sge.length,
		DMA_TO_DEVICE);

	if (unlikely(ib_post_send(cm_id->qp, first_wr, NULL))) {
		pr_err("sending cmd response failed\n");
		nvmet_rdma_release_rsp(rsp);
	}
}

static void nvmet_rdma_read_data_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(wc->wr_cqe, struct nvmet_rdma_rsp, read_cqe);
	struct nvmet_rdma_queue *queue = wc->qp->qp_context;
	u16 status = 0;

	WARN_ON(rsp->n_rdma <= 0);
	atomic_add(rsp->n_rdma, &queue->sq_wr_avail);
	rsp->n_rdma = 0;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		nvmet_rdma_rw_ctx_destroy(rsp);
		nvmet_req_uninit(&rsp->req);
		nvmet_rdma_release_rsp(rsp);
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			pr_info("RDMA READ for CQE 0x%p failed with status %s (%d).\n",
				wc->wr_cqe, ib_wc_status_msg(wc->status), wc->status);
			nvmet_rdma_error_comp(queue);
		}
		return;
	}

	if (rsp->req.metadata_len)
		status = nvmet_rdma_check_pi_status(rsp->rw.reg->mr);
	nvmet_rdma_rw_ctx_destroy(rsp);

	if (unlikely(status))
		nvmet_req_complete(&rsp->req, status);
	else
		rsp->req.execute(&rsp->req);
}

static void nvmet_rdma_write_data_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(wc->wr_cqe, struct nvmet_rdma_rsp, write_cqe);
	struct nvmet_rdma_queue *queue = wc->qp->qp_context;
	struct rdma_cm_id *cm_id = rsp->queue->cm_id;
	u16 status;

	if (!IS_ENABLED(CONFIG_BLK_DEV_INTEGRITY))
		return;

	WARN_ON(rsp->n_rdma <= 0);
	atomic_add(rsp->n_rdma, &queue->sq_wr_avail);
	rsp->n_rdma = 0;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		nvmet_rdma_rw_ctx_destroy(rsp);
		nvmet_req_uninit(&rsp->req);
		nvmet_rdma_release_rsp(rsp);
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			pr_info("RDMA WRITE for CQE failed with status %s (%d).\n",
				ib_wc_status_msg(wc->status), wc->status);
			nvmet_rdma_error_comp(queue);
		}
		return;
	}

	/*
	 * Upon RDMA completion check the signature status
	 * - if succeeded send good NVMe response
	 * - if failed send bad NVMe response with appropriate error
	 */
	status = nvmet_rdma_check_pi_status(rsp->rw.reg->mr);
	if (unlikely(status))
		rsp->req.cqe->status = cpu_to_le16(status << 1);
	nvmet_rdma_rw_ctx_destroy(rsp);

	if (unlikely(ib_post_send(cm_id->qp, &rsp->send_wr, NULL))) {
		pr_err("sending cmd response failed\n");
		nvmet_rdma_release_rsp(rsp);
	}
}

static void nvmet_rdma_use_inline_sg(struct nvmet_rdma_rsp *rsp, u32 len,
		u64 off)
{
	int sg_count = num_pages(len);
	struct scatterlist *sg;
	int i;

	sg = rsp->cmd->inline_sg;
	for (i = 0; i < sg_count; i++, sg++) {
		if (i < sg_count - 1)
			sg_unmark_end(sg);
		else
			sg_mark_end(sg);
		sg->offset = off;
		sg->length = min_t(int, len, PAGE_SIZE - off);
		len -= sg->length;
		if (!i)
			off = 0;
	}

	rsp->req.sg = rsp->cmd->inline_sg;
	rsp->req.sg_cnt = sg_count;
}

static u16 nvmet_rdma_map_sgl_inline(struct nvmet_rdma_rsp *rsp)
{
	struct nvme_sgl_desc *sgl = &rsp->req.cmd->common.dptr.sgl;
	u64 off = le64_to_cpu(sgl->addr);
	u32 len = le32_to_cpu(sgl->length);

	if (!nvme_is_write(rsp->req.cmd)) {
		rsp->req.error_loc =
			offsetof(struct nvme_common_command, opcode);
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	if (off + len > rsp->queue->dev->inline_data_size) {
		pr_err("invalid inline data offset!\n");
		return NVME_SC_SGL_INVALID_OFFSET | NVME_SC_DNR;
	}

	/* no data command? */
	if (!len)
		return 0;

	nvmet_rdma_use_inline_sg(rsp, len, off);
	rsp->flags |= NVMET_RDMA_REQ_INLINE_DATA;
	rsp->req.transfer_len += len;
	return 0;
}

static u16 nvmet_rdma_map_sgl_keyed(struct nvmet_rdma_rsp *rsp,
		struct nvme_keyed_sgl_desc *sgl, bool invalidate)
{
	u64 addr = le64_to_cpu(sgl->addr);
	u32 key = get_unaligned_le32(sgl->key);
	struct ib_sig_attrs sig_attrs;
	int ret;

	rsp->req.transfer_len = get_unaligned_le24(sgl->length);

	/* no data command? */
	if (!rsp->req.transfer_len)
		return 0;

	if (rsp->req.metadata_len)
		nvmet_rdma_set_sig_attrs(&rsp->req, &sig_attrs);

	ret = nvmet_req_alloc_sgls(&rsp->req);
	if (unlikely(ret < 0))
		goto error_out;

	ret = nvmet_rdma_rw_ctx_init(rsp, addr, key, &sig_attrs);
	if (unlikely(ret < 0))
		goto error_out;
	rsp->n_rdma += ret;

	if (invalidate) {
		rsp->invalidate_rkey = key;
		rsp->flags |= NVMET_RDMA_REQ_INVALIDATE_RKEY;
	}

	return 0;

error_out:
	rsp->req.transfer_len = 0;
	return NVME_SC_INTERNAL;
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
			rsp->req.error_loc =
				offsetof(struct nvme_common_command, dptr);
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
			rsp->req.error_loc =
				offsetof(struct nvme_common_command, dptr);
			return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		}
	default:
		pr_err("invalid SGL type: %#x\n", sgl->type);
		rsp->req.error_loc = offsetof(struct nvme_common_command, dptr);
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
		if (rdma_rw_ctx_post(&rsp->rw, queue->qp,
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
	struct nvmet_rdma_queue *queue = wc->qp->qp_context;
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
	if (unlikely(!rsp)) {
		/*
		 * we get here only under memory pressure,
		 * silently drop and have the host retry
		 * as we can't even fail it.
		 */
		nvmet_rdma_post_recv(queue->dev, cmd);
		return;
	}
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

static void nvmet_rdma_destroy_srq(struct nvmet_rdma_srq *nsrq)
{
	nvmet_rdma_free_cmds(nsrq->ndev, nsrq->cmds, nsrq->ndev->srq_size,
			     false);
	ib_destroy_srq(nsrq->srq);

	kfree(nsrq);
}

static void nvmet_rdma_destroy_srqs(struct nvmet_rdma_device *ndev)
{
	int i;

	if (!ndev->srqs)
		return;

	for (i = 0; i < ndev->srq_count; i++)
		nvmet_rdma_destroy_srq(ndev->srqs[i]);

	kfree(ndev->srqs);
}

static struct nvmet_rdma_srq *
nvmet_rdma_init_srq(struct nvmet_rdma_device *ndev)
{
	struct ib_srq_init_attr srq_attr = { NULL, };
	size_t srq_size = ndev->srq_size;
	struct nvmet_rdma_srq *nsrq;
	struct ib_srq *srq;
	int ret, i;

	nsrq = kzalloc(sizeof(*nsrq), GFP_KERNEL);
	if (!nsrq)
		return ERR_PTR(-ENOMEM);

	srq_attr.attr.max_wr = srq_size;
	srq_attr.attr.max_sge = 1 + ndev->inline_page_count;
	srq_attr.attr.srq_limit = 0;
	srq_attr.srq_type = IB_SRQT_BASIC;
	srq = ib_create_srq(ndev->pd, &srq_attr);
	if (IS_ERR(srq)) {
		ret = PTR_ERR(srq);
		goto out_free;
	}

	nsrq->cmds = nvmet_rdma_alloc_cmds(ndev, srq_size, false);
	if (IS_ERR(nsrq->cmds)) {
		ret = PTR_ERR(nsrq->cmds);
		goto out_destroy_srq;
	}

	nsrq->srq = srq;
	nsrq->ndev = ndev;

	for (i = 0; i < srq_size; i++) {
		nsrq->cmds[i].nsrq = nsrq;
		ret = nvmet_rdma_post_recv(ndev, &nsrq->cmds[i]);
		if (ret)
			goto out_free_cmds;
	}

	return nsrq;

out_free_cmds:
	nvmet_rdma_free_cmds(ndev, nsrq->cmds, srq_size, false);
out_destroy_srq:
	ib_destroy_srq(srq);
out_free:
	kfree(nsrq);
	return ERR_PTR(ret);
}

static int nvmet_rdma_init_srqs(struct nvmet_rdma_device *ndev)
{
	int i, ret;

	if (!ndev->device->attrs.max_srq_wr || !ndev->device->attrs.max_srq) {
		/*
		 * If SRQs aren't supported we just go ahead and use normal
		 * non-shared receive queues.
		 */
		pr_info("SRQ requested but not supported.\n");
		return 0;
	}

	ndev->srq_size = min(ndev->device->attrs.max_srq_wr,
			     nvmet_rdma_srq_size);
	ndev->srq_count = min(ndev->device->num_comp_vectors,
			      ndev->device->attrs.max_srq);

	ndev->srqs = kcalloc(ndev->srq_count, sizeof(*ndev->srqs), GFP_KERNEL);
	if (!ndev->srqs)
		return -ENOMEM;

	for (i = 0; i < ndev->srq_count; i++) {
		ndev->srqs[i] = nvmet_rdma_init_srq(ndev);
		if (IS_ERR(ndev->srqs[i])) {
			ret = PTR_ERR(ndev->srqs[i]);
			goto err_srq;
		}
	}

	return 0;

err_srq:
	while (--i >= 0)
		nvmet_rdma_destroy_srq(ndev->srqs[i]);
	kfree(ndev->srqs);
	return ret;
}

static void nvmet_rdma_free_dev(struct kref *ref)
{
	struct nvmet_rdma_device *ndev =
		container_of(ref, struct nvmet_rdma_device, ref);

	mutex_lock(&device_list_mutex);
	list_del(&ndev->entry);
	mutex_unlock(&device_list_mutex);

	nvmet_rdma_destroy_srqs(ndev);
	ib_dealloc_pd(ndev->pd);

	kfree(ndev);
}

static struct nvmet_rdma_device *
nvmet_rdma_find_get_device(struct rdma_cm_id *cm_id)
{
	struct nvmet_rdma_port *port = cm_id->context;
	struct nvmet_port *nport = port->nport;
	struct nvmet_rdma_device *ndev;
	int inline_page_count;
	int inline_sge_count;
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

	inline_page_count = num_pages(nport->inline_data_size);
	inline_sge_count = max(cm_id->device->attrs.max_sge_rd,
				cm_id->device->attrs.max_recv_sge) - 1;
	if (inline_page_count > inline_sge_count) {
		pr_warn("inline_data_size %d cannot be supported by device %s. Reducing to %lu.\n",
			nport->inline_data_size, cm_id->device->name,
			inline_sge_count * PAGE_SIZE);
		nport->inline_data_size = inline_sge_count * PAGE_SIZE;
		inline_page_count = inline_sge_count;
	}
	ndev->inline_data_size = nport->inline_data_size;
	ndev->inline_page_count = inline_page_count;

	if (nport->pi_enable && !(cm_id->device->attrs.kernel_cap_flags &
				  IBK_INTEGRITY_HANDOVER)) {
		pr_warn("T10-PI is not supported by device %s. Disabling it\n",
			cm_id->device->name);
		nport->pi_enable = false;
	}

	ndev->device = cm_id->device;
	kref_init(&ndev->ref);

	ndev->pd = ib_alloc_pd(ndev->device, 0);
	if (IS_ERR(ndev->pd))
		goto out_free_dev;

	if (nvmet_rdma_use_srq) {
		ret = nvmet_rdma_init_srqs(ndev);
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
	struct ib_qp_init_attr qp_attr = { };
	struct nvmet_rdma_device *ndev = queue->dev;
	int nr_cqe, ret, i, factor;

	/*
	 * Reserve CQ slots for RECV + RDMA_READ/RDMA_WRITE + RDMA_SEND.
	 */
	nr_cqe = queue->recv_queue_size + 2 * queue->send_queue_size;

	queue->cq = ib_cq_pool_get(ndev->device, nr_cqe + 1,
				   queue->comp_vector, IB_POLL_WORKQUEUE);
	if (IS_ERR(queue->cq)) {
		ret = PTR_ERR(queue->cq);
		pr_err("failed to create CQ cqe= %d ret= %d\n",
		       nr_cqe + 1, ret);
		goto out;
	}

	qp_attr.qp_context = queue;
	qp_attr.event_handler = nvmet_rdma_qp_event;
	qp_attr.send_cq = queue->cq;
	qp_attr.recv_cq = queue->cq;
	qp_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_attr.qp_type = IB_QPT_RC;
	/* +1 for drain */
	qp_attr.cap.max_send_wr = queue->send_queue_size + 1;
	factor = rdma_rw_mr_factor(ndev->device, queue->cm_id->port_num,
				   1 << NVMET_RDMA_MAX_MDTS);
	qp_attr.cap.max_rdma_ctxs = queue->send_queue_size * factor;
	qp_attr.cap.max_send_sge = max(ndev->device->attrs.max_sge_rd,
					ndev->device->attrs.max_send_sge);

	if (queue->nsrq) {
		qp_attr.srq = queue->nsrq->srq;
	} else {
		/* +1 for drain */
		qp_attr.cap.max_recv_wr = 1 + queue->recv_queue_size;
		qp_attr.cap.max_recv_sge = 1 + ndev->inline_page_count;
	}

	if (queue->port->pi_enable && queue->host_qid)
		qp_attr.create_flags |= IB_QP_CREATE_INTEGRITY_EN;

	ret = rdma_create_qp(queue->cm_id, ndev->pd, &qp_attr);
	if (ret) {
		pr_err("failed to create_qp ret= %d\n", ret);
		goto err_destroy_cq;
	}
	queue->qp = queue->cm_id->qp;

	atomic_set(&queue->sq_wr_avail, qp_attr.cap.max_send_wr);

	pr_debug("%s: max_cqe= %d max_sge= %d sq_size = %d cm_id= %p\n",
		 __func__, queue->cq->cqe, qp_attr.cap.max_send_sge,
		 qp_attr.cap.max_send_wr, queue->cm_id);

	if (!queue->nsrq) {
		for (i = 0; i < queue->recv_queue_size; i++) {
			queue->cmds[i].queue = queue;
			ret = nvmet_rdma_post_recv(ndev, &queue->cmds[i]);
			if (ret)
				goto err_destroy_qp;
		}
	}

out:
	return ret;

err_destroy_qp:
	rdma_destroy_qp(queue->cm_id);
err_destroy_cq:
	ib_cq_pool_put(queue->cq, nr_cqe + 1);
	goto out;
}

static void nvmet_rdma_destroy_queue_ib(struct nvmet_rdma_queue *queue)
{
	ib_drain_qp(queue->qp);
	if (queue->cm_id)
		rdma_destroy_id(queue->cm_id);
	ib_destroy_qp(queue->qp);
	ib_cq_pool_put(queue->cq, queue->recv_queue_size + 2 *
		       queue->send_queue_size + 1);
}

static void nvmet_rdma_free_queue(struct nvmet_rdma_queue *queue)
{
	pr_debug("freeing queue %d\n", queue->idx);

	nvmet_sq_destroy(&queue->nvme_sq);

	nvmet_rdma_destroy_queue_ib(queue);
	if (!queue->nsrq) {
		nvmet_rdma_free_cmds(queue->dev, queue->cmds,
				queue->recv_queue_size,
				!queue->host_qid);
	}
	nvmet_rdma_free_rsps(queue);
	ida_free(&nvmet_rdma_queue_ida, queue->idx);
	kfree(queue);
}

static void nvmet_rdma_release_queue_work(struct work_struct *w)
{
	struct nvmet_rdma_queue *queue =
		container_of(w, struct nvmet_rdma_queue, release_work);
	struct nvmet_rdma_device *dev = queue->dev;

	nvmet_rdma_free_queue(queue);

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

	return rdma_reject(cm_id, (void *)&rej, sizeof(rej),
			   IB_CM_REJ_CONSUMER_DEFINED);
}

static struct nvmet_rdma_queue *
nvmet_rdma_alloc_queue(struct nvmet_rdma_device *ndev,
		struct rdma_cm_id *cm_id,
		struct rdma_cm_event *event)
{
	struct nvmet_rdma_port *port = cm_id->context;
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
	queue->port = port->nport;

	spin_lock_init(&queue->state_lock);
	queue->state = NVMET_RDMA_Q_CONNECTING;
	INIT_LIST_HEAD(&queue->rsp_wait_list);
	INIT_LIST_HEAD(&queue->rsp_wr_wait_list);
	spin_lock_init(&queue->rsp_wr_wait_lock);
	INIT_LIST_HEAD(&queue->free_rsps);
	spin_lock_init(&queue->rsps_lock);
	INIT_LIST_HEAD(&queue->queue_list);

	queue->idx = ida_alloc(&nvmet_rdma_queue_ida, GFP_KERNEL);
	if (queue->idx < 0) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_destroy_sq;
	}

	/*
	 * Spread the io queues across completion vectors,
	 * but still keep all admin queues on vector 0.
	 */
	queue->comp_vector = !queue->host_qid ? 0 :
		queue->idx % ndev->device->num_comp_vectors;


	ret = nvmet_rdma_alloc_rsps(queue);
	if (ret) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_ida_remove;
	}

	if (ndev->srqs) {
		queue->nsrq = ndev->srqs[queue->comp_vector % ndev->srq_count];
	} else {
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
	if (!queue->nsrq) {
		nvmet_rdma_free_cmds(queue->dev, queue->cmds,
				queue->recv_queue_size,
				!queue->host_qid);
	}
out_free_responses:
	nvmet_rdma_free_rsps(queue);
out_ida_remove:
	ida_free(&nvmet_rdma_queue_ida, queue->idx);
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
	case IB_EVENT_QP_LAST_WQE_REACHED:
		pr_debug("received last WQE reached event for queue=0x%p\n",
			 queue);
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

	if (queue->host_qid == 0) {
		/* Let inflight controller teardown complete */
		flush_workqueue(nvmet_wq);
	}

	ret = nvmet_rdma_cm_accept(cm_id, queue, &event->param.conn);
	if (ret) {
		/*
		 * Don't destroy the cm_id in free path, as we implicitly
		 * destroy the cm_id here with non-zero ret code.
		 */
		queue->cm_id = NULL;
		goto free_queue;
	}

	mutex_lock(&nvmet_rdma_queue_mutex);
	list_add_tail(&queue->queue_list, &nvmet_rdma_queue_list);
	mutex_unlock(&nvmet_rdma_queue_mutex);

	return 0;

free_queue:
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
		while (!list_empty(&queue->rsp_wait_list)) {
			struct nvmet_rdma_rsp *rsp;

			rsp = list_first_entry(&queue->rsp_wait_list,
					       struct nvmet_rdma_rsp,
					       wait_list);
			list_del(&rsp->wait_list);
			nvmet_rdma_put_rsp(rsp);
		}
		fallthrough;
	case NVMET_RDMA_Q_LIVE:
		queue->state = NVMET_RDMA_Q_DISCONNECTING;
		disconnect = true;
		break;
	case NVMET_RDMA_Q_DISCONNECTING:
		break;
	}
	spin_unlock_irqrestore(&queue->state_lock, flags);

	if (disconnect) {
		rdma_disconnect(queue->cm_id);
		queue_work(nvmet_wq, &queue->release_work);
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
	queue_work(nvmet_wq, &queue->release_work);
}

/**
 * nvmet_rdma_device_removal() - Handle RDMA device removal
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
	struct nvmet_rdma_port *port;

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
	if (xchg(&port->cm_id, NULL) != cm_id)
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
		if (!queue) {
			struct nvmet_rdma_port *port = cm_id->context;

			queue_delayed_work(nvmet_wq, &port->repair_work, 0);
			break;
		}
		fallthrough;
	case RDMA_CM_EVENT_DISCONNECTED:
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		nvmet_rdma_queue_disconnect(queue);
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		ret = nvmet_rdma_device_removal(cm_id, queue);
		break;
	case RDMA_CM_EVENT_REJECTED:
		pr_debug("Connection rejected: %s\n",
			 rdma_reject_msg(cm_id, event->status));
		fallthrough;
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

static void nvmet_rdma_destroy_port_queues(struct nvmet_rdma_port *port)
{
	struct nvmet_rdma_queue *queue, *tmp;
	struct nvmet_port *nport = port->nport;

	mutex_lock(&nvmet_rdma_queue_mutex);
	list_for_each_entry_safe(queue, tmp, &nvmet_rdma_queue_list,
				 queue_list) {
		if (queue->port != nport)
			continue;

		list_del_init(&queue->queue_list);
		__nvmet_rdma_queue_disconnect(queue);
	}
	mutex_unlock(&nvmet_rdma_queue_mutex);
}

static void nvmet_rdma_disable_port(struct nvmet_rdma_port *port)
{
	struct rdma_cm_id *cm_id = xchg(&port->cm_id, NULL);

	if (cm_id)
		rdma_destroy_id(cm_id);

	/*
	 * Destroy the remaining queues, which are not belong to any
	 * controller yet. Do it here after the RDMA-CM was destroyed
	 * guarantees that no new queue will be created.
	 */
	nvmet_rdma_destroy_port_queues(port);
}

static int nvmet_rdma_enable_port(struct nvmet_rdma_port *port)
{
	struct sockaddr *addr = (struct sockaddr *)&port->addr;
	struct rdma_cm_id *cm_id;
	int ret;

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

	ret = rdma_bind_addr(cm_id, addr);
	if (ret) {
		pr_err("binding CM ID to %pISpcs failed (%d)\n", addr, ret);
		goto out_destroy_id;
	}

	ret = rdma_listen(cm_id, 128);
	if (ret) {
		pr_err("listening to %pISpcs failed (%d)\n", addr, ret);
		goto out_destroy_id;
	}

	port->cm_id = cm_id;
	return 0;

out_destroy_id:
	rdma_destroy_id(cm_id);
	return ret;
}

static void nvmet_rdma_repair_port_work(struct work_struct *w)
{
	struct nvmet_rdma_port *port = container_of(to_delayed_work(w),
			struct nvmet_rdma_port, repair_work);
	int ret;

	nvmet_rdma_disable_port(port);
	ret = nvmet_rdma_enable_port(port);
	if (ret)
		queue_delayed_work(nvmet_wq, &port->repair_work, 5 * HZ);
}

static int nvmet_rdma_add_port(struct nvmet_port *nport)
{
	struct nvmet_rdma_port *port;
	__kernel_sa_family_t af;
	int ret;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	nport->priv = port;
	port->nport = nport;
	INIT_DELAYED_WORK(&port->repair_work, nvmet_rdma_repair_port_work);

	switch (nport->disc_addr.adrfam) {
	case NVMF_ADDR_FAMILY_IP4:
		af = AF_INET;
		break;
	case NVMF_ADDR_FAMILY_IP6:
		af = AF_INET6;
		break;
	default:
		pr_err("address family %d not supported\n",
			nport->disc_addr.adrfam);
		ret = -EINVAL;
		goto out_free_port;
	}

	if (nport->inline_data_size < 0) {
		nport->inline_data_size = NVMET_RDMA_DEFAULT_INLINE_DATA_SIZE;
	} else if (nport->inline_data_size > NVMET_RDMA_MAX_INLINE_DATA_SIZE) {
		pr_warn("inline_data_size %u is too large, reducing to %u\n",
			nport->inline_data_size,
			NVMET_RDMA_MAX_INLINE_DATA_SIZE);
		nport->inline_data_size = NVMET_RDMA_MAX_INLINE_DATA_SIZE;
	}

	ret = inet_pton_with_scope(&init_net, af, nport->disc_addr.traddr,
			nport->disc_addr.trsvcid, &port->addr);
	if (ret) {
		pr_err("malformed ip/port passed: %s:%s\n",
			nport->disc_addr.traddr, nport->disc_addr.trsvcid);
		goto out_free_port;
	}

	ret = nvmet_rdma_enable_port(port);
	if (ret)
		goto out_free_port;

	pr_info("enabling port %d (%pISpcs)\n",
		le16_to_cpu(nport->disc_addr.portid),
		(struct sockaddr *)&port->addr);

	return 0;

out_free_port:
	kfree(port);
	return ret;
}

static void nvmet_rdma_remove_port(struct nvmet_port *nport)
{
	struct nvmet_rdma_port *port = nport->priv;

	cancel_delayed_work_sync(&port->repair_work);
	nvmet_rdma_disable_port(port);
	kfree(port);
}

static void nvmet_rdma_disc_port_addr(struct nvmet_req *req,
		struct nvmet_port *nport, char *traddr)
{
	struct nvmet_rdma_port *port = nport->priv;
	struct rdma_cm_id *cm_id = port->cm_id;

	if (inet_addr_is_any((struct sockaddr *)&cm_id->route.addr.src_addr)) {
		struct nvmet_rdma_rsp *rsp =
			container_of(req, struct nvmet_rdma_rsp, req);
		struct rdma_cm_id *req_cm_id = rsp->queue->cm_id;
		struct sockaddr *addr = (void *)&req_cm_id->route.addr.src_addr;

		sprintf(traddr, "%pISc", addr);
	} else {
		memcpy(traddr, nport->disc_addr.traddr, NVMF_TRADDR_SIZE);
	}
}

static u8 nvmet_rdma_get_mdts(const struct nvmet_ctrl *ctrl)
{
	if (ctrl->pi_support)
		return NVMET_RDMA_MAX_METADATA_MDTS;
	return NVMET_RDMA_MAX_MDTS;
}

static u16 nvmet_rdma_get_max_queue_size(const struct nvmet_ctrl *ctrl)
{
	return NVME_RDMA_MAX_QUEUE_SIZE;
}

static const struct nvmet_fabrics_ops nvmet_rdma_ops = {
	.owner			= THIS_MODULE,
	.type			= NVMF_TRTYPE_RDMA,
	.msdbd			= 1,
	.flags			= NVMF_KEYED_SGLS | NVMF_METADATA_SUPPORTED,
	.add_port		= nvmet_rdma_add_port,
	.remove_port		= nvmet_rdma_remove_port,
	.queue_response		= nvmet_rdma_queue_response,
	.delete_ctrl		= nvmet_rdma_delete_ctrl,
	.disc_traddr		= nvmet_rdma_disc_port_addr,
	.get_mdts		= nvmet_rdma_get_mdts,
	.get_max_queue_size	= nvmet_rdma_get_max_queue_size,
};

static void nvmet_rdma_remove_one(struct ib_device *ib_device, void *client_data)
{
	struct nvmet_rdma_queue *queue, *tmp;
	struct nvmet_rdma_device *ndev;
	bool found = false;

	mutex_lock(&device_list_mutex);
	list_for_each_entry(ndev, &device_list, entry) {
		if (ndev->device == ib_device) {
			found = true;
			break;
		}
	}
	mutex_unlock(&device_list_mutex);

	if (!found)
		return;

	/*
	 * IB Device that is used by nvmet controllers is being removed,
	 * delete all queues using this device.
	 */
	mutex_lock(&nvmet_rdma_queue_mutex);
	list_for_each_entry_safe(queue, tmp, &nvmet_rdma_queue_list,
				 queue_list) {
		if (queue->dev->device != ib_device)
			continue;

		pr_info("Removing queue %d\n", queue->idx);
		list_del_init(&queue->queue_list);
		__nvmet_rdma_queue_disconnect(queue);
	}
	mutex_unlock(&nvmet_rdma_queue_mutex);

	flush_workqueue(nvmet_wq);
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
	nvmet_unregister_transport(&nvmet_rdma_ops);
	ib_unregister_client(&nvmet_rdma_ib_client);
	WARN_ON_ONCE(!list_empty(&nvmet_rdma_queue_list));
	ida_destroy(&nvmet_rdma_queue_ida);
}

module_init(nvmet_rdma_init);
module_exit(nvmet_rdma_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("nvmet-transport-1"); /* 1 == NVMF_TRTYPE_RDMA */
