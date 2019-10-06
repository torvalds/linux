// SPDX-License-Identifier: GPL-2.0+
/*
 * caam - Freescale FSL CAAM support for crypto API
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 * Copyright 2016-2019 NXP
 *
 * Based on talitos crypto API driver.
 *
 * relationship of job descriptors to shared descriptors (SteveC Dec 10 2008):
 *
 * ---------------                     ---------------
 * | JobDesc #1  |-------------------->|  ShareDesc  |
 * | *(packet 1) |                     |   (PDB)     |
 * ---------------      |------------->|  (hashKey)  |
 *       .              |              | (cipherKey) |
 *       .              |    |-------->| (operation) |
 * ---------------      |    |         ---------------
 * | JobDesc #2  |------|    |
 * | *(packet 2) |           |
 * ---------------           |
 *       .                   |
 *       .                   |
 * ---------------           |
 * | JobDesc #3  |------------
 * | *(packet 3) |
 * ---------------
 *
 * The SharedDesc never changes for a connection unless rekeyed, but
 * each packet will likely be in a different place. So all we need
 * to know to process the packet is where the input is, where the
 * output goes, and what context we want to process with. Context is
 * in the SharedDesc, packet references in the JobDesc.
 *
 * So, a job desc looks like:
 *
 * ---------------------
 * | Header            |
 * | ShareDesc Pointer |
 * | SEQ_OUT_PTR       |
 * | (output buffer)   |
 * | (output length)   |
 * | SEQ_IN_PTR        |
 * | (input buffer)    |
 * | (input length)    |
 * ---------------------
 */

#include "compat.h"

#include "regs.h"
#include "intern.h"
#include "desc_constr.h"
#include "jr.h"
#include "error.h"
#include "sg_sw_sec4.h"
#include "key_gen.h"
#include "caamalg_desc.h"

/*
 * crypto alg
 */
#define CAAM_CRA_PRIORITY		3000
/* max key is sum of AES_MAX_KEY_SIZE, max split key size */
#define CAAM_MAX_KEY_SIZE		(AES_MAX_KEY_SIZE + \
					 CTR_RFC3686_NONCE_SIZE + \
					 SHA512_DIGEST_SIZE * 2)

#define AEAD_DESC_JOB_IO_LEN		(DESC_JOB_IO_LEN + CAAM_CMD_SZ * 2)
#define GCM_DESC_JOB_IO_LEN		(AEAD_DESC_JOB_IO_LEN + \
					 CAAM_CMD_SZ * 4)
#define AUTHENC_DESC_JOB_IO_LEN		(AEAD_DESC_JOB_IO_LEN + \
					 CAAM_CMD_SZ * 5)

#define CHACHAPOLY_DESC_JOB_IO_LEN	(AEAD_DESC_JOB_IO_LEN + CAAM_CMD_SZ * 6)

#define DESC_MAX_USED_BYTES		(CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN_MIN)
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
	u32 sh_desc_enc[DESC_MAX_USED_LEN];
	u32 sh_desc_dec[DESC_MAX_USED_LEN];
	u8 key[CAAM_MAX_KEY_SIZE];
	dma_addr_t sh_desc_enc_dma;
	dma_addr_t sh_desc_dec_dma;
	dma_addr_t key_dma;
	enum dma_data_direction dir;
	struct device *jrdev;
	struct alginfo adata;
	struct alginfo cdata;
	unsigned int authsize;
};

static int aead_null_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(jrdev->parent);
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - AEAD_DESC_JOB_IO_LEN -
			ctx->adata.keylen_pad;

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_AEAD_NULL_ENC_LEN) {
		ctx->adata.key_inline = true;
		ctx->adata.key_virt = ctx->key;
	} else {
		ctx->adata.key_inline = false;
		ctx->adata.key_dma = ctx->key_dma;
	}

	/* aead_encrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_aead_null_encap(desc, &ctx->adata, ctx->authsize,
				    ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_AEAD_NULL_DEC_LEN) {
		ctx->adata.key_inline = true;
		ctx->adata.key_virt = ctx->key;
	} else {
		ctx->adata.key_inline = false;
		ctx->adata.key_dma = ctx->key_dma;
	}

	/* aead_decrypt shared descriptor */
	desc = ctx->sh_desc_dec;
	cnstr_shdsc_aead_null_decap(desc, &ctx->adata, ctx->authsize,
				    ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_dec_dma,
				   desc_bytes(desc), ctx->dir);

	return 0;
}

static int aead_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 struct caam_aead_alg, aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(jrdev->parent);
	u32 ctx1_iv_off = 0;
	u32 *desc, *nonce = NULL;
	u32 inl_mask;
	unsigned int data_len[2];
	const bool ctr_mode = ((ctx->cdata.algtype & OP_ALG_AAI_MASK) ==
			       OP_ALG_AAI_CTR_MOD128);
	const bool is_rfc3686 = alg->caam.rfc3686;

	if (!ctx->authsize)
		return 0;

	/* NULL encryption / decryption */
	if (!ctx->cdata.keylen)
		return aead_null_set_sh_desc(aead);

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
	 * In case |user key| > |derived key|, using DKP<imm,imm>
	 * would result in invalid opcodes (last bytes of user key) in
	 * the resulting descriptor. Use DKP<ptr,imm> instead => both
	 * virtual and dma key addresses are needed.
	 */
	ctx->adata.key_virt = ctx->key;
	ctx->adata.key_dma = ctx->key_dma;

	ctx->cdata.key_virt = ctx->key + ctx->adata.keylen_pad;
	ctx->cdata.key_dma = ctx->key_dma + ctx->adata.keylen_pad;

	data_len[0] = ctx->adata.keylen_pad;
	data_len[1] = ctx->cdata.keylen;

	if (alg->caam.geniv)
		goto skip_enc;

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (desc_inline_query(DESC_AEAD_ENC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      AUTHENC_DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	/* aead_encrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_aead_encap(desc, &ctx->cdata, &ctx->adata, ivsize,
			       ctx->authsize, is_rfc3686, nonce, ctx1_iv_off,
			       false, ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

skip_enc:
	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (desc_inline_query(DESC_AEAD_DEC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      AUTHENC_DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	/* aead_decrypt shared descriptor */
	desc = ctx->sh_desc_dec;
	cnstr_shdsc_aead_decap(desc, &ctx->cdata, &ctx->adata, ivsize,
			       ctx->authsize, alg->caam.geniv, is_rfc3686,
			       nonce, ctx1_iv_off, false, ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_dec_dma,
				   desc_bytes(desc), ctx->dir);

	if (!alg->caam.geniv)
		goto skip_givenc;

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (desc_inline_query(DESC_AEAD_GIVENC_LEN +
			      (is_rfc3686 ? DESC_AEAD_CTR_RFC3686_LEN : 0),
			      AUTHENC_DESC_JOB_IO_LEN, data_len, &inl_mask,
			      ARRAY_SIZE(data_len)) < 0)
		return -EINVAL;

	ctx->adata.key_inline = !!(inl_mask & 1);
	ctx->cdata.key_inline = !!(inl_mask & 2);

	/* aead_givencrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_aead_givencap(desc, &ctx->cdata, &ctx->adata, ivsize,
				  ctx->authsize, is_rfc3686, nonce,
				  ctx1_iv_off, false, ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

skip_givenc:
	return 0;
}

static int aead_setauthsize(struct crypto_aead *authenc,
				    unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(authenc);

	ctx->authsize = authsize;
	aead_set_sh_desc(authenc);

	return 0;
}

static int gcm_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	unsigned int ivsize = crypto_aead_ivsize(aead);
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - GCM_DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
	 * AES GCM encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_GCM_ENC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_enc;
	cnstr_shdsc_gcm_encap(desc, &ctx->cdata, ivsize, ctx->authsize, false);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_GCM_DEC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_dec;
	cnstr_shdsc_gcm_decap(desc, &ctx->cdata, ivsize, ctx->authsize, false);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_dec_dma,
				   desc_bytes(desc), ctx->dir);

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

static int rfc4106_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	unsigned int ivsize = crypto_aead_ivsize(aead);
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - GCM_DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
	 * RFC4106 encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_RFC4106_ENC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_enc;
	cnstr_shdsc_rfc4106_encap(desc, &ctx->cdata, ivsize, ctx->authsize,
				  false);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_RFC4106_DEC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_dec;
	cnstr_shdsc_rfc4106_decap(desc, &ctx->cdata, ivsize, ctx->authsize,
				  false);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_dec_dma,
				   desc_bytes(desc), ctx->dir);

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

static int rfc4543_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	unsigned int ivsize = crypto_aead_ivsize(aead);
	u32 *desc;
	int rem_bytes = CAAM_DESC_BYTES_MAX - GCM_DESC_JOB_IO_LEN -
			ctx->cdata.keylen;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	/*
	 * RFC4543 encrypt shared descriptor
	 * Job Descriptor and Shared Descriptor
	 * must fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_RFC4543_ENC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_enc;
	cnstr_shdsc_rfc4543_encap(desc, &ctx->cdata, ivsize, ctx->authsize,
				  false);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

	/*
	 * Job Descriptor and Shared Descriptors
	 * must all fit into the 64-word Descriptor h/w Buffer
	 */
	if (rem_bytes >= DESC_RFC4543_DEC_LEN) {
		ctx->cdata.key_inline = true;
		ctx->cdata.key_virt = ctx->key;
	} else {
		ctx->cdata.key_inline = false;
		ctx->cdata.key_dma = ctx->key_dma;
	}

	desc = ctx->sh_desc_dec;
	cnstr_shdsc_rfc4543_decap(desc, &ctx->cdata, ivsize, ctx->authsize,
				  false);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_dec_dma,
				   desc_bytes(desc), ctx->dir);

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

static int chachapoly_set_sh_desc(struct crypto_aead *aead)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	unsigned int ivsize = crypto_aead_ivsize(aead);
	u32 *desc;

	if (!ctx->cdata.keylen || !ctx->authsize)
		return 0;

	desc = ctx->sh_desc_enc;
	cnstr_shdsc_chachapoly(desc, &ctx->cdata, &ctx->adata, ivsize,
			       ctx->authsize, true, false);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

	desc = ctx->sh_desc_dec;
	cnstr_shdsc_chachapoly(desc, &ctx->cdata, &ctx->adata, ivsize,
			       ctx->authsize, false, false);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_dec_dma,
				   desc_bytes(desc), ctx->dir);

	return 0;
}

static int chachapoly_setauthsize(struct crypto_aead *aead,
				  unsigned int authsize)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);

	if (authsize != POLY1305_DIGEST_SIZE)
		return -EINVAL;

	ctx->authsize = authsize;
	return chachapoly_set_sh_desc(aead);
}

static int chachapoly_setkey(struct crypto_aead *aead, const u8 *key,
			     unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	unsigned int saltlen = CHACHAPOLY_IV_SIZE - ivsize;

	if (keylen != CHACHA_KEY_SIZE + saltlen) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ctx->cdata.key_virt = key;
	ctx->cdata.keylen = keylen - saltlen;

	return chachapoly_set_sh_desc(aead);
}

static int aead_setkey(struct crypto_aead *aead,
			       const u8 *key, unsigned int keylen)
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
	print_hex_dump_debug("key in @"__stringify(__LINE__)": ",
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
		dma_sync_single_for_device(jrdev, ctx->key_dma,
					   ctx->adata.keylen_pad +
					   keys.enckeylen, ctx->dir);
		goto skip_split_key;
	}

	ret = gen_split_key(ctx->jrdev, ctx->key, &ctx->adata, keys.authkey,
			    keys.authkeylen, CAAM_MAX_KEY_SIZE -
			    keys.enckeylen);
	if (ret) {
		goto badkey;
	}

	/* postpend encryption key to auth split key */
	memcpy(ctx->key + ctx->adata.keylen_pad, keys.enckey, keys.enckeylen);
	dma_sync_single_for_device(jrdev, ctx->key_dma, ctx->adata.keylen_pad +
				   keys.enckeylen, ctx->dir);

	print_hex_dump_debug("ctx.key@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, ctx->key,
			     ctx->adata.keylen_pad + keys.enckeylen, 1);

skip_split_key:
	ctx->cdata.keylen = keys.enckeylen;
	memzero_explicit(&keys, sizeof(keys));
	return aead_set_sh_desc(aead);
badkey:
	crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
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

static int gcm_setkey(struct crypto_aead *aead,
		      const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	int err;

	err = aes_check_keylen(keylen);
	if (err) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return err;
	}

	print_hex_dump_debug("key in @"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	memcpy(ctx->key, key, keylen);
	dma_sync_single_for_device(jrdev, ctx->key_dma, keylen, ctx->dir);
	ctx->cdata.keylen = keylen;

	return gcm_set_sh_desc(aead);
}

static int rfc4106_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	int err;

	err = aes_check_keylen(keylen - 4);
	if (err) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return err;
	}

	print_hex_dump_debug("key in @"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	memcpy(ctx->key, key, keylen);

	/*
	 * The last four bytes of the key material are used as the salt value
	 * in the nonce. Update the AES key length.
	 */
	ctx->cdata.keylen = keylen - 4;
	dma_sync_single_for_device(jrdev, ctx->key_dma, ctx->cdata.keylen,
				   ctx->dir);
	return rfc4106_set_sh_desc(aead);
}

static int rfc4543_setkey(struct crypto_aead *aead,
			  const u8 *key, unsigned int keylen)
{
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	int err;

	err = aes_check_keylen(keylen - 4);
	if (err) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return err;
	}

	print_hex_dump_debug("key in @"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	memcpy(ctx->key, key, keylen);

	/*
	 * The last four bytes of the key material are used as the salt value
	 * in the nonce. Update the AES key length.
	 */
	ctx->cdata.keylen = keylen - 4;
	dma_sync_single_for_device(jrdev, ctx->key_dma, ctx->cdata.keylen,
				   ctx->dir);
	return rfc4543_set_sh_desc(aead);
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
	u32 *desc;
	const bool is_rfc3686 = alg->caam.rfc3686;

	print_hex_dump_debug("key in @"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = key;
	ctx->cdata.key_inline = true;

	/* skcipher_encrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_skcipher_encap(desc, &ctx->cdata, ivsize, is_rfc3686,
				   ctx1_iv_off);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

	/* skcipher_decrypt shared descriptor */
	desc = ctx->sh_desc_dec;
	cnstr_shdsc_skcipher_decap(desc, &ctx->cdata, ivsize, is_rfc3686,
				   ctx1_iv_off);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_dec_dma,
				   desc_bytes(desc), ctx->dir);

	return 0;
}

static int aes_skcipher_setkey(struct crypto_skcipher *skcipher,
			       const u8 *key, unsigned int keylen)
{
	int err;

	err = aes_check_keylen(keylen);
	if (err) {
		crypto_skcipher_set_flags(skcipher,
					  CRYPTO_TFM_RES_BAD_KEY_LEN);
		return err;
	}

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
	if (err) {
		crypto_skcipher_set_flags(skcipher,
					  CRYPTO_TFM_RES_BAD_KEY_LEN);
		return err;
	}

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
	if (err) {
		crypto_skcipher_set_flags(skcipher,
					  CRYPTO_TFM_RES_BAD_KEY_LEN);
		return err;
	}

	return skcipher_setkey(skcipher, key, keylen, ctx1_iv_off);
}

static int arc4_skcipher_setkey(struct crypto_skcipher *skcipher,
				const u8 *key, unsigned int keylen)
{
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
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;

	if (keylen != 2 * AES_MIN_KEY_SIZE  && keylen != 2 * AES_MAX_KEY_SIZE) {
		crypto_skcipher_set_flags(skcipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		dev_err(jrdev, "key size mismatch\n");
		return -EINVAL;
	}

	ctx->cdata.keylen = keylen;
	ctx->cdata.key_virt = key;
	ctx->cdata.key_inline = true;

	/* xts_skcipher_encrypt shared descriptor */
	desc = ctx->sh_desc_enc;
	cnstr_shdsc_xts_skcipher_encap(desc, &ctx->cdata);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_enc_dma,
				   desc_bytes(desc), ctx->dir);

	/* xts_skcipher_decrypt shared descriptor */
	desc = ctx->sh_desc_dec;
	cnstr_shdsc_xts_skcipher_decap(desc, &ctx->cdata);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_dec_dma,
				   desc_bytes(desc), ctx->dir);

	return 0;
}

/*
 * aead_edesc - s/w-extended aead descriptor
 * @src_nents: number of segments in input s/w scatterlist
 * @dst_nents: number of segments in output s/w scatterlist
 * @mapped_src_nents: number of segments in input h/w link table
 * @mapped_dst_nents: number of segments in output h/w link table
 * @sec4_sg_bytes: length of dma mapped sec4_sg space
 * @sec4_sg_dma: bus physical mapped address of h/w link table
 * @sec4_sg: pointer to h/w link table
 * @hw_desc: the h/w job descriptor followed by any referenced link tables
 */
struct aead_edesc {
	int src_nents;
	int dst_nents;
	int mapped_src_nents;
	int mapped_dst_nents;
	int sec4_sg_bytes;
	dma_addr_t sec4_sg_dma;
	struct sec4_sg_entry *sec4_sg;
	u32 hw_desc[];
};

/*
 * skcipher_edesc - s/w-extended skcipher descriptor
 * @src_nents: number of segments in input s/w scatterlist
 * @dst_nents: number of segments in output s/w scatterlist
 * @mapped_src_nents: number of segments in input h/w link table
 * @mapped_dst_nents: number of segments in output h/w link table
 * @iv_dma: dma address of iv for checking continuity and link table
 * @sec4_sg_bytes: length of dma mapped sec4_sg space
 * @sec4_sg_dma: bus physical mapped address of h/w link table
 * @sec4_sg: pointer to h/w link table
 * @hw_desc: the h/w job descriptor followed by any referenced link tables
 *	     and IV
 */
struct skcipher_edesc {
	int src_nents;
	int dst_nents;
	int mapped_src_nents;
	int mapped_dst_nents;
	dma_addr_t iv_dma;
	int sec4_sg_bytes;
	dma_addr_t sec4_sg_dma;
	struct sec4_sg_entry *sec4_sg;
	u32 hw_desc[0];
};

static void caam_unmap(struct device *dev, struct scatterlist *src,
		       struct scatterlist *dst, int src_nents,
		       int dst_nents,
		       dma_addr_t iv_dma, int ivsize, dma_addr_t sec4_sg_dma,
		       int sec4_sg_bytes)
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
		dma_unmap_single(dev, iv_dma, ivsize, DMA_BIDIRECTIONAL);
	if (sec4_sg_bytes)
		dma_unmap_single(dev, sec4_sg_dma, sec4_sg_bytes,
				 DMA_TO_DEVICE);
}

static void aead_unmap(struct device *dev,
		       struct aead_edesc *edesc,
		       struct aead_request *req)
{
	caam_unmap(dev, req->src, req->dst,
		   edesc->src_nents, edesc->dst_nents, 0, 0,
		   edesc->sec4_sg_dma, edesc->sec4_sg_bytes);
}

static void skcipher_unmap(struct device *dev, struct skcipher_edesc *edesc,
			   struct skcipher_request *req)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	int ivsize = crypto_skcipher_ivsize(skcipher);

	caam_unmap(dev, req->src, req->dst,
		   edesc->src_nents, edesc->dst_nents,
		   edesc->iv_dma, ivsize,
		   edesc->sec4_sg_dma, edesc->sec4_sg_bytes);
}

static void aead_encrypt_done(struct device *jrdev, u32 *desc, u32 err,
				   void *context)
{
	struct aead_request *req = context;
	struct aead_edesc *edesc;
	int ecode = 0;

	dev_dbg(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	edesc = container_of(desc, struct aead_edesc, hw_desc[0]);

	if (err)
		ecode = caam_jr_strstatus(jrdev, err);

	aead_unmap(jrdev, edesc, req);

	kfree(edesc);

	aead_request_complete(req, ecode);
}

static void aead_decrypt_done(struct device *jrdev, u32 *desc, u32 err,
				   void *context)
{
	struct aead_request *req = context;
	struct aead_edesc *edesc;
	int ecode = 0;

	dev_dbg(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	edesc = container_of(desc, struct aead_edesc, hw_desc[0]);

	if (err)
		ecode = caam_jr_strstatus(jrdev, err);

	aead_unmap(jrdev, edesc, req);

	kfree(edesc);

	aead_request_complete(req, ecode);
}

static void skcipher_encrypt_done(struct device *jrdev, u32 *desc, u32 err,
				  void *context)
{
	struct skcipher_request *req = context;
	struct skcipher_edesc *edesc;
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	int ivsize = crypto_skcipher_ivsize(skcipher);
	int ecode = 0;

	dev_dbg(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	edesc = container_of(desc, struct skcipher_edesc, hw_desc[0]);

	if (err)
		ecode = caam_jr_strstatus(jrdev, err);

	skcipher_unmap(jrdev, edesc, req);

	/*
	 * The crypto API expects us to set the IV (req->iv) to the last
	 * ciphertext block (CBC mode) or last counter (CTR mode).
	 * This is used e.g. by the CTS mode.
	 */
	if (ivsize && !ecode) {
		memcpy(req->iv, (u8 *)edesc->sec4_sg + edesc->sec4_sg_bytes,
		       ivsize);
		print_hex_dump_debug("dstiv  @"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, req->iv,
				     edesc->src_nents > 1 ? 100 : ivsize, 1);
	}

	caam_dump_sg("dst    @" __stringify(__LINE__)": ",
		     DUMP_PREFIX_ADDRESS, 16, 4, req->dst,
		     edesc->dst_nents > 1 ? 100 : req->cryptlen, 1);

	kfree(edesc);

	skcipher_request_complete(req, ecode);
}

static void skcipher_decrypt_done(struct device *jrdev, u32 *desc, u32 err,
				  void *context)
{
	struct skcipher_request *req = context;
	struct skcipher_edesc *edesc;
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	int ivsize = crypto_skcipher_ivsize(skcipher);
	int ecode = 0;

	dev_dbg(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	edesc = container_of(desc, struct skcipher_edesc, hw_desc[0]);
	if (err)
		ecode = caam_jr_strstatus(jrdev, err);

	skcipher_unmap(jrdev, edesc, req);

	/*
	 * The crypto API expects us to set the IV (req->iv) to the last
	 * ciphertext block (CBC mode) or last counter (CTR mode).
	 * This is used e.g. by the CTS mode.
	 */
	if (ivsize && !ecode) {
		memcpy(req->iv, (u8 *)edesc->sec4_sg + edesc->sec4_sg_bytes,
		       ivsize);

		print_hex_dump_debug("dstiv  @" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, req->iv,
				     ivsize, 1);
	}

	caam_dump_sg("dst    @" __stringify(__LINE__)": ",
		     DUMP_PREFIX_ADDRESS, 16, 4, req->dst,
		     edesc->dst_nents > 1 ? 100 : req->cryptlen, 1);

	kfree(edesc);

	skcipher_request_complete(req, ecode);
}

/*
 * Fill in aead job descriptor
 */
static void init_aead_job(struct aead_request *req,
			  struct aead_edesc *edesc,
			  bool all_contig, bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	int authsize = ctx->authsize;
	u32 *desc = edesc->hw_desc;
	u32 out_options, in_options;
	dma_addr_t dst_dma, src_dma;
	int len, sec4_sg_index = 0;
	dma_addr_t ptr;
	u32 *sh_desc;

	sh_desc = encrypt ? ctx->sh_desc_enc : ctx->sh_desc_dec;
	ptr = encrypt ? ctx->sh_desc_enc_dma : ctx->sh_desc_dec_dma;

	len = desc_len(sh_desc);
	init_job_desc_shared(desc, ptr, len, HDR_SHARE_DEFER | HDR_REVERSE);

	if (all_contig) {
		src_dma = edesc->mapped_src_nents ? sg_dma_address(req->src) :
						    0;
		in_options = 0;
	} else {
		src_dma = edesc->sec4_sg_dma;
		sec4_sg_index += edesc->mapped_src_nents;
		in_options = LDST_SGF;
	}

	append_seq_in_ptr(desc, src_dma, req->assoclen + req->cryptlen,
			  in_options);

	dst_dma = src_dma;
	out_options = in_options;

	if (unlikely(req->src != req->dst)) {
		if (!edesc->mapped_dst_nents) {
			dst_dma = 0;
			out_options = 0;
		} else if (edesc->mapped_dst_nents == 1) {
			dst_dma = sg_dma_address(req->dst);
			out_options = 0;
		} else {
			dst_dma = edesc->sec4_sg_dma +
				  sec4_sg_index *
				  sizeof(struct sec4_sg_entry);
			out_options = LDST_SGF;
		}
	}

	if (encrypt)
		append_seq_out_ptr(desc, dst_dma,
				   req->assoclen + req->cryptlen + authsize,
				   out_options);
	else
		append_seq_out_ptr(desc, dst_dma,
				   req->assoclen + req->cryptlen - authsize,
				   out_options);
}

static void init_gcm_job(struct aead_request *req,
			 struct aead_edesc *edesc,
			 bool all_contig, bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	u32 *desc = edesc->hw_desc;
	bool generic_gcm = (ivsize == GCM_AES_IV_SIZE);
	unsigned int last;

	init_aead_job(req, edesc, all_contig, encrypt);
	append_math_add_imm_u32(desc, REG3, ZERO, IMM, req->assoclen);

	/* BUG This should not be specific to generic GCM. */
	last = 0;
	if (encrypt && generic_gcm && !(req->assoclen + req->cryptlen))
		last = FIFOLD_TYPE_LAST1;

	/* Read GCM IV */
	append_cmd(desc, CMD_FIFO_LOAD | FIFOLD_CLASS_CLASS1 | IMMEDIATE |
			 FIFOLD_TYPE_IV | FIFOLD_TYPE_FLUSH1 | GCM_AES_IV_SIZE | last);
	/* Append Salt */
	if (!generic_gcm)
		append_data(desc, ctx->key + ctx->cdata.keylen, 4);
	/* Append IV */
	append_data(desc, req->iv, ivsize);
	/* End of blank commands */
}

static void init_chachapoly_job(struct aead_request *req,
				struct aead_edesc *edesc, bool all_contig,
				bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	unsigned int assoclen = req->assoclen;
	u32 *desc = edesc->hw_desc;
	u32 ctx_iv_off = 4;

	init_aead_job(req, edesc, all_contig, encrypt);

	if (ivsize != CHACHAPOLY_IV_SIZE) {
		/* IPsec specific: CONTEXT1[223:128] = {NONCE, IV} */
		ctx_iv_off += 4;

		/*
		 * The associated data comes already with the IV but we need
		 * to skip it when we authenticate or encrypt...
		 */
		assoclen -= ivsize;
	}

	append_math_add_imm_u32(desc, REG3, ZERO, IMM, assoclen);

	/*
	 * For IPsec load the IV further in the same register.
	 * For RFC7539 simply load the 12 bytes nonce in a single operation
	 */
	append_load_as_imm(desc, req->iv, ivsize, LDST_CLASS_1_CCB |
			   LDST_SRCDST_BYTE_CONTEXT |
			   ctx_iv_off << LDST_OFFSET_SHIFT);
}

static void init_authenc_job(struct aead_request *req,
			     struct aead_edesc *edesc,
			     bool all_contig, bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_aead_alg *alg = container_of(crypto_aead_alg(aead),
						 struct caam_aead_alg, aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctx->jrdev->parent);
	const bool ctr_mode = ((ctx->cdata.algtype & OP_ALG_AAI_MASK) ==
			       OP_ALG_AAI_CTR_MOD128);
	const bool is_rfc3686 = alg->caam.rfc3686;
	u32 *desc = edesc->hw_desc;
	u32 ivoffset = 0;

	/*
	 * AES-CTR needs to load IV in CONTEXT1 reg
	 * at an offset of 128bits (16bytes)
	 * CONTEXT1[255:128] = IV
	 */
	if (ctr_mode)
		ivoffset = 16;

	/*
	 * RFC3686 specific:
	 *	CONTEXT1[255:128] = {NONCE, IV, COUNTER}
	 */
	if (is_rfc3686)
		ivoffset = 16 + CTR_RFC3686_NONCE_SIZE;

	init_aead_job(req, edesc, all_contig, encrypt);

	/*
	 * {REG3, DPOVRD} = assoclen, depending on whether MATH command supports
	 * having DPOVRD as destination.
	 */
	if (ctrlpriv->era < 3)
		append_math_add_imm_u32(desc, REG3, ZERO, IMM, req->assoclen);
	else
		append_math_add_imm_u32(desc, DPOVRD, ZERO, IMM, req->assoclen);

	if (ivsize && ((is_rfc3686 && encrypt) || !alg->caam.geniv))
		append_load_as_imm(desc, req->iv, ivsize,
				   LDST_CLASS_1_CCB |
				   LDST_SRCDST_BYTE_CONTEXT |
				   (ivoffset << LDST_OFFSET_SHIFT));
}

/*
 * Fill in skcipher job descriptor
 */
static void init_skcipher_job(struct skcipher_request *req,
			      struct skcipher_edesc *edesc,
			      const bool encrypt)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct device *jrdev = ctx->jrdev;
	int ivsize = crypto_skcipher_ivsize(skcipher);
	u32 *desc = edesc->hw_desc;
	u32 *sh_desc;
	u32 in_options = 0, out_options = 0;
	dma_addr_t src_dma, dst_dma, ptr;
	int len, sec4_sg_index = 0;

	print_hex_dump_debug("presciv@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, req->iv, ivsize, 1);
	dev_dbg(jrdev, "asked=%d, cryptlen%d\n",
	       (int)edesc->src_nents > 1 ? 100 : req->cryptlen, req->cryptlen);

	caam_dump_sg("src    @" __stringify(__LINE__)": ",
		     DUMP_PREFIX_ADDRESS, 16, 4, req->src,
		     edesc->src_nents > 1 ? 100 : req->cryptlen, 1);

	sh_desc = encrypt ? ctx->sh_desc_enc : ctx->sh_desc_dec;
	ptr = encrypt ? ctx->sh_desc_enc_dma : ctx->sh_desc_dec_dma;

	len = desc_len(sh_desc);
	init_job_desc_shared(desc, ptr, len, HDR_SHARE_DEFER | HDR_REVERSE);

	if (ivsize || edesc->mapped_src_nents > 1) {
		src_dma = edesc->sec4_sg_dma;
		sec4_sg_index = edesc->mapped_src_nents + !!ivsize;
		in_options = LDST_SGF;
	} else {
		src_dma = sg_dma_address(req->src);
	}

	append_seq_in_ptr(desc, src_dma, req->cryptlen + ivsize, in_options);

	if (likely(req->src == req->dst)) {
		dst_dma = src_dma + !!ivsize * sizeof(struct sec4_sg_entry);
		out_options = in_options;
	} else if (!ivsize && edesc->mapped_dst_nents == 1) {
		dst_dma = sg_dma_address(req->dst);
	} else {
		dst_dma = edesc->sec4_sg_dma + sec4_sg_index *
			  sizeof(struct sec4_sg_entry);
		out_options = LDST_SGF;
	}

	append_seq_out_ptr(desc, dst_dma, req->cryptlen + ivsize, out_options);
}

/*
 * allocate and map the aead extended descriptor
 */
static struct aead_edesc *aead_edesc_alloc(struct aead_request *req,
					   int desc_bytes, bool *all_contig_ptr,
					   bool encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents = 0, mapped_dst_nents = 0;
	int src_len, dst_len = 0;
	struct aead_edesc *edesc;
	int sec4_sg_index, sec4_sg_len, sec4_sg_bytes;
	unsigned int authsize = ctx->authsize;

	if (unlikely(req->dst != req->src)) {
		src_len = req->assoclen + req->cryptlen;
		dst_len = src_len + (encrypt ? authsize : (-authsize));

		src_nents = sg_nents_for_len(req->src, src_len);
		if (unlikely(src_nents < 0)) {
			dev_err(jrdev, "Insufficient bytes (%d) in src S/G\n",
				src_len);
			return ERR_PTR(src_nents);
		}

		dst_nents = sg_nents_for_len(req->dst, dst_len);
		if (unlikely(dst_nents < 0)) {
			dev_err(jrdev, "Insufficient bytes (%d) in dst S/G\n",
				dst_len);
			return ERR_PTR(dst_nents);
		}
	} else {
		src_len = req->assoclen + req->cryptlen +
			  (encrypt ? authsize : 0);

		src_nents = sg_nents_for_len(req->src, src_len);
		if (unlikely(src_nents < 0)) {
			dev_err(jrdev, "Insufficient bytes (%d) in src S/G\n",
				src_len);
			return ERR_PTR(src_nents);
		}
	}

	if (likely(req->src == req->dst)) {
		mapped_src_nents = dma_map_sg(jrdev, req->src, src_nents,
					      DMA_BIDIRECTIONAL);
		if (unlikely(!mapped_src_nents)) {
			dev_err(jrdev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}
	} else {
		/* Cover also the case of null (zero length) input data */
		if (src_nents) {
			mapped_src_nents = dma_map_sg(jrdev, req->src,
						      src_nents, DMA_TO_DEVICE);
			if (unlikely(!mapped_src_nents)) {
				dev_err(jrdev, "unable to map source\n");
				return ERR_PTR(-ENOMEM);
			}
		} else {
			mapped_src_nents = 0;
		}

		/* Cover also the case of null (zero length) output data */
		if (dst_nents) {
			mapped_dst_nents = dma_map_sg(jrdev, req->dst,
						      dst_nents,
						      DMA_FROM_DEVICE);
			if (unlikely(!mapped_dst_nents)) {
				dev_err(jrdev, "unable to map destination\n");
				dma_unmap_sg(jrdev, req->src, src_nents,
					     DMA_TO_DEVICE);
				return ERR_PTR(-ENOMEM);
			}
		} else {
			mapped_dst_nents = 0;
		}
	}

	/*
	 * HW reads 4 S/G entries at a time; make sure the reads don't go beyond
	 * the end of the table by allocating more S/G entries.
	 */
	sec4_sg_len = mapped_src_nents > 1 ? mapped_src_nents : 0;
	if (mapped_dst_nents > 1)
		sec4_sg_len += pad_sg_nents(mapped_dst_nents);
	else
		sec4_sg_len = pad_sg_nents(sec4_sg_len);

	sec4_sg_bytes = sec4_sg_len * sizeof(struct sec4_sg_entry);

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = kzalloc(sizeof(*edesc) + desc_bytes + sec4_sg_bytes,
			GFP_DMA | flags);
	if (!edesc) {
		caam_unmap(jrdev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->mapped_src_nents = mapped_src_nents;
	edesc->mapped_dst_nents = mapped_dst_nents;
	edesc->sec4_sg = (void *)edesc + sizeof(struct aead_edesc) +
			 desc_bytes;
	*all_contig_ptr = !(mapped_src_nents > 1);

	sec4_sg_index = 0;
	if (mapped_src_nents > 1) {
		sg_to_sec4_sg_last(req->src, src_len,
				   edesc->sec4_sg + sec4_sg_index, 0);
		sec4_sg_index += mapped_src_nents;
	}
	if (mapped_dst_nents > 1) {
		sg_to_sec4_sg_last(req->dst, dst_len,
				   edesc->sec4_sg + sec4_sg_index, 0);
	}

	if (!sec4_sg_bytes)
		return edesc;

	edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
					    sec4_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
		dev_err(jrdev, "unable to map S/G table\n");
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
		return ERR_PTR(-ENOMEM);
	}

	edesc->sec4_sg_bytes = sec4_sg_bytes;

	return edesc;
}

static int gcm_encrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, GCM_DESC_JOB_IO_LEN, &all_contig, true);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor */
	init_gcm_job(req, edesc, all_contig, true);

	print_hex_dump_debug("aead jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
			     desc_bytes(edesc->hw_desc), 1);

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, aead_encrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int chachapoly_encrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret;

	edesc = aead_edesc_alloc(req, CHACHAPOLY_DESC_JOB_IO_LEN, &all_contig,
				 true);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	desc = edesc->hw_desc;

	init_chachapoly_job(req, edesc, all_contig, true);
	print_hex_dump_debug("chachapoly jobdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	ret = caam_jr_enqueue(jrdev, desc, aead_encrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int chachapoly_decrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret;

	edesc = aead_edesc_alloc(req, CHACHAPOLY_DESC_JOB_IO_LEN, &all_contig,
				 false);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	desc = edesc->hw_desc;

	init_chachapoly_job(req, edesc, all_contig, false);
	print_hex_dump_debug("chachapoly jobdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	ret = caam_jr_enqueue(jrdev, desc, aead_decrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int ipsec_gcm_encrypt(struct aead_request *req)
{
	return crypto_ipsec_check_assoclen(req->assoclen) ? : gcm_encrypt(req);
}

static int aead_encrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, AUTHENC_DESC_JOB_IO_LEN,
				 &all_contig, true);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor */
	init_authenc_job(req, edesc, all_contig, true);

	print_hex_dump_debug("aead jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
			     desc_bytes(edesc->hw_desc), 1);

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, aead_encrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int gcm_decrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret = 0;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, GCM_DESC_JOB_IO_LEN, &all_contig, false);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_gcm_job(req, edesc, all_contig, false);

	print_hex_dump_debug("aead jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
			     desc_bytes(edesc->hw_desc), 1);

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, aead_decrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int ipsec_gcm_decrypt(struct aead_request *req)
{
	return crypto_ipsec_check_assoclen(req->assoclen) ? : gcm_decrypt(req);
}

static int aead_decrypt(struct aead_request *req)
{
	struct aead_edesc *edesc;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct caam_ctx *ctx = crypto_aead_ctx(aead);
	struct device *jrdev = ctx->jrdev;
	bool all_contig;
	u32 *desc;
	int ret = 0;

	caam_dump_sg("dec src@" __stringify(__LINE__)": ",
		     DUMP_PREFIX_ADDRESS, 16, 4, req->src,
		     req->assoclen + req->cryptlen, 1);

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, AUTHENC_DESC_JOB_IO_LEN,
				 &all_contig, false);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_authenc_job(req, edesc, all_contig, false);

	print_hex_dump_debug("aead jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
			     desc_bytes(edesc->hw_desc), 1);

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, aead_decrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		aead_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

/*
 * allocate and map the skcipher extended descriptor for skcipher
 */
static struct skcipher_edesc *skcipher_edesc_alloc(struct skcipher_request *req,
						   int desc_bytes)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int src_nents, mapped_src_nents, dst_nents = 0, mapped_dst_nents = 0;
	struct skcipher_edesc *edesc;
	dma_addr_t iv_dma = 0;
	u8 *iv;
	int ivsize = crypto_skcipher_ivsize(skcipher);
	int dst_sg_idx, sec4_sg_ents, sec4_sg_bytes;

	src_nents = sg_nents_for_len(req->src, req->cryptlen);
	if (unlikely(src_nents < 0)) {
		dev_err(jrdev, "Insufficient bytes (%d) in src S/G\n",
			req->cryptlen);
		return ERR_PTR(src_nents);
	}

	if (req->dst != req->src) {
		dst_nents = sg_nents_for_len(req->dst, req->cryptlen);
		if (unlikely(dst_nents < 0)) {
			dev_err(jrdev, "Insufficient bytes (%d) in dst S/G\n",
				req->cryptlen);
			return ERR_PTR(dst_nents);
		}
	}

	if (likely(req->src == req->dst)) {
		mapped_src_nents = dma_map_sg(jrdev, req->src, src_nents,
					      DMA_BIDIRECTIONAL);
		if (unlikely(!mapped_src_nents)) {
			dev_err(jrdev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}
	} else {
		mapped_src_nents = dma_map_sg(jrdev, req->src, src_nents,
					      DMA_TO_DEVICE);
		if (unlikely(!mapped_src_nents)) {
			dev_err(jrdev, "unable to map source\n");
			return ERR_PTR(-ENOMEM);
		}
		mapped_dst_nents = dma_map_sg(jrdev, req->dst, dst_nents,
					      DMA_FROM_DEVICE);
		if (unlikely(!mapped_dst_nents)) {
			dev_err(jrdev, "unable to map destination\n");
			dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
			return ERR_PTR(-ENOMEM);
		}
	}

	if (!ivsize && mapped_src_nents == 1)
		sec4_sg_ents = 0; // no need for an input hw s/g table
	else
		sec4_sg_ents = mapped_src_nents + !!ivsize;
	dst_sg_idx = sec4_sg_ents;

	/*
	 * Input, output HW S/G tables: [IV, src][dst, IV]
	 * IV entries point to the same buffer
	 * If src == dst, S/G entries are reused (S/G tables overlap)
	 *
	 * HW reads 4 S/G entries at a time; make sure the reads don't go beyond
	 * the end of the table by allocating more S/G entries. Logic:
	 * if (output S/G)
	 *      pad output S/G, if needed
	 * else if (input S/G) ...
	 *      pad input S/G, if needed
	 */
	if (ivsize || mapped_dst_nents > 1) {
		if (req->src == req->dst)
			sec4_sg_ents = !!ivsize + pad_sg_nents(sec4_sg_ents);
		else
			sec4_sg_ents += pad_sg_nents(mapped_dst_nents +
						     !!ivsize);
	} else {
		sec4_sg_ents = pad_sg_nents(sec4_sg_ents);
	}

	sec4_sg_bytes = sec4_sg_ents * sizeof(struct sec4_sg_entry);

	/*
	 * allocate space for base edesc and hw desc commands, link tables, IV
	 */
	edesc = kzalloc(sizeof(*edesc) + desc_bytes + sec4_sg_bytes + ivsize,
			GFP_DMA | flags);
	if (!edesc) {
		dev_err(jrdev, "could not allocate extended descriptor\n");
		caam_unmap(jrdev, req->src, req->dst, src_nents, dst_nents, 0,
			   0, 0, 0);
		return ERR_PTR(-ENOMEM);
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->mapped_src_nents = mapped_src_nents;
	edesc->mapped_dst_nents = mapped_dst_nents;
	edesc->sec4_sg_bytes = sec4_sg_bytes;
	edesc->sec4_sg = (struct sec4_sg_entry *)((u8 *)edesc->hw_desc +
						  desc_bytes);

	/* Make sure IV is located in a DMAable area */
	if (ivsize) {
		iv = (u8 *)edesc->sec4_sg + sec4_sg_bytes;
		memcpy(iv, req->iv, ivsize);

		iv_dma = dma_map_single(jrdev, iv, ivsize, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(jrdev, iv_dma)) {
			dev_err(jrdev, "unable to map IV\n");
			caam_unmap(jrdev, req->src, req->dst, src_nents,
				   dst_nents, 0, 0, 0, 0);
			kfree(edesc);
			return ERR_PTR(-ENOMEM);
		}

		dma_to_sec4_sg_one(edesc->sec4_sg, iv_dma, ivsize, 0);
	}
	if (dst_sg_idx)
		sg_to_sec4_sg(req->src, req->cryptlen, edesc->sec4_sg +
			      !!ivsize, 0);

	if (req->src != req->dst && (ivsize || mapped_dst_nents > 1))
		sg_to_sec4_sg(req->dst, req->cryptlen, edesc->sec4_sg +
			      dst_sg_idx, 0);

	if (ivsize)
		dma_to_sec4_sg_one(edesc->sec4_sg + dst_sg_idx +
				   mapped_dst_nents, iv_dma, ivsize, 0);

	if (ivsize || mapped_dst_nents > 1)
		sg_to_sec4_set_last(edesc->sec4_sg + dst_sg_idx +
				    mapped_dst_nents);

	if (sec4_sg_bytes) {
		edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
						    sec4_sg_bytes,
						    DMA_TO_DEVICE);
		if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
			dev_err(jrdev, "unable to map S/G table\n");
			caam_unmap(jrdev, req->src, req->dst, src_nents,
				   dst_nents, iv_dma, ivsize, 0, 0);
			kfree(edesc);
			return ERR_PTR(-ENOMEM);
		}
	}

	edesc->iv_dma = iv_dma;

	print_hex_dump_debug("skcipher sec4_sg@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, edesc->sec4_sg,
			     sec4_sg_bytes, 1);

	return edesc;
}

static int skcipher_encrypt(struct skcipher_request *req)
{
	struct skcipher_edesc *edesc;
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	int ret = 0;

	if (!req->cryptlen)
		return 0;

	/* allocate extended descriptor */
	edesc = skcipher_edesc_alloc(req, DESC_JOB_IO_LEN * CAAM_CMD_SZ);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_skcipher_job(req, edesc, true);

	print_hex_dump_debug("skcipher jobdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
			     desc_bytes(edesc->hw_desc), 1);

	desc = edesc->hw_desc;
	ret = caam_jr_enqueue(jrdev, desc, skcipher_encrypt_done, req);

	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		skcipher_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static int skcipher_decrypt(struct skcipher_request *req)
{
	struct skcipher_edesc *edesc;
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct caam_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	int ret = 0;

	if (!req->cryptlen)
		return 0;

	/* allocate extended descriptor */
	edesc = skcipher_edesc_alloc(req, DESC_JOB_IO_LEN * CAAM_CMD_SZ);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Create and submit job descriptor*/
	init_skcipher_job(req, edesc, false);
	desc = edesc->hw_desc;

	print_hex_dump_debug("skcipher jobdesc@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, edesc->hw_desc,
			     desc_bytes(edesc->hw_desc), 1);

	ret = caam_jr_enqueue(jrdev, desc, skcipher_decrypt_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		skcipher_unmap(jrdev, edesc, req);
		kfree(edesc);
	}

	return ret;
}

static struct caam_skcipher_alg driver_algs[] = {
	{
		.skcipher = {
			.base = {
				.cra_name = "cbc(aes)",
				.cra_driver_name = "cbc-aes-caam",
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
				.cra_driver_name = "cbc-3des-caam",
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
				.cra_driver_name = "cbc-des-caam",
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
				.cra_driver_name = "ctr-aes-caam",
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
				.cra_driver_name = "rfc3686-ctr-aes-caam",
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
				.cra_driver_name = "xts-aes-caam",
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
				.cra_name = "ecb(des)",
				.cra_driver_name = "ecb-des-caam",
				.cra_blocksize = DES_BLOCK_SIZE,
			},
			.setkey = des_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_DES | OP_ALG_AAI_ECB,
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "ecb(aes)",
				.cra_driver_name = "ecb-aes-caam",
				.cra_blocksize = AES_BLOCK_SIZE,
			},
			.setkey = aes_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_ECB,
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "ecb(des3_ede)",
				.cra_driver_name = "ecb-des3-caam",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			},
			.setkey = des3_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_3DES | OP_ALG_AAI_ECB,
	},
	{
		.skcipher = {
			.base = {
				.cra_name = "ecb(arc4)",
				.cra_driver_name = "ecb-arc4-caam",
				.cra_blocksize = ARC4_BLOCK_SIZE,
			},
			.setkey = arc4_skcipher_setkey,
			.encrypt = skcipher_encrypt,
			.decrypt = skcipher_decrypt,
			.min_keysize = ARC4_MIN_KEY_SIZE,
			.max_keysize = ARC4_MAX_KEY_SIZE,
		},
		.caam.class1_alg_type = OP_ALG_ALGSEL_ARC4 | OP_ALG_AAI_ECB,
	},
};

static struct caam_aead_alg driver_aeads[] = {
	{
		.aead = {
			.base = {
				.cra_name = "rfc4106(gcm(aes))",
				.cra_driver_name = "rfc4106-gcm-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = rfc4106_setkey,
			.setauthsize = rfc4106_setauthsize,
			.encrypt = ipsec_gcm_encrypt,
			.decrypt = ipsec_gcm_decrypt,
			.ivsize = GCM_RFC4106_IV_SIZE,
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
				.cra_driver_name = "rfc4543-gcm-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = rfc4543_setkey,
			.setauthsize = rfc4543_setauthsize,
			.encrypt = ipsec_gcm_encrypt,
			.decrypt = ipsec_gcm_decrypt,
			.ivsize = GCM_RFC4543_IV_SIZE,
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
				.cra_driver_name = "gcm-aes-caam",
				.cra_blocksize = 1,
			},
			.setkey = gcm_setkey,
			.setauthsize = gcm_setauthsize,
			.encrypt = gcm_encrypt,
			.decrypt = gcm_decrypt,
			.ivsize = GCM_AES_IV_SIZE,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.caam = {
			.class1_alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_GCM,
			.nodkp = true,
		},
	},
	/* single-pass ipsec_esp descriptor */
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_MD5 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA1 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA224 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA256 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA384 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),"
					    "ecb(cipher_null))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "ecb-cipher_null-caam",
				.cra_blocksize = NULL_BLOCK_SIZE,
			},
			.setkey = aead_setkey,
			.setauthsize = aead_setauthsize,
			.encrypt = aead_encrypt,
			.decrypt = aead_decrypt,
			.ivsize = NULL_IV_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.caam = {
			.class2_alg_type = OP_ALG_ALGSEL_SHA512 |
					   OP_ALG_AAI_HMAC_PRECOMP,
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(aes))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(md5),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-hmac-md5-"
						   "cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha1),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha1-cbc-aes-caam",
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
						   "cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha224),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha224-cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha256),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha256-cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha384),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha384-cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha512),"
					    "cbc(aes)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha512-cbc-aes-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-des3_ede-caam",
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
						   "cbc-des3_ede-caam",
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
						   "cbc-des3_ede-caam",
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
						   "cbc-des3_ede-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-des3_ede-caam",
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
						   "cbc-des3_ede-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-des3_ede-caam",
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
						   "cbc-des3_ede-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-des3_ede-caam",
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
						   "cbc-des3_ede-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-des3_ede-caam",
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
						   "cbc-des3_ede-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(des))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-des-caam",
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
						   "cbc-des-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-des-caam",
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
						   "hmac-sha1-cbc-des-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-des-caam",
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
						   "hmac-sha224-cbc-des-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-des-caam",
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
						   "hmac-sha256-cbc-des-caam",
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
						   "cbc-des-caam",
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
						   "hmac-sha384-cbc-des-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(des))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-des-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "echainiv(authenc(hmac(sha512),"
					    "cbc(des)))",
				.cra_driver_name = "echainiv-authenc-"
						   "hmac-sha512-cbc-des-caam",
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
		},
	},
	{
		.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),"
					    "rfc3686(ctr(aes)))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
						   "rfc3686-ctr-aes-caam",
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
	{
		.aead = {
			.base = {
				.cra_name = "rfc7539(chacha20,poly1305)",
				.cra_driver_name = "rfc7539-chacha20-poly1305-"
						   "caam",
				.cra_blocksize = 1,
			},
			.setkey = chachapoly_setkey,
			.setauthsize = chachapoly_setauthsize,
			.encrypt = chachapoly_encrypt,
			.decrypt = chachapoly_decrypt,
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
						   "poly1305-caam",
				.cra_blocksize = 1,
			},
			.setkey = chachapoly_setkey,
			.setauthsize = chachapoly_setauthsize,
			.encrypt = chachapoly_encrypt,
			.decrypt = chachapoly_decrypt,
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
};

static int caam_init_common(struct caam_ctx *ctx, struct caam_alg_entry *caam,
			    bool uses_dkp)
{
	dma_addr_t dma_addr;
	struct caam_drv_private *priv;

	ctx->jrdev = caam_jr_alloc();
	if (IS_ERR(ctx->jrdev)) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return PTR_ERR(ctx->jrdev);
	}

	priv = dev_get_drvdata(ctx->jrdev->parent);
	if (priv->era >= 6 && uses_dkp)
		ctx->dir = DMA_BIDIRECTIONAL;
	else
		ctx->dir = DMA_TO_DEVICE;

	dma_addr = dma_map_single_attrs(ctx->jrdev, ctx->sh_desc_enc,
					offsetof(struct caam_ctx,
						 sh_desc_enc_dma),
					ctx->dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(ctx->jrdev, dma_addr)) {
		dev_err(ctx->jrdev, "unable to map key, shared descriptors\n");
		caam_jr_free(ctx->jrdev);
		return -ENOMEM;
	}

	ctx->sh_desc_enc_dma = dma_addr;
	ctx->sh_desc_dec_dma = dma_addr + offsetof(struct caam_ctx,
						   sh_desc_dec);
	ctx->key_dma = dma_addr + offsetof(struct caam_ctx, key);

	/* copy descriptor header template value */
	ctx->cdata.algtype = OP_TYPE_CLASS1_ALG | caam->class1_alg_type;
	ctx->adata.algtype = OP_TYPE_CLASS2_ALG | caam->class2_alg_type;

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
	struct caam_aead_alg *caam_alg =
		 container_of(alg, struct caam_aead_alg, aead);
	struct caam_ctx *ctx = crypto_aead_ctx(tfm);

	return caam_init_common(ctx, &caam_alg->caam, !caam_alg->caam.nodkp);
}

static void caam_exit_common(struct caam_ctx *ctx)
{
	dma_unmap_single_attrs(ctx->jrdev, ctx->sh_desc_enc_dma,
			       offsetof(struct caam_ctx, sh_desc_enc_dma),
			       ctx->dir, DMA_ATTR_SKIP_CPU_SYNC);
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

void caam_algapi_exit(void)
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

int caam_algapi_init(struct device *ctrldev)
{
	struct caam_drv_private *priv = dev_get_drvdata(ctrldev);
	int i = 0, err = 0;
	u32 aes_vid, aes_inst, des_inst, md_vid, md_inst, ccha_inst, ptha_inst;
	u32 arc4_inst;
	unsigned int md_limit = SHA512_DIGEST_SIZE;
	bool registered = false, gcm_support;

	/*
	 * Register crypto algorithms the device supports.
	 * First, detect presence and attributes of DES, AES, and MD blocks.
	 */
	if (priv->era < 10) {
		u32 cha_vid, cha_inst, aes_rn;

		cha_vid = rd_reg32(&priv->ctrl->perfmon.cha_id_ls);
		aes_vid = cha_vid & CHA_ID_LS_AES_MASK;
		md_vid = (cha_vid & CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT;

		cha_inst = rd_reg32(&priv->ctrl->perfmon.cha_num_ls);
		des_inst = (cha_inst & CHA_ID_LS_DES_MASK) >>
			   CHA_ID_LS_DES_SHIFT;
		aes_inst = cha_inst & CHA_ID_LS_AES_MASK;
		md_inst = (cha_inst & CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT;
		arc4_inst = (cha_inst & CHA_ID_LS_ARC4_MASK) >>
			    CHA_ID_LS_ARC4_SHIFT;
		ccha_inst = 0;
		ptha_inst = 0;

		aes_rn = rd_reg32(&priv->ctrl->perfmon.cha_rev_ls) &
			 CHA_ID_LS_AES_MASK;
		gcm_support = !(aes_vid == CHA_VER_VID_AES_LP && aes_rn < 8);
	} else {
		u32 aesa, mdha;

		aesa = rd_reg32(&priv->ctrl->vreg.aesa);
		mdha = rd_reg32(&priv->ctrl->vreg.mdha);

		aes_vid = (aesa & CHA_VER_VID_MASK) >> CHA_VER_VID_SHIFT;
		md_vid = (mdha & CHA_VER_VID_MASK) >> CHA_VER_VID_SHIFT;

		des_inst = rd_reg32(&priv->ctrl->vreg.desa) & CHA_VER_NUM_MASK;
		aes_inst = aesa & CHA_VER_NUM_MASK;
		md_inst = mdha & CHA_VER_NUM_MASK;
		ccha_inst = rd_reg32(&priv->ctrl->vreg.ccha) & CHA_VER_NUM_MASK;
		ptha_inst = rd_reg32(&priv->ctrl->vreg.ptha) & CHA_VER_NUM_MASK;
		arc4_inst = rd_reg32(&priv->ctrl->vreg.afha) & CHA_VER_NUM_MASK;

		gcm_support = aesa & CHA_VER_MISC_AES_GCM;
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

		/* Skip ARC4 algorithms if not supported by device */
		if (!arc4_inst && alg_sel == OP_ALG_ALGSEL_ARC4)
			continue;

		/*
		 * Check support for AES modes not available
		 * on LP devices.
		 */
		if (aes_vid == CHA_VER_VID_AES_LP &&
		    (t_alg->caam.class1_alg_type & OP_ALG_AAI_MASK) ==
		    OP_ALG_AAI_XTS)
			continue;

		caam_skcipher_alg_init(t_alg);

		err = crypto_register_skcipher(&t_alg->skcipher);
		if (err) {
			pr_warn("%s alg registration failed\n",
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

		/* Skip CHACHA20 algorithms if not supported by device */
		if (c1_alg_sel == OP_ALG_ALGSEL_CHACHA20 && !ccha_inst)
			continue;

		/* Skip POLY1305 algorithms if not supported by device */
		if (c2_alg_sel == OP_ALG_ALGSEL_POLY1305 && !ptha_inst)
			continue;

		/* Skip GCM algorithms if not supported by device */
		if (c1_alg_sel == OP_ALG_ALGSEL_AES &&
		    alg_aai == OP_ALG_AAI_GCM && !gcm_support)
			continue;

		/*
		 * Skip algorithms requiring message digests
		 * if MD or MD size is not supported by device.
		 */
		if (is_mdha(c2_alg_sel) &&
		    (!md_inst || t_alg->aead.maxauthsize > md_limit))
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
		pr_info("caam algorithms registered in /proc/crypto\n");

	return err;
}
