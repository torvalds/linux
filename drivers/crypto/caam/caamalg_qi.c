/*
 * Freescale FSL CAAM support for crypto API over QI backend.
 * Based on caamalg.c
 *
 * Copyright 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 */

#include "compat.h"

#include "regs.h"
#include "intern.h"
#include "desc_constr.h"
#include "error.h"
#include "sg_sw_sec4.h"
#include "sg_sw_qm.h"
#include "key_gen.h"
#include "qi.h"
#include "jr.h"
#include "caamalg_desc.h"

/*
 * crypto alg
 */
#define CAAM_CRA_PRIORITY		2000
/* max key is sum of AES_MAX_KEY_SIZE, max split key size */
#define CAAM_MAX_KEY_SIZE		(AES_MAX_KEY_SIZE + \
					 SHA512_DIGEST_SIZE * 2)

#define DESC_MAX_USED_BYTES		(DESC_QI_AEAD_GIVENC_LEN + \
					 CAAM_MAX_KEY_SIZE)
#define DESC_MAX_USED_LEN		(DESC_MAX_USED_BYTES / CAAM_CMD_SZ)

struct caam_alg_entry {
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

/*
 * per-session context
 */
struct caam_ctx {
	struct device *jrdev;
	u32 sh_desc_enc[DESC_MAX_USED_LEN];
	u32 sh_desc_dec[DESC_MAX_USED_LEN];
	u32 sh_desc_givenc[DESC_MAX_USED_LEN];
	u8 key[CAAM_MAX_KEY_SIZE];
	dma_addr_t key_dma;
	struct alginfo adata;
	struct alginfo cdata;
	unsigned int authsize;
	struct device *qidev;
	spinlock_t lock;	/* Protects multiple init of driver context */
	struct caam_drv_ctx *drv_ctx[NUM_OP];
};

static int aead_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 typeof(*alg), aead);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
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

	if (alg->caam.geniv)
		goto skip_enc;

	/* aead_encrypt shared descriptor */
	if (desc_inline_query(DESC_QI_AEAD_ENC_LEN +
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

	cnstr_shdsc_aead_encap(ctx->sh_desc_enc, &ctx->cdata, &ctx->adata,
			       ivsize, ctx->authsize, is_rfc3686, nonce,
			       ctx1_iv_off, true);

skip_enc:
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

	cnstr_shdsc_aead_decap(ctx->sh_desc_dec, &ctx->cdata, &ctx->adata,
			       ivsize, ctx->authsize, alg->caam.geniv,
			       is_rfc3686, nonce, ctx1_iv_off, true);

	if (!alg->caam.geniv)
		goto skip_givenc;

	/* aead_givencrypt shared descriptor */
	if (desc_inline_query(DESC_QI_AEAD_GIVENC_LEN +
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

	cnstr_shdsc_aead_givencap(ctx->sh_desc_enc, &ctx->cdata, &ctx->adata,
				  ivsize, ctx->authsize, is_rfc3686, nonce,
				  ctx1_iv_off, true);

skip_givenc:
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
	struct device *jrdev = ctx->jrdev;
	struct crypto_authenc_keys keys;
	int ret = 0;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto badkey;

#ifdef DEBUG
	dev_err(jrdev, "keylen %d enckeylen %d authkeylen %d\n",
		keys.authkeylen + keys.enckeylen, keys.enckeylen,
		keys.authkeylen);
	print_hex_dump(KERN_ERR, "key in @" __stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);
#endif

	ret = gen_split_key(jrdev, ctx->key, &ctx->adata, keys.authkey,
			    keys.authkeylen, CAAM_MAX_KEY_SIZE -
			    keys.enckeylen);
	if (ret)
		goto badkey;

	/* postpend encryption key to auth split key */
	memcpy(ctx->key + ctx->adata.keylen_pad, keys.enckey, keys.enckeylen);
	dma_sync_single_for_device(jrdev, ctx->key_dma, ctx->adata.keylen_pad +
				   keys.enckeylen, DMA_TO_DEVICE);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ctx.key@" __stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, ctx->key,
		       ctx->adata.keylen_pad + keys.enckeylen, 1);
#endif

	ctx->cdata.keylen = keys.enckeylen;

	ret = aead_set_sh_desc(aead);
	if (ret)
		goto badkey;

	/* Now update the driver contexts with the new shared descriptor */
	if (ctx->drv_ctx[ENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[ENCRYPT],
					  ctx->sh_desc_enc);
		if (ret) {
			dev_err(jrdev, "driver enc context update failed\n");
			goto badkey;
		}
	}

	if (ctx->drv_ctx[DECRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[DECRYPT],
					  ctx->sh_desc_dec);
		if (ret) {
			dev_err(jrdev, "driver dec context update failed\n");
			goto badkey;
		}
	}

	return ret;
badkey:
	crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

static int ablkcipher_setkey(struct crypto_ablkcipher *ablkcipher,
			     const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(ablkcipher);
	const char *alg_name = crypto_tfm_alg_name(tfm);
	struct device *jrdev = ctx->jrdev;
	unsigned int ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	u32 ctx1_iv_off = 0;
	const bool ctr_mode = ((ctx->cdata.algtype & OP_ALG_AAI_MASK) ==
			       OP_ALG_AAI_CTR_MOD128);
	const bool is_rfc3686 = (ctr_mode && strstr(alg_name, "rfc3686"));
	int ret = 0;

	memcpy(ctx->key, key, keylen);
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "key in @" __stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);
#endif
	/*
	 * AES-CTR needs to load IV in CONTEXT1 reg
	 * at an offset of 128bits (16bytes)
	 * CONTEXT1[255:128] = IV
	 */
	if (ctr_mode)
		ctx1_iv_off = 16;

	/*
	 * RFC3686 specific:
	 *	| CONTEXT1[255:128] = {NONCE, IV, COUNTER}
	 *	| *key = {KEY, NONCE}
	 */
	if (is_rfc3686) {
		ctx1_iv_off = 16 + CTR_RFC3686_NONCE_SIZE;
		keylen -= CTR_RFC3686_NONCE_SIZE;
	}

	dma_sync_single_for_device(jrdev, ctx->key_dma, keylen, DMA_TO_DEVICE);
	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = ctx->key;
	ctx->cdata.key_inline = true;

	/* ablkcipher encrypt, decrypt, givencrypt shared descriptors */
	cnstr_shdsc_ablkcipher_encap(ctx->sh_desc_enc, &ctx->cdata, ivsize,
				     is_rfc3686, ctx1_iv_off);
	cnstr_shdsc_ablkcipher_decap(ctx->sh_desc_dec, &ctx->cdata, ivsize,
				     is_rfc3686, ctx1_iv_off);
	cnstr_shdsc_ablkcipher_givencap(ctx->sh_desc_givenc, &ctx->cdata,
					ivsize, is_rfc3686, ctx1_iv_off);

	/* Now update the driver contexts with the new shared descriptor */
	if (ctx->drv_ctx[ENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[ENCRYPT],
					  ctx->sh_desc_enc);
		if (ret) {
			dev_err(jrdev, "driver enc context update failed\n");
			goto badkey;
		}
	}

	if (ctx->drv_ctx[DECRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[DECRYPT],
					  ctx->sh_desc_dec);
		if (ret) {
			dev_err(jrdev, "driver dec context update failed\n");
			goto badkey;
		}
	}

	if (ctx->drv_ctx[GIVENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[GIVENCRYPT],
					  ctx->sh_desc_givenc);
		if (ret) {
			dev_err(jrdev, "driver givenc context update failed\n");
			goto badkey;
		}
	}

	return ret;
badkey:
	crypto_ablkcipher_set_flags(ablkcipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

static int xts_ablkcipher_setkey(struct crypto_ablkcipher *ablkcipher,
				 const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *jrdev = ctx->jrdev;
	int ret = 0;

	if (keylen != 2 * AES_MIN_KEY_SIZE  && keylen != 2 * AES_MAX_KEY_SIZE) {
		crypto_ablkcipher_set_flags(ablkcipher,
					    CRYPTO_TFM_RES_BAD_KEY_LEN);
		dev_err(jrdev, "key size mismatch\n");
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	dma_sync_single_for_device(jrdev, ctx->key_dma, keylen, DMA_TO_DEVICE);
	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = ctx->key;
	ctx->cdata.key_inline = true;

	/* xts ablkcipher encrypt, decrypt shared descriptors */
	cnstr_shdsc_xts_ablkcipher_encap(ctx->sh_desc_enc, &ctx->cdata);
	cnstr_shdsc_xts_ablkcipher_decap(ctx->sh_desc_dec, &ctx->cdata);

	/* Now update the driver contexts with the new shared descriptor */
	if (ctx->drv_ctx[ENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[ENCRYPT],
					  ctx->sh_desc_enc);
		if (ret) {
			dev_err(jrdev, "driver enc context update failed\n");
			goto badkey;
		}
	}

	if (ctx->drv_ctx[DECRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[DECRYPT],
					  ctx->sh_desc_dec);
		if (ret) {
			dev_err(jrdev, "driver dec context update failed\n");
			goto badkey;
		}
	}

	return ret;
badkey:
	crypto_ablkcipher_set_flags(ablkcipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return 0;
}

/*
 * aead_edesc - s/w-extended aead descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @iv_dma: dma address of iv for checking continuity and link table
 * @qm_sg_bytes: length of dma mapped h/w link table
 * @qm_sg_dma: bus physical mapped address of h/w link table
 * @assoclen_dma: bus physical mapped address of req->assoclen
 * @drv_req: driver-specific request structure
 * @sgt: the h/w link table
 */
struct aead_edesc {
	int src_nents;
	int dst_nents;
	dma_addr_t iv_dma;
	int qm_sg_bytes;
	dma_addr_t qm_sg_dma;
	dma_addr_t assoclen_dma;
	struct caam_drv_req drv_req;
#define CAAM_QI_MAX_AEAD_SG						\
	((CAAM_QI_MEMCACHE_SIZE - offsetof(struct aead_edesc, sgt)) /	\
	 sizeof(struct qm_sg_entry))
	struct qm_sg_entry sgt[0];
};

/*
 * ablkcipher_edesc - s/w-extended ablkcipher descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @iv_dma: dma address of iv for checking continuity and link table
 * @qm_sg_bytes: length of dma mapped h/w link table
 * @qm_sg_dma: bus physical mapped address of h/w link table
 * @drv_req: driver-specific request structure
 * @sgt: the h/w link table
 */
struct ablkcipher_edesc {
	int src_nents;
	int dst_nents;
	dma_addr_t iv_dma;
	int qm_sg_bytes;
	dma_addr_t qm_sg_dma;
	struct caam_drv_req drv_req;
#define CAAM_QI_MAX_ABLKCIPHER_SG					    \
	((CAAM_QI_MEMCACHE_SIZE - offsetof(struct ablkcipher_edesc, sgt)) / \
	 sizeof(struct qm_sg_entry))
	struct qm_sg_entry sgt[0];
};

static struct caam_drv_ctx *get_drv_ctx(struct caam_ctx *ctx,
					enum optype type)
{
	/*
	 * This function is called on the fast path with values of 'type'
	 * known at compile time. Invalid arguments are not expected and
	 * thus no checks are made.
	 */
	struct caam_drv_ctx *drv_ctx = ctx->drv_ctx[type];
	u32 *desc;

	if (unlikely(!drv_ctx)) {
		spin_lock(&ctx->lock);

		/* Read again to check if some other core init drv_ctx */
		drv_ctx = ctx->drv_ctx[type];
		if (!drv_ctx) {
			int cpu;

			if (type == ENCRYPT)
				desc = ctx->sh_desc_enc;
			else if (type == DECRYPT)
				desc = ctx->sh_desc_dec;
			else /* (type == GIVENCRYPT) */
				desc = ctx->sh_desc_givenc;

			cpu = smp_processor_id();
			drv_ctx = caam_drv_ctx_init(ctx->qidev, &cpu, desc);
			if (likely(!IS_ERR_OR_NULL(drv_ctx)))
				drv_ctx->op_type = type;

			ctx->drv_ctx[type] = drv_ctx;
		}

		spin_unlock(&ctx->lock);
	}

	return drv_ctx;
}

static void caam_unmap(struct device *dev, struct scatterlist *src,
		       struct scatterlist *dst, int src_nents,
		       int dst_nents, dma_addr_t iv_dma, int ivsize,
		       enum optype op_type, dma_addr_t qm_sg_dma,
		       int qm_sg_bytes)
{
	if (dst != src) {
		if (src_nents)
			dma_unmap_sg(dev, src, src_nents, DMA_TO_DEVICE);
		dma_unmap_sg(dev, dst, dst_nents, DMA_FROM_DEVICE);
	} else {
		dma_unmap_sg(dev, src, src_nents, DMA_BIDIRECTIONAL);
	}

	if (iv_dma)
		dma_unmap_single(dev, iv_dma, ivsize,
				 op_type == GIVENCRYPT ? DMA_FROM_DEVICE :
							 DMA_TO_DEVICE);
	if (qm_sg_bytes)
		dma_unmap_single(dev, qm_sg_dma, qm_sg_bytes, DMA_TO_DEVICE);
}

static void aead_unmap(struct device *dev,
		       struct aead_edesc *edesc,
		       struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	int ivsize = crypto_aead_ivsize(aead);

	caam_unmap(dev, req->src, req->dst, edesc->src_nents, edesc->dst_nents,
		   edesc->iv_dma, ivsize, edesc->drv_req.drv_ctx->op_type,
		   edesc->qm_sg_dma, edesc->qm_sg_bytes);
	dma_unmap_single(dev, edesc->assoclen_dma, 4, DMA_TO_DEVICE);
}

static void ablkcipher_unmap(struct device *dev,
			     struct ablkcipher_edesc *edesc,
			     struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);

	caam_unmap(dev, req->src, req->dst, edesc->src_nents, edesc->dst_nents,
		   edesc->iv_dma, ivsize, edesc->drv_req.drv_ctx->op_type,
		   edesc->qm_sg_dma, edesc->qm_sg_bytes);
}

static void aead_done(struct caam_drv_req *drv_req, u32 status)
{
	struct device *qidev;
	struct aead_edesc *edesc;
	struct aead_request *aead_req = drv_req->app_ctx;
	struct crypto_aead *aead = crypto_aead_reqtfm(aead_req);
	struct caam_ctx *caam_ctx = crypto_aead_ctx(aead);
	int ecode = 0;

	qidev = caam_ctx->qidev;

	if (unlikely(status)) {
		caam_jr_strstatus(qidev, status);
		ecode = -EIO;
	}

	edesc = container_of(drv_req, typeof(*edesc), drv_req);
	aead_unmap(qidev, edesc, aead_req);

	aead_request_complete(aead_req, ecode);
	qi_cache_free(edesc);
}

/*
 * allocate and map the aead extended descriptor
 */
static struct aead_edesc *aead_edesc_alloc(struct aead_request *req,
					   bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 typeof(*alg), aead);
	struct device *qidev = ctx->qidev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents = 0, mapped_dst_nents = 0;
	struct aead_edesc *edesc;
	dma_addr_t qm_sg_dma, iv_dma = 0;
	int ivsize = 0;
	unsigned int authsize = ctx->authsize;
	int qm_sg_index = 0, qm_sg_ents = 0, qm_sg_bytes;
	int in_len, out_len;
	struct qm_sg_entry *sg_table, *fd_sgt;
	struct caam_drv_ctx *drv_ctx;
	enum optype op_type = encrypt ? ENCRYPT : DECRYPT;

	drv_ctx = get_drv_ctx(ctx, op_type);
	if (unlikely(IS_ERR_OR_NULL(drv_ctx)))
		return (struct aead_edesc *)drv_ctx;

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = qi_cache_alloc(GFP_DMA | flags);
	if (unlikely(!edesc)) {
		dev_err(qidev, "could not allocate extended descriptor\n");
		return ERR_PTR(-ENOMEM);
	}

	if (likely(req->src == req->dst)) {
		src_nents = sg_nents_for_len(req->src, req->assoclen +
					     req->cryptlen +
						(encrypt ? authsize : 0));
		if (unlikely(src_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in src S/G\n",
				req->assoclen + req->cryptlen +
				(encrypt ? authsize : 0));
			qi_cache_free(edesc);
			return ERR_PTR(src_nents);
		}

		mapped_src_nents = dma_map_sg(qidev, req->src, src_nents,
					      DMA_BIDIRECTIONAL);
		if (unlikely(!mapped_src_nents)) {
			dev_err(qidev, "unable to map source\n");
			qi_cache_free(edesc);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		src_nents = sg_nents_for_len(req->src, req->assoclen +
					     req->cryptlen);
		if (unlikely(src_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in src S/G\n",
				req->assoclen + req->cryptlen);
			qi_cache_free(edesc);
			return ERR_PTR(src_nents);
		}

		dst_nents = sg_nents_for_len(req->dst, req->assoclen +
					     req->cryptlen +
					     (encrypt ? authsize :
							(-authsize)));
		if (unlikely(dst_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in dst S/G\n",
				req->assoclen + req->cryptlen +
				(encrypt ? authsize : (-authsize)));
			qi_cache_free(edesc);
			return ERR_PTR(dst_nents);
		}

		if (src_nents) {
			mapped_src_nents = dma_map_sg(qidev, req->src,
						      src_nents, DMA_TO_DEVICE);
			if (unlikely(!mapped_src_nents)) {
				dev_err(qidev, "unable to map source\n");
				qi_cache_free(edesc);
				return ERR_PTR(-ENOMEM);
			}
		} else {
			mapped_src_nents = 0;
		}

		mapped_dst_nents = dma_map_sg(qidev, req->dst, dst_nents,
					      DMA_FROM_DEVICE);
		if (unlikely(!mapped_dst_nents)) {
			dev_err(qidev, "unable to map destination\n");
			dma_unmap_sg(qidev, req->src, src_nents, DMA_TO_DEVICE);
			qi_cache_free(edesc);
			return ERR_PTR(-ENOMEM);
		}
	}

	if ((alg->caam.rfc3686 && encrypt) || !alg->caam.geniv) {
		ivsize = crypto_aead_ivsize(aead);
		iv_dma = dma_map_single(qidev, req->iv, ivsize, DMA_TO_DEVICE);
		if (dma_mapping_error(qidev, iv_dma)) {
			dev_err(qidev, "unable to map IV\n");
			caam_unmap(qidev, req->src, req->dst, src_nents,
				   dst_nents, 0, 0, op_type, 0, 0);
			qi_cache_free(edesc);
			return ERR_PTR(-ENOMEM);
		}
	}

	/*
	 * Create S/G table: req->assoclen, [IV,] req->src [, req->dst].
	 * Input is not contiguous.
	 */
	qm_sg_ents = 1 + !!ivsize + mapped_src_nents +
		     (mapped_dst_nents > 1 ? mapped_dst_nents : 0);
	if (unlikely(qm_sg_ents > CAAM_QI_MAX_AEAD_SG)) {
		dev_err(qidev, "Insufficient S/G entries: %d > %lu\n",
			qm_sg_ents, CAAM_QI_MAX_AEAD_SG);
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, op_type, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}
	sg_table = &edesc->sgt[0];
	qm_sg_bytes = qm_sg_ents * sizeof(*sg_table);

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;
	edesc->drv_req.app_ctx = req;
	edesc->drv_req.cbk = aead_done;
	edesc->drv_req.drv_ctx = drv_ctx;

	edesc->assoclen_dma = dma_map_single(qidev, &req->assoclen, 4,
					     DMA_TO_DEVICE);
	if (dma_mapping_error(qidev, edesc->assoclen_dma)) {
		dev_err(qidev, "unable to map assoclen\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, op_type, 0, 0);
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

	qm_sg_dma = dma_map_single(qidev, sg_table, qm_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(qidev, qm_sg_dma)) {
		dev_err(qidev, "unable to map S/G table\n");
		dma_unmap_single(qidev, edesc->assoclen_dma, 4, DMA_TO_DEVICE);
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, op_type, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	edesc->qm_sg_dma = qm_sg_dma;
	edesc->qm_sg_bytes = qm_sg_bytes;

	out_len = req->assoclen + req->cryptlen +
		  (encrypt ? ctx->authsize : (-ctx->authsize));
	in_len = 4 + ivsize + req->assoclen + req->cryptlen;

	fd_sgt = &edesc->drv_req.fd_sgt[0];
	dma_to_qm_sg_one_last_ext(&fd_sgt[1], qm_sg_dma, in_len, 0);

	if (req->dst == req->src) {
		if (mapped_src_nents == 1)
			dma_to_qm_sg_one(&fd_sgt[0], sg_dma_address(req->src),
					 out_len, 0);
		else
			dma_to_qm_sg_one_ext(&fd_sgt[0], qm_sg_dma +
					     (1 + !!ivsize) * sizeof(*sg_table),
					     out_len, 0);
	} else if (mapped_dst_nents == 1) {
		dma_to_qm_sg_one(&fd_sgt[0], sg_dma_address(req->dst), out_len,
				 0);
	} else {
		dma_to_qm_sg_one_ext(&fd_sgt[0], qm_sg_dma + sizeof(*sg_table) *
				     qm_sg_index, out_len, 0);
	}

	return edesc;
}

static inline int aead_crypt(struct aead_request *req, bool encrypt)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	int ret;

	if (unlikely(caam_congested))
		return -EAGAIN;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, encrypt);
	if (IS_ERR_OR_NULL(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor */
	ret = caam_qi_enqueue(ctx->qidev, &edesc->drv_req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(ctx->qidev, edesc, req);
		qi_cache_free(edesc);
	}

	return ret;
}

static int aead_encrypt(struct aead_request *req)
{
	return aead_crypt(req, true);
}

static int aead_decrypt(struct aead_request *req)
{
	return aead_crypt(req, false);
}

static void ablkcipher_done(struct caam_drv_req *drv_req, u32 status)
{
	struct ablkcipher_edesc *edesc;
	struct ablkcipher_request *req = drv_req->app_ctx;
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *caam_ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *qidev = caam_ctx->qidev;
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);

#ifdef DEBUG
	dev_err(qidev, "%s %d: status 0x%x\n", __func__, __LINE__, status);
#endif

	edesc = container_of(drv_req, typeof(*edesc), drv_req);

	if (status)
		caam_jr_strstatus(qidev, status);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "dstiv  @" __stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, req->info,
		       edesc->src_nents > 1 ? 100 : ivsize, 1);
	caam_dump_sg(KERN_ERR, "dst    @" __stringify(__LINE__)": ",
		     DUMP_PREFIX_ADDRESS, 16, 4, req->dst,
		     edesc->dst_nents > 1 ? 100 : req->nbytes, 1);
#endif

	ablkcipher_unmap(qidev, edesc, req);
	qi_cache_free(edesc);

	/*
	 * The crypto API expects us to set the IV (req->info) to the last
	 * ciphertext block. This is used e.g. by the CTS mode.
	 */
	scatterwalk_map_and_copy(req->info, req->dst, req->nbytes - ivsize,
				 ivsize, 0);

	ablkcipher_request_complete(req, status);
}

static struct ablkcipher_edesc *ablkcipher_edesc_alloc(struct ablkcipher_request
						       *req, bool encrypt)
{
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *qidev = ctx->qidev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents = 0, mapped_dst_nents = 0;
	struct ablkcipher_edesc *edesc;
	dma_addr_t iv_dma;
	bool in_contig;
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	int dst_sg_idx, qm_sg_ents;
	struct qm_sg_entry *sg_table, *fd_sgt;
	struct caam_drv_ctx *drv_ctx;
	enum optype op_type = encrypt ? ENCRYPT : DECRYPT;

	drv_ctx = get_drv_ctx(ctx, op_type);
	if (unlikely(IS_ERR_OR_NULL(drv_ctx)))
		return (struct ablkcipher_edesc *)drv_ctx;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (unlikely(src_nents < 0)) {
		dev_err(qidev, "Insufficient bytes (%d) in src S/G\n",
			req->nbytes);
		return ERR_PTR(src_nents);
	}

	if (unlikely(req->src != req->dst)) {
		dst_nents = sg_nents_for_len(req->dst, req->nbytes);
		if (unlikely(dst_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in dst S/G\n",
				req->nbytes);
			return ERR_PTR(dst_nents);
		}

		mapped_src_nents = dma_map_sg(qidev, req->src, src_nents,
					      DMA_TO_DEVICE);
		if (unlikely(!mapped_src_nents)) {
			dev_err(qidev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}

		mapped_dst_nents = dma_map_sg(qidev, req->dst, dst_nents,
					      DMA_FROM_DEVICE);
		if (unlikely(!mapped_dst_nents)) {
			dev_err(qidev, "unable to map destination\n");
			dma_unmap_sg(qidev, req->src, src_nents, DMA_TO_DEVICE);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		mapped_src_nents = dma_map_sg(qidev, req->src, src_nents,
					      DMA_BIDIRECTIONAL);
		if (unlikely(!mapped_src_nents)) {
			dev_err(qidev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	iv_dma = dma_map_single(qidev, req->info, ivsize, DMA_TO_DEVICE);
	if (dma_mapping_error(qidev, iv_dma)) {
		dev_err(qidev, "unable to map IV\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, 0, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	if (mapped_src_nents == 1 &&
	    iv_dma + ivsize == sg_dma_address(req->src)) {
		in_contig = true;
		qm_sg_ents = 0;
	} else {
		in_contig = false;
		qm_sg_ents = 1 + mapped_src_nents;
	}
	dst_sg_idx = qm_sg_ents;

	qm_sg_ents += mapped_dst_nents > 1 ? mapped_dst_nents : 0;
	if (unlikely(qm_sg_ents > CAAM_QI_MAX_ABLKCIPHER_SG)) {
		dev_err(qidev, "Insufficient S/G entries: %d > %lu\n",
			qm_sg_ents, CAAM_QI_MAX_ABLKCIPHER_SG);
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, op_type, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	/* allocate space for base edesc and link tables */
	edesc = qi_cache_alloc(GFP_DMA | flags);
	if (unlikely(!edesc)) {
		dev_err(qidev, "could not allocate extended descriptor\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, op_type, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;
	sg_table = &edesc->sgt[0];
	edesc->qm_sg_bytes = qm_sg_ents * sizeof(*sg_table);
	edesc->drv_req.app_ctx = req;
	edesc->drv_req.cbk = ablkcipher_done;
	edesc->drv_req.drv_ctx = drv_ctx;

	if (!in_contig) {
		dma_to_qm_sg_one(sg_table, iv_dma, ivsize, 0);
		sg_to_qm_sg_last(req->src, mapped_src_nents, sg_table + 1, 0);
	}

	if (mapped_dst_nents > 1)
		sg_to_qm_sg_last(req->dst, mapped_dst_nents, sg_table +
				 dst_sg_idx, 0);

	edesc->qm_sg_dma = dma_map_single(qidev, sg_table, edesc->qm_sg_bytes,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(qidev, edesc->qm_sg_dma)) {
		dev_err(qidev, "unable to map S/G table\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, op_type, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	fd_sgt = &edesc->drv_req.fd_sgt[0];

	if (!in_contig)
		dma_to_qm_sg_one_last_ext(&fd_sgt[1], edesc->qm_sg_dma,
					  ivsize + req->nbytes, 0);
	else
		dma_to_qm_sg_one_last(&fd_sgt[1], iv_dma, ivsize + req->nbytes,
				      0);

	if (req->src == req->dst) {
		if (!in_contig)
			dma_to_qm_sg_one_ext(&fd_sgt[0], edesc->qm_sg_dma +
					     sizeof(*sg_table), req->nbytes, 0);
		else
			dma_to_qm_sg_one(&fd_sgt[0], sg_dma_address(req->src),
					 req->nbytes, 0);
	} else if (mapped_dst_nents > 1) {
		dma_to_qm_sg_one_ext(&fd_sgt[0], edesc->qm_sg_dma + dst_sg_idx *
				     sizeof(*sg_table), req->nbytes, 0);
	} else {
		dma_to_qm_sg_one(&fd_sgt[0], sg_dma_address(req->dst),
				 req->nbytes, 0);
	}

	return edesc;
}

static struct ablkcipher_edesc *ablkcipher_giv_edesc_alloc(
	struct skcipher_givcrypt_request *creq)
{
	struct ablkcipher_request *req = &creq->creq;
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	struct device *qidev = ctx->qidev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents, mapped_dst_nents;
	struct ablkcipher_edesc *edesc;
	dma_addr_t iv_dma;
	bool out_contig;
	int ivsize = crypto_ablkcipher_ivsize(ablkcipher);
	struct qm_sg_entry *sg_table, *fd_sgt;
	int dst_sg_idx, qm_sg_ents;
	struct caam_drv_ctx *drv_ctx;

	drv_ctx = get_drv_ctx(ctx, GIVENCRYPT);
	if (unlikely(IS_ERR_OR_NULL(drv_ctx)))
		return (struct ablkcipher_edesc *)drv_ctx;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (unlikely(src_nents < 0)) {
		dev_err(qidev, "Insufficient bytes (%d) in src S/G\n",
			req->nbytes);
		return ERR_PTR(src_nents);
	}

	if (unlikely(req->src != req->dst)) {
		dst_nents = sg_nents_for_len(req->dst, req->nbytes);
		if (unlikely(dst_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in dst S/G\n",
				req->nbytes);
			return ERR_PTR(dst_nents);
		}

		mapped_src_nents = dma_map_sg(qidev, req->src, src_nents,
					      DMA_TO_DEVICE);
		if (unlikely(!mapped_src_nents)) {
			dev_err(qidev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}

		mapped_dst_nents = dma_map_sg(qidev, req->dst, dst_nents,
					      DMA_FROM_DEVICE);
		if (unlikely(!mapped_dst_nents)) {
			dev_err(qidev, "unable to map destination\n");
			dma_unmap_sg(qidev, req->src, src_nents, DMA_TO_DEVICE);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		mapped_src_nents = dma_map_sg(qidev, req->src, src_nents,
					      DMA_BIDIRECTIONAL);
		if (unlikely(!mapped_src_nents)) {
			dev_err(qidev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}

		dst_nents = src_nents;
		mapped_dst_nents = src_nents;
	}

	iv_dma = dma_map_single(qidev, creq->giv, ivsize, DMA_FROM_DEVICE);
	if (dma_mapping_error(qidev, iv_dma)) {
		dev_err(qidev, "unable to map IV\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, 0, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	qm_sg_ents = mapped_src_nents > 1 ? mapped_src_nents : 0;
	dst_sg_idx = qm_sg_ents;
	if (mapped_dst_nents == 1 &&
	    iv_dma + ivsize == sg_dma_address(req->dst)) {
		out_contig = true;
	} else {
		out_contig = false;
		qm_sg_ents += 1 + mapped_dst_nents;
	}

	if (unlikely(qm_sg_ents > CAAM_QI_MAX_ABLKCIPHER_SG)) {
		dev_err(qidev, "Insufficient S/G entries: %d > %lu\n",
			qm_sg_ents, CAAM_QI_MAX_ABLKCIPHER_SG);
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, GIVENCRYPT, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	/* allocate space for base edesc and link tables */
	edesc = qi_cache_alloc(GFP_DMA | flags);
	if (!edesc) {
		dev_err(qidev, "could not allocate extended descriptor\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, GIVENCRYPT, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;
	sg_table = &edesc->sgt[0];
	edesc->qm_sg_bytes = qm_sg_ents * sizeof(*sg_table);
	edesc->drv_req.app_ctx = req;
	edesc->drv_req.cbk = ablkcipher_done;
	edesc->drv_req.drv_ctx = drv_ctx;

	if (mapped_src_nents > 1)
		sg_to_qm_sg_last(req->src, mapped_src_nents, sg_table, 0);

	if (!out_contig) {
		dma_to_qm_sg_one(sg_table + dst_sg_idx, iv_dma, ivsize, 0);
		sg_to_qm_sg_last(req->dst, mapped_dst_nents, sg_table +
				 dst_sg_idx + 1, 0);
	}

	edesc->qm_sg_dma = dma_map_single(qidev, sg_table, edesc->qm_sg_bytes,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(qidev, edesc->qm_sg_dma)) {
		dev_err(qidev, "unable to map S/G table\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, GIVENCRYPT, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	fd_sgt = &edesc->drv_req.fd_sgt[0];

	if (mapped_src_nents > 1)
		dma_to_qm_sg_one_ext(&fd_sgt[1], edesc->qm_sg_dma, req->nbytes,
				     0);
	else
		dma_to_qm_sg_one(&fd_sgt[1], sg_dma_address(req->src),
				 req->nbytes, 0);

	if (!out_contig)
		dma_to_qm_sg_one_ext(&fd_sgt[0], edesc->qm_sg_dma + dst_sg_idx *
				     sizeof(*sg_table), ivsize + req->nbytes,
				     0);
	else
		dma_to_qm_sg_one(&fd_sgt[0], sg_dma_address(req->dst),
				 ivsize + req->nbytes, 0);

	return edesc;
}

static inline int ablkcipher_crypt(struct ablkcipher_request *req, bool encrypt)
{
	struct ablkcipher_edesc *edesc;
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	int ret;

	if (unlikely(caam_congested))
		return -EAGAIN;

	/* allocate extended descriptor */
	edesc = ablkcipher_edesc_alloc(req, encrypt);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	ret = caam_qi_enqueue(ctx->qidev, &edesc->drv_req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		ablkcipher_unmap(ctx->qidev, edesc, req);
		qi_cache_free(edesc);
	}

	return ret;
}

static int ablkcipher_encrypt(struct ablkcipher_request *req)
{
	return ablkcipher_crypt(req, true);
}

static int ablkcipher_decrypt(struct ablkcipher_request *req)
{
	return ablkcipher_crypt(req, false);
}

static int ablkcipher_givencrypt(struct skcipher_givcrypt_request *creq)
{
	struct ablkcipher_request *req = &creq->creq;
	struct ablkcipher_edesc *edesc;
	struct crypto_ablkcipher *ablkcipher = crypto_ablkcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_ablkcipher_ctx(ablkcipher);
	int ret;

	if (unlikely(caam_congested))
		return -EAGAIN;

	/* allocate extended descriptor */
	edesc = ablkcipher_giv_edesc_alloc(creq);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	ret = caam_qi_enqueue(ctx->qidev, &edesc->drv_req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		ablkcipher_unmap(ctx->qidev, edesc, req);
		qi_cache_free(edesc);
	}

	return ret;
}

#define template_ablkcipher	template_u.ablkcipher
struct caam_alg_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	u32 type;
	union {
		struct ablkcipher_alg ablkcipher;
	} template_u;
	u32 class1_alg_type;
	u32 class2_alg_type;
};

static struct caam_alg_template driver_algs[] = {
	/* ablkcipher descriptor */
	{
		.name = "cbc(aes)",
		.driver_name = "cbc-aes-caam-qi",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_GIVCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.givencrypt = ablkcipher_givencrypt,
			.geniv = "<built-in>",
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
		.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CBC,
	},
	{
		.name = "cbc(des3_ede)",
		.driver_name = "cbc-3des-caam-qi",
		.blocksize = DES3_EDE_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_GIVCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.givencrypt = ablkcipher_givencrypt,
			.geniv = "<built-in>",
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = DES3_EDE_BLOCK_SIZE,
		},
		.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_CBC,
	},
	{
		.name = "cbc(des)",
		.driver_name = "cbc-des-caam-qi",
		.blocksize = DES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_GIVCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.givencrypt = ablkcipher_givencrypt,
			.geniv = "<built-in>",
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = DES_BLOCK_SIZE,
		},
		.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_CBC,
	},
	{
		.name = "ctr(aes)",
		.driver_name = "ctr-aes-caam-qi",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.geniv = "chainiv",
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
		.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CTR_MOD128,
	},
	{
		.name = "rfc3686(ctr(aes))",
		.driver_name = "rfc3686-ctr-aes-caam-qi",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_GIVCIPHER,
		.template_ablkcipher = {
			.setkey = ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.givencrypt = ablkcipher_givencrypt,
			.geniv = "<built-in>",
			.min_keysize = AES_MIN_KEY_SIZE +
				       CTR_RFC3686_NONCE_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE +
				       CTR_RFC3686_NONCE_SIZE,
			.ivsize = CTR_RFC3686_IV_SIZE,
		},
		.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CTR_MOD128,
	},
	{
		.name = "xts(aes)",
		.driver_name = "xts-aes-caam-qi",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = xts_ablkcipher_setkey,
			.encrypt = ablkcipher_encrypt,
			.decrypt = ablkcipher_decrypt,
			.geniv = "eseqiv",
			.min_keysize = 2 * AES_MIN_KEY_SIZE,
			.max_keysize = 2 * AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
		.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_XTS,
	},
};

static struct caam_aead_alg driver_aeads[] = {
	/* single-pass ipsec_esp descriptor */
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(aes))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-aes-caam-qi",
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
						   "cbc-aes-caam-qi",
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
						   "cbc-aes-caam-qi",
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
						   "hmac-sha1-cbc-aes-caam-qi",
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
						   "cbc-aes-caam-qi",
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
						   "hmac-sha224-cbc-aes-caam-qi",
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
						   "cbc-aes-caam-qi",
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
						   "caam-qi",
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
						   "cbc-aes-caam-qi",
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
						   "caam-qi",
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
						   "cbc-aes-caam-qi",
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
						   "caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des-caam-qi",
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
						   "cbc-des-caam-qi",
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
						   "cbc-des-caam-qi",
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
						   "hmac-sha1-cbc-des-caam-qi",
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
						   "cbc-des-caam-qi",
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
						   "caam-qi",
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
						   "cbc-des-caam-qi",
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
						   "caam-qi",
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
						   "cbc-des-caam-qi",
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
						   "caam-qi",
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
						   "cbc-des-caam-qi",
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
						   "caam-qi",
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
};

struct caam_crypto_alg {
	struct list_head entry;
	struct crypto_alg crypto_alg;
	struct caam_alg_entry caam;
};

static int caam_init_common(struct caam_ctx *ctx, struct caam_alg_entry *caam)
{
	struct caam_drv_private *priv;

	/*
	 * distribute tfms across job rings to ensure in-order
	 * crypto request processing per tfm
	 */
	ctx->jrdev = caam_jr_alloc();
	if (IS_ERR(ctx->jrdev)) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return PTR_ERR(ctx->jrdev);
	}

	ctx->key_dma = dma_map_single(ctx->jrdev, ctx->key, sizeof(ctx->key),
				      DMA_TO_DEVICE);
	if (dma_mapping_error(ctx->jrdev, ctx->key_dma)) {
		dev_err(ctx->jrdev, "unable to map key\n");
		caam_jr_free(ctx->jrdev);
		return -ENOMEM;
	}

	/* copy descriptor header template value */
	ctx->cdata.algtype = OP_TYPE_CLASS1_ALG | caam->class1_alg_type;
	ctx->adata.algtype = OP_TYPE_CLASS2_ALG | caam->class2_alg_type;

	priv = dev_get_drvdata(ctx->jrdev->parent);
	ctx->qidev = priv->qidev;

	spin_lock_init(&ctx->lock);
	ctx->drv_ctx[ENCRYPT] = NULL;
	ctx->drv_ctx[DECRYPT] = NULL;
	ctx->drv_ctx[GIVENCRYPT] = NULL;

	return 0;
}

static int caam_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct caam_crypto_alg *caam_alg = container_of(alg, typeof(*caam_alg),
							crypto_alg);
	struct caam_ctx *ctx = crypto_tfm_ctx(tfm);

	return caam_init_common(ctx, &caam_alg->caam);
}

static int caam_aead_init(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct caam_aead_alg *caam_alg = container_of(alg, typeof(*caam_alg),
						      aead);
	struct caam_ctx *ctx = crypto_aead_ctx(tfm);

	return caam_init_common(ctx, &caam_alg->caam);
}

static void caam_exit_common(struct caam_ctx *ctx)
{
	caam_drv_ctx_rel(ctx->drv_ctx[ENCRYPT]);
	caam_drv_ctx_rel(ctx->drv_ctx[DECRYPT]);
	caam_drv_ctx_rel(ctx->drv_ctx[GIVENCRYPT]);

	dma_unmap_single(ctx->jrdev, ctx->key_dma, sizeof(ctx->key),
			 DMA_TO_DEVICE);

	caam_jr_free(ctx->jrdev);
}

static void caam_cra_exit(struct crypto_tfm *tfm)
{
	caam_exit_common(crypto_tfm_ctx(tfm));
}

static void caam_aead_exit(struct crypto_aead *tfm)
{
	caam_exit_common(crypto_aead_ctx(tfm));
}

static struct list_head alg_list;
static void __exit caam_qi_algapi_exit(void)
{
	struct caam_crypto_alg *t_alg, *n;
	int i;

	for (i = 0; i < ARRAY_SIZE(driver_aeads); i++) {
		struct caam_aead_alg *t_alg = driver_aeads + i;

		if (t_alg->registered)
			crypto_unregister_aead(&t_alg->aead);
	}

	if (!alg_list.next)
		return;

	list_for_each_entry_safe(t_alg, n, &alg_list, entry) {
		crypto_unregister_alg(&t_alg->crypto_alg);
		list_del(&t_alg->entry);
		kfree(t_alg);
	}
}

static struct caam_crypto_alg *caam_alg_alloc(struct caam_alg_template
					      *template)
{
	struct caam_crypto_alg *t_alg;
	struct crypto_alg *alg;

	t_alg = kzalloc(sizeof(*t_alg), GFP_KERNEL);
	if (!t_alg)
		return ERR_PTR(-ENOMEM);

	alg = &t_alg->crypto_alg;

	snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s", template->name);
	snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 template->driver_name);
	alg->cra_module = THIS_MODULE;
	alg->cra_init = caam_cra_init;
	alg->cra_exit = caam_cra_exit;
	alg->cra_priority = CAAM_CRA_PRIORITY;
	alg->cra_blocksize = template->blocksize;
	alg->cra_alignmask = 0;
	alg->cra_ctxsize = sizeof(struct caam_ctx);
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY |
			 template->type;
	switch (template->type) {
	case CRYPTO_ALG_TYPE_GIVCIPHER:
		alg->cra_type = &crypto_givcipher_type;
		alg->cra_ablkcipher = template->template_ablkcipher;
		break;
	case CRYPTO_ALG_TYPE_ABLKCIPHER:
		alg->cra_type = &crypto_ablkcipher_type;
		alg->cra_ablkcipher = template->template_ablkcipher;
		break;
	}

	t_alg->caam.class1_alg_type = template->class1_alg_type;
	t_alg->caam.class2_alg_type = template->class2_alg_type;

	return t_alg;
}

static void caam_aead_alg_init(struct caam_aead_alg *t_alg)
{
	struct aead_alg *alg = &t_alg->aead;

	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = CAAM_CRA_PRIORITY;
	alg->base.cra_ctxsize = sizeof(struct caam_ctx);
	alg->base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY;

	alg->init = caam_aead_init;
	alg->exit = caam_aead_exit;
}

static int __init caam_qi_algapi_init(void)
{
	struct device_node *dev_node;
	struct platform_device *pdev;
	struct device *ctrldev;
	struct caam_drv_private *priv;
	int i = 0, err = 0;
	u32 cha_vid, cha_inst, des_inst, aes_inst, md_inst;
	unsigned int md_limit = SHA512_DIGEST_SIZE;
	bool registered = false;

	dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	if (!dev_node) {
		dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec4.0");
		if (!dev_node)
			return -ENODEV;
	}

	pdev = of_find_device_by_node(dev_node);
	of_node_put(dev_node);
	if (!pdev)
		return -ENODEV;

	ctrldev = &pdev->dev;
	priv = dev_get_drvdata(ctrldev);

	/*
	 * If priv is NULL, it's probably because the caam driver wasn't
	 * properly initialized (e.g. RNG4 init failed). Thus, bail out here.
	 */
	if (!priv || !priv->qi_present)
		return -ENODEV;

	INIT_LIST_HEAD(&alg_list);

	/*
	 * Register crypto algorithms the device supports.
	 * First, detect presence and attributes of DES, AES, and MD blocks.
	 */
	cha_vid = rd_reg32(&priv->ctrl->perfmon.cha_id_ls);
	cha_inst = rd_reg32(&priv->ctrl->perfmon.cha_num_ls);
	des_inst = (cha_inst & CHA_ID_LS_DES_MASK) >> CHA_ID_LS_DES_SHIFT;
	aes_inst = (cha_inst & CHA_ID_LS_AES_MASK) >> CHA_ID_LS_AES_SHIFT;
	md_inst = (cha_inst & CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT;

	/* If MD is present, limit digest size based on LP256 */
	if (md_inst && ((cha_vid & CHA_ID_LS_MD_MASK) == CHA_ID_LS_MD_LP256))
		md_limit = SHA256_DIGEST_SIZE;

	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		struct caam_crypto_alg *t_alg;
		struct caam_alg_template *alg = driver_algs + i;
		u32 alg_sel = alg->class1_alg_type & OP_ALG_ALGSEL_MASK;

		/* Skip DES algorithms if not supported by device */
		if (!des_inst &&
		    ((alg_sel == OP_ALG_ALGSEL_3DES) ||
		     (alg_sel == OP_ALG_ALGSEL_DES)))
			continue;

		/* Skip AES algorithms if not supported by device */
		if (!aes_inst && (alg_sel == OP_ALG_ALGSEL_AES))
			continue;

		t_alg = caam_alg_alloc(alg);
		if (IS_ERR(t_alg)) {
			err = PTR_ERR(t_alg);
			dev_warn(priv->qidev, "%s alg allocation failed\n",
				 alg->driver_name);
			continue;
		}

		err = crypto_register_alg(&t_alg->crypto_alg);
		if (err) {
			dev_warn(priv->qidev, "%s alg registration failed\n",
				 t_alg->crypto_alg.cra_driver_name);
			kfree(t_alg);
			continue;
		}

		list_add_tail(&t_alg->entry, &alg_list);
		registered = true;
	}

	for (i = 0; i < ARRAY_SIZE(driver_aeads); i++) {
		struct caam_aead_alg *t_alg = driver_aeads + i;
		u32 c1_alg_sel = t_alg->caam.class1_alg_type &
				 OP_ALG_ALGSEL_MASK;
		u32 c2_alg_sel = t_alg->caam.class2_alg_type &
				 OP_ALG_ALGSEL_MASK;
		u32 alg_aai = t_alg->caam.class1_alg_type & OP_ALG_AAI_MASK;

		/* Skip DES algorithms if not supported by device */
		if (!des_inst &&
		    ((c1_alg_sel == OP_ALG_ALGSEL_3DES) ||
		     (c1_alg_sel == OP_ALG_ALGSEL_DES)))
			continue;

		/* Skip AES algorithms if not supported by device */
		if (!aes_inst && (c1_alg_sel == OP_ALG_ALGSEL_AES))
			continue;

		/*
		 * Check support for AES algorithms not available
		 * on LP devices.
		 */
		if (((cha_vid & CHA_ID_LS_AES_MASK) == CHA_ID_LS_AES_LP) &&
		    (alg_aai == OP_ALG_AAI_GCM))
			continue;

		/*
		 * Skip algorithms requiring message digests
		 * if MD or MD size is not supported by device.
		 */
		if (c2_alg_sel &&
		    (!md_inst || (t_alg->aead.maxauthsize > md_limit)))
			continue;

		caam_aead_alg_init(t_alg);

		err = crypto_register_aead(&t_alg->aead);
		if (err) {
			pr_warn("%s alg registration failed\n",
				t_alg->aead.base.cra_driver_name);
			continue;
		}

		t_alg->registered = true;
		registered = true;
	}

	if (registered)
		dev_info(priv->qidev, "algorithms registered in /proc/crypto\n");

	return err;
}

module_init(caam_qi_algapi_init);
module_exit(caam_qi_algapi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Support for crypto API using CAAM-QI backend");
MODULE_AUTHOR("Freescale Semiconductor");
