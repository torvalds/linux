/*
 * HEH: Hash-Encrypt-Hash mode
 *
 * Copyright (c) 2016 Google Inc.
 *
 * Authors:
 *	Alex Cope <alexcope@google.com>
 *	Eric Biggers <ebiggers@google.com>
 */

/*
 * Hash-Encrypt-Hash (HEH) is a proposed block cipher mode of operation which
 * extends the strong pseudo-random permutation (SPRP) property of block ciphers
 * (e.g. AES) to arbitrary length input strings.  It uses two keyed invertible
 * hash functions with a layer of ECB encryption applied in-between.  The
 * algorithm is specified by the following Internet Draft:
 *
 *	https://tools.ietf.org/html/draft-cope-heh-01
 *
 * Although HEH can be used as either a regular symmetric cipher or as an AEAD,
 * currently this module only provides it as a symmetric cipher.  Additionally,
 * only 16-byte nonces are supported.
 */

#include <crypto/gf128mul.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include "internal.h"

/*
 * The block size is the size of GF(2^128) elements and also the required block
 * size of the underlying block cipher.
 */
#define HEH_BLOCK_SIZE		16

struct heh_instance_ctx {
	struct crypto_shash_spawn cmac;
	struct crypto_shash_spawn poly_hash;
	struct crypto_skcipher_spawn ecb;
};

struct heh_tfm_ctx {
	struct crypto_shash *cmac;
	struct crypto_shash *poly_hash; /* keyed with tau_key */
	struct crypto_ablkcipher *ecb;
};

struct heh_cmac_data {
	u8 nonce[HEH_BLOCK_SIZE];
	__le32 nonce_length;
	__le32 aad_length;
	__le32 message_length;
	__le32 padding;
};

struct heh_req_ctx { /* aligned to alignmask */
	be128 beta1_key;
	be128 beta2_key;
	union {
		struct {
			struct heh_cmac_data data;
			struct shash_desc desc;
			/* + crypto_shash_descsize(cmac) */
		} cmac;
		struct {
			struct shash_desc desc;
			/* + crypto_shash_descsize(poly_hash) */
		} poly_hash;
		struct {
			u8 keystream[HEH_BLOCK_SIZE];
			u8 tmp[HEH_BLOCK_SIZE];
			struct scatterlist tmp_sgl[2];
			struct ablkcipher_request req;
			/* + crypto_ablkcipher_reqsize(ecb) */
		} ecb;
	} u;
};

/*
 * Get the offset in bytes to the last full block, or equivalently the length of
 * all full blocks excluding the last
 */
static inline unsigned int get_tail_offset(unsigned int len)
{
	len -= len % HEH_BLOCK_SIZE;
	return len - HEH_BLOCK_SIZE;
}

static inline struct heh_req_ctx *heh_req_ctx(struct ablkcipher_request *req)
{
	unsigned int alignmask = crypto_ablkcipher_alignmask(
						crypto_ablkcipher_reqtfm(req));

	return (void *)PTR_ALIGN((u8 *)ablkcipher_request_ctx(req),
				 alignmask + 1);
}

static inline void async_done(struct crypto_async_request *areq, int err,
			      int (*next_step)(struct ablkcipher_request *,
					       u32))
{
	struct ablkcipher_request *req = areq->data;

	if (err)
		goto out;

	err = next_step(req, req->base.flags & ~CRYPTO_TFM_REQ_MAY_SLEEP);
	if (err == -EINPROGRESS ||
	    (err == -EBUSY && (req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)))
		return;
out:
	ablkcipher_request_complete(req, err);
}

/*
 * Generate the per-message "beta" keys used by the hashing layers of HEH.  The
 * first beta key is the CMAC of the nonce, the additional authenticated data
 * (AAD), and the lengths in bytes of the nonce, AAD, and message.  The nonce
 * and AAD are each zero-padded to the next 16-byte block boundary, and the
 * lengths are serialized as 4-byte little endian integers and zero-padded to
 * the next 16-byte block boundary.
 * The second beta key is the first one interpreted as an element in GF(2^128)
 * and multiplied by x.
 *
 * Note that because the nonce and AAD may, in general, be variable-length, the
 * key generation must be done by a pseudo-random function (PRF) on
 * variable-length inputs.  CBC-MAC does not satisfy this, as it is only a PRF
 * on fixed-length inputs.  CMAC remedies this flaw.  Including the lengths of
 * the nonce, AAD, and message is also critical to avoid collisions.
 *
 * That being said, this implementation does not yet operate as an AEAD and
 * therefore there is never any AAD, nor are variable-length nonces supported.
 */
static int generate_betas(struct ablkcipher_request *req,
			  be128 *beta1_key, be128 *beta2_key)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct heh_tfm_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct heh_req_ctx *rctx = heh_req_ctx(req);
	struct heh_cmac_data *data = &rctx->u.cmac.data;
	struct shash_desc *desc = &rctx->u.cmac.desc;
	int err;

	BUILD_BUG_ON(sizeof(*data) != 2 * HEH_BLOCK_SIZE);
	memcpy(data->nonce, req->info, HEH_BLOCK_SIZE);
	data->nonce_length = cpu_to_le32(HEH_BLOCK_SIZE);
	data->aad_length = cpu_to_le32(0);
	data->message_length = cpu_to_le32(req->nbytes);
	data->padding = cpu_to_le32(0);

	desc->tfm = ctx->cmac;
	desc->flags = req->base.flags;

	err = crypto_shash_digest(desc, (const u8 *)data, sizeof(*data),
				  (u8 *)beta1_key);
	if (err)
		return err;

	gf128mul_x_ble(beta2_key, beta1_key);
	return 0;
}

/*****************************************************************************/

/*
 * This is the generic version of poly_hash.  It does the GF(2^128)
 * multiplication by 'tau_key' using a precomputed table, without using any
 * special CPU instructions.  On some platforms, an accelerated version (with
 * higher cra_priority) may be used instead.
 */

struct poly_hash_tfm_ctx {
	struct gf128mul_4k *tau_key;
};

struct poly_hash_desc_ctx {
	be128 digest;
	unsigned int count;
};

static int poly_hash_setkey(struct crypto_shash *tfm,
			    const u8 *key, unsigned int keylen)
{
	struct poly_hash_tfm_ctx *tctx = crypto_shash_ctx(tfm);
	be128 key128;

	if (keylen != HEH_BLOCK_SIZE) {
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if (tctx->tau_key)
		gf128mul_free_4k(tctx->tau_key);
	memcpy(&key128, key, HEH_BLOCK_SIZE);
	tctx->tau_key = gf128mul_init_4k_ble(&key128);
	if (!tctx->tau_key)
		return -ENOMEM;
	return 0;
}

static int poly_hash_init(struct shash_desc *desc)
{
	struct poly_hash_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->digest = (be128) { 0 };
	ctx->count = 0;
	return 0;
}

static int poly_hash_update(struct shash_desc *desc, const u8 *src,
			    unsigned int len)
{
	struct poly_hash_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct poly_hash_desc_ctx *ctx = shash_desc_ctx(desc);
	unsigned int partial = ctx->count % HEH_BLOCK_SIZE;
	u8 *dst = (u8 *)&ctx->digest + partial;

	ctx->count += len;

	/* Finishing at least one block? */
	if (partial + len >= HEH_BLOCK_SIZE) {

		if (partial) {
			/* Finish the pending block. */
			unsigned int n = HEH_BLOCK_SIZE - partial;

			len -= n;
			do {
				*dst++ ^= *src++;
			} while (--n);

			gf128mul_4k_ble(&ctx->digest, tctx->tau_key);
		}

		/* Process zero or more full blocks. */
		while (len >= HEH_BLOCK_SIZE) {
			be128 coeff;

			memcpy(&coeff, src, HEH_BLOCK_SIZE);
			be128_xor(&ctx->digest, &ctx->digest, &coeff);
			src += HEH_BLOCK_SIZE;
			len -= HEH_BLOCK_SIZE;
			gf128mul_4k_ble(&ctx->digest, tctx->tau_key);
		}
		dst = (u8 *)&ctx->digest;
	}

	/* Continue adding the next block to 'digest'. */
	while (len--)
		*dst++ ^= *src++;
	return 0;
}

static int poly_hash_final(struct shash_desc *desc, u8 *out)
{
	struct poly_hash_desc_ctx *ctx = shash_desc_ctx(desc);

	/* Finish the last block if needed. */
	if (ctx->count % HEH_BLOCK_SIZE) {
		struct poly_hash_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);

		gf128mul_4k_ble(&ctx->digest, tctx->tau_key);
	}

	memcpy(out, &ctx->digest, HEH_BLOCK_SIZE);
	return 0;
}

static void poly_hash_exit(struct crypto_tfm *tfm)
{
	struct poly_hash_tfm_ctx *tctx = crypto_tfm_ctx(tfm);

	gf128mul_free_4k(tctx->tau_key);
}

static struct shash_alg poly_hash_alg = {
	.digestsize	= HEH_BLOCK_SIZE,
	.init		= poly_hash_init,
	.update		= poly_hash_update,
	.final		= poly_hash_final,
	.setkey		= poly_hash_setkey,
	.descsize	= sizeof(struct poly_hash_desc_ctx),
	.base		= {
		.cra_name		= "poly_hash",
		.cra_driver_name	= "poly_hash-generic",
		.cra_priority		= 100,
		.cra_ctxsize		= sizeof(struct poly_hash_tfm_ctx),
		.cra_exit		= poly_hash_exit,
		.cra_module		= THIS_MODULE,
	},
};

/*****************************************************************************/

/*
 * Split the message into 16 byte blocks, padding out the last block, and use
 * the blocks as coefficients in the evaluation of a polynomial over GF(2^128)
 * at the secret point 'tau_key'. For ease of implementing the higher-level
 * heh_hash_inv() function, the constant and degree-1 coefficients are swapped
 * if there is a partial block.
 *
 * Mathematically, compute:
 *   if (no partial block)
 *     k^{N-1} * m_0 + ... + k * m_{N-2} + m_{N-1}
 *   else if (partial block)
 *     k^N * m_0 + ... + k^2 * m_{N-2} + k * m_N + m_{N-1}
 *
 * where:
 *	t is tau_key
 *	N is the number of full blocks in the message
 *	m_i is the i-th full block in the message for i = 0 to N-1 inclusive
 *	m_N is the partial block of the message zero-padded up to 16 bytes
 *
 * Note that most of this is now separated out into its own keyed hash
 * algorithm, to allow optimized implementations.  However, we still handle the
 * swapping of the last two coefficients here in the HEH template because this
 * simplifies the poly_hash algorithms: they don't have to buffer an extra
 * block, don't have to duplicate as much code, and are more similar to GHASH.
 */
static int poly_hash(struct ablkcipher_request *req, struct scatterlist *sgl,
		     be128 *hash)
{
	struct heh_req_ctx *rctx = heh_req_ctx(req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct heh_tfm_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct shash_desc *desc = &rctx->u.poly_hash.desc;
	unsigned int tail_offset = get_tail_offset(req->nbytes);
	unsigned int tail_len = req->nbytes - tail_offset;
	be128 tail[2];
	unsigned int i, n;
	struct sg_mapping_iter miter;
	int err;

	desc->tfm = ctx->poly_hash;
	desc->flags = req->base.flags;

	/* Handle all full blocks except the last */
	err = crypto_shash_init(desc);
	sg_miter_start(&miter, sgl, sg_nents(sgl),
		       SG_MITER_FROM_SG | SG_MITER_ATOMIC);
	for (i = 0; i < tail_offset && !err; i += n) {
		sg_miter_next(&miter);
		n = min_t(unsigned int, miter.length, tail_offset - i);
		err = crypto_shash_update(desc, miter.addr, n);
	}
	sg_miter_stop(&miter);
	if (err)
		return err;

	/* Handle the last full block and the partial block */
	scatterwalk_map_and_copy(tail, sgl, tail_offset, tail_len, 0);

	if (tail_len != HEH_BLOCK_SIZE) {
		/* handle the partial block */
		memset((u8 *)tail + tail_len, 0, sizeof(tail) - tail_len);
		err = crypto_shash_update(desc, (u8 *)&tail[1], HEH_BLOCK_SIZE);
		if (err)
			return err;
	}
	err = crypto_shash_final(desc, (u8 *)hash);
	if (err)
		return err;
	be128_xor(hash, hash, &tail[0]);
	return 0;
}

/*
 * Transform all full blocks except the last.
 * This is used by both the hash and inverse hash phases.
 */
static int heh_tfm_blocks(struct ablkcipher_request *req,
			  struct scatterlist *src_sgl,
			  struct scatterlist *dst_sgl, unsigned int len,
			  const be128 *hash, const be128 *beta_key)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct blkcipher_desc desc = { .flags = req->base.flags };
	struct blkcipher_walk walk;
	be128 e = *beta_key;
	int err;
	unsigned int nbytes;

	blkcipher_walk_init(&walk, dst_sgl, src_sgl, len);

	err = blkcipher_ablkcipher_walk_virt(&desc, &walk, tfm);

	while ((nbytes = walk.nbytes)) {
		const be128 *src = (be128 *)walk.src.virt.addr;
		be128 *dst = (be128 *)walk.dst.virt.addr;

		do {
			gf128mul_x_ble(&e, &e);
			be128_xor(dst, src, hash);
			be128_xor(dst, dst, &e);
			src++;
			dst++;
		} while ((nbytes -= HEH_BLOCK_SIZE) >= HEH_BLOCK_SIZE);
		err = blkcipher_walk_done(&desc, &walk, nbytes);
	}
	return err;
}

/*
 * The hash phase of HEH.  Given a message, compute:
 *
 *     (m_0 + H, ..., m_{N-2} + H, H, m_N) + (xb, x^2b, ..., x^{N-1}b, b, 0)
 *
 * where:
 *	N is the number of full blocks in the message
 *	m_i is the i-th full block in the message for i = 0 to N-1 inclusive
 *	m_N is the unpadded partial block, possibly empty
 *	H is the poly_hash() of the message, keyed by tau_key
 *	b is beta_key
 *	x is the element x in our representation of GF(2^128)
 *
 * Note that the partial block remains unchanged, but it does affect the result
 * of poly_hash() and therefore the transformation of all the full blocks.
 */
static int heh_hash(struct ablkcipher_request *req, const be128 *beta_key)
{
	be128 hash;
	unsigned int tail_offset = get_tail_offset(req->nbytes);
	unsigned int partial_len = req->nbytes % HEH_BLOCK_SIZE;
	int err;

	/* poly_hash() the full message including the partial block */
	err = poly_hash(req, req->src, &hash);
	if (err)
		return err;

	/* Transform all full blocks except the last */
	err = heh_tfm_blocks(req, req->src, req->dst, tail_offset, &hash,
			     beta_key);
	if (err)
		return err;

	/* Set the last full block to hash XOR beta_key */
	be128_xor(&hash, &hash, beta_key);
	scatterwalk_map_and_copy(&hash, req->dst, tail_offset, HEH_BLOCK_SIZE,
				 1);

	/* Copy the partial block if needed */
	if (partial_len != 0 && req->src != req->dst) {
		unsigned int offs = tail_offset + HEH_BLOCK_SIZE;

		scatterwalk_map_and_copy(&hash, req->src, offs, partial_len, 0);
		scatterwalk_map_and_copy(&hash, req->dst, offs, partial_len, 1);
	}
	return 0;
}

/*
 * The inverse hash phase of HEH.  This undoes the result of heh_hash().
 */
static int heh_hash_inv(struct ablkcipher_request *req, const be128 *beta_key)
{
	be128 hash;
	be128 tmp;
	struct scatterlist tmp_sgl[2];
	struct scatterlist *tail_sgl;
	unsigned int tail_offset = get_tail_offset(req->nbytes);
	struct scatterlist *sgl = req->dst;
	int err;

	/*
	 * The last full block was computed as hash XOR beta_key, so XOR it with
	 * beta_key to recover hash.
	 */
	tail_sgl = scatterwalk_ffwd(tmp_sgl, sgl, tail_offset);
	scatterwalk_map_and_copy(&hash, tail_sgl, 0, HEH_BLOCK_SIZE, 0);
	be128_xor(&hash, &hash, beta_key);

	/* Transform all full blocks except the last */
	err = heh_tfm_blocks(req, sgl, sgl, tail_offset, &hash, beta_key);
	if (err)
		return err;

	/*
	 * Recover the last full block.  We know 'hash', i.e. the poly_hash() of
	 * the the original message.  The last full block was the constant term
	 * of the polynomial.  To recover the last full block, temporarily zero
	 * it, compute the poly_hash(), and take the difference from 'hash'.
	 */
	memset(&tmp, 0, sizeof(tmp));
	scatterwalk_map_and_copy(&tmp, tail_sgl, 0, HEH_BLOCK_SIZE, 1);
	err = poly_hash(req, sgl, &tmp);
	if (err)
		return err;
	be128_xor(&tmp, &tmp, &hash);
	scatterwalk_map_and_copy(&tmp, tail_sgl, 0, HEH_BLOCK_SIZE, 1);
	return 0;
}

static int heh_hash_inv_step(struct ablkcipher_request *req, u32 flags)
{
	struct heh_req_ctx *rctx = heh_req_ctx(req);

	return heh_hash_inv(req, &rctx->beta2_key);
}

static int heh_ecb_step_3(struct ablkcipher_request *req, u32 flags)
{
	struct heh_req_ctx *rctx = heh_req_ctx(req);
	u8 partial_block[HEH_BLOCK_SIZE] __aligned(__alignof__(u32));
	unsigned int tail_offset = get_tail_offset(req->nbytes);
	unsigned int partial_offset = tail_offset + HEH_BLOCK_SIZE;
	unsigned int partial_len = req->nbytes - partial_offset;

	/*
	 * Extract the pad in req->dst at tail_offset, and xor the partial block
	 * with it to create encrypted partial block
	 */
	scatterwalk_map_and_copy(rctx->u.ecb.keystream, req->dst, tail_offset,
				 HEH_BLOCK_SIZE, 0);
	scatterwalk_map_and_copy(partial_block, req->dst, partial_offset,
				 partial_len, 0);
	crypto_xor(partial_block, rctx->u.ecb.keystream, partial_len);

	/*
	 * Store the encrypted final block and partial block back in dst_sg
	 */
	scatterwalk_map_and_copy(&rctx->u.ecb.tmp, req->dst, tail_offset,
				 HEH_BLOCK_SIZE, 1);
	scatterwalk_map_and_copy(partial_block, req->dst, partial_offset,
				 partial_len, 1);

	return heh_hash_inv_step(req, flags);
}

static void heh_ecb_step_2_done(struct crypto_async_request *areq, int err)
{
	return async_done(areq, err, heh_ecb_step_3);
}

static int heh_ecb_step_2(struct ablkcipher_request *req, u32 flags)
{
	struct heh_req_ctx *rctx = heh_req_ctx(req);
	unsigned int partial_len = req->nbytes % HEH_BLOCK_SIZE;
	struct scatterlist *tmp_sgl;
	int err;
	unsigned int tail_offset = get_tail_offset(req->nbytes);

	if (partial_len == 0)
		return heh_hash_inv_step(req, flags);

	/*
	 * Extract the final full block, store it in tmp, and then xor that with
	 * the value saved in u.ecb.keystream
	 */
	scatterwalk_map_and_copy(rctx->u.ecb.tmp, req->dst, tail_offset,
				 HEH_BLOCK_SIZE, 0);
	crypto_xor(rctx->u.ecb.keystream, rctx->u.ecb.tmp, HEH_BLOCK_SIZE);

	/*
	 * Encrypt the value in rctx->u.ecb.keystream to create the pad for the
	 * partial block.
	 * We cannot encrypt stack buffers, so re-use the dst_sg to do this
	 * encryption to avoid a malloc. The value at tail_offset is stored in
	 * tmp, and will be restored later.
	 */
	scatterwalk_map_and_copy(rctx->u.ecb.keystream, req->dst, tail_offset,
				 HEH_BLOCK_SIZE, 1);
	tmp_sgl = scatterwalk_ffwd(rctx->u.ecb.tmp_sgl, req->dst, tail_offset);
	ablkcipher_request_set_callback(&rctx->u.ecb.req, flags,
					heh_ecb_step_2_done, req);
	ablkcipher_request_set_crypt(&rctx->u.ecb.req, tmp_sgl, tmp_sgl,
				     HEH_BLOCK_SIZE, NULL);
	err = crypto_ablkcipher_encrypt(&rctx->u.ecb.req);
	if (err)
		return err;
	return heh_ecb_step_3(req, flags);
}

static void heh_ecb_full_done(struct crypto_async_request *areq, int err)
{
	return async_done(areq, err, heh_ecb_step_2);
}

/*
 * The encrypt phase of HEH.  This uses ECB encryption, with special handling
 * for the partial block at the end if any.  The source data is already in
 * req->dst, so the encryption happens in-place.
 *
 * After the encrypt phase we continue on to the inverse hash phase.  The
 * functions calls are chained to support asynchronous ECB algorithms.
 */
static int heh_ecb(struct ablkcipher_request *req, bool decrypt)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct heh_tfm_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct heh_req_ctx *rctx = heh_req_ctx(req);
	struct ablkcipher_request *ecb_req = &rctx->u.ecb.req;
	unsigned int tail_offset = get_tail_offset(req->nbytes);
	unsigned int full_len = tail_offset + HEH_BLOCK_SIZE;
	int err;

	/*
	 * Save the last full block before it is encrypted/decrypted. This will
	 * be used later to encrypt/decrypt the partial block
	 */
	scatterwalk_map_and_copy(rctx->u.ecb.keystream, req->dst, tail_offset,
				 HEH_BLOCK_SIZE, 0);

	/* Encrypt/decrypt all full blocks */
	ablkcipher_request_set_tfm(ecb_req, ctx->ecb);
	ablkcipher_request_set_callback(ecb_req, req->base.flags,
				      heh_ecb_full_done, req);
	ablkcipher_request_set_crypt(ecb_req, req->dst, req->dst, full_len,
				     NULL);
	if (decrypt)
		err = crypto_ablkcipher_decrypt(ecb_req);
	else
		err = crypto_ablkcipher_encrypt(ecb_req);
	if (err)
		return err;

	return heh_ecb_step_2(req, req->base.flags);
}

static int heh_crypt(struct ablkcipher_request *req, bool decrypt)
{
	struct heh_req_ctx *rctx = heh_req_ctx(req);
	int err;

	/* Inputs must be at least one full block */
	if (req->nbytes < HEH_BLOCK_SIZE)
		return -EINVAL;

	err = generate_betas(req, &rctx->beta1_key, &rctx->beta2_key);
	if (err)
		return err;

	if (decrypt)
		swap(rctx->beta1_key, rctx->beta2_key);

	err = heh_hash(req, &rctx->beta1_key);
	if (err)
		return err;

	return heh_ecb(req, decrypt);
}

static int heh_encrypt(struct ablkcipher_request *req)
{
	return heh_crypt(req, false);
}

static int heh_decrypt(struct ablkcipher_request *req)
{
	return heh_crypt(req, true);
}

static int heh_setkey(struct crypto_ablkcipher *parent, const u8 *key,
		      unsigned int keylen)
{
	struct heh_tfm_ctx *ctx = crypto_ablkcipher_ctx(parent);
	struct crypto_shash *cmac = ctx->cmac;
	struct crypto_ablkcipher *ecb = ctx->ecb;
	SHASH_DESC_ON_STACK(desc, cmac);
	u8 *derived_keys;
	u8 digest[HEH_BLOCK_SIZE];
	unsigned int i;
	int err;

	/* set prf_key = key */
	crypto_shash_clear_flags(cmac, CRYPTO_TFM_REQ_MASK);
	crypto_shash_set_flags(cmac, crypto_ablkcipher_get_flags(parent) &
				     CRYPTO_TFM_REQ_MASK);
	err = crypto_shash_setkey(cmac, key, keylen);
	crypto_ablkcipher_set_flags(parent, crypto_shash_get_flags(cmac) &
					    CRYPTO_TFM_RES_MASK);
	if (err)
		return err;

	/*
	 * Generate tau_key and ecb_key as follows:
	 * tau_key = cmac(prf_key, 0x00...01)
	 * ecb_key = cmac(prf_key, 0x00...02) || cmac(prf_key, 0x00...03) || ...
	 *           truncated to keylen bytes
	 */
	derived_keys = kzalloc(round_up(HEH_BLOCK_SIZE + keylen,
					HEH_BLOCK_SIZE), GFP_KERNEL);
	if (!derived_keys)
		return -ENOMEM;
	desc->tfm = cmac;
	desc->flags = (crypto_shash_get_flags(cmac) & CRYPTO_TFM_REQ_MASK);
	for (i = 0; i < keylen + HEH_BLOCK_SIZE; i += HEH_BLOCK_SIZE) {
		derived_keys[i + HEH_BLOCK_SIZE - 1] =
					0x01 + i / HEH_BLOCK_SIZE;
		err = crypto_shash_digest(desc, derived_keys + i,
					  HEH_BLOCK_SIZE, digest);
		if (err)
			goto out;
		memcpy(derived_keys + i, digest, HEH_BLOCK_SIZE);
	}

	err = crypto_shash_setkey(ctx->poly_hash, derived_keys, HEH_BLOCK_SIZE);
	if (err)
		goto out;

	crypto_ablkcipher_clear_flags(ecb, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(ecb, crypto_ablkcipher_get_flags(parent) &
					 CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(ecb, derived_keys + HEH_BLOCK_SIZE,
				       keylen);
	crypto_ablkcipher_set_flags(parent, crypto_ablkcipher_get_flags(ecb) &
					    CRYPTO_TFM_RES_MASK);
out:
	kzfree(derived_keys);
	return err;
}

static int heh_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct heh_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct heh_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_shash *cmac;
	struct crypto_shash *poly_hash;
	struct crypto_ablkcipher *ecb;
	unsigned int reqsize;
	int err;

	cmac = crypto_spawn_shash(&ictx->cmac);
	if (IS_ERR(cmac))
		return PTR_ERR(cmac);

	poly_hash = crypto_spawn_shash(&ictx->poly_hash);
	err = PTR_ERR(poly_hash);
	if (IS_ERR(poly_hash))
		goto err_free_cmac;

	ecb = crypto_spawn_skcipher(&ictx->ecb);
	err = PTR_ERR(ecb);
	if (IS_ERR(ecb))
		goto err_free_poly_hash;

	ctx->cmac = cmac;
	ctx->poly_hash = poly_hash;
	ctx->ecb = ecb;

	reqsize = crypto_tfm_alg_alignmask(tfm) &
		  ~(crypto_tfm_ctx_alignment() - 1);
	reqsize += max3(offsetof(struct heh_req_ctx, u.cmac.desc) +
			  sizeof(struct shash_desc) +
			  crypto_shash_descsize(cmac),
			offsetof(struct heh_req_ctx, u.poly_hash.desc) +
			  sizeof(struct shash_desc) +
			  crypto_shash_descsize(poly_hash),
			offsetof(struct heh_req_ctx, u.ecb.req) +
			  sizeof(struct ablkcipher_request) +
			  crypto_ablkcipher_reqsize(ecb));
	tfm->crt_ablkcipher.reqsize = reqsize;

	return 0;

err_free_poly_hash:
	crypto_free_shash(poly_hash);
err_free_cmac:
	crypto_free_shash(cmac);
	return err;
}

static void heh_exit_tfm(struct crypto_tfm *tfm)
{
	struct heh_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(ctx->cmac);
	crypto_free_shash(ctx->poly_hash);
	crypto_free_ablkcipher(ctx->ecb);
}

static void heh_free_instance(struct crypto_instance *inst)
{
	struct heh_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_shash(&ctx->cmac);
	crypto_drop_shash(&ctx->poly_hash);
	crypto_drop_skcipher(&ctx->ecb);
	kfree(inst);
}

/*
 * Create an instance of HEH as a ablkcipher.
 *
 * This relies on underlying CMAC and ECB algorithms, usually cmac(aes) and
 * ecb(aes).  For performance reasons we support asynchronous ECB algorithms.
 * However, we do not yet support asynchronous CMAC algorithms because CMAC is
 * only used on a small fixed amount of data per request, independent of the
 * request length.  This would change if AEAD or variable-length nonce support
 * were to be exposed.
 */
static int heh_create_common(struct crypto_template *tmpl, struct rtattr **tb,
			     const char *full_name, const char *cmac_name,
			     const char *poly_hash_name, const char *ecb_name)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct heh_instance_ctx *ctx;
	struct shash_alg *cmac;
	struct shash_alg *poly_hash;
	struct crypto_alg *ecb;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	/* User must be asking for something compatible with ablkcipher */
	if ((algt->type ^ CRYPTO_ALG_TYPE_ABLKCIPHER) & algt->mask)
		return -EINVAL;

	/* Allocate the ablkcipher instance */
	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = crypto_instance_ctx(inst);

	/* Set up the cmac spawn */
	ctx->cmac.base.inst = inst;
	err = crypto_grab_shash(&ctx->cmac, cmac_name, 0, 0);
	if (err)
		goto err_free_inst;
	cmac = crypto_spawn_shash_alg(&ctx->cmac);
	err = -EINVAL;
	if (cmac->digestsize != HEH_BLOCK_SIZE)
		goto err_drop_cmac;

	/* Set up the poly_hash spawn */
	ctx->poly_hash.base.inst = inst;
	err = crypto_grab_shash(&ctx->poly_hash, poly_hash_name, 0, 0);
	if (err)
		goto err_drop_cmac;
	poly_hash = crypto_spawn_shash_alg(&ctx->poly_hash);
	err = -EINVAL;
	if (poly_hash->digestsize != HEH_BLOCK_SIZE)
		goto err_drop_poly_hash;

	/* Set up the ecb spawn */
	ctx->ecb.base.inst = inst;
	err = crypto_grab_skcipher(&ctx->ecb, ecb_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_drop_poly_hash;
	ecb = crypto_skcipher_spawn_alg(&ctx->ecb);

	/* HEH only supports block ciphers with 16 byte block size */
	err = -EINVAL;
	if (ecb->cra_blocksize != HEH_BLOCK_SIZE)
		goto err_drop_ecb;

	/* The underlying "ECB" algorithm must not require an IV */
	err = -EINVAL;
	if ((ecb->cra_flags & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_BLKCIPHER) {
		if (ecb->cra_blkcipher.ivsize != 0)
			goto err_drop_ecb;
	} else {
		if (ecb->cra_ablkcipher.ivsize != 0)
			goto err_drop_ecb;
	}

	/* Set the instance names */
	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "heh_base(%s,%s,%s)", cmac->base.cra_driver_name,
		     poly_hash->base.cra_driver_name,
		     ecb->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_drop_ecb;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "%s", full_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_drop_ecb;

	/* Finish initializing the instance */

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
				(ecb->cra_flags & CRYPTO_ALG_ASYNC);
	inst->alg.cra_blocksize = HEH_BLOCK_SIZE;
	inst->alg.cra_ctxsize = sizeof(struct heh_tfm_ctx);
	inst->alg.cra_alignmask = ecb->cra_alignmask | (__alignof__(be128) - 1);
	inst->alg.cra_priority = ecb->cra_priority;
	inst->alg.cra_type = &crypto_ablkcipher_type;
	inst->alg.cra_init = heh_init_tfm;
	inst->alg.cra_exit = heh_exit_tfm;

	inst->alg.cra_ablkcipher.setkey = heh_setkey;
	inst->alg.cra_ablkcipher.encrypt = heh_encrypt;
	inst->alg.cra_ablkcipher.decrypt = heh_decrypt;
	if ((ecb->cra_flags & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_BLKCIPHER) {
		inst->alg.cra_ablkcipher.min_keysize = ecb->cra_blkcipher.min_keysize;
		inst->alg.cra_ablkcipher.max_keysize = ecb->cra_blkcipher.max_keysize;
	} else {
		inst->alg.cra_ablkcipher.min_keysize = ecb->cra_ablkcipher.min_keysize;
		inst->alg.cra_ablkcipher.max_keysize = ecb->cra_ablkcipher.max_keysize;
	}
	inst->alg.cra_ablkcipher.ivsize = HEH_BLOCK_SIZE;

	/* Register the instance */
	err = crypto_register_instance(tmpl, inst);
	if (err)
		goto err_drop_ecb;
	return 0;

err_drop_ecb:
	crypto_drop_skcipher(&ctx->ecb);
err_drop_poly_hash:
	crypto_drop_shash(&ctx->poly_hash);
err_drop_cmac:
	crypto_drop_shash(&ctx->cmac);
err_free_inst:
	kfree(inst);
	return err;
}

static int heh_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	const char *cipher_name;
	char full_name[CRYPTO_MAX_ALG_NAME];
	char cmac_name[CRYPTO_MAX_ALG_NAME];
	char ecb_name[CRYPTO_MAX_ALG_NAME];

	/* Get the name of the requested block cipher (e.g. aes) */
	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "heh(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	if (snprintf(cmac_name, CRYPTO_MAX_ALG_NAME, "cmac(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	if (snprintf(ecb_name, CRYPTO_MAX_ALG_NAME, "ecb(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	return heh_create_common(tmpl, tb, full_name, cmac_name, "poly_hash",
				 ecb_name);
}

static struct crypto_template heh_tmpl = {
	.name = "heh",
	.create = heh_create,
	.free = heh_free_instance,
	.module = THIS_MODULE,
};

static int heh_base_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	char full_name[CRYPTO_MAX_ALG_NAME];
	const char *cmac_name;
	const char *poly_hash_name;
	const char *ecb_name;

	cmac_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cmac_name))
		return PTR_ERR(cmac_name);

	poly_hash_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(poly_hash_name))
		return PTR_ERR(poly_hash_name);

	ecb_name = crypto_attr_alg_name(tb[3]);
	if (IS_ERR(ecb_name))
		return PTR_ERR(ecb_name);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "heh_base(%s,%s,%s)",
		     cmac_name, poly_hash_name, ecb_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	return heh_create_common(tmpl, tb, full_name, cmac_name, poly_hash_name,
				 ecb_name);
}

/*
 * If HEH is instantiated as "heh_base" instead of "heh", then specific
 * implementations of cmac, poly_hash, and ecb can be specified instead of just
 * the cipher.
 */
static struct crypto_template heh_base_tmpl = {
	.name = "heh_base",
	.create = heh_base_create,
	.free = heh_free_instance,
	.module = THIS_MODULE,
};

static int __init heh_module_init(void)
{
	int err;

	err = crypto_register_template(&heh_tmpl);
	if (err)
		return err;

	err = crypto_register_template(&heh_base_tmpl);
	if (err)
		goto out_undo_heh;

	err = crypto_register_shash(&poly_hash_alg);
	if (err)
		goto out_undo_heh_base;

	return 0;

out_undo_heh_base:
	crypto_unregister_template(&heh_base_tmpl);
out_undo_heh:
	crypto_unregister_template(&heh_tmpl);
	return err;
}

static void __exit heh_module_exit(void)
{
	crypto_unregister_template(&heh_tmpl);
	crypto_unregister_template(&heh_base_tmpl);
	crypto_unregister_shash(&poly_hash_alg);
}

module_init(heh_module_init);
module_exit(heh_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hash-Encrypt-Hash block cipher mode");
MODULE_ALIAS_CRYPTO("heh");
MODULE_ALIAS_CRYPTO("heh_base");
