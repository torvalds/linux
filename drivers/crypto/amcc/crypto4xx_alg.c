// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
 *
 * This file implements the Linux crypto algorithms.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/spinlock_types.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/hash.h>
#include <crypto/internal/hash.h>
#include <linux/dma-mapping.h>
#include <crypto/algapi.h>
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/sha1.h>
#include <crypto/ctr.h>
#include <crypto/skcipher.h>
#include "crypto4xx_reg_def.h"
#include "crypto4xx_core.h"
#include "crypto4xx_sa.h"

static void set_dynamic_sa_command_0(struct dynamic_sa_ctl *sa, u32 save_h,
				     u32 save_iv, u32 ld_h, u32 ld_iv,
				     u32 hdr_proc, u32 h, u32 c, u32 pad_type,
				     u32 op_grp, u32 op, u32 dir)
{
	sa->sa_command_0.w = 0;
	sa->sa_command_0.bf.save_hash_state = save_h;
	sa->sa_command_0.bf.save_iv = save_iv;
	sa->sa_command_0.bf.load_hash_state = ld_h;
	sa->sa_command_0.bf.load_iv = ld_iv;
	sa->sa_command_0.bf.hdr_proc = hdr_proc;
	sa->sa_command_0.bf.hash_alg = h;
	sa->sa_command_0.bf.cipher_alg = c;
	sa->sa_command_0.bf.pad_type = pad_type & 3;
	sa->sa_command_0.bf.extend_pad = pad_type >> 2;
	sa->sa_command_0.bf.op_group = op_grp;
	sa->sa_command_0.bf.opcode = op;
	sa->sa_command_0.bf.dir = dir;
}

static void set_dynamic_sa_command_1(struct dynamic_sa_ctl *sa, u32 cm,
				     u32 hmac_mc, u32 cfb, u32 esn,
				     u32 sn_mask, u32 mute, u32 cp_pad,
				     u32 cp_pay, u32 cp_hdr)
{
	sa->sa_command_1.w = 0;
	sa->sa_command_1.bf.crypto_mode31 = (cm & 4) >> 2;
	sa->sa_command_1.bf.crypto_mode9_8 = cm & 3;
	sa->sa_command_1.bf.feedback_mode = cfb;
	sa->sa_command_1.bf.sa_rev = 1;
	sa->sa_command_1.bf.hmac_muting = hmac_mc;
	sa->sa_command_1.bf.extended_seq_num = esn;
	sa->sa_command_1.bf.seq_num_mask = sn_mask;
	sa->sa_command_1.bf.mutable_bit_proc = mute;
	sa->sa_command_1.bf.copy_pad = cp_pad;
	sa->sa_command_1.bf.copy_payload = cp_pay;
	sa->sa_command_1.bf.copy_hdr = cp_hdr;
}

static inline int crypto4xx_crypt(struct skcipher_request *req,
				  const unsigned int ivlen, bool decrypt,
				  bool check_blocksize)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct crypto4xx_ctx *ctx = crypto_skcipher_ctx(cipher);
	__le32 iv[AES_IV_SIZE];

	if (check_blocksize && !IS_ALIGNED(req->cryptlen, AES_BLOCK_SIZE))
		return -EINVAL;

	if (ivlen)
		crypto4xx_memcpy_to_le32(iv, req->iv, ivlen);

	return crypto4xx_build_pd(&req->base, ctx, req->src, req->dst,
		req->cryptlen, iv, ivlen, decrypt ? ctx->sa_in : ctx->sa_out,
		ctx->sa_len, 0, NULL);
}

int crypto4xx_encrypt_noiv_block(struct skcipher_request *req)
{
	return crypto4xx_crypt(req, 0, false, true);
}

int crypto4xx_encrypt_iv_stream(struct skcipher_request *req)
{
	return crypto4xx_crypt(req, AES_IV_SIZE, false, false);
}

int crypto4xx_decrypt_noiv_block(struct skcipher_request *req)
{
	return crypto4xx_crypt(req, 0, true, true);
}

int crypto4xx_decrypt_iv_stream(struct skcipher_request *req)
{
	return crypto4xx_crypt(req, AES_IV_SIZE, true, false);
}

int crypto4xx_encrypt_iv_block(struct skcipher_request *req)
{
	return crypto4xx_crypt(req, AES_IV_SIZE, false, true);
}

int crypto4xx_decrypt_iv_block(struct skcipher_request *req)
{
	return crypto4xx_crypt(req, AES_IV_SIZE, true, true);
}

/*
 * AES Functions
 */
static int crypto4xx_setkey_aes(struct crypto_skcipher *cipher,
				const u8 *key,
				unsigned int keylen,
				unsigned char cm,
				u8 fb)
{
	struct crypto4xx_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct dynamic_sa_ctl *sa;
	int    rc;

	if (keylen != AES_KEYSIZE_256 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_128)
		return -EINVAL;

	/* Create SA */
	if (ctx->sa_in || ctx->sa_out)
		crypto4xx_free_sa(ctx);

	rc = crypto4xx_alloc_sa(ctx, SA_AES128_LEN + (keylen-16) / 4);
	if (rc)
		return rc;

	/* Setup SA */
	sa = ctx->sa_in;

	set_dynamic_sa_command_0(sa, SA_NOT_SAVE_HASH, (cm == CRYPTO_MODE_ECB ?
				 SA_NOT_SAVE_IV : SA_SAVE_IV),
				 SA_NOT_LOAD_HASH, (cm == CRYPTO_MODE_ECB ?
				 SA_LOAD_IV_FROM_SA : SA_LOAD_IV_FROM_STATE),
				 SA_NO_HEADER_PROC, SA_HASH_ALG_NULL,
				 SA_CIPHER_ALG_AES, SA_PAD_TYPE_ZERO,
				 SA_OP_GROUP_BASIC, SA_OPCODE_DECRYPT,
				 DIR_INBOUND);

	set_dynamic_sa_command_1(sa, cm, SA_HASH_MODE_HASH,
				 fb, SA_EXTENDED_SN_OFF,
				 SA_SEQ_MASK_OFF, SA_MC_ENABLE,
				 SA_NOT_COPY_PAD, SA_NOT_COPY_PAYLOAD,
				 SA_NOT_COPY_HDR);
	crypto4xx_memcpy_to_le32(get_dynamic_sa_key_field(sa),
				 key, keylen);
	sa->sa_contents.w = SA_AES_CONTENTS | (keylen << 2);
	sa->sa_command_1.bf.key_len = keylen >> 3;

	memcpy(ctx->sa_out, ctx->sa_in, ctx->sa_len * 4);
	sa = ctx->sa_out;
	sa->sa_command_0.bf.dir = DIR_OUTBOUND;
	/*
	 * SA_OPCODE_ENCRYPT is the same value as SA_OPCODE_DECRYPT.
	 * it's the DIR_(IN|OUT)BOUND that matters
	 */
	sa->sa_command_0.bf.opcode = SA_OPCODE_ENCRYPT;

	return 0;
}

int crypto4xx_setkey_aes_cbc(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	return crypto4xx_setkey_aes(cipher, key, keylen, CRYPTO_MODE_CBC,
				    CRYPTO_FEEDBACK_MODE_NO_FB);
}

int crypto4xx_setkey_aes_cfb(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	return crypto4xx_setkey_aes(cipher, key, keylen, CRYPTO_MODE_CFB,
				    CRYPTO_FEEDBACK_MODE_128BIT_CFB);
}

int crypto4xx_setkey_aes_ecb(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	return crypto4xx_setkey_aes(cipher, key, keylen, CRYPTO_MODE_ECB,
				    CRYPTO_FEEDBACK_MODE_NO_FB);
}

int crypto4xx_setkey_aes_ofb(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	return crypto4xx_setkey_aes(cipher, key, keylen, CRYPTO_MODE_OFB,
				    CRYPTO_FEEDBACK_MODE_64BIT_OFB);
}

int crypto4xx_setkey_rfc3686(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	struct crypto4xx_ctx *ctx = crypto_skcipher_ctx(cipher);
	int rc;

	rc = crypto4xx_setkey_aes(cipher, key, keylen - CTR_RFC3686_NONCE_SIZE,
		CRYPTO_MODE_CTR, CRYPTO_FEEDBACK_MODE_NO_FB);
	if (rc)
		return rc;

	ctx->iv_nonce = cpu_to_le32p((u32 *)&key[keylen -
						 CTR_RFC3686_NONCE_SIZE]);

	return 0;
}

int crypto4xx_rfc3686_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct crypto4xx_ctx *ctx = crypto_skcipher_ctx(cipher);
	__le32 iv[AES_IV_SIZE / 4] = {
		ctx->iv_nonce,
		cpu_to_le32p((u32 *) req->iv),
		cpu_to_le32p((u32 *) (req->iv + 4)),
		cpu_to_le32(1) };

	return crypto4xx_build_pd(&req->base, ctx, req->src, req->dst,
				  req->cryptlen, iv, AES_IV_SIZE,
				  ctx->sa_out, ctx->sa_len, 0, NULL);
}

int crypto4xx_rfc3686_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct crypto4xx_ctx *ctx = crypto_skcipher_ctx(cipher);
	__le32 iv[AES_IV_SIZE / 4] = {
		ctx->iv_nonce,
		cpu_to_le32p((u32 *) req->iv),
		cpu_to_le32p((u32 *) (req->iv + 4)),
		cpu_to_le32(1) };

	return crypto4xx_build_pd(&req->base, ctx, req->src, req->dst,
				  req->cryptlen, iv, AES_IV_SIZE,
				  ctx->sa_out, ctx->sa_len, 0, NULL);
}

static int
crypto4xx_ctr_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct crypto4xx_ctx *ctx = crypto_skcipher_ctx(cipher);
	size_t iv_len = crypto_skcipher_ivsize(cipher);
	unsigned int counter = be32_to_cpup((__be32 *)(req->iv + iv_len - 4));
	unsigned int nblks = ALIGN(req->cryptlen, AES_BLOCK_SIZE) /
			AES_BLOCK_SIZE;

	/*
	 * The hardware uses only the last 32-bits as the counter while the
	 * kernel tests (aes_ctr_enc_tv_template[4] for example) expect that
	 * the whole IV is a counter.  So fallback if the counter is going to
	 * overlow.
	 */
	if (counter + nblks < counter) {
		SYNC_SKCIPHER_REQUEST_ON_STACK(subreq, ctx->sw_cipher.cipher);
		int ret;

		skcipher_request_set_sync_tfm(subreq, ctx->sw_cipher.cipher);
		skcipher_request_set_callback(subreq, req->base.flags,
			NULL, NULL);
		skcipher_request_set_crypt(subreq, req->src, req->dst,
			req->cryptlen, req->iv);
		ret = encrypt ? crypto_skcipher_encrypt(subreq)
			: crypto_skcipher_decrypt(subreq);
		skcipher_request_zero(subreq);
		return ret;
	}

	return encrypt ? crypto4xx_encrypt_iv_stream(req)
		       : crypto4xx_decrypt_iv_stream(req);
}

static int crypto4xx_sk_setup_fallback(struct crypto4xx_ctx *ctx,
				       struct crypto_skcipher *cipher,
				       const u8 *key,
				       unsigned int keylen)
{
	crypto_sync_skcipher_clear_flags(ctx->sw_cipher.cipher,
				    CRYPTO_TFM_REQ_MASK);
	crypto_sync_skcipher_set_flags(ctx->sw_cipher.cipher,
		crypto_skcipher_get_flags(cipher) & CRYPTO_TFM_REQ_MASK);
	return crypto_sync_skcipher_setkey(ctx->sw_cipher.cipher, key, keylen);
}

int crypto4xx_setkey_aes_ctr(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	struct crypto4xx_ctx *ctx = crypto_skcipher_ctx(cipher);
	int rc;

	rc = crypto4xx_sk_setup_fallback(ctx, cipher, key, keylen);
	if (rc)
		return rc;

	return crypto4xx_setkey_aes(cipher, key, keylen,
		CRYPTO_MODE_CTR, CRYPTO_FEEDBACK_MODE_NO_FB);
}

int crypto4xx_encrypt_ctr(struct skcipher_request *req)
{
	return crypto4xx_ctr_crypt(req, true);
}

int crypto4xx_decrypt_ctr(struct skcipher_request *req)
{
	return crypto4xx_ctr_crypt(req, false);
}

static inline bool crypto4xx_aead_need_fallback(struct aead_request *req,
						unsigned int len,
						bool is_ccm, bool decrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);

	/* authsize has to be a multiple of 4 */
	if (aead->authsize & 3)
		return true;

	/*
	 * hardware does not handle cases where plaintext
	 * is less than a block.
	 */
	if (len < AES_BLOCK_SIZE)
		return true;

	/* assoc len needs to be a multiple of 4 and <= 1020 */
	if (req->assoclen & 0x3 || req->assoclen > 1020)
		return true;

	/* CCM supports only counter field length of 2 and 4 bytes */
	if (is_ccm && !(req->iv[0] == 1 || req->iv[0] == 3))
		return true;

	return false;
}

static int crypto4xx_aead_fallback(struct aead_request *req,
	struct crypto4xx_ctx *ctx, bool do_decrypt)
{
	struct aead_request *subreq = aead_request_ctx(req);

	aead_request_set_tfm(subreq, ctx->sw_cipher.aead);
	aead_request_set_callback(subreq, req->base.flags,
				  req->base.complete, req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
			       req->iv);
	aead_request_set_ad(subreq, req->assoclen);
	return do_decrypt ? crypto_aead_decrypt(subreq) :
			    crypto_aead_encrypt(subreq);
}

static int crypto4xx_aead_setup_fallback(struct crypto4xx_ctx *ctx,
					 struct crypto_aead *cipher,
					 const u8 *key,
					 unsigned int keylen)
{
	crypto_aead_clear_flags(ctx->sw_cipher.aead, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(ctx->sw_cipher.aead,
		crypto_aead_get_flags(cipher) & CRYPTO_TFM_REQ_MASK);
	return crypto_aead_setkey(ctx->sw_cipher.aead, key, keylen);
}

/*
 * AES-CCM Functions
 */

int crypto4xx_setkey_aes_ccm(struct crypto_aead *cipher, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(cipher);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);
	struct dynamic_sa_ctl *sa;
	int rc = 0;

	rc = crypto4xx_aead_setup_fallback(ctx, cipher, key, keylen);
	if (rc)
		return rc;

	if (ctx->sa_in || ctx->sa_out)
		crypto4xx_free_sa(ctx);

	rc = crypto4xx_alloc_sa(ctx, SA_AES128_CCM_LEN + (keylen - 16) / 4);
	if (rc)
		return rc;

	/* Setup SA */
	sa = (struct dynamic_sa_ctl *) ctx->sa_in;
	sa->sa_contents.w = SA_AES_CCM_CONTENTS | (keylen << 2);

	set_dynamic_sa_command_0(sa, SA_SAVE_HASH, SA_NOT_SAVE_IV,
				 SA_LOAD_HASH_FROM_SA, SA_LOAD_IV_FROM_STATE,
				 SA_NO_HEADER_PROC, SA_HASH_ALG_CBC_MAC,
				 SA_CIPHER_ALG_AES,
				 SA_PAD_TYPE_ZERO, SA_OP_GROUP_BASIC,
				 SA_OPCODE_HASH_DECRYPT, DIR_INBOUND);

	set_dynamic_sa_command_1(sa, CRYPTO_MODE_CTR, SA_HASH_MODE_HASH,
				 CRYPTO_FEEDBACK_MODE_NO_FB, SA_EXTENDED_SN_OFF,
				 SA_SEQ_MASK_OFF, SA_MC_ENABLE,
				 SA_NOT_COPY_PAD, SA_COPY_PAYLOAD,
				 SA_NOT_COPY_HDR);

	sa->sa_command_1.bf.key_len = keylen >> 3;

	crypto4xx_memcpy_to_le32(get_dynamic_sa_key_field(sa), key, keylen);

	memcpy(ctx->sa_out, ctx->sa_in, ctx->sa_len * 4);
	sa = (struct dynamic_sa_ctl *) ctx->sa_out;

	set_dynamic_sa_command_0(sa, SA_SAVE_HASH, SA_NOT_SAVE_IV,
				 SA_LOAD_HASH_FROM_SA, SA_LOAD_IV_FROM_STATE,
				 SA_NO_HEADER_PROC, SA_HASH_ALG_CBC_MAC,
				 SA_CIPHER_ALG_AES,
				 SA_PAD_TYPE_ZERO, SA_OP_GROUP_BASIC,
				 SA_OPCODE_ENCRYPT_HASH, DIR_OUTBOUND);

	set_dynamic_sa_command_1(sa, CRYPTO_MODE_CTR, SA_HASH_MODE_HASH,
				 CRYPTO_FEEDBACK_MODE_NO_FB, SA_EXTENDED_SN_OFF,
				 SA_SEQ_MASK_OFF, SA_MC_ENABLE,
				 SA_COPY_PAD, SA_COPY_PAYLOAD,
				 SA_NOT_COPY_HDR);

	sa->sa_command_1.bf.key_len = keylen >> 3;
	return 0;
}

static int crypto4xx_crypt_aes_ccm(struct aead_request *req, bool decrypt)
{
	struct crypto4xx_ctx *ctx  = crypto_tfm_ctx(req->base.tfm);
	struct crypto4xx_aead_reqctx *rctx = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	__le32 iv[16];
	u32 tmp_sa[SA_AES128_CCM_LEN + 4];
	struct dynamic_sa_ctl *sa = (struct dynamic_sa_ctl *)tmp_sa;
	unsigned int len = req->cryptlen;

	if (decrypt)
		len -= crypto_aead_authsize(aead);

	if (crypto4xx_aead_need_fallback(req, len, true, decrypt))
		return crypto4xx_aead_fallback(req, ctx, decrypt);

	memcpy(tmp_sa, decrypt ? ctx->sa_in : ctx->sa_out, ctx->sa_len * 4);
	sa->sa_command_0.bf.digest_len = crypto_aead_authsize(aead) >> 2;

	if (req->iv[0] == 1) {
		/* CRYPTO_MODE_AES_ICM */
		sa->sa_command_1.bf.crypto_mode9_8 = 1;
	}

	iv[3] = cpu_to_le32(0);
	crypto4xx_memcpy_to_le32(iv, req->iv, 16 - (req->iv[0] + 1));

	return crypto4xx_build_pd(&req->base, ctx, req->src, req->dst,
				  len, iv, sizeof(iv),
				  sa, ctx->sa_len, req->assoclen, rctx->dst);
}

int crypto4xx_encrypt_aes_ccm(struct aead_request *req)
{
	return crypto4xx_crypt_aes_ccm(req, false);
}

int crypto4xx_decrypt_aes_ccm(struct aead_request *req)
{
	return crypto4xx_crypt_aes_ccm(req, true);
}

int crypto4xx_setauthsize_aead(struct crypto_aead *cipher,
			       unsigned int authsize)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(cipher);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);

	return crypto_aead_setauthsize(ctx->sw_cipher.aead, authsize);
}

/*
 * AES-GCM Functions
 */

static int crypto4xx_aes_gcm_validate_keylen(unsigned int keylen)
{
	switch (keylen) {
	case 16:
	case 24:
	case 32:
		return 0;
	default:
		return -EINVAL;
	}
}

static int crypto4xx_compute_gcm_hash_key_sw(__le32 *hash_start, const u8 *key,
					     unsigned int keylen)
{
	struct crypto_aes_ctx ctx;
	uint8_t src[16] = { 0 };
	int rc;

	rc = aes_expandkey(&ctx, key, keylen);
	if (rc) {
		pr_err("aes_expandkey() failed: %d\n", rc);
		return rc;
	}

	aes_encrypt(&ctx, src, src);
	crypto4xx_memcpy_to_le32(hash_start, src, 16);
	memzero_explicit(&ctx, sizeof(ctx));
	return 0;
}

int crypto4xx_setkey_aes_gcm(struct crypto_aead *cipher,
			     const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(cipher);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);
	struct dynamic_sa_ctl *sa;
	int    rc = 0;

	if (crypto4xx_aes_gcm_validate_keylen(keylen) != 0)
		return -EINVAL;

	rc = crypto4xx_aead_setup_fallback(ctx, cipher, key, keylen);
	if (rc)
		return rc;

	if (ctx->sa_in || ctx->sa_out)
		crypto4xx_free_sa(ctx);

	rc = crypto4xx_alloc_sa(ctx, SA_AES128_GCM_LEN + (keylen - 16) / 4);
	if (rc)
		return rc;

	sa  = (struct dynamic_sa_ctl *) ctx->sa_in;

	sa->sa_contents.w = SA_AES_GCM_CONTENTS | (keylen << 2);
	set_dynamic_sa_command_0(sa, SA_SAVE_HASH, SA_NOT_SAVE_IV,
				 SA_LOAD_HASH_FROM_SA, SA_LOAD_IV_FROM_STATE,
				 SA_NO_HEADER_PROC, SA_HASH_ALG_GHASH,
				 SA_CIPHER_ALG_AES, SA_PAD_TYPE_ZERO,
				 SA_OP_GROUP_BASIC, SA_OPCODE_HASH_DECRYPT,
				 DIR_INBOUND);
	set_dynamic_sa_command_1(sa, CRYPTO_MODE_CTR, SA_HASH_MODE_HASH,
				 CRYPTO_FEEDBACK_MODE_NO_FB, SA_EXTENDED_SN_OFF,
				 SA_SEQ_MASK_ON, SA_MC_DISABLE,
				 SA_NOT_COPY_PAD, SA_COPY_PAYLOAD,
				 SA_NOT_COPY_HDR);

	sa->sa_command_1.bf.key_len = keylen >> 3;

	crypto4xx_memcpy_to_le32(get_dynamic_sa_key_field(sa),
				 key, keylen);

	rc = crypto4xx_compute_gcm_hash_key_sw(get_dynamic_sa_inner_digest(sa),
		key, keylen);
	if (rc) {
		pr_err("GCM hash key setting failed = %d\n", rc);
		goto err;
	}

	memcpy(ctx->sa_out, ctx->sa_in, ctx->sa_len * 4);
	sa = (struct dynamic_sa_ctl *) ctx->sa_out;
	sa->sa_command_0.bf.dir = DIR_OUTBOUND;
	sa->sa_command_0.bf.opcode = SA_OPCODE_ENCRYPT_HASH;

	return 0;
err:
	crypto4xx_free_sa(ctx);
	return rc;
}

static inline int crypto4xx_crypt_aes_gcm(struct aead_request *req,
					  bool decrypt)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct crypto4xx_aead_reqctx *rctx = aead_request_ctx(req);
	__le32 iv[4];
	unsigned int len = req->cryptlen;

	if (decrypt)
		len -= crypto_aead_authsize(crypto_aead_reqtfm(req));

	if (crypto4xx_aead_need_fallback(req, len, false, decrypt))
		return crypto4xx_aead_fallback(req, ctx, decrypt);

	crypto4xx_memcpy_to_le32(iv, req->iv, GCM_AES_IV_SIZE);
	iv[3] = cpu_to_le32(1);

	return crypto4xx_build_pd(&req->base, ctx, req->src, req->dst,
				  len, iv, sizeof(iv),
				  decrypt ? ctx->sa_in : ctx->sa_out,
				  ctx->sa_len, req->assoclen, rctx->dst);
}

int crypto4xx_encrypt_aes_gcm(struct aead_request *req)
{
	return crypto4xx_crypt_aes_gcm(req, false);
}

int crypto4xx_decrypt_aes_gcm(struct aead_request *req)
{
	return crypto4xx_crypt_aes_gcm(req, true);
}

/*
 * HASH SHA1 Functions
 */
static int crypto4xx_hash_alg_init(struct crypto_tfm *tfm,
				   unsigned int sa_len,
				   unsigned char ha,
				   unsigned char hm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct crypto4xx_alg *my_alg;
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);
	struct dynamic_sa_hash160 *sa;
	int rc;

	my_alg = container_of(__crypto_ahash_alg(alg), struct crypto4xx_alg,
			      alg.u.hash);
	ctx->dev   = my_alg->dev;

	/* Create SA */
	if (ctx->sa_in || ctx->sa_out)
		crypto4xx_free_sa(ctx);

	rc = crypto4xx_alloc_sa(ctx, sa_len);
	if (rc)
		return rc;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct crypto4xx_ctx));
	sa = (struct dynamic_sa_hash160 *)ctx->sa_in;
	set_dynamic_sa_command_0(&sa->ctrl, SA_SAVE_HASH, SA_NOT_SAVE_IV,
				 SA_NOT_LOAD_HASH, SA_LOAD_IV_FROM_SA,
				 SA_NO_HEADER_PROC, ha, SA_CIPHER_ALG_NULL,
				 SA_PAD_TYPE_ZERO, SA_OP_GROUP_BASIC,
				 SA_OPCODE_HASH, DIR_INBOUND);
	set_dynamic_sa_command_1(&sa->ctrl, 0, SA_HASH_MODE_HASH,
				 CRYPTO_FEEDBACK_MODE_NO_FB, SA_EXTENDED_SN_OFF,
				 SA_SEQ_MASK_OFF, SA_MC_ENABLE,
				 SA_NOT_COPY_PAD, SA_NOT_COPY_PAYLOAD,
				 SA_NOT_COPY_HDR);
	/* Need to zero hash digest in SA */
	memset(sa->inner_digest, 0, sizeof(sa->inner_digest));
	memset(sa->outer_digest, 0, sizeof(sa->outer_digest));

	return 0;
}

int crypto4xx_hash_init(struct ahash_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	int ds;
	struct dynamic_sa_ctl *sa;

	sa = ctx->sa_in;
	ds = crypto_ahash_digestsize(
			__crypto_ahash_cast(req->base.tfm));
	sa->sa_command_0.bf.digest_len = ds >> 2;
	sa->sa_command_0.bf.load_hash_state = SA_LOAD_HASH_FROM_SA;

	return 0;
}

int crypto4xx_hash_update(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct scatterlist dst;
	unsigned int ds = crypto_ahash_digestsize(ahash);

	sg_init_one(&dst, req->result, ds);

	return crypto4xx_build_pd(&req->base, ctx, req->src, &dst,
				  req->nbytes, NULL, 0, ctx->sa_in,
				  ctx->sa_len, 0, NULL);
}

int crypto4xx_hash_final(struct ahash_request *req)
{
	return 0;
}

int crypto4xx_hash_digest(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct scatterlist dst;
	unsigned int ds = crypto_ahash_digestsize(ahash);

	sg_init_one(&dst, req->result, ds);

	return crypto4xx_build_pd(&req->base, ctx, req->src, &dst,
				  req->nbytes, NULL, 0, ctx->sa_in,
				  ctx->sa_len, 0, NULL);
}

/*
 * SHA1 Algorithm
 */
int crypto4xx_sha1_alg_init(struct crypto_tfm *tfm)
{
	return crypto4xx_hash_alg_init(tfm, SA_HASH160_LEN, SA_HASH_ALG_SHA1,
				       SA_HASH_MODE_HASH);
}
