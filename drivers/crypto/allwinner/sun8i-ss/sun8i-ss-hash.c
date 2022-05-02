// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ss-hash.c - hardware cryptographic offloader for
 * Allwinner A80/A83T SoC
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 *
 * This file add support for MD5 and SHA1/SHA224/SHA256.
 *
 * You could find the datasheet in Documentation/arm/sunxi.rst
 */
#include <linux/bottom_half.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <crypto/md5.h>
#include "sun8i-ss.h"

int sun8i_ss_hash_crainit(struct crypto_tfm *tfm)
{
	struct sun8i_ss_hash_tfm_ctx *op = crypto_tfm_ctx(tfm);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	struct sun8i_ss_alg_template *algt;
	int err;

	memset(op, 0, sizeof(struct sun8i_ss_hash_tfm_ctx));

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash);
	op->ss = algt->ss;

	op->enginectx.op.do_one_request = sun8i_ss_hash_run;
	op->enginectx.op.prepare_request = NULL;
	op->enginectx.op.unprepare_request = NULL;

	/* FALLBACK */
	op->fallback_tfm = crypto_alloc_ahash(crypto_tfm_alg_name(tfm), 0,
					      CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(op->fallback_tfm)) {
		dev_err(algt->ss->dev, "Fallback driver could no be loaded\n");
		return PTR_ERR(op->fallback_tfm);
	}

	if (algt->alg.hash.halg.statesize < crypto_ahash_statesize(op->fallback_tfm))
		algt->alg.hash.halg.statesize = crypto_ahash_statesize(op->fallback_tfm);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct sun8i_ss_hash_reqctx) +
				 crypto_ahash_reqsize(op->fallback_tfm));

	dev_info(op->ss->dev, "Fallback for %s is %s\n",
		 crypto_tfm_alg_driver_name(tfm),
		 crypto_tfm_alg_driver_name(&op->fallback_tfm->base));
	err = pm_runtime_get_sync(op->ss->dev);
	if (err < 0)
		goto error_pm;
	return 0;
error_pm:
	pm_runtime_put_noidle(op->ss->dev);
	crypto_free_ahash(op->fallback_tfm);
	return err;
}

void sun8i_ss_hash_craexit(struct crypto_tfm *tfm)
{
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_tfm_ctx(tfm);

	crypto_free_ahash(tfmctx->fallback_tfm);
	pm_runtime_put_sync_suspend(tfmctx->ss->dev);
}

int sun8i_ss_hash_init(struct ahash_request *areq)
{
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	memset(rctx, 0, sizeof(struct sun8i_ss_hash_reqctx));

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_init(&rctx->fallback_req);
}

int sun8i_ss_hash_export(struct ahash_request *areq, void *out)
{
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(&rctx->fallback_req, out);
}

int sun8i_ss_hash_import(struct ahash_request *areq, const void *in)
{
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_import(&rctx->fallback_req, in);
}

int sun8i_ss_hash_final(struct ahash_request *areq)
{
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ss_alg_template *algt;
#endif

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = areq->result;

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash);
	algt->stat_fb++;
#endif

	return crypto_ahash_final(&rctx->fallback_req);
}

int sun8i_ss_hash_update(struct ahash_request *areq)
{
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;

	return crypto_ahash_update(&rctx->fallback_req);
}

int sun8i_ss_hash_finup(struct ahash_request *areq)
{
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ss_alg_template *algt;
#endif

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash);
	algt->stat_fb++;
#endif

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int sun8i_ss_hash_digest_fb(struct ahash_request *areq)
{
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ss_alg_template *algt;
#endif

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash);
	algt->stat_fb++;
#endif

	return crypto_ahash_digest(&rctx->fallback_req);
}

static int sun8i_ss_run_hash_task(struct sun8i_ss_dev *ss,
				  struct sun8i_ss_hash_reqctx *rctx,
				  const char *name)
{
	int flow = rctx->flow;
	u32 v = SS_START;
	int i;

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	ss->flows[flow].stat_req++;
#endif

	/* choose between stream0/stream1 */
	if (flow)
		v |= SS_FLOW1;
	else
		v |= SS_FLOW0;

	v |= rctx->method;

	for (i = 0; i < MAX_SG; i++) {
		if (!rctx->t_dst[i].addr)
			break;

		mutex_lock(&ss->mlock);
		if (i > 0) {
			v |= BIT(17);
			writel(rctx->t_dst[i - 1].addr, ss->base + SS_KEY_ADR_REG);
			writel(rctx->t_dst[i - 1].addr, ss->base + SS_IV_ADR_REG);
		}

		dev_dbg(ss->dev,
			"Processing SG %d on flow %d %s ctl=%x %d to %d method=%x src=%x dst=%x\n",
			i, flow, name, v,
			rctx->t_src[i].len, rctx->t_dst[i].len,
			rctx->method, rctx->t_src[i].addr, rctx->t_dst[i].addr);

		writel(rctx->t_src[i].addr, ss->base + SS_SRC_ADR_REG);
		writel(rctx->t_dst[i].addr, ss->base + SS_DST_ADR_REG);
		writel(rctx->t_src[i].len, ss->base + SS_LEN_ADR_REG);
		writel(BIT(0) | BIT(1), ss->base + SS_INT_CTL_REG);

		reinit_completion(&ss->flows[flow].complete);
		ss->flows[flow].status = 0;
		wmb();

		writel(v, ss->base + SS_CTL_REG);
		mutex_unlock(&ss->mlock);
		wait_for_completion_interruptible_timeout(&ss->flows[flow].complete,
							  msecs_to_jiffies(2000));
		if (ss->flows[flow].status == 0) {
			dev_err(ss->dev, "DMA timeout for %s\n", name);
			return -EFAULT;
		}
	}

	return 0;
}

static bool sun8i_ss_hash_need_fallback(struct ahash_request *areq)
{
	struct scatterlist *sg;

	if (areq->nbytes == 0)
		return true;
	/* we need to reserve one SG for the padding one */
	if (sg_nents(areq->src) > MAX_SG - 1)
		return true;
	sg = areq->src;
	while (sg) {
		/* SS can operate hash only on full block size
		 * since SS support only MD5,sha1,sha224 and sha256, blocksize
		 * is always 64
		 * TODO: handle request if last SG is not len%64
		 * but this will need to copy data on a new SG of size=64
		 */
		if (sg->length % 64 || !IS_ALIGNED(sg->offset, sizeof(u32)))
			return true;
		sg = sg_next(sg);
	}
	return false;
}

int sun8i_ss_hash_digest(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct sun8i_ss_alg_template *algt;
	struct sun8i_ss_dev *ss;
	struct crypto_engine *engine;
	struct scatterlist *sg;
	int nr_sgs, e, i;

	if (sun8i_ss_hash_need_fallback(areq))
		return sun8i_ss_hash_digest_fb(areq);

	nr_sgs = sg_nents(areq->src);
	if (nr_sgs > MAX_SG - 1)
		return sun8i_ss_hash_digest_fb(areq);

	for_each_sg(areq->src, sg, nr_sgs, i) {
		if (sg->length % 4 || !IS_ALIGNED(sg->offset, sizeof(u32)))
			return sun8i_ss_hash_digest_fb(areq);
	}

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash);
	ss = algt->ss;

	e = sun8i_ss_get_engine_number(ss);
	rctx->flow = e;
	engine = ss->flows[e].engine;

	return crypto_transfer_hash_request_to_engine(engine, areq);
}

/* sun8i_ss_hash_run - run an ahash request
 * Send the data of the request to the SS along with an extra SG with padding
 */
int sun8i_ss_hash_run(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct sun8i_ss_alg_template *algt;
	struct sun8i_ss_dev *ss;
	struct scatterlist *sg;
	int nr_sgs, err, digestsize;
	unsigned int len;
	u64 fill, min_fill, byte_count;
	void *pad, *result;
	int j, i, todo;
	__be64 *bebits;
	__le64 *lebits;
	dma_addr_t addr_res, addr_pad;
	__le32 *bf;

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash);
	ss = algt->ss;

	digestsize = algt->alg.hash.halg.digestsize;
	if (digestsize == SHA224_DIGEST_SIZE)
		digestsize = SHA256_DIGEST_SIZE;

	/* the padding could be up to two block. */
	pad = kzalloc(algt->alg.hash.halg.base.cra_blocksize * 2, GFP_KERNEL | GFP_DMA);
	if (!pad)
		return -ENOMEM;
	bf = (__le32 *)pad;

	result = kzalloc(digestsize, GFP_KERNEL | GFP_DMA);
	if (!result) {
		kfree(pad);
		return -ENOMEM;
	}

	for (i = 0; i < MAX_SG; i++) {
		rctx->t_dst[i].addr = 0;
		rctx->t_dst[i].len = 0;
	}

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	algt->stat_req++;
#endif

	rctx->method = ss->variant->alg_hash[algt->ss_algo_id];

	nr_sgs = dma_map_sg(ss->dev, areq->src, sg_nents(areq->src), DMA_TO_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > MAX_SG) {
		dev_err(ss->dev, "Invalid sg number %d\n", nr_sgs);
		err = -EINVAL;
		goto theend;
	}

	addr_res = dma_map_single(ss->dev, result, digestsize, DMA_FROM_DEVICE);
	if (dma_mapping_error(ss->dev, addr_res)) {
		dev_err(ss->dev, "DMA map dest\n");
		err = -EINVAL;
		goto theend;
	}

	len = areq->nbytes;
	sg = areq->src;
	i = 0;
	while (len > 0 && sg) {
		if (sg_dma_len(sg) == 0) {
			sg = sg_next(sg);
			continue;
		}
		rctx->t_src[i].addr = sg_dma_address(sg);
		todo = min(len, sg_dma_len(sg));
		rctx->t_src[i].len = todo / 4;
		len -= todo;
		rctx->t_dst[i].addr = addr_res;
		rctx->t_dst[i].len = digestsize / 4;
		sg = sg_next(sg);
		i++;
	}
	if (len > 0) {
		dev_err(ss->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto theend;
	}

	byte_count = areq->nbytes;
	j = 0;
	bf[j++] = cpu_to_le32(0x80);

	fill = 64 - (byte_count % 64);
	min_fill = 3 * sizeof(u32);

	if (fill < min_fill)
		fill += 64;

	j += (fill - min_fill) / sizeof(u32);

	switch (algt->ss_algo_id) {
	case SS_ID_HASH_MD5:
		lebits = (__le64 *)&bf[j];
		*lebits = cpu_to_le64(byte_count << 3);
		j += 2;
		break;
	case SS_ID_HASH_SHA1:
	case SS_ID_HASH_SHA224:
	case SS_ID_HASH_SHA256:
		bebits = (__be64 *)&bf[j];
		*bebits = cpu_to_be64(byte_count << 3);
		j += 2;
		break;
	}

	addr_pad = dma_map_single(ss->dev, pad, j * 4, DMA_TO_DEVICE);
	rctx->t_src[i].addr = addr_pad;
	rctx->t_src[i].len = j;
	rctx->t_dst[i].addr = addr_res;
	rctx->t_dst[i].len = digestsize / 4;
	if (dma_mapping_error(ss->dev, addr_pad)) {
		dev_err(ss->dev, "DMA error on padding SG\n");
		err = -EINVAL;
		goto theend;
	}

	err = sun8i_ss_run_hash_task(ss, rctx, crypto_tfm_alg_name(areq->base.tfm));

	dma_unmap_single(ss->dev, addr_pad, j * 4, DMA_TO_DEVICE);
	dma_unmap_sg(ss->dev, areq->src, nr_sgs, DMA_TO_DEVICE);
	dma_unmap_single(ss->dev, addr_res, digestsize, DMA_FROM_DEVICE);

	memcpy(areq->result, result, algt->alg.hash.halg.digestsize);
theend:
	kfree(pad);
	kfree(result);
	local_bh_disable();
	crypto_finalize_hash_request(engine, breq, err);
	local_bh_enable();
	return 0;
}
