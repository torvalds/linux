/*
 * GCM: Galois/Counter Mode.
 *
 * Copyright (c) 2007 Nokia Siemens Networks - Mikko Herranen <mh1@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <crypto/gf128mul.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/internal/hash.h>
#include <crypto/null.h>
#include <crypto/scatterwalk.h>
#include <crypto/hash.h>
#include "internal.h"
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

struct gcm_instance_ctx {
	struct crypto_skcipher_spawn ctr;
	struct crypto_ahash_spawn ghash;
};

struct crypto_gcm_ctx {
	struct crypto_ablkcipher *ctr;
	struct crypto_ahash *ghash;
};

struct crypto_rfc4106_ctx {
	struct crypto_aead *child;
	u8 nonce[4];
};

struct crypto_rfc4106_req_ctx {
	struct scatterlist src[3];
	struct scatterlist dst[3];
	struct aead_request subreq;
};

struct crypto_rfc4543_instance_ctx {
	struct crypto_aead_spawn aead;
};

struct crypto_rfc4543_ctx {
	struct crypto_aead *child;
	struct crypto_blkcipher *null;
	u8 nonce[4];
};

struct crypto_rfc4543_req_ctx {
	struct aead_request subreq;
};

struct crypto_gcm_ghash_ctx {
	unsigned int cryptlen;
	struct scatterlist *src;
	int (*complete)(struct aead_request *req, u32 flags);
};

struct crypto_gcm_req_priv_ctx {
	u8 iv[16];
	u8 auth_tag[16];
	u8 iauth_tag[16];
	struct scatterlist src[3];
	struct scatterlist dst[3];
	struct scatterlist sg;
	struct crypto_gcm_ghash_ctx ghash_ctx;
	union {
		struct ahash_request ahreq;
		struct ablkcipher_request abreq;
	} u;
};

struct crypto_gcm_setkey_result {
	int err;
	struct completion completion;
};

static struct {
	u8 buf[16];
	struct scatterlist sg;
} *gcm_zeroes;

static int crypto_rfc4543_copy_src_to_dst(struct aead_request *req, bool enc);

static inline struct crypto_gcm_req_priv_ctx *crypto_gcm_reqctx(
	struct aead_request *req)
{
	unsigned long align = crypto_aead_alignmask(crypto_aead_reqtfm(req));

	return (void *)PTR_ALIGN((u8 *)aead_request_ctx(req), align + 1);
}

static void crypto_gcm_setkey_done(struct crypto_async_request *req, int err)
{
	struct crypto_gcm_setkey_result *result = req->data;

	if (err == -EINPROGRESS)
		return;

	result->err = err;
	complete(&result->completion);
}

static int crypto_gcm_setkey(struct crypto_aead *aead, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_ahash *ghash = ctx->ghash;
	struct crypto_ablkcipher *ctr = ctx->ctr;
	struct {
		be128 hash;
		u8 iv[8];

		struct crypto_gcm_setkey_result result;

		struct scatterlist sg[1];
		struct ablkcipher_request req;
	} *data;
	int err;

	crypto_ablkcipher_clear_flags(ctr, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(ctr, crypto_aead_get_flags(aead) &
					 CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(ctr, key, keylen);
	crypto_aead_set_flags(aead, crypto_ablkcipher_get_flags(ctr) &
				    CRYPTO_TFM_RES_MASK);
	if (err)
		return err;

	data = kzalloc(sizeof(*data) + crypto_ablkcipher_reqsize(ctr),
		       GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	init_completion(&data->result.completion);
	sg_init_one(data->sg, &data->hash, sizeof(data->hash));
	ablkcipher_request_set_tfm(&data->req, ctr);
	ablkcipher_request_set_callback(&data->req, CRYPTO_TFM_REQ_MAY_SLEEP |
						    CRYPTO_TFM_REQ_MAY_BACKLOG,
					crypto_gcm_setkey_done,
					&data->result);
	ablkcipher_request_set_crypt(&data->req, data->sg, data->sg,
				     sizeof(data->hash), data->iv);

	err = crypto_ablkcipher_encrypt(&data->req);
	if (err == -EINPROGRESS || err == -EBUSY) {
		err = wait_for_completion_interruptible(
			&data->result.completion);
		if (!err)
			err = data->result.err;
	}

	if (err)
		goto out;

	crypto_ahash_clear_flags(ghash, CRYPTO_TFM_REQ_MASK);
	crypto_ahash_set_flags(ghash, crypto_aead_get_flags(aead) &
			       CRYPTO_TFM_REQ_MASK);
	err = crypto_ahash_setkey(ghash, (u8 *)&data->hash, sizeof(be128));
	crypto_aead_set_flags(aead, crypto_ahash_get_flags(ghash) &
			      CRYPTO_TFM_RES_MASK);

out:
	kzfree(data);
	return err;
}

static int crypto_gcm_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void crypto_gcm_init_common(struct aead_request *req)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	__be32 counter = cpu_to_be32(1);
	struct scatterlist *sg;

	memset(pctx->auth_tag, 0, sizeof(pctx->auth_tag));
	memcpy(pctx->iv, req->iv, 12);
	memcpy(pctx->iv + 12, &counter, 4);

	sg_init_table(pctx->src, 3);
	sg_set_buf(pctx->src, pctx->auth_tag, sizeof(pctx->auth_tag));
	sg = scatterwalk_ffwd(pctx->src + 1, req->src, req->assoclen);
	if (sg != pctx->src + 1)
		scatterwalk_sg_chain(pctx->src, 2, sg);

	if (req->src != req->dst) {
		sg_init_table(pctx->dst, 3);
		sg_set_buf(pctx->dst, pctx->auth_tag, sizeof(pctx->auth_tag));
		sg = scatterwalk_ffwd(pctx->dst + 1, req->dst, req->assoclen);
		if (sg != pctx->dst + 1)
			scatterwalk_sg_chain(pctx->dst, 2, sg);
	}
}

static void crypto_gcm_init_crypt(struct aead_request *req,
				  unsigned int cryptlen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ablkcipher_request *ablk_req = &pctx->u.abreq;
	struct scatterlist *dst;

	dst = req->src == req->dst ? pctx->src : pctx->dst;

	ablkcipher_request_set_tfm(ablk_req, ctx->ctr);
	ablkcipher_request_set_crypt(ablk_req, pctx->src, dst,
				     cryptlen + sizeof(pctx->auth_tag),
				     pctx->iv);
}

static inline unsigned int gcm_remain(unsigned int len)
{
	len &= 0xfU;
	return len ? 16 - len : 0;
}

static void gcm_hash_len_done(struct crypto_async_request *areq, int err);

static int gcm_hash_update(struct aead_request *req,
			   crypto_completion_t compl,
			   struct scatterlist *src,
			   unsigned int len, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ahash_request *ahreq = &pctx->u.ahreq;

	ahash_request_set_callback(ahreq, flags, compl, req);
	ahash_request_set_crypt(ahreq, src, NULL, len);

	return crypto_ahash_update(ahreq);
}

static int gcm_hash_remain(struct aead_request *req,
			   unsigned int remain,
			   crypto_completion_t compl, u32 flags)
{
	return gcm_hash_update(req, compl, &gcm_zeroes->sg, remain, flags);
}

static int gcm_hash_len(struct aead_request *req, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ahash_request *ahreq = &pctx->u.ahreq;
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	u128 lengths;

	lengths.a = cpu_to_be64(req->assoclen * 8);
	lengths.b = cpu_to_be64(gctx->cryptlen * 8);
	memcpy(pctx->iauth_tag, &lengths, 16);
	sg_init_one(&pctx->sg, pctx->iauth_tag, 16);
	ahash_request_set_callback(ahreq, flags, gcm_hash_len_done, req);
	ahash_request_set_crypt(ahreq, &pctx->sg,
				pctx->iauth_tag, sizeof(lengths));

	return crypto_ahash_finup(ahreq);
}

static int gcm_hash_len_continue(struct aead_request *req, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;

	return gctx->complete(req, flags);
}

static void gcm_hash_len_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	if (err)
		goto out;

	err = gcm_hash_len_continue(req, 0);
	if (err == -EINPROGRESS)
		return;

out:
	aead_request_complete(req, err);
}

static int gcm_hash_crypt_remain_continue(struct aead_request *req, u32 flags)
{
	return gcm_hash_len(req, flags) ?:
	       gcm_hash_len_continue(req, flags);
}

static void gcm_hash_crypt_remain_done(struct crypto_async_request *areq,
				       int err)
{
	struct aead_request *req = areq->data;

	if (err)
		goto out;

	err = gcm_hash_crypt_remain_continue(req, 0);
	if (err == -EINPROGRESS)
		return;

out:
	aead_request_complete(req, err);
}

static int gcm_hash_crypt_continue(struct aead_request *req, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	unsigned int remain;

	remain = gcm_remain(gctx->cryptlen);
	if (remain)
		return gcm_hash_remain(req, remain,
				       gcm_hash_crypt_remain_done, flags) ?:
		       gcm_hash_crypt_remain_continue(req, flags);

	return gcm_hash_crypt_remain_continue(req, flags);
}

static void gcm_hash_crypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	if (err)
		goto out;

	err = gcm_hash_crypt_continue(req, 0);
	if (err == -EINPROGRESS)
		return;

out:
	aead_request_complete(req, err);
}

static int gcm_hash_assoc_remain_continue(struct aead_request *req, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;

	if (gctx->cryptlen)
		return gcm_hash_update(req, gcm_hash_crypt_done,
				       gctx->src, gctx->cryptlen, flags) ?:
		       gcm_hash_crypt_continue(req, flags);

	return gcm_hash_crypt_remain_continue(req, flags);
}

static void gcm_hash_assoc_remain_done(struct crypto_async_request *areq,
				       int err)
{
	struct aead_request *req = areq->data;

	if (err)
		goto out;

	err = gcm_hash_assoc_remain_continue(req, 0);
	if (err == -EINPROGRESS)
		return;

out:
	aead_request_complete(req, err);
}

static int gcm_hash_assoc_continue(struct aead_request *req, u32 flags)
{
	unsigned int remain;

	remain = gcm_remain(req->assoclen);
	if (remain)
		return gcm_hash_remain(req, remain,
				       gcm_hash_assoc_remain_done, flags) ?:
		       gcm_hash_assoc_remain_continue(req, flags);

	return gcm_hash_assoc_remain_continue(req, flags);
}

static void gcm_hash_assoc_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	if (err)
		goto out;

	err = gcm_hash_assoc_continue(req, 0);
	if (err == -EINPROGRESS)
		return;

out:
	aead_request_complete(req, err);
}

static int gcm_hash_init_continue(struct aead_request *req, u32 flags)
{
	if (req->assoclen)
		return gcm_hash_update(req, gcm_hash_assoc_done,
				       req->src, req->assoclen, flags) ?:
		       gcm_hash_assoc_continue(req, flags);

	return gcm_hash_assoc_remain_continue(req, flags);
}

static void gcm_hash_init_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	if (err)
		goto out;

	err = gcm_hash_init_continue(req, 0);
	if (err == -EINPROGRESS)
		return;

out:
	aead_request_complete(req, err);
}

static int gcm_hash(struct aead_request *req, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ahash_request *ahreq = &pctx->u.ahreq;
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));

	ahash_request_set_tfm(ahreq, ctx->ghash);

	ahash_request_set_callback(ahreq, flags, gcm_hash_init_done, req);
	return crypto_ahash_init(ahreq) ?:
	       gcm_hash_init_continue(req, flags);
}

static int gcm_enc_copy_hash(struct aead_request *req, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	u8 *auth_tag = pctx->auth_tag;

	crypto_xor(auth_tag, pctx->iauth_tag, 16);
	scatterwalk_map_and_copy(auth_tag, req->dst,
				 req->assoclen + req->cryptlen,
				 crypto_aead_authsize(aead), 1);
	return 0;
}

static int gcm_encrypt_continue(struct aead_request *req, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;

	gctx->src = sg_next(req->src == req->dst ? pctx->src : pctx->dst);
	gctx->cryptlen = req->cryptlen;
	gctx->complete = gcm_enc_copy_hash;

	return gcm_hash(req, flags);
}

static void gcm_encrypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	if (err)
		goto out;

	err = gcm_encrypt_continue(req, 0);
	if (err == -EINPROGRESS)
		return;

out:
	aead_request_complete(req, err);
}

static int crypto_gcm_encrypt(struct aead_request *req)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ablkcipher_request *abreq = &pctx->u.abreq;
	u32 flags = aead_request_flags(req);

	crypto_gcm_init_common(req);
	crypto_gcm_init_crypt(req, req->cryptlen);
	ablkcipher_request_set_callback(abreq, flags, gcm_encrypt_done, req);

	return crypto_ablkcipher_encrypt(abreq) ?:
	       gcm_encrypt_continue(req, flags);
}

static int crypto_gcm_verify(struct aead_request *req)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	u8 *auth_tag = pctx->auth_tag;
	u8 *iauth_tag = pctx->iauth_tag;
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int cryptlen = req->cryptlen - authsize;

	crypto_xor(auth_tag, iauth_tag, 16);
	scatterwalk_map_and_copy(iauth_tag, req->src,
				 req->assoclen + cryptlen, authsize, 0);
	return crypto_memneq(iauth_tag, auth_tag, authsize) ? -EBADMSG : 0;
}

static void gcm_decrypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	if (!err)
		err = crypto_gcm_verify(req);

	aead_request_complete(req, err);
}

static int gcm_dec_hash_continue(struct aead_request *req, u32 flags)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ablkcipher_request *abreq = &pctx->u.abreq;
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;

	crypto_gcm_init_crypt(req, gctx->cryptlen);
	ablkcipher_request_set_callback(abreq, flags, gcm_decrypt_done, req);
	return crypto_ablkcipher_decrypt(abreq) ?: crypto_gcm_verify(req);
}

static int crypto_gcm_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int cryptlen = req->cryptlen;
	u32 flags = aead_request_flags(req);

	cryptlen -= authsize;

	crypto_gcm_init_common(req);

	gctx->src = sg_next(pctx->src);
	gctx->cryptlen = cryptlen;
	gctx->complete = gcm_dec_hash_continue;

	return gcm_hash(req, flags);
}

static int crypto_gcm_init_tfm(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct gcm_instance_ctx *ictx = aead_instance_ctx(inst);
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_ablkcipher *ctr;
	struct crypto_ahash *ghash;
	unsigned long align;
	int err;

	ghash = crypto_spawn_ahash(&ictx->ghash);
	if (IS_ERR(ghash))
		return PTR_ERR(ghash);

	ctr = crypto_spawn_skcipher(&ictx->ctr);
	err = PTR_ERR(ctr);
	if (IS_ERR(ctr))
		goto err_free_hash;

	ctx->ctr = ctr;
	ctx->ghash = ghash;

	align = crypto_aead_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	crypto_aead_set_reqsize(tfm,
		align + offsetof(struct crypto_gcm_req_priv_ctx, u) +
		max(sizeof(struct ablkcipher_request) +
		    crypto_ablkcipher_reqsize(ctr),
		    sizeof(struct ahash_request) +
		    crypto_ahash_reqsize(ghash)));

	return 0;

err_free_hash:
	crypto_free_ahash(ghash);
	return err;
}

static void crypto_gcm_exit_tfm(struct crypto_aead *tfm)
{
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_ahash(ctx->ghash);
	crypto_free_ablkcipher(ctx->ctr);
}

static void crypto_gcm_free(struct aead_instance *inst)
{
	struct gcm_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->ctr);
	crypto_drop_ahash(&ctx->ghash);
	kfree(inst);
}

static int crypto_gcm_create_common(struct crypto_template *tmpl,
				    struct rtattr **tb,
				    const char *full_name,
				    const char *ctr_name,
				    const char *ghash_name)
{
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct crypto_alg *ctr;
	struct crypto_alg *ghash_alg;
	struct hash_alg_common *ghash;
	struct gcm_instance_ctx *ctx;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ (CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_AEAD_NEW)) &
	    algt->mask)
		return -EINVAL;

	ghash_alg = crypto_find_alg(ghash_name, &crypto_ahash_type,
				    CRYPTO_ALG_TYPE_HASH,
				    CRYPTO_ALG_TYPE_AHASH_MASK);
	if (IS_ERR(ghash_alg))
		return PTR_ERR(ghash_alg);

	ghash = __crypto_hash_alg_common(ghash_alg);

	err = -ENOMEM;
	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		goto out_put_ghash;

	ctx = aead_instance_ctx(inst);
	err = crypto_init_ahash_spawn(&ctx->ghash, ghash,
				      aead_crypto_instance(inst));
	if (err)
		goto err_free_inst;

	err = -EINVAL;
	if (ghash->digestsize != 16)
		goto err_drop_ghash;

	crypto_set_skcipher_spawn(&ctx->ctr, aead_crypto_instance(inst));
	err = crypto_grab_skcipher(&ctx->ctr, ctr_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_drop_ghash;

	ctr = crypto_skcipher_spawn_alg(&ctx->ctr);

	/* We only support 16-byte blocks. */
	if (ctr->cra_ablkcipher.ivsize != 16)
		goto out_put_ctr;

	/* Not a stream cipher? */
	err = -EINVAL;
	if (ctr->cra_blocksize != 1)
		goto out_put_ctr;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "gcm_base(%s,%s)", ctr->cra_driver_name,
		     ghash_alg->cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_put_ctr;

	memcpy(inst->alg.base.cra_name, full_name, CRYPTO_MAX_ALG_NAME);

	inst->alg.base.cra_flags = (ghash->base.cra_flags | ctr->cra_flags) &
				   CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_flags |= CRYPTO_ALG_AEAD_NEW;
	inst->alg.base.cra_priority = (ghash->base.cra_priority +
				       ctr->cra_priority) / 2;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = ghash->base.cra_alignmask |
				       ctr->cra_alignmask;
	inst->alg.base.cra_ctxsize = sizeof(struct crypto_gcm_ctx);
	inst->alg.ivsize = 12;
	inst->alg.maxauthsize = 16;
	inst->alg.init = crypto_gcm_init_tfm;
	inst->alg.exit = crypto_gcm_exit_tfm;
	inst->alg.setkey = crypto_gcm_setkey;
	inst->alg.setauthsize = crypto_gcm_setauthsize;
	inst->alg.encrypt = crypto_gcm_encrypt;
	inst->alg.decrypt = crypto_gcm_decrypt;

	inst->free = crypto_gcm_free;

	err = aead_register_instance(tmpl, inst);
	if (err)
		goto out_put_ctr;

out_put_ghash:
	crypto_mod_put(ghash_alg);
	return err;

out_put_ctr:
	crypto_drop_skcipher(&ctx->ctr);
err_drop_ghash:
	crypto_drop_ahash(&ctx->ghash);
err_free_inst:
	kfree(inst);
	goto out_put_ghash;
}

static int crypto_gcm_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	const char *cipher_name;
	char ctr_name[CRYPTO_MAX_ALG_NAME];
	char full_name[CRYPTO_MAX_ALG_NAME];

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	if (snprintf(ctr_name, CRYPTO_MAX_ALG_NAME, "ctr(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "gcm(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	return crypto_gcm_create_common(tmpl, tb, full_name,
					ctr_name, "ghash");
}

static struct crypto_template crypto_gcm_tmpl = {
	.name = "gcm",
	.create = crypto_gcm_create,
	.module = THIS_MODULE,
};

static int crypto_gcm_base_create(struct crypto_template *tmpl,
				  struct rtattr **tb)
{
	const char *ctr_name;
	const char *ghash_name;
	char full_name[CRYPTO_MAX_ALG_NAME];

	ctr_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ctr_name))
		return PTR_ERR(ctr_name);

	ghash_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(ghash_name))
		return PTR_ERR(ghash_name);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "gcm_base(%s,%s)",
		     ctr_name, ghash_name) >= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	return crypto_gcm_create_common(tmpl, tb, full_name,
					ctr_name, ghash_name);
}

static struct crypto_template crypto_gcm_base_tmpl = {
	.name = "gcm_base",
	.create = crypto_gcm_base_create,
	.module = THIS_MODULE,
};

static int crypto_rfc4106_setkey(struct crypto_aead *parent, const u8 *key,
				 unsigned int keylen)
{
	struct crypto_rfc4106_ctx *ctx = crypto_aead_ctx(parent);
	struct crypto_aead *child = ctx->child;
	int err;

	if (keylen < 4)
		return -EINVAL;

	keylen -= 4;
	memcpy(ctx->nonce, key + keylen, 4);

	crypto_aead_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(child, crypto_aead_get_flags(parent) &
				     CRYPTO_TFM_REQ_MASK);
	err = crypto_aead_setkey(child, key, keylen);
	crypto_aead_set_flags(parent, crypto_aead_get_flags(child) &
				      CRYPTO_TFM_RES_MASK);

	return err;
}

static int crypto_rfc4106_setauthsize(struct crypto_aead *parent,
				      unsigned int authsize)
{
	struct crypto_rfc4106_ctx *ctx = crypto_aead_ctx(parent);

	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return crypto_aead_setauthsize(ctx->child, authsize);
}

static struct aead_request *crypto_rfc4106_crypt(struct aead_request *req)
{
	struct crypto_rfc4106_req_ctx *rctx = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4106_ctx *ctx = crypto_aead_ctx(aead);
	struct aead_request *subreq = &rctx->subreq;
	struct crypto_aead *child = ctx->child;
	struct scatterlist *sg;
	u8 *iv = PTR_ALIGN((u8 *)(subreq + 1) + crypto_aead_reqsize(child),
			   crypto_aead_alignmask(child) + 1);

	scatterwalk_map_and_copy(iv + 12, req->src, 0, req->assoclen - 8, 0);

	memcpy(iv, ctx->nonce, 4);
	memcpy(iv + 4, req->iv, 8);

	sg_init_table(rctx->src, 3);
	sg_set_buf(rctx->src, iv + 12, req->assoclen - 8);
	sg = scatterwalk_ffwd(rctx->src + 1, req->src, req->assoclen);
	if (sg != rctx->src + 1)
		sg_chain(rctx->src, 2, sg);

	if (req->src != req->dst) {
		sg_init_table(rctx->dst, 3);
		sg_set_buf(rctx->dst, iv + 12, req->assoclen - 8);
		sg = scatterwalk_ffwd(rctx->dst + 1, req->dst, req->assoclen);
		if (sg != rctx->dst + 1)
			sg_chain(rctx->dst, 2, sg);
	}

	aead_request_set_tfm(subreq, child);
	aead_request_set_callback(subreq, req->base.flags, req->base.complete,
				  req->base.data);
	aead_request_set_crypt(subreq, rctx->src,
			       req->src == req->dst ? rctx->src : rctx->dst,
			       req->cryptlen, iv);
	aead_request_set_ad(subreq, req->assoclen - 8);

	return subreq;
}

static int crypto_rfc4106_encrypt(struct aead_request *req)
{
	if (req->assoclen != 16 && req->assoclen != 20)
		return -EINVAL;

	req = crypto_rfc4106_crypt(req);

	return crypto_aead_encrypt(req);
}

static int crypto_rfc4106_decrypt(struct aead_request *req)
{
	if (req->assoclen != 16 && req->assoclen != 20)
		return -EINVAL;

	req = crypto_rfc4106_crypt(req);

	return crypto_aead_decrypt(req);
}

static int crypto_rfc4106_init_tfm(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct crypto_aead_spawn *spawn = aead_instance_ctx(inst);
	struct crypto_rfc4106_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_aead *aead;
	unsigned long align;

	aead = crypto_spawn_aead(spawn);
	if (IS_ERR(aead))
		return PTR_ERR(aead);

	ctx->child = aead;

	align = crypto_aead_alignmask(aead);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	crypto_aead_set_reqsize(
		tfm,
		sizeof(struct crypto_rfc4106_req_ctx) +
		ALIGN(crypto_aead_reqsize(aead), crypto_tfm_ctx_alignment()) +
		align + 24);

	return 0;
}

static void crypto_rfc4106_exit_tfm(struct crypto_aead *tfm)
{
	struct crypto_rfc4106_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->child);
}

static void crypto_rfc4106_free(struct aead_instance *inst)
{
	crypto_drop_aead(aead_instance_ctx(inst));
	kfree(inst);
}

static int crypto_rfc4106_create(struct crypto_template *tmpl,
				 struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;
	const char *ccm_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ (CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_AEAD_NEW)) &
	    algt->mask)
		return -EINVAL;

	ccm_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ccm_name))
		return PTR_ERR(ccm_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = aead_instance_ctx(inst);
	crypto_set_aead_spawn(spawn, aead_crypto_instance(inst));
	err = crypto_grab_aead(spawn, ccm_name, 0,
			       crypto_requires_sync(algt->type, algt->mask));
	if (err)
		goto out_free_inst;

	alg = crypto_spawn_aead_alg(spawn);

	err = -EINVAL;

	/* Underlying IV size must be 12. */
	if (crypto_aead_alg_ivsize(alg) != 12)
		goto out_drop_alg;

	/* Not a stream cipher? */
	if (alg->base.cra_blocksize != 1)
		goto out_drop_alg;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4106(%s)", alg->base.cra_name) >=
	    CRYPTO_MAX_ALG_NAME ||
	    snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4106(%s)", alg->base.cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_drop_alg;

	inst->alg.base.cra_flags = alg->base.cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_flags |= CRYPTO_ALG_AEAD_NEW;
	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask;

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_rfc4106_ctx);

	inst->alg.ivsize = 8;
	inst->alg.maxauthsize = crypto_aead_alg_maxauthsize(alg);

	inst->alg.init = crypto_rfc4106_init_tfm;
	inst->alg.exit = crypto_rfc4106_exit_tfm;

	inst->alg.setkey = crypto_rfc4106_setkey;
	inst->alg.setauthsize = crypto_rfc4106_setauthsize;
	inst->alg.encrypt = crypto_rfc4106_encrypt;
	inst->alg.decrypt = crypto_rfc4106_decrypt;

	inst->free = crypto_rfc4106_free;

	err = aead_register_instance(tmpl, inst);
	if (err)
		goto out_drop_alg;

out:
	return err;

out_drop_alg:
	crypto_drop_aead(spawn);
out_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_rfc4106_tmpl = {
	.name = "rfc4106",
	.create = crypto_rfc4106_create,
	.module = THIS_MODULE,
};

static int crypto_rfc4543_setkey(struct crypto_aead *parent, const u8 *key,
				 unsigned int keylen)
{
	struct crypto_rfc4543_ctx *ctx = crypto_aead_ctx(parent);
	struct crypto_aead *child = ctx->child;
	int err;

	if (keylen < 4)
		return -EINVAL;

	keylen -= 4;
	memcpy(ctx->nonce, key + keylen, 4);

	crypto_aead_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(child, crypto_aead_get_flags(parent) &
				     CRYPTO_TFM_REQ_MASK);
	err = crypto_aead_setkey(child, key, keylen);
	crypto_aead_set_flags(parent, crypto_aead_get_flags(child) &
				      CRYPTO_TFM_RES_MASK);

	return err;
}

static int crypto_rfc4543_setauthsize(struct crypto_aead *parent,
				      unsigned int authsize)
{
	struct crypto_rfc4543_ctx *ctx = crypto_aead_ctx(parent);

	if (authsize != 16)
		return -EINVAL;

	return crypto_aead_setauthsize(ctx->child, authsize);
}

static int crypto_rfc4543_crypt(struct aead_request *req, bool enc)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4543_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_rfc4543_req_ctx *rctx = aead_request_ctx(req);
	struct aead_request *subreq = &rctx->subreq;
	unsigned int authsize = crypto_aead_authsize(aead);
	u8 *iv = PTR_ALIGN((u8 *)(rctx + 1) + crypto_aead_reqsize(ctx->child),
			   crypto_aead_alignmask(ctx->child) + 1);
	int err;

	if (req->src != req->dst) {
		err = crypto_rfc4543_copy_src_to_dst(req, enc);
		if (err)
			return err;
	}

	memcpy(iv, ctx->nonce, 4);
	memcpy(iv + 4, req->iv, 8);

	aead_request_set_tfm(subreq, ctx->child);
	aead_request_set_callback(subreq, req->base.flags,
				  req->base.complete, req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       enc ? 0 : authsize, iv);
	aead_request_set_ad(subreq, req->assoclen + req->cryptlen -
				    subreq->cryptlen);

	return enc ? crypto_aead_encrypt(subreq) : crypto_aead_decrypt(subreq);
}

static int crypto_rfc4543_copy_src_to_dst(struct aead_request *req, bool enc)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4543_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int nbytes = req->assoclen + req->cryptlen -
			      (enc ? 0 : authsize);
	struct blkcipher_desc desc = {
		.tfm = ctx->null,
	};

	return crypto_blkcipher_encrypt(&desc, req->dst, req->src, nbytes);
}

static int crypto_rfc4543_encrypt(struct aead_request *req)
{
	return crypto_rfc4543_crypt(req, true);
}

static int crypto_rfc4543_decrypt(struct aead_request *req)
{
	return crypto_rfc4543_crypt(req, false);
}

static int crypto_rfc4543_init_tfm(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct crypto_rfc4543_instance_ctx *ictx = aead_instance_ctx(inst);
	struct crypto_aead_spawn *spawn = &ictx->aead;
	struct crypto_rfc4543_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_aead *aead;
	struct crypto_blkcipher *null;
	unsigned long align;
	int err = 0;

	aead = crypto_spawn_aead(spawn);
	if (IS_ERR(aead))
		return PTR_ERR(aead);

	null = crypto_get_default_null_skcipher();
	err = PTR_ERR(null);
	if (IS_ERR(null))
		goto err_free_aead;

	ctx->child = aead;
	ctx->null = null;

	align = crypto_aead_alignmask(aead);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	crypto_aead_set_reqsize(
		tfm,
		sizeof(struct crypto_rfc4543_req_ctx) +
		ALIGN(crypto_aead_reqsize(aead), crypto_tfm_ctx_alignment()) +
		align + 12);

	return 0;

err_free_aead:
	crypto_free_aead(aead);
	return err;
}

static void crypto_rfc4543_exit_tfm(struct crypto_aead *tfm)
{
	struct crypto_rfc4543_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->child);
	crypto_put_default_null_skcipher();
}

static void crypto_rfc4543_free(struct aead_instance *inst)
{
	struct crypto_rfc4543_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_aead(&ctx->aead);

	kfree(inst);
}

static int crypto_rfc4543_create(struct crypto_template *tmpl,
				struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;
	struct crypto_rfc4543_instance_ctx *ctx;
	const char *ccm_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ (CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_AEAD_NEW)) &
	    algt->mask)
		return -EINVAL;

	ccm_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ccm_name))
		return PTR_ERR(ccm_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = aead_instance_ctx(inst);
	spawn = &ctx->aead;
	crypto_set_aead_spawn(spawn, aead_crypto_instance(inst));
	err = crypto_grab_aead(spawn, ccm_name, 0,
			       crypto_requires_sync(algt->type, algt->mask));
	if (err)
		goto out_free_inst;

	alg = crypto_spawn_aead_alg(spawn);

	err = -EINVAL;

	/* Underlying IV size must be 12. */
	if (crypto_aead_alg_ivsize(alg) != 12)
		goto out_drop_alg;

	/* Not a stream cipher? */
	if (alg->base.cra_blocksize != 1)
		goto out_drop_alg;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4543(%s)", alg->base.cra_name) >=
	    CRYPTO_MAX_ALG_NAME ||
	    snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4543(%s)", alg->base.cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_drop_alg;

	inst->alg.base.cra_flags = alg->base.cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_flags |= CRYPTO_ALG_AEAD_NEW;
	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask;

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_rfc4543_ctx);

	inst->alg.ivsize = 8;
	inst->alg.maxauthsize = crypto_aead_alg_maxauthsize(alg);

	inst->alg.init = crypto_rfc4543_init_tfm;
	inst->alg.exit = crypto_rfc4543_exit_tfm;

	inst->alg.setkey = crypto_rfc4543_setkey;
	inst->alg.setauthsize = crypto_rfc4543_setauthsize;
	inst->alg.encrypt = crypto_rfc4543_encrypt;
	inst->alg.decrypt = crypto_rfc4543_decrypt;

	inst->free = crypto_rfc4543_free,

	err = aead_register_instance(tmpl, inst);
	if (err)
		goto out_drop_alg;

out:
	return err;

out_drop_alg:
	crypto_drop_aead(spawn);
out_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_rfc4543_tmpl = {
	.name = "rfc4543",
	.create = crypto_rfc4543_create,
	.module = THIS_MODULE,
};

static int __init crypto_gcm_module_init(void)
{
	int err;

	gcm_zeroes = kzalloc(sizeof(*gcm_zeroes), GFP_KERNEL);
	if (!gcm_zeroes)
		return -ENOMEM;

	sg_init_one(&gcm_zeroes->sg, gcm_zeroes->buf, sizeof(gcm_zeroes->buf));

	err = crypto_register_template(&crypto_gcm_base_tmpl);
	if (err)
		goto out;

	err = crypto_register_template(&crypto_gcm_tmpl);
	if (err)
		goto out_undo_base;

	err = crypto_register_template(&crypto_rfc4106_tmpl);
	if (err)
		goto out_undo_gcm;

	err = crypto_register_template(&crypto_rfc4543_tmpl);
	if (err)
		goto out_undo_rfc4106;

	return 0;

out_undo_rfc4106:
	crypto_unregister_template(&crypto_rfc4106_tmpl);
out_undo_gcm:
	crypto_unregister_template(&crypto_gcm_tmpl);
out_undo_base:
	crypto_unregister_template(&crypto_gcm_base_tmpl);
out:
	kfree(gcm_zeroes);
	return err;
}

static void __exit crypto_gcm_module_exit(void)
{
	kfree(gcm_zeroes);
	crypto_unregister_template(&crypto_rfc4543_tmpl);
	crypto_unregister_template(&crypto_rfc4106_tmpl);
	crypto_unregister_template(&crypto_gcm_tmpl);
	crypto_unregister_template(&crypto_gcm_base_tmpl);
}

module_init(crypto_gcm_module_init);
module_exit(crypto_gcm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Galois/Counter Mode");
MODULE_AUTHOR("Mikko Herranen <mh1@iki.fi>");
MODULE_ALIAS_CRYPTO("gcm_base");
MODULE_ALIAS_CRYPTO("rfc4106");
MODULE_ALIAS_CRYPTO("rfc4543");
MODULE_ALIAS_CRYPTO("gcm");
