/*
 * authencesn.c - AEAD wrapper for IPsec with extended sequence numbers,
 *                 derived from authenc.c
 *
 * Copyright (C) 2010 secunet Security Networks AG
 * Copyright (C) 2010 Steffen Klassert <steffen.klassert@secunet.com>
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/authenc.h>
#include <crypto/null.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct authenc_esn_instance_ctx {
	struct crypto_ahash_spawn auth;
	struct crypto_skcipher_spawn enc;
};

struct crypto_authenc_esn_ctx {
	unsigned int reqoff;
	struct crypto_ahash *auth;
	struct crypto_skcipher *enc;
	struct crypto_skcipher *null;
};

struct authenc_esn_request_ctx {
	struct scatterlist src[2];
	struct scatterlist dst[2];
	char tail[];
};

static void authenc_esn_request_complete(struct aead_request *req, int err)
{
	if (err != -EINPROGRESS)
		aead_request_complete(req, err);
}

static int crypto_authenc_esn_setauthsize(struct crypto_aead *authenc_esn,
					  unsigned int authsize)
{
	if (authsize > 0 && authsize < 4)
		return -EINVAL;

	return 0;
}

static int crypto_authenc_esn_setkey(struct crypto_aead *authenc_esn, const u8 *key,
				     unsigned int keylen)
{
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(authenc_esn);
	struct crypto_ahash *auth = ctx->auth;
	struct crypto_skcipher *enc = ctx->enc;
	struct crypto_authenc_keys keys;
	int err = -EINVAL;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto badkey;

	crypto_ahash_clear_flags(auth, CRYPTO_TFM_REQ_MASK);
	crypto_ahash_set_flags(auth, crypto_aead_get_flags(authenc_esn) &
				     CRYPTO_TFM_REQ_MASK);
	err = crypto_ahash_setkey(auth, keys.authkey, keys.authkeylen);
	crypto_aead_set_flags(authenc_esn, crypto_ahash_get_flags(auth) &
					   CRYPTO_TFM_RES_MASK);

	if (err)
		goto out;

	crypto_skcipher_clear_flags(enc, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(enc, crypto_aead_get_flags(authenc_esn) &
					 CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(enc, keys.enckey, keys.enckeylen);
	crypto_aead_set_flags(authenc_esn, crypto_skcipher_get_flags(enc) &
					   CRYPTO_TFM_RES_MASK);

out:
	return err;

badkey:
	crypto_aead_set_flags(authenc_esn, CRYPTO_TFM_RES_BAD_KEY_LEN);
	goto out;
}

static int crypto_authenc_esn_genicv_tail(struct aead_request *req,
					  unsigned int flags)
{
	struct crypto_aead *authenc_esn = crypto_aead_reqtfm(req);
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(authenc_esn);
	struct authenc_esn_request_ctx *areq_ctx = aead_request_ctx(req);
	struct crypto_ahash *auth = ctx->auth;
	u8 *hash = PTR_ALIGN((u8 *)areq_ctx->tail,
			     crypto_ahash_alignmask(auth) + 1);
	unsigned int authsize = crypto_aead_authsize(authenc_esn);
	unsigned int assoclen = req->assoclen;
	unsigned int cryptlen = req->cryptlen;
	struct scatterlist *dst = req->dst;
	u32 tmp[2];

	/* Move high-order bits of sequence number back. */
	scatterwalk_map_and_copy(tmp, dst, 4, 4, 0);
	scatterwalk_map_and_copy(tmp + 1, dst, assoclen + cryptlen, 4, 0);
	scatterwalk_map_and_copy(tmp, dst, 0, 8, 1);

	scatterwalk_map_and_copy(hash, dst, assoclen + cryptlen, authsize, 1);
	return 0;
}

static void authenc_esn_geniv_ahash_done(struct crypto_async_request *areq,
					 int err)
{
	struct aead_request *req = areq->data;

	err = err ?: crypto_authenc_esn_genicv_tail(req, 0);
	aead_request_complete(req, err);
}

static int crypto_authenc_esn_genicv(struct aead_request *req,
				     unsigned int flags)
{
	struct crypto_aead *authenc_esn = crypto_aead_reqtfm(req);
	struct authenc_esn_request_ctx *areq_ctx = aead_request_ctx(req);
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(authenc_esn);
	struct crypto_ahash *auth = ctx->auth;
	u8 *hash = PTR_ALIGN((u8 *)areq_ctx->tail,
			     crypto_ahash_alignmask(auth) + 1);
	struct ahash_request *ahreq = (void *)(areq_ctx->tail + ctx->reqoff);
	unsigned int authsize = crypto_aead_authsize(authenc_esn);
	unsigned int assoclen = req->assoclen;
	unsigned int cryptlen = req->cryptlen;
	struct scatterlist *dst = req->dst;
	u32 tmp[2];

	if (!authsize)
		return 0;

	/* Move high-order bits of sequence number to the end. */
	scatterwalk_map_and_copy(tmp, dst, 0, 8, 0);
	scatterwalk_map_and_copy(tmp, dst, 4, 4, 1);
	scatterwalk_map_and_copy(tmp + 1, dst, assoclen + cryptlen, 4, 1);

	sg_init_table(areq_ctx->dst, 2);
	dst = scatterwalk_ffwd(areq_ctx->dst, dst, 4);

	ahash_request_set_tfm(ahreq, auth);
	ahash_request_set_crypt(ahreq, dst, hash, assoclen + cryptlen);
	ahash_request_set_callback(ahreq, flags,
				   authenc_esn_geniv_ahash_done, req);

	return crypto_ahash_digest(ahreq) ?:
	       crypto_authenc_esn_genicv_tail(req, aead_request_flags(req));
}


static void crypto_authenc_esn_encrypt_done(struct crypto_async_request *req,
					    int err)
{
	struct aead_request *areq = req->data;

	if (!err)
		err = crypto_authenc_esn_genicv(areq, 0);

	authenc_esn_request_complete(areq, err);
}

static int crypto_authenc_esn_copy(struct aead_request *req, unsigned int len)
{
	struct crypto_aead *authenc_esn = crypto_aead_reqtfm(req);
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(authenc_esn);
	SKCIPHER_REQUEST_ON_STACK(skreq, ctx->null);

	skcipher_request_set_tfm(skreq, ctx->null);
	skcipher_request_set_callback(skreq, aead_request_flags(req),
				      NULL, NULL);
	skcipher_request_set_crypt(skreq, req->src, req->dst, len, NULL);

	return crypto_skcipher_encrypt(skreq);
}

static int crypto_authenc_esn_encrypt(struct aead_request *req)
{
	struct crypto_aead *authenc_esn = crypto_aead_reqtfm(req);
	struct authenc_esn_request_ctx *areq_ctx = aead_request_ctx(req);
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(authenc_esn);
	struct skcipher_request *skreq = (void *)(areq_ctx->tail +
						  ctx->reqoff);
	struct crypto_skcipher *enc = ctx->enc;
	unsigned int assoclen = req->assoclen;
	unsigned int cryptlen = req->cryptlen;
	struct scatterlist *src, *dst;
	int err;

	sg_init_table(areq_ctx->src, 2);
	src = scatterwalk_ffwd(areq_ctx->src, req->src, assoclen);
	dst = src;

	if (req->src != req->dst) {
		err = crypto_authenc_esn_copy(req, assoclen);
		if (err)
			return err;

		sg_init_table(areq_ctx->dst, 2);
		dst = scatterwalk_ffwd(areq_ctx->dst, req->dst, assoclen);
	}

	skcipher_request_set_tfm(skreq, enc);
	skcipher_request_set_callback(skreq, aead_request_flags(req),
				      crypto_authenc_esn_encrypt_done, req);
	skcipher_request_set_crypt(skreq, src, dst, cryptlen, req->iv);

	err = crypto_skcipher_encrypt(skreq);
	if (err)
		return err;

	return crypto_authenc_esn_genicv(req, aead_request_flags(req));
}

static int crypto_authenc_esn_decrypt_tail(struct aead_request *req,
					   unsigned int flags)
{
	struct crypto_aead *authenc_esn = crypto_aead_reqtfm(req);
	unsigned int authsize = crypto_aead_authsize(authenc_esn);
	struct authenc_esn_request_ctx *areq_ctx = aead_request_ctx(req);
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(authenc_esn);
	struct skcipher_request *skreq = (void *)(areq_ctx->tail +
						  ctx->reqoff);
	struct crypto_ahash *auth = ctx->auth;
	u8 *ohash = PTR_ALIGN((u8 *)areq_ctx->tail,
			      crypto_ahash_alignmask(auth) + 1);
	unsigned int cryptlen = req->cryptlen - authsize;
	unsigned int assoclen = req->assoclen;
	struct scatterlist *dst = req->dst;
	u8 *ihash = ohash + crypto_ahash_digestsize(auth);
	u32 tmp[2];

	/* Move high-order bits of sequence number back. */
	scatterwalk_map_and_copy(tmp, dst, 4, 4, 0);
	scatterwalk_map_and_copy(tmp + 1, dst, assoclen + cryptlen, 4, 0);
	scatterwalk_map_and_copy(tmp, dst, 0, 8, 1);

	if (crypto_memneq(ihash, ohash, authsize))
		return -EBADMSG;

	sg_init_table(areq_ctx->dst, 2);
	dst = scatterwalk_ffwd(areq_ctx->dst, dst, assoclen);

	skcipher_request_set_tfm(skreq, ctx->enc);
	skcipher_request_set_callback(skreq, flags,
				      req->base.complete, req->base.data);
	skcipher_request_set_crypt(skreq, dst, dst, cryptlen, req->iv);

	return crypto_skcipher_decrypt(skreq);
}

static void authenc_esn_verify_ahash_done(struct crypto_async_request *areq,
					  int err)
{
	struct aead_request *req = areq->data;

	err = err ?: crypto_authenc_esn_decrypt_tail(req, 0);
	aead_request_complete(req, err);
}

static int crypto_authenc_esn_decrypt(struct aead_request *req)
{
	struct crypto_aead *authenc_esn = crypto_aead_reqtfm(req);
	struct authenc_esn_request_ctx *areq_ctx = aead_request_ctx(req);
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(authenc_esn);
	struct ahash_request *ahreq = (void *)(areq_ctx->tail + ctx->reqoff);
	unsigned int authsize = crypto_aead_authsize(authenc_esn);
	struct crypto_ahash *auth = ctx->auth;
	u8 *ohash = PTR_ALIGN((u8 *)areq_ctx->tail,
			      crypto_ahash_alignmask(auth) + 1);
	unsigned int assoclen = req->assoclen;
	unsigned int cryptlen = req->cryptlen;
	u8 *ihash = ohash + crypto_ahash_digestsize(auth);
	struct scatterlist *dst = req->dst;
	u32 tmp[2];
	int err;

	cryptlen -= authsize;

	if (req->src != dst) {
		err = crypto_authenc_esn_copy(req, assoclen + cryptlen);
		if (err)
			return err;
	}

	scatterwalk_map_and_copy(ihash, req->src, assoclen + cryptlen,
				 authsize, 0);

	if (!authsize)
		goto tail;

	/* Move high-order bits of sequence number to the end. */
	scatterwalk_map_and_copy(tmp, dst, 0, 8, 0);
	scatterwalk_map_and_copy(tmp, dst, 4, 4, 1);
	scatterwalk_map_and_copy(tmp + 1, dst, assoclen + cryptlen, 4, 1);

	sg_init_table(areq_ctx->dst, 2);
	dst = scatterwalk_ffwd(areq_ctx->dst, dst, 4);

	ahash_request_set_tfm(ahreq, auth);
	ahash_request_set_crypt(ahreq, dst, ohash, assoclen + cryptlen);
	ahash_request_set_callback(ahreq, aead_request_flags(req),
				   authenc_esn_verify_ahash_done, req);

	err = crypto_ahash_digest(ahreq);
	if (err)
		return err;

tail:
	return crypto_authenc_esn_decrypt_tail(req, aead_request_flags(req));
}

static int crypto_authenc_esn_init_tfm(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct authenc_esn_instance_ctx *ictx = aead_instance_ctx(inst);
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_ahash *auth;
	struct crypto_skcipher *enc;
	struct crypto_skcipher *null;
	int err;

	auth = crypto_spawn_ahash(&ictx->auth);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	enc = crypto_spawn_skcipher2(&ictx->enc);
	err = PTR_ERR(enc);
	if (IS_ERR(enc))
		goto err_free_ahash;

	null = crypto_get_default_null_skcipher2();
	err = PTR_ERR(null);
	if (IS_ERR(null))
		goto err_free_skcipher;

	ctx->auth = auth;
	ctx->enc = enc;
	ctx->null = null;

	ctx->reqoff = ALIGN(2 * crypto_ahash_digestsize(auth),
			    crypto_ahash_alignmask(auth) + 1);

	crypto_aead_set_reqsize(
		tfm,
		sizeof(struct authenc_esn_request_ctx) +
		ctx->reqoff +
		max_t(unsigned int,
		      crypto_ahash_reqsize(auth) +
		      sizeof(struct ahash_request),
		      sizeof(struct skcipher_request) +
		      crypto_skcipher_reqsize(enc)));

	return 0;

err_free_skcipher:
	crypto_free_skcipher(enc);
err_free_ahash:
	crypto_free_ahash(auth);
	return err;
}

static void crypto_authenc_esn_exit_tfm(struct crypto_aead *tfm)
{
	struct crypto_authenc_esn_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_ahash(ctx->auth);
	crypto_free_skcipher(ctx->enc);
	crypto_put_default_null_skcipher2();
}

static void crypto_authenc_esn_free(struct aead_instance *inst)
{
	struct authenc_esn_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->enc);
	crypto_drop_ahash(&ctx->auth);
	kfree(inst);
}

static int crypto_authenc_esn_create(struct crypto_template *tmpl,
				     struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct hash_alg_common *auth;
	struct crypto_alg *auth_base;
	struct skcipher_alg *enc;
	struct authenc_esn_instance_ctx *ctx;
	const char *enc_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return -EINVAL;

	auth = ahash_attr_alg(tb[1], CRYPTO_ALG_TYPE_HASH,
			      CRYPTO_ALG_TYPE_AHASH_MASK |
			      crypto_requires_sync(algt->type, algt->mask));
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	auth_base = &auth->base;

	enc_name = crypto_attr_alg_name(tb[2]);
	err = PTR_ERR(enc_name);
	if (IS_ERR(enc_name))
		goto out_put_auth;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	err = -ENOMEM;
	if (!inst)
		goto out_put_auth;

	ctx = aead_instance_ctx(inst);

	err = crypto_init_ahash_spawn(&ctx->auth, auth,
				      aead_crypto_instance(inst));
	if (err)
		goto err_free_inst;

	crypto_set_skcipher_spawn(&ctx->enc, aead_crypto_instance(inst));
	err = crypto_grab_skcipher2(&ctx->enc, enc_name, 0,
				    crypto_requires_sync(algt->type,
							 algt->mask));
	if (err)
		goto err_drop_auth;

	enc = crypto_spawn_skcipher_alg(&ctx->enc);

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "authencesn(%s,%s)", auth_base->cra_name,
		     enc->base.cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_drop_enc;

	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "authencesn(%s,%s)", auth_base->cra_driver_name,
		     enc->base.cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_drop_enc;

	inst->alg.base.cra_flags = (auth_base->cra_flags |
				    enc->base.cra_flags) & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_priority = enc->base.cra_priority * 10 +
				      auth_base->cra_priority;
	inst->alg.base.cra_blocksize = enc->base.cra_blocksize;
	inst->alg.base.cra_alignmask = auth_base->cra_alignmask |
				       enc->base.cra_alignmask;
	inst->alg.base.cra_ctxsize = sizeof(struct crypto_authenc_esn_ctx);

	inst->alg.ivsize = crypto_skcipher_alg_ivsize(enc);
	inst->alg.chunksize = crypto_skcipher_alg_chunksize(enc);
	inst->alg.maxauthsize = auth->digestsize;

	inst->alg.init = crypto_authenc_esn_init_tfm;
	inst->alg.exit = crypto_authenc_esn_exit_tfm;

	inst->alg.setkey = crypto_authenc_esn_setkey;
	inst->alg.setauthsize = crypto_authenc_esn_setauthsize;
	inst->alg.encrypt = crypto_authenc_esn_encrypt;
	inst->alg.decrypt = crypto_authenc_esn_decrypt;

	inst->free = crypto_authenc_esn_free,

	err = aead_register_instance(tmpl, inst);
	if (err)
		goto err_drop_enc;

out:
	crypto_mod_put(auth_base);
	return err;

err_drop_enc:
	crypto_drop_skcipher(&ctx->enc);
err_drop_auth:
	crypto_drop_ahash(&ctx->auth);
err_free_inst:
	kfree(inst);
out_put_auth:
	goto out;
}

static struct crypto_template crypto_authenc_esn_tmpl = {
	.name = "authencesn",
	.create = crypto_authenc_esn_create,
	.module = THIS_MODULE,
};

static int __init crypto_authenc_esn_module_init(void)
{
	return crypto_register_template(&crypto_authenc_esn_tmpl);
}

static void __exit crypto_authenc_esn_module_exit(void)
{
	crypto_unregister_template(&crypto_authenc_esn_tmpl);
}

module_init(crypto_authenc_esn_module_init);
module_exit(crypto_authenc_esn_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steffen Klassert <steffen.klassert@secunet.com>");
MODULE_DESCRIPTION("AEAD wrapper for IPsec with extended sequence numbers");
MODULE_ALIAS_CRYPTO("authencesn");
