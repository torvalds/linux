/*
 * Cipher algorithms supported by the CESA: DES, 3DES and AES.
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 * Author: Arnaud Ebalard <arno@natisbad.org>
 *
 * This work is based on an initial version written by
 * Sebastian Andrzej Siewior < sebastian at breakpoint dot cc >
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <crypto/aes.h>
#include <crypto/des.h>

#include "cesa.h"

struct mv_cesa_des_ctx {
	struct mv_cesa_ctx base;
	u8 key[DES_KEY_SIZE];
};

struct mv_cesa_des3_ctx {
	struct mv_cesa_ctx base;
	u8 key[DES3_EDE_KEY_SIZE];
};

struct mv_cesa_aes_ctx {
	struct mv_cesa_ctx base;
	struct crypto_aes_ctx aes;
};

struct mv_cesa_skcipher_dma_iter {
	struct mv_cesa_dma_iter base;
	struct mv_cesa_sg_dma_iter src;
	struct mv_cesa_sg_dma_iter dst;
};

static inline void
mv_cesa_skcipher_req_iter_init(struct mv_cesa_skcipher_dma_iter *iter,
			       struct skcipher_request *req)
{
	mv_cesa_req_dma_iter_init(&iter->base, req->cryptlen);
	mv_cesa_sg_dma_iter_init(&iter->src, req->src, DMA_TO_DEVICE);
	mv_cesa_sg_dma_iter_init(&iter->dst, req->dst, DMA_FROM_DEVICE);
}

static inline bool
mv_cesa_skcipher_req_iter_next_op(struct mv_cesa_skcipher_dma_iter *iter)
{
	iter->src.op_offset = 0;
	iter->dst.op_offset = 0;

	return mv_cesa_req_dma_iter_next_op(&iter->base);
}

static inline void
mv_cesa_skcipher_dma_cleanup(struct skcipher_request *req)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);

	if (req->dst != req->src) {
		dma_unmap_sg(cesa_dev->dev, req->dst, creq->dst_nents,
			     DMA_FROM_DEVICE);
		dma_unmap_sg(cesa_dev->dev, req->src, creq->src_nents,
			     DMA_TO_DEVICE);
	} else {
		dma_unmap_sg(cesa_dev->dev, req->src, creq->src_nents,
			     DMA_BIDIRECTIONAL);
	}
	mv_cesa_dma_cleanup(&creq->base);
}

static inline void mv_cesa_skcipher_cleanup(struct skcipher_request *req)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ)
		mv_cesa_skcipher_dma_cleanup(req);
}

static void mv_cesa_skcipher_std_step(struct skcipher_request *req)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);
	struct mv_cesa_skcipher_std_req *sreq = &creq->std;
	struct mv_cesa_engine *engine = creq->base.engine;
	size_t  len = min_t(size_t, req->cryptlen - sreq->offset,
			    CESA_SA_SRAM_PAYLOAD_SIZE);

	mv_cesa_adjust_op(engine, &sreq->op);
	memcpy_toio(engine->sram, &sreq->op, sizeof(sreq->op));

	len = sg_pcopy_to_buffer(req->src, creq->src_nents,
				 engine->sram + CESA_SA_DATA_SRAM_OFFSET,
				 len, sreq->offset);

	sreq->size = len;
	mv_cesa_set_crypt_op_len(&sreq->op, len);

	/* FIXME: only update enc_len field */
	if (!sreq->skip_ctx) {
		memcpy_toio(engine->sram, &sreq->op, sizeof(sreq->op));
		sreq->skip_ctx = true;
	} else {
		memcpy_toio(engine->sram, &sreq->op, sizeof(sreq->op.desc));
	}

	mv_cesa_set_int_mask(engine, CESA_SA_INT_ACCEL0_DONE);
	writel_relaxed(CESA_SA_CFG_PARA_DIS, engine->regs + CESA_SA_CFG);
	BUG_ON(readl(engine->regs + CESA_SA_CMD) &
	       CESA_SA_CMD_EN_CESA_SA_ACCL0);
	writel(CESA_SA_CMD_EN_CESA_SA_ACCL0, engine->regs + CESA_SA_CMD);
}

static int mv_cesa_skcipher_std_process(struct skcipher_request *req,
					u32 status)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);
	struct mv_cesa_skcipher_std_req *sreq = &creq->std;
	struct mv_cesa_engine *engine = creq->base.engine;
	size_t len;

	len = sg_pcopy_from_buffer(req->dst, creq->dst_nents,
				   engine->sram + CESA_SA_DATA_SRAM_OFFSET,
				   sreq->size, sreq->offset);

	sreq->offset += len;
	if (sreq->offset < req->cryptlen)
		return -EINPROGRESS;

	return 0;
}

static int mv_cesa_skcipher_process(struct crypto_async_request *req,
				    u32 status)
{
	struct skcipher_request *skreq = skcipher_request_cast(req);
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(skreq);
	struct mv_cesa_req *basereq = &creq->base;

	if (mv_cesa_req_get_type(basereq) == CESA_STD_REQ)
		return mv_cesa_skcipher_std_process(skreq, status);

	return mv_cesa_dma_process(basereq, status);
}

static void mv_cesa_skcipher_step(struct crypto_async_request *req)
{
	struct skcipher_request *skreq = skcipher_request_cast(req);
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(skreq);

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ)
		mv_cesa_dma_step(&creq->base);
	else
		mv_cesa_skcipher_std_step(skreq);
}

static inline void
mv_cesa_skcipher_dma_prepare(struct skcipher_request *req)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);
	struct mv_cesa_req *basereq = &creq->base;

	mv_cesa_dma_prepare(basereq, basereq->engine);
}

static inline void
mv_cesa_skcipher_std_prepare(struct skcipher_request *req)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);
	struct mv_cesa_skcipher_std_req *sreq = &creq->std;

	sreq->size = 0;
	sreq->offset = 0;
}

static inline void mv_cesa_skcipher_prepare(struct crypto_async_request *req,
					    struct mv_cesa_engine *engine)
{
	struct skcipher_request *skreq = skcipher_request_cast(req);
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(skreq);
	creq->base.engine = engine;

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ)
		mv_cesa_skcipher_dma_prepare(skreq);
	else
		mv_cesa_skcipher_std_prepare(skreq);
}

static inline void
mv_cesa_skcipher_req_cleanup(struct crypto_async_request *req)
{
	struct skcipher_request *skreq = skcipher_request_cast(req);

	mv_cesa_skcipher_cleanup(skreq);
}

static void
mv_cesa_skcipher_complete(struct crypto_async_request *req)
{
	struct skcipher_request *skreq = skcipher_request_cast(req);
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(skreq);
	struct mv_cesa_engine *engine = creq->base.engine;
	unsigned int ivsize;

	atomic_sub(skreq->cryptlen, &engine->load);
	ivsize = crypto_skcipher_ivsize(crypto_skcipher_reqtfm(skreq));

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ) {
		struct mv_cesa_req *basereq;

		basereq = &creq->base;
		memcpy(skreq->iv, basereq->chain.last->op->ctx.blkcipher.iv,
		       ivsize);
	} else {
		memcpy_fromio(skreq->iv,
			      engine->sram + CESA_SA_CRYPT_IV_SRAM_OFFSET,
			      ivsize);
	}
}

static const struct mv_cesa_req_ops mv_cesa_skcipher_req_ops = {
	.step = mv_cesa_skcipher_step,
	.process = mv_cesa_skcipher_process,
	.cleanup = mv_cesa_skcipher_req_cleanup,
	.complete = mv_cesa_skcipher_complete,
};

static void mv_cesa_skcipher_cra_exit(struct crypto_tfm *tfm)
{
	void *ctx = crypto_tfm_ctx(tfm);

	memzero_explicit(ctx, tfm->__crt_alg->cra_ctxsize);
}

static int mv_cesa_skcipher_cra_init(struct crypto_tfm *tfm)
{
	struct mv_cesa_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->ops = &mv_cesa_skcipher_req_ops;

	crypto_skcipher_set_reqsize(__crypto_skcipher_cast(tfm),
				    sizeof(struct mv_cesa_skcipher_req));

	return 0;
}

static int mv_cesa_aes_setkey(struct crypto_skcipher *cipher, const u8 *key,
			      unsigned int len)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct mv_cesa_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	int remaining;
	int offset;
	int ret;
	int i;

	ret = crypto_aes_expand_key(&ctx->aes, key, len);
	if (ret) {
		crypto_skcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return ret;
	}

	remaining = (ctx->aes.key_length - 16) / 4;
	offset = ctx->aes.key_length + 24 - remaining;
	for (i = 0; i < remaining; i++)
		ctx->aes.key_dec[4 + i] =
			cpu_to_le32(ctx->aes.key_enc[offset + i]);

	return 0;
}

static int mv_cesa_des_setkey(struct crypto_skcipher *cipher, const u8 *key,
			      unsigned int len)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct mv_cesa_des_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 tmp[DES_EXPKEY_WORDS];
	int ret;

	if (len != DES_KEY_SIZE) {
		crypto_skcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ret = des_ekey(tmp, key);
	if (!ret && (tfm->crt_flags & CRYPTO_TFM_REQ_FORBID_WEAK_KEYS)) {
		tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	memcpy(ctx->key, key, DES_KEY_SIZE);

	return 0;
}

static int mv_cesa_des3_ede_setkey(struct crypto_skcipher *cipher,
				   const u8 *key, unsigned int len)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct mv_cesa_des_ctx *ctx = crypto_tfm_ctx(tfm);

	if (len != DES3_EDE_KEY_SIZE) {
		crypto_skcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key, key, DES3_EDE_KEY_SIZE);

	return 0;
}

static int mv_cesa_skcipher_dma_req_init(struct skcipher_request *req,
					 const struct mv_cesa_op_ctx *op_templ)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	struct mv_cesa_req *basereq = &creq->base;
	struct mv_cesa_skcipher_dma_iter iter;
	bool skip_ctx = false;
	int ret;

	basereq->chain.first = NULL;
	basereq->chain.last = NULL;

	if (req->src != req->dst) {
		ret = dma_map_sg(cesa_dev->dev, req->src, creq->src_nents,
				 DMA_TO_DEVICE);
		if (!ret)
			return -ENOMEM;

		ret = dma_map_sg(cesa_dev->dev, req->dst, creq->dst_nents,
				 DMA_FROM_DEVICE);
		if (!ret) {
			ret = -ENOMEM;
			goto err_unmap_src;
		}
	} else {
		ret = dma_map_sg(cesa_dev->dev, req->src, creq->src_nents,
				 DMA_BIDIRECTIONAL);
		if (!ret)
			return -ENOMEM;
	}

	mv_cesa_tdma_desc_iter_init(&basereq->chain);
	mv_cesa_skcipher_req_iter_init(&iter, req);

	do {
		struct mv_cesa_op_ctx *op;

		op = mv_cesa_dma_add_op(&basereq->chain, op_templ, skip_ctx, flags);
		if (IS_ERR(op)) {
			ret = PTR_ERR(op);
			goto err_free_tdma;
		}
		skip_ctx = true;

		mv_cesa_set_crypt_op_len(op, iter.base.op_len);

		/* Add input transfers */
		ret = mv_cesa_dma_add_op_transfers(&basereq->chain, &iter.base,
						   &iter.src, flags);
		if (ret)
			goto err_free_tdma;

		/* Add dummy desc to launch the crypto operation */
		ret = mv_cesa_dma_add_dummy_launch(&basereq->chain, flags);
		if (ret)
			goto err_free_tdma;

		/* Add output transfers */
		ret = mv_cesa_dma_add_op_transfers(&basereq->chain, &iter.base,
						   &iter.dst, flags);
		if (ret)
			goto err_free_tdma;

	} while (mv_cesa_skcipher_req_iter_next_op(&iter));

	/* Add output data for IV */
	ret = mv_cesa_dma_add_result_op(&basereq->chain, CESA_SA_CFG_SRAM_OFFSET,
				    CESA_SA_DATA_SRAM_OFFSET,
				    CESA_TDMA_SRC_IN_SRAM, flags);

	if (ret)
		goto err_free_tdma;

	basereq->chain.last->flags |= CESA_TDMA_END_OF_REQ;

	return 0;

err_free_tdma:
	mv_cesa_dma_cleanup(basereq);
	if (req->dst != req->src)
		dma_unmap_sg(cesa_dev->dev, req->dst, creq->dst_nents,
			     DMA_FROM_DEVICE);

err_unmap_src:
	dma_unmap_sg(cesa_dev->dev, req->src, creq->src_nents,
		     req->dst != req->src ? DMA_TO_DEVICE : DMA_BIDIRECTIONAL);

	return ret;
}

static inline int
mv_cesa_skcipher_std_req_init(struct skcipher_request *req,
			      const struct mv_cesa_op_ctx *op_templ)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);
	struct mv_cesa_skcipher_std_req *sreq = &creq->std;
	struct mv_cesa_req *basereq = &creq->base;

	sreq->op = *op_templ;
	sreq->skip_ctx = false;
	basereq->chain.first = NULL;
	basereq->chain.last = NULL;

	return 0;
}

static int mv_cesa_skcipher_req_init(struct skcipher_request *req,
				     struct mv_cesa_op_ctx *tmpl)
{
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	unsigned int blksize = crypto_skcipher_blocksize(tfm);
	int ret;

	if (!IS_ALIGNED(req->cryptlen, blksize))
		return -EINVAL;

	creq->src_nents = sg_nents_for_len(req->src, req->cryptlen);
	if (creq->src_nents < 0) {
		dev_err(cesa_dev->dev, "Invalid number of src SG");
		return creq->src_nents;
	}
	creq->dst_nents = sg_nents_for_len(req->dst, req->cryptlen);
	if (creq->dst_nents < 0) {
		dev_err(cesa_dev->dev, "Invalid number of dst SG");
		return creq->dst_nents;
	}

	mv_cesa_update_op_cfg(tmpl, CESA_SA_DESC_CFG_OP_CRYPT_ONLY,
			      CESA_SA_DESC_CFG_OP_MSK);

	if (cesa_dev->caps->has_tdma)
		ret = mv_cesa_skcipher_dma_req_init(req, tmpl);
	else
		ret = mv_cesa_skcipher_std_req_init(req, tmpl);

	return ret;
}

static int mv_cesa_skcipher_queue_req(struct skcipher_request *req,
				      struct mv_cesa_op_ctx *tmpl)
{
	int ret;
	struct mv_cesa_skcipher_req *creq = skcipher_request_ctx(req);
	struct mv_cesa_engine *engine;

	ret = mv_cesa_skcipher_req_init(req, tmpl);
	if (ret)
		return ret;

	engine = mv_cesa_select_engine(req->cryptlen);
	mv_cesa_skcipher_prepare(&req->base, engine);

	ret = mv_cesa_queue_req(&req->base, &creq->base);

	if (mv_cesa_req_needs_cleanup(&req->base, ret))
		mv_cesa_skcipher_cleanup(req);

	return ret;
}

static int mv_cesa_des_op(struct skcipher_request *req,
			  struct mv_cesa_op_ctx *tmpl)
{
	struct mv_cesa_des_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	mv_cesa_update_op_cfg(tmpl, CESA_SA_DESC_CFG_CRYPTM_DES,
			      CESA_SA_DESC_CFG_CRYPTM_MSK);

	memcpy(tmpl->ctx.blkcipher.key, ctx->key, DES_KEY_SIZE);

	return mv_cesa_skcipher_queue_req(req, tmpl);
}

static int mv_cesa_ecb_des_encrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_ECB |
			   CESA_SA_DESC_CFG_DIR_ENC);

	return mv_cesa_des_op(req, &tmpl);
}

static int mv_cesa_ecb_des_decrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_ECB |
			   CESA_SA_DESC_CFG_DIR_DEC);

	return mv_cesa_des_op(req, &tmpl);
}

struct skcipher_alg mv_cesa_ecb_des_alg = {
	.setkey = mv_cesa_des_setkey,
	.encrypt = mv_cesa_ecb_des_encrypt,
	.decrypt = mv_cesa_ecb_des_decrypt,
	.min_keysize = DES_KEY_SIZE,
	.max_keysize = DES_KEY_SIZE,
	.base = {
		.cra_name = "ecb(des)",
		.cra_driver_name = "mv-ecb-des",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
		.cra_blocksize = DES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mv_cesa_des_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = mv_cesa_skcipher_cra_init,
		.cra_exit = mv_cesa_skcipher_cra_exit,
	},
};

static int mv_cesa_cbc_des_op(struct skcipher_request *req,
			      struct mv_cesa_op_ctx *tmpl)
{
	mv_cesa_update_op_cfg(tmpl, CESA_SA_DESC_CFG_CRYPTCM_CBC,
			      CESA_SA_DESC_CFG_CRYPTCM_MSK);

	memcpy(tmpl->ctx.blkcipher.iv, req->iv, DES_BLOCK_SIZE);

	return mv_cesa_des_op(req, tmpl);
}

static int mv_cesa_cbc_des_encrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_DIR_ENC);

	return mv_cesa_cbc_des_op(req, &tmpl);
}

static int mv_cesa_cbc_des_decrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_DIR_DEC);

	return mv_cesa_cbc_des_op(req, &tmpl);
}

struct skcipher_alg mv_cesa_cbc_des_alg = {
	.setkey = mv_cesa_des_setkey,
	.encrypt = mv_cesa_cbc_des_encrypt,
	.decrypt = mv_cesa_cbc_des_decrypt,
	.min_keysize = DES_KEY_SIZE,
	.max_keysize = DES_KEY_SIZE,
	.ivsize = DES_BLOCK_SIZE,
	.base = {
		.cra_name = "cbc(des)",
		.cra_driver_name = "mv-cbc-des",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
		.cra_blocksize = DES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mv_cesa_des_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = mv_cesa_skcipher_cra_init,
		.cra_exit = mv_cesa_skcipher_cra_exit,
	},
};

static int mv_cesa_des3_op(struct skcipher_request *req,
			   struct mv_cesa_op_ctx *tmpl)
{
	struct mv_cesa_des3_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	mv_cesa_update_op_cfg(tmpl, CESA_SA_DESC_CFG_CRYPTM_3DES,
			      CESA_SA_DESC_CFG_CRYPTM_MSK);

	memcpy(tmpl->ctx.blkcipher.key, ctx->key, DES3_EDE_KEY_SIZE);

	return mv_cesa_skcipher_queue_req(req, tmpl);
}

static int mv_cesa_ecb_des3_ede_encrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_ECB |
			   CESA_SA_DESC_CFG_3DES_EDE |
			   CESA_SA_DESC_CFG_DIR_ENC);

	return mv_cesa_des3_op(req, &tmpl);
}

static int mv_cesa_ecb_des3_ede_decrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_ECB |
			   CESA_SA_DESC_CFG_3DES_EDE |
			   CESA_SA_DESC_CFG_DIR_DEC);

	return mv_cesa_des3_op(req, &tmpl);
}

struct skcipher_alg mv_cesa_ecb_des3_ede_alg = {
	.setkey = mv_cesa_des3_ede_setkey,
	.encrypt = mv_cesa_ecb_des3_ede_encrypt,
	.decrypt = mv_cesa_ecb_des3_ede_decrypt,
	.min_keysize = DES3_EDE_KEY_SIZE,
	.max_keysize = DES3_EDE_KEY_SIZE,
	.ivsize = DES3_EDE_BLOCK_SIZE,
	.base = {
		.cra_name = "ecb(des3_ede)",
		.cra_driver_name = "mv-ecb-des3-ede",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
		.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mv_cesa_des3_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = mv_cesa_skcipher_cra_init,
		.cra_exit = mv_cesa_skcipher_cra_exit,
	},
};

static int mv_cesa_cbc_des3_op(struct skcipher_request *req,
			       struct mv_cesa_op_ctx *tmpl)
{
	memcpy(tmpl->ctx.blkcipher.iv, req->iv, DES3_EDE_BLOCK_SIZE);

	return mv_cesa_des3_op(req, tmpl);
}

static int mv_cesa_cbc_des3_ede_encrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_CBC |
			   CESA_SA_DESC_CFG_3DES_EDE |
			   CESA_SA_DESC_CFG_DIR_ENC);

	return mv_cesa_cbc_des3_op(req, &tmpl);
}

static int mv_cesa_cbc_des3_ede_decrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_CBC |
			   CESA_SA_DESC_CFG_3DES_EDE |
			   CESA_SA_DESC_CFG_DIR_DEC);

	return mv_cesa_cbc_des3_op(req, &tmpl);
}

struct skcipher_alg mv_cesa_cbc_des3_ede_alg = {
	.setkey = mv_cesa_des3_ede_setkey,
	.encrypt = mv_cesa_cbc_des3_ede_encrypt,
	.decrypt = mv_cesa_cbc_des3_ede_decrypt,
	.min_keysize = DES3_EDE_KEY_SIZE,
	.max_keysize = DES3_EDE_KEY_SIZE,
	.ivsize = DES3_EDE_BLOCK_SIZE,
	.base = {
		.cra_name = "cbc(des3_ede)",
		.cra_driver_name = "mv-cbc-des3-ede",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
		.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mv_cesa_des3_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = mv_cesa_skcipher_cra_init,
		.cra_exit = mv_cesa_skcipher_cra_exit,
	},
};

static int mv_cesa_aes_op(struct skcipher_request *req,
			  struct mv_cesa_op_ctx *tmpl)
{
	struct mv_cesa_aes_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	int i;
	u32 *key;
	u32 cfg;

	cfg = CESA_SA_DESC_CFG_CRYPTM_AES;

	if (mv_cesa_get_op_cfg(tmpl) & CESA_SA_DESC_CFG_DIR_DEC)
		key = ctx->aes.key_dec;
	else
		key = ctx->aes.key_enc;

	for (i = 0; i < ctx->aes.key_length / sizeof(u32); i++)
		tmpl->ctx.blkcipher.key[i] = cpu_to_le32(key[i]);

	if (ctx->aes.key_length == 24)
		cfg |= CESA_SA_DESC_CFG_AES_LEN_192;
	else if (ctx->aes.key_length == 32)
		cfg |= CESA_SA_DESC_CFG_AES_LEN_256;

	mv_cesa_update_op_cfg(tmpl, cfg,
			      CESA_SA_DESC_CFG_CRYPTM_MSK |
			      CESA_SA_DESC_CFG_AES_LEN_MSK);

	return mv_cesa_skcipher_queue_req(req, tmpl);
}

static int mv_cesa_ecb_aes_encrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_ECB |
			   CESA_SA_DESC_CFG_DIR_ENC);

	return mv_cesa_aes_op(req, &tmpl);
}

static int mv_cesa_ecb_aes_decrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_ECB |
			   CESA_SA_DESC_CFG_DIR_DEC);

	return mv_cesa_aes_op(req, &tmpl);
}

struct skcipher_alg mv_cesa_ecb_aes_alg = {
	.setkey = mv_cesa_aes_setkey,
	.encrypt = mv_cesa_ecb_aes_encrypt,
	.decrypt = mv_cesa_ecb_aes_decrypt,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.base = {
		.cra_name = "ecb(aes)",
		.cra_driver_name = "mv-ecb-aes",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mv_cesa_aes_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = mv_cesa_skcipher_cra_init,
		.cra_exit = mv_cesa_skcipher_cra_exit,
	},
};

static int mv_cesa_cbc_aes_op(struct skcipher_request *req,
			      struct mv_cesa_op_ctx *tmpl)
{
	mv_cesa_update_op_cfg(tmpl, CESA_SA_DESC_CFG_CRYPTCM_CBC,
			      CESA_SA_DESC_CFG_CRYPTCM_MSK);
	memcpy(tmpl->ctx.blkcipher.iv, req->iv, AES_BLOCK_SIZE);

	return mv_cesa_aes_op(req, tmpl);
}

static int mv_cesa_cbc_aes_encrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_DIR_ENC);

	return mv_cesa_cbc_aes_op(req, &tmpl);
}

static int mv_cesa_cbc_aes_decrypt(struct skcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_DIR_DEC);

	return mv_cesa_cbc_aes_op(req, &tmpl);
}

struct skcipher_alg mv_cesa_cbc_aes_alg = {
	.setkey = mv_cesa_aes_setkey,
	.encrypt = mv_cesa_cbc_aes_encrypt,
	.decrypt = mv_cesa_cbc_aes_decrypt,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.base = {
		.cra_name = "cbc(aes)",
		.cra_driver_name = "mv-cbc-aes",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mv_cesa_aes_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = mv_cesa_skcipher_cra_init,
		.cra_exit = mv_cesa_skcipher_cra_exit,
	},
};
