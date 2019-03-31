/*
 * ChaCha20-Poly1305 AEAD, RFC7539
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/chacha.h>
#include <crypto/poly1305.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "internal.h"

struct chachapoly_instance_ctx {
	struct crypto_skcipher_spawn chacha;
	struct crypto_ahash_spawn poly;
	unsigned int saltlen;
};

struct chachapoly_ctx {
	struct crypto_skcipher *chacha;
	struct crypto_ahash *poly;
	/* key bytes we use for the ChaCha20 IV */
	unsigned int saltlen;
	u8 salt[];
};

struct poly_req {
	/* zero byte padding for AD/ciphertext, as needed */
	u8 pad[POLY1305_BLOCK_SIZE];
	/* tail data with AD/ciphertext lengths */
	struct {
		__le64 assoclen;
		__le64 cryptlen;
	} tail;
	struct scatterlist src[1];
	struct ahash_request req; /* must be last member */
};

struct chacha_req {
	u8 iv[CHACHA_IV_SIZE];
	struct scatterlist src[1];
	struct skcipher_request req; /* must be last member */
};

struct chachapoly_req_ctx {
	struct scatterlist src[2];
	struct scatterlist dst[2];
	/* the key we generate for Poly1305 using Chacha20 */
	u8 key[POLY1305_KEY_SIZE];
	/* calculated Poly1305 tag */
	u8 tag[POLY1305_DIGEST_SIZE];
	/* length of data to en/decrypt, without ICV */
	unsigned int cryptlen;
	/* Actual AD, excluding IV */
	unsigned int assoclen;
	union {
		struct poly_req poly;
		struct chacha_req chacha;
	} u;
};

static inline void async_done_continue(struct aead_request *req, int err,
				       int (*cont)(struct aead_request *))
{
	if (!err)
		err = cont(req);

	if (err != -EINPROGRESS && err != -EBUSY)
		aead_request_complete(req, err);
}

static void chacha_iv(u8 *iv, struct aead_request *req, u32 icb)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	__le32 leicb = cpu_to_le32(icb);

	memcpy(iv, &leicb, sizeof(leicb));
	memcpy(iv + sizeof(leicb), ctx->salt, ctx->saltlen);
	memcpy(iv + sizeof(leicb) + ctx->saltlen, req->iv,
	       CHACHA_IV_SIZE - sizeof(leicb) - ctx->saltlen);
}

static int poly_verify_tag(struct aead_request *req)
{
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	u8 tag[sizeof(rctx->tag)];

	scatterwalk_map_and_copy(tag, req->src,
				 req->assoclen + rctx->cryptlen,
				 sizeof(tag), 0);
	if (crypto_memneq(tag, rctx->tag, sizeof(tag)))
		return -EBADMSG;
	return 0;
}

static int poly_copy_tag(struct aead_request *req)
{
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);

	scatterwalk_map_and_copy(rctx->tag, req->dst,
				 req->assoclen + rctx->cryptlen,
				 sizeof(rctx->tag), 1);
	return 0;
}

static void chacha_decrypt_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_verify_tag);
}

static int chacha_decrypt(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct chacha_req *creq = &rctx->u.chacha;
	struct scatterlist *src, *dst;
	int err;

	if (rctx->cryptlen == 0)
		goto skip;

	chacha_iv(creq->iv, req, 1);

	sg_init_table(rctx->src, 2);
	src = scatterwalk_ffwd(rctx->src, req->src, req->assoclen);
	dst = src;

	if (req->src != req->dst) {
		sg_init_table(rctx->dst, 2);
		dst = scatterwalk_ffwd(rctx->dst, req->dst, req->assoclen);
	}

	skcipher_request_set_callback(&creq->req, aead_request_flags(req),
				      chacha_decrypt_done, req);
	skcipher_request_set_tfm(&creq->req, ctx->chacha);
	skcipher_request_set_crypt(&creq->req, src, dst,
				   rctx->cryptlen, creq->iv);
	err = crypto_skcipher_decrypt(&creq->req);
	if (err)
		return err;

skip:
	return poly_verify_tag(req);
}

static int poly_tail_continue(struct aead_request *req)
{
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);

	if (rctx->cryptlen == req->cryptlen) /* encrypting */
		return poly_copy_tag(req);

	return chacha_decrypt(req);
}

static void poly_tail_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_tail_continue);
}

static int poly_tail(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chachapoly_ctx *ctx = crypto_aead_ctx(tfm);
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	__le64 len;
	int err;

	sg_init_table(preq->src, 1);
	len = cpu_to_le64(rctx->assoclen);
	memcpy(&preq->tail.assoclen, &len, sizeof(len));
	len = cpu_to_le64(rctx->cryptlen);
	memcpy(&preq->tail.cryptlen, &len, sizeof(len));
	sg_set_buf(preq->src, &preq->tail, sizeof(preq->tail));

	ahash_request_set_callback(&preq->req, aead_request_flags(req),
				   poly_tail_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, preq->src,
				rctx->tag, sizeof(preq->tail));

	err = crypto_ahash_finup(&preq->req);
	if (err)
		return err;

	return poly_tail_continue(req);
}

static void poly_cipherpad_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_tail);
}

static int poly_cipherpad(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	unsigned int padlen, bs = POLY1305_BLOCK_SIZE;
	int err;

	padlen = (bs - (rctx->cryptlen % bs)) % bs;
	memset(preq->pad, 0, sizeof(preq->pad));
	sg_init_table(preq->src, 1);
	sg_set_buf(preq->src, &preq->pad, padlen);

	ahash_request_set_callback(&preq->req, aead_request_flags(req),
				   poly_cipherpad_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, preq->src, NULL, padlen);

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_tail(req);
}

static void poly_cipher_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_cipherpad);
}

static int poly_cipher(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	struct scatterlist *crypt = req->src;
	int err;

	if (rctx->cryptlen == req->cryptlen) /* encrypting */
		crypt = req->dst;

	sg_init_table(rctx->src, 2);
	crypt = scatterwalk_ffwd(rctx->src, crypt, req->assoclen);

	ahash_request_set_callback(&preq->req, aead_request_flags(req),
				   poly_cipher_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, crypt, NULL, rctx->cryptlen);

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_cipherpad(req);
}

static void poly_adpad_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_cipher);
}

static int poly_adpad(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	unsigned int padlen, bs = POLY1305_BLOCK_SIZE;
	int err;

	padlen = (bs - (rctx->assoclen % bs)) % bs;
	memset(preq->pad, 0, sizeof(preq->pad));
	sg_init_table(preq->src, 1);
	sg_set_buf(preq->src, preq->pad, padlen);

	ahash_request_set_callback(&preq->req, aead_request_flags(req),
				   poly_adpad_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, preq->src, NULL, padlen);

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_cipher(req);
}

static void poly_ad_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_adpad);
}

static int poly_ad(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	int err;

	ahash_request_set_callback(&preq->req, aead_request_flags(req),
				   poly_ad_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, req->src, NULL, rctx->assoclen);

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_adpad(req);
}

static void poly_setkey_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_ad);
}

static int poly_setkey(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	int err;

	sg_init_table(preq->src, 1);
	sg_set_buf(preq->src, rctx->key, sizeof(rctx->key));

	ahash_request_set_callback(&preq->req, aead_request_flags(req),
				   poly_setkey_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, preq->src, NULL, sizeof(rctx->key));

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_ad(req);
}

static void poly_init_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_setkey);
}

static int poly_init(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	int err;

	ahash_request_set_callback(&preq->req, aead_request_flags(req),
				   poly_init_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);

	err = crypto_ahash_init(&preq->req);
	if (err)
		return err;

	return poly_setkey(req);
}

static void poly_genkey_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_init);
}

static int poly_genkey(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chachapoly_ctx *ctx = crypto_aead_ctx(tfm);
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct chacha_req *creq = &rctx->u.chacha;
	int err;

	rctx->assoclen = req->assoclen;

	if (crypto_aead_ivsize(tfm) == 8) {
		if (rctx->assoclen < 8)
			return -EINVAL;
		rctx->assoclen -= 8;
	}

	sg_init_table(creq->src, 1);
	memset(rctx->key, 0, sizeof(rctx->key));
	sg_set_buf(creq->src, rctx->key, sizeof(rctx->key));

	chacha_iv(creq->iv, req, 0);

	skcipher_request_set_callback(&creq->req, aead_request_flags(req),
				      poly_genkey_done, req);
	skcipher_request_set_tfm(&creq->req, ctx->chacha);
	skcipher_request_set_crypt(&creq->req, creq->src, creq->src,
				   POLY1305_KEY_SIZE, creq->iv);

	err = crypto_skcipher_decrypt(&creq->req);
	if (err)
		return err;

	return poly_init(req);
}

static void chacha_encrypt_done(struct crypto_async_request *areq, int err)
{
	async_done_continue(areq->data, err, poly_genkey);
}

static int chacha_encrypt(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct chacha_req *creq = &rctx->u.chacha;
	struct scatterlist *src, *dst;
	int err;

	if (req->cryptlen == 0)
		goto skip;

	chacha_iv(creq->iv, req, 1);

	sg_init_table(rctx->src, 2);
	src = scatterwalk_ffwd(rctx->src, req->src, req->assoclen);
	dst = src;

	if (req->src != req->dst) {
		sg_init_table(rctx->dst, 2);
		dst = scatterwalk_ffwd(rctx->dst, req->dst, req->assoclen);
	}

	skcipher_request_set_callback(&creq->req, aead_request_flags(req),
				      chacha_encrypt_done, req);
	skcipher_request_set_tfm(&creq->req, ctx->chacha);
	skcipher_request_set_crypt(&creq->req, src, dst,
				   req->cryptlen, creq->iv);
	err = crypto_skcipher_encrypt(&creq->req);
	if (err)
		return err;

skip:
	return poly_genkey(req);
}

static int chachapoly_encrypt(struct aead_request *req)
{
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);

	rctx->cryptlen = req->cryptlen;

	/* encrypt call chain:
	 * - chacha_encrypt/done()
	 * - poly_genkey/done()
	 * - poly_init/done()
	 * - poly_setkey/done()
	 * - poly_ad/done()
	 * - poly_adpad/done()
	 * - poly_cipher/done()
	 * - poly_cipherpad/done()
	 * - poly_tail/done/continue()
	 * - poly_copy_tag()
	 */
	return chacha_encrypt(req);
}

static int chachapoly_decrypt(struct aead_request *req)
{
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);

	rctx->cryptlen = req->cryptlen - POLY1305_DIGEST_SIZE;

	/* decrypt call chain:
	 * - poly_genkey/done()
	 * - poly_init/done()
	 * - poly_setkey/done()
	 * - poly_ad/done()
	 * - poly_adpad/done()
	 * - poly_cipher/done()
	 * - poly_cipherpad/done()
	 * - poly_tail/done/continue()
	 * - chacha_decrypt/done()
	 * - poly_verify_tag()
	 */
	return poly_genkey(req);
}

static int chachapoly_setkey(struct crypto_aead *aead, const u8 *key,
			     unsigned int keylen)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(aead);
	int err;

	if (keylen != ctx->saltlen + CHACHA_KEY_SIZE)
		return -EINVAL;

	keylen -= ctx->saltlen;
	memcpy(ctx->salt, key + keylen, ctx->saltlen);

	crypto_skcipher_clear_flags(ctx->chacha, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ctx->chacha, crypto_aead_get_flags(aead) &
					       CRYPTO_TFM_REQ_MASK);

	err = crypto_skcipher_setkey(ctx->chacha, key, keylen);
	crypto_aead_set_flags(aead, crypto_skcipher_get_flags(ctx->chacha) &
				    CRYPTO_TFM_RES_MASK);
	return err;
}

static int chachapoly_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
	if (authsize != POLY1305_DIGEST_SIZE)
		return -EINVAL;

	return 0;
}

static int chachapoly_init(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct chachapoly_instance_ctx *ictx = aead_instance_ctx(inst);
	struct chachapoly_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_skcipher *chacha;
	struct crypto_ahash *poly;
	unsigned long align;

	poly = crypto_spawn_ahash(&ictx->poly);
	if (IS_ERR(poly))
		return PTR_ERR(poly);

	chacha = crypto_spawn_skcipher(&ictx->chacha);
	if (IS_ERR(chacha)) {
		crypto_free_ahash(poly);
		return PTR_ERR(chacha);
	}

	ctx->chacha = chacha;
	ctx->poly = poly;
	ctx->saltlen = ictx->saltlen;

	align = crypto_aead_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	crypto_aead_set_reqsize(
		tfm,
		align + offsetof(struct chachapoly_req_ctx, u) +
		max(offsetof(struct chacha_req, req) +
		    sizeof(struct skcipher_request) +
		    crypto_skcipher_reqsize(chacha),
		    offsetof(struct poly_req, req) +
		    sizeof(struct ahash_request) +
		    crypto_ahash_reqsize(poly)));

	return 0;
}

static void chachapoly_exit(struct crypto_aead *tfm)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_ahash(ctx->poly);
	crypto_free_skcipher(ctx->chacha);
}

static void chachapoly_free(struct aead_instance *inst)
{
	struct chachapoly_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->chacha);
	crypto_drop_ahash(&ctx->poly);
	kfree(inst);
}

static int chachapoly_create(struct crypto_template *tmpl, struct rtattr **tb,
			     const char *name, unsigned int ivsize)
{
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct skcipher_alg *chacha;
	struct crypto_alg *poly;
	struct hash_alg_common *poly_hash;
	struct chachapoly_instance_ctx *ctx;
	const char *chacha_name, *poly_name;
	int err;

	if (ivsize > CHACHAPOLY_IV_SIZE)
		return -EINVAL;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return -EINVAL;

	chacha_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(chacha_name))
		return PTR_ERR(chacha_name);
	poly_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(poly_name))
		return PTR_ERR(poly_name);

	poly = crypto_find_alg(poly_name, &crypto_ahash_type,
			       CRYPTO_ALG_TYPE_HASH,
			       CRYPTO_ALG_TYPE_AHASH_MASK |
			       crypto_requires_sync(algt->type,
						    algt->mask));
	if (IS_ERR(poly))
		return PTR_ERR(poly);
	poly_hash = __crypto_hash_alg_common(poly);

	err = -EINVAL;
	if (poly_hash->digestsize != POLY1305_DIGEST_SIZE)
		goto out_put_poly;

	err = -ENOMEM;
	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		goto out_put_poly;

	ctx = aead_instance_ctx(inst);
	ctx->saltlen = CHACHAPOLY_IV_SIZE - ivsize;
	err = crypto_init_ahash_spawn(&ctx->poly, poly_hash,
				      aead_crypto_instance(inst));
	if (err)
		goto err_free_inst;

	crypto_set_skcipher_spawn(&ctx->chacha, aead_crypto_instance(inst));
	err = crypto_grab_skcipher(&ctx->chacha, chacha_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_drop_poly;

	chacha = crypto_spawn_skcipher_alg(&ctx->chacha);

	err = -EINVAL;
	/* Need 16-byte IV size, including Initial Block Counter value */
	if (crypto_skcipher_alg_ivsize(chacha) != CHACHA_IV_SIZE)
		goto out_drop_chacha;
	/* Not a stream cipher? */
	if (chacha->base.cra_blocksize != 1)
		goto out_drop_chacha;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s,%s)", name, chacha->base.cra_name,
		     poly->cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_drop_chacha;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s,%s)", name, chacha->base.cra_driver_name,
		     poly->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_drop_chacha;

	inst->alg.base.cra_flags = (chacha->base.cra_flags | poly->cra_flags) &
				   CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_priority = (chacha->base.cra_priority +
				       poly->cra_priority) / 2;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = chacha->base.cra_alignmask |
				       poly->cra_alignmask;
	inst->alg.base.cra_ctxsize = sizeof(struct chachapoly_ctx) +
				     ctx->saltlen;
	inst->alg.ivsize = ivsize;
	inst->alg.chunksize = crypto_skcipher_alg_chunksize(chacha);
	inst->alg.maxauthsize = POLY1305_DIGEST_SIZE;
	inst->alg.init = chachapoly_init;
	inst->alg.exit = chachapoly_exit;
	inst->alg.encrypt = chachapoly_encrypt;
	inst->alg.decrypt = chachapoly_decrypt;
	inst->alg.setkey = chachapoly_setkey;
	inst->alg.setauthsize = chachapoly_setauthsize;

	inst->free = chachapoly_free;

	err = aead_register_instance(tmpl, inst);
	if (err)
		goto out_drop_chacha;

out_put_poly:
	crypto_mod_put(poly);
	return err;

out_drop_chacha:
	crypto_drop_skcipher(&ctx->chacha);
err_drop_poly:
	crypto_drop_ahash(&ctx->poly);
err_free_inst:
	kfree(inst);
	goto out_put_poly;
}

static int rfc7539_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	return chachapoly_create(tmpl, tb, "rfc7539", 12);
}

static int rfc7539esp_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	return chachapoly_create(tmpl, tb, "rfc7539esp", 8);
}

static struct crypto_template rfc7539_tmpls[] = {
	{
		.name = "rfc7539",
		.create = rfc7539_create,
		.module = THIS_MODULE,
	}, {
		.name = "rfc7539esp",
		.create = rfc7539esp_create,
		.module = THIS_MODULE,
	},
};

static int __init chacha20poly1305_module_init(void)
{
	return crypto_register_templates(rfc7539_tmpls,
					 ARRAY_SIZE(rfc7539_tmpls));
}

static void __exit chacha20poly1305_module_exit(void)
{
	crypto_unregister_templates(rfc7539_tmpls,
				    ARRAY_SIZE(rfc7539_tmpls));
}

module_init(chacha20poly1305_module_init);
module_exit(chacha20poly1305_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("ChaCha20-Poly1305 AEAD");
MODULE_ALIAS_CRYPTO("rfc7539");
MODULE_ALIAS_CRYPTO("rfc7539esp");
