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

struct crypto_rfc4543_instance_ctx {
	struct crypto_aead_spawn aead;
	struct crypto_skcipher_spawn null;
};

struct crypto_rfc4543_ctx {
	struct crypto_aead *child;
	struct crypto_blkcipher *null;
	u8 nonce[4];
};

struct crypto_rfc4543_req_ctx {
	u8 auth_tag[16];
	u8 assocbuf[32];
	struct scatterlist cipher[1];
	struct scatterlist payload[2];
	struct scatterlist assoc[2];
	struct aead_request subreq;
};

struct crypto_gcm_ghash_ctx {
	unsigned int cryptlen;
	struct scatterlist *src;
	void (*complete)(struct aead_request *req, int err);
};

struct crypto_gcm_req_priv_ctx {
	u8 auth_tag[16];
	u8 iauth_tag[16];
	struct scatterlist src[2];
	struct scatterlist dst[2];
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

static void *gcm_zeroes;

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
	if (err)
		return err;

	crypto_aead_set_flags(aead, crypto_ablkcipher_get_flags(ctr) &
				       CRYPTO_TFM_RES_MASK);

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
	kfree(data);
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

static void crypto_gcm_init_crypt(struct ablkcipher_request *ablk_req,
				  struct aead_request *req,
				  unsigned int cryptlen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct scatterlist *dst;
	__be32 counter = cpu_to_be32(1);

	memset(pctx->auth_tag, 0, sizeof(pctx->auth_tag));
	memcpy(req->iv + 12, &counter, 4);

	sg_init_table(pctx->src, 2);
	sg_set_buf(pctx->src, pctx->auth_tag, sizeof(pctx->auth_tag));
	scatterwalk_sg_chain(pctx->src, 2, req->src);

	dst = pctx->src;
	if (req->src != req->dst) {
		sg_init_table(pctx->dst, 2);
		sg_set_buf(pctx->dst, pctx->auth_tag, sizeof(pctx->auth_tag));
		scatterwalk_sg_chain(pctx->dst, 2, req->dst);
		dst = pctx->dst;
	}

	ablkcipher_request_set_tfm(ablk_req, ctx->ctr);
	ablkcipher_request_set_crypt(ablk_req, pctx->src, dst,
				     cryptlen + sizeof(pctx->auth_tag),
				     req->iv);
}

static inline unsigned int gcm_remain(unsigned int len)
{
	len &= 0xfU;
	return len ? 16 - len : 0;
}

static void gcm_hash_len_done(struct crypto_async_request *areq, int err);
static void gcm_hash_final_done(struct crypto_async_request *areq, int err);

static int gcm_hash_update(struct aead_request *req,
			   struct crypto_gcm_req_priv_ctx *pctx,
			   crypto_completion_t complete,
			   struct scatterlist *src,
			   unsigned int len)
{
	struct ahash_request *ahreq = &pctx->u.ahreq;

	ahash_request_set_callback(ahreq, aead_request_flags(req),
				   complete, req);
	ahash_request_set_crypt(ahreq, src, NULL, len);

	return crypto_ahash_update(ahreq);
}

static int gcm_hash_remain(struct aead_request *req,
			   struct crypto_gcm_req_priv_ctx *pctx,
			   unsigned int remain,
			   crypto_completion_t complete)
{
	struct ahash_request *ahreq = &pctx->u.ahreq;

	ahash_request_set_callback(ahreq, aead_request_flags(req),
				   complete, req);
	sg_init_one(pctx->src, gcm_zeroes, remain);
	ahash_request_set_crypt(ahreq, pctx->src, NULL, remain);

	return crypto_ahash_update(ahreq);
}

static int gcm_hash_len(struct aead_request *req,
			struct crypto_gcm_req_priv_ctx *pctx)
{
	struct ahash_request *ahreq = &pctx->u.ahreq;
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	u128 lengths;

	lengths.a = cpu_to_be64(req->assoclen * 8);
	lengths.b = cpu_to_be64(gctx->cryptlen * 8);
	memcpy(pctx->iauth_tag, &lengths, 16);
	sg_init_one(pctx->src, pctx->iauth_tag, 16);
	ahash_request_set_callback(ahreq, aead_request_flags(req),
				   gcm_hash_len_done, req);
	ahash_request_set_crypt(ahreq, pctx->src,
				NULL, sizeof(lengths));

	return crypto_ahash_update(ahreq);
}

static int gcm_hash_final(struct aead_request *req,
			  struct crypto_gcm_req_priv_ctx *pctx)
{
	struct ahash_request *ahreq = &pctx->u.ahreq;

	ahash_request_set_callback(ahreq, aead_request_flags(req),
				   gcm_hash_final_done, req);
	ahash_request_set_crypt(ahreq, NULL, pctx->iauth_tag, 0);

	return crypto_ahash_final(ahreq);
}

static void __gcm_hash_final_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;

	if (!err)
		crypto_xor(pctx->auth_tag, pctx->iauth_tag, 16);

	gctx->complete(req, err);
}

static void gcm_hash_final_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	__gcm_hash_final_done(req, err);
}

static void __gcm_hash_len_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);

	if (!err) {
		err = gcm_hash_final(req, pctx);
		if (err == -EINPROGRESS || err == -EBUSY)
			return;
	}

	__gcm_hash_final_done(req, err);
}

static void gcm_hash_len_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	__gcm_hash_len_done(req, err);
}

static void __gcm_hash_crypt_remain_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);

	if (!err) {
		err = gcm_hash_len(req, pctx);
		if (err == -EINPROGRESS || err == -EBUSY)
			return;
	}

	__gcm_hash_len_done(req, err);
}

static void gcm_hash_crypt_remain_done(struct crypto_async_request *areq,
				       int err)
{
	struct aead_request *req = areq->data;

	__gcm_hash_crypt_remain_done(req, err);
}

static void __gcm_hash_crypt_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	unsigned int remain;

	if (!err) {
		remain = gcm_remain(gctx->cryptlen);
		BUG_ON(!remain);
		err = gcm_hash_remain(req, pctx, remain,
				      gcm_hash_crypt_remain_done);
		if (err == -EINPROGRESS || err == -EBUSY)
			return;
	}

	__gcm_hash_crypt_remain_done(req, err);
}

static void gcm_hash_crypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	__gcm_hash_crypt_done(req, err);
}

static void __gcm_hash_assoc_remain_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	crypto_completion_t complete;
	unsigned int remain = 0;

	if (!err && gctx->cryptlen) {
		remain = gcm_remain(gctx->cryptlen);
		complete = remain ? gcm_hash_crypt_done :
			gcm_hash_crypt_remain_done;
		err = gcm_hash_update(req, pctx, complete,
				      gctx->src, gctx->cryptlen);
		if (err == -EINPROGRESS || err == -EBUSY)
			return;
	}

	if (remain)
		__gcm_hash_crypt_done(req, err);
	else
		__gcm_hash_crypt_remain_done(req, err);
}

static void gcm_hash_assoc_remain_done(struct crypto_async_request *areq,
				       int err)
{
	struct aead_request *req = areq->data;

	__gcm_hash_assoc_remain_done(req, err);
}

static void __gcm_hash_assoc_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	unsigned int remain;

	if (!err) {
		remain = gcm_remain(req->assoclen);
		BUG_ON(!remain);
		err = gcm_hash_remain(req, pctx, remain,
				      gcm_hash_assoc_remain_done);
		if (err == -EINPROGRESS || err == -EBUSY)
			return;
	}

	__gcm_hash_assoc_remain_done(req, err);
}

static void gcm_hash_assoc_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	__gcm_hash_assoc_done(req, err);
}

static void __gcm_hash_init_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	crypto_completion_t complete;
	unsigned int remain = 0;

	if (!err && req->assoclen) {
		remain = gcm_remain(req->assoclen);
		complete = remain ? gcm_hash_assoc_done :
			gcm_hash_assoc_remain_done;
		err = gcm_hash_update(req, pctx, complete,
				      req->assoc, req->assoclen);
		if (err == -EINPROGRESS || err == -EBUSY)
			return;
	}

	if (remain)
		__gcm_hash_assoc_done(req, err);
	else
		__gcm_hash_assoc_remain_done(req, err);
}

static void gcm_hash_init_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	__gcm_hash_init_done(req, err);
}

static int gcm_hash(struct aead_request *req,
		    struct crypto_gcm_req_priv_ctx *pctx)
{
	struct ahash_request *ahreq = &pctx->u.ahreq;
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	struct crypto_gcm_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	unsigned int remain;
	crypto_completion_t complete;
	int err;

	ahash_request_set_tfm(ahreq, ctx->ghash);

	ahash_request_set_callback(ahreq, aead_request_flags(req),
				   gcm_hash_init_done, req);
	err = crypto_ahash_init(ahreq);
	if (err)
		return err;
	remain = gcm_remain(req->assoclen);
	complete = remain ? gcm_hash_assoc_done : gcm_hash_assoc_remain_done;
	err = gcm_hash_update(req, pctx, complete, req->assoc, req->assoclen);
	if (err)
		return err;
	if (remain) {
		err = gcm_hash_remain(req, pctx, remain,
				      gcm_hash_assoc_remain_done);
		if (err)
			return err;
	}
	remain = gcm_remain(gctx->cryptlen);
	complete = remain ? gcm_hash_crypt_done : gcm_hash_crypt_remain_done;
	err = gcm_hash_update(req, pctx, complete, gctx->src, gctx->cryptlen);
	if (err)
		return err;
	if (remain) {
		err = gcm_hash_remain(req, pctx, remain,
				      gcm_hash_crypt_remain_done);
		if (err)
			return err;
	}
	err = gcm_hash_len(req, pctx);
	if (err)
		return err;
	err = gcm_hash_final(req, pctx);
	if (err)
		return err;

	return 0;
}

static void gcm_enc_copy_hash(struct aead_request *req,
			      struct crypto_gcm_req_priv_ctx *pctx)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	u8 *auth_tag = pctx->auth_tag;

	scatterwalk_map_and_copy(auth_tag, req->dst, req->cryptlen,
				 crypto_aead_authsize(aead), 1);
}

static void gcm_enc_hash_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);

	if (!err)
		gcm_enc_copy_hash(req, pctx);

	aead_request_complete(req, err);
}

static void gcm_encrypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);

	if (!err) {
		err = gcm_hash(req, pctx);
		if (err == -EINPROGRESS || err == -EBUSY)
			return;
		else if (!err) {
			crypto_xor(pctx->auth_tag, pctx->iauth_tag, 16);
			gcm_enc_copy_hash(req, pctx);
		}
	}

	aead_request_complete(req, err);
}

static int crypto_gcm_encrypt(struct aead_request *req)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ablkcipher_request *abreq = &pctx->u.abreq;
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	int err;

	crypto_gcm_init_crypt(abreq, req, req->cryptlen);
	ablkcipher_request_set_callback(abreq, aead_request_flags(req),
					gcm_encrypt_done, req);

	gctx->src = req->dst;
	gctx->cryptlen = req->cryptlen;
	gctx->complete = gcm_enc_hash_done;

	err = crypto_ablkcipher_encrypt(abreq);
	if (err)
		return err;

	err = gcm_hash(req, pctx);
	if (err)
		return err;

	crypto_xor(pctx->auth_tag, pctx->iauth_tag, 16);
	gcm_enc_copy_hash(req, pctx);

	return 0;
}

static int crypto_gcm_verify(struct aead_request *req,
			     struct crypto_gcm_req_priv_ctx *pctx)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	u8 *auth_tag = pctx->auth_tag;
	u8 *iauth_tag = pctx->iauth_tag;
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int cryptlen = req->cryptlen - authsize;

	crypto_xor(auth_tag, iauth_tag, 16);
	scatterwalk_map_and_copy(iauth_tag, req->src, cryptlen, authsize, 0);
	return memcmp(iauth_tag, auth_tag, authsize) ? -EBADMSG : 0;
}

static void gcm_decrypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);

	if (!err)
		err = crypto_gcm_verify(req, pctx);

	aead_request_complete(req, err);
}

static void gcm_dec_hash_done(struct aead_request *req, int err)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ablkcipher_request *abreq = &pctx->u.abreq;
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;

	if (!err) {
		ablkcipher_request_set_callback(abreq, aead_request_flags(req),
						gcm_decrypt_done, req);
		crypto_gcm_init_crypt(abreq, req, gctx->cryptlen);
		err = crypto_ablkcipher_decrypt(abreq);
		if (err == -EINPROGRESS || err == -EBUSY)
			return;
		else if (!err)
			err = crypto_gcm_verify(req, pctx);
	}

	aead_request_complete(req, err);
}

static int crypto_gcm_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ablkcipher_request *abreq = &pctx->u.abreq;
	struct crypto_gcm_ghash_ctx *gctx = &pctx->ghash_ctx;
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int cryptlen = req->cryptlen;
	int err;

	if (cryptlen < authsize)
		return -EINVAL;
	cryptlen -= authsize;

	gctx->src = req->src;
	gctx->cryptlen = cryptlen;
	gctx->complete = gcm_dec_hash_done;

	err = gcm_hash(req, pctx);
	if (err)
		return err;

	ablkcipher_request_set_callback(abreq, aead_request_flags(req),
					gcm_decrypt_done, req);
	crypto_gcm_init_crypt(abreq, req, cryptlen);
	err = crypto_ablkcipher_decrypt(abreq);
	if (err)
		return err;

	return crypto_gcm_verify(req, pctx);
}

static int crypto_gcm_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct gcm_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_gcm_ctx *ctx = crypto_tfm_ctx(tfm);
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

	align = crypto_tfm_alg_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	tfm->crt_aead.reqsize = align +
		offsetof(struct crypto_gcm_req_priv_ctx, u) +
		max(sizeof(struct ablkcipher_request) +
		    crypto_ablkcipher_reqsize(ctr),
		    sizeof(struct ahash_request) +
		    crypto_ahash_reqsize(ghash));

	return 0;

err_free_hash:
	crypto_free_ahash(ghash);
	return err;
}

static void crypto_gcm_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_gcm_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_ahash(ctx->ghash);
	crypto_free_ablkcipher(ctx->ctr);
}

static struct crypto_instance *crypto_gcm_alloc_common(struct rtattr **tb,
						       const char *full_name,
						       const char *ctr_name,
						       const char *ghash_name)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_alg *ctr;
	struct crypto_alg *ghash_alg;
	struct ahash_alg *ghash_ahash_alg;
	struct gcm_instance_ctx *ctx;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	ghash_alg = crypto_find_alg(ghash_name, &crypto_ahash_type,
				    CRYPTO_ALG_TYPE_HASH,
				    CRYPTO_ALG_TYPE_AHASH_MASK);
	if (IS_ERR(ghash_alg))
		return ERR_CAST(ghash_alg);

	err = -ENOMEM;
	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		goto out_put_ghash;

	ctx = crypto_instance_ctx(inst);
	ghash_ahash_alg = container_of(ghash_alg, struct ahash_alg, halg.base);
	err = crypto_init_ahash_spawn(&ctx->ghash, &ghash_ahash_alg->halg,
				      inst);
	if (err)
		goto err_free_inst;

	crypto_set_skcipher_spawn(&ctx->ctr, inst);
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
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "gcm_base(%s,%s)", ctr->cra_driver_name,
		     ghash_alg->cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_put_ctr;

	memcpy(inst->alg.cra_name, full_name, CRYPTO_MAX_ALG_NAME);

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD;
	inst->alg.cra_flags |= ctr->cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = ctr->cra_priority;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = ctr->cra_alignmask | (__alignof__(u64) - 1);
	inst->alg.cra_type = &crypto_aead_type;
	inst->alg.cra_aead.ivsize = 16;
	inst->alg.cra_aead.maxauthsize = 16;
	inst->alg.cra_ctxsize = sizeof(struct crypto_gcm_ctx);
	inst->alg.cra_init = crypto_gcm_init_tfm;
	inst->alg.cra_exit = crypto_gcm_exit_tfm;
	inst->alg.cra_aead.setkey = crypto_gcm_setkey;
	inst->alg.cra_aead.setauthsize = crypto_gcm_setauthsize;
	inst->alg.cra_aead.encrypt = crypto_gcm_encrypt;
	inst->alg.cra_aead.decrypt = crypto_gcm_decrypt;

out:
	crypto_mod_put(ghash_alg);
	return inst;

out_put_ctr:
	crypto_drop_skcipher(&ctx->ctr);
err_drop_ghash:
	crypto_drop_ahash(&ctx->ghash);
err_free_inst:
	kfree(inst);
out_put_ghash:
	inst = ERR_PTR(err);
	goto out;
}

static struct crypto_instance *crypto_gcm_alloc(struct rtattr **tb)
{
	const char *cipher_name;
	char ctr_name[CRYPTO_MAX_ALG_NAME];
	char full_name[CRYPTO_MAX_ALG_NAME];

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return ERR_CAST(cipher_name);

	if (snprintf(ctr_name, CRYPTO_MAX_ALG_NAME, "ctr(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "gcm(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	return crypto_gcm_alloc_common(tb, full_name, ctr_name, "ghash");
}

static void crypto_gcm_free(struct crypto_instance *inst)
{
	struct gcm_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->ctr);
	crypto_drop_ahash(&ctx->ghash);
	kfree(inst);
}

static struct crypto_template crypto_gcm_tmpl = {
	.name = "gcm",
	.alloc = crypto_gcm_alloc,
	.free = crypto_gcm_free,
	.module = THIS_MODULE,
};

static struct crypto_instance *crypto_gcm_base_alloc(struct rtattr **tb)
{
	const char *ctr_name;
	const char *ghash_name;
	char full_name[CRYPTO_MAX_ALG_NAME];

	ctr_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ctr_name))
		return ERR_CAST(ctr_name);

	ghash_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(ghash_name))
		return ERR_CAST(ghash_name);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "gcm_base(%s,%s)",
		     ctr_name, ghash_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	return crypto_gcm_alloc_common(tb, full_name, ctr_name, ghash_name);
}

static struct crypto_template crypto_gcm_base_tmpl = {
	.name = "gcm_base",
	.alloc = crypto_gcm_base_alloc,
	.free = crypto_gcm_free,
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
	struct aead_request *subreq = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4106_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_aead *child = ctx->child;
	u8 *iv = PTR_ALIGN((u8 *)(subreq + 1) + crypto_aead_reqsize(child),
			   crypto_aead_alignmask(child) + 1);

	memcpy(iv, ctx->nonce, 4);
	memcpy(iv + 4, req->iv, 8);

	aead_request_set_tfm(subreq, child);
	aead_request_set_callback(subreq, req->base.flags, req->base.complete,
				  req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst, req->cryptlen, iv);
	aead_request_set_assoc(subreq, req->assoc, req->assoclen);

	return subreq;
}

static int crypto_rfc4106_encrypt(struct aead_request *req)
{
	req = crypto_rfc4106_crypt(req);

	return crypto_aead_encrypt(req);
}

static int crypto_rfc4106_decrypt(struct aead_request *req)
{
	req = crypto_rfc4106_crypt(req);

	return crypto_aead_decrypt(req);
}

static int crypto_rfc4106_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_aead_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_rfc4106_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_aead *aead;
	unsigned long align;

	aead = crypto_spawn_aead(spawn);
	if (IS_ERR(aead))
		return PTR_ERR(aead);

	ctx->child = aead;

	align = crypto_aead_alignmask(aead);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	tfm->crt_aead.reqsize = sizeof(struct aead_request) +
				ALIGN(crypto_aead_reqsize(aead),
				      crypto_tfm_ctx_alignment()) +
				align + 16;

	return 0;
}

static void crypto_rfc4106_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_rfc4106_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_aead(ctx->child);
}

static struct crypto_instance *crypto_rfc4106_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct crypto_alg *alg;
	const char *ccm_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	ccm_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ccm_name))
		return ERR_CAST(ccm_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	spawn = crypto_instance_ctx(inst);
	crypto_set_aead_spawn(spawn, inst);
	err = crypto_grab_aead(spawn, ccm_name, 0,
			       crypto_requires_sync(algt->type, algt->mask));
	if (err)
		goto out_free_inst;

	alg = crypto_aead_spawn_alg(spawn);

	err = -EINVAL;

	/* We only support 16-byte blocks. */
	if (alg->cra_aead.ivsize != 16)
		goto out_drop_alg;

	/* Not a stream cipher? */
	if (alg->cra_blocksize != 1)
		goto out_drop_alg;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4106(%s)", alg->cra_name) >= CRYPTO_MAX_ALG_NAME ||
	    snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4106(%s)", alg->cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_drop_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD;
	inst->alg.cra_flags |= alg->cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_nivaead_type;

	inst->alg.cra_aead.ivsize = 8;
	inst->alg.cra_aead.maxauthsize = 16;

	inst->alg.cra_ctxsize = sizeof(struct crypto_rfc4106_ctx);

	inst->alg.cra_init = crypto_rfc4106_init_tfm;
	inst->alg.cra_exit = crypto_rfc4106_exit_tfm;

	inst->alg.cra_aead.setkey = crypto_rfc4106_setkey;
	inst->alg.cra_aead.setauthsize = crypto_rfc4106_setauthsize;
	inst->alg.cra_aead.encrypt = crypto_rfc4106_encrypt;
	inst->alg.cra_aead.decrypt = crypto_rfc4106_decrypt;

	inst->alg.cra_aead.geniv = "seqiv";

out:
	return inst;

out_drop_alg:
	crypto_drop_aead(spawn);
out_free_inst:
	kfree(inst);
	inst = ERR_PTR(err);
	goto out;
}

static void crypto_rfc4106_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_template crypto_rfc4106_tmpl = {
	.name = "rfc4106",
	.alloc = crypto_rfc4106_alloc,
	.free = crypto_rfc4106_free,
	.module = THIS_MODULE,
};

static inline struct crypto_rfc4543_req_ctx *crypto_rfc4543_reqctx(
	struct aead_request *req)
{
	unsigned long align = crypto_aead_alignmask(crypto_aead_reqtfm(req));

	return (void *)PTR_ALIGN((u8 *)aead_request_ctx(req), align + 1);
}

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

static void crypto_rfc4543_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4543_req_ctx *rctx = crypto_rfc4543_reqctx(req);

	if (!err) {
		scatterwalk_map_and_copy(rctx->auth_tag, req->dst,
					 req->cryptlen,
					 crypto_aead_authsize(aead), 1);
	}

	aead_request_complete(req, err);
}

static struct aead_request *crypto_rfc4543_crypt(struct aead_request *req,
						 bool enc)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4543_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_rfc4543_req_ctx *rctx = crypto_rfc4543_reqctx(req);
	struct aead_request *subreq = &rctx->subreq;
	struct scatterlist *src = req->src;
	struct scatterlist *cipher = rctx->cipher;
	struct scatterlist *payload = rctx->payload;
	struct scatterlist *assoc = rctx->assoc;
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int assoclen = req->assoclen;
	struct page *srcp;
	u8 *vsrc;
	u8 *iv = PTR_ALIGN((u8 *)(rctx + 1) + crypto_aead_reqsize(ctx->child),
			   crypto_aead_alignmask(ctx->child) + 1);

	memcpy(iv, ctx->nonce, 4);
	memcpy(iv + 4, req->iv, 8);

	/* construct cipher/plaintext */
	if (enc)
		memset(rctx->auth_tag, 0, authsize);
	else
		scatterwalk_map_and_copy(rctx->auth_tag, src,
					 req->cryptlen - authsize,
					 authsize, 0);

	sg_init_one(cipher, rctx->auth_tag, authsize);

	/* construct the aad */
	srcp = sg_page(src);
	vsrc = PageHighMem(srcp) ? NULL : page_address(srcp) + src->offset;

	sg_init_table(payload, 2);
	sg_set_buf(payload, req->iv, 8);
	scatterwalk_crypto_chain(payload, src, vsrc == req->iv + 8, 2);
	assoclen += 8 + req->cryptlen - (enc ? 0 : authsize);

	if (req->assoc->length == req->assoclen) {
		sg_init_table(assoc, 2);
		sg_set_page(assoc, sg_page(req->assoc), req->assoc->length,
			    req->assoc->offset);
	} else {
		BUG_ON(req->assoclen > sizeof(rctx->assocbuf));

		scatterwalk_map_and_copy(rctx->assocbuf, req->assoc, 0,
					 req->assoclen, 0);

		sg_init_table(assoc, 2);
		sg_set_buf(assoc, rctx->assocbuf, req->assoclen);
	}
	scatterwalk_crypto_chain(assoc, payload, 0, 2);

	aead_request_set_tfm(subreq, ctx->child);
	aead_request_set_callback(subreq, req->base.flags, crypto_rfc4543_done,
				  req);
	aead_request_set_crypt(subreq, cipher, cipher, enc ? 0 : authsize, iv);
	aead_request_set_assoc(subreq, assoc, assoclen);

	return subreq;
}

static int crypto_rfc4543_copy_src_to_dst(struct aead_request *req, bool enc)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4543_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int nbytes = req->cryptlen - (enc ? 0 : authsize);
	struct blkcipher_desc desc = {
		.tfm = ctx->null,
	};

	return crypto_blkcipher_encrypt(&desc, req->dst, req->src, nbytes);
}

static int crypto_rfc4543_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4543_req_ctx *rctx = crypto_rfc4543_reqctx(req);
	struct aead_request *subreq;
	int err;

	if (req->src != req->dst) {
		err = crypto_rfc4543_copy_src_to_dst(req, true);
		if (err)
			return err;
	}

	subreq = crypto_rfc4543_crypt(req, true);
	err = crypto_aead_encrypt(subreq);
	if (err)
		return err;

	scatterwalk_map_and_copy(rctx->auth_tag, req->dst, req->cryptlen,
				 crypto_aead_authsize(aead), 1);

	return 0;
}

static int crypto_rfc4543_decrypt(struct aead_request *req)
{
	int err;

	if (req->src != req->dst) {
		err = crypto_rfc4543_copy_src_to_dst(req, false);
		if (err)
			return err;
	}

	req = crypto_rfc4543_crypt(req, false);

	return crypto_aead_decrypt(req);
}

static int crypto_rfc4543_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_rfc4543_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_aead_spawn *spawn = &ictx->aead;
	struct crypto_rfc4543_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_aead *aead;
	struct crypto_blkcipher *null;
	unsigned long align;
	int err = 0;

	aead = crypto_spawn_aead(spawn);
	if (IS_ERR(aead))
		return PTR_ERR(aead);

	null = crypto_spawn_blkcipher(&ictx->null.base);
	err = PTR_ERR(null);
	if (IS_ERR(null))
		goto err_free_aead;

	ctx->child = aead;
	ctx->null = null;

	align = crypto_aead_alignmask(aead);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	tfm->crt_aead.reqsize = sizeof(struct crypto_rfc4543_req_ctx) +
				ALIGN(crypto_aead_reqsize(aead),
				      crypto_tfm_ctx_alignment()) +
				align + 16;

	return 0;

err_free_aead:
	crypto_free_aead(aead);
	return err;
}

static void crypto_rfc4543_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_rfc4543_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_aead(ctx->child);
	crypto_free_blkcipher(ctx->null);
}

static struct crypto_instance *crypto_rfc4543_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct crypto_alg *alg;
	struct crypto_rfc4543_instance_ctx *ctx;
	const char *ccm_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	ccm_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ccm_name))
		return ERR_CAST(ccm_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	ctx = crypto_instance_ctx(inst);
	spawn = &ctx->aead;
	crypto_set_aead_spawn(spawn, inst);
	err = crypto_grab_aead(spawn, ccm_name, 0,
			       crypto_requires_sync(algt->type, algt->mask));
	if (err)
		goto out_free_inst;

	alg = crypto_aead_spawn_alg(spawn);

	crypto_set_skcipher_spawn(&ctx->null, inst);
	err = crypto_grab_skcipher(&ctx->null, "ecb(cipher_null)", 0,
				   CRYPTO_ALG_ASYNC);
	if (err)
		goto out_drop_alg;

	crypto_skcipher_spawn_alg(&ctx->null);

	err = -EINVAL;

	/* We only support 16-byte blocks. */
	if (alg->cra_aead.ivsize != 16)
		goto out_drop_ecbnull;

	/* Not a stream cipher? */
	if (alg->cra_blocksize != 1)
		goto out_drop_ecbnull;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4543(%s)", alg->cra_name) >= CRYPTO_MAX_ALG_NAME ||
	    snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4543(%s)", alg->cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_drop_ecbnull;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD;
	inst->alg.cra_flags |= alg->cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_nivaead_type;

	inst->alg.cra_aead.ivsize = 8;
	inst->alg.cra_aead.maxauthsize = 16;

	inst->alg.cra_ctxsize = sizeof(struct crypto_rfc4543_ctx);

	inst->alg.cra_init = crypto_rfc4543_init_tfm;
	inst->alg.cra_exit = crypto_rfc4543_exit_tfm;

	inst->alg.cra_aead.setkey = crypto_rfc4543_setkey;
	inst->alg.cra_aead.setauthsize = crypto_rfc4543_setauthsize;
	inst->alg.cra_aead.encrypt = crypto_rfc4543_encrypt;
	inst->alg.cra_aead.decrypt = crypto_rfc4543_decrypt;

	inst->alg.cra_aead.geniv = "seqiv";

out:
	return inst;

out_drop_ecbnull:
	crypto_drop_skcipher(&ctx->null);
out_drop_alg:
	crypto_drop_aead(spawn);
out_free_inst:
	kfree(inst);
	inst = ERR_PTR(err);
	goto out;
}

static void crypto_rfc4543_free(struct crypto_instance *inst)
{
	struct crypto_rfc4543_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_aead(&ctx->aead);
	crypto_drop_skcipher(&ctx->null);

	kfree(inst);
}

static struct crypto_template crypto_rfc4543_tmpl = {
	.name = "rfc4543",
	.alloc = crypto_rfc4543_alloc,
	.free = crypto_rfc4543_free,
	.module = THIS_MODULE,
};

static int __init crypto_gcm_module_init(void)
{
	int err;

	gcm_zeroes = kzalloc(16, GFP_KERNEL);
	if (!gcm_zeroes)
		return -ENOMEM;

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
