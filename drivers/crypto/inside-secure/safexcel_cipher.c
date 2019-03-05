// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Marvell
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/authenc.h>
#include <crypto/des.h>
#include <crypto/sha.h>
#include <crypto/skcipher.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>

#include "safexcel.h"

enum safexcel_cipher_direction {
	SAFEXCEL_ENCRYPT,
	SAFEXCEL_DECRYPT,
};

enum safexcel_cipher_alg {
	SAFEXCEL_DES,
	SAFEXCEL_3DES,
	SAFEXCEL_AES,
};

struct safexcel_cipher_ctx {
	struct safexcel_context base;
	struct safexcel_crypto_priv *priv;

	u32 mode;
	enum safexcel_cipher_alg alg;
	bool aead;

	__le32 key[8];
	unsigned int key_len;

	/* All the below is AEAD specific */
	u32 hash_alg;
	u32 state_sz;
	u32 ipad[SHA512_DIGEST_SIZE / sizeof(u32)];
	u32 opad[SHA512_DIGEST_SIZE / sizeof(u32)];
};

struct safexcel_cipher_req {
	enum safexcel_cipher_direction direction;
	bool needs_inv;
};

static void safexcel_skcipher_token(struct safexcel_cipher_ctx *ctx, u8 *iv,
				    struct safexcel_command_desc *cdesc,
				    u32 length)
{
	struct safexcel_token *token;
	unsigned offset = 0;

	if (ctx->mode == CONTEXT_CONTROL_CRYPTO_MODE_CBC) {
		switch (ctx->alg) {
		case SAFEXCEL_DES:
			offset = DES_BLOCK_SIZE / sizeof(u32);
			memcpy(cdesc->control_data.token, iv, DES_BLOCK_SIZE);
			cdesc->control_data.options |= EIP197_OPTION_2_TOKEN_IV_CMD;
			break;
		case SAFEXCEL_3DES:
			offset = DES3_EDE_BLOCK_SIZE / sizeof(u32);
			memcpy(cdesc->control_data.token, iv, DES3_EDE_BLOCK_SIZE);
			cdesc->control_data.options |= EIP197_OPTION_2_TOKEN_IV_CMD;
			break;

		case SAFEXCEL_AES:
			offset = AES_BLOCK_SIZE / sizeof(u32);
			memcpy(cdesc->control_data.token, iv, AES_BLOCK_SIZE);
			cdesc->control_data.options |= EIP197_OPTION_4_TOKEN_IV_CMD;
			break;
		}
	}

	token = (struct safexcel_token *)(cdesc->control_data.token + offset);

	token[0].opcode = EIP197_TOKEN_OPCODE_DIRECTION;
	token[0].packet_length = length;
	token[0].stat = EIP197_TOKEN_STAT_LAST_PACKET |
			EIP197_TOKEN_STAT_LAST_HASH;
	token[0].instructions = EIP197_TOKEN_INS_LAST |
				EIP197_TOKEN_INS_TYPE_CRYTO |
				EIP197_TOKEN_INS_TYPE_OUTPUT;
}

static void safexcel_aead_token(struct safexcel_cipher_ctx *ctx, u8 *iv,
				struct safexcel_command_desc *cdesc,
				enum safexcel_cipher_direction direction,
				u32 cryptlen, u32 assoclen, u32 digestsize)
{
	struct safexcel_token *token;
	unsigned offset = 0;

	if (ctx->mode == CONTEXT_CONTROL_CRYPTO_MODE_CBC) {
		offset = AES_BLOCK_SIZE / sizeof(u32);
		memcpy(cdesc->control_data.token, iv, AES_BLOCK_SIZE);

		cdesc->control_data.options |= EIP197_OPTION_4_TOKEN_IV_CMD;
	}

	token = (struct safexcel_token *)(cdesc->control_data.token + offset);

	if (direction == SAFEXCEL_DECRYPT)
		cryptlen -= digestsize;

	token[0].opcode = EIP197_TOKEN_OPCODE_DIRECTION;
	token[0].packet_length = assoclen;
	token[0].instructions = EIP197_TOKEN_INS_TYPE_HASH |
				EIP197_TOKEN_INS_TYPE_OUTPUT;

	token[1].opcode = EIP197_TOKEN_OPCODE_DIRECTION;
	token[1].packet_length = cryptlen;
	token[1].stat = EIP197_TOKEN_STAT_LAST_HASH;
	token[1].instructions = EIP197_TOKEN_INS_LAST |
				EIP197_TOKEN_INS_TYPE_CRYTO |
				EIP197_TOKEN_INS_TYPE_HASH |
				EIP197_TOKEN_INS_TYPE_OUTPUT;

	if (direction == SAFEXCEL_ENCRYPT) {
		token[2].opcode = EIP197_TOKEN_OPCODE_INSERT;
		token[2].packet_length = digestsize;
		token[2].stat = EIP197_TOKEN_STAT_LAST_HASH |
				EIP197_TOKEN_STAT_LAST_PACKET;
		token[2].instructions = EIP197_TOKEN_INS_TYPE_OUTPUT |
					EIP197_TOKEN_INS_INSERT_HASH_DIGEST;
	} else {
		token[2].opcode = EIP197_TOKEN_OPCODE_RETRIEVE;
		token[2].packet_length = digestsize;
		token[2].stat = EIP197_TOKEN_STAT_LAST_HASH |
				EIP197_TOKEN_STAT_LAST_PACKET;
		token[2].instructions = EIP197_TOKEN_INS_INSERT_HASH_DIGEST;

		token[3].opcode = EIP197_TOKEN_OPCODE_VERIFY;
		token[3].packet_length = digestsize |
					 EIP197_TOKEN_HASH_RESULT_VERIFY;
		token[3].stat = EIP197_TOKEN_STAT_LAST_HASH |
				EIP197_TOKEN_STAT_LAST_PACKET;
		token[3].instructions = EIP197_TOKEN_INS_TYPE_OUTPUT;
	}
}

static int safexcel_skcipher_aes_setkey(struct crypto_skcipher *ctfm,
					const u8 *key, unsigned int len)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct crypto_aes_ctx aes;
	int ret, i;

	ret = crypto_aes_expand_key(&aes, key, len);
	if (ret) {
		crypto_skcipher_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return ret;
	}

	if (priv->flags & EIP197_TRC_CACHE && ctx->base.ctxr_dma) {
		for (i = 0; i < len / sizeof(u32); i++) {
			if (ctx->key[i] != cpu_to_le32(aes.key_enc[i])) {
				ctx->base.needs_inv = true;
				break;
			}
		}
	}

	for (i = 0; i < len / sizeof(u32); i++)
		ctx->key[i] = cpu_to_le32(aes.key_enc[i]);

	ctx->key_len = len;

	memzero_explicit(&aes, sizeof(aes));
	return 0;
}

static int safexcel_aead_aes_setkey(struct crypto_aead *ctfm, const u8 *key,
				    unsigned int len)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_ahash_export_state istate, ostate;
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct crypto_authenc_keys keys;

	if (crypto_authenc_extractkeys(&keys, key, len) != 0)
		goto badkey;

	if (keys.enckeylen > sizeof(ctx->key))
		goto badkey;

	/* Encryption key */
	if (priv->flags & EIP197_TRC_CACHE && ctx->base.ctxr_dma &&
	    memcmp(ctx->key, keys.enckey, keys.enckeylen))
		ctx->base.needs_inv = true;

	/* Auth key */
	switch (ctx->hash_alg) {
	case CONTEXT_CONTROL_CRYPTO_ALG_SHA1:
		if (safexcel_hmac_setkey("safexcel-sha1", keys.authkey,
					 keys.authkeylen, &istate, &ostate))
			goto badkey;
		break;
	case CONTEXT_CONTROL_CRYPTO_ALG_SHA224:
		if (safexcel_hmac_setkey("safexcel-sha224", keys.authkey,
					 keys.authkeylen, &istate, &ostate))
			goto badkey;
		break;
	case CONTEXT_CONTROL_CRYPTO_ALG_SHA256:
		if (safexcel_hmac_setkey("safexcel-sha256", keys.authkey,
					 keys.authkeylen, &istate, &ostate))
			goto badkey;
		break;
	case CONTEXT_CONTROL_CRYPTO_ALG_SHA384:
		if (safexcel_hmac_setkey("safexcel-sha384", keys.authkey,
					 keys.authkeylen, &istate, &ostate))
			goto badkey;
		break;
	case CONTEXT_CONTROL_CRYPTO_ALG_SHA512:
		if (safexcel_hmac_setkey("safexcel-sha512", keys.authkey,
					 keys.authkeylen, &istate, &ostate))
			goto badkey;
		break;
	default:
		dev_err(priv->dev, "aead: unsupported hash algorithm\n");
		goto badkey;
	}

	crypto_aead_set_flags(ctfm, crypto_aead_get_flags(ctfm) &
				    CRYPTO_TFM_RES_MASK);

	if (priv->flags & EIP197_TRC_CACHE && ctx->base.ctxr_dma &&
	    (memcmp(ctx->ipad, istate.state, ctx->state_sz) ||
	     memcmp(ctx->opad, ostate.state, ctx->state_sz)))
		ctx->base.needs_inv = true;

	/* Now copy the keys into the context */
	memcpy(ctx->key, keys.enckey, keys.enckeylen);
	ctx->key_len = keys.enckeylen;

	memcpy(ctx->ipad, &istate.state, ctx->state_sz);
	memcpy(ctx->opad, &ostate.state, ctx->state_sz);

	memzero_explicit(&keys, sizeof(keys));
	return 0;

badkey:
	crypto_aead_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	memzero_explicit(&keys, sizeof(keys));
	return -EINVAL;
}

static int safexcel_context_control(struct safexcel_cipher_ctx *ctx,
				    struct crypto_async_request *async,
				    struct safexcel_cipher_req *sreq,
				    struct safexcel_command_desc *cdesc)
{
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ctrl_size;

	if (ctx->aead) {
		if (sreq->direction == SAFEXCEL_ENCRYPT)
			cdesc->control_data.control0 |= CONTEXT_CONTROL_TYPE_ENCRYPT_HASH_OUT;
		else
			cdesc->control_data.control0 |= CONTEXT_CONTROL_TYPE_HASH_DECRYPT_IN;
	} else {
		cdesc->control_data.control0 |= CONTEXT_CONTROL_TYPE_CRYPTO_OUT;

		/* The decryption control type is a combination of the
		 * encryption type and CONTEXT_CONTROL_TYPE_NULL_IN, for all
		 * types.
		 */
		if (sreq->direction == SAFEXCEL_DECRYPT)
			cdesc->control_data.control0 |= CONTEXT_CONTROL_TYPE_NULL_IN;
	}

	cdesc->control_data.control0 |= CONTEXT_CONTROL_KEY_EN;
	cdesc->control_data.control1 |= ctx->mode;

	if (ctx->aead)
		cdesc->control_data.control0 |= CONTEXT_CONTROL_DIGEST_HMAC |
						ctx->hash_alg;

	if (ctx->alg == SAFEXCEL_DES) {
		cdesc->control_data.control0 |= CONTEXT_CONTROL_CRYPTO_ALG_DES;
	} else if (ctx->alg == SAFEXCEL_3DES) {
		cdesc->control_data.control0 |= CONTEXT_CONTROL_CRYPTO_ALG_3DES;
	} else if (ctx->alg == SAFEXCEL_AES) {
		switch (ctx->key_len) {
		case AES_KEYSIZE_128:
			cdesc->control_data.control0 |= CONTEXT_CONTROL_CRYPTO_ALG_AES128;
			break;
		case AES_KEYSIZE_192:
			cdesc->control_data.control0 |= CONTEXT_CONTROL_CRYPTO_ALG_AES192;
			break;
		case AES_KEYSIZE_256:
			cdesc->control_data.control0 |= CONTEXT_CONTROL_CRYPTO_ALG_AES256;
			break;
		default:
			dev_err(priv->dev, "aes keysize not supported: %u\n",
				ctx->key_len);
			return -EINVAL;
		}
	}

	ctrl_size = ctx->key_len / sizeof(u32);
	if (ctx->aead)
		/* Take in account the ipad+opad digests */
		ctrl_size += ctx->state_sz / sizeof(u32) * 2;
	cdesc->control_data.control0 |= CONTEXT_CONTROL_SIZE(ctrl_size);

	return 0;
}

static int safexcel_handle_req_result(struct safexcel_crypto_priv *priv, int ring,
				      struct crypto_async_request *async,
				      struct scatterlist *src,
				      struct scatterlist *dst,
				      unsigned int cryptlen,
				      struct safexcel_cipher_req *sreq,
				      bool *should_complete, int *ret)
{
	struct safexcel_result_desc *rdesc;
	int ndesc = 0;

	*ret = 0;

	do {
		rdesc = safexcel_ring_next_rptr(priv, &priv->ring[ring].rdr);
		if (IS_ERR(rdesc)) {
			dev_err(priv->dev,
				"cipher: result: could not retrieve the result descriptor\n");
			*ret = PTR_ERR(rdesc);
			break;
		}

		if (likely(!*ret))
			*ret = safexcel_rdesc_check_errors(priv, rdesc);

		ndesc++;
	} while (!rdesc->last_seg);

	safexcel_complete(priv, ring);

	if (src == dst) {
		dma_unmap_sg(priv->dev, src,
			     sg_nents_for_len(src, cryptlen),
			     DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(priv->dev, src,
			     sg_nents_for_len(src, cryptlen),
			     DMA_TO_DEVICE);
		dma_unmap_sg(priv->dev, dst,
			     sg_nents_for_len(dst, cryptlen),
			     DMA_FROM_DEVICE);
	}

	*should_complete = true;

	return ndesc;
}

static int safexcel_send_req(struct crypto_async_request *base, int ring,
			     struct safexcel_cipher_req *sreq,
			     struct scatterlist *src, struct scatterlist *dst,
			     unsigned int cryptlen, unsigned int assoclen,
			     unsigned int digestsize, u8 *iv, int *commands,
			     int *results)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(base->tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct safexcel_command_desc *cdesc;
	struct safexcel_result_desc *rdesc, *first_rdesc = NULL;
	struct scatterlist *sg;
	unsigned int totlen = cryptlen + assoclen;
	int nr_src, nr_dst, n_cdesc = 0, n_rdesc = 0, queued = totlen;
	int i, ret = 0;

	if (src == dst) {
		nr_src = dma_map_sg(priv->dev, src,
				    sg_nents_for_len(src, totlen),
				    DMA_BIDIRECTIONAL);
		nr_dst = nr_src;
		if (!nr_src)
			return -EINVAL;
	} else {
		nr_src = dma_map_sg(priv->dev, src,
				    sg_nents_for_len(src, totlen),
				    DMA_TO_DEVICE);
		if (!nr_src)
			return -EINVAL;

		nr_dst = dma_map_sg(priv->dev, dst,
				    sg_nents_for_len(dst, totlen),
				    DMA_FROM_DEVICE);
		if (!nr_dst) {
			dma_unmap_sg(priv->dev, src,
				     sg_nents_for_len(src, totlen),
				     DMA_TO_DEVICE);
			return -EINVAL;
		}
	}

	memcpy(ctx->base.ctxr->data, ctx->key, ctx->key_len);

	if (ctx->aead) {
		memcpy(ctx->base.ctxr->data + ctx->key_len / sizeof(u32),
		       ctx->ipad, ctx->state_sz);
		memcpy(ctx->base.ctxr->data + (ctx->key_len + ctx->state_sz) / sizeof(u32),
		       ctx->opad, ctx->state_sz);
	}

	/* command descriptors */
	for_each_sg(src, sg, nr_src, i) {
		int len = sg_dma_len(sg);

		/* Do not overflow the request */
		if (queued - len < 0)
			len = queued;

		cdesc = safexcel_add_cdesc(priv, ring, !n_cdesc, !(queued - len),
					   sg_dma_address(sg), len, totlen,
					   ctx->base.ctxr_dma);
		if (IS_ERR(cdesc)) {
			/* No space left in the command descriptor ring */
			ret = PTR_ERR(cdesc);
			goto cdesc_rollback;
		}
		n_cdesc++;

		if (n_cdesc == 1) {
			safexcel_context_control(ctx, base, sreq, cdesc);
			if (ctx->aead)
				safexcel_aead_token(ctx, iv, cdesc,
						    sreq->direction, cryptlen,
						    assoclen, digestsize);
			else
				safexcel_skcipher_token(ctx, iv, cdesc,
							cryptlen);
		}

		queued -= len;
		if (!queued)
			break;
	}

	/* result descriptors */
	for_each_sg(dst, sg, nr_dst, i) {
		bool first = !i, last = (i == nr_dst - 1);
		u32 len = sg_dma_len(sg);

		rdesc = safexcel_add_rdesc(priv, ring, first, last,
					   sg_dma_address(sg), len);
		if (IS_ERR(rdesc)) {
			/* No space left in the result descriptor ring */
			ret = PTR_ERR(rdesc);
			goto rdesc_rollback;
		}
		if (first)
			first_rdesc = rdesc;
		n_rdesc++;
	}

	safexcel_rdr_req_set(priv, ring, first_rdesc, base);

	*commands = n_cdesc;
	*results = n_rdesc;
	return 0;

rdesc_rollback:
	for (i = 0; i < n_rdesc; i++)
		safexcel_ring_rollback_wptr(priv, &priv->ring[ring].rdr);
cdesc_rollback:
	for (i = 0; i < n_cdesc; i++)
		safexcel_ring_rollback_wptr(priv, &priv->ring[ring].cdr);

	if (src == dst) {
		dma_unmap_sg(priv->dev, src,
			     sg_nents_for_len(src, totlen),
			     DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(priv->dev, src,
			     sg_nents_for_len(src, totlen),
			     DMA_TO_DEVICE);
		dma_unmap_sg(priv->dev, dst,
			     sg_nents_for_len(dst, totlen),
			     DMA_FROM_DEVICE);
	}

	return ret;
}

static int safexcel_handle_inv_result(struct safexcel_crypto_priv *priv,
				      int ring,
				      struct crypto_async_request *base,
				      bool *should_complete, int *ret)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(base->tfm);
	struct safexcel_result_desc *rdesc;
	int ndesc = 0, enq_ret;

	*ret = 0;

	do {
		rdesc = safexcel_ring_next_rptr(priv, &priv->ring[ring].rdr);
		if (IS_ERR(rdesc)) {
			dev_err(priv->dev,
				"cipher: invalidate: could not retrieve the result descriptor\n");
			*ret = PTR_ERR(rdesc);
			break;
		}

		if (likely(!*ret))
			*ret = safexcel_rdesc_check_errors(priv, rdesc);

		ndesc++;
	} while (!rdesc->last_seg);

	safexcel_complete(priv, ring);

	if (ctx->base.exit_inv) {
		dma_pool_free(priv->context_pool, ctx->base.ctxr,
			      ctx->base.ctxr_dma);

		*should_complete = true;

		return ndesc;
	}

	ring = safexcel_select_ring(priv);
	ctx->base.ring = ring;

	spin_lock_bh(&priv->ring[ring].queue_lock);
	enq_ret = crypto_enqueue_request(&priv->ring[ring].queue, base);
	spin_unlock_bh(&priv->ring[ring].queue_lock);

	if (enq_ret != -EINPROGRESS)
		*ret = enq_ret;

	queue_work(priv->ring[ring].workqueue,
		   &priv->ring[ring].work_data.work);

	*should_complete = false;

	return ndesc;
}

static int safexcel_skcipher_handle_result(struct safexcel_crypto_priv *priv,
					   int ring,
					   struct crypto_async_request *async,
					   bool *should_complete, int *ret)
{
	struct skcipher_request *req = skcipher_request_cast(async);
	struct safexcel_cipher_req *sreq = skcipher_request_ctx(req);
	int err;

	if (sreq->needs_inv) {
		sreq->needs_inv = false;
		err = safexcel_handle_inv_result(priv, ring, async,
						 should_complete, ret);
	} else {
		err = safexcel_handle_req_result(priv, ring, async, req->src,
						 req->dst, req->cryptlen, sreq,
						 should_complete, ret);
	}

	return err;
}

static int safexcel_aead_handle_result(struct safexcel_crypto_priv *priv,
				       int ring,
				       struct crypto_async_request *async,
				       bool *should_complete, int *ret)
{
	struct aead_request *req = aead_request_cast(async);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct safexcel_cipher_req *sreq = aead_request_ctx(req);
	int err;

	if (sreq->needs_inv) {
		sreq->needs_inv = false;
		err = safexcel_handle_inv_result(priv, ring, async,
						 should_complete, ret);
	} else {
		err = safexcel_handle_req_result(priv, ring, async, req->src,
						 req->dst,
						 req->cryptlen + crypto_aead_authsize(tfm),
						 sreq, should_complete, ret);
	}

	return err;
}

static int safexcel_cipher_send_inv(struct crypto_async_request *base,
				    int ring, int *commands, int *results)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(base->tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ret;

	ret = safexcel_invalidate_cache(base, priv, ctx->base.ctxr_dma, ring);
	if (unlikely(ret))
		return ret;

	*commands = 1;
	*results = 1;

	return 0;
}

static int safexcel_skcipher_send(struct crypto_async_request *async, int ring,
				  int *commands, int *results)
{
	struct skcipher_request *req = skcipher_request_cast(async);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct safexcel_cipher_req *sreq = skcipher_request_ctx(req);
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ret;

	BUG_ON(!(priv->flags & EIP197_TRC_CACHE) && sreq->needs_inv);

	if (sreq->needs_inv)
		ret = safexcel_cipher_send_inv(async, ring, commands, results);
	else
		ret = safexcel_send_req(async, ring, sreq, req->src,
					req->dst, req->cryptlen, 0, 0, req->iv,
					commands, results);
	return ret;
}

static int safexcel_aead_send(struct crypto_async_request *async, int ring,
			      int *commands, int *results)
{
	struct aead_request *req = aead_request_cast(async);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct safexcel_cipher_req *sreq = aead_request_ctx(req);
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ret;

	BUG_ON(!(priv->flags & EIP197_TRC_CACHE) && sreq->needs_inv);

	if (sreq->needs_inv)
		ret = safexcel_cipher_send_inv(async, ring, commands, results);
	else
		ret = safexcel_send_req(async, ring, sreq, req->src, req->dst,
					req->cryptlen, req->assoclen,
					crypto_aead_authsize(tfm), req->iv,
					commands, results);
	return ret;
}

static int safexcel_cipher_exit_inv(struct crypto_tfm *tfm,
				    struct crypto_async_request *base,
				    struct safexcel_cipher_req *sreq,
				    struct safexcel_inv_result *result)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ring = ctx->base.ring;

	init_completion(&result->completion);

	ctx = crypto_tfm_ctx(base->tfm);
	ctx->base.exit_inv = true;
	sreq->needs_inv = true;

	spin_lock_bh(&priv->ring[ring].queue_lock);
	crypto_enqueue_request(&priv->ring[ring].queue, base);
	spin_unlock_bh(&priv->ring[ring].queue_lock);

	queue_work(priv->ring[ring].workqueue,
		   &priv->ring[ring].work_data.work);

	wait_for_completion(&result->completion);

	if (result->error) {
		dev_warn(priv->dev,
			"cipher: sync: invalidate: completion error %d\n",
			 result->error);
		return result->error;
	}

	return 0;
}

static int safexcel_skcipher_exit_inv(struct crypto_tfm *tfm)
{
	EIP197_REQUEST_ON_STACK(req, skcipher, EIP197_SKCIPHER_REQ_SIZE);
	struct safexcel_cipher_req *sreq = skcipher_request_ctx(req);
	struct safexcel_inv_result result = {};

	memset(req, 0, sizeof(struct skcipher_request));

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      safexcel_inv_complete, &result);
	skcipher_request_set_tfm(req, __crypto_skcipher_cast(tfm));

	return safexcel_cipher_exit_inv(tfm, &req->base, sreq, &result);
}

static int safexcel_aead_exit_inv(struct crypto_tfm *tfm)
{
	EIP197_REQUEST_ON_STACK(req, aead, EIP197_AEAD_REQ_SIZE);
	struct safexcel_cipher_req *sreq = aead_request_ctx(req);
	struct safexcel_inv_result result = {};

	memset(req, 0, sizeof(struct aead_request));

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  safexcel_inv_complete, &result);
	aead_request_set_tfm(req, __crypto_aead_cast(tfm));

	return safexcel_cipher_exit_inv(tfm, &req->base, sreq, &result);
}

static int safexcel_queue_req(struct crypto_async_request *base,
			struct safexcel_cipher_req *sreq,
			enum safexcel_cipher_direction dir, u32 mode,
			enum safexcel_cipher_alg alg)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(base->tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ret, ring;

	sreq->needs_inv = false;
	sreq->direction = dir;
	ctx->alg = alg;
	ctx->mode = mode;

	if (ctx->base.ctxr) {
		if (priv->flags & EIP197_TRC_CACHE && ctx->base.needs_inv) {
			sreq->needs_inv = true;
			ctx->base.needs_inv = false;
		}
	} else {
		ctx->base.ring = safexcel_select_ring(priv);
		ctx->base.ctxr = dma_pool_zalloc(priv->context_pool,
						 EIP197_GFP_FLAGS(*base),
						 &ctx->base.ctxr_dma);
		if (!ctx->base.ctxr)
			return -ENOMEM;
	}

	ring = ctx->base.ring;

	spin_lock_bh(&priv->ring[ring].queue_lock);
	ret = crypto_enqueue_request(&priv->ring[ring].queue, base);
	spin_unlock_bh(&priv->ring[ring].queue_lock);

	queue_work(priv->ring[ring].workqueue,
		   &priv->ring[ring].work_data.work);

	return ret;
}

static int safexcel_ecb_aes_encrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_ENCRYPT, CONTEXT_CONTROL_CRYPTO_MODE_ECB,
			SAFEXCEL_AES);
}

static int safexcel_ecb_aes_decrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_DECRYPT, CONTEXT_CONTROL_CRYPTO_MODE_ECB,
			SAFEXCEL_AES);
}

static int safexcel_skcipher_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_alg_template *tmpl =
		container_of(tfm->__crt_alg, struct safexcel_alg_template,
			     alg.skcipher.base);

	crypto_skcipher_set_reqsize(__crypto_skcipher_cast(tfm),
				    sizeof(struct safexcel_cipher_req));

	ctx->priv = tmpl->priv;

	ctx->base.send = safexcel_skcipher_send;
	ctx->base.handle_result = safexcel_skcipher_handle_result;
	return 0;
}

static int safexcel_cipher_cra_exit(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	memzero_explicit(ctx->key, sizeof(ctx->key));

	/* context not allocated, skip invalidation */
	if (!ctx->base.ctxr)
		return -ENOMEM;

	memzero_explicit(ctx->base.ctxr->data, sizeof(ctx->base.ctxr->data));
	return 0;
}

static void safexcel_skcipher_cra_exit(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ret;

	if (safexcel_cipher_cra_exit(tfm))
		return;

	if (priv->flags & EIP197_TRC_CACHE) {
		ret = safexcel_skcipher_exit_inv(tfm);
		if (ret)
			dev_warn(priv->dev, "skcipher: invalidation error %d\n",
				 ret);
	} else {
		dma_pool_free(priv->context_pool, ctx->base.ctxr,
			      ctx->base.ctxr_dma);
	}
}

static void safexcel_aead_cra_exit(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ret;

	if (safexcel_cipher_cra_exit(tfm))
		return;

	if (priv->flags & EIP197_TRC_CACHE) {
		ret = safexcel_aead_exit_inv(tfm);
		if (ret)
			dev_warn(priv->dev, "aead: invalidation error %d\n",
				 ret);
	} else {
		dma_pool_free(priv->context_pool, ctx->base.ctxr,
			      ctx->base.ctxr_dma);
	}
}

struct safexcel_alg_template safexcel_alg_ecb_aes = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.skcipher = {
		.setkey = safexcel_skcipher_aes_setkey,
		.encrypt = safexcel_ecb_aes_encrypt,
		.decrypt = safexcel_ecb_aes_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "safexcel-ecb-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_cbc_aes_encrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_ENCRYPT, CONTEXT_CONTROL_CRYPTO_MODE_CBC,
			SAFEXCEL_AES);
}

static int safexcel_cbc_aes_decrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_DECRYPT, CONTEXT_CONTROL_CRYPTO_MODE_CBC,
			SAFEXCEL_AES);
}

struct safexcel_alg_template safexcel_alg_cbc_aes = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.skcipher = {
		.setkey = safexcel_skcipher_aes_setkey,
		.encrypt = safexcel_cbc_aes_encrypt,
		.decrypt = safexcel_cbc_aes_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "safexcel-cbc-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_cbc_des_encrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_ENCRYPT, CONTEXT_CONTROL_CRYPTO_MODE_CBC,
			SAFEXCEL_DES);
}

static int safexcel_cbc_des_decrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_DECRYPT, CONTEXT_CONTROL_CRYPTO_MODE_CBC,
			SAFEXCEL_DES);
}

static int safexcel_des_setkey(struct crypto_skcipher *ctfm, const u8 *key,
			       unsigned int len)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 tmp[DES_EXPKEY_WORDS];
	int ret;

	if (len != DES_KEY_SIZE) {
		crypto_skcipher_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ret = des_ekey(tmp, key);
	if (!ret && (tfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	/* if context exits and key changed, need to invalidate it */
	if (ctx->base.ctxr_dma)
		if (memcmp(ctx->key, key, len))
			ctx->base.needs_inv = true;

	memcpy(ctx->key, key, len);
	ctx->key_len = len;

	return 0;
}

struct safexcel_alg_template safexcel_alg_cbc_des = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.skcipher = {
		.setkey = safexcel_des_setkey,
		.encrypt = safexcel_cbc_des_encrypt,
		.decrypt = safexcel_cbc_des_decrypt,
		.min_keysize = DES_KEY_SIZE,
		.max_keysize = DES_KEY_SIZE,
		.ivsize = DES_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(des)",
			.cra_driver_name = "safexcel-cbc-des",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_ecb_des_encrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_ENCRYPT, CONTEXT_CONTROL_CRYPTO_MODE_ECB,
			SAFEXCEL_DES);
}

static int safexcel_ecb_des_decrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_DECRYPT, CONTEXT_CONTROL_CRYPTO_MODE_ECB,
			SAFEXCEL_DES);
}

struct safexcel_alg_template safexcel_alg_ecb_des = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.skcipher = {
		.setkey = safexcel_des_setkey,
		.encrypt = safexcel_ecb_des_encrypt,
		.decrypt = safexcel_ecb_des_decrypt,
		.min_keysize = DES_KEY_SIZE,
		.max_keysize = DES_KEY_SIZE,
		.ivsize = DES_BLOCK_SIZE,
		.base = {
			.cra_name = "ecb(des)",
			.cra_driver_name = "safexcel-ecb-des",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_cbc_des3_ede_encrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_ENCRYPT, CONTEXT_CONTROL_CRYPTO_MODE_CBC,
			SAFEXCEL_3DES);
}

static int safexcel_cbc_des3_ede_decrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_DECRYPT, CONTEXT_CONTROL_CRYPTO_MODE_CBC,
			SAFEXCEL_3DES);
}

static int safexcel_des3_ede_setkey(struct crypto_skcipher *ctfm,
				   const u8 *key, unsigned int len)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	if (len != DES3_EDE_KEY_SIZE) {
		crypto_skcipher_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	/* if context exits and key changed, need to invalidate it */
	if (ctx->base.ctxr_dma) {
		if (memcmp(ctx->key, key, len))
			ctx->base.needs_inv = true;
	}

	memcpy(ctx->key, key, len);

	ctx->key_len = len;

	return 0;
}

struct safexcel_alg_template safexcel_alg_cbc_des3_ede = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.skcipher = {
		.setkey = safexcel_des3_ede_setkey,
		.encrypt = safexcel_cbc_des3_ede_encrypt,
		.decrypt = safexcel_cbc_des3_ede_decrypt,
		.min_keysize = DES3_EDE_KEY_SIZE,
		.max_keysize = DES3_EDE_KEY_SIZE,
		.ivsize = DES3_EDE_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "safexcel-cbc-des3_ede",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_ecb_des3_ede_encrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_ENCRYPT, CONTEXT_CONTROL_CRYPTO_MODE_ECB,
			SAFEXCEL_3DES);
}

static int safexcel_ecb_des3_ede_decrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_DECRYPT, CONTEXT_CONTROL_CRYPTO_MODE_ECB,
			SAFEXCEL_3DES);
}

struct safexcel_alg_template safexcel_alg_ecb_des3_ede = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.skcipher = {
		.setkey = safexcel_des3_ede_setkey,
		.encrypt = safexcel_ecb_des3_ede_encrypt,
		.decrypt = safexcel_ecb_des3_ede_decrypt,
		.min_keysize = DES3_EDE_KEY_SIZE,
		.max_keysize = DES3_EDE_KEY_SIZE,
		.ivsize = DES3_EDE_BLOCK_SIZE,
		.base = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "safexcel-ecb-des3_ede",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_encrypt(struct aead_request *req)
{
	struct safexcel_cipher_req *creq = aead_request_ctx(req);

	return safexcel_queue_req(&req->base, creq, SAFEXCEL_ENCRYPT,
			CONTEXT_CONTROL_CRYPTO_MODE_CBC, SAFEXCEL_AES);
}

static int safexcel_aead_decrypt(struct aead_request *req)
{
	struct safexcel_cipher_req *creq = aead_request_ctx(req);

	return safexcel_queue_req(&req->base, creq, SAFEXCEL_DECRYPT,
			CONTEXT_CONTROL_CRYPTO_MODE_CBC, SAFEXCEL_AES);
}

static int safexcel_aead_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_alg_template *tmpl =
		container_of(tfm->__crt_alg, struct safexcel_alg_template,
			     alg.aead.base);

	crypto_aead_set_reqsize(__crypto_aead_cast(tfm),
				sizeof(struct safexcel_cipher_req));

	ctx->priv = tmpl->priv;

	ctx->aead = true;
	ctx->base.send = safexcel_aead_send;
	ctx->base.handle_result = safexcel_aead_handle_result;
	return 0;
}

static int safexcel_aead_sha1_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_cra_init(tfm);
	ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_SHA1;
	ctx->state_sz = SHA1_DIGEST_SIZE;
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_cbc_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.aead = {
		.setkey = safexcel_aead_aes_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha1),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha1-cbc-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha1_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha256_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_cra_init(tfm);
	ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_SHA256;
	ctx->state_sz = SHA256_DIGEST_SIZE;
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha256_cbc_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.aead = {
		.setkey = safexcel_aead_aes_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA256_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha256),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha256-cbc-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha256_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha224_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_cra_init(tfm);
	ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_SHA224;
	ctx->state_sz = SHA256_DIGEST_SIZE;
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha224_cbc_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.aead = {
		.setkey = safexcel_aead_aes_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA224_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha224),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha224-cbc-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha224_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha512_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_cra_init(tfm);
	ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_SHA512;
	ctx->state_sz = SHA512_DIGEST_SIZE;
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha512_cbc_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.aead = {
		.setkey = safexcel_aead_aes_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA512_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha512),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha512-cbc-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha512_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha384_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_cra_init(tfm);
	ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_SHA384;
	ctx->state_sz = SHA512_DIGEST_SIZE;
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha384_cbc_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.engines = EIP97IES | EIP197B | EIP197D,
	.alg.aead = {
		.setkey = safexcel_aead_aes_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA384_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha384),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha384-cbc-aes",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha384_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};
