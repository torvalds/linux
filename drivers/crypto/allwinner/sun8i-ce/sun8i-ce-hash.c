// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ce-hash.c - hardware cryptographic offloader for
 * Allwinner H3/A64/H5/H2+/H6/R40 SoC
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 *
 * This file add support for MD5 and SHA1/SHA224/SHA256/SHA384/SHA512.
 *
 * You could find the datasheet in Documentation/arch/arm/sunxi.rst
 */

#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <linux/bottom_half.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "sun8i-ce.h"

static void sun8i_ce_hash_stat_fb_inc(struct crypto_ahash *tfm)
{
	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG)) {
		struct sun8i_ce_alg_template *algt;
		struct ahash_alg *alg = crypto_ahash_alg(tfm);

		algt = container_of(alg, struct sun8i_ce_alg_template,
				    alg.hash.base);
		algt->stat_fb++;
	}
}

int sun8i_ce_hash_init_tfm(struct crypto_ahash *tfm)
{
	struct sun8i_ce_hash_tfm_ctx *op = crypto_ahash_ctx(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct sun8i_ce_alg_template *algt;
	int err;

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash.base);
	op->ce = algt->ce;

	/* FALLBACK */
	op->fallback_tfm = crypto_alloc_ahash(crypto_ahash_alg_name(tfm), 0,
					      CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(op->fallback_tfm)) {
		dev_err(algt->ce->dev, "Fallback driver could no be loaded\n");
		return PTR_ERR(op->fallback_tfm);
	}

	crypto_ahash_set_statesize(tfm,
				   crypto_ahash_statesize(op->fallback_tfm));

	crypto_ahash_set_reqsize(tfm,
				 sizeof(struct sun8i_ce_hash_reqctx) +
				 crypto_ahash_reqsize(op->fallback_tfm) +
				 CRYPTO_DMA_PADDING);

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG))
		memcpy(algt->fbname,
		       crypto_ahash_driver_name(op->fallback_tfm),
		       CRYPTO_MAX_ALG_NAME);

	err = pm_runtime_resume_and_get(op->ce->dev);
	if (err < 0)
		goto error_pm;
	return 0;
error_pm:
	crypto_free_ahash(op->fallback_tfm);
	return err;
}

void sun8i_ce_hash_exit_tfm(struct crypto_ahash *tfm)
{
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	crypto_free_ahash(tfmctx->fallback_tfm);
	pm_runtime_put_sync_suspend(tfmctx->ce->dev);
}

int sun8i_ce_hash_init(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	memset(rctx, 0, sizeof(struct sun8i_ce_hash_reqctx));

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);

	return crypto_ahash_init(&rctx->fallback_req);
}

int sun8i_ce_hash_export(struct ahash_request *areq, void *out)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);

	return crypto_ahash_export(&rctx->fallback_req, out);
}

int sun8i_ce_hash_import(struct ahash_request *areq, const void *in)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);

	return crypto_ahash_import(&rctx->fallback_req, in);
}

int sun8i_ce_hash_final(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	sun8i_ce_hash_stat_fb_inc(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);
	ahash_request_set_crypt(&rctx->fallback_req, NULL, areq->result, 0);

	return crypto_ahash_final(&rctx->fallback_req);
}

int sun8i_ce_hash_update(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);
	ahash_request_set_crypt(&rctx->fallback_req, areq->src, NULL, areq->nbytes);

	return crypto_ahash_update(&rctx->fallback_req);
}

int sun8i_ce_hash_finup(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	sun8i_ce_hash_stat_fb_inc(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);
	ahash_request_set_crypt(&rctx->fallback_req, areq->src, areq->result,
				areq->nbytes);

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int sun8i_ce_hash_digest_fb(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	sun8i_ce_hash_stat_fb_inc(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);
	ahash_request_set_crypt(&rctx->fallback_req, areq->src, areq->result,
				areq->nbytes);

	return crypto_ahash_digest(&rctx->fallback_req);
}

static bool sun8i_ce_hash_need_fallback(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ce_alg_template *algt;
	struct scatterlist *sg;

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash.base);

	if (areq->nbytes == 0) {
		if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG))
			algt->stat_fb_len0++;

		return true;
	}
	/* we need to reserve one SG for padding one */
	if (sg_nents_for_len(areq->src, areq->nbytes) > MAX_SG - 1) {
		if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG))
			algt->stat_fb_maxsg++;

		return true;
	}
	sg = areq->src;
	while (sg) {
		if (sg->length % 4) {
			if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG))
				algt->stat_fb_srclen++;

			return true;
		}
		if (!IS_ALIGNED(sg->offset, sizeof(u32))) {
			if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG))
				algt->stat_fb_srcali++;

			return true;
		}
		sg = sg_next(sg);
	}
	return false;
}

int sun8i_ce_hash_digest(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *ctx = crypto_ahash_ctx(tfm);
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct sun8i_ce_dev *ce = ctx->ce;
	struct crypto_engine *engine;
	int e;

	if (sun8i_ce_hash_need_fallback(areq))
		return sun8i_ce_hash_digest_fb(areq);

	e = sun8i_ce_get_engine_number(ce);
	rctx->flow = e;
	engine = ce->chanlist[e].engine;

	return crypto_transfer_hash_request_to_engine(engine, areq);
}

static u64 hash_pad(__le32 *buf, unsigned int bufsize, u64 padi, u64 byte_count, bool le, int bs)
{
	u64 fill, min_fill, j, k;
	__be64 *bebits;
	__le64 *lebits;

	j = padi;
	buf[j++] = cpu_to_le32(0x80);

	if (bs == 64) {
		fill = 64 - (byte_count % 64);
		min_fill = 2 * sizeof(u32) + sizeof(u32);
	} else {
		fill = 128 - (byte_count % 128);
		min_fill = 4 * sizeof(u32) + sizeof(u32);
	}

	if (fill < min_fill)
		fill += bs;

	k = j;
	j += (fill - min_fill) / sizeof(u32);
	if (j * 4 > bufsize) {
		pr_err("%s OVERFLOW %llu\n", __func__, j);
		return 0;
	}
	for (; k < j; k++)
		buf[k] = 0;

	if (le) {
		/* MD5 */
		lebits = (__le64 *)&buf[j];
		*lebits = cpu_to_le64(byte_count << 3);
		j += 2;
	} else {
		if (bs == 64) {
			/* sha1 sha224 sha256 */
			bebits = (__be64 *)&buf[j];
			*bebits = cpu_to_be64(byte_count << 3);
			j += 2;
		} else {
			/* sha384 sha512*/
			bebits = (__be64 *)&buf[j];
			*bebits = cpu_to_be64(byte_count >> 61);
			j += 2;
			bebits = (__be64 *)&buf[j];
			*bebits = cpu_to_be64(byte_count << 3);
			j += 2;
		}
	}
	if (j * 4 > bufsize) {
		pr_err("%s OVERFLOW %llu\n", __func__, j);
		return 0;
	}

	return j;
}

static int sun8i_ce_hash_prepare(struct ahash_request *areq, struct ce_task *cet)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct sun8i_ce_alg_template *algt;
	struct sun8i_ce_dev *ce;
	struct scatterlist *sg;
	int nr_sgs, err;
	unsigned int len;
	u32 common;
	u64 byte_count;
	__le32 *bf;
	int j, i, todo;
	u64 bs;
	int digestsize;

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash.base);
	ce = algt->ce;

	bs = crypto_ahash_blocksize(tfm);
	digestsize = crypto_ahash_digestsize(tfm);
	if (digestsize == SHA224_DIGEST_SIZE)
		digestsize = SHA256_DIGEST_SIZE;
	if (digestsize == SHA384_DIGEST_SIZE)
		digestsize = SHA512_DIGEST_SIZE;

	bf = (__le32 *)rctx->pad;

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG))
		algt->stat_req++;

	dev_dbg(ce->dev, "%s %s len=%d\n", __func__, crypto_tfm_alg_name(areq->base.tfm), areq->nbytes);

	memset(cet, 0, sizeof(struct ce_task));

	cet->t_id = cpu_to_le32(rctx->flow);
	common = ce->variant->alg_hash[algt->ce_algo_id];
	common |= CE_COMM_INT;
	cet->t_common_ctl = cpu_to_le32(common);

	cet->t_sym_ctl = 0;
	cet->t_asym_ctl = 0;

	rctx->nr_sgs = sg_nents_for_len(areq->src, areq->nbytes);
	nr_sgs = dma_map_sg(ce->dev, areq->src, rctx->nr_sgs, DMA_TO_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > MAX_SG) {
		dev_err(ce->dev, "Invalid sg number %d\n", nr_sgs);
		err = -EINVAL;
		goto err_out;
	}

	len = areq->nbytes;
	for_each_sg(areq->src, sg, nr_sgs, i) {
		cet->t_src[i].addr = desc_addr_val_le32(ce, sg_dma_address(sg));
		todo = min(len, sg_dma_len(sg));
		cet->t_src[i].len = cpu_to_le32(todo / 4);
		len -= todo;
	}
	if (len > 0) {
		dev_err(ce->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto err_unmap_src;
	}

	rctx->result_len = digestsize;
	rctx->addr_res = dma_map_single(ce->dev, rctx->result, rctx->result_len,
					DMA_FROM_DEVICE);
	cet->t_dst[0].addr = desc_addr_val_le32(ce, rctx->addr_res);
	cet->t_dst[0].len = cpu_to_le32(rctx->result_len / 4);
	if (dma_mapping_error(ce->dev, rctx->addr_res)) {
		dev_err(ce->dev, "DMA map dest\n");
		err = -EINVAL;
		goto err_unmap_src;
	}

	byte_count = areq->nbytes;
	j = 0;

	switch (algt->ce_algo_id) {
	case CE_ID_HASH_MD5:
		j = hash_pad(bf, 2 * bs, j, byte_count, true, bs);
		break;
	case CE_ID_HASH_SHA1:
	case CE_ID_HASH_SHA224:
	case CE_ID_HASH_SHA256:
		j = hash_pad(bf, 2 * bs, j, byte_count, false, bs);
		break;
	case CE_ID_HASH_SHA384:
	case CE_ID_HASH_SHA512:
		j = hash_pad(bf, 2 * bs, j, byte_count, false, bs);
		break;
	}
	if (!j) {
		err = -EINVAL;
		goto err_unmap_result;
	}

	rctx->pad_len = j * 4;
	rctx->addr_pad = dma_map_single(ce->dev, rctx->pad, rctx->pad_len,
					DMA_TO_DEVICE);
	cet->t_src[i].addr = desc_addr_val_le32(ce, rctx->addr_pad);
	cet->t_src[i].len = cpu_to_le32(j);
	if (dma_mapping_error(ce->dev, rctx->addr_pad)) {
		dev_err(ce->dev, "DMA error on padding SG\n");
		err = -EINVAL;
		goto err_unmap_result;
	}

	if (ce->variant->hash_t_dlen_in_bits)
		cet->t_dlen = cpu_to_le32((areq->nbytes + j * 4) * 8);
	else
		cet->t_dlen = cpu_to_le32(areq->nbytes / 4 + j);

	return 0;

err_unmap_result:
	dma_unmap_single(ce->dev, rctx->addr_res, rctx->result_len,
			 DMA_FROM_DEVICE);

err_unmap_src:
	dma_unmap_sg(ce->dev, areq->src, rctx->nr_sgs, DMA_TO_DEVICE);

err_out:
	return err;
}

static void sun8i_ce_hash_unprepare(struct ahash_request *areq,
				    struct ce_task *cet)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *ctx = crypto_ahash_ctx(tfm);
	struct sun8i_ce_dev *ce = ctx->ce;

	dma_unmap_single(ce->dev, rctx->addr_pad, rctx->pad_len, DMA_TO_DEVICE);
	dma_unmap_single(ce->dev, rctx->addr_res, rctx->result_len,
			 DMA_FROM_DEVICE);
	dma_unmap_sg(ce->dev, areq->src, rctx->nr_sgs, DMA_TO_DEVICE);
}

int sun8i_ce_hash_run(struct crypto_engine *engine, void *async_req)
{
	struct ahash_request *areq = ahash_request_cast(async_req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *ctx = crypto_ahash_ctx(tfm);
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct sun8i_ce_dev *ce = ctx->ce;
	struct sun8i_ce_flow *chan;
	int err;

	chan = &ce->chanlist[rctx->flow];

	err = sun8i_ce_hash_prepare(areq, chan->tl);
	if (err)
		return err;

	err = sun8i_ce_run_task(ce, rctx->flow, crypto_ahash_alg_name(tfm));

	sun8i_ce_hash_unprepare(areq, chan->tl);

	if (!err)
		memcpy(areq->result, rctx->result,
		       crypto_ahash_digestsize(tfm));

	local_bh_disable();
	crypto_finalize_hash_request(engine, async_req, err);
	local_bh_enable();

	return 0;
}
