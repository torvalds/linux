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
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "internal.h"

#define POLY1305_BLOCK_SIZE	16
#define POLY1305_DIGEST_SIZE	16
#define POLY1305_KEY_SIZE	32
#define CHACHA20_KEY_SIZE	32
#define CHACHA20_IV_SIZE	16
#define CHACHAPOLY_IV_SIZE	12

struct chachapoly_instance_ctx {
	struct crypto_skcipher_spawn chacha;
	struct crypto_ahash_spawn poly;
	unsigned int saltlen;
};

struct chachapoly_ctx {
	struct crypto_ablkcipher *chacha;
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
	u8 iv[CHACHA20_IV_SIZE];
	struct scatterlist src[1];
	struct ablkcipher_request req; /* must be last member */
};

struct chachapoly_req_ctx {
	/* the key we generate for Poly1305 using Chacha20 */
	u8 key[POLY1305_KEY_SIZE];
	/* calculated Poly1305 tag */
	u8 tag[POLY1305_DIGEST_SIZE];
	/* length of data to en/decrypt, without ICV */
	unsigned int cryptlen;
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
	       CHACHA20_IV_SIZE - sizeof(leicb) - ctx->saltlen);
}

static int poly_verify_tag(struct aead_request *req)
{
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	u8 tag[sizeof(rctx->tag)];

	scatterwalk_map_and_copy(tag, req->src, rctx->cryptlen, sizeof(tag), 0);
	if (crypto_memneq(tag, rctx->tag, sizeof(tag)))
		return -EBADMSG;
	return 0;
}

static int poly_copy_tag(struct aead_request *req)
{
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);

	scatterwalk_map_and_copy(rctx->tag, req->dst, rctx->cryptlen,
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
	int err;

	chacha_iv(creq->iv, req, 1);

	ablkcipher_request_set_callback(&creq->req, aead_request_flags(req),
					chacha_decrypt_done, req);
	ablkcipher_request_set_tfm(&creq->req, ctx->chacha);
	ablkcipher_request_set_crypt(&creq->req, req->src, req->dst,
				     rctx->cryptlen, creq->iv);
	err = crypto_ablkcipher_decrypt(&creq->req);
	if (err)
		return err;

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
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	__le64 len;
	int err;

	sg_init_table(preq->src, 1);
	len = cpu_to_le64(req->assoclen);
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

	padlen = (bs - (req->assoclen % bs)) % bs;
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
	ahash_request_set_crypt(&preq->req, req->assoc, NULL, req->assoclen);

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
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct chacha_req *creq = &rctx->u.chacha;
	int err;

	sg_init_table(creq->src, 1);
	memset(rctx->key, 0, sizeof(rctx->key));
	sg_set_buf(creq->src, rctx->key, sizeof(rctx->key));

	chacha_iv(creq->iv, req, 0);

	ablkcipher_request_set_callback(&creq->req, aead_request_flags(req),
					poly_genkey_done, req);
	ablkcipher_request_set_tfm(&creq->req, ctx->chacha);
	ablkcipher_request_set_crypt(&creq->req, creq->src, creq->src,
				     POLY1305_KEY_SIZE, creq->iv);

	err = crypto_ablkcipher_decrypt(&creq->req);
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
	int err;

	chacha_iv(creq->iv, req, 1);

	ablkcipher_request_set_callback(&creq->req, aead_request_flags(req),
					chacha_encrypt_done, req);
	ablkcipher_request_set_tfm(&creq->req, ctx->chacha);
	ablkcipher_request_set_crypt(&creq->req, req->src, req->dst,
				     req->cryptlen, creq->iv);
	err = crypto_ablkcipher_encrypt(&creq->req);
	if (err)
		return err;

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

	if (req->cryptlen < POLY1305_DIGEST_SIZE)
		return -EINVAL;
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

	if (keylen != ctx->saltlen + CHACHA20_KEY_SIZE)
		return -EINVAL;

	keylen -= ctx->saltlen;
	memcpy(ctx->salt, key + keylen, ctx->saltlen);

	crypto_ablkcipher_clear_flags(ctx->chacha, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(ctx->chacha, crypto_aead_get_flags(aead) &
				    CRYPTO_TFM_REQ_MASK);

	err = crypto_ablkcipher_setkey(ctx->chacha, key, keylen);
	crypto_aead_set_flags(aead, crypto_ablkcipher_get_flags(ctx->chacha) &
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

static int chachapoly_init(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct chachapoly_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct chachapoly_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_ablkcipher *chacha;
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

	align = crypto_tfm_alg_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	crypto_aead_set_reqsize(__crypto_aead_cast(tfm),
				align + offsetof(struct chachapoly_req_ctx, u) +
				max(offsetof(struct chacha_req, req) +
				    sizeof(struct ablkcipher_request) +
				    crypto_ablkcipher_reqsize(chacha),
				    offsetof(struct poly_req, req) +
				    sizeof(struct ahash_request) +
				    crypto_ahash_reqsize(poly)));

	return 0;
}

static void chachapoly_exit(struct crypto_tfm *tfm)
{
	struct chachapoly_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_ahash(ctx->poly);
	crypto_free_ablkcipher(ctx->chacha);
}

static struct crypto_instance *chachapoly_alloc(struct rtattr **tb,
						const char *name,
						unsigned int ivsize)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_alg *chacha;
	struct crypto_alg *poly;
	struct ahash_alg *poly_ahash;
	struct chachapoly_instance_ctx *ctx;
	const char *chacha_name, *poly_name;
	int err;

	if (ivsize > CHACHAPOLY_IV_SIZE)
		return ERR_PTR(-EINVAL);

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	chacha_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(chacha_name))
		return ERR_CAST(chacha_name);
	poly_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(poly_name))
		return ERR_CAST(poly_name);

	poly = crypto_find_alg(poly_name, &crypto_ahash_type,
			       CRYPTO_ALG_TYPE_HASH,
			       CRYPTO_ALG_TYPE_AHASH_MASK);
	if (IS_ERR(poly))
		return ERR_CAST(poly);

	err = -ENOMEM;
	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		goto out_put_poly;

	ctx = crypto_instance_ctx(inst);
	ctx->saltlen = CHACHAPOLY_IV_SIZE - ivsize;
	poly_ahash = container_of(poly, struct ahash_alg, halg.base);
	err = crypto_init_ahash_spawn(&ctx->poly, &poly_ahash->halg, inst);
	if (err)
		goto err_free_inst;

	crypto_set_skcipher_spawn(&ctx->chacha, inst);
	err = crypto_grab_skcipher(&ctx->chacha, chacha_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_drop_poly;

	chacha = crypto_skcipher_spawn_alg(&ctx->chacha);

	err = -EINVAL;
	/* Need 16-byte IV size, including Initial Block Counter value */
	if (chacha->cra_ablkcipher.ivsize != CHACHA20_IV_SIZE)
		goto out_drop_chacha;
	/* Not a stream cipher? */
	if (chacha->cra_blocksize != 1)
		goto out_drop_chacha;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s,%s)", name, chacha_name,
		     poly_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_drop_chacha;
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s,%s)", name, chacha->cra_driver_name,
		     poly->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto out_drop_chacha;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD;
	inst->alg.cra_flags |= (chacha->cra_flags |
				poly->cra_flags) & CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = (chacha->cra_priority +
				  poly->cra_priority) / 2;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = chacha->cra_alignmask | poly->cra_alignmask;
	inst->alg.cra_type = &crypto_nivaead_type;
	inst->alg.cra_aead.ivsize = ivsize;
	inst->alg.cra_aead.maxauthsize = POLY1305_DIGEST_SIZE;
	inst->alg.cra_ctxsize = sizeof(struct chachapoly_ctx) + ctx->saltlen;
	inst->alg.cra_init = chachapoly_init;
	inst->alg.cra_exit = chachapoly_exit;
	inst->alg.cra_aead.encrypt = chachapoly_encrypt;
	inst->alg.cra_aead.decrypt = chachapoly_decrypt;
	inst->alg.cra_aead.setkey = chachapoly_setkey;
	inst->alg.cra_aead.setauthsize = chachapoly_setauthsize;
	inst->alg.cra_aead.geniv = "seqiv";

out:
	crypto_mod_put(poly);
	return inst;

out_drop_chacha:
	crypto_drop_skcipher(&ctx->chacha);
err_drop_poly:
	crypto_drop_ahash(&ctx->poly);
err_free_inst:
	kfree(inst);
out_put_poly:
	inst = ERR_PTR(err);
	goto out;
}

static struct crypto_instance *rfc7539_alloc(struct rtattr **tb)
{
	return chachapoly_alloc(tb, "rfc7539", 12);
}

static struct crypto_instance *rfc7539esp_alloc(struct rtattr **tb)
{
	return chachapoly_alloc(tb, "rfc7539esp", 8);
}

static void chachapoly_free(struct crypto_instance *inst)
{
	struct chachapoly_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->chacha);
	crypto_drop_ahash(&ctx->poly);
	kfree(inst);
}

static struct crypto_template rfc7539_tmpl = {
	.name = "rfc7539",
	.alloc = rfc7539_alloc,
	.free = chachapoly_free,
	.module = THIS_MODULE,
};

static struct crypto_template rfc7539esp_tmpl = {
	.name = "rfc7539esp",
	.alloc = rfc7539esp_alloc,
	.free = chachapoly_free,
	.module = THIS_MODULE,
};

static int __init chacha20poly1305_module_init(void)
{
	int err;

	err = crypto_register_template(&rfc7539_tmpl);
	if (err)
		return err;

	err = crypto_register_template(&rfc7539esp_tmpl);
	if (err)
		crypto_unregister_template(&rfc7539_tmpl);

	return err;
}

static void __exit chacha20poly1305_module_exit(void)
{
	crypto_unregister_template(&rfc7539esp_tmpl);
	crypto_unregister_template(&rfc7539_tmpl);
}

module_init(chacha20poly1305_module_init);
module_exit(chacha20poly1305_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("ChaCha20-Poly1305 AEAD");
MODULE_ALIAS_CRYPTO("chacha20poly1305");
MODULE_ALIAS_CRYPTO("rfc7539");
MODULE_ALIAS_CRYPTO("rfc7539esp");
