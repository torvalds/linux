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

static int npa_aq_enqueue_wait(struct rvu *rvu, struct rvu_block *block,
			       struct npa_aq_inst_s *inst)
{
	struct admin_queue *aq = block->aq;
	struct npa_aq_res_s *result;
	int timeout = 1000;
	u64 reg, head;

	result = (struct npa_aq_res_s *)aq->res->base;

	/* Get current head pointer where to append this instruction */
	reg = rvu_read64(rvu, block->addr, NPA_AF_AQ_STATUS);
	head = (reg >> 4) & AQ_PTR_MASK;

	memcpy((void *)(aq->inst->base + (head * aq->inst->entry_sz)),
	       (void *)inst, aq->inst->entry_sz);
	memset(result, 0, sizeof(*result));
	/* sync into memory */
	wmb();

	/* Ring the doorbell and wait for result */
	rvu_write64(rvu, block->addr, NPA_AF_AQ_DOOR, 1);
	while (result->compcode == NPA_AQ_COMP_NOTDONE) {
		cpu_relax();
		udelay(1);
		timeout--;
		if (!timeout)
			return -EBUSY;
	}

	if (result->compcode != NPA_AQ_COMP_GOOD)
		/* TODO: Replace this with some error code */
		return -EBUSY;

	return 0;
}

static int rvu_npa_aq_enq_inst(struct rvu *rvu, struct npa_aq_enq_req *req,
			       struct npa_aq_enq_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, npalf, rc = 0;
	struct npa_aq_inst_s inst;
	struct rvu_block *block;
	struct admin_queue *aq;
	struct rvu_pfvf *pfvf;
	void *ctx, *mask;
	bool ena;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	if (!pfvf->aura_ctx || req->aura_id >= pfvf->aura_ctx->qsize)
		return NPA_AF_ERR_AQ_ENQUEUE;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, pcifunc);
	if (!pfvf->npalf || blkaddr < 0)
		return NPA_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	aq = block->aq;
	if (!aq) {
		dev_warn(rvu->dev, "%s: NPA AQ not initialized\n", __func__);
		return NPA_AF_ERR_AQ_ENQUEUE;
	}

	npalf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (npalf < 0)
		return NPA_AF_ERR_AF_LF_INVALID;

	memset(&inst, 0, sizeof(struct npa_aq_inst_s));
	inst.cindex = req->aura_id;
	inst.lf = npalf;
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
	case NPA_AQ_INSTOP_WRITE:
		/* Copy context and write mask */
		if (req->ctype == NPA_AQ_CTYPE_AURA) {
			memcpy(mask, &req->aura_mask,
			       sizeof(struct npa_aura_s));
			memcpy(ctx, &req->aura, sizeof(struct npa_aura_s));
		} else {
			memcpy(mask, &req->pool_mask,
			       sizeof(struct npa_pool_s));
			memcpy(ctx, &req->pool, sizeof(struct npa_pool_s));
		}
		break;
	case NPA_AQ_INSTOP_INIT:
		if (req->ctype == NPA_AQ_CTYPE_AURA) {
			if (req->aura.pool_addr >= pfvf->pool_ctx->qsize) {
				rc = NPA_AF_ERR_AQ_FULL;
				break;
			}
			/* Set pool's context address */
			req->aura.pool_addr = pfvf->pool_ctx->iova +
			(req->aura.pool_addr * pfvf->pool_ctx->entry_sz);
			memcpy(ctx, &req->aura, sizeof(struct npa_aura_s));
		} else { /* POOL's context */
			memcpy(ctx, &req->pool, sizeof(struct npa_pool_s));
		}
		break;
	case NPA_AQ_INSTOP_NOP:
	case NPA_AQ_INSTOP_READ:
	case NPA_AQ_INSTOP_LOCK:
	case NPA_AQ_INSTOP_UNLOCK:
		break;
	default:
		rc = NPA_AF_ERR_AQ_FULL;
		break;
	}

	if (rc)
		return rc;

	spin_lock(&aq->lock);

	/* Submit the instruction to AQ */
	rc = npa_aq_enqueue_wait(rvu, block, &inst);
	if (rc) {
		spin_unlock(&aq->lock);
		return rc;
	}

	/* Set aura bitmap if aura hw context is enabled */
	if (req->ctype == NPA_AQ_CTYPE_AURA) {
		if (req->op == NPA_AQ_INSTOP_INIT && req->aura.ena)
			__set_bit(req->aura_id, pfvf->aura_bmap);
		if (req->op == NPA_AQ_INSTOP_WRITE) {
			ena = (req->aura.ena & req->aura_mask.ena) |
				(test_bit(req->aura_id, pfvf->aura_bmap) &
				~req->aura_mask.ena);
			if (ena)
				__set_bit(req->aura_id, pfvf->aura_bmap);
			else
				__clear_bit(req->aura_id, pfvf->aura_bmap);
		}
	}

	/* Set pool bitmap if pool hw context is enabled */
	if (req->ctype == NPA_AQ_CTYPE_POOL) {
		if (req->op == NPA_AQ_INSTOP_INIT && req->pool.ena)
			__set_bit(req->aura_id, pfvf->pool_bmap);
		if (req->op == NPA_AQ_INSTOP_WRITE) {
			ena = (req->pool.ena & req->pool_mask.ena) |
				(test_bit(req->aura_id, pfvf->pool_bmap) &
				~req->pool_mask.ena);
			if (ena)
				__set_bit(req->aura_id, pfvf->pool_bmap);
			else
				__clear_bit(req->aura_id, pfvf->pool_bmap);
		}
	}
	spin_unlock(&aq->lock);

	if (rsp) {
		/* Copy read context into mailbox */
		if (req->op == NPA_AQ_INSTOP_READ) {
			if (req->ctype == NPA_AQ_CTYPE_AURA)
				memcpy(&rsp->aura, ctx,
				       sizeof(struct npa_aura_s));
			else
				memcpy(&rsp->pool, ctx,
				       sizeof(struct npa_pool_s));
		}
	}

	return 0;
}

static int npa_lf_hwctx_disable(struct rvu *rvu, struct hwctx_disable_req *req)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
	struct npa_aq_enq_req aq_req;
	unsigned long *bmap;
	int id, cnt = 0;
	int err = 0, rc;

	if (!pfvf->pool_ctx || !pfvf->aura_ctx)
		return NPA_AF_ERR_AQ_ENQUEUE;

	memset(&aq_req, 0, sizeof(struct npa_aq_enq_req));
	aq_req.hdr.pcifunc = req->hdr.pcifunc;

	if (req->ctype == NPA_AQ_CTYPE_POOL) {
		aq_req.pool.ena = 0;
		aq_req.pool_mask.ena = 1;
		cnt = pfvf->pool_ctx->qsize;
		bmap = pfvf->pool_bmap;
	} else if (req->ctype == NPA_AQ_CTYPE_AURA) {
		aq_req.aura.ena = 0;
		aq_req.aura_mask.ena = 1;
		cnt = pfvf->aura_ctx->qsize;
		bmap = pfvf->aura_bmap;
	}

	aq_req.ctype = req->ctype;
	aq_req.op = NPA_AQ_INSTOP_WRITE;

	for (id = 0; id < cnt; id++) {
		if (!test_bit(id, bmap))
			continue;
		aq_req.aura_id = id;
		rc = rvu_npa_aq_enq_inst(rvu, &aq_req, NULL);
		if (rc) {
			err = rc;
			dev_err(rvu->dev, "Failed to disable %s:%d context\n",
				(req->ctype == NPA_AQ_CTYPE_AURA) ?
				"Aura" : "Pool", id);
		}
	}

	return err;
}

int rvu_mbox_handler_NPA_AQ_ENQ(struct rvu *rvu,
				struct npa_aq_enq_req *req,
				struct npa_aq_enq_rsp *rsp)
{
	return rvu_npa_aq_enq_inst(rvu, req, rsp);
}

int rvu_mbox_handler_NPA_HWCTX_DISABLE(struct rvu *rvu,
				       struct hwctx_disable_req *req,
				       struct msg_rsp *rsp)
{
	return npa_lf_hwctx_disable(rvu, req);
}

static void npa_ctx_free(struct rvu *rvu, struct rvu_pfvf *pfvf)
{
	kfree(pfvf->aura_bmap);
	pfvf->aura_bmap = NULL;

	qmem_free(rvu->dev, pfvf->aura_ctx);
	pfvf->aura_ctx = NULL;

	kfree(pfvf->pool_bmap);
	pfvf->pool_bmap = NULL;

	qmem_free(rvu->dev, pfvf->pool_ctx);
	pfvf->pool_ctx = NULL;

	qmem_free(rvu->dev, pfvf->npa_qints_ctx);
	pfvf->npa_qints_ctx = NULL;
}

int rvu_mbox_handler_NPA_LF_ALLOC(struct rvu *rvu,
				  struct npa_lf_alloc_req *req,
				  struct npa_lf_alloc_rsp *rsp)
{
	int npalf, qints, hwctx_size, err, rc = 0;
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_block *block;
	struct rvu_pfvf *pfvf;
	u64 cfg, ctx_cfg;
	int blkaddr;

	if (req->aura_sz > NPA_AURA_SZ_MAX ||
	    req->aura_sz == NPA_AURA_SZ_0 || !req->nr_pools)
		return NPA_AF_ERR_PARAM;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, pcifunc);
	if (!pfvf->npalf || blkaddr < 0)
		return NPA_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	npalf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (npalf < 0)
		return NPA_AF_ERR_AF_LF_INVALID;

	/* Reset this NPA LF */
	err = rvu_lf_reset(rvu, block, npalf);
	if (err) {
		dev_err(rvu->dev, "Failed to reset NPALF%d\n", npalf);
		return NPA_AF_ERR_LF_RESET;
	}

	ctx_cfg = rvu_read64(rvu, blkaddr, NPA_AF_CONST1);

	/* Alloc memory for aura HW contexts */
	hwctx_size = 1UL << (ctx_cfg & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->aura_ctx,
			 NPA_AURA_COUNT(req->aura_sz), hwctx_size);
	if (err)
		goto free_mem;

	pfvf->aura_bmap = kcalloc(NPA_AURA_COUNT(req->aura_sz), sizeof(long),
				  GFP_KERNEL);
	if (!pfvf->aura_bmap)
		goto free_mem;

	/* Alloc memory for pool HW contexts */
	hwctx_size = 1UL << ((ctx_cfg >> 4) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->pool_ctx, req->nr_pools, hwctx_size);
	if (err)
		goto free_mem;

	pfvf->pool_bmap = kcalloc(NPA_AURA_COUNT(req->aura_sz), sizeof(long),
				  GFP_KERNEL);
	if (!pfvf->pool_bmap)
		goto free_mem;

	/* Get no of queue interrupts supported */
	cfg = rvu_read64(rvu, blkaddr, NPA_AF_CONST);
	qints = (cfg >> 28) & 0xFFF;

	/* Alloc memory for Qints HW contexts */
	hwctx_size = 1UL << ((ctx_cfg >> 8) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->npa_qints_ctx, qints, hwctx_size);
	if (err)
		goto free_mem;

	cfg = rvu_read64(rvu, blkaddr, NPA_AF_LFX_AURAS_CFG(npalf));
	/* Clear way partition mask and set aura offset to '0' */
	cfg &= ~(BIT_ULL(34) - 1);
	/* Set aura size & enable caching of contexts */
	cfg |= (req->aura_sz << 16) | BIT_ULL(34);
	rvu_write64(rvu, blkaddr, NPA_AF_LFX_AURAS_CFG(npalf), cfg);

	/* Configure aura HW context's base */
	rvu_write64(rvu, blkaddr, NPA_AF_LFX_LOC_AURAS_BASE(npalf),
		    (u64)pfvf->aura_ctx->iova);

	/* Enable caching of qints hw context */
	rvu_write64(rvu, blkaddr, NPA_AF_LFX_QINTS_CFG(npalf), BIT_ULL(36));
	rvu_write64(rvu, blkaddr, NPA_AF_LFX_QINTS_BASE(npalf),
		    (u64)pfvf->npa_qints_ctx->iova);

	goto exit;

free_mem:
	npa_ctx_free(rvu, pfvf);
	rc = -ENOMEM;

exit:
	/* set stack page info */
	cfg = rvu_read64(rvu, blkaddr, NPA_AF_CONST);
	rsp->stack_pg_ptrs = (cfg >> 8) & 0xFF;
	rsp->stack_pg_bytes = cfg & 0xFF;
	rsp->qints = (cfg >> 28) & 0xFFF;
	return rc;
}

int rvu_mbox_handler_NPA_LF_FREE(struct rvu *rvu, struct msg_req *req,
				 struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_block *block;
	struct rvu_pfvf *pfvf;
	int npalf, err;
	int blkaddr;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, pcifunc);
	if (!pfvf->npalf || blkaddr < 0)
		return NPA_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	npalf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (npalf < 0)
		return NPA_AF_ERR_AF_LF_INVALID;

	/* Reset this NPA LF */
	err = rvu_lf_reset(rvu, block, npalf);
	if (err) {
		dev_err(rvu->dev, "Failed to reset NPALF%d\n", npalf);
		return NPA_AF_ERR_LF_RESET;
	}

	npa_ctx_free(rvu, pfvf);

	return 0;
}

static int npa_aq_init(struct rvu *rvu, struct rvu_block *block)
{
	u64 cfg;
	int err;

	/* Set admin queue endianness */
	cfg = rvu_read64(rvu, block->addr, NPA_AF_GEN_CFG);
#ifdef __BIG_ENDIAN
	cfg |= BIT_ULL(1);
	rvu_write64(rvu, block->addr, NPA_AF_GEN_CFG, cfg);
#else
	cfg &= ~BIT_ULL(1);
	rvu_write64(rvu, block->addr, NPA_AF_GEN_CFG, cfg);
#endif

	/* Do not bypass NDC cache */
	cfg = rvu_read64(rvu, block->addr, NPA_AF_NDC_CFG);
	cfg &= ~0x03DULL;
	rvu_write64(rvu, block->addr, NPA_AF_NDC_CFG, cfg);

	/* Result structure can be followed by Aura/Pool context at
	 * RES + 128bytes and a write mask at RES + 256 bytes, depending on
	 * operation type. Alloc sufficient result memory for all operations.
	 */
	err = rvu_aq_alloc(rvu, &block->aq,
			   Q_COUNT(AQ_SIZE), sizeof(struct npa_aq_inst_s),
			   ALIGN(sizeof(struct npa_aq_res_s), 128) + 256);
	if (err)
		return err;

	rvu_write64(rvu, block->addr, NPA_AF_AQ_CFG, AQ_SIZE);
	rvu_write64(rvu, block->addr,
		    NPA_AF_AQ_BASE, (u64)block->aq->inst->iova);
	return 0;
}

int rvu_npa_init(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int blkaddr, err;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return 0;

	/* Initialize admin queue */
	err = npa_aq_init(rvu, &hw->block[blkaddr]);
	if (err)
		return err;

	return 0;
}

void rvu_npa_freemem(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return;

	block = &hw->block[blkaddr];
	rvu_aq_free(rvu, block->aq);
}
