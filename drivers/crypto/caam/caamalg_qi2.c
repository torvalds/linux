// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2015-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2018 NXP
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
#include <linux/fsl/mc.h>
#include <soc/fsl/dpaa2-io.h>
#include <soc/fsl/dpaa2-fd.h>

#define CAAM_CRA_PRIORITY	2000

/* max key is sum of AES_MAX_KEY_SIZE, max split key size */
#define CAAM_MAX_KEY_SIZE	(AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE + \
				 SHA512_DIGEST_SIZE * 2)

#ifndef CONFIG_CRYPTO_DEV_FSL_CAAM
bool caam_little_end;
EXPORT_SYMBOL(caam_little_end);
bool caam_imx;
EXPORT_SYMBOL(caam_imx);
#endif

/*
 * This is a a cache of buffers, from which the users of CAAM QI driver
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
};

struct caam_aead_alg {
	struct aead_alg aead;
	struct caam_alg_entry caam;
	bool registered;
};

/**
 * caam_ctx - per-session context
 * @flc: Flow Contexts array
 * @key:  [authentication key], encryption key
 * @flc_dma: I/O virtual addresses of the Flow Contexts
 * @key_dma: I/O virtual address of the key
 * @dir: DMA direction for mapping key and Flow Contexts
 * @dev: dpseci device
 * @adata: authentication algorithm details
 * @cdata: encryption algorithm details
 * @authsize: authentication tag (a.k.a. ICV / MAC) size
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
		return skcipher_request_ctx(skcipher_request_cast(areq));
	case CRYPTO_ALG_TYPE_AEAD:
		return aead_request_ctx(container_of(areq, struct aead_request,
						     base));
	default:
		return ERR_PTR(-EINVAL);
	}
}

static void caam_unmap(struct device *dev, struct scatterlist *src,
		       struct scatterlist *dst, int src_nents,
		       int dst_nents, dma_addr_t iv_dma, int ivsize,
		       dma_addr_t qm_sg_dma, int qm_sg_bytes)
{
	if (dst != src) {
		if (src_nents)
			dma_unmap_sg(dev, src, src_nents, DMA_TO_DEVICE);
		dma_unmap_sg(dev, dst, dst_nents, DMA_FROM_DEVICE);
	} else {
		dma_unmap_sg(dev, src, src_nents, DMA_BIDIRECTIONAL);
	}

	if (iv_dma)
		dma_unmap_single(dev, iv_dma, ivsize, DMA_TO_DEVICE);

	if (qm_sg_bytes)
		dma_unmap_single(dev, qm_sg_dma, qm_sg_bytes, DMA_TO_DEVICE);
}

static int aead_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 typeof(*alg), aead);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
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

	data_len[0] = ctx->adata.keylen_pad;
	data_len[1] = ctx->cdata.keylen;

	/* aead_encrypt shared descriptor */
	if (desc_inline_query((alg->caam.geniv ? DESC_QI_AEAD_GIVENC_LEN :
						 DESC_QI_AEAD_ENC_LEN) +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	if (inl_mask & 1)
		ctx->adata.key_virt = ctx->key;
	else
		ctx->adata.key_dma = ctx->key_dma;

	if (inl_mask & 2)
		ctx->cdata.key_virt = ctx->key + ctx->adata.keylen_pad;
	else
		ctx->cdata.key_dma = ctx->key_dma + ctx->adata.keylen_pad;

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

	if (inl_mask & 1)
		ctx->adata.key_virt = ctx->key;
	else
		ctx->adata.key_dma = ctx->key_dma;

	if (inl_mask & 2)
		ctx->cdata.key_virt = ctx->key + ctx->adata.keylen_pad;
	else
		ctx->cdata.key_dma = ctx->key_dma + ctx->adata.keylen_pad;

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
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	aead_set_sh_desc(authenc);

	return 0;
}

static int aead_setkey(struct crypto_aead *aead, const u8 *key,
		       unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
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
	crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
	memzero_explicit(&keys, sizeof(keys));
	return -EINVAL;
}

static struct aead_edesc *aead_edesc_alloc(struct aead_request *req,
					   bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_request *req_ctx = aead_request_ctx(req);
	struct dpaa2_fl_entry *in_fle = &req_ctx->fd_flt[1];
	struct dpaa2_fl_entry *out_fle = &req_ctx->fd_flt[0];
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 typeof(*alg), aead);
	struct device *dev = ctx->dev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents = 0, mapped_dst_nents = 0;
	struct aead_edesc *edesc;
	dma_addr_t qm_sg_dma, iv_dma = 0;
	int ivsize = 0;
	unsigned int authsize = ctx->authsize;
	int qm_sg_index = 0, qm_sg_nents = 0, qm_sg_bytes;
	int in_len, out_len;
	struct dpaa2_sg_entry *sg_table;

	/* allocate space for base edesc, link tables and IV */
	edesc = qi_cache_zalloc(GFP_DMA | flags);
	if (unlikely(!edesc)) {
		dev_err(dev, "could not allocate extended descriptor\n");
		return ERR_PTR(-ENOMEM);
	}

	if (unlikely(req->dst != req->src)) {
		src_nents = sg_nents_for_len(req->src, req->assoclen +
					     req->cryptlen);
		if (unlikely(src_nents < 0)) {
			dev_err(dev, "Insufficient bytes (%d) in src S/G\n",
				req->assoclen + req->cryptlen);
			qi_cache_free(edesc);
			return ERR_PTR(src_nents);
		}

		dst_nents = sg_nents_for_len(req->dst, req->assoclen +
					     req->cryptlen +
					     (encrypt ? authsize :
							(-authsize)));
		if (unlikely(dst_nents < 0)) {
			dev_err(dev, "Insufficient bytes (%d) in dst S/G\n",
				req->assoclen + req->cryptlen +
				(encrypt ? authsize : (-authsize)));
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

		mapped_dst_nents = dma_map_sg(dev, req->dst, dst_nents,
					      DMA_FROM_DEVICE);
		if (unlikely(!mapped_dst_nents)) {
			dev_err(dev, "unable to map destination\n");
			dma_unmap_sg(dev, req->src, src_nents, DMA_TO_DEVICE);
			qi_cache_free(edesc);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		src_nents = sg_nents_for_len(req->src, req->assoclen +
					     req->cryptlen +
						(encrypt ? authsize : 0));
		if (unlikely(src_nents < 0)) {
			dev_err(dev, "Insufficient bytes (%d) in src S/G\n",
				req->assoclen + req->cryptlen +
				(encrypt ? authsize : 0));
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
	 */
	qm_sg_nents = 1 + !!ivsize + mapped_src_nents +
		      (mapped_dst_nents > 1 ? mapped_dst_nents : 0);
	sg_table = &edesc->sgt[0];
	qm_sg_bytes = qm_sg_nents * sizeof(*sg_table);
	if (unlikely(offsetof(struct aead_edesc, sgt) + qm_sg_bytes + ivsize >
		     CAAM_QI_MEMCACHE_SIZE)) {
		dev_err(dev, "No space for %d S/G entries and/or %dB IV\n",
			qm_sg_nents, ivsize);
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, 0, 0);
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
				   dst_nents, 0, 0, 0, 0);
			qi_cache_free(edesc);
			return ERR_PTR(-ENOMEM);
		}
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;

	edesc->assoclen = cpu_to_caam32(req->assoclen);
	edesc->assoclen_dma = dma_map_single(dev, &edesc->assoclen, 4,
					     DMA_TO_DEVICE);
	if (dma_mapping_error(dev, edesc->assoclen_dma)) {
		dev_err(dev, "unable to map assoclen\n");
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	dma_to_qm_sg_one(sg_table, edesc->assoclen_dma, 4, 0);
	qm_sg_index++;
	if (ivsize) {
		dma_to_qm_sg_one(sg_table + qm_sg_index, iv_dma, ivsize, 0);
		qm_sg_index++;
	}
	sg_to_qm_sg_last(req->src, mapped_src_nents, sg_table + qm_sg_index, 0);
	qm_sg_index += mapped_src_nents;

	if (mapped_dst_nents > 1)
		sg_to_qm_sg_last(req->dst, mapped_dst_nents, sg_table +
				 qm_sg_index, 0);

	qm_sg_dma = dma_map_single(dev, sg_table, qm_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, qm_sg_dma)) {
		dev_err(dev, "unable to map S/G table\n");
		dma_unmap_single(dev, edesc->assoclen_dma, 4, DMA_TO_DEVICE);
		caam_unmap(dev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, 0, 0);
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
	} else if (mapped_dst_nents == 1) {
		dpaa2_fl_set_format(out_fle, dpaa2_fl_single);
		dpaa2_fl_set_addr(out_fle, sg_dma_address(req->dst));
	} else {
		dpaa2_fl_set_format(out_fle, dpaa2_fl_sg);
		dpaa2_fl_set_addr(out_fle, qm_sg_dma + qm_sg_index *
				  sizeof(*sg_table));
	}

	dpaa2_fl_set_len(out_fle, out_len);

	return edesc;
}

static int gcm_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
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
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	gcm_set_sh_desc(authenc);

	return 0;
}

static int gcm_setkey(struct crypto_aead *aead,
		      const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *dev = ctx->dev;

	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	memcpy(ctx->key, key, keylen);
	dma_sync_single_for_device(dev, ctx->key_dma, keylen, ctx->dir);
	ctx->cdata.keylen = keylen;

	return gcm_set_sh_desc(aead);
}

static int rfc4106_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
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
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	rfc4106_set_sh_desc(authenc);

	return 0;
}

static int rfc4106_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *dev = ctx->dev;

	if (keylen < 4)
		return -EINVAL;

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
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
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
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	rfc4543_set_sh_desc(authenc);

	return 0;
}

static int rfc4543_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *dev = ctx->dev;

	if (keylen < 4)
		return -EINVAL;

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

static void aead_unmap(struct device *dev, struct aead_edesc *edesc,
		       struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	int ivsize = crypto_aead_ivsize(aead);

	caam_unmap(dev, req->src, req->dst, edesc->src_nents, edesc->dst_nents,
		   edesc->iv_dma, ivsize, edesc->qm_sg_dma, edesc->qm_sg_bytes);
	dma_unmap_single(dev, edesc->assoclen_dma, 4, DMA_TO_DEVICE);
}

static void aead_encrypt_done(void *cbk_ctx, u32 status)
{
	struct crypto_async_request *areq = cbk_ctx;
	struct aead_request *req = container_of(areq, struct aead_request,
						base);
	struct caam_request *req_ctx = to_caam_req(areq);
	struct aead_edesc *edesc = req_ctx->edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	int ecode = 0;

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status)) {
		caam_qi2_strstatus(ctx->dev, status);
		ecode = -EIO;
	}

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
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	int ecode = 0;

	dev_dbg(ctx->dev, "%s %d: err 0x%x\n", __func__, __LINE__, status);

	if (unlikely(status)) {
		caam_qi2_strstatus(ctx->dev, status);
		/*
		 * verify hw auth check passed else return -EBADMSG
		 */
		if ((status & JRSTA_CCBERR_ERRID_MASK) ==
		     JRSTA_CCBERR_ERRID_ICVCHK)
			ecode = -EBADMSG;
		else
			ecode = -EIO;
	}

	aead_unmap(ctx->dev, edesc, req);
	qi_cache_free(edesc);
	aead_request_complete(req, ecode);
}

static int aead_encrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct caam_request *caam_req = aead_request_ctx(req);
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
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct caam_request *caam_req = aead_request_ctx(req);
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
	if (req->assoclen < 8)
		return -EINVAL;

	return aead_encrypt(req);
}

static int ipsec_gcm_decrypt(struct aead_request *req)
{
	if (req->assoclen < 8)
		return -EINVAL;

	return aead_decrypt(req);
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

static int caam_cra_init_aead(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct caam_aead_alg *caam_alg = container_of(alg, typeof(*caam_alg),
						      aead);

	crypto_aead_set_reqsize(tfm, sizeof(struct caam_request));
	return caam_cra_init(crypto_aead_ctx(tfm), &caam_alg->caam,
			     alg->setkey == aead_setkey);
}

static void caam_exit_common(struct caam_ctx *ctx)
{
	dma_unmap_single_attrs(ctx->dev, ctx->flc_dma[0],
			       offsetof(struct caam_ctx, flc_dma), ctx->dir,
			       DMA_ATTR_SKIP_CPU_SYNC);
}

static void caam_cra_exit_aead(struct crypto_aead *tfm)
{
	caam_exit_common(crypto_aead_ctx(tfm));
}

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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
			.setkey = aead_setkey,
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
						   "hmac-sha256-cbc-desi-"
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

static void caam_aead_alg_init(struct caam_aead_alg *t_alg)
{
	struct aead_alg *alg = &t_alg->aead;

	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = CAAM_CRA_PRIORITY;
	alg->base.cra_ctxsize = sizeof(struct caam_ctx);
	alg->base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY;

	alg->init = caam_cra_init_aead;
	alg->exit = caam_cra_exit_aead;
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
		err = dpaa2_io_service_register(NULL, nctx);
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
		dpaa2_io_service_deregister(NULL, &ppriv->nctx);
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
		dpaa2_io_service_deregister(NULL, &ppriv->nctx);
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
		dev_err(priv->dev, "FD error: %08x\n", fd_err);

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
		err = dpaa2_io_service_pull_fq(NULL, ppriv->rsp_fqid,
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
		err = dpaa2_io_service_rearm(NULL, &ppriv->nctx);
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
	int err;

	/*
	 * Congestion group feature supported starting with DPSECI API v5.1
	 * and only when object has been created with this capability.
	 */
	if ((DPSECI_VER(priv->major_ver, priv->minor_ver) < DPSECI_VER(5, 1)) ||
	    !(priv->dpseci_attr.options & DPSECI_OPT_HAS_CG))
		return 0;

	priv->cscn_mem = kzalloc(DPAA2_CSCN_SIZE + DPAA2_CSCN_ALIGN,
				 GFP_KERNEL | GFP_DMA);
	if (!priv->cscn_mem)
		return -ENOMEM;

	priv->cscn_mem_aligned = PTR_ALIGN(priv->cscn_mem, DPAA2_CSCN_ALIGN);
	priv->cscn_dma = dma_map_single(dev, priv->cscn_mem_aligned,
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
		dev_dbg(dev, "pair %d: rx queue %d, tx queue %d\n", i,
			priv->rx_queue_attr[i].fqid,
			priv->tx_queue_attr[i].fqid);

		ppriv = per_cpu_ptr(priv->ppriv, cpu);
		ppriv->req_fqid = priv->tx_queue_attr[i].fqid;
		ppriv->rsp_fqid = priv->rx_queue_attr[i].fqid;
		ppriv->prio = i;

		ppriv->net_dev.dev = *dev;
		INIT_LIST_HEAD(&ppriv->net_dev.napi_list);
		netif_napi_add(&ppriv->net_dev, &ppriv->napi, dpaa2_dpseci_poll,
			       DPAA2_CAAM_NAPI_WEIGHT);
		if (++i == priv->num_pairs)
			break;
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
				     0, SLAB_CACHE_DMA, NULL);
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
		if (err != -EPROBE_DEFER)
			dev_err(dev, "dpaa2_dpseci_dpio_setup() failed\n");
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

	/* register crypto algorithms the device supports */
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

		/*
		 * Skip algorithms requiring message digests
		 * if MD not supported by device.
		 */
		if (!priv->sec_attr.md_acc_num && c2_alg_sel)
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

	for (i = 0; i < ARRAY_SIZE(driver_aeads); i++) {
		struct caam_aead_alg *t_alg = driver_aeads + i;

		if (t_alg->registered)
			crypto_unregister_aead(&t_alg->aead);
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
	int err = 0, i, id;

	if (IS_ERR(req))
		return PTR_ERR(req);

	if (priv->cscn_mem) {
		dma_sync_single_for_cpu(priv->dev, priv->cscn_dma,
					DPAA2_CSCN_SIZE,
					DMA_FROM_DEVICE);
		if (unlikely(dpaa2_cscn_state_congested(priv->cscn_mem_aligned))) {
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

	/*
	 * There is no guarantee that preemption is disabled here,
	 * thus take action.
	 */
	preempt_disable();
	id = smp_processor_id() % priv->dpseci_attr.num_tx_queues;
	for (i = 0; i < (priv->dpseci_attr.num_tx_queues << 1); i++) {
		err = dpaa2_io_service_enqueue_fq(NULL,
						  priv->tx_queue_attr[id].fqid,
						  &fd);
		if (err != -EBUSY)
			break;
	}
	preempt_enable();

	if (unlikely(err)) {
		dev_err(dev, "Error enqueuing frame: %d\n", err);
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
