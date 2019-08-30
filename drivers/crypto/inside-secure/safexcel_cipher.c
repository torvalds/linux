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
#include <crypto/ctr.h>
#include <crypto/internal/des.h>
#include <crypto/gcm.h>
#include <crypto/ghash.h>
#include <crypto/sha.h>
#include <crypto/xts.h>
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
	int  xcm; /* 0=authenc, 1=GCM, 2 reserved for CCM */

	__le32 key[16];
	u32 nonce;
	unsigned int key_len, xts;

	/* All the below is AEAD specific */
	u32 hash_alg;
	u32 state_sz;
	u32 ipad[SHA512_DIGEST_SIZE / sizeof(u32)];
	u32 opad[SHA512_DIGEST_SIZE / sizeof(u32)];

	struct crypto_cipher *hkaes;
};

struct safexcel_cipher_req {
	enum safexcel_cipher_direction direction;
	/* Number of result descriptors associated to the request */
	unsigned int rdescs;
	bool needs_inv;
	int  nr_src, nr_dst;
};

static void safexcel_cipher_token(struct safexcel_cipher_ctx *ctx, u8 *iv,
				  struct safexcel_command_desc *cdesc)
{
	u32 block_sz = 0;

	if (ctx->mode == CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD) {
		cdesc->control_data.options |= EIP197_OPTION_4_TOKEN_IV_CMD;

		/* 32 bit nonce */
		cdesc->control_data.token[0] = ctx->nonce;
		/* 64 bit IV part */
		memcpy(&cdesc->control_data.token[1], iv, 8);
		/* 32 bit counter, start at 1 (big endian!) */
		cdesc->control_data.token[3] = cpu_to_be32(1);

		return;
	} else if (ctx->xcm == EIP197_XCM_MODE_GCM) {
		cdesc->control_data.options |= EIP197_OPTION_4_TOKEN_IV_CMD;

		/* 96 bit IV part */
		memcpy(&cdesc->control_data.token[0], iv, 12);
		/* 32 bit counter, start at 1 (big endian!) */
		cdesc->control_data.token[3] = cpu_to_be32(1);

		return;
	} else if (ctx->xcm == EIP197_XCM_MODE_CCM) {
		cdesc->control_data.options |= EIP197_OPTION_4_TOKEN_IV_CMD;

		/* Variable length IV part */
		memcpy(&cdesc->control_data.token[0], iv, 15 - iv[0]);
		/* Start variable length counter at 0 */
		memset((u8 *)&cdesc->control_data.token[0] + 15 - iv[0],
		       0, iv[0] + 1);

		return;
	}

	if (ctx->mode != CONTEXT_CONTROL_CRYPTO_MODE_ECB) {
		switch (ctx->alg) {
		case SAFEXCEL_DES:
			block_sz = DES_BLOCK_SIZE;
			cdesc->control_data.options |= EIP197_OPTION_2_TOKEN_IV_CMD;
			break;
		case SAFEXCEL_3DES:
			block_sz = DES3_EDE_BLOCK_SIZE;
			cdesc->control_data.options |= EIP197_OPTION_2_TOKEN_IV_CMD;
			break;
		case SAFEXCEL_AES:
			block_sz = AES_BLOCK_SIZE;
			cdesc->control_data.options |= EIP197_OPTION_4_TOKEN_IV_CMD;
			break;
		}
		memcpy(cdesc->control_data.token, iv, block_sz);
	}
}

static void safexcel_skcipher_token(struct safexcel_cipher_ctx *ctx, u8 *iv,
				    struct safexcel_command_desc *cdesc,
				    u32 length)
{
	struct safexcel_token *token;

	safexcel_cipher_token(ctx, iv, cdesc);

	/* skip over worst case IV of 4 dwords, no need to be exact */
	token = (struct safexcel_token *)(cdesc->control_data.token + 4);

	token[0].opcode = EIP197_TOKEN_OPCODE_DIRECTION;
	token[0].packet_length = length;
	token[0].stat = EIP197_TOKEN_STAT_LAST_PACKET |
			EIP197_TOKEN_STAT_LAST_HASH;
	token[0].instructions = EIP197_TOKEN_INS_LAST |
				EIP197_TOKEN_INS_TYPE_CRYPTO |
				EIP197_TOKEN_INS_TYPE_OUTPUT;
}

static void safexcel_aead_token(struct safexcel_cipher_ctx *ctx, u8 *iv,
				struct safexcel_command_desc *cdesc,
				enum safexcel_cipher_direction direction,
				u32 cryptlen, u32 assoclen, u32 digestsize)
{
	struct safexcel_token *token;

	safexcel_cipher_token(ctx, iv, cdesc);

	if (direction == SAFEXCEL_ENCRYPT) {
		/* align end of instruction sequence to end of token */
		token = (struct safexcel_token *)(cdesc->control_data.token +
			 EIP197_MAX_TOKENS - 13);

		token[12].opcode = EIP197_TOKEN_OPCODE_INSERT;
		token[12].packet_length = digestsize;
		token[12].stat = EIP197_TOKEN_STAT_LAST_HASH |
				 EIP197_TOKEN_STAT_LAST_PACKET;
		token[12].instructions = EIP197_TOKEN_INS_TYPE_OUTPUT |
					 EIP197_TOKEN_INS_INSERT_HASH_DIGEST;
	} else {
		cryptlen -= digestsize;

		/* align end of instruction sequence to end of token */
		token = (struct safexcel_token *)(cdesc->control_data.token +
			 EIP197_MAX_TOKENS - 14);

		token[12].opcode = EIP197_TOKEN_OPCODE_RETRIEVE;
		token[12].packet_length = digestsize;
		token[12].stat = EIP197_TOKEN_STAT_LAST_HASH |
				 EIP197_TOKEN_STAT_LAST_PACKET;
		token[12].instructions = EIP197_TOKEN_INS_INSERT_HASH_DIGEST;

		token[13].opcode = EIP197_TOKEN_OPCODE_VERIFY;
		token[13].packet_length = digestsize |
					  EIP197_TOKEN_HASH_RESULT_VERIFY;
		token[13].stat = EIP197_TOKEN_STAT_LAST_HASH |
				 EIP197_TOKEN_STAT_LAST_PACKET;
		token[13].instructions = EIP197_TOKEN_INS_TYPE_OUTPUT;
	}

	token[6].opcode = EIP197_TOKEN_OPCODE_DIRECTION;
	token[6].packet_length = assoclen;

	if (likely(cryptlen)) {
		token[6].instructions = EIP197_TOKEN_INS_TYPE_HASH;

		token[10].opcode = EIP197_TOKEN_OPCODE_DIRECTION;
		token[10].packet_length = cryptlen;
		token[10].stat = EIP197_TOKEN_STAT_LAST_HASH;
		token[10].instructions = EIP197_TOKEN_INS_LAST |
					 EIP197_TOKEN_INS_TYPE_CRYPTO |
					 EIP197_TOKEN_INS_TYPE_HASH |
					 EIP197_TOKEN_INS_TYPE_OUTPUT;
	} else if (ctx->xcm != EIP197_XCM_MODE_CCM) {
		token[6].stat = EIP197_TOKEN_STAT_LAST_HASH;
		token[6].instructions = EIP197_TOKEN_INS_LAST |
					EIP197_TOKEN_INS_TYPE_HASH;
	}

	if (!ctx->xcm)
		return;

	token[8].opcode = EIP197_TOKEN_OPCODE_INSERT_REMRES;
	token[8].packet_length = 0;
	token[8].instructions = AES_BLOCK_SIZE;

	token[9].opcode = EIP197_TOKEN_OPCODE_INSERT;
	token[9].packet_length = AES_BLOCK_SIZE;
	token[9].instructions = EIP197_TOKEN_INS_TYPE_OUTPUT |
				EIP197_TOKEN_INS_TYPE_CRYPTO;

	if (ctx->xcm == EIP197_XCM_MODE_GCM) {
		token[6].instructions = EIP197_TOKEN_INS_LAST |
					EIP197_TOKEN_INS_TYPE_HASH;
	} else {
		u8 *cbcmaciv = (u8 *)&token[1];
		u32 *aadlen = (u32 *)&token[5];

		/* Construct IV block B0 for the CBC-MAC */
		token[0].opcode = EIP197_TOKEN_OPCODE_INSERT;
		token[0].packet_length = AES_BLOCK_SIZE +
					 ((assoclen > 0) << 1);
		token[0].instructions = EIP197_TOKEN_INS_ORIGIN_TOKEN |
					EIP197_TOKEN_INS_TYPE_HASH;
		/* Variable length IV part */
		memcpy(cbcmaciv, iv, 15 - iv[0]);
		/* fixup flags byte */
		cbcmaciv[0] |= ((assoclen > 0) << 6) | ((digestsize - 2) << 2);
		/* Clear upper bytes of variable message length to 0 */
		memset(cbcmaciv + 15 - iv[0], 0, iv[0] - 1);
		/* insert lower 2 bytes of message length */
		cbcmaciv[14] = cryptlen >> 8;
		cbcmaciv[15] = cryptlen & 255;

		if (assoclen) {
			*aadlen = cpu_to_le32(cpu_to_be16(assoclen));
			assoclen += 2;
		}

		token[6].instructions = EIP197_TOKEN_INS_TYPE_HASH;

		/* Align AAD data towards hash engine */
		token[7].opcode = EIP197_TOKEN_OPCODE_INSERT;
		assoclen &= 15;
		token[7].packet_length = assoclen ? 16 - assoclen : 0;

		if (likely(cryptlen)) {
			token[7].instructions = EIP197_TOKEN_INS_TYPE_HASH;

			/* Align crypto data towards hash engine */
			token[10].stat = 0;

			token[11].opcode = EIP197_TOKEN_OPCODE_INSERT;
			cryptlen &= 15;
			token[11].packet_length = cryptlen ? 16 - cryptlen : 0;
			token[11].stat = EIP197_TOKEN_STAT_LAST_HASH;
			token[11].instructions = EIP197_TOKEN_INS_TYPE_HASH;
		} else {
			token[7].stat = EIP197_TOKEN_STAT_LAST_HASH;
			token[7].instructions = EIP197_TOKEN_INS_LAST |
						EIP197_TOKEN_INS_TYPE_HASH;
		}
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

	ret = aes_expandkey(&aes, key, len);
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

static int safexcel_aead_setkey(struct crypto_aead *ctfm, const u8 *key,
				unsigned int len)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_ahash_export_state istate, ostate;
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct crypto_authenc_keys keys;
	struct crypto_aes_ctx aes;
	int err = -EINVAL;

	if (crypto_authenc_extractkeys(&keys, key, len) != 0)
		goto badkey;

	if (ctx->mode == CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD) {
		/* Minimum keysize is minimum AES key size + nonce size */
		if (keys.enckeylen < (AES_MIN_KEY_SIZE +
				      CTR_RFC3686_NONCE_SIZE))
			goto badkey;
		/* last 4 bytes of key are the nonce! */
		ctx->nonce = *(u32 *)(keys.enckey + keys.enckeylen -
				      CTR_RFC3686_NONCE_SIZE);
		/* exclude the nonce here */
		keys.enckeylen -= CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD;
	}

	/* Encryption key */
	switch (ctx->alg) {
	case SAFEXCEL_3DES:
		err = verify_aead_des3_key(ctfm, keys.enckey, keys.enckeylen);
		if (unlikely(err))
			goto badkey_expflags;
		break;
	case SAFEXCEL_AES:
		err = aes_expandkey(&aes, keys.enckey, keys.enckeylen);
		if (unlikely(err))
			goto badkey;
		break;
	default:
		dev_err(priv->dev, "aead: unsupported cipher algorithm\n");
		goto badkey;
	}

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
badkey_expflags:
	memzero_explicit(&keys, sizeof(keys));
	return err;
}

static int safexcel_context_control(struct safexcel_cipher_ctx *ctx,
				    struct crypto_async_request *async,
				    struct safexcel_cipher_req *sreq,
				    struct safexcel_command_desc *cdesc)
{
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ctrl_size = ctx->key_len / sizeof(u32);

	cdesc->control_data.control1 = ctx->mode;

	if (ctx->aead) {
		/* Take in account the ipad+opad digests */
		if (ctx->xcm) {
			ctrl_size += ctx->state_sz / sizeof(u32);
			cdesc->control_data.control0 =
				CONTEXT_CONTROL_KEY_EN |
				CONTEXT_CONTROL_DIGEST_XCM |
				ctx->hash_alg |
				CONTEXT_CONTROL_SIZE(ctrl_size);
		} else {
			ctrl_size += ctx->state_sz / sizeof(u32) * 2;
			cdesc->control_data.control0 =
				CONTEXT_CONTROL_KEY_EN |
				CONTEXT_CONTROL_DIGEST_HMAC |
				ctx->hash_alg |
				CONTEXT_CONTROL_SIZE(ctrl_size);
		}
		if (sreq->direction == SAFEXCEL_ENCRYPT)
			cdesc->control_data.control0 |=
				(ctx->xcm == EIP197_XCM_MODE_CCM) ?
					CONTEXT_CONTROL_TYPE_HASH_ENCRYPT_OUT :
					CONTEXT_CONTROL_TYPE_ENCRYPT_HASH_OUT;

		else
			cdesc->control_data.control0 |=
				(ctx->xcm == EIP197_XCM_MODE_CCM) ?
					CONTEXT_CONTROL_TYPE_DECRYPT_HASH_IN :
					CONTEXT_CONTROL_TYPE_HASH_DECRYPT_IN;
	} else {
		if (sreq->direction == SAFEXCEL_ENCRYPT)
			cdesc->control_data.control0 =
				CONTEXT_CONTROL_TYPE_CRYPTO_OUT |
				CONTEXT_CONTROL_KEY_EN |
				CONTEXT_CONTROL_SIZE(ctrl_size);
		else
			cdesc->control_data.control0 =
				CONTEXT_CONTROL_TYPE_CRYPTO_IN |
				CONTEXT_CONTROL_KEY_EN |
				CONTEXT_CONTROL_SIZE(ctrl_size);
	}

	if (ctx->alg == SAFEXCEL_DES) {
		cdesc->control_data.control0 |=
			CONTEXT_CONTROL_CRYPTO_ALG_DES;
	} else if (ctx->alg == SAFEXCEL_3DES) {
		cdesc->control_data.control0 |=
			CONTEXT_CONTROL_CRYPTO_ALG_3DES;
	} else if (ctx->alg == SAFEXCEL_AES) {
		switch (ctx->key_len >> ctx->xts) {
		case AES_KEYSIZE_128:
			cdesc->control_data.control0 |=
				CONTEXT_CONTROL_CRYPTO_ALG_AES128;
			break;
		case AES_KEYSIZE_192:
			cdesc->control_data.control0 |=
				CONTEXT_CONTROL_CRYPTO_ALG_AES192;
			break;
		case AES_KEYSIZE_256:
			cdesc->control_data.control0 |=
				CONTEXT_CONTROL_CRYPTO_ALG_AES256;
			break;
		default:
			dev_err(priv->dev, "aes keysize not supported: %u\n",
				ctx->key_len >> ctx->xts);
			return -EINVAL;
		}
	}

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
	struct skcipher_request *areq = skcipher_request_cast(async);
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(areq);
	struct safexcel_cipher_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct safexcel_result_desc *rdesc;
	int ndesc = 0;

	*ret = 0;

	if (unlikely(!sreq->rdescs))
		return 0;

	while (sreq->rdescs--) {
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
	}

	safexcel_complete(priv, ring);

	if (src == dst) {
		dma_unmap_sg(priv->dev, src, sreq->nr_src, DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(priv->dev, src, sreq->nr_src, DMA_TO_DEVICE);
		dma_unmap_sg(priv->dev, dst, sreq->nr_dst, DMA_FROM_DEVICE);
	}

	/*
	 * Update IV in req from last crypto output word for CBC modes
	 */
	if ((!ctx->aead) && (ctx->mode == CONTEXT_CONTROL_CRYPTO_MODE_CBC) &&
	    (sreq->direction == SAFEXCEL_ENCRYPT)) {
		/* For encrypt take the last output word */
		sg_pcopy_to_buffer(dst, sreq->nr_dst, areq->iv,
				   crypto_skcipher_ivsize(skcipher),
				   (cryptlen -
				    crypto_skcipher_ivsize(skcipher)));
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
	struct skcipher_request *areq = skcipher_request_cast(base);
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(areq);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(base->tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct safexcel_command_desc *cdesc;
	struct safexcel_command_desc *first_cdesc = NULL;
	struct safexcel_result_desc *rdesc, *first_rdesc = NULL;
	struct scatterlist *sg;
	unsigned int totlen;
	unsigned int totlen_src = cryptlen + assoclen;
	unsigned int totlen_dst = totlen_src;
	int n_cdesc = 0, n_rdesc = 0;
	int queued, i, ret = 0;
	bool first = true;

	sreq->nr_src = sg_nents_for_len(src, totlen_src);

	if (ctx->aead) {
		/*
		 * AEAD has auth tag appended to output for encrypt and
		 * removed from the output for decrypt!
		 */
		if (sreq->direction == SAFEXCEL_DECRYPT)
			totlen_dst -= digestsize;
		else
			totlen_dst += digestsize;

		memcpy(ctx->base.ctxr->data + ctx->key_len / sizeof(u32),
		       ctx->ipad, ctx->state_sz);
		if (!ctx->xcm)
			memcpy(ctx->base.ctxr->data + (ctx->key_len +
			       ctx->state_sz) / sizeof(u32), ctx->opad,
			       ctx->state_sz);
	} else if ((ctx->mode == CONTEXT_CONTROL_CRYPTO_MODE_CBC) &&
		   (sreq->direction == SAFEXCEL_DECRYPT)) {
		/*
		 * Save IV from last crypto input word for CBC modes in decrypt
		 * direction. Need to do this first in case of inplace operation
		 * as it will be overwritten.
		 */
		sg_pcopy_to_buffer(src, sreq->nr_src, areq->iv,
				   crypto_skcipher_ivsize(skcipher),
				   (totlen_src -
				    crypto_skcipher_ivsize(skcipher)));
	}

	sreq->nr_dst = sg_nents_for_len(dst, totlen_dst);

	/*
	 * Remember actual input length, source buffer length may be
	 * updated in case of inline operation below.
	 */
	totlen = totlen_src;
	queued = totlen_src;

	if (src == dst) {
		sreq->nr_src = max(sreq->nr_src, sreq->nr_dst);
		sreq->nr_dst = sreq->nr_src;
		if (unlikely((totlen_src || totlen_dst) &&
		    (sreq->nr_src <= 0))) {
			dev_err(priv->dev, "In-place buffer not large enough (need %d bytes)!",
				max(totlen_src, totlen_dst));
			return -EINVAL;
		}
		dma_map_sg(priv->dev, src, sreq->nr_src, DMA_BIDIRECTIONAL);
	} else {
		if (unlikely(totlen_src && (sreq->nr_src <= 0))) {
			dev_err(priv->dev, "Source buffer not large enough (need %d bytes)!",
				totlen_src);
			return -EINVAL;
		}
		dma_map_sg(priv->dev, src, sreq->nr_src, DMA_TO_DEVICE);

		if (unlikely(totlen_dst && (sreq->nr_dst <= 0))) {
			dev_err(priv->dev, "Dest buffer not large enough (need %d bytes)!",
				totlen_dst);
			dma_unmap_sg(priv->dev, src, sreq->nr_src,
				     DMA_TO_DEVICE);
			return -EINVAL;
		}
		dma_map_sg(priv->dev, dst, sreq->nr_dst, DMA_FROM_DEVICE);
	}

	memcpy(ctx->base.ctxr->data, ctx->key, ctx->key_len);

	/* The EIP cannot deal with zero length input packets! */
	if (totlen == 0)
		totlen = 1;

	/* command descriptors */
	for_each_sg(src, sg, sreq->nr_src, i) {
		int len = sg_dma_len(sg);

		/* Do not overflow the request */
		if (queued - len < 0)
			len = queued;

		cdesc = safexcel_add_cdesc(priv, ring, !n_cdesc,
					   !(queued - len),
					   sg_dma_address(sg), len, totlen,
					   ctx->base.ctxr_dma);
		if (IS_ERR(cdesc)) {
			/* No space left in the command descriptor ring */
			ret = PTR_ERR(cdesc);
			goto cdesc_rollback;
		}
		n_cdesc++;

		if (n_cdesc == 1) {
			first_cdesc = cdesc;
		}

		queued -= len;
		if (!queued)
			break;
	}

	if (unlikely(!n_cdesc)) {
		/*
		 * Special case: zero length input buffer.
		 * The engine always needs the 1st command descriptor, however!
		 */
		first_cdesc = safexcel_add_cdesc(priv, ring, 1, 1, 0, 0, totlen,
						 ctx->base.ctxr_dma);
		n_cdesc = 1;
	}

	/* Add context control words and token to first command descriptor */
	safexcel_context_control(ctx, base, sreq, first_cdesc);
	if (ctx->aead)
		safexcel_aead_token(ctx, iv, first_cdesc,
				    sreq->direction, cryptlen,
				    assoclen, digestsize);
	else
		safexcel_skcipher_token(ctx, iv, first_cdesc,
					cryptlen);

	/* result descriptors */
	for_each_sg(dst, sg, sreq->nr_dst, i) {
		bool last = (i == sreq->nr_dst - 1);
		u32 len = sg_dma_len(sg);

		/* only allow the part of the buffer we know we need */
		if (len > totlen_dst)
			len = totlen_dst;
		if (unlikely(!len))
			break;
		totlen_dst -= len;

		/* skip over AAD space in buffer - not written */
		if (assoclen) {
			if (assoclen >= len) {
				assoclen -= len;
				continue;
			}
			rdesc = safexcel_add_rdesc(priv, ring, first, last,
						   sg_dma_address(sg) +
						   assoclen,
						   len - assoclen);
			assoclen = 0;
		} else {
			rdesc = safexcel_add_rdesc(priv, ring, first, last,
						   sg_dma_address(sg),
						   len);
		}
		if (IS_ERR(rdesc)) {
			/* No space left in the result descriptor ring */
			ret = PTR_ERR(rdesc);
			goto rdesc_rollback;
		}
		if (first) {
			first_rdesc = rdesc;
			first = false;
		}
		n_rdesc++;
	}

	if (unlikely(first)) {
		/*
		 * Special case: AEAD decrypt with only AAD data.
		 * In this case there is NO output data from the engine,
		 * but the engine still needs a result descriptor!
		 * Create a dummy one just for catching the result token.
		 */
		rdesc = safexcel_add_rdesc(priv, ring, true, true, 0, 0);
		if (IS_ERR(rdesc)) {
			/* No space left in the result descriptor ring */
			ret = PTR_ERR(rdesc);
			goto rdesc_rollback;
		}
		first_rdesc = rdesc;
		n_rdesc = 1;
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
		dma_unmap_sg(priv->dev, src, sreq->nr_src, DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(priv->dev, src, sreq->nr_src, DMA_TO_DEVICE);
		dma_unmap_sg(priv->dev, dst, sreq->nr_dst, DMA_FROM_DEVICE);
	}

	return ret;
}

static int safexcel_handle_inv_result(struct safexcel_crypto_priv *priv,
				      int ring,
				      struct crypto_async_request *base,
				      struct safexcel_cipher_req *sreq,
				      bool *should_complete, int *ret)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(base->tfm);
	struct safexcel_result_desc *rdesc;
	int ndesc = 0, enq_ret;

	*ret = 0;

	if (unlikely(!sreq->rdescs))
		return 0;

	while (sreq->rdescs--) {
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
	}

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
		err = safexcel_handle_inv_result(priv, ring, async, sreq,
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
		err = safexcel_handle_inv_result(priv, ring, async, sreq,
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

	if (sreq->needs_inv) {
		ret = safexcel_cipher_send_inv(async, ring, commands, results);
	} else {
		struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
		u8 input_iv[AES_BLOCK_SIZE];

		/*
		 * Save input IV in case of CBC decrypt mode
		 * Will be overwritten with output IV prior to use!
		 */
		memcpy(input_iv, req->iv, crypto_skcipher_ivsize(skcipher));

		ret = safexcel_send_req(async, ring, sreq, req->src,
					req->dst, req->cryptlen, 0, 0, input_iv,
					commands, results);
	}

	sreq->rdescs = *results;
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
	sreq->rdescs = *results;
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
			enum safexcel_cipher_direction dir)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(base->tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	int ret, ring;

	sreq->needs_inv = false;
	sreq->direction = dir;

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

static int safexcel_encrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_ENCRYPT);
}

static int safexcel_decrypt(struct skcipher_request *req)
{
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
			SAFEXCEL_DECRYPT);
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

static int safexcel_skcipher_aes_ecb_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_AES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_ECB;
	return 0;
}

struct safexcel_alg_template safexcel_alg_ecb_aes = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_AES,
	.alg.skcipher = {
		.setkey = safexcel_skcipher_aes_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "safexcel-ecb-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_aes_ecb_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_skcipher_aes_cbc_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_AES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CBC;
	return 0;
}

struct safexcel_alg_template safexcel_alg_cbc_aes = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_AES,
	.alg.skcipher = {
		.setkey = safexcel_skcipher_aes_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "safexcel-cbc-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_aes_cbc_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_skcipher_aes_cfb_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_AES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CFB;
	return 0;
}

struct safexcel_alg_template safexcel_alg_cfb_aes = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_AES_XFB,
	.alg.skcipher = {
		.setkey = safexcel_skcipher_aes_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.base = {
			.cra_name = "cfb(aes)",
			.cra_driver_name = "safexcel-cfb-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_aes_cfb_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_skcipher_aes_ofb_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_AES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_OFB;
	return 0;
}

struct safexcel_alg_template safexcel_alg_ofb_aes = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_AES_XFB,
	.alg.skcipher = {
		.setkey = safexcel_skcipher_aes_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.base = {
			.cra_name = "ofb(aes)",
			.cra_driver_name = "safexcel-ofb-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_aes_ofb_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_skcipher_aesctr_setkey(struct crypto_skcipher *ctfm,
					   const u8 *key, unsigned int len)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct crypto_aes_ctx aes;
	int ret, i;
	unsigned int keylen;

	/* last 4 bytes of key are the nonce! */
	ctx->nonce = *(u32 *)(key + len - CTR_RFC3686_NONCE_SIZE);
	/* exclude the nonce here */
	keylen = len - CTR_RFC3686_NONCE_SIZE;
	ret = aes_expandkey(&aes, key, keylen);
	if (ret) {
		crypto_skcipher_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return ret;
	}

	if (priv->flags & EIP197_TRC_CACHE && ctx->base.ctxr_dma) {
		for (i = 0; i < keylen / sizeof(u32); i++) {
			if (ctx->key[i] != cpu_to_le32(aes.key_enc[i])) {
				ctx->base.needs_inv = true;
				break;
			}
		}
	}

	for (i = 0; i < keylen / sizeof(u32); i++)
		ctx->key[i] = cpu_to_le32(aes.key_enc[i]);

	ctx->key_len = keylen;

	memzero_explicit(&aes, sizeof(aes));
	return 0;
}

static int safexcel_skcipher_aes_ctr_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_AES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD;
	return 0;
}

struct safexcel_alg_template safexcel_alg_ctr_aes = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_AES,
	.alg.skcipher = {
		.setkey = safexcel_skcipher_aesctr_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		/* Add nonce size */
		.min_keysize = AES_MIN_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
		.ivsize = CTR_RFC3686_IV_SIZE,
		.base = {
			.cra_name = "rfc3686(ctr(aes))",
			.cra_driver_name = "safexcel-ctr-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_aes_ctr_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_des_setkey(struct crypto_skcipher *ctfm, const u8 *key,
			       unsigned int len)
{
	struct safexcel_cipher_ctx *ctx = crypto_skcipher_ctx(ctfm);
	int ret;

	ret = verify_skcipher_des_key(ctfm, key);
	if (ret)
		return ret;

	/* if context exits and key changed, need to invalidate it */
	if (ctx->base.ctxr_dma)
		if (memcmp(ctx->key, key, len))
			ctx->base.needs_inv = true;

	memcpy(ctx->key, key, len);
	ctx->key_len = len;

	return 0;
}

static int safexcel_skcipher_des_cbc_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_DES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CBC;
	return 0;
}

struct safexcel_alg_template safexcel_alg_cbc_des = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_DES,
	.alg.skcipher = {
		.setkey = safexcel_des_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		.min_keysize = DES_KEY_SIZE,
		.max_keysize = DES_KEY_SIZE,
		.ivsize = DES_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(des)",
			.cra_driver_name = "safexcel-cbc-des",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_des_cbc_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_skcipher_des_ecb_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_DES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_ECB;
	return 0;
}

struct safexcel_alg_template safexcel_alg_ecb_des = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_DES,
	.alg.skcipher = {
		.setkey = safexcel_des_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		.min_keysize = DES_KEY_SIZE,
		.max_keysize = DES_KEY_SIZE,
		.base = {
			.cra_name = "ecb(des)",
			.cra_driver_name = "safexcel-ecb-des",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_des_ecb_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_des3_ede_setkey(struct crypto_skcipher *ctfm,
				   const u8 *key, unsigned int len)
{
	struct safexcel_cipher_ctx *ctx = crypto_skcipher_ctx(ctfm);
	int err;

	err = verify_skcipher_des3_key(ctfm, key);
	if (err)
		return err;

	/* if context exits and key changed, need to invalidate it */
	if (ctx->base.ctxr_dma) {
		if (memcmp(ctx->key, key, len))
			ctx->base.needs_inv = true;
	}

	memcpy(ctx->key, key, len);

	ctx->key_len = len;

	return 0;
}

static int safexcel_skcipher_des3_cbc_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_3DES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CBC;
	return 0;
}

struct safexcel_alg_template safexcel_alg_cbc_des3_ede = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_DES,
	.alg.skcipher = {
		.setkey = safexcel_des3_ede_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		.min_keysize = DES3_EDE_KEY_SIZE,
		.max_keysize = DES3_EDE_KEY_SIZE,
		.ivsize = DES3_EDE_BLOCK_SIZE,
		.base = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "safexcel-cbc-des3_ede",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_des3_cbc_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_skcipher_des3_ecb_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_3DES;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_ECB;
	return 0;
}

struct safexcel_alg_template safexcel_alg_ecb_des3_ede = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_DES,
	.alg.skcipher = {
		.setkey = safexcel_des3_ede_setkey,
		.encrypt = safexcel_encrypt,
		.decrypt = safexcel_decrypt,
		.min_keysize = DES3_EDE_KEY_SIZE,
		.max_keysize = DES3_EDE_KEY_SIZE,
		.base = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "safexcel-ecb-des3_ede",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_des3_ecb_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_encrypt(struct aead_request *req)
{
	struct safexcel_cipher_req *creq = aead_request_ctx(req);

	return safexcel_queue_req(&req->base, creq, SAFEXCEL_ENCRYPT);
}

static int safexcel_aead_decrypt(struct aead_request *req)
{
	struct safexcel_cipher_req *creq = aead_request_ctx(req);

	return safexcel_queue_req(&req->base, creq, SAFEXCEL_DECRYPT);
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

	ctx->alg  = SAFEXCEL_AES; /* default */
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CBC; /* default */
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
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA1,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha1),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha1-cbc-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
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
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA2_256,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA256_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha256),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha256-cbc-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
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
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA2_256,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA224_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha224),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha224-cbc-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
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
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA2_512,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA512_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha512),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha512-cbc-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
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
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA2_512,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = SHA384_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha384),cbc(aes))",
			.cra_driver_name = "safexcel-authenc-hmac-sha384-cbc-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
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

static int safexcel_aead_sha1_des3_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_sha1_cra_init(tfm);
	ctx->alg = SAFEXCEL_3DES; /* override default */
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_cbc_des3_ede = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.algo_mask = SAFEXCEL_ALG_DES | SAFEXCEL_ALG_SHA1,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = DES3_EDE_BLOCK_SIZE,
		.maxauthsize = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha1),cbc(des3_ede))",
			.cra_driver_name = "safexcel-authenc-hmac-sha1-cbc-des3_ede",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha1_des3_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha1_ctr_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_sha1_cra_init(tfm);
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD; /* override default */
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_ctr_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA1,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = CTR_RFC3686_IV_SIZE,
		.maxauthsize = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha1),rfc3686(ctr(aes)))",
			.cra_driver_name = "safexcel-authenc-hmac-sha1-ctr-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha1_ctr_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha256_ctr_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_sha256_cra_init(tfm);
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD; /* override default */
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha256_ctr_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA2_256,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = CTR_RFC3686_IV_SIZE,
		.maxauthsize = SHA256_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha256),rfc3686(ctr(aes)))",
			.cra_driver_name = "safexcel-authenc-hmac-sha256-ctr-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha256_ctr_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha224_ctr_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_sha224_cra_init(tfm);
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD; /* override default */
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha224_ctr_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA2_256,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = CTR_RFC3686_IV_SIZE,
		.maxauthsize = SHA224_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha224),rfc3686(ctr(aes)))",
			.cra_driver_name = "safexcel-authenc-hmac-sha224-ctr-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha224_ctr_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha512_ctr_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_sha512_cra_init(tfm);
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD; /* override default */
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha512_ctr_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA2_512,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = CTR_RFC3686_IV_SIZE,
		.maxauthsize = SHA512_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha512),rfc3686(ctr(aes)))",
			.cra_driver_name = "safexcel-authenc-hmac-sha512-ctr-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha512_ctr_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_sha384_ctr_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_sha384_cra_init(tfm);
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD; /* override default */
	return 0;
}

struct safexcel_alg_template safexcel_alg_authenc_hmac_sha384_ctr_aes = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_SHA2_512,
	.alg.aead = {
		.setkey = safexcel_aead_setkey,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = CTR_RFC3686_IV_SIZE,
		.maxauthsize = SHA384_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha384),rfc3686(ctr(aes)))",
			.cra_driver_name = "safexcel-authenc-hmac-sha384-ctr-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_sha384_ctr_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_skcipher_aesxts_setkey(struct crypto_skcipher *ctfm,
					   const u8 *key, unsigned int len)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct crypto_aes_ctx aes;
	int ret, i;
	unsigned int keylen;

	/* Check for illegal XTS keys */
	ret = xts_verify_key(ctfm, key, len);
	if (ret)
		return ret;

	/* Only half of the key data is cipher key */
	keylen = (len >> 1);
	ret = aes_expandkey(&aes, key, keylen);
	if (ret) {
		crypto_skcipher_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return ret;
	}

	if (priv->flags & EIP197_TRC_CACHE && ctx->base.ctxr_dma) {
		for (i = 0; i < keylen / sizeof(u32); i++) {
			if (ctx->key[i] != cpu_to_le32(aes.key_enc[i])) {
				ctx->base.needs_inv = true;
				break;
			}
		}
	}

	for (i = 0; i < keylen / sizeof(u32); i++)
		ctx->key[i] = cpu_to_le32(aes.key_enc[i]);

	/* The other half is the tweak key */
	ret = aes_expandkey(&aes, (u8 *)(key + keylen), keylen);
	if (ret) {
		crypto_skcipher_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return ret;
	}

	if (priv->flags & EIP197_TRC_CACHE && ctx->base.ctxr_dma) {
		for (i = 0; i < keylen / sizeof(u32); i++) {
			if (ctx->key[i + keylen / sizeof(u32)] !=
			    cpu_to_le32(aes.key_enc[i])) {
				ctx->base.needs_inv = true;
				break;
			}
		}
	}

	for (i = 0; i < keylen / sizeof(u32); i++)
		ctx->key[i + keylen / sizeof(u32)] =
			cpu_to_le32(aes.key_enc[i]);

	ctx->key_len = keylen << 1;

	memzero_explicit(&aes, sizeof(aes));
	return 0;
}

static int safexcel_skcipher_aes_xts_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_skcipher_cra_init(tfm);
	ctx->alg  = SAFEXCEL_AES;
	ctx->xts  = 1;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_XTS;
	return 0;
}

static int safexcel_encrypt_xts(struct skcipher_request *req)
{
	if (req->cryptlen < XTS_BLOCK_SIZE)
		return -EINVAL;
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
				  SAFEXCEL_ENCRYPT);
}

static int safexcel_decrypt_xts(struct skcipher_request *req)
{
	if (req->cryptlen < XTS_BLOCK_SIZE)
		return -EINVAL;
	return safexcel_queue_req(&req->base, skcipher_request_ctx(req),
				  SAFEXCEL_DECRYPT);
}

struct safexcel_alg_template safexcel_alg_xts_aes = {
	.type = SAFEXCEL_ALG_TYPE_SKCIPHER,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_AES_XTS,
	.alg.skcipher = {
		.setkey = safexcel_skcipher_aesxts_setkey,
		.encrypt = safexcel_encrypt_xts,
		.decrypt = safexcel_decrypt_xts,
		/* XTS actually uses 2 AES keys glued together */
		.min_keysize = AES_MIN_KEY_SIZE * 2,
		.max_keysize = AES_MAX_KEY_SIZE * 2,
		.ivsize = XTS_BLOCK_SIZE,
		.base = {
			.cra_name = "xts(aes)",
			.cra_driver_name = "safexcel-xts-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = XTS_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_skcipher_aes_xts_cra_init,
			.cra_exit = safexcel_skcipher_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_gcm_setkey(struct crypto_aead *ctfm, const u8 *key,
				    unsigned int len)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct crypto_aes_ctx aes;
	u32 hashkey[AES_BLOCK_SIZE >> 2];
	int ret, i;

	ret = aes_expandkey(&aes, key, len);
	if (ret) {
		crypto_aead_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		memzero_explicit(&aes, sizeof(aes));
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

	/* Compute hash key by encrypting zeroes with cipher key */
	crypto_cipher_clear_flags(ctx->hkaes, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(ctx->hkaes, crypto_aead_get_flags(ctfm) &
				CRYPTO_TFM_REQ_MASK);
	ret = crypto_cipher_setkey(ctx->hkaes, key, len);
	crypto_aead_set_flags(ctfm, crypto_cipher_get_flags(ctx->hkaes) &
			      CRYPTO_TFM_RES_MASK);
	if (ret)
		return ret;

	memset(hashkey, 0, AES_BLOCK_SIZE);
	crypto_cipher_encrypt_one(ctx->hkaes, (u8 *)hashkey, (u8 *)hashkey);

	if (priv->flags & EIP197_TRC_CACHE && ctx->base.ctxr_dma) {
		for (i = 0; i < AES_BLOCK_SIZE / sizeof(u32); i++) {
			if (ctx->ipad[i] != cpu_to_be32(hashkey[i])) {
				ctx->base.needs_inv = true;
				break;
			}
		}
	}

	for (i = 0; i < AES_BLOCK_SIZE / sizeof(u32); i++)
		ctx->ipad[i] = cpu_to_be32(hashkey[i]);

	memzero_explicit(hashkey, AES_BLOCK_SIZE);
	memzero_explicit(&aes, sizeof(aes));
	return 0;
}

static int safexcel_aead_gcm_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_cra_init(tfm);
	ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_GHASH;
	ctx->state_sz = GHASH_BLOCK_SIZE;
	ctx->xcm = EIP197_XCM_MODE_GCM;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_XCM; /* override default */

	ctx->hkaes = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(ctx->hkaes))
		return PTR_ERR(ctx->hkaes);

	return 0;
}

static void safexcel_aead_gcm_cra_exit(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_cipher(ctx->hkaes);
	safexcel_aead_cra_exit(tfm);
}

static int safexcel_aead_gcm_setauthsize(struct crypto_aead *tfm,
					 unsigned int authsize)
{
	return crypto_gcm_check_authsize(authsize);
}

struct safexcel_alg_template safexcel_alg_gcm = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_GHASH,
	.alg.aead = {
		.setkey = safexcel_aead_gcm_setkey,
		.setauthsize = safexcel_aead_gcm_setauthsize,
		.encrypt = safexcel_aead_encrypt,
		.decrypt = safexcel_aead_decrypt,
		.ivsize = GCM_AES_IV_SIZE,
		.maxauthsize = GHASH_DIGEST_SIZE,
		.base = {
			.cra_name = "gcm(aes)",
			.cra_driver_name = "safexcel-gcm-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_gcm_cra_init,
			.cra_exit = safexcel_aead_gcm_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

static int safexcel_aead_ccm_setkey(struct crypto_aead *ctfm, const u8 *key,
				    unsigned int len)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(ctfm);
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct safexcel_crypto_priv *priv = ctx->priv;
	struct crypto_aes_ctx aes;
	int ret, i;

	ret = aes_expandkey(&aes, key, len);
	if (ret) {
		crypto_aead_set_flags(ctfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		memzero_explicit(&aes, sizeof(aes));
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

	for (i = 0; i < len / sizeof(u32); i++) {
		ctx->key[i] = cpu_to_le32(aes.key_enc[i]);
		ctx->ipad[i + 2 * AES_BLOCK_SIZE / sizeof(u32)] =
			cpu_to_be32(aes.key_enc[i]);
	}

	ctx->key_len = len;
	ctx->state_sz = 2 * AES_BLOCK_SIZE + len;

	if (len == AES_KEYSIZE_192)
		ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_XCBC192;
	else if (len == AES_KEYSIZE_256)
		ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_XCBC256;
	else
		ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_XCBC128;

	memzero_explicit(&aes, sizeof(aes));
	return 0;
}

static int safexcel_aead_ccm_cra_init(struct crypto_tfm *tfm)
{
	struct safexcel_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	safexcel_aead_cra_init(tfm);
	ctx->hash_alg = CONTEXT_CONTROL_CRYPTO_ALG_XCBC128;
	ctx->state_sz = 3 * AES_BLOCK_SIZE;
	ctx->xcm = EIP197_XCM_MODE_CCM;
	ctx->mode = CONTEXT_CONTROL_CRYPTO_MODE_XCM; /* override default */
	return 0;
}

static int safexcel_aead_ccm_setauthsize(struct crypto_aead *tfm,
					 unsigned int authsize)
{
	/* Borrowed from crypto/ccm.c */
	switch (authsize) {
	case 4:
	case 6:
	case 8:
	case 10:
	case 12:
	case 14:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int safexcel_ccm_encrypt(struct aead_request *req)
{
	struct safexcel_cipher_req *creq = aead_request_ctx(req);

	if (req->iv[0] < 1 || req->iv[0] > 7)
		return -EINVAL;

	return safexcel_queue_req(&req->base, creq, SAFEXCEL_ENCRYPT);
}

static int safexcel_ccm_decrypt(struct aead_request *req)
{
	struct safexcel_cipher_req *creq = aead_request_ctx(req);

	if (req->iv[0] < 1 || req->iv[0] > 7)
		return -EINVAL;

	return safexcel_queue_req(&req->base, creq, SAFEXCEL_DECRYPT);
}

struct safexcel_alg_template safexcel_alg_ccm = {
	.type = SAFEXCEL_ALG_TYPE_AEAD,
	.algo_mask = SAFEXCEL_ALG_AES | SAFEXCEL_ALG_CBC_MAC_ALL,
	.alg.aead = {
		.setkey = safexcel_aead_ccm_setkey,
		.setauthsize = safexcel_aead_ccm_setauthsize,
		.encrypt = safexcel_ccm_encrypt,
		.decrypt = safexcel_ccm_decrypt,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
		.base = {
			.cra_name = "ccm(aes)",
			.cra_driver_name = "safexcel-ccm-aes",
			.cra_priority = SAFEXCEL_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct safexcel_cipher_ctx),
			.cra_alignmask = 0,
			.cra_init = safexcel_aead_ccm_cra_init,
			.cra_exit = safexcel_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};
