// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ChaCha20-Poly1305 AEAD, RFC7539
 *
 * Copyright (C) 2015 Martin Willi
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
	/* request flags, with MAY_SLEEP cleared if needed */
	u32 flags;
	union {
		struct poly_req poly;
		struct chacha_req chacha;
	} u;
};

static inline void async_done_continue(struct aead_request *req, int err,
				       int (*cont)(struct aead_request *))
{
	if (!err) {
		struct chachapoly_req_ctx *rctx = aead_request_ctx(req);

		rctx->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
		err = cont(req);
	}

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

static void chacha_decrypt_done(void *data, int err)
{
	async_done_continue(data, err, poly_verify_tag);
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

	src = scatterwalk_ffwd(rctx->src, req->src, req->assoclen);
	dst = src;
	if (req->src != req->dst)
		dst = scatterwalk_ffwd(rctx->dst, req->dst, req->assoclen);

	skcipher_request_set_callback(&creq->req, rctx->flags,
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

static void poly_tail_done(void *data, int err)
{
	async_done_continue(data, err, poly_tail_continue);
}

static int poly_tail(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct chachapoly_ctx *ctx = crypto_aead_ctx(tfm);
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	int err;

	preq->tail.assoclen = cpu_to_le64(rctx->assoclen);
	preq->tail.cryptlen = cpu_to_le64(rctx->cryptlen);
	sg_init_one(preq->src, &preq->tail, sizeof(preq->tail));

	ahash_request_set_callback(&preq->req, rctx->flags,
				   poly_tail_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, preq->src,
				rctx->tag, sizeof(preq->tail));

	err = crypto_ahash_finup(&preq->req);
	if (err)
		return err;

	return poly_tail_continue(req);
}

static void poly_cipherpad_done(void *data, int err)
{
	async_done_continue(data, err, poly_tail);
}

static int poly_cipherpad(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	unsigned int padlen;
	int err;

	padlen = -rctx->cryptlen % POLY1305_BLOCK_SIZE;
	memset(preq->pad, 0, sizeof(preq->pad));
	sg_init_one(preq->src, preq->pad, padlen);

	ahash_request_set_callback(&preq->req, rctx->flags,
				   poly_cipherpad_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, preq->src, NULL, padlen);

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_tail(req);
}

static void poly_cipher_done(void *data, int err)
{
	async_done_continue(data, err, poly_cipherpad);
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

	crypt = scatterwalk_ffwd(rctx->src, crypt, req->assoclen);

	ahash_request_set_callback(&preq->req, rctx->flags,
				   poly_cipher_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, crypt, NULL, rctx->cryptlen);

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_cipherpad(req);
}

static void poly_adpad_done(void *data, int err)
{
	async_done_continue(data, err, poly_cipher);
}

static int poly_adpad(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	unsigned int padlen;
	int err;

	padlen = -rctx->assoclen % POLY1305_BLOCK_SIZE;
	memset(preq->pad, 0, sizeof(preq->pad));
	sg_init_one(preq->src, preq->pad, padlen);

	ahash_request_set_callback(&preq->req, rctx->flags,
				   poly_adpad_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, preq->src, NULL, padlen);

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_cipher(req);
}

static void poly_ad_done(void *data, int err)
{
	async_done_continue(data, err, poly_adpad);
}

static int poly_ad(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	int err;

	ahash_request_set_callback(&preq->req, rctx->flags,
				   poly_ad_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, req->src, NULL, rctx->assoclen);

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_adpad(req);
}

static void poly_setkey_done(void *data, int err)
{
	async_done_continue(data, err, poly_ad);
}

static int poly_setkey(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	int err;

	sg_init_one(preq->src, rctx->key, sizeof(rctx->key));

	ahash_request_set_callback(&preq->req, rctx->flags,
				   poly_setkey_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);
	ahash_request_set_crypt(&preq->req, preq->src, NULL, sizeof(rctx->key));

	err = crypto_ahash_update(&preq->req);
	if (err)
		return err;

	return poly_ad(req);
}

static void poly_init_done(void *data, int err)
{
	async_done_continue(data, err, poly_setkey);
}

static int poly_init(struct aead_request *req)
{
	struct chachapoly_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct chachapoly_req_ctx *rctx = aead_request_ctx(req);
	struct poly_req *preq = &rctx->u.poly;
	int err;

	ahash_request_set_callback(&preq->req, rctx->flags,
				   poly_init_done, req);
	ahash_request_set_tfm(&preq->req, ctx->poly);

	err = crypto_ahash_init(&preq->req);
	if (err)
		return err;

	return poly_setkey(req);
}

static void poly_genkey_done(void *data, int err)
{
	async_done_continue(data, err, poly_init);
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

	memset(rctx->key, 0, sizeof(rctx->key));
	sg_init_one(creq->src, rctx->key, sizeof(rctx->key));

	chacha_iv(creq->iv, req, 0);

	skcipher_request_set_callback(&creq->req, rctx->flags,
				      poly_genkey_done, req);
	skcipher_request_set_tfm(&creq->req, ctx->chacha);
	skcipher_request_set_crypt(&creq->req, creq->src, creq->src,
				   POLY1305_KEY_SIZE, creq->iv);

	err = crypto_skcipher_decrypt(&creq->req);
	if (err)
		return err;

	return poly_init(req);
}

static void chacha_encrypt_done(void *data, int err)
{
	async_done_continue(data, err, poly_genkey);
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

	src = scatterwalk_ffwd(rctx->src, req->src, req->assoclen);
	dst = src;
	if (req->src != req->dst)
		dst = scatterwalk_ffwd(rctx->dst, req->dst, req->assoclen);

	skcipher_request_set_callback(&creq->req, rctx->flags,
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
	rctx->flags = aead_request_flags(req);

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
	rctx->flags = aead_request_flags(req);

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

	if (keylen != ctx->saltlen + CHACHA_KEY_SIZE)
		return -EINVAL;

	keylen -= ctx->saltlen;
	memcpy(ctx->salt, key + keylen, ctx->saltlen);

	crypto_skcipher_clear_flags(ctx->chacha, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ctx->chacha, crypto_aead_get_flags(aead) &
					       CRYPTO_TFM_REQ_MASK);
	return crypto_skcipher_setkey(ctx->chacha, key, keylen);
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
	u32 mask;
	struct aead_instance *inst;
	struct chachapoly_instance_ctx *ctx;
	struct skcipher_alg_common *chacha;
	struct hash_alg_common *poly;
	int err;

	if (ivsize > CHACHAPOLY_IV_SIZE)
		return -EINVAL;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AEAD, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	ctx = aead_instance_ctx(inst);
	ctx->saltlen = CHACHAPOLY_IV_SIZE - ivsize;

	err = crypto_grab_skcipher(&ctx->chacha, aead_crypto_instance(inst),
				   crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;
	chacha = crypto_spawn_skcipher_alg_common(&ctx->chacha);

	err = crypto_grab_ahash(&ctx->poly, aead_crypto_instance(inst),
				crypto_attr_alg_name(tb[2]), 0, mask);
	if (err)
		goto err_free_inst;
	poly = crypto_spawn_ahash_alg(&ctx->poly);

	err = -EINVAL;
	if (poly->digestsize != POLY1305_DIGEST_SIZE)
		goto err_free_inst;
	/* Need 16-byte IV size, including Initial Block Counter value */
	if (chacha->ivsize != CHACHA_IV_SIZE)
		goto err_free_inst;
	/* Not a stream cipher? */
	if (chacha->base.cra_blocksize != 1)
		goto err_free_inst;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s,%s)", name, chacha->base.cra_name,
		     poly->base.cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "%s(%s,%s)", name, chacha->base.cra_driver_name,
		     poly->base.cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_priority = (chacha->base.cra_priority +
				       poly->base.cra_priority) / 2;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = chacha->base.cra_alignmask |
				       poly->base.cra_alignmask;
	inst->alg.base.cra_ctxsize = sizeof(struct chachapoly_ctx) +
				     ctx->saltlen;
	inst->alg.ivsize = ivsize;
	inst->alg.chunksize = chacha->chunksize;
	inst->alg.maxauthsize = POLY1305_DIGEST_SIZE;
	inst->alg.init = chachapoly_init;
	inst->alg.exit = chachapoly_exit;
	inst->alg.encrypt = chachapoly_encrypt;
	inst->alg.decrypt = chachapoly_decrypt;
	inst->alg.setkey = chachapoly_setkey;
	inst->alg.setauthsize = chachapoly_setauthsize;

	inst->free = chachapoly_free;

	err = aead_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		chachapoly_free(inst);
	}
	return err;
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

subsys_initcall(chacha20poly1305_module_init);
module_exit(chacha20poly1305_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("ChaCha20-Poly1305 AEAD");
MODULE_ALIAS_CRYPTO("rfc7539");
MODULE_ALIAS_CRYPTO("rfc7539esp");
