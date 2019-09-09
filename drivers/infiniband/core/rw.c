// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 HGST, a Western Digital Company.
 */
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/pci-p2pdma.h>
#include <rdma/mr_pool.h>
#include <rdma/rw.h>

enum {
	RDMA_RW_SINGLE_WR,
	RDMA_RW_MULTI_WR,
	RDMA_RW_MR,
	RDMA_RW_SIG_MR,
};

static bool rdma_rw_force_mr;
module_param_named(force_mr, rdma_rw_force_mr, bool, 0);
MODULE_PARM_DESC(force_mr, "Force usage of MRs for RDMA READ/WRITE operations");

/*
 * Check if the device might use memory registration.  This is currently only
 * true for iWarp devices. In the future we can hopefully fine tune this based
 * on HCA driver input.
 */
static inline bool rdma_rw_can_use_mr(struct ib_device *dev, u8 port_num)
{
	if (rdma_protocol_iwarp(dev, port_num))
		return true;
	if (unlikely(rdma_rw_force_mr))
		return true;
	return false;
}

/*
 * Check if the device will use memory registration for this RW operation.
 * We currently always use memory registrations for iWarp RDMA READs, and
 * have a debug option to force usage of MRs.
 *
 * XXX: In the future we can hopefully fine tune this based on HCA driver
 * input.
 */
static inline bool rdma_rw_io_needs_mr(struct ib_device *dev, u8 port_num,
		enum dma_data_direction dir, int dma_nents)
{
	if (rdma_protocol_iwarp(dev, port_num) && dir == DMA_FROM_DEVICE)
		return true;
	if (unlikely(rdma_rw_force_mr))
		return true;
	return false;
}

static inline u32 rdma_rw_fr_page_list_len(struct ib_device *dev,
					   bool pi_support)
{
	u32 max_pages;

	if (pi_support)
		max_pages = dev->attrs.max_pi_fast_reg_page_list_len;
	else
		max_pages = dev->attrs.max_fast_reg_page_list_len;

	/* arbitrary limit to avoid allocating gigantic resources */
	return min_t(u32, max_pages, 256);
}

static inline int rdma_rw_inv_key(struct rdma_rw_reg_ctx *reg)
{
	int count = 0;

	if (reg->mr->need_inval) {
		reg->inv_wr.opcode = IB_WR_LOCAL_INV;
		reg->inv_wr.ex.invalidate_rkey = reg->mr->lkey;
		reg->inv_wr.next = &reg->reg_wr.wr;
		count++;
	} else {
		reg->inv_wr.next = NULL;
	}

	return count;
}

/* Caller must have zero-initialized *reg. */
static int rdma_rw_init_one_mr(struct ib_qp *qp, u8 port_num,
		struct rdma_rw_reg_ctx *reg, struct scatterlist *sg,
		u32 sg_cnt, u32 offset)
{
	u32 pages_per_mr = rdma_rw_fr_page_list_len(qp->pd->device,
						    qp->integrity_en);
	u32 nents = min(sg_cnt, pages_per_mr);
	int count = 0, ret;

	reg->mr = ib_mr_pool_get(qp, &qp->rdma_mrs);
	if (!reg->mr)
		return -EAGAIN;

	count += rdma_rw_inv_key(reg);

	ret = ib_map_mr_sg(reg->mr, sg, nents, &offset, PAGE_SIZE);
	if (ret < 0 || ret < nents) {
		ib_mr_pool_put(qp, &qp->rdma_mrs, reg->mr);
		return -EINVAL;
	}

	reg->reg_wr.wr.opcode = IB_WR_REG_MR;
	reg->reg_wr.mr = reg->mr;
	reg->reg_wr.access = IB_ACCESS_LOCAL_WRITE;
	if (rdma_protocol_iwarp(qp->device, port_num))
		reg->reg_wr.access |= IB_ACCESS_REMOTE_WRITE;
	count++;

	reg->sge.addr = reg->mr->iova;
	reg->sge.length = reg->mr->length;
	return count;
}

static int rdma_rw_init_mr_wrs(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u8 port_num, struct scatterlist *sg, u32 sg_cnt, u32 offset,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir)
{
	struct rdma_rw_reg_ctx *prev = NULL;
	u32 pages_per_mr = rdma_rw_fr_page_list_len(qp->pd->device,
						    qp->integrity_en);
	int i, j, ret = 0, count = 0;

	ctx->nr_ops = (sg_cnt + pages_per_mr - 1) / pages_per_mr;
	ctx->reg = kcalloc(ctx->nr_ops, sizeof(*ctx->reg), GFP_KERNEL);
	if (!ctx->reg) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ctx->nr_ops; i++) {
		struct rdma_rw_reg_ctx *reg = &ctx->reg[i];
		u32 nents = min(sg_cnt, pages_per_mr);

		ret = rdma_rw_init_one_mr(qp, port_num, reg, sg, sg_cnt,
				offset);
		if (ret < 0)
			goto out_free;
		count += ret;

		if (prev) {
			if (reg->mr->need_inval)
				prev->wr.wr.next = &reg->inv_wr;
			else
				prev->wr.wr.next = &reg->reg_wr.wr;
		}

		reg->reg_wr.wr.next = &reg->wr.wr;

		reg->wr.wr.sg_list = &reg->sge;
		reg->wr.wr.num_sge = 1;
		reg->wr.remote_addr = remote_addr;
		reg->wr.rkey = rkey;
		if (dir == DMA_TO_DEVICE) {
			reg->wr.wr.opcode = IB_WR_RDMA_WRITE;
		} else if (!rdma_cap_read_inv(qp->device, port_num)) {
			reg->wr.wr.opcode = IB_WR_RDMA_READ;
		} else {
			reg->wr.wr.opcode = IB_WR_RDMA_READ_WITH_INV;
			reg->wr.wr.ex.invalidate_rkey = reg->mr->lkey;
		}
		count++;

		remote_addr += reg->sge.length;
		sg_cnt -= nents;
		for (j = 0; j < nents; j++)
			sg = sg_next(sg);
		prev = reg;
		offset = 0;
	}

	if (prev)
		prev->wr.wr.next = NULL;

	ctx->type = RDMA_RW_MR;
	return count;

out_free:
	while (--i >= 0)
		ib_mr_pool_put(qp, &qp->rdma_mrs, ctx->reg[i].mr);
	kfree(ctx->reg);
out:
	return ret;
}

static int rdma_rw_init_map_wrs(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		struct scatterlist *sg, u32 sg_cnt, u32 offset,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir)
{
	u32 max_sge = dir == DMA_TO_DEVICE ? qp->max_write_sge :
		      qp->max_read_sge;
	struct ib_sge *sge;
	u32 total_len = 0, i, j;

	ctx->nr_ops = DIV_ROUND_UP(sg_cnt, max_sge);

	ctx->map.sges = sge = kcalloc(sg_cnt, sizeof(*sge), GFP_KERNEL);
	if (!ctx->map.sges)
		goto out;

	ctx->map.wrs = kcalloc(ctx->nr_ops, sizeof(*ctx->map.wrs), GFP_KERNEL);
	if (!ctx->map.wrs)
		goto out_free_sges;

	for (i = 0; i < ctx->nr_ops; i++) {
		struct ib_rdma_wr *rdma_wr = &ctx->map.wrs[i];
		u32 nr_sge = min(sg_cnt, max_sge);

		if (dir == DMA_TO_DEVICE)
			rdma_wr->wr.opcode = IB_WR_RDMA_WRITE;
		else
			rdma_wr->wr.opcode = IB_WR_RDMA_READ;
		rdma_wr->remote_addr = remote_addr + total_len;
		rdma_wr->rkey = rkey;
		rdma_wr->wr.num_sge = nr_sge;
		rdma_wr->wr.sg_list = sge;

		for (j = 0; j < nr_sge; j++, sg = sg_next(sg)) {
			sge->addr = sg_dma_address(sg) + offset;
			sge->length = sg_dma_len(sg) - offset;
			sge->lkey = qp->pd->local_dma_lkey;

			total_len += sge->length;
			sge++;
			sg_cnt--;
			offset = 0;
		}

		rdma_wr->wr.next = i + 1 < ctx->nr_ops ?
			&ctx->map.wrs[i + 1].wr : NULL;
	}

	ctx->type = RDMA_RW_MULTI_WR;
	return ctx->nr_ops;

out_free_sges:
	kfree(ctx->map.sges);
out:
	return -ENOMEM;
}

static int rdma_rw_init_single_wr(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		struct scatterlist *sg, u32 offset, u64 remote_addr, u32 rkey,
		enum dma_data_direction dir)
{
	struct ib_rdma_wr *rdma_wr = &ctx->single.wr;

	ctx->nr_ops = 1;

	ctx->single.sge.lkey = qp->pd->local_dma_lkey;
	ctx->single.sge.addr = sg_dma_address(sg) + offset;
	ctx->single.sge.length = sg_dma_len(sg) - offset;

	memset(rdma_wr, 0, sizeof(*rdma_wr));
	if (dir == DMA_TO_DEVICE)
		rdma_wr->wr.opcode = IB_WR_RDMA_WRITE;
	else
		rdma_wr->wr.opcode = IB_WR_RDMA_READ;
	rdma_wr->wr.sg_list = &ctx->single.sge;
	rdma_wr->wr.num_sge = 1;
	rdma_wr->remote_addr = remote_addr;
	rdma_wr->rkey = rkey;

	ctx->type = RDMA_RW_SINGLE_WR;
	return 1;
}

/**
 * rdma_rw_ctx_init - initialize a RDMA READ/WRITE context
 * @ctx:	context to initialize
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist to READ/WRITE from/to
 * @sg_cnt:	number of entries in @sg
 * @sg_offset:	current byte offset into @sg
 * @remote_addr:remote address to read/write (relative to @rkey)
 * @rkey:	remote key to operate on
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 *
 * Returns the number of WQEs that will be needed on the workqueue if
 * successful, or a negative error code.
 */
int rdma_rw_ctx_init(struct rdma_rw_ctx *ctx, struct ib_qp *qp, u8 port_num,
		struct scatterlist *sg, u32 sg_cnt, u32 sg_offset,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir)
{
	struct ib_device *dev = qp->pd->device;
	int ret;

	if (is_pci_p2pdma_page(sg_page(sg)))
		ret = pci_p2pdma_map_sg(dev->dma_device, sg, sg_cnt, dir);
	else
		ret = ib_dma_map_sg(dev, sg, sg_cnt, dir);

	if (!ret)
		return -ENOMEM;
	sg_cnt = ret;

	/*
	 * Skip to the S/G entry that sg_offset falls into:
	 */
	for (;;) {
		u32 len = sg_dma_len(sg);

		if (sg_offset < len)
			break;

		sg = sg_next(sg);
		sg_offset -= len;
		sg_cnt--;
	}

	ret = -EIO;
	if (WARN_ON_ONCE(sg_cnt == 0))
		goto out_unmap_sg;

	if (rdma_rw_io_needs_mr(qp->device, port_num, dir, sg_cnt)) {
		ret = rdma_rw_init_mr_wrs(ctx, qp, port_num, sg, sg_cnt,
				sg_offset, remote_addr, rkey, dir);
	} else if (sg_cnt > 1) {
		ret = rdma_rw_init_map_wrs(ctx, qp, sg, sg_cnt, sg_offset,
				remote_addr, rkey, dir);
	} else {
		ret = rdma_rw_init_single_wr(ctx, qp, sg, sg_offset,
				remote_addr, rkey, dir);
	}

	if (ret < 0)
		goto out_unmap_sg;
	return ret;

out_unmap_sg:
	ib_dma_unmap_sg(dev, sg, sg_cnt, dir);
	return ret;
}
EXPORT_SYMBOL(rdma_rw_ctx_init);

/**
 * rdma_rw_ctx_signature_init - initialize a RW context with signature offload
 * @ctx:	context to initialize
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist to READ/WRITE from/to
 * @sg_cnt:	number of entries in @sg
 * @prot_sg:	scatterlist to READ/WRITE protection information from/to
 * @prot_sg_cnt: number of entries in @prot_sg
 * @sig_attrs:	signature offloading algorithms
 * @remote_addr:remote address to read/write (relative to @rkey)
 * @rkey:	remote key to operate on
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 *
 * Returns the number of WQEs that will be needed on the workqueue if
 * successful, or a negative error code.
 */
int rdma_rw_ctx_signature_init(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u8 port_num, struct scatterlist *sg, u32 sg_cnt,
		struct scatterlist *prot_sg, u32 prot_sg_cnt,
		struct ib_sig_attrs *sig_attrs,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir)
{
	struct ib_device *dev = qp->pd->device;
	u32 pages_per_mr = rdma_rw_fr_page_list_len(qp->pd->device,
						    qp->integrity_en);
	struct ib_rdma_wr *rdma_wr;
	int count = 0, ret;

	if (sg_cnt > pages_per_mr || prot_sg_cnt > pages_per_mr) {
		pr_err("SG count too large: sg_cnt=%d, prot_sg_cnt=%d, pages_per_mr=%d\n",
		       sg_cnt, prot_sg_cnt, pages_per_mr);
		return -EINVAL;
	}

	ret = ib_dma_map_sg(dev, sg, sg_cnt, dir);
	if (!ret)
		return -ENOMEM;
	sg_cnt = ret;

	if (prot_sg_cnt) {
		ret = ib_dma_map_sg(dev, prot_sg, prot_sg_cnt, dir);
		if (!ret) {
			ret = -ENOMEM;
			goto out_unmap_sg;
		}
		prot_sg_cnt = ret;
	}

	ctx->type = RDMA_RW_SIG_MR;
	ctx->nr_ops = 1;
	ctx->reg = kcalloc(1, sizeof(*ctx->reg), GFP_KERNEL);
	if (!ctx->reg) {
		ret = -ENOMEM;
		goto out_unmap_prot_sg;
	}

	ctx->reg->mr = ib_mr_pool_get(qp, &qp->sig_mrs);
	if (!ctx->reg->mr) {
		ret = -EAGAIN;
		goto out_free_ctx;
	}

	count += rdma_rw_inv_key(ctx->reg);

	memcpy(ctx->reg->mr->sig_attrs, sig_attrs, sizeof(struct ib_sig_attrs));

	ret = ib_map_mr_sg_pi(ctx->reg->mr, sg, sg_cnt, NULL, prot_sg,
			      prot_sg_cnt, NULL, SZ_4K);
	if (unlikely(ret)) {
		pr_err("failed to map PI sg (%d)\n", sg_cnt + prot_sg_cnt);
		goto out_destroy_sig_mr;
	}

	ctx->reg->reg_wr.wr.opcode = IB_WR_REG_MR_INTEGRITY;
	ctx->reg->reg_wr.wr.wr_cqe = NULL;
	ctx->reg->reg_wr.wr.num_sge = 0;
	ctx->reg->reg_wr.wr.send_flags = 0;
	ctx->reg->reg_wr.access = IB_ACCESS_LOCAL_WRITE;
	if (rdma_protocol_iwarp(qp->device, port_num))
		ctx->reg->reg_wr.access |= IB_ACCESS_REMOTE_WRITE;
	ctx->reg->reg_wr.mr = ctx->reg->mr;
	ctx->reg->reg_wr.key = ctx->reg->mr->lkey;
	count++;

	ctx->reg->sge.addr = ctx->reg->mr->iova;
	ctx->reg->sge.length = ctx->reg->mr->length;
	if (sig_attrs->wire.sig_type == IB_SIG_TYPE_NONE)
		ctx->reg->sge.length -= ctx->reg->mr->sig_attrs->meta_length;

	rdma_wr = &ctx->reg->wr;
	rdma_wr->wr.sg_list = &ctx->reg->sge;
	rdma_wr->wr.num_sge = 1;
	rdma_wr->remote_addr = remote_addr;
	rdma_wr->rkey = rkey;
	if (dir == DMA_TO_DEVICE)
		rdma_wr->wr.opcode = IB_WR_RDMA_WRITE;
	else
		rdma_wr->wr.opcode = IB_WR_RDMA_READ;
	ctx->reg->reg_wr.wr.next = &rdma_wr->wr;
	count++;

	return count;

out_destroy_sig_mr:
	ib_mr_pool_put(qp, &qp->sig_mrs, ctx->reg->mr);
out_free_ctx:
	kfree(ctx->reg);
out_unmap_prot_sg:
	if (prot_sg_cnt)
		ib_dma_unmap_sg(dev, prot_sg, prot_sg_cnt, dir);
out_unmap_sg:
	ib_dma_unmap_sg(dev, sg, sg_cnt, dir);
	return ret;
}
EXPORT_SYMBOL(rdma_rw_ctx_signature_init);

/*
 * Now that we are going to post the WRs we can update the lkey and need_inval
 * state on the MRs.  If we were doing this at init time, we would get double
 * or missing invalidations if a context was initialized but not actually
 * posted.
 */
static void rdma_rw_update_lkey(struct rdma_rw_reg_ctx *reg, bool need_inval)
{
	reg->mr->need_inval = need_inval;
	ib_update_fast_reg_key(reg->mr, ib_inc_rkey(reg->mr->lkey));
	reg->reg_wr.key = reg->mr->lkey;
	reg->sge.lkey = reg->mr->lkey;
}

/**
 * rdma_rw_ctx_wrs - return chain of WRs for a RDMA READ or WRITE operation
 * @ctx:	context to operate on
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @cqe:	completion queue entry for the last WR
 * @chain_wr:	WR to append to the posted chain
 *
 * Return the WR chain for the set of RDMA READ/WRITE operations described by
 * @ctx, as well as any memory registration operations needed.  If @chain_wr
 * is non-NULL the WR it points to will be appended to the chain of WRs posted.
 * If @chain_wr is not set @cqe must be set so that the caller gets a
 * completion notification.
 */
struct ib_send_wr *rdma_rw_ctx_wrs(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u8 port_num, struct ib_cqe *cqe, struct ib_send_wr *chain_wr)
{
	struct ib_send_wr *first_wr, *last_wr;
	int i;

	switch (ctx->type) {
	case RDMA_RW_SIG_MR:
	case RDMA_RW_MR:
		/* fallthrough */
		for (i = 0; i < ctx->nr_ops; i++) {
			rdma_rw_update_lkey(&ctx->reg[i],
				ctx->reg[i].wr.wr.opcode !=
					IB_WR_RDMA_READ_WITH_INV);
		}

		if (ctx->reg[0].inv_wr.next)
			first_wr = &ctx->reg[0].inv_wr;
		else
			first_wr = &ctx->reg[0].reg_wr.wr;
		last_wr = &ctx->reg[ctx->nr_ops - 1].wr.wr;
		break;
	case RDMA_RW_MULTI_WR:
		first_wr = &ctx->map.wrs[0].wr;
		last_wr = &ctx->map.wrs[ctx->nr_ops - 1].wr;
		break;
	case RDMA_RW_SINGLE_WR:
		first_wr = &ctx->single.wr.wr;
		last_wr = &ctx->single.wr.wr;
		break;
	default:
		BUG();
	}

	if (chain_wr) {
		last_wr->next = chain_wr;
	} else {
		last_wr->wr_cqe = cqe;
		last_wr->send_flags |= IB_SEND_SIGNALED;
	}

	return first_wr;
}
EXPORT_SYMBOL(rdma_rw_ctx_wrs);

/**
 * rdma_rw_ctx_post - post a RDMA READ or RDMA WRITE operation
 * @ctx:	context to operate on
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @cqe:	completion queue entry for the last WR
 * @chain_wr:	WR to append to the posted chain
 *
 * Post the set of RDMA READ/WRITE operations described by @ctx, as well as
 * any memory registration operations needed.  If @chain_wr is non-NULL the
 * WR it points to will be appended to the chain of WRs posted.  If @chain_wr
 * is not set @cqe must be set so that the caller gets a completion
 * notification.
 */
int rdma_rw_ctx_post(struct rdma_rw_ctx *ctx, struct ib_qp *qp, u8 port_num,
		struct ib_cqe *cqe, struct ib_send_wr *chain_wr)
{
	struct ib_send_wr *first_wr;

	first_wr = rdma_rw_ctx_wrs(ctx, qp, port_num, cqe, chain_wr);
	return ib_post_send(qp, first_wr, NULL);
}
EXPORT_SYMBOL(rdma_rw_ctx_post);

/**
 * rdma_rw_ctx_destroy - release all resources allocated by rdma_rw_ctx_init
 * @ctx:	context to release
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist that was used for the READ/WRITE
 * @sg_cnt:	number of entries in @sg
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 */
void rdma_rw_ctx_destroy(struct rdma_rw_ctx *ctx, struct ib_qp *qp, u8 port_num,
		struct scatterlist *sg, u32 sg_cnt, enum dma_data_direction dir)
{
	int i;

	switch (ctx->type) {
	case RDMA_RW_MR:
		for (i = 0; i < ctx->nr_ops; i++)
			ib_mr_pool_put(qp, &qp->rdma_mrs, ctx->reg[i].mr);
		kfree(ctx->reg);
		break;
	case RDMA_RW_MULTI_WR:
		kfree(ctx->map.wrs);
		kfree(ctx->map.sges);
		break;
	case RDMA_RW_SINGLE_WR:
		break;
	default:
		BUG();
		break;
	}

	/* P2PDMA contexts do not need to be unmapped */
	if (!is_pci_p2pdma_page(sg_page(sg)))
		ib_dma_unmap_sg(qp->pd->device, sg, sg_cnt, dir);
}
EXPORT_SYMBOL(rdma_rw_ctx_destroy);

/**
 * rdma_rw_ctx_destroy_signature - release all resources allocated by
 *	rdma_rw_ctx_signature_init
 * @ctx:	context to release
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist that was used for the READ/WRITE
 * @sg_cnt:	number of entries in @sg
 * @prot_sg:	scatterlist that was used for the READ/WRITE of the PI
 * @prot_sg_cnt: number of entries in @prot_sg
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 */
void rdma_rw_ctx_destroy_signature(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u8 port_num, struct scatterlist *sg, u32 sg_cnt,
		struct scatterlist *prot_sg, u32 prot_sg_cnt,
		enum dma_data_direction dir)
{
	if (WARN_ON_ONCE(ctx->type != RDMA_RW_SIG_MR))
		return;

	ib_mr_pool_put(qp, &qp->sig_mrs, ctx->reg->mr);
	kfree(ctx->reg);

	ib_dma_unmap_sg(qp->pd->device, sg, sg_cnt, dir);
	if (prot_sg_cnt)
		ib_dma_unmap_sg(qp->pd->device, prot_sg, prot_sg_cnt, dir);
}
EXPORT_SYMBOL(rdma_rw_ctx_destroy_signature);

/**
 * rdma_rw_mr_factor - return number of MRs required for a payload
 * @device:	device handling the connection
 * @port_num:	port num to which the connection is bound
 * @maxpages:	maximum payload pages per rdma_rw_ctx
 *
 * Returns the number of MRs the device requires to move @maxpayload
 * bytes. The returned value is used during transport creation to
 * compute max_rdma_ctxts and the size of the transport's Send and
 * Send Completion Queues.
 */
unsigned int rdma_rw_mr_factor(struct ib_device *device, u8 port_num,
			       unsigned int maxpages)
{
	unsigned int mr_pages;

	if (rdma_rw_can_use_mr(device, port_num))
		mr_pages = rdma_rw_fr_page_list_len(device, false);
	else
		mr_pages = device->attrs.max_sge_rd;
	return DIV_ROUND_UP(maxpages, mr_pages);
}
EXPORT_SYMBOL(rdma_rw_mr_factor);

void rdma_rw_init_qp(struct ib_device *dev, struct ib_qp_init_attr *attr)
{
	u32 factor;

	WARN_ON_ONCE(attr->port_num == 0);

	/*
	 * Each context needs at least one RDMA READ or WRITE WR.
	 *
	 * For some hardware we might need more, eventually we should ask the
	 * HCA driver for a multiplier here.
	 */
	factor = 1;

	/*
	 * If the devices needs MRs to perform RDMA READ or WRITE operations,
	 * we'll need two additional MRs for the registrations and the
	 * invalidation.
	 */
	if (attr->create_flags & IB_QP_CREATE_INTEGRITY_EN ||
	    rdma_rw_can_use_mr(dev, attr->port_num))
		factor += 2;	/* inv + reg */

	attr->cap.max_send_wr += factor * attr->cap.max_rdma_ctxs;

	/*
	 * But maybe we were just too high in the sky and the device doesn't
	 * even support all we need, and we'll have to live with what we get..
	 */
	attr->cap.max_send_wr =
		min_t(u32, attr->cap.max_send_wr, dev->attrs.max_qp_wr);
}

int rdma_rw_init_mrs(struct ib_qp *qp, struct ib_qp_init_attr *attr)
{
	struct ib_device *dev = qp->pd->device;
	u32 nr_mrs = 0, nr_sig_mrs = 0, max_num_sg = 0;
	int ret = 0;

	if (attr->create_flags & IB_QP_CREATE_INTEGRITY_EN) {
		nr_sig_mrs = attr->cap.max_rdma_ctxs;
		nr_mrs = attr->cap.max_rdma_ctxs;
		max_num_sg = rdma_rw_fr_page_list_len(dev, true);
	} else if (rdma_rw_can_use_mr(dev, attr->port_num)) {
		nr_mrs = attr->cap.max_rdma_ctxs;
		max_num_sg = rdma_rw_fr_page_list_len(dev, false);
	}

	if (nr_mrs) {
		ret = ib_mr_pool_init(qp, &qp->rdma_mrs, nr_mrs,
				IB_MR_TYPE_MEM_REG,
				max_num_sg, 0);
		if (ret) {
			pr_err("%s: failed to allocated %d MRs\n",
				__func__, nr_mrs);
			return ret;
		}
	}

	if (nr_sig_mrs) {
		ret = ib_mr_pool_init(qp, &qp->sig_mrs, nr_sig_mrs,
				IB_MR_TYPE_INTEGRITY, max_num_sg, max_num_sg);
		if (ret) {
			pr_err("%s: failed to allocated %d SIG MRs\n",
				__func__, nr_sig_mrs);
			goto out_free_rdma_mrs;
		}
	}

	return 0;

out_free_rdma_mrs:
	ib_mr_pool_destroy(qp, &qp->rdma_mrs);
	return ret;
}

void rdma_rw_cleanup_mrs(struct ib_qp *qp)
{
	ib_mr_pool_destroy(qp, &qp->sig_mrs);
	ib_mr_pool_destroy(qp, &qp->rdma_mrs);
}
