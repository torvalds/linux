// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ce-cipher.c - hardware cryptographic offloader for
 * Allwinner H3/A64/H5/H2+/H6/R40 SoC
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
#include <crypto/internal/des.h>
#include <crypto/internal/skcipher.h>
#include "sun8i-ce.h"

static int sun8i_ce_cipher_need_fallback(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct scatterlist *sg;

	if (sg_nents(areq->src) > MAX_SG || sg_nents(areq->dst) > MAX_SG)
		return true;

	if (areq->cryptlen < crypto_skcipher_ivsize(tfm))
		return true;

	if (areq->cryptlen == 0 || areq->cryptlen % 16)
		return true;

	sg = areq->src;
	while (sg) {
		if (sg->length % 4 || !IS_ALIGNED(sg->offset, sizeof(u32)))
			return true;
		sg = sg_next(sg);
	}
	sg = areq->dst;
	while (sg) {
		if (sg->length % 4 || !IS_ALIGNED(sg->offset, sizeof(u32)))
			return true;
		sg = sg_next(sg);
	}
	return false;
}

static int sun8i_ce_cipher_fallback(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	int err;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct sun8i_ce_alg_template *algt;

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.skcipher);
	algt->stat_fb++;
#endif

	skcipher_request_set_tfm(&rctx->fallback_req, op->fallback_tfm);
	skcipher_request_set_callback(&rctx->fallback_req, areq->base.flags,
				      areq->base.complete, areq->base.data);
	skcipher_request_set_crypt(&rctx->fallback_req, areq->src, areq->dst,
				   areq->cryptlen, areq->iv);
	if (rctx->op_dir & CE_DECRYPTION)
		err = crypto_skcipher_decrypt(&rctx->fallback_req);
	else
		err = crypto_skcipher_encrypt(&rctx->fallback_req);
	return err;
}

static int sun8i_ce_cipher_prepare(struct crypto_engine *engine, void *async_req)
{
	struct skcipher_request *areq = container_of(async_req, struct skcipher_request, base);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_ce_dev *ce = op->ce;
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct sun8i_ce_alg_template *algt;
	struct sun8i_ce_flow *chan;
	struct ce_task *cet;
	struct scatterlist *sg;
	unsigned int todo, len, offset, ivsize;
	u32 common, sym;
	int flow, i;
	int nr_sgs = 0;
	int nr_sgd = 0;
	int err = 0;

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.skcipher);

	dev_dbg(ce->dev, "%s %s %u %x IV(%p %u) key=%u\n", __func__,
		crypto_tfm_alg_name(areq->base.tfm),
		areq->cryptlen,
		rctx->op_dir, areq->iv, crypto_skcipher_ivsize(tfm),
		op->keylen);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	algt->stat_req++;
#endif

	flow = rctx->flow;

	chan = &ce->chanlist[flow];

	cet = chan->tl;
	memset(cet, 0, sizeof(struct ce_task));

	cet->t_id = cpu_to_le32(flow);
	common = ce->variant->alg_cipher[algt->ce_algo_id];
	common |= rctx->op_dir | CE_COMM_INT;
	cet->t_common_ctl = cpu_to_le32(common);
	/* CTS and recent CE (H6) need length in bytes, in word otherwise */
	if (ce->variant->cipher_t_dlen_in_bytes)
		cet->t_dlen = cpu_to_le32(areq->cryptlen);
	else
		cet->t_dlen = cpu_to_le32(areq->cryptlen / 4);

	sym = ce->variant->op_mode[algt->ce_blockmode];
	len = op->keylen;
	switch (len) {
	case 128 / 8:
		sym |= CE_AES_128BITS;
		break;
	case 192 / 8:
		sym |= CE_AES_192BITS;
		break;
	case 256 / 8:
		sym |= CE_AES_256BITS;
		break;
	}

	cet->t_sym_ctl = cpu_to_le32(sym);
	cet->t_asym_ctl = 0;

	rctx->addr_key = dma_map_single(ce->dev, op->key, op->keylen, DMA_TO_DEVICE);
	if (dma_mapping_error(ce->dev, rctx->addr_key)) {
		dev_err(ce->dev, "Cannot DMA MAP KEY\n");
		err = -EFAULT;
		goto theend;
	}
	cet->t_key = cpu_to_le32(rctx->addr_key);

	ivsize = crypto_skcipher_ivsize(tfm);
	if (areq->iv && crypto_skcipher_ivsize(tfm) > 0) {
		rctx->ivlen = ivsize;
		rctx->bounce_iv = kzalloc(ivsize, GFP_KERNEL | GFP_DMA);
		if (!rctx->bounce_iv) {
			err = -ENOMEM;
			goto theend_key;
		}
		if (rctx->op_dir & CE_DECRYPTION) {
			rctx->backup_iv = kzalloc(ivsize, GFP_KERNEL);
			if (!rctx->backup_iv) {
				err = -ENOMEM;
				goto theend_key;
			}
			offset = areq->cryptlen - ivsize;
			scatterwalk_map_and_copy(rctx->backup_iv, areq->src,
						 offset, ivsize, 0);
		}
		memcpy(rctx->bounce_iv, areq->iv, ivsize);
		rctx->addr_iv = dma_map_single(ce->dev, rctx->bounce_iv, rctx->ivlen,
					       DMA_TO_DEVICE);
		if (dma_mapping_error(ce->dev, rctx->addr_iv)) {
			dev_err(ce->dev, "Cannot DMA MAP IV\n");
			err = -ENOMEM;
			goto theend_iv;
		}
		cet->t_iv = cpu_to_le32(rctx->addr_iv);
	}

	if (areq->src == areq->dst) {
		nr_sgs = dma_map_sg(ce->dev, areq->src, sg_nents(areq->src),
				    DMA_BIDIRECTIONAL);
		if (nr_sgs <= 0 || nr_sgs > MAX_SG) {
			dev_err(ce->dev, "Invalid sg number %d\n", nr_sgs);
			err = -EINVAL;
			goto theend_iv;
		}
		nr_sgd = nr_sgs;
	} else {
		nr_sgs = dma_map_sg(ce->dev, areq->src, sg_nents(areq->src),
				    DMA_TO_DEVICE);
		if (nr_sgs <= 0 || nr_sgs > MAX_SG) {
			dev_err(ce->dev, "Invalid sg number %d\n", nr_sgs);
			err = -EINVAL;
			goto theend_iv;
		}
		nr_sgd = dma_map_sg(ce->dev, areq->dst, sg_nents(areq->dst),
				    DMA_FROM_DEVICE);
		if (nr_sgd <= 0 || nr_sgd > MAX_SG) {
			dev_err(ce->dev, "Invalid sg number %d\n", nr_sgd);
			err = -EINVAL;
			goto theend_sgs;
		}
	}

	len = areq->cryptlen;
	for_each_sg(areq->src, sg, nr_sgs, i) {
		cet->t_src[i].addr = cpu_to_le32(sg_dma_address(sg));
		todo = min(len, sg_dma_len(sg));
		cet->t_src[i].len = cpu_to_le32(todo / 4);
		dev_dbg(ce->dev, "%s total=%u SG(%d %u off=%d) todo=%u\n", __func__,
			areq->cryptlen, i, cet->t_src[i].len, sg->offset, todo);
		len -= todo;
	}
	if (len > 0) {
		dev_err(ce->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto theend_sgs;
	}

	len = areq->cryptlen;
	for_each_sg(areq->dst, sg, nr_sgd, i) {
		cet->t_dst[i].addr = cpu_to_le32(sg_dma_address(sg));
		todo = min(len, sg_dma_len(sg));
		cet->t_dst[i].len = cpu_to_le32(todo / 4);
		dev_dbg(ce->dev, "%s total=%u SG(%d %u off=%d) todo=%u\n", __func__,
			areq->cryptlen, i, cet->t_dst[i].len, sg->offset, todo);
		len -= todo;
	}
	if (len > 0) {
		dev_err(ce->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto theend_sgs;
	}

	chan->timeout = areq->cryptlen;
	rctx->nr_sgs = nr_sgs;
	rctx->nr_sgd = nr_sgd;
	return 0;

theend_sgs:
	if (areq->src == areq->dst) {
		dma_unmap_sg(ce->dev, areq->src, sg_nents(areq->src),
			     DMA_BIDIRECTIONAL);
	} else {
		if (nr_sgs > 0)
			dma_unmap_sg(ce->dev, areq->src, sg_nents(areq->src),
				     DMA_TO_DEVICE);
		dma_unmap_sg(ce->dev, areq->dst, sg_nents(areq->dst),
			     DMA_FROM_DEVICE);
	}

theend_iv:
	if (areq->iv && ivsize > 0) {
		if (rctx->addr_iv)
			dma_unmap_single(ce->dev, rctx->addr_iv, rctx->ivlen, DMA_TO_DEVICE);
		offset = areq->cryptlen - ivsize;
		if (rctx->op_dir & CE_DECRYPTION) {
			memcpy(areq->iv, rctx->backup_iv, ivsize);
			kfree_sensitive(rctx->backup_iv);
		} else {
			scatterwalk_map_and_copy(areq->iv, areq->dst, offset,
						 ivsize, 0);
		}
		kfree(rctx->bounce_iv);
	}

theend_key:
	dma_unmap_single(ce->dev, rctx->addr_key, op->keylen, DMA_TO_DEVICE);

theend:
	return err;
}

static int sun8i_ce_cipher_run(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *breq = container_of(areq, struct skcipher_request, base);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(breq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_ce_dev *ce = op->ce;
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(breq);
	int flow, err;

	flow = rctx->flow;
	err = sun8i_ce_run_task(ce, flow, crypto_tfm_alg_name(breq->base.tfm));
	crypto_finalize_skcipher_request(engine, breq, err);
	return 0;
}

static int sun8i_ce_cipher_unprepare(struct crypto_engine *engine, void *async_req)
{
	struct skcipher_request *areq = container_of(async_req, struct skcipher_request, base);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_ce_dev *ce = op->ce;
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct sun8i_ce_flow *chan;
	struct ce_task *cet;
	unsigned int ivsize, offset;
	int nr_sgs = rctx->nr_sgs;
	int nr_sgd = rctx->nr_sgd;
	int flow;

	flow = rctx->flow;
	chan = &ce->chanlist[flow];
	cet = chan->tl;
	ivsize = crypto_skcipher_ivsize(tfm);

	if (areq->src == areq->dst) {
		dma_unmap_sg(ce->dev, areq->src, nr_sgs, DMA_BIDIRECTIONAL);
	} else {
		if (nr_sgs > 0)
			dma_unmap_sg(ce->dev, areq->src, nr_sgs, DMA_TO_DEVICE);
		dma_unmap_sg(ce->dev, areq->dst, nr_sgd, DMA_FROM_DEVICE);
	}

	if (areq->iv && ivsize > 0) {
		if (cet->t_iv)
			dma_unmap_single(ce->dev, rctx->addr_iv, rctx->ivlen, DMA_TO_DEVICE);
		offset = areq->cryptlen - ivsize;
		if (rctx->op_dir & CE_DECRYPTION) {
			memcpy(areq->iv, rctx->backup_iv, ivsize);
			kfree_sensitive(rctx->backup_iv);
		} else {
			scatterwalk_map_and_copy(areq->iv, areq->dst, offset,
						 ivsize, 0);
		}
		kfree(rctx->bounce_iv);
	}

	dma_unmap_single(ce->dev, rctx->addr_key, op->keylen, DMA_TO_DEVICE);

	return 0;
}

int sun8i_ce_skdecrypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct crypto_engine *engine;
	int e;

	rctx->op_dir = CE_DECRYPTION;
	if (sun8i_ce_cipher_need_fallback(areq))
		return sun8i_ce_cipher_fallback(areq);

	e = sun8i_ce_get_engine_number(op->ce);
	rctx->flow = e;
	engine = op->ce->chanlist[e].engine;

	return crypto_transfer_skcipher_request_to_engine(engine, areq);
}

int sun8i_ce_skencrypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct crypto_engine *engine;
	int e;

	rctx->op_dir = CE_ENCRYPTION;
	if (sun8i_ce_cipher_need_fallback(areq))
		return sun8i_ce_cipher_fallback(areq);

	e = sun8i_ce_get_engine_number(op->ce);
	rctx->flow = e;
	engine = op->ce->chanlist[e].engine;

	return crypto_transfer_skcipher_request_to_engine(engine, areq);
}

int sun8i_ce_cipher_init(struct crypto_tfm *tfm)
{
	struct sun8i_cipher_tfm_ctx *op = crypto_tfm_ctx(tfm);
	struct sun8i_ce_alg_template *algt;
	const char *name = crypto_tfm_alg_name(tfm);
	struct crypto_skcipher *sktfm = __crypto_skcipher_cast(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(sktfm);
	int err;

	memset(op, 0, sizeof(struct sun8i_cipher_tfm_ctx));

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.skcipher);
	op->ce = algt->ce;

	op->fallback_tfm = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(op->fallback_tfm)) {
		dev_err(op->ce->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(op->fallback_tfm));
		return PTR_ERR(op->fallback_tfm);
	}

	sktfm->reqsize = sizeof(struct sun8i_cipher_req_ctx) +
			 crypto_skcipher_reqsize(op->fallback_tfm);


	dev_info(op->ce->dev, "Fallback for %s is %s\n",
		 crypto_tfm_alg_driver_name(&sktfm->base),
		 crypto_tfm_alg_driver_name(crypto_skcipher_tfm(op->fallback_tfm)));

	op->enginectx.op.do_one_request = sun8i_ce_cipher_run;
	op->enginectx.op.prepare_request = sun8i_ce_cipher_prepare;
	op->enginectx.op.unprepare_request = sun8i_ce_cipher_unprepare;

	err = pm_runtime_get_sync(op->ce->dev);
	if (err < 0)
		goto error_pm;

	return 0;
error_pm:
	pm_runtime_put_noidle(op->ce->dev);
	crypto_free_skcipher(op->fallback_tfm);
	return err;
}

void sun8i_ce_cipher_exit(struct crypto_tfm *tfm)
{
	struct sun8i_cipher_tfm_ctx *op = crypto_tfm_ctx(tfm);

	kfree_sensitive(op->key);
	crypto_free_skcipher(op->fallback_tfm);
	pm_runtime_put_sync_suspend(op->ce->dev);
}

int sun8i_ce_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen)
{
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sun8i_ce_dev *ce = op->ce;

	switch (keylen) {
	case 128 / 8:
		break;
	case 192 / 8:
		break;
	case 256 / 8:
		break;
	default:
		dev_dbg(ce->dev, "ERROR: Invalid keylen %u\n", keylen);
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

int sun8i_ce_des3_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen)
{
	struct sun8i_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	int err;

	err = verify_skcipher_des3_key(tfm, key);
	if (err)
		return err;

	kfree_sensitive(op->key);
	op->keylen = keylen;
	op->key = kmemdup(key, keylen, GFP_KERNEL | GFP_DMA);
	if (!op->key)
		return -ENOMEM;

	crypto_skcipher_clear_flags(op->fallback_tfm, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(op->fallback_tfm, tfm->base.crt_flags & CRYPTO_TFM_REQ_MASK);

	return crypto_skcipher_setkey(op->fallback_tfm, key, keylen);
}
