/*
 * GCM: Galois/Counter Mode.
 *
 * Copyright (c) 2007 Nokia Siemens Networks - Mikko Herranen <mh1@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <crypto/algapi.h>
#include <crypto/gf128mul.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "scatterwalk.h"

struct gcm_instance_ctx {
	struct crypto_spawn ctr;
};

struct crypto_gcm_ctx {
	struct crypto_ablkcipher *ctr;
	struct gf128mul_4k *gf128;
};

struct crypto_gcm_ghash_ctx {
	u32 bytes;
	u32 flags;
	struct gf128mul_4k *gf128;
	u8 buffer[16];
};

struct crypto_gcm_req_priv_ctx {
	u8 auth_tag[16];
	u8 counter[16];
	struct crypto_gcm_ghash_ctx ghash;
};

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

	scatterwalk_start(&walk, sg);

	while (len) {
		n = scatterwalk_clamp(&walk, len);

		if (!n) {
			scatterwalk_start(&walk, sg_next(walk.sg));
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

static inline void crypto_gcm_set_counter(u8 *counterblock, u32 value)
{
	*((u32 *)&counterblock[12]) = cpu_to_be32(value);
}

static int crypto_gcm_encrypt_counter(struct crypto_aead *aead, u8 *block,
				       u32 value, const u8 *iv)
{
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_ablkcipher *ctr = ctx->ctr;
	struct ablkcipher_request req;
	struct scatterlist sg;
	u8 counterblock[16];

	if (iv == NULL)
		memset(counterblock, 0, 12);
	else
		memcpy(counterblock, iv, 12);

	crypto_gcm_set_counter(counterblock, value);

	sg_init_one(&sg, block, 16);
	ablkcipher_request_set_tfm(&req, ctr);
	ablkcipher_request_set_crypt(&req, &sg, &sg, 16, counterblock);
	ablkcipher_request_set_callback(&req, 0, NULL, NULL);
	memset(block, 0, 16);
	return crypto_ablkcipher_encrypt(&req);
}

static int crypto_gcm_setkey(struct crypto_aead *aead, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_ablkcipher *ctr = ctx->ctr;
	int alignmask = crypto_ablkcipher_alignmask(ctr);
	u8 alignbuf[16+alignmask];
	u8 *hash = (u8 *)ALIGN((unsigned long)alignbuf, alignmask+1);
	int err = 0;

	crypto_ablkcipher_clear_flags(ctr, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(ctr, crypto_aead_get_flags(aead) &
				   CRYPTO_TFM_REQ_MASK);

	err = crypto_ablkcipher_setkey(ctr, key, keylen);
	if (err)
		goto out;

	crypto_aead_set_flags(aead, crypto_ablkcipher_get_flags(ctr) &
				       CRYPTO_TFM_RES_MASK);

	err = crypto_gcm_encrypt_counter(aead, hash, -1, NULL);
	if (err)
		goto out;

	if (ctx->gf128 != NULL)
		gf128mul_free_4k(ctx->gf128);

	ctx->gf128 = gf128mul_init_4k_lle((be128 *)hash);

	if (ctx->gf128 == NULL)
		err = -ENOMEM;

 out:
	return err;
}

static int crypto_gcm_init_crypt(struct ablkcipher_request *ablk_req,
				  struct aead_request *req,
				  void (*done)(struct crypto_async_request *,
					       int))
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_gcm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_gcm_req_priv_ctx *pctx = aead_request_ctx(req);
	u32 flags = req->base.tfm->crt_flags;
	u8 *auth_tag = pctx->auth_tag;
	u8 *counter = pctx->counter;
	struct crypto_gcm_ghash_ctx *ghash = &pctx->ghash;
	int err = 0;

	ablkcipher_request_set_tfm(ablk_req, ctx->ctr);
	ablkcipher_request_set_callback(ablk_req, aead_request_flags(req),
					done, req);
	ablkcipher_request_set_crypt(ablk_req, req->src, req->dst,
				     req->cryptlen, counter);

	err = crypto_gcm_encrypt_counter(aead, auth_tag, 0, req->iv);
	if (err)
		goto out;

	memcpy(counter, req->iv, 12);
	crypto_gcm_set_counter(counter, 1);

	crypto_gcm_ghash_init(ghash, flags, ctx->gf128);

	if (req->assoclen) {
		crypto_gcm_ghash_update_sg(ghash, req->assoc, req->assoclen);
		crypto_gcm_ghash_flush(ghash);
	}

 out:
	return err;
}

static void crypto_gcm_encrypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;
	struct crypto_gcm_req_priv_ctx *pctx = aead_request_ctx(req);
	u8 *auth_tag = pctx->auth_tag;
	struct crypto_gcm_ghash_ctx *ghash = &pctx->ghash;

	crypto_gcm_ghash_update_sg(ghash, req->dst, req->cryptlen);
	crypto_gcm_ghash_final_xor(ghash, req->assoclen, req->cryptlen,
				   auth_tag);

	aead_request_complete(req, err);
}

static int crypto_gcm_encrypt(struct aead_request *req)
{
	struct ablkcipher_request abreq;
	struct crypto_gcm_req_priv_ctx *pctx = aead_request_ctx(req);
	u8 *auth_tag = pctx->auth_tag;
	struct crypto_gcm_ghash_ctx *ghash = &pctx->ghash;
	int err = 0;

	err = crypto_gcm_init_crypt(&abreq, req, crypto_gcm_encrypt_done);
	if (err)
		return err;

	if (req->cryptlen) {
		err = crypto_ablkcipher_encrypt(&abreq);
		if (err)
			return err;

		crypto_gcm_ghash_update_sg(ghash, req->dst, req->cryptlen);
	}

	crypto_gcm_ghash_final_xor(ghash, req->assoclen, req->cryptlen,
				   auth_tag);

	return err;
}

static void crypto_gcm_decrypt_done(struct crypto_async_request *areq, int err)
{
	aead_request_complete(areq->data, err);
}

static int crypto_gcm_decrypt(struct aead_request *req)
{
	struct ablkcipher_request abreq;
	struct crypto_gcm_req_priv_ctx *pctx = aead_request_ctx(req);
	u8 *auth_tag = pctx->auth_tag;
	struct crypto_gcm_ghash_ctx *ghash = &pctx->ghash;
	u8 tag[16];
	int err;

	if (!req->cryptlen)
		return -EINVAL;

	memcpy(tag, auth_tag, 16);
	err = crypto_gcm_init_crypt(&abreq, req, crypto_gcm_decrypt_done);
	if (err)
		return err;

	crypto_gcm_ghash_update_sg(ghash, req->src, req->cryptlen);
	crypto_gcm_ghash_final_xor(ghash, req->assoclen, req->cryptlen,
				   auth_tag);

	if (memcmp(tag, auth_tag, 16))
		return -EINVAL;

	return crypto_ablkcipher_decrypt(&abreq);
}

static int crypto_gcm_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct gcm_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_gcm_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_ablkcipher *ctr;
	unsigned long align;
	int err;

	ctr = crypto_spawn_ablkcipher(&ictx->ctr);
	err = PTR_ERR(ctr);
	if (IS_ERR(ctr))
		return err;

	ctx->ctr = ctr;
	ctx->gf128 = NULL;

	align = max_t(unsigned long, crypto_ablkcipher_alignmask(ctr),
		      __alignof__(u32) - 1);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	tfm->crt_aead.reqsize = align + sizeof(struct crypto_gcm_req_priv_ctx);

	return 0;
}

static void crypto_gcm_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_gcm_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->gf128 != NULL)
		gf128mul_free_4k(ctx->gf128);

	crypto_free_ablkcipher(ctx->ctr);
}

static struct crypto_instance *crypto_gcm_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *ctr;
	struct crypto_alg *cipher;
	struct gcm_instance_ctx *ctx;
	int err;
	char ctr_name[CRYPTO_MAX_ALG_NAME];

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AEAD);
	if (err)
		return ERR_PTR(err);

	cipher = crypto_attr_alg(tb[1], CRYPTO_ALG_TYPE_CIPHER,
			      CRYPTO_ALG_TYPE_MASK);

	inst = ERR_PTR(PTR_ERR(cipher));
	if (IS_ERR(cipher))
		return inst;

	inst = ERR_PTR(ENAMETOOLONG);
	if (snprintf(
		    ctr_name, CRYPTO_MAX_ALG_NAME,
		    "ctr(%s,0,16,4)", cipher->cra_name) >= CRYPTO_MAX_ALG_NAME)
		return inst;

	ctr = crypto_alg_mod_lookup(ctr_name, CRYPTO_ALG_TYPE_BLKCIPHER,
				    CRYPTO_ALG_TYPE_MASK);

	if (IS_ERR(ctr))
		return ERR_PTR(PTR_ERR(ctr));

	if (cipher->cra_blocksize != 16)
		goto out_put_ctr;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	err = -ENOMEM;
	if (!inst)
		goto out_put_ctr;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "gcm(%s)", cipher->cra_name) >= CRYPTO_MAX_ALG_NAME ||
	    snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "gcm(%s)", cipher->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;


	ctx = crypto_instance_ctx(inst);
	err = crypto_init_spawn(&ctx->ctr, ctr, inst, CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto err_free_inst;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = ctr->cra_priority;
	inst->alg.cra_blocksize = 16;
	inst->alg.cra_alignmask = __alignof__(u32) - 1;
	inst->alg.cra_type = &crypto_aead_type;
	inst->alg.cra_aead.ivsize = 12;
	inst->alg.cra_aead.maxauthsize = 16;
	inst->alg.cra_ctxsize = sizeof(struct crypto_gcm_ctx);
	inst->alg.cra_init = crypto_gcm_init_tfm;
	inst->alg.cra_exit = crypto_gcm_exit_tfm;
	inst->alg.cra_aead.setkey = crypto_gcm_setkey;
	inst->alg.cra_aead.encrypt = crypto_gcm_encrypt;
	inst->alg.cra_aead.decrypt = crypto_gcm_decrypt;

out:
	crypto_mod_put(ctr);
	return inst;
err_free_inst:
	kfree(inst);
out_put_ctr:
	inst = ERR_PTR(err);
	goto out;
}

static void crypto_gcm_free(struct crypto_instance *inst)
{
	struct gcm_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_spawn(&ctx->ctr);
	kfree(inst);
}

static struct crypto_template crypto_gcm_tmpl = {
	.name = "gcm",
	.alloc = crypto_gcm_alloc,
	.free = crypto_gcm_free,
	.module = THIS_MODULE,
};

static int __init crypto_gcm_module_init(void)
{
	return crypto_register_template(&crypto_gcm_tmpl);
}

static void __exit crypto_gcm_module_exit(void)
{
	crypto_unregister_template(&crypto_gcm_tmpl);
}

module_init(crypto_gcm_module_init);
module_exit(crypto_gcm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Galois/Counter Mode");
MODULE_AUTHOR("Mikko Herranen <mh1@iki.fi>");
