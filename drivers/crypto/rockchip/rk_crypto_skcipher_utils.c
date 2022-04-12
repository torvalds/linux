// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip crypto skcipher uitls
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */

#include "rk_crypto_skcipher_utils.h"

struct rk_crypto_algt *rk_cipher_get_algt(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);

	return container_of(alg, struct rk_crypto_algt, alg.crypto);
}

struct rk_crypto_algt *rk_aead_get_algt(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);

	return container_of(alg, struct rk_crypto_algt, alg.aead);
}

struct rk_cipher_ctx *rk_cipher_ctx_cast(struct rk_crypto_dev *rk_dev)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(rk_dev->async_req->tfm);

	return ctx;
}

struct rk_alg_ctx *rk_cipher_alg_ctx(struct rk_crypto_dev *rk_dev)
{
	return &(rk_cipher_ctx_cast(rk_dev)->algs_ctx);
}

static bool is_no_multi_blocksize(uint32_t mode)
{
	return (mode == CIPHER_MODE_CFB ||
		mode == CIPHER_MODE_OFB ||
		mode == CIPHER_MODE_CTR ||
		mode == CIPHER_MODE_XTS ||
		mode == CIPHER_MODE_GCM) ? true : false;
}

int rk_cipher_fallback(struct skcipher_request *req, struct rk_cipher_ctx *ctx, bool encrypt)
{
	int ret;

	CRYPTO_MSG("use fallback tfm");

	if (!ctx->fallback_tfm) {
		ret = -ENODEV;
		CRYPTO_MSG("fallback_tfm is empty!\n");
		goto exit;
	}

	if (!ctx->fallback_key_inited) {
		ret = crypto_skcipher_setkey(ctx->fallback_tfm,
					     ctx->key, ctx->keylen);
		if (ret) {
			CRYPTO_MSG("fallback crypto_skcipher_setkey err = %d\n",
				   ret);
			goto exit;
		}

		ctx->fallback_key_inited = true;
	}

	skcipher_request_set_tfm(&ctx->fallback_req, ctx->fallback_tfm);
	skcipher_request_set_callback(&ctx->fallback_req,
				      req->base.flags,
				      req->base.complete,
				      req->base.data);

	skcipher_request_set_crypt(&ctx->fallback_req, req->src,
				   req->dst, req->cryptlen, req->iv);

	ret = encrypt ? crypto_skcipher_encrypt(&ctx->fallback_req) :
			crypto_skcipher_decrypt(&ctx->fallback_req);

exit:
	return ret;
}

/* increment counter (128-bit int) by 1 */
static void rk_ctr128_inc(uint8_t *counter)
{
	u32 n = 16;
	u8  c;

	do {
		--n;
		c = counter[n];
		++c;
		counter[n] = c;
		if (c)
			return;
	} while (n);
}

static void rk_ctr128_calc(uint8_t *counter, uint32_t data_len)
{
	u32 i;
	u32 chunksize = AES_BLOCK_SIZE;

	for (i = 0; i < DIV_ROUND_UP(data_len, chunksize); i++)
		rk_ctr128_inc(counter);
}

static uint32_t rk_get_new_iv(struct rk_cipher_ctx *ctx, u32 mode, bool is_enc, uint8_t *iv)
{
	struct scatterlist *sg_dst;
	struct rk_alg_ctx *alg_ctx = &ctx->algs_ctx;
	uint32_t ivsize = alg_ctx->chunk_size;

	if (!iv)
		return 0;

	sg_dst = alg_ctx->aligned ? alg_ctx->sg_dst : &alg_ctx->sg_tmp;

	CRYPTO_TRACE("aligned = %u, count = %u, ivsize = %u, is_enc = %d\n",
		     alg_ctx->aligned, alg_ctx->count, ivsize, is_enc);

	switch (mode) {
	case CIPHER_MODE_CTR:
		rk_ctr128_calc(iv, alg_ctx->count);
		break;
	case CIPHER_MODE_CBC:
	case CIPHER_MODE_CFB:
		if (is_enc)
			sg_pcopy_to_buffer(sg_dst, alg_ctx->map_nents,
					   iv, ivsize, alg_ctx->count - ivsize);
		else
			memcpy(iv, ctx->lastc, ivsize);
		break;
	case CIPHER_MODE_OFB:
		sg_pcopy_to_buffer(sg_dst, alg_ctx->map_nents,
				   iv, ivsize, alg_ctx->count - ivsize);
		crypto_xor(iv, ctx->lastc, ivsize);
		break;
	default:
		return 0;
	}

	return ivsize;
}

static void rk_iv_copyback(struct rk_crypto_dev *rk_dev)
{
	uint32_t iv_size;
	struct skcipher_request *req = skcipher_request_cast(rk_dev->async_req);
	struct rk_cipher_ctx *ctx = rk_cipher_ctx_cast(rk_dev);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct rk_crypto_algt *algt = rk_cipher_get_algt(cipher);

	iv_size = rk_get_new_iv(ctx, algt->mode, ctx->is_enc, ctx->iv);

	if (iv_size && req->iv)
		memcpy(req->iv, ctx->iv, iv_size);
}

static void rk_update_iv(struct rk_crypto_dev *rk_dev)
{
	uint32_t iv_size;
	struct rk_cipher_ctx *ctx = rk_cipher_ctx_cast(rk_dev);
	struct rk_alg_ctx *algs_ctx = &ctx->algs_ctx;
	struct skcipher_request *req = skcipher_request_cast(rk_dev->async_req);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct rk_crypto_algt *algt = rk_cipher_get_algt(cipher);

	iv_size = rk_get_new_iv(ctx, algt->mode, ctx->is_enc, ctx->iv);

	if (iv_size)
		algs_ctx->ops.hw_write_iv(rk_dev, ctx->iv, iv_size);
}

static int rk_set_data_start(struct rk_crypto_dev *rk_dev)
{
	int err;
	struct rk_alg_ctx *alg_ctx = rk_cipher_alg_ctx(rk_dev);

	err = rk_dev->load_data(rk_dev, alg_ctx->sg_src, alg_ctx->sg_dst);
	if (!err) {
		u32 ivsize = alg_ctx->chunk_size;
		struct scatterlist *src_sg;
		struct rk_cipher_ctx *ctx = rk_cipher_ctx_cast(rk_dev);

		memset(ctx->lastc, 0x00, sizeof(ctx->lastc));

		src_sg = alg_ctx->aligned ? alg_ctx->sg_src : &alg_ctx->sg_tmp;

		ivsize = alg_ctx->count > ivsize ? ivsize : alg_ctx->count;

		sg_pcopy_to_buffer(src_sg, alg_ctx->map_nents,
				   ctx->lastc, ivsize, alg_ctx->count - ivsize);

		alg_ctx->ops.hw_dma_start(rk_dev, true);
	}

	return err;
}

int rk_cipher_setkey(struct crypto_skcipher *cipher, const u8 *key, unsigned int keylen)
{
	struct rk_crypto_algt *algt = rk_cipher_get_algt(cipher);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	uint32_t key_factor;
	int ret = -EINVAL;

	CRYPTO_MSG("algo = %x, mode = %x, key_len = %d\n",
		   algt->algo, algt->mode, keylen);

	/* The key length of XTS is twice the normal length */
	key_factor = algt->mode == CIPHER_MODE_XTS ? 2 : 1;

	switch (algt->algo) {
	case CIPHER_ALGO_DES:
		ret = verify_skcipher_des_key(cipher, key);
		if (ret)
			goto exit;
		break;
	case CIPHER_ALGO_DES3_EDE:
		ret = verify_skcipher_des3_key(cipher, key);
		if (ret)
			goto exit;
		break;
	case CIPHER_ALGO_AES:
		if (keylen != (AES_KEYSIZE_128 * key_factor) &&
		    keylen != (AES_KEYSIZE_192 * key_factor) &&
		    keylen != (AES_KEYSIZE_256 * key_factor))
			goto exit;
		break;
	case CIPHER_ALGO_SM4:
		if (keylen != (SM4_KEY_SIZE * key_factor))
			goto exit;
		break;
	default:
		ret = -EINVAL;
		goto exit;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;
	ctx->fallback_key_inited = false;

	ret = 0;
exit:
	return ret;
}

int rk_ablk_rx(struct rk_crypto_dev *rk_dev)
{
	int err = 0;
	struct rk_cipher_ctx *ctx = rk_cipher_ctx_cast(rk_dev);
	struct rk_alg_ctx *alg_ctx = rk_cipher_alg_ctx(rk_dev);

	CRYPTO_TRACE("left_bytes = %u\n", alg_ctx->left_bytes);

	err = rk_dev->unload_data(rk_dev);
	if (err)
		goto out_rx;

	if (alg_ctx->left_bytes) {
		rk_update_iv(rk_dev);
		if (alg_ctx->aligned) {
			if (sg_is_last(alg_ctx->sg_src)) {
				dev_err(rk_dev->dev, "[%s:%d] Lack of data\n",
					__func__, __LINE__);
				err = -ENOMEM;
				goto out_rx;
			}
			alg_ctx->sg_src = sg_next(alg_ctx->sg_src);
			alg_ctx->sg_dst = sg_next(alg_ctx->sg_dst);
		}
		err = rk_set_data_start(rk_dev);
	} else {
		if (alg_ctx->is_aead) {
			u8 hard_tag[RK_MAX_TAG_SIZE];
			u8 user_tag[RK_MAX_TAG_SIZE];
			struct aead_request *req =
				aead_request_cast(rk_dev->async_req);
			struct crypto_aead *tfm = crypto_aead_reqtfm(req);

			unsigned int authsize = crypto_aead_authsize(tfm);

			CRYPTO_TRACE("cryptlen = %u, assoclen = %u, aead authsize = %u",
				     alg_ctx->total, alg_ctx->assoclen, authsize);

			err = alg_ctx->ops.hw_get_result(rk_dev, hard_tag, authsize);
			if (err)
				goto out_rx;

			CRYPTO_DUMPHEX("hard_tag", hard_tag, authsize);
			if (!ctx->is_enc) {
				if (!sg_pcopy_to_buffer(alg_ctx->req_src,
							sg_nents(alg_ctx->req_src),
							user_tag, authsize,
							alg_ctx->total +
							alg_ctx->assoclen)) {
					err = -EINVAL;
					goto out_rx;
				}

				CRYPTO_DUMPHEX("user_tag", user_tag, authsize);
				err = crypto_memneq(user_tag, hard_tag, authsize) ? -EBADMSG : 0;
			} else {
				if (!sg_pcopy_from_buffer(alg_ctx->req_dst,
							  sg_nents(alg_ctx->req_dst),
							  hard_tag, authsize,
							  alg_ctx->total +
							  alg_ctx->assoclen)) {
					err = -EINVAL;
					goto out_rx;
				}
			}
		} else {
			rk_iv_copyback(rk_dev);
		}
	}
out_rx:
	return err;
}

int rk_ablk_start(struct rk_crypto_dev *rk_dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(rk_dev->async_req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_crypto_algt *algt = rk_cipher_get_algt(tfm);
	struct rk_alg_ctx *alg_ctx = rk_cipher_alg_ctx(rk_dev);
	unsigned long flags;
	int err = 0;

	alg_ctx->left_bytes = req->cryptlen;
	alg_ctx->total      = req->cryptlen;
	alg_ctx->sg_src     = req->src;
	alg_ctx->req_src    = req->src;
	alg_ctx->src_nents  = sg_nents_for_len(req->src, req->cryptlen);
	alg_ctx->sg_dst     = req->dst;
	alg_ctx->req_dst    = req->dst;
	alg_ctx->dst_nents  = sg_nents_for_len(req->dst, req->cryptlen);

	CRYPTO_TRACE("total = %u", alg_ctx->total);

	spin_lock_irqsave(&rk_dev->lock, flags);
	alg_ctx->ops.hw_init(rk_dev, algt->algo, algt->mode);
	err = rk_set_data_start(rk_dev);
	spin_unlock_irqrestore(&rk_dev->lock, flags);
	return err;
}

int rk_skcipher_handle_req(struct rk_crypto_dev *rk_dev, struct skcipher_request *req)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct rk_crypto_algt *algt = rk_cipher_get_algt(cipher);

	if (!IS_ALIGNED(req->cryptlen, ctx->algs_ctx.chunk_size) &&
	    !is_no_multi_blocksize(algt->mode))
		return -EINVAL;
	else
		return rk_dev->enqueue(rk_dev, &req->base);
}

int rk_aead_fallback(struct aead_request *req, struct rk_cipher_ctx *ctx, bool encrypt)
{
	int ret;
	struct aead_request *subreq = aead_request_ctx(req);

	if (!ctx->fallback_aead) {
		CRYPTO_TRACE("fallback_tfm is empty");
		return -EINVAL;
	}

	CRYPTO_MSG("use fallback tfm");

	if (!ctx->fallback_key_inited) {
		ret = crypto_aead_setkey(ctx->fallback_aead, ctx->key, ctx->keylen);
		if (ret) {
			CRYPTO_MSG("fallback crypto_skcipher_setkey err = %d\n", ret);
			goto exit;
		}

		ctx->fallback_key_inited = true;
	}

	aead_request_set_tfm(subreq, ctx->fallback_aead);
	aead_request_set_callback(subreq, req->base.flags, req->base.complete, req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst, req->cryptlen, req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	ret = encrypt ? crypto_aead_encrypt(subreq) : crypto_aead_decrypt(subreq);

exit:
	return ret;
}

int rk_aead_setkey(struct crypto_aead *cipher, const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(cipher);
	struct rk_crypto_algt *algt = rk_aead_get_algt(cipher);
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	int ret = -EINVAL;

	CRYPTO_MSG("algo = %x, mode = %x, key_len = %d\n", algt->algo, algt->mode, keylen);

	switch (algt->algo) {
	case CIPHER_ALGO_AES:
		if (keylen != AES_KEYSIZE_128 &&
		    keylen != AES_KEYSIZE_192 &&
		    keylen != AES_KEYSIZE_256)
			goto error;

		break;
	case CIPHER_ALGO_SM4:
		if (keylen != SM4_KEY_SIZE)
			goto error;

		break;
	default:
		CRYPTO_TRACE();
		goto error;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;
	ctx->fallback_key_inited = false;

	return 0;

error:
	return ret;
}

int rk_aead_start(struct rk_crypto_dev *rk_dev)
{
	struct aead_request *req = aead_request_cast(rk_dev->async_req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_aead_ctx(tfm);
	struct rk_crypto_algt *algt = rk_aead_get_algt(tfm);
	struct rk_alg_ctx *alg_ctx = rk_cipher_alg_ctx(rk_dev);
	unsigned int total = 0, authsize;
	unsigned long flags;
	int err = 0;

	total = req->cryptlen + req->assoclen;

	authsize = ctx->is_enc ? 0 : crypto_aead_authsize(tfm);

	alg_ctx->total      = req->cryptlen - authsize;
	alg_ctx->assoclen   = req->assoclen;
	alg_ctx->sg_src     = req->src;
	alg_ctx->req_src    = req->src;
	alg_ctx->src_nents  = sg_nents_for_len(req->src, total);
	alg_ctx->sg_dst     = req->dst;
	alg_ctx->req_dst    = req->dst;
	alg_ctx->dst_nents  = sg_nents_for_len(req->dst, total - authsize);
	alg_ctx->left_bytes = alg_ctx->total;

	CRYPTO_TRACE("src_nents = %zu, dst_nents = %zu", alg_ctx->src_nents, alg_ctx->dst_nents);
	CRYPTO_TRACE("is_enc = %d, authsize = %u, cryptlen = %u, total = %u, assoclen = %u",
		     ctx->is_enc, authsize, req->cryptlen, alg_ctx->total, alg_ctx->assoclen);

	spin_lock_irqsave(&rk_dev->lock, flags);
	alg_ctx->ops.hw_init(rk_dev, algt->algo, algt->mode);
	err = rk_set_data_start(rk_dev);
	spin_unlock_irqrestore(&rk_dev->lock, flags);
	return err;
}

int rk_aead_gcm_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	return crypto_gcm_check_authsize(authsize);
}

int rk_aead_handle_req(struct rk_crypto_dev *rk_dev, struct aead_request *req)
{
	return rk_dev->enqueue(rk_dev, &req->base);
}
