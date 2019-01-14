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
#include "npc.h"
#include "cgx.h"

static int nix_update_bcast_mce_list(struct rvu *rvu, u16 pcifunc, bool add);

enum mc_tbl_sz {
	MC_TBL_SZ_256,
	MC_TBL_SZ_512,
	MC_TBL_SZ_1K,
	MC_TBL_SZ_2K,
	MC_TBL_SZ_4K,
	MC_TBL_SZ_8K,
	MC_TBL_SZ_16K,
	MC_TBL_SZ_32K,
	MC_TBL_SZ_64K,
};

enum mc_buf_cnt {
	MC_BUF_CNT_8,
	MC_BUF_CNT_16,
	MC_BUF_CNT_32,
	MC_BUF_CNT_64,
	MC_BUF_CNT_128,
	MC_BUF_CNT_256,
	MC_BUF_CNT_512,
	MC_BUF_CNT_1024,
	MC_BUF_CNT_2048,
};

/* For now considering MC resources needed for broadcast
 * pkt replication only. i.e 256 HWVFs + 12 PFs.
 */
#define MC_TBL_SIZE	MC_TBL_SZ_512
#define MC_BUF_CNT	MC_BUF_CNT_128

struct mce {
	struct hlist_node	node;
	u16			idx;
	u16			pcifunc;
};

int rvu_get_nixlf_count(struct rvu *rvu)
{
	struct rvu_block *block;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return 0;
	block = &rvu->hw->block[blkaddr];
	return block->lf.max;
}

static void nix_mce_list_init(struct nix_mce_list *list, int max)
{
	INIT_HLIST_HEAD(&list->head);
	list->count = 0;
	list->max = max;
}

static u16 nix_alloc_mce_list(struct nix_mcast *mcast, int count)
{
	int idx;

	if (!mcast)
		return 0;

	idx = mcast->next_free_mce;
	mcast->next_free_mce += count;
	return idx;
}

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

static int nix_interface_init(struct rvu *rvu, u16 pcifunc, int type, int nixlf)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	u8 cgx_id, lmac_id;
	int pkind, pf;
	int err;

	pf = rvu_get_pf(pcifunc);
	if (!is_pf_cgxmapped(rvu, pf) && type != NIX_INTF_TYPE_LBK)
		return 0;

	switch (type) {
	case NIX_INTF_TYPE_CGX:
		pfvf->cgx_lmac = rvu->pf2cgxlmac_map[pf];
		rvu_get_cgx_lmac_id(pfvf->cgx_lmac, &cgx_id, &lmac_id);

		pkind = rvu_npc_get_pkind(rvu, pf);
		if (pkind < 0) {
			dev_err(rvu->dev,
				"PF_Func 0x%x: Invalid pkind\n", pcifunc);
			return -EINVAL;
		}
		pfvf->rx_chan_base = NIX_CHAN_CGX_LMAC_CHX(cgx_id, lmac_id, 0);
		pfvf->tx_chan_base = pfvf->rx_chan_base;
		pfvf->rx_chan_cnt = 1;
		pfvf->tx_chan_cnt = 1;
		cgx_set_pkind(rvu_cgx_pdata(cgx_id, rvu), lmac_id, pkind);
		rvu_npc_set_pkind(rvu, pkind, pfvf);
		break;
	case NIX_INTF_TYPE_LBK:
		break;
	}

	/* Add a UCAST forwarding rule in MCAM with this NIXLF attached
	 * RVU PF/VF's MAC address.
	 */
	rvu_npc_install_ucast_entry(rvu, pcifunc, nixlf,
				    pfvf->rx_chan_base, pfvf->mac_addr);

	/* Add this PF_FUNC to bcast pkt replication list */
	err = nix_update_bcast_mce_list(rvu, pcifunc, true);
	if (err) {
		dev_err(rvu->dev,
			"Bcast list, failed to enable PF_FUNC 0x%x\n",
			pcifunc);
		return err;
	}

	rvu_npc_install_bcast_match_entry(rvu, pcifunc,
					  nixlf, pfvf->rx_chan_base);

	return 0;
}

static void nix_interface_deinit(struct rvu *rvu, u16 pcifunc, u8 nixlf)
{
	int err;

	/* Remove this PF_FUNC from bcast pkt replication list */
	err = nix_update_bcast_mce_list(rvu, pcifunc, false);
	if (err) {
		dev_err(rvu->dev,
			"Bcast list, failed to disable PF_FUNC 0x%x\n",
			pcifunc);
	}

	/* Free and disable any MCAM entries used by this NIX LF */
	rvu_npc_disable_mcam_entries(rvu, pcifunc, nixlf);
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
	case NIX_AQ_CTYPE_MCE:
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_RX_MCAST_CFG);
		/* Check if index exceeds MCE list length */
		if (!hw->nix0->mcast.mce_ctx ||
		    (req->qidx >= (256UL << (cfg & 0xF))))
			rc = NIX_AF_ERR_AQ_ENQUEUE;

		/* Adding multicast lists for requests from PF/VFs is not
		 * yet supported, so ignore this.
		 */
		if (rsp)
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
		else if (req->ctype == NIX_AQ_CTYPE_MCE)
			memcpy(mask, &req->mce_mask,
			       sizeof(struct nix_rx_mce_s));
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
		else if (req->ctype == NIX_AQ_CTYPE_MCE)
			memcpy(ctx, &req->mce, sizeof(struct nix_rx_mce_s));
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
				       sizeof(struct nix_rsse_s));
			else if (req->ctype == NIX_AQ_CTYPE_MCE)
				memcpy(&rsp->mce, ctx,
				       sizeof(struct nix_rx_mce_s));
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

	err = nix_interface_init(rvu, pcifunc, NIX_INTF_TYPE_CGX, nixlf);
	if (err)
		goto free_mem;

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
	rsp->rx_chan_base = pfvf->rx_chan_base;
	rsp->tx_chan_base = pfvf->tx_chan_base;
	rsp->rx_chan_cnt = pfvf->rx_chan_cnt;
	rsp->tx_chan_cnt = pfvf->tx_chan_cnt;
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

	nix_interface_deinit(rvu, pcifunc, nixlf);

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

/* Disable shaping of pkts by a scheduler queue
 * at a given scheduler level.
 */
static void nix_reset_tx_shaping(struct rvu *rvu, int blkaddr,
				 int lvl, int schq)
{
	u64  cir_reg = 0, pir_reg = 0;
	u64  cfg;

	switch (lvl) {
	case NIX_TXSCH_LVL_TL1:
		cir_reg = NIX_AF_TL1X_CIR(schq);
		pir_reg = 0; /* PIR not available at TL1 */
		break;
	case NIX_TXSCH_LVL_TL2:
		cir_reg = NIX_AF_TL2X_CIR(schq);
		pir_reg = NIX_AF_TL2X_PIR(schq);
		break;
	case NIX_TXSCH_LVL_TL3:
		cir_reg = NIX_AF_TL3X_CIR(schq);
		pir_reg = NIX_AF_TL3X_PIR(schq);
		break;
	case NIX_TXSCH_LVL_TL4:
		cir_reg = NIX_AF_TL4X_CIR(schq);
		pir_reg = NIX_AF_TL4X_PIR(schq);
		break;
	}

	if (!cir_reg)
		return;
	cfg = rvu_read64(rvu, blkaddr, cir_reg);
	rvu_write64(rvu, blkaddr, cir_reg, cfg & ~BIT_ULL(0));

	if (!pir_reg)
		return;
	cfg = rvu_read64(rvu, blkaddr, pir_reg);
	rvu_write64(rvu, blkaddr, pir_reg, cfg & ~BIT_ULL(0));
}

static void nix_reset_tx_linkcfg(struct rvu *rvu, int blkaddr,
				 int lvl, int schq)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int link;

	/* Reset TL4's SDP link config */
	if (lvl == NIX_TXSCH_LVL_TL4)
		rvu_write64(rvu, blkaddr, NIX_AF_TL4X_SDP_LINK_CFG(schq), 0x00);

	if (lvl != NIX_TXSCH_LVL_TL2)
		return;

	/* Reset TL2's CGX or LBK link config */
	for (link = 0; link < (hw->cgx_links + hw->lbk_links); link++)
		rvu_write64(rvu, blkaddr,
			    NIX_AF_TL3_TL2X_LINKX_CFG(schq, link), 0x00);
}

int rvu_mbox_handler_NIX_TXSCH_ALLOC(struct rvu *rvu,
				     struct nix_txsch_alloc_req *req,
				     struct nix_txsch_alloc_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct nix_txsch *txsch;
	int lvl, idx, req_schq;
	struct rvu_pfvf *pfvf;
	struct nix_hw *nix_hw;
	int blkaddr, rc = 0;
	u16 schq;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	spin_lock(&rvu->rsrc_lock);
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		txsch = &nix_hw->txsch[lvl];
		req_schq = req->schq_contig[lvl] + req->schq[lvl];

		/* There are only 28 TL1s */
		if (lvl == NIX_TXSCH_LVL_TL1 && req_schq > txsch->schq.max)
			goto err;

		/* Check if request is valid */
		if (!req_schq || req_schq > MAX_TXSCHQ_PER_FUNC)
			goto err;

		/* If contiguous queues are needed, check for availability */
		if (req->schq_contig[lvl] &&
		    !rvu_rsrc_check_contig(&txsch->schq, req->schq_contig[lvl]))
			goto err;

		/* Check if full request can be accommodated */
		if (req_schq >= rvu_rsrc_free_count(&txsch->schq))
			goto err;
	}

	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		txsch = &nix_hw->txsch[lvl];
		rsp->schq_contig[lvl] = req->schq_contig[lvl];
		rsp->schq[lvl] = req->schq[lvl];

		schq = 0;
		/* Alloc contiguous queues first */
		if (req->schq_contig[lvl]) {
			schq = rvu_alloc_rsrc_contig(&txsch->schq,
						     req->schq_contig[lvl]);

			for (idx = 0; idx < req->schq_contig[lvl]; idx++) {
				txsch->pfvf_map[schq] = pcifunc;
				nix_reset_tx_linkcfg(rvu, blkaddr, lvl, schq);
				nix_reset_tx_shaping(rvu, blkaddr, lvl, schq);
				rsp->schq_contig_list[lvl][idx] = schq;
				schq++;
			}
		}

		/* Alloc non-contiguous queues */
		for (idx = 0; idx < req->schq[lvl]; idx++) {
			schq = rvu_alloc_rsrc(&txsch->schq);
			txsch->pfvf_map[schq] = pcifunc;
			nix_reset_tx_linkcfg(rvu, blkaddr, lvl, schq);
			nix_reset_tx_shaping(rvu, blkaddr, lvl, schq);
			rsp->schq_list[lvl][idx] = schq;
		}
	}
	goto exit;
err:
	rc = NIX_AF_ERR_TLX_ALLOC_FAIL;
exit:
	spin_unlock(&rvu->rsrc_lock);
	return rc;
}

static int nix_txschq_free(struct rvu *rvu, u16 pcifunc)
{
	int blkaddr, nixlf, lvl, schq, err;
	struct rvu_hwinfo *hw = rvu->hw;
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;
	u64 cfg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	/* Disable TL2/3 queue links before SMQ flush*/
	spin_lock(&rvu->rsrc_lock);
	for (lvl = NIX_TXSCH_LVL_TL4; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		if (lvl != NIX_TXSCH_LVL_TL2 && lvl != NIX_TXSCH_LVL_TL4)
			continue;

		txsch = &nix_hw->txsch[lvl];
		for (schq = 0; schq < txsch->schq.max; schq++) {
			if (txsch->pfvf_map[schq] != pcifunc)
				continue;
			nix_reset_tx_linkcfg(rvu, blkaddr, lvl, schq);
		}
	}

	/* Flush SMQs */
	txsch = &nix_hw->txsch[NIX_TXSCH_LVL_SMQ];
	for (schq = 0; schq < txsch->schq.max; schq++) {
		if (txsch->pfvf_map[schq] != pcifunc)
			continue;
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_SMQX_CFG(schq));
		/* Do SMQ flush and set enqueue xoff */
		cfg |= BIT_ULL(50) | BIT_ULL(49);
		rvu_write64(rvu, blkaddr, NIX_AF_SMQX_CFG(schq), cfg);

		/* Wait for flush to complete */
		err = rvu_poll_reg(rvu, blkaddr,
				   NIX_AF_SMQX_CFG(schq), BIT_ULL(49), true);
		if (err) {
			dev_err(rvu->dev,
				"NIXLF%d: SMQ%d flush failed\n", nixlf, schq);
		}
	}

	/* Now free scheduler queues to free pool */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		txsch = &nix_hw->txsch[lvl];
		for (schq = 0; schq < txsch->schq.max; schq++) {
			if (txsch->pfvf_map[schq] != pcifunc)
				continue;
			rvu_free_rsrc(&txsch->schq, schq);
			txsch->pfvf_map[schq] = 0;
		}
	}
	spin_unlock(&rvu->rsrc_lock);

	/* Sync cached info for this LF in NDC-TX to LLC/DRAM */
	rvu_write64(rvu, blkaddr, NIX_AF_NDC_TX_SYNC, BIT_ULL(12) | nixlf);
	err = rvu_poll_reg(rvu, blkaddr, NIX_AF_NDC_TX_SYNC, BIT_ULL(12), true);
	if (err)
		dev_err(rvu->dev, "NDC-TX sync failed for NIXLF %d\n", nixlf);

	return 0;
}

int rvu_mbox_handler_NIX_TXSCH_FREE(struct rvu *rvu,
				    struct nix_txsch_free_req *req,
				    struct msg_rsp *rsp)
{
	return nix_txschq_free(rvu, req->hdr.pcifunc);
}

static bool is_txschq_config_valid(struct rvu *rvu, u16 pcifunc, int blkaddr,
				   int lvl, u64 reg, u64 regval)
{
	u64 regbase = reg & 0xFFFF;
	u16 schq, parent;

	if (!rvu_check_valid_reg(TXSCHQ_HWREGMAP, lvl, reg))
		return false;

	schq = TXSCHQ_IDX(reg, TXSCHQ_IDX_SHIFT);
	/* Check if this schq belongs to this PF/VF or not */
	if (!is_valid_txschq(rvu, blkaddr, lvl, pcifunc, schq))
		return false;

	parent = (regval >> 16) & 0x1FF;
	/* Validate MDQ's TL4 parent */
	if (regbase == NIX_AF_MDQX_PARENT(0) &&
	    !is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_TL4, pcifunc, parent))
		return false;

	/* Validate TL4's TL3 parent */
	if (regbase == NIX_AF_TL4X_PARENT(0) &&
	    !is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_TL3, pcifunc, parent))
		return false;

	/* Validate TL3's TL2 parent */
	if (regbase == NIX_AF_TL3X_PARENT(0) &&
	    !is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_TL2, pcifunc, parent))
		return false;

	/* Validate TL2's TL1 parent */
	if (regbase == NIX_AF_TL2X_PARENT(0) &&
	    !is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_TL1, pcifunc, parent))
		return false;

	return true;
}

int rvu_mbox_handler_NIX_TXSCHQ_CFG(struct rvu *rvu,
				    struct nix_txschq_config *req,
				    struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	u64 reg, regval, schq_regbase;
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;
	int blkaddr, idx, err;
	int nixlf;

	if (req->lvl >= NIX_TXSCH_LVL_CNT ||
	    req->num_regs > MAX_REGS_PER_MBOX_MSG)
		return NIX_AF_INVAL_TXSCHQ_CFG;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	txsch = &nix_hw->txsch[req->lvl];
	for (idx = 0; idx < req->num_regs; idx++) {
		reg = req->reg[idx];
		regval = req->regval[idx];
		schq_regbase = reg & 0xFFFF;

		if (!is_txschq_config_valid(rvu, pcifunc, blkaddr,
					    txsch->lvl, reg, regval))
			return NIX_AF_INVAL_TXSCHQ_CFG;

		/* Replace PF/VF visible NIXLF slot with HW NIXLF id */
		if (schq_regbase == NIX_AF_SMQX_CFG(0)) {
			nixlf = rvu_get_lf(rvu, &hw->block[blkaddr],
					   pcifunc, 0);
			regval &= ~(0x7FULL << 24);
			regval |= ((u64)nixlf << 24);
		}

		rvu_write64(rvu, blkaddr, reg, regval);

		/* Check for SMQ flush, if so, poll for its completion */
		if (schq_regbase == NIX_AF_SMQX_CFG(0) &&
		    (regval & BIT_ULL(49))) {
			err = rvu_poll_reg(rvu, blkaddr,
					   reg, BIT_ULL(49), true);
			if (err)
				return NIX_AF_SMQ_FLUSH_FAILED;
		}
	}
	return 0;
}

static int nix_rx_vtag_cfg(struct rvu *rvu, int nixlf, int blkaddr,
			   struct nix_vtag_config *req)
{
	u64 regval = 0;

#define NIX_VTAGTYPE_MAX 0x8ull
#define NIX_VTAGSIZE_MASK 0x7ull
#define NIX_VTAGSTRIP_CAP_MASK 0x30ull

	if (req->rx.vtag_type >= NIX_VTAGTYPE_MAX ||
	    req->vtag_size > VTAGSIZE_T8)
		return -EINVAL;

	regval = rvu_read64(rvu, blkaddr,
			    NIX_AF_LFX_RX_VTAG_TYPEX(nixlf, req->rx.vtag_type));

	if (req->rx.strip_vtag && req->rx.capture_vtag)
		regval |= BIT_ULL(4) | BIT_ULL(5);
	else if (req->rx.strip_vtag)
		regval |= BIT_ULL(4);
	else
		regval &= ~(BIT_ULL(4) | BIT_ULL(5));

	regval &= ~NIX_VTAGSIZE_MASK;
	regval |= req->vtag_size & NIX_VTAGSIZE_MASK;

	rvu_write64(rvu, blkaddr,
		    NIX_AF_LFX_RX_VTAG_TYPEX(nixlf, req->rx.vtag_type), regval);
	return 0;
}

int rvu_mbox_handler_NIX_VTAG_CFG(struct rvu *rvu,
				  struct nix_vtag_config *req,
				  struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, nixlf, err;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	if (req->cfg_type) {
		err = nix_rx_vtag_cfg(rvu, nixlf, blkaddr, req);
		if (err)
			return NIX_AF_ERR_PARAM;
	} else {
		/* TODO: handle tx vtag configuration */
		return 0;
	}

	return 0;
}

static int nix_setup_mce(struct rvu *rvu, int mce, u8 op,
			 u16 pcifunc, int next, bool eol)
{
	struct nix_aq_enq_req aq_req;
	int err;

	aq_req.hdr.pcifunc = pcifunc;
	aq_req.ctype = NIX_AQ_CTYPE_MCE;
	aq_req.op = op;
	aq_req.qidx = mce;

	/* Forward bcast pkts to RQ0, RSS not needed */
	aq_req.mce.op = 0;
	aq_req.mce.index = 0;
	aq_req.mce.eol = eol;
	aq_req.mce.pf_func = pcifunc;
	aq_req.mce.next = next;

	/* All fields valid */
	*(u64 *)(&aq_req.mce_mask) = ~0ULL;

	err = rvu_nix_aq_enq_inst(rvu, &aq_req, NULL);
	if (err) {
		dev_err(rvu->dev, "Failed to setup Bcast MCE for PF%d:VF%d\n",
			rvu_get_pf(pcifunc), pcifunc & RVU_PFVF_FUNC_MASK);
		return err;
	}
	return 0;
}

static int nix_update_mce_list(struct nix_mce_list *mce_list,
			       u16 pcifunc, int idx, bool add)
{
	struct mce *mce, *tail = NULL;
	bool delete = false;

	/* Scan through the current list */
	hlist_for_each_entry(mce, &mce_list->head, node) {
		/* If already exists, then delete */
		if (mce->pcifunc == pcifunc && !add) {
			delete = true;
			break;
		}
		tail = mce;
	}

	if (delete) {
		hlist_del(&mce->node);
		kfree(mce);
		mce_list->count--;
		return 0;
	}

	if (!add)
		return 0;

	/* Add a new one to the list, at the tail */
	mce = kzalloc(sizeof(*mce), GFP_ATOMIC);
	if (!mce)
		return -ENOMEM;
	mce->idx = idx;
	mce->pcifunc = pcifunc;
	if (!tail)
		hlist_add_head(&mce->node, &mce_list->head);
	else
		hlist_add_behind(&mce->node, &tail->node);
	mce_list->count++;
	return 0;
}

static int nix_update_bcast_mce_list(struct rvu *rvu, u16 pcifunc, bool add)
{
	int err = 0, idx, next_idx, count;
	struct nix_mce_list *mce_list;
	struct mce *mce, *next_mce;
	struct nix_mcast *mcast;
	struct nix_hw *nix_hw;
	struct rvu_pfvf *pfvf;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return 0;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return 0;

	mcast = &nix_hw->mcast;

	/* Get this PF/VF func's MCE index */
	pfvf = rvu_get_pfvf(rvu, pcifunc & ~RVU_PFVF_FUNC_MASK);
	idx = pfvf->bcast_mce_idx + (pcifunc & RVU_PFVF_FUNC_MASK);

	mce_list = &pfvf->bcast_mce_list;
	if (idx > (pfvf->bcast_mce_idx + mce_list->max)) {
		dev_err(rvu->dev,
			"%s: Idx %d > max MCE idx %d, for PF%d bcast list\n",
			__func__, idx, mce_list->max,
			pcifunc >> RVU_PFVF_PF_SHIFT);
		return -EINVAL;
	}

	spin_lock(&mcast->mce_lock);

	err = nix_update_mce_list(mce_list, pcifunc, idx, add);
	if (err)
		goto end;

	/* Disable MCAM entry in NPC */

	if (!mce_list->count)
		goto end;
	count = mce_list->count;

	/* Dump the updated list to HW */
	hlist_for_each_entry(mce, &mce_list->head, node) {
		next_idx = 0;
		count--;
		if (count) {
			next_mce = hlist_entry(mce->node.next,
					       struct mce, node);
			next_idx = next_mce->idx;
		}
		/* EOL should be set in last MCE */
		err = nix_setup_mce(rvu, mce->idx,
				    NIX_AQ_INSTOP_WRITE, mce->pcifunc,
				    next_idx, count ? false : true);
		if (err)
			goto end;
	}

end:
	spin_unlock(&mcast->mce_lock);
	return err;
}

static int nix_setup_bcast_tables(struct rvu *rvu, struct nix_hw *nix_hw)
{
	struct nix_mcast *mcast = &nix_hw->mcast;
	int err, pf, numvfs, idx;
	struct rvu_pfvf *pfvf;
	u16 pcifunc;
	u64 cfg;

	/* Skip PF0 (i.e AF) */
	for (pf = 1; pf < (rvu->cgx_mapped_pfs + 1); pf++) {
		cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_CFG(pf));
		/* If PF is not enabled, nothing to do */
		if (!((cfg >> 20) & 0x01))
			continue;
		/* Get numVFs attached to this PF */
		numvfs = (cfg >> 12) & 0xFF;

		pfvf = &rvu->pf[pf];
		/* Save the start MCE */
		pfvf->bcast_mce_idx = nix_alloc_mce_list(mcast, numvfs + 1);

		nix_mce_list_init(&pfvf->bcast_mce_list, numvfs + 1);

		for (idx = 0; idx < (numvfs + 1); idx++) {
			/* idx-0 is for PF, followed by VFs */
			pcifunc = (pf << RVU_PFVF_PF_SHIFT);
			pcifunc |= idx;
			/* Add dummy entries now, so that we don't have to check
			 * for whether AQ_OP should be INIT/WRITE later on.
			 * Will be updated when a NIXLF is attached/detached to
			 * these PF/VFs.
			 */
			err = nix_setup_mce(rvu, pfvf->bcast_mce_idx + idx,
					    NIX_AQ_INSTOP_INIT,
					    pcifunc, 0, true);
			if (err)
				return err;
		}
	}
	return 0;
}

static int nix_setup_mcast(struct rvu *rvu, struct nix_hw *nix_hw, int blkaddr)
{
	struct nix_mcast *mcast = &nix_hw->mcast;
	struct rvu_hwinfo *hw = rvu->hw;
	int err, size;

	size = (rvu_read64(rvu, blkaddr, NIX_AF_CONST3) >> 16) & 0x0F;
	size = (1ULL << size);

	/* Alloc memory for multicast/mirror replication entries */
	err = qmem_alloc(rvu->dev, &mcast->mce_ctx,
			 (256UL << MC_TBL_SIZE), size);
	if (err)
		return -ENOMEM;

	rvu_write64(rvu, blkaddr, NIX_AF_RX_MCAST_BASE,
		    (u64)mcast->mce_ctx->iova);

	/* Set max list length equal to max no of VFs per PF  + PF itself */
	rvu_write64(rvu, blkaddr, NIX_AF_RX_MCAST_CFG,
		    BIT_ULL(36) | (hw->max_vfs_per_pf << 4) | MC_TBL_SIZE);

	/* Alloc memory for multicast replication buffers */
	size = rvu_read64(rvu, blkaddr, NIX_AF_MC_MIRROR_CONST) & 0xFFFF;
	err = qmem_alloc(rvu->dev, &mcast->mcast_buf,
			 (8UL << MC_BUF_CNT), size);
	if (err)
		return -ENOMEM;

	rvu_write64(rvu, blkaddr, NIX_AF_RX_MCAST_BUF_BASE,
		    (u64)mcast->mcast_buf->iova);

	/* Alloc pkind for NIX internal RX multicast/mirror replay */
	mcast->replay_pkind = rvu_alloc_rsrc(&hw->pkind.rsrc);

	rvu_write64(rvu, blkaddr, NIX_AF_RX_MCAST_BUF_CFG,
		    BIT_ULL(63) | (mcast->replay_pkind << 24) |
		    BIT_ULL(20) | MC_BUF_CNT);

	spin_lock_init(&mcast->mce_lock);

	return nix_setup_bcast_tables(rvu, nix_hw);
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

int rvu_mbox_handler_NIX_STATS_RST(struct rvu *rvu, struct msg_req *req,
				   struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int i, nixlf, blkaddr;
	u64 stats;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	/* Get stats count supported by HW */
	stats = rvu_read64(rvu, blkaddr, NIX_AF_CONST1);

	/* Reset tx stats */
	for (i = 0; i < ((stats >> 24) & 0xFF); i++)
		rvu_write64(rvu, blkaddr, NIX_AF_LFX_TX_STATX(nixlf, i), 0);

	/* Reset rx stats */
	for (i = 0; i < ((stats >> 32) & 0xFF); i++)
		rvu_write64(rvu, blkaddr, NIX_AF_LFX_RX_STATX(nixlf, i), 0);

	return 0;
}

/* Returns the ALG index to be set into NPC_RX_ACTION */
static int get_flowkey_alg_idx(u32 flow_cfg)
{
	u32 ip_cfg;

	flow_cfg &= ~FLOW_KEY_TYPE_PORT;
	ip_cfg = FLOW_KEY_TYPE_IPV4 | FLOW_KEY_TYPE_IPV6;
	if (flow_cfg == ip_cfg)
		return FLOW_KEY_ALG_IP;
	else if (flow_cfg == (ip_cfg | FLOW_KEY_TYPE_TCP))
		return FLOW_KEY_ALG_TCP;
	else if (flow_cfg == (ip_cfg | FLOW_KEY_TYPE_UDP))
		return FLOW_KEY_ALG_UDP;
	else if (flow_cfg == (ip_cfg | FLOW_KEY_TYPE_SCTP))
		return FLOW_KEY_ALG_SCTP;
	else if (flow_cfg == (ip_cfg | FLOW_KEY_TYPE_TCP | FLOW_KEY_TYPE_UDP))
		return FLOW_KEY_ALG_TCP_UDP;
	else if (flow_cfg == (ip_cfg | FLOW_KEY_TYPE_TCP | FLOW_KEY_TYPE_SCTP))
		return FLOW_KEY_ALG_TCP_SCTP;
	else if (flow_cfg == (ip_cfg | FLOW_KEY_TYPE_UDP | FLOW_KEY_TYPE_SCTP))
		return FLOW_KEY_ALG_UDP_SCTP;
	else if (flow_cfg == (ip_cfg | FLOW_KEY_TYPE_TCP |
			      FLOW_KEY_TYPE_UDP | FLOW_KEY_TYPE_SCTP))
		return FLOW_KEY_ALG_TCP_UDP_SCTP;

	return FLOW_KEY_ALG_PORT;
}

int rvu_mbox_handler_NIX_RSS_FLOWKEY_CFG(struct rvu *rvu,
					 struct nix_rss_flowkey_cfg *req,
					 struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int alg_idx, nixlf, blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	alg_idx = get_flowkey_alg_idx(req->flowkey_cfg);

	rvu_npc_update_flowkey_alg_idx(rvu, pcifunc, nixlf, req->group,
				       alg_idx, req->mcam_index);
	return 0;
}

static void set_flowkey_fields(struct nix_rx_flowkey_alg *alg, u32 flow_cfg)
{
	struct nix_rx_flowkey_alg *field = NULL;
	int idx, key_type;

	if (!alg)
		return;

	/* FIELD0: IPv4
	 * FIELD1: IPv6
	 * FIELD2: TCP/UDP/SCTP/ALL
	 * FIELD3: Unused
	 * FIELD4: Unused
	 *
	 * Each of the 32 possible flow key algorithm definitions should
	 * fall into above incremental config (except ALG0). Otherwise a
	 * single NPC MCAM entry is not sufficient for supporting RSS.
	 *
	 * If a different definition or combination needed then NPC MCAM
	 * has to be programmed to filter such pkts and it's action should
	 * point to this definition to calculate flowtag or hash.
	 */
	for (idx = 0; idx < 32; idx++) {
		key_type = flow_cfg & BIT_ULL(idx);
		if (!key_type)
			continue;
		switch (key_type) {
		case FLOW_KEY_TYPE_PORT:
			field = &alg[0];
			field->sel_chan = true;
			/* This should be set to 1, when SEL_CHAN is set */
			field->bytesm1 = 1;
			break;
		case FLOW_KEY_TYPE_IPV4:
			field = &alg[0];
			field->lid = NPC_LID_LC;
			field->ltype_match = NPC_LT_LC_IP;
			field->hdr_offset = 12; /* SIP offset */
			field->bytesm1 = 7; /* SIP + DIP, 8 bytes */
			field->ltype_mask = 0xF; /* Match only IPv4 */
			break;
		case FLOW_KEY_TYPE_IPV6:
			field = &alg[1];
			field->lid = NPC_LID_LC;
			field->ltype_match = NPC_LT_LC_IP6;
			field->hdr_offset = 8; /* SIP offset */
			field->bytesm1 = 31; /* SIP + DIP, 32 bytes */
			field->ltype_mask = 0xF; /* Match only IPv6 */
			break;
		case FLOW_KEY_TYPE_TCP:
		case FLOW_KEY_TYPE_UDP:
		case FLOW_KEY_TYPE_SCTP:
			field = &alg[2];
			field->lid = NPC_LID_LD;
			field->bytesm1 = 3; /* Sport + Dport, 4 bytes */
			if (key_type == FLOW_KEY_TYPE_TCP)
				field->ltype_match |= NPC_LT_LD_TCP;
			else if (key_type == FLOW_KEY_TYPE_UDP)
				field->ltype_match |= NPC_LT_LD_UDP;
			else if (key_type == FLOW_KEY_TYPE_SCTP)
				field->ltype_match |= NPC_LT_LD_SCTP;
			field->key_offset = 32; /* After IPv4/v6 SIP, DIP */
			field->ltype_mask = ~field->ltype_match;
			break;
		}
		if (field)
			field->ena = 1;
		field = NULL;
	}
}

static void nix_rx_flowkey_alg_cfg(struct rvu *rvu, int blkaddr)
{
#define FIELDS_PER_ALG	5
	u64 field[FLOW_KEY_ALG_MAX][FIELDS_PER_ALG];
	u32 flowkey_cfg, minkey_cfg;
	int alg, fid;

	memset(&field, 0, sizeof(u64) * FLOW_KEY_ALG_MAX * FIELDS_PER_ALG);

	/* Only incoming channel number */
	flowkey_cfg = FLOW_KEY_TYPE_PORT;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_PORT], flowkey_cfg);

	/* For a incoming pkt if none of the fields match then flowkey
	 * will be zero, hence tag generated will also be zero.
	 * RSS entry at rsse_index = NIX_AF_LF()_RSS_GRP()[OFFSET] will
	 * be used to queue the packet.
	 */

	/* IPv4/IPv6 SIP/DIPs */
	flowkey_cfg = FLOW_KEY_TYPE_IPV4 | FLOW_KEY_TYPE_IPV6;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_IP], flowkey_cfg);

	/* TCPv4/v6 4-tuple, SIP, DIP, Sport, Dport */
	minkey_cfg = flowkey_cfg;
	flowkey_cfg = minkey_cfg | FLOW_KEY_TYPE_TCP;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_TCP], flowkey_cfg);

	/* UDPv4/v6 4-tuple, SIP, DIP, Sport, Dport */
	flowkey_cfg = minkey_cfg | FLOW_KEY_TYPE_UDP;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_UDP], flowkey_cfg);

	/* SCTPv4/v6 4-tuple, SIP, DIP, Sport, Dport */
	flowkey_cfg = minkey_cfg | FLOW_KEY_TYPE_SCTP;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_SCTP], flowkey_cfg);

	/* TCP/UDP v4/v6 4-tuple, rest IP pkts 2-tuple */
	flowkey_cfg = minkey_cfg | FLOW_KEY_TYPE_TCP | FLOW_KEY_TYPE_UDP;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_TCP_UDP], flowkey_cfg);

	/* TCP/SCTP v4/v6 4-tuple, rest IP pkts 2-tuple */
	flowkey_cfg = minkey_cfg | FLOW_KEY_TYPE_TCP | FLOW_KEY_TYPE_SCTP;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_TCP_SCTP], flowkey_cfg);

	/* UDP/SCTP v4/v6 4-tuple, rest IP pkts 2-tuple */
	flowkey_cfg = minkey_cfg | FLOW_KEY_TYPE_UDP | FLOW_KEY_TYPE_SCTP;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_UDP_SCTP], flowkey_cfg);

	/* TCP/UDP/SCTP v4/v6 4-tuple, rest IP pkts 2-tuple */
	flowkey_cfg = minkey_cfg | FLOW_KEY_TYPE_TCP |
		      FLOW_KEY_TYPE_UDP | FLOW_KEY_TYPE_SCTP;
	set_flowkey_fields((void *)&field[FLOW_KEY_ALG_TCP_UDP_SCTP],
			   flowkey_cfg);

	for (alg = 0; alg < FLOW_KEY_ALG_MAX; alg++) {
		for (fid = 0; fid < FIELDS_PER_ALG; fid++)
			rvu_write64(rvu, blkaddr,
				    NIX_AF_RX_FLOW_KEY_ALGX_FIELDX(alg, fid),
				    field[alg][fid]);
	}
}

int rvu_mbox_handler_NIX_SET_MAC_ADDR(struct rvu *rvu,
				      struct nix_set_mac_addr *req,
				      struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_pfvf *pfvf;
	int blkaddr, nixlf;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	ether_addr_copy(pfvf->mac_addr, req->mac_addr);

	rvu_npc_install_ucast_entry(rvu, pcifunc, nixlf,
				    pfvf->rx_chan_base, req->mac_addr);
	return 0;
}

int rvu_mbox_handler_NIX_SET_RX_MODE(struct rvu *rvu, struct nix_rx_mode *req,
				     struct msg_rsp *rsp)
{
	bool allmulti = false, disable_promisc = false;
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_pfvf *pfvf;
	int blkaddr, nixlf;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	if (req->mode & NIX_RX_MODE_PROMISC)
		allmulti = false;
	else if (req->mode & NIX_RX_MODE_ALLMULTI)
		allmulti = true;
	else
		disable_promisc = true;

	if (disable_promisc)
		rvu_npc_disable_promisc_entry(rvu, pcifunc, nixlf);
	else
		rvu_npc_install_promisc_entry(rvu, pcifunc, nixlf,
					      pfvf->rx_chan_base, allmulti);
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

		err = nix_setup_mcast(rvu, hw->nix0, blkaddr);
		if (err)
			return err;

		/* Config Outer L2, IP, TCP and UDP's NPC layer info.
		 * This helps HW protocol checker to identify headers
		 * and validate length and checksums.
		 */
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OL2,
			    (NPC_LID_LA << 8) | (NPC_LT_LA_ETHER << 4) | 0x0F);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OUDP,
			    (NPC_LID_LD << 8) | (NPC_LT_LD_UDP << 4) | 0x0F);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OTCP,
			    (NPC_LID_LD << 8) | (NPC_LT_LD_TCP << 4) | 0x0F);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OIP4,
			    (NPC_LID_LC << 8) | (NPC_LT_LC_IP << 4) | 0x0F);

		nix_rx_flowkey_alg_cfg(rvu, blkaddr);
	}
	return 0;
}

void rvu_nix_freemem(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	struct nix_txsch *txsch;
	struct nix_mcast *mcast;
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

		mcast = &nix_hw->mcast;
		qmem_free(rvu->dev, mcast->mce_ctx);
		qmem_free(rvu->dev, mcast->mcast_buf);
	}
}
