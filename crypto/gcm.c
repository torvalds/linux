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
#include <crypto/scatterwalk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

struct gcm_instance_ctx {
	struct crypto_skcipher_spawn ctr;
};

struct crypto_gcm_ctx {
	struct crypto_ablkcipher *ctr;
	struct gf128mul_4k *gf128;
};

struct crypto_rfc4106_ctx {
	struct crypto_aead *child;
	u8 nonce[4];
};

struct crypto_gcm_ghash_ctx {
	u32 bytes;
	u32 flags;
	struct gf128mul_4k *gf128;
	u8 buffer[16];
};

struct crypto_gcm_req_priv_ctx {
	u8 auth_tag[16];
	u8 iauth_tag[16];
	struct scatterlist src[2];
	struct scatterlist dst[2];
	struct crypto_gcm_ghash_ctx ghash;
	struct ablkcipher_request abreq;
};

struct crypto_gcm_setkey_result {
	int err;
	struct completion completion;
};

static inline struct crypto_gcm_req_priv_ctx *crypto_gcm_reqctx(
	struct aead_request *req)
{
	unsigned long align = crypto_aead_alignmask(crypto_aead_reqtfm(req));

	return (void *)PTR_ALIGN((u8 *)aead_request_ctx(req), align + 1);
}

static void crypto_gcm_ghash_init(struct crypto_gcm_ghash_ctx *ctx, u32 flags,
				  struct gf128mul_4k *gf128)
{
	ctx->bytes = 0;
	ctx->flags = flags;
	ctx->gf128 = gf128;
	memset(ctx->buffer, 0, 16);
}

static void crypto_gcm_ghash_update(struct crypto_gcm_ghash_ctx *ctx,
				    const u8 *src, unsigned int srclen)
{
	u8 *dst = ctx->buffer;

	if (ctx->bytes) {
		int n = min(srclen, ctx->bytes);
		u8 *pos = dst + (16 - ctx->bytes);

		ctx->bytes -= n;
		srclen -= n;

		while (n--)
			*pos++ ^= *src++;

		if (!ctx->bytes)
			gf128mul_4k_lle((be128 *)dst, ctx->gf128);
	}

	while (srclen >= 16) {
		crypto_xor(dst, src, 16);
		gf128mul_4k_lle((be128 *)dst, ctx->gf128);
		src += 16;
		srclen -= 16;
	}

	if (srclen) {
		ctx->bytes = 16 - srclen;
		while (srclen--)
			*dst++ ^= *src++;
	}
}

static void crypto_gcm_ghash_update_sg(struct crypto_gcm_ghash_ctx *ctx,
				       struct scatterlist *sg, int len)
{
	struct scatter_walk walk;
	u8 *src;
	int n;

	if (!len)
		return;

	scatterwalk_start(&walk, sg);

	while (len) {
		n = scatterwalk_clamp(&walk, len);

		if (!n) {
			scatterwalk_start(&walk, scatterwalk_sg_next(walk.sg));
			n = scatterwalk_clamp(&walk, len);
		}

		src = scatterwalk_map(&walk, 0);

		crypto_gcm_ghash_update(ctx, src, n);
		len -= n;

		scatterwalk_unmap(src, 0);
		scatterwalk_advance(&walk, n);
		scatterwalk_done(&walk, 0, len);
		if (len)
			crypto_yield(ctx->flags);
	}
}

static void crypto_gcm_ghash_flush(struct crypto_gcm_ghash_ctx *ctx)
{
	u8 *dst = ctx->buffer;

	if (ctx->bytes) {
		u8 *tmp = dst + (16 - ctx->bytes);

		while (ctx->bytes--)
			*tmp++ ^= 0;

		gf128mul_4k_lle((be128 *)dst, ctx->gf128);
	}

	ctx->bytes = 0;
}

static void crypto_gcm_ghash_final_xor(struct crypto_gcm_ghash_ctx *ctx,
				       unsigned int authlen,
				       unsigned int cryptlen, u8 *dst)
{
	u8 *buf = ctx->buffer;
	u128 lengths;

	lengths.a = cpu_to_be64(authlen * 8);
	lengths.b = cpu_to_be64(cryptlen * 8);

	crypto_gcm_ghash_flush(ctx);
	crypto_xor(buf, (u8 *)&lengths, 16);
	gf128mul_4k_lle((be128 *)buf, ctx->gf128);
	crypto_xor(dst, buf, 16);
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

	if (ctx->gf128 != NULL)
		gf128mul_free_4k(ctx->gf128);

	ctx->gf128 = gf128mul_init_4k_lle(&data->hash);

	if (ctx->gf128 == NULL)
		err = -ENOMEM;

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
	u32 flags = req->base.tfm->crt_flags;
	struct crypto_gcm_ghash_ctx *ghash = &pctx->ghash;
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

	crypto_gcm_ghash_init(ghash, flags, ctx->gf128);

	crypto_gcm_ghash_update_sg(ghash, req->assoc, req->assoclen);
	crypto_gcm_ghash_flush(ghash);
}

static int crypto_gcm_hash(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	u8 *auth_tag = pctx->auth_tag;
	struct crypto_gcm_ghash_ctx *ghash = &pctx->ghash;

	crypto_gcm_ghash_update_sg(ghash, req->dst, req->cryptlen);
	crypto_gcm_ghash_final_xor(ghash, req->assoclen, req->cryptlen,
				   auth_tag);

	scatterwalk_map_and_copy(auth_tag, req->dst, req->cryptlen,
				 crypto_aead_authsize(aead), 1);
	return 0;
}

static void crypto_gcm_encrypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	if (!err)
		err = crypto_gcm_hash(req);

	aead_request_complete(req, err);
}

static int crypto_gcm_encrypt(struct aead_request *req)
{
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ablkcipher_request *abreq = &pctx->abreq;
	int err;

	crypto_gcm_init_crypt(abreq, req, req->cryptlen);
	ablkcipher_request_set_callback(abreq, aead_request_flags(req),
					crypto_gcm_encrypt_done, req);

	err = crypto_ablkcipher_encrypt(abreq);
	if (err)
		return err;

	return crypto_gcm_hash(req);
}

static int crypto_gcm_verify(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct crypto_gcm_ghash_ctx *ghash = &pctx->ghash;
	u8 *auth_tag = pctx->auth_tag;
	u8 *iauth_tag = pctx->iauth_tag;
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int cryptlen = req->cryptlen - authsize;

	crypto_gcm_ghash_final_xor(ghash, req->assoclen, cryptlen, auth_tag);

	authsize = crypto_aead_authsize(aead);
	scatterwalk_map_and_copy(iauth_tag, req->src, cryptlen, authsize, 0);
	return memcmp(iauth_tag, auth_tag, authsize) ? -EBADMSG : 0;
}

static void crypto_gcm_decrypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;

	if (!err)
		err = crypto_gcm_verify(req);

	aead_request_complete(req, err);
}

static int crypto_gcm_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_gcm_req_priv_ctx *pctx = crypto_gcm_reqctx(req);
	struct ablkcipher_request *abreq = &pctx->abreq;
	struct crypto_gcm_ghash_ctx *ghash = &pctx->ghash;
	unsigned int cryptlen = req->cryptlen;
	unsigned int authsize = crypto_aead_authsize(aead);
	int err;

	if (cryptlen < authsize)
		return -EINVAL;
	cryptlen -= authsize;

	crypto_gcm_init_crypt(abreq, req, cryptlen);
	ablkcipher_request_set_callback(abreq, aead_request_flags(req),
					crypto_gcm_decrypt_done, req);

	crypto_gcm_ghash_update_sg(ghash, req->src, cryptlen);

	err = crypto_ablkcipher_decrypt(abreq);
	if (err)
		return err;

	return crypto_gcm_verify(req);
}

static int crypto_gcm_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct gcm_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_gcm_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_ablkcipher *ctr;
	unsigned long align;
	int err;

	ctr = crypto_spawn_skcipher(&ictx->ctr);
	err = PTR_ERR(ctr);
	if (IS_ERR(ctr))
		return err;

	ctx->ctr = ctr;
	ctx->gf128 = NULL;

	align = crypto_tfm_alg_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	tfm->crt_aead.reqsize = align +
				sizeof(struct crypto_gcm_req_priv_ctx) +
				crypto_ablkcipher_reqsize(ctr);

	return 0;
}

static void crypto_gcm_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_gcm_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->gf128 != NULL)
		gf128mul_free_4k(ctx->gf128);

	crypto_free_ablkcipher(ctx->ctr);
}

static struct crypto_instance *crypto_gcm_alloc_common(struct rtattr **tb,
						       const char *full_name,
						       const char *ctr_name)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_alg *ctr;
	struct gcm_instance_ctx *ctx;
	int err;

	algt = crypto_get_attr_type(tb);
	err = PTR_ERR(algt);
	if (IS_ERR(algt))
		return ERR_PTR(err);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	ctx = crypto_instance_ctx(inst);
	crypto_set_skcipher_spawn(&ctx->ctr, inst);
	err = crypto_grab_skcipher(&ctx->ctr, ctr_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_free_inst;

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
		     "gcm_base(%s)", ctr->cra_driver_name) >=
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
	return inst;

out_put_ctr:
	crypto_drop_skcipher(&ctx->ctr);
err_free_inst:
	kfree(inst);
	inst = ERR_PTR(err);
	goto out;
}

static struct crypto_instance *crypto_gcm_alloc(struct rtattr **tb)
{
	int err;
	const char *cipher_name;
	char ctr_name[CRYPTO_MAX_ALG_NAME];
	char full_name[CRYPTO_MAX_ALG_NAME];

	cipher_name = crypto_attr_alg_name(tb[1]);
	err = PTR_ERR(cipher_name);
	if (IS_ERR(cipher_name))
		return ERR_PTR(err);

	if (snprintf(ctr_name, CRYPTO_MAX_ALG_NAME, "ctr(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "gcm(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	return crypto_gcm_alloc_common(tb, full_name, ctr_name);
}

static void crypto_gcm_free(struct crypto_instance *inst)
{
	struct gcm_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->ctr);
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
	int err;
	const char *ctr_name;
	char full_name[CRYPTO_MAX_ALG_NAME];

	ctr_name = crypto_attr_alg_name(tb[1]);
	err = PTR_ERR(ctr_name);
	if (IS_ERR(ctr_name))
		return ERR_PTR(err);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "gcm_base(%s)",
		     ctr_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	return crypto_gcm_alloc_common(tb, full_name, ctr_name);
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
	err = PTR_ERR(algt);
	if (IS_ERR(algt))
		return ERR_PTR(err);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	ccm_name = crypto_attr_alg_name(tb[1]);
	err = PTR_ERR(ccm_name);
	if (IS_ERR(ccm_name))
		return ERR_PTR(err);

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

static int __init crypto_gcm_module_init(void)
{
	int err;

	err = crypto_register_template(&crypto_gcm_base_tmpl);
	if (err)
		goto out;

	err = crypto_register_template(&crypto_gcm_tmpl);
	if (err)
		goto out_undo_base;

	err = crypto_register_template(&crypto_rfc4106_tmpl);
	if (err)
		goto out_undo_gcm;

out:
	return err;

out_undo_gcm:
	crypto_unregister_template(&crypto_gcm_tmpl);
out_undo_base:
	crypto_unregister_template(&crypto_gcm_base_tmpl);
	goto out;
}

static void __exit crypto_gcm_module_exit(void)
{
	crypto_unregister_template(&crypto_rfc4106_tmpl);
	crypto_unregister_template(&crypto_gcm_tmpl);
	crypto_unregister_template(&crypto_gcm_base_tmpl);
}

module_init(crypto_gcm_module_init);
module_exit(crypto_gcm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Galois/Counter Mode");
MODULE_AUTHOR("Mikko Herranen <mh1@iki.fi>");
MODULE_ALIAS("gcm_base");
MODULE_ALIAS("rfc4106");
