// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale FSL CAAM support for crypto API over QI backend.
 * Based on caamalg.c
 *
 * Copyright 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2019 NXP
 */

#include "compat.h"
#include "ctrl.h"
#include "regs.h"
#include "intern.h"
#include "desc_constr.h"
#include "error.h"
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

/*
 * per-session context
 */
struct caam_ctx {
	struct device *jrdev;
	u32 sh_desc_enc[DESC_MAX_USED_LEN];
	u32 sh_desc_dec[DESC_MAX_USED_LEN];
	u8 key[CAAM_MAX_KEY_SIZE];
	dma_addr_t key_dma;
	enum dma_data_direction dir;
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
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctx->jrdev->parent);

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

	if (alg->caam.geniv)
		goto skip_enc;

	/* aead_encrypt shared descriptor */
	if (desc_inline_query(DESC_QI_AEAD_ENC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	cnstr_shdsc_aead_encap(ctx->sh_desc_enc, &ctx->cdata, &ctx->adata,
			       ivsize, ctx->authsize, is_rfc3686, nonce,
			       ctx1_iv_off, true, ctrlpriv->era);

skip_enc:
	/* aead_decrypt shared descriptor */
	if (desc_inline_query(DESC_QI_AEAD_DEC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	cnstr_shdsc_aead_decap(ctx->sh_desc_dec, &ctx->cdata, &ctx->adata,
			       ivsize, ctx->authsize, alg->caam.geniv,
			       is_rfc3686, nonce, ctx1_iv_off, true,
			       ctrlpriv->era);

	if (!alg->caam.geniv)
		goto skip_givenc;

	/* aead_givencrypt shared descriptor */
	if (desc_inline_query(DESC_QI_AEAD_GIVENC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	cnstr_shdsc_aead_givencap(ctx->sh_desc_enc, &ctx->cdata, &ctx->adata,
				  ivsize, ctx->authsize, is_rfc3686, nonce,
				  ctx1_iv_off, true, ctrlpriv->era);

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
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(jrdev->parent);
	struct crypto_authenc_keys keys;
	int ret = 0;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto badkey;

	dev_dbg(jrdev, "keylen %d enckeylen %d authkeylen %d\n",
		keys.authkeylen + keys.enckeylen, keys.enckeylen,
		keys.authkeylen);
	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	/*
	 * If DKP is supported, use it in the shared descriptor to generate
	 * the split key.
	 */
	if (ctrlpriv->era >= 6) {
		ctx->adata.keylen = keys.authkeylen;
		ctx->adata.keylen_pad = split_key_len(ctx->adata.algtype &
						      OP_ALG_ALGSEL_MASK);

		if (ctx->adata.keylen_pad + keys.enckeylen > CAAM_MAX_KEY_SIZE)
			goto badkey;

		memcpy(ctx->key, keys.authkey, keys.authkeylen);
		memcpy(ctx->key + ctx->adata.keylen_pad, keys.enckey,
		       keys.enckeylen);
		dma_sync_single_for_device(jrdev->parent, ctx->key_dma,
					   ctx->adata.keylen_pad +
					   keys.enckeylen, ctx->dir);
		goto skip_split_key;
	}

	ret = gen_split_key(jrdev, ctx->key, &ctx->adata, keys.authkey,
			    keys.authkeylen, CAAM_MAX_KEY_SIZE -
			    keys.enckeylen);
	if (ret)
		goto badkey;

	/* postpend encryption key to auth split key */
	memcpy(ctx->key + ctx->adata.keylen_pad, keys.enckey, keys.enckeylen);
	dma_sync_single_for_device(jrdev->parent, ctx->key_dma,
				   ctx->adata.keylen_pad + keys.enckeylen,
				   ctx->dir);

	print_hex_dump_debug("ctx.key@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, ctx->key,
			     ctx->adata.keylen_pad + keys.enckeylen, 1);

skip_split_key:
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

	memzero_explicit(&keys, sizeof(keys));
	return ret;
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
		return err;

	err = verify_aead_des3_key(aead, keys.enckey, keys.enckeylen) ?:
	      aead_setkey(aead, key, keylen);

	memzero_explicit(&keys, sizeof(keys));
	return err;
}

static int gcm_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	int rem_bytes = CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
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

	cnstr_shdsc_gcm_encap(ctx->sh_desc_enc, &ctx->cdata, ivsize,
			      ctx->authsize, true);

	/*
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_GCM_DEC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	cnstr_shdsc_gcm_decap(ctx->sh_desc_dec, &ctx->cdata, ivsize,
			      ctx->authsize, true);

	return 0;
}

static int gcm_setauthsize(struct crypto_aead *authenc, unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);
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
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	int ret;

	ret = aes_check_keylen(keylen);
	if (ret)
		return ret;

	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	memcpy(ctx->key, key, keylen);
	dma_sync_single_for_device(jrdev->parent, ctx->key_dma, keylen,
				   ctx->dir);
	ctx->cdata.keylen = keylen;

	ret = gcm_set_sh_desc(aead);
	if (ret)
		return ret;

	/* Now update the driver contexts with the new shared descriptor */
	if (ctx->drv_ctx[ENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[ENCRYPT],
					  ctx->sh_desc_enc);
		if (ret) {
			dev_err(jrdev, "driver enc context update failed\n");
			return ret;
		}
	}

	if (ctx->drv_ctx[DECRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[DECRYPT],
					  ctx->sh_desc_dec);
		if (ret) {
			dev_err(jrdev, "driver dec context update failed\n");
			return ret;
		}
	}

	return 0;
}

static int rfc4106_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	int rem_bytes = CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	ctx->cdata.key_virt = ctx->key;

	/*
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_RFC4106_ENC_LEN) {
		ctx->cdata.key_inline = true;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	cnstr_shdsc_rfc4106_encap(ctx->sh_desc_enc, &ctx->cdata, ivsize,
				  ctx->authsize, true);

	/*
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_RFC4106_DEC_LEN) {
		ctx->cdata.key_inline = true;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	cnstr_shdsc_rfc4106_decap(ctx->sh_desc_dec, &ctx->cdata, ivsize,
				  ctx->authsize, true);

	return 0;
}

static int rfc4106_setauthsize(struct crypto_aead *authenc,
			       unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);
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
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
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
	dma_sync_single_for_device(jrdev->parent, ctx->key_dma,
				   ctx->cdata.keylen, ctx->dir);

	ret = rfc4106_set_sh_desc(aead);
	if (ret)
		return ret;

	/* Now update the driver contexts with the new shared descriptor */
	if (ctx->drv_ctx[ENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[ENCRYPT],
					  ctx->sh_desc_enc);
		if (ret) {
			dev_err(jrdev, "driver enc context update failed\n");
			return ret;
		}
	}

	if (ctx->drv_ctx[DECRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[DECRYPT],
					  ctx->sh_desc_dec);
		if (ret) {
			dev_err(jrdev, "driver dec context update failed\n");
			return ret;
		}
	}

	return 0;
}

static int rfc4543_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	int rem_bytes = CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	ctx->cdata.key_virt = ctx->key;

	/*
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_RFC4543_ENC_LEN) {
		ctx->cdata.key_inline = true;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	cnstr_shdsc_rfc4543_encap(ctx->sh_desc_enc, &ctx->cdata, ivsize,
				  ctx->authsize, true);

	/*
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_QI_RFC4543_DEC_LEN) {
		ctx->cdata.key_inline = true;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	cnstr_shdsc_rfc4543_decap(ctx->sh_desc_dec, &ctx->cdata, ivsize,
				  ctx->authsize, true);

	return 0;
}

static int rfc4543_setauthsize(struct crypto_aead *authenc,
			       unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	if (authsize != 16)
		return -EINVAL;

	ctx->authsize = authsize;
	rfc4543_set_sh_desc(authenc);

	return 0;
}

static int rfc4543_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
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
	dma_sync_single_for_device(jrdev->parent, ctx->key_dma,
				   ctx->cdata.keylen, ctx->dir);

	ret = rfc4543_set_sh_desc(aead);
	if (ret)
		return ret;

	/* Now update the driver contexts with the new shared descriptor */
	if (ctx->drv_ctx[ENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[ENCRYPT],
					  ctx->sh_desc_enc);
		if (ret) {
			dev_err(jrdev, "driver enc context update failed\n");
			return ret;
		}
	}

	if (ctx->drv_ctx[DECRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[DECRYPT],
					  ctx->sh_desc_dec);
		if (ret) {
			dev_err(jrdev, "driver dec context update failed\n");
			return ret;
		}
	}

	return 0;
}

static int skcipher_setkey(struct crypto_skcipher *skcipher, const u8 *key,
			   unsigned int keylen, const u32 ctx1_iv_off)
{
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct caam_skcipher_alg *alg =
		container_of(crypto_skcipher_alg(skcipher), typeof(*alg),
			     skcipher);
	struct device *jrdev = ctx->jrdev;
	unsigned int ivsize = crypto_skcipher_ivsize(skcipher);
	const bool is_rfc3686 = alg->caam.rfc3686;
	int ret = 0;

	print_hex_dump_debug("key in @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = key;
	ctx->cdata.key_inline = true;

	/* skcipher encrypt, decrypt shared descriptors */
	cnstr_shdsc_skcipher_encap(ctx->sh_desc_enc, &ctx->cdata, ivsize,
				   is_rfc3686, ctx1_iv_off);
	cnstr_shdsc_skcipher_decap(ctx->sh_desc_dec, &ctx->cdata, ivsize,
				   is_rfc3686, ctx1_iv_off);

	/* Now update the driver contexts with the new shared descriptor */
	if (ctx->drv_ctx[ENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[ENCRYPT],
					  ctx->sh_desc_enc);
		if (ret) {
			dev_err(jrdev, "driver enc context update failed\n");
			return -EINVAL;
		}
	}

	if (ctx->drv_ctx[DECRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[DECRYPT],
					  ctx->sh_desc_dec);
		if (ret) {
			dev_err(jrdev, "driver dec context update failed\n");
			return -EINVAL;
		}
	}

	return ret;
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

static int des3_skcipher_setkey(struct crypto_skcipher *skcipher,
				const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des3_key(skcipher, key) ?:
	       skcipher_setkey(skcipher, key, keylen, 0);
}

static int des_skcipher_setkey(struct crypto_skcipher *skcipher,
			       const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des_key(skcipher, key) ?:
	       skcipher_setkey(skcipher, key, keylen, 0);
}

static int xts_skcipher_setkey(struct crypto_skcipher *skcipher, const u8 *key,
			       unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct device *jrdev = ctx->jrdev;
	int ret = 0;

	if (keylen != 2 * AES_MIN_KEY_SIZE  && keylen != 2 * AES_MAX_KEY_SIZE) {
		dev_err(jrdev, "key size mismatch\n");
		return -EINVAL;
	}

	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = key;
	ctx->cdata.key_inline = true;

	/* xts skcipher encrypt, decrypt shared descriptors */
	cnstr_shdsc_xts_skcipher_encap(ctx->sh_desc_enc, &ctx->cdata);
	cnstr_shdsc_xts_skcipher_decap(ctx->sh_desc_dec, &ctx->cdata);

	/* Now update the driver contexts with the new shared descriptor */
	if (ctx->drv_ctx[ENCRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[ENCRYPT],
					  ctx->sh_desc_enc);
		if (ret) {
			dev_err(jrdev, "driver enc context update failed\n");
			return -EINVAL;
		}
	}

	if (ctx->drv_ctx[DECRYPT]) {
		ret = caam_drv_ctx_update(ctx->drv_ctx[DECRYPT],
					  ctx->sh_desc_dec);
		if (ret) {
			dev_err(jrdev, "driver dec context update failed\n");
			return -EINVAL;
		}
	}

	return ret;
}

/*
 * aead_edesc - s/w-extended aead descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @iv_dma: dma address of iv for checking continuity and link table
 * @qm_sg_bytes: length of dma mapped h/w link table
 * @qm_sg_dma: bus physical mapped address of h/w link table
 * @assoclen: associated data length, in CAAM endianness
 * @assoclen_dma: bus physical mapped address of req->assoclen
 * @drv_req: driver-specific request structure
 * @sgt: the h/w link table, followed by IV
 */
struct aead_edesc {
	int src_nents;
	int dst_nents;
	dma_addr_t iv_dma;
	int qm_sg_bytes;
	dma_addr_t qm_sg_dma;
	unsigned int assoclen;
	dma_addr_t assoclen_dma;
	struct caam_drv_req drv_req;
	struct qm_sg_entry sgt[];
};

/*
 * skcipher_edesc - s/w-extended skcipher descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @iv_dma: dma address of iv for checking continuity and link table
 * @qm_sg_bytes: length of dma mapped h/w link table
 * @qm_sg_dma: bus physical mapped address of h/w link table
 * @drv_req: driver-specific request structure
 * @sgt: the h/w link table, followed by IV
 */
struct skcipher_edesc {
	int src_nents;
	int dst_nents;
	dma_addr_t iv_dma;
	int qm_sg_bytes;
	dma_addr_t qm_sg_dma;
	struct caam_drv_req drv_req;
	struct qm_sg_entry sgt[];
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
			else /* (type == DECRYPT) */
				desc = ctx->sh_desc_dec;

			cpu = smp_processor_id();
			drv_ctx = caam_drv_ctx_init(ctx->qidev, &cpu, desc);
			if (!IS_ERR_OR_NULL(drv_ctx))
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

static void aead_unmap(struct device *dev,
		       struct aead_edesc *edesc,
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

static void aead_done(struct caam_drv_req *drv_req, u32 status)
{
	struct device *qidev;
	struct aead_edesc *edesc;
	struct aead_request *aead_req = drv_req->app_ctx;
	struct crypto_aead *aead = crypto_aead_reqtfm(aead_req);
	struct caam_ctx *caam_ctx = crypto_aead_ctx(aead);
	int ecode = 0;

	qidev = caam_ctx->qidev;

	if (unlikely(status))
		ecode = caam_jr_strstatus(qidev, status);

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
	int src_len, dst_len = 0;
	struct aead_edesc *edesc;
	dma_addr_t qm_sg_dma, iv_dma = 0;
	int ivsize = 0;
	unsigned int authsize = ctx->authsize;
	int qm_sg_index = 0, qm_sg_ents = 0, qm_sg_bytes;
	int in_len, out_len;
	struct qm_sg_entry *sg_table, *fd_sgt;
	struct caam_drv_ctx *drv_ctx;

	drv_ctx = get_drv_ctx(ctx, encrypt ? ENCRYPT : DECRYPT);
	if (IS_ERR_OR_NULL(drv_ctx))
		return (struct aead_edesc *)drv_ctx;

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = qi_cache_alloc(GFP_DMA | flags);
	if (unlikely(!edesc)) {
		dev_err(qidev, "could not allocate extended descriptor\n");
		return ERR_PTR(-ENOMEM);
	}

	if (likely(req->src == req->dst)) {
		src_len = req->assoclen + req->cryptlen +
			  (encrypt ? authsize : 0);

		src_nents = sg_nents_for_len(req->src, src_len);
		if (unlikely(src_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in src S/G\n",
				src_len);
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
		src_len = req->assoclen + req->cryptlen;
		dst_len = src_len + (encrypt ? authsize : (-authsize));

		src_nents = sg_nents_for_len(req->src, src_len);
		if (unlikely(src_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in src S/G\n",
				src_len);
			qi_cache_free(edesc);
			return ERR_PTR(src_nents);
		}

		dst_nents = sg_nents_for_len(req->dst, dst_len);
		if (unlikely(dst_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in dst S/G\n",
				dst_len);
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

		if (dst_nents) {
			mapped_dst_nents = dma_map_sg(qidev, req->dst,
						      dst_nents,
						      DMA_FROM_DEVICE);
			if (unlikely(!mapped_dst_nents)) {
				dev_err(qidev, "unable to map destination\n");
				dma_unmap_sg(qidev, req->src, src_nents,
					     DMA_TO_DEVICE);
				qi_cache_free(edesc);
				return ERR_PTR(-ENOMEM);
			}
		} else {
			mapped_dst_nents = 0;
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
	qm_sg_ents = 1 + !!ivsize + mapped_src_nents;
	if (mapped_dst_nents > 1)
		qm_sg_ents += pad_sg_nents(mapped_dst_nents);
	else if ((req->src == req->dst) && (mapped_src_nents > 1))
		qm_sg_ents = max(pad_sg_nents(qm_sg_ents),
				 1 + !!ivsize + pad_sg_nents(mapped_src_nents));
	else
		qm_sg_ents = pad_sg_nents(qm_sg_ents);

	sg_table = &edesc->sgt[0];
	qm_sg_bytes = qm_sg_ents * sizeof(*sg_table);
	if (unlikely(offsetof(struct aead_edesc, sgt) + qm_sg_bytes + ivsize >
		     CAAM_QI_MEMCACHE_SIZE)) {
		dev_err(qidev, "No space for %d S/G entries and/or %dB IV\n",
			qm_sg_ents, ivsize);
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, DMA_NONE, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	if (ivsize) {
		u8 *iv = (u8 *)(sg_table + qm_sg_ents);

		/* Make sure IV is located in a DMAable area */
		memcpy(iv, req->iv, ivsize);

		iv_dma = dma_map_single(qidev, iv, ivsize, DMA_TO_DEVICE);
		if (dma_mapping_error(qidev, iv_dma)) {
			dev_err(qidev, "unable to map IV\n");
			caam_unmap(qidev, req->src, req->dst, src_nents,
				   dst_nents, 0, 0, DMA_NONE, 0, 0);
			qi_cache_free(edesc);
			return ERR_PTR(-ENOMEM);
		}
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;
	edesc->drv_req.app_ctx = req;
	edesc->drv_req.cbk = aead_done;
	edesc->drv_req.drv_ctx = drv_ctx;

	edesc->assoclen = cpu_to_caam32(req->assoclen);
	edesc->assoclen_dma = dma_map_single(qidev, &edesc->assoclen, 4,
					     DMA_TO_DEVICE);
	if (dma_mapping_error(qidev, edesc->assoclen_dma)) {
		dev_err(qidev, "unable to map assoclen\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
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

	qm_sg_dma = dma_map_single(qidev, sg_table, qm_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(qidev, qm_sg_dma)) {
		dev_err(qidev, "unable to map S/G table\n");
		dma_unmap_single(qidev, edesc->assoclen_dma, 4, DMA_TO_DEVICE);
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, DMA_TO_DEVICE, 0, 0);
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
	} else if (mapped_dst_nents <= 1) {
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

static int ipsec_gcm_encrypt(struct aead_request *req)
{
	return crypto_ipsec_check_assoclen(req->assoclen) ? : aead_crypt(req,
					   true);
}

static int ipsec_gcm_decrypt(struct aead_request *req)
{
	return crypto_ipsec_check_assoclen(req->assoclen) ? : aead_crypt(req,
					   false);
}

static void skcipher_done(struct caam_drv_req *drv_req, u32 status)
{
	struct skcipher_edesc *edesc;
	struct skcipher_request *req = drv_req->app_ctx;
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *caam_ctx = crypto_skcipher_ctx(skcipher);
	struct device *qidev = caam_ctx->qidev;
	int ivsize = crypto_skcipher_ivsize(skcipher);
	int ecode = 0;

	dev_dbg(qidev, "%s %d: status 0x%x\n", __func__, __LINE__, status);

	edesc = container_of(drv_req, typeof(*edesc), drv_req);

	if (status)
		ecode = caam_jr_strstatus(qidev, status);

	print_hex_dump_debug("dstiv  @" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, req->iv,
			     edesc->src_nents > 1 ? 100 : ivsize, 1);
	caam_dump_sg("dst    @" __stringify(__LINE__)": ",
		     DUMP_PREFIX_ADDRESS, 16, 4, req->dst,
		     edesc->dst_nents > 1 ? 100 : req->cryptlen, 1);

	skcipher_unmap(qidev, edesc, req);

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

static struct skcipher_edesc *skcipher_edesc_alloc(struct skcipher_request *req,
						   bool encrypt)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct device *qidev = ctx->qidev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents = 0, mapped_dst_nents = 0;
	struct skcipher_edesc *edesc;
	dma_addr_t iv_dma;
	u8 *iv;
	int ivsize = crypto_skcipher_ivsize(skcipher);
	int dst_sg_idx, qm_sg_ents, qm_sg_bytes;
	struct qm_sg_entry *sg_table, *fd_sgt;
	struct caam_drv_ctx *drv_ctx;

	drv_ctx = get_drv_ctx(ctx, encrypt ? ENCRYPT : DECRYPT);
	if (IS_ERR_OR_NULL(drv_ctx))
		return (struct skcipher_edesc *)drv_ctx;

	src_nents = sg_nents_for_len(req->src, req->cryptlen);
	if (unlikely(src_nents < 0)) {
		dev_err(qidev, "Insufficient bytes (%d) in src S/G\n",
			req->cryptlen);
		return ERR_PTR(src_nents);
	}

	if (unlikely(req->src != req->dst)) {
		dst_nents = sg_nents_for_len(req->dst, req->cryptlen);
		if (unlikely(dst_nents < 0)) {
			dev_err(qidev, "Insufficient bytes (%d) in dst S/G\n",
				req->cryptlen);
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

	qm_sg_bytes = qm_sg_ents * sizeof(struct qm_sg_entry);
	if (unlikely(offsetof(struct skcipher_edesc, sgt) + qm_sg_bytes +
		     ivsize > CAAM_QI_MEMCACHE_SIZE)) {
		dev_err(qidev, "No space for %d S/G entries and/or %dB IV\n",
			qm_sg_ents, ivsize);
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, DMA_NONE, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	/* allocate space for base edesc, link tables and IV */
	edesc = qi_cache_alloc(GFP_DMA | flags);
	if (unlikely(!edesc)) {
		dev_err(qidev, "could not allocate extended descriptor\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, DMA_NONE, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	/* Make sure IV is located in a DMAable area */
	sg_table = &edesc->sgt[0];
	iv = (u8 *)(sg_table + qm_sg_ents);
	memcpy(iv, req->iv, ivsize);

	iv_dma = dma_map_single(qidev, iv, ivsize, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(qidev, iv_dma)) {
		dev_err(qidev, "unable to map IV\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, DMA_NONE, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;
	edesc->qm_sg_bytes = qm_sg_bytes;
	edesc->drv_req.app_ctx = req;
	edesc->drv_req.cbk = skcipher_done;
	edesc->drv_req.drv_ctx = drv_ctx;

	dma_to_qm_sg_one(sg_table, iv_dma, ivsize, 0);
	sg_to_qm_sg(req->src, req->cryptlen, sg_table + 1, 0);

	if (req->src != req->dst)
		sg_to_qm_sg(req->dst, req->cryptlen, sg_table + dst_sg_idx, 0);

	dma_to_qm_sg_one(sg_table + dst_sg_idx + mapped_dst_nents, iv_dma,
			 ivsize, 0);

	edesc->qm_sg_dma = dma_map_single(qidev, sg_table, edesc->qm_sg_bytes,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(qidev, edesc->qm_sg_dma)) {
		dev_err(qidev, "unable to map S/G table\n");
		caam_unmap(qidev, req->src, req->dst, src_nents, dst_nents,
			   iv_dma, ivsize, DMA_BIDIRECTIONAL, 0, 0);
		qi_cache_free(edesc);
		return ERR_PTR(-ENOMEM);
	}

	fd_sgt = &edesc->drv_req.fd_sgt[0];

	dma_to_qm_sg_one_last_ext(&fd_sgt[1], edesc->qm_sg_dma,
				  ivsize + req->cryptlen, 0);

	if (req->src == req->dst)
		dma_to_qm_sg_one_ext(&fd_sgt[0], edesc->qm_sg_dma +
				     sizeof(*sg_table), req->cryptlen + ivsize,
				     0);
	else
		dma_to_qm_sg_one_ext(&fd_sgt[0], edesc->qm_sg_dma + dst_sg_idx *
				     sizeof(*sg_table), req->cryptlen + ivsize,
				     0);

	return edesc;
}

static inline int skcipher_crypt(struct skcipher_request *req, bool encrypt)
{
	struct skcipher_edesc *edesc;
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	int ret;

	if (!req->cryptlen)
		return 0;

	if (unlikely(caam_congested))
		return -EAGAIN;

	/* allocate extended descriptor */
	edesc = skcipher_edesc_alloc(req, encrypt);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	ret = caam_qi_enqueue(ctx->qidev, &edesc->drv_req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		skcipher_unmap(ctx->qidev, edesc, req);
		qi_cache_free(edesc);
	}

	return ret;
}

static int skcipher_encrypt(struct skcipher_request *req)
{
	return skcipher_crypt(req, true);
}

static int skcipher_decrypt(struct skcipher_request *req)
{
	return skcipher_crypt(req, false);
}

static struct caam_skcipher_alg driver_algs[] = {
	{
		.skcipher = {
			.base = {
				.cra_name = "cbc(aes)",
				.cra_driver_name = "cbc-aes-caam-qi",
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
				.cra_driver_name = "cbc-3des-caam-qi",
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
				.cra_driver_name = "cbc-des-caam-qi",
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
				.cra_driver_name = "ctr-aes-caam-qi",
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
				.cra_driver_name = "rfc3686-ctr-aes-caam-qi",
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
				.cra_driver_name = "xts-aes-caam-qi",
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
};

static struct caam_aead_alg driver_aeads[] = {
	{
		.aead = {
			.base = {
				.cra_name = "rfc4106(gcm(aes))",
				.cra_driver_name = "rfc4106-gcm-aes-caam-qi",
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
				.cra_driver_name = "rfc4543-gcm-aes-caam-qi",
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
				.cra_driver_name = "gcm-aes-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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
						   "cbc-des3_ede-caam-qi",
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

static int caam_init_common(struct caam_ctx *ctx, struct caam_alg_entry *caam,
			    bool uses_dkp)
{
	struct caam_drv_private *priv;
	struct device *dev;

	/*
	 * distribute tfms across job rings to ensure in-order
	 * crypto request processing per tfm
	 */
	ctx->jrdev = caam_jr_alloc();
	if (IS_ERR(ctx->jrdev)) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return PTR_ERR(ctx->jrdev);
	}

	dev = ctx->jrdev->parent;
	priv = dev_get_drvdata(dev);
	if (priv->era >= 6 && uses_dkp)
		ctx->dir = DMA_BIDIRECTIONAL;
	else
		ctx->dir = DMA_TO_DEVICE;

	ctx->key_dma = dma_map_single(dev, ctx->key, sizeof(ctx->key),
				      ctx->dir);
	if (dma_mapping_error(dev, ctx->key_dma)) {
		dev_err(dev, "unable to map key\n");
		caam_jr_free(ctx->jrdev);
		return -ENOMEM;
	}

	/* copy descriptor header template value */
	ctx->cdata.algtype = OP_TYPE_CLASS1_ALG | caam->class1_alg_type;
	ctx->adata.algtype = OP_TYPE_CLASS2_ALG | caam->class2_alg_type;

	ctx->qidev = dev;

	spin_lock_init(&ctx->lock);
	ctx->drv_ctx[ENCRYPT] = NULL;
	ctx->drv_ctx[DECRYPT] = NULL;

	return 0;
}

static int caam_cra_init(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct caam_skcipher_alg *caam_alg =
		container_of(alg, typeof(*caam_alg), skcipher);

	return caam_init_common(crypto_skcipher_ctx(tfm), &caam_alg->caam,
				false);
}

static int caam_aead_init(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct caam_aead_alg *caam_alg = container_of(alg, typeof(*caam_alg),
						      aead);
	struct caam_ctx *ctx = crypto_aead_ctx(tfm);

	return caam_init_common(ctx, &caam_alg->caam, !caam_alg->caam.nodkp);
}

static void caam_exit_common(struct caam_ctx *ctx)
{
	caam_drv_ctx_rel(ctx->drv_ctx[ENCRYPT]);
	caam_drv_ctx_rel(ctx->drv_ctx[DECRYPT]);

	dma_unmap_single(ctx->jrdev->parent, ctx->key_dma, sizeof(ctx->key),
			 ctx->dir);

	caam_jr_free(ctx->jrdev);
}

static void caam_cra_exit(struct crypto_skcipher *tfm)
{
	caam_exit_common(crypto_skcipher_ctx(tfm));
}

static void caam_aead_exit(struct crypto_aead *tfm)
{
	caam_exit_common(crypto_aead_ctx(tfm));
}

void caam_qi_algapi_exit(void)
{
	int i;

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
}

static void caam_skcipher_alg_init(struct caam_skcipher_alg *t_alg)
{
	struct skcipher_alg *alg = &t_alg->skcipher;

	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = CAAM_CRA_PRIORITY;
	alg->base.cra_ctxsize = sizeof(struct caam_ctx);
	alg->base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY;

	alg->init = caam_cra_init;
	alg->exit = caam_cra_exit;
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

int caam_qi_algapi_init(struct device *ctrldev)
{
	struct caam_drv_private *priv = dev_get_drvdata(ctrldev);
	int i = 0, err = 0;
	u32 aes_vid, aes_inst, des_inst, md_vid, md_inst;
	unsigned int md_limit = SHA512_DIGEST_SIZE;
	bool registered = false;

	/* Make sure this runs only on (DPAA 1.x) QI */
	if (!priv->qi_present || caam_dpaa2)
		return 0;

	/*
	 * Register crypto algorithms the device supports.
	 * First, detect presence and attributes of DES, AES, and MD blocks.
	 */
	if (priv->era < 10) {
		u32 cha_vid, cha_inst;

		cha_vid = rd_reg32(&priv->ctrl->perfmon.cha_id_ls);
		aes_vid = cha_vid & CHA_ID_LS_AES_MASK;
		md_vid = (cha_vid & CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT;

		cha_inst = rd_reg32(&priv->ctrl->perfmon.cha_num_ls);
		des_inst = (cha_inst & CHA_ID_LS_DES_MASK) >>
			   CHA_ID_LS_DES_SHIFT;
		aes_inst = cha_inst & CHA_ID_LS_AES_MASK;
		md_inst = (cha_inst & CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT;
	} else {
		u32 aesa, mdha;

		aesa = rd_reg32(&priv->ctrl->vreg.aesa);
		mdha = rd_reg32(&priv->ctrl->vreg.mdha);

		aes_vid = (aesa & CHA_VER_VID_MASK) >> CHA_VER_VID_SHIFT;
		md_vid = (mdha & CHA_VER_VID_MASK) >> CHA_VER_VID_SHIFT;

		des_inst = rd_reg32(&priv->ctrl->vreg.desa) & CHA_VER_NUM_MASK;
		aes_inst = aesa & CHA_VER_NUM_MASK;
		md_inst = mdha & CHA_VER_NUM_MASK;
	}

	/* If MD is present, limit digest size based on LP256 */
	if (md_inst && md_vid  == CHA_VER_VID_MD_LP256)
		md_limit = SHA256_DIGEST_SIZE;

	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		struct caam_skcipher_alg *t_alg = driver_algs + i;
		u32 alg_sel = t_alg->caam.class1_alg_type & OP_ALG_ALGSEL_MASK;

		/* Skip DES algorithms if not supported by device */
		if (!des_inst &&
		    ((alg_sel == OP_ALG_ALGSEL_3DES) ||
		     (alg_sel == OP_ALG_ALGSEL_DES)))
			continue;

		/* Skip AES algorithms if not supported by device */
		if (!aes_inst && (alg_sel == OP_ALG_ALGSEL_AES))
			continue;

		caam_skcipher_alg_init(t_alg);

		err = crypto_register_skcipher(&t_alg->skcipher);
		if (err) {
			dev_warn(ctrldev, "%s alg registration failed\n",
				 t_alg->skcipher.base.cra_driver_name);
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
		if (aes_vid  == CHA_VER_VID_AES_LP && alg_aai == OP_ALG_AAI_GCM)
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
		dev_info(ctrldev, "algorithms registered in /proc/crypto\n");

	return err;
}
