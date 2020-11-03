// SPDX-License-Identifier: GPL-2.0
/*
 * Adiantum length-preserving encryption mode
 *
 * Copyright 2018 Google LLC
 */

/*
 * Adiantum is a tweakable, length-preserving encryption mode designed for fast
 * and secure disk encryption, especially on CPUs without dedicated crypto
 * instructions.  Adiantum encrypts each sector using the XChaCha12 stream
 * cipher, two passes of an ε-almost-∆-universal (ε-∆U) hash function based on
 * NH and Poly1305, and an invocation of the AES-256 block cipher on a single
 * 16-byte block.  See the paper for details:
 *
 *	Adiantum: length-preserving encryption for entry-level processors
 *      (https://eprint.iacr.org/2018/720.pdf)
 *
 * For flexibility, this implementation also allows other ciphers:
 *
 *	- Stream cipher: XChaCha12 or XChaCha20
 *	- Block cipher: any with a 128-bit block size and 256-bit key
 *
 * This implementation doesn't currently allow other ε-∆U hash functions, i.e.
 * HPolyC is not supported.  This is because Adiantum is ~20% faster than HPolyC
 * but still provably as secure, and also the ε-∆U hash function of HBSH is
 * formally defined to take two inputs (tweak, message) which makes it difficult
 * to wrap with the crypto_shash API.  Rather, some details need to be handled
 * here.  Nevertheless, if needed in the future, support for other ε-∆U hash
 * functions could be added here.
 */

#include <crypto/b128ops.h>
#include <crypto/chacha.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/poly1305.h>
#include <crypto/internal/skcipher.h>
#include <crypto/nhpoly1305.h>
#include <crypto/scatterwalk.h>
#include <linux/module.h>

#include "internal.h"

/*
 * Size of right-hand part of input data, in bytes; also the size of the block
 * cipher's block size and the hash function's output.
 */
#define BLOCKCIPHER_BLOCK_SIZE		16

/* Size of the block cipher key (K_E) in bytes */
#define BLOCKCIPHER_KEY_SIZE		32

/* Size of the hash key (K_H) in bytes */
#define HASH_KEY_SIZE		(POLY1305_BLOCK_SIZE + NHPOLY1305_KEY_SIZE)

/*
 * The specification allows variable-length tweaks, but Linux's crypto API
 * currently only allows algorithms to support a single length.  The "natural"
 * tweak length for Adiantum is 16, since that fits into one Poly1305 block for
 * the best performance.  But longer tweaks are useful for fscrypt, to avoid
 * needing to derive per-file keys.  So instead we use two blocks, or 32 bytes.
 */
#define TWEAK_SIZE		32

struct adiantum_instance_ctx {
	struct crypto_skcipher_spawn streamcipher_spawn;
	struct crypto_spawn blockcipher_spawn;
	struct crypto_shash_spawn hash_spawn;
};

struct adiantum_tfm_ctx {
	struct crypto_skcipher *streamcipher;
	struct crypto_cipher *blockcipher;
	struct crypto_shash *hash;
	struct poly1305_core_key header_hash_key;
};

struct adiantum_request_ctx {

	/*
	 * Buffer for right-hand part of data, i.e.
	 *
	 *    P_L => P_M => C_M => C_R when encrypting, or
	 *    C_R => C_M => P_M => P_L when decrypting.
	 *
	 * Also used to build the IV for the stream cipher.
	 */
	union {
		u8 bytes[XCHACHA_IV_SIZE];
		__le32 words[XCHACHA_IV_SIZE / sizeof(__le32)];
		le128 bignum;	/* interpret as element of Z/(2^{128}Z) */
	} rbuf;

	bool enc; /* true if encrypting, false if decrypting */

	/*
	 * The result of the Poly1305 ε-∆U hash function applied to
	 * (bulk length, tweak)
	 */
	le128 header_hash;

	/* Sub-requests, must be last */
	union {
		struct shash_desc hash_desc;
		struct skcipher_request streamcipher_req;
	} u;
};

/*
 * Given the XChaCha stream key K_S, derive the block cipher key K_E and the
 * hash key K_H as follows:
 *
 *     K_E || K_H || ... = XChaCha(key=K_S, nonce=1||0^191)
 *
 * Note that this denotes using bits from the XChaCha keystream, which here we
 * get indirectly by encrypting a buffer containing all 0's.
 */
static int adiantum_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct {
		u8 iv[XCHACHA_IV_SIZE];
		u8 derived_keys[BLOCKCIPHER_KEY_SIZE + HASH_KEY_SIZE];
		struct scatterlist sg;
		struct crypto_wait wait;
		struct skcipher_request req; /* must be last */
	} *data;
	u8 *keyp;
	int err;

	/* Set the stream cipher key (K_S) */
	crypto_skcipher_clear_flags(tctx->streamcipher, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(tctx->streamcipher,
				  crypto_skcipher_get_flags(tfm) &
				  CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(tctx->streamcipher, key, keylen);
	crypto_skcipher_set_flags(tfm,
				crypto_skcipher_get_flags(tctx->streamcipher) &
				CRYPTO_TFM_RES_MASK);
	if (err)
		return err;

	/* Derive the subkeys */
	data = kzalloc(sizeof(*data) +
		       crypto_skcipher_reqsize(tctx->streamcipher), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->iv[0] = 1;
	sg_init_one(&data->sg, data->derived_keys, sizeof(data->derived_keys));
	crypto_init_wait(&data->wait);
	skcipher_request_set_tfm(&data->req, tctx->streamcipher);
	skcipher_request_set_callback(&data->req, CRYPTO_TFM_REQ_MAY_SLEEP |
						  CRYPTO_TFM_REQ_MAY_BACKLOG,
				      crypto_req_done, &data->wait);
	skcipher_request_set_crypt(&data->req, &data->sg, &data->sg,
				   sizeof(data->derived_keys), data->iv);
	err = crypto_wait_req(crypto_skcipher_encrypt(&data->req), &data->wait);
	if (err)
		goto out;
	keyp = data->derived_keys;

	/* Set the block cipher key (K_E) */
	crypto_cipher_clear_flags(tctx->blockcipher, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(tctx->blockcipher,
				crypto_skcipher_get_flags(tfm) &
				CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(tctx->blockcipher, keyp,
				   BLOCKCIPHER_KEY_SIZE);
	crypto_skcipher_set_flags(tfm,
				  crypto_cipher_get_flags(tctx->blockcipher) &
				  CRYPTO_TFM_RES_MASK);
	if (err)
		goto out;
	keyp += BLOCKCIPHER_KEY_SIZE;

	/* Set the hash key (K_H) */
	poly1305_core_setkey(&tctx->header_hash_key, keyp);
	keyp += POLY1305_BLOCK_SIZE;

	crypto_shash_clear_flags(tctx->hash, CRYPTO_TFM_REQ_MASK);
	crypto_shash_set_flags(tctx->hash, crypto_skcipher_get_flags(tfm) &
					   CRYPTO_TFM_REQ_MASK);
	err = crypto_shash_setkey(tctx->hash, keyp, NHPOLY1305_KEY_SIZE);
	crypto_skcipher_set_flags(tfm, crypto_shash_get_flags(tctx->hash) &
				       CRYPTO_TFM_RES_MASK);
	keyp += NHPOLY1305_KEY_SIZE;
	WARN_ON(keyp != &data->derived_keys[ARRAY_SIZE(data->derived_keys)]);
out:
	kzfree(data);
	return err;
}

/* Addition in Z/(2^{128}Z) */
static inline void le128_add(le128 *r, const le128 *v1, const le128 *v2)
{
	u64 x = le64_to_cpu(v1->b);
	u64 y = le64_to_cpu(v2->b);

	r->b = cpu_to_le64(x + y);
	r->a = cpu_to_le64(le64_to_cpu(v1->a) + le64_to_cpu(v2->a) +
			   (x + y < x));
}

/* Subtraction in Z/(2^{128}Z) */
static inline void le128_sub(le128 *r, const le128 *v1, const le128 *v2)
{
	u64 x = le64_to_cpu(v1->b);
	u64 y = le64_to_cpu(v2->b);

	r->b = cpu_to_le64(x - y);
	r->a = cpu_to_le64(le64_to_cpu(v1->a) - le64_to_cpu(v2->a) -
			   (x - y > x));
}

/*
 * Apply the Poly1305 ε-∆U hash function to (bulk length, tweak) and save the
 * result to rctx->header_hash.  This is the calculation
 *
 *	H_T ← Poly1305_{K_T}(bin_{128}(|L|) || T)
 *
 * from the procedure in section 6.4 of the Adiantum paper.  The resulting value
 * is reused in both the first and second hash steps.  Specifically, it's added
 * to the result of an independently keyed ε-∆U hash function (for equal length
 * inputs only) taken over the left-hand part (the "bulk") of the message, to
 * give the overall Adiantum hash of the (tweak, left-hand part) pair.
 */
static void adiantum_hash_header(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct adiantum_request_ctx *rctx = skcipher_request_ctx(req);
	const unsigned int bulk_len = req->cryptlen - BLOCKCIPHER_BLOCK_SIZE;
	struct {
		__le64 message_bits;
		__le64 padding;
	} header = {
		.message_bits = cpu_to_le64((u64)bulk_len * 8)
	};
	struct poly1305_state state;

	poly1305_core_init(&state);

	BUILD_BUG_ON(sizeof(header) % POLY1305_BLOCK_SIZE != 0);
	poly1305_core_blocks(&state, &tctx->header_hash_key,
			     &header, sizeof(header) / POLY1305_BLOCK_SIZE, 1);

	BUILD_BUG_ON(TWEAK_SIZE % POLY1305_BLOCK_SIZE != 0);
	poly1305_core_blocks(&state, &tctx->header_hash_key, req->iv,
			     TWEAK_SIZE / POLY1305_BLOCK_SIZE, 1);

	poly1305_core_emit(&state, NULL, &rctx->header_hash);
}

/* Hash the left-hand part (the "bulk") of the message using NHPoly1305 */
static int adiantum_hash_message(struct skcipher_request *req,
				 struct scatterlist *sgl, le128 *digest)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct adiantum_request_ctx *rctx = skcipher_request_ctx(req);
	const unsigned int bulk_len = req->cryptlen - BLOCKCIPHER_BLOCK_SIZE;
	struct shash_desc *hash_desc = &rctx->u.hash_desc;
	struct sg_mapping_iter miter;
	unsigned int i, n;
	int err;

	hash_desc->tfm = tctx->hash;
	hash_desc->flags = 0;

	err = crypto_shash_init(hash_desc);
	if (err)
		return err;

	sg_miter_start(&miter, sgl, sg_nents(sgl),
		       SG_MITER_FROM_SG | SG_MITER_ATOMIC);
	for (i = 0; i < bulk_len; i += n) {
		sg_miter_next(&miter);
		n = min_t(unsigned int, miter.length, bulk_len - i);
		err = crypto_shash_update(hash_desc, miter.addr, n);
		if (err)
			break;
	}
	sg_miter_stop(&miter);
	if (err)
		return err;

	return crypto_shash_final(hash_desc, (u8 *)digest);
}

/* Continue Adiantum encryption/decryption after the stream cipher step */
static int adiantum_finish(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct adiantum_request_ctx *rctx = skcipher_request_ctx(req);
	const unsigned int bulk_len = req->cryptlen - BLOCKCIPHER_BLOCK_SIZE;
	le128 digest;
	int err;

	/* If decrypting, decrypt C_M with the block cipher to get P_M */
	if (!rctx->enc)
		crypto_cipher_decrypt_one(tctx->blockcipher, rctx->rbuf.bytes,
					  rctx->rbuf.bytes);

	/*
	 * Second hash step
	 *	enc: C_R = C_M - H_{K_H}(T, C_L)
	 *	dec: P_R = P_M - H_{K_H}(T, P_L)
	 */
	err = adiantum_hash_message(req, req->dst, &digest);
	if (err)
		return err;
	le128_add(&digest, &digest, &rctx->header_hash);
	le128_sub(&rctx->rbuf.bignum, &rctx->rbuf.bignum, &digest);
	scatterwalk_map_and_copy(&rctx->rbuf.bignum, req->dst,
				 bulk_len, BLOCKCIPHER_BLOCK_SIZE, 1);
	return 0;
}

static void adiantum_streamcipher_done(struct crypto_async_request *areq,
				       int err)
{
	struct skcipher_request *req = areq->data;

	if (!err)
		err = adiantum_finish(req);

	skcipher_request_complete(req, err);
}

static int adiantum_crypt(struct skcipher_request *req, bool enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct adiantum_request_ctx *rctx = skcipher_request_ctx(req);
	const unsigned int bulk_len = req->cryptlen - BLOCKCIPHER_BLOCK_SIZE;
	unsigned int stream_len;
	le128 digest;
	int err;

	if (req->cryptlen < BLOCKCIPHER_BLOCK_SIZE)
		return -EINVAL;

	rctx->enc = enc;

	/*
	 * First hash step
	 *	enc: P_M = P_R + H_{K_H}(T, P_L)
	 *	dec: C_M = C_R + H_{K_H}(T, C_L)
	 */
	adiantum_hash_header(req);
	err = adiantum_hash_message(req, req->src, &digest);
	if (err)
		return err;
	le128_add(&digest, &digest, &rctx->header_hash);
	scatterwalk_map_and_copy(&rctx->rbuf.bignum, req->src,
				 bulk_len, BLOCKCIPHER_BLOCK_SIZE, 0);
	le128_add(&rctx->rbuf.bignum, &rctx->rbuf.bignum, &digest);

	/* If encrypting, encrypt P_M with the block cipher to get C_M */
	if (enc)
		crypto_cipher_encrypt_one(tctx->blockcipher, rctx->rbuf.bytes,
					  rctx->rbuf.bytes);

	/* Initialize the rest of the XChaCha IV (first part is C_M) */
	BUILD_BUG_ON(BLOCKCIPHER_BLOCK_SIZE != 16);
	BUILD_BUG_ON(XCHACHA_IV_SIZE != 32);	/* nonce || stream position */
	rctx->rbuf.words[4] = cpu_to_le32(1);
	rctx->rbuf.words[5] = 0;
	rctx->rbuf.words[6] = 0;
	rctx->rbuf.words[7] = 0;

	/*
	 * XChaCha needs to be done on all the data except the last 16 bytes;
	 * for disk encryption that usually means 4080 or 496 bytes.  But ChaCha
	 * implementations tend to be most efficient when passed a whole number
	 * of 64-byte ChaCha blocks, or sometimes even a multiple of 256 bytes.
	 * And here it doesn't matter whether the last 16 bytes are written to,
	 * as the second hash step will overwrite them.  Thus, round the XChaCha
	 * length up to the next 64-byte boundary if possible.
	 */
	stream_len = bulk_len;
	if (round_up(stream_len, CHACHA_BLOCK_SIZE) <= req->cryptlen)
		stream_len = round_up(stream_len, CHACHA_BLOCK_SIZE);

	skcipher_request_set_tfm(&rctx->u.streamcipher_req, tctx->streamcipher);
	skcipher_request_set_crypt(&rctx->u.streamcipher_req, req->src,
				   req->dst, stream_len, &rctx->rbuf);
	skcipher_request_set_callback(&rctx->u.streamcipher_req,
				      req->base.flags,
				      adiantum_streamcipher_done, req);
	return crypto_skcipher_encrypt(&rctx->u.streamcipher_req) ?:
		adiantum_finish(req);
}

static int adiantum_encrypt(struct skcipher_request *req)
{
	return adiantum_crypt(req, true);
}

static int adiantum_decrypt(struct skcipher_request *req)
{
	return adiantum_crypt(req, false);
}

static int adiantum_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct adiantum_instance_ctx *ictx = skcipher_instance_ctx(inst);
	struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *streamcipher;
	struct crypto_cipher *blockcipher;
	struct crypto_shash *hash;
	unsigned int subreq_size;
	int err;

	streamcipher = crypto_spawn_skcipher(&ictx->streamcipher_spawn);
	if (IS_ERR(streamcipher))
		return PTR_ERR(streamcipher);

	blockcipher = crypto_spawn_cipher(&ictx->blockcipher_spawn);
	if (IS_ERR(blockcipher)) {
		err = PTR_ERR(blockcipher);
		goto err_free_streamcipher;
	}

	hash = crypto_spawn_shash(&ictx->hash_spawn);
	if (IS_ERR(hash)) {
		err = PTR_ERR(hash);
		goto err_free_blockcipher;
	}

	tctx->streamcipher = streamcipher;
	tctx->blockcipher = blockcipher;
	tctx->hash = hash;

	BUILD_BUG_ON(offsetofend(struct adiantum_request_ctx, u) !=
		     sizeof(struct adiantum_request_ctx));
	subreq_size = max(FIELD_SIZEOF(struct adiantum_request_ctx,
				       u.hash_desc) +
			  crypto_shash_descsize(hash),
			  FIELD_SIZEOF(struct adiantum_request_ctx,
				       u.streamcipher_req) +
			  crypto_skcipher_reqsize(streamcipher));

	crypto_skcipher_set_reqsize(tfm,
				    offsetof(struct adiantum_request_ctx, u) +
				    subreq_size);
	return 0;

err_free_blockcipher:
	crypto_free_cipher(blockcipher);
err_free_streamcipher:
	crypto_free_skcipher(streamcipher);
	return err;
}

static void adiantum_exit_tfm(struct crypto_skcipher *tfm)
{
	struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(tctx->streamcipher);
	crypto_free_cipher(tctx->blockcipher);
	crypto_free_shash(tctx->hash);
}

static void adiantum_free_instance(struct skcipher_instance *inst)
{
	struct adiantum_instance_ctx *ictx = skcipher_instance_ctx(inst);

	crypto_drop_skcipher(&ictx->streamcipher_spawn);
	crypto_drop_spawn(&ictx->blockcipher_spawn);
	crypto_drop_shash(&ictx->hash_spawn);
	kfree(inst);
}

/*
 * Check for a supported set of inner algorithms.
 * See the comment at the beginning of this file.
 */
static bool adiantum_supported_algorithms(struct skcipher_alg *streamcipher_alg,
					  struct crypto_alg *blockcipher_alg,
					  struct shash_alg *hash_alg)
{
	if (strcmp(streamcipher_alg->base.cra_name, "xchacha12") != 0 &&
	    strcmp(streamcipher_alg->base.cra_name, "xchacha20") != 0)
		return false;

	if (blockcipher_alg->cra_cipher.cia_min_keysize > BLOCKCIPHER_KEY_SIZE ||
	    blockcipher_alg->cra_cipher.cia_max_keysize < BLOCKCIPHER_KEY_SIZE)
		return false;
	if (blockcipher_alg->cra_blocksize != BLOCKCIPHER_BLOCK_SIZE)
		return false;

	if (strcmp(hash_alg->base.cra_name, "nhpoly1305") != 0)
		return false;

	return true;
}

static int adiantum_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	const char *streamcipher_name;
	const char *blockcipher_name;
	const char *nhpoly1305_name;
	struct skcipher_instance *inst;
	struct adiantum_instance_ctx *ictx;
	struct skcipher_alg *streamcipher_alg;
	struct crypto_alg *blockcipher_alg;
	struct crypto_alg *_hash_alg;
	struct shash_alg *hash_alg;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_SKCIPHER) & algt->mask)
		return -EINVAL;

	streamcipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(streamcipher_name))
		return PTR_ERR(streamcipher_name);

	blockcipher_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(blockcipher_name))
		return PTR_ERR(blockcipher_name);

	nhpoly1305_name = crypto_attr_alg_name(tb[3]);
	if (nhpoly1305_name == ERR_PTR(-ENOENT))
		nhpoly1305_name = "nhpoly1305";
	if (IS_ERR(nhpoly1305_name))
		return PTR_ERR(nhpoly1305_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*ictx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	ictx = skcipher_instance_ctx(inst);

	/* Stream cipher, e.g. "xchacha12" */
	crypto_set_skcipher_spawn(&ictx->streamcipher_spawn,
				  skcipher_crypto_instance(inst));
	err = crypto_grab_skcipher(&ictx->streamcipher_spawn, streamcipher_name,
				   0, crypto_requires_sync(algt->type,
							   algt->mask));
	if (err)
		goto out_free_inst;
	streamcipher_alg = crypto_spawn_skcipher_alg(&ictx->streamcipher_spawn);

	/* Block cipher, e.g. "aes" */
	crypto_set_spawn(&ictx->blockcipher_spawn,
			 skcipher_crypto_instance(inst));
	err = crypto_grab_spawn(&ictx->blockcipher_spawn, blockcipher_name,
				CRYPTO_ALG_TYPE_CIPHER, CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto out_drop_streamcipher;
	blockcipher_alg = ictx->blockcipher_spawn.alg;

	/* NHPoly1305 ε-∆U hash function */
	_hash_alg = crypto_alg_mod_lookup(nhpoly1305_name,
					  CRYPTO_ALG_TYPE_SHASH,
					  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(_hash_alg)) {
		err = PTR_ERR(_hash_alg);
		goto out_drop_blockcipher;
	}
	hash_alg = __crypto_shash_alg(_hash_alg);
	err = crypto_init_shash_spawn(&ictx->hash_spawn, hash_alg,
				      skcipher_crypto_instance(inst));
	if (err)
		goto out_put_hash;

	/* Check the set of algorithms */
	if (!adiantum_supported_algorithms(streamcipher_alg, blockcipher_alg,
					   hash_alg)) {
		pr_warn("Unsupported Adiantum instantiation: (%s,%s,%s)\n",
			streamcipher_alg->base.cra_name,
			blockcipher_alg->cra_name, hash_alg->base.cra_name);
		err = -EINVAL;
		goto out_drop_hash;
	}

	/* Instance fields */

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "adiantum(%s,%s)", streamcipher_alg->base.cra_name,
		     blockcipher_alg->cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_drop_hash;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "adiantum(%s,%s,%s)",
		     streamcipher_alg->base.cra_driver_name,
		     blockcipher_alg->cra_driver_name,
		     hash_alg->base.cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_drop_hash;

	inst->alg.base.cra_flags = streamcipher_alg->base.cra_flags &
				   CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_blocksize = BLOCKCIPHER_BLOCK_SIZE;
	inst->alg.base.cra_ctxsize = sizeof(struct adiantum_tfm_ctx);
	inst->alg.base.cra_alignmask = streamcipher_alg->base.cra_alignmask |
				       hash_alg->base.cra_alignmask;
	/*
	 * The block cipher is only invoked once per message, so for long
	 * messages (e.g. sectors for disk encryption) its performance doesn't
	 * matter as much as that of the stream cipher and hash function.  Thus,
	 * weigh the block cipher's ->cra_priority less.
	 */
	inst->alg.base.cra_priority = (4 * streamcipher_alg->base.cra_priority +
				       2 * hash_alg->base.cra_priority +
				       blockcipher_alg->cra_priority) / 7;

	inst->alg.setkey = adiantum_setkey;
	inst->alg.encrypt = adiantum_encrypt;
	inst->alg.decrypt = adiantum_decrypt;
	inst->alg.init = adiantum_init_tfm;
	inst->alg.exit = adiantum_exit_tfm;
	inst->alg.min_keysize = crypto_skcipher_alg_min_keysize(streamcipher_alg);
	inst->alg.max_keysize = crypto_skcipher_alg_max_keysize(streamcipher_alg);
	inst->alg.ivsize = TWEAK_SIZE;

	inst->free = adiantum_free_instance;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		goto out_drop_hash;

	crypto_mod_put(_hash_alg);
	return 0;

out_drop_hash:
	crypto_drop_shash(&ictx->hash_spawn);
out_put_hash:
	crypto_mod_put(_hash_alg);
out_drop_blockcipher:
	crypto_drop_spawn(&ictx->blockcipher_spawn);
out_drop_streamcipher:
	crypto_drop_skcipher(&ictx->streamcipher_spawn);
out_free_inst:
	kfree(inst);
	return err;
}

/* adiantum(streamcipher_name, blockcipher_name [, nhpoly1305_name]) */
static struct crypto_template adiantum_tmpl = {
	.name = "adiantum",
	.create = adiantum_create,
	.module = THIS_MODULE,
};

static int __init adiantum_module_init(void)
{
	return crypto_register_template(&adiantum_tmpl);
}

static void __exit adiantum_module_exit(void)
{
	crypto_unregister_template(&adiantum_tmpl);
}

module_init(adiantum_module_init);
module_exit(adiantum_module_exit);

MODULE_DESCRIPTION("Adiantum length-preserving encryption mode");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_ALIAS_CRYPTO("adiantum");
