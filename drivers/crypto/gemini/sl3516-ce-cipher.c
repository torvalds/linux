// SPDX-License-Identifier: GPL-2.0
/*
 * sl3516-ce-cipher.c - hardware cryptographic offloader for Storlink SL3516 SoC
 *
 * Copyright (C) 2021 Corentin LABBE <clabbe@baylibre.com>
 *
 * This file adds support for AES cipher with 128,192,256 bits keysize in
 * ECB mode.
 */

#include <crypto/engine.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "sl3516-ce.h"

/* sl3516_ce_need_fallback - check if a request can be handled by the CE */
static bool sl3516_ce_need_fallback(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sl3516_ce_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sl3516_ce_dev *ce = op->ce;
	struct scatterlist *in_sg;
	struct scatterlist *out_sg;
	struct scatterlist *sg;

	if (areq->cryptlen == 0 || areq->cryptlen % 16) {
		ce->fallback_mod16++;
		return true;
	}

	/*
	 * check if we have enough descriptors for TX
	 * Note: TX need one control desc for each SG
	 */
	if (sg_nents(areq->src) > MAXDESC / 2) {
		ce->fallback_sg_count_tx++;
		return true;
	}
	/* check if we have enough descriptors for RX */
	if (sg_nents(areq->dst) > MAXDESC) {
		ce->fallback_sg_count_rx++;
		return true;
	}

	sg = areq->src;
	while (sg) {
		if ((sg->length % 16) != 0) {
			ce->fallback_mod16++;
			return true;
		}
		if ((sg_dma_len(sg) % 16) != 0) {
			ce->fallback_mod16++;
			return true;
		}
		if (!IS_ALIGNED(sg->offset, 16)) {
			ce->fallback_align16++;
			return true;
		}
		sg = sg_next(sg);
	}
	sg = areq->dst;
	while (sg) {
		if ((sg->length % 16) != 0) {
			ce->fallback_mod16++;
			return true;
		}
		if ((sg_dma_len(sg) % 16) != 0) {
			ce->fallback_mod16++;
			return true;
		}
		if (!IS_ALIGNED(sg->offset, 16)) {
			ce->fallback_align16++;
			return true;
		}
		sg = sg_next(sg);
	}

	/* need same numbers of SG (with same length) for source and destination */
	in_sg = areq->src;
	out_sg = areq->dst;
	while (in_sg && out_sg) {
		if (in_sg->length != out_sg->length) {
			ce->fallback_not_same_len++;
			return true;
		}
		in_sg = sg_next(in_sg);
		out_sg = sg_next(out_sg);
	}
	if (in_sg || out_sg)
		return true;

	return false;
}

static int sl3516_ce_cipher_fallback(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sl3516_ce_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sl3516_ce_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct sl3516_ce_alg_template *algt;
	int err;

	algt = container_of(alg, struct sl3516_ce_alg_template, alg.skcipher.base);
	algt->stat_fb++;

	skcipher_request_set_tfm(&rctx->fallback_req, op->fallback_tfm);
	skcipher_request_set_callback(&rctx->fallback_req, areq->base.flags,
				      areq->base.complete, areq->base.data);
	skcipher_request_set_crypt(&rctx->fallback_req, areq->src, areq->dst,
				   areq->cryptlen, areq->iv);
	if (rctx->op_dir == CE_DECRYPTION)
		err = crypto_skcipher_decrypt(&rctx->fallback_req);
	else
		err = crypto_skcipher_encrypt(&rctx->fallback_req);
	return err;
}

static int sl3516_ce_cipher(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sl3516_ce_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sl3516_ce_dev *ce = op->ce;
	struct sl3516_ce_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct sl3516_ce_alg_template *algt;
	struct scatterlist *sg;
	unsigned int todo, len;
	struct pkt_control_ecb *ecb;
	int nr_sgs = 0;
	int nr_sgd = 0;
	int err = 0;
	int i;

	algt = container_of(alg, struct sl3516_ce_alg_template, alg.skcipher.base);

	dev_dbg(ce->dev, "%s %s %u %x IV(%p %u) key=%u\n", __func__,
		crypto_tfm_alg_name(areq->base.tfm),
		areq->cryptlen,
		rctx->op_dir, areq->iv, crypto_skcipher_ivsize(tfm),
		op->keylen);

	algt->stat_req++;

	if (areq->src == areq->dst) {
		nr_sgs = dma_map_sg(ce->dev, areq->src, sg_nents(areq->src),
				    DMA_BIDIRECTIONAL);
		if (nr_sgs <= 0 || nr_sgs > MAXDESC / 2) {
			dev_err(ce->dev, "Invalid sg number %d\n", nr_sgs);
			err = -EINVAL;
			goto theend;
		}
		nr_sgd = nr_sgs;
	} else {
		nr_sgs = dma_map_sg(ce->dev, areq->src, sg_nents(areq->src),
				    DMA_TO_DEVICE);
		if (nr_sgs <= 0 || nr_sgs > MAXDESC / 2) {
			dev_err(ce->dev, "Invalid sg number %d\n", nr_sgs);
			err = -EINVAL;
			goto theend;
		}
		nr_sgd = dma_map_sg(ce->dev, areq->dst, sg_nents(areq->dst),
				    DMA_FROM_DEVICE);
		if (nr_sgd <= 0 || nr_sgd > MAXDESC) {
			dev_err(ce->dev, "Invalid sg number %d\n", nr_sgd);
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
		rctx->t_src[i].len = todo;
		dev_dbg(ce->dev, "%s total=%u SGS(%d %u off=%d) todo=%u\n", __func__,
			areq->cryptlen, i, rctx->t_src[i].len, sg->offset, todo);
		len -= todo;
		i++;
sgs_next:
		sg = sg_next(sg);
	}
	if (len > 0) {
		dev_err(ce->dev, "remaining len %d/%u nr_sgs=%d\n", len, areq->cryptlen, nr_sgs);
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
		rctx->t_dst[i].len = todo;
		dev_dbg(ce->dev, "%s total=%u SGD(%d %u off=%d) todo=%u\n", __func__,
			areq->cryptlen, i, rctx->t_dst[i].len, sg->offset, todo);
		len -= todo;
		i++;

sgd_next:
		sg = sg_next(sg);
	}
	if (len > 0) {
		dev_err(ce->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto theend_sgs;
	}

	switch (algt->mode) {
	case ECB_AES:
		rctx->pctrllen = sizeof(struct pkt_control_ecb);
		ecb = (struct pkt_control_ecb *)ce->pctrl;

		rctx->tqflag = TQ0_TYPE_CTRL;
		rctx->tqflag |= TQ1_CIPHER;
		ecb->control.op_mode = rctx->op_dir;
		ecb->control.cipher_algorithm = ECB_AES;
		ecb->cipher.header_len = 0;
		ecb->cipher.algorithm_len = areq->cryptlen;
		cpu_to_be32_array((__be32 *)ecb->key, (u32 *)op->key, op->keylen / 4);
		rctx->h = &ecb->cipher;

		rctx->tqflag |= TQ4_KEY0;
		rctx->tqflag |= TQ5_KEY4;
		rctx->tqflag |= TQ6_KEY6;
		ecb->control.aesnk = op->keylen / 4;
		break;
	}

	rctx->nr_sgs = nr_sgs;
	rctx->nr_sgd = nr_sgd;
	err = sl3516_ce_run_task(ce, rctx, crypto_tfm_alg_name(areq->base.tfm));

theend_sgs:
	if (areq->src == areq->dst) {
		dma_unmap_sg(ce->dev, areq->src, sg_nents(areq->src),
			     DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(ce->dev, areq->src, sg_nents(areq->src),
			     DMA_TO_DEVICE);
		dma_unmap_sg(ce->dev, areq->dst, sg_nents(areq->dst),
			     DMA_FROM_DEVICE);
	}

theend:

	return err;
}

int sl3516_ce_handle_cipher_request(struct crypto_engine *engine, void *areq)
{
	int err;
	struct skcipher_request *breq = container_of(areq, struct skcipher_request, base);

	err = sl3516_ce_cipher(breq);
	local_bh_disable();
	crypto_finalize_skcipher_request(engine, breq, err);
	local_bh_enable();

	return 0;
}

int sl3516_ce_skdecrypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sl3516_ce_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sl3516_ce_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct crypto_engine *engine;

	memset(rctx, 0, sizeof(struct sl3516_ce_cipher_req_ctx));
	rctx->op_dir = CE_DECRYPTION;

	if (sl3516_ce_need_fallback(areq))
		return sl3516_ce_cipher_fallback(areq);

	engine = op->ce->engine;

	return crypto_transfer_skcipher_request_to_engine(engine, areq);
}

int sl3516_ce_skencrypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct sl3516_ce_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sl3516_ce_cipher_req_ctx *rctx = skcipher_request_ctx(areq);
	struct crypto_engine *engine;

	memset(rctx, 0, sizeof(struct sl3516_ce_cipher_req_ctx));
	rctx->op_dir = CE_ENCRYPTION;

	if (sl3516_ce_need_fallback(areq))
		return sl3516_ce_cipher_fallback(areq);

	engine = op->ce->engine;

	return crypto_transfer_skcipher_request_to_engine(engine, areq);
}

int sl3516_ce_cipher_init(struct crypto_tfm *tfm)
{
	struct sl3516_ce_cipher_tfm_ctx *op = crypto_tfm_ctx(tfm);
	struct sl3516_ce_alg_template *algt;
	const char *name = crypto_tfm_alg_name(tfm);
	struct crypto_skcipher *sktfm = __crypto_skcipher_cast(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(sktfm);
	int err;

	memset(op, 0, sizeof(struct sl3516_ce_cipher_tfm_ctx));

	algt = container_of(alg, struct sl3516_ce_alg_template, alg.skcipher.base);
	op->ce = algt->ce;

	op->fallback_tfm = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(op->fallback_tfm)) {
		dev_err(op->ce->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(op->fallback_tfm));
		return PTR_ERR(op->fallback_tfm);
	}

	sktfm->reqsize = sizeof(struct sl3516_ce_cipher_req_ctx) +
			 crypto_skcipher_reqsize(op->fallback_tfm);

	dev_info(op->ce->dev, "Fallback for %s is %s\n",
		 crypto_tfm_alg_driver_name(&sktfm->base),
		 crypto_tfm_alg_driver_name(crypto_skcipher_tfm(op->fallback_tfm)));

	err = pm_runtime_get_sync(op->ce->dev);
	if (err < 0)
		goto error_pm;

	return 0;
error_pm:
	pm_runtime_put_noidle(op->ce->dev);
	crypto_free_skcipher(op->fallback_tfm);
	return err;
}

void sl3516_ce_cipher_exit(struct crypto_tfm *tfm)
{
	struct sl3516_ce_cipher_tfm_ctx *op = crypto_tfm_ctx(tfm);

	kfree_sensitive(op->key);
	crypto_free_skcipher(op->fallback_tfm);
	pm_runtime_put_sync_suspend(op->ce->dev);
}

int sl3516_ce_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen)
{
	struct sl3516_ce_cipher_tfm_ctx *op = crypto_skcipher_ctx(tfm);
	struct sl3516_ce_dev *ce = op->ce;

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
