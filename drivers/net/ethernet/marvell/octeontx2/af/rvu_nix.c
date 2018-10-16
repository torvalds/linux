// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "cgx.h"

static inline struct nix_hw *get_nix_hw(struct rvu_hwinfo *hw, int blkaddr)
{
	if (blkaddr == BLKADDR_NIX0 && hw->nix0)
		return hw->nix0;

	return NULL;
}

static bool is_valid_txschq(struct rvu *rvu, int blkaddr,
			    int lvl, u16 pcifunc, u16 schq)
{
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return false;

	txsch = &nix_hw->txsch[lvl];
	/* Check out of bounds */
	if (schq >= txsch->schq.max)
		return false;

	spin_lock(&rvu->rsrc_lock);
	if (txsch->pfvf_map[schq] != pcifunc) {
		spin_unlock(&rvu->rsrc_lock);
		return false;
	}
	spin_unlock(&rvu->rsrc_lock);
	return true;
}

static void nix_setup_lso_tso_l3(struct rvu *rvu, int blkaddr,
				 u64 format, bool v4, u64 *fidx)
{
	struct nix_lso_format field = {0};

	/* IP's Length field */
	field.layer = NIX_TXLAYER_OL3;
	/* In ipv4, length field is at offset 2 bytes, for ipv6 it's 4 */
	field.offset = v4 ? 2 : 4;
	field.sizem1 = 1; /* i.e 2 bytes */
	field.alg = NIX_LSOALG_ADD_PAYLEN;
	rvu_write64(rvu, blkaddr,
		    NIX_AF_LSO_FORMATX_FIELDX(format, (*fidx)++),
		    *(u64 *)&field);

	/* No ID field in IPv6 header */
	if (!v4)
		return;

	/* IP's ID field */
	field.layer = NIX_TXLAYER_OL3;
	field.offset = 4;
	field.sizem1 = 1; /* i.e 2 bytes */
	field.alg = NIX_LSOALG_ADD_SEGNUM;
	rvu_write64(rvu, blkaddr,
		    NIX_AF_LSO_FORMATX_FIELDX(format, (*fidx)++),
		    *(u64 *)&field);
}

static void nix_setup_lso_tso_l4(struct rvu *rvu, int blkaddr,
				 u64 format, u64 *fidx)
{
	struct nix_lso_format field = {0};

	/* TCP's sequence number field */
	field.layer = NIX_TXLAYER_OL4;
	field.offset = 4;
	field.sizem1 = 3; /* i.e 4 bytes */
	field.alg = NIX_LSOALG_ADD_OFFSET;
	rvu_write64(rvu, blkaddr,
		    NIX_AF_LSO_FORMATX_FIELDX(format, (*fidx)++),
		    *(u64 *)&field);

	/* TCP's flags field */
	field.layer = NIX_TXLAYER_OL4;
	field.offset = 12;
	field.sizem1 = 0; /* not needed */
	field.alg = NIX_LSOALG_TCP_FLAGS;
	rvu_write64(rvu, blkaddr,
		    NIX_AF_LSO_FORMATX_FIELDX(format, (*fidx)++),
		    *(u64 *)&field);
}

static void nix_setup_lso(struct rvu *rvu, int blkaddr)
{
	u64 cfg, idx, fidx = 0;

	/* Enable LSO */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_LSO_CFG);
	/* For TSO, set first and middle segment flags to
	 * mask out PSH, RST & FIN flags in TCP packet
	 */
	cfg &= ~((0xFFFFULL << 32) | (0xFFFFULL << 16));
	cfg |= (0xFFF2ULL << 32) | (0xFFF2ULL << 16);
	rvu_write64(rvu, blkaddr, NIX_AF_LSO_CFG, cfg | BIT_ULL(63));

	/* Configure format fields for TCPv4 segmentation offload */
	idx = NIX_LSO_FORMAT_IDX_TSOV4;
	nix_setup_lso_tso_l3(rvu, blkaddr, idx, true, &fidx);
	nix_setup_lso_tso_l4(rvu, blkaddr, idx, &fidx);

	/* Set rest of the fields to NOP */
	for (; fidx < 8; fidx++) {
		rvu_write64(rvu, blkaddr,
			    NIX_AF_LSO_FORMATX_FIELDX(idx, fidx), 0x0ULL);
	}

	/* Configure format fields for TCPv6 segmentation offload */
	idx = NIX_LSO_FORMAT_IDX_TSOV6;
	fidx = 0;
	nix_setup_lso_tso_l3(rvu, blkaddr, idx, false, &fidx);
	nix_setup_lso_tso_l4(rvu, blkaddr, idx, &fidx);

	/* Set rest of the fields to NOP */
	for (; fidx < 8; fidx++) {
		rvu_write64(rvu, blkaddr,
			    NIX_AF_LSO_FORMATX_FIELDX(idx, fidx), 0x0ULL);
	}
}

static void nix_ctx_free(struct rvu *rvu, struct rvu_pfvf *pfvf)
{
	kfree(pfvf->rq_bmap);
	kfree(pfvf->sq_bmap);
	kfree(pfvf->cq_bmap);
	if (pfvf->rq_ctx)
		qmem_free(rvu->dev, pfvf->rq_ctx);
	if (pfvf->sq_ctx)
		qmem_free(rvu->dev, pfvf->sq_ctx);
	if (pfvf->cq_ctx)
		qmem_free(rvu->dev, pfvf->cq_ctx);
	if (pfvf->rss_ctx)
		qmem_free(rvu->dev, pfvf->rss_ctx);
	if (pfvf->nix_qints_ctx)
		qmem_free(rvu->dev, pfvf->nix_qints_ctx);
	if (pfvf->cq_ints_ctx)
		qmem_free(rvu->dev, pfvf->cq_ints_ctx);

	pfvf->rq_bmap = NULL;
	pfvf->cq_bmap = NULL;
	pfvf->sq_bmap = NULL;
	pfvf->rq_ctx = NULL;
	pfvf->sq_ctx = NULL;
	pfvf->cq_ctx = NULL;
	pfvf->rss_ctx = NULL;
	pfvf->nix_qints_ctx = NULL;
	pfvf->cq_ints_ctx = NULL;
}

static int nixlf_rss_ctx_init(struct rvu *rvu, int blkaddr,
			      struct rvu_pfvf *pfvf, int nixlf,
			      int rss_sz, int rss_grps, int hwctx_size)
{
	int err, grp, num_indices;

	/* RSS is not requested for this NIXLF */
	if (!rss_sz)
		return 0;
	num_indices = rss_sz * rss_grps;

	/* Alloc NIX RSS HW context memory and config the base */
	err = qmem_alloc(rvu->dev, &pfvf->rss_ctx, num_indices, hwctx_size);
	if (err)
		return err;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RSS_BASE(nixlf),
		    (u64)pfvf->rss_ctx->iova);

	/* Config full RSS table size, enable RSS and caching */
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RSS_CFG(nixlf),
		    BIT_ULL(36) | BIT_ULL(4) |
		    ilog2(num_indices / MAX_RSS_INDIR_TBL_SIZE));
	/* Config RSS group offset and sizes */
	for (grp = 0; grp < rss_grps; grp++)
		rvu_write64(rvu, blkaddr, NIX_AF_LFX_RSS_GRPX(nixlf, grp),
			    ((ilog2(rss_sz) - 1) << 16) | (rss_sz * grp));
	return 0;
}

static int nix_aq_enqueue_wait(struct rvu *rvu, struct rvu_block *block,
			       struct nix_aq_inst_s *inst)
{
	struct admin_queue *aq = block->aq;
	struct nix_aq_res_s *result;
	int timeout = 1000;
	u64 reg, head;

	result = (struct nix_aq_res_s *)aq->res->base;

	/* Get current head pointer where to append this instruction */
	reg = rvu_read64(rvu, block->addr, NIX_AF_AQ_STATUS);
	head = (reg >> 4) & AQ_PTR_MASK;

	memcpy((void *)(aq->inst->base + (head * aq->inst->entry_sz)),
	       (void *)inst, aq->inst->entry_sz);
	memset(result, 0, sizeof(*result));
	/* sync into memory */
	wmb();

	/* Ring the doorbell and wait for result */
	rvu_write64(rvu, block->addr, NIX_AF_AQ_DOOR, 1);
	while (result->compcode == NIX_AQ_COMP_NOTDONE) {
		cpu_relax();
		udelay(1);
		timeout--;
		if (!timeout)
			return -EBUSY;
	}

	if (result->compcode != NIX_AQ_COMP_GOOD)
		/* TODO: Replace this with some error code */
		return -EBUSY;

	return 0;
}

static int rvu_nix_aq_enq_inst(struct rvu *rvu, struct nix_aq_enq_req *req,
			       struct nix_aq_enq_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int nixlf, blkaddr, rc = 0;
	struct nix_aq_inst_s inst;
	struct rvu_block *block;
	struct admin_queue *aq;
	struct rvu_pfvf *pfvf;
	void *ctx, *mask;
	bool ena;
	u64 cfg;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	aq = block->aq;
	if (!aq) {
		dev_warn(rvu->dev, "%s: NIX AQ not initialized\n", __func__);
		return NIX_AF_ERR_AQ_ENQUEUE;
	}

	nixlf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	switch (req->ctype) {
	case NIX_AQ_CTYPE_RQ:
		/* Check if index exceeds max no of queues */
		if (!pfvf->rq_ctx || req->qidx >= pfvf->rq_ctx->qsize)
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	case NIX_AQ_CTYPE_SQ:
		if (!pfvf->sq_ctx || req->qidx >= pfvf->sq_ctx->qsize)
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	case NIX_AQ_CTYPE_CQ:
		if (!pfvf->cq_ctx || req->qidx >= pfvf->cq_ctx->qsize)
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	case NIX_AQ_CTYPE_RSS:
		/* Check if RSS is enabled and qidx is within range */
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_LFX_RSS_CFG(nixlf));
		if (!(cfg & BIT_ULL(4)) || !pfvf->rss_ctx ||
		    (req->qidx >= (256UL << (cfg & 0xF))))
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	default:
		rc = NIX_AF_ERR_AQ_ENQUEUE;
	}

	if (rc)
		return rc;

	/* Check if SQ pointed SMQ belongs to this PF/VF or not */
	if (req->ctype == NIX_AQ_CTYPE_SQ &&
	    req->op != NIX_AQ_INSTOP_WRITE) {
		if (!is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_SMQ,
				     pcifunc, req->sq.smq))
			return NIX_AF_ERR_AQ_ENQUEUE;
	}

	memset(&inst, 0, sizeof(struct nix_aq_inst_s));
	inst.lf = nixlf;
	inst.cindex = req->qidx;
	inst.ctype = req->ctype;
	inst.op = req->op;
	/* Currently we are not supporting enqueuing multiple instructions,
	 * so always choose first entry in result memory.
	 */
	inst.res_addr = (u64)aq->res->iova;

	/* Clean result + context memory */
	memset(aq->res->base, 0, aq->res->entry_sz);
	/* Context needs to be written at RES_ADDR + 128 */
	ctx = aq->res->base + 128;
	/* Mask needs to be written at RES_ADDR + 256 */
	mask = aq->res->base + 256;

	switch (req->op) {
	case NIX_AQ_INSTOP_WRITE:
		if (req->ctype == NIX_AQ_CTYPE_RQ)
			memcpy(mask, &req->rq_mask,
			       sizeof(struct nix_rq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_SQ)
			memcpy(mask, &req->sq_mask,
			       sizeof(struct nix_sq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_CQ)
			memcpy(mask, &req->cq_mask,
			       sizeof(struct nix_cq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_RSS)
			memcpy(mask, &req->rss_mask,
			       sizeof(struct nix_rsse_s));
		/* Fall through */
	case NIX_AQ_INSTOP_INIT:
		if (req->ctype == NIX_AQ_CTYPE_RQ)
			memcpy(ctx, &req->rq, sizeof(struct nix_rq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_SQ)
			memcpy(ctx, &req->sq, sizeof(struct nix_sq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_CQ)
			memcpy(ctx, &req->cq, sizeof(struct nix_cq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_RSS)
			memcpy(ctx, &req->rss, sizeof(struct nix_rsse_s));
		break;
	case NIX_AQ_INSTOP_NOP:
	case NIX_AQ_INSTOP_READ:
	case NIX_AQ_INSTOP_LOCK:
	case NIX_AQ_INSTOP_UNLOCK:
		break;
	default:
		rc = NIX_AF_ERR_AQ_ENQUEUE;
		return rc;
	}

	spin_lock(&aq->lock);

	/* Submit the instruction to AQ */
	rc = nix_aq_enqueue_wait(rvu, block, &inst);
	if (rc) {
		spin_unlock(&aq->lock);
		return rc;
	}

	/* Set RQ/SQ/CQ bitmap if respective queue hw context is enabled */
	if (req->op == NIX_AQ_INSTOP_INIT) {
		if (req->ctype == NIX_AQ_CTYPE_RQ && req->rq.ena)
			__set_bit(req->qidx, pfvf->rq_bmap);
		if (req->ctype == NIX_AQ_CTYPE_SQ && req->sq.ena)
			__set_bit(req->qidx, pfvf->sq_bmap);
		if (req->ctype == NIX_AQ_CTYPE_CQ && req->cq.ena)
			__set_bit(req->qidx, pfvf->cq_bmap);
	}

	if (req->op == NIX_AQ_INSTOP_WRITE) {
		if (req->ctype == NIX_AQ_CTYPE_RQ) {
			ena = (req->rq.ena & req->rq_mask.ena) |
				(test_bit(req->qidx, pfvf->rq_bmap) &
				~req->rq_mask.ena);
			if (ena)
				__set_bit(req->qidx, pfvf->rq_bmap);
			else
				__clear_bit(req->qidx, pfvf->rq_bmap);
		}
		if (req->ctype == NIX_AQ_CTYPE_SQ) {
			ena = (req->rq.ena & req->sq_mask.ena) |
				(test_bit(req->qidx, pfvf->sq_bmap) &
				~req->sq_mask.ena);
			if (ena)
				__set_bit(req->qidx, pfvf->sq_bmap);
			else
				__clear_bit(req->qidx, pfvf->sq_bmap);
		}
		if (req->ctype == NIX_AQ_CTYPE_CQ) {
			ena = (req->rq.ena & req->cq_mask.ena) |
				(test_bit(req->qidx, pfvf->cq_bmap) &
				~req->cq_mask.ena);
			if (ena)
				__set_bit(req->qidx, pfvf->cq_bmap);
			else
				__clear_bit(req->qidx, pfvf->cq_bmap);
		}
	}

	if (rsp) {
		/* Copy read context into mailbox */
		if (req->op == NIX_AQ_INSTOP_READ) {
			if (req->ctype == NIX_AQ_CTYPE_RQ)
				memcpy(&rsp->rq, ctx,
				       sizeof(struct nix_rq_ctx_s));
			else if (req->ctype == NIX_AQ_CTYPE_SQ)
				memcpy(&rsp->sq, ctx,
				       sizeof(struct nix_sq_ctx_s));
			else if (req->ctype == NIX_AQ_CTYPE_CQ)
				memcpy(&rsp->cq, ctx,
				       sizeof(struct nix_cq_ctx_s));
			else if (req->ctype == NIX_AQ_CTYPE_RSS)
				memcpy(&rsp->rss, ctx,
				       sizeof(struct nix_cq_ctx_s));
		}
	}

	spin_unlock(&aq->lock);
	return 0;
}

static int nix_lf_hwctx_disable(struct rvu *rvu, struct hwctx_disable_req *req)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
	struct nix_aq_enq_req aq_req;
	unsigned long *bmap;
	int qidx, q_cnt = 0;
	int err = 0, rc;

	if (!pfvf->cq_ctx || !pfvf->sq_ctx || !pfvf->rq_ctx)
		return NIX_AF_ERR_AQ_ENQUEUE;

	memset(&aq_req, 0, sizeof(struct nix_aq_enq_req));
	aq_req.hdr.pcifunc = req->hdr.pcifunc;

	if (req->ctype == NIX_AQ_CTYPE_CQ) {
		aq_req.cq.ena = 0;
		aq_req.cq_mask.ena = 1;
		q_cnt = pfvf->cq_ctx->qsize;
		bmap = pfvf->cq_bmap;
	}
	if (req->ctype == NIX_AQ_CTYPE_SQ) {
		aq_req.sq.ena = 0;
		aq_req.sq_mask.ena = 1;
		q_cnt = pfvf->sq_ctx->qsize;
		bmap = pfvf->sq_bmap;
	}
	if (req->ctype == NIX_AQ_CTYPE_RQ) {
		aq_req.rq.ena = 0;
		aq_req.rq_mask.ena = 1;
		q_cnt = pfvf->rq_ctx->qsize;
		bmap = pfvf->rq_bmap;
	}

	aq_req.ctype = req->ctype;
	aq_req.op = NIX_AQ_INSTOP_WRITE;

	for (qidx = 0; qidx < q_cnt; qidx++) {
		if (!test_bit(qidx, bmap))
			continue;
		aq_req.qidx = qidx;
		rc = rvu_nix_aq_enq_inst(rvu, &aq_req, NULL);
		if (rc) {
			err = rc;
			dev_err(rvu->dev, "Failed to disable %s:%d context\n",
				(req->ctype == NIX_AQ_CTYPE_CQ) ?
				"CQ" : ((req->ctype == NIX_AQ_CTYPE_RQ) ?
				"RQ" : "SQ"), qidx);
		}
	}

	return err;
}

int rvu_mbox_handler_NIX_AQ_ENQ(struct rvu *rvu,
				struct nix_aq_enq_req *req,
				struct nix_aq_enq_rsp *rsp)
{
	return rvu_nix_aq_enq_inst(rvu, req, rsp);
}

int rvu_mbox_handler_NIX_HWCTX_DISABLE(struct rvu *rvu,
				       struct hwctx_disable_req *req,
				       struct msg_rsp *rsp)
{
	return nix_lf_hwctx_disable(rvu, req);
}

int rvu_mbox_handler_NIX_LF_ALLOC(struct rvu *rvu,
				  struct nix_lf_alloc_req *req,
				  struct nix_lf_alloc_rsp *rsp)
{
	int nixlf, qints, hwctx_size, err, rc = 0;
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_block *block;
	struct rvu_pfvf *pfvf;
	u64 cfg, ctx_cfg;
	int blkaddr;

	if (!req->rq_cnt || !req->sq_cnt || !req->cq_cnt)
		return NIX_AF_ERR_PARAM;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	nixlf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	/* If RSS is being enabled, check if requested config is valid.
	 * RSS table size should be power of two, otherwise
	 * RSS_GRP::OFFSET + adder might go beyond that group or
	 * won't be able to use entire table.
	 */
	if (req->rss_sz && (req->rss_sz > MAX_RSS_INDIR_TBL_SIZE ||
			    !is_power_of_2(req->rss_sz)))
		return NIX_AF_ERR_RSS_SIZE_INVALID;

	if (req->rss_sz &&
	    (!req->rss_grps || req->rss_grps > MAX_RSS_GROUPS))
		return NIX_AF_ERR_RSS_GRPS_INVALID;

	/* Reset this NIX LF */
	err = rvu_lf_reset(rvu, block, nixlf);
	if (err) {
		dev_err(rvu->dev, "Failed to reset NIX%d LF%d\n",
			block->addr - BLKADDR_NIX0, nixlf);
		return NIX_AF_ERR_LF_RESET;
	}

	ctx_cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST3);

	/* Alloc NIX RQ HW context memory and config the base */
	hwctx_size = 1UL << ((ctx_cfg >> 4) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->rq_ctx, req->rq_cnt, hwctx_size);
	if (err)
		goto free_mem;

	pfvf->rq_bmap = kcalloc(req->rq_cnt, sizeof(long), GFP_KERNEL);
	if (!pfvf->rq_bmap)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RQS_BASE(nixlf),
		    (u64)pfvf->rq_ctx->iova);

	/* Set caching and queue count in HW */
	cfg = BIT_ULL(36) | (req->rq_cnt - 1);
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RQS_CFG(nixlf), cfg);

	/* Alloc NIX SQ HW context memory and config the base */
	hwctx_size = 1UL << (ctx_cfg & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->sq_ctx, req->sq_cnt, hwctx_size);
	if (err)
		goto free_mem;

	pfvf->sq_bmap = kcalloc(req->sq_cnt, sizeof(long), GFP_KERNEL);
	if (!pfvf->sq_bmap)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_SQS_BASE(nixlf),
		    (u64)pfvf->sq_ctx->iova);
	cfg = BIT_ULL(36) | (req->sq_cnt - 1);
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_SQS_CFG(nixlf), cfg);

	/* Alloc NIX CQ HW context memory and config the base */
	hwctx_size = 1UL << ((ctx_cfg >> 8) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->cq_ctx, req->cq_cnt, hwctx_size);
	if (err)
		goto free_mem;

	pfvf->cq_bmap = kcalloc(req->cq_cnt, sizeof(long), GFP_KERNEL);
	if (!pfvf->cq_bmap)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CQS_BASE(nixlf),
		    (u64)pfvf->cq_ctx->iova);
	cfg = BIT_ULL(36) | (req->cq_cnt - 1);
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CQS_CFG(nixlf), cfg);

	/* Initialize receive side scaling (RSS) */
	hwctx_size = 1UL << ((ctx_cfg >> 12) & 0xF);
	err = nixlf_rss_ctx_init(rvu, blkaddr, pfvf, nixlf,
				 req->rss_sz, req->rss_grps, hwctx_size);
	if (err)
		goto free_mem;

	/* Alloc memory for CQINT's HW contexts */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST2);
	qints = (cfg >> 24) & 0xFFF;
	hwctx_size = 1UL << ((ctx_cfg >> 24) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->cq_ints_ctx, qints, hwctx_size);
	if (err)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CINTS_BASE(nixlf),
		    (u64)pfvf->cq_ints_ctx->iova);
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CINTS_CFG(nixlf), BIT_ULL(36));

	/* Alloc memory for QINT's HW contexts */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST2);
	qints = (cfg >> 12) & 0xFFF;
	hwctx_size = 1UL << ((ctx_cfg >> 20) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->nix_qints_ctx, qints, hwctx_size);
	if (err)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_QINTS_BASE(nixlf),
		    (u64)pfvf->nix_qints_ctx->iova);
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_QINTS_CFG(nixlf), BIT_ULL(36));

	/* Enable LMTST for this NIX LF */
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_TX_CFG2(nixlf), BIT_ULL(0));

	/* Set CQE/WQE size, NPA_PF_FUNC for SQBs and also SSO_PF_FUNC
	 * If requester has sent a 'RVU_DEFAULT_PF_FUNC' use this NIX LF's
	 * PCIFUNC itself.
	 */
	if (req->npa_func == RVU_DEFAULT_PF_FUNC)
		cfg = pcifunc;
	else
		cfg = req->npa_func;

	if (req->sso_func == RVU_DEFAULT_PF_FUNC)
		cfg |= (u64)pcifunc << 16;
	else
		cfg |= (u64)req->sso_func << 16;

	cfg |= (u64)req->xqe_sz << 33;
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CFG(nixlf), cfg);

	/* Config Rx pkt length, csum checks and apad  enable / disable */
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RX_CFG(nixlf), req->rx_cfg);

	goto exit;

free_mem:
	nix_ctx_free(rvu, pfvf);
	rc = -ENOMEM;

exit:
	/* Set macaddr of this PF/VF */
	ether_addr_copy(rsp->mac_addr, pfvf->mac_addr);

	/* set SQB size info */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_SQ_CONST);
	rsp->sqb_size = (cfg >> 34) & 0xFFFF;
	rsp->lso_tsov4_idx = NIX_LSO_FORMAT_IDX_TSOV4;
	rsp->lso_tsov6_idx = NIX_LSO_FORMAT_IDX_TSOV6;
	return rc;
}

int rvu_mbox_handler_NIX_LF_FREE(struct rvu *rvu, struct msg_req *req,
				 struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_block *block;
	int blkaddr, nixlf, err;
	struct rvu_pfvf *pfvf;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	nixlf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	/* Reset this NIX LF */
	err = rvu_lf_reset(rvu, block, nixlf);
	if (err) {
		dev_err(rvu->dev, "Failed to reset NIX%d LF%d\n",
			block->addr - BLKADDR_NIX0, nixlf);
		return NIX_AF_ERR_LF_RESET;
	}

	nix_ctx_free(rvu, pfvf);

	return 0;
}

static int nix_setup_txschq(struct rvu *rvu, struct nix_hw *nix_hw, int blkaddr)
{
	struct nix_txsch *txsch;
	u64 cfg, reg;
	int err, lvl;

	/* Get scheduler queue count of each type and alloc
	 * bitmap for each for alloc/free/attach operations.
	 */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		txsch = &nix_hw->txsch[lvl];
		txsch->lvl = lvl;
		switch (lvl) {
		case NIX_TXSCH_LVL_SMQ:
			reg = NIX_AF_MDQ_CONST;
			break;
		case NIX_TXSCH_LVL_TL4:
			reg = NIX_AF_TL4_CONST;
			break;
		case NIX_TXSCH_LVL_TL3:
			reg = NIX_AF_TL3_CONST;
			break;
		case NIX_TXSCH_LVL_TL2:
			reg = NIX_AF_TL2_CONST;
			break;
		case NIX_TXSCH_LVL_TL1:
			reg = NIX_AF_TL1_CONST;
			break;
		}
		cfg = rvu_read64(rvu, blkaddr, reg);
		txsch->schq.max = cfg & 0xFFFF;
		err = rvu_alloc_bitmap(&txsch->schq);
		if (err)
			return err;

		/* Allocate memory for scheduler queues to
		 * PF/VF pcifunc mapping info.
		 */
		txsch->pfvf_map = devm_kcalloc(rvu->dev, txsch->schq.max,
					       sizeof(u16), GFP_KERNEL);
		if (!txsch->pfvf_map)
			return -ENOMEM;
	}
	return 0;
}

static int nix_calibrate_x2p(struct rvu *rvu, int blkaddr)
{
	int idx, err;
	u64 status;

	/* Start X2P bus calibration */
	rvu_write64(rvu, blkaddr, NIX_AF_CFG,
		    rvu_read64(rvu, blkaddr, NIX_AF_CFG) | BIT_ULL(9));
	/* Wait for calibration to complete */
	err = rvu_poll_reg(rvu, blkaddr,
			   NIX_AF_STATUS, BIT_ULL(10), false);
	if (err) {
		dev_err(rvu->dev, "NIX X2P bus calibration failed\n");
		return err;
	}

	status = rvu_read64(rvu, blkaddr, NIX_AF_STATUS);
	/* Check if CGX devices are ready */
	for (idx = 0; idx < cgx_get_cgx_cnt(); idx++) {
		if (status & (BIT_ULL(16 + idx)))
			continue;
		dev_err(rvu->dev,
			"CGX%d didn't respond to NIX X2P calibration\n", idx);
		err = -EBUSY;
	}

	/* Check if LBK is ready */
	if (!(status & BIT_ULL(19))) {
		dev_err(rvu->dev,
			"LBK didn't respond to NIX X2P calibration\n");
		err = -EBUSY;
	}

	/* Clear 'calibrate_x2p' bit */
	rvu_write64(rvu, blkaddr, NIX_AF_CFG,
		    rvu_read64(rvu, blkaddr, NIX_AF_CFG) & ~BIT_ULL(9));
	if (err || (status & 0x3FFULL))
		dev_err(rvu->dev,
			"NIX X2P calibration failed, status 0x%llx\n", status);
	if (err)
		return err;
	return 0;
}

static int nix_aq_init(struct rvu *rvu, struct rvu_block *block)
{
	u64 cfg;
	int err;

	/* Set admin queue endianness */
	cfg = rvu_read64(rvu, block->addr, NIX_AF_CFG);
#ifdef __BIG_ENDIAN
	cfg |= BIT_ULL(1);
	rvu_write64(rvu, block->addr, NIX_AF_CFG, cfg);
#else
	cfg &= ~BIT_ULL(1);
	rvu_write64(rvu, block->addr, NIX_AF_CFG, cfg);
#endif

	/* Do not bypass NDC cache */
	cfg = rvu_read64(rvu, block->addr, NIX_AF_NDC_CFG);
	cfg &= ~0x3FFEULL;
	rvu_write64(rvu, block->addr, NIX_AF_NDC_CFG, cfg);

	/* Result structure can be followed by RQ/SQ/CQ context at
	 * RES + 128bytes and a write mask at RES + 256 bytes, depending on
	 * operation type. Alloc sufficient result memory for all operations.
	 */
	err = rvu_aq_alloc(rvu, &block->aq,
			   Q_COUNT(AQ_SIZE), sizeof(struct nix_aq_inst_s),
			   ALIGN(sizeof(struct nix_aq_res_s), 128) + 256);
	if (err)
		return err;

	rvu_write64(rvu, block->addr, NIX_AF_AQ_CFG, AQ_SIZE);
	rvu_write64(rvu, block->addr,
		    NIX_AF_AQ_BASE, (u64)block->aq->inst->iova);
	return 0;
}

int rvu_nix_init(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int blkaddr, err;
	u64 cfg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return 0;
	block = &hw->block[blkaddr];

	/* Calibrate X2P bus to check if CGX/LBK links are fine */
	err = nix_calibrate_x2p(rvu, blkaddr);
	if (err)
		return err;

	/* Set num of links of each type */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST);
	hw->cgx = (cfg >> 12) & 0xF;
	hw->lmac_per_cgx = (cfg >> 8) & 0xF;
	hw->cgx_links = hw->cgx * hw->lmac_per_cgx;
	hw->lbk_links = 1;
	hw->sdp_links = 1;

	/* Initialize admin queue */
	err = nix_aq_init(rvu, block);
	if (err)
		return err;

	/* Restore CINT timer delay to HW reset values */
	rvu_write64(rvu, blkaddr, NIX_AF_CINT_DELAY, 0x0ULL);

	/* Configure segmentation offload formats */
	nix_setup_lso(rvu, blkaddr);

	if (blkaddr == BLKADDR_NIX0) {
		hw->nix0 = devm_kzalloc(rvu->dev,
					sizeof(struct nix_hw), GFP_KERNEL);
		if (!hw->nix0)
			return -ENOMEM;

		err = nix_setup_txschq(rvu, hw->nix0, blkaddr);
		if (err)
			return err;
	}
	return 0;
}

void rvu_nix_freemem(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;
	int blkaddr, lvl;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return;

	block = &hw->block[blkaddr];
	rvu_aq_free(rvu, block->aq);

	if (blkaddr == BLKADDR_NIX0) {
		nix_hw = get_nix_hw(rvu->hw, blkaddr);
		if (!nix_hw)
			return;

		for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
			txsch = &nix_hw->txsch[lvl];
			kfree(txsch->schq.bmap);
		}
	}
}
