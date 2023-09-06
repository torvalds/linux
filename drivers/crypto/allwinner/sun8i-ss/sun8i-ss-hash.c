// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ss-hash.c - hardware cryptographic offloader for
 * Allwinner A80/A83T SoC
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 *
 * This file add support for MD5 and SHA1/SHA224/SHA256.
 *
 * You could find the datasheet in Documentation/arch/arm/sunxi.rst
 */

#include <crypto/hmac.h>
#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <linux/bottom_half.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "sun8i-ss.h"

static int sun8i_ss_hashkey(struct sun8i_ss_hash_tfm_ctx *tfmctx, const u8 *key,
			    unsigned int keylen)
{
	struct crypto_shash *xtfm;
	struct shash_desc *sdesc;
	size_t len;
	int ret = 0;

	xtfm = crypto_alloc_shash("sha1", 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(xtfm))
		return PTR_ERR(xtfm);

	len = sizeof(*sdesc) + crypto_shash_descsize(xtfm);
	sdesc = kmalloc(len, GFP_KERNEL);
	if (!sdesc) {
		ret = -ENOMEM;
		goto err_hashkey_sdesc;
	}
	sdesc->tfm = xtfm;

	ret = crypto_shash_init(sdesc);
	if (ret) {
		dev_err(tfmctx->ss->dev, "shash init error ret=%d\n", ret);
		goto err_hashkey;
	}
	ret = crypto_shash_finup(sdesc, key, keylen, tfmctx->key);
	if (ret)
		dev_err(tfmctx->ss->dev, "shash finup error\n");
err_hashkey:
	kfree(sdesc);
err_hashkey_sdesc:
	crypto_free_shash(xtfm);
	return ret;
}

int sun8i_ss_hmac_setkey(struct crypto_ahash *ahash, const u8 *key,
			 unsigned int keylen)
{
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(ahash);
	int digestsize, i;
	int bs = crypto_ahash_blocksize(ahash);
	int ret;

	digestsize = crypto_ahash_digestsize(ahash);

	if (keylen > bs) {
		ret = sun8i_ss_hashkey(tfmctx, key, keylen);
		if (ret)
			return ret;
		tfmctx->keylen = digestsize;
	} else {
		tfmctx->keylen = keylen;
		memcpy(tfmctx->key, key, keylen);
	}

	tfmctx->ipad = kzalloc(bs, GFP_KERNEL);
	if (!tfmctx->ipad)
		return -ENOMEM;
	tfmctx->opad = kzalloc(bs, GFP_KERNEL);
	if (!tfmctx->opad) {
		ret = -ENOMEM;
		goto err_opad;
	}

	memset(tfmctx->key + tfmctx->keylen, 0, bs - tfmctx->keylen);
	memcpy(tfmctx->ipad, tfmctx->key, tfmctx->keylen);
	memcpy(tfmctx->opad, tfmctx->key, tfmctx->keylen);
	for (i = 0; i < bs; i++) {
		tfmctx->ipad[i] ^= HMAC_IPAD_VALUE;
		tfmctx->opad[i] ^= HMAC_OPAD_VALUE;
	}

	ret = crypto_ahash_setkey(tfmctx->fallback_tfm, key, keylen);
	if (!ret)
		return 0;

	memzero_explicit(tfmctx->key, keylen);
	kfree_sensitive(tfmctx->opad);
err_opad:
	kfree_sensitive(tfmctx->ipad);
	return ret;
}

int sun8i_ss_hash_init_tfm(struct crypto_ahash *tfm)
{
	struct sun8i_ss_hash_tfm_ctx *op = crypto_ahash_ctx(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct sun8i_ss_alg_template *algt;
	int err;

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash.base);
	op->ss = algt->ss;

	/* FALLBACK */
	op->fallback_tfm = crypto_alloc_ahash(crypto_ahash_alg_name(tfm), 0,
					      CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(op->fallback_tfm)) {
		dev_err(algt->ss->dev, "Fallback driver could no be loaded\n");
		return PTR_ERR(op->fallback_tfm);
	}

	crypto_ahash_set_statesize(tfm,
				   crypto_ahash_statesize(op->fallback_tfm));

	crypto_ahash_set_reqsize(tfm,
				 sizeof(struct sun8i_ss_hash_reqctx) +
				 crypto_ahash_reqsize(op->fallback_tfm));

	memcpy(algt->fbname, crypto_ahash_driver_name(op->fallback_tfm),
	       CRYPTO_MAX_ALG_NAME);

	err = pm_runtime_get_sync(op->ss->dev);
	if (err < 0)
		goto error_pm;
	return 0;
error_pm:
	pm_runtime_put_noidle(op->ss->dev);
	crypto_free_ahash(op->fallback_tfm);
	return err;
}

void sun8i_ss_hash_exit_tfm(struct crypto_ahash *tfm)
{
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	kfree_sensitive(tfmctx->ipad);
	kfree_sensitive(tfmctx->opad);

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

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = areq->result;

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG)) {
		struct ahash_alg *alg = crypto_ahash_alg(tfm);
		struct sun8i_ss_alg_template *algt __maybe_unused;

		algt = container_of(alg, struct sun8i_ss_alg_template,
				    alg.hash.base);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
		algt->stat_fb++;
#endif
	}

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

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG)) {
		struct ahash_alg *alg = crypto_ahash_alg(tfm);
		struct sun8i_ss_alg_template *algt __maybe_unused;

		algt = container_of(alg, struct sun8i_ss_alg_template,
				    alg.hash.base);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
		algt->stat_fb++;
#endif
	}

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int sun8i_ss_hash_digest_fb(struct ahash_request *areq)
{
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG)) {
		struct ahash_alg *alg = crypto_ahash_alg(tfm);
		struct sun8i_ss_alg_template *algt __maybe_unused;

		algt = container_of(alg, struct sun8i_ss_alg_template,
				    alg.hash.base);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
		algt->stat_fb++;
#endif
	}

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
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct sun8i_ss_alg_template *algt;
	struct scatterlist *sg;

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash.base);

	if (areq->nbytes == 0) {
		algt->stat_fb_len++;
		return true;
	}

	if (areq->nbytes >= MAX_PAD_SIZE - 64) {
		algt->stat_fb_len++;
		return true;
	}

	/* we need to reserve one SG for the padding one */
	if (sg_nents(areq->src) > MAX_SG - 1) {
		algt->stat_fb_sgnum++;
		return true;
	}

	sg = areq->src;
	while (sg) {
		/* SS can operate hash only on full block size
		 * since SS support only MD5,sha1,sha224 and sha256, blocksize
		 * is always 64
		 */
		/* Only the last block could be bounced to the pad buffer */
		if (sg->length % 64 && sg_next(sg)) {
			algt->stat_fb_sglen++;
			return true;
		}
		if (!IS_ALIGNED(sg->offset, sizeof(u32))) {
			algt->stat_fb_align++;
			return true;
		}
		if (sg->length % 4) {
			algt->stat_fb_sglen++;
			return true;
		}
		sg = sg_next(sg);
	}
	return false;
}

int sun8i_ss_hash_digest(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct sun8i_ss_alg_template *algt;
	struct sun8i_ss_dev *ss;
	struct crypto_engine *engine;
	int e;

	if (sun8i_ss_hash_need_fallback(areq))
		return sun8i_ss_hash_digest_fb(areq);

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash.base);
	ss = algt->ss;

	e = sun8i_ss_get_engine_number(ss);
	rctx->flow = e;
	engine = ss->flows[e].engine;

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

/* sun8i_ss_hash_run - run an ahash request
 * Send the data of the request to the SS along with an extra SG with padding
 */
int sun8i_ss_hash_run(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun8i_ss_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);
	struct sun8i_ss_hash_reqctx *rctx = ahash_request_ctx(areq);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct sun8i_ss_alg_template *algt;
	struct sun8i_ss_dev *ss;
	struct scatterlist *sg;
	int bs = crypto_ahash_blocksize(tfm);
	int nr_sgs, err, digestsize;
	unsigned int len;
	u64 byte_count;
	void *pad, *result;
	int j, i, k, todo;
	dma_addr_t addr_res, addr_pad, addr_xpad;
	__le32 *bf;
	/* HMAC step:
	 * 0: normal hashing
	 * 1: IPAD
	 * 2: OPAD
	 */
	int hmac = 0;

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.hash.base);
	ss = algt->ss;

	digestsize = crypto_ahash_digestsize(tfm);
	if (digestsize == SHA224_DIGEST_SIZE)
		digestsize = SHA256_DIGEST_SIZE;

	result = ss->flows[rctx->flow].result;
	pad = ss->flows[rctx->flow].pad;
	bf = (__le32 *)pad;

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
		goto err_dma_result;
	}

	j = 0;
	len = areq->nbytes;
	sg = areq->src;
	i = 0;
	while (len > 0 && sg) {
		if (sg_dma_len(sg) == 0) {
			sg = sg_next(sg);
			continue;
		}
		todo = min(len, sg_dma_len(sg));
		/* only the last SG could be with a size not modulo64 */
		if (todo % 64 == 0) {
			rctx->t_src[i].addr = sg_dma_address(sg);
			rctx->t_src[i].len = todo / 4;
			rctx->t_dst[i].addr = addr_res;
			rctx->t_dst[i].len = digestsize / 4;
			len -= todo;
		} else {
			scatterwalk_map_and_copy(bf, sg, 0, todo, 0);
			j += todo / 4;
			len -= todo;
		}
		sg = sg_next(sg);
		i++;
	}
	if (len > 0) {
		dev_err(ss->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto theend;
	}

	if (j > 0)
		i--;

retry:
	byte_count = areq->nbytes;
	if (tfmctx->keylen && hmac == 0) {
		hmac = 1;
		/* shift all SG one slot up, to free slot 0 for IPAD */
		for (k = 6; k >= 0; k--) {
			rctx->t_src[k + 1].addr = rctx->t_src[k].addr;
			rctx->t_src[k + 1].len = rctx->t_src[k].len;
			rctx->t_dst[k + 1].addr = rctx->t_dst[k].addr;
			rctx->t_dst[k + 1].len = rctx->t_dst[k].len;
		}
		addr_xpad = dma_map_single(ss->dev, tfmctx->ipad, bs, DMA_TO_DEVICE);
		err = dma_mapping_error(ss->dev, addr_xpad);
		if (err) {
			dev_err(ss->dev, "Fail to create DMA mapping of ipad\n");
			goto err_dma_xpad;
		}
		rctx->t_src[0].addr = addr_xpad;
		rctx->t_src[0].len = bs / 4;
		rctx->t_dst[0].addr = addr_res;
		rctx->t_dst[0].len = digestsize / 4;
		i++;
		byte_count = areq->nbytes + bs;
	}
	if (tfmctx->keylen && hmac == 2) {
		for (i = 0; i < MAX_SG; i++) {
			rctx->t_src[i].addr = 0;
			rctx->t_src[i].len = 0;
			rctx->t_dst[i].addr = 0;
			rctx->t_dst[i].len = 0;
		}

		addr_res = dma_map_single(ss->dev, result, digestsize, DMA_FROM_DEVICE);
		if (dma_mapping_error(ss->dev, addr_res)) {
			dev_err(ss->dev, "Fail to create DMA mapping of result\n");
			err = -EINVAL;
			goto err_dma_result;
		}
		addr_xpad = dma_map_single(ss->dev, tfmctx->opad, bs, DMA_TO_DEVICE);
		err = dma_mapping_error(ss->dev, addr_xpad);
		if (err) {
			dev_err(ss->dev, "Fail to create DMA mapping of opad\n");
			goto err_dma_xpad;
		}
		rctx->t_src[0].addr = addr_xpad;
		rctx->t_src[0].len = bs / 4;

		memcpy(bf, result, digestsize);
		j = digestsize / 4;
		i = 1;
		byte_count = digestsize + bs;

		rctx->t_dst[0].addr = addr_res;
		rctx->t_dst[0].len = digestsize / 4;
	}

	switch (algt->ss_algo_id) {
	case SS_ID_HASH_MD5:
		j = hash_pad(bf, 4096, j, byte_count, true, bs);
		break;
	case SS_ID_HASH_SHA1:
	case SS_ID_HASH_SHA224:
	case SS_ID_HASH_SHA256:
		j = hash_pad(bf, 4096, j, byte_count, false, bs);
		break;
	}
	if (!j) {
		err = -EINVAL;
		goto theend;
	}

	addr_pad = dma_map_single(ss->dev, pad, j * 4, DMA_TO_DEVICE);
	if (dma_mapping_error(ss->dev, addr_pad)) {
		dev_err(ss->dev, "DMA error on padding SG\n");
		err = -EINVAL;
		goto err_dma_pad;
	}
	rctx->t_src[i].addr = addr_pad;
	rctx->t_src[i].len = j;
	rctx->t_dst[i].addr = addr_res;
	rctx->t_dst[i].len = digestsize / 4;

	err = sun8i_ss_run_hash_task(ss, rctx, crypto_tfm_alg_name(areq->base.tfm));

	/*
	 * mini helper for checking dma map/unmap
	 * flow start for hmac = 0 (and HMAC = 1)
	 * HMAC = 0
	 * MAP src
	 * MAP res
	 *
	 * retry:
	 * if hmac then hmac = 1
	 *	MAP xpad (ipad)
	 * if hmac == 2
	 *	MAP res
	 *	MAP xpad (opad)
	 * MAP pad
	 * ACTION!
	 * UNMAP pad
	 * if hmac
	 *	UNMAP xpad
	 * UNMAP res
	 * if hmac < 2
	 *	UNMAP SRC
	 *
	 * if hmac = 1 then hmac = 2 goto retry
	 */

	dma_unmap_single(ss->dev, addr_pad, j * 4, DMA_TO_DEVICE);

err_dma_pad:
	if (hmac > 0)
		dma_unmap_single(ss->dev, addr_xpad, bs, DMA_TO_DEVICE);
err_dma_xpad:
	dma_unmap_single(ss->dev, addr_res, digestsize, DMA_FROM_DEVICE);
err_dma_result:
	if (hmac < 2)
		dma_unmap_sg(ss->dev, areq->src, sg_nents(areq->src),
			     DMA_TO_DEVICE);
	if (hmac == 1 && !err) {
		hmac = 2;
		goto retry;
	}

	if (!err)
		memcpy(areq->result, result, crypto_ahash_digestsize(tfm));
theend:
	local_bh_disable();
	crypto_finalize_hash_request(engine, breq, err);
	local_bh_enable();
	return 0;
}
