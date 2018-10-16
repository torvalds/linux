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

static void nix_ctx_free(struct rvu *rvu, struct rvu_pfvf *pfvf)
{
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

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_SQS_BASE(nixlf),
		    (u64)pfvf->sq_ctx->iova);
	cfg = BIT_ULL(36) | (req->sq_cnt - 1);
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_SQS_CFG(nixlf), cfg);

	/* Alloc NIX CQ HW context memory and config the base */
	hwctx_size = 1UL << ((ctx_cfg >> 8) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->cq_ctx, req->cq_cnt, hwctx_size);
	if (err)
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

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return 0;
	block = &hw->block[blkaddr];

	/* Calibrate X2P bus to check if CGX/LBK links are fine */
	err = nix_calibrate_x2p(rvu, blkaddr);
	if (err)
		return err;

	/* Initialize admin queue */
	err = nix_aq_init(rvu, block);
	if (err)
		return err;

	/* Restore CINT timer delay to HW reset values */
	rvu_write64(rvu, blkaddr, NIX_AF_CINT_DELAY, 0x0ULL);

	return 0;
}

void rvu_nix_freemem(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return;

	block = &hw->block[blkaddr];
	rvu_aq_free(rvu, block->aq);
}
