// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2015-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2019 NXP
 */

#include "compat.h"
#include "regs.h"
#include "caamalg_qi2.h"
#include "dpseci_cmd.h"
#include "desc_constr.h"
#include "error.h"
#include "sg_sw_sec4.h"
#include "sg_sw_qm2.h"
#include "key_gen.h"
#include "caamalg_desc.h"
#include "caamhash_desc.h"
#include "dpseci-debugfs.h"
#include <linux/dma-mapping.h>
#include <linux/fsl/mc.h>
#include <linux/kernel.h>
#include <soc/fsl/dpaa2-io.h>
#include <soc/fsl/dpaa2-fd.h>
#include <crypto/xts.h>
#include <asm/unaligned.h>

#define CAAM_CRA_PRIORITY	2000

/* max key is sum of AES_MAX_KEY_SIZE, max split key size */
#define CAAM_MAX_KEY_SIZE	(AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE + \
				 SHA512_DIGEST_SIZE * 2)

/*
 * This is a cache of buffers, from which the users of CAAM QI driver
 * can allocate short buffers. It's speedier than doing kmalloc on the hotpath.
 * NOTE: A more elegant solution would be to have some headroom in the frames
 *       being processed. This can be added by the dpaa2-eth driver. This would
 *       pose a problem for userspace application processing which cannot
 *       know of this limitation. So for now, this will work.
 * NOTE: The memcache is SMP-safe. No need to handle spinlocks in-here
 */
static struct kmem_cache *qi_cache;

struct caam_alg_entry {
	struct device *dev;
	int class1_alg_type;
	int class2_alg_type;
	bool rfc3686;
	bool geniv;
	bool nodkp;
};

struct caam_aead_alg {
	struct aead_alg aead;
	struct caam_alg_entry caam;
	bool registered;
};

struct caam_skcipher_alg {
	struct skcipher_alg skcipher;
	struct caam_alg_entry caam;
	bool registered;
};

/**
 * struct caam_ctx - per-session context
 * @flc: Flow Contexts array
 * @key:  [authentication key], encryption key
 * @flc_dma: I/O virtual addresses of the Flow Contexts
 * @key_dma: I/O virtual address of the key
 * @dir: DMA direction for mapping key and Flow Contexts
 * @dev: dpseci device
 * @adata: authentication algorithm details
 * @cdata: encryption algorithm details
 * @authsize: authentication tag (a.k.a. ICV / MAC) size
 * @xts_key_fallback: true if fallback tfm needs to be used due
 *		      to unsupported xts key lengths
 * @fallback: xts fallback tfm
 */
struct caam_ctx {
	struct caam_flc flc[NUM_OP];
	u8 key[CAAM_MAX_KEY_SIZE];
	dma_addr_t flc_dma[NUM_OP];
	dma_addr_t key_dma;
	enum dma_data_direction dir;
	struct device *dev;
	struct alginfo adata;
	struct alginfo cdata;
	unsigned int authsize;
	bool xts_key_fallback;
	struct crypto_skcipher *fallback;
};

static void *dpaa2_caam_iova_to_virt(struct dpaa2_caam_priv *priv,
				     dma_addr_t iova_addr)
{
	phys_addr_t phys_addr;

	phys_addr = priv->domain ? iommu_iova_to_phys(priv->domain, iova_addr) :
				   iova_addr;

	return phys_to_virt(phys_addr);
}

/*
 * qi_cache_zalloc - Allocate buffers from CAAM-QI cache
 *
 * Allocate data on the hotpath. Instead of using kzalloc, one can use the
 * services of the CAAM QI memory cache (backed by kmem_cache). The buffers
 * will have a size of CAAM_QI_MEMCACHE_SIZE, which should be sufficient for
 * hosting 16 SG entries.
 *
 * @flags - flags that would be used for the equivalent kmalloc(..) call
 *
 * Returns a pointer to a retrieved buffer on success or NULL on failure.
 */
static inline void *qi_cache_zalloc(gfp_t flags)
{
	return kmem_cache_zalloc(qi_cache, flags);
}

/*
 * qi_cache_free - Frees buffers allocated from CAAM-QI cache
 *
 * @obj - buffer previously allocated by qi_cache_zalloc
 *
 * No checking is being done, the call is a passthrough call to
 * kmem_cache_free(...)
 */
static inline void qi_cache_free(void *obj)
{
	kmem_cache_free(qi_cache, obj);
}

static struct caam_request *to_caam_req(struct crypto_async_request *areq)
{
	switch (crypto_tfm_alg_type(areq->tfm)) {
	case CRYPTO_ALG_TYPE_SKCIPHER:
		return skcipher_request_ctx_dma(skcipher_request_cast(areq));
	case CRYPTO_ALG_TYPE_AEAD:
		return aead_request_ctx_dma(
			container_of(areq, struct aead_request, base));
	case CRYPTO_ALG_TYPE_AHASH:
		return ahash_request_ctx_dma(ahash_request_cast(areq));
	default:
		return ERR_PTR(-EINVAL);
	}
}

static void caam_unmap(struct device *dev, struct scatterlist *src,
		       struct scatterlist *dst, int src_nents,
		       int dst_nents, dma_addr_t iv_dma, int ivsize,
		       enum dma_data_direction iv_dir, dma_addr_t qm_sg_dma,
		       int qm_sg_bytes)
{
	if (dst != src) {
		if (src_nents)
			dma_unmap_sg(dev, src, src_nents, DMA_TO_DEVICE);
		if (dst_nents)
			dma_unmap_sg(dev, dst, dst_nents, DMA_FROM_DEVICE);
	} else {
		dma_unmap_sg(dev, src, src_nents, DMA_BIDIRECTIONAL);
	}

	if (iv_dma)
		dma_unmap_single(dev, iv_dma, ivsize, iv_dir);

	if (qm_sg_bytes)
		dma_unmap_single(dev, qm_sg_dma, qm_sg_bytes, DMA_TO_DEVICE);
}

static int aead_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 typeof(*alg), aead);
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct device *dev = ctx->dev;
	struct dpaa2_caam_priv *priv = dev_get_drvdata(dev);
	struct caam_flc *flc;
	u32 *desc;
	u32 ctx1_iv_off = 0;
	u32 *nonce = NULL;
	unsigned int data_len[2];
	u32 inl_mask;
	const bool ctr_mode = ((ctx->cdata.algtype & OP_ALG_AAI_MASK) ==
			       OP_ALG_AAI_CTR_MOD128);
	const bool is_rfc3686 = alg->caam.rfc3686;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
	 * AES-CTR needs to load IV in CONTEXT1 reg
	 * at an offset of 128bits (16bytes)
	 * CONTEXT1[255:128] = IV
	 */
	if (ctr_mode)
		ctx1_iv_off = 16;

	/*
	 * RFC3686 specific:
	 *	CONTEXT1[255:128] = {NONCE, IV, COUNTER}
	 */
	if (is_rfc3686) {
		ctx1_iv_off = 16 + CTR_RFC3686_NONCE_SIZE;
		nonce = (u32 *)((void *)ctx->key + ctx->adata.keylen_pad +
				ctx->cdata.keylen - CTR_RFC3686_NONCE_SIZE);
	}

	/*
	 * In case |user key| > |derived key|, using DKP<imm,imm> would result
	 * in invalid opcodes (last bytes of user key) in the resulting
	 * descriptor. Use DKP<ptr,imm> instead => both virtual and dma key
	 * addresses are needed.
	 */
	ctx->adata.key_virt = ctx->key;
	ctx->adata.key_dma = ctx->key_dma;

	ctx->cdata.key_virt = ctx->key + ctx->adata.keylen_pad;
	ctx->cdata.key_dma = ctx->key_dma + ctx->adata.keylen_pad;

	data_len[0] = ctx->adata.keylen_pad;
	data_len[1] = ctx->cdata.keylen;

	/* aead_encrypt shared descriptor */
	if (desc_inline_query((alg->caam.geniv ? DESC_QI_AEAD_GIVENC_LEN :
						 DESC_QI_AEAD_ENC_LEN) +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	flc = &ctx->flc[ENCRYPT];
	desc = flc->sh_desc;

	if (alg->caam.geniv)
		cnstr_shdsc_aead_givencap(desc, &ctx->cdata, &ctx->adata,
					  ivsize, ctx->authsize, is_rfc3686,
					  nonce, ctx1_iv_off, true,
					  priv->sec_attr.era);
	else
		cnstr_shdsc_aead_encap(desc, &ctx->cdata, &ctx->adata,
				       ivsize, ctx->authsize, is_rfc3686, nonce,
				       ctx1_iv_off, true, priv->sec_attr.era);

	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[ENCRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	/* aead_decrypt shared descriptor */
	if (desc_inline_query(DESC_QI_AEAD_DEC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	flc = &ctx->flc[DECRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_aead_decap(desc, &ctx->cdata, &ctx->adata,
			       ivsize, ctx->authsize, alg->caam.geniv,
			       is_rfc3686, nonce, ctx1_iv_off, true,
			       priv->sec_attr.era);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[DECRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	return 0;
}

static int aead_setauthsize(struct crypto_aead *authenc, unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(authenc);

	ctx->authsize = authsize;
	aead_set_sh_desc(authenc);

	return 0;
}

static int aead_setkey(struct crypto_aead *aead, const u8 *key,
		       unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct device *dev = ctx->dev;
	struct crypto_authenc_keys keys;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto badkey;

	dev_dbg(dev, "keylen %d enckeylen %d authkeylen %d\n",
		keys.authkeylen + keys.enckeylen, keys.enckeylen,
		keys.authkeylen);
	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	ctx->adata.keylen = keys.authkeylen;
	ctx->adata.keylen_pad = split_key_len(ctx->adata.algtype &
					      OP_ALG_ALGSEL_MASK);

	if (ctx->adata.keylen_pad + keys.enckeylen > CAAM_MAX_KEY_SIZE)
		goto badkey;

	memcpy(ctx->key, keys.authkey, keys.authkeylen);
	memcpy(ctx->key + ctx->adata.keylen_pad, keys.enckey, keys.enckeylen);
	dma_sync_single_for_device(dev, ctx->key_dma, ctx->adata.keylen_pad +
				   keys.enckeylen, ctx->dir);
	print_hex_dump_debug("ctx.key@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, ctx->key,
			     ctx->adata.keylen_pad + keys.enckeylen, 1);

	ctx->cdata.keylen = keys.enckeylen;

	memzero_explicit(&keys, sizeof(keys));
	return aead_set_sh_desc(aead);
badkey:
	memzero_explicit(&keys, sizeof(keys));
	return -EINVAL;
}

static int des3_aead_setkey(struct crypto_aead *aead, const u8 *key,
			    unsigned int keylen)
{
	struct crypto_authenc_keys keys;
	int err;

	err = crypto_authenc_extractkeys(&keys, key, keylen);
	if (unlikely(err))
		goto out;

	err = -EINVAL;
	if (keys.enckeylen != DES3_EDE_KEY_SIZE)
		goto out;

	err = crypto_des3_ede_verify_key(crypto_aead_tfm(aead), keys.enckey) ?:
	      aead_setkey(aead, key, keylen);

out:
	memzero_explicit(&keys, sizeof(keys));
	return err;
}

static struct aead_edesc *aead_edesc_alloc(struct aead_request *req,
					   bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_request *req_ctx = aead_request_ctx_dma(req);
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 typeof(*alg), aead);
	struct device *dev = ctx->dev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents = 0, mapped_dst_nents = 0;
	int src_len, dst_len = 0;
	struct aead_edesc *edesc;
	dma_addr_t qm_sg_dma, iv_dma = 0;
	int ivsize = 0;
	unsigned int authsize = ctx->authsize;
	int qm_sg_index = 0, qm_sg_nents = 0, qm_sg_bytes;
	int in_len, out_len;
	struct dpaa2_sg_entry *sg_table;

	/* allocate space for base edesc, link tables and IV */
	edesc = qi_cache_zalloc(flags);
	if (unlikely(!edesc)) {
		dev_err(dev, "could not allocate extended descriptor\n");
		return ERR_PTR(-ENOMEM);
	}

	if (unlikely(req->dst != req->src)) {
		src_len = req->assoclen + req->cryptlen;
		dst_len = src_len + (encrypt ? authsize : (-authsize));

		src_nents = sg_nents_for_len(req->src, src_len);
		if (unlikely(src_nents < 0)) {
			dev_err(dev, "Insufficient bytes (%d) in src S/G\n",
				src_len);
			qi_cache_free(edesc);
			return ERR_PTR(src_nents);
		}

		dst_nents = sg_nents_for_len(req->dst, dst_len);
		if (unlikely(dst_nents < 0)) {
			dev_err(dev, "Insufficient bytes (%d) in dst S/G\n",
				dst_len);
			qi_cache_free(edesc);
			return ERR_PTR(dst_nents);
		}

		if (src_nents) {
			mapped_src_nents = dma_map_sg(dev, req->src, src_nents,
						      DMA_TO_DEVICE);
			if (unlikely(!mapped_src_nents)) {
				dev_err(dev, "unable to map source\n");
				qi_cache_free(edesc);
				return ERR_PTR(-ENOMEM);
			}
		} else {
			mapped_src_nents = 0;
		}

		if (dst_nents) {
			mapped_dst_nents = dma_map_sg(dev, req->dst, dst_nents,
						      DMA_FROM_DEVICE);
			if (unlikely(!mapped_dst_nents)) {
				dev_err(dev, "unable to map destination\n");
				dma_unmap_sg(dev, req->src, src_nents,
					     DMA_TO_DEVICE);
				qi_cache_free(edesc);
				return ERR_PTR(-ENOMEM);
			}
		} else {
			mapped_dst_nents = 0;
		}
	} else {
		src_len = req->assoclen + req->cryptlen +
			  (encrypt ? authsize : 0);

		src_nents = sg_nents_for_len(req->src, src_len);
		if (unlikely(src_nents < 0)) {
			dev_err(dev, "Insufficient bytes (%d) in src S/G\n",
				src_len);
			qi_cache_free(edesc);
			return ERR_PTR(src_nents);
		}

		mapped_src_nents = dma_map_sg(dev, req->src, src_nents,
					      DMA_BIDIRECTIONAL);
		if (unlikely(!mapped_src_nents)) {
			dev_err(dev, "unable to map source\n");
			qi_cache_free(edesc);
			return ERR_PTR(-ENOMEM);
		}
	}

	if ((alg->caam.rfc3686 && encrypt) || !alg->caam.geniv)
		ivsize = crypto_aead_ivsize(aead);

	/*
	 * Create S/G table: req->assoclen, [IV,] req->src [, req->dst].
	 * Input is not contiguous.
	 * HW reads 4 S/G entries at a time; make sure the reads don't go beyond
	 * the end of the table by allocating more S/G entries. Logic:
	 * if (src != dst && output S/G)
	 *      pad output S/G, if needed
	 * else if (src == dst && S/G)
	 *      overlapping S/Gs; pad one of them
	 * else if (input S/G) ...
	 *      pad input S/G, if needed
	 */
	qm_sg_nents = 1 + !!ivsize + mapped_src_nents;
	if (mapped_dst_nents > 1)
		qm_sg_nents += pad_sg_nents(mapped_dst_nents);
	else if ((req->src == req->dst) && (mapped_src_nents > 1))
		qm_sg_nents = max(pad_sg_nents(qm_sg_nents),
				  1 + !!ivsize +
				  pad_sg_nents(mapped_src_nents));
	else
		qm_sg_nents = pad_sg_nents(qm_sg_nents);

	sg_table = &edesc->sgt[0];
	qm_sg_bytes = qm_sg_nents * sizeof(*sg_table);
	if (unlikely(offsetof(struct aead_edesc, sgt) + qm_sg_bytes + ivsize >
		     CAAM_QI_MEMCACHE_SIZE)) {
		dev_err(dev, "No space for %d S/G entries and/or %dB IV\n",
			qm_sg_nents, ivsize);
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, DMA_NONE, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	if (ivsize) {
		u8 *iv = (u8 *)(sg_table + qm_sg_nents);

		/* Make sure IV is located in a DMAable area */
		memcpy(iv, req->iv, ivsize);

		iv_dma = dma_map_single(dev, iv, ivsize, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, iv_dma)) {
			dev_err(dev, "unable to map IV\n");
			caam_unmap(dev, req->src, req->dst, src_nents,
				   dst_nents, 0, 0, DMA_NONE, 0, 0);
			qi_cache_free(edesc);
			return ERR_PTR(-ENOMEM);
		}
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;

	if ((alg->caam.class1_alg_type & OP_ALG_ALGSEL_MASK) ==
	    OP_ALG_ALGSEL_CHACHA20 && ivsize != CHACHAPOLY_IV_SIZE)
		/*
		 * The associated data comes already with the IV but we need
		 * to skip it when we authenticate or encrypt...
		 */
		edesc->assoclen = cpu_to_caam32(req->assoclen - ivsize);
	else
		edesc->assoclen = cpu_to_caam32(req->assoclen);
	edesc->assoclen_dma = dma_map_single(dev, &edesc->assoclen, 4,
					     DMA_TO_DEVICE);
	if (dma_mapping_error(dev, edesc->assoclen_dma)) {
		dev_err(dev, "unable to map assoclen\n");
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, DMA_TO_DEVICE, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	dma_to_qm_sg_one(sg_table, edesc->assoclen_dma, 4, 0);
	qm_sg_index++;
	if (ivsize) {
		dma_to_qm_sg_one(sg_table + qm_sg_index, iv_dma, ivsize, 0);
		qm_sg_index++;
	}
	sg_to_qm_sg_last(req->src, src_len, sg_table + qm_sg_index, 0);
	qm_sg_index += mapped_src_nents;

	if (mapped_dst_nents > 1)
		sg_to_qm_sg_last(req->dst, dst_len, sg_table + qm_sg_index, 0);

	qm_sg_dma = dma_map_single(dev, sg_table, qm_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, qm_sg_dma)) {
		dev_err(dev, "unable to map S/G table\n");
		dma_unmap_single(dev, edesc->assoclen_dma, 4, DMA_TO_DEVICE);
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, DMA_TO_DEVICE, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	edesc->qm_sg_dma = qm_sg_dma;
	edesc->qm_sg_bytes = qm_sg_bytes;

	out_len = req->assoclen + req->cryptlen +
		  (encrypt ? ctx->authsize : (-ctx->authsize));
	in_len = 4 + ivsize + req->assoclen + req->cryptlen;

	memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
	dpaa2_fl_set_final(in_fle, true);
	dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
	dpaa2_fl_set_addr(in_fle, qm_sg_dma);
	dpaa2_fl_set_len(in_fle, in_len);

	if (req->dst == req->src) {
		if (mapped_src_nents == 1) {
			dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
			dpaa2_fl_set_addr(out_fle, sg_dma_address(req->src));
		} else {
			dpaa2_fl_set_format(out_fle, dpaa2_fl_sg);
			dpaa2_fl_set_addr(out_fle, qm_sg_dma +
					  (1 + !!ivsize) * sizeof(*sg_table));
		}
	} else if (!mapped_dst_nents) {
		/*
		 * crypto engine requires the output entry to be present when
		 * "frame list" FD is used.
		 * Since engine does not support FMT=2'b11 (unused entry type),
		 * leaving out_fle zeroized is the best option.
		 */
		goto skip_out_fle;
	} else if (mapped_dst_nents == 1) {
		dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
		dpaa2_fl_set_addr(out_fle, sg_dma_address(req->dst));
	} else {
		dpaa2_fl_set_format(out_fle, dpaa2_fl_sg);
		dpaa2_fl_set_addr(out_fle, qm_sg_dma + qm_sg_index *
				  sizeof(*sg_table));
	}

	dpaa2_fl_set_len(out_fle, out_len);

skip_out_fle:
	return edesc;
}

static int chachapoly_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct device *dev = ctx->dev;
	struct caam_flc *flc;
	u32 *desc;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	flc = &ctx->flc[ENCRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_chachapoly(desc, &ctx->cdata, &ctx->adata, ivsize,
			       ctx->authsize, true, true);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[ENCRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	flc = &ctx->flc[DECRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_chachapoly(desc, &ctx->cdata, &ctx->adata, ivsize,
			       ctx->authsize, false, true);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[DECRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	return 0;
}

static int chachapoly_setauthsize(struct crypto_aead *aead,
				  unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);

	if (authsize != POLY1305_DIGEST_SIZE)
		return -EINVAL;

	ctx->authsize = authsize;
	return chachapoly_set_sh_desc(aead);
}

static int chachapoly_setkey(struct crypto_aead *aead, const u8 *key,
			     unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	unsigned int saltlen = CHACHAPOLY_IV_SIZE - ivsize;

	if (keylen != CHACHA_KEY_SIZE + saltlen)
		return -EINVAL;

	ctx->cdata.key_virt = key;
	ctx->cdata.keylen = keylen - saltlen;

	return chachapoly_set_sh_desc(aead);
}

static int gcm_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct device *dev = ctx->dev;
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct caam_flc *flc;
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
	 * AES GCM encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_GCM_ENC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	flc = &ctx->flc[ENCRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_gcm_encap(desc, &ctx->cdata, ivsize, ctx->authsize, true);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[ENCRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_GCM_DEC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	flc = &ctx->flc[DECRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_gcm_decap(desc, &ctx->cdata, ivsize, ctx->authsize, true);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[DECRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	return 0;
}

static int gcm_setauthsize(struct crypto_aead *authenc, unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(authenc);
	int err;

	err = crypto_gcm_check_authsize(authsize);
	if (err)
		return err;

	ctx->authsize = authsize;
	gcm_set_sh_desc(authenc);

	return 0;
}

static int gcm_setkey(struct crypto_aead *aead,
		      const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct device *dev = ctx->dev;
	int ret;

	ret = aes_check_keylen(keylen);
	if (ret)
		return ret;
	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	memcpy(ctx->key, key, keylen);
	dma_sync_single_for_device(dev, ctx->key_dma, keylen, ctx->dir);
	ctx->cdata.keylen = keylen;

	return gcm_set_sh_desc(aead);
}

static int rfc4106_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct device *dev = ctx->dev;
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct caam_flc *flc;
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	ctx->cdata.key_virt = ctx->key;

	/*
	 * RFC4106 encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_RFC4106_ENC_LEN) {
		ctx->cdata.key_inline = true;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	flc = &ctx->flc[ENCRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_rfc4106_encap(desc, &ctx->cdata, ivsize, ctx->authsize,
				  true);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[ENCRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_RFC4106_DEC_LEN) {
		ctx->cdata.key_inline = true;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	flc = &ctx->flc[DECRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_rfc4106_decap(desc, &ctx->cdata, ivsize, ctx->authsize,
				  true);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[DECRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	return 0;
}

static int rfc4106_setauthsize(struct crypto_aead *authenc,
			       unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(authenc);
	int err;

	err = crypto_rfc4106_check_authsize(authsize);
	if (err)
		return err;

	ctx->authsize = authsize;
	rfc4106_set_sh_desc(authenc);

	return 0;
}

static int rfc4106_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct device *dev = ctx->dev;
	int ret;

	ret = aes_check_keylen(keylen - 4);
	if (ret)
		return ret;

	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	memcpy(ctx->key, key, keylen);
	/*
	 * The last four bytes of the key material are used as the salt value
	 * in the nonce. Update the AES key length.
	 */
	ctx->cdata.keylen = keylen - 4;
	dma_sync_single_for_device(dev, ctx->key_dma, ctx->cdata.keylen,
				   ctx->dir);

	return rfc4106_set_sh_desc(aead);
}

static int rfc4543_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct device *dev = ctx->dev;
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct caam_flc *flc;
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	ctx->cdata.key_virt = ctx->key;

	/*
	 * RFC4543 encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_RFC4543_ENC_LEN) {
		ctx->cdata.key_inline = true;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	flc = &ctx->flc[ENCRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_rfc4543_encap(desc, &ctx->cdata, ivsize, ctx->authsize,
				  true);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[ENCRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_RFC4543_DEC_LEN) {
		ctx->cdata.key_inline = true;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	flc = &ctx->flc[DECRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_rfc4543_decap(desc, &ctx->cdata, ivsize, ctx->authsize,
				  true);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[DECRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	return 0;
}

static int rfc4543_setauthsize(struct crypto_aead *authenc,
			       unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(authenc);

	if (authsize != 16)
		return -EINVAL;

	ctx->authsize = authsize;
	rfc4543_set_sh_desc(authenc);

	return 0;
}

static int rfc4543_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct device *dev = ctx->dev;
	int ret;

	ret = aes_check_keylen(keylen - 4);
	if (ret)
		return ret;

	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	memcpy(ctx->key, key, keylen);
	/*
	 * The last four bytes of the key material are used as the salt value
	 * in the nonce. Update the AES key length.
	 */
	ctx->cdata.keylen = keylen - 4;
	dma_sync_single_for_device(dev, ctx->key_dma, ctx->cdata.keylen,
				   ctx->dir);

	return rfc4543_set_sh_desc(aead);
}

static int skcipher_setkey(struct crypto_skcipher *skcipher, const u8 *key,
			   unsigned int keylen, const u32 ctx1_iv_off)
{
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(skcipher);
	struct caam_skcipher_alg *alg =
		container_of(crypto_skcipher_alg(skcipher),
			     struct caam_skcipher_alg, skcipher);
	struct device *dev = ctx->dev;
	struct caam_flc *flc;
	unsigned int ivsize = crypto_skcipher_ivsize(skcipher);
	u32 *desc;
	const bool is_rfc3686 = alg->caam.rfc3686;

	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = key;
	ctx->cdata.key_inline = true;

	/* skcipher_encrypt shared descriptor */
	flc = &ctx->flc[ENCRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_skcipher_encap(desc, &ctx->cdata, ivsize, is_rfc3686,
				   ctx1_iv_off);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[ENCRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	/* skcipher_decrypt shared descriptor */
	flc = &ctx->flc[DECRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_skcipher_decap(desc, &ctx->cdata, ivsize, is_rfc3686,
				   ctx1_iv_off);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[DECRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	return 0;
}

static int aes_skcipher_setkey(struct crypto_skcipher *skcipher,
			       const u8 *key, unsigned int keylen)
{
	int err;

	err = aes_check_keylen(keylen);
	if (err)
		return err;

	return skcipher_setkey(skcipher, key, keylen, 0);
}

static int rfc3686_skcipher_setkey(struct crypto_skcipher *skcipher,
				   const u8 *key, unsigned int keylen)
{
	u32 ctx1_iv_off;
	int err;

	/*
	 * RFC3686 specific:
	 *	| CONTEXT1[255:128] = {NONCE, IV, COUNTER}
	 *	| *key = {KEY, NONCE}
	 */
	ctx1_iv_off = 16 + CTR_RFC3686_NONCE_SIZE;
	keylen -= CTR_RFC3686_NONCE_SIZE;

	err = aes_check_keylen(keylen);
	if (err)
		return err;

	return skcipher_setkey(skcipher, key, keylen, ctx1_iv_off);
}

static int ctr_skcipher_setkey(struct crypto_skcipher *skcipher,
			       const u8 *key, unsigned int keylen)
{
	u32 ctx1_iv_off;
	int err;

	/*
	 * AES-CTR needs to load IV in CONTEXT1 reg
	 * at an offset of 128bits (16bytes)
	 * CONTEXT1[255:128] = IV
	 */
	ctx1_iv_off = 16;

	err = aes_check_keylen(keylen);
	if (err)
		return err;

	return skcipher_setkey(skcipher, key, keylen, ctx1_iv_off);
}

static int chacha20_skcipher_setkey(struct crypto_skcipher *skcipher,
				    const u8 *key, unsigned int keylen)
{
	if (keylen != CHACHA_KEY_SIZE)
		return -EINVAL;

	return skcipher_setkey(skcipher, key, keylen, 0);
}

static int des_skcipher_setkey(struct crypto_skcipher *skcipher,
			       const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des_key(skcipher, key) ?:
	       skcipher_setkey(skcipher, key, keylen, 0);
}

static int des3_skcipher_setkey(struct crypto_skcipher *skcipher,
			        const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des3_key(skcipher, key) ?:
	       skcipher_setkey(skcipher, key, keylen, 0);
}

static int xts_skcipher_setkey(struct crypto_skcipher *skcipher, const u8 *key,
			       unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(skcipher);
	struct device *dev = ctx->dev;
	struct dpaa2_caam_priv *priv = dev_get_drvdata(dev);
	struct caam_flc *flc;
	u32 *desc;
	int err;

	err = xts_verify_key(skcipher, key, keylen);
	if (err) {
		dev_dbg(dev, "key size mismatch\n");
		return err;
	}

	if (keylen != 2 * AES_KEYSIZE_128 && keylen != 2 * AES_KEYSIZE_256)
		ctx->xts_key_fallback = true;

	if (priv->sec_attr.era <= 8 || ctx->xts_key_fallback) {
		err = crypto_skcipher_setkey(ctx->fallback, key, keylen);
		if (err)
			return err;
	}

	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = key;
	ctx->cdata.key_inline = true;

	/* xts_skcipher_encrypt shared descriptor */
	flc = &ctx->flc[ENCRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_xts_skcipher_encap(desc, &ctx->cdata);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[ENCRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	/* xts_skcipher_decrypt shared descriptor */
	flc = &ctx->flc[DECRYPT];
	desc = flc->sh_desc;
	cnstr_shdsc_xts_skcipher_decap(desc, &ctx->cdata);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(dev, ctx->flc_dma[DECRYPT],
				   sizeof(flc->flc) + desc_bytes(desc),
				   ctx->dir);

	return 0;
}

static struct skcipher_edesc *skcipher_edesc_alloc(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_request *req_ctx = skcipher_request_ctx_dma(req);
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(skcipher);
	struct device *dev = ctx->dev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents = 0, mapped_dst_nents = 0;
	struct skcipher_edesc *edesc;
	dma_addr_t iv_dma;
	u8 *iv;
	int ivsize = crypto_skcipher_ivsize(skcipher);
	int dst_sg_idx, qm_sg_ents, qm_sg_bytes;
	struct dpaa2_sg_entry *sg_table;

	src_nents = sg_nents_for_len(req->src, req->cryptlen);
	if (unlikely(src_nents < 0)) {
		dev_err(dev, "Insufficient bytes (%d) in src S/G\n",
			req->cryptlen);
		return ERR_PTR(src_nents);
	}

	if (unlikely(req->dst != req->src)) {
		dst_nents = sg_nents_for_len(req->dst, req->cryptlen);
		if (unlikely(dst_nents < 0)) {
			dev_err(dev, "Insufficient bytes (%d) in dst S/G\n",
				req->cryptlen);
			return ERR_PTR(dst_nents);
		}

		mapped_src_nents = dma_map_sg(dev, req->src, src_nents,
					      DMA_TO_DEVICE);
		if (unlikely(!mapped_src_nents)) {
			dev_err(dev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}

		mapped_dst_nents = dma_map_sg(dev, req->dst, dst_nents,
					      DMA_FROM_DEVICE);
		if (unlikely(!mapped_dst_nents)) {
			dev_err(dev, "unable to map destination\n");
			dma_unmap_sg(dev, req->src, src_nents, DMA_TO_DEVICE);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		mapped_src_nents = dma_map_sg(dev, req->src, src_nents,
					      DMA_BIDIRECTIONAL);
		if (unlikely(!mapped_src_nents)) {
			dev_err(dev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	qm_sg_ents = 1 + mapped_src_nents;
	dst_sg_idx = qm_sg_ents;

	/*
	 * Input, output HW S/G tables: [IV, src][dst, IV]
	 * IV entries point to the same buffer
	 * If src == dst, S/G entries are reused (S/G tables overlap)
	 *
	 * HW reads 4 S/G entries at a time; make sure the reads don't go beyond
	 * the end of the table by allocating more S/G entries.
	 */
	if (req->src != req->dst)
		qm_sg_ents += pad_sg_nents(mapped_dst_nents + 1);
	else
		qm_sg_ents = 1 + pad_sg_nents(qm_sg_ents);

	qm_sg_bytes = qm_sg_ents * sizeof(struct dpaa2_sg_entry);
	if (unlikely(offsetof(struct skcipher_edesc, sgt) + qm_sg_bytes +
		     ivsize > CAAM_QI_MEMCACHE_SIZE)) {
		dev_err(dev, "No space for %d S/G entries and/or %dB IV\n",
			qm_sg_ents, ivsize);
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, DMA_NONE, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	/* allocate space for base edesc, link tables and IV */
	edesc = qi_cache_zalloc(flags);
	if (unlikely(!edesc)) {
		dev_err(dev, "could not allocate extended descriptor\n");
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, DMA_NONE, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	/* Make sure IV is located in a DMAable area */
	sg_table = &edesc->sgt[0];
	iv = (u8 *)(sg_table + qm_sg_ents);
	memcpy(iv, req->iv, ivsize);

	iv_dma = dma_map_single(dev, iv, ivsize, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, iv_dma)) {
		dev_err(dev, "unable to map IV\n");
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, DMA_NONE, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;
	edesc->qm_sg_bytes = qm_sg_bytes;

	dma_to_qm_sg_one(sg_table, iv_dma, ivsize, 0);
	sg_to_qm_sg(req->src, req->cryptlen, sg_table + 1, 0);

	if (req->src != req->dst)
		sg_to_qm_sg(req->dst, req->cryptlen, sg_table + dst_sg_idx, 0);

	dma_to_qm_sg_one(sg_table + dst_sg_idx + mapped_dst_nents, iv_dma,
			 ivsize, 0);

	edesc->qm_sg_dma = dma_map_single(dev, sg_table, edesc->qm_sg_bytes,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, edesc->qm_sg_dma)) {
		dev_err(dev, "unable to map S/G table\n");
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, DMA_BIDIRECTIONAL, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
	dpaa2_fl_set_final(in_fle, true);
	dpaa2_fl_set_len(in_fle, req->cryptlen + ivsize);
	dpaa2_fl_set_len(out_fle, req->cryptlen + ivsize);

	dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
	dpaa2_fl_set_addr(in_fle, edesc->qm_sg_dma);

	dpaa2_fl_set_format(out_fle, dpaa2_fl_sg);

	if (req->src == req->dst)
		dpaa2_fl_set_addr(out_fle, edesc->qm_sg_dma +
				  sizeof(*sg_table));
	else
		dpaa2_fl_set_addr(out_fle, edesc->qm_sg_dma + dst_sg_idx *
				  sizeof(*sg_table));

	return edesc;
}

static void aead_unmap(struct device *dev, struct aead_edesc *edesc,
		       struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	int ivsize = crypto_aead_ivsize(aead);

	caam_unmap(dev, req->src, req->dst, edesc->src_nents, edesc->dst_nents,
		   edesc->iv_dma, ivsize, DMA_TO_DEVICE, edesc->qm_sg_dma,
		   edesc->qm_sg_bytes);
	dma_unmap_single(dev, edesc->assoclen_dma, 4, DMA_TO_DEVICE);
}

static void skcipher_unmap(struct device *dev, struct skcipher_edesc *edesc,
			   struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	int ivsize = crypto_skcipher_ivsize(skcipher);

	caam_unmap(dev, req->src, req->dst, edesc->src_nents, edesc->dst_nents,
		   edesc->iv_dma, ivsize, DMA_BIDIRECTIONAL, edesc->qm_sg_dma,
		   edesc->qm_sg_bytes);
}

static void aead_encrypt_done(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct aead_request *req = container_of(areq, struct aead_request,
						base);
	struct caam_request *req_ctx = to_caam_req(areq);
	struct aead_edesc *edesc = req_ctx->edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	int ecode = 0;

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status))
		ecode = caam_qi2_strstatus(ctx->dev, status);

	aead_unmap(ctx->dev, edesc, req);
	qi_cache_free(edesc);
	aead_request_complete(req, ecode);
}

static void aead_decrypt_done(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct aead_request *req = container_of(areq, struct aead_request,
						base);
	struct caam_request *req_ctx = to_caam_req(areq);
	struct aead_edesc *edesc = req_ctx->edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	int ecode = 0;

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status))
		ecode = caam_qi2_strstatus(ctx->dev, status);

	aead_unmap(ctx->dev, edesc, req);
	qi_cache_free(edesc);
	aead_request_complete(req, ecode);
}

static int aead_encrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct caam_request *caam_req = aead_request_ctx_dma(req);
	int ret;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, true);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	caam_req->flc = &ctx->flc[ENCRYPT];
	caam_req->flc_dma = ctx->flc_dma[ENCRYPT];
	caam_req->cbk = aead_encrypt_done;
	caam_req->ctx = &req->base;
	caam_req->edesc = edesc;
	ret = dpaa2_caam_enqueue(ctx->dev, caam_req);
	if (ret != -EINPROGRESS &&
	    !(ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
		aead_unmap(ctx->dev, edesc, req);
		qi_cache_free(edesc);
	}

	return ret;
}

static int aead_decrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx_dma(aead);
	struct caam_request *caam_req = aead_request_ctx_dma(req);
	int ret;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, false);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	caam_req->flc = &ctx->flc[DECRYPT];
	caam_req->flc_dma = ctx->flc_dma[DECRYPT];
	caam_req->cbk = aead_decrypt_done;
	caam_req->ctx = &req->base;
	caam_req->edesc = edesc;
	ret = dpaa2_caam_enqueue(ctx->dev, caam_req);
	if (ret != -EINPROGRESS &&
	    !(ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
		aead_unmap(ctx->dev, edesc, req);
		qi_cache_free(edesc);
	}

	return ret;
}

static int ipsec_gcm_encrypt(struct aead_request *req)
{
	return crypto_ipsec_check_assoclen(req->assoclen) ? : aead_encrypt(req);
}

static int ipsec_gcm_decrypt(struct aead_request *req)
{
	return crypto_ipsec_check_assoclen(req->assoclen) ? : aead_decrypt(req);
}

static void skcipher_encrypt_done(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct skcipher_request *req = skcipher_request_cast(areq);
	struct caam_request *req_ctx = to_caam_req(areq);
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(skcipher);
	struct skcipher_edesc *edesc = req_ctx->edesc;
	int ecode = 0;
	int ivsize = crypto_skcipher_ivsize(skcipher);

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status))
		ecode = caam_qi2_strstatus(ctx->dev, status);

	print_hex_dump_debug("dstiv  @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, req->iv,
			     edesc->src_nents > 1 ? 100 : ivsize, 1);
	caam_dump_sg("dst    @" __stringify(__LINE__)": ",
		     DUMP_PREFIX_ADDRESS, 16, 4, req->dst,
		     edesc->dst_nents > 1 ? 100 : req->cryptlen, 1);

	skcipher_unmap(ctx->dev, edesc, req);

	/*
	 * The crypto API expects us to set the IV (req->iv) to the last
	 * ciphertext block (CBC mode) or last counter (CTR mode).
	 * This is used e.g. by the CTS mode.
	 */
	if (!ecode)
		memcpy(req->iv, (u8 *)&edesc->sgt[0] + edesc->qm_sg_bytes,
		       ivsize);

	qi_cache_free(edesc);
	skcipher_request_complete(req, ecode);
}

static void skcipher_decrypt_done(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct skcipher_request *req = skcipher_request_cast(areq);
	struct caam_request *req_ctx = to_caam_req(areq);
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(skcipher);
	struct skcipher_edesc *edesc = req_ctx->edesc;
	int ecode = 0;
	int ivsize = crypto_skcipher_ivsize(skcipher);

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status))
		ecode = caam_qi2_strstatus(ctx->dev, status);

	print_hex_dump_debug("dstiv  @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, req->iv,
			     edesc->src_nents > 1 ? 100 : ivsize, 1);
	caam_dump_sg("dst    @" __stringify(__LINE__)": ",
		     DUMP_PREFIX_ADDRESS, 16, 4, req->dst,
		     edesc->dst_nents > 1 ? 100 : req->cryptlen, 1);

	skcipher_unmap(ctx->dev, edesc, req);

	/*
	 * The crypto API expects us to set the IV (req->iv) to the last
	 * ciphertext block (CBC mode) or last counter (CTR mode).
	 * This is used e.g. by the CTS mode.
	 */
	if (!ecode)
		memcpy(req->iv, (u8 *)&edesc->sgt[0] + edesc->qm_sg_bytes,
		       ivsize);

	qi_cache_free(edesc);
	skcipher_request_complete(req, ecode);
}

static inline bool xts_skcipher_ivsize(struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	unsigned int ivsize = crypto_skcipher_ivsize(skcipher);

	return !!get_unaligned((u64 *)(req->iv + (ivsize / 2)));
}

static int skcipher_encrypt(struct skcipher_request *req)
{
	struct skcipher_edesc *edesc;
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(skcipher);
	struct caam_request *caam_req = skcipher_request_ctx_dma(req);
	struct dpaa2_caam_priv *priv = dev_get_drvdata(ctx->dev);
	int ret;

	/*
	 * XTS is expected to return an error even for input length = 0
	 * Note that the case input length < block size will be caught during
	 * HW offloading and return an error.
	 */
	if (!req->cryptlen && !ctx->fallback)
		return 0;

	if (ctx->fallback && ((priv->sec_attr.era <= 8 && xts_skcipher_ivsize(req)) ||
			      ctx->xts_key_fallback)) {
		skcipher_request_set_tfm(&caam_req->fallback_req, ctx->fallback);
		skcipher_request_set_callback(&caam_req->fallback_req,
					      req->base.flags,
					      req->base.complete,
					      req->base.data);
		skcipher_request_set_crypt(&caam_req->fallback_req, req->src,
					   req->dst, req->cryptlen, req->iv);

		return crypto_skcipher_encrypt(&caam_req->fallback_req);
	}

	/* allocate extended descriptor */
	edesc = skcipher_edesc_alloc(req);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	caam_req->flc = &ctx->flc[ENCRYPT];
	caam_req->flc_dma = ctx->flc_dma[ENCRYPT];
	caam_req->cbk = skcipher_encrypt_done;
	caam_req->ctx = &req->base;
	caam_req->edesc = edesc;
	ret = dpaa2_caam_enqueue(ctx->dev, caam_req);
	if (ret != -EINPROGRESS &&
	    !(ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
		skcipher_unmap(ctx->dev, edesc, req);
		qi_cache_free(edesc);
	}

	return ret;
}

static int skcipher_decrypt(struct skcipher_request *req)
{
	struct skcipher_edesc *edesc;
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(skcipher);
	struct caam_request *caam_req = skcipher_request_ctx_dma(req);
	struct dpaa2_caam_priv *priv = dev_get_drvdata(ctx->dev);
	int ret;

	/*
	 * XTS is expected to return an error even for input length = 0
	 * Note that the case input length < block size will be caught during
	 * HW offloading and return an error.
	 */
	if (!req->cryptlen && !ctx->fallback)
		return 0;

	if (ctx->fallback && ((priv->sec_attr.era <= 8 && xts_skcipher_ivsize(req)) ||
			      ctx->xts_key_fallback)) {
		skcipher_request_set_tfm(&caam_req->fallback_req, ctx->fallback);
		skcipher_request_set_callback(&caam_req->fallback_req,
					      req->base.flags,
					      req->base.complete,
					      req->base.data);
		skcipher_request_set_crypt(&caam_req->fallback_req, req->src,
					   req->dst, req->cryptlen, req->iv);

		return crypto_skcipher_decrypt(&caam_req->fallback_req);
	}

	/* allocate extended descriptor */
	edesc = skcipher_edesc_alloc(req);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	caam_req->flc = &ctx->flc[DECRYPT];
	caam_req->flc_dma = ctx->flc_dma[DECRYPT];
	caam_req->cbk = skcipher_decrypt_done;
	caam_req->ctx = &req->base;
	caam_req->edesc = edesc;
	ret = dpaa2_caam_enqueue(ctx->dev, caam_req);
	if (ret != -EINPROGRESS &&
	    !(ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
		skcipher_unmap(ctx->dev, edesc, req);
		qi_cache_free(edesc);
	}

	return ret;
}

static int caam_cra_init(struct caam_ctx *ctx, struct caam_alg_entry *caam,
			 bool uses_dkp)
{
	dma_addr_t dma_addr;
	int i;

	/* copy descriptor header template value */
	ctx->cdata.algtype = OP_TYPE_CLASS1_ALG | caam->class1_alg_type;
	ctx->adata.algtype = OP_TYPE_CLASS2_ALG | caam->class2_alg_type;

	ctx->dev = caam->dev;
	ctx->dir = uses_dkp ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE;

	dma_addr = dma_map_single_attrs(ctx->dev, ctx->flc,
					offsetof(struct caam_ctx, flc_dma),
					ctx->dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(ctx->dev, dma_addr)) {
		dev_err(ctx->dev, "unable to map key, shared descriptors\n");
		return -ENOMEM;
	}

	for (i = 0; i < NUM_OP; i++)
		ctx->flc_dma[i] = dma_addr + i * sizeof(ctx->flc[i]);
	ctx->key_dma = dma_addr + NUM_OP * sizeof(ctx->flc[0]);

	return 0;
}

static int caam_cra_init_skcipher(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct caam_skcipher_alg *caam_alg =
		container_of(alg, typeof(*caam_alg), skcipher);
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(tfm);
	u32 alg_aai = caam_alg->caam.class1_alg_type & OP_ALG_AAI_MASK;
	int ret = 0;

	if (alg_aai == OP_ALG_AAI_XTS) {
		const char *tfm_name = crypto_tfm_alg_name(&tfm->base);
		struct crypto_skcipher *fallback;

		fallback = crypto_alloc_skcipher(tfm_name, 0,
						 CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(fallback)) {
			dev_err(caam_alg->caam.dev,
				"Failed to allocate %s fallback: %ld\n",
				tfm_name, PTR_ERR(fallback));
			return PTR_ERR(fallback);
		}

		ctx->fallback = fallback;
		crypto_skcipher_set_reqsize_dma(
			tfm, sizeof(struct caam_request) +
			     crypto_skcipher_reqsize(fallback));
	} else {
		crypto_skcipher_set_reqsize_dma(tfm,
						sizeof(struct caam_request));
	}

	ret = caam_cra_init(ctx, &caam_alg->caam, false);
	if (ret && ctx->fallback)
		crypto_free_skcipher(ctx->fallback);

	return ret;
}

static int caam_cra_init_aead(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct caam_aead_alg *caam_alg = container_of(alg, typeof(*caam_alg),
						      aead);

	crypto_aead_set_reqsize_dma(tfm, sizeof(struct caam_request));
	return caam_cra_init(crypto_aead_ctx_dma(tfm), &caam_alg->caam,
			     !caam_alg->caam.nodkp);
}

static void caam_exit_common(struct caam_ctx *ctx)
{
	dma_unmap_single_attrs(ctx->dev, ctx->flc_dma[0],
			       offsetof(struct caam_ctx, flc_dma), ctx->dir,
			       DMA_ATTR_SKIP_CPU_SYNC);
}

static void caam_cra_exit(struct crypto_skcipher *tfm)
{
	struct caam_ctx *ctx = crypto_skcipher_ctx_dma(tfm);

	if (ctx->fallback)
		crypto_free_skcipher(ctx->fallback);
	caam_exit_common(ctx);
}

static void caam_cra_exit_aead(struct crypto_aead *tfm)
{
	caam_exit_common(crypto_aead_ctx_dma(tfm));
}

static struct caam_skcipher_alg driver_algs[] = {
	{
		.skcipher = {
			.base = {
				.cra_name = "cbc(aes)",
				.cra_driver_name = "cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aes_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "cbc(des3_ede)",
				.cra_driver_name = "cbc-3des-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = DES3_EDE_BLOCK_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "cbc(des)",
				.cra_driver_name = "cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = des_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = DES_BLOCK_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "ctr(aes)",
				.cra_driver_name = "ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = ctr_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			.chunksize = AES_BLOCK_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_AES |
					OP_ALG_AAI_CTR_MOD128,
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "rfc3686(ctr(aes))",
				.cra_driver_name = "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = rfc3686_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE +
				       CTR_RFC3686_NONCE_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE +
				       CTR_RFC3686_NONCE_SIZE,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.chunksize = AES_BLOCK_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.rfc3686 = true,
		},
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "xts(aes)",
				.cra_driver_name = "xts-aes-caam-qi2",
				.cra_flags = CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = xts_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = 2 * AES_MIN_KEY_SIZE,
			.max_keysize = 2 * AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_XTS,
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "chacha20",
				.cra_driver_name = "chacha20-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = chacha20_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = CHACHA_KEY_SIZE,
			.max_keysize = CHACHA_KEY_SIZE,
			.ivsize = CHACHA_IV_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_CHACHA20,
	},
};

static struct caam_aead_alg driver_aeads[] = {
	{
		.aead = {
			.base = {
				.cra_name = "rfc4106(gcm(aes))",
				.cra_driver_name = "rfc4106-gcm-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = rfc4106_setkey,
			.setauthsize = rfc4106_setauthsize,
			.encrypt = ipsec_gcm_encrypt,
			.decrypt = ipsec_gcm_decrypt,
			.ivsize = 8,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_GCM,
			.nodkp = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "rfc4543(gcm(aes))",
				.cra_driver_name = "rfc4543-gcm-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = rfc4543_setkey,
			.setauthsize = rfc4543_setauthsize,
			.encrypt = ipsec_gcm_encrypt,
			.decrypt = ipsec_gcm_decrypt,
			.ivsize = 8,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_GCM,
			.nodkp = true,
		},
	},
	/* Galois Counter Mode */
	{
		.aead = {
			.base = {
				.cra_name = "gcm(aes)",
				.cra_driver_name = "gcm-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = gcm_setkey,
			.setauthsize = gcm_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = 12,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_GCM,
			.nodkp = true,
		}
	},
	/* single-pass ipsec_esp descriptor */
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(aes))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(md5),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-hmac-md5-"
						   "cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha1),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha1-cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha224),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha224-cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha256),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha256-cbc-aes-"
						   "caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha384),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha384-cbc-aes-"
						   "caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-aes-caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha512),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha512-cbc-aes-"
						   "caam-qi2",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(md5),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-hmac-md5-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha1),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha1-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha224),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha224-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha256),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha256-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha384),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha384-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha512),"
					    "cbc(des3_ede)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha512-"
						   "cbc-des3_ede-caam-qi2",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(des))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(md5),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-hmac-md5-"
						   "cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha1),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha1-cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha224),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha224-cbc-des-"
						   "caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha256),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha256-cbc-des-"
						   "caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha384),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha384-cbc-des-"
						   "caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-des-caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha512),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha512-cbc-des-"
						   "caam-qi2",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = DES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.geniv = true,
		}
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc("
					    "hmac(md5),rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-md5-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc("
					    "hmac(sha1),rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha1-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc("
					    "hmac(sha224),rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha224-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc(hmac(sha256),"
					    "rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha256-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc(hmac(sha384),"
					    "rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha384-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "rfc7539(chacha20,poly1305)",
				.cra_driver_name = "rfc7539-chacha20-poly1305-"
						   "caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = chachapoly_setkey,
			.setauthsize = chachapoly_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CHACHAPOLY_IV_SIZE,
			.maxauthsize = POLY1305_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_CHACHA20 |
					   OP_ALG_AAI_AEAD,
			.class2_alg_type = OP_ALG_ALGSEL_POLY1305 |
					   OP_ALG_AAI_AEAD,
			.nodkp = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "rfc7539esp(chacha20,poly1305)",
				.cra_driver_name = "rfc7539esp-chacha20-"
						   "poly1305-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = chachapoly_setkey,
			.setauthsize = chachapoly_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = 8,
			.maxauthsize = POLY1305_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_CHACHA20 |
					   OP_ALG_AAI_AEAD,
			.class2_alg_type = OP_ALG_ALGSEL_POLY1305 |
					   OP_ALG_AAI_AEAD,
			.nodkp = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "seqiv(authenc(hmac(sha512),"
					    "rfc3686(ctr(aes))))",
				.cra_driver_name = "seqiv-authenc-hmac-sha512-"
						   "rfc3686-ctr-aes-caam-qi2",
				.cra_blocksize = 1,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES |
					   OP_ALG_AAI_CTR_MOD128,
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
			.rfc3686 = true,
			.geniv = true,
		},
	},
};

static void caam_skcipher_alg_init(struct caam_skcipher_alg *t_alg)
{
	struct skcipher_alg *alg = &t_alg->skcipher;

	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = CAAM_CRA_PRIORITY;
	alg->base.cra_ctxsize = sizeof(struct caam_ctx) + crypto_dma_padding();
	alg->base.cra_flags |= (CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
			      CRYPTO_ALG_KERN_DRIVER_ONLY);

	alg->init = caam_cra_init_skcipher;
	alg->exit = caam_cra_exit;
}

static void caam_aead_alg_init(struct caam_aead_alg *t_alg)
{
	struct aead_alg *alg = &t_alg->aead;

	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = CAAM_CRA_PRIORITY;
	alg->base.cra_ctxsize = sizeof(struct caam_ctx) + crypto_dma_padding();
	alg->base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
			      CRYPTO_ALG_KERN_DRIVER_ONLY;

	alg->init = caam_cra_init_aead;
	alg->exit = caam_cra_exit_aead;
}

/* max hash key is max split key size */
#define CAAM_MAX_HASH_KEY_SIZE		(SHA512_DIGEST_SIZE * 2)

#define CAAM_MAX_HASH_BLOCK_SIZE	SHA512_BLOCK_SIZE

/* caam context sizes for hashes: running digest + 8 */
#define HASH_MSG_LEN			8
#define MAX_CTX_LEN			(HASH_MSG_LEN + SHA512_DIGEST_SIZE)

enum hash_optype {
	UPDATE = 0,
	UPDATE_FIRST,
	FINALIZE,
	DIGEST,
	HASH_NUM_OP
};

/**
 * struct caam_hash_ctx - ahash per-session context
 * @flc: Flow Contexts array
 * @key: authentication key
 * @flc_dma: I/O virtual addresses of the Flow Contexts
 * @dev: dpseci device
 * @ctx_len: size of Context Register
 * @adata: hashing algorithm details
 */
struct caam_hash_ctx {
	struct caam_flc flc[HASH_NUM_OP];
	u8 key[CAAM_MAX_HASH_BLOCK_SIZE] ____cacheline_aligned;
	dma_addr_t flc_dma[HASH_NUM_OP];
	struct device *dev;
	int ctx_len;
	struct alginfo adata;
};

/* ahash state */
struct caam_hash_state {
	struct caam_request caam_req;
	dma_addr_t buf_dma;
	dma_addr_t ctx_dma;
	int ctx_dma_len;
	u8 buf[CAAM_MAX_HASH_BLOCK_SIZE] ____cacheline_aligned;
	int buflen;
	int next_buflen;
	u8 caam_ctx[MAX_CTX_LEN] ____cacheline_aligned;
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
};

struct caam_export_state {
	u8 buf[CAAM_MAX_HASH_BLOCK_SIZE];
	u8 caam_ctx[MAX_CTX_LEN];
	int buflen;
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
};

/* Map current buffer in state (if length > 0) and put it in link table */
static inline int buf_map_to_qm_sg(struct device *dev,
				   struct dpaa2_sg_entry *qm_sg,
				   struct caam_hash_state *state)
{
	int buflen = state->buflen;

	if (!buflen)
		return 0;

	state->buf_dma = dma_map_single(dev, state->buf, buflen,
					DMA_TO_DEVICE);
	if (dma_mapping_error(dev, state->buf_dma)) {
		dev_err(dev, "unable to map buf\n");
		state->buf_dma = 0;
		return -ENOMEM;
	}

	dma_to_qm_sg_one(qm_sg, state->buf_dma, buflen, 0);

	return 0;
}

/* Map state->caam_ctx, and add it to link table */
static inline int ctx_map_to_qm_sg(struct device *dev,
				   struct caam_hash_state *state, int ctx_len,
				   struct dpaa2_sg_entry *qm_sg, u32 flag)
{
	state->ctx_dma_len = ctx_len;
	state->ctx_dma = dma_map_single(dev, state->caam_ctx, ctx_len, flag);
	if (dma_mapping_error(dev, state->ctx_dma)) {
		dev_err(dev, "unable to map ctx\n");
		state->ctx_dma = 0;
		return -ENOMEM;
	}

	dma_to_qm_sg_one(qm_sg, state->ctx_dma, ctx_len, 0);

	return 0;
}

static int ahash_set_sh_desc(struct crypto_ahash *ahash)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	int digestsize = crypto_ahash_digestsize(ahash);
	struct dpaa2_caam_priv *priv = dev_get_drvdata(ctx->dev);
	struct caam_flc *flc;
	u32 *desc;

	/* ahash_update shared descriptor */
	flc = &ctx->flc[UPDATE];
	desc = flc->sh_desc;
	cnstr_shdsc_ahash(desc, &ctx->adata, OP_ALG_AS_UPDATE, ctx->ctx_len,
			  ctx->ctx_len, true, priv->sec_attr.era);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(ctx->dev, ctx->flc_dma[UPDATE],
				   desc_bytes(desc), DMA_BIDIRECTIONAL);
	print_hex_dump_debug("ahash update shdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	/* ahash_update_first shared descriptor */
	flc = &ctx->flc[UPDATE_FIRST];
	desc = flc->sh_desc;
	cnstr_shdsc_ahash(desc, &ctx->adata, OP_ALG_AS_INIT, ctx->ctx_len,
			  ctx->ctx_len, false, priv->sec_attr.era);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(ctx->dev, ctx->flc_dma[UPDATE_FIRST],
				   desc_bytes(desc), DMA_BIDIRECTIONAL);
	print_hex_dump_debug("ahash update first shdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	/* ahash_final shared descriptor */
	flc = &ctx->flc[FINALIZE];
	desc = flc->sh_desc;
	cnstr_shdsc_ahash(desc, &ctx->adata, OP_ALG_AS_FINALIZE, digestsize,
			  ctx->ctx_len, true, priv->sec_attr.era);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(ctx->dev, ctx->flc_dma[FINALIZE],
				   desc_bytes(desc), DMA_BIDIRECTIONAL);
	print_hex_dump_debug("ahash final shdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	/* ahash_digest shared descriptor */
	flc = &ctx->flc[DIGEST];
	desc = flc->sh_desc;
	cnstr_shdsc_ahash(desc, &ctx->adata, OP_ALG_AS_INITFINAL, digestsize,
			  ctx->ctx_len, false, priv->sec_attr.era);
	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	dma_sync_single_for_device(ctx->dev, ctx->flc_dma[DIGEST],
				   desc_bytes(desc), DMA_BIDIRECTIONAL);
	print_hex_dump_debug("ahash digest shdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	return 0;
}

struct split_key_sh_result {
	struct completion completion;
	int err;
	struct device *dev;
};

static void split_key_sh_done(void *cbk_ctx, u32 err)
{
	struct split_key_sh_result *res = cbk_ctx;

	dev_dbg(res->dev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	res->err = err ? caam_qi2_strstatus(res->dev, err) : 0;
	complete(&res->completion);
}

/* Digest hash size if it is too large */
static int hash_digest_key(struct caam_hash_ctx *ctx, u32 *keylen, u8 *key,
			   u32 digestsize)
{
	struct caam_request *req_ctx;
	u32 *desc;
	struct split_key_sh_result result;
	dma_addr_t key_dma;
	struct caam_flc *flc;
	dma_addr_t flc_dma;
	int ret = -ENOMEM;
	struct dpaa2_fl_entry *in_fle, *out_fle;

	req_ctx = kzalloc(sizeof(*req_ctx), GFP_KERNEL);
	if (!req_ctx)
		return -ENOMEM;

	in_fle = &req_ctx->fd_flt[1];
	out_fle = &req_ctx->fd_flt[0];

	flc = kzalloc(sizeof(*flc), GFP_KERNEL);
	if (!flc)
		goto err_flc;

	key_dma = dma_map_single(ctx->dev, key, *keylen, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(ctx->dev, key_dma)) {
		dev_err(ctx->dev, "unable to map key memory\n");
		goto err_key_dma;
	}

	desc = flc->sh_desc;

	init_sh_desc(desc, 0);

	/* descriptor to perform unkeyed hash on key_in */
	append_operation(desc, ctx->adata.algtype | OP_ALG_ENCRYPT |
			 OP_ALG_AS_INITFINAL);
	append_seq_fifo_load(desc, *keylen, FIFOLD_CLASS_CLASS2 |
			     FIFOLD_TYPE_LAST2 | FIFOLD_TYPE_MSG);
	append_seq_store(desc, digestsize, LDST_CLASS_2_CCB |
			 LDST_SRCDST_BYTE_CONTEXT);

	flc->flc[1] = cpu_to_caam32(desc_len(desc)); /* SDL */
	flc_dma = dma_map_single(ctx->dev, flc, sizeof(flc->flc) +
				 desc_bytes(desc), DMA_TO_DEVICE);
	if (dma_mapping_error(ctx->dev, flc_dma)) {
		dev_err(ctx->dev, "unable to map shared descriptor\n");
		goto err_flc_dma;
	}

	dpaa2_fl_set_final(in_fle, true);
	dpaa2_fl_set_format(in_fle, dpaa2_fl_single);
	dpaa2_fl_set_addr(in_fle, key_dma);
	dpaa2_fl_set_len(in_fle, *keylen);
	dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
	dpaa2_fl_set_addr(out_fle, key_dma);
	dpaa2_fl_set_len(out_fle, digestsize);

	print_hex_dump_debug("key_in@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, *keylen, 1);
	print_hex_dump_debug("shdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	result.err = 0;
	init_completion(&result.completion);
	result.dev = ctx->dev;

	req_ctx->flc = flc;
	req_ctx->flc_dma = flc_dma;
	req_ctx->cbk = split_key_sh_done;
	req_ctx->ctx = &result;

	ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
	if (ret == -EINPROGRESS) {
		/* in progress */
		wait_for_completion(&result.completion);
		ret = result.err;
		print_hex_dump_debug("digested key@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, key,
				     digestsize, 1);
	}

	dma_unmap_single(ctx->dev, flc_dma, sizeof(flc->flc) + desc_bytes(desc),
			 DMA_TO_DEVICE);
err_flc_dma:
	dma_unmap_single(ctx->dev, key_dma, *keylen, DMA_BIDIRECTIONAL);
err_key_dma:
	kfree(flc);
err_flc:
	kfree(req_ctx);

	*keylen = digestsize;

	return ret;
}

static int ahash_setkey(struct crypto_ahash *ahash, const u8 *key,
			unsigned int keylen)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	unsigned int blocksize = crypto_tfm_alg_blocksize(&ahash->base);
	unsigned int digestsize = crypto_ahash_digestsize(ahash);
	int ret;
	u8 *hashed_key = NULL;

	dev_dbg(ctx->dev, "keylen %d blocksize %d\n", keylen, blocksize);

	if (keylen > blocksize) {
		unsigned int aligned_len =
			ALIGN(keylen, dma_get_cache_alignment());

		if (aligned_len < keylen)
			return -EOVERFLOW;

		hashed_key = kmemdup(key, aligned_len, GFP_KERNEL);
		if (!hashed_key)
			return -ENOMEM;
		ret = hash_digest_key(ctx, &keylen, hashed_key, digestsize);
		if (ret)
			goto bad_free_key;
		key = hashed_key;
	}

	ctx->adata.keylen = keylen;
	ctx->adata.keylen_pad = split_key_len(ctx->adata.algtype &
					      OP_ALG_ALGSEL_MASK);
	if (ctx->adata.keylen_pad > CAAM_MAX_HASH_KEY_SIZE)
		goto bad_free_key;

	ctx->adata.key_virt = key;
	ctx->adata.key_inline = true;

	/*
	 * In case |user key| > |derived key|, using DKP<imm,imm> would result
	 * in invalid opcodes (last bytes of user key) in the resulting
	 * descriptor. Use DKP<ptr,imm> instead => both virtual and dma key
	 * addresses are needed.
	 */
	if (keylen > ctx->adata.keylen_pad) {
		memcpy(ctx->key, key, keylen);
		dma_sync_single_for_device(ctx->dev, ctx->adata.key_dma,
					   ctx->adata.keylen_pad,
					   DMA_TO_DEVICE);
	}

	ret = ahash_set_sh_desc(ahash);
	kfree(hashed_key);
	return ret;
bad_free_key:
	kfree(hashed_key);
	return -EINVAL;
}

static inline void ahash_unmap(struct device *dev, struct ahash_edesc *edesc,
			       struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx_dma(req);

	if (edesc->src_nents)
		dma_unmap_sg(dev, req->src, edesc->src_nents, DMA_TO_DEVICE);

	if (edesc->qm_sg_bytes)
		dma_unmap_single(dev, edesc->qm_sg_dma, edesc->qm_sg_bytes,
				 DMA_TO_DEVICE);

	if (state->buf_dma) {
		dma_unmap_single(dev, state->buf_dma, state->buflen,
				 DMA_TO_DEVICE);
		state->buf_dma = 0;
	}
}

static inline void ahash_unmap_ctx(struct device *dev,
				   struct ahash_edesc *edesc,
				   struct ahash_request *req, u32 flag)
{
	struct caam_hash_state *state = ahash_request_ctx_dma(req);

	if (state->ctx_dma) {
		dma_unmap_single(dev, state->ctx_dma, state->ctx_dma_len, flag);
		state->ctx_dma = 0;
	}
	ahash_unmap(dev, edesc, req);
}

static void ahash_done(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct ahash_edesc *edesc = state->caam_req.edesc;
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	int digestsize = crypto_ahash_digestsize(ahash);
	int ecode = 0;

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status))
		ecode = caam_qi2_strstatus(ctx->dev, status);

	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_FROM_DEVICE);
	memcpy(req->result, state->caam_ctx, digestsize);
	qi_cache_free(edesc);

	print_hex_dump_debug("ctx@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
			     ctx->ctx_len, 1);

	ahash_request_complete(req, ecode);
}

static void ahash_done_bi(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct ahash_edesc *edesc = state->caam_req.edesc;
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	int ecode = 0;

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status))
		ecode = caam_qi2_strstatus(ctx->dev, status);

	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_BIDIRECTIONAL);
	qi_cache_free(edesc);

	scatterwalk_map_and_copy(state->buf, req->src,
				 req->nbytes - state->next_buflen,
				 state->next_buflen, 0);
	state->buflen = state->next_buflen;

	print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->buf,
			     state->buflen, 1);

	print_hex_dump_debug("ctx@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
			     ctx->ctx_len, 1);
	if (req->result)
		print_hex_dump_debug("result@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, req->result,
				     crypto_ahash_digestsize(ahash), 1);

	ahash_request_complete(req, ecode);
}

static void ahash_done_ctx_src(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct ahash_edesc *edesc = state->caam_req.edesc;
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	int digestsize = crypto_ahash_digestsize(ahash);
	int ecode = 0;

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status))
		ecode = caam_qi2_strstatus(ctx->dev, status);

	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_BIDIRECTIONAL);
	memcpy(req->result, state->caam_ctx, digestsize);
	qi_cache_free(edesc);

	print_hex_dump_debug("ctx@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
			     ctx->ctx_len, 1);

	ahash_request_complete(req, ecode);
}

static void ahash_done_ctx_dst(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct ahash_edesc *edesc = state->caam_req.edesc;
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	int ecode = 0;

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status))
		ecode = caam_qi2_strstatus(ctx->dev, status);

	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_FROM_DEVICE);
	qi_cache_free(edesc);

	scatterwalk_map_and_copy(state->buf, req->src,
				 req->nbytes - state->next_buflen,
				 state->next_buflen, 0);
	state->buflen = state->next_buflen;

	print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->buf,
			     state->buflen, 1);

	print_hex_dump_debug("ctx@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
			     ctx->ctx_len, 1);
	if (req->result)
		print_hex_dump_debug("result@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, req->result,
				     crypto_ahash_digestsize(ahash), 1);

	ahash_request_complete(req, ecode);
}

static int ahash_update_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_request *req_ctx = &state->caam_req;
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->buf;
	int *buflen = &state->buflen;
	int *next_buflen = &state->next_buflen;
	int in_len = *buflen + req->nbytes, to_hash;
	int src_nents, mapped_nents, qm_sg_bytes, qm_sg_src_index;
	struct ahash_edesc *edesc;
	int ret = 0;

	*next_buflen = in_len & (crypto_tfm_alg_blocksize(&ahash->base) - 1);
	to_hash = in_len - *next_buflen;

	if (to_hash) {
		struct dpaa2_sg_entry *sg_table;
		int src_len = req->nbytes - *next_buflen;

		src_nents = sg_nents_for_len(req->src, src_len);
		if (src_nents < 0) {
			dev_err(ctx->dev, "Invalid number of src SG.\n");
			return src_nents;
		}

		if (src_nents) {
			mapped_nents = dma_map_sg(ctx->dev, req->src, src_nents,
						  DMA_TO_DEVICE);
			if (!mapped_nents) {
				dev_err(ctx->dev, "unable to DMA map source\n");
				return -ENOMEM;
			}
		} else {
			mapped_nents = 0;
		}

		/* allocate space for base edesc and link tables */
		edesc = qi_cache_zalloc(flags);
		if (!edesc) {
			dma_unmap_sg(ctx->dev, req->src, src_nents,
				     DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;
		qm_sg_src_index = 1 + (*buflen ? 1 : 0);
		qm_sg_bytes = pad_sg_nents(qm_sg_src_index + mapped_nents) *
			      sizeof(*sg_table);
		sg_table = &edesc->sgt[0];

		ret = ctx_map_to_qm_sg(ctx->dev, state, ctx->ctx_len, sg_table,
				       DMA_BIDIRECTIONAL);
		if (ret)
			goto unmap_ctx;

		ret = buf_map_to_qm_sg(ctx->dev, sg_table + 1, state);
		if (ret)
			goto unmap_ctx;

		if (mapped_nents) {
			sg_to_qm_sg_last(req->src, src_len,
					 sg_table + qm_sg_src_index, 0);
		} else {
			dpaa2_sg_set_final(sg_table + qm_sg_src_index - 1,
					   true);
		}

		edesc->qm_sg_dma = dma_map_single(ctx->dev, sg_table,
						  qm_sg_bytes, DMA_TO_DEVICE);
		if (dma_mapping_error(ctx->dev, edesc->qm_sg_dma)) {
			dev_err(ctx->dev, "unable to map S/G table\n");
			ret = -ENOMEM;
			goto unmap_ctx;
		}
		edesc->qm_sg_bytes = qm_sg_bytes;

		memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
		dpaa2_fl_set_final(in_fle, true);
		dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
		dpaa2_fl_set_addr(in_fle, edesc->qm_sg_dma);
		dpaa2_fl_set_len(in_fle, ctx->ctx_len + to_hash);
		dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
		dpaa2_fl_set_addr(out_fle, state->ctx_dma);
		dpaa2_fl_set_len(out_fle, ctx->ctx_len);

		req_ctx->flc = &ctx->flc[UPDATE];
		req_ctx->flc_dma = ctx->flc_dma[UPDATE];
		req_ctx->cbk = ahash_done_bi;
		req_ctx->ctx = &req->base;
		req_ctx->edesc = edesc;

		ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
		if (ret != -EINPROGRESS &&
		    !(ret == -EBUSY &&
		      req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			goto unmap_ctx;
	} else if (*next_buflen) {
		scatterwalk_map_and_copy(buf + *buflen, req->src, 0,
					 req->nbytes, 0);
		*buflen = *next_buflen;

		print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, buf,
				     *buflen, 1);
	}

	return ret;
unmap_ctx:
	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_BIDIRECTIONAL);
	qi_cache_free(edesc);
	return ret;
}

static int ahash_final_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_request *req_ctx = &state->caam_req;
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	int buflen = state->buflen;
	int qm_sg_bytes;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	struct dpaa2_sg_entry *sg_table;
	int ret;

	/* allocate space for base edesc and link tables */
	edesc = qi_cache_zalloc(flags);
	if (!edesc)
		return -ENOMEM;

	qm_sg_bytes = pad_sg_nents(1 + (buflen ? 1 : 0)) * sizeof(*sg_table);
	sg_table = &edesc->sgt[0];

	ret = ctx_map_to_qm_sg(ctx->dev, state, ctx->ctx_len, sg_table,
			       DMA_BIDIRECTIONAL);
	if (ret)
		goto unmap_ctx;

	ret = buf_map_to_qm_sg(ctx->dev, sg_table + 1, state);
	if (ret)
		goto unmap_ctx;

	dpaa2_sg_set_final(sg_table + (buflen ? 1 : 0), true);

	edesc->qm_sg_dma = dma_map_single(ctx->dev, sg_table, qm_sg_bytes,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(ctx->dev, edesc->qm_sg_dma)) {
		dev_err(ctx->dev, "unable to map S/G table\n");
		ret = -ENOMEM;
		goto unmap_ctx;
	}
	edesc->qm_sg_bytes = qm_sg_bytes;

	memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
	dpaa2_fl_set_final(in_fle, true);
	dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
	dpaa2_fl_set_addr(in_fle, edesc->qm_sg_dma);
	dpaa2_fl_set_len(in_fle, ctx->ctx_len + buflen);
	dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
	dpaa2_fl_set_addr(out_fle, state->ctx_dma);
	dpaa2_fl_set_len(out_fle, digestsize);

	req_ctx->flc = &ctx->flc[FINALIZE];
	req_ctx->flc_dma = ctx->flc_dma[FINALIZE];
	req_ctx->cbk = ahash_done_ctx_src;
	req_ctx->ctx = &req->base;
	req_ctx->edesc = edesc;

	ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
	if (ret == -EINPROGRESS ||
	    (ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
		return ret;

unmap_ctx:
	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_BIDIRECTIONAL);
	qi_cache_free(edesc);
	return ret;
}

static int ahash_finup_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_request *req_ctx = &state->caam_req;
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	int buflen = state->buflen;
	int qm_sg_bytes, qm_sg_src_index;
	int src_nents, mapped_nents;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	struct dpaa2_sg_entry *sg_table;
	int ret;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (src_nents < 0) {
		dev_err(ctx->dev, "Invalid number of src SG.\n");
		return src_nents;
	}

	if (src_nents) {
		mapped_nents = dma_map_sg(ctx->dev, req->src, src_nents,
					  DMA_TO_DEVICE);
		if (!mapped_nents) {
			dev_err(ctx->dev, "unable to DMA map source\n");
			return -ENOMEM;
		}
	} else {
		mapped_nents = 0;
	}

	/* allocate space for base edesc and link tables */
	edesc = qi_cache_zalloc(flags);
	if (!edesc) {
		dma_unmap_sg(ctx->dev, req->src, src_nents, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	edesc->src_nents = src_nents;
	qm_sg_src_index = 1 + (buflen ? 1 : 0);
	qm_sg_bytes = pad_sg_nents(qm_sg_src_index + mapped_nents) *
		      sizeof(*sg_table);
	sg_table = &edesc->sgt[0];

	ret = ctx_map_to_qm_sg(ctx->dev, state, ctx->ctx_len, sg_table,
			       DMA_BIDIRECTIONAL);
	if (ret)
		goto unmap_ctx;

	ret = buf_map_to_qm_sg(ctx->dev, sg_table + 1, state);
	if (ret)
		goto unmap_ctx;

	sg_to_qm_sg_last(req->src, req->nbytes, sg_table + qm_sg_src_index, 0);

	edesc->qm_sg_dma = dma_map_single(ctx->dev, sg_table, qm_sg_bytes,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(ctx->dev, edesc->qm_sg_dma)) {
		dev_err(ctx->dev, "unable to map S/G table\n");
		ret = -ENOMEM;
		goto unmap_ctx;
	}
	edesc->qm_sg_bytes = qm_sg_bytes;

	memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
	dpaa2_fl_set_final(in_fle, true);
	dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
	dpaa2_fl_set_addr(in_fle, edesc->qm_sg_dma);
	dpaa2_fl_set_len(in_fle, ctx->ctx_len + buflen + req->nbytes);
	dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
	dpaa2_fl_set_addr(out_fle, state->ctx_dma);
	dpaa2_fl_set_len(out_fle, digestsize);

	req_ctx->flc = &ctx->flc[FINALIZE];
	req_ctx->flc_dma = ctx->flc_dma[FINALIZE];
	req_ctx->cbk = ahash_done_ctx_src;
	req_ctx->ctx = &req->base;
	req_ctx->edesc = edesc;

	ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
	if (ret == -EINPROGRESS ||
	    (ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
		return ret;

unmap_ctx:
	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_BIDIRECTIONAL);
	qi_cache_free(edesc);
	return ret;
}

static int ahash_digest(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_request *req_ctx = &state->caam_req;
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	int digestsize = crypto_ahash_digestsize(ahash);
	int src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	int ret = -ENOMEM;

	state->buf_dma = 0;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (src_nents < 0) {
		dev_err(ctx->dev, "Invalid number of src SG.\n");
		return src_nents;
	}

	if (src_nents) {
		mapped_nents = dma_map_sg(ctx->dev, req->src, src_nents,
					  DMA_TO_DEVICE);
		if (!mapped_nents) {
			dev_err(ctx->dev, "unable to map source for DMA\n");
			return ret;
		}
	} else {
		mapped_nents = 0;
	}

	/* allocate space for base edesc and link tables */
	edesc = qi_cache_zalloc(flags);
	if (!edesc) {
		dma_unmap_sg(ctx->dev, req->src, src_nents, DMA_TO_DEVICE);
		return ret;
	}

	edesc->src_nents = src_nents;
	memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));

	if (mapped_nents > 1) {
		int qm_sg_bytes;
		struct dpaa2_sg_entry *sg_table = &edesc->sgt[0];

		qm_sg_bytes = pad_sg_nents(mapped_nents) * sizeof(*sg_table);
		sg_to_qm_sg_last(req->src, req->nbytes, sg_table, 0);
		edesc->qm_sg_dma = dma_map_single(ctx->dev, sg_table,
						  qm_sg_bytes, DMA_TO_DEVICE);
		if (dma_mapping_error(ctx->dev, edesc->qm_sg_dma)) {
			dev_err(ctx->dev, "unable to map S/G table\n");
			goto unmap;
		}
		edesc->qm_sg_bytes = qm_sg_bytes;
		dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
		dpaa2_fl_set_addr(in_fle, edesc->qm_sg_dma);
	} else {
		dpaa2_fl_set_format(in_fle, dpaa2_fl_single);
		dpaa2_fl_set_addr(in_fle, sg_dma_address(req->src));
	}

	state->ctx_dma_len = digestsize;
	state->ctx_dma = dma_map_single(ctx->dev, state->caam_ctx, digestsize,
					DMA_FROM_DEVICE);
	if (dma_mapping_error(ctx->dev, state->ctx_dma)) {
		dev_err(ctx->dev, "unable to map ctx\n");
		state->ctx_dma = 0;
		goto unmap;
	}

	dpaa2_fl_set_final(in_fle, true);
	dpaa2_fl_set_len(in_fle, req->nbytes);
	dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
	dpaa2_fl_set_addr(out_fle, state->ctx_dma);
	dpaa2_fl_set_len(out_fle, digestsize);

	req_ctx->flc = &ctx->flc[DIGEST];
	req_ctx->flc_dma = ctx->flc_dma[DIGEST];
	req_ctx->cbk = ahash_done;
	req_ctx->ctx = &req->base;
	req_ctx->edesc = edesc;
	ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
	if (ret == -EINPROGRESS ||
	    (ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
		return ret;

unmap:
	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_FROM_DEVICE);
	qi_cache_free(edesc);
	return ret;
}

static int ahash_final_no_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_request *req_ctx = &state->caam_req;
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->buf;
	int buflen = state->buflen;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	int ret = -ENOMEM;

	/* allocate space for base edesc and link tables */
	edesc = qi_cache_zalloc(flags);
	if (!edesc)
		return ret;

	if (buflen) {
		state->buf_dma = dma_map_single(ctx->dev, buf, buflen,
						DMA_TO_DEVICE);
		if (dma_mapping_error(ctx->dev, state->buf_dma)) {
			dev_err(ctx->dev, "unable to map src\n");
			goto unmap;
		}
	}

	state->ctx_dma_len = digestsize;
	state->ctx_dma = dma_map_single(ctx->dev, state->caam_ctx, digestsize,
					DMA_FROM_DEVICE);
	if (dma_mapping_error(ctx->dev, state->ctx_dma)) {
		dev_err(ctx->dev, "unable to map ctx\n");
		state->ctx_dma = 0;
		goto unmap;
	}

	memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
	dpaa2_fl_set_final(in_fle, true);
	/*
	 * crypto engine requires the input entry to be present when
	 * "frame list" FD is used.
	 * Since engine does not support FMT=2'b11 (unused entry type), leaving
	 * in_fle zeroized (except for "Final" flag) is the best option.
	 */
	if (buflen) {
		dpaa2_fl_set_format(in_fle, dpaa2_fl_single);
		dpaa2_fl_set_addr(in_fle, state->buf_dma);
		dpaa2_fl_set_len(in_fle, buflen);
	}
	dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
	dpaa2_fl_set_addr(out_fle, state->ctx_dma);
	dpaa2_fl_set_len(out_fle, digestsize);

	req_ctx->flc = &ctx->flc[DIGEST];
	req_ctx->flc_dma = ctx->flc_dma[DIGEST];
	req_ctx->cbk = ahash_done;
	req_ctx->ctx = &req->base;
	req_ctx->edesc = edesc;

	ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
	if (ret == -EINPROGRESS ||
	    (ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
		return ret;

unmap:
	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_FROM_DEVICE);
	qi_cache_free(edesc);
	return ret;
}

static int ahash_update_no_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_request *req_ctx = &state->caam_req;
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->buf;
	int *buflen = &state->buflen;
	int *next_buflen = &state->next_buflen;
	int in_len = *buflen + req->nbytes, to_hash;
	int qm_sg_bytes, src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	int ret = 0;

	*next_buflen = in_len & (crypto_tfm_alg_blocksize(&ahash->base) - 1);
	to_hash = in_len - *next_buflen;

	if (to_hash) {
		struct dpaa2_sg_entry *sg_table;
		int src_len = req->nbytes - *next_buflen;

		src_nents = sg_nents_for_len(req->src, src_len);
		if (src_nents < 0) {
			dev_err(ctx->dev, "Invalid number of src SG.\n");
			return src_nents;
		}

		if (src_nents) {
			mapped_nents = dma_map_sg(ctx->dev, req->src, src_nents,
						  DMA_TO_DEVICE);
			if (!mapped_nents) {
				dev_err(ctx->dev, "unable to DMA map source\n");
				return -ENOMEM;
			}
		} else {
			mapped_nents = 0;
		}

		/* allocate space for base edesc and link tables */
		edesc = qi_cache_zalloc(flags);
		if (!edesc) {
			dma_unmap_sg(ctx->dev, req->src, src_nents,
				     DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;
		qm_sg_bytes = pad_sg_nents(1 + mapped_nents) *
			      sizeof(*sg_table);
		sg_table = &edesc->sgt[0];

		ret = buf_map_to_qm_sg(ctx->dev, sg_table, state);
		if (ret)
			goto unmap_ctx;

		sg_to_qm_sg_last(req->src, src_len, sg_table + 1, 0);

		edesc->qm_sg_dma = dma_map_single(ctx->dev, sg_table,
						  qm_sg_bytes, DMA_TO_DEVICE);
		if (dma_mapping_error(ctx->dev, edesc->qm_sg_dma)) {
			dev_err(ctx->dev, "unable to map S/G table\n");
			ret = -ENOMEM;
			goto unmap_ctx;
		}
		edesc->qm_sg_bytes = qm_sg_bytes;

		state->ctx_dma_len = ctx->ctx_len;
		state->ctx_dma = dma_map_single(ctx->dev, state->caam_ctx,
						ctx->ctx_len, DMA_FROM_DEVICE);
		if (dma_mapping_error(ctx->dev, state->ctx_dma)) {
			dev_err(ctx->dev, "unable to map ctx\n");
			state->ctx_dma = 0;
			ret = -ENOMEM;
			goto unmap_ctx;
		}

		memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
		dpaa2_fl_set_final(in_fle, true);
		dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
		dpaa2_fl_set_addr(in_fle, edesc->qm_sg_dma);
		dpaa2_fl_set_len(in_fle, to_hash);
		dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
		dpaa2_fl_set_addr(out_fle, state->ctx_dma);
		dpaa2_fl_set_len(out_fle, ctx->ctx_len);

		req_ctx->flc = &ctx->flc[UPDATE_FIRST];
		req_ctx->flc_dma = ctx->flc_dma[UPDATE_FIRST];
		req_ctx->cbk = ahash_done_ctx_dst;
		req_ctx->ctx = &req->base;
		req_ctx->edesc = edesc;

		ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
		if (ret != -EINPROGRESS &&
		    !(ret == -EBUSY &&
		      req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
			goto unmap_ctx;

		state->update = ahash_update_ctx;
		state->finup = ahash_finup_ctx;
		state->final = ahash_final_ctx;
	} else if (*next_buflen) {
		scatterwalk_map_and_copy(buf + *buflen, req->src, 0,
					 req->nbytes, 0);
		*buflen = *next_buflen;

		print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, buf,
				     *buflen, 1);
	}

	return ret;
unmap_ctx:
	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_TO_DEVICE);
	qi_cache_free(edesc);
	return ret;
}

static int ahash_finup_no_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_request *req_ctx = &state->caam_req;
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	int buflen = state->buflen;
	int qm_sg_bytes, src_nents, mapped_nents;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	struct dpaa2_sg_entry *sg_table;
	int ret = -ENOMEM;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (src_nents < 0) {
		dev_err(ctx->dev, "Invalid number of src SG.\n");
		return src_nents;
	}

	if (src_nents) {
		mapped_nents = dma_map_sg(ctx->dev, req->src, src_nents,
					  DMA_TO_DEVICE);
		if (!mapped_nents) {
			dev_err(ctx->dev, "unable to DMA map source\n");
			return ret;
		}
	} else {
		mapped_nents = 0;
	}

	/* allocate space for base edesc and link tables */
	edesc = qi_cache_zalloc(flags);
	if (!edesc) {
		dma_unmap_sg(ctx->dev, req->src, src_nents, DMA_TO_DEVICE);
		return ret;
	}

	edesc->src_nents = src_nents;
	qm_sg_bytes = pad_sg_nents(2 + mapped_nents) * sizeof(*sg_table);
	sg_table = &edesc->sgt[0];

	ret = buf_map_to_qm_sg(ctx->dev, sg_table, state);
	if (ret)
		goto unmap;

	sg_to_qm_sg_last(req->src, req->nbytes, sg_table + 1, 0);

	edesc->qm_sg_dma = dma_map_single(ctx->dev, sg_table, qm_sg_bytes,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(ctx->dev, edesc->qm_sg_dma)) {
		dev_err(ctx->dev, "unable to map S/G table\n");
		ret = -ENOMEM;
		goto unmap;
	}
	edesc->qm_sg_bytes = qm_sg_bytes;

	state->ctx_dma_len = digestsize;
	state->ctx_dma = dma_map_single(ctx->dev, state->caam_ctx, digestsize,
					DMA_FROM_DEVICE);
	if (dma_mapping_error(ctx->dev, state->ctx_dma)) {
		dev_err(ctx->dev, "unable to map ctx\n");
		state->ctx_dma = 0;
		ret = -ENOMEM;
		goto unmap;
	}

	memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
	dpaa2_fl_set_final(in_fle, true);
	dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
	dpaa2_fl_set_addr(in_fle, edesc->qm_sg_dma);
	dpaa2_fl_set_len(in_fle, buflen + req->nbytes);
	dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
	dpaa2_fl_set_addr(out_fle, state->ctx_dma);
	dpaa2_fl_set_len(out_fle, digestsize);

	req_ctx->flc = &ctx->flc[DIGEST];
	req_ctx->flc_dma = ctx->flc_dma[DIGEST];
	req_ctx->cbk = ahash_done;
	req_ctx->ctx = &req->base;
	req_ctx->edesc = edesc;
	ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
	if (ret != -EINPROGRESS &&
	    !(ret == -EBUSY && req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
		goto unmap;

	return ret;
unmap:
	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_FROM_DEVICE);
	qi_cache_free(edesc);
	return ret;
}

static int ahash_update_first(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx_dma(ahash);
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_request *req_ctx = &state->caam_req;
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->buf;
	int *buflen = &state->buflen;
	int *next_buflen = &state->next_buflen;
	int to_hash;
	int src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	int ret = 0;

	*next_buflen = req->nbytes & (crypto_tfm_alg_blocksize(&ahash->base) -
				      1);
	to_hash = req->nbytes - *next_buflen;

	if (to_hash) {
		struct dpaa2_sg_entry *sg_table;
		int src_len = req->nbytes - *next_buflen;

		src_nents = sg_nents_for_len(req->src, src_len);
		if (src_nents < 0) {
			dev_err(ctx->dev, "Invalid number of src SG.\n");
			return src_nents;
		}

		if (src_nents) {
			mapped_nents = dma_map_sg(ctx->dev, req->src, src_nents,
						  DMA_TO_DEVICE);
			if (!mapped_nents) {
				dev_err(ctx->dev, "unable to map source for DMA\n");
				return -ENOMEM;
			}
		} else {
			mapped_nents = 0;
		}

		/* allocate space for base edesc and link tables */
		edesc = qi_cache_zalloc(flags);
		if (!edesc) {
			dma_unmap_sg(ctx->dev, req->src, src_nents,
				     DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;
		sg_table = &edesc->sgt[0];

		memset(&req_ctx->fd_flt, 0, sizeof(req_ctx->fd_flt));
		dpaa2_fl_set_final(in_fle, true);
		dpaa2_fl_set_len(in_fle, to_hash);

		if (mapped_nents > 1) {
			int qm_sg_bytes;

			sg_to_qm_sg_last(req->src, src_len, sg_table, 0);
			qm_sg_bytes = pad_sg_nents(mapped_nents) *
				      sizeof(*sg_table);
			edesc->qm_sg_dma = dma_map_single(ctx->dev, sg_table,
							  qm_sg_bytes,
							  DMA_TO_DEVICE);
			if (dma_mapping_error(ctx->dev, edesc->qm_sg_dma)) {
				dev_err(ctx->dev, "unable to map S/G table\n");
				ret = -ENOMEM;
				goto unmap_ctx;
			}
			edesc->qm_sg_bytes = qm_sg_bytes;
			dpaa2_fl_set_format(in_fle, dpaa2_fl_sg);
			dpaa2_fl_set_addr(in_fle, edesc->qm_sg_dma);
		} else {
			dpaa2_fl_set_format(in_fle, dpaa2_fl_single);
			dpaa2_fl_set_addr(in_fle, sg_dma_address(req->src));
		}

		state->ctx_dma_len = ctx->ctx_len;
		state->ctx_dma = dma_map_single(ctx->dev, state->caam_ctx,
						ctx->ctx_len, DMA_FROM_DEVICE);
		if (dma_mapping_error(ctx->dev, state->ctx_dma)) {
			dev_err(ctx->dev, "unable to map ctx\n");
			state->ctx_dma = 0;
			ret = -ENOMEM;
			goto unmap_ctx;
		}

		dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
		dpaa2_fl_set_addr(out_fle, state->ctx_dma);
		dpaa2_fl_set_len(out_fle, ctx->ctx_len);

		req_ctx->flc = &ctx->flc[UPDATE_FIRST];
		req_ctx->flc_dma = ctx->flc_dma[UPDATE_FIRST];
		req_ctx->cbk = ahash_done_ctx_dst;
		req_ctx->ctx = &req->base;
		req_ctx->edesc = edesc;

		ret = dpaa2_caam_enqueue(ctx->dev, req_ctx);
		if (ret != -EINPROGRESS &&
		    !(ret == -EBUSY && req->base.flags &
		      CRYPTO_TFM_REQ_MAY_BACKLOG))
			goto unmap_ctx;

		state->update = ahash_update_ctx;
		state->finup = ahash_finup_ctx;
		state->final = ahash_final_ctx;
	} else if (*next_buflen) {
		state->update = ahash_update_no_ctx;
		state->finup = ahash_finup_no_ctx;
		state->final = ahash_final_no_ctx;
		scatterwalk_map_and_copy(buf, req->src, 0,
					 req->nbytes, 0);
		*buflen = *next_buflen;

		print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, buf,
				     *buflen, 1);
	}

	return ret;
unmap_ctx:
	ahash_unmap_ctx(ctx->dev, edesc, req, DMA_TO_DEVICE);
	qi_cache_free(edesc);
	return ret;
}

static int ahash_finup_first(struct ahash_request *req)
{
	return ahash_digest(req);
}

static int ahash_init(struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx_dma(req);

	state->update = ahash_update_first;
	state->finup = ahash_finup_first;
	state->final = ahash_final_no_ctx;

	state->ctx_dma = 0;
	state->ctx_dma_len = 0;
	state->buf_dma = 0;
	state->buflen = 0;
	state->next_buflen = 0;

	return 0;
}

static int ahash_update(struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx_dma(req);

	return state->update(req);
}

static int ahash_finup(struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx_dma(req);

	return state->finup(req);
}

static int ahash_final(struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx_dma(req);

	return state->final(req);
}

static int ahash_export(struct ahash_request *req, void *out)
{
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	struct caam_export_state *export = out;
	u8 *buf = state->buf;
	int len = state->buflen;

	memcpy(export->buf, buf, len);
	memcpy(export->caam_ctx, state->caam_ctx, sizeof(export->caam_ctx));
	export->buflen = len;
	export->update = state->update;
	export->final = state->final;
	export->finup = state->finup;

	return 0;
}

static int ahash_import(struct ahash_request *req, const void *in)
{
	struct caam_hash_state *state = ahash_request_ctx_dma(req);
	const struct caam_export_state *export = in;

	memset(state, 0, sizeof(*state));
	memcpy(state->buf, export->buf, export->buflen);
	memcpy(state->caam_ctx, export->caam_ctx, sizeof(state->caam_ctx));
	state->buflen = export->buflen;
	state->update = export->update;
	state->final = export->final;
	state->finup = export->finup;

	return 0;
}

struct caam_hash_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	char hmac_name[CRYPTO_MAX_ALG_NAME];
	char hmac_driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	struct ahash_alg template_ahash;
	u32 alg_type;
};

/* ahash descriptors */
static struct caam_hash_template driver_hash[] = {
	{
		.name = "sha1",
		.driver_name = "sha1-caam-qi2",
		.hmac_name = "hmac(sha1)",
		.hmac_driver_name = "hmac-sha1-caam-qi2",
		.blocksize = SHA1_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA1_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA1,
	}, {
		.name = "sha224",
		.driver_name = "sha224-caam-qi2",
		.hmac_name = "hmac(sha224)",
		.hmac_driver_name = "hmac-sha224-caam-qi2",
		.blocksize = SHA224_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA224,
	}, {
		.name = "sha256",
		.driver_name = "sha256-caam-qi2",
		.hmac_name = "hmac(sha256)",
		.hmac_driver_name = "hmac-sha256-caam-qi2",
		.blocksize = SHA256_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA256,
	}, {
		.name = "sha384",
		.driver_name = "sha384-caam-qi2",
		.hmac_name = "hmac(sha384)",
		.hmac_driver_name = "hmac-sha384-caam-qi2",
		.blocksize = SHA384_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA384_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA384,
	}, {
		.name = "sha512",
		.driver_name = "sha512-caam-qi2",
		.hmac_name = "hmac(sha512)",
		.hmac_driver_name = "hmac-sha512-caam-qi2",
		.blocksize = SHA512_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA512_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA512,
	}, {
		.name = "md5",
		.driver_name = "md5-caam-qi2",
		.hmac_name = "hmac(md5)",
		.hmac_driver_name = "hmac-md5-caam-qi2",
		.blocksize = MD5_BLOCK_WORDS * 4,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = MD5_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_MD5,
	}
};

struct caam_hash_alg {
	struct list_head entry;
	struct device *dev;
	int alg_type;
	struct ahash_alg ahash_alg;
};

static int caam_hash_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);
	struct crypto_alg *base = tfm->__crt_alg;
	struct hash_alg_common *halg =
		 container_of(base, struct hash_alg_common, base);
	struct ahash_alg *alg =
		 container_of(halg, struct ahash_alg, halg);
	struct caam_hash_alg *caam_hash =
		 container_of(alg, struct caam_hash_alg, ahash_alg);
	struct caam_hash_ctx *ctx = crypto_tfm_ctx_dma(tfm);
	/* Sizes for MDHA running digests: MD5, SHA1, 224, 256, 384, 512 */
	static const u8 runninglen[] = { HASH_MSG_LEN + MD5_DIGEST_SIZE,
					 HASH_MSG_LEN + SHA1_DIGEST_SIZE,
					 HASH_MSG_LEN + 32,
					 HASH_MSG_LEN + SHA256_DIGEST_SIZE,
					 HASH_MSG_LEN + 64,
					 HASH_MSG_LEN + SHA512_DIGEST_SIZE };
	dma_addr_t dma_addr;
	int i;

	ctx->dev = caam_hash->dev;

	if (alg->setkey) {
		ctx->adata.key_dma = dma_map_single_attrs(ctx->dev, ctx->key,
							  ARRAY_SIZE(ctx->key),
							  DMA_TO_DEVICE,
							  DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(ctx->dev, ctx->adata.key_dma)) {
			dev_err(ctx->dev, "unable to map key\n");
			return -ENOMEM;
		}
	}

	dma_addr = dma_map_single_attrs(ctx->dev, ctx->flc, sizeof(ctx->flc),
					DMA_BIDIRECTIONAL,
					DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(ctx->dev, dma_addr)) {
		dev_err(ctx->dev, "unable to map shared descriptors\n");
		if (ctx->adata.key_dma)
			dma_unmap_single_attrs(ctx->dev, ctx->adata.key_dma,
					       ARRAY_SIZE(ctx->key),
					       DMA_TO_DEVICE,
					       DMA_ATTR_SKIP_CPU_SYNC);
		return -ENOMEM;
	}

	for (i = 0; i < HASH_NUM_OP; i++)
		ctx->flc_dma[i] = dma_addr + i * sizeof(ctx->flc[i]);

	/* copy descriptor header template value */
	ctx->adata.algtype = OP_TYPE_CLASS2_ALG | caam_hash->alg_type;

	ctx->ctx_len = runninglen[(ctx->adata.algtype &
				   OP_ALG_ALGSEL_SUBMASK) >>
				  OP_ALG_ALGSEL_SHIFT];

	crypto_ahash_set_reqsize_dma(ahash, sizeof(struct caam_hash_state));

	/*
	 * For keyed hash algorithms shared descriptors
	 * will be created later in setkey() callback
	 */
	return alg->setkey ? 0 : ahash_set_sh_desc(ahash);
}

static void caam_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct caam_hash_ctx *ctx = crypto_tfm_ctx_dma(tfm);

	dma_unmap_single_attrs(ctx->dev, ctx->flc_dma[0], sizeof(ctx->flc),
			       DMA_BIDIRECTIONAL, DMA_ATTR_SKIP_CPU_SYNC);
	if (ctx->adata.key_dma)
		dma_unmap_single_attrs(ctx->dev, ctx->adata.key_dma,
				       ARRAY_SIZE(ctx->key), DMA_TO_DEVICE,
				       DMA_ATTR_SKIP_CPU_SYNC);
}

static struct caam_hash_alg *caam_hash_alloc(struct device *dev,
	struct caam_hash_template *template, bool keyed)
{
	struct caam_hash_alg *t_alg;
	struct ahash_alg *halg;
	struct crypto_alg *alg;

	t_alg = kzalloc(sizeof(*t_alg), GFP_KERNEL);
	if (!t_alg)
		return ERR_PTR(-ENOMEM);

	t_alg->ahash_alg = template->template_ahash;
	halg = &t_alg->ahash_alg;
	alg = &halg->halg.base;

	if (keyed) {
		snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->hmac_name);
		snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->hmac_driver_name);
	} else {
		snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->name);
		snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->driver_name);
		t_alg->ahash_alg.setkey = NULL;
	}
	alg->cra_module = THIS_MODULE;
	alg->cra_init = caam_hash_cra_init;
	alg->cra_exit = caam_hash_cra_exit;
	alg->cra_ctxsize = sizeof(struct caam_hash_ctx) + crypto_dma_padding();
	alg->cra_priority = CAAM_CRA_PRIORITY;
	alg->cra_blocksize = template->blocksize;
	alg->cra_alignmask = 0;
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY;

	t_alg->alg_type = template->alg_type;
	t_alg->dev = dev;

	return t_alg;
}

static void dpaa2_caam_fqdan_cb(struct dpaa2_io_notification_ctx *nctx)
{
	struct dpaa2_caam_priv_per_cpu *ppriv;

	ppriv = container_of(nctx, struct dpaa2_caam_priv_per_cpu, nctx);
	napi_schedule_irqoff(&ppriv->napi);
}

static int __cold dpaa2_dpseci_dpio_setup(struct dpaa2_caam_priv *priv)
{
	struct device *dev = priv->dev;
	struct dpaa2_io_notification_ctx *nctx;
	struct dpaa2_caam_priv_per_cpu *ppriv;
	int err, i = 0, cpu;

	for_each_online_cpu(cpu) {
		ppriv = per_cpu_ptr(priv->ppriv, cpu);
		ppriv->priv = priv;
		nctx = &ppriv->nctx;
		nctx->is_cdan = 0;
		nctx->id = ppriv->rsp_fqid;
		nctx->desired_cpu = cpu;
		nctx->cb = dpaa2_caam_fqdan_cb;

		/* Register notification callbacks */
		ppriv->dpio = dpaa2_io_service_select(cpu);
		err = dpaa2_io_service_register(ppriv->dpio, nctx, dev);
		if (unlikely(err)) {
			dev_dbg(dev, "No affine DPIO for cpu %d\n", cpu);
			nctx->cb = NULL;
			/*
			 * If no affine DPIO for this core, there's probably
			 * none available for next cores either. Signal we want
			 * to retry later, in case the DPIO devices weren't
			 * probed yet.
			 */
			err = -EPROBE_DEFER;
			goto err;
		}

		ppriv->store = dpaa2_io_store_create(DPAA2_CAAM_STORE_SIZE,
						     dev);
		if (unlikely(!ppriv->store)) {
			dev_err(dev, "dpaa2_io_store_create() failed\n");
			err = -ENOMEM;
			goto err;
		}

		if (++i == priv->num_pairs)
			break;
	}

	return 0;

err:
	for_each_online_cpu(cpu) {
		ppriv = per_cpu_ptr(priv->ppriv, cpu);
		if (!ppriv->nctx.cb)
			break;
		dpaa2_io_service_deregister(ppriv->dpio, &ppriv->nctx, dev);
	}

	for_each_online_cpu(cpu) {
		ppriv = per_cpu_ptr(priv->ppriv, cpu);
		if (!ppriv->store)
			break;
		dpaa2_io_store_destroy(ppriv->store);
	}

	return err;
}

static void __cold dpaa2_dpseci_dpio_free(struct dpaa2_caam_priv *priv)
{
	struct dpaa2_caam_priv_per_cpu *ppriv;
	int i = 0, cpu;

	for_each_online_cpu(cpu) {
		ppriv = per_cpu_ptr(priv->ppriv, cpu);
		dpaa2_io_service_deregister(ppriv->dpio, &ppriv->nctx,
					    priv->dev);
		dpaa2_io_store_destroy(ppriv->store);

		if (++i == priv->num_pairs)
			return;
	}
}

static int dpaa2_dpseci_bind(struct dpaa2_caam_priv *priv)
{
	struct dpseci_rx_queue_cfg rx_queue_cfg;
	struct device *dev = priv->dev;
	struct fsl_mc_device *ls_dev = to_fsl_mc_device(dev);
	struct dpaa2_caam_priv_per_cpu *ppriv;
	int err = 0, i = 0, cpu;

	/* Configure Rx queues */
	for_each_online_cpu(cpu) {
		ppriv = per_cpu_ptr(priv->ppriv, cpu);

		rx_queue_cfg.options = DPSECI_QUEUE_OPT_DEST |
				       DPSECI_QUEUE_OPT_USER_CTX;
		rx_queue_cfg.order_preservation_en = 0;
		rx_queue_cfg.dest_cfg.dest_type = DPSECI_DEST_DPIO;
		rx_queue_cfg.dest_cfg.dest_id = ppriv->nctx.dpio_id;
		/*
		 * Rx priority (WQ) doesn't really matter, since we use
		 * pull mode, i.e. volatile dequeues from specific FQs
		 */
		rx_queue_cfg.dest_cfg.priority = 0;
		rx_queue_cfg.user_ctx = ppriv->nctx.qman64;

		err = dpseci_set_rx_queue(priv->mc_io, 0, ls_dev->mc_handle, i,
					  &rx_queue_cfg);
		if (err) {
			dev_err(dev, "dpseci_set_rx_queue() failed with err %d\n",
				err);
			return err;
		}

		if (++i == priv->num_pairs)
			break;
	}

	return err;
}

static void dpaa2_dpseci_congestion_free(struct dpaa2_caam_priv *priv)
{
	struct device *dev = priv->dev;

	if (!priv->cscn_mem)
		return;

	dma_unmap_single(dev, priv->cscn_dma, DPAA2_CSCN_SIZE, DMA_FROM_DEVICE);
	kfree(priv->cscn_mem);
}

static void dpaa2_dpseci_free(struct dpaa2_caam_priv *priv)
{
	struct device *dev = priv->dev;
	struct fsl_mc_device *ls_dev = to_fsl_mc_device(dev);
	int err;

	if (DPSECI_VER(priv->major_ver, priv->minor_ver) > DPSECI_VER(5, 3)) {
		err = dpseci_reset(priv->mc_io, 0, ls_dev->mc_handle);
		if (err)
			dev_err(dev, "dpseci_reset() failed\n");
	}

	dpaa2_dpseci_congestion_free(priv);
	dpseci_close(priv->mc_io, 0, ls_dev->mc_handle);
}

static void dpaa2_caam_process_fd(struct dpaa2_caam_priv *priv,
				  const struct dpaa2_fd *fd)
{
	struct caam_request *req;
	u32 fd_err;

	if (dpaa2_fd_get_format(fd) != dpaa2_fd_list) {
		dev_err(priv->dev, "Only Frame List FD format is supported!\n");
		return;
	}

	fd_err = dpaa2_fd_get_ctrl(fd) & FD_CTRL_ERR_MASK;
	if (unlikely(fd_err))
		dev_err_ratelimited(priv->dev, "FD error: %08x\n", fd_err);

	/*
	 * FD[ADDR] is guaranteed to be valid, irrespective of errors reported
	 * in FD[ERR] or FD[FRC].
	 */
	req = dpaa2_caam_iova_to_virt(priv, dpaa2_fd_get_addr(fd));
	dma_unmap_single(priv->dev, req->fd_flt_dma, sizeof(req->fd_flt),
			 DMA_BIDIRECTIONAL);
	req->cbk(req->ctx, dpaa2_fd_get_frc(fd));
}

static int dpaa2_caam_pull_fq(struct dpaa2_caam_priv_per_cpu *ppriv)
{
	int err;

	/* Retry while portal is busy */
	do {
		err = dpaa2_io_service_pull_fq(ppriv->dpio, ppriv->rsp_fqid,
					       ppriv->store);
	} while (err == -EBUSY);

	if (unlikely(err))
		dev_err(ppriv->priv->dev, "dpaa2_io_service_pull err %d", err);

	return err;
}

static int dpaa2_caam_store_consume(struct dpaa2_caam_priv_per_cpu *ppriv)
{
	struct dpaa2_dq *dq;
	int cleaned = 0, is_last;

	do {
		dq = dpaa2_io_store_next(ppriv->store, &is_last);
		if (unlikely(!dq)) {
			if (unlikely(!is_last)) {
				dev_dbg(ppriv->priv->dev,
					"FQ %d returned no valid frames\n",
					ppriv->rsp_fqid);
				/*
				 * MUST retry until we get some sort of
				 * valid response token (be it "empty dequeue"
				 * or a valid frame).
				 */
				continue;
			}
			break;
		}

		/* Process FD */
		dpaa2_caam_process_fd(ppriv->priv, dpaa2_dq_fd(dq));
		cleaned++;
	} while (!is_last);

	return cleaned;
}

static int dpaa2_dpseci_poll(struct napi_struct *napi, int budget)
{
	struct dpaa2_caam_priv_per_cpu *ppriv;
	struct dpaa2_caam_priv *priv;
	int err, cleaned = 0, store_cleaned;

	ppriv = container_of(napi, struct dpaa2_caam_priv_per_cpu, napi);
	priv = ppriv->priv;

	if (unlikely(dpaa2_caam_pull_fq(ppriv)))
		return 0;

	do {
		store_cleaned = dpaa2_caam_store_consume(ppriv);
		cleaned += store_cleaned;

		if (store_cleaned == 0 ||
		    cleaned > budget - DPAA2_CAAM_STORE_SIZE)
			break;

		/* Try to dequeue some more */
		err = dpaa2_caam_pull_fq(ppriv);
		if (unlikely(err))
			break;
	} while (1);

	if (cleaned < budget) {
		napi_complete_done(napi, cleaned);
		err = dpaa2_io_service_rearm(ppriv->dpio, &ppriv->nctx);
		if (unlikely(err))
			dev_err(priv->dev, "Notification rearm failed: %d\n",
				err);
	}

	return cleaned;
}

static int dpaa2_dpseci_congestion_setup(struct dpaa2_caam_priv *priv,
					 u16 token)
{
	struct dpseci_congestion_notification_cfg cong_notif_cfg = { 0 };
	struct device *dev = priv->dev;
	unsigned int alignmask;
	int err;

	/*
	 * Congestion group feature supported starting with DPSECI API v5.1
	 * and only when object has been created with this capability.
	 */
	if ((DPSECI_VER(priv->major_ver, priv->minor_ver) < DPSECI_VER(5, 1)) ||
	    !(priv->dpseci_attr.options & DPSECI_OPT_HAS_CG))
		return 0;

	alignmask = DPAA2_CSCN_ALIGN - 1;
	alignmask |= dma_get_cache_alignment() - 1;
	priv->cscn_mem = kzalloc(ALIGN(DPAA2_CSCN_SIZE, alignmask + 1),
				 GFP_KERNEL);
	if (!priv->cscn_mem)
		return -ENOMEM;

	priv->cscn_dma = dma_map_single(dev, priv->cscn_mem,
					DPAA2_CSCN_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, priv->cscn_dma)) {
		dev_err(dev, "Error mapping CSCN memory area\n");
		err = -ENOMEM;
		goto err_dma_map;
	}

	cong_notif_cfg.units = DPSECI_CONGESTION_UNIT_BYTES;
	cong_notif_cfg.threshold_entry = DPAA2_SEC_CONG_ENTRY_THRESH;
	cong_notif_cfg.threshold_exit = DPAA2_SEC_CONG_EXIT_THRESH;
	cong_notif_cfg.message_ctx = (uintptr_t)priv;
	cong_notif_cfg.message_iova = priv->cscn_dma;
	cong_notif_cfg.notification_mode = DPSECI_CGN_MODE_WRITE_MEM_ON_ENTER |
					DPSECI_CGN_MODE_WRITE_MEM_ON_EXIT |
					DPSECI_CGN_MODE_COHERENT_WRITE;

	err = dpseci_set_congestion_notification(priv->mc_io, 0, token,
						 &cong_notif_cfg);
	if (err) {
		dev_err(dev, "dpseci_set_congestion_notification failed\n");
		goto err_set_cong;
	}

	return 0;

err_set_cong:
	dma_unmap_single(dev, priv->cscn_dma, DPAA2_CSCN_SIZE, DMA_FROM_DEVICE);
err_dma_map:
	kfree(priv->cscn_mem);

	return err;
}

static int __cold dpaa2_dpseci_setup(struct fsl_mc_device *ls_dev)
{
	struct device *dev = &ls_dev->dev;
	struct dpaa2_caam_priv *priv;
	struct dpaa2_caam_priv_per_cpu *ppriv;
	int err, cpu;
	u8 i;

	priv = dev_get_drvdata(dev);

	priv->dev = dev;
	priv->dpsec_id = ls_dev->obj_desc.id;

	/* Get a handle for the DPSECI this interface is associate with */
	err = dpseci_open(priv->mc_io, 0, priv->dpsec_id, &ls_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpseci_open() failed: %d\n", err);
		goto err_open;
	}

	err = dpseci_get_api_version(priv->mc_io, 0, &priv->major_ver,
				     &priv->minor_ver);
	if (err) {
		dev_err(dev, "dpseci_get_api_version() failed\n");
		goto err_get_vers;
	}

	dev_info(dev, "dpseci v%d.%d\n", priv->major_ver, priv->minor_ver);

	if (DPSECI_VER(priv->major_ver, priv->minor_ver) > DPSECI_VER(5, 3)) {
		err = dpseci_reset(priv->mc_io, 0, ls_dev->mc_handle);
		if (err) {
			dev_err(dev, "dpseci_reset() failed\n");
			goto err_get_vers;
		}
	}

	err = dpseci_get_attributes(priv->mc_io, 0, ls_dev->mc_handle,
				    &priv->dpseci_attr);
	if (err) {
		dev_err(dev, "dpseci_get_attributes() failed\n");
		goto err_get_vers;
	}

	err = dpseci_get_sec_attr(priv->mc_io, 0, ls_dev->mc_handle,
				  &priv->sec_attr);
	if (err) {
		dev_err(dev, "dpseci_get_sec_attr() failed\n");
		goto err_get_vers;
	}

	err = dpaa2_dpseci_congestion_setup(priv, ls_dev->mc_handle);
	if (err) {
		dev_err(dev, "setup_congestion() failed\n");
		goto err_get_vers;
	}

	priv->num_pairs = min(priv->dpseci_attr.num_rx_queues,
			      priv->dpseci_attr.num_tx_queues);
	if (priv->num_pairs > num_online_cpus()) {
		dev_warn(dev, "%d queues won't be used\n",
			 priv->num_pairs - num_online_cpus());
		priv->num_pairs = num_online_cpus();
	}

	for (i = 0; i < priv->dpseci_attr.num_rx_queues; i++) {
		err = dpseci_get_rx_queue(priv->mc_io, 0, ls_dev->mc_handle, i,
					  &priv->rx_queue_attr[i]);
		if (err) {
			dev_err(dev, "dpseci_get_rx_queue() failed\n");
			goto err_get_rx_queue;
		}
	}

	for (i = 0; i < priv->dpseci_attr.num_tx_queues; i++) {
		err = dpseci_get_tx_queue(priv->mc_io, 0, ls_dev->mc_handle, i,
					  &priv->tx_queue_attr[i]);
		if (err) {
			dev_err(dev, "dpseci_get_tx_queue() failed\n");
			goto err_get_rx_queue;
		}
	}

	i = 0;
	for_each_online_cpu(cpu) {
		u8 j;

		j = i % priv->num_pairs;

		ppriv = per_cpu_ptr(priv->ppriv, cpu);
		ppriv->req_fqid = priv->tx_queue_attr[j].fqid;

		/*
		 * Allow all cores to enqueue, while only some of them
		 * will take part in dequeuing.
		 */
		if (++i > priv->num_pairs)
			continue;

		ppriv->rsp_fqid = priv->rx_queue_attr[j].fqid;
		ppriv->prio = j;

		dev_dbg(dev, "pair %d: rx queue %d, tx queue %d\n", j,
			priv->rx_queue_attr[j].fqid,
			priv->tx_queue_attr[j].fqid);

		ppriv->net_dev.dev = *dev;
		INIT_LIST_HEAD(&ppriv->net_dev.napi_list);
		netif_napi_add_tx_weight(&ppriv->net_dev, &ppriv->napi,
					 dpaa2_dpseci_poll,
					 DPAA2_CAAM_NAPI_WEIGHT);
	}

	return 0;

err_get_rx_queue:
	dpaa2_dpseci_congestion_free(priv);
err_get_vers:
	dpseci_close(priv->mc_io, 0, ls_dev->mc_handle);
err_open:
	return err;
}

static int dpaa2_dpseci_enable(struct dpaa2_caam_priv *priv)
{
	struct device *dev = priv->dev;
	struct fsl_mc_device *ls_dev = to_fsl_mc_device(dev);
	struct dpaa2_caam_priv_per_cpu *ppriv;
	int i;

	for (i = 0; i < priv->num_pairs; i++) {
		ppriv = per_cpu_ptr(priv->ppriv, i);
		napi_enable(&ppriv->napi);
	}

	return dpseci_enable(priv->mc_io, 0, ls_dev->mc_handle);
}

static int __cold dpaa2_dpseci_disable(struct dpaa2_caam_priv *priv)
{
	struct device *dev = priv->dev;
	struct dpaa2_caam_priv_per_cpu *ppriv;
	struct fsl_mc_device *ls_dev = to_fsl_mc_device(dev);
	int i, err = 0, enabled;

	err = dpseci_disable(priv->mc_io, 0, ls_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpseci_disable() failed\n");
		return err;
	}

	err = dpseci_is_enabled(priv->mc_io, 0, ls_dev->mc_handle, &enabled);
	if (err) {
		dev_err(dev, "dpseci_is_enabled() failed\n");
		return err;
	}

	dev_dbg(dev, "disable: %s\n", enabled ? "false" : "true");

	for (i = 0; i < priv->num_pairs; i++) {
		ppriv = per_cpu_ptr(priv->ppriv, i);
		napi_disable(&ppriv->napi);
		netif_napi_del(&ppriv->napi);
	}

	return 0;
}

static struct list_head hash_list;

static int dpaa2_caam_probe(struct fsl_mc_device *dpseci_dev)
{
	struct device *dev;
	struct dpaa2_caam_priv *priv;
	int i, err = 0;
	bool registered = false;

	/*
	 * There is no way to get CAAM endianness - there is no direct register
	 * space access and MC f/w does not provide this attribute.
	 * All DPAA2-based SoCs have little endian CAAM, thus hard-code this
	 * property.
	 */
	caam_little_end = true;

	caam_imx = false;

	dev = &dpseci_dev->dev;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	priv->domain = iommu_get_domain_for_dev(dev);

	qi_cache = kmem_cache_create("dpaa2_caamqicache", CAAM_QI_MEMCACHE_SIZE,
				     0, 0, NULL);
	if (!qi_cache) {
		dev_err(dev, "Can't allocate SEC cache\n");
		return -ENOMEM;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(49));
	if (err) {
		dev_err(dev, "dma_set_mask_and_coherent() failed\n");
		goto err_dma_mask;
	}

	/* Obtain a MC portal */
	err = fsl_mc_portal_allocate(dpseci_dev, 0, &priv->mc_io);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "MC portal allocation failed\n");

		goto err_dma_mask;
	}

	priv->ppriv = alloc_percpu(*priv->ppriv);
	if (!priv->ppriv) {
		dev_err(dev, "alloc_percpu() failed\n");
		err = -ENOMEM;
		goto err_alloc_ppriv;
	}

	/* DPSECI initialization */
	err = dpaa2_dpseci_setup(dpseci_dev);
	if (err) {
		dev_err(dev, "dpaa2_dpseci_setup() failed\n");
		goto err_dpseci_setup;
	}

	/* DPIO */
	err = dpaa2_dpseci_dpio_setup(priv);
	if (err) {
		dev_err_probe(dev, err, "dpaa2_dpseci_dpio_setup() failed\n");
		goto err_dpio_setup;
	}

	/* DPSECI binding to DPIO */
	err = dpaa2_dpseci_bind(priv);
	if (err) {
		dev_err(dev, "dpaa2_dpseci_bind() failed\n");
		goto err_bind;
	}

	/* DPSECI enable */
	err = dpaa2_dpseci_enable(priv);
	if (err) {
		dev_err(dev, "dpaa2_dpseci_enable() failed\n");
		goto err_bind;
	}

	dpaa2_dpseci_debugfs_init(priv);

	/* register crypto algorithms the device supports */
	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		struct caam_skcipher_alg *t_alg = driver_algs + i;
		u32 alg_sel = t_alg->caam.class1_alg_type & OP_ALG_ALGSEL_MASK;

		/* Skip DES algorithms if not supported by device */
		if (!priv->sec_attr.des_acc_num &&
		    (alg_sel == OP_ALG_ALGSEL_3DES ||
		     alg_sel == OP_ALG_ALGSEL_DES))
			continue;

		/* Skip AES algorithms if not supported by device */
		if (!priv->sec_attr.aes_acc_num &&
		    alg_sel == OP_ALG_ALGSEL_AES)
			continue;

		/* Skip CHACHA20 algorithms if not supported by device */
		if (alg_sel == OP_ALG_ALGSEL_CHACHA20 &&
		    !priv->sec_attr.ccha_acc_num)
			continue;

		t_alg->caam.dev = dev;
		caam_skcipher_alg_init(t_alg);

		err = crypto_register_skcipher(&t_alg->skcipher);
		if (err) {
			dev_warn(dev, "%s alg registration failed: %d\n",
				 t_alg->skcipher.base.cra_driver_name, err);
			continue;
		}

		t_alg->registered = true;
		registered = true;
	}

	for (i = 0; i < ARRAY_SIZE(driver_aeads); i++) {
		struct caam_aead_alg *t_alg = driver_aeads + i;
		u32 c1_alg_sel = t_alg->caam.class1_alg_type &
				 OP_ALG_ALGSEL_MASK;
		u32 c2_alg_sel = t_alg->caam.class2_alg_type &
				 OP_ALG_ALGSEL_MASK;

		/* Skip DES algorithms if not supported by device */
		if (!priv->sec_attr.des_acc_num &&
		    (c1_alg_sel == OP_ALG_ALGSEL_3DES ||
		     c1_alg_sel == OP_ALG_ALGSEL_DES))
			continue;

		/* Skip AES algorithms if not supported by device */
		if (!priv->sec_attr.aes_acc_num &&
		    c1_alg_sel == OP_ALG_ALGSEL_AES)
			continue;

		/* Skip CHACHA20 algorithms if not supported by device */
		if (c1_alg_sel == OP_ALG_ALGSEL_CHACHA20 &&
		    !priv->sec_attr.ccha_acc_num)
			continue;

		/* Skip POLY1305 algorithms if not supported by device */
		if (c2_alg_sel == OP_ALG_ALGSEL_POLY1305 &&
		    !priv->sec_attr.ptha_acc_num)
			continue;

		/*
		 * Skip algorithms requiring message digests
		 * if MD not supported by device.
		 */
		if ((c2_alg_sel & ~OP_ALG_ALGSEL_SUBMASK) == 0x40 &&
		    !priv->sec_attr.md_acc_num)
			continue;

		t_alg->caam.dev = dev;
		caam_aead_alg_init(t_alg);

		err = crypto_register_aead(&t_alg->aead);
		if (err) {
			dev_warn(dev, "%s alg registration failed: %d\n",
				 t_alg->aead.base.cra_driver_name, err);
			continue;
		}

		t_alg->registered = true;
		registered = true;
	}
	if (registered)
		dev_info(dev, "algorithms registered in /proc/crypto\n");

	/* register hash algorithms the device supports */
	INIT_LIST_HEAD(&hash_list);

	/*
	 * Skip registration of any hashing algorithms if MD block
	 * is not present.
	 */
	if (!priv->sec_attr.md_acc_num)
		return 0;

	for (i = 0; i < ARRAY_SIZE(driver_hash); i++) {
		struct caam_hash_alg *t_alg;
		struct caam_hash_template *alg = driver_hash + i;

		/* register hmac version */
		t_alg = caam_hash_alloc(dev, alg, true);
		if (IS_ERR(t_alg)) {
			err = PTR_ERR(t_alg);
			dev_warn(dev, "%s hash alg allocation failed: %d\n",
				 alg->hmac_driver_name, err);
			continue;
		}

		err = crypto_register_ahash(&t_alg->ahash_alg);
		if (err) {
			dev_warn(dev, "%s alg registration failed: %d\n",
				 t_alg->ahash_alg.halg.base.cra_driver_name,
				 err);
			kfree(t_alg);
		} else {
			list_add_tail(&t_alg->entry, &hash_list);
		}

		/* register unkeyed version */
		t_alg = caam_hash_alloc(dev, alg, false);
		if (IS_ERR(t_alg)) {
			err = PTR_ERR(t_alg);
			dev_warn(dev, "%s alg allocation failed: %d\n",
				 alg->driver_name, err);
			continue;
		}

		err = crypto_register_ahash(&t_alg->ahash_alg);
		if (err) {
			dev_warn(dev, "%s alg registration failed: %d\n",
				 t_alg->ahash_alg.halg.base.cra_driver_name,
				 err);
			kfree(t_alg);
		} else {
			list_add_tail(&t_alg->entry, &hash_list);
		}
	}
	if (!list_empty(&hash_list))
		dev_info(dev, "hash algorithms registered in /proc/crypto\n");

	return err;

err_bind:
	dpaa2_dpseci_dpio_free(priv);
err_dpio_setup:
	dpaa2_dpseci_free(priv);
err_dpseci_setup:
	free_percpu(priv->ppriv);
err_alloc_ppriv:
	fsl_mc_portal_free(priv->mc_io);
err_dma_mask:
	kmem_cache_destroy(qi_cache);

	return err;
}

static int __cold dpaa2_caam_remove(struct fsl_mc_device *ls_dev)
{
	struct device *dev;
	struct dpaa2_caam_priv *priv;
	int i;

	dev = &ls_dev->dev;
	priv = dev_get_drvdata(dev);

	dpaa2_dpseci_debugfs_exit(priv);

	for (i = 0; i < ARRAY_SIZE(driver_aeads); i++) {
		struct caam_aead_alg *t_alg = driver_aeads + i;

		if (t_alg->registered)
			crypto_unregister_aead(&t_alg->aead);
	}

	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		struct caam_skcipher_alg *t_alg = driver_algs + i;

		if (t_alg->registered)
			crypto_unregister_skcipher(&t_alg->skcipher);
	}

	if (hash_list.next) {
		struct caam_hash_alg *t_hash_alg, *p;

		list_for_each_entry_safe(t_hash_alg, p, &hash_list, entry) {
			crypto_unregister_ahash(&t_hash_alg->ahash_alg);
			list_del(&t_hash_alg->entry);
			kfree(t_hash_alg);
		}
	}

	dpaa2_dpseci_disable(priv);
	dpaa2_dpseci_dpio_free(priv);
	dpaa2_dpseci_free(priv);
	free_percpu(priv->ppriv);
	fsl_mc_portal_free(priv->mc_io);
	kmem_cache_destroy(qi_cache);

	return 0;
}

int dpaa2_caam_enqueue(struct device *dev, struct caam_request *req)
{
	struct dpaa2_fd fd;
	struct dpaa2_caam_priv *priv = dev_get_drvdata(dev);
	struct dpaa2_caam_priv_per_cpu *ppriv;
	int err = 0, i;

	if (IS_ERR(req))
		return PTR_ERR(req);

	if (priv->cscn_mem) {
		dma_sync_single_for_cpu(priv->dev, priv->cscn_dma,
					DPAA2_CSCN_SIZE,
					DMA_FROM_DEVICE);
		if (unlikely(dpaa2_cscn_state_congested(priv->cscn_mem))) {
			dev_dbg_ratelimited(dev, "Dropping request\n");
			return -EBUSY;
		}
	}

	dpaa2_fl_set_flc(&req->fd_flt[1], req->flc_dma);

	req->fd_flt_dma = dma_map_single(dev, req->fd_flt, sizeof(req->fd_flt),
					 DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, req->fd_flt_dma)) {
		dev_err(dev, "DMA mapping error for QI enqueue request\n");
		goto err_out;
	}

	memset(&fd, 0, sizeof(fd));
	dpaa2_fd_set_format(&fd, dpaa2_fd_list);
	dpaa2_fd_set_addr(&fd, req->fd_flt_dma);
	dpaa2_fd_set_len(&fd, dpaa2_fl_get_len(&req->fd_flt[1]));
	dpaa2_fd_set_flc(&fd, req->flc_dma);

	ppriv = raw_cpu_ptr(priv->ppriv);
	for (i = 0; i < (priv->dpseci_attr.num_tx_queues << 1); i++) {
		err = dpaa2_io_service_enqueue_fq(ppriv->dpio, ppriv->req_fqid,
						  &fd);
		if (err != -EBUSY)
			break;

		cpu_relax();
	}

	if (unlikely(err)) {
		dev_err_ratelimited(dev, "Error enqueuing frame: %d\n", err);
		goto err_out;
	}

	return -EINPROGRESS;

err_out:
	dma_unmap_single(dev, req->fd_flt_dma, sizeof(req->fd_flt),
			 DMA_BIDIRECTIONAL);
	return -EIO;
}
EXPORT_SYMBOL(dpaa2_caam_enqueue);

static const struct fsl_mc_device_id dpaa2_caam_match_id_table[] = {
	{
		.vendor = FSL_MC_VENDOR_FREESCALE,
		.obj_type = "dpseci",
	},
	{ .vendor = 0x0 }
};
MODULE_DEVICE_TABLE(fslmc, dpaa2_caam_match_id_table);

static struct fsl_mc_driver dpaa2_caam_driver = {
	.driver = {
		.name		= KBUILD_MODNAME,
		.owner		= THIS_MODULE,
	},
	.probe		= dpaa2_caam_probe,
	.remove		= dpaa2_caam_remove,
	.match_id_table = dpaa2_caam_match_id_table
};

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Freescale Semiconductor, Inc");
MODULE_DESCRIPTION("Freescale DPAA2 CAAM Driver");

module_fsl_mc_driver(dpaa2_caam_driver);
