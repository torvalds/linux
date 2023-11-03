// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include "aspeed-rsss.h"

//#define RSSS_SHA3_POLLING_MODE

static int aspeed_sha3_dma_prepare(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;
	struct aspeed_sha3_reqctx *rctx;
	int length, remain;

	rctx = ahash_request_ctx(req);
	remain = (rctx->total + rctx->bufcnt) % rctx->blksize;
	length = rctx->total + rctx->bufcnt - remain;

	RSSS_DBG(rsss_dev, "%s:0x%x, %s:%zu, %s:0x%x, %s:0x%x, %s:0x%x\n",
		 "rctx total", rctx->total, "bufcnt", rctx->bufcnt,
		 "offset", rctx->offset, "length", length,
		 "remain", remain);

	if (rctx->bufcnt)
		memcpy(sha3_engine->ahash_src_addr, sha3_engine->buffer_addr,
		       rctx->bufcnt);

	if (length < ASPEED_HASH_SRC_DMA_BUF_LEN) {
		scatterwalk_map_and_copy(sha3_engine->ahash_src_addr + rctx->bufcnt,
					 rctx->src_sg, rctx->offset,
					 rctx->total - remain, 0);
		rctx->offset += rctx->total - remain;

	} else {
		dev_warn(rsss_dev->dev, "SHA3 input data length is too large\n");
		return -EINVAL;
	}

	/* Copy remain data into buffer */
	scatterwalk_map_and_copy(sha3_engine->buffer_addr, rctx->src_sg,
				 rctx->offset, remain, 0);
	rctx->bufcnt = remain;

	sha3_engine->src_length = length;
	sha3_engine->src_dma = sha3_engine->ahash_src_dma_addr;

	return 0;
}

/*
 * Prepare DMA buffer as SG list buffer before
 * hardware engine processing.
 */
static int aspeed_sha3_dma_prepare_sg(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;
	struct aspeed_sha3_reqctx *rctx;
	struct aspeed_sg_list *src_list;
	struct scatterlist *s;
	int length, remain, sg_len;
	int i, rc = 0;

	rctx = ahash_request_ctx(req);
	remain = (rctx->total + rctx->bufcnt) % rctx->blksize;
	length = rctx->total + rctx->bufcnt - remain;

	RSSS_DBG(rsss_dev, "%s:0x%x, %s:%zu, %s:0x%x, %s:0x%x\n",
		 "rctx total", rctx->total, "bufcnt", rctx->bufcnt,
		 "length", length, "remain", remain);

	sg_len = dma_map_sg(rsss_dev->dev, rctx->src_sg, rctx->src_nents,
			    DMA_TO_DEVICE);
	/*
	 * Need dma_sync_sg_for_device()?
	 */
	if (!sg_len) {
		dev_warn(rsss_dev->dev, "dma_map_sg() src error\n");
		rc = -ENOMEM;
		goto end;
	}

	src_list = (struct aspeed_sg_list *)sha3_engine->ahash_src_addr;

	if (rctx->bufcnt != 0) {
		u64 phy_addr;
		u32 len;

		phy_addr = sha3_engine->buffer_dma_addr;
		len = rctx->bufcnt;
		length -= len;

		/* Last sg list */
		if (length == 0)
			len |= SG_LAST_LIST;

		src_list[0].phy_addr = cpu_to_le64(phy_addr);
		src_list[0].len = cpu_to_le32(len);

		RSSS_DBG(rsss_dev, "Remain buffer first, addr:%llx, len:0x%x\n",
			 src_list[0].phy_addr, src_list[0].len);

		src_list++;
	}

	if (length != 0) {
		for_each_sg(rctx->src_sg, s, sg_len, i) {
			u64 phy_addr = sg_dma_address(s);
			u32 len = sg_dma_len(s);
			u8 *va = sg_virt(s);

			RSSS_DBG(rsss_dev, "SG[%d] PA:%llx, VA:%llx, len:0x%x\n",
				 i, sg_dma_address(s), (u64)va, len);

			if (length > len) {
				length -= len;
			} else {
				/* Last sg list */
				len = length;
				len |= SG_LAST_LIST;
				length = 0;
			}

			src_list[i].phy_addr = cpu_to_le64(phy_addr);
			src_list[i].len = cpu_to_le32(len);

			len = len & 0xffff;
		}
	}

	if (length != 0) {
		rc = -EINVAL;
		goto free_src_sg;
	}

	rctx->offset = rctx->total - remain;
	sha3_engine->src_length = rctx->total + rctx->bufcnt - remain;
	sha3_engine->src_dma = sha3_engine->ahash_src_dma_addr;

	return 0;

free_src_sg:
	RSSS_DBG(rsss_dev, "dma_unmap_sg()\n");
	dma_unmap_sg(rsss_dev->dev, rctx->src_sg, rctx->src_nents,
		     DMA_TO_DEVICE);
end:
	return rc;
}

static int aspeed_sha3_complete(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;

	RSSS_DBG(rsss_dev, "\n");

	sha3_engine->flags &= ~CRYPTO_FLAGS_BUSY;

	crypto_finalize_hash_request(rsss_dev->crypt_engine_sha3, req, 0);

	return 0;
}

/*
 * Copy digest to the corresponding request result.
 * This function will be called at final() stage.
 */
static int aspeed_sha3_transfer(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;
	struct aspeed_sha3_reqctx *rctx;

	RSSS_DBG(rsss_dev, "\n");

	rctx = ahash_request_ctx(req);

	memcpy(req->result, sha3_engine->digest_addr, rctx->digsize);

	return aspeed_sha3_complete(rsss_dev);
}

#ifdef RSSS_SHA3_POLLING_MODE
static int aspeed_sha3_wait_complete(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	u32 sts;
	int ret;

	ret = readl_poll_timeout(rsss_dev->regs + ASPEED_SHA3_BUSY_STS, sts,
				 ((sts & SHA3_STS) == 0x0),
				 ASPEED_RSSS_POLLING_TIME,
				 ASPEED_RSSS_TIMEOUT * 10);
	if (ret) {
		dev_err(rsss_dev->dev, "SHA3 wrong engine status\n");
		return -EIO;
	}

	ret = readl_poll_timeout(rsss_dev->regs + ASPEED_RSSS_INT_STS, sts,
				 ((sts & SHA3_INT_DONE) == SHA3_INT_DONE),
				 ASPEED_RSSS_POLLING_TIME,
				 ASPEED_RSSS_TIMEOUT);
	if (ret) {
		dev_err(rsss_dev->dev, "SHA3 wrong interrupt status\n");
		return -EIO;
	}

	ast_rsss_write(rsss_dev, sts, ASPEED_RSSS_INT_STS);

	RSSS_DBG(rsss_dev, "irq sts:0x%x\n", sts);

	if (sts & SHA3_INT_DONE) {
		if (sha3_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&sha3_engine->done_task);
		else
			dev_err(rsss_dev->dev, "SHA3 no active requests.\n");
	}

	return 0;
}
#endif

/*
 * Trigger hardware engines to do the math.
 */
static int aspeed_sha3_trigger(struct aspeed_rsss_dev *rsss_dev,
			       aspeed_rsss_fn_t resume)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;
	struct aspeed_sha3_reqctx *rctx;

	RSSS_DBG(rsss_dev, "src_dma:%pad, digest_dma:%pad, length:%zu\n",
		 &sha3_engine->src_dma, &sha3_engine->digest_dma_addr,
		 sha3_engine->src_length);

	rctx = ahash_request_ctx(req);
	sha3_engine->resume = resume;

	ast_rsss_write(rsss_dev, sha3_engine->src_dma,
		       ASPEED_SHA3_SRC_LO);
	/* TODO - SRC_HI */
	ast_rsss_write(rsss_dev, sha3_engine->src_dma >> 32,
		       ASPEED_SHA3_SRC_HI);

	ast_rsss_write(rsss_dev, sha3_engine->digest_dma_addr,
		       ASPEED_SHA3_DST_LO);
	/* TODO - DST_HI */
	ast_rsss_write(rsss_dev, sha3_engine->digest_dma_addr >> 32,
		       ASPEED_SHA3_DST_HI);

	if (!sha3_engine->sg_mode)
		ast_rsss_write(rsss_dev, sha3_engine->src_length,
			       ASPEED_SHA3_SRC_LEN);

	ast_rsss_write(rsss_dev, rctx->cmd, ASPEED_SHA3_CMD);

	/* Memory barrier to ensure all data setup before engine starts */
	mb();

	rctx->cmd |= SHA3_CMD_TRIG;

	RSSS_DBG(rsss_dev, "cmd:0x%x\n", rctx->cmd);

	ast_rsss_write(rsss_dev, rctx->cmd, ASPEED_SHA3_CMD);

#ifdef RSSS_SHA3_POLLING_MODE
	return aspeed_sha3_wait_complete(rsss_dev);
#else
	return -EINPROGRESS;
#endif
}

static int aspeed_sha3_req_final(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;
	struct aspeed_sha3_reqctx *rctx = ahash_request_ctx(req);

	RSSS_DBG(rsss_dev, "\n");

	if (sha3_engine->sg_mode) {
		struct aspeed_sg_list *src_list =
				(struct aspeed_sg_list *)sha3_engine->ahash_src_addr;

		u64 phy_addr;
		u32 len;

		phy_addr = sha3_engine->buffer_dma_addr;
		len = rctx->bufcnt;
		len |= SG_LAST_LIST;

		src_list[0].phy_addr = cpu_to_le64(phy_addr);
		src_list[0].len = cpu_to_le32(len);

		RSSS_DBG(rsss_dev, "Final SG, addr:%llx, len:0x%x\n",
			 src_list[0].phy_addr, src_list[0].len);

		rctx->cmd |= SHA3_CMD_SG_MODE;
		sha3_engine->src_dma = sha3_engine->ahash_src_dma_addr;

	} else {
		sha3_engine->src_dma = sha3_engine->buffer_dma_addr;
		sha3_engine->src_length = rctx->bufcnt;
	}

	rctx->cmd |= SHA3_CMD_ACC_FINAL |
		     SHA3_CMD_HW_PAD;

	return aspeed_sha3_trigger(rsss_dev, aspeed_sha3_transfer);
}

static int aspeed_sha3_update_resume(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;
	struct aspeed_sha3_reqctx *rctx;

	RSSS_DBG(rsss_dev, "\n");

	rctx = ahash_request_ctx(req);

	rctx->cmd &= ~SHA3_CMD_TRIG;

	if (rctx->flags & SHA3_FLAGS_FINUP)
		return aspeed_sha3_req_final(rsss_dev);

	return aspeed_sha3_complete(rsss_dev);
}

static int aspeed_sha3_update_resume_sg(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;
	struct aspeed_sha3_reqctx *rctx;
	int remain;

	RSSS_DBG(rsss_dev, "\n");

	rctx = ahash_request_ctx(req);
	remain = rctx->total - rctx->offset;

	RSSS_DBG(rsss_dev, "Copy remain data from 0x%x, size:0x%x\n",
		 rctx->offset, remain);

	dma_unmap_sg(rsss_dev->dev, rctx->src_sg, rctx->src_nents,
		     DMA_TO_DEVICE);

	scatterwalk_map_and_copy(sha3_engine->buffer_addr, rctx->src_sg, rctx->offset,
				 remain, 0);

	rctx->bufcnt = remain;
	rctx->cmd &= ~(SHA3_CMD_TRIG | SHA3_CMD_SG_MODE);

	if (rctx->flags & SHA3_FLAGS_FINUP)
		return aspeed_sha3_req_final(rsss_dev);

	return aspeed_sha3_complete(rsss_dev);
}

static int aspeed_sha3_req_update(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct ahash_request *req = sha3_engine->req;
	struct aspeed_sha3_reqctx *rctx;
	aspeed_rsss_fn_t resume;
	int ret;

	RSSS_DBG(rsss_dev, "\n");

	rctx = ahash_request_ctx(req);

	if (sha3_engine->sg_mode) {
		rctx->cmd |= SHA3_CMD_SG_MODE;
		resume = aspeed_sha3_update_resume_sg;

	} else {
		resume = aspeed_sha3_update_resume;
	}

	ret = sha3_engine->dma_prepare(rsss_dev);
	if (ret)
		return ret;

	return aspeed_sha3_trigger(rsss_dev, resume);
}

static int aspeed_sha3_do_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct aspeed_sha3_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sha3_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = tctx->rsss_dev;
	struct aspeed_engine_sha3 *sha3_engine;
	int ret = 0;

	RSSS_DBG(rsss_dev, "\n");

	sha3_engine = &rsss_dev->sha3_engine;
	sha3_engine->flags |= CRYPTO_FLAGS_BUSY;

	if (rctx->op == SHA_OP_UPDATE)
		ret = aspeed_sha3_req_update(rsss_dev);
	else if (rctx->op == SHA_OP_FINAL)
		ret = aspeed_sha3_req_final(rsss_dev);

	if (ret != -EINPROGRESS)
		return ret;

	return 0;
}

static int aspeed_sha3_prepare_request(struct crypto_engine *engine,
				       void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sha3_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = tctx->rsss_dev;
	struct aspeed_engine_sha3 *sha3_engine;

	sha3_engine = &rsss_dev->sha3_engine;
	sha3_engine->req = req;

	if (sha3_engine->sg_mode)
		sha3_engine->dma_prepare = aspeed_sha3_dma_prepare_sg;
	else
		sha3_engine->dma_prepare = aspeed_sha3_dma_prepare;

	return 0;
}

static int aspeed_sha3_handle_queue(struct aspeed_rsss_dev *rsss_dev,
				    struct ahash_request *req)
{
	return crypto_transfer_hash_request_to_engine(rsss_dev->crypt_engine_sha3, req);
}

static int aspeed_sha3_update(struct ahash_request *req)
{
	struct aspeed_sha3_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sha3_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = tctx->rsss_dev;
	struct aspeed_engine_sha3 *sha3_engine;

	RSSS_DBG(rsss_dev, "req->nbytes: %d\n", req->nbytes);

	sha3_engine = &rsss_dev->sha3_engine;

	rctx->total = req->nbytes;
	rctx->src_sg = req->src;
	rctx->offset = 0;
	rctx->src_nents = sg_nents(req->src);
	rctx->op = SHA_OP_UPDATE;

	RSSS_DBG(rsss_dev, "total:0x%x, src_nents:0x%x\n", rctx->total, rctx->src_nents);

	rctx->digcnt[0] += rctx->total;
	if (rctx->digcnt[0] < rctx->total)
		rctx->digcnt[1]++;

	if (rctx->bufcnt + rctx->total < rctx->blksize) {
		scatterwalk_map_and_copy(sha3_engine->buffer_addr + rctx->bufcnt,
					 rctx->src_sg, rctx->offset,
					 rctx->total, 0);
		rctx->bufcnt += rctx->total;

		return 0;
	}

	return aspeed_sha3_handle_queue(rsss_dev, req);
}

static int aspeed_sha3_final(struct ahash_request *req)
{
	struct aspeed_sha3_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sha3_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = tctx->rsss_dev;

	RSSS_DBG(rsss_dev, "req->nbytes:%d, rctx->total:%d\n",
		 req->nbytes, rctx->total);
	rctx->op = SHA_OP_FINAL;

	return aspeed_sha3_handle_queue(rsss_dev, req);
}

static int aspeed_sha3_finup(struct ahash_request *req)
{
	struct aspeed_sha3_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sha3_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = tctx->rsss_dev;
	int rc1, rc2;

	RSSS_DBG(rsss_dev, "req->nbytes: %d\n", req->nbytes);

	rctx->flags |= SHA3_FLAGS_FINUP;

	rc1 = aspeed_sha3_update(req);
	if (rc1 == -EINPROGRESS || rc1 == -EBUSY)
		return rc1;

	/*
	 * final() has to be always called to cleanup resources
	 * even if update() failed, except EINPROGRESS
	 */
	rc2 = aspeed_sha3_final(req);

	return rc1 ? : rc2;
}

static int aspeed_sha3_init(struct ahash_request *req)
{
	struct aspeed_sha3_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct aspeed_sha3_ctx *tctx = crypto_ahash_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = tctx->rsss_dev;
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;

	RSSS_DBG(rsss_dev, "%s: digest size:%d\n",
		 crypto_tfm_alg_name(&tfm->base),
		 crypto_ahash_digestsize(tfm));

	rctx->cmd = SHA3_CMD_ACC;
	rctx->flags = 0;

	switch (crypto_ahash_digestsize(tfm)) {
	case SHA3_224_DIGEST_SIZE:
		rctx->cmd |= SHA3_CMD_MODE_224;
		rctx->flags |= SHA3_FLAGS_SHA224;
		rctx->digsize = SHA3_224_DIGEST_SIZE;
		rctx->blksize = SHA3_224_BLOCK_SIZE;
		break;
	case SHA3_256_DIGEST_SIZE:
		rctx->cmd |= SHA3_CMD_MODE_256;
		rctx->flags |= SHA3_FLAGS_SHA256;
		rctx->digsize = SHA3_256_DIGEST_SIZE;
		rctx->blksize = SHA3_256_BLOCK_SIZE;
		break;
	case SHA3_384_DIGEST_SIZE:
		rctx->cmd |= SHA3_CMD_MODE_384;
		rctx->flags |= SHA3_FLAGS_SHA384;
		rctx->digsize = SHA3_384_DIGEST_SIZE;
		rctx->blksize = SHA3_384_BLOCK_SIZE;
		break;
	case SHA3_512_DIGEST_SIZE:
		rctx->cmd |= SHA3_CMD_MODE_512;
		rctx->flags |= SHA3_FLAGS_SHA512;
		rctx->digsize = SHA3_512_DIGEST_SIZE;
		rctx->blksize = SHA3_512_BLOCK_SIZE;
		break;
	default:
		dev_warn(tctx->rsss_dev->dev, "digest size %d not support\n",
			 crypto_ahash_digestsize(tfm));
		return -EINVAL;
	}

	rctx->bufcnt = 0;
	rctx->total = 0;
	rctx->digcnt[0] = 0;
	rctx->digcnt[1] = 0;

	memset(sha3_engine->digest_addr, 0x0, SHA3_512_DIGEST_SIZE);

	return 0;
}

static int aspeed_sha3_digest(struct ahash_request *req)
{
	return aspeed_sha3_init(req) ? : aspeed_sha3_finup(req);
}

static int aspeed_sha3_export(struct ahash_request *req, void *out)
{
	struct aspeed_sha3_reqctx *rctx = ahash_request_ctx(req);

	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int aspeed_sha3_import(struct ahash_request *req, const void *in)
{
	struct aspeed_sha3_reqctx *rctx = ahash_request_ctx(req);

	memcpy(rctx, in, sizeof(*rctx));

	return 0;
}

static int aspeed_sha3_cra_init(struct crypto_tfm *tfm)
{
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	struct aspeed_sha3_ctx *tctx = crypto_tfm_ctx(tfm);
	struct aspeed_rsss_alg *ast_alg;

	ast_alg = container_of(alg, struct aspeed_rsss_alg, alg.ahash);
	tctx->rsss_dev = ast_alg->rsss_dev;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct aspeed_sha3_reqctx));

	tctx->enginectx.op.do_one_request = aspeed_sha3_do_request;
	tctx->enginectx.op.prepare_request = aspeed_sha3_prepare_request;
	tctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static void aspeed_sha3_cra_exit(struct crypto_tfm *tfm)
{
	struct aspeed_sha3_ctx *tctx = crypto_tfm_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = tctx->rsss_dev;

	RSSS_DBG(rsss_dev, "%s\n", crypto_tfm_alg_name(tfm));
}

struct aspeed_rsss_alg aspeed_rsss_algs_sha3_224 = {
	.type = ASPEED_ALGO_TYPE_AHASH,
	.alg.ahash = {
		.init	= aspeed_sha3_init,
		.update	= aspeed_sha3_update,
		.final	= aspeed_sha3_final,
		.finup	= aspeed_sha3_finup,
		.digest	= aspeed_sha3_digest,
		.export	= aspeed_sha3_export,
		.import	= aspeed_sha3_import,
		.halg = {
			.digestsize = SHA3_224_DIGEST_SIZE,
			.statesize = sizeof(struct aspeed_sha3_reqctx),
			.base = {
				.cra_name		= "sha3-224",
				.cra_driver_name	= "aspeed-sha3-224",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize		= SHA3_224_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_sha3_ctx),
				.cra_module		= THIS_MODULE,
				.cra_init		= aspeed_sha3_cra_init,
				.cra_exit		= aspeed_sha3_cra_exit,
			}
		}
	},
};

struct aspeed_rsss_alg aspeed_rsss_algs_sha3_256 = {
	.type = ASPEED_ALGO_TYPE_AHASH,
	.alg.ahash = {
		.init	= aspeed_sha3_init,
		.update	= aspeed_sha3_update,
		.final	= aspeed_sha3_final,
		.finup	= aspeed_sha3_finup,
		.digest	= aspeed_sha3_digest,
		.export	= aspeed_sha3_export,
		.import	= aspeed_sha3_import,
		.halg = {
			.digestsize = SHA3_256_DIGEST_SIZE,
			.statesize = sizeof(struct aspeed_sha3_reqctx),
			.base = {
				.cra_name		= "sha3-256",
				.cra_driver_name	= "aspeed-sha3-256",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize		= SHA3_256_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_sha3_ctx),
				.cra_module		= THIS_MODULE,
				.cra_init		= aspeed_sha3_cra_init,
				.cra_exit		= aspeed_sha3_cra_exit,
			}
		}
	},
};

struct aspeed_rsss_alg aspeed_rsss_algs_sha3_384 = {
	.type = ASPEED_ALGO_TYPE_AHASH,
	.alg.ahash = {
		.init	= aspeed_sha3_init,
		.update	= aspeed_sha3_update,
		.final	= aspeed_sha3_final,
		.finup	= aspeed_sha3_finup,
		.digest	= aspeed_sha3_digest,
		.export	= aspeed_sha3_export,
		.import	= aspeed_sha3_import,
		.halg = {
			.digestsize = SHA3_384_DIGEST_SIZE,
			.statesize = sizeof(struct aspeed_sha3_reqctx),
			.base = {
				.cra_name		= "sha3-384",
				.cra_driver_name	= "aspeed-sha3-384",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize		= SHA3_384_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_sha3_ctx),
				.cra_module		= THIS_MODULE,
				.cra_init		= aspeed_sha3_cra_init,
				.cra_exit		= aspeed_sha3_cra_exit,
			}
		}
	},
};

struct aspeed_rsss_alg aspeed_rsss_algs_sha3_512 = {
	.type = ASPEED_ALGO_TYPE_AHASH,
	.alg.ahash = {
		.init	= aspeed_sha3_init,
		.update	= aspeed_sha3_update,
		.final	= aspeed_sha3_final,
		.finup	= aspeed_sha3_finup,
		.digest	= aspeed_sha3_digest,
		.export	= aspeed_sha3_export,
		.import	= aspeed_sha3_import,
		.halg = {
			.digestsize = SHA3_512_DIGEST_SIZE,
			.statesize = sizeof(struct aspeed_sha3_reqctx),
			.base = {
				.cra_name		= "sha3-512",
				.cra_driver_name	= "aspeed-sha3-512",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_TYPE_AHASH |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_blocksize		= SHA3_512_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_sha3_ctx),
				.cra_module		= THIS_MODULE,
				.cra_init		= aspeed_sha3_cra_init,
				.cra_exit		= aspeed_sha3_cra_exit,
			}
		}
	},
};

static void aspeed_rsss_sha3_done_task(unsigned long data)
{
	struct aspeed_rsss_dev *rsss_dev = (struct aspeed_rsss_dev *)data;
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;

	(void)sha3_engine->resume(rsss_dev);
}

void aspeed_rsss_sha3_exit(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;

	crypto_engine_exit(rsss_dev->crypt_engine_sha3);
	tasklet_kill(&sha3_engine->done_task);
}

int aspeed_rsss_sha3_init(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_sha3 *sha3_engine;
	u32 val;
	int rc;

	rc = reset_control_deassert(rsss_dev->reset_sha3);
	if (rc) {
		dev_err(rsss_dev->dev, "Deassert SHA3 reset failed\n");
		goto end;
	}

	sha3_engine = &rsss_dev->sha3_engine;

	/* Initialize crypto hardware engine structure for SHA3 */
	rsss_dev->crypt_engine_sha3 = crypto_engine_alloc_init(rsss_dev->dev, true);
	if (!rsss_dev->crypt_engine_sha3) {
		rc = -ENOMEM;
		goto end;
	}

	rc = crypto_engine_start(rsss_dev->crypt_engine_sha3);
	if (rc)
		goto err_engine_sha3_start;

	tasklet_init(&sha3_engine->done_task, aspeed_rsss_sha3_done_task,
		     (unsigned long)rsss_dev);

	/* Allocate DMA buffer for hash engine input used */
	sha3_engine->ahash_src_addr =
		dmam_alloc_coherent(rsss_dev->dev,
				    ASPEED_HASH_SRC_DMA_BUF_LEN,
				    &sha3_engine->ahash_src_dma_addr,
				    GFP_KERNEL);
	if (!sha3_engine->ahash_src_addr) {
		dev_err(rsss_dev->dev, "Failed to allocate DMA src buffer\n");
		rc = -ENOMEM;
		goto err_engine_sha3_start;
	}

	sha3_engine->buffer_addr = dmam_alloc_coherent(rsss_dev->dev, SHA3_224_BLOCK_SIZE,
						       &sha3_engine->buffer_dma_addr,
						       GFP_KERNEL);
	if (!sha3_engine->buffer_addr) {
		dev_err(rsss_dev->dev, "Failed to allocate DMA buffer\n");
		rc = -ENOMEM;
		goto err_engine_sha3_start;
	}

	sha3_engine->digest_addr = dmam_alloc_coherent(rsss_dev->dev, SHA3_512_DIGEST_SIZE,
						       &sha3_engine->digest_dma_addr,
						       GFP_KERNEL);
	if (!sha3_engine->digest_addr) {
		dev_err(rsss_dev->dev, "Failed to allocate DMA digest buffer\n");
		rc = -ENOMEM;
		goto err_engine_sha3_start;
	}

	/*
	 * Set 1 to use scatter-gather mode.
	 * Set 0 to use direct mode.
	 */
	sha3_engine->sg_mode = 0;

	/* Enable SHA3 interrupt */
	val = ast_rsss_read(rsss_dev, ASPEED_RSSS_INT_EN);
	ast_rsss_write(rsss_dev, val | SHA3_INT_EN, ASPEED_RSSS_INT_EN);
	dev_info(rsss_dev->dev, "Aspeed RSSS SHA3 interrupt mode.\n");

	dev_info(rsss_dev->dev, "Aspeed RSSS SHA3 initialized (%s mode)\n",
		 sha3_engine->sg_mode ? "SG" : "Direct");

	return 0;

err_engine_sha3_start:
	crypto_engine_exit(rsss_dev->crypt_engine_sha3);
end:
	return rc;
}
