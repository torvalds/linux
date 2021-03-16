// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ss-cipher.c - hardware cryptographic offloader for
 * Allwinner A80/A83T SoC
 *
 * Copyright (C) 2016-2019 Corentin LABBE <clabbe.montjoie@gmail.com>
 *
 * This file add support for AES cipher with 128,192,256 bits keysize in
 * CBC and ECB mode.
 *
 * You could find a link for the datasheet in Documentation/arm/sunxi.rst
 */

#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/skcipher.h>
#include "sun8i-ss.h"

static bool sun8i_ss_need_fallback(struct skcipher_request *areq)
{
	struct scatterlist *in_sg = areq->src;
	struct scatterlist *out_sg = areq->dst;
	struct scatterlist *sg;

	if (areq->cryptlen == 0 || areq->cryptlen % 16)
		return true;

	if (sg_nents(areq->src) > 8 || sg_nents(areq->dst) > 8)
		return true;

	sg = areq->src;
	while (sg) {
		if ((sg->length % 16) != 0)
			return true;
		if ((sg_dma_len(sg) % 16) != 0)
			return true;
		if (!IS_ALIGNED(sg->offset, 16))
			return true;
		sg = sg_next(sg);
	}
	sg = areq->dst;
	while (sg) {
		if ((sg->length % 16) != 0)
			return true;
		if ((sg_dma_len(sg) % 16) != 0)
			return true;
		if (!IS_ALIGNED(sg->offset, 16))
			return true;
		sg = sg_next(sg);
	}

	/* SS need same numbers of SG (with same length) for source and destination */
	in_sg = areq->src;
	out_sg = areq->dst;
	while (in_sg && out_sg) {
		if (in_sg->length != out_sg->length)
			return true;
		in_sg = sg_next(in_sg);
		out_sg = sg_next(out_sg);
	}
	if (in_sg || out_sg)
		return true;
	return false;
}

static int sun8i_ss_cipher_fallback(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	int err;

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct sun8i_ss_alg_template *algt;

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.skcipher);
	algt->stat_fb++;
#endif
	skcipher_request_set_tfm(&rctx->fallback_req, op->fallback_tfm);
	skcipher_request_set_callback(&rctx->fallback_req, areq->base.flags,
				      areq->base.complete, areq->base.data);
	skcipher_request_set_crypt(&rctx->fallback_req, areq->src, areq->dst,
				   areq->cryptlen, areq->iv);
	if (rctx->op_dir & SS_DECRYPTION)
		err = crypto_skcipher_decrypt(&rctx->fallback_req);
	else
		err = crypto_skcipher_encrypt(&rctx->fallback_req);
	return err;
}

static int sun8i_ss_cipher(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_ss_dev *ss = op->ss;
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct sun8i_ss_alg_template *algt;
	struct scatterlist *sg;
	unsigned int todo, len, offset, ivsize;
	void *backup_iv = NULL;
	int nr_sgs = 0;
	int nr_sgd = 0;
	int err = 0;
	int i;

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.skcipher);

	dev_dbg(ss->dev, "%s %s %u %x IV(%p %u) key=%u\n", __func__,
		crypto_tfm_alg_name(areq->base.tfm),
		areq->cryptlen,
		rctx->op_dir, areq->iv, crypto_skcipher_ivsize(tfm),
		op->keylen);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	algt->stat_req++;
#endif

	rctx->op_mode = ss->variant->op_mode[algt->ss_blockmode];
	rctx->method = ss->variant->alg_cipher[algt->ss_algo_id];
	rctx->keylen = op->keylen;

	rctx->p_key = dma_map_single(ss->dev, op->key, op->keylen, DMA_TO_DEVICE);
	if (dma_mapping_error(ss->dev, rctx->p_key)) {
		dev_err(ss->dev, "Cannot DMA MAP KEY\n");
		err = -EFAULT;
		goto theend;
	}

	ivsize = crypto_skcipher_ivsize(tfm);
	if (areq->iv && crypto_skcipher_ivsize(tfm) > 0) {
		rctx->ivlen = ivsize;
		rctx->biv = kzalloc(ivsize, GFP_KERNEL | GFP_DMA);
		if (!rctx->biv) {
			err = -ENOMEM;
			goto theend_key;
		}
		if (rctx->op_dir & SS_DECRYPTION) {
			backup_iv = kzalloc(ivsize, GFP_KERNEL);
			if (!backup_iv) {
				err = -ENOMEM;
				goto theend_key;
			}
			offset = areq->cryptlen - ivsize;
			scatterwalk_map_and_copy(backup_iv, areq->src, offset,
						 ivsize, 0);
		}
		memcpy(rctx->biv, areq->iv, ivsize);
		rctx->p_iv = dma_map_single(ss->dev, rctx->biv, rctx->ivlen,
					    DMA_TO_DEVICE);
		if (dma_mapping_error(ss->dev, rctx->p_iv)) {
			dev_err(ss->dev, "Cannot DMA MAP IV\n");
			err = -ENOMEM;
			goto theend_iv;
		}
	}
	if (areq->src == areq->dst) {
		nr_sgs = dma_map_sg(ss->dev, areq->src, sg_nents(areq->src),
				    DMA_BIDIRECTIONAL);
		if (nr_sgs <= 0 || nr_sgs > 8) {
			dev_err(ss->dev, "Invalid sg number %d\n", nr_sgs);
			err = -EINVAL;
			goto theend_iv;
		}
		nr_sgd = nr_sgs;
	} else {
		nr_sgs = dma_map_sg(ss->dev, areq->src, sg_nents(areq->src),
				    DMA_TO_DEVICE);
		if (nr_sgs <= 0 || nr_sgs > 8) {
			dev_err(ss->dev, "Invalid sg number %d\n", nr_sgs);
			err = -EINVAL;
			goto theend_iv;
		}
		nr_sgd = dma_map_sg(ss->dev, areq->dst, sg_nents(areq->dst),
				    DMA_FROM_DEVICE);
		if (nr_sgd <= 0 || nr_sgd > 8) {
			dev_err(ss->dev, "Invalid sg number %d\n", nr_sgd);
			err = -EINVAL;
			goto theend_sgs;
		}
	}

	len = areq->cryptlen;
	i = 0;
	sg = areq->src;
	while (i < nr_sgs && sg && len) {
		if (sg_dma_len(sg) == 0)
			goto sgs_next;
		rctx->t_src[i].addr = sg_dma_address(sg);
		todo = min(len, sg_dma_len(sg));
		rctx->t_src[i].len = todo / 4;
		dev_dbg(ss->dev, "%s total=%u SGS(%d %u off=%d) todo=%u\n", __func__,
			areq->cryptlen, i, rctx->t_src[i].len, sg->offset, todo);
		len -= todo;
		i++;
sgs_next:
		sg = sg_next(sg);
	}
	if (len > 0) {
		dev_err(ss->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto theend_sgs;
	}

	len = areq->cryptlen;
	i = 0;
	sg = areq->dst;
	while (i < nr_sgd && sg && len) {
		if (sg_dma_len(sg) == 0)
			goto sgd_next;
		rctx->t_dst[i].addr = sg_dma_address(sg);
		todo = min(len, sg_dma_len(sg));
		rctx->t_dst[i].len = todo / 4;
		dev_dbg(ss->dev, "%s total=%u SGD(%d %u off=%d) todo=%u\n", __func__,
			areq->cryptlen, i, rctx->t_dst[i].len, sg->offset, todo);
		len -= todo;
		i++;
sgd_next:
		sg = sg_next(sg);
	}
	if (len > 0) {
		dev_err(ss->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto theend_sgs;
	}

	err = sun8i_ss_run_task(ss, rctx, crypto_tfm_alg_name(areq->base.tfm));

theend_sgs:
	if (areq->src == areq->dst) {
		dma_unmap_sg(ss->dev, areq->src, sg_nents(areq->src),
			     DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(ss->dev, areq->src, sg_nents(areq->src),
			     DMA_TO_DEVICE);
		dma_unmap_sg(ss->dev, areq->dst, sg_nents(areq->dst),
			     DMA_FROM_DEVICE);
	}

theend_iv:
	if (rctx->p_iv)
		dma_unmap_single(ss->dev, rctx->p_iv, rctx->ivlen,
				 DMA_TO_DEVICE);

	if (areq->iv && ivsize > 0) {
		if (rctx->biv) {
			offset = areq->cryptlen - ivsize;
			if (rctx->op_dir & SS_DECRYPTION) {
				memcpy(areq->iv, backup_iv, ivsize);
				kfree_sensitive(backup_iv);
			} else {
				scatterwalk_map_and_copy(areq->iv, areq->dst, offset,
							 ivsize, 0);
			}
			kfree(rctx->biv);
		}
	}

theend_key:
	dma_unmap_single(ss->dev, rctx->p_key, op->keylen, DMA_TO_DEVICE);

theend:

	return err;
}

static int sun8i_ss_handle_cipher_request(struct crypto_engine *engine, void *areq)
{
	int err;
	struct skcipher_request *breq = container_of(areq, struct skcipher_request, base);

	err = sun8i_ss_cipher(breq);
	crypto_finalize_skcipher_request(engine, breq, err);

	return 0;
}

int sun8i_ss_skdecrypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct crypto_engine *engine;
	int e;

	memset(rctx, 0, sizeof(struct sun8i_cipher_req_ctx));
	rctx->op_dir = SS_DECRYPTION;

	if (sun8i_ss_need_fallback(areq))
		return sun8i_ss_cipher_fallback(areq);

	e = sun8i_ss_get_engine_number(op->ss);
	engine = op->ss->flows[e].engine;
	rctx->flow = e;

	return crypto_transfer_skcipher_request_to_engine(engine, areq);
}

int sun8i_ss_skencrypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct crypto_engine *engine;
	int e;

	memset(rctx, 0, sizeof(struct sun8i_cipher_req_ctx));
	rctx->op_dir = SS_ENCRYPTION;

	if (sun8i_ss_need_fallback(areq))
		return sun8i_ss_cipher_fallback(areq);

	e = sun8i_ss_get_engine_number(op->ss);
	engine = op->ss->flows[e].engine;
	rctx->flow = e;

	return crypto_transfer_skcipher_request_to_engine(engine, areq);
}

int sun8i_ss_cipher_init(struct crypto_tfm *tfm)
{
	struct sun8i_cipher_tfm_ctx *op = crypto_tfm_ctx(tfm);
	struct sun8i_ss_alg_template *algt;
	const char *name = crypto_tfm_alg_name(tfm);
	struct crypto_skcipher *sktfm = __crypto_skcipher_cast(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(sktfm);
	int err;

	memset(op, 0, sizeof(struct sun8i_cipher_tfm_ctx));

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.skcipher);
	op->ss = algt->ss;

	op->fallback_tfm = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(op->fallback_tfm)) {
		dev_err(op->ss->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(op->fallback_tfm));
		return PTR_ERR(op->fallback_tfm);
	}

	sktfm->reqsize = sizeof(struct sun8i_cipher_req_ctx) +
			 crypto_skcipher_reqsize(op->fallback_tfm);


	dev_info(op->ss->dev, "Fallback for %s is %s\n",
		 crypto_tfm_alg_driver_name(&sktfm->base),
		 crypto_tfm_alg_driver_name(crypto_skcipher_tfm(op->fallback_tfm)));

	op->enginectx.op.do_one_request = sun8i_ss_handle_cipher_request;
	op->enginectx.op.prepare_request = NULL;
	op->enginectx.op.unprepare_request = NULL;

	err = pm_runtime_get_sync(op->ss->dev);
	if (err < 0) {
		dev_err(op->ss->dev, "pm error %d\n", err);
		goto error_pm;
	}

	return 0;
error_pm:
	crypto_free_skcipher(op->fallback_tfm);
	return err;
}

void sun8i_ss_cipher_exit(struct crypto_tfm *tfm)
{
	struct sun8i_cipher_tfm_ctx *op = crypto_tfm_ctx(tfm);

	kfree_sensitive(op->key);
	crypto_free_skcipher(op->fallback_tfm);
	pm_runtime_put_sync(op->ss->dev);
}

int sun8i_ss_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen)
{
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_ss_dev *ss = op->ss;

	switch (keylen) {
	case 128 / 8:
		break;
	case 192 / 8:
		break;
	case 256 / 8:
		break;
	default:
		dev_dbg(ss->dev, "ERROR: Invalid keylen %u\n", keylen);
		return -EINVAL;
	}
	kfree_sensitive(op->key);
	op->keylen = keylen;
	op->key = kmemdup(key, keylen, GFP_KERNEL | GFP_DMA);
	if (!op->key)
		return -ENOMEM;

	crypto_skcipher_clear_flags(op->fallback_tfm, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(op->fallback_tfm, tfm->base.crt_flags & CRYPTO_TFM_REQ_MASK);

	return crypto_skcipher_setkey(op->fallback_tfm, key, keylen);
}

int sun8i_ss_des3_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen)
{
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_ss_dev *ss = op->ss;

	if (unlikely(keylen != 3 * DES_KEY_SIZE)) {
		dev_dbg(ss->dev, "Invalid keylen %u\n", keylen);
		return -EINVAL;
	}

	kfree_sensitive(op->key);
	op->keylen = keylen;
	op->key = kmemdup(key, keylen, GFP_KERNEL | GFP_DMA);
	if (!op->key)
		return -ENOMEM;

	crypto_skcipher_clear_flags(op->fallback_tfm, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(op->fallback_tfm, tfm->base.crt_flags & CRYPTO_TFM_REQ_MASK);

	return crypto_skcipher_setkey(op->fallback_tfm, key, keylen);
}
