// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ce-hash.c - hardware cryptographic offloader for
 * Allwinner H3/A64/H5/H2+/H6/R40 SoC
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 *
 * This file add support for MD5 and SHA1/SHA224/SHA256/SHA384/SHA512.
 *
 * You could find the datasheet in Documentation/arm/sunxi.rst
 */
#include <linux/bottom_half.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/md5.h>
#include "sun8i-ce.h"

int sun8i_ce_hash_crainit(struct crypto_tfm *tfm)
{
	struct sun8i_ce_hash_tfm_ctx *op = crypto_tfm_ctx(tfm);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	struct sun8i_ce_alg_template *algt;
	int err;

	memset(op, 0, sizeof(struct sun8i_ce_hash_tfm_ctx));

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash);
	op->ce = algt->ce;

	op->enginectx.op.do_one_request = sun8i_ce_hash_run;
	op->enginectx.op.prepare_request = NULL;
	op->enginectx.op.unprepare_request = NULL;

	/* FALLBACK */
	op->fallback_tfm = crypto_alloc_ahash(crypto_tfm_alg_name(tfm), 0,
					      CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(op->fallback_tfm)) {
		dev_err(algt->ce->dev, "Fallback driver could no be loaded\n");
		return PTR_ERR(op->fallback_tfm);
	}

	if (algt->alg.hash.halg.statesize < crypto_ahash_statesize(op->fallback_tfm))
		algt->alg.hash.halg.statesize = crypto_ahash_statesize(op->fallback_tfm);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct sun8i_ce_hash_reqctx) +
				 crypto_ahash_reqsize(op->fallback_tfm));

	dev_info(op->ce->dev, "Fallback for %s is %s\n",
		 crypto_tfm_alg_driver_name(tfm),
		 crypto_tfm_alg_driver_name(&op->fallback_tfm->base));
	err = pm_runtime_get_sync(op->ce->dev);
	if (err < 0)
		goto error_pm;
	return 0;
error_pm:
	pm_runtime_put_noidle(op->ce->dev);
	crypto_free_ahash(op->fallback_tfm);
	return err;
}

void sun8i_ce_hash_craexit(struct crypto_tfm *tfm)
{
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_tfm_ctx(tfm);

	crypto_free_ahash(tfmctx->fallback_tfm);
	pm_runtime_put_sync_suspend(tfmctx->ce->dev);
}

int sun8i_ce_hash_init(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	memset(rctx, 0, sizeof(struct sun8i_ce_hash_reqctx));

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_init(&rctx->fallback_req);
}

int sun8i_ce_hash_export(struct ahash_request *areq, void *out)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(&rctx->fallback_req, out);
}

int sun8i_ce_hash_import(struct ahash_request *areq, const void *in)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_import(&rctx->fallback_req, in);
}

int sun8i_ce_hash_final(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ce_alg_template *algt;
#endif

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = areq->result;

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash);
	algt->stat_fb++;
#endif

	return crypto_ahash_final(&rctx->fallback_req);
}

int sun8i_ce_hash_update(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;

	return crypto_ahash_update(&rctx->fallback_req);
}

int sun8i_ce_hash_finup(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ce_alg_template *algt;
#endif

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash);
	algt->stat_fb++;
#endif

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int sun8i_ce_hash_digest_fb(struct ahash_request *areq)
{
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ce_alg_template *algt;
#endif

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash);
	algt->stat_fb++;
#endif

	return crypto_ahash_digest(&rctx->fallback_req);
}

static bool sun8i_ce_hash_need_fallback(struct ahash_request *areq)
{
	struct scatterlist *sg;

	if (areq->nbytes == 0)
		return true;
	/* we need to reserve one SG for padding one */
	if (sg_nents(areq->src) > MAX_SG - 1)
		return true;
	sg = areq->src;
	while (sg) {
		if (sg->length % 4 || !IS_ALIGNED(sg->offset, sizeof(u32)))
			return true;
		sg = sg_next(sg);
	}
	return false;
}

int sun8i_ce_hash_digest(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct sun8i_ce_alg_template *algt;
	struct sun8i_ce_dev *ce;
	struct crypto_engine *engine;
	struct scatterlist *sg;
	int nr_sgs, e, i;

	if (sun8i_ce_hash_need_fallback(areq))
		return sun8i_ce_hash_digest_fb(areq);

	nr_sgs = sg_nents(areq->src);
	if (nr_sgs > MAX_SG - 1)
		return sun8i_ce_hash_digest_fb(areq);

	for_each_sg(areq->src, sg, nr_sgs, i) {
		if (sg->length % 4 || !IS_ALIGNED(sg->offset, sizeof(u32)))
			return sun8i_ce_hash_digest_fb(areq);
	}

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash);
	ce = algt->ce;

	e = sun8i_ce_get_engine_number(ce);
	rctx->flow = e;
	engine = ce->chanlist[e].engine;

	return crypto_transfer_hash_request_to_engine(engine, areq);
}

int sun8i_ce_hash_run(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ce_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct sun8i_ce_alg_template *algt;
	struct sun8i_ce_dev *ce;
	struct sun8i_ce_flow *chan;
	struct ce_task *cet;
	struct scatterlist *sg;
	int nr_sgs, flow, err;
	unsigned int len;
	u32 common;
	u64 byte_count;
	__le32 *bf;
	void *buf = NULL;
	int j, i, todo;
	int nbw = 0;
	u64 fill, min_fill;
	__be64 *bebits;
	__le64 *lebits;
	void *result = NULL;
	u64 bs;
	int digestsize;
	dma_addr_t addr_res, addr_pad;

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.hash);
	ce = algt->ce;

	bs = algt->alg.hash.halg.base.cra_blocksize;
	digestsize = algt->alg.hash.halg.digestsize;
	if (digestsize == SHA224_DIGEST_SIZE)
		digestsize = SHA256_DIGEST_SIZE;
	if (digestsize == SHA384_DIGEST_SIZE)
		digestsize = SHA512_DIGEST_SIZE;

	/* the padding could be up to two block. */
	buf = kzalloc(bs * 2, GFP_KERNEL | GFP_DMA);
	if (!buf) {
		err = -ENOMEM;
		goto theend;
	}
	bf = (__le32 *)buf;

	result = kzalloc(digestsize, GFP_KERNEL | GFP_DMA);
	if (!result) {
		err = -ENOMEM;
		goto theend;
	}

	flow = rctx->flow;
	chan = &ce->chanlist[flow];

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	algt->stat_req++;
#endif
	dev_dbg(ce->dev, "%s %s len=%d\n", __func__, crypto_tfm_alg_name(areq->base.tfm), areq->nbytes);

	cet = chan->tl;
	memset(cet, 0, sizeof(struct ce_task));

	cet->t_id = cpu_to_le32(flow);
	common = ce->variant->alg_hash[algt->ce_algo_id];
	common |= CE_COMM_INT;
	cet->t_common_ctl = cpu_to_le32(common);

	cet->t_sym_ctl = 0;
	cet->t_asym_ctl = 0;

	nr_sgs = dma_map_sg(ce->dev, areq->src, sg_nents(areq->src), DMA_TO_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > MAX_SG) {
		dev_err(ce->dev, "Invalid sg number %d\n", nr_sgs);
		err = -EINVAL;
		goto theend;
	}

	len = areq->nbytes;
	for_each_sg(areq->src, sg, nr_sgs, i) {
		cet->t_src[i].addr = cpu_to_le32(sg_dma_address(sg));
		todo = min(len, sg_dma_len(sg));
		cet->t_src[i].len = cpu_to_le32(todo / 4);
		len -= todo;
	}
	if (len > 0) {
		dev_err(ce->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto theend;
	}
	addr_res = dma_map_single(ce->dev, result, digestsize, DMA_FROM_DEVICE);
	cet->t_dst[0].addr = cpu_to_le32(addr_res);
	cet->t_dst[0].len = cpu_to_le32(digestsize / 4);
	if (dma_mapping_error(ce->dev, addr_res)) {
		dev_err(ce->dev, "DMA map dest\n");
		err = -EINVAL;
		goto theend;
	}

	byte_count = areq->nbytes;
	j = 0;
	bf[j++] = cpu_to_le32(0x80);

	if (bs == 64) {
		fill = 64 - (byte_count % 64);
		min_fill = 2 * sizeof(u32) + (nbw ? 0 : sizeof(u32));
	} else {
		fill = 128 - (byte_count % 128);
		min_fill = 4 * sizeof(u32) + (nbw ? 0 : sizeof(u32));
	}

	if (fill < min_fill)
		fill += bs;

	j += (fill - min_fill) / sizeof(u32);

	switch (algt->ce_algo_id) {
	case CE_ID_HASH_MD5:
		lebits = (__le64 *)&bf[j];
		*lebits = cpu_to_le64(byte_count << 3);
		j += 2;
		break;
	case CE_ID_HASH_SHA1:
	case CE_ID_HASH_SHA224:
	case CE_ID_HASH_SHA256:
		bebits = (__be64 *)&bf[j];
		*bebits = cpu_to_be64(byte_count << 3);
		j += 2;
		break;
	case CE_ID_HASH_SHA384:
	case CE_ID_HASH_SHA512:
		bebits = (__be64 *)&bf[j];
		*bebits = cpu_to_be64(byte_count >> 61);
		j += 2;
		bebits = (__be64 *)&bf[j];
		*bebits = cpu_to_be64(byte_count << 3);
		j += 2;
		break;
	}

	addr_pad = dma_map_single(ce->dev, buf, j * 4, DMA_TO_DEVICE);
	cet->t_src[i].addr = cpu_to_le32(addr_pad);
	cet->t_src[i].len = cpu_to_le32(j);
	if (dma_mapping_error(ce->dev, addr_pad)) {
		dev_err(ce->dev, "DMA error on padding SG\n");
		err = -EINVAL;
		goto theend;
	}

	if (ce->variant->hash_t_dlen_in_bits)
		cet->t_dlen = cpu_to_le32((areq->nbytes + j * 4) * 8);
	else
		cet->t_dlen = cpu_to_le32(areq->nbytes / 4 + j);

	chan->timeout = areq->nbytes;

	err = sun8i_ce_run_task(ce, flow, crypto_tfm_alg_name(areq->base.tfm));

	dma_unmap_single(ce->dev, addr_pad, j * 4, DMA_TO_DEVICE);
	dma_unmap_sg(ce->dev, areq->src, sg_nents(areq->src),
		     DMA_TO_DEVICE);
	dma_unmap_single(ce->dev, addr_res, digestsize, DMA_FROM_DEVICE);


	memcpy(areq->result, result, algt->alg.hash.halg.digestsize);
theend:
	kfree(buf);
	kfree(result);
	local_bh_disable();
	crypto_finalize_hash_request(engine, breq, err);
	local_bh_enable();
	return 0;
}
