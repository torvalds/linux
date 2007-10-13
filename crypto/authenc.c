/*
 * Authenc: Simple AEAD wrapper for IPsec
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "scatterwalk.h"

struct authenc_instance_ctx {
	struct crypto_spawn auth;
	struct crypto_spawn enc;

	unsigned int authsize;
	unsigned int enckeylen;
};

struct crypto_authenc_ctx {
	spinlock_t auth_lock;
	struct crypto_hash *auth;
	struct crypto_ablkcipher *enc;
};

static int crypto_authenc_setkey(struct crypto_aead *authenc, const u8 *key,
				 unsigned int keylen)
{
	struct authenc_instance_ctx *ictx =
		crypto_instance_ctx(crypto_aead_alg_instance(authenc));
	unsigned int enckeylen = ictx->enckeylen;
	unsigned int authkeylen;
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct crypto_hash *auth = ctx->auth;
	struct crypto_ablkcipher *enc = ctx->enc;
	int err = -EINVAL;

	if (keylen < enckeylen) {
		crypto_aead_set_flags(authenc, CRYPTO_TFM_RES_BAD_KEY_LEN);
		goto out;
	}
	authkeylen = keylen - enckeylen;

	crypto_hash_clear_flags(auth, CRYPTO_TFM_REQ_MASK);
	crypto_hash_set_flags(auth, crypto_aead_get_flags(authenc) &
				    CRYPTO_TFM_REQ_MASK);
	err = crypto_hash_setkey(auth, key, authkeylen);
	crypto_aead_set_flags(authenc, crypto_hash_get_flags(auth) &
				       CRYPTO_TFM_RES_MASK);

	if (err)
		goto out;

	crypto_ablkcipher_clear_flags(enc, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(enc, crypto_aead_get_flags(authenc) &
					 CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(enc, key + authkeylen, enckeylen);
	crypto_aead_set_flags(authenc, crypto_ablkcipher_get_flags(enc) &
				       CRYPTO_TFM_RES_MASK);

out:
	return err;
}

static int crypto_authenc_hash(struct aead_request *req)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct authenc_instance_ctx *ictx =
		crypto_instance_ctx(crypto_aead_alg_instance(authenc));
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct crypto_hash *auth = ctx->auth;
	struct hash_desc desc = {
		.tfm = auth,
	};
	u8 *hash = aead_request_ctx(req);
	struct scatterlist *dst;
	unsigned int cryptlen;
	int err;

	hash = (u8 *)ALIGN((unsigned long)hash + crypto_hash_alignmask(auth), 
			   crypto_hash_alignmask(auth) + 1);

	spin_lock_bh(&ctx->auth_lock);
	err = crypto_hash_init(&desc);
	if (err)
		goto auth_unlock;

	err = crypto_hash_update(&desc, req->assoc, req->assoclen);
	if (err)
		goto auth_unlock;

	cryptlen = req->cryptlen;
	dst = req->dst;
	err = crypto_hash_update(&desc, dst, cryptlen);
	if (err)
		goto auth_unlock;

	err = crypto_hash_final(&desc, hash);
auth_unlock:
	spin_unlock_bh(&ctx->auth_lock);

	if (err)
		return err;

	scatterwalk_map_and_copy(hash, dst, cryptlen, ictx->authsize, 1);
	return 0;
}

static void crypto_authenc_encrypt_done(struct crypto_async_request *req,
					int err)
{
	if (!err)
		err = crypto_authenc_hash(req->data);

	aead_request_complete(req->data, err);
}

static int crypto_authenc_encrypt(struct aead_request *req)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct ablkcipher_request *abreq = aead_request_ctx(req);
	int err;

	ablkcipher_request_set_tfm(abreq, ctx->enc);
	ablkcipher_request_set_callback(abreq, aead_request_flags(req),
					crypto_authenc_encrypt_done, req);
	ablkcipher_request_set_crypt(abreq, req->src, req->dst, req->cryptlen,
				     req->iv);

	err = crypto_ablkcipher_encrypt(abreq);
	if (err)
		return err;

	return crypto_authenc_hash(req);
}

static int crypto_authenc_verify(struct aead_request *req)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct authenc_instance_ctx *ictx =
		crypto_instance_ctx(crypto_aead_alg_instance(authenc));
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct crypto_hash *auth = ctx->auth;
	struct hash_desc desc = {
		.tfm = auth,
		.flags = aead_request_flags(req),
	};
	u8 *ohash = aead_request_ctx(req);
	u8 *ihash;
	struct scatterlist *src;
	unsigned int cryptlen;
	unsigned int authsize;
	int err;

	ohash = (u8 *)ALIGN((unsigned long)ohash + crypto_hash_alignmask(auth), 
			    crypto_hash_alignmask(auth) + 1);
	ihash = ohash + crypto_hash_digestsize(auth);

	spin_lock_bh(&ctx->auth_lock);
	err = crypto_hash_init(&desc);
	if (err)
		goto auth_unlock;

	err = crypto_hash_update(&desc, req->assoc, req->assoclen);
	if (err)
		goto auth_unlock;

	cryptlen = req->cryptlen;
	src = req->src;
	err = crypto_hash_update(&desc, src, cryptlen);
	if (err)
		goto auth_unlock;

	err = crypto_hash_final(&desc, ohash);
auth_unlock:
	spin_unlock_bh(&ctx->auth_lock);

	if (err)
		return err;

	authsize = ictx->authsize;
	scatterwalk_map_and_copy(ihash, src, cryptlen, authsize, 0);
	return memcmp(ihash, ohash, authsize) ? -EINVAL : 0;
}

static void crypto_authenc_decrypt_done(struct crypto_async_request *req,
					int err)
{
	aead_request_complete(req->data, err);
}

static int crypto_authenc_decrypt(struct aead_request *req)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct ablkcipher_request *abreq = aead_request_ctx(req);
	int err;

	err = crypto_authenc_verify(req);
	if (err)
		return err;

	ablkcipher_request_set_tfm(abreq, ctx->enc);
	ablkcipher_request_set_callback(abreq, aead_request_flags(req),
					crypto_authenc_decrypt_done, req);
	ablkcipher_request_set_crypt(abreq, req->src, req->dst, req->cryptlen,
				     req->iv);

	return crypto_ablkcipher_decrypt(abreq);
}

static int crypto_authenc_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct authenc_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_authenc_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_hash *auth;
	struct crypto_ablkcipher *enc;
	unsigned int digestsize;
	int err;

	auth = crypto_spawn_hash(&ictx->auth);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	err = -EINVAL;
	digestsize = crypto_hash_digestsize(auth);
	if (ictx->authsize > digestsize)
		goto err_free_hash;

	enc = crypto_spawn_ablkcipher(&ictx->enc);
	err = PTR_ERR(enc);
	if (IS_ERR(enc))
		goto err_free_hash;

	ctx->auth = auth;
	ctx->enc = enc;
	tfm->crt_aead.reqsize = max_t(unsigned int,
				      (crypto_hash_alignmask(auth) &
				       ~(crypto_tfm_ctx_alignment() - 1)) +
				      digestsize * 2,
				      sizeof(struct ablkcipher_request) +
				      crypto_ablkcipher_reqsize(enc));

	spin_lock_init(&ctx->auth_lock);

	return 0;

err_free_hash:
	crypto_free_hash(auth);
	return err;
}

static void crypto_authenc_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_authenc_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_hash(ctx->auth);
	crypto_free_ablkcipher(ctx->enc);
}

static struct crypto_instance *crypto_authenc_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *auth;
	struct crypto_alg *enc;
	struct authenc_instance_ctx *ctx;
	unsigned int authsize;
	unsigned int enckeylen;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AEAD);
	if (err)
		return ERR_PTR(err);

	auth = crypto_attr_alg(tb[1], CRYPTO_ALG_TYPE_HASH,
			       CRYPTO_ALG_TYPE_HASH_MASK);
	if (IS_ERR(auth))
		return ERR_PTR(PTR_ERR(auth));

	err = crypto_attr_u32(tb[2], &authsize);
	inst = ERR_PTR(err);
	if (err)
		goto out_put_auth;

	enc = crypto_attr_alg(tb[3], CRYPTO_ALG_TYPE_BLKCIPHER,
			      CRYPTO_ALG_TYPE_MASK);
	inst = ERR_PTR(PTR_ERR(enc));
	if (IS_ERR(enc))
		goto out_put_auth;

	err = crypto_attr_u32(tb[4], &enckeylen);
	if (err)
		goto out_put_enc;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	err = -ENOMEM;
	if (!inst)
		goto out_put_enc;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "authenc(%s,%u,%s,%u)", auth->cra_name, authsize,
		     enc->cra_name, enckeylen) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "authenc(%s,%u,%s,%u)", auth->cra_driver_name,
		     authsize, enc->cra_driver_name, enckeylen) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	ctx = crypto_instance_ctx(inst);
	ctx->authsize = authsize;
	ctx->enckeylen = enckeylen;

	err = crypto_init_spawn(&ctx->auth, auth, inst, CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto err_free_inst;

	err = crypto_init_spawn(&ctx->enc, enc, inst, CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto err_drop_auth;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = enc->cra_priority * 10 + auth->cra_priority;
	inst->alg.cra_blocksize = enc->cra_blocksize;
	inst->alg.cra_alignmask = max(auth->cra_alignmask, enc->cra_alignmask);
	inst->alg.cra_type = &crypto_aead_type;

	inst->alg.cra_aead.ivsize = enc->cra_blkcipher.ivsize;
	inst->alg.cra_aead.authsize = authsize;

	inst->alg.cra_ctxsize = sizeof(struct crypto_authenc_ctx);

	inst->alg.cra_init = crypto_authenc_init_tfm;
	inst->alg.cra_exit = crypto_authenc_exit_tfm;

	inst->alg.cra_aead.setkey = crypto_authenc_setkey;
	inst->alg.cra_aead.encrypt = crypto_authenc_encrypt;
	inst->alg.cra_aead.decrypt = crypto_authenc_decrypt;

out:
	crypto_mod_put(enc);
out_put_auth:
	crypto_mod_put(auth);
	return inst;

err_drop_auth:
	crypto_drop_spawn(&ctx->auth);
err_free_inst:
	kfree(inst);
out_put_enc:
	inst = ERR_PTR(err);
	goto out;
}

static void crypto_authenc_free(struct crypto_instance *inst)
{
	struct authenc_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_spawn(&ctx->enc);
	crypto_drop_spawn(&ctx->auth);
	kfree(inst);
}

static struct crypto_template crypto_authenc_tmpl = {
	.name = "authenc",
	.alloc = crypto_authenc_alloc,
	.free = crypto_authenc_free,
	.module = THIS_MODULE,
};

static int __init crypto_authenc_module_init(void)
{
	return crypto_register_template(&crypto_authenc_tmpl);
}

static void __exit crypto_authenc_module_exit(void)
{
	crypto_unregister_template(&crypto_authenc_tmpl);
}

module_init(crypto_authenc_module_init);
module_exit(crypto_authenc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple AEAD wrapper for IPsec");
