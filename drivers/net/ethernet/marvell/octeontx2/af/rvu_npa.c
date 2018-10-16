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

static void npa_ctx_free(struct rvu *rvu, struct rvu_pfvf *pfvf)
{
	qmem_free(rvu->dev, pfvf->aura_ctx);
	pfvf->aura_ctx = NULL;

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

	/* Alloc memory for pool HW contexts */
	hwctx_size = 1UL << ((ctx_cfg >> 4) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->pool_ctx, req->nr_pools, hwctx_size);
	if (err)
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
	struct rvu_block *block;
	int blkaddr, err;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPA, 0);
	if (blkaddr < 0)
		return 0;

	block = &hw->block[blkaddr];

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
