// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <crypto/aes.h>
#include <crypto/internal/des.h>
#include <crypto/internal/skcipher.h>

#include "cipher.h"

static unsigned int aes_sw_max_len = CONFIG_CRYPTO_DEV_QCE_SW_MAX_LEN;
module_param(aes_sw_max_len, uint, 0644);
MODULE_PARM_DESC(aes_sw_max_len,
		 "Only use hardware for AES requests larger than this "
		 "[0=always use hardware; anything <16 breaks AES-GCM; default="
		 __stringify(CONFIG_CRYPTO_DEV_QCE_SW_MAX_LEN)"]");

static LIST_HEAD(skcipher_algs);

static void qce_skcipher_done(void *data)
{
	struct crypto_async_request *async_req = data;
	struct skcipher_request *req = skcipher_request_cast(async_req);
	struct qce_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct qce_alg_template *tmpl = to_cipher_tmpl(crypto_skcipher_reqtfm(req));
	struct qce_device *qce = tmpl->qce;
	struct qce_result_dump *result_buf = qce->dma.result_buf;
	enum dma_data_direction dir_src, dir_dst;
	u32 status;
	int error;
	bool diff_dst;

	diff_dst = (req->src != req->dst) ? true : false;
	dir_src = diff_dst ? DMA_TO_DEVICE : DMA_BIDIRECTIONAL;
	dir_dst = diff_dst ? DMA_FROM_DEVICE : DMA_BIDIRECTIONAL;

	error = qce_dma_terminate_all(&qce->dma);
	if (error)
		dev_dbg(qce->dev, "skcipher dma termination error (%d)\n",
			error);

	if (diff_dst)
		dma_unmap_sg(qce->dev, rctx->src_sg, rctx->src_nents, dir_src);
	dma_unmap_sg(qce->dev, rctx->dst_sg, rctx->dst_nents, dir_dst);

	sg_free_table(&rctx->dst_tbl);

	error = qce_check_status(qce, &status);
	if (error < 0)
		dev_dbg(qce->dev, "skcipher operation error (%x)\n", status);

	memcpy(rctx->iv, result_buf->encr_cntr_iv, rctx->ivsize);
	qce->async_req_done(tmpl->qce, error);
}

static int
qce_skcipher_async_req_handle(struct crypto_async_request *async_req)
{
	struct skcipher_request *req = skcipher_request_cast(async_req);
	struct qce_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct qce_alg_template *tmpl = to_cipher_tmpl(crypto_skcipher_reqtfm(req));
	struct qce_device *qce = tmpl->qce;
	enum dma_data_direction dir_src, dir_dst;
	struct scatterlist *sg;
	bool diff_dst;
	gfp_t gfp;
	int dst_nents, src_nents, ret;

	rctx->iv = req->iv;
	rctx->ivsize = crypto_skcipher_ivsize(skcipher);
	rctx->cryptlen = req->cryptlen;

	diff_dst = (req->src != req->dst) ? true : false;
	dir_src = diff_dst ? DMA_TO_DEVICE : DMA_BIDIRECTIONAL;
	dir_dst = diff_dst ? DMA_FROM_DEVICE : DMA_BIDIRECTIONAL;

	rctx->src_nents = sg_nents_for_len(req->src, req->cryptlen);
	if (diff_dst)
		rctx->dst_nents = sg_nents_for_len(req->dst, req->cryptlen);
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

	sg = qce_sgtable_add(&rctx->dst_tbl, req->dst, req->cryptlen);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto error_free;
	}

	sg = qce_sgtable_add(&rctx->dst_tbl, &rctx->result_sg,
			     QCE_RESULT_BUF_SZ);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto error_free;
	}

	sg_mark_end(sg);
	rctx->dst_sg = rctx->dst_tbl.sgl;

	dst_nents = dma_map_sg(qce->dev, rctx->dst_sg, rctx->dst_nents, dir_dst);
	if (!dst_nents) {
		ret = -EIO;
		goto error_free;
	}

	if (diff_dst) {
		src_nents = dma_map_sg(qce->dev, req->src, rctx->src_nents, dir_src);
		if (!src_nents) {
			ret = -EIO;
			goto error_unmap_dst;
		}
		rctx->src_sg = req->src;
	} else {
		rctx->src_sg = rctx->dst_sg;
		src_nents = dst_nents - 1;
	}

	ret = qce_dma_prep_sgs(&qce->dma, rctx->src_sg, src_nents,
			       rctx->dst_sg, dst_nents,
			       qce_skcipher_done, async_req);
	if (ret)
		goto error_unmap_src;

	qce_dma_issue_pending(&qce->dma);

	ret = qce_start(async_req, tmpl->crypto_alg_type);
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

static int qce_skcipher_setkey(struct crypto_skcipher *ablk, const u8 *key,
				 unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(ablk);
	struct qce_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	unsigned long flags = to_cipher_tmpl(ablk)->alg_flags;
	unsigned int __keylen;
	int ret;

	if (!key || !keylen)
		return -EINVAL;

	/*
	 * AES XTS key1 = key2 not supported by crypto engine.
	 * Revisit to request a fallback cipher in this case.
	 */
	if (IS_XTS(flags)) {
		__keylen = keylen >> 1;
		if (!memcmp(key, key + __keylen, __keylen))
			return -ENOKEY;
	} else {
		__keylen = keylen;
	}

	switch (__keylen) {
	case AES_KEYSIZE_128:
	case AES_KEYSIZE_256:
		memcpy(ctx->enc_key, key, keylen);
		break;
	case AES_KEYSIZE_192:
		break;
	default:
		return -EINVAL;
	}

	ret = crypto_skcipher_setkey(ctx->fallback, key, keylen);
	if (!ret)
		ctx->enc_keylen = keylen;
	return ret;
}

static int qce_des_setkey(struct crypto_skcipher *ablk, const u8 *key,
			  unsigned int keylen)
{
	struct qce_cipher_ctx *ctx = crypto_skcipher_ctx(ablk);
	int err;

	err = verify_skcipher_des_key(ablk, key);
	if (err)
		return err;

	ctx->enc_keylen = keylen;
	memcpy(ctx->enc_key, key, keylen);
	return 0;
}

static int qce_des3_setkey(struct crypto_skcipher *ablk, const u8 *key,
			   unsigned int keylen)
{
	struct qce_cipher_ctx *ctx = crypto_skcipher_ctx(ablk);
	u32 _key[6];
	int err;

	err = verify_skcipher_des3_key(ablk, key);
	if (err)
		return err;

	/*
	 * The crypto engine does not support any two keys
	 * being the same for triple des algorithms. The
	 * verify_skcipher_des3_key does not check for all the
	 * below conditions. Return -ENOKEY in case any two keys
	 * are the same. Revisit to see if a fallback cipher
	 * is needed to handle this condition.
	 */
	memcpy(_key, key, DES3_EDE_KEY_SIZE);
	if (!((_key[0] ^ _key[2]) | (_key[1] ^ _key[3])) ||
	    !((_key[2] ^ _key[4]) | (_key[3] ^ _key[5])) ||
	    !((_key[0] ^ _key[4]) | (_key[1] ^ _key[5])))
		return -ENOKEY;

	ctx->enc_keylen = keylen;
	memcpy(ctx->enc_key, key, keylen);
	return 0;
}

static int qce_skcipher_crypt(struct skcipher_request *req, int encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct qce_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct qce_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct qce_alg_template *tmpl = to_cipher_tmpl(tfm);
	unsigned int blocksize = crypto_skcipher_blocksize(tfm);
	int keylen;
	int ret;

	rctx->flags = tmpl->alg_flags;
	rctx->flags |= encrypt ? QCE_ENCRYPT : QCE_DECRYPT;
	keylen = IS_XTS(rctx->flags) ? ctx->enc_keylen >> 1 : ctx->enc_keylen;

	/* CE does not handle 0 length messages */
	if (!req->cryptlen)
		return 0;

	/*
	 * ECB and CBC algorithms require message lengths to be
	 * multiples of block size.
	 */
	if (IS_ECB(rctx->flags) || IS_CBC(rctx->flags))
		if (!IS_ALIGNED(req->cryptlen, blocksize))
			return -EINVAL;

	/*
	 * Conditions for requesting a fallback cipher
	 * AES-192 (not supported by crypto engine (CE))
	 * AES-XTS request with len <= 512 byte (not recommended to use CE)
	 * AES-XTS request with len > QCE_SECTOR_SIZE and
	 * is not a multiple of it.(Revisit this condition to check if it is
	 * needed in all versions of CE)
	 */
	if (IS_AES(rctx->flags) &&
	    ((keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_256) ||
	    (IS_XTS(rctx->flags) && ((req->cryptlen <= aes_sw_max_len) ||
	    (req->cryptlen > QCE_SECTOR_SIZE &&
	    req->cryptlen % QCE_SECTOR_SIZE))))) {
		skcipher_request_set_tfm(&rctx->fallback_req, ctx->fallback);
		skcipher_request_set_callback(&rctx->fallback_req,
					      req->base.flags,
					      req->base.complete,
					      req->base.data);
		skcipher_request_set_crypt(&rctx->fallback_req, req->src,
					   req->dst, req->cryptlen, req->iv);
		ret = encrypt ? crypto_skcipher_encrypt(&rctx->fallback_req) :
				crypto_skcipher_decrypt(&rctx->fallback_req);
		return ret;
	}

	return tmpl->qce->async_req_enqueue(tmpl->qce, &req->base);
}

static int qce_skcipher_encrypt(struct skcipher_request *req)
{
	return qce_skcipher_crypt(req, 1);
}

static int qce_skcipher_decrypt(struct skcipher_request *req)
{
	return qce_skcipher_crypt(req, 0);
}

static int qce_skcipher_init(struct crypto_skcipher *tfm)
{
	/* take the size without the fallback skcipher_request at the end */
	crypto_skcipher_set_reqsize(tfm, offsetof(struct qce_cipher_reqctx,
						  fallback_req));
	return 0;
}

static int qce_skcipher_init_fallback(struct crypto_skcipher *tfm)
{
	struct qce_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->fallback = crypto_alloc_skcipher(crypto_tfm_alg_name(&tfm->base),
					      0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback))
		return PTR_ERR(ctx->fallback);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct qce_cipher_reqctx) +
					 crypto_skcipher_reqsize(ctx->fallback));
	return 0;
}

static void qce_skcipher_exit(struct crypto_skcipher *tfm)
{
	struct qce_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->fallback);
}

struct qce_skcipher_def {
	unsigned long flags;
	const char *name;
	const char *drv_name;
	unsigned int blocksize;
	unsigned int chunksize;
	unsigned int ivsize;
	unsigned int min_keysize;
	unsigned int max_keysize;
};

static const struct qce_skcipher_def skcipher_def[] = {
	{
		.flags		= QCE_ALG_AES | QCE_MODE_ECB,
		.name		= "ecb(aes)",
		.drv_name	= "ecb-aes-qce",
		.blocksize	= AES_BLOCK_SIZE,
		.ivsize		= 0,
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
		.blocksize	= 1,
		.chunksize	= AES_BLOCK_SIZE,
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
		.min_keysize	= AES_MIN_KEY_SIZE * 2,
		.max_keysize	= AES_MAX_KEY_SIZE * 2,
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

static int qce_skcipher_register_one(const struct qce_skcipher_def *def,
				       struct qce_device *qce)
{
	struct qce_alg_template *tmpl;
	struct skcipher_alg *alg;
	int ret;

	tmpl = kzalloc(sizeof(*tmpl), GFP_KERNEL);
	if (!tmpl)
		return -ENOMEM;

	alg = &tmpl->alg.skcipher;

	snprintf(alg->base.cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->drv_name);

	alg->base.cra_blocksize		= def->blocksize;
	alg->chunksize			= def->chunksize;
	alg->ivsize			= def->ivsize;
	alg->min_keysize		= def->min_keysize;
	alg->max_keysize		= def->max_keysize;
	alg->setkey			= IS_3DES(def->flags) ? qce_des3_setkey :
					  IS_DES(def->flags) ? qce_des_setkey :
					  qce_skcipher_setkey;
	alg->encrypt			= qce_skcipher_encrypt;
	alg->decrypt			= qce_skcipher_decrypt;

	alg->base.cra_priority		= 275;
	alg->base.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_ALLOCATES_MEMORY |
					  CRYPTO_ALG_KERN_DRIVER_ONLY;
	alg->base.cra_ctxsize		= sizeof(struct qce_cipher_ctx);
	alg->base.cra_alignmask		= 0;
	alg->base.cra_module		= THIS_MODULE;

	if (IS_AES(def->flags)) {
		alg->base.cra_flags    |= CRYPTO_ALG_NEED_FALLBACK;
		alg->init		= qce_skcipher_init_fallback;
		alg->exit		= qce_skcipher_exit;
	} else {
		alg->init		= qce_skcipher_init;
	}

	INIT_LIST_HEAD(&tmpl->entry);
	tmpl->crypto_alg_type = CRYPTO_ALG_TYPE_SKCIPHER;
	tmpl->alg_flags = def->flags;
	tmpl->qce = qce;

	ret = crypto_register_skcipher(alg);
	if (ret) {
		dev_err(qce->dev, "%s registration failed\n", alg->base.cra_name);
		kfree(tmpl);
		return ret;
	}

	list_add_tail(&tmpl->entry, &skcipher_algs);
	dev_dbg(qce->dev, "%s is registered\n", alg->base.cra_name);
	return 0;
}

static void qce_skcipher_unregister(struct qce_device *qce)
{
	struct qce_alg_template *tmpl, *n;

	list_for_each_entry_safe(tmpl, n, &skcipher_algs, entry) {
		crypto_unregister_skcipher(&tmpl->alg.skcipher);
		list_del(&tmpl->entry);
		kfree(tmpl);
	}
}

static int qce_skcipher_register(struct qce_device *qce)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(skcipher_def); i++) {
		ret = qce_skcipher_register_one(&skcipher_def[i], qce);
		if (ret)
			goto err;
	}

	return 0;
err:
	qce_skcipher_unregister(qce);
	return ret;
}

const struct qce_algo_ops skcipher_ops = {
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.register_algs = qce_skcipher_register,
	.unregister_algs = qce_skcipher_unregister,
	.async_req_handle = qce_skcipher_async_req_handle,
};
