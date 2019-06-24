// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/internal/skcipher.h>

#include "cipher.h"

static LIST_HEAD(ablkcipher_algs);

static void qce_ablkcipher_done(void *data)
{
	struct crypto_async_request *async_req = data;
	struct ablkcipher_request *req = ablkcipher_request_cast(async_req);
	struct qce_cipher_reqctx *rctx = ablkcipher_request_ctx(req);
	struct qce_alg_template *tmpl = to_cipher_tmpl(async_req->tfm);
	struct qce_device *qce = tmpl->qce;
	enum dma_data_direction dir_src, dir_dst;
	u32 status;
	int error;
	bool diff_dst;

	diff_dst = (req->src != req->dst) ? true : false;
	dir_src = diff_dst ? DMA_TO_DEVICE : DMA_BIDIRECTIONAL;
	dir_dst = diff_dst ? DMA_FROM_DEVICE : DMA_BIDIRECTIONAL;

	error = qce_dma_terminate_all(&qce->dma);
	if (error)
		dev_dbg(qce->dev, "ablkcipher dma termination error (%d)\n",
			error);

	if (diff_dst)
		dma_unmap_sg(qce->dev, rctx->src_sg, rctx->src_nents, dir_src);
	dma_unmap_sg(qce->dev, rctx->dst_sg, rctx->dst_nents, dir_dst);

	sg_free_table(&rctx->dst_tbl);

	error = qce_check_status(qce, &status);
	if (error < 0)
		dev_dbg(qce->dev, "ablkcipher operation error (%x)\n", status);

	qce->async_req_done(tmpl->qce, error);
}

static int
qce_ablkcipher_async_req_handle(struct crypto_async_request *async_req)
{
	struct ablkcipher_request *req = ablkcipher_request_cast(async_req);
	struct qce_cipher_reqctx *rctx = ablkcipher_request_ctx(req);
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct qce_alg_template *tmpl = to_cipher_tmpl(async_req->tfm);
	struct qce_device *qce = tmpl->qce;
	enum dma_data_direction dir_src, dir_dst;
	struct scatterlist *sg;
	bool diff_dst;
	gfp_t gfp;
	int ret;

	rctx->iv = req->info;
	rctx->ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	rctx->cryptlen = req->nbytes;

	diff_dst = (req->src != req->dst) ? true : false;
	dir_src = diff_dst ? DMA_TO_DEVICE : DMA_BIDIRECTIONAL;
	dir_dst = diff_dst ? DMA_FROM_DEVICE : DMA_BIDIRECTIONAL;

	rctx->src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (diff_dst)
		rctx->dst_nents = sg_nents_for_len(req->dst, req->nbytes);
	else
		rctx->dst_nents = rctx->src_nents;
	if (rctx->src_nents < 0) {
		dev_err(qce->dev, "Invalid numbers of src SG.\n");
		return rctx->src_nents;
	}
	if (rctx->dst_nents < 0) {
		dev_err(qce->dev, "Invalid numbers of dst SG.\n");
		return -rctx->dst_nents;
	}

	rctx->dst_nents += 1;

	gfp = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
						GFP_KERNEL : GFP_ATOMIC;

	ret = sg_alloc_table(&rctx->dst_tbl, rctx->dst_nents, gfp);
	if (ret)
		return ret;

	sg_init_one(&rctx->result_sg, qce->dma.result_buf, QCE_RESULT_BUF_SZ);

	sg = qce_sgtable_add(&rctx->dst_tbl, req->dst);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto error_free;
	}

	sg = qce_sgtable_add(&rctx->dst_tbl, &rctx->result_sg);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto error_free;
	}

	sg_mark_end(sg);
	rctx->dst_sg = rctx->dst_tbl.sgl;

	ret = dma_map_sg(qce->dev, rctx->dst_sg, rctx->dst_nents, dir_dst);
	if (ret < 0)
		goto error_free;

	if (diff_dst) {
		ret = dma_map_sg(qce->dev, req->src, rctx->src_nents, dir_src);
		if (ret < 0)
			goto error_unmap_dst;
		rctx->src_sg = req->src;
	} else {
		rctx->src_sg = rctx->dst_sg;
	}

	ret = qce_dma_prep_sgs(&qce->dma, rctx->src_sg, rctx->src_nents,
			       rctx->dst_sg, rctx->dst_nents,
			       qce_ablkcipher_done, async_req);
	if (ret)
		goto error_unmap_src;

	qce_dma_issue_pending(&qce->dma);

	ret = qce_start(async_req, tmpl->crypto_alg_type, req->nbytes, 0);
	if (ret)
		goto error_terminate;

	return 0;

error_terminate:
	qce_dma_terminate_all(&qce->dma);
error_unmap_src:
	if (diff_dst)
		dma_unmap_sg(qce->dev, req->src, rctx->src_nents, dir_src);
error_unmap_dst:
	dma_unmap_sg(qce->dev, rctx->dst_sg, rctx->dst_nents, dir_dst);
error_free:
	sg_free_table(&rctx->dst_tbl);
	return ret;
}

static int qce_ablkcipher_setkey(struct crypto_ablkcipher *ablk, const u8 *key,
				 unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(ablk);
	struct qce_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	unsigned long flags = to_cipher_tmpl(tfm)->alg_flags;
	int ret;

	if (!key || !keylen)
		return -EINVAL;

	if (IS_AES(flags)) {
		switch (keylen) {
		case AES_KEYSIZE_128:
		case AES_KEYSIZE_256:
			break;
		default:
			goto fallback;
		}
	} else if (IS_DES(flags)) {
		u32 tmp[DES_EXPKEY_WORDS];

		ret = des_ekey(tmp, key);
		if (!ret && (crypto_ablkcipher_get_flags(ablk) &
			     CRYPTO_TFM_REQ_FORBID_WEAK_KEYS))
			goto weakkey;
	}

	ctx->enc_keylen = keylen;
	memcpy(ctx->enc_key, key, keylen);
	return 0;
fallback:
	ret = crypto_sync_skcipher_setkey(ctx->fallback, key, keylen);
	if (!ret)
		ctx->enc_keylen = keylen;
	return ret;
weakkey:
	crypto_ablkcipher_set_flags(ablk, CRYPTO_TFM_RES_WEAK_KEY);
	return -EINVAL;
}

static int qce_des3_setkey(struct crypto_ablkcipher *ablk, const u8 *key,
			   unsigned int keylen)
{
	struct qce_cipher_ctx *ctx = crypto_ablkcipher_ctx(ablk);
	u32 flags;
	int err;

	flags = crypto_ablkcipher_get_flags(ablk);
	err = __des3_verify_key(&flags, key);
	if (unlikely(err)) {
		crypto_ablkcipher_set_flags(ablk, flags);
		return err;
	}

	ctx->enc_keylen = keylen;
	memcpy(ctx->enc_key, key, keylen);
	return 0;
}

static int qce_ablkcipher_crypt(struct ablkcipher_request *req, int encrypt)
{
	struct crypto_tfm *tfm =
			crypto_ablkcipher_tfm(crypto_ablkcipher_reqtfm(req));
	struct qce_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qce_cipher_reqctx *rctx = ablkcipher_request_ctx(req);
	struct qce_alg_template *tmpl = to_cipher_tmpl(tfm);
	int ret;

	rctx->flags = tmpl->alg_flags;
	rctx->flags |= encrypt ? QCE_ENCRYPT : QCE_DECRYPT;

	if (IS_AES(rctx->flags) && ctx->enc_keylen != AES_KEYSIZE_128 &&
	    ctx->enc_keylen != AES_KEYSIZE_256) {
		SYNC_SKCIPHER_REQUEST_ON_STACK(subreq, ctx->fallback);

		skcipher_request_set_sync_tfm(subreq, ctx->fallback);
		skcipher_request_set_callback(subreq, req->base.flags,
					      NULL, NULL);
		skcipher_request_set_crypt(subreq, req->src, req->dst,
					   req->nbytes, req->info);
		ret = encrypt ? crypto_skcipher_encrypt(subreq) :
				crypto_skcipher_decrypt(subreq);
		skcipher_request_zero(subreq);
		return ret;
	}

	return tmpl->qce->async_req_enqueue(tmpl->qce, &req->base);
}

static int qce_ablkcipher_encrypt(struct ablkcipher_request *req)
{
	return qce_ablkcipher_crypt(req, 1);
}

static int qce_ablkcipher_decrypt(struct ablkcipher_request *req)
{
	return qce_ablkcipher_crypt(req, 0);
}

static int qce_ablkcipher_init(struct crypto_tfm *tfm)
{
	struct qce_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	memset(ctx, 0, sizeof(*ctx));
	tfm->crt_ablkcipher.reqsize = sizeof(struct qce_cipher_reqctx);

	ctx->fallback = crypto_alloc_sync_skcipher(crypto_tfm_alg_name(tfm),
						   0, CRYPTO_ALG_NEED_FALLBACK);
	return PTR_ERR_OR_ZERO(ctx->fallback);
}

static void qce_ablkcipher_exit(struct crypto_tfm *tfm)
{
	struct qce_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_sync_skcipher(ctx->fallback);
}

struct qce_ablkcipher_def {
	unsigned long flags;
	const char *name;
	const char *drv_name;
	unsigned int blocksize;
	unsigned int ivsize;
	unsigned int min_keysize;
	unsigned int max_keysize;
};

static const struct qce_ablkcipher_def ablkcipher_def[] = {
	{
		.flags		= QCE_ALG_AES | QCE_MODE_ECB,
		.name		= "ecb(aes)",
		.drv_name	= "ecb-aes-qce",
		.blocksize	= AES_BLOCK_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
	},
	{
		.flags		= QCE_ALG_AES | QCE_MODE_CBC,
		.name		= "cbc(aes)",
		.drv_name	= "cbc-aes-qce",
		.blocksize	= AES_BLOCK_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
	},
	{
		.flags		= QCE_ALG_AES | QCE_MODE_CTR,
		.name		= "ctr(aes)",
		.drv_name	= "ctr-aes-qce",
		.blocksize	= AES_BLOCK_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
	},
	{
		.flags		= QCE_ALG_AES | QCE_MODE_XTS,
		.name		= "xts(aes)",
		.drv_name	= "xts-aes-qce",
		.blocksize	= AES_BLOCK_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
	},
	{
		.flags		= QCE_ALG_DES | QCE_MODE_ECB,
		.name		= "ecb(des)",
		.drv_name	= "ecb-des-qce",
		.blocksize	= DES_BLOCK_SIZE,
		.ivsize		= 0,
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
	},
	{
		.flags		= QCE_ALG_DES | QCE_MODE_CBC,
		.name		= "cbc(des)",
		.drv_name	= "cbc-des-qce",
		.blocksize	= DES_BLOCK_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
	},
	{
		.flags		= QCE_ALG_3DES | QCE_MODE_ECB,
		.name		= "ecb(des3_ede)",
		.drv_name	= "ecb-3des-qce",
		.blocksize	= DES3_EDE_BLOCK_SIZE,
		.ivsize		= 0,
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
	},
	{
		.flags		= QCE_ALG_3DES | QCE_MODE_CBC,
		.name		= "cbc(des3_ede)",
		.drv_name	= "cbc-3des-qce",
		.blocksize	= DES3_EDE_BLOCK_SIZE,
		.ivsize		= DES3_EDE_BLOCK_SIZE,
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
	},
};

static int qce_ablkcipher_register_one(const struct qce_ablkcipher_def *def,
				       struct qce_device *qce)
{
	struct qce_alg_template *tmpl;
	struct crypto_alg *alg;
	int ret;

	tmpl = kzalloc(sizeof(*tmpl), GFP_KERNEL);
	if (!tmpl)
		return -ENOMEM;

	alg = &tmpl->alg.crypto;

	snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->drv_name);

	alg->cra_blocksize = def->blocksize;
	alg->cra_ablkcipher.ivsize = def->ivsize;
	alg->cra_ablkcipher.min_keysize = def->min_keysize;
	alg->cra_ablkcipher.max_keysize = def->max_keysize;
	alg->cra_ablkcipher.setkey = IS_3DES(def->flags) ?
				     qce_des3_setkey : qce_ablkcipher_setkey;
	alg->cra_ablkcipher.encrypt = qce_ablkcipher_encrypt;
	alg->cra_ablkcipher.decrypt = qce_ablkcipher_decrypt;

	alg->cra_priority = 300;
	alg->cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC |
			 CRYPTO_ALG_NEED_FALLBACK;
	alg->cra_ctxsize = sizeof(struct qce_cipher_ctx);
	alg->cra_alignmask = 0;
	alg->cra_type = &crypto_ablkcipher_type;
	alg->cra_module = THIS_MODULE;
	alg->cra_init = qce_ablkcipher_init;
	alg->cra_exit = qce_ablkcipher_exit;

	INIT_LIST_HEAD(&tmpl->entry);
	tmpl->crypto_alg_type = CRYPTO_ALG_TYPE_ABLKCIPHER;
	tmpl->alg_flags = def->flags;
	tmpl->qce = qce;

	ret = crypto_register_alg(alg);
	if (ret) {
		kfree(tmpl);
		dev_err(qce->dev, "%s registration failed\n", alg->cra_name);
		return ret;
	}

	list_add_tail(&tmpl->entry, &ablkcipher_algs);
	dev_dbg(qce->dev, "%s is registered\n", alg->cra_name);
	return 0;
}

static void qce_ablkcipher_unregister(struct qce_device *qce)
{
	struct qce_alg_template *tmpl, *n;

	list_for_each_entry_safe(tmpl, n, &ablkcipher_algs, entry) {
		crypto_unregister_alg(&tmpl->alg.crypto);
		list_del(&tmpl->entry);
		kfree(tmpl);
	}
}

static int qce_ablkcipher_register(struct qce_device *qce)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ablkcipher_def); i++) {
		ret = qce_ablkcipher_register_one(&ablkcipher_def[i], qce);
		if (ret)
			goto err;
	}

	return 0;
err:
	qce_ablkcipher_unregister(qce);
	return ret;
}

const struct qce_algo_ops ablkcipher_ops = {
	.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
	.register_algs = qce_ablkcipher_register,
	.unregister_algs = qce_ablkcipher_unregister,
	.async_req_handle = qce_ablkcipher_async_req_handle,
};
