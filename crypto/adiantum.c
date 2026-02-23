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
 */

#include <crypto/b128ops.h>
#include <crypto/chacha.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/poly1305.h>
#include <crypto/internal/skcipher.h>
#include <crypto/nh.h>
#include <crypto/scatterwalk.h>
#include <linux/module.h>

/*
 * Size of right-hand part of input data, in bytes; also the size of the block
 * cipher's block size and the hash function's output.
 */
#define BLOCKCIPHER_BLOCK_SIZE		16

/* Size of the block cipher key (K_E) in bytes */
#define BLOCKCIPHER_KEY_SIZE		32

/* Size of the hash key (K_H) in bytes */
#define HASH_KEY_SIZE		(2 * POLY1305_BLOCK_SIZE + NH_KEY_BYTES)

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
	struct crypto_cipher_spawn blockcipher_spawn;
};

struct adiantum_tfm_ctx {
	struct crypto_skcipher *streamcipher;
	struct crypto_cipher *blockcipher;
	struct poly1305_core_key header_hash_key;
	struct poly1305_core_key msg_poly_key;
	u32 nh_key[NH_KEY_WORDS];
};

struct nhpoly1305_ctx {
	/* Running total of polynomial evaluation */
	struct poly1305_state poly_state;

	/* Partial block buffer */
	u8 buffer[NH_MESSAGE_UNIT];
	unsigned int buflen;

	/*
	 * Number of bytes remaining until the current NH message reaches
	 * NH_MESSAGE_BYTES.  When nonzero, 'nh_hash' holds the partial NH hash.
	 */
	unsigned int nh_remaining;

	__le64 nh_hash[NH_NUM_PASSES];
};

struct adiantum_request_ctx {
	/*
	 * skcipher sub-request size is unknown at compile-time, so it needs to
	 * go after the members with known sizes.
	 */
	union {
		struct nhpoly1305_ctx hash_ctx;
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
	if (err)
		goto out;
	keyp += BLOCKCIPHER_KEY_SIZE;

	/* Set the hash key (K_H) */
	poly1305_core_setkey(&tctx->header_hash_key, keyp);
	keyp += POLY1305_BLOCK_SIZE;
	poly1305_core_setkey(&tctx->msg_poly_key, keyp);
	keyp += POLY1305_BLOCK_SIZE;
	for (int i = 0; i < NH_KEY_WORDS; i++)
		tctx->nh_key[i] = get_unaligned_le32(&keyp[i * 4]);
	keyp += NH_KEY_BYTES;
	WARN_ON(keyp != &data->derived_keys[ARRAY_SIZE(data->derived_keys)]);
out:
	kfree_sensitive(data);
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
 * result to @out.  This is the calculation
 *
 *	H_T ← Poly1305_{K_T}(bin_{128}(|L|) || T)
 *
 * from the procedure in section 6.4 of the Adiantum paper.  The resulting value
 * is reused in both the first and second hash steps.  Specifically, it's added
 * to the result of an independently keyed ε-∆U hash function (for equal length
 * inputs only) taken over the left-hand part (the "bulk") of the message, to
 * give the overall Adiantum hash of the (tweak, left-hand part) pair.
 */
static void adiantum_hash_header(struct skcipher_request *req, le128 *out)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
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

	poly1305_core_emit(&state, NULL, out);
}

/* Pass the next NH hash value through Poly1305 */
static void process_nh_hash_value(struct nhpoly1305_ctx *ctx,
				  const struct adiantum_tfm_ctx *key)
{
	static_assert(NH_HASH_BYTES % POLY1305_BLOCK_SIZE == 0);

	poly1305_core_blocks(&ctx->poly_state, &key->msg_poly_key, ctx->nh_hash,
			     NH_HASH_BYTES / POLY1305_BLOCK_SIZE, 1);
}

/*
 * Feed the next portion of the message data, as a whole number of 16-byte
 * "NH message units", through NH and Poly1305.  Each NH hash is taken over
 * 1024 bytes, except possibly the final one which is taken over a multiple of
 * 16 bytes up to 1024.  Also, in the case where data is passed in misaligned
 * chunks, we combine partial hashes; the end result is the same either way.
 */
static void nhpoly1305_units(struct nhpoly1305_ctx *ctx,
			     const struct adiantum_tfm_ctx *key,
			     const u8 *data, size_t len)
{
	do {
		unsigned int bytes;

		if (ctx->nh_remaining == 0) {
			/* Starting a new NH message */
			bytes = min(len, NH_MESSAGE_BYTES);
			nh(key->nh_key, data, bytes, ctx->nh_hash);
			ctx->nh_remaining = NH_MESSAGE_BYTES - bytes;
		} else {
			/* Continuing a previous NH message */
			__le64 tmp_hash[NH_NUM_PASSES];
			unsigned int pos;

			pos = NH_MESSAGE_BYTES - ctx->nh_remaining;
			bytes = min(len, ctx->nh_remaining);
			nh(&key->nh_key[pos / 4], data, bytes, tmp_hash);
			for (int i = 0; i < NH_NUM_PASSES; i++)
				le64_add_cpu(&ctx->nh_hash[i],
					     le64_to_cpu(tmp_hash[i]));
			ctx->nh_remaining -= bytes;
		}
		if (ctx->nh_remaining == 0)
			process_nh_hash_value(ctx, key);
		data += bytes;
		len -= bytes;
	} while (len);
}

static void nhpoly1305_init(struct nhpoly1305_ctx *ctx)
{
	poly1305_core_init(&ctx->poly_state);
	ctx->buflen = 0;
	ctx->nh_remaining = 0;
}

static void nhpoly1305_update(struct nhpoly1305_ctx *ctx,
			      const struct adiantum_tfm_ctx *key,
			      const u8 *data, size_t len)
{
	unsigned int bytes;

	if (ctx->buflen) {
		bytes = min(len, (int)NH_MESSAGE_UNIT - ctx->buflen);
		memcpy(&ctx->buffer[ctx->buflen], data, bytes);
		ctx->buflen += bytes;
		if (ctx->buflen < NH_MESSAGE_UNIT)
			return;
		nhpoly1305_units(ctx, key, ctx->buffer, NH_MESSAGE_UNIT);
		ctx->buflen = 0;
		data += bytes;
		len -= bytes;
	}

	if (len >= NH_MESSAGE_UNIT) {
		bytes = round_down(len, NH_MESSAGE_UNIT);
		nhpoly1305_units(ctx, key, data, bytes);
		data += bytes;
		len -= bytes;
	}

	if (len) {
		memcpy(ctx->buffer, data, len);
		ctx->buflen = len;
	}
}

static void nhpoly1305_final(struct nhpoly1305_ctx *ctx,
			     const struct adiantum_tfm_ctx *key, le128 *out)
{
	if (ctx->buflen) {
		memset(&ctx->buffer[ctx->buflen], 0,
		       NH_MESSAGE_UNIT - ctx->buflen);
		nhpoly1305_units(ctx, key, ctx->buffer, NH_MESSAGE_UNIT);
	}

	if (ctx->nh_remaining)
		process_nh_hash_value(ctx, key);

	poly1305_core_emit(&ctx->poly_state, NULL, out);
}

/*
 * Hash the left-hand part (the "bulk") of the message as follows:
 *
 *	H_L ← Poly1305_{K_L}(NH_{K_N}(pad_{128}(L)))
 *
 * See section 6.4 of the Adiantum paper.  This is an ε-almost-∆-universal
 * (ε-∆U) hash function for equal-length inputs over Z/(2^{128}Z), where the "∆"
 * operation is addition.  It hashes 1024-byte chunks of the input with the NH
 * hash function, reducing the input length by 32x.  The resulting NH hashes are
 * evaluated as a polynomial in GF(2^{130}-5), like in the Poly1305 MAC.  Note
 * that the polynomial evaluation by itself would suffice to achieve the ε-∆U
 * property; NH is used for performance since it's much faster than Poly1305.
 */
static void adiantum_hash_message(struct skcipher_request *req,
				  struct scatterlist *sgl, le128 *out)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct adiantum_request_ctx *rctx = skcipher_request_ctx(req);
	unsigned int len = req->cryptlen - BLOCKCIPHER_BLOCK_SIZE;
	struct scatter_walk walk;

	nhpoly1305_init(&rctx->u.hash_ctx);
	scatterwalk_start(&walk, sgl);
	while (len) {
		unsigned int n = scatterwalk_next(&walk, len);

		nhpoly1305_update(&rctx->u.hash_ctx, tctx, walk.addr, n);
		scatterwalk_done_src(&walk, n);
		len -= n;
	}
	nhpoly1305_final(&rctx->u.hash_ctx, tctx, out);
}

static int adiantum_crypt(struct skcipher_request *req, bool enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct adiantum_request_ctx *rctx = skcipher_request_ctx(req);
	const unsigned int bulk_len = req->cryptlen - BLOCKCIPHER_BLOCK_SIZE;
	struct scatterlist *src = req->src, *dst = req->dst;
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
		le128 bignum; /* interpret as element of Z/(2^{128}Z) */
	} rbuf;
	le128 header_hash, msg_hash;
	unsigned int stream_len;
	int err;

	if (req->cryptlen < BLOCKCIPHER_BLOCK_SIZE)
		return -EINVAL;

	/*
	 * First hash step
	 *	enc: P_M = P_R + H_{K_H}(T, P_L)
	 *	dec: C_M = C_R + H_{K_H}(T, C_L)
	 */
	adiantum_hash_header(req, &header_hash);
	if (src->length >= req->cryptlen &&
	    src->offset + req->cryptlen <= PAGE_SIZE) {
		/* Fast path for single-page source */
		void *virt = kmap_local_page(sg_page(src)) + src->offset;

		nhpoly1305_init(&rctx->u.hash_ctx);
		nhpoly1305_update(&rctx->u.hash_ctx, tctx, virt, bulk_len);
		nhpoly1305_final(&rctx->u.hash_ctx, tctx, &msg_hash);
		memcpy(&rbuf.bignum, virt + bulk_len, sizeof(le128));
		kunmap_local(virt);
	} else {
		/* Slow path that works for any source scatterlist */
		adiantum_hash_message(req, src, &msg_hash);
		memcpy_from_sglist(&rbuf.bignum, src, bulk_len, sizeof(le128));
	}
	le128_add(&rbuf.bignum, &rbuf.bignum, &header_hash);
	le128_add(&rbuf.bignum, &rbuf.bignum, &msg_hash);

	/* If encrypting, encrypt P_M with the block cipher to get C_M */
	if (enc)
		crypto_cipher_encrypt_one(tctx->blockcipher, rbuf.bytes,
					  rbuf.bytes);

	/* Initialize the rest of the XChaCha IV (first part is C_M) */
	BUILD_BUG_ON(BLOCKCIPHER_BLOCK_SIZE != 16);
	BUILD_BUG_ON(XCHACHA_IV_SIZE != 32);	/* nonce || stream position */
	rbuf.words[4] = cpu_to_le32(1);
	rbuf.words[5] = 0;
	rbuf.words[6] = 0;
	rbuf.words[7] = 0;

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
				   req->dst, stream_len, &rbuf);
	skcipher_request_set_callback(&rctx->u.streamcipher_req,
				      req->base.flags, NULL, NULL);
	err = crypto_skcipher_encrypt(&rctx->u.streamcipher_req);
	if (err)
		return err;

	/* If decrypting, decrypt C_M with the block cipher to get P_M */
	if (!enc)
		crypto_cipher_decrypt_one(tctx->blockcipher, rbuf.bytes,
					  rbuf.bytes);

	/*
	 * Second hash step
	 *	enc: C_R = C_M - H_{K_H}(T, C_L)
	 *	dec: P_R = P_M - H_{K_H}(T, P_L)
	 */
	le128_sub(&rbuf.bignum, &rbuf.bignum, &header_hash);
	if (dst->length >= req->cryptlen &&
	    dst->offset + req->cryptlen <= PAGE_SIZE) {
		/* Fast path for single-page destination */
		struct page *page = sg_page(dst);
		void *virt = kmap_local_page(page) + dst->offset;

		nhpoly1305_init(&rctx->u.hash_ctx);
		nhpoly1305_update(&rctx->u.hash_ctx, tctx, virt, bulk_len);
		nhpoly1305_final(&rctx->u.hash_ctx, tctx, &msg_hash);
		le128_sub(&rbuf.bignum, &rbuf.bignum, &msg_hash);
		memcpy(virt + bulk_len, &rbuf.bignum, sizeof(le128));
		flush_dcache_page(page);
		kunmap_local(virt);
	} else {
		/* Slow path that works for any destination scatterlist */
		adiantum_hash_message(req, dst, &msg_hash);
		le128_sub(&rbuf.bignum, &rbuf.bignum, &msg_hash);
		memcpy_to_sglist(dst, bulk_len, &rbuf.bignum, sizeof(le128));
	}
	return 0;
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
	int err;

	streamcipher = crypto_spawn_skcipher(&ictx->streamcipher_spawn);
	if (IS_ERR(streamcipher))
		return PTR_ERR(streamcipher);

	blockcipher = crypto_spawn_cipher(&ictx->blockcipher_spawn);
	if (IS_ERR(blockcipher)) {
		err = PTR_ERR(blockcipher);
		goto err_free_streamcipher;
	}

	tctx->streamcipher = streamcipher;
	tctx->blockcipher = blockcipher;

	BUILD_BUG_ON(offsetofend(struct adiantum_request_ctx, u) !=
		     sizeof(struct adiantum_request_ctx));
	crypto_skcipher_set_reqsize(
		tfm, max(sizeof(struct adiantum_request_ctx),
			 offsetofend(struct adiantum_request_ctx,
				     u.streamcipher_req) +
				 crypto_skcipher_reqsize(streamcipher)));
	return 0;

err_free_streamcipher:
	crypto_free_skcipher(streamcipher);
	return err;
}

static void adiantum_exit_tfm(struct crypto_skcipher *tfm)
{
	struct adiantum_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(tctx->streamcipher);
	crypto_free_cipher(tctx->blockcipher);
}

static void adiantum_free_instance(struct skcipher_instance *inst)
{
	struct adiantum_instance_ctx *ictx = skcipher_instance_ctx(inst);

	crypto_drop_skcipher(&ictx->streamcipher_spawn);
	crypto_drop_cipher(&ictx->blockcipher_spawn);
	kfree(inst);
}

/*
 * Check for a supported set of inner algorithms.
 * See the comment at the beginning of this file.
 */
static bool
adiantum_supported_algorithms(struct skcipher_alg_common *streamcipher_alg,
			      struct crypto_alg *blockcipher_alg)
{
	if (strcmp(streamcipher_alg->base.cra_name, "xchacha12") != 0 &&
	    strcmp(streamcipher_alg->base.cra_name, "xchacha20") != 0)
		return false;

	if (blockcipher_alg->cra_cipher.cia_min_keysize > BLOCKCIPHER_KEY_SIZE ||
	    blockcipher_alg->cra_cipher.cia_max_keysize < BLOCKCIPHER_KEY_SIZE)
		return false;
	if (blockcipher_alg->cra_blocksize != BLOCKCIPHER_BLOCK_SIZE)
		return false;

	return true;
}

static int adiantum_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	u32 mask;
	struct skcipher_instance *inst;
	struct adiantum_instance_ctx *ictx;
	struct skcipher_alg_common *streamcipher_alg;
	struct crypto_alg *blockcipher_alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SKCIPHER, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ictx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	ictx = skcipher_instance_ctx(inst);

	/* Stream cipher, e.g. "xchacha12" */
	err = crypto_grab_skcipher(&ictx->streamcipher_spawn,
				   skcipher_crypto_instance(inst),
				   crypto_attr_alg_name(tb[1]), 0,
				   mask | CRYPTO_ALG_ASYNC /* sync only */);
	if (err)
		goto err_free_inst;
	streamcipher_alg = crypto_spawn_skcipher_alg_common(&ictx->streamcipher_spawn);

	/* Block cipher, e.g. "aes" */
	err = crypto_grab_cipher(&ictx->blockcipher_spawn,
				 skcipher_crypto_instance(inst),
				 crypto_attr_alg_name(tb[2]), 0, mask);
	if (err)
		goto err_free_inst;
	blockcipher_alg = crypto_spawn_cipher_alg(&ictx->blockcipher_spawn);

	/*
	 * Originally there was an optional third parameter, for requesting a
	 * specific implementation of "nhpoly1305" for message hashing.  This is
	 * no longer supported.  The best implementation is just always used.
	 */
	if (crypto_attr_alg_name(tb[3]) != ERR_PTR(-ENOENT)) {
		err = -ENOENT;
		goto err_free_inst;
	}

	/* Check the set of algorithms */
	if (!adiantum_supported_algorithms(streamcipher_alg, blockcipher_alg)) {
		pr_warn("Unsupported Adiantum instantiation: (%s,%s)\n",
			streamcipher_alg->base.cra_name,
			blockcipher_alg->cra_name);
		err = -EINVAL;
		goto err_free_inst;
	}

	/* Instance fields */

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "adiantum(%s,%s)", streamcipher_alg->base.cra_name,
		     blockcipher_alg->cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "adiantum(%s,%s)", streamcipher_alg->base.cra_driver_name,
		     blockcipher_alg->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_blocksize = BLOCKCIPHER_BLOCK_SIZE;
	inst->alg.base.cra_ctxsize = sizeof(struct adiantum_tfm_ctx);
	inst->alg.base.cra_alignmask = streamcipher_alg->base.cra_alignmask;
	/*
	 * The block cipher is only invoked once per message, so for long
	 * messages (e.g. sectors for disk encryption) its performance doesn't
	 * matter as much as that of the stream cipher.  Thus, weigh the block
	 * cipher's ->cra_priority less.
	 */
	inst->alg.base.cra_priority = (4 * streamcipher_alg->base.cra_priority +
				       blockcipher_alg->cra_priority) /
				      5;

	inst->alg.setkey = adiantum_setkey;
	inst->alg.encrypt = adiantum_encrypt;
	inst->alg.decrypt = adiantum_decrypt;
	inst->alg.init = adiantum_init_tfm;
	inst->alg.exit = adiantum_exit_tfm;
	inst->alg.min_keysize = streamcipher_alg->min_keysize;
	inst->alg.max_keysize = streamcipher_alg->max_keysize;
	inst->alg.ivsize = TWEAK_SIZE;

	inst->free = adiantum_free_instance;

	err = skcipher_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		adiantum_free_instance(inst);
	}
	return err;
}

/* adiantum(streamcipher_name, blockcipher_name) */
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
MODULE_IMPORT_NS("CRYPTO_INTERNAL");
