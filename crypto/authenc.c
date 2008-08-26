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

#include <crypto/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/authenc.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct authenc_instance_ctx {
	struct crypto_spawn auth;
	struct crypto_skcipher_spawn enc;
};

struct crypto_authenc_ctx {
	spinlock_t auth_lock;
	struct crypto_hash *auth;
	struct crypto_ablkcipher *enc;
};

static int crypto_authenc_setkey(struct crypto_aead *authenc, const u8 *key,
				 unsigned int keylen)
{
	unsigned int authkeylen;
	unsigned int enckeylen;
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct crypto_hash *auth = ctx->auth;
	struct crypto_ablkcipher *enc = ctx->enc;
	struct rtattr *rta = (void *)key;
	struct crypto_authenc_key_param *param;
	int err = -EINVAL;

	if (!RTA_OK(rta, keylen))
		goto badkey;
	if (rta->rta_type != CRYPTO_AUTHENC_KEYA_PARAM)
		goto badkey;
	if (RTA_PAYLOAD(rta) < sizeof(*param))
		goto badkey;

	param = RTA_DATA(rta);
	enckeylen = be32_to_cpu(param->enckeylen);

	key += RTA_ALIGN(rta->rta_len);
	keylen -= RTA_ALIGN(rta->rta_len);

	if (keylen < enckeylen)
		goto badkey;

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

badkey:
	crypto_aead_set_flags(authenc, CRYPTO_TFM_RES_BAD_KEY_LEN);
	goto out;
}

static void authenc_chain(struct scatterlist *head, struct scatterlist *sg,
			  int chain)
{
	if (chain) {
		head->length += sg->length;
		sg = scatterwalk_sg_next(sg);
	}

	if (sg)
		scatterwalk_sg_chain(head, 2, sg);
	else
		sg_mark_end(head);
}

static u8 *crypto_authenc_hash(struct aead_request *req, unsigned int flags,
			       struct scatterlist *cipher,
			       unsigned int cryptlen)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct crypto_hash *auth = ctx->auth;
	struct hash_desc desc = {
		.tfm = auth,
		.flags = aead_request_flags(req) & flags,
	};
	u8 *hash = aead_request_ctx(req);
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

	err = crypto_hash_update(&desc, cipher, cryptlen);
	if (err)
		goto auth_unlock;

	err = crypto_hash_final(&desc, hash);
auth_unlock:
	spin_unlock_bh(&ctx->auth_lock);

	if (err)
		return ERR_PTR(err);

	return hash;
}

static int crypto_authenc_genicv(struct aead_request *req, u8 *iv,
				 unsigned int flags)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct scatterlist *dst = req->dst;
	struct scatterlist cipher[2];
	struct page *dstp;
	unsigned int ivsize = crypto_aead_ivsize(authenc);
	unsigned int cryptlen;
	u8 *vdst;
	u8 *hash;

	dstp = sg_page(dst);
	vdst = PageHighMem(dstp) ? NULL : page_address(dstp) + dst->offset;

	sg_init_table(cipher, 2);
	sg_set_buf(cipher, iv, ivsize);
	authenc_chain(cipher, dst, vdst == iv + ivsize);

	cryptlen = req->cryptlen + ivsize;
	hash = crypto_authenc_hash(req, flags, cipher, cryptlen);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	scatterwalk_map_and_copy(hash, cipher, cryptlen,
				 crypto_aead_authsize(authenc), 1);
	return 0;
}

static void crypto_authenc_encrypt_done(struct crypto_async_request *req,
					int err)
{
	struct aead_request *areq = req->data;

	if (!err) {
		struct crypto_aead *authenc = crypto_aead_reqtfm(areq);
		struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
		struct ablkcipher_request *abreq = aead_request_ctx(areq);
		u8 *iv = (u8 *)(abreq + 1) +
			 crypto_ablkcipher_reqsize(ctx->enc);

		err = crypto_authenc_genicv(areq, iv, 0);
	}

	aead_request_complete(areq, err);
}

static int crypto_authenc_encrypt(struct aead_request *req)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct ablkcipher_request *abreq = aead_request_ctx(req);
	struct crypto_ablkcipher *enc = ctx->enc;
	struct scatterlist *dst = req->dst;
	unsigned int cryptlen = req->cryptlen;
	u8 *iv = (u8 *)(abreq + 1) + crypto_ablkcipher_reqsize(enc);
	int err;

	ablkcipher_request_set_tfm(abreq, enc);
	ablkcipher_request_set_callback(abreq, aead_request_flags(req),
					crypto_authenc_encrypt_done, req);
	ablkcipher_request_set_crypt(abreq, req->src, dst, cryptlen, req->iv);

	memcpy(iv, req->iv, crypto_aead_ivsize(authenc));

	err = crypto_ablkcipher_encrypt(abreq);
	if (err)
		return err;

	return crypto_authenc_genicv(req, iv, CRYPTO_TFM_REQ_MAY_SLEEP);
}

static void crypto_authenc_givencrypt_done(struct crypto_async_request *req,
					   int err)
{
	struct aead_request *areq = req->data;

	if (!err) {
		struct skcipher_givcrypt_request *greq = aead_request_ctx(areq);

		err = crypto_authenc_genicv(areq, greq->giv, 0);
	}

	aead_request_complete(areq, err);
}

static int crypto_authenc_givencrypt(struct aead_givcrypt_request *req)
{
	struct crypto_aead *authenc = aead_givcrypt_reqtfm(req);
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct aead_request *areq = &req->areq;
	struct skcipher_givcrypt_request *greq = aead_request_ctx(areq);
	u8 *iv = req->giv;
	int err;

	skcipher_givcrypt_set_tfm(greq, ctx->enc);
	skcipher_givcrypt_set_callback(greq, aead_request_flags(areq),
				       crypto_authenc_givencrypt_done, areq);
	skcipher_givcrypt_set_crypt(greq, areq->src, areq->dst, areq->cryptlen,
				    areq->iv);
	skcipher_givcrypt_set_giv(greq, iv, req->seq);

	err = crypto_skcipher_givencrypt(greq);
	if (err)
		return err;

	return crypto_authenc_genicv(areq, iv, CRYPTO_TFM_REQ_MAY_SLEEP);
}

static int crypto_authenc_verify(struct aead_request *req,
				 struct scatterlist *cipher,
				 unsigned int cryptlen)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	u8 *ohash;
	u8 *ihash;
	unsigned int authsize;

	ohash = crypto_authenc_hash(req, CRYPTO_TFM_REQ_MAY_SLEEP, cipher,
				    cryptlen);
	if (IS_ERR(ohash))
		return PTR_ERR(ohash);

	authsize = crypto_aead_authsize(authenc);
	ihash = ohash + authsize;
	scatterwalk_map_and_copy(ihash, cipher, cryptlen, authsize, 0);
	return memcmp(ihash, ohash, authsize) ? -EBADMSG: 0;
}

static int crypto_authenc_iverify(struct aead_request *req, u8 *iv,
				  unsigned int cryptlen)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct scatterlist *src = req->src;
	struct scatterlist cipher[2];
	struct page *srcp;
	unsigned int ivsize = crypto_aead_ivsize(authenc);
	u8 *vsrc;

	srcp = sg_page(src);
	vsrc = PageHighMem(srcp) ? NULL : page_address(srcp) + src->offset;

	sg_init_table(cipher, 2);
	sg_set_buf(cipher, iv, ivsize);
	authenc_chain(cipher, src, vsrc == iv + ivsize);

	return crypto_authenc_verify(req, cipher, cryptlen + ivsize);
}

static int crypto_authenc_decrypt(struct aead_request *req)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct crypto_authenc_ctx *ctx = crypto_aead_ctx(authenc);
	struct ablkcipher_request *abreq = aead_request_ctx(req);
	unsigned int cryptlen = req->cryptlen;
	unsigned int authsize = crypto_aead_authsize(authenc);
	u8 *iv = req->iv;
	int err;

	if (cryptlen < authsize)
		return -EINVAL;
	cryptlen -= authsize;

	err = crypto_authenc_iverify(req, iv, cryptlen);
	if (err)
		return err;

	ablkcipher_request_set_tfm(abreq, ctx->enc);
	ablkcipher_request_set_callback(abreq, aead_request_flags(req),
					req->base.complete, req->base.data);
	ablkcipher_request_set_crypt(abreq, req->src, req->dst, cryptlen, iv);

	return crypto_ablkcipher_decrypt(abreq);
}

static int crypto_authenc_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct authenc_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_authenc_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_hash *auth;
	struct crypto_ablkcipher *enc;
	int err;

	auth = crypto_spawn_hash(&ictx->auth);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	enc = crypto_spawn_skcipher(&ictx->enc);
	err = PTR_ERR(enc);
	if (IS_ERR(enc))
		goto err_free_hash;

	ctx->auth = auth;
	ctx->enc = enc;
	tfm->crt_aead.reqsize = max_t(unsigned int,
				      (crypto_hash_alignmask(auth) &
				       ~(crypto_tfm_ctx_alignment() - 1)) +
				      crypto_hash_digestsize(auth) * 2,
				      sizeof(struct skcipher_givcrypt_request) +
				      crypto_ablkcipher_reqsize(enc) +
				      crypto_ablkcipher_ivsize(enc));

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
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_alg *auth;
	struct crypto_alg *enc;
	struct authenc_instance_ctx *ctx;
	const char *enc_name;
	int err;

	algt = crypto_get_attr_type(tb);
	err = PTR_ERR(algt);
	if (IS_ERR(algt))
		return ERR_PTR(err);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	auth = crypto_attr_alg(tb[1], CRYPTO_ALG_TYPE_HASH,
			       CRYPTO_ALG_TYPE_HASH_MASK);
	if (IS_ERR(auth))
		return ERR_PTR(PTR_ERR(auth));

	enc_name = crypto_attr_alg_name(tb[2]);
	err = PTR_ERR(enc_name);
	if (IS_ERR(enc_name))
		goto out_put_auth;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	err = -ENOMEM;
	if (!inst)
		goto out_put_auth;

	ctx = crypto_instance_ctx(inst);

	err = crypto_init_spawn(&ctx->auth, auth, inst, CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto err_free_inst;

	crypto_set_skcipher_spawn(&ctx->enc, inst);
	err = crypto_grab_skcipher(&ctx->enc, enc_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_drop_auth;

	enc = crypto_skcipher_spawn_alg(&ctx->enc);

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "authenc(%s,%s)", auth->cra_name, enc->cra_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_drop_enc;

	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "authenc(%s,%s)", auth->cra_driver_name,
		     enc->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_drop_enc;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD;
	inst->alg.cra_flags |= enc->cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = enc->cra_priority * 10 + auth->cra_priority;
	inst->alg.cra_blocksize = enc->cra_blocksize;
	inst->alg.cra_alignmask = auth->cra_alignmask | enc->cra_alignmask;
	inst->alg.cra_type = &crypto_aead_type;

	inst->alg.cra_aead.ivsize = enc->cra_ablkcipher.ivsize;
	inst->alg.cra_aead.maxauthsize = auth->cra_type == &crypto_hash_type ?
					 auth->cra_hash.digestsize :
					 auth->cra_digest.dia_digestsize;

	inst->alg.cra_ctxsize = sizeof(struct crypto_authenc_ctx);

	inst->alg.cra_init = crypto_authenc_init_tfm;
	inst->alg.cra_exit = crypto_authenc_exit_tfm;

	inst->alg.cra_aead.setkey = crypto_authenc_setkey;
	inst->alg.cra_aead.encrypt = crypto_authenc_encrypt;
	inst->alg.cra_aead.decrypt = crypto_authenc_decrypt;
	inst->alg.cra_aead.givencrypt = crypto_authenc_givencrypt;

out:
	crypto_mod_put(auth);
	return inst;

err_drop_enc:
	crypto_drop_skcipher(&ctx->enc);
err_drop_auth:
	crypto_drop_spawn(&ctx->auth);
err_free_inst:
	kfree(inst);
out_put_auth:
	inst = ERR_PTR(err);
	goto out;
}

static void crypto_authenc_free(struct crypto_instance *inst)
{
	struct authenc_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->enc);
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
